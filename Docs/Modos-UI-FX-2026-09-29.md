# Modos, interfaz y efectos: 2 vs 2 y pantallas de los cinco modos

Fecha: 2026-09-29 · Rama: `macro-update` · Estado: diseño (sin código).
Relacionados: `Docs/2026-09-29-Plan-Maestro-Modos-y-Mapas.md` (§3, §3.6, §7.9), `Docs/Catalogo-Puzzles-2026-09-29.md` (plantillas y piezas `N1` a `N6`, `M1` a `M4`), `Docs/Rally_E01B_y_Biplaza.md`, `Docs/Catalogo-Mapas-2026-09-29.md`.

## 0. Resumen y contradicciones

- **Los cinco modos** (decisión 1 y 9 del plan): Coop, Carrera, Rally Tortuga, Todos contra Todos (TcT) y 2 vs 2. Este documento diseña el 2 vs 2 completo (§1) y, para los cinco, HUD, marcadores, pantalla de fin, selector de modo y de mapa en el lobby, y VFX/SFX (§2). §3 lista lo nuevo imprescindible con coste.
- **Principio.** Todo sale de lo que ya existe: widgets en código con el estilo Tortunavy (`UTN_RunHUDWidget`, `UTN_CoopFlowHUDWidget`, familia `UI/Race/*`), sintetizadores de audio (`UTN_RaceCueSynthComponent`, `UTN_ShellImpactSynthComponent`, `UTN_BeachSplashSynthComponent`, `UTN_BeachTrapSynthComponent`, `UTN_AmbientSynthComponent`, `UTN_RaceItemSynthComponent`) y VFX en código (`UTN_TurtleDustComponent`, `UTN_DizzyBirdsComponent`, `UTN_BeachFinishSplashSubsystem`). **0 assets de arte imprescindibles nuevos** en este documento; las vistas previas de mapa son deseables.
- **Regla del proyecto**: la interfaz no decide nada del flujo (patrón de `UTN_RaceChampionWidget`, que llama a `ATN_BeachRaceGameMode::RequestChampionChoice`). Toda pantalla lee estado replicado.
- **Contradicciones que necesitan al director**:
  1. **2 vs 2 procedural existente.** `ETNProcGameMode::TwoVsTwo` (`TN_ProcMapEnums.h:33`), `ATN_ProcMapGameMode::AssignTwoVsTwoTeams` (`TN_ProcMapGameMode.cpp:601`: exactamente 4 jugadores, parejas rotando AB|CD, AC|BD, AD|BC por ronda) y el fin por progreso (`:989`) implementan un 2 vs 2 sobre mapa procedural con carriles. El plan §7.9 lo redefine como modo volumétrico de camino fijo. Propuesta: mantener la mecánica de equipos y el fin por progreso como reglas de desempate, retirar la generación procedural de carriles y colocar las plantillas del catálogo en mapas fijos. La rotación de parejas pasa a variante opcional (§1.6).
  2. **Selector de modo.** `ATN_ProcModeSelector` (LVL_HQ) enumera hoy «Clásico → Coop → Carrera → 2vs2». El plan retira Clásico/Run y añade Rally y TcT. Se propone la lista de cinco (§2.1).
  3. **`TN_BriefingWidget` y `TN_PauseMenuWidget`** ya mencionan 2 vs 2 (búsqueda por `TwoVsTwo`): sus textos pueden estar desfasados y se revisan al implementar.

## 1. Diseño del 2 vs 2

### 1.1 Experiencia buscada y cómo se enseña

Dos parejas corren la misma ruta por carriles gemelos. Cada pareja resuelve puzzles pensados para dos tortugas que se ayudan (una lanza a la otra, una hace de contrapeso, las dos tiran de palancas a la vez), y la carrera se decide por quién coordina mejor y por un sabotaje bien puesto. La sensación: «nos entendemos mejor que ellos», no «tengo que golpearles».

**Cómo se enseña.**
- **Briefing de carga** (`UTN_BriefingWidget`, ya existe): 3 líneas, las mismas siempre: «Llegad las dos», «Un sabotaje frena 5 s a la otra pareja», «Se gana al mejor de 3».
- **Sala de práctica en el HQ**: un carril corto con `throw_wall_pair` y un interruptor de sabotaje sobre un muñeco (reutiliza el patrón de `TN_TutorialPractice`). Es opcional y no bloquea nada.
- **Primera pista contextual** en el primer puzzle de cada tipo (cartel en el mundo, §2.3).

### 1.2 Estructura de la partida

