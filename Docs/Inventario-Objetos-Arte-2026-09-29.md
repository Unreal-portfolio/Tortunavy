# Inventario de objetos y presupuesto de arte

Fecha: 2026-09-29 · Base: `main` 41a0ee8a5 · Alcance: `Content/` (491 `.uasset`/`.umap`), `Source/Tortunabo` (327 cabeceras) y los assets del buggy en `HellYeah/`. Análisis de solo lectura: nombres, tamaños en disco y `grep` de rutas `/Game` y `/Engine` dentro de los paquetes; no se ha abierto ningún binario en el editor.
Complementa la §3.6 de `Docs/2026-09-29-Plan-Maestro-Modos-y-Mapas.md` (presupuesto de arte por modo); no la repite, la cuadra en la §5.

## 0. Resumen

**Regla de recuento.** Un objeto es una entrada distinta del catálogo: un valor de `ETNBeachElement` o `ETNRaceItem`, una clase de actor o un asset. Las variantes por semilla, escala o tinte no cuentan aparte. La fauna ambiental (31 especies) y la flora (34 formas) cuentan como una familia paramétrica cada una. El terreno y los mapas van aparte (§3) y no entran en los totales. Queda fuera lo que solo usan modos retirados (§4).

**Estados.** A = asset final de arte. B = placeholder, desglosado en tres subtipos:
- B: forma básica del motor, contenido de editor o greybox.
- B-proc: malla o sonido generado en C++ en tiempo de ejecución.
- B-script: asset generado por un script de Python, reproducible, pendiente de revisión visual.

C = no existe y lo pide el plan.

| | Objetos |
|---|---|
| **En juego (4 modos, HQ y menú)** | **236** |
| A: final | 34 |
| B: placeholder | 181 (33 B, 136 B-proc, 12 B-script) |
| C: por crear | 21 |
| P0 (bloquea Steam) | 56 |
| P1 | 32 |
| P2 | 123 |
| Sin acción | 25 |

**Encargos mínimos al arte para Steam (P0), tras reutilizar: 5.**
1. Key art ilustrada. De ella salen las 5 cápsulas de la tienda y el splash.
2. Logotipo. De él sale el icono.
3. Plantilla de icono «pegatina». Los 7 iconos de TcT se renderizan con `render_preview.py` sobre las mallas que ya existen.
4. `M_BuggyPaint`: material de tech-art con los parámetros de `M_TurtleBody`.
5. Kit modular del HQ: 9 piezas más las porterías, con un atlas de paleta.

El plan cuenta 11 assets imprescindibles (§3.6). Aquí salen 5 encargos, que cubren esos 11 entregables y añaden el kit del HQ, que el plan no presupuesta.

Los otros 35 objetos P0 no son encargos de arte:
- 12 sustituciones de placeholders por mallas o sintetizadores que ya existen en código.
- 22 registros de licencia de audio.
- 1 renombrado obligatorio (§2.1, fila 4).

**Encargos P1 (3):**
- Buggy estilizado propio.
- Gaviota y pelícano como SkeletalMesh única para las 4 implementaciones actuales.
- Tema visual de la UI.

**Encargos P2 (7, solo si se decide sustituir la malla generada en código):**
- 3 kits de decorado: playa y basura, restos y mobiliario, militar.
- 1 kit de trampas y estructuras.
- 1 kit de fauna hostil.
- 1 kit de flora tropical.
- 1 decal de derrape.

**Hallazgos que requieren acción inmediata:**
- **Nombre ofensivo**: `Content/Blueprints/Characters/SKM_MERGED_TORTUGANIGGER_C_1` (con su SK, SM y `_Physics`) contiene un insulto racista y sigue referenciado por `BP_TortugaCharacter`. Se cocina (`DirectoriesToAlwaysCook=/Game/Blueprints`) y el nombre aparece en el listado del `.pak`.
- **Contenido de editor y de depuración del motor en 16 assets de juego**: `/Engine/EditorMeshes`, `/Engine/VREditor`, `/Engine/EditorSounds`, `/Engine/EngineDebugMaterials`, `/Engine/EditorMaterials` y `UE4_Logo` de Vive. Aspecto de depuración y riesgo en el cocinado. Detalle en la §6.3.
- **No hay ningún sistema Niagara** en el proyecto. Todo el VFX se genera en código.
- **No hay texturas de más de 2K.** La mayor es `T_TerrainFloorN` (normal de 1024, 2,6 MB en editor).

## 1. Criterio de optimización

1. **Reutilizar antes que encargar.** Cada placeholder P0 se resuelve primero con una malla que ya existe: la tortuga con casco y tinte, las mallas de `TNBeachProp` y `TNFaunaBuild*`, o los sintetizadores.
2. **Un material maestro y un atlas de paleta.** Todo el decorado, las trampas, los enemigos y los objetos de código usan color de vértice. Si pasan a arte:
   - Un `M_PropPalette` con un atlas de paleta de 256×256 (UV a celdas de color).
   - Sin texturas propias por asset.
   - Hoy 12 ficheros C++ usan `/Engine/EngineDebugMaterials/VertexColorMaterial`, un material de depuración. Hay que unificarlos en `M_CosmeticVertexColor`.
