# Plan de implementación F0–F2: base Steam y CI, terreno preparado y Coop sobre mapas fijos

> For agentic workers: REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Implementador: `unreal-engine-engineer`; revisor: `code-reviewer`.

**Goal.** Dejar Tortunabo (UE 5.6, `main`) con (F0) un empaquetado Shipping que cocina solo lo que se juega, vuelve al menú o al lobby si un viaje falla, tiene AppID configurable y CI local; (F1) una fuente de terreno «preparada» común (manifest v2, `bake.bin`, `UTN_CoopMapData`, máscara de zonas permitidas, rescate fuera de zona en servidor, presupuesto de malla); (F2) el Coop jugado sobre mapas fijos (`ATN_ProcMapGenerator` en `Source=Prepared`) con **CP01**, un mapa propio de 600 × 600 m, las mecánicas de la carrera creadas con `ATN_BeachElement::SpawnElement` y la vegetación en `ATN_BeachDecorField`; y retirar `TN_RunGameMode`/`LVL_Demo01` conservando lo útil.

**Architecture.** El nivel manda: el terreno es StaticMesh cocinado (subnivel `_Terrain`), los datos de juego viven en un DataAsset cocinable (`UTN_CoopMapData`: camino, zonas, cajas de muerte, bake de alturas y máscara) y lo editable son marcadores no replicados (`ATN_CoopMarker`, subnivel `_Markers`). `ATN_ProcMapGenerator` gana `Source {Procedural, Prepared}`: en Prepared convierte DA + marcadores en un `TNProcMap::FLayout` sintético (lógica pura `TNCoopMap::LayoutFromCoopData`) y reutiliza nidos, meta, peligros, conchas, fauna, progreso y tormenta. La seguridad es de servidor: `UTN_SafeGroundSubsystem` expone un `ITN_SafeGroundProvider` (lo implementa el generador en Prepared) que consultan el vigilante `UTN_OutOfZoneWatchComponent` del `ATN_ProcMapGameMode`, `TNBeach::FindOpenSandSpot`/`DepthUnderTerrain` y las gaviotas. Toda decisión no trivial vive en cabeceras `*Decisions.h` puras con tests `IMPLEMENT_SIMPLE_AUTOMATION_TEST`; el Python offline tiene pytest.

**Tech Stack.** UE 5.6 C++ (módulo `Tortunabo`, DebugGame), Automation Framework, OnlineSubsystemSteam, Python 3.11 con `uv` (numpy, scipy, pillow, scikit-image, pyfqmr, pytest), Python de editor headless (`-run=pythonscript`), RunUAT `BuildCookRun`.

**Rama.** `macro-update`.

**Spec.** `Docs/2026-09-29-Plan-Maestro-Modos-y-Mapas.md` §2 (arquitectura común), §3.1 (Coop), §4 (P0), §5 (F0–F2), §7 (respuestas del director: 1, 4). Apoyos: `Docs/Analisis/2026-09-29/A_coop_volumetrico.md` (eliminado) (R1–R12), `Docs/Analisis/2026-09-29/F_gaps_steam.md`, `Docs/Inventario-Objetos-Arte-2026-09-29.md`, `Docs/Limpieza-2026-09-29.md` (estado de `macro-update`).

---

## Global Constraints

- Rama: **`macro-update`** (`main` queda congelada en `41a0ee8a5`). Cada tarea parte de `macro-update` y sus commits van ahí.
- Terreno: el método oficial es **camino** (`Scripts/terrain_path`, referencia `C01_camino`); `Mapa01` y `terrain_vol/density.py` están obsoletos y no se usan como referencia.
- Motor: UE 5.6 (`Tortunabo.uproject` → `EngineAssociation: "5.6"`); módulo único `Tortunabo`; dependencias nuevas solo en `Tortunabo.Build.cs` si se incluyen (este plan no añade ninguna: `Json` ya es privada).
- Binario del editor: siempre `UnrealEditor-Win64-DebugGame-Cmd.exe`; build y headless con el editor **cerrado**.
- Ficheros que NO se tocan en este plan (otros agentes): `TN_ShellBody.*`, `TN_BeachGiantCrab.*`, `TN_BeachToyTank.*`, `TN_BeachEnemy.*`, `TN_ShellDecisions.h`, `TN_ShellDecisionsTest.cpp`.
- `.umap` de Alvaro2rh: solo los tocan los scripts headless listados en este plan; antes de cada commit con `.umap`, `git log -1 --format=%an -- <umap>`; si el último autor no es Rodrigo, avisar antes de commitear.
- Commits: uno por tarea, conventional en español (`feat|fix|refactor|test|chore|ci|docs(ámbito): …`), **sin** `Co-Authored-By`; `git add` con las rutas de la tarea, nunca `git add -A`.
- Tamaño: ficheros nuevos 200–400 líneas (máx. 800), funciones < 50 líneas, anidamiento ≤ 4.
- Lógica pura en `Public/**/TN_*Decisions.h` (namespace propio, `inline`, sin `UObject`); tests en `Private/Tests/TN_*Test.cpp` con `EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter` y ruta `Tortunabo.<Área>.<Nombre>`.
- Enums en tests: `TestTrue(TEXT(…), A == B)`; `TestEqual` solo con tipos numéricos, `FString`, `FName` y `FVector`.
- Unity build: nada de símbolos genéricos en namespaces anónimos de `.cpp`; prefijo `TN` en cvars y helpers de fichero.
- Texto visible: `NSLOCTEXT`, nunca `FText::FromString` con literales; logs con `LogTortunabo` (`Core/TN_Log.h`).
- Sin `BlueprintImplementableEvent` para audio/VFX (convención del repo).
- Coordenadas: X = Norte, Y = Este, Z = arriba, uu = cm; manifest en uu de mundo; `water_uu = -400`.
- Voxelizado: `z_min_m = Z_MIN_M = -10` en C01, P01 y CP01 (rango `ZRange` de `macro-update`, 128 niveles por defecto; los niveles caben en `uint8`).
- Mapa preparado: rejilla del bake `step_uu = 100`, `z0_uu = -1000`, `zstep_uu = 50`, índice plano `j * nx + i` (i a lo largo de X); `nx = grid * 100 + 1` (CP01: 601).
- `bake.bin` v1: cabecera `<4sIiifffffII` de **44 bytes** (`TNCB`, versión 1, nx, ny, origin_x_uu, origin_y_uu, step_uu, z0_uu, zstep_uu, raw_size, zlib_size) + zlib RFC1950 con capas `top_rel_cm:u16`, `safe_low:u8`, `safe_high:u8`, `path_dist_dm:u16`; nivel inexistente = `255`; distancia lejana = `65535`.
- Manifest v2: conserva todas las claves v1 y añade `"manifest_version": 2` y el bloque `coop` (§ Tarea 11). La vegetación va **en línea** (`coop.vegetation.points`), no en fichero aparte (desviación consciente de A §2: 1–2 k puntos caben en el JSON y ahorran un parser binario).
- Máscara permitida: `safe_low ≤ k(Z_pies) ≤ safe_high + 1`, `k = round((Z − z0) / zstep)`; vadeable hasta `WATER_M − 0,8 m`.
- Vigilante: periodo `0,25 s`, gracia `1,5 s` en suelo no permitido, fuera de límites = rescate inmediato; límite inferior `z0 − 200 uu`; rescate a la muestra de `Main` con mayor `S ≤ LastSafe.Progress`, anillos de `150 uu`, separación mínima `120 uu`, aturdimiento `1,0 s`, reserva `SafetyNet 0,75 s`.
- CP01: generador camino (`terrain_path`) con `TN_PATH_GRID=6` (600 × 600 m), semilla `20260929`, ≤ **1 100 000** triángulos tras decimar, error de decimado ≤ **15 uu**, recorrible a pie de inicio a meta.
- Rutas de contenido: niveles `/Game/Maps/Coop/LVL_Coop_<Id>` (+ `_Terrain`, `_Markers`, `_Design`), DA `/Game/Coop/<Id>/DA_Coop_<Id>`, mallas `/Game/Terrain/Volumes/<Id>/Meshes/SM_*`, catálogo `/Game/Coop/DA_CoopMapCatalog`, GameMode `/Game/ProcMap/BP_ProcMapGameMode`.
- Etiquetas de actor: terreno `TN_MapTerrain`, agua `TN_Water`.
- Python: solo `uv run …`; nunca `python`/`pip` sueltos.
- Red: el generador solo replica `FTNProcMapNetConfig` (más `PreparedMapId`); marcadores y DA no se replican (van en el paquete del nivel); todo el rescate se decide en servidor.
- Puente de Mokius: **no existe** en `Content/` (búsqueda `*bridge*`/`*puente*`, 2026-09-29). CP01 usa puentes naturales (arcos de roca y pasarelas del estilo); el tipo de marcador `BridgeAnchor` se deja listo y el spawn del puente queda bloqueado por el asset (ver Autorrevisión).

## Comandos (exactos; `<Filtro>` se sustituye en cada tarea)

- **BUILD** (editor cerrado):
  `"C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" TortunaboEditor Win64 DebugGame -Project="C:\Users\Rodrigo\PERSONAL\ProyectosPersonales\Tortunabo\Tortunabo.uproject" -WaitMutex`
- **UETEST `<Filtro>`**:
  `"C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Win64-DebugGame-Cmd.exe" "C:\Users\Rodrigo\PERSONAL\ProyectosPersonales\Tortunabo\Tortunabo.uproject" -ExecCmds="Automation RunTests Tortunabo.<Filtro>; Quit" -nullrhi -unattended -nosplash`
  Resultado: `grep -a "Test Completed" "C:\Users\Rodrigo\PERSONAL\ProyectosPersonales\Tortunabo\Saved\Logs\Tortunabo.log"` → `Result={Success}` o `Result={Fail}` por test.
- **PYTEST** (desde la raíz del repo):
  `uv run --with numpy --with scipy --with pillow --with scikit-image --with pytest python -m pytest Scripts/tests -q`
  A partir de la Tarea 12 se añade `--with pyfqmr` (dependencia del decimado; también en `pyproject.toml`). Para un fichero: sustituir `Scripts/tests` por su ruta.
- **UEPY `<script>`** (Python de editor headless, editor cerrado):
  `"C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Win64-DebugGame-Cmd.exe" "C:\Users\Rodrigo\PERSONAL\ProyectosPersonales\Tortunabo\Tortunabo.uproject" -run=pythonscript -script="C:\Users\Rodrigo\PERSONAL\ProyectosPersonales\Tortunabo\Scripts\<script>" -EnablePlugins=PythonScriptPlugin -unattended -nosplash -nullrhi`
  Los `unreal.log` salen en `Saved\Logs\Tortunabo.log`, no en stdout. Variables de entorno antes del comando (`TN_VOLUME_DIR=…`).
- **SMOKE `<Nivel>` `<Texto>`** (desde Git Bash):
  `MSYS2_ARG_CONV_EXCL="*" "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Win64-DebugGame-Cmd.exe" "C:\Users\Rodrigo\PERSONAL\ProyectosPersonales\Tortunabo\Tortunabo.uproject" <Nivel> -game -nullrhi -nosound -unattended -NoSteam -testexit="<Texto>"`

## Review Focus

Modos de fallo que el spec implica y que ningún test del spec cubre; cada uno lleva su test en la tarea dueña.

1. **Un destino de viaje sin cocinar** (el Coop añade niveles nuevos; `LVL_Lobby`/`LVL_BeachRace` ya faltaban): el Shipping cuelga al cliente. Test `Scripts/tests/test_cook_list.py::test_every_travel_destination_is_cooked` y `::test_every_coop_level_is_cooked` (Tarea 1, ampliado en Tarea 33).
2. **Rescatar a una tortuga que otro está moviendo** (en brazos, en el pico de una gaviota, en la patada de la tormenta o derribada): el vigilante la teletransportaría y rompería la sujeción. Test `Tortunabo.Coop.WatchBusyMover` (Tarea 15).
3. **Dos rescates al mismo sitio en el mismo tick** (8 jugadoras cayendo a la vez en el mismo pozo): cápsulas solapadas y expulsión física. Test `Tortunabo.Coop.RescueSlot` (Tarea 15).
4. **`TN_REGENERATE=1` resucita marcadores borrados o mueve los retocados**: se pierde el trabajo del diseñador. Test `Scripts/tests/test_coop_markers.py` (Tarea 24).
5. **Meta orientada a −X o +X tratada como +Y** (R2) **y volumen de meta a la cota del mar en una meta alta** (R3): confeti y llegada no se disparan en P01/CP01. Test `Tortunabo.Coop.Finish` (Tarea 20).

## Mapa de ficheros

### Crear

| Fichero | Responsabilidad |
|---|---|
| `Source/Tortunabo/Public/Multiplayer/TN_TravelFailureDecisions.h` | Qué hacer ante `OnTravelFailure` según rol, menú y reintentos |
| `Source/Tortunabo/Public/Multiplayer/TN_SteamAppDecisions.h` | AppID compartido 480 y bloqueo en Shipping |
| `Source/Tortunabo/Public/Player/TN_GhostDebugDecisions.h` | Permiso de depuración remota del fantasma (N-A) |
| `Source/Tortunabo/Public/World/ProcMap/TN_CoopGridDecisions.h` | `FSafeGrid`, `ParseBake`, `IsInBounds`, `IsAllowed`, ventana de traza, distancia al mar |
| `Source/Tortunabo/Public/World/ProcMap/TN_CoopRescueDecisions.h` | Vigilante (`StepWatch`), mover ocupado, muestra de rescate, candidatos, hueco libre, `CourseBack` |
| `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h` | Meta (R2/R3), suelo del mapa (R1), `LayoutFromCoopData`, id de mapa, URL de viaje, mecánicas Coop |
| `Source/Tortunabo/Public/World/ProcMap/TN_ProcFaunaDecisions.h` | Grupos de fauna por tramos de bioma cuando no hay módulos (R5) |
| `Source/Tortunabo/Public/UI/Loading/TN_LoadingMapDecisions.h` | Qué mapas son «de rondas» para la pantalla de carga (R8) |
| `Source/Tortunabo/Public/World/TN_SafeGroundProvider.h` | Interfaz `ITN_SafeGroundProvider` |
| `Source/Tortunabo/Public/World/TN_SafeGroundSubsystem.h` / `Private/World/TN_SafeGroundSubsystem.cpp` | Registro del proveedor de suelo seguro por mundo |
| `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapData.h` / `Private/World/ProcMap/TN_CoopMapData.cpp` | DataAsset del mapa preparado + `LoadFromMemory`/`LoadFromManifest` |
| `Source/Tortunabo/Public/World/ProcMap/TN_CoopMarker.h` / `Private/World/ProcMap/TN_CoopMarker.cpp` | Marcador no replicado (salida, meta, nido, mecánica, medusa, anclaje de puente) |
| `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapAnchor.h` / `Private/World/ProcMap/TN_CoopMapAnchor.cpp` | Ancla editor-only con `KnownIds` (lápidas del importador) |
| `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapCatalog.h` / `Private/World/ProcMap/TN_CoopMapCatalog.cpp` | Catálogo de mapas Coop (id, nivel, nombre, vista previa) |
| `Source/Tortunabo/Public/Game/TN_OutOfZoneWatchComponent.h` / `Private/Game/TN_OutOfZoneWatchComponent.cpp` | Vigilante de servidor y rescate |
| `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Prepared.cpp` | Rama Prepared del generador y su proveedor de suelo seguro |
| `Source/Tortunabo/Private/Tests/TN_TravelFailureDecisionsTest.cpp` | `Tortunabo.Net.*` |
| `Source/Tortunabo/Private/Tests/TN_GhostDebugDecisionsTest.cpp` | `Tortunabo.Ghost.RemoteDebug` |
| `Source/Tortunabo/Private/Tests/TN_BeachGoldenTest.cpp` + `Private/Tests/Golden/beach_t0.txt` | Dorado T0 del reparto de la carrera |
| `Source/Tortunabo/Private/Tests/TN_CoopTestUtil.h` | Bytes de bake y manifest de prueba |
| `Source/Tortunabo/Private/Tests/TN_CoopGridDecisionsTest.cpp` | `Tortunabo.Coop.Bake/Allowed/WalkableTunnel/SeaDistance` |
| `Source/Tortunabo/Private/Tests/TN_CoopRescueDecisionsTest.cpp` | `Tortunabo.Coop.Watch*/Rescue*` |
| `Source/Tortunabo/Private/Tests/TN_CoopMapDecisionsTest.cpp` | `Tortunabo.Coop.Finish/GroundHit/Layout/MapId/TravelURL/Elements` |
| `Source/Tortunabo/Private/Tests/TN_CoopMapDataTest.cpp` | `Tortunabo.Coop.LoadFromManifest/ProviderRegistration/OpenSpotWithProvider` |
| `Source/Tortunabo/Private/Tests/TN_ProcFaunaDecisionsTest.cpp` | `Tortunabo.ProcMap.FaunaGroups` |
| `Source/Tortunabo/Public/World/ProcMap/TN_CoopDecorDecisions.h` + `Private/Tests/TN_CoopDecorDecisionsTest.cpp` | Vegetación del DA como piezas de `ATN_BeachDecorField` (`Tortunabo.Coop.Decor`) |
| `Source/Tortunabo/Private/Tests/TN_LoadingMapDecisionsTest.cpp` | `Tortunabo.Loading.RoundMaps` |
| `Source/Tortunabo/Private/Tests/TN_LobbyMissionTest.cpp` | `Tortunabo.Lobby.NextSelectorMode` |
| `Source/Tortunabo/Private/Tests/TN_MatchGameModeTest.cpp` | `Tortunabo.Retire.RunGameMode` |
| `Scripts/terrain_vol/coop.py` | BFS con padres, camino ordenado, ancho, máscara, `bake.bin`, bloque `coop` |
| `Scripts/terrain_vol/decimate.py` | Decimado cuádrico con borde fijo y tope de error |
| `Scripts/terrain_coop/__init__.py`, `Scripts/terrain_coop/cp01.py` | Diseño de CP01 (estilo, mecánicas, vegetación) |
| `Scripts/gen_terrain_coop.py` | CLI: genera el bloque Coop de CP01, C01 y P01 |
| `Scripts/coop_markers.py` | Sincronía pura de marcadores (sin dependencias; la usa el editor) |
| `Scripts/terrain_import_common.py` | Funciones de importación compartidas (StaticMesh por trozo, spawn, sol) |
| `Scripts/import_terrain_coop.py` | Importador headless del nivel Coop con subniveles, marcadores y ancla |
| `Scripts/verify_terrain_collision.py` | R9: colisión del SM frente al bake |
| `Scripts/retire_run_mode.py` | Borra `LVL_Demo01` (su script se borra con `git rm`) |
| `Scripts/verify_gamemode_redirect.py` | Comprueba que los BP de GameMode heredan de la base renombrada |
| `Scripts/tools/set_steam_appid.py` | Escribe el AppID en los dos `.ini` |
| `Scripts/ci/build_check.bat`, `Scripts/ci/ci_local.bat`, `Scripts/ci/check_automation_report.py`, `.githooks/post-merge` | Build DebugGame relativo y CI local |
| `Scripts/tests/test_cook_list.py`, `test_steam_appid.py`, `test_ci_report.py`, `test_terrain_coop.py`, `test_terrain_decimate.py`, `test_coop_markers.py`, `test_cp01.py` | pytest |

### Modificar

| Fichero | Cambio |
|---|---|
| `Config/DefaultGame.ini:15-32` | `+DirectoriesToAlwaysCook=/Game/Coop` (Tarea 1); `LVL_Run` fuera de `MapsToCook` (Tarea 35); niveles Coop (Tareas 24 y 33) |
| `Config/DefaultEngine.ini` | `[CoreRedirects]` de `TN_RunGameMode` (Tarea 36) |
| `pyproject.toml` | `pyfqmr` |
| `Source/Tortunabo/Public/Multiplayer/MP_GameInstance.h` / `Private/Multiplayer/MP_GameInstance.cpp` | `OnTravelFailure`, aviso de AppID, `SelectedCoopMapId` |
| `Source/Tortunabo/Private/Player/TN_SpectatorGhost.cpp:485-494` | Permiso de depuración remota |
| `Scripts/gen_terrain_volume.py:58` | `walk(..., min_z_m=None)` |
| `Scripts/terrain_path/layout.py:11` | `GRID` desde `TN_PATH_GRID` (4 por defecto; CP01 usa 6) |
| `Scripts/gen_terrain_path.py:76-116` | `solve()` extraída de `build_one` |
| `Scripts/gen_terrain_platforms.py:101-118` | `solve_platforms()` extraída de `build` |
| `Scripts/import_terrain_mesh.py` | Usa `terrain_import_common.py` |
| `Source/Tortunabo/Public/World/ProcMap/TN_ProcMapEnums.h` | `ETNTerrainSource`, `ETNCoopMarkerKind` |
| `Source/Tortunabo/Public/World/ProcMap/TN_ProcMapGenerator.h` | `Source`, `CoopData`, proveedor, `PreparedMapId`, `GetSeaNear` |
| `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator.cpp:85, 159-226, 355-384` | Meta con `Dir` (R2), rama Prepared, suelo por etiqueta (R1) |
| `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Spawn.cpp:231, 431, 444` | Z de salida, nidos y meta (R3/R4) |
| `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Build.cpp:3301-3309` | `SpawnFauna()` extraída (R5) |
| `Source/Tortunabo/Private/World/ProcMap/TN_ProcFauna.cpp:259-392` | Grupos sin `Layout.Modules` (R5) |
| `Source/Tortunabo/Private/Audio/TN_AmbientSoundscape.cpp:424` | `SeaNear` del generador (R6) |
| `Source/Tortunabo/Private/UI/Loading/TN_LoadingScreenSubsystem.cpp:539, 614, 1145` | `TNLoadingMaps::IsRoundMap` (R8) |
| `Source/Tortunabo/Private/World/Beach/TN_BeachStun.cpp:368-455` | Proveedor primero en `FindOpenSandSpot`/`DepthUnderTerrain` |
| `Source/Tortunabo/Public/World/Beach/TN_BeachGullZone.h` / `Private/World/Beach/TN_BeachGullZone.cpp:320-331, 1247-1266` | `CourseBackAt` |
| `Source/Tortunabo/Public/World/Beach/TN_BeachDecorField.h` / `Private/World/Beach/TN_BeachDecorField.cpp:91-112, 211-225` | Entrada con Z fija |
| `Source/Tortunabo/Public/Game/TN_ProcMapGameMode.h` / `Private/Game/TN_ProcMapGameMode.cpp` | Vigilante (Tarea 18), `ValidateCoopMapOption` (Tarea 31) |
| `Source/Tortunabo/Public/Lobby/TN_HQGameMode.h:110-119` / `Private/Lobby/TN_HQGameMode.cpp:388-432` | Coop → catálogo; fuera Clásico/`LVL_Run` |
| `Source/Tortunabo/Public/Lobby/TN_LobbyMission.h` / `Private/Lobby/TN_LobbyMission.cpp:80-153` | `SetCoopMap`, `GetHostCoopMap`, `TN.Coop.Map` (Tarea 31); `SanitizeMode` y selector sin `Classic` (Tarea 35) |
| `Source/Tortunabo/Private/UI/Briefing/TN_BriefingWidget.cpp:627-631` | La orden del día nombra el mapa Coop |
| `Source/Tortunabo/Public/Game/TN_RunGameMode.h` → `TN_MatchGameModeBase.h` (y los ficheros que lo nombran) | Renombrado a la base `ATN_MatchGameModeBase`, `Abstract` (Tarea 36; nombre de `Docs/Limpieza-2026-09-29.md` 2.3) |

---

# F0 — Base Steam y CI

### Tarea 1: Guardia del cocinado (pytest) y `/Game/Coop` en el paquete

Estado de partida en `macro-update`: `MapsToCook` ya es explícito y `/Game/_Deprecado` no se cocina (`ab4744e14`, `Docs/Limpieza-2026-09-29.md` 2.8); la lista aún incluye `LVL_Run`, que sale en la Tarea 35 junto con el Clásico. Esta tarea pone la guardia que impide volver atrás y cocina `/Game/Coop` (DA de los mapas y catálogo).

**Files:**
- Modify: `Config/DefaultGame.ini:15-32` (sección `[/Script/UnrealEd.ProjectPackagingSettings]`)
- Create: `Scripts/tests/test_cook_list.py`

**Interfaces:**
- Consumes: `MenuMapPath` (`MP_GameInstance.h:424`), `LobbyMapPath` (`TN_RunGameMode.h:133`), `ProcMapPath`/`BeachRaceMapPath`/`MatchMapPath` (`TN_HQGameMode.h:111-119`).
- Produces: `read_ini_list(path, section, key) -> list[str]`; `FORBIDDEN` y `TRAVEL_MEMBERS`, que amplía la Tarea 35.

- [ ] **Paso 1: test que falla.** Crear `Scripts/tests/test_cook_list.py`:

```python
"""Guardia del cocinado (Config/DefaultGame.ini): todo destino de viaje se cocina y nada de prueba se cuela.

    uv run --with numpy --with scipy --with pillow --with scikit-image --with pytest python -m pytest Scripts/tests/test_cook_list.py -q
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INI = ROOT / "Config" / "DefaultGame.ini"
SECTION = "[/Script/UnrealEd.ProjectPackagingSettings]"
REQUIRED = (
    "/Game/Maps/Lobby/LVL_Menu",
    "/Game/Maps/Lobby/LVL_HQ",
    "/Game/Maps/Lobby/LVL_Lobby",
    "/Game/Maps/Run/LVL_BeachRace",
    "/Game/Maps/Run/LVL_ProcMap",
    "/Game/Maps/LVL_Transition",
)
# LVL_Run se añade aquí en la Tarea 35 (retirada del Clásico).
FORBIDDEN = ("LVL_TestMap", "LVL_LevelMetrics", "LVL_ProcGenDemo", "LVL_Demo01", "LVL_Mapa01", "LVL_MapVariants",
             "LVL_ProcMap_Terrain")
# Miembros de C++ cuyo valor por defecto es un destino de viaje (MatchMapPath desaparece en la Tarea 35).
TRAVEL_MEMBERS = ("MenuMapPath", "LobbyMapPath", "ProcMapPath", "BeachRaceMapPath", "MatchMapPath")


def section_lines(path: Path, section: str) -> list[str]:
    out, inside = [], False
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("["):
            inside = line == section
            continue
        if inside and line and not line.startswith(";"):
            out.append(line)
    return out


def read_ini_list(path: Path, section: str, key: str) -> list[str]:
    pattern = re.compile(rf'^\+{key}=\((?:FilePath|Path)="([^"]+)"\)$')
    return [m.group(1) for line in section_lines(path, section) if (m := pattern.match(line))]


def travel_destinations() -> set[str]:
    found = set()
    member = re.compile(r'FString\s+(\w+)\s*=\s*TEXT\("(/Game/Maps/[^"]+)"\)')
    for header in (ROOT / "Source" / "Tortunabo" / "Public").rglob("*.h"):
        for name, value in member.findall(header.read_text(encoding="utf-8", errors="ignore")):
            if name in TRAVEL_MEMBERS:
                found.add(value)
    return found


def test_required_maps_are_cooked():
    cooked = read_ini_list(INI, SECTION, "MapsToCook")
    missing = [m for m in REQUIRED if m not in cooked]
    assert not missing, f"faltan en MapsToCook: {missing}"


def test_maps_directory_is_not_cooked_wholesale():
    dirs = read_ini_list(INI, SECTION, "DirectoriesToAlwaysCook")
    assert "/Game/Maps" not in dirs, "DirectoriesToAlwaysCook=/Game/Maps anula MapsToCook y cuela los mapas de prueba"


def test_no_test_map_is_cooked():
    cooked = "\n".join(f'{m}"' for m in read_ini_list(INI, SECTION, "MapsToCook"))
    leaked = [name for name in FORBIDDEN if name in cooked]
    assert not leaked, f"mapas de prueba en el cocinado: {leaked}"


def test_every_travel_destination_is_cooked():
    cooked = set(read_ini_list(INI, SECTION, "MapsToCook"))
    missing = sorted(travel_destinations() - cooked)
    assert not missing, f"destinos de viaje sin cocinar: {missing}"


def test_every_coop_level_is_cooked():
    cooked = set(read_ini_list(INI, SECTION, "MapsToCook"))
    coop_dir = ROOT / "Content" / "Maps" / "Coop"
    levels = [f"/Game/Maps/Coop/{p.stem}" for p in coop_dir.glob("LVL_Coop_*.umap")
              if not p.stem.endswith(("_Terrain", "_Markers", "_Design"))] if coop_dir.exists() else []
    missing = [lvl for lvl in levels if lvl not in cooked]
    assert not missing, f"niveles Coop sin cocinar: {missing}"


def test_coop_content_is_cooked_and_deprecated_is_not():
    assert "/Game/Coop" in read_ini_list(INI, SECTION, "DirectoriesToAlwaysCook")
    assert "/Game/_Deprecado" in read_ini_list(INI, SECTION, "DirectoriesToNeverCook")
```

  (`test_no_test_map_is_cooked` compara con comillas de cierre: `LVL_Run"` no casa con `LVL_Run_…`, y en la Tarea 35 se añade `LVL_Run"` a `FORBIDDEN`.)
- [ ] **Paso 2: ver el fallo.** PYTEST con `Scripts/tests/test_cook_list.py` → 5 passed y `test_coop_content_is_cooked_and_deprecated_is_not` en FAIL (`/Game/Coop` no está).
- [ ] **Paso 3: implementación.** En `Config/DefaultGame.ini`, justo después de `+DirectoriesToAlwaysCook=(Path="/Game/ProcMap")`, añadir `+DirectoriesToAlwaysCook=(Path="/Game/Coop")`.
- [ ] **Paso 4: ver pasar.** PYTEST con el fichero → 6 passed.
- [ ] **Paso 5: commit.** `git add Config/DefaultGame.ini Scripts/tests/test_cook_list.py` · `git commit -m "test(cook): guardia de destinos de viaje y mapas de prueba; /Game/Coop en el paquete"`

### Tarea 2: Verificar que `Source/Logs` está fuera del repo

Hecho en `macro-update` (`Docs/Limpieza-2026-09-29.md` 1.1 y 1.7; `.gitignore`, sección «Logs de ejecución»). Solo verificación, sin commit.

**Files:** ninguno. **Interfaces:** ninguna.

- [ ] **Paso 1: verificar.** `git ls-files Source/Logs` → vacío; `git check-ignore -v Source/Logs/Tortunabo_2.log` → imprime la regla de `.gitignore`. Si cualquiera de los dos falla: añadir `Source/Logs/` al final de `.gitignore`, `git rm --cached -r Source/Logs` y commit `chore(repo): Source/Logs fuera del control de versiones`.

### Tarea 3: `OnTravelFailure` con vuelta al lobby (anfitrión) o al menú (invitado)

**Files:**
- Create: `Source/Tortunabo/Public/Multiplayer/TN_TravelFailureDecisions.h`, `Source/Tortunabo/Private/Tests/TN_TravelFailureDecisionsTest.cpp`
- Modify: `Source/Tortunabo/Public/Multiplayer/MP_GameInstance.h:486-497`, `Source/Tortunabo/Private/Multiplayer/MP_GameInstance.cpp:102-105` y tras `:1515`

**Interfaces:**
- Consumes: `UMP_GameInstance::HideLoadingScreen()`, `DestroyCurrentSession()`, `PendingMenuNotice`, `MenuMapPath`, `LobbyReturnMapPath`, `IsMenuWorld(UWorld*)`, `bIsPendingTravel`.
- Produces: `TNTravel::ETravelFailureAction {ReturnHostToLobby, ReturnToMenu, StayInMenu}`; `TNTravel::DecideTravelFailure(ENetMode NetMode, bool bInMenu, int32 PriorFailures) -> ETravelFailureAction`; `TNTravel::LobbyTravelURL(const FString&) -> FString`; `TNTravel::ActionName(ETravelFailureAction) -> const TCHAR*`; `void UMP_GameInstance::OnTravelFailure(UWorld*, ETravelFailure::Type, const FString&)`; `int32 UMP_GameInstance::TravelFailureCount`.

- [ ] **Paso 1: test que falla.** Crear `Source/Tortunabo/Private/Tests/TN_TravelFailureDecisionsTest.cpp`:

```cpp
// Fallo de viaje (N-E): el anfitrión vuelve al lobby con todos; el invitado, al menú; un segundo fallo seguido, al menú.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Net; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Multiplayer/TN_TravelFailureDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTravelFailureTest, "Tortunabo.Net.TravelFailure",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTravelFailureTest::RunTest(const FString& Parameters)
{
	using namespace TNTravel;
	TestTrue(TEXT("Anfitrión en partida → lobby"), DecideTravelFailure(NM_ListenServer, false, 0) == ETravelFailureAction::ReturnHostToLobby);
	TestTrue(TEXT("Invitado → menú"), DecideTravelFailure(NM_Client, false, 0) == ETravelFailureAction::ReturnToMenu);
	TestTrue(TEXT("Standalone → menú"), DecideTravelFailure(NM_Standalone, false, 0) == ETravelFailureAction::ReturnToMenu);
	TestTrue(TEXT("Ya en el menú → quedarse"), DecideTravelFailure(NM_Client, true, 0) == ETravelFailureAction::StayInMenu);
	TestTrue(TEXT("Anfitrión con un fallo previo → menú (sin bucle lobby→lobby)"), DecideTravelFailure(NM_ListenServer, false, 1) == ETravelFailureAction::ReturnToMenu);
	TestEqual(TEXT("Lobby por defecto"), LobbyTravelURL(FString()), FString(TEXT("/Game/Maps/Lobby/LVL_Lobby")));
	TestEqual(TEXT("Lobby del que se salió"), LobbyTravelURL(TEXT("/Game/Maps/Lobby/LVL_HQ")), FString(TEXT("/Game/Maps/Lobby/LVL_HQ")));
	return true;
}

#endif
```

- [ ] **Paso 2: ver el fallo.** BUILD → error de compilación `TN_TravelFailureDecisions.h: No such file` (el test no compila: fallo esperado).
- [ ] **Paso 3: implementación mínima.** Crear `Source/Tortunabo/Public/Multiplayer/TN_TravelFailureDecisions.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"

/**
 * Qué hacer cuando un viaje falla (UEngine::OnTravelFailure: mapa sin cocinar, paquete que falta, URL mala). Sin esto,
 * un ServerTravel fallido deja al invitado colgado en la pantalla de carga (F_gaps_steam N-E).
 */
namespace TNTravel
{
	enum class ETravelFailureAction : uint8
	{
		/** El anfitrión vuelve al lobby con todos (ServerTravel). */
		ReturnHostToLobby,
		/** Sesión destruida y al menú con aviso. */
		ReturnToMenu,
		/** Ya estaba en el menú: solo el aviso. */
		StayInMenu
	};

	inline constexpr const TCHAR* DefaultLobbyPath = TEXT("/Game/Maps/Lobby/LVL_Lobby");

	/** PriorFailures: fallos seguidos desde el último mapa cargado; el segundo ya no reintenta el lobby. */
	inline ETravelFailureAction DecideTravelFailure(ENetMode NetMode, bool bInMenu, int32 PriorFailures)
	{
		if (bInMenu)
		{
			return ETravelFailureAction::StayInMenu;
		}
		const bool bHost = NetMode == NM_ListenServer || NetMode == NM_DedicatedServer;
		return bHost && PriorFailures == 0 ? ETravelFailureAction::ReturnHostToLobby : ETravelFailureAction::ReturnToMenu;
	}

	inline FString LobbyTravelURL(const FString& LobbyReturnMapPath)
	{
		return LobbyReturnMapPath.IsEmpty() ? FString(DefaultLobbyPath) : LobbyReturnMapPath;
	}

	inline const TCHAR* ActionName(ETravelFailureAction Action)
	{
		switch (Action)
		{
		case ETravelFailureAction::ReturnHostToLobby: return TEXT("anfitrión al lobby");
		case ETravelFailureAction::ReturnToMenu:      return TEXT("al menú");
		default:                                      return TEXT("se queda en el menú");
		}
	}
}
```

- [ ] **Paso 4: ver pasar el test puro.** BUILD; UETEST `Net.TravelFailure` → `Result={Success}`.
- [ ] **Paso 5: cablear en la GameInstance.** En `MP_GameInstance.h`, junto a `OnNetworkFailure` (línea 487):

```cpp
	/** @brief Hook de fallo de viaje (OnTravelFailure): anfitrión al lobby, invitado al menú (TNTravel::DecideTravelFailure). */
	void OnTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	/** Fallos de viaje seguidos desde el último mapa cargado (lo pone a 0 HandlePostLoadMap). */
	int32 TravelFailureCount = 0;
```

  En `MP_GameInstance.cpp`, añadir `#include "Multiplayer/TN_TravelFailureDecisions.h"`; en `Init()` justo después de `GEngine->OnNetworkFailure().AddUObject(...)` (línea 104): `GEngine->OnTravelFailure().AddUObject(this, &UMP_GameInstance::OnTravelFailure);`; en `HandlePostLoadMap` (primera línea del cuerpo): `TravelFailureCount = 0;`; y tras `HandleConnectionLost` (después de la línea 1515):

```cpp
void UMP_GameInstance::OnTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	const ENetMode NetMode = World ? World->GetNetMode() : NM_Standalone;
	const TNTravel::ETravelFailureAction Action = TNTravel::DecideTravelFailure(NetMode, IsMenuWorld(World), TravelFailureCount);
	++TravelFailureCount;
	UE_LOG(LogTortunabo, Error, TEXT("[MP] TRAVEL FAILURE %s: %s → %s"), ETravelFailure::ToString(FailureType), *ErrorString,
		TNTravel::ActionName(Action));
	bIsPendingTravel = false;
	HideLoadingScreen();
	if (Action == TNTravel::ETravelFailureAction::ReturnHostToLobby && World)
	{
		World->ServerTravel(TNTravel::LobbyTravelURL(LobbyReturnMapPath));
		return;
	}
	PendingMenuNotice.Text = NSLOCTEXT("TNRooms", "TravelFailed", "No se ha podido cargar la partida y has vuelto al menú.");
	PendingMenuNotice.bError = true;
	PendingMenuNotice.bOpenJoin = false;
	if (Action == TNTravel::ETravelFailureAction::ReturnToMenu)
	{
		DestroyCurrentSession();
		if (APlayerController* PC = GetFirstLocalPlayerController())
		{
			PC->ClientTravel(MenuMapPath, TRAVEL_Absolute);
		}
	}
}
```

- [ ] **Paso 6: verificar.** BUILD sin errores; UETEST `Net` → todo `Success`. Verificación manual (PIE 2 jugadores, listen): en la consola del anfitrión `servertravel /Game/Maps/NoExiste` → log `[MP] TRAVEL FAILURE` con `anfitrión al lobby` y los dos acaban en `LVL_Lobby`.
- [ ] **Paso 7: commit.** `git add Source/Tortunabo/Public/Multiplayer/TN_TravelFailureDecisions.h Source/Tortunabo/Private/Tests/TN_TravelFailureDecisionsTest.cpp Source/Tortunabo/Public/Multiplayer/MP_GameInstance.h Source/Tortunabo/Private/Multiplayer/MP_GameInstance.cpp` · `git commit -m "fix(red): OnTravelFailure devuelve al anfitrión al lobby y al invitado al menú"`

### Tarea 4: AppID configurable y bloqueo del 480 en Shipping

**Files:**
- Create: `Source/Tortunabo/Public/Multiplayer/TN_SteamAppDecisions.h`, `Scripts/tools/set_steam_appid.py`, `Scripts/tests/test_steam_appid.py`
- Modify: `Source/Tortunabo/Private/Tests/TN_TravelFailureDecisionsTest.cpp` (añade un test), `Source/Tortunabo/Private/Multiplayer/MP_GameInstance.cpp` (`Init`, tras `EnsureSteamAppIdFile`)

**Interfaces:**
- Produces: `TNSteamApp::SharedTestAppId = 480`; `TNSteamApp::IsSharedTestAppId(int32) -> bool`; `TNSteamApp::IsShippingBlocker(int32 AppId, bool bShipping) -> bool`; `set_steam_appid.set_app_id(root: Path, app_id: int, allow_test: bool = False) -> list[Path]`.

- [ ] **Paso 1: tests que fallan.** Añadir al final (antes de `#endif`) de `TN_TravelFailureDecisionsTest.cpp`:

```cpp
#include "Multiplayer/TN_SteamAppDecisions.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSteamAppIdTest, "Tortunabo.Net.SteamAppId",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSteamAppIdTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("480 es el AppID de prueba compartido"), TNSteamApp::IsSharedTestAppId(480));
	TestTrue(TEXT("Shipping con 480 bloquea"), TNSteamApp::IsShippingBlocker(480, true));
	TestTrue(TEXT("Shipping sin AppID bloquea"), TNSteamApp::IsShippingBlocker(0, true));
	TestFalse(TEXT("Development con 480 no bloquea"), TNSteamApp::IsShippingBlocker(480, false));
	TestFalse(TEXT("Shipping con AppID propio no bloquea"), TNSteamApp::IsShippingBlocker(3141590, true));
	return true;
}
```

  (El `#include` va arriba con los demás.) Crear `Scripts/tests/test_steam_appid.py`:

```python
"""AppID de Steam coherente en los dos .ini y escrito por Scripts/tools/set_steam_appid.py."""

from __future__ import annotations

import re
import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Scripts" / "tools"))

from set_steam_appid import read_app_ids, set_app_id  # noqa: E402


def copy_config(tmp_path: Path) -> Path:
    (tmp_path / "Config").mkdir()
    for name in ("DefaultEngine.ini", "DefaultGame.ini"):
        shutil.copy(ROOT / "Config" / name, tmp_path / "Config" / name)
    return tmp_path


def test_both_ini_files_agree():
    ids = read_app_ids(ROOT)
    assert len(set(ids.values())) == 1, f"AppID distinto en cada .ini: {ids}"


def test_set_app_id_rewrites_both_files(tmp_path):
    root = copy_config(tmp_path)
    changed = set_app_id(root, 3141590)
    assert len(changed) == 2
    assert set(read_app_ids(root).values()) == {3141590}


def test_refuses_shared_test_app_id(tmp_path):
    root = copy_config(tmp_path)
    with pytest.raises(ValueError):
        set_app_id(root, 480)


def test_keeps_the_rest_of_the_file(tmp_path):
    root = copy_config(tmp_path)
    before = (root / "Config" / "DefaultEngine.ini").read_text(encoding="utf-8")
    set_app_id(root, 3141590)
    after = (root / "Config" / "DefaultEngine.ini").read_text(encoding="utf-8")
    assert re.sub(r"SteamDevAppId=\d+", "", before) == re.sub(r"SteamDevAppId=\d+", "", after)
```

- [ ] **Paso 2: ver el fallo.** PYTEST con `Scripts/tests/test_steam_appid.py` → `ModuleNotFoundError: set_steam_appid`. BUILD → no compila (`TN_SteamAppDecisions.h` no existe).
- [ ] **Paso 3: implementación.** Crear `Source/Tortunabo/Public/Multiplayer/TN_SteamAppDecisions.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

/** AppID de Steam: el 480 (Spacewar) lo comparten miles de proyectos; publicar con él es un error (F_gaps_steam #1). */
namespace TNSteamApp
{
	inline constexpr int32 SharedTestAppId = 480;

	inline bool IsSharedTestAppId(int32 AppId) { return AppId == SharedTestAppId; }

	/** true si una build Shipping sale con el AppID de prueba o sin AppID. */
	inline bool IsShippingBlocker(int32 AppId, bool bShipping)
	{
		return bShipping && (AppId <= 0 || IsSharedTestAppId(AppId));
	}
}
```

  En `MP_GameInstance.cpp`, `#include "Multiplayer/TN_SteamAppDecisions.h"` y en `Init()` justo antes de `if (GEngine)` (línea 102):

```cpp
	if (TNSteamApp::IsShippingBlocker(SteamDevAppId, UE_BUILD_SHIPPING != 0))
	{
		UE_LOG(LogTortunabo, Error, TEXT("[MP] Build Shipping con SteamDevAppId=%d: cambiarlo con Scripts/tools/set_steam_appid.py antes de publicar."),
			SteamDevAppId);
	}
```

  Crear `Scripts/tools/set_steam_appid.py`:

```python
"""Escribe el AppID de Steam en Config/DefaultEngine.ini ([OnlineSubsystemSteam]) y Config/DefaultGame.ini
([/Script/Tortunabo.MP_GameInstance]). Se ejecuta cuando Valve asigna el AppID:

    uv run python Scripts/tools/set_steam_appid.py 3141590
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

SHARED_TEST_APP_ID = 480
FILES = {"DefaultEngine.ini": "[OnlineSubsystemSteam]", "DefaultGame.ini": "[/Script/Tortunabo.MP_GameInstance]"}
KEY = re.compile(r"^SteamDevAppId=(\d+)\s*$")


def _rewrite(text: str, section: str, app_id: int | None) -> tuple[str, int | None]:
    out, inside, found = [], False, None
    for line in text.splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("["):
            inside = stripped == section
        match = KEY.match(stripped) if inside else None
        if match:
            found = int(match.group(1))
            if app_id is not None:
                line = f"SteamDevAppId={app_id}\n"
        out.append(line)
    return "".join(out), found


def read_app_ids(root: Path) -> dict[str, int | None]:
    return {name: _rewrite((root / "Config" / name).read_text(encoding="utf-8"), section, None)[1]
            for name, section in FILES.items()}


def set_app_id(root: Path, app_id: int, allow_test: bool = False) -> list[Path]:
    if app_id <= 0 or (app_id == SHARED_TEST_APP_ID and not allow_test):
        raise ValueError(f"AppID {app_id} no valido para publicar (480 es el de prueba compartido)")
    changed = []
    for name, section in FILES.items():
        path = root / "Config" / name
        text, found = _rewrite(path.read_text(encoding="utf-8"), section, app_id)
        if found is None:
            raise ValueError(f"{path}: falta SteamDevAppId en {section}")
        path.write_text(text, encoding="utf-8")
        changed.append(path)
    return changed


def main() -> None:
    parser = argparse.ArgumentParser(description="Escribe el AppID de Steam en los .ini del proyecto.")
    parser.add_argument("app_id", type=int)
    parser.add_argument("--allow-test", action="store_true", help="permite 480 (solo desarrollo)")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    for path in set_app_id(root, args.app_id, args.allow_test):
        print(f"{path}: SteamDevAppId={args.app_id}")


if __name__ == "__main__":
    main()
```

- [ ] **Paso 4: ver pasar.** PYTEST con `Scripts/tests/test_steam_appid.py` → 4 passed. BUILD; UETEST `Net.SteamAppId` → `Success`.
- [ ] **Paso 5: trámite (manual, Rodrigo).** Steamworks → alta de desarrollador (100 USD), crear la app «Tortunavy», anotar el AppID; cuando llegue: `uv run python Scripts/tools/set_steam_appid.py <AppID>` y commit aparte `chore(steam): AppID propio`. Plazo de revisión de Valve: semanas; se inicia hoy.
- [ ] **Paso 6: commit.** `git add Source/Tortunabo/Public/Multiplayer/TN_SteamAppDecisions.h Source/Tortunabo/Private/Tests/TN_TravelFailureDecisionsTest.cpp Source/Tortunabo/Private/Multiplayer/MP_GameInstance.cpp Scripts/tools/set_steam_appid.py Scripts/tests/test_steam_appid.py` · `git commit -m "feat(steam): AppID configurable por script y error en Shipping con el 480"`

### Tarea 5: Depuración remota del fantasma solo en editor o con cvar (N-A)

**Files:**
- Create: `Source/Tortunabo/Public/Player/TN_GhostDebugDecisions.h`, `Source/Tortunabo/Private/Tests/TN_GhostDebugDecisionsTest.cpp`
- Modify: `Source/Tortunabo/Private/Player/TN_SpectatorGhost.cpp:485-494`

**Interfaces:**
- Produces: `TNGhostDebug::IsRemoteDebugAllowed(bool bWithEditor, int32 AllowRemoteDebugCVar) -> bool`; cvar `TN.Ghost.AllowRemoteDebug` (0 por defecto, `ECVF_Cheat`).

- [ ] **Paso 1: test que falla.** `TN_GhostDebugDecisionsTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Player/TN_GhostDebugDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNGhostRemoteDebugTest, "Tortunabo.Ghost.RemoteDebug",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNGhostRemoteDebugTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("En el editor, sí"), TNGhostDebug::IsRemoteDebugAllowed(true, 0));
	TestFalse(TEXT("Build Development sin cvar, no (N-A)"), TNGhostDebug::IsRemoteDebugAllowed(false, 0));
	TestTrue(TEXT("Build Development con TN.Ghost.AllowRemoteDebug 1, sí"), TNGhostDebug::IsRemoteDebugAllowed(false, 1));
	TestFalse(TEXT("Otro valor de la cvar, no"), TNGhostDebug::IsRemoteDebugAllowed(false, 2));
	return true;
}

#endif
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila (falta la cabecera).
- [ ] **Paso 3: implementación.** `Source/Tortunabo/Public/Player/TN_GhostDebugDecisions.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

/** N-A (F_gaps_steam §3): un invitado no ejecuta comandos de depuración en el anfitrión de una build distribuida. */
namespace TNGhostDebug
{
	inline bool IsRemoteDebugAllowed(bool bWithEditor, int32 AllowRemoteDebugCVar)
	{
		return bWithEditor || AllowRemoteDebugCVar == 1;
	}
}
```

  En `TN_SpectatorGhost.cpp`: `#include "Player/TN_GhostDebugDecisions.h"` y, a nivel de fichero tras los includes:

```cpp
static TAutoConsoleVariable<int32> CVarTNGhostAllowRemoteDebug(TEXT("TN.Ghost.AllowRemoteDebug"), 0,
	TEXT("1 = el anfitrión acepta comandos de depuración del fantasma pedidos por invitados (fuera del editor)."), ECVF_Cheat);
```

  y el cuerpo de `ServerRunDebug_Implementation` (líneas 485-494) pasa a:

```cpp
void ATN_SpectatorGhost::ServerRunDebug_Implementation(uint8 Command, int32 PlayerIndex)
{
#if !UE_BUILD_SHIPPING
	if (!TNGhostDebug::IsRemoteDebugAllowed(WITH_EDITOR != 0, CVarTNGhostAllowRemoteDebug.GetValueOnGameThread()))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Ghost] Comando de depuración %u de un invitado rechazado (TN.Ghost.AllowRemoteDebug 0)."), Command);
		return;
	}
	if (Command <= static_cast<uint8>(TNGhostInternal::EDebugCommand::Become))
	{
		TNGhostInternal::RunDebugCommand(GetWorld(), static_cast<TNGhostInternal::EDebugCommand>(Command), PlayerIndex, GetOwnerController());
	}
#endif
}
```

- [ ] **Paso 4: ver pasar.** BUILD; UETEST `Ghost.RemoteDebug` → `Success`.
- [ ] **Paso 5: commit.** `git add Source/Tortunabo/Public/Player/TN_GhostDebugDecisions.h Source/Tortunabo/Private/Tests/TN_GhostDebugDecisionsTest.cpp Source/Tortunabo/Private/Player/TN_SpectatorGhost.cpp` · `git commit -m "fix(seguridad): la depuración remota del fantasma exige editor o TN.Ghost.AllowRemoteDebug"`

### Tarea 6: Dorado T0 del reparto de la carrera

**Files:** Create `Source/Tortunabo/Private/Tests/TN_BeachGoldenTest.cpp`, `Source/Tortunabo/Private/Tests/Golden/beach_t0.txt` (lo escribe el primer run).

**Interfaces:**
- Consumes: `TNBeachLayout::GenerateRound(int32 Seed, ETNProcDifficulty, FRoundLayout&)` (`TN_BeachLayout.h:4283`), `FItem` (`:1744`).
- Produces: `TNBeachGoldenTest::HashRound(const TNBeachLayout::FRoundLayout&) -> uint64`; fichero dorado (72 líneas `semilla dificultad hash`) que protege F5a.

- [ ] **Paso 1: test que falla.** `TN_BeachGoldenTest.cpp`:

```cpp
// Dorado T0 (plan maestro §3.2): 24 semillas × 3 dificultades del reparto de la playa, antes de tocar nada de F5.
// El primer run escribe Golden/beach_t0.txt y falla a propósito: revisarlo, commitearlo y volver a ejecutar.

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/Beach/TN_BeachLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBeachGoldenTest
{
	uint64 HashRound(const TNBeachLayout::FRoundLayout& Layout)
	{
		uint64 Hash = 1469598103934665603ull;
		auto Mix = [&Hash](int64 Value)
		{
			for (int32 Byte = 0; Byte < 8; ++Byte)
			{
				Hash ^= static_cast<uint8>(Value >> (8 * Byte));
				Hash *= 1099511628211ull;
			}
		};
		Mix(Layout.Items.Num());
		for (const TNBeachLayout::FItem& Item : Layout.Items)
		{
			Mix(static_cast<int64>(Item.Element));
			Mix(FMath::RoundToInt64(Item.Pos.X));
			Mix(FMath::RoundToInt64(Item.Pos.Y));
			Mix(FMath::RoundToInt64(Item.Yaw * 10.0));
			Mix(Item.Spec.Seed);
			Mix(FMath::RoundToInt64(Item.Spec.SizeScale * 1000.f));
			Mix(FMath::RoundToInt64(Item.Spec.Extent));
			Mix(static_cast<int64>(Item.Role));
		}
		return Hash;
	}

	FString GoldenPath()
	{
		return FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/Tortunabo/Private/Tests/Golden/beach_t0.txt"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachGoldenT0Test, "Tortunabo.Beach.GoldenT0",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachGoldenT0Test::RunTest(const FString& Parameters)
{
	TArray<FString> Lines;
	for (int32 Seed = 1; Seed <= 24; ++Seed)
	{
		for (int32 Difficulty = 0; Difficulty < 3; ++Difficulty)
		{
			TNBeachLayout::FRoundLayout Layout;
			TNBeachLayout::GenerateRound(Seed, static_cast<ETNProcDifficulty>(Difficulty), Layout);
			Lines.Add(FString::Printf(TEXT("%d %d %016llx"), Seed, Difficulty, TNBeachGoldenTest::HashRound(Layout)));
		}
	}
	const FString Path = TNBeachGoldenTest::GoldenPath();
	if (!FPaths::FileExists(Path))
	{
		FFileHelper::SaveStringArrayToFile(Lines, *Path);
		AddError(FString::Printf(TEXT("Dorado T0 creado en %s: revisarlo, commitearlo y volver a ejecutar."), *Path));
		return false;
	}
	TArray<FString> Golden;
	FFileHelper::LoadFileToStringArray(Golden, *Path);
	TestEqual(TEXT("Entradas del dorado"), Golden.Num(), Lines.Num());
	for (int32 i = 0; i < FMath::Min(Golden.Num(), Lines.Num()); ++i)
	{
		TestEqual(*FString::Printf(TEXT("Reparto %s"), *Lines[i].Left(5)), Lines[i], Golden[i]);
	}
	return true;
}

#endif
```

- [ ] **Paso 2: ver el fallo.** BUILD; UETEST `Beach.GoldenT0` → `Result={Fail}` con «Dorado T0 creado»; existe `Golden/beach_t0.txt` con 72 líneas (`wc -l`).
- [ ] **Paso 3: ver pasar.** UETEST `Beach.GoldenT0` otra vez → `Success`. Correrlo una tercera vez → `Success` (determinismo).
- [ ] **Paso 4: commit.** `git add Source/Tortunabo/Private/Tests/TN_BeachGoldenTest.cpp Source/Tortunabo/Private/Tests/Golden/beach_t0.txt` · `git commit -m "test(carrera): dorado T0 del reparto con 24 semillas y 3 dificultades"`

### Tarea 7: `Scripts/ci/build_check.bat` relativo y CI local

**Files:**
- Create: `Scripts/ci/build_check.bat` (el de la raíz está en `Deprecado/` desde `macro-update`, Limpieza 1.6), `Scripts/ci/ci_local.bat`, `Scripts/ci/check_automation_report.py`, `Scripts/tests/test_ci_report.py`, `.githooks/post-merge`

**Interfaces:**
- Produces: `check_automation_report.summarize(report: dict) -> tuple[int, int, list[str]]` (total, fallidos, rutas fallidas); `check_automation_report.main(argv) -> int` (0 = verde); `ci_local.bat` (códigos 1 build, 2 tests UE, 3 pytest, 4 cook).

- [ ] **Paso 1: test que falla.** `Scripts/tests/test_ci_report.py`:

```python
"""Lectura del informe de Automation (index.json de -ReportExportPath) para la CI local."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Scripts" / "ci"))

from check_automation_report import main, summarize  # noqa: E402

REPORT = {"succeeded": 3, "succeededWithWarnings": 1, "failed": 1, "notRun": 0,
          "tests": [{"fullTestPath": "Tortunabo.Coop.Bake", "state": "Success"},
                    {"fullTestPath": "Tortunabo.Coop.Watch", "state": "Fail"}]}


def test_summarize_counts_and_lists_failures():
    total, failed, paths = summarize(REPORT)
    assert (total, failed, paths) == (5, 1, ["Tortunabo.Coop.Watch"])


def test_main_fails_with_a_failed_test(tmp_path):
    path = tmp_path / "index.json"
    path.write_text(json.dumps(REPORT), encoding="utf-8-sig")
    assert main([str(path), "--min-tests", "1"]) == 1


def test_main_fails_when_too_few_tests_ran(tmp_path):
    path = tmp_path / "index.json"
    path.write_text(json.dumps({**REPORT, "failed": 0, "tests": []}), encoding="utf-8")
    assert main([str(path), "--min-tests", "87"]) == 1


def test_main_passes_when_green(tmp_path):
    path = tmp_path / "index.json"
    path.write_text(json.dumps({**REPORT, "failed": 0, "tests": []}), encoding="utf-8")
    assert main([str(path), "--min-tests", "4"]) == 0
```

- [ ] **Paso 2: ver el fallo.** PYTEST con `Scripts/tests/test_ci_report.py` → `ModuleNotFoundError`.
- [ ] **Paso 3: implementación.** `Scripts/ci/check_automation_report.py`:

```python
"""Falla (codigo 1) si el informe de Automation tiene tests en rojo o menos de --min-tests ejecutados.

    uv run python Scripts/ci/check_automation_report.py Saved/Automation/CI/index.json --min-tests 87
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def summarize(report: dict) -> tuple[int, int, list[str]]:
    failed = int(report.get("failed", 0))
    total = int(report.get("succeeded", 0)) + int(report.get("succeededWithWarnings", 0)) + failed
    paths = [t.get("fullTestPath", "?") for t in report.get("tests", []) if t.get("state") == "Fail"]
    return total, failed, paths


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path)
    parser.add_argument("--min-tests", type=int, default=87)
    args = parser.parse_args(argv)
    if not args.report.exists():
        print(f"sin informe: {args.report}")
        return 1
    total, failed, paths = summarize(json.loads(args.report.read_text(encoding="utf-8-sig")))
    for path in paths:
        print(f"FALLA {path}")
    print(f"{total} tests, {failed} en rojo (minimo {args.min_tests})")
    return 0 if failed == 0 and total >= args.min_tests else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
```

  `Scripts/ci/build_check.bat`:

```bat
@echo off
setlocal
if "%UE_ROOT%"=="" set "UE_ROOT=C:\Program Files\Epic Games\UE_5.6"
set "PROJECT=%~dp0..\..\Tortunabo.uproject"
"%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" TortunaboEditor Win64 DebugGame -Project="%PROJECT%" -WaitMutex -NoHotReload > "%TEMP%\ue_build_out.txt" 2>&1
set "BUILD_EXIT=%ERRORLEVEL%"
findstr /i /c:"error C" /c:"warning C" /c:": error" /c:"cannot open" "%TEMP%\ue_build_out.txt" > "%TEMP%\ue_build_errors.txt"
echo BUILD EXIT CODE: %BUILD_EXIT%
type "%TEMP%\ue_build_errors.txt"
exit /b %BUILD_EXIT%
```

  `Scripts/ci/ci_local.bat`:

```bat
@echo off
rem CI local: build DebugGame, tests de Automation Tortunabo, pytest y (TN_CI_COOK=1) BuildCookRun Shipping.
setlocal
if "%UE_ROOT%"=="" set "UE_ROOT=C:\Program Files\Epic Games\UE_5.6"
set "ROOT=%~dp0..\.."
set "PROJECT=%ROOT%\Tortunabo.uproject"
set "REPORT=%ROOT%\Saved\Automation\CI"
call "%~dp0build_check.bat" || exit /b 1
if exist "%REPORT%" rmdir /s /q "%REPORT%"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Win64-DebugGame-Cmd.exe" "%PROJECT%" -ExecCmds="Automation RunTests Tortunabo; Quit" -ReportExportPath="%REPORT%" -nullrhi -unattended -nosplash -NoSteam
uv run python "%ROOT%\Scripts\ci\check_automation_report.py" "%REPORT%\index.json" --min-tests 87 || exit /b 2
pushd "%ROOT%"
uv run --with numpy --with scipy --with pillow --with scikit-image --with pytest --with pyfqmr python -m pytest Scripts/tests -q
if errorlevel 1 (popd & exit /b 3)
popd
if "%TN_CI_COOK%"=="1" (
  call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJECT%" -noP4 -platform=Win64 -clientconfig=Shipping -build -cook -stage -pak -archive -archivedirectory="%ROOT%\Saved\Packages" -unattended -utf8output || exit /b 4
)
echo CI LOCAL OK
```

  `.githooks/post-merge`:

```sh
#!/bin/sh
# CI local tras cada merge. Opt-in: git config core.hooksPath .githooks. TN_CI_SKIP=1 la salta.
[ "$TN_CI_SKIP" = "1" ] && exit 0
cmd.exe //c "Scripts\\ci\\ci_local.bat"
```

  `git update-index --chmod=+x .githooks/post-merge`.
- [ ] **Paso 4: ver pasar.** PYTEST con `Scripts/tests/test_ci_report.py` → 4 passed. `cmd.exe //c Scripts\ci\build_check.bat` → `BUILD EXIT CODE: 0`. `cmd.exe //c Scripts\ci\ci_local.bat` → `CI LOCAL OK` (≥ 87 tests; con el editor cerrado).
- [ ] **Paso 5: commit.** `git add Scripts/ci Scripts/tests/test_ci_report.py .githooks/post-merge` · `git commit -m "ci: build_check relativo en DebugGame y CI local con informe de Automation y pytest"`

### Tarea 8: Playtest Shipping con dos máquinas (manual)

**Files:** ninguno de código. Build en `Saved/Packages/Windows`.
**Interfaces:** Consumes Tareas 1, 3, 4.

- [ ] **Paso 1: empaquetar.** `set TN_CI_COOK=1 && cmd.exe //c Scripts\ci\ci_local.bat` → `CI LOCAL OK`; `Saved/Packages/Windows/Tortunabo.exe` existe. Una segunda build `-clientconfig=Development` (mismo comando RunUAT cambiando `Shipping` por `Development` y `-archivedirectory=…\Saved\PackagesDev`) para las pruebas de consola.
- [ ] **Paso 2: checklist Shipping (2 PC con Steam abierto, cuentas distintas).** Marcar SÍ/NO y anotar el commit:
  1. Menú carga; crear sala pública de 4.
  2. El invitado encuentra la sala y entra en `LVL_Lobby` (no se queda en carga).
  3. Carrera en la playa (`LVL_BeachRace`) arranca con los dos.
  4. Fin de ronda → vuelta a `LVL_Lobby` → «Salir» → menú en las dos máquinas.
  5. `Saved/Logs` sin `Failed to load package` ni `TRAVEL FAILURE`.
  6. Log del anfitrión con `[MP] Build Shipping con SteamDevAppId=480` mientras no haya AppID propio (esperado).
- [ ] **Paso 3: checklist Development (fallo de viaje forzado).** Anfitrión en `LVL_Lobby` → consola `servertravel /Game/Maps/NoExiste` → los dos acaban en `LVL_Lobby` y el log dice `anfitrión al lobby`; repetirlo desde un invitado que se desconecta a mitad de viaje (cerrar red 5 s) → menú con el aviso «No se ha podido cargar la partida…».
- [ ] **Paso 4: registro.** Añadir el resultado a la sección Build/Steam de la checklist de playtest del proyecto (`project_playtest_checklist`). Sin commit.

---

# F1 — Terreno preparado común

### Tarea 9: Alcance con cota mínima y BFS con padres (camino ordenado de P01)

**Files:**
- Modify: `Scripts/gen_terrain_volume.py:58-89` (`walk`)
- Create: `Scripts/terrain_vol/coop.py` (primera parte), `Scripts/tests/test_terrain_coop.py`

**Interfaces:**
- Consumes: `global_standable`, `ground_level`, `world_index`, `build_all` (`gen_terrain_volume.py`); `PlatformModel`, `pick_start_end` (P01).
- Produces: `walk(standable, start, dry_only=True, z_min_m=Z_MIN_M, min_z_m: float | None = None) -> np.ndarray` (`z_min_m` ya existe en `macro-update`); `coop.MOVES: list[tuple[int, int, int]]`; `coop.min_level(min_z_m: float) -> int`; `coop.walk_with_parents(standable, start, min_k: int, links=()) -> tuple[np.ndarray, np.ndarray, dict]`; `coop.trace_path(parent, link_parent, start, end) -> list[tuple[int, int, int]]`.

- [ ] **Paso 1: tests que fallan.** Crear `Scripts/tests/test_terrain_coop.py`:

```python
"""Datos del Coop sobre mapas fijos (Scripts/terrain_vol/coop.py): camino ordenado, mascara y bake.bin.

    uv run --with numpy --with scipy --with pillow --with scikit-image --with pytest python -m pytest Scripts/tests/test_terrain_coop.py -q
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from gen_terrain_platforms import pick_start_end  # noqa: E402
from gen_terrain_volume import build_all, global_standable, ground_level, walk, world_index, zone_map  # noqa: E402
from terrain_platforms.layout import GRID as P01_GRID  # noqa: E402
from terrain_platforms.model import PlatformModel  # noqa: E402
from terrain_vol import coop  # noqa: E402
from terrain_vol.export import global_top  # noqa: E402
from terrain_vol.layout import WATER_M  # noqa: E402


@pytest.fixture(scope="module")
def p01():
    model = PlatformModel()
    chunks = build_all(model, grid=P01_GRID)
    standable = global_standable(chunks, grid=P01_GRID)
    start_xy, end_xy = pick_start_end(model.layout)
    s_ij, e_ij = world_index(start_xy), world_index(end_xy)
    start = (*s_ij, ground_level(standable, *s_ij))
    end = (*e_ij, ground_level(standable, *e_ij))
    return {"model": model, "chunks": chunks, "standable": standable, "start": start, "end": end}


@pytest.fixture(scope="module")
def p01_walk(p01):
    return coop.walk_with_parents(p01["standable"], p01["start"], coop.min_level(WATER_M + 0.2))


def test_parents_reach_the_same_cells_as_walk(p01, p01_walk):
    seen, _, _ = p01_walk
    assert np.array_equal(seen, walk(p01["standable"], p01["start"]))


def test_min_z_extends_walk_to_shallow_water(p01):
    dry = walk(p01["standable"], p01["start"])
    wade = walk(p01["standable"], p01["start"], min_z_m=WATER_M - 0.8)
    assert wade.sum() >= dry.sum()
    assert np.all(wade[dry])


def test_p01_path_is_ordered_from_start_to_end(p01, p01_walk):
    _, parent, link_parent = p01_walk
    path = coop.trace_path(parent, link_parent, p01["start"], p01["end"])
    assert path[0] == p01["start"] and path[-1] == p01["end"]
    legal = set(coop.MOVES)
    for a, b in zip(path, path[1:]):
        assert (b[0] - a[0], b[1] - a[1], b[2] - a[2]) in legal, f"paso ilegal {a} -> {b}"


def test_trace_path_rejects_an_unreached_end(p01, p01_walk):
    _, parent, link_parent = p01_walk
    with pytest.raises(ValueError):
        coop.trace_path(parent, link_parent, p01["start"], (0, 0, 0))
```

- [ ] **Paso 2: ver el fallo.** PYTEST con `Scripts/tests/test_terrain_coop.py` → `ImportError: cannot import name 'coop'`.
- [ ] **Paso 3: implementación.** En `Scripts/gen_terrain_volume.py`, cabecera de `walk` y cálculo de `dry_k` (el resto del cuerpo no cambia):

```python
def walk(standable: np.ndarray, start: tuple[int, int, int], dry_only: bool = True, z_min_m: float = Z_MIN_M,
         min_z_m: float | None = None) -> np.ndarray:
    """Celdas alcanzables desde start andando (desnivel <= CLIMB_STEPS entre vecinas) o saltando
    en linea recta hasta JUMP_CELLS celdas (sin subir mas de una muestra). z_min_m: cota del nivel 0.
    min_z_m manda sobre dry_only: cota mas baja pisable (p. ej. WATER_M - 0,8 para vadear)."""
    if min_z_m is not None:
        dry_k = max(0, int(np.ceil((min_z_m - z_min_m) / STEP_Z_M)))
    else:
        dry_k = int(np.ceil((WATER_M + 0.2 - z_min_m) / STEP_Z_M)) if dry_only else 0
```

  Crear `Scripts/terrain_vol/coop.py`:

```python
"""Datos del Coop sobre mapas fijos (plan maestro §2.1, informe A §2): camino ordenado por BFS con padres sobre lo
caminable, ancho, mascara de niveles permitidos (safe_low/high) y bake.bin. Mismas reglas de paso que
gen_terrain_volume.walk: andar con desnivel <= CLIMB_STEPS o saltar en linea recta hasta JUMP_CELLS celdas."""

from __future__ import annotations

from collections import deque

import numpy as np

from .layout import STEP_Z_M, Z_MIN_M

CLIMB_STEPS = 2
JUMP_CELLS = 3
LINK_CODE = 255                                   # llegada por un enlace (medusa de un escalon)


def _moves() -> list[tuple[int, int, int]]:
    out = []
    for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        out.extend((di, dj, dk) for dk in range(-CLIMB_STEPS, CLIMB_STEPS + 1))
        for reach in range(2, JUMP_CELLS + 1):
            out.extend((di * reach, dj * reach, dk) for dk in range(-4, 2))
    return out


MOVES = _moves()                                  # codigo de padre = indice + 1 (0 = inicio o no alcanzada)


def min_level(min_z_m: float) -> int:
    return max(0, int(np.ceil((min_z_m - Z_MIN_M) / STEP_Z_M)))


def _link_feet(links, shape) -> dict[tuple[int, int], int]:
    feet = {}
    for n, ((fi, fj), _) in enumerate(links):
        for a in range(fi - 1, fi + 2):
            for b in range(fj - 1, fj + 2):
                if 0 <= a < shape[0] and 0 <= b < shape[1]:
                    feet.setdefault((a, b), n)
    return feet


def walk_with_parents(standable: np.ndarray, start: tuple[int, int, int], min_k: int, links=()):
    """(seen, parent, link_parent). links: ((i, j) del pie, (i, j) de arriba), como los saltos de medusa de C01."""
    ni, nj, nk = standable.shape
    seen = np.zeros_like(standable)
    parent = np.zeros(standable.shape, dtype=np.uint8)
    link_parent: dict[tuple[int, int, int], tuple[int, int, int]] = {}
    if not standable[start]:
        return seen, parent, link_parent
    feet, used = _link_feet(links, (ni, nj)), set()
    seen[start] = True
    queue = deque([start])
    while queue:
        cell = queue.popleft()
        i, j, k = cell
        for code, (di, dj, dk) in enumerate(MOVES, start=1):
            a, b, c = i + di, j + dj, k + dk
            if 0 <= a < ni and 0 <= b < nj and min_k <= c < nk and standable[a, b, c] and not seen[a, b, c]:
                seen[a, b, c] = True
                parent[a, b, c] = code
                queue.append((a, b, c))
        n = feet.get((i, j))
        if n is not None and n not in used:
            used.add(n)
            hi, hj = links[n][1]
            levels = np.nonzero(standable[hi, hj])[0]
            if len(levels) and not seen[hi, hj, levels.max()]:
                top = (hi, hj, int(levels.max()))
                seen[top] = True
                parent[top] = LINK_CODE
                link_parent[top] = cell
                queue.append(top)
    return seen, parent, link_parent


def trace_path(parent: np.ndarray, link_parent: dict, start, end) -> list[tuple[int, int, int]]:
    """Celdas de start a end siguiendo los padres; ValueError si end no se alcanzo."""
    end = tuple(int(v) for v in end)
    if end != tuple(start) and parent[end] == 0:
        raise ValueError(f"{end} no se alcanza desde el inicio")
    path, cell = [end], end
    while parent[cell] != 0:
        if parent[cell] == LINK_CODE:
            cell = link_parent[cell]
        else:
            di, dj, dk = MOVES[parent[cell] - 1]
            cell = (cell[0] - di, cell[1] - dj, cell[2] - dk)
        path.append(cell)
    return path[::-1]
```

- [ ] **Paso 4: ver pasar.** PYTEST con `Scripts/tests/test_terrain_coop.py` → 4 passed; PYTEST completo sin regresiones (`test_terrain_volume.py` usa `walk` con la firma antigua).
- [ ] **Paso 5: commit.** `git add Scripts/gen_terrain_volume.py Scripts/terrain_vol/coop.py Scripts/tests/test_terrain_coop.py` · `git commit -m "feat(terreno): BFS con padres y camino ordenado de inicio a meta (P01)"`

### Tarea 10: Muestreo del camino, ancho, máscara permitida y `bake.bin`

**Files:** Modify `Scripts/terrain_vol/coop.py`, `Scripts/tests/test_terrain_coop.py`.

**Interfaces:**
- Consumes: `trace_path` (Tarea 9); `global_top`.
- Produces: `coop.ORIGIN_M`; `coop.cells_to_world(cells) -> np.ndarray` (n, 3) en m; `coop.resample_path(points_m, step_m=2.0) -> np.ndarray`; `coop.path_widths(seen, points_m, min_m=2.0, max_m=20.0) -> np.ndarray`; `coop.safe_levels(seen) -> tuple[np.ndarray, np.ndarray]` (uint8, 255 = sin nivel); `coop.path_distance_dm(points_m, shape) -> np.ndarray` (uint16); `coop.write_bake(path, top_m, low, high, path_dm) -> None`; `coop.read_bake(path) -> dict`; `NO_LEVEL = 255`, `FAR_PATH_DM = 65535`, `BAKE_HEADER = struct.Struct("<4sIiifffffII")`.

- [ ] **Paso 1: tests que fallan.** Añadir a `Scripts/tests/test_terrain_coop.py`:

```python
@pytest.fixture(scope="module")
def p01_path_m(p01, p01_walk):
    _, parent, link_parent = p01_walk
    return coop.resample_path(coop.cells_to_world(coop.trace_path(parent, link_parent, p01["start"], p01["end"])))


def test_resampled_path_steps_are_about_two_metres(p01_path_m):
    steps = np.linalg.norm(np.diff(p01_path_m[:, :2], axis=0), axis=1)
    assert steps.max() <= 3.5 and np.median(steps) == pytest.approx(2.0, abs=0.6)


def test_widths_are_clamped(p01_walk, p01_path_m):
    widths = coop.path_widths(p01_walk[0], p01_path_m)
    assert widths.min() >= 2.0 and widths.max() <= 20.0


def test_safe_levels_bracket_every_reached_cell(p01_walk):
    seen = p01_walk[0]
    low, high = coop.safe_levels(seen)
    i, j, k = np.nonzero(seen)
    assert np.all(low[i, j] <= k) and np.all(k <= high[i, j])
    assert np.all(low[~seen.any(axis=2)] == coop.NO_LEVEL)


def test_bake_round_trip(tmp_path, p01, p01_walk, p01_path_m):
    top = global_top(p01["chunks"], grid=P01_GRID)
    low, high = coop.safe_levels(p01_walk[0])
    dist = coop.path_distance_dm(p01_path_m, top.shape)
    path = tmp_path / "bake.bin"
    coop.write_bake(path, top, low, high, dist)
    raw = path.read_bytes()
    assert raw[:4] == b"TNCB" and coop.BAKE_HEADER.size == 44 and len(raw) > 44
    back = coop.read_bake(path)
    assert (back["nx"], back["ny"]) == top.shape
    assert np.array_equal(back["safe_low"], low) and np.array_equal(back["safe_high"], high)
    assert np.array_equal(back["path_dist_dm"], dist)
    assert np.abs(back["top_m"] - top).max() <= 0.006
```

- [ ] **Paso 2: ver el fallo.** PYTEST con el fichero → `AttributeError: module 'terrain_vol.coop' has no attribute 'resample_path'`.
- [ ] **Paso 3: implementación.** En `coop.py`, imports: `import struct`, `import zlib`, `from pathlib import Path`, `from scipy import ndimage`, y `from .layout import CELL_M, STEP_XY_M, STEP_Z_M, UU_PER_M, Z_MIN_M`. Añadir:

```python
NO_LEVEL = 255
FAR_PATH_DM = 65535
BAKE_MAGIC = b"TNCB"
BAKE_VERSION = 1
BAKE_HEADER = struct.Struct("<4sIiifffffII")        # 44 bytes (Global Constraints)
ORIGIN_M = -CELL_M / 2.0                            # la muestra (0, 0) de la rejilla, como world_index


def cells_to_world(cells) -> np.ndarray:
    arr = np.asarray(cells, dtype=np.float64)
    return np.column_stack((ORIGIN_M + arr[:, 0] * STEP_XY_M, ORIGIN_M + arr[:, 1] * STEP_XY_M,
                            Z_MIN_M + arr[:, 2] * STEP_Z_M))


def resample_path(points_m: np.ndarray, step_m: float = 2.0) -> np.ndarray:
    """Una muestra cada step_m de recorrido en planta, con la primera y la ultima."""
    seg = np.linalg.norm(np.diff(points_m[:, :2], axis=0), axis=1)
    arc = np.concatenate([[0.0], np.cumsum(seg)])
    idx = np.unique(np.concatenate([np.searchsorted(arc, np.arange(0.0, arc[-1], step_m)), [len(points_m) - 1]]))
    return points_m[np.clip(idx, 0, len(points_m) - 1)]


def indices_of(points_m: np.ndarray, shape) -> tuple[np.ndarray, np.ndarray]:
    i = np.clip(np.round((points_m[:, 0] - ORIGIN_M) / STEP_XY_M).astype(int), 0, shape[0] - 1)
    j = np.clip(np.round((points_m[:, 1] - ORIGIN_M) / STEP_XY_M).astype(int), 0, shape[1] - 1)
    return i, j


def path_widths(seen: np.ndarray, points_m: np.ndarray, min_m: float = 2.0, max_m: float = 20.0) -> np.ndarray:
    """Ancho = 2 x distancia a lo no alcanzable en planta (transformada de distancia de lo pisable)."""
    dt = ndimage.distance_transform_edt(seen.any(axis=2)) * STEP_XY_M
    i, j = indices_of(points_m, seen.shape)
    return np.clip(2.0 * dt[i, j], min_m, max_m)


def safe_levels(seen: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Nivel k mas bajo y mas alto alcanzable desde el inicio en cada columna (NO_LEVEL si ninguno)."""
    reached = seen.any(axis=2)
    nk = seen.shape[2]
    low = np.where(reached, seen.argmax(axis=2), NO_LEVEL).astype(np.uint8)
    high = np.where(reached, nk - 1 - seen[:, :, ::-1].argmax(axis=2), NO_LEVEL).astype(np.uint8)
    return low, high


def path_distance_dm(points_m: np.ndarray, shape) -> np.ndarray:
    mask = np.zeros(shape[:2], dtype=bool)
    i, j = indices_of(points_m, shape)
    mask[i, j] = True
    dist_m = ndimage.distance_transform_edt(~mask) * STEP_XY_M
    return np.clip(np.round(dist_m * 10.0), 0, FAR_PATH_DM).astype(np.uint16)


def write_bake(path: Path, top_m: np.ndarray, low: np.ndarray, high: np.ndarray, path_dm: np.ndarray) -> None:
    """bake.bin v1 (Global Constraints): capas en orden j * nx + i, zlib RFC1950."""
    nx, ny = top_m.shape
    z0_uu = Z_MIN_M * UU_PER_M
    top_rel = np.clip(np.round(top_m * UU_PER_M - z0_uu), 0, 65535)
    raw = b"".join(a.T.astype(t).tobytes() for a, t in ((top_rel, "<u2"), (low, "u1"), (high, "u1"), (path_dm, "<u2")))
    comp = zlib.compress(raw, 6)
    origin_uu = ORIGIN_M * UU_PER_M
    header = BAKE_HEADER.pack(BAKE_MAGIC, BAKE_VERSION, nx, ny, origin_uu, origin_uu, STEP_XY_M * UU_PER_M, z0_uu,
                              STEP_Z_M * UU_PER_M, len(raw), len(comp))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + comp)


def read_bake(path: Path) -> dict:
    data = path.read_bytes()
    magic, version, nx, ny, ox, oy, step, z0, zstep, raw_size, comp_size = BAKE_HEADER.unpack_from(data, 0)
    if magic != BAKE_MAGIC or version != BAKE_VERSION:
        raise ValueError(f"{path}: no es bake.bin v{BAKE_VERSION}")
    raw = zlib.decompress(data[BAKE_HEADER.size:BAKE_HEADER.size + comp_size])
    n = nx * ny
    if len(raw) != raw_size or raw_size != n * 6:
        raise ValueError(f"{path}: tamano de capas incoherente")

    def layer(offset: int, dtype: str, size: int) -> np.ndarray:
        return np.frombuffer(raw[offset:offset + size], dtype=dtype).reshape(ny, nx).T

    top_rel = layer(0, "<u2", 2 * n)
    return {"nx": nx, "ny": ny, "origin_uu": (ox, oy), "step_uu": step, "z0_uu": z0, "zstep_uu": zstep,
            "top_m": (top_rel.astype(np.float64) + z0) / UU_PER_M, "safe_low": layer(2 * n, "u1", n),
            "safe_high": layer(3 * n, "u1", n), "path_dist_dm": layer(4 * n, "<u2", 2 * n)}
```

- [ ] **Paso 4: ver pasar.** PYTEST con el fichero → 8 passed.
- [ ] **Paso 5: commit.** `git add Scripts/terrain_vol/coop.py Scripts/tests/test_terrain_coop.py` · `git commit -m "feat(terreno): máscara de niveles permitidos, ancho del camino y bake.bin v1"`

### Tarea 11: Bloque `coop` del manifest v2 y `gen_terrain_coop.py` (C01 y P01)

**Files:**
- Modify: `Scripts/terrain_vol/coop.py`, `Scripts/tests/test_terrain_coop.py`, `Scripts/gen_terrain_path.py:76-116`, `Scripts/gen_terrain_platforms.py:101-118`
- Create: `Scripts/gen_terrain_coop.py`

**Interfaces:**
- Consumes: Tareas 9–10; `zone_map` (`gen_terrain_volume.py:90`); `kill_boxes_uu` (P01 y C01).
- Produces: `coop.ZONE_BIOME: dict[str, str]`; `coop.CoopExtras` (dataclass: `map_id`, `kill_boxes_uu`, `jellyfish_uu`, `beach_elements`, `vegetation`, `swim`, `finish_in_water`, `bridges`); `coop.solve_walk(standable, start, min_z_m, links=()) -> tuple`; `coop.build_coop_block(out_dir, standable, start, end, top_m, zone_weights, extras, min_z_m, links=()) -> dict`; `coop.write_manifest_v2(out_dir, block) -> None`; `gen_terrain_path.solve(name, seed, style) -> tuple[PathModel, dict, int, dict]`; `gen_terrain_platforms.solve_platforms(layout_path, seed) -> tuple`; CLI `gen_terrain_coop.py --map {C01,P01,CP01}`.

Formato del bloque (uu de mundo; toda `pos` con la Z del suelo caminable):

```json
"manifest_version": 2,
"coop": {
  "map_id": "C01",
  "bounds_uu": [-5000, -5000, 35000, 35000],
  "path": {"step_uu": 200, "main": [[x, y, z, ancho]], "branches": []},
  "start": {"id": "start", "pos": [x, y, z], "yaw": 90.0},
  "finish": {"id": "finish", "pos": [x, y, z], "dir": [dx, dy], "width_uu": 1500, "depth_uu": 1200, "in_water": false},
  "nests": [{"id": "nest_01", "pos": [x, y, z], "order": 1}],
  "water": {"z_uu": -400, "swim": false},
  "zones": [{"name": "cliffs", "biome": "Rocky", "s0_uu": 0, "s1_uu": 12000}],
  "kill_boxes_uu": [{"center": [x, y, z], "extent": [ex, ey, ez], "yaw": 0.0}],
  "jellyfish": [{"id": "jelly_01", "pos": [x, y, z]}],
  "beach_elements": [{"id": "cat_01", "element": "Catapult", "pos": [x, y, z], "yaw": 90.0, "size": 1.0, "extent": 0.0}],
  "bridges": [],
  "vegetation": {"count": 1, "points": [{"element": "Rock", "pos": [x, y, z], "yaw": 0.0, "size": 1.0, "seed": 7}]},
  "bake": {"file": "coop/bake.bin", "origin_uu": [-5000, -5000], "step_uu": 100, "nx": 401, "ny": 401, "z0_uu": -1000,
           "zstep_uu": 50, "layers": ["top_rel_cm:u16", "safe_low:u8", "safe_high:u8", "path_dist_dm:u16"]}
}
```

`branches` va vacío en v2: el progreso de un lazo es el de la muestra de `main` más cercana (decisión de este plan; los lazos siguen siendo terreno permitido por la máscara).

- [ ] **Paso 1: tests que fallan.** Añadir a `test_terrain_coop.py`:

```python
@pytest.fixture(scope="module")
def p01_block(tmp_path_factory, p01):
    out = tmp_path_factory.mktemp("P01_plataformas")
    top = global_top(p01["chunks"], grid=P01_GRID)
    extras = coop.CoopExtras(map_id="P01")
    block = coop.build_coop_block(out, p01["standable"], p01["start"], p01["end"], top,
                                  zone_map(p01["model"], p01["chunks"], grid=P01_GRID), extras, WATER_M + 0.2)
    return out, block


def test_block_has_every_key(p01_block):
    _, block = p01_block
    for key in ("map_id", "bounds_uu", "path", "start", "finish", "nests", "water", "zones", "kill_boxes_uu",
                "jellyfish", "beach_elements", "bridges", "vegetation", "bake"):
        assert key in block, key


def test_finish_points_along_the_last_stretch(p01_block):
    _, block = p01_block
    main = np.asarray(block["path"]["main"])
    tail = main[-1, :2] - main[-6, :2]
    assert np.dot(tail / np.linalg.norm(tail), np.asarray(block["finish"]["dir"])) > 0.9


def test_nests_are_ordered(p01_block):
    _, block = p01_block
    assert [n["order"] for n in block["nests"]] == list(range(1, len(block["nests"]) + 1))


def test_zones_are_contiguous(p01_block):
    _, block = p01_block
    zones = block["zones"]
    assert zones[0]["s0_uu"] == 0
    assert all(a["s1_uu"] == b["s0_uu"] for a, b in zip(zones, zones[1:]))
    assert {z["biome"] for z in zones} <= set(coop.ZONE_BIOME.values())


def test_bake_file_matches_the_block(p01_block):
    out, block = p01_block
    back = coop.read_bake(out / block["bake"]["file"])
    assert (back["nx"], back["ny"]) == (block["bake"]["nx"], block["bake"]["ny"])


def test_write_manifest_v2_keeps_v1_keys(tmp_path, p01_block):
    _, block = p01_block
    (tmp_path / "manifest.json").write_text(json.dumps({"name": "P01", "cells": [], "water_uu": -400.0}), encoding="utf-8")
    coop.write_manifest_v2(tmp_path, block)
    manifest = json.loads((tmp_path / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["manifest_version"] == 2 and manifest["name"] == "P01" and manifest["coop"]["map_id"] == "P01"
```

- [ ] **Paso 2: ver el fallo.** PYTEST con el fichero → `AttributeError: ... 'CoopExtras'`.
- [ ] **Paso 3: implementación en `coop.py`.** Imports: `import json`, `from dataclasses import dataclass, field`, ampliar `from .layout import …, WATER_M`. Añadir:

```python
NEST_SPACING_M = 90.0
NEST_END_MARGIN_M = 40.0
PATH_STEP_M = 2.0
ZONE_BIOME = {"cliffs": "Rocky", "canyon": "Desert", "marsh": "Water", "algae": "Mangrove", "beach": "Beach"}


@dataclass
class CoopExtras:
    map_id: str
    kill_boxes_uu: list = field(default_factory=list)
    jellyfish_uu: list = field(default_factory=list)
    beach_elements: list = field(default_factory=list)
    vegetation: list = field(default_factory=list)
    swim: bool = False
    finish_in_water: bool = False
    bridges: list = field(default_factory=list)


def uu(p) -> list[float]:
    return [round(float(v) * UU_PER_M, 1) for v in p]


def arc_of(points_m: np.ndarray) -> np.ndarray:
    return np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(points_m[:, :2], axis=0), axis=1))])


def zones_along(points_m: np.ndarray, arc: np.ndarray, zone_weights: dict) -> list[dict]:
    names = list(zone_weights)
    i, j = indices_of(points_m, next(iter(zone_weights.values())).shape)
    per_sample = np.argmax(np.stack([zone_weights[n][i, j] for n in names]), axis=0)
    zones, begin = [], 0
    for s in range(1, len(points_m) + 1):
        if s == len(points_m) or per_sample[s] != per_sample[begin]:
            name = names[per_sample[begin]]
            zones.append({"name": name, "biome": ZONE_BIOME[name], "s0_uu": round(arc[begin] * UU_PER_M, 1),
                          "s1_uu": round(arc[min(s, len(arc) - 1)] * UU_PER_M, 1)})
            begin = s
    return zones


def yaw_of(d) -> float:
    return float(np.degrees(np.arctan2(d[1], d[0])))


def solve_walk(standable, start, min_z_m: float, links=()):
    """(seen, parent, link_parent) con la cota minima pisable del mapa."""
    return walk_with_parents(standable, start, min_level(min_z_m), links)


def main_path(parent, link_parent, start, end) -> np.ndarray:
    return resample_path(cells_to_world(trace_path(parent, link_parent, start, end)), PATH_STEP_M)


def build_coop_block(out_dir: Path, standable, start, end, top_m, zone_weights, extras: CoopExtras, min_z_m: float,
                     links=()) -> dict:
    seen, parent, link_parent = solve_walk(standable, start, min_z_m, links)
    points = main_path(parent, link_parent, start, end)
    widths, arc = path_widths(seen, points), arc_of(points)
    low, high = safe_levels(seen)
    write_bake(out_dir / "coop" / "bake.bin", top_m, low, high, path_distance_dm(points, top_m.shape))
    nx, ny = top_m.shape
    origin_uu, span_x, span_y = ORIGIN_M * UU_PER_M, (nx - 1) * STEP_XY_M * UU_PER_M, (ny - 1) * STEP_XY_M * UU_PER_M
    tail = points[-1, :2] - points[max(0, len(points) - 6), :2]
    head = points[min(5, len(points) - 1), :2] - points[0, :2]
    nests = [{"id": f"nest_{n + 1:02d}", "pos": uu(points[int(np.searchsorted(arc, s))]), "order": n + 1}
             for n, s in enumerate(np.arange(NEST_SPACING_M, arc[-1] - NEST_END_MARGIN_M, NEST_SPACING_M))]
    return {
        "map_id": extras.map_id,
        "bounds_uu": [origin_uu, origin_uu, origin_uu + span_x, origin_uu + span_y],
        "path": {"step_uu": PATH_STEP_M * UU_PER_M,
                 "main": [[*uu(p), round(float(w) * UU_PER_M, 1)] for p, w in zip(points, widths)], "branches": []},
        "start": {"id": "start", "pos": uu(points[0]), "yaw": yaw_of(head)},
        "finish": {"id": "finish", "pos": uu(points[-1]), "dir": [float(v) for v in tail / max(np.linalg.norm(tail), 1e-6)],
                   "width_uu": 1500.0, "depth_uu": 1200.0, "in_water": extras.finish_in_water},
        "nests": nests,
        "water": {"z_uu": WATER_M * UU_PER_M, "swim": extras.swim},
        "zones": zones_along(points, arc, zone_weights),
        "kill_boxes_uu": extras.kill_boxes_uu,
        "jellyfish": [{"id": f"jelly_{n + 1:02d}", "pos": p} for n, p in enumerate(extras.jellyfish_uu)],
        "beach_elements": extras.beach_elements,
        "bridges": extras.bridges,
        "vegetation": {"count": len(extras.vegetation), "points": extras.vegetation},
        "bake": {"file": "coop/bake.bin", "origin_uu": [origin_uu, origin_uu], "step_uu": STEP_XY_M * UU_PER_M,
                 "nx": nx, "ny": ny, "z0_uu": Z_MIN_M * UU_PER_M, "zstep_uu": STEP_Z_M * UU_PER_M,
                 "layers": ["top_rel_cm:u16", "safe_low:u8", "safe_high:u8", "path_dist_dm:u16"]},
    }


def write_manifest_v2(out_dir: Path, block: dict) -> None:
    path = out_dir / "manifest.json"
    manifest = json.loads(path.read_text(encoding="utf-8"))
    manifest["manifest_version"] = 2
    manifest["coop"] = block
    path.write_text(json.dumps(manifest, indent=1), encoding="utf-8")
```

- [ ] **Paso 4: extraer `solve` de los generadores.** En `Scripts/gen_terrain_path.py`, sustituir las líneas 78-93 de `build_one` (el bucle de semillas y el `raise`) por una función de módulo, y en `build_one` llamar `model, chunks, used, result = solve(name, seed, style)` tras `t0 = time.time()`:

```python
def solve(name: str, seed: int, style: PathStyle) -> tuple[PathModel, dict, int, dict]:
    """Primer modelo recorrible con seed, seed + RESEED_STEP, ... (MAX_TRIES): (modelo, trozos, semilla, check)."""
    model, chunks, result, used = None, None, {"ok": False}, seed
    for attempt in range(MAX_TRIES):
        used = seed + attempt * RESEED_STEP
        try:
            model = PathModel(used, style)
        except RuntimeError as exc:                 # sin camino principal valido con esta semilla
            print(f"{name}: semilla {used} descartada ({exc})", flush=True)
            continue
        chunks = build_all(model, grid=GRID)
        result = check(model, chunks)
        if result["ok"]:
            break
    if model is None:
        raise RuntimeError(f"ninguna de las {MAX_TRIES} semillas da un camino principal valido")
    return model, chunks, used, result
```

  En `Scripts/gen_terrain_platforms.py`, nueva función y `build` la usa en lugar de sus cuatro primeras líneas tras `t0`:

```python
def solve_platforms(layout_path: Path, seed: int):
    """(modelo, trozos, inicio, final, check) del mapa de plataformas."""
    model = PlatformModel(seed, load_layout(layout_path))
    chunks = build_all(model, grid=GRID)
    start_xy, end_xy = pick_start_end(model.layout)
    return model, chunks, start_xy, end_xy, check(model, chunks, start_xy, end_xy)
```

  `build` queda: `model, chunks, start_xy, end_xy, result = solve_platforms(layout_path, seed)` y sigue en `top = global_top(...)`.
- [ ] **Paso 5: CLI.** Crear `Scripts/gen_terrain_coop.py`:

```python
"""Anade el bloque Coop (manifest v2 + coop/bake.bin) a un mapa fijo de Scripts/terrain_volumes/Variants.

    uv run --with numpy --with scipy --with pillow --with scikit-image --with pyfqmr python Scripts/gen_terrain_coop.py --map C01
    ... --map P01
    ... --map CP01        (Tarea 32: genera el terreno de CP01, lo decima y escribe todo)
"""

from __future__ import annotations

import argparse
from pathlib import Path

from gen_terrain_volume import global_standable, ground_level, world_index, zone_map
from terrain_vol import coop
from terrain_vol.export import global_top
from terrain_vol.layout import UU_PER_M, WATER_M

VARIANTS = Path(__file__).resolve().parent / "terrain_volumes" / "Variants"
WADE_M = 0.8


def cell_of(standable, xy) -> tuple[int, int, int]:
    i, j = world_index(xy)
    return i, j, ground_level(standable, i, j)


def run_c01() -> dict:
    import gen_terrain_path as gp
    from terrain_path.canyon import kill_boxes_uu
    from terrain_path.layout import GRID
    from terrain_path.model import walkable
    from terrain_path.style import C01_SEED, C01_STYLE
    from terrain_vol.mesh import z_levels
    model, chunks, _, _ = gp.solve("C01_camino", C01_SEED, C01_STYLE)
    standable = walkable(global_standable(chunks, grid=GRID), model.grid.height[1:-1, 1:-1], z_levels())
    standable = model.remove_deadly(standable, z_levels())
    links = [(world_index(link[0]), world_index(link[1])) for link in model.jump_links()]
    extras = coop.CoopExtras(map_id="C01", kill_boxes_uu=[b for c in model.canyons for b in kill_boxes_uu(c)],
                             jellyfish_uu=[[round(float(c) * UU_PER_M, 1) for c in st.jelly] for st in model.jump_steps],
                             swim=True, finish_in_water=True)
    out = VARIANTS / "C01_camino"
    block = coop.build_coop_block(out, standable, cell_of(standable, model.start), cell_of(standable, model.end),
                                  global_top(chunks, grid=GRID), zone_map(model, chunks, grid=GRID), extras,
                                  WATER_M - WADE_M, links)
    coop.write_manifest_v2(out, block)
    return block


def run_p01() -> dict:
    import gen_terrain_platforms as gpl
    from terrain_platforms.layout import GRID, LAYOUT_FILE
    from terrain_platforms.model import SEED
    model, chunks, start_xy, end_xy, _ = gpl.solve_platforms(LAYOUT_FILE, SEED)
    standable = global_standable(chunks, grid=GRID)
    extras = coop.CoopExtras(map_id="P01", kill_boxes_uu=gpl.kill_boxes_uu())
    out = VARIANTS / "P01_plataformas"
    block = coop.build_coop_block(out, standable, cell_of(standable, start_xy), cell_of(standable, end_xy),
                                  global_top(chunks, grid=GRID), zone_map(model, chunks, grid=GRID), extras,
                                  WATER_M + 0.2)
    coop.write_manifest_v2(out, block)
    return block


def main() -> None:
    parser = argparse.ArgumentParser(description="Bloque Coop (manifest v2) de un mapa fijo.")
    parser.add_argument("--map", choices=("C01", "P01", "CP01"), required=True)
    args = parser.parse_args()
    if args.map == "CP01":
        from terrain_coop.cp01 import build_cp01
        block = build_cp01(VARIANTS)
    else:
        block = run_c01() if args.map == "C01" else run_p01()
    print(f"{block['map_id']}: camino {len(block['path']['main'])} muestras, {len(block['nests'])} nidos, "
          f"{len(block['zones'])} zonas, {len(block['beach_elements'])} mecanicas, {block['vegetation']['count']} plantas")


if __name__ == "__main__":
    main()
```

  (`walkable`, `remove_deadly`, `jump_links`, `jump_steps` y `canyons` son los que ya usa `gen_terrain_path.check`/`build_one`; `z_levels` está en `terrain_vol/mesh.py:59`.)
- [ ] **Paso 6: ver pasar y generar.** PYTEST con el fichero → 14 passed. `uv run --with numpy --with scipy --with pillow --with scikit-image python Scripts/gen_terrain_coop.py --map C01` y `--map P01` → imprimen muestras > 0; existen `Scripts/terrain_volumes/Variants/C01_camino/coop/bake.bin` y `.../P01_plataformas/coop/bake.bin`. PYTEST completo verde (incluidos `test_terrain_path.py` y `test_terrain_platforms.py`, que ejercitan `build_one`/`build` refactorizados).
- [ ] **Paso 7: commit.** `git add Scripts/terrain_vol/coop.py Scripts/tests/test_terrain_coop.py Scripts/gen_terrain_path.py Scripts/gen_terrain_platforms.py Scripts/gen_terrain_coop.py Scripts/terrain_volumes/Variants/C01_camino/manifest.json Scripts/terrain_volumes/Variants/C01_camino/coop Scripts/terrain_volumes/Variants/P01_plataformas/manifest.json Scripts/terrain_volumes/Variants/P01_plataformas/coop` · `git commit -m "feat(terreno): manifest v2 con bloque coop y bake para C01 y P01"`

### Tarea 12: Presupuesto de malla (decimado cuádrico con borde fijo)

**Files:**
- Create: `Scripts/terrain_vol/decimate.py`, `Scripts/tests/test_terrain_decimate.py`
- Modify: `pyproject.toml`

**Interfaces:**
- Consumes: `ChunkMesh` (`terrain_vol/mesh.py:45`, `@dataclass`).
- Produces: `decimate.MAX_ERROR_UU = 15.0`; `decimate.BUDGET_TRIS = 1_100_000`; `decimate.point_triangle_distance(p, a, b, c) -> np.ndarray`; `decimate.max_deviation(orig_vertices, vertices, faces) -> float`; `decimate.decimate_chunk(chunk, target_ratio, max_error_uu=MAX_ERROR_UU) -> ChunkMesh`; `decimate.fit_budget(chunks: dict, budget=BUDGET_TRIS) -> tuple[dict, dict]` (informe `{"before", "after", "worst_error_uu"}`).

- [ ] **Paso 1: tests que fallan.** `Scripts/tests/test_terrain_decimate.py`:

```python
"""Decimado del terreno volumetrico (plan maestro §2.5).

    uv run --with numpy --with scipy --with pillow --with scikit-image --with pytest --with pyfqmr python -m pytest Scripts/tests/test_terrain_decimate.py -q
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from terrain_vol import decimate  # noqa: E402
from terrain_vol.mesh import ChunkMesh  # noqa: E402


def grid_chunk(n: int = 40, bump: float = 30.0) -> ChunkMesh:
    xs, ys = np.meshgrid(np.linspace(-5000, 5000, n), np.linspace(-5000, 5000, n), indexing="ij")
    zs = bump * np.exp(-((xs / 2500.0) ** 2 + (ys / 2500.0) ** 2))
    verts = np.column_stack([xs.ravel(), ys.ravel(), zs.ravel()]).astype(np.float32)
    faces = []
    for i in range(n - 1):
        for j in range(n - 1):
            a, b, c, d = i * n + j, (i + 1) * n + j, i * n + j + 1, (i + 1) * n + j + 1
            faces += [(a, c, b), (b, c, d)]
    normals = np.tile(np.array([0, 0, 1], np.float32), (len(verts), 1))
    return ChunkMesh(0, 0, verts, normals, np.full((len(verts), 4), 200, np.uint8), np.asarray(faces, np.uint32),
                     np.zeros((0, 11), np.float32), np.zeros((2, 2)), np.zeros((2, 2, 2), bool), None)


def boundary_positions(verts: np.ndarray, faces: np.ndarray) -> set:
    edges = np.sort(np.concatenate([faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]]]), axis=1)
    uniq, counts = np.unique(edges, axis=0, return_counts=True)
    return {tuple(np.round(verts[i], 2)) for i in np.unique(uniq[counts == 1])}


def test_point_triangle_distance_inside_and_outside():
    a, b, c = np.array([[0, 0, 0.]]), np.array([[10, 0, 0.]]), np.array([[0, 10, 0.]])
    assert decimate.point_triangle_distance(np.array([[2, 2, 5.]]), a, b, c)[0] == 5.0
    assert np.isclose(decimate.point_triangle_distance(np.array([[-3, 0, 4.]]), a, b, c)[0], 5.0)


def test_decimation_halves_a_smooth_chunk_within_tolerance():
    chunk = grid_chunk()
    out = decimate.decimate_chunk(chunk, 0.3)
    assert len(out.triangles) < 0.5 * len(chunk.triangles)
    assert decimate.max_deviation(chunk.vertices, out.vertices, out.triangles) <= decimate.MAX_ERROR_UU


def test_decimation_keeps_the_chunk_border():
    chunk = grid_chunk()
    out = decimate.decimate_chunk(chunk, 0.3)
    assert boundary_positions(chunk.vertices, chunk.triangles) <= {tuple(np.round(v, 2)) for v in out.vertices}


def test_fit_budget_reports_and_respects_budget():
    chunks = {(0, 0): grid_chunk(), (1, 0): grid_chunk()}
    total = sum(len(c.triangles) for c in chunks.values())
    _, report = decimate.fit_budget(chunks, budget=total // 2)
    assert report["before"] == total and report["after"] <= total // 2
    assert report["worst_error_uu"] <= decimate.MAX_ERROR_UU
```

- [ ] **Paso 2: ver el fallo.** `uv run --with numpy --with scipy --with pillow --with scikit-image --with pytest --with pyfqmr python -m pytest Scripts/tests/test_terrain_decimate.py -q` → `ImportError`.
- [ ] **Paso 3: implementación.** `pyproject.toml`: `dependencies = ["numpy", "scipy", "pillow", "scikit-image", "pytest", "pyfqmr"]`. Crear `Scripts/terrain_vol/decimate.py`:

```python
"""Presupuesto de malla del terreno volumetrico (plan maestro §2.5): decimado cuadrico por trozo (pyfqmr) antes de
TNTM2, con el borde del trozo fijo (la costura con el vecino sigue exacta) y un tope de error medido de verdad
(distancia punto-triangulo de cada vertice original a la malla decimada)."""

from __future__ import annotations

from dataclasses import replace

import numpy as np
import pyfqmr
from scipy.spatial import cKDTree

from .mesh import ChunkMesh

MAX_ERROR_UU = 15.0
BUDGET_TRIS = 1_100_000
CANDIDATES = 16
RATIO_STEP = 0.1


def _segment_distance(p, a, b):
    ab = b - a
    t = np.clip(((p - a) * ab).sum(1) / np.maximum((ab * ab).sum(1), 1e-12), 0.0, 1.0)
    return np.linalg.norm(p - (a + t[:, None] * ab), axis=1)


def point_triangle_distance(p, a, b, c):
    ab, ac = b - a, c - a
    n = np.cross(ab, ac)
    nn = np.maximum((n * n).sum(1), 1e-12)
    d = ((p - a) * n).sum(1) / nn
    v2 = p - d[:, None] * n - a
    d00, d01, d11 = (ab * ab).sum(1), (ab * ac).sum(1), (ac * ac).sum(1)
    d20, d21 = (v2 * ab).sum(1), (v2 * ac).sum(1)
    den = np.maximum(d00 * d11 - d01 * d01, 1e-12)
    v = (d11 * d20 - d01 * d21) / den
    w = (d00 * d21 - d01 * d20) / den
    inside = (v >= 0) & (w >= 0) & (v + w <= 1)
    plane = np.abs(d) * np.sqrt(nn)
    edge = np.minimum(np.minimum(_segment_distance(p, a, b), _segment_distance(p, b, c)), _segment_distance(p, c, a))
    return np.where(inside, plane, edge)


def max_deviation(orig_vertices, vertices, faces) -> float:
    if len(faces) == 0 or len(orig_vertices) == 0:
        return 0.0
    tri = np.asarray(vertices, np.float64)[np.asarray(faces, np.int64)]
    k = min(CANDIDATES, len(tri))
    orig = np.asarray(orig_vertices, np.float64)
    _, idx = cKDTree(tri.mean(axis=1)).query(orig, k=k)
    idx = np.asarray(idx).reshape(len(orig), k)
    t = tri[idx.ravel()]
    dist = point_triangle_distance(np.repeat(orig, k, axis=0), t[:, 0], t[:, 1], t[:, 2]).reshape(-1, k).min(axis=1)
    return float(dist.max())


def _vertex_normals(v, f):
    fn = np.cross(v[f[:, 1]] - v[f[:, 0]], v[f[:, 2]] - v[f[:, 0]])
    n = np.zeros_like(v)
    for corner in range(3):
        np.add.at(n, f[:, corner], fn)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)


def _rebuild(chunk: ChunkMesh, verts: np.ndarray, faces: np.ndarray) -> ChunkMesh:
    _, nearest = cKDTree(chunk.vertices.astype(np.float64)).query(verts)
    normals = _vertex_normals(verts, faces)
    if (normals * chunk.normals[nearest]).sum(1).mean() < 0.0:
        normals = -normals
    return replace(chunk, vertices=verts.astype(np.float32), normals=normals.astype(np.float32),
                   colors=chunk.colors[nearest], triangles=faces.astype(np.uint32))


def decimate_chunk(chunk: ChunkMesh, target_ratio: float, max_error_uu: float = MAX_ERROR_UU) -> ChunkMesh:
    """El trozo con ~target_ratio de sus triangulos; si el error pasa de max_error_uu, sube el ratio de 0,1 en 0,1
    y, si ni asi, devuelve el original."""
    if len(chunk.triangles) < 8:
        return chunk
    verts, faces = chunk.vertices.astype(np.float64), chunk.triangles.astype(np.int32)
    ratio = max(0.02, target_ratio)
    while ratio < 1.0:
        simplifier = pyfqmr.Simplify()
        simplifier.setMesh(verts, faces)
        simplifier.simplify_mesh(target_count=max(4, int(len(faces) * ratio)), aggressiveness=7,
                                 preserve_border=True, verbose=False)
        out = simplifier.getMesh()
        nv, nf = np.asarray(out[0], np.float64), np.asarray(out[1], np.int64)
        if len(nf) and max_deviation(verts, nv, nf) <= max_error_uu:
            return _rebuild(chunk, nv, nf)
        ratio += RATIO_STEP
    return chunk


def fit_budget(chunks: dict, budget: int = BUDGET_TRIS) -> tuple[dict, dict]:
    before = sum(len(c.triangles) for c in chunks.values())
    if before <= budget:
        return chunks, {"before": before, "after": before, "worst_error_uu": 0.0}
    ratio = 0.97 * budget / before
    out = {key: decimate_chunk(chunk, ratio) for key, chunk in chunks.items()}
    worst = max(max_deviation(chunks[k].vertices, out[k].vertices, out[k].triangles) for k in chunks)
    return out, {"before": before, "after": sum(len(c.triangles) for c in out.values()), "worst_error_uu": worst}
```

- [ ] **Paso 4: ver pasar.** Comando del Paso 2 → 4 passed; PYTEST completo con `--with pyfqmr` verde.
- [ ] **Paso 5: commit.** `git add pyproject.toml Scripts/terrain_vol/decimate.py Scripts/tests/test_terrain_decimate.py` · `git commit -m "feat(terreno): decimado por trozo con borde fijo y tope de error de 15 cm"`

### Tarea 13: Rejilla horneada en C++ (`ParseBake`, `IsAllowed`, suelo en túnel)

**Files:**
- Create: `Source/Tortunabo/Public/World/ProcMap/TN_CoopGridDecisions.h`, `Source/Tortunabo/Private/Tests/TN_CoopTestUtil.h`, `Source/Tortunabo/Private/Tests/TN_CoopGridDecisionsTest.cpp`

**Interfaces:**
- Consumes: formato `bake.bin` v1 (Tarea 10).
- Produces (namespace `TNCoopMap`): `BakeHeaderBytes = 44`, `BakeVersion = 1`, `NoLevel = 255`, `FarPathDm = 65535`, `TraceAboveHint = 200.0`, `BelowFloorMargin = 200.0`; `FName TerrainTag()` (`TN_MapTerrain`); `struct FSafeGrid { FVector2D Origin; double Step; int32 NX, NY; double Z0, ZStep; TArray<uint16> TopRel; TArray<uint8> SafeLow, SafeHigh; TArray<uint16> PathDistDm; bool IsValid() const; int32 Index(int32, int32) const; bool CellOf(const FVector2D&, int32&, int32&) const; double LevelZ(int32) const; int32 LevelOf(double) const; double TopZAt(int32, int32) const; FVector2D Max() const; }`; `bool ParseBake(TArrayView<const uint8>, FSafeGrid&, FString&)`; `bool IsInBounds(const FSafeGrid&, const FVector& Feet)`; `bool IsAllowed(const FSafeGrid&, const FVector& Feet)`; `struct FTraceWindow { double Top; double Bottom; bool bHasLevels; }`; `FTraceWindow WalkableTraceWindow(const FSafeGrid&, const FVector2D&, double ZHint)`; `double FallbackWalkableZ(const FSafeGrid&, const FVector2D&, double ZHint)`; `void ComputeSeaDistance(const FSafeGrid&, double WaterZ, TArray<uint16>& OutDm)`; `float SeaNearFromDistance(uint16 Dm)`. Test util: `TNCoopTestUtil::MakeGrid(int32 NX, int32 NY, uint8 Level, FVector2D Origin)`, `TNCoopTestUtil::MakeBakeBytes(const TNCoopMap::FSafeGrid&)`, `TNCoopTestUtil::MakeManifestJson()`.

- [ ] **Paso 1: utilidades de test.** Crear `Source/Tortunabo/Private/Tests/TN_CoopTestUtil.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Misc/Compression.h"
#include "World/ProcMap/TN_CoopGridDecisions.h"

/** Datos de prueba del mapa preparado: rejilla, bytes de bake.bin v1 y un manifest v2 mínimo (21 × 21 m, suelo a Z = 0). */
namespace TNCoopTestUtil
{
	template <typename T>
	void AppendRaw(TArray<uint8>& Bytes, const T& Value)
	{
		Bytes.Append(reinterpret_cast<const uint8*>(&Value), sizeof(T));
	}

	/** Suelo en el nivel Level (Z = z0 + Level * zstep) en todas las columnas, camino a 0 dm. */
	inline TNCoopMap::FSafeGrid MakeGrid(int32 NX, int32 NY, uint8 Level, const FVector2D& Origin = FVector2D::ZeroVector)
	{
		TNCoopMap::FSafeGrid Grid;
		Grid.Origin = Origin;
		Grid.NX = NX;
		Grid.NY = NY;
		const int32 N = NX * NY;
		Grid.TopRel.Init(static_cast<uint16>(Level * 50), N);
		Grid.SafeLow.Init(Level, N);
		Grid.SafeHigh.Init(Level, N);
		Grid.PathDistDm.Init(0, N);
		return Grid;
	}

	inline TArray<uint8> MakeBakeBytes(const TNCoopMap::FSafeGrid& Grid)
	{
		TArray<uint8> Raw;
		for (const uint16 V : Grid.TopRel) { AppendRaw(Raw, V); }
		Raw.Append(Grid.SafeLow);
		Raw.Append(Grid.SafeHigh);
		for (const uint16 V : Grid.PathDistDm) { AppendRaw(Raw, V); }
		const int32 Bound = FCompression::CompressMemoryBound(NAME_Zlib, Raw.Num());
		TArray<uint8> Compressed;
		Compressed.SetNumUninitialized(Bound);
		int32 CompressedSize = Bound;
		const bool bCompressed = FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedSize, Raw.GetData(), Raw.Num());
		check(bCompressed);
		Compressed.SetNum(CompressedSize);
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>("TNCB"), 4);
		AppendRaw(Bytes, TNCoopMap::BakeVersion);
		AppendRaw(Bytes, Grid.NX);
		AppendRaw(Bytes, Grid.NY);
		AppendRaw(Bytes, static_cast<float>(Grid.Origin.X));
		AppendRaw(Bytes, static_cast<float>(Grid.Origin.Y));
		AppendRaw(Bytes, static_cast<float>(Grid.Step));
		AppendRaw(Bytes, static_cast<float>(Grid.Z0));
		AppendRaw(Bytes, static_cast<float>(Grid.ZStep));
		AppendRaw(Bytes, static_cast<uint32>(Raw.Num()));
		AppendRaw(Bytes, static_cast<uint32>(Compressed.Num()));
		Bytes.Append(Compressed);
		return Bytes;
	}

	/** Manifest v2 mínimo del mapa de prueba T01 (camino recto a lo largo de X en Y = 1000). */
	inline FString MakeManifestJson()
	{
		return TEXT(R"JSON({"name":"T01","water_uu":-400.0,"manifest_version":2,"coop":{"map_id":"T01","bounds_uu":[0,0,2000,2000],
"path":{"step_uu":200,"main":[[100,1000,0,600],[1000,1000,0,600],[1900,1000,0,600]],"branches":[]},
"start":{"id":"start","pos":[100,1000,0],"yaw":0},
"finish":{"id":"finish","pos":[1900,1000,0],"dir":[1,0],"width_uu":1500,"depth_uu":1200,"in_water":false},
"nests":[{"id":"nest_01","pos":[1000,1000,0],"order":1}],
"water":{"z_uu":-400,"swim":false},
"zones":[{"name":"beach","biome":"Beach","s0_uu":0,"s1_uu":1800}],
"kill_boxes_uu":[{"center":[1000,300,-425],"extent":[1000,200,225],"yaw":0}],
"jellyfish":[{"id":"jelly_01","pos":[500,1200,0]}],
"beach_elements":[{"id":"cat_01","element":"Catapult","pos":[700,1000,0],"yaw":0,"size":1.0,"extent":0}],
"bridges":[],
"vegetation":{"count":1,"points":[{"element":"Rock","pos":[400,1500,0],"yaw":30,"size":1.2,"seed":7}]},
"bake":{"file":"coop/bake.bin","origin_uu":[0,0],"step_uu":100,"nx":21,"ny":21,"z0_uu":-1000,"zstep_uu":50,"layers":[]}}})JSON");
	}
}
```

- [ ] **Paso 2: test que falla.** `Source/Tortunabo/Private/Tests/TN_CoopGridDecisionsTest.cpp`:

```cpp
// Rejilla horneada del mapa preparado (TN_CoopGridDecisions.h).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Coop; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Tests/TN_CoopTestUtil.h"
#include "World/ProcMap/TN_CoopGridDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopBakeTest, "Tortunabo.Coop.Bake",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopBakeTest::RunTest(const FString& Parameters)
{
	TNCoopMap::FSafeGrid Source = TNCoopTestUtil::MakeGrid(5, 4, 20, FVector2D(-5000.0, -5000.0));
	Source.SafeHigh[Source.Index(2, 3)] = 30;
	Source.PathDistDm[Source.Index(4, 0)] = 1234;
	TNCoopMap::FSafeGrid Read;
	FString Error;
	TestTrue(TEXT("Lee bake.bin v1"), TNCoopMap::ParseBake(TNCoopTestUtil::MakeBakeBytes(Source), Read, Error));
	TestEqual(TEXT("NX"), Read.NX, 5);
	TestEqual(TEXT("NY"), Read.NY, 4);
	TestEqual(TEXT("Origen X"), Read.Origin.X, -5000.0);
	TestEqual(TEXT("Nivel alto de (2, 3): índice j * nx + i"), Read.SafeHigh[Read.Index(2, 3)], static_cast<uint8>(30));
	TestEqual(TEXT("Distancia al camino de (4, 0)"), Read.PathDistDm[Read.Index(4, 0)], static_cast<uint16>(1234));
	TArray<uint8> Bad = TNCoopTestUtil::MakeBakeBytes(Source);
	Bad[0] = 'X';
	TestFalse(TEXT("Rechaza otra cabecera"), TNCoopMap::ParseBake(Bad, Read, Error));
	TArray<uint8> Short = TNCoopTestUtil::MakeBakeBytes(Source);
	Short.SetNum(30);
	TestFalse(TEXT("Rechaza un fichero cortado"), TNCoopMap::ParseBake(Short, Read, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopAllowedTest, "Tortunabo.Coop.Allowed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopAllowedTest::RunTest(const FString& Parameters)
{
	TNCoopMap::FSafeGrid Grid = TNCoopTestUtil::MakeGrid(21, 21, 20);           // suelo a Z = -1000 + 20 * 50 = 0
	Grid.SafeLow[Grid.Index(5, 5)] = TNCoopMap::NoLevel;
	Grid.SafeHigh[Grid.Index(5, 5)] = TNCoopMap::NoLevel;
	TestTrue(TEXT("En el suelo permitido"), TNCoopMap::IsAllowed(Grid, FVector(1000.0, 1000.0, 0.0)));
	TestTrue(TEXT("Un nivel por encima (safe_high + 1)"), TNCoopMap::IsAllowed(Grid, FVector(1000.0, 1000.0, 50.0)));
	TestFalse(TEXT("Dos niveles por encima"), TNCoopMap::IsAllowed(Grid, FVector(1000.0, 1000.0, 100.0)));
	TestFalse(TEXT("Un nivel por debajo (pozo)"), TNCoopMap::IsAllowed(Grid, FVector(1000.0, 1000.0, -50.0)));
	TestFalse(TEXT("Columna inalcanzable"), TNCoopMap::IsAllowed(Grid, FVector(500.0, 500.0, 0.0)));
	TestFalse(TEXT("Fuera de la rejilla"), TNCoopMap::IsAllowed(Grid, FVector(2500.0, 1000.0, 0.0)));
	TestTrue(TEXT("Dentro de límites"), TNCoopMap::IsInBounds(Grid, FVector(1000.0, 1000.0, -1100.0)));
	TestFalse(TEXT("Bajo el suelo del volumen"), TNCoopMap::IsInBounds(Grid, FVector(1000.0, 1000.0, -1300.0)));
	TestFalse(TEXT("Fuera en X"), TNCoopMap::IsInBounds(Grid, FVector(-10.0, 1000.0, 0.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopWalkableTunnelTest, "Tortunabo.Coop.WalkableTunnel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopWalkableTunnelTest::RunTest(const FString& Parameters)
{
	TNCoopMap::FSafeGrid Grid = TNCoopTestUtil::MakeGrid(21, 21, 20);
	Grid.TopRel[Grid.Index(10, 10)] = 2500;                                        // techo del túnel a Z = 1500
	const TNCoopMap::FTraceWindow Window = TNCoopMap::WalkableTraceWindow(Grid, FVector2D(1000.0, 1000.0), 50.0);
	TestTrue(TEXT("La columna tiene niveles"), Window.bHasLevels);
	TestTrue(TEXT("La traza empieza por debajo del techo"), Window.Top < 1500.0);
	TestEqual(TEXT("Empieza 1 m sobre el nivel más alto permitido"), Window.Top, 100.0);
	TestEqual(TEXT("Acaba 1 m bajo el más bajo"), Window.Bottom, -100.0);
	TestEqual(TEXT("Sin colisión, el suelo del túnel"), TNCoopMap::FallbackWalkableZ(Grid, FVector2D(1000.0, 1000.0), 1400.0), 0.0);
	const TNCoopMap::FTraceWindow Deep = TNCoopMap::WalkableTraceWindow(Grid, FVector2D(1000.0, 1000.0), -900.0);
	TestTrue(TEXT("Referencia muy baja: todo el rango permitido"), Deep.Top >= Deep.Bottom);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopSeaDistanceTest, "Tortunabo.Coop.SeaDistance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopSeaDistanceTest::RunTest(const FString& Parameters)
{
	TNCoopMap::FSafeGrid Grid = TNCoopTestUtil::MakeGrid(21, 21, 20);
	for (int32 J = 0; J < 21; ++J)
	{
		for (int32 I = 15; I < 21; ++I) { Grid.TopRel[Grid.Index(I, J)] = 0; }   // mar abierto al norte (Z = -1000)
	}
	Grid.TopRel[Grid.Index(5, 5)] = 0;                                             // charca interior: no es mar
	TArray<uint16> Dm;
	TNCoopMap::ComputeSeaDistance(Grid, -400.0, Dm);
	TestEqual(TEXT("En el mar"), Dm[Grid.Index(17, 3)], static_cast<uint16>(0));
	TestEqual(TEXT("A 5 m de la orilla"), Dm[Grid.Index(10, 3)], static_cast<uint16>(50));
	TestTrue(TEXT("La charca no cuenta como mar"), Dm[Grid.Index(5, 5)] >= 90);
	TestEqual(TEXT("SeaNear en el mar"), TNCoopMap::SeaNearFromDistance(0), 1.f);
	TestEqual(TEXT("SeaNear a 120 m"), TNCoopMap::SeaNearFromDistance(1200), 0.f);
	return true;
}

#endif
```

- [ ] **Paso 3: ver el fallo.** BUILD → no compila (`TN_CoopGridDecisions.h` no existe).
- [ ] **Paso 4: implementación.** Crear `Source/Tortunabo/Public/World/ProcMap/TN_CoopGridDecisions.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Misc/Compression.h"

/**
 * Rejilla horneada de un mapa preparado (bake.bin v1, Scripts/terrain_vol/coop.py) como lógica PURA: lectura, zonas
 * permitidas, ventana de traza del suelo caminable (túneles y voladizos) y distancia al mar.
 * Cabecera (little endian, 44 bytes): char[4] 'TNCB', uint32 versión (1), int32 nx, int32 ny, float32 origin_x_uu,
 * float32 origin_y_uu, float32 step_uu, float32 z0_uu, float32 zstep_uu, uint32 raw_size, uint32 zlib_size.
 * Capas (zlib RFC1950, índice j * nx + i con i a lo largo de X): uint16 top_rel_cm (cara más alta − z0), uint8 safe_low,
 * uint8 safe_high (niveles k alcanzables desde la salida; 255 = ninguno), uint16 path_dist_dm.
 * Todo en espacio de MUNDO: el mapa no se gira ni se escala (Global Constraints del plan F0–F2).
 */
namespace TNCoopMap
{
	inline constexpr int32 BakeHeaderBytes = 44;
	inline constexpr uint32 BakeVersion = 1;
	inline constexpr uint8 NoLevel = 255;
	inline constexpr uint16 FarPathDm = 65535;
	/** La traza del suelo empieza como mucho 2 m sobre la cota de referencia: desde +30 m daría el techo del túnel (R4). */
	inline constexpr double TraceAboveHint = 200.0;
	/** Por debajo de z0 − esto: caída sin fondo, fuera de límites. */
	inline constexpr double BelowFloorMargin = 200.0;
	/** Profundidad a partir de la cual una celda por debajo del agua cuenta como mar. */
	inline constexpr double SeaDepth = 50.0;

	/** Etiqueta de los trozos del terreno (Scripts/import_terrain_coop.py). */
	inline FName TerrainTag() { return FName(TEXT("TN_MapTerrain")); }

	struct FSafeGrid
	{
		FVector2D Origin = FVector2D::ZeroVector;
		double Step = 100.0;
		int32 NX = 0;
		int32 NY = 0;
		double Z0 = -1000.0;
		double ZStep = 50.0;
		TArray<uint16> TopRel;
		TArray<uint8> SafeLow;
		TArray<uint8> SafeHigh;
		TArray<uint16> PathDistDm;

		bool IsValid() const
		{
			const int32 N = NX * NY;
			return NX > 1 && NY > 1 && TopRel.Num() == N && SafeLow.Num() == N && SafeHigh.Num() == N && PathDistDm.Num() == N;
		}
		int32 Index(int32 I, int32 J) const { return J * NX + I; }
		bool CellOf(const FVector2D& XY, int32& OutI, int32& OutJ) const
		{
			OutI = FMath::RoundToInt32((XY.X - Origin.X) / Step);
			OutJ = FMath::RoundToInt32((XY.Y - Origin.Y) / Step);
			return OutI >= 0 && OutJ >= 0 && OutI < NX && OutJ < NY;
		}
		double LevelZ(int32 K) const { return Z0 + K * ZStep; }
		int32 LevelOf(double Z) const { return FMath::RoundToInt32((Z - Z0) / ZStep); }
		double TopZAt(int32 I, int32 J) const { return Z0 + TopRel[Index(I, J)]; }
		FVector2D Max() const { return Origin + FVector2D((NX - 1) * Step, (NY - 1) * Step); }
	};

	namespace Detail
	{
		template <typename T>
		T ReadAt(TArrayView<const uint8> Bytes, int32 Offset)
		{
			T Value;
			FMemory::Memcpy(&Value, Bytes.GetData() + Offset, sizeof(T));
			return Value;
		}

		template <typename T>
		void CopyLayer(const TArray<uint8>& Raw, int64& Offset, int32 Count, TArray<T>& Out)
		{
			Out.SetNumUninitialized(Count);
			FMemory::Memcpy(Out.GetData(), Raw.GetData() + Offset, sizeof(T) * Count);
			Offset += static_cast<int64>(sizeof(T)) * Count;
		}
	}

	inline bool ParseBake(TArrayView<const uint8> Bytes, FSafeGrid& Out, FString& OutError)
	{
		using namespace Detail;
		if (Bytes.Num() < BakeHeaderBytes || FMemory::Memcmp(Bytes.GetData(), "TNCB", 4) != 0)
		{
			OutError = TEXT("bake.bin: falta la cabecera TNCB");
			return false;
		}
		const uint32 Version = ReadAt<uint32>(Bytes, 4);
		const int32 NX = ReadAt<int32>(Bytes, 8);
		const int32 NY = ReadAt<int32>(Bytes, 12);
		const uint32 RawSize = ReadAt<uint32>(Bytes, 36);
		const uint32 ZlibSize = ReadAt<uint32>(Bytes, 40);
		const int64 N = static_cast<int64>(NX) * NY;
		if (Version != BakeVersion || NX < 2 || NY < 2 || static_cast<int64>(RawSize) != N * 6
			|| BakeHeaderBytes + static_cast<int64>(ZlibSize) > Bytes.Num())
		{
			OutError = FString::Printf(TEXT("bake.bin: cabecera incoherente (v%u, %d x %d, %u bytes)"), Version, NX, NY, RawSize);
			return false;
		}
		TArray<uint8> Raw;
		Raw.SetNumUninitialized(RawSize);
		if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), static_cast<int64>(RawSize), Bytes.GetData() + BakeHeaderBytes,
			static_cast<int64>(ZlibSize)))
		{
			OutError = TEXT("bake.bin: zlib no descomprime");
			return false;
		}
		Out = FSafeGrid();
		Out.NX = NX;
		Out.NY = NY;
		Out.Origin = FVector2D(ReadAt<float>(Bytes, 16), ReadAt<float>(Bytes, 20));
		Out.Step = ReadAt<float>(Bytes, 24);
		Out.Z0 = ReadAt<float>(Bytes, 28);
		Out.ZStep = ReadAt<float>(Bytes, 32);
		int64 Offset = 0;
		CopyLayer(Raw, Offset, static_cast<int32>(N), Out.TopRel);
		CopyLayer(Raw, Offset, static_cast<int32>(N), Out.SafeLow);
		CopyLayer(Raw, Offset, static_cast<int32>(N), Out.SafeHigh);
		CopyLayer(Raw, Offset, static_cast<int32>(N), Out.PathDistDm);
		return true;
	}

	inline bool IsInBounds(const FSafeGrid& Grid, const FVector& Feet)
	{
		const FVector2D Hi = Grid.Max();
		return Grid.IsValid() && Feet.X >= Grid.Origin.X && Feet.Y >= Grid.Origin.Y && Feet.X <= Hi.X && Feet.Y <= Hi.Y
			&& Feet.Z >= Grid.Z0 - BelowFloorMargin;
	}

	/** Permitido: safe_low ≤ k(pies) ≤ safe_high + 1 en la columna, dentro de límites (plan maestro §2.3). */
	inline bool IsAllowed(const FSafeGrid& Grid, const FVector& Feet)
	{
		int32 I = 0;
		int32 J = 0;
		if (!IsInBounds(Grid, Feet) || !Grid.CellOf(FVector2D(Feet.X, Feet.Y), I, J))
		{
			return false;
		}
		const int32 Idx = Grid.Index(I, J);
		if (Grid.SafeLow[Idx] == NoLevel)
		{
			return false;
		}
		const int32 K = Grid.LevelOf(Feet.Z);
		return K >= Grid.SafeLow[Idx] && K <= Grid.SafeHigh[Idx] + 1;
	}

	struct FTraceWindow
	{
		double Top = 0.0;
		double Bottom = 0.0;
		bool bHasLevels = false;
	};

	/** De dónde a dónde trazar el suelo bajo ZHint sin tocar techos: acotado a los niveles permitidos de la columna. */
	inline FTraceWindow WalkableTraceWindow(const FSafeGrid& Grid, const FVector2D& XY, double ZHint)
	{
		FTraceWindow Window;
		Window.Top = ZHint + TraceAboveHint;
		Window.Bottom = ZHint - 3000.0;
		int32 I = 0;
		int32 J = 0;
		if (!Grid.IsValid() || !Grid.CellOf(XY, I, J) || Grid.SafeLow[Grid.Index(I, J)] == NoLevel)
		{
			return Window;
		}
		const int32 Idx = Grid.Index(I, J);
		Window.bHasLevels = true;
		Window.Bottom = Grid.LevelZ(Grid.SafeLow[Idx]) - 100.0;
		Window.Top = FMath::Min(ZHint + TraceAboveHint, Grid.LevelZ(Grid.SafeHigh[Idx]) + 100.0);
		if (Window.Top < Window.Bottom)
		{
			Window.Top = Grid.LevelZ(Grid.SafeHigh[Idx]) + 100.0;
		}
		return Window;
	}

	/** Sin colisión que trazar: el nivel permitido más cercano a ZHint (o la cara de arriba si la columna no tiene). */
	inline double FallbackWalkableZ(const FSafeGrid& Grid, const FVector2D& XY, double ZHint)
	{
		int32 I = 0;
		int32 J = 0;
		if (!Grid.IsValid() || !Grid.CellOf(XY, I, J))
		{
			return ZHint;
		}
		const int32 Idx = Grid.Index(I, J);
		if (Grid.SafeLow[Idx] == NoLevel)
		{
			return FMath::Min(ZHint, Grid.TopZAt(I, J));
		}
		return Grid.LevelZ(FMath::Clamp(Grid.LevelOf(ZHint), static_cast<int32>(Grid.SafeLow[Idx]), static_cast<int32>(Grid.SafeHigh[Idx])));
	}

	namespace Detail
	{
		/** Mar = celdas hondas conectadas al borde del mapa (las charcas interiores no cuentan). */
		inline void FloodSea(const FSafeGrid& Grid, double WaterZ, TArray<uint8>& OutSea)
		{
			OutSea.Init(0, Grid.NX * Grid.NY);
			TArray<FIntPoint> Queue;
			auto Deep = [&Grid, WaterZ](int32 I, int32 J) { return Grid.TopZAt(I, J) < WaterZ - SeaDepth; };
			auto Push = [&](int32 I, int32 J)
			{
				if (I < 0 || J < 0 || I >= Grid.NX || J >= Grid.NY || OutSea[Grid.Index(I, J)] || !Deep(I, J)) { return; }
				OutSea[Grid.Index(I, J)] = 1;
				Queue.Add(FIntPoint(I, J));
			};
			for (int32 I = 0; I < Grid.NX; ++I) { Push(I, 0); Push(I, Grid.NY - 1); }
			for (int32 J = 0; J < Grid.NY; ++J) { Push(0, J); Push(Grid.NX - 1, J); }
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				const FIntPoint C = Queue[Head];
				Push(C.X + 1, C.Y); Push(C.X - 1, C.Y); Push(C.X, C.Y + 1); Push(C.X, C.Y - 1);
			}
		}

		/** Distancia chaflán 10/14 en dm (paso de 1 m) en dos pasadas. */
		inline void Chamfer(const FSafeGrid& Grid, TArray<int32>& D)
		{
			const int32 Orth = FMath::RoundToInt32(Grid.Step / 10.0);
			const int32 Diag = FMath::RoundToInt32(Grid.Step * UE_SQRT_2 / 10.0);
			auto Relax = [&](int32 I, int32 J, int32 DI, int32 DJ, int32 Cost)
			{
				const int32 A = I + DI;
				const int32 B = J + DJ;
				if (A < 0 || B < 0 || A >= Grid.NX || B >= Grid.NY) { return; }
				D[Grid.Index(I, J)] = FMath::Min(D[Grid.Index(I, J)], D[Grid.Index(A, B)] + Cost);
			};
			for (int32 J = 0; J < Grid.NY; ++J)
			{
				for (int32 I = 0; I < Grid.NX; ++I) { Relax(I, J, -1, 0, Orth); Relax(I, J, 0, -1, Orth); Relax(I, J, -1, -1, Diag); Relax(I, J, 1, -1, Diag); }
			}
			for (int32 J = Grid.NY - 1; J >= 0; --J)
			{
				for (int32 I = Grid.NX - 1; I >= 0; --I) { Relax(I, J, 1, 0, Orth); Relax(I, J, 0, 1, Orth); Relax(I, J, 1, 1, Diag); Relax(I, J, -1, 1, Diag); }
			}
		}
	}

	/** Distancia (dm) de cada celda al mar abierto (R6: SeaNear en mapas fijos, en vez de la costa de semilla). */
	inline void ComputeSeaDistance(const FSafeGrid& Grid, double WaterZ, TArray<uint16>& OutDm)
	{
		OutDm.Init(FarPathDm, FMath::Max(0, Grid.NX * Grid.NY));
		if (!Grid.IsValid())
		{
			return;
		}
		TArray<uint8> Sea;
		Detail::FloodSea(Grid, WaterZ, Sea);
		TArray<int32> D;
		D.SetNumUninitialized(Sea.Num());
		for (int32 i = 0; i < Sea.Num(); ++i) { D[i] = Sea[i] ? 0 : TNumericLimits<int32>::Max() / 4; }
		Detail::Chamfer(Grid, D);
		for (int32 i = 0; i < D.Num(); ++i) { OutDm[i] = static_cast<uint16>(FMath::Min(D[i], static_cast<int32>(FarPathDm))); }
	}

	/** Rompientes a tope en el mar y nada a 120 m (el mismo alcance que la costa del procedural). */
	inline float SeaNearFromDistance(uint16 Dm)
	{
		return FMath::Clamp(1.f - static_cast<float>(Dm) / 1200.f, 0.f, 1.f);
	}
}
```

- [ ] **Paso 5: ver pasar.** BUILD; UETEST `Coop` → `Coop.Bake`, `Coop.Allowed`, `Coop.WalkableTunnel`, `Coop.SeaDistance` en `Success`.
- [ ] **Paso 6: commit.** `git add Source/Tortunabo/Public/World/ProcMap/TN_CoopGridDecisions.h Source/Tortunabo/Private/Tests/TN_CoopTestUtil.h Source/Tortunabo/Private/Tests/TN_CoopGridDecisionsTest.cpp` · `git commit -m "feat(coop): lectura del bake y máscara de zonas permitidas con suelo bajo techo de túnel"`

### Tarea 14: `UTN_CoopMapData` y `LoadFromManifest`

**Files:**
- Create: `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapData.h`, `Source/Tortunabo/Private/World/ProcMap/TN_CoopMapData.cpp`, `Source/Tortunabo/Private/Tests/TN_CoopMapDataTest.cpp`

**Interfaces:**
- Consumes: `TNCoopMap::ParseBake`, `FSafeGrid` (Tarea 13); `ETNProcBiome`; `ETNBeachElement`.
- Produces: `USTRUCT FTNCoopPathPoint { FVector Pos; float Width; float S; }`, `FTNCoopZone { FName Name; ETNProcBiome Biome; float S0; float S1; }`, `FTNCoopKillBox { FVector Center; FVector Extent; float Yaw; }`, `FTNCoopPlacement { FName Id; FVector Pos; float Yaw; int32 Order; }`, `FTNCoopFinish { FVector Pos; FVector2D Dir; float Width; float Depth; bool bInWater; }`, `FTNCoopElement { FName Id; ETNBeachElement Element; FVector Pos; float Yaw; float SizeScale; float Extent; }`, `FTNCoopDecorPoint { ETNBeachElement Element; FVector Pos; float Yaw; float SizeScale; int32 Seed; }`, `FTNCoopBakeGrid { FVector2D Origin; float Step; int32 NX; int32 NY; float Z0; float ZStep; TArray<uint16> TopRel; TArray<uint8> SafeLow; TArray<uint8> SafeHigh; TArray<uint16> PathDistDm; }`; `UTN_CoopMapData : UPrimaryDataAsset` con `MapId`, `WaterZ`, `bSwim`, `Main`, `Zones`, `KillBoxes`, `Start`, `Finish`, `Nests`, `Jellyfish`, `Elements`, `Vegetation`, `Bake`, `LastLoadError`; `bool LoadFromMemory(const FString& ManifestJson, TArrayView<const uint8> BakeBytes, FString& OutError)`; `UFUNCTION(BlueprintCallable) bool LoadFromManifest(const FString& ManifestPath)` (WITH_EDITOR); `const TNCoopMap::FSafeGrid& GetSafeGrid() const`; `int32 NearestMainIndex(const FVector&) const`; `FVector2D MainDirAt(int32) const`.

- [ ] **Paso 1: test que falla.** `Source/Tortunabo/Private/Tests/TN_CoopMapDataTest.cpp`:

```cpp
// DataAsset del mapa preparado (UTN_CoopMapData) y su proveedor de suelo seguro.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Coop; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Tests/TN_CoopTestUtil.h"
#include "World/ProcMap/TN_CoopMapData.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopLoadFromManifestTest, "Tortunabo.Coop.LoadFromManifest",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopLoadFromManifestTest::RunTest(const FString& Parameters)
{
	UTN_CoopMapData* Data = NewObject<UTN_CoopMapData>();
	FString Error;
	const TArray<uint8> Bake = TNCoopTestUtil::MakeBakeBytes(TNCoopTestUtil::MakeGrid(21, 21, 20));
	TestTrue(TEXT("Carga el manifest v2"), Data->LoadFromMemory(TNCoopTestUtil::MakeManifestJson(), Bake, Error));
	TestEqual(TEXT("MapId"), Data->MapId, FName(TEXT("T01")));
	TestEqual(TEXT("Muestras del camino"), Data->Main.Num(), 3);
	TestEqual(TEXT("S de la última muestra"), Data->Main.Last().S, 1800.f);
	TestTrue(TEXT("Zona de playa"), Data->Zones[0].Biome == ETNProcBiome::Beach);
	TestEqual(TEXT("Nidos"), Data->Nests.Num(), 1);
	TestTrue(TEXT("Mecánica"), Data->Elements[0].Element == ETNBeachElement::Catapult);
	TestTrue(TEXT("Vegetación"), Data->Vegetation[0].Element == ETNBeachElement::Rock);
	TestEqual(TEXT("Semilla de la planta"), Data->Vegetation[0].Seed, 7);
	TestEqual(TEXT("Medusas"), Data->Jellyfish.Num(), 1);
	TestEqual(TEXT("Cajas de muerte"), Data->KillBoxes.Num(), 1);
	TestTrue(TEXT("Meta hacia +X"), Data->Finish.Dir.Equals(FVector2D(1.0, 0.0)));
	TestTrue(TEXT("Rejilla válida"), Data->GetSafeGrid().IsValid());
	TestEqual(TEXT("Muestra más cercana"), Data->NearestMainIndex(FVector(950.0, 1100.0, 0.0)), 1);
	TestTrue(TEXT("Dirección del camino"), Data->MainDirAt(1).Equals(FVector2D(1.0, 0.0)));

	UTN_CoopMapData* Broken = NewObject<UTN_CoopMapData>();
	TestFalse(TEXT("Rechaza un manifest sin bloque coop"), Broken->LoadFromMemory(TEXT("{\"name\":\"X\"}"), Bake, Error));
	TestFalse(TEXT("Rechaza un elemento desconocido"), Broken->LoadFromMemory(
		TNCoopTestUtil::MakeManifestJson().Replace(TEXT("\"Catapult\""), TEXT("\"NoExiste\"")), Bake, Error));
	return true;
}

#endif
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila (`TN_CoopMapData.h`).
- [ ] **Paso 3: cabecera.** `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapData.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "World/Beach/TN_BeachTypes.h"
#include "World/ProcMap/TN_CoopGridDecisions.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "TN_CoopMapData.generated.h"

USTRUCT()
struct FTNCoopPathPoint
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category = "Coop") FVector Pos = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Coop") float Width = 600.f;
	UPROPERTY(VisibleAnywhere, Category = "Coop") float S = 0.f;
};

USTRUCT()
struct FTNCoopZone
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category = "Coop") FName Name;
	UPROPERTY(VisibleAnywhere, Category = "Coop") ETNProcBiome Biome = ETNProcBiome::Beach;
	UPROPERTY(VisibleAnywhere, Category = "Coop") float S0 = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Coop") float S1 = 0.f;
};

USTRUCT()
struct FTNCoopKillBox
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category = "Coop") FVector Center = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Coop") FVector Extent = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Coop") float Yaw = 0.f;
};

/** Salida, nido (Order) o medusa: id estable del manifest, sitio con la Z del suelo caminable y giro. */
USTRUCT()
struct FTNCoopPlacement
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category = "Coop") FName Id;
	UPROPERTY(VisibleAnywhere, Category = "Coop") FVector Pos = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Coop") float Yaw = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Coop") int32 Order = 0;
};

USTRUCT()
struct FTNCoopFinish
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category = "Coop") FVector Pos = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Coop") FVector2D Dir = FVector2D(1.0, 0.0);
	UPROPERTY(VisibleAnywhere, Category = "Coop") float Width = 1500.f;
	UPROPERTY(VisibleAnywhere, Category = "Coop") float Depth = 1200.f;
	UPROPERTY(VisibleAnywhere, Category = "Coop") bool bInWater = false;
};

USTRUCT()
struct FTNCoopElement
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category = "Coop") FName Id;
	UPROPERTY(VisibleAnywhere, Category = "Coop") ETNBeachElement Element = ETNBeachElement::Catapult;
	UPROPERTY(VisibleAnywhere, Category = "Coop") FVector Pos = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Coop") float Yaw = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Coop") float SizeScale = 1.f;
	UPROPERTY(VisibleAnywhere, Category = "Coop") float Extent = 0.f;
};

USTRUCT()
struct FTNCoopDecorPoint
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category = "Coop") ETNBeachElement Element = ETNBeachElement::Rock;
	UPROPERTY(VisibleAnywhere, Category = "Coop") FVector Pos = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category = "Coop") float Yaw = 0.f;
	UPROPERTY(VisibleAnywhere, Category = "Coop") float SizeScale = 1.f;
	UPROPERTY(VisibleAnywhere, Category = "Coop") int32 Seed = 0;
};

/** Copia serializable de TNCoopMap::FSafeGrid (bake.bin v1). */
USTRUCT()
struct FTNCoopBakeGrid
{
	GENERATED_BODY()
	UPROPERTY() FVector2D Origin = FVector2D::ZeroVector;
	UPROPERTY() float Step = 100.f;
	UPROPERTY() int32 NX = 0;
	UPROPERTY() int32 NY = 0;
	UPROPERTY() float Z0 = -1000.f;
	UPROPERTY() float ZStep = 50.f;
	UPROPERTY() TArray<uint16> TopRel;
	UPROPERTY() TArray<uint8> SafeLow;
	UPROPERTY() TArray<uint8> SafeHigh;
	UPROPERTY() TArray<uint16> PathDistDm;
};

/**
 * Datos de juego de un mapa preparado (Coop sobre mapas fijos, plan maestro §2.1): lo escribe
 * Scripts/import_terrain_coop.py con LoadFromManifest (manifest v2 + coop/bake.bin) y lo cocina el nivel que lo
 * referencia (ATN_ProcMapGenerator::CoopData). Todo en espacio de mundo; la Z de cada sitio es la del suelo caminable.
 */
UCLASS(BlueprintType)
class TORTUNABO_API UTN_CoopMapData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop") FName MapId;
	UPROPERTY(VisibleAnywhere, Category = "Coop") float WaterZ = -400.f;
	UPROPERTY(VisibleAnywhere, Category = "Coop") bool bSwim = false;
	UPROPERTY(VisibleAnywhere, Category = "Coop") TArray<FTNCoopPathPoint> Main;
	UPROPERTY(VisibleAnywhere, Category = "Coop") TArray<FTNCoopZone> Zones;
	UPROPERTY(VisibleAnywhere, Category = "Coop") TArray<FTNCoopKillBox> KillBoxes;
	UPROPERTY(VisibleAnywhere, Category = "Coop") FTNCoopPlacement Start;
	UPROPERTY(VisibleAnywhere, Category = "Coop") FTNCoopFinish Finish;
	UPROPERTY(VisibleAnywhere, Category = "Coop") TArray<FTNCoopPlacement> Nests;
	UPROPERTY(VisibleAnywhere, Category = "Coop") TArray<FTNCoopPlacement> Jellyfish;
	UPROPERTY(VisibleAnywhere, Category = "Coop") TArray<FTNCoopElement> Elements;
	UPROPERTY(VisibleAnywhere, Category = "Coop") TArray<FTNCoopDecorPoint> Vegetation;
	UPROPERTY(VisibleAnywhere, Category = "Coop") FTNCoopBakeGrid Bake;

	/** Motivo del último fallo de carga (lo lee el importador de Python). */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Coop") FString LastLoadError;

	/** Rellena el asset desde el texto del manifest v2 y los bytes de su bake.bin. false y OutError si algo no casa. */
	bool LoadFromMemory(const FString& ManifestJson, TArrayView<const uint8> BakeBytes, FString& OutError);

#if WITH_EDITOR
	/** Editor: lee ManifestPath y el bake que nombra (ruta relativa al manifest). Deja el error en LastLoadError. */
	UFUNCTION(BlueprintCallable, Category = "Coop")
	bool LoadFromManifest(const FString& ManifestPath);
#endif

	const TNCoopMap::FSafeGrid& GetSafeGrid() const { return Grid; }

	/** Muestra del camino principal más cercana (la altura pesa 1,5 veces: distingue el tramo de arriba del de abajo). */
	int32 NearestMainIndex(const FVector& Location) const;

	/** Dirección del camino en la muestra Index (hacia la meta). */
	FVector2D MainDirAt(int32 Index) const;

	virtual void PostLoad() override;
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("TNCoopMap"), MapId); }

private:
	TNCoopMap::FSafeGrid Grid;

	void RebuildGrid();
};
```

- [ ] **Paso 4: implementación.** `Source/Tortunabo/Private/World/ProcMap/TN_CoopMapData.cpp`:

```cpp
#include "World/ProcMap/TN_CoopMapData.h"

#include "Core/TN_Log.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace TNCoopMapDataDetail
{
	using FJsonArray = TArray<TSharedPtr<FJsonValue>>;

	FVector Vec3(const FJsonArray& A)
	{
		return A.Num() >= 3 ? FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()) : FVector::ZeroVector;
	}

	FVector PosOf(const TSharedPtr<FJsonObject>& Obj)
	{
		const FJsonArray* Pos = nullptr;
		return Obj.IsValid() && Obj->TryGetArrayField(TEXT("pos"), Pos) ? Vec3(*Pos) : FVector::ZeroVector;
	}

	template <typename TEnum>
	bool EnumByName(const FString& Name, TEnum& Out)
	{
		const int64 Value = StaticEnum<TEnum>()->GetValueByNameString(Name);
		if (Value == INDEX_NONE) { return false; }
		Out = static_cast<TEnum>(Value);
		return true;
	}

	void ReadPath(const TSharedPtr<FJsonObject>& Coop, TArray<FTNCoopPathPoint>& Out)
	{
		Out.Reset();
		const TSharedPtr<FJsonObject>* Path = nullptr;
		const FJsonArray* Main = nullptr;
		if (!Coop->TryGetObjectField(TEXT("path"), Path) || !(*Path)->TryGetArrayField(TEXT("main"), Main)) { return; }
		float S = 0.f;
		for (const TSharedPtr<FJsonValue>& V : *Main)
		{
			const FJsonArray& P = V->AsArray();
			if (P.Num() < 4) { continue; }
			FTNCoopPathPoint& Point = Out.AddDefaulted_GetRef();
			Point.Pos = Vec3(P);
			Point.Width = static_cast<float>(P[3]->AsNumber());
			if (Out.Num() > 1) { S += static_cast<float>(FVector::Dist2D(Out[Out.Num() - 2].Pos, Point.Pos)); }
			Point.S = S;
		}
	}

	void ReadPlacements(const TSharedPtr<FJsonObject>& Coop, const TCHAR* Field, TArray<FTNCoopPlacement>& Out)
	{
		Out.Reset();
		const FJsonArray* List = nullptr;
		if (!Coop->TryGetArrayField(Field, List)) { return; }
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			FTNCoopPlacement& P = Out.AddDefaulted_GetRef();
			P.Id = FName(Obj->GetStringField(TEXT("id")));
			P.Pos = PosOf(Obj);
			double Number = 0.0;
			if (Obj->TryGetNumberField(TEXT("yaw"), Number)) { P.Yaw = static_cast<float>(Number); }
			if (Obj->TryGetNumberField(TEXT("order"), Number)) { P.Order = static_cast<int32>(Number); }
		}
	}

	bool ReadElements(const TSharedPtr<FJsonObject>& Coop, TArray<FTNCoopElement>& Out, FString& OutError)
	{
		Out.Reset();
		const FJsonArray* List = nullptr;
		if (!Coop->TryGetArrayField(TEXT("beach_elements"), List)) { return true; }
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			FTNCoopElement& E = Out.AddDefaulted_GetRef();
			if (!EnumByName(Obj->GetStringField(TEXT("element")), E.Element))
			{
				OutError = FString::Printf(TEXT("mecánica desconocida '%s'"), *Obj->GetStringField(TEXT("element")));
				return false;
			}
			E.Id = FName(Obj->GetStringField(TEXT("id")));
			E.Pos = PosOf(Obj);
			E.Yaw = static_cast<float>(Obj->GetNumberField(TEXT("yaw")));
			E.SizeScale = static_cast<float>(Obj->GetNumberField(TEXT("size")));
			E.Extent = static_cast<float>(Obj->GetNumberField(TEXT("extent")));
		}
		return true;
	}

	bool ReadVegetation(const TSharedPtr<FJsonObject>& Coop, TArray<FTNCoopDecorPoint>& Out, FString& OutError)
	{
		Out.Reset();
		const TSharedPtr<FJsonObject>* Veg = nullptr;
		const FJsonArray* Points = nullptr;
		if (!Coop->TryGetObjectField(TEXT("vegetation"), Veg) || !(*Veg)->TryGetArrayField(TEXT("points"), Points)) { return true; }
		for (const TSharedPtr<FJsonValue>& V : *Points)
		{
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			FTNCoopDecorPoint& P = Out.AddDefaulted_GetRef();
			if (!EnumByName(Obj->GetStringField(TEXT("element")), P.Element))
			{
				OutError = FString::Printf(TEXT("decorado desconocido '%s'"), *Obj->GetStringField(TEXT("element")));
				return false;
			}
			P.Pos = PosOf(Obj);
			P.Yaw = static_cast<float>(Obj->GetNumberField(TEXT("yaw")));
			P.SizeScale = static_cast<float>(Obj->GetNumberField(TEXT("size")));
			P.Seed = static_cast<int32>(Obj->GetNumberField(TEXT("seed")));
		}
		return true;
	}

	bool ReadZones(const TSharedPtr<FJsonObject>& Coop, TArray<FTNCoopZone>& Out, FString& OutError)
	{
		Out.Reset();
		const FJsonArray* List = nullptr;
		if (!Coop->TryGetArrayField(TEXT("zones"), List)) { return true; }
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			FTNCoopZone& Z = Out.AddDefaulted_GetRef();
			if (!EnumByName(Obj->GetStringField(TEXT("biome")), Z.Biome))
			{
				OutError = FString::Printf(TEXT("bioma desconocido '%s'"), *Obj->GetStringField(TEXT("biome")));
				return false;
			}
			Z.Name = FName(Obj->GetStringField(TEXT("name")));
			Z.S0 = static_cast<float>(Obj->GetNumberField(TEXT("s0_uu")));
			Z.S1 = static_cast<float>(Obj->GetNumberField(TEXT("s1_uu")));
		}
		return true;
	}

	void ReadKillBoxes(const TSharedPtr<FJsonObject>& Coop, TArray<FTNCoopKillBox>& Out)
	{
		Out.Reset();
		const FJsonArray* List = nullptr;
		if (!Coop->TryGetArrayField(TEXT("kill_boxes_uu"), List)) { return; }
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			FTNCoopKillBox& Box = Out.AddDefaulted_GetRef();
			Box.Center = Vec3(Obj->GetArrayField(TEXT("center")));
			Box.Extent = Vec3(Obj->GetArrayField(TEXT("extent")));
			Box.Yaw = static_cast<float>(Obj->GetNumberField(TEXT("yaw")));
		}
	}

	void ReadFinish(const TSharedPtr<FJsonObject>& Coop, FTNCoopFinish& Out)
	{
		const TSharedPtr<FJsonObject>* Finish = nullptr;
		if (!Coop->TryGetObjectField(TEXT("finish"), Finish)) { return; }
		const FJsonArray& Dir = (*Finish)->GetArrayField(TEXT("dir"));
		Out.Pos = PosOf(*Finish);
		Out.Dir = Dir.Num() >= 2 ? FVector2D(Dir[0]->AsNumber(), Dir[1]->AsNumber()).GetSafeNormal() : FVector2D(1.0, 0.0);
		Out.Width = static_cast<float>((*Finish)->GetNumberField(TEXT("width_uu")));
		Out.Depth = static_cast<float>((*Finish)->GetNumberField(TEXT("depth_uu")));
		Out.bInWater = (*Finish)->GetBoolField(TEXT("in_water"));
	}
}

bool UTN_CoopMapData::LoadFromMemory(const FString& ManifestJson, TArrayView<const uint8> BakeBytes, FString& OutError)
{
	using namespace TNCoopMapDataDetail;
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* Coop = nullptr;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestJson), Root) || !Root.IsValid()
		|| !Root->TryGetObjectField(TEXT("coop"), Coop))
	{
		OutError = TEXT("manifest sin bloque 'coop' (manifest v2)");
		return false;
	}
	TNCoopMap::FSafeGrid Parsed;
	if (!TNCoopMap::ParseBake(BakeBytes, Parsed, OutError)
		|| !ReadElements(*Coop, Elements, OutError) || !ReadVegetation(*Coop, Vegetation, OutError) || !ReadZones(*Coop, Zones, OutError))
	{
		return false;
	}
	MapId = FName((*Coop)->GetStringField(TEXT("map_id")));
	const TSharedPtr<FJsonObject>& Water = (*Coop)->GetObjectField(TEXT("water"));
	WaterZ = static_cast<float>(Water->GetNumberField(TEXT("z_uu")));
	bSwim = Water->GetBoolField(TEXT("swim"));
	ReadPath(*Coop, Main);
	ReadKillBoxes(*Coop, KillBoxes);
	ReadFinish(*Coop, Finish);
	ReadPlacements(*Coop, TEXT("nests"), Nests);
	ReadPlacements(*Coop, TEXT("jellyfish"), Jellyfish);
	const TSharedPtr<FJsonObject>& StartObj = (*Coop)->GetObjectField(TEXT("start"));
	Start.Id = FName(StartObj->GetStringField(TEXT("id")));
	Start.Pos = PosOf(StartObj);
	Start.Yaw = static_cast<float>(StartObj->GetNumberField(TEXT("yaw")));
	if (Main.Num() < 2)
	{
		OutError = TEXT("el camino principal necesita al menos 2 muestras");
		return false;
	}
	Bake.Origin = Parsed.Origin;
	Bake.Step = static_cast<float>(Parsed.Step);
	Bake.NX = Parsed.NX;
	Bake.NY = Parsed.NY;
	Bake.Z0 = static_cast<float>(Parsed.Z0);
	Bake.ZStep = static_cast<float>(Parsed.ZStep);
	Bake.TopRel = Parsed.TopRel;
	Bake.SafeLow = Parsed.SafeLow;
	Bake.SafeHigh = Parsed.SafeHigh;
	Bake.PathDistDm = Parsed.PathDistDm;
	Grid = MoveTemp(Parsed);
	MarkPackageDirty();
	return true;
}

#if WITH_EDITOR
bool UTN_CoopMapData::LoadFromManifest(const FString& ManifestPath)
{
	FString Json;
	TArray<uint8> BakeBytes;
	LastLoadError.Reset();
	if (!FFileHelper::LoadFileToString(Json, *ManifestPath))
	{
		LastLoadError = FString::Printf(TEXT("no se lee %s"), *ManifestPath);
		return false;
	}
	const FString BakePath = FPaths::Combine(FPaths::GetPath(ManifestPath), TEXT("coop"), TEXT("bake.bin"));
	if (!FFileHelper::LoadFileToArray(BakeBytes, *BakePath))
	{
		LastLoadError = FString::Printf(TEXT("no se lee %s"), *BakePath);
		return false;
	}
	const bool bOk = LoadFromMemory(Json, BakeBytes, LastLoadError);
	UE_LOG(LogTortunabo, Log, TEXT("[Coop] %s: %s"), *ManifestPath, bOk ? TEXT("cargado") : *LastLoadError);
	return bOk;
}
#endif

void UTN_CoopMapData::PostLoad()
{
	Super::PostLoad();
	RebuildGrid();
}

void UTN_CoopMapData::RebuildGrid()
{
	Grid = TNCoopMap::FSafeGrid();
	Grid.Origin = Bake.Origin;
	Grid.Step = Bake.Step;
	Grid.NX = Bake.NX;
	Grid.NY = Bake.NY;
	Grid.Z0 = Bake.Z0;
	Grid.ZStep = Bake.ZStep;
	Grid.TopRel = Bake.TopRel;
	Grid.SafeLow = Bake.SafeLow;
	Grid.SafeHigh = Bake.SafeHigh;
	Grid.PathDistDm = Bake.PathDistDm;
}

int32 UTN_CoopMapData::NearestMainIndex(const FVector& Location) const
{
	int32 Best = INDEX_NONE;
	double BestScore = TNumericLimits<double>::Max();
	for (int32 i = 0; i < Main.Num(); ++i)
	{
		const FVector D = Main[i].Pos - Location;
		const double Score = D.X * D.X + D.Y * D.Y + 2.25 * D.Z * D.Z;
		if (Score < BestScore) { BestScore = Score; Best = i; }
	}
	return Best;
}

FVector2D UTN_CoopMapData::MainDirAt(int32 Index) const
{
	if (Main.Num() < 2 || !Main.IsValidIndex(Index)) { return FVector2D(1.0, 0.0); }
	const int32 A = FMath::Min(Index, Main.Num() - 2);
	return FVector2D(Main[A + 1].Pos - Main[A].Pos).GetSafeNormal();
}
```

- [ ] **Paso 5: ver pasar.** BUILD; UETEST `Coop.LoadFromManifest` → `Success`.
- [ ] **Paso 6: commit.** `git add Source/Tortunabo/Public/World/ProcMap/TN_CoopMapData.h Source/Tortunabo/Private/World/ProcMap/TN_CoopMapData.cpp Source/Tortunabo/Private/Tests/TN_CoopMapDataTest.cpp` · `git commit -m "feat(coop): DataAsset del mapa preparado con carga del manifest v2 y del bake"`

### Tarea 15: Lógica pura del vigilante y del rescate

**Files:**
- Create: `Source/Tortunabo/Public/World/ProcMap/TN_CoopRescueDecisions.h`, `Source/Tortunabo/Private/Tests/TN_CoopRescueDecisionsTest.cpp`

**Interfaces:**
- Consumes: `TNBeach::ETNBeachMover` (`TN_BeachStun.h:22`).
- Produces (namespace `TNCoopMap`): `WatchPeriodSeconds = 0.25`, `UnsafeGraceSeconds = 1.5`, `RescueMinSeparation = 120.0`, `RescueLateralStep = 150.0`, `RescueBackSamples = 8`; `enum class EWatchVerdict : uint8 { Safe, Pending, Rescue }`; `struct FWatchState { double UnsafeSince = -1.0; FVector LastSafe; float LastSafeProgress = 0.f; bool bHasSafe = false; }`; `struct FWatchInput { double Now; FVector Feet; float Progress; bool bInBounds, bAllowed, bGrounded, bMoverBusy; }`; `EWatchVerdict StepWatch(FWatchState&, const FWatchInput&)`; `void ResetAfterRescue(FWatchState&)`; `bool IsMoverBusyForRescue(TNBeach::ETNBeachMover)`; `int32 RescueSampleIndex(const TArray<float>& MainS, float Progress)`; `void RescueCandidates(const TArray<FVector>& MainPos, const TArray<FVector2D>& MainDir, int32 Index, double HalfWidth, TArray<FVector>& Out)`; `int32 PickRescueSlot(const TArray<FVector>& Candidates, const TArray<FVector>& Occupied, double MinSeparation)`.

- [ ] **Paso 1: tests que fallan.** `Source/Tortunabo/Private/Tests/TN_CoopRescueDecisionsTest.cpp`:

```cpp
// Vigilante fuera de zona y rescate (TN_CoopRescueDecisions.h). Review Focus 2 y 3 del plan F0–F2.

#include "Misc/AutomationTest.h"
#include "World/ProcMap/TN_CoopRescueDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNCoopRescueTest
{
	TNCoopMap::FWatchInput Input(double Now, bool bAllowed, bool bGrounded = true, bool bInBounds = true, bool bBusy = false)
	{
		TNCoopMap::FWatchInput In;
		In.Now = Now;
		In.Feet = FVector(100.0 * Now, 0.0, 0.0);
		In.Progress = static_cast<float>(1000.0 * Now);
		In.bAllowed = bAllowed;
		In.bGrounded = bGrounded;
		In.bInBounds = bInBounds;
		In.bMoverBusy = bBusy;
		return In;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopWatchTest, "Tortunabo.Coop.Watch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopWatchTest::RunTest(const FString& Parameters)
{
	using namespace TNCoopMap;
	using TNCoopRescueTest::Input;
	FWatchState State;
	TestTrue(TEXT("Suelo permitido"), StepWatch(State, Input(0.0, true)) == EWatchVerdict::Safe);
	TestEqual(TEXT("Guarda el último sitio seguro"), State.LastSafeProgress, 0.f);
	TestTrue(TEXT("Entra en suelo no permitido"), StepWatch(State, Input(0.25, false)) == EWatchVerdict::Pending);
	TestTrue(TEXT("1,25 s después, aún no"), StepWatch(State, Input(1.5, false)) == EWatchVerdict::Pending);
	TestTrue(TEXT("1,5 s después, rescate"), StepWatch(State, Input(1.75, false)) == EWatchVerdict::Rescue);
	ResetAfterRescue(State);
	TestEqual(TEXT("Tras el rescate, el contador vuelve a cero"), State.UnsafeSince, -1.0);
	TestTrue(TEXT("Fuera de límites: rescate inmediato"), StepWatch(State, Input(2.0, true, true, false)) == EWatchVerdict::Rescue);
	FWatchState Air;
	TestTrue(TEXT("En el aire sobre suelo no permitido no empieza a contar"), StepWatch(Air, Input(0.0, false, false)) == EWatchVerdict::Safe);
	StepWatch(Air, Input(0.25, false));
	TestTrue(TEXT("Rebotando en el pozo sigue contando"), StepWatch(Air, Input(1.8, false, false)) == EWatchVerdict::Rescue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopWatchBusyMoverTest, "Tortunabo.Coop.WatchBusyMover",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopWatchBusyMoverTest::RunTest(const FString& Parameters)
{
	using namespace TNCoopMap;
	using TNBeach::ETNBeachMover;
	TestTrue(TEXT("En brazos"), IsMoverBusyForRescue(ETNBeachMover::Carried));
	TestTrue(TEXT("En el pico de una gaviota"), IsMoverBusyForRescue(ETNBeachMover::Held));
	TestTrue(TEXT("Derribada"), IsMoverBusyForRescue(ETNBeachMover::Knockdown));
	TestTrue(TEXT("Patada de la tormenta"), IsMoverBusyForRescue(ETNBeachMover::StormKick));
	TestTrue(TEXT("Recién rescatada"), IsMoverBusyForRescue(ETNBeachMover::SafetyNet));
	TestFalse(TEXT("Andando"), IsMoverBusyForRescue(ETNBeachMover::None));
	TestFalse(TEXT("En su bola"), IsMoverBusyForRescue(ETNBeachMover::Ball));
	FWatchState State;
	StepWatch(State, TNCoopRescueTest::Input(0.0, false));
	TestTrue(TEXT("Otro la mueve: nunca rescate, aunque lleve 5 s fuera"),
		StepWatch(State, TNCoopRescueTest::Input(5.0, false, true, true, true)) == EWatchVerdict::Pending);
	TestEqual(TEXT("…y el contador se reinicia"), State.UnsafeSince, -1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopRescueSampleTest, "Tortunabo.Coop.RescueSample",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopRescueSampleTest::RunTest(const FString& Parameters)
{
	const TArray<float> S = { 0.f, 200.f, 400.f, 600.f };
	TestEqual(TEXT("Mayor S ≤ progreso"), TNCoopMap::RescueSampleIndex(S, 450.f), 2);
	TestEqual(TEXT("Exacto"), TNCoopMap::RescueSampleIndex(S, 600.f), 3);
	TestEqual(TEXT("Antes de la salida"), TNCoopMap::RescueSampleIndex(S, -10.f), 0);
	TestEqual(TEXT("Sin camino"), TNCoopMap::RescueSampleIndex(TArray<float>(), 10.f), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopRescueSlotTest, "Tortunabo.Coop.RescueSlot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopRescueSlotTest::RunTest(const FString& Parameters)
{
	TArray<FVector> Pos;
	TArray<FVector2D> Dir;
	for (int32 i = 0; i < 12; ++i) { Pos.Add(FVector(200.0 * i, 0.0, 0.0)); Dir.Add(FVector2D(1.0, 0.0)); }
	TArray<FVector> Candidates;
	TNCoopMap::RescueCandidates(Pos, Dir, 10, 300.0, Candidates);
	TestEqual(TEXT("Primero, la muestra misma"), Candidates[0], Pos[10]);
	TestEqual(TEXT("Luego, hacia atrás por el camino"), Candidates[1], Pos[9]);
	TestTrue(TEXT("Al final, a los lados dentro del ancho"), FMath::Abs(Candidates.Last().Y) <= 300.0);
	TArray<FVector> Occupied = { Pos[10] };
	const int32 First = TNCoopMap::PickRescueSlot(Candidates, Occupied, TNCoopMap::RescueMinSeparation);
	TestEqual(TEXT("La muestra ocupada se salta"), Candidates[First], Pos[9]);
	TArray<FVector> Taken;
	for (int32 k = 0; k < 8; ++k)
	{
		const int32 Slot = TNCoopMap::PickRescueSlot(Candidates, Taken, TNCoopMap::RescueMinSeparation);
		TestTrue(*FString::Printf(TEXT("Rescate %d con sitio"), k), Slot != INDEX_NONE);
		if (Slot != INDEX_NONE) { Taken.Add(Candidates[Slot]); }
	}
	for (int32 a = 0; a < Taken.Num(); ++a)
	{
		for (int32 b = a + 1; b < Taken.Num(); ++b)
		{
			TestTrue(TEXT("Ocho rescates a la vez, sin solaparse"), FVector::Dist2D(Taken[a], Taken[b]) >= TNCoopMap::RescueMinSeparation);
		}
	}
	return true;
}

#endif
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila.
- [ ] **Paso 3: implementación.** `Source/Tortunabo/Public/World/ProcMap/TN_CoopRescueDecisions.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachStun.h"

/**
 * Vigilante fuera de zona del Coop (plan maestro §2.3) como lógica PURA. El servidor lo evalúa cada WatchPeriodSeconds
 * por tortuga: en suelo permitido guarda el último sitio seguro; más de UnsafeGraceSeconds en suelo no permitido (pozo
 * inalcanzable, lanzamiento fuera del recorrido) o fuera de límites, rescate a la muestra del camino con mayor S ≤ el
 * progreso seguro. Nunca rescata a quien mueve otro (brazos, pico, derribo, tormenta, red de seguridad).
 */
namespace TNCoopMap
{
	inline constexpr double WatchPeriodSeconds = 0.25;
	inline constexpr double UnsafeGraceSeconds = 1.5;
	inline constexpr double RescueMinSeparation = 120.0;
	inline constexpr double RescueLateralStep = 150.0;
	inline constexpr int32 RescueBackSamples = 8;

	enum class EWatchVerdict : uint8 { Safe, Pending, Rescue };

	struct FWatchState
	{
		double UnsafeSince = -1.0;
		FVector LastSafe = FVector::ZeroVector;
		float LastSafeProgress = 0.f;
		bool bHasSafe = false;
	};

	struct FWatchInput
	{
		double Now = 0.0;
		FVector Feet = FVector::ZeroVector;
		float Progress = 0.f;
		bool bInBounds = true;
		bool bAllowed = true;
		bool bGrounded = true;
		bool bMoverBusy = false;
	};

	inline bool IsMoverBusyForRescue(TNBeach::ETNBeachMover Mover)
	{
		using TNBeach::ETNBeachMover;
		return Mover == ETNBeachMover::Carried || Mover == ETNBeachMover::Held || Mover == ETNBeachMover::Knockdown
			|| Mover == ETNBeachMover::StormKick || Mover == ETNBeachMover::SafetyNet || Mover == ETNBeachMover::Eaten;
	}

	inline EWatchVerdict StepWatch(FWatchState& State, const FWatchInput& In)
	{
		if (In.bMoverBusy)
		{
			State.UnsafeSince = -1.0;
			return EWatchVerdict::Pending;
		}
		if (!In.bInBounds)
		{
			return EWatchVerdict::Rescue;
		}
		if (In.bGrounded && In.bAllowed)
		{
			State.LastSafe = In.Feet;
			State.LastSafeProgress = In.Progress;
			State.bHasSafe = true;
			State.UnsafeSince = -1.0;
			return EWatchVerdict::Safe;
		}
		if (In.bGrounded && State.UnsafeSince < 0.0)
		{
			State.UnsafeSince = In.Now;
		}
		if (State.UnsafeSince < 0.0)
		{
			return EWatchVerdict::Safe;
		}
		return In.Now - State.UnsafeSince >= UnsafeGraceSeconds - KINDA_SMALL_NUMBER ? EWatchVerdict::Rescue : EWatchVerdict::Pending;
	}

	inline void ResetAfterRescue(FWatchState& State)
	{
		State.UnsafeSince = -1.0;
	}

	/** Índice de la muestra con mayor S ≤ Progress (0 si va por detrás de la salida; INDEX_NONE sin camino). */
	inline int32 RescueSampleIndex(const TArray<float>& MainS, float Progress)
	{
		if (MainS.Num() == 0)
		{
			return INDEX_NONE;
		}
		int32 Best = 0;
		for (int32 i = 0; i < MainS.Num() && MainS[i] <= Progress; ++i)
		{
			Best = i;
		}
		return Best;
	}

	/** Candidatos: la muestra, RescueBackSamples muestras hacia atrás por el camino y, por último, a los lados de la muestra. */
	inline void RescueCandidates(const TArray<FVector>& MainPos, const TArray<FVector2D>& MainDir, int32 Index, double HalfWidth,
		TArray<FVector>& Out)
	{
		Out.Reset();
		if (!MainPos.IsValidIndex(Index))
		{
			return;
		}
		for (int32 Back = 0; Back <= RescueBackSamples && Index - Back >= 0; ++Back)
		{
			Out.Add(MainPos[Index - Back]);
		}
		const FVector2D D = MainDir.IsValidIndex(Index) ? MainDir[Index] : FVector2D(1.0, 0.0);
		const FVector Across(-D.Y, D.X, 0.0);
		for (double Side = RescueLateralStep; Side <= HalfWidth; Side += RescueLateralStep)
		{
			Out.Add(MainPos[Index] + Across * Side);
			Out.Add(MainPos[Index] - Across * Side);
		}
	}

	/** El primer candidato a MinSeparation (en planta) de todo lo ocupado; INDEX_NONE si ninguno. */
	inline int32 PickRescueSlot(const TArray<FVector>& Candidates, const TArray<FVector>& Occupied, double MinSeparation)
	{
		const double MinSq = MinSeparation * MinSeparation;
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			const FVector& C = Candidates[i];
			if (!Occupied.ContainsByPredicate([&C, MinSq](const FVector& O) { return FVector::DistSquared2D(C, O) < MinSq; }))
			{
				return i;
			}
		}
		return INDEX_NONE;
	}
}
```

- [ ] **Paso 4: ver pasar.** BUILD; UETEST `Coop` → `Coop.Watch`, `Coop.WatchBusyMover`, `Coop.RescueSample`, `Coop.RescueSlot` en `Success`.
- [ ] **Paso 5: commit.** `git add Source/Tortunabo/Public/World/ProcMap/TN_CoopRescueDecisions.h Source/Tortunabo/Private/Tests/TN_CoopRescueDecisionsTest.cpp` · `git commit -m "feat(coop): lógica del vigilante fuera de zona y del sitio de rescate"`

### Tarea 16: Proveedor de suelo seguro, subsistema y `Source=Prepared` en el generador

**Files:**
- Create: `Source/Tortunabo/Public/World/TN_SafeGroundProvider.h`, `Source/Tortunabo/Public/World/TN_SafeGroundSubsystem.h`, `Source/Tortunabo/Private/World/TN_SafeGroundSubsystem.cpp`, `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Prepared.cpp`
- Modify: `Source/Tortunabo/Public/World/ProcMap/TN_ProcMapEnums.h` (tras `ETNProcDifficulty`), `Source/Tortunabo/Public/World/ProcMap/TN_ProcMapGenerator.h:83-93, 186-230`, `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator.cpp:59-67, 159-176`, `Source/Tortunabo/Private/Tests/TN_CoopMapDataTest.cpp`

**Interfaces:**
- Consumes: `UTN_CoopMapData` (Tarea 14), `TN_CoopGridDecisions.h` (13), `TN_CoopRescueDecisions.h` (15).
- Produces: `UENUM ETNTerrainSource : uint8 { Procedural, Prepared }`; `class ITN_SafeGroundProvider { virtual bool IsInBounds(const FVector& Feet) const = 0; virtual bool IsAllowed(const FVector& Feet) const = 0; virtual float WalkableZAt(const FVector2D& XY, float ZHint) const = 0; virtual float GetProgressAt(const FVector& Location) const = 0; virtual FVector CourseBackAt(const FVector& Location) const = 0; virtual bool FindRescueSpot(const ACharacter* Turtle, float LastSafeProgress, const TArray<FVector>& Reserved, FTransform& OutTransform) const = 0; }`; `UTN_SafeGroundSubsystem::RegisterProvider(UObject*)`, `UnregisterProvider(UObject*)`, `static const ITN_SafeGroundProvider* FindProvider(const UObject* WorldContext)`; `ATN_ProcMapGenerator : ITN_SafeGroundProvider`, `bool IsPrepared() const`, `void SetPreparedSource(UTN_CoopMapData*)`, `UTN_CoopMapData* GetCoopData() const`, `void BuildPreparedFromNetConfig()` (F1: solo lista el mapa; la Tarea 27 la completa).

- [ ] **Paso 1: test que falla.** Añadir a `TN_CoopMapDataTest.cpp` (includes: `Engine/Engine.h`, `Engine/World.h`, `World/ProcMap/TN_ProcMapGenerator.h`, `World/TN_SafeGroundSubsystem.h`):

```cpp
namespace TNCoopMapDataTest
{
	/** Mundo de juego vacío con un generador Prepared sobre el mapa T01 (sin colisión: el suelo sale del bake). */
	struct FPreparedWorld
	{
		UWorld* World = nullptr;
		ATN_ProcMapGenerator* Generator = nullptr;

		explicit FPreparedWorld(UTN_CoopMapData* Data)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNCoopTestWorld"));
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			const FTransform At(FVector(0.0, 0.0, -400.0));
			Generator = World->SpawnActorDeferred<ATN_ProcMapGenerator>(ATN_ProcMapGenerator::StaticClass(), At);
			Generator->SetPreparedSource(Data);
			Generator->FinishSpawning(At);
			Generator->DispatchBeginPlay();
		}

		~FPreparedWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	UTN_CoopMapData* LoadT01(FAutomationTestBase& Test)
	{
		UTN_CoopMapData* Data = NewObject<UTN_CoopMapData>();
		FString Error;
		Test.TestTrue(TEXT("Carga T01"), Data->LoadFromMemory(TNCoopTestUtil::MakeManifestJson(),
			TNCoopTestUtil::MakeBakeBytes(TNCoopTestUtil::MakeGrid(21, 21, 20)), Error));
		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopProviderRegistrationTest, "Tortunabo.Coop.ProviderRegistration",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopProviderRegistrationTest::RunTest(const FString& Parameters)
{
	TNCoopMapDataTest::FPreparedWorld Scene(TNCoopMapDataTest::LoadT01(*this));
	const ITN_SafeGroundProvider* Provider = UTN_SafeGroundSubsystem::FindProvider(Scene.World);
	if (!TestNotNull(TEXT("El generador Prepared se registra como proveedor"), Provider)) { return false; }
	TestTrue(TEXT("Suelo permitido"), Provider->IsAllowed(FVector(1000.0, 1000.0, 0.0)));
	TestFalse(TEXT("Techo no permitido"), Provider->IsAllowed(FVector(1000.0, 1000.0, 1500.0)));
	TestFalse(TEXT("Fuera de límites"), Provider->IsInBounds(FVector(3000.0, 1000.0, 0.0)));
	TestEqual(TEXT("Suelo sin colisión: el del bake"), Provider->WalkableZAt(FVector2D(1000.0, 1000.0), 1400.f), 0.f);
	TestEqual(TEXT("Progreso en la muestra central"), Provider->GetProgressAt(FVector(1000.0, 1000.0, 0.0)), 900.f);
	TestTrue(TEXT("Hacia atrás = −X"), Provider->CourseBackAt(FVector(1000.0, 1000.0, 0.0)).Equals(FVector(-1.0, 0.0, 0.0)));
	return true;
}
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila (`TN_SafeGroundSubsystem.h`, `SetPreparedSource`).
- [ ] **Paso 3: enum e interfaz.** En `TN_ProcMapEnums.h`, tras `ETNProcDifficulty`:

```cpp
/** De dónde sale el terreno del generador: procedural (por semilla) o preparado (nivel fijo + UTN_CoopMapData). */
UENUM(BlueprintType)
enum class ETNTerrainSource : uint8
{
	Procedural UMETA(DisplayName = "Procedural"),
	Prepared   UMETA(DisplayName = "Preparado (mapa fijo)")
};
```

  `Source/Tortunabo/Public/World/TN_SafeGroundProvider.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TN_SafeGroundProvider.generated.h"

class ACharacter;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTN_SafeGroundProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Fachada de «suelo válido» del mapa en juego (plan maestro §2.2): zonas permitidas, suelo caminable bajo una cota
 * (sin techos de túnel), progreso, dirección de vuelta y sitio de rescate. La implementa ATN_ProcMapGenerator en
 * Prepared; la resuelve UTN_SafeGroundSubsystem. Todo en espacio de mundo; solo lo usa el servidor salvo WalkableZAt.
 */
class TORTUNABO_API ITN_SafeGroundProvider
{
	GENERATED_BODY()

public:
	virtual bool IsInBounds(const FVector& Feet) const = 0;
	virtual bool IsAllowed(const FVector& Feet) const = 0;
	/** Suelo bajo ZHint acotado a los niveles permitidos, trazando desde como mucho ZHint + 2 m. */
	virtual float WalkableZAt(const FVector2D& XY, float ZHint) const = 0;
	virtual float GetProgressAt(const FVector& Location) const = 0;
	/** Hacia la salida por el camino (−dirección del camino más cercano), en planta y unitario. */
	virtual FVector CourseBackAt(const FVector& Location) const = 0;
	/** Servidor: sitio de rescate (cápsula de pie) en la muestra con mayor S ≤ LastSafeProgress, lejos de Reserved. */
	virtual bool FindRescueSpot(const ACharacter* Turtle, float LastSafeProgress, const TArray<FVector>& Reserved,
		FTransform& OutTransform) const = 0;
};
```

- [ ] **Paso 4: subsistema.** `Source/Tortunabo/Public/World/TN_SafeGroundSubsystem.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TN_SafeGroundSubsystem.generated.h"

class ITN_SafeGroundProvider;

/** Un proveedor de suelo seguro por mundo (el generador del mapa preparado). Sin proveedor, cada sistema sigue con lo suyo. */
UCLASS()
class TORTUNABO_API UTN_SafeGroundSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterProvider(UObject* Provider);
	void UnregisterProvider(UObject* Provider);
	const ITN_SafeGroundProvider* GetProvider() const;

	/** El proveedor del mundo de WorldContext, o nullptr. */
	static const ITN_SafeGroundProvider* FindProvider(const UObject* WorldContext);

private:
	TWeakObjectPtr<UObject> ProviderObject;
};
```

  `Source/Tortunabo/Private/World/TN_SafeGroundSubsystem.cpp`:

```cpp
#include "World/TN_SafeGroundSubsystem.h"

#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "World/TN_SafeGroundProvider.h"

void UTN_SafeGroundSubsystem::RegisterProvider(UObject* Provider)
{
	if (!Provider || !Cast<ITN_SafeGroundProvider>(Provider))
	{
		UE_LOG(LogTortunabo, Error, TEXT("[SafeGround] %s no implementa ITN_SafeGroundProvider."), *GetNameSafe(Provider));
		return;
	}
	if (ProviderObject.IsValid() && ProviderObject.Get() != Provider)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[SafeGround] %s sustituye a %s como proveedor."), *GetNameSafe(Provider), *GetNameSafe(ProviderObject.Get()));
	}
	ProviderObject = Provider;
}

void UTN_SafeGroundSubsystem::UnregisterProvider(UObject* Provider)
{
	if (ProviderObject.Get() == Provider)
	{
		ProviderObject.Reset();
	}
}

const ITN_SafeGroundProvider* UTN_SafeGroundSubsystem::GetProvider() const
{
	return Cast<ITN_SafeGroundProvider>(ProviderObject.Get());
}

const ITN_SafeGroundProvider* UTN_SafeGroundSubsystem::FindProvider(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UTN_SafeGroundSubsystem* Subsystem = World ? World->GetSubsystem<UTN_SafeGroundSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetProvider() : nullptr;
}
```

- [ ] **Paso 5: generador.** En `TN_ProcMapGenerator.h`: `#include "World/TN_SafeGroundProvider.h"`, `class UTN_CoopMapData;`, la clase pasa a `class TORTUNABO_API ATN_ProcMapGenerator : public AActor, public ITN_SafeGroundProvider`; en `public:` tras `BeginPlay`:

```cpp
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Mapa preparado (Coop sobre mapas fijos): el terreno es del nivel; camino, zonas y máscara salen de CoopData. */
	bool IsPrepared() const { return Source == ETNTerrainSource::Prepared && CoopData != nullptr; }
	void SetPreparedSource(UTN_CoopMapData* InData) { Source = ETNTerrainSource::Prepared; CoopData = InData; }
	UTN_CoopMapData* GetCoopData() const { return CoopData; }

	// ITN_SafeGroundProvider (solo en Prepared)
	virtual bool IsInBounds(const FVector& Feet) const override;
	virtual bool IsAllowed(const FVector& Feet) const override;
	virtual float WalkableZAt(const FVector2D& XY, float ZHint) const override;
	virtual float GetProgressAt(const FVector& Location) const override;
	virtual FVector CourseBackAt(const FVector& Location) const override;
	virtual bool FindRescueSpot(const ACharacter* Turtle, float LastSafeProgress, const TArray<FVector>& Reserved,
		FTransform& OutTransform) const override;
```

  en la sección de propiedades (tras `bTerrainOnly`, línea 215):

```cpp
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ProcMap|Prepared")
	ETNTerrainSource Source = ETNTerrainSource::Procedural;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ProcMap|Prepared", meta = (EditCondition = "Source == ETNTerrainSource::Prepared"))
	TObjectPtr<UTN_CoopMapData> CoopData;
```

  y en `private:` junto a `BuildFromNetConfig`: `void BuildPreparedFromNetConfig();`. En `TN_ProcMapGenerator.cpp` (includes `World/TN_SafeGroundSubsystem.h` y `World/ProcMap/TN_CoopMapData.h`): `BeginPlay` (línea 59) añade al final:

```cpp
	if (IsPrepared())
	{
		if (UTN_SafeGroundSubsystem* Safe = GetWorld()->GetSubsystem<UTN_SafeGroundSubsystem>())
		{
			Safe->RegisterProvider(this);
		}
		UE_LOG(LogTortunabo, Log, TEXT("[Coop] Proveedor de suelo seguro registrado (%s)."), *CoopData->MapId.ToString());
	}
```

  y en `BuildFromNetConfig` (línea 165), justo después de comprobar `World`:

```cpp
	if (IsPrepared())
	{
		BuildPreparedFromNetConfig();
		return;
	}
```

  Crear `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Prepared.cpp`:

```cpp
// Rama Prepared de ATN_ProcMapGenerator (Coop sobre mapas fijos, plan F0–F2): proveedor de suelo seguro y construcción.

#include "World/ProcMap/TN_ProcMapGenerator.h"

#include "Components/CapsuleComponent.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "World/ProcMap/TN_CoopMapData.h"
#include "World/ProcMap/TN_CoopRescueDecisions.h"
#include "World/TN_SafeGroundSubsystem.h"

void ATN_ProcMapGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UTN_SafeGroundSubsystem* Safe = World->GetSubsystem<UTN_SafeGroundSubsystem>())
		{
			Safe->UnregisterProvider(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ATN_ProcMapGenerator::BuildPreparedFromNetConfig()
{
	// F1: el terreno y su colisión vienen en el nivel; el layout de juego (nidos, meta, peligros) llega en la Tarea 27.
	Clear();
	bMapReady = true;
	bReportedReady = false;
	BuiltGeneration = NetConfig.Generation;
	UE_LOG(LogTortunabo, Log, TEXT("[Coop] Mapa preparado %s listo (generación %d)."), *CoopData->MapId.ToString(), BuiltGeneration);
	OnMapGeneratedNative.Broadcast(BuiltGeneration);
	OnMapGenerated.Broadcast(BuiltGeneration);
	if (GetWorld()->IsGameWorld() && GetNetMode() == NM_Client)
	{
		ReportReadyToServer();
	}
}

bool ATN_ProcMapGenerator::IsInBounds(const FVector& Feet) const
{
	return IsPrepared() && TNCoopMap::IsInBounds(CoopData->GetSafeGrid(), Feet);
}

bool ATN_ProcMapGenerator::IsAllowed(const FVector& Feet) const
{
	return IsPrepared() && TNCoopMap::IsAllowed(CoopData->GetSafeGrid(), Feet);
}

float ATN_ProcMapGenerator::WalkableZAt(const FVector2D& XY, float ZHint) const
{
	if (!IsPrepared())
	{
		return ZHint;
	}
	const TNCoopMap::FSafeGrid& Grid = CoopData->GetSafeGrid();
	const TNCoopMap::FTraceWindow Window = TNCoopMap::WalkableTraceWindow(Grid, XY, ZHint);
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(TNCoopWalkableZ), true);
	const UWorld* World = GetWorld();
	if (World && World->LineTraceSingleByChannel(Hit, FVector(XY, Window.Top), FVector(XY, Window.Bottom), ECC_WorldStatic, Params)
		&& Hit.GetActor() && Hit.GetActor()->ActorHasTag(TNCoopMap::TerrainTag()))
	{
		return static_cast<float>(Hit.ImpactPoint.Z);
	}
	return static_cast<float>(TNCoopMap::FallbackWalkableZ(Grid, XY, ZHint));
}

float ATN_ProcMapGenerator::GetProgressAt(const FVector& Location) const
{
	if (!IsPrepared())
	{
		return GetPathProgress(Location);
	}
	const int32 Index = CoopData->NearestMainIndex(Location);
	return CoopData->Main.IsValidIndex(Index) ? CoopData->Main[Index].S : 0.f;
}

FVector ATN_ProcMapGenerator::CourseBackAt(const FVector& Location) const
{
	if (!IsPrepared())
	{
		return -GetActorForwardVector().GetSafeNormal2D();
	}
	const FVector2D Dir = CoopData->MainDirAt(CoopData->NearestMainIndex(Location));
	return FVector(-Dir.X, -Dir.Y, 0.0).GetSafeNormal();
}

bool ATN_ProcMapGenerator::FindRescueSpot(const ACharacter* Turtle, float LastSafeProgress, const TArray<FVector>& Reserved,
	FTransform& OutTransform) const
{
	const UWorld* World = GetWorld();
	if (!IsPrepared() || !Turtle || !World)
	{
		return false;
	}
	TArray<float> S;
	TArray<FVector> Pos;
	TArray<FVector2D> Dir;
	for (int32 i = 0; i < CoopData->Main.Num(); ++i)
	{
		S.Add(CoopData->Main[i].S);
		Pos.Add(CoopData->Main[i].Pos);
		Dir.Add(CoopData->MainDirAt(i));
	}
	const int32 Index = TNCoopMap::RescueSampleIndex(S, LastSafeProgress);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	TArray<FVector> Candidates;
	TNCoopMap::RescueCandidates(Pos, Dir, Index, CoopData->Main[Index].Width * 0.5, Candidates);
	TArray<FVector> Occupied = Reserved;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		if (*It != Turtle) { Occupied.Add(It->GetActorLocation()); }
	}
	const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
	const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 34.f;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TNCoopRescueSpot), false, Turtle);
	for (int32 Tries = 0; Tries < Candidates.Num(); ++Tries)
	{
		const int32 Slot = TNCoopMap::PickRescueSlot(Candidates, Occupied, TNCoopMap::RescueMinSeparation);
		if (Slot == INDEX_NONE) { return false; }
		const FVector C = Candidates[Slot];
		const FVector Stand(C.X, C.Y, WalkableZAt(FVector2D(C.X, C.Y), static_cast<float>(C.Z)) + HalfHeight + 5.0);
		Occupied.Add(C);
		if (World->OverlapBlockingTestByChannel(Stand, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight), Query))
		{
			continue;
		}
		const FVector2D Forward = Dir[Index];
		OutTransform = FTransform(FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(Forward.Y, Forward.X)), 0.0), Stand);
		UE_LOG(LogTortunabo, Verbose, TEXT("[Coop] Sitio de rescate a %.0f cm del camino."), FVector::Dist2D(Stand, Pos[Index]));
		return true;
	}
	return false;
}
```

  (Include adicional en el `.cpp`: `EngineUtils.h` para `TActorIterator`.)
- [ ] **Paso 6: ver pasar.** BUILD; UETEST `Coop` → todos `Success`, incluido `Coop.ProviderRegistration`; UETEST `ProcMap` → sin regresiones (el procedural no cambia).
- [ ] **Paso 7: commit.** `git add Source/Tortunabo/Public/World/TN_SafeGroundProvider.h Source/Tortunabo/Public/World/TN_SafeGroundSubsystem.h Source/Tortunabo/Private/World/TN_SafeGroundSubsystem.cpp Source/Tortunabo/Public/World/ProcMap/TN_ProcMapEnums.h Source/Tortunabo/Public/World/ProcMap/TN_ProcMapGenerator.h Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator.cpp Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Prepared.cpp Source/Tortunabo/Private/Tests/TN_CoopMapDataTest.cpp` · `git commit -m "feat(coop): proveedor de suelo seguro y fuente Prepared en el generador del mapa"`

### Tarea 17: `TNBeach::FindOpenSandSpot` y `DepthUnderTerrain` consultan antes al proveedor

**Files:**
- Modify: `Source/Tortunabo/Private/World/Beach/TN_BeachStun.cpp:368-455`, `Source/Tortunabo/Private/Tests/TN_CoopMapDataTest.cpp`

**Interfaces:**
- Consumes: `UTN_SafeGroundSubsystem::FindProvider`, `ITN_SafeGroundProvider::WalkableZAt/IsAllowed` (Tarea 16).
- Produces: comportamiento nuevo de `TNBeach::FindOpenSandSpot(const ACharacter*, const FVector&, float, FTransform&, const TArray<FVector>*, float)` y `TNBeach::DepthUnderTerrain(const UObject*, const FVector&)` con proveedor; helpers `TNBeachStunDetail::TurtleStandingCapsule(const ACharacter*, float&, float&)`, `TNBeachStunDetail::TurtleQuery(const ACharacter*) -> FCollisionQueryParams`, `TNBeachStunDetail::FindOpenSpotWithProvider(...)`.

- [ ] **Paso 1: test que falla.** Añadir a `TN_CoopMapDataTest.cpp` (include `World/Beach/TN_BeachStun.h`, `GameFramework/Character.h`):

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopOpenSpotWithProviderTest, "Tortunabo.Coop.OpenSpotWithProvider",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopOpenSpotWithProviderTest::RunTest(const FString& Parameters)
{
	TNCoopMapDataTest::FPreparedWorld Scene(TNCoopMapDataTest::LoadT01(*this));
	ACharacter* Turtle = Scene.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(1000.0, 1000.0, 200.0)));
	FTransform Spot;
	TestTrue(TEXT("Hay arena abierta en el camino del mapa preparado"),
		TNBeach::FindOpenSandSpot(Turtle, FVector(1000.0, 1000.0, 0.0), 0.f, Spot));
	TestEqual(TEXT("De pie sobre el suelo del bake"), Spot.GetLocation().Z,
		static_cast<double>(Turtle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) + 5.0, 1.0);
	TestFalse(TEXT("Fuera de los límites del mapa, no"), TNBeach::FindOpenSandSpot(Turtle, FVector(5000.0, 5000.0, 0.0), 0.f, Spot));
	TestEqual(TEXT("Profundidad bajo el terreno con proveedor"), TNBeach::DepthUnderTerrain(Turtle, FVector(1000.0, 1000.0, -30.0)), 30.f, 0.5f);
	return true;
}
```

- [ ] **Paso 2: ver el fallo.** BUILD; UETEST `Coop.OpenSpotWithProvider` → `Result={Fail}` («Hay arena abierta…» falso: sin `ATN_BeachRaceGenerator` devuelve false).
- [ ] **Paso 3: implementación.** En `TN_BeachStun.cpp`: includes `World/TN_SafeGroundProvider.h`, `World/TN_SafeGroundSubsystem.h`. Dentro de `namespace TNBeachStunDetail` (el existente), añadir:

```cpp
	void TurtleStandingCapsule(const ACharacter* Turtle, float& OutHalfHeight, float& OutRadius)
	{
		const ACharacter* Defaults = Turtle->GetClass()->GetDefaultObject<ACharacter>();
		const UCapsuleComponent* DefaultCapsule = Defaults ? Defaults->GetCapsuleComponent() : nullptr;
		const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
		OutHalfHeight = DefaultCapsule ? DefaultCapsule->GetScaledCapsuleHalfHeight() : (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f);
		OutRadius = DefaultCapsule ? DefaultCapsule->GetScaledCapsuleRadius() : (Capsule ? Capsule->GetScaledCapsuleRadius() : 34.f);
	}

	FCollisionQueryParams TurtleQuery(const ACharacter* Turtle)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(TNBeachOpenSand), false, Turtle);
		if (const ATortugaCharacter* TurtleCharacter = Cast<ATortugaCharacter>(Turtle))
		{
			if (const UTN_ShellComponent* Shell = TurtleCharacter->GetShellComponent())
			{
				Query.AddIgnoredActor(Shell->GetBody());
			}
		}
		return Query;
	}

	/** Mapa preparado: anillos alrededor de Desired, suelo del proveedor (sin techos de túnel) y solo zona permitida. */
	bool FindOpenSpotWithProvider(const ITN_SafeGroundProvider& Provider, UWorld& World, const ACharacter* Turtle,
		const FVector& Desired, float SearchRadius, FTransform& OutTransform, const TArray<FVector>* Avoid, float AvoidRadius)
	{
		float HalfHeight = 88.f;
		float Radius = 34.f;
		TurtleStandingCapsule(Turtle, HalfHeight, Radius);
		const FCollisionQueryParams Query = TurtleQuery(Turtle);
		const double AvoidSq = FMath::Square(static_cast<double>(AvoidRadius));
		const int32 Rings = SearchRadius > 0.f ? FMath::Max(1, FMath::CeilToInt32(SearchRadius / SpotRingStep)) : 0;
		for (int32 Ring = 0; Ring <= Rings; ++Ring)
		{
			const int32 Count = Ring == 0 ? 1 : FMath::Min(8 + 4 * (Ring - 1), 24);
			for (int32 k = 0; k < Count; ++k)
			{
				const double Angle = (Ring % 2 == 0 ? 0.0 : 0.5) * UE_DOUBLE_TWO_PI / Count + UE_DOUBLE_TWO_PI * k / Count;
				const FVector Point = Desired + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * (Ring * SpotRingStep);
				if (Avoid && AvoidSq > 0.0 && Avoid->ContainsByPredicate([&Point, AvoidSq](const FVector& Bad) { return FVector::DistSquared2D(Point, Bad) < AvoidSq; }))
				{
					continue;
				}
				const FVector Feet(Point.X, Point.Y, Provider.WalkableZAt(FVector2D(Point.X, Point.Y), static_cast<float>(Desired.Z)));
				const FVector Stand = Feet + FVector(0.0, 0.0, HalfHeight + 5.0);
				if (!Provider.IsAllowed(Feet) || IsWaterAt(World, Stand)
					|| World.OverlapBlockingTestByChannel(Stand, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius + SpotCapsulePad, HalfHeight), Query))
				{
					continue;
				}
				OutTransform = FTransform(FRotator(0.f, Turtle->GetActorRotation().Yaw, 0.f), Stand);
				return true;
			}
		}
		return false;
	}
```

  (`IsWaterAt(*World, Stand)` ya se llama así en la línea 431: `World` es `UWorld&` en ambos sitios.) En `TNBeach::FindOpenSandSpot`, tras `UWorld* World = …` (línea 372), antes de buscar `ATN_BeachRaceGenerator`:

```cpp
	if (const ITN_SafeGroundProvider* Provider = UTN_SafeGroundSubsystem::FindProvider(Turtle))
	{
		return World && FindOpenSpotWithProvider(*Provider, *World, Turtle, Desired, SearchRadius, OutTransform, Avoid, AvoidRadius);
	}
```

  y las líneas 378-392 (cápsula y `Query`) pasan a `float HalfHeight = 88.f; float Radius = 34.f; TurtleStandingCapsule(Turtle, HalfHeight, Radius); const FCollisionQueryParams Query = TurtleQuery(Turtle);`. En `TNBeach::DepthUnderTerrain` (línea 442), al principio:

```cpp
	if (const ITN_SafeGroundProvider* Provider = UTN_SafeGroundSubsystem::FindProvider(WorldContext))
	{
		return Provider->WalkableZAt(FVector2D(Probe.X, Probe.Y), static_cast<float>(Probe.Z)) - static_cast<float>(Probe.Z);
	}
```

- [ ] **Paso 4: ver pasar.** BUILD; UETEST `Coop` y UETEST `Beach` → todo `Success` (la playa sin proveedor no cambia).
- [ ] **Paso 5: commit.** `git add Source/Tortunabo/Private/World/Beach/TN_BeachStun.cpp Source/Tortunabo/Private/Tests/TN_CoopMapDataTest.cpp` · `git commit -m "feat(coop): arena abierta y profundidad bajo el terreno con el proveedor de suelo seguro"`

### Tarea 18: Vigilante de servidor en `ATN_ProcMapGameMode` y vuelta de las gaviotas por el camino

**Files:**
- Create: `Source/Tortunabo/Public/Game/TN_OutOfZoneWatchComponent.h`, `Source/Tortunabo/Private/Game/TN_OutOfZoneWatchComponent.cpp`
- Modify: `Source/Tortunabo/Public/Game/TN_ProcMapGameMode.h:47` (constructor/propiedad), `Source/Tortunabo/Private/Game/TN_ProcMapGameMode.cpp:40-66` (constructor), `Source/Tortunabo/Public/World/Beach/TN_BeachGullZone.h` (declaración privada), `Source/Tortunabo/Private/World/Beach/TN_BeachGullZone.cpp:1263`

**Interfaces:**
- Consumes: `TNCoopMap::StepWatch`, `IsMoverBusyForRescue`, `ResetAfterRescue`, `WatchPeriodSeconds` (15); `ITN_SafeGroundProvider` (16); `TNBeach::GetTurtleMover`, `RelocateTurtle`, `StunTurtle`, `ClaimTurtle` (`TN_BeachStun.h`).
- Produces: `UTN_OutOfZoneWatchComponent` (`GetRescueCount() const -> int32`, `RescueStunSeconds = 1.0f`, `RescueClaimSeconds = 0.75f`); `ATN_ProcMapGameMode::OutOfZoneWatch`; `FVector ATN_BeachGullZone::CourseBackFor(const FVector& Where) const`.

- [ ] **Paso 1: verificación previa (no hay test automático de integración).** La lógica ya está cubierta por `Coop.Watch*` y `Coop.RescueSlot`; esta tarea es cableado de servidor. Su comprobación es la Tarea 34 (PIE 4P) y el smoke de la Tarea 25. Anotar el punto de partida: `grep -c "\[Coop\] Rescate" Saved/Logs/Tortunabo.log` → 0.
- [ ] **Paso 2: componente.** `Source/Tortunabo/Public/Game/TN_OutOfZoneWatchComponent.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "World/ProcMap/TN_CoopRescueDecisions.h"
#include "TN_OutOfZoneWatchComponent.generated.h"

class ATortugaCharacter;
class ITN_SafeGroundProvider;

/**
 * Servidor: vigila cada WatchPeriodSeconds a las tortugas del mapa preparado y rescata a la que lleva UnsafeGraceSeconds
 * en suelo no permitido o sale de los límites (plan maestro §2.3). Sin proveedor de suelo seguro no hace nada: en la
 * playa y en el procedural mandan sus propias reglas. Agua y barrancos siguen siendo muerte por cajas.
 */
UCLASS(ClassGroup = (Tortunabo), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_OutOfZoneWatchComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_OutOfZoneWatchComponent();

	int32 GetRescueCount() const { return RescueCount; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Mareo tras el rescate (pajaritos del aturdimiento de la playa). */
	UPROPERTY(EditDefaultsOnly, Category = "Coop|Rescate", meta = (ClampMin = "0.0"))
	float RescueStunSeconds = 1.f;

	/** Reserva SafetyNet tras el rescate: nada la relanza mientras. */
	UPROPERTY(EditDefaultsOnly, Category = "Coop|Rescate", meta = (ClampMin = "0.0"))
	float RescueClaimSeconds = 0.75f;

private:
	void TickWatch();
	void WatchTurtle(ATortugaCharacter* Turtle, const ITN_SafeGroundProvider& Provider, double Now, TArray<FVector>& Reserved);
	void Rescue(ATortugaCharacter* Turtle, const ITN_SafeGroundProvider& Provider, TNCoopMap::FWatchState& State, double Now,
		TArray<FVector>& Reserved);

	TMap<TWeakObjectPtr<ATortugaCharacter>, TNCoopMap::FWatchState> States;
	FTimerHandle WatchHandle;
	int32 RescueCount = 0;
};
```

  `Source/Tortunabo/Private/Game/TN_OutOfZoneWatchComponent.cpp`:

```cpp
#include "Game/TN_OutOfZoneWatchComponent.h"

#include "Components/CapsuleComponent.h"
#include "Core/TN_Log.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/TortugaCharacter.h"
#include "TimerManager.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/TN_SafeGroundProvider.h"
#include "World/TN_SafeGroundSubsystem.h"

UTN_OutOfZoneWatchComponent::UTN_OutOfZoneWatchComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTN_OutOfZoneWatchComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		GetWorld()->GetTimerManager().SetTimer(WatchHandle, this, &UTN_OutOfZoneWatchComponent::TickWatch,
			static_cast<float>(TNCoopMap::WatchPeriodSeconds), true);
	}
}

void UTN_OutOfZoneWatchComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WatchHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UTN_OutOfZoneWatchComponent::TickWatch()
{
	UWorld* World = GetWorld();
	const ITN_SafeGroundProvider* Provider = UTN_SafeGroundSubsystem::FindProvider(World);
	if (!World || !Provider)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	TArray<FVector> Reserved;
	for (TActorIterator<ATortugaCharacter> It(World); It; ++It)
	{
		if (!It->IsDead())
		{
			WatchTurtle(*It, *Provider, Now, Reserved);
		}
	}
}

void UTN_OutOfZoneWatchComponent::WatchTurtle(ATortugaCharacter* Turtle, const ITN_SafeGroundProvider& Provider, double Now,
	TArray<FVector>& Reserved)
{
	const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
	const UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
	const TNBeach::ETNBeachMover Mover = TNBeach::GetTurtleMover(Turtle);
	TNCoopMap::FWatchInput In;
	In.Now = Now;
	In.Feet = Turtle->GetActorLocation() - FVector(0.0, 0.0, Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f);
	In.Progress = Provider.GetProgressAt(Turtle->GetActorLocation());
	In.bInBounds = Provider.IsInBounds(In.Feet);
	In.bAllowed = Provider.IsAllowed(In.Feet);
	// En su bola no hay suelo del CMC: cuenta como apoyada (una bola quieta en un pozo también se rescata).
	In.bGrounded = (Move && Move->IsMovingOnGround()) || Mover == TNBeach::ETNBeachMover::Ball;
	In.bMoverBusy = TNCoopMap::IsMoverBusyForRescue(Mover);
	TNCoopMap::FWatchState& State = States.FindOrAdd(Turtle);
	if (TNCoopMap::StepWatch(State, In) == TNCoopMap::EWatchVerdict::Rescue)
	{
		Rescue(Turtle, Provider, State, Now, Reserved);
	}
}

void UTN_OutOfZoneWatchComponent::Rescue(ATortugaCharacter* Turtle, const ITN_SafeGroundProvider& Provider, TNCoopMap::FWatchState& State,
	double Now, TArray<FVector>& Reserved)
{
	const double Outside = State.UnsafeSince >= 0.0 ? Now - State.UnsafeSince : 0.0;
	TNCoopMap::ResetAfterRescue(State);
	FTransform Where;
	if (!Provider.FindRescueSpot(Turtle, State.LastSafeProgress, Reserved, Where))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Coop] Rescate sin sitio libre para %s (S=%.0f)."), *GetNameSafe(Turtle), State.LastSafeProgress);
		return;
	}
	TNBeach::RelocateTurtle(Turtle, Where);
	TNBeach::StunTurtle(Turtle, RescueStunSeconds);
	TNBeach::ClaimTurtle(Turtle, TNBeach::ETNBeachMover::SafetyNet, RescueClaimSeconds);
	Reserved.Add(Where.GetLocation());
	++RescueCount;
	UE_LOG(LogTortunabo, Log, TEXT("[Coop] Rescate #%d %s: %.2f s fuera de zona → S=%.0f en %s"), RescueCount, *GetNameSafe(Turtle),
		Outside, State.LastSafeProgress, *Where.GetLocation().ToCompactString());
}
```

- [ ] **Paso 3: cablear en el GameMode.** `TN_ProcMapGameMode.h`: `class UTN_OutOfZoneWatchComponent;` y en `protected:`

```cpp
	/** Servidor: rescate fuera de zona en mapas preparados (sin proveedor de suelo seguro, inactivo). */
	UPROPERTY(VisibleAnywhere, Category = "ProcMap|Coop")
	TObjectPtr<UTN_OutOfZoneWatchComponent> OutOfZoneWatch;
```

  `TN_ProcMapGameMode.cpp`: `#include "Game/TN_OutOfZoneWatchComponent.h"` y, en el constructor, `OutOfZoneWatch = CreateDefaultSubobject<UTN_OutOfZoneWatchComponent>(TEXT("OutOfZoneWatch"));`.
- [ ] **Paso 4: gaviotas.** `TN_BeachGullZone.h`, en `private:`: `/** Hacia la salida desde Where: por el camino del mapa preparado si lo hay; si no, CourseBack. */ FVector CourseBackFor(const FVector& Where) const;`. En `TN_BeachGullZone.cpp` (includes del proveedor y el subsistema):

```cpp
FVector ATN_BeachGullZone::CourseBackFor(const FVector& Where) const
{
	if (const ITN_SafeGroundProvider* Provider = UTN_SafeGroundSubsystem::FindProvider(this))
	{
		const FVector Back = Provider->CourseBackAt(Where).GetSafeNormal2D();
		if (!Back.IsNearlyZero())
		{
			return Back;
		}
	}
	return CourseBack;
}
```

  y en `ReleaseCarried` (línea 1263) `CourseBack * ReleaseLaunch` pasa a `CourseBackFor(Carried->GetActorLocation()) * ReleaseLaunch`. La suelta sobre suelo no permitido la recoge el vigilante (1,5 s en el suelo): no hace falta rama propia.
- [ ] **Paso 5: verificar.** BUILD; UETEST `Coop` y `Beach` → `Success` (sin regresión en `Beach.Gull*`).
- [ ] **Paso 6: commit.** `git add Source/Tortunabo/Public/Game/TN_OutOfZoneWatchComponent.h Source/Tortunabo/Private/Game/TN_OutOfZoneWatchComponent.cpp Source/Tortunabo/Public/Game/TN_ProcMapGameMode.h Source/Tortunabo/Private/Game/TN_ProcMapGameMode.cpp Source/Tortunabo/Public/World/Beach/TN_BeachGullZone.h Source/Tortunabo/Private/World/Beach/TN_BeachGullZone.cpp` · `git commit -m "feat(coop): rescate fuera de zona en el servidor y vuelta de las gaviotas por el camino"`

### Tarea 19: `MapCollisionUnder` acepta el terreno por etiqueta (R1)

**Files:**
- Create: `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h`, `Source/Tortunabo/Private/Tests/TN_CoopMapDecisionsTest.cpp`
- Modify: `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator.cpp:343-384`

**Interfaces:**
- Consumes: `TNCoopMap::TerrainTag()` (Tarea 13).
- Produces: `TNCoopMap::IsMapGroundHit(bool bHitGenerator, bool bHitStartStructure, bool bHitTerrainTag) -> bool`; log `[ProcMap] Pawn local liberado tras %.2f s` (medida de la aceptación «ningún pawn congelado > 1 s»).

- [ ] **Paso 1: test que falla.** Crear `Source/Tortunabo/Private/Tests/TN_CoopMapDecisionsTest.cpp`:

```cpp
// Decisiones del mapa preparado (TN_CoopMapDecisions.h): suelo del mapa, meta, layout, id, URL y mecánicas.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Coop; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/ProcMap/TN_CoopMapDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopGroundHitTest, "Tortunabo.Coop.GroundHit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopGroundHitTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Terreno procedural del propio generador"), TNCoopMap::IsMapGroundHit(true, false, false));
	TestTrue(TEXT("Estructura de salida"), TNCoopMap::IsMapGroundHit(false, true, false));
	TestTrue(TEXT("Trozo de terreno preparado (TN_MapTerrain): R1"), TNCoopMap::IsMapGroundHit(false, false, true));
	TestFalse(TEXT("Cualquier otra cosa"), TNCoopMap::IsMapGroundHit(false, false, false));
	return true;
}

#endif
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila.
- [ ] **Paso 3: implementación.** Crear `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_CoopGridDecisions.h"

/**
 * Decisiones del Coop sobre mapas fijos (plan F0–F2) que no son de la rejilla ni del rescate: suelo del mapa bajo el
 * peón (R1), meta (R2/R3), layout sintético, id del mapa, URL de viaje y mecánicas permitidas.
 */
namespace TNCoopMap
{
	/** R1: suelo del mapa = el propio generador (procedural), la estructura de salida o un trozo con TN_MapTerrain. */
	inline bool IsMapGroundHit(bool bHitGenerator, bool bHitStartStructure, bool bHitTerrainTag)
	{
		return bHitGenerator || bHitStartStructure || bHitTerrainTag;
	}
}
```

  En `TN_ProcMapGenerator.cpp`: `#include "World/ProcMap/TN_CoopMapDecisions.h"`; el `return` de `MapCollisionUnder` (línea 382) pasa a:

```cpp
	const AActor* HitActor = Hit.GetActor();
	return bHit && TNCoopMap::IsMapGroundHit(HitActor == this, Cast<ATN_ProcStartStructure>(HitActor) != nullptr,
		HitActor && HitActor->ActorHasTag(TNCoopMap::TerrainTag()));
```

  y en `FreezeLocalPawnUntilReady`, dentro de `else if (!bWaiting && bFrozeLocalPawn)` antes de `ReadySince = -1.0;`:

```cpp
		UE_LOG(LogTortunabo, Log, TEXT("[ProcMap] Pawn local liberado tras %.2f s"), ReadySince >= 0.0 ? World->GetTimeSeconds() - ReadySince : 0.0);
```

- [ ] **Paso 4: ver pasar.** BUILD; UETEST `Coop.GroundHit` → `Success`; UETEST `ProcMap` sin regresiones.
- [ ] **Paso 5: commit.** `git add Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h Source/Tortunabo/Private/Tests/TN_CoopMapDecisionsTest.cpp Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator.cpp` · `git commit -m "fix(procmap): el suelo del mapa preparado (TN_MapTerrain) suelta al peón al aparecer"`

### Tarea 20: Meta orientada por `Dir` y a la cota del marcador (R2, R3)

**Files:**
- Modify: `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h`, `Source/Tortunabo/Private/Tests/TN_CoopMapDecisionsTest.cpp`, `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator.cpp:84`, `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Spawn.cpp:440-449`, `Source/Tortunabo/Public/World/ProcMap/TN_ProcMapGenerator.h` (miembro privado)

**Interfaces:**
- Consumes: `TNProcMap::FFeature` (`Location`, `Dir`, `Length`, `Height`), `TNProcMap::SeaLevel`.
- Produces: `TNCoopMap::IsInsideFinish(const FVector2D& LineCenter, const FVector2D& Dir, double Length, double Width, const FVector2D& P) -> bool`; `TNCoopMap::FinishVolumeZ(bool bUseMarkerZ, double MarkerZ, double SeaLevel) -> double`; `ATN_ProcMapGenerator::bPreparedFinishInWater` (lo rellena la Tarea 27).

- [ ] **Paso 1: test que falla (Review Focus 5).** Añadir a `TN_CoopMapDecisionsTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopFinishTest, "Tortunabo.Coop.Finish",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopFinishTest::RunTest(const FString& Parameters)
{
	using TNCoopMap::IsInsideFinish;
	const FVector2D Line(1000.0, 2000.0);
	// +Y, como el procedural: el mismo resultado que la comprobación antigua (P.Y en [Y, Y + Length] y |ΔX| ≤ ancho/2 + 15 m).
	TestTrue(TEXT("+Y dentro"), IsInsideFinish(Line, FVector2D(0.0, 1.0), 1200.0, 1500.0, FVector2D(1500.0, 2600.0)));
	TestFalse(TEXT("+Y detrás de la línea"), IsInsideFinish(Line, FVector2D(0.0, 1.0), 1200.0, 1500.0, FVector2D(1000.0, 1900.0)));
	TestFalse(TEXT("+Y demasiado al lado"), IsInsideFinish(Line, FVector2D(0.0, 1.0), 1200.0, 1500.0, FVector2D(3400.0, 2600.0)));
	// −X (C01, CP01): antes nunca se disparaba.
	TestTrue(TEXT("−X dentro"), IsInsideFinish(Line, FVector2D(-1.0, 0.0), 1200.0, 1500.0, FVector2D(500.0, 2100.0)));
	TestFalse(TEXT("−X al otro lado de la línea"), IsInsideFinish(Line, FVector2D(-1.0, 0.0), 1200.0, 1500.0, FVector2D(1500.0, 2000.0)));
	// R3: meta seca a +25 m (P01, end_uu z = 2594): el volumen va a su cota, no 30 m por debajo.
	TestEqual(TEXT("Meta seca preparada"), TNCoopMap::FinishVolumeZ(true, 2594.0, 0.0), 2894.0);
	TestEqual(TEXT("Meta en el agua o procedural"), TNCoopMap::FinishVolumeZ(false, 2594.0, 0.0), -300.0);
	return true;
}
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila (`IsInsideFinish`).
- [ ] **Paso 3: implementación.** Añadir a `TN_CoopMapDecisions.h`:

```cpp
	/** Holgura lateral del volumen de meta, la misma del procedural (ATN_ProcMapGenerator::Tick, antes de R2). */
	inline constexpr double FinishSideSlack = 1500.0;

	/** R2: P dentro del volumen de meta que empieza en LineCenter y se abre Length hacia Dir (antes se suponía +Y). */
	inline bool IsInsideFinish(const FVector2D& LineCenter, const FVector2D& Dir, double Length, double Width, const FVector2D& P)
	{
		const FVector2D D = Dir.GetSafeNormal();
		const FVector2D Rel = P - LineCenter;
		const double Along = FVector2D::DotProduct(Rel, D);
		const double Across = FMath::Abs(FVector2D::CrossProduct(D, Rel));
		return Along >= 0.0 && Along <= Length && Across <= Width * 0.5 + FinishSideSlack;
	}

	/** R3: cota del centro del volumen de meta (semialto 900): la del marcador en una meta seca preparada; si no, bajo el mar. */
	inline double FinishVolumeZ(bool bUseMarkerZ, double MarkerZ, double SeaLevel)
	{
		return bUseMarkerZ ? MarkerZ + 300.0 : SeaLevel - 300.0;
	}
```

  `TN_ProcMapGenerator.h`, junto a `bFrozeLocalPawn`: `/** Mapa preparado: la meta está en el agua (volumen a la cota del mar, como el procedural). */ bool bPreparedFinishInWater = false;`. En `TN_ProcMapGenerator.cpp:84` la línea `const bool bIn = …` pasa a:

```cpp
				const bool bIn = TNCoopMap::IsInsideFinish(FVector2D(F.Location.X, F.Location.Y), F.Dir, F.Length, F.Height, FVector2D(P.X, P.Y));
```

  En `TN_ProcMapGenerator_Spawn.cpp` (include de `TN_CoopMapDecisions.h`), la línea 444 pasa a:

```cpp
				const double FinishZ = TNCoopMap::FinishVolumeZ(IsPrepared() && !bPreparedFinishInWater, F.Location.Z, TNProcMap::SeaLevel);
				const FVector Loc = MapToWorld2D(C + F.Dir.GetSafeNormal() * (F.Length * 0.5), FinishZ);
```

- [ ] **Paso 4: ver pasar.** BUILD; UETEST `Coop.Finish` → `Success`; UETEST `ProcMap` sin regresiones.
- [ ] **Paso 5: commit.** `git add Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h Source/Tortunabo/Private/Tests/TN_CoopMapDecisionsTest.cpp Source/Tortunabo/Public/World/ProcMap/TN_ProcMapGenerator.h Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator.cpp Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Spawn.cpp` · `git commit -m "fix(procmap): la meta respeta su dirección y la cota de una meta alta"`

### Tarea 21: Fauna sin `Layout.Modules` y `SpawnFauna()` extraída (R5)

**Files:**
- Create: `Source/Tortunabo/Public/World/ProcMap/TN_ProcFaunaDecisions.h`, `Source/Tortunabo/Private/Tests/TN_ProcFaunaDecisionsTest.cpp`
- Modify: `Source/Tortunabo/Private/World/ProcMap/TN_ProcFauna.cpp:259, 283-310, 380-392`, `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Build.cpp:3301-3309`, `Source/Tortunabo/Public/World/ProcMap/TN_ProcMapGenerator.h:234`

**Interfaces:**
- Consumes: `TNProcMap::FPathSample` (`S`, `Biome`, `Module`).
- Produces: `TNProcFauna::GroupLength = 12000.0`; `TNProcFauna::BuildGroups(const TArray<TNProcMap::FPathSample>& Main, double MaxLength, TArray<int32>& OutGroupOfSample, TArray<ETNProcBiome>& OutGroupBiome)`; `void ATN_ProcMapGenerator::SpawnFauna()`.

- [ ] **Paso 1: test que falla.** `Source/Tortunabo/Private/Tests/TN_ProcFaunaDecisionsTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "World/ProcMap/TN_ProcFaunaDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNProcFaunaGroupsTest, "Tortunabo.ProcMap.FaunaGroups",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNProcFaunaGroupsTest::RunTest(const FString& Parameters)
{
	TArray<TNProcMap::FPathSample> Main;
	for (int32 i = 0; i < 10; ++i)
	{
		TNProcMap::FPathSample& S = Main.AddDefaulted_GetRef();
		S.S = 1000.0 * i;
		S.Biome = i < 5 ? ETNProcBiome::Beach : ETNProcBiome::Rocky;
	}
	TArray<int32> GroupOf;
	TArray<ETNProcBiome> Biomes;
	TNProcFauna::BuildGroups(Main, 3000.0, GroupOf, Biomes);
	TestEqual(TEXT("Un grupo por muestra"), GroupOf.Num(), 10);
	TestEqual(TEXT("Cuatro tramos (2 de playa, 2 de roca)"), Biomes.Num(), 4);
	TestTrue(TEXT("Tramo 0 de playa"), Biomes[0] == ETNProcBiome::Beach);
	TestTrue(TEXT("Tramo 2 de roca"), Biomes[2] == ETNProcBiome::Rocky);
	TestEqual(TEXT("La muestra 4 abre tramo por largo"), GroupOf[4], 1);
	TestEqual(TEXT("La muestra 5 abre tramo por bioma"), GroupOf[5], 2);
	TestEqual(TEXT("La última"), GroupOf[9], 3);
	TNProcFauna::BuildGroups(TArray<TNProcMap::FPathSample>(), 3000.0, GroupOf, Biomes);
	TestEqual(TEXT("Sin camino, sin grupos"), Biomes.Num(), 0);
	return true;
}

#endif
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila.
- [ ] **Paso 3: implementación.** `Source/Tortunabo/Public/World/ProcMap/TN_ProcFaunaDecisions.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapLayout.h"

/** Fauna ambiental sin módulos (mapa preparado, R5): se reparte por tramos del camino de un mismo bioma. */
namespace TNProcFauna
{
	/** Largo máximo de un tramo: del orden de un módulo del procedural. */
	inline constexpr double GroupLength = 12000.0;

	inline void BuildGroups(const TArray<TNProcMap::FPathSample>& Main, double MaxLength, TArray<int32>& OutGroupOfSample,
		TArray<ETNProcBiome>& OutGroupBiome)
	{
		OutGroupOfSample.Reset(Main.Num());
		OutGroupBiome.Reset();
		double GroupStart = 0.0;
		for (int32 i = 0; i < Main.Num(); ++i)
		{
			if (i == 0 || Main[i].Biome != OutGroupBiome.Last() || Main[i].S - GroupStart > MaxLength)
			{
				OutGroupBiome.Add(Main[i].Biome);
				GroupStart = Main[i].S;
			}
			OutGroupOfSample.Add(OutGroupBiome.Num() - 1);
		}
	}
}
```

  `TN_ProcFauna.cpp` (include `World/ProcMap/TN_ProcFaunaDecisions.h`):
  - Línea 259: `if (!Layout.bValid || Layout.Main.Num() < 2)`.
  - Antes de `struct FTNFaunaAnchor` (línea 270):

```cpp
	// Sin módulos (mapa preparado, R5): tramos del camino de un mismo bioma en lugar de módulos.
	const bool bUseModules = Layout.Modules.Num() > 0;
	TArray<int32> GroupOfMain;
	TArray<ETNProcBiome> GroupBiome;
	if (!bUseModules)
	{
		TNProcFauna::BuildGroups(Layout.Main, TNProcFauna::GroupLength, GroupOfMain, GroupBiome);
	}
	auto GroupOfMainSample = [&](int32 MainIndex)
	{
		return bUseModules ? Layout.Main[MainIndex].Module : (GroupOfMain.IsValidIndex(MainIndex) ? GroupOfMain[MainIndex] : INDEX_NONE);
	};
	auto BiomeOfGroup = [&](int32 Group) { return bUseModules ? Layout.Modules[Group].Biome : GroupBiome[Group]; };
```

  - `AddAnchor` recibe el grupo: `auto AddAnchor = [&Anchors](const TNProcMap::FPathSample& Sample, float Progress, int32 Group)` con `An.Module = Group;` en lugar de `An.Module = Sample.Module;`.
  - Línea 296: `const int32 Group = GroupOfMainSample(i); if ((Smp.Flags & SkipFlags) == 0 && Group >= 0) { AddAnchor(Smp, static_cast<float>(Smp.S), Group); }`.
  - Línea 308: `const int32 Group = bUseModules ? Smp.Module : GroupOfMainSample(Br.ForkSample); if ((Smp.Flags & SkipFlags) == 0 && Group >= 0) { AddAnchor(Smp, static_cast<float>(FMath::Lerp(ForkS, RejoinS, Smp.S / BranchLen)), Group); }`.
  - Líneas 380-384:

```cpp
	TArray<int32> RouteModules;
	if (bUseModules)
	{
		for (const TNProcMap::FPathSample& Smp : Layout.Main)
		{
			if (Layout.Modules.IsValidIndex(Smp.Module)) { RouteModules.AddUnique(Smp.Module); }
		}
	}
	else
	{
		for (int32 Group = 0; Group < GroupBiome.Num(); ++Group) { RouteModules.Add(Group); }
	}
```

  - Líneas 386 y 392: `Layout.Modules[Mod].Biome` → `BiomeOfGroup(Mod)`.

  `TN_ProcMapGenerator.h`, en `private:` junto a `BuildStructures`: `/** Fauna ambiental local (no en servidor dedicado); la llaman BuildStructures (procedural) y el Prepared. */ void SpawnFauna();`. En `TN_ProcMapGenerator_Build.cpp`, las líneas 3301-3309 (el bloque «Fauna ambiental…») se sustituyen por `SpawnFauna();` al final de `BuildStructures` y el bloque pasa a:

```cpp
void ATN_ProcMapGenerator::SpawnFauna()
{
	// Fauna ambiental (solo visual y local). Clear() la destruye al regenerar; espera sola a que el mapa esté listo.
	if (GetWorld() && GetWorld()->IsGameWorld() && GetNetMode() != NM_DedicatedServer)
	{
		if (ATN_ProcFauna* FaunaActor = Cast<ATN_ProcFauna>(SpawnMapActor(ATN_ProcFauna::StaticClass(), GetActorTransform(), false)))
		{
			FaunaActor->Init(this, Layout.Params.Seed);
		}
	}
}
```

- [ ] **Paso 4: ver pasar.** BUILD; UETEST `ProcMap` → `ProcMap.FaunaGroups` y el resto `Success` (el procedural sigue por módulos: mismo reparto).
- [ ] **Paso 5: commit.** `git add Source/Tortunabo/Public/World/ProcMap/TN_ProcFaunaDecisions.h Source/Tortunabo/Private/Tests/TN_ProcFaunaDecisionsTest.cpp Source/Tortunabo/Private/World/ProcMap/TN_ProcFauna.cpp Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Build.cpp Source/Tortunabo/Public/World/ProcMap/TN_ProcMapGenerator.h` · `git commit -m "fix(procmap): la fauna nace sin módulos y fuera de BuildStructures"`

### Tarea 22: La pantalla de carga reconoce los mapas Coop (R8)

**Files:**
- Create: `Source/Tortunabo/Public/UI/Loading/TN_LoadingMapDecisions.h`, `Source/Tortunabo/Private/Tests/TN_LoadingMapDecisionsTest.cpp`
- Modify: `Source/Tortunabo/Private/UI/Loading/TN_LoadingScreenSubsystem.cpp:539, 614, 1145`

**Interfaces:**
- Produces: `TNLoadingMaps::IsCoopMap(const FString&)`, `TNLoadingMaps::IsRoundMap(const FString&)`, `TNLoadingMaps::HasProcMapGenerator(const FString&)`.

- [ ] **Paso 1: test que falla.** `Source/Tortunabo/Private/Tests/TN_LoadingMapDecisionsTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "UI/Loading/TN_LoadingMapDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNLoadingRoundMapsTest, "Tortunabo.Loading.RoundMaps",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNLoadingRoundMapsTest::RunTest(const FString& Parameters)
{
	using namespace TNLoadingMaps;
	TestTrue(TEXT("Coop preparado en PIE"), IsRoundMap(TEXT("UEDPIE_0_LVL_Coop_CP01")));
	TestTrue(TEXT("Coop preparado espera al generador"), HasProcMapGenerator(TEXT("LVL_Coop_C01")));
	TestTrue(TEXT("Procedural"), IsRoundMap(TEXT("LVL_ProcMap")) && HasProcMapGenerator(TEXT("LVL_ProcMap")));
	TestTrue(TEXT("Playa"), IsRoundMap(TEXT("LVL_BeachRace")));
	TestFalse(TEXT("La playa no tiene generador de ProcMap"), HasProcMapGenerator(TEXT("LVL_BeachRace")));
	TestFalse(TEXT("Lobby"), IsRoundMap(TEXT("LVL_Lobby")));
	TestFalse(TEXT("LVL_Run retirado"), IsRoundMap(TEXT("LVL_Run")));
	TestFalse(TEXT("Subnivel de marcadores no es un mapa de rondas por sí mismo"), IsCoopMap(TEXT("LVL_Coop_CP01_Markers")));
	return true;
}

#endif
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila.
- [ ] **Paso 3: implementación.** `Source/Tortunabo/Public/UI/Loading/TN_LoadingMapDecisions.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

/** Qué mapas son de rondas para la pantalla de carga (R8): antes se detectaba solo «ProcMap» y el Coop preparado no esperaba. */
namespace TNLoadingMaps
{
	inline bool IsCoopMap(const FString& MapName)
	{
		return MapName.Contains(TEXT("LVL_Coop_")) && !MapName.EndsWith(TEXT("_Terrain")) && !MapName.EndsWith(TEXT("_Markers"))
			&& !MapName.EndsWith(TEXT("_Design"));
	}

	inline bool HasProcMapGenerator(const FString& MapName)
	{
		return MapName.Contains(TEXT("ProcMap")) || IsCoopMap(MapName);
	}

	inline bool IsRoundMap(const FString& MapName)
	{
		return HasProcMapGenerator(MapName) || MapName.Contains(TEXT("BeachRace"));
	}
}
```

  En `TN_LoadingScreenSubsystem.cpp` (include de la cabecera): línea 539 `(RoundMap.Contains(TEXT("ProcMap")) || RoundMap.Contains(TEXT("BeachRace")))` → `TNLoadingMaps::IsRoundMap(RoundMap)`; línea 614 `MapName.Contains(TEXT("ProcMap")) || MapName.Contains(TEXT("LVL_Run")) || MapName.Contains(TEXT("BeachRace"))` → `TNLoadingMaps::IsRoundMap(MapName)`; línea 1145 `MapName.Contains(TEXT("ProcMap"))` → `TNLoadingMaps::HasProcMapGenerator(MapName)`.
- [ ] **Paso 4: ver pasar.** BUILD; UETEST `Loading` → `Success`.
- [ ] **Paso 5: commit.** `git add Source/Tortunabo/Public/UI/Loading/TN_LoadingMapDecisions.h Source/Tortunabo/Private/Tests/TN_LoadingMapDecisionsTest.cpp Source/Tortunabo/Private/UI/Loading/TN_LoadingScreenSubsystem.cpp` · `git commit -m "fix(carga): la pantalla de carga espera también en los mapas Coop preparados"`

### Tarea 23: Marcadores `ATN_CoopMarker` y ancla `ATN_CoopMapAnchor`

**Files:**
- Create: `Source/Tortunabo/Public/World/ProcMap/TN_CoopMarker.h`, `Source/Tortunabo/Private/World/ProcMap/TN_CoopMarker.cpp`, `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapAnchor.h`, `Source/Tortunabo/Private/World/ProcMap/TN_CoopMapAnchor.cpp`
- Modify: `Source/Tortunabo/Public/World/ProcMap/TN_ProcMapEnums.h`, `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h`, `Source/Tortunabo/Private/Tests/TN_CoopMapDataTest.cpp`

**Interfaces:**
- Produces: `UENUM ETNCoopMarkerKind : uint8 { Start, Finish, EggNest, BeachElement, Jellyfish, BridgeAnchor }`; `TNCoopMap::FCoopMarker { ETNCoopMarkerKind Kind; FName Id; FVector Pos; double Yaw; int32 Order; ETNBeachElement Element; float SizeScale; float Extent; float FinishWidth; float FinishDepth; bool bInWater; FName BridgeId; bool bBridgeEnd; }`; `ATN_CoopMarker` (propiedades `Kind`, `StableId`, `NestOrder`, `Element`, `SizeScale`, `Extent`, `FinishWidth`, `FinishDepth`, `bFinishInWater`, `BridgeId`, `bBridgeEnd`; `TNCoopMap::FCoopMarker ToView() const`); `ATN_CoopMapAnchor` (`MapId`, `KnownIds`, editor-only).

- [ ] **Paso 1: test que falla.** Añadir a `TN_CoopMapDataTest.cpp` (include `World/ProcMap/TN_CoopMarker.h`):

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopMarkerViewTest, "Tortunabo.Coop.MarkerView",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopMarkerViewTest::RunTest(const FString& Parameters)
{
	TNCoopMapDataTest::FPreparedWorld Scene(TNCoopMapDataTest::LoadT01(*this));
	ATN_CoopMarker* Marker = Scene.World->SpawnActor<ATN_CoopMarker>(ATN_CoopMarker::StaticClass(),
		FTransform(FRotator(0.0, 90.0, 0.0), FVector(700.0, 1000.0, 0.0)));
	Marker->Kind = ETNCoopMarkerKind::BeachElement;
	Marker->StableId = TEXT("cat_01");
	Marker->Element = ETNBeachElement::Catapult;
	const TNCoopMap::FCoopMarker View = Marker->ToView();
	TestTrue(TEXT("Tipo"), View.Kind == ETNCoopMarkerKind::BeachElement);
	TestEqual(TEXT("Id estable"), View.Id, FName(TEXT("cat_01")));
	TestEqual(TEXT("Sitio en mundo"), View.Pos, FVector(700.0, 1000.0, 0.0));
	TestEqual(TEXT("Giro"), View.Yaw, 90.0, 0.01);
	TestFalse(TEXT("No se replica (va en el paquete del nivel)"), Marker->GetIsReplicated());
	TestTrue(TEXT("Oculto en juego"), Marker->IsHidden());
	return true;
}
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila.
- [ ] **Paso 3: enum y vista pura.** `TN_ProcMapEnums.h`, tras `ETNTerrainSource`:

```cpp
/** Qué marca un ATN_CoopMarker del subnivel _Markers de un mapa Coop preparado. */
UENUM(BlueprintType)
enum class ETNCoopMarkerKind : uint8
{
	Start        UMETA(DisplayName = "Salida"),
	Finish       UMETA(DisplayName = "Meta"),
	EggNest      UMETA(DisplayName = "Nido de huevos"),
	BeachElement UMETA(DisplayName = "Mecánica de la playa"),
	Jellyfish    UMETA(DisplayName = "Medusa saltarina"),
	BridgeAnchor UMETA(DisplayName = "Anclaje de puente de Mokius")
};
```

  `TN_CoopMapDecisions.h`: includes `World/Beach/TN_BeachTypes.h` y `World/ProcMap/TN_ProcMapEnums.h`; dentro del namespace:

```cpp
	/** Lo que la lógica necesita de un marcador (espacio de mundo). */
	struct FCoopMarker
	{
		ETNCoopMarkerKind Kind = ETNCoopMarkerKind::Start;
		FName Id;
		FVector Pos = FVector::ZeroVector;
		double Yaw = 0.0;
		int32 Order = 0;
		ETNBeachElement Element = ETNBeachElement::Catapult;
		float SizeScale = 1.f;
		float Extent = 0.f;
		float FinishWidth = 1500.f;
		float FinishDepth = 1200.f;
		bool bInWater = false;
		FName BridgeId;
		bool bBridgeEnd = false;
	};
```

- [ ] **Paso 4: actores.** `Source/Tortunabo/Public/World/ProcMap/TN_CoopMarker.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Beach/TN_BeachTypes.h"
#include "World/ProcMap/TN_CoopMapDecisions.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "TN_CoopMarker.generated.h"

class UBillboardComponent;

/**
 * Marcador de diseño de un mapa Coop preparado (subnivel _Markers): salida, meta, nido, mecánica, medusa o anclaje de
 * puente. No se replica: viaja en el paquete del nivel y todas las máquinas lo leen igual. Oculto en juego. Lo crea
 * Scripts/import_terrain_coop.py con un StableId; el diseñador lo mueve o lo borra y el importador lo respeta.
 */
UCLASS(HideCategories = (Collision, Physics, Replication, Input, LOD, Cooking))
class TORTUNABO_API ATN_CoopMarker : public AActor
{
	GENERATED_BODY()

public:
	ATN_CoopMarker();

	TNCoopMap::FCoopMarker ToView() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop")
	ETNCoopMarkerKind Kind = ETNCoopMarkerKind::EggNest;

	/** Id estable del manifest (nest_03, cat_01…): el importador nunca recrea un id que el diseñador borró. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop")
	FName StableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop", meta = (EditCondition = "Kind == ETNCoopMarkerKind::EggNest", EditConditionHides))
	int32 NestOrder = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop", meta = (EditCondition = "Kind == ETNCoopMarkerKind::BeachElement", EditConditionHides))
	ETNBeachElement Element = ETNBeachElement::Catapult;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop", meta = (EditCondition = "Kind == ETNCoopMarkerKind::BeachElement", EditConditionHides, ClampMin = "0.7", ClampMax = "1.4"))
	float SizeScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop", meta = (EditCondition = "Kind == ETNCoopMarkerKind::BeachElement", EditConditionHides, ClampMin = "0.0"))
	float Extent = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop", meta = (EditCondition = "Kind == ETNCoopMarkerKind::Finish", EditConditionHides))
	float FinishWidth = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop", meta = (EditCondition = "Kind == ETNCoopMarkerKind::Finish", EditConditionHides))
	float FinishDepth = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop", meta = (EditCondition = "Kind == ETNCoopMarkerKind::Finish", EditConditionHides))
	bool bFinishInWater = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop", meta = (EditCondition = "Kind == ETNCoopMarkerKind::BridgeAnchor", EditConditionHides))
	FName BridgeId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop", meta = (EditCondition = "Kind == ETNCoopMarkerKind::BridgeAnchor", EditConditionHides))
	bool bBridgeEnd = false;

#if WITH_EDITORONLY_DATA
private:
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Sprite;
#endif
};
```

  `Source/Tortunabo/Private/World/ProcMap/TN_CoopMarker.cpp`:

```cpp
#include "World/ProcMap/TN_CoopMarker.h"

#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"

ATN_CoopMarker::ATN_CoopMarker()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetHidden(true);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
#if WITH_EDITORONLY_DATA
	Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite)
	{
		Sprite->SetupAttachment(RootComponent);
	}
#endif
}

TNCoopMap::FCoopMarker ATN_CoopMarker::ToView() const
{
	TNCoopMap::FCoopMarker View;
	View.Kind = Kind;
	View.Id = StableId;
	View.Pos = GetActorLocation();
	View.Yaw = GetActorRotation().Yaw;
	View.Order = NestOrder;
	View.Element = Element;
	View.SizeScale = SizeScale;
	View.Extent = Extent;
	View.FinishWidth = FinishWidth;
	View.FinishDepth = FinishDepth;
	View.bInWater = bFinishInWater;
	View.BridgeId = BridgeId;
	View.bBridgeEnd = bBridgeEnd;
	return View;
}
```

  `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapAnchor.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_CoopMapAnchor.generated.h"

/**
 * Ancla del importador en el subnivel _Markers (solo editor, no se cocina): los ids de marcador que ya se emitieron.
 * Un id conocido sin actor = el diseñador lo borró; TN_REGENERATE=1 no lo resucita (Scripts/coop_markers.py).
 */
UCLASS(NotBlueprintable)
class TORTUNABO_API ATN_CoopMapAnchor : public AActor
{
	GENERATED_BODY()

public:
	ATN_CoopMapAnchor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coop")
	FName MapId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coop")
	TArray<FName> KnownIds;
};
```

  `Source/Tortunabo/Private/World/ProcMap/TN_CoopMapAnchor.cpp`:

```cpp
#include "World/ProcMap/TN_CoopMapAnchor.h"

#include "Components/SceneComponent.h"

ATN_CoopMapAnchor::ATN_CoopMapAnchor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	bIsEditorOnlyActor = true;
	SetHidden(true);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}
```

- [ ] **Paso 5: ver pasar.** BUILD; UETEST `Coop.MarkerView` → `Success`.
- [ ] **Paso 6: commit.** `git add Source/Tortunabo/Public/World/ProcMap/TN_ProcMapEnums.h Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h Source/Tortunabo/Public/World/ProcMap/TN_CoopMarker.h Source/Tortunabo/Private/World/ProcMap/TN_CoopMarker.cpp Source/Tortunabo/Public/World/ProcMap/TN_CoopMapAnchor.h Source/Tortunabo/Private/World/ProcMap/TN_CoopMapAnchor.cpp Source/Tortunabo/Private/Tests/TN_CoopMapDataTest.cpp` · `git commit -m "feat(coop): marcadores de diseño no replicados y ancla de ids del importador"`

### Tarea 24: Importador headless `import_terrain_coop.py` (subniveles, marcadores, lápidas)

**Files:**
- Create: `Scripts/coop_markers.py`, `Scripts/tests/test_coop_markers.py`, `Scripts/terrain_import_common.py`, `Scripts/import_terrain_coop.py`
- Modify: `Scripts/import_terrain_mesh.py:27-101`

**Interfaces:**
- Consumes: manifest v2 (Tarea 11); `UTN_CoopMapData.load_from_manifest` (14); `ATN_CoopMarker`, `ATN_CoopMapAnchor` (23); `ATN_ProcMapGenerator.source/coop_data` (16).
- Produces: `coop_markers.marker_specs(coop: dict) -> list[dict]`; `coop_markers.plan_marker_sync(known_ids, existing_ids, manifest_ids) -> tuple[list[str], list[str]]`; `coop_markers.snake_upper(name) -> str`; `terrain_import_common` (`project_dir`, `load_or_none`, `build_assets`, `spawn`, `configure_sun`, `existing_labels`, `spawn_lighting`); nivel `/Game/Maps/Coop/LVL_Coop_<Id>` con `_Terrain`, `_Markers`, `_Design`; línea `+MapsToCook` del nivel.

- [ ] **Paso 1: test que falla (Review Focus 4).** `Scripts/tests/test_coop_markers.py`:

```python
"""Sincronia de marcadores del importador Coop: TN_REGENERATE=1 nunca mueve ni resucita (Review Focus 4)."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from coop_markers import marker_specs, plan_marker_sync, snake_upper  # noqa: E402

COOP = {
    "start": {"id": "start", "pos": [0, 0, 100], "yaw": 90.0},
    "finish": {"id": "finish", "pos": [9000, 0, 50], "dir": [-1.0, 0.0], "width_uu": 1500, "depth_uu": 1200, "in_water": True},
    "nests": [{"id": "nest_01", "pos": [3000, 0, 80], "order": 1}, {"id": "nest_02", "pos": [6000, 0, 60], "order": 2}],
    "jellyfish": [{"id": "jelly_01", "pos": [1000, 200, 0]}],
    "beach_elements": [{"id": "cat_01", "element": "Catapult", "pos": [2000, 0, 90], "yaw": 0.0, "size": 1.0, "extent": 0.0}],
    "bridges": [{"id": "bridge_01_a", "bridge_id": "bridge_01", "end": False, "pos": [4000, 0, 70]}],
}


def test_first_import_creates_every_marker():
    ids = [s["id"] for s in marker_specs(COOP)]
    create, known = plan_marker_sync([], [], ids)
    assert create == ids and known == sorted(ids)


def test_deleted_marker_is_not_resurrected():
    ids = [s["id"] for s in marker_specs(COOP)]
    existing = [i for i in ids if i != "nest_02"]                  # el disenador borro nest_02
    create, _ = plan_marker_sync(sorted(ids), existing, ids)
    assert "nest_02" not in create and create == []


def test_existing_markers_are_never_recreated_or_moved():
    ids = [s["id"] for s in marker_specs(COOP)]
    create, _ = plan_marker_sync(sorted(ids), ids, ids + ["nest_03"])
    assert create == ["nest_03"]


def test_marker_specs_cover_every_kind():
    kinds = {s["kind"] for s in marker_specs(COOP)}
    assert kinds == {"Start", "Finish", "EggNest", "Jellyfish", "BeachElement", "BridgeAnchor"}
    finish = next(s for s in marker_specs(COOP) if s["kind"] == "Finish")
    assert finish["yaw"] == 180.0 and finish["in_water"] is True


def test_snake_upper_matches_unreal_python_enums():
    assert snake_upper("WobblyPlatform") == "WOBBLY_PLATFORM"
    assert snake_upper("Catapult") == "CATAPULT"
    assert snake_upper("EggNest") == "EGG_NEST"
```

- [ ] **Paso 2: ver el fallo.** PYTEST con `Scripts/tests/test_coop_markers.py` → `ModuleNotFoundError: coop_markers`.
- [ ] **Paso 3: lógica pura.** `Scripts/coop_markers.py` (solo biblioteca estándar: lo importa el Python del editor):

```python
"""Marcadores de un mapa Coop preparado (subnivel _Markers) a partir del bloque coop del manifest v2, y su
sincronia al reimportar: solo se crean ids nunca emitidos; un id conocido sin actor lo borro el disenador (lapida)
y no se recrea; un marcador existente nunca se mueve."""

from __future__ import annotations

import math
import re


def snake_upper(name: str) -> str:
    """Nombre de un valor de UENUM tal como lo expone el Python de Unreal (WobblyPlatform -> WOBBLY_PLATFORM)."""
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).upper()


def marker_specs(coop: dict) -> list[dict]:
    start, finish = coop["start"], coop["finish"]
    specs = [{"kind": "Start", "id": start["id"], "pos": start["pos"], "yaw": float(start["yaw"])},
             {"kind": "Finish", "id": finish["id"], "pos": finish["pos"],
              "yaw": round(math.degrees(math.atan2(finish["dir"][1], finish["dir"][0])), 3),
              "width": finish["width_uu"], "depth": finish["depth_uu"], "in_water": bool(finish["in_water"])}]
    specs += [{"kind": "EggNest", "id": n["id"], "pos": n["pos"], "yaw": 0.0, "order": n["order"]} for n in coop["nests"]]
    specs += [{"kind": "Jellyfish", "id": j["id"], "pos": j["pos"], "yaw": 0.0} for j in coop.get("jellyfish", [])]
    specs += [{"kind": "BeachElement", "id": e["id"], "pos": e["pos"], "yaw": float(e["yaw"]), "element": e["element"],
               "size": float(e["size"]), "extent": float(e["extent"])} for e in coop.get("beach_elements", [])]
    specs += [{"kind": "BridgeAnchor", "id": b["id"], "pos": b["pos"], "yaw": 0.0, "bridge_id": b["bridge_id"],
               "end": bool(b["end"])} for b in coop.get("bridges", [])]
    return specs


def plan_marker_sync(known_ids: list[str], existing_ids: list[str], manifest_ids: list[str]) -> tuple[list[str], list[str]]:
    """(ids a crear, KnownIds nuevo)."""
    known, existing = set(known_ids), set(existing_ids)
    create = [i for i in manifest_ids if i not in known and i not in existing]
    return create, sorted(known | existing | set(manifest_ids))
```

- [ ] **Paso 4: ver pasar.** PYTEST con el fichero → 5 passed.
- [ ] **Paso 5: módulo común.** Mover sin cambios las funciones `project_dir`, `load_or_none`, `build_assets`, `spawn`, `configure_sun`, `existing_labels` y las constantes `GRIDMAP_ROOT`, `TERRAIN_MATERIAL_PATH`, `FOLIAGE_MATERIAL_PATH`, `WATER_MATERIAL_PATH`, `PLANE_MESH_PATH`, `PLANE_SIZE_UU`, `asset_lib`, `asset_tools` (líneas 33-101 de `import_terrain_mesh.py`) a `Scripts/terrain_import_common.py`, y añadir en él:

```python
def spawn_lighting():
    """Sol, cielo y luz de cielo como los del mapa volumetrico (import_terrain_mesh.build_level)."""
    sun = spawn(unreal.DirectionalLight, "Sun", unreal.Vector(0.0, 0.0, 5000.0), unreal.Rotator(0.0, -50.0, 35.0))
    configure_sun(sun)
    spawn(unreal.SkyAtmosphere, "SkyAtmosphere", unreal.Vector(0.0, 0.0, 0.0))
    sky = spawn(unreal.SkyLight, "SkyLight", unreal.Vector(0.0, 0.0, 5000.0))
    sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky_component.set_editor_property("real_time_capture", True)
    sky_component.set_editor_property("intensity", 2.0)
```

  `import_terrain_mesh.py` sustituye esas líneas por `sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))` + `from terrain_import_common import (GAME_MODE_PATH, PLANE_MESH_PATH, PLANE_SIZE_UU, WATER_MATERIAL_PATH, asset_lib, build_assets, configure_sun, existing_labels, load_or_none, project_dir, spawn)` (`GAME_MODE_PATH` y `PLAYER_START_*` se quedan en `import_terrain_mesh.py`; importar solo lo movido) y `import sys`.
- [ ] **Paso 6: importador.** `Scripts/import_terrain_coop.py`:

```python
"""Importa un mapa fijo con bloque Coop (manifest v2) y monta su nivel jugable (plan F0-F2, Tarea 24).

    TN_VOLUME_DIR=<repo>/Scripts/terrain_volumes/Variants/C01_camino  UEPY import_terrain_coop.py

Crea o actualiza:
  /Game/Terrain/Volumes/<Id>/     DA_<trozo> y Meshes/SM_<trozo> (terrain_import_common.build_assets)
  /Game/Coop/<Id>/DA_Coop_<Id>    UTN_CoopMapData.load_from_manifest
  /Game/Maps/Coop/LVL_Coop_<Id>   persistente: generador Prepared, BP_ProcMapGameMode, luz, cielo y 4 PlayerStart
     _Terrain   trozos (tag TN_MapTerrain; los de la corona sin colision) y agua (tag TN_Water)   lo posee el script
     _Markers   ATN_CoopMarker por id y ATN_CoopMapAnchor (KnownIds)                            solo ids nuevos
     _Design    vacio                                                                           el script no lo abre
Con el nivel ya creado exige TN_REGENERATE=1 y nunca mueve ni resucita marcadores (coop_markers.plan_marker_sync).
"""

import json
import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from coop_markers import marker_specs, plan_marker_sync, snake_upper  # noqa: E402
from terrain_import_common import (PLANE_MESH_PATH, PLANE_SIZE_UU, WATER_MATERIAL_PATH, asset_lib, asset_tools,  # noqa: E402
                                   build_assets, load_or_none, project_dir, spawn, spawn_lighting)

PROC_GAME_MODE = "/Game/ProcMap/BP_ProcMapGameMode"
SUBLEVELS = ("_Terrain", "_Markers", "_Design")
PLAYER_STARTS = 4
PLAYER_START_RING_UU = 250.0
PLAYER_START_LIFT_UU = 150.0
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def all_actors(cls):
    return [a for a in actors.get_all_level_actors() if isinstance(a, cls)]


def ensure_data_asset(map_id: str, manifest_path: str):
    folder, name = f"/Game/Coop/{map_id}", f"DA_Coop_{map_id}"
    data = load_or_none(f"{folder}/{name}")
    if data is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.TN_CoopMapData)
        data = asset_tools.create_asset(name, folder, unreal.TN_CoopMapData, factory)
    if not data.load_from_manifest(manifest_path):
        raise RuntimeError(f"{folder}/{name}: {data.get_editor_property('last_load_error')}")
    asset_lib.save_loaded_asset(data)
    return data


def ensure_level(level_path: str) -> bool:
    """True si el nivel es nuevo (con sus tres subniveles siempre cargados)."""
    if asset_lib.does_asset_exist(level_path):
        levels.load_level(level_path)
        return False
    levels.new_level(level_path)
    for suffix in SUBLEVELS:
        if unreal.EditorLevelUtils.create_new_streaming_level(unreal.LevelStreamingAlwaysLoaded, level_path + suffix, False) is None:
            raise RuntimeError(f"no se pudo crear el subnivel {level_path + suffix}")
    levels.save_all_dirty_levels()
    return True


def place_terrain(manifest: dict, meshes: dict, level_name: str) -> int:
    levels.set_current_level_by_name(level_name + "_Terrain")
    have = {a.get_actor_label(): a for a in all_actors(unreal.StaticMeshActor)}
    for cell in manifest["cells"]:
        label = f"Terrain_{cell['name']}"
        tile = have.get(label) or spawn(unreal.StaticMeshActor, label, unreal.Vector(*cell["center_uu"], 0.0))
        component = tile.static_mesh_component
        component.set_static_mesh(meshes[cell["name"]])
        if cell.get("collision", True):
            tile.tags = [unreal.Name("TN_MapTerrain")]
        else:
            component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    if "Water" not in have:
        extent = manifest["grid"] * manifest["cell_uu"] * 3.0
        center = (manifest["grid"] - 1) * manifest["cell_uu"] * 0.5
        water = spawn(unreal.StaticMeshActor, "Water", unreal.Vector(center, center, manifest["water_uu"]))
        water.static_mesh_component.set_static_mesh(unreal.load_asset(PLANE_MESH_PATH))
        water.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        water.static_mesh_component.set_cast_shadow(False)
        material = load_or_none(WATER_MATERIAL_PATH)
        if material:
            water.static_mesh_component.set_material(0, material)
        water.set_actor_scale3d(unreal.Vector(extent / PLANE_SIZE_UU, extent / PLANE_SIZE_UU, 1.0))
        water.tags = [unreal.Name("TN_Water")]
    return len(manifest["cells"])


def place_marker(spec: dict):
    marker = spawn(unreal.TN_CoopMarker, spec["id"], unreal.Vector(*spec["pos"]), unreal.Rotator(0.0, 0.0, spec["yaw"]))
    marker.set_editor_property("kind", getattr(unreal.TNCoopMarkerKind, snake_upper(spec["kind"])))
    marker.set_editor_property("stable_id", unreal.Name(spec["id"]))
    if spec["kind"] == "EggNest":
        marker.set_editor_property("nest_order", spec["order"])
    elif spec["kind"] == "BeachElement":
        marker.set_editor_property("element", getattr(unreal.TNBeachElement, snake_upper(spec["element"])))
        marker.set_editor_property("size_scale", spec["size"])
        marker.set_editor_property("extent", spec["extent"])
    elif spec["kind"] == "Finish":
        marker.set_editor_property("finish_width", spec["width"])
        marker.set_editor_property("finish_depth", spec["depth"])
        marker.set_editor_property("finish_in_water", spec["in_water"])
    elif spec["kind"] == "BridgeAnchor":
        marker.set_editor_property("bridge_id", unreal.Name(spec["bridge_id"]))
        marker.set_editor_property("bridge_end", spec["end"])
    marker.set_folder_path(f"Coop/{spec['kind']}")


def sync_markers(coop: dict, level_name: str) -> tuple[int, int]:
    levels.set_current_level_by_name(level_name + "_Markers")
    anchors = all_actors(unreal.TN_CoopMapAnchor)
    anchor = anchors[0] if anchors else spawn(unreal.TN_CoopMapAnchor, "CoopMapAnchor", unreal.Vector(0.0, 0.0, 0.0))
    anchor.set_editor_property("map_id", unreal.Name(coop["map_id"]))
    specs = {s["id"]: s for s in marker_specs(coop)}
    existing = [str(m.get_editor_property("stable_id")) for m in all_actors(unreal.TN_CoopMarker)]
    known = [str(n) for n in anchor.get_editor_property("known_ids")]
    create, new_known = plan_marker_sync(known, existing, list(specs))
    for marker_id in create:
        place_marker(specs[marker_id])
    anchor.set_editor_property("known_ids", [unreal.Name(i) for i in new_known])
    return len(create), len(new_known)


def place_persistent(coop: dict, data, level_name: str, is_new: bool) -> None:
    levels.set_current_level_by_name(level_name)
    generators = all_actors(unreal.TN_ProcMapGenerator)
    x0, y0 = coop["bounds_uu"][0], coop["bounds_uu"][1]
    generator = generators[0] if generators else spawn(unreal.TN_ProcMapGenerator, "CoopGenerator",
                                                       unreal.Vector(x0, y0, coop["water"]["z_uu"]))
    generator.set_editor_property("source", unreal.TNTerrainSource.PREPARED)
    generator.set_editor_property("coop_data", data)
    if not is_new:
        return
    spawn_lighting()
    sx, sy, sz = coop["start"]["pos"]
    for k in range(PLAYER_STARTS):
        angle = 2.0 * math.pi * k / PLAYER_STARTS
        spawn(unreal.PlayerStart, f"PlayerStart_{k}", unreal.Vector(sx + PLAYER_START_RING_UU * math.cos(angle),
                                                                   sy + PLAYER_START_RING_UU * math.sin(angle), sz + PLAYER_START_LIFT_UU))
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", asset_lib.load_blueprint_class(PROC_GAME_MODE))


def add_to_cook(level_path: str) -> None:
    ini = os.path.join(project_dir(), "Config", "DefaultGame.ini")
    line = f'+MapsToCook=(FilePath="{level_path}")'
    with open(ini, encoding="utf-8") as handle:
        text = handle.read()
    if line in text:
        return
    anchor = '+MapsToCook=(FilePath="/Game/Maps/LVL_Transition")'
    with open(ini, "w", encoding="utf-8") as handle:
        handle.write(text.replace(anchor, anchor + "\n" + line))


def main() -> None:
    volume_dir = os.environ["TN_VOLUME_DIR"].replace("\\", "/")
    manifest_path = f"{volume_dir}/manifest.json"
    with open(manifest_path, encoding="utf-8") as handle:
        manifest = json.load(handle)
    if manifest.get("manifest_version") != 2:
        raise RuntimeError(f"{manifest_path}: falta el bloque coop (gen_terrain_coop.py)")
    coop = manifest["coop"]
    map_id = coop["map_id"]
    level_path = f"/Game/Maps/Coop/LVL_Coop_{map_id}"
    if asset_lib.does_asset_exist(level_path) and os.environ.get("TN_REGENERATE") != "1":
        raise RuntimeError(f"{level_path} ya existe (mapa fijo): TN_REGENERATE=1 recarga malla y datos sin tocar lo retocado")
    _, meshes = build_assets(volume_dir, manifest, f"/Game/Terrain/Volumes/{map_id}")
    data = ensure_data_asset(map_id, manifest_path)
    is_new = ensure_level(level_path)
    level_name = level_path.rsplit("/", 1)[-1]
    tiles = place_terrain(manifest, meshes, level_name)
    created, known = sync_markers(coop, level_name)
    place_persistent(coop, data, level_name, is_new)
    levels.save_all_dirty_levels()
    add_to_cook(level_path)
    unreal.log(f"[CoopImport] {level_path}: {tiles} trozos, {created} marcadores nuevos, KnownIds {known}")


main()
```

- [ ] **Paso 7: ejecutar sobre C01 y verificar.** `set TN_VOLUME_DIR=C:\Users\Rodrigo\PERSONAL\ProyectosPersonales\Tortunabo\Scripts\terrain_volumes\Variants\C01_camino` y UEPY `import_terrain_coop.py`. Criterio: en `Saved\Logs\Tortunabo.log` aparece `[CoopImport] /Game/Maps/Coop/LVL_Coop_C01: 156 trozos, N marcadores nuevos, KnownIds N` con N = 2 + nidos + medusas del manifest; existen `Content/Maps/Coop/LVL_Coop_C01.umap`, `LVL_Coop_C01_Terrain.umap`, `LVL_Coop_C01_Markers.umap`, `LVL_Coop_C01_Design.umap`, `Content/Coop/C01/DA_Coop_C01.uasset`; `Config/DefaultGame.ini` contiene `+MapsToCook=(FilePath="/Game/Maps/Coop/LVL_Coop_C01")`. Segunda pasada sin `TN_REGENERATE` → falla con «ya existe»; con `TN_REGENERATE=1` → `0 marcadores nuevos`. PYTEST `Scripts/tests/test_cook_list.py` → verde (`test_every_coop_level_is_cooked` ahora con un nivel).
- [ ] **Paso 8: commit.** `git add Scripts/coop_markers.py Scripts/tests/test_coop_markers.py Scripts/terrain_import_common.py Scripts/import_terrain_mesh.py Scripts/import_terrain_coop.py Config/DefaultGame.ini Content/Maps/Coop Content/Coop/C01 Content/Terrain/Volumes/C01` · `git commit -m "feat(coop): importador del nivel Coop con subniveles, marcadores estables y C01 importado"`

### Tarea 25: R9 (colisión de la malla fuente) y smoke headless de `LVL_Coop_C01` (gate F1)

**Files:**
- Create: `Scripts/verify_terrain_collision.py`
- Modify (solo si falla el paso 2): `Source/Tortunabo/Private/World/TN_TerrainMeshAsset.cpp:132`

**Interfaces:**
- Consumes: `LVL_Coop_C01` (24), `Variants/C01_camino/coop/bake.bin` (11).
- Produces: log `[R9] p99 %.1f cm, max %.1f cm en %d trazas`; criterio p99 ≤ 20 cm y máximo ≤ 50 cm.

- [ ] **Paso 1: script.** `Scripts/verify_terrain_collision.py`:

```python
"""R9: la colision del StaticMesh del terreno sale de la malla fuente y no de la reserva Nanite al 10 %.
Traza 400 columnas permitidas de LVL_Coop_<Id> y compara el impacto con la cota del bake (top_rel_cm).

    TN_VOLUME_DIR=<repo>/Scripts/terrain_volumes/Variants/C01_camino TN_COOP_LEVEL=/Game/Maps/Coop/LVL_Coop_C01 UEPY verify_terrain_collision.py
"""

import os
import random
import struct
import zlib

import unreal

HEADER = struct.Struct("<4sIiifffffII")
SAMPLES = 400
P99_LIMIT_CM = 20.0
MAX_LIMIT_CM = 50.0


def read_bake(path):
    with open(path, "rb") as handle:
        data = handle.read()
    _, _, nx, ny, ox, oy, step, z0, _, raw_size, comp_size = HEADER.unpack_from(data, 0)
    raw = zlib.decompress(data[HEADER.size:HEADER.size + comp_size])
    n = nx * ny
    top = struct.unpack_from(f"<{n}H", raw, 0)
    low = raw[2 * n:3 * n]
    return nx, ny, ox, oy, step, z0, top, low


def main():
    nx, ny, ox, oy, step, z0, top, low = read_bake(os.path.join(os.environ["TN_VOLUME_DIR"], "coop", "bake.bin"))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(os.environ.get("TN_COOP_LEVEL", "/Game/Maps/Coop/LVL_Coop_C01"))
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    rng = random.Random(9)
    cells = [idx for idx in range(nx * ny) if low[idx] != 255]
    errors = []
    for idx in rng.sample(cells, min(SAMPLES, len(cells))):
        i, j = idx % nx, idx // nx
        x, y, z = ox + i * step, oy + j * step, z0 + top[idx]
        hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, z + 200.0), unreal.Vector(x, y, z - 500.0),
                                                     unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [],
                                                     unreal.DrawDebugTrace.NONE, True)
        if hit is not None and hit.to_tuple()[0]:
            errors.append(abs(hit.to_tuple()[4].z - z))
        else:
            errors.append(500.0)
    errors.sort()
    p99, worst = errors[int(0.99 * (len(errors) - 1))], errors[-1]
    unreal.log(f"[R9] p99 {p99:.1f} cm, max {worst:.1f} cm en {len(errors)} trazas")
    if p99 > P99_LIMIT_CM or worst > MAX_LIMIT_CM:
        raise RuntimeError(f"[R9] la colision no sigue la malla fuente (p99 {p99:.1f} cm, max {worst:.1f} cm)")


main()
```

- [ ] **Paso 2: medir.** `TN_VOLUME_DIR=…\C01_camino` y UEPY `verify_terrain_collision.py` → leer `[R9]` en el log.
  - Si pasa (p99 ≤ 20 y máx. ≤ 50): no se toca C++; anotar las cifras en el commit.
  - Si falla: en `TN_TerrainMeshAsset.cpp:132` `Mesh->NaniteSettings.FallbackPercentTriangles = 0.1f;` pasa a `Mesh->NaniteSettings.FallbackPercentTriangles = 1.f;` con el comentario `// R9: la colisión compleja de una malla Nanite sale de la reserva; al 100 % coincide con la malla fuente (ya decimada en Python, §2.5).`; BUILD; reimportar C01 con `TN_REGENERATE=1` (Tarea 24, paso 7) y repetir la medida hasta que pase.
- [ ] **Paso 3: smoke headless (gate F1).** SMOKE `/Game/Maps/Coop/LVL_Coop_C01` `[Coop] Mapa preparado C01 listo` → el proceso sale con código 0. En el log: `[Coop] Proveedor de suelo seguro registrado (C01)`; **cero** líneas `[MapVariantLoader]` y `[ProcMap] Mapa listo` (no hay ProcMesh ni generación procedural); ninguna `[ProcMap] Pawn local liberado tras` con más de 1,00 s.
- [ ] **Paso 4: tests.** UETEST `Coop` y PYTEST → todo verde (gate F1: tests de `IsAllowed`/`WalkableZAt`).
- [ ] **Paso 5: commit.** `git add Scripts/verify_terrain_collision.py` (+ `Source/Tortunabo/Private/World/TN_TerrainMeshAsset.cpp` y `Content/Terrain/Volumes/C01` si hubo corrección) · `git commit -m "test(terreno): R9 colisión del terreno contra el bake (p99 X cm) y smoke de LVL_Coop_C01"` (X = cifra medida).

---

# F2 — Coop sobre mapas fijos

### Tarea 26: `LayoutFromCoopData` (layout sintético, R7, biomas y `ModuleAt` seguro)

**Files:**
- Modify: `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h`, `Source/Tortunabo/Private/Tests/TN_CoopMapDecisionsTest.cpp`

**Interfaces:**
- Consumes: `TNProcMap::FLayout`, `FPathSample`, `FFeature`, `EFeature`, `FKillBox` (`TN_ProcMapLayout.h`); `FCoopMarker` (23).
- Produces: `TNCoopMap::FCoopZone { double S0; double S1; ETNProcBiome Biome; }`; `TNCoopMap::FCoopLayoutInput { uint32 Seed; double WorldSize; TArray<FVector> Main; TArray<double> MainWidth; TArray<FCoopZone> Zones; TArray<TNProcMap::FKillBox> KillBoxes; TArray<FCoopMarker> Markers; }` (todo en espacio del MAPA del generador); `TNCoopMap::BiomeCellSize = 800.0`, `StartAreaRadius = 1200.0`; `bool TNCoopMap::LayoutFromCoopData(const FCoopLayoutInput&, TNProcMap::FLayout&, FString& OutError)`.

- [ ] **Paso 1: test que falla.** Añadir a `TN_CoopMapDecisionsTest.cpp`:

```cpp
namespace TNCoopMapDecisionsTest
{
	TNCoopMap::FCoopMarker Marker(ETNCoopMarkerKind Kind, const FVector& Pos, double Yaw = 0.0, int32 Order = 0)
	{
		TNCoopMap::FCoopMarker M;
		M.Kind = Kind;
		M.Pos = Pos;
		M.Yaw = Yaw;
		M.Order = Order;
		return M;
	}

	/** Camino recto de 18 m a lo largo de X (espacio del mapa, Z = 400 sobre el agua) con salida, meta a −X, nido y medusa. */
	TNCoopMap::FCoopLayoutInput MakeInput()
	{
		TNCoopMap::FCoopLayoutInput In;
		In.Seed = 42;
		In.WorldSize = 2000.0;
		In.Main = { FVector(100.0, 1000.0, 400.0), FVector(1000.0, 1000.0, 400.0), FVector(1900.0, 1000.0, 400.0) };
		In.MainWidth = { 600.0, 600.0, 600.0 };
		In.Zones = { { 0.0, 1800.0, ETNProcBiome::Beach } };
		TNProcMap::FKillBox Box;
		Box.Center = FVector(1000.0, 300.0, -25.0);
		Box.Half = FVector(1000.0, 200.0, 225.0);
		In.KillBoxes = { Box };
		In.Markers = { Marker(ETNCoopMarkerKind::Start, FVector(100.0, 1000.0, 400.0)),
			Marker(ETNCoopMarkerKind::Finish, FVector(1900.0, 1000.0, 400.0), 180.0),
			Marker(ETNCoopMarkerKind::EggNest, FVector(1010.0, 1000.0, 400.0), 0.0, 1),
			Marker(ETNCoopMarkerKind::Jellyfish, FVector(500.0, 1200.0, 400.0)) };
		return In;
	}

	int32 CountFeatures(const TNProcMap::FLayout& Layout, TNProcMap::EFeature Type)
	{
		return Layout.Features.FilterByPredicate([Type](const TNProcMap::FFeature& F) { return F.Type == Type; }).Num();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopLayoutTest, "Tortunabo.Coop.Layout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopLayoutTest::RunTest(const FString& Parameters)
{
	using namespace TNProcMap;
	using namespace TNCoopMapDecisionsTest;
	FLayout Layout;
	FString Error;
	TestTrue(TEXT("Layout válido"), TNCoopMap::LayoutFromCoopData(MakeInput(), Layout, Error) && Layout.bValid);
	TestEqual(TEXT("R7: WorldSize = lado del mapa"), Layout.WorldSize, 2000.0);
	TestEqual(TEXT("Largo del camino"), Layout.MainLength(), 1800.0, 0.5);
	TestTrue(TEXT("Bioma de la zona"), Layout.Main[1].Biome == ETNProcBiome::Beach);
	TestEqual(TEXT("Z caminable de la muestra"), Layout.Main[1].Z, 400.0);
	TestTrue(TEXT("Dirección de la muestra"), Layout.Main[1].Dir.Equals(FVector2D(1.0, 0.0)));
	TestEqual(TEXT("ModuleAt sin módulos no revienta"), Layout.ModuleAt(FVector2D(1000.0, 1000.0)), INDEX_NONE);
	TestTrue(TEXT("Lejos del borde"), Layout.BorderDistAt(FVector2D(1000.0, 1000.0)) > 1e8);
	TestEqual(TEXT("Salida"), CountFeatures(Layout, EFeature::StartArea), 1);
	TestEqual(TEXT("Meta"), CountFeatures(Layout, EFeature::Finish), 1);
	TestEqual(TEXT("Nido"), CountFeatures(Layout, EFeature::EggNest), 1);
	TestEqual(TEXT("Medusa como rebotador"), CountFeatures(Layout, EFeature::Bouncer), 1);
	const FFeature* Finish = Layout.Features.FindByPredicate([](const FFeature& F) { return F.Type == EFeature::Finish; });
	TestTrue(TEXT("Meta hacia −X (giro 180)"), Finish && Finish->Dir.Equals(FVector2D(-1.0, 0.0), 1e-4));
	const FFeature* Nest = Layout.Features.FindByPredicate([](const FFeature& F) { return F.Type == EFeature::EggNest; });
	TestTrue(TEXT("Nido: orden y muestra más cercana"), Nest && Nest->Aux == 1 && Nest->PathIndex == 1);
	TestTrue(TEXT("EndPoint en la meta"), Layout.EndPoint.Equals(FVector2D(1900.0, 1000.0)));
	double W[NumBiomes];
	Layout.BiomeWeightsAt(FVector2D(1000.0, 1000.0), W);
	TestEqual(TEXT("Raster de biomas: todo playa"), W[BiomeIndex(ETNProcBiome::Beach)], 1.0, 1e-6);
	TestEqual(TEXT("Cajas de muerte"), Layout.KillBoxes.Num(), 1);
	TNCoopMap::FCoopLayoutInput Bad = MakeInput();
	Bad.Main.SetNum(1);
	TestFalse(TEXT("Sin camino no hay layout"), TNCoopMap::LayoutFromCoopData(Bad, Layout, Error));
	return true;
}
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila (`FCoopLayoutInput`).
- [ ] **Paso 3: implementación.** Añadir a `TN_CoopMapDecisions.h` (include `World/ProcMap/TN_ProcMapLayout.h`):

```cpp
	struct FCoopZone
	{
		double S0 = 0.0;
		double S1 = 0.0;
		ETNProcBiome Biome = ETNProcBiome::Beach;
	};

	/** Entrada del layout sintético, en espacio del MAPA del generador (mundo − ubicación del generador). */
	struct FCoopLayoutInput
	{
		uint32 Seed = 0;
		double WorldSize = 0.0;
		TArray<FVector> Main;
		TArray<double> MainWidth;
		TArray<FCoopZone> Zones;
		TArray<TNProcMap::FKillBox> KillBoxes;
		TArray<FCoopMarker> Markers;
	};

	inline constexpr double BiomeCellSize = 800.0;
	inline constexpr double StartAreaRadius = 1200.0;

	namespace LayoutDetail
	{
		inline ETNProcBiome BiomeAtS(const TArray<FCoopZone>& Zones, double S)
		{
			for (const FCoopZone& Z : Zones)
			{
				if (S >= Z.S0 && S <= Z.S1) { return Z.Biome; }
			}
			return Zones.Num() > 0 ? Zones.Last().Biome : ETNProcBiome::Beach;
		}

		inline int32 NearestSample(const TArray<TNProcMap::FPathSample>& Main, const FVector& P)
		{
			int32 Best = INDEX_NONE;
			double BestScore = TNumericLimits<double>::Max();
			for (int32 i = 0; i < Main.Num(); ++i)
			{
				const FVector D(Main[i].P.X - P.X, Main[i].P.Y - P.Y, Main[i].Z - P.Z);
				const double Score = D.X * D.X + D.Y * D.Y + 2.25 * D.Z * D.Z;
				if (Score < BestScore) { BestScore = Score; Best = i; }
			}
			return Best;
		}

		inline FVector2D YawDir(double YawDeg)
		{
			const double A = FMath::DegreesToRadians(YawDeg);
			return FVector2D(FMath::Cos(A), FMath::Sin(A));
		}

		inline void FillMain(const FCoopLayoutInput& In, TNProcMap::FLayout& Out)
		{
			double S = 0.0;
			for (int32 i = 0; i < In.Main.Num(); ++i)
			{
				TNProcMap::FPathSample& Smp = Out.Main.AddDefaulted_GetRef();
				if (i > 0) { S += FVector2D::Distance(FVector2D(In.Main[i - 1]), FVector2D(In.Main[i])); }
				const int32 A = FMath::Max(0, i - 1);
				const int32 B = FMath::Min(In.Main.Num() - 1, i + 1);
				Smp.P = FVector2D(In.Main[i]);
				Smp.Dir = FVector2D(In.Main[B] - In.Main[A]).GetSafeNormal();
				Smp.Z = In.Main[i].Z;
				Smp.Width = In.MainWidth[i];
				Smp.S = S;
				Smp.Biome = BiomeAtS(In.Zones, S);
			}
		}

		/** Sin módulos: una celda de raster que no pertenece a ninguno (ModuleAt = INDEX_NONE, borde lejano). */
		inline void FillModuleRaster(const FCoopLayoutInput& In, TNProcMap::FLayout& Out)
		{
			Out.Params.Seed = In.Seed;
			Out.Params.CellSize = In.WorldSize;
			Out.WorldSize = In.WorldSize;
			Out.RasterW = 1;
			Out.RasterH = 1;
			Out.ModuleOfCell = { static_cast<int16>(INDEX_NONE) };
			Out.BorderDist = { 1e9f };
			Out.ModuleDist = { 1e9f };
		}

		/** Pesos de bioma del raster grueso: el de la muestra del camino más cercana (en planta). */
		inline void FillBiomeRaster(TNProcMap::FLayout& Out)
		{
			Out.BiomeCell = BiomeCellSize;
			Out.BiomeW = FMath::Max(1, FMath::CeilToInt32(Out.WorldSize / BiomeCellSize));
			Out.BiomeH = Out.BiomeW;
			Out.BiomeWeights.Init(0.f, Out.BiomeW * Out.BiomeH * TNProcMap::NumBiomes);
			Out.LevelField.Init(0.f, Out.BiomeW * Out.BiomeH);
			Out.ElevatedField.Init(0.f, Out.BiomeW * Out.BiomeH);
			for (int32 Y = 0; Y < Out.BiomeH; ++Y)
			{
				for (int32 X = 0; X < Out.BiomeW; ++X)
				{
					const FVector C((X + 0.5) * BiomeCellSize, (Y + 0.5) * BiomeCellSize, 0.0);
					int32 Best = 0;
					double BestSq = TNumericLimits<double>::Max();
					for (int32 i = 0; i < Out.Main.Num(); ++i)
					{
						const double Sq = FVector2D::DistSquared(Out.Main[i].P, FVector2D(C));
						if (Sq < BestSq) { BestSq = Sq; Best = i; }
					}
					Out.BiomeWeights[(Y * Out.BiomeW + X) * TNProcMap::NumBiomes + TNProcMap::BiomeIndex(Out.Main[Best].Biome)] = 1.f;
				}
			}
		}

		inline void AddMarkerFeature(const FCoopMarker& M, TNProcMap::FLayout& Out)
		{
			using namespace TNProcMap;
			FFeature F;
			F.Location = M.Pos;
			F.Target = M.Pos;
			F.Dir = YawDir(M.Yaw);
			F.PathIndex = NearestSample(Out.Main, M.Pos);
			F.Biome = Out.Main.IsValidIndex(F.PathIndex) ? Out.Main[F.PathIndex].Biome : ETNProcBiome::Beach;
			switch (M.Kind)
			{
			case ETNCoopMarkerKind::Start:
				F.Type = EFeature::StartArea;
				F.Radius = StartAreaRadius;
				Out.StartPoint = FVector2D(M.Pos);
				break;
			case ETNCoopMarkerKind::Finish:
				F.Type = EFeature::Finish;
				F.Length = M.FinishDepth;
				F.Width = M.FinishWidth;
				F.Height = M.FinishWidth;
				Out.EndPoint = FVector2D(M.Pos);
				break;
			case ETNCoopMarkerKind::EggNest:
				F.Type = EFeature::EggNest;
				F.Aux = M.Order;
				break;
			case ETNCoopMarkerKind::Jellyfish:
				F.Type = EFeature::Bouncer;
				break;
			default:
				return;   // Mecánicas y anclajes de puente no son features: los crea el generador (SpawnCoopElements).
			}
			Out.Features.Add(F);
		}
	}

	/** Layout de juego de un mapa preparado: camino y zonas del DA, features desde los marcadores del nivel. */
	inline bool LayoutFromCoopData(const FCoopLayoutInput& In, TNProcMap::FLayout& Out, FString& OutError)
	{
		Out = TNProcMap::FLayout();
		if (In.Main.Num() < 2 || In.MainWidth.Num() != In.Main.Num() || In.WorldSize <= 0.0)
		{
			OutError = FString::Printf(TEXT("camino de %d muestras (anchos %d) en un mapa de %.0f cm"), In.Main.Num(), In.MainWidth.Num(), In.WorldSize);
			Out.FailReason = "coop: camino o tamaño invalidos";
			return false;
		}
		LayoutDetail::FillModuleRaster(In, Out);
		LayoutDetail::FillMain(In, Out);
		Out.StartPoint = Out.Main[0].P;
		Out.EndPoint = Out.Main.Last().P;
		LayoutDetail::FillBiomeRaster(Out);
		for (const FCoopMarker& M : In.Markers)
		{
			LayoutDetail::AddMarkerFeature(M, Out);
		}
		Out.KillBoxes = In.KillBoxes;
		Out.bValid = true;
		return true;
	}
```

- [ ] **Paso 4: ver pasar.** BUILD; UETEST `Coop.Layout` → `Success`.
- [ ] **Paso 5: commit.** `git add Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h Source/Tortunabo/Private/Tests/TN_CoopMapDecisionsTest.cpp` · `git commit -m "feat(coop): layout sintético del mapa preparado con biomas, nidos, meta y medusas"`

### Tarea 27: Construcción Prepared del generador (R4, `PreparedMapId`, agua, fauna)

**Files:**
- Modify: `Source/Tortunabo/Public/World/ProcMap/TN_ProcMapGenerator.h:42-58, 229-260`, `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Prepared.cpp`, `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator.cpp:116-137, 159-164`, `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Spawn.cpp:231, 431`, `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Build.cpp:1640, 1666`, `Source/Tortunabo/Private/Tests/TN_CoopMapDataTest.cpp`, `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h`

**Interfaces:**
- Consumes: `LayoutFromCoopData` (26); `UTN_CoopMapData` (14); `ATN_CoopMarker::ToView` (23); `SpawnFauna` (21); `bPreparedFinishInWater` (20).
- Produces: `FTNProcMapNetConfig::PreparedMapId` (`UPROPERTY FName`); `TNCoopMap::CheckPreparedMapId(FName Server, FName Local) -> bool`; `ATN_ProcMapGenerator::BuildPreparedFromNetConfig()` completo; `bool BuildPreparedLayout(FString& OutError)`; `void BuildTerrainFromBake()`; `double PreparedGroundMap(const FVector2D& MapXY, double ZHintMap) const`; log `[Coop] Mapa preparado %s listo (generación %d): %d muestras, %d nidos, %d rebotadores`.

- [ ] **Paso 1: tests que fallan.** En `TN_CoopMapDecisionsTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopMapIdTest, "Tortunabo.Coop.MapId",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopMapIdTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Mismo mapa"), TNCoopMap::CheckPreparedMapId(TEXT("CP01"), TEXT("CP01")));
	TestFalse(TEXT("Otro mapa en el cliente"), TNCoopMap::CheckPreparedMapId(TEXT("CP01"), TEXT("C01")));
	TestFalse(TEXT("Servidor sin id"), TNCoopMap::CheckPreparedMapId(NAME_None, NAME_None));
	return true;
}
```

  En `TN_CoopMapDataTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopPreparedBuildTest, "Tortunabo.Coop.PreparedBuild",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopPreparedBuildTest::RunTest(const FString& Parameters)
{
	TNCoopMapDataTest::FPreparedWorld Scene(TNCoopMapDataTest::LoadT01(*this));
	Scene.Generator->SetTerrainOnly(true);                       // sin peligros ni conchas: solo terreno y layout
	Scene.Generator->ServerGenerate(7, ETNProcGameMode::Coop, ETNProcDifficulty::Normal);
	TestTrue(TEXT("Mapa listo"), Scene.Generator->IsMapReady());
	TestEqual(TEXT("Camino del DA"), Scene.Generator->GetLayout().Main.Num(), 3);
	TestEqual(TEXT("PreparedMapId replicable"), Scene.Generator->GetNetConfig().PreparedMapId, FName(TEXT("T01")));
	TestEqual(TEXT("Alturas del bake (mundo)"), Scene.Generator->GetTerrainHeightAt(FVector(1000.0, 1000.0, 0.0)), 0.f, 1.f);
	TestEqual(TEXT("Progreso por el índice del generador"), Scene.Generator->GetPathProgress(FVector(1000.0, 1000.0, 0.0)), 900.f, 1.f);
	return true;
}
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila (`PreparedMapId`, `CheckPreparedMapId`).
- [ ] **Paso 3: red.** `FTNProcMapNetConfig` (`TN_ProcMapGenerator.h:42-58`), tras `Generation`:

```cpp
	/** Mapa preparado (Coop): id del DA del servidor; el cliente comprueba que tiene el mismo nivel. NAME_None = procedural. */
	UPROPERTY(BlueprintReadOnly, Category = "ProcMap")
	FName PreparedMapId;
```

  `TN_CoopMapDecisions.h`: `inline bool CheckPreparedMapId(FName Server, FName Local) { return !Server.IsNone() && Server == Local; }`. En `TN_ProcMapGenerator.cpp`: `ServerGenerate` (línea 131) añade antes de `NetConfig.Generation += 1;` → `NetConfig.PreparedMapId = IsPrepared() ? CoopData->MapId : NAME_None;`; `OnRep_NetConfig` (línea 157) pasa a:

```cpp
void ATN_ProcMapGenerator::OnRep_NetConfig()
{
	if (!NetConfig.PreparedMapId.IsNone() && !TNCoopMap::CheckPreparedMapId(NetConfig.PreparedMapId, CoopData ? CoopData->MapId : NAME_None))
	{
		UE_LOG(LogTortunabo, Error, TEXT("[Coop] PreparedMapId del servidor (%s) no coincide con el nivel local (%s): no se construye."),
			*NetConfig.PreparedMapId.ToString(), CoopData ? *CoopData->MapId.ToString() : TEXT("sin DA"));
		return;
	}
	if (NetConfig.Generation > 0 && NetConfig.Generation != BuiltGeneration)
	{
		BuildFromNetConfig();
	}
}
```

  (El `BeginPlay` del cliente que entra tarde pasa por `BuildFromNetConfig` → `BuildPreparedFromNetConfig`, que repite la comprobación: primera línea del Paso 4.)
- [ ] **Paso 4: construcción.** En `TN_ProcMapGenerator.h`, `private:`: `bool BuildPreparedLayout(FString& OutError);`, `void BuildTerrainFromBake();`, `double PreparedGroundMap(const FVector2D& MapXY, double ZHintMap) const;`. Sustituir el cuerpo F1 de `BuildPreparedFromNetConfig` en `TN_ProcMapGenerator_Prepared.cpp` (includes: `EngineUtils.h`, `World/ProcMap/TN_CoopMarker.h`, `World/ProcMap/TN_CoopMapDecisions.h`):

```cpp
void ATN_ProcMapGenerator::BuildPreparedFromNetConfig()
{
	UWorld* World = GetWorld();
	if (!NetConfig.PreparedMapId.IsNone() && !TNCoopMap::CheckPreparedMapId(NetConfig.PreparedMapId, CoopData->MapId))
	{
		UE_LOG(LogTortunabo, Error, TEXT("[Coop] PreparedMapId %s ≠ %s: no se construye."), *NetConfig.PreparedMapId.ToString(), *CoopData->MapId.ToString());
		return;
	}
	Clear();
	FString Error;
	if (!BuildPreparedLayout(Error))
	{
		UE_LOG(LogTortunabo, Error, TEXT("[Coop] Layout del mapa preparado %s: %s"), *CoopData->MapId.ToString(), *Error);
		return;
	}
	BuildTerrainFromBake();
	BuildWater();
	SpawnFauna();
	if (World->IsGameWorld())
	{
		SpawnTraversalActors();
		if (World->GetNetMode() != NM_Client)
		{
			SpawnServerActors();
		}
		if (!bTerrainOnly)
		{
			SpawnHazards();
			SpawnShells();
		}
	}
	BuildProgressIndex();
	bMapReady = true;
	bReportedReady = false;
	BuiltGeneration = NetConfig.Generation;
	UE_LOG(LogTortunabo, Log, TEXT("[Coop] Mapa preparado %s listo (generación %d): %d muestras, %d features, %d cajas de muerte."),
		*CoopData->MapId.ToString(), BuiltGeneration, Layout.Main.Num(), Layout.Features.Num(), Layout.KillBoxes.Num());
	OnMapGeneratedNative.Broadcast(BuiltGeneration);
	OnMapGenerated.Broadcast(BuiltGeneration);
	if (World->IsGameWorld() && World->GetNetMode() == NM_Client)
	{
		ReportReadyToServer();
	}
}

bool ATN_ProcMapGenerator::BuildPreparedLayout(FString& OutError)
{
	TNCoopMap::FCoopLayoutInput In;
	In.Seed = static_cast<uint32>(NetConfig.Seed);
	In.WorldSize = (CoopData->Bake.NX - 1) * CoopData->Bake.Step;
	for (const FTNCoopPathPoint& P : CoopData->Main)
	{
		In.Main.Add(WorldToMap(P.Pos));
		In.MainWidth.Add(P.Width);
	}
	for (const FTNCoopZone& Z : CoopData->Zones)
	{
		In.Zones.Add({ Z.S0, Z.S1, Z.Biome });
	}
	for (const FTNCoopKillBox& K : CoopData->KillBoxes)
	{
		TNProcMap::FKillBox Box;
		Box.Center = WorldToMap(K.Center);
		Box.Dir = FVector2D(FMath::Cos(FMath::DegreesToRadians(K.Yaw)), FMath::Sin(FMath::DegreesToRadians(K.Yaw)));
		Box.Half = K.Extent;
		In.KillBoxes.Add(Box);
	}
	bPreparedFinishInWater = CoopData->Finish.bInWater;
	for (TActorIterator<ATN_CoopMarker> It(GetWorld()); It; ++It)
	{
		TNCoopMap::FCoopMarker View = It->ToView();
		View.Pos = WorldToMap(View.Pos);
		View.Yaw -= GetActorRotation().Yaw;
		if (View.Kind == ETNCoopMarkerKind::Finish) { bPreparedFinishInWater = View.bInWater; }
		In.Markers.Add(View);
	}
	return TNCoopMap::LayoutFromCoopData(In, Layout, OutError);
}

void ATN_ProcMapGenerator::BuildTerrainFromBake()
{
	const TNCoopMap::FSafeGrid& Grid = CoopData->GetSafeGrid();
	const FVector Base = GetActorLocation();
	LatticeOrigin = Grid.Origin - FVector2D(Base.X, Base.Y);
	LatticeSpacing = Grid.Step;
	LatticeNX = Grid.NX;
	LatticeNY = Grid.NY;
	TerrainDetail = TNProcMap::FTerrainDetail();
	const int32 N = Grid.NX * Grid.NY;
	Heights.SetNumUninitialized(N);
	PathDist.SetNumUninitialized(N);
	PathMask.Init(0, N);
	for (int32 i = 0; i < N; ++i)
	{
		Heights[i] = static_cast<float>(Grid.Z0 + Grid.TopRel[i] - Base.Z);
		PathDist[i] = Grid.PathDistDm[i] == TNCoopMap::FarPathDm ? 1e9f : Grid.PathDistDm[i] * 10.f;
		PathMask[i] = Grid.PathDistDm[i] <= 20 ? 1 : 0;
	}
}

double ATN_ProcMapGenerator::PreparedGroundMap(const FVector2D& MapXY, double ZHintMap) const
{
	const FVector World = MapToWorld(FVector(MapXY.X, MapXY.Y, ZHintMap));
	return WalkableZAt(FVector2D(World.X, World.Y), static_cast<float>(World.Z)) - GetActorLocation().Z;
}
```

  Las mecánicas (Tarea 29) y la vegetación (Tarea 30) se enchufan en esta función en sus propias tareas.
- [ ] **Paso 5: R4 y agua.** `TN_ProcMapGenerator_Spawn.cpp:231` (salidas): `const FVector Loc = MapToWorld2D(P, (IsPrepared() ? PreparedGroundMap(P, Layout.Main[0].Z) : TerrainHeightMap(P)) + 110.0);`. `:431` (nidos): `const FVector Loc = MapToWorld2D(C, IsPrepared() ? F.Location.Z : TerrainHeightMap(C));`. `TN_ProcMapGenerator_Build.cpp:1640`: `if (BasicPlane && !IsPrepared())` (el nivel ya trae su lámina `TN_Water`); tras la comprobación de mundo de juego (`:1666`): `if (IsPrepared() && !CoopData->bSwim) { return; }` (solo los mapas con `water.swim`, C01, tienen volumen nadable).
- [ ] **Paso 6: ver pasar.** BUILD; UETEST `Coop` → todos `Success` (incluidos `Coop.MapId` y `Coop.PreparedBuild`); UETEST `ProcMap` sin regresiones.
- [ ] **Paso 7: smoke.** SMOKE `/Game/Maps/Coop/LVL_Coop_C01` `[Coop] Mapa preparado C01 listo (generación 1)` → sale con 0; el log trae `[Fauna]` con más de 0 animales (R5) y ninguna línea `PreparedMapId`.
- [ ] **Paso 8: commit.** `git add Source/Tortunabo/Public/World/ProcMap/TN_ProcMapGenerator.h Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator.cpp Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Prepared.cpp Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Spawn.cpp Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Build.cpp Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h Source/Tortunabo/Private/Tests/TN_CoopMapDecisionsTest.cpp Source/Tortunabo/Private/Tests/TN_CoopMapDataTest.cpp` · `git commit -m "feat(coop): el generador construye el mapa preparado desde el DA y los marcadores"`

### Tarea 28: `SeaNear` desde la rejilla en mapas fijos (R6)

**Files:**
- Modify: `Source/Tortunabo/Public/World/ProcMap/TN_CoopGridDecisions.h`, `Source/Tortunabo/Private/Tests/TN_CoopGridDecisionsTest.cpp`, `Source/Tortunabo/Public/World/ProcMap/TN_ProcMapGenerator.h`, `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Prepared.cpp`, `Source/Tortunabo/Private/Audio/TN_AmbientSoundscape.cpp:424`

**Interfaces:**
- Consumes: `ComputeSeaDistance`, `SeaNearFromDistance` (13).
- Produces: `TNCoopMap::SeaNearAt(const FSafeGrid&, const TArray<uint16>& SeaDm, const FVector2D& WorldXY) -> float`; `float ATN_ProcMapGenerator::GetSeaNear(const FVector2D& MapPos) const`; miembro `TArray<uint16> PreparedSeaDm`.

- [ ] **Paso 1: test que falla.** Añadir a `TN_CoopGridDecisionsTest.cpp`, dentro de `FTNCoopSeaDistanceTest::RunTest` antes del `return`:

```cpp
	TestEqual(TEXT("SeaNearAt en el mar"), TNCoopMap::SeaNearAt(Grid, Dm, FVector2D(1700.0, 300.0)), 1.f);
	TestEqual(TEXT("SeaNearAt fuera de la rejilla"), TNCoopMap::SeaNearAt(Grid, Dm, FVector2D(-5000.0, 0.0)), 0.f);
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila.
- [ ] **Paso 3: implementación.** En `TN_CoopGridDecisions.h`:

```cpp
	inline float SeaNearAt(const FSafeGrid& Grid, const TArray<uint16>& SeaDm, const FVector2D& WorldXY)
	{
		int32 I = 0;
		int32 J = 0;
		if (!Grid.IsValid() || SeaDm.Num() != Grid.NX * Grid.NY || !Grid.CellOf(WorldXY, I, J))
		{
			return 0.f;
		}
		return SeaNearFromDistance(SeaDm[Grid.Index(I, J)]);
	}
```

  `TN_ProcMapGenerator.h`: público `/** 0..1: cercanía del mar abierto (rompientes del paisaje sonoro). */ float GetSeaNear(const FVector2D& MapPos) const;`; privado `TArray<uint16> PreparedSeaDm;`. En `BuildTerrainFromBake` (Tarea 27), al final: `TNCoopMap::ComputeSeaDistance(Grid, CoopData->WaterZ, PreparedSeaDm);`. En `TN_ProcMapGenerator_Prepared.cpp`:

```cpp
float ATN_ProcMapGenerator::GetSeaNear(const FVector2D& MapPos) const
{
	if (IsPrepared())
	{
		const FVector World = MapToWorld(FVector(MapPos.X, MapPos.Y, 0.0));
		return TNCoopMap::SeaNearAt(CoopData->GetSafeGrid(), PreparedSeaDm, FVector2D(World.X, World.Y));
	}
	return FMath::Clamp(1.f - static_cast<float>(Layout.CoastY(MapPos.X) - MapPos.Y) / 12000.f, 0.f, 1.f);
}
```

  `TN_AmbientSoundscape.cpp:424` pasa a `Ctx.SeaNear = Gen->GetSeaNear(MapPos);` (mismo valor en el procedural: `TNAmbienceSaturate` es la misma pinza 0..1).
- [ ] **Paso 4: ver pasar.** BUILD; UETEST `Coop.SeaDistance` → `Success`.
- [ ] **Paso 5: commit.** `git add Source/Tortunabo/Public/World/ProcMap/TN_CoopGridDecisions.h Source/Tortunabo/Private/Tests/TN_CoopGridDecisionsTest.cpp Source/Tortunabo/Public/World/ProcMap/TN_ProcMapGenerator.h Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Prepared.cpp Source/Tortunabo/Private/Audio/TN_AmbientSoundscape.cpp` · `git commit -m "fix(audio): rompientes del mar en mapas fijos desde la distancia al mar horneada"`

### Tarea 29: Mecánicas de la carrera con `ATN_BeachElement::SpawnElement` (y auditoría R10)

**Files:**
- Modify: `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h`, `Source/Tortunabo/Private/Tests/TN_CoopMapDecisionsTest.cpp`, `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Prepared.cpp`

**Interfaces:**
- Consumes: `ATN_BeachElement::SpawnElement(UWorld*, const FTransform&, const FTNBeachElementSpec&)` (`TN_BeachElement.h:37`); `ATN_CoopMarker::ToView`.
- Produces: `TNCoopMap::IsCoopMechanic(ETNBeachElement) -> bool` (catapulta, trampolín, plataforma móvil, plataforma tambaleante, paso de quads, tanque de juguete, zona de gaviotas); `TNCoopMap::ElementSeed(int32 RoundSeed, FName Id) -> int32`; `void ATN_ProcMapGenerator::SpawnCoopElements()` (servidor); logs `[Coop] Mecánica %s (%s) en %s` y `[Coop] %d mecánicas de la playa creadas, %d omitidas.`

- [ ] **Paso 1: test que falla.** En `TN_CoopMapDecisionsTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopElementsTest, "Tortunabo.Coop.Elements",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopElementsTest::RunTest(const FString& Parameters)
{
	const ETNBeachElement Allowed[] = { ETNBeachElement::Catapult, ETNBeachElement::Trampoline, ETNBeachElement::MovingPlatform,
		ETNBeachElement::WobblyPlatform, ETNBeachElement::QuadLane, ETNBeachElement::ToyTank, ETNBeachElement::GullZone };
	for (const ETNBeachElement E : Allowed)
	{
		TestTrue(*FString::Printf(TEXT("%s es mecánica del Coop"), *UEnum::GetValueAsString(E)), TNCoopMap::IsCoopMechanic(E));
	}
	TestFalse(TEXT("La fortaleza colosal no (no cabe en un camino de 4-10 m)"), TNCoopMap::IsCoopMechanic(ETNBeachElement::FortressColossal));
	TestFalse(TEXT("El decorado no"), TNCoopMap::IsCoopMechanic(ETNBeachElement::Rock));
	TestEqual(TEXT("Semilla determinista"), TNCoopMap::ElementSeed(7, TEXT("cat_01")), TNCoopMap::ElementSeed(7, TEXT("cat_01")));
	TestNotEqual(TEXT("Distinta por id"), TNCoopMap::ElementSeed(7, TEXT("cat_01")), TNCoopMap::ElementSeed(7, TEXT("cat_02")));
	TestNotEqual(TEXT("Distinta por ronda"), TNCoopMap::ElementSeed(7, TEXT("cat_01")), TNCoopMap::ElementSeed(8, TEXT("cat_01")));
	TestTrue(TEXT("No negativa"), TNCoopMap::ElementSeed(-3, TEXT("tank_01")) >= 0);
	return true;
}
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila.
- [ ] **Paso 3: implementación.** `TN_CoopMapDecisions.h`:

```cpp
	/** Mecánicas de la carrera que el Coop coloca desde marcadores (plan maestro §3.1). */
	inline bool IsCoopMechanic(ETNBeachElement Element)
	{
		switch (Element)
		{
		case ETNBeachElement::Catapult:
		case ETNBeachElement::Trampoline:
		case ETNBeachElement::MovingPlatform:
		case ETNBeachElement::WobblyPlatform:
		case ETNBeachElement::QuadLane:
		case ETNBeachElement::ToyTank:
		case ETNBeachElement::GullZone:
			return true;
		default:
			return false;
		}
	}

	/** Semilla de la variante de un elemento: la misma en todas las máquinas, distinta por marcador y por ronda. */
	inline int32 ElementSeed(int32 RoundSeed, FName Id)
	{
		return static_cast<int32>(HashCombine(GetTypeHash(Id.ToString()), GetTypeHash(RoundSeed)) & 0x7fffffffu);
	}
```

  `TN_ProcMapGenerator.h`, `private:`: `/** Servidor: mecánicas de la carrera desde los marcadores BeachElement. */ void SpawnCoopElements();`. En `BuildPreparedFromNetConfig` (Tarea 27), dentro de `if (World->GetNetMode() != NM_Client)` tras `SpawnServerActors();`: `if (!bTerrainOnly) { SpawnCoopElements(); }`. En `TN_ProcMapGenerator_Prepared.cpp` (include `World/Beach/TN_BeachElement.h`):

```cpp
void ATN_ProcMapGenerator::SpawnCoopElements()
{
	int32 Spawned = 0;
	int32 Skipped = 0;
	for (TActorIterator<ATN_CoopMarker> It(GetWorld()); It; ++It)
	{
		const TNCoopMap::FCoopMarker M = It->ToView();
		if (M.Kind != ETNCoopMarkerKind::BeachElement)
		{
			continue;
		}
		if (!TNCoopMap::IsCoopMechanic(M.Element))
		{
			++Skipped;
			UE_LOG(LogTortunabo, Warning, TEXT("[Coop] %s: %s no es una mecánica del Coop."), *M.Id.ToString(), *UEnum::GetValueAsString(M.Element));
			continue;
		}
		FTNBeachElementSpec Spec;
		Spec.Element = M.Element;
		Spec.Seed = TNCoopMap::ElementSeed(NetConfig.Seed, M.Id);
		Spec.SizeScale = M.SizeScale;
		Spec.Extent = M.Extent;
		ATN_BeachElement* Element = ATN_BeachElement::SpawnElement(GetWorld(), FTransform(FRotator(0.0, M.Yaw, 0.0), M.Pos), Spec);
		if (!Element)
		{
			++Skipped;
			continue;
		}
		SpawnedActors.Add(Element);
		++Spawned;
		UE_LOG(LogTortunabo, Log, TEXT("[Coop] Mecánica %s (%s) en %s"), *M.Id.ToString(), *UEnum::GetValueAsString(M.Element), *M.Pos.ToCompactString());
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Coop] %d mecánicas de la playa creadas, %d omitidas."), Spawned, Skipped);
}
```

- [ ] **Paso 4: auditoría R10 (lectura, sin cambios de código salvo los ya hechos).** Comprobar en el código, para cada mecánica de la lista, qué pasa sin `ATN_BeachRaceGenerator` y anotarlo en el mensaje de commit:
  1. Catapulta y trampolín: `TNBeachRideKit::SeaYawInActor` (`TN_BeachRideKit.h:35`) devuelve 0 → lanzan según el giro del marcador (`yaw` del manifest a lo largo del camino). Aceptado.
  2. Gaviotas: `CourseBackFor` (Tarea 18). Resuelto.
  3. Tanque de juguete y demás enemigos: `ATN_BeachEnemy::GroundHeightAt` cae a `TraceGround` (`TN_BeachEnemy.cpp:787-795`) y `CacheObstacles` queda vacío. Aceptado (no se toca `TN_BeachEnemy.*`, reservado a otro agente).
  4. Aturdir/recolocar: `TNBeach::FindOpenSandSpot`/`DepthUnderTerrain` con proveedor (Tarea 17). Resuelto.
  5. Plataformas móvil y tambaleante, paso de quads: sin llamadas a `ATN_BeachRaceGenerator::Find`. Nada que hacer.
  La activación de cada una se verifica en PIE (Tarea 34).
- [ ] **Paso 5: ver pasar.** BUILD; UETEST `Coop.Elements` → `Success`.
- [ ] **Paso 6: commit.** `git add Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h Source/Tortunabo/Private/Tests/TN_CoopMapDecisionsTest.cpp Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Prepared.cpp` · `git commit -m "feat(coop): mecánicas de la carrera desde marcadores con SpawnElement (auditoría R10 en el cuerpo)"`

### Tarea 30: Vegetación del Coop en `ATN_BeachDecorField` con cota fija

**Files:**
- Create: `Source/Tortunabo/Public/World/ProcMap/TN_CoopDecorDecisions.h`, `Source/Tortunabo/Private/Tests/TN_CoopDecorDecisionsTest.cpp`
- Modify: `Source/Tortunabo/Public/World/Beach/TN_BeachDecorField.h:162-170, 207-260`, `Source/Tortunabo/Private/World/Beach/TN_BeachDecorField.cpp:91-93, 225`, `Source/Tortunabo/Public/World/ProcMap/TN_ProcMapGenerator.h`, `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Prepared.cpp`, `Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator.cpp:73-77, 293-297`

**Interfaces:**
- Consumes: `TNBeachLayout::FRoundLayout`, `FItem` (`TN_BeachLayout.h:1744, 2214`); `TNBeach::FootprintRadius`, `TNBeach::CategoryOf` (`TN_BeachTypes.h`); `FTNCoopDecorPoint` (14).
- Produces: `TNCoopMap::FCoopDecorInput { ETNBeachElement Element; FVector Pos; float Yaw; float SizeScale; int32 Seed; }`; `int32 TNCoopMap::DecorItemsFromPoints(const TArray<FCoopDecorInput>&, TNBeachLayout::FRoundLayout& OutLayout, TArray<float>& OutZ)` (devuelve los descartados); `void ATN_BeachDecorField::BeginBuildFixed(const TNBeachLayout::FRoundLayout& Layout, const TArray<float>& ItemZ, int32 Round)`; `ATN_ProcMapGenerator::CoopDecor` y `BuildCoopDecor()`.

- [ ] **Paso 1: test que falla.** `Source/Tortunabo/Private/Tests/TN_CoopDecorDecisionsTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "World/ProcMap/TN_CoopDecorDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopDecorTest, "Tortunabo.Coop.Decor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopDecorTest::RunTest(const FString& Parameters)
{
	TArray<TNCoopMap::FCoopDecorInput> Points;
	Points.Add({ ETNBeachElement::Rock, FVector(400.0, 1500.0, 420.0), 30.f, 1.2f, 7 });
	Points.Add({ ETNBeachElement::Starfish, FVector(900.0, 1400.0, 410.0), 0.f, 1.f, 8 });
	Points.Add({ ETNBeachElement::Catapult, FVector(700.0, 1000.0, 400.0), 0.f, 1.f, 9 });
	TNBeachLayout::FRoundLayout Layout;
	TArray<float> Z;
	const int32 Rejected = TNCoopMap::DecorItemsFromPoints(Points, Layout, Z);
	TestEqual(TEXT("La catapulta no es decorado"), Rejected, 1);
	TestEqual(TEXT("Dos piezas"), Layout.Items.Num(), 2);
	TestEqual(TEXT("Una cota por pieza"), Z.Num(), 2);
	TestEqual(TEXT("Cota del punto"), Z[0], 420.f);
	TestEqual(TEXT("Huella escalada"), Layout.Items[0].Radius, TNBeach::FootprintRadius(ETNBeachElement::Rock) * 1.2, 0.01);
	TestEqual(TEXT("Semilla"), Layout.Items[1].Spec.Seed, 8);
	TestTrue(TEXT("Elemento"), Layout.Items[1].Element == ETNBeachElement::Starfish);
	return true;
}

#endif
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila.
- [ ] **Paso 3: lógica pura.** `Source/Tortunabo/Public/World/ProcMap/TN_CoopDecorDecisions.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachLayout.h"
#include "World/Beach/TN_BeachTypes.h"

/** Vegetación del Coop (puntos del DA) como piezas de ATN_BeachDecorField, con su cota del suelo caminable. */
namespace TNCoopMap
{
	struct FCoopDecorInput
	{
		ETNBeachElement Element = ETNBeachElement::Rock;
		FVector Pos = FVector::ZeroVector;   // espacio del mapa del generador
		float Yaw = 0.f;
		float SizeScale = 1.f;
		int32 Seed = 0;
	};

	/** Rellena OutLayout.Items y OutZ (misma longitud); devuelve cuántos puntos no eran decorado. */
	inline int32 DecorItemsFromPoints(const TArray<FCoopDecorInput>& Points, TNBeachLayout::FRoundLayout& OutLayout, TArray<float>& OutZ)
	{
		OutLayout.Items.Reset();
		OutZ.Reset();
		int32 Rejected = 0;
		for (const FCoopDecorInput& P : Points)
		{
			if (TNBeach::CategoryOf(P.Element) != ETNBeachCategory::Decor)
			{
				++Rejected;
				continue;
			}
			TNBeachLayout::FItem& Item = OutLayout.Items.AddDefaulted_GetRef();
			Item.Element = P.Element;
			Item.Pos = FVector2D(P.Pos.X, P.Pos.Y);
			Item.Yaw = P.Yaw;
			Item.Radius = TNBeach::FootprintRadius(P.Element) * P.SizeScale;
			Item.Core = Item.Radius;
			Item.Spec.Element = P.Element;
			Item.Spec.Seed = P.Seed;
			Item.Spec.SizeScale = P.SizeScale;
			OutZ.Add(static_cast<float>(P.Pos.Z));
		}
		OutLayout.NumDecor = OutLayout.Items.Num();
		return Rejected;
	}
}
```

- [ ] **Paso 4: entrada con cota fija en el campo de decorado.** `TN_BeachDecorField.h`, `public:` tras `BeginBuild`: `/** Como BeginBuild, pero cada pieza a ItemZ[índice] (mapas fijos: la arena de la playa no existe ahí). */ void BeginBuildFixed(const TNBeachLayout::FRoundLayout& Layout, const TArray<float>& ItemZ, int32 Round);`; `private:` `TArray<float> FixedItemZ;`. `TN_BeachDecorField.cpp`: en `BeginBuild` tras `ClearDecor();` → `FixedItemZ.Reset();`; nueva función:

```cpp
void ATN_BeachDecorField::BeginBuildFixed(const TNBeachLayout::FRoundLayout& Layout, const TArray<float>& ItemZ, int32 Round)
{
	BeginBuild(Layout, Round);
	FixedItemZ = ItemZ;
}
```

  y la línea 225 pasa a:

```cpp
	const double SeatZ = FixedItemZ.IsValidIndex(LayoutIndex) ? static_cast<double>(FixedItemZ[LayoutIndex]) : TNBeachLayout::PlacementZ(LayoutItem);
	Item.ItemXf = FTransform(FRotator(0.0, LayoutItem.Yaw, 0.0), FVector(LayoutItem.Pos.X, LayoutItem.Pos.Y, SeatZ));
```

- [ ] **Paso 5: generador.** `TN_ProcMapGenerator.h`: `class ATN_BeachDecorField;`, privado `UPROPERTY(Transient) TObjectPtr<ATN_BeachDecorField> CoopDecor;` y `/** Todas las máquinas salvo el dedicado: vegetación del DA. */ void BuildCoopDecor();`. En `BuildPreparedFromNetConfig` (Tarea 27), al final del bloque `if (World->IsGameWorld())`: `BuildCoopDecor();`. En `Clear()` (`TN_ProcMapGenerator.cpp:293`): `if (CoopDecor) { CoopDecor->Destroy(); CoopDecor = nullptr; }`. En `Tick`, tras `TNAmbientFX::TickOwner(this, DeltaTime);` (línea 77): `if (CoopDecor && CoopDecor->IsBuilding()) { CoopDecor->StepBuild(0.004); }`. Definición de `BuildCoopDecor` en `TN_ProcMapGenerator_Prepared.cpp` (includes `World/Beach/TN_BeachDecorField.h`, `World/ProcMap/TN_CoopDecorDecisions.h`):

```cpp
void ATN_ProcMapGenerator::BuildCoopDecor()
{
	if (GetNetMode() == NM_DedicatedServer || CoopData->Vegetation.Num() == 0)
	{
		return;
	}
	TArray<TNCoopMap::FCoopDecorInput> Points;
	Points.Reserve(CoopData->Vegetation.Num());
	for (const FTNCoopDecorPoint& P : CoopData->Vegetation)
	{
		Points.Add({ P.Element, WorldToMap(P.Pos), P.Yaw, P.SizeScale, P.Seed });
	}
	TNBeachLayout::FRoundLayout Decor;
	TArray<float> Z;
	const int32 Rejected = TNCoopMap::DecorItemsFromPoints(Points, Decor, Z);
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	CoopDecor = GetWorld()->SpawnActor<ATN_BeachDecorField>(ATN_BeachDecorField::StaticClass(), GetActorTransform(), Params);
	if (CoopDecor)
	{
		CoopDecor->BeginBuildFixed(Decor, Z, NetConfig.Generation);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Coop] Vegetación: %d piezas (%d descartadas)."), Decor.Items.Num(), Rejected);
}
```

- [ ] **Paso 6: ver pasar.** BUILD; UETEST `Coop` → `Coop.Decor` y el resto `Success`; UETEST `Beach` sin regresiones (la playa llama a `BeginBuild`, que limpia `FixedItemZ`).
- [ ] **Paso 7: commit.** `git add Source/Tortunabo/Public/World/ProcMap/TN_CoopDecorDecisions.h Source/Tortunabo/Private/Tests/TN_CoopDecorDecisionsTest.cpp Source/Tortunabo/Public/World/Beach/TN_BeachDecorField.h Source/Tortunabo/Private/World/Beach/TN_BeachDecorField.cpp Source/Tortunabo/Public/World/ProcMap/TN_ProcMapGenerator.h Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator.cpp Source/Tortunabo/Private/World/ProcMap/TN_ProcMapGenerator_Prepared.cpp` · `git commit -m "feat(coop): vegetación del mapa preparado en ATN_BeachDecorField a la cota del suelo"`

### Tarea 31: Catálogo Coop, elección del mapa y viaje desde el lobby

**Files:**
- Create: `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapCatalog.h`, `Source/Tortunabo/Private/World/ProcMap/TN_CoopMapCatalog.cpp`
- Modify: `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h`, `Source/Tortunabo/Private/Tests/TN_CoopMapDecisionsTest.cpp`, `Source/Tortunabo/Public/Multiplayer/MP_GameInstance.h:381-386`, `Source/Tortunabo/Public/Lobby/TN_LobbyMission.h`, `Source/Tortunabo/Private/Lobby/TN_LobbyMission.cpp`, `Source/Tortunabo/Public/Lobby/TN_HQGameMode.h:110-119`, `Source/Tortunabo/Private/Lobby/TN_HQGameMode.cpp:33, 413-428`, `Source/Tortunabo/Public/Game/TN_ProcMapGameMode.h`, `Source/Tortunabo/Private/Game/TN_ProcMapGameMode.cpp:68-72`, `Source/Tortunabo/Private/UI/Briefing/TN_BriefingWidget.cpp:627-631`

**Interfaces:**
- Consumes: `PendingStartStyle` (`MP_GameInstance.h:392`); `TNLobbyMission::SyncLobby`.
- Produces: `USTRUCT FTNCoopMapEntry { FName Id; TSoftObjectPtr<UWorld> Level; FText DisplayName; TSoftObjectPtr<UTexture2D> Preview; }`; `UTN_CoopMapCatalog : UPrimaryDataAsset { TArray<FTNCoopMapEntry> Maps; const FTNCoopMapEntry* FindById(FName) const; TArray<FName> GetIds() const; }`; `TNCoopMap::NextMapId(const TArray<FName>&, FName) -> FName`; `TNCoopMap::ResolveMapId(const TArray<FName>&, FName Wanted) -> FName`; `TNCoopMap::BuildCoopTravelURL(const FString& LevelPackage, FName Id, bool bEggs) -> FString`; `UMP_GameInstance::SelectedCoopMapId`; `TNLobbyMission::SetCoopMap(const UObject*, FName) -> bool`, `TNLobbyMission::GetHostCoopMap(const UObject*) -> FName`; cvar de consola `TN.Coop.Map <Id>`; `ATN_HQGameMode::CoopCatalog` (`/Game/Coop/DA_CoopMapCatalog`).

- [ ] **Paso 1: test que falla.** En `TN_CoopMapDecisionsTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopTravelURLTest, "Tortunabo.Coop.TravelURL",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopTravelURLTest::RunTest(const FString& Parameters)
{
	const TArray<FName> Ids = { TEXT("CP01"), TEXT("C01") };
	TestEqual(TEXT("Siguiente"), TNCoopMap::NextMapId(Ids, TEXT("CP01")), FName(TEXT("C01")));
	TestEqual(TEXT("Da la vuelta"), TNCoopMap::NextMapId(Ids, TEXT("C01")), FName(TEXT("CP01")));
	TestEqual(TEXT("Sin elegir: el primero"), TNCoopMap::NextMapId(Ids, NAME_None), FName(TEXT("CP01")));
	TestEqual(TEXT("Catálogo vacío"), TNCoopMap::NextMapId(TArray<FName>(), TEXT("CP01")), FName(NAME_None));
	TestEqual(TEXT("Resuelve uno que existe"), TNCoopMap::ResolveMapId(Ids, TEXT("C01")), FName(TEXT("C01")));
	TestEqual(TEXT("Uno que ya no existe: el primero"), TNCoopMap::ResolveMapId(Ids, TEXT("X99")), FName(TEXT("CP01")));
	TestEqual(TEXT("URL con huevos"), TNCoopMap::BuildCoopTravelURL(TEXT("/Game/Maps/Coop/LVL_Coop_CP01"), TEXT("CP01"), true),
		FString(TEXT("/Game/Maps/Coop/LVL_Coop_CP01?ProcStart=Eggs?CoopMap=CP01")));
	TestEqual(TEXT("URL con puerta"), TNCoopMap::BuildCoopTravelURL(TEXT("/Game/Maps/Coop/LVL_Coop_C01"), TEXT("C01"), false),
		FString(TEXT("/Game/Maps/Coop/LVL_Coop_C01?ProcStart=Gate?CoopMap=C01")));
	return true;
}
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila.
- [ ] **Paso 3: lógica pura.** `TN_CoopMapDecisions.h`:

```cpp
	inline FName NextMapId(const TArray<FName>& Ids, FName Current)
	{
		if (Ids.Num() == 0) { return NAME_None; }
		const int32 At = Ids.IndexOfByKey(Current);
		return Ids[At == INDEX_NONE ? 0 : (At + 1) % Ids.Num()];
	}

	inline FName ResolveMapId(const TArray<FName>& Ids, FName Wanted)
	{
		return Ids.Contains(Wanted) ? Wanted : (Ids.Num() > 0 ? Ids[0] : FName(NAME_None));
	}

	inline FString BuildCoopTravelURL(const FString& LevelPackage, FName Id, bool bEggs)
	{
		return FString::Printf(TEXT("%s?ProcStart=%s?CoopMap=%s"), *LevelPackage, bEggs ? TEXT("Eggs") : TEXT("Gate"), *Id.ToString());
	}
```

- [ ] **Paso 4: catálogo.** `Source/Tortunabo/Public/World/ProcMap/TN_CoopMapCatalog.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TN_CoopMapCatalog.generated.h"

class UTexture2D;

USTRUCT(BlueprintType)
struct FTNCoopMapEntry
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop") FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop") TSoftObjectPtr<UWorld> Level;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop") FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop") TSoftObjectPtr<UTexture2D> Preview;
};

/** Mapas Coop jugables (/Game/Coop/DA_CoopMapCatalog): lo rellena Scripts/import_terrain_coop.py; el primero es el de por defecto. */
UCLASS(BlueprintType)
class TORTUNABO_API UTN_CoopMapCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coop")
	TArray<FTNCoopMapEntry> Maps;

	const FTNCoopMapEntry* FindById(FName Id) const;
	TArray<FName> GetIds() const;

	/** El catálogo del proyecto (carga síncrona: 1 KB, se llama en el lobby). */
	static const UTN_CoopMapCatalog* Get();
};
```

  `Source/Tortunabo/Private/World/ProcMap/TN_CoopMapCatalog.cpp`:

```cpp
#include "World/ProcMap/TN_CoopMapCatalog.h"

const FTNCoopMapEntry* UTN_CoopMapCatalog::FindById(FName Id) const
{
	return Maps.FindByPredicate([Id](const FTNCoopMapEntry& E) { return E.Id == Id; });
}

TArray<FName> UTN_CoopMapCatalog::GetIds() const
{
	TArray<FName> Ids;
	for (const FTNCoopMapEntry& E : Maps) { Ids.Add(E.Id); }
	return Ids;
}

const UTN_CoopMapCatalog* UTN_CoopMapCatalog::Get()
{
	static const FSoftObjectPath Path(TEXT("/Game/Coop/DA_CoopMapCatalog.DA_CoopMapCatalog"));
	return Cast<UTN_CoopMapCatalog>(Path.TryLoad());
}
```

- [ ] **Paso 5: elección y viaje.** `MP_GameInstance.h`, tras `SelectedProcDifficulty` (línea 385): `/** Coop: mapa preparado elegido (id del catálogo; NAME_None = el primero). */ UPROPERTY() FName SelectedCoopMapId;`. `TN_LobbyMission.h`: `TORTUNABO_API bool SetCoopMap(const UObject* WorldContext, FName MapId);` y `TORTUNABO_API FName GetHostCoopMap(const UObject* WorldContext);`. `TN_LobbyMission.cpp`:

```cpp
bool TNLobbyMission::SetCoopMap(const UObject* WorldContext, FName MapId)
{
	UMP_GameInstance* GI = TNLobbyMissionDetail::HostGameInstance(WorldContext);
	const UTN_CoopMapCatalog* Catalog = UTN_CoopMapCatalog::Get();
	if (!GI || !Catalog || !Catalog->FindById(MapId))
	{
		return false;
	}
	GI->SelectedCoopMapId = MapId;
	UE_LOG(LogTortunabo, Log, TEXT("[Misión] Mapa Coop de la próxima partida: %s"), *MapId.ToString());
	SyncLobby(WorldContext);
	return true;
}

FName TNLobbyMission::GetHostCoopMap(const UObject* WorldContext)
{
	const UWorld* World = TNLobbyMissionDetail::WorldOf(WorldContext);
	const UMP_GameInstance* GI = World ? Cast<UMP_GameInstance>(World->GetGameInstance()) : nullptr;
	const UTN_CoopMapCatalog* Catalog = UTN_CoopMapCatalog::Get();
	return Catalog ? TNCoopMap::ResolveMapId(Catalog->GetIds(), GI ? GI->SelectedCoopMapId : NAME_None) : FName(NAME_None);
}

static FAutoConsoleCommandWithWorldAndArgs GTNCoopMapCommand(TEXT("TN.Coop.Map"),
	TEXT("Anfitrión: mapa Coop de la próxima partida (id del catálogo, p. ej. TN.Coop.Map CP01)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() == 1 && !TNLobbyMission::SetCoopMap(World, FName(*Args[0])))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Misión] TN.Coop.Map %s: no eres el anfitrión o el mapa no está en el catálogo."), *Args[0]);
		}
	}));
```

  (includes `World/ProcMap/TN_CoopMapCatalog.h`, `World/ProcMap/TN_CoopMapDecisions.h`.) En `TN_HQGameMode.cpp` (mismos includes y `Lobby/TN_LobbyMission.h`), el bloque `else if (GI->SelectedProcMode != ETNProcGameMode::Classic) { … }` (líneas 419-428) pasa a:

```cpp
		else if (GI->SelectedProcMode != ETNProcGameMode::Classic)
		{
			if (bBeachRace)
			{
				UE_LOG(LogTortunabo, Error, TEXT("[HQGameMode] No existe %s (se crea con Scripts/build_beach_race.py): la carrera se juega en el mapa procedural."),
					*BeachRaceMapPath);
			}
			const UTN_CoopMapCatalog* Catalog = UTN_CoopMapCatalog::Get();
			const FName CoopId = TNLobbyMission::GetHostCoopMap(this);
			const FTNCoopMapEntry* Entry = Catalog ? Catalog->FindById(CoopId) : nullptr;
			const bool bEggs = GI->PendingStartStyle == ETNMatchStartStyle::Eggs;
			if (GI->SelectedProcMode == ETNProcGameMode::Coop && Entry && !Entry->Level.IsNull())
			{
				// Coop sobre mapas fijos: el nivel preparado del catálogo (plan maestro §3.1).
				TravelURL = TNCoopMap::BuildCoopTravelURL(Entry->Level.ToSoftObjectPath().GetLongPackageName(), CoopId, bEggs);
			}
			else
			{
				// También en la URL: la lee ATN_ProcMapGameMode y sustituye a la del viaje anterior.
				TravelURL = ProcMapPath + (bEggs ? TEXT("?ProcStart=Eggs") : TEXT("?ProcStart=Gate"));
			}
		}
```

  `TN_ProcMapGameMode.h`, privado: `/** ?CoopMap= (pruebas y viaje del lobby) contra el DA del nivel cargado: error en el log si no casan. */ void ValidateCoopMapOption() const;`. En `TN_ProcMapGameMode.cpp` (include `World/ProcMap/TN_CoopMapData.h`), llamada en `BeginPlay` justo después de `EnsureGenerator();`, y:

```cpp
void ATN_ProcMapGameMode::ValidateCoopMapOption() const
{
	const FString CoopOption = UGameplayStatics::ParseOption(OptionsString, TEXT("CoopMap"));
	const UTN_CoopMapData* Data = Generator ? Generator->GetCoopData() : nullptr;
	if (!CoopOption.IsEmpty() && Data && Data->MapId != FName(*CoopOption))
	{
		UE_LOG(LogTortunabo, Error, TEXT("[Coop] ?CoopMap=%s no coincide con el nivel cargado (%s)."), *CoopOption, *Data->MapId.ToString());
	}
}
```

  En `TN_BriefingWidget.cpp:629`, cuando el modo es Coop, la orden del día nombra el mapa:

```cpp
		const FName CoopId = Mode == ETNProcGameMode::Coop ? TNLobbyMission::GetHostCoopMap(this) : NAME_None;
		const UTN_CoopMapCatalog* Catalog = UTN_CoopMapCatalog::Get();
		const FTNCoopMapEntry* Entry = Catalog && !CoopId.IsNone() ? Catalog->FindById(CoopId) : nullptr;
		MissionOrders->SetText(Entry
			? FText::Format(NSLOCTEXT("Tortunabo", "BriefingMissionOrdersMap", "Orden del día: {0} · {1} · {2}"),
				TNLobbyMission::ModeName(Mode).ToUpper(), TNLobbyMission::DifficultyName(Difficulty).ToUpper(), Entry->DisplayName)
			: FText::Format(NSLOCTEXT("Tortunabo", "BriefingMissionOrders", "Orden del día: {0} · {1}"),
				TNLobbyMission::ModeName(Mode).ToUpper(), TNLobbyMission::DifficultyName(Difficulty).ToUpper()));
```

- [ ] **Paso 6: ver pasar.** BUILD; UETEST `Coop.TravelURL` → `Success`; UETEST `Lobby` y `Loading` sin regresiones.
- [ ] **Paso 7: commit.** `git add Source/Tortunabo/Public/World/ProcMap/TN_CoopMapCatalog.h Source/Tortunabo/Private/World/ProcMap/TN_CoopMapCatalog.cpp Source/Tortunabo/Public/World/ProcMap/TN_CoopMapDecisions.h Source/Tortunabo/Private/Tests/TN_CoopMapDecisionsTest.cpp Source/Tortunabo/Public/Multiplayer/MP_GameInstance.h Source/Tortunabo/Public/Lobby/TN_LobbyMission.h Source/Tortunabo/Private/Lobby/TN_LobbyMission.cpp Source/Tortunabo/Private/Lobby/TN_HQGameMode.cpp Source/Tortunabo/Public/Game/TN_ProcMapGameMode.h Source/Tortunabo/Private/Game/TN_ProcMapGameMode.cpp Source/Tortunabo/Private/UI/Briefing/TN_BriefingWidget.cpp` · `git commit -m "feat(coop): catálogo de mapas fijos, elección del mapa y viaje del lobby al nivel preparado"`

### Tarea 32: CP01, mapa Coop propio de 600 × 600 m (generador camino)

**Files:**
- Modify: `Scripts/terrain_path/layout.py:11`, `Scripts/gen_terrain_coop.py` (rama CP01)
- Create: `Scripts/terrain_coop/__init__.py` (vacío), `Scripts/terrain_coop/cp01.py`, `Scripts/tests/test_cp01.py`; salida `Scripts/terrain_volumes/Variants/CP01_coop/` (trozos TNTM2, `Outer/`, `coop/bake.bin`, `manifest.json`, `preview.png`)

**Interfaces:**
- Consumes: `gen_terrain_path.solve` (11); `coop.*` (9–11); `decimate.fit_budget` (12); `write_map`, `write_outer`, `kill_boxes_uu`, `walkable`.
- Produces: `terrain_path.layout.GRID` leído de `TN_PATH_GRID` (por defecto 4); `cp01.CP01_NAME = "CP01_coop"`, `CP01_SEED = 20260929`, `CP01_STYLE: PathStyle`, `ELEMENTS`, `VEGETATION`, `place_elements(points_m, arc, seen) -> list[dict]`, `place_vegetation(seen, path_dm, zone_weights, rng) -> list[dict]`, `build_cp01(variants: Path) -> dict`.

- [ ] **Paso 1: tests que fallan.** `Scripts/tests/test_cp01.py`:

```python
"""CP01 (plan F0-F2, Tarea 32): 600 x 600 m, recorrible, <= 1,1 M triangulos, mecanicas y vegetacion validas.
Lee la salida versionada (Scripts/terrain_volumes/Variants/CP01_coop); regenerarla es gen_terrain_coop.py --map CP01."""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

SCRIPTS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SCRIPTS))

from terrain_vol import coop  # noqa: E402

OUT = SCRIPTS / "terrain_volumes" / "Variants" / "CP01_coop"
MECHANICS = {"Catapult", "Trampoline", "MovingPlatform", "WobblyPlatform", "QuadLane", "ToyTank", "GullZone"}


@pytest.fixture(scope="module")
def manifest():
    path = OUT / "manifest.json"
    if not path.exists():
        pytest.fail("falta CP01: uv run ... python Scripts/gen_terrain_coop.py --map CP01")
    return json.loads(path.read_text(encoding="utf-8"))


def test_path_grid_comes_from_the_environment():
    code = "from terrain_path.layout import GRID, MAP_MAX_M; print(GRID, MAP_MAX_M)"
    env = {**os.environ, "TN_PATH_GRID": "6"}
    out = subprocess.run([sys.executable, "-c", code], cwd=SCRIPTS, env=env, capture_output=True, text=True, check=True)
    assert out.stdout.split() == ["6", "550.0"]


def test_cp01_is_600_metres_and_walkable(manifest):
    assert manifest["grid"] == 6 and manifest["recorrible"] is True
    x0, y0, x1, y1 = manifest["coop"]["bounds_uu"]
    assert (x1 - x0, y1 - y0) == (60000.0, 60000.0)


def test_cp01_fits_the_mesh_budget(manifest):
    tris = sum(cell["triangles"] for cell in manifest["cells"])
    assert tris <= 1_100_000, f"{tris} triangulos"


def test_every_mechanic_is_placed_on_allowed_ground(manifest):
    block = manifest["coop"]
    bake = coop.read_bake(OUT / block["bake"]["file"])
    kinds = {e["element"] for e in block["beach_elements"]}
    assert kinds == MECHANICS
    for e in block["beach_elements"]:
        i = round((e["pos"][0] - bake["origin_uu"][0]) / bake["step_uu"])
        j = round((e["pos"][1] - bake["origin_uu"][1]) / bake["step_uu"])
        k = round((e["pos"][2] - bake["z0_uu"]) / bake["zstep_uu"])
        assert bake["safe_low"][i, j] <= k <= bake["safe_high"][i, j] + 1, e["id"]


def test_mechanics_are_spread_out(manifest):
    pos = np.array([e["pos"][:2] for e in manifest["coop"]["beach_elements"]])
    gaps = np.linalg.norm(pos[:, None] - pos[None], axis=2) + np.eye(len(pos)) * 1e9
    assert gaps.min() >= 2500.0


def test_vegetation_is_off_the_path_and_spaced(manifest):
    block = manifest["coop"]
    points = block["vegetation"]["points"]
    assert 800 <= len(points) <= 1500
    bake = coop.read_bake(OUT / block["bake"]["file"])
    xy = np.array([p["pos"][:2] for p in points])
    i = np.round((xy[:, 0] - bake["origin_uu"][0]) / bake["step_uu"]).astype(int)
    j = np.round((xy[:, 1] - bake["origin_uu"][1]) / bake["step_uu"]).astype(int)
    assert bake["path_dist_dm"][i, j].min() >= 40
    d = np.linalg.norm(xy[:, None] - xy[None], axis=2) + np.eye(len(xy)) * 1e9
    assert d.min() >= 299.0
```

- [ ] **Paso 2: ver el fallo.** PYTEST con `Scripts/tests/test_cp01.py` → `test_path_grid_comes_from_the_environment` FAIL (`4 350.0`) y el resto FAIL («falta CP01»).
- [ ] **Paso 3: rejilla configurable.** `Scripts/terrain_path/layout.py`: `import os` y la línea 11 pasa a `GRID = int(os.environ.get("TN_PATH_GRID", "4"))        # 4 = C01 (400 m); CP01 usa 6 (600 m)`. Sin la variable, C01 y el catálogo no cambian (`test_terrain_path.py` verde).
- [ ] **Paso 4: diseño.** `Scripts/terrain_coop/cp01.py`:

```python
"""CP01: mapa Coop propio (plan maestro §3.1 y respuesta 1 del director), 600 x 600 m con el generador camino
(terrain_path, metodo oficial). Puentes naturales: arcos de roca y cruces con puente fino del propio estilo.
Exige TN_PATH_GRID=6 antes de importar terrain_path (lo pone gen_terrain_coop.py --map CP01)."""

from __future__ import annotations

import json
import os
from pathlib import Path

import numpy as np

if os.environ.get("TN_PATH_GRID") != "6":
    raise RuntimeError("CP01 exige TN_PATH_GRID=6 antes de importar terrain_path")

import gen_terrain_path as gp  # noqa: E402
from gen_terrain_volume import global_standable, ground_level, world_index, zone_map  # noqa: E402
from terrain_path.canyon import kill_boxes_uu  # noqa: E402
from terrain_path.layout import GRID  # noqa: E402
from terrain_path.model import walkable  # noqa: E402
from terrain_path.outer import write_outer  # noqa: E402
from terrain_path.style import PathStyle  # noqa: E402
from terrain_vol import coop, decimate  # noqa: E402
from terrain_vol.export import global_top, write_map  # noqa: E402
from terrain_vol.layout import STEP_XY_M, STEP_Z_M, UU_PER_M, WATER_M, Z_MIN_M  # noqa: E402
from terrain_vol.mesh import z_levels  # noqa: E402

CP01_NAME = "CP01_coop"
CP01_SEED = 20260929
WADE_M = 0.8
CROWN_RESERVE_TRIS = 150_000                  # corona sin colision (write_outer): se descuenta del presupuesto
CP01_STYLE = PathStyle(
    name=CP01_NAME,
    description=("Coop CP01 (600 x 600 m): acantilados con arcos de roca, barranco con puente fino, rio vadeable "
                 "con islas, dunas y meta en la playa."),
    main_length_m=(1100.0, 1350.0), loops=8, nested_loops=2, crossings=(2, 4), bridge_share=0.6,
    hill_tunnels=2, arches=4, extra_hill_tunnels=1, canyon="deadly", canyon_count=(1, 2),
    jump_steps=2, lagoon_chance=0.3, streams=(1, 2), width_m=(4.5, 7.0, 11.0))

# (id, fraccion de S del camino principal, desplazamiento lateral m, elemento, giro sobre el camino, tamano, extent uu)
ELEMENTS = (
    ("cat_01", 0.12, 0.0, "Catapult", 0.0, 1.0, 0.0),
    ("tramp_01", 0.21, 3.0, "Trampoline", 0.0, 1.0, 0.0),
    ("wobbly_01", 0.30, 0.0, "WobblyPlatform", 0.0, 1.0, 0.0),
    ("moving_01", 0.40, 0.0, "MovingPlatform", 90.0, 1.0, 0.0),
    ("quad_01", 0.52, 0.0, "QuadLane", 90.0, 1.0, 2400.0),
    ("tank_01", 0.63, 5.0, "ToyTank", 0.0, 1.0, 3000.0),
    ("gull_01", 0.74, 0.0, "GullZone", 0.0, 1.0, 0.0),
    ("cat_02", 0.84, -3.0, "Catapult", 0.0, 1.0, 0.0),
    ("tramp_02", 0.91, 4.0, "Trampoline", 0.0, 1.1, 0.0),
)
VEGETATION = {
    "cliffs": (("Rock", 0.5), ("RockCluster", 0.3), ("Driftwood", 0.2)),
    "canyon": (("Rock", 0.4), ("RockCluster", 0.4), ("OldPlanks", 0.2)),
    "marsh": (("MossyLog", 0.4), ("Driftwood", 0.3), ("FishingNet", 0.3)),
    "algae": (("Coconut", 0.3), ("DecorShell", 0.3), ("Starfish", 0.2), ("Driftwood", 0.2)),
    "beach": (("PlantedUmbrella", 0.2), ("BeachChair", 0.2), ("DecorShell", 0.3), ("Starfish", 0.3)),
}
VEG_TARGET = 1500
VEG_BAND_M = (4.0, 25.0)
VEG_SPACING_M = 3.0


def walkable_z(seen: np.ndarray, xy, z_hint_m: float) -> float | None:
    """Nivel alcanzable de la columna mas cercano a z_hint_m; None si la columna no se alcanza."""
    i, j = coop.indices_of(np.array([[xy[0], xy[1], 0.0]]), seen.shape)
    ks = np.nonzero(seen[i[0], j[0]])[0]
    if not len(ks):
        return None
    zs = Z_MIN_M + ks * STEP_Z_M
    return float(zs[np.argmin(np.abs(zs - z_hint_m))])


def place_elements(points_m: np.ndarray, arc: np.ndarray, seen: np.ndarray) -> list[dict]:
    out = []
    for eid, frac, lateral, element, yaw_offset, size, extent in ELEMENTS:
        k = min(int(np.searchsorted(arc, frac * arc[-1])), len(points_m) - 1)
        d = points_m[min(k + 1, len(points_m) - 1), :2] - points_m[max(k - 1, 0), :2]
        d = d / max(np.linalg.norm(d), 1e-6)
        xy = points_m[k, :2] + np.array([-d[1], d[0]]) * lateral
        z = walkable_z(seen, xy, float(points_m[k, 2]))
        if z is None:                                   # el lado no se alcanza: en el propio camino
            xy, z = points_m[k, :2], float(points_m[k, 2])
        out.append({"id": eid, "element": element, "pos": coop.uu((xy[0], xy[1], z)),
                    "yaw": round(coop.yaw_of(d) + yaw_offset, 2), "size": size, "extent": extent})
    return out


def place_vegetation(seen: np.ndarray, path_dm: np.ndarray, zone_weights: dict, rng: np.random.Generator) -> list[dict]:
    reached = seen.any(axis=2)
    band = reached & (path_dm >= VEG_BAND_M[0] * 10) & (path_dm <= VEG_BAND_M[1] * 10)
    candidates = np.argwhere(band)
    rng.shuffle(candidates)
    names = list(zone_weights)
    cell = VEG_SPACING_M / STEP_XY_M
    taken: dict[tuple[int, int], tuple[int, int]] = {}
    out = []
    for i, j in candidates:
        key = (int(i // cell), int(j // cell))
        near = [taken[(key[0] + a, key[1] + b)] for a in (-1, 0, 1) for b in (-1, 0, 1) if (key[0] + a, key[1] + b) in taken]
        if key in taken or any((i - pi) ** 2 + (j - pj) ** 2 < cell * cell for pi, pj in near):
            continue
        taken[key] = (int(i), int(j))
        zone = names[int(np.argmax([zone_weights[n][i, j] for n in names]))]
        elements, weights = zip(*VEGETATION[zone])
        element = str(rng.choice(elements, p=np.asarray(weights) / sum(weights)))
        k = int(np.nonzero(seen[i, j])[0].max())
        pos = (coop.ORIGIN_M + i * STEP_XY_M, coop.ORIGIN_M + j * STEP_XY_M, Z_MIN_M + k * STEP_Z_M)
        out.append({"element": element, "pos": coop.uu(pos), "yaw": round(float(rng.uniform(0.0, 360.0)), 1),
                    "size": round(float(rng.uniform(0.8, 1.2)), 2), "seed": int(rng.integers(1, 2 ** 31 - 1))})
        if len(out) >= VEG_TARGET:
            break
    return out


def build_cp01(variants: Path) -> dict:
    model, chunks, used, result = gp.solve(CP01_NAME, CP01_SEED, CP01_STYLE)
    if not result["ok"]:
        raise RuntimeError(f"CP01 no recorrible con {CP01_SEED}..: {result}")
    chunks, report = decimate.fit_budget(chunks, decimate.BUDGET_TRIS - CROWN_RESERVE_TRIS)
    standable = walkable(global_standable(chunks, grid=GRID), model.grid.height[1:-1, 1:-1], z_levels())
    standable = model.remove_deadly(standable, z_levels())
    top = global_top(chunks, grid=GRID)
    zones = zone_map(model, chunks, grid=GRID)
    kills = [b for c in model.canyons for b in kill_boxes_uu(c)]
    out = variants / CP01_NAME
    s_ij, e_ij = world_index(model.start), world_index(model.end)
    write_map(out, CP01_NAME, used, chunks, (*model.start, float(top[s_ij])), (*model.end, float(top[e_ij])), zones,
              model.route.points, style=CP01_STYLE,
              extra_manifest={"description": CP01_STYLE.description, "recorrible": True, "kill_boxes_uu": kills,
                              "mesh_budget": report}, grid=GRID)
    manifest_path = out / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["cells"] += write_outer(model, out, CP01_NAME)
    manifest_path.write_text(json.dumps(manifest, indent=1), encoding="utf-8")
    start = (*s_ij, ground_level(standable, *s_ij))
    end = (*e_ij, ground_level(standable, *e_ij))
    links = [(world_index(link[0]), world_index(link[1])) for link in model.jump_links()]
    seen, parent, link_parent = coop.solve_walk(standable, start, WATER_M - WADE_M, links)
    points = coop.main_path(parent, link_parent, start, end)
    extras = coop.CoopExtras(
        map_id="CP01", kill_boxes_uu=kills, finish_in_water=True,
        jellyfish_uu=[[round(float(c) * UU_PER_M, 1) for c in st.jelly] for st in model.jump_steps],
        beach_elements=place_elements(points, coop.arc_of(points), seen),
        vegetation=place_vegetation(seen, coop.path_distance_dm(points, top.shape), zones, np.random.default_rng(CP01_SEED)))
    block = coop.build_coop_block(out, standable, start, end, top, zones, extras, WATER_M - WADE_M, links)
    coop.write_manifest_v2(out, block)
    return block
```

  `Scripts/gen_terrain_coop.py`, rama CP01 de `main()`: `import os` arriba y

```python
    if args.map == "CP01":
        os.environ["TN_PATH_GRID"] = "6"                 # antes de importar terrain_path (lo hace cp01)
        from terrain_coop.cp01 import build_cp01
        block = build_cp01(VARIANTS)
```

- [ ] **Paso 5: generar.** `uv run --with numpy --with scipy --with pillow --with scikit-image --with pyfqmr python Scripts/gen_terrain_coop.py --map CP01` → imprime `CP01: camino N muestras, M nidos, Z zonas, 9 mecanicas, V plantas`. Si `solve` agota las 10 semillas («ninguna de las 10 semillas…» o `result["ok"]` falso), cambiar en `CP01_STYLE` `main_length_m=(950.0, 1200.0)` y `loops=7` y repetir; anotar el cambio en el commit.
- [ ] **Paso 6: ver pasar.** PYTEST con `Scripts/tests/test_cp01.py` (con `--with pyfqmr`) → 6 passed; PYTEST completo verde (`test_terrain_path.py` sigue con `GRID = 4`).
- [ ] **Paso 7: lámina para Rodrigo.** `start Scripts\terrain_volumes\Variants\CP01_coop\preview.png` y `preview_debug.png` (vista cenital con el camino): revisión visual del director antes de importar.
- [ ] **Paso 8: commit.** `git add Scripts/terrain_path/layout.py Scripts/terrain_coop Scripts/gen_terrain_coop.py Scripts/tests/test_cp01.py Scripts/terrain_volumes/Variants/CP01_coop` · `git commit -m "feat(coop): CP01, mapa Coop propio de 600 m con el generador camino, mecánicas y vegetación"`

### Tarea 33: Importar CP01 y C01, registrar el catálogo y cocinarlos

**Files:**
- Modify: `Scripts/import_terrain_coop.py` (registro en el catálogo), `Scripts/tests/test_cook_list.py` (sin cambios de código: ahora cubre dos niveles), `Config/DefaultGame.ini` (lo escribe el importador)
- Create (editor): `Content/Maps/Coop/LVL_Coop_CP01*.umap`, `Content/Coop/CP01/DA_Coop_CP01.uasset`, `Content/Coop/DA_CoopMapCatalog.uasset`, `Content/Terrain/Volumes/CP01/**`

**Interfaces:**
- Consumes: `UTN_CoopMapCatalog` (31); importador (24); `Variants/CP01_coop` (32), `Variants/C01_camino` (11).
- Produces: `import_terrain_coop.ensure_catalog_entry(map_id: str, level_path: str, display_name: str) -> int` (entradas del catálogo); `CATALOG_ORDER = ("CP01", "C01")`.

- [ ] **Paso 1: registro en el catálogo.** En `Scripts/import_terrain_coop.py`, constantes `CATALOG_PATH = "/Game/Coop/DA_CoopMapCatalog"`, `CATALOG_ORDER = ("CP01", "C01")`, `DISPLAY_NAMES = {"CP01": "Costa de los Arcos", "C01": "Camino del Río"}` y:

```python
def ensure_catalog_entry(map_id: str, level_path: str, display_name: str) -> int:
    catalog = load_or_none(CATALOG_PATH)
    if catalog is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.TN_CoopMapCatalog)
        catalog = asset_tools.create_asset("DA_CoopMapCatalog", "/Game/Coop", unreal.TN_CoopMapCatalog, factory)
    entries = [e for e in catalog.get_editor_property("maps") if str(e.get_editor_property("id")) != map_id]
    entry = unreal.TNCoopMapEntry()
    entry.set_editor_property("id", unreal.Name(map_id))
    entry.set_editor_property("level", unreal.load_asset(level_path))
    entry.set_editor_property("display_name", unreal.Text(display_name))
    entries.append(entry)
    order = {name: n for n, name in enumerate(CATALOG_ORDER)}
    entries.sort(key=lambda e: order.get(str(e.get_editor_property("id")), len(order)))
    catalog.set_editor_property("maps", entries)
    asset_lib.save_loaded_asset(catalog)
    return len(entries)
```

  y en `main()`, tras `add_to_cook(level_path)`: `count = ensure_catalog_entry(map_id, level_path, DISPLAY_NAMES.get(map_id, map_id))`, añadiendo `catálogo {count}` al log final.
- [ ] **Paso 2: importar.** Con el editor cerrado: `TN_VOLUME_DIR=…\Variants\CP01_coop` → UEPY `import_terrain_coop.py`; `TN_REGENERATE=1 TN_VOLUME_DIR=…\Variants\C01_camino` → UEPY `import_terrain_coop.py`. Criterio: log `[CoopImport] /Game/Maps/Coop/LVL_Coop_CP01: <trozos> trozos, <n> marcadores nuevos, KnownIds <n>, catálogo 1` y luego `… LVL_Coop_C01: … 0 marcadores nuevos …, catálogo 2`.
- [ ] **Paso 3: comprobar.** PYTEST `Scripts/tests/test_cook_list.py` → 6 passed (dos niveles Coop en `MapsToCook`). R9 sobre CP01: `TN_VOLUME_DIR=…\CP01_coop TN_COOP_LEVEL=/Game/Maps/Coop/LVL_Coop_CP01` UEPY `verify_terrain_collision.py` → `[R9]` dentro del límite. SMOKE `/Game/Maps/Coop/LVL_Coop_CP01` `[Coop] Mapa preparado CP01 listo (generación 1)` → 0, con `[Coop] 9 mecánicas de la playa creadas, 0 omitidas.` y `[Coop] Vegetación: <V> piezas (0 descartadas).` en el log.
- [ ] **Paso 4: commit.** Comprobar autor de los `.umap` tocados (`git log -1 --format=%an -- Content/Maps/Coop`). `git add Scripts/import_terrain_coop.py Config/DefaultGame.ini Content/Maps/Coop Content/Coop Content/Terrain/Volumes/CP01 Content/Terrain/Volumes/C01` · `git commit -m "feat(coop): CP01 y C01 importados, en el catálogo y en el cocinado"`

### Tarea 34: Verificación PIE 4P con PktLag 150 (gate F2, manual)

**Files:** ninguno. **Interfaces:** consume todo F1–F2.

- [ ] **Paso 1: preparación.** Editor DebugGame (`UnrealEditor-Win64-DebugGame.exe`), `Editor Preferences → Level Editor → Play`: 4 jugadores, *Play As Listen Server*, *Use Single Process*. Abrir `LVL_Lobby`; en la consola del anfitrión `TN.Coop.Map CP01`, modo Coop en la pizarra del general; en cada cliente `Net PktLag=150` y `Net PktLoss=2`.
- [ ] **Paso 2: checklist CP01** (marcar SÍ/NO, anotar commit):
  1. El viaje llega a `LVL_Coop_CP01` en las 4 ventanas; la pizarra decía «COOP · NORMAL · Costa de los Arcos».
  2. Ningún `Pawn local liberado tras` > 1,00 s en el log; nadie cae al vacío al aparecer.
  3. `grep -c MapVariantLoader Saved/Logs/Tortunabo*.log` = 0 (ningún cliente cocina colisión).
  4. Rescates: con un cliente, 20 lanzamientos forzados fuera de zona (catapulta `cat_01` apuntando fuera, o `teleport` a un pozo no permitido con `TN.Ghost` en el anfitrión) → 20 líneas `[Coop] Rescate #` con `≤ 1,75 s`; en cada una, la tortuga aparece sobre el camino (inspección visual: a menos de 1 m de la franja del camino) y aturdida.
  5. En brazos de otra tortuga sobre un pozo durante 5 s: **no** hay rescate hasta que la suelta (Review Focus 2).
  6. Gaviota `gull_01` suelta a una tortuga: vuelve hacia la salida por el camino (no hacia −X del mundo).
  7. Cada mecánica activada al menos una vez: catapulta, trampolín, plataforma móvil, plataforma tambaleante, paso de quads, tanque, gaviotas; ninguna línea de error con `Find` nulo.
  8. Nidos: morir en el agua (`kill_boxes_uu`) reaparece en el último nido alcanzado.
  9. Meta: al entrar por la playa (dirección del manifest, no +Y) salta el confeti y termina la ronda (R2/R3).
  10. Vegetación visible y quieta en las 4 ventanas; la fauna aparece (R5).
- [ ] **Paso 3: checklist C01.** `TN.Coop.Map C01` y repetir 1–5, 8 y 9, más: el río es nadable (`water.swim`) y las medusas de los escalones rebotan (migrado de `LVL_Demo01`).
- [ ] **Paso 4: `PreparedMapId`.** En el editor, cambiar `MapId` de `DA_Coop_C01` a `C01X` **sin guardar**; PIE 2 jugadores con *Run Under One Process* desactivado (el cliente es otro proceso y lee el asset guardado, `C01`); abrir `LVL_Coop_C01` → en el log del cliente `[Coop] PreparedMapId del servidor (C01X) no coincide con el nivel local (C01)` y el cliente no construye el mapa. Deshacer el cambio sin guardar.
- [ ] **Paso 5: registro.** Resultado en la checklist de playtest; sin commit.

---

# Retirada de `TN_RunGameMode` y `LVL_Demo01`

Qué se rescata (§7.1) y dónde queda: `ReviveImmunitySeconds`, muerte con rescate, resultados y vuelta al lobby siguen en la base que heredan `ATN_ProcMapGameMode` y `ATN_BeachRaceGameMode` (renombrada en la Tarea 36); las medusas de los escalones de `LVL_Demo01` pasan a rebotadores del layout (Tarea 26, `jellyfish` del manifest), el agua con `TN_Water` al importador (Tarea 24) y meta y nidos a marcadores (Tareas 23 y 27). Se quedan fuera de este plan, bloqueados por el lote 4.5 de `Docs/Limpieza-2026-09-29.md` (autores Alvaro2rh, Mokius y Sylkerr): `LVL_Run`, `BP_ChunkManager`, los `BP_Chunk_*`, `BP_RunGameMode` y el código de chunks de la base.

### Tarea 35: El Clásico deja de ofrecerse y `LVL_Run` sale del cocinado

**Files:**
- Create: `Source/Tortunabo/Private/Tests/TN_LobbyMissionTest.cpp`
- Modify: `Source/Tortunabo/Public/Lobby/TN_LobbyMission.h`, `Source/Tortunabo/Private/Lobby/TN_LobbyMission.cpp:80-94`, `Source/Tortunabo/Public/Lobby/TN_HQGameMode.h:25, 110-111`, `Source/Tortunabo/Private/Lobby/TN_HQGameMode.cpp:388-419`, `Source/Tortunabo/Public/World/ProcMap/TN_ProcMapEnums.h:27-35`, `Config/DefaultGame.ini` (línea `LVL_Run`), `Scripts/tests/test_cook_list.py`

**Interfaces:**
- Produces: `TNLobbyMission::SanitizeMode(ETNProcGameMode) -> ETNProcGameMode` (Clásico y `Count` → Coop); `NextSelectorMode` sin Clásico; `ATN_HQGameMode` sin `MatchMapPath`.

- [ ] **Paso 1: tests que fallan.** `Source/Tortunabo/Private/Tests/TN_LobbyMissionTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Lobby/TN_LobbyMission.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNLobbyNextSelectorModeTest, "Tortunabo.Lobby.NextSelectorMode",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNLobbyNextSelectorModeTest::RunTest(const FString& Parameters)
{
	using TNLobbyMission::NextSelectorMode;
	TestTrue(TEXT("Coop → Carrera"), NextSelectorMode(ETNProcGameMode::Coop, 3) == ETNProcGameMode::Race);
	TestTrue(TEXT("Carrera con 3 → Coop (ni 2 vs 2 ni Clásico)"), NextSelectorMode(ETNProcGameMode::Race, 3) == ETNProcGameMode::Coop);
	TestTrue(TEXT("Carrera con 4 → 2 vs 2"), NextSelectorMode(ETNProcGameMode::Race, 4) == ETNProcGameMode::TwoVsTwo);
	TestTrue(TEXT("2 vs 2 → Coop: el Clásico ya no se ofrece"), NextSelectorMode(ETNProcGameMode::TwoVsTwo, 4) == ETNProcGameMode::Coop);
	TestTrue(TEXT("Clásico guardado → Coop"), TNLobbyMission::SanitizeMode(ETNProcGameMode::Classic) == ETNProcGameMode::Coop);
	TestTrue(TEXT("Fuera de rango → Coop"), TNLobbyMission::SanitizeMode(ETNProcGameMode::Count) == ETNProcGameMode::Coop);
	TestTrue(TEXT("Carrera se queda"), TNLobbyMission::SanitizeMode(ETNProcGameMode::Race) == ETNProcGameMode::Race);
	return true;
}

#endif
```

  En `Scripts/tests/test_cook_list.py`: `FORBIDDEN` gana `'LVL_Run"'` (con la comilla de cierre) y `TRAVEL_MEMBERS` pierde `"MatchMapPath"`.
- [ ] **Paso 2: ver el fallo.** BUILD → no compila (`SanitizeMode`). PYTEST `Scripts/tests/test_cook_list.py` → `test_no_test_map_is_cooked` FAIL (`LVL_Run"`).
- [ ] **Paso 3: implementación.** `TN_LobbyMission.h`, dentro del namespace:

```cpp
	/** Modo jugable: el Clásico (retirado, plan maestro §7.1) y los valores fuera de rango pasan a Coop. */
	inline ETNProcGameMode SanitizeMode(ETNProcGameMode Mode)
	{
		return Mode == ETNProcGameMode::Classic || Mode >= ETNProcGameMode::Count ? ETNProcGameMode::Coop : Mode;
	}
```

  `TN_LobbyMission.cpp:84-92` (el bucle de `NextSelectorMode`):

```cpp
	for (int32 Step = 0; Step < Count; ++Step)
	{
		Next = (Next + 1) % Count;
		const ETNProcGameMode Candidate = static_cast<ETNProcGameMode>(Next);
		// El Clásico (LVL_Run) está retirado; 2 vs 2 solo se ofrece con exactamente 4 jugadores.
		if (Candidate != ETNProcGameMode::Classic && (Candidate != ETNProcGameMode::TwoVsTwo || ConnectedPlayers == 4))
		{
			break;
		}
	}
```

  `TN_ProcMapEnums.h`: el comentario de `ETNProcGameMode` pasa a `/** Modo de juego. Classic está retirado (plan maestro §7.1): se conserva el valor por las salas anunciadas con él y se trata como Coop. */` y `Classic UMETA(DisplayName = "Clásico (retirado)", Hidden),`. `TN_HQGameMode.h`: borrar `MatchMapPath` (líneas 110-111) y en el comentario de la línea 25 `Seamless Travel hacia LVL_Run` → `Seamless Travel al modo elegido (playa, Coop preparado o mapa procedural)`. `TN_HQGameMode.cpp`: `FString TravelURL = MatchMapPath;` (línea 388) → `FString TravelURL = ProcMapPath;`; primera línea dentro de `if (UMP_GameInstance* GI = …)`: `GI->SelectedProcMode = TNLobbyMission::SanitizeMode(GI->SelectedProcMode);`; `else if (GI->SelectedProcMode != ETNProcGameMode::Classic)` → `else`; el comentario de la línea 396 pierde «Clásico → LVL_Run». `Config/DefaultGame.ini`: borrar `+MapsToCook=(FilePath="/Game/Maps/Run/LVL_Run")`.
- [ ] **Paso 4: ver pasar.** BUILD; UETEST `Lobby` → `Success`; PYTEST `Scripts/tests/test_cook_list.py` → 6 passed.
- [ ] **Paso 5: commit.** `git add Source/Tortunabo/Private/Tests/TN_LobbyMissionTest.cpp Source/Tortunabo/Public/Lobby/TN_LobbyMission.h Source/Tortunabo/Private/Lobby/TN_LobbyMission.cpp Source/Tortunabo/Public/Lobby/TN_HQGameMode.h Source/Tortunabo/Private/Lobby/TN_HQGameMode.cpp Source/Tortunabo/Public/World/ProcMap/TN_ProcMapEnums.h Config/DefaultGame.ini Scripts/tests/test_cook_list.py` · `git commit -m "refactor(lobby): retira el modo Clásico del lobby y LVL_Run del cocinado"`

### Tarea 36: `ATN_RunGameMode` → base abstracta `ATN_MatchGameModeBase`

**Files:**
- Rename: `Source/Tortunabo/Public/Game/TN_RunGameMode.h` → `TN_MatchGameModeBase.h`, `Source/Tortunabo/Private/Game/TN_RunGameMode.cpp` → `TN_MatchGameModeBase.cpp`, `Source/Tortunabo/Public/World/TN_RunGameModeAccess.h` → `TN_MatchGameModeAccess.h`, `Source/Tortunabo/Private/World/TN_RunGameModeAccess.cpp` → `TN_MatchGameModeAccess.cpp`
- Modify: los ficheros de `Source/` que nombran `TN_RunGameMode` (hoy 27, incluidos `TN_ProcMapGameMode.h:40` y `TN_BeachRaceGameMode.h:119`), `Config/DefaultEngine.ini` (nueva sección `[CoreRedirects]`)
- Create: `Source/Tortunabo/Private/Tests/TN_MatchGameModeTest.cpp`, `Scripts/verify_gamemode_redirect.py`

**Interfaces:**
- Produces: `UCLASS(Abstract) class ATN_MatchGameModeBase : public AGameMode` (misma API pública que `ATN_RunGameMode`); `ATN_MatchGameModeBase* TN_ResolveMatchGameMode(UWorld*)`; `+ClassRedirects=(OldName="/Script/Tortunabo.TN_RunGameMode",NewName="/Script/Tortunabo.TN_MatchGameModeBase")`.

- [ ] **Paso 1: test que falla.** `Source/Tortunabo/Private/Tests/TN_MatchGameModeTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Game/TN_BeachRaceGameMode.h"
#include "Game/TN_MatchGameModeBase.h"
#include "Game/TN_ProcMapGameMode.h"
#include "UObject/CoreRedirects.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRunGameModeRetiredTest, "Tortunabo.Retire.RunGameMode",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRunGameModeRetiredTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("La base no se puede usar como GameMode de un nivel"), ATN_MatchGameModeBase::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
	TestTrue(TEXT("El Coop hereda de la base"), ATN_ProcMapGameMode::StaticClass()->IsChildOf(ATN_MatchGameModeBase::StaticClass()));
	TestTrue(TEXT("La carrera hereda de la base"), ATN_BeachRaceGameMode::StaticClass()->IsChildOf(ATN_MatchGameModeBase::StaticClass()));
	const FCoreRedirectObjectName Old(TEXT("/Script/Tortunabo.TN_RunGameMode"));
	TestEqual(TEXT("Redirección de la clase vieja (BP y niveles guardados)"),
		FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, Old).ToString(), FString(TEXT("/Script/Tortunabo.TN_MatchGameModeBase")));
	return true;
}

#endif
```

- [ ] **Paso 2: ver el fallo.** BUILD → no compila (`TN_MatchGameModeBase.h`).
- [ ] **Paso 3: renombrado mecánico** (Git Bash, raíz del repo):

```bash
git mv Source/Tortunabo/Public/Game/TN_RunGameMode.h Source/Tortunabo/Public/Game/TN_MatchGameModeBase.h
git mv Source/Tortunabo/Private/Game/TN_RunGameMode.cpp Source/Tortunabo/Private/Game/TN_MatchGameModeBase.cpp
git mv Source/Tortunabo/Public/World/TN_RunGameModeAccess.h Source/Tortunabo/Public/World/TN_MatchGameModeAccess.h
git mv Source/Tortunabo/Private/World/TN_RunGameModeAccess.cpp Source/Tortunabo/Private/World/TN_MatchGameModeAccess.cpp
git grep -l -e "TN_RunGameMode" -e "TN_ResolveRunGameMode" -e "\[RunGameMode\]" -- Source | xargs sed -i \
  -e 's/TN_RunGameModeAccess/TN_MatchGameModeAccess/g' \
  -e 's/TN_ResolveRunGameMode/TN_ResolveMatchGameMode/g' \
  -e 's/ATN_RunGameMode/ATN_MatchGameModeBase/g' \
  -e 's/TN_RunGameMode\.generated\.h/TN_MatchGameModeBase.generated.h/g' \
  -e 's/TN_RunGameMode\.h/TN_MatchGameModeBase.h/g' \
  -e 's/TN_RunGameMode/TN_MatchGameModeBase/g' \
  -e 's/\[RunGameMode\]/[MatchGameMode]/g'
git grep -n -e "TN_RunGameMode" -e "ATN_RunGameMode" -- Source    # debe salir vacío
```

  En `TN_MatchGameModeBase.h`: `UCLASS()` → `UCLASS(Abstract)` y el comentario de clase pasa a `@brief Base de los modos por rondas (Coop preparado/procedural y carrera en la playa): staging tras el viaje, meta, muerte con rescate, DBNO con inmunidad tras revivir, resultados y vuelta al lobby. Antes ATN_RunGameMode (redirección en DefaultEngine.ini).`. En `Config/DefaultEngine.ini`, al final:

```ini
[CoreRedirects]
+ClassRedirects=(OldName="/Script/Tortunabo.TN_RunGameMode",NewName="/Script/Tortunabo.TN_MatchGameModeBase")
```

- [ ] **Paso 4: ver pasar.** BUILD (reiniciar el editor: cambia el layout de clases, Live Coding no vale); UETEST `Retire` → `Success`; UETEST `Tortunabo` completo → sin regresiones.
- [ ] **Paso 5: BP con la redirección.** `Scripts/verify_gamemode_redirect.py`:

```python
"""Los BP de GameMode cargan con la base renombrada (redireccion TN_RunGameMode -> TN_MatchGameModeBase)."""

import unreal

BPS = ("/Game/ProcMap/BP_ProcMapGameMode", "/Game/Blueprints/Gameplay/GameModes/BP_RunGameMode")
BASE = unreal.load_class(None, "/Script/Tortunabo.TN_MatchGameModeBase")
for path in BPS:
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    if cls is None or not unreal.MathLibrary.class_is_child_of(cls, BASE):
        raise RuntimeError(f"[Redirect] {path} no hereda de TN_MatchGameModeBase")
    unreal.log(f"[Redirect] {path}: OK")
```

  UEPY `verify_gamemode_redirect.py` → dos líneas `[Redirect] …: OK` en el log. SMOKE `/Game/Maps/Coop/LVL_Coop_CP01` `[Coop] Mapa preparado CP01 listo` → 0.
- [ ] **Paso 6: commit.** Los `git mv` ya dejan preparados los cuatro renombrados; el resto, solo lo que tocó el `sed` (nunca `git add -u`: hay cambios de otros agentes en el árbol): `git add $(git grep -l -e TN_MatchGameModeBase -e TN_MatchGameModeAccess -e TN_ResolveMatchGameMode -e "\[MatchGameMode\]" -- Source) Config/DefaultEngine.ini Source/Tortunabo/Private/Tests/TN_MatchGameModeTest.cpp Scripts/verify_gamemode_redirect.py` · `git commit -m "refactor(modos): ATN_RunGameMode pasa a la base abstracta ATN_MatchGameModeBase con redirección"`

### Tarea 37: Borrar `LVL_Demo01` y su script

**Files:**
- Create: `Scripts/retire_run_mode.py`
- Delete: `Content/Maps/Run/LVL_Demo01.umap` (Rodrigo, `Docs/Limpieza-2026-09-29.md` 2.4), `Scripts/build_demo_level.py`

**Interfaces:** Consumes: Tarea 33 (el destino de la migración existe: `import_terrain_coop.py` y `LVL_Coop_C01`, condición de la Limpieza 2.4).

- [ ] **Paso 1: script.** `Scripts/retire_run_mode.py`:

```python
"""Retira LVL_Demo01 (plan maestro §7.1). Lo util ya vive en el Coop preparado: medusas = rebotadores del layout,
agua con TN_Water, meta y nidos desde marcadores; ReviveImmunitySeconds lo hereda ATN_ProcMapGameMode de
ATN_MatchGameModeBase. BP_RunGameMode, LVL_Run y LVL_TestMap se quedan hasta el lote 4.5 de Docs/Limpieza-2026-09-29.md.

    UEPY retire_run_mode.py
"""

import unreal

lib = unreal.EditorAssetLibrary
DEMO = "/Game/Maps/Run/LVL_Demo01"


def main() -> None:
    if not lib.does_asset_exist(DEMO):
        unreal.log("[RetireRun] LVL_Demo01 ya no existe")
        return
    referencers = [r for r in lib.find_package_referencers_for_asset(DEMO, False) if r != DEMO]
    if referencers:
        raise RuntimeError(f"[RetireRun] LVL_Demo01 aun referenciado por {referencers}")
    if not lib.delete_asset(DEMO):
        raise RuntimeError("[RetireRun] no se pudo borrar LVL_Demo01")
    unreal.log("[RetireRun] LVL_Demo01 borrado")


main()
```

- [ ] **Paso 2: ejecutar.** `git log -1 --format=%an -- Content/Maps/Run/LVL_Demo01.umap` → Rodrigo. UEPY `retire_run_mode.py` → `[RetireRun] LVL_Demo01 borrado`; `Content/Maps/Run/LVL_Demo01.umap` ya no existe. `git rm Scripts/build_demo_level.py`. `git grep -n "LVL_Demo01\|build_demo_level" -- Source Config Scripts` → solo `Scripts/tests/test_cook_list.py` (en `FORBIDDEN`) y `Scripts/retire_run_mode.py`.
- [ ] **Paso 3: tests.** PYTEST y UETEST `Tortunabo` → verdes.
- [ ] **Paso 4: commit.** `git add Scripts/retire_run_mode.py Content/Maps/Run/LVL_Demo01.umap Scripts/build_demo_level.py` · `git commit -m "chore(mapas): retira LVL_Demo01 y su script; lo útil vive en el Coop preparado"`

---

## Dependencias, carriles y horas

| Tarea | Depende de | h (est. propia) |
|---|---|---|
| 1 Guardia de cocinado | — | 1 |
| 2 `Source/Logs` (verificación) | — | 0,2 |
| 3 `OnTravelFailure` | — | 4 |
| 4 AppID | — | 3 (+ trámite Valve) |
| 5 N-A | — | 1,5 |
| 6 Dorado T0 | — | 3 |
| 7 CI local | 1 | 8 |
| 8 Playtest Shipping | 1, 3, 4, 7 | 4 + playtest |
| 9 BFS con padres | — | 4 |
| 10 Máscara y bake | 9 | 4 |
| 11 Manifest v2 (C01, P01) | 10 | 5 |
| 12 Decimado | — | 5 |
| 13 Rejilla C++ | — | 4 |
| 14 DA | 13 | 5 |
| 15 Vigilante (puro) | — | 3 |
| 16 Proveedor y Prepared | 13, 14, 15 | 5 |
| 17 `FindOpenSandSpot` | 16 | 3 |
| 18 Vigilante y gaviotas | 15, 16 | 4 |
| 19 R1 | 13 | 1,5 |
| 20 R2/R3 | 19 | 2 |
| 21 R5 | — | 3 |
| 22 R8 | — | 1,5 |
| 23 Marcadores | 19 | 2,5 |
| 24 Importador | 1, 11, 14, 16, 23 | 7 |
| 25 R9 + smoke (gate F1) | 24 | 2 |
| 26 Layout sintético | 23 | 4 |
| 27 Construcción Prepared | 20, 21, 25, 26 | 6 |
| 28 R6 | 27 | 2 |
| 29 Mecánicas | 27 | 3 |
| 30 Vegetación | 27 | 4 |
| 31 Catálogo y lobby | 27 | 5 |
| 32 CP01 | 11, 12 | 8 |
| 33 Importar CP01/C01 | 24, 31, 32 | 3 |
| 34 PIE 4P (gate F2) | 28, 29, 30, 33 | 6 |
| 35 Clásico fuera | 1 | 2 |
| 36 Base abstracta | 35 | 3 |
| 37 `LVL_Demo01` fuera | 33 | 1 |

Total: F0 ≈ 25 h, F1 ≈ 62 h, F2 ≈ 41 h, retirada ≈ 6 h; **≈ 134 h** (horquilla 110–160 h), sin contar playtests. Carriles paralelos (ficheros disjuntos): Python 9–12 y 32 · C++ puro 13, 15, 19–22 · F0 entero · retirada 35–36 tras F2.

## Autorrevisión

**Cobertura del spec (F0–F2 del §5, §2, §3.1 y §7.1).**

| Punto del spec | Tarea |
|---|---|
| F0: cocinado con `LVL_Lobby` y `LVL_BeachRace`, sin mapas de prueba | 1 (guardia), 35 (`LVL_Run`) |
| F0: `OnTravelFailure` (N-E) | 3 |
| F0: CI (`BuildCookRun` + Automation) y `build_check` relativo | 7 |
| F0: fuera `Source/Logs` | 2 (ya hecho en `macro-update`) |
| F0: N-A | 5 |
| F0: dorado T0 | 6 |
| F0: AppID | 4 (código y script; trámite manual) |
| F0: playtest Shipping | 8 |
| F1: manifest v2, bake, máscara | 9, 10, 11 |
| F1: camino ordenado BFS (P01) | 9 |
| F1: DA y `LoadFromManifest` | 14 |
| F1: importador con subniveles, ids estables, lápidas, `TN_REGENERATE` | 23, 24 |
| F1: `SafeGround`, `WalkableZAt` en túnel, `FindOpenSandSpot` | 13, 16, 17 |
| F1: rescate fuera de zona en servidor, gaviotas `CourseBackAt` | 15, 18 |
| F1: R1, R2/R3, R5, R8, R9 | 19, 20, 21, 22, 25 |
| F1: presupuesto de malla | 12, 32 (CP01 ≤ 1,1 M) |
| F2: generador Prepared, R4, R6, R7, `PreparedMapId`, entrada tardía | 26, 27, 28 |
| F2: mecánicas con `SpawnElement`, auditoría R10 | 29 |
| F2: vegetación en `ATN_BeachDecorField` | 30 |
| F2: catálogo, lobby, `TravelURL`, `?CoopMap=` | 31 |
| F2: CP01 propio de ~600 m, puentes naturales | 32, 33 |
| F2: aceptación (§3.1) | 25, 33, 34 |
| §7.1: retirar `TN_RunGameMode`/`LVL_Demo01` rescatando lo útil | 35, 36, 37 |

**Desviaciones y huecos declarados.**
1. `ATN_BeachRaceGenerator` no implementa `ITN_SafeGroundProvider` (spec §2.2): la playa conserva su lógica por la rama sin proveedor; se hará en F5a para no chocar con el carril 4.
2. Puentes de Mokius: el asset no está en el repo. El marcador `BridgeAnchor` existe; el spawn del puente queda bloqueado por el asset. CP01 usa los puentes naturales del generador camino (`arches`, cruces con `bridge_share`).
3. Elección del mapa Coop: catálogo, pizarra del general y consola `TN.Coop.Map`; falta una fila de botones en la pizarra (UI, est. 4 h, tarea aparte).
4. `branches` vacío en el manifest v2: el progreso de un lazo es el de la muestra principal más cercana.
5. Vegetación en línea en el manifest (no `vegetation.bin`).
6. `RebakeHeightsFromLevel` (A §5.7) y la vista previa en la pantalla de carga (§3.6, deseable) quedan fuera.
7. 2 vs 2 y la carrera procedural en `LVL_ProcMap` no se tocan (fuera de F0–F2).
8. `LVL_Run`, `BP_RunGameMode`, `LVL_TestMap` y el código de chunks de la base siguen hasta el lote 4.5 de la Limpieza (otros autores).
9. P01 recibe su bloque Coop (Tarea 11) pero no se importa: es el mapa de TcT (F7).

**Placeholders.** Búsqueda de `TBD`, `TODO`, «añadir validación», «similar a la tarea»: ninguno. Las dos ramas condicionales del plan (R9 en la Tarea 25 y el reintento de estilo en la 32) llevan su código o sus valores exactos.

**Consistencia de nombres y firmas** (se usan igual en todas las tareas): `TNCoopMap::FSafeGrid`/`ParseBake`/`IsAllowed`/`IsInBounds`/`WalkableTraceWindow`/`FallbackWalkableZ`/`ComputeSeaDistance`/`SeaNearAt` (13, 28); `FTNCoopBakeGrid`, `UTN_CoopMapData::LoadFromMemory`/`LoadFromManifest`/`GetSafeGrid`/`NearestMainIndex`/`MainDirAt` (14); `StepWatch`/`FWatchState`/`FWatchInput`/`IsMoverBusyForRescue`/`RescueSampleIndex`/`RescueCandidates`/`PickRescueSlot` (15, 16, 18); `ITN_SafeGroundProvider::{IsInBounds, IsAllowed, WalkableZAt, GetProgressAt, CourseBackAt, FindRescueSpot}` (16, 17, 18); `ETNTerrainSource`, `ETNCoopMarkerKind`, `FCoopMarker`, `ATN_CoopMarker::ToView` (16, 23, 26, 27, 29); `FCoopLayoutInput`/`LayoutFromCoopData` (26, 27); `IsInsideFinish`/`FinishVolumeZ`/`bPreparedFinishInWater` (20, 27); `SpawnFauna` (21, 27); `SpawnCoopElements` (29), `BuildCoopDecor`/`BeginBuildFixed` (30); `UTN_CoopMapCatalog::Get`/`FindById`/`GetIds`, `NextMapId`/`ResolveMapId`/`BuildCoopTravelURL`, `SelectedCoopMapId` (31, 33); Python `walk(…, z_min_m, min_z_m)`, `coop.walk_with_parents`/`trace_path(parent, link_parent, start, end)`/`solve_walk`/`main_path`/`build_coop_block`/`write_manifest_v2`, `decimate.fit_budget`, `coop_markers.marker_specs`/`plan_marker_sync` (9–12, 24, 32); `ATN_MatchGameModeBase` (36, 37).
