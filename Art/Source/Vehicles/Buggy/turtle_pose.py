"""Tortuga sentada (conductora o artillera) a partir de la malla real de TotugaDemo_Rig.

Python puro con numpy (sin bpy): lo usan build_buggy.py (para medir la cadera y colocar asientos y sockets) y
render_sheet.py (para las siluetas de la lámina).

Datos: Scripts/tools/data/turtle_geo.json (vértices antes del skinning, en unidades de malla) y
Scripts/tools/data/turtle_ref_pose.txt (huesos en la postura en T), los mismos que usa Scripts/tools/fk_sim.py.
Ejes de la malla: mira a +Y, arriba +Z, su izquierda es +X; el personaje la escala x2,5 (BP_TortugaCharacter).

La postura es rígida por tramos (cada vértice sigue a un solo hueso, sin pesos): basta para medir y para una
silueta, no para animar. Salida en el espacio del buggy de Blender: metros, +X delante, +Y izquierda, +Z arriba,
con el hueso Hips en el origen.
"""
import json
import math
import os

import numpy as np

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
DATA_DIR = os.path.join(REPO_ROOT, 'Scripts', 'tools', 'data')
MESH_SCALE = 2.5          # escala de la malla en BP_TortugaCharacter
CM_TO_M = 0.01

# Reparto de vértices por hueso (unidades de malla, ver cortes por altura de la malla en el docstring de build_buggy).
BODY_MIN_Z = 19.5         # por debajo solo hay piernas
ARM_MIN_ABS_X = 7.5       # el torso mide x ±6,5; los brazos empiezan en el hombro (x 6,2)
ARM_MIN_Z = 28.0
CROTCH_HALF_X = 1.0      # la entrepierna (x 0, z >= 17) es del cuerpo, no de un muslo
CROTCH_MIN_Z = 17.0

PARENT = {}


def _chain(names):
    for child, parent in zip(names[1:], names[:-1]):
        PARENT[child] = parent


_chain(['Hips', 'Spine', 'Spine1', 'Spine2', 'Neck', 'Head', 'HeadTop_End'])
for _s in ('Left', 'Right'):
    _chain(['Spine2', _s + 'Shoulder', _s + 'Arm', _s + 'ForeArm', _s + 'Hand', _s + 'HandIndex1'])
    _chain(['Hips', _s + 'UpLeg', _s + 'Leg', _s + 'Foot', _s + 'ToeBase', _s + 'Toe_End'])


def _load():
    ref = {}
    with open(os.path.join(DATA_DIR, 'turtle_ref_pose.txt'), encoding='utf-8') as f:
        for line in f:
            fields = line.strip().split('|')
            if len(fields) >= 3:
                ref[fields[0]] = np.array([float(v) for v in fields[2].split()])
    with open(os.path.join(DATA_DIR, 'turtle_geo.json'), encoding='utf-8') as f:
        geo = json.load(f)
    return ref, np.array(geo['v'], dtype=float), [tuple(t) for t in geo['t']]


def _bone_of(v):
    x, _, z = v
    side = 'Left' if x > 0 else 'Right'
    if z < BODY_MIN_Z:
        if abs(x) < CROTCH_HALF_X and z >= CROTCH_MIN_Z:
            return 'Hips'
        if z >= 13.0:
            return side + 'UpLeg'
        return side + 'Leg' if z >= 3.8 else side + 'Foot'
    if abs(x) > ARM_MIN_ABS_X and z > ARM_MIN_Z:
        if abs(x) < 15.4:
            return side + 'Arm'
        return side + 'ForeArm' if abs(x) < 27.6 else side + 'Hand'
    return 'Hips'


def _descendants(bone):
    out = {bone}
    changed = True
    while changed:
        changed = False
        for child, parent in PARENT.items():
            if parent in out and child not in out:
                out.add(child)
                changed = True
    return out


