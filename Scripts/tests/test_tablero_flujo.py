"""Tests del ciclo (flujo.py, base.py), dependencias (bloqueos.py) y opciones de Status (estados.py)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tablero"))
import base  # noqa: E402
import bloqueos  # noqa: E402
import estados  # noqa: E402
import flujo  # noqa: E402

LISTA = {"Revisión IA": "Aprobada", "Editor": "Funciona"}


# --- Estados: In progress, In review, Revisiones, QA editor, Done y Validada (lotes) ----------------

def test_aprobada_fusionada_y_probada_cierra():
    assert flujo.estado_objetivo("In review", LISTA, fusionada=True, en_lote=False) == ("Done", True)


@pytest.mark.parametrize("valores, esperado", [
    ({"Revisión IA": "Aprobada"}, "QA editor"),                              # aprobada sin probar
    ({"Revisión IA": "Aprobada", "Editor": "Sin probar"}, "QA editor"),
    ({"Editor": "Funciona"}, "In review"),                                   # probada, sin revisión aprobada
    ({"Revisión IA": "Pendiente", "Editor": "Funciona"}, "In review"),
    ({}, "In review"),
])
def test_fusionada_sin_las_dos_validaciones_no_llega_a_done(valores, esperado):
    assert flujo.estado_objetivo("In review", valores, fusionada=True, en_lote=False) == (esperado, False)


def test_validada_solo_para_miembros_de_un_lote():
    assert flujo.estado_objetivo("In review", LISTA, fusionada=False, en_lote=True) == ("Validada", False)
    # Una issue suelta también espera en Validada antes de fusionar (#282).
    assert flujo.estado_objetivo("In review", LISTA, fusionada=False, en_lote=False) == ("Validada", False)


@pytest.mark.parametrize("valores", [{}, {"Editor": "Funciona"}])
def test_miembro_de_lote_a_medias_no_se_valida(valores):
    assert flujo.estado_objetivo("In review", valores, fusionada=False, en_lote=True) == (None, False)


@pytest.mark.parametrize("en_lote", [True, False])
@pytest.mark.parametrize("editor", [None, "Sin probar"])
def test_aprobada_sin_probar_y_sin_fusionar_pasa_a_qa_editor(en_lote, editor):
    valores = {"Revisión IA": "Aprobada", "Editor": editor}
    assert flujo.estado_objetivo("In review", valores, fusionada=False, en_lote=en_lote) == ("QA editor", False)
    # Ya en QA editor no se mueve; probada en el editor pasa a Validada.
    assert flujo.estado_objetivo("QA editor", valores, fusionada=False, en_lote=en_lote) == (None, False)
    assert flujo.estado_objetivo("QA editor", LISTA, fusionada=False, en_lote=en_lote) == ("Validada", False)


@pytest.mark.parametrize("estado", ["Revisiones", "In progress", "Bloqueada"])
def test_aprobada_sin_probar_fuera_de_in_review_no_se_mueve(estado):
    # En Revisiones la tiene un fallo o una petición; en In progress, alguien trabajando.
    valores = {"Revisión IA": "Aprobada", "Editor": "Sin probar"}
    assert flujo.estado_objetivo(estado, valores, fusionada=False, en_lote=True) == (None, False)


def test_lote_fusionado_con_todo_validado_pasa_a_done():
    assert flujo.estado_objetivo("Validada", LISTA, fusionada=True, en_lote=True) == ("Done", True)


@pytest.mark.parametrize("valores", [
    {"Revisión IA": "Aprobada", "Editor": "Falla"},
    {"Revisión IA": "Cambios pedidos", "Editor": "Funciona"},
])
@pytest.mark.parametrize("fusionada", [False, True])
def test_un_fallo_lleva_a_revisiones_salvo_en_in_progress(valores, fusionada):
    assert flujo.estado_objetivo("In review", valores, fusionada, en_lote=False) == ("Revisiones", False)
    assert flujo.estado_objetivo("In progress", valores, fusionada, en_lote=False) == (None, False)


def test_in_progress_probada_que_funciona_se_queda_en_in_progress():
    assert flujo.estado_objetivo("In progress", {"Editor": "Funciona"}, fusionada=False, en_lote=False) == (None, False)


def test_tarea_solo_de_prueba_en_qa_editor_cierra_al_funcionar():
    assert flujo.estado_objetivo("QA editor", {"Editor": "Funciona"}, False, False, sin_pr=True) == ("Done", True)
    assert flujo.estado_objetivo("QA editor", {"Editor": "Sin probar"}, False, False, sin_pr=True) == (None, False)
    assert flujo.estado_objetivo("In review", {"Editor": "Funciona"}, False, False, sin_pr=True) == (None, False)


def test_falla_en_el_editor_en_in_progress_no_sale_de_in_progress():
    assert flujo.estado_tras_fallo_editor("In progress") is None
    for estado in ("In review", "QA editor", "Validada", "Done", None):
        assert flujo.estado_tras_fallo_editor(estado) == "Revisiones"


def test_mandar_a_revision_sin_probar_lo_dice():
    assert flujo.preparar_revision({}) == ("Sin probar", flujo.AVISO_SIN_QA)
    assert flujo.preparar_revision({"Editor": "Sin probar"}) == (None, flujo.AVISO_SIN_QA)
    assert flujo.preparar_revision({"Editor": "Funciona"}) == (None, None)


def test_no_se_manda_a_revision_algo_que_falla():
    with pytest.raises(flujo.EnvioRechazado):
        flujo.preparar_revision({"Editor": "Falla"})


def test_editor_tras_fusion_conserva_funciona():
    assert flujo.editor_tras_fusion({"Editor": "Funciona"}) is None
    assert flujo.editor_tras_fusion({"Editor": "Falla"}) == "Sin probar"
    assert flujo.editor_tras_fusion({}) == "Sin probar"


@pytest.mark.parametrize("estado", ["In progress", "Revisiones", "Done"])
def test_fusion_antigua_no_arrastra_trabajo_en_marcha(estado):
    assert not flujo.mueve_por_fusion(estado, con_pr_abierta=False)


@pytest.mark.parametrize("estado", ["In review", "QA editor", "Validada", None])
def test_fusion_mueve_lo_que_esperaba_la_fusion(estado):
    assert flujo.mueve_por_fusion(estado, con_pr_abierta=False)
    assert not flujo.mueve_por_fusion(estado, con_pr_abierta=True)


def test_done_antes_de_fusionar_se_cierra_solo_con_las_dos_validaciones():
    """#436: Done antes de fusionar (#282) se cierra al fusionarse, si está revisada y probada."""
    lista = {"Revisión IA": "Aprobada", "Editor": "Funciona"}
    assert flujo.cierra_por_fusion("Done", lista, con_pr_abierta=False)
    assert not flujo.cierra_por_fusion("Done", lista, con_pr_abierta=True)
    assert not flujo.cierra_por_fusion("Done", {**lista, "Editor": "Sin probar"}, con_pr_abierta=False)
    assert not flujo.cierra_por_fusion("In progress", lista, con_pr_abierta=False)


