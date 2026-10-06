"""Genera un mapa "camino primero" a partir de un boceto del equipo de diseno (#875) en
Scripts/terrain_volumes/Variants/<nombre>/ (trozos TNTM2, corona, manifest, vistas, boceto y lamina) y
lo anade a Variants/index.json (desplegable de ATN_MapVariantLoader).

    uv run --with numpy --with scipy --with pillow --with scikit-image --with pyfqmr python Scripts/gen_terrain_sketch.py
        D01_boceto (el unico preset por ahora: terrain_path/sketch_d01.py)
    ... gen_terrain_sketch.py --medir
        solo mide el recorrido sobre el boceto (pixeles y metros con la escala del preset)

Determinista: el boceto, el preset y la semilla fijan el resultado. Comprobaciones (sketch_io.check):
recorrido a pie de la salida al mar, cada tunel de boca a boca en los dos sentidos, salida de la
hondonada, sin pisar las paredes, cotas de las zonas en el orden de la leyenda y altura de la cueva.
"""

from __future__ import annotations

import argparse
import json
import shutil
import time
from pathlib import Path

import numpy as np
from scipy import ndimage

from gen_terrain_path import DECIMATE_M, update_index
from gen_terrain_volume import zone_map
from terrain_path import sketch_io
from terrain_path.outer import DECIMATE_M as OUTER_DECIMATE_M
from terrain_path.sketch import SEA, class_map, load_rgb, route_length_px
from terrain_path.sketch_d01 import D01_SPEC
from terrain_path.sketch_model import SCRIPTS, SketchModel
from terrain_vol.export import write_map
from terrain_vol.layout import UU_PER_M, WATER_M

VARIANTS = SCRIPTS / "terrain_volumes" / "Variants"
PRESETS = {D01_SPEC.name: D01_SPEC}
MAX_CAVE_M = 10.0


def measure(spec) -> None:
    labels = class_map(spec, load_rgb(SCRIPTS / spec.image))
    goal = ndimage.binary_dilation(labels == SEA, iterations=1) & (labels == spec.beach_class)
    px = route_length_px(labels, spec.zone_classes, spec.start_px, goal)
    print(f"{spec.name}: recorrido sobre el boceto {px:.0f} px = {px * spec.m_per_px:.0f} m "
          f"a {spec.m_per_px} m/px (1000 m -> {1000.0 / px:.3f} m/px)")


def generate(spec, decimate_m: float, outer_decimate_m: float) -> dict:
    t0 = time.time()
    model = SketchModel(spec)
    chunks = sketch_io.build_chunks(model)
    result = sketch_io.check(model, chunks)
    main_pts, main_m = sketch_io.route(model, result["seen"], use_tunnels=False)
    _, short_m = sketch_io.route(model, result["seen"], use_tunnels=True)
    model.route.points = main_pts
    caves = {ax["name"]: round(sketch_io.cave_clearance(model, ax), 2) for ax in model.tunnel_axes}
    table = sketch_io.zone_table(model, result["top"])
    order_ok = sketch_io.legend_order_ok(table)
    ok = result["ok"] and order_ok and max(caves.values()) <= MAX_CAVE_M
    triangles_full = sum(len(c.triangles) for c in chunks.values())
    if decimate_m > 0.0:
        from terrain_vol.decimate import decimate_chunks
        chunks = decimate_chunks(chunks, decimate_m)
    top = result["top"]
    s_ij, e_ij = sketch_io.index_of(model, model.start), sketch_io.index_of(model, model.end)
    out = VARIANTS / spec.name
    grid = max(spec.rows, spec.cols)
    mouths = [[round(float(c) * UU_PER_M, 1) for c in ax["pts"][int(np.argmin(np.abs(ax["s"] - s)))]]
              for ax in model.tunnel_axes for s in ax["covered"]]
    write_map(out, spec.name, spec.seed, chunks, (*model.start, float(top[s_ij])), (*model.end, float(top[e_ij])),
              zone_map(model, chunks, grid=grid), main_pts[::4], style=None,
              extra_manifest={"description": spec.description, "recorrible": ok, "cols": spec.cols, "rows": spec.rows,
                              "kill_boxes_uu": [], "jellyfish_uu": [], "tunnel_mouths_uu": mouths,
                              "sketch": {"image": "boceto.jpeg", "m_per_px": spec.m_per_px,
                                         "ref_px": list(spec.ref_px), "ref_world_m": list(spec.ref_world)},
                              "route_main_m": round(main_m, 1), "zones": table},
              grid=grid)
    manifest_path = out / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["cells"] += sketch_io.write_outer_rect(model, out, spec.name, outer_decimate_m)
    manifest_path.write_text(json.dumps(manifest, indent=1, ensure_ascii=False), encoding="utf-8")
    shutil.copyfile(SCRIPTS / spec.image, out / "boceto.jpeg")
    sketch_io.lamina(model, top, main_pts, out / "lamina.png")
    grid_tris = [len(c.triangles) for c in chunks.values()]
    area_km2 = spec.rows * spec.cols * 0.01
    return {"name": spec.name, "seed": spec.seed, "description": spec.description, "ok": ok,
            "size_mb": round(sum(f.stat().st_size for f in out.rglob("*") if f.is_file()) / (1024 * 1024), 2),
            "time_s": round(time.time() - t0, 1), "meta": result["meta"], "paredes_pisadas": result["paredes_pisadas"],
            "salida_hondonada": result["salida_hondonada"], "tuneles": result["tuneles"], "cuevas_alto_m": caves,
            "principal_m": round(main_m, 1), "mas_corto_m": round(short_m, 1),
            "sprint_s": round(main_m / sketch_io.SPRINT_MS), "andando_s": round(main_m / sketch_io.WALK_MS),
            "zonas": table, "orden_leyenda": order_ok, "triangles_full": triangles_full,
            "triangles": sum(grid_tris), "max_chunk": max(grid_tris), "tri_per_km2": round(sum(grid_tris) / area_km2),
            "start": [round(float(v), 1) for v in model.start], "end": [round(float(v), 1) for v in model.end],
            "start_z": round(float(top[s_ij]) - WATER_M, 2)}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("name", nargs="?", default=D01_SPEC.name, choices=sorted(PRESETS))
    parser.add_argument("--medir", action="store_true", help="solo mide el recorrido sobre el boceto")
    parser.add_argument("--decimate-cm", type=float, default=DECIMATE_M * 100.0)
    parser.add_argument("--outer-decimate-cm", type=float, default=OUTER_DECIMATE_M * 100.0)
    args = parser.parse_args()
    spec = PRESETS[args.name]
    if args.medir:
        measure(spec)
        return
    r = generate(spec, args.decimate_cm / 100.0, args.outer_decimate_cm / 100.0)
    update_index([r])
    report = {k: v for k, v in r.items() if k not in ("description",)}
    print(json.dumps(report, indent=1, ensure_ascii=False))
    if not r["ok"]:
        raise SystemExit(f"{r['name']}: NO VALIDO")


if __name__ == "__main__":
    main()
