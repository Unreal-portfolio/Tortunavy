"""Primitivas low-poly de caras planas para la biblioteca de borradores (cm, +X delante, Z arriba).

Mismo estilo que el tanque de juguete de TN_BeachCritterMeshes.h: piezas cerradas (cada una manifold por sí misma)
que se acumulan en un solo bmesh, con una zona de color por cara. Las zonas se codifican en el color de vértice con
máscaras RGB; así un único material maestro decodifica cualquier prop:
    Base = lerp(lerp(lerp(lerp(Trim, Paint, R), Detail, G), Dark, B), Light, R*G*B) * A
(A = sombreado por cara).

`Builder.frame(matrix)` apila transformaciones: todo lo que se crea dentro se coloca con esa matriz, igual que los
sockets (`socket`) y las envolventes de colisión (`col_box`, `col_hull`).
"""
import math
from contextlib import contextmanager

import bmesh
from mathutils import Euler, Matrix, Vector

ZONE_MASKS = {
    'trim': (0.0, 0.0, 0.0),
    'paint': (1.0, 0.0, 0.0),
    'detail': (0.0, 1.0, 0.0),
    'dark': (0.0, 0.0, 1.0),
    'light': (1.0, 1.0, 1.0),
}
ZONE_IDS = {name: i + 1 for i, name in enumerate(ZONE_MASKS)}
ZONE_NAMES = {i: name for name, i in ZONE_IDS.items()}
IDENTITY = Matrix.Identity(4)


def mirror_y(points):
    """Duplica cada punto con Y cambiada de signo (piezas simétricas respecto al plano XZ)."""
    return [p for x, y, z in points for p in ((x, y, z), (x, -y, z))]


def axis_matrix(origin, axis, up_hint=(0.0, 0.0, 1.0)):
    """Matriz que lleva el eje local Z a `axis` y el origen local a `origin`."""
    axis = Vector(axis).normalized()
    hint = Vector(up_hint)
    if abs(axis.dot(hint.normalized())) > 0.99:
        hint = Vector((1.0, 0.0, 0.0)) if abs(axis.x) < 0.9 else Vector((0.0, 1.0, 0.0))
    x = hint.cross(axis).normalized()
    y = axis.cross(x)
    rot = Matrix((x, y, axis)).transposed().to_4x4()
    return Matrix.Translation(Vector(origin)) @ rot


def rot_x(deg):
    return Matrix.Rotation(math.radians(deg), 4, 'X')


def rot_y(deg):
    return Matrix.Rotation(math.radians(deg), 4, 'Y')


def rot_z(deg):
    return Matrix.Rotation(math.radians(deg), 4, 'Z')


def move(x, y, z):
    return Matrix.Translation((x, y, z))


