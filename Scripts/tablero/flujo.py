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
"""

from __future__ import annotations

# Estados con trabajo en marcha o ya terminado: una fusión antigua no debe arrastrarlos.
ESTADOS_EN_CURSO = ("In progress", "Revisiones", "Done")
ETIQUETA_DECISION = "decision"
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
    actor = entorno.get("GITHUB_TRIGGERING_ACTOR") or entorno.get("GITHUB_ACTOR")
    if entorno.get("GITHUB_ACTIONS") != "true" or entorno.get("GITHUB_EVENT_NAME") != "workflow_dispatch" or not actor:
        return ""
    return f"{MARCA_PUENTE}{actor} a través del puente._"


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