3. **Kits modulares.** Fortalezas, castillo del HQ, vallas, pasarela y caminito ya se montan por módulos en código (`TN_BeachFortressKit.h`, `TN_CastleKit.h`, `BuildBoardwalkModule`). El arte debe entregar módulos con el mismo paso de rejilla, no piezas únicas.
4. **Una malla con variaciones de escala y tinte.** Ya se aplica en `ATN_BeachDecor`, con la caché por `(Element, Variant)` en `TN_BeachDecor.cpp:145` y `SizeScale` de 0,7 a 1,4. Se mantiene: como mucho 3 variantes de malla por elemento.
5. **ISM para lo repetido.** `ATN_BeachDecorField` ya agrupa en ISM por lote, con distancias de corte y gemelos de sombra (`TN_BeachDecorField.cpp:369-390`). Cualquier sustituto de arte tiene que entrar por esa misma receta (`FRecipe`), no como actor suelto.
6. **LODs y Nanite:**
   - Nanite solo en el terreno, que ya lo usa (`TN_TerrainMeshAsset.cpp:130`).
   - En props de menos de 5 k triángulos, Nanite no: ISM y distancia de corte.
   - LODs solo en SkeletalMesh (tortuga, enemigos, buggy) y en props de más de 3 k triángulos.
7. **Presupuestos por tipo** (LOD0):

| Tipo | Triángulos | Texturas |
|---|---|---|
| Decorado pequeño (basura, conchas) | 100–400 | Atlas de paleta compartido |
| Decorado mediano (silla, sombrilla, castillo pequeño) | 400–1 500 | Atlas de paleta |
| Decorado grande (castillo enorme, restos de vela, tronco) | 1 500–4 000 | Atlas de paleta; LOD1 al 50 % |
| Trampa o interactuable | 1 000–4 000 | Atlas de paleta |
| Módulo de fortaleza o del kit del HQ | 500–2 000 por módulo | Atlas de paleta más 1 textura de arena de 512 compartida |
| Enemigo (SkeletalMesh, ≤ 20 huesos) | 1 500–5 000 | Atlas de paleta |
| Tortuga jugadora | 8 000–12 000 | Parámetros de `M_TurtleBody`, 1K como máximo |
| Casco u objeto en la mano | 300–1 200 | Color de vértice |
| Buggy | 6 000–10 000 el chasis y 800 cada rueda | `M_BuggyPaint` por parámetros, 1K como máximo |
| Icono | — | 256×256 de origen, 128×128 en el HUD |

## 2. Inventario

Columnas: objeto | Nº | tipo | modos | estado | ruta o clase | acción de arte | prioridad | presupuesto.
Modos: Co = Coop, Ca = Carrera, Ra = Rally, TcT = Todos contra Todos, HQ = lobby. Rutas de `Content/` relativas a `/Game`; las clases, a `Source/Tortunabo`.

### 2.1 Personajes y enemigos (29)

| Objeto | Nº | Tipo | Modos | Estado | Ruta o clase | Acción de arte | Prio | Presupuesto |
|---|---|---|---|---|---|---|---|---|
| Tortuga jugadora | 1 | Personaje | Todos | A | `Meshses/Characters/Player/TotugaDemo_Rig` (988 KB, Álvaro, 22-09), `Cosmetics/Materials/M_TurtleBody` | Comprobar LODs en el editor (no se ven sin abrir el binario). Al mover la carpeta `Meshses`, renombrar a `SK_Turtle` | P1 | 8–12 k tris; LOD1 50 %, LOD2 20 % |
| Animaciones Mixamo (Drunk_Run_Forward, Old_Man_Idle, Salute, Walking, Yelling) | 5 | Animación | HQ (PNJ), menú | B | `Animations/Character/TortugaDemo/Anim` | La locomoción es procedural (`UTN_TurtleAnimInstance`). Sustituir las 3 que usa el código (`Old_Man_Idle` ×8, `Salute` ×25, `Yelling` ×4) por poses propias, o recortarlas | P2 | ACL; ≤ 3 s. Hoy `Old_Man_Idle` pesa 905 KB y `Yelling` 758 KB |
| Emotes (9 ranuras) | 1 | Animación | Todos | B-proc | `UTN_TurtleAnimInstance`, `PlayWheelEmoteMontage` (`TortugaCharacter.h:802`) | Ninguna | — | — |
| Malla heredada `SKM_MERGED_TORTUGA…_C_1` (+SK, SM, `_Physics`) | 1 | Personaje | Referenciada por `BP_TortugaCharacter` | B | `Blueprints/Characters/SKM_MERGED_*` (251 KB) | **Renombrar o borrar**: el nombre es un insulto racista. Pasar la física a `TotugaDemo_Rig_PhysicsAsset` | **P0** | — |
| Enemigos de la Carrera: GiantCrab, SeaUrchin, Lizard, QuadLane (quad y bañista), HermitCrab, PoolOctopus, SandFleas, ToyTank, SandWorm | 9 | Enemigo | Ca, Co (marcadores), TcT | B-proc | `ATN_Beach*`; `Private/World/Beach/TN_BeachEnemyMeshes.h`, `TN_BeachCritterMeshes.h`, `TN_BeachSandWormMeshes.h` | Mantener. Si se sustituyen, un solo encargo: «kit de fauna hostil» | P2 | 1,5–5 k tris, ≤ 20 huesos |
| Gaviota y pelícano (GullZone, RaceGullStrike, RacePelicanTaxi) | 2 | Enemigo | Ca, Co, TcT | B-proc | `ATN_BeachGullZone`, `TNFaunaBuildBird` | **Una SkeletalMesh de ave** con aleteo, que sirva también para `BP_EnemySeagull`, `BP_SeagullActor` y la gaviota de `ATN_ProcFauna`: 4 implementaciones, 1 malla | P1 | 1,5 k tris, 8 huesos |
| Cangrejo `BP_CrabActor` | 1 | Enemigo | Co | A | `Blueprints/Characters/Meshes/SKM_MiniCangrejo` (Mokius) | Quitar `LevelColorationUnlitMaterial`, un material de depuración | P1 | — |
| Gaviota `BP_EnemySeagull` | 1 | Enemigo | Co | B | `Blueprints/Gameplay/Enemies/Seagull` (Cone del motor) | Reutilizar la gaviota procedural o la de la fila del ave: 0 arte | **P0** | — |
| Quad `BP_QuadActor` | 1 | Enemigo | Co | B | `Blueprints/Gameplay/Enemies/Quad` (Cube + `MI_Grid_*`) | Reutilizar `BuildQuadBody` y `BuildQuadWheel`: 0 arte | **P0** | — |
| Caca de gaviota (decal) | 1 | Peligro | Co | B-proc | `ProcMap/Materials/M_PoopSplatDecal` (`create_poop_decal.py`) | Ninguna | — | — |
| Fauna ambiental (31 especies) | 1 | Decorado vivo | Co, HQ, Ca | B-proc | `ATN_ProcFauna`, `TN_ProcMapFaunaMeshes.h` | Ninguna. Si se hace arte, 9 cuerpos base (`ETNFaunaBody`) con tinte, no 31 | P2 | 300–1 500 tris |
| PNJ tendero y general | 2 | Personaje | HQ | B | `ATN_ShopKeeper`, `ATN_GeneralBriefing` (BasicShapes + `Old_Man_Idle`) | Reutilizar `TotugaDemo_Rig` con casco (`SM_Helmet_Captain` o `_Sailor`) y tinte de `DT_Skins`: 0 arte | **P0** | — |
| Fantasma espectador y huevo fantasma | 2 | Personaje | Todos | B-proc | `ATN_SpectatorGhost`, `ATN_GhostEgg`, `TN_GhostMeshes.h` | Ninguna | — | — |
| Tortuga al volante | 1 | Animación | Ra | C | — | Pose de caparazón en el asiento (plan §3.6, «Deseable») | P2 | 1 pose |

