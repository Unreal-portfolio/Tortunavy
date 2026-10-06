"""Pipeline de un asset de la biblioteca: malla -> .blend + FBX -> validación (también del FBX reimportado) -> lámina.

Unidades: la escena trabaja en cm (scale_length 0,01) y el FBX sale en cm (UnitScaleFactor 1).
Material único M_TN_IAProp: réplica en Blender de la decodificación de zonas (ver ia_mesh.ZONE_MASKS).
"""
import datetime
import json
import math
import os
import tempfile

import bmesh
import bpy
import numpy as np
from mathutils import Euler, Matrix, Vector

import ia_mesh

MIN_BLENDER = (4, 2, 0)
MATERIAL_NAME = 'M_TN_IAProp'
LIBRARY_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(LIBRARY_ROOT)))
PANEL_W, PANEL_H = 560, 470
BACKGROUND = (0.95, 0.95, 0.95)
TEXT_COLOR = 0x1C1A22
MARKER_COLOR = 0xFF2DB4
GHOST_COLOR = 0xB9C2C9
MAX_SHEET_BYTES = 1024 * 1024
DIM_TOLERANCE_CM = 0.5


def rel(path):
    return os.path.relpath(path, REPO_ROOT).replace(os.sep, '/')


def srgb_to_linear(hex_color):
    def chan(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return (chan((hex_color >> 16) & 255), chan((hex_color >> 8) & 255), chan(hex_color & 255), 1.0)


def enum_ok(owner_rna, prop, value):
    items = [i.identifier for i in owner_rna.properties[prop].enum_items]
    if value not in items:
        raise RuntimeError(f'{prop}: {value!r} no existe en esta versión ({items})')
    return value


# ── Escena y material ───────────────────────────────────────────────────────

def setup_scene():
    if bpy.app.version < MIN_BLENDER:
        raise RuntimeError(f'Blender {bpy.app.version_string} < {MIN_BLENDER}')
    bpy.ops.wm.read_factory_settings(use_empty=True)
    units = bpy.context.scene.unit_settings
    units.system = 'METRIC'
    units.scale_length = 0.01
    try:  # enum dinámico (depende de `system`): RNA solo declara DEFAULT
        units.length_unit = 'CENTIMETERS'
    except TypeError as err:
        print(f'[ia] aviso: length_unit sin cambiar ({err})')
    export = bpy.data.collections.new('Export')
    preview = bpy.data.collections.new('SheetOnly')
    for c in (export, preview):
        bpy.context.scene.collection.children.link(c)
    return export, preview


def _node(nodes, type_name, bl_idname):
    node = next((n for n in nodes if n.type == type_name), None)
    return node or nodes.new(bl_idname)


def _mix(nodes, links, a, b, fac, blend='MIX'):
    mix = nodes.new('ShaderNodeMix')
    mix.data_type = enum_ok(mix.bl_rna, 'data_type', 'RGBA')
    mix.blend_type = enum_ok(mix.bl_rna, 'blend_type', blend)
    sock = {s.identifier: s for s in mix.inputs}
    for key, val in (('A_Color', a), ('B_Color', b)):
        if isinstance(val, tuple):
            sock[key].default_value = val
        else:
            links.new(val, sock[key])
    if isinstance(fac, float):
        sock['Factor_Float'].default_value = fac
    else:
        links.new(fac, sock['Factor_Float'])
    return next(s for s in mix.outputs if s.identifier == 'Result_Color')


def build_material(palette):
    """Zonas por máscara de color de vértice 'Zone' y un color por zona (lo que hará el MI en Unreal)."""
    mat = bpy.data.materials.new(MATERIAL_NAME)
    if bpy.app.version < (5, 0, 0):  # en 5.x los materiales siempre tienen nodos
        mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = _node(nodes, 'BSDF_PRINCIPLED', 'ShaderNodeBsdfPrincipled')
    vcol = nodes.new('ShaderNodeVertexColor')
    vcol.layer_name = 'Zone'
    sep = nodes.new('ShaderNodeSeparateColor')
    links.new(vcol.outputs['Color'], sep.inputs[0])
    r, g, bl = sep.outputs[0], sep.outputs[1], sep.outputs[2]
    col = {k: srgb_to_linear(v) for k, v in palette.items()}
    c = _mix(nodes, links, col['trim'], col['paint'], r)
    c = _mix(nodes, links, c, col['detail'], g)
    c = _mix(nodes, links, c, col['dark'], bl)
    rg = nodes.new('ShaderNodeMath')
    rg.operation = enum_ok(rg.bl_rna, 'operation', 'MULTIPLY')
    links.new(r, rg.inputs[0])
    links.new(g, rg.inputs[1])
    rgb = nodes.new('ShaderNodeMath')
    rgb.operation = enum_ok(rgb.bl_rna, 'operation', 'MULTIPLY')
    links.new(rg.outputs[0], rgb.inputs[0])
    links.new(bl, rgb.inputs[1])
    c = _mix(nodes, links, c, col['light'], rgb.outputs[0])
    shaded = _mix(nodes, links, c, vcol.outputs['Alpha'], 1.0, 'MULTIPLY')
    ins = {s.identifier: s for s in bsdf.inputs}
    links.new(shaded, ins['Base Color'])
    ins['Roughness'].default_value = 0.7
    ins['Emission Color'].default_value = col['light']
    links.new(rgb.outputs[0], ins['Emission Strength'])
    mat.diffuse_color = col['paint']
    return mat


# ── Objetos ─────────────────────────────────────────────────────────────────

def mesh_object(name, builder, mat, collection):
    bm = builder.finish()
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    attr = mesh.color_attributes.new('Zone', 'FLOAT_COLOR', 'CORNER')
    zones, shades = mesh.attributes['zone'].data, mesh.attributes['shade'].data
    for poly in mesh.polygons:
        r, g, b = ia_mesh.ZONE_MASKS[ia_mesh.ZONE_NAMES[zones[poly.index].value]]
        color = (r, g, b, shades[poly.index].value)
        for li in poly.loop_indices:
            attr.data[li].color = color
    mesh.attributes.remove(mesh.attributes['zone'])
    mesh.attributes.remove(mesh.attributes['shade'])
    mesh.color_attributes.active_color = attr
    mesh.color_attributes.render_color_index = mesh.color_attributes.active_color_index
    mesh.materials.append(mat)
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    _unwrap(obj)
    return obj


def _unwrap(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.02)
    bpy.ops.object.mode_set(mode='OBJECT')


def socket_objects(obj, sockets, collection):
    out = []
    for name, (loc, rot) in sockets.items():
        empty = bpy.data.objects.new('SOCKET_' + name, None)
        empty.empty_display_type = enum_ok(empty.bl_rna, 'empty_display_type', 'ARROWS')
        empty.empty_display_size = 10.0
        empty.parent = obj
        empty.location = loc
        empty.rotation_euler = Euler(tuple(math.radians(a) for a in rot), 'XYZ')
        collection.objects.link(empty)
        out.append(empty)
    return out


def collision_objects(obj, hulls, collection):
    """UCX_<malla>_NN: envolventes convexas que Unreal toma como colisión simple."""
    out = []
    for i, points in enumerate(hulls):
        bm = bmesh.new()
        unique = list(dict.fromkeys(tuple(round(c, 4) for c in p) for p in points))
        verts = [bm.verts.new(p) for p in unique]
        res = bmesh.ops.convex_hull(bm, input=verts)
        loose = list({g for g in res['geom_unused'] + res['geom_interior'] if isinstance(g, bmesh.types.BMVert)})
        if loose:
            bmesh.ops.delete(bm, geom=loose, context='VERTS')
        mesh = bpy.data.meshes.new(f'UCX_{obj.name}_{i:02d}')
        bm.to_mesh(mesh)
        bm.free()
        ucx = bpy.data.objects.new(mesh.name, mesh)
        ucx.display_type = enum_ok(ucx.bl_rna, 'display_type', 'WIRE')
        collection.objects.link(ucx)
        out.append(ucx)
    return out


def export_fbx(objects, path):
    op_rna = bpy.ops.export_scene.fbx.get_rna_type()
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:
        o.hide_set(False)
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={'MESH', 'EMPTY'},
        apply_unit_scale=True, apply_scale_options=enum_ok(op_rna, 'apply_scale_options', 'FBX_SCALE_UNITS'),
        mesh_smooth_type=enum_ok(op_rna, 'mesh_smooth_type', 'FACE'),
        colors_type=enum_ok(op_rna, 'colors_type', 'SRGB'),
        add_leaf_bones=False, bake_anim=False)


