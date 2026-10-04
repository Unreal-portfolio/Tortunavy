"""Saltos de circuito de tierra (#682) para los circuitos de Rally por vueltas: doble, cresta y salto largo sobre hueco.
Se suman a la mesa de #622 (rally_circuit_elements.JumpDesign) con la misma interfaz (lip_x, height_m, length_m,
drop_m, landing_zone, profile, fly, as_dict), así que el trazado, el manifest y el validador los tratan igual.

Perfil (x en m desde el pie de la rampa, cotas relativas a él):

    rampa (transición EASE_M + recta a lip_deg) | labio | [hueco] | cara de recepción convexa | salida suave

  - doble: tras el labio, cara trasera de FACE_DEG, vaguada (a la cota del pie como mucho) y cara de subida de
    FACE_DEG hasta la cresta de la recepción, CREST_DROP_M["doble"] por debajo del labio. Es la rampa doble del
    motocross: kicker y montículo de aterrizaje;
  - hueco: lo mismo con la vaguada GAP_DEPTH_M por debajo del pie y más larga; va en recta rápida (salto largo);
  - cresta: sin hueco, la cima redondeada (ROUND_M) cae directamente a la cara de recepción (salto en cresta).

La cresta de la recepción está donde la trayectoria a CLEAR_FACTOR · v (más lenta que el piloto IA, que llega a
0,9 · v) pasa CLEARANCE_M por encima: a la velocidad de la IA también se salva el hueco. La cara de recepción es
convexa: su pendiente pasa de face_deg[0] en la cresta a face_deg[1] en face_ramp_m y sigue recta; se buscan esos
tres valores (FACE_SEARCH) para que el choque perpendicular al suelo sea mínimo a 0,9 · v y a v, con el aterrizaje
a v a no más de MAX_LAND_DROP_M por debajo del labio. La cara baja LAND_MARGIN_M más allá del aterrizaje a v y
termina en una salida suave (RUNOUT_EASE_M) a la cota final. El turbo vuela más y cae en la salida llana, como en la
mesa: el validador solo pide que lo haga dentro de la calzada y antes del final de la recta del salto.
"""

from __future__ import annotations

import itertools
import math
from dataclasses import dataclass, field

import numpy as np

from .rally_circuit_elements import EASE_M, MAX_DROP_M, RUNOUT_EASE_M, RUNOUT_MIN_M
from .rally_circuit_physics import G, flight

SHAPED_KINDS = ("doble", "cresta", "hueco")
DX_M = 0.05
FACE_DEG = 24.0                  # caras del hueco (se suben andando si se cae dentro)
ROUND_M = 1.5                    # redondeo del labio, de la vaguada y de la cresta
CLEAR_FACTOR = 0.8
AI_FACTOR = 0.9                  # TNRallyCircuit::FBrakeTuning::JumpLipSpeedFactor
CLEARANCE_M = 0.4
CREST_DROP_M = {"doble": 0.3, "hueco": 0.9, "cresta": 0.0}
GAP_DEPTH_M = 2.0                # vaguada del hueco bajo la cota del pie
MAX_LAND_DROP_M = 3.8
LAND_MARGIN_M = 6.0
IMPACT_TARGET_MS = 4.0
IMPACT_AI_TARGET_MS = 4.5
MIN_LIP_DEG = 6.0
FACE_SEARCH = (tuple(range(2, 14, 2)), tuple(range(8, 24, 2)), (6.0, 10.0, 15.0, 22.0, 30.0, 40.0))


@dataclass(frozen=True)
class ShapedParams:
    kind: str
    lip_deg: float
    height_m: float              # altura del labio sobre el pie

    @classmethod
    def draw(cls, kind: str, rng: np.random.Generator) -> ShapedParams:
        lip = {"doble": (10.0, 13.0), "cresta": (8.0, 11.0), "hueco": (10.0, 12.0)}[kind]
        height = {"doble": (2.0, 2.6), "cresta": (2.4, 3.2), "hueco": (2.2, 2.8)}[kind]
        return cls(kind, float(rng.uniform(*lip)), float(rng.uniform(*height)))


