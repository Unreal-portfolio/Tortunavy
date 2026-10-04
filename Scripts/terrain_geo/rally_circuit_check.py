"""Validador de un circuito de Rally por vueltas (#622) sobre la variante YA ESCRITA (manifest y trozos TNTM2): mide
la cara superior de la malla con colisión (rally_corridor.MeshSampler), no el campo de alturas del generador.

    report = circuit_report(Path(".../Variants/R01_circuito_dunas"))
    checks = verdict(report)

Mide: cierre y continuidad del eje, escalones fuera de los saltos, pendiente sostenida, peralte (manifest y medido
en la malla), saltos (velocidad de llegada recalculada sobre la malla, ángulo de salida medido, vuelo y aterrizaje
dentro de la zona y de la calzada, también con turbo), rasantes (altura y radio vertical frente a la velocidad con
turbo), radio mínimo, separación entre tramos, parrilla 2 x 4 llana, suelo bajo las barreras de #303 y puertas.
"""

from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np
from scipy import ndimage

from terrain_vol.layout import UU_PER_M, WATER_M

from .rally_circuit_physics import G, boost_arrival, speed_profile, takeoff_flight
from .rally_corridor import MeshSampler, radii

LIMITS = {"wrap_gap_m": 1.5, "max_step_m": 0.4, "sustained_grade_deg": 8.0, "bank_deg": 15.0, "bank_mesh_deg": 15.5,
          "jumps": 3, "banked": 4, "crests": 2, "banked_min_deg": 6.0, "crest_min_m": 1.5, "min_radius_m": 15.0,
          "separation_m": 40.0, "grid_slope_deg": 3.0, "barrier_dz_m": 1.0, "impact_ms": 5.0, "speed_rel": 0.08,
          "checkpoint_gap_m": (120.0, 300.0), "landing_margin_m": 1.5}
SUSTAINED_M = 30
SEPARATION_ARC_M = 150.0
BANK_PROBE_M = 5.0
BARRIER_BASE_M = 15.0            # FBarrierParams: max(media calzada + ShoulderCm, MinOffsetCm)
BARRIER_RUNOFF_M = 8.0           # MaxRunoffCm por fuera de las curvas
BARRIER_CURVE_K = (1.0 / 250.0, 1.0 / 60.0)


