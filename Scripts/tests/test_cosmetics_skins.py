"""Catálogo de skins de la tienda (#873, Scripts/cosmetics_skins.py): rareza, precio y filas nuevas.

    uv run pytest Scripts/tests/test_cosmetics_skins.py
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import cosmetics_skins as cs  # noqa: E402

RARITIES = {"Common", "Rare", "Epic"}
COLUMNS = {"Name", "SkinId", "DisplayName", "Category", "Price", "Rarity", "Description", "Color", "Color2",
           "BellyAmount", "Pattern", "PatternScale", "Shine", "Glow", "EyeStyle", "BellyMaterial", "EyeShineMaterial",
           "EyesMouthMaterial", "SkinMaterial", "ShellMaterial", "Icon"}


def test_precio_de_100_a_400_segun_la_rareza():
    rows = cs.skin_rows()
    for row in rows:
        assert row["Rarity"] in RARITIES, row["Name"]
        assert row["Price"] == cs.PRICE_BY_RARITY[row["Rarity"]], row["Name"]
        assert 100 <= row["Price"] <= 400, row["Name"]


def test_hay_skins_de_las_tres_rarezas_para_la_caja():
    rarities = {row["Rarity"] for row in cs.skin_rows()}
    assert rarities == RARITIES


def test_al_menos_ocho_skins_nuevas_y_todas_en_la_tabla():
    names = [row["Name"] for row in cs.skin_rows()]
    assert len(cs.NEW_IN_873) >= 8
    assert set(cs.NEW_IN_873) <= set(names)


def test_ids_unicos_y_todas_las_columnas():
    rows = cs.skin_rows()
    names = [row["Name"] for row in rows]
    assert len(names) == len(set(names))
    for row in rows:
        assert set(row) == COLUMNS, row["Name"]
        assert row["SkinId"] == row["Name"]
        assert row["Name"].split("_", 1)[0] == row["Category"]


def test_color_lineal_como_el_editor():
    # 0xC8A165 (Escamas clásicas) en lineal: el valor que ya tenía DT_Skins.
    color = cs.lin_json(0xC8A165)
    assert color == {"R": 0.57758, "G": 0.3564, "B": 0.13014, "A": 1.0}
