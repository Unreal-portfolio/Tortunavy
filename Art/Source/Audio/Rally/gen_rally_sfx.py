"""Genera los SFX del modo Rally de Tortunabo por síntesis procedural.

Sin muestras de terceros: osciladores, ruido filtrado, síntesis modal y
envolventes con semillas fijas, así que volver a ejecutarlo reproduce los
mismos WAV bit a bit.

Salida (junto a este fichero):
    wav/*.wav       mono, 44,1 kHz, 16 bit; bucles con costura en cruce por cero
    manifest.json   duración, bucle, pico, LUFS, uso previsto, licencia
    lamina.png      onda y espectrograma de cada sonido, para revisión

Uso (desde la raíz del repo):
    uv run --with numpy --with scipy --with pyloudnorm --with matplotlib \
        python Art/Source/Audio/Rally/gen_rally_sfx.py
"""
from __future__ import annotations

import json
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import sfx_copilot as cop  # noqa: E402
import sfx_vehicle as veh  # noqa: E402
import sfx_weapons as wpn  # noqa: E402
from sfx_dsp import SR, n_of  # noqa: E402
from sfx_lamina import render_lamina  # noqa: E402
from sfx_master import master, measure_lufs, peak_dbfs, rotate_to_seam, write_wav  # noqa: E402

BASE_SEED = 20261001
LUFS_LOOP = -16.0
LUFS_HIT = -14.0
LUFS_UI = -16.0
UE_ROOT = "/Game/Audio/Rally"
LICENSE = "Original de Tortunabo: síntesis procedural con gen_rally_sfx.py, sin muestras de terceros"

Recipe = Callable[[np.random.Generator], np.ndarray]


@dataclass(frozen=True)
class SoundSpec:
    name: str
    duration_s: float
    is_loop: bool
    target_lufs: float
    usage: str
    recipe: Recipe
    gain_link: str | None = None
    extra: dict = field(default_factory=dict)


def _engine(key: str) -> Recipe:
    return lambda rng: veh.engine_loop(veh.ENGINE_LAYERS[key], rng)


def _engine_spec(name: str, key: str, usage: str) -> SoundSpec:
    rpm = veh.ENGINE_LAYERS[key].rpm
    return SoundSpec(name, veh.ENGINE_LOOP_S, True, LUFS_LOOP, usage, _engine(key),
                     extra={"rpm_base": rpm, "nota": "pitch en UE = rpm_actual / rpm_base"})


SPECS: tuple[SoundSpec, ...] = (
    _engine_spec("SFX_Buggy_Engine_Idle", "idle", "Motor al ralentí; capa baja del mezclador por RPM."),
    _engine_spec("SFX_Buggy_Engine_Mid", "mid", "Motor a medio gas; capa media del mezclador por RPM."),
    _engine_spec("SFX_Buggy_Engine_High", "high", "Motor a fondo; capa alta del mezclador por RPM."),
    SoundSpec("SFX_Buggy_Skid_Loop", 2.0, True, LUFS_LOOP,
              "Derrape sobre arena; volumen por deslizamiento lateral.", veh.skid_loop),
    SoundSpec("SFX_Buggy_Turbo_Loop", 1.5, True, LUFS_LOOP,
              "Turbo activo, en bucle tras Turbo_Start.", veh.turbo_loop),
    SoundSpec("SFX_Buggy_Turbo_Start", 0.6, False, LUFS_LOOP,
              "Arranque del turbo; termina al nivel de Turbo_Loop (ganancia enlazada).",
              veh.turbo_start, gain_link="SFX_Buggy_Turbo_Loop"),
    SoundSpec("SFX_Buggy_Land", 0.5, False, LUFS_HIT, "Aterrizaje tras un salto (suspensión).", veh.land),
    SoundSpec("SFX_Buggy_Crash", 0.7, False, LUFS_HIT, "Choque de la carrocería contra obstáculos o buggies.", veh.crash),
    SoundSpec("SFX_Buggy_Explode", 1.2, False, LUFS_HIT, "Buggy destruido: puf de arena y muelles.", veh.explode),
    SoundSpec("SFX_Turret_Coco", 0.4, False, LUFS_HIT, "Disparo de coco (tubo neumático).", wpn.turret_coco),
    SoundSpec("SFX_Turret_Alga", 0.4, False, LUFS_HIT, "Disparo de alga.", wpn.turret_alga),
    SoundSpec("SFX_Turret_Burbuja", 0.4, False, LUFS_HIT, "Disparo de burbuja.", wpn.turret_burbuja),
    SoundSpec("SFX_Turret_Mortero", 0.6, False, LUFS_HIT, "Disparo de mortero (más grave).", wpn.turret_mortero),
    SoundSpec("SFX_Turret_Tinta", 0.45, False, LUFS_HIT, "Disparo de tinta (chapoteo).", wpn.turret_tinta),
    SoundSpec("SFX_Turret_Ancla", 0.6, False, LUFS_HIT, "Disparo del ancla con cadena.", wpn.turret_ancla),
    SoundSpec("SFX_Turret_Overheat", 0.6, False, LUFS_HIT, "Torreta sobrecalentada (vapor).", wpn.turret_overheat),
    SoundSpec("SFX_Impact_Coco", 0.5, False, LUFS_HIT, "Impacto de coco contra un buggy.", wpn.impact_coco),
    SoundSpec("SFX_Impact_Splash", 0.5, False, LUFS_HIT, "Impacto de alga o tinta.", wpn.impact_splash),
    SoundSpec("SFX_Impact_Bubble_Pop", 0.3, False, LUFS_HIT, "Burbuja que revienta.", wpn.impact_bubble_pop),
    SoundSpec("SFX_Rally_Light_Beep", 0.35, False, LUFS_UI, "Semáforo de salida: luces 3, 2, 1.", wpn.light_beep),
    SoundSpec("SFX_Rally_Light_Go", 0.8, False, LUFS_UI, "Semáforo de salida: luz verde.", wpn.light_go),
    SoundSpec("SFX_Rally_Call_Beep", cop.BEEP_S, False, LUFS_UI,
              "Copiloto: pitido de curva, al lado de la curva y tantas veces como el grado.", cop.call_beep),
    SoundSpec("SFX_Rally_Call_Crest", cop.CUE_S, False, LUFS_UI, "Copiloto: cresta (cambio de rasante).", cop.call_crest),
    SoundSpec("SFX_Rally_Call_Jump", cop.CUE_S, False, LUFS_UI, "Copiloto: salto.", cop.call_jump),
    SoundSpec("SFX_Rally_Call_Water", cop.CUE_S, False, LUFS_UI, "Copiloto: agua (vadeo).", cop.call_water),
)


