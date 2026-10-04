"""Lanzador de servidor + clientes locales con emulación de red (Scripts/tools/red_local.py, issue #65)."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Scripts" / "tools"))

from red_local import (  # noqa: E402
    NET_ARGS,
    Emulation,
    Plan,
    client_args,
    commands,
    main,
    parse_args,
    parse_monkey_spec,
    read_monkey_report,
    server_args,
    summarize_client_log,
    summarize_server_log,
    summary_ok,
)

LOGS = Path("C:/tmp/red")


def plan(**overrides) -> Plan:
    base = {"log_dir": LOGS, "engine": Path("C:/UE")}
    base.update(overrides)
    return Plan(**base)


def test_ocho_instancias_por_defecto_con_ipnetdriver():
    cmds = commands(plan())
    assert [name for name, _ in cmds] == ["servidor"] + [f"cliente{i}" for i in range(1, 8)]
    for _, cmd in cmds:
        assert cmd[0].replace("\\", "/").endswith("Engine/Binaries/Win64/UnrealEditor-Win64-DebugGame.exe")
        assert "-game" in cmd
        for net_arg in NET_ARGS:
            assert net_arg in cmd


def test_servidor_listen_con_puerto_y_registro_propio():
    args = server_args(plan(port=7800))
    assert args[1] == "/Game/Maps/Run/LVL_BeachRace?listen"
    assert "-port=7800" in args
    assert f"-abslog={(LOGS / 'servidor.log').as_posix()}" in args


def test_clientes_se_conectan_al_puerto_y_cada_uno_con_su_registro():
    args = client_args(plan(port=7800), 3)
    assert args[1] == "127.0.0.1:7800"
    assert f"-abslog={(LOGS / 'cliente3.log').as_posix()}" in args


def test_emulacion_en_clientes_y_no_en_el_servidor_salvo_que_se_pida():
    emulation = Emulation(150, 30, 2)
    assert emulation.args() == ["-PktLag=150", "-PktLagVariance=30", "-PktLoss=2"]
    assert "-PktLag=150" in client_args(plan(emulation=emulation), 1)
    assert not any(arg.startswith("-Pkt") for arg in server_args(plan(emulation=emulation)))
    assert "-PktLoss=2" in server_args(plan(emulation=emulation, emulate_server=True))
    assert Emulation().args() == []


def test_render_por_defecto_solo_el_servidor():
    p = plan()
    assert "-windowed" in server_args(p) and "-nullrhi" not in server_args(p)
    assert "-nullrhi" in client_args(p, 1)
    headless = plan(render="ninguno")
    assert "-nullrhi" in server_args(headless)
    everyone = plan(render="todos")
    assert "-windowed" in client_args(everyone, 2)


def test_monkey_en_clientes_espera_al_mundo_de_cliente_con_semilla_distinta():
    p = plan(monkey="60:9", quit_when_done=True, monkey_clients=2, clients=3)
    first = client_args(p, 1)
    assert "-TNMonkey=60:10" in first
    assert "-TNMonkeyNet=client" in first
    assert "-TNQuitWhenDone" in first
    assert f"-TNMonkeyOut={(LOGS / 'monkey_cliente1.json').as_posix()}" in first
    assert "-TNMonkey=60:11" in client_args(p, 2)
    third = client_args(p, 3)
    assert not any(arg.startswith("-TNMonkey") for arg in third)
    assert "-TNQuitWhenDone" not in third
    assert not any(arg.startswith("-TNMonkey") for arg in server_args(p))


def test_estres_e_informe_de_bug():
    p = plan(stress="heavy", stress_seconds=90, bug_report_after=20, quit_when_done=True, clients=2)
    server = server_args(p)
    assert "-TNStress=heavy" in server and "-TNStressSeconds=90" in server and "-TNQuitWhenDone" in server
    assert "-TNBugReportAfter=20" in client_args(p, 1)
    assert not any(arg.startswith("-TNBugReportAfter") for arg in client_args(p, 2))


@pytest.mark.parametrize(
    "overrides",
    [
        {"clients": 8},
        {"clients": -1},
        {"render": "algo"},
        {"port": 80},
        {"monkey": "x:1"},
        {"monkey": "0:1"},
        {"monkey": "60:9", "monkey_clients": 9},
        {"stress": "mega"},
        {"emulation": Emulation(lag_ms=-1)},
        {"emulation": Emulation(loss_pct=101)},
    ],
)
def test_validacion_rechaza_valores_fuera_de_rango(overrides):
    with pytest.raises(ValueError):
        plan(**overrides).validate()


def test_parse_monkey_spec():
    assert parse_monkey_spec("60:9") == (60.0, 9)
    assert parse_monkey_spec("30") == (30.0, 1)


def test_argumentos_de_la_linea_de_ordenes():
    p, args = parse_args(["--clientes", "3", "--lag", "150", "--varianza", "30", "--perdida", "2", "--render", "ninguno", "--carpeta", "C:/tmp/x"])
    assert p.clients == 3 and p.emulation == Emulation(150, 30, 2) and p.render == "ninguno"
    assert not args.mostrar


def test_mostrar_no_lanza_y_valida(capsys):
    assert main(["--clientes", "2", "--mostrar", "--motor", "C:/UE"]) == 0
    out = capsys.readouterr().out
    assert out.count("UnrealEditor-Win64-DebugGame.exe") == 3
    assert main(["--clientes", "9", "--mostrar"]) == 2


SERVER_LOG = """[2026.10.02-10.00.00:000][  0]LogNet: GameNetDriver IpNetDriver_0 IpNetDriver listening on port 7777
[2026.10.02-10.00.05:000][ 10]LogNet: Join succeeded: DESKTOP-1
[2026.10.02-10.00.07:000][ 20]LogNet: Join succeeded: DESKTOP-2
[2026.10.02-10.01.20:000][900]LogTortunabo: [Estrés] Terminado (tiempo cumplido). Informe guardado: C:/p/Saved/Stress/heavy.json
[2026.10.02-10.01.21:000][901]LogExit: Exiting.
"""

CLIENT_LOG = """[2026.10.02-10.00.05:000][  5]LogNet: Welcomed by server (Level: /Game/Maps/Run/LVL_BeachRace, Game: /Script/Tortunabo.X)
[2026.10.02-10.00.25:000][100]LogTortunabo: Display: [Informe] línea de órdenes: informe de bug en C:/p/Saved/BugReports/2026-10-02_10-00-25 (LVL_BeachRace, Client, abc).
[2026.10.02-10.01.15:000][800]LogTortunabo: [Monkey] Sesión terminada (tiempo cumplido): sin fallos. Informe guardado: C:/tmp/red/monkey_cliente1.json
"""


def test_resumen_de_registros():
    server = summarize_server_log(SERVER_LOG)
    assert server["listening_port"] == 7777
    assert server["joins"] == ["DESKTOP-1", "DESKTOP-2"]
    assert server["stress_report"] == "C:/p/Saved/Stress/heavy.json"
    client = summarize_client_log(CLIENT_LOG)
    assert client["welcomed"] and client["level"] == "/Game/Maps/Run/LVL_BeachRace"
    assert client["monkey"]["verdict"] == "sin fallos"
    assert client["monkey"]["report"] == "C:/tmp/red/monkey_cliente1.json"
    assert client["bug_report"] == "C:/p/Saved/BugReports/2026-10-02_10-00-25"
    lost = summarize_client_log("LogNet: Warning: TravelFailure: PendingConnectionFailure")
    assert not lost["welcomed"] and "PendingConnectionFailure" in lost["net_failures"]


def test_informe_del_monkey_y_veredicto(tmp_path):
    report = tmp_path / "monkey.json"
    report.write_text(json.dumps({"net_mode": "Client", "players": 1, "net_corrections": 4, "passed": True}), encoding="utf-8")
    data = read_monkey_report(report)
    assert data["players"] == 1 and data["net_corrections"] == 4
    assert read_monkey_report(tmp_path / "no.json") is None

    good = {"instances": {"servidor": {}, "cliente1": {"welcomed": True, "monkey": {"data": data}}}}
    assert summary_ok(good)
    unconnected = {"instances": {"servidor": {}, "cliente1": {"welcomed": False}}}
    assert not summary_ok(unconnected)
    failed = {"instances": {"cliente1": {"welcomed": True, "monkey": {"data": {**data, "passed": False}}}}}
    assert not summary_ok(failed)


def test_puerto_libre_salta_el_ocupado():
    import socket

    from red_local import find_free_udp_port

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as busy:
        busy.bind(("0.0.0.0", 0))
        taken = busy.getsockname()[1]
        assert find_free_udp_port(taken, tries=5) != taken


def test_rutas_relativas_del_juego_van_desde_el_ejecutable():
    from red_local import resolve_game_path

    engine = Path("C:/UE")
    assert resolve_game_path("../../../../x/Saved/a.json", engine) == Path("C:/x/Saved/a.json").resolve()
    assert resolve_game_path("C:/abs/b.json", engine) == Path("C:/abs/b.json")
