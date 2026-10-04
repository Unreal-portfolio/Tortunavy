"""Validador de los elementos de tierra (#682) sobre la variante YA ESCRITA, como rally_circuit_check (que lo llama):
saltos de forma (el piloto IA, a 0,9 · v, salva el hueco), baches (solo en recta, amplitud medida en la malla dentro
del recorrido de la suspensión del manifest y al menos al 60 % de la de diseño, y fuera de la frenada de las horquillas,
#696), badén (profundidad medida, no despega ni con turbo) y banqueta
(caballón medido por fuera de la horquilla y peralte). Solo añade criterios al veredicto si el manifest es del perfil
«tierra».
"""

from __future__ import annotations

import math

import numpy as np

from .rally_circuit_dirt import BERM_PEAK_M, DIP_MAX_G
from .rally_circuit_jumps import AI_FACTOR
from .rally_circuit_physics import G
from .rally_circuit_width import WIDTH_NORMAL_M

LIMITS_TIERRA = {"jumps": 4, "jump_kinds": 2, "bumps": 2, "straight_k": 1.0 / 400.0, "bump_measured_min": 0.6,
                 "amplitude_tol_m": 0.02, "dip_depth_tol_m": 0.15, "dip_g": DIP_MAX_G + 0.15,
                 "berm_min_ratio": 0.6, "banqueta_bank_deg": 8.0, "impact_ai_ms": 6.0, "hairpin_brake_gap_m": 0.0}
SAMPLE_M = 0.25
# Piloto IA (ATN_RallyAIController.h y FBrakeTuning de TN_RallyCircuit.h): frenada, horquillas y punta. Un test los lee
# del C++ para que no se desincronicen.
AI_BRAKE_G = 0.5
AI_HAIRPIN_G = 0.6
AI_MAX_KMH = 90.0
AI_MIN_KMH = 25.0


def is_tierra(manifest: dict) -> bool:
    return manifest.get("generator", {}).get("profile") == "tierra"


def _arc_index(s: float, step: float, n: int) -> int:
    return int(round(s / step)) % n


def _span_indices(span, step: float, n: int) -> np.ndarray:
    k0, k1 = _arc_index(span[0], step, n), _arc_index(span[1], step, n)
    return np.arange(k0, k1 + 1) % n if k1 >= k0 else np.arange(k0, k1 + n + 1) % n


def _along(ctx: dict, sampler, span, lateral_m: float = 0.0) -> tuple[np.ndarray, np.ndarray]:
    """Cota de la malla cada SAMPLE_M por el eje (desplazado lateral_m a la derecha) en el arco span."""
    pts, tan, right, step = ctx["pts"], ctx["tan"], ctx["right"], ctx["step"]
    n = len(pts)
    length = (span[1] - span[0]) % ctx["total"]
    s = np.arange(0.0, length, SAMPLE_M)
    k = np.array([_arc_index(span[0] + v, step, n) for v in s])
    frac = (span[0] + s) / step - np.round((span[0] + s) / step)
    xy = pts[k] + frac[:, None] * step * tan[k] + lateral_m * right[k]
    return s, sampler.top(xy)


def jump_ai_fields(e: dict, x: np.ndarray, line: np.ndarray, v: float, takeoff_flight) -> dict:
    """Vuelo medido a la velocidad del piloto IA (AI_FACTOR · v): en los saltos con hueco, tiene que pasarlo."""
    if "gap_s_m" not in e:
        return {"jump_kind": e["jump_kind"]} if "jump_kind" in e else {}
    land, _, angle = takeoff_flight(x, line, AI_FACTOR * v)
    gap_end = e["gap_s_m"][1] - e["lip_s_m"]
    zone_end = e["landing_s_m"][1] - e["lip_s_m"]
    impact = math.inf
    if land is not None:
        th = math.radians(angle)
        vx, vz = AI_FACTOR * v * math.cos(th), AI_FACTOR * v * math.sin(th) - G * land.airtime_s
        g = math.radians(-land.ground_deg)
        impact = max(0.0, -(vx * -math.sin(g) + vz * math.cos(g)))
    return {"jump_kind": e.get("jump_kind"), "gap_m": round(gap_end, 2),
            "x_land_ai_m": land.x_land_m if land else None, "impact_ai_ms": round(impact, 2),
            "ai_clears_gap": bool(land and gap_end <= land.x_land_m <= zone_end)}


