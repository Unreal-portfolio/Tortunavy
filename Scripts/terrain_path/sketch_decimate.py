"""Decimación por regiones de los trozos de un mapa de boceto (#875).

Lo que el jugador pisa o tiene delante (suelo, pared hasta la cresta, mar junto a la orilla) se decima
con el error de C01 (5 cm). El fondo de vistas que queda detrás de la cresta de las paredes no se
alcanza nunca (la comprobación exige cero celdas de pared pisadas) y solo se ve de lejos y en escorzo:
se decima con un error mayor. Cada trozo se parte en dos submallas por triángulo; las dos se deciman
por separado con el borde abierto fijo (terrain_vol.decimate), así que la línea de corte entre ellas
no se mueve y, al unirlas, los vértices comunes se sueldan sin grieta.
"""

from __future__ import annotations

from dataclasses import is_dataclass, replace

import numpy as np

from terrain_vol.decimate import decimate_mesh, spatial_order
from terrain_vol.layout import UU_PER_M

NEAR_ERROR_M = 0.05          # el de C01 (gen_terrain_path.DECIMATE_M)
FAR_ERROR_M = 0.45           # fondo de vistas tras la cresta: no se pisa y no se ve de cerca
NEAR_COLOR_STEP = 10         # el de terrain_vol.decimate: la orilla y el pie de pared conservan su línea
FAR_COLOR_STEP = 256         # en el fondo de vistas el color no fija vértices (no hay líneas que guardar)


def _submesh(vertices, normals, colors, triangles, mask):
    sub = triangles[mask]
    used = np.unique(sub)
    remap = np.full(len(vertices), -1, dtype=np.int64)
    remap[used] = np.arange(len(used))
    return vertices[used], normals[used], colors[used], remap[sub]


def _merge(parts):
    """Une submallas y suelda los vértices con la misma posición exacta (la línea de corte)."""
    v = np.concatenate([p[0] for p in parts])
    n = np.concatenate([p[1] for p in parts])
    c = np.concatenate([p[2] for p in parts])
    offsets = np.cumsum([0] + [len(p[0]) for p in parts[:-1]])
    t = np.concatenate([p[3].astype(np.int64) + off for p, off in zip(parts, offsets)])
    uniq, first, inverse = np.unique(v, axis=0, return_index=True, return_inverse=True)
    t = inverse.reshape(-1)[t]
    t = t[(t[:, 0] != t[:, 1]) & (t[:, 1] != t[:, 2]) & (t[:, 2] != t[:, 0])]
    order, t = spatial_order(uniq, t)
    src = first[order]
    return v[src].astype(np.float32), n[src].astype(np.float32), c[src].astype(np.uint8), t.astype(np.uint32)


def decimate_split(vertices, normals, colors, triangles, far: np.ndarray, near_m: float = NEAR_ERROR_M,
                   far_m: float = FAR_ERROR_M, near_color_step: int = NEAR_COLOR_STEP):
    """Decima una malla de trozo (uu locales) con near_m donde far es False y far_m donde es True
    (far: un booleano por triángulo)."""
    triangles = np.asarray(triangles, dtype=np.int64)
    parts = []
    for mask, error, step in ((~far, near_m, near_color_step), (far, far_m, FAR_COLOR_STEP)):
        if not mask.any():
            continue
        parts.append(decimate_mesh(*_submesh(vertices, normals, colors, triangles, mask), max_error_m=error,
                                   color_step=step))
    return _merge(parts)


def far_triangles(model, chunk_center_m, vertices, triangles) -> np.ndarray:
    """Triángulos cuyo centro cae en el fondo de vistas del modelo (region == 2)."""
    cen = np.asarray(vertices, dtype=np.float64)[np.asarray(triangles, dtype=np.int64)].mean(axis=1) / UU_PER_M
    x = cen[:, 0] + chunk_center_m[0]
    y = cen[:, 1] + chunk_center_m[1]
    i = np.clip(np.rint(x - model.ax_x[0]).astype(int), 0, model.region.shape[0] - 1)
    j = np.clip(np.rint(y - model.ax_y[0]).astype(int), 0, model.region.shape[1] - 1)
    return model.region[i, j] == 2


def decimate_chunks_split(model, chunks: dict, near_m: float = NEAR_ERROR_M, far_m: float = FAR_ERROR_M,
                          near_color_step: int = NEAR_COLOR_STEP) -> dict:
    """decimate_split() sobre cada trozo y clear_seams() despues; el resto de campos del trozo no cambia."""
    from terrain_vol.layout import CELL_M

    from .sketch_io import clear_seams
    out = {}
    for (col, row), chunk in chunks.items():
        center = ((row + 0.5) * CELL_M + model.x_min, (col + 0.5) * CELL_M + model.y_min)
        far = far_triangles(model, center, chunk.vertices, chunk.triangles)
        v, n, c, t = decimate_split(chunk.vertices, chunk.normals, chunk.colors, chunk.triangles, far, near_m, far_m,
                                    near_color_step)
        fields = {"vertices": v, "normals": n, "colors": c, "triangles": t}
        done = replace(chunk, **fields) if is_dataclass(chunk) else type(chunk)(**{**vars(chunk), **fields})
        out[(col, row)] = clear_seams(done)
    return out
