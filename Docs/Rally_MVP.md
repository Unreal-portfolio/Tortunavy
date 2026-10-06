# Rally Tortuga: MVP jugable (2026-10-01)

Primera versión completa del Rally (objeto #40): buggy biplaza, carrera con puertas, puestos, meta, reaparición, torreta de la artillera con munición variada, piloto IA e interfaz. Va **separado** del lobby y del menú: se juega abriendo `LVL_Rally` directamente. La integración en el lobby será otra issue.

Fuentes: plan maestro §3.3 y §7.2, `Docs/Rally_Sistemas.md`, `Docs/Rally_E01B_y_Biplaza.md`, `Docs/superpowers/specs/modos/03-Rally.md`. Donde este documento difiere, manda la **Decisión** registrada en #40 (torreta en vez de los 4 ítems de caja).

## Cómo se juega

- `open LVL_Rally` abre R01 (`?Variant=R01_circuito_dunas`, por defecto). Desde #692 el Rally usa solo circuitos del generador de vueltas (`Docs/Rally_Circuitos_Vueltas.md`): R01 a R06; E01B, I03R, I04 e I06 salieron del selector y del repo. Opciones: `?Seats=1` (un buggy por jugador), `?Bots=N` (buggies con piloto IA), `?Laps=N`.
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
| Alga | caja, 2 cargas | sale a 3000 cm/s con el doble de gravedad (#770); al tocar buggy o suelo, o al acabarse en el aire, deja en el suelo de debajo (solo el escenario, con su inclinación) un charco de 6 m durante 5 s: agarre ×0,35, velocidad máx. ×0,5 y, al entrar, un derrape corto de hasta 90 °/s que decide el servidor, a **cualquier** buggy dentro, incluido el propio | 60 cm/s |
| Burbuja | caja, 1 carga | burbuja lenta que flota 6 s; el primer buggy que la toca (propio o rival) gana un escudo de 4 s que anula un impacto o un charco | 40 cm/s (#629) |
| Concha | caja, 2 cargas (#629) | corre por el suelo hacia donde apunta la torreta, rebota en las paredes y hace trompear al primero que toca | 250 cm/s |
| Concha teledirigida | caja, 1 carga (#629) | igual, pero persigue al buggy de justo delante (disparada hacia atrás sale recta) | 250 cm/s |
| Mortero | caja, 1 carga | explosión de 5 m: impulso vertical 450 cm/s sin vuelco forzado a todos los buggies dentro, también al propio | 700 cm/s (hacia atrás = turbo) |
| Tinta | caja, 2 cargas | mancha la pantalla de las dos ocupantes del buggy alcanzado 3 s | 60 cm/s |
| Ancla | caja (rara), 2 cargas | se engancha al buggy alcanzado y lo frena 2 s | 200 cm/s |
| Ráfaga de erizos (#715) | caja, 1 carga = 12 púas | mientras se mantiene el gatillo (principal o especial), una púa cada 0,125 s a 9000 cm/s (gravedad 0,3): 12 en 1,5 s. El servidor lleva la cadencia y para la ráfaga si dejan de llegar peticiones (sigue donde iba al volver a apretar); un bot la suelta entera. Cada púa que acierta: empujón lateral 120 cm/s, bamboleo 0,15 s y 2 de daño | 40 cm/s por púa |
| Medusa saltarina (#771) | caja, 2 cargas | sin proyectil: el servidor da al buggy propio un impulso vertical de 770 cm/s (unos 3 m en llano, como el del mortero). En el aire no se puede usar, las conchas pasan por debajo y los charcos no le tocan. Los bots botan con una teledirigida que les persigue a menos de 25 m o un charco delante, y si no, a los 4 s | — |
| Arpón (#772) | caja, 1 carga | proyectil a 7000 cm/s (gravedad 0,2) que, si se clava, une con una cuerda (`ATN_RallyHarpoonTether`, visible en todas las máquinas) al buggy propio con el alcanzado y tira de él hacia ese buggy 2 s con 1800 cm/s², hasta el 115 % de la velocidad punta y nunca más. Si falla, nada; si el alcanzado lleva escudo, se gasta y no hay remolque. Los bots lo usan contra el de delante a entre 15 y 60 m | 80 cm/s |
| Pez globo (#773) | caja, 2 cargas | se lanza en parábola corta (1500 cm/s) hacia donde apunte o hacia atrás y, donde cae (en un buggy, en el suelo de debajo), queda una mina (`ATN_RallyPufferMine`) apoyada según la normal 15 s. Se arma a los 0,5 s; quien la lanza es inmune 1,5 s. Un buggy a menos de 4 m la hincha 0,3 s y explota con la explosión del mortero (`MortarBlastAt`, 5 m) y su ráfaga. Los bots la sueltan con alguien detrás a menos de 40 m | 40 cm/s |

Cajas «?» (`ATN_KartItemBox`, #629; sustituyen a la antigua caja de munición, `ATN_RallyAmmoBox`, ya borrada): filas en las puertas pares y a mitad de tramo; reaparecen a los 3 s; el reparto pondera por puesto (los últimos, más Mortero, Burbuja y Concha teledirigida; los primeros, más Alga, Tinta y Concha; la Ráfaga de erizos, igual de la cabeza a la mitad de la tabla y menos hacia atrás; la Medusa saltarina y el Arpón, sobre todo los últimos; el Pez globo, sobre todo los primeros; el Ancla es rara en todos los puestos, algo menos para los primeros). Solo dan munición de la torreta: nada de turbos ni estrellas. En Karts (mapa generado) las mismas cajas siguen dando los objetos de Karts (`UTN_KartItemComponent`): la caja da lo que usa el vehículo (`ATN_KartBuggy::UsesDriverItems`).

## Circuitos por vueltas (#622)

`open LVL_Rally?Variant=R01_circuito_dunas` (con `?Laps=N`, `?Bots=N`) carga el circuito generado (`Docs/Rally_Circuitos_Vueltas.md`): circuito cerrado con la salida y la meta en la puerta 0, 9 puertas en orden, parrilla 2 × 4 detrás de la línea, barrera continua y las vueltas del manifest (3) si la URL no dice otra cosa. Lo que el C++ hace con los campos nuevos del manifest (`TNRallyCircuit`, `Rally/TN_RallyCircuit.h`):

- `bank_deg`: cada puerta se inclina con el peralte de su arco (`ATN_RallyTrack::GetBankDegAtArc`). Antes, quien iba por el lado bajo de una curva peraltada pasaba por debajo del volumen y la puerta no contaba. El volumen baja además 3 m por debajo de la calzada (`ATN_RallyGate::BelowRoadCm`) para los buggies con la suspensión hundida en una vaguada.
- `elements`: las notas de «salto» y «cresta» del copiloto y de la tableta salen del labio y la cima del manifest, no de la forma del eje. El piloto IA llega a cada labio a la velocidad de diseño × 0,9 (`JumpLipSpeedFactor`) y a cada horquilla a la de su radio con 0,6 g (`HairpinLateralG`), frenando con `BrakeDecelG`.
- E01B e I03R no traen esos campos y funcionan como antes.
- Medida (04-10, 1 bot, sin editor): 5 vueltas en 466 s (93 s por vuelta; la ideal del generador es de 67 s), sin atascos, vuelcos ni puertas perdidas.

## Lo que el Rally toma de Karts (#631)

Decisión del 04-10 en #627: Karts se queda entero (su modo, su mapa generado, sus objetos y sus bots) y el Rally de
`LVL_Rally` adopta su cámara, sus bots y su peso. `ATN_RallyGameMode` usa `ATN_RallyKartBuggy` (el `ATN_KartBuggy` sin
objetos: mirada libre con vuelta al centro y vista trasera, peso de la artillera en el giro) y el PlayerController de
Karts (HUD del peso). La dificultad del lobby (`?ProcDifficulty=`), las plazas del anfitrión, la parrilla completada con
bots hasta `MinTeams` y los bots ajustados a la dificultad (`ConfigureBot`) viven en `ATN_RallyGameMode`
(`TN_RallyGameModeLobby.cpp`) y valen para los dos modos; `ATN_KartGameMode` solo cambia la pista y el buggy. Viniendo
del lobby (`?FromLobby`), al acabar los resultados se vuelve a él, también desde el menú de pausa.

## Menú (#632)

El Rally es un modo propio del menú (`ETNProcGameMode::Rally`, aparte de Karts): «Crear partida», la pestaña «Misión» del
general y la pizarra ofrecen Cooperativo, Carrera, Supervivencia, Karts, Todos contra Todos, Rally y 2 vs 2
(`TNLobbyMission::MenuModes`). Con el Rally se elige el circuito (`TNLobbyMission::RallyMapOptions`: los manifests de
`Scripts/terrain_volumes/Variants` con `"mode": "rally"`) y, con el Rally o Karts, las tortugas por buggy. El lobby viaja a
`LVL_Rally?Variant=<v>?FromLobby` y al acabar se vuelve a él. En una build cocinada sin manifests el Rally no se ofrece,
como Todos contra Todos sin su arena. El 2 vs 2 se puede elegir siempre; si al salir no son cuatro, se juega Carrera.

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
| Disparo de la artillera (#333) | `Vehicles/TN_BuggyGunnerPawn.*`, `Vehicles/TN_RallyTracerFX.*` | `ServerFire` lleva la dirección en el mundo que ve el cliente; el servidor la usa si se separa ≤ 35° de la suya (`TNRallyTurret::ResolveClientFireDirection`, `MaxCameraAimErrorDeg`). El cliente ve un trazador local inmediato |
| Salida del proyectil hacia la mira (#717) | `Vehicles/TN_RallyTurretLogic.*`, `Vehicles/TN_BuggyGunnerPawn.cpp`, `Vehicles/TN_BuggyTurretComponent.cpp` | «Va donde quiere y no donde apunta»: (1) el proyectil sumaba la velocidad del buggy a la de salida, y un disparo lateral con el buggy a 100 km/h salía unos 25° hacia delante; ahora `TNRallyTurret::ShotVelocity` elige la velocidad de salida para que la resultante vaya hacia el disparo (hacia delante y hacia atrás, igual que antes); (2) la cámara de hombro mira 8° por debajo del cañón y el centro de la pantalla (la mira) no era donde llegaba el tiro: la artillera traza un rayo desde la cámara hasta el primer obstáculo (o 80 m) y el tiro sale de la boca hacia ese punto, subido lo que cae el proyectil (`AimedShotDirection`, `ComputeShotDirection`). Con gafas o en primera persona sin cámara de hombro, el eje del cañón. Tests `Tortunabo.Rally.Turret.ShotAim.*` y `Tortunabo.Rally.Measure.LateralShot` (con física) |
| Voz (#329) | `Voice/` | La voz por proximidad del juego en el peón (buggy o artillera); las dos ocupantes comparten interfono (`ITN_VoiceIntercom`) y se oyen a volumen completo. Regla en `TNVoiceRouting` |

Red: el servidor tiene la autoridad del buggy (movimiento replicado de Chaos, `PredictiveInterpolation`), de las puertas, los puestos y los impactos. Los clientes solo mandan entradas (conducción por el movimiento de Chaos, apuntado y disparo por RPC validada).

## Pruebas sin editor (fuera de Shipping)

- Nivel: `Scripts/build_rally_level.py` crea `LVL_Rally` (editor headless **sin** `-nullrhi`: con `-nullrhi` el editor revienta al colocar actores del proyecto). No está en `MapsToCook`.
- Carreras solo de IA: `LVL_Rally?Variant=V?Bots=4?AutoStart?Races=10?RaceTimeout=300 -server -nullrhi`; cada carrera deja una línea `[RallyStats]` (terminados, atascos, vuelcos, caídas, fuera de pista, ganador).
- Comandos: `TN.Rally.Measure [s] [cerrar]` (#100; da el 0-60 y el 0-100 km/h), `TN.Rally.DebugEffects [vida] [espera] [turbo]` (humo a media vida y llama del turbo sin combate ni mando, #294 y #296), `TN.Rally.StatusLater`, `TN.Rally.LocalFire`, `TN.Rally.DebugCosmetics`, `TN.Rally.DebugTeleport`, `TN.Rally.ShotLater <s> [nombre]` (captura con la interfaz en `Saved/Screenshots`), `TN.Rally.ViewShot <s> <fichero.png> <x> <y> <z> <pitch> <yaw> [fov]` (captura sin interfaz desde una cámara fija, p. ej. la cenital de la variante), `TN.Rally.TabletWorldLater <s> [1|0]` (la pantallita en un panel 3D sobre el peón, o de vuelta a la pantalla), `TN.Rally.MeasureTurn [km/h = 20] [cerrar]` (radio de giro con el volante a tope, #606; objetivo < 6 m a 20 km/h), `TN.Rally.RampHold [grados = 15] [cerrar]` (buggy frenado como en la parrilla en una rampa: desplazamiento en 5 s, objetivo < 5 cm, #611) y `TN.Rally.PanelMasked 0|1` (paneles 3D translúcidos o enmascarados, para comparar la estela, #605).
- Dirección (#606): 38° de rueda interior a cualquier velocidad (`UTN_BuggyData::MaxSteerAngleDeg`), volante lineal y rápido (`SteerRiseRate` 7/s). El piloto IA frena antes de las curvas que vienen en 90 m (`BrakeProbeCm`, 0,5 g) y acota la dirección a 0,9 g de lateral (`TNBuggy::SafeSteerFraction`) para no volcar.
- Parrilla (#611): durante la espera y la cuenta atrás, freno de estacionamiento de Chaos (`SetParked`) y el buggy anclado a su hueco (`TNBuggy::GridHoldVelocity`); el pedal de freno parado mete la marcha atrás y no sujetaba el buggy en cuesta.
- Barrera (#303): continua a los dos lados de todo el trazado (`FBarrierParams::bContinuous`), de vallas y neumáticos por trozos de 120 m; el registro `[RallyDressing] Barrera ...` y `Tortunabo.Rally.Dressing.Tires.BarrierHasNoGaps` (en R02 desde #692) comprueban que ningún hueco pasa de 110 cm (una tortuga).
- Torreta y artillera (#435): `TN.Rally.DebugTurretFit [espera] [carpeta o -] [cerrar 0|1] [carpeta de volcado]` sienta a la artillera visual, recorre la vuelta entera cada 30° con el cabeceo mínimo, recto y máximo, y deja por puntería una línea `[Torreta]` con los puntos de cada pieza dentro de la artillera (malla deformada en CPU) o de la carrocería y su holgura, más fotos cenital y lateral si hay carpeta. Con paso fijo para que los muelles de la animación no se disparen tras cada medida: `LVL_Rally?Seats=1 -game -windowed -UseFixedTimeStep -FPS=30 -ExecCmds="TN.Rally.DebugTurretFit 12 <carpeta> 1"`. Con `-dpcvars=TN.Rally.DebugRiderAirborne=1` mide la pose del salto (encogida y agarrada); con carpeta de volcado (cuarto argumento) deja también la artillera, la carrocería, el aro, el carro y el cañón de cada puntería en texto.
- Capturas sin editor: `LVL_Rally?Seats=1 -game -RenderOffscreen -windowed -ResX=1920 -ResY=1080 -ForceRes -ExecCmds="TN.Rally.ShotLater 17 Panel, TN.Rally.DebugQuitAfter 22"`. Con `-nullrhi` no se dibujan widgets: la pantallita y los paneles solo se ejercitan con render.
- Medida en I03R: punta 113,5 km/h, 0-100 km/h 6,6 s, frenada desde 60 km/h 12,9 m. En E01B: punta 99,8 km/h (no llega a 100 en 25 s), frenada 13,2 m.
- Piloto IA (2026-10-01, 7 carreras por variante): I03R 23/28 terminados, 3 atascos, 1 vuelco, 3 caídas al agua, ganador 119-123 s; E01B 23/28, 2 atascos, 1 vuelco, 1 caída, ganador 111-120 s. Los fallos se concentran tras choques entre buggies: en I03R, en la curva de la cabeza (arco 200-285 m, con agua por fuera); en E01B, fuera de la calzada en un talud.

## Fuera de este MVP

Física síncrona con subpasos (#101), `GeoRegion` y validador completo del corredor (#107–#109), decimado (#111), atribuciones (#112), skins y tienda (#114, #115), sonda de red con 8 buggies (#118), copa de 3 carreras y la integración en lobby y menú.
