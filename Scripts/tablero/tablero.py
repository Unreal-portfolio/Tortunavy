"""Tablero de desarrollo de Tortunabo sobre GitHub Projects.

Una sola fuente de verdad para los tres desarrolladores y sus Claude: las issues
del repo y el proyecto «Tortunabo · Desarrollo». Este script hace de forma
determinista lo que no debe depender de que una IA se acuerde: listar lo
pendiente, coger una tarea, mover estados, reconciliar el tablero con las PR, dejar
memoria en las issues y auditar la organización del tablero.

Uso (desde la raíz del repo):
    uv run python Scripts/tablero/tablero.py pendiente
    uv run python Scripts/tablero/tablero.py coger 42 [--rama <rama del lote>] [--nocturna] [--forzar]   # rama desde su base
    uv run python Scripts/tablero/tablero.py soltar 42 --motivo "..."
    uv run python Scripts/tablero/tablero.py estado 42 "In review"
    uv run python Scripts/tablero/tablero.py editor 42 funciona|falla --como "PIE 4P"   # en cualquier estado
    uv run python Scripts/tablero/tablero.py nueva --titulo "..." --tipo bug --area Red --prioridad P1 --tamano S [--fase F4] --cuerpo cuerpo.md --objeto "Rally Tortuga"
    uv run python Scripts/tablero/tablero.py objeto "Rally Tortuga" --area Modos --descripcion "..."
    uv run python Scripts/tablero/tablero.py colgar 57 90
    uv run python Scripts/tablero/tablero.py sync [--aplicar]
    uv run python Scripts/tablero/tablero.py resumen 42 --que "..." [--por-que "..."] --como "..." [--pr 118]
    uv run python Scripts/tablero/tablero.py resumenes 42
    uv run python Scripts/tablero/tablero.py lote crear --titulo "..." 57 58 59 [--pr 120] [--excepcion "..." [--autoriza Mokius]]
    uv run python Scripts/tablero/tablero.py lote añadir 130 60 [--excepcion "..." [--autoriza Mokius]]
    uv run python Scripts/tablero/tablero.py lote estado 130
    uv run python Scripts/tablero/tablero.py decidir 42 --texto "..."
    uv run python Scripts/tablero/tablero.py pedir 42 --texto "..." | atendida 42 --nota "..."
    uv run python Scripts/tablero/tablero.py auditar [--aplicar]
    uv run python Scripts/tablero/tablero.py colisiones [--aplicar]
    uv run python Scripts/tablero/tablero.py bloquear 57 --por 40 [--por 41]
    uv run python Scripts/tablero/tablero.py volcado [--publicar 131]
    uv run python Scripts/tablero/tablero.py avisos [--aplicar] [--publicar 196 --parte 127]
    uv run python Scripts/tablero/tablero.py chamber 57 58 --motivo "..."   # descartar: etiqueta, comentario y cierre
    uv run python Scripts/tablero/tablero.py organizacion propagar [--aplicar]   # organización de dev a main y dev-<modo>
    uv run python Scripts/tablero/tablero.py puente --comando "estado 42 Ready"

Líneas de trabajo: sin etiqueta `modo:*`, la issue es de la línea principal y su rama base es `dev` (rama de trabajo
`feat|fix/<n>-<slug>`); con `modo:tct`, `modo:carrera`, `modo:rally` o `modo:vr`, su rama base es `dev-<modo>` (rama
de trabajo `dev-<modo>-<n>-<slug>`). `coger` crea la rama desde `origin/<base>` y rechaza una línea sin rama en origin
(hoy `dev-vr`) salvo con `--forzar`, que la saca de `chamber`. «Fusionada» es fusionada en su rama base para `sync`,
`ia`, `editor`, `auditar`, `lote estado` y `avisos`; `colisiones` solo compara PR con la misma base; `pendiente`
enseña primero la línea principal y después una sección por modo. `avisos` vigila los pushes directos solo en `dev`.

Sesión nocturna desatendida (Claude solo toda la noche, no una franja horaria): con `--nocturna` o TN_SESION_NOCTURNA=1,
`coger` solo acepta `⚠️bug⚠️`, `pulido` y `refactor`; `--forzar` coge cualquier otra y lo comenta en la issue.

Lotes: como mucho 3 issues por lote y 1 lote abierto por persona (definición en lotes.py). `lote crear` y `lote añadir`
lo exigen; `--excepcion "<motivo>"`, con `--autoriza <aprobador>` si quien lo lanza no es aprobador, lo salta y deja
la etiqueta `excepcion` y un comentario en el lote. Los lotes de solo issues `refactor` no tienen topes. `auditar`
marca con `revisar-organizacion` el lote que los incumple sin `excepcion`.

Refactorización (`refactor`): sin revisión cruzada ni QA editor; `revision` no la manda a revisión, `sync` la cierra
en Done al fusionarse su PR en su rama base y ni `sync`, ni `auditar`, ni `avisos` la tratan como incidencia.

Organización: las rutas de `rutas_organizacion_propagar` (equipo.json) son iguales en `dev`, `main` y cada
`dev-<modo>`. `organizacion propagar` dice qué ramas difieren y, con `--aplicar`, abre una PR por rama desde
`org/propagar-<AAAAMMDD>-<destino>` sin tocar el árbol de trabajo; `sync` avisa de las que se han quedado atrás.

Las issues descartadas (etiqueta `chamber`, cerradas como not planned y en Backlog) quedan fuera de todo: `sync`,
`auditar`, `colisiones`, `volcado`, `pendiente`, `avisos` y `conversacion` no las procesan, y `coger`, `revision`,
`ia`, `editor`, `pedir` y `estado` las rechazan, también con `--forzar`. Solo `coger`/`estado` con `--retomar`,
lanzado en local por un aprobador con una **Decisión** suya posterior al descarte, quita la etiqueta y las reabre;
el puente no acepta `--retomar`. Una PR que enlaza una descartada sigue a la vista: `sync` avisa y `avisos` la da por
incidencia si se fusiona con código.

Las tareas y los fallos se agrupan por objeto (issue padre con la etiqueta `objeto`)
como sub-issues nativas de GitHub. Módulos: base.py (gh, git, proyecto, PR), flujo.py
(reglas del ciclo), objetos.py, bloqueos.py (dependencias), lotes.py, memoria.py (Resumen
y Decisión), auditoria.py, colisiones.py, estados.py (opciones de Status), control.py
(memoria, auditoría, colisiones, dependencias), control_lotes.py (lotes y resúmenes),
peticiones.py (pedir un cambio en la propia issue), volcado.py (volcado a Markdown y puente para GitHub Actions),
avisos.py y control_avisos.py (pushes directos a dev sin revisión y avisos diarios por persona), pendiente.py
(presentación de `pendiente` por líneas) y organizacion.py (organización igual en todas las ramas).

Requiere `gh` autenticado con el scope `project` (`gh auth refresh -s project`).
"""

