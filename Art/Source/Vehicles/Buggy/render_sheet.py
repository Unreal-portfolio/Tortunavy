"""Lámina de revisión del buggy: 4 vistas con las dos tortugas sentadas en silueta y una tira de 3 skins.

    blender -b --factory-startup --python-exit-code 1 --python Art/Source/Vehicles/Buggy/render_sheet.py

Abre SM_TN_Buggy.blend (lo que generó build_buggy.py), no lo guarda. Workbench, cámaras ortográficas en frente,
lado y planta, y perspectiva en 3/4. Salida: Art/Source/Vehicles/Buggy/preview/buggy_sheet.png (1600 × 1500).
"""
import json
import math
import os
import sys
import tempfile

import bpy
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_buggy as bb  # noqa: E402
import turtle_pose  # noqa: E402

SHEET_PATH = os.path.join(HERE, 'preview', 'buggy_sheet.png')
PANEL_W, PANEL_H = 800, 560
SKIN_W, SKIN_H = 1600 // 3, 380
TURTLE_COLOR = 0x2F5D50
TEXT_COLOR = 0x1C1A22
MARKER_COLOR = 0xFF2DB4
BACKGROUND = (0.95, 0.95, 0.95)
SKINS = [
    ('Mar', {'paint': 0x2F80ED, 'detail': 0xFFD23F, 'wheel': 0x2B2833, 'trim': 0x80878C, 'light': 0xFFEE99}),
    ('Alga', {'paint': 0x3DAE5A, 'detail': 0xF4EFE2, 'wheel': 0x3A2E27, 'trim': 0x5B6168, 'light': 0xFFEE99}),
    ('Medusa', {'paint': 0xC98BFF, 'detail': 0xFF6FA8, 'wheel': 0x2B2833, 'trim': 0xD5DCE6, 'light': 0xFFEE99}),
]


def _preview_colors(obj, skin):
    """Color de vértice 'Preview' = decodificación de la zona con la paleta (lo mismo que hará M_TN_Buggy)."""
    mesh = obj.data
    zone = mesh.color_attributes['Zone']
    prev = mesh.color_attributes.get('Preview') or mesh.color_attributes.new('Preview', 'FLOAT_COLOR', 'CORNER')
    pal = {tuple(v): np.array(bb.srgb_to_linear(skin[k])[:3]) for k, v in bb.bmx.ZONE_MASKS.items()}
    for i, z in enumerate(zone.data):
        r, g, b, a = z.color
        rgb = pal[(round(r), round(g), round(b))] * a
        prev.data[i].color = (rgb[0], rgb[1], rgb[2], 1.0)
    mesh.color_attributes.active_color = prev


def _flat_mesh(name, verts, faces, color_hex, collection):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([tuple(v) for v in verts], [], faces)
    attr = mesh.color_attributes.new('Preview', 'FLOAT_COLOR', 'CORNER')
    col = bb.srgb_to_linear(color_hex)
    for d in attr.data:
        d.color = col
    mesh.color_attributes.active_color = attr
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    return obj


def _turtles(collection):
    body = bpy.data.objects[bb.BODY_NAME]
    for socket, make in (('SOCKET_Seat_Driver', turtle_pose.driver), ('SOCKET_Seat_Gunner', turtle_pose.gunner)):
        verts, tris = make().to_buggy()
        origin = np.array((body.matrix_world @ bpy.data.objects[socket].matrix_local).translation)
        _flat_mesh('Turtle_' + socket[7:], verts * bb.M_TO_CM + origin, tris, TURTLE_COLOR, collection)


def _socket_markers(collection):
    """Bolitas magenta en los sockets (las de los asientos quedan dentro de la cadera de cada tortuga)."""
    body = bpy.data.objects[bb.BODY_NAME]
    for obj in [o for o in bpy.data.objects if o.name.startswith('SOCKET_')]:
        c = (body.matrix_world @ obj.matrix_local).translation
        r = 6.0
        verts = [c + Vector(d) * r for d in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))]
        faces = [(0, 2, 4), (2, 1, 4), (1, 3, 4), (3, 0, 4), (2, 0, 5), (1, 2, 5), (3, 1, 5), (0, 3, 5)]
        _flat_mesh('Marker_' + obj.name[7:], verts, faces, MARKER_COLOR, collection)


