"""Avisos diarios: qué ha entrado en dev sin pasar por revisión y el resultado de la rutina, por correo al director.

A dev solo se llega por PR (nunca por push directo) y fusionarla puede cualquiera de los tres; lo que no puede
faltar, en el trabajo que las lleva, es la revisión IA cruzada y la prueba en el editor. `sync` y `auditar` ya devuelven a revisión lo que llega por una PR sin
validar. Aquí se cubre lo que ellos no ven y se junta todo en un correo:

- Un push directo con commits de código que no son de ninguna PR fusionada abre una issue `sin-revision` (In review,
  P0, con revisor cruzado), sea quien sea el autor: el aviso queda como estado en el tablero, no solo en el correo.
- Una PR fusionada en dev con una issue sin revisión aprobada (o, en un lote, sin probar), o sin issue y con código,
  sale como incidencia.
- Lo que solo toca rutas de organización (tablero, skills, workflows, guía, documentación) va sin revisión a
  propósito: no es incidencia.
- El aviso se publica como comentario que menciona a cada destinatario; GitHub se lo manda por correo.

Funciones puras; hablar con GitHub es cosa de control_avisos.py.
"""

from __future__ import annotations

import re
from datetime import datetime, timedelta

import auditoria
import volcado

ETIQUETA = auditoria.ETIQUETA_SIN_REVISION
COLOR = "B60205"
DESCRIPCION_ETIQUETA = "Código que entró en dev por push directo, sin PR ni revisión (tablero.py avisos)"
TIPOS_PUSH = ("push", "force_push")
# Una PR que GitHub da por fusionada por el propio push lleva la hora del push, con segundos de diferencia.
MARGEN_FUSION = timedelta(minutes=5)
MAX_COMMITS = 40
MAX_LINEAS = 15
MAX_TITULO = 70
MAX_PARTE = 6000
MAX_EXTRACTO = 140
ETIQUETAS_DE_AVISO = (ETIQUETA, auditoria.ETIQUETA, auditoria.ETIQUETA_QA)


def _commits(n: int) -> str:
    return f"{n} commit{'s' if n != 1 else ''}"


def fecha(texto: str) -> datetime:
    return datetime.fromisoformat(texto.replace("Z", "+00:00"))


def pushes_directos(actividad: list[dict], desde: datetime) -> list[dict]:
    """Pushes a la rama que no son la fusión de una PR, desde `desde`, del más antiguo al más reciente."""
    lista = [{"actor": (a.get("actor") or {}).get("login") or "desconocido", "antes": a["before"],
              "despues": a["after"], "cuando": fecha(a["timestamp"]), "forzado": a["activity_type"] == "force_push"}
             for a in actividad if a.get("activity_type") in TIPOS_PUSH and fecha(a["timestamp"]) >= desde]
    return sorted(lista, key=lambda p: p["cuando"])


def prs_del_push(commits: list[dict], prs_por_commit: dict[str, list[dict]], cuando: datetime) -> set[int]:
    """PR ya fusionadas cuando llegó el push (o fusionadas por él) a las que pertenecen sus commits."""
    limite = cuando + MARGEN_FUSION
    return {pr["number"] for c in commits for pr in prs_por_commit.get(c["sha"], [])
            if pr.get("merged_at") and fecha(pr["merged_at"]) <= limite}


def commits_sin_pr(commits: list[dict], prs_por_commit: dict[str, list[dict]], cuando: datetime) -> list[dict]:
    """Commits del push que no son de ninguna PR fusionada. Los de mezcla no cuentan: no traen código propio."""
    return [c for c in commits if c["padres"] < 2 and not prs_del_push([c], prs_por_commit, cuando)]


def es_organizativo(ficheros: list[str], rutas: list[str]) -> bool:
    """True si todos los ficheros están en rutas de organización (una lista vacía no lo es: no se sabe qué toca)."""
    return bool(ficheros) and all(any(f == r or f.startswith(r) for r in rutas) for f in ficheros)


def pr_sin_validar(pr: dict, issues: dict[int, dict], rutas: list[str], lotes_: set[int]) -> str | None:
    """Incidencia de una PR fusionada en dev: issues sin revisión aprobada (en un lote, también sin probar) o sin issue.

    `issues` son los items del tablero por número (con `valores`); `lotes_`, los números de las issues `lote`.
    """
    ficheros = [f["path"] for f in pr.get("files") or []]
    if es_organizativo(ficheros, rutas):
        return None
    refs = sorted(pr["refs"] - lotes_)
    en_lote = bool(pr["refs"] & lotes_)
    quien = (pr.get("mergedBy") or {}).get("login") or "alguien"
    cabecera = f"PR #{pr['number']} fusionada en dev por **{quien}** ({fecha(pr['mergedAt']):%d-%m %H:%M} UTC)"
    if not refs:
        return f"{cabecera} sin enlazar ninguna issue y con código ({len(ficheros)} ficheros)."
    faltan = []
    for n in refs:
        valores = (issues.get(n) or {}).get("valores") or {}
        if not valores:
            continue  # issue fuera del tablero (p. ej. de otro repo): no se puede juzgar
        falta = [] if valores.get("Revisión IA") == "Aprobada" else [f"Revisión IA = {valores.get('Revisión IA') or 'vacía'}"]
        if en_lote and valores.get("Editor") != "Funciona":
            falta.append(f"Editor = {valores.get('Editor') or 'vacío'} (lote)")
        if falta:
            faltan.append(f"#{n} ({', '.join(falta)})")
    return f"{cabecera} con {', '.join(faltan)}." if faltan else None


