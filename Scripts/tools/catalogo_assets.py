"""Catálogo de assets del juego en CSV (issues #126, #287 y #312).

Escribe Art/catalogo_assets.csv con una fila por paquete de Content/ (todo /Game salvo /Game/_Deprecado): mallas,
sonidos, Blueprints, widgets, mapas, materiales y sus instancias, texturas, animaciones, Niagara, datos, entradas y
redirectores. Columnas: nombre, ruta, tipo (clase), categoría, estado y su motivo, triángulos y caja en cm (mallas),
duración (sonidos y animaciones), carpeta, origen, autor, uso y fuente en Art/. Añade las mallas que solo existen en
las ramas de arte (origin/Arte de María y origin/ArteDev de Mokius), leídas de git sin copiar los binarios. La galería
(galeria_assets.py) importa este módulo para ordenar las piezas igual.

- Categoría: la de la clase en CATEGORIES (las clases de datos propias, /Script/Tortunabo, son «Datos»); una clase
  sin categoría detiene el script.
- Estado (estado_de, gana la primera regla que encaja):
    1. deprecado: redirector (se quita con Fix Up Redirectors), carpeta OLD o sin usar con la limpieza (#31)
       pendiente (catalogo_pendientes.CLEANUP_ISSUE);
    2. placeholder: contenido provisional que se sustituirá (PLACEHOLDER_RULES: tortuga de demo, greybox de
       cuadrícula, del Modeling Mode y del blockout) y todo lo de origen «Motor o plantilla» (plantillas de UE,
       Mixamo, packs);
    3. final: el resto.
- Origen: lo decide la primera regla de ORIGIN_RULES cuyo prefijo encaja; un asset sin regla detiene el script.
- Autor: autor del primer commit que añadió el .uasset o .umap (siguiendo los renombrados); «solo local» si no está
  versionado.
- Uso: referencias desde mapas y Blueprints según el AssetRegistry; si no hay, las indirectas (DataTable, DataAsset)
  y las rutas escritas en Source/ o Config/ (un mapa cuenta también por su nombre corto); «sin usar» si no hay
  ninguna.
- Nota: la de la regla de origen y, si está sin usar, «Pendiente: #N» con la issue que lo aplicará
  (catalogo_pendientes.PENDING, #287). Avisa de los assets sin usar que no tienen issue.
- Comprueba que hay una fila por cada .uasset y .umap de Content/ fuera de _Deprecado.

Uso (editor cerrado):
  UnrealEditor-Win64-DebugGame-Cmd.exe Tortunabo.uproject -run=pythonscript
    -script=Scripts/tools/catalogo_assets.py -EnablePlugins=PythonScriptPlugin -unattended -nosplash -nullrhi -nosound
"""

import csv
import os
import re
import subprocess
import sys
from collections import Counter

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import catalogo_pendientes as pendientes  # noqa: E402  (después de añadir Scripts/tools al path)
from catalogo_reglas import (  # noqa: E402,F401  (galeria_assets.py usa los orígenes a través de este módulo)
    DEPRECADO, ESTADO_ORDER, FINAL, HUMANO, IA, MOTOR, ORIGIN_ORDER, ORIGIN_RULES, PLACEHOLDER, PLACEHOLDER_RULES,
    SCRIPT, estado_de, origin_of,
)

CSV_REL = "Art/catalogo_assets.csv"
LOG_PREFIX = "[Catalogo]"
MESH_CLASSES = ("StaticMesh", "SkeletalMesh")
SOUND_CLASSES = ("SoundWave", "SoundCue")
# Fuera de la galería (galeria_assets.py): lo deprecado, lo de desarrollo y la propia galería.
EXCLUDED = ("/_Deprecado", "/Deprecado", "/Developers", "/OLD/", "/Game/Maps/Dev/")
# Fuera del catálogo: solo lo ya deprecado (#126: «todo Content/ salvo /Game/_Deprecado»).
CATALOG_EXCLUDED = ("/Game/_Deprecado/",)
# Referencias que no cuentan como uso: la propia galería y lo deprecado.
IGNORED_REFERENCERS = ("/Game/Maps/Dev/LVL_GaleriaAssets", "/Game/_Deprecado", "/Deprecado/")
ART_BRANCHES = (("origin/Arte", "Arte"), ("origin/ArteDev", "ArteDev"))
ART_SOURCE_EXT = (".fbx", ".wav", ".blend")
PACKAGE_EXT = (".uasset", ".umap")

