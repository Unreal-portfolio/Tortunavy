# Rally Tortuga: MVP jugable (2026-10-01)

Primera versión completa del Rally (objeto #40): buggy biplaza, carrera con puertas, puestos, meta, reaparición, torreta de la artillera con munición variada, piloto IA e interfaz. Va **separado** del lobby y del menú: se juega abriendo `LVL_Rally` directamente. La integración en el lobby será otra issue.

Fuentes: plan maestro §3.3 y §7.2, `Docs/Rally_Sistemas.md`, `Docs/Rally_E01B_y_Biplaza.md`, `Docs/superpowers/specs/modos/03-Rally.md`. Donde este documento difiere, manda la **Decisión** registrada en #40 (torreta en vez de los 4 ítems de caja).

## Cómo se juega

- `open LVL_Rally` abre E01B (`?Variant=E01B_espana_rally`, por defecto); también `?Variant=I03R_tortuga_magna`, `C01_camino` y las demás variantes con `checkpoints_uu`). Opciones: `?Seats=1` (un buggy por jugador), `?Bots=N` (buggies con piloto IA), `?Laps=N`.
- Biplaza (por defecto): los jugadores se emparejan por orden de llegada; la 1.ª de cada pareja conduce y la 2.ª es la artillera. Si una tortuga va sola, conduce y dispara ella con apuntado automático.
- Semáforo de 3 s; salir antes corta el motor 1 s. Cuando llega el primer buggy quedan 20 s; después, resultados y, a los 15 s, carrera nueva en el mismo mapa.

## Controles (Enhanced Input creado en C++, sin assets)

