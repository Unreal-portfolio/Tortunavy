# Catálogo de puzzles: plantillas para Coop, 2 vs 2, parkour y zona de pensar

Fecha: 2026-09-29 · Rama: `macro-update` · Estado: diseño (sin código).
Relacionados: `Docs/2026-09-29-Plan-Maestro-Modos-y-Mapas.md` (§3.1 Coop, §3.6 arte, §7.9 el 2 vs 2), `Docs/superpowers/specs/2026-09-29-foto-a-mapa-design.md` (§4 `MapSketch.json`, §6 plantillas), `Docs/Rally_E01B_y_Biplaza.md`, `Docs/Catalogo-Mapas-2026-09-29.md`.

## 0. Resumen

- **15 plantillas**: 6 de Coop (grupo), 5 de 2 vs 2 (pareja, con `sabotage` como módulo), 3 de parkour y 1 de zona de pensar con dos variantes. Parkour y zona de pensar sirven a Coop y 2 vs 2.
- **0 assets nuevos**. Toda pieza nueva usa malla procedural (`TNBeachProp`, `TN_BeachPropMeshes.h`) y SFX de los sintetizadores existentes (plan §3.6). Los borradores IA de `Art/Library/IA` se enchufan después sustituyendo la malla; ninguna plantilla depende de ellos.
- **C++ que falta**: 6 piezas nuevas y 4 modificaciones pequeñas (§2). Las plantillas que dependen de una pieza pendiente se marcan `PENDIENTE` y el validador de Foto→Mapa las rechaza hasta que exista (spec §6).
- **Contradicción a resolver por el director (§6.1)**: ya existe un 2 vs 2 en `ETNProcGameMode::TwoVsTwo` sobre mapa procedural con carriles; el plan §7.9 lo redefine como modo volumétrico de camino fijo.

## 1. Convenciones

**Espacio local.** +X = sentido de avance del camino (como `ATN_ProcThrowWall`); unidades en metros en este documento (en el motor, ×100). La plantilla se coloca con `at` (metros del boceto) y `yaw_deg`; las piezas llevan la etiqueta `P<id>_<rol>` para que `ManagerTag`/`TargetActorTag` las enlacen sin referencias duras (patrón actual de `ATN_PressurePlate.ManagerTag` y `FTN_TransformAction.TargetActorTag`).

**Capacidades de la tortuga usadas** (valores del código; los marcados «supuesto» se verifican en playtest): salto 1,5 m (`ThrowWall`), doble salto (dive), caparazón (Ctrl), agarrar solo a quien está en caparazón y lanzar como la bola, noqueo desde `MinKnockdownSpeed` 600 cm/s, escape de agarre machacando salto. Velocidad de marcha ≈ 6 m/s (supuesto).

**Tiempo objetivo.** Mediana de un grupo que ya conoce la mecánica y no falla. La primera vez se espera 2× a 3×. Coop: 30 a 90 s por puzzle. 2 vs 2: 20 a 45 s por puzzle, para que una carrera de 5 a 6 min lleve 5 a 6 puzzles más tramos de parkour.

**Dificultad.** 1 = se entiende sin cartel, 2 = requiere hablar, 3 = requiere coordinación de tiempo o ensayo y error.

**Fallo y reintento (regla común).** Ningún puzzle mata ni bloquea la partida. Todo fallo devuelve la pieza a su estado inicial (`Reset()` en el servidor, §2 `UTN_PuzzleStateComponent`) tras un enfriamiento de 3 s en 2 vs 2 y de 1 s en Coop, y suena la señal de fallo (§ Doc 2). Si un jugador queda en una zona sin salida (arriba de un muro sin rampa), el rescate del plan §2.3 lo devuelve al último punto seguro.

**Autoridad y réplica (regla común).**
- El servidor decide toda activación, todo temporizador y todo reinicio. El cliente solo envía la interacción (`OnInteracted` de `ATN_DirectInteractableBase`, distancia validada en el servidor) o pisa una placa (overlap solo en servidor).
- El estado de cada pieza es una propiedad replicada con `OnRep` que reconstruye el visual (patrón de `bRampDown` y `bRaised` en `TN_ProcPuzzleActors.h`): así la entrada tardía ve el puzzle en su estado real y no hay RPC de estado.
- Los movimientos (rampa, puerta, plataforma) interpolan localmente entre dos estados replicados; nada del movimiento es autoritativo por frame.
- Entrada de datos por plantilla: `puzzle_id` estable en el boceto, para que el registro de progreso sobreviva a reconexiones.

**Sabotaje en 2 vs 2 (regla común).** Cada pareja tiene su carril de puzzles y el módulo `sabotage` (plantilla 15) puede colgarse de cualquier puzzle de pareja. Reglas de ronda, coste y límites en `Docs/Modos-UI-FX-2026-09-29.md` §1.5. En cada plantilla, la fila «Sabotaje» dice qué pieza concreta puede tocar el rival y qué efecto tiene.

## 2. Piezas: existentes y pendientes

### 2.1 Existentes (se reutilizan sin cambios de código salvo lo marcado en 2.3)

