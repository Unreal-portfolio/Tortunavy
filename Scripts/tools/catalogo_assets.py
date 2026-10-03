"""Catálogo de assets del juego en CSV (issues #126, #287 y #312).

Escribe Art/catalogo_assets.csv con una fila por StaticMesh, SkeletalMesh, SoundWave y SoundCue de /Game (sin _Deprecado,
Developers ni /Engine): nombre, ruta, tipo, triángulos, caja en cm, duración, carpeta, origen, autor, uso y fuente en Art/.
Añade las mallas que solo existen en las ramas de arte (origin/Arte de María y origin/ArteDev de Mokius), leídas de git
sin copiar los binarios. La galería (galeria_assets.py) importa este módulo para ordenar las piezas igual.

- Origen: lo decide la primera regla de ORIGIN_RULES cuyo prefijo encaja; un asset sin regla detiene el script.
- Autor: autor del primer commit que añadió el .uasset (siguiendo los renombrados); «solo local» si no está versionado.
- Uso: referencias desde mapas y Blueprints según el AssetRegistry; si no hay, las indirectas (DataTable, DataAsset) y
  las rutas escritas en Source/ o Config/; «sin usar» si no hay ninguna.
- Nota: la de la regla de origen y, si está sin usar, «Pendiente: #N» con la issue que lo aplicará
  (catalogo_pendientes.PENDING, #287). Avisa de los assets sin usar que no tienen issue.

Uso (editor cerrado):
  UnrealEditor-Win64-DebugGame-Cmd.exe Tortunabo.uproject -run=pythonscript
    -script=Scripts/tools/catalogo_assets.py -EnablePlugins=PythonScriptPlugin -unattended -nosplash -nullrhi
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

CSV_REL = "Art/catalogo_assets.csv"
LOG_PREFIX = "[Catalogo]"
MESH_CLASSES = ("StaticMesh", "SkeletalMesh")
SOUND_CLASSES = ("SoundWave", "SoundCue")
EXCLUDED = ("/_Deprecado", "/Deprecado", "/Developers", "/OLD/", "/Game/Maps/Dev/")
# Referencias que no cuentan como uso: la propia galería y lo deprecado.
IGNORED_REFERENCERS = ("/Game/Maps/Dev/LVL_GaleriaAssets", "/Game/_Deprecado", "/Deprecado/")
ART_BRANCHES = (("origin/Arte", "Arte"), ("origin/ArteDev", "ArteDev"))
ART_SOURCE_EXT = (".fbx", ".wav", ".blend")

HUMANO = "Humano"
IA = "IA"
SCRIPT = "Script-Blender"
MOTOR = "Motor o plantilla"
ORIGIN_ORDER = (HUMANO, IA, SCRIPT, MOTOR)

# (prefijo de ruta /Game, origen, fuente por defecto, nota). Gana la primera que encaja: lo concreto va antes.
# Revisadas con el historial de git a 2026-10-02 (autores del primer commit de cada carpeta entre paréntesis).
ORIGIN_RULES = (
    ("/Game/Art/IA/", IA, "", "Biblioteca IA (Art/Library/IA/_pipeline: imagen a malla y limpieza en Blender)"),
    ("/Game/Art/Source/", SCRIPT, "", "Modelado por script de Blender en Art/Source"),
    ("/Game/Generated/Meshes/Buggy/", SCRIPT, "", "Buggy por script de Blender, copiado de HellYeah (01979da1c)"),
    ("/Game/Terrain/", SCRIPT, "Scripts/gen_terrain_volume.py", "Terreno generado e importado por import_terrain_mesh.py"),
    ("/Game/Environment/Water/", SCRIPT, "Scripts/build_water.py", "Superficie de agua generada por script"),
    ("/Game/Cosmetics/Helmets/", SCRIPT, "Scripts/cosmetics_meshes.py", "Cascos modelados en Python (Mokius, f4e675c1d)"),
    ("/Game/Audio/Rally/", SCRIPT, "Art/Source/Audio/Rally/gen_rally_sfx.py", "Sonidos sintetizados por script"),
    ("/Game/Vehicles/", MOTOR, "", "Plantilla Vehicle de UE (OffroadCar), copiada de HellYeah"),
    ("/Game/StarterContent/", MOTOR, "", "Starter Content de UE"),
    ("/Game/Characters/Mannequins/", MOTOR, "", "Maniquí de las plantillas de UE"),
    ("/Game/Audio/EffectSounds/FootstepsMiniPack/", MOTOR, "", "Pack externo de pasos; licencia sin registrar"),
    ("/Game/Meshses/", HUMANO, "", "Modelado a mano por los artistas (Alvaro2rh; María en la rama Arte)"),
    ("/Game/Maps/", HUMANO, "", "Modelado a mano en el editor (Modeling Mode, carpetas _GENERATED de Alvaro2rh)"),
    ("/Game/Blueprints/Characters/Meshes/", HUMANO, "", "Modelos a mano de María, subidos por Mokius (abril)"),
    ("/Game/Blueprints/Characters/", HUMANO, "", "Tortuga del jugador: piezas a mano fusionadas en el editor"),
    ("/Game/Audio/", HUMANO, "", "Sonidos elegidos por el equipo (Mokius, MiguelilloElPillo); licencia sin registrar"),
)

AUTHOR_NAMES = {
    "Rodrigo Fernandez": "SkiTemplar (Rodrigo)",
    "Rodrigo Fernández": "SkiTemplar (Rodrigo)",
    "SkiTemplar": "SkiTemplar (Rodrigo)",
    "María Calzado del Valle": "María Calzado",
}
LOCAL_ONLY = "solo local"
FIELDS = ("nombre", "ruta", "tipo", "triangulos", "caja_cm", "duracion_s", "carpeta", "origen", "autor", "uso",
          "refs_mapas", "refs_bp", "refs_otros", "refs_codigo", "fuente", "nota")


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
    return not path.startswith("/Game/") or any(token in path + "/" for token in EXCLUDED)


def origin_of(package):
    for prefix, origin, source, note in ORIGIN_RULES:
        if package.startswith(prefix):
            return origin, source, note
    raise RuntimeError(f"{package} no encaja en ninguna regla de ORIGIN_RULES: añade una")


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
        elif len(parts) == 3 and parts[0][:1] in "RC":
            authors[parts[2]] = authors.get(parts[1], current)
    return {path: AUTHOR_NAMES.get(name, name) for path, name in authors.items()}


def package_to_file(package):
    return "Content/" + package[len("/Game/"):] + ".uasset"


def file_to_package(path):
    return "/Game/" + path[len("Content/"):-len(".uasset")]


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
    return re.search(re.escape(package) + r"(?![A-Za-z0-9_])", ctx["corpus"]) is not None


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


def asset_row(data, ctx):
    package = str(data.package_name)
    cls = str(data.asset_class_path.asset_name)
    origin, rule_source, note = origin_of(package)
    maps, bps, others, live, has_code = usage(ctx, package)
    name = str(data.asset_name)
    row = {
        "nombre": name, "ruta": package, "tipo": cls, "triangulos": "", "caja_cm": "", "duracion_s": "",
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
    else:
        duration = data.get_tag_value("Duration")
        row["duracion_s"] = f"{float(duration):.2f}" if duration else ""
    return row


def gather_assets(registry, class_names):
    found = [d for d in registry.get_assets(class_filter(class_names)) if not is_excluded(str(d.package_path))]
    return sorted(found, key=lambda d: str(d.package_name))


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
            rows[path] = {"nombre": name, "ruta": package, **tags, "duracion_s": "",
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


def build_catalog():
    """Filas del catálogo (esta rama y ramas de arte) y nº de mallas de esta rama según el AssetRegistry."""
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    ctx = {"registry": registry, "kinds": referencer_kinds(registry), "corpus": code_corpus(),
           "sources": art_sources(), "authors": first_authors("HEAD"), "used": {}}
    meshes = gather_assets(registry, MESH_CLASSES)
    sounds = gather_assets(registry, SOUND_CLASSES)
    rows = [asset_row(d, ctx) for d in meshes + sounds]
    rows += branch_rows(ctx)
    return sorted(rows, key=sort_key), len(meshes)


def write_csv(rows):
    path = os.path.join(project_dir(), CSV_REL)
    with open(path, "w", encoding="utf-8-sig", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=FIELDS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    return path


def check(rows, mesh_total):
    in_branch = [r for r in rows if r["tipo"] in MESH_CLASSES and not r["uso"].startswith("solo en la")]
    if len(in_branch) != mesh_total:
        raise RuntimeError(f"Mallas: {mesh_total} en el AssetRegistry y {len(in_branch)} en el CSV")
    missing = [r["ruta"] for r in rows if r["origen"] not in ORIGIN_ORDER]
    if missing:
        raise RuntimeError(f"Filas sin origen: {missing}")


def report(rows):
    for key in ("origen", "autor", "tipo"):
        counts = Counter(r[key] for r in rows)
        log(f"Por {key}: " + "; ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    combos = Counter((r["origen"], r["autor"]) for r in rows)
    log("Por origen y autor: " + "; ".join(f"{o}/{a} {v}" for (o, a), v in sorted(combos.items())))
    log(f"Sin usar: {sum(1 for r in rows if r['uso'] == 'sin usar')}; "
        f"solo en ramas de arte: {sum(1 for r in rows if r['uso'].startswith('solo en la'))}")


def main():
    rows, mesh_total = build_catalog()
    check(rows, mesh_total)
    rows = pendientes.annotate(rows)
    for path in pendientes.unlinked(rows):
        unreal.log_warning(f"{LOG_PREFIX} sin usar y sin issue que lo aplique (catalogo_pendientes.PENDING): {path}")
    path = write_csv(rows)
    report(rows)
    log(f"{len(rows)} filas ({mesh_total} mallas de esta rama) en {path}")
    log("OK")


if __name__ == "__main__":
    main()
