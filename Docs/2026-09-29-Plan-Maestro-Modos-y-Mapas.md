# Plan maestro: modos y mapas de Tortunabo

Fecha: 2026-09-29 · Base: `main` 41a0ee8a5 · Fuentes: informes A–G en `Docs/Analisis/2026-09-29/`.
Notación: `[B §4]` = informe y apartado; `fichero:línea` = código (relativo a `Source/Tortunabo/`); «est. propia» = estimación de este plan, sin medir; `[1.ª pasada]` = valor de ajuste para playtest.

## 0. Resumen ejecutivo

1. Cuatro modos: Coop, Carrera, Rally Tortuga y Todos contra Todos (TcT). Nada más.
2. Una fuente de terreno volumétrico empaquetable (StaticMesh, manifest v2, bake de alturas, máscara) sirve a Coop, TcT y Rally. La Carrera conserva su heightfield.
3. Coop: mapas fijos (`Source=Prepared` en `ATN_ProcMapGenerator`) con rescate fuera de zona.
4. Carrera: se corrigen 10 defectos del reparto y se añaden tramos de autor; el relieve no se toca.
5. Rally: buggy de HellYeah sin fichas, skins por tinte, mapas geográficos volumétricos con calzada tallada y malla reducida, 4 ítems.
6. TcT en P01: amplía lo existente (doble salto, noqueo, caparazón) con deslizamiento en pendiente, estampado y 7 objetos.
7. Red: 6 mecánicas con desfase (el puente tambaleante, grave), noqueo con ragdoll local y dive sin predicción.
8. Steam: cocinado explícito, `OnTravelFailure`, CI, AppID propio, guardados versionados.
9. Hoja de ruta F0–F8: 549–752 h de código, sin playtests; hasta 6 carriles paralelos.
10. Arte: 11 assets nuevos imprescindibles; el resto se reutiliza o se genera en código.

## 1. Decisiones cerradas

