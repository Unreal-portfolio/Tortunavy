"""Catálogo de circuitos del Rally (#692): R03 a R06 (terrain_geo/rally_circuit_themes.CIRCUITS), generados con el
generador de vueltas (perfil tierra de #682, anchos por tramo de #622) con semillas y temas distintos; sobre cada variante
YA GENERADA, el validador de la malla (rally_circuit_check y rally_circuit_check_dirt) y su registro en el índice y en el
selector del Rally (TN_LobbyMission*.cpp).

    uv run pytest Scripts/tests/test_terrain_rally_catalogo.py

Las variantes se regeneran con
`uv run --with pyfqmr --with matplotlib python Scripts/gen_terrain_rally_circuit.py --all-circuits`.
"""

from __future__ import annotations

import copy
import json
import re
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from terrain_geo import rally_circuit as rc  # noqa: E402
from terrain_geo.build import VARIANTS  # noqa: E402
from terrain_geo.heightfield import ZONES  # noqa: E402
from terrain_geo.rally_circuit_check import LIMITS, load_report, verdict  # noqa: E402
from terrain_geo.rally_circuit_check_dirt import LIMITS_TIERRA  # noqa: E402
from terrain_geo.rally_circuit_dirt import SUSPENSION  # noqa: E402
from terrain_geo.rally_circuit_themes import CIRCUITS, THEMES, Theme, theme  # noqa: E402
from terrain_vol.layout import UU_PER_M  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
LOBBY = ROOT / "Source/Tortunabo/Private/Lobby"
RETIRED = ("E01B_espana_rally", "I03R_tortuga_magna", "I04_volcan_hueco", "I06_feroe")
NAMES = sorted(CIRCUITS)


def _generated(name: str) -> bool:
    return (VARIANTS / name / "manifest.json").exists()


