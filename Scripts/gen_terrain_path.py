"""Genera mapas "camino primero" (Docs/Diseno_Terreno_CaminoPrimero.md) en
Scripts/terrain_volumes/Variants/<nombre>/ (trozos TNTM2, manifest, vistas) y los pone al
principio de Variants/index.json (el desplegable de ATN_MapVariantLoader en LVL_MapVariants).

    uv run --with numpy --with scipy --with pillow --with scikit-image --with pyfqmr python Scripts/gen_terrain_path.py
        C01 (por defecto)
    ... gen_terrain_path.py --catalog [C05_muchos_lazos ...]
        el catalogo de 30 (terrain_path/variants.py), o solo los nombres dados

Los trozos se deciman al final (terrain_vol/decimate.py, necesita pyfqmr: anade --with pyfqmr al uv
run) con un error maximo de --decimate-cm (5 cm; la corona sin colision, --outer-decimate-cm, 25 cm);
0 = sin decimar. El borde de cada trozo no se mueve: las costuras siguen exactas.

Cada mapa se comprueba: se llega a pie (o saltando) del inicio al final, se alcanzan todos los
lazos y no se pisa el fondo de vistas. Si no, se reintenta con otra semilla (hasta 3 veces).
"""

from __future__ import annotations

import argparse
import json
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

import numpy as np

from gen_terrain_volume import build_all, global_standable, ground_level, walk, world_index, zone_map
from terrain_path.layout import GRID, UU_PER_M
from terrain_path.canyon import kill_boxes_uu
from terrain_path.model import PathModel, walkable
from terrain_path.outer import DECIMATE_M as OUTER_DECIMATE_M, write_outer
from terrain_path.placement_io import carry_placements
from terrain_path.style import C01_SEED, C01_STYLE, PathStyle
from terrain_path.variants import PATH_VARIANTS
from terrain_vol.export import global_top, write_map
from terrain_vol.mesh import z_levels

VARIANTS = Path(__file__).resolve().parent / "terrain_volumes" / "Variants"
RESEED_STEP = 1000
MAX_TRIES = 10           # con 3, 18 de los 30 del catalogo salian no recorribles o sin camino
DECIMATE_M = 0.05        # error maximo de la decimacion de los trozos con colision


def walk_with_links(standable: np.ndarray, start: tuple[int, int, int], links) -> np.ndarray:
    """walk() mas los saltos de medusa: si se llega al pie de un escalon, se sigue desde arriba."""
    seen = walk(standable, start) if start[2] >= 0 else np.zeros_like(standable)
    pending = list(links)
    while pending:
        progress = False
        for link in list(pending):
            (li, lj), (hi, hj) = world_index(link[0]), world_index(link[1])
            if not seen[li - 1:li + 2, lj - 1:lj + 2].any():
                continue
            pending.remove(link)
            k = ground_level(standable, hi, hj)
            if k >= 0 and not seen[hi, hj, k]:
                seen |= walk(standable, (hi, hj, k))
            progress = True
        if not progress:
            break
    return seen


def check(model: PathModel, chunks) -> dict:
    """Recorrido real sobre la malla: final, lazos y vistas (celdas del fondo alcanzadas)."""
    standable = walkable(global_standable(chunks, grid=GRID), model.grid.height[1:-1, 1:-1], z_levels())
    standable = model.remove_deadly(standable, z_levels())          # caer al barranco es morir
    s_ij, e_ij = world_index(model.start), world_index(model.end)
    start = (*s_ij, ground_level(standable, *s_ij))
    seen = walk_with_links(standable, start, model.jump_links())
    flat = seen.any(axis=2)
    loops_ok = 0
    for loop in model.plan.graph.loops():
        i, j = world_index(loop.point_at(loop.length / 2.0))
        loops_ok += bool(flat[i - 2:i + 3, j - 2:j + 3].any())
    vista = int((flat & (model.region[1:-1, 1:-1] == 2)).sum())
    ok = bool(seen[e_ij].any()) and loops_ok == len(model.plan.graph.loops()) and vista == 0
    return {"ok": ok, "loops_ok": loops_ok, "vista": vista}