def _trajectory(v: float, lip_deg: float, x: np.ndarray) -> np.ndarray:
    th = math.radians(lip_deg)
    vx = v * math.cos(th)
    return x * math.tan(th) - G * x * x / (2.0 * vx * vx)


def _x_at(v: float, lip_deg: float, z: float) -> float:
    """Distancia del labio a la que la trayectoria a v baja a la cota z (relativa al labio)."""
    th = math.radians(lip_deg)
    vx, vz = v * math.cos(th), v * math.sin(th)
    return vx * (vz + math.sqrt(vz * vz + 2.0 * G * max(-z, -vz * vz / (2.0 * G)))) / G


def _slopes_to_z(slope_deg: np.ndarray) -> np.ndarray:
    return np.concatenate([[0.0], np.cumsum(np.tan(np.radians(slope_deg[:-1])) * DX_M)])


def _ramp(x: np.ndarray, x0: float, length: float, a0: float, a1: float) -> np.ndarray:
    u = np.clip((x - x0) / max(length, 1e-6), 0.0, 1.0)
    return a0 + (a1 - a0) * u


def _contact(v: float, lip_deg: float, x: np.ndarray, z: np.ndarray, start: float) -> tuple[float, float] | None:
    """(x de contacto, choque perpendicular) de la trayectoria a v sobre la cara (x, z) relativa al labio."""
    traj = _trajectory(v, lip_deg, x)
    hit = np.nonzero((x >= start) & (traj <= z))[0]
    if len(hit) == 0:
        return None
    i = max(int(hit[0]), 1)
    th = math.radians(lip_deg)
    vx = v * math.cos(th)
    vz = v * math.sin(th) - G * x[i] / vx
    ground = math.atan(-(z[i] - z[i - 1]) / (x[i] - x[i - 1]))
    return float(x[i]), float(math.hypot(vx, vz) * math.sin(math.atan2(-vz, vx) - ground))


def _face(xc: float, zc: float, face: tuple[float, float, float], until: float) -> tuple[np.ndarray, np.ndarray]:
    x = np.arange(xc, until, DX_M)
    slope = _ramp(x, xc, face[2], face[0], face[1])
    return x, zc - _slopes_to_z(slope)


def _best_face(lip_deg: float, v: float, xc: float, zc: float, height: float) -> tuple[tuple[float, float, float], float, float]:
    """Pendientes de la cara (inicio, final, largo de la transición) con el menor choque a 0,9 · v y a v. Devuelve
    también el x y la cota del aterrizaje a v (relativos al labio)."""
    best = None
    for a0, a1, ramp in itertools.product(*FACE_SEARCH):
        if a1 < a0:
            continue
        x, z = _face(xc, zc, (a0, a1, ramp), xc + 120.0)
        ai, design = _contact(AI_FACTOR * v, lip_deg, x, z, xc), _contact(v, lip_deg, x, z, xc)
        if ai is None or design is None:
            continue
        z_land = float(np.interp(design[0], x, z))
        if z_land < -MAX_LAND_DROP_M or -_floor(height, z_land, a1) > MAX_DROP_M:
            continue
        score = max(ai[1], design[1])
        if best is None or score < best[0]:
            best = (score, (float(a0), float(a1), float(ramp)), design[0], z_land)
    if best is None:
        raise ValueError(f"sin cara de recepción para lip {lip_deg:.1f} y v {v:.1f}")
    return best[1], best[2], best[3]


def _floor(height: float, land_z: float, face_end_deg: float) -> float:
    """Cota final relativa al pie: la cara sigue LAND_MARGIN_M tras el aterrizaje a v y la salida suave baja la
    mitad de RUNOUT_EASE_M a su pendiente."""
    return height + land_z - (LAND_MARGIN_M + RUNOUT_EASE_M / 2.0) * math.tan(math.radians(face_end_deg))


