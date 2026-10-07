"""Preset del mapa D01_boceto (#875): el boceto del equipo de diseno pasado a terreno.

Lectura del boceto (Scripts/terrain_path/sketches/D01_boceto.jpeg, 1600 x 1600 px, Norte arriba):
  - marron: pared o limite; azul: mar al norte; gris: bocas de tunel; leyenda abajo a la derecha;
  - tonos de arena: zonas jugables, mas oscuro = mas alto. Salida en la zona mas oscura (abajo) y
    meta en el mar, pasando por la playa clara de arriba;
  - cueva de arriba (una sola, amplia): baja desde la boca de la meseta media, se abre bajo las zonas
    siguiendo la mancha discontinua y sube hasta la boca de la hondonada;
  - tunel de abajo (un solo paso): de la hondonada a la zona oscura de abajo, bajo un cerro.

Decisiones del director (#875):
  - 2026-10-06: pasillos en cuesta entre zonas (sin bajadas de golpe), suelo casi llano dentro de cada
    zona, tuneles sin bifurcaciones, cueva de 10 m de alto como maximo, paredes mas altas que en C01;
  - 2026-10-07, tras ver la lamina: recorrido de unos 600 m (550-650) en vez de 1000, todo el mapa a
    escala; la cueva va por debajo de las zonas y se ensancha (ya no es un pasillo en superficie); mas
    desnivel entre zonas con cuestas de 25 grados como mucho y la hondonada mucho mas hundida; paredes
    con contorno y cara irregulares sin perder altura; el exterior del anillo de muros no se toca.

Escala: M_PER_PX sale de medir el recorrido por el centro sobre el boceto (gen_terrain_sketch.py --medir).
"""

from __future__ import annotations

import math

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

# Cota del suelo sobre el agua (m). Orden de la leyenda: cuanto mas oscuro, mas alto; 5 m por tono desde la
# playa. La hondonada (la mas clara) queda 12 m por debajo de la meseta media que la rodea (antes, 8 m) y
# por encima del agua: la cueva baja hasta 0,8 m y vuelve a subir hasta ella.
ZONE_HEIGHT_M = {HOLLOW: 3.0, BEACH: 5.0, LIGHT: 10.0, MEDIUM: 15.0, DARK: 20.0, DARKEST: 25.0}

# Cuestas entre mesetas (pasillos y laderas): 20 grados, por debajo de los 25 que pide el director.
RAMP_DEG = 20.0
CAVE_LOW_M = 0.8

DECISIONS = (
    "2026-10-06: pasillos en cuesta entre zonas, suelo casi llano dentro de cada zona, tuneles sin bifurcaciones, "
    "cueva de 10 m de alto como maximo, paredes mas altas que en C01.",
    "2026-10-07: recorrido de unos 600 m (550-650); cueva amplia por debajo de las zonas que baja desde la boca y "
    "sube a la otra; mas desnivel entre zonas con cuestas de 25 grados como mucho; hondonada mucho mas hundida; "
    "paredes con contorno y cara irregulares; el exterior del anillo de muros no se toca.",
)

ZONE_NAMES = {HOLLOW: "hondonada", BEACH: "playa", LIGHT: "clara", MEDIUM: "media", DARK: "oscura",
              DARKEST: "muy oscura (salida)"}

M_PER_PX = 0.474

# Cueva bajo las zonas, de norte a sur: arranca en la meseta media de arriba, entra bajo el muro por la boca
# gris (740, 808), baja a 20 grados hasta CAVE_LOW_M, se abre bajo las dos mesetas medias siguiendo la mancha
# discontinua (semiancho medido entre sus dos lineas) y sube hasta la boca de la hondonada (655, 1048).
# Por vertice: (cota del suelo sobre el agua o None = la de la zona, semiancho m, alto m).
CAVE = Tunnel(
    name="cueva",
    axis_px=((752, 785), (740, 808), (686, 822), (682, 850), (667, 880), (663, 910), (661, 935), (654, 960),
             (637, 990), (645, 1015), (652, 1042), (657, 1062), (662, 1088)),
    covered=(0.0, 1.0),
    half_width_m=5.0,
    height_m=8.4,
    rock_m=3.0,
    underground=True,
    profile=((None, 4.5, 5.0), (11.0, 4.5, 5.5), (2.0, 8.0, 8.0), (1.0, 30.0, 8.4), (CAVE_LOW_M, 26.0, 8.4),
             (CAVE_LOW_M, 22.0, 8.4), (CAVE_LOW_M, 16.0, 8.4), (CAVE_LOW_M, 9.0, 8.4), (0.9, 6.0, 8.0),
             (1.4, 5.0, 7.0), (2.2, 5.0, 6.0), (None, 5.0, 5.5), (None, 5.0, 5.5)),
)

PASSAGE = Tunnel(
    name="tunel",
    # De la hondonada (~700, 1205) a la zona oscura de abajo (~710, 1262); el tramo cubierto sigue bajo un
    # cerro dentro de la zona oscura para que la pendiente sea suave (17 m de desnivel en unos 70 m).
    axis_px=((699, 1185), (701, 1228), (711, 1262), (721, 1300), (730, 1345), (738, 1390), (742, 1420)),
    covered=(0.18, 0.80),
    half_width_m=4.5,
    height_m=5.5,
    rock_m=5.0,
)

D01_SPEC = SketchSpec(
    name="D01_boceto",
    description=("Boceto D01 del equipo de diseno: mesetas escalonadas de la salida (zona mas alta) al mar, "
                 "hondonada hundida, cueva amplia bajo las zonas y tunel; unos 600 m de recorrido."),
    image="terrain_path/sketches/D01_boceto.jpeg",
    m_per_px=M_PER_PX,
    ref_px=(745.0, 1600.0),
    ref_world=(-30.0, 150.0),
    cols=4,
    rows=7,
    palette=PALETTE,
    zone_height_m=ZONE_HEIGHT_M,
    sea_floor_m=-3.0,
    legend_box_px=(1100, 1295, 1600, 1600),
    bottom_cap_px=(712.0, 1530.0, 150.0, 80.0),
    sea_fan_px=((775, 455), (935, 455), (975, 392), (745, 382)),
    beach_class=BEACH,
    tunnels=(CAVE, PASSAGE),
    start_px=(712.0, 1545.0),
    corridor_grade=math.tan(math.radians(RAMP_DEG)),
    slope_deg=RAMP_DEG,
    sea_grade=0.2,
    slope_pairs=((HOLLOW, MEDIUM),),
    edge_warp_m=2.5,
    wall_noise=1.0,
    foot_angle_deg=70.0,
    decimate_m=0.05,
    far_decimate_m=0.45,
    outer_decimate_m=0.25,
    extra={"hollow_class": HOLLOW, "medium_class": MEDIUM, "names": ZONE_NAMES, "decisions": DECISIONS},
)
