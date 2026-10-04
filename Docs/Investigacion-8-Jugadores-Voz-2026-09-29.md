# Investigación: 8 jugadores con chat de voz en listen server con Steam

Fecha: 2026-09-29 · Rama: `macro-update` · Motor: UE 5.6 · Solo investigación (sin cambios de código ni compilación).

Alcance: Carrera, Rally y Todos contra Todos a 8, con voz de 8 personas y réplica de 8 en un listen server sobre Steam. Las cifras de red son **estimaciones** hechas sobre el código y las especificaciones. Nada de este documento se ha medido en runtime; la sección 5 explica cómo medirlo.

## Veredicto

**Viable con condiciones.** La réplica de juego a 8 cabe en la subida de un anfitrión doméstico si el Rally usa la opción E de `Docs/Analisis/2026-09-29/B_buggy_sync.md` (eliminado). La voz actual no cabe: el códec propio (µ-law de 8 bits a 16 kHz) gasta unos **17 KB/s por flujo**. En el pico (8 personas hablando a la vez) el anfitrión subiría entre **545 y 685 KB/s (4,4–5,5 Mbit/s)**, lo que satura una línea de 5 Mbit/s. Con Opus a 24 kbit/s (unos 3,5 KB/s por flujo) el peor caso baja a **160–250 KB/s (1,3–2,0 Mbit/s)** en los tres modos.

Condiciones:

1. Pasar la voz a Opus.
2. Rally con la opción E: autoridad del conductor y 30/10 Hz.
3. Poner pulsar para hablar por defecto y limitar las voces que recibe cada oyente.
4. Bajar la frecuencia de los actores siempre relevantes que ahora replican a la frecuencia por defecto.
5. Medir con 8 clientes Steam reales antes de dar la cifra por buena.

## 1. Estado actual (con fichero y línea)

### Sesión y plazas

| Qué | Valor | Dónde |
|---|---|---|
| Tope global de plazas | 8 | `Config/DefaultGame.ini:11` (`MP_GameInstance.MaxPlayers`) y `:19` (`[/Script/Engine.GameSession] MaxPlayers=8`) |
| Tamaños de sala | 4, 6 u 8 | `Source/Tortunabo/Public/Multiplayer/TN_RoomTypes.h:90-92` (`TNRoomLimits`) |
| Plazas de la sesión Steam | `NumPublicConnections = ActiveRoom.MaxPlayers` | `Source/Tortunabo/Private/Multiplayer/MP_GameInstance.cpp:533` |
| Lobby de Steam, presencia y unirse en curso | `bUseLobbiesIfAvailable`, `bUsesPresence`, `bAllowJoinInProgress = true` | `MP_GameInstance.cpp:535-539` |
| Control de plazas al entrar | Lo aplica `HandleGameModePreLogin` en el servidor | `MP_GameInstance.cpp:2096-2103` |
| Marcador «Sala: X/Y» del lobby | `LobbyExpectedPlayers = 0`, que toma las plazas de la sesión (8) | `Source/Tortunabo/Public/Lobby/TN_HQGameMode.h:77-80` y `Private/Lobby/TN_HQGameMode.cpp:279` |
| AppID | 480 (Spacewar) | `Config/DefaultGame.ini:10`, `Config/DefaultEngine.ini:116` |

`bUseLobbiesVoiceChatIfAvailable` no se usa (no hace falta, porque la voz del motor está apagada).

### Red

