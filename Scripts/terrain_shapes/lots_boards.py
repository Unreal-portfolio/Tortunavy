"""Tableros de Todos contra Todos de la familia de A05 Tablero y A06 Panal: teselados de hexágonos y de cuadrados
que se leen desde arriba. Seis mapas de 200 x 200 m (lote B):

  - A07 Panal en pirámide: 37 columnas hexagonales que suben por anillos hasta el centro (rey de la colina);
  - A08 Panal roto: 24 hexágonos sueltos sobre el agua a cuatro cotas, unidos por puentes estrechos;
  - A09 Colmena: 61 celdas con charcas de muerte (celdas hundidas) y tres celdas reales elevadas;
  - A10 Ajedrez: 8 x 8 casillas pegadas a dos cotas, rampas a caballo de las aristas y torres en las esquinas;
  - A11 Zigurat: terrazas cuadradas concéntricas con foso y puentes sobre él;
  - A12 Damas: solo las 32 casillas oscuras, a tres cotas, unidas por pasarelas en diagonal.

Un tablero es un diccionario de casillas (Tile: centro y cota sobre el agua) más sus enlaces: rampas e istmos
pintados en el campo de alturas (ramp_link) o puentes naturales en 3D (bridge_link). Cada factoría devuelve
(ShapeMap, Extras) para kit_writer.write_kit_map; los ids se registran en MAPS.
"""

from __future__ import annotations

import math
from dataclasses import asdict, dataclass, replace
from typing import Callable, Iterable

import numpy as np

from .arena_extra import hex_centres, sd_hex
from .bridges import NaturalBridge
from .canvas import SEABED_M, Canvas, plateau, sd_box, smoothstep
from .kit import KitModel, above, ramp
from .kit_writer import Extras
from .model import ShapeMap

TCT_NESTS = 8
RAMP_DEG = 16.0              # pendiente de diseño de las rampas entre casillas
MAX_LINK_DEG = 18.0          # tope: el validador anda a 20 grados
SHORE_M, DROP_M = 0.3, 0.6   # canto de una casilla: pared casi vertical, como el panal
END_MARGIN_M = 2.0           # una rampa no llega a menos de esto del centro de su casilla
EDGE_INSET_M = 0.5           # un puente en cuesta arranca casi en el canto: más adentro deja escalón en la meseta

Key = tuple[int, int]
Point = tuple[float, float]
Pair = tuple[Key, Key]
SignedDistance = Callable[[np.ndarray, np.ndarray, float, float], np.ndarray]


@dataclass(frozen=True)
class Tile:
    """Casilla de un tablero: centro (e, n) y cota sobre el agua (negativa: charca)."""
    centre: Point
    h: float


# ── Grafo del tablero ────────────────────────────────────────────────────────────
def near_pairs(centres: dict[Key, Point], reach_m: float) -> list[Pair]:
    """Pares de casillas cuyos centros distan menos de reach_m, ordenados (deterministas)."""
    keys = sorted(centres)
    return [(a, b) for k, a in enumerate(keys) for b in keys[k + 1:] if math.dist(centres[a], centres[b]) < reach_m]


def connected(keys: Iterable[Key], pairs: list[Pair]) -> bool:
    """Todas las casillas de `keys` se alcanzan entre sí por los pares dados."""
    alive = set(keys)
    if not alive:
        return True
    near: dict[Key, list[Key]] = {k: [] for k in alive}
    for a, b in pairs:
        if a in alive and b in alive:
            near[a].append(b)
            near[b].append(a)
    stack = [min(alive)]
    seen = set(stack)
    while stack:
        for other in near[stack.pop()]:
            if other not in seen:
                seen.add(other)
                stack.append(other)
    return len(seen) == len(alive)


