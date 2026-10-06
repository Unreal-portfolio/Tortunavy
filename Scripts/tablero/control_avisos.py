"""Comandos de notificaciones: `avisos` (lo que ha entrado en dev sin revisión y el resultado de la rutina, por correo
al director) y `silenciar` (baja del dueño del token en las issues abiertas que toca el puente).

La lógica pura vive en avisos.py y volcado.py; aquí solo se habla con GitHub.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from datetime import datetime, timedelta, timezone

import avisos
import flujo
import lotes
import objetos
import volcado
from base import (CONFIG, INTEGRACION, REPO, ErrorTablero, cargar_proyecto, comentar, elegir_revisor, gh, issues_de_pr,
                  poner_campo, usuario_actual)

RUTAS_ORGANIZACION = CONFIG["avisos"]["rutas_organizacion"]
DESTINATARIOS = CONFIG["avisos"]["destinatarios"]
# El comentario con la mención lo tiene que escribir otra cuenta: GitHub no avisa a nadie de lo que hace él mismo.
VARIABLE_TOKEN_AVISOS = "AVISOS_TOKEN"


def leer_pushes(desde: datetime) -> list[dict]:
    salida = gh("api", f"repos/{REPO}/activity?ref={INTEGRACION}&time_period=week&per_page=100")
    return avisos.pushes_directos(json.loads(salida), desde)


def leer_commits(push: dict) -> list[dict]:
    salida = gh("api", f"repos/{REPO}/compare/{push['antes']}...{push['despues']}")
    return [{"sha": c["sha"], "titulo": c["commit"]["message"].splitlines()[0], "padres": len(c["parents"])}
            for c in json.loads(salida).get("commits", [])][-avisos.MAX_COMMITS:]


def leer_prs(commits: list[dict]) -> dict[str, list[dict]]:
    return {c["sha"]: json.loads(gh("api", f"repos/{REPO}/commits/{c['sha']}/pulls")) for c in commits}


def con_codigo(commits: list[dict]) -> list[dict]:
    """Commits que tocan algo más que rutas de organización (tablero, skills, workflows, guía, documentación)."""
    return [c for c in commits if not avisos.es_organizativo(
        json.loads(gh("api", f"repos/{REPO}/commits/{c['sha']}", "--jq", "[.files[].filename]")), RUTAS_ORGANIZACION)]


def prs_sin_validar(proyecto: dict, desde: datetime) -> list[str]:
    """Incidencias de las PR fusionadas en dev desde `desde`."""
    campos = "number,body,headRefName,mergedAt,mergedBy,files"
    prs = json.loads(gh("pr", "list", "--repo", REPO, "--state", "merged", "--base", INTEGRACION, "--limit", "50",
                        "--json", campos))
    lotes_ = {n for n, i in proyecto["items"].items() if lotes.es_lote(i)}
    lineas = []
    for pr in sorted(prs, key=lambda p: p["mergedAt"]):
        if avisos.fecha(pr["mergedAt"]) < desde:
            continue
        refs = issues_de_pr(pr) | (issues_de_pr(pr, menciones=True) & lotes_)  # su lote va con «Refs #lote»
        if linea := avisos.pr_sin_validar({**pr, "refs": refs}, proyecto["items"], RUTAS_ORGANIZACION, lotes_):
            lineas.append(linea)
    return lineas


def issues_sin_revision() -> dict[str, int]:
    """Título → número de las issues `sin-revision`, abiertas o cerradas, para no duplicar un push."""
    salida = gh("issue", "list", "--repo", REPO, "--state", "all", "--label", avisos.ETIQUETA,
                "--limit", "200", "--json", "title,number")
    return {i["title"].strip(): i["number"] for i in json.loads(salida)}


def crear_issue(proyecto: dict, push: dict, commits: list[dict]) -> int:
    objetos.crear_etiqueta_si_falta(gh, REPO, avisos.ETIQUETA, avisos.COLOR, avisos.DESCRIPCION_ETIQUETA)
    crear = ["issue", "create", "--repo", REPO, "--title", avisos.titulo_issue(push["actor"], push["despues"]),
             "--label", avisos.ETIQUETA, "--body", avisos.cuerpo_issue(push, commits, REPO, INTEGRACION)]
    if push["actor"] in CONFIG["miembros"]:
        crear += ["--assignee", push["actor"]]
    numero = int(gh(*crear).strip().splitlines()[-1].rstrip("/").rsplit("/", 1)[-1])
    campos = {"Status": "In review", "Prioridad": "P0", "Tamaño": "S", "Fase": "Sin fase",
              "Revisión IA": "Pendiente", "Editor": "Sin probar"}
    if push["actor"] in CONFIG["miembros"]:
        campos["Revisor"] = elegir_revisor(proyecto, push["actor"])
    for campo, valor in campos.items():
        poner_campo(proyecto, numero, campo, valor)
    return numero


def incidencias(proyecto: dict, desde: datetime, aplicar: bool) -> list[str]:
    """Una línea por push directo desde `desde`; con `aplicar`, abre la issue `sin-revision` de los que traen código."""
    existentes, lista = None, []
    for push in leer_pushes(desde):
        try:
            commits = leer_commits(push)
        except ErrorTablero as exc:  # p. ej. un push forzado cuyo commit anterior ya no existe
            lista.append(f"**{push['actor']}** subió a dev por push directo ({push['cuando']:%d-%m %H:%M} UTC) "
                         f"y no se puede leer su diff: {exc}")
            continue
        prs_por_commit = leer_prs(commits)
        sin_pr = con_codigo(avisos.commits_sin_pr(commits, prs_por_commit, push["cuando"]))
        prs = avisos.prs_del_push(commits, prs_por_commit, push["cuando"])
        if not sin_pr and not prs:
            continue
        numero = None
        if sin_pr:
            existentes = issues_sin_revision() if existentes is None else existentes
            titulo = avisos.titulo_issue(push["actor"], push["despues"])
            numero = existentes.get(titulo)
            if numero is None and aplicar:
                numero = existentes[titulo] = crear_issue(proyecto, push, sin_pr)
        lista.append(avisos.linea_push(push, sin_pr, prs, numero))
    return lista


def reconciliar(proyecto: dict, aplicar: bool) -> list[str]:
    """Issues `sin-revision` abiertas: su código ya está en dev, así que avanzan solo con las dos validaciones."""
    cambios = []
    for numero, issue in sorted(proyecto["items"].items()):
        if issue["state"] != "OPEN" or avisos.ETIQUETA not in objetos.nombres_etiquetas(issue) \
                or flujo.es_chamber(issue):
            continue
        actual = issue["valores"].get("Status")
        destino, cerrar = flujo.estado_objetivo(actual, issue["valores"], fusionada=True, en_lote=False)
        if not destino or destino == actual:
            continue
        cambios.append(f"#{numero} → {destino}" + (" (revisada y probada: se cierra)" if cerrar else ""))
        if not aplicar:
            continue
        poner_campo(proyecto, numero, "Status", destino)
        if cerrar:
            comentar(numero, f"Fusionada en `{INTEGRACION}` por push directo, revisada y probada: Done.")
            gh("issue", "close", str(numero), "--repo", REPO, "--reason", "completed")
    return cambios


def leer_parte(numero: int | None, ahora: datetime) -> str | None:
    if numero is None:
        return None
    comentarios = json.loads(gh("api", f"repos/{REPO}/issues/{numero}/comments?per_page=100"))
    return avisos.parte_reciente(comentarios, ahora)


def publicar(numero: int, texto: str) -> None:
    """Comenta con el token de AVISOS_TOKEN si está definido (en el workflow, el de Actions), no con el del tablero."""
    entorno = dict(os.environ)
    if entorno.get(VARIABLE_TOKEN_AVISOS):
        entorno["GH_TOKEN"] = entorno[VARIABLE_TOKEN_AVISOS]
    proc = subprocess.run(["gh", "issue", "comment", str(numero), "--repo", REPO, "--body-file", "-"],
                          capture_output=True, text=True, encoding="utf-8", input=texto, env=entorno)
    if proc.returncode != 0:
        raise ErrorTablero(f"gh issue comment {numero}: {proc.stderr.strip()}")


def leer_comentarios(desde: datetime) -> list[dict]:
    """Comentarios de issues y de revisión de PR actualizados desde `desde` (la API filtra por actualización)."""
    lista = []
    for ruta in ("issues/comments", "pulls/comments"):
        salida = gh("api", "--paginate", f"repos/{REPO}/{ruta}?since={desde:%Y-%m-%dT%H:%M:%SZ}&per_page=100",
                    "--jq", ".[] | @json")
        lista += [json.loads(linea) for linea in salida.splitlines() if linea.strip()]
    return lista


def extras(login: str, desde: datetime) -> list[tuple[str, list[str]]]:
    """Secciones «PR nuevas» y «Te mencionan» del aviso de `login`."""
    campos = "number,title,author,baseRefName,createdAt,state,isDraft"
    prs = json.loads(gh("pr", "list", "--repo", REPO, "--state", "all", "--limit", "60", "--json", campos))
    return [("PR nuevas", [avisos.linea_pr(p, INTEGRACION) for p in avisos.prs_nuevas(prs, desde)]),
            ("Te mencionan", [avisos.linea_mencion(c) for c in avisos.menciones(leer_comentarios(desde), login, desde)])]


def cmd_avisos(args: argparse.Namespace) -> None:
    ahora = datetime.now(timezone.utc)
    proyecto = cargar_proyecto()
    desde = ahora - timedelta(hours=args.horas)
    lista = incidencias(proyecto, desde, args.aplicar)
    cambios = reconciliar(proyecto, args.aplicar)
    if args.aplicar and (lista or cambios):
        proyecto = cargar_proyecto()  # con las issues recién creadas y los estados nuevos
    trabajo = [i for i in proyecto["items"].values()
               if i["state"] == "OPEN" and not objetos.es_objeto(i) and not lotes.es_lote(i)]
    lista += prs_sin_validar(proyecto, desde)
    parte = leer_parte(args.parte, ahora)
    print(f"## Avisos · {ahora:%Y-%m-%d %H:%M} UTC" + ("" if args.aplicar else " (simulación: usa --aplicar)") + "\n")
    print("\n".join(f"- {c}" for c in cambios) or "- sin cambios en las issues `sin-revision`")
    for login in DESTINATARIOS:
        aprobador = login in CONFIG["aprobadores"]
        texto = avisos.render(login, avisos.secciones(trabajo, login, aprobador, ahora, CONFIG["dias_sin_movimiento"]),
                              lista, ahora, parte, extras(login, desde))
        if texto is None:
            print(f"\n{login}: nada que avisar")
            continue
        print(f"\n{texto}")
        if args.publicar is not None:
            publicar(args.publicar, texto)
    if args.publicar is not None:
        print(f"Avisos publicados en #{args.publicar}")


def cmd_silenciar(_args: argparse.Namespace) -> None:
    """Da de baja al usuario del token de las notificaciones de las issues abiertas (no de las PR).

    Sin el scope `notifications` no se puede: avisa y termina bien, para no dar por fallido un puente que sí
    ha reconciliado y volcado el tablero.
    """
    try:
        silenciar_issues_abiertas()
    except ErrorTablero as exc:
        if not volcado.falta_scope_de_notificaciones(str(exc)):
            raise
        print("::warning::No se silencian las issues: al token le falta el scope `notifications` "
              "(añádelo al token del secreto TABLERO_TOKEN).")


def silenciar_issues_abiertas() -> None:
    owner, repo = REPO.split("/", 1)
    nodos, cursor = [], None
    while True:
        args = ["api", "graphql", "-f", f"query={volcado.CONSULTA_SUSCRIPCIONES}", "-f", f"owner={owner}", "-f", f"repo={repo}"]
        if cursor:
            args += ["-f", f"cursor={cursor}"]
        datos = json.loads(gh(*args))["data"]["repository"]["issues"]
        nodos += datos["nodes"]
        if not datos["pageInfo"]["hasNextPage"]:
            break
        cursor = datos["pageInfo"]["endCursor"]
    pendientes = volcado.a_silenciar(nodos)
    for nodo in pendientes:
        gh("api", "graphql", "-f", f"query={volcado.MUTACION_SILENCIAR}", "-f", f"id={nodo['id']}")
    print(f"Silenciadas {len(pendientes)} issues de {len(nodos)} abiertas para {usuario_actual()}")


def anadir_comandos(sub: argparse._SubParsersAction) -> None:
    p = sub.add_parser("avisos", help="lo que ha entrado en dev sin revisión y el parte de la rutina, por correo al director")
    p.add_argument("--aplicar", action="store_true", help="abrir las issues `sin-revision` y avanzar las validadas")
    p.add_argument("--publicar", type=int, metavar="ISSUE", help="comentar en esa issue el aviso de cada destinatario")
    p.add_argument("--parte", type=int, metavar="ISSUE", help="issue del parte de la rutina, para adjuntarlo")
    p.add_argument("--horas", type=int, default=26, help="ventana de los pushes y las fusiones (por defecto, 26 h)")
    p.set_defaults(fn=cmd_avisos)
    sub.add_parser("silenciar", help="darme de baja de las notificaciones de las issues abiertas (no de las PR)"
                   ).set_defaults(fn=cmd_silenciar)