# Categoría de cada clase (asset_class_path.asset_name). Las clases de /Script/Tortunabo son datos (DataAsset propios).
CATEGORIES = {
    "StaticMesh": "Malla", "SkeletalMesh": "Malla",
    "Skeleton": "Esqueleto y física", "PhysicsAsset": "Esqueleto y física",
    "SoundWave": "Sonido", "SoundCue": "Sonido", "SoundAttenuation": "Sonido", "SoundClass": "Sonido",
    "SoundMix": "Sonido", "SoundConcurrency": "Sonido", "MetaSoundSource": "Sonido",
    "Blueprint": "Blueprint",
    "WidgetBlueprint": "Widget",
    "World": "Mapa",
    "Material": "Material", "MaterialFunction": "Material", "MaterialParameterCollection": "Material",
    "MaterialInstanceConstant": "Instancia de material",
    "Texture2D": "Textura", "TextureCube": "Textura", "TextureRenderTarget2D": "Textura",
    "AnimSequence": "Animación", "AnimMontage": "Animación", "BlendSpace": "Animación", "BlendSpace1D": "Animación",
    "AnimBlueprint": "Animación", "ControlRigBlueprint": "Animación",
    "NiagaraSystem": "Niagara", "NiagaraEmitter": "Niagara",
    "DataTable": "Datos", "CurveFloat": "Datos", "UserDefinedStruct": "Datos", "UserDefinedEnum": "Datos",
    "InputAction": "Entrada", "InputMappingContext": "Entrada",
    "Font": "Fuente", "FontFace": "Fuente",
    "ObjectRedirector": "Redirector",
}
OWN_CLASSES_PACKAGE = "/Script/Tortunabo"
OWN_CLASSES_CATEGORY = "Datos"

AUTHOR_NAMES = {
    "Rodrigo Fernandez": "SkiTemplar (Rodrigo)",
    "Rodrigo Fernández": "SkiTemplar (Rodrigo)",
    "SkiTemplar": "SkiTemplar (Rodrigo)",
    "María Calzado del Valle": "María Calzado",
}
LOCAL_ONLY = "solo local"
FIELDS = ("nombre", "ruta", "tipo", "categoria", "estado", "motivo_estado", "triangulos", "caja_cm", "duracion_s",
          "carpeta", "origen", "autor", "uso", "refs_mapas", "refs_bp", "refs_otros", "refs_codigo", "fuente", "nota")


def log(msg):
    unreal.log(f"{LOG_PREFIX} {msg}")


def project_dir():
    return os.path.normpath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))


def git(*args, binary=False):
    cmd = ["git", "-c", "core.quotepath=false", *args]
    res = subprocess.run(cmd, cwd=project_dir(), capture_output=True)
    if res.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)}: {res.stderr.decode('utf-8', 'replace').strip()}")
    return res.stdout if binary else res.stdout.decode("utf-8", "replace")


def is_excluded(path):
    """Fuera de la galería (galeria_assets.py)."""
    return not path.startswith("/Game/") or any(token in path + "/" for token in EXCLUDED)


def in_catalog(package_path):
    """Carpeta /Game que entra en el catálogo: todo menos /Game/_Deprecado."""
    return package_path.startswith("/Game/") and not any(token in package_path + "/" for token in CATALOG_EXCLUDED)


def category_of(class_package, class_name):
    if class_name in CATEGORIES:
        return CATEGORIES[class_name]
    if class_package == OWN_CLASSES_PACKAGE:
        return OWN_CLASSES_CATEGORY
    raise RuntimeError(f"La clase {class_package}.{class_name} no tiene categoría: añádela a CATEGORIES")


