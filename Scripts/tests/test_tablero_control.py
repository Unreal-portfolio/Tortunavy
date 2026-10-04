"""Tests de memoria (memoria.py), auditoría (auditoria.py), lotes (lotes.py) y colisiones (colisiones.py)."""

import sys
from datetime import UTC, date, datetime, timedelta
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tablero"))
import auditoria  # noqa: E402
import colisiones  # noqa: E402
import lotes  # noqa: E402
import memoria  # noqa: E402

AHORA = datetime(2026, 10, 20, 12, tzinfo=UTC)


# --- Resumen y Decisión ----------------------------------------------------------------------------

def test_resumen_con_que_por_que_y_como():
    texto = memoria.texto_resumen("El puente\nno oscilaba.", "Excitation a uint8.", 118, por_que="Se truncaba a 0.")
    assert texto.splitlines() == ["**Resumen**", "- Qué fallaba: El puente no oscilaba.", "- Por qué: Se truncaba a 0.",
                                  "- Cómo se arregló: Excitation a uint8. (PR #118)"]
    assert memoria.es_resumen(texto)


def test_resumen_sin_causa_conocida_son_dos_lineas():
    assert len(memoria.texto_resumen("a", "b").splitlines()) == 3


@pytest.mark.parametrize("que, como, por_que", [("x" * 401, "bien", None), ("bien", "x" * 401, None),
                                                ("bien", "bien", "x" * 401), ("", "bien", None), ("bien", "  \n ", None)])
def test_resumen_rechaza_parrafadas_y_vacios(que, como, por_que):
    with pytest.raises(memoria.ErrorMemoria):
        memoria.texto_resumen(que, como, por_que=por_que)


def test_resumen_admite_el_limite_exacto():
    assert memoria.texto_resumen("x" * 400, "y" * 400, por_que="z" * 400)


def test_decision_con_fecha_y_quien():
    texto = memoria.texto_decision("La rama se llama dev.", "SkiTemplar", date(2026, 9, 29))
    assert texto == "**Decisión** (2026-09-29, SkiTemplar): La rama se llama dev."
    with pytest.raises(memoria.ErrorMemoria):
        memoria.texto_decision("x" * 401, "SkiTemplar", date(2026, 9, 29))


# --- Auditoría -------------------------------------------------------------------------------------

def _issue(status="Ready", **extra):
    valores = {"Status": status, "Prioridad": "P1", "Área": "Red", "Tamaño": "S", "Fase": "F4",
               **extra.pop("valores", {})}
    base = {"numero": 5, "titulo": "Tarea", "estado": "OPEN", "motivo_cierre": None, "cerrada": None,
            "cuerpo": "Qué hay que hacer.\n\n- [ ] Hecho.", "etiquetas": {"tarea"}, "asignados": [] if status in auditoria.ESTADOS_SIN_DUENO else ["Mokius"], "padre": 40, "bloqueantes": [], "lotes": [],
            "comentarios": [], "valores": valores, "con_pr": False, "fusionada": False, "lote_fusionado": None,
            "prs_sin_lote": [], "revisor_sugerido": "SkiTemplar"}
    return {**base, **extra}


def _textos(issue):
    return [p["texto"] for p in auditoria.problemas(issue, AHORA)]


def test_issue_en_orden_no_tiene_problemas():
    assert auditoria.problemas(_issue(), AHORA) == []


def test_sin_padre_ni_campos():
    vacios = {"Prioridad": None, "Área": None, "Tamaño": None, "Fase": None}
    lista = auditoria.problemas(_issue(padre=None, valores=vacios), AHORA)
    assert len(lista) == 5 and lista[0]["texto"].startswith("no cuelga")
    assert "sin Fase" in [p["texto"] for p in lista]
    assert {p["tipo"] for p in lista} == {"organizacion"}


@pytest.mark.parametrize("extra, esperado", [
    ({"etiquetas": set()}, "sin etiqueta de tipo"),
    ({"titulo": "x" * 81}, "título de 81 caracteres"),
    ({"cuerpo": "  \n"}, "sin cuerpo"),
    ({"cuerpo": "Hay que hacerlo."}, "sin criterios de aceptación"),
])
def test_formato_de_la_issue(extra, esperado):
    assert any(t.startswith(esperado) for t in _textos(_issue(**extra)))