| Concepto | Regla | Valor inicial y motivo |
|---|---|---|
| Jugadores | Exactamente 4, dos parejas. Con menos, el anfitrión no puede iniciar (el selector muestra «Necesita 4»). Ya lo exige `TN_ProcMapGameMode.cpp:321` | 4. Sin bots en MVP: un puzzle de pareja con un bot mal hecho es peor que no jugarlo |
| Parejas | Fijas durante todo el encuentro y elegidas en el lobby (§1.6) | Fijas: la coordinación se aprende y mejora en 3 rondas |
| Encuentro | Al mejor de 3 rondas (gana quien llegue a 2) | 12 a 18 min típicos; 3 es el mínimo para que un mal comienzo no decida |
| Ronda | Una carrera completa por la ruta | Objetivo 5 a 6 min (cuentas: 5 puzzles de pareja × 30 s + 3 tramos de parkour × 20 s + 400 m a 6 m/s + 2 zonas comunes de 20 s ≈ 5,3 min) |
| Carriles | Los equipos alternan carril entre rondas (A/B, B/A) | Elimina cualquier ventaja de trazado |
| Zonas comunes | 2 por mapa, de 30 a 40 m: los dos carriles se juntan sobre un puente o plaza | Es el único sitio de contacto directo (§1.4) |
| Bifurcación | 1 por mapa: atajo (puzzle de dificultad 3, ahorra ≈ 25 s si sale a la primera) o rodeo (parkour largo, seguro, 60 a 90 s) | Menos bifurcaciones que en Coop pero más grandes (plan §7.9) |
| Tope de ronda | 8 min | Evita rondas eternas por un puzzle atascado |

### 1.3 Puntuación, fin de ronda y fin de encuentro

- **Meta de equipo.** Los dos miembros tienen que cruzar la meta. El **tiempo del equipo es el de la segunda en llegar**. La primera que llega espera junto a la meta y puede usar pings y chat rápido, pero no sabotear (regla de §1.5: el sabotaje se bloquea a los equipos que han terminado, y su interruptor está en el carril).
- **Gana la ronda** el primer equipo completo en cruzar. Ese instante lo fija el servidor con marca de tiempo (`GetServerWorldTimeSeconds` como `double`); empate exacto: gana quien tuvo antes a su primera miembro en meta; si aun así hay empate, gana el equipo con menos sabotajes recibidos.
- **Tope de 8 min.** Gana el equipo con mayor suma de progreso de sus miembros (regla que ya existe en `TN_ProcMapGameMode.cpp:989`, `GetPlayerProgress`). El progreso pasa a medirse sobre `path_dist_dm` del bake del mapa (plan §2.1), no sobre nidos.
- **Puntos de encuentro:** 1 por ronda ganada. A 2, fin del encuentro. Empate imposible (máximo 3 rondas).
- **Conchas de recompensa** (moneda existente, `RaceShellHalves`): ronda ganada 3, perdida 1. La pérdida da algo para que el modo no castigue a quien aprende. Valores de primera pasada.
- **Estadísticas del fin** (sin efecto en la puntuación): tiempo de cada puzzle, sabotajes puestos y recibidos, mejor lanzamiento.
- **Abandono.** Si una jugadora se desconecta en mitad de una ronda: 45 s de gracia con aviso «Esperando a X»; si vuelve (misma cuenta de Steam), retoma en el último punto seguro. Pasados 45 s, su pareja no puede resolver los puzzles: el equipo pierde la ronda y, si en las dos rondas siguientes sigue sin volver, el encuentro se da por perdido. Esto es una regla dura por diseño: sin bots no hay forma justa de seguir.
- **Fin de encuentro.** Pantalla de resultado del §2.2 y, con la decisión solo del anfitrión, «Revancha» (mismas parejas, otro mapa) o «Volver al lobby».

### 1.4 Contacto entre equipos

- **En carriles**: sin contacto. Cada carril está separado por un río de altura (agua sin daño, rescate) o un muro. No hay lanzamientos ni empujones entre carriles.
- **En zonas comunes**: la bola física y el noqueo por velocidad (`MinKnockdownSpeed` 600 cm/s) siguen activos, así que rodar contra un rival lo derriba; **agarrar y lanzar a un rival queda desactivado** (`bAllowGrabOpponents=false` en la zona). Motivo: lanzar rivales fuera del camino es poderoso y difícil de equilibrar en un modo cooperativo por parejas; decisión abierta para el director (§4).
- **Rescate**: una jugadora que cae del mapa vuelve al último punto seguro (plan §2.3) tras 3 s, sin perder puntos.

### 1.5 Sabotajes

Idea de diseño: el sabotaje es una decisión con coste. El interruptor está junto a una zona de espera de un puzzle propio (ascensor subiendo, rampa esperando, ciclo del géiser), de forma que pulsarlo cuesta ≤ 2 s. Así pulsarlo compensa siempre un poco, pero exponerse a que el rival lo haga también obliga a decidir cuándo.

**Reglas comunes.**