def spanning_links(keys: Iterable[Key], pairs: list[Pair], rng: np.random.Generator, extra: int) -> list[Pair]:
    """Árbol al azar que une todas las casillas más `extra` enlaces sueltos (los primeros que sobran)."""
    alive = sorted(keys)
    usable = [(a, b) for a, b in pairs if a in set(alive) and b in set(alive)]
    parent = {k: k for k in alive}

    def root(k: Key) -> Key:
        while parent[k] != k:
            parent[k] = parent[parent[k]]
            k = parent[k]
        return k

    tree, spare = [], []
    for idx in rng.permutation(len(usable)):
        a, b = usable[idx]
        if root(a) != root(b):
            parent[root(a)] = root(b)
            tree.append((a, b))
        else:
            spare.append((a, b))
    if len(tree) != len(alive) - 1:
        raise ValueError("el tablero no es conexo: hay casillas sin vecina")
    return tree + spare[:max(0, extra)]


def drop_tiles(keys: Iterable[Key], pairs: list[Pair], rng: np.random.Generator, count: int,
               keep: frozenset[Key] = frozenset()) -> list[Key]:
    """Elige `count` casillas al azar para quitarlas sin partir el tablero ni tocar las de `keep`."""
    order = sorted(keys)
    alive, dropped = list(order), []
    for idx in rng.permutation(len(order)):
        if len(dropped) == count:
            break
        key = order[idx]
        rest = [k for k in alive if k != key]
        if key not in keep and connected(rest, pairs):
            alive = rest
            dropped.append(key)
    if len(dropped) < count:
        raise ValueError(f"solo se pueden quitar {len(dropped)} casillas de {count} sin partir el tablero")
    return dropped


def walk_levels(links: list[Pair], start: Key, count: int, rng: np.random.Generator) -> dict[Key, int]:
    """Nivel (0..count - 1) de cada casilla por un paseo al azar sobre los enlaces desde `start`: cada casilla
    queda un nivel por encima o por debajo de la que la descubre (en los extremos, al mismo)."""
    near: dict[Key, list[Key]] = {}
    for a, b in links:
        near.setdefault(a, []).append(b)
        near.setdefault(b, []).append(a)
    out = {start: int(rng.integers(0, count))}
    queue = [start]
    while queue:
        key = queue.pop(0)
        for other in near.get(key, []):
            if other not in out:
                out[other] = int(np.clip(out[key] + rng.choice((-1, 1)), 0, count - 1))
                queue.append(other)
    return out


def settle_levels(index: dict[Key, int], links: list[Pair]) -> dict[Key, int]:
    """Baja las casillas que sobresalen hasta que dos enlazadas no difieren más de un nivel."""
    out = dict(index)
    changed = True
    while changed:
        changed = False
        for a, b in links:
            hi, lo = (a, b) if out[a] > out[b] else (b, a)
            if out[hi] - out[lo] > 1:
                out[hi] = out[lo] + 1
                changed = True
    return out


def hex_ring(key: Key) -> int:
    """Anillo de una celda axial (q, r) alrededor de la central."""
    q, r = key
    return max(abs(q), abs(r), abs(q + r))


# ── Pintura del campo de alturas ─────────────────────────────────────────────────
def paint_tiles(e, n, tiles: dict[Key, Tile], sd_of: SignedDistance, grow_m: float = 0.0) -> np.ndarray:
    """Campo de alturas de un tablero: cada píxel toma la cota de su casilla y baja al fondo fuera de todas.
    grow_m = SHORE_M en tableros de casillas pegadas: la costura entre dos casillas no hace surco."""
    best = np.full(e.shape, -1e3)
    level = np.zeros(e.shape)
    for tile in tiles.values():
        sd = sd_of(e, n, *tile.centre)
        level = np.where(sd > best, tile.h, level)
        best = np.maximum(best, sd)
    return SEABED_M + (above(level) - SEABED_M) * smoothstep(-DROP_M, SHORE_M, best + grow_m)


def _axis(a: Point, b: Point) -> tuple[float, float, float]:
    dist = math.dist(a, b)
    return (b[0] - a[0]) / dist, (b[1] - a[1]) / dist, dist