| Pieza | Clase | Uso en el catálogo |
|---|---|---|
| Placa de presión | `ATN_PressurePlate` (`Momentary`/`Latched`, `ManagerTag`, `bOccupied` replicado) | `plate_balance`, `think_room`, `counterweight_lift` |
| Gestor de placas | `ATN_PressurePlateGroupManager` (`ManagedPlateTags`, `HoldDurationRequired` 2 s, `bOneShot`, `TriggerThreshold`, `TriggerActions`) | igual |
| Botón interactuable | `ATN_ButtonInteractable` (`PressesRequired`, `MoveTargetTag`, estados cíclicos) | `think_room`, `geyser_ferry`, `basket_hold` |
| Gestor de botones | `ATN_ButtonGroupManager` (`bOneShot`, `TriggerThreshold`, `TriggerActions`) | `think_room` |
| Acción de transformación | `FTN_TransformAction` (`TargetActorTag`, `TransitionDuration`) | puertas y puentes simples |
| Interruptor procedural | `ATN_ProcSwitch` (`SetTarget`, `EffectSeconds` 8 s) | `throw_chain`, `throw_wall_pair`, `sabotage` |
| Muro de lanzamiento | `ATN_ProcThrowWall` (4,8 m, rampa 7,5 m, `LowerRamp(Duration)`) | `throw_chain`, `throw_wall_pair` |
| Compuerta de sabotaje | `ATN_ProcSabotageGate` (`Raise(Duration)`) | `sabotage` |
| Géiser | `ATN_ProcGeyser` (`JetLow` 2,6 m, `JetHigh` 10,5 m, `CycleSeconds` 4,2 s, `Target`) | `geyser_aim`, `geyser_ferry` |
| Puerta de conchas | `ATN_BeachShellGate` (`PushSeconds` 0,6, `OpenHold` 3 s) | `shell_gauntlet` |
| Plataforma móvil | `ATN_BeachMovingPlatform` (ferri 3,3 m/s, ascensor 1,7 m/s, `bElevator`) | `platform_power`, `counterweight_lift` |
| Plataforma tambaleante / rompible | `ATN_BeachWobblyPlatform`, `ATN_BreakablePlatform` | parkour |
| Catapulta / trampolín | `ATN_BeachCatapult`, `ATN_BeachTrampoline` (y `ATN_JellyfishTrampoline`) | `catapult_gap` |
| Deslizadero | `ATN_ProcSlideZone` | opcional en `catapult_gap` |

### 2.2 Nuevas (faltan en C++)

| Id | Pieza | Qué es | Coste (h, est. propia) | Usada por |
|---|---|---|---|---|
| N1 | `ATN_TimedLever` | Palanca propia: hereda `ATN_DirectInteractableBase`; estados `Idle`/`Pulled`; `HoldSeconds` (0 = pulso, >0 = vuelve sola), `GroupTag`; propiedad `PulledAtServerTime` replicada; sonido de `TN_RaceItemSynth`. Hoy el «interruptor» es `ATN_ProcSwitch`, atado a ThrowWall y SabotageGate | 5 a 7 | `basket_hold`, `geyser_aim`, `lever_relay`, `sabotage`, `platform_power` |
| N2 | `ATN_ShellBasket` | Cesta elevada (sobre poste de 3 m): receptor que solo detecta cuerpos `ATN_ShellBody`/tortuga en caparazón; `bOccupied` replicado; `bLatch`. Una placa de presión con filtro de caparazón y malla cóncava | 6 a 8 | `basket_hold`, `throw_chain` (variante) |
| N3 | `ATN_PuzzleLogic` | Gestor lógico de pareja/grupo con modos `Window` (dos entradas dentro de una ventana), `Sequence` (orden fijo) y `Code` (combinación de placa+botón); cuenta intentos y emite éxito/fallo | 10 a 14 | `lever_relay`, `think_room` |
| N4 | `ATN_PuzzleDoor` | Puerta/compuerta genérica con estado replicado `Closed/Open`, apertura temporizada o pegada, `TransitionDuration`. `FTN_TransformAction` no vuelve sola a cerrar y `ATN_ProcSabotageGate` solo sube desde el suelo | 3 a 4 | `basket_hold`, `shell_gauntlet`, `think_room`, `sabotage` |
| N5 | `UTN_PuzzleStateComponent` | Componente común: `PuzzleId`, `OwnerTeam` (−1 = todos), `Progress` (0 a 100 %) replicado, `Reset()` autoritativo, delegado `OnSolved`/`OnFailed`. Da el dato al HUD y a la puntuación (Doc 2) | 6 a 8 | todas |
| N6 | `ITN_Activatable` | Interfaz mínima `SetPowered(bool, Instigator)` que implementan N1/N2/N4, la plataforma y el géiser; permite que una placa o palanca active cualquier pieza sin acoplar clases | 3 a 4 | `platform_power`, `geyser_aim`, `counterweight_lift` |

**Total de piezas nuevas: 6, ≈ 33 a 45 h.** Expansor de plantillas: es Python en el importador de Foto→Mapa (spec §7), 0 C++.

### 2.3 Modificaciones a piezas existentes

