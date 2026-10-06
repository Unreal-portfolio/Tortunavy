"""Lotes: varias issues que viajan en una sola PR.

Un lote es una issue temporal con la etiqueta `lote` y el título «Lote: <título>». Sus
miembros siguen colgando de su objeto real y se vinculan al lote con las dependencias
nativas de GitHub: el lote está «blocked by» cada miembro. Cada miembro se revisa y se
prueba por separado y, listo, espera en Validada. La PR del lote no se fusiona hasta que
todos están en Validada; al fusionarla pasan a Done a la vez y el lote se cierra con un
Resumen del conjunto.
"""

from __future__ import annotations

import objetos

ETIQUETA = "lote"
COLOR = "C5DEF5"
DESCRIPCION_ETIQUETA = "Issue temporal que agrupa las issues de una misma PR (tablero.py lote)"
PREFIJO = "Lote: "
MINIMO_MIEMBROS = 2
LISTOS = ("Validada", "Done")


class ErrorLote(ValueError):
    """Lote mal formado: pocos miembros, repetidos o que no son issues de trabajo."""


def es_lote(issue: dict) -> bool:
    return ETIQUETA in objetos.nombres_etiquetas(issue)


def titulo(texto: str) -> str:
    limpio = texto.strip()
    return limpio if limpio.startswith(PREFIJO) else PREFIJO + limpio


def comprobar_miembros(numeros: list[int]) -> list[int]:
    """Miembros en orden y sin repetir; error si son menos de dos."""
    unicos = list(dict.fromkeys(numeros))
    if len(unicos) != len(numeros):
        raise ErrorLote("Hay miembros repetidos en el lote.")
    if len(unicos) < MINIMO_MIEMBROS:
        raise ErrorLote(f"Un lote agrupa al menos {MINIMO_MIEMBROS} issues; para una sola, PR normal.")
    return unicos


def cuerpo(miembros: list[int], pr: int | None) -> str:
    lista = "\n".join(f"- [ ] #{n}" for n in miembros)
    enlace = f"PR del lote: #{pr}." if pr else "PR del lote: pendiente (su cuerpo debe llevar «Refs #<este lote>»)."
    return (f"Lote temporal: estas issues van en la misma PR.\n\n{lista}\n\n{enlace}\n\n"
            "Cada miembro se revisa y se prueba por separado y espera en Validada. La PR no se fusiona hasta que "
            "todos estén en Validada (`tablero.py lote estado <n>`); al fusionarla pasan a Done y el lote se "
            "cierra con un **Resumen** del conjunto.")


def lotes_de(issue: dict) -> list[int]:
    """Lotes abiertos de los que forma parte la issue (issues `lote` a las que bloquea)."""
    nodos = (issue.get("blocking") or {}).get("nodes") or []
    return sorted(n["number"] for n in nodos if n.get("state") == "OPEN" and es_lote(n))


def faltas(valores: dict, estado_issue: str) -> list[str]:
    """Qué le falta a un miembro para estar listo (lista vacía si está en Validada o Done)."""
    if valores.get("Status") in LISTOS or estado_issue == "CLOSED":
        return []
    lista = []
    if valores.get("Editor") == "Falla" or valores.get("Revisión IA") == "Cambios pedidos":
        lista.append("tiene un fallo por arreglar")
    if valores.get("Revisión IA") != "Aprobada":
        lista.append("falta la revisión IA aprobada")
    if valores.get("Editor") != "Funciona":
        lista.append("falta probarla en el editor")
    return lista or ["aprobada y probada, pero no está en Validada (ejecuta `sync --aplicar`)"]


def pendientes(miembros: dict[int, tuple[dict, str]]) -> dict[int, list[str]]:
    """Miembros que no están listos, con lo que les falta. Vacío = la PR del lote se puede fusionar."""
    return {n: f for n, (valores, estado) in sorted(miembros.items()) if (f := faltas(valores, estado))}


def con_decision(etiquetas_por_miembro: dict[int, set[str]], etiqueta: str) -> list[int]:
    """Miembros con una decisión pendiente: la PR del lote no se fusiona hasta que se decida."""
    return sorted(n for n, etiquetas in etiquetas_por_miembro.items() if etiqueta in etiquetas)


def pr_necesita_lote(refs_trabajo: set[int], con_lote: bool) -> bool:
    """Una PR que cierra varias issues de trabajo debe ir con su lote."""
    return len(refs_trabajo) >= MINIMO_MIEMBROS and not con_lote


def miembros_nuevos(lote: int, pedidos: list[int], actuales: set[int]) -> list[int]:
    """Miembros que hay que añadir al lote, en orden y sin los que ya están. Error si no queda ninguno."""
    if lote in pedidos:
        raise ErrorLote(f"El lote #{lote} no puede ser miembro de sí mismo.")
    nuevos = [n for n in dict.fromkeys(pedidos) if n not in actuales]
    if not nuevos:
        raise ErrorLote(f"{', '.join(f'#{n}' for n in pedidos)} ya están en el lote #{lote}.")
    return nuevos


def cuerpo_con_miembros(texto: str, nuevos: list[int]) -> str:
    """Cuerpo del lote con los miembros nuevos añadidos tras la última casilla de la lista."""
    lineas = texto.splitlines()
    casillas = [i for i, linea in enumerate(lineas) if linea.lstrip().startswith(("- [ ] #", "- [x] #"))]
    lineas_nuevas = [f"- [ ] #{n}" for n in nuevos]
    if not casillas:
        return texto.rstrip() + "\n\n" + "\n".join(lineas_nuevas)
    corte = casillas[-1] + 1
    return "\n".join(lineas[:corte] + lineas_nuevas + lineas[corte:])


def fuera_del_lote(cierra: set[int], miembros_por_lote: dict[int, set[int]]) -> dict[int, list[int]]:
    """Issues de trabajo que cierra una PR enlazada a lotes sin ser miembro de ninguno: {issue: [lotes]}."""
    if not miembros_por_lote:
        return {}
    todos = set().union(*miembros_por_lote.values())
    return {n: sorted(miembros_por_lote) for n in sorted(cierra - todos)}