def ramp_link(height: np.ndarray, e, n, a: Tile, b: Tile, width_m: float, inset_m: float):
    """Enlace pintado entre dos casillas vecinas: istmo llano (misma cota, de inset_m del centro de una a
    inset_m del de la otra) o rampa a RAMP_DEG centrada en la arista común. Devuelve (campo, pasarela)."""
    ue, un, dist = _axis(a.centre, b.centre)
    half = dist / 2.0 - inset_m if a.h == b.h else 0.5 * abs(b.h - a.h) / math.tan(math.radians(RAMP_DEG))
    if half > dist / 2.0 - END_MARGIN_M:
        raise ValueError(f"rampa de {2.0 * half:.1f} m entre casillas a {dist:.1f} m: no cabe a {RAMP_DEG} grados")
    mid = (0.5 * (a.centre[0] + b.centre[0]), 0.5 * (a.centre[1] + b.centre[1]))
    pa = (mid[0] - ue * half, mid[1] - un * half)
    pb = (mid[0] + ue * half, mid[1] + un * half)
    out = ramp(height, e, n, pa, pb, above(a.h), above(b.h), width_m, blend_m=0.6)
    return out, ((pa[0] - ue * 1.5, pa[1] - un * 1.5), (pb[0] + ue * 1.5, pb[1] + un * 1.5))


def paint_links(height: np.ndarray, e, n, tiles: dict[Key, Tile], links: list[Pair], width_m: float, inset_m: float):
    """Pinta todos los enlaces (ramp_link) y devuelve (campo, pasarelas a validar)."""
    corridors = []
    for a, b in links:
        height, corridor = ramp_link(height, e, n, tiles[a], tiles[b], width_m, inset_m)
        corridors.append(corridor)
    return height, corridors


def bridge_link(a: Tile, b: Tile, width_m: float, reach_m: float, pier_m: float = 2.5) -> NaturalBridge:
    """Puente natural entre dos casillas, de reach_m del centro de una a reach_m del de la otra."""
    ue, un, _ = _axis(a.centre, b.centre)
    pa = (a.centre[0] + ue * reach_m, a.centre[1] + un * reach_m)
    pb = (b.centre[0] - ue * reach_m, b.centre[1] - un * reach_m)
    bridge = NaturalBridge(pa, pb, above(a.h), above(b.h), width_m=width_m, rise_m=0.3 if a.h == b.h else 0.0,
                           pier_m=pier_m, overlap_m=1.0)
    if bridge.slope_deg() > MAX_LINK_DEG:
        raise ValueError(f"puente de {bridge.slope_deg():.1f} grados: pasa de {MAX_LINK_DEG}")
    return bridge


def jump_points(tiles: dict[Key, Tile], pairs: list[Pair], links: list[Pair]) -> list[Point]:
    """Punto medio de cada par de casillas secas vecinas sin enlace: el hueco que se salta."""
    linked = set(links)
    return [(0.5 * (tiles[a].centre[0] + tiles[b].centre[0]), 0.5 * (tiles[a].centre[1] + tiles[b].centre[1]))
            for a, b in pairs if a in tiles and b in tiles and (a, b) not in linked
            and tiles[a].h > 0.0 and tiles[b].h > 0.0]


def _outer(tiles: dict[Key, Tile], count: int) -> list[Key]:
    """Las `count` casillas secas más lejanas del centro del mapa (deterministas)."""
    dry = sorted(k for k, t in tiles.items() if t.h > 0.0)
    return sorted(dry, key=lambda k: -math.hypot(*tiles[k].centre))[:count]


def _finish(spec, generator: str, canvas: Canvas, model: KitModel, required: dict[str, Point], corridors: list,
            start: Point, end: Point | None, links: list[Pair], markers: dict) -> tuple[ShapeMap, Extras]:
    params = {"generator": generator, **asdict(spec), "links": [[list(a), list(b)] for a, b in links]}
    shape = ShapeMap(spec.name, spec.seed, spec.mode, spec.description, canvas, model, start, end, required, corridors,
                     corridor_width_m=3.0, corridor_slope_deg=20.0, params=params)
    return shape, Extras(nests=TCT_NESTS, markers=markers)


def _names(prefix: str, tiles: dict[Key, Tile]) -> dict[str, Point]:
    return {f"{prefix}_{i}_{j}": t.centre for (i, j), t in tiles.items() if t.h > 0.0}


def _hex_geometry(side_m: float, gap_m: float) -> tuple[float, float, SignedDistance]:
    """(apotema, paso entre centros, distancia con signo) de un panal de plano arriba."""
    apothem = side_m * math.sqrt(3.0) / 2.0
    return apothem, 2.0 * apothem + gap_m, lambda e, n, ce, cn: sd_hex(e, n, ce, cn, side_m)


