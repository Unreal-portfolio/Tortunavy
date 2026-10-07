"""Crea /Game/Maps/Run/LVL_Demo01: el mapa C01 (ATN_MapVariantLoader) con partida de Run completa.

Se ejecuta dentro del editor abierto (MCP de Unreal o consola Python):

    exec(open(r"<proyecto>/Scripts/build_demo_level.py", encoding="utf-8").read())

Parte de LVL_MapVariants (cargador + luz + cielo) y le pone: BP_RunGameMode, 4 PlayerStart en la
salida, meta en la playa, medusas en los escalones, agua con el tag TN_Water y el contenido de
diseno (puntos, enemigos, objetos) a lo largo del camino. Las posiciones salen del manifest de la
variante y de Saved/c01_design_points.json (muestras del camino principal y de los lazos, en uu).
Es idempotente: borra lo que puso la vez anterior (carpeta "Demo") antes de volver a colocar.
"""

import json
import math
import os

import unreal

SOURCE_LEVEL = "/Game/Maps/Run/LVL_MapVariants"
DEMO_LEVEL = "/Game/Maps/Run/LVL_Demo01"
VARIANT = "C01_camino"
FOLDER = "Demo"
BP = "/Game/Blueprints/Gameplay"
CLASSES = {
    "game_mode": f"{BP}/GameModes/BP_RunGameMode.BP_RunGameMode_C",
    "finish": f"{BP}/Interaction/BP_FinishLineVolume.BP_FinishLineVolume_C",
    "jelly": f"{BP}/Items/BP_JellyfishActor.BP_JellyfishActor_C",
    "item_zone": f"{BP}/Items/BP_ItemSpawnZone.BP_ItemSpawnZone_C",
    "crab_zone": f"{BP}/Enemies/Crabs/BP_CrabSpawnZone.BP_CrabSpawnZone_C",
    "dropping_zone": f"{BP}/Enemies/Seagull/BP_DroppingSpawnZone.BP_DroppingSpawnZone_C",
    "banana": f"{BP}/Hazards/BP_BananaPeel.BP_BananaPeel_C",
}
PLAYER_COUNT = 4
PLAYER_SPACING_UU = 180.0
CAPSULE_LIFT_UU = 120.0

project = unreal.Paths.project_dir()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assets = unreal.EditorAssetLibrary


def load_json(relative):
    with open(os.path.join(project, relative), encoding="utf-8") as f:
        return json.load(f)


def world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def ground_z(x, y, fallback):
    """Cota del suelo bajo (x, y): traza vertical contra la colision del terreno."""
    hit = unreal.SystemLibrary.line_trace_single(
        world(), unreal.Vector(x, y, fallback + 3000.0), unreal.Vector(x, y, fallback - 3000.0),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
    if hit is None:
        return fallback
    blocking = hit.to_tuple()[0]
    return hit.to_tuple()[4].z if blocking else fallback


def spawn(kind, x, y, z, yaw=0.0, label=None, lift=0.0, snap=True, scale=None):
    cls = unreal.load_class(None, CLASSES[kind])
    if cls is None:
        raise RuntimeError(f"No existe la clase {CLASSES[kind]}")
    zz = (ground_z(x, y, z) if snap else z) + lift
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(x, y, zz), unreal.Rotator(0.0, 0.0, yaw))
    actor.set_folder_path(f"{FOLDER}/{kind}")
    if label:
        actor.set_actor_label(label)
    if scale:
        actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor


def yaw_between(a, b):
    return math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))


def prepare_level():
    # Nunca duplicate_asset de un nivel: deja su mundo cargado y el siguiente load_level revienta
    # el editor ("Old world ... not cleaned up by garbage collection"). Se abre el origen la primera
    # vez y se guarda con otro nombre al final (save_demo).
    target = DEMO_LEVEL if assets.does_asset_exist(DEMO_LEVEL) else SOURCE_LEVEL
    if world().get_outermost().get_name() != target:
        levels.load_level(target)
    for actor in actors.get_all_level_actors():
        if str(actor.get_folder_path()).startswith(FOLDER):
            actors.destroy_actor(actor)
    settings = world().get_world_settings()
    settings.set_editor_property("default_game_mode", unreal.load_class(None, CLASSES["game_mode"]))


