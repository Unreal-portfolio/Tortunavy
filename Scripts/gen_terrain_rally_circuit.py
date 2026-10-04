"""Genera un circuito de Rally por vueltas (#622) en Scripts/terrain_volumes/Variants/<id>/ (trozos TNTM2, manifest,
vistas y lámina de revisión) y lo añade a index.json. El juego lo carga con LVL_Rally?Variant=<id>.

    uv run --with pyfqmr --with matplotlib python Scripts/gen_terrain_rally_circuit.py [--seed 622] [--name R01_...]
        [--no-decimate] [--no-sheet]
    uv run --with pyfqmr --with matplotlib python Scripts/gen_terrain_rally_circuit.py --profile tierra   # R02 (#682)

El trazado, los elementos y el terreno están en terrain_geo/rally_circuit*.py; la validación, sobre la variante ya
escrita, en terrain_geo/rally_circuit_check.py (la repite Scripts/tests/test_terrain_rally_circuit.py).

Manifest: lo común de write_map más lo que lee ATN_RallyTrack (mode "rally", closed true, laps, road_uu cada ~1 m
sin repetir el primer punto, road_width_m, checkpoints_uu [x, y, z, yaw] con la primera puerta en la línea de
salida, start_uu = end_uu, start_yaw, kill_boxes_uu) y, nuevo en #622 (Docs/Rally_Circuitos_Vueltas.md):

  - bank_deg: peralte por punto de road_uu, en grados; positivo = el lado derecho de la marcha más bajo (curva a
    la derecha), negativo = el izquierdo; |bank_deg| <= 15. El juego todavía no lo lee (siguiente paso, en C++).
  - elements: los elementos con su arco (s_m, m desde la línea de salida por road_uu) y su diseño (saltos con la
    velocidad de llegada, el labio, la zona de aterrizaje y el vuelo; curvas con radio, giro y peralte; rasantes
    con altura y radio vertical).
  - markers_uu: "parrilla" (los 8 huecos 2 x 4 de TNRally::GridSlotOffset), y labio y aterrizaje de cada salto y
    cima de cada rasante.
  - physics: la BuggySpec con la que se han dimensionado; checks: el informe del validador y su veredicto.

Ancho por tramos (#622, «Tramos variados», terrain_geo/rally_circuit_width.py):

  - road_widths_m: ancho de la calzada (m) por punto de road_uu, de 10 a 20 m, con transiciones lineales;
    road_width_m pasa a ser el máximo (quien solo lea ese campo pone las barreras fuera de toda la calzada).
  - width_sections: tramos de ancho constante en el orden de la carrera (s_m, length_m, width_m, class estrecho,
    normal o ancho, y la pieza que lo pide); cada elemento lleva además su width_m (mediana en su arco).

Perfil tierra (#682): los saltos llevan `jump_kind` (mesa, doble, cresta, hueco) y, los de forma, `gap_s_m` (del
labio a la cresta de la recepción), `v_ai_kmh` y `land_ai_s_m` (el piloto IA llega a 0,9 · v); elementos nuevos
`baches` (`pattern` whoops o tabla_lavar, amplitud, longitud de onda, tramo de las ondas `train_s_m`), `baden`
(profundidad, largo, `dip_s_m`, barro) y `banqueta` (horquilla con caballón por fuera); `suspension`: la del buggy
con la que se acotan los baches; `generator.profile`: "tierra".
"""

from __future__ import annotations

import argparse
import json
import math
import time

import numpy as np
from terrain_geo import rally_circuit as rc
from terrain_geo.build import VARIANTS, dir_size_mb, kill_boxes_uu, update_index, write_credits
from terrain_geo.heightfield import ZONES
from terrain_geo.rally_circuit_check import load_report
from terrain_geo.rally_circuit_dirt import BERM_RISE_M, BUMP_LEAD_M, DIP_LEAD_M, SUSPENSION
from terrain_geo.rally_circuit_elements import impact_ms
from terrain_geo.rally_circuit_jumps import AI_FACTOR, ShapedJump
from terrain_geo.rally_circuit_physics import BUGGY
from terrain_geo.rally_circuit_width import RULES_M, TAPER_M, width_sections
from terrain_vol.export import global_top, write_map
from terrain_vol.layout import CELL_SAMPLES, UU_PER_M
from terrain_vol.mesh import build_chunk

CHECKPOINT_EVERY_M = 200.0
DECIMATE_NEAR_M = 0.08
DECIMATE_FAR_M = 0.25
DECIMATE_FINE_M = 0.02           # trozos con baches o badén: el decimado normal se come la tabla de lavar
NEAR_M = 30.0
# Parrilla 2 x 4 (TN_RallyLogic.h): primera fila a 10 m de la salida, 8 m entre filas, 3,5 m a cada lado del eje.
GRID_FIRST_ROW_M, GRID_ROW_M, GRID_HALF_M = 10.0, 8.0, 3.5


