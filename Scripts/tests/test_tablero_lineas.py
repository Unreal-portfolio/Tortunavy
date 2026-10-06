"""Modelo de ramas del 06-10: líneas de modo (`modo:*` → `dev-<modo>`), topes de lotes, regla de noche,
refactorización sin revisión y propagación de la organización. Sin llamadas a GitHub: gh siempre simulado; git real
solo en repositorios temporales."""

import argparse
import importlib.util
import json
import os
import subprocess
import sys
from datetime import date, datetime, timezone
from pathlib import Path

import pytest

CARPETA = Path(__file__).resolve().parents[1] / "tablero"
sys.path.insert(0, str(CARPETA))
import auditoria  # noqa: E402
import avisos  # noqa: E402
import base  # noqa: E402
import colisiones  # noqa: E402
import control  # noqa: E402
import control_avisos  # noqa: E402
import control_lotes  # noqa: E402
import flujo  # noqa: E402
import lotes  # noqa: E402
import memoria  # noqa: E402
import objetos  # noqa: E402
import organizacion  # noqa: E402

spec = importlib.util.spec_from_file_location("tablero_lineas", CARPETA / "tablero.py")
assert spec is not None and spec.loader is not None
tablero = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tablero)

LISTA = {"Revisión IA": "Aprobada", "Editor": "Funciona"}
SIN_VALIDAR = {"Revisión IA": "Pendiente", "Editor": "Sin probar"}


def _item(numero, status, *etiquetas, abierta=True, asignados=(), autor=None, miembros=(), **valores):
    """Item del Project como lo devuelve cargar_proyecto; `miembros` = [(número, estado)] si es un lote."""
    return {"number": numero, "title": f"Tarea {numero}", "state": "OPEN" if abierta else "CLOSED",
            "updatedAt": "2026-10-05T10:00:00Z", "valores": {**({"Status": status} if status else {}), **valores},
            "labels": {"nodes": [{"name": e} for e in etiquetas or ("tarea",)]},
            "assignees": {"nodes": [{"login": a} for a in asignados]},
            "author": {"login": autor} if autor else None,
            "blockedBy": {"nodes": [{"number": n, "state": s} for n, s in miembros]}, "blocking": {"nodes": []}}


def _lote(numero, miembros, autor="Mokius", *etiquetas):
    return _item(numero, None, "lote", *etiquetas, autor=autor, miembros=[(m, "OPEN") for m in miembros])


def _items(*issues):
    return {i["number"]: i for i in issues}


def _pr(numero, cuerpo="", base_="dev", rama="feat/x", **extra):
    return {"number": numero, "baseRefName": base_, "headRefName": rama, "body": cuerpo, "mergeable": "MERGEABLE",
            "author": {"login": "Mokius"}, "title": f"PR {numero}", "reviewDecision": None, "isDraft": False,
            "mergedAt": "2026-10-06T08:00:00Z", **extra}


def _args(**campos):
    return argparse.Namespace(**campos)


class Gh:
    """gh simulado: responde a las lecturas con `respuestas` (subcadena → salida) y apunta todas las llamadas."""

    def __init__(self, respuestas=None):
        self.respuestas, self.llamadas = respuestas or {}, []

    def __call__(self, *args, entrada=None):
        self.llamadas.append(tuple(a for a in args if a not in ("--repo", base.REPO)))
        texto = " ".join(args)
        return next((r for clave, r in self.respuestas.items() if clave in texto), "[]")

    def escrituras(self):
        lecturas = (("issue", "view"), ("label", "list"), ("api", "graphql"), ("pr", "list"), ("issue", "list"))
        return [a for a in self.llamadas if a[:2] not in lecturas]


# --- rama base: una única función -------------------------------------------------------------------

@pytest.mark.parametrize("issue, esperada", [
    (_item(1, "Ready"), "dev"),
    (_item(1, "Ready", "tarea", "modo:tct"), "dev-tct"),
    ({"labels": [{"name": "modo:rally"}]}, "dev-rally"),           # gh issue list / view
    ({"etiquetas": {"tarea", "modo:carrera"}}, "dev-carrera"),     # normalizada de la auditoría
    (_item(1, "Ready", "modo:vr"), "dev-vr"),
])
def test_rama_base_por_etiqueta_de_modo(issue, esperada):
    assert flujo.rama_base(issue) == esperada


@pytest.mark.parametrize("etiquetas", [("modo:desconocido",), ("modo-tct",), ("tct",), ("modo:TCT",)])
def test_una_etiqueta_que_no_es_de_un_modo_conocido_deja_la_issue_en_dev(etiquetas):
    assert flujo.rama_base(_item(1, "Ready", *etiquetas)) == "dev"


def test_con_varias_etiquetas_de_modo_manda_la_prioridad():
    assert flujo.rama_base(_item(1, "Ready", "modo:rally", "modo:tct")) == "dev-tct"


@pytest.mark.parametrize("rama, es", [("dev", True), ("dev-tct", True), ("dev-vr", True), ("dev-tct-42-x", False),
                                      ("main", False), ("chamber", False), ("dev-foo", False), (None, False)])
def test_es_rama_de_linea_solo_las_bases_exactas(rama, es):
    assert flujo.es_rama_de_linea(rama) is es


@pytest.mark.parametrize("issue, rama", [
    (_item(42, "Ready", "⚠️bug⚠️"), "fix/42-fallo"),
    (_item(42, "Ready", "tarea"), "feat/42-fallo"),
    (_item(42, "Ready", "⚠️bug⚠️", "modo:tct"), "dev-tct-42-fallo"),
    (_item(42, "Ready", "tarea", "modo:carrera"), "dev-carrera-42-fallo"),
])
def test_rama_de_trabajo_segun_la_linea(issue, rama):
    assert flujo.rama_de_trabajo(42, "fallo", issue) == rama


@pytest.mark.parametrize("rama, refs", [("dev-tct-42-cosa", {42}), ("feat/42-cosa", {42}), ("dev-tct", set()),
                                        ("org/propagar-20261006-main", set()), ("dev-foo-42-x", set())])
def test_issues_de_pr_lee_el_numero_de_las_ramas_de_modo(rama, refs):
    assert base.issues_de_pr({"body": "", "headRefName": rama}) == refs


# --- coger -----------------------------------------------------------------------------------------

def _coger(monkeypatch, issue, remotas=("dev-tct", "dev-carrera", "dev-rally"), locales=""):
    """Prepara cmd_coger con dobles; devuelve (llamadas a git, llamadas a gh, campos, comentarios)."""
    git_, campos, comentarios = [], [], []
    gh = Gh()

    def git(*args):
        git_.append(args)
        return locales if args[:2] == ("branch", "--list") else ""

    monkeypatch.setattr(tablero, "cargar_issue", lambda n: {"items": {n: issue}})
    monkeypatch.setattr(tablero, "poner_campo", lambda _p, n, c, v: campos.append((n, c, v)))
    monkeypatch.setattr(tablero, "usuario_actual", lambda: "Mokius")
    monkeypatch.delenv("TN_SESION_NOCTURNA", raising=False)
    monkeypatch.setattr(tablero, "existe_rama_remota", lambda rama: rama in remotas)
    monkeypatch.setattr(tablero, "git", git)
    monkeypatch.setattr(tablero, "gh", gh)
    monkeypatch.setattr(tablero, "comentar", lambda n, t: comentarios.append((n, t)))
    return git_, gh, campos, comentarios


def test_coger_una_issue_de_modo_crea_la_rama_desde_su_linea(monkeypatch, capsys):
    git_, gh, campos, _ = _coger(monkeypatch, _item(42, "Ready", "⚠️bug⚠️", "modo:tct"))
    tablero.cmd_coger(_args(numero=42, forzar=False, retomar=False, rama=None))
    assert ("fetch", "origin", "dev-tct") in git_
    assert ("switch", "-c", "dev-tct-42-tarea-42", "origin/dev-tct") in git_
    assert campos == [(42, "Status", "In progress")]
    assert "su PR va a dev-tct" in capsys.readouterr().out


def test_coger_de_la_linea_principal_sigue_igual(monkeypatch):
    git_, *_ = _coger(monkeypatch, _item(42, "Ready", "tarea"))
    tablero.cmd_coger(_args(numero=42, forzar=False, retomar=False, rama=None))
    assert ("switch", "-c", "feat/42-tarea-42", "origin/dev") in git_


