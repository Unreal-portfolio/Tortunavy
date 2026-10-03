# Pico periódico de «25 Hz» en el escenario control (#57) — 2026-10-03

Coste 6 y recomendación 1 de `Docs/Estres-Monkey-2026-09-29.md`: en reposo, p50 de 5,5-8,7 ms y p95 de 17-21 ms, con unos 65 picos cada 10 s y una separación mediana de unos 40 ms.

## Conclusión

El pico no lo provoca el juego. Es el planificador de Windows en una CPU híbrida (i9-13950HX: 8 núcleos P con HT, lógicos 0-15, y 16 núcleos E, lógicos 16-31), con otros procesos cargando la máquina. Sin ventana visible ni audio (`-nullrhi -nosound`), Windows da al proceso QoS baja y, cuando hay competencia, mueve el hilo de juego a núcleos E a ráfagas. En un núcleo E, el fotograma entero va entre 1,5 y 2 veces más lento. El «periodo» de 40-75 ms es el ritmo al que el planificador hace esos cambios, no un temporizador del juego. Ningún scope del juego se dispara cada 40 ms.

## Método

- Build DebugGame del editor, `LVL_BeachRace`, `-TNStress=control`, `-game -nullrhi`. Comando de 2026-09-29 (sacado de la sesión que escribió el informe): `-unattended -nosound -NoVerifyGC -log -TNStressSeconds=60 -TNStressWarmup=10`.
- Traza `-trace=cpu,frame` (30 s), exportada sin interfaz con `UnrealInsights.exe -OpenTraceFile=<utrace> -ExecOnAnalysisCompleteCmd="@=<rsp>" -NoUI -AutoQuit`. El `.rsp` contiene `TimingInsights.ExportTimingEvents <csv> -threads=GameThread -columns=TimerName,StartTime,EndTime,Depth`. Si se pasa el comando directamente en la línea de órdenes desde Git Bash, no se ejecuta; con el `.rsp`, sí. Se compara el tiempo inclusivo de cada scope (profundidad ≤ 10) entre los fotogramas con pico (> 1,8 veces la mediana) y los normales (≤ 1,2 veces la mediana).
- Reproducción en el mismo commit del informe (`6c4ed2d82`, con el arreglo C4723 de `592d56b9e` para que compile) y en `dev` (`80db84394`).
- Afinidad de proceso forzada a núcleos P (`0xFFFF`) o a núcleos E (`0xFFFF0000`).
- Sonda nueva en TN.Stress: en cada fotograma registra en qué clase de núcleo estaba el hilo de juego (`GetCurrentProcessorNumberEx` y `GetSystemCpuSetInformation`).

## Resultados

| Prueba | p50 (ms) | p95 (ms) | Observaciones |
| --- | --- | --- | --- |
| Informe del 09-29, máquina cargada (flujo con varios agentes) | 5,5-8,7 | 16,7-21,3 | ~65 picos cada 10 s |
| `6c4ed2d82`, mismos argumentos y carga moderada | 4,4-5,1 | 5,7-9,8 | Picos con separación mediana de 39-138 ms |
| `6c4ed2d82`, con sonido y sin `-unattended`/`-log` | 4,6-5,8 | 5,3-7,1 | Sin picos |
| `6c4ed2d82`, solo núcleos P | 4,3-4,8 | 5,0-5,6 | Sin picos |
| `6c4ed2d82`, solo núcleos E | 6,8-7,2 | 7,5-9,6 | Todo el fotograma 1,6 veces más lento |
| `dev`, traza `cpu,frame` | 4,57 | 6,41 | 130 picos de 6.275 fotogramas |

En la traza, los fotogramas con pico duran 10,1 ms y los normales 4,5 ms. `UWorld_Tick` pasa de 4,25 a 9,49 ms, y todas las clases de actor suben en la misma proporción: `TN_BeachMine` de 0,60 a 1,26 ms, `TN_BeachSeaweed` de 0,63 a 1,29 ms y `TN_BeachShellGate` de 0,47 a 0,95 ms. Ningún scope aparece solo en los picos: el mayor añade 0,09 ms. La recolección de basura es incremental (0,025 ms por fotograma) y no coincide con los picos. Que todo el fotograma se ralentice por igual apunta a la CPU, no a un sistema concreto.

