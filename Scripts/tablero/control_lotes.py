"""Comandos de lotes (`lote crear`, `lote añadir`, `lote estado`) y de memoria por objeto (`resumenes`).

La lógica pura de los lotes vive en lotes.py; aquí solo se habla con GitHub.
"""

from __future__ import annotations

import argparse
import json

import bloqueos
import flujo
import lotes
import memoria
import objetos
from base import (CONFIG, REPO, ErrorTablero, cargar_proyecto, comentar, gh, issues_de_pr, item_de_issue, prs_abiertas,
                  quien_lanza)

CONSULTA_HERMANAS = """
query($owner: String!, $repo: String!, $num: Int!) {
  repository(owner: $owner, name: $repo) { issue(number: $num) {
    number title
    subIssues(first: 100) { nodes { number title state labels(first: 20) { nodes { name } }
      comments(last: 30) { nodes { body } } } }
    parent { number title
      subIssues(first: 100) { nodes { number title state labels(first: 20) { nodes { name } }
        comments(last: 30) { nodes { body } } } } }
  } }
}
"""


def pr_de_miembros(miembros: list[int]) -> int | None:
    """PR abierta que enlaza a los miembros; error si hay varias distintas."""
    candidatas = {p["number"] for p in prs_abiertas() if issues_de_pr(p) & set(miembros)}
    if len(candidatas) > 1:
        raise ErrorTablero(f"Los miembros están en varias PR abiertas ({sorted(candidatas)}): indica cuál con --pr.")
    return next(iter(candidatas), None)


def pr_del_lote(lote: int) -> int | None:
    """PR abierta que enlaza el lote con «Refs #lote»; error si hay varias."""
    candidatas = {p["number"] for p in prs_abiertas() if lote in issues_de_pr(p, menciones=True)}
    if len(candidatas) > 1:
        raise ErrorTablero(f"El lote #{lote} está enlazado desde varias PR abiertas ({sorted(candidatas)}).")
    return next(iter(candidatas), None)


def comprobar_de_trabajo(proyecto: dict, miembros: list[int]) -> None:
    """Error si algún miembro es un objeto o un lote."""
    ajenas = [n for n in miembros if objetos.es_objeto(proyecto["items"].get(n, {})) or lotes.es_lote(proyecto["items"].get(n, {}))]
    if ajenas:
        raise ErrorTablero(f"{', '.join(f'#{n}' for n in ajenas)} no son issues de trabajo (objeto o lote).")


def vincular_miembros(lote: int, id_lote: str, miembros: list[int], pr: int | None) -> None:
    """El lote queda «blocked by» cada miembro, y cada miembro lo dice en un comentario."""
    for m in miembros:
        gh("api", "graphql", "-f", f"query={bloqueos.MUTACION}", "-f", f"issue={id_lote}",
           "-f", f"bloqueante={objetos.leer_issue(gh, REPO, m)['id']}")
        comentar(m, f"En el lote #{lote}{f' (PR #{pr})' if pr else ''}.")


def asignados_de(proyecto: dict, numeros: list[int]) -> set[str]:
    return {a["login"] for n in numeros for a in (proyecto["items"].get(n, {}).get("assignees") or {}).get("nodes", [])}