def test_coger_con_rama_de_lote_en_un_modo_la_crea_desde_su_linea(monkeypatch):
    git_, *_ = _coger(monkeypatch, _item(43, "Ready", "tarea", "modo:rally"))
    tablero.cmd_coger(_args(numero=43, forzar=False, retomar=False, rama="dev-rally-42-lote"))
    assert ("switch", "-c", "dev-rally-42-lote", "origin/dev-rally") in git_


def test_coger_con_rama_de_lote_existente_entra_en_ella(monkeypatch):
    git_, *_ = _coger(monkeypatch, _item(43, "Ready", "tarea", "modo:rally"), locales="  dev-rally-42-lote")
    tablero.cmd_coger(_args(numero=43, forzar=False, retomar=False, rama="dev-rally-42-lote"))
    assert ("switch", "dev-rally-42-lote") in git_


def test_coger_vr_sin_rama_remota_se_rechaza_sin_tocar_nada(monkeypatch):
    git_, gh, campos, comentarios = _coger(monkeypatch, _item(42, "Backlog", "tarea", "modo:vr"))
    with pytest.raises(base.ErrorTablero, match="dev-vr") as error:
        tablero.cmd_coger(_args(numero=42, forzar=False, retomar=False, rama=None))
    assert "\n" not in str(error.value) and "aún no tiene rama" in str(error.value)
    assert git_ == [] and gh.llamadas == [] and campos == [] and comentarios == []


def test_coger_vr_con_forzar_saca_la_rama_de_chamber(monkeypatch, capsys):
    git_, *_ = _coger(monkeypatch, _item(42, "Backlog", "tarea", "modo:vr"))
    tablero.cmd_coger(_args(numero=42, forzar=True, retomar=False, rama=None))
    assert ("switch", "-c", "dev-vr-42-tarea-42", "origin/chamber") in git_
    assert "Aviso: dev-vr no existe" in capsys.readouterr().out


# --- sesiones nocturnas desatendidas ----------------------------------------------------------------

@pytest.mark.parametrize("bandera, entorno, nocturna", [
    (False, {}, False), (True, {}, True), (False, {"TN_SESION_NOCTURNA": "1"}, True),
    (False, {"TN_SESION_NOCTURNA": "0"}, False), (False, {"TN_SESION_NOCTURNA": ""}, False),
])
def test_es_sesion_nocturna_por_bandera_o_por_variable(bandera, entorno, nocturna):
    assert flujo.es_sesion_nocturna(bandera, entorno) is nocturna


@pytest.mark.parametrize("etiqueta, permitida", [("⚠️bug⚠️", True), ("bug", True), ("pulido", True),
                                                 ("refactor", True), ("tarea", False)])
def test_en_sesion_nocturna_solo_bugs_pulido_y_refactor(etiqueta, permitida):
    motivo = flujo.motivo_nocturno(42, _item(42, "Ready", etiqueta), nocturna=True)
    assert (motivo is None) is permitida
    assert flujo.motivo_nocturno(42, _item(42, "Ready", etiqueta), nocturna=False) is None


def test_no_es_una_franja_horaria():
    assert "hora" not in flujo.motivo_nocturno.__code__.co_varnames
    assert not hasattr(base, "ahora_local") and not hasattr(flujo, "es_de_noche")


@pytest.mark.parametrize("por_variable", [False, True])
def test_coger_una_tarea_en_sesion_nocturna_se_rechaza_sin_tocar_nada(monkeypatch, por_variable):
    git_, gh, campos, comentarios = _coger(monkeypatch, _item(42, "Ready", "tarea"))
    if por_variable:
        monkeypatch.setenv("TN_SESION_NOCTURNA", "1")
    with pytest.raises(base.ErrorTablero, match="sesión nocturna desatendida") as error:
        tablero.cmd_coger(_args(numero=42, forzar=False, retomar=False, rama=None, nocturna=not por_variable))
    assert "\n" not in str(error.value)
    assert git_ == [] and gh.llamadas == [] and campos == [] and comentarios == []


def test_coger_una_tarea_sin_sesion_nocturna_vale_a_cualquier_hora(monkeypatch):
    _, _, campos, comentarios = _coger(monkeypatch, _item(42, "Ready", "tarea"))
    tablero.cmd_coger(_args(numero=42, forzar=False, retomar=False, rama=None, nocturna=False))
    assert campos == [(42, "Status", "In progress")] and comentarios == []


@pytest.mark.parametrize("por_variable", [False, True])
def test_coger_en_sesion_nocturna_con_forzar_deja_un_comentario_de_una_linea(monkeypatch, por_variable):
    _, _, campos, comentarios = _coger(monkeypatch, _item(42, "Ready", "tarea"))
    if por_variable:
        monkeypatch.setenv("TN_SESION_NOCTURNA", "1")
    tablero.cmd_coger(_args(numero=42, forzar=True, retomar=False, rama=None, nocturna=not por_variable))
    assert campos == [(42, "Status", "In progress")]
    (numero, texto), = comentarios
    assert numero == 42 and texto.startswith("**Sesión nocturna**") and "\n" not in texto


@pytest.mark.parametrize("etiqueta", ["⚠️bug⚠️", "pulido", "refactor"])
def test_coger_en_sesion_nocturna_un_bug_pulido_o_refactor_no_comenta_nada(monkeypatch, etiqueta):
    _, _, campos, comentarios = _coger(monkeypatch, _item(42, "Ready", etiqueta))
    tablero.cmd_coger(_args(numero=42, forzar=False, retomar=False, rama=None, nocturna=True))
    assert campos == [(42, "Status", "In progress")] and comentarios == []


def test_la_bandera_nocturna_esta_en_coger():
    parser = argparse.ArgumentParser()
    tablero.anadir_comandos_de_flujo(parser.add_subparsers())
    assert parser.parse_args(["coger", "42", "--nocturna"]).nocturna is True
    assert parser.parse_args(["coger", "42"]).nocturna is False


# --- fusionada en su rama base ---------------------------------------------------------------------

def test_esta_fusionada_solo_en_su_rama_base():
    fusionadas = [_pr(900, "Closes #5", "dev-tct")]
    assert base.esta_fusionada(5, fusionadas, [], "dev-tct")
    assert not base.esta_fusionada(5, fusionadas, [], "dev")
    assert not base.esta_fusionada(5, fusionadas, [_pr(901, "Closes #5", "dev-tct")], "dev-tct")


def test_sync_mueve_una_issue_de_modo_con_su_pr_fusionada_en_su_linea(monkeypatch):
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [_pr(900, "Closes #5\nCloses #6", "dev-tct")])
    monkeypatch.setattr(tablero, "tiene_resumen", lambda n: True)
    proyecto = {"items": _items(_item(5, "Validada", "tarea", "modo:tct", **LISTA), _item(6, "Validada", **LISTA))}
    cambios = []
    tablero.reconciliar_fusiones(proyecto, [], cambios, [])
    assert [texto for texto, _ in cambios] == ["#5 → Done (PR #900 fusionada en dev-tct; revisada y probada: se cierra)"]


def test_sync_no_da_por_fusionada_una_issue_de_modo_que_entro_en_dev(monkeypatch):
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [_pr(900, "Closes #5", "dev")])
    proyecto = {"items": _items(_item(5, "Validada", "tarea", "modo:tct", **LISTA))}
    cambios = []
    tablero.reconciliar_fusiones(proyecto, [], cambios, [])
    assert cambios == []


def test_sync_decide_por_la_pr_de_su_linea_aunque_haya_una_mas_reciente_en_otra(monkeypatch):
    monkeypatch.setattr(tablero, "prs_fusionadas",
                        lambda: [_pr(901, "Closes #5", "dev"), _pr(900, "Closes #5", "dev-tct")])
    monkeypatch.setattr(tablero, "tiene_resumen", lambda n: True)
    proyecto = {"items": _items(_item(5, "Validada", "tarea", "modo:tct", **LISTA))}
    cambios = []
    tablero.reconciliar_fusiones(proyecto, [], cambios, [])
    assert [texto.split(" (")[0] for texto, _ in cambios] == ["#5 → Done"] and "PR #900" in cambios[0][0]


