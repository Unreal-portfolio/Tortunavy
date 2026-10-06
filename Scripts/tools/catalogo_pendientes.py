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
# Limpieza de basura del repo por lotes: lo que la tiene pendiente se borra (en el catálogo, estado «deprecado»).
CLEANUP_ISSUE = 31

# (prefijo de la ruta /Game, issue que lo aplica). Gana el primero que encaja: lo concreto va antes.
PENDING = (
    ("/Game/Art/IA/puzzles/", 599),
    ("/Game/Art/IA/decoracion/", 601),
    ("/Game/Audio/EffectSounds/FootstepsMiniPack/", 604),
    ("/Game/Audio/EffectSounds/", 348),
    # La galería de assets es una herramienta: la abre y la rehace su issue.
    ("/Game/Maps/Dev/", 312),
    # Restos sin referencias en mapas, Blueprints ni código: la limpieza por lotes decide si se borran.
    # Modeling Mode en las carpetas _GENERATED de los mapas y piezas del blockout del lobby que no se colocaron.
    ("/Game/Maps/_GENERATED/", CLEANUP_ISSUE),
    ("/Game/Maps/Lobby/_GENERATED/", CLEANUP_ISSUE),
    ("/Game/Blueprints/Builder/", CLEANUP_ISSUE),
    ("/Game/Materials/Grid/", CLEANUP_ISSUE),
    # Tortuga fusionada antigua: la del jugador sale de BP_TortugaCharacter (#581, cerrada) y no la usa.
    ("/Game/Blueprints/Characters/SKM_Tortuga_Merged", CLEANUP_ISSUE),
    ("/Game/Blueprints/Characters/SK_Tortuga_Merged", CLEANUP_ISSUE),
    ("/Game/Blueprints/Characters/Textures/gorro", CLEANUP_ISSUE),
    # Materiales de LVL_ProcGenDemo, que ya no existe (M_GridTerrain y M_GridTerrainWet sí se usan).
    ("/Game/Blueprints/Gameplay/GridMap/M", CLEANUP_ISSUE),
    ("/Game/Blueprints/Gameplay/Chunks/BP_Chunk_Medium_Personaliced", CLEANUP_ISSUE),
    ("/Game/Blueprints/Gameplay/Interaction/BP_CollectionZone", CLEANUP_ISSUE),
    ("/Game/ProcMap/Materials/MI_ProcSlideWater", CLEANUP_ISSUE),
    ("/Game/Maps/LVL_LevelMetrics", CLEANUP_ISSUE),
    # Redirectores de assets movidos (Fix Up Redirectors).
    ("/Game/Animations/Character/TortugaDemo/Salute", CLEANUP_ISSUE),
    ("/Game/Animations/Character/TortugaDemo/Yelling", CLEANUP_ISSUE),
    ("/Game/Blueprints/BP_Net", CLEANUP_ISSUE),
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
