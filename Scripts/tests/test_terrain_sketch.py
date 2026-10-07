"""Mapa D01 desde el boceto del equipo de diseño (#875): criterios de aceptación sobre la variante generada
(Scripts/terrain_volumes/Variants/D01_boceto, la escribe gen_terrain_sketch.py en ~10 minutos).

Los tests no regeneran el mapa: leen su manifest, sus trozos (la malla que carga el juego) y
validacion.npz (rejillas de 1 m del mapa) y rehacen las medidas con terrain_path.sketch_metrics. La
referencia de C01 (altura de pared y triángulos) se mide igual sobre su modelo y su manifest.

Decisiones del director que fijan los umbrales (#875): 2026-10-06, cuestas andables entre zonas, suelo casi
llano dentro de ellas, cueva de 10 m como máximo, paredes más altas que C01; 2026-10-07, recorrido de unos
600 m (550-650), cueva amplia por debajo de las zonas, cuestas de 25 grados como mucho y hondonada mucho más
hundida.

    uv run pytest Scripts/tests/test_terrain_sketch.py -q
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from terrain_path import sketch_metrics as sm  # noqa: E402
from terrain_path.sketch import SEA, WALL, polyline_points  # noqa: E402
from terrain_path.sketch_d01 import D01_SPEC  # noqa: E402
from terrain_path.sketch_report import EDGE_M, profiles, triangle_counts  # noqa: E402
from terrain_vol.export import read_chunk  # noqa: E402
from terrain_vol.layout import UU_PER_M, WATER_M  # noqa: E402
from terrain_vol.validate import check_seams, reachable  # noqa: E402

VARIANTS = Path(__file__).resolve().parent.parent / "terrain_volumes" / "Variants"
D01 = VARIANTS / D01_SPEC.name
C01 = VARIANTS / "C01_camino"
MESH_LIMIT_DEG = sm.WALKABLE_FLOOR_DEG - sm.SLOPE_MARGIN_DEG       # 39,77: lo andable con 5 grados de margen
RAMP_LIMIT_DEG = 25.0                                              # cuestas entre zonas (director, 07-10)
ROUTE_RANGE_M = (550.0, 650.0)                                     # director, 07-10 (antes, 900-1100)
HOLLOW_DROP_M = 10.0                                               # hondonada bajo la meseta media


@pytest.fixture(scope="module")
def manifest() -> dict:
    return json.loads((D01 / "manifest.json").read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def grids() -> dict:
    with np.load(D01 / "validacion.npz") as data:
        g = {k: data[k] for k in data.files}
    g["top"] = g["top_cm"] / 100.0 + WATER_M
    g["height"] = g["height_cm"] / 100.0 + WATER_M
    g["e_any"] = g["e_dm"] / 10.0
    g["floor"] = (g["region"] == 0) & (g["e_any"] < -EDGE_M) & ~g["tunnel"] & (g["labels"] != SEA)
    return g


@pytest.fixture(scope="module")
def meshes(manifest) -> list[tuple[np.ndarray, np.ndarray]]:
    """Trozos con colisión en metros del mundo (lo que carga ATN_MapVariantLoader)."""
    out = []
    for cell in manifest["cells"]:
        if not cell.get("collision", True):
            continue
        chunk = read_chunk(D01 / cell["file"])
        center = np.array([cell["center_uu"][0], cell["center_uu"][1], 0.0]) / UU_PER_M
        out.append((chunk["vertices"].astype(np.float64) / UU_PER_M + center, chunk["triangles"].astype(np.int64)))
    return out


# -- 1. zonas, muros, mar y túneles ---------------------------------------------------------------
def test_tiene_zonas_muros_mar_y_dos_tuneles(grids, manifest):
    present = set(np.unique(grids["labels"]).tolist())
    assert {SEA, WALL} | set(D01_SPEC.zone_classes) <= present
    assert [t["name"] for t in manifest["tuneles"]] == ["cueva", "tunel"]
    assert len(manifest["tunnel_mouths_uu"]) == 4
    assert any("600 m" in d for d in manifest["decisiones_director"])


def test_las_cotas_de_las_zonas_siguen_la_leyenda(grids):
    """Más oscuro, más alto: la mediana de la cota de cada zona crece al bajar la luminancia de su tono."""
    def luminance(cls):
        r, g, b = D01_SPEC.palette[cls]
        return 0.299 * r + 0.587 * g + 0.114 * b

    order = sorted(D01_SPEC.zone_classes, key=lambda c: -luminance(c))
    medians = [float(np.median(grids["top"][grids["plateau"] & (grids["labels"] == c)])) for c in order]
    assert all(a + 1.5 < b for a, b in zip(medians[:-1], medians[1:])), medians


# -- 2. recorrido -------------------------------------------------------------------------------
def test_recorrido_por_el_centro_de_550_a_650_m(grids):
    start, end = tuple(grids["start_ij"]), tuple(grids["end_ij"])
    reach = reachable(grids["top"], start)
    walk = reach & (grids["region"] == 0) & ~grids["tunnel"]
    goal = np.zeros_like(walk)
    goal[end[0] - 2:end[0] + 3, end[1] - 2:end[1] + 3] = True
    _, length = sm.centerline(walk, start, goal & walk)
    assert ROUTE_RANGE_M[0] <= length <= ROUTE_RANGE_M[1], f"recorrido {length:.0f} m"


# -- 3. pendientes y escalones ------------------------------------------------------------------
def test_mesetas_casi_llanas_y_cuestas_andables_con_margen(grids):
    slopes = sm.floor_slopes(grids["top"], grids["plateau"], grids["ramp"], grids["floor"])
    assert slopes["mesetas"]["celdas"] > 20_000 and slopes["cuestas"]["celdas"] > 1_000
    assert slopes["mesetas"]["max_deg"] <= 10.0, slopes
    assert slopes["cuestas"]["max_deg"] < RAMP_LIMIT_DEG, slopes


def test_la_malla_del_suelo_no_tiene_caras_inandables_ni_escalones(grids, meshes):
    report = sm.floor_triangles(meshes, grids["floor"], grids["top"], tuple(grids["origin_m"]))
    assert report["triangulos_suelo"] > 10_000
    assert report["limite_deg"] <= MESH_LIMIT_DEG + 0.01
    assert report["mas_empinados_que_limite"] == 0 and report["escalones"] == 0, report


# -- 4. caminabilidad y túneles -----------------------------------------------------------------
def test_validador_de_c01_en_verde(manifest):
    """sketch_io.check (el de gen_terrain_path.check sobre el volumen) pasó al generar: salida -> mar,
    cada túnel de boca a boca en los dos sentidos, salida de la hondonada y ninguna pared pisada."""
    assert manifest["recorrible"] is True
    assert all(manifest["metricas"]["criterios"].values()), manifest["metricas"]["criterios"]


def test_los_trozos_casan_sin_grietas():
    assert check_seams(D01)["ok"]


def test_tuneles_atravesables_con_techo_acotado(manifest, meshes):
    """Rayos verticales contra la malla a lo largo del eje de cada túnel: suelo y techo en todo el tramo
    cubierto, techo de 10 m como mucho y holgura para la tortuga, y suelo andable."""
    report = profiles(meshes, manifest["tuneles"])
    for name, t in report.items():
        assert t["sin_suelo"] == 0 and t["sin_techo"] == 0, (name, t)
        assert t["alto_max_m"] <= 10.0, (name, t)
        assert t["alto_min_m"] >= sm.TURTLE_HEIGHT_M + 1.2, (name, t)
        assert t["pendiente_max_deg"] < RAMP_LIMIT_DEG, (name, t)


def test_la_hondonada_queda_muy_hundida(grids):
    from terrain_path.sketch_d01 import HOLLOW, MEDIUM
    med = {c: float(np.median(grids["top"][grids["plateau"] & (grids["labels"] == c)])) for c in (HOLLOW, MEDIUM)}
    assert med[MEDIUM] - med[HOLLOW] >= HOLLOW_DROP_M, med


def test_la_cueva_es_amplia_y_va_por_debajo_de_las_zonas(manifest, grids):
    """El eje de la cueva baja desde su boca por debajo del suelo de las zonas y se ensancha (la mancha
    discontinua del boceto); en su tramo con techo, el suelo de encima es zona jugable, no pared."""
    cave = next(t for t in manifest["tuneles"] if t["name"] == "cueva")
    axis = np.array(cave["eje_m"])
    o = grids["origin_m"]
    i = np.rint(axis[:, 0] - o[0]).astype(int)
    j = np.rint(axis[:, 1] - o[1]).astype(int)
    depth = grids["top"][i, j] - axis[:, 2]
    assert cave["semiancho_m"] >= 15.0
    assert depth.max() >= 12.0, depth.max()
    low = axis[:, 2].min()
    assert low < axis[0, 2] - 4.0 and low < axis[-1, 2] - 0.5, (axis[0, 2], low, axis[-1, 2])   # baja y luego sube
    from scipy.spatial import cKDTree
    ii, jj = np.nonzero(np.ones(grids["labels"].shape, dtype=bool))
    dist, k = cKDTree(axis[:, :2]).query(np.stack([ii + o[0], jj + o[1]], axis=1))
    under = (dist < axis[k, 3]).reshape(grids["labels"].shape)
    over_zone = np.isin(grids["labels"][under], D01_SPEC.zone_classes).mean()
    assert cave["bajo_zonas"] and over_zone > 0.5, over_zone


def test_cada_tunel_es_un_solo_eje_sin_bifurcaciones():
    for tunnel in D01_SPEC.tunnels:
        pts = polyline_points(tunnel.axis_px, 1.0)
        turn = np.diff(np.unwrap(np.arctan2(*np.diff(pts, axis=0).T[::-1])))
        assert np.all(np.abs(turn) < np.radians(60.0)), tunnel.name


# -- 5. paredes ---------------------------------------------------------------------------------
@pytest.fixture(scope="module")
def c01_wall() -> dict:
    from terrain_path.model import PathModel
    from terrain_path.style import C01_SEED, C01_STYLE
    model = PathModel(C01_SEED, C01_STYLE)
    return sm.wall_height(model.grid.height[1:-1, 1:-1], model.region[1:-1, 1:-1])


def test_paredes_mas_altas_que_c01(grids, c01_wall):
    d01 = sm.wall_height(grids["height"], grids["region"])
    assert d01["media_m"] > c01_wall["media_m"], (d01, c01_wall)
    assert d01["p50_m"] > c01_wall["p50_m"], (d01, c01_wall)


# -- 6. triángulos ------------------------------------------------------------------------------
def test_no_mas_triangulos_que_c01():
    d01, c01 = triangle_counts(D01 / "manifest.json"), triangle_counts(C01 / "manifest.json")
    assert d01["triangulos_trozos"] <= c01["triangulos_trozos"], (d01, c01)
    assert d01["triangulos_total"] <= c01["triangulos_total"], (d01, c01)
