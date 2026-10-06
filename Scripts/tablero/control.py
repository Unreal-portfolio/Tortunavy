"""Comandos de memoria y control del tablero.

- `resumen` y `decidir`: la memoria del equipo vive en las issues (memoria.py).
- `auditar`: problemas de organización de las issues de trabajo (auditoria.py).
- `colisiones`: PR abiertas contra la misma rama base (dev o dev-<modo>) que chocan al mezclarse (colisiones.py).
- `auditar` también marca los lotes abiertos que incumplen los topes (lotes.py) sin la etiqueta `excepcion`.
- `bloquear`: dependencias nativas de GitHub y estado Bloqueada (bloqueos.py).
- `lote` y `resumenes`: en control_lotes.py.
- `asegurar-estados`: añade Bloqueada y Validada al campo Status sin perder valores (estados.py).
- `chamber`: descarta issues del juego (etiqueta `chamber`, sin asignados ni avisos, en Backlog y cerradas como
  not planned) y avisa de las PR abiertas que las enlazan.
"""

from __future__ import annotations

import argparse
import json
import tempfile
from collections import Counter
from datetime import date, datetime, timedelta, timezone
from pathlib import Path

import auditoria
import bloqueos
import colisiones
import estados
import flujo
import memoria
import objetos
import lotes
import peticiones
from base import (CONFIG, INTEGRACION, NUMERO, OWNER, REPO, ErrorTablero, borrar_cache_campos, cargar_issue,
                  cargar_proyecto, comentar, elegir_revisor, esta_fusionada, gh, issues_de_pr, numeros_chamber,
                  poner_campo, prs_abiertas, prs_fusionadas, quien_lanza, rama_base, solo_descartadas,
                  usuario_actual)


def cmd_resumen(args: argparse.Namespace) -> None:
    comentar(args.numero, memoria.texto_resumen(args.que, args.como, args.pr, args.por_que))
    print(f"#{args.numero}: resumen publicado")


def cmd_decidir(args: argparse.Namespace) -> None:
    comentar(args.numero, memoria.texto_decision(args.texto, args.quien or usuario_actual(), date.today()))
    print(f"#{args.numero}: decisión publicada")


# --- auditar --------------------------------------------------------------------------------------

def contexto_prs(nodos: list[dict], proyecto: dict, abiertas: list[dict], fusionadas: list[dict]) -> dict[int, dict]:
    """Por issue: si tiene PR, si está fusionada, lote fusionado, PR sin lote y revisor sugerido."""
    lotes_abiertos = {n["number"] for n in nodos if lotes.ETIQUETA in objetos.nombres_etiquetas(n)}
    chamber = {n["number"] for n in nodos if flujo.es_chamber(n)} | flujo.descartadas(proyecto["items"])
    no_trabajo = lotes_abiertos | chamber | {n["number"] for n in nodos if objetos.es_objeto(n)}
    con_pr = {n for pr in abiertas + fusionadas for n in issues_de_pr(pr)}
    contexto: dict[int, dict] = {}
    for nodo in nodos:
        n = nodo["number"]
        asignados = [a["login"] for a in nodo["assignees"]["nodes"]]
        contexto[n] = {"con_pr": n in con_pr, "fusionada": esta_fusionada(n, fusionadas, abiertas, rama_base(nodo)),
                       "revisor_sugerido": elegir_revisor(proyecto, asignados[0]) if asignados else None,
                       "prs_sin_lote": [], "lote_fusionado": None, "fuera_de_lote": []}
    for nodo in nodos:
        if nodo["number"] not in lotes_abiertos:
            continue
        pr = next((p["number"] for p in fusionadas if flujo.es_rama_de_linea(p["baseRefName"], INTEGRACION)
                   and nodo["number"] in issues_de_pr(p, menciones=True)), None)
        for miembro in bloqueos.bloqueantes(nodo):
            if pr and miembro["number"] in contexto:
                contexto[miembro["number"]]["lote_fusionado"] = pr
    miembros = {n["number"]: {b["number"] for b in bloqueos.bloqueantes(n)}
                for n in nodos if n["number"] in lotes_abiertos}
    marcar_prs_abiertas(contexto, abiertas, miembros, no_trabajo)
    return contexto


