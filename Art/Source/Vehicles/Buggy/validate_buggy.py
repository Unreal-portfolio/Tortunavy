"""Comprueba por script los FBX del buggy (lo que de verdad llega a Unreal); sale con código 1 si algo falla.

    blender -b --factory-startup --python-exit-code 1 --python Art/Source/Vehicles/Buggy/validate_buggy.py
"""
import os
import sys

import bmesh
import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_buggy as bb  # noqa: E402

from io_scene_fbx import parse_fbx  # noqa: E402

MAX_BODY_TRIS, MAX_TIRE_TRIS = 3000, 400
# Carrocería de HellYeah (SM_BuggyBody.fbx medido en Blender 5.2): x -202,5..210, y ±122,6, z 40..179 cm.
HY_BODY_DIMS_CM = (412.5, 245.2, 139.0)
HY_TIRE_DIMS_CM = (102.0, 35.0, 102.0)
TOLERANCE = 0.05
SOCKETS = ('Seat_Driver', 'Seat_Gunner', 'Muzzle_Gunner')

results = []


def check(name, ok, detail):
    results.append(ok)
    print(f"[validate] {'OK   ' if ok else 'FALLO'} {name}: {detail}")


# ── FBX en crudo: unidades y transformaciones ───────────────────────────────

def _props70(elem):
    block = next((e for e in elem.elems if e.id == b'Properties70'), None)
    return {} if block is None else {p.props[0]: p.props[4:] for p in block.elems}


def _fbx_raw(path):
    root, _ = parse_fbx.parse(path)
    settings = next(e for e in root.elems if e.id == b'GlobalSettings')
    unit = _props70(settings)[b'UnitScaleFactor'][0]
    objects = next(e for e in root.elems if e.id == b'Objects')
    models = {}
    for m in (e for e in objects.elems if e.id == b'Model'):
        name = m.props[1].split(b'\x00')[0].decode()
        p = _props70(m)
        models[name] = {'scale': tuple(p.get(b'Lcl Scaling', (1.0, 1.0, 1.0))),
                        'translation': tuple(p.get(b'Lcl Translation', (0.0, 0.0, 0.0)))}
    return unit, models


# ── Geometría importada ──────────────────────────────────────────────────────