def _square_centres(cells: int, pitch_m: float) -> dict[Key, Point]:
    """Centros (fila i hacia el Norte, columna j hacia el Este) de una rejilla cells x cells centrada."""
    offset = (cells - 1) * pitch_m / 2.0
    return {(i, j): (j * pitch_m - offset, i * pitch_m - offset) for i in range(cells) for j in range(cells)}


# ── A07 Panal en pirámide ────────────────────────────────────────────────────────
@dataclass(frozen=True)
class PyramidSpec:
    """Panal de `rings` anillos de columnas hexagonales de lado side_m separadas gap_m de agua; la cota la da
    el anillo (ring_heights[0] es la central). Un árbol de enlaces más extra_links: istmos llanos dentro de un
    anillo y rampas a 16 grados entre anillos."""
    name: str
    seed: int
    grid: int = 2
    rings: int = 3
    side_m: float = 12.0
    gap_m: float = 2.0
    ring_heights: tuple[float, ...] = (11.0, 8.0, 5.0, 2.0)
    link_m: float = 5.0
    extra_links: int = 12
    description: str = ""
    mode: str = "tct"


def pyramid_a07(seed: int = 3007) -> PyramidSpec:
    return PyramidSpec("A07_panal_piramide", seed, description=(
        "Panal en pirámide (Todos contra Todos): 37 columnas hexagonales de 12 m de lado separadas por 2 m de agua "
        "que suben por anillos de +2 a +5, +8 y +11 m en la central; istmos dentro de cada anillo y rampas a 16° "
        "entre anillos. Quien manda arriba ve todo el panal; caer entre columnas es la muerte."))


def build_pyramid(spec: PyramidSpec) -> tuple[ShapeMap, Extras]:
    canvas = Canvas(spec.grid)
    e, n = canvas.design_grid()
    apothem, pitch, sd_of = _hex_geometry(spec.side_m, spec.gap_m)
    centres = hex_centres(spec.rings, pitch)
    tiles = {k: Tile(c, spec.ring_heights[hex_ring(k)]) for k, c in centres.items()}
    pairs = near_pairs(centres, pitch * 1.01)
    links = spanning_links(tiles, pairs, np.random.default_rng(spec.seed), spec.extra_links)
    height = paint_tiles(e, n, tiles, sd_of)
    height, corridors = paint_links(height, e, n, tiles, links, spec.link_m, apothem - 3.0)
    outer = _outer(tiles, 3)
    markers = {"cofre": [(0.0, 0.0)], "salto": jump_points(tiles, pairs, links),
               "trampolin": [tiles[k].centre for k in outer]}
    return _finish(spec, "panal_piramide", canvas, KitModel(canvas, height), _names("hex", tiles), corridors,
                   tiles[(0, -spec.rings)].centre, (0.0, 0.0), links, markers)


# ── A08 Panal roto ───────────────────────────────────────────────────────────────
@dataclass(frozen=True)
class ShardsSpec:
    """Panal de `rings` anillos del que solo quedan `keep` hexágonos de lado side_m, separados gap_m de agua
    (no se salta) y a las cotas de `levels` (dos enlazados difieren un nivel como mucho). Los unen puentes
    naturales de bridge_m de ancho: un árbol más extra_links."""
    name: str
    seed: int
    grid: int = 2
    rings: int = 3
    side_m: float = 10.0
    gap_m: float = 9.0
    keep: int = 24
    levels: tuple[float, ...] = (2.0, 4.5, 7.0, 9.5)
    bridge_m: float = 3.5
    extra_links: int = 3
    description: str = ""
    mode: str = "tct"


def shards_a08(seed: int = 3008) -> ShardsSpec:
    return ShardsSpec("A08_panal_roto", seed, description=(
        "Panal roto (Todos contra Todos): 24 hexágonos de 10 m de lado sueltos sobre el agua a +2, +4,5, +7 y +9,5 m, "
        "separados 9 m y unidos por puentes naturales de 3,5 m de ancho; cada puente es un duelo y el que cae, muere."))


