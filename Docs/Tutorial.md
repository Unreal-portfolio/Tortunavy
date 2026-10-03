# Tutorial de la primera partida

Un paseo corto (2-4 min) por dos islas flotantes a **180 m sobre el lobby del castillo** que enseña todos los controles.
Cada jugador lo hace **la primera vez que llega a un lobby en su ordenador**; al acabar cae por una cascada a la plaza del
castillo. Estado: rama `claude/modo-carrera`, 29-09-2026 (sin compilar).

## Cómo es

- **Siempre igual**: no es procedural. El recorrido está diseñado a mano en `TN_TutorialLayout.h` (cotas, anchos, estaciones)
  y la vegetación, los colores y la fauna salen de una semilla fija (`TNTutorial::Seed`). Es idéntico en todas las máquinas y
  en todas las partidas.
- **Aspecto del mapa cooperativo**: pasillo natural con taludes y meseta como los caminos de `World/ProcMap` (material
  `M_ProcTerrain` con la franja del camino, colores de bioma de `DA_ProcMapSettings`, vegetación con `PlaceFloraRows` del
  generador, agua `MI_ProcSeaAnim`, fauna ambiental con las mallas de `ATN_ProcFauna`). Siete biomas seguidos: selva, roca,
  playa, desierto, agua, manglar y ruinas humanas.
- **Dos islas**: la primera (estaciones 1-12) termina en un cañón de 11 m que se cruza con la catapulta (o por un tronco
  caído al lado); la segunda (13-19) acaba en el borde de la cascada.
- **Invisible desde abajo**: cada máquina solo lo dibuja si su cámara está en la zona del tutorial o su jugador está dentro.
  Desde el lobby no se ve nada.
- Cada estación tiene su cartel de madera con el número y el nombre. A la derecha de la pantalla, una tarjeta con la
  estación, lo que hay que hacer con **la tecla o el botón que el jugador tiene puestos** (de `UTN_GameSettingsSubsystem`:
  teclado o mando según lo último que se ha tocado, se refresca cada segundo) y una casilla que se marca al hacerlo. Nada
  bloquea: se puede pasar de largo cualquier estación.

## Estaciones

| N.º | Estación | Bioma | Qué se hace (se marca cuando...) |
|---|---|---|---|
| 1 | ¡Hola, tortuga! | Selva | Moverse (3 m andados) y mirar alrededor (90° de cámara). |
| 2 | Correr | Selva | Correr 1,2 s. Consejo: el salvavidas es la energía. |
| 3 | Saltar | Roca | Saltar (al escalón de 90 cm). |
| 4 | Panzazo | Roca | En el aire, volver a pulsar saltar (hay una zanja para cruzar en plancha). |
| 5 | Coger objetos | Playa | Coger la barrita de energía (sale otra a los 3 s). |
| 6 | Rebuscar | Playa | Mantener junto al montículo de arena y coger la bola que sale (se puede repetir). |
| 7 | La aleta y el caparazón | Playa | Cambiar lo de la aleta por lo del caparazón y soltar lo de la aleta. |
| 8 | Lanzar | Playa | Darle con la bola al cangrejo de prácticas (va de lado a lado, no hace daño y se marea). |
| 9 | Usar objetos | Playa | Usar la barrita (energía sin límite un rato). |
| 10 | El caparazón | Desierto | Meterse en el caparazón, rodar 2,5 m cuesta abajo y salir con la misma tecla. |
| 11 | Trampolín | Desierto | Rebotar en la medusa para subir a la roca de la catapulta. |
| 12 | Catapulta | Desierto | Cruzar el cañón con la catapulta (de pie o en bola; se recarga en 3 s). El tronco cruza sin marcarla. |
| 13 | Nadar | Agua | Cruzar la laguna nadando y salir saltando. |
| 14 | Coger a una tortuga | Manglar | Coger a **Rodolfo** (tortuga de prácticas en su caparazón) y lanzarlo o dejarlo. Con otro jugador, vale el compañero. |
| 15 | ¡Que te cogen! | Manglar | Meterse en el caparazón junto a **Berta**: te coge; moverse 1 s sin parar hasta soltarse (te suelta sola a los 9 s). |
| 16 | Bailes y frases | Ruinas | Abrir la rueda de bailes y la de frases. |
| 17 | Tu voz | Ruinas | Hablar (o pulsar la tecla de hablar; sin micrófono se marca a los 8 s). Explica «Pulsar para hablar». |
| 18 | El menú de pausa | Ruinas | Abrir el menú de pausa (ajustes, controles, accesibilidad; el juego no se para). |
| 19 | ¡Al castillo! | Ruinas | Saltar por la cascada. |

