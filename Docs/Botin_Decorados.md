# Botín en los decorados del mapa procedural (mantener E para rebuscar)

En el mapa procedural se puede rebuscar en muchos de los decorados que hay en mitad de los caminos: estatuas,
cabezas de piedra, la tortuga colosal, rocas grandes, barcas, carros, cajas, tinajas, restos de barcos y de guerra...
Se mantiene pulsada la tecla de interactuar poco más de un segundo; al terminar, con suerte, sale un objeto de los de
siempre de un saltito («¡puf!»); si no, una nubecilla del color del suelo del bioma («¡pof!»).

## Cómo se juega

1. Al acercarse al borde de un decorado buscable (por cualquier lado, a 2,5 m como mucho) sale el aviso
   **«Mantén para rebuscar»** con la tecla dentro de un aro vacío. Los buscables tienen alguna chispita dorada al pie
   cuando la cámara está a menos de 18 m (más de una a la vez en los grandes) y, mientras quede por buscar y la cámara
   esté a menos de 90 m, el anillo dorado de los objetos del suelo, **fijo alrededor del decorado** y del tamaño de su
   huella (ver «Brillo de lo que se coge»): no se mueve al rodearlo.
2. Al **mantener E** el aro se llena en dorado (1,3 s), saltan tierra y piedrecitas del borde hacia la tortuga y suena
   un rebuscar de arena y chinitas (lo oyen los que estén cerca). **Soltar antes cancela** (y se puede volver a
   empezar). También se corta si la tortuga se aleja del decorado, se mete en el caparazón, queda tumbada, muere o la
   cogen en brazos.
3. Al completarse, el servidor sortea: con un **55 %** sale un objeto de `DT_Items`, con una nubecilla blanca,
   chispas doradas y un «¡puf!» con campanita; el objeto asoma pequeño, da un saltito corto girando (como la tortuga
   al salir del probador, pero más cerca) y queda en el suelo a algo más de un metro, hacia la tortuga y un poco de lado.
   Se recoge como cualquier objeto (E). Si no hay suerte: nube de polvo del bioma (arena en la playa y el desierto,
   tierra en la selva y el manglar, polvo de roca en los acantilados, ceniza en el volcán) y un «¡pof!» sordo con un
   «buuu» bajito.
4. **Cada decorado se rebusca una vez para todo el grupo** (decisión de diseño): el botín queda en el suelo para quien
   lo coja, así que rebuscar es una decisión de equipo (¿quién se para?) y el mapa no se llena de objetos. Mientras
   una tortuga rebusca, a las demás no les sale el aviso. Ya buscado, el decorado no tiene aviso ni chispas.

El inventario lleno no impide rebuscar: el objeto se queda en el suelo.

## El cofre del tesoro del lobby (`ATN_TreasureChest`)

En lo alto de la torre del homenaje del lobby hay un cofre que se rebusca igual (mantener E, mismo aro, mismo
«¡puf!» y mismo saltito), pero con otras reglas:

| | Decorados del mapa | Cofre del lobby |
|---|---|---|
| Duración | 1,3 s (`tn.Search.Seconds` la cambia) | **5 s**, siempre (no hace caso de `tn.Search.Seconds`) |
| Suerte | 55 % (`tn.Search.Luck` la fuerza) | **siempre sale un objeto** (no hace caso de `tn.Search.Luck`) |
| Veces | una sola vez para todo el grupo | **las que se quiera**, tras **2,5 s de respiro** después de cada objeto |
| Objetos sin recoger | sin límite | **seis como mucho**: al salir el séptimo, se quita el más viejo |
| Dónde cae el objeto | en el suelo, a un metro largo hacia la tortuga | sale de dentro del cofre y cae **delante de él, en su tarima** (nunca fuera de la azotea) |
| Aviso | «Mantén para rebuscar» | «Mantén para rebuscar en el cofre» |

- **La tapa** lo cuenta todo, en cada máquina y a partir del estado replicado:
  - al empezar, cruje y se entreabre;
  - mientras dura la búsqueda, sube a tirones de 18° a 55° según el progreso, temblando;
  - al salir el objeto, salta a unos 110° y aguanta 1,2 s;
  - después se cierra con un «¡clonc!» (y se cierra igual si se suelta antes).
- **Brillo dorado dentro**: una luz cálida sin sombras que sube con la tapa y aprieta al salir el objeto, y destellos
  que suben de las monedas.
- Mientras se rebusca, lo que salta hacia la tortuga son **monedas y chispas doradas**: el «polvo» del cofre es oro
  y el sonido de rebuscar va más agudo (`RummagePitch` 1,4).
