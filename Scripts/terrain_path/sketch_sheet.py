"""Lámina cenital de un mapa de boceto (#875): el boceto (izquierda) y el mapa de alturas generado
(derecha) a la misma escala, Norte arriba, con el recorrido principal, el eje de cada túnel, sus bocas,
la salida y la meta, y una escala de colores en metros sobre el agua."""

from __future__ import annotations

import numpy as np
from PIL import Image, ImageDraw

from terrain_vol.layout import WATER_M

from .sketch import load_rgb

SCALE = 2                       # píxeles de la lámina por metro
HEADER_PX, FOOTER_PX, GAP_PX = 60, 90, 30
# Escala de alturas (m sobre el agua -> color): arena baja, verdes medios, ocres altos y roca clara.
HEIGHT_STOPS = ((0.0, (0.95, 0.90, 0.68)), (4.0, (0.86, 0.80, 0.45)), (8.0, (0.58, 0.72, 0.38)),
                (12.0, (0.33, 0.56, 0.30)), (16.0, (0.60, 0.45, 0.25)), (24.0, (0.45, 0.32, 0.22)),
                (32.0, (0.93, 0.93, 0.93)))
WATER_RGB = np.array([0.12, 0.38, 0.68])
CONTOUR_M = 3.0                 # una curva por tono del boceto


def height_rgb(h: np.ndarray) -> np.ndarray:
    """Color de la escala de alturas para h (m sobre el agua)."""
    keys = np.array([k for k, _ in HEIGHT_STOPS])
    cols = np.array([c for _, c in HEIGHT_STOPS])
    hc = np.clip(h, keys[0], keys[-1])
    return np.stack([np.interp(hc, keys, cols[:, k]) for k in range(3)], axis=-1)


def relief_image(top: np.ndarray) -> Image.Image:
    h = top - WATER_M
    gx, gy = np.gradient(top)
    light = np.clip((gx * 0.5 - gy * 0.35 + 1.0) / np.sqrt(gx * gx + gy * gy + 1.0), 0.0, 1.2)
    rgb = height_rgb(h) * (0.6 + 0.4 * light)[..., None]
    band = np.floor(h / CONTOUR_M)
    contour = (band != np.roll(band, 1, 0)) | (band != np.roll(band, 1, 1))
    rgb = np.where(contour[..., None], rgb * 0.7, rgb)
    rgb = np.where((h < 0.0)[..., None], WATER_RGB, rgb)
    image = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)[::-1])
    return image.resize((image.width * SCALE, image.height * SCALE), Image.NEAREST)


def cave_footprint(model) -> np.ndarray:
    """Planta de las cuevas bajo las zonas en su tramo con techo (rejilla del mapa, filas = Norte): a menos del
    semiancho del eje."""
    X, Y = np.meshgrid(model.ax_x, model.ax_y, indexing="ij")
    from .sketch_io import interior
    u, half, under, t = model._tunnel_query(X, Y, ("half", "under", "t"))
    return interior(model, (u < half) & (under > 0.5) & (t >= 0.0) & (t <= 1.0))


def _outline(mask: np.ndarray) -> np.ndarray:
    from scipy import ndimage
    return mask & ~ndimage.binary_erosion(mask, iterations=2)


def crop_scaled(sketch: Image.Image, spec, model, size) -> Image.Image:
    """El trozo del boceto que cubre el mapa (lo que cae fuera, color de pared), al tamaño dado."""
    left, top_px = spec.world_to_px(model.x_max, model.y_min)
    right, bottom = spec.world_to_px(model.x_min, model.y_max)
    margin = 800
    canvas = Image.new("RGB", (sketch.width + 2 * margin, sketch.height + 2 * margin), (143, 89, 0))
    canvas.paste(sketch, (margin, margin))
    box = tuple(int(round(float(v))) + margin for v in (left, top_px, right, bottom))
    return canvas.crop(box).resize(size, Image.BILINEAR)