def marcar_prs_abiertas(contexto: dict[int, dict], abiertas: list[dict], miembros: dict[int, set[int]],
                        no_trabajo: set[int]) -> None:
    """PR que cierran varias issues sin lote, e issues que cierra la PR de un lote sin ser miembros de él."""
    for pr in abiertas:
        trabajo = issues_de_pr(pr) - no_trabajo
        enlazados = {lote: miembros[lote] for lote in issues_de_pr(pr, menciones=True) & miembros.keys()}
        if lotes.pr_necesita_lote(trabajo, con_lote=bool(enlazados)):
            for n in trabajo & contexto.keys():
                contexto[n]["prs_sin_lote"].append(pr["number"])
        for n, lotes_pr in lotes.fuera_del_lote(trabajo, enlazados).items():
            if n in contexto:
                contexto[n]["fuera_de_lote"] += [(pr["number"], lote) for lote in lotes_pr]


def issues_auditables(proyecto: dict, ahora: datetime) -> tuple[list[dict], list[tuple[dict, list[dict]]]]:
    """Issues de trabajo abiertas y cerradas en los últimos días, con el contexto de sus PR, y los lotes abiertos con
    sus problemas de topes (lista vacía si cumplen, para quitarles la etiqueta si la tenían)."""
    desde = ahora - timedelta(days=auditoria.DIAS_RESUMEN)
    nodos = auditoria.leer_issues(gh, REPO, "OPEN") + auditoria.leer_issues(gh, REPO, "CLOSED", desde)
    contexto = contexto_prs(nodos, proyecto, prs_abiertas(), prs_fusionadas())
    issues = [auditoria.normalizar(n, proyecto["items"].get(n["number"], {}).get("valores", {}), contexto[n["number"]])
              for n in nodos]
    return [i for i in issues if auditoria.es_de_trabajo(i)], lotes_auditables(nodos)


def lotes_auditables(nodos: list[dict]) -> list[tuple[dict, list[dict]]]:
    """Lotes abiertos (no descartados) con un problema de organización por cada tope que incumplen sin `excepcion`."""
    por_numero = {n["number"]: n for n in nodos}
    abiertos = lotes.lotes_abiertos(por_numero)
    malos = lotes.incumplimientos(abiertos)
    return [(auditoria.normalizar(por_numero[n], {}), auditoria.problemas_de_lote(malos.get(n, [])))
            for n in sorted(abiertos) if not flujo.es_chamber(por_numero[n])]


def cmd_auditar(args: argparse.Namespace) -> None:
    proyecto, ahora = cargar_proyecto(), datetime.now(timezone.utc)
    trabajo, de_lotes = issues_auditables(proyecto, ahora)
    informe = [(i, auditoria.problemas(i, ahora)) for i in trabajo] + de_lotes
    con_problemas = sorted([(i, lista) for i, lista in informe if lista], key=lambda x: x[0]["numero"])
    tipos = Counter(p["tipo"] for _, lista in con_problemas for p in lista)
    print(f"## Auditoría de organización · {len(informe)} issues revisadas, {len(con_problemas)} con problemas "
          f"({tipos['grave']} graves, {tipos['trivial']} triviales, {tipos['organizacion']} de organización)"
          + ("" if args.aplicar else " (simulación: usa --aplicar)") + "\n")
    for issue, lista in con_problemas:
        print(f"- #{issue['numero']} {issue['titulo']}: " + "; ".join(f"[{p['tipo']}] {p['texto']}" for p in lista))
    conversaciones = {i["numero"]: a for i, _ in informe
                      if auditoria.es_de_trabajo(i) and (a := auditoria.accion_peticion(i))}
    if conversaciones:
        print("\n### Conversación en las issues (etiqueta `peticion`)")
        print("\n".join(f"- #{n}: {'alguien espera respuesta del asignado' if a == 'poner' else 'ya contestada'}"
                        for n, a in sorted(conversaciones.items())))
    recuento = Counter(p["texto"].split(" (")[0] for _, lista in con_problemas for p in lista)
    print("\n### Recuento por problema")
    print("\n".join(f"- {n} × {p}" for p, n in recuento.most_common()) or "- ninguno")
    if args.aplicar:
        aplicar_auditoria(proyecto, informe)
        aplicar_conversaciones(conversaciones)


