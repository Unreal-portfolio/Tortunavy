"""Tests de Scripts/tablero/avisos.py (sin red ni gh)."""

import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tablero"))
import auditoria  # noqa: E402
import avisos  # noqa: E402

AHORA = datetime(2026, 10, 1, 6, 10, tzinfo=timezone.utc)
PUSH_RUBI = {"actor": "Ruben-Besteiro", "antes": "4ce191a0e" + "0" * 31, "despues": "f8dfd9aa2" + "0" * 31,
             "cuando": datetime(2026, 9, 30, 16, 25, 40, tzinfo=timezone.utc), "forzado": False}


def _actividad(tipo, cuando, actor="Ruben-Besteiro", antes="a" * 40, despues="b" * 40):
    return {"activity_type": tipo, "timestamp": cuando, "actor": {"login": actor}, "before": antes, "after": despues}


def _commit(sha, titulo="fix: algo", padres=1):
    return {"sha": sha, "titulo": titulo, "padres": padres}


def _issue(numero, status, *, asignados=(), etiquetas=(), actualizada="2026-09-30T20:00:00Z", **valores):
    return {"number": numero, "title": f"Tarea {numero}", "state": "OPEN", "updatedAt": actualizada,
            "valores": {"Status": status, **valores},
            "labels": {"nodes": [{"name": e} for e in etiquetas]},
            "assignees": {"nodes": [{"login": a} for a in asignados]}}


# --- pushes directos -------------------------------------------------------------------------------

def test_solo_cuentan_los_pushes_y_dentro_de_la_ventana():
    actividad = [_actividad("pr_merge", "2026-09-30T15:26:06Z"), _actividad("push", "2026-09-30T16:25:40Z"),
                 _actividad("force_push", "2026-09-30T18:00:00Z", actor="Mokius"),
                 _actividad("push", "2026-09-28T10:00:00Z")]
    pushes = avisos.pushes_directos(actividad, AHORA - timedelta(hours=26))
    assert [(p["actor"], p["forzado"]) for p in pushes] == [("Ruben-Besteiro", False), ("Mokius", True)]


def test_caso_del_30_09_commits_sin_pr_y_pr_marcada_fusionada_por_el_push():
    """Supervivencia (PR #166, fusionada por el push), un merge de dev y dos commits de gaviota sin PR."""
    commits = [_commit("8f647bf88"), _commit("b5e9c9383", "merge: dev en la rama", padres=2),
               _commit("01f663271", "feat(hud): aviso de gaviota"), _commit("f8dfd9aa2", "fix(gaviota): sombrilla")]
    prs = {"8f647bf88": [{"number": 166, "merged_at": "2026-09-30T16:25:41Z"}, {"number": 194, "merged_at": None}],
           "01f663271": [{"number": 194, "merged_at": None}], "f8dfd9aa2": [], "b5e9c9383": []}
    sin_pr = avisos.commits_sin_pr(commits, prs, PUSH_RUBI["cuando"])
    assert [c["sha"] for c in sin_pr] == ["01f663271", "f8dfd9aa2"], "el merge no trae código propio"
    assert avisos.prs_del_push(commits, prs, PUSH_RUBI["cuando"]) == {166}
    linea = avisos.linea_push(PUSH_RUBI, sin_pr, {166}, 197)
    assert "**Ruben-Besteiro**" in linea and "2 commits sin PR ni revisión → #197" in linea
    assert "`01f663271` feat(hud): aviso de gaviota" in linea and "PR #166" in linea


def test_una_pr_fusionada_despues_del_push_no_tapa_el_commit():
    prs = {"abc": [{"number": 200, "merged_at": "2026-10-01T09:00:00Z"}]}
    assert avisos.commits_sin_pr([_commit("abc")], prs, PUSH_RUBI["cuando"]) == [_commit("abc")]


RUTAS = ["Scripts/tablero/", "Scripts/tests/test_tablero", ".claude/", ".github/", "CLAUDE.md", "Docs/"]