- Para volver a rebuscar hay que **soltar E y volver a mantenerla**. Durante el respiro no sale el aviso.
- Sonidos nuevos del sintetizador (`ETNSearchSound`): `LidCreak` (roce a tirones por dos resonancias de madera) y
  `LidThump` (golpe grave con la altura que cae, la caja que resuena y un tintineo de herrajes).
- Dónde está y quién lo crea: ver `Docs/Lobby_Castillo.md`.

## Los cofres de la playa (`ATN_BeachChest`, modo carrera)

En la playa del modo carrera hay cofres como el del lobby, en sitios especiales y en la cima de las fortalezas de
arena (los pone el reparto con el elemento `TreasureChest`). Se abren igual que un rebuscable (mantener E, aro, «¡puf!»
y saltito del objeto; `ATN_BeachChestSpot` hereda de `ATN_BeachSearchSpot`), pero son otra cosa: se tarda en abrir y
dan de lo mejor para avanzar. Detalle y red en `Docs/Modo_Carrera.md`, «Cofres».

| | Rebuscables de la playa | Cofre del lobby | Cofre de la playa |
|---|---|---|---|
| Duración | 1,3 s | 5 s | **5,5 s**, siempre (no hace caso de `tn.Search.Seconds`) |
| Suerte | 70 % | siempre | **siempre** (no hace caso de `tn.Search.Luck`) |
| Veces | una para todas | las que se quiera, con respiro | **una por ronda** para todas |
| Qué da | un objeto (pesos por posición de la carrera: los de atrás reciben lo que hace remontar) | un objeto al azar | **dos objetos** con los pesos de la carrera **sesgados a lo mejor** y **seis conchas de puntos** (de 25 a 100: 200 o 250 puntos) |
| Dónde cae | a un metro largo hacia la tortuga | delante, en su tarima | un objeto hacia la tortuga; el otro y las conchas **saltan de dentro y caen en corona** alrededor |
| Después | sin aviso ni chispas | se cierra con un «¡clonc!» | **queda abierto y vacío, con un brillo dorado apagado** |
| Aviso | «Mantén para rebuscar» | «Mantén para rebuscar en el cofre» | «Mantén para abrir el cofre» |

- **Aspecto**: el cofre del lobby 2,2 veces más grande, de madera blanqueada con herrajes oxidados, percebes, algas, una
  estrella de mar pegada y arena al pie; dentro, además de monedas y gemas, perlas y una vieira. Por abrir, una columna
  de luz dorada que se ve de lejos y la luz de dentro que late por la rendija de la tapa.
- **La tapa**, como la del lobby: cruje y se entreabre a tirones mientras se abre (de 12° a 48°) y cae con un «¡clonc!»
  si se suelta antes; al salir el premio salta hacia atrás con un chorro de chispas y **se queda abierta** (104°).
- **Pesos por posición** (`TNRaceItems::RollLoot` con la fuente `Chest`, `Docs/Modo_Carrera.md`, «Objetos de carrera»): los
  dos objetos se sortean según el puesto de quien abre el cofre, entre los de siempre de `DT_Items` y los objetos de carrera
  (turbos, pelícano taxi, protector solar, cangrejo teledirigido, gaviota justiciera, mina, nube de tormenta, disco volador,
  silbato). Al cofre se le aplica un factor que sesga a lo mejor para avanzar (energía sin fin ×2, coco dorado ×2, pelícano
  taxi ×1,6, barra llena ×1,6, triple coco y protector solar ×1,4) y que baja lo flojo (concha trampa ×0,6, silbato ×0,6); la
  cabezota y el tótem, nunca (en la carrera no sirven). Antes: energía sin fin 3, barra llena 2,2, bola y tinta 1,6, concha
  trampa 0,6 (`ATN_BeachChestSpot::ChestWeight`, que queda como respaldo).
- Mientras una tortuga lo abre, a las demás no les sale el aviso (lo de siempre). Los premios que nadie coge se van con
  la ronda.

## Objetos de carrera en el botín (playa del modo carrera)

En la carrera, lo que sale de los **rebuscables**, de los **cofres** y de las **cajas de objetos** sueltas ya no es solo el
catálogo de `DT_Items`: se suman los objetos de carrera definidos en código (coco turbo, triple coco, coco dorado, pelícano taxi,
protector solar, cangrejo teledirigido, gaviota justiciera, mina de arena, nube de tormenta, disco volador y silbato del
sargento) y **todos se sortean según el puesto de quien rebusca, abre o coge** (`TNRaceItems::RollLoot`): a las de atrás les
tocan la bala (pelícano) y la estrella (protector solar); a las de delante, lo que se lanza y lo defensivo. No hay filas nuevas
en `DT_Items` ni script del editor. Las cajas de objetos (`ATN_RaceItemBox`, un cubo de colores con una «?») sustituyen a los
objetos fijos sueltos por la playa. Detalle, pesos y pruebas: `Docs/Modo_Carrera.md`, «Objetos de carrera»; comandos:
`Docs/Comandos_Prueba.md`.

