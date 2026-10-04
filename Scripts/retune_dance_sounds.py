"""Reimporta la pista 9 de DanceSounds (ya recortada a 30 s) y fija la calidad de compresion a 40.

Se ejecuta DENTRO del editor de Unreal (headless):
    UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<repo>/Scripts/retune_dance_sounds.py
        -unattended -nosplash -nullrhi
"""

import os

import unreal

CARPETA = "/Game/Audio/DanceSounds"
CALIDAD = 40
REIMPORTAR = {"9"}

raiz = os.path.join(unreal.SystemLibrary.get_project_directory(), "Content", "Audio", "DanceSounds")

for nombre in sorted(REIMPORTAR):
    tarea = unreal.AssetImportTask()
    tarea.filename = os.path.join(raiz, nombre + ".mp3")
    tarea.destination_path = CARPETA
    tarea.destination_name = nombre
    tarea.replace_existing = True
    tarea.automated = True
    tarea.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([tarea])
    unreal.log(f"[DanceSounds] reimportada {nombre}: {tarea.imported_object_paths}")

for i in range(10):
    ruta = f"{CARPETA}/{i}"
    onda = unreal.load_asset(ruta)
    if onda is None:
        unreal.log_error(f"[DanceSounds] no existe {ruta}")
        continue
    onda.set_editor_property("compression_quality", CALIDAD)
    onda.set_editor_property("looping", True)
    unreal.EditorAssetLibrary.save_asset(ruta)
    unreal.log(f"[DanceSounds] {i}: calidad={onda.get_editor_property('compression_quality')} duracion={onda.get_editor_property('duration'):.2f}")