from __future__ import annotations

import argparse
import json
import os
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
import organizacion
import peticiones
import volcado
from base import (CONFIG, ESTADOS, INTEGRACION, ORDEN_PRIORIDAD, ORDEN_TAMANO, REPO, ErrorTablero,
                  cargar_campos, cargar_issue, cargar_proyecto, cierres_de_pr, comentar, comprobar_campos,
                  elegir_revisor, esta_fusionada, existe_rama_remota, gh, git, issues_de_pr, item_de_issue,
                  poner_campo, prs_abiertas, prs_fusionadas, rama_base, rechazar_descartada, retomar_descartada,
                  slug, usuario_actual, vaciar_campo)
from flujo import RAMA_ARCHIVO
# clave_orden, con_decision y urgentes_de_organizacion se reexportan: los tests y las skills las usan desde aquí.
from pendiente import (ETIQUETA_DECISION, clave_orden, con_decision, etiquetas_de, linea_de_pr,  # noqa: F401
                       motivo_decision, pendiente_de_linea, por_linea, urgentes_de_organizacion)


def cmd_pendiente(_args: argparse.Namespace) -> None:
    """Lo pendiente por líneas: primero la principal (dev), después una sección por cada modo con algo abierto."""
    yo = usuario_actual()
    aprobador = yo in CONFIG["aprobadores"]
    completo = cargar_proyecto()
    vivas = flujo.sin_chamber(completo["items"])  # las descartadas no son de nadie
    chamber = flujo.descartadas(completo["items"])
    prs = prs_abiertas()
    print(f"Tablero para {yo} ({CONFIG['miembros'].get(yo, {}).get('nombre', yo)}) · rama de integración: {INTEGRACION}"
          f" · líneas de modo: {', '.join(flujo.rama_de_modo(m, INTEGRACION) for m in flujo.MODOS)}\n")
    for rama, items in por_linea(vivas):
        if rama != INTEGRACION:
            print(f"## Línea {rama} (`{flujo.PREFIJO_MODO}{rama.removeprefix(INTEGRACION + '-')}`)\n")
        pendiente_de_linea(rama, {**completo, "items": items}, [p for p in prs if linea_de_pr(p) == rama],
                           yo, aprobador, chamber)