def uu(p, z: float, yaw: float | None = None) -> list[float]:
    out = [round(float(p[0]) * UU_PER_M, 1), round(float(p[1]) * UU_PER_M, 1), round(float(z) * UU_PER_M, 1)]
    return out + [round(yaw, 1)] if yaw is not None else out


def yaw_deg(psi: float) -> float:
    return round((math.degrees(psi) + 180.0) % 360.0 - 180.0, 1)


def checkpoints(track: rc.Track) -> list[int]:
    """Índices de las puertas: la línea de salida y luego cada ~CHECKPOINT_EVERY_M; una que cae en un salto pasa
    a 10 m después de su recepción."""
    total, step = track.total, track.plan.step_m
    count = max(3, int(round(total / CHECKPOINT_EVERY_M)))
    out = []
    for s in total * np.arange(count) / count:
        for j in track.jumps:
            if j.s0 - 10.0 <= s <= j.s0 + j.design.length_m:
                s = j.s0 + j.design.length_m + 10.0
        out.append(int(round(s / step)) % len(track.plan.pts))
    return sorted(set(out))


def grid_slots(track: rc.Track, road: np.ndarray) -> list[list[float]]:
    out = []
    for slot in range(8):
        back = GRID_FIRST_ROW_M + GRID_ROW_M * (slot // 2)
        side = -GRID_HALF_M if slot % 2 == 0 else GRID_HALF_M
        k = track.index(track.total - back)
        psi = track.plan.psi[k]
        p = road[k] + side * np.array([-math.sin(psi), math.cos(psi)])
        out.append(uu(p, track.z[k]))
    return out


def _span(s0: float, s1: float, total: float) -> list[float]:
    return [round(s0 % total, 2), round(s1 % total, 2) or round(total, 2)]


def _span_mask(track: rc.Track, s0: float, s1: float) -> np.ndarray:
    return rc.cyclic_mask(track.arc, track.total, s0, s1)


def elements(track: rc.Track) -> list[dict]:
    plan, total = track.plan, track.total
    out, counts = [], {}
    jumps = {j.piece: j for j in track.jumps}
    crests = {c.piece: c for c in track.crests}
    bumps = {b.piece: b for b in track.bumps}
    dips = {d.piece: d for d in track.dips}
    for piece, s0, s1 in plan.spans:
        p = plan.pieces[piece]
        counts[p.kind] = counts.get(p.kind, 0) + 1
        e = {"type": p.kind, "id": f"{p.kind}_{counts[p.kind]}", "s_m": _span(s0, s1, total)}
        if p.kind in ("curva_peraltada", "horquilla", "chicane"):
            e.update(radius_m=round(p.radius_m, 1), angle_deg=round(p.angle_deg, 1),
                     side="derecha" if p.angle_deg > 0 else "izquierda")
        if p.kind in ("curva_peraltada", "horquilla"):
            e.update(bank_deg=round(p.bank_deg, 2), bank_signed_deg=round(math.copysign(p.bank_deg, p.angle_deg), 2))
        e["width_m"] = round(float(np.median(track.width_m[_span_mask(track, s0, s1)])), 2)
        if p.kind == "recta":
            e.update(length_m=round((s1 - s0) % total, 1), v_max_kmh=round(float(track.speed.max()) * 3.6, 1))
        if p.kind == "salto":
            j = jumps[piece]
            d = j.design
            land, boost = d.fly(d.v_design), d.fly(d.v_boost)
            lip = j.s0 + d.lip_x
            e.update(s_m=_span(j.s0, j.s0 + d.length_m, total), straight_s_m=_span(s0, s1, total),
                     lip_s_m=round(lip, 2), lip_deg=round(d.lip_deg, 2), height_m=round(d.height_m, 2),
                     table_m=round(d.table_m, 2), landing_deg=round(d.landing_deg, 2),
                     landing_s_m=[round(j.s0 + x, 2) for x in d.landing_zone],
                     land_design_s_m=round(lip + land.x_land_m, 2), land_boost_s_m=round(lip + boost.x_land_m, 2),
                     v_design_ms=round(d.v_design, 2), v_boost_ms=round(d.v_boost, 2),
                     v_design_kmh=round(d.v_design * 3.6, 1), v_boost_kmh=round(d.v_boost * 3.6, 1),
                     airtime_s=land.airtime_s, apex_m=land.apex_m, impact_ms=round(impact_ms(d, d.v_design), 2),
                     impact_boost_ms=round(impact_ms(d, d.v_boost), 2), drop_m=round(d.drop_m, 2), design=d.as_dict())
            e.update(_jump_kind_fields(j.s0, d))
        if p.kind == "baches":
            b = bumps[piece]
            e.update(train_s_m=_span(b.s0 + BUMP_LEAD_M, b.s0 + BUMP_LEAD_M + b.design.train_m, total),
                     **b.design.as_dict())
        if p.kind == "baden":
            dip = dips[piece]
            e.update(dip_s_m=_span(dip.s0 + DIP_LEAD_M, dip.s0 + DIP_LEAD_M + dip.design.length_m, total),
                     surface="barro", **dip.design.as_dict())
        if p.kind == "rasante":
            c = crests[piece]
            e.update(crest_s_m=round(c.s0 + c.design.length_m / 2.0, 2), **c.design.as_dict(),
                     v_boost_kmh=round(c.design.v_boost * 3.6, 1))
        out.append(e)
        if p.variant == "banqueta":
            out.append({"type": "banqueta", "id": f"banqueta_{counts[p.kind]}", "s_m": e["s_m"], "side": e["side"],
                        "outside": "izquierda" if p.angle_deg > 0 else "derecha", "bank_deg": e["bank_deg"],
                        "berm_rise_m": BERM_RISE_M})
    return out


def _jump_kind_fields(s0: float, d) -> dict:
    """Forma del salto (#682); en los de forma, el hueco y el aterrizaje del piloto IA (a AI_FACTOR · v)."""
    if not isinstance(d, ShapedJump):
        return {"jump_kind": "mesa"}
    lip = s0 + d.lip_x
    ai = d.fly(AI_FACTOR * d.v_design)
    return {"jump_kind": d.kind, "gap_s_m": [round(lip, 2), round(lip + d.crest_x_m, 2)],
            "v_ai_kmh": round(AI_FACTOR * d.v_design * 3.6, 1),
            "land_ai_s_m": round(lip + ai.x_land_m, 2) if ai else None,
            "impact_ai_ms": round(impact_ms(d, AI_FACTOR * d.v_design), 2)}


def fine_cells(model: rc.RallyCircuitModel) -> set:
    """Trozos (col, row) que tocan los baches o el badén (con 20 m de margen)."""
    track, out = model.track, set()
    spans = [(b.s0, b.design.piece_m) for b in track.bumps] + [(d.s0, rc.DIP_PIECE_M) for d in track.dips]
    for s0, length in spans:
        for s in np.arange(s0, s0 + length, 2.0):
            p = model.road[track.index(s)]
            for dx in (-20.0, 0.0, 20.0):
                for dy in (-20.0, 0.0, 20.0):
                    out.add((int(round((p[1] + dy) / 100.0)), int(round((p[0] + dx) / 100.0))))
    return out


def build_chunks(model: rc.RallyCircuitModel, decimate: bool) -> dict:
    chunks = {cell: build_chunk(model, *cell) for cell in model.cells()}
    if not decimate:
        return chunks
    from terrain_vol.decimate import decimate_chunks
    fine = fine_cells(model)
    near = {c: m for c, m in chunks.items() if model.cell_gap(*c) <= NEAR_M and c not in fine}
    far = {c: m for c, m in chunks.items() if c not in near and c not in fine}
    return {**decimate_chunks(near, DECIMATE_NEAR_M), **decimate_chunks(far, DECIMATE_FAR_M),
            **decimate_chunks({c: m for c, m in chunks.items() if c in fine}, DECIMATE_FINE_M)}


def manifest_extra(track: rc.Track, model: rc.RallyCircuitModel, name: str, seed: int, decimate: bool) -> dict:
    road = model.road
    cps = checkpoints(track)
    marks = {"parrilla": grid_slots(track, road)}
    for j, e in zip(track.jumps, [e for e in elements(track) if e["type"] == "salto"]):
        for key, s in (("labio", e["lip_s_m"]), ("aterrizaje", e["land_design_s_m"])):
            k = track.index(s)
            marks.setdefault(f"{e['id']}_{key}", []).append(uu(road[k], track.z[k]))
    for e in [e for e in elements(track) if e["type"] == "rasante"]:
        k = track.index(e["crest_s_m"])
        marks[f"{e['id']}_cima"] = [uu(road[k], track.z[k])]
    tierra = track.profile == "tierra"
    return {
        "description": rc.TIERRA_DESCRIPTION if tierra else rc.DESCRIPTION, "mode": "rally", "closed": True, "laps": rc.LAPS,
        "kill_boxes_uu": kill_boxes_uu(model.frame.grid),
        "z_range": [model.z_range.z_min_m, model.z_range.levels, model.z_range.step_m],
        "generator": {"generator": "rally_circuit_vueltas", "profile": track.profile, "seed": seed,
                      "attempt": track.plan.attempt,
                      "pieces": [p.__dict__ for p in track.plan.pieces], "road_w_m": rc.ROAD_W_M,
                      "shoulder_m": rc.SHOULDER_M, "berm_m": rc.BERM_M, "talud_deg": rc.TALUD_DEG,
                      "rows": model.frame.rows, "cols": model.frame.cols, "shift_m": model.frame.shift.tolist(),
                      "decimate_m": [DECIMATE_NEAR_M, DECIMATE_FAR_M] if decimate else 0.0, "near_m": NEAR_M,
                      "width_rules_m": {k: list(v) for k, v in RULES_M.items()}, "width_taper_m": TAPER_M},
        "physics": BUGGY.as_dict(), **({"suspension": SUSPENSION.as_dict()} if tierra else {}),
        "road_width_m": round(float(track.width_m.max()), 2), "road_uu": [uu(p, zz) for p, zz in zip(road, track.z)],
        "road_widths_m": [round(float(w), 2) for w in track.width_m],
        "width_sections": width_sections(track.plan, seed),
        "bank_deg": [round(float(b), 2) for b in track.bank_deg],
        "checkpoints_uu": [uu(road[k], track.z[k], yaw_deg(track.plan.psi[k])) for k in cps],
        "start_yaw": yaw_deg(track.plan.psi[0]),
        "elements": elements(track), "tunnels": [], "decks": [], "bridges": [], "markers_uu": marks,
        "lap": {"length_m": round(track.total, 1),
                "ideal_lap_s": round(float((track.plan.step_m / np.maximum(track.speed, 1.0)).sum()), 1)},
    }


def build(seed: int, name: str, decimate: bool = True, sheet: bool = True, profile: str = "dunas") -> dict:
    t0 = time.time()
    track = rc.build_track(seed, profile)
    model = rc.RallyCircuitModel(track, seed)
    chunks = build_chunks(model, decimate)
    grid = model.frame.grid
    extra = manifest_extra(track, model, name, seed, decimate)
    out = VARIANTS / name
    size = grid * (CELL_SAMPLES - 1) + 1
    zones = {zone: (np.ones((size, size)) if zone == "cliffs" else np.zeros((size, size))) for zone in ZONES}
    start = (*model.road[0], float(track.z[0]))
    write_map(out, name, seed, chunks, start, start, zones, model.road, extra_manifest=extra, grid=grid)
    issue = "#682" if profile == "tierra" else "#622"
    write_credits(out, f"Circuito de Rally por vueltas {name}: Scripts/gen_terrain_rally_circuit.py (semilla {seed}, {issue}).\n")
    report, checks = load_report(out)
    ok = all(checks.values())
    data = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
    data["checks"] = {"circuit": report, "verdict": checks}
    data["recorrible"] = ok
    (out / "manifest.json").write_text(json.dumps(data, indent=1), encoding="utf-8")
    if sheet:
        from terrain_geo.rally_circuit_sheet import render_circuit_sheet
        render_circuit_sheet(out / "lamina.png", name, data, global_top(chunks, grid=grid), track, model)
    size_mb = dir_size_mb(out)
    update_index(name, seed, ok, size_mb, extra["description"], {"mode": "rally"})
    return {"ok": ok, "time_s": round(time.time() - t0, 1), "grid": [model.frame.rows, model.frame.cols],
            "cells": len(chunks), "triangles": int(sum(len(c.triangles) for c in chunks.values())),
            "size_mb": size_mb, "checks": checks}


def main() -> None:
    parser = argparse.ArgumentParser(description="Genera un circuito de Rally por vueltas (#622).")
    parser.add_argument("--profile", choices=("dunas", "tierra"), default="dunas",
                        help="dunas (#622) o tierra (#682: saltos con forma, baches, badén y banqueta)")
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--name", default=None)
    parser.add_argument("--no-decimate", action="store_true", help="no decimar los trozos (no necesita pyfqmr)")
    parser.add_argument("--no-sheet", action="store_true", help="sin lámina (no necesita matplotlib)")
    args = parser.parse_args()
    tierra = args.profile == "tierra"
    seed = args.seed if args.seed is not None else (rc.TIERRA_SEED if tierra else rc.SEED)
    name = args.name or (rc.TIERRA_NAME if tierra else rc.NAME)
    r = build(seed, name, decimate=not args.no_decimate, sheet=not args.no_sheet, profile=args.profile)
    print(json.dumps(r, indent=1, ensure_ascii=False))


if __name__ == "__main__":
    main()
