"""Lámina PNG de la colocación (#652): planta del recorrido (Norte arriba) sobre la vista cenital
del mapa, con lo colocado por categoría, las zonas de exclusión y la curva de intensidad por tramos."""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from .layout import MAP_MAX_M, MAP_MIN_M
from .placement_catalog import (
    CALM_MAX,
    DECOR,
    ENEMY,
    LOOT,
    MECHANIC,
    OBSTACLE,
    PEAK_MIN,
    PUZZLE,
    PUZZLES,
    TRAMO_M,
    VEGETATION,
)
from .placement_rules import Placement, exclusion_discs, footprint_points, tramo_intensity
from .placement_site import Site

SCALE = 3                      # píxeles por metro de la planta
PANEL_W = 470
CURVE_H = 230
COLORS = {PUZZLE: (225, 40, 200), MECHANIC: (40, 150, 255), ENEMY: (230, 30, 30), OBSTACLE: (255, 140, 0),
          LOOT: (255, 215, 0), DECOR: (90, 70, 50), VEGETATION: (40, 130, 40)}
LABELS = {PUZZLE: "Puzle (huella)", MECHANIC: "Mecánica", ENEMY: "Enemigo", OBSTACLE: "Obstáculo",
          LOOT: "Botín (rebusca, charco y conchas)", DECOR: "Decorado",
          VEGETATION: "Vegetación"}
LINE_COLORS = ((200, 30, 30), (255, 150, 30), (150, 80, 220))     # principal, rodeo, atajo


