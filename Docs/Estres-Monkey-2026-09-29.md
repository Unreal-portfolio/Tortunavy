# Monkey y estrés — 2026-09-29

Rama `macro-update`, build DebugGame, `-game -nullrhi` sobre `LVL_BeachRace`. Tests: 104/104 (97 previos + 7 nuevos).

## Herramientas

| Qué | Cómo | Salida |
| --- | --- | --- |
| Monkey | consola `TN.Monkey <s> [semilla] [jugadores]` o `-TNMonkey=<s>:<semilla> [-TNMonkeyPlayers=N] [-TNMonkeyWarmup=s] [-TNMonkeyOut=ruta] [-TNQuitWhenDone]` | `Saved/Monkey/<fecha>.json` |
| Estrés | consola `TN.Stress <light\|heavy\|race8\|control> [s]` o `-TNStress=<escenario> [-TNStressSeconds=60] [-TNStressWarmup=10] [-TNStressNoMonkey] [-TNStressDefaultQoS]` (por defecto pide QoS alta y solo núcleos P; el informe dice qué porcentaje de fotogramas cayó en núcleos E) | `Saved/Stress/<escenario>_<fecha>.json` |
| Test | `Tortunabo.Monkey.Headless30s` lanza un proceso hijo (monkey 30 s, semilla 11) y falla con asserts/ensures, caídas sin rescatar, proceso caído o sin informe | `Saved/Monkey/automation-headless30s.json` |

Notas de uso:

- `-ExecCmds` no se ejecuta con `-game`: por eso los modos por línea de órdenes.
- En Git Bash exportar `MSYS2_ARG_CONV_EXCL="*"`, o `/Game/...` se convierte en una ruta de Git.
- Solo compilaciones que no son Shipping. Código en `Source/Tortunabo/{Public,Private}/Testing/`.
- El monkey sube el tope de 4 jugadores locales del motor (`MaxSplitscreenPlayers`) para poder llegar a 8.
- Pausa/reanudar solo actúa con interfaz real: en `-nullrhi` se cuenta como `pause_skipped_no_ui`.
- Red local con varios procesos: `Scripts/tools/red_local.py` ([`Pruebas_Red_Local.md`](Pruebas_Red_Local.md)). A mano, todos con `-NetDriverOverrides=/Script/OnlineSubsystemUtils.IpNetDriver -ini:Engine:[OnlineSubsystem]:DefaultPlatformService=Null -NoSteam`; servidor con mapa `?listen`, cliente con `127.0.0.1`.

## Resultados del monkey (3 × 60 s)

| Semilla | Jugadores | Asserts | Caídas sin rescatar | Atascos | Errores de log | Rescates | Fotograma medio |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 101 | 2 | 0 | 0 | 0 | 0 | 0 | ~7 ms |
| 202 | 4 | 0 | 0 | 0 | 0 | 0 | 7,0 ms (p99 9,1) |
| 303 | 8 | 0 | 0 | 0 | 0 | 0 | 17,5 ms (máx. 36,7) |

Las tres pasan. Avisos de `TN.Shell.Debug` (todos en el servidor): 0 en la 101, 6 en la 202 (1 caja bajo el terreno, 5 saltos de velocidad) y 4 en la 303 (saltos de velocidad).
Correcciones de red: 0 (todo en un proceso; ver "Pendiente").

## Bugs y hallazgos

