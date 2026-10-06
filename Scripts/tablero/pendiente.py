"""`pendiente`: qué hay para cada persona, por líneas (la principal primero y después una por modo).

Funciones de presentación; `cmd_pendiente` (en tablero.py) lee el Project y las PR y las llama.
"""

from __future__ import annotations

import auditoria
import bloqueos
import colisiones
import flujo
import lotes
import objetos
import peticiones
from base import ESTADOS, INTEGRACION, ORDEN_PRIORIDAD, ORDEN_TAMANO, es_de, rama_base, solo_descartadas


def clave_orden(issue: dict) -> tuple:
    v = issue["valores"]
    return (ORDEN_PRIORIDAD.get(v.get("Prioridad"), 9), ORDEN_TAMANO.get(v.get("Tamaño"), 9), issue["number"])


def linea(issue: dict) -> str:
    v = issue["valores"]
    etiquetas = ",".join(n["name"] for n in issue.get("labels", {}).get("nodes", []))
    meta = " ".join(x for x in (v.get("Prioridad"), v.get("Tamaño"), v.get("Área"), v.get("Fase")) if x)
    validacion = [f"{c}: {v[c]}" for c in ("Revisión IA", "Editor") if v.get(c)]
    if validacion:
        meta += " | " + ", ".join(validacion)
    quien = ",".join(a["login"] for a in issue.get("assignees", {}).get("nodes", [])) or "libre"
    return f"  #{issue['number']} [{meta}] {issue['title']}  ({quien}{'; ' + etiquetas if etiquetas else ''})"


ETIQUETA_DECISION = flujo.ETIQUETA_DECISION


def motivo_decision(numero: int, issue: dict, forzar: bool) -> str | None:
    """Por qué no coger aún una issue con una decisión pendiente (None si no la tiene o se fuerza)."""
    if forzar or ETIQUETA_DECISION not in etiquetas_de(issue):
        return None
    return (f"#{numero} tiene una decisión pendiente (etiqueta `decision`): lee la pregunta en sus comentarios y "
            "consúltala con la persona. Si decide seguir sin esperar, repite con --forzar; si la decisión ya está "
            f"tomada, regístrala antes con `tablero.py decidir {numero} --texto \"...\"` y quita la etiqueta.")


def con_decision(issues: list[dict], login: str, aprobador: bool) -> list[dict]:
    """Issues y objetos con decisión pendiente: todas para los aprobadores; para el resto, las que tiene asignadas."""
    return [i for i in issues if ETIQUETA_DECISION in etiquetas_de(i) and (aprobador or es_de(i, login))]


def etiquetas_de(issue: dict) -> set[str]:
    return {n["name"] for n in issue.get("labels", {}).get("nodes", [])}


def urgentes_de_organizacion(issues: list[dict], login: str, aprobador: bool) -> list[dict]:
    """Lo que va antes que cualquier otra tarea: colisiones entre PR e issues con avisos de organización.

    Los avisos son `revisar-organizacion` (de `auditar`), `revisar-qa` (de la rutina diaria) y `sin-revision` (push
    directo a dev sin revisar, de `avisos`). Las colisiones son
    de todo el equipo; los avisos, del asignado (los aprobadores ven todos).
    """
    avisos = {auditoria.ETIQUETA, auditoria.ETIQUETA_QA, auditoria.ETIQUETA_SIN_REVISION}
    return [i for i in issues if colisiones.ETIQUETA in etiquetas_de(i)
            or (avisos & etiquetas_de(i) and (aprobador or es_de(i, login)))]


def objetos_con_aviso(proyecto: dict) -> list[dict]:
    """Objetos abiertos que la rutina de QA ha marcado: no se cogen, pero su aviso tiene que verse."""
    return [i for i in proyecto["items"].values()
            if i["state"] == "OPEN" and objetos.es_objeto(i) and auditoria.ETIQUETA_QA in etiquetas_de(i)]


def probables_en_editor(issues: list[dict], login: str) -> list[dict]:
    """Tareas propias en In progress o In review que aún no constan como Funciona en el editor."""
    return [i for i in issues if es_de(i, login) and i["valores"].get("Status") in ("In progress", "In review")
            and i["valores"].get("Editor") != "Funciona"]


def por_linea(items: dict[int, dict]) -> list[tuple[str, dict[int, dict]]]:
    """Items repartidos por rama base: la principal siempre y primero; después, los modos con algo abierto."""
    grupos: dict[str, dict[int, dict]] = {rama: {} for rama in flujo.ramas_de_linea(INTEGRACION)}
    for numero, issue in items.items():
        grupos[rama_base(issue)][numero] = issue
    return [(rama, grupo) for rama, grupo in grupos.items()
            if rama == INTEGRACION or any(i.get("state") == "OPEN" for i in grupo.values())]