def _label(cam, text, aspect):
    """Texto en la esquina superior izquierda del encuadre (malla con color plano, hija de la cámara)."""
    curve = bpy.data.curves.new('Label', type='FONT')
    curve.body = text
    tmp = bpy.data.objects.new('LabelTmp', curve)
    bpy.context.scene.collection.objects.link(tmp)
    if cam.data.type == 'ORTHO':
        dist, half_w = 50.0, cam.data.ortho_scale / 2
    else:
        dist = 100.0
        half_w = dist * math.tan(cam.data.angle / 2)
    half_h = half_w / aspect
    curve.size = half_w * 0.075
    bpy.context.view_layer.update()
    mesh = bpy.data.meshes.new_from_object(tmp.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    bpy.data.objects.remove(tmp)
    obj = _flat_mesh('Label', [v.co for v in mesh.vertices], [tuple(p.vertices) for p in mesh.polygons], TEXT_COLOR,
                     bpy.context.scene.collection)
    obj.parent = cam
    obj.location = (-half_w * 0.96, half_h * 0.96 - curve.size, -dist)
    return obj


def _camera(name, location, target, ortho_scale=None, lens=40.0):
    data = bpy.data.cameras.new(name)
    if ortho_scale:
        data.type = 'ORTHO'
        data.ortho_scale = ortho_scale
    else:
        data.lens = lens
    data.clip_start, data.clip_end = 1.0, 10000.0
    cam = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(cam)
    cam.location = location
    cam.rotation_euler = (Vector(target) - Vector(location)).to_track_quat('-Z', 'Y').to_euler()
    return cam


def _setup_render():
    scene = bpy.context.scene
    try:
        scene.render.engine = 'BLENDER_WORKBENCH'
    except TypeError as err:
        raise RuntimeError(f'Workbench no disponible: {err}') from err
    shading = scene.display.shading
    shading.light = bb._enum_ok(shading.bl_rna, 'light', 'STUDIO')
    shading.color_type = bb._enum_ok(shading.bl_rna, 'color_type', 'VERTEX')
    shading.show_object_outline = True
    shading.show_cavity = True
    shading.cavity_type = bb._enum_ok(shading.bl_rna, 'cavity_type', 'WORLD')
    shading.background_type = bb._enum_ok(shading.bl_rna, 'background_type', 'VIEWPORT')
    shading.background_color = BACKGROUND
    view = scene.view_settings
    try:  # enum dinámico (depende de la configuración OCIO): RNA no lo lista
        view.view_transform = 'Standard'
    except TypeError as err:
        print(f'[render_sheet] aviso: view_transform sin cambiar ({err})')
    scene.render.film_transparent = False
    fmt = scene.render.image_settings
    fmt.file_format = bb._enum_ok(fmt.bl_rna, 'file_format', 'PNG')
    fmt.color_mode = 'RGB'


def _render(cam, width, height, path):
    scene = bpy.context.scene
    scene.camera = cam
    for obj in scene.objects:
        if obj.name.startswith('Label'):
            obj.hide_render = obj.parent != cam
    scene.render.resolution_x, scene.render.resolution_y = width, height
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(path)
    px = np.array(img.pixels[:], dtype=np.float32).reshape(height, width, 4)
    bpy.data.images.remove(img)
    return px


def _views(stats):
    body_tris, tire_tris = stats
    aspect = PANEL_W / PANEL_H
    return [
        (_camera('Cam34', (640, 560, 400), (-10, 0, 95)), f'3/4 · chasis {body_tris} tris · rueda {tire_tris} tris · magenta = sockets'),
        (_camera('CamSide', (0, -1500, 110), (0, 0, 110), 560), 'Lado derecho (ortográfica, +X a la derecha)'),
        (_camera('CamFront', (1500, 0, 110), (0, 0, 110), 400), 'Frente (ortográfica, desde +X)'),
        (_camera('CamTop', (0, 0, 1500), (0, 0, 0), 560), 'Planta (ortográfica, +X a la derecha)'),
    ], aspect


def main():
    bpy.ops.wm.open_mainfile(filepath=bb.BLEND_PATH)
    with open(bb.MANIFEST_PATH, encoding='utf-8') as f:
        manifest = json.load(f)
    stats = tuple(a['tris'] for a in manifest['assets'])
    _setup_render()
    extra = bpy.data.collections.new('SheetOnly')
    bpy.context.scene.collection.children.link(extra)
    body = bpy.data.objects[bb.BODY_NAME]
    tire = bpy.data.objects[bb.TIRE_NAME]
    _preview_colors(body, bb.DEFAULT_SKIN)
    _preview_colors(tire, bb.DEFAULT_SKIN)
    tire.hide_render = True  # la rueda maestra está en el origen; se ven las 4 instancias Preview_Tire_*
    _turtles(extra)
    _socket_markers(extra)
    views, aspect = _views(stats)
    for cam, text in views:
        _label(cam, text, aspect)
    tmp = tempfile.mkdtemp(prefix='buggy_sheet_')
    sheet = np.ones((PANEL_H * 2 + SKIN_H, PANEL_W * 2, 4), dtype=np.float32)
    for i, (cam, _) in enumerate(views):
        px = _render(cam, PANEL_W, PANEL_H, os.path.join(tmp, f'view_{i}.png'))
        row, col = divmod(i, 2)
        top = SKIN_H + (1 - row) * PANEL_H  # las filas de numpy van de abajo arriba
        sheet[top:top + PANEL_H, col * PANEL_W:(col + 1) * PANEL_W] = px
    for obj in [o for o in extra.objects if o.name.startswith('Turtle_')]:
        obj.hide_render = True
    for i, (name, skin) in enumerate(SKINS):
        _preview_colors(body, skin)
        _preview_colors(tire, skin)
        cam = _camera(f'CamSkin{i}', (600, 520, 330), (-10, 0, 80))
        _label(cam, f'Skin «{name}» (solo parámetros del MI)', SKIN_W / SKIN_H)
        px = _render(cam, SKIN_W, SKIN_H, os.path.join(tmp, f'skin_{i}.png'))
        sheet[0:SKIN_H, i * SKIN_W:(i + 1) * SKIN_W] = px
    os.makedirs(os.path.dirname(SHEET_PATH), exist_ok=True)
    out = bpy.data.images.new('buggy_sheet', PANEL_W * 2, PANEL_H * 2 + SKIN_H, alpha=False)
    out.pixels.foreach_set(sheet.ravel())
    out.filepath_raw = SHEET_PATH
    out.file_format = 'PNG'
    out.save()
    print(f'[render_sheet] {SHEET_PATH} {os.path.getsize(SHEET_PATH) / 1024:.0f} KB')


if __name__ == '__main__':
    main()
