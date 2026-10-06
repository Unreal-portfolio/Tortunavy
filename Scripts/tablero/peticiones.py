"""Peticiones: pedir o comentar un cambio hablando en la propia issue.

Se habla en los comentarios de la issue, como en un chat. `auditar` (cada mañana, por el puente) mira
quién habló el último: si no fue quien la tiene asignada, le pone la etiqueta `peticion`, y `pendiente`
se la enseña antes que su trabajo. Su Claude lee la conversación, hace lo que toque, corrige los
criterios del cuerpo y contesta; al contestar, la etiqueta se quita sola (o al momento con
`tablero.py atendida`).

`tablero.py pedir` es lo mismo sin esperar a la mañana, y además devuelve a Revisiones lo que ya
estaba entregado (In review, QA editor o Validada): lo revisado y probado ya no es lo que se pide.
"""

from __future__ import annotations

import argparse
import re
from datetime import date

import objetos
from base import (REPO, ErrorTablero, cargar_issue, comentar, es_de, gh, poner_campo, rechazar_descartada,
                  usuario_actual)

ETIQUETA = "peticion"
COLOR = "D93F0B"
DESCRIPCION_ETIQUETA = "Conversación sin contestar por quien tiene la issue: léela en sus comentarios"
CABECERA = "**Petición**"
CABECERA_ATENDIDA = "**Petición atendida**"
# Estados en los que el trabajo ya se entregó: una petición nueva lo devuelve a Revisiones.
ESTADOS_ENTREGADOS = ("In review", "QA editor", "Validada")


def texto_peticion(texto: str, quien: str, dia: date) -> str:
    limpio = re.sub(r"[ \t]+", " ", texto or "").strip()
    if not limpio:
        raise ValueError("La petición está vacía.")
    return f"{CABECERA} ({dia.isoformat()}, {quien}): {limpio}"


def texto_atendida(nota: str, quien: str) -> str:
    limpio = re.sub(r"\s+", " ", nota or "").strip()
    if not limpio:
        raise ValueError("Falta decir qué se ha hecho con la petición (--nota).")
    return f"{CABECERA_ATENDIDA} ({quien}): {limpio}"


def estado_tras_peticion(actual: str | None) -> str | None:
    """Revisiones si el trabajo ya estaba entregado; None si la issue se queda donde está."""
    return "Revisiones" if actual in ESTADOS_ENTREGADOS else None


def campos_tras_peticion(valores: dict) -> dict[str, str]:
    """Campos que cambian cuando llega una petición sobre algo ya entregado (vacío si no cambia nada)."""
    if estado_tras_peticion(valores.get("Status")) is None:
        return {}
    campos = {"Status": "Revisiones", "Revisión IA": "Cambios pedidos"}
    if valores.get("Editor") == "Funciona":
        campos["Editor"] = "Sin probar"  # lo que se probó ya no es lo que se pide
    return campos


def para(issues: list[dict], login: str, aprobador: bool) -> list[dict]:
    """Issues abiertas con petición pendiente: las suyas, las que no tienen dueño y, para los aprobadores, todas."""
    def sin_dueno(issue: dict) -> bool:
        return not issue.get("assignees", {}).get("nodes")

    return [i for i in issues if ETIQUETA in objetos.nombres_etiquetas(i) and (aprobador or es_de(i, login) or sin_dueno(i))]


def cmd_pedir(args: argparse.Namespace) -> None:
    proyecto = cargar_issue(args.numero)
    issue = proyecto["items"].get(args.numero)
    if issue is None:
        raise ErrorTablero(f"La issue #{args.numero} no está en el tablero.")
    rechazar_descartada(args.numero, issue)
    if issue["state"] != "OPEN":
        raise ErrorTablero(f"#{args.numero} está cerrada: si el mismo fallo ha vuelto, `tablero.py editor {args.numero} falla`; "
                           "si es otra cosa, crea una issue nueva en su objeto.")
    try:
        texto = texto_peticion(args.texto, usuario_actual(), date.today())
    except ValueError as exc:
        raise ErrorTablero(str(exc)) from exc
    objetos.crear_etiqueta_si_falta(gh, REPO, ETIQUETA, COLOR, DESCRIPCION_ETIQUETA)
    comentar(args.numero, texto)
    gh("issue", "edit", str(args.numero), "--repo", REPO, "--add-label", ETIQUETA)
    campos = campos_tras_peticion(issue["valores"])
    for campo, valor in campos.items():
        poner_campo(proyecto, args.numero, campo, valor)
    print(f"#{args.numero}: petición publicada" + ("; vuelve a Revisiones" if campos else ""))


def cmd_atendida(args: argparse.Namespace) -> None:
    try:
        texto = texto_atendida(args.nota, usuario_actual())
    except ValueError as exc:
        raise ErrorTablero(str(exc)) from exc
    comentar(args.numero, texto)
    gh("issue", "edit", str(args.numero), "--repo", REPO, "--remove-label", ETIQUETA)
    print(f"#{args.numero}: petición atendida")


def anadir_comandos(sub: argparse._SubParsersAction) -> None:
    p = sub.add_parser("pedir", help="pedir un cambio en una issue: comentario **Petición** y etiqueta `peticion`")
    p.add_argument("numero", type=int)
    p.add_argument("--texto", required=True, help="qué se pide, con el detalle que haga falta")
    p.set_defaults(fn=cmd_pedir)
    p = sub.add_parser("atendida", help="cerrar la petición de una issue: qué se ha hecho y fuera la etiqueta")
    p.add_argument("numero", type=int)
    p.add_argument("--nota", required=True, help="qué se ha hecho con lo pedido")
    p.set_defaults(fn=cmd_atendida)
