"""Buggy biplaza del Rally Tortuga: SM_TN_BuggyBody (chasis + sockets) y SM_TN_BuggyTire (rueda, pivote en el eje).

Uso (reproducible; regenera .blend, FBX y manifest):
    blender -b --factory-startup --python-exit-code 1 --python Art/Source/Vehicles/Buggy/build_buggy.py
    blender -b --factory-startup --python-exit-code 1 --python Art/Source/Vehicles/Buggy/validate_buggy.py
    blender -b --factory-startup --python-exit-code 1 --python Art/Source/Vehicles/Buggy/render_sheet.py
(o build_buggy.ps1, que encadena los tres).

Medidas (buggy de HellYeah, Tools/Blender/props/buggy.py y HYBuggyWheel.cpp): la física la pone el esqueleto
SKM_Offroad del template de vehículos de UE; la carrocería es una malla estática pegada a la raíz del esqueleto y
las ruedas, mallas estáticas en los sockets VisWheel_*. Por eso aquí hay mallas separadas y no un esqueleto propio:
el pipeline de HellYeah (a portar según Docs/Analisis/2026-09-29/B_buggy_sync.md) ya trae el esqueleto con sus
huesos PhysWheel_*, y un esqueleto nuevo obligaría a rehacer PhysicsAsset, AnimBP y ajuste de Chaos.
    PhysWheel_FL/FR  x = +1,683  y = ±1,241  z = 0,511   (m, origen del esqueleto a ras de suelo)
    PhysWheel_BL/BR  x = -1,352  y = ±1,398  z = 0,508
    Rueda: radio 51 cm, ancho 35 cm. Batalla 3,035 m. Carrocería de HellYeah: 4,125 × 2,452 × 1,39 m (z 0,40-1,79).

Asientos: salen de la malla real de la tortuga sentada (turtle_pose.py): cadera a 0,374 m del suelo de los pies y a
0,154 m del cojín; caparazón 0,20 m por detrás de la cadera. Los sockets Seat_* están en la articulación Hips de la
tortuga sentada, mirando a +X.

Unreal (sin importar todavía): FBX en cm (UnitScaleFactor 1), escala 1, +X delante, Z arriba, origen en el suelo.
    Import: Static Mesh, Combine Meshes on, Auto Generate Collision off (el chasis colisiona con el PhysicsAsset),
    Import Vertex Color = Replace, Normal Import = Import Normals, sin Generate Lightmap UVs si no hay lightmaps.
    Sockets: los empties SOCKET_* del FBX pasan a sockets de SM_TN_BuggyBody.
    Material M_TN_Buggy (a crear en Content por el equipo): un solo material; decodifica el color de vértice
        Base = lerp(lerp(lerp(lerp(TrimColor, PaintColor, R), DetailColor, G), WheelColor, B), LightColor, R*G*B) * A
        Emissive = LightColor * R*G*B * LightEmissive. Parámetros vectoriales para las skins en un MI; sin texturas.
"""
import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import buggy_mesh as bmx  # noqa: E402
import turtle_pose  # noqa: E402

MIN_BLENDER = (4, 2, 0)
M_TO_CM = 100.0
EXPORT_DIR = os.path.join(HERE, 'export')
BLEND_PATH = os.path.join(HERE, 'SM_TN_Buggy.blend')
MANIFEST_PATH = os.path.join(HERE, 'manifest.json')
BODY_NAME, TIRE_NAME, MATERIAL_NAME = 'SM_TN_BuggyBody', 'SM_TN_BuggyTire', 'M_TN_Buggy'

# ── Medidas de HellYeah (m) ──────────────────────────────────────────────────
TIRE_RADIUS, TIRE_WIDTH = 0.51, 0.35
FRONT_AXLE = (1.683, 1.241, 0.511)
REAR_AXLE = (-1.352, 1.398, 0.508)
FRONT_HUB_Y = FRONT_AXLE[1] - TIRE_WIDTH / 2
REAR_HUB_Y = REAR_AXLE[1] - TIRE_WIDTH / 2