| Ajuste | Valor | Dónde |
|---|---|---|
| Driver de red | `SteamSocketsNetDriver`, con `IpNetDriver` de reserva | `Config/DefaultEngine.ini:108-109` |
| `NetServerMaxTickRate` / `MaxNetTickRate` | 60 / 60 | `DefaultEngine.ini:123-124` |
| `MaxInternetClientRate` / `MaxClientRate` | 200 000 B/s por conexión | `DefaultEngine.ini:125-126` |
| `ConfiguredInternetSpeed` / `ConfiguredLanSpeed` | 200 000 | `DefaultEngine.ini:131-132` |
| Frecuencia adaptativa | `net.UseAdaptiveNetUpdateFrequency=1` | `DefaultEngine.ini:137` |
| `TotalNetBandwidth` | No está en el proyecto. De serie vale 32 000 (`Engine/Config/BaseGame.ini:20`), pero en 5.6 **nadie llama** a `AGameNetworkManager::UpdateNetSpeeds` (`GameNetworkManager.cpp:89-99`; solo se llama desde su propio temporizador). No limita nada | — |
| **Reserva `IpNetDriver`** (pruebas con `-nosteam`) | **Sin configurar**: toma la base del motor, **30 Hz y 100 000 B/s** (`Engine/Config/BaseEngine.ini:1768-1773`) | Las pruebas locales miden otra configuración que Steam |
| Tamaño de paquete Steam | `MAX_PACKET = 1024` B | `Engine/Plugins/Runtime/Steam/SteamSockets/Source/SteamSockets/Private/SteamSocketsNetConnection.cpp:15` |
| Límite de envío de Steam | `SendRateMin/Max` = 0 (sin límite), según el comentario del SDK | `Engine/Source/ThirdParty/Steamworks/Steamv157/sdk/public/steam/steamnetworkingtypes.h:1190-1194` |

**Nada protege la subida total del anfitrión.** Cada conexión puede llegar a 200 KB/s, así que con 7 clientes el techo teórico es 1,4 MB/s (11 Mbit/s).

### Voz (sistema propio, no el VOIP del motor)

| Qué | Valor | Dónde |
|---|---|---|
| VOIP del motor | Apagado (`[Voice] bEnabled=false`) | `Config/DefaultEngine.ini:119-120` |
| `RegisterRemoteTalker`, `UVOIPTalker`, `StartNetworkedVoice`, `bHasVoiceEnabled`, `VoiceNotificationDelta` | **Sin uso** en `Source/` | — |
| Componente | `UProximityVoiceComponent` (captura WASAPI, reducción de muestreo, compresión y RPC) | `Source/Tortunabo/Public/Voice/ProximityVoiceComponent.h:15-24` |
| Dónde se crea | En `OnPossess`, sobre cualquier pawn poseído | `Source/Tortunabo/Private/Player/MP_GamePlayerController.cpp:186-193` |
| Códec | µ-law de 8 bits, 1 byte por muestra | `ProximityVoiceComponent.cpp:693-707` |
| Muestreo | Captura a 48 kHz, reducida ×3 a **16 kHz** | `.h:118`, `.h:133` |
| Envío | Cada 80 ms (`SendInterval`), unos 1 280 B por paquete | `.h:124`, `.cpp:520-546` |
| Cliente → servidor | `Server_SendVoiceData`, no fiable; tope de 8 192 B y límite de 25 Hz en el servidor | `.h:175-180`, `.cpp:557-588` |
| Servidor → oyentes | Solo los oyentes a menos de `OuterRadius` = 25 m y, como mucho, los **4 más cercanos** (`MaxVoiceListeners = 4`), por `ClientReceiveVoice` no fiable | `.h:109-140`, `.cpp:601-634`, `MP_GamePlayerController.h:210-211` |
| Atenuación en el receptor | `InnerRadius` = 3 m, `OuterRadius` = 25 m | `.h:108-112`, `.cpp:360-365` |
| Pulsar para hablar | Existe, pero **viene apagado** (`bPushToTalk = false`, tecla V). Por defecto el micro está abierto con puerta de umbral (`SpeakingThreshold` = 0,01, retención de 0,3 s) | `Public/Settings/TN_SettingsSaveGame.h:57-61`, `Private/Settings/TN_GameSettingsSubsystem.cpp:1167-1180` |
| Voz por equipo o de espectador | No existe | — |

Consecuencias técnicas:

- Los 1 280 B por paquete superan el `MAX_PACKET` de 1 024 B, así que cada envío sale en **2 bunches parciales no fiables**. Si se pierde uno, se pierde el fragmento entero, y con un 2 % de pérdida por paquete se pierde un 4 % de los fragmentos.
- El tope de 4 limita los oyentes **por hablante**, no las voces **por oyente**. Un cliente puede recibir 7 flujos a la vez.

