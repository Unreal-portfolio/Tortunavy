# Fantasma espectador y volver a la vida desde un huevo

Quien está de espectador es un **fantasmita de tortuga**, en todos los modos: muerto en el cooperativo; en la carrera
(donde no se muere nunca), quien ya ha llegado a la meta o no corre el sprint final; y en el lobby, solo para probar. El
espectador ve a toda la gente y cambia entre ellos, mantiene la interfaz de la tortuga que sigue, se ve en su HUD como
el icono de la tortuga fantasma y elige cámara libre (orbita alrededor de la tortuga) o fija (la vista del propio
jugador seguido). Los demás lo ven flotando justo donde está su cámara, mirando a la tortuga que sigue, con la colita
al viento.

**Volver a la vida desde un huevo** (`TNGhost::ReviveIntoEgg`) es del **cooperativo**: ahí se revivirá en los nidos (en
el futuro, la torre de nidos de huevos), que aún no existen. En la carrera no se usa (los finalistas del sprint salen de
los huevos de salida normales) y `TN.Ghost.Revive` no hace nada en ella. Todo está en código: mallas generadas en
ejecución, interfaz en C++ y sonidos sintetizados; sin assets nuevos.

## Piezas

| Pieza | Archivo | Qué hace |
|---|---|---|
| Contrato `TNGhost` | `Public/Player/TN_Ghost.h` | `ReviveIntoEgg`, `OnHatched`, `IsReviving`, `IsGhost` (sin cambios de interfaz). |
| `ATN_SpectatorGhost` | `Player/TN_SpectatorGhost.*` | El fantasma: uno por espectador, replicado y con el PlayerController de dueño. Malla, vuelo al huevo, envío de la cámara, controles y enganche de las cámaras y del cartel. También `TNGhost::GetHUDSubject` e `IsGhostPlayer` (para el HUD). |
| `UTN_GhostCameraModifier` | `Player/TN_GhostCameraModifier.*` | Cámara libre (órbita) y fija, con mezcla suave, y la cámara del vuelo al huevo. |
| `ATN_GhostEgg` | `Player/TN_GhostEgg.*` | El huevo de `ReviveIntoEgg`: sale del suelo, vibra «pum, pum, pum», eclosiona y se hunde. |
| `TN_Ghost.cpp` | `Private/Player/TN_Ghost.cpp` | La implementación del contrato, los enganches del PlayerController y la consola `TN.Ghost.*`. |
| `TN_GhostInternal.h` | `Private/Player/` | Enganches internos (no son contrato). |
| `TN_GhostMeshes.h` | `Private/Player/` | La malla del fantasmita. |
| `UTN_GhostHUDWidget` | `UI/HUD/TN_GhostHUDWidget.*` | El cartel del fantasma y «Te mira...» para los que tienen tortuga. |
| `UTN_GhostHatchWidget` | `UI/HUD/TN_GhostHatchWidget.*` | La transición de la cáscara oscura en la pantalla del que vuelve; en la carrera, también la de la llegada al agua y la del paso entre rondas (modo `ShowCurtain`, ver «La cáscara oscura en la carrera»). |
| `TNHUDGhostFace` | `Private/UI/HUD/TN_HUDGhostFace.h` | La cara de tortuga fantasma del HUD (el HUD no tenía icono de fantasma: es nueva, con el estilo de `TN_HUDFaces.h`). |

Toques en código compartido (pequeños y agrupados):

- `AMP_GamePlayerController`: `EnterSpectateMode` llama a `TNGhostInternal::OnEnterSpectate` (crea el fantasma y apunta
  la tortuga que deja); `OnPossess` llama a `TNGhostInternal::OnPossess` (el fantasma se desvanece);
  `SpectateByDirection` deja cambiar de tortuga también a un fantasma aunque su PlayerState diga que está vivo (lobby).
- `UTN_PlayerHUDWidget`: el inventario sigue al ViewTarget, como ya hacía la energía.
- `UTN_RunHUDWidget` y `UTN_RunFlowHUDWidget`: distintivo, pista, puntos, avisos y tripulación enseñan la tortuga de
  `TNGhost::GetHUDSubject` (la propia o, de fantasma, la seguida); los fantasmas salen en la tripulación con su cara de
  fantasma, flotando.

## Qué ve cada uno

**El espectador (en su pantalla):**