def cmd_coger(args: argparse.Namespace) -> None:
    proyecto = cargar_issue(args.numero)
    issue = proyecto["items"].get(args.numero)
    if issue is None:
        raise ErrorTablero(f"La issue #{args.numero} no está en el tablero. Ejecuta `sync --aplicar` o créala con `nueva`.")
    rechazar_descartada(args.numero, issue, args.retomar)
    if motivo := bloqueos.motivo_para_no_coger(args.numero, issue):
        raise ErrorTablero(motivo)
    if motivo := motivo_decision(args.numero, issue, args.forzar):
        raise ErrorTablero(motivo)
    nocturna = flujo.motivo_nocturno(args.numero, issue,
                                     flujo.es_sesion_nocturna(getattr(args, "nocturna", False), os.environ))
    if nocturna and not args.forzar:
        raise ErrorTablero(nocturna)
    yo = usuario_actual()
    otros = [a["login"] for a in issue["assignees"]["nodes"] if a["login"] != yo]
    if otros and not args.forzar:
        raise ErrorTablero(f"#{args.numero} ya es de {', '.join(otros)}. Habla con esa persona o usa --forzar.")
    base = rama_base_para_coger(args.numero, issue, args.forzar)
    if git("status", "--porcelain", "--untracked-files=no"):
        raise ErrorTablero("Tienes cambios sin guardar en ficheros versionados. Haz commit o stash antes de cambiar de rama.")
    rama = args.rama or flujo.rama_de_trabajo(args.numero, slug(issue["title"]), issue, INTEGRACION)
    retomar_descartada(args.numero, issue)  # solo llega aquí descartada con un --retomar aceptado
    gh("issue", "edit", str(args.numero), "--repo", REPO, "--add-assignee", "@me")
    poner_campo(proyecto, args.numero, "Status", "In progress")
    if nocturna:
        comentar(args.numero, flujo.texto_nocturno())
    git("fetch", "origin", base)
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
            git("switch", "-c", rama, f"origin/{base}")
        print(f"#{args.numero} asignada a {yo}, en In progress. Rama del lote: {rama}.")
        return
    git("switch", "-c", rama, f"origin/{base}")
    print(f"#{args.numero} asignada a {yo}, en In progress. Rama nueva: {rama} (desde origin/{base}; "
          f"su PR va a {rama_base(issue)}).")


def rama_base_para_coger(numero: int, issue: dict, forzar: bool) -> str:
    """Rama de la que sale el trabajo: la base de la issue. Una línea de modo sin rama remota (hoy `dev-vr`) no se coge;
    con --forzar el trabajo sale de `chamber`, de donde nacen las líneas de modo, y su PR espera a que exista la base.
    """
    base = rama_base(issue)
    if base == INTEGRACION or existe_rama_remota(base):
        return base
    if not forzar:
        etiqueta = f"{flujo.PREFIJO_MODO}{flujo.modo_de(issue)}"
        raise ErrorTablero(f"#{numero} es de la línea {base} (`{etiqueta}`), que aún no "
                           f"tiene rama en origin: se coge cuando un aprobador la cree desde `{RAMA_ARCHIVO}`.")
    print(f"Aviso: {base} no existe en origin; la rama sale de origin/{RAMA_ARCHIVO} y su PR espera a que exista "
          f"{base}.")
    return RAMA_ARCHIVO


