"""Issues descartadas (etiqueta `chamber`): ninguna rutina las procesa ni avisa de ellas, ningún comando del ciclo las
toca (tampoco con `--forzar`) y solo `--retomar`, de un aprobador con una Decisión posterior, las retoma. Sus PR
siguen a la vista."""

import argparse
import importlib.util
import json
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
import memoria  # noqa: E402
import peticiones  # noqa: E402
import volcado  # noqa: E402

spec = importlib.util.spec_from_file_location("tablero_chamber", CARPETA / "tablero.py")
assert spec is not None and spec.loader is not None
tablero = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tablero)

AHORA = datetime(2026, 10, 6, 7, 15, tzinfo=timezone.utc)
CHAMBER = flujo.ETIQUETA_CHAMBER


def _item(numero, status, *etiquetas, abierta=True, asignados=(), actualizada="2026-09-20T10:00:00Z", **valores):
    """Item del Project tal como lo devuelve cargar_proyecto."""
    return {"number": numero, "title": f"Tarea {numero}", "state": "OPEN" if abierta else "CLOSED",
            "updatedAt": actualizada, "valores": {**({"Status": status} if status else {}), **valores},
            "labels": {"nodes": [{"name": e} for e in ("tarea", *etiquetas)]},
            "assignees": {"nodes": [{"login": a} for a in asignados]}, "blockedBy": {"nodes": []}}


def _items(*issues):
    return {i["number"]: i for i in issues}


def _pr(numero, cuerpo, rama="feat/x", mergeable="MERGEABLE"):
    return {"number": numero, "baseRefName": "dev", "headRefName": rama, "body": cuerpo, "mergeable": mergeable,
            "author": {"login": "Mokius"}, "title": f"PR {numero}", "reviewDecision": None, "isDraft": False}


def _args(**campos):
    return argparse.Namespace(**campos)


# --- predicado y reglas puras -----------------------------------------------------------------------

@pytest.mark.parametrize("issue", [
    {"labels": [{"name": CHAMBER}]},                       # gh issue list / view
    {"labels": {"nodes": [{"name": "tarea"}, {"name": CHAMBER}]}},  # Project
    {"etiquetas": {"tarea", CHAMBER}},                      # normalizada de la auditoría
])
def test_es_chamber_con_cualquier_forma_de_etiquetas(issue):
    assert flujo.es_chamber(issue)


@pytest.mark.parametrize("issue", [{}, {"labels": [{"name": "tarea"}]}, {"etiquetas": {"objeto"}},
                                   {"labels": {"nodes": [{"name": "chamberlain"}]}}])
def test_sin_la_etiqueta_no_es_chamber(issue):
    assert not flujo.es_chamber(issue)


def test_sin_chamber_y_descartadas_no_mutan_los_items():
    items = _items(_item(1, "Ready"), _item(2, "Ready", CHAMBER, abierta=False))
    assert list(flujo.sin_chamber(items)) == [1]
    assert flujo.descartadas(items) == {2}
    assert set(items) == {1, 2}


def test_motivo_chamber_es_de_una_linea_y_no_dice_como_saltarselo():
    motivo = flujo.motivo_chamber(42, _item(42, "Ready", CHAMBER))
    assert motivo and "\n" not in motivo and "SkiTemplar o Mokius" in motivo
    assert "--forzar" not in motivo and "--retomar" not in motivo
    assert flujo.motivo_chamber(42, _item(42, "Ready")) is None


DESCARTE = flujo.texto_chamber("Fuera del modo único")
APROBADORES = ("SkiTemplar", "Mokius")


def _decision(quien, texto="Vuelve al juego"):
    return memoria.texto_decision(texto, quien, date(2026, 10, 6))


def test_quien_decide_lee_la_firma_de_la_decision():
    assert memoria.quien_decide(_decision("Mokius")) == "Mokius"
    assert memoria.quien_decide("  " + _decision("SkiTemplar")) == "SkiTemplar"
    assert memoria.quien_decide("**Resumen**\n- Qué fallaba: x") is None
    assert memoria.quien_decide("Decisión (2026-10-06, Mokius): sin negrita") is None


@pytest.mark.parametrize("comentarios, esperado", [
    ([DESCARTE, _decision("Mokius")], True),
    ([DESCARTE, _decision("SkiTemplar") + "\n\n_Lanzado por SkiTemplar a través del puente._"], True),
    ([_decision("Mokius")], True),  # etiqueta puesta a mano, sin comentario de descarte
    ([_decision("Mokius"), DESCARTE], False),  # la decisión es anterior al descarte
    ([DESCARTE, _decision("Mokius"), DESCARTE], False),  # descartada otra vez después
    ([DESCARTE, _decision("Ruben-Besteiro")], False),  # no la firma un aprobador
    ([DESCARTE, "Mokius dice que vuelve"], False),
    ([], False),
])
def test_decision_tras_descarte(comentarios, esperado):
    assert flujo.decision_tras_descarte(comentarios, APROBADORES) is esperado


def test_motivo_para_no_retomar_exige_aprobador_y_decision():
    con_decision = [DESCARTE, _decision("Mokius")]
    assert flujo.motivo_para_no_retomar(42, "SkiTemplar", APROBADORES, con_decision) is None
    assert "solo un aprobador" in flujo.motivo_para_no_retomar(42, "Ruben-Besteiro", APROBADORES, con_decision)
    assert "Decisión" in flujo.motivo_para_no_retomar(42, "Mokius", APROBADORES, [DESCARTE])