def linea_de_pr(pr: dict) -> str:
    """Línea en la que se enseña una PR: su base si es una rama de línea; si no (p. ej. a main), la principal."""
    return pr["baseRefName"] if flujo.es_rama_de_linea(pr.get("baseRefName"), INTEGRACION) else INTEGRACION


def pendiente_de_linea(rama: str, proyecto: dict, prs: list[dict], yo: str, aprobador: bool, chamber: set[int]) -> None:
    """Secciones de `pendiente` de una línea. En la principal salen todas; en un modo, solo las que tienen algo."""
    principal = rama == INTEGRACION

    def seccion(titulo: str, lineas: list[str]) -> None:
        if principal or lineas:
            imprimir_seccion(titulo, lineas)

    abiertas = [i for i in proyecto["items"].values()
                if i["state"] == "OPEN" and not objetos.es_objeto(i) and not lotes.es_lote(i)]
    por_estado = {e: sorted([i for i in abiertas if i["valores"].get("Status") == e], key=clave_orden) for e in ESTADOS}
    mias = [i for i in por_estado["In progress"] if es_de(i, yo)]
    seccion("Primero: colisiones entre PR y organización del tablero",
            [linea(i) for i in sorted(urgentes_de_organizacion(abiertas + objetos_con_aviso(proyecto), yo, aprobador),
                                      key=clave_orden)])
    seccion("Peticiones: alguien pide un cambio en la issue (lee sus comentarios antes de seguir)",
            [linea(i) for i in sorted(peticiones.para(abiertas, yo, aprobador), key=clave_orden)])
    seccion("Tu trabajo en curso", [linea(i) for i in mias])
    seccion("Puedes probar en el editor (tus tareas en curso o en revisión; no esperes a la revisión)",
            [linea(i) for i in sorted(probables_en_editor(abiertas, yo), key=clave_orden)])
    seccion("Te toca revisar (revisión IA cruzada)",
            [linea(i) for i in por_estado["In review"] if i["valores"].get("Revisor") == yo])
    seccion("Revisiones: algo no funciona, fallo comentado en la issue", [linea(i) for i in por_estado["Revisiones"]])
    seccion("Tus PR abiertas", [f"  PR #{p['number']} {p['title']} ({p['reviewDecision'] or 'sin revisar'}, {p['mergeable']})"
                                + marca_descartada(p, chamber) for p in prs if p["author"]["login"] == yo])
    if aprobador:
        seccion("PR de otros por revisar", [f"  PR #{p['number']} de {p['author']['login']}: {p['title']}"
                                            + marca_descartada(p, chamber)
                                            for p in prs if p["author"]["login"] != yo and not p["isDraft"]])
    todas_abiertas = [i for i in proyecto["items"].values() if i["state"] == "OPEN"]
    seccion("Decisiones pendientes (etiqueta decision)" if aprobador else "Esperan una decisión de SkiTemplar o Mokius",
            [linea(i) for i in sorted(con_decision(todas_abiertas, yo, aprobador), key=clave_orden)])
    seccion("En QA editor: aprobado por la IA, falta probar en el editor", [linea(i) for i in por_estado["QA editor"]])
    seccion(f"Validadas: revisadas y probadas, esperan a que su PR se fusione en {rama}",
            [linea(i) for i in por_estado["Validada"]])
    no_cogibles = {ETIQUETA_DECISION, "bloqueado"}
    libres = [i for i in por_estado["Ready"] if not i["assignees"]["nodes"] and not bloqueos.abiertas(i)
              and not no_cogibles & {n["name"] for n in i["labels"]["nodes"]}]
    if not aprobador:
        libres.sort(key=lambda i: (ORDEN_TAMANO.get(i["valores"].get("Tamaño"), 9) > 1, clave_orden(i)))
    seccion("Libre para coger (Ready)", [linea(i) for i in libres[:10]])
    if not libres and principal:
        seccion("Nada en Ready: backlog por concretar", [linea(i) for i in por_estado["Backlog"][:8]])


def marca_descartada(pr: dict, chamber: set[int]) -> str:
    """Sufijo de una PR que solo enlaza issues descartadas: no se revisa, se cierra o se le quitan los «Closes»."""
    return " · solo issues descartadas (`chamber`): ciérrala" if solo_descartadas(pr, chamber) else ""


def imprimir_seccion(titulo: str, lineas: list[str]) -> None:
    print(f"{titulo}:")
    print("\n".join(lineas) if lineas else "  (nada)")
    print()
