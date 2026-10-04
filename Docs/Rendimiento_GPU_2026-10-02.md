# Render y GPU con ventana — 2026-10-02

Issue #58. Medida con ventana (sin `-nullrhi`) de los hilos de juego y de render, del RHI y de la GPU sobre `LVL_BeachRace`, con los escenarios de `TN.Stress` del [informe de estrés](Estres-Monkey-2026-09-29.md). La referencia es la build empaquetada **Development** (`Saved/Packages/dev-2026-10-02`, compilada el 2026-10-02 a la 01:18). DebugGame se mide solo como comparación: su hilo de juego no es representativo de Shipping.

## Equipo y configuración

| Qué | Valor |
| --- | --- |
| Equipo | Portátil, CPU Intel Core i9-13950HX (24 núcleos: 8 P + 16 E, 32 hilos), 32 GB de RAM |
| GPU | NVIDIA GeForce RTX 4060 Laptop, 8 GB, controlador 616.92 (32.0.16.1692); la pantalla del portátil va conectada a la NVIDIA (el motor elige el adaptador 0) |
| Pantalla | Panel de 2560 × 1600 a 240 Hz con escala del 150 %; un segundo monitor de 1920 × 1080 conectado |
| Sistema | Windows 11 25H2 (10.0.26200), enchufado, plan «Alto rendimiento». GPU a ~86 °C y ~2475 MHz con carga |
| Juego | Ventana de 1920 × 1080 (`-windowed -ResX=1920 -ResY=1080`; el tamaño real del backbuffer no sale en el log), D3D12 SM6, escalabilidad Épica (3) en todos los grupos, Lumen (GI y reflejos), sombras virtuales, `r.RayTracing=True`, TSR. `r.VSync 0` y `t.MaxFPS 0` |
| Motor | UE 5.6.1 (CL 44394996) |

## Cómo se ha medido

El script `Scripts/tools/medir_render_gpu.py` lanza el juego, captura el CSV del CsvProfiler y lo parte por las fases que el subsistema de estrés escribe en el log. Por fase descarta los primeros 1,5 s (igual que `TN.Stress`) y saca media, p95 y máximo de `FrameTime`, `GameThreadTime`, `RenderThreadTime`, `RHIThreadTime` y `GPUTime`, y media y máximo de `RHI/DrawCalls` y `RHI/PrimitivesDrawn` (los contadores de `stat rhi`). También guarda los pases `GPU/*` (`-csvGpuStats` equivale a `r.GPUCsvStatsEnabled 1`).

```bash
# Desde la raíz del repo, con el PC libre. --build development|debuggame, --scenario control|heavy|race8
uv run python Scripts/tools/medir_render_gpu.py run --build development --scenario heavy --frames 16000 --out <carpeta>
uv run python Scripts/tools/medir_render_gpu.py run --build debuggame --scenario control --frames 6000 --out <carpeta>
# Experimentos: --cvars "r.ScreenPercentage 50" --tag sp50
```

Comando exacto que lanza (Development, `heavy`):

```text
Saved\Packages\dev-2026-10-02\Windows\Tortunabo.exe /Game/Maps/Run/LVL_BeachRace -windowed -ResX=1920 -ResY=1080 -NoSteam -nosplash -handleensurepercent=0 "-ExecCmds=r.VSync 0, t.MaxFPS 0, r.GPUCsvStatsEnabled 1, csvprofile exitoncompletion, csvprofile frames=16000" -abslog=<carpeta>\development_heavy.log -TNStress=heavy -TNStressSeconds=60 -TNStressWarmup=10
```

DebugGame: `UnrealEditor-Win64-DebugGame-Cmd.exe Tortunabo.uproject /Game/Maps/Run/LVL_BeachRace -game` y el resto igual (`frames=6000` en `control`, 8000 en `heavy` y `race8`).

Por qué así:

