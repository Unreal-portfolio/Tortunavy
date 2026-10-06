"""Partes de la colocación (#652) que no deciden el ritmo: las mecánicas de tránsito, el botín y el
decorado. Reciben el Planner de placement.py."""

from __future__ import annotations

import math

import numpy as np
from scipy.spatial import cKDTree

from .placement_catalog import (
    DECOR,
    DECOR_AXIS_MIN_M,
    DECOR_BY_BIOME,
    DECOR_STEP_M,
    LOOT,
    MECHANIC,
    PUZZLE,
    VEGETATION,
    VEGETATION_BY_BIOME,
)
from .placement_rules import Placement, footprint_points

MECHANIC_GAP_M = 70.0
RISE_M = 1.8                       # subida en 10 m a partir de la cual un trampolín ayuda
SEARCH_SPOTS = 10
SHELL_GROUP = 5
SHELL_EVERY_M = (55.0, 70.0)       # principal, lazos


def _yaw(a, b) -> float:
    return math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))


# -- mecánicas -----------------------------------------------------------------------------------------
def _runs(mask: np.ndarray, arc: np.ndarray, min_len: float) -> list[tuple[float, float]]:
    out, start = [], None
    for k, flag in enumerate(np.append(mask, False)):
        if flag and start is None:
            start = k
        if not flag and start is not None:
            if arc[k - 1] - arc[start] >= min_len:
                out.append((float(arc[start]), float(arc[k - 1])))
            start = None
    return out


def _mechanic(pl, kind: str, line: int, s: float, q: float = 0.0, length: float = 0.0, extent: float = 0.0,
              **params) -> Placement | None:
    p = Placement(pl.make_id(MECHANIC, kind, line, s, q), MECHANIC, kind, line, float(s), q, length, 0.0, extent,
                  params=params)
    if not pl.fits(p, margin=8.0):
        return None
    node = pl.site.node(line, s)
    others = [(o.line, o.s) for o in pl.items if o.category == MECHANIC]
    if others and pl.site.geodesic_from(others)[:, node].min() < MECHANIC_GAP_M:
        return None
    if not pl.curve_ok(p):
        return None
    return pl.add(p)


def place_mechanics(pl) -> None:
    site = pl.site
    # Agua: pasarela a la entrada del tramo de río más largo en el que quepa.
    runs = sorted(((ln.id, a, b) for ln in site.lines for a, b in _runs(ln.water, ln.arc, 20.0)),
                  key=lambda r: r[2] - r[1], reverse=True)
    for lid, a, b in runs:
        length = min(20.0, b - a - 4.0)
        if any(_mechanic(pl, "Boardwalk", lid, float(s), length=length, extent=length) is not None
               for s in np.arange(a + 4.0, b - 4.0, 3.0)):
            break
    # Subidas: trampolín (o pala rampa en las dunas) al pie, a un lado del camino.
    rises = []
    for ln in site.lines:
        z10 = np.interp(ln.arc + 10.0, ln.arc, ln.z) - ln.z
        for k in np.nonzero((z10 >= RISE_M) & ~ln.blocked & ~ln.water)[0][::3]:
            rises.append((float(z10[k]) + float(pl.rng.uniform(0.0, 0.5)), ln.id, float(ln.arc[k])))
    placed = 0
    for _gain, lid, s in sorted(rises, reverse=True):
        if placed >= 3:
            break
        ln = site.line(lid)
        k = ln.index(s)
        kind = "SpadeRamp" if ln.biome[k] == 2 else "Trampoline"
        q = float(pl.rng.choice([-1.0, 1.0])) * 0.5 * float(ln.half_width[k])
        if _mechanic(pl, kind, lid, s, q=q) is not None:
            placed += 1


# -- botín ------------------------------------------------------------------------------------------------
def _loot(pl, kind: str, line: int, s: float, q: float = 0.0, margin: float = 4.0) -> Placement | None:
    p = Placement(pl.make_id(LOOT, kind, line, s, q), LOOT, kind, line, float(s), q)
    ln = pl.site.line(line)
    k = ln.index(s)
    if ln.blocked[k] or ln.water[k] or not pl.fits(p, margin=margin):
        return None
    return pl.add(p, refresh=False)


def place_loot(pl) -> None:
    site = pl.site
    # Rebuscas al pie de las paredes, repartidas por longitud.
    total = sum(ln.length for ln in site.lines)
    for k in range(SEARCH_SPOTS):
        u = (k + float(pl.rng.uniform(0.2, 0.8))) / SEARCH_SPOTS * total
        for ln in site.lines:
            if u > ln.length:
                u -= ln.length
                continue
            for s in np.clip(u + np.arange(0.0, 30.0, 3.0), 0.0, ln.length):
                k_s = ln.index(float(s))
                q = float(pl.rng.choice([-1.0, 1.0])) * max(0.0, float(ln.half_width[k_s]) - 1.0)
                if _loot(pl, "SearchSpot", ln.id, float(s), q=q) is not None:
                    break
            break
    # Conchas de puntos: ristras de 5 por el centro del camino.
    for ln in site.lines:
        every = SHELL_EVERY_M[0] if ln.id == 0 else SHELL_EVERY_M[1]
        for s0 in np.arange(25.0, ln.length - 10.0, every):
            for j in range(SHELL_GROUP):
                _loot(pl, "ScoreShell", ln.id, float(s0 + 3.0 * j), margin=2.0)


# -- decorado ---------------------------------------------------------------------------------------------
def place_decor(pl) -> None:
    """Decorado y vegetación junto a las paredes (a más de DECOR_AXIS_MIN_M del eje), fuera de túneles,
    tableros, agua y huellas de puzle, y a más de 3 m de cualquier otra pieza."""
    site = pl.site
    taken = [footprint_points(site, o) for o in pl.items]
    tree = cKDTree(np.vstack(taken)) if taken else None
    junctions = np.array(site.junctions) if site.junctions else np.zeros((0, 2))
    spans = [(o.line, *o.footprint) for o in pl.items if o.category == PUZZLE]
    for ln in site.lines:
        side = 1.0
        for s in np.arange(3.0, ln.length - 3.0, DECOR_STEP_M):
            s = float(s + pl.rng.uniform(-1.5, 1.5))
            k = ln.index(s)
            hw = float(ln.half_width[k])
            side = -side
            if ln.blocked[k] or ln.water[k] or hw < DECOR_AXIS_MIN_M + 0.6:
                continue
            if any(lid == ln.id and a - 3.0 <= s <= b + 3.0 for lid, a, b in spans):
                continue
            q = side * float(pl.rng.uniform(max(DECOR_AXIS_MIN_M, hw - 2.5), hw - 0.6))
            xy = np.array(ln.at(s, q)[:2])
            if tree is not None and tree.query(xy)[0] < 3.0:
                continue
            if len(junctions) and np.min(np.hypot(*(junctions - xy).T)) < 6.0:
                continue
            biome = int(ln.biome[k])
            if pl.rng.random() < 0.25:
                cat, kind = VEGETATION, str(pl.rng.choice(VEGETATION_BY_BIOME[biome]))
            else:
                cat, kind = DECOR, str(pl.rng.choice(DECOR_BY_BIOME[biome]))
            pl.add(Placement(pl.make_id(cat, kind, ln.id, s, q), cat, kind, ln.id, s, q, 0.0,
                             float(pl.rng.uniform(-180.0, 180.0)), 0.0, round(float(pl.rng.uniform(0.8, 1.2)), 2)),
                   refresh=False)


__all__ = ["place_decor", "place_loot", "place_mechanics"]