def _manifest(name: str) -> dict:
    return json.loads((VARIANTS / name / "manifest.json").read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def reports() -> dict:
    return {name: load_report(VARIANTS / name) for name in NAMES if _generated(name)}


# ── Temas ────────────────────────────────────────────────────────────────────────
def test_el_tema_base_es_el_relieve_de_r01_y_r02():
    """R01 y R02 se generaron antes de los temas: el tema base reproduce sus constantes."""
    base = theme("base")
    assert (base.hills_m, base.hills_scale_m, base.near_m, base.ripples_m, base.rim_m) == (10.0, 110.0, 2.0, 0.4, 14.0)
    assert base.color_zone == "cliffs" and base.shore_m == 3.5 and base.wall_strata == 0.0
    assert base.trail_color == rc.RallyCircuitModel.trail_color and base.trail_strength == rc.RallyCircuitModel.trail_strength


def test_caso_negativo_tema_desconocido_o_paleta_fuera_de_la_arena():
    with pytest.raises(ValueError):
        theme("selva")
    THEMES["_verde"] = Theme("_verde", color_zone="algae_verde")
    try:
        with pytest.raises(ValueError):
            theme("_verde")
    finally:
        del THEMES["_verde"]


def test_catalogo_con_semillas_y_temas_distintos_y_perfil_tierra():
    circuits = list(CIRCUITS.values())
    assert len(circuits) >= 4
    assert len({c.seed for c in circuits}) == len(circuits) and len({c.theme for c in circuits}) == len(circuits)
    assert all(c.profile == "tierra" and c.theme in THEMES and c.theme != "base" for c in circuits)
    assert all(re.fullmatch(r"R0[3-9]_circuito_[a-z_]+", c.name) for c in circuits)
    assert rc.SEED not in {c.seed for c in circuits} and rc.TIERRA_SEED not in {c.seed for c in circuits}


# ── Variantes generadas ──────────────────────────────────────────────────────────
@pytest.mark.parametrize("name", NAMES)
def test_registrada_en_el_indice_con_su_tema_y_vistas(name):
    if not _generated(name):
        pytest.skip(f"{name} sin generar")
    c, data = CIRCUITS[name], _manifest(name)
    index = json.loads((VARIANTS / "index.json").read_text(encoding="utf-8"))
    entry = next(e for e in index if e["name"] == name)
    assert entry["mode"] == "rally" and entry["recorrible"] is True and entry["seed"] == c.seed
    gen = data["generator"]
    assert gen["generator"] == "rally_circuit_vueltas" and gen["profile"] == c.profile and gen["theme"] == c.theme
    assert data["closed"] is True and data["laps"] == rc.LAPS and data["description"] == c.description
    assert all(len(cp) == 4 for cp in data["checkpoints_uu"]) and len(data["road_widths_m"]) == len(data["road_uu"])
    for view in ("preview.png", "lamina.png"):
        assert 10_000 < (VARIANTS / name / view).stat().st_size < 1_000_000


@pytest.mark.parametrize("name", NAMES)
def test_reproducible_desde_la_semilla(name):
    if not _generated(name):
        pytest.skip(f"{name} sin generar")
    c, data = CIRCUITS[name], _manifest(name)
    track = rc.build_track(c.seed, c.profile)
    road = track.plan.pts + rc.make_frame(track).shift
    got = np.asarray(data["road_uu"], dtype=np.float64)
    assert len(got) == len(road) and np.abs(got[:, :2] - road * UU_PER_M).max() < 0.1
    assert np.abs(got[:, 2] - track.z * UU_PER_M).max() < 0.1


@pytest.mark.parametrize("name", NAMES)
def test_veredicto_completo_en_verde(name, reports):
    if name not in reports:
        pytest.skip(f"{name} sin generar")
    _, checks = reports[name]
    assert all(checks.values()), {k: v for k, v in checks.items() if not v}
    assert _manifest(name)["checks"]["verdict"] == checks


@pytest.mark.parametrize("name", NAMES)
def test_saltos_y_baches_medidos_en_la_malla(name, reports):
    if name not in reports:
        pytest.skip(f"{name} sin generar")
    report, _ = reports[name]
    kinds = [j["jump_kind"] for j in report["jumps"]]
    assert len(kinds) >= LIMITS_TIERRA["jumps"] and len(set(kinds)) >= 3
    for j in report["jumps"]:
        assert j["zone_m"][0] <= j["x_land_m"] <= j["zone_m"][1] and j["impact_ms"] <= LIMITS["impact_ms"]
        if j.get("gap_m") is not None:
            assert j["ai_clears_gap"]
    bumps = report["tierra"]["bumps"]
    assert {b["pattern"] for b in bumps} == {"whoops", "tabla_lavar"}
    for b in bumps:
        assert 0.6 * b["amplitude_m"] <= b["measured_amplitude_m"] <= SUSPENSION.max_amplitude_m + 0.02


def test_saltos_baches_y_trazados_variados_entre_circuitos():
    """Cada circuito trae su orden de piezas, su vuelta y sus saltos y baches (alturas, ondas) distintos."""
    made = [n for n in NAMES if _generated(n)]
    if len(made) < 2:
        pytest.skip("menos de dos circuitos generados")
    data = {n: _manifest(n) for n in made}
    orders = {tuple(p["kind"] + p["variant"] for p in d["generator"]["pieces"]) for d in data.values()}
    assert len(orders) == len(made)
    assert len({d["lap"]["length_m"] for d in data.values()}) == len(made)
    heights = {tuple(sorted(round(e["height_m"], 1) for e in d["elements"] if e["type"] == "salto")) for d in data.values()}
    waves = {tuple(sorted(round(e["wavelength_m"], 1) for e in d["elements"] if e["type"] == "baches")) for d in data.values()}
    assert len(heights) == len(made) and len(waves) == len(made)


@pytest.mark.parametrize("name", NAMES)
def test_caso_negativo_el_veredicto_cae_con_un_bache_fuera_de_la_suspension(name, reports):
    if name not in reports:
        pytest.skip(f"{name} sin generar")
    report, _ = reports[name]
    broken = copy.deepcopy(report)
    broken["tierra"]["bumps"][0]["measured_amplitude_m"] = 0.0
    assert verdict(broken, _manifest(name))["bumps"] is False


# ── Colores del tema en la malla ─────────────────────────────────────────────────
def test_la_paleta_del_tema_manda_fuera_de_la_playa():
    track = rc.build_track(CIRCUITS["R05_circuito_marismas"].seed, "tierra")
    model = rc.RallyCircuitModel(track, CIRCUITS["R05_circuito_marismas"].seed, "marismas")
    x, y = model.road[::200, 0], model.road[::200, 1]
    w = model.color_weights(x, y)
    assert set(w) == set(ZONES) and np.allclose(sum(w.values()), 1.0)
    assert (w["marsh"] > 0.5).all() and np.allclose(w["cliffs"], 0.0)


# ── Selector del Rally (C++) ─────────────────────────────────────────────────────
def test_selector_del_rally_solo_con_circuitos_del_generador():
    names = (LOBBY / "TN_LobbyMission.cpp").read_text(encoding="utf-8")
    rally = (LOBBY / "TN_LobbyMissionRally.cpp").read_text(encoding="utf-8")
    known = re.search(r"KnownCircuits\[\] = \{(.*?)\};", rally, re.S).group(1)
    for name in ["R01_circuito_dunas", "R02_circuito_tierra", *NAMES]:
        assert f'TEXT("{name}")' in known or (name == "R01_circuito_dunas" and "DefaultRallyCircuit" in known)
        key = "RallyMap" + name[:3]
        assert re.search(rf'TEXT\("{name}"\).*NSLOCTEXT\("Tortunabo", "{key}", "[^"]+"\)', names), name
    for name in RETIRED:
        assert name not in known and name not in names and not (VARIANTS / name).exists()


@pytest.mark.parametrize("name", RETIRED)
def test_variantes_de_autor_fuera_del_indice(name):
    index = json.loads((VARIANTS / "index.json").read_text(encoding="utf-8"))
    assert all(e["name"] != name for e in index)
