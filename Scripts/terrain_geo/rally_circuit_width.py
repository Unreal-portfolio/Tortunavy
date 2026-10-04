"""Ancho de la calzada por tramos de los circuitos de Rally por vueltas (#622, criterio «Tramos variados» del director,
04-10): la calzada se estrecha y se ensancha por reglas de ritmo, no tiene un ancho único.

    widths = width_profile(plan, seed)       # ancho (m) por muestra del eje, con transiciones de TAPER_M
    sections = width_sections(plan, widths)  # tramos estrechos, normales y anchos para el manifest

Reglas (por pieza del trazado, rally_circuit_plan):

  - recta de salida: ancha (zona de adelantamiento y parrilla 2 x 4);
  - horquilla: ensanche, que empieza HAIRPIN_LEAD_M antes, en la frenada (adelantamiento por dentro);
  - curva peraltada: las cerradas (radio < TIGHT_RADIUS_M), algo más anchas para elegir trazada; las rápidas, más
    estrechas (comprometen la línea);
  - salto: algo más ancho que el normal (margen lateral en el aterrizaje);
  - chicane, rasante, baches y badén: estrechos (precisión, rasante ciego, tramo roto);
  - enlaces: el ancho normal (WIDTH_NORMAL_M), que separa siempre dos tramos estrechos o anchos.

El ancho por pieza es constante y la transición entre dos anchos es lineal en TAPER_M (media móvil de TAPER_M sobre
el perfil escalonado, sin cambiar el valor dentro de las piezas más largas que TAPER_M). Los sorteos van con su propio
generador ([seed, 622, 1]): el trazado, el perfil y los elementos de una semilla no cambian.
"""

from __future__ import annotations

import numpy as np
from scipy import ndimage

from .rally_circuit_plan import Plan

WIDTH_NORMAL_M = 14.0
WIDTH_RANGE_M = (10.0, 20.0)          # ningún tramo sale de aquí (el validador lo exige)
NARROW_MAX_M = 12.5                   # hasta aquí, tramo «estrecho»
WIDE_MIN_M = 16.5                     # desde aquí, tramo «ancho»
TAPER_M = 24.0
HAIRPIN_LEAD_M = 30.0
TIGHT_RADIUS_M = 50.0
RULES_M = {
    "recta": (17.0, 18.5),
    "horquilla": (18.5, 20.0),
    "curva_cerrada": (15.0, 16.5),
    "curva_rapida": (12.0, 13.0),
    "salto": (15.0, 16.0),
    "chicane": (10.0, 11.0),
    "rasante": (10.5, 11.5),
    "baches": (11.0, 12.0),
    "baden": (10.5, 11.5),
}


def _rule(kind: str, radius_m: float) -> str:
    if kind == "curva_peraltada":
        return "curva_cerrada" if radius_m < TIGHT_RADIUS_M else "curva_rapida"
    return kind


def _mask(arc: np.ndarray, total: float, s0: float, s1: float) -> np.ndarray:
    s0, s1 = s0 % total, s1 % total
    return (arc >= s0) & (arc <= s1) if s0 <= s1 else (arc >= s0) | (arc <= s1)


def target_widths(plan: Plan, seed: int) -> np.ndarray:
    """Ancho escalonado (m) por muestra: el de la regla de cada pieza y WIDTH_NORMAL_M en los enlaces."""
    rng = np.random.default_rng([seed, 622, 1])
    arc, total = plan.arc, plan.length_m
    out = np.full(len(arc), WIDTH_NORMAL_M)
    hairpins = []
    for piece, s0, s1 in plan.spans:
        p = plan.pieces[piece]
        rule = RULES_M.get(_rule(p.kind, p.radius_m))
        width = float(rng.uniform(*rule)) if rule else WIDTH_NORMAL_M
        if p.kind == "horquilla":
            hairpins.append((s0 - HAIRPIN_LEAD_M, s1, width))
            continue
        out[_mask(arc, total, s0, s1)] = width
    for s0, s1, width in hairpins:      # la frenada de la horquilla manda sobre la cola de la pieza anterior
        out[_mask(arc, total, s0, s1)] = width
    return out


def width_profile(plan: Plan, seed: int) -> np.ndarray:
    """Ancho (m) por muestra del eje con transiciones lineales de TAPER_M."""
    stepped = target_widths(plan, seed)
    size = max(1, int(round(TAPER_M / plan.step_m)))
    smooth = ndimage.uniform_filter1d(stepped, size, mode="wrap")
    return np.clip(np.round(smooth, 3), *WIDTH_RANGE_M)


def width_class(width_m: float) -> str:
    if width_m <= NARROW_MAX_M:
        return "estrecho"
    return "ancho" if width_m >= WIDE_MIN_M else "normal"


def width_sections(plan: Plan, seed: int) -> list[dict]:
    """Tramos de ancho constante del perfil escalonado (antes de las transiciones), en el orden de la carrera, con el
    tramo que cruza la línea de salida unido: s_m desde la línea, ancho, clase y la pieza que lo pide."""
    stepped = target_widths(plan, seed)
    reason = np.full(len(stepped), "enlace", dtype=object)
    for piece, s0, s1 in plan.spans:
        p = plan.pieces[piece]
        lead = HAIRPIN_LEAD_M if p.kind == "horquilla" else 0.0
        reason[_mask(plan.arc, plan.length_m, s0 - lead, s1)] = p.kind
    change = np.nonzero((stepped != np.roll(stepped, 1)) | (reason != np.roll(reason, 1)))[0]
    if len(change) == 0:
        return [{"s_m": [0.0, round(plan.length_m, 2)], "width_m": round(float(stepped[0]), 2),
                 "class": width_class(float(stepped[0])), "piece": str(reason[0])}]
    step, n = plan.step_m, len(stepped)
    out = []
    for i, start in enumerate(change):
        end = change[(i + 1) % len(change)]
        length = float(((end - start) % n or n) * step)
        s0 = float(start * step)
        w = float(stepped[start])
        out.append({"s_m": [round(s0, 2), round((s0 + length) % plan.length_m, 2) or round(plan.length_m, 2)],
                    "length_m": round(length, 1), "width_m": round(w, 2), "class": width_class(w),
                    "piece": str(reason[start])})
    return out