def comprobar_topes(proyecto: dict, miembros: list[int], personas: set[str], args: argparse.Namespace,
                    lote: int | None = None) -> str | None:
    """Aplica los topes de lotes (lotes.py) antes de tocar nada. Devuelve el comentario de la excepción, si hace falta.

    `miembros` son todos los del lote tal como quedará; `personas`, las que lo tendrían abierto. Sin excederse, la
    excepción no se usa; excedido y sin `--excepcion`, error. Un lote `excepcion` o de solo `refactor` no tiene topes.
    """
    excepcion = getattr(args, "excepcion", None)
    etiquetas = [objetos.nombres_etiquetas(proyecto["items"].get(n, {})) for n in miembros]
    propias = objetos.nombres_etiquetas(proyecto["items"].get(lote, {})) if lote else set()
    if lotes.fuera_de_topes(propias, etiquetas):
        return None
    motivos = lotes.excesos(len(miembros), personas, lotes.lotes_abiertos(proyecto["items"]), excluir=lote)
    if not motivos:
        if excepcion:
            print("El lote cabe en los topes: no hace falta la excepción.")
        return None
    if not excepcion:
        raise ErrorTablero(f"El lote no cabe en los topes ({'; '.join(motivos)}). Pártelo o pide permiso a "
                           "SkiTemplar o Mokius: --excepcion \"<motivo>\" [--autoriza <aprobador>].")
    try:
        autoriza = lotes.autorizacion(quien_lanza(), getattr(args, "autoriza", None), CONFIG["aprobadores"])
        return lotes.texto_excepcion(motivos, excepcion, autoriza)
    except lotes.ErrorLote as exc:
        raise ErrorTablero(str(exc)) from exc


def marcar_excepcion(lote: int, texto: str) -> None:
    """Etiqueta `excepcion` en la issue `lote` y el comentario de una línea con el motivo y quién autoriza."""
    objetos.crear_etiqueta_si_falta(gh, REPO, lotes.ETIQUETA_EXCEPCION, lotes.COLOR_EXCEPCION,
                                    lotes.DESCRIPCION_EXCEPCION)
    gh("issue", "edit", str(lote), "--repo", REPO, "--add-label", lotes.ETIQUETA_EXCEPCION)
    comentar(lote, texto)


def cmd_lote_crear(args: argparse.Namespace) -> None:
    try:
        miembros = lotes.comprobar_miembros(args.miembros)
    except lotes.ErrorLote as exc:
        raise ErrorTablero(str(exc)) from exc
    proyecto = cargar_proyecto()
    comprobar_de_trabajo(proyecto, miembros)
    excepcion = comprobar_topes(proyecto, miembros, {quien_lanza(), *asignados_de(proyecto, miembros)}, args)
    pr = args.pr or pr_de_miembros(miembros)
    objetos.crear_etiqueta_si_falta(gh, REPO, lotes.ETIQUETA, lotes.COLOR, lotes.DESCRIPCION_ETIQUETA)
    url = gh("issue", "create", "--repo", REPO, "--title", lotes.titulo(args.titulo), "--label", lotes.ETIQUETA,
             "--body", lotes.cuerpo(miembros, pr)).strip().splitlines()[-1]
    numero = int(url.rstrip("/").rsplit("/", 1)[-1])
    vincular_miembros(numero, objetos.leer_issue(gh, REPO, numero)["id"], miembros, pr)
    item_de_issue(proyecto, numero)  # en el tablero sin Status, como los objetos
    if excepcion:
        marcar_excepcion(numero, excepcion)
    if pr:
        cuerpo = json.loads(gh("pr", "view", str(pr), "--repo", REPO, "--json", "body"))["body"] or ""
        if f"Refs #{numero}" not in cuerpo:
            gh("pr", "edit", str(pr), "--repo", REPO, "--body", f"{cuerpo.rstrip()}\n\nRefs #{numero}")
    print(f"Lote #{numero} creado con {', '.join(f'#{m}' for m in miembros)}"
          + (f"; enlazado a la PR #{pr}." if pr else ". Añade «Refs #%d» a la PR del lote." % numero))