def test_actor_de_puente_solo_en_comandos_lanzados_a_mano():
    puente = {"GITHUB_ACTIONS": "true", "GITHUB_EVENT_NAME": "workflow_dispatch", "GITHUB_TRIGGERING_ACTOR": "Mokius"}
    assert flujo.actor_de_puente(puente) == "Mokius"
    assert flujo.actor_de_puente({**puente, "GITHUB_EVENT_NAME": "schedule"}) is None
    assert flujo.actor_de_puente({}) is None


def test_texto_chamber_compacta_el_motivo_y_rechaza_el_vacio():
    assert flujo.texto_chamber("Fuera del  modo\núnico") == "**Descartada** (`chamber`): Fuera del modo único"
    with pytest.raises(ValueError):
        flujo.texto_chamber("  \n ")


PERSONAS = ("peticion", "decision", "revisar-qa")


def test_pasos_chamber_de_una_issue_abierta_asignada_y_con_avisos():
    datos = {"state": "OPEN", "labels": [{"name": "tarea"}, {"name": "peticion"}, {"name": "decision"}],
             "assignees": [{"login": "Mokius"}, {"login": "SkiTemplar"}], "comments": []}
    assert flujo.pasos_chamber(datos, "t", _item(1, "In review"), PERSONAS) == {
        "etiquetar": True, "quitar": ["decision", "peticion"], "desasignar": ["Mokius", "SkiTemplar"],
        "comentar": True, "backlog": True, "cerrar": True}


def test_pasos_chamber_idempotente_incluso_con_la_firma_del_puente():
    texto = flujo.texto_chamber("modo recortado")
    datos = {"state": "CLOSED", "labels": [{"name": CHAMBER}], "assignees": [],
             "comments": [{"body": texto + "\n\n_Lanzado por Mokius a través del puente._"}]}
    assert flujo.pasos_chamber(datos, texto, _item(1, "Backlog", CHAMBER), PERSONAS) == {
        "etiquetar": False, "quitar": [], "desasignar": [], "comentar": False, "backlog": False, "cerrar": False}


def test_pasos_chamber_no_reabre_ni_cierra_una_completada_ni_la_mete_en_el_tablero():
    datos = {"state": "CLOSED", "labels": [], "assignees": [], "comments": []}
    pasos = flujo.pasos_chamber(datos, "t", None, PERSONAS)
    assert pasos["cerrar"] is False and pasos["backlog"] is False


def test_pasos_chamber_aparca_en_backlog_una_tarea_recien_entrada_sin_status():
    datos = {"state": "OPEN", "labels": [{"name": "tarea"}], "assignees": [], "comments": []}
    assert flujo.pasos_chamber(datos, "t", _item(1, None), PERSONAS)["backlog"] is True


@pytest.mark.parametrize("etiqueta", ["objeto", "lote"])
def test_pasos_chamber_no_da_status_a_un_objeto_ni_a_un_lote(etiqueta):
    datos = {"state": "OPEN", "labels": [{"name": etiqueta}], "assignees": [], "comments": []}
    pasos = flujo.pasos_chamber(datos, "t", {"number": 1, "valores": {}}, PERSONAS)
    assert pasos["backlog"] is False and pasos["cerrar"] is True


def _nodo(numero, *etiquetas, estado="OPEN"):
    return {"number": numero, "state": estado, "labels": {"nodes": [{"name": e} for e in etiquetas]}}


def _leida(blocking=(), sub_issues=()):
    return {"blocking": {"nodes": list(blocking)}, "subIssues": {"nodes": list(sub_issues)}}


def test_avisos_de_descarte_lotes_dependientes_y_sub_issues():
    leida = _leida(blocking=[_nodo(130, "lote"), _nodo(57, "tarea"), _nodo(58, "tarea")],
                   sub_issues=[_nodo(60, "tarea"), _nodo(61, "bug")])
    avisos = flujo.avisos_de_descarte(42, leida)
    assert len(avisos) == 3
    assert avisos[0].startswith("Aviso: #42 es miembro del lote #130")
    assert avisos[1].startswith("Aviso: #57, #58 dependían de #42") and "Ready" in avisos[1]
    assert avisos[2].startswith("Aviso: las sub-issues #60, #61 de #42 siguen vivas")


def test_avisos_de_descarte_ignora_lo_cerrado_y_lo_ya_descartado():
    leida = _leida(blocking=[_nodo(130, "lote", estado="CLOSED"), _nodo(57, "tarea", CHAMBER),
                             _nodo(58, "tarea", estado="CLOSED")],
                   sub_issues=[_nodo(60, "tarea", CHAMBER), _nodo(61, "tarea", estado="CLOSED")])
    assert flujo.avisos_de_descarte(42, leida) == []
    assert flujo.avisos_de_descarte(42, {}) == []


@pytest.mark.parametrize("estado, esperado", [("OPEN", {"quitar_etiqueta": True, "reabrir": False}),
                                             ("CLOSED", {"quitar_etiqueta": True, "reabrir": True})])
def test_pasos_retomar_una_descartada(estado, esperado):
    assert flujo.pasos_retomar({"state": estado, "labels": [{"name": CHAMBER}]}) == esperado


def test_pasos_retomar_no_toca_una_viva_aunque_este_cerrada():
    assert flujo.pasos_retomar({"state": "CLOSED", "labels": []}) == {"quitar_etiqueta": False, "reabrir": False}


def test_el_comentario_del_descarte_no_es_conversacion():
    assert not auditoria.es_conversacion(flujo.texto_chamber("fuera"))