def cmd_soltar(args: argparse.Namespace) -> None:
    """Dejo de trabajar en la issue: sin asignado y de vuelta a Ready. Asignado significa «estoy con ella ahora»."""
    proyecto = cargar_issue(args.numero)
    if args.numero not in proyecto["items"]:
        raise ErrorTablero(f"La issue #{args.numero} no está en el tablero.")
    gh("issue", "edit", str(args.numero), "--repo", REPO, "--remove-assignee", "@me")
    poner_campo(proyecto, args.numero, "Status", "Ready")
    comentar(args.numero, f"Vuelve a Ready sin asignado: {args.motivo}")
    print(f"#{args.numero} → Ready, sin asignado")


def cmd_estado(args: argparse.Namespace) -> None:
    proyecto = cargar_issue(args.numero)
    issue = proyecto["items"].get(args.numero, {})
    if args.estado != "Backlog":  # una descartada se queda aparcada en Backlog
        rechazar_descartada(args.numero, issue, args.retomar)
        retomar_descartada(args.numero, issue)
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
    proyecto = cargar_issue(args.numero)
    rechazar_descartada(args.numero, proyecto["items"].get(args.numero, {}))
    if flujo.es_refactor(proyecto["items"].get(args.numero, {})):
        base = rama_base(proyecto["items"][args.numero])
        print(f"#{args.numero} es `{flujo.ETIQUETA_REFACTOR}`: no pasa por revisión cruzada. Fusiona su PR en {base} "
              "cuando compile en DebugGame y pasen los tests; `sync --aplicar` la lleva a Done (con su **Resumen**).")
        return
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
    proyecto = cargar_issue(args.numero)
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
        item_de_issue(cargar_issue(numero), numero)
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
    proyecto = cargar_campos()  # la issue aún no existe: solo hacen falta los ids de los campos
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
    proyecto = cargar_issue(numero)
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
    avisos_organizacion(avisos)
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
        if n in proyecto["items"] or issue["title"] in auditoria.TITULOS_EXCLUIDOS or flujo.es_chamber(issue):
            continue
        if objetos.es_objeto(issue) or lotes.es_lote(issue):
            cambios.append((f"#{n} (objeto o lote) entra al tablero sin Status ({issue['title']})",
                            lambda n=n: item_de_issue(proyecto, n)))
        else:
            cambios.append((f"#{n} entra al tablero en Backlog ({issue['title']})",
                            lambda n=n: poner_campo(proyecto, n, "Status", "Backlog")))
    for n, issue in flujo.sin_chamber(proyecto["items"]).items():
        if objetos.es_objeto(issue) or lotes.es_lote(issue):
            if issue["valores"].get("Status"):
                cambios.append((f"#{n} es un objeto o un lote: se le quita el Status «{issue['valores']['Status']}»",
                                lambda n=n: vaciar_campo(proyecto, n, "Status")))
        elif issue["state"] == "CLOSED" and issue["valores"].get("Status") != "Done":
            cambios.append((f"#{n} cerrada → Done", lambda n=n: poner_campo(proyecto, n, "Status", "Done")))


def reconciliar_bloqueos(proyecto: dict, cambios: list) -> None:
    """Bloqueadas cuyas dependencias ya están todas cerradas: pasan a Ready y pierden `bloqueado`."""
    for n, issue in flujo.sin_chamber(proyecto["items"]).items():
        if issue["state"] == "OPEN" and bloqueos.desbloquea(issue):
            cerradas = ", ".join(f"#{b['number']}" for b in bloqueos.bloqueantes(issue))
            cambios.append((f"#{n} → Ready (ya están cerradas {cerradas})", lambda n=n: desbloquear(proyecto, n)))


