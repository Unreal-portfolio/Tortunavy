"""Hechos del mapa que necesita la colocación por reglas (#652): los caminos del grafo con su cota,
anchura, bioma y tramos bloqueados, las uniones, los cruces, la salida y la meta, y el grafo de
distancias por el camino (geodésicas).

Se construye a partir de un PathModel (site_from_model) o a mano en los tests (Site con SiteLine
sintéticas): el generador y el validador solo leen un Site, nunca el modelo del terreno.

Convenciones de terrain_path: metros, X = Norte, Y = Este; arco s a lo largo de cada camino.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from functools import cached_property

import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import dijkstra

BIOME_NAMES = ("acantilado", "agua", "dunas", "playa")

# Márgenes de los tramos bloqueados (m de arco a cada lado)
DECK_MARGIN_M = 6.0        # tablero de un puente (cruce o barranco): estrecho y sin paredes
ARCH_MARGIN_M = 4.0        # arco de roca sobre el camino
STEP_BEFORE_M = 8.0        # escalón de medusa: el pie, donde rebota la medusa
STEP_AFTER_M = 4.0
CROSS_LOWER_M = 14.0       # camino de abajo de un cruce: tramo cubierto por el de arriba


@dataclass(frozen=True)
class SiteLine:
    """Un camino del grafo muestreado cada ~1 m de arco."""

    id: int
    parent: int | None             # None = camino principal
    points: np.ndarray             # (N, 2)
    arc: np.ndarray                # (N,)
    z: np.ndarray                  # cota del suelo del camino (con los rellanos de medusa)
    half_width: np.ndarray         # semiancho del suelo andable
    biome: np.ndarray              # índice de BIOME_NAMES
    blocked: np.ndarray            # túnel, tablero, arco, escalón: nada que no sea decorado
    water: np.ndarray              # tramo de río (se cruza por islas y bajíos)
    s_out: float = 0.0             # arco en el padre donde sale
    s_back: float = 0.0            # arco en el padre donde vuelve

    @property
    def length(self) -> float:
        return float(self.arc[-1])

    def index(self, s: float) -> int:
        return int(np.clip(np.searchsorted(self.arc, s), 0, len(self.arc) - 1))

    def point(self, s: float) -> np.ndarray:
        return np.array([np.interp(s, self.arc, self.points[:, 0]), np.interp(s, self.arc, self.points[:, 1])])

    def tangent(self, s: float) -> np.ndarray:
        d = self.point(min(s + 2.0, self.length)) - self.point(max(s - 2.0, 0.0))
        return d / max(float(np.linalg.norm(d)), 1e-9)

    def normal(self, s: float) -> np.ndarray:
        t = self.tangent(s)
        return np.array([-t[1], t[0]])

    def yaw_deg(self, s: float) -> float:
        t = self.tangent(s)
        return math.degrees(math.atan2(t[1], t[0]))

    def at(self, s: float, q: float = 0.0) -> tuple[float, float, float]:
        """Punto del suelo a arco s y desplazamiento lateral q (m, con signo)."""
        p = self.point(s) + self.normal(s) * q
        return float(p[0]), float(p[1]), float(np.interp(s, self.arc, self.z))

    def window(self, s0: float, s1: float) -> slice:
        return slice(self.index(max(s0, 0.0)), self.index(min(s1, self.length)) + 1)


@dataclass(frozen=True)
class Site:
    name: str
    lines: tuple[SiteLine, ...]
    start: tuple[float, float, float]
    end: tuple[float, float, float]
    junctions: tuple[tuple[float, float], ...]     # salidas y vueltas de los lazos
    crossings: tuple[tuple[float, float], ...]     # cruces a distinto nivel
    extras: dict = field(default_factory=dict)      # datos informativos (p. ej. medusas)

    def line(self, line_id: int) -> SiteLine:
        return self.lines[[ln.id for ln in self.lines].index(line_id)]

    @property
    def main(self) -> SiteLine:
        return self.line(0)

    # -- grafo de distancias por el camino ---------------------------------------------------
    @cached_property
    def _offsets(self) -> dict[int, int]:
        out, n = {}, 0
        for ln in self.lines:
            out[ln.id] = n
            n += len(ln.arc)
        return out

    @cached_property
    def node_count(self) -> int:
        return sum(len(ln.arc) for ln in self.lines)

    def node(self, line_id: int, s: float) -> int:
        return self._offsets[line_id] + self.line(line_id).index(s)

    @cached_property
    def graph(self):
        """Matriz dispersa simétrica: muestras consecutivas de cada camino y, en cada lazo, sus dos
        extremos unidos al padre en s_out y s_back. Un lazo cuyo padre no existe queda suelto."""
        rows, cols, weights = [], [], []
        ids = {ln.id for ln in self.lines}
        for ln in self.lines:
            base = self._offsets[ln.id]
            k = np.arange(len(ln.arc) - 1)
            rows += list(base + k)
            cols += list(base + k + 1)
            weights += list(np.maximum(np.diff(ln.arc), 1e-6))
            if ln.parent is None or ln.parent not in ids:
                continue
            parent = self.line(ln.parent)
            for k_line, s_parent in ((0, ln.s_out), (len(ln.arc) - 1, ln.s_back)):
                p_node = self.node(parent.id, s_parent)
                gap = float(np.linalg.norm(ln.points[k_line] - parent.points[parent.index(s_parent)]))
                rows.append(base + k_line)
                cols.append(p_node)
                weights.append(max(gap, 1e-6))
        n = self.node_count
        m = coo_matrix((weights, (rows, cols)), shape=(n, n)).tocsr()
        return m + m.T

    def geodesic_from(self, sources: list[tuple[int, float]], limit: float = np.inf) -> np.ndarray:
        """Distancia por el camino desde cada (línea, s) de 'sources' a todas las muestras: (k, N)."""
        if not sources:
            return np.zeros((0, self.node_count))
        idx = [self.node(lid, s) for lid, s in sources]
        return np.atleast_2d(dijkstra(self.graph, directed=False, indices=idx, limit=limit))

    def reachable(self) -> np.ndarray:
        """Muestras a las que se llega desde la salida y desde las que se llega a la meta."""
        d = self.geodesic_from([(0, 0.0), (0, self.main.length)])
        return np.isfinite(d[0]) & np.isfinite(d[1])

    # -- proyección -----------------------------------------------------------------------------
    def project(self, xy) -> tuple[int, float, float]:
        """(línea, s, distancia lateral) de la muestra de camino más cercana a xy."""
        best = (0, 0.0, np.inf)
        p = np.asarray(xy[:2], dtype=float)
        for ln in self.lines:
            d = np.hypot(*(ln.points - p).T)
            k = int(np.argmin(d))
            if d[k] < best[2]:
                best = (ln.id, float(ln.arc[k]), float(d[k]))
        return best

    def bifurcations(self) -> list[tuple[SiteLine, SiteLine, float, float]]:
        """(lazo, padre, s_out, s_back) de cada lazo con su padre presente."""
        ids = {ln.id for ln in self.lines}
        return [(ln, self.line(ln.parent), ln.s_out, ln.s_back) for ln in self.lines
                if ln.parent is not None and ln.parent in ids]


def _ranges_mask(arc: np.ndarray, ranges) -> np.ndarray:
    out = np.zeros(len(arc), dtype=bool)
    for a, b in ranges:
        out |= (arc >= a) & (arc <= b)
    return out


def site_from_model(model, name: str) -> Site:
    """Site de un PathModel ya construido (terrain_path.model)."""
    from .steps import lift

    graph, plan = model.plan.graph, model.plan
    river_lines = set(model.river.banks) if getattr(model, "river", None) else set()
    lines = []
    for ln in graph.lines:
        prof = plan.profiles[ln.id]
        arc = ln.arc
        z = prof.z + (lift(arc, model.jump_steps) if ln.id == 0 else 0.0)
        ranges = [(a - DECK_MARGIN_M, b + DECK_MARGIN_M) for lid, a, b in model.deck_cuts if lid == ln.id]
        ranges += [(a - ARCH_MARGIN_M, b + ARCH_MARGIN_M) for lid, a, b in model.arch_ranges if lid == ln.id]
        ranges += [(c.s_lower - CROSS_LOWER_M, c.s_lower + CROSS_LOWER_M) for c in plan.crossings if c.lower == ln.id]
        if ln.id == 0:
            ranges += [(st.s0 - STEP_BEFORE_M, st.s_end + STEP_AFTER_M) for st in model.jump_steps]
        blocked = prof.tunnel.astype(bool) | _ranges_mask(arc, ranges)
        water = np.zeros(len(arc), dtype=bool)
        if ln.id in river_lines:
            bank_arc = model.river.banks[ln.id][0]
            water = (prof.biome == 1) & (arc >= float(np.min(bank_arc))) & (arc <= float(np.max(bank_arc)))
        lines.append(SiteLine(ln.id, ln.parent, ln.points.copy(), arc.copy(), np.asarray(z, dtype=float),
                              prof.half_width.copy(), prof.biome.astype(int), blocked, water, ln.s_out, ln.s_back))
    main = lines[0]
    junctions = tuple((float(p[0]), float(p[1])) for ln in graph.loops() for p in (ln.points[0], ln.points[-1]))
    crossings = tuple((float(c.point[0]), float(c.point[1])) for c in plan.crossings)
    extras = {"jellyfish_m": [list(map(float, st.jelly)) for st in model.jump_steps],
              "canyon_s_main": [float(c.s_main) for c in model.canyons]}
    return Site(name, tuple(lines), main.at(0.0), main.at(main.length), junctions, crossings, extras)