def test_solo_descartadas_si_todas_las_que_cierra_son_chamber():
    assert base.solo_descartadas(_pr(1, "Closes #5\nCloses #6"), {5, 6})
    assert not base.solo_descartadas(_pr(1, "Closes #5\nCloses #7"), {5, 6})
    assert not base.solo_descartadas(_pr(1, "Sin issue"), {5, 6}), "una PR sin issue no es una PR descartada"
    assert not base.solo_descartadas(_pr(1, "Closes #9\nRefs #5"), {5}), "Refs no cuenta"


def test_la_carga_de_revisiones_no_cuenta_las_descartadas():
    proyecto = {"items": _items(_item(1, "In review", CHAMBER, Revisor="Mokius"),
                                _item(2, "In review", CHAMBER, Revisor="Mokius"),
                                _item(3, "In review", Revisor="SkiTemplar"))}
    assert base.elegir_revisor(proyecto, "Ruben-Besteiro") == "Mokius"


# --- sync -----------------------------------------------------------------------------------------

def test_sync_no_mueve_a_done_una_chamber_cerrada(monkeypatch):
    monkeypatch.setattr(tablero, "gh", lambda *a, **k: "[]")
    proyecto = {"items": _items(_item(1, "Ready", CHAMBER, abierta=False), _item(2, "Ready", abierta=False))}
    cambios = []
    tablero.reconciliar_issues_sueltas(proyecto, cambios)
    assert [texto for texto, _ in cambios] == ["#2 cerrada → Done"]


def test_sync_no_mete_en_backlog_una_chamber_abierta_fuera_del_tablero(monkeypatch):
    sueltas = [{"number": 7, "title": "Tutorial", "labels": [{"name": CHAMBER}]},
               {"number": 8, "title": "Rally: fallo", "labels": [{"name": "tarea"}]}]
    monkeypatch.setattr(tablero, "gh", lambda *a, **k: json.dumps(sueltas))
    cambios = []
    tablero.reconciliar_issues_sueltas({"items": {}}, cambios)
    assert [texto for texto, _ in cambios] == ["#8 entra al tablero en Backlog (Rally: fallo)"]


def test_sync_no_desbloquea_una_chamber():
    cerradas = {"nodes": [{"number": 40, "state": "CLOSED"}]}
    proyecto = {"items": _items({**_item(1, "Bloqueada", CHAMBER), "blockedBy": cerradas},
                                {**_item(2, "Bloqueada"), "blockedBy": cerradas})}
    cambios = []
    tablero.reconciliar_bloqueos(proyecto, cambios)
    assert [texto for texto, _ in cambios] == ["#2 → Ready (ya están cerradas #40)"]


def test_pr_que_enlaza_una_chamber_no_la_mueve_pero_avisa(monkeypatch):
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [_pr(400, "Closes #12")])
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [])
    proyecto = {"items": _items(_item(12, "Ready", CHAMBER))}
    cambios, avisos_ = [], []
    tablero.reconciliar_prs(proyecto, cambios, avisos_)
    assert not cambios
    assert avisos_ == ["PR #400 enlaza la issue descartada #12: quita su «Closes» o cierra la PR"]


def test_pr_de_una_viva_no_avisa_de_descartes(monkeypatch):
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [_pr(400, "Closes #12")])
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [])
    proyecto = {"items": _items(_item(12, "In review"))}
    avisos_ = []
    tablero.reconciliar_prs(proyecto, [], avisos_)
    assert avisos_ == []


def test_pr_mixta_mueve_la_viva_y_no_la_chamber(monkeypatch):
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [_pr(400, "Closes #12\nCloses #13")])
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [])
    proyecto = {"items": _items(_item(12, "Ready", CHAMBER), _item(13, "In progress"))}
    cambios, avisos_ = [], []
    tablero.reconciliar_prs(proyecto, cambios, avisos_)
    assert [texto for texto, _ in cambios] == ["#13 → In review (PR #400)"]
    assert avisos_ == ["PR #400 enlaza la issue descartada #12: quita su «Closes» o cierra la PR"]


def test_fusion_no_mueve_una_chamber_abierta(monkeypatch):
    monkeypatch.setattr(tablero, "prs_fusionadas",
                        lambda: [{"number": 900, "baseRefName": "dev", "headRefName": "x", "body": "Closes #5\nCloses #6"}])
    proyecto = {"items": _items(_item(5, "Ready", CHAMBER), _item(6, "Ready"))}
    cambios, avisos_ = [], []
    tablero.reconciliar_fusiones(proyecto, [], cambios, avisos_)
    assert [texto.split(" (")[0] for texto, _ in cambios] == ["#6 → In review"]


def test_miembro_chamber_de_un_lote_no_pasa_a_validada(monkeypatch):
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [])
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [])
    en_lote = {"blocking": {"nodes": [{"number": 130, "state": "OPEN", "labels": {"nodes": [{"name": "lote"}]}}]}}
    listos = {"Revisión IA": "Aprobada", "Editor": "Funciona"}
    proyecto = {"items": _items({**_item(1, "QA editor", CHAMBER, **listos), **en_lote},
                                {**_item(2, "QA editor", **listos), **en_lote})}
    cambios = []
    tablero.reconciliar_lotes(proyecto, cambios, [])
    assert [texto.split(" (")[0] for texto, _ in cambios] == ["#2 → Validada"]


def test_estancadas_y_validaciones_no_avisan_de_chamber():
    proyecto = {"items": _items(_item(1, "In progress", CHAMBER), _item(2, "In review", CHAMBER),
                                _item(3, "In progress"), _item(4, "In review"))}
    avisos_ = []
    tablero.reconciliar_estancadas(proyecto, avisos_)
    tablero.avisos_validacion(proyecto, avisos_)
    assert avisos_ == ["#3 está In progress sin asignar", "En review sin revisión de una segunda IA: #4"]


