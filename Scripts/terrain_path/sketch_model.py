"""Modelo 3D del mapa a partir de un boceto (terrain_path/sketch.py): la misma interfaz que PathModel
para el resto de la cadena (terrain_vol.mesh.build_chunk, vertex_colors, export TNTM2).

Relieve 2D en una rejilla de 1 m del rectangulo del mapa (cols x rows trozos de 100 m):
  - dentro de las zonas y del mar, el suelo de sketch.floor_heights (mesetas con cuestas);
  - fuera, la pared con la seccion de C01 (field.section: pie casi vertical, ladera de 32-57 grados y
    remate natural), calculada por separado desde cada clase de zona y unida con un minimo suave, asi
    que entre dos zonas a distinta cota no queda ningun salto en la bisectriz. El remate es mas alto que
    en C01 (RIM_M) y, lejos de las zonas, baja al fondo de vistas de C01 (dunas y lagos);
  - sobre el tramo cubierto de cada tunel, roca hasta la clave + Tunnel.rock_m.
Densidad 3D: relieve - Z, rugosidad de pared por encima del pie y el hueco de cada tunel (seccion de
PathModel._carve, con la altura de la cueva acotada)."""

from __future__ import annotations

import math
from dataclasses import replace
from pathlib import Path
from types import SimpleNamespace

import numpy as np
from scipy import ndimage
from scipy.spatial import cKDTree

from terrain_vol.density import Fields, smooth
from terrain_vol.noise import Fbm2D, ValueNoise2D, ValueNoise3D

from . import field
from .curves import resample
from .layout import CELL_M, CELL_SAMPLES, STEP_XY_M, WATER_M, Z_MAX_M, Z_MIN_M
from .sketch import SEA, WALL, SketchSpec, class_map, floor_heights, load_rgb
from .style import PathStyle

SCRIPTS = Path(__file__).resolve().parent.parent
PAD = 2                                  # celdas de margen alrededor del mapa en la rejilla global
RIM_M = (9.0, 13.0)                      # remate de la pared sobre el suelo (C01: 4,5-6,5 m)
HILL_AMP_M = (2.0, 16.0)                 # lomas junto a las zonas (C01: 1-14 m)
FADE_REACH_M = (12.0, 50.0)              # distancia a la que las lomas se funden con las vistas
CREST_K_M = 2.5                          # minimo suave entre las paredes de dos clases (sin cuchilla)
TUNNEL_MOUTH_M = 3.0                     # el hueco sigue esto mas alla del tramo cubierto
ZONE_KEYS = ("cliffs", "marsh", "algae", "beach")


class _Noise2D(ValueNoise2D):
    """ValueNoise2D sobre un rectangulo dado (la de terrain_vol cubre el mapa de 600 m)."""

    def __init__(self, rng: np.random.Generator, wavelength: float, lo: float, hi: float):
        self.wavelength = wavelength
        self.origin = lo
        cells = int(math.ceil((hi - lo) / wavelength)) + 3
        self.lattice = rng.uniform(-1.0, 1.0, (cells, cells))


class _Fbm(Fbm2D):
    def __init__(self, rng, wavelength: float, octaves: int, lo: float, hi: float, gain: float = 0.5):
        self.layers = [(gain ** o, _Noise2D(rng, wavelength / (2 ** o), lo, hi)) for o in range(octaves)]
        self.norm = sum(a for a, _ in self.layers)


class _Noise3D(ValueNoise3D):
    def __init__(self, rng, wavelength: float, lo: float, hi: float):
        self.wavelength = wavelength
        self.origin = (lo, lo, Z_MIN_M - 50.0)
        cells_xy = int(math.ceil((hi - lo) / wavelength)) + 3
        cells_z = int(math.ceil((Z_MAX_M + 40.0 - Z_MIN_M + 100.0) / wavelength)) + 3
        self.lattice = rng.uniform(-1.0, 1.0, (cells_xy, cells_xy, cells_z))


