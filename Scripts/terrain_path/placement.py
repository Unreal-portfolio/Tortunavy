"""Colocación offline por reglas de diseño para los mapas «camino primero» (#652, coop de autor C01).

Con una semilla, recorre el grafo del camino (principal, lazos que son rodeos y lazos que son
atajos) y coloca, por este orden: una catapulta que salta un meandro, puzles de grupo (en el
principal si caben; si no, en un rodeo), parkour en los atajos, mecánicas,
enemigos y obstáculos (densidad inversa a la longitud de la ruta, con calma tras cada puzle o pico),
botín y decorado. Lo colocado a
mano (bloque "manual" del manifest) es intocable: cuenta como restricción y nunca se mueve ni se
borra; lo suprimido por los diseñadores ("suppressed") no se vuelve a crear. Las reglas y el
validador están en placement_rules.py; la CLI es Scripts/place_terrain_path.py.
"""

from __future__ import annotations

from dataclasses import dataclass, field, replace

import numpy as np

from . import placement_extras as extras
from .placement_catalog import (
    BASE_ENEMY_PER_100M,
    BASE_OBSTACLE_PER_100M,
    DENSITY_TOL,
    ENEMY,
    GAMEPLAY,
    GROUP_KINDS,
    HAZARDS,
    HOSTILE,
    HOSTILE_GAP_M,
    OBSTACLE,
    PARKOUR_ON_ROUTES,
    PUZZLE,
    PUZZLE_GAP_M,
    PUZZLES,
    ROUTE_FACTOR,
)
from .placement_rules import (
    Placement,
    Violation,
    calm_radius,
    curve_problems,
    exclusion_discs,
    footprint_points,
    position_problem,
    route_danger,
    tramo_intensity,
    validate,
)
from .placement_site import Site

PREFIX = {"puzzle": "pz", "mechanic": "mec", "enemy": "en", "obstacle": "ob", "loot": "bot",
          "decor": "dec", "vegetation": "veg"}
GROUP_EVERY_M = 220.0          # un puzle de grupo por cada tantos metros de principal (2 a 5)
GROUP_ON_COVERED = 15.0        # penalización (m de avance) de un tramo del principal con alternativa
GROUP_ON_DETOUR = 40.0         # penalización de un rodeo: el grupo puede saltárselo
REPAIR_ROUNDS = 80


@dataclass
class PlacementResult:
    auto: list[Placement]
    manual: list[Placement]
    violations: list[Violation] = field(default_factory=list)

    @property
    def all(self) -> list[Placement]:
        return self.manual + self.auto


