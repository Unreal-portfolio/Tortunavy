"""Puzzles (Docs/2026-10-06-Plan-Maestro-Modo-Unico.md): la palanca y la placa de presión.

Escala: tortuga de ~115 cm de pie. Origen en la base (suelo, Z = 0) salvo las piezas que giran o se deslizan, que
llevan el origen en su bisagra; la pieza fija tiene un socket con el mismo nombre (Pivot, Leaf, Platform...) donde se
engancha la móvil.
"""
import math

from ia_mesh import Builder, mirror_y, move, rot_x, rot_z

CATEGORY = 'puzzles'
STONE = 0x9C9186


def scallop(b, size, zone, depth=1.6, lip_zone=None):
    """Concha de vieira plana en el plano XZ (charnela en el origen, abanico hacia +Z), grosor en Y."""
    pts = [(-0.18 * size, -0.05 * size), (0.18 * size, -0.05 * size)]
    n = 11
    for i in range(n + 1):  # borde festoneado de 15° a 165°
        a = math.radians(15 + 150 * i / n)
        r = size * (1.0 if i % 2 == 0 else 0.9)
        pts.append((math.cos(a) * r, math.sin(a) * r))
    b.prism(pts, depth, zone)
    with b.frame(move(0, -depth * 0.6, 0)):
        b.prism([(-0.28 * size, -0.12 * size), (0.28 * size, -0.12 * size), (0.2 * size, 0.1 * size),
                 (-0.2 * size, 0.1 * size)], depth, lip_zone or zone)


def star(b, r_out, r_in, depth, zone, points=5):
    poly = []
    for i in range(points * 2):
        a = math.pi / 2 + math.pi * i / points
        r = r_out if i % 2 == 0 else r_in
        poly.append((math.cos(a) * r, math.sin(a) * r))
    b.prism(poly, depth, zone)


# ── Palanca con base ────────────────────────────────────────────────────────
PIVOT_Z = 46.0


def palanca_base():
    b = Builder()
    b.hull(mirror_y([(-36, 26, 0), (36, 26, 0), (-36, 26, 24), (36, 26, 24), (-31, 21, 31), (31, 21, 31)]), 'dark')
    b.box((-22, -14, 30), (22, 14, 33), 'trim', 0.9)
    for side in (1, -1):  # carrilleras que sujetan el eje
        b.hull([(x, side * y, z) for x, y, z in ((-12, 8, 30), (12, 8, 30), (-9, 8, 52), (9, 8, 52),
                                                   (-12, 12, 30), (12, 12, 30), (-9, 12, 52), (9, 12, 52))], 'paint')
        b.cyl((0, side * 12, PIVOT_Z), (0, side * 14.5, PIVOT_Z), 3.4, 'trim', seg=8)
    b.cyl((0, -12, PIVOT_Z), (0, 12, PIVOT_Z), 2.0, 'trim', seg=6)
    # Ranura de recorrido con marcas de posición.
    for x, zone in ((-16, 'detail'), (16, 'light')):
        b.box((x - 2.5, -5, 33), (x + 2.5, 5, 34.2), zone)
    with b.frame(move(36.6, 0, 7.0) @ rot_z(90)):  # vieira en la cara delantera
        scallop(b, 11.0, 'detail', lip_zone='light')
    b.socket('Pivot', (0, 0, PIVOT_Z))
    b.col_box((-36, -26, 0), (36, 26, 31))
    return b


def palanca_brazo():
    """Brazo de la palanca: origen en el eje (gira en Y), en reposo vertical."""
    b = Builder()
    b.cyl((0, -7.5, 0), (0, 7.5, 0), 4.6, 'trim', seg=8)
    b.hull(mirror_y([(-3.2, 2.6, 0), (3.2, 2.6, 0), (-2.4, 2.2, 52), (2.4, 2.2, 52)]), 'dark')
    b.cyl((0, 0, 47), (0, 0, 55), 2.8, 'trim', seg=8)
    b.sphere((0, 0, 61), 7.5, 'paint', seg=8, rings=6)
    b.col_hull([(0, 0, 0), (0, 0, 68)] + [(x, y, z) for x in (-7.5, 7.5) for y in (-7.5, 7.5) for z in (54, 68)])
    return b


# ── Placa de presión ────────────────────────────────────────────────────────

def _placa(pad_top, emblem_zone):
    b = Builder()
    for lo, hi in (((-62, -62, 0), (62, -50, 7)), ((-62, 50, 0), (62, 62, 7)),
                   ((-62, -50, 0), (-50, 50, 7)), ((50, -50, 0), (62, 50, 7))):
        b.box(lo, hi, 'trim')
    b.box((-50, -50, 0), (50, 50, 1.5), 'trim', 0.7)
    b.hull([(x * s, y * s, z) for x, y in ((1, 1), (1, -1), (-1, 1), (-1, -1)) for s, z in
            ((49.0, 1.5), (49.0, pad_top - 2.5), (45.0, pad_top))], 'paint')
    for x, y in ((56, 56), (56, -56), (-56, 56), (-56, -56)):
        b.cyl((x, y, 7), (x, y, 8.4), 2.6, 'detail', seg=6)
    with b.frame(move(0, 0, pad_top + 0.6) @ rot_x(90)):
        star(b, 30.0, 13.0, 1.2, emblem_zone)
    b.col_box((-62, -62, 0), (62, 62, pad_top))
    return b


def placa_subida():
    return _placa(11.0, 'detail')


def placa_bajada():
    return _placa(4.0, 'light')


ASSETS = [
    dict(slug='palanca', title='Palanca con base', category=CATEGORY, budget=800,
         prompt='Stylized low-poly puzzle lever on a sandstone block base, painted side cheeks holding the axle, '
                'wooden lever arm with a round knob, scallop shell on the front, two position marks, flat-shaded',
         palette={'paint': 0x2F80ED, 'detail': 0xF28C28, 'dark': 0xC9A574, 'trim': 0x5B6168, 'light': 0x7CFFB0},
         parts=[dict(name='SM_TN_PalancaBase', build=palanca_base, budget=500, pivot='base', role='base fija',
                     required_sockets=('Pivot',)),
                dict(name='SM_TN_PalancaBrazo', build=palanca_brazo, budget=300, pivot='hinge',
                     role='brazo (gira en Y alrededor del origen)', preview_loc=(0, 0, PIVOT_Z),
                     preview_rot=(0, 28, 0))],
         notes='El brazo se engancha en el socket Pivot de la base y gira en Y (±30°). Zona Light = marca de '
               'posición activa (emisiva).'),
    dict(slug='placa_presion', title='Placa de presión (subida y bajada)', category=CATEGORY, budget=800,
         prompt='Stylized low-poly square pressure plate with a stone frame, raised painted pad and a starfish '
                'emblem; two states: up (emblem plain) and down (emblem glowing), flat-shaded',
         palette={'paint': 0xE4572E, 'detail': 0xFFD23F, 'dark': 0x6B4A33, 'trim': STONE, 'light': 0x7CFFB0},
         parts=[dict(name='SM_TN_PlacaPresion_Subida', build=placa_subida, budget=400, pivot='base',
                     role='estado subido', preview_loc=(0, -75, 0)),
                dict(name='SM_TN_PlacaPresion_Bajada', build=placa_bajada, budget=400, pivot='base',
                     role='estado bajado (emblema emisivo)', preview_loc=(0, 75, 0))],
         notes='Dos mallas completas (una por estado) según el encargo; alternativa barata: marco fijo + pad móvil.'),
]
