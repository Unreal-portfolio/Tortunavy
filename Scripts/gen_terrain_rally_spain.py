"""Genera E01B_espana_rally (Rally punto a punto por España, Docs/Rally_E01B_y_Biplaza.md §2) en
Scripts/terrain_volumes/Variants/E01B_espana_rally/ (trozos TNTM2, manifest, vistas) y lo añade a index.json.

    uv run --with pyfqmr python Scripts/gen_terrain_rally_spain.py [--no-decimate]

El trazado y el terreno están en terrain_geo/rally_spain.py; la validación del corredor, sobre la variante ya
escrita, en terrain_geo/rally_corridor.py (la repite Scripts/tests/test_terrain_rally_spain.py).

Manifest (además de lo común de write_map): mode "rally", closed false, road_uu (eje cada 1 m, [x, y, z] en uu),
road_width_m, checkpoints_uu ([x, y, z, yaw], yaw = rumbo de la marcha en grados de Unreal) entre start_uu (línea
de salida, tras la recta de parrilla) y end_uu (meta, antes de la escapatoria), markers_uu (parrilla e hitos),
kill_boxes_uu (el agua de toda la rejilla), geo (proyección, escala, exageración y rejilla), offset_uu y checks
(el validador). Cada trozo de fondo (fuera del corredor, terrain_geo/rally_spain.py:BackgroundModel) lleva
"collision": false y "background": true: el cargador lo crea sin colisión y quien se sale del corredor cae a la caja
de muerte.

La rejilla abarca la península entera (rally_spain.Layout, #532): el corredor se genera en las coordenadas locales
de su marco y, al escribir, trozos y coordenadas se desplazan offset_uu (filas y columnas enteras), así que sus
trozos son los mismos ficheros con otro nombre. Scripts/tests/data/e01b_corredor.json guarda su huella
(rally_corridor.corridor_fingerprint): si el corredor cambia a propósito, se vuelve a escribir.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import time
from concurrent.futures import ProcessPoolExecutor
from dataclasses import replace

import numpy as np
from PIL import Image
from scipy import ndimage
from terrain_geo import rally_spain as rs
from terrain_geo.build import VARIANTS, dir_size_mb, kill_boxes_uu, update_index, write_credits
from terrain_geo.heightfield import ZONES
from terrain_geo.layout import SPAIN_REGION
from terrain_geo.rally_corridor import corridor_report
from terrain_vol.export import global_top, write_map
from terrain_vol.layout import CELL_SAMPLES, MAP_MIN_M, UU_PER_M, WATER_M
from terrain_vol.mesh import build_chunk

CHECKPOINT_EVERY_M = 200.0
DECIMATE_ERROR_M = 0.08            # trozos con la calzada o su talud
DECIMATE_FAR_M = 0.25              # trozos de fondo (a mas de NEAR_M del eje)
NEAR_M = 20.0
DECIMATE_BACKGROUND_M = 0.6        # trozos de fondo (sin colisión): solo se ven, de lejos
SEABED_CLIP_M = WATER_M - 0.5      # el fondo no lleva lecho marino: bajo el agua translúcida se veían sus cuadros
PREVIEW_MAX_PX = 2000
TRIANGLE_BUDGET = 1_500_000        # corredor + fondo (#532: península entera)

# Criterios de aceptación (los mismos que comprueba el test).
LIMITS = {"road_m": (1800.0, 2600.0), "sustained_grade_deg": 12.0, "short_grade_deg": 20.0, "max_step_m": 0.4,
          "min_width_m": 12.0, "min_radius_m": 25.0, "checkpoint_gap_m": (150.0, 250.0), "exaggeration": (3.0, 6.0)}


def sample_top(top: np.ndarray, pts: np.ndarray) -> np.ndarray:
    coords = [pts[:, 0] - MAP_MIN_M, pts[:, 1] - MAP_MIN_M]
    return ndimage.map_coordinates(top, coords, order=1, mode="nearest")


def checkpoint_indices(arc: np.ndarray, start: int, end: int) -> list[int]:
    """Índices del eje repartidos a partes iguales entre salida y meta, a unos CHECKPOINT_EVERY_M."""
    course = arc[end] - arc[start]
    parts = max(1, int(round(course / CHECKPOINT_EVERY_M)))
    marks = arc[start] + course * np.arange(1, parts) / parts
    return [int(np.searchsorted(arc, m)) for m in marks]


def yaw_deg(pts: np.ndarray, k: int) -> float:
    t = pts[min(k + 2, len(pts) - 1)] - pts[max(k - 2, 0)]
    return math.degrees(math.atan2(t[1], t[0]))


def uu(p, z: float, yaw: float | None = None) -> list[float]:
    out = [round(float(p[0]) * UU_PER_M, 1), round(float(p[1]) * UU_PER_M, 1), round(float(z) * UU_PER_M, 1)]
    return out + [round(yaw, 1)] if yaw is not None else out


def save_preview(path, rgb: np.ndarray) -> None:
    """Norte arriba y, como mucho, PREVIEW_MAX_PX de lado (la rejilla de la península pasa de 3 000 muestras)."""
    step = max(1, int(math.ceil(max(rgb.shape[:2]) / PREVIEW_MAX_PX)))
    Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)[::-1][::step, ::step]).save(path)


def render_preview(out, top: np.ndarray, layout: rs.Layout, built: set, road: np.ndarray, cps: list[int]) -> None:
    """Vista cenital (Norte arriba) de la rejilla de la variante: relieve sombreado, agua, trozos no generados en gris
    oscuro; preview_debug.png añade la calzada (rojo) y los checkpoints (amarillo)."""
    h, w = layout.rows * 100 + 1, layout.cols * 100 + 1
    t = top[:h, :w]
    gx, gy = np.gradient(t)
    light = np.clip((gx * 0.5 - gy * 0.35 + 1.0) / np.sqrt(gx * gx + gy * gy + 1.0) * 0.8, 0.0, 1.0)
    height = np.clip((t - WATER_M) / 40.0, 0.0, 1.0)[..., None]
    base = np.array([0.93, 0.84, 0.60]) * (1 - height) + np.array([0.62, 0.45, 0.30]) * height
    rgb = base * (0.35 + 0.65 * light)[..., None]
    rgb = np.where((t < WATER_M)[..., None], rgb * 0.25 + np.array([0.12, 0.35, 0.65]) * 0.75, rgb)
    mask = np.zeros((h, w), dtype=bool)
    for col, row in built:
        mask[row * 100:row * 100 + 101, col * 100:col * 100 + 101] = True
    rgb = np.where(mask[..., None], rgb, np.array([0.18, 0.18, 0.2]))
    save_preview(out / "preview.png", rgb)
    debug = rgb.copy()
    for k, (x, y) in enumerate(road):
        i, j = int(round(x - MAP_MIN_M)), int(round(y - MAP_MIN_M))
        if 0 <= i < h and 0 <= j < w:
            debug[max(0, i - 2):i + 3, max(0, j - 2):j + 3] = (0.9, 0.1, 0.1)
    for k in cps:
        i, j = int(round(road[k, 0] - MAP_MIN_M)), int(round(road[k, 1] - MAP_MIN_M))
        debug[max(0, i - 6):i + 7, max(0, j - 6):j + 7] = (1.0, 0.9, 0.1)
    save_preview(out / "preview_debug.png", debug)


def verdict(report: dict, exaggeration: float) -> dict[str, bool]:
    cp = report["checkpoints"]
    return {"road_m": LIMITS["road_m"][0] <= report["road_m"] <= LIMITS["road_m"][1],
            "sustained_grade": report["sustained_grade_deg"] <= LIMITS["sustained_grade_deg"],
            "short_grade": report["short_grade_deg"] <= LIMITS["short_grade_deg"],
            "step": report["max_step_m"] <= LIMITS["max_step_m"],
            "width": report["min_width_m"] >= LIMITS["min_width_m"],
            "radius": report["min_radius_m"] >= LIMITS["min_radius_m"],
            "on_mesh": report["missing_samples"] == 0 and report["min_above_water_m"] > 0.3,
            "checkpoints": (LIMITS["checkpoint_gap_m"][0] <= cp["gap_min_m"] and cp["gap_max_m"] <= LIMITS["checkpoint_gap_m"][1]
                            and cp["ordered"] and cp["covered"] == 0),
            "exaggeration": LIMITS["exaggeration"][0] <= exaggeration <= LIMITS["exaggeration"][1]}


def _decimate_one(args):
    vertices, normals, colors, triangles, error_m = args
    from terrain_vol.decimate import decimate_mesh
    return decimate_mesh(vertices, normals, colors, triangles, error_m)


def drop_seabed(chunk):
    """Quita los triángulos con los tres vértices por debajo de SEABED_CLIP_M y los vértices que quedan sueltos."""
    tris = chunk.triangles.astype(np.int64)
    keep = (chunk.vertices[tris][:, :, 2] >= SEABED_CLIP_M * UU_PER_M).any(axis=1)
    if keep.all():
        return chunk
    tris = tris[keep]
    used = np.unique(tris)
    remap = np.full(len(chunk.vertices), -1, dtype=np.int64)
    remap[used] = np.arange(len(used))
    return replace(chunk, vertices=chunk.vertices[used], normals=chunk.normals[used], colors=chunk.colors[used],
                   triangles=remap[tris].astype(np.uint32))


def build_background(model: rs.RallySpainModel, layout: rs.Layout, corridor, decimate: bool) -> dict:
    """Trozos de fondo (claves locales del marco): los de tierra de la rejilla ampliada fuera del corredor, decimados a
    DECIMATE_BACKGROUND_M (en paralelo) y sin lecho marino. Se sueltan las celdas pisables y los campos: el fondo no
    se valida ni se pisa."""
    background = rs.BackgroundModel(model, layout)
    chunks = {}
    for cell in background.cells(corridor):
        chunk = build_chunk(background, *cell)
        chunks[cell] = replace(chunk, standable=np.zeros((0, 0, 0), dtype=bool), fields=None)
    if not decimate:
        return {k: drop_seabed(c) for k, c in chunks.items()}
    keys = sorted(chunks)
    jobs = [(chunks[k].vertices, chunks[k].normals, chunks[k].colors, chunks[k].triangles, DECIMATE_BACKGROUND_M)
            for k in keys]
    with ProcessPoolExecutor(max_workers=max(1, min(8, (os.cpu_count() or 2) // 2))) as pool:
        results = list(pool.map(_decimate_one, jobs))
    return {k: drop_seabed(replace(chunks[k], vertices=v, normals=n, colors=c, triangles=t))
            for k, (v, n, c, t) in zip(keys, results)}


def mark_background(manifest: dict, background: set) -> None:
    """Los trozos de fondo, sin colisión (el cargador lee "collision")."""
    for cell in manifest["cells"]:
        if (cell["col"], cell["row"]) in background:
            cell["collision"] = False
            cell["background"] = True


def shift_cells(chunks: dict, layout: rs.Layout) -> dict:
    """De (col, fila) locales del marco a los de la rejilla ampliada."""
    return {(col + layout.col0, row + layout.row0): chunk for (col, row), chunk in chunks.items()}


def build(decimate: bool = True) -> dict:
    t0 = time.time()
    frame = rs.make_frame()
    layout = rs.make_layout(frame)
    model = rs.RallySpainModel(frame)
    gaps = model.cell_gaps()
    chunks = {cell: build_chunk(model, *cell) for cell in gaps}
    if decimate:
        from terrain_vol.decimate import decimate_chunks
        near = {c: m for c, m in chunks.items() if gaps[c] <= NEAR_M}
        far = {c: m for c, m in chunks.items() if gaps[c] > NEAR_M}
        chunks = {**decimate_chunks(near, DECIMATE_ERROR_M), **decimate_chunks(far, DECIMATE_FAR_M)}
    background = shift_cells(build_background(model, layout, gaps, decimate), layout)
    chunks = shift_cells(chunks, layout)
    corridor_tris = sum(len(m.triangles) for m in chunks.values())
    background_tris = sum(len(m.triangles) for m in background.values())
    chunks = {**chunks, **background}
    grid = layout.grid
    top = global_top(chunks, grid=grid)
    offset = layout.offset_m
    offset_uu = [float(v) * UU_PER_M for v in offset]
    local_road = model.road
    road = local_road + offset                     # en el mundo de la variante

    def wuu(p_local, zz: float, yaw: float | None = None) -> list[float]:
        """uu() en coordenadas locales y luego el desplazamiento: los mismos valores de antes de #532 más offset_uu."""
        v = uu(p_local, zz, yaw)
        return [round(v[0] + offset_uu[0], 1), round(v[1] + offset_uu[1], 1)] + v[2:]

    arc = rs.arc_length(local_road)                # el mismo arco que antes de #532, sin el desplazamiento
    z = sample_top(top, road)
    start = int(np.searchsorted(arc, rs.GRID_M))
    end = int(np.searchsorted(arc, arc[-1] - rs.RUNOFF_M))
    cps = checkpoint_indices(arc, start, end)
    hitos = {name: frame.projection.to_game(lon, lat) for name, lat, lon in rs.HITOS}
    hito_pts = {name: np.array([float(v) for v in xy]) for name, xy in hitos.items()}       # locales
    exaggeration = model.k * frame.projection.ground_m_per_game_m()
    extra = {
        "description": rs.DESCRIPTION, "mode": "rally", "closed": False, "laps": 1, "kill_boxes_uu": kill_boxes_uu(grid),
        "offset_uu": offset_uu,
        "z_range": [model.z_range.z_min_m, model.z_range.levels, model.z_range.step_m],
        "generator": {"generator": "rally_spain", "seed": rs.SEED, "hitos": [list(h) for h in rs.HITOS],
                      "bends": [list(map(list, b)) for b in rs.BENDS], "road_w_m": rs.ROAD_W_M,
                      "shoulder_m": rs.SHOULDER_M, "max_grade_deg": rs.MAX_GRADE_DEG, "band_m": rs.BAND_M,
                      "decimate_m": [DECIMATE_ERROR_M, DECIMATE_FAR_M] if decimate else 0.0,
                      "near_m": NEAR_M, "background_decimate_m": DECIMATE_BACKGROUND_M if decimate else 0.0},
        "triangles": {"corridor": corridor_tris, "background": background_tris,
                      "total": corridor_tris + background_tris, "budget": TRIANGLE_BUDGET},
        "geo": {"source": "ES_dem.png (E01)", "projection": "merc", "center_lonlat": list(frame.projection.center),
                "ground_m_per_game_m": round(frame.projection.ground_m_per_game_m(), 1),
                "exaggeration": round(exaggeration, 3), "k": model.k, "rows": layout.rows, "cols": layout.cols,
                "corridor_rows": frame.rows, "corridor_cols": frame.cols, "peninsula_lonlat": list(rs.PENINSULA_LONLAT)},
        "road_width_m": rs.ROAD_W_M, "road_uu": [wuu(p, zz) for p, zz in zip(local_road, z)],
        "checkpoints_uu": [wuu(local_road[k], z[k], yaw_deg(local_road, k)) for k in cps],
        "start_yaw": round(yaw_deg(local_road, start), 1),
        "tunnels": [], "decks": [], "bridges": [],
        "markers_uu": {"parrilla": [wuu(local_road[0], z[0]), wuu(local_road[start], z[start])],
                       **{f"hito_{name}": [wuu(p, float(sample_top(top, (p + offset)[None, :])[0]))]
                          for name, p in hito_pts.items()}},
    }
    out = VARIANTS / rs.NAME
    size = grid * (CELL_SAMPLES - 1) + 1
    zones = {zone: (np.ones((size, size)) if zone == "cliffs" else np.zeros((size, size))) for zone in ZONES}
    write_map(out, rs.NAME, rs.SEED, chunks, (*road[start], z[start]), (*road[end], z[end]), zones, road,
              extra_manifest=extra, grid=grid)
    write_credits(out, SPAIN_REGION.credits(("Trazado de Rally E01B: Scripts/gen_terrain_rally_spain.py.",)))
    render_preview(out, top, layout, set(chunks), road, cps)
    data = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
    mark_background(data, set(background))
    (out / "manifest.json").write_text(json.dumps(data, indent=1), encoding="utf-8")
    report = corridor_report(out)
    checks = verdict(report, exaggeration)
    ok = all(checks.values())
    data["checks"] = {"corridor": report, "verdict": checks}
    data["recorrible"] = ok
    (out / "manifest.json").write_text(json.dumps(data, indent=1), encoding="utf-8")
    size_mb = dir_size_mb(out)
    update_index(rs.NAME, rs.SEED, ok, size_mb, rs.DESCRIPTION, {"mode": "rally"})
    chunk_bytes = sum(f.stat().st_size for f in (out / "Chunks").iterdir())
    return {"ok": ok, "time_s": round(time.time() - t0, 1), "grid": [layout.rows, layout.cols],
            "offset_uu": offset_uu, "cells": len(chunks), "background_cells": len(background),
            "triangles": extra["triangles"], "size_mb": size_mb,
            "chunk_bytes": chunk_bytes, "exaggeration": round(exaggeration, 2), "report": report, "checks": checks}


def main() -> None:
    parser = argparse.ArgumentParser(description="Genera E01B_espana_rally (Rally punto a punto por España).")
    parser.add_argument("--no-decimate", action="store_true", help="no decimar los trozos (no necesita pyfqmr)")
    args = parser.parse_args()
    r = build(decimate=not args.no_decimate)
    print(json.dumps({k: v for k, v in r.items()}, indent=1, ensure_ascii=False))


if __name__ == "__main__":
    main()
