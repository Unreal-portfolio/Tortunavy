"""Buggy del Rally en /Game (#290): chasis de física, material por zonas y skins de Art/Source/Vehicles/Buggy.

Hace, en /Game/Art/Source/Vehicles/Buggy/export (donde la galería ya importó SM_TN_BuggyBody y SM_TN_BuggyTire):
  1. Importa SK_TN_BuggyChassis.fbx (build_chassis.py) como malla esquelética, con su esqueleto y sin materiales.
  2. Le pone PA_TN_BuggyChassis: copia del PhysicsAsset de SKM_Offroad (misma caja del chasis, misma masa e inercia),
     y su hueso raíz sin giro. Así ATN_Buggy deja de cargar SKM_Offroad y su AnimBP sin cambiar la física.
  3. Crea M_TN_BuggyZones como describe build_buggy.py para M_TN_Buggy (el de la importación FBX no tiene parámetros) y
     se lo pone a SM_TN_BuggyBody y SM_TN_BuggyTire: decodifica las zonas del color de vértice
     Base = lerp(lerp(lerp(lerp(Trim, Paint, R), Detail, G), Wheel, B), Light, R*G*B) * A
     Emissive = Light * R*G*B * LightEmissive, con un parámetro vectorial por zona.
  4. Crea las skins MI_TN_Buggy_Mar, MI_TN_Buggy_Alga y MI_TN_Buggy_Medusa con los colores de render_sheet.py.

Uso (editor cerrado):
  UnrealEditor-Win64-DebugGame-Cmd.exe Tortunabo.uproject -run=pythonscript
    -script=Scripts/tools/import_buggy_rally.py -EnablePlugins=PythonScriptPlugin -unattended -nosplash -nullrhi
Se puede repetir: reimporta el chasis, rehace las skins y conserva M_TN_BuggyZones si ya existe (para rehacerlo, bórralo antes).
"""

import os

import unreal

DEST = "/Game/Art/Source/Vehicles/Buggy/export"
CHASSIS = "SK_TN_BuggyChassis"
PHYSICS_ASSET = "PA_TN_BuggyChassis"
OFFROAD_PHYSICS_ASSET = "/Game/Vehicles/OffroadCar/SKM_Offroad_PhysicsAsset"
MATERIAL = "M_TN_BuggyZones"
MESHES = ("SM_TN_BuggyBody", "SM_TN_BuggyTire")
LOG_PREFIX = "[BuggyRally]"

# Colores sRGB de Art/Source/Vehicles/Buggy/render_sheet.py (SKINS) y build_buggy.py (DEFAULT_SKIN).
DEFAULT_SKIN = {"Paint": 0xE4572E, "Detail": 0xF4EFE2, "Wheel": 0x2B2833, "Trim": 0x80878C, "Light": 0xFFEE99}
SKINS = {
    "Mar": {"Paint": 0x2F80ED, "Detail": 0xFFD23F, "Wheel": 0x2B2833, "Trim": 0x80878C, "Light": 0xFFEE99},
    "Alga": {"Paint": 0x3DAE5A, "Detail": 0xF4EFE2, "Wheel": 0x3A2E27, "Trim": 0x5B6168, "Light": 0xFFEE99},
    "Medusa": {"Paint": 0xC98BFF, "Detail": 0xFF6FA8, "Wheel": 0x2B2833, "Trim": 0xD5DCE6, "Light": 0xFFEE99},
}
ZONES = ("Trim", "Paint", "Detail", "Wheel", "Light")
LIGHT_EMISSIVE = 2.0
ROUGHNESS = 0.7
# Especular bajo: con el 0,5 por defecto, las zonas oscuras (ruedas, motor) reflejan el cielo y salen azuladas.
SPECULAR = 0.15

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def log(msg):
    unreal.log(f"{LOG_PREFIX} {msg}")


def project_dir():
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def srgb_to_linear(hex_color):
    def chan(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return unreal.LinearColor(chan((hex_color >> 16) & 255), chan((hex_color >> 8) & 255), chan(hex_color & 255), 1.0)


def import_chassis():
    fbx = os.path.join(project_dir(), "Art", "Source", "Vehicles", "Buggy", "export", CHASSIS + ".fbx")
    if not os.path.isfile(fbx):
        raise RuntimeError(f"falta {fbx}: ejecuta antes build_chassis.py en Blender")
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("create_physics_asset", False)
    skd = ui.get_editor_property("skeletal_mesh_import_data")
    skd.set_editor_property("import_morph_targets", False)
    skd.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", fbx)
    task.set_editor_property("destination_path", DEST)
    task.set_editor_property("destination_name", CHASSIS)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", ui)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh = unreal.load_asset(f"{DEST}/{CHASSIS}")
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"no se ha importado {CHASSIS} como malla esquelética")
    log(f"chasis importado: {task.get_editor_property('imported_object_paths')}")
    zero_bone_rotations(mesh)
    return mesh


def zero_bone_rotations(mesh):
    """El FBX de Blender deja los huesos girados 90° en X: sin giro, como OffroadCar en SKM_Offroad."""
    modifier = unreal.SkeletonModifier()
    modifier.set_skeletal_mesh(mesh)
    for bone in modifier.get_all_bone_names():
        world = modifier.get_bone_transform(bone, True)
        flat = unreal.Transform(world.translation, unreal.Rotator(0.0, 0.0, 0.0), unreal.Vector(1.0, 1.0, 1.0))
        modifier.set_bone_transform(bone, flat, False)
    if not modifier.commit_skeleton_to_skeletal_mesh():
        raise RuntimeError(f"no se han podido enderezar los huesos de {CHASSIS}")
    EAL.save_loaded_asset(mesh)
    EAL.save_loaded_asset(mesh.get_editor_property("skeleton"))


