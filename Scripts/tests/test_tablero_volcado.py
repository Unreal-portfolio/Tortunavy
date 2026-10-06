"""Tests de Scripts/tablero/volcado.py (sin red ni gh)."""

import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tablero"))
import volcado  # noqa: E402
from base import ErrorTablero  # noqa: E402

AHORA = datetime(2026, 9, 30, 5, 15, tzinfo=timezone.utc)


def _issue(numero, status, *, titulo="Tarea", abierta=True, etiquetas=(), asignados=(), espera=(), **valores):
    return {"number": numero, "title": titulo, "state": "OPEN" if abierta else "CLOSED",
            "valores": {**({"Status": status} if status else {}), **valores},
            "labels": {"nodes": [{"name": e} for e in etiquetas]},
            "assignees": {"nodes": [{"login": a} for a in asignados]},
            "blockedBy": {"nodes": [{"number": n, "state": s} for n, s in espera]}}


def _items(*issues):
    return {i["number"]: i for i in issues}


def test_fila_lleva_campos_pr_y_dependencias_abiertas():
    issue = _issue(44, "In review", titulo="Tanques", etiquetas=["⚠️bug⚠️"], asignados=["Ruben-Besteiro"],
                   espera=[(40, "OPEN"), (41, "CLOSED")], Prioridad="P1", Editor="Funciona", Revisor="Mokius",
                   **{"Tamaño": "S", "Área": "Modos"})
    fila = volcado.fila(issue, [124])
    assert fila == "| #44 | Tanques | Ruben-Besteiro | P1 | S | Modos | — | — | Funciona | Mokius | #124 | ⚠️bug⚠️ | #40 |"
    assert fila.count("|") == volcado.CABECERA.count("|") == volcado.SEPARADOR.count("|")


def test_fila_recorta_el_titulo_y_escapa_las_barras():
    fila = volcado.fila(_issue(1, "Ready", titulo="a|b " + "x" * 100), [])
    assert "a\\|b" in fila and "…" in fila
    assert fila.count(" | ") == 12, "una barra del título no puede abrir una columna más"


def test_render_agrupa_por_estado_y_deja_fuera_done_cerradas():
    texto = volcado.render(_items(_issue(1, "Ready"), _issue(2, "In review"), _issue(3, "In review"),
                                  _issue(4, "Done", abierta=False)), {2: [9]}, AHORA)
    assert "2026-09-30 05:15 UTC · 3 issues abiertas" in texto
    assert texto.index("## Ready (1)") < texto.index("## In review (2)")
    assert "| #2 | Tarea | — | — | — | — | — | — | — | — | #9 |" in texto
    assert "#4" not in texto and "## Done" not in texto and "Estado incoherente" not in texto


def test_render_separa_lo_incoherente_los_sin_estado_y_los_objetos():
    texto = volcado.render(_items(_issue(5, "Done"), _issue(6, "In review", abierta=False), _issue(7, None),
                                  _issue(8, None, titulo="Rally", etiquetas=["objeto"]),
                                  _issue(9, "In progress", titulo="Lote: bugs", etiquetas=["lote"])), {}, AHORA)
    assert "## Sin estado (1)" in texto
    incoherente = texto.split("## Estado incoherente (2)")[1].split("##")[0]
    assert "| #5 | abierta | Done |" in incoherente and "| #6 | cerrada | In review |" in incoherente
    assert "## Objetos y lotes abiertos (2)" in texto and "- #8 Rally" in texto and "- #9 Lote: bugs" in texto
    assert "## In progress" not in texto, "el lote no cuenta como issue de trabajo"


def test_render_recorta_lo_que_no_cabe_en_una_issue():
    items = _items(*(_issue(n, "Backlog", titulo="t" * 70, etiquetas=["tarea"] * 5) for n in range(1, 700)))
    texto = volcado.render(items, {}, AHORA)
    assert len(texto) <= volcado.MAX_CUERPO + 60 and texto.rstrip().endswith("(recortado: no cabe en una issue)")


def test_puente_trocea_el_comando_permitido():
    assert volcado.argumentos_de_puente('estado 123 "In review"') == ["estado", "123", "In review"]
    assert volcado.argumentos_de_puente("sync --aplicar") == ["sync", "--aplicar"]


@pytest.mark.parametrize("comando", ["coger 5", "revision 5", "nueva --titulo x", "", "   ", 'estado 5 "sin cerrar',
                                     "; rm -rf /"])
def test_puente_rechaza_lo_que_no_esta_en_la_lista(comando):
    with pytest.raises(ErrorTablero):
        volcado.argumentos_de_puente(comando)


def test_silenciar_solo_toca_las_suscritas():
    nodos = [{"id": "a", "number": 1, "viewerSubscription": "SUBSCRIBED"},
             {"id": "b", "number": 2, "viewerSubscription": "UNSUBSCRIBED"},
             {"id": "c", "number": 3, "viewerSubscription": "IGNORED"}, {"id": "d", "number": 4}]
    assert [n["id"] for n in volcado.a_silenciar(nodos)] == ["a"]


def test_silenciar_distingue_la_falta_del_scope_de_otros_errores():
    sin_scope = ("gh api graphql -f…: gh: Your token has not been granted the required scopes to execute this query. "
                 "The 'updateSubscription' field requires one of the following scopes: ['notifications']")
    assert volcado.falta_scope_de_notificaciones(sin_scope)
    assert not volcado.falta_scope_de_notificaciones("gh api graphql -f…: HTTP 502")


def test_atascadas_solo_las_que_esperan_a_alguien_y_llevan_dias_paradas():
    def parada(numero, status, dias, **extra):
        fecha = (AHORA - timedelta(days=dias, hours=1)).isoformat().replace("+00:00", "Z")
        return {**_issue(numero, status, **extra), "updatedAt": fecha}

    issues = [parada(1, "In review", 5), parada(2, "Revisiones", 9), parada(3, "In review", 1), parada(4, "Ready", 30),
              parada(5, "Validada", 4, abierta=False), _issue(6, "In progress")]
    assert [(i["number"], dias) for i, dias in volcado.atascadas(issues, AHORA, 3)] == [(2, 9), (1, 5)]
    texto = volcado.render(_items(*issues), {}, AHORA, 3)
    assert "## Sin movimiento desde hace más de 3 días (2)" in texto and "| #2 | Revisiones | 9 |" in texto
    assert "Sin movimiento" not in volcado.render(_items(parada(3, "In review", 1)), {}, AHORA, 3)


def test_el_puente_admite_pedir_y_atendida():
    assert volcado.argumentos_de_puente('pedir 44 --texto "Recarga a 4 s"') == ["pedir", "44", "--texto", "Recarga a 4 s"]
    assert volcado.argumentos_de_puente('atendida 44 --nota "Hecho"')[0] == "atendida"