def test_aplicar_fusion_comenta_la_rama_base_de_la_issue(monkeypatch):
    campos, comentarios, gh = [], [], Gh()
    monkeypatch.setattr(tablero, "poner_campo", lambda _p, n, c, v: campos.append((n, c, v)))
    monkeypatch.setattr(tablero, "comentar", lambda n, t: comentarios.append(t))
    monkeypatch.setattr(tablero, "gh", gh)
    proyecto = {"items": _items(_item(5, "Validada", "tarea", "modo:carrera", **LISTA))}
    tablero.aplicar_fusion(proyecto, 5)
    assert comentarios == ["Fusionada en `dev-carrera`, revisada y probada: Done."]
    assert ("issue", "close", "5", "--reason", "completed") in gh.llamadas


def test_el_lote_se_cierra_con_su_pr_fusionada_en_una_linea_de_modo(monkeypatch):
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [_pr(900, "Closes #5\nRefs #130", "dev-tct")])
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [])
    monkeypatch.setattr(tablero, "tiene_resumen", lambda n: True)
    lote = _item(130, None, "lote", miembros=[(5, "CLOSED")])
    cambios = []
    tablero.reconciliar_lotes({"items": _items(lote)}, cambios, [])
    assert [texto for texto, _ in cambios] == ["lote #130 se cierra: PR #900 fusionada y todos sus miembros cerrados"]


def test_el_lote_no_se_cierra_con_una_pr_fusionada_fuera_de_las_lineas(monkeypatch):
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [_pr(900, "Refs #130", "main")])
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [])
    cambios = []
    tablero.reconciliar_lotes({"items": _items(_item(130, None, "lote", miembros=[(5, "CLOSED")]))}, cambios, [])
    assert cambios == []


def _ia_aprobada(monkeypatch, issue, fusionadas):
    campos, gh = [], Gh()
    monkeypatch.setattr(tablero, "cargar_issue", lambda n: {"items": {n: issue}})
    monkeypatch.setattr(tablero, "poner_campo", lambda _p, n, c, v: campos.append((n, c, v)))
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: fusionadas)
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [])
    monkeypatch.setattr(tablero, "comentar", lambda *a: None)
    monkeypatch.setattr(tablero, "gh", gh)
    tablero.cmd_ia(_args(numero=5, veredicto="aprobada", revisor="Mokius (Claude)", nota=None))
    return campos


def test_ia_aprobada_cierra_una_de_modo_fusionada_en_su_linea(monkeypatch):
    issue = _item(5, "In review", "tarea", "modo:tct", Editor="Funciona", Revisor="Mokius")
    campos = _ia_aprobada(monkeypatch, issue, [_pr(900, "Closes #5", "dev-tct")])
    assert (5, "Status", "Done") in campos


def test_ia_aprobada_no_cierra_una_de_modo_fusionada_en_dev(monkeypatch):
    issue = _item(5, "In review", "tarea", "modo:tct", Editor="Funciona", Revisor="Mokius")
    campos = _ia_aprobada(monkeypatch, issue, [_pr(900, "Closes #5", "dev")])
    assert (5, "Status", "Validada") in campos and (5, "Status", "Done") not in campos


@pytest.mark.parametrize("base_pr, estado", [("dev-rally", "Done"), ("dev", "QA editor")])
def test_editor_funciona_usa_la_rama_base(monkeypatch, base_pr, estado):
    issue = _item(5, "QA editor", "tarea", "modo:rally", **{"Revisión IA": "Aprobada"}, Editor="Sin probar")
    campos = []
    monkeypatch.setattr(tablero, "cargar_issue", lambda n: {"items": {n: issue}})
    monkeypatch.setattr(tablero, "poner_campo", lambda _p, n, c, v: campos.append((n, c, v)))
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [_pr(900, "Closes #5", base_pr)])
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [])
    monkeypatch.setattr(tablero, "comentar", lambda *a: None)
    monkeypatch.setattr(tablero, "gh", Gh())
    tablero.cmd_editor(_args(numero=5, resultado="funciona", como="PIE", nota=None))
    assert (estado == "Done") is ((5, "Status", "Done") in campos)


def test_auditar_juzga_la_fusion_en_la_rama_base_de_cada_issue():
    nodos = [{"number": n, "labels": {"nodes": [{"name": "tarea"}, *etiquetas]}, "assignees": {"nodes": []},
              "blockedBy": {"nodes": []}} for n, etiquetas in ((5, [{"name": "modo:tct"}]), (6, []))]
    fusionadas = [_pr(900, "Closes #5\nCloses #6", "dev-tct")]
    contexto = control.contexto_prs(nodos, {"items": {}}, [], fusionadas)
    assert contexto[5]["fusionada"] is True and contexto[6]["fusionada"] is False


def test_sync_avisa_de_una_pr_con_otra_base_que_su_issue(monkeypatch):
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [])
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [_pr(400, "Closes #5", "dev"), _pr(401, "Closes #6", "dev-tct"),
                                                         _pr(402, "Closes #7", "rama-rara")])
    proyecto = {"items": _items(_item(5, "In progress", "tarea", "modo:tct"), _item(6, "In progress", "tarea", "modo:tct"),
                                _item(7, "In progress"))}
    avisos_ = []
    tablero.reconciliar_prs(proyecto, [], avisos_)
    assert any(a.startswith("PR #400 va a dev y #5 es de dev-tct") for a in avisos_)
    assert not any("PR #401" in a for a in avisos_)
    assert any(a.startswith("PR #402 apunta a rama-rara, que no es una rama de línea") for a in avisos_)


def test_sync_no_avisa_de_la_pr_de_dev_a_main_ni_de_las_de_organizacion(monkeypatch):
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [])
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [_pr(500, "", "main", rama="dev"),
                                                         _pr(501, "", "main", rama="org/propagar-20261006-main"),
                                                         _pr(502, "", "dev-tct", rama="org/propagar-20261006-dev-tct")])
    avisos_ = []
    tablero.reconciliar_prs({"items": {}}, [], avisos_)
    assert avisos_ == []


# --- avisos ----------------------------------------------------------------------------------------

def _fusionada(numero, refs, base_="dev", ficheros=("Source/a.cpp",)):
    return {"number": numero, "refs": set(refs), "files": [{"path": f} for f in ficheros], "baseRefName": base_,
            "mergedBy": {"login": "Mokius"}, "mergedAt": "2026-10-05T20:00:00Z"}


def test_avisos_juzga_una_pr_de_modo_en_su_linea():
    items = _items(_item(5, "Done", "tarea", "modo:tct", **LISTA), _item(6, "Done", **LISTA))
    assert avisos.pr_sin_validar(_fusionada(1, [5], "dev-tct"), items, [], set()) is None
    linea = avisos.pr_sin_validar(_fusionada(2, [6], "dev-tct"), items, [], set())
    assert linea.startswith("PR #2 fusionada en dev-tct") and "#6 (es de dev, no de dev-tct)" in linea


def test_avisos_lee_las_pr_fusionadas_en_todas_las_lineas(monkeypatch):
    prs = [{"number": n, "body": f"Closes #{n}", "headRefName": "x", "baseRefName": b, "mergedAt": "2026-10-06T08:00:00Z",
            "mergedBy": {"login": "Mokius"}, "files": [{"path": "Source/a.cpp"}]}
           for n, b in ((1, "dev"), (2, "dev-tct"), (3, "main"), (4, "dev-tct-9-x"))]
    gh = Gh({"pr list": json.dumps(prs)})
    monkeypatch.setattr(control_avisos, "gh", gh)
    items = _items(_item(1, "Done", **SIN_VALIDAR), _item(2, "Done", "tarea", "modo:tct", **SIN_VALIDAR),
                   _item(3, "Done", **SIN_VALIDAR), _item(4, "Done", "tarea", "modo:tct", **SIN_VALIDAR))
    lineas = control_avisos.prs_sin_validar({"items": items}, datetime(2026, 10, 5, tzinfo=timezone.utc))
    assert [linea.split(" fusionada")[0] for linea in lineas] == ["PR #1", "PR #2"]
    assert "--base" not in gh.llamadas[0]


def test_linea_pr_no_marca_las_pr_a_una_linea_de_modo():
    pr = {"number": 1, "title": "x", "author": {"login": "Mokius"}, "state": "OPEN"}
    assert "hacia" not in avisos.linea_pr({**pr, "baseRefName": "dev-tct"}, "dev")
    assert "hacia `main`" in avisos.linea_pr({**pr, "baseRefName": "main"}, "dev")


# --- colisiones ------------------------------------------------------------------------------------