class Builder:
    """Acumula piezas cerradas en un bmesh, con zona y sombreado por cara, sockets y envolventes de colisión."""

    def __init__(self):
        self.bm = bmesh.new()
        self.zone = self.bm.faces.layers.int.new('zone')
        self.shade = self.bm.faces.layers.float.new('shade')
        self._stack = [IDENTITY.copy()]
        self.sockets = {}
        self.collision = []

    # ── Transformaciones ────────────────────────────────────────────────────
    @contextmanager
    def frame(self, matrix):
        self._stack.append(self._stack[-1] @ matrix)
        try:
            yield self
        finally:
            self._stack.pop()

    @property
    def matrix(self):
        return self._stack[-1]

    def _place(self, verts):
        mat = self.matrix
        if mat != IDENTITY:
            for v in verts:
                v.co = mat @ v.co

    def _tag(self, faces, zone, shade):
        if zone not in ZONE_IDS:
            raise ValueError(f'zona desconocida: {zone}')
        for face in faces:
            face[self.zone] = ZONE_IDS[zone]
            face[self.shade] = shade
        return faces

    @staticmethod
    def _faces_of(verts):
        return {f for v in verts for f in v.link_faces}

    # ── Metadatos ───────────────────────────────────────────────────────────
    def socket(self, name, location, rotation_deg=(0.0, 0.0, 0.0)):
        """Socket de Unreal (empty SOCKET_<name> en el FBX). La rotación es relativa al marco actual."""
        loc = self.matrix @ Vector(location)
        rot = (self.matrix.to_3x3().normalized() @ _euler_matrix(rotation_deg)).to_euler()
        self.sockets[name] = (tuple(loc), tuple(math.degrees(a) for a in rot))

    def col_box(self, lo, hi):
        lo, hi = Vector(lo), Vector(hi)
        corners = [(x, y, z) for x in (lo.x, hi.x) for y in (lo.y, hi.y) for z in (lo.z, hi.z)]
        self.col_hull(corners)

    def col_hull(self, points):
        self.collision.append([tuple(self.matrix @ Vector(p)) for p in points])

    # ── Primitivas ──────────────────────────────────────────────────────────
    def box(self, lo, hi, zone, shade=1.0):
        lo, hi = Vector(lo), Vector(hi)
        size = hi - lo
        mat = Matrix.Translation((lo + hi) / 2) @ Matrix.Diagonal((size.x, size.y, size.z, 1.0))
        verts = bmesh.ops.create_cube(self.bm, size=1.0, matrix=mat)['verts']
        self._place(verts)
        return self._tag(self._faces_of(verts), zone, shade)

    def hull(self, points, zone, shade=1.0):
        """Envolvente convexa de los puntos: pieza facetada y cerrada."""
        unique = list(dict.fromkeys(tuple(round(c, 4) for c in p) for p in points))  # mirror_y duplica y = 0
        verts = [self.bm.verts.new(p) for p in unique]
        res = bmesh.ops.convex_hull(self.bm, input=verts)
        loose = list({g for g in res['geom_unused'] + res['geom_interior'] if isinstance(g, bmesh.types.BMVert)})
        if loose:
            bmesh.ops.delete(self.bm, geom=loose, context='VERTS')
        faces = {g for g in res['geom'] if isinstance(g, bmesh.types.BMFace)}
        self._place({v for f in faces for v in f.verts})
        return self._tag(faces, zone, shade)

    def cyl(self, p0, p1, r, zone, seg=8, r1=None, shade=1.0, extend=0.0, phase=0.0):
        """Cilindro (o tronco de cono si r1) cerrado de p0 a p1; r1 = 0 da un cono con punta."""
        r1 = r if r1 is None else r1
        v0, v1 = Vector(p0), Vector(p1)
        axis = (v1 - v0).normalized()
        a, b = v0 - axis * extend, v1 + axis * extend
        frame = axis_matrix(a, axis)
        ring0 = [frame @ Vector((r * math.cos(t), r * math.sin(t), 0.0)) for t in _angles(seg, phase)]
        length = (b - a).length
        if r1 <= 1e-6:
            return self.hull(ring0 + [b], zone, shade)
        ring1 = [frame @ Vector((r1 * math.cos(t), r1 * math.sin(t), length)) for t in _angles(seg, phase)]
        return self.loft([ring0, ring1], zone, shade)

    def loft(self, rings, zone, shade=1.0, zone_of_band=None, cap=True):
        """Une anillos con el mismo número de puntos (en orden) y tapa los extremos con n-gonos."""
        vrings = [[self.bm.verts.new(p) for p in ring] for ring in rings]
        n = len(vrings[0])
        faces = []
        for bi in range(len(vrings) - 1):
            r0, r1 = vrings[bi], vrings[bi + 1]
            for s in range(n):
                t = (s + 1) % n
                f = self.bm.faces.new((r0[s], r0[t], r1[t], r1[s]))
                z, sh = zone_of_band(bi, s) if zone_of_band else (zone, shade)
                self._tag([f], z, sh)
                faces.append(f)
        if cap:
            for ring, rev in ((vrings[0], True), (vrings[-1], False)):
                f = self.bm.faces.new(list(reversed(ring)) if rev else ring)
                self._tag([f], zone, shade)
                faces.append(f)
        self._place({v for ring in vrings for v in ring})
        return faces

    def lathe(self, profile, zone, seg=8, origin=(0, 0, 0), axis=(0, 0, 1), phase=0.0, shade=1.0,
              zone_of_band=None):
        """Sólido de revolución. `profile` = [(radio, altura)] a lo largo de `axis`; radio 0 en una punta = polo.

        Sin polo, la punta se tapa con un n-gono. `zone_of_band(banda, segmento)` -> (zona, sombreado).
        """
        frame = axis_matrix(origin, axis)
        angs = _angles(seg, phase)
        zb = zone_of_band or (lambda band, s: (zone, shade))
        pole_a = profile[0][0] <= 1e-6
        pole_b = profile[-1][0] <= 1e-6
        inner = profile[1 if pole_a else 0: len(profile) - 1 if pole_b else len(profile)]
        rings = [[self.bm.verts.new(frame @ Vector((r * math.cos(a), r * math.sin(a), h))) for a in angs]
                 for r, h in inner]
        band0 = 1 if pole_a else 0
        new_verts = [v for ring in rings for v in ring]
        for s in range(seg):
            t = (s + 1) % seg
            for bi in range(len(rings) - 1):
                f = self.bm.faces.new((rings[bi][s], rings[bi][t], rings[bi + 1][t], rings[bi + 1][s]))
                self._tag([f], *zb(band0 + bi, s))
        if pole_a:
            pa = self.bm.verts.new(frame @ Vector((0.0, 0.0, profile[0][1])))
            new_verts.append(pa)
            for s in range(seg):
                self._tag([self.bm.faces.new((pa, rings[0][(s + 1) % seg], rings[0][s]))], *zb(0, s))
        else:
            self._tag([self.bm.faces.new(list(reversed(rings[0])))], *zb(0, 0))
        last = len(profile) - 2
        if pole_b:
            pb = self.bm.verts.new(frame @ Vector((0.0, 0.0, profile[-1][1])))
            new_verts.append(pb)
            for s in range(seg):
                self._tag([self.bm.faces.new((pb, rings[-1][s], rings[-1][(s + 1) % seg]))], *zb(last, s))
        else:
            self._tag([self.bm.faces.new(rings[-1])], *zb(last, 0))
        self._place(new_verts)

    def sphere(self, center, r, zone, seg=8, rings=5, shade=1.0, zone_of_band=None, phase=0.0, squash=1.0):
        prof = [(r * math.sin(math.pi * i / rings), -r * squash * math.cos(math.pi * i / rings))
                for i in range(rings + 1)]
        prof[0], prof[-1] = (0.0, prof[0][1]), (0.0, prof[-1][1])
        self.lathe(prof, zone, seg, origin=center, phase=phase, shade=shade, zone_of_band=zone_of_band)

    def torus(self, center, axis, major, minor, zone, major_seg=8, minor_seg=4, shade=1.0, phase=0.0,
              minor_phase=0.0):
        frame = axis_matrix(center, axis)
        rings = []
        for i in range(major_seg):
            a = phase + 2 * math.pi * i / major_seg
            radial = Vector((math.cos(a), math.sin(a), 0.0))
            ring = []
            for j in range(minor_seg):
                b = minor_phase + 2 * math.pi * j / minor_seg
                local = radial * (major + minor * math.cos(b)) + Vector((0.0, 0.0, minor * math.sin(b)))
                ring.append(self.bm.verts.new(frame @ local))
            rings.append(ring)
        faces = []
        for i in range(major_seg):
            r0, r1 = rings[i], rings[(i + 1) % major_seg]
            for j in range(minor_seg):
                k = (j + 1) % minor_seg
                faces.append(self.bm.faces.new((r0[j], r1[j], r1[k], r0[k])))
        self._place({v for ring in rings for v in ring})
        return self._tag(faces, zone, shade)

    def prism(self, poly, depth, zone, shade=1.0, side_zone=None):
        """Extruye un polígono 2D (u, v) -> (X, Z) de grosor `depth` centrado en Y=0 (usar `frame` para colocarlo).

        Admite polígonos cóncavos (las tapas se triangulan en `finish`).
        """
        half = depth / 2
        front = [self.bm.verts.new((u, -half, v)) for u, v in poly]
        back = [self.bm.verts.new((u, half, v)) for u, v in poly]
        faces = [self.bm.faces.new(front), self.bm.faces.new(list(reversed(back)))]
        self._tag(faces, zone, shade)
        n = len(poly)
        for i in range(n):
            j = (i + 1) % n
            f = self.bm.faces.new((front[j], front[i], back[i], back[j]))
            self._tag([f], side_zone or zone, shade)
            faces.append(f)
        self._place(front + back)
        return faces

    def sweep(self, path, radius, zone, seg=6, shade=1.0, zone_of_band=None, phase=0.0, end_dirs=None):
        """Tubo cerrado a lo largo de una polilínea; `radius` escalar o lista (uno por punto).

        `end_dirs` = (dirección inicial, dirección final) fuerza el plano de las tapas (p. ej. apoyar en el suelo).
        """
        pts = [Vector(p) for p in path]
        radii = radius if isinstance(radius, (list, tuple)) else [radius] * len(pts)
        tangents = []
        for i in range(len(pts)):
            a = pts[max(i - 1, 0)]
            b = pts[min(i + 1, len(pts) - 1)]
            tangents.append((b - a).normalized())
        if end_dirs:
            tangents[0], tangents[-1] = Vector(end_dirs[0]).normalized(), Vector(end_dirs[1]).normalized()
        normal = axis_matrix((0, 0, 0), tangents[0]).col[0].xyz
        rings = []
        for i, (p, t) in enumerate(zip(pts, tangents)):
            normal = (normal - t * normal.dot(t)).normalized()  # transporte paralelo
            binormal = t.cross(normal)
            rings.append([p + (normal * math.cos(a) + binormal * math.sin(a)) * radii[i] for a in _angles(seg, phase)])
        return self.loft(rings, zone, shade, zone_of_band=zone_of_band)

    def slab_grid(self, u0, u1, v0, v1, depth, nu, nv, zone_of_cell, shade=1.0, side_zone='trim'):
        """Placa (u -> X, v -> Z, grosor en Y) con las dos caras divididas en celdas de zona propia (damero, rótulo)."""
        us = [u0 + (u1 - u0) * i / nu for i in range(nu + 1)]
        vs = [v0 + (v1 - v0) * j / nv for j in range(nv + 1)]
        half = depth / 2
        grids = {}
        for side, y in (('f', -half), ('b', half)):
            grids[side] = [[self.bm.verts.new((u, y, v)) for v in vs] for u in us]
        for i in range(nu):
            for j in range(nv):
                zone = zone_of_cell(i, j)
                f = grids['f']
                self._tag([self.bm.faces.new((f[i][j], f[i + 1][j], f[i + 1][j + 1], f[i][j + 1]))], zone, shade)
                bk = grids['b']
                self._tag([self.bm.faces.new((bk[i][j], bk[i][j + 1], bk[i + 1][j + 1], bk[i + 1][j]))], zone, shade)
        perim = ([(i, 0) for i in range(nu)] + [(nu, j) for j in range(nv)] +
                 [(i, nv) for i in range(nu, 0, -1)] + [(0, j) for j in range(nv, 0, -1)])
        for k in range(len(perim)):
            a, b = perim[k], perim[(k + 1) % len(perim)]
            fa, fb = grids['f'][a[0]][a[1]], grids['f'][b[0]][b[1]]
            ba, bb = grids['b'][a[0]][a[1]], grids['b'][b[0]][b[1]]
            self._tag([self.bm.faces.new((fb, fa, ba, bb))], side_zone, shade)
        self._place([v for side in grids.values() for col in side for v in col])

    # ── Cierre ──────────────────────────────────────────────────────────────
    def finish(self):
        """Triangula n-gonos, orienta las normales hacia fuera por pieza y devuelve el bmesh."""
        ngons = [f for f in self.bm.faces if len(f.verts) > 4]
        if ngons:
            bmesh.ops.triangulate(self.bm, faces=ngons)
        bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces[:])
        for part in components(self.bm):
            if signed_volume(part) < 0.0:
                bmesh.ops.reverse_faces(self.bm, faces=part)
        return self.bm


def _angles(seg, phase=0.0):
    return [phase + 2 * math.pi * s / seg for s in range(seg)]


def _euler_matrix(deg):
    return Euler(tuple(math.radians(a) for a in deg), 'XYZ').to_matrix()


def components(bm):
    """Piezas conexas (por aristas) del bmesh, como listas de caras."""
    seen, parts = set(), []
    for face in bm.faces:
        if face in seen:
            continue
        stack, part = [face], []
        seen.add(face)
        while stack:
            cur = stack.pop()
            part.append(cur)
            for edge in cur.edges:
                for nb in edge.link_faces:
                    if nb not in seen:
                        seen.add(nb)
                        stack.append(nb)
        parts.append(part)
    return parts


def signed_volume(faces):
    vol = 0.0
    for face in faces:
        co = [v.co for v in face.verts]
        for i in range(1, len(co) - 1):
            vol += co[0].dot(co[i].cross(co[i + 1])) / 6.0
    return vol