# ── Distribución (m) ─────────────────────────────────────────────────────────
FLOOR_Z = 0.55             # suelo de la cabina (pies de la conductora)
GUNNER_FLOOR_Z = 0.90      # reposapiés de la artillera (tapa de la carrocería trasera)
DRIVER_HIP_X = 0.22
GUNNER_HIP_X = -0.80
SEAT_HALF_Y = 0.25
ROLL_Y = 0.55              # semiancho de los arcos
DRIVER_HOOP_X, GUNNER_RAIL_Z = -0.14, 1.45
REAR_HOOP_X, REAR_HOOP_TOP = -1.22, 1.75
BAR_R = 0.04
MUZZLE_AHEAD_OF_SNOUT, MUZZLE_ABOVE_HIP = 0.10, 0.42

# ── Paleta por defecto (sRGB) para el .blend y la lámina; en Unreal son parámetros del MI ──
DEFAULT_SKIN = {'paint': 0xE4572E, 'detail': 0xF4EFE2, 'wheel': 0x2B2833, 'trim': 0x80878C, 'light': 0xFFEE99}


def srgb_to_linear(hex_color):
    def chan(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return (chan((hex_color >> 16) & 255), chan((hex_color >> 8) & 255), chan(hex_color & 255), 1.0)


def seat_layout():
    """Cadera, cojín y respaldo de los dos asientos a partir de la tortuga sentada."""
    m = turtle_pose.driver().measure()
    layout = {}
    for key, hip_x, floor in (('driver', DRIVER_HIP_X, FLOOR_Z), ('gunner', GUNNER_HIP_X, GUNNER_FLOOR_Z)):
        hip_z = floor + m['hip_above_floor']
        layout[key] = {
            'hip': (hip_x, 0.0, hip_z),
            'cushion_z': hip_z - m['hip_above_seat'],
            'back_x': hip_x - m['shell_behind_hip'] - 0.005,
            'floor_z': floor,
        }
    layout['measure'] = {k: round(float(v), 4) for k, v in m.items()}
    return layout


# ── Chasis ───────────────────────────────────────────────────────────────────

def _mirror(points):
    return [p for x, y, z in points for p in ((x, y, z), (x, -y, z))]


def _hull_body(b):
    # Suelo de cabina y pontones laterales con el canto superior achaflanado.
    b.box((-0.10, -0.60, 0.40), (1.00, 0.60, FLOOR_Z), 'trim', 0.8)
    for side in (1, -1):
        b.hull([(x, side * y, z) for x, y, z in (
            (-0.10, 0.58, 0.40), (1.02, 0.58, 0.40), (-0.10, 0.58, 0.88), (1.02, 0.58, 0.90),
            (-0.10, 0.84, 0.44), (1.02, 0.84, 0.44), (-0.10, 0.84, 0.76), (1.02, 0.84, 0.78),
            (-0.10, 0.76, 0.88), (1.02, 0.76, 0.90), (0.00, 0.80, 0.40), (0.92, 0.80, 0.40))], 'paint')
    # Morro y capó: se estrecha y baja hacia delante.
    b.hull(_mirror([(0.95, 0.78, 0.40), (0.95, 0.78, 0.98), (1.10, 0.74, 1.00),
                    (2.02, 0.52, 0.40), (2.10, 0.50, 0.46), (2.10, 0.48, 0.68), (1.98, 0.50, 0.72)]), 'paint')
    # Carrocería trasera (reposapiés de la artillera y soporte del motor), sin caja ni plataforma de carga.
    b.hull(_mirror([(-0.10, 0.62, 0.40), (-0.10, 0.62, GUNNER_FLOOR_Z), (-1.78, 0.62, 0.42),
                    (-1.78, 0.62, GUNNER_FLOOR_Z + 0.04), (-1.92, 0.50, 0.54), (-1.92, 0.50, 0.88)]), 'paint')


def _hood_details(b):
    back, front = (1.10, 0.0, 1.00), (1.98, 0.0, 0.72)
    n = bmx.slope_normal(back, front)
    for y0, y1 in ((0.06, 0.16), (-0.16, -0.06)):  # dos franjas de rally
        pts = []
        for x, _, z in (back, front):
            for y in (y0, y1):
                pts += [bmx.offset((x, y, z), n, -0.01), bmx.offset((x, y, z), n, 0.012)]
        b.hull(pts, 'detail')
    for side in (1, -1):  # faros con aro
        b.tube((2.04, side * 0.30, 0.57), (2.11, side * 0.30, 0.57), 0.10, 'trim', 10)
        b.tube((2.08, side * 0.30, 0.57), (2.135, side * 0.30, 0.57), 0.08, 'light', 10)
    b.tube((2.15, -0.62, 0.44), (2.15, 0.62, 0.44), 0.045, 'trim', 8, extend=0.02)  # parachoques
    for side in (1, -1):
        b.tube((2.15, side * 0.40, 0.44), (2.00, side * 0.40, 0.46), 0.035, 'trim', 6)
        # Escarapela del dorsal en el pontón.
        b.tube((0.45, side * 0.835, 0.62), (0.45, side * 0.857, 0.62), 0.13, 'detail', 10)
        b.tube((0.45, side * 0.85, 0.62), (0.45, side * 0.866, 0.62), 0.065, 'paint', 8)


def _cockpit(b, seats):
    # Salpicadero, columna y volante (donde caen las manos de la conductora sentada).
    b.hull(_mirror([(0.90, 0.60, 0.86), (1.06, 0.60, 0.86), (0.95, 0.56, 1.04), (1.08, 0.60, 1.02)]), 'trim')
    hip = Vector(seats['driver']['hip'])
    wheel_c = hip + Vector((0.52, 0.0, 0.25))
    col_axis = (Vector((0.97, 0.0, 0.98)) - wheel_c).normalized()
    b.tube((0.97, 0.0, 0.98), tuple(wheel_c), 0.025, 'trim', 6)
    b.torus(tuple(wheel_c), tuple(col_axis), 0.19, 0.025, 'wheel', 8, 4)
    b.tube(tuple(wheel_c), tuple(wheel_c + col_axis * 0.05), 0.05, 'detail', 6)
    for key, pedestal_floor in (('driver', FLOOR_Z), ('gunner', GUNNER_FLOOR_Z)):
        s = seats[key]
        hx, cz, bx = s['hip'][0], s['cushion_z'], s['back_x']
        b.box((hx - 0.16, -0.18, pedestal_floor), (hx + 0.16, 0.18, cz - 0.10), 'trim', 0.85)
        b.box((bx, -SEAT_HALF_Y, cz - 0.10), (hx + 0.22, SEAT_HALF_Y, cz), 'detail')
        b.box((bx - 0.10, -SEAT_HALF_Y, cz - 0.10), (bx, SEAT_HALF_Y, cz + (0.31 if key == 'driver' else 0.30)), 'detail', 0.9)


def _roll_cage(b):
    ext = BAR_R
    for side in (1, -1):
        y = side * ROLL_Y
        # Arco de la conductora, que hace de asidero de la artillera.
        b.tube((DRIVER_HOOP_X, y, FLOOR_Z), (DRIVER_HOOP_X, y, GUNNER_RAIL_Z), BAR_R, 'trim', 8, extend=ext)
        b.tube((DRIVER_HOOP_X, y, GUNNER_RAIL_Z), (1.00, side * 0.60, 1.00), BAR_R, 'trim', 8, extend=ext)
        # Barandilla lateral hasta el arco trasero (a la altura de la cintura de la artillera).
        b.tube((DRIVER_HOOP_X, y, GUNNER_RAIL_Z), (REAR_HOOP_X, y, GUNNER_RAIL_Z), BAR_R, 'trim', 8, extend=ext)
        # Arco trasero, detrás del respaldo de la artillera, y tirantes al motor.
        b.tube((REAR_HOOP_X, y, GUNNER_FLOOR_Z), (REAR_HOOP_X, y, REAR_HOOP_TOP), BAR_R, 'trim', 8, extend=ext)
        b.tube((REAR_HOOP_X, y, REAR_HOOP_TOP - BAR_R), (-1.85, side * 0.45, 0.92), BAR_R, 'trim', 8)
    b.tube((DRIVER_HOOP_X, -ROLL_Y, GUNNER_RAIL_Z), (DRIVER_HOOP_X, ROLL_Y, GUNNER_RAIL_Z), BAR_R, 'trim', 8, extend=ext)
    b.tube((REAR_HOOP_X, -ROLL_Y, REAR_HOOP_TOP), (REAR_HOOP_X, ROLL_Y, REAR_HOOP_TOP), BAR_R, 'trim', 8, extend=ext)


def _engine(b):
    b.box((-1.84, -0.34, GUNNER_FLOOR_Z), (-1.30, 0.34, 1.16), 'trim', 0.75)
    for k in range(4):  # aletas de refrigeración
        x = -1.76 + k * 0.14
        b.box((x - 0.03, -0.37, 0.98), (x + 0.03, 0.37, 1.20), 'trim', 0.9)
    b.tube((-1.57, 0.0, 1.14), (-1.57, 0.0, 1.30), 0.15, 'detail', 10)  # filtro de aire
    b.tube((-1.57, 0.0, 1.29), (-1.57, 0.0, 1.33), 0.10, 'paint', 10)
    for side in (1, -1):  # escapes
        b.tube((-1.70, side * 0.26, 0.98), (-1.97, side * 0.30, 1.10), 0.05, 'trim', 8, extend=0.02)
        b.tube((-1.96, side * 0.30, 1.095), (-2.00, side * 0.31, 1.115), 0.06, 'trim', 8, shade=0.6)
    b.tube((-1.98, -0.60, 0.58), (-1.98, 0.60, 0.58), 0.045, 'trim', 8, extend=0.02)  # parachoques trasero


def _suspension(b):
    for side in (1, -1):
        for axle, hub_y, inner_y, mount_z in ((FRONT_AXLE, FRONT_HUB_Y, 0.52, 0.58), (REAR_AXLE, REAR_HUB_Y, 0.58, 0.52)):
            x, _, wz = axle
            b.tube((x, side * inner_y, mount_z), (x, side * hub_y, wz), 0.04, 'trim', 6)
            b.tube((x, side * inner_y, mount_z + 0.16), (x, side * (hub_y - 0.02), wz + 0.10), 0.035, 'trim', 6)
            b.tube((x, side * (hub_y - 0.03), wz), (x, side * hub_y, wz), 0.08, 'trim', 8)  # mangueta
            # Amortiguador con muelle de color (zona detalle); la copela queda dentro de la carrocería.
            top_dx, top_y, top_z = (-0.10, 0.55, 0.78) if axle is FRONT_AXLE else (0.10, 0.58, 0.86)
            b.tube((x + top_dx, side * (hub_y - 0.18), wz + 0.05), (x + top_dx, side * top_y, top_z), 0.055, 'detail', 8)


def build_body(seats):
    b = bmx.Builder()
    _hull_body(b)
    _hood_details(b)
    _cockpit(b, seats)
    _roll_cage(b)
    _engine(b)
    _suspension(b)
    return b.finish(M_TO_CM)


# ── Rueda ────────────────────────────────────────────────────────────────────

TIRE_PROFILE = [  # (radio, y) de dentro (-Y) a fuera (+Y, cara exterior de las ruedas izquierdas)
    (0.0, -0.12), (0.30, -0.12), (0.34, -0.175), (0.45, -0.175), (0.50, -0.13),
    (0.50, 0.13), (0.45, 0.175), (0.34, 0.175), (0.30, 0.11), (0.12, 0.11), (0.10, 0.15), (0.0, 0.15)]
TIRE_SEGMENTS = 12
RUBBER_BANDS = range(2, 7)
SPOKE_BAND = 8


def _tire_zone(band, seg):
    if band in RUBBER_BANDS:
        return 'wheel', 1.0 if band == 4 else 0.9
    if band == SPOKE_BAND:
        return ('detail', 1.0) if seg % 4 == 0 else ('trim', 1.0)  # radios claros: se ve girar
    if band >= SPOKE_BAND + 1:
        return 'detail', 1.0
    return 'trim', 0.85


def build_tire():
    b = bmx.Builder()
    b.lathe(TIRE_PROFILE, TIRE_SEGMENTS, _tire_zone)
    step = 2 * math.pi / TIRE_SEGMENTS
    face_r = 0.50 * math.cos(step / 2)
    for s in range(TIRE_SEGMENTS):  # tacos alternados sobre la banda de rodadura
        a = step * (s + 0.5)
        y = 0.055 if s % 2 else -0.055
        lo, hi = (-0.045, y - 0.07, face_r - 0.01), (0.045, y + 0.07, TIRE_RADIUS)
        faces = b.box(lo, hi, 'wheel', 0.8)
        verts = {v for f in faces for v in f.verts}
        rot = Matrix.Rotation(-(a - math.pi / 2), 4, 'Y')  # la caja nace arriba (+Z): se gira a su ángulo
        for v in verts:
            v.co = (rot @ v.co.to_4d()).to_3d()
    return b.finish(M_TO_CM)


# ── Escena, material y exportación ───────────────────────────────────────────

def _enum_ok(owner_rna, prop, value):
    items = [i.identifier for i in owner_rna.properties[prop].enum_items]
    if value not in items:
        raise RuntimeError(f'{prop}: {value!r} no existe en esta versión ({items})')
    return value


def _node(nodes, type_name, bl_idname):
    node = next((n for n in nodes if n.type == type_name), None)
    return node or nodes.new(bl_idname)


def _mix(nodes, links, a, b, fac, blend='MIX'):
    mix = nodes.new('ShaderNodeMix')
    mix.data_type = _enum_ok(mix.bl_rna, 'data_type', 'RGBA')
    mix.blend_type = _enum_ok(mix.bl_rna, 'blend_type', blend)
    sock = {s.identifier: s for s in mix.inputs}
    links.new(a, sock['A_Color']) if not isinstance(a, tuple) else setattr(sock['A_Color'], 'default_value', a)
    links.new(b, sock['B_Color']) if not isinstance(b, tuple) else setattr(sock['B_Color'], 'default_value', b)
    links.new(fac, sock['Factor_Float']) if not isinstance(fac, float) else setattr(sock['Factor_Float'], 'default_value', fac)
    return next(s for s in mix.outputs if s.identifier == 'Result_Color')


def build_material(skin):
    """Réplica en Blender de M_TN_Buggy: zonas por máscara de color de vértice y un color por zona."""
    mat = bpy.data.materials.get(MATERIAL_NAME) or bpy.data.materials.new(MATERIAL_NAME)
    if bpy.app.version < (5, 0, 0):  # en 5.x los materiales siempre tienen nodos (use_nodes está obsoleto)
        mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = _node(nodes, 'BSDF_PRINCIPLED', 'ShaderNodeBsdfPrincipled')
    for n in [n for n in nodes if n not in (bsdf,) and n.type != 'OUTPUT_MATERIAL']:
        nodes.remove(n)
    vcol = nodes.new('ShaderNodeVertexColor')
    vcol.layer_name = 'Zone'
    sep = nodes.new('ShaderNodeSeparateColor')
    links.new(vcol.outputs['Color'], sep.inputs[0])
    r, g, bl = sep.outputs[0], sep.outputs[1], sep.outputs[2]
    col = {k: srgb_to_linear(v) for k, v in skin.items()}
    c = _mix(nodes, links, col['trim'], col['paint'], r)
    c = _mix(nodes, links, c, col['detail'], g)
    c = _mix(nodes, links, c, col['wheel'], bl)
    rg = nodes.new('ShaderNodeMath')
    rg.operation = _enum_ok(rg.bl_rna, 'operation', 'MULTIPLY')
    links.new(r, rg.inputs[0]); links.new(g, rg.inputs[1])
    rgb = nodes.new('ShaderNodeMath')
    rgb.operation = 'MULTIPLY'
    links.new(rg.outputs[0], rgb.inputs[0]); links.new(bl, rgb.inputs[1])
    c = _mix(nodes, links, c, col['light'], rgb.outputs[0])
    shaded = _mix(nodes, links, c, vcol.outputs['Alpha'], 1.0, 'MULTIPLY')
    ins = {s.identifier: s for s in bsdf.inputs}
    links.new(shaded, ins['Base Color'])
    ins['Roughness'].default_value = 0.7
    ins['Emission Color'].default_value = col['light']
    links.new(rgb.outputs[0], ins['Emission Strength'])
    mat.diffuse_color = col['paint']
    return mat


def _mesh_object(name, bm, mat, collection):
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    attr = mesh.color_attributes.new('Zone', 'FLOAT_COLOR', 'CORNER')
    zones, shades = mesh.attributes['zone'].data, mesh.attributes['shade'].data
    for poly in mesh.polygons:
        r, g, b = bmx.ZONE_MASKS[bmx.ZONE_NAMES[zones[poly.index].value]]
        color = (r, g, b, shades[poly.index].value)
        for li in poly.loop_indices:
            attr.data[li].color = color
    mesh.attributes.remove(mesh.attributes['zone'])
    mesh.attributes.remove(mesh.attributes['shade'])
    mesh.color_attributes.active_color = attr
    mesh.color_attributes.render_color_index = mesh.color_attributes.active_color_index
    mesh.materials.append(mat)
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    return obj


def _unwrap(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.02)
    bpy.ops.object.mode_set(mode='OBJECT')


def _sockets(body, seats, collection):
    gunner = seats['gunner']['hip']
    # Delante del hocico de la artillera (no dentro de su cabeza), por encima del arco y detrás de la conductora.
    muzzle = (gunner[0] + seats['measure']['snout_ahead_of_hip'] + MUZZLE_AHEAD_OF_SNOUT, 0.0, gunner[2] + MUZZLE_ABOVE_HIP)
    points = {'Seat_Driver': seats['driver']['hip'], 'Seat_Gunner': gunner, 'Muzzle_Gunner': muzzle}
    out = {}
    for name, loc in points.items():
        empty = bpy.data.objects.new('SOCKET_' + name, None)
        empty.empty_display_type = _enum_ok(empty.bl_rna, 'empty_display_type', 'ARROWS')
        empty.empty_display_size = 20.0
        empty.parent = body
        empty.location = Vector(loc) * M_TO_CM
        collection.objects.link(empty)
        out[name] = [round(float(c) * M_TO_CM, 2) for c in loc]
    return out


def _wheel_instances(tire, collection):
    for tag, (x, y, z), side in (('FL', FRONT_AXLE, 1), ('FR', FRONT_AXLE, -1), ('BL', REAR_AXLE, 1), ('BR', REAR_AXLE, -1)):
        inst = bpy.data.objects.new(f'Preview_Tire_{tag}', tire.data)
        inst.location = Vector((x, side * y, z)) * M_TO_CM
        inst.rotation_euler = (0.0, 0.0, 0.0 if side > 0 else math.pi)  # como HYBuggy: las derechas giradas 180°
        collection.objects.link(inst)


def _export(objects, name):
    os.makedirs(EXPORT_DIR, exist_ok=True)
    path = os.path.join(EXPORT_DIR, name + '.fbx')
    op_rna = bpy.ops.export_scene.fbx.get_rna_type()
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={'MESH', 'EMPTY'},
        apply_unit_scale=True, apply_scale_options=_enum_ok(op_rna, 'apply_scale_options', 'FBX_SCALE_UNITS'),
        mesh_smooth_type=_enum_ok(op_rna, 'mesh_smooth_type', 'FACE'),
        colors_type=_enum_ok(op_rna, 'colors_type', 'SRGB'),
        add_leaf_bones=False, bake_anim=False)
    return os.path.relpath(path, turtle_pose.REPO_ROOT).replace(os.sep, '/')


