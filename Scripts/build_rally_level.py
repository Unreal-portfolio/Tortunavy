"""Crea /Game/Maps/Rally/LVL_Rally: el nivel del Rally Tortuga (ATN_RallyGameMode).

Headless, con el editor cerrado (en Git Bash, MSYS_NO_PATHCONV=1):

    UnrealEditor-Win64-DebugGame-Cmd.exe <uproject> -ExecCmds="py <ruta>/Scripts/build_rally_level.py,QUIT_EDITOR"
        -EnablePlugins=PythonScriptPlugin -nullrhi -unattended -nosplash -NoSteam

(-ExecutePythonScript cierra el editor antes de que termine el guardado.)

Contenido:
- Sol, SkyAtmosphere y SkyLight de LVL_Demo01, tal cual: se abre LVL_Demo01 y se guarda como LVL_Rally sin guardar
  nunca LVL_Demo01.
- Mar (#532): un único plano "Water" (tag TN_Water) a la cota water_uu de la variante, centrado en su rejilla y
  SEA_MARGIN_M más allá de cada borde, con M_RallySea (el azul hondo del agua cartoon de build_water_toon.py, opaco
  y sin oleaje de vértice: el lecho no se ve a través). Sustituye a la superficie de LVL_Mapa01 y al plano de
  horizonte heredados de LVL_Demo01: el cuadro azul claro era esa superficie (1,2 km en el origen) y la mancha
  blanca de Málaga salía de ella sobre el lecho del corredor; el plano de horizonte no se veía.
- Niebla (ExponentialHeightFog) con los ajustes y la posición de la de LVL_TestMap (LVL_Demo01 no tiene).
- ATN_MapVariantLoader con R01_circuito_dunas (la carrera cambia de variante con ?Variant=...), un ATN_RallyTrack y un
  PlayerStart en la salida de la variante.
- WorldSettings: GameMode override = ATN_RallyGameMode.

Idempotente: si LVL_Rally ya existe, lo abre y solo corrige lo que falte.
"""

import json
import os
import sys

import unreal

# El editor no pone la carpeta del script en sys.path: build_water (ayudantes de material) vive al lado.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_water as water  # noqa: E402

RALLY_LEVEL = "/Game/Maps/Rally/LVL_Rally"
LIGHT_SOURCE = "/Game/Maps/Run/LVL_Demo01"
FOG_SOURCE = "/Game/Maps/Run/LVL_TestMap"
VARIANT = "R01_circuito_dunas"        # #692: el Rally solo usa circuitos del generador de vueltas
PLAYER_START_LIFT_UU = 150.0
SEA_MATERIAL = "M_RallySea"
SEA_ROOT = "/Game/Maps/Rally"
SEA_MARGIN_M = 20000.0           # agua más allá de cada borde de la rejilla: hasta el horizonte desde la cámara alta
PLANE_UU = 100.0                 # /Engine/BasicShapes/Plane mide 1 m de lado

# Azul hondo del agua cartoon (build_water_toon.py) con sus líneas de brillo, opaco: translúcido, el lecho de los
# trozos del corredor (a 1,5-5 m bajo el agua) se veía a través hasta en la banda honda del plano de dos triángulos.
SEA_HLSL = """\
float3 col = float3(0.08, 0.45, 0.80);
float n = Texture2DSample(Foam, FoamSampler, P.xy / 600.0 + T * float2(0.010, 0.004)).r;
float glint = step(0.93, sin(dot(P.xy, float2(0.004, 0.0027)) + T * 0.9) * (0.6 + 0.4 * n));
return lerp(col, float3(1.0, 1.0, 1.0), 0.6 * glint);
"""

FOG_PROPERTIES = (
    "fog_density", "fog_height_falloff", "second_fog_data", "fog_inscattering_luminance",
    "sky_atmosphere_ambient_contribution_color_scale", "inscattering_color_cubemap", "inscattering_color_cubemap_angle",
    "inscattering_texture_tint", "fully_directional_inscattering_color_distance",
    "non_directional_inscattering_color_distance", "directional_inscattering_exponent",
    "directional_inscattering_start_distance", "directional_inscattering_luminance", "fog_max_opacity", "start_distance",
    "end_distance", "fog_cutoff_distance", "enable_volumetric_fog", "volumetric_fog_scattering_distribution",
    "volumetric_fog_albedo", "volumetric_fog_emissive", "volumetric_fog_extinction_scale", "volumetric_fog_distance",
    "volumetric_fog_start_distance", "volumetric_fog_near_fade_in_distance",
    "volumetric_fog_static_lighting_scattering_intensity", "override_light_colors_with_fog_inscattering_colors",
    "holdout", "render_in_main_pass", "visible_in_reflection_captures", "visible_in_real_time_sky_captures",
)

