"""Coloca puzles, mecánicas, enemigos, nidos, botín y decorado en un mapa «camino primero» (#652)
y lo escribe en el bloque "placements" de su manifest, sin tocar lo colocado a mano.

    uv run python Scripts/place_terrain_path.py                      # C01_camino, semilla 652
    uv run python Scripts/place_terrain_path.py C05_muchos_lazos --seed 7
    uv run python Scripts/place_terrain_path.py --comprobar          # valida lo que ya hay, sin escribir
    uv run python Scripts/place_terrain_path.py --simular            # genera y valida, sin escribir

Rehace el modelo del camino con la semilla y el estilo del manifest (unos 15 s), genera con la
semilla de colocación, valida las reglas (terrain_path/placement_rules.py) y, si se cumplen, escribe
el manifest y la lámina placements.png junto a él. Con violaciones no escribe (salvo --forzar).
Formato del bloque: terrain_path/placement_io.py.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import fields
from pathlib import Path

from terrain_path.model import PathModel
from terrain_path.placement import PlacementResult, generate, placement_counts
from terrain_path.placement_catalog import PUZZLES
from terrain_path.placement_io import build_block, from_json, read_block, with_block, write_manifest
from terrain_path.placement_rules import route_danger, validate
from terrain_path.placement_sheet import puzzle_notes, render_sheet
from terrain_path.placement_site import Site, site_from_model
from terrain_path.style import PathStyle

VARIANTS = Path(__file__).resolve().parent / "terrain_volumes" / "Variants"
DEFAULT_SEED = 652
START_TOLERANCE_M = 1.0


def style_from_manifest(manifest: dict) -> PathStyle:
    raw = manifest.get("style")
    if not raw:
        raise SystemExit("el manifest no trae 'style': no es un mapa de terrain_path")
    names = {f.name for f in fields(PathStyle)}
    return PathStyle(**{k: tuple(v) if isinstance(v, list) else v for k, v in raw.items() if k in names})


def load_site(name: str) -> tuple[Site, dict, Path]:
    path = VARIANTS / name / "manifest.json"
    if not path.exists():
        raise SystemExit(f"no existe {path}")
    manifest = json.loads(path.read_text(encoding="utf-8"))
    model = PathModel(int(manifest["seed"]), style_from_manifest(manifest))
    start = [v / 100.0 for v in manifest["start_uu"][:2]]
    gap = ((model.start[0] - start[0]) ** 2 + (model.start[1] - start[1]) ** 2) ** 0.5
    if gap > START_TOLERANCE_M:
        raise SystemExit(f"el modelo rehecho no coincide con el manifest (salida a {gap:.1f} m): regenera el mapa")
    return site_from_model(model, name), manifest, path


def summarize(site: Site, result: PlacementResult) -> dict:
    items = result.all
    puzzles = [p for p in items if p.category == "puzzle"]
    routes = []
    for loop, parent, a, b in site.bifurcations():
        routes.append({"loop": loop.id, "parent": parent.id, "loop_m": round(loop.length, 1),
                       "alternative_m": round(b - a, 1),
                       "loop_danger_per_100m": round(100.0 * route_danger(site, items, loop.id, 0.0, loop.length)
                                                     / loop.length, 2),
                       "alternative_danger_per_100m": round(100.0 * route_danger(site, items, parent.id, a, b)
                                                            / max(b - a, 1.0), 2)})
    return {"counts": placement_counts(items),
            "puzzles_main": sum(p.line == 0 for p in puzzles),
            "puzzles_loops": sum(p.line != 0 for p in puzzles),
            "group_puzzles_main": sum(p.line == 0 and PUZZLES.get(p.kind) is not None
                                      and PUZZLES[p.kind].mode == "grupo" for p in puzzles),
            "routes": routes, "violations": len(result.violations)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("variant", nargs="?", default="C01_camino")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED, help="semilla de la colocación")
    parser.add_argument("--incluir-pendientes", action="store_true",
                        help="usa también las plantillas cuyo C++ falta (basket_hold, geyser_aim, think_room)")
    parser.add_argument("--comprobar", action="store_true", help="valida el bloque actual y no escribe")
    parser.add_argument("--simular", action="store_true", help="genera y valida sin escribir")
    parser.add_argument("--forzar", action="store_true", help="escribe aunque haya violaciones")
    parser.add_argument("--sin-lamina", action="store_true")
    args = parser.parse_args()

    site, manifest, path = load_site(args.variant)
    manual_raw, suppressed, block = read_block(manifest)
    manual = [from_json(site, d) for d in manual_raw]
    if args.comprobar:
        auto = [from_json(site, d, "auto") for d in block.get("auto", [])]
        problems = validate(site, manual + auto)
        for v in problems:
            print(f"[{v.rule}] {v.message}")
        print(f"{args.variant}: {len(auto)} automáticas, {len(manual)} manuales, {len(problems)} violaciones")
        if block.get("stale"):
            print("aviso: el terreno se regeneró con otra semilla; las automáticas están caducadas (rehazlas)")
        return 1 if problems or block.get("stale") else 0

    result = generate(site, args.seed, manual, suppressed, args.incluir_pendientes)
    summary = summarize(site, result)
    for v in result.violations:
        print(f"[{v.rule}] {v.message}")
    print(json.dumps(summary["counts"], ensure_ascii=False))
    print(f"puzles: {summary['puzzles_main']} en el principal ({summary['group_puzzles_main']} de grupo), "
          f"{summary['puzzles_loops']} en lazos; violaciones: {summary['violations']}")
    if args.simular or (result.violations and not args.forzar):
        return 1 if result.violations else 0
    new_block = build_block(site, result.auto, manual_raw, suppressed, args.seed, int(manifest["seed"]),
                            args.incluir_pendientes, summary)
    write_manifest(path, with_block(manifest, new_block))
    print(f"escrito {path}")
    if not args.sin_lamina:
        title = f"{args.variant} · colocación por reglas (semilla {args.seed})"
        out = render_sheet(site, result.all, path.parent / "placements.png", path.parent / "preview.png", title,
                           puzzle_notes(site, result.all))
        print(f"lámina {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
