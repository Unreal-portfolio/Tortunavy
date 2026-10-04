"""Temas de los circuitos de Rally por vueltas (#692): lo que cambia el entorno de un circuito generado sin tocar su
trazado ni sus elementos (eso lo deciden la semilla y el perfil, terrain_geo/rally_circuit_plan.py).

Un tema fija el relieve natural alrededor de la calzada (RallyCircuitModel._natural), la paleta de los colores de
vértice (siempre arena: terrain_vol/mesh.py, «todo arena» del 2026-09-25), el color de la calzada y el del barro. El
tema «base» reproduce R01 y R02 tal como se generaron en #622 y #682.

Elementos de firme que activa el tema (#696): `warning_bumps` pone baches de aviso en la frenada de cada horquilla
(rally_circuit.place_warnings; el trazado y el resto de elementos no cambian). La tabla de lavar y los whoops los pone
el perfil «tierra» de todos los circuitos de R02 a R06.

    THEMES["cantera"]                 # parámetros del tema
    CIRCUITS["R04_circuito_cantera"]  # semilla, perfil y tema de cada circuito del catálogo del Rally
"""

from __future__ import annotations

from dataclasses import dataclass

COLOR_ZONES = ("cliffs", "beach", "marsh")      # arena seca, arena de playa y arena húmeda (ZONE_PALETTE)


@dataclass(frozen=True)
class Theme:
    key: str
    hills_m: float = 10.0               # relieve que crece lejos del eje (amplitud de las lomas, m)
    hills_scale_m: float = 110.0        # tamaño de las lomas
    near_m: float = 2.0                 # relieve junto a la calzada
    ripples_m: float = 0.4              # rizado fino
    rim_m: float = 14.0                 # cordón del borde de la rejilla (cierra la vista)
    color_zone: str = "cliffs"          # paleta del terreno: cliffs (arena), beach (playa) o marsh (húmeda)
    shore_m: float = 3.5                # por debajo de WATER_M + shore_m, playa (como HeightfieldModel)
    wall_strata: float = 0.0            # vetas horizontales en los taludes (estratos de cantera)
    trail_color: tuple[float, float, float] = (0.42, 0.30, 0.17)
    trail_strength: float = 0.55
    mud_strength: float | None = None   # None: el de rally_circuit_dirt
    warning_bumps: bool = False         # baches de aviso en la frenada de cada horquilla (#696)


THEMES: dict[str, Theme] = {
    "base": Theme("base"),
    # Dunas grandes y suaves de arena de playa, con un cordón alto que tapa el horizonte.
    "dunas_costeras": Theme("dunas_costeras", hills_m=14.0, hills_scale_m=150.0, near_m=1.5, ripples_m=0.7,
                            rim_m=18.0, color_zone="beach", shore_m=6.0, trail_color=(0.50, 0.38, 0.22),
                            trail_strength=0.45),
    # Cantera: lomas cortas y altas con estratos en los taludes y la pista más oscura.
    "cantera": Theme("cantera", hills_m=18.0, hills_scale_m=70.0, near_m=3.0, ripples_m=0.25, rim_m=24.0,
                     wall_strata=0.8, trail_color=(0.34, 0.24, 0.13), trail_strength=0.65),
    # Marismas: llano, bajo y húmedo, con más barro en el badén.
    "marismas": Theme("marismas", hills_m=3.5, hills_scale_m=170.0, near_m=0.8, ripples_m=0.3, rim_m=8.0,
                      color_zone="marsh", shore_m=5.0, trail_color=(0.30, 0.22, 0.12), trail_strength=0.6,
                      mud_strength=0.95, warning_bumps=True),
    # Lomas secas: colinas redondas medianas, pista de tierra clara.
    "lomas_secas": Theme("lomas_secas", hills_m=11.0, hills_scale_m=95.0, near_m=2.5, ripples_m=0.5, rim_m=16.0,
                         trail_color=(0.55, 0.40, 0.22), trail_strength=0.5, warning_bumps=True),
}


@dataclass(frozen=True)
class Circuit:
    name: str
    seed: int
    profile: str
    theme: str
    description: str


CIRCUITS: dict[str, Circuit] = {c.name: c for c in (
    Circuit("R03_circuito_dunas_costeras", 6928, "tierra", "dunas_costeras",
            "Circuito de Rally por vueltas de tierra entre dunas costeras (#692): saltos con forma y mesa, whoops y "
            "tabla de lavar, badén con barro, banquetas en las horquillas y dunas altas de arena de playa."),
    Circuit("R04_circuito_cantera", 6929, "tierra", "cantera",
            "Circuito de Rally por vueltas de tierra en una cantera (#692): saltos con forma y mesa, whoops y tabla "
            "de lavar, badén con barro, banquetas en las horquillas y taludes altos con estratos."),
    Circuit("R05_circuito_marismas", 6927, "tierra", "marismas",
            "Circuito de Rally por vueltas de tierra por las marismas (#692): saltos con forma y mesa, whoops y "
            "tabla de lavar, badén con más barro, banquetas en las horquillas y un llano de arena húmeda."),
    Circuit("R06_circuito_lomas", 6933, "tierra", "lomas_secas",
            "Circuito de Rally por vueltas de tierra entre lomas secas (#692): saltos con forma y mesa, whoops y "
            "tabla de lavar, badén con barro, banquetas en las horquillas y colinas redondas."),
)}


def theme(key: str) -> Theme:
    if key not in THEMES:
        raise ValueError(f"tema desconocido: {key} (hay {', '.join(THEMES)})")
    t = THEMES[key]
    if t.color_zone not in COLOR_ZONES:
        raise ValueError(f"tema {key}: zona de color {t.color_zone} no es de {COLOR_ZONES}")
    return t