# ── Comprobaciones ──────────────────────────────────────────────────────────

def tris(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def bbox_cm(obj):
    co = np.array([v.co[:] for v in obj.data.vertices])
    lo, hi = co.min(axis=0), co.max(axis=0)
    return [round(float(x), 2) for x in lo], [round(float(x), 2) for x in hi]


def mesh_checks(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    non_manifold = sum(1 for e in bm.edges if len(e.link_faces) != 2)
    degenerate = sum(1 for f in bm.faces if f.calc_area() < 1e-4)
    loose = sum(1 for v in bm.verts if not v.link_faces)
    inverted = sum(1 for part in ia_mesh.components(bm) if ia_mesh.signed_volume(part) < 0.0)
    pieces = len(ia_mesh.components(bm))
    bm.free()
    return {'non_manifold_edges': non_manifold, 'degenerate_faces': degenerate, 'loose_verts': loose,
            'inverted_pieces': inverted, 'closed_pieces': pieces, 'material_slots': len(obj.material_slots),
            'uv_layers': len(obj.data.uv_layers)}


def reimport_fbx(path):
    """Reimporta el FBX en una escena vacía y mide lo que de verdad llega a Unreal."""
    from io_scene_fbx import parse_fbx
    root, _ = parse_fbx.parse(path)
    settings = next(e for e in root.elems if e.id == b'GlobalSettings')
    block = next(e for e in settings.elems if e.id == b'Properties70')
    unit = next(p.props[4] for p in block.elems if p.props[0] == b'UnitScaleFactor')
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    meshes = [o for o in bpy.data.objects if o.type == 'MESH' and not o.name.startswith('UCX_')]
    dims = {}
    for o in meshes:
        pts = np.array([(o.matrix_world @ v.co)[:] for v in o.data.vertices]) * 100.0  # m -> cm
        dims[o.name] = {'lo': pts.min(axis=0).round(2).tolist(), 'hi': pts.max(axis=0).round(2).tolist(),
                        'vertex_colors': [a.name for a in o.data.color_attributes]}
    sockets = sorted(o.name for o in bpy.data.objects if o.type == 'EMPTY' and o.name.startswith('SOCKET_'))
    ucx = sorted(o.name for o in bpy.data.objects if o.name.startswith('UCX_'))
    return {'unit_scale_factor': float(unit), 'meshes': dims, 'sockets': sockets, 'ucx': ucx}


# ── Lámina ──────────────────────────────────────────────────────────────────

def _flat_mesh(name, verts, faces, color_hex, collection):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([tuple(v) for v in verts], [], faces)
    attr = mesh.color_attributes.new('Preview', 'FLOAT_COLOR', 'CORNER')
    col = srgb_to_linear(color_hex)
    for d in attr.data:
        d.color = col
    mesh.color_attributes.active_color = attr
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    return obj


def preview_colors(obj, palette):
    mesh = obj.data
    zone = mesh.color_attributes['Zone']
    prev = mesh.color_attributes.new('Preview', 'FLOAT_COLOR', 'CORNER')
    pal = {tuple(v): np.array(srgb_to_linear(palette[k])[:3]) for k, v in ia_mesh.ZONE_MASKS.items()}
    for i, z in enumerate(zone.data):
        r, g, b, a = z.color
        rgb = pal[(round(r), round(g), round(b))] * a
        prev.data[i].color = (rgb[0], rgb[1], rgb[2], 1.0)
    mesh.color_attributes.active_color = prev


def _setup_render():
    scene = bpy.context.scene
    try:
        scene.render.engine = 'BLENDER_WORKBENCH'
    except TypeError as err:
        raise RuntimeError(f'Workbench no disponible: {err}') from err
    shading = scene.display.shading
    shading.light = enum_ok(shading.bl_rna, 'light', 'STUDIO')
    shading.color_type = enum_ok(shading.bl_rna, 'color_type', 'VERTEX')
    shading.show_object_outline = True
    shading.show_cavity = True
    shading.cavity_type = enum_ok(shading.bl_rna, 'cavity_type', 'WORLD')
    shading.background_type = enum_ok(shading.bl_rna, 'background_type', 'VIEWPORT')
    shading.background_color = BACKGROUND
    try:  # enum dinámico (depende de la configuración OCIO)
        scene.view_settings.view_transform = 'Standard'
    except TypeError as err:
        print(f'[ia] aviso: view_transform sin cambiar ({err})')
    fmt = scene.render.image_settings
    fmt.file_format = enum_ok(fmt.bl_rna, 'file_format', 'PNG')
    fmt.color_mode = 'RGB'
    fmt.compression = 90


def _camera(name, location, target, ortho_scale=None, lens=50.0, clip=(1.0, 10000.0)):
    data = bpy.data.cameras.new(name)
    if ortho_scale:
        data.type = 'ORTHO'
        data.ortho_scale = ortho_scale
    else:
        data.lens = lens
    data.clip_start, data.clip_end = clip
    cam = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(cam)
    cam.location = location
    cam.rotation_euler = (Vector(target) - Vector(location)).to_track_quat('-Z', 'Y').to_euler()
    return cam


def _label(cam, text, aspect, dist, collection):
    curve = bpy.data.curves.new('Label', type='FONT')
    curve.body = text
    tmp = bpy.data.objects.new('LabelTmp', curve)
    bpy.context.scene.collection.objects.link(tmp)
    half_w = cam.data.ortho_scale / 2 if cam.data.type == 'ORTHO' else dist * math.tan(cam.data.angle / 2)
    half_h = half_w / aspect
    curve.size = half_w * 0.062
    bpy.context.view_layer.update()
    mesh = bpy.data.meshes.new_from_object(tmp.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    bpy.data.objects.remove(tmp)
    obj = _flat_mesh('Label_' + cam.name, [v.co for v in mesh.vertices], [tuple(p.vertices) for p in mesh.polygons],
                     TEXT_COLOR, collection)
    obj.parent = cam
    obj.location = (-half_w * 0.95, half_h * 0.95 - curve.size, -dist)
    return obj


def _markers(objs, radius, collection):
    for obj in objs:
        for child in [c for c in obj.children if c.name.startswith('SOCKET_')]:
            c = (obj.matrix_world @ child.matrix_local).translation
            verts = [c + Vector(d) * radius for d in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))]
            faces = [(0, 2, 4), (2, 1, 4), (1, 3, 4), (3, 0, 4), (2, 0, 5), (1, 2, 5), (3, 1, 5), (0, 3, 5)]
            _flat_mesh('Marker_' + child.name, verts, faces, MARKER_COLOR, collection)


def _nice_bar(length):
    for step in (1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000):
        if step >= length * 0.3:
            return step
    return 1000


def _fit_distance(pts, center, direction, aspect, lens=50.0, sensor=36.0, margin=1.06):
    """Distancia de la cámara en perspectiva para que todos los vértices entren (con hueco arriba para el rótulo)."""
    rot = (-direction).to_track_quat('-Z', 'Y').to_matrix()
    right, up = rot.col[0], rot.col[1]
    tan_h = (sensor / 2) / lens
    tan_v = tan_h / aspect * 0.8
    rel_pts = pts - np.array(center[:])
    x = np.abs(rel_pts @ np.array(right[:]))
    y = np.abs(rel_pts @ np.array(up[:]))
    z = rel_pts @ np.array(direction[:])
    return float(max((z + x / tan_h).max(), (z + y / tan_v).max()) * margin)


def _render(cam, width, height, path):
    scene = bpy.context.scene
    scene.camera = cam
    for obj in scene.objects:
        if obj.name.startswith('Label_'):
            obj.hide_render = obj.parent != cam
        elif obj.name == 'ScaleBar':
            obj.hide_render = cam.name != 'CamSide'
    scene.render.resolution_x, scene.render.resolution_y = width, height
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(path)
    px = np.array(img.pixels[:], dtype=np.float32).reshape(height, width, 4)
    bpy.data.images.remove(img)
    return px


def render_sheet(objs, palette, title, path, collection):
    """3 vistas (3/4, frente desde +X y lado derecho) de todas las piezas montadas, con sockets en magenta."""
    _setup_render()
    for o in objs:
        preview_colors(o, palette)
    pts = np.array([(o.matrix_world @ v.co)[:] for o in objs for v in o.data.vertices])
    lo, hi = pts.min(axis=0), pts.max(axis=0)
    center, size = (lo + hi) / 2, hi - lo
    radius = float(np.linalg.norm(size) / 2)
    _markers(objs, max(radius * 0.035, 0.6), collection)
    bar = _nice_bar(max(size[0], size[2]))
    bar_x0 = center[0] - bar / 2  # centrada: cabe en la vista lateral aunque la pieza sea estrecha en X
    bar_z = lo[2] - max(size[2] * 0.06, radius * 0.04)
    thick = max(radius * 0.012, 0.25)
    _flat_mesh('ScaleBar', [(bar_x0, -thick, bar_z - thick), (bar_x0 + bar, -thick, bar_z - thick),
                            (bar_x0 + bar, -thick, bar_z + thick), (bar_x0, -thick, bar_z + thick)],
               [(0, 1, 2, 3)], TEXT_COLOR, collection)
    aspect = PANEL_W / PANEL_H
    far = radius * 6
    clip = (radius * 0.02, radius * 20)
    fit = 1.3
    front_scale = max(size[1], (size[2] + (lo[2] - bar_z)) * aspect) * fit
    side_scale = max(size[0], (size[2] + (lo[2] - bar_z)) * aspect) * fit
    c = Vector(center)
    d34 = Vector((1.0, -1.15, 0.8)).normalized()
    cam34 = _camera('Cam34', c + d34 * _fit_distance(pts, c, d34, aspect), c, clip=clip)
    views = [
        (cam34, title),
        (_camera('CamFront', c + Vector((far, 0, 0)), c, front_scale, clip=clip), 'Frente (desde +X)'),
        (_camera('CamSide', c + Vector((0, -far, 0)), c, side_scale, clip=clip),
         f'Lado derecho (+X a la derecha) · barra = {bar:g} cm'),
    ]
    for cam, text in views:
        _label(cam, text, aspect, radius * 0.5 if cam.data.type == 'ORTHO' else radius * 0.3, collection)
    sheet = np.ones((PANEL_H, PANEL_W * len(views), 4), dtype=np.float32)
    with tempfile.TemporaryDirectory(prefix='ia_sheet_') as tmp:
        for i, (cam, _) in enumerate(views):
            sheet[:, i * PANEL_W:(i + 1) * PANEL_W] = _render(cam, PANEL_W, PANEL_H, os.path.join(tmp, f'v{i}.png'))
    out = bpy.data.images.new('sheet', PANEL_W * len(views), PANEL_H, alpha=False)
    out.pixels.foreach_set(sheet.ravel())
    out.filepath_raw = path
    out.file_format = 'PNG'
    out.save()
    return os.path.getsize(path)


# ── Asset completo ──────────────────────────────────────────────────────────

def run_asset(asset, generator_rel):
    """Construye, exporta, dibuja y valida un asset. Devuelve (manifest, lista de fallos)."""
    folder = os.path.join(LIBRARY_ROOT, asset['category'], asset['slug'])
    os.makedirs(folder, exist_ok=True)
    export, sheet_col = setup_scene()
    mat = build_material(asset['palette'])
    parts, fails = [], []
    objs = []
    for part in asset['parts']:
        b = part['build']()
        obj = mesh_object(part['name'], b, mat, export)
        socks = socket_objects(obj, b.sockets, export)
        ucx = collision_objects(obj, b.collision, export)
        fbx = os.path.join(folder, part['name'] + '.fbx')
        export_fbx([obj] + socks + ucx, fbx)
        for sock in socks:  # nombres únicos en el .blend: otra pieza puede usar el mismo socket
            sock.name = f'{sock.name}__{part["name"]}'
        for u in ucx:
            u.hide_set(True)
            u.hide_render = True
        lo, hi = bbox_cm(obj)
        checks = mesh_checks(obj)
        info = {'name': part['name'], 'role': part.get('role', ''), 'fbx': rel(fbx), 'tris': tris(obj),
                'budget_tris': part['budget'], 'bbox_cm': {'min': lo, 'max': hi,
                                                           'size': [round(h - l, 2) for l, h in zip(lo, hi)]},
                'pivot': part['pivot'], 'sockets_cm_deg': {k: {'loc': [round(x, 2) for x in v[0]],
                                                               'rot': [round(x, 2) for x in v[1]]}
                                                           for k, v in b.sockets.items()},
                'collision_ucx': len(ucx), 'checks': checks}
        fails += _part_fails(part, info)
        parts.append(info)
        obj.location = part.get('preview_loc', (0, 0, 0))
        obj.rotation_euler = Euler(tuple(math.radians(a) for a in part.get('preview_rot', (0, 0, 0))), 'XYZ')
        objs.append(obj)
    total = sum(p['tris'] for p in parts)
    if total > asset['budget']:
        fails.append(f"total {total} tris > presupuesto {asset['budget']}")
    blend = os.path.join(folder, asset['slug'] + '.blend')
    for o in objs:
        o.location, o.rotation_euler = (0, 0, 0), (0, 0, 0)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=blend, compress=True)
    for o, part in zip(objs, asset['parts']):
        o.location = part.get('preview_loc', (0, 0, 0))
        o.rotation_euler = Euler(tuple(math.radians(a) for a in part.get('preview_rot', (0, 0, 0))), 'XYZ')
    bpy.context.view_layer.update()
    sheet = os.path.join(folder, asset['slug'] + '_lamina.png')
    title = f"{asset['title']} · {total}/{asset['budget']} tris · borrador IA"
    sheet_bytes = render_sheet(objs, asset['palette'], title, sheet, sheet_col)
    if sheet_bytes >= MAX_SHEET_BYTES:
        fails.append(f'lámina de {sheet_bytes / 1024:.0f} KB (>= 1 MB)')
    for info in parts:  # lo que de verdad llega a Unreal
        re = reimport_fbx(os.path.join(REPO_ROOT, info['fbx']))
        info['fbx_reimport'] = re
        fails += _fbx_fails(info, re)
    manifest = {
        'name': asset['slug'], 'title': asset['title'], 'category': asset['category'],
        'status': 'borrador IA', 'status_note': 'Borrador para que el equipo de modelado lo rehaga; no importado a Content.',
        'prompt': asset['prompt'],
        'tool': {'requested': 'Hunyuan3D / Tripo / Rodin vía Blender MCP', 'used': 'bpy procedural',
                 'reason': 'Blender MCP no disponible en la sesión que generó la biblioteca; forma construida por script.'},
        'generator': {'script': generator_rel, 'function': [p['build'].__name__ for p in asset['parts']],
                      'command': 'powershell -File Art/Library/IA/_pipeline/build_library.ps1 -Only ' + asset['slug']},
        'blender': bpy.app.version_string,
        'license': 'Original de Tortunabo, generado por script (sin fuentes externas)',
        'source_url': None,
        'style_reference': ['Source/Tortunabo/Private/World/Beach/TN_BeachCritterMeshes.h (tanque de juguete)'],
        'budget_tris': asset['budget'], 'tris_total': total, 'parts': parts,
        'blend': rel(blend), 'sheet': rel(sheet), 'sheet_kb': round(sheet_bytes / 1024, 1),
        'material': {'slot': MATERIAL_NAME, 'vertex_color': 'Zone',
                     'decode': 'Base = lerp(lerp(lerp(lerp(Trim, Paint, R), Detail, G), Dark, B), Light, R*G*B) * A',
                     'palette_srgb': {k: f'#{v:06X}' for k, v in asset['palette'].items()},
                     'textures': 'ninguna'},
        'unreal_import': {'units': 'cm (UnitScaleFactor 1), escala 1', 'axes': '+X delante, Z arriba',
                          'settings': 'Static Mesh; Combine Meshes on; Import Vertex Color = Replace; '
                                      'Normal Import = Import Normals; Auto Generate Collision off (UCX_ del FBX '
                                      'si lo hay); sockets desde los empties SOCKET_*'},
        'notes': asset.get('notes', ''),
        'validation': {'passed': not fails, 'failures': fails},
        'generated': datetime.date.today().isoformat(),
    }
    with open(os.path.join(folder, 'manifest.json'), 'w', encoding='utf-8') as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
    return manifest, fails