def test_esta_fusionada_solo_en_dev_y_sin_pr_abierta():
    fusionadas = [{"body": "Closes #7 Closes #8", "headRefName": "fix/7-x", "baseRefName": "dev"},
                  {"body": "Closes #9", "headRefName": "feat/9-x", "baseRefName": "main"}]
    assert base.esta_fusionada(7, fusionadas, [])
    assert base.esta_fusionada(8, fusionadas, [])       # PR con varias issues
    assert not base.esta_fusionada(9, fusionadas, [])   # fusionada en otra rama
    assert not base.esta_fusionada(10, fusionadas, [])
    assert not base.esta_fusionada(7, fusionadas, [{"body": "", "headRefName": "fix/7-otra"}])


# --- Dependencias ----------------------------------------------------------------------------------

def _bloqueada(*deps, estado="Bloqueada"):
    return {"valores": {"Status": estado}, "blockedBy": {"nodes": [{"number": n, "state": s} for n, s in deps]}}


def test_coger_rechaza_y_nombra_lo_que_falta():
    motivo = bloqueos.motivo_para_no_coger(20, _bloqueada((12, "OPEN"), (13, "CLOSED"), (15, "OPEN")))
    assert "#12, #15" in motivo and "#13" not in motivo


def test_coger_rechaza_bloqueada_sin_dependencias():
    assert "sin dependencias" in bloqueos.motivo_para_no_coger(20, _bloqueada())


def test_coger_permite_sin_bloqueantes_abiertas():
    assert bloqueos.motivo_para_no_coger(20, _bloqueada((12, "CLOSED"), estado="Ready")) is None
    assert bloqueos.motivo_para_no_coger(20, {"valores": {"Status": "Ready"}}) is None