- La interfaz de la tortuga que sigue, como si fuera la suya: salvavidas de energía con su cara y su nombre, sus dos
  objetos, su puesto en la pista del mar, su contador de conchas (lo que sube sin aviso de concha vuela desde la
  tortuga), sus avisos (tormenta, panza arriba, reanimando) y sus bocadillos del chat rápido.
- En la tripulación (izquierda), todos menos la tortuga seguida: él mismo con la cara de fantasma que flota, y los demás
  fantasmas también con ella.
- Abajo a la derecha, su icono de tortuga fantasma (flota y se mece) con su nombre en una cinta y un cartel:
  «¡Eres un fantasma!», «Mirando a *Nombre*», la cámara que lleva («Cámara libre: gira alrededor de la tortuga» o
  «Cámara fija: lo que ve *Nombre*»), los controles (los del ratón y teclado o los del mando, según lo último que haya
  tocado) y «También miran: ...» si hay más fantasmas mirando a la misma tortuga. Queda por encima de la esquina donde
  el menú de ajustes pone el contador de FPS.
- No ve su propio fantasma (flota justo en su cámara), salvo en el vuelo al huevo.
- Si la tortuga que sigue deja de valer (llega, cae, se va, se oculta o pasa a fantasma), a los 0,35 s pasa sola a la
  siguiente. Si no queda nadie, «No queda nadie a quien mirar» y la cámara libre orbita el último sitio.

**Los demás jugadores:**

- Un fantasmita de tortuga (~1,1 m de la nariz a la cola) flotando donde está la cámara del espectador, mirando a la
  tortuga que sigue: cabeza, cuello, caparazón con escudos, peto, dos aletas que baten, coloretes, ojitos oscuros con
  brillos y boquita, y en vez de patas traseras una cola que sale de debajo del caparazón, adelgaza, se enrosca hacia
  arriba y la recorre una onda (Casper). Blanco azulado y translúcido (`M_ProcFXCloud`, borde difuminado; ojos con
  `M_ProcFXSoft`), sube y baja y se balancea.
- Sin colisión, sin sombra. Aparece con un fundido; se desvanece si la cámara de ese jugador pasa a menos de 2,3 m y no
  se ve a menos de 0,7 m (así no tapa la vista: con la cámara fija el fantasma flota justo en la cámara del jugador
  seguido, que no lo ve). Nunca más lejos de 11 m de la tortuga que sigue.
- Con tortuga, abajo a la izquierda encima del distintivo: «Te mira *Ana*» / «Te miran *Ana* y *Leo*» con el icono de
  fantasma.

## Controles del espectador

| Acción | Teclado y ratón | Mando |
|---|---|---|
| Cambiar de tortuga | ← / → (y Re Pág / Av Pág, como antes) | LB / RB, cruceta ← / → |
| Cámara libre ⇄ fija | C | R3 (pulsar el stick derecho) |
| Girar la cámara libre | Ratón | Stick derecho |
| Acercar / alejar la cámara libre | Rueda | Gatillo derecho / izquierdo |
| Rueda con la cámara fija | Cambia de tortuga (como antes) | — |

La cámara libre usa la sensibilidad y el eje Y invertido del menú de ajustes (`UTN_GameSettingsSubsystem::
GetLookSensitivity` e `IsLookYInverted`, del ratón o del mando). La elección libre o fija se recuerda para la próxima vez
que se sea fantasma (por jugador local, mientras dure el juego). Con el cursor a la vista (una rueda, el menú de pausa)
la cámara no gira.

Los controles son un `UInputComponent` propio (prioridad 5) que el fantasma mete en la pila del PlayerController local
al empezar y saca al acabar (como el menú de pausa, que va por encima con prioridad 100).

## Cámaras

- **Fija**: el ViewTarget del PlayerController es la tortuga seguida (como antes; el HUD y el cambio de jugador lo usan),
  así que se ve su propia cámara (brazo, retraso, colisión y FOV) con la rotación de su jugador. En un cliente esa
  rotación llega por `TargetViewRotation`: el servidor pone también la tortuga como ViewTarget de ese PlayerController
  (`ATN_SpectatorGhost::ApplyServerFollow`); el cliente elige solo a quién sigue (`bClientSimulatingViewTarget`, que se
  quita al dejar de ser fantasma).
