"""Trazado en planta de los circuitos de Rally por vueltas (#622): una secuencia de piezas con semilla que cierra
sobre sí misma.

Piezas (PIECE_KINDS): recta de salida (recta de velocidad con la parrilla 2 x 4), curvas peraltadas, horquillas,
chicanes, rectas con salto y rectas con cambio de rasante. Reglas de colocación:

  - la recta de salida va primero; después, en orden sorteado, las piezas de giro (curva, chicane o el zigzag de
    las dos horquillas) con la recta con elemento que las sigue: los saltos, detrás de una pieza lenta (horquilla o
    chicane), para llegar a ellos a velocidad media; los rasantes, detrás de curvas peraltadas (arrange);
  - entre dos piezas siempre hay un enlace recto de longitud libre (LINK_MIN_M o más);
  - el giro total es de +-360 grados: las curvas peraltadas giran todas hacia el lado del lazo (el lazo de base es
    convexo), la chicane tiene giro neto nulo y las dos horquillas (HAIRPIN_DEG, menos de 180 para que sus ramas se
    abran en V) giran en sentidos opuestos con una recta con elemento entre ellas (zigzag);
  - las longitudes libres (la recta de salida y los enlaces) se resuelven por mínimos cuadrados con cotas para que
    el lazo cierre; la curvatura se suaviza (sigma CURVATURE_SIGMA_M) para no tener quiebros de volante;
  - dos tramos del lazo que estén a más de SEPARATION_ARC_M por el arco quedan a SEPARATION_MIN_M o más en planta
    (sin cruces y con sitio para las dos barreras); si no, se repite el sorteo con el intento siguiente;
  - una recta de baches nunca va justo antes de una horquilla (#696): sus baches caerían en la frenada (con la
    semilla 6938, el piloto IA se pasó la horquilla). El orden que lo hace se descarta (bumps_before_hairpin).

Perfil «tierra» (#682, make_plan(seed, "tierra")): las mismas reglas y además cuatro saltos de tres formas
(rally_circuit_jumps: doble y cresta en el zigzag de las horquillas, la mesa tras la chicane y el salto largo sobre
hueco tras una curva peraltada, donde se llega rápido), dos rectas de baches (whoops y tabla de lavar) y un badén
con barro (rally_circuit_dirt), repartidos con los rasantes detrás de las curvas peraltadas, y banqueta de tierra
en las dos horquillas (peralte de BANQUETA_BANK_DEG y caballón por fuera). El perfil por defecto («dunas», #622)
no cambia: misma semilla, mismo trazado.

Coordenadas: X = Norte, Y = Este (terrain_vol/layout.py); rumbo psi desde el Norte hacia el Este (el yaw de
Unreal), positivo = giro a la derecha.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np
from scipy import ndimage
from scipy.optimize import lsq_linear

from .rally_circuit_dirt import BANQUETA_BANK_DEG, DIP_PIECE_M, BumpDesign

PIECE_KINDS = ("recta", "curva_peraltada", "horquilla", "chicane", "salto", "rasante", "baches", "baden")
PROFILES = ("dunas", "tierra")
LINK_MIN_M = 25.0
LINK_PREF_M = 35.0
MAIN_MIN_M = 170.0
MAIN_PREF_M = 190.0
START_LINE_M = 75.0              # la línea de salida, a esto del principio de la recta de salida
JUMP_APPROACH_M = 5.0            # recta fija antes de la rampa (más el enlace libre)
JUMP_RESERVE_M = 140.0           # rampa, mesa, recepción y escapatoria
CHICANE_GAP_M = 8.0
CURVATURE_SIGMA_M = 4.0
SEPARATION_ARC_M = 150.0
SEPARATION_MIN_M = 45.0
LENGTH_M = (1300.0, 2500.0)
MAX_SPAN_M = 820.0
MAX_ATTEMPTS = 60
ORDERINGS = 24
JUMPS = 3
BANKED_DEG = (45.0, 150.0)
HAIRPIN_DEG = (150.0, 170.0)         # menos de 180: las dos ramas de la horquilla se abren en V
CRESTS = 2
TIERRA_LENGTH_M = (1500.0, 3000.0)
TIERRA_MAX_SPAN_M = 950.0


@dataclass(frozen=True)
class Piece:
    kind: str
    radius_m: float = 0.0
    angle_deg: float = 0.0          # giro con signo (positivo a la derecha); en la chicane, el de cada mitad
    bank_deg: float = 0.0
    length_m: float = 0.0           # recta del rasante, de los baches y del badén
    variant: str = ""               # forma del salto (rally_circuit_jumps), patrón de baches o "banqueta" (#682)
    amplitude_m: float = 0.0        # baches: amplitud, longitud de onda y número de ondas
    wavelength_m: float = 0.0
    count: int = 0


@dataclass(frozen=True)
class Seg:
    kind: str                       # "S" recta o "A" arco
    length_m: float
    angle_deg: float = 0.0
    variable: bool = False
    min_m: float = 0.0
    pref_m: float = 0.0
    piece: int = -1                 # índice de la pieza (-1: enlace)


@dataclass
class Plan:
    seed: int
    attempt: int
    pieces: list[Piece]
    segs: list[Seg]
    pts: np.ndarray                 # eje cada ~1 m, empezando en la línea de salida, sin repetir el primero
    psi: np.ndarray                 # rumbo (rad)
    curvature: np.ndarray           # 1/m, positiva a la derecha
    step_m: float
    spans: list[tuple[int, float, float]] = field(default_factory=list)   # (pieza, s0, s1) desde la línea

    @property
    def length_m(self) -> float:
        return self.step_m * len(self.pts)

    @property
    def arc(self) -> np.ndarray:
        return self.step_m * np.arange(len(self.pts))


# ── Sorteo de piezas ─────────────────────────────────────────────────────────────
def _banked_angles(rng: np.random.Generator, count: int, target: float) -> list[float] | None:
    """count giros del mismo signo que suman target, de BANKED_DEG cada uno (el lazo de base queda convexo)."""
    for _ in range(60):
        mags = rng.uniform(0.7, 1.3, count)
        mags *= abs(target) / mags.sum()
        if (mags >= BANKED_DEG[0]).all() and (mags <= BANKED_DEG[1]).all():
            return [float(math.copysign(m, target)) for m in mags]
    return None


def draw_pieces(rng: np.random.Generator,
                profile: str = "dunas") -> tuple[list[Piece], list[Piece], tuple[Piece, Piece]] | None:
    """Curvas peraltadas y chicane, rectas con elemento y la pareja de horquillas (sin ordenar). Las curvas giran
    todas hacia el mismo lado (el del lazo) y las horquillas forman un zigzag hacia fuera, de giro neto casi nulo."""
    direction = 1.0 if rng.random() < 0.5 else -1.0
    first = rng.uniform(*HAIRPIN_DEG)
    second = first + rng.uniform(-8.0, 8.0)
    hairpins = (Piece("horquilla", rng.uniform(18.0, 22.0), -direction * first, rng.uniform(0.0, 4.0)),
                Piece("horquilla", rng.uniform(18.0, 22.0), direction * second, rng.uniform(0.0, 4.0)))
    banked = _banked_angles(rng, int(rng.integers(4, 6)), direction * (360.0 + first - second))
    if banked is None:
        return None
    turns = [Piece("curva_peraltada", rng.uniform(38.0, 70.0), a, rng.uniform(9.0, 15.0)) for a in banked]
    turns.append(Piece("chicane", rng.uniform(28.0, 40.0), (1 if rng.random() < 0.5 else -1) * rng.uniform(28.0, 40.0)))
    straights = [Piece("salto") for _ in range(JUMPS)]
    straights += [Piece("rasante", length_m=rng.uniform(75.0, 100.0)) for _ in range(CRESTS)]
    if profile == "tierra":
        return _tierra_pieces(rng, turns, straights, hairpins)
    return turns, straights, hairpins


def _tierra_pieces(rng: np.random.Generator, turns: list[Piece], straights: list[Piece],
                   hairpins: tuple[Piece, Piece]) -> tuple[list[Piece], list[Piece], tuple[Piece, Piece]]:
    """Perfil tierra (#682): banqueta en las horquillas, saltos con forma, baches y badén."""
    hairpins = tuple(Piece("horquilla", h.radius_m, h.angle_deg, float(rng.uniform(*BANQUETA_BANK_DEG)),
                           variant="banqueta") for h in hairpins)
    jumps = [Piece("salto", variant=v) for v in ("doble", "cresta", "mesa", "hueco")]
    crests = [p for p in straights if p.kind == "rasante"]
    bumps = []
    for pattern in ("whoops", "tabla_lavar"):
        b = BumpDesign.draw(pattern, rng)
        bumps.append(Piece("baches", length_m=b.piece_m, variant=pattern, amplitude_m=b.amplitude_m,
                           wavelength_m=b.wavelength_m, count=b.count))
    return turns, jumps + crests + bumps + [Piece("baden", length_m=DIP_PIECE_M, variant="barro")], hairpins


def arrange(rng: np.random.Generator, turns: list[Piece], straights: list[Piece],
            hairpins: tuple[Piece, Piece]) -> list[Piece]:
    """Recta de salida y, en orden sorteado, las piezas de giro con la recta con elemento que las sigue. Los saltos
    van detrás de una pieza lenta (horquilla o chicane: se llega a ellos a velocidad media, no a fondo): uno dentro
    del zigzag (entre las dos horquillas), otro tras él y otro tras la chicane. Los rasantes, tras curvas peraltadas
    sorteadas."""
    jumps = [p for p in straights if p.kind == "salto"]
    crests = [p for p in straights if p.kind == "rasante"]
    banked = [t for t in turns if t.kind == "curva_peraltada"]
    chicane = [t for t in turns if t.kind == "chicane"]
    if len(jumps) > JUMPS:
        return _arrange_tierra(rng, banked, chicane, straights, hairpins)
    units: list[list[Piece]] = [[hairpins[0], jumps[0], hairpins[1], jumps[1]], chicane + [jumps[2]]]
    with_crest = set(rng.choice(len(banked), size=len(crests), replace=False).tolist())
    crest_iter = iter(crests)
    units += [[t, next(crest_iter)] if i in with_crest else [t] for i, t in enumerate(banked)]
    seq = [Piece("recta")]
    for i in rng.permutation(len(units)):
        seq += units[i]
    return seq


def _arrange_tierra(rng: np.random.Generator, banked: list[Piece], chicane: list[Piece], straights: list[Piece],
                    hairpins: tuple[Piece, Piece]) -> list[Piece]:
    """Perfil tierra: zigzag con la doble y la cresta (se llega despacio, desde una horquilla), la mesa tras la
    chicane y, tras las curvas peraltadas (todas con algo detrás), el salto largo sobre hueco (el primero de su
    curva: se llega rápido), los rasantes, los baches y el badén."""
    jumps = {p.variant: p for p in straights if p.kind == "salto"}
    followers = [p for p in straights if p.kind in ("rasante", "baches", "baden")]
    units: list[list[Piece]] = [[hairpins[0], jumps["doble"], hairpins[1], jumps["cresta"]],
                                chicane + [jumps["mesa"]]]
    slots: list[list[Piece]] = [[] for _ in banked]
    order = [jumps["hueco"]] + [followers[i] for i in rng.permutation(len(followers))]
    for i, k in enumerate(rng.permutation(len(order))):
        slots[i % len(banked)].append(order[k])
    for slot in slots:
        slot.sort(key=lambda p: p.kind != "salto")
    units += [[t] + slot for t, slot in zip(banked, slots)]
    seq = [Piece("recta")]
    for i in rng.permutation(len(units)):
        seq += units[i]
    return seq


def piece_segs(index: int, piece: Piece) -> list[Seg]:
    if piece.kind == "recta":
        return [Seg("S", MAIN_PREF_M, variable=True, min_m=MAIN_MIN_M, pref_m=MAIN_PREF_M, piece=index)]
    if piece.kind in ("curva_peraltada", "horquilla"):
        return [Seg("A", piece.radius_m * math.radians(abs(piece.angle_deg)), piece.angle_deg, piece=index)]
    if piece.kind == "chicane":
        arc = piece.radius_m * math.radians(abs(piece.angle_deg))
        return [Seg("A", arc, piece.angle_deg, piece=index), Seg("S", CHICANE_GAP_M, piece=index),
                Seg("A", arc, -piece.angle_deg, piece=index)]
    if piece.kind == "salto":
        return [Seg("S", JUMP_APPROACH_M + JUMP_RESERVE_M, piece=index)]
    if piece.kind in ("rasante", "baches", "baden"):
        return [Seg("S", piece.length_m, piece=index)]
    raise ValueError(piece.kind)


def build_segs(pieces: list[Piece]) -> list[Seg]:
    segs: list[Seg] = []
    for i, piece in enumerate(pieces):
        segs += piece_segs(i, piece)
        segs.append(Seg("S", LINK_PREF_M, variable=True, min_m=LINK_MIN_M, pref_m=LINK_PREF_M))
    return segs


# ── Cierre ───────────────────────────────────────────────────────────────────────
def _headings(segs: list[Seg]) -> np.ndarray:
    """Rumbo (rad) al empezar cada tramo."""
    turns = np.radians([s.angle_deg if s.kind == "A" else 0.0 for s in segs])
    return np.concatenate([[0.0], np.cumsum(turns)[:-1]])


def _displacement(seg: Seg, psi0: float) -> np.ndarray:
    if seg.kind == "S":
        return seg.length_m * np.array([math.cos(psi0), math.sin(psi0)])
    a = math.radians(seg.angle_deg)
    r = seg.length_m / abs(a)
    chord = 2.0 * r * math.sin(abs(a) / 2.0)
    mid = psi0 + a / 2.0
    return chord * np.array([math.cos(mid), math.sin(mid)])


def close_loop(segs: list[Seg]) -> list[Seg] | None:
    """Longitudes de los tramos libres para que el lazo vuelva al origen (None si no hay solución con sus cotas)."""
    psi = _headings(segs)
    var = [i for i, s in enumerate(segs) if s.variable]
    fixed = sum((_displacement(s, psi[i]) for i, s in enumerate(segs) if not s.variable), np.zeros(2))
    A = np.array([[math.cos(psi[i]) for i in var], [math.sin(psi[i]) for i in var]])
    pref = np.array([segs[i].pref_m for i in var])
    weight = 200.0
    M = np.vstack([weight * A, np.diag(1.0 / pref)])
    rhs = np.concatenate([-weight * fixed, np.ones(len(var))])
    lo = np.array([segs[i].min_m for i in var])
    sol = lsq_linear(M, rhs, bounds=(lo, np.full(len(var), 700.0)), method="bvls")
    if np.hypot(*(A @ sol.x + fixed)) > 0.05:
        return None
    out = list(segs)
    for i, length in zip(var, sol.x):
        out[i] = Seg("S", float(length), variable=True, min_m=segs[i].min_m, pref_m=segs[i].pref_m, piece=segs[i].piece)
    return out


def sample(segs: list[Seg], turn_sign: float) -> tuple[np.ndarray, np.ndarray, np.ndarray, float, list]:
    """Eje cada ~1 m desde el principio del primer tramo: puntos, rumbo, curvatura y los (pieza, s0, s1)."""
    total = sum(s.length_m for s in segs)
    n = int(round(total))
    step = total / n
    s_mid = (np.arange(n) + 0.5) * step
    ends = np.cumsum([s.length_m for s in segs])
    idx = np.minimum(np.searchsorted(ends, s_mid), len(segs) - 1)
    k = np.array([math.radians(segs[i].angle_deg) / segs[i].length_m if segs[i].kind == "A" else 0.0 for i in idx])
    k = ndimage.gaussian_filter1d(k, CURVATURE_SIGMA_M / step, mode="wrap")
    k *= turn_sign * 2.0 * math.pi / (k.sum() * step)            # giro total exacto
    psi = np.concatenate([[0.0], np.cumsum(k * step)[:-1]])
    psi_mid = psi + 0.5 * k * step
    d = step * np.column_stack([np.cos(psi_mid), np.sin(psi_mid)])
    pts = np.vstack([[0.0, 0.0], np.cumsum(d, axis=0)[:-1]])
    error = np.cumsum(d, axis=0)[-1]                              # el último paso debería volver al origen
    pts -= (np.arange(n) / n)[:, None] * error[None, :]
    starts = ends - np.array([s.length_m for s in segs])
    spans = []
    for piece in sorted({s.piece for s in segs if s.piece >= 0}):
        mine = [i for i, s in enumerate(segs) if s.piece == piece]
        spans.append((piece, float(starts[mine[0]]), float(ends[mine[-1]])))
    return pts, psi, k, step, spans


def separation_ok(pts: np.ndarray, step: float) -> bool:
    sub = pts[::2]
    n = len(sub)
    d = np.hypot(sub[:, None, 0] - sub[None, :, 0], sub[:, None, 1] - sub[None, :, 1])
    i = np.arange(n)
    gap = np.abs(i[:, None] - i[None, :]) * 2.0 * step
    gap = np.minimum(gap, n * 2.0 * step - gap)
    return bool((d[gap > SEPARATION_ARC_M] >= SEPARATION_MIN_M).all())


def bumps_before_hairpin(pieces: list[Piece]) -> bool:
    """Si alguna recta de baches va justo antes (tras su enlace) de una horquilla: sus baches caerían en la frenada."""
    n = len(pieces)
    return any(p.kind == "baches" and pieces[(i + 1) % n].kind == "horquilla" for i, p in enumerate(pieces))


def _candidate(seed: int, attempt: int, pieces: list[Piece],
               profile: str = "dunas") -> tuple[float, Plan, list] | None:
    if bumps_before_hairpin(pieces):
        return None
    segs = close_loop(build_segs(pieces))
    if segs is None:
        return None
    turn_sign = 1.0 if sum(s.angle_deg for s in segs) > 0 else -1.0
    pts, psi, k, step, spans = sample(segs, turn_sign)
    total = step * len(pts)
    span = pts.max(axis=0) - pts.min(axis=0)
    length, max_span = (TIERRA_LENGTH_M, TIERRA_MAX_SPAN_M) if profile == "tierra" else (LENGTH_M, MAX_SPAN_M)
    if not (length[0] <= total <= length[1]) or span.max() > max_span or not separation_ok(pts, step):
        return None
    return total, Plan(seed, attempt, pieces, segs, pts, psi, k, step), spans


def make_plan(seed: int, profile: str = "dunas") -> Plan:
    """Primer sorteo (semilla, intento) con algún orden de piezas (de ORDERINGS) que da un lazo cerrado, sin cruces,
    de LENGTH_M y que cabe en MAX_SPAN_M; de sus órdenes válidos, el más corto."""
    for attempt in range(MAX_ATTEMPTS):
        rng = np.random.default_rng([seed, attempt])
        drawn = draw_pieces(rng, profile)
        if drawn is None:
            continue
        found = [c for c in (_candidate(seed, attempt, arrange(rng, *drawn), profile) for _ in range(ORDERINGS)) if c]
        if found:
            _, plan, spans = min(found, key=lambda c: c[0])
            return _rotate_to_start(plan, spans)
    raise RuntimeError(f"semilla {seed} ({profile}): ningún trazado válido en {MAX_ATTEMPTS} intentos")


def _rotate_to_start(plan: Plan, spans: list) -> Plan:
    """La muestra 0 pasa a ser la línea de salida (START_LINE_M dentro de la recta de salida)."""
    shift = int(round(START_LINE_M / plan.step_m))
    total = plan.length_m
    plan.pts = np.roll(plan.pts, -shift, axis=0)
    plan.psi = np.roll(plan.psi, -shift)
    plan.curvature = np.roll(plan.curvature, -shift)
    offset = shift * plan.step_m
    plan.spans = [(p, (s0 - offset) % total, (s1 - offset) % total or total) for p, s0, s1 in spans]
    return plan