def cmd_conversacion(args: argparse.Namespace) -> None:
    """Pone o quita `peticion` en una sola issue según quién habló el último. No lee el Project: le basta el repo."""
    owner, nombre = REPO.split("/", 1)
    salida = gh("api", "graphql", "-f", f"query={auditoria.CONSULTA_CONVERSACION}", "-f", f"owner={owner}",
                "-f", f"repo={nombre}", "-F", f"num={args.numero}")
    nodo = json.loads(salida)["data"]["repository"]["issue"]
    if nodo is None:
        raise ErrorTablero(f"La issue #{args.numero} no existe.")
    etiquetas = objetos.nombres_etiquetas(nodo)
    if objetos.ETIQUETA in etiquetas or lotes.ETIQUETA in etiquetas or flujo.ETIQUETA_CHAMBER in etiquetas:
        print(f"#{args.numero} es un objeto, un lote o una issue descartada: no lleva `peticion`")
        return
    accion = auditoria.accion_peticion(auditoria.conversacion_de(nodo))
    print(f"#{args.numero}: " + {"poner": "conversación sin contestar por el asignado → `peticion`",
                                 "quitar": "conversación contestada → fuera `peticion`", None: "sin cambios"}[accion]
          + ("" if args.aplicar or accion is None else " (simulación: usa --aplicar)"))
    if args.aplicar and accion:
        aplicar_conversaciones({args.numero: accion})


def aplicar_conversaciones(conversaciones: dict[int, str]) -> None:
    """Pone o quita `peticion` según quién habló el último en cada issue."""
    if "poner" in conversaciones.values():
        objetos.crear_etiqueta_si_falta(gh, REPO, peticiones.ETIQUETA, peticiones.COLOR, peticiones.DESCRIPCION_ETIQUETA)
    for numero, accion in conversaciones.items():
        gh("issue", "edit", str(numero), "--repo", REPO,
           "--add-label" if accion == "poner" else "--remove-label", peticiones.ETIQUETA)


def aplicar_auditoria(proyecto: dict, informe: list[tuple[dict, list[dict]]]) -> None:
    """Graves → Revisiones y P0; triviales → se corrigen; organización → etiqueta y comentario."""
    if any(p["tipo"] == "organizacion" for _, lista in informe for p in lista):
        objetos.crear_etiqueta_si_falta(gh, REPO, auditoria.ETIQUETA, auditoria.COLOR, auditoria.DESCRIPCION_ETIQUETA)
    for issue, lista in informe:
        accion = auditoria.acciones(issue, lista)
        numero = str(issue["numero"])
        if accion["reabrir"]:
            gh("issue", "reopen", numero, "--repo", REPO)
        for campo, valor in accion["campos"].items():
            poner_campo(proyecto, issue["numero"], campo, valor)
        for texto in accion["comentarios"]:
            comentar(issue["numero"], texto)
        if accion["etiquetar"]:
            gh("issue", "edit", numero, "--repo", REPO, "--add-label", auditoria.ETIQUETA)
        if accion["desetiquetar"]:
            gh("issue", "edit", numero, "--repo", REPO, "--remove-label", auditoria.ETIQUETA)


# --- colisiones -----------------------------------------------------------------------------------