La sonda, en `dev` con QoS por defecto (`-TNStressDefaultQoS`) y otros procesos compilando, sitúa en un núcleo E entre el 58 % y el 100 % de los picos en casi todas las fases que los tienen; la excepción es una fase con 3 picos, en la que solo el 8 % cae en núcleo E. El hilo de juego pasa en núcleos E entre el 0,2 % y el 94 % de los fotogramas, según la carga. Con carga moderada, la mediana del fotograma es de 6,3-8,7 ms en núcleos E y de 5,0-6,2 ms en núcleos P.

**Coste:** con carga moderada, el p95 sube de ~5,5 a 9-11 ms (+4-5 ms) y cada pico cuesta entre +4 y +6 ms. Con la máquina muy cargada, como el 09-29, el p95 llegó a 17-21 ms.

## Corrección aplicada (solo en la herramienta de medida)

`TN.Stress` (no Shipping), en `Source/Tortunabo/{Public,Private}/Testing/TN_CpuCoreProbe.*` y `TN_StressSubsystem.cpp`:

1. Al empezar, pide a Windows QoS alta (`SetProcessInformation`/`SetThreadInformation` con `PowerThrottling`, que apaga EcoQoS explícitamente). En una CPU híbrida, además, limita los núcleos por defecto del proceso a los P (`SetProcessDefaultCpuSets`). Así mide el juego como si estuviera en primer plano. `-TNStressDefaultQoS` desactiva las dos cosas y deja medir como antes. Si `TN.Stress` se lanza desde la consola del editor, el proceso se queda así hasta que se cierra.
2. El informe incluye `cpu_hybrid`, `high_qos_applied` y `performance_cores_only` y, por fase, `frames_on_efficiency_core_pct`, `frame_p50_ms_on_efficiency_core`/`_on_performance_core` y `spikes_on_efficiency_core_pct`. Una medida con fotogramas en núcleos E está contaminada.
3. Test `Tortunabo.Testing.CpuCoreProbe`.

El juego no cambia: con ventana en primer plano y audio, Windows ya le da QoS alta. Las mediciones con ventana del 02-10 y el 03-10 no tienen picos (p95/p50 ≈ 1,15).

## p95 antes y después (`dev`, escenario control, 60 s, otros agentes compilando a la vez)

| Ejecución | Otros procesos | Frames en núcleo E | p95 por fase (ms) |
| --- | --- | --- | --- |
| Antes (`-TNStressDefaultQoS`) | 7 | 7-39 % | 6,6 / 11,0 / 10,4 / 8,9 / 10,9 / 9,5 |
| Después | 5 | 0 % | 17,0* / 6,8 / 6,6 / 7,1 / 7,0 / 7,8 |
| Antes (`-TNStressDefaultQoS`) | 14 | 52-94 % | 24,4 / 10,8 / 14,1 / 14,5 / 15,6 / 16,1 |
| Después | 5 | 0 % | 11,2 / 13,5 / 20,3 / 25,4 / 11,7 / 11,7 |

\* Primera fase con la carga de los otros procesos en su punto más alto.

Después de la corrección no queda ningún fotograma en núcleos E, pero los picos por competencia en los núcleos P siguen ahí si la máquina está saturada (cuarta fila). Con 24 procesos de carga artificial, las dos variantes suben el p50 a 9-14 ms. Ninguna corrección del juego quita eso.

## Recomendaciones

- Medir el control y el resto de escenarios sin compilaciones ni otros editores en marcha, y descartar las fases con `frames_on_efficiency_core_pct` > 0 o con p95/p50 > 1,5. En la máquina de referencia, en reposo, el control da un p50 de 4,1-4,9 ms y un p95 de 5,0-5,6 ms.
- Para medir render con ventana: la ventana en primer plano ya da QoS alta.
- `TN.Monkey` mide el fotograma igual y no pide QoS. Si sus cifras de rendimiento se van a usar, conviene aplicarle lo mismo.
- Se cierra el coste 6 del informe del 09-29: no es del juego.

## Sin verificar

- No se ha hecho ninguna traza con cambios de contexto (`-trace=contextswitch` necesita permisos de administrador para ETW). Que el hilo pasa a núcleos E se deduce de la sonda, que muestrea al final de cada fotograma, no de una traza del planificador.
- No se ha probado en una CPU no híbrida ni en una build empaquetada.
- La carga real del 09-29 no se puede reproducir con exactitud. Las cifras con carga dependen de lo que hicieran a la vez los otros agentes.
