"""Arenas nuevas de Todos contra Todos (lote 1): coliseos y campos de batalla.

  - N01 Coliseo: arena central con foso, podio, tres gradas en anillo y talud exterior; 4 puertas (tunel bajo la
    corona y puente sobre el foso) y 4 escaleras en las diagonales;
  - N02 Anfiteatro: gradas en semicirculo talladas en una ladera que bajan a la orquesta y al escenario, junto
    al agua, con dos muelles laterales;
  - N03 Volcan-arena: crater con un rio de lava (agua: caer es la muerte) que lo parte en dos, tres pasos en el
    fondo y dos arcos en el borde;
  - N04 Atolon: anillo de arena con pasos de agua, laguna interior, islotes y puentes;
  - N17 Fortaleza en estrella: baluartes con adarve, foso, camino cubierto y glacis;
  - N18 Yin-yang: disco partido por un canal en S en dos lobulos a distinta cota, con una colina y un estanque
    como ojos.

Primero el suelo por donde se anda (mesetas, rampas a <= 18 grados, puentes) y despues el relieve alrededor.
Cotas sobre el agua <= +15 m. Cada factoria devuelve (ShapeMap, Extras) para kit_writer.write_kit_map.
"""

from __future__ import annotations

import math
from dataclasses import asdict, dataclass, replace
from typing import Callable

import numpy as np

from terrain_vol.layout import WATER_M
from terrain_vol.noise import Fbm2D

from .bridges import NaturalBridge
from .canvas import SEABED_M, Canvas, rotate, sd_box, sd_circle, smoothstep
from .kit import Axis, KitModel, Tunnel, above, along_polyline, ramp, ramp_slope_deg, sea_floor
from .kit_writer import Clearance, Extras
from .model import ShapeMap, ShapeModel

TCT_NESTS = 8
MAX_TOP_M = 15.0                 # cota maxima de un mapa de TcT sobre el agua
MAX_RAMP_DEG = 18.0              # rampas, cupulas y puentes (el validador anda a WALK_DEG)
WALK_DEG = 20.0

Point = tuple[float, float]


@dataclass(frozen=True)
class ArenaSpec:
    name: str
    seed: int
    description: str = ""
    grid: int = 2
    mode: str = "tct"


# ── Piezas comunes ───────────────────────────────────────────────────────────────
def _pt(r: float, deg: float, c: Point = (0.0, 0.0)) -> Point:
    return c[0] + r * math.cos(math.radians(deg)), c[1] + r * math.sin(math.radians(deg))


def _seeded(spec, seed: int | None):
    return spec if seed is None else replace(spec, seed=seed)


def _wobble(seed: int, e, n, amp_m: float, wavelength_m: float = 30.0) -> np.ndarray:
    """Ruido suave (semilla) de +-amp_m para que la costa no sea un circulo de compas."""
    return amp_m * Fbm2D(np.random.default_rng(seed), wavelength_m, octaves=3)(n + 300.0, e + 300.0)


def _lift(e, n, land_sd, level_m, shore_m: float = 1.0, drop_m: float = 1.0) -> np.ndarray:
    """Campo de alturas de la tierra land_sd (positiva dentro) a la cota level_m sobre el agua, con la orilla
    bajando al fondo entre shore_m dentro y drop_m fuera del contorno."""
    land = SEABED_M + (above(0.0) + level_m - SEABED_M) * smoothstep(-drop_m, shore_m, land_sd)
    return np.maximum(sea_floor(e, n, land_sd), land)


def _dome(e, n, c: Point, radius: float, rise: float) -> np.ndarray:
    """Cupula de rise m en el centro y 0 en el borde (pendiente maxima 1,5 * rise / radius)."""
    return rise * smoothstep(radius, 0.0, np.hypot(e - c[0], n - c[1]))


def dome_slope_deg(radius: float, rise: float) -> float:
    return math.degrees(math.atan(1.5 * rise / radius))


def _probe(canvas: Canvas, height: np.ndarray) -> Callable[[Point], float]:
    model = ShapeModel(canvas, height)

    def height_at(p: Point) -> float:
        X, Y = canvas.to_world(*p)
        return float(model.ground_height(np.array([X]), np.array([Y]))[0])
    return height_at


def _span(a: Point, b: Point, height_at, width_m: float = 4.0, rise_m: float = 0.3) -> NaturalBridge:
    """Puente natural de a a b con el tablero a la cota del suelo en cada extremo."""
    a, b = (float(a[0]), float(a[1])), (float(b[0]), float(b[1]))
    return NaturalBridge(a, b, height_at(a), height_at(b), width_m=width_m, rise_m=rise_m)


def _radial_ramp(height, e, n, deg: float, r0: float, r1: float, h0: float, h1: float, width_m: float,
                 c: Point = (0.0, 0.0)) -> tuple[np.ndarray, float, tuple[Point, Point]]:
    """Rampa radial (desde c) de r0 (cota h0) a r1 (h1): campo, pendiente y pasarela a validar."""
    a, b = _pt(r0, deg, c), _pt(r1, deg, c)
    height = ramp(height, e, n, a, b, above(h0), above(h1), width_m, blend_m=0.5)
    return height, ramp_slope_deg(h0, h1, a, b), (_pt(r0 - 2.0, deg, c), _pt(r1 + 2.0, deg, c))


def _static(height: np.ndarray, slopes: list[float]) -> dict[str, dict]:
    """Comprobaciones del generador: cota maxima y pendiente de rampas, cupulas y puentes."""
    top, steep = float(height.max()) - WATER_M, max(slopes)
    return {"cotas": {"ok": top <= MAX_TOP_M, "max_m": round(top, 2), "summary": f"máx. +{top:.1f} m"},
            "rampas": {"ok": steep <= MAX_RAMP_DEG, "max_deg": round(steep, 2), "summary": f"máx. {steep:.1f}°"}}


