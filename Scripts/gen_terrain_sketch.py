"""Genera un mapa "camino primero" a partir de un boceto del equipo de diseno (#875) en
Scripts/terrain_volumes/Variants/<nombre>/ (trozos TNTM2, corona, manifest, boceto y validacion.npz), lo
anade a Variants/index.json (desplegable de ATN_MapVariantLoader) y deja la lamina en Docs/Mapas/.

    uv run --with numpy --with scipy --with pillow --with scikit-image --with pyfqmr python Scripts/gen_terrain_sketch.py
        D01_boceto (el unico preset por ahora: terrain_path/sketch_d01.py), unos 10 minutos
    ... gen_terrain_sketch.py --medir
        solo mide el recorrido sobre el boceto (el mas corto y el del centro, con la escala del preset)

Determinista: el boceto, el preset y la semilla fijan el resultado. Comprobaciones:
  - sobre el volumen (sketch_io.check, como gen_terrain_path.check en C01): recorrido a pie de la salida al
    mar, cada tunel de boca a boca en los dos sentidos, salida de la hondonada y ninguna pared pisada;
  - criterios de #875 (acceptance): recorrido por el centro de 900 a 1100 m, mesetas de 10 grados como
    mucho, cuestas con 5 grados de margen sobre lo andable, malla del suelo sin escalones, tuneles con suelo
    y techo de 3 a 10 m, paredes mas altas que en C01 y no mas triangulos que C01.
Sale con error si algo no se cumple. Los tests (Scripts/tests/test_terrain_sketch.py) rehacen las medidas
sobre lo escrito.
"""

from __future__ import annotations

import argparse
import json
import shutil
import time
from pathlib import Path

import numpy as np
from scipy import ndimage

from gen_terrain_path import update_index
from gen_terrain_volume import zone_map
from terrain_path import sketch_io, sketch_report, sketch_sheet
from terrain_path import sketch_metrics as sm
from terrain_path.sketch_decimate import decimate_chunks_split
from terrain_path.sketch import SEA, class_map, load_rgb, route_length_px
from terrain_path.sketch_d01 import D01_SPEC
from terrain_path.sketch_metrics import centerline
from terrain_path.sketch_model import SCRIPTS, SketchModel
from terrain_vol.export import write_map
from terrain_vol.layout import UU_PER_M

VARIANTS = SCRIPTS / "terrain_volumes" / "Variants"
PRESETS = {D01_SPEC.name: D01_SPEC}
REPO = SCRIPTS.parent
MAX_CAVE_M = 10.0                    # decisión del director (#875): cueva de 10 m de alto como máximo
MIN_CAVE_M = 3.0                     # tortuga de 1,8 m con margen
PLATEAU_MAX_DEG = 10.0
RAMP_MAX_DEG = 25.0                  # decisión del director del 07-10: cuestas de 25 grados como mucho
HOLLOW_DROP_M = 10.0                 # la hondonada, al menos esto por debajo de la meseta media
ROUTE_TARGET_M = 600.0               # decisión del director del 07-10 (antes, 1000 m)
ROUTE_RANGE_M = (550.0, 650.0)


def measure(spec) -> None:
    """Recorrido sobre el boceto: el más corto (en píxeles) y el del centro de lo transitable (en metros
    con la escala del preset), que es el que fija la escala (unos 600 m, decisión del director)."""
    labels = class_map(spec, load_rgb(SCRIPTS / spec.image))
    goal = ndimage.binary_dilation(labels == SEA, iterations=1) & (labels == spec.beach_class)
    px = route_length_px(labels, spec.zone_classes, spec.start_px, goal)
    walk = np.isin(labels, spec.zone_classes)
    start = (int(round(spec.start_px[1])), int(round(spec.start_px[0])))
    _, center_m = centerline(walk, start, goal, cell_m=spec.m_per_px)
    print(f"{spec.name}: a {spec.m_per_px} m/px, recorrido mas corto {px:.0f} px = {px * spec.m_per_px:.0f} m; "
          f"por el centro {center_m:.0f} m ({ROUTE_TARGET_M:.0f} m por el centro -> {spec.m_per_px * ROUTE_TARGET_M / center_m:.3f} m/px)")