def test_lo_organizativo_va_sin_revision_a_proposito():
    assert avisos.es_organizativo(["Scripts/tablero/avisos.py", "Scripts/tests/test_tablero_avisos.py", "CLAUDE.md"], RUTAS)
    assert avisos.es_organizativo([".claude/skills/tortu-revisar/SKILL.md", ".github/workflows/x.yml"], RUTAS)
    assert not avisos.es_organizativo(["CLAUDE.md", "Source/Tortunabo/Private/World/TN_EnemySeagull.cpp"], RUTAS)
    assert not avisos.es_organizativo(["Config/DefaultEngine.ini"], RUTAS), "el push de Mokius del 29-09 es código"
    assert not avisos.es_organizativo([], RUTAS), "sin ficheros no se sabe qué toca"


def _pr(numero, refs, ficheros, por="Ruben-Besteiro"):
    return {"number": numero, "refs": set(refs), "files": [{"path": f} for f in ficheros],
            "mergedBy": {"login": por}, "mergedAt": "2026-09-30T16:25:41Z"}


def test_pr_de_lote_fusionada_con_miembros_sin_probar():
    items = {144: {"valores": {"Revisión IA": "Aprobada", "Editor": "Sin probar"}},
             145: {"valores": {"Revisión IA": "Aprobada"}},
             156: {"valores": {"Revisión IA": "Aprobada", "Editor": "Funciona"}}}
    linea = avisos.pr_sin_validar(_pr(166, [144, 145, 156, 167], ["Source/a.cpp"]), items, RUTAS, {167})
    assert linea == ("PR #166 fusionada en dev por **Ruben-Besteiro** (30-09 16:25 UTC) con "
                     "#144 (Editor = Sin probar (lote)), #145 (Editor = vacío (lote)).")


def test_pr_fuera_de_lote_solo_exige_la_revision():
    items = {44: {"valores": {"Revisión IA": "Aprobada", "Editor": "Sin probar"}},
             45: {"valores": {"Revisión IA": "Pendiente"}}}
    assert avisos.pr_sin_validar(_pr(1, [44], ["Source/a.cpp"]), items, RUTAS, set()) is None, "irá a QA editor"
    assert "#45 (Revisión IA = Pendiente)" in avisos.pr_sin_validar(_pr(2, [45], ["Source/a.cpp"]), items, RUTAS, set())


def test_pr_sin_issue_con_codigo_es_incidencia_y_la_organizativa_no():
    assert "sin enlazar ninguna issue" in avisos.pr_sin_validar(_pr(3, [], ["Content/a.uasset"]), {}, RUTAS, set())
    assert avisos.pr_sin_validar(_pr(4, [], ["Scripts/tablero/base.py"], "SkiTemplar"), {}, RUTAS, set()) is None
    assert avisos.pr_sin_validar(_pr(5, [999], ["Source/a.cpp"]), {}, RUTAS, set()) is None, "fuera del tablero"


def test_issue_sin_revision_titulo_corto_y_criterios():
    titulo = avisos.titulo_issue(PUSH_RUBI["actor"], PUSH_RUBI["despues"])
    assert titulo == "Revisar el push directo a dev de Ruben-Besteiro (f8dfd9aa2)"
    assert len(titulo) <= auditoria.MAX_TITULO
    cuerpo = avisos.cuerpo_issue(PUSH_RUBI, [_commit("01f663271" + "0" * 31, "feat(hud): aviso")], "org/repo", "dev")
    assert "@Ruben-Besteiro subió 1 commit a `dev`" in cuerpo and "`01f663271` feat(hud): aviso" in cuerpo
    assert "compare/4ce191a0e...f8dfd9aa2" in cuerpo
    assert auditoria.problemas_de_formato(titulo, cuerpo, {"tarea"}) == []


