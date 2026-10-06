"""Organización igual en todas las ramas: `organizacion propagar` y el aviso de `sync`.

Las skills y la guía se leen de la rama en la que trabaja cada uno, y los cron y la rutina leen `main`: las rutas de
`rutas_organizacion_propagar` (equipo.json) tienen que ser iguales en `dev`, `main` y cada `dev-<modo>`. Un cambio de
organización entra primero en `dev` y después se propaga por PR a cada destino.

- Destinos: `main` y las ramas remotas que se llaman exactamente `dev-<modo>` con un modo conocido (`flujo.MODOS`);
  nunca las ramas de trabajo `dev-<modo>-<n>-<slug>`. Se descubren con `git ls-remote --heads origin`.
- Propagar a un destino: rama `org/propagar-<AAAAMMDD>-<destino>` desde `origin/<destino>` con las rutas copiadas de
  `origin/dev` y borrado lo que ya no existe en `dev` dentro de ellas, un commit y su PR. Si ya hay una PR
  `org/propagar-*` abierta hacia ese destino, de cualquier día, se actualiza su rama y no se abre otra. Todo con git de bajo nivel
  (índice temporal, `commit-tree` y `push` del commit): no se toca el árbol de trabajo ni la rama actual del usuario.
- Comparar es solo git (`ls-tree` de las cabezas remotas): `sync` lo usa para avisar sin gastar API de GitHub.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import tempfile
from datetime import date
from fnmatch import fnmatchcase
from pathlib import Path

import flujo
from base import CONFIG, INTEGRACION, REPO, ErrorTablero, gh

RUTAS = CONFIG["rutas_organizacion_propagar"]
ESTABLE = CONFIG["rama_estable"]
PREFIJO_RAMA = "org/propagar-"
MAX_FICHEROS_EN_PR = 40
CEROS = "0" * 40
# Raíz del repositorio en el que se ejecuta git (None = el directorio actual). Los tests la cambian.
RAIZ: str | None = None


def _git(*args: str, entrada: str | None = None, entorno: dict | None = None) -> str:
    proc = subprocess.run(["git", *args], capture_output=True, text=True, encoding="utf-8", input=entrada,
                          env=entorno, cwd=RAIZ)
    if proc.returncode != 0:
        raise ErrorTablero(f"git {' '.join(args[:3])}: {proc.stderr.strip()}")
    return proc.stdout


# --- reglas puras ---------------------------------------------------------------------------------

def coincide(ruta: str, patrones: list[str]) -> bool:
    """True si la ruta cae en algún patrón: carpeta (`x/`), comodín (`x*`) o fichero exacto (y lo que cuelga de él)."""
    for patron in patrones:
        if patron.endswith("/"):
            encaja = ruta.startswith(patron)
        elif any(c in patron for c in "*?["):
            encaja = fnmatchcase(ruta, patron)
        else:
            encaja = ruta == patron or ruta.startswith(patron + "/")
        if encaja:
            return True
    return False


def limitadores(patrones: list[str]) -> list[str]:
    """Prefijos de carpeta que acotan `ls-tree` a lo que pueden cubrir los patrones (lista vacía = todo el árbol)."""
    lista = []
    for patron in patrones:
        corte = min((patron.index(c) for c in "*?[" if c in patron), default=len(patron))
        prefijo = patron[:corte]
        if corte < len(patron):  # con comodín: la carpeta que lo contiene
            prefijo = prefijo.rsplit("/", 1)[0] if "/" in prefijo else ""
        prefijo = prefijo.rstrip("/")
        if not prefijo:
            return []
        lista.append(prefijo)
    return sorted(set(lista))


def destinos(salida_ls_remote: str, integracion: str = INTEGRACION, estable: str = ESTABLE,
             modos: tuple[str, ...] = flujo.MODOS) -> list[str]:
    """Ramas destino de la propagación según `git ls-remote --heads origin`: `main` y cada `dev-<modo>` exacta."""
    ramas = {linea.split("refs/heads/", 1)[1].strip()
             for linea in salida_ls_remote.splitlines() if "refs/heads/" in linea}
    candidatas = [estable, *(flujo.rama_de_modo(m, integracion) for m in modos)]
    return [r for r in candidatas if r in ramas]


def diferencias(origen: dict[str, tuple[str, str]], destino: dict[str, tuple[str, str]]) -> tuple[dict, list[str]]:
    """(entradas de `origen` que faltan o cambian en `destino`, rutas de `destino` que ya no existen en `origen`)."""
    copiar = {ruta: valor for ruta, valor in origen.items() if destino.get(ruta) != valor}
    return copiar, sorted(set(destino) - set(origen))


def rama_propagacion(destino: str, dia: date) -> str:
    return f"{PREFIJO_RAMA}{dia:%Y%m%d}-{destino}"


def es_pr_de_organizacion(pr: dict) -> bool:
    """PR que abre `organizacion propagar`: sin issue y sin revisión, solo rutas de organización."""
    return (pr.get("headRefName") or "").startswith(PREFIJO_RAMA)


def es_pr_hacia_estable(pr: dict) -> bool:
    """PR a `main` que no es trabajo de una issue: la de `dev` a `main` o una propagación de la organización."""
    return pr.get("baseRefName") == ESTABLE and (pr.get("headRefName") == INTEGRACION or es_pr_de_organizacion(pr))


def titulo_pr(destino: str) -> str:
    return f"chore(organización): propaga la organización de {INTEGRACION} a {destino}"


def cuerpo_pr(destino: str, copiar: dict, borrar: list[str]) -> str:
    rutas = sorted(copiar) + [f"{r} (se borra)" for r in borrar]
    lista = "\n".join(f"- `{r}`" for r in rutas[:MAX_FICHEROS_EN_PR])
    if len(rutas) > MAX_FICHEROS_EN_PR:
        lista += f"\n- … y {len(rutas) - MAX_FICHEROS_EN_PR} más"
    quien = "La fusiona un aprobador (SkiTemplar o Mokius)." if destino == ESTABLE else "La puede fusionar cualquiera."
    return (f"Copia desde `{INTEGRACION}` las rutas de organización (`rutas_organizacion_propagar` de "
            f"`Scripts/tablero/equipo.json`) para que `{destino}` tenga la misma guía, skills, tablero y workflows.\n\n"
            f"{lista}\n\nSolo toca rutas de organización: va sin issue ni revisión cruzada. {quien}\n\n"
            "Generada por `tablero.py organizacion propagar --aplicar`.")


# --- git ------------------------------------------------------------------------------------------

def entradas(ref: str, patrones: list[str] = RUTAS) -> dict[str, tuple[str, str]]:
    """{ruta: (modo, objeto)} de los ficheros de `ref` dentro de los patrones (`ls-tree`, sin leer blobs)."""
    salida = _git("ls-tree", "-r", "-z", "--full-tree", ref, "--", *limitadores(patrones))
    resultado = {}
    for registro in filter(None, salida.split("\0")):
        cabecera, ruta = registro.split("\t", 1)
        modo, _tipo, objeto = cabecera.split()
        if coincide(ruta, patrones):
            resultado[ruta] = (modo, objeto)
    return resultado


def ramas_destino() -> list[str]:
    return destinos(_git("ls-remote", "--heads", "origin"))


def traer(ramas: list[str]) -> None:
    """Actualiza origin/<rama> de cada una (git, sin API de GitHub)."""
    refspecs = [f"+refs/heads/{r}:refs/remotes/origin/{r}" for r in dict.fromkeys(ramas)]
    _git("fetch", "--quiet", "--no-tags", "origin", *refspecs)


def comparar(destino: str) -> tuple[dict, list[str]]:
    """Diferencias de organización de origin/<destino> respecto a origin/dev (tras `traer`)."""
    return diferencias(entradas(f"origin/{INTEGRACION}"), entradas(f"origin/{destino}"))


def ramas_desfasadas() -> list[str]:
    """Destinos cuya organización difiere de dev, para el aviso de `sync`."""
    lista = ramas_destino()
    traer([INTEGRACION, *lista])
    return [d for d in lista if any(comparar(d))]


def commit_propagado(destino: str, copiar: dict, borrar: list[str]) -> str:
    """Commit sobre origin/<destino> con las rutas de dev, en un índice temporal: el árbol de trabajo no cambia."""
    with tempfile.TemporaryDirectory() as carpeta:
        entorno = {**os.environ, "GIT_INDEX_FILE": str(Path(carpeta) / "index")}
        _git("read-tree", f"origin/{destino}", entorno=entorno)
        registros = [f"0 {CEROS}\t{r}" for r in borrar] + [f"{m} {o}\t{r}" for r, (m, o) in sorted(copiar.items())]
        _git("update-index", "-z", "--index-info", entrada="".join(f"{r}\0" for r in registros), entorno=entorno)
        arbol = _git("write-tree", entorno=entorno).strip()
    return _git("commit-tree", arbol, "-p", f"origin/{destino}", "-m", titulo_pr(destino)).strip()


def propagacion_abierta(prs: list[dict], destino: str) -> dict | None:
    """PR de propagación abierta hacia `destino`, sea del día que sea (la más reciente si hubiera varias)."""
    candidatas = [pr for pr in prs if es_pr_de_organizacion(pr) and pr.get("baseRefName", destino) == destino]
    return max(candidatas, key=lambda pr: pr.get("number", 0), default=None)


def propagar(destino: str, copiar: dict, borrar: list[str], dia: date) -> str:
    """Sube la rama de la propagación y abre su PR. Devuelve la línea del informe.

    Si ya hay una PR de propagación abierta hacia `destino` (de hoy o de otro día), actualiza su rama en vez de abrir
    otra: una sola PR de organización pendiente por destino.
    """
    abiertas = json.loads(gh("pr", "list", "--repo", REPO, "--base", destino, "--state", "open", "--limit", "100",
                             "--json", "number,headRefName,baseRefName"))
    previa = propagacion_abierta(abiertas, destino)
    rama = previa["headRefName"] if previa else rama_propagacion(destino, dia)
    commit = commit_propagado(destino, copiar, borrar)
    _git("push", "--quiet", "origin", f"+{commit}:refs/heads/{rama}")  # la rama es del tablero, no de una persona
    if previa:
        return f"{destino}: rama {rama} actualizada; su PR #{previa['number']} ya estaba abierta"
    url = gh("pr", "create", "--repo", REPO, "--base", destino, "--head", rama, "--title", titulo_pr(destino),
             "--body", cuerpo_pr(destino, copiar, borrar)).strip().splitlines()[-1]
    return f"{destino}: PR abierta {url}"


def cmd_propagar(args: argparse.Namespace) -> None:
    lista = ramas_destino()
    traer([INTEGRACION, *lista])
    print(f"## Organización de {INTEGRACION} en {', '.join(lista) or 'ninguna rama destino'}"
          + ("" if args.aplicar else " (simulación: usa --aplicar)") + "\n")
    for destino in lista:
        copiar, borrar = comparar(destino)
        if not copiar and not borrar:
            print(f"- {destino}: igual que {INTEGRACION}")
            continue
        print(f"- {destino}: difiere ({len(copiar)} ficheros por copiar, {len(borrar)} por borrar)")
        if args.aplicar:
            print(f"  {propagar(destino, copiar, borrar, date.today())}")


def anadir_comandos(sub: argparse._SubParsersAction) -> None:
    p = sub.add_parser("organizacion", help="organización igual en dev, main y cada dev-<modo>")
    acciones = p.add_subparsers(dest="accion", required=True)
    q = acciones.add_parser("propagar", help="copiar por PR las rutas de organización de dev a main y a cada "
                                             "dev-<modo> (sin --aplicar, solo dice qué ramas difieren)")
    q.add_argument("--aplicar", action="store_true")
    q.set_defaults(fn=cmd_propagar)