| # | Decisión | Contradicción con los informes y resolución |
|---|---|---|
| 1 | Modos: Coop, Carrera a pie, Rally Tortuga, TcT. Fuera escalada y fiesta. | [D §3–4] propone 13 modos y un top 4 (sumo, patata, escalada, caza). **Descartado.** |
| 2 | Coop con mapas volumétricos fijos desde bocetos; al menos un mapa propio de 600 m; mecánicas y vegetación de la carrera; protección fuera de zona. | [D §0 C4, §4] propone un Coop híbrido con camino procedural. **Descartado**; se aplica [A]. |
| 3 | Carrera: generador de Mokius y relieve intactos; pulido del reparto y tramos de autor. | Sin contradicción. Precisión [E]: el relieve no depende de la semilla (`TerrainSeed` es `constexpr`, `TN_BeachLayout.h:68`); la semilla solo decide el reparto. |
| 4 | Ningún modo principal 100 % procedural en assets. | Coincide con [D §4] salvo en Coop (ver 2). |
| 5 | Rally: buggy sin fichas, skins, mapas geográficos volumétricos (relieve ×3–6, calzada tallada, LOD y decimado). | [C §3] recomienda UE Landscape porque la malla volumétrica no escala (3 M tri/km²). **Descartado**: sale la tarea Landscape [C §6 #4] y entra la reducción de malla (§2.5). |
| 6 | TcT tipo Gang Beasts en P01 con «física a medias»: agarrar/lanzar y estampado, sin ragdoll activo. | [D §1] es la base. El ragdoll del noqueo es local y descuadra la posición [G §1]: se corrige (§3.5). |
| 7 | Doble salto: lanza a la agarrada, desliza en pendiente y se estampa contra la pared, sincronizado. | [G §2] coincide: rebote predicho y bola física (`StartBody`), sin ragdoll. |
| 8 | Todas las mecánicas sincronizadas. | Se aplica [B §4] (§4). |
| 9 | El terreno volumétrico ya está en `main`. | — |

## 2. Arquitectura común: terreno preparado

Principio [A §1]: tras importar, el nivel manda. El terreno es StaticMesh; los datos de juego, un DataAsset cocinable; lo editable, marcadores del nivel. La usan Coop, TcT y Rally.

### 2.1 Piezas

- **Manifest v2** [A §2]: conserva las claves v1 y añade el bloque `coop` (también para TcT y Rally): `bounds_uu`, `path` (principal y ramales con ancho), `start`, `finish`, `nests`, `water{z_uu, swim}`, `zones`, `beach_elements`, `vegetation`, `bake`. Toda `pos` lleva la Z del **suelo caminable**. El Rally añade `checkpoints_uu` [C §4.5].
- **Bake** (`bake.bin`, cabecera `TNCB` y capas zlib, paso 100 uu): `top_z_cm`, `safe_low`, `safe_high`, `path_dist_dm`. E01: 2,2 MB en bruto [A §2].
- **Máscara de zonas permitidas**: `safe_low/high` = niveles k mínimo y máximo alcanzables desde el inicio por columna; salen del `seen` que ya calcula `walk()` (`Scripts/gen_terrain_volume.py:57`). El camino ordenado sale del BFS con padres; P01 hoy no tiene orden (`gen_terrain_platforms.py:110`).
- **StaticMesh**: `UTN_TerrainMeshAsset::BuildStaticMesh` (`TN_TerrainMeshAsset.cpp:50-150`) ya crea SM con Nanite y colisión compleja. `Scripts/import_terrain_coop.py` monta `LVL_<modo>_<n>` con subniveles `_Terrain` (del script), `_Markers` (el diseñador retoca) y `_Design` (el script no lo abre) [A §3].
- **`UTN_CoopMapData`** (DataAsset) y **`ATN_CoopMarker`** (no replicado, va en el paquete).
- `ATN_MapVariantLoader` queda solo para vista previa: lee `Scripts/`, que no se empaqueta (`TN_MapVariantLoader.cpp:157`), y cocina la colisión síncrona (`:344`).

### 2.2 Fachada de altura válida en túneles

`ITN_SafeGroundProvider` [A §1]: `IsAllowed`, `FindRescueSpot`, `CourseBackAt` y, nueva, **`WalkableZAt(XY, ZHint)`**: el suelo bajo `ZHint`, acotado a `[safe_low, safe_high]`, trazando desde `ZHint + 2 m`. Motivo: `TerrainHeightMap` devuelve el techo del túnel (R4, `TN_ProcMapGenerator_Spawn.cpp:431` y `:231`). Proveedores: `ATN_ProcMapGenerator` (Prepared), `ATN_BeachRaceGenerator` (lógica actual) y el GameMode del Rally. Los resuelve `UTN_SafeGroundSubsystem`; `TNBeach::FindOpenSandSpot` (`TN_BeachStun.cpp:373-377`) lo consulta primero.

### 2.3 Rescate fuera de zona (servidor)

[A §4]: permitido si `safe_low ≤ k(Z) ≤ safe_high + 1` y dentro de `bounds_uu`. Vigilancia cada 0,25 s por pawn: en suelo permitido guarda `LastSafe`; más de 1,5 s en suelo no permitido, o fuera de límites, dispara el rescate a la muestra del camino con mayor S ≤ `LastSafe.Progress` (anillos si está ocupada), con `TNBeach::ReleaseTurtle` y aturdimiento (`TN_BeachStun.cpp:341-365`). Gaviotas: `CourseBack` pasa de −X fijo (`TN_BeachGullZone.cpp:323-331`) a `CourseBackAt(P)`; una suelta en zona no permitida se rescata al aterrizar. Agua y barrancos siguen siendo muerte por `kill_boxes_uu`.

### 2.4 Correcciones del generador para Prepared [A §6]

R1 aceptar la etiqueta `TN_MapTerrain` (`TN_ProcMapGenerator.cpp:382`; hoy congela el pawn hasta 10 s) · R2 meta con `F.Dir` (`:85`) · R3 volumen de meta con la Z del marcador (`Spawn.cpp:444`) · R5 extraer `SpawnFauna()` de `BuildStructures` (`Build.cpp:3305`) · R6 `SeaNear` desde la máscara · R7 `ProgressOrigin` con `WorldSize = grid·cell_uu` · R8 la pantalla de carga detecta la partida por GameMode (`TN_LoadingScreenSubsystem.cpp:539/614/1145`) · R9 verificar que la colisión sale de la malla fuente y no de la reserva Nanite al 10 %.

### 2.5 Presupuesto de malla (consecuencia de la decisión 5)

Datos: ~17 MB por millón de triángulos en SM de editor [A §3]; 3 M tri/km² [C §1]; un mapa de rally de 1,8–2,4 km de lado [B §5] daría 9,7–17 M triángulos [calc]. Medidas: voxel adaptativo (1 m en calzada, arcén y voladizos; 2–4 m fuera); decimado cuádrico por trozo antes de TNTM2 con tope de error vertical; colisión simple decimada fuera de la calzada; streaming por subniveles (World Partition solo por encima de 4 km [C §3]); mapa centrado en el origen, sin pasar de ±8 km. Objetivo propuesto, a validar en F6b: 1 M tri/km² de media (≈98 MB de SM de editor en 2,4 km [calc]).

## 3. Modos

### 3.1 Coop sobre mapas fijos

**Objetivo.** Mapas de diseño: boceto → generador volumétrico → retoque. Candidatos: C01, E01, F01 y **CP01**, mapa propio de Claude de 600 × 600 m. P01 queda para TcT.

**Diseño.** `ATN_ProcMapGenerator` gana `Source {Procedural, Prepared}` y `CoopData`. En Prepared, `BuildLayout` toma camino del DA y features de los marcadores; `BuildTerrainFromBake` rellena `Heights`; se saltan estructuras, *scatter* y flora, no la fauna; el generador va en Z = `water_uu`, así `SeaLevel = 0` cae en la lámina (`TN_ProcMapTerrain.h:96`). Mecánicas desde marcadores `BeachElement`: catapulta, `TN_BeachToyTank`, `TN_BeachQuadLane`, trampolín, plataformas móviles y tambaleantes, gaviotas. Vegetación: `ATN_BeachDecorField` con los puntos del DA. `TN_REGENERATE=1` nunca mueve marcadores ni resucita los borrados (`ATN_CoopMapAnchor.KnownIds`).

**Cambios.** Nuevos: `TN_CoopMapData`, `TN_CoopMarker`, `TN_SafeGroundSubsystem`, `UTN_CoopMapCatalog`, `export.py::write_coop`, `import_terrain_coop.py`. Modificados: `TN_ProcMapGenerator*.cpp`, `TN_ProcMapGameMode` (vigilante), `TN_BeachGullZone`, `TN_BeachStun`, `TN_LoadingScreenSubsystem`, `ATN_HQGameMode` (`TravelURL` al nivel de Coop; hoy `ProcMapPath`, `TN_HQGameMode.h:115`). Auditoría R10: 22 ficheros llaman a `ATN_BeachRaceGenerator::Find` y pueden quedar inertes fuera de la playa.

**Red.** `ServerTravel` a `/Game/Maps/Coop/LVL_Coop_<n>`; DA y marcadores ya están en cada paquete. `FTNProcMapNetConfig.PreparedMapId` con error si no coincide en `OnRep`. `Seed` sigue decidiendo conchas y peligros. Entrada tardía por `BeginPlay` (`TN_ProcMapGenerator.cpp:64`).

**Tests.** Automatización: `LoadFromManifest`, `IsAllowed`, `WalkableZAt` en túnel, `FindRescueSpot`. Pytest: BFS con padres, máscara, formato de `bake.bin`. Smoke `-game` headless por mapa; PIE 4P *listen*.

**Aceptación.** CP01 recorrible y ≤ 1,1 M triángulos (C01: 1,06 M [A §3]) · ningún cliente cocina colisión (`TN_MapVariantLoader.cpp:358` ausente del log) · ningún pawn congelado > 1 s al aparecer · 20 lanzamientos forzados fuera de zona → 20 rescates en ≤ 1,75 s a < 1 m del camino · meta válida orientada a −X y +Y · cada mecánica activada al menos una vez en PIE 4P sin `Find` nulo · `PreparedMapId` alterado → error en log.

### 3.2 Carrera: pulido del reparto y tramos de autor

**Objetivo.** Generador y relieve intactos; reparto corregido [E §1] e inserción de tramos de autor.

**Diseño.**

- Correcciones primero: **P1**, el sprint deja de destruir el castillo principal (D1, `TN_BeachRaceGenerator.cpp:527-533`): `GenerateRound(..., bSprint)` lo aparta de [SprintX − 80, SprintX + 30] m. **P2**, `OwnerItem` en `Reserved`/`Interest` (sin lanzadores huérfanos). **P3**, `EnsureCatapults` con `TryAddWithArc`.
- Pulido P4–P8: filas sin posición fija y respiro cada ~150 m; presupuesto de decorado alcanzable (D6: primer tercio al 52 % con objetivo 66–76 %); halo de 2,5–3 m en trampas; equidad en la primera banda; métricas. P9 (subflujos de RNG) en commit aparte con nuevo test dorado.
- Híbrido [E §3]: `UTN_BeachStretchAsset` (un tramo por fichero, `Pinned` o flotante) y `UTN_BeachRaceCourseAsset` (slots). `PlaceAuthored()` antes de `PlaceMainDungeon` con su propio RNG; `Role = Authored` intocable por `RestorePassage`. Vocabulario `ETNBeachElement`: sin ruta nueva de red ni render.

**Cambios.** `TN_BeachLayout.h`, `TN_BeachRaceGenerator(.h/.cpp/_Round.cpp)`, `FTNBeachRoundNet` (`TN_BeachRaceGenerator.h:30-61`: `CourseId`, `CourseHash`, colocaciones), `UTN_BeachRoundSyncComponent`; editor: `LVL_BeachStretch_Workbench`, `ATN_BeachStretchProxy`, `ATN_BeachStretchVolume` (*Export/Import/Validate*).

**Red.** El servidor elige tramos con enfriamiento entre rondas; los clientes reciben las colocaciones resueltas; hash distinto → aviso.

**Tests.** Dorado 24 semillas × 3 dificultades (T0) antes de tocar nada; con curso vacío, reparto idéntico bit a bit; tests de tramos, carrera fija y sprint.

**Aceptación.** Castillo intacto tras el sprint en las 72 semillas · 0 lanzadores huérfanos y arcos de catapulta validados (el test deja de excluir `Launcher`, `TN_BeachLayoutTest.cpp:715`) · cobertura del primer tercio ≥ 66 % · hash del heightfield idéntico · un tramo que por sí solo corta el paso se rechaza al exportar.

### 3.3 Rally Tortuga

**Objetivo.** Rally de buggies en mapas geográficos volumétricos: E01B (España conducible), luego Europa, Mundo y el catálogo de [C §5].

**Buggy** [B §1, §7]. `HYBuggy` → `ATN_Buggy` en `Vehicles/`, **sin `Cargo`** ni casos de carga; rutas a `UPROPERTY(EditDefaultsOnly)`; plugin `ChaosVehiclesPlugin`. Física **síncrona con subpasos** (`MaxSubstepDeltaTime=0.016667`); asíncrona solo si `buggy_measure` lo exige, porque afecta a caparazón, ragdoll, catapulta y trampolín. Pawn = buggy con tortuga visual (`UTN_CosmeticLook`) [B §2]; pasajeras reales según P2. Pulido: atropello desde `RunOverSpeed`, anti-vuelco y enderezado a 4 s, fricción por material, cámara, audio en C++ sin BIE, HUD. Skins: `DT_BuggySkins`, `FTN_BuggyLook` replicado en el PlayerState, categorías `BuggyPaint/Wheels/Deco`, `M_BuggyPaint` con los parámetros de `M_TurtleBody` (patrones `ETNShellPattern` gratis), pestaña en `UTN_ShopWidget`.

**Mapas** [C §2–4, B §5]. E01 no es conducible: pendiente p50 18°, p90 45°, exageración efectiva 24,7× [C §1]. `GeoRegion` (pyproj, zoom automático) sustituye las constantes de `layout.py`/`model.py`; exageración 3–6× calibrada por pendiente; ruta de coste mínimo (`MCP_Geometric`); corredor tallado de 12–16 m, arcenes de 10–20 m, pendiente sostenida ≤ 12°, peralte < 8°, radio ≥ 25 m; checkpoints cada 150–250 m (`checkpoints_uu`); mar o precipicio → último arco [D §2.2]; malla según §2.5; créditos de fuentes obligatorios [C §1]. Europa en LAEA y Mundo equirectangular ±72° [C §2].

**Red** [B §3]. Opción A (servidor con autoridad y `PredictiveInterpolation`): los 2,4 m de HellYeah son el retraso normal de un proxy. 30 Hz cerca y 10 Hz lejos; `ForceNetUpdate` en choque, boost y enderezado. Opción E (autoridad del conductor validada por el servidor, 20–30 h) solo si la medición lo pide. Subida del anfitrión: ≈280 KB/s a 60 Hz con 8 buggies; objetivo 100–140 KB/s.

**Ítems en el Rally.** A favor: son el catch-up natural de una carrera de 8–10 min, dan función a la artillera [D §2.3] y `RollLoot` ya pondera por puesto (`TN_RaceItems.h:153`). En contra: cada efecto sobre el chasis es un impulso autoritativo que el cliente ve un RTT tarde (con opción E, RPC `Client` al dueño [B §3]), sube el tráfico del anfitrión, los vuelcos provocados frustran, y gaviota o pelícano tendrían que coger un vehículo (coste L [D §2.4]). El rebufo (+15 % a < 30 m [D §2.4]) ya da algo de catch-up. **Recomendación: sí, 4 ítems** en cajas en los arcos y a mitad de tramo; los usa la artillera o, si va sola, la conductora. Fuera: coco dorado, gaviota, pelícano, disco y silbato.

| Ítem | Efecto sobre el buggy [1.ª pasada] | Autoridad y réplica |
|---|---|---|
| `Coconut` | Boost de 2 s (`BoostMath`) | Fuerza en el servidor; `bBoosting` replicado con `ForceNetUpdate`; el dueño solo predice lo visual |
| `SandMine` | Se suelta detrás; impulso vertical y agarre al 50 % durante 1 s, sin vuelco forzado | Solo servidor (`AddImpulse` al chasis); sin empujón local, para no repetir el doble empujón [B §4] |
| `HomingCrab` | Persigue al buggy de delante; velocidad ×0,5 al impactar | Movimiento y frenazo en el servidor |
| `Sunscreen` | Escudo de 4 s contra los otros 3 y el atropello | Estado replicado en el buggy; el servidor filtra impactos |

**Cambios.** Nuevos `Vehicles/TN_Buggy*` y `TN_BuggyData`; modificados `TN_RaceItemComponent` (objetivo vehículo), `TN_CosmeticLook`, `TN_CosmeticSaveGame`, `TN_ShopWidget`, `DefaultEngine.ini` (`[/Script/Engine.PhysicsSettings]`); Python: `Scripts/terrain_geo/*` (`GeoRegion`, ruta, tallado, validador) y decimado en `terrain_vol/mesh.py`.

**Tests.** `BuggySpec` sin carga; validador del corredor en pytest; piloto IA headless; `buggy_measure`.

**Aceptación.** Corredor de E01B: pendiente ≤ 12° sostenida y ≤ 20° en tramo corto, escalón ≤ 0,4 m entre muestras de 1 m, radio ≥ 25 m, ancho ≥ 12 m [B §5] · piloto IA: 0 atascos y 0 vuelcos en 10 recorridos · subida del anfitrión ≤ 140 KB/s con 8 buggies · correcciones del conductor medidas con `p.NetShowCorrections` · una skin comprada se ve igual en 4 máquinas y persiste al reiniciar.

### 3.4 Todos contra Todos: «Batalla de los Puentes» en P01

**Objetivo.** Caos tipo Gang Beasts en el mapa del director, sobre **lo que ya existe**: doble salto (dive), noqueo, caparazón y agarrar/lanzar. Se amplía con las dos piezas que faltan [G §0]: seguir deslizando en pendiente de arena y estamparse contra la pared en el aire (§3.5).

**Diseño** [D §1]. 2–8 jugadoras, a 3 conchas (`ETNBeachRacePhase`, `RaceShellHalves`); rondas de 120 s [1.ª pasada]. Puntos: +1 por echar a una rival al río, atribuido al último contacto en 6 s (`LastInstigator`); caída sin agresor = 0 y 8 s de espera; muerte por entorno = +1 a la líder. Caer al río es baja con reaparición, no muerte real [D §0 C1]. Reaparición: 4 s en el huevo de su meseta y 2 s de inmunidad (`ReviveImmunitySeconds`, `TN_RunGameMode.h:145`). Puentes en MVP: ráfagas de viento y barricadas; el corte exige `ATN_BridgeSpan` (P4). Loot: `RollLoot` con `Norm` por puntos. Gaviotas con suelta ajustada a tablero o meseta (~70 %); cangrejos. P01 [D §1.9]: 8 nidos, 8 cajas, cofre en el cuello, 15 % de cobertura con assets existentes, 2 rampas de 20°, 2 catapultas y 3 trampolines, todo como marcadores (§2) en `LVL_Bridges_P01`.

**Catálogo inicial de objetos.** Base: `ATN_ThrowableItemActor` (impacto decidido en el servidor, `TN_ThrowableItemActor.cpp:270`; trayectoria local desde `Multicast_InitializeThrow`; punto final fijado por `Multicast_BallStopped`), `ApplyKnockdown` (`TortugaCharacter.h:1368`), `ForceEnterShell`/`StartBody` (`TN_ShellComponent.h:70/79`) y `ATN_InkProjectile`. Las «pistolas» disparan proyectiles de ese patrón: sin *hitscan* ni ragdoll nuevo. Horas: est. propia. Aceptación en PIE 4P con PktLag 150 y 2 % de pérdida.

| Objeto | Efecto | Autoridad y réplica | h | Assets nuevos | Aceptación |
|---|---|---|---|---|---|
| Piedra | Lanzable; noquea desde 600 cm/s (`MinKnockdownSpeed`, `.h:98`) | Throwable sin cambios | 2–3 | 1 icono (malla `Piedra1.uasset`) | 20 lanzamientos: noqueo solo si v ≥ 600; un único pickup por lanzamiento |
| Balón de playa | Rebota mucho; empuja sin noquear | Flag `bKnockdownOnHit=false` + `PushImpulse`: `LaunchCharacter` en servidor y dueño (patrón `TN_BeachClamTrap.cpp:871-886`) | 3–4 | 1 icono (malla procedural `TNBeachProp`) | 0 correcciones > 50 cm tras el empuje |
| Trabuco de aire | Cono corto: mete a la rival en caparazón y la lanza como bola | Servidor, respetando `TNBeach::ResolveMover`: `ForceEnterShell(false)` + `StartBody(v)`; se replica `ATN_ShellBody` | 5–7 | 1 icono (malla procedural) | Bola en la misma posición en 4 máquinas (±5 cm) |
| Dardo de medusa | Proyectil lento y visible que noquea | `ApplyKnockdown` en el servidor; **depende** del noqueo autoritativo (§3.5) | 4–6 | 1 icono (malla de medusa reescalada) | Cápsula tras el noqueo igual en 4 máquinas (±10 cm) |
| Pistola de tinta | Ráfaga de 3 tintas que ciega | `ATN_InkProjectile` + `MulticastApplyInkEffect` (`TN_InkProjectile.h:111`) | 2–3 | 1 icono (`BP_InkProjectile`) | 3 impactos → 3 efectos en todas las máquinas |
| Garfio | Atrae a la rival; en caparazón, la deja a tiro de agarre | Servidor valida alcance y visión; `LaunchCharacter` en servidor y dueño, o `StartBody` en caparazón | 8–12 | 1 icono (ancla procedural + `UCableComponent`) | 20 tirones sin atravesar geometría; agarre válido en 1 s |
| Pala de mano | Golpe cuerpo a cuerpo; noquea a < 1,5 m | Barrido en servidor + `ApplyKnockdown` | 4–6 | 1 icono (receta de `TN_BeachSpadeRamp` a escala) | Nunca golpea a través de pared (test de traza) |

Total: 28–41 h y 7 assets nuevos (iconos).

**Cambios.** `ETNProcGameMode::Bridges` (`TN_ProcMapEnums.h:29`) o GameMode propio; `LastInstigator` en `ATortugaCharacter`; filas en `DT_Items`; subclases de `ATN_ThrowableItemActor`; actor de viento.

**Tests.** Atribución (< 6 s puntúa, ≥ 6 s no); reaparición en el nido más alejado si hay rival a < 15 m; pesos de loot por puntos.

**Aceptación.** Ronda a 8 jugadoras sin errores; encuentro cada 15–20 s [D §1.8] medido por telemetría en playtest; todos los objetos cumplen su columna.

### 3.5 Tortuga: doble salto físico

**Estado** [G §0–1]. Lanzar a la agarrada ya existe (`ThrowWithDive`, `TN_CarryComponent.cpp:266`). Deslizar existe, pero en arena se anula: con μ = 800 solo acelera por encima de 45,2°, ningún suelo caminable. El estampado en vuelo no existe. El dive no se predice: `LaunchCharacter` solo corre en el servidor (`TortugaCharacter_Dive.cpp:207`) y el dueño lo recibe como corrección. El ragdoll del noqueo es local en cada máquina (`TortugaCharacter_Knockdown.cpp:358`).

**Diseño** [G §2], como ampliación:

1. **Pendiente** (CMC, predicho): desde `BellySlopeMinAngle` (12°), rozamiento ×0,3 y freno ×0,4; temporizador en pausa en bajada (tope 6 s); entrada con módulo 3D y tope de 1000 cm/s.
2. **Rebote predicho en vuelo**: `HandleImpact` con `N.Z < 0,35` y velocidad relativa; restitución 0,45; conserva el 60 % tangencial.
3. **Estampado autoritativo**: desde 650 cm/s se anota `PendingSplat`; `TickDive` llama a `ServerDiveSplat` (árbitro, `EndDive`, `ForceEnterShell(false)`, `StartBody(v reflejada)`, `Multicast_DiveSplatFX`). Ningún actor se crea dentro de `ServerMove`.
4. **Predicción del dive**: `DiveDir` en `FTNTurtleNetworkMoveDataContainer` (`TN_TurtleMovementComponent.h:31`) e impulso en `PerformMovement`. Obligatoria por la decisión 7.
5. **Noqueo autoritativo** (est. propia, 6–10 h): extender `ServerFreezeRagdoll`/`RagdollFrozenLoc` (`Knockdown.cpp:915`), hoy solo en la muerte, al noqueo; el ragdoll queda cosmético y la posición la fija el servidor. Lo exigen el dardo y la pala.

**Cambios.** Nuevos `Public/Player/TN_DiveDecisions.h` y `Private/Tests/TN_DiveDecisionsTest.cpp`; modificados `TN_TurtleMovementComponent.cpp`, `TortugaCharacter_Dive.cpp`, `TortugaCharacter_Knockdown.cpp`. Coordinar con Rubén (tocó `Dive.cpp` hoy). Plan Mode: replicación y más de 5 ficheros.

**Tests** (`Tortunabo.Dive.*`). Llano en arena: parada en < 0,8 s. Arena a 25°: > 200 cm/s tras 2 s. Caso negativo: con los parámetros actuales, arena a 30° no acelera. Impacto: pared a 400 → rebote; a 700 → estampado; pendiente → nada; objeto que se aleja → nada.

**Aceptación.** PIE 4P con PktLag 150 y 2 % de pérdida: bola tras el estampado en la misma posición en 4 máquinas (±5 cm); 0 correcciones > 50 cm al iniciar el dive tras la predicción; la caja lanzada no choca con la portadora. Validación del director en el playtest de la tarea 0 [G §4].

### 3.6 Presupuesto de arte

Criterio: reutilizar antes que crear. Las mallas de props se generan en código (`TNBeachProp`, `TN_BeachPropMeshes.h`, color de vértice con `M_CosmeticVertexColor`); el SFX sale de los sintetizadores existentes (`TN_RaceItemSynth`, `TN_ShellImpactSynth`, `TN_BeachSplashSynthComponent`, `TN_BeachTrapSynthComponent`, `TN_AmbientSynthComponent`); el VFX reutiliza `TN_TurtleDustComponent`, `TN_DizzyBirdsComponent` y `TN_BeachFinishSplash`.

| Modo | Asset | Tipo | Reutilización | Prioridad |
|---|---|---|---|---|
| Coop | Terreno de CP01 y demás | Malla | Pipeline volumétrico | 0 |
| Coop | Lámina de agua | Material | `M_GridWater` | 0 |
| Coop | Vista previa en la carga | UI | Render del generador | Deseable (0) |
| Coop | Efecto de rescate | VFX/SFX | Aturdimiento de `TN_BeachStun` y pajaritos | 0 |
| Carrera | Tramos de autor | Mallas | Vocabulario `ETNBeachElement` | 0 |
| Carrera | Mallas propias de tramo [E T8] | Mallas | — | Deseable |
| Rally | Buggy, ruedas, skeleton | Malla/anim. | `SKM_Offroad`, `SM_BuggyBody`, `SM_BuggyTire` (migración) | 0 |
| Rally | Pintura y patrones | Material | **`M_BuggyPaint`** (instancia con parámetros de `M_TurtleBody` y `ETNShellPattern`) | **Imprescindible (1)** |
| Rally | Llantas y decoración | Mallas | `buggy.py` parametrizado | Deseable |
| Rally | Tortuga al volante | Animación | Pose de caparazón en el asiento | Deseable (pose propia) |
| Rally | Motor, derrape, bocina, impactos | SFX | Sintetizador nuevo en C++ y `TN_ShellImpactSynth` | 0 |
| Rally | Polvo, salpicadura, estela | VFX | `TN_TurtleDustComponent` teñido, `TN_BeachFinishSplash` | 0 |
| Rally | Marcas de derrape | Decal | — | Deseable |
| Rally | Arcos de checkpoint | Malla | Arco de neumático de la meta [D §2.1] | 0 |
| Rally | Velocímetro y HUD | UI | HUD de carrera y texto C++ | 0 |
| Rally | Iconos de skins | UI | `render_preview.py` | 0 |
| Rally | 4 ítems | Todos | `TN_RaceItems` existentes | 0 |
| TcT | Cobertura, barricadas, rampas, catapultas | Mallas | Sacos, erizos, castillos, puerta de conchas [D §1.4, §1.9] | 0 |
| TcT | Ráfaga de viento | VFX/SFX | Polvo y `TN_AmbientSynthComponent` | 0 |
| TcT | Puntos, «quién te empujó», flechas | UI | HUD de carrera con caras | 0 |
| TcT | 7 objetos | Iconos | — | **Imprescindible (7)** |
| TcT | Disparo | Animación | La de lanzar | Deseable |
| Tortuga | Estampado | VFX/SFX | Polvo, `DizzyBirds`, `TN_ShellImpactSynth`, aplastado en código | 0 |
| Steam | Icono y splash | Imagen | — | **Imprescindible (2)** [F #9] |
| Steam | Cápsulas de tienda | Imagen (lote) | — | **Imprescindible (1)** |

**Total de assets nuevos imprescindibles: 11** (Rally 1, TcT 7, Steam 3).

## 4. Correcciones transversales, por prioridad

| Pri. | Corrección | Evidencia | h |
|---|---|---|---|
| P0 | Cocinado: `MapsToCook` con `LVL_Lobby`, `LVL_BeachRace` y los niveles nuevos; fuera `DirectoriesToAlwaysCook=/Game/Maps`, mapas de test y visor (cierra N-B) | [F #10], `DefaultGame.ini:17-22` | 3 |
| P0 | `OnTravelFailure` con vuelta al menú (N-E, alta) | [F #5] | 4 |
| P0 | CI: `BuildCookRun` + `Automation RunTests Tortunabo` por merge; `build_check.bat` relativo | [F #11, §4.5] | 12 |
| P0 | Trámite del AppID (Valve tarda semanas) | [F #1] | 4 |
| P1 | Puente tambaleante: `Excitation` del servidor en uint8 a 10 Hz; `Dips` solo visuales. Riesgo alto; afecta a TcT | [B §4], `TN_WobblyBridge.cpp:40-51` | 4 |
| P1 | Noqueo autoritativo (§3.5) | [G §1] | 6–10 |
| P1 | Carrera: la tortuga en caparazón gira como un torbellino y atraviesa el mapa. **Diagnóstico en curso**, sin estimar | Informe aparte | — |
| P1 | Predicción del dive | [G tarea 6] | 6 |
| P2 | Plataforma tambaleante: alabeo y cabeceo del servidor en int8 a 15 Hz (hoy 19–25 cm de desfase) | [B §4] | 3 |
| P2 | Mina: quitar el empujón local | `TN_BeachMine.cpp:522-523, 584-587` | 2 |
| P2 | Boost de ítems en `FTNSavedMove_Turtle` (≈30 cm de corrección) | `TN_RaceItemComponent.cpp:196-211` | 4 |
| P2 | Trampolín: rebote detectado en el CMC o tolerancia tras el rebote | `TN_BeachTrampoline.cpp:640-671` | 4 |
| P2 | Quad: compensación de lag, `FTNTrapClock` visual; retirar `ATN_QuadActor` | [B §4] | 2 |
| P2 | Guardados con versión, migración y escritura asíncrona | [F #4] | 6 |
| P2 | N-A (RPC de depuración en Development), N-D (`ServerGoToStation` sin validar) | [F §3] | 2 (est. propia) |
| P3 | Deck y mando, logros y *rich presence*, créditos y licencias (Noto OFL, audio, fuentes geográficas), fuentes CJK, icono, localización | [F #3, #7–9, #12], [C §1] | 76 (F8) |

## 5. Hoja de ruta

Cada fase = commits independientes, uno por unidad cerrada. El gate bloquea las fases que dependen de ella. Cada iteración de playtest con Steam y build empaquetada suma 16 h [F §4.1].

| Fase | Contenido | Depende de | h | Gate de playtest |
|---|---|---|---|---|
| F0 | Cocinado, `OnTravelFailure`, CI, fuera `Source/Logs`, N-A, dorado T0, trámite AppID | — | 22–26 | Build empaquetada lobby → carrera → menú con 2 máquinas; CI verde con los 87 tests |
| F1 | Terreno preparado (§2): manifest v2, bake, máscara, DA, importador, `SafeGround`, `WalkableZAt`, R8, R9 | F0 | 34–40 | Smoke headless de `LVL_Coop_C01` sin ProcMesh; tests de `IsAllowed`/`WalkableZAt` |
| F2 | Coop Prepared: generador, marcadores, R1–R7, auditoría R10, catálogo y lobby, **CP01** | F1 | 48–58 | PIE 4P completa C01 y CP01; rescates (§3.1) |
| F3 | Tortuga: pendiente, rebote, estampado, predicción del dive, noqueo autoritativo | F0 | 25–35 | Playtest con el director (`TN.Dive.Debug 1`); 4P con NetEmulation |
| F4 | Sincronización [B §4] salvo boost; N-C/N-D; torbellino | F0 | 21–24 + torbellino | `p.NetShowCorrections` 4P en puente, plataforma, trampolín, mina y quad |
| F5a | Carrera T1–T6: P1–P3, assets de tramo/curso, `PlaceAuthored`, red, editor, tests | F0 (T0) | 59–80 | Dorado idéntico; carrera 4P con un tramo fijo |
| F5b | Carrera: P4–P8 y P9 | F5a | 33–46 | Playtest de ritmo y legibilidad |
| F6a | Buggy: port, medición, pawn, muerte y reaparición | F0 | 25–36 | Pista de prueba: `buggy_measure` y 4 buggies en PIE |
| F6b | Mapas geo: `GeoRegion`, atribuciones, ruta y tallado, checkpoints, decimado y streaming, E01B, rendimiento con 8 | F1, F6a | 63–99 | Piloto IA sin atascos; subida del anfitrión bajo el límite |
| F6c | Skins, `buggy.py`, pulido y 4 ítems | F6a | 69–95 | Tienda, probador y carrera completa en E01B |
| F6d | Sonda de correcciones, 30/10 Hz; opción E si hace falta | F6b | 6–36 | Correcciones del conductor medidas y aceptadas |
| F7 | TcT: modo, viento y barricadas, loot por puntos, P01 con marcadores, HUD, 7 objetos | F1, F3, F4 | 68–101 | Ronda a 8; encuentro cada 15–20 s |
| F8 | Cierre Steam: AppID en código, ajustes, localización, Deck, logros, créditos, icono | F0 | 76 | Shipping en Steam real con 4 jugadores y checklist completo |

**Total: 549–752 h** más playtests. Fuera del total: puentes como actores (est. propia 16–24 h, P4).

**Carriles paralelos (ficheros disjuntos).**

| Carril | Ficheros | Fases |
|---|---|---|
| 1 Python terreno | `Scripts/terrain_vol/*`, `Scripts/terrain_geo/*`, `import_terrain_coop.py`, `Scripts/tests/*` | F1, F6b (Python) |
| 2 ProcMap/Coop | `World/ProcMap/*`, `TN_ProcMapGameMode*`, `TN_SafeGroundSubsystem`, `TN_LoadingScreenSubsystem` | F1 (C++), F2, F7 |
| 3 Jugador | `Player/*` y el boost de `TN_RaceItemComponent` (toca `FTNSavedMove_Turtle`) | F3 y esa parte de F4 |
| 4 Playa/Carrera | `TN_BeachLayout.h`, `TN_BeachRaceGenerator*`, `TN_BeachLayoutTest.cpp` | F5a, F5b |
| 5 Mecánicas de red | `TN_WobblyBridge`, `TN_BeachWobblyPlatform`, `TN_BeachTrampoline`, `TN_BeachMine`, `TN_BeachQuadLane` | F4 |
| 6 Vehículos | `Vehicles/*`, física en `DefaultEngine.ini`, cosméticos del buggy | F6a, F6c |

Hay que serializar `TN_BeachGullZone`/`TN_BeachStun` (carriles 2 y 5), `MP_GameInstance` y `TN_PauseMenuWidget` en F8 [F §2] y los `.umap` que toca Alvaro2rh. Los agentes no compilan ni hacen commit; un integrador compila en DebugGame.

## 6. Riesgos

| Riesgo | Prob. | Impacto | Mitigación |
|---|---|---|---|
| La malla volumétrica del Rally no escala (48 M tri en 16 km² [C §3]) | Alta | Alto | §2.5; lado ≤ 2,4 km; medir en F6b antes de Europa o Mundo |
| Chaos Vehicles sobre trimesh en vez de heightfield | Media | Alto | Colisión simple decimada; `buggy_measure` en E01B |
| Subida del anfitrión con 8 buggies (280 KB/s) | Alta | Alto | 30/10 Hz; opción E [B §3] |
| La física asíncrona rompe caparazón, ragdoll, catapulta o trampolín | Media | Alto | Síncrona con subpasos [B §1] |
| Puente tambaleante desincronizado en TcT | Alta | Alto | P1 en F4 antes de F7 |
| El noqueo con ragdoll local descuadra la posición | Alta | Medio | Congelado autoritativo (§3.5) |
| Torbellino del caparazón en la carrera (diagnóstico en curso) | Confirmado | Alto | Tarea P1; bloquea el gate de F4 |
| R10: mecánicas inertes fuera de la playa | Media | Medio | Auditoría en F2 |
| R9: colisión de la reserva Nanite | Baja | Alto | Traza SM contra TNTM2 en F1 |
| Fronteras en disputa en mapas continentales | Media | Alto (Steam) | Costa en vez de fronteras [C §2] (P6) |
| MDE sin atribución | Alta si se ignora | Alto | Créditos en F6b/F8 [C §1] |
| Conflictos con Rubén (`Dive.cpp`) y Alvaro (`.umap`) | Media | Medio | Coordinación; subnivel `_Design` [A §3] |
| Más deslizamiento = más muertes por borde y atajos | Media | Bajo | Ajuste en el playtest de F3 [G §4] |
| Tres GameModes vivos multiplican la superficie de red [F N-G] | Alta | Medio | P1 |

## 7. Respuestas del director (2026-09-29)

1. **Coop y Run.** `LVL_Demo01` era una demo. `TN_RunGameMode` y lo marcado como obsoleto dejan de estar disponibles si no aportan: el Coop unifica sobre `ATN_ProcMapGameMode` (Prepared); de `TN_RunGameMode` solo se rescata lo que sirva (p. ej. `ReviveImmunitySeconds`) antes de retirarlo. Desbloquea F2.
2. **Plazas del buggy: biplaza.** Conductora delante y una **segunda tortuga detrás, la artillera**, que lanza los ítems (y otras acciones propias por diseñar: algo original y divertido). Se aceptan las 12–16 h de asientos [B §6 #10]. Si la tortuga va sola, conduce y lanza ella. Desbloquea F6a y F6c.
3. **E01B a criterio del plan.** Partidas muy rápidas: el trazado se dimensiona a partir de la velocidad real del buggy (`UHYBuggyData`) para una carrera corta; se permiten puentes, túneles y cualquier elemento que la haga más divertida. Punto a punto o circuito, lo que salga mejor del análisis. Desbloquea F6b.
4. **Puentes: los dos tipos.** Los de Mokius (asset con anclajes de inicio y fin, los más vistosos) y los naturales del director (tallados en el propio terreno, válidos para muchos mapas). P01 y el Rally pueden mezclar ambos; `ATN_BridgeSpan` se hace sobre el puente de Mokius.
5. **Agarre en TcT: confirmado.** Solo se agarra a quien está en caparazón. El empujón de la bola derriba solo por encima de `MinKnockdownSpeed`.
6. **Borde de los mapas continentales: ambas.** Costa real y fronteras políticas, según el mapa y lo que dé mejor juego.
7. **Estampado y pistolas: confirmado.** El estampado contra la pared usa la bola física replicada (`StartBody`). La «pistola de ragdoll» es una **pistola de noqueo**: el proyectil aplica `ApplyKnockdown` en el servidor a la tortuga alcanzada; no lanza ragdolls. El ragdoll, si se ve, es solo cosmético. Criterio del director: lo que mejor optimice y mejor quede.
8. **Assets low poly propios.** El director pide que se creen assets low poly con el estilo del resto del juego cuando falten (primero el buggy biplaza), por encargo al agente de arte con criterio medible y lámina de revisión, sin sustituir al equipo de arte en los assets finales.
9. **2 vs 2 se mantiene como quinto modo** (volumétrico, mapas de camino fijos). Enfoque: puzzles por parejas en los que los dos compañeros se ayudan, con bifurcaciones más grandes pero en menor número que en Coop, y carrera entre los dos equipos con la opción de sabotear al rival. Diferencia con Coop: en Coop hay enemigos y puzzles en los que coopera todo el grupo; en 2 vs 2 los puzzles son para 2 y el objetivo es llegar antes que la otra pareja. Necesita diseño propio (puzzles de pareja y mecánicas de sabotaje) en un plan posterior.
