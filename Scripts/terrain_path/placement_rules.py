"""Reglas de diseño de la colocación (#652) y su validador.

validate(site, placements) devuelve las violaciones (lista vacía = el mapa cumple). Lo usan el
generador (terrain_path.placement), la CLI (--comprobar) y los tests. Reglas:

- posicion: dentro del camino, fuera de túneles, tableros, arcos y escalones; el agua solo para lo
  que vive en ella; huella del puzle dentro del camino y con su anchura mínima; decorado lejos del eje.
- alcanzable: se llega desde la salida y se sigue hasta la meta (grafo del camino).
- exclusion: nada de juego en uniones, cruces, salida ni meta.
- separacion_puzles: puzles separados PUZZLE_GAP_M por el camino.
- calma_puzle: sin enemigos ni obstáculos junto a un puzle.
- hostiles_separados: enemigos y obstáculos sin amontonar.
- curva: tramos de TRAMO_M; tras un pico, calma; el primero y el último del principal, tranquilos.
- densidad: en cada bifurcación, la ruta corta lleva más peligro por metro que la larga.
- secuencia: dos puzles seguidos del principal no son del mismo tipo.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np

from .placement_catalog import (
    CALM_MAX,
    DECOR,
    DECOR_AXIS_MIN_M,
    DENSITY_TOL,
    EXCLUDE_M,
    GAMEPLAY,
    HAZARDS,
    HOSTILE,
    HOSTILE_GAP_M,
    MECHANIC,
    PEAK_MIN,
    PUZZLE,
    PUZZLE_CALM_M,
    PUZZLE_GAP_M,
    PUZZLES,
    SHORT_MIN_FREE_M,
    TRAMO_M,
    VEGETATION,
    danger_of,
    intensity_of,
)
from .placement_site import Site

WATER_MECHANICS = frozenset({"Boardwalk"})


@dataclass(frozen=True)
class Placement:
    id: str
    category: str
    kind: str
    line: int
    s: float
    q: float = 0.0                   # desplazamiento lateral (m, con signo; + a la derecha del avance)
    length: float = 0.0              # huella a lo largo del camino (puzles, tramos)
    yaw_offset_deg: float = 0.0      # respecto a la tangente del camino
    extent_m: float = 0.0            # Extent del elemento (alambre, paso de quads, pasarela)
    size_scale: float = 1.0
    params: dict = field(default_factory=dict, compare=False, hash=False)
    source: str = "auto"             # "auto" (generador) o "manual" (diseñadores)

    @property
    def footprint(self) -> tuple[float, float]:
        return self.s - 0.5 * self.length, self.s + 0.5 * self.length


@dataclass(frozen=True)
class Violation:
    rule: str
    message: str
    ids: tuple[str, ...] = ()


def _by_category(placements, *cats) -> list[Placement]:
    return [p for p in placements if p.category in cats]


def exclusion_discs(site: Site) -> list[tuple[str, np.ndarray, float]]:
    """(motivo, centro xy, radio) de cada zona en la que no va nada de juego."""
    discs = [("salida", np.array(site.start[:2]), EXCLUDE_M["start"]),
             ("meta", np.array(site.end[:2]), EXCLUDE_M["end"])]
    discs += [("union", np.array(j), EXCLUDE_M["junction"]) for j in site.junctions]
    discs += [("cruce", np.array(c), EXCLUDE_M["crossing"]) for c in site.crossings]
    return discs


def footprint_points(site: Site, p: Placement) -> np.ndarray:
    ln = site.line(p.line)
    if p.length <= 0.0:
        return np.array([ln.at(p.s, p.q)[:2]])
    s0, s1 = p.footprint
    s = np.linspace(max(s0, 0.0), min(s1, ln.length), max(2, int(p.length) + 1))
    return np.array([ln.at(v, p.q)[:2] for v in s])


def position_problem(site: Site, p: Placement) -> str | None:
    """Motivo por el que p no puede estar donde está (o None)."""
    if p.line not in {ln.id for ln in site.lines}:
        return f"la línea {p.line} no existe"
    ln = site.line(p.line)
    if not 0.0 <= p.s <= ln.length:
        return f"s = {p.s:.1f} fuera de la línea {p.line} ({ln.length:.0f} m)"
    k = ln.index(p.s)
    if abs(p.q) > ln.half_width[k] + 0.01:
        return f"a {abs(p.q):.1f} m del eje con semiancho {ln.half_width[k]:.1f} m"
    if p.category in (DECOR, VEGETATION):
        return None if abs(p.q) >= DECOR_AXIS_MIN_M - 0.01 else f"decorado a {abs(p.q):.1f} m del eje"
    s0, s1 = p.footprint if p.length > 0.0 else (p.s, p.s)
    if s0 < -0.01 or s1 > ln.length + 0.01:
        return "la huella se sale de la línea"
    win = ln.window(s0, s1)
    if p.category in GAMEPLAY:
        if ln.blocked[win].any():
            return "en un túnel, tablero, arco o escalón"
        wet = bool(ln.water[win].any())
        if p.category in HOSTILE and p.kind in HAZARDS and HAZARDS[p.kind].water != wet:
            return "en el agua" if wet else "fuera del agua, donde vive"
        if p.category == MECHANIC and wet and p.kind not in WATER_MECHANICS:
            return "mecánica en el agua"
        if p.category == PUZZLE and wet:
            return "en el agua"
    if p.category == PUZZLE and p.kind in PUZZLES:
        need = PUZZLES[p.kind].min_half_width_m
        if float(ln.half_width[win].min()) < need - 0.01:
            return f"camino de {ln.half_width[win].min():.1f} m de semiancho; {p.kind} pide {need:.1f}"
    return None


def check_positions(site: Site, placements) -> list[Violation]:
    out = []
    for p in placements:
        why = position_problem(site, p)
        if why:
            out.append(Violation("posicion", f"{p.id}: {why}", (p.id,)))
    return out


def check_reachable(site: Site, placements) -> list[Violation]:
    ok = site.reachable()
    ids = {ln.id for ln in site.lines}
    return [Violation("alcanzable", f"{p.id}: no se llega desde la salida o no lleva a la meta", (p.id,))
            for p in placements if p.category not in (DECOR, VEGETATION) and p.line in ids
            and not ok[site.node(p.line, p.s)]]


def check_exclusions(site: Site, placements) -> list[Violation]:
    discs = exclusion_discs(site)
    out = []
    for p in placements:
        if p.category not in GAMEPLAY or p.line not in {ln.id for ln in site.lines}:
            continue
        pts = footprint_points(site, p)
        for why, c, r in discs:
            if float(np.min(np.hypot(*(pts - c).T))) < r - 0.01:
                out.append(Violation("exclusion", f"{p.id}: a menos de {r:.0f} m de {why}", (p.id,)))
                break
    return out


def _geo(site: Site, items: list[Placement]) -> np.ndarray:
    return site.geodesic_from([(p.line, p.s) for p in items])


def check_puzzle_gap(site: Site, placements) -> list[Violation]:
    puzzles = _by_category(placements, PUZZLE)
    d = _geo(site, puzzles)
    out = []
    for a in range(len(puzzles)):
        for b in range(a + 1, len(puzzles)):
            gap = d[a, site.node(puzzles[b].line, puzzles[b].s)]
            if gap < PUZZLE_GAP_M:
                out.append(Violation("separacion_puzles", f"{puzzles[a].id} y {puzzles[b].id} a {gap:.0f} m "
                                     f"(mínimo {PUZZLE_GAP_M:.0f})", (puzzles[a].id, puzzles[b].id)))
    return out


def calm_radius(p: Placement) -> float:
    return 0.5 * p.length + PUZZLE_CALM_M


def check_calm(site: Site, placements) -> list[Violation]:
    puzzles = _by_category(placements, PUZZLE)
    hostiles = _by_category(placements, *HOSTILE)
    d = _geo(site, puzzles)
    out = []
    for a, pz in enumerate(puzzles):
        for h in hostiles:
            gap = d[a, site.node(h.line, h.s)]
            if gap < calm_radius(pz):
                out.append(Violation("calma_puzle", f"{h.id} a {gap:.0f} m de {pz.id} (calma de "
                                     f"{calm_radius(pz):.0f} m)", (pz.id, h.id)))
    return out


def check_hostile_gap(site: Site, placements) -> list[Violation]:
    hostiles = _by_category(placements, *HOSTILE)
    d = _geo(site, hostiles)
    out = []
    for a in range(len(hostiles)):
        for b in range(a + 1, len(hostiles)):
            gap = d[a, site.node(hostiles[b].line, hostiles[b].s)]
            if gap < HOSTILE_GAP_M:
                out.append(Violation("hostiles_separados", f"{hostiles[a].id} y {hostiles[b].id} a {gap:.0f} m",
                                     (hostiles[a].id, hostiles[b].id)))
    return out


def tramo_intensity(site: Site, placements, line_id: int) -> np.ndarray:
    ln = site.line(line_id)
    curve = np.zeros(max(1, int(math.ceil(ln.length / TRAMO_M))))
    for p in placements:
        if p.line == line_id:
            curve[min(int(p.s // TRAMO_M), len(curve) - 1)] += intensity_of(p.category, p.kind)
    return curve


def curve_problems(curve: np.ndarray, is_main: bool) -> list[str]:
    out = []
    if is_main and len(curve) > 2:
        if curve[0] > CALM_MAX:
            out.append(f"el primer tramo tiene intensidad {curve[0]:.1f} (máximo {CALM_MAX})")
        if curve[-1] > CALM_MAX:
            out.append(f"el último tramo tiene intensidad {curve[-1]:.1f} (máximo {CALM_MAX})")
    for k in range(len(curve) - 1):
        if curve[k] >= PEAK_MIN and curve[k + 1] > CALM_MAX:
            out.append(f"tramo {k} es un pico ({curve[k]:.1f}) y el siguiente no da calma ({curve[k + 1]:.1f})")
    return out


def check_curve(site: Site, placements) -> list[Violation]:
    out = []
    for ln in site.lines:
        for why in curve_problems(tramo_intensity(site, placements, ln.id), ln.id == 0):
            out.append(Violation("curva", f"línea {ln.id}: {why}"))
    return out


def free_hostile_mask(site: Site, placements) -> np.ndarray:
    """Muestras donde podría ir un enemigo u obstáculo: abiertas, fuera de exclusiones y de la calma
    de los puzles. Se usa para no exigir peligro a una ruta corta sin sitio."""
    free = np.ones(site.node_count, dtype=bool)
    discs = exclusion_discs(site)
    for ln in site.lines:
        base = site.node(ln.id, 0.0)
        mask = ~ln.blocked
        for _why, c, r in discs:
            mask &= np.hypot(*(ln.points - c).T) >= r
        free[base:base + len(ln.arc)] = mask
    puzzles = _by_category(placements, PUZZLE)
    if puzzles:
        d = _geo(site, puzzles)
        for a, pz in enumerate(puzzles):
            free &= d[a] >= calm_radius(pz)
    return free


def route_danger(site: Site, placements, line_id: int, s0: float, s1: float) -> float:
    return sum(danger_of(p.category, p.kind) for p in placements if p.line == line_id and s0 <= p.s <= s1)


def check_density(site: Site, placements) -> list[Violation]:
    out = []
    free = None
    for loop, parent, s_out, s_back in site.bifurcations():
        sides = [(loop.id, 0.0, loop.length), (parent.id, s_out, s_back)]
        lengths = [loop.length, s_back - s_out]
        if max(lengths) <= 0.0 or (max(lengths) - min(lengths)) / max(lengths) < DENSITY_TOL:
            continue
        short, long_ = (0, 1) if lengths[0] < lengths[1] else (1, 0)
        dens = [route_danger(site, placements, *sides[k]) / lengths[k] for k in (0, 1)]
        tag = f"lazo {loop.id} ({lengths[0]:.0f} m) frente a línea {parent.id} ({lengths[1]:.0f} m)"
        if dens[short] < dens[long_] - 1e-9:
            out.append(Violation("densidad", f"{tag}: la corta tiene {100 * dens[short]:.2f} peligros/100 m y la "
                                 f"larga {100 * dens[long_]:.2f}"))
            continue
        if dens[short] == 0.0:
            free = free_hostile_mask(site, placements) if free is None else free
            line_id, a, b = sides[short]
            ln = site.line(line_id)
            nodes = site.node(line_id, 0.0) + np.arange(ln.index(a), ln.index(b) + 1)
            if free[nodes].sum() >= SHORT_MIN_FREE_M:
                out.append(Violation("densidad", f"{tag}: la corta no tiene ningún peligro"))
    return out


def check_sequence(site: Site, placements) -> list[Violation]:
    main_puzzles = sorted((p for p in placements if p.category == PUZZLE and p.line == 0), key=lambda p: p.s)
    return [Violation("secuencia", f"{a.id} y {b.id} son seguidos y del mismo tipo", (a.id, b.id))
            for a, b in zip(main_puzzles, main_puzzles[1:]) if a.kind == b.kind]


CHECKS = (check_positions, check_reachable, check_exclusions, check_puzzle_gap, check_calm, check_hostile_gap,
          check_curve, check_density, check_sequence)


def validate(site: Site, placements) -> list[Violation]:
    placements = list(placements)
    out = []
    for check in CHECKS:
        out += check(site, placements)
    return out
