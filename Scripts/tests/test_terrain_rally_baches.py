"""Reglas del generador de circuitos de Rally para los baches (#696):

  - la malla no aplana la tabla de lavar: la calzada de los trenes de baches no pasa por el suavizado de Taubin
    (RallyCircuitModel.unsmoothed_mask, terrain_vol.mesh.build_chunk), que la dejaba al 31-60 % de su amplitud;
  - ninguna recta de baches va justo antes de una horquilla (rally_circuit_plan.bumps_before_hairpin), y el
    validador comprueba en cada variante que los baches quedan fuera de la frenada de las horquillas
    (rally_circuit_check_dirt.hairpin_brake_gap);
  - en R02 a R06 YA GENERADOS, cada tren de baches medido en la malla conserva al menos el 60 % de su amplitud y
    acaba antes de la frenada de la siguiente horquilla;
  - baches de aviso: si el tema los activa (rally_circuit_themes, `warning_bumps`), cada horquilla lleva en su frenada
    dos o tres ondas bajas (rally_circuit.place_warnings) sin cambiar el trazado ni el resto del lazo, y en R05 y R06
    YA GENERADOS el validador los mide en la malla y dentro de la frenada del piloto IA (`warning_bumps`).

    uv run pytest Scripts/tests/test_terrain_rally_baches.py
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
from terrain_geo.heightfield import HeightfieldModel  # noqa: E402
from terrain_geo.layout import RASTER_PX_M  # noqa: E402
from terrain_geo.rally_circuit_check import load_report, verdict  # noqa: E402
from terrain_geo import rally_circuit_plan as plan_module  # noqa: E402
from terrain_geo.rally_circuit_check_dirt import (AI_BRAKE_G, AI_HAIRPIN_G, AI_MAX_KMH, AI_MIN_KMH,  # noqa: E402
                                                  LIMITS_TIERRA, ai_brake_m, hairpin_brake_gap)
from terrain_geo.rally_circuit_dirt import (BUMP_LEAD_M, BUMP_PATTERNS, WARNING_CLEAR_M, WARNING_COUNT,  # noqa: E402
                                           WARNING_END_M, WARNING_WAVELENGTH_M, BumpDesign)
from terrain_geo.rally_circuit_physics import G  # noqa: E402
from terrain_geo.rally_circuit_plan import Piece, bumps_before_hairpin, make_plan  # noqa: E402
from terrain_geo.rally_circuit_themes import CIRCUITS, THEMES  # noqa: E402
from terrain_geo.rally_corridor import MeshSampler  # noqa: E402
from terrain_vol.layout import CELL_M, MAP_MIN_M, UU_PER_M  # noqa: E402
from terrain_vol.mesh import build_chunk  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
TIERRA = {rc.TIERRA_NAME: rc.TIERRA_SEED, **{name: c.seed for name, c in CIRCUITS.items()}}
# 6938 (whoops justo antes de la primera horquilla, #692), 6923 y 7001 ponían baches justo antes de una horquilla.
SEEDS_THAT_DID = (6938, 6923, 7001)
MIN_RATIO = LIMITS_TIERRA["bump_measured_min"]


# ── La malla conserva la tabla de lavar ──────────────────────────────────────────
def _washboard_model(wavelength_m: float, amplitude_m: float, pinned: bool) -> HeightfieldModel:
    """Un trozo (0, 0) llano con ondas de tabla de lavar a lo largo de X, la peor dirección para el suavizado."""
    n = int(CELL_M / RASTER_PX_M)
    c = MAP_MIN_M + (np.arange(n) + 0.5) * RASTER_PX_M
    X, _ = np.meshgrid(c, c, indexing="ij")
    model = HeightfieldModel(5.0 + amplitude_m * (1.0 - np.cos(2.0 * math.pi * X / wavelength_m)))
    if pinned:
        model.unsmoothed_mask = lambda x, y: np.ones(len(x), dtype=bool)
    return model


def _mesh_amplitude(model: HeightfieldModel) -> float:
    """Media amplitud medida como el validador (percentiles 97 y 3) en tres líneas a lo largo de las ondas."""
    chunk = build_chunk(model, 0, 0)
    sampler = MeshSampler.__new__(MeshSampler)
    v = chunk.vertices.astype(np.float64) / UU_PER_M
    sampler.cells = [(0.0, 0.0, v[chunk.triangles.astype(np.int64)])]
    t = np.arange(-30.0, 30.0, 0.25)
    out = []
    for lateral in (-4.0, 0.0, 4.0):
        z = sampler.top(np.column_stack([t, np.full(len(t), lateral)]))
        out.append(float(np.nanpercentile(z, 97) - np.nanpercentile(z, 3)) / 2.0)
    return min(out)


def test_la_malla_sin_suavizar_conserva_la_tabla_de_lavar_mas_corta():
    amplitude = BUMP_PATTERNS["tabla_lavar"]["amplitude_m"][0]
    wavelength = BUMP_PATTERNS["tabla_lavar"]["wavelength_m"][0]
    kept = _mesh_amplitude(_washboard_model(wavelength, amplitude, pinned=True))
    assert kept >= 0.75 * amplitude


def test_caso_negativo_con_el_suavizado_la_tabla_de_lavar_se_aplana():
    amplitude = BUMP_PATTERNS["tabla_lavar"]["amplitude_m"][0]
    wavelength = BUMP_PATTERNS["tabla_lavar"]["wavelength_m"][0]
    assert _mesh_amplitude(_washboard_model(wavelength, amplitude, pinned=False)) < MIN_RATIO * amplitude


def test_el_modelo_del_circuito_no_suaviza_sus_trenes_de_baches():
    track = rc.build_track(rc.TIERRA_SEED, "tierra")
    model = rc.RallyCircuitModel(track, rc.TIERRA_SEED)
    assert len(track.bumps) == 2
    for b in track.bumps:
        s = np.linspace(b.s0 + BUMP_LEAD_M, b.s0 + BUMP_LEAD_M + b.design.train_m, 25)
        axis = model.road[[track.index(v) for v in s]]
        assert model.unsmoothed_mask(axis[:, 0], axis[:, 1]).all()
    far = model.road[[track.index(v) for v in (0.0, 40.0)]]      # parrilla y salida: se suavizan
    assert not model.unsmoothed_mask(far[:, 0], far[:, 1]).any()


# ── Baches fuera de la frenada de las horquillas ─────────────────────────────────
def test_bumps_before_hairpin_detecta_la_pareja():
    pieces = [Piece("recta"), Piece("curva_peraltada"), Piece("baches"), Piece("horquilla"), Piece("salto")]
    assert bumps_before_hairpin(pieces)
    assert not bumps_before_hairpin([Piece("recta"), Piece("baches"), Piece("curva_peraltada"), Piece("horquilla")])
    assert bumps_before_hairpin([Piece("horquilla"), Piece("recta"), Piece("baches")])    # el lazo vuelve al principio


@pytest.mark.parametrize("seed", (*TIERRA.values(), *SEEDS_THAT_DID, 1, 2, 3))
def test_el_trazado_nunca_pone_baches_justo_antes_de_una_horquilla(seed):
    plan = make_plan(seed, "tierra")
    assert not bumps_before_hairpin(plan.pieces)
    assert sorted(p.variant for p in plan.pieces if p.kind == "baches") == ["tabla_lavar", "whoops"]


def test_los_numeros_del_piloto_ia_son_los_del_cpp():
    """La frenada del validador es la del piloto IA (ATN_RallyAIController.h y FBrakeTuning de TN_RallyCircuit.h)."""
    ai = (ROOT / "Source/Tortunabo/Public/Rally/TN_RallyAIController.h").read_text(encoding="utf-8")
    circuit = (ROOT / "Source/Tortunabo/Public/Rally/TN_RallyCircuit.h").read_text(encoding="utf-8")

    def value(src: str, name: str) -> float:
        return float(re.search(rf"\b{name}\s*=\s*([0-9.]+)f", src).group(1))

    assert AI_BRAKE_G == pytest.approx(value(ai, "BrakeDecelG"))
    assert AI_HAIRPIN_G == pytest.approx(value(ai, "HairpinLateralG"))
    assert AI_MAX_KMH == pytest.approx(value(ai, "MaxSpeedKmh"))
    assert AI_MIN_KMH == pytest.approx(value(circuit, "MinKmh"))


def test_hairpin_brake_gap_mide_hasta_donde_frena_el_piloto_ia():
    """A 25 m/s hacia una horquilla de 20 m de radio que empieza en el arco 500: la IA frena unos 52 m."""
    n = 1000
    ctx = {"speed": np.full(n, 25.0), "step": 1.0, "pts": np.zeros((n, 2)), "total": float(n)}
    hairpin = [{"s_m": [500.0, 540.0], "radius_m": 20.0}]
    brake = ai_brake_m(25.0, 20.0)
    assert brake == pytest.approx((25.0 ** 2 - AI_HAIRPIN_G * G * 20.0) / (2.0 * AI_BRAKE_G * G))
    assert hairpin_brake_gap(400.0, ctx, hairpin) == pytest.approx(100.0 - brake)
    assert hairpin_brake_gap(470.0, ctx, hairpin) < 0.0           # el tren acaba dentro de la frenada
    assert hairpin_brake_gap(600.0, ctx, hairpin) > 800.0         # la siguiente es la de la vuelta siguiente
    assert hairpin_brake_gap(470.0, ctx, []) is None


@pytest.mark.parametrize("seed", SEEDS_THAT_DID)
def test_caso_negativo_sin_la_regla_los_whoops_caen_en_la_frenada(seed, monkeypatch):
    """Sin bumps_before_hairpin, esas semillas ponen los whoops justo antes de una horquilla, dentro de la frenada."""
    monkeypatch.setattr(plan_module, "bumps_before_hairpin", lambda pieces: False)
    track = rc.build_track(seed, "tierra")
    hairpins = [{"s_m": [s0, s1], "radius_m": track.plan.pieces[i].radius_m}
                for i, s0, s1 in track.plan.spans if track.plan.pieces[i].kind == "horquilla"]
    ctx = {"speed": track.speed, "step": track.plan.step_m, "pts": track.plan.pts, "total": track.total}
    gaps = [hairpin_brake_gap(b.s0 + BUMP_LEAD_M + b.design.train_m, ctx, hairpins) for b in track.bumps]
    assert min(gaps) < 0.0


# ── Variantes generadas ──────────────────────────────────────────────────────────
@pytest.fixture(scope="module")
def reports() -> dict:
    return {n: load_report(VARIANTS / n) for n in TIERRA if (VARIANTS / n / "manifest.json").exists()}


@pytest.mark.parametrize("name", sorted(TIERRA))
def test_baches_conservados_y_fuera_de_la_frenada(name, reports):
    if name not in reports:
        pytest.skip(f"{name} sin generar")
    report, checks = reports[name]
    bumps = report["tierra"]["bumps"]
    assert {b["pattern"] for b in bumps} == {"whoops", "tabla_lavar"}
    for b in bumps:
        assert b["measured_amplitude_m"] >= MIN_RATIO * b["amplitude_m"], b
        assert b["hairpin_brake_gap_m"] is not None and b["hairpin_brake_gap_m"] >= LIMITS_TIERRA["hairpin_brake_gap_m"], b
    assert checks["bumps"] and checks["bumps_braking"]


@pytest.mark.parametrize("name", sorted(TIERRA))
def test_caso_negativo_el_veredicto_cae_con_baches_en_la_frenada(name, reports):
    if name not in reports:
        pytest.skip(f"{name} sin generar")
    report, _ = reports[name]
    manifest = json.loads((VARIANTS / name / "manifest.json").read_text(encoding="utf-8"))
    broken = copy.deepcopy(report)
    broken["tierra"]["bumps"][0]["hairpin_brake_gap_m"] = -5.0
    assert verdict(broken, manifest)["bumps_braking"] is False
    flat = copy.deepcopy(report)
    tabla = next(b for b in flat["tierra"]["bumps"] if b["pattern"] == "tabla_lavar")
    tabla["measured_amplitude_m"] = 0.55 * tabla["amplitude_m"]
    assert verdict(flat, manifest)["bumps"] is False


# ── Baches de aviso en la frenada de las horquillas ──────────────────────────────
WARNED = sorted(n for n, c in CIRCUITS.items() if THEMES[c.theme].warning_bumps)


def test_al_menos_un_circuito_de_r03_a_r06_activa_los_baches_de_aviso_y_el_tema_base_no():
    assert WARNED
    assert not THEMES["base"].warning_bumps


def test_draw_warning_mete_tres_ondas_si_caben_dos_si_no_y_ninguna_sin_sitio():
    rng = np.random.default_rng(1)
    roomy = BumpDesign.draw_warning(rng, 40.0)
    assert roomy.count == WARNING_COUNT[1] and roomy.within_limits()
    tight = BumpDesign.draw_warning(rng, 2.0 * WARNING_WAVELENGTH_M[0] + 0.5)
    assert tight.count == WARNING_COUNT[0] and tight.train_m <= 2.0 * WARNING_WAVELENGTH_M[0] + 0.5
    assert BumpDesign.draw_warning(rng, 2.0 * WARNING_WAVELENGTH_M[0] - 0.5) is None


@pytest.mark.parametrize("name", WARNED)
def test_cada_horquilla_lleva_sus_baches_de_aviso_en_el_enlace_previo(name):
    c = CIRCUITS[name]
    track = rc.build_track(c.seed, c.profile, warning_bumps=True)
    spans = track.plan.spans
    hairpins = {p: (i, s0) for i, (p, s0, _) in enumerate(spans) if track.plan.pieces[p].kind == "horquilla"}
    assert sorted(w.piece for w in track.warnings) == sorted(hairpins)
    for w in track.warnings:
        i, s0 = hairpins[w.piece]
        start, end = w.s0 + BUMP_LEAD_M, w.s0 + BUMP_LEAD_M + w.design.train_m
        assert (s0 - end) % track.total == pytest.approx(WARNING_END_M)
        assert (start - spans[i - 1][2]) % track.total >= WARNING_CLEAR_M - 1e-6
        assert w.design.within_limits()


@pytest.mark.parametrize("name", WARNED)
def test_los_baches_de_aviso_no_cambian_el_resto_del_lazo(name):
    c = CIRCUITS[name]
    with_warnings = rc.build_track(c.seed, c.profile, warning_bumps=True)
    without = rc.build_track(c.seed, c.profile)
    inside = np.zeros(len(with_warnings.arc), dtype=bool)
    for w in with_warnings.warnings:
        inside |= rc.cyclic_mask(with_warnings.arc, with_warnings.total, w.s0, w.s0 + w.design.piece_m)
    assert np.abs(with_warnings.z - without.z)[~inside].max() < 1e-6
    assert np.abs(with_warnings.z - without.z)[inside].max() > 0.05


@pytest.mark.parametrize("name", WARNED)
def test_baches_de_aviso_medidos_en_la_malla_y_dentro_de_la_frenada(name, reports):
    if name not in reports:
        pytest.skip(f"{name} sin generar")
    report, checks = reports[name]
    t = report["tierra"]
    assert t["warnings_on"] and len(t["warnings"]) == t["hairpins"] == 2
    for w in t["warnings"]:
        assert w["braking_overlap_m"] >= w["wavelength_m"], w
        assert w["measured_amplitude_m"] >= MIN_RATIO * w["amplitude_m"], w
        assert w["within_suspension"], w
    assert checks["warning_bumps"]


@pytest.mark.parametrize("name", WARNED)
def test_caso_negativo_el_veredicto_cae_sin_baches_de_aviso_o_fuera_de_la_frenada(name, reports):
    if name not in reports:
        pytest.skip(f"{name} sin generar")
    report, _ = reports[name]
    manifest = json.loads((VARIANTS / name / "manifest.json").read_text(encoding="utf-8"))
    missing = copy.deepcopy(report)
    missing["tierra"]["warnings"].pop()
    assert verdict(missing, manifest)["warning_bumps"] is False
    early = copy.deepcopy(report)
    early["tierra"]["warnings"][0]["braking_overlap_m"] = -10.0
    assert verdict(early, manifest)["warning_bumps"] is False


@pytest.mark.parametrize("name", sorted(set(TIERRA) - set(WARNED)))
def test_sin_el_tema_no_hay_baches_de_aviso(name, reports):
    if name not in reports:
        pytest.skip(f"{name} sin generar")
    report, checks = reports[name]
    assert not report["tierra"]["warnings"] and checks["warning_bumps"]