### 2.2 Decoración (66)

| Objeto | Nº | Tipo | Modos | Estado | Ruta o clase | Acción de arte | Prio | Presupuesto |
|---|---|---|---|---|---|---|---|---|
| Playa y basura: Coconut, StrandedJellyfish, SixPackRings, RedBra, Clam, DecorShell, Starfish, PlasticCup, Bottle, Lollipop, WatermelonRind, Straw, SodaCan, BottleCaps, FlipFlop, JuiceBox, BeachTowel, SunscreenBottle, PopsicleSticks, SnackShells, RopePiece, Sunglasses, ToyBucket, BeachBall, Frisbee, Cuttlebone, RubberDuck, GullFeather | 28 | Decoración (se puede rebuscar) | Ca, Co, TcT | B-proc | `ATN_BeachDecor` y `TNBeachProp::Build*` (`Private/World/Beach/TN_BeachPropMeshes.h`, 3 712 líneas) | Mantener. Encargo P2 «kit playa»: 28 SM con 1 atlas; ≤ 3 variantes por elemento | P2 | 100–400 tris |
| Restos y naturales: Rock, RockCluster, ShipSailWreck, MossyLog, OldPlanks, FishingNet, Driftwood, Buoy | 8 | Decoración | Ca, Co, TcT | B-proc | Ídem | Encargo P2 «restos y mobiliario» | P2 | 400–4 000 tris; LOD1 en los de más de 3 k |
| Mobiliario: PlantedUmbrella, BeachChair, SandCastleSmall, SandCastleHuge | 4 | Decoración | Ca, Co, TcT | B-proc | Ídem | Ídem | P2 | 400–4 000 tris |
| Recorribles: Boardwalk, WoodenPostPath | 2 | Decoración modular | Ca, Co | B-proc | `BuildBoardwalkModule`, `BuildPathPost`, `BuildPathRope` | Módulo con el paso del código (`BuildTiledPiece`) | P2 | 300–800 tris por módulo |
| Militar (Tortunavy): Sandbags, AmmoCrate, TankTrap, MilitaryHelmet, CamoNet, Jerrycan, ToySoldiers | 7 | Decoración y cobertura de TcT | Ca, Co, TcT | B-proc | `TN_BeachMilitaryMeshes.h` | Encargo P2 «kit militar». Sacos y erizos son las barricadas de TcT | P2 | 200–1 500 tris |
| Escenografía de la Carrera: árbol colosal de salida, cartel «¡A LA META!», arco de neumático de meta, boyas y banderolas | 4 | Decoración | Ca (el arco también en Ra) | B-proc | `TN_BeachRaceGenerator_Scenery.cpp`, `TN_ProcMapFinishMeshes.h` | Ninguna. El arco se reutiliza como checkpoint del Rally | P2 | Árbol: ≤ 8 k tris y LOD |
| Flora (34 formas; en arena se usa el subconjunto tropical) | 1 | Decoración | Ca (selva de los bordes), Co (`vegetation` del DataAsset) | B-proc | `TN_ProcMapFlora.h` (`EFloraShape`), `TN_ProcMapFloraMeshes.h`, `M_ProcFoliage` | Si se hace arte: 8 SM tropicales (Palm, FanPalm, Bush, Grass, Reeds, SeaGrape, Pandanus, Fern) con viento por vértice, en ISM. Las formas de desierto y volcán no hacen falta | P2 | Árbol 1,5–3 k tris más LOD1; matorral 200–600 |
| Kit greybox del HQ: Floor10x10, Tower, FenceDoor_3x4, Fence_1x3, Fence_2x4, Fence_3x4, Fence_4x1, Fence_4x4, BP_Net | 9 | Decoración modular | HQ, menú | B | `Blueprints/Builder/*` sobre `Maps/Lobby/_GENERATED/Alvaro/Box_*` y `Boolean_*` (mallas de Modeling Mode) con `MI_Grid_*` | **Encargo P0 «kit HQ»**: 9 módulos con un atlas, con la misma rejilla (1×2, 1×3, 2×4, 3×4, 4×4 m). **Coordinar con Álvaro, que es el dueño de estos assets** | **P0** | 500–2 000 tris por módulo |
| Valle y castillo del HQ | 2 | Decoración | HQ | B-proc | `ATN_LobbyValley`, `ATN_SandCastleLobby`, `TN_CastleKit.h` | Ninguna; comparten módulos con las fortalezas | P2 | — |
| Agua | 1 | Decoración | Todos | A | `Environment/Water/SM_WaterSurface` (534 KB), `M_TortunaboWater`, `M_TortunaboWaterToon`, `T_WaterNormal` (1024) y `T_WaterFoam` (`build_water.py`) | Sacar `DA_WaterSurface` (6,2 MB, intermedio del importador) del cocinado | P1 | — |

