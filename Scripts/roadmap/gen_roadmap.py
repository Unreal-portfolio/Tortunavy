import math
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from roadmap_data import EPICS, OLAS, T  # noqa: E402

KEYS = ["id", "tit", "desc", "hmin", "hmax", "deps", "quien", "crit", "ref", "carril", "tipo", "ola", "estado", "fase"]
tasks = [dict(zip(KEYS, t)) for t in T]
for t in tasks:
    t["hmin"], t["hmax"] = float(t["hmin"]), float(t["hmax"])
byid = {t["id"]: t for t in tasks}

# ---- Precisiones del director (2026-09-29, tarde) ----
byid["E1-03"].update(tit="F0-T3 OnTravelFailure (hueco de robustez)", hmin=2, hmax=2,
    desc="Seamless Travel: enlazar GEngine->OnTravelFailure (OnNetworkFailure ya está, MP_GameInstance.cpp:108) y volver al lobby con aviso",
    crit="Viaje a mapa inexistente → lobby/menú con aviso en 2 máquinas")
for k in ("E1-04", "E1-05"):
    byid[k].update(ola=13)
byid["E1-04"]["desc"] = "Más tarde (decisión del director): sigue el 480 hasta entonces; no bloquea nada"
byid["E1-05"]["desc"] = "Más tarde (decisión del director): cuenta, pago y AppID propio; no bloquea nada"
byid["E1-08"]["deps"] = "E1-01,E1-03,E1-07"
byid["E16-04"]["deps"] = "E4-09"
byid["E0-05"].update(hmin=8, hmax=10, ref="specs/modos/00 §2; Investigacion-8-Jugadores §7 #9")
byid["E11-01"]["ref"] = "00 §2; Investigacion-8-Jugadores §7 #7"
byid["E11-02"].update(desc="TN.Stress race8: 8 pawns, objetos y mecánicas; medir ms, KB/s, draw calls",
                      ref="00 §2; Investigacion-8-Jugadores §7 #7")
byid["E6-29"].update(estado="pend", fase="MVP", ola=9, deps="E6-26",
    desc="Autoridad del conductor validada por el servidor y 30/10 Hz: condición de la investigación de 8 jugadores",
    ref="03-Rally §9; B_buggy_sync; Investigacion-8-Jugadores §Veredicto")
for k in ("E10-08", "E10-09", "E10-10", "E10-11"):
    del byid[k]
tasks = [t for t in tasks if t["id"] in byid]
REF8 = "Investigacion-8-Jugadores-Voz-2026-09-29 §7"
new10 = [
    ("E10-08", "Investigación de 8 jugadores con voz", "Veredicto: viable con Opus, pulsar para hablar y opción E del Rally", 0, 0, "—", "Opus (hecho)", "Documento entregado", REF8, "DOC", "DOC", 11, "hecho", "MVP"),
    ("E10-09", "Voz con Opus", "Build.cs, componente, byte de versión y test de ida y vuelta del códec (hoy µ-law ≈ 17 KB/s por flujo)", 6, 8, "E0-01", "UE", "Test de códec verde; KB/s por flujo medido y bajo el objetivo de la investigación", REF8 + " #1", "NET", "CPP", 2, "pend", "MVP"),
    ("E10-10", "Pulsar para hablar por defecto y puerta de umbral", "", 1, 1, "E10-09", "UE", "PTT activo en un perfil nuevo", REF8 + " #2", "NET", "CPP", 2, "pend", "MVP"),
    ("E10-11", "Config IpNetDriver igual que Steam y 8 instancias con emulación", "Scripts de lanzamiento; tope MaxClientRate tras medir", 2, 3, "E10-09", "devops-engineer", "8 instancias locales con PktLag por script", REF8 + " #5", "NET", "CPP", 2, "pend", "MVP"),
    ("E10-12", "Reglas de reenvío de voz", "Tope de voces por oyente, canal de equipo 2 vs 2, espectador, silenciar, descarte por saturación", 8, 10, "E10-10", "UE", "Con 8 hablando, cada oyente recibe ≤ tope (test)", REF8 + " #3", "NET", "CPP", 11, "pend", "MVP"),
    ("E10-13", "Frecuencia y relevancia de actores siempre relevantes", "Auditoría y medida con stat net", 3, 4, "E10-11", "UE", "Tabla de KB/s por actor antes y después", REF8 + " #4", "NET", "CPP", 11, "pend", "MVP"),
    ("E10-14", "TN.Voice.FakeTalk", "Voz sintética para pruebas", 2, 3, "E10-09", "UE", "8 hablantes simulados en PIE", REF8 + " #6", "NET", "CPP", 11, "pend", "MVP"),
    ("E10-15", "Telemetría LogTNNet y aviso de subida del anfitrión", "", 3, 4, "E0-02", "UE", "Aviso en pantalla si la subida supera el presupuesto", REF8 + " #8", "NET", "CPP", 11, "pend", "MVP"),
    ("E10-16", "Voz del conductor en el Rally", "Voz en el pawn buggy y relevancia", 2, 2, "E6-19", "UE", "Voz audible entre buggies en PIE 4P", REF8 + " #10", "VH", "CPP", 9, "pend", "MVP"),
    ("E10-17", "Sesiones de prueba a 8", "PIE a 8, 8 locales, máquinas virtuales y 2 playtests Steam con 8 personas", 10, 12, "E10-12,E10-13,E11-02", "Rodrigo + UE", "15 min con 8 sin [Desync] y subida del anfitrión dentro del presupuesto", REF8 + " #11", "QA", "MAN", 11, "pend", "MVP"),
    ("E10-18", "Entrada tardía y reconexión en los modos nuevos", "Coop, Rally, TcT, 2 vs 2", 6, 8, "E0-09", "UE", "Reconexión a mitad de ronda en cada modo sin estado roto", "Plan §3.1 Red; auditoría 08-18 #5", "NET", "CPP", 11, "pend", "MVP"),
]
# E10-12 original (entrada tardía) ya existía con ese id: se sustituye por la lista nueva
tasks = [t for t in tasks if not t["id"].startswith("E10-1")]
for n in new10:
    d = dict(zip(KEYS, n)); d["hmin"], d["hmax"] = float(d["hmin"]), float(d["hmax"])
    tasks.append(d)
