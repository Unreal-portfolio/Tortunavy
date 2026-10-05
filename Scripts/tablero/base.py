"""Base del tablero: configuración, llamadas a gh y git, lectura del proyecto y de las PR.

La comparten tablero.py y control.py; aquí no hay reglas del ciclo (están en flujo.py).
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import unicodedata
from collections.abc import Callable
from datetime import datetime, timedelta, timezone
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


# Campos de una issue con su item del Project: los mismos que trae CONSULTA_ITEMS para cada item.
CAMPOS_ISSUE = """
    number title state url updatedAt
    assignees(first: 5) { nodes { login } } labels(first: 15) { nodes { name } }
    blockedBy(first: 50) { nodes { number state } }
    blocking(first: 10) { nodes { number state labels(first: 10) { nodes { name } } } }
    projectItems(first: 10) { nodes { id project { number }
      fieldValues(first: 20) { nodes {
        ... on ProjectV2ItemFieldSingleSelectValue { name field { ... on ProjectV2SingleSelectField { name } } }
      } } } }
"""

# Una issue con su item de este Project. El listado del Project tarda en traer los items recién añadidos.
CONSULTA_ITEM = """
query($owner: String!, $repo: String!, $num: Int!) {
  repository(owner: $owner, name: $repo) { issue(number: $num) {""" + CAMPOS_ISSUE + """  } }
}
"""

CAMPOS_PROYECTO = """
    id
    fields(first: 30) { nodes { ... on ProjectV2SingleSelectField { id name options { id name } } } }
