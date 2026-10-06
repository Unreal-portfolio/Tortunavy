"""Comprobaciones rápidas antes de entrar en main (.github/workflows/validar-main.yml, Docs/Flujo_Git.md).

Se ejecuta en los servidores de GitHub sin Unreal: revisa lo que se puede revisar sin compilar. También se puede lanzar en
local desde la raíz del repositorio:
    python .github/scripts/validar.py --base origin/main

Errores (paran la fusión):
  - archivos de Binaries/, Intermediate/, Saved/, DerivedDataCache/ o .vs/ en los cambios;
  - archivos de más de 50 MB en los cambios (GitHub rechaza los de más de 100 MB);
  - marcadores de conflicto (<<<<<<<, >>>>>>>) en archivos de texto;
  - código fuente (.h, .cpp, .cs) que no está en UTF-8;
  - scripts de Python con errores de sintaxis;
  - JSON mal formado (Tortunabo.uproject, manifiestos, .json de Scripts y Tools);
  - la misma clave de localización (NSLOCTEXT) con dos textos distintos;
  - traducciones (.po) con marcadores, plurales o saltos de línea rotos (Tools/Localization/po_tool.py check);
  - un texto visible que no se puede traducir: una línea nueva de Source/ que crea un FText desde un literal con letras
    (FText::FromString(TEXT("Hola")), FText::FromName, también dentro de un Printf o de un «? :»). Lo que de verdad no se
    traduce (nombres de tecla, siglas, cifras) se marca a propósito con INVTEXT("…") o FText::AsCultureInvariant(…).
Avisos (no paran): archivos de más de 10 MB; claves NSLOCTEXT nuevas o cambiadas que aún no están en Game.manifest (falta
recogerlas y traducirlas, Docs/Localizacion.md).

Con --todos, la comprobación de textos revisa todo Source/ y no solo las líneas nuevas.
"""
import argparse
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FORBIDDEN_PREFIXES = ("Binaries/", "Intermediate/", "Saved/", "DerivedDataCache/", ".vs/")
FORBIDDEN_SUFFIXES = (".sln", ".VC.db", ".suo")
ERROR_MB, WARN_MB = 50, 10
TEXT_EXT = (".h", ".hpp", ".cpp", ".cs", ".ini", ".py", ".md", ".json", ".po", ".txt", ".uproject", ".uplugin", ".yml",
            ".yaml", ".bat", ".sh", ".csv")
SOURCE_EXT = (".h", ".hpp", ".cpp", ".cs")
CONFLICT = re.compile(r"^(<<<<<<< |>>>>>>> )", re.M)
NSLOCTEXT = re.compile(r'NSLOCTEXT\s*\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)', re.S)
LOCTEXT_NS = re.compile(r'#define\s+LOCTEXT_NAMESPACE\s+"([^"]*)"')
LOCTEXT = re.compile(r'(?<![A-Z_])LOCTEXT\s*\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)', re.S)
FTEXT_FROM = re.compile(r"FText::From(?:String|Name)\s*\(")
STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
# Lo que no es texto dentro de un literal: formatos de Printf (%d, %.1f, %s), argumentos de FText::Format ({0}) y escapes.
FORMAT_SPEC = re.compile(r"%[-+ 0#]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[a-zA-Z]|\{[^{}]*\}|\\[nrt]")
LETTER = re.compile(r"[^\W\d_]")
MANIFEST = os.path.join("Content", "Localization", "Game", "Game.manifest")

errors, warnings = [], []


def gh(kind, msg, path=None, line=None):
    loc = ""
    if path:
        loc = f" file={path}" + (f",line={line}" if line else "")
    print(f"::{kind}{loc}::{msg}")


def error(msg, path=None, line=None):
    errors.append((msg, path))
    gh("error", msg, path, line)


def warn(msg, path=None, line=None):
    warnings.append((msg, path))
    gh("warning", msg, path, line)


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")


def tracked(*patterns):
    out = git("ls-files", "--", *patterns).stdout.splitlines()
    return [p for p in out if os.path.isfile(os.path.join(ROOT, p))]


def read_text(path):
    with open(os.path.join(ROOT, path), "rb") as f:
        return f.read()


