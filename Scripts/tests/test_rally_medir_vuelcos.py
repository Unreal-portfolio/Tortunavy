"""Lectura de las líneas [RallyStats] de Scripts/rally_medir_vuelcos.py (#695): el formato es el de
ATN_RallyGameMode (TN_RallyGameModeRace.cpp), que el test lee del C++ para que no se desincronicen.

    uv run pytest Scripts/tests/test_rally_medir_vuelcos.py
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from rally_medir_vuelcos import ROOT, RaceStats, command, parse_stats  # noqa: E402

LOG = """[2026.10.04-18.00.00:000][  0]LogTNRally: [RallyStats] carrera 1 variante R05_circuito_marismas: terminados 1/1, \
atascos 0, vuelcos 2, caidas 0, fuera_de_pista 0, peticiones 0, reventados 0, giros 0, ganador 382.1 s
[2026.10.04-18.00.01:000][  1]LogTNRally: [RallyStats] 1 carreras hechas (?Races=1): fin.
"""


def test_lee_la_linea_de_fin_de_carrera():
    assert parse_stats(LOG) == [RaceStats("R05_circuito_marismas", 1, 1, 0, 2)]


def test_falla_con_mas_vuelcos_de_la_cuenta_o_sin_terminar():
    assert not RaceStats("R05", 1, 1, 0, 2).ok(1)
    assert RaceStats("R05", 1, 1, 0, 1).ok(1)
    assert not RaceStats("R05", 0, 1, 0, 0).ok(1)


def test_el_formato_es_el_del_cpp():
    src = (ROOT / "Source/Tortunabo/Private/Rally/TN_RallyGameModeRace.cpp").read_text(encoding="utf-8")
    assert "[RallyStats] carrera %d variante %s: terminados %d/%d, atascos %d, vuelcos %d," in src


def test_el_comando_va_sin_ventana_y_con_su_puerto():
    cmd = command(Path("editor.exe"), Path("T.uproject"), "R06_circuito_lomas", 3, 7811, Path("r.log"))
    assert "-server" in cmd and "-nullrhi" in cmd and "-port=7811" in cmd
    assert "Variant=R06_circuito_lomas" in cmd[2] and "Laps=3" in cmd[2] and "Bots=1" in cmd[2]
