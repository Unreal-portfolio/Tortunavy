"""Base del tablero: configuración, llamadas a gh y git, lectura del proyecto y de las PR.

La comparten tablero.py y control.py; aquí no hay reglas del ciclo (están en flujo.py).
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import unicodedata
from pathlib import Path

import flujo

CONFIG = json.loads((Path(__file__).parent / "equipo.json").read_text(encoding="utf-8"))
REPO = CONFIG["repo"]
OWNER = CONFIG["proyecto"]["owner"]
NUMERO = CONFIG["proyecto"]["numero"]
INTEGRACION = CONFIG["rama_integracion"]
ESTADOS = ["Backlog", "Bloqueada", "Ready", "In progress", "In review", "Revisiones", "QA editor", "Validada", "Done"]
ORDEN_PRIORIDAD = {"P0": 0, "P1": 1, "P2": 2, "P3": 3}
ORDEN_TAMANO = {"XS": 0, "S": 1, "M": 2, "L": 3}
# «Closes #n» cierra la issue al fusionar; «Refs #n» solo la menciona (así se enlaza la PR con su lote).
REF_CIERRE = re.compile(r"\b(?:close[sd]?|fix(?:e[sd])?|resolve[sd]?|cierra|resuelve)\s+#(\d+)", re.I)
REF_MENCION = re.compile(r"\brefs?\s+#(\d+)", re.I)
REF_RAMA = re.compile(r"/(\d+)-")
# Respuesta de `gh project item-add` cuando la issue ya es un item del Project (auto-add de GitHub).
YA_EN_PROYECTO = "Content already exists"

CONSULTA_ITEMS = """
query($org: String!, $num: Int!, $cursor: String) {
  organization(login: $org) { projectV2(number: $num) {
    id
    fields(first: 30) { nodes { ... on ProjectV2SingleSelectField { id name options { id name } } } }
    items(first: 100, after: $cursor) {
      pageInfo { hasNextPage endCursor }
      nodes {
        id
        content { __typename
          ... on Issue { number title state url updatedAt
            assignees(first: 5) { nodes { login } } labels(first: 15) { nodes { name } }
            blockedBy(first: 50) { nodes { number state } }
            blocking(first: 10) { nodes { number state labels(first: 10) { nodes { name } } } } }
          ... on PullRequest { number title state url }
        }
        fieldValues(first: 20) { nodes {
          ... on ProjectV2ItemFieldSingleSelectValue { name field { ... on ProjectV2SingleSelectField { name } } }
        } }
      }
    }
  } }
}
"""


# Una issue con su item de este Project. El listado del Project tarda en traer los items recién añadidos.
CONSULTA_ITEM = """
query($owner: String!, $repo: String!, $num: Int!) {
  repository(owner: $owner, name: $repo) { issue(number: $num) {
    number title state url updatedAt
    assignees(first: 5) { nodes { login } } labels(first: 15) { nodes { name } }
    blockedBy(first: 50) { nodes { number state } }
    blocking(first: 10) { nodes { number state labels(first: 10) { nodes { name } } } }
    projectItems(first: 10) { nodes { id project { number }
      fieldValues(first: 20) { nodes {
        ... on ProjectV2ItemFieldSingleSelectValue { name field { ... on ProjectV2SingleSelectField { name } } }
      } } } }
  } }
}
"""


class ErrorTablero(RuntimeError):
    """Fallo de gh o de git que el usuario debe ver tal cual."""


def gh(*args: str, entrada: str | None = None) -> str:
    """Ejecuta gh y devuelve stdout; si falla, lanza ErrorTablero con su stderr."""
    proc = subprocess.run(["gh", *args], capture_output=True, text=True, encoding="utf-8", input=entrada)
    if proc.returncode != 0:
        raise ErrorTablero(f"gh {' '.join(args[:3])}…: {proc.stderr.strip()}")
    return proc.stdout


def git(*args: str) -> str:
    proc = subprocess.run(["git", *args], capture_output=True, text=True, encoding="utf-8")
    if proc.returncode != 0:
        raise ErrorTablero(f"git {' '.join(args)}: {proc.stderr.strip()}")
    return proc.stdout.strip()


def item_desde_issue(issue: dict | None, numero_proyecto: int) -> dict | None:
    """Item del tablero a partir de una issue leída con CONSULTA_ITEM; None si no existe o no está en este Project."""
    if not issue:
        return None
    propios = [i for i in issue["projectItems"]["nodes"] if i["project"]["number"] == numero_proyecto]
    if not propios:
        return None
    valores = {v["field"]["name"]: v["name"] for v in propios[0]["fieldValues"]["nodes"] if v.get("field")}
    contenido = {k: v for k, v in issue.items() if k != "projectItems"}
    return {"item": propios[0]["id"], "valores": valores, **contenido}


def completar_con(proyecto: dict, numero: int) -> None:
    """Si el listado del Project aún no trae la issue (recién añadida), la lee directamente y la incorpora."""
    if numero in proyecto["items"]:
        return
    owner, nombre = REPO.split("/", 1)
    salida = gh("api", "graphql", "-f", f"query={CONSULTA_ITEM}", "-f", f"owner={owner}", "-f", f"repo={nombre}",
                "-F", f"num={numero}")
    item = item_desde_issue(json.loads(salida)["data"]["repository"]["issue"], NUMERO)
    if item:
        proyecto["items"][numero] = item


def cargar_proyecto(numero: int | None = None) -> dict:
    """Devuelve id del proyecto, campos {nombre: {id, opciones}} e items de issues por número.

    Con `numero`, garantiza que esa issue está entre los items aunque el listado todavía no la traiga.
    """
    items, cursor, datos = {}, None, None
    while True:
        args = ["api", "graphql", "-f", f"query={CONSULTA_ITEMS}", "-f", f"org={OWNER}", "-F", f"num={NUMERO}"]
        if cursor:
            args += ["-f", f"cursor={cursor}"]
        datos = json.loads(gh(*args))["data"]["organization"]["projectV2"]
        for nodo in datos["items"]["nodes"]:
            contenido = nodo.get("content") or {}
            if contenido.get("__typename") != "Issue":
                continue
            valores = {v["field"]["name"]: v["name"] for v in nodo["fieldValues"]["nodes"] if v.get("field")}
            items[contenido["number"]] = {"item": nodo["id"], "valores": valores, **contenido}
        pagina = datos["items"]["pageInfo"]
        if not pagina["hasNextPage"]:
            break
        cursor = pagina["endCursor"]
    campos = {
        f["name"]: {"id": f["id"], "opciones": {o["name"]: o["id"] for o in f["options"]}}
        for f in datos["fields"]["nodes"] if f
    }
    proyecto = {"id": datos["id"], "campos": campos, "items": items}
    if numero is not None:
        completar_con(proyecto, numero)
    return proyecto


def item_de_issue(proyecto: dict, numero: int) -> str:
    """Id del item del proyecto para la issue; la añade si aún no está.

    Si el auto-add del Project se adelanta, `item-add` responde «Content already exists»:
    entonces se lee el item que ya existe y se sigue con él.
    """
    if numero in proyecto["items"]:
        return proyecto["items"][numero]["item"]
    cache = proyecto.setdefault("items_nuevos", {})
    if numero not in cache:
        url = f"https://github.com/{REPO}/issues/{numero}"
        try:
            salida = gh("project", "item-add", str(NUMERO), "--owner", OWNER, "--url", url, "--format", "json")
        except ErrorTablero as exc:
            if YA_EN_PROYECTO not in str(exc):
                raise
            completar_con(proyecto, numero)
            if numero not in proyecto["items"]:
                raise ErrorTablero(f"#{numero} ya está en el Project, pero no se encuentra su item.") from exc
            return proyecto["items"][numero]["item"]
        cache[numero] = json.loads(salida)["id"]
    return cache[numero]


def comprobar_campos(proyecto: dict, campos: dict) -> None:
    """Falla si algún valor no es una opción de su campo (los vacíos no cuentan): para comprobar antes de crear nada."""
    for campo, valor in campos.items():
        if not valor:
            continue
        info = proyecto["campos"].get(campo)
        if info is None or valor not in info["opciones"]:
            raise ErrorTablero(f"El campo «{campo}» no admite «{valor}». Opciones: {list((info or {}).get('opciones', {}))}")


def poner_campo(proyecto: dict, numero: int, campo: str, valor: str) -> None:
    comprobar_campos(proyecto, {campo: valor})
    info = proyecto["campos"][campo]
    gh("project", "item-edit", "--project-id", proyecto["id"], "--id", item_de_issue(proyecto, numero),
       "--field-id", info["id"], "--single-select-option-id", info["opciones"][valor])


def vaciar_campo(proyecto: dict, numero: int, campo: str) -> None:
    """Deja el campo sin valor; los objetos, por ejemplo, no llevan Status."""
    gh("project", "item-edit", "--project-id", proyecto["id"], "--id", item_de_issue(proyecto, numero),
       "--field-id", proyecto["campos"][campo]["id"], "--clear")


def usuario_actual() -> str:
    return gh("api", "user", "--jq", ".login").strip()


def prs_abiertas() -> list[dict]:
    campos = "number,title,headRefName,baseRefName,body,author,mergeable,isDraft,reviewDecision,createdAt,updatedAt"
    return json.loads(gh("pr", "list", "--repo", REPO, "--state", "open", "--limit", "100", "--json", campos))


def issues_de_pr(pr: dict, menciones: bool = False) -> set[int]:
    """Issues que cierra la PR (cuerpo y rama); con `menciones`, también las citadas con «Refs #n».

    Las menciones solo sirven para enlazar la PR con su lote: una issue citada no avanza ni cuenta como fusionada.
    """
    cuerpo = pr.get("body") or ""
    refs = {int(n) for n in REF_CIERRE.findall(cuerpo)}
    refs |= {int(n) for n in REF_RAMA.findall(pr.get("headRefName") or "")}
    if menciones:
        refs |= {int(n) for n in REF_MENCION.findall(cuerpo)}
    return refs


def es_de(issue: dict, login: str) -> bool:
    return login in {a["login"] for a in issue.get("assignees", {}).get("nodes", [])}


def slug(texto: str) -> str:
    ascii_ = unicodedata.normalize("NFKD", texto).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "-", ascii_.lower()).strip("-")[:40].strip("-")


def comentar(numero: int, texto: str) -> None:
    gh("issue", "comment", str(numero), "--repo", REPO, "--body", texto + flujo.firma_de_puente(os.environ))


def prs_fusionadas() -> list[dict]:
    """Últimas PR fusionadas del repo, de la más reciente a la más antigua, con su rama destino."""
    campos = "number,headRefName,baseRefName,body,mergedAt"
    return json.loads(gh("pr", "list", "--repo", REPO, "--state", "merged", "--limit", "100", "--json", campos))


def esta_fusionada(numero: int, fusionadas: list[dict], abiertas: list[dict]) -> bool:
    """True si una PR fusionada en dev enlaza la issue y no tiene ninguna PR abierta.

    Una PR abierta significa que hay un cambio nuevo sin fusionar: la fusión anterior ya no vale.
    """
    if any(numero in issues_de_pr(pr) for pr in abiertas):
        return False
    return any(pr["baseRefName"] == INTEGRACION and numero in issues_de_pr(pr) for pr in fusionadas)


def elegir_revisor(proyecto: dict, autor: str) -> str:
    """Revisor cruzado según equipo.json; entre varios, el que tiene menos revisiones abiertas."""
    candidatos = CONFIG["revisores"].get(autor) or [a for a in CONFIG["aprobadores"] if a != autor]
    carga = {c: 0 for c in candidatos}
    for issue in proyecto["items"].values():
        r = issue["valores"].get("Revisor")
        if issue["valores"].get("Status") == "In review" and r in carga:
            carga[r] += 1
    return min(candidatos, key=lambda c: (carga[c], candidatos.index(c)))
