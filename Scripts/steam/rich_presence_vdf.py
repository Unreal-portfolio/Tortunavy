"""Genera el archivo de presencia de Steam (rich presence) desde las traducciones del juego.

La lista de amigos de Steam enseña el texto de la clave «steam_display» (#TN_Menu, #TN_CoopLevel...) traducido al idioma de
quien mira, con los textos de este archivo, que se sube en Steamworks (Community > Rich Presence Localization) cuando el
juego tenga AppID propio; con el 480 de pruebas no se puede subir.

Una sola fuente: los NSLOCTEXT del espacio «TNPresence» (Source/Tortunabo/Private/Multiplayer/TN_RichPresenceRules.cpp), ya
traducidos en Content/Localization/Game/<cultura>/Game.po. Cada clave del código (Menu, CoopLevel...) es el token
#TN_<clave> y cada {Argumento} del texto pasa a %argumento% (el valor lo manda UTN_RichPresenceSubsystem con ese nombre).

Uso:
    uv run python Scripts/steam/rich_presence_vdf.py            # escribe Scripts/steam/rich_presence.vdf
    uv run python Scripts/steam/rich_presence_vdf.py --check    # solo comprueba que está al día (código 1 si no)

Sale con código 1 si algún idioma no tiene la traducción de algún texto o si cambia sus {argumentos}.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
PO_ROOT = PROJECT / "Content" / "Localization" / "Game"
OUTPUT = Path(__file__).resolve().parent / "rich_presence.vdf"
NAMESPACE = "TNPresence"

# Cultura del juego -> código de idioma de la API de Steam (partner.steamgames.com/doc/store/localization/languages).
STEAM_LANGUAGES = {
    "en": "english",
    "es-ES": "spanish",
    "fr": "french",
    "de": "german",
    "it": "italian",
    "pt-BR": "brazilian",
    "ru": "russian",
    "pl": "polish",
    "tr": "turkish",
    "ja": "japanese",
    "ko": "koreana",
    "zh-Hans": "schinese",
    "zh-Hant": "tchinese",
}
NATIVE_CULTURE = "es-ES"

_ARGUMENT = re.compile(r"\{([A-Za-z][A-Za-z0-9_]*)\}")
_PO_STRING = re.compile(r'^(msgctxt|msgid|msgstr)\s+"(.*)"$')
_PO_CONTINUATION = re.compile(r'^"(.*)"$')


def unescape_po(text: str) -> str:
    """Quita los escapes de una cadena de .po (\\" \\\\ \\n \\t)."""
    out = []
    i = 0
    while i < len(text):
        char = text[i]
        if char == "\\" and i + 1 < len(text):
            nxt = text[i + 1]
            out.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(nxt, nxt))
            i += 2
            continue
        out.append(char)
        i += 1
    return "".join(out)


def parse_po(text: str) -> dict[str, tuple[str, str]]:
    """{msgctxt: (msgid, msgstr)} de un .po (las cadenas pueden seguir en varias líneas)."""
    entries: dict[str, tuple[str, str]] = {}
    current: dict[str, str] = {}
    field = None

    def flush() -> None:
        if "msgctxt" in current and "msgid" in current:
            entries[current["msgctxt"]] = (current["msgid"], current.get("msgstr", ""))

    for raw in text.splitlines():
        line = raw.strip()
        match = _PO_STRING.match(line)
        if match:
            key, value = match.group(1), unescape_po(match.group(2))
            if key == "msgctxt" or (key == "msgid" and "msgid" in current):
                flush()
                current = {}
            current[key] = value
            field = key
            continue
        match = _PO_CONTINUATION.match(line)
        if match and field:
            current[field] += unescape_po(match.group(1))
            continue
        if not line:
            field = None
    flush()
    return entries


def to_steam(text: str) -> str:
    """{Players} -> %players%: el formato de los parámetros de Steam, con el nombre en minúsculas."""
    return _ARGUMENT.sub(lambda m: "%" + m.group(1).lower() + "%", text)


def arguments(text: str) -> set[str]:
    return set(_ARGUMENT.findall(text))


def escape_vdf(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def collect(po_root: Path) -> tuple[dict[str, dict[str, str]], list[str]]:
    """{idioma de Steam: {token: texto}} y la lista de problemas (traducciones que faltan o con otros argumentos)."""
    languages: dict[str, dict[str, str]] = {}
    problems: list[str] = []
    prefix = NAMESPACE + ","
    for culture, steam in STEAM_LANGUAGES.items():
        po_path = po_root / culture / "Game.po"
        if not po_path.exists():
            problems.append(f"{culture}: no está {po_path}")
            continue
        entries = parse_po(po_path.read_text(encoding="utf-8"))
        tokens: dict[str, str] = {}
        for context, (source, translation) in sorted(entries.items()):
            if not context.startswith(prefix):
                continue
            key = context[len(prefix):]
            text = translation or (source if culture == NATIVE_CULTURE else "")
            if not text:
                problems.append(f"{culture}: falta la traducción de {context} («{source}»)")
                continue
            if arguments(text) != arguments(source):
                problems.append(f"{culture}: {context} cambia los argumentos ({sorted(arguments(source))} -> {sorted(arguments(text))})")
                continue
            tokens["#TN_" + key] = to_steam(text)
        if not tokens:
            problems.append(f"{culture}: ningún texto de {NAMESPACE} (¿falta recoger los textos?)")
        languages[steam] = tokens
    return languages, problems


def render(languages: dict[str, dict[str, str]]) -> str:
    """Formato de Steamworks con todos los idiomas en un archivo («lang» > idioma > «tokens»)."""
    lines = ['"lang"', "{"]
    for steam in STEAM_LANGUAGES.values():
        tokens = languages.get(steam)
        if not tokens:
            continue
        lines += [f'\t"{steam}"', "\t{", '\t\t"tokens"', "\t\t{"]
        for token in sorted(tokens):
            lines.append(f'\t\t\t"{escape_vdf(token)}"\t"{escape_vdf(tokens[token])}"')
        lines += ["\t\t}", "\t}"]
    lines.append("}")
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--po", type=Path, default=PO_ROOT, help="carpeta con <cultura>/Game.po")
    parser.add_argument("--out", type=Path, default=OUTPUT, help="archivo .vdf que se escribe")
    parser.add_argument("--check", action="store_true", help="no escribe: falla si el archivo no está al día")
    args = parser.parse_args(argv)

    languages, problems = collect(args.po)
    for problem in problems:
        print("  ! " + problem)
    if problems:
        return 1
    text = render(languages)
    if args.check:
        current = args.out.read_text(encoding="utf-8") if args.out.exists() else ""
        if current != text:
            print(f"{args.out} no está al día: vuelve a generarlo.")
            return 1
        print(f"{args.out} al día ({len(languages)} idiomas).")
        return 0
    args.out.write_text(text, encoding="utf-8", newline="\n")
    count = len(next(iter(languages.values()), {}))
    print(f"Escrito {args.out}: {len(languages)} idiomas, {count} textos.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
