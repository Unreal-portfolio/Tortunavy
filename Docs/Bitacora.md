# Bitácora de desarrollo

Registro cronológico de sesiones. Solo se añade; una entrada por sesión, con lo hecho,
lo verificado y lo que queda. El detalle técnico vive en los commits y en `Docs/`.

## 2026-09-22 — Merge de DisenoIng, plugin MCP y terreno por módulos de 400 m

Rama de trabajo: `feat/procgen-terrain` (HEAD `49079d5`, 24 commits sobre main, sin push).
main: `c231ef3`.

### Repositorio

- Merge de `DisenoIng` (Mokius, 2026-04-29) en main: registro por tag de botones y
  placas, `BP_Chunk_Medium_Personaliced`, fallback de ragdoll portado a
  `TortugaCharacter_Knockdown.cpp`, `MaxConcurrent` en `ATN_SpawnZoneBase`. Binarios en
  conflicto (`BP_ChunkManager`, `BP_QuadActor`): se conservó main. Pendiente manual:
  volver a añadir `BP_Chunk_Medium_Personaliced` al pool de `BP_ChunkManager`.
- `.gitattributes`: `.uasset`, `.umap` y similares como binarios.
- `UnrealMCPython` versionado en main (Source/Content/Resources, 1,8 MB); el script
  `setup-unreal-mcp.ps1` queda para reinstalar o registrar el servidor en Claude Code.
- Ramas locales borradas: `merge/diseno-ing`, `feat/procgen-grid-demo`.

### Terreno por módulos (decisión de Rodrigo: celda de 400 m, módulos como plantilla)

- Runtime: `UTN_TerrainModuleAsset`, `ATN_TerrainModuleTile`,
  `TN_TerrainModuleDecisions.h`, `TN_TerrainModuleWallDecisions.h`, modo módulos en
  `ATN_GridMapGenerator` (grid 4x4, celda 40 000 en `LVL_ProcGenDemo`).
- Pipeline: `Scripts/gen_terrain_modules.py` (PNG 16 bits, borde canónico verificado)
  → `Scripts/import_terrain_modules.py` (DA_M_* + BP_M_* en `/Game/Terrain/Modules`).
- Verificado en editor headless: 300 módulos importados, 5 tests
  `Tortunabo.TerrainModule.*` verdes, smoke `-game` del nivel de demo OK, con rutas
  secundarias (atajos, rutas altas) y puentes como instancias.
- Hecho pero solo con build del target de juego: muros de basura en bocas no usadas
  (`BlockedExits` + `WallSeed` replicados), cobertura de salidas (T y cruz valen para
  rectas), test `CoveringAndWalls`, librería de 600 PNG con 5 estilos.

### Siguiente sesión

1. Cerrar editor, `Build.bat TortunaboEditor Win64 DebugGame`.
2. `import_terrain_modules.py` (600 módulos, headless).
3. `Automation RunTests Tortunabo.TerrainModule` (6) y smoke `-game`.
4. Reabrir editor con `UnrealEditor-Win64-DebugGame.exe`, mirar muros y puentes en PIE.
5. Push de `feat/procgen-terrain` y merge a main.

Kanban: `card-1790093849652-b7kug3`. Diseño: `Docs/Archivo/2026-09-22-Terreno-Modulos-400m.md` (eliminado).

## 2026-09-22 (tarde-noche) — Rediseño del terreno: fases 1 y 2

Tras el playtest del terreno de 400 m: poco natural (secundarios y puentes pegados),
módulos demasiado grandes, todo pared o agua. Decisiones: módulos de 200 m, solo celdas
de ruta, arcos transitables por debajo, biomas por semilla (los diseñadores editan
encima), no regenerar la librería de 600 hasta la fase 4. `ATN_GridMapGenerator` pasa a
ser el layout oficial del Run; `ATN_ChunkManager` queda para puzzles en plazas.

- `96c466e` `ATN_SeagullActor` deprecado (NotPlaceable + aviso en BeginPlay).
- `0676da9` F1: desvíos a nivel de mapa (`TN_GridRouteDecisions.h`), solo celdas de ruta.
- `3d96bb5` PlayerStarts y pawns ya creados se recolocan en la celda de inicio.
- `62c7124` Arcos de roca (`TNTerrainModule::BuildArchMesh`) en lugar de puentes de cubo.
- `a5d05e4` F2: generador a 200 m con bifurcaciones, atajos curvos, ruta alta suavizada,
  arcos decorativos y validación de acceso a pie. Muestra de 60 en
  `/Game/Terrain/ModulesPreview`, asignada a `BP_GridMapGenerator` (grid 6x6).
- Verificado: 42/42 tests, smoke `-game` y captura con el jugador en la plaza de inicio.
- Análisis de sistemas (workflow Sonnet + síntesis) y de ragdoll en red; decisiones de
  ragdoll en memoria (`project_ragdoll_red.md`).

### Siguiente sesión

1. Playtest en PIE de `LVL_ProcGenDemo`: bifurcaciones, atajos y arcos de cerca.
2. Revisar el ensure del ISM en `ATN_TerrainModuleTile::BuildWalls` (¿previo a estos cambios?).
3. F3: monolitos, cuevas/overhangs, zonas hundidas, acantilados.
4. F4: biomas por semilla con blend y regenerar la librería borrando la de 400 m.
5. Pendiente: GDD de Rodrigo para cruzar con la auditoría de sistemas; fix de ragdoll en red.
