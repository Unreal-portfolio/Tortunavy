"""Genera los mapas inventados (lotes N01-N16) y los que faltaban del catalogo (A02-A06, I02-I06) en
Scripts/terrain_volumes/Variants/<nombre>/ con su lamina en Docs/Mapas/<nombre>.png. No toca el indice salvo
con --register (leer-modificar-escribir de las entradas generadas).

    uv run --with numpy --with scipy --with pillow --with scikit-image --with matplotlib \
        python Scripts/gen_terrain_inventados.py --map N01 [--map N02 ...] [--lot 1] [--register] [--no-sheet]
    ... gen_terrain_inventados.py --list

Cada id es un generador parametrico con semilla (terrain_shapes/*): --seed cambia la semilla y --name el nombre
de la carpeta para probar variantes sin pisar el mapa del catalogo.
"""

from __future__ import annotations

import argparse
import dataclasses
from typing import Callable

from terrain_shapes.kit_writer import Extras, kit_summary, register_maps, write_kit_map
from terrain_shapes.model import ShapeMap

Factory = Callable[[int | None], tuple[ShapeMap, Extras]]
TCT_NESTS = 8


def _reseed(spec, seed: int | None):
    return spec if seed is None else dataclasses.replace(spec, seed=seed)


# ── Arenas del catalogo (A02-A06) ────────────────────────────────────────────────
def _arena_catalog(key: str) -> Factory:
    def make(seed):
        from gen_terrain_arena import CATALOG
        shape = CATALOG[key]()
        if seed is not None:
            shape.seed = seed
        return shape, Extras(nests=TCT_NESTS)
    return make


def _clock(seed):
    from terrain_shapes.arena_extra import build_clock, clock_a04
    shape, markers = build_clock(_reseed(clock_a04(), seed))
    return shape, Extras(nests=TCT_NESTS, markers=markers)


def _honeycomb(seed):
    from terrain_shapes.arena_extra import build_honeycomb, honeycomb_a06
    shape, markers = build_honeycomb(_reseed(honeycomb_a06(), seed))
    return shape, Extras(nests=TCT_NESTS, markers=markers)


MAPS: dict[str, tuple[str, Factory]] = {
    "A02": ("C1", _arena_catalog("A02")),
    "A03": ("C1", _arena_catalog("A03")),
    "A04": ("C1", _clock),
    "A05": ("C1", _arena_catalog("A05")),
    "A06": ("C1", _honeycomb),
}


def _load_lots() -> None:
    """Los lotes se registran en sus modulos (cada uno añade sus ids a MAPS)."""
    import importlib
    for module in ("terrain_shapes.lots_islands", "terrain_shapes.lots_rally", "terrain_shapes.lots_arenas",
                   "terrain_shapes.lots_giants", "terrain_shapes.lots_routes", "terrain_shapes.lots_boards"):
        try:
            mod = importlib.import_module(module)
        except ModuleNotFoundError as exc:
            if exc.name != module:
                raise
            continue
        MAPS.update(mod.MAPS)


def build(key: str, seed: int | None = None, name: str | None = None, sheet: bool = True, **kwargs) -> dict:
    _load_lots()
    shape, extras = MAPS[key][1](seed)
    if name:
        shape.name = name
    return write_kit_map(shape, extras, sheet=sheet, **kwargs)


def main() -> None:
    _load_lots()
    parser = argparse.ArgumentParser(description="Genera los mapas inventados y los que faltaban del catalogo.")
    parser.add_argument("--map", action="append", default=[], choices=sorted(MAPS))
    parser.add_argument("--lot", action="append", default=[], help="lote entero (C1, C2, 1, 2, 3, 4)")
    parser.add_argument("--seed", type=int)
    parser.add_argument("--name")
    parser.add_argument("--no-sheet", action="store_true")
    parser.add_argument("--register", action="store_true", help="añade las entradas generadas a index.json")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    if args.list:
        for key, (lot, _) in sorted(MAPS.items()):
            print(f"{key}\tlote {lot}")
        return
    keys = list(args.map) + [k for k, (lot, _) in sorted(MAPS.items()) if lot in args.lot]
    done = []
    for key in keys:
        r = build(key, args.seed, args.name, sheet=not args.no_sheet)
        print(kit_summary(r), flush=True)
        done.append(r["name"])
    if args.register:
        print("indice:", register_maps(done))


if __name__ == "__main__":
    main()