def acceptance(metrics: dict, ref: dict) -> dict[str, bool]:
    """Criterios de aceptación de #875 sobre las métricas (sketch_report.measure) y la referencia de C01."""
    slopes, tri_floor, walls = metrics["pendientes"], metrics["triangulos_suelo"], metrics["pared"]
    ramp_limit = min(sm.WALKABLE_FLOOR_DEG - sm.SLOPE_MARGIN_DEG, RAMP_MAX_DEG)
    tunnels_ok = all(t["sin_suelo"] == 0 and t["sin_techo"] == 0 and t["alto_max_m"] <= MAX_CAVE_M
                     and t["alto_min_m"] >= MIN_CAVE_M and t["pendiente_max_deg"] < ramp_limit
                     for t in metrics["tuneles"].values())
    return {
        "recorrido": ROUTE_RANGE_M[0] <= metrics["recorrido_m"] <= ROUTE_RANGE_M[1],
        "mesetas": slopes["mesetas"]["max_deg"] <= PLATEAU_MAX_DEG,
        "cuestas": slopes["cuestas"]["max_deg"] < ramp_limit,
        "malla_suelo": tri_floor["mas_empinados_que_limite"] == 0 and tri_floor["escalones"] == 0,
        "tuneles": tunnels_ok,
        "pared": walls["media_m"] > ref["pared"]["media_m"],
        "hondonada": metrics["hondonada_bajo_media_m"] >= HOLLOW_DROP_M,
        "triangulos": metrics["triangulos_trozos"] <= ref["triangulos_trozos"]
        and metrics["triangulos_total"] <= ref["triangulos_total"],
    }


def _write_variant(model, chunks: dict, top: np.ndarray, main_pts: np.ndarray, main_m: float, table: list,
                   axes: list, outer_decimate_m: float) -> Path:
    """Trozos, corona, manifest y boceto de la variante; devuelve la ruta del manifest."""
    spec = model.spec
    s_ij, e_ij = sketch_io.index_of(model, model.start), sketch_io.index_of(model, model.end)
    out = VARIANTS / spec.name
    grid = max(spec.rows, spec.cols)
    mouths = [[round(float(c) * UU_PER_M, 1) for c in ax["pts"][int(np.argmin(np.abs(ax["s"] - s)))]]
              for ax in model.tunnel_axes for s in ax["covered"]]
    write_map(out, spec.name, spec.seed, chunks, (*model.start, float(top[s_ij])), (*model.end, float(top[e_ij])),
              zone_map(model, chunks, grid=grid), main_pts[::4], style=None,
              extra_manifest={"description": spec.description, "decisiones_director": list(spec.extra.get("decisions", ())),
                              "recorrible": False, "cols": spec.cols,
                              "rows": spec.rows, "kill_boxes_uu": [], "jellyfish_uu": [], "tunnel_mouths_uu": mouths,
                              "sketch": {"image": "boceto.jpeg", "m_per_px": spec.m_per_px,
                                         "ref_px": list(spec.ref_px), "ref_world_m": list(spec.ref_world)},
                              "route_main_m": round(main_m, 1), "zones": table, "tuneles": axes,
                              "meta": sketch_report.finish_block(model),
                              "recorrido_uu": [[round(float(c) * UU_PER_M, 1) for c in p] for p in main_pts[::25]]},
              grid=grid)
    manifest_path = out / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["cells"] += sketch_io.write_outer_rect(model, out, spec.name, outer_decimate_m)
    manifest_path.write_text(json.dumps(manifest, indent=1, ensure_ascii=False), encoding="utf-8")
    shutil.copyfile(SCRIPTS / spec.image, out / "boceto.jpeg")
    return manifest_path


def _write_sheet(model, top: np.ndarray, main_pts: np.ndarray, metrics: dict, ref: dict) -> Path:
    sheet = REPO / "Docs" / "Mapas" / f"{model.spec.name.split('_')[0]}_lamina.png"
    sheet.parent.mkdir(parents=True, exist_ok=True)
    cave = metrics["tuneles"]["cueva"]
    caption = (f"Recorrido por el centro {metrics['recorrido_m']:.0f} m; cuestas hasta "
               f"{metrics['pendientes']['cuestas']['max_deg']:.0f} grados; pared media {metrics['pared']['media_m']:.1f} m "
               f"(C01 {ref['pared']['media_m']:.1f} m); cueva de {cave['alto_min_m']:.1f}-{cave['alto_max_m']:.1f} m de alto.")
    sketch_sheet.lamina(model, top, main_pts, sheet, caption)
    return sheet