| Regla | Valor | Motivo |
|---|---|---|
| Efecto | 5 s | Un puzzle de pareja dura 20 a 40 s: 5 s se nota sin decidir la ronda |
| Aviso | Sirena a todas durante 1 s antes de aplicarse | Justicia y lectura; da margen a salir de la compuerta |
| Enfriamiento del interruptor | 20 s | Evita el acoso continuo |
| Máximo por pareja y ronda | 3 | Impacto máximo de 15 s (≈ 5 % de la ronda) |
| Máximo por pareja y puzzle | 1 | Evita repetir sobre un puzzle atascado |
| Inmunidad tras recibir | 10 s | Sin encadenados |
| Restricciones | Sin efecto si la víctima ya ha terminado; no se puede pulsar el propio interruptor (servidor, `OwnerTeam`) | Consistencia |
| Ayuda al que va detrás | Ninguna en MVP; opción posterior: enfriamiento −50 % si el equipo va > 20 % atrás | Se mide en playtest antes de añadirla |

**Catálogo de efectos.**

| Id | Efecto | Pieza | Estado | Coste (h) | Contrajuego |
|---|---|---|---|---|---|
| S1 | **Compuerta**: una compuerta emerge del suelo y cierra el paso 5 s | `ATN_ProcSabotageGate` + `ATN_ProcSwitch` (M4) | **MVP**, existe | ≈ 3 (M4, incluido en el catálogo) | Esperar; una jugadora con margen puede empezar el siguiente tramo por la ruta larga si hay bifurcación |
| S2 | **Desvío de géiser**: el géiser rival apunta a un hueco 4 s | `ATN_ProcGeyser` (M3) | Después | 2 a 3 | El puzzle vuelve a esperar el siguiente pulso |
| S3 | **Pasarela inestable**: plataformas tambaleantes con doble inclinación 6 s | `ATN_BeachWobblyPlatform` (parámetro nuevo) | Después | 3 a 4 | Cruzar por el otro lado o esperar |
| S4 | **Tinta**: proyectil de tinta a la zona común (ciega 3 s) | `ATN_InkProjectile` + `MulticastApplyInkEffect` (existente) | Después | 2 a 3 | Cerrar el caparazón (Ctrl) |
| S5 | **Luz baja** en la sala de pensar 4 s | Post-proceso local | Cut? | 2 a 3 | — |

**MVP: solo S1.** Con una compuerta y los tres tiempos de la tabla, el modo ya tiene su capa competitiva; el resto se añade tras el primer playtest de 4 personas.

**Casos límite.**
- Un pawn sobre la compuerta cuando va a subir: el servidor retrasa hasta 1 s y luego sube apartándolo hacia el lado de la víctima sin daño.
- Ronda que termina con una compuerta levantada: se baja y se limpia el estado.
- Las compuertas se colocan en tierra firme; el validador de Foto→Mapa comprueba que ninguna queda bajo `water_level_m`.

### 1.6 Emparejamiento y lobby

| Aspecto | Diseño |
|---|---|
| Formación | Lobby de Steam con invitación (el flujo actual). Dos plataformas de equipo (naranja y azul) en LVL_HQ junto al selector de modo: cada jugadora se sube a una; máx. 2 por plataforma. Si el anfitrión inicia con jugadoras sin plataforma, se rellenan por orden de unión |
| Emparejamiento público | Fuera de MVP. Más adelante, un botón «Buscar pareja» que junta a dos duos o a cuatro sueltas por orden de llegada; sin MMR |
| Rotación de parejas | Variante «Rotativo» (desactivada por defecto): reutiliza `AssignTwoVsTwoTeams` (AB|CD, AC|BD, AD|BC) y puntúa a cada jugadora por separado. Fuera de MVP |
| Distintivo del equipo | Color y **patrón de caparazón** (rayas frente a puntos, `ETNShellPattern`) para no depender solo del color (daltonismo) |

### 1.7 Mapas del 2 vs 2

Volumétricos de camino fijo, con `mode: "2v2"` en `MapSketch.json` (Foto→Mapa, spec §4) y las plantillas del catálogo de puzzles. Nombres propuestos con prefijo `DV` (duelo). Los tamaños son estimaciones propias, a validar con el generador.

| Id | Idea | Ruta | Puzzles (kind) | Estado |
|---|---|---|---|---|
| `DV01_canal` | Dos carriles a cada orilla de un canal; puentes de Mokius en las 2 zonas comunes | ≈ 400 m, 500 × 300 m | `throw_wall_pair` ×2, `wobbly_run`, `breakable_chain`, `catapult_gap`; sabotaje S1 | **MVP** |
| `DV02_dunas` | Dunas gemelas con acantilado central y bifurcación (atajo con `think_room`) | ≈ 450 m | `throw_wall_pair`, `counterweight_lift`, `lever_relay`, `think_room`, `wobbly_run` | Después (necesita N1, N3, N4, N6) |
| `DV03_faro` | Subida en espiral a un faro; los carriles se cruzan en las zonas comunes | ≈ 350 m | `geyser_ferry`, `counterweight_lift`, `lever_relay`, `catapult_gap` | Después |