def _part_fails(part, info):
    fails = []
    c = info['checks']
    name = info['name']
    if info['tris'] > part['budget']:
        fails.append(f"{name}: {info['tris']} tris > {part['budget']}")
    for key in ('non_manifold_edges', 'degenerate_faces', 'loose_verts', 'inverted_pieces'):
        if c[key]:
            fails.append(f'{name}: {key} = {c[key]}')
    if c['material_slots'] != 1:
        fails.append(f"{name}: {c['material_slots']} materiales")
    lo, hi = info['bbox_cm']['min'], info['bbox_cm']['max']
    if part['pivot'] == 'base' and abs(lo[2]) > 0.05:
        fails.append(f'{name}: origen en la base pero z mínima = {lo[2]}')
    if part['pivot'] != 'base' and not all(l - 0.05 <= 0.0 <= h + 0.05 for l, h in zip(lo, hi)):
        fails.append(f'{name}: el pivote ({part["pivot"]}) queda fuera de la caja {lo}..{hi}')
    for s in part.get('required_sockets', ()):
        if s not in info['sockets_cm_deg']:
            fails.append(f'{name}: falta el socket {s}')
    lim = part.get('size_cm')
    if lim:
        size = info['bbox_cm']['size']
        for axis, (a, b) in zip('XYZ', lim):
            k = 'XYZ'.index(axis)
            if not a <= size[k] <= b:
                fails.append(f'{name}: tamaño {axis} = {size[k]} cm fuera de [{a}, {b}]')
    return fails


def _fbx_fails(info, re):
    fails = []
    name = info['name']
    if abs(re['unit_scale_factor'] - 1.0) > 1e-6:
        fails.append(f"{name}: UnitScaleFactor {re['unit_scale_factor']} (se espera 1 = cm)")
    got = re['meshes'].get(name)
    if not got:
        return fails + [f'{name}: la malla no está en el FBX reimportado']
    for key in ('min', 'max'):
        exp = info['bbox_cm'][key]
        val = got['lo' if key == 'min' else 'hi']
        if any(abs(a - b) > DIM_TOLERANCE_CM for a, b in zip(exp, val)):
            fails.append(f'{name}: caja reimportada {key} {val} != {exp}')
    if 'Zone' not in got['vertex_colors']:
        fails.append(f'{name}: el FBX no trae el color de vértice Zone')
    want = sorted('SOCKET_' + s for s in info['sockets_cm_deg'])
    if re['sockets'] != want:
        fails.append(f"{name}: sockets del FBX {re['sockets']} != {want}")
    if len(re['ucx']) != info['collision_ucx']:
        fails.append(f"{name}: UCX en el FBX {len(re['ucx'])} != {info['collision_ucx']}")
    return fails
