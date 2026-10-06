"""Importa el mapa volumetrico fijo y monta su nivel.

Se ejecuta DENTRO del editor de Unreal (headless):
    UnrealEditor-Win64-DebugGame-Cmd.exe <uproject> -run=pythonscript
        -script=<repo>/Scripts/import_terrain_mesh.py -EnablePlugins=PythonScriptPlugin
        -unattended -nosplash -nullrhi

Lee Scripts/terrain_volumes/<nombre>/manifest.json (lo escribe gen_terrain_volume.py) y:
    - crea un DA_<trozo> (UTN_TerrainMeshAsset) por trozo en /Game/Terrain/Volumes/<nombre>,
      cargado desde su binario;
    - crea con ellos un StaticMesh editable por trozo, SM_<trozo> en .../Meshes (Modeling Mode:
      esculpir, deformar, cortar; colision de la propia malla);
    - crea el nivel /Game/Maps/Run/LVL_<nombre>: un StaticMeshActor por trozo en su sitio,
      lamina de agua, sol, cielo, PlayerStart en el inicio y el GameMode de la demo.

El mapa es FIJO y se disena encima (obstaculos, puzles, retoques): los trozos se guardan para
editarlos. Por eso, si el mapa ya existe:
    - sin TN_REGENERATE=1 no se toca nada y el script falla avisando;
    - con TN_REGENERATE=1 solo se recargan los trozos (DA_* en sitio: mismas referencias) y se
      crean los actores que falten. Nada de lo colocado a mano en el nivel se borra.

Variables de entorno (opcionales):
    TN_VOLUME_DIR   carpeta con manifest.json (por defecto Scripts/terrain_volumes/Variants/C01_camino)
    TN_REGENERATE   1 = sobrescribir la malla de un mapa ya importado
"""

import json
import math
import os

import unreal

GRIDMAP_ROOT = "/Game/Blueprints/Gameplay/GridMap"
TERRAIN_MATERIAL_PATH = f"{GRIDMAP_ROOT}/M_GridTerrain"
FOLIAGE_MATERIAL_PATH = f"{GRIDMAP_ROOT}/M_GridJunk"
WATER_MATERIAL_PATH = f"{GRIDMAP_ROOT}/M_GridWater"
GAME_MODE_PATH = f"{GRIDMAP_ROOT}/BP_GridDemoGameMode"
PLANE_MESH_PATH = "/Engine/BasicShapes/Plane"
TILE_CLASS_PATH = "/Script/Tortunabo.TN_TerrainMeshTile"
PLANE_SIZE_UU = 100.0
PLAYER_STARTS = 4
PLAYER_START_RING_UU = 250.0
PLAYER_START_LIFT_UU = 150.0

asset_lib = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def project_dir():
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def load_or_none(path):
    """Carga por ruta de objeto completa: en el editor abierto el registro de assets puede no
    ver aun un asset escrito por el commandlet, pero la carga directa si lo encuentra."""
    if asset_lib.does_asset_exist(path):
        return asset_lib.load_asset(path)
    name = path.rsplit('/', 1)[-1]
    return unreal.load_asset(f'{path}.{name}')


def build_assets(volume_dir, manifest, root):
    """Crea o recarga EN SITIO cada DA_<trozo>: los actores del nivel siguen apuntando a el."""
    assets, meshes = {}, {}
    material = load_or_none(TERRAIN_MATERIAL_PATH)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TN_TerrainMeshAsset)
    for cell in manifest["cells"]:
        name = f"DA_{cell['name']}"
        asset = load_or_none(f"{root}/{name}") or asset_tools.create_asset(name, root, unreal.TN_TerrainMeshAsset, factory)
        path = os.path.join(volume_dir, cell["file"]).replace("\\", "/")
        if not asset.load_from_file(path):
            raise RuntimeError(f"{name}: LoadFromFile rechazo {path}")
        asset_lib.save_loaded_asset(asset)
        assets[cell["name"]] = asset
        mesh = asset.build_static_mesh(f"{root}/Meshes", f"SM_{cell['name']}", material)
        if not mesh:
            raise RuntimeError(f"{name}: BuildStaticMesh fallo")
        asset_lib.save_loaded_asset(mesh)
        meshes[cell["name"]] = mesh
    return assets, meshes


def spawn(actor_class, label, location, rotation=unreal.Rotator(0.0, 0.0, 0.0)):
    actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(actor_class, location, rotation)
    actor.set_actor_label(label)
    return actor