class Planner:
    def __init__(self, site: Site, seed: int, manual=(), suppressed=(), include_pending: bool = False):
        self.site, self.seed = site, seed
        self.rng = np.random.default_rng(seed)
        self.manual = list(manual)
        self.suppressed = set(suppressed)
        self.include_pending = include_pending
        self.auto: list[Placement] = []
        self._ids = {p.id for p in self.manual}
        self._refresh()

    # -- estado ---------------------------------------------------------------------------------
    @property
    def items(self) -> list[Placement]:
        return self.manual + self.auto

    def _refresh(self) -> None:
        site, items = self.site, self.items
        self.discs = exclusion_discs(site)
        n = site.node_count
        puzzles = [p for p in items if p.category == PUZZLE]
        hostiles = [p for p in items if p.category in HOSTILE]
        dp = site.geodesic_from([(p.line, p.s) for p in puzzles])
        dh = site.geodesic_from([(p.line, p.s) for p in hostiles])
        self.d_puzzle = dp.min(axis=0) if len(puzzles) else np.full(n, np.inf)
        self.d_hostile = dh.min(axis=0) if len(hostiles) else np.full(n, np.inf)
        self.calm_ok = np.ones(n, dtype=bool)
        for k, p in enumerate(puzzles):
            self.calm_ok &= dp[k] >= calm_radius(p)

    def make_id(self, category: str, kind: str, line: int, s: float, q: float = 0.0) -> str:
        side = "d" if q > 0.5 else "i" if q < -0.5 else ""
        return f"{PREFIX[category]}-{kind}-l{line}-s{int(round(s))}{side}"

    def add(self, p: Placement, refresh: bool = True) -> Placement | None:
        if p.id in self.suppressed:
            return None
        pid, k = p.id, 2
        while pid in self._ids:
            pid, k = f"{p.id}-{k}", k + 1
        p = replace(p, id=pid)
        self._ids.add(pid)
        self.auto.append(p)
        if refresh and (p.category == PUZZLE or p.category in HOSTILE):
            self._refresh()
        return p

    def remove(self, p: Placement) -> None:
        self.auto.remove(p)
        self._refresh()

    # -- comprobaciones locales -----------------------------------------------------------------
    def clear_of_exclusions(self, p: Placement) -> bool:
        pts = footprint_points(self.site, p)
        for _why, c, r in self.discs:
            if float(np.min(np.hypot(*(pts - c).T))) < r:
                return False
        return True

    def clear_of_footprints(self, p: Placement, margin: float) -> bool:
        """p no pisa la huella de un puzle ni queda a menos de 'margin' de otra pieza de juego."""
        site = self.site
        mine = footprint_points(site, p)
        for o in self.items:
            if o.category not in GAMEPLAY or o.id == p.id or o.id == p.linked:
                continue
            if p.linked and o.linked == p.linked:
                continue
            if o.line == p.line and o.category == PUZZLE:
                a, b = o.footprint
                if a - margin <= p.s <= b + margin:
                    return False
            other = footprint_points(site, o)
            dist = np.min(np.hypot(mine[:, None, 0] - other[None, :, 0], mine[:, None, 1] - other[None, :, 1]))
            if dist < margin:
                return False
        return True

    def fits(self, p: Placement, margin: float = 4.0) -> bool:
        if p.id in self.suppressed or position_problem(self.site, p) is not None:
            return False
        if p.category in GAMEPLAY and not self.clear_of_exclusions(p):
            return False
        return self.clear_of_footprints(p, margin)

    def curve_ok(self, p: Placement) -> bool:
        curve = tramo_intensity(self.site, self.items + [p], p.line)
        return not curve_problems(curve, p.line == 0)

    # -- puzles -----------------------------------------------------------------------------------
    def _kinds(self, mode: str) -> list[str]:
        return [k for k, spec in PUZZLES.items() if spec.mode == mode and k in GROUP_KINDS + PARKOUR_ON_ROUTES
                and (spec.status == "mvp" or self.include_pending)]

    def puzzle_ok(self, p: Placement) -> bool:
        node = self.site.node(p.line, p.s)
        return (self.fits(p, margin=6.0) and self.d_puzzle[node] >= PUZZLE_GAP_M
                and self.d_hostile[node] >= calm_radius(p) and self.curve_ok(p))

    def _puzzle(self, kind: str, line: int, s: float, **params) -> Placement:
        spec = PUZZLES[kind]
        return Placement(self.make_id(PUZZLE, kind, line, s), PUZZLE, kind, line, float(s), 0.0, spec.length_m,
                         params={**spec.params, **params})

    def place_group_puzzles(self) -> None:
        """Puzles de grupo repartidos por el avance del recorrido: en el principal si cabe (mejor fuera
        de los tramos con alternativa) y, si no, en un rodeo (la ruta larga es la de pensar)."""
        site, main = self.site, self.site.main
        kinds = self._kinds("grupo")
        if not kinds:
            return
        order = list(self.rng.permutation(kinds))
        count = int(np.clip(round(main.length / GROUP_EVERY_M), 2, 5))
        d_start = site.geodesic_from([(0, 0.0)])[0]
        cand = []                                     # (penalización, línea, s)
        covered = np.zeros(len(main.arc), dtype=bool)
        for loop, parent, a, b in site.bifurcations():
            if parent.id == 0:
                covered |= (main.arc >= a) & (main.arc <= b)
            if loop.length > (b - a) * (1.0 + DENSITY_TOL):
                cand += [(GROUP_ON_DETOUR, loop.id, float(s)) for s in np.arange(0.0, loop.length, 2.0)]
        cand += [(GROUP_ON_COVERED if covered[main.index(s)] else 0.0, 0, float(s))
                 for s in np.arange(0.0, main.length, 2.0)]
        progress = np.array([d_start[site.node(lid, s)] for _pen, lid, s in cand])
        penalty = np.array([pen for pen, _lid, _s in cand])
        for k in range(count):
            target = main.length * (k + 1) / (count + 1) + float(self.rng.uniform(-25.0, 25.0))
            for i in np.argsort(np.abs(progress - target) + penalty):
                if self._try_group_at(cand[i][1], cand[i][2], order, k, d_start):
                    break

    def _main_puzzles(self) -> list[Placement]:
        return [p for p in self.items if p.category == PUZZLE and p.line == 0]

    def _try_group_at(self, line: int, s: float, order: list[str], k: int, d_start: np.ndarray) -> bool:
        here = d_start[self.site.node(line, s)]
        group = [(d_start[self.site.node(p.line, p.s)], p.kind) for p in self.items
                 if p.category == PUZZLE and PUZZLES.get(p.kind) and PUZZLES[p.kind].mode == "grupo"]
        before = [kind for g, kind in sorted(group) if g < here]
        after = [kind for g, kind in sorted(group) if g > here]
        neighbours = {before[-1] if before else None, after[0] if after else None}
        if line == 0:                       # y sus vecinos del principal, que se recorren seguidos
            main = sorted(self._main_puzzles(), key=lambda p: p.s)
            prev = [p.kind for p in main if p.s < s]
            nxt = [p.kind for p in main if p.s > s]
            neighbours |= set(prev[-1:]) | set(nxt[:1])
        for kind in order[k % len(order):] + order[:k % len(order)]:
            if kind in neighbours:
                continue
            p = self._puzzle(kind, line, s)
            if self.puzzle_ok(p):
                self.add(p)
                return True
        return False

    def place_route_parkour(self) -> None:
        kinds = self._kinds("parkour")
        if not kinds:
            return
        k = int(self.rng.integers(len(kinds)))
        for loop, _parent, a, b in self.site.bifurcations():
            if loop.length > (b - a) * (1.0 - DENSITY_TOL) or loop.length < 50.0:
                continue                      # solo los atajos claros llevan parkour
            grid = np.arange(0.0, loop.length, 2.0)
            for s in grid[np.argsort(np.abs(grid - loop.length / 2.0))]:
                p = self._puzzle(kinds[k % len(kinds)], loop.id, float(s))
                if self.puzzle_ok(p):
                    self.add(p)
                    k += 1
                    break

    def place_catapult_gap(self) -> None:
        extras.place_catapult_gap(self)

    # -- enemigos y obstáculos ---------------------------------------------------------------------
    def route_factor(self, line_id: int) -> np.ndarray:
        """Factor de densidad por muestra: >1 en atajos (la ruta corta), <1 en rodeos."""
        ln = self.site.line(line_id)
        logf = np.zeros(len(ln.arc))
        hits = np.zeros(len(ln.arc))
        for loop, parent, a, b in self.site.bifurcations():
            ratio = np.clip(loop.length / max(b - a, 1.0), *ROUTE_FACTOR)
            if loop.id == line_id:
                logf += -np.log(ratio)
                hits += 1.0
            elif parent.id == line_id:
                inside = (ln.arc >= a) & (ln.arc <= b)
                logf[inside] += np.log(ratio)
                hits[inside] += 1.0
        return np.exp(np.where(hits > 0, logf / np.maximum(hits, 1.0), 0.0))

    def hostile_free(self, line_id: int) -> np.ndarray:
        ln = self.site.line(line_id)
        base = self.site.node(line_id, 0.0)
        free = ~ln.blocked & self.calm_ok[base:base + len(ln.arc)]
        for _why, c, r in self.discs:
            free &= np.hypot(*(ln.points - c).T) >= r
        return free

    def hazard_at(self, line_id: int, s: float, category: str | None = None) -> Placement | None:
        ln = self.site.line(line_id)
        k = ln.index(s)
        biome, wet, hw = int(ln.biome[k]), bool(ln.water[k]), float(ln.half_width[k])
        counts = {}
        for o in self.items:
            counts[o.kind] = counts.get(o.kind, 0) + 1
        p_enemy = BASE_ENEMY_PER_100M / (BASE_ENEMY_PER_100M + BASE_OBSTACLE_PER_100M)
        first = category or (ENEMY if self.rng.random() < p_enemy else OBSTACLE)
        for cat in (first, OBSTACLE if first == ENEMY else ENEMY):
            specs = [h for h in HAZARDS.values() if h.category == cat and biome in h.biomes and h.water == wet
                     and hw >= h.min_half_width_m and counts.get(h.kind, 0) < h.max_per_map]
            if not specs:
                continue
            w = np.array([h.weight for h in specs])
            h = specs[int(self.rng.choice(len(specs), p=w / w.sum()))]
            q, yaw, extent, length = 0.0, 0.0, h.extent_m, 0.0
            if h.layout == "point":
                q = float(self.rng.uniform(-0.35, 0.35)) * hw
            elif h.layout == "across":
                yaw, extent = 90.0, 2.0 * hw + 2.0
            else:
                length = h.extent_m
            return Placement(self.make_id(cat, h.kind, line_id, s, q), cat, h.kind, line_id, float(s), q, length,
                             yaw, extent)
        return None

    def try_hostile(self, line_id: int, s_target: float, window: float = 20.0,
                    lo: float = 0.0, hi: float = np.inf) -> Placement | None:
        ln = self.site.line(line_id)
        free = self.hostile_free(line_id)
        base = self.site.node(line_id, 0.0)
        cand = np.nonzero(free & (np.abs(ln.arc - s_target) <= window) & (ln.arc >= lo) & (ln.arc <= hi))[0]
        for k in cand[np.argsort(np.abs(ln.arc[cand] - s_target))]:
            if self.d_hostile[base + k] < HOSTILE_GAP_M:
                continue
            p = self.hazard_at(line_id, float(ln.arc[k]))
            if p is not None and self.fits(p, margin=3.0) and self.curve_ok(p):
                return self.add(p)
        return None

    def place_hostiles(self) -> None:
        site = self.site
        d = site.geodesic_from([(0, 0.0), (0, site.main.length)])
        per_m = (BASE_ENEMY_PER_100M + BASE_OBSTACLE_PER_100M) / 100.0
        for ln in sorted(site.lines, key=lambda ln: ln.id):
            base = site.node(ln.id, 0.0)
            nodes = base + np.arange(len(ln.arc))
            progress = d[0, nodes] / np.maximum(d[0, nodes] + d[1, nodes], 1.0)
            # Cuántos: por la longitud entera de la ruta. Dónde: solo en lo libre (fuera de uniones,
            # túneles y calma de los puzles); la separación y la curva de intensidad frenan el exceso.
            dens = per_m * self.route_factor(ln.id) * (0.7 + 0.6 * progress) * np.gradient(ln.arc)
            n = int(float(dens.sum()) + self.rng.random())
            dens = dens * self.hostile_free(ln.id)
            total = float(dens.sum())
            if n == 0 or total <= 0.0:
                continue
            cdf = np.cumsum(dens) / total
            for k in range(n):
                u = (k + float(self.rng.uniform(0.2, 0.8))) / n
                self.try_hostile(ln.id, float(ln.arc[min(int(np.searchsorted(cdf, u)), len(ln.arc) - 1)]))

    def repair_density(self) -> None:
        """Ajusta cada bifurcación hasta que la ruta corta lleva más peligro por metro que la larga."""
        stuck: set[int] = set()
        for _ in range(REPAIR_ROUNDS):
            bad = [b for b in self._density_gaps() if b[0] not in stuck]
            if not bad:
                return
            loop_id, short, long_ = bad[0]
            if self.try_hostile(short[0], 0.5 * (short[1] + short[2]), window=0.5 * (short[2] - short[1]),
                                lo=short[1], hi=short[2]):
                continue
            removable = [p for p in self.auto if p.category in HOSTILE and p.line == long_[0]
                         and long_[1] <= p.s <= long_[2]]
            if removable and route_danger(self.site, self.items, *long_) > 0.0:
                self.remove(removable[int(self.rng.integers(len(removable)))])
            else:
                stuck.add(loop_id)

    def _density_gaps(self):
        out = []
        for loop, parent, a, b in self.site.bifurcations():
            sides = [(loop.id, 0.0, loop.length), (parent.id, a, b)]
            lengths = [loop.length, b - a]
            if max(lengths) <= 0.0 or (max(lengths) - min(lengths)) / max(lengths) < DENSITY_TOL:
                continue
            short, long_ = (0, 1) if lengths[0] < lengths[1] else (1, 0)
            dens = [route_danger(self.site, self.items, *sides[k]) / lengths[k] for k in (0, 1)]
            if dens[short] < dens[long_] - 1e-9 or dens[short] == 0.0:
                out.append((loop.id, sides[short], sides[long_]))
        return out

    # -- todo ---------------------------------------------------------------------------------------
    def run(self) -> PlacementResult:
        self.place_catapult_gap()
        self.place_group_puzzles()
        self.place_route_parkour()
        extras.place_mechanics(self)
        self.place_hostiles()
        self.repair_density()
        extras.place_loot(self)
        extras.place_decor(self)
        return PlacementResult(self.auto, self.manual, validate(self.site, self.items))


def generate(site: Site, seed: int, manual=(), suppressed=(), include_pending: bool = False) -> PlacementResult:
    return Planner(site, seed, manual, suppressed, include_pending).run()


def placement_counts(placements) -> dict[str, int]:
    out: dict[str, int] = {}
    for p in placements:
        out[p.category] = out.get(p.category, 0) + 1
    return out