@dataclass(frozen=True)
class ShapedJump:
    kind: str
    lip_deg: float
    kicker_lin_m: float
    v_design: float
    v_boost: float
    crest_x_m: float             # cresta de la recepción, desde el labio (0 en la cresta)
    crest_z_m: float             # cota de la cresta relativa al labio
    trough_z_m: float            # fondo de la vaguada relativo al pie (0 sin hueco)
    face_deg: tuple[float, float, float]
    land_x_m: float              # aterrizaje a v_design, desde el labio
    floor_z_m: float             # cota final relativa al pie (-drop)
    face_end_x: float            # fin de la cara de recepción (desde el pie), donde empieza la salida suave
    _xs: np.ndarray = field(repr=False, compare=False, default=None)
    _zs: np.ndarray = field(repr=False, compare=False, default=None)

    @property
    def lip_x(self) -> float:
        return EASE_M + self.kicker_lin_m

    @property
    def height_m(self) -> float:
        return math.tan(math.radians(self.lip_deg)) * (EASE_M / 2.0 + self.kicker_lin_m)

    @property
    def table_m(self) -> float:
        """Largo del hueco (del labio a la cresta de la recepción); 0 en la cresta."""
        return self.crest_x_m

    @property
    def landing_deg(self) -> float:
        return self.face_deg[1]

    @property
    def length_m(self) -> float:
        return float(self._xs[-1])

    @property
    def drop_m(self) -> float:
        return -self.floor_z_m

    @property
    def landing_zone(self) -> tuple[float, float]:
        """Zona de aterrizaje (x desde el pie): de la cresta de la recepción al final de su cara."""
        start = self.lip_x + max(self.crest_x_m, 1.0)
        return start, max(self.face_end_x, start + 1.0)

    def profile(self, x: np.ndarray) -> np.ndarray:
        x = np.asarray(x, dtype=np.float64)
        return np.where(x <= 0.0, 0.0, np.interp(x, self._xs, self._zs, right=self.floor_z_m))

    def fly(self, v: float, launch_deg: float | None = None):
        x = np.arange(0.0, self.length_m + 40.0, 0.1)
        z = self.profile(self.lip_x + x) - self.height_m
        return flight(x, z, v, self.lip_deg if launch_deg is None else launch_deg)

    def as_dict(self) -> dict:
        return {"kind": self.kind, "lip_deg": round(self.lip_deg, 3), "kicker_lin_m": round(self.kicker_lin_m, 3),
                "v_design": round(self.v_design, 3), "v_boost": round(self.v_boost, 3),
                "gap_m": round(self.crest_x_m, 2), "crest_z_m": round(self.crest_z_m, 3),
                "trough_z_m": round(self.trough_z_m, 3), "face_deg": [round(v, 2) for v in self.face_deg],
                "land_x_m": round(self.land_x_m, 2), "height_m": round(self.height_m, 3),
                "drop_m": round(self.drop_m, 3), "length_m": round(self.length_m, 2), "lip_x_m": round(self.lip_x, 2),
                "landing_zone_x_m": [round(v, 2) for v in self.landing_zone]}


def _gap_slopes(kind: str, height: float, xc: float, zc_abs: float) -> tuple[np.ndarray, float]:
    """Pendientes (grados, cada DX_M desde el labio hasta la cresta de la recepción) del hueco: cara trasera,
    vaguada y cara de subida de FACE_DEG, con esquinas vivas (las redondea _round). Devuelve también la cota del
    fondo relativa al pie: en el hueco, GAP_DEPTH_M por debajo; en la doble, la del pie; y si no cabe, la vaguada
    en V que cabe entre las dos caras."""
    t = math.tan(math.radians(FACE_DEG))
    target = -GAP_DEPTH_M if kind == "hueco" else 0.0
    bottom = max(target, (height + zc_abs - t * (xc - 2.0 * ROUND_M)) / 2.0)
    down, up = (height - bottom) / t, (zc_abs - bottom) / t
    flat = max(0.0, xc - down - up)
    x = np.arange(0.0, xc, DX_M)
    return np.where(x < down, -FACE_DEG, np.where(x < down + flat, 0.0, FACE_DEG)), bottom


def _round(slopes: np.ndarray, start: int) -> np.ndarray:
    """Redondea las esquinas de la pendiente a partir de `start` (media móvil de ROUND_M): labio, vaguada y cresta."""
    from scipy import ndimage
    out = slopes.copy()
    out[start:] = ndimage.uniform_filter1d(slopes, int(round(ROUND_M / DX_M)), mode="nearest")[start:]
    return out