"""

# Consulta dirigida de los comandos de una issue (#780): la issue con su item y, si la caché no los tiene, el id del
# Project y sus campos, en una sola petición. Cuesta 1 punto frente a los ~55 de leer el Project entero.
CONSULTA_DIRIGIDA = """
query($owner: String!, $repo: String!, $num: Int!, $org: String!, $proyecto: Int!, $conCampos: Boolean!) {
  rateLimit { cost remaining }
  organization(login: $org) @include(if: $conCampos) { projectV2(number: $proyecto) {""" + CAMPOS_PROYECTO + """  } }
  repository(owner: $owner, name: $repo) { issue(number: $num) {""" + CAMPOS_ISSUE + """  } }
}
"""

CONSULTA_CAMPOS = """
query($org: String!, $proyecto: Int!) {
  rateLimit { cost remaining }
  organization(login: $org) { projectV2(number: $proyecto) {""" + CAMPOS_PROYECTO + """  } }
}
"""

# Status y Revisor de todos los items, para repartir revisiones entre varios candidatos sin leer el Project entero.
CONSULTA_REVISIONES = """
query($org: String!, $proyecto: Int!, $cursor: String) {
  organization(login: $org) { projectV2(number: $proyecto) {
    items(first: 100, after: $cursor) {
      pageInfo { hasNextPage endCursor }
      nodes {
        content { ... on Issue { number } }
        status: fieldValueByName(name: "Status") { ... on ProjectV2ItemFieldSingleSelectValue { name } }
        revisor: fieldValueByName(name: "Revisor") { ... on ProjectV2ItemFieldSingleSelectValue { name } }
      }
    }
  } }
}
"""

# Ids del Project, de sus campos y de sus opciones: cambian muy poco y se guardan en disco (ignorado por git).
DIR_CACHE = Path(__file__).parent / ".cache"
CADUCIDAD_CAMPOS = timedelta(hours=24)


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
    proyecto = {"id": datos["id"], "campos": campos_de(datos), "items": items}
    if numero is not None:
        completar_con(proyecto, numero)
    return proyecto


def campos_de(proyecto_v2: dict) -> dict:
    """Campos de selección única del Project: {nombre: {id, opciones: {nombre: id}}}."""
    return {
        f["name"]: {"id": f["id"], "opciones": {o["name"]: o["id"] for o in f["options"]}}
        for f in proyecto_v2["fields"]["nodes"] if f
    }


# --- consulta dirigida y caché de campos (#780) ---------------------------------------------------

def fichero_cache() -> Path:
    return DIR_CACHE / f"campos-{OWNER}-{NUMERO}.json"


def leer_cache_campos(ahora: datetime | None = None) -> dict | None:
    """{id, campos} del Project guardados en disco; None si no hay, están corruptos o han caducado."""
    try:
        datos = json.loads(fichero_cache().read_text(encoding="utf-8"))
        guardado = datetime.fromisoformat(datos["guardado"])
        vigente = timedelta(0) <= (ahora or datetime.now(timezone.utc)) - guardado < CADUCIDAD_CAMPOS
        return {"id": datos["id"], "campos": datos["campos"]} if vigente else None
    except (OSError, ValueError, KeyError, TypeError):
        return None


def guardar_cache_campos(proyecto: dict) -> None:
    """Escritura atómica: un comando que se corta a medias no deja una caché rota."""
    DIR_CACHE.mkdir(parents=True, exist_ok=True)
    destino = fichero_cache()
    temporal = destino.with_suffix(".tmp")
    datos = {"guardado": datetime.now(timezone.utc).isoformat(), "id": proyecto["id"], "campos": proyecto["campos"]}
    temporal.write_text(json.dumps(datos, ensure_ascii=False, indent=1), encoding="utf-8")
    temporal.replace(destino)


def borrar_cache_campos() -> None:
    """Para cuando cambian las opciones de un campo (p. ej. `asegurar-estados`): la próxima lectura las pide."""
    fichero_cache().unlink(missing_ok=True)


def cargar_campos(forzar: bool = False) -> dict:
    """Proyecto sin items: id y campos, de la caché si está vigente (0 puntos) o de GitHub (1 punto)."""
    guardados = None if forzar else leer_cache_campos()
    if guardados is not None:
        return {**guardados, "items": {}, "parcial": True, "campos_de_cache": True}
    salida = gh("api", "graphql", "-f", f"query={CONSULTA_CAMPOS}", "-f", f"org={OWNER}", "-F", f"proyecto={NUMERO}")
    datos = json.loads(salida)["data"]["organization"]["projectV2"]
    proyecto = {"id": datos["id"], "campos": campos_de(datos)}
    guardar_cache_campos(proyecto)
    return {**proyecto, "items": {}, "parcial": True, "campos_de_cache": False}


def cargar_issue(numero: int) -> dict:
    """Proyecto parcial para los comandos de una issue: id, campos y solo el item de `numero` (si está en el Project).

    Una sola petición de 1 punto: la issue con sus bloqueantes, lotes, etiquetas y valores del Project, y los campos
    si la caché no los tiene. `sync`, `auditar`, `colisiones`, `volcado` y `pendiente` siguen con `cargar_proyecto`.
    """
    guardados = leer_cache_campos()
    owner, nombre = REPO.split("/", 1)
    salida = gh("api", "graphql", "-f", f"query={CONSULTA_DIRIGIDA}", "-f", f"owner={owner}", "-f", f"repo={nombre}",
                "-F", f"num={numero}", "-f", f"org={OWNER}", "-F", f"proyecto={NUMERO}",
                "-F", f"conCampos={'false' if guardados else 'true'}")
    datos = json.loads(salida)["data"]
    de_cache = guardados is not None
    if not de_cache:
        proyecto_v2 = datos["organization"]["projectV2"]
        guardados = {"id": proyecto_v2["id"], "campos": campos_de(proyecto_v2)}
        guardar_cache_campos(guardados)
    item = item_desde_issue(datos["repository"]["issue"], NUMERO)
    return {**guardados, "items": {numero: item} if item else {}, "parcial": True, "campos_de_cache": de_cache,
            "coste": (datos.get("rateLimit") or {}).get("cost")}


def refrescar_campos(proyecto: dict) -> bool:
    """Si los campos venían de la caché, los vuelve a pedir a GitHub. True si ha refrescado."""
    if not proyecto.get("campos_de_cache"):
        return False
    frescos = cargar_campos(forzar=True)
    proyecto.update(id=frescos["id"], campos=frescos["campos"], campos_de_cache=False)
    return True


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


def campos_invalidos(proyecto: dict, campos: dict) -> list[str]:
    """Errores de los valores que no son una opción de su campo (los vacíos no cuentan)."""
    errores = []
    for campo, valor in campos.items():
        info = proyecto["campos"].get(campo)
        if valor and (info is None or valor not in info["opciones"]):
            errores.append(f"El campo «{campo}» no admite «{valor}». Opciones: {list((info or {}).get('opciones', {}))}")
    return errores


def comprobar_campos(proyecto: dict, campos: dict) -> None:
    """Falla si algún valor no es una opción de su campo: para comprobar antes de crear nada.

    Con los campos de la caché, un valor desconocido puede ser una opción nueva: se refrescan antes de fallar.
    """
    errores = campos_invalidos(proyecto, campos)
    if errores and refrescar_campos(proyecto):
        errores = campos_invalidos(proyecto, campos)
    if errores:
        raise ErrorTablero(errores[0])


def editar_item(proyecto: dict, numero: int, argumentos: Callable[[dict], list[str]]) -> None:
    """`gh project item-edit` con los ids de `argumentos(proyecto)`.

    Si falla con ids de la caché (p. ej. opciones redefinidas), refresca los campos y reintenta una vez.
    """
    item = item_de_issue(proyecto, numero)
    try:
        gh("project", "item-edit", "--project-id", proyecto["id"], "--id", item, *argumentos(proyecto))
    except ErrorTablero:
        if not refrescar_campos(proyecto):
            raise
        gh("project", "item-edit", "--project-id", proyecto["id"], "--id", item, *argumentos(proyecto))


def poner_campo(proyecto: dict, numero: int, campo: str, valor: str) -> None:
    comprobar_campos(proyecto, {campo: valor})
    editar_item(proyecto, numero, lambda p: ["--field-id", p["campos"][campo]["id"],
                                             "--single-select-option-id", p["campos"][campo]["opciones"][valor]])


def vaciar_campo(proyecto: dict, numero: int, campo: str) -> None:
    """Deja el campo sin valor; los objetos, por ejemplo, no llevan Status."""
    editar_item(proyecto, numero, lambda p: ["--field-id", p["campos"][campo]["id"], "--clear"])


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


def revisiones_del_proyecto() -> dict:
    """Items de issues con solo Status y Revisor (1 punto por página de 100), para un proyecto parcial."""
    items, cursor = {}, None
    while True:
        args = ["api", "graphql", "-f", f"query={CONSULTA_REVISIONES}", "-f", f"org={OWNER}", "-F", f"proyecto={NUMERO}"]
        if cursor:
            args += ["-f", f"cursor={cursor}"]
        datos = json.loads(gh(*args))["data"]["organization"]["projectV2"]["items"]
        for nodo in datos["nodes"]:
            numero = (nodo.get("content") or {}).get("number")
            if numero is None:
                continue
            valores = {campo: nodo[clave]["name"] for clave, campo in (("status", "Status"), ("revisor", "Revisor"))
                       if (nodo.get(clave) or {}).get("name")}
            items[numero] = {"valores": valores}
        if not datos["pageInfo"]["hasNextPage"]:
            return items
        cursor = datos["pageInfo"]["endCursor"]


def elegir_revisor(proyecto: dict, autor: str) -> str:
    """Revisor cruzado según equipo.json; entre varios, el que tiene menos revisiones abiertas.

    Con un solo candidato no hace falta contar; con un proyecto parcial, la carga se lee aparte.
    """
    candidatos = CONFIG["revisores"].get(autor) or [a for a in CONFIG["aprobadores"] if a != autor]
    if len(candidatos) == 1:
        return candidatos[0]
    items = revisiones_del_proyecto() if proyecto.get("parcial") else proyecto["items"]
    carga = {c: 0 for c in candidatos}
    for issue in items.values():
        r = issue["valores"].get("Revisor")
        if issue["valores"].get("Status") == "In review" and r in carga:
            carga[r] += 1
    return min(candidatos, key=lambda c: (carga[c], candidatos.index(c)))