def first_authors(ref):
    """Ruta de cada fichero de Content/ en `ref` -> autor del commit que lo añadió, siguiendo los renombrados."""
    out = git("-c", "diff.renameLimit=50000", "log", ref, "--reverse", "-M", "--name-status", "--format=@@%an",
              "--", "Content")
    authors, current = {}, None
    for line in out.splitlines():
        if line.startswith("@@"):
            current = line[2:]
            continue
        parts = line.split("\t")
        if len(parts) == 2 and parts[0] == "A":
            authors.setdefault(parts[1], current)
        elif len(parts) == 2 and parts[0] == "D":
            authors.pop(parts[1], None)  # si otro lo vuelve a añadir en la misma ruta, el autor es ese
        elif len(parts) == 3 and parts[0][:1] in "RC":
            authors[parts[2]] = authors.get(parts[1], current)
    return {path: AUTHOR_NAMES.get(name, name) for path, name in authors.items()}


def package_to_file(package):
    """Fichero de Content/ del paquete: el .umap si es un mapa que existe en disco, si no el .uasset."""
    stem = "Content/" + package[len("/Game/"):]
    return stem + ".umap" if os.path.isfile(os.path.join(project_dir(), stem + ".umap")) else stem + ".uasset"


def file_to_package(path):
    return "/Game/" + os.path.splitext(path[len("Content/"):])[0]


def disk_packages():
    """Paquetes /Game de los .uasset y .umap de Content/ que entran en el catálogo."""
    root = os.path.join(project_dir(), "Content")
    found = set()
    for folder, _, files in os.walk(root):
        for name in files:
            if name.endswith(PACKAGE_EXT):
                rel = "Content/" + os.path.relpath(os.path.join(folder, name), root).replace("\\", "/")
                package = file_to_package(rel)
                if in_catalog(package.rsplit("/", 1)[0]):
                    found.add(package)
    return found


def art_sources():
    """Nombre (en minúsculas) -> fichero de Art/ con ese nombre (fbx, wav o blend; el primero en ART_SOURCE_EXT)."""
    root = project_dir()
    found = {}
    for folder, _, files in os.walk(os.path.join(root, "Art")):
        for name in files:
            stem, ext = os.path.splitext(name)
            ext = ext.lower()
            if ext not in ART_SOURCE_EXT:
                continue
            rel = os.path.relpath(os.path.join(folder, name), root).replace("\\", "/")
            key = stem.lower()
            if key not in found or ART_SOURCE_EXT.index(ext) < ART_SOURCE_EXT.index(os.path.splitext(found[key])[1].lower()):
                found[key] = rel
    return found


def code_corpus():
    root = project_dir()
    chunks = []
    for base, exts in (("Source", (".h", ".cpp", ".inl", ".cs")), ("Config", (".ini",))):
        for folder, _, files in os.walk(os.path.join(root, base)):
            for name in files:
                if name.endswith(exts):
                    with open(os.path.join(folder, name), encoding="utf-8", errors="replace") as fh:
                        chunks.append(fh.read())
    return "\n".join(chunks)


def class_filter(class_names, recursive=False):
    paths = [unreal.TopLevelAssetPath("/Script/Engine", cls) for cls in class_names]
    return unreal.ARFilter(class_paths=paths, package_paths=["/Game"], recursive_paths=True, recursive_classes=recursive)


def referencer_kinds(registry):
    kinds = {}
    for data in registry.get_assets(class_filter(("Blueprint",), recursive=True)):
        kinds[str(data.package_name)] = "bp"
    for data in registry.get_assets(class_filter(("World",))):
        kinds[str(data.package_name)] = "mapa"
    # El esqueleto y el PhysicsAsset de una malla la referencian, pero no son un uso.
    for data in registry.get_assets(class_filter(("PhysicsAsset", "Skeleton"))):
        kinds[str(data.package_name)] = "companero"
    return kinds