### Frecuencias de réplica relevantes

| Actor | Frecuencia (mín.) | Relevancia | Dónde |
|---|---|---|---|
| Tortuga (`ATortugaCharacter`) | 30 (10) Hz | `bAlwaysRelevant` | `Private/Player/TortugaCharacter.cpp:70-75` |
| PlayerState | 5 (1) Hz | — | `Private/Core/TN_CoopPlayerState.cpp:16-17` |
| Cangrejo | 25 Hz | Por distancia | `Private/World/TN_CrabActor.cpp:29` |
| Objetos físicos e interactuables | 30/10 y 4/2 Hz | `DORM_DormantAll` | `TN_PhysicsObjectActor.cpp:47-51`, `TN_InteractableBase.cpp:14-16` |
| **Gaviota enemiga, proyectil de tinta, quad y excremento de gaviota** | **Sin fijar: la de serie de `AActor` (100 Hz, en la práctica el tick de 60)** | `bAlwaysRelevant` y movimiento replicado | `TN_EnemySeagull.cpp:21`, `TN_InkProjectile.cpp:15`, `TN_QuadActor.cpp:17`, `TN_SeagullDroppingActor.cpp:18` |

Salvedad: un Blueprint hijo podría sobrescribir esa frecuencia. No se ha comprobado en los assets.

ReplicationGraph e Iris: **no se usan**. No hay ninguna referencia en `Config/`, `Tortunabo.Build.cs` ni `Tortunabo.uproject`.

## 2. Ancho de banda estimado

Supuestos:

- Sobrecarga por paquete: cabecera de UE, bunch, UDP/IP y el relé de Steam (SDR), unos 70 B.
- La voz del anfitrión le llega a él mismo sin coste de red.
- «Típico» = 2 personas hablando a la vez. «Pico» = 8 a la vez.

### Voz por flujo

| Códec | Carga útil | Con cabeceras | kbit/s |
|---|---|---|---|
| Actual (µ-law de 8 bits a 16 kHz) | 16,0 KB/s | **≈ 17 KB/s** (2 paquetes cada 80 ms) | ≈ 136 |
| Opus VOIP a 24 kbit/s y 16 kHz (`FVoiceEncoderOpus`, `Engine/Source/Runtime/Online/Voice/Private/VoiceCodecOpus.cpp:236, 386-393`; 50 tramas/s, `:30`) | 3,0 KB/s | **≈ 3,5 KB/s** (240 B cada 80 ms, 1 paquete) | ≈ 28 |
| Voz nativa de Steam (`VoiceEngineSteam`, también Opus) | ≈ 2–3 KB/s | ≈ 3 KB/s | Referencia, no se propone |

### Voz: subida del anfitrión

El anfitrión reenvía cada flujo a cada oyente remoto, así que su subida es hablantes × oyentes.

| Escenario | Flujos | Códec actual | Opus |
|---|---|---|---|
| Típico: 2 hablan, 4 oyentes cada uno | 8 | 136 KB/s | 28 KB/s |
| Pico con el tope de 4: 8 hablan | 32 | **544 KB/s (4,4 Mbit/s)** | 112 KB/s (0,9 Mbit/s) |
| Pico sin tope (`MaxVoiceListeners = 0`) | 49 | 833 KB/s (6,7 Mbit/s) | 172 KB/s |
| Bajada de un cliente en el peor caso: 7 voces | 7 | 119 KB/s (de un tope de 200) | 25 KB/s |

### Juego sin voz: subida del anfitrión (7 clientes remotos)

