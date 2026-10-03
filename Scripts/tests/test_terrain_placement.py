"""Colocación por reglas de diseño para los mapas «camino primero» (#652).

Un sitio sintético (rápido) prueba el generador y cada regla del validador, con un caso negativo por
regla; el mapa real C01 (rehace el modelo, unos 15 s) prueba la colocación entera y el bloque que
hay escrito en su manifest.
"""

from __future__ import annotations

import json
import sys
from dataclasses import replace
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from terrain_path.curves import resample  # noqa: E402
from terrain_path.placement import generate  # noqa: E402
from terrain_path.placement_catalog import HOSTILE, PUZZLE_GAP_M, TRAMO_M  # noqa: E402
from terrain_path.placement_io import (  # noqa: E402
    build_block,
    carry_placements,
    from_json,
    progress_field,
    read_block,
    to_json,
    with_block,
)
from terrain_path.placement_rules import Placement, validate  # noqa: E402
from terrain_path.placement_site import Site, SiteLine  # noqa: E402

TUNNEL = (180.0, 200.0)       # tramo cubierto del principal sintético
WATER = (620.0, 660.0)        # tramo de río del principal sintético


def _line(line_id, parent, waypoints, s_out=0.0, s_back=0.0, water=None, tunnel=None, half_width=7.0):
    pts, arc = resample(np.array(waypoints, dtype=float), 1.0)
    biome = np.full(len(arc), 2)
    wet = np.zeros(len(arc), dtype=bool)
    if water:
        wet = (arc >= water[0]) & (arc <= water[1])
        biome[wet] = 1
    blocked = (arc >= tunnel[0]) & (arc <= tunnel[1]) if tunnel else np.zeros(len(arc), dtype=bool)
    return SiteLine(line_id, parent, pts, arc, np.zeros(len(arc)), np.full(len(arc), half_width), biome, blocked,
                    wet, s_out, s_back)


def _site(extra_lines=()) -> Site:
    """Principal de 799 m con un rodeo (lazo 1: 186 m frente a 130 m) y un atajo (lazo 2: 153 m
    frente a 249 m): sus extremos coinciden con puntos del principal."""
    main = _line(0, None, [(0, 0), (300, 0), (340, 80), (410, 80), (450, 0), (700, 0)], water=WATER, tunnel=TUNNEL)
    detour = _line(1, 0, [(100, 0), (130, 50), (200, 50), (230, 0)], 100.0, 230.0)
    short = _line(2, 0, [(300, 0), (375, -15), (450, 0)], 300.0, 300.0 + 2 * np.hypot(40, 80) + 70.0)
    lines = (main, detour, short) + tuple(extra_lines)
    junctions = tuple((float(p[0]), float(p[1])) for ln in lines[1:] for p in (ln.points[0], ln.points[-1]))
    return Site("sintetico", lines, main.at(0.0), main.at(main.length), junctions, ())


@pytest.fixture(scope="module")
def site():
    return _site()


def P(pid, category, kind, line, s, q=0.0, length=0.0, **params):
    return Placement(pid, category, kind, line, float(s), q, length, params=params)


def _nests(site):
    return [P(f"n{k}", "nest", "EggNest", 0, s) for k, s in enumerate((150.0, 290.0, 470.0, 600.0))]


def _rules(site, placements):
    return {v.rule for v in validate(site, placements)}


# -- sitio y grafo ------------------------------------------------------------------------------------------
def test_el_sitio_sintetico_tiene_un_rodeo_y_un_atajo(site):
    kinds = {loop.id: loop.length / (b - a) for loop, _p, a, b in site.bifurcations()}
    assert kinds[1] > 1.3 and kinds[2] < 0.7


def test_la_geodesica_va_por_el_camino_y_no_en_linea_recta(site):
    loop = site.line(1)
    d = site.geodesic_from([(1, 0.0)])[0]
    assert d[site.node(1, loop.length)] == pytest.approx(130.0, abs=2.0)          # por el principal
    assert d[site.node(1, loop.length / 2.0)] == pytest.approx(loop.length / 2.0, abs=1.0)
    assert d[site.node(0, 400.0)] == pytest.approx(300.0, abs=2.0)                # 100 m de vuelta + 300