def titulo_issue(actor: str, despues: str) -> str:
    return f"Revisar el push directo a dev de {actor} ({despues[:9]})"


def cuerpo_issue(push: dict, commits: list[dict], repo: str, integracion: str) -> str:
    lista = "\n".join(f"- `{c['sha'][:9]}` {c['titulo']}" for c in commits)
    return (f"@{push['actor']} subió {_commits(len(commits))} a `{integracion}` por push directo el "
            f"{push['cuando']:%Y-%m-%d %H:%M} UTC. No son de ninguna PR fusionada y no han pasado revisión:\n\n"
            f"{lista}\n\n"
            f"Diff: https://github.com/{repo}/compare/{push['antes'][:9]}...{push['despues'][:9]}\n\n"
            f"A `{integracion}` solo se llega por PR desde la rama de la issue. Este código ya está dentro: no se "
            "revierte, se revisa aquí.\n\n"
            "## Criterios de aceptación\n\n"
            "- [ ] Revisión IA cruzada del diff (`tablero.py ia <n> aprobada|cambios --revisor \"<quién> (Claude)\"`).\n"
            "- [ ] Probado en el editor (`tablero.py editor <n> funciona|falla`).\n"
            "- [ ] Si estos commits ya tienen su propia issue, está enlazada en un comentario.\n")


def linea_push(push: dict, sin_pr: list[dict], prs: set[int], issue: int | None) -> str:
    """Una línea del correo por push directo: quién, cuántos commits sin revisión y dónde queda registrado."""
    partes = [f"**{push['actor']}** subió a dev por push directo{' forzado' if push['forzado'] else ''} "
              f"({push['cuando']:%d-%m %H:%M} UTC)"]
    if sin_pr:
        destino = f" → #{issue}" if issue else ""
        partes.append(f"{_commits(len(sin_pr))} sin PR ni revisión{destino}: "
                      + ", ".join(f"`{c['sha'][:9]}` {c['titulo']}" for c in sin_pr[:5])
                      + ("…" if len(sin_pr) > 5 else ""))
    if prs:
        partes.append("trae la PR " + ", ".join(f"#{n}" for n in sorted(prs))
                      + ", que consta como fusionada sin que nadie la fusionara")
    return "; ".join(partes) + "."


def _etiquetas(issue: dict) -> set[str]:
    return {n["name"] for n in (issue.get("labels") or {}).get("nodes", [])}


def _asignados(issue: dict) -> set[str]:
    return {a["login"] for a in (issue.get("assignees") or {}).get("nodes", [])}


def secciones(issues: list[dict], login: str, aprobador: bool, ahora: datetime, dias_atasco: int) -> list[tuple[str, list[dict]]]:
    """Lo que espera por `login` entre las issues de trabajo abiertas, por secciones con contenido.

    Los aprobadores ven además lo que no es de nadie: avisos de organización, decisiones y Revisiones sin asignar.
    """
    def suya(issue: dict) -> bool:
        return login in _asignados(issue)

    def en(estado: str) -> list[dict]:
        return [i for i in issues if i["valores"].get("Status") == estado]

    paradas = [i for i, _ in volcado.atascadas(issues, ahora, dias_atasco)
               if suya(i) or i["valores"].get("Revisor") == login]
    todas = [
        ("Colisiones y avisos de organización", [i for i in issues if "colision" in _etiquetas(i)
                                                 or (_etiquetas(i) & set(ETIQUETAS_DE_AVISO) and (aprobador or suya(i)))]),
        ("Peticiones sin contestar", [i for i in issues if "peticion" in _etiquetas(i)
                                      and (aprobador or suya(i) or not _asignados(i))]),
        ("Te toca revisar", [i for i in en("In review") if i["valores"].get("Revisor") == login]),
        ("Revisiones: algo falla", [i for i in en("Revisiones") if suya(i) or (aprobador and not _asignados(i))]),
        ("Decisiones pendientes", [i for i in issues if "decision" in _etiquetas(i) and (aprobador or suya(i))]),
        ("QA editor: falta probar en el editor", [i for i in en("QA editor") if aprobador or suya(i)]),
        ("Tu trabajo en curso", [i for i in en("In progress") if suya(i)]),
        (f"Sin movimiento desde hace más de {dias_atasco} días", paradas),
    ]
    return [(titulo, sorted(lista, key=lambda i: i["number"])) for titulo, lista in todas if lista]