# --- coger y estado -------------------------------------------------------------------------------

class Detenido(Exception):
    """El comando ha pasado las comprobaciones y ha llegado a git o a GitHub."""


def _con_issue(monkeypatch, issue):
    monkeypatch.setattr(tablero, "cargar_issue", lambda n: {"items": {n: issue}})
    ediciones = []
    monkeypatch.setattr(tablero, "poner_campo", lambda _p, n, campo, valor: ediciones.append((n, campo, valor)))
    return ediciones


def _gh_espia(monkeypatch, *modulos, comentarios=()):
    """Sustituye gh en `modulos` (y en base, que usan retomar y comentar) y devuelve las llamadas que escriben.

    `issue view` (la lectura de comentarios de `--retomar`) devuelve `comentarios` y no se apunta.
    """
    llamadas = []

    def gh(*args, **_k):
        if args[:2] == ("issue", "view"):
            return json.dumps({"comments": [{"body": c} for c in comentarios]})
        llamadas.append(tuple(a for a in args if a not in ("--repo", base.REPO)))
        return ""

    for modulo in (base, *modulos):
        monkeypatch.setattr(modulo, "gh", gh)
    return llamadas


@pytest.mark.parametrize("forzar", [False, True])
def test_coger_rechaza_una_chamber_tambien_con_forzar(monkeypatch, forzar):
    _con_issue(monkeypatch, _item(42, "Ready", CHAMBER))
    monkeypatch.setattr(tablero, "git", lambda *a: pytest.fail("no debe tocar git"))
    llamadas = _gh_espia(monkeypatch, tablero)
    with pytest.raises(base.ErrorTablero, match="descartada") as error:
        tablero.cmd_coger(_args(numero=42, forzar=forzar, retomar=False, rama=None))
    assert "\n" not in str(error.value) and llamadas == []


@pytest.mark.parametrize("forzar", [False, True])
def test_coger_sigue_con_una_viva(monkeypatch, forzar):
    _con_issue(monkeypatch, _item(42, "Ready"))
    monkeypatch.setattr(tablero, "usuario_actual", lambda: "SkiTemplar")
    monkeypatch.delenv("TN_SESION_NOCTURNA", raising=False)  # fuera de una sesión nocturna desatendida

    def git(*_a):
        raise Detenido

    monkeypatch.setattr(tablero, "git", git)
    with pytest.raises(Detenido):
        tablero.cmd_coger(_args(numero=42, forzar=forzar, retomar=False, rama=None))


def test_estado_ready_sobre_una_chamber_avisa_y_no_mueve(monkeypatch):
    ediciones = _con_issue(monkeypatch, _item(42, "Backlog", CHAMBER))
    with pytest.raises(base.ErrorTablero, match="descartada"):
        tablero.cmd_estado(_args(numero=42, estado="Ready", retomar=False))
    assert ediciones == []


@pytest.mark.parametrize("estado, etiquetas", [("Backlog", (CHAMBER,)), ("Ready", ())])
def test_estado_mueve_a_backlog_una_chamber_o_cualquier_estado_una_viva(monkeypatch, estado, etiquetas):
    ediciones = _con_issue(monkeypatch, _item(42, "Backlog", *etiquetas))
    llamadas = _gh_espia(monkeypatch, tablero)
    tablero.cmd_estado(_args(numero=42, estado=estado, retomar=False))
    assert ediciones == [(42, "Status", estado)] and llamadas == []


@pytest.mark.parametrize("estado", ["In progress", "Revisiones", "Done"])
def test_estado_rechaza_sacar_de_backlog_una_chamber_sin_retomar(monkeypatch, estado):
    ediciones = _con_issue(monkeypatch, _item(42, "Backlog", CHAMBER))
    with pytest.raises(base.ErrorTablero, match="descartada"):
        tablero.cmd_estado(_args(numero=42, estado=estado, retomar=False))
    assert ediciones == []


def _lanza(monkeypatch, quien):
    monkeypatch.setattr(base, "quien_lanza", lambda: quien)


def test_estado_con_retomar_de_un_aprobador_con_decision_la_retoma_y_la_mueve(monkeypatch):
    ediciones = _con_issue(monkeypatch, _item(42, "Backlog", CHAMBER, abierta=False))
    llamadas = _gh_espia(monkeypatch, tablero, comentarios=[DESCARTE, _decision("Mokius")])
    _lanza(monkeypatch, "SkiTemplar")
    tablero.cmd_estado(_args(numero=42, estado="Ready", retomar=True))
    assert llamadas[:2] == [("issue", "edit", "42", "--remove-label", CHAMBER), ("issue", "reopen", "42")]
    assert llamadas[2][:3] == ("issue", "comment", "42") and "Retomada" in llamadas[2][4]
    assert ediciones == [(42, "Status", "Ready")]


@pytest.mark.parametrize("quien, comentarios", [
    ("Ruben-Besteiro", [DESCARTE, _decision("Mokius")]),  # no es aprobador
    ("SkiTemplar", [DESCARTE]),  # aprobador, pero sin decisión
    ("SkiTemplar", [_decision("Mokius"), DESCARTE]),  # la decisión es anterior al descarte
    ("Mokius", [DESCARTE, _decision("Ruben-Besteiro")]),  # la decisión no es de un aprobador
])
def test_retomar_sin_aprobador_o_sin_decision_no_toca_nada(monkeypatch, quien, comentarios):
    ediciones = _con_issue(monkeypatch, _item(42, "Backlog", CHAMBER, abierta=False))
    llamadas = _gh_espia(monkeypatch, tablero, comentarios=comentarios)
    _lanza(monkeypatch, quien)
    with pytest.raises(base.ErrorTablero, match="descartada"):
        tablero.cmd_estado(_args(numero=42, estado="Ready", retomar=True))
    assert ediciones == [] and llamadas == []


