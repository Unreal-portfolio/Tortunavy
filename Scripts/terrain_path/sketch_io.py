"""Montaje, comprobacion y salida de un mapa de boceto (terrain_path/sketch_model.py) sobre una rejilla
rectangular de cols x rows trozos de 100 m (los generadores de C01 suponen un cuadrado de 4 x 4).

  - build_chunks / assemble: trozos de terrain_vol.mesh y sus rejillas globales;
  - check: recorrido a pie sobre el volumen (gen_terrain_volume.walk, como gen_terrain_path.check):
    salida -> mar, cada tunel de boca a boca por dentro, salida de la hondonada y sin pisar paredes;
  - route: camino principal (sin tuneles) y su longitud;
  - write_outer_rect: corona barata sin colision alrededor del rectangulo (outer.py para 4 x 4);
  - lamina: boceto y vista cenital lado a lado, con las bocas de los tuneles.
"""

from __future__ import annotations

from types import SimpleNamespace

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage
from skimage.graph import MCP_Geometric, route_through_array

from gen_terrain_volume import ground_level, walk
from terrain_vol.density import smooth
from terrain_vol.export import write_chunk
from terrain_vol.layout import CELL_M, CELL_SAMPLES, UU_PER_M, WATER_M
from terrain_vol.mesh import build_chunk, vertex_colors, z_levels

from . import field
from .model import walkable
from .sketch import SEA, load_rgb

SPRINT_MS, WALK_MS = 4.0, 2.0


def build_chunks(model) -> dict:
    return {(col, row): build_chunk(model, col, row) for row in range(model.spec.rows) for col in range(model.spec.cols)}


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
    band = np.zeros((ni, nj), bool)
    floor_grid = np.full((ni, nj), np.nan)
    for (x, y), z in zip(pts, floor):
        i, j = index_of(model, (x, y))
        r = int(axis["half"] * 0.6)
        band[max(i - r, 0):i + r + 1, max(j - r, 0):j + r + 1] = True
        floor_grid[max(i - r, 0):i + r + 1, max(j - r, 0):j + r + 1] = z
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


def check(model, chunks) -> dict:
    stand, top = standable_grid(model, chunks)
    s_ij, e_ij = index_of(model, model.start), index_of(model, model.end)
    start = (*s_ij, ground_level(stand, *s_ij))
    seen = walk(stand, start)
    flat = seen.any(axis=2)
    end_ok = bool(flat[e_ij[0] - 2:e_ij[0] + 3, e_ij[1] - 2:e_ij[1] + 3].any())
    e_any = interior(model, model.e_any)
    tunnel = interior(model, model.grid.tunnel) > 0.3
    wall_top = int((flat & (e_any > 3.0) & ~tunnel).sum())
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


def route(model, seen: np.ndarray, use_tunnels: bool) -> tuple[np.ndarray, float]:
    """Camino mas corto a pie de la salida a la meta sobre lo alcanzado (puntos del mundo, metros)."""
    flat = seen.any(axis=2)
    if not use_tunnels:
        flat = flat & ~(interior(model, model.grid.tunnel) > 0.3)
    cost = np.where(flat, 1.0, np.inf)
    s_ij, e_ij = index_of(model, model.start), index_of(model, model.end)
    e_ij = tuple(np.argwhere(flat[e_ij[0] - 2:e_ij[0] + 3, e_ij[1] - 2:e_ij[1] + 3])[0] + np.array(e_ij) - 2)
    path, cost_total = route_through_array(cost, s_ij, e_ij, fully_connected=True, geometric=True)
    pts = np.array([[model.x_min + i, model.y_min + j] for i, j in path], dtype=np.float64)
    return pts, float(cost_total)


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


def _outer_cell(model, x0: float, y0: float, decimate_m: float):
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
        local, normals, colors, tris = decimate_mesh(local, normals, colors, tris, max_error_m=decimate_m)
    return center, SimpleNamespace(vertices=local.astype(np.float32), normals=normals, colors=colors, triangles=tris,
                                   instances=np.zeros((0, 11), np.float32))


def write_outer_rect(model, out, name: str, decimate_m: float) -> list[dict]:
    (out / "Outer").mkdir(exist_ok=True)
    cells = []
    xs = np.arange(model.x_min - OUTER_M, model.x_max + OUTER_M - 1e-6, CELL_OUT_M)
    ys = np.arange(model.y_min - OUTER_M, model.y_max + OUTER_M - 1e-6, CELL_OUT_M)
    for i, x0 in enumerate(xs):
        for j, y0 in enumerate(ys):
            if model.x_min <= x0 < model.x_max and model.y_min <= y0 < model.y_max:
                continue
            center, mesh = _outer_cell(model, float(x0), float(y0), decimate_m)
            file = f"Outer/o{i}_{j}.bin"
            write_chunk(out / file, mesh)
            cells.append({"name": f"M_{name}_outer_{i}_{j}", "file": file, "collision": False,
                          "center_uu": [center[0] * UU_PER_M, center[1] * UU_PER_M],
                          "vertices": int(len(mesh.vertices)), "triangles": int(len(mesh.triangles)), "instances": 0})
    return cells


