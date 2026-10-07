"""Economía de puntos (#873): crea o actualiza DA_PointsEconomy y rehace las filas de DT_Skins con su rareza y su precio.

Se ejecuta con el editor cerrado (o dentro de él):
    UnrealEditor-Win64-DebugGame-Cmd.exe <Tortunabo.uproject> -run=pythonscript -script=<repo>/Scripts/build_points_economy.py
        -EnablePlugins=PythonScriptPlugin -unattended -nosplash -nullrhi

  /Game/Blueprints/Gameplay/Economy/DA_PointsEconomy (UTN_PointsEconomy): fórmula de los puntos de final de partida,
    tiempo objetivo por nivel y caja sorpresa. Los valores son los de la decisión del director del 07-10 (los de serie de
    la clase); si el asset ya existe, se conservan los que haya cambiado alguien en el editor.
  /Game/Blueprints/Gameplay/Cosmetics/DT_Skins: filas de Scripts/cosmetics_skins.py (no toca DT_Helmets).
"""

import os
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts")
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import build_cosmetics as BC  # noqa: E402

ECONOMY_FOLDER = "/Game/Blueprints/Gameplay/Economy"
ECONOMY_NAME = "DA_PointsEconomy"
ECONOMY_PATH = f"{ECONOMY_FOLDER}/{ECONOMY_NAME}"

# Tiempo objetivo (segundos) de cada nivel; los que no estén usan DefaultTargetSeconds (600).
LEVEL_TARGET_SECONDS = {"LVL_Demo01": 600.0}


def ensure_economy_asset():
    """Crea DA_PointsEconomy si no existe y le pone los tiempos objetivo que falten."""
    lib = unreal.EditorAssetLibrary
    if lib.does_asset_exist(ECONOMY_PATH):
        asset = lib.load_asset(ECONOMY_PATH)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.TN_PointsEconomy)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            ECONOMY_NAME, ECONOMY_FOLDER, unreal.TN_PointsEconomy, factory)
    if asset is None:
        unreal.log_error(f"[Economía] No se ha podido crear {ECONOMY_PATH}")
        return False
    targets = asset.get_editor_property("level_target_seconds")
    for level, seconds in LEVEL_TARGET_SECONDS.items():
        if level not in targets:
            targets[level] = seconds
    asset.set_editor_property("level_target_seconds", targets)
    lib.save_loaded_asset(asset, only_if_is_dirty=False)
    box = asset.get_editor_property("mystery_box")
    score = asset.get_editor_property("score")
    unreal.log(f"[Economía] {ECONOMY_PATH}: caja {box.get_editor_property('box_price')} puntos, "
               f"meta {score.get_editor_property('finish_points')}, objetivos {dict(targets)}")
    return True


def main():
    if not BC.tables_ready():
        unreal.log_error("[Economía] Falta compilar el C++ con la columna Rarity de FTN_SkinData: DT_Skins sin tocar.")
        return
    ensure_economy_asset()
    BC.fill_tables(("skins",))
    unreal.log("[Economía] Listo.")


if __name__ == "__main__":
    main()
