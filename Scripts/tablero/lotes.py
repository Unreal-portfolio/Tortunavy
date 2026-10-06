"""Lotes: varias issues que viajan en una sola PR.

Un lote es una issue temporal con la etiqueta `lote` y el título «Lote: <título>». Sus
miembros siguen colgando de su objeto real y se vinculan al lote con las dependencias
nativas de GitHub: el lote está «blocked by» cada miembro. Cada miembro se revisa y se
prueba por separado y, listo, espera en Validada. La PR del lote no se fusiona hasta que
todos están en Validada; al fusionarla pasan a Done a la vez y el lote se cierra con un
Resumen del conjunto.

Topes (prohibición del director del 06-10): un lote tiene como mucho MAXIMO_MIEMBROS issues y cada persona tiene como
mucho MAXIMO_LOTES_POR_PERSONA lote abierto.

- **Lote abierto de una persona**: issue `lote` abierta cuyo autor es esa persona o que tiene algún miembro abierto
  asignado a ella. Mientras tanto puede trabajar issues sueltas.
- **Fuera de los topes**: el lote con la etiqueta `excepcion` (un aprobador lo autorizó) y el lote cuyos miembros son
  todos `refactor`. No cuentan para el tope de nadie ni se auditan por tamaño.
- Los miembros cuentan todos, abiertos y cerrados: el tope es del tamaño de la PR.
"""

from __future__ import annotations

from collections.abc import Iterable

import objetos

ETIQUETA = "lote"
COLOR = "C5DEF5"
DESCRIPCION_ETIQUETA = "Issue temporal que agrupa las issues de una misma PR (tablero.py lote)"
PREFIJO = "Lote: "
MINIMO_MIEMBROS = 2
MAXIMO_MIEMBROS = 3
MAXIMO_LOTES_POR_PERSONA = 1
ETIQUETA_EXCEPCION = "excepcion"
COLOR_EXCEPCION = "D93F0B"
DESCRIPCION_EXCEPCION = "Lote fuera de los topes (3 issues, 1 abierto por persona) con permiso de un aprobador"
CABECERA_EXCEPCION = "**Excepción a los topes de lotes**"
# La misma de flujo.py, que importa este módulo.
ETIQUETA_REFACTOR = "refactor"
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


def faltas(valores: dict, estado_issue: str, refactor: bool = False) -> list[str]:
    """Qué le falta a un miembro para estar listo (lista vacía si está en Validada o Done).

    Una refactorización no pasa por revisión IA ni por el editor: solo la frena un fallo comentado.
    """
    if valores.get("Status") in LISTOS or estado_issue == "CLOSED":
        return []
    if refactor:
        con_fallo = valores.get("Editor") == "Falla" or valores.get("Revisión IA") == "Cambios pedidos"
        return ["tiene un fallo por arreglar"] if con_fallo else []
    lista = []
    if valores.get("Editor") == "Falla" or valores.get("Revisión IA") == "Cambios pedidos":
        lista.append("tiene un fallo por arreglar")
    if valores.get("Revisión IA") != "Aprobada":
        lista.append("falta la revisión IA aprobada")
    if valores.get("Editor") != "Funciona":
        lista.append("falta probarla en el editor")
    return lista or ["aprobada y probada, pero no está en Validada (ejecuta `sync --aplicar`)"]


def pendientes(miembros: dict[int, tuple[dict, str]], refactor: Iterable[int] = ()) -> dict[int, list[str]]:
    """Miembros que no están listos, con lo que les falta. Vacío = la PR del lote se puede fusionar.

    `refactor`: los miembros `refactor`, que no necesitan las validaciones.
    """
    refactor = set(refactor)
    return {n: f for n, (valores, estado) in sorted(miembros.items()) if (f := faltas(valores, estado, n in refactor))}


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


def _asignados(issue: dict) -> set[str]:
    return {a["login"] for a in (issue.get("assignees") or {}).get("nodes", [])}


def fuera_de_topes(etiquetas_lote: set[str], etiquetas_miembros: Iterable[set[str]]) -> bool:
    """True si el lote no cuenta para los topes: tiene `excepcion` o todos sus miembros son `refactor`."""
    miembros = list(etiquetas_miembros)
    return ETIQUETA_EXCEPCION in etiquetas_lote or (bool(miembros) and all(ETIQUETA_REFACTOR in e for e in miembros))


