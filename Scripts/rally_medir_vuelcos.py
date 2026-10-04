"""Vuelcos del piloto IA del Rally en carreras completas sin ventana (#695, criterio 5): lanza LVL_Rally como servidor
dedicado sin render (-server -nullrhi, sin ventana) con un bot, `--laps` vueltas y las cajas «?» del juego (munición
incluida), lee la línea [RallyStats] de cada variante y falla si el bot vuelca más de `--max-vuelcos` veces en alguna
o no termina.

    uv run python Scripts/rally_medir_vuelcos.py                                  # R02 a R06, 3 vueltas, 1 vuelco como mucho
    uv run python Scripts/rally_medir_vuelcos.py --variantes R06_circuito_lomas --paralelo 1

Necesita el editor compilado (TortunaboEditor Win64 DebugGame). Cada variante tarda unos 6-7 min (la carrera va a
tiempo real); con --paralelo se lanzan varias a la vez, cada una con su puerto y su registro en Saved/Logs.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_EDITOR = Path("C:/Program Files/Epic Games/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Win64-DebugGame-Cmd.exe")
VARIANTS = ("R02_circuito_tierra", "R03_circuito_dunas_costeras", "R04_circuito_cantera", "R05_circuito_marismas",
            "R06_circuito_lomas")
STATS = re.compile(r"\[RallyStats\] carrera \d+ variante (?P<variant>\S+): terminados (?P<finished>\d+)/(?P<teams>\d+), "
                   r"atascos (?P<stuck>\d+), vuelcos (?P<flips>\d+)")
BASE_PORT = 7810
RACE_TIMEOUT_S = 900


@dataclass(frozen=True)
class RaceStats:
    variant: str
    finished: int
    teams: int
    stuck: int
    flips: int

    def ok(self, max_flips: int) -> bool:
        return self.flips <= max_flips and self.finished == self.teams


def parse_stats(text: str) -> list[RaceStats]:
    """Líneas [RallyStats] de fin de carrera de un registro."""
    return [RaceStats(m["variant"], int(m["finished"]), int(m["teams"]), int(m["stuck"]), int(m["flips"]))
            for m in STATS.finditer(text)]


def command(editor: Path, uproject: Path, variant: str, laps: int, port: int, log: Path) -> list[str]:
    url = f"/Game/Maps/Rally/LVL_Rally?Variant={variant}?Bots=1?AutoStart?Races=1?Laps={laps}?RaceTimeout={RACE_TIMEOUT_S}"
    return [str(editor), str(uproject), url, "-server", "-nullrhi", "-NoSteam", "-unattended", "-nosound", "-nosplash",
            f"-port={port}", f"-ABSLOG={log}"]


def run_variant(editor: Path, uproject: Path, variant: str, laps: int, port: int, timeout_s: float) -> RaceStats | None:
    log = ROOT / "Saved" / "Logs" / f"RallyVuelcos_{variant}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run(command(editor, uproject, variant, laps, port, log), cwd=ROOT, timeout=timeout_s,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    except subprocess.TimeoutExpired:
        print(f"{variant}: sin terminar en {timeout_s:.0f} s", file=sys.stderr)
    stats = parse_stats(log.read_text(encoding="utf-8", errors="replace")) if log.exists() else []
    return stats[-1] if stats else None


def main() -> int:
    parser = argparse.ArgumentParser(description="Vuelcos del piloto IA del Rally en carreras sin ventana (#695).")
    parser.add_argument("--variantes", nargs="+", default=list(VARIANTS))
    parser.add_argument("--vueltas", type=int, default=3)
    parser.add_argument("--max-vuelcos", type=int, default=1)
    parser.add_argument("--paralelo", type=int, default=1, help="carreras a la vez (cada una en su puerto)")
    parser.add_argument("--editor", type=Path, default=Path(os.environ.get("UE_EDITOR_CMD", DEFAULT_EDITOR)))
    parser.add_argument("--uproject", type=Path, default=ROOT / "Tortunabo.uproject")
    args = parser.parse_args()
    if not args.editor.exists():
        print(f"No está el editor: {args.editor} (UE_EDITOR_CMD o --editor)", file=sys.stderr)
        return 2
    timeout_s = RACE_TIMEOUT_S + 600.0
    with ThreadPoolExecutor(max_workers=max(1, args.paralelo)) as pool:
        futures = {v: pool.submit(run_variant, args.editor, args.uproject, v, args.vueltas, BASE_PORT + i, timeout_s)
                   for i, v in enumerate(args.variantes)}
        results = {v: f.result() for v, f in futures.items()}
    failed = False
    print(f"| Circuito | Terminados | Atascos | Vuelcos ({args.vueltas} vueltas, máx. {args.max_vuelcos}) |")
    print("|---|---|---|---|")
    for variant, stats in results.items():
        if stats is None:
            failed = True
            print(f"| {variant} | sin [RallyStats] | - | - |")
            continue
        failed |= not stats.ok(args.max_vuelcos)
        mark = "" if stats.ok(args.max_vuelcos) else " FALLA"
        print(f"| {variant} | {stats.finished}/{stats.teams} | {stats.stuck} | {stats.flips}{mark} |")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
