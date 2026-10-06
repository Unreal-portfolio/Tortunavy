"""Reglas puras del ciclo de una issue: validaciones, envío a revisión y fusión en dev.

- In progress: el autor puede probar en el editor. Si funciona, Editor = Funciona y la issue
  no pasará por QA editor; si falla, se queda en In progress (no se manda algo que no funciona).
- In review: terminada. Si el autor no la probó, va con Editor = Sin probar y se dice
  («Sin QA editor»).
- Revisiones: la revisión o la prueba encontraron un fallo.
- QA editor: revisión IA aprobada, falta probarla en el editor (en su rama o, si se fusionó antes de #282, en dev).
  Si ya se probó antes de la aprobación, se salta este paso y va directa a Validada.
- Validada: revisión IA aprobada y probada en el editor en su rama, sin fusionar (#282). En un lote espera a
  los demás miembros; con todo el lote (o la issue suelta) en Validada, pasa a Done y se fusiona la PR.
- A dev solo entra lo que está en Done.
- Descartada (etiqueta `chamber`, cerrada como not planned y, si es una tarjeta, en Backlog): fuera del juego tras el
  recorte a un único modo. Ninguna rutina la mueve, la audita ni avisa de ella, y ningún comando del ciclo la toca
  (`--forzar` tampoco). Solo vuelve con `--retomar`, lanzado en local por un aprobador después de registrar una
  **Decisión** suya posterior al descarte.
"""

from __future__ import annotations

import lotes
import memoria
import objetos

# Estados con trabajo en marcha o ya terminado: una fusión antigua no debe arrastrarlos.
ESTADOS_EN_CURSO = ("In progress", "Revisiones", "Done")
ETIQUETA_DECISION = "decision"
ETIQUETA_CHAMBER = "chamber"
COLOR_CHAMBER = "BFBFBF"
DESCRIPCION_CHAMBER = "Descartada en el recorte del juego: cerrada como not planned; ninguna rutina la procesa"
CABECERA_CHAMBER = "**Descartada**"
# Pie de los comentarios que escribe el puente cuando alguien lanza un comando a mano.
MARCA_PUENTE = "\n\n_Lanzado por "
AVISO_SIN_QA = "**Sin QA editor**: sin probar por el autor."


class EnvioRechazado(ValueError):
    """No se puede mandar a revisión algo que falla en el editor."""


def estado_objetivo(actual: str | None, valores: dict, fusionada: bool, en_lote: bool,
                    sin_pr: bool = False) -> tuple[str | None, bool]:
    """Estado que corresponde a la issue y si hay que cerrarla; None = no cambia.

    - Cambios pedidos o Editor = Falla → Revisiones, salvo en In progress: quien la tiene la arregla ahí.
    - Sin fusionar: aprobada y probada (en un lote o suelta) → Validada; aprobada sin probar y en In review
      → QA editor; en otro caso no cambia (en Revisiones manda el fallo o la petición que la devolvió).
    - Fusionada en dev: sin revisión aprobada → In review; aprobada sin probar → QA editor;
      aprobada y probada → Done, y se cierra.
    - Tarea solo de prueba (en QA editor y sin ninguna PR): Editor = Funciona → Done.
    """
    ia, editor = valores.get("Revisión IA"), valores.get("Editor")
    if editor == "Falla" or ia == "Cambios pedidos":
        return (None, False) if actual == "In progress" else ("Revisiones", False)
    if sin_pr and actual == "QA editor":
        return ("Done", True) if editor == "Funciona" else (None, False)
    lista = ia == "Aprobada" and editor == "Funciona"
    if not fusionada:
        if lista:
            return "Validada", False
        return ("QA editor", False) if ia == "Aprobada" and actual == "In review" else (None, False)
    if ia != "Aprobada":
        return "In review", False
    if editor != "Funciona":
        return "QA editor", False
    return "Done", True


def estado_tras_fallo_editor(actual: str | None) -> str | None:
    """Estado tras `editor falla`: en In progress no cambia (se sigue arreglando); en otro, Revisiones."""
    return None if actual == "In progress" else "Revisiones"


def preparar_revision(valores: dict) -> tuple[str | None, str | None]:
    """(Editor a fijar, aviso a comentar) al mandar a revisión. Error si falla en el editor."""
    editor = valores.get("Editor")
    if editor == "Falla":
        raise EnvioRechazado("Falla en el editor: no se manda a revisión algo que no funciona. "
                             "Arréglalo y pruébalo (`editor <n> funciona`); si ya está corregido y no puedes "
                             "probarlo, `campo <n> Editor \"Sin probar\"` y vuelve a mandarla.")
    if editor == "Funciona":
        return None, None
    return ("Sin probar" if editor != "Sin probar" else None), AVISO_SIN_QA