def _shape(spec: ArenaSpec, canvas: Canvas, model: ShapeModel, start: Point, end: Point | None, required: dict,
           corridors: list, **params) -> ShapeMap:
    return ShapeMap(spec.name, spec.seed, spec.mode, spec.description, canvas, model, start, end, required, corridors,
                    corridor_width_m=3.0, corridor_slope_deg=WALK_DEG, params={**asdict(spec), **params})


# ── N01 Coliseo ──────────────────────────────────────────────────────────────────
@dataclass(frozen=True)
class ColosseumSpec(ArenaSpec):
    """Arena de arena_r, foso de moat_m, podio de podium_m y gradas (fondo, cota) en anillo hacia fuera; la
    ultima grada es la corona y por fuera baja un talud de tierra a berm_deg hasta la playa. Puertas: carril a
    cielo abierto por las gradas bajas, tunel bajo la corona y puente sobre el foso."""
    arena_r: float = 20.0
    moat_m: float = 9.0
    podium_m: float = 7.0
    floor_h: float = 2.5
    tiers: tuple[tuple[float, float], ...] = ((8.0, 4.5), (8.0, 6.75), (8.0, 9.0))
    berm_deg: float = 17.0
    beach_m: float = 5.0
    gates_deg: tuple[float, ...] = (0.0, 90.0, 180.0, 270.0)
    gate_w_m: float = 7.0
    gate_clear_m: float = 4.2
    stairs_deg: tuple[float, ...] = (45.0, 135.0, 225.0, 315.0)
    stair_w_m: float = 5.0
    stair_slope_deg: float = 17.0


def colosseum_n01(seed: int = 4001) -> ColosseumSpec:
    return ColosseumSpec("N01_coliseo", seed, (
        "Coliseo (Todos contra Todos): arena de 40 m rodeada por un foso de 9 m, podio y tres gradas en anillo a "
        "+4,5, +6,75 y +9 m; 4 puertas (túnel bajo la corona y puente sobre el foso), 4 escaleras en las "
        "diagonales y talud de tierra por fuera hasta la playa."))


def colosseum_radii(spec: ColosseumSpec) -> tuple[float, tuple[float, ...], float, float, float]:
    """(borde interior del podio, borde interior de cada grada, borde exterior de la corona, pie del talud,
    costa)."""
    podium_in = spec.arena_r + spec.moat_m
    edges, r = [], podium_in + spec.podium_m
    for depth, _ in spec.tiers:
        edges.append(r)
        r += depth
    berm_out = r + (spec.tiers[-1][1] - spec.floor_h) / math.tan(math.radians(spec.berm_deg))
    return podium_in, tuple(edges), r, berm_out, berm_out + spec.beach_m


def tunnel_span(spec: ColosseumSpec) -> tuple[float, float]:
    """Radios de las dos bocas del tunel de una puerta: bajo la corona y el talud mientras queda 1,2 m de roca."""
    _, edges, crown_out, _, _ = colosseum_radii(spec)
    cover = spec.tiers[-1][1] - spec.floor_h - spec.gate_clear_m - 1.2
    return edges[-1], crown_out + cover / math.tan(math.radians(spec.berm_deg))


def _colosseum_gates(spec: ColosseumSpec, height, e, n) -> tuple[np.ndarray, list[Tunnel], list[Clearance]]:
    podium_in, _, _, berm_out, _ = colosseum_radii(spec)
    t_in, t_out = tunnel_span(spec)
    floor = above(spec.floor_h)
    tunnels, holes = [], []
    for k, deg in enumerate(spec.gates_deg):
        c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
        along, lateral = e * c + n * s, np.abs(-e * s + n * c)
        lane = (lateral <= spec.gate_w_m / 2.0) & (along >= podium_in + 1.0) & ((along < t_in) | (along > t_out))
        height = np.where(lane, np.minimum(height, floor), height)
        rr = np.arange(t_in - 1.5, t_out + 1.51, 0.5)
        axis = Axis.of(np.column_stack([rr * c, rr * s]), np.full(len(rr), floor))
        tunnels.append(Tunnel(axis, spec.gate_w_m, spec.gate_clear_m, name=f"puerta_{k + 1}"))
        rr = np.arange(podium_in + 2.0, berm_out + 1.0, 0.5)
        whole = Axis.of(np.column_stack([rr * c, rr * s]), np.full(len(rr), floor))
        holes.append(Clearance(whole, spec.gate_w_m, spec.gate_clear_m - 0.5, f"puerta_{k + 1}"))
    return height, tunnels, holes


def _colosseum_height(spec: ColosseumSpec, e, n) -> np.ndarray:
    r = np.hypot(e, n)
    podium_in, edges, crown_out, _, coast = colosseum_radii(spec)
    crown_h = spec.tiers[-1][1]
    level = np.full(e.shape, spec.floor_h)
    for r_in, (_, h) in zip(edges, spec.tiers):
        level = np.where(r >= r_in, h, level)
    berm = crown_h - (r - crown_out) * math.tan(math.radians(spec.berm_deg))
    level = np.where(r >= crown_out, np.maximum(berm, spec.floor_h), level)
    outer = coast - r + _wobble(spec.seed, e, n, 2.0)
    return _lift(e, n, np.maximum(spec.arena_r - r, np.minimum(r - podium_in, outer)), level)