def _straight(ctx: dict, span) -> float:
    idx = _span_indices(span, ctx["step"], len(ctx["pts"]))
    return float(np.abs(ctx["curvature"][idx]).max())


def ai_brake_m(v_ms: float, hairpin_radius_m: float) -> float:
    """m que necesita el piloto IA para frenar de v_ms (como mucho AI_MAX_KMH) a la velocidad de una horquilla de ese
    radio (TNRallyCircuit::FeatureSpeedLimitKmh: HairpinLateralG y BrakeDecelG de ATN_RallyAIController)."""
    v = min(v_ms, AI_MAX_KMH / 3.6)
    v_hairpin = max(math.sqrt(AI_HAIRPIN_G * G * max(hairpin_radius_m, 0.0)), AI_MIN_KMH / 3.6)
    return max(0.0, (v * v - v_hairpin * v_hairpin) / (2.0 * AI_BRAKE_G * G))


def hairpin_brake_gap(train_end_m: float, ctx: dict, hairpins: list[dict]) -> float | None:
    """m entre el final del tren de baches y el punto en el que el piloto IA empieza a frenar para la siguiente horquilla,
    llegando a la velocidad de la línea ideal (ctx["speed"]) del final del tren (negativo: los baches caen en la frenada,
    #696). None sin horquillas."""
    if not hairpins:
        return None
    total, step, n = ctx["total"], ctx["step"], len(ctx["pts"])
    nxt = min(hairpins, key=lambda h: (h["s_m"][0] - train_end_m) % total)
    v = float(ctx["speed"][_arc_index(train_end_m, step, n)])
    return (nxt["s_m"][0] - train_end_m) % total - ai_brake_m(v, nxt["radius_m"])


def bump_report(e: dict, ctx: dict, sampler, max_amplitude_m: float, hairpins: list[dict] | None = None) -> dict:
    out = {"id": e["id"], "pattern": e["pattern"], "amplitude_m": e["amplitude_m"],
           "wavelength_m": e["wavelength_m"], "max_curvature": round(_straight(ctx, e["s_m"]), 5)}
    gap = hairpin_brake_gap(e["train_s_m"][1], ctx, hairpins or [])
    out["hairpin_brake_gap_m"] = None if gap is None else round(gap, 1)
    measured = []
    for lateral in (-4.0, 0.0, 4.0):
        s, z = _along(ctx, sampler, e["train_s_m"], lateral)
        ok = ~np.isnan(z)
        trend = np.polyval(np.polyfit(s[ok], z[ok], 1), s[ok])
        rel = z[ok] - trend
        measured.append(float(np.percentile(rel, 97) - np.percentile(rel, 3)) / 2.0)
    out.update(measured_amplitude_m=round(min(measured), 3), measured_amplitude_max_m=round(max(measured), 3),
               within_suspension=bool(e["amplitude_m"] <= max_amplitude_m
                                      and max(measured) <= max_amplitude_m + LIMITS_TIERRA["amplitude_tol_m"]),
               wheel_fits=bool(e["valley_radius_m"] >= 0.504))
    return out


