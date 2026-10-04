"""Tablero de desarrollo de Tortunabo sobre GitHub Projects.

Una sola fuente de verdad para los tres desarrolladores y sus Claude: las issues
del repo y el proyecto «Tortunabo · Desarrollo». Este script hace de forma
determinista lo que no debe depender de que una IA se acuerde: listar lo
pendiente, coger una tarea, mover estados, reconciliar el tablero con las PR, dejar
memoria en las issues y auditar la organización del tablero.

Uso (desde la raíz del repo):
    uv run python Scripts/tablero/tablero.py pendiente
    uv run python Scripts/tablero/tablero.py coger 42
    uv run python Scripts/tablero/tablero.py soltar 42 --motivo "..."
    uv run python Scripts/tablero/tablero.py estado 42 "In review"
    uv run python Scripts/tablero/tablero.py editor 42 funciona|falla --como "PIE 4P"   # en cualquier estado
    uv run python Scripts/tablero/tablero.py nueva --titulo "..." --tipo bug --area Red --prioridad P1 --tamano S [--fase F4] --cuerpo cuerpo.md --objeto "Rally Tortuga"
    uv run python Scripts/tablero/tablero.py objeto "Rally Tortuga" --area Modos --descripcion "..."
    uv run python Scripts/tablero/tablero.py colgar 57 90
    uv run python Scripts/tablero/tablero.py sync [--aplicar]
    uv run python Scripts/tablero/tablero.py resumen 42 --que "..." [--por-que "..."] --como "..." [--pr 118]
    uv run python Scripts/tablero/tablero.py resumenes 42
    uv run python Scripts/tablero/tablero.py lote crear --titulo "..." 57 58 59 [--pr 120]
    uv run python Scripts/tablero/tablero.py lote estado 130
    uv run python Scripts/tablero/tablero.py decidir 42 --texto "..."
    uv run python Scripts/tablero/tablero.py pedir 42 --texto "..." | atendida 42 --nota "..."
    uv run python Scripts/tablero/tablero.py auditar [--aplicar]
    uv run python Scripts/tablero/tablero.py colisiones [--aplicar]
    uv run python Scripts/tablero/tablero.py bloquear 57 --por 40 [--por 41]
    uv run python Scripts/tablero/tablero.py volcado [--publicar 131]
    uv run python Scripts/tablero/tablero.py avisos [--aplicar] [--publicar 196 --parte 127]
    uv run python Scripts/tablero/tablero.py puente --comando "estado 42 Ready"

Las tareas y los fallos se agrupan por objeto (issue padre con la etiqueta `objeto`)
como sub-issues nativas de GitHub. Módulos: base.py (gh, git, proyecto, PR), flujo.py
(reglas del ciclo), objetos.py, bloqueos.py (dependencias), lotes.py, memoria.py (Resumen
y Decisión), auditoria.py, colisiones.py, estados.py (opciones de Status), control.py
(memoria, auditoría, colisiones, dependencias), control_lotes.py (lotes y resúmenes),
peticiones.py (pedir un cambio en la propia issue), volcado.py (volcado a Markdown y puente para GitHub Actions)
y avisos.py y control_avisos.py (pushes directos a dev sin revisión y avisos diarios por persona).

Requiere `gh` autenticado con el scope `project` (`gh auth refresh -s project`).
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

import auditoria
import bloqueos
import colisiones
import control
import control_avisos
import control_lotes
import flujo
import lotes
import memoria
import objetos
import peticiones
import volcado
from base import (CONFIG, ESTADOS, INTEGRACION, ORDEN_PRIORIDAD, ORDEN_TAMANO, REPO,
                  ErrorTablero, cargar_proyecto, comentar, comprobar_campos, elegir_revisor, es_de, esta_fusionada, gh, git, issues_de_pr,
                  item_de_issue, poner_campo, prs_abiertas, prs_fusionadas, slug, usuario_actual,
                  vaciar_campo)


def clave_orden(issue: dict) -> tuple:
    v = issue["valores"]
    return (ORDEN_PRIORIDAD.get(v.get("Prioridad"), 9), ORDEN_TAMANO.get(v.get("Tamaño"), 9), issue["number"])


def linea(issue: dict) -> str:
    v = issue["valores"]
    etiquetas = ",".join(n["name"] for n in issue.get("labels", {}).get("nodes", []))
    meta = " ".join(x for x in (v.get("Prioridad"), v.get("Tamaño"), v.get("Área"), v.get("Fase")) if x)
    validacion = [f"{c}: {v[c]}" for c in ("Revisión IA", "Editor") if v.get(c)]
    if validacion:
        meta += " | " + ", ".join(validacion)
    quien = ",".join(a["login"] for a in issue.get("assignees", {}).get("nodes", [])) or "libre"
    return f"  #{issue['number']} [{meta}] {issue['title']}  ({quien}{'; ' + etiquetas if etiquetas else ''})"


ETIQUETA_DECISION = flujo.ETIQUETA_DECISION


def motivo_decision(numero: int, issue: dict, forzar: bool) -> str | None:
    """Por qué no coger aún una issue con una decisión pendiente (None si no la tiene o se fuerza)."""
    if forzar or ETIQUETA_DECISION not in etiquetas_de(issue):
        return None
    return (f"#{numero} tiene una decisión pendiente (etiqueta `decision`): lee la pregunta en sus comentarios y "
            "consúltala con la persona. Si decide seguir sin esperar, repite con --forzar; si la decisión ya está "
            f"tomada, regístrala antes con `tablero.py decidir {numero} --texto \"...\"` y quita la etiqueta.")


def con_decision(issues: list[dict], login: str, aprobador: bool) -> list[dict]:
    """Issues y objetos con decisión pendiente: todas para los aprobadores; para el resto, las que tiene asignadas."""
    return [i for i in issues if ETIQUETA_DECISION in etiquetas_de(i) and (aprobador or es_de(i, login))]


def etiquetas_de(issue: dict) -> set[str]:
    return {n["name"] for n in issue.get("labels", {}).get("nodes", [])}


def urgentes_de_organizacion(issues: list[dict], login: str, aprobador: bool) -> list[dict]:
    """Lo que va antes que cualquier otra tarea: colisiones entre PR e issues con avisos de organización.

    Los avisos son `revisar-organizacion` (de `auditar`), `revisar-qa` (de la rutina diaria) y `sin-revision` (push
    directo a dev sin revisar, de `avisos`). Las colisiones son
    de todo el equipo; los avisos, del asignado (los aprobadores ven todos).
    """
    avisos = {auditoria.ETIQUETA, auditoria.ETIQUETA_QA, auditoria.ETIQUETA_SIN_REVISION}
    return [i for i in issues if colisiones.ETIQUETA in etiquetas_de(i)
            or (avisos & etiquetas_de(i) and (aprobador or es_de(i, login)))]


def objetos_con_aviso(proyecto: dict) -> list[dict]:
    """Objetos abiertos que la rutina de QA ha marcado: no se cogen, pero su aviso tiene que verse."""
    return [i for i in proyecto["items"].values()
            if i["state"] == "OPEN" and objetos.es_objeto(i) and auditoria.ETIQUETA_QA in etiquetas_de(i)]


def probables_en_editor(issues: list[dict], login: str) -> list[dict]:
    """Tareas propias en In progress o In review que aún no constan como Funciona en el editor."""
    return [i for i in issues if es_de(i, login) and i["valores"].get("Status") in ("In progress", "In review")
            and i["valores"].get("Editor") != "Funciona"]


def cmd_pendiente(_args: argparse.Namespace) -> None:
    yo = usuario_actual()
    aprobador = yo in CONFIG["aprobadores"]
    proyecto = cargar_proyecto()
    abiertas = [i for i in proyecto["items"].values()
                if i["state"] == "OPEN" and not objetos.es_objeto(i) and not lotes.es_lote(i)]
    por_estado = {e: sorted([i for i in abiertas if i["valores"].get("Status") == e], key=clave_orden) for e in ESTADOS}
    mias = [i for i in por_estado["In progress"] if es_de(i, yo)]
    prs = prs_abiertas()
    print(f"Tablero para {yo} ({CONFIG['miembros'].get(yo, {}).get('nombre', yo)}) · rama de integración: {INTEGRACION}\n")
    seccion("Primero: colisiones entre PR y organización del tablero",
            [linea(i) for i in sorted(urgentes_de_organizacion(abiertas + objetos_con_aviso(proyecto), yo, aprobador),
                                      key=clave_orden)])
    seccion("Peticiones: alguien pide un cambio en la issue (lee sus comentarios antes de seguir)",
            [linea(i) for i in sorted(peticiones.para(abiertas, yo, aprobador), key=clave_orden)])
    seccion("Tu trabajo en curso", [linea(i) for i in mias])
    seccion("Puedes probar en el editor (tus tareas en curso o en revisión; no esperes a la revisión)",
            [linea(i) for i in sorted(probables_en_editor(abiertas, yo), key=clave_orden)])
    seccion("Te toca revisar (revisión IA cruzada)",
            [linea(i) for i in por_estado["In review"] if i["valores"].get("Revisor") == yo])
    seccion("Revisiones: algo no funciona, fallo comentado en la issue", [linea(i) for i in por_estado["Revisiones"]])
    seccion("Tus PR abiertas", [f"  PR #{p['number']} {p['title']} ({p['reviewDecision'] or 'sin revisar'}, {p['mergeable']})"
                                for p in prs if p["author"]["login"] == yo])
    if aprobador:
        seccion("PR de otros por revisar", [f"  PR #{p['number']} de {p['author']['login']}: {p['title']}"
                                            for p in prs if p["author"]["login"] != yo and not p["isDraft"]])
    todas_abiertas = [i for i in proyecto["items"].values() if i["state"] == "OPEN"]
    seccion("Decisiones pendientes (etiqueta decision)" if aprobador else "Esperan una decisión de SkiTemplar o Mokius",
            [linea(i) for i in sorted(con_decision(todas_abiertas, yo, aprobador), key=clave_orden)])
    seccion("En QA editor: aprobado por la IA, falta probar en el editor", [linea(i) for i in por_estado["QA editor"]])
    seccion("Validadas: revisadas y probadas, esperan a que su PR se fusione en dev", [linea(i) for i in por_estado["Validada"]])
    no_cogibles = {ETIQUETA_DECISION, "bloqueado"}
    libres = [i for i in por_estado["Ready"] if not i["assignees"]["nodes"] and not bloqueos.abiertas(i)
              and not no_cogibles & {n["name"] for n in i["labels"]["nodes"]}]
    if not aprobador:
        libres.sort(key=lambda i: (ORDEN_TAMANO.get(i["valores"].get("Tamaño"), 9) > 1, clave_orden(i)))
    seccion("Libre para coger (Ready)", [linea(i) for i in libres[:10]])
    if not libres:
        seccion("Nada en Ready: backlog por concretar", [linea(i) for i in por_estado["Backlog"][:8]])


def seccion(titulo: str, lineas: list[str]) -> None:
    print(f"{titulo}:")
    print("\n".join(lineas) if lineas else "  (nada)")
    print()


def cmd_coger(args: argparse.Namespace) -> None:
    proyecto = cargar_proyecto(args.numero)
    issue = proyecto["items"].get(args.numero)
    if issue is None:
        raise ErrorTablero(f"La issue #{args.numero} no está en el tablero. Ejecuta `sync --aplicar` o créala con `nueva`.")
    if motivo := bloqueos.motivo_para_no_coger(args.numero, issue):
        raise ErrorTablero(motivo)
    if motivo := motivo_decision(args.numero, issue, args.forzar):
        raise ErrorTablero(motivo)
    yo = usuario_actual()
    otros = [a["login"] for a in issue["assignees"]["nodes"] if a["login"] != yo]
    if otros and not args.forzar:
        raise ErrorTablero(f"#{args.numero} ya es de {', '.join(otros)}. Habla con esa persona o usa --forzar.")
    if git("status", "--porcelain", "--untracked-files=no"):
        raise ErrorTablero("Tienes cambios sin guardar en ficheros versionados. Haz commit o stash antes de cambiar de rama.")
    es_bug = any(n["name"] in ("⚠️bug⚠️", "bug") for n in issue["labels"]["nodes"])
    rama = args.rama or f"{'fix' if es_bug else 'feat'}/{args.numero}-{slug(issue['title'])}"
    gh("issue", "edit", str(args.numero), "--repo", REPO, "--add-assignee", "@me")
    poner_campo(proyecto, args.numero, "Status", "In progress")
    git("fetch", "origin", INTEGRACION)
    if args.rama:
        # Las ramas son por lote (#282): la tarjeta se hace en la rama del lote, que se crea con la primera.
        git("fetch", "origin")
        locales = git("branch", "--list", rama).strip()
        remotas = git("branch", "-r", "--list", f"origin/{rama}").strip()
        if locales:
            git("switch", rama)
        elif remotas:
            git("switch", "-c", rama, "--track", f"origin/{rama}")
        else:
            git("switch", "-c", rama, f"origin/{INTEGRACION}")
        print(f"#{args.numero} asignada a {yo}, en In progress. Rama del lote: {rama}.")
        return
    git("switch", "-c", rama, f"origin/{INTEGRACION}")
    print(f"#{args.numero} asignada a {yo}, en In progress. Rama nueva: {rama} (desde origin/{INTEGRACION}).")


def cmd_soltar(args: argparse.Namespace) -> None:
    """Dejo de trabajar en la issue: sin asignado y de vuelta a Ready. Asignado significa «estoy con ella ahora»."""
    proyecto = cargar_proyecto(args.numero)
    if args.numero not in proyecto["items"]:
        raise ErrorTablero(f"La issue #{args.numero} no está en el tablero.")
    gh("issue", "edit", str(args.numero), "--repo", REPO, "--remove-assignee", "@me")
    poner_campo(proyecto, args.numero, "Status", "Ready")
    comentar(args.numero, f"Vuelve a Ready sin asignado: {args.motivo}")
    print(f"#{args.numero} → Ready, sin asignado")


def cmd_estado(args: argparse.Namespace) -> None:
    proyecto = cargar_proyecto(args.numero)
    issue = proyecto["items"].get(args.numero, {})
    estado = bloqueos.estado_al_aprobar(args.estado, issue)
    poner_campo(proyecto, args.numero, "Status", estado)
    if estado != args.estado:
        gh("issue", "edit", str(args.numero), "--repo", REPO, "--add-label", bloqueos.ETIQUETA)
        espera = ", ".join(f"#{n}" for n in bloqueos.abiertas(issue))
        print(f"#{args.numero} → {estado} (aprobada, pero depende de {espera}; `sync` la pasa a Ready al cerrarse)")
        return
    print(f"#{args.numero} → {estado}")


def cmd_revision(args: argparse.Namespace) -> None:
    """Manda una issue terminada a revisión cruzada: In review, revisor asignado y Revisión IA pendiente.

    Si el autor no la ha probado en el editor, va con Editor = Sin probar y lo dice («Sin QA editor»);
    si falla en el editor, no se manda.
    """
    proyecto = cargar_proyecto(args.numero)
    try:
        editor, aviso = flujo.preparar_revision(proyecto["items"].get(args.numero, {}).get("valores", {}))
    except flujo.EnvioRechazado as exc:
        raise ErrorTablero(f"#{args.numero}: {exc}") from exc
    autor = usuario_actual()
    revisor = args.revisor or elegir_revisor(proyecto, autor)
    poner_campo(proyecto, args.numero, "Status", "In review")
    poner_campo(proyecto, args.numero, "Revisor", revisor)
    poner_campo(proyecto, args.numero, "Revisión IA", "Pendiente")
    if editor:
        poner_campo(proyecto, args.numero, "Editor", editor)
    for pr in prs_abiertas():
        if args.numero in issues_de_pr(pr):
            gh("pr", "edit", str(pr["number"]), "--repo", REPO, "--add-reviewer", revisor)
    comentar(args.numero, f"Lista para revisión: @{revisor}." + (f"\n\n{aviso}" if aviso else ""))
    print(f"#{args.numero} → In review; revisa {revisor}")


def cmd_campo(args: argparse.Namespace) -> None:
    proyecto = cargar_proyecto(args.numero)
    poner_campo(proyecto, args.numero, args.campo, args.valor)
    print(f"#{args.numero} {args.campo} → {args.valor}")


def resolver_padre(args: argparse.Namespace) -> int | None:
    """Número del objeto del que colgará la issue nueva (lo crea si `--objeto` no existe)."""
    if args.padre is not None:
        return args.padre
    if not args.objeto:
        return None
    numero, creado = objetos.buscar_o_crear(gh, REPO, args.objeto)
    if creado:
        item_de_issue(cargar_proyecto(), numero)
        print(f"Objeto nuevo #{numero}: {args.objeto}")
    return numero


def cmd_nueva(args: argparse.Namespace) -> None:
    padre = resolver_padre(args)
    if padre is not None and (repetida := objetos.sub_issue_existente(gh, REPO, padre, args.titulo)):
        print(f"#{repetida} ya existe en el objeto #{padre} con ese título; no se crea otra.")
        return
    etiqueta = "⚠️bug⚠️" if args.tipo == "bug" else "tarea"
    etiquetas = [etiqueta, *args.etiqueta]
    try:
        cuerpo = Path(args.cuerpo).read_text(encoding="utf-8")
    except OSError as exc:
        raise ErrorTablero(f"No se puede leer el cuerpo «{args.cuerpo}»: {exc}") from exc
    if defectos := auditoria.problemas_de_formato(args.titulo, cuerpo, set(etiquetas)):
        raise ErrorTablero("La issue no se crea: " + "; ".join(defectos) + ".")
    # Los campos se comprueban antes de crear la issue: con un valor que no existe quedaría creada a medias.
    proyecto = cargar_proyecto()
    campos = {"Status": args.estado, "Prioridad": args.prioridad, "Tamaño": args.tamano, "Área": args.area, "Fase": args.fase,
              "Editor": "Sin probar" if args.estado == "QA editor" else None}
    comprobar_campos(proyecto, campos)
    crear = ["issue", "create", "--repo", REPO, "--title", args.titulo, "--body-file", args.cuerpo]
    for e in etiquetas:
        crear += ["--label", e]
    url = gh(*crear).strip().splitlines()[-1]
    numero = int(url.rstrip("/").rsplit("/", 1)[-1])
    if padre is not None:
        objetos.colgar(gh, REPO, numero, padre)
    for campo, valor in campos.items():
        if valor:
            poner_campo(proyecto, numero, campo, valor)
    print(f"#{numero} creada en {args.estado}{f' dentro de #{padre}' if padre else ''}: {url}")


def cmd_objeto(args: argparse.Namespace) -> None:
    """Busca el objeto abierto con ese título o lo crea; lo deja en el proyecto sin Status."""
    numero, creado = objetos.buscar_o_crear(gh, REPO, args.nombre, args.descripcion, nuevo=args.nuevo)
    proyecto = cargar_proyecto()
    item_de_issue(proyecto, numero)
    if args.area:
        poner_campo(proyecto, numero, "Área", args.area)
    print(numero)
    print(f"{'Creado' if creado else 'Ya existía'} el objeto #{numero}: {args.nombre}", file=sys.stderr)


def cmd_colgar(args: argparse.Namespace) -> None:
    if objetos.colgar(gh, REPO, args.hijo, args.padre):
        print(f"#{args.hijo} cuelga ahora de #{args.padre}")
    else:
        print(f"#{args.hijo} ya colgaba de #{args.padre}")


def cmd_sync(args: argparse.Namespace) -> None:
    proyecto = cargar_proyecto()
    cambios, avisos = [], []
    reconciliar_issues_sueltas(proyecto, cambios)
    reconciliar_bloqueos(proyecto, cambios)
    reconciliar_lotes(proyecto, cambios, avisos)
    reconciliar_prs(proyecto, cambios, avisos)
    reconciliar_estancadas(proyecto, avisos)
    avisos_validacion(proyecto, avisos)
    print(f"## Parte del tablero · {datetime.now(timezone.utc):%Y-%m-%d %H:%M} UTC\n")
    print("### Cambios de estado" + ("" if args.aplicar else " (simulación: usa --aplicar)"))
    print("\n".join(f"- {c[0]}" for c in cambios) or "- ninguno")
    print("\n### Avisos")
    print("\n".join(f"- {a}" for a in avisos) or "- ninguno")
    if args.aplicar:
        for _texto, accion in cambios:
            accion()


def reconciliar_issues_sueltas(proyecto: dict, cambios: list) -> None:
    abiertas = json.loads(gh("issue", "list", "--repo", REPO, "--state", "open", "--limit", "500",
                             "--json", "number,title,labels"))
    for issue in abiertas:
        n = issue["number"]
        if n in proyecto["items"] or issue["title"] in auditoria.TITULOS_EXCLUIDOS:
            continue
        if objetos.es_objeto(issue) or lotes.es_lote(issue):
            cambios.append((f"#{n} (objeto o lote) entra al tablero sin Status ({issue['title']})",
                            lambda n=n: item_de_issue(proyecto, n)))
        else:
            cambios.append((f"#{n} entra al tablero en Backlog ({issue['title']})",
                            lambda n=n: poner_campo(proyecto, n, "Status", "Backlog")))
    for n, issue in proyecto["items"].items():
        if objetos.es_objeto(issue) or lotes.es_lote(issue):
            if issue["valores"].get("Status"):
                cambios.append((f"#{n} es un objeto o un lote: se le quita el Status «{issue['valores']['Status']}»",
                                lambda n=n: vaciar_campo(proyecto, n, "Status")))
        elif issue["state"] == "CLOSED" and issue["valores"].get("Status") != "Done":
            cambios.append((f"#{n} cerrada → Done", lambda n=n: poner_campo(proyecto, n, "Status", "Done")))


def reconciliar_bloqueos(proyecto: dict, cambios: list) -> None:
    """Bloqueadas cuyas dependencias ya están todas cerradas: pasan a Ready y pierden `bloqueado`."""
    for n, issue in proyecto["items"].items():
        if issue["state"] == "OPEN" and bloqueos.desbloquea(issue):
            cerradas = ", ".join(f"#{b['number']}" for b in bloqueos.bloqueantes(issue))
            cambios.append((f"#{n} → Ready (ya están cerradas {cerradas})", lambda n=n: desbloquear(proyecto, n)))


def reconciliar_lotes(proyecto: dict, cambios: list, avisos: list) -> None:
    """Lotes abiertos: miembros listos → Validada; con la PR del lote en dev y todos cerrados, se cierra el lote."""
    fusionadas, abiertas = prs_fusionadas(), prs_abiertas()
    for n, issue in proyecto["items"].items():
        if issue["state"] != "OPEN" or objetos.es_objeto(issue) or lotes.es_lote(issue) or not lotes.lotes_de(issue):
            continue
        if esta_fusionada(n, fusionadas, abiertas):
            continue  # la decide reconciliar_fusiones: pasarla a Validada aquí la dejaría cerrada en Validada (#436)
        destino, _ = flujo.estado_objetivo(issue["valores"].get("Status"), issue["valores"], False, en_lote=True)
        if destino == "Validada" and issue["valores"].get("Status") != "Validada":
            cambios.append((f"#{n} → Validada (aprobada y probada; espera al resto de su lote)",
                            lambda n=n: poner_campo(proyecto, n, "Status", "Validada")))
    for n, lote in proyecto["items"].items():
        if lote["state"] != "OPEN" or not lotes.es_lote(lote):
            continue
        miembros = [b for b in (lote.get("blockedBy") or {}).get("nodes", [])]
        pr = next((p["number"] for p in fusionadas
                   if n in issues_de_pr(p, menciones=True) and p["baseRefName"] == INTEGRACION), None)
        if pr is None or any(m["state"] == "OPEN" for m in miembros):
            continue
        if not tiene_resumen(n):
            avisos.append(f"lote #{n} se cierra sin **Resumen** del conjunto: añádelo con `resumen {n}`")
        cambios.append((f"lote #{n} se cierra: PR #{pr} fusionada y todos sus miembros cerrados",
                        lambda n=n: gh("issue", "close", str(n), "--repo", REPO, "--reason", "completed")))


def desbloquear(proyecto: dict, numero: int) -> None:
    poner_campo(proyecto, numero, "Status", "Ready")
    if bloqueos.ETIQUETA in etiquetas_de(proyecto["items"][numero]):
        gh("issue", "edit", str(numero), "--repo", REPO, "--remove-label", bloqueos.ETIQUETA)


def reconciliar_prs(proyecto: dict, cambios: list, avisos: list) -> None:
    abiertas = prs_abiertas()
    conflictos = conflictos_con_base([p for p in abiertas if p["mergeable"] == "CONFLICTING"])
    for pr in abiertas:
        refs = issues_de_pr(pr)
        if pr["baseRefName"] != INTEGRACION:
            avisos.append(f"PR #{pr['number']} apunta a {pr['baseRefName']}, no a {INTEGRACION}")
        if pr["mergeable"] == "CONFLICTING":
            arreglo = ("solo localización: regenerarla" if colisiones.solo_localizacion(conflictos.get(pr["number"]) or [])
                       else "rebase del autor")
            avisos.append(f"PR #{pr['number']} ({pr['author']['login']}) tiene conflictos con {pr['baseRefName']}: {arreglo}")
        if not refs:
            avisos.append(f"PR #{pr['number']} no cierra ninguna issue (falta «Closes #n» o rama tipo/<n>-slug)")
        for n in sorted(refs):
            reconciliar_pr_issue(proyecto, pr, n, cambios, avisos)
    reconciliar_fusiones(proyecto, abiertas, cambios, avisos)


def conflictos_con_base(conflictivas: list[dict]) -> dict[int, list[str]]:
    """Ficheros en conflicto de cada PR con su rama base, con `git merge-tree` (git, sin gastar API).

    Solo las PR que GitHub ya marca CONFLICTING; si git no puede comprobarlo, la PR no aparece.
    """
    if not conflictivas or not colisiones.traer_cabezas([p["number"] for p in conflictivas],
                                                        [p["baseRefName"] for p in conflictivas]):
        return {}
    resultado = {}
    for pr in conflictivas:
        ficheros = colisiones.conflicto_con_base(pr["number"], pr["baseRefName"])
        if ficheros:
            resultado[pr["number"]] = ficheros
    return resultado


def reconciliar_pr_issue(proyecto: dict, pr: dict, n: int, cambios: list, avisos: list) -> None:
    """Issue que cierra una PR abierta: pasa a In review salvo que falle en el editor o espere una decisión."""
    issue = proyecto["items"].get(n, {})
    if issue and objetos.es_objeto(issue):
        avisos.append(f"PR #{pr['number']} enlaza el objeto #{n}: debe enlazar una de sus sub-issues")
        return
    if issue and lotes.es_lote(issue):
        return
    valores = issue.get("valores", {})
    if valores.get("Status") not in (None, "Backlog", "Ready", "In progress"):
        return
    if valores.get("Editor") == "Falla":
        avisos.append(f"#{n} tiene PR abierta (#{pr['number']}) pero falla en el editor: no pasa a In review")
    elif ETIQUETA_DECISION in etiquetas_de(issue):
        avisos.append(f"#{n} tiene PR abierta (#{pr['number']}) pero espera una decisión (`decision`): "
                      "no pasa a In review")
    else:
        cambios.append((f"#{n} → In review (PR #{pr['number']})",
                        lambda n=n, autor=pr["author"]["login"]: mover_a_review(proyecto, n, autor)))


def reconciliar_fusiones(proyecto: dict, abiertas: list[dict], cambios: list, avisos: list) -> None:
    """Issues con la PR fusionada en dev: Done si están revisadas y probadas; si no, el estado dice qué falta."""
    con_pr_abierta = {n for pr in abiertas for n in issues_de_pr(pr)}
    ya_vistas: set[int] = set()  # una issue con varias PR fusionadas se decide por la más reciente
    for pr in prs_fusionadas():
        if pr["baseRefName"] != INTEGRACION:
            continue
        for n in issues_de_pr(pr) - ya_vistas:
            ya_vistas.add(n)
            issue = proyecto["items"].get(n)
            if not issue or objetos.es_objeto(issue) or lotes.es_lote(issue) or issue["state"] != "OPEN":
                continue  # los objetos y los lotes no llevan Status: solo se mueven las issues de trabajo
            actual = issue["valores"].get("Status")
            cierra_en_done = flujo.cierra_por_fusion(actual, issue["valores"], n in con_pr_abierta)
            if not cierra_en_done and not flujo.mueve_por_fusion(actual, n in con_pr_abierta):
                continue
            valores = valores_tras_fusion(issue["valores"])
            destino, cerrar = flujo.estado_objetivo(actual, valores, fusionada=True, en_lote=False)
            if destino == actual and valores == issue["valores"] and not cerrar:
                continue
            if cerrar and not tiene_resumen(n):
                avisos.append(f"#{n} se cierra sin comentario **Resumen**: añádelo con `resumen {n}`")
            motivo = "; revisada y probada: se cierra" if cerrar else ""
            if cierra_en_done:
                motivo = "; ya estaba en Done: se cierra"
            cambios.append((f"#{n} → {destino} (PR #{pr['number']} fusionada en {INTEGRACION}{motivo})",
                            lambda n=n: aplicar_fusion(proyecto, n)))


def valores_tras_fusion(valores: dict) -> dict:
    editor = flujo.editor_tras_fusion(valores)
    return {**valores, "Editor": editor} if editor and valores.get("Editor") != editor else dict(valores)


def tiene_resumen(numero: int) -> bool:
    datos = json.loads(gh("issue", "view", str(numero), "--repo", REPO, "--json", "comments"))
    return any(memoria.es_resumen(c["body"]) for c in datos["comments"])


def mover_a_review(proyecto: dict, numero: int, autor: str) -> None:
    poner_campo(proyecto, numero, "Status", "In review")
    valores = proyecto["items"].get(numero, {}).get("valores", {})
    editor, aviso = flujo.preparar_revision(valores)
    if editor:
        poner_campo(proyecto, numero, "Editor", editor)
    if aviso:
        comentar(numero, aviso)
    if not valores.get("Revisor"):
        poner_campo(proyecto, numero, "Revisor", elegir_revisor(proyecto, autor))
    if not valores.get("Revisión IA"):
        poner_campo(proyecto, numero, "Revisión IA", "Pendiente")


def aplicar_fusion(proyecto: dict, numero: int) -> None:
    """Issue cuya PR se ha fusionado en dev: Editor sin probar si no constaba y el estado que falte."""
    previos = proyecto["items"][numero]["valores"]
    valores = valores_tras_fusion(previos)
    if valores.get("Editor") != previos.get("Editor"):
        poner_campo(proyecto, numero, "Editor", valores["Editor"])
    estado = aplicar_estado(proyecto, numero, valores, fusionada=True)
    if estado == "QA editor":
        gh("issue", "edit", str(numero), "--repo", REPO, "--add-label", "qa")


def aplicar_estado(proyecto: dict, numero: int, valores: dict, fusionada: bool, sin_pr: bool = False) -> str | None:
    """Pone el estado que marcan las validaciones, la fusión y el lote; cierra si toca. Devuelve el estado nuevo."""
    issue = proyecto["items"].get(numero, {})
    actual = issue.get("valores", {}).get("Status")
    estado, cerrar = flujo.estado_objetivo(actual, valores, fusionada, en_lote=bool(lotes.lotes_de(issue)), sin_pr=sin_pr)
    if estado and (estado != actual or cerrar):
        # Al cerrar se fija aunque la foto ya lo diga: otro cambio del mismo `sync` puede haberlo movido (#436).
        poner_campo(proyecto, numero, "Status", estado)
    if cerrar and proyecto["items"].get(numero, {}).get("state") != "CLOSED":
        motivo = "Probada en el editor" if sin_pr else f"Fusionada en `{INTEGRACION}`, revisada y probada"
        comentar(numero, f"{motivo}: Done.")
        gh("issue", "close", str(numero), "--repo", REPO, "--reason", "completed")
    return estado


def cmd_ia(args: argparse.Namespace) -> None:
    """Registra la revisión de una IA distinta de la que escribió el cambio."""
    proyecto = cargar_proyecto(args.numero)
    valor = {"aprobada": "Aprobada", "cambios": "Cambios pedidos", "pendiente": "Pendiente"}[args.veredicto]
    poner_campo(proyecto, args.numero, "Revisión IA", valor)
    if args.veredicto == "cambios":
        poner_campo(proyecto, args.numero, "Status", "Revisiones")
        if proyecto["items"].get(args.numero, {}).get("valores", {}).get("Editor") == "Funciona":
            # El arreglo cambia el código que se probó: la prueba anterior ya no lo valida.
            poner_campo(proyecto, args.numero, "Editor", "Sin probar")
    revisor = flujo.revisor_del_equipo(args.revisor, CONFIG["miembros"])
    if revisor and proyecto["items"].get(args.numero, {}).get("valores", {}).get("Revisor") != revisor:
        poner_campo(proyecto, args.numero, "Revisor", revisor)  # el campo dice quién ha revisado de verdad
    if args.nota:
        comentar(args.numero, f"**Revisión IA ({args.revisor}): {valor}.**\n\n{args.nota}")
    destino = None
    if args.veredicto == "aprobada":
        issue = proyecto["items"].get(args.numero, {})
        valores = {**issue.get("valores", {}), "Revisión IA": valor}
        destino = aplicar_estado(proyecto, args.numero, valores,
                                 esta_fusionada(args.numero, prs_fusionadas(), prs_abiertas()))
    print(f"#{args.numero} Revisión IA → {valor}{f'; estado: {destino}' if destino else ''}")


def issue_para_editor(proyecto: dict, numero: int) -> dict:
    """Issue del tablero; si no está (p. ej. cerrada y fuera del proyecto), la lee de GitHub y la añade."""
    issue = proyecto["items"].get(numero)
    if issue is not None:
        return issue
    datos = json.loads(gh("issue", "view", str(numero), "--repo", REPO, "--json", "state,labels"))
    item_de_issue(proyecto, numero)
    return {"state": datos["state"], "labels": datos["labels"], "valores": {}}


def cmd_editor(args: argparse.Namespace) -> None:
    """Registra la prueba en el editor de Unreal (PIE o Standalone) en cualquier estado de la issue.

    `funciona` fija el campo (en In progress la issue ya no pasará por QA editor) y aplica la
    regla: aprobada y en dev → Done; aprobada, miembro de un lote y sin fusionar → Validada.
    `falla` en In progress solo lo anota (se sigue arreglando ahí); en otro estado la lleva a
    Revisiones y la reabre si estaba cerrada, con `regresion` si ya funcionaba.
    """
    proyecto = cargar_proyecto(args.numero)
    issue = issue_para_editor(proyecto, args.numero)
    if objetos.es_objeto(issue):
        raise ErrorTablero(f"#{args.numero} es un objeto: registra la prueba en su sub-issue o crea una con `nueva --objeto`.")
    previo = issue["valores"].get("Editor")
    if args.resultado == "funciona":
        poner_campo(proyecto, args.numero, "Editor", "Funciona")
        comentar(args.numero, f"**Editor: funciona** ({args.como}).\n\n{args.nota or ''}".strip())
        valores = {**issue["valores"], "Editor": "Funciona"}
        fusionadas, abiertas = prs_fusionadas(), prs_abiertas()
        sin_pr = not any(args.numero in issues_de_pr(pr) for pr in fusionadas + abiertas)
        estado = aplicar_estado(proyecto, args.numero, valores, esta_fusionada(args.numero, fusionadas, abiertas), sin_pr)
        print(f"#{args.numero} Editor → Funciona{f'; estado: {estado}' if estado else ''}")
        return
    poner_campo(proyecto, args.numero, "Editor", "Falla")
    destino = flujo.estado_tras_fallo_editor(issue["valores"].get("Status"))
    if destino is None:
        comentar(args.numero, f"**Editor: falla** ({args.como}), en curso.\n\n{args.nota or 'Sin detalle.'}")
        print(f"#{args.numero} Editor → Falla; sigue en In progress hasta que funcione")
        return
    poner_campo(proyecto, args.numero, "Status", destino)
    poner_campo(proyecto, args.numero, "Revisión IA", "Pendiente")
    etiquetas = ["regresion"] if previo == "Funciona" or issue["state"] == "CLOSED" else []
    if issue["state"] == "CLOSED":
        gh("issue", "reopen", str(args.numero), "--repo", REPO)
    for e in etiquetas:
        gh("issue", "edit", str(args.numero), "--repo", REPO, "--add-label", e)
    comentar(args.numero, f"**Editor: falla** ({args.como}).\n\n{args.nota or 'Sin detalle: añade pasos para reproducirlo.'}")
    print(f"#{args.numero} Editor → Falla; pasa a Revisiones{' como regresión' if etiquetas else ''}")


def reconciliar_estancadas(proyecto: dict, avisos: list) -> None:
    limite = datetime.now(timezone.utc) - timedelta(days=CONFIG["dias_sin_movimiento"])
    for n, issue in proyecto["items"].items():
        if issue["state"] != "OPEN" or issue["valores"].get("Status") != "In progress":
            continue
        actualizada = datetime.fromisoformat(issue["updatedAt"].replace("Z", "+00:00"))
        if issue["valores"].get("Editor") == "Falla":
            avisos.append(f"#{n} falló en el editor y está en curso: prioridad antes de coger trabajo nuevo")
        quien = ",".join(a["login"] for a in issue["assignees"]["nodes"]) or "sin asignar"
        if not issue["assignees"]["nodes"]:
            avisos.append(f"#{n} está In progress sin asignar")
        elif actualizada < limite:
            avisos.append(f"#{n} ({quien}) lleva más de {CONFIG['dias_sin_movimiento']} días sin movimiento")


def avisos_validacion(proyecto: dict, avisos: list) -> None:
    abiertas = [(n, i) for n, i in proyecto["items"].items() if i["state"] == "OPEN"]
    sin_ia = [f"#{n}" for n, i in abiertas
              if i["valores"].get("Status") == "In review" and i["valores"].get("Revisión IA") != "Aprobada"]
    sin_editor = [f"#{n}" for n, i in abiertas
                  if i["valores"].get("Status") == "QA editor" and i["valores"].get("Editor") in (None, "Sin probar")]
    if sin_ia:
        avisos.append(f"En review sin revisión de una segunda IA: {', '.join(sin_ia)}")
    if sin_editor:
        avisos.append(f"En QA sin probar en el editor: {', '.join(sin_editor)}")


def anadir_comandos_de_flujo(sub: argparse._SubParsersAction) -> None:
    """Comandos que mueven una issue por el ciclo: coger, estado, revisión, validaciones."""
    sub.add_parser("pendiente", help="qué hay para mí ahora").set_defaults(fn=cmd_pendiente)
    p = sub.add_parser("coger", help="asignarme una issue y crear su rama (o entrar en la de su lote con --rama)")
    p.add_argument("numero", type=int)
    p.add_argument("--forzar", action="store_true")
    p.add_argument("--rama", help="rama del lote en la que se hace esta tarjeta (se crea desde dev si no existe)")
    p.set_defaults(fn=cmd_coger)
    p = sub.add_parser("soltar", help="dejar una issue que tenía en curso: sin asignado y de vuelta a Ready")
    p.add_argument("numero", type=int)
    p.add_argument("--motivo", required=True, help="por qué la dejo y en qué punto queda")
    p.set_defaults(fn=cmd_soltar)
    p = sub.add_parser("estado", help="mover una issue de columna")
    p.add_argument("numero", type=int)
    p.add_argument("estado", choices=ESTADOS)
    p.set_defaults(fn=cmd_estado)
    p = sub.add_parser("revision", help="mandar una issue terminada a revisión cruzada")
    p.add_argument("numero", type=int)
    p.add_argument("--revisor", choices=list(CONFIG["miembros"]))
    p.set_defaults(fn=cmd_revision)
    p = sub.add_parser("campo", help="fijar Prioridad, Tamaño, Área, Fase, Editor… de una issue")
    p.add_argument("numero", type=int)
    p.add_argument("campo")
    p.add_argument("valor")
    p.set_defaults(fn=cmd_campo)
    p = sub.add_parser("ia", help="registrar la revisión de una segunda IA")
    p.add_argument("numero", type=int)
    p.add_argument("veredicto", choices=["aprobada", "cambios", "pendiente"])
    p.add_argument("--revisor", default="IA revisora", help="p. ej. «Codex», «code-reviewer»")
    p.add_argument("--nota")
    p.set_defaults(fn=cmd_ia)
    p = sub.add_parser("editor", help="registrar la prueba en el editor de Unreal")
    p.add_argument("numero", type=int)
    p.add_argument("resultado", choices=["funciona", "falla"])
    p.add_argument("--como", default="PIE", help="PIE 1P, PIE 4P, Standalone… o «validación implícita»")
    p.add_argument("--nota")
    p.set_defaults(fn=cmd_editor)


def cmd_volcado(args: argparse.Namespace) -> None:
    """Imprime el tablero en Markdown o, con --publicar, lo deja como cuerpo de esa issue."""
    proyecto = cargar_proyecto()
    prs_por_issue: dict[int, list[int]] = {}
    for pr in prs_abiertas():
        for numero in issues_de_pr(pr):
            prs_por_issue.setdefault(numero, []).append(pr["number"])
    texto = volcado.render(proyecto["items"], prs_por_issue, datetime.now(timezone.utc), CONFIG["dias_sin_movimiento"])
    if args.publicar is None:
        print(texto)
        return
    gh("issue", "edit", str(args.publicar), "--repo", REPO, "--body-file", "-", entrada=texto)
    print(f"Volcado publicado en #{args.publicar} ({len(texto)} caracteres)")


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


def anadir_comandos_de_alta(sub: argparse._SubParsersAction) -> None:
    """Comandos que crean u organizan issues: nueva, objeto, colgar y sync."""
    p = sub.add_parser("nueva", help="crear issue y colocarla en el tablero")
    p.add_argument("--titulo", required=True)
    p.add_argument("--tipo", choices=["bug", "tarea"], required=True)
    p.add_argument("--cuerpo", required=True, help="fichero markdown con el cuerpo")
    p.add_argument("--estado", default="Backlog", choices=ESTADOS)
    p.add_argument("--prioridad", choices=list(ORDEN_PRIORIDAD), required=True)
    p.add_argument("--tamano", choices=list(ORDEN_TAMANO), required=True)
    p.add_argument("--area", required=True)
    p.add_argument("--fase", default="Sin fase", help="F0…F8 del plan maestro; por defecto, «Sin fase»")
    p.add_argument("--etiqueta", action="append", default=[])
    padre = p.add_mutually_exclusive_group()
    padre.add_argument("--objeto", help="título del objeto del que cuelga (se crea si no existe)")
    padre.add_argument("--padre", type=int, help="número de la issue padre")
    p.set_defaults(fn=cmd_nueva)
    p = sub.add_parser("objeto", help="buscar o crear un objeto (issue padre) e imprimir su número")
    p.add_argument("nombre")
    p.add_argument("--area")
    p.add_argument("--descripcion")
    p.add_argument("--nuevo", action="store_true", help="crearlo aunque haya objetos con nombre parecido")
    p.set_defaults(fn=cmd_objeto)
    p = sub.add_parser("colgar", help="colgar una issue existente como sub-issue de otra")
    p.add_argument("hijo", type=int)
    p.add_argument("padre", type=int)
    p.set_defaults(fn=cmd_colgar)
    p = sub.add_parser("volcado", help="tablero completo en Markdown, para quien no puede leer el Project")
    p.add_argument("--publicar", type=int, metavar="ISSUE", help="sustituir el cuerpo de esa issue por el volcado")
    p.set_defaults(fn=cmd_volcado)
    sub.add_parser("silenciar", help="darme de baja de las notificaciones de las issues abiertas (no de las PR)"
                   ).set_defaults(fn=cmd_silenciar)
    p = sub.add_parser("puente", help="ejecutar un comando recibido por el workflow (lista cerrada)")
    p.add_argument("--comando", required=True, help='por ejemplo: estado 42 Ready')
    p.set_defaults(fn=None)
    p = sub.add_parser("sync", help="reconciliar tablero, PR e issues")
    p.add_argument("--aplicar", action="store_true")
    p.set_defaults(fn=cmd_sync)


def main() -> int:
    for flujo_salida in (sys.stdout, sys.stderr):  # la consola de Windows (cp1252) no imprime «→» (#437)
        flujo_salida.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="Tablero de desarrollo de Tortunabo")
    sub = parser.add_subparsers(dest="cmd", required=True)
    anadir_comandos_de_flujo(sub)
    anadir_comandos_de_alta(sub)
    control.anadir_comandos(sub)
    control_lotes.anadir_comandos(sub)
    control_avisos.anadir_comandos(sub)
    peticiones.anadir_comandos(sub)
    args = parser.parse_args()
    try:
        if args.cmd == "puente":
            args = parser.parse_args(volcado.argumentos_de_puente(args.comando))
        args.fn(args)
    except (ErrorTablero, objetos.ErrorObjeto, memoria.ErrorMemoria) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