def test_pares_solo_entre_pr_de_la_misma_base():
    ficheros = {1: {"a.cpp"}, 2: {"a.cpp"}, 3: {"a.cpp"}, 4: {"a.cpp"}}
    bases = {1: "dev", 2: "dev", 3: "dev-tct", 4: "dev-rally"}
    assert colisiones.pares_por_base(ficheros, bases) == [(1, 2, ["a.cpp"])]
    assert colisiones.pares_por_base(ficheros, {n: "dev" for n in ficheros}) == colisiones.pares(ficheros)


def test_cmd_colisiones_compara_por_base_y_cuenta_las_pr_de_modo(monkeypatch, capsys):
    monkeypatch.setattr(control, "numeros_chamber", lambda: set())
    monkeypatch.setattr(control, "prs_abiertas", lambda: [_pr(400, base_="dev"), _pr(401, base_="dev-tct"),
                                                          _pr(402, base_="dev-tct"), _pr(403, base_="main")])
    traidas = []
    monkeypatch.setattr(colisiones, "traer_cabezas", lambda prs: traidas.append(set(prs)) or True)
    monkeypatch.setattr(colisiones, "ficheros_de_pr", lambda _gh, _repo, n: {"Source/a.cpp"})
    monkeypatch.setattr(colisiones, "conflicto_git", lambda a, b: ["Source/a.cpp"])
    monkeypatch.setattr(colisiones, "colisiones_abiertas", lambda _gh, _repo: [])
    control.cmd_colisiones(_args(aplicar=False))
    salida = capsys.readouterr().out
    assert traidas == [{400, 401, 402}]
    assert "3 PR, 1 pares en conflicto" in salida and "PR #401 y #402" in salida and "#400 y" not in salida


def test_la_issue_de_colision_cita_la_base_de_las_pr(monkeypatch):
    creadas = []
    monkeypatch.setattr(control, "gh", lambda *a, **k: creadas.append(a) or "https://x/issues/77")
    monkeypatch.setattr(control, "objeto_y_area", lambda _p, _pr: (None, None))
    monkeypatch.setattr(control, "poner_campo", lambda *a: None)
    control.crear_issue_colision({"items": {}}, _pr(401, base_="dev-tct", rama="dev-tct-5-a"),
                                 _pr(402, base_="dev-tct", rama="dev-tct-6-b"), ["Source/a.cpp"])
    cuerpo = creadas[0][creadas[0].index("--body") + 1]
    assert "abiertas contra `dev-tct`" in cuerpo


# --- topes de lotes --------------------------------------------------------------------------------

def test_lotes_abiertos_personas_y_fuera_de_topes():
    items = _items(_lote(130, [5, 6], autor="Ruben-Besteiro"), _item(5, "In progress", asignados=["Mokius"]),
                   _item(6, "In progress"), _lote(131, [7, 8], "SkiTemplar", "excepcion"),
                   _lote(132, [9], "SkiTemplar"), _item(9, "In progress", "refactor"),
                   {**_lote(133, [5], "Mokius"), "state": "CLOSED"})
    abiertos = lotes.lotes_abiertos(items)
    assert set(abiertos) == {130, 131, 132}
    assert abiertos[130] == {"miembros": [5, 6], "personas": {"Ruben-Besteiro", "Mokius"}, "fuera": False}
    assert abiertos[131]["fuera"] and abiertos[132]["fuera"]


def test_un_miembro_cerrado_no_hace_suyo_el_lote():
    items = _items(_item(130, None, "lote", autor="Mokius", miembros=[(5, "CLOSED")]),
                   _item(5, "Done", asignados=["Ruben-Besteiro"], abierta=False))
    assert lotes.lotes_abiertos(items)[130]["personas"] == {"Mokius"}


def test_excesos_y_el_lote_que_no_cuenta():
    abiertos = {130: {"miembros": [5, 6], "personas": {"Mokius"}, "fuera": False},
                131: {"miembros": [7], "personas": {"Ruben-Besteiro"}, "fuera": True}}
    assert lotes.excesos(3, {"SkiTemplar"}, abiertos) == []
    assert lotes.excesos(4, {"SkiTemplar"}, abiertos) == ["4 issues (máximo 3 por lote)"]
    assert lotes.excesos(2, {"Mokius"}, abiertos) == ["Mokius ya tiene abierto el lote #130 (máximo 1 por persona)"]
    assert lotes.excesos(2, {"Mokius"}, abiertos, excluir=130) == [], "su propio lote no cuenta"
    assert lotes.excesos(2, {"Ruben-Besteiro"}, abiertos) == [], "un lote con excepción no cuenta"


def test_incumplimientos_marca_el_lote_grande_y_el_segundo_de_una_persona():
    abiertos = {130: {"miembros": [1, 2], "personas": {"Mokius"}, "fuera": False},
                131: {"miembros": [3, 4], "personas": {"Mokius"}, "fuera": False},
                132: {"miembros": [5, 6, 7, 8], "personas": {"SkiTemplar"}, "fuera": False},
                133: {"miembros": [9, 10, 11, 12], "personas": {"SkiTemplar"}, "fuera": True}}
    malos = lotes.incumplimientos(abiertos)
    assert set(malos) == {131, 132}
    assert malos[131] == ["Mokius ya tiene abierto el lote #130 (máximo 1 por persona)"]


@pytest.mark.parametrize("quien, autoriza, esperado", [("SkiTemplar", None, "SkiTemplar"),
                                                      ("Ruben-Besteiro", "Mokius", "Mokius")])
def test_autorizacion_de_la_excepcion(quien, autoriza, esperado):
    assert lotes.autorizacion(quien, autoriza, ["SkiTemplar", "Mokius"]) == esperado


@pytest.mark.parametrize("quien, autoriza", [("Ruben-Besteiro", None), ("Ruben-Besteiro", "Ruben-Besteiro")])
def test_autorizacion_sin_aprobador_falla(quien, autoriza):
    with pytest.raises(lotes.ErrorLote):
        lotes.autorizacion(quien, autoriza, ["SkiTemplar", "Mokius"])


def test_texto_excepcion_de_una_linea_y_sin_motivo_falla():
    texto = lotes.texto_excepcion(["4 issues (máximo 3 por lote)"], "Emergencia\n de la demo", "Mokius")
    assert "\n" not in texto and texto.startswith("**Excepción a los topes de lotes**") and "Autoriza Mokius" in texto
    with pytest.raises(lotes.ErrorLote):
        lotes.texto_excepcion(["x"], "  ", "Mokius")


def _crear_lote(monkeypatch, items, miembros, quien="Mokius", **opciones):
    """Lanza `lote crear` con dobles; devuelve (gh, comentarios)."""
    gh, comentarios = Gh({"issue create": "https://github.com/x/y/issues/140\n", "label list": "[]"}), []
    monkeypatch.setattr(control_lotes, "gh", gh)
    monkeypatch.setattr(control_lotes, "cargar_proyecto", lambda: {"items": items})
    monkeypatch.setattr(control_lotes, "quien_lanza", lambda: quien)
    monkeypatch.setattr(control_lotes, "prs_abiertas", lambda: [])
    monkeypatch.setattr(control_lotes, "item_de_issue", lambda *a: None)
    monkeypatch.setattr(control_lotes, "comentar", lambda n, t: comentarios.append((n, t)))
    monkeypatch.setattr(objetos, "leer_issue", lambda _gh, _repo, n: {"id": f"ID{n}", "number": n})
    control_lotes.cmd_lote_crear(_args(titulo="x", pr=None, miembros=list(miembros),
                                       **{"excepcion": None, "autoriza": None, **opciones}))
    return gh, comentarios


def _trabajo(*numeros, etiqueta="tarea", asignados=()):
    return [_item(n, "In progress", etiqueta, asignados=asignados) for n in numeros]


def test_lote_crear_de_tres_cabe(monkeypatch):
    gh, comentarios = _crear_lote(monkeypatch, _items(*_trabajo(5, 6, 7)), [5, 6, 7])
    assert any(a[:2] == ("issue", "create") for a in gh.llamadas)
    assert not any("excepcion" in a for a in gh.escrituras()) and len(comentarios) == 3


def test_lote_crear_de_cuatro_se_rechaza_sin_crear_nada(monkeypatch):
    with pytest.raises(base.ErrorTablero, match="4 issues") as error:
        _crear_lote(monkeypatch, _items(*_trabajo(5, 6, 7, 8)), [5, 6, 7, 8])
    assert "--excepcion" in str(error.value) and "\n" not in str(error.value)


