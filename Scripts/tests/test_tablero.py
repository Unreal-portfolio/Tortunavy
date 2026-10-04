"""Tests de las funciones puras de Scripts/tablero/tablero.py (sin red ni gh)."""

import importlib.util
import sys
from pathlib import Path

import pytest

CARPETA = Path(__file__).resolve().parents[1] / "tablero"
sys.path.insert(0, str(CARPETA))  # tablero.py importa objetos.py de su misma carpeta
import objetos  # noqa: E402

RUTA = CARPETA / "tablero.py"
spec = importlib.util.spec_from_file_location("tablero", RUTA)
assert spec is not None and spec.loader is not None
tablero = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tablero)


def test_slug_quita_tildes_y_simbolos():
    assert tablero.slug("Puente tambaleante: Excitation en uint8 (10 Hz)") == "puente-tambaleante-excitation-en-uint8-1"
    assert tablero.slug("Validar el salto de la tortuga en PIE") == "validar-el-salto-de-la-tortuga-en-pie"


def test_slug_no_termina_en_guion_al_cortar():
    assert not tablero.slug("a" * 39 + " bbbb").endswith("-")


def test_issues_de_pr_lee_cuerpo_y_rama():
    pr = {"body": "Arregla el puente.\n\nCloses #19\nFixes #21\nRefs #33", "headRefName": "fix/20-puente-tambaleante"}
    assert tablero.issues_de_pr(pr) == {19, 20, 21}
    assert tablero.issues_de_pr(pr, menciones=True) == {19, 20, 21, 33}


def test_issues_de_pr_solo_palabras_completas():
    pr = {"body": "Ver hotfix #5 y prefs #6; cierra #7", "headRefName": "nube/x"}
    assert tablero.issues_de_pr(pr, menciones=True) == {7}


def _pr_abierta(numero, cuerpo, rama="feat/x"):
    return {"number": numero, "baseRefName": "dev", "headRefName": rama, "body": cuerpo, "mergeable": "MERGEABLE",
            "author": {"login": "Mokius"}}


def _item(estado, *etiquetas, **valores):
    return {"state": "OPEN", "valores": {"Status": estado, **valores},
            "labels": {"nodes": [{"name": e} for e in ("tarea", *etiquetas)]}}


def test_refs_no_mueve_la_issue_citada_y_closes_si(monkeypatch):
    """#356 solo citada con «Refs» no pasa a In review; #12, que la PR cierra, sí."""
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [_pr_abierta(400, "Closes #12\nRefs #356")])
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [])
    proyecto = {"items": {12: _item("In progress"), 356: _item("Ready")}}
    cambios, avisos = [], []
    tablero.reconciliar_prs(proyecto, cambios, avisos)
    assert [texto for texto, _ in cambios] == ["#12 → In review (PR #400)"]
    assert not avisos


def test_pr_abierta_no_mueve_una_issue_con_decision_pendiente(monkeypatch):
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [_pr_abierta(400, "Closes #12")])
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [])
    proyecto = {"items": {12: _item("Ready", "decision")}}
    cambios, avisos = [], []
    tablero.reconciliar_prs(proyecto, cambios, avisos)
    assert not cambios
    assert avisos == ["#12 tiene PR abierta (#400) pero espera una decisión (`decision`): no pasa a In review"]


def test_refs_en_una_pr_fusionada_no_cuenta_como_fusion(monkeypatch):
    pr = {"number": 401, "baseRefName": "dev", "headRefName": "feat/x", "body": "Refs #356"}
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [pr])
    proyecto = {"items": {356: _item("In review", **{"Revisión IA": "Aprobada", "Editor": "Funciona"})}}
    cambios, avisos = [], []
    tablero.reconciliar_fusiones(proyecto, [], cambios, avisos)
    assert not cambios
    assert not tablero.esta_fusionada(356, [pr], [])
    assert tablero.esta_fusionada(12, [{**pr, "body": "Closes #12"}], [])


def test_lote_enlazado_con_refs_se_cierra_al_fusionar(monkeypatch):
    pr = {"number": 402, "baseRefName": "dev", "headRefName": "feat/440-x", "body": "Closes #440\nRefs #439"}
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [pr])
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [])
    monkeypatch.setattr(tablero, "tiene_resumen", lambda n: True)
    lote = {"state": "OPEN", "valores": {}, "labels": {"nodes": [{"name": "lote"}]},
            "blockedBy": {"nodes": [{"number": 440, "state": "CLOSED"}, {"number": 441, "state": "CLOSED"}]}}
    cambios, avisos = [], []
    tablero.reconciliar_lotes({"items": {439: lote}}, cambios, avisos)
    assert [texto for texto, _ in cambios] == ["lote #439 se cierra: PR #402 fusionada y todos sus miembros cerrados"]