def test_formato_correcto_con_titulo_en_el_limite_y_casilla_marcada():
    assert auditoria.problemas_de_formato("x" * 80, "Hecho:\n- [x] Compila.", {"⚠️bug⚠️"}) == []
    assert auditoria.problemas(_issue("Revisiones", etiquetas={"colision"}, cuerpo=""), AHORA) == []


def test_p0_en_backlog_se_avisa_y_en_ready_no():
    assert any(t.startswith("P0 en Backlog") for t in _textos(_issue("Backlog", valores={"Prioridad": "P0"})))
    assert auditoria.problemas(_issue("Ready", valores={"Prioridad": "P0"}), AHORA) == []
    assert auditoria.problemas(_issue("Backlog"), AHORA) == []


def test_ready_con_bloqueantes_abiertas_pasa_a_bloqueada():
    espera = [{"number": 99, "state": "OPEN"}, {"number": 98, "state": "CLOSED"}]
    lista = auditoria.problemas(_issue("Ready", bloqueantes=espera), AHORA)
    assert [(p["tipo"], p["campo"], p["valor"]) for p in lista] == [("trivial", "Status", "Bloqueada")]
    assert "#99" in lista[0]["texto"] and "#98" not in lista[0]["texto"]
    assert auditoria.problemas(_issue("Ready", bloqueantes=[{"number": 98, "state": "CLOSED"}]), AHORA) == []
    assert auditoria.problemas(_issue("Backlog", bloqueantes=espera), AHORA) == []


@pytest.mark.parametrize("issue, esperado", [
    (_issue("In progress", asignados=[]), "In progress sin asignado"),
    (_issue("In review", valores={"Revisor": "Mokius"}), "In review sin PR enlazada"),
    (_issue("In review", con_pr=True, revisor_sugerido=None), "In review sin Revisor ni asignado"),
    (_issue("Revisiones", comentarios=["Lista para revisión."]), "en Revisiones sin un comentario"),
    (_issue("Revisiones", comentarios=["**Editor: falla** (PIE).\n\nSin detalle."]), "en Revisiones sin un comentario"),
    (_issue("Bloqueada"), "en Bloqueada sin dependencias"),
    (_issue("In review", valores={"Revisor": "Mokius"}, con_pr=True, prs_sin_lote=[120]), "su PR #120 cierra varias"),
])
def test_reglas_de_organizacion(issue, esperado):
    assert any(t.startswith(esperado) for t in _textos(issue))


@pytest.mark.parametrize("comentario", [
    "**Editor: falla** (PIE 4P).\n\nEl cliente no ve la catapulta.",
    "**Revisión IA (Mokius (Claude)): Cambios pedidos.**\n\nFalta DOREPLIFETIME.",
    "Al saltar dos veces se cae del mapa.",
])
def test_revisiones_con_fallo_explicado_esta_en_orden(comentario):
    assert auditoria.problemas(_issue("Revisiones", comentarios=[comentario]), AHORA) == []


def test_colision_en_revisiones_no_necesita_comentario():
    assert auditoria.problemas(_issue("Revisiones", etiquetas={"colision"}), AHORA) == []


def test_trivial_mueve_la_tarjeta_y_rellena_el_campo_evidente():
    issue = _issue("In review", con_pr=True, fusionada=True, valores={"Revisión IA": "Aprobada"})
    lista = auditoria.problemas(issue, AHORA)
    assert [(p["tipo"], p["campo"], p["valor"]) for p in lista] == [("trivial", "Status", "QA editor"),
                                                                    ("trivial", "Revisor", "SkiTemplar")]
    accion = auditoria.acciones(issue, lista)
    assert accion["campos"] == {"Status": "QA editor", "Revisor": "SkiTemplar"}
    assert accion["comentarios"][0].startswith(f"{auditoria.CABECERA}: corregido")
    assert not accion["etiquetar"] and not accion["reabrir"]


def test_validada_suelta_aprobada_y_probada_sin_fusionar_no_se_mueve():
    """#282: una issue suelta también espera en Validada a la fusión."""
    issue = _issue("Validada", con_pr=True, valores={"Revisor": "Mokius", "Revisión IA": "Aprobada", "Editor": "Funciona"})
    assert auditoria.columna_correcta(issue) is None
    assert auditoria.problemas(issue, AHORA) == []


@pytest.mark.parametrize("lotes_, valores", [([], {"Revisión IA": "Pendiente", "Editor": "Funciona"}),
                                             ([131], {"Revisión IA": "Aprobada", "Editor": "Sin probar"})])