def test_lote_crear_con_un_lote_abierto_de_la_misma_persona_se_rechaza(monkeypatch):
    items = _items(*_trabajo(5, 6), *_trabajo(7, asignados=["Ruben-Besteiro"]), _lote(130, [7], "SkiTemplar"))
    with pytest.raises(base.ErrorTablero, match="Ruben-Besteiro ya tiene abierto el lote #130"):
        _crear_lote(monkeypatch, items, [5, 6], quien="Ruben-Besteiro")
    with pytest.raises(base.ErrorTablero, match="SkiTemplar ya tiene abierto el lote #130"):
        _crear_lote(monkeypatch, items, [5, 6], quien="SkiTemplar")


def test_lote_crear_con_excepcion_de_un_aprobador_etiqueta_y_comenta(monkeypatch):
    gh, comentarios = _crear_lote(monkeypatch, _items(*_trabajo(5, 6, 7, 8)), [5, 6, 7, 8], quien="SkiTemplar",
                                  excepcion="Emergencia de la demo")
    assert ("issue", "edit", "140", "--add-label", "excepcion") in gh.llamadas
    texto = dict(comentarios)[140]
    assert "\n" not in texto and "4 issues" in texto and "Emergencia de la demo" in texto and "SkiTemplar" in texto


def test_lote_crear_con_excepcion_sin_aprobador_no_crea_nada(monkeypatch):
    gh = Gh()
    monkeypatch.setattr(control_lotes, "gh", gh)
    with pytest.raises(base.ErrorTablero, match="--autoriza"):
        _crear_lote(monkeypatch, _items(*_trabajo(5, 6, 7, 8)), [5, 6, 7, 8], quien="Ruben-Besteiro",
                    excepcion="Prisa")


def test_lote_crear_con_excepcion_autorizada_por_un_aprobador(monkeypatch):
    gh, comentarios = _crear_lote(monkeypatch, _items(*_trabajo(5, 6, 7, 8)), [5, 6, 7, 8], quien="Ruben-Besteiro",
                                  excepcion="Prisa", autoriza="Mokius")
    assert "Autoriza Mokius" in dict(comentarios)[140]


def test_lote_crear_con_excepcion_que_no_hace_falta_no_etiqueta(monkeypatch, capsys):
    gh, comentarios = _crear_lote(monkeypatch, _items(*_trabajo(5, 6)), [5, 6], excepcion="por si acaso")
    assert not any("excepcion" in a for a in gh.escrituras())
    assert "no hace falta la excepción" in capsys.readouterr().out


def test_lote_de_solo_refactor_no_tiene_topes(monkeypatch):
    items = _items(*_trabajo(5, 6, 7, 8, etiqueta="refactor"), _lote(130, [9], "Mokius"))
    gh, _ = _crear_lote(monkeypatch, items, [5, 6, 7, 8])
    assert any(a[:2] == ("issue", "create") for a in gh.llamadas)


def _anadir(monkeypatch, items, actuales, nuevos, **opciones):
    vista = json.dumps({"title": "Lote: x", "labels": [{"name": "lote"}], "state": "OPEN", "body": "- [ ] #5"})
    gh, comentarios = Gh({"issue view": vista, "label list": "[]"}), []
    monkeypatch.setattr(control_lotes, "gh", gh)
    monkeypatch.setattr(control_lotes, "cargar_proyecto", lambda: {"items": items})
    monkeypatch.setattr(control_lotes, "prs_abiertas", lambda: [])
    monkeypatch.setattr(control_lotes, "comentar", lambda n, t: comentarios.append((n, t)))
    monkeypatch.setattr(control_lotes, "quien_lanza", lambda: "SkiTemplar")
    monkeypatch.setattr(objetos, "leer_issue", lambda _gh, _repo, n: {
        "id": f"ID{n}", "number": n, "blockedBy": {"nodes": [{"number": m, "state": "OPEN"} for m in actuales]
                                                   if n == 130 else []}})
    control_lotes.cmd_lote_anadir(_args(lote=130, miembros=list(nuevos), **{"excepcion": None, "autoriza": None,
                                                                             **opciones}))
    return gh, comentarios


def test_lote_anadir_un_cuarto_se_rechaza(monkeypatch):
    items = _items(_lote(130, [5, 6, 7]), *_trabajo(5, 6, 7, 8))
    with pytest.raises(base.ErrorTablero, match="4 issues"):
        _anadir(monkeypatch, items, [5, 6, 7], [8])


def test_lote_anadir_un_miembro_de_quien_ya_tiene_otro_lote_se_rechaza(monkeypatch):
    items = _items(_lote(130, [5]), _lote(131, [9], "Ruben-Besteiro"), *_trabajo(5),
                   *_trabajo(8, asignados=["Ruben-Besteiro"]))
    with pytest.raises(base.ErrorTablero, match="Ruben-Besteiro ya tiene abierto el lote #131"):
        _anadir(monkeypatch, items, [5], [8])


def test_lote_anadir_con_excepcion_marca_el_lote(monkeypatch):
    items = _items(_lote(130, [5, 6, 7]), *_trabajo(5, 6, 7, 8))
    gh, comentarios = _anadir(monkeypatch, items, [5, 6, 7], [8], excepcion="Los cuatro van juntos")
    assert ("issue", "edit", "130", "--add-label", "excepcion") in gh.llamadas
    assert any(n == 130 and t.startswith("**Excepción") for n, t in comentarios)


def test_lote_anadir_a_un_lote_con_excepcion_no_tiene_topes(monkeypatch):
    items = _items(_lote(130, [5, 6, 7], "Mokius", "excepcion"), *_trabajo(5, 6, 7, 8))
    gh, _ = _anadir(monkeypatch, items, [5, 6, 7], [8])
    assert not any(a[:4] == ("issue", "edit", "130", "--add-label") for a in gh.llamadas)


def test_las_banderas_de_excepcion_estan_en_lote_crear_y_anadir():
    parser = argparse.ArgumentParser()
    control_lotes.anadir_comandos(parser.add_subparsers())
    args = parser.parse_args(["lote", "crear", "--titulo", "x", "5", "6", "--excepcion", "y", "--autoriza", "Mokius"])
    assert args.excepcion == "y" and args.autoriza == "Mokius"
    assert parser.parse_args(["lote", "añadir", "130", "8", "--excepcion", "z"]).excepcion == "z"
    with pytest.raises(SystemExit):
        parser.parse_args(["lote", "crear", "--titulo", "x", "5", "6", "--autoriza", "Ruben-Besteiro"])


def _nodo(numero, *etiquetas, autor="Mokius", miembros=(), asignados=(), estado="OPEN"):
    """Nodo de la auditoría (CONSULTA_ISSUES)."""
    return {"number": numero, "title": f"Lote {numero}", "body": "", "state": estado, "stateReason": None,
            "closedAt": None, "author": {"login": autor}, "labels": {"nodes": [{"name": e} for e in etiquetas]},
            "assignees": {"nodes": [{"login": a} for a in asignados]}, "parent": None,
            "blockedBy": {"nodes": [{"number": m, "state": "OPEN"} for m in miembros]}, "blocking": {"nodes": []},
            "comments": {"nodes": []}}


def test_auditar_marca_los_lotes_fuera_de_topes_sin_excepcion():
    nodos = [_nodo(130, "lote", miembros=[1, 2]), _nodo(131, "lote", miembros=[3]),
             _nodo(132, "lote", "excepcion", autor="SkiTemplar", miembros=[4, 5, 6, 7]),
             _nodo(133, "lote", autor="Ruben-Besteiro", miembros=[8, 9, 10, 11]),
             _nodo(134, "lote", autor="SkiTemplar", miembros=[12, 13, 14, 15]),
             *(_nodo(n, "refactor") for n in (12, 13, 14, 15))]
    informe = {i["numero"]: lista for i, lista in control.lotes_auditables(nodos)}
    assert set(informe) == {130, 131, 132, 133, 134}
    assert informe[130] == [] and informe[132] == [] and informe[134] == []
    assert "Mokius ya tiene abierto el lote #130" in informe[131][0]["texto"]
    assert "4 issues (máximo 3 por lote)" in informe[133][0]["texto"]
    assert all(p["tipo"] == "organizacion" for p in informe[131] + informe[133])