- `-csvCaptureFrames` arranca la captura antes de iniciar el RHI y el motor se cae (`IsRayTracingAllowed() may only be called once RHI is initialized`). Se arranca con `-ExecCmds` en el fotograma 1, que sí se ejecuta con `-game`.
- Con `-TNQuitWhenDone` el CSV queda truncado (el motor sale sin cerrar la captura). Por eso la captura tiene un número fijo de fotogramas (`csvprofile frames=N`) y cierra el juego al terminar (`exitoncompletion`); N se elige para que cubra los 70 s del estrés.
- `-handleensurepercent=0`: ver el hallazgo 1.
- Línea base = escenario `control` (una tortuga, nada creado, 6 fases de 10 s). Todos los escenarios llevan el monkey moviendo las tortugas, así que la cámara no mira siempre a lo mismo: las draw calls y los triángulos varían 3-4 veces entre fases del mismo escenario.

## Resultados — Development empaquetada (referencia)

Tiempos en ms, media / p95 / peor fotograma. En `heavy` se da el conjunto de las 6 fases y la última fase, con todo vivo (200 enemigos, 100 cajas y hasta 28 lanzables).

| Escenario | Fotogramas | Fotograma | Juego (game) | Render (draw) | RHI | GPU |
| --- | --- | --- | --- | --- | --- | --- |
| Línea base (`control`) | 4423 | 11,5 / 13,2 / 18,2 | 3,9 / 4,6 / 14,5 | 11,5 / 13,2 / 15,7 | 3,8 / 4,9 / 6,6 | 9,7 / 11,1 / 12,6 |
| `heavy`, 6 fases | 3821 | 13,3 / 17,5 / 26,8 | 7,9 / 11,7 / 22,2 | 13,3 / 17,5 / 23,1 | 4,1 / 5,2 / 19,9 | 9,7 / 11,2 / 12,4 |
| `heavy`, todo vivo | 491 | 17,2 / 18,6 / 23,1 | 11,7 / 13,2 / 21,1 | 17,2 / 18,6 / 23,1 | 4,8 / 5,4 / 19,9 | 10,5 / 11,6 / 12,3 |
| `race8` | 4609 | 12,7 / 14,0 / 23,1 | 7,3 / 8,6 / 17,8 | 12,7 / 14,0 / 22,1 | 3,0 / 3,6 / 9,6 | 11,3 / 12,6 / 16,4 |

Draw calls y triángulos (`stat rhi`), media / máximo por fotograma:

| Escenario | Draw calls | Triángulos |
| --- | --- | --- |
| Línea base | 472 / 3250 | 158 k / 1,31 M |
| `heavy`, 6 fases | 879 / 3733 | 188 k / 1,30 M |
| `heavy`, todo vivo | 1290 / 3733 | 357 k / 1,30 M |
| `race8` | 3610 / 6555 | 761 k / 1,87 M |

`heavy` por fase (Development; tiempos en ms, media / p95):

| Fase | Fotograma | Juego | Render | GPU | Draw calls media |
| --- | --- | --- | --- | --- | --- |
| Base | 10,6 / 11,9 | 3,7 / 4,1 | 10,6 / 11,9 | 8,9 / 9,7 | 501 |
| + 100 cangrejos | 11,5 / 12,8 | 6,7 / 8,3 | 11,5 / 12,8 | 9,6 / 10,4 | 872 |
| + 50 gaviotas | 12,5 / 13,6 | 8,3 / 9,2 | 12,5 / 13,6 | 9,6 / 10,7 | 717 |
| + 50 tanques | 14,3 / 15,3 | 9,7 / 10,8 | 14,3 / 15,3 | 9,6 / 10,3 | 940 |
| + 100 cajas | 16,2 / 17,6 | 10,2 / 11,2 | 16,2 / 17,6 | 10,8 / 11,7 | 1224 |
| + lanzables | 17,2 / 18,6 | 11,7 / 13,2 | 17,2 / 18,6 | 10,5 / 11,6 | 1290 |

Las cifras del CSV coinciden con las del informe JSON de `TN.Stress` (en `race8`: fotograma 12,69 frente a 12,7; juego 7,27 frente a 7,3; GPU 11,25 frente a 11,3).

## DebugGame (comparación)