| Modo | Estimación | Fuente |
|---|---|---|
| Carrera | **50–90 KB/s**. Tortugas: 7 × 7 × ~1,3 KB/s a 30 Hz en movimiento. Más PlayerState, enemigos y conchas dormidas. Las gaviotas a 60 Hz empujan hacia el extremo alto | `Docs/superpowers/specs/modos/02-Carrera.md:358` (5–8 KB/s por cliente, objetivo ≤ 60 KB/s sin voz) |
| Todos contra Todos | **80–110 KB/s** | `04-TodosContraTodos.md:343` (78 KB/s + margen, objetivo ≤ 140) |
| Rally, opción E (30/10 Hz) | **98 KB/s en régimen y 104 en la salida** | `03-Rally.md:366-382` |
| Rally ingenuo (8 buggies a 60 Hz) | **280 KB/s (2,2 Mbit/s)** | `Docs/Analisis/2026-09-29/B_buggy_sync.md:84-88` (eliminado) |

Bajada del anfitrión: `ServerMove` de 7 clientes, unos 2–4 KB/s cada uno, así que menos de 30 KB/s (no limita).

### Total de subida del anfitrión y comparación con una línea doméstica

Referencia de subida doméstica:

- 5 Mbit/s = 625 KB/s. Con un uso sano del 40–60 % (resto de la casa y bufferbloat) quedan **250–375 KB/s**.
- 20 Mbit/s = 2 500 KB/s.
- Objetivo propuesto para el anfitrión: **≤ 250 KB/s (2 Mbit/s)**.

| Modo | Voz actual, típico / pico | Opus, típico / pico | ¿Cabe en 5 Mbit/s? |
|---|---|---|---|
| Carrera | 186–226 / 594–634 KB/s | **78–118 / 162–202 KB/s** | Actual: no en el pico. Opus: sí |
| Todos contra Todos | 216–246 / 624–654 KB/s | **108–138 / 192–222 KB/s** | Actual: no. Opus: sí |
| Rally, opción E | 234–240 / 642–648 KB/s | **126–132 / 210–216 KB/s** | Actual: no. Opus: sí, en el límite en la salida |
| Rally ingenuo | 416 / 824 KB/s | 308 / 392 KB/s | No en ningún caso |

Con 20 Mbit/s de subida cabe todo, incluso la voz actual. El problema es el anfitrión con ADSL, cable asimétrico o 4G, que es la mediana fuera de España. Aunque el ancho de banda sobre, la voz actual (2 paquetes por envío) sigue siendo más frágil ante la pérdida de paquetes.

## 3. Riesgos y mitigaciones