def test_issues_de_pr_sin_referencias():
    assert tablero.issues_de_pr({"body": "Mejora de rendimiento #sinissue", "headRefName": "nube/optim-2026-09-29"}) == set()


def test_orden_prioridad_antes_que_tamano():
    alta_grande = {"number": 5, "valores": {"Prioridad": "P0", "Tamaño": "L"}}
    baja_pequena = {"number": 1, "valores": {"Prioridad": "P2", "Tamaño": "XS"}}
    sin_campos = {"number": 2, "valores": {}}
    orden = sorted([baja_pequena, sin_campos, alta_grande], key=tablero.clave_orden)
    assert [i["number"] for i in orden] == [5, 1, 2]


def _proyecto_con_revisiones(*revisores):
    items = {n: {"valores": {"Status": "In review", "Revisor": r}} for n, r in enumerate(revisores)}
    return {"items": items}


def test_revisor_cruzado_nunca_el_autor():
    proyecto = _proyecto_con_revisiones()
    assert tablero.elegir_revisor(proyecto, "SkiTemplar") == "Mokius"
    assert tablero.elegir_revisor(proyecto, "Mokius") == "SkiTemplar"


def test_revisor_de_rubi_reparte_carga():
    assert tablero.elegir_revisor(_proyecto_con_revisiones(), "Ruben-Besteiro") == "Mokius"
    assert tablero.elegir_revisor(_proyecto_con_revisiones("Mokius"), "Ruben-Besteiro") == "SkiTemplar"


def test_es_objeto_con_etiquetas_de_gh_y_del_proyecto():
    assert objetos.es_objeto({"labels": [{"name": "objeto"}]})
    assert objetos.es_objeto({"labels": {"nodes": [{"name": "tarea"}, {"name": "objeto"}]}})
    assert not objetos.es_objeto({"labels": {"nodes": [{"name": "tarea"}]}})
    assert not objetos.es_objeto({})


def test_buscar_por_titulo_exacto_y_solo_abiertas():
    issues = [
        {"number": 3, "title": "Rally Tortuga", "state": "CLOSED"},
        {"number": 7, "title": "Rally Tortuga · Red", "state": "OPEN"},
        {"number": 9, "title": " Rally Tortuga ", "state": "OPEN"},
    ]
    assert objetos.buscar_por_titulo(issues, "Rally Tortuga") == 9
    assert objetos.buscar_por_titulo(issues, "rally tortuga") is None
    assert objetos.buscar_por_titulo([], "Rally Tortuga") is None


def test_comprobar_padre_idempotente_y_sin_robar_hijos():
    assert objetos.comprobar_padre(20, 90, None) is True
    assert objetos.comprobar_padre(20, 90, 90) is False
    with pytest.raises(objetos.ErrorObjeto, match="ya cuelga de #40"):
        objetos.comprobar_padre(20, 90, 40)
    with pytest.raises(objetos.ErrorObjeto):
        objetos.comprobar_padre(90, 90, None)


def test_cuerpo_objeto_usa_la_descripcion_o_el_nombre():
    assert objetos.cuerpo_objeto("HUD y menús", "HUD, tutorial y ajustes.").startswith("HUD, tutorial y ajustes.")
    assert objetos.cuerpo_objeto("HUD y menús", None).startswith("Objeto «HUD y menús».")
    assert "vista «Objetos»" in objetos.cuerpo_objeto("X", "")




def _con_etiquetas(numero, *etiquetas, quien="Ruben-Besteiro"):
    return {"number": numero, "labels": {"nodes": [{"name": e} for e in etiquetas]},
            "assignees": {"nodes": [{"login": quien}]}, "valores": {}}


def test_colisiones_y_organizacion_van_primero():
    issues = [_con_etiquetas(1, "colision", quien="Mokius"), _con_etiquetas(2, "revisar-organizacion"),
              _con_etiquetas(3, "revisar-organizacion", quien="Mokius"), _con_etiquetas(4, "tarea")]
    assert [i["number"] for i in tablero.urgentes_de_organizacion(issues, "Ruben-Besteiro", False)] == [1, 2]
    assert [i["number"] for i in tablero.urgentes_de_organizacion(issues, "SkiTemplar", True)] == [1, 2, 3]