Cada estación cuenta como aprendida al marcar sus casillas (una felicitación corta). Los textos están en `TN_TutorialTexts.h`
(`NSLOCTEXT`, espacio `TNTutorial`).

## El final: la cascada

El arroyo de la última isla cae por el borde. Al pasar el borde cayendo, el servidor y la máquina del jugador hacen lo
mismo: la tortuga cae **recta** (sin velocidad horizontal ni control de movimiento), sin caparazón automático, sin muerte ni
golpe al aterrizar (`SetFallImmuneUntilLanded`). La cascada y la niebla **se desvanecen a mitad de la caída** (entre el 32 %
y el 55 %) y se aterriza en la plaza del castillo, en el centro de los `PlayerStart` del lobby (sitio despejado). Al tocar
suelo vuelve el control y sale el mensaje de despedida.

## Quién lo hace (red)

- **El guardado es de cada ordenador** (`UTN_TutorialSaveGame`, ranura `TutorialState_0`; en el editor, una por ventana de
  PIE: `TutorialState_0_PIE1`, `_PIE2`...). El servidor no guarda nada de nadie.
- `ATN_HQGameMode` coloca el recorrido (`ATN_TutorialCourse`, replicado sin propiedades) al empezar el lobby y engancha a cada
  `PlayerController` un `UTN_TutorialPlayerComponent` (replicado; `bInTutorial` solo al dueño).
- **Dos caminos para entrar**, los dos seguros en Steam y en el editor:
  1. Al unirse a una sala (`UMP_GameInstance::OnJoinSessionComplete`), si ese ordenador no lo ha hecho, la URL lleva
     `?TNTut=1`. El servidor lo lee en `InitNewPlayer` y lo mete en el tutorial en cuanto tiene tortuga: aparece ya arriba.
  2. En cualquier lobby (anfitrión, PIE, vuelta de una partida), el componente de la máquina del jugador mira su guardado a
     los 0,4 s de tener tortuga y, si no está hecho, se lo pide al servidor (`ServerRequestTutorial`). Si ya estaba dentro
     por el camino 1, no pasa nada.
- Pueden estar varios a la vez (hasta 8 sitios de salida). Las piezas de práctica las crea el servidor y se replican
  (cangrejo, montículo, barrita, medusa, catapulta, Rodolfo y Berta) solo a quien está a menos de 90 m: los del lobby no las
  reciben. Se quitan a los 20 s sin nadie dentro. El decorado (terreno, plantas, agua, cascada, carteles, fauna) lo monta cada
  máquina por su cuenta, igual en todas.
- **No toca el lobby**: ni el estado de listo ni la cuenta atrás. Como la cuenta atrás espera a que *todos* estén listos, la
  sala espera a quien esté en el tutorial (o a que lo salte).
- Caerse de las islas o salir de la zona devuelve al último punto de control (uno por estación y otro pasado el cañón).
- **Se da por hecho** al aterrizar tras la cascada o al saltarlo: menú de pausa > **Saltar el tutorial** (con confirmación;
  solo aparece dentro del tutorial) o `TN.Tutorial.Skip`. Saltarlo baja al jugador a la plaza del castillo. Se apunta en el
  guardado de su ordenador en ese momento.
