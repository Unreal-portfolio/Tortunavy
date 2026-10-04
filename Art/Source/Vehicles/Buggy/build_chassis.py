"""Esqueleto de física del buggy del Rally: SK_TN_BuggyChassis (#290).

Uso (reproducible; regenera export/SK_TN_BuggyChassis.fbx):
    blender -b --factory-startup --python-exit-code 1 --python Art/Source/Vehicles/Buggy/build_chassis.py

Sustituye a SKM_Offroad (8,9 MB, con AnimBP) como malla raíz de ATN_Buggy. Chaos Vehicles necesita una malla con
cuerpo físico en la raíz del peón (AWheeledVehiclePawn): esta lleva solo lo imprescindible y no se ve.
    - Hueso raíz «OffroadCar» en el origen (a ras de suelo): ahí está el cuerpo del chasis. Se llama como en el
      template porque su PhysicsAsset se duplica tal cual (Scripts/tools/import_buggy_rally.py): misma caja, mismo
      reparto de masa e inercia, y así no cambian las medidas de conducción de Docs/Rally_MVP.md.
    - Huesos PhysWheel_FL/FR/BL/BR en el eje de cada rueda (las mismas cotas que build_buggy.py: FRONT_AXLE y
      REAR_AXLE). Chaos coloca las ruedas en ellos; los neumáticos visibles los mueve ATN_Buggy en C++.
    - Una caja de 12 triángulos con todo el peso en la raíz (una malla esquelética necesita geometría): va oculta.

Ejes y unidades como build_buggy.py: centímetros, +X delante, Z arriba, origen en el suelo. En Blender +Y es la
izquierda (FL en +Y); el FBX la pasa a -Y de Unreal, como los huesos de SKM_Offroad.
"""
import os
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_buggy as bb  # noqa: E402

CHASSIS_NAME = 'SK_TN_BuggyChassis'
ROOT_BONE = 'OffroadCar'
BONE_LENGTH_CM = 20.0
# Caja oculta (m): el suelo de la cabina y los pontones, sin tocar las ruedas.
PROXY_MIN = (-1.80, -0.60, 0.40)
PROXY_MAX = (1.80, 0.60, 0.90)


def wheel_bones():
    """Nombre y centro (m, ejes de Blender) de cada hueso de rueda."""
    fx, fy, fz = bb.FRONT_AXLE
    rx, ry, rz = bb.REAR_AXLE
    return {'PhysWheel_FL': (fx, fy, fz), 'PhysWheel_FR': (fx, -fy, fz),
            'PhysWheel_BL': (rx, ry, rz), 'PhysWheel_BR': (rx, -ry, rz)}


def build_armature(collection):
    data = bpy.data.armatures.new('Armature')
    arm = bpy.data.objects.new('Armature', data)  # Unreal no crea hueso para un nodo raíz llamado Armature
    collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode='EDIT')
    root = data.edit_bones.new(ROOT_BONE)
    root.head = (0.0, 0.0, 0.0)
    # El giro que el FBX deja en los huesos lo quita import_buggy_rally.py: la caja del PhysicsAsset está en los ejes
    # del hueso raíz, que tiene que quedar sin giro, como en el template.
    root.tail = (0.0, -BONE_LENGTH_CM, 0.0)
    for name, (x, y, z) in wheel_bones().items():
        bone = data.edit_bones.new(name)
        head = (x * bb.M_TO_CM, y * bb.M_TO_CM, z * bb.M_TO_CM)
        bone.head = head
        bone.tail = (head[0], head[1] - BONE_LENGTH_CM, head[2])
        bone.parent = root
    bpy.ops.object.mode_set(mode='OBJECT')
    return arm


def build_proxy(collection, arm):
    lo = [c * bb.M_TO_CM for c in PROXY_MIN]
    hi = [c * bb.M_TO_CM for c in PROXY_MAX]
    verts = [(x, y, z) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    mesh = bpy.data.meshes.new(CHASSIS_NAME)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(CHASSIS_NAME, mesh)
    collection.objects.link(obj)
    group = obj.vertex_groups.new(name=ROOT_BONE)
    group.add(list(range(len(verts))), 1.0, 'REPLACE')
    obj.parent = arm
    mod = obj.modifiers.new('Armature', 'ARMATURE')
    mod.object = arm
    return obj


def export(arm, proxy):
    os.makedirs(bb.EXPORT_DIR, exist_ok=True)
    path = os.path.join(bb.EXPORT_DIR, CHASSIS_NAME + '.fbx')
    op_rna = bpy.ops.export_scene.fbx.get_rna_type()
    bpy.ops.object.select_all(action='DESELECT')
    for o in (arm, proxy):
        o.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={'ARMATURE', 'MESH'},
        apply_unit_scale=True, apply_scale_options=bb._enum_ok(op_rna, 'apply_scale_options', 'FBX_SCALE_UNITS'),
        mesh_smooth_type=bb._enum_ok(op_rna, 'mesh_smooth_type', 'FACE'),
        add_leaf_bones=False, bake_anim=False, use_armature_deform_only=False,
        primary_bone_axis='Y', secondary_bone_axis='X')
    return path


def main():
    if bpy.app.version < bb.MIN_BLENDER:
        raise RuntimeError(f'Blender {bpy.app.version_string} < {bb.MIN_BLENDER}')
    export_collection, _ = bb._setup_scene()
    arm = build_armature(export_collection)
    proxy = build_proxy(export_collection, arm)
    path = export(arm, proxy)
    bones = {name: [round(c * bb.M_TO_CM, 1) for c in loc] for name, loc in wheel_bones().items()}
    print(f'[build_chassis] {os.path.relpath(path, HERE)} huesos_cm={bones}')


if __name__ == '__main__':
    main()