def test_urgentes_incluye_los_avisos_de_la_rutina_de_qa():
    def issue(numero, etiqueta, asignado=None):
        return {"number": numero, "labels": {"nodes": [{"name": etiqueta}]},
                "assignees": {"nodes": [{"login": asignado}] if asignado else []}}
    issues = [issue(1, "revisar-qa", "Mokius"), issue(2, "revisar-organizacion"), issue(3, "tarea", "Mokius")]
    assert [i["number"] for i in tablero.urgentes_de_organizacion(issues, "SkiTemplar", aprobador=True)] == [1, 2]
    # Quien no es aprobador solo ve los avisos de lo suyo.
    assert [i["number"] for i in tablero.urgentes_de_organizacion(issues, "Mokius", aprobador=False)] == [1]
    assert tablero.urgentes_de_organizacion(issues, "Ruben-Besteiro", aprobador=False) == []


def _issue_etiquetada(numero, etiquetas, asignado=None):
    return {"number": numero, "labels": {"nodes": [{"name": e} for e in etiquetas]},
            "assignees": {"nodes": [{"login": asignado}] if asignado else []}, "valores": {}}


def test_coger_con_decision_pendiente_consulta_antes():
    pendiente = _issue_etiquetada(44, ["tarea", "decision"])
    motivo = tablero.motivo_decision(44, pendiente, forzar=False)
    assert "decisión pendiente" in motivo and "--forzar" in motivo and "decidir 44" in motivo
    # NEGATIVO: forzando, o sin la etiqueta, se coge como siempre.
    assert tablero.motivo_decision(44, pendiente, forzar=True) is None
    assert tablero.motivo_decision(45, _issue_etiquetada(45, ["tarea"]), forzar=False) is None


def test_decisiones_todas_para_aprobadores_y_las_suyas_para_el_resto():
    issues = [_issue_etiquetada(44, ["decision"], "Ruben-Besteiro"), _issue_etiquetada(128, ["objeto", "decision"]),
              _issue_etiquetada(12, ["tarea"], "Ruben-Besteiro")]
    assert [i["number"] for i in tablero.con_decision(issues, "SkiTemplar", aprobador=True)] == [44, 128]
    assert [i["number"] for i in tablero.con_decision(issues, "Ruben-Besteiro", aprobador=False)] == [44]
    assert tablero.con_decision(issues, "Otro", aprobador=False) == []


def test_objetos_parecidos_evitan_duplicados():
    abiertos = [{"number": 95, "title": "HUD y menús", "state": "OPEN"},
                {"number": 93, "title": "Red · Conexión y lobby", "state": "OPEN"},
                {"number": 128, "title": "HUD", "state": "CLOSED"}]
    assert [o["number"] for o in objetos.parecidos(abiertos, "HUD")] == [95]
    assert [o["number"] for o in objetos.parecidos(abiertos, "Menus del juego")] == [95]
    assert objetos.parecidos(abiertos, "Supervivencia") == []
    assert objetos.palabras_clave("Voz y audio de la sala") == {"voz", "audio", "sala"}


def test_buscar_o_crear_no_crea_un_objeto_parecido_sin_pedirlo():
    creadas = []

    def gh(*args, **_):
        if args[:2] == ("issue", "list"):
            return '[{"number": 95, "title": "HUD y menús", "state": "OPEN"}]'
        if args[:2] == ("label", "list"):
            return '[{"name": "objeto"}]'
        creadas.append(args)
        return "https://github.com/x/y/issues/200\n"

    assert objetos.buscar_o_crear(gh, "x/y", "HUD y menús") == (95, False)
    with pytest.raises(objetos.ErrorObjeto, match="#95 «HUD y menús»"):
        objetos.buscar_o_crear(gh, "x/y", "HUD")
    assert not creadas
    assert objetos.buscar_o_crear(gh, "x/y", "HUD", nuevo=True) == (200, True)
    assert objetos.buscar_o_crear(gh, "x/y", "Supervivencia") == (200, True)


def test_fusion_de_la_pr_de_un_lote_no_da_status_al_lote(monkeypatch):
    """PR #166 (30-09): «Refs #167» no debe mover la issue del lote; sus miembros, sí."""
    pr = {"number": 166, "baseRefName": "dev", "headRefName": "feat/144-supervivencia", "body": "Closes #144\nRefs #167"}
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [pr])
    proyecto = {"items": {
        144: {"state": "OPEN", "valores": {"Status": "In review", "Revisión IA": "Aprobada", "Editor": "Sin probar"},
              "labels": {"nodes": [{"name": "tarea"}]}},
        167: {"state": "OPEN", "valores": {}, "labels": {"nodes": [{"name": "lote"}]}},
    }}
    cambios, avisos = [], []
    tablero.reconciliar_fusiones(proyecto, [], cambios, avisos)
    assert [texto for texto, _ in cambios] == ["#144 → QA editor (PR #166 fusionada en dev)"]