1. **Bola del caparazón: salto de velocidad al nacer sobre el terreno del generador** (11 avisos en 3 runs, ~1,8 s de edad). La caja aparece con su parte de abajo 14-127 cm bajo `ProceduralMeshComponent_16/17` de `TN_BeachRaceGenerator_0` y la depenetración le da 940-1470 cm/s de golpe (impulso de contacto 21 000-76 000). `TN_ShellBody.cpp` (instrumento en `~l.300-338`). Probable causa: la caja se crea con la parte baja dentro de la malla. No se ha corregido.
2. **Caja del caparazón 93 cm bajo el terreno junto a una fortaleza** (semilla 202, caja 11, 0,93 s de edad, contacto con `TN_BeachFortress_0.CastleCollision` y `ProceduralMeshComponent_17`, impulso 432 690). Posición (58,5; 10,5; 34,1) m. Sin corregir.
3. **Lanzables con tope por clase**: `ATN_RaceMine::MaxInWorld = 12` (`TN_RaceMine.cpp:39`), `ATN_RaceFrisbee::MaxInWorld = 6` (`TN_RaceFrisbee.cpp:30`). De 100 o 500 pedidos solo viven 28 a la vez. No es un fallo: el escenario los repone cada 0,5 s.
4. **Cliente remoto sin monkey** (resuelto en #80): con `-TNMonkey` en un cliente, si no llegaba a conectar (el servidor aún no escuchaba) volvía a `LVL_Menu`, la sesión arrancaba ahí sin jugadores y el informe salía vacío. Ahora `-TNMonkeyNet=client` (o un primer argumento que es una dirección) espera al mundo conectado; ver [`Pruebas_Red_Local.md`](Pruebas_Red_Local.md).
5. Aviso de datos en cada arranque: `DT_Helmets` no tiene `Helmet_Default` (`[PC] ServerSyncUnlockedHelmets`), y la malla no tiene los sockets `Pata1/Pata2/Brazo1/Brazo2/Cola/Cabeza` (animación de huesos desactivada en cada tortuga). Ruido de arranque, no de partida.

Sin asserts, ensures, errores de log, caídas bajo el mapa, atascos ni rescates en ~13 minutos de juego aleatorio acumulado.

## Costes (top 10)

Fotograma en ms, DebugGame headless, monkey moviendo las tortugas. El control (sin crear nada, 60 s en 6 fases) deriva de 10,8 a 8,0 ms; se resta esa deriva. Los costes 1, 2, 4 y 5 salen de la ejecución `heavy` en un solo proceso, con los lanzables antes que las cajas (orden anterior al último commit); la tabla de fotograma por fase y la de red son de la ejecución con dos procesos y el orden actual.

| # | Coste | Cifra |
| --- | --- | --- |
| 1 | Lanzables vivos (mina, disco, cangrejo teledirigido), heavy | ~+10 ms con 28 vivos (0,36 ms cada uno); pico de 96 ms al crearlos |
| 2 | 100 cangrejos (gigantes y ermitaños), heavy | +7,1 ms netos (0,07 ms cada uno); +21 MB |
| 3 | 8 tortugas vs 4 (race8/semilla 303 vs 202) | 17,5 ms frente a 7,0 ms: ~2,5 ms por tortuga a partir de la 5.ª, no lineal |
| 4 | 50 tanques de juguete, heavy | +7,7 ms netos (0,15 ms cada uno) |
| 5 | 50 zonas de gaviotas, heavy | +3,5 ms netos (0,07 ms cada una); +20 MB |
| 6 | Picos periódicos en el control | p50 5,5-8,7 ms pero p95 17-21 ms: ~65 picos por 10 s, uno cada ~40 ms (25 Hz). **No es del juego** (2026-10-03, #57): el planificador de Windows mueve el hilo de juego a núcleos E de la CPU híbrida cuando hay otros procesos cargando la máquina. Ver `Docs/Analisis/2026-10-03-Pico-25Hz-control.md` |
| 7 | Actores con Tick en reposo | 549-584 de ~700 actores; 253 `BP_ScorePickup_C`, cada uno con `WidgetComponent` y `TN_PickupGlowComponent` con Tick |
| 8 | 100 cajas de objetos | ~0 ms, pero +194 componentes con Tick (2 por caja) |
| 9 | Hitch al crear grupos | 21-96 ms en el fotograma de creación (lanzables 96, cangrejos 47, tanques 41) |
| 10 | Memoria por grupo (heavy) | cangrejos +21 MB, gaviotas +20, lanzables +13, cajas +9, tanques +7; total ~2013 → 2083 MB |

Fotograma medio por fase (heavy, red): base 9,7 → cangrejos 16,2 → gaviotas 22,1 → tanques 27,5 → cajas 27,4 → lanzables 29,6 (p95 37,2). light: 13,9 → 14,4 → 16,8 → 18,5 → 18,7 → 19,2.

## Red (por conexión, un cliente por IP local)

| Fase | light KB/s salida | heavy KB/s salida | heavy máx. | Entrada |
| --- | --- | --- | --- | --- |
| Base | 4,3 | 5,5 | 6,0 | 5,5-6,2 |
| + cangrejos | 5,2 | 8,5 | 11,4 | 4,3 |
| + gaviotas | 5,3 | 7,8 | 9,4 | 4,3 |
| + tanques | 5,5 | 10,5 | 12,7 | 3,3-4,4 |
| + cajas | 5,3 | 10,1 | 12,9 | 2,8-4,6 |
| + lanzables | 7,8 | 10,9 | 12,9 | 3,5 |

Actores replicados: ~634-665 en la base, 968 (heavy) con todo. Muy por debajo de `MaxClientRate=200000`. Las cifras son de un cliente quieto en un solo proceso local: no incluyen pérdida ni latencia.

## Recomendaciones (por prioridad)

1. **Perfilar el pico de 25 Hz** (p95 2-3 veces la mediana en reposo): con `-trace=cpu,frame` y Unreal Insights sobre `control`. Es lo que más aporta al p95 sin tocar contenido.
2. **Bajar el Tick de `BP_ScorePickup_C` y `TN_PickupGlowComponent`** (253 actores y 119 componentes con Tick en el mapa): tick por distancia o materiales por parámetro global. Es el mayor número de actores con Tick.
3. **Presupuesto de tortugas**: 8 tortugas cuestan 2,5 veces las de 4; mirar qué escala peor (`TickLegAnimation`/`TickEmote` por tortuga, el sondeo de interacción de 10 Hz con `OverlapMultiByObjectType` por tortuga, `UTN_TurtleFoleyComponent`).
4. **Cangrejos y tanques**: 0,07-0,15 ms cada uno; con 200 ya son ~19 ms. Poner LOD de Tick (los `Enemy` lejanos ya van "despacio": comprobar que baja de verdad) y tope de activos por radio.
5. **Crear enemigos en varios fotogramas** (hitch de 47-96 ms al hacerlo de golpe; en juego real, `SpawnElement` del generador).
6. **Corregir el nacimiento de la caja del caparazón** (bug 1 y 2): separarla del suelo al crearla o usar `SetPhysicsLinearVelocity` tras la depenetración.

## Pendiente

- ~~Render y GPU no medidos~~: medidos con ventana el 2026-10-02 en [`Rendimiento_GPU_2026-10-02.md`](Rendimiento_GPU_2026-10-02.md) (#58). En reposo limita la GPU (9,7 ms de 11,5); con `heavy`, el hilo de render (17,2 ms) y el de juego (11,7 ms). Script: `Scripts/tools/medir_render_gpu.py`.
- ~~El monkey del cliente remoto (para `net_corrections`) no arrancó~~: resuelto en #80, medido en [`Pruebas_Red_Local.md`](Pruebas_Red_Local.md).
- ~~Sin `stat unit` de verdad~~: hilos de juego y render, RHI y GPU por separado en [`Rendimiento_GPU_2026-10-02.md`](Rendimiento_GPU_2026-10-02.md). Las cifras de este informe son de DebugGame headless; en Development el hilo de juego cuesta 2,5-3 veces menos.
- Pausa/reanudar sin probar (solo con interfaz).
- Sin perfil por clase de coste de Tick: los costes por grupo salen de restar fases. Falta Insights.