def build_shards(spec: ShardsSpec) -> tuple[ShapeMap, Extras]:
    canvas = Canvas(spec.grid)
    e, n = canvas.design_grid()
    apothem, pitch, sd_of = _hex_geometry(spec.side_m, spec.gap_m)
    centres = hex_centres(spec.rings, pitch)
    pairs = near_pairs(centres, pitch * 1.01)
    rng = np.random.default_rng(spec.seed)
    gone = set(drop_tiles(centres, pairs, rng, len(centres) - spec.keep, frozenset({(0, 0)})))
    alive = sorted(k for k in centres if k not in gone)
    links = spanning_links(alive, pairs, rng, spec.extra_links)
    index = settle_levels(walk_levels(links, (0, 0), len(spec.levels), rng), links)
    tiles = {k: Tile(centres[k], spec.levels[index[k]]) for k in alive}
    height = paint_tiles(e, n, tiles, sd_of)
    bridges = tuple(bridge_link(tiles[a], tiles[b], spec.bridge_m, apothem - EDGE_INSET_M) for a, b in links)
    corridors = [b.inner_points(1.5) for b in bridges]
    top = max(alive, key=lambda k: (tiles[k].h, -math.hypot(*tiles[k].centre)))
    markers = {"cofre": [tiles[top].centre], "trampolin": [tiles[k].centre for k in _outer(tiles, 3)],
               "puente_estrecho": [(0.5 * (b.a[0] + b.b[0]), 0.5 * (b.a[1] + b.b[1])) for b in bridges]}
    start = tiles[min(alive, key=lambda k: tiles[k].centre[1])].centre
    return _finish(spec, "panal_roto", canvas, KitModel(canvas, height, bridges), _names("hex", tiles), corridors,
                   start, None, links, markers)


# ── A09 Colmena ──────────────────────────────────────────────────────────────────
@dataclass(frozen=True)
class HiveSpec:
    """Colmena de `rings` anillos de celdas de lado side_m separadas gap_m de agua, a height_m; `pools` celdas
    al azar quedan hundidas pool_depth_m bajo el agua (charcas de muerte) y tres celdas reales del anillo 2 suben
    a royal_h_m. Un árbol de istmos y rampas entre las celdas secas más extra_links."""
    name: str
    seed: int
    grid: int = 2
    rings: int = 4
    side_m: float = 9.0
    gap_m: float = 2.0
    height_m: float = 3.0
    royal_h_m: float = 5.0
    pools: int = 10
    pool_depth_m: float = 0.8
    link_m: float = 4.0
    extra_links: int = 26
    description: str = ""
    mode: str = "tct"


def hive_a09(seed: int = 3009) -> HiveSpec:
    return HiveSpec("A09_colmena", seed, description=(
        "Colmena (Todos contra Todos): 61 celdas hexagonales de 9 m de lado a +3 m separadas por 2 m de agua; 10 "
        "celdas hundidas son charcas de muerte y tres celdas reales a +5 m, con rampa, guardan el botín. Istmos de "
        "4 m unen las celdas secas; el resto de huecos se salta."))


def _royal_cells(rng: np.random.Generator) -> frozenset[Key]:
    """Tres esquinas alternas del anillo 2 (a 120 grados entre sí); la semilla elige cuál de las dos ternas."""
    corners = ((2, 0), (0, 2), (-2, 2), (-2, 0), (0, -2), (2, -2))
    return frozenset(corners[int(rng.integers(0, 2))::2])


def build_hive(spec: HiveSpec) -> tuple[ShapeMap, Extras]:
    canvas = Canvas(spec.grid)
    e, n = canvas.design_grid()
    apothem, pitch, sd_of = _hex_geometry(spec.side_m, spec.gap_m)
    centres = hex_centres(spec.rings, pitch)
    pairs = near_pairs(centres, pitch * 1.01)
    rng = np.random.default_rng(spec.seed)
    royals = _royal_cells(rng)
    pools = set(drop_tiles(centres, pairs, rng, spec.pools, royals | {(0, 0)}))
    tiles = {k: Tile(c, -spec.pool_depth_m if k in pools else spec.royal_h_m if k in royals else spec.height_m)
             for k, c in centres.items()}
    dry = sorted(k for k in tiles if k not in pools)
    links = spanning_links(dry, pairs, rng, spec.extra_links)
    height = paint_tiles(e, n, tiles, sd_of)
    height, corridors = paint_links(height, e, n, tiles, links, spec.link_m, apothem - 3.0)
    markers = {"charca": [tiles[k].centre for k in sorted(pools)], "cofre": [tiles[k].centre for k in sorted(royals)],
               "salto": jump_points(tiles, pairs, links), "trampolin": [tiles[k].centre for k in _outer(tiles, 3)]}
    start = tiles[min(dry, key=lambda k: tiles[k].centre[1])].centre
    return _finish(spec, "colmena", canvas, KitModel(canvas, height), _names("celda", tiles), corridors, start,
                   None, links, markers)