def test_coger_con_retomar_retoma_la_chamber_antes_de_asignarla(monkeypatch):
    ediciones = _con_issue(monkeypatch, _item(42, "Backlog", CHAMBER))
    llamadas = _gh_espia(monkeypatch, tablero, comentarios=[DESCARTE, _decision("SkiTemplar")])
    _lanza(monkeypatch, "SkiTemplar")
    monkeypatch.setattr(tablero, "usuario_actual", lambda: "SkiTemplar")
    monkeypatch.delenv("TN_SESION_NOCTURNA", raising=False)  # fuera de una sesión nocturna desatendida
    monkeypatch.setattr(tablero, "git", lambda *a: "")
    tablero.cmd_coger(_args(numero=42, forzar=False, retomar=True, rama=None))
    assert llamadas[0] == ("issue", "edit", "42", "--remove-label", CHAMBER)
    assert ("issue", "reopen", "42") not in llamadas, "abierta: no se reabre"
    assert ("issue", "edit", "42", "--add-assignee", "@me") in llamadas
    assert ediciones == [(42, "Status", "In progress")]


def _comando_ciclo(nombre, numero):
    return {"revision": (tablero.cmd_revision, _args(numero=numero, revisor=None)),
            "ia": (tablero.cmd_ia, _args(numero=numero, veredicto="cambios", revisor="Mokius (Claude)", nota="x")),
            "editor": (tablero.cmd_editor, _args(numero=numero, resultado="falla", como="PIE", nota=None)),
            "pedir": (peticiones.cmd_pedir, _args(numero=numero, texto="Cámbialo"))}[nombre]


@pytest.mark.parametrize("nombre", ["revision", "ia", "editor", "pedir"])
@pytest.mark.parametrize("abierta", [True, False])
def test_los_comandos_del_ciclo_rechazan_una_chamber_sin_tocar_nada(monkeypatch, nombre, abierta):
    issue = _item(42, "Backlog", CHAMBER, abierta=abierta)
    ediciones = _con_issue(monkeypatch, issue)
    monkeypatch.setattr(peticiones, "cargar_issue", lambda n: {"items": {n: issue}})
    llamadas = _gh_espia(monkeypatch, tablero, peticiones)
    fn, args = _comando_ciclo(nombre, 42)
    with pytest.raises(base.ErrorTablero, match="descartada") as error:
        fn(args)
    assert "\n" not in str(error.value) and ediciones == [] and llamadas == []


def test_editor_falla_no_reabre_una_chamber_que_no_esta_en_el_tablero(monkeypatch):
    monkeypatch.setattr(tablero, "cargar_issue", lambda n: {"items": {}})
    monkeypatch.setattr(tablero, "item_de_issue", lambda *_a: pytest.fail("no debe meterla en el tablero"))
    monkeypatch.setattr(tablero, "gh", lambda *a, **k: json.dumps({"state": "CLOSED", "labels": [{"name": CHAMBER}]}))
    with pytest.raises(base.ErrorTablero, match="descartada"):
        tablero.cmd_editor(_args(numero=42, resultado="falla", como="PIE", nota=None))


def test_editor_falla_sigue_reabriendo_una_viva_cerrada(monkeypatch):
    ediciones = _con_issue(monkeypatch, _item(42, "Done", abierta=False, Editor="Funciona"))
    llamadas = _gh_espia(monkeypatch, tablero)
    monkeypatch.setattr(tablero, "comentar", lambda *_a: None)
    tablero.cmd_editor(_args(numero=42, resultado="falla", como="PIE", nota=None))
    assert ("issue", "reopen", "42") in llamadas and (42, "Status", "Revisiones") in ediciones


def _opciones(nombre):
    return {o for a in _parser(nombre)._actions for o in a.option_strings}


def test_retomar_es_un_flag_propio_de_coger_y_estado():
    assert "--retomar" in _opciones("coger") and "--retomar" in _opciones("estado")
    assert "--forzar" not in _opciones("estado")


# --- pendiente ------------------------------------------------------------------------------------

def test_pendiente_no_ensena_chamber_y_marca_sus_pr(monkeypatch, capsys):
    items = _items(_item(1, "Ready", CHAMBER), _item(2, "Ready"), _item(3, "Revisiones", CHAMBER, "decision"),
                   _item(4, "In progress", CHAMBER, asignados=["SkiTemplar"]))
    monkeypatch.setattr(tablero, "usuario_actual", lambda: "SkiTemplar")
    monkeypatch.setattr(tablero, "cargar_proyecto", lambda: {"items": items})
    prs = [{**_pr(500, "Closes #1"), "author": {"login": "SkiTemplar"}}, {**_pr(501, "Closes #2"), "author": {"login": "SkiTemplar"}}]
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: prs)
    tablero.cmd_pendiente(_args())
    salida = capsys.readouterr().out
    assert "#2 " in salida
    assert not any(f"#{n} " in salida for n in (1, 3, 4))
    lineas = {linea.split()[1]: linea for linea in salida.splitlines() if linea.strip().startswith("PR #")}
    assert "solo issues descartadas" in lineas["#500"], "una PR de una descartada no desaparece"
    assert "descartadas" not in lineas["#501"]


