"""Auditoría de organización del tablero: problemas de forma, no de código.

Cada problema tiene un tipo que decide qué hace `auditar --aplicar` (nunca toca código):

- `grave`: el fallo de organización afecta al trabajo (PR de un lote fusionada con miembros
  sin validar, estado incoherente con la PR, issue cerrada sin probar). La issue pasa a
  Revisiones con Prioridad P0 (reabierta si estaba cerrada) y un comentario que lo explica.
- `trivial`: basta mover la tarjeta a su columna o rellenar un campo evidente. Se corrige
  sin más y se anota en un comentario.
- `organizacion`: el resto. Etiqueta `revisar-organizacion` y un comentario con la lista.

También comprueba la forma de la issue (`problemas_de_formato`): etiqueta de tipo, título corto, cuerpo y
criterios de aceptación. `nueva` usa la misma función para no crear issues mal formadas.

La detección es pura: `problemas` recibe la issue ya normalizada con el contexto de sus PR
(`con_pr`, `fusionada`, `lote_fusionado`, `prs_sin_lote`, `fuera_de_lote`, `revisor_sugerido`).
"""

from __future__ import annotations

import json
import re
from collections.abc import Callable
from datetime import datetime, timedelta, timezone

import bloqueos
import flujo
import lotes
import memoria
import objetos

Gh = Callable[..., str]

ETIQUETA = "revisar-organizacion"
# La pone y la quita la rutina diaria de QA para lo que un script no ve; `auditar` no la toca.
ETIQUETA_QA = "revisar-qa"
COLOR = "FBCA04"
DESCRIPCION_ETIQUETA = "La issue tiene problemas de organización en el tablero (tablero.py auditar)"
CABECERA = "**Revisión de organización**"
DIAS_RESUMEN = 14
# Las issues cerradas antes de que existiera este sistema no tienen Resumen ni validaciones: no se auditan.
INICIO_SISTEMA = datetime(2026, 9, 30, tzinfo=timezone.utc)
# Asignado significa «estoy con ella ahora»: en estas columnas nadie está trabajando en la issue.
ESTADOS_SIN_DUENO = ("Backlog", "Ready", bloqueos.ESTADO)
CAMPOS_OBLIGATORIOS = ("Prioridad", "Área", "Tamaño", "Fase")
ETIQUETAS_TIPO = ("tarea", "⚠️bug⚠️", "bug")
# La misma de peticiones.py, que no se importa aquí para que la auditoría siga siendo pura.
ETIQUETA_PETICION = "peticion"
CABECERA_PETICION = "**Petición**"
CABECERA_ATENDIDA = "**Petición atendida**"
MAX_TITULO = 80
CASILLA = re.compile(r"^\s*[-*] \[[ xX]\]", re.M)
TITULOS_EXCLUIDOS = {"Parte diario del tablero", "Estado del tablero", "Avisos diarios del tablero"}
# Issues que abre `avisos` por un push directo a dev: su ciclo lo lleva control_avisos.py, no la auditoría.
ETIQUETA_SIN_REVISION = "sin-revision"
# Comentarios que escribe el propio tablero y no explican por qué algo falla.
PREFIJOS_AUTOMATICOS = ("Lista para revisión", "**Editor: funciona**", "**Revisión IA", "Fusionada en",
                        CABECERA_ATENDIDA, "**Rutina",
                        CABECERA, memoria.CABECERA_RESUMEN, memoria.CABECERA_DECISION, "**Sin QA editor**",
                        "Forma parte del lote", "En el lote #", "Vuelve a Ready sin asignado", "Probada en el editor",
                        flujo.CABECERA_CHAMBER)

CONSULTA_ISSUES = """
query($owner: String!, $repo: String!, $cursor: String, $since: DateTime) {
  repository(owner: $owner, name: $repo) {
    issues(first: 50, after: $cursor, states: [ESTADOS], filterBy: {since: $since}) {
      pageInfo { hasNextPage endCursor }
      nodes { number title body state stateReason closedAt author { login }
        labels(first: 20) { nodes { name } }
        assignees(first: 5) { nodes { login } }
        parent { number }
        blockedBy(first: 50) { nodes { number state } }
        blocking(first: 10) { nodes { number state labels(first: 10) { nodes { name } } } }
        comments(last: 40) { nodes { body author { login } } }
      }
    }
  }
}
"""


