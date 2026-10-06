"""Colisiones entre PR abiertas: pares cuyos cambios chocan al mezclarlos.

Dos PR que tocan el mismo fichero en sitios distintos se fusionan solas; solo cuenta un par si
`git merge-tree` de sus dos cabezas da conflicto. Si git no puede comprobarlo (sin red, sin las
cabezas), se vuelve a lo prudente: los ficheros en común.

Cada par se convierte en una issue «Mezclar PR #a y #b: ficheros en común» con las instrucciones
para que un Claude las mezcle, y se cierra sola cuando el par deja de chocar o una de las dos PR
se fusiona o se cierra. Los binarios de Unreal no se mezclan: esa issue lleva `decision` para que
un aprobador decida qué versión gana. La localización generada no se mezcla a mano: se regenera, y
siempre igual (la PR que se fusione en segundo lugar recoge, une los `.po` y compila). Por eso un par
que choca solo en localización no crea issue, y la que exista se cierra. Una PR que solo enlaza issues descartadas
(`chamber`) no cuenta, y la issue de una colisión en la que está se cierra.
"""

from __future__ import annotations

import json
import re
import subprocess
from collections.abc import Callable, Iterable
from itertools import combinations

import memoria

Gh = Callable[..., str]
Conflicto = Callable[[int, int], "list[str] | None"]

ETIQUETA = "colision"
COLOR = "B60205"
DESCRIPCION_ETIQUETA = "Dos PR abiertas chocan al mezclarse: hay que mezclarlas (tablero.py colisiones)"
EXTENSIONES_BINARIAS = (".uasset", ".umap")
LOCALIZACION = "Content/Localization/"
REF_PR = "refs/remotes/tablero-pr/{}"
TITULO = re.compile(r"^Mezclar PR #(\d+) y #(\d+):")
SOLO_LOCALIZACION = "Solo localización generada: la regenera la PR que se fusione en segundo lugar"


def pares(ficheros_por_pr: dict[int, set[str]],
          conflicto: Conflicto | None = None) -> list[tuple[int, int, list[str]]]:
    """Pares (a, b) con a < b que chocan, con la lista ordenada de los ficheros en conflicto.

    Sin `conflicto`, cuenta todo fichero en común. Con él, se comprueban todos los pares, también
    los que no comparten ficheros: la lista de `gh` se corta en 100 ficheros y una PR grande
    puede chocar fuera de ella. Si devuelve None (no se pudo comprobar), cuenta los ficheros en común.
    """
    resultado = []
    for a, b in combinations(sorted(ficheros_por_pr), 2):
        comunes = ficheros_por_pr[a] & ficheros_por_pr[b]
        if not comunes and conflicto is None:
            continue
        en_conflicto = conflicto(a, b) if conflicto else None
        ficheros = comunes if en_conflicto is None else set(en_conflicto)
        if ficheros:
            resultado.append((a, b, sorted(ficheros)))
    return resultado


def titulo(a: int, b: int) -> str:
    primera, segunda = sorted((a, b))
    return f"Mezclar PR #{primera} y #{segunda}: ficheros en común"


def par_de_titulo(texto: str) -> tuple[int, int] | None:
    encontrado = TITULO.match(texto.strip())
    return tuple(sorted((int(encontrado[1]), int(encontrado[2])))) if encontrado else None


def hay_binarios(ficheros: list[str]) -> bool:
    return any(f.lower().endswith(EXTENSIONES_BINARIAS) for f in ficheros)


def es_localizacion(fichero: str) -> bool:
    return fichero.startswith(LOCALIZACION)


def solo_localizacion(ficheros: list[str]) -> bool:
    return bool(ficheros) and all(es_localizacion(f) for f in ficheros)


def separar(en_conflicto: list[tuple[int, int, list[str]]]) -> tuple[list, list]:
    """(pares con algún fichero que no es de localización, pares que chocan solo en localización)."""
    con_codigo = [par for par in en_conflicto if not solo_localizacion(par[2])]
    localizacion = [par for par in en_conflicto if solo_localizacion(par[2])]
    return con_codigo, localizacion


def etiquetas(ficheros: list[str]) -> list[str]:
    return [ETIQUETA, "decision"] if hay_binarios(ficheros) else [ETIQUETA]


def cuerpo(antigua: dict, reciente: dict, ficheros: list[str], integracion: str) -> str:
    """Cuerpo de la issue: ficheros en conflicto e instrucciones para mezclar (o para decidir si hay binarios).

    Solo se listan los ficheros que no son de localización: esos se regeneran y se cuentan en una línea aparte.
    """
    codigo = [f for f in ficheros if not es_localizacion(f)]
    lista = "\n".join(f"- `{f}`" for f in codigo)
    if len(codigo) < len(ficheros):
        lista += f"\n\nAdemás, {len(ficheros) - len(codigo)} ficheros de localización: se regeneran."
    cabecera = (f"Las PR #{antigua['number']} (`{antigua['headRefName']}`) y #{reciente['number']} "
                f"(`{reciente['headRefName']}`) están abiertas contra `{integracion}` y chocan al mezclarse en:"
                f"\n\n{lista}\n\n")
    if hay_binarios(ficheros):
        return cabecera + ("**No mezclar.** Hay `.uasset` o `.umap` en conflicto: son binarios y no se fusionan. "
                           "SkiTemplar o Mokius deciden qué versión gana (etiqueta `decision`); la otra PR rehace "
                           "su cambio sobre la versión elegida.")
    return cabecera + (
        "Instrucciones para Claude:\n"
        f"1. Rebasa la PR más reciente, #{reciente['number']}, sobre la rama de #{antigua['number']} "
        f"(`{antigua['headRefName']}`), o sobre `{integracion}` si #{antigua['number']} ya está fusionada.\n"
        "2. Mezcla las dos versiones de cada fichero conservando el comportamiento de ambas PR; "
        "no descartes ninguno de los dos cambios.\n"
        "3. Compila (`TortunaboEditor` DebugGame si hay C++) y ejecuta los tests que afecten.\n"
        "4. Actualiza la PR rebasada y comenta aquí el resultado. Esta issue se cierra sola cuando las dos "
        "PR dejan de chocar.")


