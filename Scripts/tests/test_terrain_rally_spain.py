"""Criterios de aceptación de E01B_espana_rally (Rally punto a punto por España) sobre la variante YA GENERADA
(manifest y trozos de Scripts/terrain_volumes/Variants/E01B_espana_rally): Docs/Rally_E01B_y_Biplaza.md §2.

    uv run pytest Scripts/tests/test_terrain_rally_spain.py

La variante se regenera con `uv run --with pyfqmr python Scripts/gen_terrain_rally_spain.py`.
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from terrain_geo.build import VARIANTS  # noqa: E402
from terrain_geo.rally_corridor import MeshSampler, corridor_fingerprint, corridor_report, flat_widths  # noqa: E402
from terrain_vol.layout import UU_PER_M, WATER_M  # noqa: E402

NAME = "E01B_espana_rally"
OUT = VARIANTS / NAME
E01 = VARIANTS / "E01_espana"
# Huella del corredor antes de ampliar la rejilla a la península (#532, commit 1e8b43605), en coordenadas locales.
CORRIDOR_REF = Path(__file__).resolve().parent / "data" / "e01b_corredor.json"

pytestmark = pytest.mark.skipif(not (OUT / "manifest.json").exists(), reason=f"{NAME} sin generar")


def chunk_bytes(variant: Path, collision_only: bool = False) -> int:
    manifest = json.loads((variant / "manifest.json").read_text(encoding="utf-8"))
    return sum((variant / c["file"]).stat().st_size for c in manifest["cells"]
               if not collision_only or c.get("collision", True))


@pytest.fixture(scope="module")
def manifest() -> dict:
    return json.loads((OUT / "manifest.json").read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def sampler(manifest) -> MeshSampler:
    return MeshSampler(OUT, manifest)


@pytest.fixture(scope="module")
def report(sampler) -> dict:
    return corridor_report(OUT, sampler)


# ── Formato y registro ───────────────────────────────────────────────────────────
def test_registrada_en_el_indice_con_preview(manifest):
    index = json.loads((VARIANTS / "index.json").read_text(encoding="utf-8"))
    entry = next(e for e in index if e["name"] == NAME)
    assert entry["mode"] == "rally" and entry["recorrible"] is True
    assert (OUT / "preview.png").stat().st_size > 10_000
    assert manifest["format"] == "TNTM2" and manifest["mode"] == "rally" and manifest["closed"] is False
    assert all((OUT / c["file"]).exists() for c in manifest["cells"])


def test_agua_y_cajas_de_muerte_como_e01(manifest):
    e01 = json.loads((E01 / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["water_uu"] == e01["water_uu"]
    (box,), (ref,) = manifest["kill_boxes_uu"], e01["kill_boxes_uu"]
    assert box["yaw"] == ref["yaw"] == 0.0
    top, ref_top = box["center"][2] + box["extent"][2], ref["center"][2] + ref["extent"][2]
    assert top == pytest.approx(ref_top) and box["center"][2] - box["extent"][2] == pytest.approx(ref["center"][2] - ref["extent"][2])
    for cell in manifest["cells"]:                     # la caja cubre todos los trozos
        for axis in (0, 1):
            assert abs(cell["center_uu"][axis] - box["center"][axis]) + 5000.0 <= box["extent"][axis] + 1e-3


def test_trozos_no_pesan_mas_que_e01():
    """El corredor (lo que se pisa y se cocina como colisión) no pesa más que E01; con el fondo sin colisión de
    #532 (la península entera a la vista), no más del triple."""
    assert chunk_bytes(OUT, collision_only=True) <= chunk_bytes(E01)
    assert chunk_bytes(OUT) <= 3 * chunk_bytes(E01)


def test_exageracion_reducida_y_relieve_de_pirineo_a_playa(manifest, sampler):
    geo = manifest["geo"]
    assert 3.0 <= geo["exaggeration"] <= 6.0
    assert geo["exaggeration"] == pytest.approx(geo["k"] * geo["ground_m_per_game_m"], rel=1e-3)
    assert geo["ground_m_per_game_m"] == pytest.approx(400.0, rel=0.01)
    start_z, end_z = manifest["start_uu"][2] / UU_PER_M, manifest["end_uu"][2] / UU_PER_M
    assert start_z - WATER_M >= 8.0, "la salida está en el Pirineo"
    assert end_z - WATER_M <= 4.0, "la meta está junto al mar"
    hitos = manifest["markers_uu"]
    pyrenees = hitos["hito_canfranc"][0][2] / UU_PER_M
    meseta = hitos["hito_guadalajara"][0][2] / UU_PER_M
    assert pyrenees > meseta > end_z


