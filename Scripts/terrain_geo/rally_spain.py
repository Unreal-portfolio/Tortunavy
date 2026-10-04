"""E01B_espana_rally: trayecto de Rally punto a punto por España (Docs/Rally_E01B_y_Biplaza.md §2).

Del Pirineo aragonés a la Costa del Sol por Zaragoza, Guadalajara, Toledo y Despeñaperros, sobre el relieve real
de Scripts/terrain_geo/data/ES_dem.png (el mismo MDE que E01) con otra escala y otra exageración:

  - Escala: 1 m de juego = GROUND_M_PER_GAME_M m de suelo (Mercator con el centro en la mitad del trazado).
  - Exageración vertical efectiva EXAGGERATION (3-6x, frente a los 24,7x de E01): k = EXAGGERATION / escala.
  - Tierra = cota real > 0 (Francia es tierra: el Pirineo sigue al norte); el mar solo aparece en Málaga.
  - Calzada tallada: ROAD_W_M de firme más arcenes, perfil suavizado con pendiente <= MAX_GRADE_DEG, desmonte y
    terraplén a TALUD_DEG desde el borde del arcén. Sin túneles ni puentes en esta versión.
  - Corredor: los trozos de 100 m a menos de BAND_M del eje, con colisión. Fondo (BackgroundModel): el resto de
    trozos de tierra de la rejilla ampliada (Layout: la península entera, PENINSULA_LONLAT, con Portugal y
    Baleares; África, al sur de AFRICA_LINE, es mar), mismo relieve y misma exageración, decimados más y sin
    colisión: solo se ven; salirse del corredor es caer al agua de la caja de muerte.

Coordenadas como terrain_vol/layout.py: X = Norte, Y = Este, trozo (col, fila) centrado en (fila * 100, col * 100).
Todo se calcula en las coordenadas LOCALES del marco del corredor (Frame: ROWS x COLS trozos desde el origen, las
de la variante hasta #532): el fondo al oeste y al sur queda en filas y columnas negativas. Al escribir la variante,
trozos y coordenadas se desplazan con Layout.offset_m (filas y columnas enteras), así que el corredor es el mismo.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from functools import cached_property

import numpy as np
from scipy import interpolate, ndimage
from scipy.spatial import cKDTree
from terrain_vol.density import smooth
from terrain_vol.layout import CELL_M, MAP_MIN_M, WATER_M, ZRange

from .fetch_spain import load_dem
from .heightfield import HeightfieldModel
from .layout import RASTER_PX_M, SPAIN_REGION
from .region import GameProjection
from .sources import lonlat_to_mercator

NAME = "E01B_espana_rally"
SEED = 20261001
GROUND_M_PER_GAME_M = 400.0
EXAGGERATION = 4.5
ROAD_W_M = 14.0
SHOULDER_M = 3.0
TALUD_DEG = 33.0
MAX_GRADE_DEG = 7.0
PROFILE_SIGMA_M = 35.0
MIN_ROAD_ABOVE_WATER_M = 0.8
BEACH_GRADE_DEG = 4.0            # bajada final hasta la playa de la meta
BAND_M = 70.0
GRID_M = 60.0                    # recta de parrilla (2 x 4 buggies) antes de la línea de salida
RUNOFF_M = 30.0                  # escapatoria tras la meta
MARGIN_M = 110.0
LAND_BASE_M = 0.45
LAND_CELL_M = WATER_M + 0.2      # un trozo de fondo se genera si alguna muestra queda por encima de esta cota
LAND_PROBE_M = 2.0               # paso de las muestras con las que se decide si un trozo de fondo tiene tierra
# Península entera (#532): (lon O, lat S, lon E, lat N) que la rejilla ampliada debe contener (Finisterre, Cabo de
# São Vicente, Tarifa, Menorca, Estaca de Bares).
PENINSULA_LONLAT = (-9.65, 35.92, 4.45, 43.85)
# Al sur de esta línea (lon, lat) es África, que se trata como mar: pasa entre Tarifa y Ceuta y entre el Cabo de
# Gata y Orán.
AFRICA_LINE = ((-10.5, 35.90), (-5.55, 35.975), (-4.5, 36.20), (-2.3, 36.45), (4.6, 37.30))
AFRICA_SEA_M = -200.0            # cota que se le da a África en el MDE del fondo
BACKGROUND_PX_M = 1.0            # paso del raster del fondo ampliado (el del corredor es RASTER_PX_M)
# Francia llega a los bordes norte y este de la rejilla: en esta franja la tierra baja al mar (con una costa irregular,
# EDGE_WOBBLE_M) para que no acabe en un corte recto. Las costas de la península quedan a más de 90 m de esos bordes.
EDGE_FADE_M = 60.0
EDGE_WOBBLE_M = 15.0
DESCRIPTION = ("España para el Rally (punto a punto): del Pirineo aragonés a la playa de Málaga por el Ebro, la "
               "Meseta, el Tajo y Despeñaperros, con el relieve real (1 m de juego = 0,4 km, exageración 4,5x); "
               "calzada tallada de 14 m y, fuera del corredor, la península entera y Baleares de fondo (sin colisión).")

# (nombre, lat, lon) de los hitos, en el orden de la marcha.
HITOS = (
    ("canfranc", 42.70, -0.53),
    ("zaragoza", 41.66, -0.88),
    ("guadalajara", 40.63, -3.17),
    ("toledo", 39.86, -4.02),
    ("despenaperros", 38.35, -3.50),
    ("malaga", 36.725, -4.42),
)

# Curvas de autor entre hitos: (fraccion del tramo, desplazamiento lateral en m, positivo a la derecha de la
# marcha). Herraduras suaves en el Pirineo, eses en Sierra Morena y en la bajada a la costa.
BENDS = (
    ((0.22, 20.0), (0.5, -20.0), (0.78, 14.0)),
    ((0.25, 30.0), (0.5, 40.0), (0.75, 18.0)),
    ((0.3, -22.0), (0.62, 16.0)),
    ((0.2, 22.0), (0.4, -18.0), (0.58, 20.0), (0.76, -16.0), (0.9, 6.0)),
    ((0.16, -22.0), (0.34, 22.0), (0.52, -22.0), (0.70, 20.0), (0.86, -10.0)),
)


# ── Marco ────────────────────────────────────────────────────────────────────────
@dataclass(frozen=True)
class Frame:
    projection: GameProjection
    rows: int
    cols: int

    @property
    def grid(self) -> int:
        return max(self.rows, self.cols)

    def axis(self, count_cells: int) -> np.ndarray:
        px = int(round(count_cells * CELL_M / RASTER_PX_M))
        return MAP_MIN_M + (np.arange(px) + 0.5) * RASTER_PX_M


def make_frame() -> Frame:
    """Proyección de juego que deja los hitos, con MARGIN_M, dentro del rectángulo de trozos."""
    lats = np.array([h[1] for h in HITOS])
    lons = np.array([h[2] for h in HITOS])
    mx, my = lonlat_to_mercator(lons, lats)
    cx, cy = 0.5 * (mx.min() + mx.max()), 0.5 * (my.min() + my.max())
    lon_c = float(np.degrees(cx / 6378137.0))
    lat_c = float(np.degrees(2.0 * np.arctan(np.exp(cy / 6378137.0)) - np.pi / 2.0))
    scale = GROUND_M_PER_GAME_M / math.cos(math.radians(lat_c))
    span_x = (my.max() - my.min()) / scale + 2.0 * MARGIN_M
    span_y = (mx.max() - mx.min()) / scale + 2.0 * MARGIN_M
    rows, cols = int(math.ceil(span_x / CELL_M)), int(math.ceil(span_y / CELL_M))
    origin = (MAP_MIN_M + rows * CELL_M / 2.0, MAP_MIN_M + cols * CELL_M / 2.0)
    proj = GameProjection("merc", (lon_c, lat_c), scale, max(rows, cols), center_xy=(float(cx), float(cy)), origin=origin)
    return Frame(proj, rows, cols)


@dataclass(frozen=True)
class Layout:
    """Rejilla ampliada (#532): el marco del corredor más las filas (sur) y columnas (oeste) que hacen falta para
    la península entera. El trozo local (col, fila) es (col + col0, fila + row0) en la variante, y todo lo que
    está en coordenadas de mundo se desplaza offset_m."""
    frame: Frame
    row0: int
    col0: int
    rows: int
    cols: int

    @property
    def grid(self) -> int:
        return max(self.rows, self.cols)

    @property
    def offset_m(self) -> np.ndarray:
        return np.array([self.row0 * CELL_M, self.col0 * CELL_M])

    def local_cells(self) -> list[tuple[int, int]]:
        """Trozos (col, fila) de la rejilla ampliada, en coordenadas locales del marco."""
        return [(col, row) for row in range(-self.row0, self.rows - self.row0)
                for col in range(-self.col0, self.cols - self.col0)]


def make_layout(frame: Frame) -> Layout:
    lon_w, lat_s, lon_e, lat_n = PENINSULA_LONLAT
    lon_c, lat_c = frame.projection.center
    x_s, x_n = frame.projection.to_game(lon_c, lat_s)[0], frame.projection.to_game(lon_c, lat_n)[0]
    y_w, y_e = frame.projection.to_game(lon_w, lat_c)[1], frame.projection.to_game(lon_e, lat_c)[1]
    row0 = max(0, int(math.ceil((MAP_MIN_M - float(x_s)) / CELL_M)))
    col0 = max(0, int(math.ceil((MAP_MIN_M - float(y_w)) / CELL_M)))
    rows = row0 + max(frame.rows, int(math.ceil((float(x_n) - MAP_MIN_M) / CELL_M)))
    cols = col0 + max(frame.cols, int(math.ceil((float(y_e) - MAP_MIN_M) / CELL_M)))
    return Layout(frame, row0, col0, rows, cols)


def africa_mask(frame: Frame, X: np.ndarray, Y: np.ndarray) -> np.ndarray:
    """True en los puntos de juego al sur de AFRICA_LINE (África)."""
    lon, lat = frame.projection.to_lonlat(X, Y)
    line = np.array(AFRICA_LINE)
    return np.asarray(lat) < np.interp(lon, line[:, 0], line[:, 1])


def exaggeration_k(frame: Frame) -> float:
    return EXAGGERATION / frame.projection.ground_m_per_game_m()


# ── MDE ──────────────────────────────────────────────────────────────────────────
def sample_dem(dem: np.ndarray, frame: Frame, X: np.ndarray, Y: np.ndarray) -> np.ndarray:
    """Cota real (m) en los puntos de juego (X, Y) del marco, leída de ES_dem (rejilla de E01) con bicúbica."""
    lon, lat = frame.projection.to_lonlat(X, Y)
    Xe, Ye = SPAIN_REGION.game_projection().to_game(lon, lat)
    coords = [(np.asarray(Xe) - MAP_MIN_M) / RASTER_PX_M - 0.5, (np.asarray(Ye) - MAP_MIN_M) / RASTER_PX_M - 0.5]
    return ndimage.map_coordinates(dem, coords, order=3, mode="nearest")


def value_noise(rng: np.random.Generator, wavelength: float, extent_m: float, octaves: int = 4):
    """Ruido de valor fBm en [-1, 1] sobre un cuadrado de extent_m desde MAP_MIN_M (el de terrain_vol se limita
    al volumen de 600 m). Fuera del cuadrado la red se repite (índices módulo su tamaño): dentro da lo mismo que
    antes de #532 y fuera sigue siendo continuo, sin las franjas de una red recortada."""
    layers = []
    for o in range(octaves):
        w = wavelength / 2 ** o
        cells = int(math.ceil((extent_m + 400.0) / w)) + 3
        layers.append((0.5 ** o, w, rng.uniform(-1.0, 1.0, (cells, cells))))
    norm = sum(a for a, _, _ in layers)

    def noise(X: np.ndarray, Y: np.ndarray) -> np.ndarray:
        out = 0.0
        for amp, w, lattice in layers:
            u, v = (np.asarray(X) - MAP_MIN_M + 200.0) / w, (np.asarray(Y) - MAP_MIN_M + 200.0) / w
            i0, j0 = np.floor(u).astype(np.int64), np.floor(v).astype(np.int64)
            fu, fv = smooth(0.0, 1.0, u - i0), smooth(0.0, 1.0, v - j0)
            n0, n1 = lattice.shape
            i1, j1 = (i0 + 1) % n0, (j0 + 1) % n1
            i0, j0 = i0 % n0, j0 % n1
            a, b = lattice[i0, j0], lattice[i1, j0]
            c, d = lattice[i0, j1], lattice[i1, j1]
            out = out + amp * ((a * (1 - fu) + b * fu) * (1 - fv) + (c * (1 - fu) + d * fu) * fv)
        return out / norm
    return noise


# ── Trazado ──────────────────────────────────────────────────────────────────────
def control_points(frame: Frame) -> np.ndarray:
    """Hitos más las curvas de autor (BENDS) en coordenadas de juego (X, Y)."""
    hitos = np.array([frame.projection.to_game(lon, lat) for _, lat, lon in HITOS], dtype=np.float64)
    out = [hitos[0]]
    for seg, bends in enumerate(BENDS):
        a, b = hitos[seg], hitos[seg + 1]
        d = b - a
        u = d / np.hypot(*d)
        right = np.array([-u[1], u[0]])        # X Norte, Y Este: a la derecha de la marcha
        for t, off in bends:
            out.append(a + d * t + right * off)
        out.append(b)
    return np.array(out)


def resample_open(points: np.ndarray, step_m: float = 1.0) -> np.ndarray:
    tck, _ = interpolate.splprep([points[:, 0], points[:, 1]], s=0.0, k=3)
    dense = np.column_stack(interpolate.splev(np.linspace(0.0, 1.0, 60 * len(points)), tck))
    arc = np.concatenate([[0.0], np.cumsum(np.hypot(*np.diff(dense, axis=0).T))])
    s = np.linspace(0.0, arc[-1], int(round(arc[-1] / step_m)) + 1)
    return np.column_stack([np.interp(s, arc, dense[:, 0]), np.interp(s, arc, dense[:, 1])])


def route(frame: Frame) -> np.ndarray:
    """Eje de la calzada cada 1 m: la recta de parrilla (GRID_M, en la dirección de salida) y el trazado."""
    pts = resample_open(control_points(frame))
    u = pts[1] - pts[0]
    u = u / np.hypot(*u)
    back = pts[0][None, :] - u[None, :] * np.arange(GRID_M, 0.0, -1.0)[:, None]
    return np.vstack([back, pts])


def arc_length(pts: np.ndarray) -> np.ndarray:
    return np.concatenate([[0.0], np.cumsum(np.hypot(*np.diff(pts, axis=0).T))])


def lipschitz(values: np.ndarray, grade: float, passes: int = 4) -> np.ndarray:
    """Perfil (muestras cada 1 m) con pendiente <= grade: recorte hacia delante y hacia atrás, repetido."""
    out = values.astype(np.float64).copy()
    for _ in range(passes):
        for k in range(1, len(out)):
            out[k] = min(max(out[k], out[k - 1] - grade), out[k - 1] + grade)
        for k in range(len(out) - 2, -1, -1):
            out[k] = min(max(out[k], out[k + 1] - grade), out[k + 1] + grade)
    return out


def road_profile(ground: np.ndarray) -> np.ndarray:
    """Cota de la calzada: el terreno bajo el eje suavizado, con la pendiente acotada y siempre sobre el agua."""
    z = ndimage.gaussian_filter1d(ground, PROFILE_SIGMA_M, mode="nearest")
    to_end = np.arange(len(z))[::-1].astype(np.float64)              # m hasta el final (muestras de 1 m)
    beach = WATER_M + MIN_ROAD_ABOVE_WATER_M + 0.2 + math.tan(math.radians(BEACH_GRADE_DEG)) * to_end
    z = np.maximum(np.minimum(z, beach), WATER_M + MIN_ROAD_ABOVE_WATER_M)     # la meta baja a la playa
    z = lipschitz(z, math.tan(math.radians(MAX_GRADE_DEG)))
    z = ndimage.gaussian_filter1d(z, 6.0, mode="nearest")          # sin quiebros en los cambios de rasante
    return np.maximum(z, WATER_M + MIN_ROAD_ABOVE_WATER_M)


# ── Modelo ───────────────────────────────────────────────────────────────────────
class RallySpainModel(HeightfieldModel):
    """Campo de alturas del corredor (raster de ROWS x COLS trozos) con la calzada tallada."""
    trail_color = (0.42, 0.30, 0.17)
    trail_strength = 0.55

    def __init__(self, frame: Frame, seed: int = SEED):
        self.frame, self.seed = frame, seed
        self.k = exaggeration_k(frame)
        self.z_range = ZRange()
        X = frame.axis(frame.rows)[:, None]
        Y = frame.axis(frame.cols)[None, :]
        Xg, Yg = np.broadcast_arrays(X, Y)
        dem = sample_dem(load_dem(), frame, Xg, Yg)
        natural = self._natural(dem, Xg, Yg, np.random.default_rng(seed))
        self.road = route(frame)
        self.road_ground = self._sample_raster(natural, self.road)
        self.road_z = road_profile(self.road_ground)
        height, self.trail = self._carve(natural, Xg, Yg)
        super().__init__(height)

    def _natural(self, dem: np.ndarray, X, Y, rng, px_m: float = RASTER_PX_M) -> np.ndarray:
        land = np.maximum(dem, 0.0)
        relief = land * self.k
        extent = max(self.frame.rows, self.frame.cols) * CELL_M
        detail = value_noise(rng, 48.0, extent, octaves=3)(X, Y)
        amp = 0.08 + 0.7 * smooth(400.0 * self.k, 2200.0 * self.k, relief)
        land_h = WATER_M + LAND_BASE_M + relief + amp * detail
        land_w = ndimage.gaussian_filter(smooth(-20.0, 20.0, dem), 2.0 * RASTER_PX_M / px_m, mode="nearest")
        sea_h = WATER_M - 1.5 - 4.0 * (1.0 - np.exp(-np.maximum(-dem, 0.0) / 250.0))
        return sea_h + (land_h - sea_h) * land_w

    @staticmethod
    def _sample_raster(raster: np.ndarray, pts: np.ndarray) -> np.ndarray:
        coords = [(pts[:, 0] - MAP_MIN_M) / RASTER_PX_M - 0.5, (pts[:, 1] - MAP_MIN_M) / RASTER_PX_M - 0.5]
        return ndimage.map_coordinates(raster, coords, order=1, mode="nearest")

    def _carve(self, natural: np.ndarray, X, Y) -> tuple[np.ndarray, np.ndarray]:
        """Desmonte y terraplén hasta el prisma de la calzada; a más de ~150 m del eje el terreno queda intacto."""
        pts = np.column_stack([X.ravel(), Y.ravel()])
        d, k = cKDTree(self.road).query(pts, distance_upper_bound=BAND_M + 120.0)
        near = np.isfinite(d)
        d = np.where(near, d, 1e4).reshape(X.shape)
        k = np.where(near, k, 0).reshape(X.shape)
        road_z = self.road_z[k]
        flat = ROAD_W_M / 2.0 + SHOULDER_M
        reach = np.clip(d - flat, 0.0, None) * math.tan(math.radians(TALUD_DEG))
        height = np.where(near.reshape(X.shape), np.clip(natural, road_z - reach, road_z + reach), natural)
        trail = 1.0 - smooth(ROAD_W_M / 2.0 - 1.0, ROAD_W_M / 2.0 + 0.5, d)
        return height, trail

    def trail_mask(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        return np.clip(self._sample(self.trail, x, y), 0.0, 1.0)

    @cached_property
    def arc(self) -> np.ndarray:
        return arc_length(self.road)

    def cell_gaps(self) -> dict[tuple[int, int], float]:
        """Distancia (m) del eje a cada trozo (col, fila) a menos de BAND_M; 0 si el eje lo cruza."""
        out = {}
        half = CELL_M / 2.0
        for row in range(self.frame.rows):
            for col in range(self.frame.cols):
                gap = float(np.hypot(np.clip(np.abs(self.road[:, 0] - row * CELL_M) - half, 0.0, None),
                                     np.clip(np.abs(self.road[:, 1] - col * CELL_M) - half, 0.0, None)).min())
                if gap <= BAND_M:
                    out[(col, row)] = gap
        return out


# ── Fondo ────────────────────────────────────────────────────────────────────────
class BackgroundModel(HeightfieldModel):
    """Relieve de los trozos de fondo (coordenadas locales del marco): dentro del rectángulo del marco, bordes
    incluidos, el del corredor (RallySpainModel), para que las costuras con sus trozos casen; fuera, un raster de
    BACKGROUND_PX_M que cubre la rejilla ampliada (Layout) con el mismo MDE, ruido, exageración y talla, y con
    África (africa_mask) hundida bajo el agua."""

    def __init__(self, model: RallySpainModel, layout: Layout):
        self.model, self.layout, self.k, self.z_range = model, layout, model.k, model.z_range
        self.trail_color, self.trail_strength = model.trail_color, model.trail_strength
        frame = model.frame
        self.x_edge = MAP_MIN_M + frame.rows * CELL_M
        self.y_edge = MAP_MIN_M + frame.cols * CELL_M
        self.x0 = MAP_MIN_M - layout.row0 * CELL_M
        self.y0 = MAP_MIN_M - layout.col0 * CELL_M
        self.outer = self._outer()
        super().__init__(model.height)

    def _outer(self) -> np.ndarray:
        layout, frame = self.layout, self.model.frame
        px = BACKGROUND_PX_M
        X = (self.x0 + (np.arange(int(round(layout.rows * CELL_M / px))) + 0.5) * px)[:, None]
        Y = (self.y0 + (np.arange(int(round(layout.cols * CELL_M / px))) + 0.5) * px)[None, :]
        Xg, Yg = np.broadcast_arrays(X, Y)
        dem = sample_dem(load_dem(), frame, Xg, Yg)
        dem = np.where(africa_mask(frame, Xg, Yg), np.minimum(dem, AFRICA_SEA_M), dem)
        dem = self._fade_edges(dem, Xg, Yg)
        # Mismo generador aleatorio que RallySpainModel: el ruido de detalle es una función del punto (X, Y).
        natural = self.model._natural(dem, Xg, Yg, np.random.default_rng(self.model.seed), px_m=px)
        height, _ = self.model._carve(natural, Xg, Yg)
        return height

    def _fade_edges(self, dem: np.ndarray, X: np.ndarray, Y: np.ndarray) -> np.ndarray:
        """Hunde la tierra en los últimos EDGE_FADE_M antes de los bordes norte y este de la rejilla."""
        layout = self.layout
        edge = np.minimum(self.x0 + layout.rows * CELL_M - X, self.y0 + layout.cols * CELL_M - Y)
        extent = layout.grid * CELL_M
        wobble = value_noise(np.random.default_rng(self.model.seed + 1), 120.0, extent, octaves=3)(X, Y)
        keep = smooth(0.0, EDGE_FADE_M, edge - EDGE_WOBBLE_M + EDGE_WOBBLE_M * wobble)
        return dem * keep + AFRICA_SEA_M * (1.0 - keep)

    def _inner(self, X, Y) -> np.ndarray:
        X, Y = np.asarray(X), np.asarray(Y)
        return (X >= MAP_MIN_M) & (X <= self.x_edge) & (Y >= MAP_MIN_M) & (Y <= self.y_edge)

    def ground_height(self, X: np.ndarray, Y: np.ndarray) -> np.ndarray:
        coords = [(np.asarray(X) - self.x0) / BACKGROUND_PX_M - 0.5, (np.asarray(Y) - self.y0) / BACKGROUND_PX_M - 0.5]
        outer = ndimage.map_coordinates(self.outer, coords, order=1, mode="nearest")
        return np.where(self._inner(X, Y), self.model.ground_height(X, Y), outer)

    def trail_mask(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        return np.where(self._inner(x, y), self.model.trail_mask(x, y), 0.0)

    def has_land(self, col: int, row: int) -> bool:
        """Alguna muestra del trozo (cada LAND_PROBE_M) queda sobre el agua."""
        n = int(round(CELL_M / LAND_PROBE_M)) + 1
        x = row * CELL_M - CELL_M / 2.0 + LAND_PROBE_M * np.arange(n)
        y = col * CELL_M - CELL_M / 2.0 + LAND_PROBE_M * np.arange(n)
        X, Y = np.meshgrid(x, y, indexing="ij")
        return bool((self.ground_height(X, Y) > LAND_CELL_M).any())

    def cells(self, corridor) -> list[tuple[int, int]]:
        """Trozos (col, fila) locales de la rejilla ampliada con tierra que no son del corredor."""
        return [cell for cell in self.layout.local_cells() if cell not in corridor and self.has_land(*cell)]
