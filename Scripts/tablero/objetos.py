"""Objetos del tablero: issues padre que agrupan tareas y fallos como sub-issues nativas.

Un objeto es un sistema o una pieza del juego (el Rally, el puente tambaleante, el
HUD…). Es una issue con la etiqueta `objeto`, vive en el proyecto sin Status (no
se mueve por el Kanban) y sus tareas y fallos cuelgan de él como sub-issues.

Las funciones que hablan con GitHub reciben `gh` (la función de tablero.py) para
no depender de su configuración global; las puras se prueban sin red.
"""

from __future__ import annotations

import json
import re
import unicodedata
from collections.abc import Callable, Iterable

Gh = Callable[..., str]

ETIQUETA = "objeto"
COLOR = "1D76DB"
DESCRIPCION_ETIQUETA = "Sistema o pieza del juego que agrupa sus tareas y fallos"
# Palabras que no distinguen un objeto de otro al comparar nombres.
PALABRAS_VACIAS = frozenset({"y", "e", "o", "de", "del", "el", "la", "los", "las", "en", "con", "a", "al"})
MIN_LETRAS_PALABRA = 3

CONSULTA_ISSUE = """
query($owner: String!, $repo: String!, $num: Int!) {
  repository(owner: $owner, name: $repo) { issue(number: $num) {
    id number title state
    blockedBy(first: 50) { nodes { number state } }
    parent { number title }
    subIssues(first: 100) { nodes { number title state } }
  } }
}
"""

MUTACION_COLGAR = """
mutation($padre: ID!, $hijo: ID!) {
  addSubIssue(input: {issueId: $padre, subIssueId: $hijo}) { issue { number } subIssue { number } }
}
"""


class ErrorObjeto(RuntimeError):
    """Operación sobre objetos que no se puede hacer tal como se ha pedido."""


def nombres_etiquetas(issue: dict) -> set[str]:
    """Etiquetas de una issue, venga de `gh issue list` (lista) o del proyecto (nodes)."""
    etiquetas = issue.get("labels") or []
    if isinstance(etiquetas, dict):
        etiquetas = etiquetas.get("nodes", [])
    return {e["name"] for e in etiquetas}


def es_objeto(issue: dict) -> bool:
    return ETIQUETA in nombres_etiquetas(issue)


def buscar_por_titulo(issues: Iterable[dict], titulo: str) -> int | None:
    """Número de la primera issue abierta con ese título exacto (sin espacios de los extremos)."""
    buscado = titulo.strip()
    for issue in issues:
        if issue.get("state", "OPEN") == "OPEN" and issue["title"].strip() == buscado:
            return issue["number"]
    return None


def palabras_clave(nombre: str) -> set[str]:
    """Palabras del nombre en minúsculas y sin tildes, sin las vacías ni las de menos de tres letras."""
    ascii_ = unicodedata.normalize("NFKD", nombre).encode("ascii", "ignore").decode().lower()
    return {p for p in re.findall(r"[a-z0-9]+", ascii_) if len(p) >= MIN_LETRAS_PALABRA and p not in PALABRAS_VACIAS}


def parecidos(issues: Iterable[dict], nombre: str) -> list[dict]:
    """Objetos abiertos que comparten alguna palabra clave con el nombre: posibles duplicados («HUD» y «HUD y menús»)."""
    buscadas = palabras_clave(nombre)
    return [i for i in issues if i.get("state", "OPEN") == "OPEN" and buscadas & palabras_clave(i["title"])]


def cuerpo_objeto(nombre: str, descripcion: str | None) -> str:
    texto = descripcion.strip() if descripcion else f"Objeto «{nombre}»."
    return (f"{texto}\n\nObjeto del tablero: sus tareas y fallos cuelgan de aquí como sub-issues. "
            "No se mueve por el Kanban; se ve en la vista «Objetos».")


def comprobar_padre(hijo: int, padre: int, padre_actual: int | None) -> bool:
    """True si hay que colgar; False si ya cuelga de ese padre. Error si cuelga de otro."""
    if hijo == padre:
        raise ErrorObjeto(f"#{hijo} no puede colgar de sí misma.")
    if padre_actual is None:
        return True
    if padre_actual == padre:
        return False
    raise ErrorObjeto(f"#{hijo} ya cuelga de #{padre_actual}. Quítala de allí antes de colgarla de #{padre}.")


