"""Lanza un servidor y hasta 7 clientes locales del juego (-game) por IpNetDriver, con emulación de red.

Todas las instancias usan IpNetDriver con la misma configuración que SteamSocketsNetDriver (Config/DefaultEngine.ini,
[/Script/OnlineSubsystemUtils.IpNetDriver]) y el subsistema en línea NULL, así que se mide lo mismo que con Steam sin
necesitar 8 cuentas. La emulación (PktLag, PktLagVariance, PktLoss) va en los clientes y, con --emular-servidor, también
en el servidor. Cada instancia escribe su registro en <carpeta>/<nombre>.log. Documentación: Docs/Pruebas_Red_Local.md.

Uso (desde la raíz del repositorio, con el editor DebugGame compilado y cerrado):
    uv run python Scripts/tools/red_local.py --clientes 7 --lag 150 --varianza 30 --perdida 2
    uv run python Scripts/tools/red_local.py --clientes 3 --render ninguno --monkey 60:9 --estres heavy --esperar 150
    uv run python Scripts/tools/red_local.py --clientes 7 --mostrar        # solo enseña las órdenes

Con --esperar SEG espera a que acaben las instancias (o las cierra al pasar SEG), resume en pantalla quién se ha conectado,
los informes del monkey (jugadores y correcciones de red) y el de estrés (KB/s por conexión), y lo guarda en resumen.json.
Sale con código 1 si algún cliente no se ha conectado o un informe del monkey ha fallado.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import socket
import subprocess
import sys
import time
from dataclasses import dataclass, field, replace
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "Tortunabo.uproject"
DEFAULT_ENGINE = Path(os.environ.get("UE_ROOT", r"C:\Program Files\Epic Games\UE_5.6"))
# El editor en DebugGame: el de Development carga una DLL vieja del juego (Docs/Comandos_Prueba.md).
EDITOR_EXE = Path("Engine/Binaries/Win64/UnrealEditor-Win64-DebugGame.exe")
DEFAULT_MAP = "/Game/Maps/Run/LVL_BeachRace"
DEFAULT_PORT = 7777
MAX_CLIENTS = 7
RENDER_MODES = ("ninguno", "servidor", "todos")
STRESS_SCENARIOS = ("light", "heavy", "race8", "control")

# IpNetDriver en vez de SteamSockets y subsistema NULL: varias instancias en un mismo PC sin Steam.
NET_ARGS = (
    "-NetDriverOverrides=/Script/OnlineSubsystemUtils.IpNetDriver",
    "-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=Null",
    "-NoSteam",
)
COMMON_ARGS = ("-game", "-log", "-unattended", "-nosplash")

# Líneas del registro del motor y del juego que resume --esperar.
RE_LISTENING = re.compile(r"IpNetDriver listening on port (\d+)")
RE_JOIN = re.compile(r"Join succeeded: (.+)")
RE_WELCOMED = re.compile(r"Welcomed by server \(Level: ([^,]+)")
RE_MONKEY_DONE = re.compile(r"\[Monkey\] Sesión terminada \(([^)]*)\): (.+?)\. Informe (?:guardado|NO guardado): (.+)$")
RE_STRESS_DONE = re.compile(r"\[Estrés\] Terminado \(([^)]*)\)\. Informe (?:guardado|NO guardado): (.+)$", re.MULTILINE)
RE_BUG_REPORT = re.compile(r"\[Informe\] [^:]+: informe de bug en (.+?) \(")
RE_TRAVEL_FAIL = re.compile(r"(TravelFailure|NetworkFailure|ConnectionTimeout|PendingConnectionFailure)")


@dataclass(frozen=True)
class Emulation:
    """Emulación de red del motor (FPacketSimulationSettings) para una instancia."""

    lag_ms: int = 0
    variance_ms: int = 0
    loss_pct: int = 0

    def validate(self) -> None:
        if not 0 <= self.lag_ms <= 2000:
            raise ValueError(f"--lag fuera de rango (0-2000 ms): {self.lag_ms}")
        if not 0 <= self.variance_ms <= 1000:
            raise ValueError(f"--varianza fuera de rango (0-1000 ms): {self.variance_ms}")
        if not 0 <= self.loss_pct <= 100:
            raise ValueError(f"--perdida fuera de rango (0-100 %): {self.loss_pct}")

    def args(self) -> list[str]:
        out = []
        if self.lag_ms:
            out.append(f"-PktLag={self.lag_ms}")
        if self.variance_ms:
            out.append(f"-PktLagVariance={self.variance_ms}")
        if self.loss_pct:
            out.append(f"-PktLoss={self.loss_pct}")
        return out


@dataclass(frozen=True)
class Plan:
    """Todo lo que hace falta para construir las órdenes de las instancias."""

    engine: Path = DEFAULT_ENGINE
    project: Path = PROJECT
    map: str = DEFAULT_MAP
    port: int = DEFAULT_PORT
    clients: int = MAX_CLIENTS
    render: str = "servidor"
    emulation: Emulation = field(default_factory=Emulation)
    emulate_server: bool = False
    monkey: str | None = None
    monkey_clients: int | None = None
    monkey_server: bool = False
    monkey_warmup: float = 10.0
    stress: str | None = None
    stress_seconds: int = 60
    bug_report_after: float | None = None
    quit_when_done: bool = False
    log_dir: Path = ROOT / "Saved" / "RedLocal"
    extra_server: tuple[str, ...] = ()
    extra_client: tuple[str, ...] = ()

    def validate(self) -> None:
        if not 0 <= self.clients <= MAX_CLIENTS:
            raise ValueError(f"--clientes entre 0 y {MAX_CLIENTS} (8 instancias con el servidor): {self.clients}")
        if self.render not in RENDER_MODES:
            raise ValueError(f"--render debe ser uno de {RENDER_MODES}: {self.render}")
        if not 1024 <= self.port <= 65535:
            raise ValueError(f"--puerto fuera de rango: {self.port}")
        if self.monkey is not None:
            parse_monkey_spec(self.monkey)
        if self.monkey_clients is not None and not 0 <= self.monkey_clients <= self.clients:
            raise ValueError(f"--monkey-clientes entre 0 y --clientes ({self.clients}): {self.monkey_clients}")
        if self.stress is not None and self.stress not in STRESS_SCENARIOS:
            raise ValueError(f"--estres debe ser uno de {STRESS_SCENARIOS}: {self.stress}")
        self.emulation.validate()

    @property
    def executable(self) -> Path:
        return self.engine / EDITOR_EXE


def parse_monkey_spec(spec: str) -> tuple[float, int]:
    """«segundos:semilla» (como -TNMonkey) → (segundos, semilla)."""
    seconds_text, _, seed_text = spec.partition(":")
    try:
        seconds = float(seconds_text)
        seed = int(seed_text) if seed_text else 1
    except ValueError as error:
        raise ValueError(f"--monkey debe ser segundos:semilla (p. ej. 60:9): {spec}") from error
    if not 1 <= seconds <= 3600:
        raise ValueError(f"--monkey: segundos entre 1 y 3600: {spec}")
    return seconds, seed


def _render_args(rendered: bool, slot: int) -> list[str]:
    if not rendered:
        return ["-nullrhi", "-nosound"]
    # Ventanas pequeñas en cascada para que se vean todas.
    return ["-windowed", "-ResX=640", "-ResY=360", f"-WinX={40 + slot * 60}", f"-WinY={40 + slot * 60}"]


def _monkey_args(plan: Plan, seed_offset: int, net_filter: str, out_name: str) -> list[str]:
    seconds, seed = parse_monkey_spec(plan.monkey or "")
    out = [
        f"-TNMonkey={seconds:g}:{seed + seed_offset}",
        f"-TNMonkeyNet={net_filter}",
        f"-TNMonkeyWarmup={plan.monkey_warmup:g}",
        f"-TNMonkeyOut={(plan.log_dir / out_name).as_posix()}",
    ]
    return out


def server_args(plan: Plan) -> list[str]:
    """Argumentos del servidor (listen server con el mapa)."""
    args = [str(plan.project), f"{plan.map}?listen", f"-port={plan.port}", *COMMON_ARGS, *NET_ARGS]
    args += _render_args(plan.render in ("servidor", "todos"), 0)
    args.append(f"-abslog={(plan.log_dir / 'servidor.log').as_posix()}")
    if plan.emulate_server:
        args += plan.emulation.args()
    if plan.monkey and plan.monkey_server:
        args += _monkey_args(plan, 0, "server", "monkey_servidor.json")
    if plan.stress:
        args += [f"-TNStress={plan.stress}", f"-TNStressSeconds={plan.stress_seconds}"]
    if plan.quit_when_done and (plan.stress or (plan.monkey and plan.monkey_server)):
        args.append("-TNQuitWhenDone")
    args += list(plan.extra_server)
    return args


def client_args(plan: Plan, index: int) -> list[str]:
    """Argumentos del cliente index (1..clientes): se conecta a 127.0.0.1:puerto."""
    if not 1 <= index <= plan.clients:
        raise ValueError(f"cliente fuera de rango: {index}")
    args = [str(plan.project), f"127.0.0.1:{plan.port}", *COMMON_ARGS, *NET_ARGS]
    args += _render_args(plan.render == "todos", index)
    args.append(f"-abslog={(plan.log_dir / f'cliente{index}.log').as_posix()}")
    args += plan.emulation.args()
    monkey_clients = plan.clients if plan.monkey_clients is None else plan.monkey_clients
    runs_monkey = plan.monkey is not None and index <= monkey_clients
    if runs_monkey:
        # Semilla distinta por cliente: cada proceso tiene su jugador 0 y, con la misma, harían lo mismo.
        args += _monkey_args(plan, index, "client", f"monkey_cliente{index}.json")
        if plan.quit_when_done:
            args.append("-TNQuitWhenDone")
    if plan.bug_report_after is not None and index == 1:
        args.append(f"-TNBugReportAfter={plan.bug_report_after:g}")
    args += list(plan.extra_client)
    return args


def commands(plan: Plan) -> list[tuple[str, list[str]]]:
    """(nombre, orden completa) de cada instancia, el servidor primero."""
    plan.validate()
    exe = str(plan.executable)
    out = [("servidor", [exe, *server_args(plan)])]
    out += [(f"cliente{index}", [exe, *client_args(plan, index)]) for index in range(1, plan.clients + 1)]
    return out


def summarize_server_log(text: str) -> dict:
    listening = RE_LISTENING.search(text)
    stress = RE_STRESS_DONE.search(text) if "[Estrés]" in text else None
    monkey = _monkey_line(text)
    return {
        "listening_port": int(listening.group(1)) if listening else None,
        "joins": [match.group(1).strip() for match in RE_JOIN.finditer(text)],
        "stress_report": stress.group(2).strip() if stress else None,
        "monkey": monkey,
        "bug_report": _bug_line(text),
    }


def summarize_client_log(text: str) -> dict:
    welcomed = RE_WELCOMED.search(text)
    return {
        "welcomed": welcomed is not None,
        "level": welcomed.group(1).strip() if welcomed else None,
        "net_failures": sorted(set(RE_TRAVEL_FAIL.findall(text))),
        "monkey": _monkey_line(text),
        "bug_report": _bug_line(text),
    }


def _monkey_line(text: str) -> dict | None:
    for line in reversed(text.splitlines()):
        match = RE_MONKEY_DONE.search(line)
        if match:
            return {"reason": match.group(1), "verdict": match.group(2), "report": match.group(3).strip()}
    return None


def _bug_line(text: str) -> str | None:
    match = RE_BUG_REPORT.search(text)
    return match.group(1).strip() if match else None


def read_monkey_report(path: Path) -> dict | None:
    """Lo que interesa del informe JSON del monkey: jugadores, correcciones de red y veredicto."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return {
        "net_mode": data.get("net_mode"),
        "map": data.get("map"),
        "players": data.get("players"),
        "net_corrections": data.get("net_corrections"),
        "passed": data.get("passed"),
        "failures": data.get("failures", []),
    }


