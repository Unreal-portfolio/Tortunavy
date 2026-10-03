# Salas públicas y privadas

Una partida de Tortunavy es una **sala**: la sesión de Steam (el subsistema NULL en el editor) que crea el anfitrión al
pulsar «Crear partida». Cada sala tiene un **nombre** gracioso de la playa (al azar de `TNRoomNames`, cada jugador lo lee
en el idioma que ha elegido en el juego), un **código** de 5 caracteres, un **modo** (Cooperativo o Carrera), unas **plazas** (4, 6 u 8, el
anfitrión incluido) y puede ser **pública** (sale en la lista de «Unirse») o **privada** (solo se entra con el código o
por invitación de Steam). El anfitrión puede **cerrarla** (no entra nadie más) y **expulsar** a quien quiera. El cierre,
las plazas y los expulsados los aplica el servidor al entrar (PreLogin), no solo el anuncio.

## Piezas

| Pieza | Archivo | Qué hace |
|---|---|---|
| `UMP_GameInstance` | `Multiplayer/MP_GameInstance.*` | Crea la sala (`HostRoom` → `HostSession`), busca (`RefreshRoomList`, `JoinRoomByCode`, `FindAndJoinSession`), entra (`JoinListedRoom`), cierra y abre (`SetRoomLocked`), expulsa (`KickFromRoom`), rechaza al entrar (`HandleGameModePreLogin`) y mantiene el anuncio y el `ATN_RoomInfo` al día (`RoomTick`, cada segundo). |
| `TN_RoomTypes` | `Multiplayer/TN_RoomTypes.*` | Claves de la sesión (`TNRoomKeys`), códigos (`TNRoomCode`), `FTNRoomConfig`, `FTNRoomListing`, `FTNRoomSnapshot` y los textos de rechazo (`TNRoomText`). |
| `TNRoomNames` | `Multiplayer/TN_RoomNames.*` | Los 242 nombres, en el canal de localización del motor: texto origen en español de España (`NSLOCTEXT("TNRoomNames", "Room_000", ...)`, una clave estable por índice); cada idioma lo adapta en su `Game.po` ([Localización](Localizacion.md)). Las adaptaciones inglesas de antes están en `Tools/Localization/room_names_en.csv`, listas para meterse como traducción al inglés. La sesión anuncia solo el índice. |
| `ATN_RoomInfo` | `Multiplayer/TN_RoomInfo.*` | Actor replicado (siempre relevante) con la sala: nombre, código, privada, cerrada, plazas y anfitrión, y los expulsados. Lo crea la GameInstance del anfitrión en cada mapa; los invitados lo leen para el menú de pausa. |
| `UTN_RoomMenuWidget` | `UI/Menu/TN_RoomMenuWidget.*` | Pantallas «Crear partida» y «Unirse» del menú principal, y el aviso de arriba al volver al menú. |
| `UTN_RoomCodeField` | `UI/Menu/TN_RoomMenuWidget.*` | Campo del código: cinco casillas, mayúsculas automáticas, pegar, teclado, ratón y mando. |
| `UMP_MainMenuWidget` | `UI/Menu/MP_MainMenuWidget.*` | Los botones del Blueprint: «Crear partida» y «Unirse» abren las pantallas de salas; «Salir». |
| `UTN_PauseMenuWidget` | `UI/Pause/TN_PauseMenuWidget.*` | Cabecera con la sala y página «Sala»: código, cerrar y abrir, invitar y quién está dentro con el «⋮» para expulsar. |
| `TNRoomArt` | `Private/UI/Menu/TN_RoomArt.h` | Iconos pintados en código: «⋮», candado y el grupo de tortugas del botón «Sala». |
| `UTN_TravelFailureSubsystem` | `Multiplayer/TN_TravelFailureSubsystem.*`, `TN_TravelFailureDecisions.h` | Qué pasa cuando un viaje de mapa falla (mapa sin cocinar, paquete que falta): el viaje a un mapa que no existe no empieza, el anfitrión se queda en el lobby o lo recarga con todos y, si aun así falla, el invitado vuelve al menú con el aviso. Ver [Viaje de mapa fallido](#viaje-de-mapa-fallido). |

## Ajustes de la sesión

La sesión anuncia estos ajustes (`EOnlineDataAdvertisementType::ViaOnlineService`). Con Steam se guardan como datos del
lobby con el sufijo del tipo (`ROOMCODE_s`, `PRIVATE_i`...).

| Clave | Tipo | Qué es |
|---|---|---|
| `SEARCH_KEYWORDS` | texto | `TortunaboLobby`: separa nuestras salas de las de otros proyectos (el AppId 480 de pruebas es compartido). |
| `ROOMNAME_ID` | número | Índice del nombre en `TNRoomNames` (nunca el texto: cada uno lo lee en el idioma elegido en el juego; el índice y el orden de la tabla no cambian nunca). |
| `ROOMCODE` | texto | Código de 5 caracteres. Lo tienen todas las salas; solo se enseña en las privadas (y en la página «Sala»). |
| `PRIVATE` | número | 1: privada. |
| `LOCKED` | número | 1: cerrada por el anfitrión. |
| `MODE` | número | `ETNProcGameMode` (lo que eligió el anfitrión; si lo cambia el general del lobby, se vuelve a anunciar). |
| `PLAYERS` | número | Tortugas dentro, el anfitrión incluido. |

Además: `NumPublicConnections` = plazas de la sala; `bAllowJoinInProgress`, `bAllowJoinViaPresence` y `bAllowInvites` a
`true` siempre, **también en las privadas y en las cerradas**:

- Un lobby privado de Steam no sale en ninguna búsqueda, así que la búsqueda por código no lo encontraría. Por eso todas
  son lobbies públicos y la lista deja fuera las privadas con `PRIVATE = 0`.
- Un lobby «no unible» tampoco sale en las búsquedas; así una sala cerrada sigue saliendo en la lista, con su candado. El
  cierre lo aplica el servidor al entrar.
- Steam sí esconde solo los lobbies **llenos** (los marca como no unibles): con Steam una sala llena no sale en la lista
  ni la encuentra el código. Con el NULL sí salen, marcadas «llena».

El anfitrión vuelve a anunciar la sesión (`UpdateSession`) al cerrar o abrir la sala y, desde `RoomTick`, cuando cambia
el número de tortugas o el modo (solo si cambia algo).

## Crear

«Crear partida» abre la pantalla de crear con lo último que se eligió en esta ejecución y un nombre y un código nuevos
(`MakeRoomDraft`):

- **Modo**: Cooperativo o Carrera (`TNLobbyMission::MenuModes`, con su explicación abajo).
- **Sala**: Pública o Privada.
- **Plazas**: 4, 6 u 8 tortugas (sin pasar de `MaxPlayers` de `DefaultGame.ini`, 8).
- **Nombre de la sala**: al azar; «Otro nombre» (Intro, A o clic) saca otro distinto (`TNRoomNames::Random`).
- **Código de la sala** (solo en la privada): 5 caracteres de `ABCDEFGHJKMNPQRSTUVWXYZ23456789` (sin O, 0, I, 1 ni L);
  Intro, A o clic lo copian.

«Crear partida» guarda la elección (`RememberRoomDraft`) y llama a `HostRoom`: el modo va a `SelectedProcMode`, se
vacían las listas de miembros y expulsados de la sala anterior, se crea la sesión y se viaja al lobby con la pantalla de
carga de siempre. `HostSession` y `HostSessionWithMode` (Blueprint) siguen funcionando: sin sala elegida crean una pública
con nombre y código al azar.

Las plazas de la sala son también las del marcador «Sala: X/Y» del lobby: `ATN_HQGameMode` lee
`UMP_GameInstance::GetMaxPlayers()`, que en el anfitrión con sala devuelve las de su sala (las mismas que aplica el
PreLogin) y, si no, el tope de `DefaultGame.ini`.

## Buscar y unirse

«Unirse» abre la pantalla de unirse:

- **Con código**: el campo de cinco casillas y «Entrar». Se escribe directamente (las minúsculas pasan a mayúsculas; una
  O, 0, I, 1 o L avisa de que los códigos no las llevan), Retroceso y Supr borran, ← → cambian de casilla, Ctrl+V,
  Mayús+Insert, clic derecho o «Pegar» pegan (de «Código: K7M2P» saca «K7M2P») e Intro entra. Con mando: A empieza a
  escribir; ↑ ↓ cambian la letra, ← → la casilla, X borra y A o B terminan. La rueda del ratón sobre una casilla también
  cambia su letra.
- **Partidas públicas**: la lista (hasta 200 salas) con el nombre en el idioma que has elegido, el modo y el anfitrión, «3/4» y si está
  cerrada (candado, en coral) o llena. Primero las que tienen sitio y, entre ellas, las más llenas. Se busca al abrir la
  pantalla, cada 20 s mientras está abierta y con «Actualizar» (F5 o Y del mando). Intro, A o clic en una sala: entrar.

Búsquedas (`StartRoomSearch`; solo una a la vez, la siguiente espera su turno y a los 25 s sin respuesta se da por
fallida). En la cola solo cabe una y la ocupa la más importante (`TNRoomSearchRules`): «unirse a la primera», después
el código y, por último, la lista, que se repite sola y no pisa a un código que espera respuesta (#73). Mientras se crea
una sala o se entra en otra no se busca nada más:

| Búsqueda | Filtros en el servidor de Steam | Filtro aquí (también con el NULL) |
|---|---|---|
| Lista y «unirse a la primera» | `SEARCH_KEYWORDS = TortunaboLobby`, `PRIVATE = 0` | Palabra clave y sin las privadas |
| Código | `SEARCH_KEYWORDS = TortunaboLobby`, `ROOMCODE = <código>` | Palabra clave y el mismo código |

Antes de intentar entrar se mira el anuncio: si la sala está cerrada o llena, se dice sin intentarlo. Mensajes:

| Caso | Mensaje |
|---|---|
| Código incompleto | «El código tiene 5 letras y números (nunca lleva O, 0, I, 1 ni L).» |
| Código que no existe | «No hay ninguna sala con el código K7M2P. Revisa el código (con Steam, una sala llena tampoco aparece).» |
| Cerrada (anuncio o servidor) | «La sala está cerrada: el anfitrión no deja entrar a nadie más.» |
| Llena (anuncio, Steam o servidor) | «La sala está llena: no queda sitio para otra tortuga.» |
| Expulsado (servidor) | «El anfitrión te ha expulsado de esta sala: no puedes volver a entrar.» |
| La sala ya no existe | «Esa sala ya no existe: puede que el anfitrión se haya ido. Actualiza la lista.» |
| Sin Steam | «No hay conexión con Steam: ábrelo y vuelve a intentarlo.» |

Si el rechazo llega del servidor al conectar, el motor vuelve a cargar el menú: el aviso lo guarda la GameInstance
(`PendingMenuNotice`) y el menú nuevo lo enseña abriendo otra vez «Unirse».

## En la sala: cerrar, abrir y expulsar

Menú de pausa (todos los mapas: lobby, mapa procedural, carrera):

- **Cabecera**: `«Tortugas al horno» de Mokius · privada · código K7M2P · 3/4 tortugas · cerrada` (el código solo si
  es privada; «cerrada» solo si lo está).
- **Portada**: botón **Sala** en las partidas en red.
- **Página «Sala»**: nombre y si es pública o privada; el código (Intro, A o clic: copiarlo); **Entrada: Abierta /
  Cerrada** (el anfitrión; los invitados la ven como texto); «Invitar a amigos de Steam» (con Steam); y **tortugas en la
  sala** con su ping o «Anfitrión». El anfitrión ve un «⋮» en los demás: Intro, A o clic → opciones → «Expulsar» →
  «¿Expulsar a X?» → «Sí, expulsar». La lista se rehace sola si alguien entra o sale o la sala se cierra.

Cerrar (`SetRoomLocked`): no entra nadie nuevo, ni por la lista, ni con el código ni por invitación. Los que ya están
siguen y **pueden volver a entrar** si se les cae la conexión (la GameInstance apunta el id de cada jugador que entra en
`RoomMemberIds` y a los miembros no se les aplica el cierre ni el tope de plazas). Se anuncia al momento.

Expulsar (`KickFromRoom`): el servidor apunta su `PlayerId` en `ATN_RoomInfo`; su cliente lo ve al llegarle la réplica
y se va solo al menú principal con el aviso «El anfitrión te ha expulsado de la sala «X».». Si en 2,5 s sigue conectado
(réplica perdida, cliente colgado), el servidor lo echa con `AGameSession::KickPlayer`. Su id queda en `KickedRoomIds`:
no puede volver a entrar mientras dure la sala (PreLogin: «TNRoom:Kicked»).

## Servidor: rechazo al entrar

`FGameModeEvents::GameModePreLoginEvent` (desde la GameInstance, sin tocar ningún GameMode; solo actúa en el servidor de
su propia GameInstance, que en el editor hay una por ventana). En orden:

1. Expulsado de esta sala → `TNRoom:Kicked`.
2. Sala cerrada y no es miembro → `TNRoom:Locked`.
3. No es miembro y ya hay tantas tortugas como plazas (`GetNumPlayers + GetNumSpectators`) → `TNRoom:Full`.

El texto viaja como fallo de red (`NMT_Failure` → `PendingConnectionFailure`) y el cliente lo cambia por el mensaje en su
idioma (`TNRoomText::RefusedMessage`: `FText` de la localización, «TNRooms», así que sale en el idioma elegido en el juego y se
traduce con el resto), deja la sesión y vuelve al menú. Con el viaje sin cortes del juego (lobby ↔
partida) nadie vuelve a pasar por el PreLogin; solo quien se reconecta tras perder la conexión.

## Viaje de mapa fallido

`UTN_TravelFailureSubsystem` atiende los viajes de mapa fallidos (mapa sin cocinar, paquete que falta, URL mala). Sin él, un
`ServerTravel` fallido dejaba al invitado colgado en la pantalla de carga y sin mensaje. Le llegan por dos caminos:

- **Antes de viajar.** `ATN_HQGameMode` y `ATN_RunGameMode` (los que viajan sin cortes) comprueban en `CanServerTravel` que el
  mapa existe (`UTN_TravelFailureSubsystem::CanServerTravelTo`, como lo mira el motor). Si no existe, el viaje no empieza: los
  invitados no reciben el `ClientTravel` al mapa que falta (antes fallaban también y volvían al menú por su cuenta) y el fallo
  se atiende sin pasar por `UEngine::OnTravelFailure`, que lo trataría como una desconexión.
- **`UEngine::OnTravelFailure`**: lo que se escapa de esa comprobación (un mapa que existe pero no carga, un viaje duro).

La regla es pura (`TNTravel::DecideTravelFailure`, tests `Tortunabo.Net.TravelFailure`):

| Quién y dónde | Qué pasa |
|---|---|
| Anfitrión, primer fallo, fuera del lobby o en un lobby que ya había empezado la cuenta atrás | **Al fotograma siguiente**, `ServerTravel` al lobby del que salió (`LobbyReturnMapPath`); la pantalla de carga dice «No se ha podido cargar la partida: volvéis al lobby.». Si el viaje no arranca, se oculta la pantalla de carga y va al menú. |
| Anfitrión en un lobby en pie (sin cuenta atrás ni pausa antes de viajar) | No viaja: no hay a dónde volver. Se quita la pantalla de carga y se queda donde está, con su sesión y sus invitados. |
| Invitado, partida sin red o segundo fallo seguido | Sesión cerrada y al menú, con «No se ha podido cargar la partida y has vuelto al menú.». |
| Ya en el menú | Solo el aviso; se cierra la sesión de Steam que quedara. |

- **Por qué al fotograma siguiente.** En el viaje sin cortes (el de lobby y partida) el motor avisa del fallo *dentro* de
  `ProcessServerTravel`, con `World->NextURL` aún lleno (se vacía al volver), y `UWorld::ServerTravel` no hace nada si
  `NextURL` no está vacío, pero devuelve `true`. Por eso el subsistema espera al fotograma siguiente y luego comprueba el efecto
  (`TNTravel::DidTravelStart`: viaje sin cortes en marcha o `NextURL` puesto), no el valor devuelto.
- **El motor ya manda al anfitrión al menú.** Si el fallo llega por `UEngine::OnTravelFailure`, `UEngine::HandleTravelFailure`
  lo atiende *después* que el subsistema (se enlazó antes y `Broadcast` recorre los delegados del último al primero): llama a
  `HandleDisconnect`, que deja pedido un viaje a `?closed` (la entrada por defecto, el menú) para el tick siguiente y le quita
  `?Listen` a la última URL; su `LoadMap` cancelaría hasta un viaje sin cortes recién arrancado. Por eso, cuando el anfitrión se
  queda en su sesión, el subsistema espera al principio del fotograma siguiente (`FCoreDelegates::OnBeginFrame`, antes del
  `TickWorldTravel` siguiente), anula ese viaje (solo si es `?closed`) y devuelve el `?Listen` (`KeepHostInSession`). Hecho en
  el acto, el motor lo deshacía justo después: el anfitrión acababa en el menú y los invitados veían que se había cortado la
  conexión.
- **Un lobby «en pie».** `ATN_HQGameMode::BeginMatchTravel` destruye las tortugas *antes* de pedir el viaje y el castillo no les
  quita el «listo» ([Pantalla de carga](Pantalla_Carga.md)): tras un lanzamiento fallido el lobby no se puede jugar, así que
  se recarga con el mismo `ServerTravel` con que se vuelve de una partida. Solo un lobby que no lanzaba nada se deja como está.
- **Viaje duro** (PIE sin `net.AllowPIESeamlessTravel 1`, o `?NoSeamlessTravel`): el anfitrión **no** vuelve al lobby, acaba en
  el menú. Cuando el motor avisa ya ha cerrado el driver de red (los invitados pierden la conexión), destruido el
  mundo y los controladores y cargado el menú, y `HandlePostLoadMap` ya ha olvidado la sala (`ResetRoomState`). Volver al lobby
  sería alojar una sala nueva desde el menú, sin los datos de la sala y con la carrera por el puerto de escucha de Steam de
  `OnCreateSessionComplete`: no se hace. En el juego empaquetado el lobby y las partidas viajan sin cortes, así que este
  camino solo lo recorren PIE y el viaje del menú al lobby.
- **Los invitados** no reciben un viaje a un mapa que no existe (se para en `CanServerTravel`): se quedan en el lobby o siguen
  al anfitrión cuando lo recarga. Si el fallo es de los que llegan por `UEngine::OnTravelFailure` (ya se les había mandado el
  viaje), fallan igual y vuelven al menú por su cuenta, con el aviso, mientras el anfitrión recarga el lobby; si el mapa
  sí les carga, el `ServerTravel` al lobby del anfitrión cancela el suyo y lo siguen.
- **Prueba:** `TN.Travel.Fail` en el anfitrión (con un invitado conectado): en el registro del anfitrión, un solo
  `[MP] Fallo de viaje PackageMissing ... (fallo 1 seguido)`, ningún `LoadMap` a `LVL_Menu` y el invitado sigue en el lobby.

## Detalles

- **Sala por defecto**: un servidor escucha sin sala elegida (p. ej. «Play As Listen Server» del editor) usa una pública
  con nombre y código al azar y 8 plazas (`EnsureActiveRoom`), así que cerrar y expulsar también se prueban en el editor.
- **Una operación a la vez** (#73): crear la sala, entrar en otra, cerrar la sesión vieja para hacerlo o viajar al mapa
  (`FTNRoomOpState`, `UMP_GameInstance::RoomOp`). Mientras dura, otro «Crear», «Unirse», código, «unirse a la primera» o
  invitación de Steam no hace nada (antes destruía la sesión que se estaba creando). Si Steam no contesta en 30 s se
  deshace con el aviso de siempre («No se ha podido crear la sala…» o el de entrar); el viaje tiene 120 s. Se libera al
  contestar Steam, al rechazar el servidor o al cargar el mapa.
- **Al volver al menú** (`HandlePostLoadMap` en `LVL_Menu`) se olvida la sala (`ResetRoomState`).
- **El anfitrión se va**: los invitados vuelven al menú con «Se ha acabado la partida: el anfitrión se ha ido o se ha
  perdido la conexión.».
- **Invitaciones de Steam**: siguen igual (`InviteFriends`, `OnSessionUserInviteAccepted`); valen también en las
  privadas; el cierre y la expulsión los aplica el servidor.
- **Portapapeles**: `FPlatformApplicationMisc` (módulo `ApplicationCore`, en `Tortunabo.Build.cs`).

## Qué probar

### En el editor (subsistema NULL)

1. **Play As Listen Server** con 2 o 3 jugadores. En el anfitrión, Esc → **Sala**: nombre, «Pública», código, Entrada
   «Abierta», las tortugas con «Anfitrión» y el ping. La cabecera de la pausa dice ««…» de … · pública · 3/8 tortugas».
   En un invitado, la misma cabecera y la página «Sala» sin «Entrada» editable ni «⋮».
2. Anfitrión: **Entrada → Cerrada**. La cabecera de todos pasa a «· cerrada» (en el invitado, en menos de un segundo).
3. Anfitrión: «⋮» de un invitado → «Expulsar» → «Sí, expulsar». Ese invitado vuelve al menú principal con el aviso
   arriba; los demás siguen. La lista de la página «Sala» se actualiza.
4. **Standalone con dos procesos** (o dos ventanas del juego empaquetado sin Steam): en uno, «Crear partida» → Privada, 4
   plazas, «Otro nombre» un par de veces, «Copiar» el código → «Crear partida». En el otro, «Unirse»: la sala privada **no**
   sale en la lista; Ctrl+V en el campo (o «Pegar») → «Entrar» → entra. Con una pública, sale en la lista con «1/4» y se
   entra desde ella.
5. En el campo del código: escribir en minúsculas (pasan a mayúsculas), una «O» (avisa), Retroceso, flechas, clic en una
   casilla, rueda del ratón; con mando, A → ↑ ↓ ← → X → B.
6. Viaje fallido (Play As Listen Server, 2 jugadores; en la consola del anfitrión `TN.Travel.Fail` pide un `ServerTravel` a un
   mapa que no existe). Antes `net.AllowPIESeamlessTravel 1` para el viaje sin cortes (sin él el viaje es duro):
   - Sin cortes, en el lobby en pie: no viaja, sin pantalla de carga; el registro dice «El lobby sigue en pie». Desde un mapa de
     partida (p. ej. `LVL_Run`): el anfitrión vuelve al lobby (la pantalla de carga dice «…volvéis al lobby.»); el invitado, al
     menú con «No se ha podido cargar la partida y has vuelto al menú.».
   - Duro (sin la consola de arriba): el anfitrión acaba en el menú, sin sala (en Steam, la sesión cerrada); el invitado pierde
     la conexión y vuelve al menú.

### Con dos PC y Steam (AppId 480)

1. PC A: «Crear partida» → Carrera, **Pública**, **4 plazas** → Crear. En el lobby, el marcador «Sala: 1/4».
2. PC B: «Unirse»: la sala sale en la lista con su nombre (en el idioma de B), «Carrera · de A» y «1/4». «Actualizar»
   (y F5 / Y del mando) vuelve a buscar. Entrar desde la lista → lobby; en A, «Sala: 2/4» y la cabecera de la pausa «2/4».
3. PC B vuelve al menú. PC A: pausa → Sala → **Cerrada**. PC B: la sala sale con candado y «cerrada»; al pulsarla, «está
   cerrada» sin intentarlo. Con el código (visible en la página «Sala» de A) → «está cerrada».
4. PC A: **Abierta** otra vez → B entra con el código. PC A: «⋮» de B → Expulsar → B vuelve al menú con «El anfitrión te
   ha expulsado de la sala «…».». B intenta volver con el código → «te ha expulsado… no puedes volver a entrar».
5. PC A: menú principal → «Crear partida» → **Privada, 6 plazas**. PC B: la sala **no** sale en la lista; con el código
   (copiado en A con «Copiar» y pasado por chat) entra. Lobby: «Sala: 2/6».
6. Llena: sala de 4 con 4 tortugas (o bajar `MaxPlayers` a 2 en `DefaultGame.ini` para probar con dos PC y crear de 4:
   las opciones se quedan en «2 tortugas»): un tercero no la ve en la lista con Steam; con el código, «No hay ninguna
   sala…» (Steam esconde las llenas); si llega a conectar, el servidor lo rechaza con «La sala está llena».
7. Invitación de Steam (pausa → Sala → «Invitar a amigos de Steam») a una sala privada: entra. Con la sala cerrada: «La
   sala está cerrada».
8. Viajes: con la sala cerrada, empezar la partida (lobby → carrera o mapa procedural) y volver al lobby: nadie se queda
   fuera; la cabecera de la pausa sigue diciendo «cerrada» y el código es el mismo.
9. El anfitrión cierra el juego a mitad: B vuelve al menú con «Se ha acabado la partida: el anfitrión se ha ido…».
10. Viaje fallido, en la build empaquetada: sin cocinar el mapa del modo elegido (p. ej. `LVL_Run` en el Clásico, o quitarlo
    del `.pak`), todos listos en el lobby. Tras la cuenta atrás, A (anfitrión) vuelve a un lobby nuevo, con su
    tortuga, sin quedarse tras la pantalla de carga («No se ha podido cargar la partida: volvéis al lobby.»); B vuelve al menú
    con «No se ha podido cargar la partida y has vuelto al menú.». La sala de A sigue abierta y B puede volver a entrar.

## Idioma de los nombres y de los avisos

Antes `TNRoomNames::IsSpanish()` miraba la cultura del motor, que en el editor (y en un juego sin datos de localización «es»)
es «en» aunque Windows esté en español, y el nombre salía en inglés. Ahora:

- La tabla es texto origen en español de España en el canal de localización: `Get(Id)` devuelve un `FText` con espacio de
  nombres `TNRoomNames` y clave `Room_%03d` (la prueba `Tortunabo.Multiplayer.RoomNames.Table` comprueba la identidad de los 242).
  El idioma sale del ajuste «Idioma» del menú de pausa (`TNLanguage`), no de la cultura de Windows ni de la del editor: en el juego
  es la cultura del motor y en el editor la previsualización del idioma del juego. Sin traducción, el nombre sale en español.
- `TNRoomNames::GetSource` es el español (registros, localización) y `IsSpanish` sigue al idioma del juego.
- El inglés de antes (adaptaciones con la misma gracia, no traducciones) está en `Tools/Localization/room_names_en.csv`
  (`Namespace, Key, Spanish, English`); la fase de traducción lo usa tal cual como traducción al inglés y
  `Tortunabo.Multiplayer.RoomNames.EnglishCsv` comprueba que cumple las mismas reglas (28 caracteres, sin repetidos) y cuadra con el
  código.
- Los avisos de rechazo (`TNRoomText::RefusedMessage`, «expulsado», «cerrada», «llena») ya eran `NSLOCTEXT` y siguen el mismo
  idioma; el que se guarda en `PendingMenuNotice` es un `FText` y se vuelve a resolver al enseñarse.