def crear_etiqueta_si_falta(gh: Gh, repo: str, nombre: str, color: str, descripcion: str) -> None:
    """Crea la etiqueta en el repo si aún no existe (la usan objetos, auditoría y colisiones)."""
    existentes = json.loads(gh("label", "list", "--repo", repo, "--limit", "200", "--json", "name"))
    if not any(e["name"] == nombre for e in existentes):
        gh("label", "create", nombre, "--repo", repo, "--color", color, "--description", descripcion)


def asegurar_etiqueta(gh: Gh, repo: str) -> None:
    crear_etiqueta_si_falta(gh, repo, ETIQUETA, COLOR, DESCRIPCION_ETIQUETA)


def objetos_abiertos(gh: Gh, repo: str) -> list[dict]:
    salida = gh("issue", "list", "--repo", repo, "--state", "open", "--label", ETIQUETA,
                "--limit", "200", "--json", "number,title,state")
    return json.loads(salida)


def leer_issue(gh: Gh, repo: str, numero: int) -> dict:
    """Id de nodo, padre y sub-issues de una issue."""
    owner, nombre = repo.split("/", 1)
    salida = gh("api", "graphql", "-f", f"query={CONSULTA_ISSUE}", "-f", f"owner={owner}",
                "-f", f"repo={nombre}", "-F", f"num={numero}")
    issue = json.loads(salida)["data"]["repository"]["issue"]
    if issue is None:
        raise ErrorObjeto(f"La issue #{numero} no existe en {repo}.")
    return issue


def crear_objeto(gh: Gh, repo: str, nombre: str, descripcion: str | None) -> int:
    asegurar_etiqueta(gh, repo)
    url = gh("issue", "create", "--repo", repo, "--title", nombre.strip(), "--label", ETIQUETA,
             "--body", cuerpo_objeto(nombre, descripcion)).strip().splitlines()[-1]
    return int(url.rstrip("/").rsplit("/", 1)[-1])


def buscar_o_crear(gh: Gh, repo: str, nombre: str, descripcion: str | None = None,
                   nuevo: bool = False) -> tuple[int, bool]:
    """Número del objeto abierto con ese título; lo crea si no existe. Devuelve (número, creado).

    Si no existe pero hay objetos con nombre parecido, no lo crea salvo que se pida con `nuevo`: un objeto
    duplicado parte en dos la memoria de un mismo sistema.
    """
    abiertos = objetos_abiertos(gh, repo)
    existente = buscar_por_titulo(abiertos, nombre)
    if existente is not None:
        return existente, False
    if not nuevo and (candidatos := parecidos(abiertos, nombre)):
        lista = "; ".join(f"#{c['number']} «{c['title'].strip()}»" for c in candidatos)
        raise ErrorObjeto(f"No existe el objeto «{nombre.strip()}», pero hay parecidos: {lista}. Usa el título exacto "
                          f"de uno de ellos o, si de verdad es otro sistema, créalo con "
                          f"`tablero.py objeto \"{nombre.strip()}\" --nuevo`.")
    return crear_objeto(gh, repo, nombre, descripcion), True


def colgar(gh: Gh, repo: str, hijo: int, padre: int) -> bool:
    """Cuelga `hijo` como sub-issue de `padre`. Devuelve False si ya colgaba de él."""
    datos_hijo = leer_issue(gh, repo, hijo)
    padre_actual = (datos_hijo.get("parent") or {}).get("number")
    if not comprobar_padre(hijo, padre, padre_actual):
        return False
    datos_padre = leer_issue(gh, repo, padre)
    gh("api", "graphql", "-f", f"query={MUTACION_COLGAR}", "-f", f"padre={datos_padre['id']}",
       "-f", f"hijo={datos_hijo['id']}")
    return True


def sub_issue_existente(gh: Gh, repo: str, padre: int, titulo: str) -> int | None:
    """Número de una sub-issue abierta de `padre` con ese título, para no duplicarla."""
    return buscar_por_titulo(leer_issue(gh, repo, padre)["subIssues"]["nodes"], titulo)