def reconciliar_lotes(proyecto: dict, cambios: list, avisos: list) -> None:
    """Lotes abiertos: miembros listos → Validada; con la PR del lote en dev y todos cerrados, se cierra el lote."""
    fusionadas, abiertas = prs_fusionadas(), prs_abiertas()
    vivas = flujo.sin_chamber(proyecto["items"])
    for n, issue in vivas.items():
        if issue["state"] != "OPEN" or objetos.es_objeto(issue) or lotes.es_lote(issue) or not lotes.lotes_de(issue):
            continue
        if esta_fusionada(n, fusionadas, abiertas, rama_base(issue)):
            continue  # la decide reconciliar_fusiones: pasarla a Validada aquí la dejaría cerrada en Validada (#436)
        destino, _ = flujo.estado_objetivo(issue["valores"].get("Status"), issue["valores"], False, en_lote=True)
        if destino == "Validada" and issue["valores"].get("Status") != "Validada":
            cambios.append((f"#{n} → Validada (aprobada y probada; espera al resto de su lote)",
                            lambda n=n: poner_campo(proyecto, n, "Status", "Validada")))
    for n, lote in vivas.items():
        if lote["state"] != "OPEN" or not lotes.es_lote(lote):
            continue
        miembros = [b for b in (lote.get("blockedBy") or {}).get("nodes", [])]
        pr = next((p["number"] for p in fusionadas
                   if n in issues_de_pr(p, menciones=True) and flujo.es_rama_de_linea(p["baseRefName"], INTEGRACION)),
                  None)
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
        if not flujo.es_rama_de_linea(pr["baseRefName"], INTEGRACION) and not organizacion.es_pr_hacia_estable(pr):
            avisos.append(f"PR #{pr['number']} apunta a {pr['baseRefName']}, que no es una rama de línea "
                          f"({INTEGRACION} o {INTEGRACION}-<modo>)")
        if pr["mergeable"] == "CONFLICTING":
            arreglo = ("solo localización: regenerarla" if colisiones.solo_localizacion(conflictos.get(pr["number"]) or [])
                       else "rebase del autor")
            avisos.append(f"PR #{pr['number']} ({pr['author']['login']}) tiene conflictos con {pr['baseRefName']}: {arreglo}")
        if not refs and not organizacion.es_pr_de_organizacion(pr) and not organizacion.es_pr_hacia_estable(pr):
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
    if flujo.es_chamber(issue):  # no se mueve, pero la PR sigue a la vista: no puede entrar en dev sin que se vea
        avisos.append(f"PR #{pr['number']} enlaza la issue descartada #{n}: quita su «Closes» o cierra la PR")
        return
    if issue and objetos.es_objeto(issue):
        avisos.append(f"PR #{pr['number']} enlaza el objeto #{n}: debe enlazar una de sus sub-issues")
        return
    if issue and lotes.es_lote(issue):
        return
    base = rama_base(issue)
    if issue and flujo.es_rama_de_linea(pr["baseRefName"], INTEGRACION) and pr["baseRefName"] != base:
        avisos.append(f"PR #{pr['number']} va a {pr['baseRefName']} y #{n} es de {base}: corrige la base de la PR "
                      f"o la etiqueta `{flujo.PREFIJO_MODO}*` (fusionada ahí no cuenta)")
    if flujo.es_refactor(issue):
        return  # una refactorización no pasa por In review: se fusiona directa
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
    for pr in sorted(prs_fusionadas(), key=lambda p: p.get("mergedAt") or "", reverse=True):
        if not flujo.es_rama_de_linea(pr["baseRefName"], INTEGRACION):
            continue
        for n in issues_de_pr(pr) - ya_vistas:
            issue = proyecto["items"].get(n)
            if issue and pr["baseRefName"] != rama_base(issue):
                continue  # fusionada en otra línea: para esta issue no cuenta
            ya_vistas.add(n)
            if not issue or objetos.es_objeto(issue) or lotes.es_lote(issue) or issue["state"] != "OPEN" \
                    or flujo.es_chamber(issue):
                continue  # los objetos y los lotes no llevan Status, y las descartadas no se mueven
            actual, refactor = issue["valores"].get("Status"), flujo.es_refactor(issue)
            cierra_en_done = flujo.cierra_por_fusion(actual, issue["valores"], n in con_pr_abierta, refactor)
            # Es la PR fusionada más reciente que la enlaza (ya_vistas); para una refactorización, además, la cierra.
            vigente = n in cierres_de_pr(pr)
            if not cierra_en_done and not flujo.mueve_por_fusion(actual, n in con_pr_abierta, refactor, vigente):
                continue
            valores = valores_tras_fusion(issue["valores"], refactor)
            destino, cerrar = flujo.estado_objetivo(actual, valores, fusionada=True, en_lote=False, refactor=refactor)
            if destino == actual and valores == issue["valores"] and not cerrar:
                continue
            if cerrar and not tiene_resumen(n):
                avisos.append(f"#{n} se cierra sin comentario **Resumen**: añádelo con `resumen {n}`")
            motivo = ("; refactor: se cierra sin revisión ni prueba" if refactor
                      else "; revisada y probada: se cierra") if cerrar else ""
            if cierra_en_done:
                motivo = "; ya estaba en Done: se cierra"
            cambios.append((f"#{n} → {destino} (PR #{pr['number']} fusionada en {pr['baseRefName']}{motivo})",
                            lambda n=n: aplicar_fusion(proyecto, n)))


