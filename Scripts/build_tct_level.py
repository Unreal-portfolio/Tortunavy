"""Crea el nivel de Todos contra Todos (#651): /Game/Maps/Run/LVL_Tct.

Con el C++ ya compilado (ATN_TctArena y ATN_TctGameMode). Se puede ejecutar dentro del editor abierto:
    exec(open(r"<repo>/Scripts/build_tct_level.py", encoding="utf-8").read())
o sin ventana, con el editor cerrado (DebugGame, como los tests):
    UnrealEditor-Win64-DebugGame-Cmd.exe <uproject> -run=pythonscript -script=<repo>/Scripts/build_tct_level.py
        -EnablePlugins=PythonScriptPlugin -unattended -nosplash -nullrhi

Crea el nivel (o lo abre, si ya existe) y deja:
  - Sol, cielo, luz del cielo en tiempo real y niebla suave, como LVL_BeachRace.
  - La arena (ATN_TctArena, etiqueta «ArenaTcT») en el origen con la variante A01_diana. El GameMode la cambia con
    ?Arena=<variante> y crea el mar que sube; la malla se construye desde Scripts/terrain_volumes al cargar el nivel.
  - Un PlayerStart (el cargador lo lleva a la salida del manifest; el GameMode crea las ocho salidas de verdad al medir).
  - En World Settings, el GameMode /Script/Tortunabo.TN_TctGameMode.
Idempotente: lo que ya existe (por su etiqueta) se reutiliza.
"""

import unreal

MAP_PATH = "/Game/Maps/Run/LVL_Tct"
GAME_MODE_CLASS = "/Script/Tortunabo.TN_TctGameMode"
ARENA_LABEL = "ArenaTcT"
ARENA_VARIANT = "A01_diana"

asset_lib = unreal.EditorAssetLibrary


def require_cpp():
    if not hasattr(unreal, "TN_TctArena"):
        raise RuntimeError("[TcT] Falta ATN_TctArena: compila el C++ y vuelve a abrir el editor.")


def open_level():
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if asset_lib.does_asset_exist(MAP_PATH):
        level_subsystem.load_level(MAP_PATH)
    else:
        level_subsystem.new_level(MAP_PATH)
    return level_subsystem


def build_level():
    require_cpp()
    level_subsystem = open_level()
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    by_label = {actor.get_actor_label(): actor for actor in actor_subsystem.get_all_level_actors()}

    def ensure_actor(label, actor_class, location=unreal.Vector(0, 0, 0), rotation=unreal.Rotator(0, 0, 0)):
        """(actor, creado): el que tenga esa etiqueta o uno nuevo."""
        if label in by_label:
            return by_label[label], False
        actor = actor_subsystem.spawn_actor_from_class(actor_class, location, rotation)
        actor.set_actor_label(label)
        by_label[label] = actor
        return actor, True

    # Sol alto y algo de lado: la diana se ve entera y cada tortuga proyecta su sombra casi debajo (se ve dónde cae).
    sun, created = ensure_actor("Sol", unreal.DirectionalLight, unreal.Vector(0, 0, 30000))
    sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-65.0, yaw=35.0), False)
    if created:
        light = sun.get_component_by_class(unreal.DirectionalLightComponent)
        light.set_editor_property("atmosphere_sun_light", True)
        light.set_editor_property("intensity", 10.0)
    ensure_actor("Cielo", unreal.SkyAtmosphere)
    sky, created = ensure_actor("LuzDelCielo", unreal.SkyLight, unreal.Vector(0, 0, 30000))
    if created:
        sky.get_component_by_class(unreal.SkyLightComponent).set_editor_property("real_time_capture", True)
    fog, created = ensure_actor("Niebla", unreal.ExponentialHeightFog, unreal.Vector(0, 0, -500))
    if created:
        fog_comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        fog_comp.set_editor_property("fog_density", 0.006)
        fog_comp.set_editor_property("fog_height_falloff", 0.05)
        fog_comp.set_editor_property("start_distance", 30000.0)

    arena, _ = ensure_actor(ARENA_LABEL, unreal.TN_TctArena)
    arena.set_editor_property("variant", ARENA_VARIANT)
    ensure_actor("Salida", unreal.PlayerStart, unreal.Vector(8750, 8750, 0))

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    game_mode = unreal.load_class(None, GAME_MODE_CLASS)
    if game_mode:
        world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    else:
        unreal.log_warning(f"[TcT] No existe {GAME_MODE_CLASS}: pon el GameMode en World Settings cuando compile.")

    if not level_subsystem.save_current_level():
        raise RuntimeError(f"[TcT] No se ha podido guardar {MAP_PATH}.")
    unreal.log(f"[TcT] {MAP_PATH} listo: arena «{ARENA_LABEL}» ({ARENA_VARIANT}), luz, cielo y GameMode de Todos contra Todos.")


build_level()