def build_colosseum(seed: int | None = None, spec: ColosseumSpec | None = None) -> tuple[ShapeMap, Extras]:
    spec = _seeded(spec or colosseum_n01(), seed)
    canvas = Canvas(spec.grid)
    e, n = canvas.design_grid()
    podium_in, edges, crown_out, berm_out, _ = colosseum_radii(spec)
    crown_h = spec.tiers[-1][1]
    height = _colosseum_height(spec, e, n)
    r0 = edges[0] - 1.0
    r1 = r0 + (crown_h - spec.floor_h) / math.tan(math.radians(spec.stair_slope_deg))
    if r1 > crown_out - 2.0:
        raise ValueError(f"la escalera llega a r = {r1:.1f} m y la corona acaba en {crown_out:.1f} m")
    slopes, corridors = [], []
    for deg in spec.stairs_deg:
        height, slope, corridor = _radial_ramp(height, e, n, deg, r0, r1, spec.floor_h, crown_h, spec.stair_w_m)
        slopes.append(slope)
        corridors.append(corridor)
    height, tunnels, holes = _colosseum_gates(spec, height, e, n)
    probe = _probe(canvas, height)
    bridges = [_span(_pt(spec.arena_r - 1.5, deg), _pt(podium_in + 1.5, deg), probe, 5.0) for deg in spec.gates_deg]
    corridors += [b.inner_points(1.5) for b in bridges]
    slopes += [b.slope_deg() for b in bridges]
    crown_r = 0.5 * (edges[-1] + crown_out) + 1.5
    required = {"arena": (0.0, 0.0), "podio": _pt(podium_in + 3.5, 45.0), "grada_baja": _pt(edges[0] + 4.0, 30.0),
                "exterior": _pt(berm_out + 2.0, 45.0),
                **{f"corona_{k + 1}": _pt(crown_r, deg - 25.0) for k, deg in enumerate(spec.stairs_deg)}}
    markers = {"puerta": [_pt(tunnel_span(spec)[1] + 3.0, deg) for deg in spec.gates_deg], "catapulta": [(0.0, 0.0)],
               "trampolin": [_pt(crown_r, deg + 20.0) for deg in spec.stairs_deg]}
    model = KitModel(canvas, height, tuple(bridges), voids=tuple(tunnels))
    shape = _shape(spec, canvas, model, required["podio"], (0.0, 0.0), required, corridors, generator="coliseo")
    return shape, Extras(nests=TCT_NESTS, markers=markers, clearances=holes, static=_static(height, slopes),
                         notes="4 puertas con túnel")


# ── N02 Anfiteatro ───────────────────────────────────────────────────────────────
@dataclass(frozen=True)
class TheatreSpec(ArenaSpec):
    """Gradas (fondo, cota) en semicirculo al Norte de `centre` alrededor de la orquesta (orchestra_r); la
    ultima es la cresta de la ladera, que baja por detras a back_deg hasta la playa. Al Sur, el escenario
    (semiancho, fondo) sobre el agua con `columns` columnas y un muelle de quay_m a cada lado."""
    centre: Point = (0.0, -30.0)
    orchestra_r: float = 14.0
    floor_h: float = 2.0
    tiers: tuple[tuple[float, float], ...] = ((9.0, 4.0), (9.0, 6.0), (9.0, 8.0), (11.0, 10.0))
    back_deg: float = 17.0
    beach_m: float = 5.0
    quay_m: float = 7.0
    stage: Point = (22.0, 24.0)
    columns: int = 4
    column_h: float = 4.0
    stairs_deg: tuple[float, ...] = (45.0, 90.0, 135.0)
    stair_w_m: float = 5.0


def theatre_n02(seed: int = 4002) -> TheatreSpec:
    return TheatreSpec("N02_anfiteatro", seed, (
        "Anfiteatro (Todos contra Todos): cuatro gradas en semicírculo a +4, +6, +8 y +10 m talladas en una ladera "
        "que bajan a la orquesta y al escenario, a +2 m junto al agua; tres escaleras, dos muelles laterales al pie "
        "del muro y la ladera trasera bajando a la playa."))


def theatre_radii(spec: TheatreSpec) -> tuple[tuple[float, ...], float, float, float]:
    """(borde interior de cada grada, borde exterior de la cresta, pie de la ladera, costa), desde centre."""
    edges, r = [], spec.orchestra_r
    for depth, _ in spec.tiers:
        edges.append(r)
        r += depth
    back_out = r + (spec.tiers[-1][1] - spec.floor_h) / math.tan(math.radians(spec.back_deg))
    return tuple(edges), r, back_out, back_out + spec.beach_m


def theatre_columns(spec: TheatreSpec) -> list[Point]:
    half_w, depth = spec.stage
    return [(spec.centre[0] + float(x), spec.centre[1] - depth + 5.0)
            for x in np.linspace(-(half_w - 5.0), half_w - 5.0, spec.columns)]


def _theatre_height(spec: TheatreSpec, e, n) -> np.ndarray:
    ce, cn = spec.centre
    dn = n - cn
    r = np.hypot(e - ce, dn)
    edges, crest_out, _, coast = theatre_radii(spec)
    level = np.full(e.shape, spec.floor_h)
    for r_in, (_, h) in zip(edges, spec.tiers):
        level = np.where(r >= r_in, h, level)
    back = spec.tiers[-1][1] - (r - crest_out) * math.tan(math.radians(spec.back_deg))
    level = np.where(r >= crest_out, np.maximum(back, spec.floor_h), level)
    level = np.where(dn >= 0.0, level, spec.floor_h)
    half_w, depth = spec.stage
    stage = sd_box(e, n, ce, cn - depth / 2.0, half_w, depth / 2.0, corner=3.0)
    hill = np.minimum(coast - r + _wobble(spec.seed, e, n, 2.0), dn + spec.quay_m)
    height = _lift(e, n, np.maximum(hill, stage), level)
    for column in theatre_columns(spec):
        sd = sd_circle(e, n, *column, 2.0)
        top = above(spec.floor_h) + spec.column_h * smoothstep(-1.0, 0.3, sd)
        height = np.where(sd > -1.0, np.maximum(height, top), height)
    return height


