"""Primitivas de síntesis para los SFX del modo Rally (Tortunabo).

Todas las funciones devuelven arrays nuevos (float64, mono, SR Hz) y no mutan
la entrada. Las funciones ``circular_*`` y ``periodic_*`` tratan la señal como
periódica: así los bucles no tienen costura, porque todo lo que suena en ellos
es periódico en la longitud exacta del bucle.
"""
from __future__ import annotations

from typing import Callable

import numpy as np
from scipy import signal

SR = 44100
MIN_ATTACK_S = 0.0003


def n_of(duration_s: float) -> int:
    return int(round(duration_s * SR))


def time_axis(n: int) -> np.ndarray:
    return np.arange(n) / SR


def phase_from_freq(freq: np.ndarray) -> np.ndarray:
    """Fase acumulada (rad) de una frecuencia instantánea; empieza en 0."""
    return 2.0 * np.pi * np.concatenate(([0.0], np.cumsum(freq[:-1]))) / SR


def sweep_freq(n: int, f_start: float, f_end: float, glide_s: float | None = None) -> np.ndarray:
    """Frecuencia con barrido exponencial de f_start a f_end en glide_s."""
    glide = n / SR if glide_s is None else glide_s
    u = np.clip(time_axis(n) / max(glide, 1e-6), 0.0, 1.0)
    return f_start * (f_end / f_start) ** u


def sine_sweep(n: int, f_start: float, f_end: float, glide_s: float | None = None) -> np.ndarray:
    return np.sin(phase_from_freq(sweep_freq(n, f_start, f_end, glide_s)))


def env_ad(n: int, attack_s: float, tau_s: float) -> np.ndarray:
    """Ataque en coseno alzado y caída exponencial con constante tau_s."""
    t = time_axis(n)
    attack = max(attack_s, MIN_ATTACK_S)
    rise = 0.5 - 0.5 * np.cos(np.pi * np.clip(t / attack, 0.0, 1.0))
    fall = np.exp(-np.maximum(t - attack, 0.0) / tau_s)
    return rise * fall


def env_asr(n: int, attack_s: float, release_start_s: float, release_s: float) -> np.ndarray:
    """Ataque, sostenido a 1 y relajación en coseno alzado."""
    t = time_axis(n)
    rise = 0.5 - 0.5 * np.cos(np.pi * np.clip(t / max(attack_s, MIN_ATTACK_S), 0.0, 1.0))
    rel = np.clip((t - release_start_s) / release_s, 0.0, 1.0)
    return rise * (0.5 + 0.5 * np.cos(np.pi * rel))


def modal(n: int, freqs, taus, amps, attack_s: float = 0.0005) -> np.ndarray:
    """Suma de modos amortiguados (síntesis modal de golpes)."""
    t = time_axis(n)
    out = np.zeros(n)
    for freq, tau, amp in zip(freqs, taus, amps):
        out += amp * np.sin(2.0 * np.pi * freq * t) * env_ad(n, attack_s, tau)
    return out


def spring_boing(n: int, f0: float, wobble_hz: float, depth: float, tau: float, droop: float = 0.1) -> np.ndarray:
    """Muelle de dibujo animado: tono con vibrato ancho que se apaga."""
    t = time_axis(n)
    wobble = depth * np.sin(2.0 * np.pi * wobble_hz * t) * np.exp(-t / (tau * 1.2))
    freq = f0 * (1.0 + wobble) * (1.0 - droop * np.clip(t / (tau * 3.0), 0.0, 1.0))
    phase = phase_from_freq(freq)
    tone = np.sin(phase) + 0.3 * np.sin(2.71 * phase) * np.exp(-t / (tau * 0.4))
    return tone * env_ad(n, 0.002, tau)


def place(n: int, sig: np.ndarray, start_s: float) -> np.ndarray:
    """Coloca sig en un buffer de n muestras a partir de start_s (recorta)."""
    out = np.zeros(n)
    start = n_of(start_s)
    if start >= n:
        return out
    length = min(len(sig), n - start)
    out[start:start + length] = sig[:length]
    return out


def add_circular(buf: np.ndarray, sig: np.ndarray, start: int) -> np.ndarray:
    """Suma sig a buf empezando en start y dando la vuelta al final."""
    out = buf.copy()
    idx = (start + np.arange(len(sig))) % len(buf)
    np.add.at(out, idx, sig)
    return out


def normalize_rms(x: np.ndarray) -> np.ndarray:
    rms = float(np.sqrt(np.mean(x ** 2)))
    if rms < 1e-12:
        raise ValueError("Señal vacía: no se puede normalizar")
    return x / rms


def normalize_peak(x: np.ndarray) -> np.ndarray:
    peak = float(np.max(np.abs(x)))
    if peak < 1e-12:
        raise ValueError("Señal vacía: no se puede normalizar")
    return x / peak


