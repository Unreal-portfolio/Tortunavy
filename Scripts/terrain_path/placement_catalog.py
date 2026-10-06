"""Catálogo de lo que coloca terrain_path.placement (#652) y las cifras de las reglas de diseño.

Puzles: plantillas de Docs/Catalogo-Puzzles-2026-09-29.md (§3 Coop y §4 parkour); las que dependen de
piezas de C++ pendientes llevan status "pendiente" y solo se colocan con --incluir-pendientes.
Enemigos, peligros, mecánicas y botín: Docs/superpowers/specs/modos/01-Coop.md (§4 y §5), con el
nombre del enumerado ETNBeachElement o de la clase de mundo que los crea.
"""

from __future__ import annotations

from dataclasses import dataclass, field

# Categorías
PUZZLE, MECHANIC, ENEMY, OBSTACLE = "puzzle", "mechanic", "enemy", "obstacle"
LOOT, DECOR, VEGETATION = "loot", "decor", "vegetation"
GAMEPLAY = frozenset({PUZZLE, MECHANIC, ENEMY, OBSTACLE})
HOSTILE = frozenset({ENEMY, OBSTACLE})
CATEGORIES = (PUZZLE, MECHANIC, ENEMY, OBSTACLE, LOOT, DECOR, VEGETATION)

# -- Reglas (m salvo indicación) ------------------------------------------------------------------
PUZZLE_GAP_M = 90.0          # entre centros de dos puzles, por el camino
PUZZLE_CALM_M = 25.0         # sin enemigos ni obstáculos a esta distancia de la huella de un puzle
HOSTILE_GAP_M = 14.0         # entre dos enemigos u obstáculos, por el camino
EXCLUDE_M = {"start": 40.0, "end": 50.0, "junction": 12.0, "crossing": 15.0}
TRAMO_M = 50.0               # tramo de la curva de intensidad
PEAK_MIN = 4.0               # intensidad de un tramo a partir de la cual es un pico
CALM_MAX = 2.0               # intensidad máxima del tramo que sigue a un pico (y del primero)
BASE_ENEMY_PER_100M = 1.0    # densidad base (ruta sin alternativa), D-08 del spec de Coop
BASE_OBSTACLE_PER_100M = 0.7
ROUTE_FACTOR = (0.4, 2.5)    # límites del factor de densidad por longitud relativa de la ruta
DENSITY_TOL = 0.15           # diferencia relativa de longitud por debajo de la cual no se exige orden
SHORT_MIN_FREE_M = 30.0      # una ruta corta con al menos esto libre lleva algún peligro
DECOR_AXIS_MIN_M = 4.0       # ningún decorado a menos de esto del eje (D-10)
DECOR_STEP_M = 6.0
PARKOUR_DANGER = 2.0         # un parkour cuenta como dos peligros en la densidad de su ruta


@dataclass(frozen=True)
class PuzzleSpec:
    kind: str
    mode: str                  # "grupo" (bloquea el paso) o "parkour"
    length_m: float            # huella a lo largo del camino
    min_half_width_m: float
    difficulty: int
    status: str                # "mvp" o "pendiente"
    pieces: str
    params: dict = field(default_factory=dict)

    @property
    def intensity(self) -> float:
        return 2.0 + self.difficulty


PUZZLES: dict[str, PuzzleSpec] = {p.kind: p for p in (
    PuzzleSpec("throw_chain", "grupo", 24.0, 4.0, 2, "mvp", "ATN_ProcThrowWall + ATN_ProcSwitch",
               {"walls": 1, "wall_height_m": 4.8, "ramp_run_m": 7.5, "effect_s": 8}),
    PuzzleSpec("shell_gauntlet", "grupo", 34.0, 3.0, 2, "mvp", "3 ATN_BeachShellGate",
               {"gates": 3, "spacing_m": 10, "open_hold_s": 3, "final_door": False}),
    PuzzleSpec("plate_balance", "grupo", 26.0, 6.0, 1, "mvp",
               "ATN_PressurePlate x3-5 + ATN_PressurePlateGroupManager + ATN_PuzzleDoor (N4, M1)",
               {"plates": 3, "spacing_m": 6, "hold_s": 2, "ball_counts_double": True, "latch": False}),
    PuzzleSpec("wobbly_run", "parkour", 28.0, 3.5, 2, "mvp", "5-8 ATN_BeachWobblyPlatform",
               {"platforms": 6, "gap_m": 2.5, "long_gap_m": 4.5}),
    PuzzleSpec("breakable_chain", "parkour", 26.0, 3.5, 2, "mvp", "6-10 ATN_BreakablePlatform + ATN_BeachTrampoline",
               {"platforms": 8, "respawn_s": 6, "trampoline": True}),
    PuzzleSpec("catapult_gap", "parkour", 8.0, 3.0, 1, "mvp", "ATN_BeachCatapult + ATN_BeachTrampoline",
               {"long_way": True, "landing": "trampoline"}),
    PuzzleSpec("basket_hold", "grupo", 30.0, 5.0, 2, "pendiente", "N1 + N4 + N2",
               {"lever_hold_s": 8, "lever_to_door_m": 20, "basket_height_m": 3, "latch": True}),
    PuzzleSpec("geyser_aim", "grupo", 30.0, 6.0, 2, "pendiente", "ATN_ProcGeyser (M3) + N1 + N6",
               {"aim_positions": 3, "target_height_m": 8, "cycle_s": 4.2, "lever_distance_m": 10}),
    PuzzleSpec("think_room", "grupo", 30.0, 7.0, 3, "pendiente", "N3 + N4 + N5",
               {"variant": "code", "steps": 3, "plates": 4, "buttons": 2}),
)}
GROUP_KINDS = tuple(k for k, p in PUZZLES.items() if p.mode == "grupo")
PARKOUR_ON_ROUTES = ("wobbly_run", "breakable_chain")