| Escenario | Fotogramas | Fotograma | Juego | Render | RHI | GPU | Draw calls | Triángulos |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Línea base | 4226 | 12,1 / 13,6 / 32,2 | 9,9 / 11,2 / 32,2 | 12,0 / 13,6 / 15,9 | 4,0 / 4,7 / 7,2 | 10,1 / 11,3 / 13,2 | 1030 / 2696 | 296 k / 939 k |
| `heavy`, 6 fases ¹ | 2502 | 20,2 / 32,5 / 90,4 | 19,7 / 32,4 / 89,4 | 13,7 / 18,8 / 86,7 | 4,5 / 5,6 / 7,6 | 9,4 / 10,8 / 14,7 | 1421 / 3800 | 316 k / 1,23 M |
| `heavy`, todo vivo ¹ | 247 | 33,8 / 45,7 / 90,4 | 33,6 / 41,9 / 89,4 | 18,6 / 20,1 / 86,7 | 5,2 / 6,0 / 6,4 | 10,7 / 11,6 / 14,7 | 2916 / 3800 | 563 k / 1,23 M |
| `race8` ¹ | 3458 | 16,9 / 18,5 / 141,5 | 16,9 / 18,5 / 42,1 | 11,3 / 12,9 / 149,0 | 3,7 / 4,4 / 7,0 | 11,9 / 13,2 / 18,5 | 4494 / 7946 | 868 k / 1,83 M |

¹ Con uno o dos procesos `-nullrhi` de otro árbol de trabajo corriendo a la vez (pruebas de otra sesión). La línea base de DebugGame y todas las de Development son con el PC libre.

La GPU mide lo mismo en las dos builds (9,7 frente a 10,1 ms en reposo): para la GPU, DebugGame vale. El hilo de juego no: 2,5 veces más en reposo (9,9 frente a 3,9 ms) y 2,9 veces más con todo vivo (33,6 frente a 11,7 ms).

## Experimentos de diagnóstico (Development)

| Experimento | Escenario | Fotograma | Juego | Render | RHI | GPU | Lectura |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Referencia | línea base | 11,5 | 3,9 | 11,5 | 3,8 | 9,7 | |
| `r.ScreenPercentage 50` | línea base, 30 s | 7,8 | 4,2 | 7,8 | 3,7 | 5,8 | La mitad de píxeles: −3,9 ms de GPU y −3,7 ms de fotograma. Limita la GPU |
| `r.AllowOcclusionQueries 0` | línea base, 30 s | 11,2 | 4,4 | 7,0 | 8,4 | 9,5 | El fotograma no cambia: la espera pasa del hilo de render al RHI. Ambos esperan a la GPU |
| Referencia | `heavy`, todo vivo | 17,2 | 11,7 | 17,2 | 4,8 | 10,5 | |
| `r.RayTracing.Enable 0` | `heavy`, todo vivo | 14,8 | 11,0 | 14,8 | 3,4 | 9,8 | −2,4 ms de render con todo vivo (−1,6 ms en el conjunto de `heavy`) |

Desglose del hilo de render en la línea base: de sus 11,5 ms, 6,1 son `EventWait/Visibility` (espera a los resultados de oclusión, que llegan de la GPU) y el trabajo propio ronda 5,4 ms. Con todo vivo en `heavy` ya no espera: `RenderThreadOther` 7,4 ms, `UpdatePrimitiveTransform` 2,2, `RenderOther` 2,1, alta y baja de primitivas ~1 y `RayTracing_FinishGatherInstances` 0,5. El hilo de juego con todo vivo: `TickActors` 7,5 ms y `EndOfFrameUpdates` 1,2.

GPU por pases (Development, media en ms):

| Pase | Línea base | `heavy` | `race8` |
| --- | --- | --- | --- |
| TemporalSuperResolution | 2,33 | 2,18 | 2,02 |
| Unaccounted | 1,46 | 1,67 | 1,00 |
| ShadowDepths (sombras virtuales) | 1,46 | 1,53 | 2,17 |
| RenderDeferredLighting | 0,87 | 0,70 | — |
| ShadowProjection | 0,46 | 0,46 | — |
| BasePass | 0,28 | 0,30 | 0,92 |
| Postprocessing | 0,31 | 0,31 | 0,76 |
| NaniteVisBuffer | — | — | 0,72 |

## Conclusiones