def valores_tras_fusion(valores: dict, refactor: bool = False) -> dict:
    """Validaciones tras la fusión: Editor sin probar si no constaba. Una refactorización no se prueba: no cambia."""
    editor = None if refactor else flujo.editor_tras_fusion(valores)
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
    valores = valores_tras_fusion(previos, flujo.es_refactor(proyecto["items"][numero]))
    if valores.get("Editor") != previos.get("Editor"):
        poner_campo(proyecto, numero, "Editor", valores["Editor"])
    estado = aplicar_estado(proyecto, numero, valores, fusionada=True)
    if estado == "QA editor":
        gh("issue", "edit", str(numero), "--repo", REPO, "--add-label", "qa")


def aplicar_estado(proyecto: dict, numero: int, valores: dict, fusionada: bool, sin_pr: bool = False) -> str | None:
    """Pone el estado que marcan las validaciones, la fusión y el lote; cierra si toca. Devuelve el estado nuevo."""
    issue = proyecto["items"].get(numero, {})
    actual, refactor = issue.get("valores", {}).get("Status"), flujo.es_refactor(issue)
    estado, cerrar = flujo.estado_objetivo(actual, valores, fusionada, en_lote=bool(lotes.lotes_de(issue)), sin_pr=sin_pr,
                                           refactor=refactor)
    if estado and (estado != actual or cerrar):
        # Al cerrar se fija aunque la foto ya lo diga: otro cambio del mismo `sync` puede haberlo movido (#436).
        poner_campo(proyecto, numero, "Status", estado)
    if cerrar and proyecto["items"].get(numero, {}).get("state") != "CLOSED":
        detalle = "refactor, sin revisión ni prueba" if refactor else "revisada y probada"
        motivo = "Probada en el editor" if sin_pr else f"Fusionada en `{rama_base(issue)}`, {detalle}"
        comentar(numero, f"{motivo}: Done.")
        gh("issue", "close", str(numero), "--repo", REPO, "--reason", "completed")
    return estado


def cmd_ia(args: argparse.Namespace) -> None:
    """Registra la revisión de una IA distinta de la que escribió el cambio."""
    proyecto = cargar_issue(args.numero)
    rechazar_descartada(args.numero, proyecto["items"].get(args.numero, {}))
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
                                 esta_fusionada(args.numero, prs_fusionadas(), prs_abiertas(), rama_base(issue)))
    print(f"#{args.numero} Revisión IA → {valor}{f'; estado: {destino}' if destino else ''}")


def issue_para_editor(proyecto: dict, numero: int) -> dict:
    """Issue del tablero; si no está (p. ej. cerrada y fuera del proyecto), la lee de GitHub y la añade.

    Una descartada (`chamber`) se rechaza antes de tocar nada: reabierta en Revisiones, ninguna rutina la vería.
    """
    issue = proyecto["items"].get(numero)
    if issue is not None:
        rechazar_descartada(numero, issue)
        return issue
    datos = json.loads(gh("issue", "view", str(numero), "--repo", REPO, "--json", "state,labels"))
    issue = {"state": datos["state"], "labels": datos["labels"], "valores": {}}
    rechazar_descartada(numero, issue)
    item_de_issue(proyecto, numero)
    return issue