| Acción | Conductora | Artillera |
|---|---|---|
| Acelerar / frenar / girar | W / S / A-D · RT / LT / stick izq. | — |
| Freno de mano | Shift izq. · X | — |
| Turbo (mientras haya carga; se recarga derrapando con freno de mano y en el aire) | Espacio · A | — |
| Enderezar (pulsar) / reaparecer (mantener 1,5 s) | R · Y | R · Y |
| Apuntar | automático si va sola | ratón · stick der. |
| Coco (básica) | clic izq. · RB (sola) | clic izq. · RT |
| Munición especial | clic der. · LB (sola) | clic der. · LT |
| Disparar hacia atrás | Q · B (sola) | — (apunta detrás) |
| Cambiar de munición | rueda del ratón · cruceta izq./der. (sola) | rueda del ratón · cruceta izq./der. |
| Cantar la próxima nota a la conductora (#330) | — | F · A |
| Aviso rápido: «¡Turbo ya!» / «¡Frena!» (#330) | — | 1 · cruceta arriba / 2 · B |
| Pulsar para hablar (ajustes de voz) | V · cruceta abajo | V · cruceta abajo |
| Espectador (en meta o sin buggy): vista anterior / siguiente | A / D · ← / → · LB / RB | A / D · ← / → · LB / RB |

## Torreta y munición

El retroceso es la mecánica central: cada disparo empuja al buggy propio en sentido contrario. Disparar hacia delante frena un poco; disparar hacia atrás acelera y castiga al que viene detrás. Todo lo decide el servidor (`Server` RPC validada, impulsos con `AddImpulse` en el servidor y `ForceNetUpdate`).

| Munición | Origen | Efecto [1.ª pasada] | Retroceso |
|---|---|---|---|
| Coco | infinita; 6 disparos seguidos sobrecalientan 2,5 s | impacto: impulso lateral 350 cm/s y bamboleo de dirección 0,4 s | 120 cm/s |
| Alga | caja, 2 cargas | al tocar buggy o suelo deja un charco de 6 m durante 5 s: agarre ×0,5 y velocidad máx. ×0,6 a **cualquier** buggy dentro, incluido el propio | 60 cm/s |
| Burbuja | caja, 1 carga | burbuja lenta que flota 6 s; el primer buggy que la toca (propio o rival) gana un escudo de 4 s que anula un impacto o un charco | 0 |
| Mortero | caja, 1 carga | explosión de 5 m: impulso vertical 450 cm/s sin vuelco forzado a todos los buggies dentro, también al propio | 700 cm/s (hacia atrás = turbo) |
| Tinta | caja, 2 cargas | mancha la pantalla de las dos ocupantes del buggy alcanzado 3 s | 60 cm/s |
| Ancla | caja (rara), 2 cargas | se engancha al buggy alcanzado y lo frena 2 s | 200 cm/s |

Cajas de munición: filas en las puertas pares y a mitad de tramo; reaparecen a los 3 s; el reparto pondera por puesto (los últimos, más Mortero y Burbuja; los primeros, más Alga y Tinta; el Ancla es rara en todos los puestos, algo menos para los primeros).

## Arquitectura

| Pieza | Carpeta | Qué hace |
|---|---|---|
| `ITN_RallyVehicle` | `Rally/TN_RallyVehicle.h` | Contrato buggy ↔ carrera (plazas, reaparición, motor, munición, mando IA) |
| `ATN_Buggy` | `Vehicles/` | Port de `AHYBuggy` sin `Cargo` ni fichas: Chaos Vehicles, derrape, enderezado, cámara, asientos y tortugas sentadas. Modelo de `Art/Source/Vehicles/Buggy` (#290): chasis de física `SK_TN_BuggyChassis` (PhysicsAsset copiado del template, sin AnimBP), carrocería y ruedas con sockets `Seat_*` y `Muzzle_Gunner`, skins Mar, Alga y Medusa con la pintura del equipo; lo importa `Scripts/tools/import_buggy_rally.py` |
| `ATN_BuggyGunnerPawn` | `Vehicles/` | Peón de la artillera, sujeto a `Seat_Gunner`, con cámara y apuntado replicado |
| `UTN_BuggyTurretComponent` | `Vehicles/` | Apuntado, calentamiento, munición especial, disparo con retroceso |
| `ATN_RallyProjectile` y efectos | `Vehicles/` | Proyectiles replicados, charco de alga, burbuja, explosión, tinta |
| `ATN_RallyTrack` | `Rally/` | Lee `checkpoints_uu` del manifest de la variante; spline, puertas, bordes con mallas existentes, parrilla y cajas |
| `ATN_RallyGameMode` / `GameState` / `PlayerState` | `Rally/` | Emparejado, fases, puertas en orden, vueltas, puestos a 5 Hz, contramano, reaparición, meta y resultados |
| `TNRally::` (lógica pura) | `Rally/TN_RallyLogic.h` | Orden de puestos, contramano, validación de puerta (regla del 60 %), puntos; con tests `Tortunabo.Rally.*` |
| `ATN_RallyAIController` | `Rally/` | Piloto IA: sigue la spline, frena en curva, dispara al de delante |
| `UTN_RallyHUDWidget` | `Rally/` | Para todas: semáforo, contramano, reaparición, tinta, cierre, espectador y resultados. Solo la artillera: velocidad, turbo, puesto, vuelta, munición, vida y mira (C++ con `TN_RaceUIKit`) |
| `UTN_RallyDashboardComponent` | `Rally/UI/` | Interfaz diegética de la conductora (#299): velocidad, turbo y vida en el salpicadero; puesto y vuelta en el cartel del arco; encima del salpicadero, la placa de la nota cantada (#331). Tres `UWidgetComponent` solo en la máquina de la conductora |
| `UTN_RallyCopilotTablet` | `Rally/UI/` | Pantallita de la artillera (mapa, notas de copiloto, perfil de 400 m con las cajas de munición, munición) y versión compacta para la conductora sola. Presentación `Screen` (con `TNVR::AddToScreen`, como el HUD) o `World` (#334: `PresentInWorldFor` la aloja en un `UWidgetComponent` y la pinta igual, ocupando el panel; tamaño de la maqueta en `TNRallyTabletLayout::DesignSize`) |
| `UTN_RallyCopilotComponent` | `Rally/` | Copiloto automático (#331), solo en la máquina de la conductora y solo si su buggy va sin artillera humana: canta cada nota unos 3 s antes de llegar (nunca a menos de 60 m) con una señal al lado de la curva (tantos pitidos como el grado; cresta, salto y agua con su sonido, `SFX_Rally_Call_*`) y una placa de 1,5 s sobre el salpicadero («‹ 3» y la nota). Regla en `TNRallyCopilot` (tests `Tortunabo.Rally.Copilot.*`). `TN.Rally.Copilot` 0/1/2: apagado, automático, o forzado también en la plaza de artillera (pruebas con `?BotDriver`) |
| `UTN_RallyCameraDirector` / `ATN_RallyPodium` | `Rally/` | Llegada (#306): plano lateral a cámara lenta (solo sin VR y sin más jugadores), podio flotante sobre la meta donde el servidor aparca a cada buggy que llega, espectador con dron; con VR, corte seco y vista desde el asiento. Lógica en `TNRallyCamera` |
| Cantos de la artillera (#330) | `Rally/TN_RallyCrewCalls.*`, `Vehicles/TN_BuggyGunnerPawn_Calls.cpp` | La artillera canta la próxima nota (la primera de los próximos 600 m) o un aviso rápido; el servidor la vuelve a buscar en su pista y la valida (sentada, 0-600 m, 0,6 s entre cantos) y la manda solo a las dos ocupantes (`ClientRallyCrewCall`), que la oyen con la señal del copiloto y la conductora ve la placa 1,5 s. Tests `Tortunabo.Rally.CrewCalls.*`; `TN.Rally.LocalCall [0/1/2] [espera] [veces]` para probarlo sin teclado |
| Confirmación de impactos (#332) | `Rally/TN_RallyHitReport.*` | El servidor avisa a las ocupantes del buggy que dispara y del alcanzado (`ClientRallyHitReport`): marca en la mira 0,3 s y sonido corto a quien da; las 3 últimas líneas en la tableta («Coco → Rubi · morro», «Te da Rubi: Tinta · cola»). Tests `Tortunabo.Rally.HitReport.*`; `TN.Rally.TabletOpenLater <espera>` abre la tableta para las fotos |
| Disparo de la artillera (#333) | `Vehicles/TN_BuggyGunnerPawn.*`, `Vehicles/TN_RallyTracerFX.*` | `ServerFire` lleva la dirección en el mundo que ve el cliente; el servidor la usa si se separa ≤ 12° de la suya (`TNRallyTurret::ResolveClientFireDirection`). El cliente ve un trazador local inmediato |
| Voz (#329) | `Voice/` | La voz por proximidad del juego en el peón (buggy o artillera); las dos ocupantes comparten interfono (`ITN_VoiceIntercom`) y se oyen a volumen completo. Regla en `TNVoiceRouting` |

Red: el servidor tiene la autoridad del buggy (movimiento replicado de Chaos, `PredictiveInterpolation`), de las puertas, los puestos y los impactos. Los clientes solo mandan entradas (conducción por el movimiento de Chaos, apuntado y disparo por RPC validada).

## Pruebas sin editor (fuera de Shipping)

- Nivel: `Scripts/build_rally_level.py` crea `LVL_Rally` (editor headless **sin** `-nullrhi`: con `-nullrhi` el editor revienta al colocar actores del proyecto). No está en `MapsToCook`.
- Carreras solo de IA: `LVL_Rally?Variant=V?Bots=4?AutoStart?Races=10?RaceTimeout=300 -server -nullrhi`; cada carrera deja una línea `[RallyStats]` (terminados, atascos, vuelcos, caídas, fuera de pista, ganador).
- Comandos: `TN.Rally.Measure [s] [cerrar]` (#100; da el 0-60 y el 0-100 km/h), `TN.Rally.DebugEffects [vida] [espera] [turbo]` (humo a media vida y llama del turbo sin combate ni mando, #294 y #296), `TN.Rally.StatusLater`, `TN.Rally.LocalFire`, `TN.Rally.DebugCosmetics`, `TN.Rally.DebugTeleport`, `TN.Rally.ShotLater <s> [nombre]` (captura con la interfaz en `Saved/Screenshots`), `TN.Rally.ViewShot <s> <fichero.png> <x> <y> <z> <pitch> <yaw> [fov]` (captura sin interfaz desde una cámara fija, p. ej. la cenital de la variante), `TN.Rally.TabletWorldLater <s> [1|0]` (la pantallita en un panel 3D sobre el peón, o de vuelta a la pantalla).
- Torreta y artillera (#435): `TN.Rally.DebugTurretFit [espera] [carpeta o -] [cerrar 0|1] [carpeta de volcado]` sienta a la artillera visual, recorre la vuelta entera cada 30° con el cabeceo mínimo, recto y máximo, y deja por puntería una línea `[Torreta]` con los puntos de cada pieza dentro de la artillera (malla deformada en CPU) o de la carrocería y su holgura, más fotos cenital y lateral si hay carpeta. Con paso fijo para que los muelles de la animación no se disparen tras cada medida: `LVL_Rally?Seats=1 -game -windowed -UseFixedTimeStep -FPS=30 -ExecCmds="TN.Rally.DebugTurretFit 12 <carpeta> 1"`. Con `-dpcvars=TN.Rally.DebugRiderAirborne=1` mide la pose del salto (encogida y agarrada); con carpeta de volcado (cuarto argumento) deja también la artillera, la carrocería, el aro, el carro y el cañón de cada puntería en texto.
- Capturas sin editor: `LVL_Rally?Seats=1 -game -RenderOffscreen -windowed -ResX=1920 -ResY=1080 -ForceRes -ExecCmds="TN.Rally.ShotLater 17 Panel, TN.Rally.DebugQuitAfter 22"`. Con `-nullrhi` no se dibujan widgets: la pantallita y los paneles solo se ejercitan con render.
- Medida en I03R: punta 113,5 km/h, 0-100 km/h 6,6 s, frenada desde 60 km/h 12,9 m. En E01B: punta 99,8 km/h (no llega a 100 en 25 s), frenada 13,2 m.
- Piloto IA (2026-10-01, 7 carreras por variante): I03R 23/28 terminados, 3 atascos, 1 vuelco, 3 caídas al agua, ganador 119-123 s; E01B 23/28, 2 atascos, 1 vuelco, 1 caída, ganador 111-120 s. Los fallos se concentran tras choques entre buggies: en I03R, en la curva de la cabeza (arco 200-285 m, con agua por fuera); en E01B, fuera de la calzada en un talud.

## Fuera de este MVP

Física síncrona con subpasos (#101), `GeoRegion` y validador completo del corredor (#107–#109), decimado (#111), atribuciones (#112), skins y tienda (#114, #115), sonda de red con 8 buggies (#118), copa de 3 carreras y la integración en lobby y menú.