def test_issue_en_done_antes_de_fusionar_se_cierra(monkeypatch):
    """#436: el ciclo de #282 pasa la issue a Done antes de fusionar y `Closes #n` no cierra en dev."""
    pr = {"number": 279, "baseRefName": "dev", "headRefName": "feat/271-mapa", "body": "Closes #271"}
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [pr])
    monkeypatch.setattr(tablero, "tiene_resumen", lambda n: n != 272)
    validada = {"Status": "Done", "Revisión IA": "Aprobada", "Editor": "Funciona"}
    proyecto = {"items": {
        271: {"state": "OPEN", "valores": dict(validada), "labels": {"nodes": [{"name": "tarea"}]}},
    }}
    cambios, avisos = [], []
    tablero.reconciliar_fusiones(proyecto, [], cambios, avisos)
    assert [texto for texto, _ in cambios] == ["#271 → Done (PR #279 fusionada en dev; ya estaba en Done: se cierra)"]
    assert not avisos

    pr["body"] = "Closes #272"
    proyecto["items"] = {272: {"state": "OPEN", "valores": dict(validada), "labels": {"nodes": [{"name": "tarea"}]}}}
    cambios, avisos = [], []
    tablero.reconciliar_fusiones(proyecto, [], cambios, avisos)
    assert len(cambios) == 1 and avisos == ["#272 se cierra sin comentario **Resumen**: añádelo con `resumen 272`"]


def test_miembro_de_lote_en_done_se_cierra_en_done_al_fusionar(monkeypatch):
    """#436 (puente 03-10): el mismo `sync` lo pasaba a Validada por el lote y lo cerraba sin volver a Done."""
    pr = {"number": 438, "baseRefName": "dev", "headRefName": "fix/436-sync", "body": "Closes #436\nRefs #439"}
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [pr])
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [])
    monkeypatch.setattr(tablero, "tiene_resumen", lambda n: True)
    estados, cerradas = {}, []
    monkeypatch.setattr(tablero, "poner_campo", lambda proyecto, n, campo, valor: estados.__setitem__((n, campo), valor))
    monkeypatch.setattr(tablero, "comentar", lambda n, texto: None)
    monkeypatch.setattr(tablero, "gh", lambda *args: cerradas.append(int(args[2])) if args[:2] == ("issue", "close") else "")
    lote = {"number": 439, "state": "OPEN", "labels": {"nodes": [{"name": "lote"}]}}
    miembro = _item("Done", **{"Revisión IA": "Aprobada", "Editor": "Funciona"})
    miembro["blocking"] = {"nodes": [lote]}
    proyecto = {"items": {436: miembro}}
    cambios, avisos = [], []
    tablero.reconciliar_lotes(proyecto, cambios, avisos)
    tablero.reconciliar_fusiones(proyecto, [], cambios, avisos)
    assert [texto for texto, _ in cambios] == ["#436 → Done (PR #438 fusionada en dev; ya estaba en Done: se cierra)"]
    for _texto, accion in cambios:
        accion()
    assert estados == {(436, "Status"): "Done"}
    assert cerradas == [436]


def test_miembro_de_lote_sin_fusionar_sigue_pasando_a_validada(monkeypatch):
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [])
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [_pr_abierta(438, "Closes #436\nRefs #439")])
    miembro = _item("In review", **{"Revisión IA": "Aprobada", "Editor": "Funciona"})
    miembro["blocking"] = {"nodes": [{"number": 439, "state": "OPEN", "labels": {"nodes": [{"name": "lote"}]}}]}
    cambios, avisos = [], []
    tablero.reconciliar_lotes({"items": {436: miembro}}, cambios, avisos)
    assert [texto for texto, _ in cambios] == ["#436 → Validada (aprobada y probada; espera al resto de su lote)"]


@pytest.mark.parametrize("valores, abiertas", [
    ({"Status": "Done", "Revisión IA": "Aprobada", "Editor": "Sin probar"}, []),
    ({"Status": "Done", "Revisión IA": "Pendiente", "Editor": "Funciona"}, []),
    ({"Status": "Done", "Revisión IA": "Aprobada", "Editor": "Funciona"},
     [{"number": 300, "body": "Closes #271", "headRefName": "fix/271-otra"}]),
])
def test_done_sin_las_dos_validaciones_o_con_pr_abierta_no_se_cierra(monkeypatch, valores, abiertas):
    pr = {"number": 279, "baseRefName": "dev", "headRefName": "feat/271-mapa", "body": "Closes #271"}
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [pr])
    proyecto = {"items": {271: {"state": "OPEN", "valores": valores, "labels": {"nodes": [{"name": "tarea"}]}}}}
    cambios, avisos = [], []
    tablero.reconciliar_fusiones(proyecto, abiertas, cambios, avisos)
    assert not cambios