def ficheros_de_pr(gh: Gh, repo: str, numero: int) -> set[str]:
    salida = gh("pr", "view", str(numero), "--repo", repo, "--json", "files")
    return {f["path"] for f in json.loads(salida).get("files") or []}


def colisiones_abiertas(gh: Gh, repo: str) -> list[dict]:
    """Issues `colision` abiertas (número y título), para no duplicar un par y cerrar las resueltas."""
    salida = gh("issue", "list", "--repo", repo, "--state", "open", "--label", ETIQUETA,
                "--limit", "200", "--json", "number,title")
    return json.loads(salida)


def resueltas(abiertas: list[dict], prs: set[int], vigentes: set[tuple[int, int]],
              localizacion: frozenset[tuple[int, int]] | set[tuple[int, int]] = frozenset(),
              descartadas: frozenset[int] | set[int] = frozenset()) -> list[tuple[int, str]]:
    """Issues `colision` que ya se pueden cerrar, con el motivo.

    Se cierran si alguna PR solo enlaza issues descartadas (`descartadas`), si ya no está abierta, si el par
    choca solo en localización (`localizacion`) o si ya no choca (no está en `vigentes`, los pares que necesitan
    issue).
    """
    resultado = []
    for issue in abiertas:
        par = par_de_titulo(issue["title"])
        if par is None:
            continue
        sin_juego = [n for n in par if n in descartadas]
        fuera = [n for n in par if n not in prs]
        if sin_juego:
            resultado.append((issue["number"], f"la PR #{sin_juego[0]} solo enlaza issues descartadas (`chamber`)"))
        elif fuera:
            resultado.append((issue["number"], f"la PR #{fuera[0]} ya no está abierta"))
        elif par in localizacion:
            resultado.append((issue["number"], SOLO_LOCALIZACION))
        elif par not in vigentes:
            resultado.append((issue["number"], f"las PR #{par[0]} y #{par[1]} ya se mezclan sin conflicto"))
    return resultado


def texto_cierre(motivo: str) -> str:
    """Comentario **Resumen** con el que se cierra una issue `colision` resuelta."""
    if motivo == SOLO_LOCALIZACION:
        return f"{memoria.CABECERA_RESUMEN}: {SOLO_LOCALIZACION} (`tablero.py colisiones`)."
    return memoria.texto_resumen("dos PR abiertas chocaban al mezclarse",
                                 f"ya no hace falta mezclarlas: {motivo} (`tablero.py colisiones`)")


# --- git ------------------------------------------------------------------------------------------

def _raiz() -> str | None:
    proc = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, encoding="utf-8")
    return proc.stdout.strip() or None if proc.returncode == 0 else None


def _git(*args: str) -> subprocess.CompletedProcess:
    """git desde la raíz del repo: merge-tree --name-only da las rutas relativas al directorio actual."""
    return subprocess.run(["git", *args], capture_output=True, text=True, encoding="utf-8", cwd=_raiz())


def traer_cabezas(numeros: Iterable[int], ramas: Iterable[str] = ()) -> bool:
    """Descarga la cabeza de cada PR en REF_PR y cada rama de `ramas` en origin/<rama>.

    Sin blobs: merge-tree solo pide los de los ficheros que chocan. En un clon superficial (el
    puente) hace falta la historia para encontrar la base común. Es git: no gasta API de GitHub.
    """
    refspecs = [f"+refs/pull/{n}/head:{REF_PR.format(n)}" for n in numeros]
    refspecs += [f"+refs/heads/{r}:refs/remotes/origin/{r}" for r in sorted(set(ramas))]
    if not refspecs:
        return True
    extra = ["--unshallow"] if _git("rev-parse", "--is-shallow-repository").stdout.strip() == "true" else []
    filtro = ["--filter=blob:none"] if _git("config", "remote.origin.promisor").stdout.strip() == "true" else []
    return _git("fetch", "--quiet", "--no-tags", *filtro, *extra, "origin", *refspecs).returncode == 0


def conflicto_git(a: int, b: int) -> list[str] | None:
    """Ficheros en conflicto al mezclar las cabezas de dos PR ([] si se mezclan solas, None si no se sabe)."""
    return _conflicto_refs(REF_PR.format(a), REF_PR.format(b))


def conflicto_con_base(numero: int, base: str) -> list[str] | None:
    """Ficheros en conflicto al mezclar la cabeza de una PR con origin/<base> (tras `traer_cabezas`)."""
    return _conflicto_refs(f"refs/remotes/origin/{base}", REF_PR.format(numero))


def _conflicto_refs(ref_a: str, ref_b: str) -> list[str] | None:
    proc = _git("merge-tree", "--write-tree", "--name-only", "--no-messages", ref_a, ref_b)
    if proc.returncode == 0:
        return []
    ficheros = [linea for linea in proc.stdout.splitlines()[1:] if linea.strip()]
    # git también sale con 1 si una ref no existe: sin ficheros en conflicto, no se sabe.
    return ficheros if proc.returncode == 1 and ficheros else None
