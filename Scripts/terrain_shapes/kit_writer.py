"""Escritura y validacion de los mapas del kit (terrain_shapes/kit.py) por encima de writer.write_shape_map:

  - huecos con altura libre: cada tunel o paso inferior es aire de su ancho y alto con suelo solido debajo;
  - calzada de Rally: radio >= 25 m, pendiente <= 12 grados, ancho >= 12 m en cada muestra del eje (fuera de
    tuneles y pasos inferiores) y tramos no contiguos separados;
  - nidos de Todos contra Todos: 8 puntos alcanzables a >= 15 m entre si y >= 5 m del agua;
  - manifest con nidos, puntos de control, tuneles, tableros y marcas (puzzles, catapultas...), y la lamina.

El indice (Variants/index.json) no se toca al generar: register_maps lo actualiza leyendo-modificando-escribiendo
solo las entradas pedidas, justo antes de cada commit.
"""

from __future__ import annotations

import json
import math
import textwrap
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from scipy import ndimage

from terrain_geo.build import VARIANTS, dir_size_mb, update_index
from terrain_vol.validate import DRY_M, gradient_slope_deg, reachable, wide_ground

from .kit import Axis, arc_length, max_grade_deg, min_radius
from .model import ShapeMap
from .writer import SHEETS, write_shape_map


@dataclass(frozen=True)
class Clearance:
    """Hueco que debe quedar libre: eje con la cota del suelo, ancho y altura libre minimos."""
    axis: Axis
    width_m: float
    clearance_m: float
    name: str = "hueco"
    min_cover_m: float = 0.0          # roca maciza minima sobre la boveda (tuneles: 8 m), lejos de las bocas
    portal_m: float = 12.0            # tramo de boca en cada extremo sin exigir cobertura


@dataclass(frozen=True)
class Road:
    """Eje de una calzada de Rally (denso, 1 m) con su cota; covered marca las muestras bajo techo (tuneles y
    pasos inferiores), que se validan como Clearance y no por la cota superior."""
    pts: np.ndarray
    z: np.ndarray
    closed: bool = True
    covered: np.ndarray | None = None
    exempt: np.ndarray | None = None                         # sin comprobar separacion (junto a un cruce a dos niveles)
    width_m: float = 12.0
    max_grade_deg: float = 12.0
    min_radius_m: float = 25.0
    checkpoint_every_m: float = 200.0


@dataclass
class Extras:
    road: Road | None = None
    clearances: list[Clearance] = field(default_factory=list)
    nests: int = 0                                           # nidos exigidos (TcT: 8)
    markers: dict[str, list[tuple[float, float]]] = field(default_factory=dict)
    notes: str = ""                                          # linea extra de la lamina
    static: dict[str, dict] = field(default_factory=dict)    # comprobaciones del generador ({"ok": ...})


# ── Comprobaciones ───────────────────────────────────────────────────────────────
def _tangents(pts: np.ndarray, closed: bool) -> np.ndarray:
    nxt = np.roll(pts, -1, axis=0) if closed else np.vstack([pts[1:], pts[-1:] + (pts[-1] - pts[-2])])
    prv = np.roll(pts, 1, axis=0) if closed else np.vstack([pts[:1] - (pts[1] - pts[0]), pts[:-1]])
    t = nxt - prv
    return t / np.maximum(np.hypot(*t.T), 1e-9)[:, None]


def check_clearance(shape: ShapeMap, hole: Clearance, every_m: float = 3.0) -> dict:
    """Aire en el ancho (menos 1,5 m por lado) y hasta la altura libre (menos 0,5 m); solido bajo el suelo."""
    pts, z = hole.axis.array, hole.axis.zs
    arc = arc_length(pts)
    keep = np.nonzero((arc >= 2.0) & (arc <= arc[-1] - 2.0))[0]
    keep = keep[:: max(1, int(round(every_m / max(np.median(np.diff(arc)), 1e-6))))]
    tan = _tangents(pts, False)[keep]
    side = np.column_stack([-tan[:, 1], tan[:, 0]])
    reach = hole.width_m / 2.0 - 1.5
    probes = np.concatenate([pts[keep] + side * o for o in (-reach, 0.0, reach)])
    floors = np.tile(z[keep], 3)
    X, Y = shape.canvas.to_world(probes[:, 0], probes[:, 1])
    Z = np.arange(floors.min() - 1.0, floors.max() + hole.clearance_m, 0.25)
    D = shape.model.density(np.asarray(X)[None, :], np.asarray(Y)[None, :], Z)[0]
    bad = []
    for k, f in enumerate(floors):
        band = (Z > f + 0.3) & (Z < f + hole.clearance_m - 0.5)
        below = (Z > f - 1.0) & (Z < f - 0.4)
        if (D[k, band] >= 0.0).any():
            bad.append((int(keep[k % len(keep)]), "techo"))
        elif (D[k, below] <= 0.0).any():
            bad.append((int(keep[k % len(keep)]), "suelo"))
    cover = _cover(shape, hole, pts, z, arc) if hole.min_cover_m > 0.0 else None
    if cover is not None and cover["thin"]:
        bad.append((cover["thin"][0], "cobertura"))
    out = {"name": hole.name, "samples": int(len(floors)), "bad": len(bad), "first_bad": bad[:6], "ok": not bad}
    if cover is not None:
        out["min_cover_m"] = cover["min_m"]
    return out