Presupuesto por mapa: ≤ 0,9 M triángulos (Coop se ha fijado en ≤ 1,1 M; el 2 vs 2 renderiza dos carriles iguales, así que se reserva margen); cero assets nuevos (plan §3.6). Con solo el MVP la variedad es baja (3 de 8 puzzles del catálogo); es un límite conocido que se levanta al llegar N1, N3 y N6.

### 1.8 Red y autoridad

- **GameMode**: `ATN_ProcMapGameMode` en `Mode == TwoVsTwo` con `Source = Prepared` (plan §3.1) o GameMode propio si crece. Decide en el servidor: parejas, inicio de ronda, llegada a meta, sabotajes, puntuación y fin de encuentro.
- **GameState** replica un `FTNTeamScoreNet[2]`: `RoundWins`, `Progress` (0 a 1000, entero), `FinishedMembers`, `SabotagesLeft`, `SabotageReadyAt` (tiempo de servidor). Es el único dato que lee el HUD del 2 vs 2 (§2.2).
- **Progreso de puzzle** por `UTN_PuzzleStateComponent` (catálogo N5): el servidor lo actualiza; el anillo de progreso del mundo lo lee.
- **PlayerState**: `TeamIndex` ya existe (`TN_CoopPlayerState.h:210`). Se añade `SlotInTeam` (0/1) para colocar a las dos en el marcador.
- **Reconexión**: la cuenta se identifica por `UniqueNetId`; el `PlayerState` inactivo se conserva 45 s.
- **Entrada tardía**: no permitida en mitad de una ronda (solo reconexión de las 4).

### 1.9 Pruebas y aceptación

- Automatización (`Tortunabo.TwoVsTwo.*`): fin por segunda llegada; empate exacto; tope de 8 min con progreso; inmunidad 10 s; máximo 3 sabotajes por ronda; sabotaje propio rechazado; abandono a los 45 s.
- PIE 4P con PktLag 150 y 2 % de pérdida: los 4 clientes ven la misma marca de ronda y el mismo marcador; una reconexión en los 45 s restaura posición y progreso; la sirena llega antes de que suba la compuerta en todos.
- Playtest: 3 encuentros de 4 personas nuevas; la tasa de rondas que llegan al tope de 8 min es < 10 %; diferencia media entre equipos en la meta ≤ 30 s (si es mayor, el mapa está desequilibrado).
- Coste del modo: ≈ 30 a 42 h de lógica (GameMode/GameState/equipos/sabotaje/abandono) sin contar las piezas de puzzle del catálogo (§3).

## 2. Interfaz y efectos por modo

### 2.1 Lobby: selector de modo y de mapa

**Hoy.** La elección es física, en LVL_HQ: `ATN_ProcModeSelector` (interactuable, `ETNProcSelectorKind {Mode, Difficulty}`) pasa a la siguiente opción en cada interacción, guarda la elección en `UMP_GameInstance::SelectedProcMode/Difficulty` y replica la etiqueta 3D; la pizarra del General Galápago (`TNLobbyMission`) muestra lo mismo.

**Diseño (MVP).**

| Elemento | Diseño | Estado |
|---|---|---|
| Selector de modo | Se mantiene el interactuable. Lista de cinco: **Coop → Carrera → Rally → Todos contra Todos → 2 vs 2**. Interactuar avanza; mantener retrocede. La etiqueta 3D muestra nombre, icono de modo (cara HUD, `TN_HUDFaces.h`) y jugadoras necesarias | Cambio pequeño (enum ya contempla `Count`; hay que añadir `Rally`, `FreeForAll` y retirar `Classic`) |
| Requisito de jugadoras | Coop 1 a 8; Carrera 2 a 8; Rally 1 a 8 (biplaza si hay pareja); TcT 2 a 8; 2 vs 2 exactamente 4. Si no se cumple, la etiqueta se pone en rojo con «Necesita N» y la interacción de inicio suena a fallo | Nuevo, ≈ 3 h |
| Selector de mapa | Nuevo tipo `ETNProcSelectorKind::Map`: un segundo interactuable junto al de modo. Recorre **solo los mapas del modo elegido**, tomados del catálogo de mapas (`UTN_CoopMapCatalog` del plan §3.1 se generaliza a un catálogo con etiqueta de modo). Etiqueta: id, nombre y duración estimada | Nuevo, ≈ 10 a 14 h con catálogo |
| Vista previa del mapa | MVP: texto (id, nombre, duración, dificultad, jugadoras). Deseable: miniatura renderizada por el propio generador (plan §3.6: «Vista previa en la carga») | Deseable |
| Aleatorio | Primera opción de cada lista: «Al azar», que elige en el servidor al iniciar (semilla de la partida) | Nuevo, incluido en el anterior |
| Dificultad | El selector existente sigue para Coop y Carrera; en TcT, Rally y 2 vs 2 no aplica (la etiqueta se apaga) | Sin coste |
| Equipos (solo 2 vs 2) | Dos plataformas (naranja y azul) con contador de ocupantes; la etiqueta del selector de modo pone «Equipos: 1/2, 2/2» | Nuevo, ≈ 4 a 6 h |
| Quién decide | Solo el anfitrión interactúa con los selectores; el resto lo ve replicado (comportamiento actual) | Sin cambio |

