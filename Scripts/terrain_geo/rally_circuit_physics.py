"""Física de punto del buggy del Rally para dimensionar y validar los circuitos por vueltas (#622).

No simula Chaos: es la envolvente de lo que el buggy puede hacer, calibrada con los valores del juego y las medidas
de Docs/Rally_MVP.md. Sirve para dos cosas:

  - perfil de velocidad de la línea ideal sobre el eje (aceleración con pendiente, límite de curva con peralte y
    frenada hacia atrás), de donde sale la velocidad de llegada a cada salto;
  - vuelo balístico desde el labio de una rampa sobre el perfil de la calzada (punto y ángulo de aterrizaje), y
    despegue sobre un perfil medido en la malla (takeoff_flight).

Fuentes de cada constante (BuggySpec):
  - punta: TNRallyTurret::BuggyTopSpeedCms = 3050 cm/s (Source/Tortunabo/Public/Vehicles/TN_RallyTurretLogic.h);
  - arranque: 0-60 km/h en unos 2 s (UTN_BuggyData::MaxTorque = 1275, par plano hasta el 55 % de MaxRPM, que son
    60 km/h: TNBuggy::TorqueCurveKeys);
  - medio: 0-100 km/h en 6,6 s medido en I03R (Docs/Rally_MVP.md), con la aceleración cayendo hasta la punta;
  - frenada: de 60 km/h en 13 m medida en I03R y E01B (Docs/Rally_MVP.md): 10,6 m/s²;
  - agarre lateral: 1,2 g (Docs/Rally_E01B_y_Biplaza.md §1.2, «≥ 25 m a 60 km/h»);
  - turbo: UTN_BuggyData::BoostTopSpeedMultiplier 1,15, BoostTorqueMultiplier 1,35 y BoostPushAccel 300 cm/s².
"""

from __future__ import annotations

import math
from dataclasses import asdict, dataclass

import numpy as np
from scipy import ndimage

G = 9.81
BOOST_SECONDS = 3.0         # una barra de turbo llena (UTN_BuggyData::BoostDrainPerSecond = 1/3)


@dataclass(frozen=True)
class BuggySpec:
    top_speed_ms: float = 30.5
    launch_accel_ms2: float = 8.3
    flat_torque_ms: float = 16.7
    mid_accel_ms2: float = 4.9
    brake_decel_ms2: float = 10.6
    lateral_grip: float = 1.2
    boost_top_multiplier: float = 1.15
    boost_torque_multiplier: float = 1.35
    boost_push_ms2: float = 3.0

    def as_dict(self) -> dict:
        return asdict(self)


BUGGY = BuggySpec()


def drive_accel(v: float, spec: BuggySpec = BUGGY, boost: bool = False) -> float:
    """Aceleración del motor (m/s²) a v m/s en llano: plana hasta flat_torque_ms y luego lineal hasta 0 en la punta.
    Con turbo, el par por boost_torque_multiplier sobre la curva estirada a la punta del turbo más el empuje."""
    top = spec.top_speed_ms * (spec.boost_top_multiplier if boost else 1.0)
    scaled = v * spec.top_speed_ms / top
    if scaled < spec.flat_torque_ms:
        base = spec.launch_accel_ms2
    else:
        base = spec.mid_accel_ms2 * max(0.0, spec.top_speed_ms - scaled) / (spec.top_speed_ms - spec.flat_torque_ms)
    if not boost:
        return base
    return base * spec.boost_torque_multiplier + (spec.boost_push_ms2 if v < top else 0.0)


def top_speed(spec: BuggySpec = BUGGY, boost: bool = False) -> float:
    return spec.top_speed_ms * (spec.boost_top_multiplier if boost else 1.0)


def curve_speed(curvature: np.ndarray, bank_tan_inward: np.ndarray, spec: BuggySpec = BUGGY) -> np.ndarray:
    """Velocidad máxima (m/s) en una curva de curvatura |k| (1/m) con el peralte hacia dentro (tangente, >= 0):
    v² = g·R·(mu + tan b) / (1 - mu·tan b). Infinita en recta."""
    k = np.abs(np.asarray(curvature, dtype=np.float64))
    tb = np.clip(np.asarray(bank_tan_inward, dtype=np.float64), 0.0, None)
    mu = spec.lateral_grip
    factor = (mu + tb) / np.maximum(1.0 - mu * tb, 1e-6)
    with np.errstate(divide="ignore"):
        return np.where(k > 1e-6, np.sqrt(G * factor / np.maximum(k, 1e-6)), np.inf)


def speed_profile(curvature: np.ndarray, bank_tan_inward: np.ndarray, grade_sin: np.ndarray, airborne: np.ndarray,
                  ds: float = 1.0, closed: bool = True, spec: BuggySpec = BUGGY, boost: bool = False) -> np.ndarray:
    """Velocidad de la línea ideal (m/s) en cada muestra del eje (cada ds m): acelera con el motor y la pendiente
    hasta el límite de curva y la punta, y frena hacia atrás lo justo para entrar en cada curva. En el aire
    (airborne) no hay tracción ni frenada: conserva la velocidad. En un circuito cerrado es la vuelta lanzada (tres
    vueltas seguidas, se toma la del medio); abierto, sale parado."""
    n = len(curvature)
    reps = 3 if closed else 1
    limit = np.minimum(curve_speed(curvature, bank_tan_inward, spec), top_speed(spec, boost))
    lim = np.tile(limit, reps)
    sin_g = np.tile(np.asarray(grade_sin, dtype=np.float64), reps)
    air = np.tile(np.asarray(airborne, dtype=bool), reps)
    v = np.empty(n * reps)
    v[0] = lim[0] if closed else 0.0
    for k in range(1, n * reps):
        if air[k]:
            v[k] = v[k - 1]
            continue
        a = drive_accel(v[k - 1], spec, boost) - G * sin_g[k - 1]
        v[k] = min(lim[k], math.sqrt(max(0.0, v[k - 1] ** 2 + 2.0 * a * ds)))
    for k in range(n * reps - 2, -1, -1):
        if air[k + 1]:
            continue
        b = spec.brake_decel_ms2 + G * sin_g[k]
        v[k] = min(v[k], math.sqrt(max(0.0, v[k + 1] ** 2 + 2.0 * max(b, 0.5) * ds)))
    return v[n:2 * n].copy() if closed else v