| Id | Clase | Cambio | Coste (h) |
|---|---|---|---|
| M1 | `ATN_PressurePlate` + gestor | `OccupantWeight` por ocupante: caparazón/bola cuenta 2 (spec §6 `plate_balance`); `EvaluateCondition` suma pesos frente a `TriggerThreshold` | 3 a 4 |
| M2 | `ATN_BeachMovingPlatform` | `bRequiresPower` + implementar `ITN_Activatable`; se detiene en el punto más cercano al perder energía (solo modo ascensor y ferri) | 3 a 4 |
| M3 | `ATN_ProcGeyser` | `SetAimIndex(int)` entre N posiciones de `Target` (hoy es un solo `FVector`); implementa `ITN_Activatable` | 2 a 3 |
| M4 | `ATN_ProcSwitch` | `SetTarget` acepta cualquier `ITN_Activatable`, no solo ThrowWall/SabotageGate; añade `OwnerTeam` y enfriamiento | 2 a 3 |

**Total: ≈ 10 a 14 h.**

## 3. Coop (puzzles de grupo, 3 a 8 jugadoras)

Todas escalan con el número de vivas: `TriggerThreshold = -1` del gestor ya significa «todas las vivas» (`GetEffectiveThreshold(AlivePlayers)`).

### 1. `plate_balance` · Tres placas, una bola

| Campo | Valor |
|---|---|
| Objetivo | Abrir la puerta de la sala manteniendo N placas pisadas a la vez durante `hold_s` |
| Jugadores | 3 a 8; con 3 basta cubrir 3 placas, con 8 se piden 5 |
| Piezas | 3 a 5 `ATN_PressurePlate` (`Momentary`) + `ATN_PressurePlateGroupManager` + puerta `ATN_PuzzleDoor` (N4). Existentes salvo N4 y M1 |
| Secuencia | 1) Las placas están separadas ≥ 6 m, en una plataforma con desnivel. 2) Cada jugadora pisa una. 3) Si faltan cuerpos, una jugadora se mete en caparazón y cuenta doble (M1). 4) Mantienen `hold_s`; suena la nota de condición cumplida. 5) La puerta se abre 20 s o queda abierta (`latch`) |
| Tiempo objetivo | 30 a 45 s |
| Fallo y reintento | Soltar una placa antes de `hold_s` reinicia el contador del gestor (ya lo hace `HoldDurationRequired`). Sin penalización |
| Red | Servidor: overlaps, pesos y contador. Replica: `bOccupied` por placa, `bConditionMet` del gestor, estado de la puerta. Cliente: luz de la placa por `OnRep` |
| Dificultad | 1 |
| `kind` y parámetros | `plate_balance`: `plates` (3 a 5), `spacing_m` (6), `hold_s` (2), `ball_counts_double` (true), `latch` (false), `door_width_m` (8) |
| Sabotaje 2 vs 2 | No usada en 2 vs 2 (necesita ≥ 3) |

### 2. `basket_hold` · La cesta y la palanca

| Campo | Valor |
|---|---|
| Objetivo | Cruzar una puerta que la palanca solo abre 8 s y dejarla pegada abierta metiendo una bola en la cesta del otro lado |
| Jugadores | 3 a 8 |
| Piezas | `ATN_TimedLever` (N1, `HoldSeconds` 8), `ATN_PuzzleDoor` (N4), `ATN_ShellBasket` (N2) sobre poste de 3 m tras la puerta. Faltan N1, N2, N4 (`PENDIENTE`) |
| Secuencia | 1) Una jugadora tira la palanca (a 20 m de la puerta). 2) El grupo corre a la puerta (≈ 3,5 s de carrera). 3) Una jugadora se mete en caparazón, otra la agarra y la lanza a la cesta al otro lado. 4) La bola en la cesta pega la puerta abierta (`bLatch`) y sale rodando. 5) El resto cruza sin prisa |
| Tiempo objetivo | 40 a 60 s |
| Fallo y reintento | Si la puerta se cierra sin bola en la cesta, la palanca se rearma a los 2 s. Una bola que no entra rueda hasta el pie del poste (sin daño) y se reintenta |
| Red | Servidor: temporizador de la palanca, sensor de la cesta, puerta. Replica: `PulledAtServerTime` (para la cuenta atrás visual sincronizada), `bOccupied` de la cesta, estado de la puerta. El lanzamiento usa la trayectoria de siempre (`Multicast_InitializeThrow`) |
| Dificultad | 2 |
| `kind` y parámetros | `basket_hold`: `lever_hold_s` (8), `lever_to_door_m` (20), `basket_height_m` (3), `latch` (true) |
| Sabotaje 2 vs 2 | La versión de pareja es `throw_wall_pair`; `basket_hold` no se usa en 2 vs 2 |

### 3. `throw_chain` · Cadena de lanzamientos

| Campo | Valor |
|---|---|
| Objetivo | Superar un muro de 4,8 m: se lanza a una jugadora arriba, pulsa el interruptor y baja la rampa para el resto |
| Jugadores | 3 a 8; con más de 4 se encadenan 2 muros con 12 m de separación |
| Piezas | `ATN_ProcThrowWall` (existente) + `ATN_ProcSwitch` en `GetSwitchLocation()` (existente). Solo con existentes |
| Secuencia | 1) Una jugadora se mete en caparazón. 2) Otra la agarra y la lanza; el ángulo de lanzamiento desde el pie del muro basta para superar 4,8 m. 3) La lanzada llega arriba, sale de la bola y pulsa el interruptor. 4) La rampa baja `EffectSeconds` (8 s por defecto). 5) Todas suben |
| Tiempo objetivo | 30 a 45 s por muro |
| Fallo y reintento | Un lanzamiento corto cae al pie del muro (sin noqueo si la velocidad es < 600 cm/s). Se reintenta al momento. La rampa se vuelve a alzar sola: quien no haya subido tras 8 s vuelve a pulsar (el que está arriba) |
| Red | Ya resuelto en `ATN_ProcThrowWall`: `bRampDown` y `Dimensions` replicados con `OnRep`; el servidor ejecuta `LowerRamp` |
| Dificultad | 2 |
| `kind` y parámetros | `throw_chain`: `walls` (1 o 2), `wall_height_m` (4,8), `ramp_run_m` (7,5), `effect_s` (8) |
| Sabotaje 2 vs 2 | No usada en 2 vs 2 (la de pareja es `throw_wall_pair`) |

