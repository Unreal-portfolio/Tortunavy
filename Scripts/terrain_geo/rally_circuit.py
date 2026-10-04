"""Circuitos de Rally por vueltas (#622): trazado cerrado con semilla, elementos colocados por reglas y terreno
volumétrico alrededor (campo de alturas que pasa por el marching cubes de terrain_vol, como E01B).

No confundir con terrain_shapes/rally_circuit.py (lazos de autor de I03R, I04 e I06 sobre el kit de formas).

    track = build_track(SEED)            # planta (rally_circuit_plan), perfil, peralte y velocidades
    model = RallyCircuitModel(track)     # campo de alturas con la calzada tallada y peraltada

Perfil del eje: cota de base (enlaces y rectas: subidas suaves que recuperan lo que bajan los saltos, con una
ondulación sorteada) más los elementos (rally_circuit_elements). Los saltos y los rasantes se dimensionan con la
velocidad de la línea ideal (rally_circuit_physics.speed_profile) en su labio o su cima, y como el perfil cambia la
velocidad, se repite DESIGN_PASSES veces.

Sección transversal: plataforma del ancho del tramo (rally_circuit_width: de 10 a 20 m, ROAD_W_M de referencia) más
2 x SHOULDER_M con el peralte (positivo = lado derecho más bajo), berma llana a la cota del borde hasta BERM_M del eje
(ahí van las barreras de #303, pegadas al borde de la calzada) y talud de TALUD_DEG hasta el terreno natural.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np
from scipy import ndimage
from scipy.spatial import cKDTree

from terrain_vol.density import smooth
from terrain_vol.layout import CELL_M, MAP_MIN_M, WATER_M, ZRange

from .heightfield import ZONES, HeightfieldModel
from .layout import RASTER_PX_M
from .rally_circuit_dirt import (BERM_END_M, BUMP_LEAD_M, DIP_DEPTH_M, DIP_LEAD_M, DIP_PIECE_M, MUD_COLOR, MUD_STRENGTH,
                                 BumpDesign, DipDesign, berm_lift, design_dip)
from .rally_circuit_elements import (BANK_RAMP_M, CrestDesign, JumpDesign, JumpParams, bank_profile, design_crest,
                                     design_jump)
from .rally_circuit_jumps import SHAPED_KINDS, ShapedJump, ShapedParams, design_shaped
from .rally_circuit_physics import boost_arrival, speed_profile
from .rally_circuit_plan import JUMP_APPROACH_M, JUMP_RESERVE_M, Plan, make_plan
from .rally_circuit_themes import Theme, theme as get_theme
from .rally_circuit_width import width_profile
from .rally_spain import value_noise

NAME = "R01_circuito_dunas"
SEED = 622
TIERRA_NAME = "R02_circuito_tierra"
TIERRA_SEED = 682
TIERRA_DESCRIPTION = ("Circuito de Rally por vueltas de tierra generado (#682): lazo cerrado con cuatro saltos (doble, "
                      "cresta, mesa y salto largo sobre hueco), whoops y tabla de lavar en recta, badén con barro, "
                      "banqueta de tierra en las horquillas, curvas peraltadas, chicane y cambios de rasante.")
LAPS = 3
ROAD_W_M = 14.0                    # ancho de referencia (el de los tramos normales); el real va por tramos
SHOULDER_M = 3.0
BERM_M = 26.0
TALUD_DEG = 33.0
MARGIN_M = 110.0
BUMP_UNSMOOTHED_MARGIN_M = 4.0      # calzada sin suavizar antes y después de cada tren de baches (#696)
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
    design: JumpDesign | ShapedJump


@dataclass
class PlacedBump:
    piece: int
    s0: float                       # principio de la recta de baches
    design: BumpDesign


@dataclass
class PlacedDip:
    piece: int
    s0: float
    design: DipDesign


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
    bumps: list[PlacedBump] = field(default_factory=list)
    dips: list[PlacedDip] = field(default_factory=list)
    profile: str = "dunas"
    base_free: np.ndarray | None = None
    inward_tan: np.ndarray | None = None
    grade_sin: np.ndarray | None = None
    width_m: np.ndarray | None = None   # ancho de la calzada por muestra (rally_circuit_width)

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
    for b in track.bumps:
        rel += np.where(arc >= b.s0, b.design.profile(arc - b.s0), 0.0)
    for d in track.dips:
        rel += np.where(arc >= d.s0, d.design.profile(arc - d.s0), 0.0)
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
    for b in track.bumps:                       # baches y badén sobre base llana: solo su propio perfil
        w[cyclic_mask(arc, total, b.s0 - 5.0, b.s0 + b.design.piece_m + 5.0)] = 0.0
    for d in track.dips:
        w[cyclic_mask(arc, total, d.s0 - 5.0, d.s0 + DIP_PIECE_M + 5.0)] = 0.0
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


def _design(prm: JumpParams | ShapedParams, v: float, v_boost: float) -> JumpDesign | ShapedJump:
    if isinstance(prm, ShapedParams):
        return design_shaped(prm, v, v_boost, JUMP_RESERVE_M)
    return design_jump(prm, v, v_boost, JUMP_RESERVE_M)


def build_track(seed: int = SEED, profile: str = "dunas") -> Track:
    """Perfil, peralte y velocidades del circuito. El perfil «tierra» (#682) añade los saltos con forma, los baches,
    el badén y la banqueta; sus sorteos van con su propio generador ([seed, 682]) y el de «dunas» no cambia."""
    plan = make_plan(seed, profile)
    rng = np.random.default_rng([seed, 622])
    rng_dirt = np.random.default_rng([seed, 682])
    arc, n = plan.arc, len(plan.pts)
    bank = bank_profile(arc, plan.length_m, _bank_curves(plan))
    track = Track(plan, np.zeros(n), bank, np.zeros(n), np.zeros(n), np.zeros(n, dtype=bool), profile=profile,
                  width_m=width_profile(plan, seed))
    jump_params, crest_heights, dip_depths = {}, {}, {}
    for piece, s0, _ in plan.spans:
        p = plan.pieces[piece]
        if p.kind == "salto":
            prm = ShapedParams.draw(p.variant, rng_dirt) if p.variant in SHAPED_KINDS else JumpParams.draw(rng)
            jump_params[piece] = (s0 + JUMP_APPROACH_M, prm)
        elif p.kind == "rasante":
            crest_heights[piece] = (s0, rng.uniform(*CREST_HEIGHT_M))
        elif p.kind == "baches":
            track.bumps.append(PlacedBump(piece, s0, BumpDesign(p.variant, p.amplitude_m, p.wavelength_m, p.count)))
        elif p.kind == "baden":
            dip_depths[piece] = (s0, float(rng_dirt.uniform(*DIP_DEPTH_M)))
    waves = ((rng.uniform(200.0, 350.0), rng.uniform(0, 2 * math.pi)), (rng.uniform(90.0, 150.0), rng.uniform(0, 2 * math.pi)))
    _speeds(track)
    lip_x = {p: 20.0 for p in jump_params}
    for _ in range(DESIGN_PASSES):
        track.jumps = []
        for p, (s, prm) in jump_params.items():
            k = track.index(s + lip_x[p])
            track.jumps.append(PlacedJump(p, s, _design(prm, float(track.speed[k]), track.boost_at(k))))
            lip_x[p] = track.jumps[-1].design.lip_x
        track.crests = []
        for p, (s, h) in crest_heights.items():
            top = track.index(s + plan.pieces[p].length_m / 2.0)
            track.crests.append(PlacedCrest(p, s, design_crest(plan.pieces[p].length_m, h, float(track.speed_boost[top]))))
        track.dips = [PlacedDip(p, s, design_dip(d, float(track.speed_boost[track.index(s + DIP_PIECE_M / 2.0)])))
                      for p, (s, d) in dip_depths.items()]
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
    """Campo de alturas de la variante (rows x cols trozos) con la calzada tallada y peraltada; el tema
    (rally_circuit_themes, #692) da el relieve de alrededor y los colores."""
    trail_color = (0.42, 0.30, 0.17)
    trail_strength = 0.55

    def __init__(self, track: Track, seed: int = SEED, theme: str = "base"):
        self.track, self.frame = track, make_frame(track)
        self.theme: Theme = get_theme(theme)
        self.trail_color, self.trail_strength = self.theme.trail_color, self.theme.trail_strength
        self.wall_strata = self.theme.wall_strata
        if self.theme.mud_strength is not None:
            self.mud_strength = self.theme.mud_strength
        self.road = track.plan.pts + self.frame.shift
        X = self.frame.axis(self.frame.rows)[:, None]
        Y = self.frame.axis(self.frame.cols)[None, :]
        Xg, Yg = np.broadcast_arrays(X, Y)
        near = self._nearest(Xg, Yg)
        natural = self._natural(Xg, Yg, near, np.random.default_rng([seed, 7]))
        height, self.trail = self._carve(natural, near)
        self.unsmoothed = near["bump"] * (np.abs(near["lateral"]) <= near["half"] + SHOULDER_M) * (near["dist"] <= BERM_M)
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
        berm, mud, half, bump = self.berm_dir(), self.mud_axis(), self.half_width(), self.bump_axis()
        shape = X.shape
        return {"dist": dist.reshape(shape), "lateral": lateral.reshape(shape), "z": z.reshape(shape),
                "bank": bank.reshape(shape), "berm": np.interp(frac, idx, np.append(berm, berm[0])).reshape(shape),
                "mud": np.interp(frac, idx, np.append(mud, mud[0])).reshape(shape),
                "bump": np.interp(frac, idx, np.append(bump, bump[0])).reshape(shape),
                "half": np.interp(frac, idx, np.append(half, half[0])).reshape(shape)}

    def half_width(self) -> np.ndarray:
        """Media calzada (m) por muestra del eje; ROAD_W_M / 2 si el trazado no trae ancho por tramo."""
        width = self.track.width_m
        return np.full(len(self.road), ROAD_W_M / 2.0) if width is None else np.asarray(width) / 2.0

    def berm_dir(self) -> np.ndarray:
        """Por muestra del eje: peso (0..1) de la banqueta con el signo del lado de fuera de la curva (+1 derecha),
        en las horquillas con banqueta y con rampas de BANK_RAMP_M a cada lado."""
        plan, arc, total = self.track.plan, self.track.arc, self.track.total
        out = np.zeros(len(arc))
        for piece, s0, s1 in plan.spans:
            p = plan.pieces[piece]
            if p.variant != "banqueta":
                continue
            inside = cyclic_mask(arc, total, s0, s1)
            d = np.minimum(np.abs((arc - s0 + total / 2.0) % total - total / 2.0),
                           np.abs((arc - s1 + total / 2.0) % total - total / 2.0))
            w = np.where(inside, 1.0, np.clip(1.0 - d / BANK_RAMP_M, 0.0, 1.0))
            out = np.where(w > np.abs(out), -math.copysign(1.0, p.angle_deg) * w, out)
        return out

    def mud_axis(self) -> np.ndarray:
        """Por muestra del eje: 1 en el badén (y 3 m a cada lado), 0 fuera."""
        arc, total = self.track.arc, self.track.total
        out = np.zeros(len(arc))
        for d in self.track.dips:
            out[cyclic_mask(arc, total, d.s0 + DIP_LEAD_M - 3.0, d.s0 + DIP_LEAD_M + d.design.length_m + 3.0)] = 1.0
        return out

    def bump_axis(self) -> np.ndarray:
        """Por muestra del eje: 1 en los trenes de baches (y BUMP_UNSMOOTHED_MARGIN_M a cada lado), 0 fuera."""
        arc, total = self.track.arc, self.track.total
        out = np.zeros(len(arc))
        for b in self.track.bumps:
            s0 = b.s0 + BUMP_LEAD_M - BUMP_UNSMOOTHED_MARGIN_M
            out[cyclic_mask(arc, total, s0, s0 + b.design.train_m + 2.0 * BUMP_UNSMOOTHED_MARGIN_M)] = 1.0
        return out

    def unsmoothed_mask(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        """Calzada de los trenes de baches (#696): terrain_vol.mesh no la suaviza, porque el Taubin aplana las ondas
        cortas (la tabla de lavar salía al 31-60 % de su amplitud). El marching cubes de un campo de alturas ya pone
        los vértices a la cota exacta del campo."""
        return self._sample(self.unsmoothed, np.asarray(x, dtype=np.float64), np.asarray(y, dtype=np.float64)) > 0.5

    def _natural(self, X, Y, near: dict, rng: np.random.Generator) -> np.ndarray:
        """Relieve alrededor (dunas en el tema base): la cota de la calzada difuminada, más relieve que crece lejos
        del eje y un cordón en el borde de la rejilla (cierra la vista)."""
        t = self.theme
        regional = ndimage.gaussian_filter(near["z"], 40.0 / RASTER_PX_M, mode="nearest")
        extent = self.frame.grid * CELL_M
        hills = 0.5 + 0.5 * value_noise(rng, t.hills_scale_m, extent, octaves=3)(X, Y)
        ripples = value_noise(rng, 22.0, extent, octaves=2)(X, Y)
        amp = t.near_m + t.hills_m * smooth(35.0, 150.0, near["dist"])
        edge = np.minimum.reduce([X - MAP_MIN_M, Y - MAP_MIN_M, MAP_MIN_M + self.frame.rows * CELL_M - X,
                                  MAP_MIN_M + self.frame.cols * CELL_M - Y])
        rim = t.rim_m * smooth(0.7 * MARGIN_M, 0.0, edge)
        return np.maximum(regional - 1.0 + amp * hills + t.ripples_m * ripples + rim, WATER_M + 1.5)

    def color_weights(self, x: np.ndarray, y: np.ndarray) -> dict[str, np.ndarray]:
        """Playa junto al agua y la paleta del tema en el resto; con el tema base, la de HeightfieldModel."""
        beach = smooth(WATER_M + self.theme.shore_m, WATER_M + 0.6, self.ground_height(x, y))
        zone = self.theme.color_zone
        rest = np.zeros(len(x)) if zone == "beach" else 1.0 - beach
        out = {z: np.zeros(len(x)) for z in ZONES}
        out["beach"] = beach if zone != "beach" else np.ones(len(x))
        if zone != "beach":
            out[zone] = rest
        return out

    def _carve(self, natural: np.ndarray, near: dict) -> tuple[np.ndarray, np.ndarray]:
        d, lat, half = near["dist"], near["lateral"], near["half"]
        platform = half + SHOULDER_M
        road_like = near["z"] - np.clip(lat, -platform, platform) * np.tan(np.radians(near["bank"]))
        berm = near["berm"]
        # La banqueta va por fuera del borde del tramo: se desplaza con lo que el tramo se aparta de los 14 m.
        shift = half - ROAD_W_M / 2.0
        road_like = road_like + np.abs(berm) * berm_lift(lat * np.sign(berm) - shift) * (d <= BERM_END_M + shift + 1.0)
        mud = near["mud"] * (1.0 - smooth(half, half + 2.0, np.abs(lat))) * (d <= BERM_M)
        self.mud = mud
        reach = np.clip(d - BERM_M, 0.0, None) * math.tan(math.radians(TALUD_DEG))
        height = np.where(d <= BERM_M, road_like, np.clip(natural, road_like - reach, road_like + reach))
        trail = 1.0 - smooth(half - 1.0, half + 0.5, np.abs(lat) * (d <= BERM_M) + 99.0 * (d > BERM_M))
        return height, trail

    def trail_mask(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        return np.clip(self._sample(self.trail, x, y), 0.0, 1.0)

    mud_color = MUD_COLOR
    mud_strength = MUD_STRENGTH

    def mud_mask(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        """Barro del badén (#682): terrain_vol.mesh oscurece ahí el color de los vértices."""
        return np.clip(self._sample(self.mud, x, y), 0.0, 1.0)

    def cells(self) -> list[tuple[int, int]]:
        return [(col, row) for row in range(self.frame.rows) for col in range(self.frame.cols)]

    def cell_gap(self, col: int, row: int) -> float:
        """Distancia (m) del eje al trozo (0 si lo cruza)."""
        half = CELL_M / 2.0
        return float(np.hypot(np.clip(np.abs(self.road[:, 0] - row * CELL_M) - half, 0.0, None),
                              np.clip(np.abs(self.road[:, 1] - col * CELL_M) - half, 0.0, None)).min())
