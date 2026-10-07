"""Montaje, comprobacion y salida de un mapa de boceto (terrain_path/sketch_model.py) sobre una rejilla
rectangular de cols x rows trozos de 100 m (los generadores de C01 suponen un cuadrado de 4 x 4).

  - build_chunks / assemble: trozos de terrain_vol.mesh y sus rejillas globales;
  - check: recorrido a pie sobre el volumen (gen_terrain_volume.walk, como gen_terrain_path.check):
    salida -> mar, cada tunel de boca a boca por dentro, salida de la hondonada y sin pisar paredes;
  - route: camino principal (sin tuneles) y su longitud;
  - write_outer_rect: corona barata sin colision alrededor del rectangulo (outer.py para 4 x 4);
  - la lamina (boceto y mapa de alturas lado a lado) esta en sketch_sheet.py.
"""

from __future__ import annotations

from dataclasses import is_dataclass, replace
from types import SimpleNamespace

import numpy as np
from scipy import ndimage
from scipy.spatial import cKDTree

from gen_terrain_volume import ground_level, walk
from terrain_vol.density import smooth
from terrain_vol.export import write_chunk
from terrain_vol.layout import CELL_M, CELL_SAMPLES, UU_PER_M, WATER_M
from terrain_vol.mesh import build_chunk, vertex_colors, z_levels

from . import field
from .model import walkable
from .sketch import SEA
from .sketch_metrics import centerline

SPRINT_MS, WALK_MS = 4.0, 2.0


SEAM_CLEAR_UU = 1.0      # un vertice que no esta en la costura queda al menos a esto de ella


def clear_seams(chunk):
    """Aparta de la costura (hasta SEAM_CLEAR_UU, 1 cm) los vertices que quedan a menos de eso sin estar en
    ella. El marching cubes los deja asi donde la densidad roza 0 en el borde y la decimacion puede acercar
    alguno: el vecino no los tiene y la comprobacion de costuras (vertice a vertice, 0,5 uu) los da por
    grieta. Los de la costura (exactos, fijos al decimar) no se tocan."""
    half = CELL_M / 2.0 * UU_PER_M
    v = np.array(chunk.vertices, dtype=np.float32, copy=True)
    for axis in (0, 1):
        gap = half - np.abs(v[:, axis])
        near = (gap > 1e-3) & (gap < SEAM_CLEAR_UU)
        v[near, axis] = np.sign(v[near, axis]) * (half - SEAM_CLEAR_UU)
    fields = {"vertices": v}
    return replace(chunk, **fields) if is_dataclass(chunk) else type(chunk)(**{**vars(chunk), **fields})


def build_chunks(model) -> dict:
    return {(col, row): clear_seams(build_chunk(model, col, row))
            for row in range(model.spec.rows) for col in range(model.spec.cols)}


def assemble(chunks: dict, rows: int, cols: int, attr: str) -> np.ndarray:
    """Rejilla global (filas = Norte) de un atributo de los trozos ("top" o "standable")."""
    first = getattr(next(iter(chunks.values())), attr)
    n = CELL_SAMPLES - 1
    out = np.zeros((rows * n + 1, cols * n + 1) + first.shape[2:], dtype=first.dtype)
    for (col, row), chunk in chunks.items():
        block = getattr(chunk, attr)
        sl = (slice(row * n, row * n + CELL_SAMPLES), slice(col * n, col * n + CELL_SAMPLES))
        out[sl] = (out[sl] | block) if first.dtype == bool else block
    return out


def index_of(model, p) -> tuple[int, int]:
    """(fila, columna) de la rejilla del mapa sin margen (la de assemble)."""
    return int(round(p[0] - model.x_min)), int(round(p[1] - model.y_min))


def interior(model, grid: np.ndarray) -> np.ndarray:
    """Recorte de una rejilla del modelo (con PAD) a la del mapa."""
    from .sketch_model import PAD
    return grid[PAD:grid.shape[0] - PAD, PAD:grid.shape[1] - PAD]


# -- comprobacion ------------------------------------------------------------------------------
def standable_grid(model, chunks) -> tuple[np.ndarray, np.ndarray]:
    stand = assemble(chunks, model.spec.rows, model.spec.cols, "standable")
    top = assemble(chunks, model.spec.rows, model.spec.cols, "top")
    return walkable(stand, interior(model, model.grid.height), z_levels()), top


def _level_near(stand: np.ndarray, i: int, j: int, z: float) -> int:
    levels = np.nonzero(stand[i, j])[0]
    if not len(levels):
        return -1
    zs = z_levels()[levels]
    return int(levels[np.argmin(np.abs(zs - z))])