def direct_referencers(ctx, package):
    opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
                                                 include_hard_package_references=True,
                                                 include_searchable_names=False,
                                                 include_soft_management_references=False,
                                                 include_hard_management_references=False)
    refs = [str(r) for r in ctx["registry"].get_referencers(package, opts) or []]
    return [r for r in refs if r != package and not any(token in r for token in IGNORED_REFERENCERS)
            and ctx["kinds"].get(r) != "companero"]


def in_code(ctx, package):
    """La ruta del paquete escrita en Source/ o Config/; un mapa cuenta también por su nombre corto (open LVL_X)."""
    if re.search(re.escape(package) + r"(?![A-Za-z0-9_])", ctx["corpus"]) is not None:
        return True
    if ctx["kinds"].get(package) == "mapa":
        short = package.rsplit("/", 1)[-1]
        return re.search(r"(?<![A-Za-z0-9_])" + re.escape(short) + r"(?![A-Za-z0-9_])", ctx["corpus"]) is not None
    return False


def is_used(ctx, package, depth=0):
    """Cierto si un mapa, un Blueprint o el código llegan al paquete, directamente o a través de otros assets."""
    memo = ctx["used"]
    if package in memo:
        return memo[package]
    memo[package] = False
    used = in_code(ctx, package)
    for ref in direct_referencers(ctx, package):
        if used:
            break
        used = ctx["kinds"].get(ref) in ("mapa", "bp") or (depth < 8 and is_used(ctx, ref, depth + 1))
    memo[package] = used
    return used


def usage(ctx, package):
    refs = direct_referencers(ctx, package)
    maps = [r for r in refs if ctx["kinds"].get(r) == "mapa"]
    bps = [r for r in refs if ctx["kinds"].get(r) == "bp"]
    others = [r for r in refs if r not in maps and r not in bps]
    live = [r for r in others if is_used(ctx, r)]
    return maps, bps, others, live, in_code(ctx, package)


def usage_text(maps, bps, live_others, has_code):
    code = " + código" if has_code else ""
    if maps or bps:
        return f"{len(maps) + len(bps)} ({len(maps)} mapas, {len(bps)} BP){code}"
    if live_others:
        names = ", ".join(sorted(r.rsplit("/", 1)[-1] for r in live_others)[:3])
        return f"indirecto: {names}{code}"
    return "código C++" if has_code else "sin usar"


def fmt_size(size):
    return "x".join(str(int(round(v))) for v in (size.x, size.y, size.z))


def mesh_size(asset):
    """Tamaño de la caja (cm) y su esquina mínima, para StaticMesh y SkeletalMesh."""
    if isinstance(asset, unreal.StaticMesh):
        box = asset.get_bounding_box()
        return box.max - box.min, box.min
    bounds = asset.get_bounds()
    ext, origin = bounds.box_extent, bounds.origin
    return ext * 2.0, origin - ext


def duration_tag(data, cls):
    """Duración en segundos (sonidos y secuencias de animación), con dos decimales; vacía si no tiene."""
    tag = "SequenceLength" if cls == "AnimSequence" else "Duration"
    value = data.get_tag_value(tag)
    try:
        return f"{float(value):.2f}" if value else ""
    except ValueError:
        return ""


def asset_row(data, ctx):
    package = str(data.package_name)
    cls = str(data.asset_class_path.asset_name)
    origin, rule_source, note = origin_of(package)
    maps, bps, others, live, has_code = usage(ctx, package)
    name = package.rsplit("/", 1)[-1]
    row = {
        "nombre": name, "ruta": package, "tipo": cls,
        "categoria": category_of(str(data.asset_class_path.package_name), cls), "estado": "", "motivo_estado": "",
        "triangulos": "", "caja_cm": "", "duracion_s": "",
        "carpeta": str(data.package_path), "origen": origin,
        "autor": ctx["authors"].get(package_to_file(package), LOCAL_ONLY),
        "uso": usage_text(maps, bps, live, has_code), "refs_mapas": len(maps), "refs_bp": len(bps),
        "refs_otros": len(others), "refs_codigo": int(has_code),
        "fuente": ctx["sources"].get(name.lower(), rule_source), "nota": note,
    }
    if cls in MESH_CLASSES:
        row["triangulos"] = str(data.get_tag_value("Triangles") or "")
        asset = data.get_asset()
        if asset is not None:
            row["caja_cm"] = fmt_size(mesh_size(asset)[0])
    elif cls in SOUND_CLASSES or cls == "AnimSequence":
        row["duracion_s"] = duration_tag(data, cls)
    return row