### 4. `geyser_aim` · El géiser orientable

| Campo | Valor |
|---|---|
| Objetivo | Poner el chorro del géiser sobre la plataforma alta usando la palanca y subir en el ciclo |
| Jugadores | 2 a 8 (una sola opera la palanca) |
| Piezas | `ATN_ProcGeyser` (M3: `SetAimIndex`, 3 posiciones de `Target`), `ATN_TimedLever` (N1) con 3 posiciones, `ITN_Activatable` (N6). Faltan N1, N6, M3 (`PENDIENTE`) |
| Secuencia | 1) El géiser dispara a un hueco vacío (posición 0). 2) Una jugadora en la palanca la gira a la posición 2 (la plataforma alta). 3) El resto espera al pulso `JetHigh` del ciclo de 4,2 s y salta al chorro. 4) Las subidas caen en la plataforma; la última jugadora baja la palanca y sube con el ciclo |
| Tiempo objetivo | 45 a 75 s |
| Fallo y reintento | Caer al chorro en la posición equivocada lanza a una zona sin daño (arena). La palanca no se bloquea nunca |
| Red | Servidor: índice de puntería y ciclo del géiser. Replica: `AimIndex` (int8) y fase del ciclo por tiempo de servidor; el cliente anima el chorro local |
| Dificultad | 2 |
| `kind` y parámetros | `geyser_aim`: `aim_positions` (3), `target_height_m` (8), `cycle_s` (4,2), `lever_distance_m` (10) |
| Sabotaje 2 vs 2 | No usada (la de pareja es `geyser_ferry`) |

### 5. `shell_gauntlet` · Las tres puertas de conchas

| Campo | Valor |
|---|---|
| Objetivo | Pasar tres puertas de conchas seguidas: cada una se abre 3 s cuando una bola la empuja |
| Jugadores | 3 a 8 |
| Piezas | 3 `ATN_BeachShellGate` (`PushSeconds` 0,6, `OpenHold` 3 s, ya existentes) separadas 10 m, más una puerta final `ATN_PuzzleDoor` opcional. Solo con existentes |
| Secuencia | 1) Una jugadora se hace bola y empuja la primera puerta ≥ 0,6 s. 2) Se abre 3 s: el resto cruza corriendo. 3) La bola sigue a la segunda y repite mientras el grupo avanza por el pasillo. 4) La última puerta se atraviesa con la bola dentro |
| Tiempo objetivo | 35 a 50 s |
| Fallo y reintento | La puerta se cierra sola a los 3 s; quien queda entre puertas espera a que la bola vuelva. La bola no muere ni se atasca |
| Red | Servidor: empuje y temporizador; réplica ya resuelta en el elemento (`bOpen`, `ChangedAt`) |
| Dificultad | 2 |
| `kind` y parámetros | `shell_gauntlet`: `gates` (3), `spacing_m` (10), `open_hold_s` (3), `final_door` (false) |
| Sabotaje 2 vs 2 | No usada (con dos jugadoras es trivial) |

### 6. `platform_power` · Energía para la plataforma

| Campo | Valor |
|---|---|
| Objetivo | Cruzar un abismo de 30 m en una plataforma que solo se mueve mientras alguien mantiene una placa |
| Jugadores | 3 a 8 (1 a 2 mantienen la placa; el resto viaja) |
| Piezas | `ATN_BeachMovingPlatform` (ferri, 3,3 m/s, M2), `ATN_PressurePlate` (`Momentary`) con `ITN_Activatable` (N6). Faltan N6 y M2 (`PENDIENTE`) |
| Secuencia | 1) Las viajeras suben a la plataforma. 2) La que se queda pisa la placa junto al embarcadero; la plataforma se mueve. 3) Al llegar, el ferri para (`FerryDwell` 1,6 s) y las viajeras bajan. 4) En la orilla de llegada hay una segunda placa `Latched`: una viajera la pisa y deja la plataforma con energía permanente, que vuelve sola al embarcadero y recoge a la que mantenía la primera placa |
| Tiempo objetivo | 50 a 70 s |
| Fallo y reintento | Perder la energía detiene la plataforma en el punto actual (M2). Un salto al vacío cae en el rescate (plan §2.3): 3 s de espera y vuelta al embarcadero |
| Red | Servidor: posición de la plataforma (curva por tiempo de servidor, como el elemento actual) y energía. Replica: `bPowered` y `PhaseTime` |
| Dificultad | 2 |
| `kind` y parámetros | `platform_power`: `gap_m` (30), `mode` (`ferry` o `elevator`), `plates` (2; la de llegada es `Latched`), `speed_mps` (3,3) |
| Sabotaje 2 vs 2 | No usada (la de pareja es `counterweight_lift`) |

