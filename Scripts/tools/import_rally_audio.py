"""Importa los SFX del modo Rally como SoundWave en /Game/Audio/Rally/.

Lee Art/Source/Audio/Rally/manifest.json (lo genera gen_rally_sfx.py), importa
cada WAV reemplazando el asset si ya existe, marca looping=True en los bucles
y guarda los assets. Es idempotente: se puede volver a lanzar tras regenerar.

Ejecución sin abrir el editor (con el editor cerrado):
    UnrealEditor-Win64-DebugGame-Cmd.exe "<ruta>/Tortunabo.uproject" ^
        -run=pythonscript -script="<ruta>/Scripts/tools/import_rally_audio.py" -unattended -nosplash

También vale desde la consola Python del editor: py "Scripts/tools/import_rally_audio.py".
"""
import json
import os

import unreal

DEST_ROOT = "/Game/Audio/Rally"
ATTENUATION_NAME = "ATT_Rally"
# Atenuación compartida: el motor, los disparos y los impactos de los demás buggies se oyen según la distancia. Los
# pitidos del semáforo son de interfaz (PlaySound2D) y van sin ella.
ATTENUATION_INNER_RADIUS_CM = 600.0
ATTENUATION_FALLOFF_CM = 6000.0
# Interfaz sin atenuación: el semáforo (PlaySound2D) y las señales del copiloto, que espacializa UTN_RallyCopilotComponent
# sin atenuar para que suenen al lado de la curva.
UI_PREFIXES = ("SFX_Rally_Light_", "SFX_Rally_Call_")
# TN_RALLY_AUDIO_ONLY="SFX_Rally_Call_,..." importa solo los sonidos con esos prefijos (los demás .uasset no se tocan).
ONLY_ENV = "TN_RALLY_AUDIO_ONLY"
SOURCE_DIR = os.path.join(unreal.Paths.project_dir(), "Art", "Source", "Audio", "Rally")
MANIFEST_PATH = os.path.join(SOURCE_DIR, "manifest.json")


def load_manifest():
    if not os.path.isfile(MANIFEST_PATH):
        raise RuntimeError(
            f"Falta {MANIFEST_PATH}: genera los WAV con Art/Source/Audio/Rally/gen_rally_sfx.py")
    with open(MANIFEST_PATH, encoding="utf-8") as handle:
        return json.load(handle)["sounds"]


def import_wav(asset_tools, entry):
    source = os.path.join(SOURCE_DIR, entry["file"].replace("/", os.sep))
    if not os.path.isfile(source):
        raise RuntimeError(f"Falta el WAV {source}")
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source)
    task.set_editor_property("destination_path", DEST_ROOT)
    task.set_editor_property("destination_name", entry["name"])
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    asset_tools.import_asset_tasks([task])
    sound = unreal.EditorAssetLibrary.load_asset(f"{DEST_ROOT}/{entry['name']}")
    if not isinstance(sound, unreal.SoundWave):
        raise RuntimeError(f"La importación de {entry['name']} no produjo un SoundWave")
    return sound


def ensure_attenuation(asset_tools):
    path = f"{DEST_ROOT}/{ATTENUATION_NAME}"
    attenuation = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if attenuation is None:
        attenuation = asset_tools.create_asset(ATTENUATION_NAME, DEST_ROOT, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    if not isinstance(attenuation, unreal.SoundAttenuation):
        raise RuntimeError(f"No se pudo crear {path}")
    settings = attenuation.get_editor_property("attenuation")
    settings.set_editor_property("attenuate", True)
    settings.set_editor_property("spatialize", True)
    settings.set_editor_property("attenuation_shape", unreal.AttenuationShape.SPHERE)
    settings.set_editor_property("attenuation_shape_extents", unreal.Vector(ATTENUATION_INNER_RADIUS_CM, 0.0, 0.0))
    settings.set_editor_property("falloff_distance", ATTENUATION_FALLOFF_CM)
    attenuation.set_editor_property("attenuation", settings)
    if not unreal.EditorAssetLibrary.save_loaded_asset(attenuation, only_if_is_dirty=False):
        raise RuntimeError(f"No se pudo guardar {path}")
    return attenuation


def main():
    entries = load_manifest()
    only = tuple(p for p in os.environ.get(ONLY_ENV, "").split(",") if p)
    if only:
        entries = [entry for entry in entries if entry["name"].startswith(only)]
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    attenuation = ensure_attenuation(asset_tools)
    failures = []
    for entry in entries:
        try:
            sound = import_wav(asset_tools, entry)
            sound.set_editor_property("looping", bool(entry["loop"]))
            is_ui = entry["name"].startswith(UI_PREFIXES)
            sound.set_editor_property("attenuation_settings", None if is_ui else attenuation)
            if not unreal.EditorAssetLibrary.save_loaded_asset(sound, only_if_is_dirty=False):
                raise RuntimeError("no se pudo guardar")
            unreal.log(f"[import_rally_audio] {entry['name']} importado (looping={entry['loop']})")
        except RuntimeError as error:
            failures.append(f"{entry['name']}: {error}")
            unreal.log_error(f"[import_rally_audio] {entry['name']}: {error}")
    unreal.log(f"[import_rally_audio] {len(entries) - len(failures)}/{len(entries)} SoundWave en {DEST_ROOT}")
    if failures:
        raise RuntimeError("Importación incompleta:\n" + "\n".join(failures))


main()