- **Libre**: `UTN_GhostCameraModifier` sustituye la vista por una órbita alrededor de la tortuga (a 55 cm sobre su
  centro), de 1,7 a 9 m (4,2 al empezar), con el cabeceo entre -70° y +30°. Tantea con una esfera de 14 cm por el canal
  de cámara (no atraviesa paredes ni el suelo) y además nunca queda a menos de 40 cm del suelo que tiene debajo. Al pasar
  de la fija a la libre la órbita arranca donde estaba la cámara, sin salto; el cambio entre las dos es una mezcla de
  0,45 s. Al cambiar de tortuga, el punto al que mira se desliza hasta la nueva.
- El modificador va antes que los temblores (prioridad 0), y su clase no se llama «...Shake...» para que el ajuste de
  temblor no lo apague.

## Red

- **Quién crea el fantasma**: el servidor, en `EnterSpectateMode` (muerte en el cooperativo, meta en la carrera, o
  `TN.Ghost.Become`), con el PlayerController de dueño; apunta la tortuga que deja (oculta o panza arriba). Se replica a
  todos siempre (`bAlwaysRelevant`, 15 veces por segundo) sin movimiento replicado: viajan su PlayerState, su fase
  (espectador, volviendo, se va), la vista y los datos de la vuelta a la vida.
- **La cámara**: el dueño manda al servidor dónde está su cámara y a quién sigue (`ServerUpdateView`, no fiable) como
  mucho 12 veces por segundo, en cuanto cambia de tortuga, y cada 0,5 s aunque no se mueva. El servidor la limita a 11 m
  de la tortuga y la replica a todos menos al dueño (`COND_SkipOwner`); cada máquina la suaviza. En el anfitrión no hay
  envío.
- **Lo que le llega al espectador**: el servidor decide qué es relevante para un espectador desde su peón de espectador
  (`LastSpectatorSyncLocation`); el dueño lo lleva a su cámara cada fotograma, así recibe lo que pasa donde mira aunque
  esté lejos de donde se hizo fantasma.
- **Se acaba**: al poseer una tortuga (`OnPossess`: salida del huevo, rescate del cooperativo, ronda nueva) pasa a «se
  va», se desvanece en 0,4 s y se destruye. Si su jugador se va, el servidor lo destruye. En un viaje desaparece con el
  mundo; al acabar, el dueño saca sus controles, su modificador de cámara y el `bClientSimulatingViewTarget`.

## Volver a la vida desde un huevo (cooperativo)

```cpp
// Servidor. EggTransform: apoyado en el suelo; su X es hacia donde mirará la tortuga al salir.
TNGhost::ReviveIntoEgg(PC, EggTransform);
TNGhost::OnHatched().AddLambda([](APlayerController* PC, APawn* Pawn) { /* reglas del modo: vivo otra vez, etc. */ });
TNGhost::IsReviving(PC);  // del vuelo del fantasma a la salida del huevo
TNGhost::IsGhost(PC);     // espectador ahora (volviendo incluido)
```

- **Devuelve false** si PC es nulo, no hay autoridad, no hay GameMode o ya está volviendo.
- **Si es un fantasma**: vuelo en U de 1,2 s (sube, arquea por encima del huevo y baja en vertical), se mete de cabeza
  (encoge y se apaga al entrar), el huevo vibra 1 s «pum, pum, pum» (tres golpes con su luz cálida por dentro) y
  eclosiona: 2,2 s en total. **Si aún tiene tortuga**: se le quita (pasa a fantasma sin verse) y el huevo aparece, vibra
  y eclosiona (1,4 s).
- **El huevo** (`ATN_GhostEgg`): el mismo que los de la pila (`TNCastleKit::BuildEggCup` y `BuildEggLid`, con el color de
  acento según el jugador), sale del suelo de un saltito, se mece mientras espera, vibra con el fantasma dentro y al
  eclosionar la tapa salta dando vueltas hacia un lado como en la salida con huevos del mapa procedural
  (`ATN_ProcStartStructure`), se posa y se esfuma; la base se queda 1,6 s y se hunde. Los sonidos son los sintetizados
  de la pantalla de carga del huevo (`UTN_EggSynthComponent`): «¡clac!» al salir, soplido al entrar el fantasma, un
  golpe por cada «pum», crujido y «¡pum!» al eclosionar; en el mundo, con distancia, y no en la máquina del que vuelve
  (que los oye en su pantalla).
