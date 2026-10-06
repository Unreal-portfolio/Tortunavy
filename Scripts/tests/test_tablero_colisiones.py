"""Tests de `colisiones` con pares que chocan solo en la localización generada (sin red, git ni gh)."""

import argparse
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tablero"))
import colisiones  # noqa: E402
import control  # noqa: E402
import memoria  # noqa: E402
import objetos  # noqa: E402
import tablero  # noqa: E402

LOC = ["Content/Localization/Game/Game.manifest", "Content/Localization/Game/es-ES/Game.po",
       "Content/Localization/Game/en/Game.locres"]


def _pr(numero, rama="feat/x", mergeable="MERGEABLE"):
    return {"number": numero, "baseRefName": "dev", "headRefName": rama, "body": f"Closes #{numero + 1000}",
            "mergeable": mergeable, "author": {"login": "Mokius"}, "createdAt": f"2026-10-0{numero % 9 + 1}"}


class Registro:
    """Sustituye a gh y a los efectos de control.py: guarda lo que se habría hecho en GitHub."""

    def __init__(self):
        self.creadas, self.comentarios, self.cerradas, self.campos = [], [], [], []

    def gh(self, *args):
        if args[:2] == ("issue", "close"):
            self.cerradas.append(int(args[2]))
        return ""


@pytest.fixture
def colisionar(monkeypatch):
    """Ejecuta `colisiones --aplicar` con los conflictos y las issues `colision` abiertas que se le pasen."""
    def ejecutar(conflictos: dict[tuple[int, int], list[str]], abiertas: list[dict] | None = None) -> Registro:
        registro = Registro()
        numeros = sorted({n for par in conflictos for n in par} | {9, 12, 15})
        monkeypatch.setattr(control, "prs_abiertas", lambda: [_pr(n) for n in numeros])
        monkeypatch.setattr(colisiones, "traer_cabezas", lambda *a, **k: True)
        monkeypatch.setattr(colisiones, "ficheros_de_pr", lambda _gh, _repo, n: set())
        monkeypatch.setattr(colisiones, "conflicto_git", lambda a, b: conflictos.get((a, b), []))
        monkeypatch.setattr(colisiones, "colisiones_abiertas", lambda _gh, _repo: abiertas or [])
        monkeypatch.setattr(objetos, "crear_etiqueta_si_falta", lambda *a: None)
        monkeypatch.setattr(control, "gh", registro.gh)
        monkeypatch.setattr(control, "cargar_proyecto", lambda *a: {"items": {}})
        monkeypatch.setattr(control, "comentar", lambda n, texto: registro.comentarios.append((n, texto)))
        monkeypatch.setattr(control, "crear_issue_colision",
                            lambda _p, a, b, ficheros: registro.creadas.append(
                                (a["number"], b["number"], colisiones.cuerpo(a, b, ficheros, "dev"))))
        control.cmd_colisiones(argparse.Namespace(aplicar=True))
        return registro
    return ejecutar


def test_par_solo_de_localizacion_no_crea_issue(colisionar, capsys):
    registro = colisionar({(9, 12): LOC})
    assert registro.creadas == []
    salida = capsys.readouterr().out
    assert "1 pares solo de localización, sin issue" in salida and "#9/#12" in salida
    assert "0 pares en conflicto" in salida


def test_par_mixto_crea_issue_con_los_ficheros_de_codigo(colisionar):
    registro = colisionar({(9, 12): ["Source/Tortunabo/X.cpp", *LOC]})
    assert [(a, b) for a, b, _ in registro.creadas] == [(9, 12)]
    cuerpo = registro.creadas[0][2]
    assert "- `Source/Tortunabo/X.cpp`" in cuerpo
    assert "Content/Localization" not in cuerpo
    assert "Además, 3 ficheros de localización: se regeneran." in cuerpo


def test_issue_abierta_solo_de_localizacion_se_cierra_con_resumen(colisionar):
    abiertas = [{"number": 300, "title": colisiones.titulo(9, 12)},
                {"number": 301, "title": colisiones.titulo(12, 15)}]
    registro = colisionar({(9, 12): LOC, (12, 15): ["Source/Tortunabo/X.cpp"]}, abiertas)
    assert registro.cerradas == [300]
    assert registro.creadas == []
    (numero, texto), = registro.comentarios
    assert numero == 300 and memoria.es_resumen(texto) and len(texto.splitlines()) == 1
    assert colisiones.SOLO_LOCALIZACION in texto


def test_resueltas_cierra_el_par_solo_de_localizacion():
    abiertas = [{"number": 300, "title": colisiones.titulo(9, 12)}]
    assert colisiones.resueltas(abiertas, {9, 12}, set(), {(9, 12)}) == [(300, colisiones.SOLO_LOCALIZACION)]


def test_separar_pares_por_localizacion():
    mixto, solo = (1, 2, ["Source/X.cpp", LOC[0]]), (3, 4, LOC)
    assert colisiones.separar([mixto, solo]) == ([mixto], [solo])


@pytest.mark.parametrize("ficheros, arreglo", [(LOC, "solo localización: regenerarla"),
                                               (["Source/X.cpp", *LOC], "rebase del autor"),
                                               (None, "rebase del autor")])
def test_aviso_de_conflicto_con_dev_solo_de_localizacion(monkeypatch, ficheros, arreglo):
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [_pr(400, mergeable="CONFLICTING")])
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [])
    monkeypatch.setattr(colisiones, "traer_cabezas", lambda *a, **k: True)
    monkeypatch.setattr(colisiones, "conflicto_con_base", lambda n, base: ficheros)
    avisos = []
    tablero.reconciliar_prs({"items": {}}, [], avisos)
    assert f"PR #400 (Mokius) tiene conflictos con dev: {arreglo}" in avisos
