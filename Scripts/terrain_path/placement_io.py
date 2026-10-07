"""Bloque "placements" del manifest de una variante (#652): lectura, mezcla y escritura.

    "placements": {
      "format": 1, "generator": "Scripts/place_terrain_path.py", "seed": 652, "map_seed": 60026,
      "include_pending": false,
      "auto": [ {...}, ... ],        # lo genera la CLI; se rehace entero en cada pasada
      "manual": [ {...}, ... ],      # de los diseñadores: la CLI nunca lo toca
      "suppressed": [ "id", ... ],   # ids de "auto" borrados a mano: no se vuelven a crear
      "summary": {...}
    }

Cada entrada: id, category, kind, class, line, s_m, q_m, location_uu [x, y, z] (z = suelo del camino;
el cargador la ajusta con una traza vertical), yaw_deg (absoluto, X = Norte), length_m, extent_uu,
size_scale, intensity, params y source. Una entrada manual solo necesita category, kind y location_uu.
Lo que ocupa un tramo (length_m > 0: puzles, pasarela, ermitaño, tanque) lleva además path_uu, la
polilínea del camino bajo su huella cada PATH_STEP_M (el cargador reparte las piezas por ella y no en recta).
Lo automático lleva también progress_m, su avance por el recorrido en metros del principal: la distancia
por el camino a la salida entre la suma de las distancias a la salida y a la meta, por la longitud del
principal. En el principal sin atajos es s_m; en un lazo, lo que equivale en el principal.
"""

from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path

import numpy as np

from .layout import UU_PER_M
from .placement_catalog import class_of, intensity_of
from .placement_rules import Placement
from .placement_site import Site

FORMAT = 1
GENERATOR = "Scripts/place_terrain_path.py"
PATH_STEP_M = 2.0       # separación de los puntos de path_uu


def _uu(point) -> list[float]:
    return [round(float(c) * UU_PER_M, 1) for c in point]


def _path_uu(site: Site, p: Placement) -> list[list[float]] | None:
    """Polilínea del camino (uu) bajo la huella de p, a su desplazamiento lateral; None si p es puntual."""
    if p.length <= 0.0:
        return None
    ln = site.line(p.line)
    s0, s1 = max(p.s - 0.5 * p.length, 0.0), min(p.s + 0.5 * p.length, ln.length)
    count = max(2, int(round((s1 - s0) / PATH_STEP_M)) + 1)
    return [_uu(ln.at(float(s), p.q)) for s in np.linspace(s0, s1, count)]


def progress_field(site: Site) -> np.ndarray:
    """Avance (m del principal) de cada muestra del grafo: d_salida / (d_salida + d_meta) * largo."""
    main = site.main
    d = site.geodesic_from([(0, 0.0), (0, main.length)])
    total = d[0] + d[1]
    with np.errstate(invalid="ignore", divide="ignore"):
        frac = np.where(np.isfinite(total) & (total > 0.0), d[0] / total, np.nan)
    return frac * main.length


def to_json(site: Site, p: Placement, progress: np.ndarray | None = None) -> dict:
    """Entrada del manifest. progress: progress_field(site), para no rehacerlo en cada entrada."""
    if progress is None:
        progress = progress_field(site)
    ln = site.line(p.line)
    x, y, z = ln.at(p.s, p.q)
    yaw = (ln.yaw_deg(p.s) + p.yaw_offset_deg + 180.0) % 360.0 - 180.0
    out = {"id": p.id, "category": p.category, "kind": p.kind, "class": class_of(p.category, p.kind),
           "line": p.line, "s_m": round(p.s, 2), "q_m": round(p.q, 2),
           "location_uu": [round(x * UU_PER_M, 1), round(y * UU_PER_M, 1), round(z * UU_PER_M, 1)],
           "yaw_deg": round(yaw, 1), "length_m": round(p.length, 2), "extent_uu": round(p.extent_m * UU_PER_M, 1),
           "size_scale": p.size_scale, "intensity": intensity_of(p.category, p.kind),
           "params": p.params, "source": p.source}
    advance = float(progress[site.node(p.line, p.s)])
    if np.isfinite(advance):
        out["progress_m"] = round(advance, 1)
    path = _path_uu(site, p)
    if path is not None:
        out["path_uu"] = path
    return out


def from_json(site: Site, d: dict, source: str = "manual") -> Placement:
    """Placement de una entrada del manifest. Sin line/s_m (lo normal en una manual), se proyecta
    location_uu sobre el camino más cercano."""
    if "line" in d and "s_m" in d:
        line, s, q = int(d["line"]), float(d["s_m"]), float(d.get("q_m", 0.0))
    else:
        xy = np.asarray(d["location_uu"][:2], dtype=float) / UU_PER_M
        line, s, _dist = site.project(xy)
        ln = site.line(line)
        q = float(np.dot(xy - ln.point(s), ln.normal(s)))
    return Placement(str(d["id"]), str(d["category"]), str(d["kind"]), line, s, q,
                     float(d.get("length_m", 0.0)), 0.0, float(d.get("extent_uu", 0.0)) / UU_PER_M,
                     float(d.get("size_scale", 1.0)), dict(d.get("params", {})), source)


def read_block(manifest: dict) -> tuple[list[dict], list[str], dict]:
    """(manual, suppressed, bloque entero) del manifest; vacíos si aún no hay bloque."""
    block = manifest.get("placements") or {}
    return list(block.get("manual", [])), [str(i) for i in block.get("suppressed", [])], block


def build_block(site: Site, auto, manual_raw: list[dict], suppressed: list[str], seed: int, map_seed: int,
                include_pending: bool, summary: dict) -> dict:
    progress = progress_field(site)
    return {"format": FORMAT, "generator": GENERATOR, "seed": seed, "map_seed": map_seed,
            "include_pending": include_pending, "auto": [to_json(site, p, progress) for p in auto],
            "manual": manual_raw, "suppressed": suppressed, "summary": summary}


def with_block(manifest: dict, block: dict) -> dict:
    """Copia del manifest con el bloque nuevo; el resto de claves queda igual y en su orden."""
    out = dict(manifest)
    out["placements"] = block
    return out


def write_manifest(path: Path, manifest: dict) -> None:
    """Escritura atómica con el mismo formato que gen_terrain_path (indent=1)."""
    text = json.dumps(manifest, indent=1)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=".manifest.", suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(text)
        os.replace(tmp, path)
    except BaseException:
        Path(tmp).unlink(missing_ok=True)
        raise


def carry_placements(old_manifest_path: Path, new_seed: int) -> dict | None:
    """Bloque "placements" del manifest anterior para volver a ponerlo tras regenerar el terreno
    (gen_terrain_path borra la carpeta de la variante). Lo manual y lo suprimido se conservan tal
    cual; si la semilla del mapa ha cambiado, lo automático queda marcado "stale" hasta rehacerlo."""
    if not old_manifest_path.exists():
        return None
    try:
        old = json.loads(old_manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"no se puede leer {old_manifest_path} para conservar sus placements: {exc}") from exc
    block = old.get("placements")
    if not block:
        return None
    block = dict(block)
    if int(block.get("map_seed", new_seed)) != int(new_seed):
        block["stale"] = True
    return block