def _cover(shape: ShapeMap, hole: Clearance, pts, z, arc) -> dict:
    """Roca maciza sobre la boveda en el eje, lejos de las bocas: la menor de todas y las muestras por debajo."""
    inner = np.nonzero((arc >= hole.portal_m) & (arc <= arc[-1] - hole.portal_m))[0][::2]
    if len(inner) == 0:
        return {"min_m": 0.0, "thin": []}
    X, Y = shape.canvas.to_world(pts[inner, 0], pts[inner, 1])
    Z = np.arange(z[inner].min(), z[inner].max() + hole.clearance_m + 40.0, 0.25)
    D = shape.model.density(np.asarray(X)[None, :], np.asarray(Y)[None, :], Z)[0]
    covers, thin = [], []
    for k, f in enumerate(z[inner]):
        above_roof = Z > f + hole.clearance_m
        solid = (D[k] > 0.0)[above_roof]
        start = int(np.argmax(solid)) if solid.any() else len(solid)      # la boveda real (sobre el galibo)
        rest = solid[start:]
        run = 0.25 * (int(np.argmin(rest)) if (~rest).any() else len(rest))
        covers.append(run)
        if run < hole.min_cover_m:
            thin.append(int(inner[k]))
    return {"min_m": round(float(min(covers)), 2), "thin": thin}


def check_road(shape: ShapeMap, top: np.ndarray, road: Road) -> dict:
    arc = arc_length(road.pts)
    total = float(arc[-1] + (np.hypot(*(road.pts[0] - road.pts[-1])) if road.closed else 0.0))
    radius, k_radius = min_radius(road.pts, road.closed)
    grade = max_grade_deg(arc, road.z, road.closed)
    covered = np.zeros(len(road.pts), dtype=bool) if road.covered is None else road.covered
    exempt = np.zeros(len(road.pts), dtype=bool) if road.exempt is None else road.exempt
    wide = wide_ground(top, road.width_m, road.max_grade_deg)
    apron = np.convolve(np.concatenate([covered[-8:], covered, covered[:8]]).astype(float), np.ones(17), "same")[8:-8] > 0         if road.closed and covered.any() else covered
    open_idx = np.nonzero(~apron)[0][::2]                    # sin la boca de cada tunel (8 m): alli manda la fachada
    ij = np.array([shape.canvas.to_ij(*road.pts[k]) for k in open_idx])
    narrow = [int(k) for k, (i, j) in zip(open_idx, ij) if not wide[i, j]]
    # Tramos no contiguos (a mas de 80 m de arco) a menos de ancho + 8 m, fuera de lo cubierto.
    from scipy.spatial import cKDTree
    tree = cKDTree(road.pts)
    close = []
    for a, b in tree.query_pairs(road.width_m + 8.0):
        gap = abs(arc[a] - arc[b])
        if road.closed:
            gap = min(gap, total - gap)
        if gap > 80.0 and not covered[a] and not covered[b] and not exempt[a] and not exempt[b]:
            close.append((a, b))
    ok = radius >= road.min_radius_m and grade <= road.max_grade_deg and not narrow and not close
    return {"length_m": round(total, 1), "min_radius_m": round(radius, 1), "min_radius_at": k_radius,
            "max_grade_deg": round(grade, 2), "narrow_samples": len(narrow), "narrow_first": narrow[:5],
            "too_close_pairs": len(close), "ok": bool(ok)}