def test_todo_el_sitio_es_alcanzable(site):
    assert site.reachable().all()


# -- generador ----------------------------------------------------------------------------------------------
@pytest.mark.parametrize("seed", [1, 2, 3, 652])
def test_el_generador_cumple_todas_las_reglas(site, seed):
    result = generate(site, seed)
    assert result.violations == []
    cats = {p.category for p in result.auto}
    assert {"puzzle", "nest", "loot", "decor"} <= cats and cats & HOSTILE


def test_el_generador_es_determinista(site):
    a = [(p.id, p.s, p.q) for p in generate(site, 9).auto]
    b = [(p.id, p.s, p.q) for p in generate(site, 9).auto]
    assert a == b


def test_lo_manual_no_se_toca_y_cuenta_como_restriccion(site):
    manual = [replace(P("mi_puzle", "puzzle", "throw_chain", 0, 260.0, length=24.0), source="manual")]
    result = generate(site, 652, manual=manual)
    assert result.manual == manual and all(p.source == "auto" for p in result.auto)
    assert "mi_puzle" not in {p.id for p in result.auto}
    d = site.geodesic_from([(0, 260.0)])[0]
    assert all(d[site.node(p.line, p.s)] >= PUZZLE_GAP_M for p in result.auto if p.category == "puzzle")


def test_lo_suprimido_no_se_vuelve_a_crear(site):
    first = generate(site, 652)
    gone = next(p.id for p in first.auto if p.category == "puzzle")
    again = generate(site, 652, suppressed=[gone])
    assert gone not in {p.id for p in again.auto}


def test_el_atajo_lleva_mas_peligro_por_metro_que_la_ruta_larga(site):
    result = generate(site, 652)
    assert not [v for v in result.violations if v.rule == "densidad"]


# -- casos negativos: cada regla salta ---------------------------------------------------------------------
def test_puzles_demasiado_juntos(site):
    items = _nests(site) + [P("a", "puzzle", "throw_chain", 0, 260.0, length=24.0),
                            P("b", "puzzle", "shell_gauntlet", 0, 260.0 + PUZZLE_GAP_M - 30.0, length=34.0)]
    assert "separacion_puzles" in _rules(site, items)


def test_enemigo_en_la_calma_de_un_puzle(site):
    items = _nests(site) + [P("a", "puzzle", "throw_chain", 0, 260.0, length=24.0),
                            P("e", "enemy", "Lizard", 0, 280.0)]
    assert "calma_puzle" in _rules(site, items)


@pytest.mark.parametrize("s, line", [(10.0, 0), (101.0, 0), (152.0, 0), (5.0, 1)])
def test_nada_en_la_salida_uniones_ni_nidos(site, s, line):
    assert "exclusion" in _rules(site, _nests(site) + [P("e", "enemy", "Lizard", line, s)])


def test_nada_junto_a_la_meta(site):
    main = site.main
    assert "exclusion" in _rules(site, _nests(site) + [P("e", "obstacle", "Mine", 0, main.length - 10.0)])


@pytest.mark.parametrize("item", [
    P("t", "puzzle", "throw_chain", 0, 190.0, length=24.0),        # túnel
    P("w", "puzzle", "throw_chain", 0, 640.0, length=24.0),        # río
    P("g", "enemy", "GullZone", 0, 400.0),                          # gaviotas fuera del agua
    P("l", "enemy", "Lizard", 0, 640.0),                            # lagarto en el río
    P("q", "enemy", "Lizard", 0, 400.0, q=9.0),                     # fuera del camino
    P("d", "decor", "Rock", 0, 400.0, q=1.0),                       # decorado en el eje
    P("x", "puzzle", "plate_balance", 2, 76.0, length=26.0),        # cabe, pero se cambia la anchura abajo
])
def test_posiciones_imposibles(site, item):
    if item.id == "x":
        narrow = replace(site.line(2), half_width=np.full(len(site.line(2).arc), 4.0))
        site = Site(site.name, (site.lines[0], site.lines[1], narrow), site.start, site.end, site.junctions, ())
    assert "posicion" in _rules(site, _nests(site) + [item])


