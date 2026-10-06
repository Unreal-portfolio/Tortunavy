"""Dependencias entre issues con las relaciones nativas de GitHub («blocked by»).

Una issue aprobada que espera a otras está en el estado Bloqueada y lleva la etiqueta `bloqueado`.
Cuando se cierran todas sus bloqueantes, `sync` la pasa a Ready y quita la etiqueta. En Backlog
(sin aprobar) la dependencia se registra y la issue se queda en Backlog: bloquear no aprueba.
Las funciones puras reciben la issue con `blockedBy` tal como la devuelve GraphQL.
"""

from __future__ import annotations

from collections.abc import Callable

Gh = Callable[..., str]

ESTADO = "Bloqueada"
ETIQUETA = "bloqueado"

MUTACION = """
mutation($issue: ID!, $bloqueante: ID!) {
  addBlockedBy(input: {issueId: $issue, blockingIssueId: $bloqueante}) { issue { number } }
}
"""


def bloqueantes(issue: dict) -> list[dict]:
    """Issues de las que depende, con número y estado (OPEN o CLOSED)."""
    return list((issue.get("blockedBy") or {}).get("nodes") or [])


def abiertas(issue: dict) -> list[int]:
    """Números de las bloqueantes que siguen abiertas."""
    return sorted(b["number"] for b in bloqueantes(issue) if b.get("state") == "OPEN")


def estado(issue: dict) -> str | None:
    return (issue.get("valores") or {}).get("Status")


def motivo_para_no_coger(numero: int, issue: dict) -> str | None:
    """Por qué no se puede coger la issue (None si se puede): nombra lo que falta."""
    pendientes = abiertas(issue)
    if pendientes:
        lista = ", ".join(f"#{n}" for n in pendientes)
        return f"#{numero} está bloqueada: depende de {lista}, que siguen abiertas. Coge otra o ayuda a cerrarlas."
    if estado(issue) == ESTADO and not bloqueantes(issue):
        return (f"#{numero} está en Bloqueada sin dependencias registradas. Registra de qué depende con "
                f"`tablero.py bloquear {numero} --por <m>` o pide a un aprobador que la pase a Ready.")
    return None


def estado_tras_bloquear(actual: str | None) -> str | None:
    """Estado tras registrar una dependencia: Backlog se queda como está (None); el resto, Bloqueada."""
    return None if actual == "Backlog" else ESTADO


def estado_al_aprobar(pedido: str, issue: dict) -> str:
    """Estado real al mover una issue: Ready con bloqueantes abiertas es Bloqueada."""
    return ESTADO if pedido == "Ready" and abiertas(issue) else pedido


def desbloquea(issue: dict) -> bool:
    """True si está Bloqueada, tiene dependencias y ya están todas cerradas."""
    return estado(issue) == ESTADO and bool(bloqueantes(issue)) and not abiertas(issue)


def nuevas_dependencias(numero: int, pedidas: list[int], actuales: list[dict]) -> list[int]:
    """Bloqueantes que hay que registrar: sin repetir las que ya existen. Error si depende de sí misma."""
    if numero in pedidas:
        raise ValueError(f"#{numero} no puede depender de sí misma.")
    ya = {b["number"] for b in actuales}
    return [m for m in dict.fromkeys(pedidas) if m not in ya]