def _import(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    return {o.name: o for o in bpy.data.objects}


def _bounds_cm(obj, offset=Vector()):
    pts = [(obj.matrix_world @ v.co) * 100.0 + offset for v in obj.data.vertices]
    lo = Vector([min(p[i] for p in pts) for i in range(3)])
    hi = Vector([max(p[i] for p in pts) for i in range(3)])
    return lo, hi


def _components(bm):
    seen, parts = set(), []
    for f in bm.faces:
        if f.index in seen:
            continue
        stack, part = [f], []
        seen.add(f.index)
        while stack:
            cur = stack.pop()
            part.append(cur)
            for e in cur.edges:
                for nb in e.link_faces:
                    if nb.index not in seen:
                        seen.add(nb.index)
                        stack.append(nb)
        parts.append(part)
    return parts


def _signed_volume(faces):
    vol = 0.0
    for f in faces:
        co = [v.co for v in f.verts]
        for i in range(1, len(co) - 1):
            vol += co[0].dot(co[i].cross(co[i + 1])) / 6.0
    return vol


def _topology(obj, label, require_manifold):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.faces.ensure_lookup_table()
    bad_edges = sum(1 for e in bm.edges if not e.is_manifold)
    bad_verts = sum(1 for v in bm.verts if not v.is_manifold)
    parts = _components(bm)
    inverted = sum(1 for p in parts if _signed_volume(p) <= 0.0)
    flipped = sum(1 for e in bm.edges if e.is_manifold and not e.is_contiguous)
    if require_manifold:
        check(f'{label} manifold', bad_edges == 0 and bad_verts == 0,
              f'aristas no manifold {bad_edges}, vértices no manifold {bad_verts}')
    check(f'{label} normales hacia fuera', inverted == 0 and flipped == 0,
          f'{len(parts)} piezas cerradas, {inverted} con volumen <= 0, {flipped} aristas con caras de sentido opuesto')
    bm.free()


def _zones(obj, label):
    mesh = obj.data
    attr = mesh.color_attributes.get('Zone') or (mesh.color_attributes[0] if len(mesh.color_attributes) else None)
    if attr is None:
        check(f'{label} color de vértice', False, 'sin atributo de color')
        return {}
    masks = {tuple(v): k for k, v in bb.bmx.ZONE_MASKS.items()}
    counts, mixed, unknown = {}, 0, 0
    for poly in mesh.polygons:
        cols = {tuple(round(c) for c in attr.data[li].color[:3]) for li in poly.loop_indices}
        raw = [attr.data[li].color[:3] for li in poly.loop_indices]
        if len(cols) != 1:
            mixed += 1
            continue
        col = cols.pop()
        if any(abs(c - round(c)) > 0.02 for rgb in raw for c in rgb) or col not in masks:
            unknown += 1
            continue
        counts[masks[col]] = counts.get(masks[col], 0) + 1
    check(f'{label} zonas de color', mixed == 0 and unknown == 0,
          f'caras por zona {counts}, mezcladas {mixed}, fuera de máscara {unknown}')
    return counts


def _material(obj, label):
    slots = [s.material.name.split('.')[0] for s in obj.material_slots if s.material]
    images = [i for i in bpy.data.images if i.size[0] > 0]
    big = [i.name for i in images if max(i.size) > 512]
    check(f'{label} material', slots == [bb.MATERIAL_NAME] and not big,
          f'ranuras {slots}, texturas {len(images)} (> 512 px: {big or "ninguna"})')


def _dims_check(label, lo, hi, ref):
    dims = hi - lo
    errs = [abs(d - r) / r for d, r in zip(dims, ref)]
    check(f'{label} bounding box ±5 % de HellYeah', max(errs) <= TOLERANCE,
          f'{dims.x:.1f} × {dims.y:.1f} × {dims.z:.1f} cm (ref {ref[0]} × {ref[1]} × {ref[2]}), '
          f'error máx {max(errs) * 100:.1f} %')


def validate_body():
    path = os.path.join(bb.EXPORT_DIR, bb.BODY_NAME + '.fbx')
    unit, models = _fbx_raw(path)
    body_raw = models.get(bb.BODY_NAME, {})
    check('FBX chasis en cm, escala 1', abs(unit - 1.0) < 1e-6 and all(abs(s - 1) < 1e-6 for s in body_raw.get('scale', (0,))),
          f'UnitScaleFactor {unit}, Lcl Scaling {body_raw.get("scale")}, Lcl Translation {body_raw.get("translation")}')
    objs = _import(path)
    body = objs[bb.BODY_NAME]
    tris = sum(len(p.vertices) - 2 for p in body.data.polygons)
    check('chasis triángulos', tris <= MAX_BODY_TRIS, f'{tris} <= {MAX_BODY_TRIS}')
    lo, hi = _bounds_cm(body)
    _dims_check('chasis', lo, hi, HY_BODY_DIMS_CM)
    check('chasis origen', abs(lo.z - 40.0) <= 2.0 and body.matrix_world.translation.length < 1e-4,
          f'origen en (0,0,0) = suelo del esqueleto; bajos a z {lo.z:.1f} cm (HellYeah 40), x {lo.x:.1f}..{hi.x:.1f}')
    _topology(body, 'chasis', True)
    counts = _zones(body, 'chasis')
    for zone in ('paint', 'detail', 'wheel', 'trim', 'light'):
        if zone not in counts:
            check(f'chasis zona {zone}', False, 'sin caras')
    _material(body, 'chasis')
    _front_is_plus_x(body)
    sockets = _sockets(objs, body)
    check('sin carga', not any('cargo' in n.lower() or n.startswith(('UBX_', 'UCX_')) for n in objs),
          f'objetos {sorted(objs)}')
    return lo, hi, sockets


def _front_is_plus_x(body):
    mesh = body.data
    attr = mesh.color_attributes[0]
    xs = [(body.matrix_world @ mesh.vertices[mesh.loops[p.loop_start].vertex_index].co).x * 100.0
          for p in mesh.polygons if all(c > 0.5 for c in attr.data[p.loop_start].color[:3])]
    mean = sum(xs) / len(xs) if xs else 0.0
    check('+X hacia delante', mean > 150.0, f'faros (zona luz) en x media {mean:.1f} cm')


def _sockets(objs, body):
    found = {}
    for name in SOCKETS:
        obj = objs.get('SOCKET_' + name)
        if obj is None:
            check(f'socket {name}', False, 'no está en el FBX')
            continue
        found[name] = obj.matrix_world.translation * 100.0
        rot = obj.matrix_world.to_euler()
        parent_ok = obj.parent == body
        check(f'socket {name}', parent_ok and max(abs(a) for a in rot) < 1e-3,
              f'({found[name].x:.1f}, {found[name].y:.1f}, {found[name].z:.1f}) cm, hijo del chasis {parent_ok}, giro {tuple(round(a, 4) for a in rot)}')
    if len(found) == len(SOCKETS):
        d, g, m = found['Seat_Driver'], found['Seat_Gunner'], found['Muzzle_Gunner']
        seats = bb.seat_layout()
        hip = seats['measure']['hip_above_floor'] * 100.0
        check('asientos a la altura de cadera', abs(d.z - (bb.FLOOR_Z * 100 + hip)) < 0.5 and abs(g.z - (bb.GUNNER_FLOOR_Z * 100 + hip)) < 0.5,
              f'cadera sentada a {hip:.1f} cm de los pies y {seats["measure"]["hip_above_seat"] * 100:.1f} cm del cojín; '
              f'conductora z {d.z:.1f}, artillera z {g.z:.1f}')
        verts, _ = bb.turtle_pose.gunner().to_buggy()
        gap = min((Vector(v) * 100.0 + g - m).length for v in verts)
        check('boca fuera de la artillera', gap >= 5.0, f'{gap:.1f} cm al vértice más cercano de la tortuga (>= 5)')
        check('artillera detrás y más alta', g.x < d.x and g.z > d.z and m.z > g.z,
              f'Δx {g.x - d.x:.1f} cm, Δz {g.z - d.z:.1f} cm; boca {m.z - g.z:.1f} cm sobre su cadera')
    return found


def validate_tire():
    path = os.path.join(bb.EXPORT_DIR, bb.TIRE_NAME + '.fbx')
    unit, models = _fbx_raw(path)
    check('FBX rueda en cm, escala 1', abs(unit - 1.0) < 1e-6 and all(abs(s - 1) < 1e-6 for s in models[bb.TIRE_NAME]['scale']),
          f'UnitScaleFactor {unit}, Lcl Scaling {models[bb.TIRE_NAME]["scale"]}')
    tire = _import(path)[bb.TIRE_NAME]
    tris = sum(len(p.vertices) - 2 for p in tire.data.polygons)
    check('rueda triángulos', tris <= MAX_TIRE_TRIS, f'{tris} <= {MAX_TIRE_TRIS}')
    lo, hi = _bounds_cm(tire)
    center = (lo + hi) / 2
    check('rueda pivote en el eje', center.length < 1.0 and tire.matrix_world.translation.length < 1e-4,
          f'centro de la caja ({center.x:.2f}, {center.y:.2f}, {center.z:.2f}) cm, eje a lo largo de Y')
    _dims_check('rueda', lo, hi, HY_TIRE_DIMS_CM)
    _topology(tire, 'rueda', False)
    _zones(tire, 'rueda')
    _material(tire, 'rueda')
    return lo, hi


def validate_assembly(body_lo, body_hi, tire_lo, tire_hi):
    """Chasis + ruedas en los huesos PhysWheel: toca el suelo en z 0 y la caja total cuadra con HellYeah."""
    wheels = [(x, s * y, z) for (x, y, z) in (bb.FRONT_AXLE, bb.REAR_AXLE) for s in (1, -1)]
    lo, hi = body_lo.copy(), body_hi.copy()
    for w in wheels:
        c = Vector(w) * 100.0
        lo = Vector([min(lo[i], c[i] + tire_lo[i]) for i in range(3)])
        hi = Vector([max(hi[i], c[i] + tire_hi[i]) for i in range(3)])
    check('conjunto apoyado en el suelo', abs(lo.z) <= 1.5, f'z mínima {lo.z:.2f} cm')
    dims = hi - lo
    print(f'[validate] conjunto con ruedas: {dims.x:.1f} × {dims.y:.1f} × {dims.z:.1f} cm '
          f'(x {lo.x:.1f}..{hi.x:.1f}, y {lo.y:.1f}..{hi.y:.1f}, z {lo.z:.1f}..{hi.z:.1f})')


def main():
    print(f'[validate] Blender {bpy.app.version_string}')
    body_lo, body_hi, _ = validate_body()
    tire_lo, tire_hi = validate_tire()
    validate_assembly(body_lo, body_hi, tire_lo, tire_hi)
    failed = results.count(False)
    print(f'[validate] {"OK" if not failed else "FALLO"}: {len(results) - failed}/{len(results)} comprobaciones')
    if failed:
        sys.exit(1)


if __name__ == '__main__':
    main()