# --- auditar y conversación -----------------------------------------------------------------------

def _normalizada(estado="OPEN", etiquetas=("tarea",)):
    return {"numero": 5, "titulo": "Tarea", "estado": estado, "etiquetas": set(etiquetas), "asignados": ["Mokius"],
            "comentarios": ["Cambia el color, por favor."], "autores": ["SkiTemplar"]}


@pytest.mark.parametrize("estado", ["OPEN", "CLOSED"])
def test_la_auditoria_no_trata_las_chamber_como_trabajo(estado):
    assert not auditoria.es_de_trabajo(_normalizada(estado, ("tarea", CHAMBER)))
    assert auditoria.es_de_trabajo(_normalizada(estado))


def test_una_chamber_no_lleva_peticion_aunque_nadie_haya_contestado():
    assert auditoria.accion_peticion(_normalizada(etiquetas=("tarea", CHAMBER))) is None
    assert auditoria.accion_peticion(_normalizada()) == "poner"


def _nodo(numero, *etiquetas, estado="OPEN"):
    return {"number": numero, "state": estado, "labels": {"nodes": [{"name": e} for e in etiquetas]},
            "assignees": {"nodes": []}, "blockedBy": {"nodes": []}, "blocking": {"nodes": []}}


def test_una_pr_con_una_chamber_y_una_viva_no_necesita_lote():
    nodos = [_nodo(5, "tarea", CHAMBER, estado="CLOSED"), _nodo(6, "tarea"), _nodo(7, "tarea")]
    contexto = control.contexto_prs(nodos, {"items": {}}, [_pr(400, "Closes #5\nCloses #6")], [])
    assert contexto[6]["prs_sin_lote"] == []
    contexto = control.contexto_prs(nodos, {"items": {}}, [_pr(401, "Closes #6\nCloses #7")], [])
    assert contexto[6]["prs_sin_lote"] == [401]


def test_conversacion_no_etiqueta_una_chamber(monkeypatch, capsys):
    nodo = {**_nodo(42, "tarea", CHAMBER), "comments": {"nodes": [{"body": "¿Y esto?", "author": {"login": "Mokius"}}]}}
    llamadas = []

    def gh(*args, **_k):
        llamadas.append(args)
        return json.dumps({"data": {"repository": {"issue": nodo}}})

    monkeypatch.setattr(control, "gh", gh)
    control.cmd_conversacion(_args(numero=42, aplicar=True))
    assert len(llamadas) == 1 and "descartada" in capsys.readouterr().out


# --- colisiones -----------------------------------------------------------------------------------

def test_colision_con_una_pr_descartada_se_cierra_con_su_motivo():
    abiertas = [{"number": 300, "title": colisiones.titulo(9, 12)}, {"number": 301, "title": colisiones.titulo(9, 15)}]
    resultado = colisiones.resueltas(abiertas, prs={9, 15}, vigentes={(9, 15)}, descartadas={12})
    assert resultado == [(300, "la PR #12 solo enlaza issues descartadas (`chamber`)")]


def test_cmd_colisiones_no_cuenta_las_pr_de_issues_chamber(monkeypatch, capsys):
    monkeypatch.setattr(control, "numeros_chamber", lambda: {12})
    monkeypatch.setattr(control, "prs_abiertas", lambda: [_pr(400, "Closes #12"), _pr(401, "Closes #13")])
    contadas = []

    def traer(prs):
        contadas.append(set(prs))
        return False

    monkeypatch.setattr(colisiones, "traer_cabezas", traer)
    monkeypatch.setattr(colisiones, "ficheros_de_pr", lambda _gh, _repo, n: {"Source/a.cpp"})
    monkeypatch.setattr(colisiones, "colisiones_abiertas", lambda _gh, _repo: [])
    control.cmd_colisiones(_args(aplicar=False))
    assert contadas == [{401}]
    assert "1 PR, 0 pares" in capsys.readouterr().out


# --- volcado y puente -----------------------------------------------------------------------------

def test_volcado_deja_fuera_las_chamber_de_todas_las_secciones():
    items = _items(_item(1, "In progress", CHAMBER, abierta=False), _item(2, "In review", CHAMBER),
                   _item(3, "In review"))
    texto = volcado.render(items, {}, AHORA)
    assert "| #3 |" in texto and "1 issues abiertas" in texto
    assert "#1" not in texto and "#2" not in texto
    assert "Estado incoherente" not in texto, "una chamber cerrada fuera de Done no es incoherente"


def test_volcado_sigue_marcando_lo_incoherente_que_no_es_chamber():
    texto = volcado.render(_items(_item(1, "In progress", abierta=False)), {}, AHORA)
    assert "Estado incoherente (1)" in texto


def test_el_puente_no_admite_retomar():
    with pytest.raises(base.ErrorTablero, match="--retomar"):
        volcado.argumentos_de_puente("estado 42 Ready --retomar")
    assert volcado.argumentos_de_puente("estado 42 Ready") == ["estado", "42", "Ready"]


def test_el_puente_admite_chamber():
    assert volcado.argumentos_de_puente('chamber 12 13 --motivo "fuera del modo único"') == \
        ["chamber", "12", "13", "--motivo", "fuera del modo único"]


# --- avisos ---------------------------------------------------------------------------------------

def _fusionada(numero, refs, ficheros=("Source/a.cpp",)):
    return {"number": numero, "refs": set(refs), "files": [{"path": f} for f in ficheros],
            "mergedBy": {"login": "Mokius"}, "mergedAt": "2026-10-05T20:00:00Z"}


