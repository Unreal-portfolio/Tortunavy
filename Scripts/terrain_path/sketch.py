"""Mapa "camino primero" a partir de un boceto del equipo de diseno (D01, #875).

El boceto es una imagen con zonas de color plano: el tono de arena da la cota (mas oscuro, mas alto),
el marron es pared o limite, el azul es el mar y el gris marca las bocas de los tuneles. Este modulo
convierte el boceto en un mapa de clases por pixel y calcula el suelo de las zonas jugables:

  - cada zona es una meseta de suelo casi llano a la cota de su tono;
  - donde dos zonas se tocan, el desnivel se salva con una cuesta andable: rampa lineal suave en los
    pasillos (contacto corto) y ladera mas pronunciada, pero siempre por debajo de la pendiente
    maxima andable, en los bordes largos entre mesetas;
  - la playa baja al mar en cuesta.

Convenciones: pixel (x a la derecha, y hacia abajo, Norte arriba). Mundo como terrain_vol/layout.py:
X = Norte, Y = Este, en metros. La conversion la fija SketchSpec (escala y pixel de referencia).
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np
from PIL import Image
from scipy import ndimage
from skimage.graph import MCP_Geometric

SEA, WALL = 0, 1                     # clases que no son zona jugable
UNKNOWN = -1


@dataclass(frozen=True)
class Tunnel:
    """Tramo subterraneo del camino: eje en pixeles del boceto (de la boca de arriba a la de abajo),
    tramo cubierto (fraccion del eje) y seccion. El suelo va en rampa lineal entre las cotas de las
    zonas de sus bocas."""
    name: str
    axis_px: tuple[tuple[float, float], ...]
    covered: tuple[float, float]          # fraccion del eje (0..1) con techo
    half_width_m: float
    height_m: float                       # de suelo a clave (antes del ruido)
    rock_m: float                         # roca minima sobre la clave


@dataclass(frozen=True)
class SketchSpec:
    name: str
    description: str
    image: str                                        # ruta del boceto, relativa a Scripts/
    m_per_px: float
    ref_px: tuple[float, float]                       # pixel (x, y) que va a ref_world
    ref_world: tuple[float, float]                    # (X Norte, Y Este) en metros
    cols: int                                         # trozos de 100 m hacia el Este
    rows: int                                         # trozos de 100 m hacia el Norte
    palette: dict[int, tuple[int, int, int]]          # clase -> color del boceto
    zone_height_m: dict[int, float]                   # clase de zona -> cota del suelo sobre el agua
    sea_floor_m: float                                # cota del lecho del mar sobre el agua (negativa)
    legend_box_px: tuple[int, int, int, int]          # (x0, y0, x1, y1): leyenda, se pinta de pared
    bottom_cap_px: tuple[float, float, float, float]  # elipse (cx, cy, rx, ry) que cierra la zona de abajo
    sea_fan_px: tuple[tuple[float, float], ...]       # poligono: pared que pasa a playa (abanico al mar)
    beach_class: int
    tunnels: tuple[Tunnel, ...]
    start_px: tuple[float, float]
    corridor_grade: float = 0.12                      # pasillos: rampa lineal suave
    slope_deg: float = 26.0                           # laderas entre mesetas (andable: < 44,76 grados)
    sea_grade: float = 0.08                           # bajada de la playa al mar
    slope_pairs: tuple[tuple[int, int], ...] = ()     # contactos entre mesetas (ladera); el resto, pasillo
    floor_noise_m: float = 0.25                       # variacion ligera del suelo de las zonas
    seed: int = 875
    extra: dict = field(default_factory=dict)

    # -- conversiones ----------------------------------------------------------------------
    def px_to_world(self, x_px, y_px):
        x_px, y_px = np.asarray(x_px, dtype=np.float64), np.asarray(y_px, dtype=np.float64)
        north = self.ref_world[0] + (self.ref_px[1] - y_px) * self.m_per_px
        east = self.ref_world[1] + (x_px - self.ref_px[0]) * self.m_per_px
        return north, east

    def world_to_px(self, north, east):
        north, east = np.asarray(north, dtype=np.float64), np.asarray(east, dtype=np.float64)
        x_px = self.ref_px[0] + (east - self.ref_world[1]) / self.m_per_px
        y_px = self.ref_px[1] - (north - self.ref_world[0]) / self.m_per_px
        return x_px, y_px

    @property
    def zone_classes(self) -> tuple[int, ...]:
        return tuple(sorted(self.zone_height_m))


# -- boceto -> clases ----------------------------------------------------------------------
CONFIDENT_RGB = 14.0       # distancia de color por debajo de la cual un pixel es de esa clase
SPECK_PX = 300             # componentes mas pequenas que esto son trazos o ruido de JPEG


def classify(rgb: np.ndarray, palette: dict[int, tuple[int, int, int]]) -> np.ndarray:
    """Clase por pixel donde el color es inequivoco; UNKNOWN en contornos, trazos y bocas grises."""
    keys = sorted(palette)
    colors = np.array([palette[k] for k in keys], dtype=np.float64)
    dist = np.linalg.norm(rgb[..., None, :].astype(np.float64) - colors[None, None], axis=-1)
    best = np.argmin(dist, axis=-1)
    labels = np.array(keys, dtype=np.int16)[best]
    return np.where(dist.min(axis=-1) <= CONFIDENT_RGB, labels, UNKNOWN).astype(np.int16)


def fill_unknown(labels: np.ndarray) -> np.ndarray:
    missing = labels == UNKNOWN
    if not missing.any():
        return labels
    _, (ii, jj) = ndimage.distance_transform_edt(missing, return_indices=True)
    return labels[ii, jj]


def drop_specks(labels: np.ndarray, min_px: int = SPECK_PX) -> np.ndarray:
    out = labels.copy()
    for cls in np.unique(labels):
        comp, count = ndimage.label(out == cls)
        if count == 0:
            continue
        sizes = np.bincount(comp.ravel(), minlength=count + 1)
        small = sizes < min_px
        small[0] = False
        out[small[comp]] = UNKNOWN
    return fill_unknown(out)


def polygon_mask(shape: tuple[int, int], poly) -> np.ndarray:
    from skimage.draw import polygon
    mask = np.zeros(shape, dtype=bool)
    xs, ys = zip(*poly)
    rr, cc = polygon(np.array(ys), np.array(xs), shape)
    mask[rr, cc] = True
    return mask


def capsule_mask(shape: tuple[int, int], axis_px, radius_px: float) -> np.ndarray:
    """Pixeles a menos de radius_px de la polilinea (muestreada cada 0,5 px)."""
    pts = polyline_points(axis_px, 0.5)
    line = np.zeros(shape, dtype=bool)
    ii = np.clip(np.rint(pts[:, 1]).astype(int), 0, shape[0] - 1)
    jj = np.clip(np.rint(pts[:, 0]).astype(int), 0, shape[1] - 1)
    line[ii, jj] = True
    return ndimage.distance_transform_edt(~line) <= radius_px


def polyline_points(axis_px, step_px: float = 1.0) -> np.ndarray:
    pts = np.asarray(axis_px, dtype=np.float64)
    seg = np.hypot(*np.diff(pts, axis=0).T)
    arc = np.concatenate([[0.0], np.cumsum(seg)])
    s = np.arange(0.0, arc[-1] + 1e-9, step_px)
    return np.stack([np.interp(s, arc, pts[:, 0]), np.interp(s, arc, pts[:, 1])], axis=1)


def class_map(spec: SketchSpec, rgb: np.ndarray) -> np.ndarray:
    """Clase por pixel del boceto, con los retoques que el dibujo deja abiertos: leyenda, cierre de la
    zona de abajo (el dibujo la corta en el borde), abanico de la playa al mar y la roca sobre cada
    tunel (el tramo cubierto pasa a pared)."""
    labels = drop_specks(fill_unknown(classify(rgb, spec.palette)))
    h, w = labels.shape
    x0, y0, x1, y1 = spec.legend_box_px
    labels[y0:y1, x0:x1] = WALL
    cx, cy, rx, ry = spec.bottom_cap_px
    yy, xx = np.mgrid[0:h, 0:w]
    outside_cap = (yy > cy) & (((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2 > 1.0)
    labels[outside_cap & np.isin(labels, spec.zone_classes)] = WALL
    fan = polygon_mask(labels.shape, spec.sea_fan_px) & (labels == WALL)
    labels[fan] = spec.beach_class
    for tunnel in spec.tunnels:
        labels[tunnel_rock_mask(spec, tunnel, labels.shape)] = WALL
    return labels


def tunnel_rock_mask(spec: SketchSpec, tunnel: Tunnel, shape) -> np.ndarray:
    """Roca sobre el tramo cubierto de un tunel: capsula de semiancho + roca lateral."""
    pts = polyline_points(tunnel.axis_px)
    a, b = (int(round(f * (len(pts) - 1))) for f in tunnel.covered)
    radius = (tunnel.half_width_m + max(tunnel.rock_m, 4.0)) / spec.m_per_px
    return capsule_mask(shape, pts[a:b + 1], radius)


def load_rgb(path) -> np.ndarray:
    with Image.open(path) as image:
        return np.asarray(image.convert("RGB"))


# -- suelo de las zonas --------------------------------------------------------------------
@dataclass
class Contact:
    low: int
    high: int
    length_m: float
    kind: str                 # "pasillo" | "ladera" | "mar"
    half_m: float             # medio largo de la cuesta (a cada lado del contacto)


def _contacts(labels: np.ndarray, a: int, b: int) -> list[np.ndarray]:
    """Componentes del contacto entre las clases a y b (pixeles de a o b que tocan la otra)."""
    ma, mb = labels == a, labels == b
    st = np.ones((3, 3), bool)
    touch = (ma & ndimage.binary_dilation(mb, st)) | (mb & ndimage.binary_dilation(ma, st))
    comp, count = ndimage.label(touch, structure=st)
    return [comp == k for k in range(1, count + 1)]


def floor_heights(spec: SketchSpec, labels: np.ndarray, px_m: float) -> tuple[np.ndarray, list[Contact]]:
    """Cota del suelo (m sobre el agua) de cada pixel de zona o mar (NaN en la pared) y los contactos.

    Mesetas llanas a la cota de su clase. En cada contacto entre dos cotas distintas, una cuesta lineal
    a lo largo de la distancia geodesica al contacto (dentro de las dos zonas, sin cruzar paredes):
    medio desnivel a cada lado, con la pendiente del tipo de contacto."""
    heights = dict(spec.zone_height_m)
    heights[SEA] = spec.sea_floor_m
    floor = np.full(labels.shape, np.nan)
    for cls, z in heights.items():
        floor[labels == cls] = z
    best = np.full(labels.shape, np.inf)          # distancia (relativa) del contacto que manda
    ramp = floor.copy()
    contacts: list[Contact] = []
    classes = sorted(heights)
    for ia, a in enumerate(classes):
        for b in classes[ia + 1:]:
            if heights[a] == heights[b] or SEA in (a, b) and spec.beach_class not in (a, b):
                continue
            low, high = (a, b) if heights[a] < heights[b] else (b, a)
            for seed in _contacts(labels, a, b):
                length = float(seed.sum()) * px_m / 2.0          # el contacto tiene dos pixeles de grosor
                if length < 2.0:
                    continue
                if SEA in (a, b):
                    kind, grade = "mar", spec.sea_grade
                elif (low, high) in spec.slope_pairs or (high, low) in spec.slope_pairs:
                    kind, grade = "ladera", float(np.tan(np.radians(spec.slope_deg)))
                else:
                    kind, grade = "pasillo", spec.corridor_grade
                dz = heights[high] - heights[low]
                half = 0.5 * dz / grade
                contacts.append(Contact(low, high, length, kind, half))
                _apply_ramp(labels, seed, low, high, heights, half, px_m, ramp, best)
    return ramp, contacts


def _apply_ramp(labels, seed, low, high, heights, half_m, px_m, ramp, best) -> None:
    pad = int(np.ceil(half_m / px_m)) + 3
    rows, cols = np.nonzero(seed)
    r0, r1 = max(rows.min() - pad, 0), min(rows.max() + pad + 1, labels.shape[0])
    c0, c1 = max(cols.min() - pad, 0), min(cols.max() + pad + 1, labels.shape[1])
    win = labels[r0:r1, c0:c1]
    inside = (win == low) | (win == high)
    cost = np.where(inside, 1.0, np.inf)
    mcp = MCP_Geometric(cost)
    starts = np.argwhere(seed[r0:r1, c0:c1])
    dist, _ = mcp.find_costs(starts)
    d_m = np.where(np.isfinite(dist), dist * px_m, np.inf)
    mid = 0.5 * (heights[low] + heights[high])
    u = np.clip(d_m / half_m, 0.0, 1.0)
    value = np.where(win == high, mid + (heights[high] - mid) * u, mid - (mid - heights[low]) * u)
    rel = d_m / half_m
    take = inside & (rel < 1.0) & (rel < best[r0:r1, c0:c1])
    ramp[r0:r1, c0:c1][take] = value[take]
    best[r0:r1, c0:c1][take] = rel[take]


def route_length_px(labels: np.ndarray, zone_classes, start_px, goal_mask: np.ndarray) -> float:
    """Recorrido mas corto (pixeles) de la salida a goal_mask andando por las zonas (sin tuneles)."""
    walkable = np.isin(labels, zone_classes)
    cost = np.where(walkable | goal_mask, 1.0, np.inf)
    dist, _ = MCP_Geometric(cost).find_costs([(int(start_px[1]), int(start_px[0]))])
    return float(dist[goal_mask].min())
