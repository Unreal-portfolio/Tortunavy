"""Miniaturas de cada malla de la galería y lámina cenital (issue #312), para la página web del catálogo.

Abre /Game/Maps/Dev/LVL_GaleriaAssets (generado por galeria_assets.py) y, con un SceneCapture2D que solo ve a ese
actor, renderiza una miniatura cuadrada de THUMB_PX por cada actor de malla de la galería en <salida>/thumbs/<nombre>.jpg.
Después renderiza la galería entera desde arriba, en ortográfica, en <salida>/galeria.png. No guarda el mapa.

Uso (editor cerrado; necesita RHI, así que sin -nullrhi):
  UnrealEditor-Win64-DebugGame-Cmd.exe Tortunabo.uproject -run=pythonscript
    -script=Scripts/tools/miniaturas_assets.py -EnablePlugins=PythonScriptPlugin -AllowCommandletRendering
    -unattended -nosplash
Variables: TN_MINIATURAS_DIR (por defecto Saved/Galeria; las miniaturas no se versionan).
"""

import math
import os

import unreal

MAP_PATH = "/Game/Maps/Dev/LVL_GaleriaAssets"
THUMB_PX = 160
SHEET_MAX_PX = 8192
SHEET_MARGIN_CM = 600.0
FOV_DEG = 30.0
VIEW_DIR = unreal.Vector(0.45, 1.0, 0.7)  # desde delante (+Y, donde está el nombre), a la derecha y desde arriba
EXPOSURE_BIAS = 0.5  # exposición fija: la captura no tiene historia de autoexposición
FILL_LUX = 4.0  # luz de relleno de las miniaturas
CAPTURE_FLAGS_OFF = ("DynamicShadows", "AmbientOcclusion", "BillboardSprites")
ENVIRONMENT_LABELS = ("Atmosfera", "Cielo", "Sol", "CamaraCenital", "LuzRelleno")  # sus iconos de editor no van en la lámina
OFFSTAGE = unreal.Vector(-4000.0, -4000.0, 500.0)  # fuera de la galería, donde se crean los actores auxiliares
LOG_PREFIX = "[Miniaturas]"


def log(msg):
    unreal.log(f"{LOG_PREFIX} {msg}")


def output_dir():
    default = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Galeria")
    path = os.path.normpath(os.environ.get("TN_MINIATURAS_DIR") or default)
    os.makedirs(os.path.join(path, "thumbs"), exist_ok=True)
    return path


def make_capture(actors, world, width, height):
    capture = actors.spawn_actor_from_class(unreal.SceneCapture2D, OFFSTAGE)
    for helper in capture.get_components_by_class(unreal.PrimitiveComponent):
        helper.set_visibility(False)  # icono y malla de cámara del editor: saldrían en la lámina
    comp = capture.capture_component2d
    target = unreal.RenderingLibrary.create_render_target2d(world, width, height,
                                                            unreal.TextureRenderTargetFormat.RTF_RGBA8)
    comp.set_editor_property("texture_target", target)
    comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    comp.set_editor_property("capture_every_frame", False)
    comp.set_editor_property("capture_on_movement", False)
    comp.set_editor_property("post_process_settings", fixed_exposure(comp.get_editor_property("post_process_settings")))
    comp.set_editor_property("post_process_blend_weight", 1.0)
    # Una captura suelta no llena las sombras virtuales: con sombras, todo sale en sombra (negro).
    flags = [unreal.EngineShowFlagsSetting(show_flag_name=name, enabled=False) for name in CAPTURE_FLAGS_OFF]
    comp.set_editor_property("show_flag_settings", flags)
    return capture, comp, target


def fixed_exposure(settings):
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
    settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", EXPOSURE_BIAS)
    return settings


def write_image(target, path, image_format):
    options = unreal.ImageWriteOptions()
    options.set_editor_property("format", image_format)
    options.set_editor_property("compression_quality", 90)
    options.set_editor_property("overwrite_file", True)
    options.set_editor_property("async_", False)
    unreal.ImageWriteBlueprintLibrary.export_to_disk(target, path, options)


def gallery_mesh_actors(actors):
    found = [a for a in actors.get_all_level_actors()
             if isinstance(a, (unreal.StaticMeshActor, unreal.SkeletalMeshActor))
             and str(a.get_folder_path()).startswith("Galeria/")]
    return sorted(found, key=lambda a: a.get_actor_label())


def frame(comp, capture, actor):
    origin, extent = actor.get_actor_bounds(False)
    radius = max(extent.length(), 1.0)
    distance = radius / math.sin(math.radians(FOV_DEG * 0.5)) * 1.05
    direction = VIEW_DIR.normal()
    location = origin + direction * distance
    capture.set_actor_location(location, False, False)
    capture.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location, origin), False)
    comp.clear_show_only_components()
    comp.show_only_actor_components(actor, False)


