"""Tableros de Todos contra Todos del lote B (terrain_shapes/lots_boards.py): A07 a A12, teselados de hexágonos
y de cuadrados. Cada mapa se genera una vez en una carpeta temporal y se comprueba con números lo propio de su
diseño: casillas, cotas, enlaces y validadores de la malla.

    uv run --with pytest --with numpy --with scipy --with pillow --with scikit-image --with matplotlib \
        python -m pytest Scripts/tests/test_terrain_boards.py
"""

from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from terrain_shapes import lots_boards as boards  # noqa: E402
from terrain_shapes.kit_writer import write_kit_map  # noqa: E402
from terrain_vol.layout import WATER_M  # noqa: E402
from terrain_vol.validate import reachable  # noqa: E402

MAX_TCT_M = 15.0
TCT_TRIANGLES = 350_000


def above_water(shape, e: float, n: float) -> float:
    X, Y = shape.canvas.to_world(e, n)
    return float(shape.model.ground_height(np.array([X]), np.array([Y]))[0]) - WATER_M


def tile_heights(shape) -> Counter:
    """Cuántas casillas hay a cada cota (medida en el modelo, en el punto requerido de cada una)."""
    return Counter(round(above_water(shape, *p), 1) for p in shape.required.values())


@pytest.fixture(scope="module")
def built(tmp_path_factory):
    """Genera cada mapa una sola vez: (ShapeMap, Extras, resultado de write_kit_map)."""
    cache: dict[str, tuple] = {}

    def get(key: str):
        if key not in cache:
            shape, extras = boards.MAPS[key][1](None)
            result = write_kit_map(shape, extras, sheet=False, variants=tmp_path_factory.mktemp(key))
            cache[key] = (shape, extras, result)
        return cache[key]

    return get


def assert_valid_arena(shape, result) -> None:
    """Criterio común: validadores en verde, 8 nidos, tope de triángulos, 200 m, cota máxima y todas las
    casillas alcanzables a pie (20 grados) desde el inicio sobre la malla."""
    assert result["ok"], {k: result[k] for k in ("required_names_missed", "corridors_failed", "unreachable_islands")}
    assert result["checks"]["nests"]["found"] == 8
    assert result["budget"]["triangles"] <= TCT_TRIANGLES and result["seams"]["ok"]
    assert shape.mode == "tct" and shape.canvas.grid == 2
    assert result["top_max"] - WATER_M <= MAX_TCT_M
    seen = reachable(result["top"], shape.canvas.to_ij(*shape.start), shape.corridor_slope_deg)
    assert all(seen[shape.canvas.to_ij(*p)] for p in shape.required.values())


def test_a07_pyramid_rises_by_rings_to_the_centre(built):
    shape, extras, result = built("A07")
    assert_valid_arena(shape, result)
    assert tile_heights(shape) == {2.0: 18, 5.0: 12, 8.0: 6, 11.0: 1}
    assert above_water(shape, 0.0, 0.0) == pytest.approx(11.0, abs=0.05)
    links = [(tuple(a), tuple(b)) for a, b in shape.params["links"]]
    steps = {frozenset((boards.hex_ring(a), boards.hex_ring(b))) for a, b in links}
    assert {frozenset((k, k + 1)) for k in range(3)} <= steps                   # hay rampa en cada escalón
    assert len(links) == 36 + 12 and extras.markers["cofre"] == [(0.0, 0.0)]


def test_a08_shards_are_24_loose_hexagons_at_four_heights_joined_by_narrow_bridges(built):
    shape, _, result = built("A08")
    assert_valid_arena(shape, result)
    heights = tile_heights(shape)
    assert sum(heights.values()) == 24 and set(heights) == {2.0, 4.5, 7.0, 9.5}
    bridges = shape.model.bridges
    assert len(bridges) == 23 + 3 and all(b.width_m == 3.5 for b in bridges)
    assert max(b.slope_deg() for b in bridges) <= 18.0
    assert all(abs(b.z_a - b.z_b) <= 2.5 + 1e-9 for b in bridges)             # un nivel como mucho por puente
    mid = [(0.5 * (b.a[0] + b.b[0]), 0.5 * (b.a[1] + b.b[1])) for b in bridges]
    assert all(above_water(shape, *p) < 0.0 for p in mid)                     # bajo el puente hay agua


