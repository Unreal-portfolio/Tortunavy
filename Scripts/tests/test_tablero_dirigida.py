"""Consulta dirigida de los comandos de una issue y caché de los campos del Project (#780), con gh simulado."""

import argparse
import importlib.util
import json
import re
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

import pytest

CARPETA = Path(__file__).resolve().parents[1] / "tablero"
sys.path.insert(0, str(CARPETA))
import base  # noqa: E402
import control  # noqa: E402
import peticiones  # noqa: E402

spec = importlib.util.spec_from_file_location("tablero_dirigida", CARPETA / "tablero.py")
assert spec is not None and spec.loader is not None
tablero = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tablero)

CONSULTA_ENTERA = "items(first: 100, after: $cursor)"


# --- coste de las consultas -------------------------------------------------------------------------

CONEXION_O_BLOQUE = re.compile(r"\w+\s*\(([^()]*)\)\s*(?:@\w+\s*\([^()]*\)\s*)?\{|\{|\}")


def coste_estimado(consulta: str) -> int:
    """Coste de una consulta según la fórmula publicada por GitHub para la API GraphQL.

    Cada conexión con `first`/`last` necesita tantas peticiones como elementos tiene su conexión padre; el coste es
    la suma de peticiones entre 100, redondeada, y como mínimo 1.
    """
    pila, peticiones_ = [1], 0
    for bloque in CONEXION_O_BLOQUE.finditer(consulta):
        if bloque.group(0) == "}":
            pila.pop()
            continue
        limite = re.search(r"\b(?:first|last):\s*(\d+)", bloque.group(1) or "")
        if limite:
            peticiones_ += pila[-1]
            pila.append(pila[-1] * int(limite.group(1)))
        else:
            pila.append(pila[-1])
    assert pila == [1], "llaves desequilibradas"
    return max(1, round(peticiones_ / 100))


def test_la_consulta_dirigida_pide_su_coste_y_cuesta_5_puntos_o_menos():
    assert "rateLimit { cost" in base.CONSULTA_DIRIGIDA
    assert coste_estimado(base.CONSULTA_DIRIGIDA) <= 5
    assert CONSULTA_ENTERA not in base.CONSULTA_DIRIGIDA


def test_el_project_entero_cuesta_mucho_mas_por_pagina():
    # 15 puntos por cada página de 100 items: con ~350 items, los ~55 por comando que motivaron #780.
    assert coste_estimado(base.CONSULTA_ITEMS) == 15
    assert coste_estimado(base.CONSULTA_DIRIGIDA) == 1
    assert coste_estimado(base.CONSULTA_CAMPOS) == 1
    assert coste_estimado(base.CONSULTA_REVISIONES) == 1


# --- gh simulado ------------------------------------------------------------------------------------

def _valores(**valores):
    return [{"name": v, "field": {"name": c.replace("_", " ")}} for c, v in valores.items()]


def _issue(numero=42, estado="OPEN", bloqueantes=(), etiquetas=(), asignados=(), en_proyecto=True, lotes=(),
           **valores):
    items = [{"id": f"PVTI_{numero}", "project": {"number": base.NUMERO},
              "fieldValues": {"nodes": _valores(**valores)}}] if en_proyecto else []
    return {"number": numero, "title": "Rally: fallo", "state": estado, "url": "u", "updatedAt": "2026-10-05T00:00:00Z",
            "assignees": {"nodes": [{"login": a} for a in asignados]},
            "labels": {"nodes": [{"name": e} for e in etiquetas]},
            "blockedBy": {"nodes": [{"number": b, "state": s} for b, s in bloqueantes]},
            "blocking": {"nodes": [{"number": n, "state": "OPEN", "labels": {"nodes": [{"name": "lote"}]}}
                                   for n in lotes]},
            "projectItems": {"nodes": items}}