def build_theatre(seed: int | None = None, spec: TheatreSpec | None = None) -> tuple[ShapeMap, Extras]:
    spec = _seeded(spec or theatre_n02(), seed)
    canvas = Canvas(spec.grid)
    e, n = canvas.design_grid()
    c = spec.centre
    edges, crest_out, back_out, _ = theatre_radii(spec)
    height = _theatre_height(spec, e, n)
    slopes, corridors = [spec.back_deg], []
    for deg in spec.stairs_deg:
        height, slope, corridor = _radial_ramp(height, e, n, deg, spec.orchestra_r - 2.0, edges[-1] + 2.0,
                                               spec.floor_h, spec.tiers[-1][1], spec.stair_w_m, c)
        slopes.append(slope)
        corridors.append(corridor)
    quay_n = c[1] - spec.quay_m / 2.0
    for side in (1.0, -1.0):
        corridors.append(((c[0] + side * (spec.orchestra_r + 2.0), quay_n), (c[0] + side * (back_out + 2.0), quay_n)))
    crest_r = 0.5 * (edges[-1] + crest_out) + 1.5
    required = {"escenario": (c[0], c[1] - spec.stage[1] / 2.0), "orquesta": c, "cresta": _pt(crest_r, 70.0, c),
                "playa": _pt(back_out + 2.5, 90.0, c), "muelle_este": (c[0] + 60.0, quay_n),
                "muelle_oeste": (c[0] - 60.0, quay_n)}
    markers = {"columna": theatre_columns(spec), "catapulta": [c],
               "trampolin": [_pt(crest_r, deg, c) for deg in (20.0, 110.0, 160.0)]}
    model = KitModel(canvas, height)
    shape = _shape(spec, canvas, model, required["escenario"], None, required, corridors, generator="anfiteatro")
    return shape, Extras(nests=TCT_NESTS, markers=markers, static=_static(height, slopes))


# ── N03 Volcan-arena ─────────────────────────────────────────────────────────────
@dataclass(frozen=True)
class CraterSpec(ArenaSpec):
    """Crater de fondo llano (floor_r, floor_h), pared interior hasta el borde (rim_in a rim_out, rim_h) y
    ladera exterior a outer_deg hasta la playa. Un rio de lava de river_w cruza el volcan por el centre con
    meandros (meander_m de amplitud cada meander_len_m) dentro del crater; pasos en `fords` (m a lo largo del
    eje, +- ford_jitter_m con la semilla) y un arco en cada corte del borde. Rampas a +-45 grados del eje."""
    floor_r: float = 36.0
    floor_h: float = 3.0
    rim_in: float = 44.0
    rim_out: float = 52.0
    rim_h: float = 10.0
    outer_deg: float = 16.0
    beach_h: float = 1.5
    beach_m: float = 5.0
    river_w_m: float = 7.0
    meander_m: float = 9.0
    meander_len_m: float = 64.0
    fords: tuple[float, ...] = (-22.0, 0.0, 22.0)
    ford_jitter_m: float = 4.0
    ramp_w_m: float = 5.0
    ramp_slope_deg: float = 17.0


def crater_n03(seed: int = 4003) -> CraterSpec:
    return CraterSpec("N03_volcan_arena", seed, (
        "Volcán-arena (Todos contra Todos): cráter de 72 m a +3 m con el borde a +10 m y un río de lava de 7 m "
        "(agua: caer es la muerte) que lo parte en dos; tres pasos en el fondo, dos arcos en el borde y cuatro "
        "rampas que bajan del borde a la arena."))


def crater_layout(spec: CraterSpec) -> tuple[float, tuple[float, ...]]:
    """(angulo del eje del rio, posicion de los pasos a lo largo del eje) segun la semilla."""
    rng = np.random.default_rng(spec.seed)
    axis_deg = float(rng.uniform(0.0, 180.0))
    jitter = rng.uniform(-spec.ford_jitter_m, spec.ford_jitter_m, len(spec.fords))
    return axis_deg, tuple(float(f + j) for f, j in zip(spec.fords, jitter))


def lava_river(spec: CraterSpec, axis_deg: float, reach_m: float = 110.0) -> tuple[np.ndarray, np.ndarray]:
    """(arco a lo largo del eje, puntos) del rio: serpentea en el fondo del crater y sale recto por el borde."""
    s = np.arange(-reach_m, reach_m + 0.25, 0.5)
    calm = 1.0 - smoothstep(spec.floor_r - 8.0, spec.floor_r + 6.0, np.abs(s))
    off = spec.meander_m * np.sin(2.0 * math.pi * s / spec.meander_len_m) * calm
    u, v = np.array(_pt(1.0, axis_deg)), np.array(_pt(1.0, axis_deg + 90.0))
    return s, s[:, None] * u + off[:, None] * v


def _river_spans(spec: CraterSpec, s, pts, stations, height_at, rise_m: float) -> list[NaturalBridge]:
    """Puentes perpendiculares al rio en las posiciones `stations` del eje."""
    reach = spec.river_w_m / 2.0 + 3.0
    out = []
    for station in stations:
        k = int(np.argmin(np.abs(s - station)))
        tangent = pts[k + 1] - pts[k - 1]
        tangent = tangent / np.hypot(*tangent)
        normal = np.array([-tangent[1], tangent[0]])
        out.append(_span(pts[k] - normal * reach, pts[k] + normal * reach, height_at, 4.0, rise_m))
    return out


