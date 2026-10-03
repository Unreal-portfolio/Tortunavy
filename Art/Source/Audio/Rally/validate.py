"""Valida los WAV del modo Rally contra la especificación del encargo.

Comprueba por fichero: formato (mono, 44,1 kHz, 16 bit), duración (±5 %),
pico <= -1 dBFS y, en los bucles, costura limpia (|primera - última| < 0,01,
arranque en cruce por cero y misma pendiente a ambos lados). También verifica
que manifest.json lista cada fichero. La sonoridad se informa (AVISO si se
desvía más de 3 LU del objetivo) pero no hace fallar la validación.

Uso (desde la raíz del repo):
    uv run --with numpy --with pyloudnorm python Art/Source/Audio/Rally/validate.py
Sale con código 1 si algún fichero falla.
"""
from __future__ import annotations

import json
import sys
import wave
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
SAMPLE_RATE = 44100
PEAK_MAX_DBFS = -1.0
DURATION_TOL = 0.05
SEAM_TOL = 0.01
LUFS_WARN_LU = 3.0

# nombre: (duración s, bucle, LUFS objetivo o None si va enlazado a otro)
SPEC = {
    "SFX_Buggy_Engine_Idle": (2.0, True, -16.0),
    "SFX_Buggy_Engine_Mid": (2.0, True, -16.0),
    "SFX_Buggy_Engine_High": (2.0, True, -16.0),
    "SFX_Buggy_Skid_Loop": (2.0, True, -16.0),
    "SFX_Buggy_Turbo_Start": (0.6, False, None),
    "SFX_Buggy_Turbo_Loop": (1.5, True, -16.0),
    "SFX_Buggy_Land": (0.5, False, -14.0),
    "SFX_Buggy_Crash": (0.7, False, -14.0),
    "SFX_Buggy_Explode": (1.2, False, -14.0),
    "SFX_Turret_Coco": (0.4, False, -14.0),
    "SFX_Turret_Alga": (0.4, False, -14.0),
    "SFX_Turret_Burbuja": (0.4, False, -14.0),
    "SFX_Turret_Mortero": (0.6, False, -14.0),
    "SFX_Turret_Tinta": (0.45, False, -14.0),
    "SFX_Turret_Ancla": (0.6, False, -14.0),
    "SFX_Turret_Overheat": (0.6, False, -14.0),
    "SFX_Impact_Coco": (0.5, False, -14.0),
    "SFX_Impact_Splash": (0.5, False, -14.0),
    "SFX_Impact_Bubble_Pop": (0.3, False, -14.0),
    "SFX_Rally_Light_Beep": (0.35, False, -16.0),
    "SFX_Rally_Light_Go": (0.8, False, -16.0),
    "SFX_Rally_Call_Beep": (0.09, False, -16.0),
    "SFX_Rally_Call_Crest": (0.3, False, -16.0),
    "SFX_Rally_Call_Jump": (0.3, False, -16.0),
    "SFX_Rally_Call_Water": (0.3, False, -16.0),
}
TURRET_RANGE_S = (0.3, 0.6)


def read_wav(path: Path) -> tuple[tuple[int, int, int], np.ndarray]:
    with wave.open(str(path), "rb") as wav:
        fmt = (wav.getnchannels(), wav.getframerate(), wav.getsampwidth())
        frames = wav.readframes(wav.getnframes())
    if fmt[2] != 2:
        return fmt, np.zeros(0)
    return fmt, np.frombuffer(frames, dtype="<i2").astype(np.float64) / 32767.0


def measure_lufs(x: np.ndarray) -> float | None:
    try:
        import pyloudnorm as pyln
    except ImportError:
        return None
    min_n = int(round(0.41 * SAMPLE_RATE))
    padded = np.concatenate((x, np.zeros(max(0, min_n - len(x)))))
    return float(pyln.Meter(SAMPLE_RATE).integrated_loudness(padded))


def seam_errors(x: np.ndarray) -> list[str]:
    errors = []
    jump = abs(x[0] - x[-1])
    if jump >= SEAM_TOL:
        errors.append(f"salto en la costura {jump:.4f}")
    if abs(x[0]) >= SEAM_TOL:
        errors.append(f"no empieza en cruce por cero ({x[0]:.4f})")
    slope_start, slope_end = x[1] - x[0], x[-1] - x[-2]
    if abs(slope_start - slope_end) >= SEAM_TOL or (slope_start * slope_end < 0 and abs(slope_start - slope_end) > 1e-3):
        errors.append(f"pendiente distinta ({slope_start:.4f} / {slope_end:.4f})")
    return errors


def check(name: str, spec: tuple, listed: set[str]) -> tuple[list[str], str]:
    duration, is_loop, target = spec
    path = HERE / "wav" / f"{name}.wav"
    if not path.is_file():
        return [f"no existe {path.name}"], ""
    fmt, x = read_wav(path)
    errors = []
    if fmt != (1, SAMPLE_RATE, 2):
        errors.append(f"formato {fmt} (se espera mono, {SAMPLE_RATE} Hz, 16 bit)")
        return errors, ""
    real = len(x) / SAMPLE_RATE
    if abs(real - duration) > DURATION_TOL * duration:
        errors.append(f"duración {real:.3f} s (spec {duration} s ±5 %)")
    if name.startswith("SFX_Turret_") and not TURRET_RANGE_S[0] <= real <= TURRET_RANGE_S[1]:
        errors.append(f"disparo fuera de {TURRET_RANGE_S} s")
    peak = 20.0 * np.log10(max(np.max(np.abs(x)), 1e-12))
    if peak > PEAK_MAX_DBFS:
        errors.append(f"pico {peak:.2f} dBFS")
    if is_loop:
        errors.extend(seam_errors(x))
    if name not in listed:
        errors.append("no está en manifest.json")
    lufs = measure_lufs(x)
    info = f"{real:.2f} s  pico {peak:6.2f} dBFS"
    if lufs is not None:
        info += f"  {lufs:6.1f} LUFS"
        if target is not None and abs(lufs - target) > LUFS_WARN_LU:
            info += f"  AVISO: objetivo {target}"
    return errors, info


def main() -> int:
    manifest_path = HERE / "manifest.json"
    listed = set()
    if manifest_path.is_file():
        listed = {s["name"] for s in json.loads(manifest_path.read_text(encoding="utf-8"))["sounds"]}
    failures = 0
    for name, spec in SPEC.items():
        errors, info = check(name, spec, listed)
        failures += bool(errors)
        status = "FALLO" if errors else "OK"
        print(f"{status:<5} {name:<26} {info}{'  ' + '; '.join(errors) if errors else ''}")
    extra = sorted(p.stem for p in (HERE / "wav").glob("*.wav") if p.stem not in SPEC)
    for name in extra:
        print(f"AVISO {name}: WAV fuera de la especificación")
    print(f"\n{len(SPEC) - failures}/{len(SPEC)} OK")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
