"""road_uu de los circuitos de Rally (terrain_shapes/rally_road.py) sobre la variante YA GENERADA I03R_tortuga_magna:
el eje que lee ATN_RallyTrack sale de la salida, pasa por cada puerta y cierra el lazo.

    uv run pytest Scripts/tests/test_rally_road.py
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from terrain_shapes.lots_rally import turtle_rally_spec  # noqa: E402
from terrain_shapes.rally_road import spec_road  # noqa: E402

MANIFEST = Path(__file__).resolve().parents[1] / "terrain_volumes" / "Variants" / "I03R_tortuga_magna" / "manifest.json"
# I03R salió del Rally y del repo (#692): se regenera con terrain_shapes/lots_rally.py si hace falta.
pytestmark = pytest.mark.skipif(not MANIFEST.exists(), reason="I03R_tortuga_magna sin generar (archivada fuera del repo, #692)")


def _manifest() -> dict:
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def test_road_starts_at_start_and_is_closed():
    data = _manifest()
    road = data["road_uu"]
    assert data["closed"] is True
    assert math.dist(road[0], data["start_uu"]) < 1.0
    assert math.dist(road[0][:2], road[-1][:2]) < 150.0          # el lazo cierra (1 m entre muestras)


def test_every_gate_lies_on_the_road():
    data = _manifest()
    road = data["road_uu"]
    for gate in data["checkpoints_uu"]:
        nearest = min(math.dist(gate[:2], p[:2]) for p in road)
        assert nearest < 100.0, f"puerta {gate} a {nearest:.0f} uu del eje"


def test_manifest_road_matches_spec():
    pts, z = spec_road(turtle_rally_spec())
    road = _manifest()["road_uu"]
    assert len(road) == len(pts)
    assert all(abs(p[2] - zz * 100.0) < 1.0 for p, zz in zip(road, z))
    steps = [math.dist(a[:2], b[:2]) for a, b in zip(road, road[1:])]
    assert max(steps) < 150.0 and min(steps) > 50.0              # muestras cada ~1 m