def test_desbloquea_solo_con_todas_cerradas():
    assert bloqueos.desbloquea(_bloqueada((12, "CLOSED"), (13, "CLOSED")))
    assert not bloqueos.desbloquea(_bloqueada((12, "CLOSED"), (13, "OPEN")))
    assert not bloqueos.desbloquea(_bloqueada())
    assert not bloqueos.desbloquea(_bloqueada((12, "CLOSED"), estado="Ready"))


def test_nuevas_dependencias_sin_repetir_ni_autodependencia():
    assert bloqueos.nuevas_dependencias(20, [12, 13, 12], [{"number": 13}]) == [12]
    with pytest.raises(ValueError):
        bloqueos.nuevas_dependencias(20, [20], [])


# --- Opciones de Status ----------------------------------------------------------------------------

ACTUALES = [
    {"id": "a", "name": "Backlog", "color": "GRAY", "description": "Por concretar"},
    {"id": "b", "name": "Ready", "color": "BLUE", "description": "Lista"},
    {"id": "c", "name": "QA editor", "color": "PURPLE", "description": "Fusionada en macro-update"},
    {"id": "d", "name": "Done", "color": "GREEN", "description": "Cerrada"},
]


def test_opciones_objetivo_inserta_conserva_ids_y_corrige_descripciones():
    nuevas = estados.opciones_objetivo(ACTUALES, estados.NUEVAS, estados.DESCRIPCIONES)
    assert [o["name"] for o in nuevas] == ["Backlog", "Bloqueada", "Ready", "QA editor", "Validada", "Done"]
    assert "lote" in nuevas[4]["description"]
    assert [o.get("id") for o in nuevas] == ["a", None, "b", "c", None, "d"]
    assert "macro-update" not in " ".join(o["description"] for o in nuevas)


def test_opciones_objetivo_idempotente():
    nuevas = estados.opciones_objetivo(ACTUALES, estados.NUEVAS, estados.DESCRIPCIONES)
    assert estados.opciones_objetivo(nuevas, estados.NUEVAS, estados.DESCRIPCIONES) is None


def test_opciones_objetivo_falla_si_falta_la_referencia():
    with pytest.raises(ValueError):
        estados.opciones_objetivo(ACTUALES[:1], estados.NUEVAS, {})


def test_restaurar_solo_lo_que_cambio_o_se_vacio():
    foto = {"i1": "Ready", "i2": "Done", "i3": None}
    assert estados.pendientes_de_restaurar(foto, {"i1": "Ready", "i2": None, "i3": None}) == {"i2": "Done"}
    assert estados.pendientes_de_restaurar(foto, foto) == {}


def test_bloquear_en_backlog_no_aprueba():
    assert bloqueos.estado_tras_bloquear("Backlog") is None
    for estado in ("Ready", "In progress", "Bloqueada", None):
        assert bloqueos.estado_tras_bloquear(estado) == "Bloqueada"


def test_aprobar_con_bloqueantes_abiertas_deja_en_bloqueada():
    con_espera = _bloqueada((12, "OPEN"), estado="Backlog")
    assert bloqueos.estado_al_aprobar("Ready", con_espera) == "Bloqueada"
    assert bloqueos.estado_al_aprobar("Ready", _bloqueada((12, "CLOSED"), estado="Backlog")) == "Ready"
    assert bloqueos.estado_al_aprobar("Backlog", con_espera) == "Backlog"
    assert bloqueos.estado_al_aprobar("Ready", {}) == "Ready"


@pytest.mark.parametrize("firma, login", [("Mokius (Claude)", "Mokius"), ("SkiTemplar", "SkiTemplar"),
                                          ("Codex", None), ("IA revisora", None), ("", None), (None, None)])
def test_revisor_del_equipo_sale_de_la_firma(firma, login):
    assert flujo.revisor_del_equipo(firma, {"SkiTemplar": {}, "Mokius": {}}) == login


def test_firma_de_puente_solo_en_comandos_lanzados_a_mano():
    puente = {"GITHUB_ACTIONS": "true", "GITHUB_EVENT_NAME": "workflow_dispatch", "GITHUB_TRIGGERING_ACTOR": "Mokius"}
    assert flujo.firma_de_puente(puente) == "\n\n_Lanzado por Mokius a través del puente._"
    assert flujo.firma_de_puente({**puente, "GITHUB_EVENT_NAME": "schedule"}) == ""
    assert flujo.firma_de_puente({}) == ""
    assert flujo.firma_de_puente({"GITHUB_ACTIONS": "true", "GITHUB_EVENT_NAME": "workflow_dispatch"}) == ""