## Montículos de arena (playa del modo carrera)

En la playa del modo carrera, junto a cada decorado que se puede rebuscar hay un **montículo pequeño de arena
removida** (a veces con una chapa, un palito de helado o un trozo de concha asomando) que **tiembla a ratos** con
granitos que saltan, más a menudo y más fuerte cuando una tortuga se acerca: así se sabe desde lejos dónde se puede
rebuscar. Ya rebuscado, se aplasta y **queda aplanado y quieto**.

- Lo lleva el registro de rebuscables de la playa (`ATN_BeachSearchRegistry`, `TN_BeachLoot.h`, y
  `Private/World/Beach/TN_BeachSearchMounds.cpp`): el servidor decide dónde va cada uno y lo replica, compacto, con el
  estado de cada punto (libre o rebuscado); cada máquina con pantalla los monta igual. Mallas de
  `TNBeachDecorKit::SearchMoundMesh` (`TN_BeachDecor.cpp`).
- Barato para cientos: lejos, quietos e instanciados (sin sombra, hasta 120 m); cerca de una cámara (45 m, los 16 más
  cercanos) los mueve un componente de una reserva, como las partes animadas del decorado de la playa. Nada en el
  servidor dedicado.
- El anillo dorado fijo del rebuscable (ver «Brillo de lo que se coge») se centra en su montículo, a ras de su arena y
  algo mayor que él (cada actor `ATN_BeachSearchSpot` sabe el índice de su montículo).
- Detalle, cifras y pruebas: `Docs/Modo_Carrera.md`, «Montículos de arena».

**Cooperativo (mapa procedural): pendiente, anotado.** Los buscables del mapa procedural (`ATN_ProcSearchSpot`) no lo
admiten sin tocar bastante:

- Están en todos los biomas (selva, manglar, acantilados, volcán, desierto, nieve...) y el montículo es de arena a
  color de vértice: haría falta teñirlo con el polvo de cada bioma (`FTNSearchSpotShape::Dust`), que
  `M_CosmeticVertexColor` no admite (un material o una instancia con un parámetro de tinte, en `Content/`).
- `ATN_ProcSearchSpot` es la base común del cofre del lobby (`ATN_TreasureChest`), de los cofres de la playa y de los
  rebuscables de la playa: el montículo tendría que ir con un interruptor (p. ej. `bSandMound`, apagado por defecto) para
  no salir dos veces en la playa ni junto a los cofres.
- Cómo hacerlo cuando toque: cada `ATN_ProcSearchSpot` con el interruptor pondría su montículo con la misma receta en el
  punto del borde de su huella hacia el camino principal; los quietos, en una malla instanciada por mapa (en
  `ATN_ProcMapGenerator`, que ya reparte los buscables) y el temblor con `bNearView`/`TickLocalFX`, que ya existen para
  las chispitas; aplanado al quedar `IsSearched()`.

## Brillo de lo que se coge (todos los modos)

Todo lo que se puede coger lleva la misma marca, para que se entienda igual en el cooperativo, la carrera y el lobby:
**oro que gira en el suelo y chispitas que suben = aquí hay algo para ti**.

- **Objetos del suelo**: todo `ATN_PickupInteractableBase` (lo que da un objeto del inventario: zonas de objetos, lo que
  sale de rebuscar o del cofre, lo que se suelta, la bola lanzada cuando se para y el botín de la playa) lleva de serie
  `UTN_PickupGlowComponent` (`World/TN_PickupGlowComponent.h/.cpp`, componente `PickupGlow`). No hay que tocar ningún
  Blueprint.
  - **Anillo dorado** en el suelo: seis guiones afilados con un destello crema en cada hueco y un aro fino por dentro,
    apoyado en el suelo que haya debajo (inclinado con él). Gira despacio (al revés que el objeto) y respira. Su radio
    sale del tamaño del objeto (55-110 cm). Se dibuja hasta 90 m.
  - **Columna de luz tenue** de 3,6 m que sube del anillo y se desvanece hacia arriba: lo que se ve de lejos (hasta
    150 m).
  - **Chispitas doradas** que suben del anillo (las mismas que las de los rebuscables), a menos de 30 m de la cámara.
  - **Luz** dorada suave, sin sombras (420 lm, 3,8 m de alcance), solo a menos de 18 m de la cámara; se apaga al
    alejarse.
  - **El objeto** sube 14 cm, flota ±6 cm y gira despacio (0,2 vueltas/s) a menos de 40 m de la cámara.
  - Mientras el objeto se mueve (el saltito al salir de un rebuscable) la marca se esconde; al pararse aparece
    creciendo. Al recogerlo se apaga con él.
  - Ajustes por Blueprint en `PickupGlow`: `RingRadius` (0 = según el objeto), `BeamHeight` (0 = sin columna),
    `bFloatAndSpin`, `FloatLift`, `FloatBob`, `SpinTurnsPerSecond`, `LightLumens` (0 = sin luz), `LightRadius` y las
    distancias (`LightRange`, `SparkleRange`, `AnimRange`, `RingDrawDistance`, `BeamDrawDistance`).