**Red.** La elección viaja al servidor en la `UMP_GameInstance` del anfitrión y al iniciar la partida `ATN_HQGameMode` hace `ServerTravel` al nivel elegido (plan §3.1: `TravelURL`). El id de mapa elegido se replica en el `GameState` para que el marcador y la pantalla de fin puedan mostrarlo.

### 2.2 Los cinco modos

Cada tabla dice qué se **reutiliza** (nombre de clase real) y qué es **nuevo**.

#### Coop (mapas fijos con puzzles de grupo)

| Aspecto | Diseño |
|---|---|
| HUD | `UTN_RunHUDWidget` (cara, salvavidas de energía, pista de la salida a la meta) con la pista alimentada por la distancia a lo largo del camino del bake (`path_dist_dm`); `UTN_CoopFlowHUDWidget` para el estado («Puzzle 2/3 placas», cuenta atrás de puerta) |
| Marcadores | Anillo de progreso sobre el puzzle activo (**reutiliza `UTN_HoldRingWidget`**, alimentado por `UTN_PuzzleStateComponent.Progress`, ver §2.3); flechas de cada compañera fuera de pantalla; aviso «fuera de zona» al ser rescatada (plan §2.3) |
| Pantalla de fin | Panel de Resultados de `UTN_CoopFlowHUDWidget` (hasta 8 filas: puesto, nombre, tiempo, conchas) y `ATN_RacePodiumStage` si se quiere podio; sin necesidad de pantalla nueva |
| VFX | Rescate: aturdimiento de `TN_BeachStun` y pajaritos (`UTN_DizzyBirdsComponent`), ya previsto en el plan §3.6; puzzle resuelto: brillo + polvo (§2.3) |
| SFX | Placas, botones y puertas: `UTN_BeachTrapSynthComponent`; meta: `UTN_ScoreShellSynthComponent`; voz de proximidad ya existe |
| Nuevo | Cues de puzzle (§2.3) |

#### Carrera (playa procedural, tramos de autor)

| Aspecto | Diseño |
|---|---|
| HUD | Sin cambios: `UTN_RunHUDWidget` con pista playa-mar, `UTN_RaceRoundClockWidget`, `UTN_RaceRoundIntroWidget` («RONDA N»), `UTN_RaceSprintWidget`, `UTN_RaceFinishCountdownWidget` |
| Marcadores | Los actuales (tortugas de colores en la pista). Los tramos de autor no añaden HUD |
| Pantalla de fin | `UTN_RaceArrivalWidget` (puesto), `UTN_RaceTallyWidget` (recuento por ronda) y `UTN_RaceChampionWidget` con `ATN_RacePodiumStage`; botones Volver a jugar, Cambiar de modo, Salir (solo el anfitrión elige) |
| VFX | `UTN_BeachFinishSplashSubsystem`, polvo `UTN_TurtleDustComponent`, aturdimiento |
| SFX | `UTN_RaceCueSynthComponent` (cuentas atrás, título, puesto, trombón), `UTN_RaceItemSynthComponent`, `UTN_BeachSplashSynthComponent` |
| Nuevo | **Nada.** Solo la etiqueta del mapa en el selector (§2.1) |

#### Rally Tortuga (buggy, mapas geográficos)

| Aspecto | Diseño |
|---|---|
| HUD | Reutiliza `UTN_RunHUDWidget` (pista lineal con buggies como marcadores, con la ruta de coste mínimo del Rally) y el reloj/rondas de la Carrera (copa de 3 carreras: `UTN_RaceRoundIntroWidget`, `UTN_RaceTallyWidget`, `UTN_RaceChampionWidget`). **Nuevo**: velocímetro en texto (C++), posición «2.º / 8», contador de checkpoint («arco 3/9», distancia al siguiente), y **panel de artillera** con 2 ranuras de ítem y anillo de equilibrio (A2) hecho con `UTN_HoldRingWidget` |
| Marcadores | Arcos de checkpoint (mallas del arco de neumático de la meta; plan §3.6, prioridad 0); flecha de dirección fuera de pantalla al siguiente arco; nombre e icono del buggy delante y detrás a < 60 m |
| Pantalla de fin | Igual que Carrera (tally + campeón). La copa se cierra con `ATN_RacePodiumStage` con el buggy de la ganadora en lugar de la tortuga (variación de malla, sin sistema nuevo) |
| VFX | Polvo teñido por el terreno (`UTN_TurtleDustComponent`), salpicadura al pasar agua (`UTN_BeachFinishSplashSubsystem`), estela del turbo (mismo componente, con densidad ×3); marcas de derrape: deseable |
| SFX | **Nuevo** `UTN_BuggyEngineSynth` (motor, derrape, bocina; plan §3.6, ya previsto), impactos con `UTN_ShellImpactSynthComponent`, cues de checkpoint con `UTN_RaceCueSynthComponent` (nota ascendente por arco) |
| Nuevo | Velocímetro y contador de checkpoint, panel de artillera, sintetizador del motor |

