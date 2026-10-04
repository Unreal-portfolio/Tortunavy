"""Circuitos de Rally de tierra (#682): saltos de forma (doble, cresta y salto largo sobre hueco), baches (whoops y
tabla de lavar) acotados por la suspensión del buggy, badén con barro y banqueta en las horquillas; y, sobre la
variante YA GENERADA (Scripts/terrain_volumes/Variants/R02_circuito_tierra), los criterios medidos en la malla
(terrain_geo/rally_circuit_check.py y rally_circuit_check_dirt.py).

    uv run pytest Scripts/tests/test_terrain_rally_tierra.py

La variante se regenera con
`uv run --with pyfqmr --with matplotlib python Scripts/gen_terrain_rally_circuit.py --profile tierra`.
"""

from __future__ import annotations

import copy
import json
import math
import re
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from terrain_geo import rally_circuit as rc  # noqa: E402
from terrain_geo.build import VARIANTS  # noqa: E402
from terrain_geo.rally_circuit_check import LIMITS, load_report, verdict  # noqa: E402
from terrain_geo.rally_circuit_check_dirt import LIMITS_TIERRA  # noqa: E402
from terrain_geo.rally_circuit_dirt import (BUMP_PATTERNS, SUSPENSION, BumpDesign, berm_lift,  # noqa: E402
                                            design_dip, BERM_END_M, BERM_PEAK_M, BERM_RISE_M, DIP_MAX_G)
from terrain_geo.rally_circuit_elements import impact_ms  # noqa: E402
from terrain_geo.rally_circuit_jumps import AI_FACTOR, ShapedParams, design_shaped  # noqa: E402
from terrain_geo.rally_circuit_physics import G  # noqa: E402
from terrain_geo.rally_circuit_plan import JUMP_RESERVE_M, make_plan, separation_ok  # noqa: E402
from terrain_vol.layout import UU_PER_M  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = VARIANTS / rc.TIERRA_NAME
SEEDS = (rc.TIERRA_SEED, 1, 2, 3)
generated = pytest.mark.skipif(not (OUT / "manifest.json").exists(), reason=f"{rc.TIERRA_NAME} sin generar")


# ── Suspensión leída del C++ ─────────────────────────────────────────────────────
def test_la_suspension_es_la_del_buggy_en_el_cpp():
    """Los límites de los baches salen de UChaosVehicleWheel en ApplySharedWheelSetup (TN_BuggyWheel.cpp)."""
    src = (ROOT / "Source/Tortunabo/Private/Vehicles/TN_BuggyWheel.cpp").read_text(encoding="utf-8")

    def value(name: str) -> float:
        return float(re.search(rf"Wheel\.{name}\s*=\s*([0-9.]+)f", src).group(1))

    assert SUSPENSION.raise_m == pytest.approx(value("SuspensionMaxRaise") / 100.0)
    assert SUSPENSION.drop_m == pytest.approx(value("SuspensionMaxDrop") / 100.0)
    assert SUSPENSION.wheel_radius_m == pytest.approx(value("WheelRadius") / 100.0)


# ── Saltos de forma ──────────────────────────────────────────────────────────────
@pytest.mark.parametrize("kind,v", [("doble", 18.0), ("doble", 26.0), ("cresta", 21.0), ("cresta", 25.5),
                                    ("hueco", 27.0), ("hueco", 31.0)])
def test_salto_de_forma_aterriza_en_su_cara_a_la_velocidad_de_llegada_y_a_la_de_la_ia(kind, v):
    d = design_shaped(ShapedParams(kind, 11.0, 2.6), v, 1.3 * v, JUMP_RESERVE_M)
    zone = [x - d.lip_x for x in d.landing_zone]
    land, ai = d.fly(v), d.fly(AI_FACTOR * v)
    assert zone[0] <= land.x_land_m <= zone[1] and zone[0] <= ai.x_land_m <= zone[1]
    assert ai.x_land_m >= d.crest_x_m                       # la IA (0,9 · v) salva el hueco
    assert impact_ms(d, v) <= LIMITS["impact_ms"] and impact_ms(d, AI_FACTOR * v) <= LIMITS["impact_ms"]
    assert d.drop_m <= 5.0 + 1e-6 and d.length_m + 20.0 <= JUMP_RESERVE_M
    assert land.airtime_s >= 1.0                            # más vistoso que la mesa de #622 (0,8 s)


