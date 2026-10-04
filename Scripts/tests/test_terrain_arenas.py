"""Arenas nuevas de Todos contra Todos del lote 1 (terrain_shapes/lots_arenas.py): N01 coliseo, N02 anfiteatro,
N03 volcan-arena, N04 atolon, N17 fortaleza en estrella y N18 yin-yang.

    uv run --with pytest --with numpy --with scipy --with pillow --with scikit-image --with matplotlib \
        python -m pytest Scripts/tests/test_terrain_arenas.py
"""

from __future__ import annotations

import math
import sys
from dataclasses import replace
from pathlib import Path

import numpy as np
import pytest
from scipy import ndimage

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from terrain_shapes import lots_arenas as la  # noqa: E402
from terrain_shapes.kit_writer import check_clearance, write_kit_map  # noqa: E402
from terrain_vol.layout import WATER_M  # noqa: E402

TCT_TRIANGLES = 350_000
RING_DEG = np.arange(0.0, 360.0, 5.0)


def above_water(shape, e, n) -> np.ndarray:
    """Cota del campo de alturas (sin puentes) sobre el agua en los puntos de diseño (e, n)."""
    e, n = np.atleast_1d(np.asarray(e, dtype=np.float64)), np.atleast_1d(np.asarray(n, dtype=np.float64))
    X, Y = shape.canvas.to_world(e, n)
    return shape.model.ground_height(np.asarray(X), np.asarray(Y)) - WATER_M


def at(shape, p) -> float:
    return float(above_water(shape, p[0], p[1])[0])


def ring(shape, r: float, c=(0.0, 0.0), degs=RING_DEG) -> np.ndarray:
    a = np.radians(degs)
    return above_water(shape, c[0] + r * np.cos(a), c[1] + r * np.sin(a))


@pytest.mark.parametrize("key", sorted(la.MAPS))
def test_every_arena_is_valid_on_the_mesh(key, tmp_path):
    shape, extras = la.MAPS[key][1](None)
    r = write_kit_map(shape, extras, sheet=False, variants=tmp_path)
    assert r["ok"], r["checks"]
    assert r["budget"]["triangles"] <= TCT_TRIANGLES and r["seams"]["ok"]
    assert not r["unreachable_islands"] and not r["required_names_missed"] and not r["corridors_failed"]
    assert r["checks"]["nests"]["found"] == 8
    assert r["checks"]["cotas"]["max_m"] <= 15.0 and r["checks"]["rampas"]["max_deg"] <= 18.0
    assert r["top_max"] - WATER_M <= 15.0
    assert all(hole["ok"] for hole in r["checks"].get("clearances", []))


@pytest.mark.parametrize("key", sorted(la.MAPS))
def test_the_seed_changes_the_map_and_the_same_seed_repeats_it(key):
    build = la.MAPS[key][1]
    base, again, other = build(None)[0], build(None)[0], build(7)[0]
    assert np.array_equal(base.model.height, again.model.height)
    assert not np.array_equal(base.model.height, other.model.height) and other.seed == 7


# ── N01 Coliseo ──────────────────────────────────────────────────────────────────
def test_colosseum_has_four_gates_and_the_moat_surrounds_the_arena():
    shape, extras = la.build_colosseum()
    spec = la.colosseum_n01()
    assert len(shape.model.voids) == 4 and len(shape.model.bridges) == 4 and len(extras.clearances) == 4
    assert (ring(shape, spec.arena_r + spec.moat_m / 2.0) < 0.0).all()                      # foso sin huecos
    assert at(shape, (0.0, 0.0)) == pytest.approx(spec.floor_h, abs=0.05)
    for bridge in shape.model.bridges:                                                   # cada puente salva el foso
        assert math.hypot(*bridge.a) < spec.arena_r and math.hypot(*bridge.b) > spec.arena_r + spec.moat_m
    assert all(check_clearance(shape, hole)["ok"] for hole in extras.clearances)


