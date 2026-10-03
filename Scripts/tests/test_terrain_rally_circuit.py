"""Circuitos de Rally por vueltas (#622): física de dimensionado, trazado cerrado con semilla, elementos y, sobre la
variante YA GENERADA (Scripts/terrain_volumes/Variants/R01_circuito_dunas), los criterios de aceptación medidos en
la malla (terrain_geo/rally_circuit_check.py).

    uv run pytest Scripts/tests/test_terrain_rally_circuit.py

La variante se regenera con `uv run --with pyfqmr --with matplotlib python Scripts/gen_terrain_rally_circuit.py`.
"""

from __future__ import annotations

import copy
import json
import math
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from terrain_geo import rally_circuit as rc  # noqa: E402
from terrain_geo.build import VARIANTS  # noqa: E402
from terrain_geo.rally_circuit_check import LIMITS, load_report, verdict  # noqa: E402
from terrain_geo.rally_circuit_elements import (MAX_BANK_DEG, CrestDesign, JumpParams, bank_profile,  # noqa: E402
                                                design_crest, design_jump, impact_ms)
from terrain_geo.rally_circuit_physics import (BUGGY, G, boost_arrival, curve_speed, flight,  # noqa: E402
                                               speed_profile, takeoff_flight)
from terrain_geo.rally_circuit_plan import JUMP_RESERVE_M, make_plan, separation_ok  # noqa: E402
from terrain_vol.layout import UU_PER_M  # noqa: E402

OUT = VARIANTS / rc.NAME
SEEDS = (rc.SEED, 1, 2, 3, 4)
generated = pytest.mark.skipif(not (OUT / "manifest.json").exists(), reason=f"{rc.NAME} sin generar")


# ── Física ───────────────────────────────────────────────────────────────────────
def test_vuelo_en_llano_como_la_formula():
    x = np.arange(0.0, 80.0, 0.05)
    for v, deg in ((18.0, 12.0), (25.0, 9.0), (30.0, 6.0)):
        land = flight(x, np.zeros_like(x), v, deg)
        assert land.x_land_m == pytest.approx(v * v * math.sin(math.radians(2 * deg)) / G, abs=0.1)
        assert land.apex_m == pytest.approx((v * math.sin(math.radians(deg))) ** 2 / (2 * G), abs=0.01)


def test_perfil_de_velocidad_punta_curva_y_frenada():
    n = 1000
    k = np.zeros(n)
    k[600:700] = 1.0 / 25.0                                     # curva de 25 m sin peralte
    v = speed_profile(k, np.zeros(n), np.zeros(n), np.zeros(n, dtype=bool), closed=False)
    assert v[0] == 0.0 and v[550] == pytest.approx(BUGGY.top_speed_ms, rel=0.02)
    assert v[650] == pytest.approx(math.sqrt(G * 25.0 * BUGGY.lateral_grip), rel=1e-3)
    assert (np.diff(v[400:600]) <= 1e-9).any()                  # frena antes de la curva


def test_el_peralte_sube_la_velocidad_de_curva_y_el_turbo_la_de_llegada():
    flat, banked = curve_speed(np.array([1 / 40.0]), np.array([0.0]))[0], curve_speed(np.array([1 / 40.0]), np.array([math.tan(math.radians(15))]))[0]
    assert banked > flat * 1.2
    n = 600
    zeros = np.zeros(n)
    v = speed_profile(zeros, zeros, zeros, np.zeros(n, dtype=bool), closed=False)
    assert boost_arrival(v, zeros, zeros, zeros, 120) > v[120] + 3.0


def test_despegue_en_el_labio_de_un_perfil_medido():
    x = np.arange(-10.0, 60.0, 0.25)
    z = np.where(x < 0.0, x * math.tan(math.radians(10.0)), 0.0)
    land, x0, angle = takeoff_flight(x, z, 24.0)
    assert -1.0 <= x0 <= 0.5 and angle == pytest.approx(10.0, abs=0.6)
    assert land.x_land_m == pytest.approx(24.0 ** 2 * math.sin(math.radians(20.0)) / G, rel=0.08)