def test_hueco_mas_hondo_y_largo_que_la_doble():
    doble = design_shaped(ShapedParams("doble", 11.0, 2.6), 27.0, 35.0, JUMP_RESERVE_M)
    hueco = design_shaped(ShapedParams("hueco", 11.0, 2.6), 27.0, 35.0, JUMP_RESERVE_M)
    assert hueco.trough_z_m < 0.0 <= doble.trough_z_m + 1e-9
    assert hueco.crest_x_m > doble.crest_x_m


def test_caso_negativo_mas_lento_que_la_ia_cae_en_el_hueco():
    d = design_shaped(ShapedParams("hueco", 11.0, 2.6), 28.0, 36.0, JUMP_RESERVE_M)
    assert d.fly(0.6 * 28.0).x_land_m < d.crest_x_m


# ── Baches, badén y banqueta ─────────────────────────────────────────────────────
@pytest.mark.parametrize("pattern", sorted(BUMP_PATTERNS))
def test_baches_dentro_de_la_suspension(pattern):
    rng = np.random.default_rng(682)
    for _ in range(50):
        b = BumpDesign.draw(pattern, rng)
        assert b.within_limits()
        assert 2.0 * b.amplitude_m <= SUSPENSION.raise_m + SUSPENSION.drop_m
        x = np.arange(0.0, b.piece_m, 0.05)
        z = b.profile(x)
        assert z.min() >= 0.0 and z.max() == pytest.approx(2.0 * b.amplitude_m, rel=0.01)
        assert z[0] == 0.0 and abs(z[-1]) < 1e-9


def test_caso_negativo_bache_que_pasa_la_suspension():
    assert not BumpDesign("whoops", 0.3, 9.0, 6).within_limits()
    assert not BumpDesign("tabla_lavar", 0.08, 1.5, 12).within_limits()      # la rueda no cabe en el valle


def test_baden_no_despega_y_cabe():
    for v in (20.0, 28.0, 35.0):
        d = design_dip(0.6, v)
        assert d.curvature_g <= DIP_MAX_G + 1e-9 and d.length_m <= 50.0 and d.depth_m > 0.2


def test_banqueta_sube_por_fuera_y_vuelve_antes_de_las_barreras():
    assert berm_lift(np.array([0.0, 5.0, -10.0]))[0:3].max() == 0.0
    assert berm_lift(np.array([BERM_PEAK_M]))[0] == pytest.approx(BERM_RISE_M)
    assert berm_lift(np.array([BERM_END_M, 15.0]))[0:2].max() == pytest.approx(0.0, abs=1e-9)


# ── Trazado ──────────────────────────────────────────────────────────────────────
@pytest.mark.parametrize("seed", SEEDS)
def test_trazado_tierra_cerrado_y_con_las_piezas_por_reglas(seed):
    plan = make_plan(seed, "tierra")
    assert np.hypot(*(plan.pts[0] - plan.pts[-1])) <= 1.5 and separation_ok(plan.pts, plan.step_m)
    kinds = [p.kind for p in plan.pieces]
    jumps = [p.variant for p in plan.pieces if p.kind == "salto"]
    assert sorted(jumps) == ["cresta", "doble", "hueco", "mesa"]
    assert sorted(p.variant for p in plan.pieces if p.kind == "baches") == ["tabla_lavar", "whoops"]
    assert kinds.count("baden") == 1 and kinds.count("rasante") == 2
    assert all(p.variant == "banqueta" and abs(p.bank_deg) >= 10.0 for p in plan.pieces if p.kind == "horquilla")
    for i, p in enumerate(plan.pieces):
        if p.kind == "salto":                       # la doble y la cresta tras horquilla, la mesa tras la chicane
            prev = plan.pieces[i - 1].kind
            assert prev == {"doble": "horquilla", "cresta": "horquilla", "mesa": "chicane"}.get(p.variant, "curva_peraltada")


def test_el_perfil_dunas_no_cambia():
    """R01 (#622) se reproduce igual: el perfil tierra no toca los sorteos del de dunas."""
    plan = make_plan(rc.SEED)
    assert all(p.variant == "" for p in plan.pieces) and "baches" not in [p.kind for p in plan.pieces]


