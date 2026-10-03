"""Mapa galería con todos los assets del juego (issue #312).

Importa los FBX de Art/Source y Art/Library/IA en /Game/Art/... (solo si faltan) y genera
/Game/Maps/Dev/LVL_GaleriaAssets con cada StaticMesh y SkeletalMesh de /Game, ordenados como el catálogo
(catalogo_assets.py): por origen (Humano, IA, Script-Blender, Motor o plantilla) y dentro por carpeta. Cada origen
lleva un cartel grande con sus autores y cada pieza su nombre en el suelo, delante. Las piezas enormes o diminutas se
escalan para que quepan entre MIN_PIECE_CM y MAX_PIECE_CM y la escala va en su cartel. Al final van los Blueprints de
actor con presencia en el mundo. Comprueba que hay un actor por malla del catálogo presente en la rama.

Todo está en el suelo y mirando arriba: la cámara CamaraCenital (y miniaturas_assets.py) lo leen desde arriba, con +X
hacia la derecha de la imagen y +Y hacia abajo.

Uso (editor cerrado):
  UnrealEditor-Win64-DebugGame-Cmd.exe Tortunabo.uproject -run=pythonscript
    -script=Scripts/tools/galeria_assets.py -EnablePlugins=PythonScriptPlugin -unattended -nosplash -nullrhi
Variables: TN_GALERIA_REIMPORT=1 reimporta los FBX aunque ya existan.
"""

import os
import sys
from collections import defaultdict

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import catalogo_assets as catalogo  # noqa: E402  (después de añadir Scripts/tools al path)

MAP_PATH = "/Game/Maps/Dev/LVL_GaleriaAssets"
ART_ROOTS = ("Art/Source", "Art/Library/IA")
MAX_ROW_CM = 16000.0
GROUP_DEPTH = 3  # carpetas agrupadas a este nivel bajo /Game (p. ej. Art/IA/puzzles)
MAX_PIECE_CM = 900.0
MIN_PIECE_CM = 300.0
GAP_CM = 220.0
NAME_SIZE_CM = 70.0
NAME_CHAR_CM = NAME_SIZE_CM * 0.55
FOLDER_SIZE_CM = 200.0
SECTION_SIZE_CM = 600.0
# Sol, cielo y atmósfera fuera de la galería: sus iconos de editor no tapan los carteles.
ENV_ORIGIN = unreal.Vector(-4000.0, -4000.0, 0.0)
# La galería empieza lejos del origen: los iconos de editor de los carteles se quedan en (0, 0) y saldrían en la lámina.
START_Y_CM = 3000.0
LOG_PREFIX = "[Galeria]"
# Texto tumbado en el suelo, mirando arriba, que se lee con +X a la derecha.
FLAT_TEXT = unreal.Rotator(roll=0.0, pitch=90.0, yaw=90.0)
ORIGIN_COLORS = {
    catalogo.HUMANO: unreal.Color(r=255, g=170, b=60, a=255),
    catalogo.IA: unreal.Color(r=200, g=120, b=255, a=255),
    catalogo.SCRIPT: unreal.Color(r=90, g=220, b=140, a=255),
    catalogo.MOTOR: unreal.Color(r=170, g=170, b=170, a=255),
}
WHITE = unreal.Color(r=255, g=255, b=255, a=255)
FOLDER_COLOR = unreal.Color(r=255, g=230, b=120, a=255)
# Blueprints sin malla propia o que no son objetos del mundo: modos, controladores, volúmenes y gestores.
BP_SKIP = ("GameMode", "GameInstance", "Controller", "Manager", "Zone", "Volume", "Spawner")


def log(msg):
    unreal.log(f"{LOG_PREFIX} {msg}")


def fbx_tasks():
    root = catalogo.project_dir()
    reimport = os.environ.get("TN_GALERIA_REIMPORT") == "1"
    tasks = []
    for art_root in ART_ROOTS:
        for folder, _, files in os.walk(os.path.join(root, art_root)):
            if "_pipeline" in folder:
                continue
            for name in files:
                if not name.lower().endswith(".fbx"):
                    continue
                rel = os.path.relpath(folder, root).replace("\\", "/")
                dest = "/Game/" + rel.replace("Art/Library/IA", "Art/IA").replace(" ", "_")
                asset_name = os.path.splitext(name)[0]
                if not reimport and unreal.EditorAssetLibrary.does_asset_exist(f"{dest}/{asset_name}"):
                    continue
                tasks.append(make_task(os.path.join(folder, name), dest))
    return tasks


