"""Volcado del tablero a Markdown y puente para lanzar comandos desde GitHub Actions.

La rutina diaria de Claude en la nube solo llega a las rutas REST del repositorio: no
puede leer ni mover el Project, que es de la organización. El workflow
`.github/workflows/tablero-puente.yml` sí puede. Publica este volcado en una issue del
repo, que la rutina lee, y ejecuta por ella los comandos de `PERMITIDOS`.

El puente comenta y mueve issues con el token de una persona, y GitHub la suscribe a cada issue que toca: `silenciar`
la da de baja de las issues abiertas para que solo le lleguen las PR, las menciones y lo que tenga asignado.

Funciones puras; hablar con GitHub es cosa de tablero.py.
"""

from __future__ import annotations

import shlex
from datetime import datetime, timedelta

import flujo
import lotes
import objetos
from base import ESTADOS, ErrorTablero

MAX_TITULO = 70
# Una issue de GitHub admite 65 536 caracteres de cuerpo.
MAX_CUERPO = 60000
# Subcomandos que el workflow acepta por `workflow_dispatch`: los que mantienen el tablero.
# Quedan fuera los que crean ramas o dependen del usuario que los lanza (`coger`, `revision`). `chamber` solo lo
# lanza un aprobador: lo comprueban `argumentos_de_puente` y, otra vez, `control.cmd_chamber`.
PERMITIDOS = ("estado", "campo", "sync", "auditar", "colisiones", "bloquear", "colgar", "resumen", "decidir",
              "editor", "ia", "volcado", "pedir", "atendida", "chamber")
CABECERA = ("| # | Título | Asignados | Prio. | Tam. | Área | Fase | Revisión IA | Editor | Revisor | PR | Etiquetas "
            "| Espera a |")
SEPARADOR = "|" + "---|" * 13
# Estados en los que una issue espera a alguien: si no se mueve, está atascada.
ESTADOS_VIVOS = ("In progress", "In review", "Revisiones", "QA editor", "Validada")


def argumentos_de_puente(texto: str, actor: str | None = None, aprobadores=()) -> list[str]:
    """Trocea el comando recibido por el workflow; lo rechaza si su subcomando no está permitido.

    `actor` es quien disparó el puente (None si no se sabe: lo vuelve a comprobar el comando). Un `chamber` lanzado
    por alguien que no está en `aprobadores` se rechaza aquí, antes de tocar nada.
    """
    try:
        partes = shlex.split(texto or "")
    except ValueError as exc:
        raise ErrorTablero(f"Comando mal formado: {exc}") from exc
    if not partes:
        raise ErrorTablero("Comando vacío.")
    if partes[0] not in PERMITIDOS:
        raise ErrorTablero(f"«{partes[0]}» no se puede lanzar por el puente. Permitidos: {', '.join(PERMITIDOS)}")
    if "--retomar" in partes:  # el puente comenta con el token de otro: recuperar una descartada se hace en local
        raise ErrorTablero("«--retomar» no se puede lanzar por el puente: lo hace un aprobador en local.")
    if partes[0] == "chamber" and actor is not None and (motivo := flujo.motivo_para_no_descartar(actor, aprobadores)):
        raise ErrorTablero(motivo)
    return partes


def _nombres(issue: dict, clave: str, campo: str) -> list[str]:
    return [n[campo] for n in (issue.get(clave) or {}).get("nodes", [])]


def _celda(texto: str) -> str:
    return str(texto).replace("|", "\\|").replace("\n", " ").strip() or "—"


def fila(issue: dict, prs: list[int]) -> str:
    v = issue.get("valores", {})
    titulo = issue.get("title", "")
    if len(titulo) > MAX_TITULO:
        titulo = titulo[:MAX_TITULO - 1] + "…"
    espera = [f"#{b['number']}" for b in (issue.get("blockedBy") or {}).get("nodes", []) if b.get("state") == "OPEN"]
    celdas = (f"#{issue['number']}", titulo, ", ".join(_nombres(issue, "assignees", "login")), v.get("Prioridad", ""),
              v.get("Tamaño", ""), v.get("Área", ""), v.get("Fase", ""), v.get("Revisión IA", ""), v.get("Editor", ""), v.get("Revisor", ""), ", ".join(f"#{n}" for n in prs),
              ", ".join(_nombres(issue, "labels", "name")), ", ".join(espera))
    return "| " + " | ".join(_celda(c) for c in celdas) + " |"


def _tabla(issues: list[dict], prs_por_issue: dict[int, list[int]]) -> list[str]:
    return [CABECERA, SEPARADOR, *(fila(i, prs_por_issue.get(i["number"], [])) for i in issues)]