def generate(spec, decimate_m: float, outer_decimate_m: float, far_decimate_m: float) -> dict:
    t0 = time.time()
    model = SketchModel(spec)
    chunks = sketch_io.build_chunks(model)
    result = sketch_io.check(model, chunks)
    main_pts, main_m = sketch_io.route(model, result["seen"], result["top"], use_tunnels=False)
    _, short_m = sketch_io.route(model, result["seen"], result["top"], use_tunnels=True)
    model.route.points = main_pts
    caves = {ax["name"]: round(sketch_io.cave_clearance(model, ax), 2) for ax in model.tunnel_axes}
    table = sketch_io.zone_table(model, result["top"])
    order_ok = sketch_io.legend_order_ok(table)
    triangles_full = sum(len(c.triangles) for c in chunks.values())
    # El volumen se comprueba sin decimar; despues, el fondo de vistas con mas error que el resto.
    chunks = decimate_chunks_split(model, chunks, decimate_m, far_decimate_m)
    top = result["top"]
    metrics, saved = sketch_report.measure(model, top, chunks, main_m, short_m)
    manifest_path = _write_variant(model, chunks, top, main_pts, main_m, table, saved["axes"], outer_decimate_m)
    metrics.update(sketch_report.triangle_counts(manifest_path))
    ref = sketch_report.c01_reference(VARIANTS)
    checks = acceptance(metrics, ref)
    ok = result["ok"] and order_ok and max(caves.values()) <= MAX_CAVE_M and all(checks.values())
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["recorrible"] = bool(result["ok"])
    manifest["metricas"] = {**metrics, "referencia_c01": ref, "criterios": checks, "cuevas_alto_volumen_m": caves}
    manifest_path.write_text(json.dumps(manifest, indent=1, ensure_ascii=False), encoding="utf-8")
    sketch_report.save_grids(manifest_path.parent, saved["grids"])
    sheet = _write_sheet(model, top, main_pts, metrics, ref)
    out = manifest_path.parent
    return {"name": spec.name, "seed": spec.seed, "description": spec.description, "ok": ok,
            "size_mb": round(sum(f.stat().st_size for f in out.rglob("*") if f.is_file()) / (1024 * 1024), 2),
            "time_s": round(time.time() - t0, 1), "meta": result["meta"], "paredes_pisadas": result["paredes_pisadas"],
            "salida_hondonada": result["salida_hondonada"], "tuneles": result["tuneles"], "cuevas_alto_m": caves,
            "principal_m": round(main_m, 1), "con_tuneles_m": round(short_m, 1),
            "sprint_s": round(main_m / sketch_io.SPRINT_MS), "andando_s": round(main_m / sketch_io.WALK_MS),
            "zonas": table, "orden_leyenda": order_ok, "triangles_full": triangles_full,
            "metricas": manifest["metricas"], "lamina": str(sheet)}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("name", nargs="?", default=D01_SPEC.name, choices=sorted(PRESETS))
    parser.add_argument("--medir", action="store_true", help="solo mide el recorrido sobre el boceto")
    parser.add_argument("--decimate-cm", type=float, default=None, help="por defecto, el del preset")
    parser.add_argument("--outer-decimate-cm", type=float, default=None, help="por defecto, el del preset")
    parser.add_argument("--far-decimate-cm", type=float, default=None,
                        help="error de la decimacion del fondo de vistas, tras la cresta (por defecto, el del preset)")
    args = parser.parse_args()
    spec = PRESETS[args.name]
    if args.medir:
        measure(spec)
        return
    def metres(cm, default):
        return default if cm is None else cm / 100.0

    r = generate(spec, metres(args.decimate_cm, spec.decimate_m), metres(args.outer_decimate_cm, spec.outer_decimate_m),
                 metres(args.far_decimate_cm, spec.far_decimate_m))
    update_index([r])
    report = {k: v for k, v in r.items() if k not in ("description",)}
    print(json.dumps(report, indent=1, ensure_ascii=False))
    if not r["ok"]:
        raise SystemExit(f"{r['name']}: NO VALIDO")


if __name__ == "__main__":
    main()