def test_pr_fusionada_con_codigo_de_una_chamber_es_incidencia():
    items = _items(_item(5, "Backlog", CHAMBER, abierta=False), _item(6, "In progress"))
    assert avisos.pr_sin_validar(_fusionada(1, [5]), items, [], set()).endswith("con #5 (descartada: `chamber`).")
    linea = avisos.pr_sin_validar(_fusionada(2, [5, 6]), items, [], set())
    assert "#5 (descartada: `chamber`)" in linea and "#6 (Revisión IA = vacía)" in linea


def test_pr_fusionada_organizativa_de_una_chamber_no_es_incidencia():
    items = _items(_item(5, "Backlog", CHAMBER, abierta=False))
    rutas = ["Scripts/tablero/"]
    assert avisos.pr_sin_validar(_fusionada(1, [5], ("Scripts/tablero/a.py",)), items, rutas, set()) is None


def test_avisos_por_persona_no_ensenan_chamber():
    issues = [_item(1, "In review", CHAMBER, Revisor="SkiTemplar"), _item(2, "In review", Revisor="SkiTemplar"),
              _item(3, "Revisiones", CHAMBER, "peticion")]
    secciones = avisos.secciones(issues, "SkiTemplar", True, AHORA, 3)
    assert {i["number"] for _, lista in secciones for i in lista} == {2}


def test_avisos_no_avanza_una_sin_revision_descartada():
    listos = {"Revisión IA": "Aprobada", "Editor": "Funciona"}
    proyecto = {"items": _items(_item(1, "In review", avisos.ETIQUETA, CHAMBER, **listos),
                                _item(2, "In review", avisos.ETIQUETA, **listos))}
    assert control_avisos.reconciliar(proyecto, aplicar=False) == ["#2 → Done (revisada y probada: se cierra)"]


# --- resumenes ------------------------------------------------------------------------------------

def test_resumenes_marca_las_chamber():
    viva = {"number": 5, "state": "CLOSED", "title": "Rally: vuelco", "labels": {"nodes": []}}
    descartada = {**viva, "number": 6, "labels": {"nodes": [{"name": CHAMBER}]}}
    assert control_lotes.linea_hermana(viva) == "#5 (CLOSED) Rally: vuelco"
    assert control_lotes.linea_hermana(descartada) == "#6 (CLOSED, chamber) Rally: vuelco"


# --- comando chamber ------------------------------------------------------------------------------

class GhDescarte:
    """gh simulado para `chamber`: una issue, la lista de etiquetas del repo, lo que lee GraphQL (dependencias y
    sub-issues) y las llamadas que escriben."""

    def __init__(self, issue, etiquetas=(), leida=None):
        self.issue, self.etiquetas = issue, list(etiquetas)
        self.leida = {"id": "I_42", "number": 42, **(leida or {})}
        self.escrituras, self.comentarios, self.campos = [], [], []

    def __call__(self, *args, entrada=None):
        if args[:2] == ("issue", "view"):
            return json.dumps(self.issue)
        if args[:2] == ("label", "list"):
            return json.dumps([{"name": e} for e in self.etiquetas])
        if args[:2] == ("api", "graphql"):
            return json.dumps({"data": {"repository": {"issue": self.leida}}})
        self.escrituras.append(args[:3] + tuple(a for a in args[3:] if a != "--repo" and a != base.REPO))
        return ""

    def comentar(self, numero, texto):
        self.comentarios.append((numero, texto))


def _descartar(monkeypatch, gh, *numeros, motivo="Fuera del modo único", item=None, prs=(), chamber=()):
    monkeypatch.setattr(control, "gh", gh)
    monkeypatch.setattr(control, "comentar", gh.comentar)
    monkeypatch.setattr(control, "cargar_issue", lambda n: {"items": {n: item} if item else {}})
    monkeypatch.setattr(control, "poner_campo", lambda _p, n, campo, valor: gh.campos.append((n, campo, valor)))
    monkeypatch.setattr(control, "prs_abiertas", lambda: list(prs))
    monkeypatch.setattr(control, "numeros_chamber", lambda: set(chamber))
    control.cmd_chamber(_args(numeros=list(numeros), motivo=motivo))


def test_chamber_etiqueta_limpia_comenta_aparca_y_cierra_como_not_planned(monkeypatch, capsys):
    gh = GhDescarte({"state": "OPEN", "labels": [{"name": "tarea"}, {"name": "peticion"}, {"name": "bloqueado"}],
                     "assignees": [{"login": "Mokius"}], "comments": []})
    _descartar(monkeypatch, gh, 42, 42, item=_item(42, "In review"))
    assert gh.escrituras[0][:3] == ("label", "create", CHAMBER)
    assert gh.escrituras[1:] == [("issue", "edit", "42", "--add-label", CHAMBER),
                                 ("issue", "edit", "42", "--remove-label", "bloqueado,peticion"),
                                 ("issue", "edit", "42", "--remove-assignee", "Mokius"),
                                 ("issue", "close", "42", "--reason", "not planned")]
    assert gh.comentarios == [(42, "**Descartada** (`chamber`): Fuera del modo único")]
    assert gh.campos == [(42, "Status", "Backlog")]
    assert capsys.readouterr().out.count("#42 descartada") == 1, "un número repetido se procesa una vez"