def pick_nests(top: np.ndarray, start_ij: tuple[int, int], count: int, min_sep_m: float = 15.0,
               min_water_m: float = 5.0, max_slope_deg: float = 15.0) -> list[tuple[int, int]]:
    """Muestreo del punto mas lejano entre los sitios validos (alcanzables, lejos del agua, casi llanos),
    empezando por el mas cercano al inicio; se para si el siguiente queda a menos de min_sep_m."""
    seen = reachable(top, start_ij)
    far = ndimage.distance_transform_edt(top > DRY_M)
    flat = gradient_slope_deg(top, seen) <= max_slope_deg
    cand = np.argwhere(seen & (far >= min_water_m) & flat)
    if len(cand) == 0:
        return []
    cand = cand[::max(1, len(cand) // 20000)]
    first = int(np.argmin(((cand - np.array(start_ij)) ** 2).sum(axis=1)))
    chosen = [first]
    dist = np.hypot(*(cand - cand[first]).T)
    while len(chosen) < count:
        k = int(np.argmax(dist))
        if dist[k] < min_sep_m:
            break
        chosen.append(k)
        dist = np.minimum(dist, np.hypot(*(cand - cand[k]).T))
    return [tuple(int(v) for v in cand[k]) for k in chosen]


def checkpoints(road: Road) -> list[int]:
    arc = arc_length(road.pts)
    marks = np.arange(0.0, arc[-1], road.checkpoint_every_m)
    out = []
    covered = road.covered if road.covered is not None else np.zeros(len(arc), dtype=bool)
    for m in marks:
        k = int(np.searchsorted(arc, m))
        while k < len(arc) - 1 and covered[k]:
            k += 1
        out.append(k)
    return out


# ── Escritura ────────────────────────────────────────────────────────────────────
def _uu(shape: ShapeMap, top: np.ndarray, e: float, n: float, yaw: float | None = None) -> list[float]:
    X, Y = shape.canvas.to_world(e, n)
    i, j = shape.canvas.to_ij(e, n)
    out = [round(X * 100.0, 1), round(Y * 100.0, 1), round(float(top[i, j]) * 100.0, 1)]
    return out + [round(yaw, 1)] if yaw is not None else out


def road_uu(canvas, pts: np.ndarray, z: np.ndarray) -> list[list[float]]:
    """Eje de la calzada (diseño e, n en m y cota absoluta en m) en uu: lo lee ATN_RallyTrack para su spline."""
    out = []
    for (e, n), zz in zip(pts, z):
        X, Y = canvas.to_world(float(e), float(n))
        out.append([round(X * 100.0, 1), round(Y * 100.0, 1), round(float(zz) * 100.0, 1)])
    return out


def _ij_to_design(shape: ShapeMap, ij) -> tuple[float, float]:
    from terrain_vol.layout import MAP_MIN_M
    X, Y = MAP_MIN_M + ij[0], MAP_MIN_M + ij[1]
    e, n = shape.canvas.to_design(X, Y)
    return float(e), float(n)


def write_kit_map(shape: ShapeMap, extras: Extras | None = None, sheet: bool = True, variants: Path = VARIANTS,
                  sheets: Path = SHEETS) -> dict:
    """write_shape_map (trozos, manifest, validadores comunes) sin tocar el indice, mas las comprobaciones de
    Extras; el veredicto final va a manifest.recorrible y a result["ok"]."""
    extras = extras or Extras()
    r = write_shape_map(shape, sheet=False, register=False, variants=variants, sheets=sheets)
    top = r["top"]
    out = variants / shape.name
    checks: dict = {}
    ok = bool(r["ok"])
    holes = [check_clearance(shape, h) for h in extras.clearances]
    if holes:
        checks["clearances"] = holes
        ok = ok and all(h["ok"] for h in holes)
    for key, value in extras.static.items():
        checks[key] = value
        ok = ok and bool(value.get("ok", True))
    data = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
    marks: dict[str, tuple[int, int]] = {k: shape.canvas.to_ij(*p) for k, p in shape.required.items()}
    if extras.road is not None:
        road = extras.road
        checks["road"] = check_road(shape, top, road)
        ok = ok and checks["road"]["ok"]
        tan = _tangents(road.pts, road.closed)
        cps = checkpoints(road)
        data["checkpoints_uu"] = [_uu(shape, top, *road.pts[k], math.degrees(math.atan2(tan[k, 0], tan[k, 1])))
                                  for k in cps]
        marks.update({f"c{m}": shape.canvas.to_ij(*road.pts[k]) for m, k in enumerate(cps)})
        data["road_uu"] = road_uu(shape.canvas, road.pts, road.z)
        data["closed"] = bool(road.closed)
    if extras.nests:
        s_ij = shape.canvas.to_ij(*shape.start)
        nests = pick_nests(top, s_ij, extras.nests)
        checks["nests"] = {"wanted": extras.nests, "found": len(nests), "ok": len(nests) >= extras.nests}
        ok = ok and checks["nests"]["ok"]
        data["nests_uu"] = [_uu(shape, top, *_ij_to_design(shape, p)) for p in nests]
        marks.update({f"N{k + 1}": p for k, p in enumerate(nests)})
    solids = getattr(shape.model, "solids", ())
    voids = getattr(shape.model, "voids", ())
    data["tunnels"] = [v.manifest(shape.canvas) for v in voids if hasattr(v, "manifest")]
    data["decks"] = [s.manifest(shape.canvas) for s in solids if hasattr(s, "manifest")]
    data["markers_uu"] = {k: [_uu(shape, top, *p) for p in pts] for k, pts in extras.markers.items()}
    data["checks"] = checks
    data["recorrible"] = ok
    (out / "manifest.json").write_text(json.dumps(data, indent=1), encoding="utf-8")
    r.update({"ok": ok, "checks": checks, "size_mb": dir_size_mb(out)})
    if sheet:
        from terrain_vol.sheet import render_sheet, sheet_subtitle
        parts = [f"{len(shape.model.bridges) + len(data['decks'])} puentes y tableros", f"{len(data['tunnels'])} túneles"]
        if extras.road is not None:
            rd = checks["road"]
            parts.append(f"vuelta {rd['length_m']:.0f} m, radio mín. {rd['min_radius_m']:.0f} m, pendiente máx. "
                         f"{rd['max_grade_deg']:.1f}°".replace(".", ","))
        if extras.nests:
            parts.append(f"{checks['nests']['found']} nidos")
        if extras.notes:
            parts.append(extras.notes)
        report = {**r, "ok": ok}
        subtitle = sheet_subtitle(textwrap.fill(shape.description, 190), shape.canvas.grid, report, r["size_mb"],
                                  "; ".join(parts))
        end = shape.canvas.to_ij(*shape.end) if shape.end else None
        r["sheet_bytes"] = render_sheet(top, sheets / f"{shape.name}.png", f"{shape.name} ({shape.mode})", subtitle,
                                        shape.canvas.to_ij(*shape.start), end, marks)
    return r


def kit_summary(r: dict) -> str:
    b = r["budget"]
    extra = []
    road = r.get("checks", {}).get("road")
    if road:
        extra.append(f"calzada {road['length_m']} m r>={road['min_radius_m']}@{road['min_radius_at']} {road['max_grade_deg']}° estrechas "
                     f"{road['narrow_samples']} cerca {road['too_close_pairs']}")
    for h in r.get("checks", {}).get("clearances", []):
        if not h["ok"]:
            extra.append(f"hueco {h['name']} mal en {h['bad']}/{h['samples']} {h['first_bad']}")
    for key, value in r.get("checks", {}).items():
        if key not in ("road", "clearances", "nests") and isinstance(value, dict):
            extra.append(f"{key} {'ok' if value.get('ok') else 'MAL'} {value.get('summary', '')}")
    nests = r.get("checks", {}).get("nests")
    if nests:
        extra.append(f"nidos {nests['found']}/{nests['wanted']}")
    return (f"{r['name']}: {'OK' if r['ok'] else 'NO VALIDO'} {r['size_mb']} MB, {b['triangles']} tri (tope "
            f"{b['budget_triangles']}), {b['editor_mb_est']} MB SM; alcanzable {r['walkable_share'] * 100:.0f} %; "
            f"sin llegar {r['required_names_missed']}; pasarelas mal {len(r['corridors_failed'])}; islas "
            f"{len(r['unreachable_islands'])}; costuras {'OK' if r['seams']['ok'] else 'GRIETAS'}; " + "; ".join(extra))


def register_maps(names: list[str], variants: Path = VARIANTS) -> list[str]:
    """Añade o actualiza en index.json SOLO las entradas pedidas (leer-modificar-escribir por entrada)."""
    done = []
    for name in names:
        data = json.loads((variants / name / "manifest.json").read_text(encoding="utf-8"))
        update_index(name, int(data["seed"]), bool(data.get("recorrible")), dir_size_mb(variants / name),
                     data["description"], {"mode": data["mode"]})
        done.append(name)
    return done