def test_un_lote_de_refactor_sigue_exento_aunque_un_miembro_cerrado_no_este_cargado():
    lote = _nodo(134, "lote", autor="SkiTemplar", miembros=[12, 13, 14, 15])
    lote["blockedBy"]["nodes"][-1]["state"] = "CLOSED"  # cerrada hace más de 14 días: la auditoría no la lee
    nodos = [_nodo(130, "lote", autor="SkiTemplar", miembros=[1]), lote, *(_nodo(n, "refactor") for n in (12, 13, 14))]
    informe = {i["numero"]: lista for i, lista in control.lotes_auditables(nodos)}
    assert informe[134] == []
    assert lotes.lotes_abiertos({n["number"]: n for n in nodos})[134]["fuera"]


def test_un_miembro_abierto_sin_cargar_sigue_quitando_la_exencion():
    lote = _nodo(134, "lote", autor="SkiTemplar", miembros=[12, 13, 14, 15])
    nodos = [lote, *(_nodo(n, "refactor") for n in (12, 13, 14))]
    assert not lotes.lotes_abiertos({n["number"]: n for n in nodos})[134]["fuera"]


@pytest.mark.parametrize("etiquetas_lote, miembros, fuera", [
    ({"lote", "refactor"}, [], True), ({"lote"}, [], False), ({"lote", "excepcion"}, [{"tarea"}], True),
    ({"lote", "refactor"}, [{"tarea"}], False), ({"lote"}, [{"refactor"}, {"refactor"}], True)])
def test_fuera_de_topes_sin_miembros_conocidos_decide_la_etiqueta_del_lote(etiquetas_lote, miembros, fuera):
    assert lotes.fuera_de_topes(etiquetas_lote, miembros) is fuera


def test_auditar_etiqueta_el_lote_que_incumple_y_desetiqueta_el_que_ya_cumple():
    nodos = [_nodo(130, "lote", miembros=[1, 2, 3, 4]), _nodo(131, "lote", auditoria.ETIQUETA, autor="X", miembros=[5])]
    informe = dict((i["numero"], (i, lista)) for i, lista in control.lotes_auditables(nodos))
    accion = auditoria.acciones(*informe[130])
    assert accion["etiquetar"] and len(accion["comentarios"]) == 1 and accion["campos"] == {}
    assert auditoria.acciones(*informe[131])["desetiquetar"]


def test_la_consulta_de_la_auditoria_trae_el_autor():
    assert "author { login }" in auditoria.CONSULTA_ISSUES and "author { login }" in base.CONSULTA_ITEMS


# --- refactorización -------------------------------------------------------------------------------

@pytest.mark.parametrize("fusionada, esperado", [(True, ("Done", True)), (False, (None, False))])
def test_estado_objetivo_de_una_refactorizacion(fusionada, esperado):
    assert flujo.estado_objetivo("In progress", SIN_VALIDAR, fusionada, False, refactor=True) == esperado


def test_una_refactorizacion_con_un_fallo_sigue_yendo_a_revisiones():
    assert flujo.estado_objetivo("Done", {"Editor": "Falla"}, True, False, refactor=True) == ("Revisiones", False)


def test_sync_lleva_a_done_una_refactorizacion_fusionada_sin_validar(monkeypatch):
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [_pr(900, "Closes #5\nCloses #6", "dev")])
    monkeypatch.setattr(tablero, "tiene_resumen", lambda n: n == 5)
    proyecto = {"items": _items(_item(5, "In progress", "refactor", **SIN_VALIDAR),
                                _item(6, "In progress", "tarea", **SIN_VALIDAR))}
    cambios, avisos_ = [], []
    tablero.reconciliar_fusiones(proyecto, [], cambios, avisos_)
    assert [texto for texto, _ in cambios] == [
        "#5 → Done (PR #900 fusionada en dev; refactor: se cierra sin revisión ni prueba)"]
    assert avisos_ == []


def _sync_refactor(monkeypatch, fusionadas, abiertas=(), status="In progress"):
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: list(fusionadas))
    monkeypatch.setattr(tablero, "tiene_resumen", lambda n: True)
    proyecto = {"items": _items(_item(5, status, "refactor", **SIN_VALIDAR))}
    cambios = []
    tablero.reconciliar_fusiones(proyecto, list(abiertas), cambios, [])
    return [texto for texto, _ in cambios]


def test_sync_no_cierra_una_refactorizacion_en_curso_por_una_etapa_fusionada_sin_closes(monkeypatch):
    etapa = _pr(900, "Refs #5: etapa 1", rama="refactor/5-recorte")
    assert _sync_refactor(monkeypatch, [etapa]) == []


def test_sync_no_cierra_una_refactorizacion_por_un_closes_antiguo_tras_una_etapa_mas_reciente(monkeypatch):
    antigua = _pr(880, "Closes #5", mergedAt="2026-10-01T08:00:00Z")
    etapa = _pr(900, "Refs #5: etapa 2", rama="refactor/5-recorte", mergedAt="2026-10-06T08:00:00Z")
    assert _sync_refactor(monkeypatch, [antigua, etapa]) == [], "gh no las devuelve por fecha de fusión"


def test_sync_no_cierra_una_refactorizacion_con_una_pr_abierta_que_la_enlaza(monkeypatch):
    abierta = _pr(901, "Refs #5", rama="refactor/5-x")
    assert _sync_refactor(monkeypatch, [_pr(880, "Closes #5")], abiertas=[abierta]) == []


def test_sync_cierra_una_refactorizacion_cuando_su_ultima_pr_fusionada_la_cierra(monkeypatch):
    etapa = _pr(880, "Refs #5", rama="refactor/5-recorte", mergedAt="2026-10-01T08:00:00Z")
    final = _pr(900, "Closes #5", mergedAt="2026-10-06T08:00:00Z")
    assert _sync_refactor(monkeypatch, [etapa, final]) == [
        "#5 → Done (PR #900 fusionada en dev; refactor: se cierra sin revisión ni prueba)"]


@pytest.mark.parametrize("vigente, mueve", [(True, True), (False, False)])
def test_mueve_por_fusion_de_una_refactorizacion_en_curso_exige_una_fusion_vigente(vigente, mueve):
    assert flujo.mueve_por_fusion("In progress", False, refactor=True, cierre_vigente=vigente) is mueve
    assert not flujo.mueve_por_fusion("In progress", True, refactor=True, cierre_vigente=True)
    assert not flujo.mueve_por_fusion("Done", False, refactor=True, cierre_vigente=True)


def test_aplicar_fusion_de_una_refactorizacion_no_toca_el_editor(monkeypatch):
    campos, comentarios, gh = [], [], Gh()
    monkeypatch.setattr(tablero, "poner_campo", lambda _p, n, c, v: campos.append((n, c, v)))
    monkeypatch.setattr(tablero, "comentar", lambda n, t: comentarios.append(t))
    monkeypatch.setattr(tablero, "gh", gh)
    tablero.aplicar_fusion({"items": _items(_item(5, "In progress", "refactor", "modo:tct", **SIN_VALIDAR))}, 5)
    assert campos == [(5, "Status", "Done")]
    assert comentarios == ["Fusionada en `dev-tct`, refactor, sin revisión ni prueba: Done."]
    assert ("issue", "close", "5", "--reason", "completed") in gh.llamadas


def test_una_pr_abierta_no_manda_una_refactorizacion_a_in_review(monkeypatch):
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [])
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [_pr(400, "Closes #5\nCloses #6")])
    proyecto = {"items": _items(_item(5, "In progress", "refactor"), _item(6, "In progress"))}
    cambios = []
    tablero.reconciliar_prs(proyecto, cambios, [])
    assert [texto for texto, _ in cambios] == ["#6 → In review (PR #400)"]


def test_avisos_de_validacion_no_cuentan_las_refactorizaciones():
    proyecto = {"items": _items(_item(5, "In review", "refactor"), _item(6, "In review"))}
    avisos_ = []
    tablero.avisos_validacion(proyecto, avisos_)
    assert avisos_ == ["En review sin revisión de una segunda IA: #6"]


def test_revision_de_una_refactorizacion_no_pide_revisor(monkeypatch, capsys):
    campos = []
    monkeypatch.setattr(tablero, "cargar_issue", lambda n: {"items": {n: _item(n, "In progress", "refactor")}})
    monkeypatch.setattr(tablero, "poner_campo", lambda *a: campos.append(a))
    monkeypatch.setattr(tablero, "usuario_actual", lambda: pytest.fail("no busca revisor"))
    tablero.cmd_revision(_args(numero=5, revisor=None))
    salida = capsys.readouterr().out
    assert campos == [] and "no pasa por revisión cruzada" in salida and "Fusiona su PR en dev" in salida