def crater_radii(spec: CraterSpec) -> tuple[float, float]:
    """(pie de la ladera exterior, costa)."""
    outer = spec.rim_out + (spec.rim_h - spec.beach_h) / math.tan(math.radians(spec.outer_deg))
    return outer, outer + spec.beach_m


def build_crater(seed: int | None = None, spec: CraterSpec | None = None) -> tuple[ShapeMap, Extras]:
    spec = _seeded(spec or crater_n03(), seed)
    canvas = Canvas(spec.grid)
    e, n = canvas.design_grid()
    r = np.hypot(e, n)
    axis_deg, fords = crater_layout(spec)
    outer, coast = crater_radii(spec)
    level = np.interp(r, [spec.floor_r, spec.rim_in, spec.rim_out, outer], [spec.floor_h, spec.rim_h, spec.rim_h, spec.beach_h])
    height = _lift(e, n, coast - r + _wobble(spec.seed, e, n, 2.0), level, 1.5, 2.0)
    r1 = spec.rim_in + 4.0
    r0 = r1 - (spec.rim_h - spec.floor_h) / math.tan(math.radians(spec.ramp_slope_deg))
    slopes, corridors = [spec.outer_deg], []
    for k in range(4):
        height, slope, corridor = _radial_ramp(height, e, n, axis_deg + 45.0 + 90.0 * k, r0, r1, spec.floor_h,
                                               spec.rim_h, spec.ramp_w_m)
        slopes.append(slope)
        corridors.append(corridor)
    s, pts = lava_river(spec, axis_deg)
    d, _ = along_polyline(e, n, pts)
    half = spec.river_w_m / 2.0
    carve = 1.0 - smoothstep(half - 0.5, half + 1.5, d)
    height = np.minimum(height, height + (SEABED_M - height) * carve)
    probe = _probe(canvas, height)
    rim_r = 0.5 * (spec.rim_in + spec.rim_out)
    bridges = _river_spans(spec, s, pts, fords, probe, 0.4) + _river_spans(spec, s, pts, (-rim_r, rim_r), probe, 0.5)
    corridors += [b.inner_points(1.5) for b in bridges]
    slopes += [b.slope_deg() for b in bridges]
    side = axis_deg + 90.0
    required = {"arena_a": _pt(14.0, side), "arena_b": _pt(14.0, side + 180.0), "borde_a": _pt(rim_r, side),
                "borde_b": _pt(rim_r, side + 180.0), "playa": _pt(outer + 2.5, side)}
    lava = [tuple(p) for p in pts[::40] if np.hypot(*p) < outer]
    markers = {"lava": lava, "catapulta": [required["arena_a"], required["arena_b"]],
               "trampolin": [_pt(rim_r, side + d) for d in (-35.0, 35.0, 145.0, 215.0)]}
    model = KitModel(canvas, height, tuple(bridges))
    shape = _shape(spec, canvas, model, required["arena_a"], None, required, corridors, generator="volcan_arena",
                   axis_deg=axis_deg, ford_stations=list(fords))
    return shape, Extras(nests=TCT_NESTS, markers=markers, static=_static(height, slopes), notes="río de lava")


# ── N04 Atolon ───────────────────────────────────────────────────────────────────
@dataclass(frozen=True)
class AtollSpec(ArenaSpec):
    """Anillo de arena (radio medio ring_r, ancho ring_w, cota ring_h) cortado por `passes` pasos de agua de
    pass_w (los de bridged_passes, con puente); en la laguna, un islote central con cupula y `passes` islotes a
    islet_orbit del centro, entre paso y paso: todos unidos al central y los de ring_links tambien al anillo.
    Seis motus (cupulas bajas) sobre el anillo."""
    ring_r: float = 60.0
    ring_w: float = 20.0
    ring_h: float = 2.5
    passes: int = 3
    pass_w_m: float = 12.0
    pass_jitter_deg: float = 20.0
    bridged_passes: tuple[int, ...] = (1, 2)
    centre_r: float = 12.0
    centre_rise: float = 2.0
    islet_orbit: float = 30.0
    islet_r: float = 8.5
    islet_h: float = 3.0
    ring_links: tuple[int, ...] = (0, 1)
    motus: int = 6
    motu_r: float = 9.0
    motu_rise: float = 1.8


def atoll_n04(seed: int = 4004) -> AtollSpec:
    return AtollSpec("N04_atolon", seed, (
        "Atolón (Todos contra Todos): anillo de arena de 140 m a +2,5 m con tres pasos de agua (dos con puente), "
        "laguna interior con un islote central de +5 m y tres islotes unidos por puentes; seis motus dan relieve "
        "al anillo."))


def atoll_layout(spec: AtollSpec) -> tuple[tuple[float, ...], tuple[float, ...]]:
    """(angulos de los pasos, angulos de los islotes) segun la semilla; cada islote queda entre dos pasos."""
    rng = np.random.default_rng(spec.seed)
    first = float(rng.uniform(0.0, 360.0))
    sector = 360.0 / spec.passes
    jitter = rng.uniform(-spec.pass_jitter_deg, spec.pass_jitter_deg, spec.passes)
    passes = tuple(first + sector * k + (float(jitter[k]) if k else 0.0) for k in range(spec.passes))
    nudge = rng.uniform(-8.0, 8.0, spec.passes)
    return passes, tuple(first + sector * (k + 0.5) + float(nudge[k]) for k in range(spec.passes))


