# Tirones en el lobby con más jugadores (#152) — 2026-10-03

Síntoma de la issue: el juego va fluido, pero cada 3-4 s se congela hasta 250 ms. Pasa sobre todo en el lobby, con más jugadores conectados y solo en el PC de Rodrigo (i9-13950HX híbrido, RTX 4060 Laptop de 8 GB), tanto de anfitrión como de invitado.

## Conclusión

- **El patrón de 3-4 s no se ha reproducido.** Han sido nueve ejecuciones en este PC, unos 25 minutos de lobby con 3 jugadores, con trazas de Insights del anfitrión y de los clientes. Ningún sistema del juego se dispara cada 3-4 s, ni con los jugadores quietos ni con todos moviéndose (monkey).
- **Los tirones que sí aparecen no son lógica del juego.** En el momento del tirón, el hilo de juego espera al de render (`Frame Sync Time`) y el de render espera a tareas o a la GPU. Las tareas que tardan muchísimo más de lo normal tienen dos causas, las dos ajenas al juego:
  1. **Hilos sin CPU.** Una tarea que suele durar 0,01 ms (`Get ProcMesh Elements`) llega a 100-119 ms. Los workers se quedan hasta 500 ms en `LowerThreadPriority`, que no tiene trabajo dentro: el hilo ha bajado su prioridad y Windows no lo vuelve a poner en marcha. Pasa a ráfagas de 4-15 s cuando otros procesos cargan la máquina. Durante el tirón, la GPU baja del 80-90 % al 33-38 % de uso: no tiene trabajo, porque la CPU no se lo manda.
  2. **GPU compartida.** El render espera a la GPU 170-510 ms (`GPUBound_WaitingForGPUForOcclusionQueries`, `Present` → `Submission_Wait` de 203 ms) cuando otro proceso usa la misma GPU. En la ejecución 9, la VRAM pasó de 4,5 a 7,6 GB de 8 GB (otro proceso de la máquina) justo al empezar los tirones de 16:12:31-39.