def add_fill_light(actors):
    """Luz de relleno sin sombras desde el lado de la cámara (el mapa no se guarda): sin ella, la cara en sombra es negra."""
    rotation = unreal.MathLibrary.find_look_at_rotation(VIEW_DIR, unreal.Vector(0, 0, 0))
    fill = actors.spawn_actor_from_class(unreal.DirectionalLight, OFFSTAGE, rotation)
    comp = fill.get_component_by_class(unreal.DirectionalLightComponent)
    comp.set_editor_property("intensity", FILL_LUX)
    comp.set_editor_property("cast_shadows", False)
    comp.set_editor_property("atmosphere_sun_light", False)
    fill.set_actor_label("LuzRelleno")
    return fill


def render_thumbs(actors, world, out_dir):
    capture, comp, target = make_capture(actors, world, THUMB_PX, THUMB_PX)
    comp.set_editor_property("fov_angle", FOV_DEG)
    comp.set_editor_property("primitive_render_mode",
                             unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
    written, seen = 0, set()
    for actor in gallery_mesh_actors(actors):
        name = actor.get_actor_label()
        if name in seen:
            name = f"{name}_{actor.get_folder_path()}".replace("/", "_")
        seen.add(name)
        frame(comp, capture, actor)
        comp.capture_scene()
        path = os.path.join(out_dir, "thumbs", f"{name}.jpg")
        write_image(target, path, unreal.DesiredImageFormat.JPG)
        written += int(os.path.isfile(path))
    capture.destroy_actor()
    return written, len(seen)


def gallery_bounds(actors):
    lo = unreal.Vector(1e9, 1e9, 0)
    hi = unreal.Vector(-1e9, -1e9, 0)
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label() in ENVIRONMENT_LABELS + ("Suelo",):
            continue
        origin, extent = actor.get_actor_bounds(False)
        if isinstance(actor, unreal.TextRenderActor):  # sus límites incluyen el icono, que está en el origen
            origin, extent = actor.get_actor_location(), unreal.Vector(1, 1, 1)
        if extent.length() <= 0:
            continue
        lo = unreal.Vector(min(lo.x, origin.x - extent.x), min(lo.y, origin.y - extent.y), 0)
        hi = unreal.Vector(max(hi.x, origin.x + extent.x), max(hi.y, origin.y + extent.y), 0)
    return lo - unreal.Vector(SHEET_MARGIN_CM, SHEET_MARGIN_CM, 0), hi + unreal.Vector(SHEET_MARGIN_CM, SHEET_MARGIN_CM, 0)


def render_sheet(actors, world, out_dir):
    lo, hi = gallery_bounds(actors)
    width_cm, height_cm = hi.x - lo.x, hi.y - lo.y
    scale = SHEET_MAX_PX / max(width_cm, height_cm)
    width_px, height_px = max(int(width_cm * scale), 64), max(int(height_cm * scale), 64)
    capture, comp, target = make_capture(actors, world, width_px, height_px)
    comp.set_editor_property("projection_type", unreal.CameraProjectionMode.ORTHOGRAPHIC)
    comp.set_editor_property("ortho_width", width_cm)
    # Solo salen las piezas, los carteles y el suelo: el resto (luces, cielo, brocha del nivel) son ayudas de editor.
    comp.set_editor_property("primitive_render_mode",
                             unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
    for actor in actors.get_all_level_actors():
        if not str(actor.get_folder_path()) and actor.get_actor_label() != "Suelo":
            continue
        for part in actor.get_components_by_class(unreal.PrimitiveComponent):
            if not isinstance(part, unreal.BillboardComponent):  # los iconos de los carteles están en el origen
                comp.show_only_component(part)
    center = (lo + hi) * 0.5
    capture.set_actor_location(unreal.Vector(center.x, center.y, 50000), False, False)
    capture.set_actor_rotation(unreal.Rotator(roll=0, pitch=-90, yaw=-90), False)
    comp.capture_scene()
    path = os.path.join(out_dir, "galeria.png")
    write_image(target, path, unreal.DesiredImageFormat.PNG)
    capture.destroy_actor()
    log(f"Lámina {width_px} x {height_px} px ({width_cm / 100:.0f} x {height_cm / 100:.0f} m, "
        f"{1 / scale:.1f} cm/px) en {path}")


def main():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.load_level(MAP_PATH):
        raise RuntimeError(f"No se pudo abrir {MAP_PATH}")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    out_dir = output_dir()
    # Sin tick, las mallas que aún se están construyendo en segundo plano no llegan a dibujarse.
    unreal.SystemLibrary.execute_console_command(world, "Editor.AsyncAssetCompilationFinishAll")
    fill = add_fill_light(actors)
    written, total = render_thumbs(actors, world, out_dir)
    log(f"Miniaturas: {written} de {total} ({100.0 * written / max(total, 1):.0f} %) en {out_dir}")
    render_sheet(actors, world, out_dir)
    fill.destroy_actor()
    log("OK")


main()
