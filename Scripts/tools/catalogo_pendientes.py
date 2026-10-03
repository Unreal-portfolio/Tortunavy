"""Issue que aplicará cada asset «sin usar» de Art/catalogo_assets.csv (#287).

Decisión del 01-10: todo asset nuevo de Art/ se aplica en la misma PR o abre una issue que lo enlaza. catalogo_assets.py
importa este módulo al generar el CSV: añade «Pendiente: #N» a la nota de cada fila sin usar y avisa de las que no tienen
issue. El test Scripts/tests/test_catalogo_pendientes.py falla si el CSV versionado tiene alguna sin issue.

Sin Unreal, también anota un CSV ya generado (idempotente):
    uv run python Scripts/tools/catalogo_pendientes.py            # anota Art/catalogo_assets.csv
    uv run python Scripts/tools/catalogo_pendientes.py --comprobar # solo comprueba; sale con 1 si falta alguna issue
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path

UNUSED = "sin usar"
NOTE_MARK = "Pendiente: #"
NOTE_SEPARATOR = " · "

# (prefijo de la ruta /Game, issue que lo aplica). Gana el primero que encaja: lo concreto va antes.
PENDING = (
    ("/Game/Art/IA/puzzles/", 599),
    ("/Game/Art/IA/todos_contra_todos/", 600),
    ("/Game/Art/IA/decoracion/", 601),
    ("/Game/Art/IA/rally/caja_items/", 304),
    ("/Game/Art/IA/rally/", 602),
    ("/Game/Art/Source/Vehicles/Buggy/", 290),
    ("/Game/Audio/Rally/", 603),
    ("/Game/Audio/EffectSounds/FootstepsMiniPack/", 604),
    ("/Game/Audio/EffectSounds/", 348),
    ("/Game/Blueprints/Characters/SKM_Tortuga_Merged", 581),
    # Restos del Modeling Mode en las carpetas _GENERATED de los mapas: se borran en la limpieza por lotes.
    ("/Game/Maps/_GENERATED/", 31),
    ("/Game/Maps/Lobby/_GENERATED/", 31),
)

ROOT = Path(__file__).resolve().parents[2]
CSV_PATH = ROOT / "Art" / "catalogo_assets.csv"


def issue_for(path: str) -> int | None:
    """Issue que aplicará el asset de `path` (ruta /Game), o None si no hay ninguna."""
    for prefix, issue in PENDING:
        if path.startswith(prefix):
            return issue
    return None


def base_note(note: str) -> str:
    """Nota sin la marca «Pendiente: #N» que añade annotate."""
    parts = [part for part in note.split(NOTE_SEPARATOR) if not part.startswith(NOTE_MARK)]
    return NOTE_SEPARATOR.join(parts)


def annotate(rows: list[dict]) -> list[dict]:
    """Copias de las filas con «Pendiente: #N» en la nota de cada asset sin usar que tiene issue (idempotente)."""
    out = []
    for row in rows:
        note = base_note(row.get("nota", ""))
        issue = issue_for(row["ruta"]) if row.get("uso") == UNUSED else None
        if issue is not None:
            note = f"{note}{NOTE_SEPARATOR}{NOTE_MARK}{issue}" if note else f"{NOTE_MARK}{issue}"
        out.append({**row, "nota": note})
    return out


def unlinked(rows: list[dict]) -> list[str]:
    """Rutas de los assets sin usar que no tienen issue."""
    return [row["ruta"] for row in rows if row.get("uso") == UNUSED and issue_for(row["ruta"]) is None]


def read_rows(path: Path = CSV_PATH) -> tuple[list[str], list[dict]]:
    with path.open(encoding="utf-8-sig", newline="") as fh:
        reader = csv.DictReader(fh)
        return list(reader.fieldnames or []), list(reader)


def write_rows(fields: list[str], rows: list[dict], path: Path = CSV_PATH) -> None:
    with path.open("w", encoding="utf-8-sig", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def main(argv: list[str]) -> int:
    fields, rows = read_rows()
    missing = unlinked(rows)
    if "--comprobar" not in argv:
        write_rows(fields, annotate(rows))
    for path in missing:
        print(f"sin issue: {path}")
    print(f"{sum(1 for r in rows if r.get('uso') == UNUSED)} sin usar, {len(missing)} sin issue")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