# ── Calzada ──────────────────────────────────────────────────────────────────────
def test_longitud_de_calzada(report):
    assert 1800.0 <= report["road_m"] <= 2600.0
    assert 1800.0 <= report["checkpoints"]["course_m"] <= 2600.0
    assert report["missing_samples"] == 0
    assert report["min_above_water_m"] > 0.3


def test_pendientes_y_escalon(report):
    assert report["sustained_grade_deg"] <= 12.0          # ventanas de 30 m
    assert report["short_grade_deg"] <= 20.0              # ventanas de 4 m (< 30 m)
    assert report["max_step_m"] <= 0.4                    # entre muestras de 1 m


def test_ancho_y_radio(report):
    assert report["min_width_m"] >= 12.0
    assert report["min_radius_m"] >= 25.0


def test_el_ancho_detecta_un_corte():
    """Caso negativo del validador de ancho: un escalón de 1 m a 4 m a cada lado del eje deja 8 m de llano, no 12."""
    class Step:
        def top(self, xy):
            xy = np.asarray(xy).reshape(-1, 2)
            return np.where(np.abs(xy[:, 1]) > 4.0, 1.0, 0.0)
    pts = np.column_stack([np.arange(20.0), np.zeros(20)])
    widths = flat_widths(Step(), pts, np.zeros(20))
    assert widths.max() < 12.0


# ── Checkpoints ──────────────────────────────────────────────────────────────────
def test_checkpoints_sobre_la_calzada(manifest, report):
    cp = report["checkpoints"]
    assert cp["count"] >= 8 and all(len(c) == 4 for c in manifest["checkpoints_uu"])
    assert cp["ordered"]
    assert 150.0 <= cp["gap_min_m"] and cp["gap_max_m"] <= 250.0
    assert cp["max_offset_m"] <= 1.0 and cp["max_dz_m"] <= 0.3
    assert cp["max_yaw_err_deg"] <= 10.0
    assert cp["covered"] == 0, "ningún checkpoint en túnel ni en puente"


def test_salida_y_meta_en_los_extremos(manifest, report):
    cp = report["checkpoints"]
    assert cp["start_arc_m"] <= 80.0
    assert report["road_m"] - cp["end_arc_m"] <= 50.0
    road = np.asarray(manifest["road_uu"]) / UU_PER_M
    start, end = np.array(manifest["start_uu"][:2]) / UU_PER_M, np.array(manifest["end_uu"][:2]) / UU_PER_M
    assert np.hypot(*(start - road[0, :2])) < np.hypot(*(start - road[-1, :2]))
    assert math.hypot(*(end - road[-1, :2])) < math.hypot(*(end - road[0, :2]))


# ── Fondo (#532) ─────────────────────────────────────────────────────────────────
CLEAR_LAND_M = 50.0          # cota real (m) que seguro da tierra sobre el agua (el borde de la costa se difumina)
PROBE_M = 4.0


def land_cells(manifest: dict) -> set[tuple[int, int]]:
    """Trozos (col, fila) de la rejilla de la variante con alguna muestra (cada PROBE_M) de cota real > CLEAR_LAND_M
    fuera de África, leída directamente del MDE con la proyección del marco (sin pasar por el generador)."""
    from terrain_geo import rally_spain as rs
    from terrain_geo.fetch_spain import load_dem
    frame, dem = rs.make_frame(), load_dem()
    ox, oy = np.asarray(manifest["offset_uu"]) / UU_PER_M
    n = int(round(100.0 / PROBE_M)) + 1
    out = set()
    for row in range(manifest["geo"]["rows"]):
        for col in range(manifest["geo"]["cols"]):
            x = row * 100.0 - 50.0 + PROBE_M * np.arange(n) - ox
            y = col * 100.0 - 50.0 + PROBE_M * np.arange(n) - oy
            X, Y = np.meshgrid(x, y, indexing="ij")
            if ((rs.sample_dem(dem, frame, X, Y) > CLEAR_LAND_M) & ~rs.africa_mask(frame, X, Y)).any():
                out.add((col, row))
    return out


def cell_of(manifest: dict, lon: float, lat: float) -> tuple[int, int]:
    """(col, fila) de la variante que contiene el punto (lon, lat)."""
    from terrain_geo import rally_spain as rs
    X, Y = rs.make_frame().projection.to_game(lon, lat)
    ox, oy = np.asarray(manifest["offset_uu"]) / UU_PER_M
    return int(math.floor((float(Y) + oy + 50.0) / 100.0)), int(math.floor((float(X) + ox + 50.0) / 100.0))


def missing_land(manifest: dict, land: set[tuple[int, int]]) -> set[tuple[int, int]]:
    return land - {(c["col"], c["row"]) for c in manifest["cells"]}


@pytest.fixture(scope="module")
def land(manifest) -> set[tuple[int, int]]:
    return land_cells(manifest)


