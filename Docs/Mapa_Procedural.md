# Mapa procedural por módulos (World/ProcMap)

> **Estado (2026-10-02):** el plan maestro (`Docs/2026-09-29-Plan-Maestro-Modos-y-Mapas.md`) retira la generación procedural del Coop: el Coop usa mapas volumétricos fijos (`Source=Prepared` en `ATN_ProcMapGenerator`). Este documento sigue describiendo el framework `World/ProcMap` (GameMode, salida, nado, red); la generación procedural de §3 ya no es el camino vigente.

Sistema nuevo de generación de mapas: una rejilla de **módulos irregulares de 400 m**
por la que serpentea un camino largo y natural, con biomas por regiones, cruces
colosales (puentes y murallas con puerta que pasan por encima o por debajo de un tramo ya
recorrido), ramas que se vuelven a unir, agua con fauna peligrosa y tres modos de
juego (Coop, Carrera y 2vs2). **Convive** con el sistema de chunks
(`ATN_ChunkManager` + `LVL_Run`), que queda intacto como modo *Clásico*.

---

## 1. Cómo probarlo

1. Compila el proyecto (se añadieron los módulos `ProceduralMeshComponent` y `PCG`
   y ambos plugins en `Tortunabo.uproject`).
2. En el editor, consola Python:
   ```python
   exec(open(r"<repo>/Scripts/build_procmap_assets.py", encoding="utf-8").read())
   ```
   Crea `/Game/ProcMap` (materiales, `DA_ProcMapSettings`, 8 `DA_Biome_*`,
   `BP_ProcMapGameMode`), el nivel `/Game/Maps/Run/LVL_ProcMap` y coloca en
   `LVL_HQ` dos selectores (modo y dificultad) junto a la zona de listos.
   Es idempotente: no pisa assets que ya existan.
3. Desde el lobby: interactúa con los selectores (Clásico → Coop → Carrera → 2vs2;
   Fácil → Normal → Difícil). *Clásico* viaja a `LVL_Run` como siempre; *Carrera*, a la playa (`LVL_BeachRace`,
   `Docs/Modo_Carrera.md`; si ese nivel no existe, a `LVL_ProcMap`); el resto a `LVL_ProcMap`. En el castillo
   (`LVL_Lobby`) no hay selectores: el modo se elige en el menú principal. El modo por defecto es Clásico para no
   cambiar nada hasta que se elija.
4. Sin lobby: abre `LVL_ProcMap` en PIE. Opciones de URL para iterar:
   `open LVL_ProcMap?ProcMode=Race?ProcDifficulty=Hard?ProcSeed=42`
   (`ProcMode` = `Coop` | `Race` | `2v2`). También `FixedSeed` en el GameMode.
5. Previsualizar sin jugar: selecciona `ProcMapGenerator` en el nivel y pulsa
   **GenerateInEditor** (semilla, modo y dificultad en *ProcMap|Editor*). Con
   `bDebugDraw` dibuja camino, ramas y módulos.

6. **Probar la tormenta** en cualquier sitio (PIE, consola): `TNStorm <Selva|Playa|Desierto|Volcan|Agua|Rocas|Manglar|Pueblo>`
   lleva la tortuga al tramo más largo de ese bioma y pone el frente de la tormenta 9 m por detrás
   (inofensiva); `TNStorm Geiser` y `TNStorm Cascada` van cada vez al siguiente géiser o cascada del
   mapa; un segundo número cambia la distancia (negativo = ya dentro); `TNStorm Off` la para y la
   devuelve a la normal.
7. **Solo terreno** (para comparar generadores): `ATN_ProcMapGenerator::bTerrainOnly` genera el
   terreno y lo integrado en el camino (cuevas, puentes, murallas, huecos, troncos, obstáculos,
   géiseres y cascadas) sin vegetación, fauna, formaciones decorativas, hitos, recompensas, huevos,
   peligros ni efectos. `ATN_TerrainViewGameMode` (nivel `LVL_ProcMap_Terrain`) lo usa con una tortuga
   sin HUD; `TNRegen [semilla]` vuelve a generar. El nivel es una copia de `LVL_ProcMap` con ese GameMode
   en World Settings y `bTerrainOnly` marcado en el generador (también para *Generate In Editor*). Como
   `LVL_ProcMap` y `Content/ProcMap/`, no está en el repositorio.
8. **Rebuscar en los decorados** (mantener E junto a estatuas, rocas grandes, barcas, cajas...: a veces sale un
   objeto): `tn.Search.Show 1` marca los buscables y `tn.Search.Luck 1` fuerza la suerte. Todo en
   [Botin_Decorados.md](Botin_Decorados.md).
9. **Conchas de puntos** (consola, también como cliente: el trabajo lo hace el servidor):
   - `TNShells 1 8`, `TNShells 25 3`, `TNShells 50`, `TNShells 100`: sueltan N conchas (hasta 20) de ese valor en fila
     delante de la tortuga, a 2,6 m y luego cada 1,7 m, para cogerlas de una carrera (duran 5 min si nadie las coge).
   - `TNShells Especial`: lleva cada vez a la siguiente concha especial (50 o 100) del mapa, en orden por el camino,
     4 m antes de ella y mirándola.
   - `TNShells Lista`: cuántas hay de cada tamaño y dónde van las especiales (lo mismo que la línea `[ProcMap] Conchas:`
     del registro).

Cada generación deja en el Output Log una línea `[ProcMap] Mapa listo · semilla …`
con módulos en ruta, cruces, ramas, **longitud del camino y minutos estimados**
a 5,5 m/s, y los tiempos de cada fase.

---

## 2. Arquitectura

Dos capas, como el resto del proyecto (`TNGridLogic`, `TNChunkLogic`):

**Lógica pura** (`namespace TNProcMap`, solo cabeceras, sin UObjects, determinista):

| Cabecera | Qué hace |
|---|---|
| `TN_ProcMapMath.h` | RNG SplitMix64 con `Fork` por fase, ruido de gradiente, fBm, ridged, utilidades 2D. |
| `TN_ProcMapLayout.h` | Tipos del resultado (`FLayout`): módulos, ruta, portales, cruces, camino muestreado, ramas, features. |
| `TN_ProcMapModules.h` | Módulos irregulares **siempre conexos**: semillas con jitter + Dijkstra multi-fuente sobre coste con ruido, limpieza de conectividad y transformadas de distancia. |
| `TN_ProcMapRoute.h` | Ruta por los módulos: DFS aleatorio con Warnsdorff y poda por alcanzabilidad; reserva los pasos de los cruces colosales (A→B→C sobre un módulo ya visitado). |
| `TN_ProcMapPath.h` | Portales en las fronteras (PCA), "caminante" con meandros senoidales dentro de cada módulo, suavizado Chaikin, anchos por tramos 3,5–60 m, perfil de alturas con límite de pendiente y cortes en géiser/tobogán, ramas y carriles. |
| `TN_ProcMapFeatures.h` | Biomas por regiones (tipo Minecraft), huecos saltables, isletas y pasarelas, pilas de huevos, puzles 2vs2, río opcional, decoración y reparto de peligros. |
| `TN_ProcMapTerrain.h` | Altura por vértice: parámetros mezclados por bioma con *domain warp*, pasillo del camino con arcén y taludes de 55-75°, torres y puertas de los cruces, loma sobre las cuevas, volcanes asentados en el relieve, muros del borde, costa y mar abierto al norte. `HeightAtPoint` evalúa la altura en cualquier punto con el mismo campo del camino que el mallado. |
| `TN_ProcMapTerrainDetail.h` | Detalle adaptativo: los cuadrados de 1,5 m en los que la malla se aparta más de 20 cm de la forma real cerca de los caminos (60 cm lejos) se parten a 0,5 m; sus vecinos cosen con un abanico. Mallas de las teselas estancas y altura dibujada en cualquier punto (`SurfaceAt`). |
| `TN_ProcMapCaves.h` | Cuevas: tramos del principal de 120-260 m que atraviesan una montaña por un túnel con pasos estrechos, una o dos cámaras anchas y, en el volcán, río de lava que se salta; prefieren tramos que cruzan terreno alto y se estrechan por un desfiladero hasta la boca. |
| `TN_ProcMapFormations.h` | Formaciones temáticas por bioma: arcos que cruzan el camino, piezas en las explanadas (con carriles libres) e hitos lejanos (naturaleza, entorno y guerra). |
| `TN_ProcMapFlora.h` | Vegetación, rocas y objetos sueltos: especies por bioma y reparto determinista en manchas, también en los taludes. |
| `TN_ProcMapShells.h` | Conchas de puntos de 1, 50 y 100: rachas por el camino y los desvíos, arcos sobre los saltos, filas por las cornisas y especiales en retos o escondidas (`PlanShells`). Los tamaños y el reparto de una recogida en iconos del HUD, en `World/TN_ScoreShells.h`. |
| `TN_ProcMapGenerate.h` | `GenerateLayout(params)`: orquesta todo, valida y reintenta. |