def cmd_lote_anadir(args: argparse.Namespace) -> None:
    """Mete issues en un lote ya creado: dependencia, comentario en cada una y casilla en el cuerpo del lote."""
    datos = json.loads(gh("issue", "view", str(args.lote), "--repo", REPO, "--json", "title,labels,state,body"))
    if not lotes.es_lote(datos) or datos["state"] != "OPEN":
        raise ErrorTablero(f"#{args.lote} no es un lote abierto (etiqueta `{lotes.ETIQUETA}`).")
    lote = objetos.leer_issue(gh, REPO, args.lote)
    actuales = [b["number"] for b in bloqueos.bloqueantes(lote)]
    try:
        nuevos = lotes.miembros_nuevos(args.lote, args.miembros, set(actuales))
    except lotes.ErrorLote as exc:
        raise ErrorTablero(str(exc)) from exc
    proyecto = cargar_proyecto()
    comprobar_de_trabajo(proyecto, nuevos)
    excepcion = comprobar_topes(proyecto, actuales + nuevos, asignados_de(proyecto, nuevos), args, lote=args.lote)
    pr = pr_del_lote(args.lote)
    vincular_miembros(args.lote, lote["id"], nuevos, pr)
    if excepcion:
        marcar_excepcion(args.lote, excepcion)
    gh("issue", "edit", str(args.lote), "--repo", REPO, "--body-file", "-",
       entrada=lotes.cuerpo_con_miembros(datos["body"] or "", nuevos))
    lista = ", ".join(f"#{n}" for n in nuevos)
    print(f"Lote #{args.lote}: añadidas {lista}" + (f"; su PR es la #{pr}." if pr else "; aún sin PR enlazada."))
    if pr:
        cierra = issues_de_pr(next(p for p in prs_abiertas() if p["number"] == pr))
        if faltan := [n for n in nuevos if n not in cierra]:
            print(f"Aviso: añade «Closes {', '.join(f'#{n}' for n in faltan)}» al cuerpo de la PR #{pr}.")


def etiquetas_de_miembros(nodos: list[dict], items: dict[int, dict]) -> dict[int, set[str]]:
    """Etiquetas de cada miembro, de la dependencia y de su tarjeta: una descartada puede estar fuera del tablero."""
    return {b["number"]: objetos.nombres_etiquetas(b) | objetos.nombres_etiquetas(items.get(b["number"], {}))
            for b in nodos}


def cmd_lote_estado(args: argparse.Namespace) -> None:
    """Qué miembros faltan. Termina con error si la PR del lote aún no se puede fusionar."""
    datos = json.loads(gh("issue", "view", str(args.numero), "--repo", REPO, "--json", "title,labels,state"))
    if not lotes.es_lote(datos):
        raise ErrorTablero(f"#{args.numero} no es un lote (etiqueta `{lotes.ETIQUETA}`).")
    proyecto = cargar_proyecto()
    nodos = bloqueos.bloqueantes(objetos.leer_issue(gh, REPO, args.numero))
    miembros = {b["number"]: (proyecto["items"].get(b["number"], {}).get("valores", {}), b["state"]) for b in nodos}
    print(f"{datos['title']} (#{args.numero}, {datos['state']}): {len(miembros)} miembros")
    for n, (valores, estado) in sorted(miembros.items()):
        print(f"  #{n} {valores.get('Status') or estado}")
    etiquetas = etiquetas_de_miembros(nodos, proyecto["items"])
    if descartadas := lotes.con_decision(etiquetas, flujo.ETIQUETA_CHAMBER):  # cerrada no es lista: no se revisó
        raise ErrorTablero(f"NO fusionar la PR del lote: {', '.join(f'#{n}' for n in descartadas)} descartada "
                           f"(`{flujo.ETIQUETA_CHAMBER}`): saca su código y su «Closes» de la PR y quítala del lote.")
    pendientes = lotes.pendientes(miembros, {n for n, e in etiquetas.items() if flujo.ETIQUETA_REFACTOR in e})
    if not miembros or pendientes:
        detalle = "; ".join(f"#{n}: {', '.join(f)}" for n, f in pendientes.items()) or "el lote no tiene miembros"
        raise ErrorTablero(f"NO fusionar la PR del lote: {detalle}.")
    if sin_decidir := lotes.con_decision(etiquetas, flujo.ETIQUETA_DECISION):
        raise ErrorTablero(f"NO fusionar la PR del lote: {', '.join(f'#{n}' for n in sin_decidir)} con decisión "
                           "pendiente. Regístrala con `tablero.py decidir <n> --texto \"...\"` y quita la etiqueta.")
    if sin_contestar := lotes.con_decision(etiquetas, "peticion"):
        raise ErrorTablero(f"NO fusionar la PR del lote: {', '.join(f'#{n}' for n in sin_contestar)} con una "
                           "conversación sin contestar (etiqueta `peticion`): léela y resuélvela antes.")
    print("Todos los miembros están en Validada: la PR del lote se puede fusionar.")