CONSULTA_CONVERSACION = """
query($owner: String!, $repo: String!, $num: Int!) {
  repository(owner: $owner, name: $repo) { issue(number: $num) {
    number state
    labels(first: 20) { nodes { name } }
    assignees(first: 5) { nodes { login } }
    comments(last: 40) { nodes { body author { login } } }
  } }
}
"""


def problema(texto: str, tipo: str = "organizacion", campo: str | None = None, valor: str | None = None) -> dict:
    return {"texto": texto, "tipo": tipo, "campo": campo, "valor": valor}


def explica_fallo(comentario: str) -> bool:
    """True si el comentario cuenta un fallo: prueba fallida con detalle, cambios pedidos o texto libre."""
    texto = comentario.lstrip()
    if texto.startswith("**Editor: falla**"):
        return "Sin detalle" not in texto
    if texto.startswith("**Revisión IA"):
        return "Cambios pedidos" in texto.splitlines()[0]
    return bool(texto) and not texto.startswith(PREFIJOS_AUTOMATICOS)


def es_conversacion(comentario: str) -> bool:
    """True si el comentario lo escribe alguien para los demás: texto libre, petición (o su respuesta) o decisión."""
    texto = comentario.lstrip()
    if texto.startswith((CABECERA_PETICION, CABECERA_ATENDIDA, memoria.CABECERA_DECISION)):
        return True
    return bool(texto) and not texto.startswith(PREFIJOS_AUTOMATICOS)


def autor_real(autor: str | None, comentario: str) -> str | None:
    """Quién habla: el autor del comentario o, si lo escribió el puente, quien lanzó el comando."""
    if flujo.MARCA_PUENTE in comentario:
        return comentario.rsplit(flujo.MARCA_PUENTE, 1)[1].split(" ", 1)[0]
    return autor


def conversacion_pendiente(issue: dict) -> bool:
    """True si lo último que se ha dicho en la issue espera respuesta de quien la tiene.

    Cuenta el último comentario de conversación (`es_conversacion`): si no es de un asignado, alguien ha pedido,
    decidido o comentado algo que el asignado aún no ha contestado. Sin asignado solo cuenta una petición
    expresa (`tablero.py pedir`): los comentarios sueltos los lee quien la coja.
    """
    autores = issue.get("autores") or [None] * len(issue["comentarios"])
    for autor, comentario in reversed(list(zip(autores, issue["comentarios"]))):
        if not es_conversacion(comentario):
            continue
        texto = comentario.lstrip()
        if texto.startswith(CABECERA_ATENDIDA):
            return False
        if not issue["asignados"]:
            return texto.startswith(CABECERA_PETICION)
        return autor_real(autor, comentario) not in issue["asignados"]
    return False


def conversacion_de(nodo: dict) -> dict:
    """Lo que necesita `accion_peticion` de una issue leída con CONSULTA_CONVERSACION."""
    comentarios = nodo["comments"]["nodes"]
    return {"estado": nodo["state"], "etiquetas": objetos.nombres_etiquetas(nodo),
            "asignados": [a["login"] for a in nodo["assignees"]["nodes"]],
            "comentarios": [c["body"] for c in comentarios],
            "autores": [(c.get("author") or {}).get("login") for c in comentarios]}


def accion_peticion(issue: dict) -> str | None:
    """«poner» o «quitar» la etiqueta `peticion` según la conversación de una issue abierta; None si está bien.

    Una descartada (`chamber`) no lleva `peticion`: nadie va a contestar.
    """
    if issue["estado"] != "OPEN" or flujo.es_chamber(issue):
        return None
    pendiente, etiquetada = conversacion_pendiente(issue), ETIQUETA_PETICION in issue["etiquetas"]
    if pendiente == etiquetada:
        return None
    return "poner" if pendiente else "quitar"