def test_colosseum_tiers_rise_outwards_and_the_tunnel_runs_under_the_crown():
    shape, extras = la.build_colosseum()
    spec = la.colosseum_n01()
    _, edges, crown_out, _, _ = la.colosseum_radii(spec)
    tiers = [at(shape, la._pt(r_in + 4.0, 20.0)) for r_in in edges]
    assert tiers == pytest.approx([h for _, h in spec.tiers], abs=0.05)
    t_in, t_out = la.tunnel_span(spec)
    assert at(shape, la._pt(edges[0] + 4.0, 0.0)) == pytest.approx(spec.floor_h, abs=0.05)   # carril abierto
    assert at(shape, la._pt(t_in + 4.0, 0.0)) == pytest.approx(spec.tiers[-1][1], abs=0.05)  # roca sobre el tunel
    assert t_out - t_in >= 10.0 and t_out > crown_out
    assert spec.tiers[-1][1] - spec.floor_h - spec.gate_clear_m >= 2.0                       # techo de >= 2 m
    assert extras.static["rampas"]["max_deg"] <= 18.0


def test_colosseum_refuses_stairs_that_do_not_fit_and_flags_steep_ones():
    with pytest.raises(ValueError):
        la.build_colosseum(spec=replace(la.colosseum_n01(), stair_slope_deg=12.0))
    _, extras = la.build_colosseum(spec=replace(la.colosseum_n01(), stair_slope_deg=25.0))
    assert not extras.static["rampas"]["ok"]


# ── N02 Anfiteatro ───────────────────────────────────────────────────────────────
def test_theatre_tiers_are_a_semicircle_that_steps_down_to_a_stage_by_the_water():
    shape, extras = la.build_theatre()
    spec = la.theatre_n02()
    c = spec.centre
    edges, crest_out, _, _ = la.theatre_radii(spec)
    for deg in (20.0, 70.0, 110.0, 160.0):                                  # lejos de las escaleras
        tiers = [at(shape, la._pt(r_in + 4.5, deg, c)) for r_in in edges]
        assert tiers == pytest.approx([h for _, h in spec.tiers], abs=0.05)
    south = np.arange(200.0, 341.0, 5.0)
    assert (ring(shape, crest_out - 5.0, c, south) < 0.0).all()             # al Sur del diametro no hay grada
    half_w, depth = spec.stage
    assert at(shape, (c[0], c[1] - depth / 2.0)) == pytest.approx(spec.floor_h, abs=0.05)
    assert at(shape, (c[0], c[1] - depth - 3.0)) < 0.0 and at(shape, (c[0] + half_w + 3.0, c[1] - depth / 2.0)) < 0.0
    assert len(extras.markers["columna"]) == 4 and at(shape, extras.markers["columna"][0]) > spec.floor_h + 3.0
    assert extras.static["rampas"]["max_deg"] <= 18.0 and extras.static["cotas"]["max_m"] <= 15.0


# ── N03 Volcan-arena ─────────────────────────────────────────────────────────────
def test_crater_floor_is_split_in_two_by_the_lava_river_and_crossed_five_times():
    shape, extras = la.build_crater()
    spec = la.crater_n03()
    axis = np.arange(-spec.floor_r, spec.floor_r + 0.5, 1.0)
    e, n = np.meshgrid(axis, axis)
    dry = (above_water(shape, e.ravel(), n.ravel()).reshape(e.shape) > 0.1) & (np.hypot(e, n) < spec.floor_r - 2.0)
    assert ndimage.label(dry)[1] == 2                                       # sin puentes, dos mitades
    assert len(shape.model.bridges) == 5
    rim_r = 0.5 * (spec.rim_in + spec.rim_out)
    rim = ring(shape, rim_r)
    assert (rim < 0.0).sum() >= 2 and np.median(rim) == pytest.approx(spec.rim_h, abs=0.05)
    on_rim = [b for b in shape.model.bridges if abs(math.hypot(*b.a) - rim_r) < 3.0]
    assert len(on_rim) == 2 and all(b.z_a - WATER_M == pytest.approx(spec.rim_h, abs=0.3) for b in on_rim)
    assert extras.static["rampas"]["max_deg"] <= 18.0 and len(extras.markers["lava"]) >= 6


