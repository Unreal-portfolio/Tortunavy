"""Crea /Game/Maps/Run/LVL_D01: el mapa D01 del boceto de diseño (#875) con partida de Run.

Mismo flujo que build_demo_level.py (LVL_Demo01 con C01): el terreno lo monta ATN_MapVariantLoader desde
Scripts/terrain_volumes/Variants/D01_boceto (lo genera gen_terrain_sketch.py). Parte de LVL_Demo01 (cargador,
luz, cielo y agua), quita su contenido de diseño (carpeta "Demo") y pone:
  - el cargador en la variante D01_boceto;
  - BP_RunGameMode;
  - 4 PlayerStart en fila en la salida, mirando al recorrido (el primero con el tag MapVariantStart);
  - la meta (BP_FinishLineVolume) en el mar, cruzando toda la orilla de la playa (bloque "meta" del manifest);
  - el agua centrada en el mapa (el D01 mide 500 x 1000 m) y el plano del horizonte más grande.

Se ejecuta dentro del editor (headless o abierto):
    UnrealEditor-Cmd.exe <uproject> -ExecCmds="py <ruta>/Scripts/build_d01_level.py" -unattended -nosplash
Nunca guarda LVL_Demo01: el nivel se guarda con otro nombre (save_map). Es idempotente: si LVL_D01 ya
existe, lo abre y rehace lo de la carpeta "D01".
"""

import json
import math
import os

import unreal

SOURCE_LEVEL = "/Game/Maps/Run/LVL_Demo01"
TARGET_LEVEL = "/Game/Maps/Run/LVL_D01"
VARIANT = "D01_boceto"
FOLDER = "D01"
SOURCE_FOLDER = "Demo"
BP = "/Game/Blueprints/Gameplay"
GAME_MODE = f"{BP}/GameModes/BP_RunGameMode.BP_RunGameMode_C"
FINISH = f"{BP}/Interaction/BP_FinishLineVolume.BP_FinishLineVolume_C"
FINISH_BOX_UU = 400.0                    # ATN_FinishLineVolume: caja de 200 uu de semilado
FINISH_HEIGHT_UU = 1600.0
PLAYER_COUNT = 4
PLAYER_SPACING_UU = 180.0
CAPSULE_LIFT_UU = 120.0
HORIZON_SCALE = 4000.0                   # plano del motor (1 m) -> 4 km
WATER_Z_UU = -400.0

project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assets = unreal.EditorAssetLibrary


def log(message):
    unreal.log(f"[build_d01_level] {message}")


def world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def load_manifest():
    path = os.path.join(project, "Scripts", "terrain_volumes", "Variants", VARIANT, "manifest.json")
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def ground_z(x, y, fallback):
    """Cota del suelo bajo (x, y): traza vertical contra la colisión del terreno."""
    hit = unreal.SystemLibrary.line_trace_single(
        world(), unreal.Vector(x, y, fallback + 3000.0), unreal.Vector(x, y, fallback - 3000.0),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
    if hit is None:
        return fallback
    values = hit.to_tuple()
    return values[4].z if values[0] else fallback


def prepare_level():
    target = TARGET_LEVEL if assets.does_asset_exist(TARGET_LEVEL) else SOURCE_LEVEL
    if world().get_outermost().get_name() != target:
        levels.load_level(target)
    for actor in actors.get_all_level_actors():
        folder = str(actor.get_folder_path())
        if folder.startswith(FOLDER) or folder.startswith(SOURCE_FOLDER):
            actors.destroy_actor(actor)
    settings = world().get_world_settings()
    settings.set_editor_property("default_game_mode", unreal.load_class(None, GAME_MODE))


def set_variant():
    loader_class = unreal.load_class(None, "/Script/Tortunabo.TN_MapVariantLoader")
    loaders = [a for a in actors.get_all_level_actors() if a.get_class() == loader_class]
    if len(loaders) != 1:
        raise RuntimeError(f"se esperaba un ATN_MapVariantLoader en el nivel y hay {len(loaders)}")
    loader = loaders[0]
    loader.set_editor_property("variant", VARIANT)
    loader.recargar()
    return loader


def place_starts(manifest):
    start = manifest["start_uu"]
    route = manifest["recorrido_uu"]
    ahead = route[min(2, len(route) - 1)]
    yaw = math.degrees(math.atan2(ahead[1] - start[1], ahead[0] - start[0]))
    side = (-math.sin(math.radians(yaw)), math.cos(math.radians(yaw)))
    starts = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
    while len(starts) < PLAYER_COUNT:
        starts.append(actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 0)))
    for extra in starts[PLAYER_COUNT:]:
        actors.destroy_actor(extra)
    for index, player_start in enumerate(starts[:PLAYER_COUNT]):
        offset = (index - (PLAYER_COUNT - 1) / 2.0) * PLAYER_SPACING_UU
        x, y = start[0] + side[0] * offset, start[1] + side[1] * offset
        z = ground_z(x, y, start[2]) + CAPSULE_LIFT_UU
        player_start.root_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        player_start.set_actor_location_and_rotation(unreal.Vector(x, y, z), unreal.Rotator(0.0, 0.0, yaw), False, True)
        player_start.set_actor_label(f"PlayerStart_{index + 1}")
        player_start.tags = [unreal.Name("MapVariantStart")] if index == 0 else []


def place_finish(manifest):
    meta = manifest["meta"]
    x, y, z = meta["center_uu"]
    cls = unreal.load_class(None, FINISH)
    if cls is None:
        raise RuntimeError(f"No existe la clase {FINISH}")
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(x, y, z + FINISH_HEIGHT_UU / 2.0 - 200.0),
                                          unreal.Rotator(0.0, 0.0, meta["yaw_deg"]))
    actor.set_folder_path(f"{FOLDER}/Meta")
    actor.set_actor_label("Meta_Mar")
    actor.set_actor_scale3d(unreal.Vector(meta["depth_m"] * 100.0 / FINISH_BOX_UU,
                                          meta["width_m"] * 100.0 / FINISH_BOX_UU,
                                          FINISH_HEIGHT_UU / FINISH_BOX_UU))


def place_water(manifest):
    """Agua centrada en el mapa (rejilla de rows x cols trozos) y horizonte más grande."""
    cell = manifest["cell_uu"]
    cx = (manifest["rows"] - 1) * cell / 2.0
    cy = (manifest["cols"] - 1) * cell / 2.0
    for actor in actors.get_all_level_actors():
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        label = actor.get_actor_label().lower()
        if "water" not in label:
            continue
        location = actor.get_actor_location()
        actor.set_actor_location(unreal.Vector(cx, cy, location.z), False, False)
        if "horizon" in label:
            actor.set_actor_scale3d(unreal.Vector(HORIZON_SCALE, HORIZON_SCALE, 1.0))
        tags = list(actor.tags)
        if unreal.Name("TN_Water") not in tags:
            actor.tags = tags + [unreal.Name("TN_Water")]


def main():
    manifest = load_manifest()
    prepare_level()
    set_variant()
    place_water(manifest)
    place_starts(manifest)
    place_finish(manifest)
    if world().get_outermost().get_name() == TARGET_LEVEL:
        saved = levels.save_current_level()
    else:
        saved = unreal.EditorLoadingAndSavingUtils.save_map(world(), TARGET_LEVEL)
    if not saved:
        raise RuntimeError(f"no se pudo guardar {TARGET_LEVEL} (¿abierto en otro editor?)")
    log(f"{TARGET_LEVEL} listo con {VARIANT}.")


main()