def resolve_base(base):
    """El commit con el que comparar: la base dada si existe; si no, el padre de HEAD; None si tampoco hay."""
    if not base or set(base) == {"0"} or git("cat-file", "-e", f"{base}^{{commit}}").returncode != 0:
        head_parent = git("rev-parse", "--verify", "HEAD~1")
        return head_parent.stdout.strip() if head_parent.returncode == 0 else None
    return base


def changed_files(base):
    """(estado, ruta, blob) de lo que cambia entre base y HEAD; lista vacía si no hay base utilizable."""
    base = resolve_base(base)
    if base is None:
        print("Sin base con la que comparar: se omiten las comprobaciones de los cambios.")
        return []
    raw = git("diff", "--raw", "--no-renames", "-z", base, "HEAD")
    if raw.returncode != 0:
        warn(f"No se pudo comparar con {base}: {raw.stderr.strip()}")
        return []
    items, parts = [], raw.stdout.split("\0")
    i = 0
    while i < len(parts) - 1:
        meta = parts[i].split()
        if len(meta) >= 5:
            status, new_blob, path = meta[4], meta[3], parts[i + 1]
            items.append((status, path, new_blob))
        i += 2
    return items


def check_changes(base):
    changes = changed_files(base)
    print(f"Cambios revisados: {len(changes)} archivos.")
    for status, path, blob in changes:
        if status.startswith("D"):
            continue
        if path.startswith(FORBIDDEN_PREFIXES) or path.endswith(FORBIDDEN_SUFFIXES):
            error(f"No se suben archivos generados o locales: {path}", path)
            continue
        size = git("cat-file", "-s", blob)
        if size.returncode == 0:
            mb = int(size.stdout.strip()) / (1024 * 1024)
            if mb > ERROR_MB:
                error(f"Archivo de {mb:.1f} MB (el máximo es {ERROR_MB} MB): {path}", path)
            elif mb > WARN_MB:
                warn(f"Archivo grande ({mb:.1f} MB): {path}", path)


def check_text_files():
    files = [p for p in tracked("Source", "Config", "Docs", "Scripts", "Tools", ".github", "Content/Localization")
             if p.endswith(TEXT_EXT) and not p.startswith("Plugins/")]
    for path in files:
        data = read_text(path)
        if path.endswith(SOURCE_EXT):
            try:
                data.decode("utf-8-sig")
            except UnicodeDecodeError as exc:
                error(f"No está en UTF-8 ({exc.reason} en el byte {exc.start})", path)
                continue
        text = data.decode("utf-8-sig", errors="replace")
        if path.endswith(".md") or path == ".github/scripts/validar.py":
            continue  # los documentos pueden citar marcadores de conflicto como ejemplo
        m = CONFLICT.search(text)
        if m:
            error("Marcador de conflicto sin resolver", path, text.count("\n", 0, m.start()) + 1)
    print(f"Archivos de texto revisados: {len(files)}.")


def check_python():
    files = [p for p in tracked("*.py") if not p.startswith("Plugins/")]
    for path in files:
        try:
            compile(read_text(path).decode("utf-8-sig", errors="replace"), path, "exec")
        except SyntaxError as exc:
            error(f"Error de sintaxis de Python: {exc.msg}", path, exc.lineno)
    print(f"Scripts de Python revisados: {len(files)}.")


def check_json():
    files = [p for p in tracked("*.json", "*.uproject", "*.uplugin", "Content/Localization/*.manifest",
                                "Content/Localization/*.archive") if not p.startswith("Plugins/")]
    for path in files:
        data = read_text(path)
        try:
            # Los .archive y .manifest de Unreal están en UTF-16 con BOM; el resto, en UTF-8.
            utf16 = data.startswith(bytes([0xFF, 0xFE])) or data.startswith(bytes([0xFE, 0xFF]))
            json.loads(data.decode("utf-16") if utf16 else data.decode("utf-8-sig"))
        except (ValueError, UnicodeDecodeError) as exc:
            error(f"JSON mal formado: {exc}", path)
    print(f"Archivos JSON revisados: {len(files)}.")


def unquote(chunks):
    return "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', chunks))