# ── A10 Ajedrez ──────────────────────────────────────────────────────────────────
@dataclass(frozen=True)
class ChessSpec:
    """Tablero de cells x cells casillas pegadas de size_m: las claras a low_m y las oscuras a high_m, con el
    escalón vertical entre ellas. Rampas de ramp_m de ancho a caballo de las aristas (un árbol más extra_links)
    y una torre maciza de tower_m de lado a tower_h_m en el rincón de cada casilla de esquina."""
    name: str
    seed: int
    grid: int = 2
    cells: int = 8
    size_m: float = 18.0
    low_m: float = 2.0
    high_m: float = 5.0
    ramp_m: float = 4.0
    extra_links: int = 8
    tower_m: float = 7.0
    tower_h_m: float = 9.0
    description: str = ""
    mode: str = "tct"


def chess_a10(seed: int = 3010) -> ChessSpec:
    return ChessSpec("A10_ajedrez", seed, description=(
        "Ajedrez (Todos contra Todos): tablero de 8 × 8 casillas de 18 m pegadas, las claras a +2 m y las oscuras a "
        "+5 m con escalón vertical entre ellas; rampas de 4 m a caballo de las aristas y cuatro torres macizas de "
        "+9 m en las esquinas como cobertura. El borde del tablero cae al agua."))


def _towers(height: np.ndarray, e, n, spec: ChessSpec) -> tuple[np.ndarray, list[Point]]:
    """Torres macizas en el rincón exterior de las cuatro casillas de esquina (a 1 m del borde del tablero)."""
    reach = spec.cells * spec.size_m / 2.0 - 1.0 - spec.tower_m / 2.0
    centres = [(se * reach, sn * reach) for se in (-1.0, 1.0) for sn in (-1.0, 1.0)]
    for ce, cn in centres:
        sd = sd_box(e, n, ce, cn, spec.tower_m / 2.0, spec.tower_m / 2.0)
        height = np.maximum(height, plateau(sd, spec.tower_h_m, SHORE_M, DROP_M))
    return height, centres


def build_chess(spec: ChessSpec) -> tuple[ShapeMap, Extras]:
    canvas = Canvas(spec.grid)
    e, n = canvas.design_grid()
    half = spec.size_m / 2.0
    centres = _square_centres(spec.cells, spec.size_m)
    tiles = {k: Tile(c, spec.high_m if sum(k) % 2 else spec.low_m) for k, c in centres.items()}
    pairs = near_pairs(centres, spec.size_m * 1.01)
    links = spanning_links(tiles, pairs, np.random.default_rng(spec.seed), spec.extra_links)
    height = paint_tiles(e, n, tiles, lambda e, n, ce, cn: sd_box(e, n, ce, cn, half, half), grow_m=SHORE_M)
    height, corridors = paint_links(height, e, n, tiles, links, spec.ramp_m, half)
    height, towers = _towers(height, e, n, spec)
    last = spec.cells - 1
    required = _names("casilla", tiles)
    for i, j in ((0, 0), (0, last), (last, 0), (last, last)):          # el centro queda al pie de la torre
        ce, cn = centres[(i, j)]
        required[f"casilla_{i}_{j}"] = (ce - math.copysign(4.0, ce), cn - math.copysign(4.0, cn))
    markers = {"torre": towers, "trampolin": [required[f"casilla_{i}_{j}"] for i, j in ((0, 0), (last, last))],
               "catapulta": [(0.0, 0.0)]}
    return _finish(spec, "ajedrez", canvas, KitModel(canvas, height), required, corridors, required["casilla_0_0"],
                   None, links, markers)