def test_la_auditoria_no_trata_las_issues_sin_revision_como_trabajo():
    issue = {"etiquetas": {avisos.ETIQUETA}, "titulo": "Revisar el push directo a dev de X (abc)"}
    assert not auditoria.es_de_trabajo(issue)
    assert not auditoria.es_de_trabajo({"etiquetas": set(), "titulo": "Avisos diarios del tablero"})


# --- resumen por persona ---------------------------------------------------------------------------

def _tablero():
    return [_issue(13, "In review", asignados=["Mokius"], Revisor="SkiTemplar", Prioridad="P1"),
            _issue(144, "QA editor", asignados=["Ruben-Besteiro"]),
            _issue(25, "Revisiones"),
            _issue(126, "Backlog", etiquetas=["decision"]),
            _issue(147, "In progress", asignados=["Ruben-Besteiro"], etiquetas=["peticion"]),
            _issue(60, "In progress", asignados=["Ruben-Besteiro"], actualizada="2026-09-20T10:00:00Z"),
            _issue(197, "In review", asignados=["Ruben-Besteiro"], etiquetas=[avisos.ETIQUETA], Revisor="Mokius")]


def _numeros(secciones):
    return {titulo.split(" (")[0].split(":")[0]: [i["number"] for i in lista] for titulo, lista in secciones}


def test_aprobador_ve_lo_suyo_y_lo_que_no_es_de_nadie():
    secciones = _numeros(avisos.secciones(_tablero(), "SkiTemplar", True, AHORA, 3))
    assert secciones["Te toca revisar"] == [13]
    assert secciones["Revisiones"] == [25], "sin asignar: la ven los aprobadores"
    assert secciones["Decisiones pendientes"] == [126]
    assert secciones["QA editor"] == [144]
    assert secciones["Colisiones y avisos de organización"] == [197]
    assert "Tu trabajo en curso" not in secciones


def test_miembro_ve_solo_lo_suyo():
    secciones = _numeros(avisos.secciones(_tablero(), "Ruben-Besteiro", False, AHORA, 3))
    assert secciones == {"Colisiones y avisos de organización": [197], "Peticiones sin contestar": [147],
                         "QA editor": [144], "Tu trabajo en curso": [60, 147],
                         "Sin movimiento desde hace más de 3 días": [60]}


def test_render_menciona_resume_y_adjunta_el_parte():
    secciones = avisos.secciones(_tablero(), "SkiTemplar", True, AHORA, 3)
    texto = avisos.render("SkiTemplar", secciones, ["**Ruben-Besteiro** subió a dev por push directo."], AHORA,
                          parte="**Parte 2026-10-01**\n- nada")
    primera = texto.splitlines()[0]
    assert primera.startswith("@SkiTemplar · avisos del 2026-10-01: 1 incidencia; ")
    assert "1 · te toca revisar" in primera
    assert "### Incidencias: código en dev sin revisión" in texto and "- #13 Tarea 13 (P1, Mokius)" in texto
    assert texto.index("### Incidencias") < texto.index("### Te toca revisar") < texto.index("### Parte de la rutina")


def test_render_sin_nada_no_publica_y_recorta_listas_largas():
    assert avisos.render("Mokius", [], [], AHORA) is None
    muchas = [_issue(n, "QA editor") for n in range(1, 21)]
    texto = avisos.render("Mokius", [("QA editor: falta probar", muchas)], [], AHORA)
    assert f"- #{avisos.MAX_LINEAS} " in texto and f"- #{avisos.MAX_LINEAS + 1} " not in texto
    assert "… y 5 más" in texto


def test_parte_reciente_solo_si_es_de_hoy():
    viejo = {"created_at": "2026-09-30T05:40:00Z", "updated_at": "2026-09-30T15:50:00Z", "body": "ayer"}
    nuevo = {"created_at": "2026-10-01T05:40:00Z", "body": "hoy"}
    assert avisos.parte_reciente([viejo, nuevo], AHORA) == "hoy"
    assert avisos.parte_reciente([viejo], AHORA) is None
    assert avisos.parte_reciente([], AHORA) is None


