"""Elementos verticales y peralte de los circuitos de Rally por vueltas (#622): saltos con recepción, cambios de
rasante y curvas peraltadas, dimensionados con la física del buggy (rally_circuit_physics).

Salto (JumpDesign), en metros desde el pie de la rampa y cotas relativas a él:

    rampa (transición EASE_M + tramo recto a lip_deg) | labio | mesa | rodilla | recepción recta | salida suave

  - lip_deg sale de la velocidad de llegada v (perfil de la línea ideal): la altura del vuelo sobre el labio es
    apex_m (sorteado), sen(lip) = sqrt(2 g apex) / v, entre LIP_DEG;
  - es una mesa (tabletop): el vuelo a v cae en `aim` (78-90 %) del largo de la mesa, que es llana; más lento cae
    antes en la mesa y más rápido (turbo) pasa la rodilla y baja por la recepción. Apuntar a la rodilla no es
    robusto: con una trayectoria tan tendida, unos centímetros de redondeo en la malla mueven el contacto 15 m;
  - la recepción baja hasta la cota del pie de la rampa y se alarga hasta que el vuelo con turbo también toca en
    ella, sin bajar más de MAX_DROP_M;
  - la zona de aterrizaje es la segunda mitad de la mesa más la rodilla y la recepción recta.

Rasante (CrestDesign): joroba de coseno de length_m y height_m con el radio vertical en la cima por encima de
CREST_SAFETY * v_boost² / g: ni con turbo se despega en él.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

from .rally_circuit_physics import G, flight

EASE_M = 10.0                    # transición de la rampa (de llano a lip_deg)
KNUCKLE_M = 4.0                  # rodilla: de la mesa a la recepción
RUNOUT_EASE_M = 14.0             # de la recepción al llano
LIP_DEG = (5.0, 14.0)
LANDING_MARGIN_M = 4.0
MAX_DROP_M = 5.0
RUNOUT_MIN_M = 20.0              # llano tras el salto antes de acabar su recta
CREST_SAFETY = 1.15
BANK_RAMP_M = 14.0
MAX_BANK_DEG = 15.0


@dataclass(frozen=True)
class JumpParams:
    apex_m: float                # altura del vuelo sobre el labio a la velocidad de llegada
    height_m: float              # altura del labio (y de la mesa) sobre el pie de la rampa
    landing_deg: float           # pendiente de la recepción
    aim: float                   # fracción de la mesa en la que toca el vuelo a la velocidad de llegada

    @classmethod
    def draw(cls, rng: np.random.Generator) -> JumpParams:
        return cls(rng.uniform(0.6, 1.0), rng.uniform(2.6, 3.6), rng.uniform(14.0, 18.0), rng.uniform(0.78, 0.9))


@dataclass(frozen=True)
class JumpDesign:
    lip_deg: float
    kicker_lin_m: float
    table_m: float
    landing_deg: float
    landing_lin_m: float
    v_design: float
    v_boost: float

    @property
    def lip_x(self) -> float:
        return EASE_M + self.kicker_lin_m

    @property
    def height_m(self) -> float:
        return math.tan(math.radians(self.lip_deg)) * (EASE_M / 2.0 + self.kicker_lin_m)

    @property
    def knuckle_x(self) -> float:
        return self.lip_x + self.table_m

    @property
    def landing_end_x(self) -> float:
        return self.knuckle_x + KNUCKLE_M + self.landing_lin_m

    @property
    def length_m(self) -> float:
        return self.landing_end_x + RUNOUT_EASE_M

    @property
    def drop_m(self) -> float:
        """Cuánto más baja queda la calzada tras el salto que antes de la rampa."""
        tl = math.tan(math.radians(self.landing_deg))
        return tl * (KNUCKLE_M / 2.0 + self.landing_lin_m + RUNOUT_EASE_M / 2.0) - self.height_m

    @property
    def landing_zone(self) -> tuple[float, float]:
        """Zona de aterrizaje (x desde el pie de la rampa): segunda mitad de la mesa, rodilla y recepción recta."""
        return self.lip_x + 0.5 * self.table_m, self.landing_end_x

    def profile(self, x: np.ndarray) -> np.ndarray:
        """Cota (m) relativa al pie de la rampa en x m; antes del pie, 0, y tras la salida, -drop_m."""
        x = np.asarray(x, dtype=np.float64)
        tk, tl = math.tan(math.radians(self.lip_deg)), math.tan(math.radians(self.landing_deg))
        h = self.height_m
        z = np.where(x <= EASE_M, tk * np.clip(x, 0.0, None) ** 2 / (2.0 * EASE_M),
                     tk * EASE_M / 2.0 + tk * (x - EASE_M))
        z = np.where(x >= self.lip_x, h, z)
        u = x - self.knuckle_x
        z = np.where(u > 0.0, h - tl * np.clip(u, 0.0, KNUCKLE_M) ** 2 / (2.0 * KNUCKLE_M), z)
        u = x - self.knuckle_x - KNUCKLE_M
        z = np.where(u > 0.0, h - tl * KNUCKLE_M / 2.0 - tl * np.clip(u, 0.0, self.landing_lin_m), z)
        u = x - self.landing_end_x
        z_l = h - tl * (KNUCKLE_M / 2.0 + self.landing_lin_m)
        ue = np.clip(u, 0.0, RUNOUT_EASE_M)
        return np.where(u > 0.0, z_l - tl * (ue - ue * ue / (2.0 * RUNOUT_EASE_M)), z)

    def fly(self, v: float, launch_deg: float | None = None):
        """Vuelo desde el labio sobre el propio perfil (x_land_m, desde el labio)."""
        x = np.arange(0.0, self.length_m + 40.0, 0.1)
        z = self.profile(self.lip_x + x) - self.height_m
        return flight(x, z, v, self.lip_deg if launch_deg is None else launch_deg)

    def as_dict(self) -> dict:
        d = {k: round(float(v), 3) for k, v in self.__dict__.items()}
        return {**d, "height_m": round(self.height_m, 3), "drop_m": round(self.drop_m, 3),
                "length_m": round(self.length_m, 2), "lip_x_m": round(self.lip_x, 2),
                "landing_zone_x_m": [round(v, 2) for v in self.landing_zone]}


def impact_ms(design: JumpDesign, v: float) -> float:
    """Velocidad de choque contra el suelo (m/s, perpendicular a él) al aterrizar a v; inf si no toca."""
    land = design.fly(v)
    if land is None:
        return math.inf
    th = math.radians(design.lip_deg)
    vx, vz = v * math.cos(th), v * math.sin(th) - G * land.airtime_s
    ground = math.radians(-land.ground_deg)
    normal = np.array([-math.sin(ground), math.cos(ground)])
    return float(max(0.0, -(vx * normal[0] + vz * normal[1])))


def _sized(params: JumpParams, apex: float, v: float, v_boost: float) -> JumpDesign:
    """Labio para apex a v; rampa recta hasta height_m; mesa con la que el vuelo a v (que en llano mide
    v² sen(2 lip) / g) cae en `aim` de su largo; recepción hasta la cota del pie (sin bajada neta), alargada si el
    vuelo con turbo la pasa, como mucho hasta MAX_DROP_M de bajada."""
    lip = math.degrees(math.asin(min(1.0, math.sqrt(2.0 * G * apex) / max(v, 1.0))))
    lip = float(np.clip(lip, *LIP_DEG))
    tk, tl = math.tan(math.radians(lip)), math.tan(math.radians(params.landing_deg))
    kicker = float(np.clip(params.height_m / tk - EASE_M / 2.0, 4.0, 40.0))
    height = tk * (EASE_M / 2.0 + kicker)
    table = max(4.0, v * v * math.sin(math.radians(2.0 * lip)) / G / params.aim)
    landing = max(4.0, height / tl - KNUCKLE_M / 2.0 - RUNOUT_EASE_M / 2.0)
    longest = landing + MAX_DROP_M / tl
    design = JumpDesign(lip, kicker, table, params.landing_deg, landing, v, v_boost)
    while landing < longest:
        boost = design.fly(v_boost)
        if boost is not None and boost.x_land_m <= design.landing_end_x - design.lip_x - 1.0:
            break
        landing = min(longest, landing + 2.0)
        design = JumpDesign(lip, kicker, table, params.landing_deg, landing, v, v_boost)
    return design


def design_jump(params: JumpParams, v: float, v_boost: float, max_length_m: float) -> JumpDesign:
    """Salto para llegar a v m/s (v_boost con turbo): baja la altura del vuelo hasta que cabe en max_length_m."""
    apex = params.apex_m
    while True:
        design = _sized(params, apex, v, v_boost)
        if design.length_m + RUNOUT_MIN_M <= max_length_m or apex < 0.2:
            return design
        apex *= 0.85


@dataclass(frozen=True)
class CrestDesign:
    length_m: float
    height_m: float
    v_boost: float

    @property
    def radius_m(self) -> float:
        """Radio vertical en la cima del coseno: L² / (2 pi² h)."""
        return self.length_m ** 2 / (2.0 * math.pi ** 2 * max(self.height_m, 1e-6))

    def profile(self, x: np.ndarray) -> np.ndarray:
        x = np.clip(np.asarray(x, dtype=np.float64), 0.0, self.length_m)
        return 0.5 * self.height_m * (1.0 - np.cos(2.0 * math.pi * x / self.length_m))

    def as_dict(self) -> dict:
        return {"length_m": round(self.length_m, 2), "height_m": round(self.height_m, 3),
                "radius_m": round(self.radius_m, 1), "v_boost": round(self.v_boost, 2)}


def design_crest(length_m: float, height_target_m: float, v_boost: float) -> CrestDesign:
    limit = length_m ** 2 / (2.0 * math.pi ** 2 * CREST_SAFETY * v_boost ** 2 / G)
    return CrestDesign(length_m, min(height_target_m, limit), v_boost)


def bank_profile(arc: np.ndarray, total: float, curves: list[tuple[float, float, float]]) -> np.ndarray:
    """Peralte con signo (grados, positivo = lado derecho más bajo) en cada muestra: el de cada curva (s0, s1,
    peralte con signo) en todo su arco, con rampas lineales de BANK_RAMP_M a cada lado."""
    out = np.zeros(len(arc))
    for s0, s1, bank in curves:
        d = np.minimum.reduce([np.abs((arc - s0 + total / 2.0) % total - total / 2.0),
                               np.abs((arc - s1 + total / 2.0) % total - total / 2.0)])
        inside = ((arc - s0) % total) <= ((s1 - s0) % total)
        weight = np.where(inside, 1.0, np.clip(1.0 - d / BANK_RAMP_M, 0.0, 1.0))
        stronger = weight * abs(bank) > np.abs(out)
        out = np.where(stronger, weight * bank, out)
    return np.clip(out, -MAX_BANK_DEG, MAX_BANK_DEG)