def cmd_colisiones(args: argparse.Namespace) -> None:
    chamber = numeros_chamber()
    # Solo PR hacia una rama de línea (dev o dev-<modo>); cada una se compara con las de su misma base.
    en_linea = [p for p in prs_abiertas() if flujo.es_rama_de_linea(p["baseRefName"], INTEGRACION)]
    descartadas = {p["number"] for p in en_linea if solo_descartadas(p, chamber)}  # solo issues `chamber`: no cuentan
    prs = {p["number"]: p for p in en_linea if p["number"] not in descartadas}
    con_git = colisiones.traer_cabezas(prs)
    if not con_git:
        print("Aviso: no se pudieron descargar las cabezas de las PR; cuento los ficheros en común.\n")
    pares = colisiones.pares_por_base({n: colisiones.ficheros_de_pr(gh, REPO, n) for n in prs},
                                      {n: p["baseRefName"] for n, p in prs.items()},
                                      colisiones.conflicto_git if con_git else None)
    pares, localizacion = colisiones.separar(pares)  # solo localización: se regenera, sin issue
    abiertas = colisiones.colisiones_abiertas(gh, REPO)
    existentes = {i["title"].strip() for i in abiertas}
    nuevos = [par for par in pares if colisiones.titulo(par[0], par[1]) not in existentes]
    resueltas = colisiones.resueltas(abiertas, set(prs), {(a, b) for a, b, _ in pares},
                                     {(a, b) for a, b, _ in localizacion}, descartadas) if con_git else []
    print(f"## Colisiones entre PR abiertas contra su misma rama base · {len(prs)} PR, "
          f"{len(pares)} pares en conflicto, "
          f"{len(nuevos)} sin issue, {len(resueltas)} issues resueltas"
          + ("" if args.aplicar else " (simulación: usa --aplicar)") + "\n")
    for a, b, ficheros in pares:
        estado = "nueva" if (a, b, ficheros) in nuevos else "ya tiene issue"
        binarios = "; con binarios: decision" if colisiones.hay_binarios(ficheros) else ""
        regenerar = sum(colisiones.es_localizacion(f) for f in ficheros)
        extra = f"; además {regenerar} de localización" if regenerar else ""
        print(f"- PR #{a} y #{b}: {len(ficheros) - regenerar} ficheros en conflicto ({estado}{binarios}{extra})")
    if localizacion:
        lista = ", ".join(f"#{a}/#{b}" for a, b, _ in localizacion)
        print(f"- {len(localizacion)} pares solo de localización, sin issue (la regenera la PR que se fusione "
              f"en segundo lugar): {lista}")
    for numero, motivo in resueltas:
        print(f"- #{numero} resuelta: {motivo}")
    if not args.aplicar:
        return
    proyecto = cargar_proyecto() if nuevos or resueltas else None
    if nuevos:
        objetos.crear_etiqueta_si_falta(gh, REPO, colisiones.ETIQUETA, colisiones.COLOR, colisiones.DESCRIPCION_ETIQUETA)
        for a, b, ficheros in nuevos:
            crear_issue_colision(proyecto, prs[a], prs[b], ficheros)
    for numero, motivo in resueltas:
        comentar(numero, colisiones.texto_cierre(motivo))
        gh("issue", "close", str(numero), "--repo", REPO, "--reason", "completed")
        if numero in proyecto["items"]:  # sin esperar al sync: que no siga en Revisiones
            poner_campo(proyecto, numero, "Status", "Done")
        print(f"#{numero} cerrada y en Done: {motivo}")


def objeto_y_area(proyecto: dict, pr: dict) -> tuple[int | None, str | None]:
    """Objeto del que cuelga la primera issue enlazada de la PR y su Área, si se pueden saber."""
    for n in sorted(issues_de_pr(pr)):
        try:
            padre = (objetos.leer_issue(gh, REPO, n).get("parent") or {}).get("number")
        except (objetos.ErrorObjeto, ErrorTablero):
            continue
        area = proyecto["items"].get(n, {}).get("valores", {}).get("Área")
        if padre:
            return padre, area
    return None, None