byid = {t["id"]: t for t in tasks}
byid["E16-06"]["deps"] = "E7-14,E8-07,E6-28,E10-17"

def num(tid):
    e, n = tid.split("-"); return (int(e[1:]), int(n))
tasks.sort(key=lambda t: num(t["id"]))

# ---- Validación de dependencias ----
bad = []
for t in tasks:
    for d in [x.strip() for x in t["deps"].split(",") if x.strip() not in ("—", "")]:
        if d not in byid:
            bad.append((t["id"], d))
if bad:
    print("DEPS ROTAS", bad); sys.exit(1)

def fh(a, b):
    f = lambda x: (f"{x:.1f}".rstrip("0").rstrip(".")).replace(".", ",")
    return f(a) if a == b else f"{f(a)}–{f(b)}"

ESTADO = {"pend": "planificado", "curso": "en curso", "hecho": "hecho", "cond": "condicional"}

# ---- Sesiones ----
CARRIL_ORD = ["INT", "ST", "FW", "PY", "CP", "PL", "NET", "BR", "VH", "TC", "DV", "UI", "ART", "CL", "LOC", "DOC", "QA"]
work = [t for t in tasks if t["estado"] in ("pend", "curso") and t["tipo"] != "EXT" and t["hmax"] > 0]
work.sort(key=lambda t: (t["ola"], num(t["id"])[0], CARRIL_ORD.index(t["carril"]), num(t["id"])))
chunks = []
for t in work:
    mid = (t["hmin"] + t["hmax"]) / 2
    parts = max(1, math.ceil(mid / 5.5)) if mid > 6 else 1
    for p in range(parts):
        chunks.append((t, mid / parts, f"{t['id']}" + (f" ({p+1}/{parts})" if parts > 1 else "")))
sessions = []
cur = None
for t, h, label in chunks:
    key = (t["ola"], t["id"].split("-")[0], t["carril"])
    if cur and cur["key"] == key and cur["h"] + h <= 6.0:
        cur["items"].append(label); cur["h"] += h; cur["tasks"].append(t)
    else:
        cur = {"key": key, "items": [label], "h": h, "tasks": [t]}
        sessions.append(cur)
for i, s in enumerate(sessions, 1):
    s["n"] = i
    ts = s["tasks"]
    s["cpp"] = any(x["tipo"] == "CPP" for x in ts)
    s["rod"] = any("Rodrigo" in x["quien"] or x["tipo"] == "MAN" for x in ts)
    s["arte"] = any("equipo de arte" in x["quien"] for x in ts)
for t in tasks:
    t["ses"] = [s["n"] for s in sessions if t in s["tasks"]]

# ---- Totales ----
def tot(ts, pred):
    sel = [t for t in ts if pred(t)]
    return sum(t["hmin"] for t in sel), sum(t["hmax"] for t in sel), len(sel)

out = []
w = out.append
w(Path(__file__).with_name("roadmap_header.md").read_text(encoding="utf-8"))

w("\n## 2. Totales\n")
w("| Épica | Tareas | Hechas | MVP (h) | Después (h) | Condicional (h) | Sesiones |")
w("|---|---|---|---|---|---|---|")
g = [0, 0, 0, 0, 0, 0, 0, 0]
for e, name in EPICS.items():
    ts = [t for t in tasks if t["id"].split("-")[0] == e]
    a = tot(ts, lambda t: t["fase"] == "MVP" and t["estado"] in ("pend", "curso"))
    b = tot(ts, lambda t: t["fase"] == "Despues" and t["estado"] in ("pend", "curso"))
    c = tot(ts, lambda t: t["estado"] == "cond")
    hechas = sum(1 for t in ts if t["estado"] == "hecho")
    ns = len({s["n"] for s in sessions if s["key"][1] == e})
    w(f"| {e} {name} | {len(ts)} | {hechas} | {fh(a[0], a[1])} | {fh(b[0], b[1])} | {fh(c[0], c[1])} | {ns} |")
    for i, v in enumerate([len(ts), hechas, a[0], a[1], b[0], b[1], c[0], c[1]]):
        g[i] += v