def test_validada_sin_las_dos_validaciones_vuelve_a_in_review(lotes_, valores):
    issue = _issue("Validada", con_pr=True, lotes=lotes_, valores={"Revisor": "Mokius", **valores})
    assert auditoria.columna_correcta(issue) == "In review"


def test_validada_con_cambios_pedidos_va_a_revisiones():
    issue = _issue("Validada", con_pr=True, valores={"Revisor": "Mokius", "Revisión IA": "Cambios pedidos"})
    assert auditoria.columna_correcta(issue) == "Revisiones"


def test_grave_lote_fusionado_sin_validar_va_a_revisiones_con_p0():
    issue = _issue("QA editor", con_pr=True, fusionada=True, lote_fusionado=130, lotes=[131],
                   valores={"Revisión IA": "Aprobada", "Editor": "Sin probar"})
    lista = auditoria.problemas(issue, AHORA)
    assert lista[0]["tipo"] == "grave" and "#130" in lista[0]["texto"]
    assert not [p for p in lista if p["tipo"] == "trivial"]  # lo grave manda: no se "arregla" moviendo la tarjeta
    accion = auditoria.acciones(issue, lista)
    assert accion["campos"] == {"Status": "Revisiones", "Prioridad": "P0"}
    assert "Revisiones con P0" in accion["comentarios"][0]


def test_grave_qa_editor_con_pr_sin_fusionar_y_tarea_de_prueba_sin_pr_en_orden():
    con_pr = _issue("QA editor", con_pr=True, valores={"Editor": "Sin probar"})
    assert auditoria.problemas(con_pr, AHORA)[0]["tipo"] == "grave"
    aprobada = _issue("QA editor", con_pr=True, valores={"Editor": "Sin probar", "Revisión IA": "Aprobada",
                                                         "Revisor": "Mokius"})
    assert auditoria.problemas(aprobada, AHORA) == []
    assert auditoria.problemas(_issue("QA editor", valores={"Editor": "Sin probar"}), AHORA) == []


def test_in_review_aprobada_sin_probar_se_mueve_a_qa_editor():
    issue = _issue("In review", con_pr=True, valores={"Revisor": "Mokius", "Revisión IA": "Aprobada",
                                                     "Editor": "Sin probar"})
    assert auditoria.columna_correcta(issue) == "QA editor"


def test_grave_ya_en_revisiones_no_se_repite():
    issue = _issue("Revisiones", lote_fusionado=130, comentarios=["Falla el choque."])
    assert auditoria.problemas(issue, AHORA) == []


def test_cerrada_completada_sin_probar_se_reabre_en_revisiones_con_p0():
    issue = _issue("Done", estado="CLOSED", motivo_cierre="COMPLETED", cerrada=AHORA - timedelta(days=2),
                   comentarios=[memoria.texto_resumen("a", "b")], valores={"Editor": "Sin probar"})
    lista = auditoria.problemas(issue, AHORA)
    assert [p["tipo"] for p in lista] == ["grave"]
    accion = auditoria.acciones(issue, lista)
    assert accion["reabrir"] and accion["campos"]["Prioridad"] == "P0"



def test_colision_cerrada_no_necesita_prueba_en_el_editor():
    """#175 (30-09): la colisión entre las PR #124 y #166 se cerró sin Editor y la auditoría la reabría."""
    issue = _issue("Done", estado="CLOSED", motivo_cierre="COMPLETED", cerrada=AHORA - timedelta(days=2),
                   etiquetas={"colision"}, comentarios=[memoria.texto_resumen("a", "b")], valores={})
    assert auditoria.problemas(issue, AHORA) == []

def test_cerrada_antes_del_inicio_del_sistema_no_se_audita():
    ahora = auditoria.INICIO_SISTEMA + timedelta(days=1)
    previa = _issue("Done", estado="CLOSED", motivo_cierre="COMPLETED", cerrada=auditoria.INICIO_SISTEMA - timedelta(hours=2))
    posterior = {**previa, "cerrada": auditoria.INICIO_SISTEMA + timedelta(hours=2)}
    assert auditoria.problemas(previa, ahora) == []
    assert [p["tipo"] for p in auditoria.problemas(posterior, ahora)] == ["grave", "organizacion"]


@pytest.mark.parametrize("estado", ["Backlog", "Ready", "Bloqueada"])
def test_asignado_fuera_de_curso_se_avisa(estado):
    extra = {"bloqueantes": [7]} if estado == "Bloqueada" else {}
    assert any("tiene asignado (Mokius)" in t for t in _textos(_issue(estado, asignados=["Mokius"], **extra)))
    assert not any("tiene asignado" in t for t in _textos(_issue(estado, **extra)))
    assert not any("tiene asignado" in t for t in _textos(_issue("In progress")))