def _atoll_height(spec: AtollSpec, e, n, passes, islets) -> np.ndarray:
    r = np.hypot(e, n)
    theta = np.degrees(np.arctan2(n, e))
    rw = r + _wobble(spec.seed, e, n, 3.0, 40.0)
    ring = np.minimum(rw - (spec.ring_r - spec.ring_w / 2.0), spec.ring_r + spec.ring_w / 2.0 - rw)
    for deg in passes:
        lateral = np.abs(r * np.sin(np.radians(theta - deg)))
        facing = np.cos(np.radians(theta - deg)) > 0.0
        ring = np.where(facing, np.minimum(ring, lateral - spec.pass_w_m / 2.0), ring)
    land = np.maximum(ring, sd_circle(e, n, 0.0, 0.0, spec.centre_r))
    for deg in islets:
        land = np.maximum(land, sd_circle(e, n, *_pt(spec.islet_orbit, deg), spec.islet_r))
    lagoon = r < spec.islet_orbit + spec.islet_r + 4.0
    height = _lift(e, n, land, np.where(lagoon, spec.islet_h, spec.ring_h), 2.0, 2.5)
    height = height + _dome(e, n, (0.0, 0.0), spec.centre_r - 2.0, spec.centre_rise)
    for k in range(spec.motus):
        c = _pt(spec.ring_r, passes[0] + (k + 0.5) * 360.0 / spec.motus)
        height = height + _dome(e, n, c, spec.motu_r, spec.motu_rise) * smoothstep(0.0, 3.0, ring)
    return height


def _atoll_bridges(spec: AtollSpec, passes, islets, height_at) -> list[NaturalBridge]:
    inner = spec.ring_r - spec.ring_w / 2.0
    out = [_span(_pt(spec.centre_r - 3.0, deg), _pt(spec.islet_orbit - spec.islet_r + 3.0, deg), height_at)
           for deg in islets]
    out += [_span(_pt(spec.islet_orbit + spec.islet_r - 3.0, islets[k]), _pt(inner + 6.0, islets[k]), height_at)
            for k in spec.ring_links]
    half_gap = math.degrees((spec.pass_w_m / 2.0 + 4.0) / spec.ring_r)
    out += [_span(_pt(spec.ring_r, passes[k] - half_gap), _pt(spec.ring_r, passes[k] + half_gap), height_at, 4.0, 0.4)
            for k in spec.bridged_passes]
    return out


def build_atoll(seed: int | None = None, spec: AtollSpec | None = None) -> tuple[ShapeMap, Extras]:
    spec = _seeded(spec or atoll_n04(), seed)
    canvas = Canvas(spec.grid)
    e, n = canvas.design_grid()
    passes, islets = atoll_layout(spec)
    height = _atoll_height(spec, e, n, passes, islets)
    bridges = _atoll_bridges(spec, passes, islets, _probe(canvas, height))
    slopes = [b.slope_deg() for b in bridges] + [dome_slope_deg(spec.motu_r, spec.motu_rise),
                                                 dome_slope_deg(spec.centre_r - 2.0, spec.centre_rise)]
    required = {"centro": (0.0, 0.0), **{f"islote_{k + 1}": _pt(spec.islet_orbit, deg) for k, deg in enumerate(islets)},
                **{f"anillo_{k + 1}": _pt(spec.ring_r + 3.0, deg) for k, deg in enumerate(islets)}}
    open_passes = [_pt(spec.ring_r, deg) for k, deg in enumerate(passes) if k not in spec.bridged_passes]
    markers = {"paso_abierto": open_passes, "cofre": [(0.0, 0.0)],
               "trampolin": [_pt(spec.islet_orbit, deg) for deg in islets]}
    model = KitModel(canvas, height, tuple(bridges))
    shape = _shape(spec, canvas, model, required["anillo_1"], (0.0, 0.0), required,
                   [b.inner_points(1.5) for b in bridges], generator="atolon", passes_deg=list(passes),
                   islets_deg=list(islets))
    return shape, Extras(nests=TCT_NESTS, markers=markers, static=_static(height, slopes))


# ── N17 Fortaleza en estrella ────────────────────────────────────────────────────
@dataclass(frozen=True)
class StarFortSpec(ArenaSpec):
    """Estrella de `tips` baluartes (puntas a r_tip, entrantes a r_in) girada con la semilla: plaza de armas a
    parade_h con un cerro central, adarve de wall_m a wall_h en todo el contorno (la punta de cada baluarte es
    una plataforma) con una rampa por baluarte; foso de moat_m, camino cubierto de covered_m a la cota de la
    plaza y glacis que baja a la playa. En cada entrante, una puerta (hueco en el adarve) con su puente."""
    tips: int = 5
    r_tip: float = 54.0
    r_in: float = 29.0
    parade_h: float = 4.0
    wall_h: float = 7.0
    wall_m: float = 7.0
    moat_m: float = 9.0
    covered_m: float = 6.0
    glacis_m: float = 12.0
    beach_h: float = 1.5
    beach_m: float = 5.0
    gate_w_m: float = 8.0
    ramp_w_m: float = 5.0
    ramp_len_m: float = 10.5
    keep_r: float = 15.0
    keep_rise: float = 3.0


def star_fort_n17(seed: int = 4017) -> StarFortSpec:
    return StarFortSpec("N17_fortaleza_estrella", seed, (
        "Fortaleza en estrella (Todos contra Todos): cinco baluartes con adarve a +7 m alrededor de una plaza de "
        "armas a +4 m con un cerro central; foso de 9 m con cinco puentes en las puertas de los entrantes, camino "
        "cubierto y glacis que baja a la playa."))