1. **En reposo limita la GPU, no la CPU.** Development a 1080p en Épico: 11,5 ms de fotograma (~87 fps) con 9,7 ms de GPU y 3,9 de hilo de juego. Bajar la resolución a la mitad quita 3,7 ms del fotograma; quitar las consultas de oclusión no cambia nada. El hilo de render parece el cuello (`Render` = `Fotograma`) solo porque espera a la GPU.
2. **Con carga, el cuello pasa a la CPU, sobre todo al hilo de render.** Con 200 enemigos, 100 cajas y los lanzables vivos, la GPU apenas sube (+0,8 ms), el hilo de juego llega a 11,7 ms y el de render a 17,2 ms de trabajo real: actualizar transformaciones de primitivas que se mueven, altas y bajas de primitivas y las instancias del ray tracing. Apagar el ray tracing quita 2,4 ms de ese fotograma.
3. **Qué mirar primero.** (a) El presupuesto de GPU: TSR se lleva 2,3 ms (el grupo de antialiasing Épico pone `r.TSR.History.ScreenPercentage=200`) y las sombras virtuales 1,5; son los dos pases más caros y los que más escalan con resolución y vistas. (b) Si el ray tracing por hardware aporta algo visible: cuesta CPU en el hilo de render con muchos objetos móviles. (c) El coste de escena de los objetos que se mueven (cajas, lanzables, cangrejos): menos componentes con colisión o visibles por objeto y sin `Tick` lejos de la cámara.
4. **`race8` en un solo proceso es pantalla partida, no 8 jugadores en red.** Las 8 tortugas son jugadores locales; el motor solo dibuja 4 vistas (las demás quedan con tamaño 0). Eso multiplica por ~7 las draw calls (3610 de media frente a 472) y sube `InitViews_Scene` a 3,8 ms y las sombras a 2,2 ms, y aun así el fotograma es 12,7 ms. En red cada PC dibuja una vista: su coste de GPU estará cerca de la línea base.
5. **DebugGame vale para la GPU y para el número de draw calls, no para decidir dónde está el cuello.** En DebugGame todo parece limitado por el hilo de juego (33,6 ms con todo vivo); en Development el hilo de juego es 2,5-3 veces más barato y el cuello está en otra parte. Las decisiones de rendimiento, con la build empaquetada.

## Hallazgos y avisos

1. **Ensure en una malla estática**: `GetStaticMaterials()[MaterialIndex].UVChannelData.bInitialized` (`StaticMesh.cpp:4936`, desde `UStaticMeshComponent::GetMaterialStreamingData`) salta en algunas cargas de `LVL_BeachRace`, en las dos builds (3 de las 6 ejecuciones lanzadas sin `-handleensurepercent=0`; con esa opción ya no se registra). En una ejecución de la build empaquetada el proceso se quedó colgado más de 10 min preparando el informe del fallo; se ha medido con `-handleensurepercent=0`. Es el único fallo del informe del monkey en `race8` («1 asserts o ensures»). Falta saber qué malla es.
2. **Sensibilidad a otros procesos**: una línea base de Development con dos procesos `-nullrhi` ajenos corriendo a la vez dio 14,4 ms de fotograma y 7,8 de RHI (frente a 11,5 y 3,8 con el PC libre). Se ha descartado y repetido. Medir siempre con el PC libre y comprobarlo antes (`tasklist`).
3. **Triángulos**: `RHI/PrimitivesDrawn` no cuenta lo que dibuja Nanite (hay pase `NaniteVisBuffer`). Las cifras son de las mallas sin Nanite.
4. **`RenderThreadTime` incluye esperas** (`EventWait`), igual que `stat unit`: un hilo de render igual al fotograma no prueba que sea el cuello. Para separar trabajo de espera, el desglose `Exclusive/RenderThread/*` del CSV o Unreal Insights.
5. Los picos periódicos de 25 Hz del informe headless (p95 de 2-3 veces la mediana) no aparecen con ventana: en Development el p95 de la línea base es 1,15 veces la media.

Datos sin procesar (CSV, logs y JSON por ejecución): no están en el repositorio. Se regeneran con el script en unos 4 minutos por escenario.

## Niveles de calidad (2026-10-03, #577)

