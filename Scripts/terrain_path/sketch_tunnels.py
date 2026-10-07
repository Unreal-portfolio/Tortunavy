"""Ejes de los túneles de un mapa de boceto (#875): muestras cada 0,5 m con la cota del suelo, el semiancho,
el alto y si están bajo las zonas (cueva) o bajo un cerro (túnel con roca encima). SketchModel las consulta
con un árbol (punto más cercano del eje) para tallar el hueco y pintar el suelo.

Cueva bajo las zonas (Tunnel.underground): el perfil se da por vértice del eje y se interpola a lo largo;
el tramo con techo es donde el suelo de las zonas queda por encima de la clave más ROOF_ROCK_M de roca.
"""

from __future__ import annotations

import numpy as np
from scipy.spatial import cKDTree

from .curves import resample
from .layout import WATER_M

TUNNEL_MOUTH_M = 3.0             # el hueco sigue esto más allá del tramo cubierto
ROOF_ROCK_M = 1.0                # roca mínima sobre la clave para contar como tramo con techo
CROWN = 1.04 * 1.06              # clave sobre el suelo / alto nominal, con el ruido de alto al máximo
KEYS = ("floor", "half", "height", "t", "rock", "span", "under")


def _vertex_arc(world_pts: np.ndarray) -> np.ndarray:
    return np.concatenate([[0.0], np.cumsum(np.hypot(*np.diff(world_pts, axis=0).T))])


def _profile(model, tunnel, world_pts, s, pts):
    """(suelo absoluto, semiancho, alto) por muestra del eje."""
    if not tunnel.profile:
        n = len(s)
        return None, np.full(n, tunnel.half_width_m), np.full(n, tunnel.height_m)
    if len(tunnel.profile) != len(world_pts):
        raise ValueError(f"{tunnel.name}: el perfil necesita un valor por vertice del eje")
    sv = _vertex_arc(world_pts) * (s[-1] / max(_vertex_arc(world_pts)[-1], 1e-9))
    floor_v = np.array([model.floor_at(*p) if z is None else z + WATER_M
                        for p, (z, _, _) in zip(world_pts, tunnel.profile)])
    half_v = np.array([h for _, h, _ in tunnel.profile])
    height_v = np.array([a for _, _, a in tunnel.profile])
    return np.interp(s, sv, floor_v), np.interp(s, sv, half_v), np.interp(s, sv, height_v)


def _roofed_range(model, pts, s, floor, height) -> tuple[float, float]:
    """Primer y último punto del eje con techo: suelo de la zona encima >= clave + ROOF_ROCK_M."""
    ground = np.array([model.floor_at(*p) for p in pts[::4]])
    roofed = ground >= floor[::4] + CROWN * height[::4] + ROOF_ROCK_M
    idx = np.nonzero(roofed)[0]
    if not len(idx):
        raise ValueError("la cueva no cabe bajo las zonas: ningun tramo con techo")
    return float(s[::4][idx[0]]), float(s[::4][idx[-1]])


def build(model):
    """(ejes, árbol de muestras, categorías por muestra) de los túneles del preset."""
    cats = {k: [] for k in KEYS}
    points, axes = [], []
    for tunnel in model.spec.tunnels:
        px = np.array(tunnel.axis_px, dtype=np.float64)
        wx, wy = model.spec.px_to_world(px[:, 0], px[:, 1])
        world_pts = np.stack([wx, wy], axis=1)
        pts, s = resample(world_pts, 0.5)
        length = s[-1]
        z0, z1 = (float(model.floor_at(*pts[k])) for k in (0, -1))
        floor, half, height = _profile(model, tunnel, world_pts, s, pts)
        if tunnel.underground:
            s0, s1 = _roofed_range(model, pts, s, floor, height)
        else:
            s0, s1 = (f * length for f in tunnel.covered)
            floor = np.interp(s, [s0, s1], [z0, z1])
        # La roca (sketch.tunnel_rock_mask) empieza un radio antes del tramo cubierto: el hueco también.
        reach0 = float(np.interp(s0, s, half)) + max(tunnel.rock_m, 4.0) + TUNNEL_MOUTH_M
        reach1 = float(np.interp(s1, s, half)) + max(tunnel.rock_m, 4.0) + TUNNEL_MOUTH_M
        keep = (s >= s0 - reach0 - 4.0) & (s <= s1 + reach1 + 4.0)
        span = (s >= s0 - reach0) & (s <= s1 + reach1)
        if tunnel.underground:
            # La cueva se talla a lo largo de todo el eje: la rampa de entrada empieza en el suelo de la zona,
            # antes del tramo con techo (donde el suelo coincide con la zona, el hueco solo corta aire).
            keep = np.ones_like(s, dtype=bool)
            span = keep.copy()
        t = (s - s0) / max(s1 - s0, 1e-9)
        axes.append({"name": tunnel.name, "pts": pts, "s": s, "floor": floor, "covered": (s0, s1),
                     "half": float(half.max()), "half_s": half, "height": float(height.max()), "rock": tunnel.rock_m,
                     "z": (z0, z1), "underground": tunnel.underground})
        points.append(pts[keep])
        n = int(keep.sum())
        for key, value in (("floor", floor[keep]), ("half", half[keep]), ("height", height[keep]), ("t", t[keep]),
                           ("rock", np.full(n, tunnel.rock_m)), ("span", span[keep].astype(float)),
                           ("under", np.full(n, float(tunnel.underground)))):
            cats[key].append(value)
    return axes, cKDTree(np.vstack(points)), {k: np.concatenate(v) for k, v in cats.items()}
