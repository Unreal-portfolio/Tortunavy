"""Validador del ancho por tramos (#622, «Tramos variados»): sobre el manifest ya escrito (road_widths_m) comprueba
que el ancho está en su rango, que cambia con transiciones suaves, que hay tramos estrechos y anchos de verdad (no
un ancho único) y que las horquillas se ensanchan. Lo llama rally_circuit_check.circuit_report.
"""

from __future__ import annotations

import numpy as np

from .rally_circuit_width import NARROW_MAX_M, WIDE_MIN_M, WIDTH_RANGE_M

LIMITS_WIDTH = {"range_m": WIDTH_RANGE_M, "max_rate": 0.5, "sections": 2, "section_min_m": 30.0,
                "spread_m": 5.0, "hairpin_min_m": WIDE_MIN_M}


def road_widths(manifest: dict, count: int) -> np.ndarray:
    """Ancho (m) por punto de road_uu: road_widths_m si viene con un valor por punto; si no, road_width_m en todos."""
    widths = manifest.get("road_widths_m")
    if widths is not None and len(widths) == count:
        return np.asarray(widths, dtype=np.float64)
    return np.full(count, float(manifest["road_width_m"]))


def _runs(flag: np.ndarray) -> list[int]:
    """Longitudes (en muestras) de los tramos seguidos con flag, en un lazo cerrado."""
    if flag.all():
        return [len(flag)]
    start = int(np.argmin(flag))                     # empieza en una muestra sin flag: ningún tramo queda partido
    rolled = np.roll(flag, -start)
    out, run = [], 0
    for value in rolled:
        if value:
            run += 1
        elif run:
            out.append(run)
            run = 0
    return out + ([run] if run else [])


def _hairpin_widths(manifest: dict, ctx: dict, widths: np.ndarray) -> list[dict]:
    step, n, total = ctx["step"], len(widths), ctx["total"]
    out = []
    for e in manifest["elements"]:
        if e["type"] != "horquilla":
            continue
        s0, s1 = e["s_m"]
        length = (s1 - s0) % total
        k = (np.arange(int(round(0.25 * length / step)), int(round(0.75 * length / step)) + 1)
             + int(round(s0 / step))) % n
        out.append({"id": e["id"], "width_m": round(float(np.median(widths[k])), 2)})
    return out


def width_report(manifest: dict, ctx: dict) -> dict:
    step = ctx["step"]
    widths = ctx["half"] * 2.0
    rate = np.abs(np.roll(widths, -1) - widths) / step
    narrow = [r * step for r in _runs(widths <= NARROW_MAX_M + 1e-6)]
    wide = [r * step for r in _runs(widths >= WIDE_MIN_M - 1e-6)]
    minimum = LIMITS_WIDTH["section_min_m"]
    return {"per_point": manifest.get("road_widths_m") is not None and len(manifest["road_widths_m"]) == len(widths),
            "min_m": round(float(widths.min()), 2), "max_m": round(float(widths.max()), 2),
            "declared_max_m": manifest["road_width_m"], "max_rate": round(float(rate.max()), 3),
            "narrow_sections": sum(length >= minimum for length in narrow),
            "wide_sections": sum(length >= minimum for length in wide),
            "narrow_m": round(float(sum(narrow)), 1), "wide_m": round(float(sum(wide)), 1),
            "sections_listed": len(manifest.get("width_sections", [])),
            "hairpins": _hairpin_widths(manifest, ctx, widths)}


def width_verdict(r: dict) -> dict[str, bool]:
    L = LIMITS_WIDTH
    w = r["widths"]
    return {
        "widths": w["per_point"] and L["range_m"][0] <= w["min_m"] and w["max_m"] <= L["range_m"][1]
        and abs(w["declared_max_m"] - w["max_m"]) <= 0.01 and w["max_rate"] <= L["max_rate"],
        "width_variety": w["narrow_sections"] >= L["sections"] and w["wide_sections"] >= L["sections"]
        and w["max_m"] - w["min_m"] >= L["spread_m"] and w["sections_listed"] >= 2 * L["sections"]
        and all(h["width_m"] >= L["hairpin_min_m"] for h in w["hairpins"]),
    }