`Config/DefaultScalability.ini` (nuevo) cambia solo lo necesario respecto a `BaseScalability.ini` del motor. `r.Shadow.Virtual.Enable=1` sale de `DefaultEngine.ini`: con prioridad de ajuste de proyecto, la escalabilidad no podía cambiarlo.

| Nivel | Sombras |
| --- | --- |
| Bajo (0) | CSM, 1 cascada de 1024, `r.ShadowQuality=2`, `r.Shadow.DistanceScale=0.35`. Antes: ninguna sombra dinámica (`r.ShadowQuality=0` del motor) |
| Medio (1) | CSM, 2 cascadas de 2048, `r.Shadow.DistanceScale=0.6`. Antes: VSM con 512 páginas |
| Alto (2) | VSM del motor con 4 rayos SMRT direccionales (en vez de 8) |
| Épico (3) y Cine | Sin cambios (VSM) |

Medida: `medir_render_gpu.py --build debuggame --scenario control --resx 1280 --resy 720` en `LVL_BeachRace`, con `scalability N` en `-ExecCmds` (cambio en caliente tras arrancar en otro nivel). Media de todas las fases, en ms; mismo equipo que arriba (RTX 4060 Laptop). Las cifras de CPU llevan ruido: otros procesos del motor compilaban y pasaban tests a la vez.

| Nivel | GPU antes → después | `ShadowDepths` antes → después | `ShadowProjection` antes → después |
| --- | --- | --- | --- |
| Bajo | 1,77 → 1,96 | 0 → 0,24 (vuelve a haber sombras) | 0 → 0,01 |
| Medio | 2,98 → 2,71 | 0,52 → 0,48 | 0,11 → 0,02 |
| Alto | 4,86 → 4,71 | 0,67 → 0,64 | 0,31 → 0,29 |
| Épico | 7,55 → 7,79 | 1,41 → 1,31 | 0,41 → 0,39 |

Las cvars resultantes (consultadas en el log de cada ejecución): Bajo y Medio con `r.Shadow.Virtual.Enable=0` y `r.ShadowQuality` 2 y 3; Alto y Épico con `r.Shadow.Virtual.Enable=1`. En Épico no cambia nada: la diferencia es ruido.

### Ray tracing por calidad (#562)

`UTN_RayTracingQualitySubsystem` (subsistema de motor) sigue a `sg.GlobalIlluminationQuality` y pone `r.RayTracing.Enable=0` en iluminación global Baja, Media y Alta (Bajo y Medio ya iban sin Lumen; Alto pasa a Lumen por software) y 1 en Épica y Cine. No va en `DefaultScalability.ini` porque esa cvar no es `ECVF_Scalability` y el motor ignora la línea (con un `ensure`); se fija con prioridad de escalabilidad, así que un valor puesto por consola o en `Engine.ini` manda. Con él apagado no se crean los BLAS (el de cada sección de ProcMesh incluido), el hilo de render no recoge instancias y Lumen pasa a software.

| Medida | Ray tracing encendido | Apagado |
| --- | --- | --- |
| `LVL_ProcMap` (Coop Normal, semilla aleatoria), calidad Baja, `RayTracingGeometry/TotalResidentSizeMB` | 399,9 MB (en el tope del pool de 400 MB) | 0 MB |
| Ídem, GPU / RHI (ms) | 3,8 / 3,1 | 2,9 / 2,3 |
| `LVL_BeachRace` `heavy`, todo vivo, calidad Alta: hilo de render (ms) | 22,7 | 16,8 |
| Ídem: trabajo exclusivo `RayTracing*` del hilo de render (ms) | 0,74 | 0,05 |
| Ídem: GPU total / pases `RayTracing*` / Lumen (ms) | 8,8 / 0,66 / 0,46 | 7,9 / 0 / 0,31 |

Encendido = `r.RayTracing.Enable 1` por consola, que tiene más prioridad y el subsistema no pisa. DebugGame: el hilo de juego (30-38 ms con todo vivo) marca el fotograma, así que la ganancia en fotograma hay que confirmarla en Development. Falta la revisión visual del director de Alto (Lumen por software, sin SDF del terreno ProcMesh) en acantilados y cuevas.
