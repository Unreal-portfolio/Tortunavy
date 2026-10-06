"""Tests de `lote añadir`, de `bloquear` sobre lotes y del aviso de issues cerradas fuera de su lote (sin red ni gh)."""

import argparse
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tablero"))
import auditoria  # noqa: E402
import base  # noqa: E402
import control  # noqa: E402
import control_lotes  # noqa: E402
import lotes  # noqa: E402
import objetos  # noqa: E402

CUERPO_LOTE = lotes.cuerpo([440, 441], 500)


# --- Funciones puras -------------------------------------------------------------------------------

def test_miembros_nuevos_sin_los_que_ya_estan():
    assert lotes.miembros_nuevos(439, [441, 442, 442, 443], {440, 441}) == [442, 443]
    with pytest.raises(lotes.ErrorLote):
        lotes.miembros_nuevos(439, [440, 441], {440, 441})
    with pytest.raises(lotes.ErrorLote):
        lotes.miembros_nuevos(439, [439], set())


def test_cuerpo_con_miembros_tras_la_ultima_casilla():
    nuevo = lotes.cuerpo_con_miembros(CUERPO_LOTE, [442])
    assert "- [ ] #441\n- [ ] #442\n" in nuevo
    assert nuevo.count("PR del lote") == 1
    assert lotes.cuerpo_con_miembros("Sin lista.", [442]) == "Sin lista.\n\n- [ ] #442"


def test_fuera_del_lote():
    assert lotes.fuera_del_lote({440, 442}, {439: {440, 441}}) == {442: [439]}
    assert lotes.fuera_del_lote({440, 441}, {439: {440, 441}}) == {}
    assert lotes.fuera_del_lote({442}, {}) == {}


# --- Aviso en auditar ------------------------------------------------------------------------------

def _nodo(numero, *etiquetas, bloqueantes=()):
    return {"number": numero, "assignees": {"nodes": []}, "labels": {"nodes": [{"name": e} for e in etiquetas]},
            "blockedBy": {"nodes": [{"number": b, "state": "OPEN"} for b in bloqueantes]}}


def test_issue_cerrada_por_la_pr_de_un_lote_sin_ser_miembro():
    nodos = [_nodo(439, "lote", bloqueantes=(440, 441)), _nodo(440, "tarea"), _nodo(441, "tarea"), _nodo(442, "tarea")]
    pr = {"number": 500, "baseRefName": "dev", "headRefName": "feat/440-x",
          "body": "Closes #441\nCloses #442\nRefs #439"}
    contexto = control.contexto_prs(nodos, {"items": {}}, [pr], [])
    assert contexto[442]["fuera_de_lote"] == [(500, 439)]
    assert contexto[440]["fuera_de_lote"] == contexto[441]["fuera_de_lote"] == []
    assert all(not c["prs_sin_lote"] for c in contexto.values())  # el lote se enlaza con «Refs #439»
    issue = auditoria.normalizar({**nodos[3], "title": "Tarea", "state": "OPEN", "comments": {"nodes": []}},
                                 {}, contexto[442])
    textos = [p["texto"] for p in auditoria.organizacion_abierta(issue)]
    assert any(t.startswith("la cierra la PR #500 pero no es miembro del lote #439") for t in textos)


# --- Comandos con gh simulado ----------------------------------------------------------------------

class GhFalso:
    """Registra las llamadas a gh y responde a las lecturas que usan los comandos."""

    def __init__(self, respuestas: dict[str, str]):
        self.respuestas, self.llamadas = respuestas, []

    def __call__(self, *args, entrada=None):
        self.llamadas.append((args, entrada))
        return next((r for clave, r in self.respuestas.items() if clave in " ".join(args)), "{}")


def _leer_issue(numero_lote, miembros):
    def leer(_gh, _repo, numero):
        return {"id": f"ID{numero}", "number": numero,
                "blockedBy": {"nodes": [{"number": m, "state": "OPEN"} for m in miembros]
                              if numero == numero_lote else []}}
    return leer


def _proyecto(*lotes_):
    items = {n: {"valores": {}, "labels": {"nodes": [{"name": "lote" if n in lotes_ else "tarea"}]}}
             for n in (439, 440, 441, 442)}
    return {"items": items}


def test_lote_anadir_vincula_comenta_y_anade_la_casilla(monkeypatch, capsys):
    vista = json.dumps({"title": "Lote: x", "labels": [{"name": "lote"}], "state": "OPEN", "body": CUERPO_LOTE})
    gh = GhFalso({"issue view": vista})
    comentarios = []
    monkeypatch.setattr(control_lotes, "gh", gh)
    monkeypatch.setattr(objetos, "leer_issue", _leer_issue(439, [440, 441]))
    monkeypatch.setattr(control_lotes, "cargar_proyecto", lambda: _proyecto(439))
    monkeypatch.setattr(control_lotes, "comentar", lambda n, t: comentarios.append((n, t)))
    monkeypatch.setattr(control_lotes, "prs_abiertas",
                        lambda: [{"number": 500, "headRefName": "feat/440-x", "body": "Closes #440\nRefs #439"}])
    control_lotes.cmd_lote_anadir(argparse.Namespace(lote=439, miembros=[441, 442]))
    mutaciones = [a for a, _ in gh.llamadas if a[:2] == ("api", "graphql")]
    assert len(mutaciones) == 1 and "bloqueante=ID442" in mutaciones[0]
    assert comentarios == [(442, "En el lote #439 (PR #500).")]
    edicion = next(e for a, e in gh.llamadas if a[:2] == ("issue", "edit"))
    assert "- [ ] #442" in edicion
    assert "añade «Closes #442» al cuerpo de la PR #500" in capsys.readouterr().out


def test_lote_anadir_rechaza_lo_que_no_es_un_lote(monkeypatch):
    vista = json.dumps({"title": "Tarea", "labels": [{"name": "tarea"}], "state": "OPEN", "body": ""})
    monkeypatch.setattr(control_lotes, "gh", GhFalso({"issue view": vista}))
    with pytest.raises(base.ErrorTablero):
        control_lotes.cmd_lote_anadir(argparse.Namespace(lote=440, miembros=[442]))


@pytest.mark.parametrize("numero, cambia", [(439, False), (440, True)])
def test_bloquear_un_lote_no_le_pone_status_ni_etiqueta(monkeypatch, numero, cambia):
    gh, campos = GhFalso({}), []
    monkeypatch.setattr(control, "gh", gh)
    monkeypatch.setattr(objetos, "leer_issue", _leer_issue(439, []))
    proyecto = _proyecto(439)
    proyecto["items"][440]["valores"] = {"Status": "Ready"}
    monkeypatch.setattr(control, "cargar_issue", lambda _n: proyecto)
    monkeypatch.setattr(control, "poner_campo", lambda *a: campos.append(a[1:]))
    control.cmd_bloquear(argparse.Namespace(numero=numero, por=[442]))
    etiquetado = any(a[:2] == ("issue", "edit") for a, _ in gh.llamadas)
    assert (campos == [(numero, "Status", "Bloqueada")]) is cambia
    assert etiquetado is cambia
    assert any(a[:2] == ("api", "graphql") for a, _ in gh.llamadas)  # la dependencia se registra siempre


def test_el_subcomando_lote_anadir_existe():
    parser = argparse.ArgumentParser()
    control_lotes.anadir_comandos(parser.add_subparsers())
    args = parser.parse_args(["lote", "añadir", "439", "441", "442"])
    assert args.fn is control_lotes.cmd_lote_anadir and args.lote == 439 and args.miembros == [441, 442]