def cmd_editor(args: argparse.Namespace) -> None:
    """Registra la prueba en el editor de Unreal (PIE o Standalone) en cualquier estado de la issue.

    `funciona` fija el campo (en In progress la issue ya no pasará por QA editor) y aplica la
    regla: aprobada y en dev → Done; aprobada, miembro de un lote y sin fusionar → Validada.
    `falla` en In progress solo lo anota (se sigue arreglando ahí); en otro estado la lleva a
    Revisiones y la reabre si estaba cerrada, con `regresion` si ya funcionaba.
    """
    proyecto = cargar_issue(args.numero)
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
        fusionada = esta_fusionada(args.numero, fusionadas, abiertas, rama_base(issue))
        estado = aplicar_estado(proyecto, args.numero, valores, fusionada, sin_pr)
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
    for n, issue in flujo.sin_chamber(proyecto["items"]).items():
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
    # Una refactorización no lleva revisión IA ni prueba en el editor: no es un aviso.
    abiertas = [(n, i) for n, i in flujo.sin_chamber(proyecto["items"]).items()
                if i["state"] == "OPEN" and not flujo.es_refactor(i)]
    sin_ia = [f"#{n}" for n, i in abiertas
              if i["valores"].get("Status") == "In review" and i["valores"].get("Revisión IA") != "Aprobada"]
    sin_editor = [f"#{n}" for n, i in abiertas
                  if i["valores"].get("Status") == "QA editor" and i["valores"].get("Editor") in (None, "Sin probar")]
    if sin_ia:
        avisos.append(f"En review sin revisión de una segunda IA: {', '.join(sin_ia)}")
    if sin_editor:
        avisos.append(f"En QA sin probar en el editor: {', '.join(sin_editor)}")


def avisos_organizacion(avisos: list) -> None:
    """Una línea por rama destino (main y cada dev-<modo>) cuya organización difiere de dev. Solo git, sin API."""
    try:
        desfasadas = organizacion.ramas_desfasadas()
    except ErrorTablero as exc:
        avisos.append(f"No se pudo comparar la organización de las ramas con {INTEGRACION}: {exc}")
        return
    if desfasadas:
        avisos.append(f"Organización distinta de {INTEGRACION} en {', '.join(desfasadas)}: "
                      "`organizacion propagar --aplicar`")


AYUDA_RETOMAR = ("solo aprobadores, en local y con una Decisión posterior al descarte: devuelve al ciclo una issue "
                 "descartada (`chamber`)")


def anadir_comandos_de_flujo(sub: argparse._SubParsersAction) -> None:
    """Comandos que mueven una issue por el ciclo: coger, estado, revisión, validaciones."""
    sub.add_parser("pendiente", help="qué hay para mí ahora").set_defaults(fn=cmd_pendiente)
    p = sub.add_parser("coger", help="asignarme una issue y crear su rama desde su base, dev o dev-<modo> (o entrar "
                                     "en la de su lote con --rama)")
    p.add_argument("numero", type=int)
    p.add_argument("--forzar", action="store_true",
                   help="coger aunque sea de otro, espere una decisión, no sea bug, pulido ni refactor en una sesión "
                        "nocturna (queda comentado) o su línea aún no tenga rama")
    p.add_argument("--nocturna", action="store_true",
                   help=f"sesión nocturna desatendida (también {flujo.VARIABLE_NOCTURNA}=1): solo bugs, pulido y "
                        "refactor")
    p.add_argument("--retomar", action="store_true", help=AYUDA_RETOMAR)
    p.add_argument("--rama", help="rama del lote en la que se hace esta tarjeta (se crea desde su base si no existe)")
    p.set_defaults(fn=cmd_coger)
    p = sub.add_parser("soltar", help="dejar una issue que tenía en curso: sin asignado y de vuelta a Ready")
    p.add_argument("numero", type=int)
    p.add_argument("--motivo", required=True, help="por qué la dejo y en qué punto queda")
    p.set_defaults(fn=cmd_soltar)
    p = sub.add_parser("estado", help="mover una issue de columna")
    p.add_argument("numero", type=int)
    p.add_argument("estado", choices=ESTADOS)
    p.add_argument("--retomar", action="store_true", help=AYUDA_RETOMAR)
    p.set_defaults(fn=cmd_estado)
    p = sub.add_parser("revision", help="mandar una issue terminada a revisión cruzada (una `refactor` no va: se "
                                        "fusiona directa)")
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
    organizacion.anadir_comandos(sub)
    args = parser.parse_args()

    try:
        if args.cmd == "puente":
            args = parser.parse_args(volcado.argumentos_de_puente(args.comando, flujo.actor_de_puente(os.environ),
                                                                  CONFIG["aprobadores"]))
        args.fn(args)
    except (ErrorTablero, objetos.ErrorObjeto, memoria.ErrorMemoria) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
