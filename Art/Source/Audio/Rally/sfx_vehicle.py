"""Recetas de síntesis del buggy: motor, derrape, turbo, aterrizaje, choque y explosión.

Cada receta recibe un ``np.random.Generator`` con semilla fija y devuelve la
señal cruda (sin nivel final): el masterizado lo hace ``sfx_master``.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from sfx_dsp import (
    SR, circular_shape, env_ad, fade_edges, highpass, lowpass, mag_band,
    mag_highpass, mag_lowpass, modal, n_of, normalize_peak, normalize_rms, periodic_smooth,
    phase_from_freq, place, scatter_grains, sine_sweep, spring_boing, svf, time_axis,
)

ENGINE_LOOP_S = 2.0
# Bicilíndrico de cuatro tiempos con cigüeñal a 270 grados: explosiones
# desiguales (0 y 270 de cada 720 grados), el «potato» simpático de una moto pequeña.
FIRING_OFFSETS = (0.0, 270.0 / 720.0)
CYLINDER_GAIN = (1.0, 0.86)


@dataclass(frozen=True)
class EngineLayer:
    cycle_hz: float      # ciclos de 720 grados por segundo = rpm / 120
    exhaust_hz: float    # resonancia del escape
    tau_s: float         # caída de cada pulso
    noise_lp_hz: float   # brillo del soplido de cada explosión
    noise_amt: float
    out_lp_hz: float     # silenciador
    jitter: float        # irregularidad de encendido (fracción de ciclo)

    @property
    def rpm(self) -> int:
        return int(round(self.cycle_hz * 120))


# Las frecuencias de ciclo dan un número entero de ciclos en 2 s (bucle exacto).
ENGINE_LAYERS = {
    "idle": EngineLayer(7.5, 92.0, 0.034, 1300.0, 0.55, 1700.0, 0.010),
    "mid": EngineLayer(18.5, 116.0, 0.021, 1900.0, 0.65, 2500.0, 0.007),
    "high": EngineLayer(28.5, 142.0, 0.014, 2600.0, 0.75, 3300.0, 0.005),
}


def _exhaust_pulse(layer: EngineLayer, rng: np.random.Generator, amp: float) -> np.ndarray:
    n = n_of(0.14)
    t = time_axis(n)
    freq = layer.exhaust_hz * (1.0 + 0.22 * np.exp(-t / 0.008))
    phase = phase_from_freq(freq)
    tone = np.sin(phase) * env_ad(n, 0.0015, layer.tau_s)
    overtone = 0.3 * np.sin(2.37 * phase) * env_ad(n, 0.001, layer.tau_s * 0.5)
    thump = 0.45 * np.sin(phase * 0.5) * env_ad(n, 0.002, layer.tau_s * 1.4)
    burst = normalize_rms(lowpass(rng.standard_normal(n), layer.noise_lp_hz))
    burst = 0.25 * layer.noise_amt * burst * env_ad(n, 0.0008, layer.tau_s * 0.35)
    return amp * (tone + overtone + thump + burst)


def engine_loop(layer: EngineLayer, rng: np.random.Generator) -> np.ndarray:
    n = n_of(ENGINE_LOOP_S)
    cycles = int(round(ENGINE_LOOP_S * layer.cycle_hz))
    out = np.zeros(n)
    for cycle in range(cycles):
        for offset, gain in zip(FIRING_OFFSETS, CYLINDER_GAIN):
            jitter = rng.normal(0.0, layer.jitter)
            start = int(round((cycle + offset + jitter) / layer.cycle_hz * SR)) % n
            pulse = _exhaust_pulse(layer, rng, gain * (1.0 + rng.normal(0.0, 0.07)))
            np.add.at(out, (start + np.arange(len(pulse))) % n, pulse)
    t = time_axis(n)
    mech = sum(np.sin(2 * np.pi * layer.cycle_hz * k * t + rng.uniform(0, 2 * np.pi)) / k
               for k in range(4, 14))
    ticks = circular_shape(rng.standard_normal(n), mag_band(3800.0, 0.4))
    tick_env = (0.5 + 0.5 * np.cos(2 * np.pi * layer.cycle_hz * 4 * t)) ** 12
    out = normalize_peak(out) + 0.03 * normalize_peak(mech) + 0.02 * normalize_peak(ticks * tick_env)
    out = circular_shape(out, mag_lowpass(layer.out_lp_hz, 2), mag_highpass(32.0, 2))
    drive = 1.4
    return np.tanh(drive * normalize_peak(out)) / np.tanh(drive)


def skid_loop(rng: np.random.Generator) -> np.ndarray:
    """Arena rascada: lecho de ruido, miles de granos crujientes y retumbo de neumático."""
    n = n_of(2.0)
    density = 0.55 + 0.45 * periodic_smooth(n, rng, 9.0)
    swell = 0.7 + 0.3 * periodic_smooth(n, rng, 3.0)
    bed = circular_shape(rng.standard_normal(n), lambda f: mag_band(2100.0, 1.2)(f) + 0.5 * mag_band(650.0, 1.0)(f))
    src = circular_shape(rng.standard_normal(n), mag_highpass(1800.0, 2), mag_lowpass(9000.0, 2))
    count = 4200
    starts = rng.integers(0, n, count)
    keep = rng.random(count) < density[starts]
    starts = starts[keep]
    amps = np.minimum(rng.exponential(1.0, len(starts)), 3.0)
    lengths = rng.integers(30, 180, len(starts))
    grains = scatter_grains(n, src, starts, amps, lengths, rng, circular=True)
    rumble = circular_shape(rng.standard_normal(n), mag_lowpass(220.0, 2), mag_highpass(45.0, 2))
    out = (0.35 * normalize_rms(bed) * swell * density + 0.55 * normalize_rms(grains)
           + 0.4 * normalize_rms(rumble) * swell)
    return circular_shape(out, mag_highpass(70.0, 2), mag_lowpass(10000.0, 2))


TURBO_WHISTLE_HZ = 2400.0  # 3600 ciclos en 1,5 s
TURBO_MIX = {"air": 0.5, "hiss": 0.16, "roar": 0.26, "whistle": 0.11}


def turbo_loop(rng: np.random.Generator) -> np.ndarray:
    n = n_of(1.5)
    t = time_axis(n)
    air = normalize_rms(circular_shape(rng.standard_normal(n), mag_band(1100.0, 1.0)))
    hiss = normalize_rms(circular_shape(rng.standard_normal(n), mag_highpass(4000.0, 2), mag_lowpass(11000.0, 2)))
    roar = normalize_rms(circular_shape(rng.standard_normal(n), mag_band(180.0, 0.8)))
    flutter = 1.0 + 0.08 * np.sin(2 * np.pi * (8 / 1.5) * t)
    vib = 1.5 * np.sin(2 * np.pi * (6 / 1.5) * t)
    whistle = np.sin(2 * np.pi * TURBO_WHISTLE_HZ * t + vib) + 0.35 * np.sin(4 * np.pi * TURBO_WHISTLE_HZ * t + 2 * vib)
    return (TURBO_MIX["air"] * air * flutter + TURBO_MIX["hiss"] * hiss + TURBO_MIX["roar"] * roar
            + TURBO_MIX["whistle"] * whistle / np.sqrt(0.5 * (1 + 0.35 ** 2)))


def turbo_start(rng: np.random.Generator) -> np.ndarray:
    """Soplido creciente que termina con la misma mezcla y nivel que turbo_loop."""
    n = n_of(0.6)
    u = time_axis(n) / 0.6
    air = normalize_rms(svf(rng.standard_normal(n), 320.0 * (1100.0 / 320.0) ** u, 1.2, "bp"))
    hiss = normalize_rms(highpass(lowpass(rng.standard_normal(n), 11000.0), 4000.0))
    roar = normalize_rms(svf(rng.standard_normal(n), 120.0 + 60.0 * u, 1.0, "bp"))
    whistle_f = 1000.0 * (TURBO_WHISTLE_HZ / 1000.0) ** (u ** 0.6)
    phase = phase_from_freq(whistle_f)
    whistle = (np.sin(phase) + 0.35 * np.sin(2 * phase)) / np.sqrt(0.5 * (1 + 0.35 ** 2))
    out = (TURBO_MIX["air"] * air * u ** 1.4 + TURBO_MIX["hiss"] * hiss * u ** 2.2
           + TURBO_MIX["roar"] * roar * u ** 1.0 + TURBO_MIX["whistle"] * whistle * u ** 2.0)
    return fade_edges(out, 0.005, 0.012)


def land(rng: np.random.Generator) -> np.ndarray:
    n = n_of(0.5)
    thud = sine_sweep(n, 120.0, 42.0, 0.08) * env_ad(n, 0.002, 0.09)
    impact = 0.6 * normalize_peak(lowpass(rng.standard_normal(n), 500.0)) * env_ad(n, 0.001, 0.035)
    spring = 0.32 * place(n, spring_boing(n, 210.0, 11.0, 0.12, 0.16), 0.012)
    m = n_of(0.25)
    rebound = 0.45 * place(n, sine_sweep(m, 95.0, 50.0, 0.05) * env_ad(m, 0.012, 0.06), 0.17)
    sand = 0.22 * normalize_peak(highpass(lowpass(rng.standard_normal(n), 4000.0), 900.0)) * env_ad(n, 0.012, 0.11)
    return fade_edges(thud + impact + spring + rebound + sand, 0.0005, 0.03)


def _clink(rng: np.random.Generator, length_s: float, lo: float, hi: float) -> np.ndarray:
    m = n_of(length_s)
    freqs = np.sort(rng.uniform(lo, hi, 3))
    tick = normalize_peak(highpass(rng.standard_normal(m), 2500.0)) * env_ad(m, 0.0002, 0.0015)
    return modal(m, freqs, (0.03, 0.02, 0.012), (0.5, 0.35, 0.2), 0.0003) + 0.25 * tick


def crash(rng: np.random.Generator) -> np.ndarray:
    n = n_of(0.7)
    transient = 0.8 * normalize_peak(highpass(rng.standard_normal(n), 300.0)) * env_ad(n, 0.0005, 0.012)
    thud = 0.9 * sine_sweep(n, 110.0, 50.0, 0.06) * env_ad(n, 0.002, 0.07)
    freqs = np.array([185, 312, 458, 731, 1127, 1690, 2480, 3350]) * rng.uniform(0.97, 1.03, 8)
    panel = modal(n, freqs, (0.18, 0.15, 0.12, 0.1, 0.08, 0.06, 0.05, 0.04),
                  (0.5, 0.45, 0.4, 0.32, 0.28, 0.2, 0.15, 0.1))
    rattle = np.zeros(n)
    when = 0.035
    for i in range(9):
        when += rng.exponential(0.045)
        rattle += 0.35 * 0.8 ** i * place(n, _clink(rng, 0.12, 1400.0, 4500.0), when)
    dust = 0.15 * normalize_peak(highpass(rng.standard_normal(n), 1500.0)) * env_ad(n, 0.02, 0.15)
    return fade_edges(transient + thud + 0.8 * panel + rattle + dust, 0.0003, 0.04)


def explode(rng: np.random.Generator) -> np.ndarray:
    """Puf de arena: golpe sordo, nube filtrada, lluvia de granos y muelles que saltan."""
    n = n_of(1.2)
    t = time_axis(n)
    boom = 0.9 * sine_sweep(n, 85.0, 32.0, 0.25) * env_ad(n, 0.006, 0.22)
    cutoff = 250.0 + 2300.0 * np.exp(-t / 0.15)
    puff = 0.8 * normalize_peak(svf(rng.standard_normal(n), cutoff, 0.8, "lp")) * env_ad(n, 0.008, 0.28)
    src = highpass(lowpass(rng.standard_normal(n), 8000.0), 2000.0)
    count = 1100
    grain_t = 0.08 + rng.exponential(0.32, count)
    grain_t = grain_t[grain_t < 1.12]
    grain_amps = rng.exponential(1.0, len(grain_t)) * np.exp(-(grain_t - 0.08) / 0.3)
    sand = scatter_grains(n, src, (grain_t * SR).astype(int), grain_amps,
                          rng.integers(40, 200, len(grain_t)), rng)
    springs = sum(amp * place(n, spring_boing(n, f0, wob, 0.22, tau, 0.15), at)
                  for f0, wob, tau, at, amp in ((330, 13, 0.35, 0.06, 0.3), (470, 15, 0.3, 0.14, 0.24),
                                                (260, 10, 0.3, 0.29, 0.26)))
    clanks = 0.25 * place(n, _clink(rng, 0.15, 900.0, 3000.0), 0.42) + 0.15 * place(n, _clink(rng, 0.15, 1200.0, 3500.0), 0.63)
    sand_level = 0.35 * normalize_peak(sand)
    return fade_edges(boom + puff + sand_level + springs + clanks, 0.0005, 0.08)
