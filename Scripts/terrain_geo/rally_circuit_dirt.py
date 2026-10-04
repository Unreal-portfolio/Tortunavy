"""Elementos de circuito de tierra (#682) para los circuitos de Rally por vueltas: baches (whoops y tabla de lavar),
badén con barro y banqueta de tierra en las horquillas. Se colocan por reglas en rally_circuit_plan (baches y badén
son rectas propias; la banqueta va en las horquillas) y se tallan en rally_circuit.

Suspensión del buggy (SuspensionSpec): la de UChaosVehicleWheel en ApplySharedWheelSetup
(Source/Tortunabo/Private/Vehicles/TN_BuggyWheel.cpp): SuspensionMaxRaise = SuspensionMaxDrop = 25 cm y
WheelRadius = 50,4 cm. Un test lee esas cifras del C++ para que no se desincronicen.

Baches (BumpDesign): tren de `count` ondas z = A · (1 - cos(2 pi x / lambda)) (montículos de 2A de pico a pico que
empiezan y acaban a cota 0 con pendiente nula), solo en rectas. Límites:
  - recorrido: con el chasis a media altura, la rueda sube y baja A: A <= SUSPENSION_USE · min(subida, bajada);
  - la rueda cabe en el valle: radio de curvatura del valle lambda² / (4 pi² A) >= radio de rueda;
  - la malla lo representa: lambda >= MIN_WAVELENGTH_M (vóxel de 1 m) y A >= MIN_AMPLITUDE_M (decimado fino).
  Whoops: A 0,14-0,19 m y lambda 8-11 m; tabla de lavar: A 0,07-0,10 m y lambda 4,5-6 m.

Badén (DipDesign): coseno hacia abajo de depth_m y length_m, con barro (vértices más oscuros). El largo sale de la
velocidad de la línea ideal: la curvatura en el fondo y en los bordes, 2 pi² d / L², por v² no pasa de
DIP_MAX_G · g (ni se despega al salir ni se hunde la suspensión al fondo).

Banqueta: caballón de tierra por fuera de la horquilla, de BERM_RISE_M en el borde de la plataforma, que sube desde
el borde de la calzada y baja antes de las barreras; con el peralte de la horquilla subido a BANQUETA_BANK_DEG.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

from .rally_circuit_physics import G

SUSPENSION_RAISE_M = 0.25        # UChaosVehicleWheel::SuspensionMaxRaise (TN_BuggyWheel.cpp)
SUSPENSION_DROP_M = 0.25         # UChaosVehicleWheel::SuspensionMaxDrop
WHEEL_RADIUS_M = 0.504           # UChaosVehicleWheel::WheelRadius
SUSPENSION_USE = 0.8
MIN_WAVELENGTH_M = 4.5
MIN_AMPLITUDE_M = 0.06
BUMP_LEAD_M = 12.0               # recta llana antes y después del tren de baches
BUMP_PATTERNS = {"whoops": {"amplitude_m": (0.14, 0.19), "wavelength_m": (8.0, 11.0), "count": (6, 9)},
                 "tabla_lavar": {"amplitude_m": (0.07, 0.10), "wavelength_m": (4.5, 6.0), "count": (10, 15)}}
DIP_PIECE_M = 70.0
DIP_DEPTH_M = (0.45, 0.65)
DIP_MAX_G = 0.8
DIP_LEAD_M = 10.0
MUD_COLOR = (0.23, 0.16, 0.09)
MUD_STRENGTH = 0.85
BERM_RISE_M = 0.9
BERM_START_M = 6.0               # desde el eje: empieza a subir 1 m antes del borde de la calzada (7 m)
BERM_PEAK_M = 10.5
BERM_END_M = 14.0                # vuelve a la berma antes de las barreras (15-23 m)
BANQUETA_BANK_DEG = (10.0, 13.0)


@dataclass(frozen=True)
class SuspensionSpec:
    raise_m: float = SUSPENSION_RAISE_M
    drop_m: float = SUSPENSION_DROP_M
    wheel_radius_m: float = WHEEL_RADIUS_M

    @property
    def max_amplitude_m(self) -> float:
        return SUSPENSION_USE * min(self.raise_m, self.drop_m)

    def as_dict(self) -> dict:
        return {"raise_m": self.raise_m, "drop_m": self.drop_m, "wheel_radius_m": self.wheel_radius_m,
                "use": SUSPENSION_USE, "max_amplitude_m": round(self.max_amplitude_m, 3)}


SUSPENSION = SuspensionSpec()


@dataclass(frozen=True)
class BumpDesign:
    pattern: str
    amplitude_m: float
    wavelength_m: float
    count: int

    @classmethod
    def draw(cls, pattern: str, rng: np.random.Generator) -> BumpDesign:
        p = BUMP_PATTERNS[pattern]
        lo, hi = p["count"]
        return cls(pattern, float(rng.uniform(*p["amplitude_m"])), float(rng.uniform(*p["wavelength_m"])),
                   int(rng.integers(lo, hi + 1)))

    @property
    def train_m(self) -> float:
        return self.wavelength_m * self.count

    @property
    def piece_m(self) -> float:
        return self.train_m + 2.0 * BUMP_LEAD_M

    @property
    def valley_radius_m(self) -> float:
        return self.wavelength_m ** 2 / (4.0 * math.pi ** 2 * self.amplitude_m)

    def liftoff_kmh(self) -> float:
        """Velocidad a partir de la cual el buggy se despega en las crestas (v² · A (2 pi / lambda)² > g)."""
        return 3.6 * math.sqrt(G / (self.amplitude_m * (2.0 * math.pi / self.wavelength_m) ** 2))

    def within_limits(self, spec: SuspensionSpec = SUSPENSION) -> bool:
        return (self.amplitude_m <= spec.max_amplitude_m and self.valley_radius_m >= spec.wheel_radius_m
                and self.wavelength_m >= MIN_WAVELENGTH_M and self.amplitude_m >= MIN_AMPLITUDE_M)

    def profile(self, x: np.ndarray) -> np.ndarray:
        """Cota relativa (m) a x m del principio de la pieza (BUMP_LEAD_M llanos antes del primer bache)."""
        u = np.asarray(x, dtype=np.float64) - BUMP_LEAD_M
        inside = (u >= 0.0) & (u <= self.train_m)
        return np.where(inside, self.amplitude_m * (1.0 - np.cos(2.0 * math.pi * u / self.wavelength_m)), 0.0)

    def as_dict(self) -> dict:
        return {"pattern": self.pattern, "amplitude_m": round(self.amplitude_m, 3),
                "wavelength_m": round(self.wavelength_m, 2), "count": self.count,
                "peak_to_peak_m": round(2.0 * self.amplitude_m, 3), "train_m": round(self.train_m, 1),
                "valley_radius_m": round(self.valley_radius_m, 2), "liftoff_kmh": round(self.liftoff_kmh(), 1)}


@dataclass(frozen=True)
class DipDesign:
    depth_m: float
    length_m: float
    v_ms: float

    @property
    def curvature_g(self) -> float:
        """Aceleración vertical en el fondo y en los bordes a v_ms, en g."""
        return self.v_ms ** 2 * 2.0 * math.pi ** 2 * self.depth_m / self.length_m ** 2 / G

    def profile(self, x: np.ndarray) -> np.ndarray:
        """Cota relativa a x m del principio de la pieza (DIP_LEAD_M llanos antes del badén)."""
        u = np.asarray(x, dtype=np.float64) - DIP_LEAD_M
        inside = (u >= 0.0) & (u <= self.length_m)
        return np.where(inside, -0.5 * self.depth_m * (1.0 - np.cos(2.0 * math.pi * u / self.length_m)), 0.0)

    def as_dict(self) -> dict:
        return {"depth_m": round(self.depth_m, 3), "length_m": round(self.length_m, 2),
                "v_kmh": round(self.v_ms * 3.6, 1), "curvature_g": round(self.curvature_g, 3)}


def design_dip(depth_target_m: float, v_ms: float, max_length_m: float = DIP_PIECE_M - 2.0 * DIP_LEAD_M) -> DipDesign:
    """Badén de depth_target_m que cabe en max_length_m: el largo justo para DIP_MAX_G a v_ms; si no cabe, menos
    hondo."""
    length = math.pi * math.sqrt(2.0 * depth_target_m * v_ms ** 2 / (DIP_MAX_G * G))
    if length <= max_length_m:
        return DipDesign(depth_target_m, max(length, 20.0), v_ms)
    depth = DIP_MAX_G * G * max_length_m ** 2 / (2.0 * math.pi ** 2 * v_ms ** 2)
    return DipDesign(depth, max_length_m, v_ms)


def berm_lift(outside_m: np.ndarray) -> np.ndarray:
    """Subida (m) del caballón de la banqueta a `outside_m` del eje por el lado de fuera de la curva: de 0 en
    BERM_START_M a BERM_RISE_M en BERM_PEAK_M (cóncava, como un peralte que se cierra) y de vuelta a 0 en
    BERM_END_M."""
    d = np.asarray(outside_m, dtype=np.float64)
    up = np.clip((d - BERM_START_M) / (BERM_PEAK_M - BERM_START_M), 0.0, 1.0) ** 2
    down = np.clip((BERM_END_M - d) / (BERM_END_M - BERM_PEAK_M), 0.0, 1.0)
    return BERM_RISE_M * np.where(d <= BERM_PEAK_M, up, 0.5 - 0.5 * np.cos(math.pi * down))
