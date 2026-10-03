# Estrés «caos»: peor caso de juego real (2026-10-03, #585)

Pregunta del director: cuánto cae el fotograma (p50/p95/p99 de juego, render y GPU) y cuánto sube la memoria del proceso
cuando cuatro tortugas usan catapultas, se cogen y se lanzan, ruedan en su bola y lanzan objetos en ráfagas, con
cangrejos, gaviotas y tanques encima; y qué lo causa.

**Conclusión**: en la playa, con escalabilidad Épica a 1080p, el pico cuesta +2,3 ms de p50 (11,9 → 14,2 ms) y +3,9 ms
de p95 (14,4 → 18,3 ms): el juego aguanta >60 fps en este PC. Lo que más sube es el **hilo de juego** (8,1 → 13,9 ms),
sobre todo por **recrear el estado de render de componentes en cada fotograma** (ISM animados) y el Tick de los enemigos;
en el render, los **~3 000 dibujos de mallas de tiempo de ejecución**. La GPU apenas se mueve con el caos (9,9 → 11,2 ms):
la manda la escalabilidad (Lumen, TSR). En **Medio**, GPU 4,7-5,9 ms y VRAM 1,9 GB (frente a 10-12 ms y 3,3 GB), y el
cuello pasa a ser el hilo de juego (12 ms en el pico). El **mapa del Coop** es otra liga: 12 GB comprometidos, 4-6,7 GB de
RAM y **700-1 000 correcciones de red por fase** con un solo cliente.

## Método

- Escenario nuevo `TN.Stress caos` (`Testing/TN_StressChaos*`, `TN_StressChaosPlan.h`): siete fases de 20 s que suman
  carga — referencia (andar), + catapultas (8 reutilizables a 10-18 m), + coger y lanzar, + bola, + ráfagas de objetos
  (mina, disco, cangrejo teledirigido, nube, gaviota, coco y los lanzables de `DT_Items`: coco y tinta; uno por tortuga
  cada 1,2 s), + enemigos (12 cangrejos gigantes y ermitaños, 4 zonas de gaviotas y 4 tanques a 8-40 m) y pico (otros
  tantos enemigos, 4 catapultas más y ráfagas cada 0,6 s).
- Cuatro tortugas locales del anfitrión (jugadores extra con la pantalla partida **apagada**: la GPU pinta una vista, como
  en el PC de cada jugador) que juegan con las funciones de los Input Actions (`Move`, `ToggleShell`, `TryGrabNearest`/
  `TryInteract`, `TryUseEquippedItem`). Los objetos se los da el anfitrión como una caja. Única ayuda: si una tortuga no
  llega al cazo de una catapulta en 3 s, se la deja caer dentro (columna «ayudas»).