def test_cerrada_reciente_sin_resumen_y_antigua_no_se_audita():
    reciente = _issue("Done", estado="CLOSED", motivo_cierre="NOT_PLANNED", cerrada=AHORA - timedelta(days=3))
    antigua = _issue("Done", estado="CLOSED", motivo_cierre="COMPLETED", cerrada=AHORA - timedelta(days=15))
    assert _textos(reciente)[0].startswith("cerrada sin comentario **Resumen**")
    assert auditoria.problemas(antigua, AHORA) == []
    con_resumen = {**reciente, "comentarios": [memoria.texto_resumen("a", "b")]}
    assert auditoria.problemas(con_resumen, AHORA) == []


def test_acciones_de_organizacion_idempotentes():
    lista = [auditoria.problema("sin Área")]
    nueva = _issue()
    accion = auditoria.acciones(nueva, lista)
    assert accion["etiquetar"] and accion["comentarios"] == [auditoria.texto_comentario(lista)] and not accion["campos"]
    ya = _issue(etiquetas={auditoria.ETIQUETA}, comentarios=[auditoria.texto_comentario(lista)])
    assert auditoria.acciones(ya, lista) == {"campos": {}, "reabrir": False, "comentarios": [],
                                             "etiquetar": False, "desetiquetar": False}
    assert auditoria.acciones(ya, [])["desetiquetar"] is True
    assert auditoria.acciones(nueva, [])["desetiquetar"] is False


def test_objetos_lotes_y_parte_diario_no_son_de_trabajo():
    assert not auditoria.es_de_trabajo(_issue(etiquetas={"objeto"}))
    assert not auditoria.es_de_trabajo(_issue(etiquetas={"lote"}))
    assert not auditoria.es_de_trabajo(_issue(titulo="Parte diario del tablero"))
    assert not auditoria.es_de_trabajo(_issue(titulo="Estado del tablero"))
    assert auditoria.es_de_trabajo(_issue())


# --- Lotes -----------------------------------------------------------------------------------------

def test_lote_necesita_dos_miembros_sin_repetir():
    assert lotes.comprobar_miembros([57, 58, 59]) == [57, 58, 59]
    for malos in ([57], [], [57, 57]):
        with pytest.raises(lotes.ErrorLote):
            lotes.comprobar_miembros(malos)


def test_titulo_y_cuerpo_del_lote():
    assert lotes.titulo("Bugs del Rally") == lotes.titulo("Lote: Bugs del Rally") == "Lote: Bugs del Rally"
    cuerpo = lotes.cuerpo([57, 58], 120)
    assert "- [ ] #57" in cuerpo and "#120" in cuerpo


def test_lotes_de_solo_lotes_abiertos():
    issue = {"blocking": {"nodes": [{"number": 130, "state": "OPEN", "labels": {"nodes": [{"name": "lote"}]}},
                                    {"number": 131, "state": "CLOSED", "labels": {"nodes": [{"name": "lote"}]}},
                                    {"number": 90, "state": "OPEN", "labels": {"nodes": [{"name": "tarea"}]}}]}}
    assert lotes.lotes_de(issue) == [130]
    assert lotes.lotes_de({}) == []


def test_pendientes_del_lote_bloquean_la_fusion():
    miembros = {57: ({"Status": "Validada"}, "OPEN"),
                58: ({"Status": "In review", "Revisión IA": "Aprobada", "Editor": "Sin probar"}, "OPEN"),
                59: ({"Status": "Revisiones", "Editor": "Falla"}, "OPEN"),
                60: ({"Status": "Done"}, "CLOSED")}
    pendientes = lotes.pendientes(miembros)
    assert list(pendientes) == [58, 59]
    assert pendientes[58] == ["falta probarla en el editor"]
    assert "tiene un fallo por arreglar" in pendientes[59]
    assert lotes.pendientes({57: ({"Status": "Validada"}, "OPEN")}) == {}


def test_miembro_con_decision_pendiente_bloquea_la_fusion_del_lote():
    assert lotes.con_decision({44: {"tarea", "decision"}, 45: {"⚠️bug⚠️"}, 46: set()}, "decision") == [44]
    assert lotes.con_decision({45: {"⚠️bug⚠️"}}, "decision") == []