def crear_issue_colision(proyecto: dict, pr_a: dict, pr_b: dict, ficheros: list[str]) -> None:
    antigua, reciente = sorted((pr_a, pr_b), key=lambda p: (p.get("createdAt") or "", p["number"]))
    titulo = colisiones.titulo(pr_a["number"], pr_b["number"])
    crear = ["issue", "create", "--repo", REPO, "--title", titulo,
             "--body", colisiones.cuerpo(antigua, reciente, ficheros, pr_a.get("baseRefName") or INTEGRACION)]
    for etiqueta in colisiones.etiquetas(ficheros):
        crear += ["--label", etiqueta]
    numero = int(gh(*crear).strip().splitlines()[-1].rstrip("/").rsplit("/", 1)[-1])
    padre, area = objeto_y_area(proyecto, pr_a)
    if padre:
        objetos.colgar(gh, REPO, numero, padre)
    for campo, valor in {"Status": "Revisiones", "Prioridad": "P1", "Tamaño": "S", "Área": area, "Fase": "Sin fase"}.items():
        if valor:
            poner_campo(proyecto, numero, campo, valor)
    print(f"#{numero} creada: {titulo}{f' (dentro de #{padre})' if padre else ''}")


# --- dependencias ---------------------------------------------------------------------------------

def cmd_bloquear(args: argparse.Namespace) -> None:
    """Registra de qué issues depende `numero` (relación nativa «blocked by») y la pasa a Bloqueada.

    En Backlog solo registra la dependencia: la issue aún no está aprobada y bloquearla no la aprueba.
    Un objeto o un lote no llevan Status: solo se registra la dependencia.
    """
    issue = objetos.leer_issue(gh, REPO, args.numero)
    try:
        nuevas = bloqueos.nuevas_dependencias(args.numero, args.por, bloqueos.bloqueantes(issue))
    except ValueError as exc:
        raise ErrorTablero(str(exc)) from exc
    for m in nuevas:
        bloqueante = objetos.leer_issue(gh, REPO, m)
        gh("api", "graphql", "-f", f"query={bloqueos.MUTACION}", "-f", f"issue={issue['id']}",
           "-f", f"bloqueante={bloqueante['id']}")
    todas = ", ".join(f"#{m}" for m in sorted({*args.por, *(b["number"] for b in bloqueos.bloqueantes(issue))}))
    proyecto = cargar_issue(args.numero)
    item = proyecto["items"].get(args.numero, {})
    if objetos.es_objeto(item) or lotes.es_lote(item):
        print(f"#{args.numero} es un objeto o un lote: depende de {todas}, "
              f"sin Status ni etiqueta `{bloqueos.ETIQUETA}`."
              + (" Para meter miembros en un lote, `lote añadir`." if lotes.es_lote(item) else ""))
        return
    actual = item.get("valores", {}).get("Status")
    if bloqueos.estado_tras_bloquear(actual) is None:
        print(f"#{args.numero} sigue en Backlog; depende de {todas}. Al aprobarla pasará a Bloqueada si siguen abiertas.")
        return
    poner_campo(proyecto, args.numero, "Status", bloqueos.ESTADO)
    gh("issue", "edit", str(args.numero), "--repo", REPO, "--add-label", bloqueos.ETIQUETA)
    print(f"#{args.numero} → Bloqueada; depende de {todas}. `sync` la pasa a Ready cuando se cierren.")


# --- descartes ------------------------------------------------------------------------------------

# Etiquetas que piden algo a una persona: en una issue descartada nadie va a atenderlas.
ETIQUETAS_DE_PERSONAS = (peticiones.ETIQUETA, flujo.ETIQUETA_DECISION, auditoria.ETIQUETA, auditoria.ETIQUETA_QA,
                         bloqueos.ETIQUETA)
PASOS_DESCARTE = ("etiquetar", "quitar", "desasignar", "comentar", "backlog", "cerrar")