def _proyecto_v2(prefijo="O"):
    opciones = {"Status": ["Ready", "In progress", "In review", "Revisiones", "QA editor", "Validada", "Done",
                           "Bloqueada"],
                "Revisión IA": ["Pendiente", "Aprobada", "Cambios pedidos"],
                "Editor": ["Sin probar", "Funciona", "Falla"], "Revisor": ["SkiTemplar", "Mokius"],
                "Prioridad": ["P1"]}
    return {"id": "PVT_1", "fields": {"nodes": [
        {"id": f"F_{c}", "name": c, "options": [{"id": f"{prefijo}_{v}", "name": v} for v in vs]}
        for c, vs in opciones.items()] + [{}]}}


class GhFalso:
    """Responde a las consultas del tablero y registra cada llamada; falla si alguien lee el Project entero."""

    def __init__(self, issue, proyecto_v2=None, revisiones=None):
        self.issue, self.proyecto_v2 = issue, proyecto_v2 or _proyecto_v2()
        self.revisiones = revisiones or []
        self.llamadas, self.ediciones = [], []

    def consultas(self, nombre):
        return [a for a in self.llamadas if a[:2] == ("api", "graphql") and nombre in a[3]]

    def __call__(self, *args, entrada=None):
        self.llamadas.append(args)
        if args[:2] == ("api", "graphql"):
            return self.graphql(args)
        if args[:2] == ("project", "item-edit"):
            self.ediciones.append(self.edicion(args))
            return ""
        if args[:2] in {("issue", "comment"), ("issue", "close"), ("issue", "edit"), ("issue", "reopen")}:
            return ""
        if args[:2] == ("api", "user"):
            return "SkiTemplar\n"
        raise AssertionError(f"llamada inesperada a gh: {args}")

    def graphql(self, args):
        consulta = args[3]
        assert CONSULTA_ENTERA not in consulta or "fieldValueByName" in consulta, "no se lee el Project entero"
        variables = dict(a.split("=", 1) for a in args[3:] if "=" in a and not a.startswith("query="))
        if "fieldValueByName" in consulta:
            nodos = [{"content": {"number": n}, "status": {"name": s}, "revisor": {"name": r}}
                     for n, s, r in self.revisiones]
            return json.dumps({"data": {"organization": {"projectV2": {"items": {
                "pageInfo": {"hasNextPage": False, "endCursor": None}, "nodes": nodos}}}}})
        datos = {"rateLimit": {"cost": 1, "remaining": 4000}}
        if "repository(" in consulta:
            datos["repository"] = {"issue": self.issue}
        if "fields(first" in consulta and variables.get("conCampos", "true") == "true":
            datos["organization"] = {"projectV2": self.proyecto_v2}
        return json.dumps({"data": datos})

    @staticmethod
    def edicion(args):
        campo = args[args.index("--field-id") + 1]
        valor = args[args.index("--single-select-option-id") + 1] if "--single-select-option-id" in args else None
        return args[args.index("--id") + 1], campo, valor


@pytest.fixture
def cache(monkeypatch, tmp_path):
    monkeypatch.setattr(base, "DIR_CACHE", tmp_path / ".cache")
    return tmp_path / ".cache"


def _usar(monkeypatch, gh_falso, *modulos):
    for modulo in (base, tablero, control, peticiones, *modulos):
        monkeypatch.setattr(modulo, "gh", gh_falso)
    monkeypatch.setenv("GITHUB_EVENT_NAME", "")


# --- caché de los campos ----------------------------------------------------------------------------

def test_primera_lectura_pide_los_campos_y_la_siguiente_los_toma_de_la_cache(monkeypatch, cache):
    gh = GhFalso(_issue(Status="Ready"))
    _usar(monkeypatch, gh)
    primero = base.cargar_issue(42)
    segundo = base.cargar_issue(42)
    assert [a for a in gh.llamadas[0] if a.startswith("conCampos=")] == ["conCampos=true"]
    assert [a for a in gh.llamadas[1] if a.startswith("conCampos=")] == ["conCampos=false"]
    assert len(gh.llamadas) == 2  # una petición por comando, también sin caché
    assert primero["campos"] == segundo["campos"] and primero["id"] == segundo["id"] == "PVT_1"
    assert segundo["items"][42]["valores"] == {"Status": "Ready"} and segundo["coste"] == 1
    assert (cache / f"campos-{base.OWNER}-{base.NUMERO}.json").exists()