def test_revision_de_una_tarea_sigue_pidiendo_revisor(monkeypatch):
    campos = []
    monkeypatch.setattr(tablero, "cargar_issue", lambda n: {"items": {n: _item(n, "In progress", **LISTA)}})
    monkeypatch.setattr(tablero, "poner_campo", lambda _p, n, c, v: campos.append((c, v)))
    monkeypatch.setattr(tablero, "usuario_actual", lambda: "SkiTemplar")
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [])
    monkeypatch.setattr(tablero, "comentar", lambda *a: None)
    tablero.cmd_revision(_args(numero=5, revisor=None))
    assert ("Status", "In review") in campos and ("Revisor", "Mokius") in campos


def _auditada(estado, *etiquetas, **contexto):
    return {"numero": 5, "titulo": "Tarea", "estado": estado, "etiquetas": {"tarea", *etiquetas},
            "valores": {"Status": "In progress", **SIN_VALIDAR}, "cerrada": datetime(2026, 10, 5, tzinfo=timezone.utc),
            "motivo_cierre": "COMPLETED", "comentarios": [memoria.texto_resumen("x", "y")], "asignados": ["Mokius"],
            "padre": 1,
            "cuerpo": "x\n- [ ] y", "bloqueantes": [], **contexto}


def test_auditar_no_da_por_grave_una_refactorizacion_cerrada_sin_probar():
    ahora = datetime(2026, 10, 6, tzinfo=timezone.utc)
    assert auditoria.problemas(_auditada("CLOSED", "refactor"), ahora) == []
    assert [p["tipo"] for p in auditoria.problemas(_auditada("CLOSED"), ahora)] == ["grave"]


def test_auditar_no_mueve_una_refactorizacion_fusionada_ni_la_trata_como_grave():
    issue = _auditada("OPEN", "refactor", fusionada=True, lote_fusionado=900, lotes=[130])
    assert auditoria.graves_abierta(issue) == [] and auditoria.columna_correcta(issue) is None
    assert auditoria.graves_abierta({**issue, "etiquetas": {"tarea"}}) != []


def test_avisos_no_da_por_incidencia_una_refactorizacion():
    items = _items(_item(5, "Done", "refactor", **SIN_VALIDAR), _item(6, "Done", **SIN_VALIDAR))
    assert avisos.pr_sin_validar(_fusionada(1, [5]), items, [], set()) is None
    assert avisos.pr_sin_validar(_fusionada(1, [5], ficheros=("Source/a.cpp",)), items, [], {130}) is None
    assert "#6 (Revisión IA = Pendiente)" in avisos.pr_sin_validar(_fusionada(2, [6]), items, [], set())


def test_lote_estado_deja_fusionar_con_miembros_refactor_sin_validar(monkeypatch, capsys):
    miembros = [{"number": n, "state": "OPEN", "labels": {"nodes": [{"name": "refactor"}]}} for n in (5, 6)]

    def gh(*args, **_k):
        if args[:2] == ("issue", "view"):
            return json.dumps({"title": "Lote: x", "labels": [{"name": "lote"}], "state": "OPEN"})
        return json.dumps({"data": {"repository": {"issue": {"id": "I", "number": 130,
                                                              "blockedBy": {"nodes": miembros}}}}})

    monkeypatch.setattr(control_lotes, "gh", gh)
    monkeypatch.setattr(control_lotes, "cargar_proyecto", lambda: {"items": _items(
        _item(5, "In progress", "refactor", **SIN_VALIDAR), _item(6, "In progress", "refactor", **SIN_VALIDAR))})
    control_lotes.cmd_lote_estado(_args(numero=130))
    assert "se puede fusionar" in capsys.readouterr().out
    assert lotes.pendientes({5: (SIN_VALIDAR, "OPEN")}) != {}, "sin refactor, le faltan las validaciones"


# --- pendiente por líneas --------------------------------------------------------------------------

def test_pendiente_agrupa_por_lineas_con_la_principal_primero(monkeypatch, capsys):
    items = _items(_item(2, "Ready"), _item(3, "Ready", "tarea", "modo:tct"), _item(4, "Ready", "tarea", "modo:rally",
                                                                                     abierta=False))
    monkeypatch.setattr(tablero, "usuario_actual", lambda: "SkiTemplar")
    monkeypatch.setattr(tablero, "cargar_proyecto", lambda: {"items": items})
    prs = [{**_pr(600, "Closes #3", "dev-tct"), "author": {"login": "SkiTemplar"}},
           {**_pr(601, "Closes #2", "dev"), "author": {"login": "SkiTemplar"}}]
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: prs)
    tablero.cmd_pendiente(_args())
    salida = capsys.readouterr().out
    principal, _, tct = salida.partition("## Línea dev-tct (`modo:tct`)")
    assert tct, "hay sección de dev-tct"
    assert "#2 " in principal and "#3 " not in principal and "PR #601" in principal and "PR #600" not in principal
    assert "#3 " in tct and "PR #600" in tct and "#2 " not in tct
    assert "dev-rally" not in salida.split("líneas de modo")[1].split("\n", 1)[1], "sin nada abierto, no sale"
    assert "(nada)" not in tct, "en un modo solo salen las secciones con algo"


def test_por_linea_sin_modos_solo_la_principal():
    assert [rama for rama, _ in tablero.por_linea(_items(_item(1, "Ready")))] == ["dev"]
    assert [rama for rama, _ in tablero.por_linea({})] == ["dev"]


# --- organización: reglas puras --------------------------------------------------------------------

RUTAS = ["Scripts/tablero/", "Scripts/tests/test_tablero*", ".claude/", ".github/", "CLAUDE.md"]


@pytest.mark.parametrize("ruta, dentro", [("Scripts/tablero/base.py", True), ("Scripts/tests/test_tablero_x.py", True),
                                          (".claude/skills/a/SKILL.md", True), ("CLAUDE.md", True),
                                          ("Docs/CLAUDE.md", False), ("Scripts/tests/test_terrain.py", False),
                                          ("Scripts/tableros/x.py", False), ("Source/a.cpp", False)])
def test_coincide_con_las_rutas_de_organizacion(ruta, dentro):
    assert organizacion.coincide(ruta, RUTAS) is dentro


def test_limitadores_acotan_ls_tree():
    assert organizacion.limitadores(RUTAS) == [".claude", ".github", "CLAUDE.md", "Scripts/tablero", "Scripts/tests"]
    assert organizacion.limitadores(["*.md", "Scripts/"]) == [], "un comodín en la raíz obliga a leer todo"


def test_destinos_solo_main_y_las_lineas_exactas():
    salida = "\n".join(f"{i}abc\trefs/heads/{r}" for i, r in enumerate(
        ["dev", "main", "chamber", "dev-tct", "dev-tct-42-cosa", "dev-rally", "dev-foo", "org/propagar-1-main"]))
    assert organizacion.destinos(salida) == ["main", "dev-tct", "dev-rally"]


def test_diferencias_copia_lo_distinto_y_borra_lo_que_sobra():
    origen = {"a": ("100644", "1"), "b": ("100644", "2")}
    destino = {"a": ("100644", "1"), "b": ("100644", "9"), "c": ("100644", "3")}
    assert organizacion.diferencias(origen, destino) == ({"b": ("100644", "2")}, ["c"])
    assert organizacion.diferencias(origen, origen) == ({}, [])


def test_el_aviso_de_sync_por_organizacion(monkeypatch):
    avisos_ = []
    monkeypatch.setattr(organizacion, "ramas_desfasadas", lambda: ["main", "dev-tct"])
    tablero.avisos_organizacion(avisos_)
    monkeypatch.setattr(organizacion, "ramas_desfasadas", lambda: [])
    tablero.avisos_organizacion(avisos_)

    def falla():
        raise base.ErrorTablero("git ls-remote: sin red")

    monkeypatch.setattr(organizacion, "ramas_desfasadas", falla)
    tablero.avisos_organizacion(avisos_)
    assert avisos_[0] == "Organización distinta de dev en main, dev-tct: `organizacion propagar --aplicar`"
    assert len(avisos_) == 2 and "sin red" in avisos_[1] and "\n" not in avisos_[1]


