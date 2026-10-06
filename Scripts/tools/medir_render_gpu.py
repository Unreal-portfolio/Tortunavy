"""Medida de render y GPU con ventana sobre las fases de TN.Stress (issue #58).

Lanza el juego con ventana, captura un CSV del CsvProfiler (FrameTime, GameThreadTime, RenderThreadTime,
RHIThreadTime, GPUTime, RHI/DrawCalls, RHI/PrimitivesDrawn y los pases GPU/*) y lo parte por las fases que
escribe el subsistema de estrés en el log. Saca media, p95 y máximo por fase.

Por qué así:
- `-csvCaptureFrames` arranca la captura antes de iniciar el RHI y el motor se cae (IsRayTracingAllowed);
  por eso se arranca con `-ExecCmds` en el fotograma 1.
- Al salir con `-TNQuitWhenDone` el CSV queda truncado (el motor no cierra la captura): se usa
  `csvprofile frames=N` + `csvprofile exitoncompletion`, y la sesión de estrés termina antes que la captura.
- Fila del CSV = fotograma del motor - fotograma de inicio de la captura. El log da el fotograma módulo 1000
  y la hora; con la suma de FrameTime se elige la fila exacta.

Uso:
    uv run python Scripts/tools/medir_render_gpu.py run --build development --scenario heavy --frames 30000 --out <dir>
    uv run python Scripts/tools/medir_render_gpu.py analyze --csv <fichero.csv> --log <fichero.log> [--json salida.json]
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UPROJECT = ROOT / "Tortunabo.uproject"
EDITOR_EXE = Path(r"C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Win64-DebugGame-Cmd.exe")
PACKAGED_ROOT = ROOT / "Saved" / "Packages" / "dev-2026-10-02" / "Windows"
PACKAGED_EXE = PACKAGED_ROOT / "Tortunabo.exe"
MAP = "/Game/Maps/Run/LVL_Demo01"
SETTLE_SECONDS = 1.5  # igual que TNStressDetail::SettleSeconds: se descarta el arranque de cada fase
TIMED = ["FrameTime", "GameThreadTime", "RenderThreadTime", "RHIThreadTime", "GPUTime"]
COUNTS = ["RHI/DrawCalls", "RHI/PrimitivesDrawn"]
LOG_LINE = re.compile(r"^\[(\d{4})\.(\d{2})\.(\d{2})-(\d{2})\.(\d{2})\.(\d{2}):(\d{3})\]\[\s*(\d+)\](.*)$")
ADAPTER = re.compile(r"Found D3D12 adapter (\d+): ([^(]+)")
CHOSEN = re.compile(r"Chosen D3D12 Adapter Id = (\d+)")
PHASE = re.compile(r"\[Estr\S*\] Fase (\d+)/(\d+) \S(\w+)\S")


def build_command(build: str, scenario: str, seconds: int, warmup: int, frames: int, res: tuple[int, int], log: Path,
                  cvars: str = "") -> list[str]:
    extra = f"{cvars}, " if cvars else ""
    exec_cmds = f"r.VSync 0, t.MaxFPS 0, r.GPUCsvStatsEnabled 1, {extra}csvprofile exitoncompletion, csvprofile frames={frames}"
    # -handleensurepercent=0: el ensure de UVChannelData (StaticMesh.cpp:4936) colgaba la build empaquetada
    # mientras preparaba el informe de fallo.
    common = [MAP, "-windowed", f"-ResX={res[0]}", f"-ResY={res[1]}", "-NoSteam", "-nosplash", "-handleensurepercent=0",
              f"-ExecCmds={exec_cmds}", f"-abslog={log}"]
    if scenario != "none":
        common += [f"-TNStress={scenario}", f"-TNStressSeconds={seconds}", f"-TNStressWarmup={warmup}"]
    if build == "debuggame":
        return [str(EDITOR_EXE), str(UPROJECT), *common[:1], "-game", *common[1:]]
    return [str(PACKAGED_EXE), *common]


def saved_dirs() -> list[Path]:
    return [ROOT / "Saved", PACKAGED_ROOT / "Tortunabo" / "Saved", Path(os.environ.get("LOCALAPPDATA", "")) / "Tortunabo" / "Saved"]


def newest_since(pattern: str, since: float) -> Path | None:
    found = [p for d in saved_dirs() if d.is_dir() for p in d.glob(pattern) if p.stat().st_mtime >= since]
    return max(found, key=lambda p: p.stat().st_mtime) if found else None


def read_csv(path: Path) -> tuple[list[str], list[list[str]]]:
    with path.open(newline="", encoding="utf-8", errors="replace") as handle:
        lines = list(csv.reader(handle))
    header = lines[0]
    # Una captura cerrada repite la cabecera al final ([HasHeaderRowAtEnd]); esa es la completa.
    for line in lines[1:]:
        if line and line[0] == "EVENTS":
            header = line
    rows = [line for line in lines[1:] if line and line[0] != "EVENTS" and not line[0].startswith("[")]
    return header, rows


def column(header: list[str], rows: list[list[str]], name: str) -> list[float]:
    if name not in header:
        return []
    index = header.index(name)
    out = []
    for row in rows:
        try:
            out.append(float(row[index]))
        except (IndexError, ValueError):
            out.append(float("nan"))
    return out


def parse_log(path: Path) -> dict:
    result: dict = {"capture": None, "phases": [], "end": None, "adapter": None, "res": None}
    adapters: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = LOG_LINE.match(raw)
        if not match:
            continue
        y, mo, d, h, mi, s, ms, frame, text = match.groups()
        stamp = datetime(int(y), int(mo), int(d), int(h), int(mi), int(s), int(ms) * 1000, tzinfo=timezone.utc).timestamp()  # el log va en UTC
        event = {"t": stamp, "frame_mod": int(frame)}
        if "Capture started" in text and result["capture"] is None:
            result["capture"] = event
        elif (phase := PHASE.search(text)) is not None:
            result["phases"].append({**event, "index": int(phase.group(1)), "total": int(phase.group(2)), "group": phase.group(3)})
        elif "] Terminado (" in text and "Estr" in text:
            result["end"] = event
        elif (found := ADAPTER.search(text)) is not None:
            adapters[found.group(1)] = found.group(2).strip()
        elif (chosen := CHOSEN.search(text)) is not None:
            result["adapter"] = adapters.get(chosen.group(1), chosen.group(1))
        elif "viewport resized to" in text.lower():
            result["res"] = text.rsplit(" ", 1)[-1]
    return result


def locate_row(event: dict, capture: dict, cumulative: list[float]) -> int:
    """Fila del CSV del evento: la más cercana a su hora cuyo fotograma módulo 1000 coincide."""
    target_ms = (event["t"] - capture["t"]) * 1000.0
    estimate = next((i for i, c in enumerate(cumulative) if c >= target_ms), len(cumulative) - 1)
    candidates = [r for r in range(max(0, estimate - 600), min(len(cumulative), estimate + 600))
                  if (capture["frame_mod"] + r) % 1000 == event["frame_mod"]]
    return min(candidates, key=lambda r: abs(r - estimate)) if candidates else estimate


def percentile(values: list[float], q: float) -> float:
    ordered = sorted(v for v in values if not math.isnan(v))
    if not ordered:
        return float("nan")
    k = (len(ordered) - 1) * q
    lo = int(k)
    hi = min(lo + 1, len(ordered) - 1)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (k - lo)


def summarize(header: list[str], rows: list[list[str]]) -> dict:
    out: dict = {"frames": len(rows)}
    for name in TIMED:
        values = [v for v in column(header, rows, name) if not math.isnan(v)]
        if values:
            out[name] = {"avg": sum(values) / len(values), "p95": percentile(values, 0.95), "max": max(values)}
    for name in COUNTS:
        values = [v for v in column(header, rows, name) if not math.isnan(v)]
        if values:
            out[name] = {"avg": sum(values) / len(values), "max": max(values)}
    passes = []
    for name in header:
        if name.startswith("GPU/"):
            values = [v for v in column(header, rows, name) if not math.isnan(v)]
            if values:
                passes.append((name[4:], sum(values) / len(values)))
    out["gpu_passes_top"] = [{"pass": p, "avg_ms": round(a, 3)} for p, a in sorted(passes, key=lambda x: -x[1])[:8]]
    return out


def analyze(csv_path: Path, log_path: Path) -> dict:
    header, rows = read_csv(csv_path)
    log = parse_log(log_path)
    frame_times = column(header, rows, "FrameTime")
    cumulative, total = [], 0.0
    for value in frame_times:
        total += 0.0 if math.isnan(value) else value
        cumulative.append(total)
    report: dict = {"csv": str(csv_path), "log": str(log_path), "adapter": log["adapter"], "viewport": log["res"], "csv_frames": len(rows), "windows": []}
    if log["capture"] is None or not log["phases"]:
        report["windows"].append({"name": "captura completa", **summarize(header, rows)})
        return report
    starts = [locate_row(p, log["capture"], cumulative) for p in log["phases"]]
    end_row = locate_row(log["end"], log["capture"], cumulative) if log["end"] else len(rows)
    bounds = starts[1:] + [end_row]
    measured: list[list[str]] = []
    for phase, start, stop in zip(log["phases"], starts, bounds):
        settle_ms = cumulative[start] + SETTLE_SECONDS * 1000.0
        first = next((r for r in range(start, stop) if cumulative[r] >= settle_ms), stop)
        window = rows[first:stop]
        measured += window
        report["windows"].append({"name": f"{phase['index']}/{phase['total']} {phase['group']}", "rows": [first, stop], **summarize(header, window)})
    report["windows"].append({"name": "todas las fases", **summarize(header, measured)})
    return report


def markdown(report: dict) -> str:
    lines = ["| Ventana | Fotogramas | Frame media/p95/máx | Game media/p95/máx | Render media/p95/máx | RHI media/p95/máx | GPU media/p95/máx | Draw calls media/máx | Primitivas media/máx |",
             "|---|---|---|---|---|---|---|---|---|"]
    for w in report["windows"]:
        def t(name: str, w: dict = w) -> str:
            v = w.get(name)
            return f"{v['avg']:.1f} / {v['p95']:.1f} / {v['max']:.1f}" if v else "-"

        def c(name: str, w: dict = w) -> str:
            v = w.get(name)
            return f"{v['avg']:,.0f} / {v['max']:,.0f}".replace(",", ".") if v else "-"
        lines.append(f"| {w['name']} | {w['frames']} | {t('FrameTime')} | {t('GameThreadTime')} | {t('RenderThreadTime')} | "
                     f"{t('RHIThreadTime')} | {t('GPUTime')} | {c('RHI/DrawCalls')} | {c('RHI/PrimitivesDrawn')} |")
    return "\n".join(lines)


def run(args: argparse.Namespace) -> int:
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    tag = f"{args.build}_{args.scenario}" + (f"_{args.tag}" if args.tag else "")
    log = out / f"{tag}.log"
    command = build_command(args.build, args.scenario, args.seconds, args.warmup, args.frames, (args.resx, args.resy), log, args.cvars)
    (out / f"{tag}.cmd.txt").write_text(subprocess.list2cmdline(command), encoding="utf-8")
    since = time.time()
    completed = subprocess.run(command, timeout=args.timeout, check=False)
    csv_path = newest_since("Profiling/CSV/*.csv", since)
    stress = newest_since(f"Stress/{args.scenario}_*.json", since)
    if csv_path is None:
        print(f"Sin CSV (código de salida {completed.returncode}).", file=sys.stderr)
        return 1
    report = analyze(csv_path, log)
    report.update({"build": args.build, "scenario": args.scenario, "exit_code": completed.returncode,
                   "stress_report": str(stress) if stress else None, "command": subprocess.list2cmdline(command)})
    (out / f"{tag}.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(markdown(report))
    if stress is None and args.scenario != "none":
        print("Aviso: la sesión de estrés no dejó informe (¿la captura terminó antes?).", file=sys.stderr)
        return 2
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    r = sub.add_parser("run")
    r.add_argument("--build", choices=["debuggame", "development"], required=True)
    r.add_argument("--scenario", default="heavy", help="light|heavy|tortugas8|control|none")
    r.add_argument("--seconds", type=int, default=60)
    r.add_argument("--warmup", type=int, default=10)
    r.add_argument("--frames", type=int, default=20000, help="fotogramas de captura; deben cubrir carga + calentamiento + medida")
    r.add_argument("--resx", type=int, default=1920)
    r.add_argument("--resy", type=int, default=1080)
    r.add_argument("--timeout", type=int, default=900)
    r.add_argument("--cvars", default="", help='órdenes extra antes de la captura, p. ej. "r.ScreenPercentage 50"')
    r.add_argument("--tag", default="", help="sufijo de los ficheros de salida")
    r.add_argument("--out", required=True)
    a = sub.add_parser("analyze")
    a.add_argument("--csv", required=True)
    a.add_argument("--log", required=True)
    a.add_argument("--json")
    args = parser.parse_args()
    if args.mode == "run":
        return run(args)
    report = analyze(Path(args.csv), Path(args.log))
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(markdown(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
