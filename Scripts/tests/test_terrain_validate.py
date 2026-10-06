"""Validadores comunes de mapas (Scripts/terrain_vol/validate.py): transitabilidad, ancho de pasarela, islas
inalcanzables, presupuesto de malla y costuras entre trozos.

    uv run --with pytest --with numpy --with scipy --with pillow --with scikit-image \
        python -m pytest Scripts/tests/test_terrain_validate.py
"""

from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from terrain_vol.export import read_chunk, write_chunk  # noqa: E402
from terrain_vol.layout import WATER_M  # noqa: E402
from terrain_vol.validate import (MeshBudget, check_budget, check_seams, corridor_ok, evaluate_map,  # noqa: E402
                                  local_slope_deg, reachable, slope_stats, unreachable_islands, wide_ground)

VARIANTS = Path(__file__).resolve().parent.parent / "terrain_volumes" / "Variants"
SEA = WATER_M - 1.5


def two_islands(bridge_width: int = 0, bridge_rise: float = 0.0) -> np.ndarray:
    """Dos mesetas de 20 x 20 m a +3 m separadas por 10 m de agua; con bridge_width > 0, una pasarela entre ellas."""
    top = np.full((40, 60), SEA)
    top[10:30, 5:25] = WATER_M + 3.0
    top[10:30, 35:55] = WATER_M + 3.0
    if bridge_width:
        rows = slice(20 - bridge_width // 2, 20 - bridge_width // 2 + bridge_width)
        span = np.arange(25, 35)
        top[rows, 25:35] = WATER_M + 3.0 + bridge_rise * np.sin(np.pi * (span - 24.0) / 11.0)[None, :]
    return top


def test_water_separates_and_a_bridge_joins():
    assert not reachable(two_islands(), (20, 10))[20, 45]
    assert reachable(two_islands(4), (20, 10))[20, 45]


def test_a_step_higher_than_the_climb_blocks_the_way():
    top = np.full((10, 30), WATER_M + 1.0)
    top[:, 15:] = WATER_M + 3.5                         # escalon de 2,5 m
    assert not reachable(top, (5, 2))[5, 25]
    top[4:7, 10:16] = WATER_M + 1.0 + 0.5 * np.arange(6)[None, :]     # rampa de 0,5 m por metro
    assert reachable(top, (5, 2))[5, 25]


def test_slope_statistics():
    top = np.tile(np.arange(20, dtype=float) * np.tan(np.radians(10.0)), (20, 1))
    assert local_slope_deg(top)[5, 5] == pytest.approx(10.0)
    stats = slope_stats(top, np.ones(top.shape, bool))
    assert stats["p50"] == pytest.approx(10.0) and stats["max"] == pytest.approx(10.0)


def test_bridge_width_is_checked():
    assert corridor_ok(two_islands(4), (20, 12), (20, 45), min_width_m=3.0)
    assert not corridor_ok(two_islands(2), (20, 12), (20, 45), min_width_m=3.0)       # 2 m: estrecha
    assert not corridor_ok(two_islands(), (20, 12), (20, 45), min_width_m=3.0)


def test_bridge_slope_is_checked():
    arched = two_islands(5, bridge_rise=4.0)                                          # sube ~1,1 m por metro
    assert corridor_ok(arched, (20, 12), (20, 45), min_width_m=3.0, max_slope_deg=50.0)
    assert not corridor_ok(arched, (20, 12), (20, 45), min_width_m=3.0, max_slope_deg=20.0)
    assert wide_ground(arched, 3.0, 20.0)[20, 12] and not wide_ground(arched, 3.0, 20.0)[20, 28]


def test_unreachable_islands_unless_marked_or_tiny():
    top = two_islands()
    lost = unreachable_islands(top, (20, 10))
    assert len(lost) == 1 and lost[0]["area_m2"] == 400.0
    assert unreachable_islands(top, (20, 10), marked=((15, 40),)) == []
    assert unreachable_islands(top, (20, 10), min_area_m2=500.0) == []
    assert unreachable_islands(two_islands(4), (20, 10)) == []


def test_evaluate_map_puts_it_all_together():
    good = evaluate_map(two_islands(4), None, (20, 10), (20, 45), corridors=(((20, 12), (20, 45)),))
    assert good["ok"] and good["end_reached"] and not good["unreachable_islands"]
    bad = evaluate_map(two_islands(), None, (20, 10), (20, 45))
    assert not bad["ok"] and not bad["end_reached"] and len(bad["unreachable_islands"]) == 1


def test_budgets_follow_the_plan_and_the_catalogue():
    assert MeshBudget.for_grid(6).max_triangles == 1_100_000                         # 600 m: CP01
    assert MeshBudget.for_grid(24).max_triangles == 5_760_000                        # 2,4 km: 1 M/km2
    assert MeshBudget.for_mode("rally", 6) == MeshBudget(1_200_000, 21.0)
    assert MeshBudget.for_mode("tct", 3) == MeshBudget(350_000, 6.0)
    assert MeshBudget.for_mode(None, 6) == MeshBudget.for_grid(6)


@pytest.mark.parametrize("name", ["C01_camino"])
def test_existing_maps_are_within_budget_and_without_cracks(name):
    budget = check_budget(VARIANTS / name)
    assert budget["ok"], budget
    seams = check_seams(VARIANTS / name)
    assert seams["ok"] and seams["max_gap_uu"] < 0.5, seams


def test_a_shifted_chunk_is_a_crack(tmp_path):
    src = VARIANTS / "C01_camino"
    dst = tmp_path / "C01"
    shutil.copytree(src, dst)
    data = read_chunk(dst / "Chunks" / "r0c2.bin")
    moved = SimpleNamespace(**data)
    moved.vertices = data["vertices"] + np.array([0.0, 0.0, 30.0], dtype=np.float32)
    write_chunk(dst / "Chunks" / "r0c2.bin", moved)
    seams = check_seams(dst)
    assert not seams["ok"] and any((2, 0) in pair for pair in seams["cracks"])
    tight = check_budget(dst, MeshBudget(100_000, 50.0))
    assert not tight["ok"]


def test_c01_camino_se_guarda_decimado():
    """C01_camino sale de gen_terrain_path.py decimado a 5 cm: 3,8 M triangulos/km2 en la rejilla con
    colision antes (608 k en 0,16 km2), 1,42 M despues; ningun trozo de 100 m pasa de 30 k."""
    manifest = json.loads((VARIANTS / "C01_camino" / "manifest.json").read_text(encoding="utf-8"))
    grid = [c for c in manifest["cells"] if "col" in c]
    area_km2 = (manifest["grid"] * manifest["cell_uu"] / 100.0 / 1000.0) ** 2
    assert sum(c["triangles"] for c in grid) / area_km2 <= 1_600_000
    assert max(c["triangles"] for c in grid) <= 30_000