def test_pr_con_varias_issues_necesita_lote():
    assert lotes.pr_necesita_lote({57, 58}, con_lote=False)
    assert not lotes.pr_necesita_lote({57, 58}, con_lote=True)
    assert not lotes.pr_necesita_lote({57}, con_lote=False)


# --- Colisiones ------------------------------------------------------------------------------------

def test_pares_con_ficheros_en_comun():
    ficheros = {12: {"a.cpp", "b.h"}, 9: {"b.h", "c.cpp", "a.cpp"}, 15: {"z.py"}}
    assert colisiones.pares(ficheros) == [(9, 12, ["a.cpp", "b.h"])]


def test_sin_pares_si_no_comparten_nada():
    assert colisiones.pares({1: {"a"}, 2: {"b"}}) == []
    assert colisiones.pares({}) == []


def test_titulo_estable_para_no_duplicar():
    assert colisiones.titulo(12, 9) == colisiones.titulo(9, 12) == "Mezclar PR #9 y #12: ficheros en común"


def test_binarios_llevan_decision_y_no_se_mezclan():
    ficheros = ["Content/Maps/LVL_Demo01.umap", "Source/X.cpp"]
    assert colisiones.etiquetas(ficheros) == ["colision", "decision"]
    assert colisiones.etiquetas(["Source/X.cpp"]) == ["colision"]
    antigua, reciente = {"number": 9, "headRefName": "feat/9-a"}, {"number": 12, "headRefName": "fix/12-b"}
    assert "No mezclar" in colisiones.cuerpo(antigua, reciente, ficheros, "dev")
    texto = colisiones.cuerpo(antigua, reciente, ["Source/X.cpp"], "dev")
    assert "Rebasa la PR más reciente, #12" in texto and "feat/9-a" in texto


def test_aviso_de_organizacion_firmado_por_el_puente_no_se_repite():
    lista = [auditoria.problema("sin Fase")]
    firmado = auditoria.texto_comentario(lista) + "\n\n_Lanzado por Mokius a través del puente._"
    ya = _issue(etiquetas={"tarea", auditoria.ETIQUETA}, comentarios=[firmado])
    assert auditoria.acciones(ya, lista)["comentarios"] == []


# --- Conversación en la issue ----------------------------------------------------------------------

def _charla(*turnos, asignados=("Ruben-Besteiro",), etiquetas=("tarea",), estado="OPEN"):
    return _issue("In progress", asignados=list(asignados), etiquetas=set(etiquetas), estado=estado,
                  autores=[a for a, _ in turnos], comentarios=[c for _, c in turnos])


@pytest.mark.parametrize("turnos, pendiente", [
    ([("SkiTemplar", "Mejor 4 s que 5, que se hace eterno.")], True),
    ([("SkiTemplar", "Mejor 4 s."), ("Ruben-Besteiro", "Hecho, 4 s.")], False),
    ([("SkiTemplar", "Mejor 4 s."), ("Ruben-Besteiro", "**Editor: funciona** (PIE).")], True),
    ([("SkiTemplar", "**Decisión** (2026-09-30, SkiTemplar): recarga a 4 s.")], True),
    ([("SkiTemplar", "**Petición** (2026-09-30, SkiTemplar): súbelo a 4 s."),
      ("SkiTemplar", "**Petición atendida** (Ruben-Besteiro): hecho.")], False),
    ([("SkiTemplar", "**Revisión IA (Mokius (Claude)): Aprobada.**\n\nBien.")], False),
    ([("Ruben-Besteiro", "Lo dejo en 2,4 s.")], False),
    ([("SkiTemplar", "Sube a 4 s.\n\n_Lanzado por Ruben-Besteiro a través del puente._")], False),
    ([("SkiTemplar", "Sube a 4 s.\n\n_Lanzado por Mokius a través del puente._")], True),
    ([], False),
])
def test_conversacion_pendiente_si_el_ultimo_en_hablar_no_es_el_asignado(turnos, pendiente):
    assert auditoria.conversacion_pendiente(_charla(*turnos)) is pendiente


def test_sin_asignado_solo_cuenta_una_peticion_expresa():
    suelto = _charla(("Mokius", "Con un solo jugador va bien."), asignados=())
    expresa = _charla(("Mokius", "**Petición** (2026-09-30, Mokius): reproducidlo con Rodrigo de anfitrión."), asignados=())
    assert not auditoria.conversacion_pendiente(suelto) and auditoria.conversacion_pendiente(expresa)


