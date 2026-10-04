"""Recetas de síntesis de la torreta, los impactos y el semáforo de salida.

Cada receta recibe un ``np.random.Generator`` con semilla fija y devuelve la
señal cruda (sin nivel final): el masterizado lo hace ``sfx_master``.
"""
from __future__ import annotations

import numpy as np

from sfx_dsp import (
    chirp_drop, env_ad, env_asr, fade_edges, highpass, lowpass, modal, n_of, normalize_peak,
    phase_from_freq, place, sine_sweep, svf, time_axis,
)


def _noise_burst(rng: np.random.Generator, n: int, lo: float | None, hi: float | None,
                 attack_s: float, tau_s: float) -> np.ndarray:
    x = rng.standard_normal(n)
    if hi is not None:
        x = lowpass(x, hi)
    if lo is not None:
        x = highpass(x, lo)
    return normalize_peak(x) * env_ad(n, attack_s, tau_s)


def _tube_pop(n: int, f_hi: float, f_lo: float, tau: float) -> np.ndarray:
    """«Pum» hueco de tubo cerrado: fundamental y armónicos impares que caen de tono."""
    glide = 0.05
    return (sine_sweep(n, f_hi, f_lo, glide) * env_ad(n, 0.0015, tau)
            + 0.3 * sine_sweep(n, 3 * f_hi, 3 * f_lo, glide) * env_ad(n, 0.001, tau * 0.45)
            + 0.12 * sine_sweep(n, 5 * f_hi, 5 * f_lo, glide) * env_ad(n, 0.001, tau * 0.3))


def _droplets(rng: np.random.Generator, n: int, count: int, t_lo: float, t_hi: float,
              f_lo: float, f_hi: float, amp: float) -> np.ndarray:
    out = np.zeros(n)
    m = n_of(0.08)
    for when in np.sort(rng.uniform(t_lo, t_hi, count)):
        decay = np.exp(-(when - t_lo) / max(t_hi - t_lo, 1e-3))
        drop = chirp_drop(m, rng.uniform(f_lo, f_hi), rng.uniform(1.4, 1.9), rng.uniform(0.008, 0.02), 0.001)
        out += amp * rng.uniform(0.5, 1.0) * decay * place(n, drop, when)
    return out


def turret_coco(rng: np.random.Generator) -> np.ndarray:
    n = n_of(0.4)
    click = 0.35 * _noise_burst(rng, n, 2000.0, 6000.0, 0.0003, 0.002)
    air = 0.7 * _noise_burst(rng, n, 500.0, 1400.0, 0.0008, 0.025)
    thump = 0.5 * np.sin(2 * np.pi * 75.0 * time_axis(n)) * env_ad(n, 0.003, 0.05)
    tail = 0.08 * _noise_burst(rng, n, 2500.0, None, 0.01, 0.09)
    return fade_edges(click + air + _tube_pop(n, 270.0, 165.0, 0.06) + thump + tail, 0.0003, 0.03)


def turret_alga(rng: np.random.Generator) -> np.ndarray:
    """Disparo húmedo: pum de tubo más ligero y un «flup» con formantes que bajan."""
    n = n_of(0.4)
    t = time_axis(n)
    sweep = 350.0 + 1050.0 * np.exp(-t / 0.06)
    noise = rng.standard_normal(n)
    squelch = normalize_peak(svf(noise, sweep, 3.0, "bp") + 0.5 * svf(noise, sweep * 2.4, 3.0, "bp"))
    squelch = 0.8 * squelch * env_ad(n, 0.004, 0.07)
    air = 0.4 * _noise_burst(rng, n, 400.0, 1200.0, 0.001, 0.02)
    blop = 0.3 * place(n, chirp_drop(n_of(0.06), 500.0, 2.0, 0.015), 0.06)
    return fade_edges(0.6 * _tube_pop(n, 220.0, 150.0, 0.05) + squelch + air + blop, 0.0003, 0.03)