## 4. Parkour (Coop y 2 vs 2, sin lógica de puzzle)

Tramos de habilidad. No llevan `UTN_PuzzleStateComponent` de bloqueo: el progreso es solo espacial. En 2 vs 2 se colocan **dos veces** (una por carril) o en zona compartida.

### 7. `wobbly_run` · Pasarela tambaleante

| Campo | Valor |
|---|---|
| Objetivo | Cruzar una hilera de plataformas que se inclinan bajo el peso |
| Jugadores | 1 a 8 (cada una por su cuenta; el peso de varias las inclina antes) |
| Piezas | 5 a 8 `ATN_BeachWobblyPlatform` en fila con huecos de 2,5 m (salto simple) y uno de 4,5 m (doble salto). Solo existentes |
| Secuencia | Saltar de plataforma en plataforma; en el hueco largo, doble salto; el hueco final acaba en suelo firme con punto de reaparición |
| Tiempo objetivo | 15 a 25 s |
| Fallo y reintento | Caer al agua o al vacío: rescate del plan §2.3 al inicio del tramo (3 s). Sin límite de intentos |
| Red | Ya resuelto en el elemento (inclinación por peso, replicada). Servidor decide la caída y el rescate |
| Dificultad | 2 |
| `kind` y parámetros | `wobbly_run`: `platforms` (5 a 8), `gap_m` (2,5), `long_gap_m` (4,5), `length_m` (auto) |
| Sabotaje 2 vs 2 | La compuerta de `sabotage` se coloca a la salida del tramo (bloquea al que sale, no al que cruza). Ampliación posterior: multiplicar la inclinación ×2 durante 6 s (necesita parámetro nuevo en la plataforma; fuera de MVP) |

### 8. `breakable_chain` · Camino que se rompe

| Campo | Valor |
|---|---|
| Objetivo | Cruzar plataformas que se rompen poco después de pisarlas, sin parar |
| Jugadores | 1 a 8 |
| Piezas | 6 a 10 `ATN_BreakablePlatform` en zigzag, una `ATN_BeachTrampoline` intermedia. Solo existentes |
| Secuencia | Avanzar sin detenerse; el trampolín salta un tramo roto de 6 m; la última plataforma es firme |
| Tiempo objetivo | 15 a 20 s |
| Fallo y reintento | Las plataformas rotas reaparecen a los 6 s (parámetro del elemento; verificar) para que quien caiga pueda reintentar y las rezagadas no queden encerradas. Rescate como en `wobbly_run` |
| Red | Servidor decide la rotura por tiempo de contacto; el estado (roto/entero) va replicado por el elemento |
| Dificultad | 2 |
| `kind` y parámetros | `breakable_chain`: `platforms` (6 a 10), `respawn_s` (6), `trampoline` (true) |
| Sabotaje 2 vs 2 | Igual que `wobbly_run`. Un sabotaje que rompa plataformas del rival queda fuera: castiga sin contrajuego |

### 9. `catapult_gap` · El cañón de tortugas

| Campo | Valor |
|---|---|
| Objetivo | Cruzar un abismo de 40 m lanzándose con la catapulta a un trampolín de aterrizaje |
| Jugadores | 1 a 8 |
| Piezas | `ATN_BeachCatapult` (con la validación de arco de `TryAddWithArc`, plan §3.2 P3) y `ATN_BeachTrampoline` o `ATN_JellyfishTrampoline` como aterrizaje. Solo existentes |
| Secuencia | Subirse a la catapulta, salir disparada, caer en el trampolín (amortigua) y seguir. Alternativa a pie: el camino largo de 90 s alrededor, para no bloquear a nadie si falla |
| Tiempo objetivo | 10 a 15 s por la vía rápida; 60 a 90 s por la larga |
| Fallo y reintento | Un disparo corto cae al rescate (3 s). La catapulta se recarga en su tiempo normal |
| Red | Ya resuelto en los elementos (disparo decidido en servidor, arco validado). Sin mecánica nueva |
| Dificultad | 1 |
| `kind` y parámetros | `catapult_gap`: `gap_m` (40), `long_way` (true), `landing` (`trampoline` o `jellyfish`) |
| Sabotaje 2 vs 2 | Sin efecto posible (no hay compuerta natural). Es el tramo «limpio» donde la carrera se decide por habilidad |

## 5. Zona de pensar (Coop y 2 vs 2)

### 10. `think_room` · La sala de las pistas

