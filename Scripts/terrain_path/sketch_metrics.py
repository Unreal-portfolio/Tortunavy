"""Medidas de un mapa de boceto (#875) que comprueban el generador y los tests de la variante:

  - centerline: recorrido por el centro de lo transitable (camino de coste mínimo que se aparta de los
    bordes) y su largo, suavizado para no contar el zigzag de la rejilla;
  - wall_height: altura de las paredes sobre el suelo que las toca (sirve igual para C01 y para D01);
  - floor_slopes: pendiente máxima en las mesetas (lejos de sus bordes) y en las cuestas entre ellas;
  - floor_triangles: triángulos de la malla sobre el suelo más empinados que lo andable y escalones;
  - tunnel_profile: altura libre (suelo a techo) y pendiente a lo largo del eje de un túnel, por rayos
    verticales contra la malla exportada.

Todo trabaja sobre rejillas de 1 m (filas = Norte, columnas = Este) o sobre la malla de los trozos, sin
depender del modelo que las generó.
"""

from __future__ import annotations

import math

import numpy as np
from scipy import ndimage
from skimage.graph import MCP_Geometric

from terrain_vol.validate import disk, local_slope_deg

WALKABLE_FLOOR_DEG = 44.765          # UCharacterMovementComponent::WalkableFloorAngle (sin cambiar en Source/)
MAX_STEP_HEIGHT_M = 0.45             # UCharacterMovementComponent::MaxStepHeight (45 cm, sin cambiar)
TURTLE_HEIGHT_M = 1.8                # cápsula de la tortuga: 90 cm de semialtura
SLOPE_MARGIN_DEG = 5.0               # margen pedido sobre lo andable en las cuestas
CLEARANCE_M = 10.0                   # el recorrido se aparta hasta esto de los bordes
EDGE_PENALTY = 4.0                   # sobrecoste de pegarse al borde (1 + EDGE_PENALTY en el borde)
SMOOTH_M = 9.0                       # ventana del suavizado del recorrido


# -- recorrido por el centro -------------------------------------------------------------------
def smooth_polyline(points: np.ndarray, window: int) -> np.ndarray:
    """Media móvil de la polilínea, con los extremos fijos."""
    if window < 3 or len(points) < window:
        return np.asarray(points, dtype=np.float64)
    pad = window // 2
    padded = np.pad(np.asarray(points, dtype=np.float64), ((pad, pad), (0, 0)), mode="edge")
    kernel = np.ones(window) / window
    out = np.stack([np.convolve(padded[:, k], kernel, mode="valid") for k in range(2)], axis=1)
    out[0], out[-1] = points[0], points[-1]
    return out


def polyline_length(points: np.ndarray) -> float:
    return float(np.hypot(*np.diff(points, axis=0).T).sum())


def centerline(walk: np.ndarray, start: tuple[int, int], goal, cell_m: float = 1.0,
               clearance_m: float = CLEARANCE_M, smooth_m: float = SMOOTH_M) -> tuple[np.ndarray, float]:
    """(puntos (fila, columna) del recorrido suavizado, largo en metros) de start a goal por walk.
    goal: celda (fila, columna) o máscara booleana (se toma la celda de menor coste)."""
    d = ndimage.distance_transform_edt(walk) * cell_m
    penalty = np.clip((clearance_m - d) / clearance_m, 0.0, 1.0) ** 2
    cost = np.where(walk, 1.0 + EDGE_PENALTY * penalty, np.inf)
    mcp = MCP_Geometric(cost)
    costs, _ = mcp.find_costs([tuple(int(v) for v in start)])
    if isinstance(goal, np.ndarray) and goal.dtype == bool:
        cand = np.argwhere(goal & np.isfinite(costs))
        if not len(cand):
            raise ValueError("la meta no se alcanza desde la salida")
        end = tuple(cand[np.argmin(costs[tuple(cand.T)])])
    else:
        end = tuple(int(v) for v in goal)
        if not np.isfinite(costs[end]):
            raise ValueError("la meta no se alcanza desde la salida")
    path = np.array(mcp.traceback(end), dtype=np.float64)
    window = max(3, int(round(smooth_m / cell_m)) | 1)
    pts = smooth_polyline(path, window)
    return pts, polyline_length(pts) * cell_m


# -- paredes -----------------------------------------------------------------------------------
def wall_height(height: np.ndarray, region: np.ndarray, radius_m: float = 15.0) -> dict:
    """Altura de pared sobre el suelo: en cada celda de suelo (region 0) que toca pared (region 1), lo
    más alto de la pared (regiones 1 y 2) a menos de radius_m menos la cota de esa celda."""
    floor, wall = region == 0, np.isin(region, (1, 2))
    border = floor & ndimage.binary_dilation(region == 1)
    masked = np.where(wall, height, -np.inf)
    top = ndimage.maximum_filter(masked, footprint=disk(radius_m), mode="constant", cval=-np.inf)
    rise = (top - height)[border]
    rise = rise[np.isfinite(rise)]
    return {"media_m": round(float(rise.mean()), 2), "p10_m": round(float(np.percentile(rise, 10)), 2),
            "p50_m": round(float(np.median(rise)), 2), "bordes": int(rise.size)}