def sd_polygon(e, n, verts: np.ndarray) -> np.ndarray:
    """Distancia con signo (positiva dentro) a un poligono simple de vertices `verts`."""
    d = np.full(np.shape(e), np.inf)
    inside = np.zeros(np.shape(e), dtype=bool)
    for (ae, an), (be, bn) in zip(verts, np.roll(verts, -1, axis=0)):
        de, dn = be - ae, bn - an
        t = np.clip(((e - ae) * de + (n - an) * dn) / (de * de + dn * dn), 0.0, 1.0)
        d = np.minimum(d, np.hypot(e - ae - t * de, n - an - t * dn))
        inside ^= ((an > n) != (bn > n)) & (e < ae + (n - an) * de / (dn if abs(dn) > 1e-12 else 1e-12))
    return np.where(inside, d, -d)


def star_layout(spec: StarFortSpec) -> tuple[np.ndarray, tuple[float, ...], tuple[float, ...]]:
    """(vertices de la estrella, angulos de las puntas, angulos de los entrantes) segun la semilla."""
    step = 360.0 / spec.tips
    rot = float(np.random.default_rng(spec.seed).uniform(0.0, step))
    tips = tuple(rot + step * k for k in range(spec.tips))
    gates = tuple(deg + step / 2.0 for deg in tips)
    verts = [p for tip, gate in zip(tips, gates) for p in (_pt(spec.r_tip, tip), _pt(spec.r_in, gate))]
    return np.array(verts), tips, gates


def _along_ray(verts: np.ndarray, deg: float, reach_m: float) -> tuple[np.ndarray, np.ndarray]:
    """(radios, distancia con signo a la estrella) a lo largo del rayo de angulo deg desde el centro."""
    rr = np.arange(0.0, reach_m, 0.25)
    return rr, sd_polygon(rr * math.cos(math.radians(deg)), rr * math.sin(math.radians(deg)), verts)


def _star_height(spec: StarFortSpec, e, n, verts, gates) -> np.ndarray:
    s = sd_polygon(e, n, verts)
    x = -s - spec.moat_m                                        # metros por fuera del foso
    wall = s < spec.wall_m
    for deg in gates:
        c, sn = math.cos(math.radians(deg)), math.sin(math.radians(deg))
        wall &= ~((e * c + n * sn > 0.0) & (np.abs(-e * sn + n * c) < spec.gate_w_m / 2.0))
    glacis = np.interp(x, [spec.covered_m, spec.covered_m + spec.glacis_m], [spec.parade_h, spec.beach_h])
    level = np.where(s > 0.0, np.where(wall, spec.wall_h, spec.parade_h), glacis)
    coast = spec.covered_m + spec.glacis_m + spec.beach_m - x + _wobble(spec.seed, e, n, 1.0)
    height = _lift(e, n, np.maximum(s, np.minimum(x, coast)), level)
    return height + _dome(e, n, (0.0, 0.0), spec.keep_r, spec.keep_rise)


def build_star_fort(seed: int | None = None, spec: StarFortSpec | None = None) -> tuple[ShapeMap, Extras]:
    spec = _seeded(spec or star_fort_n17(), seed)
    canvas = Canvas(spec.grid)
    e, n = canvas.design_grid()
    verts, tips, gates = star_layout(spec)
    height = _star_height(spec, e, n, verts, gates)
    rr, sd = _along_ray(verts, tips[0], spec.r_tip)
    wall_r = float(rr[sd >= spec.wall_m].max())                 # donde empieza la plataforma del baluarte
    slopes, corridors = [dome_slope_deg(spec.keep_r, spec.keep_rise)], []
    for deg in tips:
        height, slope, corridor = _radial_ramp(height, e, n, deg, wall_r + 0.5 - spec.ramp_len_m, wall_r + 0.5,
                                               spec.parade_h, spec.wall_h, spec.ramp_w_m)
        slopes.append(slope)
        corridors.append(corridor)
    rr, sd = _along_ray(verts, gates[0], spec.r_in + 40.0)
    landing = float(rr[(rr > spec.r_in) & (-sd - spec.moat_m >= 3.0)].min())
    probe = _probe(canvas, height)
    bridges = [_span(_pt(spec.r_in - 3.0, deg), _pt(landing, deg), probe) for deg in gates]
    corridors += [b.inner_points(1.5) for b in bridges]
    slopes += [b.slope_deg() for b in bridges]
    beach_r = spec.r_tip + spec.moat_m + spec.covered_m + spec.glacis_m + spec.beach_m / 2.0
    required = {"plaza": (0.0, 0.0), "camino_cubierto": _pt(landing + 1.5, gates[0]), "playa": _pt(beach_r, tips[0]),
                **{f"baluarte_{k + 1}": _pt(wall_r + 5.0, deg) for k, deg in enumerate(tips)}}
    markers = {"puerta": [_pt(spec.r_in - 3.0, deg) for deg in gates], "cofre": [(0.0, 0.0)],
               "catapulta": [_pt(wall_r + 5.0, deg) for deg in tips[::2]],
               "trampolin": [_pt(landing + 1.5, deg) for deg in gates[1::2]]}
    model = KitModel(canvas, height, tuple(bridges))
    shape = _shape(spec, canvas, model, required["camino_cubierto"], (0.0, 0.0), required, corridors,
                   generator="fortaleza_estrella", tips_deg=list(tips), gates_deg=list(gates))
    return shape, Extras(nests=TCT_NESTS, markers=markers, static=_static(height, slopes))