- **Por qué este PC y no el portátil de Mokius (hipótesis, sin verificar en su portátil):** este PC tiene residentes muchos procesos que usan GPU y CPU (un editor de Unreal abierto, Wallpaper Engine, Discord con aceleración por hardware, NVIDIA Overlay, MSI Center, Nahimic, servicios de Killer, Steam). Además, la CPU es híbrida: Windows puede mandar los hilos de prioridad baja (workers de UE) a núcleos E o dejarlos sin turno (#57). Con más jugadores, el fotograma cuesta más (más tortugas, más mallas procedurales de cara y lengua, más red) y el margen ante esa competencia es menor. Esto explica tirones intermitentes que dependen de lo que haya abierto, pero no un periodo fijo de 3-4 s: eso solo se puede confirmar con una traza del caso real.
- **Picos del juego que sí existen, pero no son periódicos:**
  - Al entrar un jugador: el tutorial por jugador (`TN_TutorialPlayer`) tarda 0,9-1,2 s en un fotograma, con `LoadObject` síncronos de 111-541 ms y la creación de mallas. En el cliente que entra, `OnRep_ReplicatedHasBegunPlay` tarda 897 ms. En el anfitrión, `ClientSetHUD` tarda 63 ms. Es el caso de #77 (cargas síncronas).
  - El recolector de basura cada 61 s: 20-43 ms.

## Método

- Build DebugGame del editor en `dev` (`80db84394`). `LVL_Lobby?listen` con `-game -windowed -ResX=960 -ResY=540 -NoSteam` y 2 clientes `127.0.0.1` en este mismo PC. Trazas `-trace=cpu,frame,loadtime,net -statnamedevents` en todas las instancias. En la ejecución 8, el anfitrión también con `gpu`; en la 9, con `bookmark`.
- Exportación sin interfaz: `UnrealInsights.exe -OpenTraceFile=<utrace> -ExecOnAnalysisCompleteCmd="@=<rsp>" -NoUI -AutoQuit`, con `TimingInsights.ExportTimingEvents <csv> -threads=GameThread|RenderThread*|RHIThread|*Worker* -columns=...` en el `.rsp`. Las rutas, con barras normales (`cygpath -m`). Desde Git Bash, `\$` en la ruta rompe el comando.
- Fotograma = de inicio a inicio de `FEngineLoop::Tick`. Para cada fotograma de más de 50 ms se suma el tiempo de cada scope del hilo de juego. Después se miran, en la misma ventana de tiempo, los scopes del hilo de render, del RHI y de los workers.
- Movimiento con el monkey (`-TNMonkey=<s>:<semilla>`) en el anfitrión y en los clientes. Uso de VRAM y de GPU con `nvidia-smi` cada 0,5 s.

## Resultados

Fotogramas de más de 50 y de más de 100 ms en el anfitrión, sin contar los primeros 40 s (carga y entrada de los clientes):

| # | Montaje | Duración | p50 (ms) | > 50 ms | > 100 ms | Máx. (ms) | Lo que se ve |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 3 instancias con gráficos en Épico | 132 s | 132 | 925 | 848 | 1506 | GPU saturada: 7 fps |
| 2 | 3 instancias en Bajo, a 60 fps | 137 s | 16,7 | 2 | 0 | 63 | Fluido |
| 3 | Anfitrión en Épico; clientes en Bajo, a 30 fps | 138 s | 15,9 | 80 | 14 | 174 | Ráfaga de 15 s: tareas de 0,01 ms que tardan 100 ms |
| 4 | Igual que la 3, con monkey | 133 s | 17,5 | 38 | 6 | 160 | Ráfaga de 4 s |
| 5 | Igual que la 4, todo fijado a núcleos P | 139 s | 12,0 | 68 | 4 | 120 | Más rápido, con la misma ráfaga y cortes de audio (16 hilos lógicos para 3 procesos) |
| 6 | Clientes sin gráficos (`-nullrhi`) y sin límite (270 fps) | 160 s | 10,5 | 181 | 10 | 864 | Los clientes se comen la CPU: workers 500 ms en `LowerThreadPriority` |
| 7 | Clientes sin gráficos, a 30 fps | 189 s | 9,7 | 88 | 9 | 412 | La GPU no termina: oclusión 170 ms, `Present` 203 ms |
| 8 | Igual que la 7 | 187 s | 10,7 | 12 | 0 | 94 | Sin tirones |
| 9 | Igual que la 7, 5 min, con `-TNHitchLog` | 287 s | 9,1 | 8 | 3 | 165 | Tirones solo mientras otro proceso usaba la GPU o la CPU |

Clientes de las ejecuciones 2-5 (en Bajo, a 30 fps): 1-41 fotogramas de más de 50 ms y ninguno de más de 160 ms. El de la ejecución 3 tuvo 41 en una ráfaga.

Las ejecuciones 7 y 8 tienen el mismo montaje: en una hay tirones de 400 ms y en la otra ninguno de más de 94 ms. Encaja con lo «intermitente» de la issue: depende de lo que más haya en marcha en la máquina, no del juego.

**Coste:** con la máquina tranquila, el lobby con 3 jugadores va a 9-11 ms de mediana y sin tirones de más de 100 ms. Cuando otro proceso compite, aparecen ráfagas de 4-15 s con fotogramas de 50-500 ms.

## Lo que se ha añadido: registro de tirones

Como el tirón real no se ha podido capturar aquí, el juego trae ahora una forma de capturarlo en la partida de verdad sin tener que pulsar nada a tiempo:

- `-TNHitchLog` (50 ms) o `-TNHitchLog=<ms>` en la línea de órdenes, o `TN.HitchLog.ThresholdMs <ms>` en la consola. No existe en Shipping y viene apagado por defecto.
- Cada fotograma por encima del umbral escribe una línea en el log, por ejemplo: `[Tirón] 165 ms (juego 22.2, render 184.4, RHI 3.1, GPU 5.8 ms: hilo de render) · 3 jugadores · ventana sin foco · n.º 7, a 4.1 s del anterior (mediana 4.4 s)`. Los tiempos de hilo no cuentan las esperas: si ningún hilo llega a la mitad del fotograma, sale «ningún hilo ocupado» (una espera de presentación o del controlador, o el proceso parado). También deja un marcador «Tirón 165 ms» en Insights si la traza lleva el canal `bookmark`.
- Atribuye el tirón al hilo que explica al menos la mitad del fotograma. Si no lo explica ninguno, pone «ningún hilo (el proceso no corría)»: es la señal del planificador o de otro proceso.
- Código: `Source/Tortunabo/{Public,Private}/Testing/TN_HitchTracker.*` (lógica pura) y `TN_HitchMonitorSubsystem.*`. Test: `Tortunabo.Testing.HitchTracker`.

## Siguiente paso (para cerrar los criterios 1 y 3)

1. Partida real con Steam: Rodrigo en su PC y Mokius en el suyo, una instancia en cada uno, build Development o empaquetada, con `-TNHitchLog -trace=cpu,frame,gpu,bookmark,loadtime,net`. Hay que dejar el lobby 5 min con 2 o más jugadores, hablando por voz como en el caso real.
2. Si las líneas `[Tirón]` salen cada 3-4 s:
   - **Hilo de juego:** el scope del marcador en Insights lo dice (voz, carga síncrona, red).
   - **GPU:** mirar la pista de GPU en ese marcador.
   - **Ningún hilo:** el proceso no corría. Repetir cerrando Wallpaper Engine, Discord (o quitándole la aceleración por hardware), NVIDIA Overlay y MSI Center, uno a uno. Repetir también con el proceso en prioridad alta.
3. Si no salen, el tirón era de lo que había abierto aquel día. Se puede cerrar la issue con el log como prueba.

## Sin verificar

- Todo se ha medido en un solo PC con 3 instancias por loopback y `-NoSteam`. No se ha medido la red real, Steam, la voz con gente hablando, una build empaquetada ni el portátil de Mokius.
- Los procesos «externos» de las ejecuciones con tirones no se identificaron en el momento: en la máquina había otros agentes compilando o ejecutando el juego. Que la competencia venga de ellos se deduce de los tiempos (tareas paradas, GPU sin uso, VRAM de otro proceso), no de una traza del sistema. Una traza ETW de cambios de contexto necesita permisos de administrador.
- El criterio 3 de la issue (5 min sin fotogramas de más de 50 ms) no se cumple en este PC con 3 instancias: en la ejecución 9 hubo 8, todos con otro proceso compitiendo.