def turret_burbuja(rng: np.random.Generator) -> np.ndarray:
    n = n_of(0.4)
    main = 0.9 * chirp_drop(n, 320.0, 2.6, 0.07, 0.004)
    puff = 0.3 * _noise_burst(rng, n, None, 800.0, 0.003, 0.03)
    pop = 0.3 * _tube_pop(n, 160.0, 120.0, 0.04)
    small = _droplets(rng, n, 5, 0.07, 0.28, 700.0, 1400.0, 0.38)
    return fade_edges(main + puff + pop + small, 0.0005, 0.03)


def turret_mortero(rng: np.random.Generator) -> np.ndarray:
    n = n_of(0.6)
    pop = _tube_pop(n, 150.0, 72.0, 0.12)
    sub = 0.8 * np.sin(2 * np.pi * 48.0 * time_axis(n)) * env_ad(n, 0.004, 0.15)
    blast = 0.8 * _noise_burst(rng, n, None, 900.0, 0.001, 0.05)
    ring = modal(n, (420.0, 1130.0), (0.22, 0.12), (0.12, 0.06))
    tail = 0.08 * _noise_burst(rng, n, 1500.0, None, 0.02, 0.15)
    return fade_edges(pop + sub + blast + ring + tail, 0.0003, 0.05)


def turret_tinta(rng: np.random.Generator) -> np.ndarray:
    """Chapoteo: chorro con formante que baja, «splort» grave y gotitas."""
    n = n_of(0.45)
    t = time_axis(n)
    noise = rng.standard_normal(n)
    splash = 0.8 * normalize_peak(svf(noise, 700.0 + 1900.0 * np.exp(-t / 0.08), 1.5, "bp")) * env_ad(n, 0.002, 0.08)
    splort = 0.6 * normalize_peak(svf(rng.standard_normal(n), 300.0, 4.0, "bp")) * env_ad(n, 0.004, 0.06)
    drops = _droplets(rng, n, 10, 0.02, 0.3, 1200.0, 2600.0, 0.28)
    return fade_edges(0.5 * _tube_pop(n, 240.0, 160.0, 0.05) + splash + splort + drops, 0.0003, 0.03)


def turret_ancla(rng: np.random.Generator) -> np.ndarray:
    """Golpe de lanzamiento, campanazo del ancla y la cadena que se desenrolla."""
    n = n_of(0.6)
    thunk = 0.8 * sine_sweep(n, 130.0, 80.0, 0.05) * env_ad(n, 0.002, 0.07)
    air = 0.5 * _noise_burst(rng, n, None, 1000.0, 0.001, 0.03)
    clang = modal(n, (520.0, 1260.0, 2110.0, 3020.0), (0.25, 0.18, 0.12, 0.08), (0.25, 0.2, 0.14, 0.08))
    chain = np.zeros(n)
    m = n_of(0.08)
    when = 0.03
    while when < 0.5:
        level = 0.55 - 0.8 * (when - 0.03)
        freqs = (rng.uniform(1800, 2600), rng.uniform(3200, 4200), rng.uniform(5200, 6800))
        link = modal(m, freqs, (0.03, 0.02, 0.012), (0.5, 0.35, 0.2), 0.0003)
        chain += level * rng.uniform(0.6, 1.0) * place(n, link, when)
        if rng.random() < 0.5:
            chain += 0.4 * level * place(n, link, when + rng.uniform(0.004, 0.008))
        when += rng.uniform(0.022, 0.034)
    return fade_edges(thunk + air + clang + chain, 0.0003, 0.04)