def cmd_chamber(args: argparse.Namespace) -> None:
    """Descarta issues: etiqueta `chamber`, motivo, sin asignados ni avisos, en Backlog y cerradas como not planned.

    Idempotente: lo que ya está hecho no se repite. Una issue ya cerrada (p. ej. completada) no se reabre. Un objeto o
    un lote se cierra sin tocar su Status (no lleva). Nada se propaga solo: se avisa de las PR abiertas que las enlazan
    (`sync` lo repite), de los lotes de los que son miembro, de las issues que dependían de ellas y de sus
    sub-issues vivas. Solo lo lanza un aprobador: el dueño del `gh` en local o quien dispara el puente a mano.
    """
    if motivo := flujo.motivo_para_no_descartar(quien_lanza(), CONFIG["aprobadores"]):
        raise ErrorTablero(motivo)
    try:
        texto = flujo.texto_chamber(args.motivo)
    except ValueError as exc:
        raise ErrorTablero(str(exc)) from exc
    objetos.crear_etiqueta_si_falta(gh, REPO, flujo.ETIQUETA_CHAMBER, flujo.COLOR_CHAMBER, flujo.DESCRIPCION_CHAMBER)
    numeros = list(dict.fromkeys(args.numeros))
    for numero in numeros:
        descartar(numero, texto)
    for linea in prs_con_descartadas(prs_abiertas(), set(numeros), numeros_chamber()):
        print(linea)


def descartar(numero: int, texto: str) -> None:
    datos = json.loads(gh("issue", "view", str(numero), "--repo", REPO, "--json", "state,labels,assignees,comments"))
    proyecto = cargar_issue(numero)
    pasos = flujo.pasos_chamber(datos, texto, proyecto["items"].get(numero), ETIQUETAS_DE_PERSONAS)
    # La etiqueta va primero: el comentario dispara «Conversación en las issues», que así ya la ve descartada.
    if pasos["etiquetar"]:
        gh("issue", "edit", str(numero), "--repo", REPO, "--add-label", flujo.ETIQUETA_CHAMBER)
    if pasos["quitar"]:
        gh("issue", "edit", str(numero), "--repo", REPO, "--remove-label", ",".join(pasos["quitar"]))
    if pasos["desasignar"]:
        gh("issue", "edit", str(numero), "--repo", REPO, "--remove-assignee", ",".join(pasos["desasignar"]))
    if pasos["comentar"]:
        comentar(numero, texto)
    if pasos["backlog"]:
        poner_campo(proyecto, numero, "Status", "Backlog")  # fuera de las columnas de trabajo del Kanban
    if pasos["cerrar"]:
        gh("issue", "close", str(numero), "--repo", REPO, "--reason", "not planned")
    hecho = [nombre for nombre in PASOS_DESCARTE if pasos[nombre]]
    print(f"#{numero} descartada ({', '.join(hecho)})" if hecho else f"#{numero} ya estaba descartada")
    for aviso in flujo.avisos_de_descarte(numero, objetos.leer_issue(gh, REPO, numero)):
        print(aviso)


def prs_con_descartadas(prs: list[dict], numeros: set[int], chamber: set[int]) -> list[str]:
    """Un aviso por PR abierta que enlaza alguna de `numeros`: sin issue viva, su código no debe entrar en dev."""
    lineas = []
    for pr in prs:
        enlazadas = sorted(issues_de_pr(pr) & numeros)
        if not enlazadas:
            continue
        cuales = ", ".join(f"#{n}" for n in enlazadas)
        accion = "ciérrala" if solo_descartadas(pr, chamber | numeros) else f"quítale el «Closes» de {cuales}"
        lineas.append(f"Aviso: la PR #{pr['number']} ({pr['author']['login']}) enlaza {cuales}, descartada: {accion}")
    return lineas


# --- opciones de Status ---------------------------------------------------------------------------