| Riesgo | Mitigación | Prioridad |
|---|---|---|
| Voz sin comprimir de verdad: 17 KB/s por flujo | Opus a 24 kbit/s con el módulo `Voice` del motor (`IVoiceModule::CreateVoiceEncoder`/`CreateVoiceDecoder`, 16 kHz mono) en `ProximityVoiceComponent`. Divide la voz entre 5 y quita los bunches parciales | **Crítica** |
| Micrófono abierto por defecto: el ruido de fondo cuenta como voz y el pico de 8 hablantes se vuelve habitual | `bPushToTalk = true` por defecto; conservar la puerta de umbral para quien elija voz abierta | Alta |
| El tope limita oyentes por hablante, no voces por oyente | El servidor da a cada oyente como mucho 4 voces a la vez, las más cercanas. En la salida de la carrera (8 juntos) acota tanto la bajada como la subida | Alta |
| 2 vs 2: el compañero puede quedar fuera de los 4 más cercanos, o a más de 25 m | Canal de equipo: el compañero siempre se oye, sin atenuación (o con una mínima) y fuera del tope. Los rivales, por proximidad | Media (solo 2 vs 2) |
| Espectadores y muertos hablando a los vivos | Canal de espectador: los muertos solo se oyen entre sí y con los vivos a menos de 3 m, o se decide por diseño | Media |
| Saturación de la conexión: la voz no fiable compite con el juego | Antes de `ClientReceiveVoice`, no reenviar si `Connection->IsNetReady(false)` es falso (la conexión está saturada); se pierde voz antes que movimiento | Media |
| Actores `bAlwaysRelevant` sin frecuencia (gaviota, tinta, quad, excremento) | Fijar 15–20 Hz (mínimo 5) y `NetCullDistanceSquared` a 150–300 m donde lo permita el diseño; `bAlwaysRelevant` solo para las 8 tortugas y los estados globales | Media |
| Buggies a 60 Hz | Opción E + 30 Hz cerca, 10 lejos y 5 a más de 300 m (`03-Rally.md:382`); `ForceNetUpdate` en choques, turbo y enderezado | Alta en Rally |
| Pruebas locales con otra configuración que Steam (`IpNetDriver` a 30 Hz y 100 KB/s) | Copiar los valores de `SteamSocketsNetDriver` en `[/Script/OnlineSubsystemUtils.IpNetDriver]` | Alta (invalida las medidas) |
| Sin tope de subida total del anfitrión | Tras medir, bajar `MaxClientRate`/`MaxInternetClientRate` a 100 000 por conexión, como seguro (techo de 700 KB/s); no es el objetivo, es un cortafuegos | Baja |
| Voz del conductor en el Rally | El componente se crea en cada `OnPossess` (`MP_GamePlayerController.cpp:186`). Si el jugador posee el buggy, la voz se crea en el buggy: verificar el cierre de la captura al cambiar de pawn y que el buggy sea relevante para el oyente (si no, `ClientReceiveVoice` llega con `SpeakerActor` nulo y se descarta) | Media |
| Dormancy | Ya está bien usada: `DORM_DormantAll` y `FlushNetDormancy` en objetos físicos, interactuables, medusas y conchas. Extenderla a cajas, trampas y objetos del Rally | Baja |
| **ReplicationGraph** | **No merece la pena a 8.** Rinde con decenas de conexiones y miles de actores. El plugin sigue en **beta** (`Engine/Plugins/Runtime/ReplicationGraph/ReplicationGraph.uplugin:15`) y costaría 20–40 h. Con 8 clientes, la relevancia y frecuencia por actor dan lo mismo | Descartado |
| **Iris** | **No en producción.** En 5.6 se compila (`TargetRules.cs:1113`, `bUseIris = true`), pero el plugin está marcado **experimental** (`Engine/Plugins/Experimental/Iris/Iris.uplugin:15`) y no se activa sin `net.Iris.UseIrisReplication=1`. Obligaría a revisar `NetSerialize` propios, dormancy y RPC grandes. Reevaluar en 5.7 o 5.8 | Descartado |
| Cuantización | `FRepMovement` ya va con cuantización mínima por defecto (posición entera, rotación en bytes). La ganancia está en el buggy: estado compacto con `NetSerialize` (posición int16 relativa, rotación de 8 bits, velocidad cuantizada), que da unos 24 B de subida por cliente (`03-Rally.md:379`). En la tortuga no merece la pena | Media en Rally |

## 4. Referencias externas

En esta sesión no había herramientas web (WebSearch/WebFetch), así que no hay nada verificado en internet. Lo siguiente sale del código del motor 5.6 y del SDK de Steam incluido:

- **SteamSockets** está marcado como no beta (`SteamSockets.uplugin:12`). Usa el relé de Steam (SDR): oculta la IP del anfitrión y añade unos 10–30 ms (dato conocido, no medido). El SDK 1.57 no limita la tasa de envío por defecto. El límite práctico es la subida del anfitrión, no Steam.
- **El VOIP del motor con Steam** (`VoiceEngineSteam.cpp`) también pasa por la conexión del juego: en un listen server el anfitrión reenvía igual. Cambiar al VOIP del motor no quita el reenvío. Solo lo quitaría un servicio de voz externo (EOS Voice/RTC, Vivox), a costa de otra dependencia, de cuentas y de reimplementar la proximidad ajustando el volumen por participante. No se recomienda para el lanzamiento.
- Casos conocidos de 4 a 8 jugadores con anfitrión jugador y voz de proximidad (Deep Rock Galactic en UE4, Lethal Company en Unity con 4 de serie, Human: Fall Flat con 8, Valheim con 10). Todos usan códecs de 16–32 kbit/s del estilo de Opus. **No se ha verificado en esta sesión**; confirmarlo con `agy` si hace falta citarlo.

## 5. Plan de verificación y búsqueda de bugs a 8