def linea_hermana(hermana: dict) -> str:
    """Cabecera de una sub-issue en `resumenes`; las descartadas van marcadas: su memoria no es de un sistema vivo."""
    estado = f"{hermana['state']}, {flujo.ETIQUETA_CHAMBER}" if flujo.es_chamber(hermana) else hermana["state"]
    return f"#{hermana['number']} ({estado}) {hermana['title']}"


def cmd_resumenes(args: argparse.Namespace) -> None:
    """Resúmenes de las demás sub-issues del mismo objeto (abiertas y cerradas), para ver fallos parecidos.

    Las descartadas (`chamber`) se listan marcadas «(…, chamber)»: lo que se aprendió sigue valiendo, pero no son
    trabajo vivo.
    """
    owner, nombre = REPO.split("/", 1)
    salida = gh("api", "graphql", "-f", f"query={CONSULTA_HERMANAS}", "-f", f"owner={owner}",
                "-f", f"repo={nombre}", "-F", f"num={args.numero}")
    issue = json.loads(salida)["data"]["repository"]["issue"]
    if issue is None:
        raise ErrorTablero(f"La issue #{args.numero} no existe.")
    objeto = issue["parent"] or issue  # si se pide un objeto, sus propias sub-issues
    hermanas = [h for h in objeto["subIssues"]["nodes"] if h["number"] != args.numero]
    descartadas = sum(flujo.es_chamber(h) for h in hermanas)
    print(f"Objeto #{objeto['number']} {objeto['title']}: {len(hermanas)} sub-issues más"
          + (f" ({descartadas} descartadas)" if descartadas else ""))
    con_resumen = 0
    for h in hermanas:
        textos = [c["body"].strip() for c in h["comments"]["nodes"] if memoria.es_resumen(c["body"])]
        if textos:
            con_resumen += 1
            print(f"\n{linea_hermana(h)}\n{textos[-1]}")
    if not con_resumen:
        print("Ninguna tiene aún comentario **Resumen**.")


def anadir_excepcion(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--excepcion", metavar="MOTIVO",
                        help=f"saltar los topes ({lotes.MAXIMO_MIEMBROS} issues por lote, "
                             f"{lotes.MAXIMO_LOTES_POR_PERSONA} lote abierto por persona) con permiso de un aprobador")
    parser.add_argument("--autoriza", choices=list(CONFIG["aprobadores"]),
                        help="aprobador que autoriza la excepción (obligatorio si no lo eres)")


def anadir_comandos(sub: argparse._SubParsersAction) -> None:
    p = sub.add_parser("lote", help="lotes: varias issues en una misma PR")
    acciones = p.add_subparsers(dest="accion", required=True)
    q = acciones.add_parser("crear", help="crear el lote de una PR con sus issues")
    q.add_argument("--titulo", required=True)
    q.add_argument("--pr", type=int, help="PR del lote (por defecto, la PR abierta que enlaza a los miembros)")
    q.add_argument("miembros", type=int, nargs="+")
    anadir_excepcion(q)
    q.set_defaults(fn=cmd_lote_crear)
    q = acciones.add_parser("añadir", aliases=["anadir"], help="meter issues en un lote ya creado")
    q.add_argument("lote", type=int)
    q.add_argument("miembros", type=int, nargs="+")
    anadir_excepcion(q)
    q.set_defaults(fn=cmd_lote_anadir)
    q = acciones.add_parser("estado", help="qué miembros faltan; error si la PR aún no se puede fusionar")
    q.add_argument("numero", type=int)
    q.set_defaults(fn=cmd_lote_estado)
    p = sub.add_parser("resumenes", help="resúmenes de las demás sub-issues del mismo objeto")
    p.add_argument("numero", type=int)
    p.set_defaults(fn=cmd_resumenes)