#### Todos contra Todos (P01 y arenas)

| Aspecto | Diseño |
|---|---|
| HUD | `UTN_RunHUDWidget` sin pista; en su lugar **marcador de puntos por jugadora** (cara `TN_HUDFaces.h` + número, ordenado, con la propia resaltada); `UTN_RaceRoundClockWidget` con los 120 s; ranura del objeto activo (icono de los 7 objetos del plan §3.4) |
| Marcadores | **«Quién te empujó»** (`LastInstigator`): línea en pantalla «X te tiró» con la cara y una flecha de dirección durante 3 s; **+1 flotante** sobre la propia cabeza al puntuar (atribución dentro de 6 s); flechas de rivales fuera de pantalla a < 30 m; aviso de ráfaga de viento 1,5 s antes (borde de pantalla amarillo) |
| Reaparición | `UTN_GhostHatchWidget` (la cáscara de reaparición de la Carrera): 4 s en el huevo de la meseta con cuenta atrás y luego 2 s de inmunidad (parpadeo del cuerpo) |
| Pantalla de fin | Por ronda: `UTN_RaceTallyWidget` con puntos en lugar de puestos. Al final: `UTN_RaceChampionWidget` (por puntos totales). Ambas ya existen; se adaptan a «puntuación» en lugar de «orden de llegada» (§3, U8) |
| VFX | Estampado contra la pared: polvo + pajaritos + aplastado en código (plan §3.6); caída al río: salpicadura; ráfaga: polvo lateral |
| SFX | `UTN_ShellImpactSynthComponent` (choque, bola, estampado), `UTN_RaceItemSynthComponent` (objetos), `UTN_BeachSplashSynthComponent` (río), `UTN_AmbientSynthComponent` (viento) y `UTN_RaceCueSynthComponent` (+1: notita de moneda) |
| Nuevo | Marcador por puntos, aviso «X te tiró», +1 flotante, flechas de rivales |

#### 2 vs 2

| Aspecto | Diseño |
|---|---|
| HUD | `UTN_RunHUDWidget` con **dos pistas paralelas** (equipo propio y rival) en la barra de progreso: dos caparazones por pista, con el patrón de equipo. Arriba al centro, **pastillas de rondas** (○● vs ●○, al mejor de 3) y el reloj de ronda (`UTN_RaceRoundClockWidget`). Debajo, `SabotageReadyAt` como icono con anillo (listo / en enfriamiento / inmune) |
| Marcadores | **Compañera**: nombre y cara flotando sobre su cabeza y flecha de borde de pantalla si está a > 40 m. **Puzzle activo**: anillo de progreso (§2.3). **Sabotaje entrante**: borde rojo parpadeante 1 s y sirena (aviso justo) y, mientras dure, un contador de 5 s sobre la compuerta. **Llamada «¡Ya!»**: botón de rueda radial (`UTN_RadialWheelWidgetBase`, ya existe) que inicia una cuenta atrás compartida de 3, 2, 1 con el sonido de `UTN_RaceCueSynthComponent` a las dos: es la ayuda para `lever_relay`. Quick chat de `UTN_CoopFlowHUDWidget` para el resto |
| Pantalla de fin de ronda | `UTN_RaceRoundIntroWidget` («RONDA 2 · vais 1–0») y `UTN_RaceArrivalWidget` adaptada: «Ganáis / Perdéis por 12,4 s» con las dos caras |
| Pantalla de fin de encuentro | `UTN_RaceChampionWidget` con «¡Pareja campeona!»: las dos caras juntas en el escalón alto de `ATN_RacePodiumStage` y estadísticas de §1.3; botones Revancha, Cambiar de modo, Salir (solo el anfitrión) |
| VFX | Compuerta que emerge: nube de polvo del suelo (`UTN_TurtleDustComponent` con color de equipo); puzzle resuelto: destellos + polvo (§2.3); meta: `UTN_BeachFinishSplashSubsystem` |
| SFX | Sirena de sabotaje, compuerta (golpe grave), cues de puzzle (§2.3): sintetizados; meta y cuentas atrás: `UTN_RaceCueSynthComponent`; hitos: `UTN_ScoreShellSynthComponent` |
| Nuevo | Marcador de dos equipos, pastillas de ronda, indicador de sabotaje, llamada «¡Ya!», marcador de compañera, cues de puzzle |

### 2.3 Elementos comunes de puzzle (Coop y 2 vs 2)

