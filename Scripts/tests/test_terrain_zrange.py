"""Rango vertical configurable del voxelizado (terrain_vol.layout.ZRange): por defecto 128 niveles y sin
cambiar la malla del mapa vigente (C01_camino; Mapa01 esta obsoleto).

    uv run --with pytest --with numpy --with scipy --with pillow --with scikit-image \
        python -m pytest Scripts/tests/test_terrain_zrange.py
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from terrain_vol.export import write_chunk  # noqa: E402
from terrain_vol.layout import DEFAULT_Z_RANGE, LEGACY_Z_RANGE, STEP_Z_M, Z_MAX_M, Z_MIN_M, Z_SAMPLES, ZRange  # noqa: E402
from terrain_vol.mesh import build_chunk, model_z_range  # noqa: E402

VARIANTS = Path(__file__).resolve().parent.parent / "terrain_volumes" / "Variants"


def test_default_range_is_128_levels_from_the_same_floor():
    assert DEFAULT_Z_RANGE.levels == Z_SAMPLES == 128
    assert DEFAULT_Z_RANGE.z_min_m == Z_MIN_M and DEFAULT_Z_RANGE.step_m == STEP_Z_M
    assert DEFAULT_Z_RANGE.z_max_m == pytest.approx(53.5)
    assert LEGACY_Z_RANGE.levels == 89 and LEGACY_Z_RANGE.z_max_m == pytest.approx(Z_MAX_M)
    assert len(DEFAULT_Z_RANGE.z_values()) == 128


def test_covering_range_grows_only_when_the_terrain_needs_it():
    assert ZRange.covering(20.0) == DEFAULT_Z_RANGE
    tall = ZRange.covering(80.0, headroom_m=3.0)
    assert tall.z_max_m >= 83.0 and tall.levels > 128
    with pytest.raises(ValueError):
        ZRange(levels=1)


def test_a_model_can_fix_its_own_range():
    class Tall:
        z_range = ZRange(levels=200)

    assert model_z_range(Tall()).levels == 200
    assert model_z_range(object()) == DEFAULT_Z_RANGE


def _same_mesh(model, col: int, row: int) -> None:
    old = build_chunk(model, col, row, LEGACY_Z_RANGE)
    new = build_chunk(model, col, row)
    assert np.array_equal(old.vertices, new.vertices)
    assert np.array_equal(old.triangles, new.triangles)
    assert np.array_equal(old.colors, new.colors)
    assert np.allclose(old.top, new.top)
    assert new.standable.shape[2] == 128 and old.standable.shape[2] == 89
    assert np.array_equal(old.standable, new.standable[:, :, :89])


def test_path_map_mesh_does_not_change(tmp_path):
    from terrain_path.model import PathModel
    from terrain_path.style import C01_SEED, C01_STYLE
    model = PathModel(C01_SEED, C01_STYLE)           # C01_camino: el trozo con mas triangulos, lomas a 20 m
    _same_mesh(model, 2, 0)
    # C01_camino se guarda decimado (gen_terrain_path.py, 5 cm): se compara con el mismo proceso.
    pytest.importorskip("pyfqmr")
    from terrain_vol.decimate import decimate_chunks
    out = tmp_path / "r0c2.bin"
    write_chunk(out, decimate_chunks({(2, 0): build_chunk(model, 2, 0)})[(2, 0)])
    stored = VARIANTS / "C01_camino" / "Chunks" / "r0c2.bin"
    assert out.read_bytes() == stored.read_bytes(), f"{stored} ha cambiado"
