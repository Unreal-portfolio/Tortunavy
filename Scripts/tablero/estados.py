"""Opciones del campo Status del proyecto: añadir estados sin perder los valores de los items.

Redefinir las opciones de un campo de selección única con `updateProjectV2Field` puede
cambiar sus ids y vaciar el valor de los items. Las opciones existentes se envían con su
id (GitHub las conserva) y, como red, se hace una foto del Status de todos los items
(issues, PR y borradores) antes de tocarlo y se restaura lo que haya cambiado.
"""

from __future__ import annotations

import json
from collections.abc import Callable

Gh = Callable[..., str]

CAMPO = "Status"
# (opción nueva, opción tras la que va)
NUEVAS = [
    ({"name": "Bloqueada", "color": "GRAY",
      "description": "Espera a que se cierren las issues de las que depende"}, "Backlog"),
    ({"name": "Validada", "color": "PINK",
      "description": "Aprobada y probada; espera a las demás issues de su lote"}, "QA editor"),
]
DESCRIPCIONES = {
    "Backlog": "Aún no aprobada para hacerse",
    "Ready": "Aprobada: se puede trabajar",
    "In progress": "Alguien (o su Claude) la está haciendo o la dejó a medias",
    "In review": "Terminada: la revisa la IA de otro miembro del equipo",
    "Revisiones": "La revisión o la prueba encontraron un fallo, comentado en la issue",
    "QA editor": "Aprobada por la IA; falta probarla en el editor",
    "Validada": "Aprobada y probada; espera a las demás issues de su lote",
    "Done": "Fusionada en dev, aprobada y probada en el editor; cerrada",
}

CONSULTA = """
query($org: String!, $num: Int!, $cursor: String) {
  organization(login: $org) { projectV2(number: $num) {
    id
    field(name: "Status") { ... on ProjectV2SingleSelectField { id options { id name color description } } }
    items(first: 100, after: $cursor) {
      pageInfo { hasNextPage endCursor }
      nodes { id fieldValueByName(name: "Status") { ... on ProjectV2ItemFieldSingleSelectValue { name } } }
    }
  } }
}
"""

MUTACION = """
mutation($campo: ID!, $opciones: [ProjectV2SingleSelectFieldOptionInput!]) {
  updateProjectV2Field(input: {fieldId: $campo, singleSelectOptions: $opciones}) {
    projectV2Field { ... on ProjectV2SingleSelectField { options { id name } } }
  }
}
"""


def opciones_objetivo(opciones: list[dict], nuevas: list[tuple[dict, str]],
                      descripciones: dict[str, str]) -> list[dict] | None:
    """Opciones actuales (con su id) más las nuevas en su sitio y las descripciones corregidas.

    None si no hay nada que cambiar: todas las nuevas existen y las descripciones ya coinciden.
    """
    resultado = [{"id": o.get("id"), "name": o["name"], "color": o["color"],
                  "description": descripciones.get(o["name"], o.get("description") or "")} for o in opciones]
    for nueva, despues in nuevas:
        nombres = [o["name"] for o in resultado]
        if nueva["name"] in nombres:
            continue
        if despues not in nombres:
            raise ValueError(f"No existe la opción «{despues}» en {CAMPO}: {nombres}")
        resultado.insert(nombres.index(despues) + 1, dict(nueva))
    actuales = [{"id": o.get("id"), "name": o["name"], "color": o["color"], "description": o.get("description") or ""}
                for o in opciones]
    return None if resultado == actuales else resultado


def pendientes_de_restaurar(foto: dict[str, str | None], actual: dict[str, str | None]) -> dict[str, str]:
    """Items cuyo Status ha cambiado o se ha vaciado respecto a la foto: item → valor a restaurar."""
    return {item: valor for item, valor in foto.items() if valor is not None and actual.get(item) != valor}


def leer(gh: Gh, org: str, numero: int) -> dict:
    """Id del proyecto, campo Status con sus opciones y foto {item: Status} de todos los items."""
    foto, cursor, datos = {}, None, None
    while True:
        args = ["api", "graphql", "-f", f"query={CONSULTA}", "-f", f"org={org}", "-F", f"num={numero}"]
        if cursor:
            args += ["-f", f"cursor={cursor}"]
        datos = json.loads(gh(*args))["data"]["organization"]["projectV2"]
        for nodo in datos["items"]["nodes"]:
            foto[nodo["id"]] = (nodo.get("fieldValueByName") or {}).get("name")
        if not datos["items"]["pageInfo"]["hasNextPage"]:
            break
        cursor = datos["items"]["pageInfo"]["endCursor"]
    return {"proyecto": datos["id"], "campo": datos["field"]["id"], "opciones": datos["field"]["options"], "foto": foto}


def redefinir(gh: Gh, campo: str, opciones: list[dict]) -> list[dict]:
    peticion = json.dumps({"query": MUTACION, "variables": {"campo": campo, "opciones": opciones}})
    datos = json.loads(gh("api", "graphql", "--input", "-", entrada=peticion))
    return datos["data"]["updateProjectV2Field"]["projectV2Field"]["options"]


def restaurar(gh: Gh, estado: dict, opciones: dict[str, str], cambios: dict[str, str]) -> None:
    """Vuelve a poner en cada item el Status de la foto (por nombre, con los ids vigentes)."""
    for item, valor in cambios.items():
        gh("project", "item-edit", "--project-id", estado["proyecto"], "--id", item,
           "--field-id", estado["campo"], "--single-select-option-id", opciones[valor])