| Campo | Valor |
|---|---|
| Objetivo | Resolver una combinación leyendo las pistas de la pared y ejecutándola entre todas |
| Jugadores | Variante `code`: 2 a 4. Variante `sequence`: 2 a 8 |
| Piezas | Variante `code`: 4 `ATN_PressurePlate` (colores) + 2 `ATN_ButtonInteractable` + `ATN_PuzzleLogic` en modo `Code` (N3) + `ATN_PuzzleDoor` (N4) + pistas en pared como mallas procedurales de glifos (círculo, triángulo, cuadrado, estrella) con color de vértice. Variante `sequence`: 4 pilares `ATN_ButtonInteractable` + N3 en modo `Sequence`. Faltan N3, N4, N5 (`PENDIENTE`) |
| Secuencia `code` | 1) La pared muestra 3 pasos: glifo de placa + color de botón. 2) En cada paso una jugadora mantiene la placa indicada y otra pulsa el botón. 3) Tres pasos correctos abren la puerta. Con 2 jugadoras, una sostiene y la otra pulsa; con 4, dos pasos se pueden hacer a la vez |
| Secuencia `sequence` | 1) Los pilares se iluminan en un orden de 4 (Simon). 2) Se repite pulsando en el mismo orden. 3) Cada acierto suma un pilar más hasta 4 |
| Tiempo objetivo | 45 a 90 s (`code`, 3 pasos); 40 a 60 s (`sequence`) |
| Fallo y reintento | Paso incorrecto: alarma de 1 s, el código vuelve al paso 1, 3 s de espera (1 s en Coop). Las pistas no cambian entre intentos para que nunca sea suerte |
| Red | El servidor genera el código con la semilla del puzzle (`Seed` de la partida + `PuzzleId`) y **no lo replica**: replica solo las pistas (glifos visibles) y `Progress` (0 a 3). Así un cliente modificado no puede leer la solución. En 2 vs 2 cada carril usa una semilla distinta, para que mirar al rival no sirva |
| Dificultad | 3 |
| `kind` y parámetros | `think_room`: `variant` (`code` o `sequence`), `steps` (3), `plates` (4), `buttons` (2), `wrong_penalty_s` (3), `seed_salt` (entero) |
| Sabotaje 2 vs 2 | La compuerta de `sabotage` se pone a la salida de la sala. Además, la sala tiene 4 s de «luz baja» posible como sabotaje futuro (fuera de MVP) |

## 6. 2 vs 2 (puzzles de pareja)

Cada plantilla de esta sección se coloca **dos veces**, una por equipo, con `OwnerTeam` 0 y 1 (N5). Las dos instancias son idénticas en piezas y distancias (equidad) y solo difieren en el color del equipo y en la semilla de cualquier dato secreto. Reglas del modo y puntuación: `Docs/Modos-UI-FX-2026-09-29.md` §1.

### 11. `lever_relay` · El relevo de palancas

| Campo | Valor |
|---|---|
| Objetivo | Abrir la puerta tirando de dos palancas separadas 30 m dentro de una ventana de 1,5 s |
| Jugadores | 2 (una por palanca) |
| Piezas | 2 `ATN_TimedLever` (N1), `ATN_PuzzleLogic` en modo `Window` (N3), `ATN_PuzzleDoor` (N4). Faltan N1, N3, N4 (`PENDIENTE`) |
| Secuencia | 1) Cada una se coloca en su palanca (visibles entre sí, sin línea de voz obligatoria: la voz de proximidad `VoiceIndicatorWidget` ya existe). 2) Se ponen de acuerdo con el chat rápido o la voz. 3) Tiran con menos de 1,5 s de diferencia. 4) La puerta se abre 12 s (o pegada, `latch`) |
| Tiempo objetivo | 20 a 35 s |
| Fallo y reintento | Fuera de ventana: la primera palanca vuelve arriba a los 1,5 s, luz roja en ambas, sin penalización de tiempo. Reintento inmediato |
| Red | Servidor mide las dos `PulledAtServerTime` (no el reloj del cliente) y decide. Como la latencia desplaza cada palanca, la ventana admite `window_s` + 0,15 s de tolerancia por RTT medido (tope 0,3 s). Replica: estado de las dos palancas y de la puerta |
| Dificultad | 2 |
| `kind` y parámetros | `lever_relay`: `lever_distance_m` (30), `window_s` (1,5), `door_open_s` (12), `latch` (false) |
| Sabotaje 2 vs 2 | La compuerta rival va tras la puerta. Contrajuego: si la palanca está en la zona de espera del propio equipo, la pareja atacada no pierde más de 5 s |

### 12. `counterweight_lift` · El contrapeso

| Campo | Valor |
|---|---|
| Objetivo | Subir a un saliente de 6 m: una hace de contrapeso en la placa, otra sube en el ascensor y baja la rampa para la primera |
| Jugadores | 2 |
| Piezas | `ATN_PressurePlate` (`Momentary`), `ATN_BeachMovingPlatform` (`bElevator`, `LiftSpeed` 1,7 m/s; M2, N6), `ATN_ButtonInteractable` con rampa vía `FTN_TransformAction` en lo alto. Faltan N6 y M2 (`PENDIENTE`) |
| Secuencia | 1) Una pisa la placa; el ascensor sube ≈ 3,5 s. 2) La otra, que iba montada, sale arriba y pulsa el botón. 3) La rampa baja (`TransitionDuration` 1,5 s, `ramp_s` 8). 4) La de la placa sube por la rampa; la carrera sigue |
| Tiempo objetivo | 25 a 40 s |
| Fallo y reintento | Si la de la placa la abandona antes de que la otra salga, el ascensor baja despacio con ella dentro (sin daño) y se repite. Si el botón se pulsa demasiado pronto, la rampa no toca el suelo: se reintenta |
| Red | Servidor: peso de la placa, posición del ascensor por tiempo de servidor. Replica: `bPowered`, `PhaseTime`, estado de la rampa |
| Dificultad | 2 |
| `kind` y parámetros | `counterweight_lift`: `height_m` (6), `lift_speed_mps` (1,7), `ramp_s` (8), `ramp_run_m` (10) |
| Sabotaje 2 vs 2 | La compuerta rival sale al pie de la rampa de arriba. Es el mejor sitio: la pareja queda con una jugadora arriba y otra abajo mirando 5 s |