# --- PR nuevas y menciones -------------------------------------------------------------------------

def _comentario(autor, cuerpo, cuando="2026-10-01T05:00:00Z", numero=144, pr=False):
    url = f"https://api.github.com/repos/o/r/{'pulls' if pr else 'issues'}/{numero}"
    return {"user": {"login": autor}, "body": cuerpo, "created_at": cuando,
            "html_url": f"https://github.com/o/r/issues/{numero}#issuecomment-1",
            **({"pull_request_url": url} if pr else {"issue_url": url})}


def test_menciones_de_otros_en_la_ventana():
    desde = AHORA - timedelta(hours=26)
    comentarios = [_comentario("Mokius", "@SkiTemplar ¿lo pruebas en PIE 2P?"),
                   _comentario("Ruben-Besteiro", "Hecho, @skitemplar", numero=191, pr=True),
                   _comentario("SkiTemplar", "@SkiTemplar nota para mí"),
                   _comentario("github-actions[bot]", "@SkiTemplar · avisos"),
                   _comentario("Mokius", "@SkiTemplarX no es él"),
                   _comentario("Mokius", "Lista para revisión. Revisor: @SkiTemplar (su Claude hace la revisión IA)."),
                   _comentario("Mokius", "@SkiTemplar antiguo", cuando="2026-09-28T10:00:00Z")]
    lista = avisos.menciones(comentarios, "SkiTemplar", desde)
    assert [c["body"] for c in lista] == ["@SkiTemplar ¿lo pruebas en PIE 2P?", "Hecho, @skitemplar"]
    linea = avisos.linea_mencion(lista[1])
    assert linea.startswith("[#191](https://github.com/o/r/issues/191#issuecomment-1) **Ruben-Besteiro** (01-10 05:00)")


def test_linea_mencion_recorta_y_aplana():
    linea = avisos.linea_mencion(_comentario("Mokius", "@SkiTemplar\n\n" + "x" * 300))
    assert "\n" not in linea and linea.endswith("…") and len(linea.split("): ", 1)[1]) == avisos.MAX_EXTRACTO


def test_prs_nuevas_marcan_las_que_no_van_a_dev():
    prs = [{"number": 231, "title": "ci: plantilla", "author": {"login": "SkiTemplar"}, "baseRefName": "main",
            "createdAt": "2026-09-30T18:30:00Z", "state": "MERGED", "isDraft": False},
           {"number": 206, "title": "fix(red): FakeError", "author": {"login": "SkiTemplar"}, "baseRefName": "dev",
            "createdAt": "2026-09-30T17:50:00Z", "state": "OPEN", "isDraft": True},
           {"number": 87, "title": "vr", "author": {"login": "Mokius"}, "baseRefName": "main",
            "createdAt": "2026-09-20T10:00:00Z", "state": "MERGED", "isDraft": False}]
    nuevas = avisos.prs_nuevas(prs, AHORA - timedelta(hours=26))
    assert [p["number"] for p in nuevas] == [206, 231]
    assert avisos.linea_pr(nuevas[0], "dev") == "#206 fix(red): FakeError (SkiTemplar, borrador)"
    assert avisos.linea_pr(nuevas[1], "dev") == "#231 ci: plantilla (SkiTemplar, fusionada) · **hacia `main`**"


def test_render_con_solo_menciones_publica_y_las_cuenta():
    texto = avisos.render("SkiTemplar", [], [], AHORA, extras=[("PR nuevas", []), ("Te mencionan", ["[#1](u) **Mokius**: hola"])])
    assert texto.splitlines()[0] == "@SkiTemplar · avisos del 2026-10-01: 1 · te mencionan."
    assert "### Te mencionan (1)" in texto and "### PR nuevas" not in texto