def read_stress_report(path: Path) -> dict | None:
    """KB/s de salida por conexión de cada fase del informe de estrés."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    phases = []
    for phase in data.get("phases", []):
        phases.append(
            {
                "group": phase.get("group"),
                "connections": phase.get("connections"),
                "out_kb_s_avg": phase.get("net_out_kb_s_per_connection_avg"),
                "out_kb_s_max": phase.get("net_out_kb_s_per_connection_max"),
            }
        )
    return {"scenario": data.get("scenario"), "phases": phases}


def _read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8-sig", errors="replace")
    except OSError:
        return ""


def find_free_udp_port(start: int, tries: int = 100) -> int:
    """Primer puerto UDP libre desde start: otra copia del juego (otro worktree) puede estar ya en 7777."""
    for port in range(start, min(start + tries, 65536)):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            try:
                probe.bind(("0.0.0.0", port))
            except OSError:
                continue
            return port
    raise ValueError(f"sin puerto UDP libre entre {start} y {start + tries - 1}")


def wait_for_server(log: Path, process: subprocess.Popen, timeout_s: float) -> int | None:
    """Puerto en el que escucha el servidor, o None si no llega a escuchar (o se cierra) en timeout_s.

    Los clientes se lanzan después: si llegan antes, caen al menú por tiempo de conexión. El puerto se lee del registro porque
    el motor salta al siguiente si el pedido está ocupado, y un cliente que fuese al pedido entraría en la partida de otro.
    """
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if process.poll() is not None:
            return None
        match = RE_LISTENING.search(_read(log))
        if match:
            return int(match.group(1))
        time.sleep(1.0)
    return None


def resolve_game_path(path: str, engine: Path) -> Path:
    """Ruta que escribe el juego en su registro: las relativas lo son a Engine/Binaries/Win64 (el directorio del ejecutable)."""
    candidate = Path(path)
    return candidate if candidate.is_absolute() else (engine / EDITOR_EXE.parent / candidate).resolve()


def build_summary(plan: Plan, names: list[str]) -> dict:
    summary: dict = {"date": datetime.now().isoformat(timespec="seconds"), "log_dir": str(plan.log_dir), "instances": {}}
    for name in names:
        text = _read(plan.log_dir / f"{name}.log")
        info = summarize_server_log(text) if name == "servidor" else summarize_client_log(text)
        if info.get("monkey"):
            info["monkey"]["report"] = str(resolve_game_path(info["monkey"]["report"], plan.engine))
            info["monkey"]["data"] = read_monkey_report(Path(info["monkey"]["report"]))
        if info.get("bug_report"):
            info["bug_report"] = str(resolve_game_path(info["bug_report"], plan.engine))
        if name == "servidor" and info.get("stress_report"):
            info["stress_report"] = str(resolve_game_path(info["stress_report"], plan.engine))
            info["stress"] = read_stress_report(Path(info["stress_report"]))
        summary["instances"][name] = info
    return summary


def summary_ok(summary: dict) -> bool:
    for name, info in summary["instances"].items():
        if name != "servidor" and not info.get("welcomed"):
            return False
        data = (info.get("monkey") or {}).get("data")
        if info.get("monkey") and (not data or not data.get("passed")):
            return False
    return True


def print_summary(summary: dict) -> None:
    server = summary["instances"].get("servidor", {})
    print(f"Servidor: escucha en {server.get('listening_port')}, {len(server.get('joins', []))} clientes unidos")
    for name, info in summary["instances"].items():
        if name == "servidor":
            continue
        state = f"conectado a {info['level']}" if info["welcomed"] else f"SIN CONECTAR {info['net_failures']}"
        print(f"  {name}: {state}")
    for name, info in summary["instances"].items():
        monkey = info.get("monkey")
        if monkey:
            data = monkey.get("data") or {}
            print(
                f"  monkey {name}: {monkey['verdict']}; {data.get('net_mode')}, jugadores={data.get('players')}, "
                f"net_corrections={data.get('net_corrections')} ({monkey['report']})"
            )
        if info.get("bug_report"):
            print(f"  informe F8 {name}: {info['bug_report']}")
    stress = server.get("stress")
    if stress:
        for phase in stress["phases"]:
            print(
                f"  estrés {stress['scenario']} · {phase['group']}: {phase['connections']} conexiones, "
                f"{phase['out_kb_s_avg']:.1f} KB/s medio, {phase['out_kb_s_max']:.1f} máx. por conexión"
            )


def launch(plan: Plan, wait_s: float | None, stagger_s: float) -> int:
    if not plan.executable.exists():
        print(f"No está el editor DebugGame: {plan.executable}. Compila TortunaboEditor Win64 DebugGame.", file=sys.stderr)
        return 2
    plan.log_dir.mkdir(parents=True, exist_ok=True)
    cmds = commands(plan)
    processes: list[tuple[str, subprocess.Popen]] = []
    name, cmd = cmds[0]
    processes.append((name, subprocess.Popen(cmd, cwd=ROOT)))
    print(f"servidor: pid {processes[0][1].pid}, registro {plan.log_dir / 'servidor.log'}")
    port = wait_for_server(plan.log_dir / "servidor.log", processes[0][1], 180.0)
    if port is None:
        print("El servidor no ha llegado a escuchar en 180 s (mira servidor.log).", file=sys.stderr)
        processes[0][1].kill()
        return 1
    if port != plan.port:
        print(f"El servidor escucha en {port} (pedido {plan.port}): los clientes van a {port}.")
        plan = replace(plan, port=port)
        cmds = commands(plan)
    for name, cmd in cmds[1:]:
        time.sleep(stagger_s)
        process = subprocess.Popen(cmd, cwd=ROOT)
        processes.append((name, process))
        print(f"{name}: pid {process.pid}, registro {plan.log_dir / (name + '.log')}")
    if wait_s is None:
        print("Instancias en marcha. Ciérralas a mano; los registros quedan en", plan.log_dir)
        return 0

    deadline = time.monotonic() + wait_s
    while time.monotonic() < deadline and any(process.poll() is None for _, process in processes):
        time.sleep(2.0)
    for name, process in processes:
        if process.poll() is None:
            process.terminate()
    for _, process in processes:
        try:
            process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()

    summary = build_summary(plan, [name for name, _ in processes])
    (plan.log_dir / "resumen.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print_summary(summary)
    return 0 if summary_ok(summary) else 1


def parse_args(argv: list[str] | None = None) -> tuple[Plan, argparse.Namespace]:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--clientes", type=int, default=MAX_CLIENTS, help=f"clientes además del servidor (0-{MAX_CLIENTS}; por defecto {MAX_CLIENTS})")
    parser.add_argument("--mapa", default=DEFAULT_MAP, help=f"mapa del servidor (por defecto {DEFAULT_MAP})")
    parser.add_argument("--puerto", type=int, help=f"puerto del servidor (por defecto el primero libre desde {DEFAULT_PORT})")
    parser.add_argument("--render", choices=RENDER_MODES, default="servidor", help="quién dibuja; el resto va con -nullrhi")
    parser.add_argument("--lag", type=int, default=0, help="PktLag en ms (latencia de salida de cada cliente)")
    parser.add_argument("--varianza", type=int, default=0, help="PktLagVariance en ms")
    parser.add_argument("--perdida", type=int, default=0, help="PktLoss en %%")
    parser.add_argument("--emular-servidor", action="store_true", help="aplica también la emulación a la salida del servidor")
    parser.add_argument("--monkey", help="segundos:semilla del monkey en los clientes (arranca al entrar en la partida)")
    parser.add_argument("--monkey-clientes", type=int, help="cuántos clientes llevan monkey (por defecto todos)")
    parser.add_argument("--monkey-servidor", action="store_true", help="el servidor también juega solo")
    parser.add_argument("--monkey-espera", type=float, default=10.0, help="segundos de espera del monkey al entrar (por defecto 10)")
    parser.add_argument("--estres", choices=STRESS_SCENARIOS, help="escenario de TN.Stress en el servidor")
    parser.add_argument("--estres-segundos", type=int, default=60)
    parser.add_argument("--informe-tras", type=float, help="el cliente 1 crea un informe de bug (F8) tras estos segundos")
    parser.add_argument("--salir-al-acabar", action="store_true", help="cada instancia con monkey o estrés se cierra al terminar")
    parser.add_argument("--carpeta", type=Path, help="carpeta de los registros (por defecto Saved/RedLocal/<fecha>)")
    parser.add_argument("--motor", type=Path, default=DEFAULT_ENGINE, help="raíz de UE 5.6 (o variable UE_ROOT)")
    parser.add_argument("--extra-servidor", action="append", default=[], help="argumento más para el servidor (repetible)")
    parser.add_argument("--extra-cliente", action="append", default=[], help="argumento más para cada cliente (repetible)")
    parser.add_argument("--esperar", type=float, help="espera hasta SEG segundos, cierra lo que quede y resume")
    parser.add_argument("--escalonar", type=float, default=2.0, help="segundos entre clientes (por defecto 2)")
    parser.add_argument("--mostrar", action="store_true", help="solo enseña las órdenes, sin lanzar nada")
    args = parser.parse_args(argv)
    log_dir = args.carpeta or ROOT / "Saved" / "RedLocal" / datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    plan = Plan(
        engine=args.motor,
        map=args.mapa,
        port=args.puerto if args.puerto is not None else find_free_udp_port(DEFAULT_PORT),
        clients=args.clientes,
        render=args.render,
        emulation=Emulation(args.lag, args.varianza, args.perdida),
        emulate_server=args.emular_servidor,
        monkey=args.monkey,
        monkey_clients=args.monkey_clientes,
        monkey_server=args.monkey_servidor,
        monkey_warmup=args.monkey_espera,
        stress=args.estres,
        stress_seconds=args.estres_segundos,
        bug_report_after=args.informe_tras,
        quit_when_done=args.salir_al_acabar,
        log_dir=log_dir.resolve(),
        extra_server=tuple(args.extra_servidor),
        extra_client=tuple(args.extra_cliente),
    )
    return plan, args


def main(argv: list[str] | None = None) -> int:
    try:
        plan, args = parse_args(argv)
        plan.validate()
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    if args.mostrar:
        for name, cmd in commands(plan):
            print(f"# {name}\n{subprocess.list2cmdline(cmd)}\n")
        return 0
    return launch(plan, args.esperar, args.escalonar)


if __name__ == "__main__":
    sys.exit(main())