# -- lamina ------------------------------------------------------------------------------------
def lamina(model, top: np.ndarray, route_pts: np.ndarray, path) -> None:
    """Boceto (izquierda) y vista cenital sombreada del relieve (derecha) a la misma escala, Norte arriba,
    con las bocas de los tuneles (circulos rojos), el eje de cada tunel y el camino principal."""
    spec = model.spec
    gx, gy = np.gradient(top)
    light = np.clip((gx * 0.5 - gy * 0.35 + 1.0) / np.sqrt(gx * gx + gy * gy + 1.0) * 0.8, 0.0, 1.0)
    h = np.clip((top - WATER_M) / 30.0, 0.0, 1.0)
    rgb = np.stack([0.55 + 0.4 * h, 0.42 + 0.35 * h, 0.25 + 0.2 * h], -1) * (0.35 + 0.65 * light)[..., None]
    rgb = np.where((top < WATER_M)[..., None], np.array([0.1, 0.35, 0.65]), rgb)
    contour = (np.floor((top - WATER_M) / 2.0) != np.floor((np.roll(top, 1, 0) - WATER_M) / 2.0))
    rgb = np.where(contour[..., None], rgb * 0.85, rgb)
    relief = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)[::-1])
    scale = 2
    relief = relief.resize((relief.width * scale, relief.height * scale), Image.NEAREST)
    # Boceto recortado al rectangulo del mapa y llevado a la misma escala.
    from .sketch_model import SCRIPTS
    sketch = _crop_scaled(Image.fromarray(load_rgb(SCRIPTS / spec.image)), spec, model, relief.size)
    sheet = Image.new("RGB", (relief.width * 2 + 30, relief.height + 60), (245, 240, 230))
    sheet.paste(sketch, (0, 60))
    sheet.paste(relief, (relief.width + 30, 60))
    draw = ImageDraw.Draw(sheet)

    def to_px(p, offset):
        return offset + (p[1] - model.y_min) * scale, 60 + (model.x_max - p[0]) * scale

    for offset in (0, relief.width + 30):
        pts = [to_px(p, offset) for p in route_pts[::4]]
        draw.line(pts, fill=(200, 30, 30), width=3)
        for ax in model.tunnel_axes:
            s0, s1 = ax["covered"]
            sel = (ax["s"] >= s0) & (ax["s"] <= s1)
            draw.line([to_px(p, offset) for p in ax["pts"][sel][::4]], fill=(60, 60, 60), width=3)
            for s in (s0, s1):
                k = int(np.argmin(np.abs(ax["s"] - s)))
                cx, cy = to_px(ax["pts"][k], offset)
                draw.ellipse((cx - 14, cy - 14, cx + 14, cy + 14), outline=(220, 0, 0), width=4)
        for p, color in ((model.start, (0, 160, 0)), (model.end, (0, 0, 200))):
            cx, cy = to_px(p, offset)
            draw.rectangle((cx - 9, cy - 9, cx + 9, cy + 9), fill=color)
    draw.text((10, 10), f"{spec.name}: boceto (izq.) y relieve generado (der.). Escala {spec.m_per_px} m/px; "
                        f"rojo: camino principal; circulos: bocas de tunel; verde: salida; azul: meta", fill=(0, 0, 0))
    sheet.save(path)


def _crop_scaled(sketch: Image.Image, spec, model, size) -> Image.Image:
    """El trozo del boceto que cubre el mapa (lo que cae fuera, color de pared), al tamano dado."""
    left, top_px = spec.world_to_px(model.x_max, model.y_min)
    right, bottom = spec.world_to_px(model.x_min, model.y_max)
    margin = 800
    canvas = Image.new("RGB", (sketch.width + 2 * margin, sketch.height + 2 * margin), (143, 89, 0))
    canvas.paste(sketch, (margin, margin))
    box = tuple(int(round(float(v))) + margin for v in (left, top_px, right, bottom))
    return canvas.crop(box).resize(size, Image.BILINEAR)


def sea_mask(model) -> np.ndarray:
    return interior(model, model.labels) == SEA