def atascadas(issues: list[dict], ahora: datetime, dias: int) -> list[tuple[dict, int]]:
    """Issues abiertas que esperan a alguien y llevan más de `dias` sin movimiento, con los días que llevan."""
    lista = []
    for issue in issues:
        actualizada = issue.get("updatedAt")
        if issue.get("state") != "OPEN" or issue.get("valores", {}).get("Status") not in ESTADOS_VIVOS or not actualizada:
            continue
        parada = ahora - datetime.fromisoformat(actualizada.replace("Z", "+00:00"))
        if parada > timedelta(days=dias):
            lista.append((issue, parada.days))
    return sorted(lista, key=lambda par: (-par[1], par[0]["number"]))


def render(items: dict[int, dict], prs_por_issue: dict[int, list[int]], ahora: datetime, dias_atasco: int = 3) -> str:
    """Tablero completo en Markdown: las issues abiertas por estado, lo atascado y lo incoherente aparte, y los objetos.

    Las descartadas (`chamber`) no salen en ninguna sección.
    """
    issues = sorted(flujo.sin_chamber(items).values(), key=lambda i: i["number"])
    trabajo = [i for i in issues if not objetos.es_objeto(i) and not lotes.es_lote(i)]
    abiertas = [i for i in trabajo if i.get("state") == "OPEN"]
    lineas = [f"Volcado automático del tablero · {ahora:%Y-%m-%d %H:%M} UTC · {len(abiertas)} issues abiertas.",
              "No edites esta issue: el workflow «Puente del tablero» sustituye su contenido en cada ejecución.", ""]
    for estado in (*ESTADOS, None):
        grupo = [i for i in abiertas if i.get("valores", {}).get("Status") == estado]
        if estado == "Done" or not grupo:
            continue
        lineas += [f"## {estado or 'Sin estado'} ({len(grupo)})", "", *_tabla(grupo, prs_por_issue), ""]
    if paradas := atascadas(trabajo, ahora, dias_atasco):
        lineas += [f"## Sin movimiento desde hace más de {dias_atasco} días ({len(paradas)})", "",
                   "| # | Status | Días | Asignados | Título |", "|---|---|---|---|---|"]
        lineas += [f"| #{i['number']} | {i['valores']['Status']} | {dias} | "
                   f"{_celda(', '.join(_nombres(i, 'assignees', 'login')))} | {_celda(i.get('title', '')[:MAX_TITULO])} |"
                   for i, dias in paradas]
        lineas.append("")
    incoherentes = [i for i in trabajo
                    if (i.get("state") == "OPEN") == (i.get("valores", {}).get("Status") == "Done")]
    if incoherentes:
        lineas += [f"## Estado incoherente ({len(incoherentes)})", "",
                   "Abiertas en Done, o cerradas fuera de Done.", "",
                   "| # | Issue | Status | Título |", "|---|---|---|---|"]
        lineas += [f"| #{i['number']} | {'abierta' if i.get('state') == 'OPEN' else 'cerrada'} | "
                   f"{_celda(i.get('valores', {}).get('Status', ''))} | {_celda(i.get('title', '')[:MAX_TITULO])} |"
                   for i in incoherentes]
        lineas.append("")
    especiales = [i for i in issues if i.get("state") == "OPEN" and (objetos.es_objeto(i) or lotes.es_lote(i))]
    if especiales:
        lineas += [f"## Objetos y lotes abiertos ({len(especiales)})", ""]
        lineas += [f"- #{i['number']} {i.get('title', '')}" for i in especiales]
    texto = "\n".join(lineas).rstrip() + "\n"
    if len(texto) > MAX_CUERPO:
        texto = texto[:MAX_CUERPO].rsplit("\n", 1)[0] + "\n\n… (recortado: no cabe en una issue)\n"
    return texto


CONSULTA_SUSCRIPCIONES = """
query($owner: String!, $repo: String!, $cursor: String) {
  repository(owner: $owner, name: $repo) {
    issues(first: 100, after: $cursor, states: [OPEN]) {
      pageInfo { hasNextPage endCursor }
      nodes { id number viewerSubscription }
    }
  }
}
"""
MUTACION_SILENCIAR = """
mutation($id: ID!) { updateSubscription(input: {subscribableId: $id, state: UNSUBSCRIBED}) { subscribable { id } } }
"""


def falta_scope_de_notificaciones(error: str) -> bool:
    """True si gh rechazó la consulta porque al token le falta el scope `notifications`."""
    return "notifications" in error and "scopes" in error


def a_silenciar(nodos: list[dict]) -> list[dict]:
    """Issues abiertas a las que el dueño del token está suscrito (las ignoradas y las ya silenciadas no se tocan)."""
    return [n for n in nodos if n.get("viewerSubscription") == "SUBSCRIBED"]
