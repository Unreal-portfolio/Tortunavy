"""Masterizado de los SFX: sonoridad (BS.1770 con pyloudnorm), limitador, costura de bucles y WAV."""
from __future__ import annotations

from pathlib import Path

import numpy as np
import pyloudnorm as pyln
from scipy.io import wavfile
from scipy.ndimage import minimum_filter1d, uniform_filter1d

from sfx_dsp import SR, highpass, n_of

CEILING_DBFS = -1.1          # margen sobre -1 dBFS para el redondeo a 16 bits
MAX_LIMIT_DB = 10.0          # reducción máxima que se acepta para llegar a la sonoridad
LIMITER_HALF_S = 0.008
LUFS_MIN_WINDOW_S = 0.41     # BS.1770 necesita al menos un bloque de 400 ms
PCM_SCALE = 32767.0


def db_to_lin(db: float) -> float:
    return float(10.0 ** (db / 20.0))


def peak_dbfs(x: np.ndarray) -> float:
    return float(20.0 * np.log10(max(np.max(np.abs(x)), 1e-12)))


def measure_lufs(x: np.ndarray) -> float:
    """Sonoridad integrada; los sonidos de menos de 400 ms se rellenan con silencio."""
    min_n = n_of(LUFS_MIN_WINDOW_S)
    padded = np.concatenate((x, np.zeros(max(0, min_n - len(x)))))
    return float(pyln.Meter(SR).integrated_loudness(padded))


def limit(x: np.ndarray, ceiling: float, circular: bool) -> np.ndarray:
    """Limitador sin latencia (offline): mínimo móvil y doble media móvil.

    La media de anchura total <= ventana del mínimo garantiza que la ganancia
    suavizada nunca supera la necesaria en ninguna muestra.
    """
    mode = "wrap" if circular else "nearest"
    half = n_of(LIMITER_HALF_S)
    needed = np.minimum(1.0, ceiling / np.maximum(np.abs(x), 1e-12))
    gain = minimum_filter1d(needed, size=2 * half + 1, mode=mode)
    gain = uniform_filter1d(gain, size=half + 1, mode=mode)
    gain = uniform_filter1d(gain, size=half + 1, mode=mode)
    return np.clip(x * gain, -ceiling, ceiling)


def remove_dc(x: np.ndarray, circular: bool) -> np.ndarray:
    return x - np.mean(x) if circular else highpass(x, 18.0)


def master(x: np.ndarray, target_lufs: float, circular: bool,
           fixed_gain: float | None = None) -> tuple[np.ndarray, float]:
    """Lleva x a target_lufs con pico <= CEILING_DBFS. Devuelve (señal, ganancia)."""
    ceiling = db_to_lin(CEILING_DBFS)
    clean = remove_dc(x, circular)
    if fixed_gain is not None:
        return limit(clean * fixed_gain, ceiling, circular), fixed_gain
    max_gain = ceiling * db_to_lin(MAX_LIMIT_DB) / float(np.max(np.abs(clean)))
    gain = min(db_to_lin(target_lufs - measure_lufs(clean)), max_gain)
    for _ in range(10):
        diff = target_lufs - measure_lufs(limit(clean * gain, ceiling, circular))
        if abs(diff) < 0.1 or (diff > 0 and gain >= max_gain * 0.999):
            break
        gain = min(gain * db_to_lin(diff), max_gain)
    return limit(clean * gain, ceiling, circular), gain


def rotate_to_seam(x: np.ndarray) -> np.ndarray:
    """Rota un bucle periódico para que empiece en un cruce por cero ascendente
    con la misma pendiente a ambos lados de la costura."""
    cur = x
    prev = np.roll(x, 1)
    prev2 = np.roll(x, 2)
    nxt = np.roll(x, -1)
    slope_start = nxt - cur
    slope_end = prev - prev2
    cost = np.abs(cur) + np.abs(prev) + 4.0 * np.abs(slope_start - slope_end)
    cost = cost + np.where((prev <= 0.0) & (cur >= 0.0), 0.0, 1.0)
    return np.roll(x, -int(np.argmin(cost)))


def to_pcm16(x: np.ndarray) -> np.ndarray:
    return np.clip(np.round(x * PCM_SCALE), -32768, 32767).astype(np.int16)


def write_wav(path: Path, x: np.ndarray) -> np.ndarray:
    """Escribe WAV mono 16 bit y devuelve lo escrito en float para medirlo."""
    pcm = to_pcm16(x)
    wavfile.write(str(path), SR, pcm)
    return pcm.astype(np.float64) / PCM_SCALE