def check_loc_keys():
    seen = {}
    for path in tracked("Source"):
        if not path.endswith((".h", ".cpp")):
            continue
        text = read_text(path).decode("utf-8-sig", errors="replace")
        entries = [(m.group(1), m.group(2), unquote(m.group(3)), m.start()) for m in NSLOCTEXT.finditer(text)]
        ns = LOCTEXT_NS.search(text)
        if ns:
            entries += [(ns.group(1), m.group(1), unquote(m.group(2)), m.start()) for m in LOCTEXT.finditer(text)]
        for namespace, key, source, pos in entries:
            line = text.count("\n", 0, pos) + 1
            prev = seen.get((namespace, key))
            if prev is None:
                seen[(namespace, key)] = (source, path, line)
            elif prev[0] != source:
                error(f"La clave de localización «{namespace},{key}» tiene dos textos distintos (también en {prev[1]}:{prev[2]})",
                      path, line)
    print(f"Claves de localización revisadas: {len(seen)}.")


def strip_line_comment(line):
    """La línea sin su comentario «//» (respeta las comillas: «"http://…"» no es un comentario)."""
    in_str, esc = False, False
    for i, ch in enumerate(line):
        if esc:
            esc = False
        elif ch == "\\":
            esc = True
        elif ch == '"':
            in_str = not in_str
        elif not in_str and line.startswith("//", i):
            return line[:i]
    return line


def call_argument(line, start):
    """El texto entre el «(» de la posición start y su «)» (hasta el final de la línea si la llamada sigue en otra)."""
    depth, in_str, esc = 0, False, False
    for i in range(start, len(line)):
        ch = line[i]
        if esc:
            esc = False
        elif ch == "\\":
            esc = True
        elif ch == '"':
            in_str = not in_str
        elif not in_str and ch == "(":
            depth += 1
        elif not in_str and ch == ")":
            depth -= 1
            if depth == 0:
                return line[start + 1:i]
    return line[start + 1:]


def untranslatable_literals(line):
    """Literales con letras que acaban en un FText sin pasar por la localización (FText::FromString o FromName)."""
    code = strip_line_comment(line)
    found = []
    for m in FTEXT_FROM.finditer(code):
        for lit in STRING_LITERAL.findall(call_argument(code, m.end() - 1)):
            if LETTER.search(FORMAT_SPEC.sub("", lit)):
                found.append(lit)
    return found


def parse_added_lines(diff_text):
    """(ruta, línea, texto) de las líneas añadidas en un «git diff -U0»."""
    out, path, line_no = [], None, 0
    for raw in diff_text.splitlines():
        if raw.startswith("+++ "):
            path = raw[6:] if raw.startswith("+++ b/") else None
        elif raw.startswith("@@"):
            m = re.search(r"\+(\d+)", raw)
            line_no = int(m.group(1)) if m else 0
        elif raw.startswith("+") and path:
            out.append((path, line_no, raw[1:]))
            line_no += 1
    return out


def added_source_lines(base):
    base = resolve_base(base)
    if base is None:
        return []
    diff = git("diff", "-U0", "--no-color", "--no-renames", base, "HEAD", "--", "Source")
    return [(p, n, t) for p, n, t in parse_added_lines(diff.stdout) if p.endswith((".h", ".cpp"))]


def all_source_lines():
    out = []
    for path in tracked("Source"):
        if path.endswith((".h", ".cpp")):
            text = read_text(path).decode("utf-8-sig", errors="replace")
            out += [(path, i, line) for i, line in enumerate(text.splitlines(), 1)]
    return out


def is_test_source(path):
    return "/Tests/" in path


def load_manifest_keys(path=None):
    """{(espacio, clave): texto} de Game.manifest (UTF-16 con BOM); None si no existe."""
    full = path or os.path.join(ROOT, MANIFEST)
    if not os.path.isfile(full):
        return None
    with open(full, "rb") as f:
        data = f.read()
    utf16 = data.startswith(bytes([0xFF, 0xFE])) or data.startswith(bytes([0xFE, 0xFF]))
    root = json.loads(data.decode("utf-16") if utf16 else data.decode("utf-8-sig"))
    keys = {}

    def walk(node, namespace):
        for child in node.get("Children", []):
            for key in child.get("Keys", []):
                keys[(namespace, key.get("Key", ""))] = child.get("Source", {}).get("Text", "")
        for sub_node in node.get("Subnamespaces", []):
            walk(sub_node, sub_node.get("Namespace", ""))

    walk(root, root.get("Namespace", ""))
    return keys