def tunnel_walk(model, stand: np.ndarray, axis: dict) -> dict:
    """Recorrido de boca a boca por dentro del tunel: walk() limitado a la franja del eje (semiancho) y a
    1,5 m del suelo del tunel, desde la primera muestra del eje hasta la ultima."""
    pts, floor = axis["pts"], axis["floor"]
    s0, s1 = axis["covered"]
    sel = (axis["s"] >= s0 - 15.0) & (axis["s"] <= s1 + 15.0)
    pts, floor = pts[sel], floor[sel]
    ni, nj, nk = stand.shape
    # Cada celda toma el suelo de la muestra del eje mas cercana (en una cueva que baja deprisa, las franjas
    # de muestras vecinas se pisaban y la boca se quedaba con el suelo de mas abajo).
    halves = axis["half_s"][sel]
    ii, jj = np.mgrid[0:ni, 0:nj]
    dist, k = cKDTree(pts).query(np.stack([model.x_min + ii.ravel(), model.y_min + jj.ravel()], axis=1))
    band = (dist < 0.6 * halves[k]).reshape(ni, nj)
    floor_grid = np.where(band, floor[k].reshape(ni, nj), np.nan)
    zl = z_levels()
    near_floor = np.abs(zl[None, None, :] - np.nan_to_num(floor_grid, nan=1e3)[..., None]) < 1.5
    limited = stand & band[..., None] & near_floor
    a, b = index_of(model, pts[0]), index_of(model, pts[-1])
    ka, kb = _level_near(limited, *a, floor[0]), _level_near(limited, *b, floor[-1])
    if ka < 0 or kb < 0:
        return {"ok": False, "reason": "boca sin suelo"}
    forward = walk(limited, (*a, ka), dry_only=False)[b[0], b[1], kb]
    back = walk(limited, (*b, kb), dry_only=False)[a[0], a[1], ka]
    return {"ok": bool(forward and back), "ida": bool(forward), "vuelta": bool(back),
            "largo_m": round(float(s1 - s0), 1), "pendiente": round(abs(axis["z"][1] - axis["z"][0]) / (s1 - s0), 3)}


def surface_seen(seen: np.ndarray, top: np.ndarray) -> np.ndarray:
    """Columnas alcanzadas a la cota de su superficie (top, +-1 m): sin el suelo de las cuevas de debajo."""
    zl = z_levels()
    k = np.rint((top - zl[0]) / (zl[1] - zl[0])).astype(int)
    out = np.zeros(top.shape, dtype=bool)
    for dk in range(-2, 3):
        kk = np.clip(k + dk, 0, seen.shape[2] - 1)
        out |= np.take_along_axis(seen, kk[..., None], axis=2)[..., 0]
    return out


