"""Ningún otro tramo de la calzada pasa por el volumen de una puerta del Rally (revisión de #622).

ATN_RallyGate cuenta el paso con un volumen de WidthCm × DepthCm que va de BelowRoadCm por debajo del eje a HeightCm por
encima. Si otro tramo (un paso a distinto nivel o un rasante justo debajo) entrara en ese volumen, un buggy que circula
por él cruzaría el plano de la puerta. Este test lo comprueba en todos los circuitos por vueltas generados.

    uv run pytest Scripts/tests/test_rally_gate_clearance.py
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from terrain_geo.build import VARIANTS  # noqa: E402

# Copia de ATN_RallyGate (Source/Tortunabo/Public/Rally/TN_RallyGate.h), en cm.
GATE_WIDTH_CM = 2400.0
GATE_DEPTH_CM = 400.0
GATE_HEIGHT_CM = 1000.0
GATE_BELOW_ROAD_CM = 300.0
# Holgura del buggy (altura de la carrocería sobre el eje de la calzada) y tramo propio de la puerta que se ignora.
BUGGY_HEIGHT_CM = 250.0
OWN_ARC_CM = 3000.0


def gate_intrusions(road: np.ndarray, gates: list[list[float]]) -> list[tuple[int, int]]:
    """(puerta, muestra) de cada punto de otro tramo cuyo buggy entraría en el volumen de la puerta."""
    seg = np.linalg.norm(np.diff(road[:, :2], axis=0), axis=1)
    arc = np.concatenate(([0.0], np.cumsum(seg)))
    total = arc[-1] + float(np.linalg.norm(road[0, :2] - road[-1, :2]))
    out = []
    for g, (gx, gy, gz, yaw) in enumerate(gates):
        own = int(np.argmin(np.hypot(road[:, 0] - gx, road[:, 1] - gy)))
        gap = np.abs(arc - arc[own])
        gap = np.minimum(gap, total - gap)
        c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        dx, dy = road[:, 0] - gx, road[:, 1] - gy
        along, across = dx * c + dy * s, -dx * s + dy * c
        inside_plan = (np.abs(along) <= GATE_DEPTH_CM / 2) & (np.abs(across) <= GATE_WIDTH_CM / 2)
        dz = road[:, 2] - gz
        inside_z = (dz + BUGGY_HEIGHT_CM >= -GATE_BELOW_ROAD_CM) & (dz <= GATE_HEIGHT_CM)
        hits = np.nonzero(inside_plan & inside_z & (gap > OWN_ARC_CM))[0]
        out.extend((g, int(i)) for i in hits)
    return out


def rally_variants() -> list[Path]:
    found = []
    for manifest in sorted(VARIANTS.glob("*/manifest.json")):
        data = json.loads(manifest.read_text(encoding="utf-8"))
        if data.get("mode") == "rally" and data.get("closed") and data.get("checkpoints_uu"):
            found.append(manifest)
    return found


@pytest.mark.parametrize("manifest", rally_variants(), ids=lambda p: p.parent.name)
def test_ningun_tramo_entra_en_el_volumen_de_una_puerta(manifest):
    data = json.loads(manifest.read_text(encoding="utf-8"))
    road = np.asarray(data["road_uu"], dtype=np.float64)
    assert gate_intrusions(road, data["checkpoints_uu"]) == []


def test_caso_negativo_un_paso_inferior_bajo_la_puerta_se_detecta():
    """Una recta de 200 m y otra que vuelve cruzándola 2 m por debajo de la puerta del centro."""
    ida = np.array([[x, 0.0, 1000.0] for x in range(0, 20000, 100)], dtype=np.float64)
    vuelta = np.array([[10000.0, y, 800.0] for y in range(-10000, 10000, 100)], dtype=np.float64)
    road = np.vstack((ida, vuelta))
    hits = gate_intrusions(road, [[10000.0, 0.0, 1000.0, 0.0]])
    assert hits and all(road[i, 2] == 800.0 for _, i in hits)