def test_puzle_en_un_camino_que_no_conecta():
    floating = _line(9, 42, [(100, 300), (200, 300)], 0.0, 10.0)
    site = _site((floating,))
    assert "alcanzable" in _rules(site, _nests(site) + [P("p", "puzzle", "wobbly_run", 9, 50.0, length=28.0)])


def test_densidad_al_reves(site):
    items = _nests(site) + [P(f"e{k}", "enemy", "Lizard", 1, s) for k, s in enumerate((40.0, 70.0, 100.0, 130.0))]
    assert "densidad" in _rules(site, items)


def test_atajo_sin_peligro_con_sitio_libre(site):
    assert "densidad" in _rules(site, _nests(site))


def test_pico_sin_calma_despues(site):
    s0 = 5 * TRAMO_M                    # pico: un puzle de intensidad 4 en el tramo 5
    items = _nests(site) + [P("a", "puzzle", "throw_chain", 0, s0 + 10.0, length=24.0),
                            P("e1", "enemy", "GiantCrab", 0, s0 + TRAMO_M + 45.0),
                            P("e2", "enemy", "SandFleas", 0, s0 + TRAMO_M + 25.0)]
    assert "curva" in _rules(site, items)
    calm = [p for p in items if p.id != "e1"]          # 1,5 en el tramo siguiente: sí hay calma
    assert "curva" not in _rules(site, calm)


def test_enemigos_amontonados(site):
    items = _nests(site) + [P("a", "enemy", "Lizard", 0, 400.0), P("b", "enemy", "SeaUrchin", 0, 405.0)]
    assert "hostiles_separados" in _rules(site, items)


def test_sin_nidos_hay_tramos_sin_reaparicion(site):
    assert "nidos" in _rules(site, [])


def test_puzle_de_grupo_sin_nido_antes(site):
    items = [P("n0", "nest", "EggNest", 0, 150.0), P("n1", "nest", "EggNest", 0, 330.0),
             P("n2", "nest", "EggNest", 0, 500.0), P("n3", "nest", "EggNest", 0, 650.0),
             P("a", "puzzle", "throw_chain", 0, 440.0, length=24.0)]
    assert "nidos" in _rules(site, items)
    assert "nidos" not in _rules(site, items[:-1])


def test_dos_puzles_seguidos_del_mismo_tipo(site):
    items = _nests(site) + [P("a", "puzzle", "throw_chain", 0, 330.0, length=24.0),
                            P("b", "puzzle", "throw_chain", 0, 520.0, length=24.0)]
    assert "secuencia" in _rules(site, items)


# -- manifest -----------------------------------------------------------------------------------------------
def test_el_bloque_va_y_vuelve_y_respeta_el_resto_del_manifest(site):
    result = generate(site, 652)
    manifest = {"name": "x", "cells": [1, 2], "kill_boxes_uu": []}
    manual = [{"id": "m1", "category": "enemy", "kind": "GiantCrab", "location_uu": [65000.0, 100.0, 0.0]}]
    block = build_block(site, result.auto, manual, ["pz-zzz"], 652, 1, False, {})
    out = with_block(manifest, block)
    assert list(out)[:3] == ["name", "cells", "kill_boxes_uu"] and manifest.get("placements") is None
    raw_manual, suppressed, _ = read_block(json.loads(json.dumps(out)))
    assert raw_manual == manual and suppressed == ["pz-zzz"]
    back = [from_json(site, to_json(site, p), "auto") for p in result.auto]
    assert all(a.id == b.id and a.line == b.line and abs(a.s - b.s) < 0.01 for a, b in zip(result.auto, back))
    projected = from_json(site, manual[0])
    assert projected.line == 0 and projected.s == pytest.approx(749.0, abs=1.5) and projected.q == pytest.approx(1.0, abs=0.1)