# ── N18 Yin-yang ─────────────────────────────────────────────────────────────────
@dataclass(frozen=True)
class YinYangSpec(ArenaSpec):
    """Disco de `radius` partido por un canal en S de channel_w en dos lobulos: el yang a yang_h, con una colina
    (hill_r, hill_rise) como ojo, y el yin a yin_h, con un estanque de pond_r como ojo. Tres puentes cruzan el
    canal: uno en el centro y uno en cada arco (+- arc_jitter_deg con la semilla, que tambien gira el disco)."""
    radius: float = 70.0
    channel_w_m: float = 8.0
    yang_h: float = 5.0
    yin_h: float = 2.5
    hill_r: float = 22.0
    hill_rise: float = 4.4
    pond_r: float = 9.0
    arc_jitter_deg: float = 25.0


def yin_yang_n18(seed: int = 4018) -> YinYangSpec:
    return YinYangSpec("N18_yin_yang", seed, (
        "Yin-yang (Todos contra Todos): disco de 140 m partido por un canal en S de 8 m en dos lóbulos, el yang a "
        "+5 m con una colina de +9,4 m como ojo y el yin a +2,5 m con un estanque como ojo; tres puentes en rampa "
        "cruzan el canal."))


def yin_yang_layout(spec: YinYangSpec) -> tuple[float, float, float]:
    """(giro del disco, angulo del puente del arco yang, angulo del puente del arco yin) segun la semilla."""
    rng = np.random.default_rng(spec.seed)
    rot = float(rng.uniform(0.0, 360.0))
    jitter = rng.uniform(-spec.arc_jitter_deg, spec.arc_jitter_deg, 2)
    return rot, float(jitter[0]), 180.0 + float(jitter[1])


def s_curve(radius: float) -> np.ndarray:
    """La S del yin-yang en el marco sin girar: media circunferencia derecha del circulo de arriba y media
    izquierda del de abajo, de (0, radius) a (0, -radius) pasando por el centro."""
    half = radius / 2.0
    a, b = np.radians(np.linspace(90.0, -90.0, 181)), np.radians(np.linspace(90.0, 270.0, 181))
    upper = np.column_stack([half * np.cos(a), half + half * np.sin(a)])
    lower = np.column_stack([half * np.cos(b), -half + half * np.sin(b)])
    return np.vstack([upper, lower[1:]])


def _yin_yang_height(spec: YinYangSpec, e, n, rot: float) -> np.ndarray:
    u, v = rotate(e, n, 0.0, 0.0, rot)
    half = spec.radius / 2.0
    d, _ = along_polyline(u, v, s_curve(spec.radius))
    in_up, r_dn = np.hypot(u, v - half) < half, np.hypot(u, v + half)
    yang = in_up | ((u < 0.0) & (r_dn >= half))
    coast = spec.radius - np.hypot(u, v) + _wobble(spec.seed, e, n, 1.5)
    land = np.minimum(np.minimum(coast, d - spec.channel_w_m / 2.0), r_dn - spec.pond_r)
    height = _lift(e, n, land, np.where(yang, spec.yang_h, spec.yin_h), 1.5, 1.5)
    return height + _dome(u, v, (0.0, half), spec.hill_r, spec.hill_rise)


def build_yin_yang(seed: int | None = None, spec: YinYangSpec | None = None) -> tuple[ShapeMap, Extras]:
    spec = _seeded(spec or yin_yang_n18(), seed)
    canvas = Canvas(spec.grid)
    e, n = canvas.design_grid()
    rot, yang_deg, yin_deg = yin_yang_layout(spec)
    height = _yin_yang_height(spec, e, n, rot)
    half, reach = spec.radius / 2.0, spec.channel_w_m / 2.0 + 3.0
    cos, sin = math.cos(math.radians(rot)), math.sin(math.radians(rot))

    def world(p: Point) -> Point:                               # del marco sin girar al de diseño
        return p[0] * cos - p[1] * sin, p[0] * sin + p[1] * cos

    up, down = (0.0, half), (0.0, -half)
    ends = [((0.0, reach), (0.0, -reach)), (_pt(half - reach, yang_deg, up), _pt(half + reach, yang_deg, up)),
            (_pt(half + reach, yin_deg, down), _pt(half - reach, yin_deg, down))]
    probe = _probe(canvas, height)
    bridges = [_span(world(a), world(b), probe, 4.0, 0.0) for a, b in ends]
    slopes = [b.slope_deg() for b in bridges] + [dome_slope_deg(spec.hill_r, spec.hill_rise)]
    required = {"colina": world(up), "lobulo_yang": world((-half, 0.0)), "lobulo_yin": world((half, 0.0)),
                "orilla_estanque": world((spec.pond_r + 6.0, -half))}
    markers = {"estanque": [world(down)], "cofre": [world(up)], "catapulta": [world((-half, 0.0)), world((half, 0.0))],
               "trampolin": [world((half + 12.0, -half)), world((-half - 12.0, half)), world((0.0, half + 25.0))]}
    model = KitModel(canvas, height, tuple(bridges))
    shape = _shape(spec, canvas, model, required["lobulo_yin"], required["colina"], required,
                   [b.inner_points(1.5) for b in bridges], generator="yin_yang", rot_deg=rot,
                   bridge_arcs_deg=[yang_deg, yin_deg])
    return shape, Extras(nests=TCT_NESTS, markers=markers, static=_static(height, slopes))


MAPS = {
    "N01": ("1", lambda seed: build_colosseum(seed)),
    "N02": ("1", lambda seed: build_theatre(seed)),
    "N03": ("1", lambda seed: build_crater(seed)),
    "N04": ("1", lambda seed: build_atoll(seed)),
    "N17": ("1", lambda seed: build_star_fort(seed)),
    "N18": ("1", lambda seed: build_yin_yang(seed)),
}