project = unreal.Paths.project_dir()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assets = unreal.EditorAssetLibrary


def log(message):
    unreal.log(f"[rally] {message}")


def world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def level_actors(cls):
    return [a for a in actors.get_all_level_actors() if isinstance(a, cls)]


def read_fog():
    """Transform y ajustes de la niebla de LVL_TestMap (solo lectura: no se guarda)."""
    levels.load_level(FOG_SOURCE)
    fogs = level_actors(unreal.ExponentialHeightFog)
    if not fogs:
        raise RuntimeError(f"{FOG_SOURCE} no tiene ExponentialHeightFog")
    fog = fogs[0]
    component = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    values = {}
    for name in FOG_PROPERTIES:
        try:
            values[name] = component.get_editor_property(name)
        except Exception:  # noqa: BLE001 - propiedad que no existe en esta versión: se informa y se sigue
            log(f"niebla: la propiedad {name} no existe en UE 5.6, se omite")
    return fog.get_actor_transform(), values


def open_target():
    """Abre LVL_Rally si existe; si no, LVL_Demo01 (que se guardará con otro nombre, nunca con el suyo)."""
    if assets.does_asset_exist(RALLY_LEVEL):
        levels.load_level(RALLY_LEVEL)
        return False
    levels.load_level(LIGHT_SOURCE)
    return True


def ensure_fog(transform, values):
    fogs = level_actors(unreal.ExponentialHeightFog)
    fog = fogs[0] if fogs else actors.spawn_actor_from_class(unreal.ExponentialHeightFog, transform.translation)
    fog.set_actor_transform(transform, False, True)
    fog.set_actor_label("ExponentialHeightFog")
    component = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    for name, value in values.items():
        component.set_editor_property(name, value)


def ensure_loader():
    loaders = level_actors(unreal.TN_MapVariantLoader)
    loader = loaders[0] if loaders else actors.spawn_actor_from_class(unreal.TN_MapVariantLoader, unreal.Vector(0, 0, 0))
    for extra in loaders[1:]:
        actors.destroy_actor(extra)
    loader.set_editor_property("variant", unreal.Name(VARIANT))
    loader.set_actor_label("MapVariantLoader")
    return loader


def ensure_track():
    tracks = level_actors(unreal.TN_RallyTrack)
    track = tracks[0] if tracks else actors.spawn_actor_from_class(unreal.TN_RallyTrack, unreal.Vector(0, 0, 0))
    for extra in tracks[1:]:
        actors.destroy_actor(extra)
    track.set_actor_label("RallyTrack")
    return track


def ensure_player_start(manifest):
    """Un PlayerStart en la salida (el GameMode sienta a cada jugador en su buggy; esto es solo el punto de vista)."""
    starts = level_actors(unreal.PlayerStart)
    for extra in starts[1:]:
        actors.destroy_actor(extra)
    x, y, z = manifest["start_uu"]
    location = unreal.Vector(x, y, z + PLAYER_START_LIFT_UU)
    rotation = unreal.Rotator(0.0, 0.0, float(manifest.get("start_yaw", 0.0)))
    start = starts[0] if starts else actors.spawn_actor_from_class(unreal.PlayerStart, location, rotation)
    start.root_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    start.set_actor_location_and_rotation(location, rotation, False, True)
    start.set_actor_label("PlayerStart")
    start.tags = [unreal.Name("MapVariantStart")]