def cmd_asegurar_estados(args: argparse.Namespace) -> None:
    """Añade Bloqueada y Validada a Status y corrige descripciones, conservando el Status de los items."""
    antes = estados.leer(gh, OWNER, NUMERO)
    nuevas = estados.opciones_objetivo(antes["opciones"], estados.NUEVAS, estados.DESCRIPCIONES)
    if nuevas is None:
        print("Status ya está al día: " + ", ".join(f"{o['name']} ({o['id']})" for o in antes["opciones"]))
        return
    if not args.aplicar:
        print("Opciones de Status (simulación: usa --aplicar):")
        print("\n".join(f"- {o['name']}: {o['description']}" for o in nuevas))
        return
    foto = Path(tempfile.gettempdir()) / f"tablero-status-{datetime.now():%Y%m%d-%H%M%S}.json"
    foto.write_text(json.dumps(antes["foto"], ensure_ascii=False, indent=1), encoding="utf-8")
    opciones = {o["name"]: o["id"] for o in estados.redefinir(gh, antes["campo"], nuevas)}
    borrar_cache_campos()  # los ids de las opciones de Status han cambiado
    despues = estados.leer(gh, OWNER, NUMERO)
    cambios = estados.pendientes_de_restaurar(antes["foto"], despues["foto"])
    estados.restaurar(gh, despues, opciones, cambios)
    final = estados.pendientes_de_restaurar(antes["foto"], estados.leer(gh, OWNER, NUMERO)["foto"])
    print("Opciones de Status: " + ", ".join(f"{n} ({i})" for n, i in opciones.items()))
    print(f"Foto de {len(antes['foto'])} items en {foto}; restaurados {len(cambios)}; "
          f"distintos de la foto al final: {len(final)}.")


def anadir_comandos(sub: argparse._SubParsersAction) -> None:
    p = sub.add_parser("resumen", help="comentar el **Resumen** de una issue (qué fallaba, por qué y cómo se arregló)")
    p.add_argument("numero", type=int)
    p.add_argument("--que", required=True, help=f"qué fallaba (máx. {memoria.MAX_CARACTERES} caracteres)")
    p.add_argument("--por-que", dest="por_que", help=f"causa, si se sabe (máx. {memoria.MAX_CARACTERES} caracteres)")
    p.add_argument("--como", required=True, help=f"cómo se arregló (máx. {memoria.MAX_CARACTERES} caracteres)")
    p.add_argument("--pr", type=int)
    p.set_defaults(fn=cmd_resumen)
    p = sub.add_parser("decidir", help="comentar una **Decisión** en la issue u objeto afectado")
    p.add_argument("numero", type=int)
    p.add_argument("--texto", required=True, help=f"la decisión (máx. {memoria.MAX_CARACTERES} caracteres)")
    p.add_argument("--quien", help="quién decide (por defecto, tu login)")
    p.set_defaults(fn=cmd_decidir)
    for nombre, ayuda, fn in (("auditar", "problemas de organización de las issues de trabajo", cmd_auditar),
                              ("colisiones", "PR abiertas contra la misma rama base que chocan al mezclarse",
                               cmd_colisiones),

                              ("asegurar-estados", "añadir Bloqueada y Validada a Status sin perder valores",
                               cmd_asegurar_estados)):
        p = sub.add_parser(nombre, help=ayuda)
        p.add_argument("--aplicar", action="store_true")
        p.set_defaults(fn=fn)
    p = sub.add_parser("conversacion", help="poner o quitar `peticion` en una issue según quién habló el último")
    p.add_argument("numero", type=int)
    p.add_argument("--aplicar", action="store_true")
    p.set_defaults(fn=cmd_conversacion)
    p = sub.add_parser("chamber", help="descartar issues: etiqueta `chamber`, motivo, sin asignados ni avisos, en "
                                       "Backlog y cerradas como not planned (idempotente)")
    p.add_argument("numeros", type=int, nargs="+", metavar="numero")
    p.add_argument("--motivo", required=True, help="por qué se descarta (una línea)")
    p.set_defaults(fn=cmd_chamber)
    p = sub.add_parser("bloquear", help="registrar de qué issues depende una y pasarla a Bloqueada (en Backlog se queda)")
    p.add_argument("numero", type=int)
    p.add_argument("--por", type=int, action="append", required=True, help="issue de la que depende (repetible)")
    p.set_defaults(fn=cmd_bloquear)