def lotes_abiertos(issues: dict[int, dict]) -> dict[int, dict]:
    """Lotes abiertos entre `issues` (items del Project o nodos de la auditoría, por número).

    Para cada uno: sus miembros (todos), las personas de las que es «lote abierto» (autor y asignados de los miembros
    abiertos) y si está fuera de los topes. Un miembro que no está en `issues` cuenta sin etiquetas ni asignados.
    """
    resultado = {}
    for numero, lote in issues.items():
        if lote.get("state") != "OPEN" or not es_lote(lote):
            continue
        nodos = (lote.get("blockedBy") or {}).get("nodes") or []
        datos = [issues.get(n["number"]) or {} for n in nodos]
        personas = {a for nodo, dato in zip(nodos, datos) if nodo.get("state") == "OPEN" for a in _asignados(dato)}
        if autor := (lote.get("author") or {}).get("login"):
            personas.add(autor)
        resultado[numero] = {"miembros": [n["number"] for n in nodos], "personas": personas,
                             "fuera": fuera_de_topes(objetos.nombres_etiquetas(lote),
                                                     [objetos.nombres_etiquetas(d) for d in datos])}
    return resultado


def excesos(miembros: int, personas: set[str], abiertos: dict[int, dict], excluir: int | None = None) -> list[str]:
    """Topes que incumpliría un lote con `miembros` issues de `personas` (vacío = cabe). `excluir`: el propio lote."""
    motivos = [f"{miembros} issues (máximo {MAXIMO_MIEMBROS} por lote)"] if miembros > MAXIMO_MIEMBROS else []
    for persona in sorted(personas):
        otros = sorted(n for n, d in abiertos.items() if n != excluir and not d["fuera"] and persona in d["personas"])
        if len(otros) >= MAXIMO_LOTES_POR_PERSONA:
            motivos.append(f"{persona} ya tiene abierto el lote {', '.join(f'#{n}' for n in otros)} "
                           f"(máximo {MAXIMO_LOTES_POR_PERSONA} por persona)")
    return motivos


def incumplimientos(abiertos: dict[int, dict]) -> dict[int, list[str]]:
    """Lotes abiertos que incumplen los topes sin estar fuera de ellos, con los motivos (para `auditar`).

    Si una persona tiene varios lotes abiertos, el más antiguo (número menor) es el legítimo: se marcan los demás.
    """
    resultado = {}
    for numero, datos in sorted(abiertos.items()):
        if datos["fuera"]:
            continue
        anteriores = {n: d for n, d in abiertos.items() if n < numero}
        if motivos := excesos(len(datos["miembros"]), datos["personas"], anteriores):
            resultado[numero] = motivos
    return resultado


def autorizacion(quien: str, autoriza: str | None, aprobadores: Iterable[str]) -> str:
    """Aprobador que autoriza la excepción: `--autoriza` o, si no se da, quien lanza el comando si es aprobador."""
    aprobadores = list(aprobadores)
    if autoriza:
        if autoriza not in aprobadores:
            raise ErrorLote(f"{autoriza} no es aprobador: autorizan {' o '.join(aprobadores)}.")
        return autoriza
    if quien in aprobadores:
        return quien
    raise ErrorLote(f"{quien} no es aprobador: la excepción necesita --autoriza <{'|'.join(aprobadores)}>.")


def texto_excepcion(motivos: list[str], motivo: str, autoriza: str) -> str:
    """Comentario de una línea en la issue `lote`: qué topes se saltan, por qué y quién lo autoriza."""
    limpio = " ".join((motivo or "").split())
    if not limpio:
        raise ErrorLote("Falta el motivo de la excepción (--excepcion \"<motivo>\").")
    return f"{CABECERA_EXCEPCION} ({'; '.join(motivos)}): {limpio}. Autoriza {autoriza}."


def fuera_del_lote(cierra: set[int], miembros_por_lote: dict[int, set[int]]) -> dict[int, list[int]]:
    """Issues de trabajo que cierra una PR enlazada a lotes sin ser miembro de ninguno: {issue: [lotes]}."""
    if not miembros_por_lote:
        return {}
    todos = set().union(*miembros_por_lote.values())
    return {n: sorted(miembros_por_lote) for n in sorted(cierra - todos)}