def test_la_etiqueta_peticion_se_pone_y_se_quita_sola():
    pide = ("SkiTemplar", "Mejor 4 s.")
    assert auditoria.accion_peticion(_charla(pide)) == "poner"
    assert auditoria.accion_peticion(_charla(pide, etiquetas=("tarea", "peticion"))) is None
    contestada = _charla(pide, ("Ruben-Besteiro", "Hecho."), etiquetas=("tarea", "peticion"))
    assert auditoria.accion_peticion(contestada) == "quitar"
    assert auditoria.accion_peticion(_charla(pide, ("Ruben-Besteiro", "Hecho."))) is None
    assert auditoria.accion_peticion(_charla(pide, estado="CLOSED")) is None


def test_conversacion_de_una_issue_leida_del_repo():
    nodo = {"number": 44, "state": "OPEN", "labels": {"nodes": [{"name": "tarea"}]},
            "assignees": {"nodes": [{"login": "Ruben-Besteiro"}]},
            "comments": {"nodes": [{"body": "La decisión actual es de 4 segundos.", "author": {"login": "SkiTemplar"}},
                                   {"body": "**Editor: funciona** (PIE).", "author": None}]}}
    assert auditoria.accion_peticion(auditoria.conversacion_de(nodo)) == "poner"


def test_las_notas_de_la_rutina_no_son_conversacion():
    nota = ("SkiTemplar", "**Rutina:** paso la tarjeta a Ready; estaba en In review sin PR.")
    assert not auditoria.conversacion_pendiente(_charla(nota))
    assert auditoria.conversacion_pendiente(_charla(nota, ("SkiTemplar", "Falta reproducirlo con 4 jugadores.")))


def test_solo_cuentan_los_pares_con_conflicto_real():
    ficheros = {9: {"a.cpp", "b.h"}, 12: {"a.cpp", "b.h"}, 15: {"a.cpp"}}
    conflicto = {(9, 12): ["b.h"], (9, 15): [], (12, 15): None}
    assert colisiones.pares(ficheros, lambda a, b: conflicto[(a, b)]) == [(9, 12, ["b.h"]), (12, 15, ["a.cpp"])]


def test_con_git_se_comprueban_tambien_los_pares_sin_ficheros_en_comun():
    # gh corta la lista en 100 ficheros: una PR grande puede chocar fuera de lo que se ve.
    ficheros = {240: {"a.cpp"}, 320: {"b.cpp"}, 330: {"c.cpp"}}
    conflicto = {(240, 320): ["Voz.cpp"], (240, 330): [], (320, 330): None}
    assert colisiones.pares(ficheros, lambda a, b: conflicto[(a, b)]) == [(240, 320, ["Voz.cpp"])]


def test_par_de_titulo():
    assert colisiones.par_de_titulo(colisiones.titulo(12, 9)) == (9, 12)
    assert colisiones.par_de_titulo("Otra cosa") is None


def test_resueltas_por_pr_cerrada_o_sin_conflicto():
    abiertas = [{"number": 100, "title": colisiones.titulo(9, 12)},
                {"number": 101, "title": colisiones.titulo(9, 15)},
                {"number": 102, "title": colisiones.titulo(12, 15)},
                {"number": 103, "title": "Mezclar a mano"}]
    resultado = colisiones.resueltas(abiertas, prs={9, 12, 15, 20}, vigentes={(9, 12)})
    assert [n for n, _ in resultado] == [101, 102]
    resultado = colisiones.resueltas(abiertas, prs={9, 15}, vigentes={(9, 15)})
    assert resultado[0] == (100, "la PR #12 ya no está abierta")
    assert [n for n, _ in resultado] == [100, 102]


def test_localizacion_se_regenera_en_vez_de_mezclar():
    antigua, reciente = {"number": 9, "headRefName": "feat/9-a"}, {"number": 12, "headRefName": "fix/12-b"}
    solo = ["Content/Localization/Game/Game.manifest", "Content/Localization/Game/es-ES/Game.po"]
    assert colisiones.solo_localizacion(solo)
    assert not colisiones.solo_localizacion(solo + ["Source/X.cpp"])
    assert not colisiones.solo_localizacion([])
    mixto = colisiones.cuerpo(antigua, reciente, solo + ["Source/X.cpp"], "dev")
    assert "Además, 2 ficheros de localización: se regeneran." in mixto and "Game.po" not in mixto
    assert "Además" not in colisiones.cuerpo(antigua, reciente, ["Source/X.cpp"], "dev")
