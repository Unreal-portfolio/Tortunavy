"""Medidas de aceptación de un mapa de boceto (#875) y lo que se guarda para que los tests las rehagan
sin regenerar el mapa (10 minutos):

  - validacion.npz en la carpeta de la variante: rejillas de 1 m del mapa (cota superior de la malla,
    relieve del modelo, regiones, clases del boceto, distancia al borde de lo jugable, mesetas, cuestas,
    túneles) y la salida y la meta;
  - bloque "tuneles" del manifest: eje de cada túnel (cada 2 m) con la cota prevista del suelo;
  - bloque "metricas" del manifest: las cifras de abajo.

La referencia de C01 (altura de pared y triángulos) se mide igual sobre su modelo y su manifest.
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
from scipy import ndimage

from terrain_vol.layout import UU_PER_M, WATER_M
from terrain_vol.validate import reachable

from . import sketch_metrics as sm
from .sketch import SEA

TUNNEL_STEP_M = 2.0
MOUTH_TRIM_M = 2.0                # el perfil empieza y acaba esto dentro de las bocas (la cara de la pared)
EDGE_M = 2.0                      # el suelo se mide a partir de esto dentro del pie de la pared (el pie se curva)


def _interior(model, grid):
    from .sketch_io import interior
    return interior(model, grid)


def masks(model) -> dict[str, np.ndarray]:
    """Rejillas del mapa (sin margen) para las medidas."""
    region = _interior(model, model.region).astype(np.uint8)
    labels = _interior(model, model.labels).astype(np.int8)
    e_any = _interior(model, model.e_any)
    tunnel = ndimage.binary_dilation(_interior(model, model.grid.tunnel) > 0.05, iterations=3)
    zone = np.isin(labels, model.spec.zone_classes)
    ramp = _interior(model, model.ramp) & zone
    floor = (region == 0) & (e_any < -EDGE_M) & ~tunnel & (labels != SEA)
    return {"region": region, "labels": labels, "e_dm": np.clip(np.rint(e_any * 10.0), -32000, 32000).astype(np.int16),
            "tunnel": tunnel, "ramp": ramp, "plateau": zone & ~ramp, "floor": floor}


def tunnel_axes(model) -> list[dict]:
    """Eje de cada túnel en su tramo cubierto (sin MOUTH_TRIM_M en cada boca), cada TUNNEL_STEP_M: x, y, cota
    prevista del suelo y semiancho (m)."""
    out = []
    for ax in model.tunnel_axes:
        s0, s1 = ax["covered"]
        s = np.arange(s0 + MOUTH_TRIM_M, s1 - MOUTH_TRIM_M + 1e-6, TUNNEL_STEP_M)
        x = np.interp(s, ax["s"], ax["pts"][:, 0])
        y = np.interp(s, ax["s"], ax["pts"][:, 1])
        z = np.interp(s, ax["s"], ax["floor"])
        h = np.interp(s, ax["s"], ax["half_s"])
        out.append({"name": ax["name"], "semiancho_m": ax["half"], "bajo_zonas": bool(ax["underground"]),
                    "eje_m": [[round(float(v), 2) for v in row] for row in zip(x, y, z, h)]})
    return out


def world_meshes(chunks: dict, model) -> list[tuple[np.ndarray, np.ndarray]]:
    from terrain_vol.layout import CELL_M
    out = []
    for (col, row), chunk in chunks.items():
        center = np.array([(row + 0.5) * CELL_M + model.x_min, (col + 0.5) * CELL_M + model.y_min, 0.0])
        out.append((np.asarray(chunk.vertices, dtype=np.float64) / UU_PER_M + center,
                    np.asarray(chunk.triangles, dtype=np.int64)))
    return out


def tunnel_triangles(meshes, axis_m: np.ndarray, reach_m: float) -> np.ndarray:
    """Triángulos (N, 3, 3) del mundo cerca del eje de un túnel."""
    lo, hi = axis_m[:, :2].min(axis=0) - reach_m, axis_m[:, :2].max(axis=0) + reach_m
    parts = []
    for vertices, triangles in meshes:
        tri = vertices[triangles]
        c = tri[:, :, :2].mean(axis=1)
        keep = np.all((c >= lo) & (c <= hi), axis=1)
        parts.append(tri[keep])
    return np.concatenate(parts)


def profiles(meshes, axes: list[dict]) -> dict:
    out = {}
    for ax in axes:
        axis = np.array(ax["eje_m"])
        tri = tunnel_triangles(meshes, axis, 6.0)
        out[ax["name"]] = sm.tunnel_profile(axis[:, :2], axis[:, 2], tri)
    return out


def measure(model, top: np.ndarray, chunks_out: dict, route_m: float, short_m: float) -> tuple[dict, dict]:
    """(métricas, rejillas para validacion.npz) del mapa ya decimado."""
    from .sketch_io import index_of
    g = masks(model)
    height = _interior(model, model.grid.height)
    meshes = world_meshes(chunks_out, model)
    axes = tunnel_axes(model)
    s_ij, e_ij = index_of(model, model.start), index_of(model, model.end)
    reach = reachable(top, s_ij)
    metrics = {
        "recorrido_m": round(route_m, 1), "recorrido_con_tuneles_m": round(short_m, 1),
        "meta_a_pie_2d": bool(reach[e_ij[0] - 2:e_ij[0] + 3, e_ij[1] - 2:e_ij[1] + 3].any()),
        "pendientes": sm.floor_slopes(top, g["plateau"], g["ramp"], g["floor"]),
        "triangulos_suelo": sm.floor_triangles(meshes, g["floor"], top, (model.x_min, model.y_min)),
        "tuneles": profiles(meshes, axes),
        "pared": sm.wall_height(height, g["region"]),
        "hondonada_bajo_media_m": hollow_drop(model, top, g),
        "triangulos": int(sum(len(c.triangles) for c in chunks_out.values())),
    }
    grids = {**{k: v for k, v in g.items() if k != "floor"},
             "top_cm": np.rint((top - WATER_M) * 100.0).astype(np.int16),
             "height_cm": np.rint((height - WATER_M) * 100.0).astype(np.int16),
             "start_ij": np.array(s_ij), "end_ij": np.array(e_ij),
             "origin_m": np.array([model.x_min, model.y_min])}
    return metrics, {"grids": grids, "axes": axes}


def finish_block(model) -> dict:
    """Meta en el mar: caja que cruza toda la orilla de la playa (celdas de playa junto al mar), con el
    centro 4 m mar adentro desde la orilla y a la cota del agua. Yaw 0: su eje X mira al Norte (al mar)."""
    labels = _interior(model, model.labels)
    shore = (labels == model.spec.beach_class) & ndimage.binary_dilation(labels == SEA, iterations=2)
    cells = np.argwhere(shore)
    x = model.x_min + float(np.percentile(cells[:, 0], 90)) + 4.0
    y0, y1 = model.y_min + float(cells[:, 1].min()), model.y_min + float(cells[:, 1].max())
    return {"center_uu": [round(x * UU_PER_M, 1), round(0.5 * (y0 + y1) * UU_PER_M, 1), round(WATER_M * UU_PER_M, 1)],
            "width_m": round(y1 - y0 + 10.0, 1), "depth_m": 8.0, "yaw_deg": 0.0}


def hollow_drop(model, top: np.ndarray, g: dict) -> float:
    """Cuanto queda la hondonada por debajo de la meseta media (medianas de la cota en sus mesetas)."""
    hollow, medium = model.spec.extra["hollow_class"], model.spec.extra["medium_class"]
    med = {c: float(np.median(top[g["plateau"] & (g["labels"] == c)])) for c in (hollow, medium)}
    return round(med[medium] - med[hollow], 2)


def save_grids(out: Path, grids: dict) -> None:
    np.savez_compressed(out / "validacion.npz", **grids)


def c01_reference(variants: Path) -> dict:
    """Altura de pared y triángulos de C01, medidos igual que los de D01."""
    from .model import PathModel
    from .style import C01_SEED, C01_STYLE
    model = PathModel(C01_SEED, C01_STYLE)
    wall = sm.wall_height(model.grid.height[1:-1, 1:-1], model.region[1:-1, 1:-1])
    return {"pared": wall, **triangle_counts(variants / "C01_camino" / "manifest.json")}


def triangle_counts(manifest_path: Path) -> dict:
    cells = json.loads(manifest_path.read_text(encoding="utf-8"))["cells"]
    near = sum(c["triangles"] for c in cells if c.get("collision", True))
    outer = sum(c["triangles"] for c in cells if not c.get("collision", True))
    return {"triangulos_trozos": near, "triangulos_corona": outer, "triangulos_total": near + outer}
