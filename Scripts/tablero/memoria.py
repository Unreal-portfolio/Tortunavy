"""Memoria del equipo en las issues: comentarios «**Resumen**» y «**Decisión**».

No hay un registro aparte: lo que pasó y lo que se decidió se lee en la propia issue
(u objeto) afectada. Los textos son cortos a propósito; si no caben en el límite, es
que hay que resumir, no escribir una parrafada.
"""

from __future__ import annotations

import re
from datetime import date

MAX_CARACTERES = 400
CABECERA_RESUMEN = "**Resumen**"
CABECERA_DECISION = "**Decisión**"


class ErrorMemoria(ValueError):
    """Texto de resumen o de decisión que no cumple el formato breve."""


def compactar(texto: str | None, campo: str) -> str:
    """Una sola línea sin espacios repetidos; error si queda vacía o supera el límite."""
    limpio = re.sub(r"\s+", " ", texto or "").strip()
    if not limpio:
        raise ErrorMemoria(f"«{campo}» está vacío.")
    if len(limpio) > MAX_CARACTERES:
        raise ErrorMemoria(f"«{campo}» tiene {len(limpio)} caracteres (máximo {MAX_CARACTERES}): resúmelo en una o dos frases.")
    return limpio


def texto_resumen(que: str, como: str, pr: int | None = None, por_que: str | None = None) -> str:
    """Comentario «**Resumen**» de dos o tres líneas: qué fallaba, por qué (si se sabe) y cómo se arregló."""
    lineas = [CABECERA_RESUMEN, f"- Qué fallaba: {compactar(que, '--que')}"]
    if por_que and por_que.strip():
        lineas.append(f"- Por qué: {compactar(por_que, '--por-que')}")
    sufijo = f" (PR #{pr})" if pr is not None else ""
    lineas.append(f"- Cómo se arregló: {compactar(como, '--como')}{sufijo}")
    return "\n".join(lineas)


def texto_decision(texto: str, quien: str, dia: date) -> str:
    """Línea «**Decisión** (fecha, quién): …» para comentar en la issue u objeto afectado."""
    return f"{CABECERA_DECISION} ({dia.isoformat()}, {quien}): {compactar(texto, '--texto')}"


def es_resumen(comentario: str) -> bool:
    return comentario.lstrip().startswith(CABECERA_RESUMEN)