@dataclass(frozen=True)
class Flight:
    x_land_m: float          # distancia horizontal del labio al punto de contacto
    z_land_m: float          # cota relativa al labio en el contacto
    airtime_s: float
    apex_m: float            # altura máxima sobre el labio
    descent_deg: float       # ángulo de la trayectoria al tocar (positivo hacia abajo)
    ground_deg: float        # ángulo del suelo en el contacto (positivo cuesta abajo)


def flight(x: np.ndarray, z: np.ndarray, v: float, launch_deg: float, min_x: float = 0.5) -> Flight | None:
    """Vuelo desde el labio (x = 0, z = 0) a v m/s con launch_deg de salida sobre el perfil (x, z) de la calzada por
    delante del labio (x creciente, cotas relativas al labio). None si no toca suelo dentro del perfil."""
    th = math.radians(launch_deg)
    vx, vz = v * math.cos(th), v * math.sin(th)
    if vx <= 0.1:
        return None
    traj = vz * (x / vx) - 0.5 * G * (x / vx) ** 2
    below = np.nonzero((x >= min_x) & (traj <= z))[0]
    if len(below) == 0:
        return None
    i = int(below[0])
    if i > 0:                                    # interpolación lineal entre las dos muestras que lo encierran
        d0, d1 = traj[i - 1] - z[i - 1], traj[i] - z[i]
        f = d0 / (d0 - d1) if d0 != d1 else 1.0
        xl = float(x[i - 1] + f * (x[i] - x[i - 1]))
    else:
        xl = float(x[i])
    t = xl / vx
    zl = vz * t - 0.5 * G * t * t
    slope = np.gradient(z, x)
    ground = float(np.interp(xl, x, slope))
    return Flight(round(xl, 2), round(zl, 3), round(t, 3), round(vz * vz / (2.0 * G), 3),
                  round(math.degrees(math.atan2(G * t - vz, vx)), 2), round(-math.degrees(math.atan(ground)), 2))


def boost_arrival(speed: np.ndarray, curvature: np.ndarray, bank_tan_inward: np.ndarray, grade_sin: np.ndarray,
                  index: int, ds: float = 1.0, seconds: float = BOOST_SECONDS, spec: BuggySpec = BUGGY) -> float:
    """Velocidad en la muestra `index` si se pisa el turbo (una barra llena, `seconds`) justo antes de llegar: desde
    la línea ideal `seconds` antes, con la aceleración del turbo y los mismos límites de curva."""
    n = len(speed)
    start, t = index, 0.0
    while t < seconds and (index - start) < n - 1:
        start -= 1
        t += ds / max(float(speed[start % n]), 1.0)
    limit = np.minimum(curve_speed(curvature, bank_tan_inward, spec), top_speed(spec, boost=True))
    v = float(speed[start % n])
    for k in range(start + 1, index + 1):
        a = drive_accel(v, spec, boost=True) - G * float(grade_sin[(k - 1) % n])
        v = min(float(limit[k % n]), math.sqrt(max(0.0, v * v + 2.0 * a * ds)))
    return v


def takeoff_flight(x: np.ndarray, z: np.ndarray, v: float, window: tuple[float, float] = (-8.0, 4.0),
                   smooth_m: float = 0.75, min_air_m: float = 2.0) -> tuple[Flight | None, float, float]:
    """Despegue y vuelo sobre un perfil MEDIDO (x creciente con paso uniforme, cotas de la malla): el buggy despega
    en el primer punto de `window` en el que la curvatura convexa del suelo (suavizado smooth_m) pide más aceleración
    centrípeta de la que da la gravedad (v²·k >= g·cos a) y la parábola, con la pendiente del metro anterior, no
    vuelve a tocar el suelo en min_air_m. Devuelve (vuelo con x_land_m desde x = 0, x de despegue, ángulo)."""
    dx = float(x[1] - x[0])
    zs = ndimage.gaussian_filter1d(np.asarray(z, dtype=np.float64), smooth_m / dx, mode="nearest")
    d1 = np.gradient(zs, dx)
    convex = -np.gradient(d1, dx) / (1.0 + d1 * d1) ** 1.5
    lift = (v * v * convex >= G / np.sqrt(1.0 + d1 * d1)) & (x >= window[0]) & (x <= window[1])
    back = max(1, int(round(1.0 / dx)))
    for i in np.nonzero(lift)[0]:
        if i < back:
            continue
        angle = math.degrees(math.atan((z[i] - z[i - back]) / (back * dx)))
        fl = flight(x[i:] - x[i], z[i:] - z[i], v, angle, min_x=0.25)
        if fl is not None and fl.x_land_m >= min_air_m:
            return Flight(round(fl.x_land_m + float(x[i]), 2), fl.z_land_m, fl.airtime_s, fl.apex_m, fl.descent_deg,
                          fl.ground_deg), float(x[i]), angle
    return None, math.nan, math.nan