def revisor_del_equipo(firma: str | None, miembros) -> str | None:
    """Login del equipo con el que empieza la firma de una revisión («Mokius (Claude)» → Mokius)."""
    login = (firma or "").split(" ", 1)[0].strip()
    return login if login in miembros else None


def firma_de_puente(entorno) -> str:
    """Pie «Lanzado por <quién>» si el comando corre en el puente por `workflow_dispatch`; si no, cadena vacía.

    El puente comenta con el token de una sola persona: sin la firma no se sabe quién lanzó el comando.
    """
    actor = actor_de_puente(entorno)
    return f"{MARCA_PUENTE}{actor} a través del puente._" if actor else ""


def actor_de_puente(entorno) -> str | None:
    """Quién lanzó a mano el comando que corre en el puente (`workflow_dispatch`); None fuera del puente."""
    actor = entorno.get("GITHUB_TRIGGERING_ACTOR") or entorno.get("GITHUB_ACTOR")
    if entorno.get("GITHUB_ACTIONS") != "true" or entorno.get("GITHUB_EVENT_NAME") != "workflow_dispatch":
        return None
    return actor or None


def sin_firma(comentario: str) -> str:
    """El comentario sin el pie del puente, para compararlo con el texto que genera el tablero."""
    return comentario.split(MARCA_PUENTE, 1)[0].strip()


def editor_tras_fusion(valores: dict) -> str | None:
    """Editor al fusionar: Sin probar si no había valor o había fallado; se conserva un Funciona."""
    return None if valores.get("Editor") == "Funciona" else "Sin probar"


def mueve_por_fusion(estado: str | None, con_pr_abierta: bool) -> bool:
    """Si una PR fusionada puede mover la issue: no si hay trabajo en marcha o una PR abierta."""
    return estado not in ESTADOS_EN_CURSO and not con_pr_abierta


def cierra_por_fusion(estado: str | None, valores: dict, con_pr_abierta: bool) -> bool:
    """Si una issue abierta que ya está en Done se cierra al fusionarse su PR en dev.

    El ciclo de #282 la pasa a Done antes de fusionar, y `Closes #n` no cierra nada fuera de la rama
    por defecto: sin esto se quedaría abierta en Done (#436).
    """
    return (estado == "Done" and not con_pr_abierta
            and estado_objetivo(estado, valores, fusionada=True, en_lote=False) == ("Done", True))


# --- issues descartadas (`chamber`) ----------------------------------------------------------------

def es_chamber(issue: dict) -> bool:
    """True si la issue está descartada (etiqueta `chamber`), abierta o cerrada.

    Admite la issue del Project o de `gh` (`labels`) y la normalizada de la auditoría (`etiquetas`).
    """
    etiquetas = issue.get("etiquetas")
    return ETIQUETA_CHAMBER in (etiquetas if etiquetas is not None else objetos.nombres_etiquetas(issue))


def sin_chamber(items: dict[int, dict]) -> dict[int, dict]:
    """Copia de los items del tablero sin las issues descartadas."""
    return {n: i for n, i in items.items() if not es_chamber(i)}


def descartadas(items: dict[int, dict]) -> set[int]:
    """Números de las issues descartadas entre los items del tablero."""
    return {n for n, i in items.items() if es_chamber(i)}


def motivo_chamber(numero: int, issue: dict) -> str | None:
    """Por qué un comando del ciclo no toca una issue descartada (None si no lo está)."""
    if not es_chamber(issue):
        return None
    return (f"#{numero} está descartada (`{ETIQUETA_CHAMBER}`): no entra en el ciclo; "
            "si vuelve al juego lo deciden SkiTemplar o Mokius.")


def decision_tras_descarte(comentarios: list[str], aprobadores) -> bool:
    """True si un aprobador firmó una **Decisión** después del último comentario de descarte.

    Sin comentario de descarte (etiqueta puesta a mano) vale cualquier decisión de un aprobador.
    """
    limpios = [sin_firma(c).lstrip() for c in comentarios]
    descartes = [i for i, c in enumerate(limpios) if c.startswith(CABECERA_CHAMBER)]
    posteriores = limpios[descartes[-1] + 1:] if descartes else limpios
    return any(memoria.quien_decide(c) in aprobadores for c in posteriores)


