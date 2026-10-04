"""Archivo de presencia de Steam generado desde los Game.po (Scripts/steam/rich_presence_vdf.py)."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Scripts" / "steam"))

import rich_presence_vdf as vdf  # noqa: E402

PO_ES = '''
msgctxt "TNPresence,LobbyCount"
msgid "En el lobby ({Players}/{Max})"
msgstr "En el lobby ({Players}/{Max})"

msgctxt "TNPresence,Menu"
msgid "En el menú"
msgstr ""

msgctxt "TNRooms,Other"
msgid "Otra cosa"
msgstr "Otra cosa"
'''

PO_EN = '''
msgctxt "TNPresence,LobbyCount"
msgid "En el lobby ({Players}/{Max})"
msgstr ""
"In the lobby ({Players}/"
"{Max})"

msgctxt "TNPresence,Menu"
msgid "En el menú"
msgstr "In the \\"menu\\""
'''


def write_po(root: Path, culture: str, text: str) -> None:
    folder = root / culture
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "Game.po").write_text(text, encoding="utf-8")


def test_to_steam_lowercases_arguments():
    assert vdf.to_steam("Coop, nivel {Level}") == "Coop, nivel %level%"
    assert vdf.to_steam("({Players}/{Max})") == "(%players%/%max%)"
    assert vdf.to_steam("Sin argumentos") == "Sin argumentos"


def test_parse_po_joins_continuations_and_unescapes():
    entries = vdf.parse_po(PO_EN)
    assert entries["TNPresence,LobbyCount"] == ("En el lobby ({Players}/{Max})", "In the lobby ({Players}/{Max})")
    assert entries["TNPresence,Menu"][1] == 'In the "menu"'


def test_collect_uses_source_only_for_native_culture(tmp_path, monkeypatch):
    monkeypatch.setattr(vdf, "STEAM_LANGUAGES", {"es-ES": "spanish", "en": "english"})
    write_po(tmp_path, "es-ES", PO_ES)
    write_po(tmp_path, "en", PO_EN)
    languages, problems = vdf.collect(tmp_path)
    assert problems == []
    assert languages["spanish"] == {"#TN_LobbyCount": "En el lobby (%players%/%max%)", "#TN_Menu": "En el menú"}
    assert languages["english"]["#TN_Menu"] == 'In the "menu"'
    assert "#TN_Other" not in languages["spanish"]


def test_collect_reports_missing_translation_and_changed_arguments(tmp_path, monkeypatch):
    monkeypatch.setattr(vdf, "STEAM_LANGUAGES", {"fr": "french"})
    write_po(tmp_path, "fr", PO_EN.replace('"In the \\"menu\\""', '""').replace('"{Max})"', '"{Plazas})"'))
    _, problems = vdf.collect(tmp_path)
    assert any("falta la traducción de TNPresence,Menu" in p for p in problems)
    assert any("TNPresence,LobbyCount cambia los argumentos" in p for p in problems)


def test_render_escapes_quotes():
    text = vdf.render({"english": {"#TN_Menu": 'In the "menu"'}})
    assert '"#TN_Menu"\t"In the \\"menu\\""' in text
    assert text.startswith('"lang"\n{\n\t"english"')


def test_repository_file_is_up_to_date():
    # Los 13 idiomas traducidos y el .vdf del repositorio generado con ellos.
    assert vdf.main(["--check"]) == 0