def configure_sun(sun):
    """Sombras suaves sobre el terreno de marching cubes: sesgo para que las caras casi de
    canto no se sombreen a si mismas a dientes, y penumbra algo mas ancha."""
    light = sun.get_component_by_class(unreal.DirectionalLightComponent)
    light.set_editor_property("shadow_bias", 0.9)
    light.set_editor_property("shadow_slope_bias", 0.9)
    light.set_editor_property("light_source_angle", 1.5)


def existing_labels():
    return {a.get_actor_label(): a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}


def build_level(manifest, meshes, level_path):
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    current = world.get_path_name().split('.')[0] if world else ''
    if current == level_path:
        pass                                          # ya abierto en el editor: se usa tal cual
    elif asset_lib.does_asset_exist(level_path) or unreal.load_asset(level_path):
        level_subsystem.load_level(level_path)       # se conserva todo lo que haya
    else:
        level_subsystem.new_level(level_path)
    have = existing_labels()

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for cell in manifest["cells"]:
        label = f"Terrain_{cell['name']}"
        tile = have.get(label)
        if tile is not None and not isinstance(tile, unreal.StaticMeshActor):
            # Tile procedural de una importacion anterior: se sustituye por el StaticMesh.
            actor_subsystem.destroy_actor(tile)
            tile = None
        if tile is None:
            x, y = cell["center_uu"]
            tile = spawn(unreal.StaticMeshActor, label, unreal.Vector(x, y, 0.0))
            tile.set_folder_path("Terrain")
        tile.static_mesh_component.set_static_mesh(meshes[cell["name"]])

    if "Sun" in have:
        configure_sun(have["Sun"])
    if "Water" in have:
        level_subsystem.save_current_level()
        return

    # Lamina de agua: el plano del motor mide PLANE_SIZE_UU; cubre el mapa entero.
    extent = manifest["grid"] * manifest["cell_uu"]
    center = (manifest["grid"] - 1) * manifest["cell_uu"] * 0.5
    water = spawn(unreal.StaticMeshActor, "Water", unreal.Vector(center, center, manifest["water_uu"]))
    component = water.static_mesh_component
    component.set_static_mesh(unreal.load_asset(PLANE_MESH_PATH))
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    component.set_cast_shadow(False)
    water_material = load_or_none(WATER_MATERIAL_PATH)
    if water_material:
        component.set_material(0, water_material)
    water.set_actor_scale3d(unreal.Vector(extent / PLANE_SIZE_UU, extent / PLANE_SIZE_UU, 1.0))

    sun = spawn(unreal.DirectionalLight, "Sun", unreal.Vector(0.0, 0.0, 5000.0), unreal.Rotator(0.0, -50.0, 35.0))
    configure_sun(sun)
    spawn(unreal.SkyAtmosphere, "SkyAtmosphere", unreal.Vector(0.0, 0.0, 0.0))
    sky = spawn(unreal.SkyLight, "SkyLight", unreal.Vector(0.0, 0.0, 5000.0))
    sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky_component.set_editor_property("real_time_capture", True)
    sky_component.set_editor_property("intensity", 2.0)

    # PlayerStart en corro alrededor del inicio de la ruta, sobre el suelo del mapa.
    sx, sy, sz = manifest["start_uu"]
    for k in range(PLAYER_STARTS):
        angle = 2.0 * math.pi * k / PLAYER_STARTS
        location = unreal.Vector(sx + PLAYER_START_RING_UU * math.cos(angle), sy + PLAYER_START_RING_UU * math.sin(angle),
                                 sz + PLAYER_START_LIFT_UU)
        spawn(unreal.PlayerStart, f"PlayerStart_{k}", location)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    game_mode = asset_lib.load_blueprint_class(GAME_MODE_PATH) if asset_lib.does_asset_exist(GAME_MODE_PATH) else None
    if game_mode:
        world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    level_subsystem.save_current_level()


def main():
    volume_dir = os.environ.get("TN_VOLUME_DIR", f"{project_dir()}/Scripts/terrain_volumes/Variants/C01_camino")
    with open(f"{volume_dir}/manifest.json", encoding="utf-8") as handle:
        manifest = json.load(handle)
    name = manifest["name"]
    level_path = f"/Game/Maps/Run/LVL_{name}"
    if asset_lib.does_asset_exist(level_path) and os.environ.get("TN_REGENERATE") != "1":
        raise RuntimeError(f"{level_path} ya existe y es un mapa fijo (se disena encima). "
                           "Para recargar su malla, TN_REGENERATE=1 (conserva lo colocado a mano).")
    assets, meshes = build_assets(volume_dir, manifest, f"/Game/Terrain/Volumes/{name}")
    build_level(manifest, meshes, level_path)
    unreal.log(f"[TerrainMesh] {len(assets)} trozos importados; nivel /Game/Maps/Run/LVL_{name} guardado")


main()
