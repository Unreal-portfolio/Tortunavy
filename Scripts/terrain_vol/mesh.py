"""Malla de cada trozo: marching cubes sobre la densidad, normales del gradiente global,
color de vertice por zona e instancias del bosque de algas.

Los trozos vecinos comparten el plano de su borde y evaluan la densidad con las mismas
funciones de mundo: los vertices y normales del borde salen identicos en los dos.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import ndimage, sparse
from skimage.measure import marching_cubes

from .density import Fields, MapModel, smooth
from .layout import (CELL_SAMPLES, DEFAULT_Z_RANGE, STEP_XY_M, STEP_Z_M, UU_PER_M, WATER_M, ZRange, cell_bounds,
                     cell_center)

# Paletas lineales (las de FModuleColors y TNTerrainBiome::ColorsFor): suelo, alto, alto alt,
# pared, pared alt, humedo.
PALETTES = {
    "sand": ((0.62, 0.44, 0.21), (0.48, 0.33, 0.16), (0.56, 0.36, 0.18), (0.40, 0.27, 0.13), (0.58, 0.45, 0.27),
             (0.09, 0.065, 0.035)),
    "beach": ((0.58, 0.47, 0.29), (0.40, 0.31, 0.18), (0.47, 0.35, 0.19), (0.30, 0.23, 0.14), (0.44, 0.36, 0.22),
              (0.12, 0.10, 0.07)),
    # Arena humeda (zona encharcada): mas oscura y fria, siempre arena.
    "damp": ((0.47, 0.37, 0.21), (0.42, 0.32, 0.18), (0.46, 0.35, 0.19), (0.36, 0.27, 0.15), (0.44, 0.34, 0.20),
             (0.10, 0.08, 0.05)),
}
# Todo arena (2026-09-25): sin paleta verde en ninguna zona. La antigua zona "algae" es ahora el
# bioma DUNAS (arena lisa, sin follaje): usa la misma paleta "sand" que acantilados y canon; la
# zona encharcada ("marsh") es arena humeda.
ZONE_PALETTE = {"cliffs": "sand", "canyon": "sand", "marsh": "damp", "algae": "sand", "beach": "beach"}
TRAIL_COLOR = (0.40, 0.28, 0.14)        # arena pisada del camino principal
PLAZA_COLOR = (0.78, 0.64, 0.40)        # salida y meta: arena clara
PLANT_ALGAE = False          # 2026-09-25: sin modelos de alga ni en ninguna otra zona; solo arena

FOLIAGE_SPACING_M = 2.0
SMOOTH_ITERATIONS = 12                  # Taubin (encoge poco): quita el escalonado del marching cubes
FOLIAGE_SHAPES = {"stalk": 0, "frond": 1, "bush": 2}      # orden de TNTerrainBiome::EFoliageShape
FOLIAGE_COLORS = ((0.10, 0.14, 0.03), (0.27, 0.29, 0.07))


@dataclass
class ChunkMesh:
    col: int
    row: int
    vertices: np.ndarray        # (N, 3) float32, uu, locales al centro del trozo
    normals: np.ndarray         # (N, 3) float32
    colors: np.ndarray          # (N, 4) uint8, color LINEAL * 255
    triangles: np.ndarray       # (M, 3) uint32, cara visible segun Unreal
    instances: np.ndarray       # (K, 11) float32: forma, x, y, z (uu), yaw (grados), sx, sy, sz, r, g, b
    top: np.ndarray             # (CELL_SAMPLES, CELL_SAMPLES) cota de la superficie mas alta (m)
    standable: np.ndarray       # (CELL_SAMPLES, CELL_SAMPLES, Z_SAMPLES) bool: se puede estar de pie
    fields: Fields


def z_levels(z_range: ZRange = DEFAULT_Z_RANGE) -> np.ndarray:
    return z_range.z_values()


def model_z_range(model) -> ZRange:
    """Rango vertical del voxelizado de un modelo: su atributo `z_range` o DEFAULT_Z_RANGE."""
    return getattr(model, "z_range", None) or DEFAULT_Z_RANGE


def taubin(vertices: np.ndarray, faces: np.ndarray, pinned: np.ndarray, iterations: int) -> np.ndarray:
    """Suavizado de Taubin (lambda/mu) con los vertices pinned fijos: los del borde del trozo
    no se mueven, asi que la costura con el vecino sigue exacta."""
    n = len(vertices)
    rows = np.concatenate([faces[:, 0], faces[:, 1], faces[:, 2], faces[:, 1], faces[:, 2], faces[:, 0]])
    cols = np.concatenate([faces[:, 1], faces[:, 2], faces[:, 0], faces[:, 0], faces[:, 1], faces[:, 2]])
    adjacency = sparse.coo_matrix((np.ones(len(rows)), (rows, cols)), shape=(n, n)).tocsr()
    adjacency.data[:] = 1.0
    degree = np.asarray(adjacency.sum(axis=1)).ravel()
    free = (~pinned & (degree > 0))[:, None]
    v = vertices.copy()
    for _ in range(iterations):
        for factor in (0.5, -0.53):
            mean = adjacency @ v / np.maximum(degree, 1.0)[:, None]
            v = np.where(free, v + factor * (mean - v), v)
    return v


def mesh_normals(vertices: np.ndarray, faces: np.ndarray, count: int) -> np.ndarray:
    """Normal de vertice = suma de las normales de sus caras (ponderadas por area), con la
    cara visible de Unreal (opuesta a (B-A)x(C-A))."""
    a, b, c = vertices[faces[:, 0]], vertices[faces[:, 1]], vertices[faces[:, 2]]
    face = -np.cross(b - a, c - a)
    out = np.zeros((count, 3))
    for k in range(3):
        np.add.at(out, faces[:, k], face)
    return out / np.maximum(np.linalg.norm(out, axis=1, keepdims=True), 1e-9)


def hash_uniform(ix: np.ndarray, iy: np.ndarray, salt: int) -> np.ndarray:
    """Numero en [0, 1) determinista por celda de la rejilla global (independiente del trozo)."""
    h = (ix.astype(np.uint64) * np.uint64(0x9E3779B97F4A7C15)) ^ (iy.astype(np.uint64) * np.uint64(0xC2B2AE3D27D4EB4F)) \
        ^ np.uint64(salt * 0x165667B19E3779F9 & 0xFFFFFFFFFFFFFFFF)
    h ^= h >> np.uint64(33)
    h *= np.uint64(0xFF51AFD7ED558CCD)
    h ^= h >> np.uint64(33)
    h *= np.uint64(0xC4CEB9FE1A85EC53)
    h ^= h >> np.uint64(33)
    return (h >> np.uint64(11)).astype(np.float64) / float(1 << 53)


def vertex_colors(model: MapModel, world: np.ndarray, normals: np.ndarray) -> np.ndarray:
    x, y, z = world[:, 0], world[:, 1], world[:, 2]
    # Pesos propios del color si el modelo los da (difuminados, sin fronteras rectas).
    w = model.color_weights(x, y) if hasattr(model, "color_weights") else model.zones.weights(x, y)
    vein = 0.5 + 0.5 * np.sin(x / 53.0 + 1.7) * np.cos(y / 41.0 - 0.6)
    lo_c, hi_c = getattr(model, "cliff_band", (0.25, 0.6))
    cliff = smooth(lo_c, hi_c, 1.0 - normals[:, 2])
    cliff = np.maximum(cliff, (normals[:, 2] < -0.2).astype(np.float64))     # techos de tunel y voladizos
    high = smooth(4.0, 9.0, z) * getattr(model, "high_tint", 1.0)
    wet = smooth(WATER_M + 0.6, WATER_M, z)
    wall_mix = getattr(model, "wall_color_mix", 0.6)
    # Vetas horizontales en la pared (estratos de arena compactada), solo si el modelo las pide.
    strata = getattr(model, "wall_strata", 0.0) * cliff * (0.5 + 0.5 * np.sin(z * 2.0 * np.pi / 0.9 + 0.8 * np.sin(x / 13.0)))
    out = np.zeros((len(x), 3))
    for zone, palette_name in ZONE_PALETTE.items():
        floor, hi, hi_alt, wall, wall_alt, wet_c = (np.array(c) for c in PALETTES[palette_name])
        c = floor + (hi + (hi_alt - hi) * vein[:, None] - floor) * high[:, None]
        c = c + (wall + (wall_alt - wall) * vein[:, None] - c) * (wall_mix * cliff)[:, None]
        c = c + (wet_c - c) * wet[:, None]
        out += w[zone][:, None] * c
    # Camino principal (arena pisada, mas oscura) y salida/meta (arena clara): se leen desde lejos.
    flat_up = smooth(0.7, 0.9, normals[:, 2])
    trail = model.trail_mask_3d(x, y, z) if hasattr(model, "trail_mask_3d") else model.trail_mask(x, y)
    trail_full = trail * (1.0 - wet)
    trail = trail_full * flat_up
    trail_color = np.array(getattr(model, "trail_color", TRAIL_COLOR))
    out = out + (trail_color - out) * (getattr(model, "trail_strength", 0.7) * trail)[:, None]
    if hasattr(model, "mud_mask"):                  # barro (badén de los circuitos de tierra, #682)
        mud = model.mud_mask(x, y) * (1.0 - wet)
        out = out + (np.array(model.mud_color) - out) * (model.mud_strength * mud)[:, None]
    plaza = model.plaza_mask(x, y) * flat_up
    out = out + (np.array(PLAZA_COLOR) - out) * (0.8 * plaza)[:, None]
    out = out * (1.0 - strata)[:, None]
    tint = 1.0 + 0.07 * np.sin(x / 9.5) * np.cos(y / 7.4)
    out = np.clip(out * tint[:, None], 0.0, 1.0)
    # Alfa: 1 = arena y pared, 0 = camino (el material cambia la normal de detalle a arena pisada).
    rgba = np.concatenate([out, (1.0 - trail_full)[:, None]], axis=1)
    return np.rint(rgba * 255.0).astype(np.uint8)


def top_surface(D: np.ndarray, z_range: ZRange = DEFAULT_Z_RANGE) -> np.ndarray:
    """Cota (m) del paso de solido a aire mas alto de cada columna."""
    solid = D > 0.0
    levels = D.shape[2]
    k = levels - 1 - np.argmax(solid[:, :, ::-1], axis=2)             # indice del solido mas alto
    k = np.clip(k, 0, levels - 2)
    i, j = np.indices(k.shape)
    d0, d1 = D[i, j, k], D[i, j, k + 1]
    t = np.clip(d0 / np.maximum(d0 - d1, 1e-9), 0.0, 1.0)
    return z_range.z_min_m + z_range.step_m * (k + t)


def standable_cells(D: np.ndarray, clearance_m: float = 2.0, step_z_m: float = STEP_Z_M) -> np.ndarray:
    """Solido con aire encima en clearance_m: donde cabe la tortuga de pie."""
    solid = D > 0.0
    free = ~solid
    steps = int(round(clearance_m / step_z_m))
    ok = solid.copy()
    for k in range(1, steps + 1):
        above = np.zeros_like(free)
        above[:, :, :-k] = free[:, :, k:]
        ok &= above
    return ok


def foliage_instances(model: MapModel, col: int, row: int, fields: Fields, top: np.ndarray) -> np.ndarray:
    """Algas sobre una rejilla global de FOLIAGE_SPACING_M con desplazamiento por celda (ninguna
    si PLANT_ALGAE es False)."""
    if not PLANT_ALGAE:
        return np.zeros((0, 11), dtype=np.float32)
    x0, x1, y0, y1 = cell_bounds(col, row)
    ix = np.arange(int(np.floor(x0 / FOLIAGE_SPACING_M)), int(np.ceil(x1 / FOLIAGE_SPACING_M)))
    iy = np.arange(int(np.floor(y0 / FOLIAGE_SPACING_M)), int(np.ceil(y1 / FOLIAGE_SPACING_M)))
    IX, IY = np.meshgrid(ix, iy, indexing="ij")
    px = (IX + hash_uniform(IX, IY, 1)) * FOLIAGE_SPACING_M
    py = (IY + hash_uniform(IX, IY, 2)) * FOLIAGE_SPACING_M
    inside = (px >= x0) & (px < x1) & (py >= y0) & (py < y1)
    gi = np.clip(np.rint(px - x0).astype(int), 0, CELL_SAMPLES - 1)
    gj = np.clip(np.rint(py - y0).astype(int), 0, CELL_SAMPLES - 1)
    density = fields.foliage[gi, gj]
    ground = top[gi, gj]
    keep = inside & (hash_uniform(IX, IY, 3) < density) & (ground > WATER_M + 0.3)
    shape_roll, size_roll = hash_uniform(IX, IY, 4)[keep], hash_uniform(IX, IY, 5)[keep]
    yaw, tint = hash_uniform(IX, IY, 6)[keep] * 360.0, hash_uniform(IX, IY, 7)[keep]
    cx, cy = cell_center(col, row)
    lx, ly = (px[keep] - cx) * UU_PER_M, (py[keep] - cy) * UU_PER_M
    gz = ground[keep] * UU_PER_M - 20.0
    tall = 400.0 + (1200.0 - 400.0) * size_roll ** 2
    bush = shape_roll < 0.25
    frond = (shape_roll >= 0.25) & (shape_roll < 0.55)
    width = 150.0 + 170.0 * size_roll
    shape = np.where(bush, FOLIAGE_SHAPES["bush"], np.where(frond, FOLIAGE_SHAPES["frond"], FOLIAGE_SHAPES["stalk"]))
    shape = shape.astype(np.float64)
    sx = np.where(bush, width, np.where(frond, 90.0, 45.0)) / 100.0
    sy = np.where(bush, width * 0.8, np.where(frond, 60.0, 45.0)) / 100.0
    sz = np.where(bush, width * 0.55, tall) / 100.0
    pivot = np.where(bush, width * 0.2, tall * 0.5)
    lo, hi = np.array(FOLIAGE_COLORS[0]), np.array(FOLIAGE_COLORS[1])
    color = lo + (hi - lo) * tint[:, None]
    return np.column_stack([shape, lx, ly, gz + pivot, yaw, sx, sy, sz, color]).astype(np.float32)


def build_chunk(model: MapModel, col: int, row: int, z_range: ZRange | None = None) -> ChunkMesh:
    """z_range: rango vertical del voxelizado (por defecto el del modelo o DEFAULT_Z_RANGE)."""
    zr = z_range or model_z_range(model)
    # Una muestra de margen para las normales (gradiente centrado tambien en el borde).
    fields_pad, X, Y = model.chunk_fields(col, row, pad=1)
    Z = z_levels(zr)
    D_pad = model.density(X, Y, Z, fields_pad)
    D = D_pad[1:-1, 1:-1, :]
    fields = fields_pad.window(1, CELL_SAMPLES + 1, 1, CELL_SAMPLES + 1)

    verts, faces, _, _ = marching_cubes(D, level=0.0, spacing=(STEP_XY_M, STEP_XY_M, zr.step_m))
    edge = CELL_SAMPLES - 1
    pinned = (verts[:, 0] < 1e-6) | (verts[:, 0] > edge * STEP_XY_M - 1e-6) \
        | (verts[:, 1] < 1e-6) | (verts[:, 1] > edge * STEP_XY_M - 1e-6)
    smoothed = taubin(verts, faces, pinned, SMOOTH_ITERATIONS)
    # (las normales de la malla se calculan tras orientar las caras: ver mesh_normals)
    x0, _, y0, _ = cell_bounds(col, row)
    world = smoothed + np.array([x0, y0, zr.z_min_m])

    grad = np.gradient(D_pad, STEP_XY_M, STEP_XY_M, zr.step_m)
    coords = np.stack([verts[:, 0] / STEP_XY_M + 1, verts[:, 1] / STEP_XY_M + 1, verts[:, 2] / zr.step_m], axis=0)
    g = np.stack([ndimage.map_coordinates(c, coords, order=1, mode="nearest") for c in grad], axis=1)
    normals = -g / np.maximum(np.linalg.norm(g, axis=1, keepdims=True), 1e-9)

    # Cara visible de Unreal: la opuesta a (B-A)x(C-A). El marching cubes ya da un sentido coherente
    # en toda la malla: se decide UNA vez (voto de todas las caras contra la normal del campo). Antes
    # se orientaba cada triangulo por separado y, donde el gradiente del campo y la cara no coincidian
    # (laminas finas junto a los estribos), quedaban caras giradas respecto a sus vecinas.
    a, b, c = world[faces[:, 0]], world[faces[:, 1]], world[faces[:, 2]]
    cross = np.cross(b - a, c - a)
    face_n = normals[faces].mean(axis=1)
    faces = faces.copy()
    if np.einsum("ij,ij->i", cross, face_n).sum() > 0.0:
        faces = faces[:, [0, 2, 1]]

    # Normales de la propia malla (suma de caras): la luz sigue a los triangulos y las
    # sombras no salen a dientes. En el borde del trozo, las del campo: iguales en el vecino.
    normals = np.where(pinned[:, None], normals, mesh_normals(world, faces, len(world)))

    cx, cy = cell_center(col, row)
    local = (world - np.array([cx, cy, 0.0])) * UU_PER_M
    top = top_surface(D, zr)
    return ChunkMesh(col, row, local.astype(np.float32), normals.astype(np.float32), vertex_colors(model, world, normals),
                     faces.astype(np.uint32), foliage_instances(model, col, row, fields, top), top,
                     standable_cells(D, step_z_m=zr.step_m), fields)