# ── Variante generada ────────────────────────────────────────────────────────────
@pytest.fixture(scope="module")
def manifest() -> dict:
    return json.loads((OUT / "manifest.json").read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def measured() -> tuple[dict, dict]:
    return load_report(OUT)


@generated
def test_registrada_y_con_lo_que_lee_tn_rally_track(manifest):
    index = json.loads((VARIANTS / "index.json").read_text(encoding="utf-8"))
    entry = next(e for e in index if e["name"] == rc.TIERRA_NAME)
    assert entry["mode"] == "rally" and entry["recorrible"] is True
    road = manifest["road_uu"]
    assert manifest["closed"] is True and manifest["laps"] >= 2 and manifest["generator"]["profile"] == "tierra"
    assert all(len(cp) == 4 for cp in manifest["checkpoints_uu"]) and math.dist(manifest["checkpoints_uu"][0][:3], road[0]) < 1.0
    assert len(manifest["bank_deg"]) == len(road) and manifest["suspension"]["max_amplitude_m"] == SUSPENSION.max_amplitude_m
    for name in ("preview.png", "lamina.png"):
        assert 10_000 < (OUT / name).stat().st_size < 1_000_000


@generated
def test_variante_reproducible_desde_la_semilla(manifest):
    track = rc.build_track(manifest["seed"], "tierra")
    road = track.plan.pts + rc.make_frame(track).shift
    got = np.asarray(manifest["road_uu"], dtype=np.float64)
    assert len(got) == len(road) and np.abs(got[:, :2] - road * UU_PER_M).max() < 0.1
    assert np.abs(got[:, 2] - track.z * UU_PER_M).max() < 0.1


@generated
def test_cuatro_saltos_de_varios_tipos_aterrizan_en_la_calzada(measured, manifest):
    report, checks = measured
    kinds = [j["jump_kind"] for j in report["jumps"]]
    assert len(kinds) >= 4 and len(set(kinds)) >= 2 and checks["jumps"] and checks["jump_kinds"]
    for j in report["jumps"]:
        assert j["speed_rel_err"] <= LIMITS["speed_rel"] and j["zone_m"][0] <= j["x_land_m"] <= j["zone_m"][1]
        assert j["landing_lateral_m"] <= j["landing_half_m"] - LIMITS["landing_margin_m"] and j["impact_ms"] <= LIMITS["impact_ms"]
        if j.get("gap_m") is not None:
            assert j["ai_clears_gap"] and j["impact_ai_ms"] <= LIMITS_TIERRA["impact_ai_ms"]


@generated
def test_baches_en_recta_y_dentro_de_la_suspension(measured):
    report, checks = measured
    bumps = report["tierra"]["bumps"]
    assert len(bumps) >= 2 and {b["pattern"] for b in bumps} == {"whoops", "tabla_lavar"} and checks["bumps"]
    for b in bumps:
        assert b["max_curvature"] <= LIMITS_TIERRA["straight_k"]
        assert 0.6 * b["amplitude_m"] <= b["measured_amplitude_m"] <= SUSPENSION.max_amplitude_m + 0.02


@generated
def test_baden_y_banqueta_medidos_en_la_malla(measured):
    report, checks = measured
    dip, berms = report["tierra"]["dips"], report["tierra"]["banquetas"]
    assert checks["dip"] and checks["banqueta"] and dip and berms
    assert all(d["surface"] == "barro" and d["curvature_g"] * G <= (DIP_MAX_G + 0.15) * G for d in dip)
    assert all(b["measured_rise_m"] >= 0.6 * b["berm_rise_m"] for b in berms)


@generated
def test_veredicto_completo_en_verde(measured, manifest):
    _, checks = measured
    assert all(checks.values()), {k: v for k, v in checks.items() if not v}
    assert manifest["checks"]["verdict"] == checks and manifest["recorrible"] is True


@generated
def test_el_veredicto_cae_con_cada_criterio_de_tierra_roto(measured, manifest):
    """Caso negativo: un bache por encima de la suspensión, un salto de un solo tipo, la IA que cae en el hueco y
    una banqueta sin caballón."""
    report, _ = measured
    edits = (("bumps", lambda r: r["tierra"]["bumps"][0].update(within_suspension=False)),
             ("jump_kinds", lambda r: [j.update(jump_kind="mesa") for j in r["jumps"]]),
             ("jump_kinds", lambda r: [j.update(ai_clears_gap=False) for j in r["jumps"] if j.get("gap_m") is not None]),
             ("banqueta", lambda r: r["tierra"]["banquetas"][0].update(measured_rise_m=0.0)),
             ("dip", lambda r: r["tierra"]["dips"][0].update(curvature_g=2.0)))
    for key, edit in edits:
        broken = copy.deepcopy(report)
        edit(broken)
        assert verdict(broken, manifest)[key] is False