def make_task(fbx_path, dest):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("import_materials", True)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    smd = ui.get_editor_property("static_mesh_import_data")
    smd.set_editor_property("combine_meshes", True)
    smd.set_editor_property("auto_generate_collision", False)
    smd.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    smd.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", fbx_path)
    task.set_editor_property("destination_path", dest)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", ui)
    return task


def import_art():
    tasks = fbx_tasks()
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    imported = sum(len(t.get_editor_property("imported_object_paths") or []) for t in tasks)
    log(f"FBX importados: {len(tasks)} ficheros, {imported} assets")


def group_folder(folder):
    parts = folder.split("/")[2:]
    return "/Game/" + "/".join(parts[:GROUP_DEPTH])


def gallery_sections(rows):
    """{origen: {carpeta: [filas]}} con las mallas de esta rama, en el orden del catálogo."""
    sections = {origin: defaultdict(list) for origin in catalogo.ORIGIN_ORDER}
    for row in rows:
        if row["tipo"] in catalogo.MESH_CLASSES and not row["uso"].startswith("solo en la"):
            sections[row["origen"]][group_folder(row["carpeta"])].append(row)
    return {o: dict(f) for o, f in sections.items() if f}


def spawn_label(actors, text, location, height, color, folder="Etiquetas"):
    label = actors.spawn_actor_from_class(unreal.TextRenderActor, location, FLAT_TEXT)
    comp = label.get_editor_property("text_render")
    comp.set_editor_property("text", text)
    comp.set_editor_property("world_size", height)
    comp.set_editor_property("text_render_color", color)
    comp.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_LEFT)
    comp.set_editor_property("vertical_alignment", unreal.VerticalTextAligment.EVRTA_TEXT_TOP)
    # El icono de editor de cada cartel se queda en el origen del mundo: sin textura, no tapa nada.
    for sprite in label.get_components_by_class(unreal.BillboardComponent):
        sprite.set_sprite(None)
        sprite.set_visibility(False)
    label.set_actor_label(f"Etiqueta_{text[:40]}")
    label.set_folder_path(folder)
    return label


def display_scale(size):
    biggest = max(size.x, size.y, size.z)
    if biggest > MAX_PIECE_CM:
        return MAX_PIECE_CM / biggest
    if 0 < biggest < MIN_PIECE_CM:
        return MIN_PIECE_CM / biggest
    return 1.0


def scale_text(scale):
    return "" if scale == 1.0 else f"  (escala {scale:.3g})".replace(".", ",")


def place_mesh(actors, asset, x, y_top, scale):
    size, box_min = catalogo.mesh_size(asset)
    loc = unreal.Vector(x - box_min.x * scale, y_top - box_min.y * scale, -box_min.z * scale)
    if isinstance(asset, unreal.StaticMesh):
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, loc)
        actor.static_mesh_component.set_static_mesh(asset)
    else:
        actor = actors.spawn_actor_from_class(unreal.SkeletalMeshActor, loc)
        actor.skeletal_mesh_component.set_skinned_asset_and_update(asset)
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    actor.set_actor_label(asset.get_name())
    return actor


class Cursor:
    """Posición de colocación: filas a lo largo de +X que bajan por +Y."""

    def __init__(self, y=0.0):
        self.x, self.y, self.row_depth = 0.0, y, 0.0

    def new_row(self):
        if self.x > 0:
            self.y += self.row_depth + GAP_CM
        self.x, self.row_depth = 0.0, 0.0

    def take(self, width, depth):
        if self.x > 0 and self.x + width > MAX_ROW_CM:
            self.new_row()
        x = self.x
        self.x += width + GAP_CM
        self.row_depth = max(self.row_depth, depth)
        return x, self.y

    def skip(self, depth):
        self.new_row()
        self.y += depth