def test_los_tramos_llevan_su_polilinea_y_el_geiser_su_destino(site):
    ln = site.line(0)
    puzzle = to_json(site, Placement("pz", "puzzle", "throw_chain", 0, 300.0, length=24.0))
    path = np.asarray(puzzle["path_uu"]) / 100.0
    assert len(path) == 13
    assert np.allclose(path[0], ln.at(288.0), atol=0.01) and np.allclose(path[-1], ln.at(312.0), atol=0.01)
    assert "path_uu" not in to_json(site, Placement("en", "enemy", "SeaUrchin", 0, 300.0))
    assert "target_uu" not in to_json(site, Placement("tr", "mechanic", "Trampoline", 0, 300.0))
    geyser = to_json(site, Placement("gy", "mechanic", "Geyser", 0, 300.0))
    assert np.allclose(np.asarray(geyser["target_uu"]) / 100.0, ln.at(314.0), atol=0.01)


def test_el_avance_ordena_los_lazos_entre_los_puntos_del_principal(site):
    """progress_m (orden de los nidos al cargar): un punto de un lazo cae entre sus extremos en el
    principal y no detrás de todo el principal."""
    progress = progress_field(site)

    def at(line, s):
        return float(progress[site.node(line, s)])

    main = site.main
    assert at(0, 0.0) == pytest.approx(0.0, abs=0.5)
    assert at(0, main.length) == pytest.approx(main.length, abs=0.5)
    assert at(0, 50.0) < at(0, 400.0) < at(0, 700.0)
    detour, short = site.line(1), site.line(2)
    assert at(0, 90.0) < at(1, detour.length / 2.0) < at(0, 240.0)
    assert at(0, 290.0) < at(2, short.length / 2.0) < at(0, 560.0)
    entry = to_json(site, P("n", "nest", "EggNest", 1, detour.length / 2.0), progress)
    assert entry["progress_m"] == pytest.approx(at(1, detour.length / 2.0), abs=0.1)


def test_regenerar_el_terreno_conserva_lo_colocado(tmp_path):
    path = tmp_path / "manifest.json"
    path.write_text(json.dumps({"seed": 7, "placements": {"map_seed": 7, "manual": [{"id": "m"}], "auto": []}}))
    assert carry_placements(path, 7)["manual"] == [{"id": "m"}]
    assert carry_placements(path, 8)["stale"] is True
    assert carry_placements(tmp_path / "no.json", 7) is None


# -- mapa real C01 -------------------------------------------------------------------------------------------
@pytest.fixture(scope="module")
def c01():
    from place_terrain_path import load_site
    return load_site("C01_camino")


def test_c01_se_coloca_sin_violaciones(c01):
    site, _manifest, _path = c01
    result = generate(site, 652)
    assert result.violations == []
    count = {c: sum(p.category == c for p in result.auto) for c in ("puzzle", "nest", "mechanic")}
    assert count["puzzle"] >= 3 and count["nest"] >= 3 and count["mechanic"] >= 2
    assert sum(p.category in HOSTILE for p in result.auto) >= 10
    assert any(p.kind == "catapult_gap" for p in result.auto)


def test_c01_el_bloque_del_manifest_cumple_las_reglas(c01):
    site, manifest, _path = c01
    manual, _suppressed, block = read_block(manifest)
    assert block.get("format") == 1 and block.get("map_seed") == manifest["seed"] and not block.get("stale")
    placed = [from_json(site, d, "auto") for d in block["auto"]] + [from_json(site, d) for d in manual]
    assert validate(site, placed) == []


def test_c01_lamina(c01, tmp_path):
    from terrain_path.placement_sheet import render_sheet
    site, _manifest, path = c01
    out = render_sheet(site, generate(site, 652).all, tmp_path / "lamina.png", path.parent / "preview.png", "C01")
    from PIL import Image
    assert Image.open(out).size[0] > 1200