| Elemento | Diseño | Reutiliza | Nuevo |
|---|---|---|---|
| Anillo de progreso | Anillo en el mundo (billboard) sobre la pieza principal: se llena según `Progress` del puzzle; verde al resolverse, rojo al fallar | `UTN_HoldRingWidget` | Enlace con `UTN_PuzzleStateComponent` |
| Cartel de primera vez | Un cartel de malla procedural con pictogramas (tortuga+flecha+muro) junto al primer puzzle de cada tipo; se apaga tras verlo una vez (registro en `TN_TutorialSaveGame`) | `TN_TutorialWidget`, `TN_InteractPromptWidget` | Pictogramas en `TNBeachProp` |
| Indicación de interacción | Prompt «Tirar de la palanca» / «Pulsar» | `TN_InteractPromptWidget` | Ninguno |
| Éxito | Campanita ascendente de 3 notas + destello y polvo alrededor de la pieza | `UTN_RaceCueSynthComponent` (patrón), polvo | `UTN_PuzzleCueSynth` (3 cues) |
| Fallo | Zumbido corto descendente y luz roja 1 s | ídem | Incluido |
| Placa / palanca / puerta | Clic de placa, golpe grave de palanca, rodar de puerta | `UTN_BeachTrapSynthComponent` | Incluido |
| Sirena de sabotaje | Dos notas alternadas, 1 s | ídem | Incluido |

### 2.4 Accesibilidad y red de la interfaz

- **Color no basta**: equipos con color + patrón de caparazón; los cues de fallo y éxito tienen sonido además de luz.
- **Texto**: todo en español con ortografía completa; marcador y avisos con contraste mínimo 4,5:1 sobre el panel azul marino del HUD Tortunavy.
- **Red**: los widgets leen estado replicado con sondeo cada `RefreshInterval` y el delegado de estado como respaldo (patrón de `UTN_CoopFlowHUDWidget`); ningún widget envía datos de juego, solo peticiones (`RequestChampionChoice`).
- **Rendimiento**: sin texturas nuevas; los widgets se construyen en código como los actuales.

## 3. Piezas nuevas imprescindibles y coste

Horas de estimación propia, primera pasada, sin depuración de red. «Imprescindible» = sin ella el modo no se entiende o no se puede jugar. Las piezas de puzzle en C++ (N1 a N6, M1 a M4) están en `Docs/Catalogo-Puzzles-2026-09-29.md` §2 (≈ 43 a 59 h en total) y no se repiten aquí.

### 3.1 Interfaz

| Id | Pieza | Modo | Basada en | Imprescindible | h |
|---|---|---|---|---|---|
| U1 | Marcador de dos equipos (`UTN_TeamRaceHUDWidget`): dos pistas, pastillas de ronda, icono de sabotaje | 2 vs 2 | `UTN_RunHUDWidget` | Sí | 10 a 14 |
| U2 | Anillo de progreso de puzzle enlazado a `Progress` | Coop, 2 vs 2 | `UTN_HoldRingWidget` | Sí | 4 a 6 |
| U3 | Marcador de compañera y flechas de borde de pantalla (compartido con TcT y Rally) | Todos | `TN_HUDFaces.h`, `UTN_RunHUDWidget` | Sí | 6 a 8 |
| U4 | Alerta de sabotaje (borde rojo, contador sobre la compuerta) y llamada «¡Ya!» en la rueda radial | 2 vs 2 | `UTN_RadialWheelWidgetBase`, `TN_InteractPromptWidget` | Sí (alerta); «¡Ya!» deseable | 5 a 7 |
| U5 | Selector de mapa (`ETNProcSelectorKind::Map`), lista de cinco modos, requisito de jugadoras, catálogo con etiqueta de modo | Todos | `ATN_ProcModeSelector` | Sí | 13 a 17 |
| U6 | Plataformas de equipo del lobby | 2 vs 2 | `ATN_DirectInteractableBase` (volumen de placa) | Sí | 4 a 6 |
| U7 | HUD del Rally: velocímetro, posición, checkpoint, panel de artillera | Rally | `UTN_RunHUDWidget`, `UTN_HoldRingWidget` | Sí | 12 a 16 |
| U8 | Marcador de TcT por puntos, aviso «X te tiró», +1 flotante | TcT | `UTN_RunHUDWidget`, `UTN_GhostHatchWidget` | Sí | 8 a 12 |
| U9 | Tally y Champion por puntos y por equipos (Arrival con dos caras, podio con la pareja) | TcT, 2 vs 2 | `UTN_RaceTallyWidget`, `UTN_RaceChampionWidget`, `ATN_RacePodiumStage` | Sí | 10 a 14 |
| U10 | Cartel de primera vez con pictogramas de puzzle | Coop, 2 vs 2 | `TN_TutorialWidget` | Deseable | 4 a 6 |
| | **Subtotal imprescindible (U1 a U9, sin «¡Ya!»)** | | | | **≈ 72 a 100** |

