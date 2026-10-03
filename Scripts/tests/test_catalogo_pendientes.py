"""Cada asset sin usar del catálogo enlaza la issue que lo aplicará (#287, Scripts/tools/catalogo_pendientes.py).

    uv run pytest Scripts/tests/test_catalogo_pendientes.py
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

import catalogo_pendientes as cp  # noqa: E402


def row(path, use="sin usar", note="Biblioteca IA"):
    return {"ruta": path, "uso": use, "nota": note}


def test_anota_la_issue_de_un_asset_sin_usar():
    out = cp.annotate([row("/Game/Art/IA/puzzles/palanca/SM_TN_PalancaBase")])
    assert out[0]["nota"] == "Biblioteca IA · Pendiente: #599"


def test_lo_concreto_gana_a_lo_general():
    assert cp.issue_for("/Game/Art/IA/rally/caja_items/SM_TN_CajaItemsRally") == 304
    assert cp.issue_for("/Game/Art/IA/rally/caracola/SM_TN_Caracola") == 602
    assert cp.issue_for("/Game/Audio/EffectSounds/FootstepsMiniPack/SoundWav/SandAudio") == 604
    assert cp.issue_for("/Game/Audio/EffectSounds/Throw/SC_Throw") == 348


def test_no_toca_los_usados_ni_el_original():
    original = [row("/Game/Art/IA/puzzles/palanca/SM_TN_PalancaBase", use="1 (1 mapas, 0 BP)")]
    out = cp.annotate(original)
    assert out[0]["nota"] == "Biblioteca IA"
    assert out[0] is not original[0]


def test_es_idempotente_y_quita_la_marca_si_ya_se_usa():
    once = cp.annotate([row("/Game/Audio/Rally/SFX_Buggy_Land")])
    assert cp.annotate(once) == once
    used = [{**once[0], "uso": "código C++"}]
    assert cp.annotate(used)[0]["nota"] == "Biblioteca IA"


def test_sin_issue_se_avisa():
    assert cp.unlinked([row("/Game/Art/IA/nuevo/SM_Nuevo"), row("/Game/Art/IA/puzzles/x/SM_X")]) == ["/Game/Art/IA/nuevo/SM_Nuevo"]


def test_el_catalogo_versionado_no_tiene_assets_sin_usar_sin_issue():
    _, rows = cp.read_rows()
    assert cp.unlinked(rows) == []
    for r in rows:
        if r["uso"] == cp.UNUSED:
            assert f"{cp.NOTE_MARK}{cp.issue_for(r['ruta'])}" in r["nota"], r["ruta"]
