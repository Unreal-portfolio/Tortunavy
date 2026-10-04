"""Eje de la calzada de un circuito de Rally (`road_uu`) para el manifest.

ATN_RallyTrack construye su spline con `road_uu` (eje cada 1 m, [x, y, z] en uu) si el manifest lo trae; si no, la
traza por las puertas (`checkpoints_uu`) y, entre dos puertas separadas 180 m, la curva se aparta de la calzada: en
I03R los buggies se metian contra la pared del tunel de la cabeza. write_kit_map ya escribe `road_uu`; esto lo anade a
un manifest generado antes sin regenerar los trozos (el eje sale de RallySpec igual que en build_rally).

    uv run python -m terrain_shapes.rally_road I03R
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

from .canvas import Canvas
from .kit import above, arc_length, profile, resample
from .kit_writer import road_uu
from .rally_circuit import RallySpec, _ctrl_fraction

VARIANTS = Path(__file__).resolve().parents[1] / "terrain_volumes" / "Variants"


def spec_road(spec: RallySpec) -> tuple[np.ndarray, np.ndarray]:
    """Eje y cota de la calzada, como en build_rally (sin tallar el terreno)."""
    pts = resample(spec.control, 1.0, closed=True)
    arc = arc_length(pts)
    total = float(arc[-1] + np.hypot(*(pts[0] - pts[-1])))
    to_t = _ctrl_fraction(spec, pts, arc, total) if spec.by_ctrl else (lambda v: v)
    z = above(profile(arc, [(to_t(t), h) for t, h in spec.heights], total=total, closed=True))
    return pts, z


def patch_manifest(spec: RallySpec, variants: Path = VARIANTS) -> int:
    """Escribe road_uu, road_width_m y closed en el manifest de la variante; devuelve los puntos del eje."""
    path = variants / spec.name / "manifest.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    pts, z = spec_road(spec)
    data["road_uu"] = road_uu(Canvas(spec.grid), pts, z)
    data["road_width_m"] = spec.road_w
    data["closed"] = True
    path.write_text(json.dumps(data, indent=1), encoding="utf-8")
    return len(pts)


def main(argv: list[str]) -> int:
    from .lots_rally import turtle_rally_spec
    specs = {"I03R": turtle_rally_spec}
    for key in argv or ["I03R"]:
        spec = specs[key]()
        print(f"{spec.name}: road_uu con {patch_manifest(spec)} puntos")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
