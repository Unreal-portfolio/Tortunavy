"""Lista de piezas de arte que lee Scripts/arte/rellenar_catalogos.py (la tabla del C++, Docs/Arte_Assets.md).

    uv run python -m pytest Scripts/tests/test_arte_catalogos.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "arte"))

import rellenar_catalogos as rc  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
NAME = re.compile(r"^(Lobby|ProcMap|Beach|Turtle)(\.[A-Z][A-Za-z0-9]*)+$")


def test_lee_la_tabla_del_cpp():
    slots = rc.read_slots(str(ROOT))
    assert len(slots) > 0
    names = [s["name"] for s in slots]
    assert len(names) == len(set(names)), "piezas repetidas en la tabla"
    for slot in slots:
        assert NAME.match(slot["name"]), slot["name"]
        assert slot["kind"] in ("Pieza", "Componente", "Instancias", "Hueso"), slot["name"]
        # Las piezas de la tortuga (y solo ellas) van pegadas a un hueso.
        assert (slot["kind"] == "Hueso") == slot["name"].startswith("Turtle."), slot["name"]
        assert (ROOT / "Source" / "Tortunabo" / "Private" / slot["source"]).is_file(), slot["source"]
        assert slot["name"].split(".")[0] in rc.CATALOGS


def test_cuenta_todas_las_lineas_de_la_tabla():
    # Cada TN_ART_SLOT( de los .inl se lee: ninguno con un formato que el script no entienda.
    art = ROOT / "Source" / "Tortunabo" / "Private" / "Art"
    total = sum(f.read_text(encoding="utf-8").count("\nTN_ART_SLOT(") for f in art.glob("TN_ArtSlots_*.inl"))
    assert total == len(rc.read_slots(str(ROOT)))


def test_piezas_de_la_tortuga_en_su_catalogo():
    turtle = [s["name"] for s in rc.read_slots(str(ROOT)) if s["name"].startswith("Turtle.")]
    assert {"Turtle.Shell", "Turtle.Helmet", "Turtle.Eyes", "Turtle.Tongue"} <= set(turtle)
    assert rc.CATALOGS["Turtle"] == "/Game/Art/DA_Arte_Tortuga"
    text = rc.markdown(rc.read_slots(str(ROOT)))
    assert "### Piezas de la tortuga (DA_Arte_Tortuga)" in text


def test_markdown_tiene_una_fila_por_pieza():
    slots = rc.read_slots(str(ROOT))
    text = rc.markdown(slots)
    rows = [line for line in text.splitlines() if line.startswith("| `")]
    assert len(rows) == len(slots)
    assert "#### Lobby.Castle" in text


def test_info_dice_tipo_tamano_pivote_y_fichero():
    slot = {"name": "Lobby.Castle.Tower", "kind": "Pieza", "source": "Lobby/TN_CastleKit.h", "what": "Torre",
            "size": "Radio 250", "pivot": "Centro de la base"}
    info = rc.info_text(slot)
    for part in ("Torre", "Pieza", "Radio 250", "Centro de la base", "Lobby/TN_CastleKit.h"):
        assert part in info