def fade_edges(x: np.ndarray, fade_in_s: float = 0.001, fade_out_s: float = 0.02) -> np.ndarray:
    out = x.copy()
    n_in, n_out = max(n_of(fade_in_s), 1), max(n_of(fade_out_s), 1)
    out[:n_in] *= 0.5 - 0.5 * np.cos(np.pi * np.arange(n_in) / n_in)
    out[-n_out:] *= 0.5 + 0.5 * np.cos(np.pi * (np.arange(n_out) + 1) / n_out)
    return out


# --- Filtros causales (one-shots) -------------------------------------------

def _butter(x: np.ndarray, order: int, cutoff, kind: str) -> np.ndarray:
    sos = signal.butter(order, cutoff, btype=kind, fs=SR, output="sos")
    return signal.sosfilt(sos, x)


def lowpass(x: np.ndarray, fc: float, order: int = 2) -> np.ndarray:
    return _butter(x, order, fc, "lowpass")


def highpass(x: np.ndarray, fc: float, order: int = 2) -> np.ndarray:
    return _butter(x, order, fc, "highpass")


def bandpass(x: np.ndarray, lo: float, hi: float, order: int = 2) -> np.ndarray:
    return _butter(x, order, (lo, hi), "bandpass")


def svf(x: np.ndarray, fc, q: float, mode: str = "bp") -> np.ndarray:
    """Filtro de estado variable TPT con frecuencia de corte variable en el tiempo."""
    if mode not in ("lp", "bp", "hp"):
        raise ValueError(f"Modo de SVF no válido: {mode}")
    fc_arr = np.broadcast_to(np.clip(np.asarray(fc, dtype=float), 10.0, 0.45 * SR), x.shape)
    g = np.tan(np.pi * fc_arr / SR)
    k = 1.0 / q
    a1 = 1.0 / (1.0 + g * (g + k))
    a2 = g * a1
    a3 = g * a2
    out = np.empty_like(x)
    ic1 = ic2 = 0.0
    for i, xi in enumerate(x):
        v3 = xi - ic2
        v1 = a1[i] * ic1 + a2[i] * v3
        v2 = ic2 + a2[i] * ic1 + a3[i] * v3
        ic1, ic2 = 2.0 * v1 - ic1, 2.0 * v2 - ic2
        out[i] = v2 if mode == "lp" else v1 if mode == "bp" else xi - k * v1 - v2
    return out


# --- Procesado circular (bucles) --------------------------------------------

MagFn = Callable[[np.ndarray], np.ndarray]


def mag_lowpass(fc: float, order: int = 2) -> MagFn:
    return lambda f: 1.0 / np.sqrt(1.0 + (f / fc) ** (2 * order))


def mag_highpass(fc: float, order: int = 2) -> MagFn:
    return lambda f: 1.0 / np.sqrt(1.0 + (fc / np.maximum(f, 1e-3)) ** (2 * order))


def mag_band(fc: float, width_oct: float) -> MagFn:
    """Campana gaussiana en escala logarítmica; width_oct = desviación en octavas."""
    return lambda f: np.exp(-0.5 * (np.log2(np.maximum(f, 1e-3) / fc) / width_oct) ** 2)


def circular_shape(x: np.ndarray, *mags: MagFn) -> np.ndarray:
    """Filtra x en frecuencia como señal periódica (sin transitorio de arranque)."""
    freqs = np.fft.rfftfreq(len(x), 1.0 / SR)
    gain = np.ones_like(freqs)
    for mag in mags:
        gain = gain * mag(freqs)
    return np.fft.irfft(np.fft.rfft(x) * gain, len(x))


def periodic_smooth(n: int, rng: np.random.Generator, max_hz: float) -> np.ndarray:
    """Ruido lento periódico en n muestras, normalizado a [-1, 1]."""
    duration = n / SR
    k_max = max(1, int(max_hz * duration))
    spectrum = np.zeros(n // 2 + 1, dtype=complex)
    spectrum[1:k_max + 1] = rng.standard_normal(k_max) + 1j * rng.standard_normal(k_max)
    return normalize_peak(np.fft.irfft(spectrum, n))


def scatter_grains(n: int, src: np.ndarray, starts, amps, lengths, rng: np.random.Generator,
                   circular: bool = False) -> np.ndarray:
    """Granos con ventana de Hann tomados de src en posiciones aleatorias."""
    out = np.zeros(n)
    for start, amp, length in zip(starts, amps, lengths):
        length = int(length)
        offset = int(rng.integers(0, len(src) - length))
        grain = amp * src[offset:offset + length] * np.hanning(length)
        if circular:
            np.add.at(out, (int(start) + np.arange(length)) % n, grain)
            continue
        end = min(int(start) + length, n)
        out[int(start):end] += grain[:end - int(start)]
    return out


def chirp_drop(n: int, f0: float, rise: float, tau: float, attack_s: float = 0.002) -> np.ndarray:
    """Gota o burbuja: tono que sube (resonancia de Minnaert) y se apaga."""
    return sine_sweep(n, f0, f0 * rise, tau * 2.0) * env_ad(n, attack_s, tau)