- **La tortuga** se crea al eclosionar con `AGameModeBase::RestartPlayerAtTransform` dentro del huevo, de pie y mirando
  hacia su X (antes se quitan el «solo espectador» del PlayerState y el estado de espectador, como en
  `RespawnControllerFresh`; si algo tapa el sitio, se prueba 1,2 m más arriba y, si aun así no sale, sigue de fantasma).
  Ningún GameMode del proyecto sobrescribe `RestartPlayer`, `RestartPlayerAtTransform`, `SpawnDefaultPawnAtTransform` ni
  `FinishRestartPlayer`: sale el peón por defecto con los cosméticos que pone `AMP_GamePlayerController::OnPossess`.
  Después: entrada, cámara y giro como al reanimar, el saltito (380 cm/s hacia delante y 620 hacia arriba, el de los
  huevos de la salida; lo lanzan el servidor y su dueño a la vez) y `OnHatched`.
- **Lo que queda de antes**: la tortuga que dejó al hacerse fantasma (oculta o panza arriba) se destruye al eclosionar,
  y su `ATN_RescuePickup` del cooperativo también.
- **Lo que no hace**: no toca las banderas del PlayerState (`bIsAlive`, `bIsEliminated`, `bHasFinishedRun`...) ni las
  listas del GameMode (inmunidad, DBNO, rondas): eso lo decide quien llama, en `OnHatched`. `OnHatched` es global (un
  delegado para todos los mundos): hay que mirar el mundo del PlayerController.
- **Pantalla del que vuelve** (solo en la suya, `UTN_GhostHatchWidget`): la cámara se aparta un poco y mira el vuelo; la
  pantalla se pone negra mientras el fantasma se mete; «¡PUM!» y una cáscara de huevo oscura tapa la pantalla entera,
  con una unión en zigzag por el medio por la que se cuela la luz; con cada «pum» tiembla, se resquebraja con líneas de
  luz dorada y la luz crece; al eclosionar (y en cuanto ya tiene su tortuga; como mucho 2,5 s después) las dos mitades
  salen despedidas entre trozos de cáscara y un fogonazo, y se ve a su tortuga saliendo del huevo de un saltito.
  Reutiliza el blanco, los trozos de cáscara y los sonidos de la pantalla de carga del huevo.

## La cáscara oscura en la carrera (ronda 4)

La misma cáscara (`UTN_GhostHatchWidget`) hace de transición en la carrera, en un modo propio que no cambia la de volver a
la vida (`ShowFor` sigue igual). La maneja `UTN_RaceScreensSubsystem` (detalle en `Docs/Modo_Carrera.md`, «Llegada al agua»
y «Entre ronda y ronda»):

- `UTN_GhostHatchWidget::ShowCurtain(PC, SegundosDeCierre, EsperaMáxima)`: sin fundido a negro ni «¡PUM!»; las dos mitades
  entran desde fuera de la pantalla, desde arriba y desde abajo, cada vez más deprisa (se ve la partida por la rendija) y
  se juntan con un «¡clac!» y una sacudida; cerrada, la unión brilla. Devuelve el widget (ZOrder `ViewportZOrder` = 50).
- `Knock()`: un «pum» desde dentro (tiembla, golpe y crujido); los tres primeros abren una grieta de luz y la luz crece.
- `Open(true)`: se rompe como al eclosionar (fogonazo, mitades despedidas y trozos); `Open(false)`: se funde en 0,25 s. Se
  quita sola al acabar. Si nadie la abre en la espera máxima, se rompe sola.
- `IsClosed()` (las mitades ya tapan toda la pantalla) e `IsOpening()`.
- **Llegada al agua**: se cierra en 0,28 s según se zambulle la tortuga y encima va «Has quedado X.º»; al romperse, quien
  ha llegado ya es fantasma espectador (el servidor la pasa a espectadora a los 0,8 s, tapado) y ve a las que siguen
  corriendo; si ya no queda nadie, se rompe sobre el recuento.
- **Entre rondas**: se cierra al irse el recuento con «RONDA N» encima, da un «pum» por cada número del 3, 2, 1 y se
  rompe al dar la salida. Detrás, el fantasma se va (la tortuga nueva ya está en su huevo) sin que se vea el cambio.

## Consola (pruebas)

