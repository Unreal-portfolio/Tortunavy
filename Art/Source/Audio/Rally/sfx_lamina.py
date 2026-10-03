"""Lámina de revisión: forma de onda y espectrograma de cada SFX."""
from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from scipy import signal  # noqa: E402

from sfx_dsp import SR  # noqa: E402

COLUMNS = 2
ROW_HEIGHT_IN = 1.75
SPEC_MAX_HZ = 12000.0
SPEC_FLOOR_DB = -90.0


def _title(meta: dict) -> str:
    loop = "  [BUCLE]" if meta["loop"] else ""
    return (f"{meta['name']}  {meta['duration_s']:.2f} s  {meta['lufs']:.1f} LUFS  "
            f"pico {meta['peak_dbfs']:.1f} dBFS{loop}")


def _plot_wave(ax, x: np.ndarray, meta: dict) -> None:
    t = np.arange(len(x)) / SR
    ax.plot(t, x, lw=0.4, color="#1f6f8b")
    ax.set_xlim(0, t[-1])
    ax.set_ylim(-1, 1)
    ax.axhline(-0.891, color="#c0392b", lw=0.4, ls=":")
    ax.axhline(0.891, color="#c0392b", lw=0.4, ls=":")
    ax.set_title(_title(meta), fontsize=7, loc="left")
    ax.tick_params(labelsize=6)


def _plot_spec(ax, x: np.ndarray) -> None:
    freqs, times, power = signal.spectrogram(x, SR, nperseg=1024, noverlap=896, window="hann")
    db = 10.0 * np.log10(power / max(power.max(), 1e-20) + 1e-12)
    ax.pcolormesh(times, freqs, np.maximum(db, SPEC_FLOOR_DB), shading="auto", cmap="magma",
                  vmin=SPEC_FLOOR_DB, vmax=0.0)
    ax.set_ylim(0, SPEC_MAX_HZ)
    ax.set_yticks([0, 2000, 4000, 8000, 12000])
    ax.set_yticklabels(["0", "2k", "4k", "8k", "12k"])
    ax.tick_params(labelsize=6)


def render_lamina(items: list[tuple[np.ndarray, dict]], path: Path) -> None:
    rows = (len(items) + COLUMNS - 1) // COLUMNS
    fig, axes = plt.subplots(rows, COLUMNS * 2, figsize=(22, rows * ROW_HEIGHT_IN),
                             gridspec_kw={"width_ratios": [1.0, 1.2] * COLUMNS})
    axes = np.atleast_2d(axes)
    for ax in axes.flat:
        ax.set_visible(False)
    for i, (x, meta) in enumerate(items):
        row, col = divmod(i, COLUMNS)
        wave_ax, spec_ax = axes[row, col * 2], axes[row, col * 2 + 1]
        wave_ax.set_visible(True)
        spec_ax.set_visible(True)
        _plot_wave(wave_ax, x, meta)
        _plot_spec(spec_ax, x)
    fig.suptitle("Tortunabo · SFX del modo Rally (síntesis procedural) · onda (línea roja = -1 dBFS) "
                 "y espectrograma 0-12 kHz", fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.985))
    fig.savefig(path, dpi=110)
    plt.close(fig)