def _linea(issue: dict) -> str:
    titulo = issue.get("title", "")
    if len(titulo) > MAX_TITULO:
        titulo = titulo[:MAX_TITULO - 1] + "…"
    detalle = ", ".join(x for x in (issue["valores"].get("Prioridad"), ", ".join(sorted(_asignados(issue)))) if x)
    return f"- #{issue['number']} {titulo}" + (f" ({detalle})" if detalle else "")


def _numero(comentario: dict) -> int:
    url = comentario.get("issue_url") or comentario.get("pull_request_url") or ""
    return int(url.rstrip("/").rsplit("/", 1)[-1])


def menciones(comentarios: list[dict], login: str, desde: datetime) -> list[dict]:
    """Comentarios (de issues o de revisión de PR) escritos desde `desde` por otra persona que mencionan a `login`.

    Los automáticos del tablero no cuentan: lo que dicen ya está en las demás secciones.
    """
    patron = re.compile(rf"@{re.escape(login)}(?![\w-])", re.I)
    propias = {login.lower(), "github-actions[bot]"}
    lista = [c for c in comentarios if fecha(c["created_at"]) >= desde and patron.search(c.get("body") or "")
             and auditoria.es_conversacion(c.get("body") or "")  # «Lista para revisión» ya sale en «Te toca revisar»
             and ((c.get("user") or {}).get("login") or "").lower() not in propias]
    return sorted(lista, key=lambda c: c["created_at"])


def linea_mencion(comentario: dict) -> str:
    texto = " ".join((comentario.get("body") or "").split())
    if len(texto) > MAX_EXTRACTO:
        texto = texto[:MAX_EXTRACTO - 1] + "…"
    return (f"[#{_numero(comentario)}]({comentario['html_url']}) **{comentario['user']['login']}** "
            f"({fecha(comentario['created_at']):%d-%m %H:%M}): {texto}")


def prs_nuevas(prs: list[dict], desde: datetime) -> list[dict]:
    return sorted([p for p in prs if fecha(p["createdAt"]) >= desde], key=lambda p: p["number"])


def linea_pr(pr: dict, integracion: str) -> str:
    estado = {"OPEN": "abierta", "MERGED": "fusionada", "CLOSED": "cerrada"}.get(pr.get("state"), pr.get("state"))
    if pr.get("isDraft") and pr.get("state") == "OPEN":
        estado = "borrador"
    destino = pr.get("baseRefName") or "?"
    aviso = f" · **hacia `{destino}`**" if destino != integracion else ""
    return f"#{pr['number']} {pr['title']} ({pr['author']['login']}, {estado}){aviso}"


def render(login: str, por_secciones: list[tuple[str, list[dict]]], incidencias: list[str], ahora: datetime,
           parte: str | None = None, extras: list[tuple[str, list[str]]] = ()) -> str | None:
    """Comentario para `login`, con mención para que GitHub se lo mande por correo; None si no hay nada que decirle.

    `extras` son secciones ya redactadas (título, líneas), como las PR nuevas y las menciones; van tras las incidencias.
    """
    extras = [(titulo, lineas) for titulo, lineas in extras if lineas]
    if not por_secciones and not incidencias and not extras:
        return None
    resumen = [f"{len(incidencias)} incidencia{'s' if len(incidencias) != 1 else ''}"] if incidencias else []
    resumen += [f"{len(lineas)} · {titulo.lower()}" for titulo, lineas in extras]
    resumen += [f"{len(lista)} · {titulo.split(':')[0].lower()}" for titulo, lista in por_secciones]
    lineas = [f"@{login} · avisos del {ahora:%Y-%m-%d}: " + "; ".join(resumen) + ".", ""]
    if incidencias:
        lineas += ["### Incidencias: código en dev sin revisión", "", *(f"- {i}" for i in incidencias), ""]
    for titulo, extra in extras:
        lineas += [f"### {titulo} ({len(extra)})", "", *(f"- {e}" for e in extra[:MAX_LINEAS])]
        if len(extra) > MAX_LINEAS:
            lineas.append(f"- … y {len(extra) - MAX_LINEAS} más")
        lineas.append("")
    for titulo, lista in por_secciones:
        lineas += [f"### {titulo} ({len(lista)})", "", *(_linea(i) for i in lista[:MAX_LINEAS])]
        if len(lista) > MAX_LINEAS:
            lineas.append(f"- … y {len(lista) - MAX_LINEAS} más (`tablero.py pendiente`)")
        lineas.append("")
    if parte:
        recortado = parte.strip()
        if len(recortado) > MAX_PARTE:
            recortado = recortado[:MAX_PARTE].rsplit("\n", 1)[0] + "\n\n… (recortado)"
        lineas += ["---", "", "### Parte de la rutina", "", recortado, ""]
    return "\n".join(lineas).rstrip() + "\n"


def parte_reciente(comentarios: list[dict], ahora: datetime, horas: int = 12) -> str | None:
    """Texto del último parte de la rutina si se escribió o se actualizó en las últimas `horas`."""
    if not comentarios:
        return None
    ultimo = comentarios[-1]
    momento = fecha(ultimo.get("updated_at") or ultimo["created_at"])
    return ultimo.get("body") if ahora - momento <= timedelta(hours=horas) else None