def test_comprobar_campos_rechaza_opciones_que_no_existen_y_admite_vacios():
    from base import ErrorTablero, comprobar_campos

    proyecto = {"campos": {"Área": {"id": "A", "opciones": {"Red": "1", "Personaje": "2"}},
                           "Prioridad": {"id": "P", "opciones": {"P0": "a"}}}}
    comprobar_campos(proyecto, {"Área": "Personaje", "Prioridad": "P0", "Editor": None})
    with pytest.raises(ErrorTablero, match="Área"):
        comprobar_campos(proyecto, {"Área": "Arte", "Prioridad": "P0"})
    with pytest.raises(ErrorTablero, match="Fase"):
        comprobar_campos(proyecto, {"Fase": "F1"})


def _gh_con_auto_add(ediciones: list, error_item_add: str = "Content already exists in this project"):
    """gh falso: crea la issue #700, `item-add` falla como cuando el auto-add del Project se adelanta."""
    import json

    from base import NUMERO, ErrorTablero

    def gh(*args, entrada=None):
        if args[:2] == ("issue", "create"):
            return "https://github.com/Unreal-portfolio/Tortunavy/issues/700\n"
        if args[:2] == ("project", "item-add"):
            raise ErrorTablero(f"gh project item-add {NUMERO}…: {error_item_add}")
        if args[:2] == ("api", "graphql"):
            issue = {"number": 700, "title": "Rally: fallo", "state": "OPEN", "url": "u", "updatedAt": "t",
                     "assignees": {"nodes": []}, "labels": {"nodes": []},
                     "blockedBy": {"nodes": []}, "blocking": {"nodes": []},
                     "projectItems": {"nodes": [{"id": "PVTI_auto", "project": {"number": NUMERO},
                                                 "fieldValues": {"nodes": []}}]}}
            return json.dumps({"data": {"repository": {"issue": issue}}})
        if args[:2] == ("project", "item-edit"):
            ediciones.append((args[args.index("--id") + 1], args[args.index("--field-id") + 1],
                              args[args.index("--single-select-option-id") + 1]))
            return ""
        raise AssertionError(f"llamada inesperada a gh: {args}")

    return gh


def _proyecto_vacio():
    opciones = {"Status": ["Ready"], "Prioridad": ["P1"], "Tamaño": ["S"], "Área": ["Red"], "Fase": ["F4"],
                "Editor": ["Sin probar"]}
    return {"id": "PVT_1", "items": {},
            "campos": {c: {"id": f"F_{c}", "opciones": {v: f"O_{v}" for v in vs}} for c, vs in opciones.items()}}


def test_nueva_sigue_con_los_campos_si_el_auto_add_se_adelanta(monkeypatch, tmp_path):
    """#699: «Content already exists» en item-add no deja la issue sin campos ni estado."""
    import argparse

    import base

    ediciones = []
    gh_falso = _gh_con_auto_add(ediciones)
    monkeypatch.setattr(base, "gh", gh_falso)
    monkeypatch.setattr(tablero, "gh", gh_falso)
    monkeypatch.setattr(tablero, "cargar_proyecto", _proyecto_vacio)
    monkeypatch.setattr(tablero.auditoria, "problemas_de_formato", lambda *_: [])
    cuerpo = tmp_path / "cuerpo.md"
    cuerpo.write_text("Contexto\n\n- [ ] criterio\n", encoding="utf-8")
    args = argparse.Namespace(padre=None, objeto=None, titulo="Rally: fallo", tipo="bug", etiqueta=[],
                              cuerpo=str(cuerpo), estado="Ready", prioridad="P1", tamano="S", area="Red", fase="F4")
    tablero.cmd_nueva(args)
    assert ediciones == [("PVTI_auto", "F_Status", "O_Ready"), ("PVTI_auto", "F_Prioridad", "O_P1"),
                         ("PVTI_auto", "F_Tamaño", "O_S"), ("PVTI_auto", "F_Área", "O_Red"),
                         ("PVTI_auto", "F_Fase", "O_F4")]


def test_item_de_issue_no_traga_otros_errores_de_item_add(monkeypatch):
    import base

    monkeypatch.setattr(base, "gh", _gh_con_auto_add([], error_item_add="HTTP 502: Bad Gateway"))
    with pytest.raises(base.ErrorTablero, match="Bad Gateway"):
        base.item_de_issue(_proyecto_vacio(), 700)