# ── A11 Zigurat ──────────────────────────────────────────────────────────────────
@dataclass(frozen=True)
class ZigguratSpec:
    """Rejilla de cells x cells losas pegadas de size_m cuya cota depende del anillo cuadrado (ring_heights[0]
    es la losa central; None: foso de agua). Rampas a 16 grados entre terrazas (ramps[k] entre el anillo k y el
    k + 1) y un puente natural por lado sobre el foso, en una losa al azar."""
    name: str
    seed: int
    grid: int = 2
    cells: int = 9
    size_m: float = 16.0
    ring_heights: tuple[float | None, ...] = (11.0, 8.0, 5.0, None, 2.0)
    ramps: tuple[int, ...] = (2, 4)
    ramp_m: float = 5.0
    bridge_m: float = 4.0
    description: str = ""
    mode: str = "tct"


def ziggurat_a11(seed: int = 3011) -> ZigguratSpec:
    return ZigguratSpec("A11_zigurat", seed, description=(
        "Zigurat (Todos contra Todos): pirámide escalonada de losas de 16 m con terrazas a +5, +8 y +11 m, rodeada "
        "por un foso de agua de 16 m y una ribera cuadrada a +2 m; cuatro puentes naturales cruzan el foso y las "
        "rampas entre terrazas cambian de sitio con la semilla."))


def _square_ring(key: Key, cells: int) -> int:
    mid = cells // 2
    return max(abs(key[0] - mid), abs(key[1] - mid))


def _moat_pairs(spec: ZigguratSpec, rng: np.random.Generator) -> list[Pair]:
    """Un par (losa de la ribera, losa de la terraza baja) por lado, enfrentadas a través del foso."""
    mid = spec.cells // 2
    moat = spec.ring_heights.index(None)
    out = []
    for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        t = int(rng.integers(-(moat - 1), moat))
        inner = (mid + (moat - 1) * di + t * abs(dj), mid + (moat - 1) * dj + t * abs(di))
        out.append(((inner[0] + 2 * di, inner[1] + 2 * dj), inner))
    return out


def _terrace_ramps(spec: ZigguratSpec, tiles: dict[Key, Tile], pairs: list[Pair], rng: np.random.Generator) -> list[Pair]:
    """ramps[k] rampas al azar entre el anillo k y el k + 1 (de la cima hacia fuera)."""
    out = []
    for k, count in enumerate(spec.ramps):
        step = [(a, b) for a, b in pairs if a in tiles and b in tiles
                and {_square_ring(a, spec.cells), _square_ring(b, spec.cells)} == {k, k + 1}]
        out.extend(step[idx] for idx in rng.permutation(len(step))[:count])
    return out


def build_ziggurat(spec: ZigguratSpec) -> tuple[ShapeMap, Extras]:
    canvas = Canvas(spec.grid)
    e, n = canvas.design_grid()
    half = spec.size_m / 2.0
    centres = _square_centres(spec.cells, spec.size_m)
    tiles = {k: Tile(c, spec.ring_heights[_square_ring(k, spec.cells)]) for k, c in centres.items()
             if spec.ring_heights[_square_ring(k, spec.cells)] is not None}
    rng = np.random.default_rng(spec.seed)
    ramps = _terrace_ramps(spec, tiles, near_pairs(centres, spec.size_m * 1.01), rng)
    crossings = _moat_pairs(spec, rng)
    height = paint_tiles(e, n, tiles, lambda e, n, ce, cn: sd_box(e, n, ce, cn, half, half), grow_m=SHORE_M)
    height, corridors = paint_links(height, e, n, tiles, ramps, spec.ramp_m, half)
    bridges = tuple(bridge_link(tiles[a], tiles[b], spec.bridge_m, half - 1.5, pier_m=4.0) for a, b in crossings)
    corridors = corridors + [b.inner_points(1.5) for b in bridges]
    mid = spec.cells // 2
    corner = centres[(0, 0)]
    markers = {"cofre": [centres[(mid, mid)]], "trampolin": [corner, centres[(spec.cells - 1, spec.cells - 1)]],
               "puente_foso": [(0.5 * (b.a[0] + b.b[0]), 0.5 * (b.a[1] + b.b[1])) for b in bridges]}
    return _finish(spec, "zigurat", canvas, KitModel(canvas, height, bridges), _names("losa", tiles), corridors,
                   corner, centres[(mid, mid)], ramps + crossings, markers)