def gather_assets(registry, class_names):
    found = [d for d in registry.get_assets(class_filter(class_names)) if in_catalog(str(d.package_path))]
    return sorted(found, key=lambda d: str(d.package_name))


def gather_packages(registry):
    """Un asset por paquete de /Game que entra en el catálogo: el que se llama como el paquete (un redirector de
    Blueprint guarda también sus _C y Default__) o, si no hay, el primero por nombre."""
    flt = unreal.ARFilter(package_paths=["/Game"], recursive_paths=True)
    by_package = {}
    for data in registry.get_assets(flt):
        if not in_catalog(str(data.package_path)):
            continue
        by_package.setdefault(str(data.package_name), []).append(data)
    chosen = []
    for package, datas in by_package.items():
        short = package.rsplit("/", 1)[-1]
        named = [d for d in datas if str(d.asset_name) == short]
        chosen.append(named[0] if named else sorted(datas, key=lambda d: str(d.asset_name))[0])
    return sorted(chosen, key=lambda d: str(d.package_name))


def blob_tags(blob):
    """Lee la clase y las etiquetas Triangles/ApproxSize del AssetRegistry guardado en la cabecera del .uasset."""
    head = blob[:262144]
    match = re.search(rb"/Script/Engine\.(StaticMesh|SkeletalMesh)\b", head)
    if not match:
        return None
    tokens = [t.decode("ascii") for t in re.findall(rb"[\x20-\x7e]{1,}", head)]
    tags = {}
    for i, token in enumerate(tokens[:-1]):
        if token in ("Triangles", "ApproxSize") and token not in tags:
            tags[token] = tokens[i + 1]
    tris = tags.get("Triangles", "")
    size = tags.get("ApproxSize", "")
    return {"tipo": match.group(1).decode(), "triangulos": tris if tris.isdigit() else "",
            "caja_cm": size if re.fullmatch(r"\d+x\d+x\d+", size) else ""}


def local_stems():
    stems = set()
    for _, _, files in os.walk(os.path.join(project_dir(), "Content")):
        stems.update(os.path.splitext(f)[0].lower() for f in files if f.endswith((".uasset", ".umap")))
    return stems


def branch_candidates(ref, known, head_history):
    try:
        files = git("ls-tree", "-r", "--name-only", ref, "--", "Content").splitlines()
    except RuntimeError as err:
        log(f"Rama {ref} no disponible: {err}")
        return []
    return [f for f in files if f.endswith(".uasset") and not is_excluded(file_to_package(f))
            and f not in head_history and os.path.splitext(os.path.basename(f))[0].lower() not in known]


def branch_rows(ctx):
    """Mallas que solo existen en las ramas de arte: nunca estuvieron en la historia de esta rama ni en local."""
    known = local_stems() | {os.path.splitext(os.path.basename(f))[0].lower()
                             for f in git("ls-tree", "-r", "--name-only", "HEAD", "--", "Content").splitlines()}
    rows = {}
    for ref, label in ART_BRANCHES:
        candidates = branch_candidates(ref, known, ctx["authors"])
        authors = first_authors(ref) if candidates else {}
        for path in candidates:
            if path in rows:
                rows[path]["ramas"].append(label)
                continue
            tags = blob_tags(git("cat-file", "blob", f"{ref}:{path}", binary=True))
            if tags is None:
                continue
            package = file_to_package(path)
            origin, rule_source, note = origin_of(package)
            name = package.rsplit("/", 1)[-1]
            rows[path] = {"nombre": name, "ruta": package, **tags, "categoria": CATEGORIES[tags["tipo"]],
                          "estado": "", "motivo_estado": "", "duracion_s": "",
                          "carpeta": package.rsplit("/", 1)[0], "origen": origin,
                          "autor": authors.get(path, LOCAL_ONLY), "refs_mapas": "", "refs_bp": "", "refs_otros": "",
                          "refs_codigo": "", "fuente": ctx["sources"].get(name.lower(), rule_source), "nota": note,
                          "ramas": [label]}
    result = []
    for row in rows.values():
        ramas = row.pop("ramas")
        row["uso"] = "solo en la rama " + ramas[0] if len(ramas) == 1 else "solo en las ramas " + " y ".join(ramas)
        result.append(row)
    return result