def graves_abierta(issue: dict) -> list[dict]:
    """Fallos de organización que afectan al trabajo en una issue abierta.

    Una refactorización se fusiona sin revisión ni prueba a propósito: no es un fallo.
    """
    estado = issue["valores"].get("Status")
    if estado == "Revisiones" or flujo.es_refactor(issue):
        return []  # ya está donde debe (el comentario explica el fallo) o no necesita las validaciones
    if issue.get("lote_fusionado") and estado not in lotes.LISTOS:
        return [problema(f"la PR #{issue['lote_fusionado']} de su lote se fusionó sin que esta issue estuviera "
                         "validada (revisión IA aprobada y Editor = Funciona)", "grave")]
    if (estado == "QA editor" and issue.get("con_pr") and not issue.get("fusionada")
            and issue["valores"].get("Revisión IA") != "Aprobada"):
        # Sin PR es una tarea solo de prueba (se crea directamente en QA editor); con PR, exige la revisión aprobada.
        return [problema("está en QA editor sin revisión IA aprobada y su PR no está fusionada en su rama base",
                         "grave")]
    return []


def columna_correcta(issue: dict) -> str | None:
    """Columna que le corresponde según la regla, si es otra y el cambio es solo mover la tarjeta."""
    estado = issue["valores"].get("Status")
    fusionada, en_lote, refactor = issue.get("fusionada", False), bool(issue.get("lotes")), flujo.es_refactor(issue)
    if fusionada and not flujo.mueve_por_fusion(estado, con_pr_abierta=False, refactor=refactor):
        return None
    destino, _ = flujo.estado_objetivo(estado, issue["valores"], fusionada, en_lote, refactor=refactor)
    if destino is None and estado == "Validada" and not fusionada and not refactor:
        return "In review"  # Validada exige revisión IA aprobada y Editor = Funciona (en un lote o suelta)
    return destino if destino and destino != estado and destino != "Done" else None


def triviales_abierta(issue: dict) -> list[dict]:
    valores = issue["valores"]
    estado = valores.get("Status")
    lista = []
    if destino := columna_correcta(issue):
        lista.append(problema(f"está en {estado or 'sin estado'} y le corresponde {destino}", "trivial", "Status", destino))
    if estado == "In review" and not valores.get("Revisor") and issue.get("revisor_sugerido"):
        lista.append(problema("In review sin Revisor", "trivial", "Revisor", issue["revisor_sugerido"]))
    elif estado == "In review" and not valores.get("Revisor"):
        lista.append(problema("In review sin Revisor ni asignado del que deducirlo"))
    if estado == "QA editor" and not valores.get("Editor"):
        lista.append(problema("QA editor sin campo Editor", "trivial", "Editor", "Sin probar"))
    if estado == "Ready" and (esperas := bloqueos.abiertas({"blockedBy": {"nodes": issue["bloqueantes"]}})):
        lista.append(problema(f"está en Ready y depende de {', '.join(f'#{n}' for n in esperas)}, que siguen abiertas",
                              "trivial", "Status", bloqueos.ESTADO))
    return lista


def problemas_de_formato(titulo: str, cuerpo: str | None, etiquetas: set[str]) -> list[str]:
    """Defectos de forma de una issue de trabajo: tipo, título, cuerpo y criterios de aceptación."""
    lista = []
    if not etiquetas & set(ETIQUETAS_TIPO):
        lista.append("sin etiqueta de tipo (`tarea` o `⚠️bug⚠️`)")
    if len(titulo.strip()) > MAX_TITULO:
        lista.append(f"título de {len(titulo.strip())} caracteres (máximo {MAX_TITULO}): el detalle va en el cuerpo")
    if not (cuerpo or "").strip():
        lista.append("sin cuerpo: falta qué hay que hacer o qué falla y los criterios de aceptación")
    elif not CASILLA.search(cuerpo):
        lista.append("sin criterios de aceptación (casillas `- [ ]` en el cuerpo)")
    return lista


