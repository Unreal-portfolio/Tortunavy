"""Preset del mapa D01_boceto (#875): el boceto del equipo de diseno pasado a terreno.

Lectura del boceto (Scripts/terrain_path/sketches/D01_boceto.jpeg, 1600 x 1600 px, Norte arriba):
  - marron: pared o limite; azul: mar al norte; gris: bocas de tunel; leyenda abajo a la derecha;
  - tonos de arena: zonas jugables, mas oscuro = mas alto. Salida en la zona mas oscura (abajo) y
    meta en el mar, pasando por la playa clara de arriba;
  - cueva de arriba (una sola, amplia): de la meseta media a la hondonada, bajo el muro en diagonal;
  - tunel de abajo (un solo paso): de la hondonada a la zona oscura de abajo.

Escala (correccion del director, 2026-10-06): unos 1000 m de recorrido por el camino principal, de la
salida al mar. M_PER_PX sale de route_length_px sobre el boceto (gen_terrain_sketch.py --medir).
"""

from __future__ import annotations

from .sketch import SEA, WALL, SketchSpec, Tunnel

HOLLOW, BEACH, LIGHT, MEDIUM, DARK, DARKEST = 2, 3, 4, 5, 6, 7

PALETTE = {
    SEA: (0, 153, 233),
    WALL: (143, 89, 0),
    HOLLOW: (255, 229, 178),
    BEACH: (243, 204, 125),
    LIGHT: (227, 189, 116),
    MEDIUM: (198, 163, 95),
    DARK: (172, 137, 71),
    DARKEST: (147, 115, 54),
}

# Cota del suelo sobre el agua (m). Orden de la leyenda: cuanto mas oscuro, mas alto. 2 m por tono: la
# hondonada queda 8 m por debajo de la zona oscura, lo que el tunel de abajo salva al 11 %.
ZONE_HEIGHT_M = {HOLLOW: 1.0, BEACH: 3.0, LIGHT: 5.0, MEDIUM: 7.0, DARK: 9.0, DARKEST: 11.0}

ZONE_NAMES = {HOLLOW: "hondonada", BEACH: "playa", LIGHT: "clara", MEDIUM: "media", DARK: "oscura",
              DARKEST: "muy oscura (salida)"}

M_PER_PX = 0.79

CAVE = Tunnel(
    name="cueva",
    # De la meseta media (boca de arriba, ~740, 805) a la hondonada (~655, 1045), bajo el muro diagonal.
    axis_px=((752, 768), (742, 806), (706, 842), (672, 900), (653, 960), (651, 1010), (657, 1050),
             (664, 1100)),
    covered=(0.12, 0.86),
    half_width_m=12.0,
    height_m=8.2,
    rock_m=7.0,
)

PASSAGE = Tunnel(
    name="tunel",
    # De la hondonada (~700, 1205) a la zona oscura de abajo (~710, 1262); el tramo cubierto sigue bajo
    # un cerro dentro de la zona oscura para que la pendiente sea suave (8 m de desnivel).
    axis_px=((699, 1185), (701, 1228), (711, 1262), (721, 1300), (730, 1345)),
    covered=(0.22, 0.84),
    half_width_m=4.5,
    height_m=5.5,
    rock_m=5.0,
)

D01_SPEC = SketchSpec(
    name="D01_boceto",
    description=("Boceto D01 del equipo de diseno: mesetas escalonadas de la salida (zona mas alta) al mar, "
                 "hondonada, cueva amplia y tunel."),
    image="terrain_path/sketches/D01_boceto.jpeg",
    m_per_px=M_PER_PX,
    ref_px=(745.0, 1600.0),
    ref_world=(-35.0, 200.0),
    cols=5,
    rows=10,
    palette=PALETTE,
    zone_height_m=ZONE_HEIGHT_M,
    sea_floor_m=-3.0,
    legend_box_px=(1100, 1295, 1600, 1600),
    bottom_cap_px=(712.0, 1530.0, 150.0, 80.0),
    sea_fan_px=((775, 455), (935, 455), (975, 392), (745, 382)),
    beach_class=BEACH,
    tunnels=(CAVE, PASSAGE),
    start_px=(712.0, 1545.0),
    slope_pairs=((HOLLOW, MEDIUM),),
    extra={"hollow_class": HOLLOW, "names": ZONE_NAMES},
)