### 3.2 Efectos

| Id | Pieza | Modo | Basada en | Imprescindible | h |
|---|---|---|---|---|---|
| F1 | `UTN_PuzzleCueSynth`: éxito, fallo, placa, palanca, puerta, sirena | Coop, 2 vs 2 | `UTN_RaceCueSynthComponent`, `UTN_BeachTrapSynthComponent` | Sí | 8 a 10 |
| F2 | `UTN_BuggyEngineSynth`: motor, derrape, bocina (ya previsto en el plan §3.6) | Rally | `UTN_ScoreShellSynthComponent` (patrón de `ISoundGenerator`) | Sí | 10 a 14 |
| F3 | Destello de puzzle resuelto y polvo de compuerta | Coop, 2 vs 2 | `UTN_TurtleDustComponent`, `UTN_BeachFinishSplashSubsystem` | Sí | 3 a 5 |
| F4 | +1 flotante con partículas de moneda | TcT | `UTN_RaceCueSynthComponent` | Deseable | 2 a 3 |
| F5 | Marcas de derrape (decal) | Rally | — | Deseable | 6 a 8 |
| | **Subtotal imprescindible (F1 a F3)** | | | | **≈ 21 a 29** |

### 3.3 Resumen

| Concepto | Piezas | Horas |
|---|---|---|
| Interfaz imprescindible | 9 (U1 a U9) | 72 a 100 |
| Efectos imprescindibles | 3 (F1 a F3) | 21 a 29 |
| Lógica del modo 2 vs 2 (§1.9) | GameMode, GameState, equipos, sabotaje, abandono | 30 a 42 |
| Piezas de puzzle en C++ del catálogo | 6 nuevas y 4 modificadas | 43 a 59 |
| **Total nuevo** | **≈ 18 piezas + 4 modificaciones** | **≈ 166 a 230** |
| Assets de arte nuevos imprescindibles | 0 | — |

Orden sugerido: U5+U6 (selector y equipos) → lógica 2 vs 2 → M1/M4/N4/N5 + `throw_wall_pair` + `sabotage` → U1/U4/F1 → `DV01_canal` jugable de 4 personas. El resto (U7/F2 de Rally, U8 de TcT) sigue los plazos de sus modos.

## 4. Preguntas y decisiones para el director

1. **2 vs 2 procedural existente** (§0): ¿se retira el generador de carriles y se conservan solo los actores de puzzle y el reparto de parejas? Recomendación: sí.
2. **Contacto en zonas comunes** (§1.4): ¿se permite agarrar y lanzar a rivales? Recomendación: no en MVP; sí la bola y el noqueo por velocidad.
3. **Abandono** (§1.3): regla dura de forfeit tras 45 s. ¿Se prefiere un bot que haga de pareja pasiva en los puzzles de espera? Cuesta ≥ 40 h y es frágil.
4. **Conchas de recompensa** (3 / 1) y la duración del encuentro (mejor de 3, 12 a 18 min) son primera pasada; se ajustan en el primer playtest de 4.
5. **Emparejamiento público**: fuera de MVP. ¿Se quiere «Buscar pareja» antes de Steam (plan §4)?
6. **Selector físico**: se conserva el interactuable del HQ frente a un menú clásico. Recomendación: mantener; un menú de mapas con miniaturas queda como deseable.
7. **Lista de modos del selector**: ¿el modo Clásico (Run) desaparece del selector? El plan §7.1 lo retira; conviene confirmarlo antes de tocar el enum.

## 5. Criterios de aceptación de este documento

- Los cinco modos tienen HUD, marcadores, pantalla de fin, selector y efectos descritos con clases reales (comprobado por búsqueda de cada nombre en `Source/Tortunabo`).
- Cada pieza nueva imprescindible tiene coste y clase base de la que parte.
- 2 vs 2: reglas, puntuación, fin, emparejamiento, sabotajes y mapas cerrados salvo los puntos de §4.


## Decisiones aplicadas (2026-09-29)

Tomadas por delegación del director, coherentes con el plan maestro:

1. El 2 vs 2 procedural actual (`ETNProcGameMode::TwoVsTwo`, generador de carriles) se retira a favor del 2 vs 2 volumétrico de §7.9; el código pasa a `Deprecado/` cuando exista el nuevo.
2. **Corregido por el director:** en el 2 vs 2 SÍ se puede agarrar y lanzar a los rivales (con la regla general: solo a quien está en caparazón), además de a la compañera. Es parte del sabotaje entre equipos.
3. Si una jugadora abandona, su pareja pierde la ronda a los 45 s si no vuelve; sin bots.
4. Clásico sale del selector de modos y se retira con el modo Run (plan F0–F2, tareas T35–T37).
5. Los valores marcados «supuesto» se miden en PIE antes de fijarlos.