class SketchModel:
    def __init__(self, spec: SketchSpec):
        self.spec = spec
        self.seed = spec.seed
        self.style = replace(PathStyle(), name=spec.name, description=spec.description, castles=0)
        self.x_min, self.y_min = -CELL_M / 2.0, -CELL_M / 2.0
        self.x_max, self.y_max = self.x_min + spec.rows * CELL_M, self.y_min + spec.cols * CELL_M
        lo, hi = -1300.0, max(self.x_max, self.y_max) + 1300.0       # cubre tambien la corona
        rng = np.random.default_rng(spec.seed)
        self.n_floor = _Fbm(rng, 25.0, 2, lo, hi)
        self.n_rim = _Fbm(rng, 45.0, 3, lo, hi)
        self.n_top = _Fbm(rng, 14.0, 3, lo, hi)
        self.n_pond = _Fbm(rng, 34.0, 3, lo, hi)
        self.n_big = _Fbm(rng, 160.0, 2, lo, hi)
        self.n_dune_warp = _Fbm(rng, 70.0, 2, lo, hi)
        self.n_dune_mix = _Fbm(rng, 120.0, 2, lo, hi)
        self.n_dune_amp = _Fbm(rng, 90.0, 2, lo, hi)
        self.n_wall2d = _Fbm(rng, 11.0, 3, lo, hi)
        self.n_band = _Fbm(rng, 80.0, 2, lo, hi)
        self.n_run = _Fbm(rng, 50.0, 2, lo, hi)
        self.n_crest_big = _Fbm(rng, 60.0, 2, lo, hi)
        self.n_crest_mid = _Fbm(rng, 18.0, 3, lo, hi)
        self.n_crest_fine = _Fbm(rng, 6.0, 2, lo, hi)
        self.n_tunnel = _Fbm(rng, 9.0, 2, lo, hi)
        self.n_wall3d = _Noise3D(rng, 6.5, -200.0, max(self.x_max, self.y_max) + 200.0)
        self.dune_angle = float(rng.uniform(0.0, np.pi))
        self.dune_angle_2 = self.dune_angle + float(rng.uniform(0.6, 1.1))
        self.canyon_field = None
        # Boceto -> clases y suelo (en pixeles del boceto).
        rgb = load_rgb(SCRIPTS / spec.image)
        self.labels_px = class_map(spec, rgb)
        self.floor_px, self.contacts = floor_heights(spec, self.labels_px, spec.m_per_px)
        # Rejilla del mundo (X Norte = filas, Y Este = columnas).
        self.ax_x = self.x_min + STEP_XY_M * np.arange(-PAD, int(round(spec.rows * CELL_M)) + PAD + 1)
        self.ax_y = self.y_min + STEP_XY_M * np.arange(-PAD, int(round(spec.cols * CELL_M)) + PAD + 1)
        X, Y = np.meshgrid(self.ax_x, self.ax_y, indexing="ij")
        self.labels = self._sample_labels(X, Y)
        self._build_tunnels()
        self.grid = self._fields(X, Y)
        self._color_grid()
        sx, sy = spec.px_to_world(*spec.start_px)
        self.start = np.array([float(sx), float(sy)])
        self.end = self._end_point()
        self.route = SimpleNamespace(points=np.array([self.start, self.end]))
        self.zones = self
        self.high_tint = 0.25
        self.trail_strength = 0.6
        self.trail_color = (0.34, 0.24, 0.13)
        self.cliff_band = (0.12, 0.3)
        self.wall_color_mix = 1.0
        self.wall_strata = 0.0
        self.wall_noise = 0.45

    # -- boceto en la rejilla del mundo ---------------------------------------------------------
    def _px(self, X, Y):
        return self.spec.world_to_px(X, Y)

    def _sample_labels(self, X, Y) -> np.ndarray:
        xp, yp = self._px(X, Y)
        h, w = self.labels_px.shape
        i = np.clip(np.rint(yp).astype(int), 0, h - 1)
        j = np.clip(np.rint(xp).astype(int), 0, w - 1)
        out = self.labels_px[i, j].copy()
        out[(yp < 0)] = SEA                                   # al norte del boceto sigue el mar
        out[(yp >= h) | (xp < 0) | (xp >= w)] = WALL
        return out

    def _sample_px(self, raster: np.ndarray, X, Y, order: int = 1) -> np.ndarray:
        xp, yp = self._px(X, Y)
        return ndimage.map_coordinates(raster, [yp, xp], order=order, mode="nearest")

    def _class_floor(self, cls: int) -> np.ndarray:
        """Suelo de la clase cls prolongado fuera de ella (el del pixel de la clase mas cercano)."""
        mask = self.labels_px == cls
        _, (ii, jj) = ndimage.distance_transform_edt(~mask, return_indices=True)
        return np.nan_to_num(self.floor_px[ii, jj], nan=0.0)

    # -- tuneles ---------------------------------------------------------------------------------
    def _build_tunnels(self) -> None:
        cats = {k: [] for k in ("p", "floor", "half", "height", "t", "rock", "span")}
        self.tunnel_axes = []
        for tunnel in self.spec.tunnels:
            px = np.array(tunnel.axis_px, dtype=np.float64)
            wx, wy = self.spec.px_to_world(px[:, 0], px[:, 1])
            pts, s = resample(np.stack([wx, wy], axis=1), 0.5)
            length = s[-1]
            s0, s1 = (f * length for f in tunnel.covered)
            z0, z1 = (float(self._floor_at(*pts[k])) for k in (0, -1))
            floor = np.interp(s, [s0, s1], [z0, z1])
            # La roca (sketch.tunnel_rock_mask) empieza un radio antes del tramo cubierto: el hueco tambien.
            reach = tunnel.half_width_m + max(tunnel.rock_m, 4.0) + TUNNEL_MOUTH_M
            keep = (s >= s0 - reach - 4.0) & (s <= s1 + reach + 4.0)
            span = (s >= s0 - reach) & (s <= s1 + reach)
            t = (s - s0) / max(s1 - s0, 1e-9)
            self.tunnel_axes.append({"name": tunnel.name, "pts": pts, "s": s, "floor": floor, "covered": (s0, s1),
                                     "half": tunnel.half_width_m, "height": tunnel.height_m, "rock": tunnel.rock_m,
                                     "z": (z0, z1)})
            cats["p"].append(pts[keep])
            cats["floor"].append(floor[keep])
            cats["t"].append(t[keep])
            cats["span"].append(span[keep].astype(float))
            for key, value in (("half", tunnel.half_width_m), ("height", tunnel.height_m), ("rock", tunnel.rock_m)):
                cats[key].append(np.full(int(keep.sum()), value))
        self.tunnel_tree = cKDTree(np.vstack(cats["p"]))
        self.tunnel_cat = {k: np.concatenate(v) for k, v in cats.items() if k != "p"}

    def _floor_at(self, x: float, y: float) -> float:
        filled = self._filled_floor()
        return float(self._sample_px(filled, np.array([x]), np.array([y]))[0]) + WATER_M

    def _filled_floor(self) -> np.ndarray:
        if not hasattr(self, "_floor_filled"):
            nan = np.isnan(self.floor_px)
            _, (ii, jj) = ndimage.distance_transform_edt(nan, return_indices=True)
            self._floor_filled = self.floor_px[ii, jj]
        return self._floor_filled

    def _tunnel_query(self, X, Y):
        d, k = self.tunnel_tree.query(np.stack([np.ravel(X), np.ravel(Y)], axis=1))
        c = self.tunnel_cat
        return (d.reshape(X.shape),) + tuple(c[key][k].reshape(X.shape) for key in ("t", "floor", "half", "height", "rock"))

    def _tunnel_span(self, X, Y) -> np.ndarray:
        _, k = self.tunnel_tree.query(np.stack([np.ravel(X), np.ravel(Y)], axis=1))
        return self.tunnel_cat["span"][k].reshape(np.shape(X)) > 0.5

    def tunnel_zone(self, X, Y) -> np.ndarray:
        u, _, _, half, _, _ = self._tunnel_query(X, Y)
        return 1.0 - smooth(half + 1.5, half + 5.0, u)

    # -- relieve ---------------------------------------------------------------------------------
    def _fields(self, X, Y) -> Fields:
        spec, shape = self.spec, X.shape
        zone_like = list(spec.zone_classes) + [SEA]
        lab = self.labels
        floor = self._sample_px(self._filled_floor(), X, Y) + WATER_M
        inside = np.isin(lab, zone_like)
        e_any = self._signed(inside)
        n_floor = self.n_floor(X, Y)
        near_tunnel = self.tunnel_zone(X, Y)
        floor_in = floor + spec.floor_noise_m * n_floor * (lab != SEA) * (1.0 - near_tunnel)
        # Remate (cap) de las paredes: el de todas las clases cercanas y el relieve natural de fuera.
        rim_lo, rim_hi = RIM_M
        rim_h = rim_lo + (rim_hi - rim_lo) * self.n_rim.unit(X, Y)
        band = 2.0 + 8.0 * self.n_band.unit(X, Y)
        tan_fall = np.tan(np.radians(18.0 + 22.0 * (0.5 + 0.5 * self.n_top(X, Y))))
        envelope = np.full(shape, -1e3)
        per_class = {}
        for cls in zone_like:
            e_c = self._signed(lab == cls)
            zf_c = self._sample_px(self._class_floor(cls), X, Y) + WATER_M
            per_class[cls] = (e_c, zf_c)
            c = np.maximum(e_c, 0.0)
            drop = 0.3 * np.clip(c, 0.0, band + 1.5) + np.maximum(c - band - 1.5, 0.0) * tan_fall
            rim = rim_h if cls != SEA else 0.5 * rim_h
            envelope = np.maximum(envelope, zf_c + rim - drop)
        base = ndimage.gaussian_filter(floor, 6.0, mode="nearest")
        v = field.vista(self, X, Y)
        reach = FADE_REACH_M[0] + (FADE_REACH_M[1] - FADE_REACH_M[0]) * self.n_run.unit(X, Y)
        fade = ndimage.gaussian_filter(1.0 - smooth(reach, reach + 25.0, e_any), 5.0, mode="nearest")
        natural = v + (np.maximum(base - v, 0.0) + self._hills(X, Y)) * fade
        outer = ndimage.gaussian_filter(np.maximum(natural, envelope), 1.2, mode="nearest")
        # Pared desde cada clase (seccion de C01) y minimo suave entre clases.
        n_rim, n_top, n_wall = self.n_rim.unit(X, Y), self.n_top(X, Y), self.n_wall2d(X, Y)
        wall = None
        crest = np.zeros(shape)
        for cls, (e_c, zf_c) in per_class.items():
            bw = np.zeros(shape + (4,))
            bw[..., 3 if cls in (SEA, spec.beach_class) else 0] = 1.0
            h_c, _, crest_c = field.section(np.maximum(e_c, 0.0), zf_c, zf_c, np.full(shape, 10.0), bw, n_rim, n_top,
                                            np.zeros(shape), self.style, None, n_wall, H_in=outer)
            crest = np.where(e_c <= np.min([pc[0] for pc in per_class.values()], axis=0), crest_c, crest)
            wall = h_c if wall is None else field.soft_min(wall, h_c, CREST_K_M)
        height = np.where(e_any <= 0.0, floor_in, wall)
        height = self._stamp_tunnels(X, Y, height, e_any)
        height = field.clip_spikes(height)
        self.e_any = e_any
        tunnel = near_tunnel * self._tunnel_span(X, Y)
        region = np.where(e_any <= 0.0, 0, np.where(e_any < crest + 6.0, 1, 2))
        region = np.where(lab == SEA, 3, region)
        self.region = np.where(tunnel > 0.5, 0, region)
        wall_band = smooth(0.5, 2.0, e_any) * (1.0 - smooth(14.0, 22.0, e_any)) * (1.0 - tunnel)
        weights = {k: np.zeros(shape) for k in ZONE_KEYS}
        weights["cliffs"] = np.ones(shape)
        weights["canyon"] = np.zeros(shape)
        floor_out = np.where(e_any <= 0.0, floor_in, floor)
        path = 1.0 - smooth(-1.0, 0.5, e_any)
        zeros = np.zeros(shape)
        return Fields(weights, np.maximum(e_any, 0.0), zeros, height, floor_out, np.clip(wall_band, 0, 1), zeros,
                      tunnel, np.clip(path, 0, 1))

    def _signed(self, mask: np.ndarray) -> np.ndarray:
        """Distancia con signo (m) al borde de mask: negativa dentro."""
        out = ndimage.distance_transform_edt(~mask) * STEP_XY_M
        inn = ndimage.distance_transform_edt(mask) * STEP_XY_M
        return np.where(mask, -inn + 0.5, out - 0.5)

    def _hills(self, X, Y):
        lo, hi = HILL_AMP_M
        amp = lo + (hi - lo) * smooth(0.3, 0.75, self.n_crest_big.unit(X, Y))
        shape = 0.25 + 0.75 * smooth(0.2, 0.9, self.n_crest_mid.unit(X, Y))
        return amp * shape + 0.9 * self.n_crest_fine.unit(X, Y)

    def _stamp_tunnels(self, X, Y, height, e_any):
        """Roca sobre el tramo cubierto: nunca por debajo de clave + rock_m (con lomas), cupula que baja
        hacia fuera; no toca el suelo de las zonas (las bocas quedan en la cara de la pared)."""
        u, t, floor, half, gap, rock = self._tunnel_query(X, Y)
        top = floor + 1.1 * gap + rock + 2.0 * self.n_crest_mid.unit(X, Y)
        over = (1.0 - smooth(half + 4.0, half + 16.0, u)) * smooth(-0.02, 0.04, t) * smooth(-1.04, -0.98, -t)
        over = over * smooth(0.0, 2.0, e_any)
        return np.maximum(height, top * over + height * (1.0 - over))

    def _carve(self, X, Y, Z3):
        """> 0 dentro del hueco del tunel (seccion de PathModel._carve). La clave queda a 1,04 x gap
        sobre el suelo; el ruido de altura se acota para que la cueva no pase de 10 m."""
        u, t, floor, half, gap, _ = self._tunnel_query(X, Y)
        half = half * (1.0 + 0.1 * self.n_tunnel(X, Y))
        height = gap * (1.0 + 0.06 * self.n_tunnel(Y, X))
        v = Z3 - floor[..., None]
        center, radius_v = 0.42 * height[..., None], 0.62 * height[..., None]
        ellipse = 1.0 - np.sqrt((u[..., None] / half[..., None]) ** 2 + ((v - center) / radius_v) ** 2)
        inside = np.minimum(ellipse * half[..., None], v + 0.15)
        return np.where(self._tunnel_span(X, Y)[..., None], inside, -1.0), v

    # -- color -----------------------------------------------------------------------------------
    def _color_grid(self) -> None:
        beachy = np.isin(self.labels, (SEA, self.spec.beach_class)).astype(np.float64)
        self._beach_w = ndimage.gaussian_filter(beachy, 18.0, mode="nearest")

    def _grid_sample(self, grid, x, y, order: int = 1):
        coords = [(np.asarray(x) - self.ax_x[0]) / STEP_XY_M, (np.asarray(y) - self.ax_y[0]) / STEP_XY_M]
        return ndimage.map_coordinates(grid, coords, order=order, mode="nearest")

    def color_weights(self, x, y) -> dict[str, np.ndarray]:
        b = np.clip(self._grid_sample(self._beach_w, x, y), 0.0, 1.0)
        out = {k: np.zeros(np.shape(x)) for k in ZONE_KEYS}
        out["cliffs"], out["beach"] = 1.0 - b, b
        out["canyon"] = np.zeros(np.shape(x))
        return out

    def weights(self, x, y):
        return self.color_weights(x, y)

    def trail_mask(self, x, y) -> np.ndarray:
        e = self._grid_sample(self.e_any, x, y)
        return 1.0 - smooth(-0.6, 0.8, e)

    def trail_mask_3d(self, x, y, z) -> np.ndarray:
        """Suelo de las zonas y de los tuneles (arena pisada); no las paredes."""
        floor = self._grid_sample(self.grid.floor, x, y)
        on_floor = 1.0 - smooth(0.35, 0.9, np.abs(np.asarray(z) - floor))
        return np.maximum(self.trail_mask(x, y) * on_floor, self._tunnel_floor_mask(x, y, z))

    def _tunnel_floor_mask(self, x, y, z):
        u, t, floor, half, _, _ = self._tunnel_query(np.asarray(x), np.asarray(y))
        on = (1.0 - smooth(0.3, 0.8, np.abs(np.asarray(z) - floor))) * (1.0 - smooth(0.6, 0.9, u / half))
        return on * self._tunnel_span(np.asarray(x), np.asarray(y))

    def plaza_mask(self, x, y) -> np.ndarray:
        return np.zeros(np.shape(x))

    # -- interfaz de trozos ----------------------------------------------------------------------
    def chunk_fields(self, col: int, row: int, pad: int = 0):
        i0 = row * int(CELL_M / STEP_XY_M) + PAD - pad
        j0 = col * int(CELL_M / STEP_XY_M) + PAD - pad
        n = CELL_SAMPLES + 2 * pad
        X, Y = np.meshgrid(self.ax_x[i0:i0 + n], self.ax_y[j0:j0 + n], indexing="ij")
        return self.grid.window(i0, i0 + n, j0, j0 + n), X, Y

    def density(self, X, Y, Z, f: Fields) -> np.ndarray:
        X3, Y3, Z3 = X[..., None], Y[..., None], Z[None, None, :]
        D = f.height[..., None] - Z3
        band = f.wall_band[..., None]
        if np.any(band > 0.0):
            above = smooth(field.FOOT_M, field.FOOT_M + 1.5, Z3 - f.floor[..., None])
            below_top = smooth(1.0, 3.0, f.height[..., None] - Z3)
            D = D + self.wall_noise * band * above * below_top * self.n_wall3d(X3, Y3, Z3)
        if np.any(f.tunnel > 0.0):
            carve, v = self._carve(X, Y, Z3)
            carve = carve + 0.4 * self.n_wall3d(X3 * 1.7, Y3 * 1.7, Z3 * 1.7) * (carve > -0.5) * smooth(0.3, 1.2, v)
            D = np.minimum(D, -carve)
        return D

    # -- salida y meta ---------------------------------------------------------------------------
    def _end_point(self) -> np.ndarray:
        """Meta: punto de la orilla (suelo a la cota del agua) de la playa mas cercano al contacto con el mar."""
        beach = self.labels == self.spec.beach_class
        near_sea = ndimage.binary_dilation(self.labels == SEA, iterations=3) & beach
        h = self.grid.height
        shore = near_sea & (np.abs(h - (WATER_M + 0.6)) < 0.3)          # orilla seca, junto al agua
        cand = np.argwhere(shore if shore.any() else near_sea)
        center = cand.mean(axis=0)
        i, j = cand[np.argmin(((cand - center) ** 2).sum(axis=1))]
        return np.array([self.ax_x[i], self.ax_y[j]])

    def remove_deadly(self, standable, z_levels):
        return standable

    def jump_links(self):
        return []