def test_organizacion_propagar_esta_en_la_ayuda():
    parser = argparse.ArgumentParser()
    organizacion.anadir_comandos(parser.add_subparsers())
    args = parser.parse_args(["organizacion", "propagar", "--aplicar"])
    assert args.fn is organizacion.cmd_propagar and args.aplicar


def test_rutas_de_propagacion_en_equipo_json():
    assert base.CONFIG["rutas_organizacion_propagar"] == RUTAS
    assert not any(r.startswith("Docs") for r in base.CONFIG["rutas_organizacion_propagar"])


# --- organización: con git de verdad en repositorios temporales ------------------------------------

def _g(cwd, *args):
    proc = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, encoding="utf-8")
    assert proc.returncode == 0, proc.stderr
    return proc.stdout.strip()


def _escribir(raiz, ficheros):
    for ruta, texto in ficheros.items():
        destino = raiz / ruta
        destino.parent.mkdir(parents=True, exist_ok=True)
        destino.write_text(texto, encoding="utf-8")


@pytest.fixture
def repos(tmp_path, monkeypatch):
    """origin con dev, main (organización vieja), dev-tct (igual que dev) y una rama de trabajo, y un clon del usuario."""
    config = tmp_path / "gitconfig"
    config.write_text("[user]\n\tname = Prueba\n\temail = prueba@example.com\n[init]\n\tdefaultBranch = dev\n",
                      encoding="utf-8")
    for clave, valor in {"GIT_CONFIG_GLOBAL": str(config), "GIT_CONFIG_NOSYSTEM": "1"}.items():
        monkeypatch.setenv(clave, valor)
    origen, semilla, usuario = tmp_path / "origin.git", tmp_path / "semilla", tmp_path / "usuario"
    _g(tmp_path, "init", "--bare", "-q", str(origen))
    _g(tmp_path, "init", "-q", str(semilla))
    _escribir(semilla, {"Scripts/tablero/a.py": "v1\n", "Scripts/tablero/viejo.py": "se borra\n",
                        "CLAUDE.md": "guía v1\n", "Source/x.cpp": "juego\n", "Scripts/tests/test_terrain.py": "t\n"})
    _g(semilla, "add", "-A")
    _g(semilla, "commit", "-q", "-m", "base")
    _g(semilla, "branch", "main")
    _g(semilla, "rm", "-q", "Scripts/tablero/viejo.py")
    _escribir(semilla, {"Scripts/tablero/a.py": "v2\n", "CLAUDE.md": "guía v2\n", ".claude/skills/s.md": "skill\n",
                        "Scripts/tests/test_tablero_x.py": "test\n", "Source/x.cpp": "juego en dev\n"})
    _g(semilla, "add", "-A")
    _g(semilla, "commit", "-q", "-m", "organización nueva")
    _g(semilla, "branch", "dev-tct")
    _g(semilla, "branch", "dev-tct-5-cosa", "main")
    _g(semilla, "remote", "add", "origin", str(origen))
    _g(semilla, "push", "-q", "origin", "dev", "main", "dev-tct", "dev-tct-5-cosa")
    _g(tmp_path, "clone", "-q", "-b", "dev", str(origen), str(usuario))
    _escribir(usuario, {"Source/x.cpp": "cambio sin guardar del usuario\n"})
    monkeypatch.setattr(organizacion, "RAIZ", str(usuario))
    return {"origen": origen, "usuario": usuario}


def test_ramas_desfasadas_solo_main(repos):
    assert organizacion.ramas_desfasadas() == ["main"]


def test_propagar_sin_aplicar_solo_informa(repos, monkeypatch, capsys):
    monkeypatch.setattr(organizacion, "gh", lambda *a, **k: pytest.fail("no debe llamar a GitHub"))
    organizacion.cmd_propagar(_args(aplicar=False))
    salida = capsys.readouterr().out
    assert "- main: difiere (4 ficheros por copiar, 1 por borrar)" in salida and "- dev-tct: igual que dev" in salida
    assert "simulación" in salida and "dev-tct-5-cosa" not in salida
    assert "org/propagar" not in _g(repos["origen"], "branch", "--list")


def test_propagar_con_aplicar_abre_la_pr_sin_tocar_el_arbol_del_usuario(repos, monkeypatch, capsys):
    gh = Gh({"pr create": "https://github.com/x/y/pull/950\n", "pr list": "[]"})
    monkeypatch.setattr(organizacion, "gh", gh)
    usuario, origen = repos["usuario"], repos["origen"]
    antes = (_g(usuario, "rev-parse", "HEAD"), _g(usuario, "status", "--porcelain"), _g(usuario, "branch", "--show-current"))
    organizacion.cmd_propagar(_args(aplicar=True))
    rama = organizacion.rama_propagacion("main", date.today())
    assert (_g(usuario, "rev-parse", "HEAD"), _g(usuario, "status", "--porcelain"),
            _g(usuario, "branch", "--show-current")) == antes
    assert _g(origen, "rev-parse", f"{rama}^") == _g(origen, "rev-parse", "main"), "sale de main"
    ficheros = set(_g(origen, "ls-tree", "-r", "--name-only", rama).splitlines())
    assert "Scripts/tablero/viejo.py" not in ficheros and ".claude/skills/s.md" in ficheros
    assert _g(origen, "show", f"{rama}:Scripts/tablero/a.py") == "v2"
    assert _g(origen, "show", f"{rama}:CLAUDE.md") == "guía v2"
    assert _g(origen, "show", f"{rama}:Source/x.cpp") == "juego", "lo que no es organización no se toca"
    crear = next(a for a in gh.llamadas if a[:2] == ("pr", "create"))
    assert crear[crear.index("--base") + 1] == "main" and crear[crear.index("--head") + 1] == rama
    assert "PR abierta https://github.com/x/y/pull/950" in capsys.readouterr().out
    assert not any(a[:2] == ("pr", "create") and "dev-tct" in a for a in gh.llamadas), "dev-tct ya era igual"


def test_propagar_otra_vez_el_mismo_dia_actualiza_la_rama_sin_otra_pr(repos, monkeypatch, capsys):
    rama = organizacion.rama_propagacion("main", date.today())
    gh = Gh({"pr list": json.dumps([{"number": 950, "headRefName": rama, "baseRefName": "main"}])})
    monkeypatch.setattr(organizacion, "gh", gh)
    organizacion.cmd_propagar(_args(aplicar=True))
    assert not any(a[:2] == ("pr", "create") for a in gh.llamadas)
    assert "su PR #950 ya estaba abierta" in capsys.readouterr().out


def test_propagar_actualiza_la_pr_abierta_de_otro_dia_sin_abrir_otra(repos, monkeypatch, capsys):
    vieja = "org/propagar-20261001-main"
    abiertas = [{"number": 940, "headRefName": vieja, "baseRefName": "main"},
                {"number": 941, "headRefName": "feat/5-x", "baseRefName": "main"}]
    gh = Gh({"--head": "[]", "pr list": json.dumps(abiertas), "pr create": "https://x/pull/1\n"})
    monkeypatch.setattr(organizacion, "gh", gh)
    organizacion.cmd_propagar(_args(aplicar=True))
    assert not any(a[:2] == ("pr", "create") for a in gh.llamadas)
    assert f"rama {vieja} actualizada; su PR #940 ya estaba abierta" in capsys.readouterr().out
    origen = repos["origen"]
    assert _g(origen, "show", f"{vieja}:CLAUDE.md") == "guía v2"
    assert organizacion.rama_propagacion("main", date.today()) not in _g(origen, "branch", "--list")


def test_propagacion_abierta_solo_hacia_su_destino():
    prs = [{"number": 940, "headRefName": "org/propagar-20261001-dev-tct", "baseRefName": "dev-tct"},
           {"number": 941, "headRefName": "feat/5-x", "baseRefName": "main"}]
    assert organizacion.propagacion_abierta(prs, "main") is None
    assert organizacion.propagacion_abierta(prs, "dev-tct")["number"] == 940


def test_el_entorno_del_usuario_no_cambia_tras_propagar(repos, monkeypatch):
    monkeypatch.setattr(organizacion, "gh", Gh({"pr create": "https://x/pull/1\n"}))
    organizacion.cmd_propagar(_args(aplicar=True))
    assert "GIT_INDEX_FILE" not in os.environ
    assert (repos["usuario"] / "Source/x.cpp").read_text(encoding="utf-8") == "cambio sin guardar del usuario\n"