w(f"| **Total** | **{g[0]}** | **{g[1]}** | **{fh(g[2], g[3])}** | **{fh(g[4], g[5])}** | **{fh(g[6], g[7])}** | **{len(sessions)}** |")
mvp_mid = (g[2] + g[3]) / 2; all_mid = (g[2] + g[3] + g[4] + g[5]) / 2
w(f"\nGlobal comprometido (MVP + Después, sin condicionales): **{fh(g[2] + g[4], g[3] + g[5])} h**. "
  f"Punto medio MVP ≈ {mvp_mid:.0f} h; con Después ≈ {all_mid:.0f} h. "
  f"Sesiones de ~4–6 h: **{len(sessions)}** ({sum(1 for s in sessions if s['key'][0] < 14)} hasta la 1.0 y "
  f"{sum(1 for s in sessions if s['key'][0] == 14)} de Después). "
  f"Necesitan a Rodrigo: {sum(1 for s in sessions if s['rod'])}. Compilan UE: {sum(1 for s in sessions if s['cpp'])}; "
  f"sin compilar (Python, arte, documentación, manuales): {sum(1 for s in sessions if not s['cpp'])}.\n")
w("Las horas son estimaciones de primera pasada de cada spec (sin depuración de red salvo donde se indica). "
  "Solape conocido sin descontar: interfaz y efectos comunes entre 01-Coop §7 y 05-2vs2 §13 (≈ 10–20 h), "
  "declarado en las propias specs. Las 16 h por iteración de playtest Steam salen de F_gaps_steam §4.1.\n")

w("\n## 3. Tareas por épica\n")
w("Estado: planificado, en curso, hecho o condicional (fuera del total hasta que el director lo confirme). "
  "Sesión: número de la sección 4 donde se hace.\n")
for e, name in EPICS.items():
    ts = [t for t in tasks if t["id"].split("-")[0] == e]
    a = tot(ts, lambda t: t["estado"] in ("pend", "curso") and t["fase"] == "MVP")
    w(f"\n### {e}. {name} ({fh(a[0], a[1])} h MVP)\n")
    w("| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |")
    w("|---|---|---|---|---|---|---|---|---|---|---|")
    for t in ts:
        ses = ",".join(str(x) for x in t["ses"]) or "—"
        fase = "Después" if t["fase"] == "Despues" else "MVP"
        w(f"| {t['id']} | {t['tit']} | {t['desc'] or '—'} | {fh(t['hmin'], t['hmax'])} | {t['deps']} | {t['quien']} | "
          f"{t['crit'] or '—'} | {t['ref']} | {ESTADO[t['estado']]} | {fase} | {ses} |")

w("\n## 4. Sesiones de trabajo\n")
w("Cada sesión = un objetivo cerrado de ~4–6 h de agente, dentro de una épica y un carril. "
  "Columna «Paralelo»: **P** = no compila UE, puede ir en paralelo con cualquier sesión; "
  "**C** = compila UE: en paralelo solo con sesiones C de otro carril (ficheros disjuntos) y la compilación "
  "la serializa el integrador en DebugGame; **R** = necesita a Rodrigo (PIE, Steam, decisión o lámina); "
  "**A** = depende de entregas del equipo de arte. Las tareas partidas llevan (n/m).\n")
last = None
for s in sessions:
    ola = s["key"][0]
    if ola != last:
        w(f"\n### Ola {ola}. {OLAS[ola]}\n")
        w("| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |")
        w("|---|---|---|---|---|---|---|")
        last = ola
    objetivo = "; ".join(dict.fromkeys(x["tit"] for x in s["tasks"]))
    flags = ("C" if s["cpp"] else "P") + (" · R" if s["rod"] else "") + (" · A" if s["arte"] else "")
    w(f"| {s['n']} | {s['key'][1]} | {s['key'][2]} | {', '.join(s['items'])} | {objetivo} | {s['h']:.1f}".replace(".", ",")
      + f" | {flags} |")

Path(sys.argv[1]).write_text("\n".join(out) + "\n", encoding="utf-8")
print("tareas", len(tasks), "sesiones", len(sessions), "MVP", g[2], g[3], "Despues", g[4], g[5], "cond", g[6], g[7],
      "rodrigo", sum(1 for s in sessions if s["rod"]))
for e in EPICS:
    print(e, [t["id"] for t in tasks if t["id"].split("-")[0] == e and t["estado"] == "hecho"])