| Paso | Cómo | Qué mide | Criterio de aceptación |
|---|---|---|---|
| 1. PIE a 8 | Play As Listen Server, 8 jugadores, «Run Under One Process» apagado; emulación de red «Custom»: 150 ms, varianza de 30 y 2 % de pérdida (o `NetEmulation.PktLag 150`, `NetEmulation.PktLoss 2` en consola) | Lógica de roles, OnRep, unirse en curso | Una ronda completa por modo sin `ensure` ni errores de red |
| 2. 8 instancias `-game` en un PC | Anfitrión: `UnrealEditor-Win64-DebugGame.exe Tortunabo LVL_x?listen -game -nosteam -windowed -ResX=640 -ResY=360`. 7 clientes: `127.0.0.1 -game -nosteam -nullrhi -PktLag=150 -PktLoss=2`. Hace falta haber copiado antes la configuración a `IpNetDriver` | Frecuencias y ancho de banda sin Steam | Anfitrión ≤ 250 KB/s con voz sintética |
| 3. Monkey y estrés | `TN.Monkey` en los 7 clientes: el componente y el plan existen (`Public/Testing/TN_MonkeyPlan.h`, `Private/Testing/TN_MonkeyComponent.cpp`), pero **el comando de consola todavía no está registrado**. `TN.Stress race8` **no existe aún**. Hace falta una inyección de voz sintética (`TN.Voice.FakeTalk <porcentaje>`) para medir la voz sin micrófonos ni personas | Atascos, fugas y picos de red con semilla reproducible | 30 min sin fallos y sin crecimiento de memoria de más del 5 % |
| 4. Perfilado | `stat net` en el anfitrión (Out Rate, Out Saturation, Packet Loss). Arrancar con `-NetTrace=1 -trace=net,default` y abrir **Networking Insights** en Unreal Insights (bytes por actor, propiedad y RPC). `net.ListActorChannels` para ver los canales abiertos. El Network Profiler antiguo queda sustituido por Insights | Qué actor y qué RPC gastan más | Voz ≤ 30 % de la subida en el caso típico |
| 5. Detector de desincronía | Especificado en `Docs/superpowers/specs/modos/00-Arquitectura-y-Bugs.md` §2 (un hash cada 2 s y `[Desync]` con el campo que difiere). **Sin implementar**: ni `[Desync]` ni `LogTNNet` aparecen en `Source/` | Divergencias de estado sin mirar vídeos | 0 `[Desync]` en una ronda de 8 con 150 ms y 2 % de pérdida |
| 6. Steam real con 8 cuentas | Steam solo admite **una cuenta por sesión de Windows**, así que hacen falta 8 PCs o equivalentes. Con el AppID 480 cualquier cuenta gratuita sirve; con el AppID propio hacen falta claves o Steam Playtest | NAT, SDR, latencia real y voz real | Ronda completa a 8 en los 3 modos |

Alternativas para el paso 6, de menor a mayor fidelidad:

- **a)** 2–3 PCs o portátiles propios más máquinas virtuales Hyper-V con Windows y Steam, con clientes `-nullrhi` y Monkey (unos 4 GB de RAM por máquina virtual). Sirve para red y SDR, pero todo sale desde la misma conexión de casa.
- **b)** Máquinas virtuales en la nube: 7 instancias Windows de ~0,5–1 €/h, unos 10 € por sesión de 2 h. Dan rutas de internet reales.
- **c)** Playtest con 8 personas en 3 o más domicilios y proveedores distintos. Es **obligatorio al menos una vez** para validar el anfitrión doméstico y la voz humana. Steam Playtest facilita las claves.

Sandboxie u otros trucos para dos instancias de Steam en un mismo PC funcionan de forma no oficial. No se recomiendan.

Orden: 1 → 2 (tras los cambios de Config) → 4 → 3 → 5 → 6a → 6c.

## 6. Cambios necesarios

### Config