def motivo_para_no_retomar(numero: int, actor: str, aprobadores, comentarios: list[str]) -> str | None:
    """Por qué `--retomar` no devuelve al ciclo una descartada (None si se puede): aprobador y decisión registrada."""
    if actor not in aprobadores:
        return f"#{numero} está descartada: solo un aprobador puede retomarla ({actor} no lo es)."
    if not decision_tras_descarte(comentarios, aprobadores):
        return (f"#{numero} está descartada y ningún aprobador ha registrado después una **Decisión** que la "
                "recupere: sin ella no se retoma.")
    return None


def texto_chamber(motivo: str | None) -> str:
    """Comentario de una línea con el motivo del descarte; error si el motivo está vacío."""
    limpio = " ".join((motivo or "").split())
    if not limpio:
        raise ValueError("Falta el motivo del descarte (--motivo).")
    return f"{CABECERA_CHAMBER} (`{ETIQUETA_CHAMBER}`): {limpio}"


def pasos_chamber(issue: dict, texto: str, item: dict | None, etiquetas_de_personas: tuple[str, ...]) -> dict:
    """Lo que falta para dejar descartada una issue leída con `gh issue view --json state,labels,assignees,comments`.

    `item` es su item del Project (None si no está en el tablero): la tarjeta se aparca en Backlog, salvo un objeto o
    un lote, que no llevan Status. Se quitan las
    `etiquetas_de_personas` (peticion, decision, avisos…): nadie va a atenderlas. Idempotente: lo que ya está hecho
    no se repite (ni la etiqueta, ni el mismo comentario, ni el cierre).
    """
    comentarios = {sin_firma(c.get("body") or "") for c in issue.get("comments") or []}
    aparcada = item is None or (item.get("valores") or {}).get("Status") == "Backlog"
    return {"etiquetar": not es_chamber(issue),
            "quitar": sorted(objetos.nombres_etiquetas(issue) & set(etiquetas_de_personas)),
            "desasignar": sorted(a["login"] for a in issue.get("assignees") or []),
            "comentar": texto not in comentarios,
            "backlog": es_tarjeta(issue) and not aparcada,
            "cerrar": issue.get("state") == "OPEN"}


def es_tarjeta(issue: dict) -> bool:
    """True si la issue va en las columnas del Kanban: ni un objeto ni un lote, que no llevan Status."""
    return not (objetos.es_objeto(issue) or lotes.es_lote(issue))


def avisos_de_descarte(numero: int, leida: dict) -> list[str]:
    """Lo que el descarte deja colgando, para decidirlo a mano: el descarte no se propaga solo.

    `leida` es la issue de `objetos.leer_issue` (`blocking` y `subIssues` con sus etiquetas).
    - Lotes abiertos de los que es miembro: `lote estado` no deja fusionar su PR mientras siga dentro.
    - Issues que dependían de ella: cerrada cuenta como resuelta y `sync` las pasaría a Ready.
    - Sus sub-issues abiertas y vivas (las de un objeto): siguen en el ciclo hasta que se descarten una a una.
    """
    nodos = [n for n in (leida.get("blocking") or {}).get("nodes") or []
             if n.get("state") == "OPEN" and not es_chamber(n)]
    sub_issues = (leida.get("subIssues") or {}).get("nodes") or []
    en_lotes = _lista(n["number"] for n in nodos if lotes.es_lote(n))
    dependientes = _lista(n["number"] for n in nodos if not lotes.es_lote(n))
    vivas = _lista(s["number"] for s in sub_issues if s.get("state") == "OPEN" and not es_chamber(s))
    avisos = []
    if en_lotes:
        avisos.append(f"Aviso: #{numero} es miembro del lote {en_lotes}: saca su código y su «Closes» de la PR y "
                      "su dependencia del lote; `lote estado` no deja fusionarla mientras siga dentro")
    if dependientes:
        avisos.append(f"Aviso: {dependientes} dependían de #{numero}: cerrada cuenta como resuelta y `sync` las "
                      "pasará a Ready; descártalas también o registra de qué dependen ahora")
    if vivas:
        avisos.append(f"Aviso: las sub-issues {vivas} de #{numero} siguen vivas: el descarte no se propaga; "
                      "descarta las que también salgan del juego")
    return avisos


def _lista(numeros) -> str:
    return ", ".join(f"#{n}" for n in sorted(numeros))


def pasos_retomar(issue: dict) -> dict:
    """Lo que hace falta para retomar una issue descartada: quitarle la etiqueta y reabrirla si está cerrada."""
    descartada = es_chamber(issue)
    return {"quitar_etiqueta": descartada, "reabrir": descartada and issue.get("state") == "CLOSED"}