def place_starts(start, first_point):
    """4 PlayerStart en fila transversal a la salida, mirando al camino."""
    yaw = yaw_between(start, first_point)
    side = (-math.sin(math.radians(yaw)), math.cos(math.radians(yaw)))
    starts = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
    while len(starts) < PLAYER_COUNT:
        starts.append(actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 0)))
    for index, player_start in enumerate(starts[:PLAYER_COUNT]):
        offset = (index - (PLAYER_COUNT - 1) / 2.0) * PLAYER_SPACING_UU
        x, y = start[0] + side[0] * offset, start[1] + side[1] * offset
        z = ground_z(x, y, start[2]) + CAPSULE_LIFT_UU
        player_start.root_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        player_start.set_actor_location_and_rotation(unreal.Vector(x, y, z), unreal.Rotator(0.0, 0.0, yaw), False, True)
        player_start.set_actor_label(f"PlayerStart_{index + 1}")
        player_start.tags = [unreal.Name("MapVariantStart")] if index == 0 else []


def tag_water():
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.StaticMeshActor) and "water" in actor.get_actor_label().lower():
            tags = list(actor.tags)
            if unreal.Name("TN_Water") not in tags:
                actor.tags = tags + [unreal.Name("TN_Water")]


def place_design(manifest, design):
    main = design["main"]              # [x, y, z, bioma, semiancho_m, tunel]
    loops = {entry[0]: entry for entry in design["loops"]}   # id: [id, largo, x, y, z]

    # Meta: cruza la boca de la playa, perpendicular a la llegada.
    end = manifest["end_uu"]
    spawn("finish", end[0], end[1], end[2], yaw=yaw_between(main[-2], end), label="Meta",
          lift=200.0, scale=(2.0, 20.0, 4.0))

    # Medusas al pie de los escalones (el generador las exige para subir).
    for index, (x, y, z) in enumerate(manifest.get("jellyfish_uu", [])):
        spawn("jelly", x, y, z, label=f"Medusa_Escalon_{index + 1}")

    # Premios en lo alto de los lazos largos (el desvio compensa).
    for loop_id in (1, 3, 7, 6):
        _, _, x, y, z = loops[loop_id]
        spawn("item_zone", x, y, z, lift=50.0, label=f"Objetos_Lazo{loop_id}")

    # Acantilado: un cangrejo en la plaza tras el tunel (primer encuentro, facil).
    x, y, z, *_ = main[7]
    spawn("crab_zone", x, y, z, label="Cangrejos_Plaza", scale=(4.0, 4.0, 1.0))
    # Dunas: nido de cangrejos donde el camino se abre, y pieles de platano en el estrechamiento.
    x, y, z, *_ = main[19]
    spawn("crab_zone", x, y, z, label="Cangrejos_Dunas", scale=(6.0, 6.0, 1.0))
    for n in (17, 22):
        x, y, z, *_ = main[n]
        spawn("banana", x, y, z, lift=10.0, label=f"Platano_{n}")
    # Playa: cagadas de gaviota en el sprint final.
    x, y, z, *_ = main[25]
    spawn("dropping_zone", x, y, z, lift=1500.0, snap=False, label="Cagadas_Playa", scale=(8.0, 8.0, 1.0))


def main():
    manifest = load_json(f"Scripts/terrain_volumes/Variants/{VARIANT}/manifest.json")
    design = load_json("Saved/c01_design_points.json")
    prepare_level()
    place_starts(manifest["start_uu"], design["main"][1])
    tag_water()
    place_design(manifest, design)
    if assets.does_asset_exist(DEMO_LEVEL):
        levels.save_current_level()
    else:
        unreal.EditorLoadingAndSavingUtils.save_map(world(), DEMO_LEVEL)
    unreal.log(f"[build_demo_level] {DEMO_LEVEL} listo.")


main()