def sort_key(row):
    return ORIGIN_ORDER.index(row["origen"]), row["carpeta"].lower(), row["nombre"].lower()


def with_estado(row):
    estado, reason = estado_de(row)
    return {**row, "estado": estado, "motivo_estado": reason}


def build_catalog():
    """Filas del catálogo (esta rama y ramas de arte) y nº de mallas de esta rama según el AssetRegistry."""
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    ctx = {"registry": registry, "kinds": referencer_kinds(registry), "corpus": code_corpus(),
           "sources": art_sources(), "authors": first_authors("HEAD"), "used": {}}
    meshes = gather_assets(registry, MESH_CLASSES)
    rows = [asset_row(d, ctx) for d in gather_packages(registry)]
    rows += branch_rows(ctx)
    return sorted((with_estado(r) for r in rows), key=sort_key), len(meshes)


def write_csv(rows):
    path = os.path.join(project_dir(), CSV_REL)
    with open(path, "w", encoding="utf-8-sig", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=FIELDS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    return path


def check(rows, mesh_total, on_disk):
    in_branch = [r for r in rows if r["tipo"] in MESH_CLASSES and not r["uso"].startswith("solo en la")]
    if len(in_branch) != mesh_total:
        raise RuntimeError(f"Mallas: {mesh_total} en el AssetRegistry y {len(in_branch)} en el CSV")
    missing = [r["ruta"] for r in rows if r["origen"] not in ORIGIN_ORDER]
    if missing:
        raise RuntimeError(f"Filas sin origen: {missing}")
    no_state = [r["ruta"] for r in rows if r["estado"] not in ESTADO_ORDER]
    if no_state:
        raise RuntimeError(f"Filas sin estado: {no_state}")
    # Todo Content/ salvo _Deprecado: una fila por .uasset o .umap, ni más ni menos.
    listed = {r["ruta"] for r in rows if not r["uso"].startswith("solo en la")}
    if listed != on_disk:
        raise RuntimeError(f"Content/ y el CSV no cuadran: faltan {sorted(on_disk - listed)[:20]}, "
                           f"sobran {sorted(listed - on_disk)[:20]}")


def report(rows):
    for key in ("origen", "autor", "categoria", "estado", "tipo"):
        counts = Counter(r[key] for r in rows)
        log(f"Por {key}: " + "; ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    combos = Counter((r["origen"], r["autor"]) for r in rows)
    log("Por origen y autor: " + "; ".join(f"{o}/{a} {v}" for (o, a), v in sorted(combos.items())))
    log(f"Sin usar: {sum(1 for r in rows if r['uso'] == 'sin usar')}; "
        f"solo en ramas de arte: {sum(1 for r in rows if r['uso'].startswith('solo en la'))}")


def main():
    rows, mesh_total = build_catalog()
    check(rows, mesh_total, disk_packages())
    rows = pendientes.annotate(rows)
    for path in pendientes.unlinked(rows):
        unreal.log_warning(f"{LOG_PREFIX} sin usar y sin issue que lo aplique (catalogo_pendientes.PENDING): {path}")
    path = write_csv(rows)
    report(rows)
    log(f"{len(rows)} filas ({mesh_total} mallas de esta rama) en {path}")
    log("OK")


if __name__ == "__main__":
    main()
