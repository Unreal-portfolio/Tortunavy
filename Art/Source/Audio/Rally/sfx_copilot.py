"""Recetas de síntesis de las señales del copiloto (#331, #330).

Señales cortas de interfaz, de la misma familia que el semáforo de salida
(tono senoidal con dos armónicos suaves), pensadas para sonar a un lado de la
cámara: el pitido de curva se repite tantas veces como el grado de la nota,
así que dura menos que la separación entre pitidos (0,11 s). Cada receta
recibe un ``np.random.Generator`` con semilla fija y devuelve la señal cruda;
el masterizado lo hace ``sfx_master``.
"""
from __future__ import annotations

import numpy as np

from sfx_dsp import env_ad, env_asr, fade_edges, lowpass, n_of, phase_from_freq, sweep_freq, time_axis

BEEP_S = 0.09
CUE_S = 0.3


def _tone(freq: np.ndarray) -> np.ndarray:
    phase = phase_from_freq(freq)
    return np.sin(phase) + 0.12 * np.sin(2 * phase) + 0.06 * np.sin(3 * phase)


def call_beep(rng: np.random.Generator) -> np.ndarray:
    """Pitido de curva: corto y limpio, una octava por encima del semáforo (el tono lo sube UE en las cerradas)."""
    n = n_of(BEEP_S)
    return fade_edges(_tone(np.full(n, 1320.0)) * env_asr(n, 0.003, 0.05, 0.035), 0.0005, 0.01)


def call_crest(rng: np.random.Generator) -> np.ndarray:
    """Cresta: sube y baja, como el cambio de rasante."""
    n = n_of(CUE_S)
    t = time_axis(n)
    freq = 900.0 + 500.0 * np.sin(np.pi * np.clip(t / 0.24, 0.0, 1.0))
    return fade_edges(_tone(freq) * env_asr(n, 0.004, 0.2, 0.08), 0.0005, 0.02)


def call_jump(rng: np.random.Generator) -> np.ndarray:
    """Salto: barrido hacia arriba que se corta en lo alto."""
    n = n_of(CUE_S)
    return fade_edges(_tone(sweep_freq(n, 600.0, 1800.0, 0.22)) * env_asr(n, 0.004, 0.22, 0.06), 0.0005, 0.02)


def call_water(rng: np.random.Generator) -> np.ndarray:
    """Agua: dos «blup» que caen, con un poco de soplo filtrado."""
    n = n_of(CUE_S)
    out = np.zeros(n)
    for start_s, f_start in ((0.0, 1500.0), (0.13, 1150.0)):
        start = n_of(start_s)
        m = n - start
        blup = _tone(sweep_freq(m, f_start, f_start * 0.45, 0.08)) * env_ad(m, 0.002, 0.04)
        out[start:] += blup
    hiss = 0.08 * lowpass(rng.standard_normal(n), 2500.0) * env_ad(n, 0.003, 0.06)
    return fade_edges(out + hiss, 0.0005, 0.02)