def build_one(name: str, seed: int, style: PathStyle, description: str, decimate_m: float = DECIMATE_M,
              outer_decimate_m: float = OUTER_DECIMATE_M) -> dict:
    t0 = time.time()
    model = None
    for attempt in range(MAX_TRIES):
        used = seed + attempt * RESEED_STEP
        try:
            model = PathModel(used, style)
        except RuntimeError as exc:                 # sin camino principal valido con esta semilla
            print(f"{name}: semilla {used} descartada ({exc})", flush=True)
            continue
        chunks = build_all(model, grid=GRID)
        result = check(model, chunks)
        if result["ok"]:
            break
    if model is None:
        raise RuntimeError(f"ninguna de las {MAX_TRIES} semillas da un camino principal valido")
    # La comprobacion mira el volumen (celdas pisables), no la malla: se decima despues, una vez.
    triangles_full = sum(len(c.triangles) for c in chunks.values())
    if decimate_m > 0.0:
        from terrain_vol.decimate import decimate_chunks
        chunks = decimate_chunks(chunks, decimate_m)
    top = global_top(chunks, grid=GRID)
    s_ij, e_ij = world_index(model.start), world_index(model.end)
    out = VARIANTS / name
    # write_map borra la carpeta: lo colocado (bloque "placements", #652) se guarda y se repone.
    placements = carry_placements(out / "manifest.json", used)
    write_map(out, name, used, chunks, (*model.start, float(top[s_ij])), (*model.end, float(top[e_ij])),
              zone_map(model, chunks, grid=GRID), model.route.points, style=style,
              extra_manifest={"description": description, "recorrible": result["ok"],
                              "kill_boxes_uu": [b for c in model.canyons for b in kill_boxes_uu(c)],
                              "jellyfish_uu": [[round(float(c) * UU_PER_M, 1) for c in st.jelly]
                                               for st in model.jump_steps]},
              grid=GRID)
    # Corona de terreno barato alrededor (sin colision): el final del mapa no se ve desde dentro.
    manifest_path = out / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["cells"] += write_outer(model, out, name, outer_decimate_m)
    if placements is not None:
        manifest["placements"] = placements
    manifest_path.write_text(json.dumps(manifest, indent=1), encoding="utf-8")
    g = model.plan.graph
    return {"name": name, "seed": used, "description": description, "ok": result["ok"],
            "size_mb": round(sum(f.stat().st_size for f in out.rglob("*") if f.is_file()) / (1024 * 1024), 2),
            "time_s": round(time.time() - t0, 1), "length": round(g.main.length), "loops": len(g.loops()),
            "crossings": len(model.plan.crossings), "tunnels": len(model.plan.hill_tunnels),
            "arches": len(model.arch_ranges), "canyon": len(model.canyons), "islands": len(model.river.islands) if model.river else 0,
            "streams": sum(p.stream for p in model.plan.profiles.values()),
            "lagoons": int(model.plan.profiles[0].lagoon is not None and model.plan.profiles[0].lagoon.max() > 0.5),
            "vista": result["vista"], "loops_ok": result["loops_ok"], "steps": len(model.jump_steps),
            "triangles_full": triangles_full, "triangles": sum(len(c.triangles) for c in chunks.values())}


def update_index(results: list[dict]) -> None:
    index_path = VARIANTS / "index.json"
    index = json.loads(index_path.read_text(encoding="utf-8")) if index_path.exists() else []
    fresh = {r["name"]: {"name": r["name"], "seed": r["seed"], "description": r["description"],
                         "recorrible": r["ok"], "size_mb": r["size_mb"]} for r in results}
    old = {e["name"]: e for e in index}
    old.update(fresh)
    camino = sorted((e for n, e in old.items() if n.startswith("C") and n[1:3].isdigit()), key=lambda e: e["name"])
    rest = [e for n, e in old.items() if not (n.startswith("C") and n[1:3].isdigit())]
    index_path.write_text(json.dumps(camino + rest, indent=1, ensure_ascii=False), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("names", nargs="*", help="con --catalog: solo estos nombres")
    parser.add_argument("--catalog", action="store_true", help="genera el catalogo C02-C31")
    parser.add_argument("--seed", type=int, default=C01_SEED, help="semilla de C01")
    parser.add_argument("--workers", type=int, default=3)
    parser.add_argument("--decimate-cm", type=float, default=DECIMATE_M * 100.0,
                        help="error maximo de la decimacion de los trozos (cm); 0 = sin decimar")
    parser.add_argument("--outer-decimate-cm", type=float, default=OUTER_DECIMATE_M * 100.0,
                        help="error maximo de la decimacion de la corona (cm); 0 = sin decimar")
    args = parser.parse_args()
    if not args.catalog:
        jobs = [(C01_STYLE.name, args.seed, C01_STYLE, C01_STYLE.description)]
    else:
        wanted = set(args.names)
        jobs = [(v.name, v.seed, v.style, v.description) for v in PATH_VARIANTS if not wanted or v.name in wanted]
    results = []
    with ProcessPoolExecutor(max_workers=max(1, min(args.workers, len(jobs)))) as pool:
        futures = {pool.submit(build_one, *job, args.decimate_cm / 100.0, args.outer_decimate_cm / 100.0): job[0]
                   for job in jobs}
        for future in as_completed(futures):
            try:
                r = future.result()
            except Exception as exc:                        # noqa: BLE001 (se informa y sigue)
                print(f"{futures[future]}: ERROR {type(exc).__name__}: {exc}", flush=True)
                continue
            results.append(r)
            print(f"{r['name']}: {'OK' if r['ok'] else 'NO VALIDO'} {r['time_s']}s semilla {r['seed']} "
                  f"principal {r['length']} m, {r['loops']} lazos ({r['loops_ok']} alcanzados), "
                  f"{r['crossings']} cruces, {r['canyon']} barrancos, {r['arches']} arcos, {r['tunnels']} tuneles, {r['islands']} islas, "
                  f"{r['lagoons']} lagunas, {r['streams']} arroyos, "
                  f"{r['steps']} escalones de medusa, vistas pisadas {r['vista']}, "
                  f"{r['triangles_full']} -> {r['triangles']} triangulos, {r['size_mb']} MB", flush=True)
    update_index(results)
    bad = [r["name"] for r in results if not r["ok"]]
    print(f"{len(results)} mapas, {sum(r['size_mb'] for r in results):.1f} MB; no validos: {bad or 'ninguno'}")


if __name__ == "__main__":
    main()