def check(model, chunks) -> dict:
    stand, top = standable_grid(model, chunks)
    s_ij, e_ij = index_of(model, model.start), index_of(model, model.end)
    start = (*s_ij, ground_level(stand, *s_ij))
    seen = walk(stand, start)
    flat = seen.any(axis=2)
    end_ok = bool(flat[e_ij[0] - 2:e_ij[0] + 3, e_ij[1] - 2:e_ij[1] + 3].any())
    e_any = interior(model, model.e_any)
    tunnel = interior(model, model.grid.tunnel) > 0.3
    wall_top = int((surface_seen(seen, top) & (e_any > 3.0) & ~tunnel).sum())
    tunnels = {ax["name"]: tunnel_walk(model, stand, ax) for ax in model.tunnel_axes}
    for ax in model.tunnel_axes:
        mid = ax["pts"][len(ax["pts"]) // 2]
        i, j = index_of(model, mid)
        k = _level_near(stand, i, j, float(np.interp(len(ax["pts"]) // 2, np.arange(len(ax["floor"])), ax["floor"])))
        tunnels[ax["name"]]["desde_salida"] = bool(k >= 0 and seen[i, j, k])
    labels = interior(model, model.labels)
    hollow = model.spec.extra.get("hollow_class")
    hollow_out = None
    if hollow is not None:
        cells = np.argwhere(labels == hollow)
        ci, cj = cells[len(cells) // 2]
        k = ground_level(stand, ci, cj)
        back = walk(stand, (int(ci), int(cj), k))
        hollow_out = bool(back[e_ij[0] - 2:e_ij[0] + 3, e_ij[1] - 2:e_ij[1] + 3].any())
    ok = end_ok and wall_top == 0 and all(t["ok"] and t["desde_salida"] for t in tunnels.values()) \
        and hollow_out is not False
    return {"ok": ok, "meta": end_ok, "paredes_pisadas": wall_top, "tuneles": tunnels, "salida_hondonada": hollow_out,
            "seen": seen, "top": top, "stand": stand}


def route(model, seen: np.ndarray, top: np.ndarray, use_tunnels: bool) -> tuple[np.ndarray, float]:
    """Recorrido a pie de la salida a la meta por el centro de lo alcanzado (sketch_metrics.centerline):
    puntos del mundo y largo en metros. Sin tuneles: solo la superficie, sin las bocas ni el suelo de las
    cuevas (que pasan por debajo de las zonas)."""
    flat = seen.any(axis=2)
    if not use_tunnels:
        flat = surface_seen(seen, top) & ~(interior(model, model.grid.tunnel) > 0.3)
    s_ij, e_ij = index_of(model, model.start), index_of(model, model.end)
    goal = np.zeros_like(flat)
    goal[e_ij[0] - 2:e_ij[0] + 3, e_ij[1] - 2:e_ij[1] + 3] = True
    path, length = centerline(flat, s_ij, goal & flat)
    return path + np.array([model.x_min, model.y_min]), length


def cave_clearance(model, axis: dict, step: int = 8) -> float:
    """Mayor altura libre (m) de suelo a techo en el tramo cubierto, columna a columna sobre el eje."""
    from .sketch_model import PAD
    zl = z_levels()
    s0, s1 = axis["covered"]
    best = 0.0
    for p, s, fz in zip(axis["pts"][::step], axis["s"][::step], axis["floor"][::step]):
        if not s0 <= s <= s1:
            continue
        i, j = index_of(model, p)
        f = model.grid.window(i + PAD, i + PAD + 1, j + PAD, j + PAD + 1)
        X, Y = np.array([[model.ax_x[i + PAD]]]), np.array([[model.ax_y[j + PAD]]])
        D = model.density(X, Y, zl, f)[0, 0]
        solid = D > 0.0
        k0 = int(np.argmin(np.abs(zl - fz)))
        air = np.nonzero(~solid[k0:])[0]
        if not len(air):
            continue
        a = k0 + air[0]
        above = np.nonzero(solid[a:])[0]
        if len(above):
            best = max(best, float(zl[a + above[0]] - zl[a - 1]))
    return best


def zone_table(model, top: np.ndarray) -> list[dict]:
    """Cota media de cada zona en el relieve generado frente a su tono en el boceto (luminancia)."""
    labels = interior(model, model.labels)
    rows = []
    for cls in model.spec.zone_classes:
        mask = labels == cls
        rgb = model.spec.palette[cls]
        rows.append({"clase": cls, "zona": model.spec.extra.get("names", {}).get(cls, str(cls)),
                     "luminancia": round(0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2], 1),
                     "cota_media_m": round(float(np.median(top[mask]) - WATER_M), 2),
                     "cota_diseno_m": model.spec.zone_height_m[cls], "area_m2": int(mask.sum())})
    return rows


def legend_order_ok(table: list[dict]) -> bool:
    """Mas oscuro (menos luminancia), mas alto: la cota crece al bajar la luminancia."""
    ordered = sorted(table, key=lambda r: -r["luminancia"])
    return all(a["cota_media_m"] < b["cota_media_m"] for a, b in zip(ordered[:-1], ordered[1:]))


# -- corona ------------------------------------------------------------------------------------
OUTER_M, CELL_OUT_M, STEP_OUT_M, BLEND_M = 1000.0, 200.0, 5.0, 80.0
OUTER_COLOR_STEP = 256


def _outer_height(model, X, Y):
    xc, yc = np.clip(X, model.x_min, model.x_max), np.clip(Y, model.y_min, model.y_max)
    dist = np.hypot(X - xc, Y - yc)
    from .sketch_model import STEP_XY_M
    border = ndimage.map_coordinates(model.grid.height, [(xc - model.ax_x[0]) / STEP_XY_M,
                                                         (yc - model.ax_y[0]) / STEP_XY_M], order=1, mode="nearest")
    dunes = field.dune_field(model, X, Y, model.dune_angle, model.style.vista_dune_wave_m[1])
    far = WATER_M + 1.5 + 2.2 * dunes + 3.0 * model.n_big.unit(X, Y) + 6.0 * smooth(250.0, OUTER_M, dist)
    sea = smooth(0.0, 40.0, X - model.x_max)
    far = far * (1.0 - sea) + (WATER_M - 4.0) * sea
    t = smooth(0.0, BLEND_M, dist)
    return border * (1.0 - t) + far * t


def _outer_cell(model, x0: float, y0: float, decimate_m: float, color_step: int):
    n = int(round(CELL_OUT_M / STEP_OUT_M)) + 1
    xs, ys = x0 + STEP_OUT_M * np.arange(-1, n + 1), y0 + STEP_OUT_M * np.arange(-1, n + 1)
    Xp, Yp = np.meshgrid(xs, ys, indexing="ij")
    tol = 1e-6
    sink = np.zeros(Xp.shape)
    for edge, sign, axis in ((model.x_min, 1.0, 0), (model.x_max, -1.0, 0), (model.y_min, 1.0, 1), (model.y_max, -1.0, 1)):
        C, O = (Xp, Yp) if axis == 0 else (Yp, Xp)
        lo, hi = (model.y_min, model.y_max) if axis == 0 else (model.x_min, model.x_max)
        hit = (np.abs(C - edge) < tol) & (O >= lo - tol) & (O <= hi + tol)
        C[hit] += sign * 3.0
        sink[hit] = 0.35
    Hp = _outer_height(model, Xp, Yp) - sink
    gx, gy = (g[1:-1, 1:-1] for g in np.gradient(Hp, STEP_OUT_M))
    X, Y, H = Xp[1:-1, 1:-1], Yp[1:-1, 1:-1], Hp[1:-1, 1:-1]
    normals = np.stack([-gx, -gy, np.ones_like(H)], axis=-1)
    normals /= np.linalg.norm(normals, axis=-1, keepdims=True)
    world = np.stack([X, Y, H], axis=-1).reshape(-1, 3)
    normals = normals.reshape(-1, 3)
    idx = np.arange(n * n).reshape(n, n)
    a, b, c, d = idx[:-1, :-1].ravel(), idx[1:, :-1].ravel(), idx[:-1, 1:].ravel(), idx[1:, 1:].ravel()
    tris = np.concatenate([np.stack([a, b, c], 1), np.stack([b, d, c], 1)])
    cross = np.cross(world[tris[:, 1]] - world[tris[:, 0]], world[tris[:, 2]] - world[tris[:, 0]])
    flip = cross[:, 2] > 0.0
    tris[flip] = tris[flip][:, [0, 2, 1]]
    colors = vertex_colors(model, world, normals)
    center = np.array([x0 + CELL_OUT_M / 2.0, y0 + CELL_OUT_M / 2.0])
    local = (world - np.array([center[0], center[1], 0.0])) * UU_PER_M
    normals, tris = normals.astype(np.float32), tris.astype(np.uint32)
    if decimate_m > 0.0:
        from terrain_vol.decimate import decimate_mesh
        local, normals, colors, tris = decimate_mesh(local, normals, colors, tris, max_error_m=decimate_m,
                                                     color_step=color_step)
    return center, SimpleNamespace(vertices=local.astype(np.float32), normals=normals, colors=colors, triangles=tris,
                                   instances=np.zeros((0, 11), np.float32))


def write_outer_rect(model, out, name: str, decimate_m: float, color_step: int = OUTER_COLOR_STEP) -> list[dict]:
    """Corona sin colision alrededor del rectangulo. Sin costuras de color (color_step 256): son dunas
    lejanas sin lineas que conservar y las costuras fijaban la mitad de sus vertices."""
    (out / "Outer").mkdir(exist_ok=True)
    cells = []
    xs = np.arange(model.x_min - OUTER_M, model.x_max + OUTER_M - 1e-6, CELL_OUT_M)
    ys = np.arange(model.y_min - OUTER_M, model.y_max + OUTER_M - 1e-6, CELL_OUT_M)
    for i, x0 in enumerate(xs):
        for j, y0 in enumerate(ys):
            if model.x_min <= x0 < model.x_max and model.y_min <= y0 < model.y_max:
                continue
            center, mesh = _outer_cell(model, float(x0), float(y0), decimate_m, color_step)
            file = f"Outer/o{i}_{j}.bin"
            write_chunk(out / file, mesh)
            cells.append({"name": f"M_{name}_outer_{i}_{j}", "file": file, "collision": False,
                          "center_uu": [center[0] * UU_PER_M, center[1] * UU_PER_M],
                          "vertices": int(len(mesh.vertices)), "triangles": int(len(mesh.triangles)), "instances": 0})
    return cells


def sea_mask(model) -> np.ndarray:
    return interior(model, model.labels) == SEA