@pytest.mark.parametrize("contenido", ["no es json", json.dumps({"id": "PVT_1"})])
def test_cache_rota_cuenta_como_vacia(cache, contenido):
    cache.mkdir()
    base.fichero_cache().write_text(contenido, encoding="utf-8")
    assert base.leer_cache_campos() is None


def test_cache_caducada_o_del_futuro_no_vale(cache):
    base.guardar_cache_campos({"id": "PVT_1", "campos": {}})
    ahora = datetime.now(timezone.utc)
    assert base.leer_cache_campos(ahora) == {"id": "PVT_1", "campos": {}}
    assert base.leer_cache_campos(ahora + base.CADUCIDAD_CAMPOS + timedelta(minutes=1)) is None
    assert base.leer_cache_campos(ahora - timedelta(hours=1)) is None
    base.borrar_cache_campos()
    assert base.leer_cache_campos() is None
    base.borrar_cache_campos()  # sin fichero no falla


def test_issue_fuera_del_project_no_tiene_item(monkeypatch, cache):
    _usar(monkeypatch, GhFalso(_issue(en_proyecto=False)))
    assert base.cargar_issue(42)["items"] == {}


def test_opcion_nueva_que_la_cache_no_conoce_refresca_los_campos(monkeypatch, cache):
    gh = GhFalso(_issue(Status="Ready"))
    _usar(monkeypatch, gh)
    base.guardar_cache_campos({"id": "PVT_1", "campos": base.campos_de(_proyecto_v2())})
    gh.proyecto_v2["fields"]["nodes"][0]["options"].append({"id": "O_Nueva", "name": "Nueva"})
    proyecto = base.cargar_issue(42)
    base.poner_campo(proyecto, 42, "Status", "Nueva")
    assert gh.ediciones == [("PVTI_42", "F_Status", "O_Nueva")]
    assert len(gh.consultas("fields(first")) == 2  # la dirigida (sin campos) y el refresco
    with pytest.raises(base.ErrorTablero, match="no admite «Inexistente»"):
        base.poner_campo(proyecto, 42, "Status", "Inexistente")  # ya refrescada: falla sin volver a pedirlos


def test_ids_caducados_en_la_cache_se_refrescan_y_se_reintenta(monkeypatch, cache):
    """Tras redefinir las opciones (asegurar-estados) los ids cambian aunque los nombres sigan."""
    gh = GhFalso(_issue(Status="Ready"), proyecto_v2=_proyecto_v2("NUEVO"))
    base.guardar_cache_campos({"id": "PVT_1", "campos": base.campos_de(_proyecto_v2("VIEJO"))})
    ediciones = []

    def gh_que_rechaza_ids_viejos(*args, entrada=None):
        if args[:2] == ("project", "item-edit") and "VIEJO_Done" in args:
            ediciones.append("rechazada")
            raise base.ErrorTablero("gh project item-edit…: option not found")
        return gh(*args, entrada=entrada)

    _usar(monkeypatch, gh_que_rechaza_ids_viejos)
    base.poner_campo(base.cargar_issue(42), 42, "Status", "Done")
    assert ediciones == ["rechazada"] and gh.ediciones == [("PVTI_42", "F_Status", "NUEVO_Done")]
    assert base.leer_cache_campos()["campos"]["Status"]["opciones"]["Done"] == "NUEVO_Done"


def test_sin_cache_un_fallo_de_item_edit_no_se_reintenta(monkeypatch, cache):
    def gh(*args, entrada=None):
        if args[:2] == ("project", "item-edit"):
            raise base.ErrorTablero("gh project item-edit…: HTTP 502")
        return GhFalso(_issue(Status="Ready"))(*args, entrada=entrada)

    _usar(monkeypatch, gh)
    with pytest.raises(base.ErrorTablero, match="502"):
        base.poner_campo(base.cargar_issue(42), 42, "Status", "Done")