def _rot(axis, deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    if axis == 'X':
        return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
    if axis == 'Y':
        return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


class SeatedTurtle:
    """Malla de la tortuga en unidades de malla, con giros por hueso como Pose.turn de fk_sim."""

    def __init__(self):
        self.joints, self.verts, self.tris = _load()
        self.bones = [_bone_of(v) for v in self.verts]

    def turn(self, bone, axis, deg):
        pivot = self.joints[bone].copy()
        moved = _descendants(bone)
        rot = _rot(axis, deg)
        for name in moved:
            if name in self.joints:
                self.joints[name] = pivot + rot @ (self.joints[name] - pivot)
        mask = np.array([b in moved for b in self.bones])
        self.verts[mask] = pivot + (self.verts[mask] - pivot) @ rot.T

    def sit(self):
        for side in ('Left', 'Right'):
            self.turn(side + 'UpLeg', 'X', 90.0)   # muslos hacia delante
            self.turn(side + 'Leg', 'X', -90.0)    # espinillas hacia abajo
        return self

    def arms_driver(self):
        """Manos al volante: la mano queda ~0,55 m delante, ±0,20 m al lado y ~0,27 m sobre la cadera."""
        for side, sign in (('Left', 1.0), ('Right', -1.0)):
            self.turn(side + 'Arm', 'Y', sign * 10.0)
            self.turn(side + 'Arm', 'Z', sign * 60.0)
            self.turn(side + 'ForeArm', 'Z', sign * 50.0)
            self.turn(side + 'ForeArm', 'X', 25.0)
        return self

    def arms_gunner(self):
        """Brazos libres: izquierdo apuntando al frente y arriba, derecho levantado para lanzar."""
        self.turn('LeftArm', 'Z', 80.0)
        self.turn('LeftArm', 'X', 25.0)
        self.turn('RightArm', 'Y', 95.0)
        self.turn('RightArm', 'X', -20.0)
        return self

    def measure(self):
        """Cotas de la postura sentada, en metros y relativas al hueso Hips (z hacia arriba, x hacia delante)."""
        hips = self.joints['Hips']
        legs_low = np.array([(b.endswith('Leg') and not b.endswith('UpLeg')) or b.endswith('Foot') for b in self.bones])
        seat_pts = self.verts[np.array([b == 'Hips' or b.endswith('UpLeg') for b in self.bones])]
        body = self.verts[np.array([b == 'Hips' for b in self.bones])]
        head = body[body[:, 2] > self.joints['Neck'][2]]
        k = MESH_SCALE * CM_TO_M
        return {
            'snout_ahead_of_hip': (head[:, 1].max() - hips[1]) * k,
            'hip_above_seat': (hips[2] - seat_pts[:, 2].min()) * k,
            'hip_above_floor': (hips[2] - self.verts[legs_low][:, 2].min()) * k,
            'shell_behind_hip': (hips[1] - body[:, 1].min()) * k,
            'toes_ahead_of_hip': (self.verts[legs_low][:, 1].max() - hips[1]) * k,
            'head_top_above_hip': (self.verts[:, 2].max() - hips[2]) * k,
            'half_width': np.abs(body[:, 0]).max() * k,
        }

    def to_buggy(self):
        """Vértices en el espacio del buggy (m, +X delante, +Y izquierda) con Hips en el origen, y triángulos.

        Malla (x izq., y delante, z) -> buggy (y, x, z): es una reflexión (UE es levógiro), así que los
        triángulos se devuelven con el orden invertido para conservar las normales hacia fuera.
        """
        rel = (self.verts - self.joints['Hips']) * MESH_SCALE * CM_TO_M
        out = np.stack([rel[:, 1], rel[:, 0], rel[:, 2]], axis=1)
        return out, [(a, c, b) for a, b, c in self.tris]


def driver():
    return SeatedTurtle().sit().arms_driver()


def gunner():
    return SeatedTurtle().sit().arms_gunner()