# ── A12 Damas ────────────────────────────────────────────────────────────────────
@dataclass(frozen=True)
class DraughtsSpec:
    """Tablero de cells x cells del que solo existen las casillas oscuras (size_m de lado, paso pitch_m): las
    claras son agua y las vecinas se tocan en diagonal. Cotas de `levels` al azar (dos enlazadas difieren un
    nivel como mucho) y pasarelas en diagonal de bridge_m de ancho: un árbol más extra_links."""
    name: str
    seed: int
    grid: int = 2
    cells: int = 8
    size_m: float = 15.0
    pitch_m: float = 21.0
    corner_m: float = 1.5
    levels: tuple[float, ...] = (3.0, 5.5, 8.0)
    bridge_m: float = 3.5
    extra_links: int = 8
    description: str = ""
    mode: str = "tct"


def draughts_a12(seed: int = 3012) -> DraughtsSpec:
    return DraughtsSpec("A12_damas", seed, description=(
        "Damas (Todos contra Todos): de un tablero de 8 × 8 solo existen las 32 casillas oscuras, mesetas de 15 m a "
        "+3, +5,5 y +8 m; las claras son agua. Se avanza como una ficha, en diagonal, por pasarelas naturales de 3,5 m "
        "de esquina a esquina."))


def build_draughts(spec: DraughtsSpec) -> tuple[ShapeMap, Extras]:
    canvas = Canvas(spec.grid)
    e, n = canvas.design_grid()
    half = spec.size_m / 2.0
    centres = {k: c for k, c in _square_centres(spec.cells, spec.pitch_m).items() if sum(k) % 2 == 0}
    pairs = near_pairs(centres, spec.pitch_m * 1.5)                      # solo las vecinas en diagonal
    rng = np.random.default_rng(spec.seed)
    links = spanning_links(centres, pairs, rng, spec.extra_links)
    index = settle_levels(walk_levels(links, (0, 0), len(spec.levels), rng), links)
    tiles = {k: Tile(c, spec.levels[index[k]]) for k, c in centres.items()}
    height = paint_tiles(e, n, tiles, lambda e, n, ce, cn: sd_box(e, n, ce, cn, half, half, corner=spec.corner_m))
    reach = (half - spec.corner_m) * math.sqrt(2.0) + spec.corner_m - EDGE_INSET_M
    bridges = tuple(bridge_link(tiles[a], tiles[b], spec.bridge_m, reach) for a, b in links)
    corridors = [b.inner_points(1.5) for b in bridges]
    last = spec.cells - 1
    markers = {"trampolin": [centres[(0, 0)], centres[(last, last)]],
               "catapulta": [tiles[k].centre for k in _outer(tiles, 2)],
               "pasarela": [(0.5 * (b.a[0] + b.b[0]), 0.5 * (b.a[1] + b.b[1])) for b in bridges]}
    return _finish(spec, "damas", canvas, KitModel(canvas, height, bridges), _names("casilla", tiles), corridors,
                   centres[(0, 0)], None, links, markers)


# ── Registro ─────────────────────────────────────────────────────────────────────
def _factory(default: Callable, builder: Callable) -> Callable[[int | None], tuple[ShapeMap, Extras]]:
    def make(seed: int | None = None) -> tuple[ShapeMap, Extras]:
        spec = default()
        return builder(spec if seed is None else replace(spec, seed=seed))
    return make


MAPS = {
    "A07": ("B", _factory(pyramid_a07, build_pyramid)),
    "A08": ("B", _factory(shards_a08, build_shards)),
    "A09": ("B", _factory(hive_a09, build_hive)),
    "A10": ("B", _factory(chess_a10, build_chess)),
    "A11": ("B", _factory(ziggurat_a11, build_ziggurat)),
    "A12": ("B", _factory(draughts_a12, build_draughts)),
}