@dataclass(frozen=True)
class HazardSpec:
    kind: str
    category: str              # ENEMY u OBSTACLE
    intensity: float
    biomes: frozenset          # índices de BIOME_NAMES
    water: bool = False        # va en el tramo de río (y solo ahí)
    min_half_width_m: float = 2.5
    max_per_map: int = 99
    weight: float = 1.0
    layout: str = "point"      # "point", "across" (cruza el camino) o "along" (recorre un tramo)
    extent_m: float = 0.0


HAZARDS: dict[str, HazardSpec] = {h.kind: h for h in (
    HazardSpec("GiantCrab", ENEMY, 2.5, frozenset({0, 2, 3}), min_half_width_m=5.0, max_per_map=3, weight=0.6),
    HazardSpec("SeaUrchin", ENEMY, 1.5, frozenset({0, 3})),
    HazardSpec("Lizard", ENEMY, 1.0, frozenset({0, 2})),
    HazardSpec("SandFleas", ENEMY, 1.5, frozenset({2, 3})),
    HazardSpec("HermitCrab", ENEMY, 1.5, frozenset({0, 2}), layout="along", extent_m=20.0, weight=0.7),
    HazardSpec("ToyTank", ENEMY, 2.0, frozenset({2, 3}), min_half_width_m=5.0, max_per_map=2,
               layout="along", extent_m=15.0, weight=0.6),
    HazardSpec("QuadLane", ENEMY, 2.0, frozenset({3}), min_half_width_m=6.0, max_per_map=2, layout="across"),
    HazardSpec("GullZone", ENEMY, 1.5, frozenset({1, 3}), water=True, extent_m=12.0),
    HazardSpec("PoolOctopus", ENEMY, 2.0, frozenset({1}), water=True, max_per_map=2),
    HazardSpec("BarbedWire", OBSTACLE, 1.0, frozenset({0, 2, 3}), layout="across"),
    HazardSpec("Mine", OBSTACLE, 1.0, frozenset({2, 3})),
    HazardSpec("Seaweed", OBSTACLE, 1.0, frozenset({1}), water=True),
    HazardSpec("ClamTrap", OBSTACLE, 1.0, frozenset({1, 3}), water=True),
)}

MECHANIC_INTENSITY = 0.5
MECHANICS = {
    "Trampoline": "ETNBeachElement::Trampoline",
    "SpadeRamp": "ETNBeachElement::SpadeRamp",
    "Catapult": "ETNBeachElement::Catapult",
    "MovingPlatform": "ETNBeachElement::MovingPlatform",
    "Boardwalk": "ETNBeachElement::Boardwalk",
    "Geyser": "ATN_ProcGeyser",
}
LOOT_CLASSES = {
    "SearchSpot": "ATN_BeachSearchSpot",
    "FishingPool": "ATN_FishingPool",
    "ScoreShell": "BP_ScorePickup",
}

DECOR_BY_BIOME = (
    ("Rock", "RockCluster", "MossyLog", "ShipSailWreck", "Driftwood", "Sandbags", "TankTrap", "AmmoCrate"),
    ("Clam", "DecorShell", "Starfish", "FishingNet", "Buoy", "OldPlanks", "Driftwood", "RubberDuck"),
    ("Coconut", "PlantedUmbrella", "BeachChair", "SandCastleSmall", "Bottle", "SodaCan", "FlipFlop", "ToyBucket"),
    ("StrandedJellyfish", "Starfish", "DecorShell", "BeachTowel", "SandCastleSmall", "BeachBall", "Lollipop",
     "WatermelonRind"),
)
VEGETATION_BY_BIOME = (("Shrub", "Grass"), ("Grass",), ("Palm", "Shrub", "Grass"), ("Palm", "Grass"))


def class_of(category: str, kind: str) -> str:
    """Clase o elemento de Unreal que crea el cargador para (categoría, kind)."""
    if category == PUZZLE:
        return PUZZLES[kind].pieces if kind in PUZZLES else kind
    if category in HOSTILE:
        return f"ETNBeachElement::{kind}"
    if category == MECHANIC:
        return MECHANICS.get(kind, kind)
    if category == LOOT:
        return LOOT_CLASSES.get(kind, kind)
    if category == DECOR:
        return f"ETNBeachElement::{kind}"
    return "ATN_BeachDecorField"


def intensity_of(category: str, kind: str) -> float:
    if category == PUZZLE:
        return PUZZLES[kind].intensity if kind in PUZZLES else 3.0
    if category in HOSTILE:
        return HAZARDS[kind].intensity if kind in HAZARDS else 1.0
    if category == MECHANIC:
        return MECHANIC_INTENSITY
    return 0.0


def danger_of(category: str, kind: str) -> float:
    """Peso de un elemento en la densidad de peligro de su ruta."""
    if category in HOSTILE:
        return 1.0
    if category == PUZZLE and kind in PARKOUR_ON_ROUTES:
        return PARKOUR_DANGER
    return 0.0