def place_row(actors, cursor, row, origin_folder):
    asset = unreal.load_asset(row["ruta"])
    if asset is None:
        log(f"No se pudo cargar {row['ruta']}")
        return False
    size, _ = catalogo.mesh_size(asset)
    scale = display_scale(size)
    label = row["nombre"] + scale_text(scale)
    width = max(size.x * scale, len(label) * NAME_CHAR_CM, MIN_PIECE_CM)
    depth = size.y * scale + NAME_SIZE_CM * 2.5
    x, y = cursor.take(width, depth)
    actor = place_mesh(actors, asset, x, y, scale)
    actor.set_folder_path(f"Galeria/{origin_folder}/" + row["carpeta"].replace("/Game/", ""))
    spawn_label(actors, label, unreal.Vector(x, y + size.y * scale + NAME_SIZE_CM * 0.6, 2), NAME_SIZE_CM, WHITE)
    return True


def section_title(origin, folders):
    authors = sorted({r["autor"] for rows in folders.values() for r in rows})
    total = sum(len(rows) for rows in folders.values())
    count = f"{total} malla" if total == 1 else f"{total} mallas"
    return f"{origin.upper()}  ·  {count}  ·  {', '.join(authors)}"


def layout(actors, sections, branch_note):
    placed, cursor = 0, Cursor(START_Y_CM)
    for origin, folders in sections.items():
        cursor.new_row()
        folder_name = origin.replace(" ", "_")
        spawn_label(actors, section_title(origin, folders), unreal.Vector(0, cursor.y, 2), SECTION_SIZE_CM,
                    ORIGIN_COLORS[origin])
        cursor.skip(SECTION_SIZE_CM * 1.4)
        if origin == catalogo.HUMANO and branch_note:
            spawn_label(actors, branch_note, unreal.Vector(0, cursor.y, 2), FOLDER_SIZE_CM, ORIGIN_COLORS[origin])
            cursor.skip(FOLDER_SIZE_CM * 1.6)
        for folder, rows in folders.items():
            cursor.new_row()
            spawn_label(actors, folder.replace("/Game/", "") + f"  ({len(rows)})", unreal.Vector(0, cursor.y, 2),
                        FOLDER_SIZE_CM, FOLDER_COLOR)
            cursor.skip(FOLDER_SIZE_CM * 1.5)
            placed += sum(1 for row in rows if place_row(actors, cursor, row, folder_name))
        cursor.skip(SECTION_SIZE_CM)
        log(f"{origin}: termina en Y = {cursor.y:.0f} cm")
    cursor.new_row()
    return placed, cursor


def gather_blueprints():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "Blueprint")],
                          package_paths=["/Game"], recursive_paths=True)
    by_folder = defaultdict(list)
    for data in registry.get_assets(flt):
        path = str(data.package_path)
        native = str(data.get_tag_value("NativeParentClass") or "")
        if catalogo.is_excluded(path) or any(token in native for token in BP_SKIP):
            continue
        if not any(token in native for token in ("Actor", "Character", "Pawn", "/Script/Tortunabo.")):
            continue
        by_folder[path].append(data)
    for folder in by_folder:
        by_folder[folder].sort(key=lambda d: str(d.asset_name))
    return dict(sorted(by_folder.items()))


def spawn_blueprint(actors, data, cursor):
    cls = unreal.EditorAssetLibrary.load_blueprint_class(str(data.package_name))
    # Se crea ya en su fila: algún BP tiene componentes con posición absoluta que no se mueven con el actor.
    spawn_at = unreal.Vector(cursor.x, cursor.y, 0)
    actor = actors.spawn_actor_from_class(cls, spawn_at) if cls else None
    if actor is None:
        return None
    origin, extent = actor.get_actor_bounds(False)
    origin = origin - spawn_at
    if extent.x <= 0:
        origin, extent = unreal.Vector(0, 0, 0), unreal.Vector(100, 100, 100)
    scale = display_scale(extent * 2.0)
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    origin, extent = origin * scale, extent * scale
    size = extent * 2.0
    label = str(data.asset_name) + scale_text(scale)
    width = max(size.x, len(label) * NAME_CHAR_CM, MIN_PIECE_CM)
    x, y = cursor.take(width, size.y + NAME_SIZE_CM * 2.5)
    actor.set_actor_location(unreal.Vector(x + extent.x - origin.x, y + extent.y - origin.y, extent.z - origin.z),
                             False, False)
    actor.set_actor_label(str(data.asset_name))
    spawn_label(actors, label, unreal.Vector(x, y + size.y + NAME_SIZE_CM * 0.6, 2), NAME_SIZE_CM, WHITE)
    return actor