def _smoothstep(e0: float, e1: float, x):
    t = np.clip((np.asarray(x) - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def axis_geometry(pts: np.ndarray, step: float) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Tangente, normal a la derecha y curvatura con signo (positiva a la derecha) de un lazo cerrado."""
    d = np.roll(pts, -1, axis=0) - np.roll(pts, 1, axis=0)
    t = d / np.maximum(np.hypot(*d.T), 1e-9)[:, None]
    right = np.column_stack([-t[:, 1], t[:, 0]])
    psi = np.arctan2(t[:, 1], t[:, 0])
    dpsi = (np.roll(psi, -1) - np.roll(psi, 1) + np.pi) % (2.0 * np.pi) - np.pi
    return t, right, ndimage.gaussian_filter1d(dpsi, 2.0, mode="wrap") / (2.0 * step)


def _in_spans(arc: np.ndarray, total: float, spans: list[tuple[float, float]]) -> np.ndarray:
    out = np.zeros(len(arc), dtype=bool)
    for s0, s1 in spans:
        s0, s1 = s0 % total, s1 % total
        out |= ((arc >= s0) & (arc <= s1)) if s0 <= s1 else ((arc >= s0) | (arc <= s1))
    return out


def _jump_report(e: dict, ctx: dict, sampler: MeshSampler) -> dict:
    pts, tan, step = ctx["pts"], ctx["tan"], ctx["step"]
    lip_k = int(round(e["lip_s_m"] / step)) % len(pts)
    u = tan[lip_k]
    x = np.arange(-10.0, 140.0, 0.25)
    line = sampler.top(pts[lip_k][None, :] + x[:, None] * u[None, :])
    line = np.where(np.isnan(line), -50.0, line)
    v = float(ctx["speed"][lip_k])
    v_boost = boost_arrival(ctx["speed"], ctx["curvature"], ctx["inward"], ctx["grade"], lip_k, step)

    def fly(speed: float):
        return takeoff_flight(x, line, speed)

    land, x_off, launch = fly(v)
    land_b, _, launch_b = fly(v_boost)
    zone = [e["landing_s_m"][0] - e["lip_s_m"], e["landing_s_m"][1] - e["lip_s_m"]]

    def lateral(fl) -> float:
        if fl is None:
            return math.inf
        p = pts[lip_k] + fl.x_land_m * u
        return float(np.hypot(*(pts - p).T).min())

    def impact(fl, speed: float, angle: float) -> float:
        if fl is None:
            return math.inf
        th = math.radians(angle)
        vx, vz = speed * math.cos(th), speed * math.sin(th) - G * fl.airtime_s
        g = math.radians(-fl.ground_deg)
        return max(0.0, -(vx * -math.sin(g) + vz * math.cos(g)))

    straight_end = e["straight_s_m"][1] - e["lip_s_m"]
    return {"id": e["id"], "lip_s_m": e["lip_s_m"], "launch_deg": round(launch, 2), "takeoff_x_m": round(x_off, 2), "design_lip_deg": e["lip_deg"],
            "v_ms": round(v, 2), "v_design_ms": e["v_design_ms"], "v_boost_ms": round(v_boost, 2),
            "speed_rel_err": round(abs(v - e["v_design_ms"]) / e["v_design_ms"], 3),
            "x_land_m": land.x_land_m if land else None, "x_land_boost_m": land_b.x_land_m if land_b else None,
            "zone_m": [round(zone[0], 2), round(zone[1], 2)], "airtime_s": land.airtime_s if land else None,
            "landing_lateral_m": round(lateral(land), 2), "boost_lateral_m": round(lateral(land_b), 2),
            "impact_ms": round(impact(land, v, launch), 2), "impact_boost_ms": round(impact(land_b, v_boost, launch_b), 2),
            "in_zone": bool(land and zone[0] <= land.x_land_m <= zone[1]),
            "boost_on_straight": bool(land_b and land_b.x_land_m <= straight_end - 5.0)}


def _crest_report(e: dict, ctx: dict) -> dict:
    z, step = ctx["z"], ctx["step"]
    k = int(round(e["crest_s_m"] / step)) % len(z)
    w = int(round(12.0 / step))
    idx = np.arange(k - w, k + w + 1) % len(z)
    a, _, _ = np.polyfit((idx - k) * step, z[idx], 2)
    radius = math.inf if a >= 0 else -1.0 / (2.0 * a)
    s0, s1 = e["s_m"]
    ends = z[[int(round(s0 / step)) % len(z), int(round(s1 / step)) % len(z)]]
    v = float(ctx["speed_boost"][k])
    return {"id": e["id"], "crest_s_m": e["crest_s_m"], "height_m": round(float(z[k] - ends.mean()), 2),
            "radius_m": round(radius, 1), "v_boost_ms": round(v, 2), "takeoff_radius_m": round(v * v / G, 1),
            "keeps_ground": bool(radius >= v * v / G)}


def _barrier_dz(ctx: dict, sampler: MeshSampler) -> float:
    """Mayor desnivel entre el suelo bajo cada barrera (como la coloca FBarrierParams) y el borde de su lado."""
    pts, right, k, z, bank = ctx["pts"], ctx["right"], ctx["curvature"], ctx["z"], ctx["bank"]
    half = ctx["road_w"] / 2.0
    worst = 0.0
    for side in (-1.0, 1.0):
        inside = np.sign(k) == side
        mag = np.abs(k)
        out = BARRIER_BASE_M + BARRIER_RUNOFF_M * _smoothstep(*BARRIER_CURVE_K, mag)
        inn = np.maximum(half + 1.5, np.minimum(BARRIER_BASE_M, 0.6 / np.maximum(mag, 1e-6)))
        off = np.where(inside & (mag > 1e-6), inn, out)
        probe = sampler.top(pts + side * off[:, None] * right)
        edge = z - side * (half + 3.0) * np.tan(np.radians(bank))
        worst = max(worst, float(np.nanmax(np.abs(probe - edge))))
    return worst


def circuit_report(variant_dir: Path, sampler: MeshSampler | None = None) -> dict:
    sampler = sampler or MeshSampler(variant_dir)
    m = sampler.manifest
    road = np.asarray(m["road_uu"], dtype=np.float64) / UU_PER_M
    pts = road[:, :2]
    seg = np.hypot(*(np.roll(pts, -1, axis=0) - pts).T)
    step = float(np.median(seg))
    arc = step * np.arange(len(pts))
    total = float(seg.sum())
    z = sampler.top(pts)
    zf = np.where(np.isnan(z), -1e3, z)
    tan, right, curvature = axis_geometry(pts, step)
    bank = np.asarray(m["bank_deg"], dtype=np.float64)
    inward = np.tan(np.radians(bank)) * np.sign(curvature)
    dz = (np.roll(zf, -1) - np.roll(zf, 1)) / (2.0 * step)
    grade = dz / np.sqrt(1.0 + dz * dz)
    jumps = [e for e in m["elements"] if e["type"] == "salto"]
    air = _in_spans(arc, total, [(e["lip_s_m"] + 0.5, e["land_design_s_m"]) for e in jumps])
    speed = speed_profile(curvature, inward, grade, air, step)
    speed_boost = speed_profile(curvature, inward, grade, air, step, boost=True)
    ctx = {"pts": pts, "tan": tan, "right": right, "curvature": curvature, "inward": inward, "grade": grade,
           "arc": arc, "step": step, "z": zf, "bank": bank, "speed": speed, "speed_boost": speed_boost,
           "road_w": m["road_width_m"]}
    exempt = _in_spans(arc, total, [tuple(e["s_m"]) for e in jumps])
    steps = np.abs(np.roll(zf, -1) - zf)
    win = int(round(SUSTAINED_M / step))
    sustained = np.abs(np.roll(zf, -win) - zf) / (win * step)
    window_exempt = np.array([exempt[(i + np.arange(win + 1)) % len(zf)].any() for i in range(len(zf))])
    left = sampler.top(pts - BANK_PROBE_M * right)
    rgt = sampler.top(pts + BANK_PROBE_M * right)
    bank_mesh = np.degrees(np.arctan((left - rgt) / (2.0 * BANK_PROBE_M)))
    banked = []
    for e in m["elements"]:
        if e["type"] != "curva_peraltada":
            continue
        s0, s1 = e["s_m"]
        core = _in_spans(arc, total, [(s0 + 0.25 * ((s1 - s0) % total), s1 - 0.25 * ((s1 - s0) % total))])
        measured = float(np.nanmedian(bank_mesh[core]))
        banked.append({"id": e["id"], "bank_deg": e["bank_deg"], "measured_deg": round(measured, 2),
                       "ok": bool(abs(measured - e["bank_signed_deg"]) <= 2.0 and abs(e["bank_deg"]) >= LIMITS["banked_min_deg"])})
    sub = pts[::2]
    n = len(sub)
    dist = np.hypot(sub[:, None, 0] - sub[None, :, 0], sub[:, None, 1] - sub[None, :, 1])
    gap = np.abs(np.arange(n)[:, None] - np.arange(n)[None, :]) * 2.0 * step
    gap = np.minimum(gap, total - gap)
    slots = np.asarray(m["markers_uu"]["parrilla"], dtype=np.float64) / UU_PER_M
    grid_slope = 0.0
    for p in slots:
        k = int(np.argmin(np.hypot(*(pts - p[:2]).T)))
        ring = sampler.top(p[None, :2] + np.array([[2, 0], [-2, 0], [0, 2], [0, -2]]) @ np.array([tan[k], right[k]]))
        grid_slope = max(grid_slope, math.degrees(math.atan(max(abs(ring[0] - ring[1]), abs(ring[2] - ring[3])) / 4.0)))
    cps = np.asarray(m["checkpoints_uu"], dtype=np.float64) / UU_PER_M
    cp_arcs = [arc[int(np.argmin(np.hypot(*(pts - c[:2]).T)))] for c in cps]
    cp_gaps = np.diff(cp_arcs + [total + cp_arcs[0]])
    return {"road_m": round(total, 1), "step_m": round(step, 3), "samples": len(pts),
            "wrap_gap_m": round(float(seg[-1]), 3), "max_seg_m": round(float(seg.max()), 3),
            "min_seg_m": round(float(seg.min()), 3), "missing_samples": int(np.isnan(z).sum()),
            "min_above_water_m": round(float(zf.min() - WATER_M), 2),
            "max_step_m": round(float(steps[~exempt].max()), 3), "max_step_in_jumps_m": round(float(steps[exempt].max()), 3),
            "sustained_grade_deg": round(math.degrees(math.atan(float(sustained[~window_exempt].max()))), 2),
            "bank_manifest_max_deg": round(float(np.abs(bank).max()), 2),
            "bank_mesh_max_deg": round(float(np.nanmax(np.abs(bank_mesh))), 2), "banked": banked,
            "jumps": [_jump_report(e, ctx, sampler) for e in jumps],
            "crests": [_crest_report(e, ctx) for e in m["elements"] if e["type"] == "rasante"],
            "min_radius_m": round(float(radii(np.vstack([pts[-8:], pts, pts[:8]])).min()), 1),
            "min_separation_m": round(float(dist[gap > SEPARATION_ARC_M].min()), 1),
            "grid_slope_deg": round(grid_slope, 2), "grid_slots": len(slots),
            "barrier_dz_m": round(_barrier_dz(ctx, sampler), 2),
            "checkpoints": {"count": len(cps), "first_at_start": bool(np.hypot(*(cps[0][:2] - pts[0])) < 1.0),
                            "gap_min_m": round(float(cp_gaps.min()), 1), "gap_max_m": round(float(cp_gaps.max()), 1),
                            "ordered": bool((np.diff(cp_arcs) > 0).all())},
            "lap_time_s": round(float((step / np.maximum(speed, 1.0)).sum()), 1)}


def verdict(r: dict, manifest: dict) -> dict[str, bool]:
    L = LIMITS
    cp = r["checkpoints"]
    return {
        "closed": manifest["closed"] is True and r["wrap_gap_m"] <= L["wrap_gap_m"] and r["max_seg_m"] <= L["wrap_gap_m"]
        and r["min_seg_m"] >= 0.5,
        "on_mesh": r["missing_samples"] == 0 and r["min_above_water_m"] > 1.0,
        "steps": r["max_step_m"] <= L["max_step_m"],
        "sustained_grade": r["sustained_grade_deg"] <= L["sustained_grade_deg"],
        "bank": r["bank_manifest_max_deg"] <= L["bank_deg"] and r["bank_mesh_max_deg"] <= L["bank_mesh_deg"],
        "banked_curves": sum(b["ok"] for b in r["banked"]) >= L["banked"],
        "jumps": len(r["jumps"]) >= L["jumps"] and all(
            j["in_zone"] and j["landing_lateral_m"] <= manifest["road_width_m"] / 2.0 - L["landing_margin_m"]
            and j["boost_on_straight"] and j["boost_lateral_m"] <= manifest["road_width_m"] / 2.0 - L["landing_margin_m"]
            and j["impact_ms"] <= L["impact_ms"] and j["speed_rel_err"] <= L["speed_rel"] for j in r["jumps"]),
        "crests": sum(c["height_m"] >= L["crest_min_m"] and c["keeps_ground"] for c in r["crests"]) >= L["crests"],
        "radius": r["min_radius_m"] >= L["min_radius_m"],
        "separation": r["min_separation_m"] >= L["separation_m"],
        "grid": r["grid_slots"] == 8 and r["grid_slope_deg"] <= L["grid_slope_deg"],
        "barrier_ground": r["barrier_dz_m"] <= L["barrier_dz_m"],
        "checkpoints": cp["first_at_start"] and cp["ordered"] and L["checkpoint_gap_m"][0] <= cp["gap_min_m"]
        and cp["gap_max_m"] <= L["checkpoint_gap_m"][1],
    }


def load_report(variant_dir: Path) -> tuple[dict, dict]:
    manifest = json.loads((variant_dir / "manifest.json").read_text(encoding="utf-8"))
    report = circuit_report(variant_dir, MeshSampler(variant_dir, manifest))
    return report, verdict(report, manifest)