def dip_report(e: dict, ctx: dict, sampler) -> dict:
    s, z = _along(ctx, sampler, e["dip_s_m"])
    ends = np.nanmean(np.concatenate([z[:4], z[-4:]]))
    depth = float(ends - np.nanmin(z))
    k = _arc_index((e["dip_s_m"][0] + e["dip_s_m"][1]) / 2.0, ctx["step"], len(ctx["pts"]))
    v = float(ctx["speed_boost"][k])
    g = v * v * 2.0 * math.pi ** 2 * depth / e["length_m"] ** 2 / G
    return {"id": e["id"], "depth_m": e["depth_m"], "measured_depth_m": round(depth, 3),
            "v_boost_kmh": round(v * 3.6, 1), "curvature_g": round(g, 3), "surface": e.get("surface"),
            "max_curvature": round(_straight(ctx, e["s_m"]), 5)}


def banqueta_report(e: dict, ctx: dict, sampler) -> dict:
    s0, s1 = e["s_m"]
    total = ctx["total"]
    core = ((s0 + 0.3 * ((s1 - s0) % total)), (s1 - 0.3 * ((s1 - s0) % total)))
    idx = _span_indices(core, ctx["step"], len(ctx["pts"]))
    side = 1.0 if e["outside"] == "derecha" else -1.0
    pts, right, z, bank = ctx["pts"][idx], ctx["right"][idx], ctx["z"][idx], ctx["bank"][idx]
    half = ctx["half"][idx]
    platform = half + 3.0
    base = z - side * platform * np.tan(np.radians(bank))
    # La banqueta sigue al borde del tramo (rally_circuit._carve): su cresta se aparta lo que el tramo pasa de 14 m.
    peak = BERM_PEAK_M + (half - WIDTH_NORMAL_M / 2.0)
    lift = sampler.top(pts + side * peak[:, None] * right) - base
    return {"id": e["id"], "outside": e["outside"], "berm_rise_m": e["berm_rise_m"],
            "measured_rise_m": round(float(np.nanmedian(lift)), 3), "bank_deg": e["bank_deg"]}


def tierra_report(manifest: dict, ctx: dict, sampler) -> dict:
    max_amp = manifest["suspension"]["max_amplitude_m"]
    els = manifest["elements"]
    hairpins = [e for e in els if e["type"] == "horquilla"]
    return {"bumps": [bump_report(e, ctx, sampler, max_amp, hairpins) for e in els if e["type"] == "baches"],
            "dips": [dip_report(e, ctx, sampler) for e in els if e["type"] == "baden"],
            "banquetas": [banqueta_report(e, ctx, sampler) for e in els if e["type"] == "banqueta"],
            "suspension": manifest["suspension"]}


def tierra_verdict(r: dict) -> dict[str, bool]:
    L = LIMITS_TIERRA
    t = r["tierra"]
    jumps = r["jumps"]
    shaped = [j for j in jumps if j.get("gap_m") is not None]
    return {
        "jump_kinds": len(jumps) >= L["jumps"] and len({j.get("jump_kind") for j in jumps}) >= L["jump_kinds"]
        and all(j["ai_clears_gap"] and j["impact_ai_ms"] <= L["impact_ai_ms"] for j in shaped),
        "bumps": len(t["bumps"]) >= L["bumps"] and len({b["pattern"] for b in t["bumps"]}) >= 2 and all(
            b["max_curvature"] <= L["straight_k"] and b["within_suspension"] and b["wheel_fits"]
            and b["measured_amplitude_m"] >= L["bump_measured_min"] * b["amplitude_m"] for b in t["bumps"]),
        "bumps_braking": all(b.get("hairpin_brake_gap_m") is None or b["hairpin_brake_gap_m"] >= L["hairpin_brake_gap_m"]
                             for b in t["bumps"]),
        "dip": len(t["dips"]) >= 1 and all(
            abs(d["measured_depth_m"] - d["depth_m"]) <= L["dip_depth_tol_m"] and d["curvature_g"] <= L["dip_g"]
            and d["max_curvature"] <= L["straight_k"] for d in t["dips"]),
        "banqueta": len(t["banquetas"]) >= 1 and all(
            b["measured_rise_m"] >= L["berm_min_ratio"] * b["berm_rise_m"] and abs(b["bank_deg"]) >= L["banqueta_bank_deg"]
            for b in t["banquetas"]),
    }