- `BP_TutorialEntryInteractable` («Repetir el tutorial»), si está en el nivel, vuelve a empezarlo desde la salida. Estando ya
  dentro, `bInTutorial` no cambia y no hay nada que replicar, así que el servidor avisa con `ClientResetProgress` y el HUD
  del jugador (tareas tachadas, estación y medidas) empieza de cero; lo mismo con `TN.Tutorial.Start` y `TN.Tutorial.Station 1`
  (#85). Volver a otra estación ya pisada conserva lo hecho.

## Reiniciarlo para probar

**Archivo** (no en Shipping): crear `Saved/ResetTutorial.txt` en la carpeta del proyecto. Cada vez que arranca el juego o el PIE, el tutorial
vuelve a quedar por hacer y el log lo dice con un aviso (`[Tutorial] ...ResetTutorial.txt existe: tutorial reiniciado para
la ventana N`).

- Vacío: todas las ventanas.
- Con números (separados por espacios, comas o líneas): solo esas ventanas de PIE. `0` = la primera (el anfitrión) o el
  juego suelto; `1` = «Cliente 1»; `2` = «Cliente 2»...
- El archivo se queda: para volver a lo normal, se borra.

**Consola** (en la ventana de quien lo prueba; sirve en el anfitrión y en un cliente):

| Comando | Qué hace |
|---|---|
| `TN.Tutorial.Reset` | Deja el tutorial por hacer en esta ventana: empieza al llegar al siguiente lobby. |
| `TN.Tutorial.Start` | En el lobby: empieza ahora desde la salida (aunque ya esté hecho o se esté dentro). |
| `TN.Tutorial.Skip` | Lo salta como el menú de pausa: al lobby y apuntado como hecho. |
| `TN.Tutorial.Station N` | Lleva a la estación N (1-19), metiendo en el tutorial si hace falta. Sin número, la lista. El salto a cualquiera es de pruebas: solo el anfitrión (o en Standalone) y fuera de Shipping; un cliente solo puede volver a una estación que ya ha pisado (el servidor rechaza el resto: `[Tutorial] … rechazada`). |
| `TN.Tutorial.Info` | Escribe la ranura del guardado, si está hecho, si se está dentro, la estación y cuántos hay (en el servidor). |

## Archivos

| Archivo | Qué es |
|---|---|
| `Private/Lobby/TN_TutorialLayout.h` | El diseño fijo: cotas, anchos, biomas, estaciones, puntos de control y sitios de cada pieza. |
| `Private/Lobby/TN_TutorialTexts.h` | Textos (título, tarea y consejo de cada estación). |
| `Public/Lobby/TN_TutorialCourse.h`, `Private/Lobby/TN_TutorialCourse.cpp` | El recorrido: participantes, puntos de control, cascada, piezas de práctica, visibilidad. |
| `Private/Lobby/TN_TutorialCourse_Build.cpp` | Malla de las islas, decorado, agua, cascada, nubes, vegetación y carteles. |
| `Public/Lobby/TN_TutorialPlayerComponent.h`, `.cpp` | Por jugador: pedirlo, detectar cada tarea, teclas, tarjeta en pantalla, caída final, guardado. |
| `Public/Lobby/TN_TutorialWidget.h`, `.cpp` | La tarjeta de la derecha y el mensaje final. |
| `Public/Lobby/TN_TutorialPractice.h`, `.cpp` | Cangrejo de prácticas, montículo para rebuscar y catapulta que se recarga. |
| `Public/Lobby/TN_TutorialFauna.h`, `.cpp` | Fauna ambiental local (huye al acercarse). |
| `Private/Lobby/TN_TutorialCommands.cpp` | Comandos `TN.Tutorial.*`. |
| `Lobby/TN_HQGameMode` | Coloca el recorrido, engancha el componente y lee `?TNTut=1`. |
| `Multiplayer/MP_GameInstance`, `TN_TutorialSaveGame` | Guardado por ordenador y ventana, archivo de reinicio y `?TNTut=1` al unirse. |
| `UI/Pause/TN_PauseMenuWidget.cpp` | Entrada «Saltar el tutorial». |
| `Private/World/ProcMap/TN_ProcMapTrailColors.h` | Colores del camino sacados de `TN_ProcMapGenerator_Build.cpp` para reutilizarlos (mismo comportamiento en el cooperativo). |

## Pruebas

**Solo (PIE, 1 jugador, lobby)**

1. Crear `Saved/ResetTutorial.txt` vacío y darle a Play: se aparece en el lobby y en menos de un segundo arriba, en la salida.
   Log: `[Tutorial] Primera partida en esta máquina`, `empieza el tutorial`.
2. Hacer las 19 estaciones mirando que cada tecla de la tarjeta sea la que se tiene puesta (cambiar una en Ajustes >
   Controles y ver que cambia; coger el mando y ver los botones).
3. Caerse por un lado: vuelve al último cartel. `TN.Tutorial.Station 12` y cruzar con la catapulta en bola y de pie.
4. Saltar por la cascada: cae recta, la cascada se desvanece a media caída, aterriza en la plaza sin caparazón ni golpe y
   sale la despedida. Borrar el archivo y volver a darle a Play: ya no sale (`TN.Tutorial.Info` → hecho).
5. `TN.Tutorial.Start`, menú de pausa > Saltar el tutorial > Saltar: baja a la plaza. Desde el lobby no se ve nada arriba.

**Anfitrión + cliente nuevo (PIE, 2 jugadores, Listen Server)**

1. `Saved/ResetTutorial.txt` con `1`: solo el cliente hace el tutorial; el anfitrión se queda en el lobby y no ve el
   recorrido ni las piezas.
2. El anfitrión se pone listo: la cuenta atrás no empieza hasta que el cliente acaba o lo salta.
3. Con `0 1`, los dos a la vez: salen en sitios distintos, se ven, uno puede coger al otro en la estación 14 y lanzarlo.
4. Por Steam (dos ordenadores): el que se une sin haberlo hecho aparece directamente arriba (`[HQGameMode] ... entra por
   primera vez (?TNTut=1)` en el log del anfitrión).