def _setup_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    units = bpy.context.scene.unit_settings
    units.system = 'METRIC'
    units.scale_length = 1.0 / M_TO_CM
    try:  # enum dinámico (depende de `system`): RNA solo declara DEFAULT
        units.length_unit = 'CENTIMETERS'
    except TypeError as err:
        print(f'[build_buggy] aviso: length_unit sin cambiar ({err})')
    export = bpy.data.collections.new('Export')
    preview = bpy.data.collections.new('Preview')
    for c in (export, preview):
        bpy.context.scene.collection.children.link(c)
    return export, preview


def _tris(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def main():
    if bpy.app.version < MIN_BLENDER:
        raise RuntimeError(f'Blender {bpy.app.version_string} < {MIN_BLENDER}')
    export, preview = _setup_scene()
    seats = seat_layout()
    mat = build_material(DEFAULT_SKIN)
    body = _mesh_object(BODY_NAME, build_body(seats), mat, export)
    tire = _mesh_object(TIRE_NAME, build_tire(), mat, export)
    for obj in (body, tire):
        _unwrap(obj)
    sockets = _sockets(body, seats, export)
    manifest = {'generator': 'Art/Source/Vehicles/Buggy/build_buggy.py', 'blender': bpy.app.version_string,
                'license': 'Original de Tortunabo, generado por script (sin fuentes externas)',
                'reference_style': 'Source/Tortunabo/Private/World/Beach/TN_BeachCritterMeshes.h (tanque de juguete)',
                'reference_measures': 'HellYeah Tools/Blender/props/buggy.py + HYBuggyWheel.cpp',
                'turtle_data': 'Scripts/tools/data/turtle_geo.json + turtle_ref_pose.txt',
                'seat_measure_m': seats['measure'], 'sockets_cm': sockets, 'assets': []}
    for obj, extra in ((body, [s for s in export.objects if s.name.startswith('SOCKET_')]), (tire, [])):
        fbx = _export([obj] + extra, obj.name)
        manifest['assets'].append({'name': obj.name, 'fbx': fbx, 'tris': _tris(obj),
                                   'dimensions_cm': [round(d, 1) for d in obj.dimensions],
                                   'material_slots': [s.material.name for s in obj.material_slots]})
    _wheel_instances(tire, preview)
    tire.hide_set(True)
    tire.hide_render = True
    with open(MANIFEST_PATH, 'w', encoding='utf-8') as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
    bpy.context.preferences.filepaths.save_version = 0  # sin .blend1
    bpy.ops.wm.save_as_mainfile(filepath=BLEND_PATH, compress=True)
    for a in manifest['assets']:
        print(f"[build_buggy] {a['name']} tris={a['tris']} dims_cm={a['dimensions_cm']}")
    print(f'[build_buggy] sockets_cm={sockets} medida_tortuga={seats["measure"]}')


if __name__ == '__main__':
    main()