# -- pendientes del suelo ----------------------------------------------------------------------
def floor_slopes(top: np.ndarray, plateau: np.ndarray, ramp: np.ndarray, floor: np.ndarray,
                 edge_m: float = 3.0) -> dict:
    """Pendiente (grados, mayor desnivel con las 4 vecinas) en las mesetas a más de edge_m de su borde
    (pared o cuesta) y en las cuestas entre mesetas, 1,5 m más dentro del borde de floor."""
    slope = local_slope_deg(top, floor)
    inner_floor = ndimage.binary_erosion(floor, disk(1.5))
    plateau_in = ndimage.binary_erosion(plateau & floor, disk(edge_m))
    ramp_in = ramp & inner_floor
    out = {}
    for key, mask in (("mesetas", plateau_in), ("cuestas", ramp_in)):
        values = slope[mask]
        out[key] = {"max_deg": round(float(values.max()), 2) if values.size else 0.0,
                    "p99_deg": round(float(np.percentile(values, 99)), 2) if values.size else 0.0,
                    "celdas": int(values.size)}
    return out


def floor_triangles(meshes, floor: np.ndarray, top: np.ndarray, origin_m: tuple[float, float],
                    limit_deg: float = WALKABLE_FLOOR_DEG - SLOPE_MARGIN_DEG) -> dict:
    """Sobre la malla (lista de (vértices en m del mundo, triángulos)): triángulos de la superficie (centro
    en floor y a menos de 0,75 m de la cota superior top, así que no cuentan las caras de las cuevas de
    debajo), su pendiente máxima, los que pasan de limit_deg y los escalones (cara de más de 60 grados con
    más desnivel que MaxStepHeight)."""
    worst, steep, steps, count = 0.0, 0, 0, 0
    for vertices, triangles in meshes:
        tri = vertices[triangles]
        cen = tri.mean(axis=1)
        i = np.rint(cen[:, 0] - origin_m[0]).astype(int)
        j = np.rint(cen[:, 1] - origin_m[1]).astype(int)
        inside = (i >= 0) & (i < floor.shape[0]) & (j >= 0) & (j < floor.shape[1])
        on = np.zeros(len(tri), dtype=bool)
        on[inside] = floor[i[inside], j[inside]] & (np.abs(cen[inside, 2] - top[i[inside], j[inside]]) < 0.75)
        normal = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
        norm = np.linalg.norm(normal, axis=1)
        up = np.abs(normal[:, 2]) / np.maximum(norm, 1e-12)
        angle = np.degrees(np.arccos(np.clip(up, 0.0, 1.0)))
        rise = np.ptp(tri[:, :, 2], axis=1)
        sel = on & (norm > 1e-9)
        count += int(sel.sum())
        if sel.any():
            worst = max(worst, float(angle[sel].max()))
        steep += int((sel & (angle > limit_deg)).sum())
        steps += int((sel & (angle > 60.0) & (rise > MAX_STEP_HEIGHT_M)).sum())
    return {"triangulos_suelo": count, "max_deg": round(worst, 2), "mas_empinados_que_limite": steep,
            "escalones": steps, "limite_deg": round(limit_deg, 2)}


# -- túneles -----------------------------------------------------------------------------------
def vertical_hits(x: float, y: float, triangles_world: np.ndarray) -> np.ndarray:
    """Cotas (ordenadas) en las que la vertical por (x, y) corta los triángulos (N, 3, 3) dados."""
    a, b, c = triangles_world[:, 0], triangles_world[:, 1], triangles_world[:, 2]
    v0, v1 = b[:, :2] - a[:, :2], c[:, :2] - a[:, :2]
    v2 = np.array([x, y]) - a[:, :2]
    den = v0[:, 0] * v1[:, 1] - v1[:, 0] * v0[:, 1]
    ok = np.abs(den) > 1e-12
    den = np.where(ok, den, 1.0)
    u = (v2[:, 0] * v1[:, 1] - v1[:, 0] * v2[:, 1]) / den
    v = (v0[:, 0] * v2[:, 1] - v2[:, 0] * v0[:, 1]) / den
    eps = -1e-9
    hit = ok & (u >= eps) & (v >= eps) & (u + v <= 1.0 - eps)
    z = a[:, 2] + u * (b[:, 2] - a[:, 2]) + v * (c[:, 2] - a[:, 2])
    return np.sort(z[hit])


def tunnel_profile(axis_pts: np.ndarray, axis_floor: np.ndarray, triangles_world: np.ndarray,
                   floor_tol_m: float = 1.5) -> dict:
    """A lo largo del eje (puntos en m del mundo y cota prevista del suelo): suelo real (el corte más
    cercano a la cota prevista), techo (el siguiente corte por encima), altura libre y pendiente."""
    floors, clear = [], []
    for (x, y), fz in zip(axis_pts, axis_floor):
        z = vertical_hits(float(x), float(y), triangles_world)
        near = z[np.abs(z - fz) <= floor_tol_m]
        if not len(near):
            floors.append(np.nan)
            clear.append(np.nan)
            continue
        f = float(near[np.argmin(np.abs(near - fz))])
        above = z[z > f + 0.3]
        floors.append(f)
        clear.append(float(above[0] - f) if len(above) else np.inf)
    floors, clear = np.array(floors), np.array(clear)
    step = np.hypot(*np.diff(axis_pts, axis=0).T)
    grade = np.abs(np.diff(floors)) / np.maximum(step, 1e-9)
    return {"muestras": int(len(floors)), "sin_suelo": int(np.isnan(floors).sum()),
            "sin_techo": int(np.isinf(clear).sum()),
            "alto_min_m": round(float(np.nanmin(np.where(np.isinf(clear), np.nan, clear))), 2),
            "alto_max_m": round(float(np.nanmax(np.where(np.isinf(clear), np.nan, clear))), 2),
            "pendiente_max_deg": round(math.degrees(math.atan(float(np.nanmax(grade)))), 2)}