- Un cliente `-nullrhi` con 100 ms y 1 % de pérdida (`red_local.py` de la PR #388, copia sin commitear, `--clientes 1`)
  con `-TNStress=caos`: su tortuga juega igual por red. `IpNetDriver` a 60 Hz y `MaxClientRate=200000` por línea de órdenes
  (en `dev` aún no está la sección del `.ini`).
- Anfitrión con ventana 1920×1080, `t.MaxFPS 0`, editor **Development** (`UnrealEditor.exe -game`; DebugGame exagera la
  CPU). PC: i9-13950HX + RTX 4060 Laptop (8 GB), 32 GB compartidos con otros agentes y el editor de Rodrigo: un monitor
  (`nvidia-smi`, RAM libre, compilaciones ajenas cada 2 s) marca las medidas contaminadas.
- Fotograma = tiempo entre ticks; GT/RT/RHI = `GGameThreadTime`/`GRenderThreadTime`/`GRHIThreadTime`; GPU =
  `RHIGetGPUFrameCycles`. RAM = conjunto de trabajo; «commit» = memoria comprometida (`UsedVirtual`, no baja cuando Windows
  recorta el conjunto de trabajo); VRAM = `QueryVideoMemoryInfo` del proceso (DXGI). Los 1,5 s primeros de cada fase no
  cuentan en los percentiles.
- Traza aparte (`-trace=cpu,frame,gpu,memtag,bookmark,region,log -statnamedevents`; con `memory` el servidor no llegó a
  escuchar en 180 s) y análisis sin interfaz con `UnrealInsights -NoUI -AutoQuit -ExecOnAnalysisCompleteCmd=@=<órdenes>`
  (`TimingInsights.ExportTimerStatistics -region=TNChaos_* -threads=...` y `ExportTimerCallees`). Cada fase es una región
  `TNChaos_<fase>`. La traza infla el hilo de juego (~+50 %): sus cifras sirven para ordenar, no como absolutos.

## Playa (`LVL_BeachRace`), Épico, Development

Ejecución limpia (sin compilaciones ajenas, ≥10,7 GB libres). ms; KB/s de salida por conexión.

| Fase | Fotograma p50/p95/p99 | Juego p50/p95 | Render p50/p95 | GPU p50/p95 | RAM | Commit | VRAM | KB/s med./máx. | Corr. |
|---|---|---|---|---|---|---|---|---|---|
| Referencia | 11,9 / 14,4 / 15,4 | 8,1 / 10,0 | 11,9 / 13,8 | 9,9 / 11,6 | 3 407 | 6 507 | 3 278 | 30 / 56 | 0 |
| + catapultas | 12,5 / 15,0 / 16,4 | 9,1 / 11,1 | 12,5 / 14,4 | 10,6 / 12,2 | 3 421 | 6 530 | 3 280 | 20 / 52 | 0 |
| + coger y lanzar | 12,9 / 15,0 / 16,3 | 8,8 / 10,5 | 12,8 / 14,6 | 11,1 / 12,6 | 3 423 | 6 566 | 3 281 | 37 / 73 | 2 |
| + bola | 11,8 / 20,4 / 33,5 | 9,5 / 11,5 | 11,7 / 20,4 | 9,6 / 18,2 | 3 464 | 6 562 | 3 294 | 34 / 72 | 5 |
| + objetos | 13,5 / 16,2 / 17,8 | 10,2 / 12,5 | 13,4 / 15,5 | 11,4 / 12,9 | 3 472 | 6 633 | 3 358 | 25 / 56 | 44 |
| + enemigos | 13,9 / 17,3 / 19,5 | 12,8 / 15,8 | 13,8 / 16,5 | 11,6 / 12,8 | 3 477 | 6 636 | 3 358 | 30 / 59 | 36 |
| Pico | 14,2 / 18,3 / 21,0 | 13,9 / 17,9 | 13,6 / 16,5 | 11,2 / 12,6 | 3 510 | 6 643 | 3 327 | 33 / 53 | 0 |

RAM, commit y VRAM en MB al acabar la fase. Actores: 718 → 783; con Tick: 259 → 326. Acciones en las 7 fases: 71 entradas
en la bola, 3 agarres y lanzamientos, 6 viajes en catapulta (2 disparos registrados, 13 ayudas) y 112 objetos dados y 154
usos (contando los intentos). El cliente: fotograma 2,2 → 4,8 ms, 27 correcciones en total.

## Playa, Medio (`sg.*=1`), Development

| Fase | Fotograma p50/p95/p99 | Juego p50/p95 | Render p50/p95 | GPU p50/p95 | RAM | Commit | VRAM | KB/s | Corr. |
|---|---|---|---|---|---|---|---|---|---|
| Referencia | 7,1 / 8,9 / 9,8 | 6,9 / 8,8 | 6,4 / 8,2 | 4,7 / 5,4 | 3 305 | 5 023 | 1 888 | 28 / 56 | 0 |
| + catapultas | 8,2 / 10,5 / 11,8 | 8,1 / 10,4 | 7,4 / 9,6 | 5,3 / 6,4 | 3 311 | 5 061 | 1 888 | 27 / 56 | 0 |
| + coger y lanzar | 8,1 / 10,2 / 10,9 | 8,1 / 10,0 | 6,9 / 9,1 | 4,9 / 5,9 | 3 320 | 5 074 | 1 888 | 17 / 41 | 1 |
| + bola | 7,9 / 9,9 / 10,7 | 7,8 / 9,8 | 6,7 / 8,7 | 5,0 / 5,9 | 3 332 | 5 093 | 1 888 | 21 / 48 | 8 |
| + objetos | 8,7 / 11,2 / 12,8 | 8,8 / 11,1 | 7,4 / 9,3 | 5,6 / 6,4 | 3 369 | 5 248 | 2 016 | 23 / 45 | 52 |
| + enemigos | 10,5 / 13,1 / 14,3 | 10,5 / 13,0 | 8,4 / 10,2 | 5,9 / 6,5 | 3 377 | 5 256 | 2 016 | 24 / 59 | 46 |
| Pico | 12,0 / 14,2 / 15,9 | 12,0 / 14,3 | 8,1 / 10,1 | 5,2 / 5,9 | 3 394 | 5 281 | 2 016 | 40 / 60 | 7 |

En Medio el caos cuesta lo mismo en el hilo de juego (+5,1 ms de p50) y ya es el cuello: en una CPU con la mitad de
rendimiento por núcleo (estimación, no medido) el pico rondaría 24 ms (~40 fps).

## Mapa del Coop (`LVL_ProcMap`), Épico, Development

Contaminada: la RAM libre del PC bajó a 0,1 GB (otro agente compilando) y Windows recortó el conjunto de trabajo (RAM
6,5 → 3,1 GB en la referencia). El commit no se recorta y es fiable. La tormenta del Coop se para durante la prueba (si
no, mata a las tortugas a los ~2 min y la partida viaja al lobby).

| Fase | Fotograma p50/p95/p99 | Juego p50/p95 | Render p50/p95 | GPU p50/p95 | Commit | VRAM | KB/s | Corr. |
|---|---|---|---|---|---|---|---|---|
| Referencia | 15,8 / 25,2 / 83,2 | 5,8 / 9,0 | 15,9 / 25,5 | 12,0 / 14,2 | 12 244 | 4 303 | 31 / 50 | 906 |
| + catapultas | 15,3 / 41,4 / 133,8 | 6,5 / 10,9 | 15,2 / 38,0 | 12,4 / 30,3 | 12 087 | 3 919 | 15 / 71 | 698 |
| + coger y lanzar | 13,7 / 39,2 / 131,9 | 5,9 / 9,0 | 13,6 / 38,3 | 11,1 / 33,1 | 11 964 | 4 081 | 12 / 26 | 0 |
| + bola | 13,0 / 15,8 / 29,9 | 5,2 / 6,6 | 12,9 / 15,4 | 10,9 / 13,0 | 11 934 | 4 114 | 40 / 71 | 0 |
| + objetos | 14,6 / 18,0 / 20,6 | 5,8 / 8,5 | 14,6 / 17,8 | 11,9 / 14,9 | 12 091 | 4 213 | 14 / 29 | 671 |
| + enemigos | 16,4 / 21,3 / 43,5 | 8,2 / 12,4 | 16,4 / 20,4 | 13,9 / 16,2 | 12 113 | 4 184 | 19 / 38 | 236 |
| Pico | 16,0 / 30,9 / 47,5 | 11,7 / 20,6 | 15,7 / 30,1 | 12,7 / 14,4 | 12 003 | 4 154 | 17 / 35 | 90 |

Las correcciones son del servidor a la tortuga del cliente, casi todas verticales (3-16 cm) sobre
`TN_ProcMapGenerator_0.ProceduralMeshComponent_*`. Aquí sí se disparan las catapultas (25 disparos en el escenario).

## Los 10 costes mayores (traza de la playa, pico frente a referencia)

ms por fotograma en la región `TNChaos_peak` (727 fotogramas) y `TNChaos_baseline`; GT = hilo de juego, RT = de render.

| # | Coste | Pico | Referencia | Dónde |
|---|---|---|---|---|
| 1 | GT: actualizaciones de fin de fotograma forzadas en el hilo de juego (`PostTickComponentUpdate_ForcedGameThread`), con `AddPrimitive` 3,1 ms y **149 reconstrucciones de ISM por fotograma** (`BuildRenderData`) | 6,6 | 2,2 | ISM animados con `bMarkRenderStateDirty=true` cada fotograma: `TN_BeachCritterKit.h:121` (ermitaño, tanque, pulpo, pulgas) y `TN_BeachTrapCommon.cpp:229` (polvo y astillas de catapultas y trampas) |
| 2 | GT: Tick de enemigos: cangrejo gigante 0,78, tanque 0,35, zona de gaviotas 0,33, ermitaño 0,31 (más erizos del mapa 0,36) | ~2,1 | ~0,8 | `ATN_BeachEnemy::Tick` (`TN_BeachEnemy.cpp:1559`) y subclases |
| 3 | RT: dibujo de mallas `/Engine/Transient` (mallas de arte creadas en tiempo de ejecución): **3 050 por fotograma** | 2,5 | 0,8 | piezas sueltas de enemigos, catapultas y decorado (`TNArt`) |
| 4 | RT: gestión de la escena (uniform buffers, `FScene_StartFrame`, alta en el octree, `UpdateInstanceProxyData` ×151, caché de sombras virtuales) | ~3,5 | ~1,1 | consecuencia de 1 y 3 |
| 5 | GT: transformadas de componentes (`UpdateComponentToWorld` ×3 756, `MoveComponent` ×1 286, `UpdatePrimitiveTransform` ×1 407) | ~1,3 | ~0,4 | partes que se mueven cada fotograma (temblor de catapultas, patas, banderines) |
| 6 | GT: Tick de 29 catapultas (las 12 del caos y las 17 de la ronda, gastadas incluidas) | 0,52 | 0,26 | `ATN_BeachCatapult::Tick` (`TN_BeachCatapult.cpp:1004`), que en el servidor recorre todos los `ACharacter` (`:816`) |
| 7 | GPU: Lumen (sondas 2,9, reflejos 2,1, cachés de radiancia 1,8, indirecta y AO 1,7) | ~8,5* | ~7,4* | escalabilidad Épica (GI y reflejos 3) |
| 8 | GPU: TSR a 1080p | 4,5* | 3,9* | `sg.AntiAliasingQuality=3` |
| 9 | GPU: `VirtualTextureUpdate` +2,2; translucidez +0,8; `UpdateDistanceFieldAtlas` 0,54 (0 en la referencia); `FXSystemPreRender` +0,3 | ~5,9* | ~2,5* | lo que nace en el pico (enemigos, efectos, mallas nuevas con campo de distancia) |
| 10 | Memoria: **commit 12 GB y VRAM 4,2 GB en el Coop** frente a 6,5 GB y 3,3 GB en la playa; el caos solo añade +136 MB de commit y hasta +80 MB de VRAM en la playa; Medio ahorra 1,4 GB de VRAM y 1,4 GB de commit | — | — | mapa del Coop (`TN_ProcMapGenerator`) y escalabilidad |

\* Temporizadores de GPU de Insights sumados entre colas y anidados: sirven para ordenar; la GPU real del fotograma es la
de las tablas (11-12 ms en el pico con traza).

Red: 17-40 KB/s de media por conexión (máximo 76), el 20-40 % de `MaxClientRate`; no es un problema. Las correcciones sí
lo son en el Coop.

## Propuestas (una issue por coste corregible)

| Título | Cifra | Fichero:línea | Arreglo propuesto |
|---|---|---|---|
| Rendimiento: ISM animados recrean su proxy de render en cada fotograma | 149 reconstrucciones/fot., `AddPrimitive` 3,1 ms GT en el pico (1,0 en reposo) | `Private/World/Beach/TN_BeachCritterKit.h:121`, `Private/World/Beach/TN_BeachTrapCommon.cpp:229` | No marcar el estado de render sucio al mover instancias (en 5.6 el gestor de instancias ya manda el cambio) o, para pocas piezas, componentes sueltos movidos con `SetRelativeTransform`; dejar de actualizar ráfagas muertas y críticos lejanos. Medir de nuevo con `TN.Stress caos` |
| Rendimiento: 3 000 dibujos de mallas de arte sueltas en la playa | RT 2,5 ms/fot. en el pico (0,8 en reposo), 3 050 dibujos | `Private/Art/TN_Art.cpp` (creación de piezas) y kits de `World/Beach` | Fusionar en una malla las piezas rígidas de cada enemigo, catapulta y decorado; ocultar piezas de detalle lejos (`SetCullDistance`) |
| Rendimiento: Tick de enemigos de playa sin LOD de distancia en el pico | ~2,1 ms GT con 40 enemigos (cangrejo gigante 0,04 ms cada uno) | `Private/World/Beach/TN_BeachEnemy.cpp:1559` | `SetActorTickInterval` por distancia a la tortuga más cercana y sin animación fuera de cámara; comprobar el modo «despacio» con `TN.Beach.Enemy.Stats` |
| Rendimiento: catapultas gastadas o lejanas siguen con Tick y recorren personajes | 29 catapultas, 0,52 ms GT (0,018 ms cada una) | `Private/World/Beach/TN_BeachCatapult.cpp:1004` y `:816` | Meterlas en `UTN_BeachTickWakeSubsystem` (dormir sin tortuga a menos de 40 m) y detectar al pasajero con el solape del cazo en vez de `TActorIterator<ACharacter>` |
| Red: el cliente recibe 700-1 000 correcciones por fase en el mapa del Coop | 906/698/671 correcciones en 20 s con 1 cliente (playa: 0-52) | `TN_ProcMapGenerator` (colisión de `ProceduralMeshComponent_*`) | Comprobar que el cliente genera la misma colisión del terreno que el servidor (semilla, orden, complejidad); errores verticales de 3-16 cm |
| Memoria: el mapa del Coop compromete 12 GB y 4,2 GB de VRAM | commit 12,2 GB y VRAM 4,3 GB frente a 6,5 / 3,3 GB en la playa | `Private/World/ProcMap/TN_ProcMapGenerator.cpp` | `memreport -full` y LLM (`-llm`) en el Coop para repartirlo; candidatos: datos de CPU de las mallas procedurales y teselas que no se ven |
| Ajustes: escalabilidad por defecto Épica en PCs modestos | GPU 10-12 ms y VRAM 3,3 GB en una RTX 4060 (Medio: 4,7-5,9 ms y 1,9 GB) | `Private/UI/Pause/TN_PauseMenuWidget.cpp:2020` (benchmark solo a mano) | Lanzar `RunHardwareBenchmark` + `ApplyHardwareBenchmarkResults` en el primer arranque (`TN_GameSettingsSubsystem`) |

## Lo que no se pudo medir o queda por confirmar

- **Pocas catapultas a la vez en la playa**: de 13 ayudas solo 6 viajes; tortugas de pie sobre `BowlCollision`, catapulta
  cargada y con Tick, y no se arma (registro `-TNChaosVerbose`: «no sale de…»). Sin verificar si es un fallo del juego
  (`ATN_BeachCatapult::WhereOnArm`/`IsFreeRider`) o del escenario; en el Coop sí disparan (25). El coste de las catapultas
  en la playa está, por tanto, por debajo del peor caso.
- Traza con `-trace=memory`: el servidor no llegó a escuchar en 180 s. Memoria por etiqueta (LLM) sin hacer.
- Coop medido con el PC sin RAM libre (0,1 GB): RAM y p95/p99 contaminados; el commit sí vale. Repetir con el PC libre.
- PCs modestos: no hay uno a mano; las cifras de CPU y GPU son de un i9-13950HX y una RTX 4060 (estimaciones aparte).
- Editor (`-game`), no build empaquetada: la memoria incluye módulos de editor (orden de +1-2 GB).
- Un cliente en vez de tres (cambio del director: cada jugador tendrá su PC); KB/s y correcciones son de una conexión.

## Reproducir

```bash
# Una ventana (anfitrión) y un cliente -nullrhi con 100 ms y 1 % de pérdida; red_local.py está en la PR #388.
uv run python Scripts/tools/red_local.py --clientes 1 --lag 100 --perdida 1 --estres caos --estres-segundos 20 \
  --salir-al-acabar --extra-servidor=-TNStressWarmup=30 --extra-cliente=-TNStress=caos \
  --extra-cliente=-TNStressSeconds=20 --extra-cliente=-TNStressWarmup=5 --extra-cliente=-TNQuitWhenDone --esperar 480
# Informe: Saved/Stress/caos_<fecha>.json (y caos_cliente_<fecha>.json en el cliente).
```