# --- revisor cruzado con un proyecto parcial ---------------------------------------------------------

def test_revisor_con_un_solo_candidato_no_consulta_nada(monkeypatch):
    def gh(*args, entrada=None):
        raise AssertionError("no debe consultar")

    monkeypatch.setattr(base, "gh", gh)
    assert base.elegir_revisor({"parcial": True, "items": {}}, "SkiTemplar") == "Mokius"


def test_revisor_entre_varios_cuenta_la_carga_con_la_consulta_ligera(monkeypatch):
    gh = GhFalso(None, revisiones=[(1, "In review", "Mokius"), (2, "In review", "Mokius"),
                                   (3, "In review", "SkiTemplar"), (4, "Done", "SkiTemplar")])
    monkeypatch.setattr(base, "gh", gh)
    assert base.elegir_revisor({"parcial": True, "items": {}}, "Ruben-Besteiro") == "SkiTemplar"
    assert len(gh.consultas("fieldValueByName")) == 1


# --- comandos de una issue -------------------------------------------------------------------------

def _args(**campos):
    return argparse.Namespace(**campos)


def test_coger_sigue_rechazando_bloqueantes_abiertas(monkeypatch, cache):
    gh = GhFalso(_issue(Status="Ready", bloqueantes=[(40, "OPEN"), (41, "CLOSED")]))
    _usar(monkeypatch, gh)
    with pytest.raises(base.ErrorTablero, match="depende de #40, que siguen abiertas"):
        tablero.cmd_coger(_args(numero=42, forzar=False, retomar=False, rama=None))
    assert gh.ediciones == [] and len(gh.llamadas) == 1


def test_estado_ready_con_bloqueantes_abiertas_va_a_bloqueada(monkeypatch, cache, capsys):
    gh = GhFalso(_issue(Status="Backlog", bloqueantes=[(40, "OPEN")]))
    _usar(monkeypatch, gh)
    tablero.cmd_estado(_args(numero=42, estado="Ready", retomar=False))
    assert gh.ediciones == [("PVTI_42", "F_Status", "O_Bloqueada")]
    assert "Bloqueada" in capsys.readouterr().out


def _fusionada_en_dev(monkeypatch, numero=42):
    monkeypatch.setattr(tablero, "prs_fusionadas",
                        lambda: [{"number": 900, "headRefName": "fix/x", "baseRefName": "dev",
                                  "body": f"Closes #{numero}", "mergedAt": "t"}])
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [])


def test_ia_aprobada_aplica_la_regla_de_done(monkeypatch, cache):
    gh = GhFalso(_issue(Status="QA editor", Editor="Funciona", Revisión_IA="Pendiente"))
    _usar(monkeypatch, gh)
    _fusionada_en_dev(monkeypatch)
    tablero.cmd_ia(_args(numero=42, veredicto="aprobada", revisor="Mokius (Claude)", nota=None))
    assert gh.ediciones == [("PVTI_42", "F_Revisión IA", "O_Aprobada"), ("PVTI_42", "F_Revisor", "O_Mokius"),
                            ("PVTI_42", "F_Status", "O_Done")]
    assert ("issue", "close", "42") == gh.llamadas[-1][:3]


def test_ia_aprobada_sin_probar_y_sin_fusionar_no_cierra(monkeypatch, cache):
    gh = GhFalso(_issue(Status="In review", Revisor="Mokius", Revisión_IA="Pendiente"))
    _usar(monkeypatch, gh)
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [])
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [])
    tablero.cmd_ia(_args(numero=42, veredicto="aprobada", revisor="Mokius (Claude)", nota=None))
    assert gh.ediciones == [("PVTI_42", "F_Revisión IA", "O_Aprobada"), ("PVTI_42", "F_Status", "O_QA editor")]
    assert not any(a[:2] == ("issue", "close") for a in gh.llamadas)