def turret_overheat(rng: np.random.Generator) -> np.ndarray:
    """Silbido de vapor con chisporroteo inicial."""
    n = n_of(0.6)
    t = time_axis(n)
    hiss = normalize_peak(highpass(lowpass(rng.standard_normal(n), 10000.0), 2500.0))
    flutter = 1.0 + 0.2 * np.sin(2 * np.pi * 23.0 * t + 1.5 * np.sin(2 * np.pi * 3.0 * t))
    hiss = 0.6 * hiss * flutter * env_ad(n, 0.03, 0.3)
    freq = 3000.0 * (2300.0 / 3000.0) ** (t / 0.6) * (1.0 + 0.015 * np.sin(2 * np.pi * 7.0 * t))
    whistle = 0.15 * np.sin(phase_from_freq(freq)) * env_ad(n, 0.04, 0.35)
    sputter = _droplets(rng, n, 6, 0.0, 0.15, 250.0, 600.0, 0.3)
    return fade_edges(hiss + whistle + sputter, 0.001, 0.05)


def impact_coco(rng: np.random.Generator) -> np.ndarray:
    """Coco contra chapa: golpe de madera hueca y resonancia metálica inarmónica."""
    n = n_of(0.5)
    click = 0.4 * _noise_burst(rng, n, 1500.0, None, 0.0002, 0.003)
    wood = modal(n, (410.0, 960.0, 1620.0), (0.04, 0.025, 0.015), (0.6, 0.35, 0.2))
    metal = modal(n, (612.0, 1497.0, 2384.0, 3271.0, 4405.0, 5630.0),
                  (0.2, 0.15, 0.11, 0.08, 0.06, 0.04), (0.3, 0.28, 0.2, 0.14, 0.09, 0.05))
    thud = 0.3 * np.sin(2 * np.pi * 140.0 * time_axis(n)) * env_ad(n, 0.001, 0.03)
    return fade_edges(click + wood + metal + thud, 0.0002, 0.04)


def impact_splash(rng: np.random.Generator) -> np.ndarray:
    n = n_of(0.5)
    t = time_axis(n)
    splat = svf(rng.standard_normal(n), 450.0 + 1350.0 * np.exp(-t / 0.05), 1.2, "bp")
    splat = 0.9 * normalize_peak(splat) * env_ad(n, 0.002, 0.06)
    plop = 0.6 * sine_sweep(n, 170.0, 90.0, 0.04) * env_ad(n, 0.002, 0.045)
    spray = 0.25 * _noise_burst(rng, n, 3000.0, None, 0.003, 0.05)
    drops = _droplets(rng, n, 12, 0.04, 0.42, 900.0, 2400.0, 0.25)
    return fade_edges(splat + plop + spray + drops, 0.0003, 0.04)


def impact_bubble_pop(rng: np.random.Generator) -> np.ndarray:
    n = n_of(0.3)
    click = 0.6 * _noise_burst(rng, n, 2000.0, None, 0.0002, 0.0015)
    chirp = 0.8 * sine_sweep(n, 700.0, 2000.0, 0.02) * env_ad(n, 0.0008, 0.025)
    ring = 0.2 * np.sin(2 * np.pi * 2600.0 * time_axis(n)) * env_ad(n, 0.001, 0.02)
    puff = 0.3 * _noise_burst(rng, n, None, 1500.0, 0.0005, 0.012)
    return fade_edges(click + chirp + ring + puff, 0.0002, 0.03)


def _beep(n: int, freq: float, release_start_s: float, release_s: float, vibrato: float) -> np.ndarray:
    t = time_axis(n)
    vib = 1.0 + vibrato * np.sin(2 * np.pi * 6.0 * t) * np.clip((t - 0.12) / 0.1, 0.0, 1.0)
    phase = phase_from_freq(freq * vib)
    tone = np.sin(phase) + 0.12 * np.sin(2 * phase) + 0.08 * np.sin(3 * phase)
    return tone * env_asr(n, 0.004, release_start_s, release_s)


def light_beep(rng: np.random.Generator) -> np.ndarray:
    return _beep(n_of(0.35), 880.0, 0.25, 0.09, 0.0)


def light_go(rng: np.random.Generator) -> np.ndarray:
    return _beep(n_of(0.8), 1760.0, 0.62, 0.17, 0.004)