### 2.3 Interactuables (36)

| Objeto | Nº | Tipo | Modos | Estado | Ruta o clase | Acción de arte | Prio | Presupuesto |
|---|---|---|---|---|---|---|---|---|
| Trampas: BarbedWire, Seaweed, WobblyPlatform, BrokenBucket, SpadeRamp, ShellGate, ClamTrap, MovingPlatform, Mine | 9 | Interactuable | Ca, Co, TcT | B-proc | `ATN_Beach*`, `TN_BeachTrapKit.h` | Encargo P2 «kit trampas» | P2 | 1–4 k tris |
| Lanzadores: Catapult, Trampoline | 2 | Interactuable | Ca, Co, TcT | B-proc | `ATN_BeachCatapult`, `ATN_BeachTrampoline` | Ídem | P2 | 1–3 k tris |
| Estructuras escalables: SandDungeon, FortressMedium, FortressLarge, FortressColossal | 4 | Interactuable modular | Ca, Co | B-proc | `ATN_BeachFortress`, `TN_BeachFortressKit.h`, `ATN_BeachSandDungeon` | Kit modular (rampa, escalera, torre, adarve, puerta, patio), compartido con el castillo del HQ | P2 | 500–2 000 tris por módulo |
| Botín: cofre, montículos de rebusca, caja de objetos | 3 | Interactuable | Ca, Co, TcT, Ra (caja) | B-proc | `ATN_BeachChest`, `ATN_BeachSearchSpot`, `ATN_RaceItemBox` | Ninguna | P2 | 1–2 k tris |
| Tormenta | 1 | Peligro | Ca | B-proc | `ATN_BeachStorm`, `M_ProcStormVeil` | Ninguna | — | — |
| Nido de huevo (reaparición) | 1 | Interactuable | Co, TcT | B | `ATN_ProcEggNest` (BasicShapes) | Reutilizar la malla del huevo fantasma o `BP_TurtleEgg`: 0 arte | **P0** | — |
| Probador y selector de modo | 2 | Interactuable | HQ | B | `ATN_ChangingBooth`, `ATN_ProcModeSelector` (BasicShapes) | Montarlos con el kit HQ (tienda de lona, mesa) | P1 | — |
| Cofre del HQ | 1 | Interactuable | HQ | B-proc | `ATN_TreasureChest` | Ninguna (misma receta que `ATN_BeachChest`) | P2 | — |
| Tutorial: muñeco, rebusca, catapulta | 3 | Interactuable | HQ | B-proc | `ATN_TutorialDummy`, `ATN_TutorialSearchSpot`, `ATN_TutorialCatapult` | Ninguna | P2 | — |
| Parque del HQ: JellyfishTrampoline, PlaygroundPiece, WobblyBridge | 3 | Interactuable | HQ | B-proc | `Lobby/Playground/*`, `TN_PlaygroundMeshKit.h` | Ninguna | P2 | — |
| Estatuas de cascos y de skins | 2 | Interactuable | HQ | B | `Blueprints/Gameplay/Cosmetics/BP_HatStatue` y `BP_SkinStatue` (Cube + 3 materiales de depuración) | Tortuga en pose fija sobre peana del kit HQ: 0 arte | **P0** | — |
| Piel de plátano | 1 | Peligro | HQ | A | `BP_BananaPeel` → `Blueprints/Characters/Meshes/Plantano` | Quitar `/Engine/EngineMaterials/CubeMaterial` | — | — |
| Objeto físico que rebota | 1 | Interactuable | HQ | B | `BP_BouncePhysicsObject` (`EngineMeshes/Sphere` + `M_ColorGrid` de calibración) | Reutilizar `BuildBeachBall` | P1 | — |
| Porterías | 1 | Interactuable | HQ | B | `TN_GoalZone`, `_GENERATED/Alvaro/Goal_3x1` | Dentro del encargo del kit HQ | **P0** | 800 tris |
| Arcos de checkpoint | 1 | Interactuable | Ra | C | — | Reutilizar el arco de neumático de la meta: 0 arte | P1 | — |
| Puente que se corta | 1 | Interactuable | TcT | C | `ATN_BridgeSpan` (pregunta P4 del plan) | Reutilizar `ATN_WobblyBridge` | P2 | — |