def _font(size: int):
    for name in ("arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def _px(xy) -> tuple[float, float]:
    """Metros (X Norte, Y Este) a píxel de la planta (Norte arriba, Este a la derecha)."""
    x, y = float(xy[0]), float(xy[1])
    return (y - MAP_MIN_M) * SCALE, (MAP_MAX_M - x) * SCALE


def _background(preview: Path | None, size: int) -> Image.Image:
    if preview is not None and preview.exists():
        img = Image.open(preview).convert("RGB").resize((size, size), Image.NEAREST)
        return Image.blend(img, Image.new("RGB", img.size, (255, 255, 255)), 0.35)
    return Image.new("RGB", (size, size), (225, 210, 180))


def _line_kind(site: Site, line_id: int) -> int:
    if line_id == 0:
        return 0
    ln = site.line(line_id)
    return 2 if ln.length < (ln.s_back - ln.s_out) else 1


def _draw_routes(draw, site: Site) -> None:
    for ln in site.lines:
        color = LINE_COLORS[_line_kind(site, ln.id)]
        pts = [_px(p) for p in ln.points[::2]]
        draw.line(pts, fill=color, width=3 if ln.id == 0 else 2)
        blocked = ln.points[ln.blocked]
        for p in blocked[::3]:
            x, y = _px(p)
            draw.rectangle([x - 1, y - 1, x + 1, y + 1], fill=(60, 60, 60))
        mid = _px(ln.point(ln.length / 2.0))
        draw.text((mid[0] + 4, mid[1] - 14), f"L{ln.id}", fill=color, font=_font(14))


def _draw_placements(draw, site: Site, placements, show_discs: bool) -> None:
    if show_discs:
        for _why, c, r in exclusion_discs(site):
            x, y = _px(c)
            draw.ellipse([x - r * SCALE, y - r * SCALE, x + r * SCALE, y + r * SCALE], outline=(120, 120, 120))
    order = (VEGETATION, DECOR, LOOT, MECHANIC, OBSTACLE, ENEMY, PUZZLE)
    font = _font(13)
    for cat in order:
        for p in (p for p in placements if p.category == cat):
            pts = footprint_points(site, p)
            color = COLORS.get(cat, (0, 0, 0))
            if cat == PUZZLE:
                draw.line([_px(q) for q in pts], fill=color, width=9)
                x, y = _px(pts[len(pts) // 2])
                draw.text((x + 8, y - 6), p.kind, fill=(20, 20, 20), font=font, stroke_width=2,
                          stroke_fill=(255, 255, 255))
                continue
            x, y = _px(pts[len(pts) // 2])
            r = {DECOR: 1.5, VEGETATION: 1.5, LOOT: 2.5, MECHANIC: 6, OBSTACLE: 5, ENEMY: 5}.get(cat, 4)
            if cat in (ENEMY, OBSTACLE):
                draw.polygon([(x, y - r - 1), (x + r, y + r), (x - r, y + r)], fill=color, outline=(0, 0, 0))
            elif cat == MECHANIC:
                draw.rectangle([x - r, y - r, x + r, y + r], fill=color, outline=(0, 0, 0))
                draw.text((x + 8, y - 6), p.kind, fill=(10, 40, 90), font=font, stroke_width=2,
                          stroke_fill=(255, 255, 255))
            else:
                draw.ellipse([x - r, y - r, x + r, y + r], fill=color)


def _draw_curve(img: Image.Image, site: Site, placements, top: int) -> None:
    draw = ImageDraw.Draw(img)
    font = _font(14)
    curve = tramo_intensity(site, placements, 0)
    width = img.width - 80
    peak = max(float(curve.max()), PEAK_MIN + 1.0)
    base_y = top + CURVE_H - 40
    draw.text((20, top + 6), f"Curva de intensidad del principal (tramos de {TRAMO_M:.0f} m): "
              f"pico ≥ {PEAK_MIN:.0f}, calma ≤ {CALM_MAX:.0f}", fill=(0, 0, 0), font=font)
    bar = width / len(curve)
    for y_val, color in ((PEAK_MIN, (230, 30, 30)), (CALM_MAX, (40, 150, 40))):
        y = base_y - y_val / peak * (CURVE_H - 80)
        draw.line([(60, y), (60 + width, y)], fill=color, width=1)
    for k, v in enumerate(curve):
        x0 = 60 + k * bar
        y = base_y - v / peak * (CURVE_H - 80)
        color = (230, 30, 30) if v >= PEAK_MIN else (40, 150, 40) if v <= CALM_MAX else (240, 160, 30)
        draw.rectangle([x0 + 2, y, x0 + bar - 2, base_y], fill=color)
        draw.text((x0 + 4, base_y + 4), f"{k * TRAMO_M:.0f}", fill=(60, 60, 60), font=_font(11))


def render_sheet(site: Site, placements, out: Path, preview: Path | None = None, title: str = "",
                 notes: list[str] | None = None, show_discs: bool = True) -> Path:
    placements = list(placements)
    size = int((MAP_MAX_M - MAP_MIN_M) * SCALE) + 1
    plan = _background(preview, size)
    draw = ImageDraw.Draw(plan)
    _draw_routes(draw, site)
    _draw_placements(draw, site, placements, show_discs)
    for label, p in (("SALIDA", site.start), ("META", site.end)):
        x, y = _px(p)
        draw.text((x + 8, y - 8), label, fill=(0, 0, 0), font=_font(18), stroke_width=3, stroke_fill=(255, 255, 255))
    img = Image.new("RGB", (size + PANEL_W, size + CURVE_H), (250, 248, 242))
    img.paste(plan, (0, 0))
    draw = ImageDraw.Draw(img)
    x0, y = size + 18, 16
    draw.text((x0, y), title, fill=(0, 0, 0), font=_font(20))
    y += 36
    counts: dict[str, int] = {}
    for p in placements:
        counts[p.category] = counts.get(p.category, 0) + 1
    for cat, label in LABELS.items():
        draw.rectangle([x0, y + 3, x0 + 14, y + 17], fill=COLORS[cat], outline=(0, 0, 0))
        draw.text((x0 + 22, y), f"{label}: {counts.get(cat, 0)}", fill=(0, 0, 0), font=_font(15))
        y += 24
    for color, label in zip(LINE_COLORS, ("Camino principal", "Rodeo (ruta larga)", "Atajo (ruta corta)")):
        draw.line([(x0, y + 10), (x0 + 14, y + 10)], fill=color, width=4)
        draw.text((x0 + 22, y), label, fill=(0, 0, 0), font=_font(15))
        y += 24
    draw.text((x0, y), "Gris: túnel, tablero, arco o escalón. Círculos: exclusión", fill=(60, 60, 60), font=_font(13))
    y += 30
    for line in notes or []:
        draw.text((x0, y), line, fill=(0, 0, 0), font=_font(14))
        y += 20
    _draw_curve(img, site, placements, size)
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    return out


def puzzle_notes(site: Site, placements: list[Placement]) -> list[str]:
    out = ["Puzles:"]
    for p in sorted((p for p in placements if p.category == PUZZLE), key=lambda p: (p.line, p.s)):
        where = "principal" if p.line == 0 else f"lazo {p.line}"
        mode = PUZZLES[p.kind].mode if p.kind in PUZZLES else "?"
        out.append(f"  {p.kind} ({mode}), {where}, s = {p.s:.0f} m")
    return out


__all__ = ["puzzle_notes", "render_sheet"]