def build_shaped(kind: str, lip_deg: float, kicker_lin_m: float, v: float, v_boost: float) -> ShapedJump:
    tk = math.tan(math.radians(lip_deg))
    lip_x = EASE_M + kicker_lin_m
    height = tk * (EASE_M / 2.0 + kicker_lin_m)
    crest_rel = -CREST_DROP_M[kind]
    xc = 0.5 if kind == "cresta" else _x_at(CLEAR_FACTOR * v, lip_deg, crest_rel + CLEARANCE_M)
    face, land_x, land_z = _best_face(lip_deg, v, xc, crest_rel, height)
    floor_abs = height + land_z - LAND_MARGIN_M * math.tan(math.radians(face[1]))
    x = np.arange(0.0, lip_x, DX_M)
    kicker = np.where(x <= EASE_M, np.degrees(np.arctan(tk * x / EASE_M)), lip_deg)
    if kind == "cresta":
        gap, trough = np.full(int(round(xc / DX_M)), -face[0]), 0.0
    else:
        gap, trough = _gap_slopes(kind, height, xc, height + crest_rel)
    xf = np.arange(0.0, 200.0, DX_M)
    slopes = np.concatenate([kicker, gap, -_ramp(xf, 0.0, face[2], face[0], face[1])])
    zs = _slopes_to_z(slopes)
    # La cara baja hasta la cota final (sin pasar del aterrizaje a v con su margen) y sale suave al llano.
    end = max(int(np.argmax(zs <= floor_abs)) if (zs <= floor_abs).any() else len(zs) - 1,
              int(round((lip_x + land_x + LAND_MARGIN_M) / DX_M)))
    ease = np.linspace(slopes[end], 0.0, int(round(RUNOUT_EASE_M / DX_M)))
    slopes = _round(np.concatenate([slopes[:end], ease]), len(kicker) - int(round(ROUND_M / DX_M)))
    zs = _slopes_to_z(slopes)
    xs = DX_M * np.arange(len(slopes))
    return ShapedJump(kind, lip_deg, kicker_lin_m, v, v_boost, xc, crest_rel, trough, face, land_x, float(zs[-1]),
                      end * DX_M, xs, zs)


def _impacts(jump: ShapedJump) -> tuple[float, float]:
    """Choque perpendicular (m/s) a v_design y a la velocidad del piloto IA, sobre el perfil del salto."""
    from .rally_circuit_elements import impact_ms
    return impact_ms(jump, jump.v_design), impact_ms(jump, AI_FACTOR * jump.v_design)


def design_shaped(params: ShapedParams, v: float, v_boost: float, max_length_m: float) -> ShapedJump:
    """Salto de forma (doble, cresta u hueco) para llegar a v m/s. Baja el ángulo del labio de grado en grado si no
    cabe en max_length_m, si la recepción baja demasiado o si el choque pasa de IMPACT_TARGET_MS (a v) o de
    IMPACT_AI_TARGET_MS (a la velocidad del piloto IA); si ninguno lo cumple, el de menor choque que cabe."""
    lip, best = params.lip_deg, None
    while lip >= MIN_LIP_DEG:
        tk = math.tan(math.radians(lip))
        kicker = float(np.clip(params.height_m / tk - EASE_M / 2.0, 4.0, 40.0))
        try:
            jump = build_shaped(params.kind, lip, kicker, v, v_boost)
        except ValueError:
            jump = None
        if jump is not None and jump.length_m + RUNOUT_MIN_M <= max_length_m and jump.drop_m <= MAX_DROP_M + 1e-6:
            hit, hit_ai = _impacts(jump)
            if hit <= IMPACT_TARGET_MS and hit_ai <= IMPACT_AI_TARGET_MS:
                return jump
            if best is None or max(hit, hit_ai) < best[0]:
                best = (max(hit, hit_ai), jump)
        lip -= 1.0
    if best is None:
        raise ValueError(f"salto {params.kind} sin diseño para v {v:.1f} m/s")
    return best[1]
