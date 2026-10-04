"""Lámina de revisión de un circuito de Rally por vueltas (#622): planta con los elementos, perfil de alturas con
los vuelos de los saltos, y velocidad y peralte por el arco. Se guarda junto a la variante (lamina.png, < 1 MB).
Necesita matplotlib (uv run --with matplotlib ...).
"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np

from terrain_vol.layout import MAP_MIN_M, UU_PER_M, WATER_M
from terrain_vol.sheet import _colormap, _save_small

COLORS = {"recta": "#1f77b4", "curva_peraltada": "#d62728", "horquilla": "#9467bd", "chicane": "#ff7f0e",
          "salto": "#2ca02c", "rasante": "#8c564b", "baches": "#e377c2", "baden": "#5a3b1c", "banqueta": "#17becf"}
LABELS = {"recta": "recta de salida", "curva_peraltada": "curva peraltada", "horquilla": "horquilla",
          "chicane": "chicane", "salto": "salto", "rasante": "cambio de rasante", "baches": "baches",
          "baden": "badén con barro", "banqueta": "banqueta de tierra"}


def _mask(arc: np.ndarray, total: float, span) -> np.ndarray:
    s0, s1 = span[0] % total, span[1] % total
    return ((arc >= s0) & (arc <= s1)) if s0 <= s1 else ((arc >= s0) | (arc <= s1))


def _plan_panel(ax, top: np.ndarray, data: dict, road: np.ndarray, arc: np.ndarray, total: float) -> None:
    from matplotlib.colors import LightSource, TwoSlopeNorm
    rel = top - WATER_M
    norm = TwoSlopeNorm(vcenter=0.0, vmin=-1.0, vmax=max(float(rel.max()), 2.0))
    shade = LightSource(azdeg=315, altdeg=40).hillshade(rel, vert_exag=2.0)
    rgb = _colormap()(norm(rel))[..., :3] * (0.55 + 0.45 * shade[..., None])
    rows, cols = top.shape[0] - 1, top.shape[1] - 1
    ax.imshow(rgb, origin="lower", extent=(MAP_MIN_M, MAP_MIN_M + cols, MAP_MIN_M, MAP_MIN_M + rows))
    ax.plot(road[:, 1], road[:, 0], "-", color="0.15", lw=3.2, alpha=0.8)
    seen = set()
    for e in data["elements"]:
        m = _mask(arc, total, e["s_m"])
        seg = np.where(m[:, None], road, np.nan)
        label = LABELS[e["type"]] if e["type"] not in seen else None
        seen.add(e["type"])
        ax.plot(seg[:, 1], seg[:, 0], "-", color=COLORS[e["type"]], lw=2.6, label=label)
        mid = road[np.nonzero(m)[0][len(np.nonzero(m)[0]) // 2]]
        text = e["id"].replace("curva_peraltada", "peralte").replace("_", " ")
        if e["type"] == "curva_peraltada":
            text += f" {e['bank_deg']:.0f}°"
        if e["type"] == "salto":
            text += f" {e.get('jump_kind', '')} {e['v_design_kmh']:.0f} km/h".replace("  ", " ")
        if e["type"] == "baches":
            text += f" {e['pattern'].replace('_', ' ')} {2 * e['amplitude_m']:.2f} m / {e['wavelength_m']:.1f} m"
        ax.annotate(text, (mid[1], mid[0]), xytext=(6, 6), textcoords="offset points", fontsize=7,
                    backgroundcolor=(1, 1, 1, 0.65))
    slots = np.asarray(data["markers_uu"]["parrilla"]) / UU_PER_M
    ax.plot(slots[:, 1], slots[:, 0], "s", ms=3.5, mfc="white", mec="k", label="parrilla 2 x 4")
    cps = np.asarray(data["checkpoints_uu"]) / UU_PER_M
    ax.plot(cps[:, 1], cps[:, 0], "o", ms=6, mfc="yellow", mec="k", label="puertas")
    for key, pts in data["markers_uu"].items():
        if key.endswith("_aterrizaje"):
            p = np.asarray(pts[0]) / UU_PER_M
            ax.plot(p[1], p[0], "v", ms=7, mfc="lime", mec="k")
    k = 30
    ax.annotate("", (road[k, 1], road[k, 0]), (road[0, 1], road[0, 0]),
                arrowprops={"arrowstyle": "-|>", "color": "k", "lw": 2})
    ax.set_xlim(MAP_MIN_M, MAP_MIN_M + cols)
    ax.set_ylim(MAP_MIN_M, MAP_MIN_M + rows)
    ax.set_xlabel("Este (m)")
    ax.set_ylabel("Norte (m)")
    ax.legend(loc="lower left", fontsize=7, framealpha=0.85)
    ax.set_title("Planta (flecha: sentido de la marcha desde la salida; triángulo verde: aterrizaje a la velocidad "
                 "de llegada)", fontsize=9)


def _profile_panel(ax, data: dict, track, arc: np.ndarray, total: float) -> None:
    z = np.asarray(data["road_uu"])[:, 2] / UU_PER_M
    for e in data["elements"]:
        if e["type"] in ("salto", "rasante", "curva_peraltada"):
            s0, s1 = e["s_m"]
            ax.axvspan(s0, s1 if s1 >= s0 else total, color=COLORS[e["type"]], alpha=0.15, lw=0)
    ax.plot(arc, z, "-", color="0.15", lw=1.2, label="eje (cota)")
    for j in track.jumps:
        d = j.design
        lip = j.s0 + d.lip_x
        z_lip = float(np.interp(lip, arc, z))
        for v, style, label in ((d.v_design, "-", "vuelo a la velocidad de llegada"), (d.v_boost, ":", "vuelo con turbo")):
            land = d.fly(v)
            if land is None:
                continue
            x = np.linspace(0.0, land.x_land_m, 60)
            th = math.radians(d.lip_deg)
            zz = z_lip + x * math.tan(th) - 9.81 * x * x / (2.0 * v * v * math.cos(th) ** 2)
            ax.plot(lip + x, zz, style, color=COLORS["salto"], lw=1.3,
                    label=label if j is track.jumps[0] else None)
        zone = [j.s0 + x for x in d.landing_zone]
        ax.plot(zone, [np.interp(zone[0], arc, z) - 0.6] * 2, "-", color="lime", lw=4, alpha=0.8,
                label="zona de aterrizaje" if j is track.jumps[0] else None)
    for e in data["elements"]:
        if e["type"] == "rasante":
            ax.plot(e["crest_s_m"], np.interp(e["crest_s_m"], arc, z) + 0.4, "^", color=COLORS["rasante"], ms=7)
    ax.set_xlim(0.0, total)
    ax.set_ylabel("cota (m)")
    ax.legend(loc="upper right", fontsize=7, ncol=2)
    ax.set_title("Perfil de alturas por el arco (verde: saltos; marrón: rasantes; rojo: curvas peraltadas)", fontsize=9)


def _speed_panel(ax, data: dict, track, arc: np.ndarray) -> None:
    ax.plot(arc, track.speed * 3.6, "-", color=COLORS["recta"], lw=1.2, label="velocidad ideal (km/h)")
    ax.plot(arc, track.speed_boost * 3.6, ":", color=COLORS["recta"], lw=1.0, label="con turbo continuo (km/h)")
    ax.set_ylabel("km/h")
    ax.set_xlabel("arco desde la línea de salida (m)")
    ax2 = ax.twinx()
    ax2.plot(arc, np.asarray(data["bank_deg"]), "-", color=COLORS["curva_peraltada"], lw=1.0, label="peralte (°)")
    ax2.set_ylim(-16, 16)
    ax2.set_ylabel("peralte (°, + = derecha más baja)")
    ax.legend(loc="upper left", fontsize=7)
    ax2.legend(loc="upper right", fontsize=7)
    ax.set_xlim(0.0, arc[-1])


def render_circuit_sheet(path: Path, name: str, data: dict, top: np.ndarray, track, model) -> int:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    arc, total = track.arc, track.total
    road = np.asarray(data["road_uu"])[:, :2] / UU_PER_M
    fig = plt.figure(figsize=(18, 10), dpi=80)
    checks = data["checks"]["verdict"]
    lap = data["lap"]
    fig.suptitle(f"{name}: circuito de Rally por vueltas ({'#682' if data['generator'].get('profile') == 'tierra' else '#622'}), semilla {data['seed']}", fontsize=14,
                 fontweight="bold", x=0.01, ha="left")
    jumps = [e for e in data["elements"] if e["type"] == "salto"]
    banked = [e for e in data["elements"] if e["type"] == "curva_peraltada"]
    crests = [e for e in data["elements"] if e["type"] == "rasante"]
    jump_text = ", ".join(f"{e['v_design_kmh']:.0f} km/h y {e['airtime_s']:.1f} s en el aire" for e in jumps)
    bank_text = ", ".join(f"{e['bank_deg']:.0f}°" for e in banked)
    failed = [k for k, v in checks.items() if not v]
    summary = (f"Vuelta de {lap['length_m']:.0f} m ({data['laps']} vueltas), vuelta ideal {lap['ideal_lap_s']:.0f} s; "
               f"{len(jumps)} saltos ({jump_text}); {len(banked)} curvas peraltadas ({bank_text}); "
               f"{len(crests)} rasantes; rejilla {model.frame.rows} x {model.frame.cols} trozos. "
               f"Validador: {'FALLA ' + ', '.join(failed) if failed else 'todo en verde'}.")
    fig.text(0.01, 0.945, summary, fontsize=9, ha="left", va="top", wrap=True)
    top = top[:model.frame.rows * 100 + 1, :model.frame.cols * 100 + 1]     # la rejilla es rows x cols, no grid x grid
    _plan_panel(fig.add_axes((0.03, 0.05, 0.42, 0.85)), top, data, road, arc, total)
    _profile_panel(fig.add_axes((0.51, 0.47, 0.46, 0.42)), data, track, arc, total)
    _speed_panel(fig.add_axes((0.51, 0.06, 0.43, 0.32)), data, track, arc)
    written = _save_small(fig, path)
    plt.close(fig)
    return written