# ── Elementos ────────────────────────────────────────────────────────────────────
@pytest.mark.parametrize("v", [18.0, 21.0, 24.0, 27.0])
def test_salto_aterriza_en_su_zona_a_la_velocidad_de_llegada(v):
    params = JumpParams(0.8, 3.2, 16.0, 0.85)
    d = design_jump(params, v, v * 1.3, JUMP_RESERVE_M)
    land = d.fly(v)
    zone = [x - d.lip_x for x in d.landing_zone]
    assert zone[0] <= land.x_land_m <= zone[1]
    assert d.fly(0.6 * v).x_land_m <= d.table_m                 # más lento cae en la mesa
    assert impact_ms(d, v) <= LIMITS["impact_ms"]
    assert d.lip_deg <= 14.0 and math.tan(math.radians(d.landing_deg)) <= LIMITS["max_step_m"]
    assert d.length_m + 20.0 <= JUMP_RESERVE_M


def test_rasante_no_despega_ni_con_turbo():
    c = design_crest(80.0, 3.5, 35.0)
    assert c.radius_m >= 35.0 ** 2 / G and 0.0 < c.height_m <= 3.5
    assert CrestDesign(80.0, 6.0, 35.0).radius_m < 35.0 ** 2 / G      # caso negativo: el de 6 m despegaría


def test_peralte_acotado_y_con_rampas():
    arc = np.arange(0.0, 500.0)
    bank = bank_profile(arc, 500.0, [(100.0, 200.0, 18.0), (300.0, 350.0, -12.0)])
    assert np.abs(bank).max() <= MAX_BANK_DEG
    assert bank[150] == MAX_BANK_DEG and bank[320] == -12.0 and bank[50] == 0.0
    assert 0.0 < bank[95] < MAX_BANK_DEG                        # rampa de entrada


# ── Trazado ──────────────────────────────────────────────────────────────────────
@pytest.mark.parametrize("seed", SEEDS)
def test_trazado_cerrado_sin_cruces_y_con_las_piezas(seed):
    plan = make_plan(seed)
    assert np.hypot(*(plan.pts[0] - plan.pts[-1])) <= 1.5
    assert abs(abs(plan.curvature.sum() * plan.step_m) - 2 * math.pi) < 1e-6
    assert separation_ok(plan.pts, plan.step_m)
    kinds = [p.kind for p in plan.pieces]
    assert kinds[0] == "recta" and kinds.count("salto") >= 3 and kinds.count("rasante") >= 2
    assert kinds.count("curva_peraltada") >= 4 and kinds.count("horquilla") == 2 and kinds.count("chicane") == 1
    for i, kind in enumerate(kinds):                            # regla: cada salto sale de una pieza lenta
        if kind == "salto":
            assert kinds[i - 1] in ("horquilla", "chicane")


def test_misma_semilla_mismo_trazado():
    a, b = make_plan(rc.SEED), make_plan(rc.SEED)
    assert np.array_equal(a.pts, b.pts) and a.attempt == b.attempt