### 13. `throw_wall_pair` · El muro de lanzamiento

| Campo | Valor |
|---|---|
| Objetivo | Superar un muro de 4,8 m: la compañera lanza, la lanzada pulsa el interruptor y la rampa baja |
| Jugadores | 2 |
| Piezas | `ATN_ProcThrowWall`, `ATN_ProcSwitch` (existentes; construidas para este modo, ver el comentario de `TN_ProcPuzzleActors.h`). Solo existentes |
| Secuencia | 1) La compañera A se mete en caparazón. 2) B la agarra y la lanza (agarrar solo a quien está en caparazón). 3) A cae arriba, sale de la bola y pulsa el interruptor. 4) La rampa baja `effect_s` (8 s, valor del original; con el sabotaje de 5 s activo deja margen para reintentar). 5) B sube corriendo y se sigue |
| Tiempo objetivo | 20 a 30 s |
| Fallo y reintento | Lanzamiento corto: A cae al pie sin noqueo (< 600 cm/s) y B la vuelve a agarrar. Si A no pulsa a tiempo, la rampa sube sola y se repite. Escape de agarre machacando salto ya existente |
| Red | Resuelto en la clase (`bRampDown`, `Dimensions`); servidor ejecuta `LowerRamp`. Solo se añade `OwnerTeam` (N5) para el HUD |
| Dificultad | 2 |
| `kind` y parámetros | `throw_wall_pair`: `wall_height_m` (4,8), `ramp_run_m` (7,5), `effect_s` (8) |
| Sabotaje 2 vs 2 | El caso de diseño original: el interruptor rival levanta la `ATN_ProcSabotageGate` del otro carril. La compuerta se sitúa **a la salida del muro, tras la rampa**: no impide el lanzamiento y sí retiene 5 s a la pareja que ya ha subido. Un uso por pareja y puzzle (plantilla 15) |

### 14. `geyser_ferry` · El géiser a dedo

| Campo | Valor |
|---|---|
| Objetivo | Una orienta el géiser con la palanca; la otra salta al chorro, llega a un saliente y tiende un puente para la primera |
| Jugadores | 2 |
| Piezas | `ATN_ProcGeyser` (M3, 2 posiciones), `ATN_TimedLever` (N1), `ITN_Activatable` (N6), `ATN_ButtonInteractable` + `FTN_TransformAction` (puente). Faltan N1, N6, M3 (`PENDIENTE`) |
| Secuencia | 1) A gira la palanca a la posición «saliente». 2) B espera al pulso `JetHigh` (ciclo de 4,2 s) y salta al chorro. 3) B aterriza en el saliente y pulsa el botón. 4) El puente se extiende 1,5 s y queda 10 s. 5) A cruza |
| Tiempo objetivo | 25 a 40 s |
| Fallo y reintento | Un salto fuera de tiempo aterriza en arena, sin daño; se repite en el siguiente pulso. La palanca queda en la posición elegida hasta que alguien la cambie |
| Red | Servidor: índice de puntería y fase del ciclo. Replica: `AimIndex`, fase por tiempo de servidor, estado del puente |
| Dificultad | 3 |
| `kind` y parámetros | `geyser_ferry`: `ledge_height_m` (8), `cycle_s` (4,2), `bridge_open_s` (10) |
| Sabotaje 2 vs 2 | La compuerta rival se sitúa en el puente. Alternativa posterior (fuera de MVP): «desviar el géiser» del rival a la posición vacía durante 4 s |

### 15. `sabotage` · Módulo de sabotaje

No es un puzzle: es una capa que se cuelga de cualquier plantilla 7 a 14 mediante `hooks` en el `kind`.

| Campo | Valor |
|---|---|
| Objetivo | Frenar 5 s a la pareja rival sin tocarla, al coste de estar cerca de una zona de espera propia |
| Jugadores | 1 de la pareja atacante; afecta a las 2 rivales |
| Piezas | `ATN_ProcSwitch` de sabotaje (M4: `OwnerTeam`, enfriamiento) + `ATN_ProcSabotageGate` del carril rival (existentes, con M4). Solo existentes más M4 |
| Secuencia | 1) El interruptor de sabotaje está en un rincón junto a una espera del puzzle propio (ascensor subiendo, rampa esperando, ciclo del géiser): coste esperado ≤ 2 s. 2) La jugadora lo pulsa. 3) Suena la sirena a **todos** durante 1 s (aviso justo) y la compuerta del rival sube 5 s. 4) La compuerta cae sola |
| Tiempo objetivo | Efecto de 5 s; coste al atacante de 0 a 2 s |
| Fallo y reintento | Si hay un pawn sobre la compuerta cuando va a subir, el servidor la retrasa hasta 1 s y, pasado ese tiempo, sube empujando fuera (sin daño). Enfriamiento del interruptor: 20 s. **Máximo un sabotaje por pareja y puzzle** |
| Red | El servidor valida `OwnerTeam` (no se puede pulsar el del propio equipo) y el enfriamiento. Replica: la compuerta ya replica `bRaised`; se añade `RaiseEndTime` (tiempo de servidor) para el temporizador del HUD del rival. Sirena: multicast no fiable |
| Dificultad | 1 |
| `kind` y parámetros | En el puzzle: `"sabotage": {"effect": "gate", "duration_s": 5, "cooldown_s": 20, "warning_s": 1, "at": "wait_zone"}`. Solo `effect: gate` en MVP |
| Sabotaje 2 vs 2 | Es el sabotaje. Balance completo y catálogo de efectos posteriores en Doc 2 §1.5 |