def test_chamber_es_idempotente(monkeypatch, capsys):
    texto = flujo.texto_chamber("Fuera del modo único")
    gh = GhDescarte({"state": "CLOSED", "labels": [{"name": CHAMBER}], "assignees": [], "comments": [{"body": texto}]},
                    etiquetas=[CHAMBER])
    _descartar(monkeypatch, gh, 42, item=_item(42, "Backlog", CHAMBER, abierta=False))
    assert gh.escrituras == [] and gh.comentarios == [] and gh.campos == []
    assert "#42 ya estaba descartada" in capsys.readouterr().out


def test_chamber_avisa_de_las_pr_abiertas_que_la_enlazan(monkeypatch, capsys):
    gh = GhDescarte({"state": "OPEN", "labels": [], "assignees": [], "comments": []}, etiquetas=[CHAMBER])
    prs = [_pr(400, "Closes #42"), _pr(401, "Closes #42\nCloses #43"), _pr(402, "Closes #43")]
    _descartar(monkeypatch, gh, 42, prs=prs)
    salida = capsys.readouterr().out
    assert "Aviso: la PR #400 (Mokius) enlaza #42, descartada: ciérrala" in salida
    assert "Aviso: la PR #401 (Mokius) enlaza #42, descartada: quítale el «Closes» de #42" in salida
    assert "PR #402" not in salida
    assert not [e for e in gh.escrituras if e[0] == "pr"], "las PR no se cierran solas"


def test_chamber_avisa_de_lotes_dependientes_y_sub_issues_sin_propagar_el_descarte(monkeypatch, capsys):
    leida = _leida(blocking=[_nodo(130, "lote"), _nodo(57, "tarea")], sub_issues=[_nodo(60, "tarea")])
    gh = GhDescarte({"state": "OPEN", "labels": [{"name": "objeto"}], "assignees": [], "comments": []},
                    etiquetas=[CHAMBER], leida=leida)
    _descartar(monkeypatch, gh, 42, item={"number": 42, "valores": {}})
    salida = capsys.readouterr().out
    assert "miembro del lote #130" in salida and "#57 dependían de #42" in salida
    assert "las sub-issues #60 de #42 siguen vivas" in salida
    assert gh.campos == [], "un objeto no lleva Status"
    assert [e for e in gh.escrituras if e[2] != "42"] == [], "solo toca la issue descartada"


def test_chamber_sin_nada_colgando_no_avisa(monkeypatch, capsys):
    gh = GhDescarte({"state": "OPEN", "labels": [], "assignees": [], "comments": []}, etiquetas=[CHAMBER])
    _descartar(monkeypatch, gh, 42)
    assert "Aviso" not in capsys.readouterr().out


def test_prs_con_descartadas_cuenta_las_descartadas_de_antes():
    prs = [_pr(401, "Closes #42\nCloses #43")]
    assert control.prs_con_descartadas(prs, {42}, {43}) == [
        "Aviso: la PR #401 (Mokius) enlaza #42, descartada: ciérrala"]
    assert control.prs_con_descartadas(prs, {50}, {43}) == []


def test_chamber_sin_motivo_no_toca_nada(monkeypatch):
    gh = GhDescarte({"state": "OPEN", "labels": [], "assignees": [], "comments": []})
    with pytest.raises(base.ErrorTablero, match="motivo"):
        _descartar(monkeypatch, gh, 42, motivo="   ")
    assert gh.escrituras == []


def _parser(nombre):
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd")
    tablero.anadir_comandos_de_flujo(sub)
    control.anadir_comandos(sub)
    return sub.choices[nombre]


def test_chamber_esta_en_la_ayuda_y_exige_motivo():
    p = _parser("chamber")
    assert p.parse_args(["12", "13", "--motivo", "x"]).numeros == [12, 13]
    with pytest.raises(SystemExit):
        p.parse_args(["12"])


# --- lote estado ----------------------------------------------------------------------------------

def _lote_estado(monkeypatch, miembros, items):
    """Lanza `lote estado 130` con `miembros` (nodos de blockedBy) y las tarjetas `items` del tablero."""
    def gh(*args, **_k):
        if args[:2] == ("issue", "view"):
            return json.dumps({"title": "Lote: x", "labels": [{"name": "lote"}], "state": "OPEN"})
        leida = {"id": "I_130", "number": 130, "blockedBy": {"nodes": miembros}}
        return json.dumps({"data": {"repository": {"issue": leida}}})

    monkeypatch.setattr(control_lotes, "gh", gh)
    monkeypatch.setattr(control_lotes, "cargar_proyecto", lambda: {"items": items})
    control_lotes.cmd_lote_estado(_args(numero=130))


LISTA = {"Revisión IA": "Aprobada", "Editor": "Funciona"}


@pytest.mark.parametrize("en_tablero", [True, False])
def test_lote_estado_no_deja_fusionar_con_un_miembro_descartado(monkeypatch, en_tablero):
    miembros = [_nodo(57, "tarea"), _nodo(58, "tarea", *(() if en_tablero else (CHAMBER,)), estado="CLOSED")]
    items = _items(_item(57, "Validada", **LISTA), *([_item(58, "Backlog", CHAMBER, abierta=False)] if en_tablero else []))
    with pytest.raises(base.ErrorTablero, match="#58 descartada"):
        _lote_estado(monkeypatch, miembros, items)


def test_lote_estado_deja_fusionar_sin_descartadas(monkeypatch, capsys):
    miembros = [_nodo(57, "tarea"), _nodo(58, "tarea")]
    _lote_estado(monkeypatch, miembros, _items(_item(57, "Validada", **LISTA), _item(58, "Validada", **LISTA)))
    assert "se puede fusionar" in capsys.readouterr().out