def assign_physics(mesh):
    path = f"{DEST}/{PHYSICS_ASSET}"
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    physics = EAL.duplicate_asset(OFFROAD_PHYSICS_ASSET, path)
    if not physics:
        raise RuntimeError(f"no se ha podido copiar {OFFROAD_PHYSICS_ASSET}")
    # La vista previa (PreviewSkeletalMesh, solo editor y referencia blanda) no está expuesta a Python: sigue
    # apuntando a SKM_Offroad, que solo se carga si se abre el PhysicsAsset en el editor.
    subsystem = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    if not subsystem.is_physics_asset_compatible(mesh, physics):
        raise RuntimeError(f"{PHYSICS_ASSET} no es compatible con {CHASSIS} (¿huesos con otro nombre?)")
    subsystem.assign_physics_asset(mesh, physics)
    EAL.save_loaded_asset(physics)
    EAL.save_loaded_asset(mesh)
    log(f"PhysicsAsset {path} asignado")


def expression(material, cls, x, y):
    return MEL.create_material_expression(material, cls, x, y)


def vector_param(material, name, color, y):
    node = expression(material, unreal.MaterialExpressionVectorParameter, -900, y)
    node.set_editor_property("parameter_name", name + "Color")
    node.set_editor_property("default_value", srgb_to_linear(color))
    return node


def lerp(material, a, b, alpha, alpha_output, x, y):
    node = expression(material, unreal.MaterialExpressionLinearInterpolate, x, y)
    MEL.connect_material_expressions(a, "", node, "A")
    MEL.connect_material_expressions(b, "", node, "B")
    MEL.connect_material_expressions(alpha, alpha_output, node, "Alpha")
    return node


def multiply(material, a, a_output, b, b_output, x, y):
    node = expression(material, unreal.MaterialExpressionMultiply, x, y)
    MEL.connect_material_expressions(a, a_output, node, "A")
    MEL.connect_material_expressions(b, b_output, node, "B")
    return node


def build_material():
    """Material nuevo (no se reutiliza el M_TN_Buggy que creó la importación FBX: borrar sus nodos desde un commandlet
    revienta el editor con un assert !IsRooted en MaterialEditor). Si ya existe, se conserva."""
    path = f"{DEST}/{MATERIAL}"
    if EAL.does_asset_exist(path):
        log(f"{MATERIAL} ya existe: se conserva")
        return unreal.load_asset(path)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MATERIAL, DEST, unreal.Material, unreal.MaterialFactoryNew())
    vc = expression(material, unreal.MaterialExpressionVertexColor, -1200, 0)
    params = {zone: vector_param(material, zone, DEFAULT_SKIN[zone], -400 + 160 * i) for i, zone in enumerate(ZONES)}
    emissive_k = expression(material, unreal.MaterialExpressionScalarParameter, -900, 500)
    emissive_k.set_editor_property("parameter_name", "LightEmissive")
    emissive_k.set_editor_property("default_value", LIGHT_EMISSIVE)

    color = lerp(material, params["Trim"], params["Paint"], vc, "R", -600, -300)
    color = lerp(material, color, params["Detail"], vc, "G", -450, -250)
    color = lerp(material, color, params["Wheel"], vc, "B", -300, -200)
    rg = multiply(material, vc, "R", vc, "G", -600, 300)
    rgb = multiply(material, rg, "", vc, "B", -450, 300)
    color = lerp(material, color, params["Light"], rgb, "", -150, -150)
    shaded = multiply(material, color, "", vc, "A", 0, -100)
    glow = multiply(material, params["Light"], "", rgb, "", -150, 300)
    glow = multiply(material, glow, "", emissive_k, "", 0, 300)
    rough = expression(material, unreal.MaterialExpressionConstant, 0, 100)
    rough.set_editor_property("r", ROUGHNESS)
    spec = expression(material, unreal.MaterialExpressionConstant, 0, 200)
    spec.set_editor_property("r", SPECULAR)

    MEL.connect_material_property(shaded, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    MEL.recompile_material(material)
    EAL.save_loaded_asset(material)
    log(f"{MATERIAL}: parámetros {MEL.get_vector_parameter_names(material)}")
    return material


def assign_material(material):
    """La carrocería y el neumático usan el material de zonas (en juego, ATN_Buggy pone encima la skin del equipo)."""
    for name in MESHES:
        mesh = unreal.load_asset(f"{DEST}/{name}")
        mesh.set_material(0, material)
        EAL.save_loaded_asset(mesh)
        log(f"{name}: {MATERIAL}")


def build_skins(material):
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    for skin, colors in SKINS.items():
        name = f"MI_TN_Buggy_{skin}"
        path = f"{DEST}/{name}"
        instance = unreal.load_asset(path) if EAL.does_asset_exist(path) else tools.create_asset(
            name, DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(instance, material)
        for zone in ZONES:
            MEL.set_material_instance_vector_parameter_value(instance, zone + "Color", srgb_to_linear(colors[zone]))
        EAL.save_loaded_asset(instance)
        log(f"skin {path}")


def main():
    mesh = import_chassis()
    assign_physics(mesh)
    material = build_material()
    assign_material(material)
    build_skins(material)
    log("hecho")


main()