Tests de automatización: `Tortunabo.ProcMap.*` (`LayoutInvariants`,
`ModulesConnected`, `Determinism`, `Terrain`, `WallBreaches`) en `Private/Tests/TN_ProcMapDecisionsTest.cpp`, y
`Tortunabo.ProcMap.Shells` y `Tortunabo.ScoreShells.Split` (reparto de las conchas y su animación) en
`Private/Tests/TN_ScoreShellsTest.cpp`.

**Capa UE** (`World/ProcMap`, `Game`, `Lobby`, `Player`):

| Clase | Papel |
|---|---|
| `ATN_ProcMapGenerator` | Traduce el layout a mundo: terreno en tiles de `UProceduralMeshComponent` con colisión y color de vértice, agua (plano + `ATN_ProcWaterVolume` nadable), estructuras colosales, formaciones y techos de cueva (con luces), vegetación procedural (mallas estáticas construidas en ejecución e instanciadas con HISM), capas de props por bioma, grafo PCG opcional por bioma y todos los actores de gameplay. Las mallas low-poly salen de cabeceras privadas sin dependencias del motor (`TN_ProcMapMeshKit.h`, `TN_ProcMapFloraMeshes.h`, `TN_ProcMapFormationMeshes.h`, `TN_ProcMapCaveMeshes.h`, `TN_ProcMapCaveDecor.h`, `TN_ProcMapFinishMeshes.h`, `TN_ProcMapPropMeshes.h`, `TN_ProcMapRockMeshes.h`), previsualizables fuera de él. |
| `UTN_ProcMapSettings` / `UTN_ProcBiomeDataAsset` | Slots de datos: perfiles por modo × dificultad, materiales, clases, y por bioma colores, capas de vegetación, peligros y criatura acuática. Sin assets, todo sale en greybox. |
| `ATN_ProcGeyser`, `ATN_ProcSlideZone`, `ATN_ProcKillVolume`, `ATN_ProcFinishVolume` | Conexiones especiales (géiser que sube, cascada-tobogán que baja, un solo sentido y automáticas), caídas mortales y meta. |
| `ATN_ProcWaterVolume`, `ATN_ProcWaterCurrent`, `ATN_ProcWhirlpool`, `ATN_ProcWaterPredator`, `ATN_ProcWaterBouncer` | Agua nadable y sus peligros: corrientes, remolinos, depredador (tiburón/morena) y criaturas con comportamiento de medusa distintas por bioma. |
| `ATN_ProcEggNest` | Pilas de huevos de reaparición en los cruces entre módulos (densidad según dificultad). |
| `ATN_ProcStartStructure` | Salida de la ronda: la puerta doble o la pila de huevos del lobby (mismo kit, `TN_CastleKit.h`), con los jugadores dentro hasta que se abre (ver «Salida» en el apartado 4). |
| `ATN_ProcThrowWall`, `ATN_ProcSabotageGate`, `ATN_ProcSwitch` | Puzles del 2vs2: muro que hay que superar lanzando al compañero (o bajando la rampa con el interruptor) y compuertas de sabotaje para la otra pareja. |
| `ATN_PathStorm` | Tormenta del Coop que avanza **por el camino** (progreso en cm), no en línea recta, y nunca más rápido que la tortuga andando. Frente con velo translúcido animado (`M_ProcStormVeil`), nubes que ruedan (`M_ProcFXCloud`) y lo que arrastra cada bioma (arena, hojas, brasas y ceniza, espuma, lluvia, polvo, humo y papeles), mezclado en degradado al cambiar de bioma; dentro, la niebla del nivel se cierra y la imagen se tiñe según el bioma del jugador (`TN_PathStormFX.h`). |
| `ATN_ProcMapGameMode` / `ATN_ProcMapGameState` | Rondas, modos, reaparición en huevos, tormenta, espera a que todos tengan el mapa. |
| `ATN_ProcModeSelector` | Interactuable del lobby para elegir modo y dificultad. |
| `UTN_CarryComponent` | Coger y lanzar tortugas (issue #6, fase 2). |
| `ATN_ScorePickup`, `ATN_ScoreShellBurst`, `UTN_ScoreShellSynthComponent` | Conchas de puntos de 1, 25, 50 y 100 (ver «Conchas de puntos» en el apartado 3): la concha, su estallido local y el «¡plin!» y el «pom» sintetizados. La animación del contador, en `UTN_RunHUDWidget`. |

---

## 3. Qué se genera

- **Rejilla** de `GridSize × GridSize` módulos de 400 m, de forma irregular y siempre
  conexos. El camino recorre `Coverage` de ellos (por defecto ~78 %: 28 de 36 en 6×6).
  Los que quedan fuera se rellenan según `EmptyModuleMode`: *Elevated* (mesetas
  inaccesibles), *BranchesAndScenery* (ramas y paisaje), *Explorable* (terreno
  transitable sin objetivo) o *Mixed*.
- **Camino principal** largo y natural, con anchura por tramos de 40–150 m: desfiladeros
  de 3,5–5 m, pasos cerrados de 6–10 m, tramos normales, anchos de 20–35 m y explanadas
  de 40–60 m, con pocos tramos intermedios (más cañones en desierto y roca, más arenales
  abiertos en la playa). La anchura se recorta para que entre dos partes del camino quede siempre un muro de 18 m.
  El terreno ondula sin tendencia general, con lomas de 0,6–2,2 m cada 50–110 m por todo el
  recorrido (12–22 m de subida por km; nada en el agua, al salir ni en la llegada); las ramas, junto a
  sus uniones, toman la cota del principal. Los cambios grandes de altura solo ocurren al cruzar de
  módulo, mediante **géiser** (sube) o **cascada-tobogán** (baja).
  Los huecos del camino principal miden 1,3–3,9 m (salto corriendo o con dive), con labios de
  madera, sillería o basalto según el bioma; algunos son más largos (hasta 1,8 veces) con **postes**
  en rejilla que los parten en saltos cortos (troncos, pilotes, basalto o columnas) y otros llevan
  **troncos de equilibrio** de labio a labio. Van a **30 m como mínimo** entre sí (antes 40) y `GapsPerKm`
  es la densidad que sale de verdad en los tramos donde caben (llanos, de menos de 26 m, fuera de estructuras,
  horquillas y agua): la probabilidad por muestra compensa los 30 m muertos tras cada hueco,
  `λ = R / (1 − R · 30 m)` con `R = min(GapsPerKm, 26,7)` por km (antes, con 9 por km salían unos 6,6).
  Las ramas arriesgadas llevan el doble. En el manglar, las pasarelas van en trozos de 12-32 m (antes 15-40 m)
  y entre trozo y trozo hay hueco el 57 / 67 / 81 % de las veces (F/N/D; antes 43 / 55 / 71 %).
  Desde Normal, parte de los huecos de labios son **saltos de panzazo** (`EGapStyle::Dive`, de
  `DiveGapMin` = 2,7 m a `DiveGapMax` = 3,7 m, sin pasar de los huecos máximos de la dificultad): más de lo
  que da un salto corriendo (2 m) y menos que con panzazo (4 m). En el camino principal siempre hay al menos
  uno (si no sale ninguno, el hueco de labios con la zanja más larga pasa a serlo). En el labio de llegada
  llevan tres chevrones amarillos y rojos que apuntan al hueco y, fuera del camino, un cartel con «!».
- **Torres de escalada** junto al borde en tramos anchos: bloques del bioma (cajas con aspa,
  tocones, sillares, losas o basalto) de 3–4 m con escalones de 1 m, banderín, recompensa de puntos
  arriba (`BP_ScorePickup`) y una medusa al pie (`BP_JellyfishActor`) para subir de un bote. Salen en el
  24 % de los turnos de obstáculo de los tramos de 13 m o más (antes 14 %).
- **Conchas de puntos** (`ATN_ScorePickup`, `BP_ScorePickup`): una vieira que gira como una moneda de plataformas
  clásico, sube y baja y brilla (`M_ProcGlow`). Si arte pone una malla propia en `PickupMesh`, se ve esa y la concha
  no (la de ayuda del motor, el signo de interrogación, cuenta como vacía). Hay **cuatro tamaños** según `ScoreValue`
  (`TNScoreShells`, `World/TN_ScoreShells.h`; hasta 5 puntos, pequeña; hasta 37, normal; hasta 75, grande; más, reina):

  | | Pequeña | Normal | Grande | Reina |
  |---|---|---|---|---|
  | Puntos | 1 | 25 (la de siempre) | 50 | 100 |
  | Aspecto | oro claro, escala 0,8 (~55 cm), más brillante, sin destellos | dorada, escala 1,5 (~1 m), destellos dorados | nácar turquesa, escala 2 (~1,4 m) | rosa y violeta con borde dorado, escala 2,5 (~1,7 m) |
  | Adornos | — | 14 destellos | 20 destellos, halo blando que late, luz de 1200 lm (5 m) y columna de luz de 9 m | 22 + 12 destellos, halo, luz de 2200 lm (7,5 m) y columna de 13 m |
  | Radio de recogida | 48 cm | 60 cm | 72 cm | 80 cm |
  | Giro | 0,6 vueltas/s | 0,45 | 0,35 | 0,3 |
  | Se dibuja hasta | 70 m | siempre | siempre | siempre |

  El centro (y la esfera de recogida) va a 60 cm del suelo (`TNScoreShells::Hover`) y la vieira gira 30-85 cm más
  arriba. Lejos de la cámara local (70 m las pequeñas, 90 m las normales, 250 m las especiales) ni giran ni mueven sus
  destellos: el tick pasa a dos veces por segundo. En los clientes la esfera no tiene colisión (decide el servidor).
- **Reparto** (`TNProcMap::PlanShells`, `TN_ProcMapShells.h`, puro y determinista; en el servidor lo pone
  `ATN_ProcMapGenerator::SpawnShells` después de los peligros, sin pisar nada de lo que estos han puesto):
  - **Normales de 25**: como antes, con los peligros de cada bioma (`BP_ScorePickup`, 4 por km con la densidad de
    peligros del perfil y 30 m entre sí) y en lo alto de las atalayas de las plazas y de las torres de escalada.
  - **Conchitas de 1** (tope de 600 por mapa):
    - *Rachas del camino principal*: 5-7 a 1,8 m una de otra, serpenteando suave dentro del cauce (a 1,3 m del borde),
      una cada 220-340 m. Si con eso pasarían del tope, se espacian más para llegar a todo el camino (en un Coop
      Difícil de ~21 km, una cada ~500 m).
    - *Rachas de ramas y desvíos*: igual, pero una cada 120-190 m (tope de 200): premian explorar.
    - *Arcos de salto*: sobre los huecos de labios y de panzazo (que miden hasta un salto esprintando y 1 m), 5 conchitas
      (6 en los de panzazo) de labio a labio, 70 cm más allá de cada uno, subiendo 90 cm en el medio (el 75 % de lo
      que sube el salto, 1,2 m): el centro de la tortuga pasa por ellas al saltar. Como mucho un arco cada 380 m de cada
      camino, o más separados si no caben en su tope (120) a lo largo de todo el mapa.
    - *Cornisas del adarve roto*: 2-6 por el medio de cada cornisa (a lo largo del eje de verdad de la muralla, a 60 cm
      sobre el adarve; tope de 40): marcan el paso estrecho. La cornisa no es zona de muerte.
    - Nunca en muestras especiales del camino (huecos, estructuras, puentes, torres, cuevas, géiseres, toboganes,
      portales, uniones, orilla, agua, carriles) ni a menos de 1,5 m de peñascos, agujas, objetos, troncos (su medio
      largo), 2,5 m de torres de escalada, recompensas y medusas, 9 m de huevos y géiseres, 8 m de puzles 2vs2, del
      claro de salida, de las torres colosales, de las formaciones del camino, de la lava y de las pozas de las cascadas,
      ni a menos de 3 m de lo que han puesto los peligros (conchas normales incluidas). Entre dos conchitas, 1,1 m
      en planta (salvo las de un mismo arco, que van a alturas distintas).
  - **Especiales de 50 y 100** (una por muralla o por rama, a 60 m como mínimo entre sí y nada a menos de 3,5 m):
    los sitios candidatos se ordenan por lo difíciles o escondidos que son y se reparten **una reina** (dos si el camino
    principal pasa de 15 km), solo en sitios de puntuación 2 o más, y **una grande cada ~3 km** de camino principal
    (1-5).

    | Sitio | Puntuación | Dónde exactamente |
    |---|---|---|
    | Brecha del adarve (de lado a lado, hasta el 75 % de un salto esprintando) | 3 + largo/10 m | en el aire sobre el centro de la brecha, a 1,5 m sobre el adarve: se coge saltándola |
    | Rama arriesgada | 2,2 | 4 m pasada la zanja de su último hueco (o a media rama) |
    | Ruta alta | 1,6 | en el centro, 9 m antes de lo alto del tobogán |
    | Salto de panzazo más largo del camino principal | 1,4 (0,9 si es de labios de 2,5 m o más) | en el suelo, 3 m pasada su zanja |
    | Desvío por un módulo vacío | 1,3-1,8 (más cuanto más lejos del principal) | en su punto más apartado del camino principal |
    | Rama tranquila | 1 | a media rama |

    Las del suelo van en el centro del camino, en una muestra normal; si ahí hay algo, se prueba a saltos de 2 m hasta
    10 m hacia delante y hacia atrás.
  - **Tramos hundidos de los puentes colosales** (los añade la capa UE, porque salen de la malla del puente,
    `TNProcAddBrokenSpan`): en el más largo de cada puente, una **reina de 100** en lo pisable del medio, 60 cm por
    encima: el codo del medio de las vigas en zigzag, la cima del poste del medio o el tablón atravesado entre las dos
    cornisas (en los dos primeros puentes del mapa; en los demás, una grande de 50).
  - Con los perfiles por defecto sale del orden de (estimación; los números reales, en el registro y en la prueba):
    Fácil (3×3, ~3 km) unas 150-250 conchitas, 1 grande y hasta 1-2 reinas; Normal (6×6, ~12 km) el tope o casi
    (500-600), 4 grandes y 1-3 reinas; Difícil (8×8, ~21 km) el tope, 5 grandes y 2-4 reinas. Cada mapa lo dice en el
    registro:
    `[ProcMap] Conchas: N pequeñas de 1 (rachas a, desvíos b, arcos de salto c, cornisas d), G grandes de 50 y R reinas
    de 100: 100 en brecha de muralla, 50 en ruta alta...`. La prueba `Tortunabo.ProcMap.Shells` da los números reales
    de varias semillas.
  - Las pequeñas suman a las conchas acumuladas de la tienda como el resto (`AccumulatedRaceScore`): unas 150-600 más
    por partida, según el mapa.
- **Recogida** (servidor): suma `ScoreValue` a `RaceScore` (`AddRaceScore`), llama a
  `ATN_CoopPlayerState::MulticastScoreShellCollected` (fiable, por el PlayerState, que ni se destruye ni duerme: el
  multicast de la propia concha se perdería al destruirla) y destruye la concha. En cada máquina con pantalla, a
  menos de 150 m de su cámara, sale un **estallido** local (`ATN_ScoreShellBurst`, sin replicar): destello (bola blanda
  que se abre en 0,25 s y un fogonazo de luz sin sombras de 350-8000 lm que se apaga en 0,35 s), anillo que se abre en
  horizontal, 7/14/24/36 chispas que saltan y caen y, en grandes y reinas, estrellitas que suben despacio. Todo crece
  con el tamaño (×0,55 / 1 / 1,35 / 1,75).
- **«¡Plin!»** (`UTN_ScoreShellSynthComponent`, sintetizado, 3D en el estallido; pleno a 4,8-10,8 m y se oye hasta
  19-56 m según el tamaño): campanitas de cristal (parciales 1, 2, 3 y 5,4) con una pizca de desafinado al azar.
  Pequeña: un mi6 corto con su octava. Normal: si5 y mi6, la moneda de siempre. Grande: arpegio do6-mi6-sol6-do7, el acorde de do mayor que queda
  (coro y trémolo) y chispitas agudas durante 0,9 s. Reina: arpegio sol5-do6-mi6-sol6-do7-mi7 y un acorde de do mayor
  con séptima y novena que se abre (1,5 s) con chispitas durante 1,4 s.
- **Contador del HUD** (`UTN_RunHUDWidget`, solo el jugador que la coge, con el aviso `OnScoreShellCollected` del
  PlayerState):
  - Los iconos salen de donde estaba la concha en pantalla (o de la tortuga si quedaba detrás de la cámara): del
    tamaño de la concha (pegatinas `TNHUDArt::ShellIconTier`: dorada, melocotón como la del contador, turquesa con
    estrella y rosa con estrella dorada), tantos como `TNScoreShells::IconCountFor` (1 punto, 1; 25, 10; 50, 14; 100,
    15; nunca más de 15) y repartiéndose el valor (`SplitIntoIcons`: suman exacto y el resto va a los últimos).
  - Dan un saltito de 180 ms a un óvalo por encima del sitio, esperan su turno temblando y salen uno cada 35-80 ms
    (0,55 s la tanda entera) en una curva de Bézier que primero sube y luego se curva hacia la concha del contador,
    acelerando al final (0,45-0,85 s según la distancia), girando, encogiéndose al 60 % y con una estela de dos copias.
  - Cada uno, al llegar, suma su parte al número, que salta (+28 %), y la concha del contador se aplasta y se endereza;
    suena un «pom» 2D (seno de do5 que cae un poco en 12 ms, su octava y un clic de madera) que sube por la escala
    pentatónica (do, re, mi, sol, la, do...) hasta dos octavas y vuelve a empezar tras 0,9 s sin llegar ninguno. Al
    acabar una grande o una reina, su icono crece desde la del contador y se apaga. Un «+N» dorado bajo el contador
    suma las recogidas seguidas y se apaga 0,6-1,1 s después de la última.
  - **Cola**: una recogida no empieza a volar hasta que ha salido el último icono de la anterior, así que no se pisan.
  - **Cuadra siempre** con `RaceScore` (la clase base sigue escribiendo la puntuación real en un `ScoreText` oculto):
    lo que sube sin aviso de concha (los puntos de llegada, un aviso perdido) sale volando de la tortuga tras 0,45 s;
    si la cuenta queda por encima (ronda nueva, puntuación que no llega), se ajusta sola en 0,6 s (2 s si aún vuelan
    iconos).
- **Terreno**: malla de 1,5 m con **detalle de 0,5 m** donde hace falta (pie y borde de los taludes,
  crestas, bocas de cueva: un 3-4 % de los cuadrados, más un 5-6 % de costuras; 1,4-1,5 veces los
  triángulos). Las paredes suben sin repisa: el talud llega al borde con la pendiente con la que arranca
  la subida, la pared del cañón sube desde el borde del cauce y, cerca de los caminos, la distancia al
  cauce es la exacta (no la rejilla de 10 m). El pie, el ancho del talud y la subida varían a lo largo del
  camino (ruido de 17-30 m). `DetailSpacing` y `DetailError` en los ajustes.
- **Color del camino**: un color de sendero propio de cada bioma, de tono y luminosidad
  claramente distintos de sus paredes (tierra anaranjada, barro claro, ceniza rojiza, arena mojada,
  arcilla roja, grava ocre, adoquín pizarra, tablas oscuras), con una línea oscura al pie del talud;
  las paredes, en degradado por pendiente (suelo, roca y roca más oscura en los tajos) con estratos
  suaves. La playa de la meta conserva su arena.
- **Detalle procedural del suelo** (`M_ProcTerrain`, nodo Custom `TERRAIN_DETAIL_HLSL` de
  `Scripts/build_procmap_assets.py`, sin texturas). Todo va en coordenadas de mundo (XY, en metros), así que no
  depende de las UV ni de las tangentes de las teselas; la normal sale en espacio de mundo (*Tangent Space Normal*
  desactivado).
  - *Relieve en todo el terreno*: tres octavas giradas de ruido de valor con derivadas analíticas (3,2 m, 1,1 m y
    0,33 m) forman un campo de alturas ficticio cuyo gradiente de superficie inclina la normal del vértice. Es más
    fuerte en el camino (×1,4) y en lo llano, y se queda en un 30 % en las paredes empinadas; los hoyos se oscurecen y
    las lomas se aclaran un 4 %. No cambia la geometría ni la colisión.
  - *Textura del camino* (donde el alfa del vértice, la máscara del camino, pasa de 0,45): manchas secas y
    polvorientas en las lomas y húmedas en los hoyos, terrones de 12 cm, grano de motas redondas de 1-2,5 cm, guijarros
    de 11-22 cm y piedrecitas de 4-9 cm, y marcas del viento (ondas de 28 cm, solo en las zonas secas). Las piedras toman
    el tono del camino, con la coronilla más clara, sombra de contacto y sombra arrojada lejos del sol
    (`SkyAtmosphereLightDirection`), para que se lean como piedras y no como hoyos. Todo modula el color de bioma que
    trae el vértice, sin sustituirlo.
  - *Sin parpadeo de lejos*: cada detalle se apaga cuando el píxel mide en el suelo más de 0,3-0,5 veces su tamaño
    (derivadas de pantalla); el fino, además, entre 0,45 y 1 vez `DetailDistance`. En los sombreadores de trazado de
    rayos no hay derivadas y el detalle va entero.
  - *Solo en las teselas*: las formaciones pintadas (`Painted`, `PaintedFar`) comparten el material, pero su alfa no
    es la máscara del camino. Las teselas ponen el dato de primitiva 0 a 1 (`SetCustomPrimitiveDataFloat` en
    `BuildTerrain`) y el material solo aplica el detalle con ese dato (parámetro `TerrainDetail`); lo demás se ve como
    antes.
  - Parámetros del material: `PathDetail` (1, fuerza de la textura del camino), `Relief` (1, fuerza del relieve) y
    `DetailDistance` (3500 cm). Coste: unas 160 instrucciones fuera del camino y unas 500 en el camino cercano; el
    detalle del camino va en una rama dinámica y solo se evalúa donde hay camino.
  - Para rehacerlo sin cambiar de nivel, en la consola Python del editor:
    ```python
    PROCMAP_SKIP_MAIN = True
    exec(open(r"<repo>/Scripts/build_procmap_assets.py", encoding="utf-8").read())
    build_terrain_material(rebuild=True)
    ```
- **Cruces colosales** tipo Mario Kart: puentes y murallas con puerta altísima que pasan por
  encima o por debajo de un módulo ya recorrido. Se llega a ellos por géiser/tobogán
  y caerse de un puente colosal es mortal. Los puentes dentro de un mismo módulo
  son normales. Sus dos torres son de sillería en talud con pretil y almenas: la de
  entrada es **hueca**: el camino llega en embudo a su puerta, a ras de suelo: un túnel
  recto de 4,6 m de ancho que atraviesa todo el grueso del muro, con bóveda de medio punto,
  suelo enlosado, portada plana al pie del talud con impostas, dovelas y clave en relieve,
  rastrillo levantado y dos antorchas. Dentro (sala iluminada por antorchas) el géiser del
  centro lanza en vertical por un hueco del forjado hasta la cima, junto al arranque del
  puente o del adarve, y su columna de agua asoma por ese hueco; la de salida lleva el
  tobogán. Las dos cimas van **enlosadas** (malla, `TowerDims::PaveLift` = 4 cm por encima
  de la cota de la torre), así que el núcleo de terreno y el tablero o el adarve que entran
  en ellas quedan debajo y no parpadean. Malla y terreno abren los mismos lados del pretil
  (`TowerOpenSides`); el núcleo quita su pretil de roca también en los 70 cm de lado cerrado
  junto a uno abierto (`TowerCoreOpenAt`), para que la rampa entre las dos cotas quede dentro
  del pretil de sillería. En la de salida, por los lados abiertos la sillería baja a plomo a
  60 cm del núcleo (`TowerDims::FlushOut`) y el terreno de alrededor no sube de la cima.
- **Ramas** que se separan y vuelven a unirse en 1–3 módulos, de cuatro tipos:
  *tranquila* (larga y holgada), *arriesgada* (cornisa de 3,5–5 m con el doble de
  huecos), *ruta alta* (sube en rampa suave por una loma junto al cauce y baja en
  tobogán al principal) y *rodeo* corto alrededor de un peñasco. Además, hasta un tercio
  de NumBranches en *desvíos* largos por los módulos que el camino no visita: salen del
  principal, pasan por el centro del módulo vacío (que deja de ser macizo; por una meseta
  es un desfiladero) y vuelven al principal en otro módulo o en el mismo; en 2vs2, **carriles**
  paralelos con puzles de lanzamiento y sabotaje. Los huecos de salto no se ponen en
  explanadas y, junto al agua, su zanja es menos honda para no bajar del nivel del mar.
- **Biomas por regiones** de varios módulos contiguos, al azar y con transición
  natural: selva, playa, desierto, volcánico, agua (isletas), acantilados rocosos,
  manglar y zona humana. El último módulo es siempre playa con mar abierto.
- **Playa de la meta**: el camino llega recto a 55-80 m de la costa y sus brazos se abren en
  arco (campana) hasta el agua, así la playa se descubre al avanzar; en la orilla la boca mide
  75-110 m y los brazos son el propio acantilado, que sigue en pie pasada la línea. La **línea
  de meta** cruza toda la boca unos 3,5 m mar adentro (agua por la rodilla): allí empieza el
  volumen de meta. Encima, un **neumático gigante en arco** (al estilo del puente Dunlop) con
  TORTUNAVY en el flanco que se ve al llegar (y, de guiño, el nombre en clave TORTUNABO en el
  que mira al mar), pasarela a cuadros con el cartel de META, rótulo «¡AL AGUA!» y
  banderas a cuadros; boyas marcan la línea de lado a lado y hay banderines y banderolas en la
  arena.
- **Agua en pozas**: en lagunas y manglar el agua no es un lago abierto sino pozas de
  25-60 m alrededor de cada tramo, con acantilado al borde; entre tramos alejados del
  recorrido (más de 90 m) y junto a la costa queda tierra alta con montañas, así que no se
  puede atajar nadando de un tramo a otro ni hasta la meta.
- **Borde** del mapa con muros altos irregulares que llevan el contenido del bioma.
- **Río** opcional (`bRiver`). Sus puentes de madera están **rotos** 3 de cada 4 veces cuando miden más de 16 m y
  debajo hay agua de verdad: les falta el centro (7 m, no se salta; bordes astillados y bandas de aviso) y hay que
  rodear por el agua. Se baja por el hueco, se va por las **piedras** (cima a 35 cm sobre el agua, a saltitos de
  menos de medio metro) o nadando hasta una **escalera de madera** pegada al puente por un lado (peldaños de 30 cm y
  42 de huella, puntales hasta el lecho) y se vuelve al tablero por un rellano, con la barandilla abierta ahí. La
  escalera se coloca donde la orilla no la entierre; si no cabe, el puente queda entero.
- **Taludes** del camino en rampa de 55-75° (no a plomo): la guarda que impide salir sube en
  rampa desde el borde de cada cauce y solo se empina entre dos cauces próximos (horquillas,
  curvas cerradas), para que la cresta que los separa no sea una rampa andable.
- **Volcanes** asentados en la cota del relieve que los rodea (percentil 75 de un anillo a 3/4
  de su radio) sobre una llanura volcánica: asoman por encima de las montañas.
- **Vegetación procedural** (`bProceduralFlora`, `FloraDensity`, material `M_ProcFoliage`): 27
  formas low-poly × 3 variantes por bioma (ceibas, árboles de copa, palmeras, mangles de
  raíces zancudas, secuoyas jóvenes, cipreses, pinos, abetos, sauces, acacias, árboles secos y
  calcinados, helechos, arbustos, hierba, flores, juncos, saguaros, cactus barril, matojos,
  setos, sombrillas, enredaderas y musgo de pared, peñascos y piedras) en manchas de bosque,
  sotobosque, pradera y pedregal, con tamaños muy variados; en los taludes junto al camino,
  densidad doble y enredaderas pegadas a la pared. En el manglar crecen también dentro de las
  pozas, junto a 16-26 secuoyas gigantes por módulo de tamaños muy distintos.
- **Objetos sueltos junto al camino** (34 tipos × 3 variantes, repartidos como la vegetación en
  rincones de ~25 m): cajas, barriles, vallas de obra, conos, pacas, bancos, farolas, buzones,
  sacos y macetas en la zona humana; conchas, estrellas de mar, cocos, troncos a la deriva, cubos
  y palas, toallas, sombrillas con hamaca, tablas de surf y salvavidas en la playa; setas,
  vasijas, antorchas tiki, postes con calavera y tocones en la selva; calaveras de vaca, huesos,
  ánforas, ruedas de carro, postes indicadores, plantas rodadoras y amatistas en el desierto;
  obsidiana, tocones calcinados, huesos e hitos en el volcán; hitos, cuarzo, cajas, barriles,
  faroles y postes en la roca; nasas, troncos, faroles y tocones en agua y manglar. Los macizos
  (cajas, barriles, pacas, bancos, farolas, buzones, vallas...) llevan colisión de caja. Sustituyen a
  las capas de formas básicas (greybox) cuando la vegetación procedural está activa.
- **Obstáculos de objetos dentro del camino** (18 tipos, con colisión y siempre con carril libre):
  pilas de cajas, barriles, vallas con conos, pacas de paja, castillos de arena, barcas volcadas,
  rincones de playa, tótems, columnas en ruinas, setas gigantes, calaveras gigantes, vasijas,
  cristales gigantes, hitos grandes, vagonetas, nasas, puestos de mercado y filas de conos, según
  el bioma; conviven con peñascos, agujas, mogotes y troncos.
- **Densidad de obstáculos** (`BuildObstacles`): un turno cada 15-36 m (antes 22-55 m) y, si en una muestra no cabe
  (estructura, horquilla, formación), se prueba en la siguiente en vez de perder el turno. Se apartan 24 m de géiseres,
  toboganes, torres, puertas, cuevas, portales y uniones, y 12 m de los huecos (aterrizar y coger carrerilla); tras una
  aguja o un tronco, el siguiente espera a que acabe (su radio o su vuelo a lo largo del camino y 12 m). Troncos caídos
  que se saltan en el 50 % de los turnos en selva, manglar y volcán (antes 40 %) y, en la playa, troncos a la deriva
  (28 %); agujas y mogotes en el 30 % de las explanadas (antes 35 %). Las **formaciones se colocan antes** que los
  obstáculos (`GenerateLayout`): los obstáculos no se ponen a menos de 3 m de la huella de una pieza de explanada ni del
  fondo o los pies de un arco, y las secuoyas del manglar se apartan de todas las formaciones.
- **Rocas del camino con estilo por bioma**: peñascos redondos, losas inclinadas, partidos,
  apilados, de estratos, columnas de basalto, con musgo, con cristales o de coral; agujas con
  sombrero, inclinadas, gemelas, chimeneas de hadas, pilares kársticos con vegetación y órganos de
  basalto; mogotes, tors de bloques, mesas de estratos y domos de lava.
- **Puentes colosales de cuatro estilos** según el bioma del cruce: colgante de cuerda (selva,
  manglar, agua, playa), viaducto de piedra con arcos rebajados entre los apoyos (roca, desierto,
  zona humana), caballete de madera con vigas hasta el suelo (desierto, playa) y hierro con
  pórticos y cadenas (volcán, zona humana). Las pilas y caballetes nunca caen sobre otro camino (el
  arco se une al siguiente) y las cuevas no se ponen bajo un tablero colosal. Los de piedra y hierro
  tienen a media altura una **plaza** redonda (sobre la pila central si la hay): pretil o barandilla
  abierta a las entradas del tablero, fuente con la tortuga o farol alto, farolas, bancos mirando al
  paisaje y una atalaya de 3 m con escalones, recompensa arriba y medusa al pie.
  Los de más de 42 m entre torres tienen **de uno a tres tramos hundidos** sin tablero (`TNProcAddBrokenSpan`): uno por
  cada 22 m de tablero útil (el que queda a más de 6 m de cada borde de torre), cada uno centrado en su parte del
  tablero y, si ahí no cabe, desplazado a saltos de 3 m hasta media parte. El primero mide 11-15 m y los demás
  9-13 m; entre dos quedan al menos 7 m de tablero entero para aterrizar y coger carrerilla, y el tipo va rotando
  (un puente de tres los lleva de los tres tipos). Solo
  donde debajo no hay nada en 9 m (ni pilas ni torres), todo el tramo cae sobre cajas de muerte y no hay nada del
  recorrido a menos de 5 m (plaza, huevos, recompensas, medusas). Cada uno se cruza de una de tres maneras: **vigas** de 60 cm
  en zigzag de lado a lado con plataformas en los codos, **postes** cuadrados de 1,1 m al tresbolillo (saltos de
  ~1,3 m y cimas alternas a -8 y -26 cm) o dos **cornisas** de 60 cm por los bordes, cada una con un hueco de 1,8 m (a
  un tercio y a dos tercios) y un tablón atravesado en medio para cambiar de lado. Todo lo pisable queda a menos de
  50 cm bajo el tablero (las cajas de muerte empiezan a 60 cm). Bordes astillados (sillares en los de piedra;
  tablones que cuelgan en los demás), barandillas cortadas y bandas de aviso antes de cada borde. El registro dice,
  por cada tramo, «Cruce N: tramo hundido de X m (tipo K, i de n)».
- **Adarve roto de las murallas colosales** (`TNProcMap::BuildWallBreaches`, `EFeature::WallBreach`,
  `WallBreachDims`): la muralla tiene **mordiscos solo en lo alto**. Donde muerden faltan el parapeto (y sus almenas)
  y el adarve, y el muro queda hundido 2,8-4,8 m (siempre por encima de la clave de la puerta, `WallDims::Crown` = 5 m);
  el cuerpo de la muralla sigue entero y desde lejos se ven los bocados en la silueta almenada. Tipos:
  - **Brecha** de lado a lado que se salta: 1,19-1,66 m en Fácil, 1,33-1,9 m en Normal y 1,5-2,22 m en Difícil
    (`GapMin/GapMax` = `LerpD(110, 155)` / `LerpD(150, 230)` por dificultad). A veces dos o tres seguidas (Fácil, hasta
    dos) con adarve entero entre ellas (`Island` = `LerpD(260, 170)`: 2,4 m en Fácil, 2,15 m en Normal y 1,8 m en
    Difícil, ±25 cm).
  - **Cornisa**: el adarve se hunde salvo una franja pegada a un parapeto, que sigue entero; 85 / 78 / 68 cm de ancho
    (`LedgeWidth` = `LerpD(90, 66)`; nunca menos de 65 cm) y 4,9-7,8 m (Fácil), 5,5-9 m (Normal) o 6,3-10,6 m (Difícil)
    de largo.
  - **Cornisa y brecha**: una cornisa y, tras 2,4-3,6 m de adarve entero, una brecha (desde Normal).
  - Por muralla, como mucho 3 / 5 / 7 grupos, con 28-47 m (Fácil), 22-39 m (Normal) o 15-29 m (Difícil) de adarve entero
    entre grupos; los tipos salen al 55/45/0 % en Fácil y al 45/35/20 % desde Normal. Nunca a menos de 9 m del borde de
    una torre (`TowerClear`; el enlosado y los lados abiertos del pretil, `TowerOpenSides`, quedan intactos) ni de 6 m
    del arco de la puerta (`GateClear`), y solo en tramos cuyo eje gira menos de 20°.
  - **Medidas con el salto real** (`TNProcMap::TurtleJump`, valores de `BP_TortugaCharacter`): `JumpZVelocity`
    485 cm/s y gravedad 980 cm/s² (sube 1,2 m y está 0,99 s en el aire), andar 200 cm/s y esprintar 400 cm/s
    (`UTN_StaminaComponent`; 200 de estamina a 15/s son más de 13 s de esprint): 1,98 m de salto en llano andando y
    3,96 m esprintando, más ~1 m si se hace el panzazo en lo alto. La brecha más larga (2,3 m a dificultad 1) es el
    58 % de un salto esprintando; hasta Normal (1,9 m) se salta sin esprintar. La cápsula mide 34 cm de radio: en una
    cornisa de 65 cm cabe entera arrimada al parapeto.
  - **Forma**: el fondo del mordisco lleva un escalón más somero junto a cada borde (12-22 % del largo, a
    30-60 % de la hondura y nunca a menos de 1,1 m bajo el adarve) y el parapeto se rompe 30-110 cm más que el
    adarve, con un resto de 8-22 cm de alto junto a la rotura (desde él no se alcanza la cima del parapeto, a más de
    1,2 m). Sillares caídos en el fondo y en los escalones y, antes de cada grupo, a veces (60 %) un trozo de almena
    caído sobre el adarve junto a un parapeto (1,2 × 0,8 × 0,54 m; en una cornisa, del lado del parapeto roto). La
    piedra rota es más oscura que la labrada. Malla por bandas (`TNProcAddWall`): parapeto izquierdo, adarve (o la
    franja que queda y la hundida) y parapeto derecho, cada una a la cota de lo que queda, con sus paredes y las caras
    del corte; las caras exteriores suben hasta lo que queda. La colisión es la de la malla: los huecos son de verdad.
  - **Caer mata**: cajas de muerte desde 60 cm bajo el adarve (como en los puentes) hasta 3 m bajo el fondo, a lo ancho
    de todo el muro y 9 m más allá de cada cara (quien salta hacia fuera por el mordisco) y a lo largo del tramo, su
    parapeto roto y 6 m más, en piezas de 4 m que siguen el eje y se solapan 60 cm. Lo pisable (la cornisa, el adarve
    entero, los restos del parapeto) queda por encima de ellas.
  - **Barreras invisibles**: sobre cada parapeto entero, de su cima a 9 m más arriba y hasta 60 cm por fuera de su
    cara (piezas de 6 m, perfil `InvisibleWall` sin bloquear la cámara, en `BoundaryWalls`): nadie se sube al parapeto
    (ni lanzado por un compañero) para rodear una cornisa o salir de la muralla. Se cortan donde el parapeto está roto:
    no tapan los mordiscos ni dejan andar por el aire.
  - Las muestras del adarve que tocan un mordisco llevan `PathFlags::Gap` (sin suelo continuo; ya eran `Elevated`, así
    que no llevan huevos, peligros, obstáculos ni fauna). La reaparición es en las pilas de huevos, nunca en el adarve.
    El registro dice «Cruce N: adarve roto con X mordiscos (B brechas y C cornisas)».
- **Formaciones temáticas**: arcos que cruzan el camino (arco de roca, esqueleto de ballena con
  columna en arco sobre las costillas, cola y cráneo con mandíbulas,
  raíces gigantes, pórtico de templo, tronco colosal caído con raíces y lianas en selva y manglar,
  acueducto en ruinas en desierto, roca y zona humana, aquí la mitad de las veces), piezas en
  explanadas (barco varado, cabeza colosal, basalto, fumarola, chimeneas de hadas, rocas en
  equilibrio, carreta, cañón, caracola gigante y ancla en la playa, círculo de piedras que se cruza
  entre sus piedras, obelisco y cráneo fósil en el desierto, agujas de obsidiana en el volcán, tortuga
  colosal de piedra, depósito de agua y, de guerra, sacos terreros, búnker, torre de vigía y carro de
  combate) e hitos lejanos (pirámide, faro, farallones, mesas, castillo en ruinas, molino,
  palafitos). La pieza de explanada se sortea primero y espera hasta 40 m a un tramo donde quepa, así
  las grandes salen en los anchos.
- **Cuevas** (1-5 por mapa en volcán, roca, selva y desierto): túneles de 120-260 m, donde se
  puede en tramos que cruzan terreno alto (el paisaje a 30 m de los dos bordes, 12 m por encima
  del camino). Encima, una montaña de cima irregular que crece hacia el centro de las largas. El terreno no
  puede tener techo, así que sobre el túnel una **tapa de montaña** une las laderas de los dos lados a su altura
  (a 7 m del borde del paso), con una loma y los colores del bioma, donde la montaña queda por encima del techo de
  roca. Así la montaña sigue por encima de la cueva en vez de quedar cortada a lo largo del camino. A cada
  boca se llega por un desfiladero de 25-40 m de paredes a plomo que pierde altura hasta 7 m y acaba
  en frente empinado (a la montaña no se sube). El camino se estrecha a 11-15 m en la boca; dentro,
  pasos de 4-7 m y una o dos cámaras de 16-26 m; en las del volcán, un río de lava cruza la primera
  (se salta; caer mata). Interior según el estilo (`TN_ProcMapCaveDecor.h`): **caliza** (estalagmitas,
  columnas, estalactitas grandes, poza, lucernario), **cristales** (racimos gigantes que brillan, con
  colisión), **selva** (raíces por la bóveda, lianas, setas luminosas, musgo, cortina de lianas en las
  bocas), **templo** (pilares, pilastras, antorchas con brasas, vasijas y huesos, portada tallada con la
  cabeza de la tortuga) y **tubo de lava** (obsidiana, basalto, grietas incandescentes). En todas, la
  estatua de la tortuga con ofrendas en la cámara más ancha, estelas y símbolos (tortuga, espiral,
  sol, olas, ojo, panal) pintados o luminosos, y luces sin sombras del color del estilo. Nada invade
  el carril central. Las del volcán, donde no pisa otros caminos ni torres, van **dentro de un volcán**
  de 70-150 m de base con su cráter y lago de lava (el túnel atraviesa su base y los desfiladeros se
  abren en su ladera) y llevan una **cámara de magma**: lago de lava a un lado del camino (mata al
  tocarlo) con anillo de basalto, coladas encendidas por la pared, grieta en la clave, brasas y luz
  fuerte; la estatua, al otro lado.
- **Viento** en la vegetación (`M_ProcFoliage`, SimpleGrassWind del motor con rachas): el alfa del
  color de vértice es el peso de balanceo (hierba entera, copas más que troncos, rocas y objetos
  quietos); cada especie deja de evaluarlo a su distancia.
- **Géiseres low-poly**: montículo de sínter en terrazas (anaranjado, crema y blanco) con poza
  turquesa y boca oscura. La columna de agua (material de agua que corre hacia arriba, blanca y
  opaca en lo alto) sube de golpe hasta 10,5 m, se sostiene temblando, baja y queda borboteando a
  2,6 m, en ciclos de 4,2 s; al lanzar a alguien vuelve a arrancar. Corona de espuma que va con la
  cima y anillo de espuma en la boca (bolas de caras planas), gotas que saltan de lo alto y caen
  alrededor, gotas que suben pegadas a la columna, espuma arriba y abajo, salpicaduras y bruma,
  todo al ritmo del chorro. En la torre hueca la columna llega a asomar por el hueco del forjado.
- **Cascadas-tobogán**: lámina de agua con UV de flujo (`M_ProcCascade`, ondas que corren ladera
  abajo), en rejilla de 30 × ~60 cm con cada vértice 25 cm sobre el punto más alto del terreno de sus
  cuadros vecinos (el terreno nunca asoma), espuma en los bordes, en el labio (el primer metro y
  medio, con espuma y gotitas que se asoman) y al pie. Abajo, una poza pegada a la cascada
  (`TNProcMap::SlidePoolOf`, la misma cuenta para el terreno, la malla y los efectos): el agua
  queda a ras del suelo donde llega la lámina (12 cm por debajo) y el fondo baja un metro justo donde
  cae la tortuga y sube suave hasta la orilla (`PoolDims`, influencia `Pool` del terreno). Sus UV
  salen del punto donde cae el agua, así que las ondas del material corren desde el impacto hacia
  fuera, y además salen anillos de onda que se abren desde ahí y se hunden al final; salpicaduras,
  espuma y bruma, todo a la cota del agua.
- **Efectos ambientales** (`TN_ProcMapAmbientFX.h`, solo visuales y locales): partículas que son
  instancias de mallas low-poly (gotas, vapor, brasas), dormidas lejos de la cámara; brasas sobre los
  lagos y ríos de lava; bandadas de gaviotas en la costa y la meta, guacamayos en la selva, pájaros
  sobre bosques y roca y buitres en el desierto (`M_ProcBird`, aleteo por el alfa del vértice).
- **Agua animada** (`M_ProcWaterAnim`): ondas en dos capas que se desplazan, color de somera a
  profunda, espuma en las orillas; más clara y rápida en los toboganes.
- **Fauna** (`ATN_ProcFauna`, `TN_ProcMapFaunaMeshes.h`; solo visual y local): 31 especies low-poly
  por bioma (monos, tucanes y ranas en la selva; cangrejos, gaviotas y tortuguitas en la playa;
  suricatos, lagartijas y correcaminos en el desierto; salamandras y escarabajos de fuego en el
  volcán; peces, pelícanos y flamencos en el agua; cabras y águilas en la roca; garzas y cangrejos
  violinistas en el manglar; gatos, palomas y gallinas en la zona humana), hasta 900 a la vez
  (`Density` 2,6).
  - Van pegadas a los caminos: bastantes en el propio camino y, de las demás, dos de cada tres a menos de 9 m de
    su borde. El resto queda hasta 25 m (60 m las de agua).
  - Dejan acercarse a la mitad de su distancia de alarma de la tabla y huyen al 75 % de su velocidad, así que se
    las ve escapar: trepan paredes, vuelan, se entierran o se meten en el agua.
  - Reaparecen por delante. Consola: `TN.Fauna.Enable`, `TN.Fauna.Stats`.
- **Tos en la tormenta** (`UTN_StormCoughComponent`, sintetizada, sin archivos de audio). Cada tortuga que está
  dentro de la tormenta tose, y cada una tiene su voz.
  - Al entrar, carraspeos sueltos.
  - Con el tiempo dentro, ataques de tos cada vez más seguidos, con jadeos. Al salir, un último carraspeo. Al
    morir, calla.
  - `ATN_PathStorm::TickCough` le pasa cada 0,1 s si está dentro y cuánto le falta para morir.
  - Prueba sin tormenta: `TN.Storm.Cough <0|1|2>` (apagado, carraspeo o tos fuerte).
- **Sonido ambiente sintetizado** (`TN_AmbientSynthComponent`, `UTN_AmbientSoundscapeComponent` en
  el PlayerController; sin archivos de audio): capas por bioma (viento, oleaje, aves, cigarras,
  grillos, ranas...) que cambian en degradado, tormenta, cuevas amortiguadas, y fuentes 3D en los
  géiseres (siguen el chorro), las cascadas y la lava. Para sustituirlo por sonidos de verdad:
  `TN_AmbienceDataAsset`. Consola: `TN.Ambience.Debug`, `TN.Ambience.Volume`.
  - Fauna esporádica, para que el fondo no canse: las aves llaman con la mitad de frecuencia que al principio (selva
    0,55 llamadas/s, antes 1,3), responden el 20 % de las veces (antes 30 %) y, tras un 20 % de las llamadas, callan
    5-14 s más. Cigarras, grillos y ranas bajan un 40 % y cantan a ratos (`FChorusGate`): cigarras 8-20 s con silencios
    de 10-25 s, grillos 6-16 s / 8-20 s, ranas 5-12 s / 10-25 s (y la mitad de croares), con fundidos de 2,5 s. Las
    campanas de la zona humana, de 0,8 a 0,45.
- **Música de fin de partida** (`UTN_MatchMusicSubsystem`, sintetizada en `TN_MusicSynthDSP.h`). Suena en 2D para el
  jugador local.
  - Tres pistas: victoria (si bemol mayor, 120 BPM), derrota (re menor, 72 BPM) y una cortinilla de eliminado.
  - Las decide `TNMatchMusic::FDirector` (`TN_MatchMusicDirector.h`) a partir de estados que ya se replican: flujo de
    la partida, resultados, llegada, eliminación y rondas ganadas. No hay RPC.
  - Se funde a silencio al final de la cuenta atrás de resultados y se para al cambiar de mapa.
  - Consola: `TN.Music.Play Victoria|Derrota|Eliminado|Tienda|Probador|Silencio` y `TN.Music.MatchVolume`.

---

## 4. Modos de juego

| | Coop | Carrera | 2vs2 |
|---|---|---|---|
| Jugadores | 1–4 | 1–4 | exactamente 4 (si no, se juega Carrera) |
| Ronda | todos llegan a la meta; tormenta por el camino | el primero en la meta gana la ronda | gana la pareja cuyos **dos** miembros llegan antes |
| Partida | `CoopRounds` (por defecto 1 = la partida) | primero en `WinsToWinMatch` (3) | victorias por jugador, primero en 3; parejas rotan AB\|CD → AC\|BD → AD\|BC |
| Mapa | largo (3×3 / 6×6 / 8×8) | corto (2×2 / 3×3 / 4×4) | corto con carriles |

- **Reaparición**: morir no elimina mientras haya una pila de huevos alcanzada
  (Coop: la más lejana del equipo; Carrera/2vs2: la del propio jugador) que quede
  por delante de la tormenta. Sin pila válida, muerte normal con rescate de compañero.
- **Rondas**: cada ronda genera un mapa nuevo (`bRegenerateEachRound`) y no arranca
  hasta que todos los clientes avisan de que lo tienen construido (o vence
  `MapReadyTimeoutSeconds`). Entre rondas el flujo pasa a *Countdown* con la cuenta
  atrás en `CountdownValue`; el resultado queda en `ATN_ProcMapGameState::RoundResultText`
  (con el delegate `OnRoundInfoChanged`) para el HUD, que aún no tiene widget propio.
- Carrera y 2vs2 tienen límite por ronda (`CompetitiveRoundTimeLimitSeconds`, 15 min):
  al agotarse gana el más adelantado por el camino.
- La tabla final de Carrera/2vs2 reutiliza el widget de resultados: puesto por
  rondas ganadas, con las victorias en la columna de puntos.

### Salida: puerta doble o huevos

Cada ronda empieza dentro de la misma pieza en la que los jugadores se pusieron listos en el lobby
(`ATN_ProcStartStructure`). Se construye con `TNCastleKit` (`Private/Lobby/TN_CastleKit.h`), el kit de
`ATN_SandCastleLobby`, así que la geometría es idéntica.

- **Puerta doble** (`ETNMatchStartStyle::Gate`): la sala entre dos puertas, al fondo del claro de salida, con la
  puerta 1 contra el talud (parece que se sale de la pared) y la puerta 2 mirando al camino. Muros, pilares, torres y
  zócalo bajan 4,5 m bajo el suelo por si el terreno no es plano. Hay ocho sitios dentro, en dos filas de cuatro a 2,3 m
  unos de otros (`Gatehouse::SpawnSpot`). Al abrirse, las hojas de
  la puerta 2 giran 100° hacia fuera en 1,25 s y su bloqueo invisible desaparece en cuanto empiezan a girar; la
  puerta 1 no se abre nunca. Lleva el rótulo «TORTUNAVY» en la cara de fuera de la puerta 2 y una luz cálida en la sala.
- **Huevos** (`ETNMatchStartStyle::Eggs`): el montículo de dos alturas con la pila de ocho huevos (siete abajo y uno
  arriba) y el escalón hacia el camino. Cada jugador aparece dentro de un huevo con la tapa puesta; una pared invisible lo sujeta hasta que se
  rompe. Los huevos se rompen uno tras otro, cada 0,12 s: la tapa salta dando vueltas, se posa y se esfuma.
  - **Pausa de 1 s en el huevo** (`TNEggHatch`, `World/TN_EggHatch.*`, la misma pieza que la salida de la carrera): la
    tortuga se queda quieta en su huevo roto; se agacha un instante y se pone de pie de un estirón, se sacude la cáscara
    (giros rápidos del cuerpo y trocitos de cáscara del color de su huevo, con un crujido) y gira hacia el camino (en la
    máquina de su jugador, también la cámara); justo antes del salto se encoge un poco. En caparazón, tumbada o en
    ragdoll no hay pose.
  - Después, 1 s tras romperse su huevo, sale despedida (`LaunchCharacter`, 3,8 m/s en horizontal, hacia fuera de la pila
    y hacia el camino, y 6,2 m/s hacia arriba). Con el reloj del servidor (`OpenServerTime`, replicado): la pose va igual
    en todas las máquinas, y la sujetan (`MOVE_None`) y la lanzan a la vez el servidor (a todas) y el cliente dueño al
    recibir `bOpen`, como en el probador. A un cliente que lo recibe más de ~2,9 s tarde le sale ya abierta, sin pausa ni
    salto.

**Colocación** (`ATN_ProcMapGenerator::SpawnStartStructure`, servidor, en cada generación). «Hacia el camino» es la
dirección del punto de salida a la primera muestra del camino que queda fuera del claro. La estructura se coloca
detrás, con su +Y local mirando al camino:

- Puerta doble: el umbral de la puerta 1 a `StartClearingRadius − 1,5 m` del centro del claro.
- Montículo: su centro a `min(StartClearingRadius − 6,5 m, 13 m)`.
- Cota: la más alta del terreno bajo el suelo de la sala (el terreno no asoma por él) o la más baja bajo el
  montículo (no queda flotando).

Se crea diferida, con el estilo puesto antes de su `BeginPlay`. Va en `SpawnedActors`, así que se destruye al
regenerar, como las pilas de huevos. No se crea en modo solo terreno ni si ningún GameMode la pide
(`SetStartStructureStyle`).

**Aparición.** Con estructura, `GetStartTransform(0..7)` y los PlayerStart 0–7 (etiqueta `TNProcStart`) quedan
dentro de ella, a 1,1 m del suelo como los del anillo: los ocho sitios de la sala o los ocho huevos
(`ATN_ProcStartStructure::NumSpots`, atado con `static_assert` a `Gatehouse::NumSpawnSpots` y a `EggMound::NumEggs`).
El anillo del claro (ocho sitios) solo se usa sin estructura.

- `ChoosePlayerStart` da a cada jugador el sitio de su slot (índice en `PlayerArray`) o, si está ocupado, el
  siguiente libre de la estructura (durante el viaje sin cortes los slots aún se reordenan).
- Al empezar la ronda, `PlacePlayersAtStart` lo deja de pie en su sitio con la altura de su cápsula
  (`GetSpawnTransform`). Hasta entonces sigue congelado, como siempre.
- En el cliente, el suelo de la estructura cuenta como suelo del mapa para soltar el peón (`MapCollisionUnder`).
- Sin estructura, todo funciona como antes.

**Apertura.** `BeginRoundPlay` programa `ATN_ProcStartStructure::Open` a `StartStructureOpenDelaySeconds`
(1,2 s), a la vez que el «¡ADELANTE!» de la pantalla de carga. Solo se replican el estilo, `bOpen` y desde cuándo
(`OpenServerTime`, hora del servidor). Quien recibe la estructura ya abierta la ve abierta del todo y no salta. Una
ronda nueva sin regenerar el mapa la cierra (`Close`).

**Del lobby al mapa.** `ATN_HQGameMode::BeginMatchTravel` guarda `ATN_SandCastleLobby::GetStartStyle()` en
`UMP_GameInstance::PendingStartStyle` y añade `?ProcStart=Gate|Eggs` a la URL del viaje. `GetStartStyle()` elige
según dónde haya más jugadores listos, en la sala o en los huevos; si empatan, la puerta.
`ATN_ProcMapGameMode::ResolveStartStyle` mira en cada generación la GameInstance y después la URL (sin nada, la
puerta doble). El resultado queda en el log: `[ProcMap] Salida: puerta doble` o `huevos`.

**Para probar.** La variable de consola `TN.Proc.StartStyle` manda sobre todo lo anterior y vale desde la siguiente
generación: −1 = lo del lobby (por defecto), 0 = puerta doble, 1 = huevos. Sin lobby también sirve
`open LVL_ProcMap?ProcStart=Eggs`. Con los huevos, `TN.Proc.Egg` (en el anfitrión; `ATN_ProcStartStructure::ReplayEggs`)
los cierra otra vez con cada tortuga dentro del suyo y a los 1,5 s los vuelve a romper: para ver la pausa en el huevo
sin regenerar el mapa.

---

## 5. Personaje

- **Nado** básico: `SwimSpeed` 625 cm/s (entre andar y esprintar), flotabilidad 1,08;
  se flota con medio cuerpo fuera. *Saltar* nadando da un impulso para subir a orillas
  e isletas (el movimiento del motor no salta en el agua).
- **Coger y lanzar** (`UTN_CarryComponent`): solo se coge a una tortuga metida en
  su caparazón o aturdida (a cualquiera, también rivales), con *Interactuar* cuando
  no hay otro interactuable delante. *Interactuar* lanza hacia donde mira la cámara;
  *Soltar objeto* la deja delante. Si la llevada intenta moverse 2 s seguidos se
  libera; mientras forcejea al portador le tiembla la cámara y su lanzamiento pierde
  fuerza. En el aire la lanzada no puede salir del caparazón: al tocar suelo rebota
  en vertical, se estira durante el rebote y aterriza de pie.
- **Caídas**: más de 5 m de caída libre → se mete sola en el caparazón; más de 35 m →
  se rompe (muere). Géiseres, toboganes y el agua no cuentan.
- **Salto** (`BP_TortugaCharacter`, lo recoge `TNProcMap::TurtleJump` para medir los retos del mapa): `JumpZVelocity`
  485 cm/s con la gravedad del motor (980 cm/s²): sube 1,2 m y está 0,99 s en el aire; andando (200 cm/s) salta
  1,98 m en llano y esprintando (400 cm/s), 3,96 m. El segundo salto en el aire es el panzazo (350 cm/s más la
  velocidad del salto, bajando a 100 cm/s). Cápsula de 34 cm de radio y 70 de semialto.

---

## 6. Red

Solo se replica `FTNProcMapNetConfig` (semilla, modo, dificultad y número de
generación). Cada máquina genera el mismo mapa en local; los actores que afectan
al movimiento (géiser, tobogán, agua, corrientes, remolinos, zonas de muerte) se
crean en todas las máquinas para que la predicción del cliente cuadre, y los que
tienen estado (enemigos, huevos, puzles, meta, PlayerStarts) solo en el servidor
y se replican. Mientras un cliente no tiene su mapa, su pawn queda congelado; al
terminar avisa con `AMP_GamePlayerController::ServerReportProcMapReady`.

Las conchas de puntos las pone el servidor y se replican (siempre relevantes, a 1 Hz porque no se mueven; el valor,
`ScoreValue`, llega con el actor y decide su aspecto). Al cogerlas no se usa el multicast de la concha, que se perdería
al destruirla, sino el del PlayerState del jugador (`MulticastScoreShellCollected`, fiable): estallido y «¡plin!» en
todas las máquinas y la animación del contador solo en la suya.

---

## 7. Parámetros por defecto (`TN_MakeDefaultProcProfile`)

Editables en `DA_ProcMapSettings → Profiles` (el script los rellena; *FillDefaultProfiles*
los restaura).

| Modo | Rejilla F/N/D | Cobertura | Cruces F/N/D | Ramas F/N/D | Carriles | Tormenta cm/s F/N/D (gracia s) |
|---|---|---|---|---|---|---|
| Coop | 3 / 6 / 8 | 0,78 | 1 / 2 / 4 | 6 / 12 / 16 | 0 | 160 / 180 / 200 (90 / 60 / 45), como mucho la velocidad de andar |
| Carrera | 2 / 3 / 4 | 0,90 | 0 / 1 / 1 | 4 / 7 / 9 | 0 | — |
| 2vs2 | 2 / 3 / 4 | 0,90 | 0 / 0 / 1 | 2 / 3 / 4 | 1 / 2 / 3 | — |

Comunes por dificultad (F/N/D): densidad de peligros 1,6 / 2,4 / 3,2; huecos por km
9 / 13 / 17 (antes 6 / 9 / 12; densidad real en los tramos donde caben, a 30 m como mínimo entre sí); una pila de
huevos cada 1 / 2 / 3 cruces de módulo. El camino va
muy poblado de saltos, trampas y obstáculos de juego. `DA_ProcMapSettings` guarda sus perfiles: al cambiar estos
valores en código hay que actualizarlos en el asset (`FillDefaultProfiles` o por Python) y guardarlo.

> **Duración**: con 400 m por módulo, el Coop 6×6 por defecto sale en torno a
> 35–40 min a 5,5 m/s, por encima de los 10–20 min objetivo. Se dejó así a
> propósito para probar; para acercarse al objetivo basta con bajar `GridSize` a 5,
> `Coverage` o `Sinuosity` en el perfil. El log de cada mapa da los minutos estimados.

---

## 8. Límites conocidos

- **Nanite** no aplica a mallas generadas en runtime (`UProceduralMeshComponent`);
  sí a las mallas de vegetación que se asignen en los biomas.
- El **PCG** es un gancho: si un bioma tiene `PCGGraph`, se ejecuta sobre el mapa
  generado. No hay grafos incluidos.
- Vegetación, formaciones y cuevas son **low-poly procedural** con color de vértice; las
  capas de formas básicas de los biomas quedan para props del borde del camino y de la zona
  humana (o para mallas de arte que se asignen en los DataAssets).
- La **vegetación** no tiene colisión (crece fuera del suelo del camino) y usa culling por
  tamaño; con ~500 mil instancias conviene vigilar el rendimiento en equipos modestos
  (`FloraDensity` la reduce).
- Los materiales `M_ProcTerrain`, `M_ProcFoliage`, `M_ProcWaterAnim`, `M_ProcCascade`, `M_ProcFXSoft`, `M_ProcGlow` y
  `M_ProcBird` se crean con `Scripts/build_procmap_assets.py` (idempotente; rehace `M_ProcFoliage` si no tiene viento
  y `M_ProcTerrain` si no tiene normal). Con `PROCMAP_SKIP_MAIN = True` antes del `exec` el script solo define las
  funciones y no cambia de nivel, para rehacer un material suelto (`build_terrain_material(rebuild=True)`).