# ── N04 Atolon ───────────────────────────────────────────────────────────────────
def test_atoll_is_a_ring_around_a_lagoon_with_islets_and_bridges():
    shape, extras = la.build_atoll()
    spec = la.atoll_n04()
    passes, islets = la.atoll_layout(spec)
    gap = spec.islet_orbit + spec.islet_r + 5.0
    assert (ring(shape, gap) < 0.0).all()                                   # laguna entre islotes y anillo
    assert (ring(shape, spec.islet_orbit - spec.islet_r - 4.0) < 0.0).all()  # y entre islotes y el central
    assert all(at(shape, la._pt(spec.ring_r, deg)) < 0.0 for deg in passes)
    assert all(at(shape, la._pt(spec.islet_orbit, deg)) == pytest.approx(spec.islet_h, abs=0.05) for deg in islets)
    assert at(shape, (0.0, 0.0)) == pytest.approx(spec.islet_h + spec.centre_rise, abs=0.05)
    dry = (ring(shape, spec.ring_r, degs=np.arange(0.0, 360.0, 1.0)) > 0.0).mean()
    assert 0.85 <= dry < 1.0                                                # anillo casi cerrado, con pasos
    assert len(shape.model.bridges) == 3 + len(spec.ring_links) + len(spec.bridged_passes)
    assert len(extras.markers["paso_abierto"]) == 1 and extras.static["cotas"]["max_m"] <= 6.0


# ── N17 Fortaleza en estrella ────────────────────────────────────────────────────
def test_star_fort_has_five_bastions_inside_a_moat_with_a_gate_per_curtain():
    shape, extras = la.build_star_fort()
    spec = la.star_fort_n17()
    verts, tips, gates = la.star_layout(spec)
    assert len(verts) == 10 and len(shape.model.bridges) == 5
    for deg in tips:
        assert at(shape, la._pt(spec.r_tip - 6.0, deg)) == pytest.approx(spec.wall_h, abs=0.05)   # plataforma
        assert at(shape, la._pt(spec.r_tip + spec.moat_m / 2.0, deg)) < 0.0                       # foso en la punta
    for deg in gates:
        assert at(shape, la._pt(spec.r_in - 3.0, deg)) == pytest.approx(spec.parade_h, abs=0.05)  # hueco del adarve
        assert at(shape, la._pt(spec.r_in - 3.0, deg + 12.0)) == pytest.approx(spec.wall_h, abs=0.05)
    rr = np.arange(spec.r_in - 8.0, spec.r_tip + spec.moat_m + 2.0, 0.5)
    for deg in RING_DEG:                                                    # el foso rodea la estrella entera
        a = math.radians(deg)
        assert above_water(shape, rr * math.cos(a), rr * math.sin(a)).min() < 0.0
    assert at(shape, (0.0, 0.0)) == pytest.approx(spec.parade_h + spec.keep_rise, abs=0.05)
    assert extras.static["rampas"]["max_deg"] <= 18.0


def test_sd_polygon_matches_a_square():
    square = np.array([(-10.0, -10.0), (10.0, -10.0), (10.0, 10.0), (-10.0, 10.0)])
    e, n = np.array([0.0, 7.0, 14.0, 13.0]), np.array([0.0, 0.0, 0.0, 14.0])
    assert la.sd_polygon(e, n, square) == pytest.approx([10.0, 3.0, -4.0, -5.0])


# ── N18 Yin-yang ─────────────────────────────────────────────────────────────────
def test_yin_yang_has_two_lobes_at_different_heights_a_hill_eye_and_a_pond_eye():
    shape, extras = la.build_yin_yang()
    spec = la.yin_yang_n18()
    assert at(shape, shape.required["lobulo_yang"]) == pytest.approx(spec.yang_h, abs=0.05)
    assert at(shape, shape.required["lobulo_yin"]) == pytest.approx(spec.yin_h, abs=0.05)
    assert at(shape, shape.required["colina"]) == pytest.approx(spec.yang_h + spec.hill_rise, abs=0.05)
    assert at(shape, extras.markers["estanque"][0]) < 0.0
    rot = math.radians(shape.params["rot_deg"])
    curve = la.s_curve(spec.radius)[10:-10:6]                               # la S, sin las dos puntas
    e = curve[:, 0] * math.cos(rot) - curve[:, 1] * math.sin(rot)
    n = curve[:, 0] * math.sin(rot) + curve[:, 1] * math.cos(rot)
    assert (above_water(shape, e, n) < 0.0).all()                           # el canal separa los lobulos
    assert len(shape.model.bridges) == 3
    for bridge in shape.model.bridges:
        assert abs(bridge.z_a - bridge.z_b) == pytest.approx(spec.yang_h - spec.yin_h, abs=0.1)
        assert bridge.slope_deg() <= 18.0