### 2.4 Ítems y pickups (40)

| Objeto | Nº | Tipo | Modos | Estado | Ruta o clase | Acción de arte | Prio | Presupuesto |
|---|---|---|---|---|---|---|---|---|
| Objetos de carrera: Box, Coconut, TripleCoconut, GoldenCoconut, PelicanTaxi, Sunscreen, HomingCrab, GullStrike, SandMine, StormCloud, Frisbee, Whistle | 12 | Ítem | Ca, TcT (`RollLoot`), Ra (4) | B-proc | `ETNRaceItem`, `TN_RaceItemArt.h` (malla e icono de 128² en código) | Ninguna. Los 4 del Rally (Coconut, SandMine, HomingCrab, Sunscreen) no necesitan arte nuevo | P2 | 300–1 200 tris |
| Conchas de puntos | 1 | Pickup | Co, Ca | B-proc | `ATN_ScoreShells`, `BP_ScorePickup` (referencia a `/Engine/EditorMeshes/EditorHelp`) | Quitar la malla de editor: 0 arte | **P0** | — |
| Concha (abierta y cerrada) | 1 | Pickup | Co | A | `BP_ConchPickUp` → `ConchaAbierta`, `ConchaCerrada` | Cambiar 2 sonidos de editor (`EditorSounds`, `VREditor`) por un sintetizador | P1 | — |
| Piedra lanzable | 1 | Ítem | Co, TcT | A | `BP_GenericThrowable` → `Piedra1` | Ninguna (el plan la reutiliza en TcT) | — | — |
| Tinta de calamar | 1 | Proyectil | Co, TcT | A | `BP_InkProjectile` → `Calamar` | Ninguna | — | — |
| Peluche (tótem) | 1 | Ítem | Co | A | `BP_TotemInteractable` → `Peluche1` | Ninguna | — | — |
| Medusa | 1 | Ítem | Co, TcT (dardo) | B | `BP_JellyfishActor` (Cylinder + `EditorMeshes/ArcadeEditorSphere`) | Reutilizar `TNBeachProp::BuildJellyfish`: 0 arte | **P0** | — |
| Pickup de rescate | 1 | Pickup | Co | B | `BP_RescuePickUp` (Cube + `TranslucentLaserPointerMaterialInst`) | Reutilizar el huevo fantasma o la concha: 0 arte | **P0** | — |
| Pickup genérico | 1 | Pickup | Co | B | `BP_GenericPickup` (`EngineMeshes/Sphere`) | Reutilizar una malla de objeto de carrera | P1 | — |
| Cascos: Captain, Crab, Crown, Halo, Hibiscus, Jelly, Party, Pirate, Propeller, Sailor, Starfish, Straw | 12 | Cosmético | Todos | B-script | `Cosmetics/Helmets/SM_Helmet_*` (20–47 KB), `Scripts/cosmetics_meshes.py` | Revisión visual de un humano. Si pasan, se promueven a A; si no, encargo de retoque sobre el mismo script | P1 | 300–1 000 tris, color de vértice |
| Skins de tortuga | 1 | Cosmético | Todos | A | `DT_Skins` + `M_TurtleBody` (`ETNShellPattern`) | Ninguna: N skins = N filas | — | — |
| Objetos de TcT: piedra, balón, trabuco de aire (la «pistola de ragdoll»), dardo de medusa, pistola de tinta, garfio, pala | 7 | Ítem | TcT | C | Plan §3.4. Mallas reutilizadas: `Piedra1`, `BuildBeachBall`, `BuildJellyfish`, `Calamar` y la receta de `TN_BeachSpadeRamp`; el trabuco y el ancla del garfio, procedurales | 7 iconos con 1 encargo de plantilla más `render_preview.py` | **P0** | Icono de 256² |

### 2.5 Vehículo (3)

