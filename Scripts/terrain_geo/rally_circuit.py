"""Circuitos de Rally por vueltas (#622): trazado cerrado con semilla, elementos colocados por reglas y terreno
volumétrico alrededor (campo de alturas que pasa por el marching cubes de terrain_vol, como E01B).

No confundir con terrain_shapes/rally_circuit.py (lazos de autor de I03R, I04 e I06 sobre el kit de formas).

    track = build_track(SEED)            # planta (rally_circuit_plan), perfil, peralte y velocidades
    model = RallyCircuitModel(track)     # campo de alturas con la calzada tallada y peraltada

Perfil del eje: cota de base (enlaces y rectas: subidas suaves que recuperan lo que bajan los saltos, con una
ondulación sorteada) más los elementos (rally_circuit_elements). Los saltos y los rasantes se dimensionan con la
velocidad de la línea ideal (rally_circuit_physics.speed_profile) en su labio o su cima, y como el perfil cambia la
velocidad, se repite DESIGN_PASSES veces.

Sección transversal: plataforma de ROAD_W_M + 2 x SHOULDER_M con el peralte (positivo = lado derecho más bajo),
berma llana a la cota del borde hasta BERM_M del eje (ahí van las barreras de #303, a 15-23 m) y talud de TALUD_DEG
hasta el terreno natural.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np
from scipy import ndimage
from scipy.spatial import cKDTree

from terrain_vol.density import smooth
from terrain_vol.layout import CELL_M, MAP_MIN_M, WATER_M, ZRange

from .heightfield import HeightfieldModel
from .layout import RASTER_PX_M
from .rally_circuit_elements import (CrestDesign, JumpDesign, JumpParams, bank_profile, design_crest,
                                     design_jump)
from .rally_circuit_physics import boost_arrival, speed_profile
from .rally_circuit_plan import JUMP_APPROACH_M, JUMP_RESERVE_M, Plan, make_plan
from .rally_spain import value_noise

NAME = "R01_circuito_dunas"
SEED = 622
LAPS = 3
ROAD_W_M = 14.0
SHOULDER_M = 3.0
PLATFORM_M = ROAD_W_M / 2.0 + SHOULDER_M
BERM_M = 26.0
TALUD_DEG = 33.0
MARGIN_M = 110.0
ROAD_MIN_M = WATER_M + 7.0
GRID_ZONE_M = (-60.0, 25.0)        # parrilla (detrás de la línea) y arrancada: llano y sin peralte
MAX_BASE_GRADE_DEG = 5.0
UNDULATION_DEG = 2.0
DESIGN_PASSES = 4
CREST_HEIGHT_M = (2.0, 3.5)
DESCRIPTION = ("Circuito de Rally por vueltas generado (#622): lazo cerrado entre dunas con tres saltos con "
               "recepción, curvas peraltadas, horquillas en zigzag, chicane y cambios de rasante; parrilla 2 x 4.")


@dataclass
class PlacedJump:
    piece: int
    s0: float                       # pie de la rampa (m desde la línea de salida)
    design: JumpDesign


@dataclass
class PlacedCrest:
    piece: int
    s0: float
    design: CrestDesign


@dataclass
class Track:
    plan: Plan
    z: np.ndarray
    bank_deg: np.ndarray
    speed: np.ndarray
    speed_boost: np.ndarray
    airborne: np.ndarray
    jumps: list[PlacedJump] = field(default_factory=list)
    crests: list[PlacedCrest] = field(default_factory=list)
    base_free: np.ndarray | None = None
    inward_tan: np.ndarray | None = None
    grade_sin: np.ndarray | None = None

    @property
    def arc(self) -> np.ndarray:
        return self.plan.arc

    @property
    def total(self) -> float:
        return self.plan.length_m

    def index(self, s: float) -> int:
        return int(round(s / self.plan.step_m)) % len(self.plan.pts)

    def boost_at(self, k: int) -> float:
        """Velocidad en la muestra k con una barra de turbo pisada justo antes (rally_circuit_physics.boost_arrival)."""
        return boost_arrival(self.speed, self.plan.curvature, self.inward_tan, self.grade_sin, k, self.plan.step_m)


def cyclic_mask(arc: np.ndarray, total: float, s0: float, s1: float) -> np.ndarray:
    """Muestras con s en [s0, s1] (con la vuelta si s0 > s1)."""
    s0, s1 = s0 % total, s1 % total
    return (arc >= s0) & (arc <= s1) if s0 <= s1 else (arc >= s0) | (arc <= s1)


def _grade_sin(z: np.ndarray, step: float) -> np.ndarray:
    dz = (np.roll(z, -1) - np.roll(z, 1)) / (2.0 * step)
    return dz / np.sqrt(1.0 + dz * dz)


def _bank_curves(plan: Plan) -> list[tuple[float, float, float]]:
    out = []
    for piece, s0, s1 in plan.spans:
        p = plan.pieces[piece]
        if p.kind in ("curva_peraltada", "horquilla") and p.bank_deg > 0.0:
            out.append((s0, s1, math.copysign(p.bank_deg, p.angle_deg)))
    return out


def _elements_rel(track: Track) -> np.ndarray:
    """Suma de los perfiles de saltos y rasantes (cota relativa; tras un salto queda su -drop_m)."""
    arc, rel = track.arc, np.zeros(len(track.arc))
    for j in track.jumps:
        rel += np.where(arc >= j.s0, j.design.profile(arc - j.s0), 0.0)
    for c in track.crests:
        rel += np.where(arc >= c.s0, c.design.profile(arc - c.s0), 0.0)
    return rel


def _base_free(track: Track) -> np.ndarray:
    """Peso (0..1) de la base: 0 en la parrilla, los elementos y las curvas peraltadas; suavizado."""
    arc, total = track.arc, track.total
    w = np.ones(len(arc))
    w[cyclic_mask(arc, total, *GRID_ZONE_M)] = 0.0
    for j in track.jumps:
        w[cyclic_mask(arc, total, j.s0 - 15.0, j.s0 + j.design.length_m + 15.0)] = 0.0
    for c in track.crests:
        w[cyclic_mask(arc, total, c.s0 - 5.0, c.s0 + c.design.length_m + 5.0)] = 0.0
    w[np.abs(track.bank_deg) > 0.5] = 0.0
    return np.clip(ndimage.gaussian_filter1d(w, 6.0 / track.plan.step_m, mode="wrap"), 0.0, 1.0) * (w > 0)


def _base(track: Track, rng_wave: tuple, drops: float) -> np.ndarray:
    """Cota de base: pendiente k (recupera las bajadas de los saltos) más una ondulación de media nula, solo donde
    la base es libre; cierra el lazo."""
    arc, step = track.arc, track.plan.step_m
    w = _base_free(track)
    track.base_free = w
    (l1, p1), (l2, p2) = rng_wave
    wave = math.tan(math.radians(UNDULATION_DEG)) * (0.6 * np.sin(2 * math.pi * arc / l1 + p1)
                                                     + 0.4 * np.sin(2 * math.pi * arc / l2 + p2))
    wave -= (wave * w).sum() / max(w.sum(), 1e-9)
    k = drops / max(w.sum() * step, 1e-9)
    slope = (k + wave) * w
    return np.concatenate([[0.0], np.cumsum(slope * step)[:-1]])


def _speeds(track: Track) -> None:
    plan = track.plan
    inward = np.tan(np.radians(track.bank_deg)) * np.sign(plan.curvature)
    grade = _grade_sin(track.z, plan.step_m)
    track.inward_tan, track.grade_sin = inward, grade
    track.speed = speed_profile(plan.curvature, inward, grade, track.airborne, plan.step_m)
    track.speed_boost = speed_profile(plan.curvature, inward, grade, track.airborne, plan.step_m, boost=True)


def _airborne(track: Track) -> np.ndarray:
    air = np.zeros(len(track.arc), dtype=bool)
    for j in track.jumps:
        land = j.design.fly(j.design.v_design)
        if land is not None:
            lip = j.s0 + j.design.lip_x
            air |= cyclic_mask(track.arc, track.total, lip + 0.5, lip + land.x_land_m)
    return air


def build_track(seed: int = SEED) -> Track:
    plan = make_plan(seed)
    rng = np.random.default_rng([seed, 622])
    arc, n = plan.arc, len(plan.pts)
    bank = bank_profile(arc, plan.length_m, _bank_curves(plan))
    track = Track(plan, np.zeros(n), bank, np.zeros(n), np.zeros(n), np.zeros(n, dtype=bool))
    jump_params, crest_heights = {}, {}
    for piece, s0, _ in plan.spans:
        kind = plan.pieces[piece].kind
        if kind == "salto":
            jump_params[piece] = (s0 + JUMP_APPROACH_M, JumpParams.draw(rng))
        elif kind == "rasante":
            crest_heights[piece] = (s0, rng.uniform(*CREST_HEIGHT_M))
    waves = ((rng.uniform(200.0, 350.0), rng.uniform(0, 2 * math.pi)), (rng.uniform(90.0, 150.0), rng.uniform(0, 2 * math.pi)))
    _speeds(track)
    lip_x = {p: 20.0 for p in jump_params}
    for _ in range(DESIGN_PASSES):
        track.jumps = []
        for p, (s, prm) in jump_params.items():
            k = track.index(s + lip_x[p])
            track.jumps.append(PlacedJump(p, s, design_jump(prm, float(track.speed[k]), track.boost_at(k),
                                                            JUMP_RESERVE_M)))
            lip_x[p] = track.jumps[-1].design.lip_x
        track.crests = []
        for p, (s, h) in crest_heights.items():
            top = track.index(s + plan.pieces[p].length_m / 2.0)
            track.crests.append(PlacedCrest(p, s, design_crest(plan.pieces[p].length_m, h, float(track.speed_boost[top]))))
        drops = sum(j.design.drop_m for j in track.jumps)
        z = _base(track, waves, drops) + _elements_rel(track)
        track.z = z + (ROAD_MIN_M - z.min())
        track.airborne = _airborne(track)
        _speeds(track)
    return track


# ── Terreno ──────────────────────────────────────────────────────────────────────
@dataclass(frozen=True)
class Frame:
    shift: np.ndarray               # m que se suman al eje del plan para llevarlo al mundo de la variante
    rows: int
    cols: int

    @property
    def grid(self) -> int:
        return max(self.rows, self.cols)

    def axis(self, count: int) -> np.ndarray:
        return MAP_MIN_M + (np.arange(int(round(count * CELL_M / RASTER_PX_M))) + 0.5) * RASTER_PX_M


def make_frame(track: Track) -> Frame:
    lo, hi = track.plan.pts.min(axis=0), track.plan.pts.max(axis=0)
    rows, cols = (int(math.ceil((hi[i] - lo[i] + 2.0 * MARGIN_M) / CELL_M)) for i in (0, 1))
    extra = np.array([rows * CELL_M, cols * CELL_M]) - (hi - lo)
    shift = MAP_MIN_M + extra / 2.0 - lo
    return Frame(np.round(shift, 3), rows, cols)


class RallyCircuitModel(HeightfieldModel):
    """Campo de alturas de la variante (rows x cols trozos) con la calzada tallada y peraltada."""
    trail_color = (0.42, 0.30, 0.17)
    trail_strength = 0.55

    def __init__(self, track: Track, seed: int = SEED):
        self.track, self.frame = track, make_frame(track)
        self.road = track.plan.pts + self.frame.shift
        X = self.frame.axis(self.frame.rows)[:, None]
        Y = self.frame.axis(self.frame.cols)[None, :]
        Xg, Yg = np.broadcast_arrays(X, Y)
        near = self._nearest(Xg, Yg)
        natural = self._natural(Xg, Yg, near, np.random.default_rng([seed, 7]))
        height, self.trail = self._carve(natural, near)
        self.z_range = ZRange.covering(float(height.max()))
        super().__init__(height)

    def _nearest(self, X: np.ndarray, Y: np.ndarray) -> dict:
        """Por píxel: distancia al eje, distancia lateral con signo (positiva a la derecha), cota y peralte del eje
        interpolados en el pie de la perpendicular."""
        plan, track = self.track.plan, self.track
        pts = np.column_stack([X.ravel(), Y.ravel()])
        dist, k = cKDTree(self.road).query(pts)
        tangent = np.column_stack([np.cos(plan.psi), np.sin(plan.psi)])
        rel = pts - self.road[k]
        along = np.einsum("ij,ij->i", rel, tangent[k])
        lateral = rel[:, 0] * -tangent[k, 1] + rel[:, 1] * tangent[k, 0]
        frac = (k + np.clip(along / plan.step_m, -0.5, 0.5)) % len(self.road)
        idx = np.arange(len(self.road) + 1)
        z = np.interp(frac, idx, np.append(track.z, track.z[0]))
        bank = np.interp(frac, idx, np.append(track.bank_deg, track.bank_deg[0]))
        shape = X.shape
        return {"dist": dist.reshape(shape), "lateral": lateral.reshape(shape), "z": z.reshape(shape),
                "bank": bank.reshape(shape)}

    def _natural(self, X, Y, near: dict, rng: np.random.Generator) -> np.ndarray:
        """Dunas alrededor: la cota de la calzada difuminada, más relieve que crece lejos del eje y un cordón de
        dunas en el borde de la rejilla (cierra la vista)."""
        regional = ndimage.gaussian_filter(near["z"], 40.0 / RASTER_PX_M, mode="nearest")
        extent = self.frame.grid * CELL_M
        hills = 0.5 + 0.5 * value_noise(rng, 110.0, extent, octaves=3)(X, Y)
        ripples = value_noise(rng, 22.0, extent, octaves=2)(X, Y)
        amp = 2.0 + 10.0 * smooth(35.0, 150.0, near["dist"])
        edge = np.minimum.reduce([X - MAP_MIN_M, Y - MAP_MIN_M, MAP_MIN_M + self.frame.rows * CELL_M - X,
                                  MAP_MIN_M + self.frame.cols * CELL_M - Y])
        rim = 14.0 * smooth(0.7 * MARGIN_M, 0.0, edge)
        return np.maximum(regional - 1.0 + amp * hills + 0.4 * ripples + rim, WATER_M + 1.5)

    def _carve(self, natural: np.ndarray, near: dict) -> tuple[np.ndarray, np.ndarray]:
        d, lat = near["dist"], near["lateral"]
        road_like = near["z"] - np.clip(lat, -PLATFORM_M, PLATFORM_M) * np.tan(np.radians(near["bank"]))
        reach = np.clip(d - BERM_M, 0.0, None) * math.tan(math.radians(TALUD_DEG))
        height = np.where(d <= BERM_M, road_like, np.clip(natural, road_like - reach, road_like + reach))
        trail = 1.0 - smooth(ROAD_W_M / 2.0 - 1.0, ROAD_W_M / 2.0 + 0.5, np.abs(lat) * (d <= BERM_M) + 99.0 * (d > BERM_M))
        return height, trail

    def trail_mask(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        return np.clip(self._sample(self.trail, x, y), 0.0, 1.0)

    def cells(self) -> list[tuple[int, int]]:
        return [(col, row) for row in range(self.frame.rows) for col in range(self.frame.cols)]

    def cell_gap(self, col: int, row: int) -> float:
        """Distancia (m) del eje al trozo (0 si lo cruza)."""
        half = CELL_M / 2.0
        return float(np.hypot(np.clip(np.abs(self.road[:, 0] - row * CELL_M) - half, 0.0, None),
                              np.clip(np.abs(self.road[:, 1] - col * CELL_M) - half, 0.0, None)).min())