def test_todas_las_celdas_de_tierra_tienen_trozo(manifest, land):
    assert len(land) > 380, "la península entera (unos 580 000 km², trozos de 40 x 40 km) con Baleares"
    assert missing_land(manifest, land) == set()


PENINSULA_EXTREMOS = (("Finisterre", -9.27, 42.89), ("Cabo da Roca", -9.49, 38.78), ("Cabo de São Vicente", -8.98, 37.02),
                      ("Tarifa", -5.60, 36.02), ("Estaca de Bares", -7.68, 43.78), ("Cabo de Creus", 3.31, 42.32),
                      ("Menorca (Favàritx)", 4.26, 39.99), ("Ibiza", 1.43, 38.98), ("Cabo de Gata", -2.19, 36.73))


def test_peninsula_entera_con_portugal_y_baleares(manifest):
    cells = {(c["col"], c["row"]) for c in manifest["cells"]}
    geo = manifest["geo"]
    for name, lon, lat in PENINSULA_EXTREMOS:
        col, row = cell_of(manifest, lon, lat)
        assert 0 <= col < geo["cols"] and 0 <= row < geo["rows"], f"{name} fuera de la rejilla"
        assert (col, row) in cells, f"{name} sin trozo"


def test_africa_es_mar(manifest):
    cells = {(c["col"], c["row"]) for c in manifest["cells"]}
    for name, lon, lat in (("Argel", 3.06, 36.75), ("Orán", -0.64, 35.70), ("Tánger", -5.81, 35.77)):
        assert cell_of(manifest, lon, lat) not in cells, f"{name} tiene trozo"


def test_corredor_igual_que_antes_de_ampliar(manifest):
    """Mismos trozos con colisión (byte a byte) y mismas coordenadas de calzada, checkpoints, salida, meta y
    marcadores que antes de #532, quitado offset_uu (filas y columnas enteras)."""
    ox, oy = manifest["offset_uu"]
    assert ox % manifest["cell_uu"] == 0 and oy % manifest["cell_uu"] == 0 and oy > 0, "la rejilla crece al oeste"
    reference = json.loads(CORRIDOR_REF.read_text(encoding="utf-8"))
    assert corridor_fingerprint(OUT, manifest) == reference


def test_huella_detecta_un_corredor_movido(manifest):
    """Caso negativo: 0,1 uu en un punto de la calzada o un desplazamiento mal declarado cambian la huella."""
    reference = json.loads(CORRIDOR_REF.read_text(encoding="utf-8"))
    road = [list(p) for p in manifest["road_uu"]]
    road[100][0] += 0.1
    assert corridor_fingerprint(OUT, {**manifest, "road_uu": road}) != reference
    ox, oy = manifest["offset_uu"]
    assert corridor_fingerprint(OUT, {**manifest, "offset_uu": [ox, oy - manifest["cell_uu"]]}) != reference


def test_detecta_una_celda_de_tierra_sin_trozo(manifest, land):
    """Caso negativo: sin uno de los trozos de tierra, el control lo encuentra."""
    cell = next(iter(sorted(land)))
    cut = {**manifest, "cells": [c for c in manifest["cells"] if (c["col"], c["row"]) != cell]}
    assert missing_land(cut, land) == {cell}


def test_fondo_sin_colision_y_corredor_con_ella(manifest):
    road = np.asarray(manifest["road_uu"])[:, :2] / UU_PER_M
    on_road = {(int(round(y / 100.0)), int(round(x / 100.0))) for x, y in road}
    cells = {(c["col"], c["row"]): c for c in manifest["cells"]}
    assert on_road <= set(cells)
    assert all(cells[k].get("collision", True) for k in on_road), "la calzada pisa trozos con colisión"
    background = [c for c in manifest["cells"] if c.get("background")]
    assert background and all(c["collision"] is False for c in background)
    assert all(c.get("collision", True) for c in manifest["cells"] if not c.get("background"))


def test_presupuesto_de_triangulos(manifest):
    total = sum(c["triangles"] for c in manifest["cells"])
    tris = manifest["triangles"]
    assert total == tris["total"] == tris["corridor"] + tris["background"]
    assert total <= 1_500_000


def test_fondo_sin_lecho_marino(manifest):
    """Ningún triángulo de fondo queda entero bajo el agua (sus cuadros se veían a través del agua translúcida)."""
    from terrain_vol.export import read_chunk
    clip_uu = (WATER_M - 0.5) * UU_PER_M
    for cell in (c for c in manifest["cells"] if c.get("background")):
        chunk = read_chunk(OUT / cell["file"])
        z = chunk["vertices"][chunk["triangles"].astype(np.int64)][:, :, 2]
        assert len(z) > 0 and (z.max(axis=1) >= clip_uu - 1.0).all(), cell["name"]