| Objeto | Nº | Tipo | Modos | Estado | Ruta o clase | Acción de arte | Prio | Presupuesto |
|---|---|---|---|---|---|---|---|---|
| Buggy | 1 | Vehículo | Ra | B | En HellYeah: `Content/Vehicles/OffroadCar/SKM_Offroad` (8,7 MB, plantilla de vehículo de Epic), `Offroad_CtrlRig` (1,46 MB), `SM_Offroad_Body` (2,77 MB), `SM_Offroad_Tire` (0,85 MB); `Tools/Blender/props/buggy.py` | Sirve para el MVP. **Encargo P1 «buggy estilizado»** con los mismos nombres de hueso, para cambiarlo sin tocar el AnimBP. Hoy desentona con el estilo de color de vértice | P1 | 6–10 k tris el chasis y 800 cada rueda; LOD1 y LOD2 |
| Pintura y patrones (`M_BuggyPaint`, `DT_BuggySkins`) | 1 | Material | Ra | C | Plan §3.3 | Encargo de tech-art: instancia con los parámetros de `M_TurtleBody`. N skins = N MI, 0 mallas | **P0** | 0 texturas nuevas |
| Llantas y decoración | 1 | Malla | Ra | C | `buggy.py` parametrizado | Variantes por script | P2 | 300–800 tris |

### 2.6 UI, VFX y SFX (62)