# ── Variante generada ────────────────────────────────────────────────────────────
@pytest.fixture(scope="module")
def manifest() -> dict:
    return json.loads((OUT / "manifest.json").read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def measured() -> tuple[dict, dict]:
    return load_report(OUT)


@generated
def test_registrada_con_vistas_y_lamina(manifest):
    index = json.loads((VARIANTS / "index.json").read_text(encoding="utf-8"))
    entry = next(e for e in index if e["name"] == rc.NAME)
    assert entry["mode"] == "rally" and entry["recorrible"] is True
    for name in ("preview.png", "lamina.png"):
        assert 10_000 < (OUT / name).stat().st_size < 1_000_000
    assert manifest["format"] == "TNTM2" and all((OUT / c["file"]).exists() for c in manifest["cells"])
    assert sum((OUT / c["file"]).stat().st_size for c in manifest["cells"]) < 5_000_000


@generated
def test_manifest_trae_lo_que_lee_tn_rally_track(manifest):
    road = manifest["road_uu"]
    assert manifest["mode"] == "rally" and manifest["closed"] is True and manifest["laps"] >= 2
    assert manifest["road_width_m"] == rc.ROAD_W_M and isinstance(manifest["start_yaw"], float)
    assert manifest["start_uu"] == manifest["end_uu"] and math.dist(manifest["start_uu"], road[0]) < 1.0
    assert all(len(cp) == 4 for cp in manifest["checkpoints_uu"])
    assert math.dist(manifest["checkpoints_uu"][0][:3], road[0]) < 1.0
    assert len(manifest["bank_deg"]) == len(road) and manifest["kill_boxes_uu"]
    assert len(manifest["markers_uu"]["parrilla"]) == 8


@generated
def test_variante_reproducible_desde_la_semilla(manifest):
    track = rc.build_track(manifest["seed"])
    road = track.plan.pts + rc.make_frame(track).shift
    got = np.asarray(manifest["road_uu"], dtype=np.float64)
    assert len(got) == len(road)
    assert np.abs(got[:, :2] - road * UU_PER_M).max() < 0.1
    assert np.abs(got[:, 2] - track.z * UU_PER_M).max() < 0.1
    assert np.abs(np.asarray(manifest["bank_deg"]) - track.bank_deg).max() < 0.01


@generated
def test_circuito_cerrado_y_continuo(measured, manifest):
    report, checks = measured
    assert checks["closed"] and checks["on_mesh"]
    assert report["wrap_gap_m"] <= 1.5 and 0.5 <= report["min_seg_m"] <= report["max_seg_m"] <= 1.5


@generated
def test_saltos_aterrizan_en_la_calzada_a_la_velocidad_calculada(measured, manifest):
    report, checks = measured
    half = manifest["road_width_m"] / 2.0
    assert len(report["jumps"]) >= 3 and checks["jumps"]
    for j in report["jumps"]:
        assert j["speed_rel_err"] <= LIMITS["speed_rel"]       # la velocidad del manifest es la de la malla
        assert j["zone_m"][0] <= j["x_land_m"] <= j["zone_m"][1]
        assert j["landing_lateral_m"] <= half - LIMITS["landing_margin_m"]
        assert j["boost_on_straight"] and j["boost_lateral_m"] <= half - LIMITS["landing_margin_m"]


@generated
def test_peraltes_y_curvas_peraltadas(measured):
    report, checks = measured
    assert report["bank_manifest_max_deg"] <= 15.0 and report["bank_mesh_max_deg"] <= 15.5
    assert sum(b["ok"] for b in report["banked"]) >= 4 and checks["banked_curves"]


@generated
def test_sin_escalones_fuera_de_los_elementos_y_rasantes(measured):
    report, checks = measured
    assert report["max_step_m"] <= LIMITS["max_step_m"] and checks["sustained_grade"]
    assert sum(c["height_m"] >= LIMITS["crest_min_m"] and c["keeps_ground"] for c in report["crests"]) >= 2


@generated
def test_parrilla_barreras_puertas_y_veredicto_completo(measured, manifest):
    _, checks = measured
    assert checks["grid"] and checks["barrier_ground"] and checks["checkpoints"] and checks["separation"]
    assert all(checks.values()), {k: v for k, v in checks.items() if not v}
    assert manifest["checks"]["verdict"] == checks and manifest["recorrible"] is True


@generated
def test_el_veredicto_cae_con_cada_criterio_roto(measured, manifest):
    """Caso negativo: el veredicto detecta un peralte de más, un salto fuera de su zona, un escalón y un lazo
    abierto."""
    report, _ = measured
    for key, edit in (("bank", lambda r: r.update(bank_manifest_max_deg=16.0)),
                      ("jumps", lambda r: r["jumps"][0].update(in_zone=False)),
                      ("steps", lambda r: r.update(max_step_m=0.6)),
                      ("closed", lambda r: r.update(wrap_gap_m=8.0))):
        broken = copy.deepcopy(report)
        edit(broken)
        assert verdict(broken, manifest)[key] is False