def layout_blueprints(actors, by_folder, cursor):
    color = unreal.Color(r=120, g=200, b=255, a=255)
    total = sum(len(v) for v in by_folder.values())
    spawn_label(actors, f"BLUEPRINTS DE ACTOR  ·  {total}  ·  fuera del catálogo", unreal.Vector(0, cursor.y, 2),
                SECTION_SIZE_CM, color)
    cursor.skip(SECTION_SIZE_CM * 1.4)
    placed = 0
    for folder, items in by_folder.items():
        cursor.new_row()
        spawn_label(actors, "BP " + folder.replace("/Game/", ""), unreal.Vector(0, cursor.y, 2), FOLDER_SIZE_CM, color)
        cursor.skip(FOLDER_SIZE_CM * 1.5)
        for data in items:
            actor = spawn_blueprint(actors, data, cursor)
            if actor is None:
                log(f"No se pudo crear {data.package_name}")
                continue
            actor.set_folder_path(folder.replace("/Game/", "GaleriaBP/"))
            placed += 1
    cursor.new_row()
    log(f"Blueprints: terminan en Y = {cursor.y:.0f} cm")
    return placed, total, cursor.y


def add_environment(actors, extent_y):
    center = unreal.Vector(MAX_ROW_CM * 0.5, extent_y * 0.5, -2)
    floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, center)
    floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
    floor.set_actor_scale3d(unreal.Vector(MAX_ROW_CM / 100 + 40, extent_y / 100 + 40, 1))
    floor.set_actor_label("Suelo")
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, ENV_ORIGIN + unreal.Vector(0, 0, 3000),
                                        unreal.Rotator(roll=0, pitch=-55, yaw=-40))
    sun.set_actor_label("Sol")
    actors.spawn_actor_from_class(unreal.SkyLight, ENV_ORIGIN + unreal.Vector(0, 0, 2000)).set_actor_label("Cielo")
    actors.spawn_actor_from_class(unreal.SkyAtmosphere, ENV_ORIGIN).set_actor_label("Atmosfera")
    height = max(MAX_ROW_CM, extent_y) * 0.9
    camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(center.x, center.y, height),
                                           unreal.Rotator(roll=0, pitch=-90, yaw=-90))
    camera.set_actor_label("CamaraCenital")


def gallery_mesh_actors(actors):
    return [a for a in actors.get_all_level_actors()
            if isinstance(a, (unreal.StaticMeshActor, unreal.SkeletalMeshActor))
            and str(a.get_folder_path()).startswith("Galeria/")]


def main():
    import_art()
    rows, mesh_total = catalogo.build_catalog()
    sections = gallery_sections(rows)
    branch_only = sum(1 for r in rows if r["uso"].startswith("solo en la"))
    branch_note = f"+ {branch_only} mallas que solo están en las ramas Arte y ArteDev: ver Art/catalogo_assets.csv"
    for origin, folders in sections.items():
        log(f"{origin}: {sum(len(v) for v in folders.values())} mallas en {len(folders)} carpetas")

    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        unreal.EditorAssetLibrary.delete_asset(MAP_PATH)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.new_level(MAP_PATH):
        raise RuntimeError(f"No se pudo crear {MAP_PATH}")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    placed, cursor = layout(actors, sections, branch_note if branch_only else "")
    cursor.skip(SECTION_SIZE_CM)
    bp_placed, bp_total, extent_y = layout_blueprints(actors, gather_blueprints(), cursor)
    add_environment(actors, extent_y)
    levels.save_current_level()

    in_level = len(gallery_mesh_actors(actors))
    log(f"Mallas del catálogo en la rama: {mesh_total}; colocadas: {placed}; actores de malla en el mapa: {in_level}")
    log(f"Blueprints: {bp_placed} de {bp_total}; tamaño de la galería: {MAX_ROW_CM:.0f} x {extent_y:.0f} cm")
    if bp_placed != bp_total:
        raise RuntimeError(f"Blueprints: {bp_total} encontrados y {bp_placed} colocados")
    if placed != mesh_total or in_level != mesh_total:
        raise RuntimeError(f"Recuento distinto: {mesh_total} mallas, {placed} colocadas, {in_level} en el mapa")
    log("OK")


main()