def test_editor_funciona_en_lote_sin_fusionar_pasa_a_validada(monkeypatch, cache):
    gh = GhFalso(_issue(Status="QA editor", Revisión_IA="Aprobada", Editor="Sin probar", lotes=[130]))
    _usar(monkeypatch, gh)
    monkeypatch.setattr(tablero, "prs_fusionadas", lambda: [])
    monkeypatch.setattr(tablero, "prs_abiertas",
                        lambda: [{"number": 901, "headRefName": "feat/42-x", "baseRefName": "dev", "body": ""}])
    tablero.cmd_editor(_args(numero=42, resultado="funciona", como="PIE 4P", nota=None))
    assert gh.ediciones == [("PVTI_42", "F_Editor", "O_Funciona"), ("PVTI_42", "F_Status", "O_Validada")]


def test_editor_falla_con_la_issue_cerrada_la_reabre_como_regresion(monkeypatch, cache):
    gh = GhFalso(_issue(estado="CLOSED", Status="Done", Editor="Funciona", Revisión_IA="Aprobada"))
    _usar(monkeypatch, gh)
    tablero.cmd_editor(_args(numero=42, resultado="falla", como="PIE", nota="pasos"))
    assert gh.ediciones == [("PVTI_42", "F_Editor", "O_Falla"), ("PVTI_42", "F_Status", "O_Revisiones"),
                            ("PVTI_42", "F_Revisión IA", "O_Pendiente")]
    assert ("issue", "reopen", "42") in [a[:3] for a in gh.llamadas]
    assert ("issue", "edit", "42", "--repo", base.REPO, "--add-label", "regresion") in gh.llamadas


def test_revision_de_rubi_reparte_con_la_consulta_ligera(monkeypatch, cache):
    gh = GhFalso(_issue(Status="In progress", Editor="Funciona"), revisiones=[(7, "In review", "Mokius")])
    _usar(monkeypatch, gh)
    monkeypatch.setattr(tablero, "usuario_actual", lambda: "Ruben-Besteiro")
    monkeypatch.setattr(tablero, "prs_abiertas", lambda: [])
    tablero.cmd_revision(_args(numero=42, revisor=None))
    assert ("PVTI_42", "F_Revisor", "O_SkiTemplar") in gh.ediciones


def test_pedir_sobre_una_issue_cerrada_se_rechaza(monkeypatch, cache):
    _usar(monkeypatch, GhFalso(_issue(estado="CLOSED", Status="Done")))
    with pytest.raises(base.ErrorTablero, match="está cerrada"):
        peticiones.cmd_pedir(_args(numero=42, texto="cambia esto"))


@pytest.mark.parametrize("comando", ["coger", "soltar", "estado", "revision", "campo", "ia", "editor", "pedir",
                                     "bloquear"])
def test_los_comandos_de_una_issue_no_leen_el_project_entero(comando):
    fn = {"pedir": peticiones.cmd_pedir, "bloquear": control.cmd_bloquear}.get(comando) \
        or getattr(tablero, f"cmd_{comando}")
    assert "cargar_issue" in fn.__code__.co_names
    assert "cargar_proyecto" not in fn.__code__.co_names


@pytest.mark.parametrize("comando", ["pendiente", "sync", "volcado", "auditar"])
def test_los_comandos_de_todo_el_tablero_siguen_leyendo_el_project_entero(monkeypatch, comando):
    class LeidoEntero(Exception):
        pass

    def entero(*_a, **_k):
        raise LeidoEntero

    for modulo in (tablero, control):
        monkeypatch.setattr(modulo, "cargar_proyecto", entero)
        monkeypatch.setattr(modulo, "cargar_issue", lambda *_a: pytest.fail("consulta dirigida"))
    monkeypatch.setattr(tablero, "usuario_actual", lambda: "SkiTemplar")
    fn = control.cmd_auditar if comando == "auditar" else getattr(tablero, f"cmd_{comando}")
    with pytest.raises(LeidoEntero):
        fn(_args(aplicar=False, publicar=None))