def test_a09_hive_has_61_cells_with_death_pools_and_three_royal_cells(built):
    shape, extras, result = built("A09")
    assert_valid_arena(shape, result)
    assert tile_heights(shape) == {3.0: 48, 5.0: 3}
    pools = extras.markers["charca"]
    assert len(pools) == 10 and len(shape.required) + len(pools) == 61
    assert all(-1.0 < above_water(shape, *p) < -0.5 for p in pools)           # hundidas: agua, no suelo
    assert sorted(round(above_water(shape, *p), 1) for p in extras.markers["cofre"]) == [5.0, 5.0, 5.0]
    assert above_water(shape, 0.0, 0.0) == pytest.approx(3.0, abs=0.05)       # la central nunca es charca


def test_a10_chessboard_alternates_two_heights_and_has_four_corner_towers(built):
    shape, extras, result = built("A10")
    assert_valid_arena(shape, result)
    assert tile_heights(shape) == {2.0: 32, 5.0: 32}
    spec = boards.chess_a10()
    for (i, j), (ce, cn) in boards._square_centres(8, spec.size_m).items():
        if (i, j) not in ((0, 0), (0, 7), (7, 0), (7, 7)):
            assert above_water(shape, ce, cn) == pytest.approx(5.0 if (i + j) % 2 else 2.0, abs=0.05)
    towers = extras.markers["torre"]
    assert len(towers) == 4 and all(above_water(shape, *p) == pytest.approx(9.0, abs=0.05) for p in towers)
    assert len(shape.params["links"]) == 63 + 8


def test_a11_ziggurat_steps_up_inside_a_moat_crossed_by_four_bridges(built):
    shape, _, result = built("A11")
    assert_valid_arena(shape, result)
    assert tile_heights(shape) == {2.0: 32, 5.0: 16, 8.0: 8, 11.0: 1}
    size = boards.ziggurat_a11().size_m
    moat = [(3 * size * se, t * size) for se in (-1, 1) for t in (-3, 0, 3)] + [(0.0, 3 * size), (0.0, -3 * size)]
    wet = [p for p in moat if above_water(shape, *p) < 0.0]
    assert len(wet) >= len(moat) - 4                                          # foso de agua, salvo bajo un puente
    assert len(shape.model.bridges) == 4 and max(b.slope_deg() for b in shape.model.bridges) <= 18.0
    assert len(shape.params["links"]) == 2 + 4 + 4 and shape.end == (0.0, 0.0)


def test_a12_draughts_keeps_only_the_dark_squares_joined_in_diagonal(built):
    shape, _, result = built("A12")
    assert_valid_arena(shape, result)
    heights = tile_heights(shape)
    assert sum(heights.values()) == 32 and set(heights) == {3.0, 5.5, 8.0}
    spec = boards.draughts_a12()
    for (i, j), (ce, cn) in boards._square_centres(8, spec.pitch_m).items():
        assert (above_water(shape, ce, cn) > 0.0) == ((i + j) % 2 == 0)       # las claras son agua
    bridges = shape.model.bridges
    assert len(bridges) == 31 + 8 and max(b.slope_deg() for b in bridges) <= 18.0
    assert all(abs(abs(b.b[0] - b.a[0]) - abs(b.b[1] - b.a[1])) < 1e-6 for b in bridges)      # todas en diagonal


def test_every_board_changes_with_the_seed_and_refuses_a_ramp_that_does_not_fit():
    for key, (_, factory) in boards.MAPS.items():
        first, other = factory(None)[0], factory(99)[0]
        assert first.params["links"] != other.params["links"], key
        assert first.name == other.name and other.seed == 99
    steep = boards.ChessSpec("X_empinado", 1, high_m=9.0)                     # 7 m de escalón en casillas de 18 m
    with pytest.raises(ValueError):
        boards.build_chess(steep)


def test_board_graph_helpers():
    centres = boards._square_centres(3, 10.0)
    pairs = boards.near_pairs(centres, 10.1)
    assert len(pairs) == 12 and boards.connected(centres, pairs)
    assert not boards.connected([(0, 0), (2, 2)], pairs)
    rng = np.random.default_rng(7)
    dropped = boards.drop_tiles(centres, pairs, rng, 3, frozenset({(1, 1)}))
    assert len(dropped) == 3 and (1, 1) not in dropped
    assert boards.connected([k for k in centres if k not in dropped], pairs)
    links = boards.spanning_links(centres, pairs, rng, 2)
    assert len(links) == 8 + 2 and boards.connected(centres, links)
    settled = boards.settle_levels({(0, 0): 0, (0, 1): 3, (0, 2): 3}, [((0, 0), (0, 1)), ((0, 1), (0, 2))])
    assert settled == {(0, 0): 0, (0, 1): 1, (0, 2): 2}
    with pytest.raises(ValueError):
        boards.spanning_links([(0, 0), (2, 2)], pairs, rng, 0)
