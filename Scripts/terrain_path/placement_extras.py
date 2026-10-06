"""Partes de la colocación (#652) que no deciden el ritmo: la catapulta que salta un meandro, las
mecánicas de tránsito, el botín y el decorado. Reciben el Planner de placement.py."""

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
    PUZZLE_GAP_M,
    PUZZLES,
    VEGETATION,
    VEGETATION_BY_BIOME,
)
from .placement_rules import Placement, footprint_points

CATAPULT_RANGE_M = (22.0, 40.0)    # distancia en planta entre la catapulta y el aterrizaje
CATAPULT_SAVING = 3.0              # el camino a pie es al menos tantas veces el salto
MECHANIC_GAP_M = 70.0
RISE_M = 1.8                       # subida en 10 m a partir de la cual un trampolín ayuda
SEARCH_SPOTS = 10
SHELL_GROUP = 5
SHELL_EVERY_M = (55.0, 70.0)       # principal, lazos


def _yaw(a, b) -> float:
    return math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))


# -- catapulta ----------------------------------------------------------------------------------------
def _catapult_candidates(pl) -> list[tuple[int, float]]:
    out = []
    for ln in pl.site.lines:
        for s in np.arange(4.0, ln.length - 4.0, 3.0):
            p = pl._puzzle("catapult_gap", ln.id, float(s))
            if pl.fits(p, margin=6.0):
                out.append((ln.id, float(s)))
    return out


def place_catapult_gap(pl) -> None:
    """Catapulta en un meandro: el aterrizaje está a 22-40 m en planta y a más del triple a pie, más
    adelante en el recorrido. El camino a pie sigue siendo la vía larga (long_way)."""
    if "catapult_gap" not in PUZZLES:
        return
    site = pl.site
    cand = _catapult_candidates(pl)
    if len(cand) < 2:
        return
    xy = np.array([site.line(lid).point(s) for lid, s in cand])
    z = np.array([float(np.interp(s, site.line(lid).arc, site.line(lid).z)) for lid, s in cand])
    d_start = site.geodesic_from([(0, 0.0)])[0]
    nodes = np.array([site.node(lid, s) for lid, s in cand])
    pairs = cKDTree(xy).query_pairs(CATAPULT_RANGE_M[1], output_type="ndarray")
    best, best_gain = None, 0.0
    geo = {}
    for a, b in pairs:
        if d_start[nodes[a]] > d_start[nodes[b]]:
            a, b = b, a
        flat = float(np.hypot(*(xy[a] - xy[b])))
        if flat < CATAPULT_RANGE_M[0] or not -8.0 <= z[b] - z[a] <= 1.5:
            continue
        if a not in geo:
            geo[a] = site.geodesic_from([cand[a]])[0]
        walk = float(geo[a][nodes[b]])
        gain = walk - flat + float(pl.rng.uniform(0.0, 5.0))
        if walk >= max(80.0, CATAPULT_SAVING * flat) and gain > best_gain:
            if pl.d_puzzle[nodes[a]] >= PUZZLE_GAP_M and pl.d_puzzle[nodes[b]] >= PUZZLE_GAP_M:
                best, best_gain = (a, b), gain
    if best is None:
        return
    (la, sa), (lb, sb) = cand[best[0]], cand[best[1]]
    landing = site.line(lb).at(sb)
    puzzle = pl._puzzle("catapult_gap", la, sa, landing_m=[round(v, 2) for v in landing], landing_line=lb,
                        landing_s=round(sb, 1))
    if not pl.puzzle_ok(puzzle):
        return
    puzzle = pl.add(puzzle)
    if puzzle is None:
        return
    yaw = _yaw(xy[best[0]], xy[best[1]]) - site.line(la).yaw_deg(sa)
    pl.add(Placement(pl.make_id(MECHANIC, "Catapult", la, sa), MECHANIC, "Catapult", la, sa, 0.0, 0.0, yaw,
                     params={"puzzle_id": puzzle.id}))
    pl.add(Placement(pl.make_id(MECHANIC, "Trampoline", lb, sb), MECHANIC, "Trampoline", lb, sb, 0.0, 0.0, 0.0,
                     params={"puzzle_id": puzzle.id, "landing": True}))


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
    # Agua: ferri en el tramo de río más largo, pasarela a la entrada de otro y un géiser.
    runs = sorted(((ln.id, a, b) for ln in site.lines for a, b in _runs(ln.water, ln.arc, 20.0)),
                  key=lambda r: r[2] - r[1], reverse=True)
    water_kinds = ["MovingPlatform", "Boardwalk", "Geyser"]
    for lid, a, b in runs:
        if not water_kinds:
            break
        kind = water_kinds[0]
        grid = np.arange(a + 4.0, b - 4.0, 3.0)
        order = grid[np.argsort(np.abs(grid - 0.5 * (a + b)))] if kind != "Boardwalk" else grid
        for s in order:
            length = min(20.0, b - a - 4.0) if kind != "Geyser" else 0.0
            if _mechanic(pl, kind, lid, float(s), length=length, extent=length) is not None:
                water_kinds.pop(0)
                break
    # Géiser en la playa si no ha cabido en el agua.
    if "Geyser" in water_kinds:
        beach = [(ln.id, float(s)) for ln in site.lines for s in ln.arc[(ln.biome == 3) & ~ln.water][::4]]
        for k in pl.rng.permutation(len(beach)):
            if _mechanic(pl, "Geyser", *beach[k]) is not None:
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


__all__ = ["place_catapult_gap", "place_decor", "place_loot", "place_mechanics"]