def test_sin_firma_recupera_el_texto_del_tablero():
    texto = "**Revisión de organización** (`tablero.py auditar`)\n- sin Fase"
    firmado = texto + flujo.firma_de_puente({"GITHUB_ACTIONS": "true", "GITHUB_EVENT_NAME": "workflow_dispatch",
                                             "GITHUB_ACTOR": "SkiTemplar"})
    assert firmado != texto and flujo.sin_firma(firmado) == texto == flujo.sin_firma(texto)


# --- Peticiones ------------------------------------------------------------------------------------

def test_peticion_lleva_fecha_y_quien_y_no_puede_ir_vacia():
    from datetime import date
    import peticiones
    assert peticiones.texto_peticion("  Recarga a 4 s,  no 5. ", "SkiTemplar", date(2026, 9, 30)) == \
        "**Petición** (2026-09-30, SkiTemplar): Recarga a 4 s, no 5."
    assert peticiones.texto_atendida("Hecho en d1dbf5f.", "Ruben-Besteiro") == "**Petición atendida** (Ruben-Besteiro): Hecho en d1dbf5f."
    for malo in ("", "   ", None):
        with pytest.raises(ValueError):
            peticiones.texto_peticion(malo, "SkiTemplar", date(2026, 9, 30))
        with pytest.raises(ValueError):
            peticiones.texto_atendida(malo, "SkiTemplar")


@pytest.mark.parametrize("valores, campos", [
    ({"Status": "Validada", "Editor": "Funciona", "Revisión IA": "Aprobada"},
     {"Status": "Revisiones", "Revisión IA": "Cambios pedidos", "Editor": "Sin probar"}),
    ({"Status": "In review", "Editor": "Sin probar"}, {"Status": "Revisiones", "Revisión IA": "Cambios pedidos"}),
    ({"Status": "QA editor"}, {"Status": "Revisiones", "Revisión IA": "Cambios pedidos"}),
    ({"Status": "In progress", "Editor": "Funciona"}, {}),
    ({"Status": "Ready"}, {}), ({"Status": "Backlog"}, {}), ({"Status": "Revisiones"}, {}), ({}, {}),
])
def test_peticion_sobre_lo_entregado_vuelve_a_revisiones(valores, campos):
    import peticiones
    assert peticiones.campos_tras_peticion(valores) == campos


def test_peticiones_para_el_asignado_las_sin_dueno_y_todas_para_aprobadores():
    import peticiones

    def issue(numero, etiquetas, *quien):
        return {"number": numero, "labels": {"nodes": [{"name": e} for e in etiquetas]},
                "assignees": {"nodes": [{"login": q} for q in quien]}}

    issues = [issue(1, ["peticion"], "Ruben-Besteiro"), issue(2, ["peticion"], "Mokius"), issue(3, ["peticion"]),
              issue(4, ["tarea"], "Ruben-Besteiro")]
    assert [i["number"] for i in peticiones.para(issues, "Ruben-Besteiro", aprobador=False)] == [1, 3]
    assert [i["number"] for i in peticiones.para(issues, "SkiTemplar", aprobador=True)] == [1, 2, 3]


def test_item_desde_issue_recien_anadida_al_project():
    issue = {"number": 176, "title": "Token", "state": "OPEN", "assignees": {"nodes": [{"login": "Mokius"}]},
             "projectItems": {"nodes": [
                 {"id": "otro", "project": {"number": 1}, "fieldValues": {"nodes": []}},
                 {"id": "PVTI_x", "project": {"number": 2},
                  "fieldValues": {"nodes": [{}, {"name": "In progress", "field": {"name": "Status"}},
                                            {"name": "P0", "field": {"name": "Prioridad"}}]}}]}}
    item = base.item_desde_issue(issue, 2)
    assert item["item"] == "PVTI_x" and item["valores"] == {"Status": "In progress", "Prioridad": "P0"}
    assert item["number"] == 176 and "projectItems" not in item
    assert base.item_desde_issue(issue, 3) is None and base.item_desde_issue(None, 2) is None