def organizacion_abierta(issue: dict) -> list[dict]:
    valores = issue["valores"]
    estado = valores.get("Status")
    lista = [] if issue.get("padre") else [problema("no cuelga de ningún objeto (`tablero.py colgar <n> <objeto>`)")]
    lista += [problema(f"sin {campo}") for campo in CAMPOS_OBLIGATORIOS if not valores.get(campo)]
    if "colision" not in issue["etiquetas"]:  # las issues `colision` las redacta el propio tablero
        lista += [problema(t) for t in problemas_de_formato(issue["titulo"], issue.get("cuerpo"), issue["etiquetas"])]
    if estado == "Backlog" and valores.get("Prioridad") == "P0":
        lista.append(problema("P0 en Backlog: lo más urgente no puede estar sin aprobar; pásala a Ready o bájale "
                              "la Prioridad"))
    if estado == "In progress" and not issue["asignados"]:
        lista.append(problema("In progress sin asignado"))
    if estado in ESTADOS_SIN_DUENO and issue["asignados"]:
        lista.append(problema(f"tiene asignado ({', '.join(issue['asignados'])}) y está en {estado}: quien la tenga "
                              "que la coja (`tablero.py coger <n>`) o la suelte (`tablero.py soltar <n>`)"))
    if estado == "In review" and not issue.get("con_pr"):
        lista.append(problema("In review sin PR enlazada (`Closes #n` en la PR o rama `tipo/<n>-slug`)"))
    if estado == "Revisiones" and "colision" not in issue["etiquetas"] \
            and not any(explica_fallo(c) for c in issue["comentarios"]):
        lista.append(problema("en Revisiones sin un comentario que explique el fallo"))
    if estado == bloqueos.ESTADO and not issue.get("bloqueantes"):
        lista.append(problema("en Bloqueada sin dependencias registradas (`tablero.py bloquear <n> --por <m>`)"))
    for pr in issue.get("prs_sin_lote", []):
        lista.append(problema(f"su PR #{pr} cierra varias issues sin lote (`tablero.py lote crear`)"))
    for pr, lote in issue.get("fuera_de_lote", []):
        lista.append(problema(f"la cierra la PR #{pr} pero no es miembro del lote #{lote} "
                              f"(`tablero.py lote añadir {lote} {issue['numero']}`)"))
    return lista


def problemas_cerrada(issue: dict, ahora: datetime) -> list[dict]:
    """Cerradas en los últimos DIAS_RESUMEN días: resumen y, si se completaron, que estuvieran probadas."""
    cerrada = issue.get("cerrada")
    if cerrada is None or cerrada < INICIO_SISTEMA or ahora - cerrada > timedelta(days=DIAS_RESUMEN):
        return []
    lista = []
    completada = issue.get("motivo_cierre") == "COMPLETED"
    # Una `colision` no trae código propio ni se prueba en el editor: se cierra cuando las dos PR se pueden fusionar.
    # Una refactorización tampoco se prueba: le basta compilar y pasar los tests.
    probable = "colision" not in issue["etiquetas"] and not flujo.es_refactor(issue)
    if completada and probable and issue["valores"] and issue["valores"].get("Editor") != "Funciona":
        lista.append(problema("se cerró como completada sin estar probada en el editor (Editor ≠ Funciona)", "grave"))
    if not any(memoria.es_resumen(c) for c in issue["comentarios"]):
        lista.append(problema("cerrada sin comentario **Resumen** (`tablero.py resumen <n> --que ... --como ...`)"))
    return lista


def problemas_de_lote(motivos: list[str]) -> list[dict]:
    """Problemas de organización de un lote que incumple los topes (lotes.py) sin la etiqueta `excepcion`."""
    return [problema(f"incumple los topes de lotes sin `{lotes.ETIQUETA_EXCEPCION}`: {m}; pártelo o pide permiso a "
                     "SkiTemplar o Mokius (etiqueta `excepcion` y un comentario con el motivo)") for m in motivos]


def problemas(issue: dict, ahora: datetime) -> list[dict]:

    """Problemas de organización de una issue de trabajo (lista vacía si está en orden)."""
    if issue["estado"] != "OPEN":
        return problemas_cerrada(issue, ahora)
    graves = graves_abierta(issue)
    triviales = [] if graves else triviales_abierta(issue)
    return graves + triviales + organizacion_abierta(issue)


def texto_comentario(lista: list[dict]) -> str:
    return f"{CABECERA} (`tablero.py auditar`)\n" + "\n".join(f"- {p['texto']}" for p in lista)