- **Decorados que se rebuscan** (`ATN_ProcSearchSpot`, también el cofre del lobby, el montículo del tutorial y los de la
  playa): las mismas chispitas doradas por el borde (en los grandes, una más por cada 30 m de perímetro, hasta cuatro a
  la vez) y **el mismo anillo dorado** de los objetos del suelo (#214), ahora **fijo**: centrado en el propio decorado, a
  ras de su suelo, sin seguir a nadie.
  - **Tamaño**: abarca la huella entera. La huella es una cápsula (`FTNSearchSpotShape`: radio y semilargo) y cabe en un
    círculo de radio `Radius + HalfLength`; el anillo se escala para que sus guiones (la corona 87-99 de la malla)
    empiecen 25 cm más allá de ese borde, así que se ve alrededor del decorado y no debajo de él. En los alargados (la
    barca, la vagoneta, el barco) es un solo círculo que los abarca: uno por el centro quedaría tapado por el propio
    decorado. Mínimo `MarkerRadius` (85 cm) para los diminutos. Ejemplos: un montículo del tutorial (huella de 135 cm) da
    un anillo de 184 cm de radio; el cofre de la playa (155 cm), 207 cm; el barco varado (hasta 10,6 m de huella), 12,5 m.
  - **Playa: en el montículo de arena.** Cada rebuscable de la playa tiene su montículo que vibra (`ATN_BeachSearchRegistry`,
    «Montículos de arena»), y el anillo se centra en él, no en la huella del decorado: a ras de su arena y algo mayor que
    su base (radio de la base = 1,1 × 100 cm × el tamaño del montículo, 0,8-1,25; los guiones empiezan 25 cm más allá: de
    130 a 187 cm de radio de anillo). Cómo se enlazan: el punto `i` del registro tiene el montículo `i`, y el servidor le
    pone ese índice al actor `ATN_BeachSearchSpot` (`SetMoundIndex`, replicado una vez). Cada máquina saca el sitio y el
    tamaño del montículo que ella misma monta a partir de lo replicado (`ATN_BeachSearchRegistry::GetMoundFoot`), así que
    es el mismo en todas; mientras el registro no haya montado ese montículo en una máquina, allí no sale anillo. El gancho
    es `GetMarkerAnchor` (`Footprint` por defecto, `Point` en el montículo, `Pending` si aún no llega). Sin montículo
    propio (el cofre de la playa, el cooperativo, el lobby y el tutorial) el anillo abarca la huella, como arriba.
  - **Suelo**: no se puede trazar al centro (el decorado lo ocupa), así que se mira el suelo en cuatro puntos de la
    propia circunferencia del anillo (fuera de la huella) y el anillo se apoya a la altura media y se inclina con el plano
    que forman (con la pendiente muy empinada, plano). Una sola vez al salir; si falta algún punto porque la colisión del
    terreno aún se está cocinando, se repite cada segundo hasta seis veces. Un punto muy distinto del centro (un escalón)
    cuenta como el suelo del centro.
  - **Movimiento**: gira despacio al revés y respira igual que el de los objetos (`TNLootGlow::RingPose`, compartida con
    `UTN_PickupGlowComponent`: -24 °/s, respiración del 4,5 % cada 2,6 s), cada decorado con su desfase (sacado de su
    sitio, no al azar: igual en todas las máquinas). Mientras alguien
    rebusca, gira cinco veces más deprisa y late (lo ven todas las máquinas, no solo quien rebusca).
  - **Visibilidad**: la de los objetos: solo en máquinas con pantalla y hasta `MarkerDrawDistance` (90 m) de la cámara
    local, igual en todas (ya no depende de dónde esté la tortuga); aparece creciendo y se va al agotarse el rebuscable
    (`IsSpent`; en los repetibles, durante el respiro). Se anima cada fotograma a menos de 40 m; más lejos, al ritmo
    lento del tick del decorado (0,3 s). En la playa el actor solo existe a menos de 50 m de alguna tortuga, así que allí
    sale desde que aparece el actor.
  - Lo que antes hacía de «marca que sigue al jugador» no era más que visual: el punto del borde por el que se rebusca
    (`GetInteractionPointFor`, `RimPointToward`), de donde salen la tierra y el objeto, no cambia.
- **Conchas de puntos** (`ATN_ScorePickup`): siguen con su brillo propio (la vieira que gira, destellos, halo, luz y
  columna de su color): se cogen al pasar, sin tecla, y así se distinguen.

**Coste**: nada en el servidor dedicado ni en red (todo local en cada máquina). Una sola malla de anillo y una de columna
para todos (`Private/World/TN_LootGlowKit.h`, construidas en ejecución con `M_ProcGlow`, opaco que brilla, y
`M_ProcFXSoft`, translúcido sin luz; sin assets nuevos), sin sombras; la luz y las chispitas solo cerca; lejos de la
cámara el componente mira la distancia cada 0,35 s y nada más.

**Probar**: tirar un objeto (soltar el equipado) y la bola (al pararse sale su anillo); rebuscar con `tn.Search.Luck 1`
(el objeto salta sin anillo y, al aterrizar, aparece creciendo); acercarse y alejarse (luz a 18 m, chispitas a 30 m,
flotar a 40 m, anillo a 90 m, columna a 150 m); recogerlo (se apaga). En un rebuscable, el anillo fijo alrededor del
decorado: rodearlo sin que se mueva, verlo desde lejos (hasta 90 m en el cooperativo), que gire más deprisa mientras se
rebusca y que se vaya al buscarse. En el lobby, el cofre: el anillo a su alrededor. Con dos jugadores, los dos ven el
mismo anillo, en el mismo sitio.

## Qué decorados se pueden rebuscar

Los elige `ATN_ProcMapGenerator::SpawnSearchSpots` (`TN_ProcMapGenerator_Spawn.cpp`) entre los decorados del layout,
a **70 m como mínimo entre sí** y con una probabilidad por tipo; primero las formaciones, luego los objetos del
camino, las agujas de roca y los peñascos. Semilla propia (derivada de la del mapa): el mismo mapa da los mismos
buscables. Nada en el modo de solo terreno (`bTerrainOnly`).

| Decorado (`EFeature`) | Tipos | Probabilidad |
|---|---|---|
| Formación de explanada (`Formation`) | barco varado, cabeza de piedra, basalto, chimenea de hadas, peñasco en equilibrio, carreta, cañón, sacos terreros, búnker, torre de vigía, carro de combate, caracola gigante, ancla, círculo de piedras (su altar), obelisco, cráneo fósil, tortuga colosal, depósito de agua | 100 % (agujas de obsidiana, 70 %) |
| Objetos del camino (`PathProp`) | cajas, barriles, pacas, castillo de arena, barca, sombrilla y tumbonas, tótem, columna en ruinas, roca calavera, tinajas, cristales, mojón, vagoneta, nasas, puesto de mercado | 45–80 % según el tipo |
| Aguja o mogote de roca (`RockSpire`) | todas | 45 % |
| Peñasco (`Boulder`) | solo los grandes (radio ≥ 1,4 m) | 30 % |

Fuera: arcos sobre el camino, hitos lejanos, vegetación (troncos, secuoyas, setas gigantes), la fumarola, vallas y
filas de conos.

La **huella** de cada decorado es una cápsula en planta a lo largo de su `Dir` (radio y semilargo aproximados a su
malla: `TNSearchSpotPlan::SearchableOf`): la barca, la vagoneta, el barco, la carreta, el cañón, el carro de combate y
el cráneo son alargados; el resto, redondos.

## Arquitectura

| Pieza | Qué hace |
|---|---|
| `ATN_ProcSearchSpot` (`World/ProcMap/TN_ProcSearchSpot.h/.cpp`) | Actor ligero y replicado (dormido casi siempre) en el suelo del centro del decorado. Hereda de `ATN_InteractableBase`; sin malla ni widget 3D; una esfera invisible (`WorldDynamic`, solo consultas) que envuelve la huella para el escaneo de interactuables. |
| `UTN_SearchSynthComponent` (mismo fichero) | Sonidos sintetizados sin archivos (patrón de `UTN_PlaygroundSynthComponent`): puñado de arena y piedrecitas, «¡puf!» y «¡pof!». Se crea al primer sonido de cada decorado y se para callado. |
| `ATN_InteractableBase` | API nueva de **interacción de mantener**: `GetHoldDuration`, `BeginHoldInteract`, `EndHoldInteract`, `GetHoldProgress`, y `GetInteractionPointFor(Pawn)` (el punto del borde más cercano; por defecto, `GetInteractionPoint()`). |
| `ATortugaCharacter` | Si el interactuable al alcance es de mantener, E manda `ServerBeginHoldInteract` en vez de `ServerTryInteract`; al soltar (`Completed`/`Canceled` de `IA_Interact`) manda `ServerEndHoldInteract`. El escaneo y la validación usan `GetInteractionPointFor(this)`. |
| `UTN_HoldRingWidget` (`UI/HUD`) | Aro de progreso pintado en código alrededor de la tecla del aviso: pista azul marino con filo crema y relleno dorado. `UTN_RunHUDWidget::TickPrompt` lo enseña en los interactuables de mantener (vacío nada más pulsar, mientras llega la respuesta del servidor). |
| `ATN_TreasureChest` (`Lobby/TN_TreasureChest.h/.cpp`) | Subclase para el cofre del lobby: fija sus reglas en el constructor y usa los ganchos de la base (abajo). Malla propia (caja y tapa construidas en código) y caja de colisión. |
| `ATN_BeachSearchSpot` (`World/Beach/TN_BeachLoot.h/.cpp`) | Subclase para el decorado de la playa del modo carrera: más suerte, pesos de la carrera, polvo de arena y pistas a la escala de la playa (ver `Docs/Modo_Carrera.md`, «Botín en la playa»). |
| `UTN_PickupGlowComponent`, `TN_LootGlowKit.h` | La marca común de lo que se coge (arriba, «Brillo de lo que se coge»): el componente de los objetos del suelo y las mallas, colores y chispitas que comparten con los rebuscables. |

### Opciones y ganchos de `ATN_ProcSearchSpot` para subclases

Los decorados del mapa los dejan como están; el cofre del lobby los usa.

| Opción o gancho | Por defecto | Qué hace |
|---|---|---|
| `bRepeatable`, `RepeatCooldown` | no, 2,5 s | Se puede rebuscar otra vez tras el respiro. `IsSpent()` dice si ahora no se puede: ya buscado (los de una vez) o dentro del respiro (los repetibles). Es lo que miran `CanInteract`, el aro, las chispitas y el tick. Los repetibles no salen nunca del escaneo. |
| `MaxLootLying` | 0 (sin límite) | Objetos sin recoger que pueden quedar a la vez; al pasarse, `SpawnLoot` destruye el más viejo. Lo recogido no cuenta: el pickup se destruye al cogerlo. |
| `RummagePitch` | 1 | Multiplica el tono del sonido de rebuscar. |
| `HintDistance` | 18 m | Distancia de la cámara al borde a la que salen las chispitas de «aquí se puede rebuscar». |
| `MarkerDrawDistance`, `MarkerRadius` | 90 m, 85 cm | Distancia de la cámara local al borde del anillo fijo hasta la que se dibuja (0 = sin anillo) y su radio mínimo (el anillo abarca la huella: `Radius + HalfLength` más un margen). |
| `ReachSlack` | 85 cm | Margen sobre el alcance de interacción (250 cm) mientras se rebusca, antes de cancelar por alejarse (antes 120 cm con 350). |
| `GetLuck()` | `LootChance` o `tn.Search.Luck` | Probabilidad de que salga algo. |
| `GetLootWeight(Fila, Objeto)` | `LootWeights` por fila o `ItemId`, o 1 | Peso de cada objeto del catálogo en el sorteo (0 lo quita). La playa usa el de la carrera. |
| `GetLootOrigin(Pawn)` | borde hacia la tortuga, a 40 cm | De dónde sale el objeto o la nube. |
| `GetRummageOrigin(Searcher)` | borde hacia la tortuga, a 20 cm | De dónde saltan tierra y sonido mientras se rebusca. |
| `FindLanding(Pawn, From)` | en el suelo, a un metro largo hacia la tortuga | Dónde cae el objeto (servidor). |
| `OnSearchStateChanged(OldState)` | nada | Reacción propia a cada cambio del estado, en cada máquina (también en el anfitrión). |
| `WantsFrameTick()` | no | Mantiene el tick a cada fotograma (animaciones propias). |
| `GetSearchState()`, `ServerNow()`, `PlaySearchSound()`, `EmitSparkles()` | | Estado replicado, hora del servidor, sonido sintetizado y chispitas doradas para las subclases. |

### Red

- **Servidor autoritativo.** El cliente solo avisa de que empieza (`ServerBeginHoldInteract`, con la misma
  validación de distancia y holgura por ping que `ServerTryInteract`) y de que suelta. El tiempo lo cuenta el
  decorado en el servidor (su tick, cada fotograma mientras alguien rebusca), que además vigila que la tortuga siga a
  su alcance (2,5 m + 0,85 m de margen) y en condiciones. El cliente no puede adelantar el final.
- **Alcance** (#214): el escaneo de la tortuga (`ATortugaCharacter::MaxInteractionDistance`) y el del rebuscable
  (`TNSearchSpotDetail::Reach`) salen de la misma cifra, `ATortugaCharacter::DefaultInteractionDistance` = **250 cm**
  (antes 350). El servidor acepta hasta `max(250, el del interactuable) + 100 + holgura por ping` (25 % del ping, hasta
  120 cm) al empezar, o sea 100 cm más de lo que deja el escaneo del cliente: la misma holgura de antes, sin cambios.
- **Estado replicado** (`FTNSearchSpotState`, un solo struct para que llegue entero): quién rebusca y desde qué hora
  del servidor (el aro de cada cliente se calcula con `GetServerWorldTimeSeconds`), el resultado y su hora, de dónde
  sale y dónde cae el objeto, el pickup y la cuenta de búsquedas completadas (`SearchCount`). Los efectos salen de
  los cambios de estado en cada máquina (`OnRep`; el anfitrión los aplica al cambiarlo), **sin multicast**: no se
  pierden, y quien entra en alcance más tarde (más de 1,5 s después) solo ve el decorado ya buscado.
- Un resultado es nuevo cuando cambia `SearchCount`. En los decorados de una vez equivale a pasar de «sin buscar» a
  buscado; en los repetibles (el cofre) distingue cada objeto que sale.
- La huella y el color del polvo se replican una vez (`COND_InitialOnly`). El actor duerme (`DORM_DormantAll`) y se
  despierta solo al cambiar de estado; vuelve a dormir 3 s después.
- **El objeto**: el servidor crea el pickup de la fila (`PickupActorClass` + `InitializeFromInventoryItem`, igual que
  `ATN_ItemSpawnZone` y soltar lo equipado) directamente en su sitio final. El saltito lo anima cada máquina con
  pantalla sobre el propio pickup (sin movimiento replicado), desde el borde del decorado hasta ese sitio, y acaba
  siempre en él; si una máquina lo recibe tarde, lo deja directamente en el suelo. Los objetos que nadie recoge se
  destruyen con el decorado al regenerarse el mapa.
- **Catálogo**: todas las filas de `DT_Items` con `PickupActorClass` y un uso (`UseType` distinto de `None`), a
  sorteo con peso 1; `LootWeights` cambia el peso por nombre de fila o `ItemId` (el tótem, que revive, va a 0,3; un
  peso 0 quita un objeto). El sorteo es `ATN_ProcSearchSpot::PickCatalogItem(Tabla, Peso, Objeto)` (estático, con el
  peso de cada fila como función): lo usan también los objetos sueltos de la playa; las subclases cambian el peso con
  `GetLootWeight`.

### Efectos

Partículas de `TNAmbientFX` (instancias de una malla, sin Niagara, nada en servidor dedicado): chispitas doradas,
nube de polvo del color del camino del bioma (el de `ResolveBiomeColors`, algo más claro; ceniza en el volcán),
piedrecitas opacas y la nubecilla blanca del «¡puf!». Los emisores de cada decorado se crean cuando hacen falta y se
quitan cuando ya no queda nada vivo y la cámara se ha ido (o el decorado ya está buscado), así que solo hay instancias
en los pocos decorados cercanos. El actor solo va a cada fotograma cuando la cámara está cerca de uno por buscar,
alguien rebusca, hay saltito o quedan partículas; si no, su tick va cada 0,3 s.

## Pruebas en PIE

1. Abrir `LVL_ProcMap` (Coop, Carrera o 2vs2) y jugar con 1 y con 2 jugadores (cliente con escucha).
2. Consola: `tn.Search.Show 1` → baliza vertical y huella de cada buscable a menos de 300 m (dorado = por buscar,
   naranja = rebuscando, verde = salió algo, gris = vacío). El log del servidor dice cuántos hay:
   `[ProcMap] Decorados para rebuscar: N de M candidatos`.
3. Ir a uno: sale «Mantén para rebuscar» al llegar a su borde por cualquier lado. Pulsar y soltar enseguida:
   el aro empieza y se corta. Mantener: aro dorado, tierra y sonido; al llenarse, «¡puf!» u «¡pof!».
4. `tn.Search.Luck 1` fuerza que siempre salga algo (`0`, que nunca; `-1`, lo normal). `tn.Search.Seconds 3` alarga
   la búsqueda para probar los cortes (alejarse, meterse en el caparazón). `TN.Debug.Interaction 1` registra en el
   servidor cada empiece, corte y resultado (`[Search] ...`).
5. Con dos jugadores: mientras uno rebusca, al otro no le sale el aviso y ve y oye la tierra; los dos ven el «¡puf!» y
   el saltito, y el objeto está en el mismo sitio para ambos. El que llega más tarde a un decorado ya buscado no ve
   efectos.
6. Regenerar el mapa (siguiente ronda): los objetos que nadie cogió desaparecen con él.

### El cofre del lobby

1. Abrir `LVL_Lobby` y jugar (1 jugador y luego anfitrión + cliente). El log del servidor dice
   `[Castillo] Cofre del tesoro en la azotea de la torre del homenaje: ...`.
2. Subir por la escalera de caracol a la azotea: el cofre está delante del torreón, sobre su tarima, mirando a la
   plaza, con chispitas doradas alrededor. Desde la plaza se ve entre las almenas.
3. Mantener E junto a él: el aro tarda 5 s en llenarse. La tapa cruje y se entreabre a tirones, con luz dorada
   dentro, y saltan monedas y chispas hacia la tortuga. Soltar antes: la tapa se cierra con un «¡clonc!» y no sale
   nada.
4. Aguantar los 5 s: «¡puf!», la tapa se abre del todo y un objeto sale de dentro y cae delante del cofre, en la
   tarima. Probarlo con `tn.Search.Luck 0` puesto: el cofre da objeto igual.
5. Soltar E y volver a mantenerla: durante unos 2,5 s no sale el aviso; luego se puede otra vez. Sacar siete objetos
   sin recogerlos: el primero desaparece.
6. Con dos jugadores: el otro ve la tapa, la luz y el saltito a la vez, y no le sale el aviso mientras el primero
   rebusca.

### Los cofres de la playa

1. `open LVL_BeachRace?BeachSeed=42` (1 jugador y luego anfitrión + cliente). En el anfitrión, `TN.Beach.Chest`: sale
   un cofre delante, con el frente hacia la tortuga, la columna de luz dorada y la luz que late por la rendija.
2. Mantener E: el aro tarda 5,5 s en llenarse; la tapa cruje y se entreabre a tirones, con luz dentro y monedas y
   chispas hacia la tortuga. Soltar a los 3 s: la tapa cae con un «¡clonc!» y no sale nada. `tn.Search.Seconds 0.3`
   no lo acorta.
3. Aguantar los 5,5 s: «¡puf!», la tapa salta y se queda abierta; un objeto cae delante, hacia la tortuga, y otro
   objeto y seis conchas saltan de dentro y caen alrededor. El cofre queda vacío, con la luz apagada y sin columna; ya no
   sale el aviso. El registro del servidor dice qué ha dado (`[Playa] cofre ... abierto por ...`).
4. Con dos jugadores: mientras una lo abre, a la otra no le sale el aviso; las dos ven la tapa, los saltos y los premios
   en el mismo sitio, y las conchas suman a quien las coge.
5. `TN.Beach.Place clear` (o la ronda siguiente) quita el cofre y lo que nadie cogió.

## Límites conocidos

- Montículos de arena: solo en la playa del modo carrera (en el cooperativo, pendiente: ver «Montículos de arena»).

- `IA_Interact` debe seguir con el disparador implícito (pulsada mientras se mantiene). Con un disparador *Pressed*,
  `Completed` llegaría enseguida y rebuscar se cortaría siempre.
- Si el servidor rechaza el empiece (p. ej. otro jugador empezó a la vez), el aro se queda vacío mientras se
  mantenga la tecla; al soltar y volver a pulsar se reintenta.
- La tortuga no hace pose ni meneo al rebuscar (los emotes van por el sistema de la rueda y el revive): lo cuentan la
  tierra que salta, el sonido y el aro.
- Las huellas son aproximadas a las mallas; con decorados muy irregulares el aviso puede salir algo antes o después
  del borde real.
- La densidad (70 m) y las probabilidades están en `TNSearchSpotPlan` (`TN_ProcMapGenerator_Spawn.cpp`); la duración,
  la suerte y los pesos en `ATN_ProcSearchSpot` (`SearchSeconds`, `LootChance`, `LootWeights`).
- Cofre del lobby: si se sigue manteniendo E después de sacar un objeto, al acabar el respiro sale el aviso con el aro
  vacío, pero no empieza otra búsqueda hasta soltar y volver a pulsar. Es lo mismo que cuando el servidor rechaza un
  empiece.
- Cofre del lobby: la tapa es solo visual (no tiene colisión); la caja de colisión es la del cofre cerrado.