## 7. Resumen para MapSketch

| `kind` | Modo | Bloquea el camino | Piezas que faltan (§2) | Coste extra (h) | Prioridad |
|---|---|---|---|---|---|
| `throw_chain` | Coop | Sí | ninguna | 0 | MVP |
| `shell_gauntlet` | Coop | Sí | N4 opcional | 0 a 4 | MVP |
| `plate_balance` | Coop | Sí | N4, M1 | 6 a 8 | MVP |
| `wobbly_run`, `breakable_chain`, `catapult_gap` | Coop, 2 vs 2 | No | ninguna | 0 | MVP |
| `throw_wall_pair` | 2 vs 2 | Sí | N5, M4 | 8 a 11 | MVP |
| `sabotage` | 2 vs 2 | Módulo | M4, N5 | (incluido) | MVP |
| `basket_hold` | Coop | Sí | N1, N2, N4 | 14 a 19 | Later |
| `geyser_aim` | Coop | Sí | N1, N6, M3 | 10 a 14 | Later |
| `platform_power` | Coop | Sí | N6, M2 | 6 a 8 | Later |
| `lever_relay` | 2 vs 2 | Sí | N1, N3, N4 | 18 a 25 | Later |
| `counterweight_lift` | 2 vs 2 | Sí | N6, M2 | 6 a 8 | Later |
| `geyser_ferry` | 2 vs 2 | Sí | N1, N6, M3 | 10 a 14 | Later |
| `think_room` | Coop, 2 vs 2 | Sí | N3, N4, N5 | 19 a 26 | Later |

**MVP de Coop y 2 vs 2**: `throw_chain`, `shell_gauntlet`, `plate_balance`, `throw_wall_pair`, `sabotage` y 3 tramos de parkour. Es lo que un mapa puede usar sin más C++ que N4, N5, M1 y M4 (≈ 22 a 30 h). El resto entra al llegar N1, N3 y N6.

Ejemplo de entrada en `MapSketch.json` (spec §4):

```json
"puzzles": [
  {"kind": "plate_balance", "id": "cp01_p1", "at": [350, 240], "yaw_deg": 90, "params": {"plates": 4, "hold_s": 2}},
  {"kind": "throw_wall_pair", "id": "dv01_p2", "at": [120, 80], "yaw_deg": 0, "team_instances": 2,
   "params": {"effect_s": 6}, "sabotage": {"effect": "gate", "duration_s": 5, "at": "wait_zone"}}
]
```

Validación de un puzzle en el importador: `kind` conocido y no `PENDIENTE`; `players` compatible con el modo del mapa; en 2 vs 2, `team_instances` = 2 y la simetría de las dos instancias dentro de 0,5 m; espacio libre de 20 m detrás del `at` en +X; el puzzle no corta el único camino sin alternativa de rescate.

## 8. Pruebas de aceptación

- **Automatización (`Tortunabo.Puzzle.*`)**: N3 modo `Window` acepta 1,4 s y rechaza 1,8 s (caso negativo); `Code` con paso erróneo vuelve a 0; `M1` suma 2 por bola; `Reset()` deja `Progress` a 0 y la pieza en su estado inicial.
- **PIE 4P con PktLag 150 y 2 % de pérdida**: `lever_relay` acepta la ventana con RTT simétrico; `throw_wall_pair`: A y B ven la bola caer en el mismo sitio (±5 cm, criterio del plan §3.4); entrada tardía en mitad de un puzzle ve la pieza en su estado actual.
- **Seguridad de red**: `think_room`: el código no aparece en ningún paquete replicado (revisión de `GetLifetimeReplicatedProps`).
- **Playtest**: cada plantilla se resuelve por una pareja o grupo nuevos en ≤ 3× el tiempo objetivo sin ayuda externa; si no, sube el cartel o baja la dificultad.

## 9. Preguntas y decisiones para el director

1. **Contradicción con código existente.** `ETNProcGameMode::TwoVsTwo` (procedural, exactamente 4 jugadores, carriles `Lane`, parejas rotando por ronda, `ATN_ProcThrowWall` + `ATN_ProcSabotageGate` en `TN_ProcPuzzleActors.h`) ya existe. El plan §7.9 lo define como modo volumétrico de camino fijo. Propuesta: reutilizar las piezas de puzzle y el reparto de parejas y retirar el generador procedural de carriles. Falta confirmar.
2. **¿Sabotaje sin contacto solo?** Se ha descartado empujar a la rival por la asimetría (una pareja ya perdiendo lo sufre más). Confirmar.
3. **`basket_hold`**: la cesta obliga a lanzar a una bola con puntería; ¿se acepta o se sustituye por un botón de zona? Es la que más depende de N2.
4. **Tolerancia de ventana `lever_relay`**: 1,5 s con +0,15 s por RTT se ha fijado sin playtest; puede ser 2 s en la primera pasada.
5. **Valores «supuesto»** a medir en el motor antes de fijar las cifras: marcha de 6 m/s, tiempo de rotura de `ATN_BreakablePlatform` y su reaparición, y disparo de `ATN_BeachCatapult` (¿se sube sola?).