| Objeto | Nº | Tipo | Modos | Estado | Ruta o clase | Acción de arte | Prio | Presupuesto |
|---|---|---|---|---|---|---|---|---|
| UI de juego (8 WBP y widgets en C++) | 1 | UI | Todos | B | `Blueprints/Gameplay/Widgets/WBP_*`, `UI/HUD/M_UI_RadialWheel`, `M_UI_TurtleBadge`, `UI/Shop/M_UI_Preview`; `WBP_PlayerHUDWidget` usa `/Engine/EditorMaterials/Cross` | Encargo P1 «tema de UI»: marcos, botones y 9-slice en un atlas | P1 | Atlas de 1024² |
| Iconos de `DT_Items` | 1 | UI | Co | B | `DT_Items` (`EngineResources/AICON`, `VREditor/Devices/Vive/UE4_Logo`, `VREditor/BasicMeshes/SM_Ball_01`) | Renderizarlos con `render_preview.py`: 0 arte | **P0** | 256² |
| Logo y fondo del menú | 2 | UI | Menú | A | `Textures/Images/RimaLogo` (315 KB), `T_Background` (205 KB) | Confirmar la autoría y registrarla en el manifiesto | P1 | — |
| Icono y splash de Steam | 2 | Imagen | Steam | C | `Build/Windows` no existe [F #9] | Derivar del logotipo (encargo 2) | **P0** | `.ico` de 256 px y splash de 1920×1080 |
| Cápsulas de la tienda | 1 | Imagen (lote) | Steam | C | — | Derivar de la key art (encargo 1) | **P0** | 5 tamaños de Steamworks |
| Vista previa del mapa en la carga | 1 | UI | Co | C | Plan §3.6 | Render del generador: 0 arte | P2 | 1024×576 |
| VFX en código: TurtleDust, DizzyBirds, ShellImpactFX, FinishSplash, RaceBurstFX, PathStormFX, ProcMapAmbientFX, PickupGlow, ScoreShellBurst, StormVeil | 10 | VFX | Todos | B-proc | `UTN_*Component`, `ATN_RaceBurstFX`, `ProcMap/Materials/M_ProcFX*` | Ninguna. No hay Niagara en el proyecto; se mantiene | P2 | — |
| VFX nuevos por reutilización: ráfaga de viento (TcT), estampado (§3.5), polvo del buggy | 3 | VFX | TcT, Ra | C | Polvo teñido, `DizzyBirds`, `TN_BeachFinishSplash` | 0 arte | P1 | — |
| Marcas de derrape | 1 | Decal | Ra | C | — | Decal de tech-art, 1 textura de 256×1024 | P2 | 1 textura |
| SFX sintetizados (15 componentes) | 15 | SFX | Todos | B-proc | `TN_RaceItemSynth`, `TN_ShellImpactSynth`, `TN_BeachSplashSynthComponent`, `TN_BeachTrapSynthComponent`, `TN_AmbientSynthComponent`, `TN_MusicSynthComponent`, `TN_RaceMusicComponent`, `TN_ScoreShellSynthComponent`, `TN_BeachCritterSynth`, `TN_BeachEnemySynth`, `TN_BeachMineSynth`, `TN_BeachSandWormSynth`, `TN_PlaygroundSynthComponent`, `UTN_SearchSynthComponent`, `TN_TurtleFoleyComponent` | Ninguna (diseño del plan) | — | — |
| Música de emotes (`DanceSounds/0`–`9`) | 10 | SFX | Todos | A | `Audio/DanceSounds` (15,3 MB; `7` = 3,5 MB y `9` = 4,1 MB; Mokius, 19-03) | **Registrar la fuente y la licencia**. Recortar a bucles de 30 s como máximo | **P0** | ≤ 30 s |
| Música del lobby | 1 | SFX | HQ, menú | A | `Audio/Music/TotugasLobbyMusica` (4,6 MB, MiguelilloElPillo) | Registrar la autoría y la cesión | P1 | — |
| SFX en WAV: FootstepsMiniPack (5 DirtRoad + SandAudio), Consume, Kill, Pickup, Throw 1–3 (+`SC_Throw`) | 12 | SFX | Co, HQ | A | `Audio/EffectSounds/*` | **Registrar la licencia del pack de pasos** | **P0** | — |
| Sonidos de editor en BPs | 1 | SFX | Co, HQ | B | `EditorSounds/GamePreview/*`, `VREditor/Sounds/UI/*` y `EngineSounds/1kSineTonePing` en `BP_ConchPickUp`, `BP_PressurePlate`, `BP_JellyfishActor` y `DT_Items` | Cambiarlos por sintetizadores: 0 arte | **P0** | — |
| Motor, derrape y bocina del buggy | 1 | SFX | Ra | C | Sintetizador nuevo en C++ (plan §3.6) | 0 arte | P1 | — |

## 3. Terreno y mapas (fuera de los totales)

Los genera el pipeline volumétrico (`Scripts/gen_terrain_*.py` y `import_terrain_mesh.py`): estado A por diseño, 0 encargos. Cifras de triángulos sumadas desde `manifest.json`:

| Mapa | Triángulos | Disco en editor | Observación |
|---|---|---|---|
| Mapa01 | 1,78 M | 31,4 MB en SM (36) + **41,5 MB en `DA_M_Mapa01_*` (36)** + 3,0 MB en presets | No está entre los candidatos del plan (C01, P01, E01, F01, CP01) y usa `BP_GridDemoGameMode`. Los DataAssets repiten la geometría de las SM (`TN_TerrainMeshAsset.h:38-50`, `UPROPERTY` sin `EditorOnly`) |
| C01 camino | 1,06 M | Pendiente de importar | Referencia de CP01 (≤ 1,1 M) |
| E01 España | 1,08 M | Pendiente | E01B conducible sigue §2.5 del plan (1 M tri/km²) |
| F01 retrato | 0,98 M | Pendiente | — |
| P01 plataformas | 0,40 M | Pendiente | TcT |

Nanite está activo en el terreno, con `KeepPercentTriangles=1` y un fallback del 10 %, y la colisión es `CTF_UseComplexAsSimple`. La reducción la trata el plan (§2.5); no se duplica aquí.

## 4. Fuera de alcance (modos retirados o legado)

No cuentan en los totales. Hay que decidir si se cocinan, porque `DefaultGame.ini:22,26` tiene `DirectoriesToAlwaysCook` sobre `/Game/Maps` y `/Game/Blueprints`, y eso mete todo lo siguiente en el `.pak`:

- **Modo Run por chunks:**
  - `Blueprints/Gameplay/Chunks` (14 BP).
  - `LVL_Run`.
  - Interactuables de BP: `BP_ButtonInteractable` (usa `VREditor/LaserPointer/CursorPointer`), `BP_PressurePlate` (`EditorMeshes/AssetViewer/Floor_Mesh`), `BP_UmbrellaInteractable` (Cone, Cylinder y `MaterialError_Mat`), `BP_TutorialEntryInteractable` (`WorldGridPreviewMaterial`), `BP_BreakablePlatform` y `BP_PhysicsObject` (`EngineMeshes/Cube`).
- **Grid y ProcMap procedural:**
  - `Blueprints/Gameplay/GridMap` (18).
  - Props y estructuras de `TN_ProcMapPropMeshes.h` (unos 45 tipos), cuevas, formaciones y `ATN_Proc*` de puzle y agua.
  - Según el plan §3.1, en Prepared se saltan estructuras, *scatter* y flora.
- **Kit de Álvaro que no usa el HQ:** 13 muebles, 10 muros o vallas y 2 suelos de `Blueprints/Builder`, y los `_GENERATED` de `LVL_Lobby`.
- **Mapas de prueba:** `LVL_TestMap`, `LVL_LevelMetrics`, `LVL_ProcGenDemo`, `LVL_ProcMap_Terrain`, `LVL_Lobby` y `LVL_Mapa01`.
- **Huérfanos** (ninguna referencia desde otro paquete): `Gorro11`, `Gorro21`, `Gorro31` y `Herizo` (282 KB), sustituidos por `SM_Helmet_*`.
- **Obsoletos:** `BP_TortugaCharacter1` (lleva materiales de depuración), `Blueprints/Gameplay/OLD` (2) y `BP_SeagullActor`.

## 5. Cuadre con el plan maestro (§3.6)

| Asset del plan | Aquí | Diferencia |
|---|---|---|
| `M_BuggyPaint` (1) | §2.5, encargo 4 | Igual |
| 7 iconos de TcT | §2.4, encargo 3 | Iguales como entregables; **1 encargo** (plantilla) en lugar de 7, porque las mallas ya existen y se renderizan |
| Icono, splash y cápsulas de Steam (3) | §2.6, encargos 1 y 2 | Igual número de entregables; se piden **key art y logo**, y los 3 se derivan de ellos |
| Buggy «(0), migración» | §2.5 | Se acepta para el MVP. **P1 añadido**: buggy estilizado, porque la plantilla Offroad pesa 13,8 MB y es de estilo realista |
| — | Kit HQ (§2.2) | **Añadido como P0.** El plan no presupuesta el HQ, que hoy es greybox con `MI_Grid_*` y es lo primero que ve la jugadora |
| — | 12 sustituciones P0 sin arte, 22 licencias de audio y el renombrado del SKM heredado | **Añadidos.** El plan no los contempla |
| Resto de filas «— (0)» | B-proc o C con 0 arte | Igual |

## 6. Assets que pesan de más o están mal referenciados

### 6.1 Peso en disco (editor)

| Asset | Tamaño | Problema | Acción | Ahorro |
|---|---|---|---|---|
| `Terrain/Volumes/Mapa01/DA_M_Mapa01_*` (36) | 41,5 MB | Repiten la geometría de las SM | Pasar los campos a `WITH_EDITORONLY_DATA`, o excluirlos del cocinado y del repo (se regeneran con el script) | 41,5 MB |
| `Terrain/Volumes/Mapa01/Meshes/SM_M_Mapa01_*` (36) | 31,4 MB | 1,78 M tris, un 68 % por encima del objetivo de CP01 (≤ 1,1 M); mapa de demo | Excluir del cocinado si no se publica | 31,4 MB en el `.pak` |
| `Environment/Water/DA_WaterSurface` | 6,2 MB | Intermedio del importador; la SM ocupa 534 KB | Igual que los DataAssets del terreno | 6,2 MB |
| Plantilla Offroad (HellYeah) | 13,8 MB | Plantilla realista; el SKM solo ya pesa 8,7 MB | Buggy estilizado de 6–10 k tris (unos 1–1,5 MB, est. propia) | ≈ 12 MB |
| `Audio/DanceSounds` (10) | 15,3 MB | Pistas largas (`7` = 3,5 MB, `9` = 4,1 MB) | Bucles de 30 s como máximo y calidad de compresión de 40 al cocinar | ≈ 60 % (est. propia) |
| `Audio/Music/TotugasLobbyMusica` | 4,6 MB | Una sola pista | Streaming y compresión | — |
| `Animations/.../Old_Man_Idle` y `Yelling` | 905 KB y 758 KB | Clips de Mixamo largos | ACL y recorte | ≈ 1 MB |

**Texturas:** ninguna pasa de 2K. Las normales de terreno y agua son de 1024 (`gen_terrain_textures.py:28`, `gen_water_textures.py:23`) y las de personaje, 9 × 51 KB. No hace falta actuar.

**Mallas sin LOD:** todas las mallas de código son de un solo LOD (`TN_ProcMapRuntimeMesh.h`), compensado con ISM y distancias de corte. En el terreno, el LOD lo hace Nanite. Quedan por confirmar en el editor los LODs de `TotugaDemo_Rig` y `SKM_MiniCangrejo`.

### 6.2 Material de depuración en código

12 ficheros C++ cargan `/Engine/EngineDebugMaterials/VertexColorMaterial`:
- `TN_PlaygroundMeshKit.h`, `TN_CastleKit.h`, `TN_GeneralBriefing.cpp`, `TN_TutorialCourse_Build.cpp`, `TN_TutorialFauna.cpp` y `TN_DizzyBirdsComponent.cpp`.
- `TN_GhostMeshes.h`, `TN_TurtleFaceComponent.cpp`, `TN_BeachDecor.cpp`, `TN_BeachEnemyKit.h`, `TN_ProcMapGenerator_Build.cpp` y `TN_ProcMapGenerator_Flora.cpp`.

Otros 11 usan `M_CosmeticVertexColor`. Unificar en uno solo reduce las permutaciones de shader y deja la iluminación bajo control del proyecto.

### 6.3 Contenido de editor o de depuración en assets de juego (16)

- **Blueprints de juego:** `BP_ScorePickup`, `BP_JellyfishActor`, `BP_ConchPickUp`, `BP_RescuePickUp`, `BP_GenericPickup`, `BP_BouncePhysicsObject`, `BP_CrabActor`, `BP_EnemySeagull` y `BP_BananaPeel`.
- **Datos, estatuas y HUD:** `DT_Items`, `BP_HatStatue`, `BP_SkinStatue` y `WBP_PlayerHUDWidget`.
- **Heredados:** `SKM_MERGED_*`, `SM_MERGED_*` y `BP_TortugaCharacter1`.
- **Del modo Run (§4):** `BP_ButtonInteractable`, `BP_PressurePlate`, `BP_UmbrellaInteractable` y `BP_TutorialEntryInteractable`.

Comprobación recomendada: cocinar con `-cookonthefly` o un cocinado completo, y buscar en el log los avisos de paquetes `EditorOnly`.

## 7. Encargos por orden

| # | Encargo | Cubre | Prio | Entregable y presupuesto |
|---|---|---|---|---|
| 1 | Key art | 5 cápsulas y splash | P0 | Ilustración de 3840×2160 con capas |
| 2 | Logotipo | Icono y logo del menú y de las cápsulas | P0 | Vectorial y `.ico` de 256 |
| 3 | Plantilla de icono «pegatina» | 7 iconos de TcT, iconos de `DT_Items` y de skins (por `render_preview.py`) | P0 | PSD de 256² y marco en 9-slice |
| 4 | `M_BuggyPaint` | Todas las skins del buggy | P0 | Material maestro y 1 MI por skin |
| 5 | Kit HQ | 9 módulos, porterías y peanas | P0 | 10 SM de 500–2 000 tris y 1 atlas de paleta |
| 6 | Buggy estilizado | Rally | P1 | SK de 6–10 k tris, 1 rueda y LOD1/LOD2 |
| 7 | Ave (gaviota y pelícano) | 4 implementaciones de gaviota y el pelícano | P1 | SK de 1,5 k tris, aleteo y planeo |
| 8 | Tema de UI | Menús, HUD y tienda | P1 | Atlas de 1024² |
| 9–15 | Kits P2: playa, restos y mobiliario, militar, trampas y estructuras, fauna hostil, flora tropical, decal de derrape | 95 objetos que hoy se generan en código | P2 | Un atlas de paleta compartido y un material maestro; recetas por `FRecipe` e ISM |