def _colorbar(draw: ImageDraw.ImageDraw, x0: int, y0: int, width: int) -> None:
    lo, hi = HEIGHT_STOPS[0][0], HEIGHT_STOPS[-1][0]
    for k in range(width):
        r, g, b = (height_rgb(np.array(lo + (hi - lo) * k / (width - 1))) * 255).astype(int)
        draw.line([(x0 + k, y0), (x0 + k, y0 + 18)], fill=(int(r), int(g), int(b)))
    for m in range(int(lo), int(hi) + 1, 4):
        x = x0 + (m - lo) / (hi - lo) * (width - 1)
        draw.line([(x, y0 + 18), (x, y0 + 24)], fill=(0, 0, 0))
        draw.text((x - 6, y0 + 26), f"{m}", fill=(0, 0, 0))
    draw.text((x0, y0 - 16), "Altura del suelo sobre el agua (m); curvas cada 3 m; azul: agua", fill=(0, 0, 0))


def lamina(model, top: np.ndarray, route_pts: np.ndarray, path, caption: str = "") -> None:
    spec = model.spec
    relief = relief_image(top)
    from .sketch_model import SCRIPTS
    sketch = crop_scaled(Image.fromarray(load_rgb(SCRIPTS / spec.image)), spec, model, relief.size)
    # Planta de la cueva: contorno discontinuo sobre las dos vistas (como la linea discontinua del boceto).
    edge = _outline(cave_footprint(model))[::-1]
    edge = np.kron(edge, np.ones((SCALE, SCALE), dtype=bool))
    dashes = ((np.add.outer(np.arange(edge.shape[0]), np.arange(edge.shape[1])) // 10) % 2 == 0)
    for image in (relief, sketch):
        pixels = np.array(image)
        pixels[edge & dashes] = (90, 90, 90)
        image.paste(Image.fromarray(pixels))
    width = relief.width * 2 + GAP_PX
    sheet = Image.new("RGB", (width, relief.height + HEADER_PX + FOOTER_PX), (245, 240, 230))
    sheet.paste(sketch, (0, HEADER_PX))
    sheet.paste(relief, (relief.width + GAP_PX, HEADER_PX))
    draw = ImageDraw.Draw(sheet)

    def to_px(p, offset):
        return offset + (p[1] - model.y_min) * SCALE, HEADER_PX + (model.x_max - p[0]) * SCALE

    for offset in (0, relief.width + GAP_PX):
        draw.line([to_px(p, offset) for p in route_pts[::4]], fill=(200, 30, 30), width=3)
        for ax in model.tunnel_axes:
            s0, s1 = ax["covered"]
            sel = (ax["s"] >= s0) & (ax["s"] <= s1)
            draw.line([to_px(p, offset) for p in ax["pts"][sel][::4]], fill=(40, 40, 40), width=3)
            for s in (s0, s1):
                k = int(np.argmin(np.abs(ax["s"] - s)))
                cx, cy = to_px(ax["pts"][k], offset)
                draw.ellipse((cx - 14, cy - 14, cx + 14, cy + 14), outline=(220, 0, 0), width=4)
        for p, color in ((model.start, (0, 160, 0)), (model.end, (0, 0, 200))):
            cx, cy = to_px(p, offset)
            draw.rectangle((cx - 9, cy - 9, cx + 9, cy + 9), fill=color)
    draw.text((10, 10), f"{spec.name}: boceto (izq.) y mapa de alturas generado (der.), {spec.m_per_px} m/px del "
                        f"boceto. Rojo: recorrido principal; gris: eje de tunel y planta de la cueva (discontinua); "
                        f"circulos: bocas de tunel; "
                        f"verde: salida; azul: meta.", fill=(0, 0, 0))
    if caption:
        draw.text((10, 30), caption, fill=(0, 0, 0))
    _colorbar(draw, relief.width + GAP_PX + 20, relief.height + HEADER_PX + 40, relief.width - 40)
    sheet.save(path)