| Fichero | Cambio |
|---|---|
| `Config/DefaultEngine.ini` | Nueva sección `[/Script/OnlineSubsystemUtils.IpNetDriver]` con `NetServerMaxTickRate=60`, `MaxNetTickRate=60`, `MaxClientRate=200000`, `MaxInternetClientRate=200000`, para que las pruebas con `-nosteam` midan lo mismo que Steam |
| `Config/DefaultEngine.ini` | Tras medir, `MaxClientRate`/`MaxInternetClientRate` = 100000 en `SteamSocketsNetDriver` como tope de seguridad (≤ 700 KB/s de subida total) |
| `Config/DefaultEngine.ini` | Nada de `TotalNetBandwidth` (no tiene efecto en 5.6), ReplicationGraph ni Iris |

### Código

| Fichero | Cambio |
|---|---|
| `Source/Tortunabo/Tortunabo.Build.cs` | Añadir `"Voice"` a `PrivateDependencyModuleNames` (encoder y decoder Opus) |
| `Source/Tortunabo/Private/Voice/ProximityVoiceComponent.cpp` y `.h` | Cambiar `CompressSamples`/`DecompressSamples` por `IVoiceEncoder`/`IVoiceDecoder` Opus a 16 kHz y 24 kbit/s. Añadir un byte de versión de códec al paquete para rechazar el formato viejo. Tope de voces por oyente, canal de equipo y de espectador, y descarte de la voz si la conexión está saturada (`IsNetReady`) |
| `Source/Tortunabo/Public/Settings/TN_SettingsSaveGame.h:57` | `bPushToTalk = true` por defecto |
| `Source/Tortunabo/Private/Player/MP_GamePlayerController.cpp:274` | `ClientReceiveVoice`: registrar las voces descartadas por `SpeakerActor` nulo (la voz del buggy en el Rally) |
| `TN_EnemySeagull.cpp`, `TN_InkProjectile.cpp`, `TN_QuadActor.cpp`, `TN_SeagullDroppingActor.cpp` | `SetNetUpdateFrequency` a 15–20 Hz y `SetMinNetUpdateFrequency(5)`; revisar si `bAlwaysRelevant` es necesario |
| Nuevo `Source/Tortunabo/Private/Net/` (subsistema) | Telemetría `LogTNNet`: bytes de salida por conexión cada segundo a CSV, aviso si el anfitrión supera 250 KB/s, y hash de desincronía |
| Nuevo comando `TN.Voice.FakeTalk` | Inyecta un tono o ruido en `CaptureBuffer` para medir la voz con bots |

## 7. Tareas para el roadmap

| # | Tarea | Horas |
|---|---|---|
| 1 | Voz Opus (`Build.cs` + componente + byte de versión + test de ida y vuelta del códec) | 6–8 |
| 2 | Pulsar para hablar por defecto y ajuste de la puerta de umbral | 1 |
| 3 | Reglas de reenvío: tope de voces por oyente, canal de equipo 2 vs 2, canal de espectador, silenciar a un jugador, descarte por saturación | 8–10 |
| 4 | Auditoría de frecuencia y relevancia de los actores siempre relevantes, y medición con `stat net` | 3–4 |
| 5 | Config `IpNetDriver` igual que Steam + scripts para lanzar 8 instancias con emulación | 2–3 |
| 6 | `TN.Voice.FakeTalk` | 2–3 |
| 7 | Cerrar `TN.Monkey` (comando y subsistema) y `TN.Stress race8` | 8–12 |
| 8 | Telemetría `LogTNNet` + aviso de subida del anfitrión | 3–4 |
| 9 | Detector de desincronía (parte de red del sistema de bugs de `00-Arquitectura`) | 8–10 |
| 10 | Verificar la voz del conductor en el Rally (voz en el pawn buggy, relevancia) | 2 |
| 11 | Sesiones de prueba: PIE a 8, 8 locales, máquinas virtuales y playtest Steam con 8 personas (2 sesiones) | 10–12 |
| **Total** | Sin contar la opción E del Rally, ya presupuestada en 20–30 h en `B_buggy_sync.md` (eliminado) | **53–69 h** |

Orden recomendado: tareas 1, 2 y 5 primero (unas 10–12 h). Esas tres bastan para pasar de «no cabe» a «cabe» en el pico. Después la 4 y la 3, y al final la verificación (6–11).