def acciones(issue: dict, lista: list[dict]) -> dict:
    """Qué hacer al aplicar. Nunca toca código; no repite comentarios ni etiquetas.

    - graves: Revisiones + P0 (reabrir si está cerrada) y un comentario que lo explica;
    - triviales: los campos a corregir y un comentario que lo anota;
    - organización: etiqueta `revisar-organizacion` y un comentario con la lista, sin repetirlo;
      se quita la etiqueta cuando ya no queda ninguno.
    """
    graves = [p for p in lista if p["tipo"] == "grave"]
    triviales = [p for p in lista if p["tipo"] == "trivial"]
    organizacion = [p for p in lista if p["tipo"] == "organizacion"]
    campos = {p["campo"]: p["valor"] for p in triviales}
    comentarios = []
    if graves:
        campos = {"Status": "Revisiones", "Prioridad": "P0"}
        comentarios.append(f"{CABECERA}: fallo de organización que afecta al trabajo; pasa a Revisiones con P0.\n"
                           + "\n".join(f"- {p['texto']}" for p in graves))
    elif triviales:
        comentarios.append(f"{CABECERA}: corregido sin más.\n" + "\n".join(f"- {p['texto']}" for p in triviales))
    etiquetada = ETIQUETA in issue["etiquetas"]
    texto = texto_comentario(organizacion) if organizacion else None
    if texto and not any(flujo.sin_firma(c) == texto for c in issue["comentarios"]):
        comentarios.append(texto)
    return {"campos": campos, "reabrir": bool(graves) and issue["estado"] != "OPEN", "comentarios": comentarios,
            "etiquetar": bool(organizacion) and not etiquetada, "desetiquetar": not organizacion and etiquetada}


def es_de_trabajo(issue: dict) -> bool:
    """Issue que se audita: ni objeto, ni lote, ni `sin-revision`, ni descartada (`chamber`), ni de las fijas."""
    etiquetas = issue["etiquetas"]
    return (objetos.ETIQUETA not in etiquetas and lotes.ETIQUETA not in etiquetas
            and ETIQUETA_SIN_REVISION not in etiquetas and not flujo.es_chamber(issue)
            and issue["titulo"] not in TITULOS_EXCLUIDOS)


def normalizar(nodo: dict, valores: dict, contexto: dict | None = None) -> dict:
    """Issue de GraphQL en la forma que usan las funciones puras, más el contexto de sus PR."""
    cerrada = nodo.get("closedAt")
    return {
        "numero": nodo["number"], "titulo": nodo["title"], "estado": nodo["state"],
        "motivo_cierre": nodo.get("stateReason"),
        "cerrada": datetime.fromisoformat(cerrada.replace("Z", "+00:00")) if cerrada else None,
        "cuerpo": nodo.get("body") or "", "etiquetas": objetos.nombres_etiquetas(nodo),
        "asignados": [a["login"] for a in nodo["assignees"]["nodes"]],
        "padre": (nodo.get("parent") or {}).get("number"),
        "bloqueantes": bloqueos.bloqueantes(nodo),
        "lotes": lotes.lotes_de(nodo),
        "comentarios": [c["body"] for c in nodo["comments"]["nodes"]],
        "autores": [(c.get("author") or {}).get("login") for c in nodo["comments"]["nodes"]],
        "valores": valores,
        **(contexto or {}),
    }


def leer_issues(gh: Gh, repo: str, estado: str, desde: datetime | None = None) -> list[dict]:
    """Issues del repo en ese estado (OPEN o CLOSED), actualizadas desde `desde` si se indica."""
    owner, nombre = repo.split("/", 1)
    consulta = CONSULTA_ISSUES.replace("ESTADOS", estado)
    nodos, cursor = [], None
    while True:
        args = ["api", "graphql", "-f", f"query={consulta}", "-f", f"owner={owner}", "-f", f"repo={nombre}"]
        if cursor:
            args += ["-f", f"cursor={cursor}"]
        if desde:
            args += ["-f", f"since={desde.isoformat()}"]
        datos = json.loads(gh(*args))["data"]["repository"]["issues"]
        nodos += datos["nodes"]
        if not datos["pageInfo"]["hasNextPage"]:
            return nodos
        cursor = datos["pageInfo"]["endCursor"]