- `TN.Ghost.Become [índice]`: ese jugador pasa a fantasma espectador (0 = anfitrión, 1 = el primer cliente...; sin
  índice, el anfitrión). Su tortuga se queda quieta y oculta (como al llegar a la meta) hasta que vuelva a la vida.
  Sirve en cualquier modo, también en el lobby.
- `TN.Ghost.Revive [índice]`: vuelve a la vida a ese jugador en un huevo 4,5 m delante de la primera tortuga viva de
  otro jugador (si no hay, delante de la suya), en el suelo y mirando como ella. Sin índice: el primer fantasma. Si es
  un muerto del cooperativo, antes lo deja vivo en su PlayerState (solo esta orden de prueba). En la carrera no hace
  nada y lo dice en el registro.
- Solo en la consola del anfitrión y fuera de Shipping. En un cliente no hacen nada (lo dicen en el registro): no hay
  RPC de pruebas que un invitado pueda llamar (Plan maestro §4, N-A).
- Registro: categoría `LogTortunabo`, mensajes `[Fantasma]`.

## Cómo probar

PIE con 2 y con 3 jugadores (escucha: anfitrión + clientes). En cada máquina mira también lo que ven los demás.

1. **Mapa procedural, cooperativo** (morir de verdad: sin pila de huevos por delante de la tormenta, o `TN.Ghost.Become 1`).
   - El muerto ve la tortuga del otro con su interfaz (energía, objetos, cara, conchas, pista) y, en la tripulación, a sí
     mismo con cara de fantasma; abajo a la derecha su icono y «Mirando a...».
   - ← / → cambia de tortuga (con 3 jugadores); C pasa de libre a fija y vuelta, sin saltos; ratón y rueda en la libre;
     la libre no atraviesa el suelo ni paredes; la fija enseña lo mismo que ve el otro jugador (mueve su cámara).
   - En la otra máquina: el fantasmita flota donde está la cámara del muerto, mira a la tortuga, mueve la cola y las
     aletas; al acercar tu cámara se desvanece; «Te mira *Nombre*» encima del distintivo.
   - `TN.Ghost.Revive 1`: el fantasma hace la U, se mete de cabeza, el huevo vibra tres veces y sale la tortuga de un
     saltito (en todas las máquinas); en la del que vuelve, negro, «¡PUM!», cáscara oscura que se resquebraja con luz y
     se abre dejando ver su tortuga saliendo; se puede mover enseguida; el cuerpo panza arriba y su rescate desaparecen.
   - Con el anfitrión de fantasma (`TN.Ghost.Become 0` y `TN.Ghost.Revive 0`): lo mismo en la máquina del anfitrión.
2. **Carrera** (llegar a la meta; o `TN.Ghost.Become 1`): el que ha llegado es fantasma y ve a los que siguen corriendo,
   con todo lo anterior. Al llegar al agua, su pantalla pasa por la cáscara oscura con «Has quedado X.º» y se rompe
   dejándole ya de fantasma (sin ver el cambio de cámara). `TN.Ghost.Revive` no hace nada (lo dice el registro). Ronda
   nueva: la cáscara con «RONDA N» tapa el cambio, el fantasma se va y, al romperse, la tortuga sale de su huevo.
   Vistas previas en cualquier mapa: `TN.Race.ArrivalPreview [puesto]` y `TN.Race.RoundPreview [ronda]`.
3. **Lobby**: `TN.Ghost.Become 1` → fantasma con cámaras y cartel; los demás lo ven flotando; `TN.Ghost.Revive 1` →
   sale del huevo delante de la otra tortuga y se mueve normal (y vuelve a poder entrar en la tienda y el probador).
4. **Viaje**: siendo fantasma, acabar la partida y volver al lobby: la cámara y los controles vuelven a los de siempre.

## Pendiente y notas

- Revivir en los nidos del cooperativo (la torre de nidos): llamar a `TNGhost::ReviveIntoEgg` con el sitio del nido y,
  en `OnHatched`, dejar al jugador vivo (banderas del PlayerState, inmunidad, rescate) como hace `RevivePlayer`.
- La interfaz de la tortuga seguida es la del HUD en código (`bUseCodeHUD`); con el HUD de Blueprint solo sigue la
  energía y el inventario.
- La voz de proximidad va con la tortuga: un fantasma no habla por ella.
