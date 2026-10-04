"""Validador del corredor de un Rally punto a punto sobre la variante YA ESCRITA (manifest y trozos TNTM2): lo
que ve el juego, no el campo de alturas del generador.

    report = corridor_report(Path(".../Variants/E01B_espana_rally"))

Mide sobre la cara superior de la malla (máximo de los triángulos que cubren cada punto) a lo largo de
`road_uu` (eje de la calzada cada 1 m):
  - longitud de la calzada y del recorrido (de start_uu a end_uu);
  - pendiente sostenida (ventanas de SUSTAINED_M), corta (ventanas de SHORT_M) y escalón entre muestras de 1 m;
  - ancho llano: tramo continuo a través del eje con la cota a <= FLAT_TOL_M de la del eje y fuera del agua;
  - radio de curva mínimo (circunferencia por p[k-8], p[k], p[k+8]);
  - checkpoints: separación por el arco (incluidos inicio y meta), distancia al eje, cota y rumbo, y que ninguno
    caiga en un túnel o un tablero del manifest.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path

import numpy as np
from terrain_vol.export import read_chunk
from terrain_vol.layout import UU_PER_M, WATER_M

SUSTAINED_M = 30.0
SHORT_M = 4.0
FLAT_TOL_M = 0.25
LATERAL_MAX_M = 12.0
LATERAL_STEP_M = 0.5
RADIUS_SPAN_M = 8.0


class MeshSampler:
    """Cota de la cara superior de la malla de una variante en puntos (X, Y) en metros (NaN fuera de la malla).
    Solo los trozos con colisión: los de fondo ("collision": false) se ven pero no se pisan."""

    def __init__(self, variant_dir: Path, manifest: dict | None = None):
        self.manifest = manifest or json.loads((variant_dir / "manifest.json").read_text(encoding="utf-8"))
        self.cells = []
        for cell in self.manifest["cells"]:
            if not cell.get("collision", True):
                continue
            chunk = read_chunk(variant_dir / cell["file"])
            cx, cy = (v / UU_PER_M for v in cell["center_uu"])
            v = chunk["vertices"].astype(np.float64) / UU_PER_M + np.array([cx, cy, 0.0])
            tri = v[chunk["triangles"].astype(np.int64)]
            self.cells.append((cx, cy, tri))

    def top(self, xy: np.ndarray, batch: int = 128) -> np.ndarray:
        xy = np.asarray(xy, dtype=np.float64).reshape(-1, 2)
        out = np.full(len(xy), np.nan)
        for cx, cy, tri in self.cells:
            inside = np.nonzero((np.abs(xy[:, 0] - cx) <= 50.01) & (np.abs(xy[:, 1] - cy) <= 50.01))[0]
            if len(inside) == 0 or len(tri) == 0:
                continue
            lo, hi = tri[:, :, :2].min(axis=1), tri[:, :, :2].max(axis=1)
            for start in range(0, len(inside), batch):
                idx = inside[start:start + batch]
                z = _top_in(tri, lo, hi, xy[idx])
                out[idx] = np.fmax(out[idx], z)
        return out


def _top_in(tri: np.ndarray, lo: np.ndarray, hi: np.ndarray, p: np.ndarray) -> np.ndarray:
    """Máximo de la cota de los triángulos que contienen cada punto (proyección cenital)."""
    out = np.full(len(p), np.nan)
    eps = 1e-6
    cand = ((p[:, None, 0] >= lo[None, :, 0] - eps) & (p[:, None, 0] <= hi[None, :, 0] + eps)
            & (p[:, None, 1] >= lo[None, :, 1] - eps) & (p[:, None, 1] <= hi[None, :, 1] + eps))
    pi, ti = np.nonzero(cand)
    if len(pi) == 0:
        return out
    a, b, c = tri[ti, 0], tri[ti, 1], tri[ti, 2]
    q = p[pi]
    v0, v1, v2 = b[:, :2] - a[:, :2], c[:, :2] - a[:, :2], q - a[:, :2]
    det = v0[:, 0] * v1[:, 1] - v0[:, 1] * v1[:, 0]
    ok = np.abs(det) > 1e-9
    safe = np.where(ok, det, 1.0)
    u = (v2[:, 0] * v1[:, 1] - v2[:, 1] * v1[:, 0]) / safe
    w = (v0[:, 0] * v2[:, 1] - v0[:, 1] * v2[:, 0]) / safe
    hit = ok & (u >= -1e-6) & (w >= -1e-6) & (u + w <= 1.0 + 1e-6)
    z = a[:, 2] + u * (b[:, 2] - a[:, 2]) + w * (c[:, 2] - a[:, 2])
    np.fmax.at(out, pi[hit], z[hit])
    return out


# ── Medidas ──────────────────────────────────────────────────────────────────────
def arc_of(pts: np.ndarray) -> np.ndarray:
    return np.concatenate([[0.0], np.cumsum(np.hypot(*np.diff(pts, axis=0).T))])


def tangents(pts: np.ndarray) -> np.ndarray:
    t = np.gradient(pts, axis=0)
    return t / np.maximum(np.hypot(*t.T), 1e-9)[:, None]


def radii(pts: np.ndarray, span_m: float = RADIUS_SPAN_M) -> np.ndarray:
    s = max(1, int(round(span_m / float(np.median(np.diff(arc_of(pts)))))))
    a, b, c = pts[:-2 * s], pts[s:-s], pts[2 * s:]
    ab, bc, ca = np.hypot(*(b - a).T), np.hypot(*(c - b).T), np.hypot(*(a - c).T)
    cross = np.abs((b[:, 0] - a[:, 0]) * (c[:, 1] - a[:, 1]) - (b[:, 1] - a[:, 1]) * (c[:, 0] - a[:, 0]))
    with np.errstate(divide="ignore", invalid="ignore"):
        return np.where(cross > 1e-9, ab * bc * ca / (2.0 * cross), np.inf)


def window_grade_deg(z: np.ndarray, window: int) -> float:
    if len(z) <= window:
        return 0.0
    return float(np.degrees(np.arctan(np.abs(z[window:] - z[:-window]).max() / window)))


def flat_widths(sampler: MeshSampler, pts: np.ndarray, z_axis: np.ndarray) -> np.ndarray:
    """Ancho (m) del tramo llano continuo que cruza el eje en cada muestra."""
    tan = tangents(pts)
    side = np.column_stack([-tan[:, 1], tan[:, 0]])
    offs = np.arange(-LATERAL_MAX_M, LATERAL_MAX_M + 1e-9, LATERAL_STEP_M)
    probes = pts[:, None, :] + side[:, None, :] * offs[None, :, None]
    z = sampler.top(probes.reshape(-1, 2)).reshape(len(pts), len(offs))
    flat = (np.abs(z - z_axis[:, None]) <= FLAT_TOL_M) & (z > WATER_M + 0.1)
    mid = len(offs) // 2
    widths = np.zeros(len(pts))
    for k in range(len(pts)):
        if not flat[k, mid]:
            continue
        right = mid
        while right + 1 < len(offs) and flat[k, right + 1]:
            right += 1
        left = mid
        while left - 1 >= 0 and flat[k, left - 1]:
            left -= 1
        widths[k] = (right - left) * LATERAL_STEP_M
    return widths


def _covered(manifest: dict, point: np.ndarray, margin_m: float = 10.0) -> bool:
    """El punto (m) cae junto al eje de un túnel o un tablero del manifest."""
    for item in manifest.get("tunnels", []) + manifest.get("decks", []):
        a, b = np.array(item["a_uu"][:2]) / UU_PER_M, np.array(item["b_uu"][:2]) / UU_PER_M
        d = b - a
        t = float(np.clip(np.dot(point - a, d) / max(float(np.dot(d, d)), 1e-9), 0.0, 1.0))
        if np.hypot(*(point - (a + t * d))) <= item.get("width_m", 14.0) / 2.0 + margin_m:
            return True
    return False


def checkpoint_report(manifest: dict, pts: np.ndarray, z_axis: np.ndarray) -> dict:
    arc = arc_of(pts)
    tan = tangents(pts)

    def locate(p_uu):
        p = np.array(p_uu[:2]) / UU_PER_M
        k = int(np.argmin(np.hypot(*(pts - p).T)))
        return k, float(np.hypot(*(pts[k] - p)))

    cps = manifest.get("checkpoints_uu", [])
    ks, offsets, yaw_err, dz, covered = [], [], [], [], 0
    for cp in cps:
        k, off = locate(cp)
        ks.append(k)
        offsets.append(off)
        yaw = math.degrees(math.atan2(tan[k, 1], tan[k, 0]))
        yaw_err.append(abs((cp[3] - yaw + 180.0) % 360.0 - 180.0))
        dz.append(abs(cp[2] / UU_PER_M - z_axis[k]))
        covered += _covered(manifest, np.array(cp[:2]) / UU_PER_M)
    k_start, off_start = locate(manifest["start_uu"])
    k_end, off_end = locate(manifest["end_uu"])
    marks = [arc[k_start]] + [arc[k] for k in ks] + [arc[k_end]]
    gaps = np.diff(marks)
    return {"count": len(cps), "gap_min_m": round(float(gaps.min()), 1), "gap_max_m": round(float(gaps.max()), 1),
            "ordered": bool((gaps > 0).all()), "max_offset_m": round(max(offsets + [off_start, off_end]), 2),
            "max_yaw_err_deg": round(max(yaw_err), 1), "max_dz_m": round(max(dz), 2), "covered": covered,
            "start_arc_m": round(float(arc[k_start]), 1), "end_arc_m": round(float(arc[k_end]), 1),
            "course_m": round(float(arc[k_end] - arc[k_start]), 1)}


def corridor_report(variant_dir: Path, sampler: MeshSampler | None = None) -> dict:
    sampler = sampler or MeshSampler(variant_dir)
    manifest = sampler.manifest
    road = np.asarray(manifest["road_uu"], dtype=np.float64) / UU_PER_M
    pts = road[:, :2]
    z = sampler.top(pts)
    step = float(np.median(np.diff(arc_of(pts))))
    missing = int(np.isnan(z).sum())
    zf = np.where(np.isnan(z), -1e3, z)
    widths = flat_widths(sampler, pts, zf)
    r = radii(pts)
    return {"road_m": round(float(arc_of(pts)[-1]), 1), "step_m": round(step, 3), "missing_samples": missing,
            "sustained_grade_deg": round(window_grade_deg(zf, int(round(SUSTAINED_M / step))), 2),
            "short_grade_deg": round(window_grade_deg(zf, int(round(SHORT_M / step))), 2),
            "max_step_m": round(float(np.abs(np.diff(zf)).max()), 3),
            "min_width_m": round(float(widths.min()), 1), "min_radius_m": round(float(r.min()), 1),
            "min_above_water_m": round(float(zf.min() - WATER_M), 2),
            "checkpoints": checkpoint_report(manifest, pts, zf)}


def corridor_fingerprint(variant_dir: Path, manifest: dict | None = None) -> dict:
    """El corredor en coordenadas locales del marco (sin manifest["offset_uu"], el desplazamiento de #532): sha256
    de cada trozo con colisión por su (col, fila) local y lo que el juego lee en coordenadas de mundo, redondeado a
    0,1 uu. Dos variantes con la misma huella tienen el mismo corredor aunque la rejilla haya crecido."""
    m = manifest or json.loads((variant_dir / "manifest.json").read_text(encoding="utf-8"))
    ox, oy = m.get("offset_uu", [0.0, 0.0])
    dr, dc = round(ox / m["cell_uu"]), round(oy / m["cell_uu"])

    def local(p):
        return [round(p[0] - ox, 1), round(p[1] - oy, 1)] + [round(v, 1) for v in p[2:]]

    cells = {f"r{c['row'] - dr}c{c['col'] - dc}": hashlib.sha256((variant_dir / c["file"]).read_bytes()).hexdigest()
             for c in m["cells"] if c.get("collision", True)}
    return {"cells": cells, "road_uu": [local(p) for p in m["road_uu"]],
            "checkpoints_uu": [local(p) for p in m["checkpoints_uu"]],
            "start_uu": local(m["start_uu"]), "end_uu": local(m["end_uu"]), "start_yaw": m["start_yaw"],
            "markers_uu": {k: [local(p) for p in v] for k, v in sorted(m["markers_uu"].items())}}