def seed_for(name: str) -> int:
    return BASE_SEED ^ zlib.crc32(name.encode("utf-8"))


def render(spec: SoundSpec, gains: dict[str, float]) -> tuple[np.ndarray, float]:
    raw = spec.recipe(np.random.default_rng(seed_for(spec.name)))
    if len(raw) != n_of(spec.duration_s):
        raise ValueError(f"{spec.name}: {len(raw)} muestras, se esperaban {n_of(spec.duration_s)}")
    fixed = gains[spec.gain_link] if spec.gain_link else None
    out, gain = master(raw, spec.target_lufs, spec.is_loop, fixed_gain=fixed)
    return (rotate_to_seam(out) if spec.is_loop else out), gain


def entry_for(spec: SoundSpec, written: np.ndarray) -> dict:
    target = f"enlazado a {spec.gain_link}" if spec.gain_link else spec.target_lufs
    return {
        "name": spec.name,
        "file": f"wav/{spec.name}.wav",
        "ue_asset": f"{UE_ROOT}/{spec.name}",
        "duration_s": round(len(written) / SR, 4),
        "loop": spec.is_loop,
        "peak_dbfs": round(peak_dbfs(written), 2),
        "lufs": round(measure_lufs(written), 2),
        "lufs_target": target,
        "uso": spec.usage,
        "seed": seed_for(spec.name),
        **spec.extra,
    }


def main() -> int:
    wav_dir = HERE / "wav"
    wav_dir.mkdir(parents=True, exist_ok=True)
    gains: dict[str, float] = {}
    entries, items = [], []
    for spec in SPECS:
        signal_out, gains[spec.name] = render(spec, gains)
        written = write_wav(wav_dir / f"{spec.name}.wav", signal_out)
        entry = entry_for(spec, written)
        entries.append(entry)
        items.append((written, entry))
        print(f"{spec.name:<26} {entry['duration_s']:5.2f} s  {entry['lufs']:6.1f} LUFS  "
              f"pico {entry['peak_dbfs']:5.1f} dBFS{'  bucle' if spec.is_loop else ''}")
    manifest = {
        "generator": "Art/Source/Audio/Rally/gen_rally_sfx.py",
        "command": "uv run --with numpy --with scipy --with pyloudnorm --with matplotlib "
                   "python Art/Source/Audio/Rally/gen_rally_sfx.py",
        "license": LICENSE,
        "source_url": None,
        "format": {"channels": 1, "sample_rate": SR, "bit_depth": 16},
        "loudness": "ITU-R BS.1770 integrada (pyloudnorm); los sonidos < 400 ms se miden con relleno de silencio",
        "ue_import": "Scripts/tools/import_rally_audio.py",
        "sounds": entries,
    }
    (HERE / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    render_lamina(items, HERE / "lamina.png")
    print(f"{len(entries)} WAV en {wav_dir}; manifest.json y lamina.png actualizados")
    return 0


if __name__ == "__main__":
    sys.exit(main())