def build_sea_material():
    """M_RallySea (se rehace entero en cada ejecución): opaco con luz, el azul hondo del agua cartoon y sus brillos."""
    path = f"{SEA_ROOT}/{SEA_MATERIAL}"
    mel = water.mel
    material = water.load_or_none(path)
    if material:
        mel.delete_all_material_expressions(material)
    else:
        material = water.asset_tools.create_asset(SEA_MATERIAL, SEA_ROOT, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    foam_texture = water.load_or_none(f"{water.WATER_ROOT}/T_WaterFoam")
    if not foam_texture:
        raise RuntimeError(f"Falta {water.WATER_ROOT}/T_WaterFoam (Scripts/build_water.py)")
    nodes = {"P": mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1400, 0),
             "T": mel.create_material_expression(material, unreal.MaterialExpressionTime, -1400, 150),
             "Foam": water.texture_object(material, "WaterFoam", foam_texture, -1400, 600)}
    sea = water.custom_node(material, SEA_HLSL, tuple(nodes), unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                            -800, 300, "MarRally")
    for pin, source in nodes.items():
        mel.connect_material_expressions(source, "", sea, pin)
    mel.connect_material_property(sea, "", unreal.MaterialProperty.MP_BASE_COLOR)
    glow = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 200)
    mel.connect_material_expressions(sea, "", glow, "A")
    glow.set_editor_property("const_b", 0.35)
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -300, 500)
    rough.set_editor_property("r", 0.35)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(material)
    water.asset_lib.save_loaded_asset(material)
    return material


def sea_transform(manifest):
    """Centro (uu), cota y escala del plano del mar: la rejilla de la variante (geo.rows x geo.cols trozos desde
    -cell/2, o grid x grid) más SEA_MARGIN_M por cada lado."""
    cell = float(manifest["cell_uu"])
    geo = manifest.get("geo", {})
    rows, cols = geo.get("rows", manifest["grid"]), geo.get("cols", manifest["grid"])
    size_x, size_y = rows * cell, cols * cell
    margin = 2.0 * SEA_MARGIN_M * 100.0
    center = unreal.Vector(-cell / 2.0 + size_x / 2.0, -cell / 2.0 + size_y / 2.0, float(manifest["water_uu"]))
    return center, unreal.Vector((size_x + margin) / PLANE_UU, (size_y + margin) / PLANE_UU, 1.0)


def ensure_sea(manifest):
    """Un único plano "Water": se quitan los demás actores de agua heredados (WaterHorizon y similares)."""
    waters = [a for a in level_actors(unreal.StaticMeshActor) if a.get_actor_label().lower().startswith("water")]
    keep = next((a for a in waters if a.get_actor_label() == "Water"), None)
    for extra in waters:
        if extra is not keep:
            log(f"mar: se quita '{extra.get_actor_label()}'")
            actors.destroy_actor(extra)
    center, scale = sea_transform(manifest)
    sea = keep or actors.spawn_actor_from_class(unreal.StaticMeshActor, center)
    sea.set_actor_label("Water")
    sea.set_folder_path("Water")
    sea.set_actor_location(center, False, False)
    sea.set_actor_rotation(unreal.Rotator(0.0, 0.0, 0.0), False)
    sea.set_actor_scale3d(scale)
    component = sea.static_mesh_component
    component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
    component.set_material(0, build_sea_material())
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    component.set_cast_shadow(False)
    if unreal.Name("TN_Water") not in sea.tags:
        sea.tags = list(sea.tags) + [unreal.Name("TN_Water")]
    log(f"mar: centro {center}, escala {scale}")


def set_game_mode():
    settings = world().get_world_settings()
    settings.set_editor_property("default_game_mode", unreal.TN_RallyGameMode.static_class())


def describe():
    for actor in actors.get_all_level_actors():
        log(f"actor {actor.get_class().get_name()} '{actor.get_actor_label()}' en {actor.get_actor_location()}")
    log(f"GameMode override: {world().get_world_settings().get_editor_property('default_game_mode')}")


def main():
    path = os.path.join(project, "Scripts", "terrain_volumes", "Variants", VARIANT, "manifest.json")
    with open(path, encoding="utf-8") as f:
        manifest = json.load(f)
    fog_transform, fog_values = read_fog()
    is_new = open_target()
    ensure_fog(fog_transform, fog_values)
    ensure_loader()
    ensure_track()
    ensure_player_start(manifest)
    ensure_sea(manifest)
    set_game_mode()
    describe()
    if is_new:
        ok = unreal.EditorLoadingAndSavingUtils.save_map(world(), RALLY_LEVEL)
    else:
        ok = levels.save_current_level()
    log(f"{RALLY_LEVEL} guardado={ok}")


main()