def unescape_cpp(text):
    return re.sub(r'\\(["\\])', r"\1", text)


def ungathered_keys(lines, manifest):
    """{ruta: ["Espacio,Clave", ...]} de los NSLOCTEXT de esas líneas que no están en el manifiesto o cambiaron de texto."""
    pending = {}
    for path, _, text in lines:
        for m in NSLOCTEXT.finditer(text):
            namespace, key, source = m.group(1), m.group(2), unescape_cpp(unquote(m.group(3)))
            if manifest.get((namespace, key)) != source:
                pending.setdefault(path, []).append(f"{namespace},{key}")
    return pending


def check_texts(base, everything=False):
    lines = [l for l in (all_source_lines() if everything else added_source_lines(base)) if not is_test_source(l[0])]
    bad = 0
    for path, line_no, text in lines:
        for lit in untranslatable_literals(text):
            bad += 1
            error(f"Texto visible que no se puede traducir: «{lit}». Ponlo con NSLOCTEXT(\"Espacio\", \"Clave\", \"{lit}\"); "
                  f"si de verdad no se traduce (tecla, sigla, cifra), con INVTEXT o FText::AsCultureInvariant "
                  f"(Docs/Localizacion.md)", path, line_no)
    manifest = load_manifest_keys()
    pending = ungathered_keys(lines, manifest) if manifest is not None and not everything else {}
    for path, keys in pending.items():
        shown = ", ".join(keys[:5]) + ("…" if len(keys) > 5 else "")
        warn(f"{len(keys)} textos nuevos o cambiados sin recoger ({shown}): hay que recogerlos y traducirlos "
             "(Scripts/localization_gather_export.bat y Tools/Localization/po_tool.py)", path)
    print(f"Líneas de código revisadas: {len(lines)}; textos que no se pueden traducir: {bad}; "
          f"archivos con textos sin recoger: {len(pending)}.")


def check_po():
    tool = os.path.join(ROOT, "Tools", "Localization", "po_tool.py")
    loc_dir = os.path.join(ROOT, "Content", "Localization", "Game")
    if not (os.path.isfile(tool) and os.path.isdir(loc_dir)):
        print("Sin traducciones que revisar.")
        return
    for culture in sorted(os.listdir(loc_dir)):
        if not os.path.isfile(os.path.join(loc_dir, culture, "Game.po")):
            continue
        run = subprocess.run([sys.executable, tool, "check", culture], cwd=ROOT, capture_output=True, text=True,
                             encoding="utf-8", errors="replace", env={**os.environ, "PYTHONIOENCODING": "utf-8"})
        summary = run.stdout.strip().splitlines()[-1] if run.stdout.strip() else run.stderr.strip()
        print(f"  {summary}")
        if run.returncode != 0:
            error(f"Traducción «{culture}» con avisos: {summary} (python Tools/Localization/po_tool.py check {culture})",
                  f"Content/Localization/Game/{culture}/Game.po")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="", help="commit con el que comparar (la base de la pull request)")
    parser.add_argument("--todos", action="store_true", help="revisa los textos de todo Source/, no solo las líneas nuevas")
    args = parser.parse_args()

    print("== Cambios"); check_changes(args.base)
    print("== Archivos de texto"); check_text_files()
    print("== Python"); check_python()
    print("== JSON"); check_json()
    print("== Claves de localización"); check_loc_keys()
    print("== Textos que no se pueden traducir"); check_texts(args.base, args.todos)
    print("== Traducciones"); check_po()

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    lines = [f"### Comprobaciones: {'bien' if not errors else f'{len(errors)} errores'}, {len(warnings)} avisos", ""]
    lines += [f"- ❌ {m}" for m, _ in errors] + [f"- ⚠️ {m}" for m, _ in warnings]
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())
