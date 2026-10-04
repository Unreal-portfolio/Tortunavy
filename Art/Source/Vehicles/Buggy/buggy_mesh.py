"""Primitivas low-poly de caras planas sobre un bmesh, con zona de color y sombreado por cara.

Estilo de referencia: el tanque de juguete de TN_BeachCritterMeshes.h (cajas, cilindros de 8-12 lados, caras
planas, color por cara y un radio claro en la rueda para que se vea girar). Todo se construye en metros; build_buggy
lo pasa a centímetros al volcarlo a la malla.
"""
import math

import bmesh
import mathutils
from mathutils import Matrix, Vector

# Zonas de color, codificadas en el color de vértice como máscaras RGB (A = sombreado por cara).
# Unreal (M_TN_Buggy) decodifica: Base = lerp(lerp(lerp(lerp(Trim, Paint, R), Detail, G), Wheel, B), Light, R*G*B) * A.
ZONE_MASKS = {
    'trim': (0.0, 0.0, 0.0),
    'paint': (1.0, 0.0, 0.0),
    'detail': (0.0, 1.0, 0.0),
    'wheel': (0.0, 0.0, 1.0),
    'light': (1.0, 1.0, 1.0),
}
ZONE_IDS = {name: i + 1 for i, name in enumerate(ZONE_MASKS)}
ZONE_NAMES = {i: name for name, i in ZONE_IDS.items()}


class Builder:
    """Acumula piezas cerradas (cada una manifold por sí misma) en un solo bmesh."""

    def __init__(self):
        self.bm = bmesh.new()
        self.zone = self.bm.faces.layers.int.new('zone')
        self.shade = self.bm.faces.layers.float.new('shade')

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

    def box(self, lo, hi, zone, shade=1.0):
        lo, hi = Vector(lo), Vector(hi)
        size = hi - lo
        mat = Matrix.Translation((lo + hi) / 2) @ Matrix.Diagonal((size.x, size.y, size.z, 1.0))
        verts = bmesh.ops.create_cube(self.bm, size=1.0, matrix=mat)['verts']
        return self._tag(self._faces_of(verts), zone, shade)

    def hull(self, points, zone, shade=1.0):
        """Envolvente convexa de los puntos: pieza facetada y cerrada (capó, pontones, carrocería)."""
        verts = [self.bm.verts.new(p) for p in points]
        res = bmesh.ops.convex_hull(self.bm, input=verts)
        loose = [g for g in res['geom_unused'] + res['geom_interior'] if isinstance(g, bmesh.types.BMVert)]
        if loose:
            bmesh.ops.delete(self.bm, geom=loose, context='VERTS')
        faces = {g for g in res['geom'] if isinstance(g, bmesh.types.BMFace)}
        return self._tag(faces, zone, shade)

    def tube(self, p0, p1, radius, zone, segments=8, shade=1.0, extend=0.0):
        """Cilindro cerrado de p0 a p1 (alargado `extend` por cada punta para tapar las uniones)."""
        v0, v1 = Vector(p0), Vector(p1)
        axis = v1 - v0
        length = axis.length + 2 * extend
        rot = axis.normalized().to_track_quat('Z', 'Y').to_matrix().to_4x4()
        mat = Matrix.Translation((v0 + v1) / 2) @ rot
        verts = bmesh.ops.create_cone(self.bm, cap_ends=True, cap_tris=False, segments=segments,
                                      radius1=radius, radius2=radius, depth=length, matrix=mat)['verts']
        return self._tag(self._faces_of(verts), zone, shade)

    def hull_along(self, section_a, section_b, zone, shade=1.0):
        """Envolvente de dos secciones (listas de puntos): loft convexo entre ellas."""
        return self.hull(list(section_a) + list(section_b), zone, shade)

    def torus(self, center, axis, major, minor, zone, major_seg=8, minor_seg=4, shade=1.0):
        rot = Vector(axis).normalized().to_track_quat('Z', 'Y').to_matrix()
        rings = []
        for i in range(major_seg):
            a = 2 * math.pi * i / major_seg
            radial = Vector((math.cos(a), math.sin(a), 0.0))
            ring = []
            for j in range(minor_seg):
                b = 2 * math.pi * j / minor_seg
                local = radial * (major + minor * math.cos(b)) + Vector((0.0, 0.0, minor * math.sin(b)))
                ring.append(self.bm.verts.new(Vector(center) + rot @ local))
            rings.append(ring)
        faces = []
        for i in range(major_seg):
            r0, r1 = rings[i], rings[(i + 1) % major_seg]
            for j in range(minor_seg):
                k = (j + 1) % minor_seg
                faces.append(self.bm.faces.new((r0[j], r1[j], r1[k], r0[k])))
        return self._tag(faces, zone, shade)

    def lathe(self, profile, segments, zone_of_band, phase=0.0):
        """Sólido de revolución alrededor de Y. `profile` = [(radio, y)], con radio 0 en las dos puntas (polos).

        `zone_of_band(band, seg)` devuelve (zona, sombreado) para la banda entre los puntos band y band+1.
        """
        if profile[0][0] != 0.0 or profile[-1][0] != 0.0:
            raise ValueError('el perfil del torno debe empezar y acabar en el eje')
        angles = [phase + 2 * math.pi * s / segments for s in range(segments)]
        pole_a = self.bm.verts.new((0.0, profile[0][1], 0.0))
        pole_b = self.bm.verts.new((0.0, profile[-1][1], 0.0))
        rings = [[self.bm.verts.new((r * math.cos(a), y, r * math.sin(a))) for a in angles] for r, y in profile[1:-1]]
        last_band = len(profile) - 2
        for s in range(segments):
            t = (s + 1) % segments
            self._tag([self.bm.faces.new((pole_a, rings[0][t], rings[0][s]))], *zone_of_band(0, s))
            for b in range(len(rings) - 1):
                face = self.bm.faces.new((rings[b][s], rings[b][t], rings[b + 1][t], rings[b + 1][s]))
                self._tag([face], *zone_of_band(b + 1, s))
            self._tag([self.bm.faces.new((pole_b, rings[-1][s], rings[-1][t]))], *zone_of_band(last_band, s))

    def finish(self, scale):
        """Normales hacia fuera por pieza y paso de metros a la unidad de la escena."""
        bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces[:])
        for part in _components(self.bm):  # red de seguridad: recalc falla en sólidos de revolución no convexos
            if _signed_volume(part) < 0.0:
                bmesh.ops.reverse_faces(self.bm, faces=part)
        bmesh.ops.scale(self.bm, vec=(scale, scale, scale), verts=self.bm.verts[:])
        return self.bm


def _components(bm):
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


def _signed_volume(faces):
    vol = 0.0
    for face in faces:
        co = [v.co for v in face.verts]
        for i in range(1, len(co) - 1):
            vol += co[0].dot(co[i].cross(co[i + 1])) / 6.0
    return vol


def slope_normal(p_back, p_front):
    """Normal hacia arriba de un plano inclinado a lo largo de X (para pegar franjas al capó)."""
    d = Vector(p_front) - Vector(p_back)
    return Vector((-d.z, 0.0, d.x)).normalized()


def offset(point, normal, dist):
    return tuple(Vector(point) + normal * dist)


def rot_y(point, angle):
    return tuple(Matrix.Rotation(angle, 3, 'Y') @ Vector(point))


__all__ = ['Builder', 'ZONE_MASKS', 'ZONE_IDS', 'ZONE_NAMES', 'slope_normal', 'offset', 'rot_y', 'mathutils']
