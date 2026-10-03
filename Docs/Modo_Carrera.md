# Modo carrera en la playa

Rama `claude/modo-carrera` (sale de `claude/elegant-fermi-6n1hxy` en `5153406b`): se puede descartar sin tocar el
cooperativo. El cooperativo (lobby del castillo y mapa procedural) sigue igual.

## Bucle de juego

1. **Menú principal**: en vez de solo «Crear partida», se elige el modo: **Cooperativo** (lo de siempre: lobby del
   castillo y mapa procedural) o **Carrera**. El modo va en `UMP_GameInstance::SelectedProcMode` (`Coop` o `Race`).
2. **Lobby**: el mismo (`LVL_Lobby`, pantalla de carga, huevos y puerta doble). El anfitrión puede cambiar ahí el modo y
   la dificultad hablando con el General Galápago (pestaña «Misión»). Al ponerse todos listos, el lobby viaja según el
   modo: Cooperativo → `LVL_ProcMap`; Carrera → `LVL_BeachRace`.
3. **Carrera** (`ATN_BeachRaceGameMode`, `ATN_BeachRaceGameState`): todos contra todos en la playa. Gana la ronda quien
   primero toca el agua tras saltar el acantilado de la orilla y se lleva una **concha** entera. Desde ese momento,
   **cuenta atrás de 10 s** para todas: quien llegue dentro se lleva **media concha**; al acabar, «¡TIEMPO!» y a cada
   una que no ha llegado le sale de la arena un **gusano gigantesco que se la come** (si ya han llegado todas, «¡TODAS AL
   AGUA!» sin gusanos). Las conchas van en medias (`ATN_CoopPlayerState::RaceShellHalves`). En carrera **no se muere**:
   lo que en el cooperativo mata, aquí aturde (bola de caparazón temblando unos segundos).
4. **Recuento** tras cada ronda (`ETNBeachRacePhase::RoundResults`): las caras de los jugadores, cada una con tres
   huecos de concha en zigzag (como en los juegos de preguntas; cada hueco, dos medias); vuela la concha entera de la
   ganadora y saltan las medias de la cuenta atrás. Luego, otra ronda con los elementos recolocados (el terreno es
   siempre el mismo).
5. **Sprint final** (`ETNBeachRacePhase::SprintIntro`) si al cerrar una ronda hay empate en lo más alto con tres conchas
   o más: título «¡SPRINT FINAL!» y una ronda corta solo para las empatadas, que salen como en cada ronda, del nido de
   huevos, pero llevado a mitad de la playa; la primera en el agua es campeona. Las demás lo miran de fantasma.
6. **Campeón** (`ETNBeachRacePhase::Champion`) al llegar una sola a tres conchas (o al ganar el sprint): menú distinto con **Volver a jugar**,
   **Cambiar de modo** y **Salir** a la izquierda y, a la derecha, un fondo animado: las tortugas en un podio hecho de
   basura de la playa. La primera levanta la concha como un trofeo con las dos manos; la segunda, decepcionada; la
   tercera, sentada en el podio, enfadadísima y pataleando. Movimientos cortos en bucle, como un GIF.

## El mapa (`LVL_BeachRace`, `ATN_BeachRaceGenerator`)

- **Escala**: la tortuga es una cría de ~5 cm; todo va a `TNBeach::Scale` = 28 veces su tamaño real.
- **Terreno fijo** (siempre el mismo): un único tramo recto de playa de `TNBeach::CourseLength` = 800 m (eran 1200:
  llegar al final era demasiado complicado; ver «Recorrido de 800 m») por `TNBeach::CourseWidth` = 280 m jugables,
  calculado para ~3 min 20 s: andando a 4,5 m/s y esprintando a 8 m/s (13 s por barra de energía), con un 40 % de
  esprint y los obstáculos, sale una media de ~4 m/s. Toda de arena, bajando hacia el mar (siempre se ve la meta) con un
  relieve irregular: dunas con cresta (algunas con una cornisa que se salta o un collado para pasar), corredores más
  bajos que se separan y se juntan, charcas y pozas de marea que se nadan y dos líneas de trincheras en zigzag.
- **Salida**: una fila de cuatro huevos (como la salida del cooperativo); cada tortuga espera dentro del suyo y, al dar
  la salida, las tapas saltan, se la ve **1 s en su huevo roto** (se pone de pie, se sacude la cáscara y mira al mar) y
  sale lanzada hacia el mar, ya corriendo, a la vez que las demás. Nada del reparto en los primeros 15 m.
- **Meta**: un acantilado de rocas al final de la playa, de unos 55 cm reales (`TNBeach::CliffHeight` = 15,5 m en el
  juego, 5-6 veces la tortuga): se salta desde el borde y se cae al agua, donde flotan las banderas de meta; se gana al
  tocar el agua. El último salto, desde la zona del borde, se hace de cabeza (zambullida).
- **Bordes**: a los lados y detrás, selva de palmeras y árboles enormes (a escala: una palmera de 10 m mide 280 m).
- **Pasarelas**: la pasarela de madera vieja y el caminito de palos con cuerda son elementos del reparto procedural
  (`Boardwalk`, `WoodenPostPath`), repartidos por la playa y a veces como guía visual hacia el mar; no marcan la
  salida ni la meta.
- **Reparto procedural** (cada ronda, con semilla): decorado, trampas y enemigos de `ETNBeachElement`
  (`Public/World/Beach/TN_BeachTypes.h`), ~3100 por ronda, cada uno con su huella, sin solaparse, dejando siempre paso
  (aunque sea sinuoso) y sin ninguna línea recta libre hacia el mar: hay que cambiar de rumbo sin parar.
- **Decorado gigante** (`ATN_BeachDecor`): cocos, medusas varadas, anillas de latas cortadas, un sujetador rojo,
  almejas, conchas y estrellas de adorno, rocas, restos de una vela de barco, troncos con musgo, tablones viejos, redes
  de pesca, vasos, botellas, chupachups, cortezas de sandía roídas, pajitas, sombrillas clavadas y sillas de playa del
  día anterior, castillos de arena pequeños y enormes, madera a la deriva. Y lo de la tropa de Tortunavy: parapetos de
  sacos terreros, cajas de munición, erizos antitanque, cascos tirados, redes de camuflaje sobre palos, bidones y
  soldaditos de juguete verdes.
- **Trampas e interacciones**: alambre de espino, algas que enredan, plataformas sobre hoyos que se tambalean y se
  rompen con más de una tortuga encima, cubos rotos por los que se pasa, palas que hacen de trampolín o de puente con un
  compañero, un castillo de arena enorme con salas por dentro que hay que atravesar, puertas de conchas, minas de
  juguete medio enterradas que explotan al pisarlas y te lanzan en bola unos metros hacia atrás.
- **Enemigos y amenazas**: cangrejos gigantes que patrullan sin parar, te ven de frente y te oyen alrededor, te
  persiguen y dan un mazazo con la pinza (te dejan despachurrada en bola, aturdida); erizos de mar grandes que ruedan
  despacio hacia ti (pinchan: derribo con ragdoll y mareo); lagartos que pasean, asustan y se esconden; quads enormes
  que cruzan la playa de lado a lado entre palmeras (temblor de pantalla, nube de humo, ruedas anchísimas: o fuera de su
  paso o en el hueco entre las ruedas; si te pillan, ragdoll lanzado); gaviotas y pelícanos que rondan por arriba, cada
  uno en su círculo (te cagan encima: ragdoll y mancha; bajan en picado con su sombra creciendo: esquiva con el panzazo;
  o te cogen con el pico, cuelgas pataleando, te suben y te sueltan: en bola al caer); la tormenta de bañistas por
  detrás, a ras de arena y más lenta que la carrera, con sombrillas, cubos y sillas de playa volando.

## Terreno, nivel y reparto (`ATN_BeachRaceGenerator`, `TN_BeachLayout.h`)

Archivos: `Public/World/Beach/TN_BeachLayout.h` (lógica pura: terreno fijo con su relieve, salida, sprint, meta y
reparto, con sus tests en `Private/Tests/TN_BeachLayoutTest.cpp`), `Public/World/Beach/TN_BeachRaceGenerator.h` y, en
`Private/World/Beach/`, `TN_BeachRaceGenerator.cpp` (rondas, consultas y meta), `_Build.cpp` (arena, roca, mar, muros y
agua), `_Features.cpp` (trincheras, cornisas, rocas de las pozas de marea y agua de las pozas), `_Start.cpp` (tapas de
los huevos de la salida y lanzamiento), `_Scenery.cpp` (salida, meta, selva y sus huecos, huellas y chapuzón) y
`TN_BeachRaceKit.h`; el nivel, `Scripts/build_beach_race.py`. Fuera: un cambio mínimo de sombras en
`Lobby/TN_LobbyValley.cpp` (ver «Sombras») y la llamada a `OpenStartEggs` en `ATN_BeachRaceGameMode::BeginRace`.

### Recorrido de 800 m (antes 1200 m)

Llegar al final de los 1200 m era demasiado complicado, así que el recorrido se acortó a **800 m**
(`TNBeach::CourseLength` = 80000 cm; la mitad, donde sale el sprint final, queda a 400 m). Todo lo que depende del largo
sale de ese número, sin cifras sueltas en cm:

- **Terreno fijo**: se sitúa por fracciones del recorrido (crestas, pozas, trincheras, corredores, línea del sprint), así
  que se comprimió a 2/3 solo. El desnivel es el 3 % del recorrido (`BeachDrop`: 24 m; la misma cuesta del 4,5 % al
  principio que los 36 m de 1200 m, y se sigue viendo el mar y las banderas de meta desde la salida con 4,2 m de margen).
  La cresta de la salida (12,5 %) se quitó y la media luna del final pasó del 89,5 % al 88,5 %: la prueba `Relief` pide
  que las crestas estén después de que el relieve crezca (120 m) y antes de que la roca lo apague (90 m del filo).
- **Menos crestas y pozas** (la misma densidad por metro): con las diez crestas y las diez pozas de antes en 2/3 de sitio,
  las cornisas (seis, cada una una banda de 116 m donde no cabe un castillo con salas) y las charcas ocupaban tanto que
  no cabía casi nada grande: 1,0 castillos con salas por ronda y la fortaleza colosal en 4 de 24 rondas (con 1200 m,
  2,1 y 23 de 24). Ahora hay **siete crestas** (tres que cruzan, dos que separan corredores, dos medias lunas; cuatro con
  cornisa) y **siete pozas** (cuatro charcas y tres de marea); ver «Medidas». Las dos trincheras se quedan (son la tropa).
- **Cuotas por ronda**: `TNBeachLayout::LengthScale` (= `TNBeach::CourseLengthScale` = 800 / 1200 = 2/3) multiplica lo que
  se contaba «por ronda» con 1200 m: castillos enormes, fortalezas grandes y medianas, quads (una como mínimo), gaviotas,
  filas (cinco huecos en vez de siete), puestos, filas de erizos, campos de minas, rincones, calles de ermitaños, pulgas,
  tanques, cofres de sitio especial, las ayudas y trampas destacadas (`PlaceFeaturedTraps`), las catapultas garantizadas
  (`MinCatapults` = 8) y las probabilidades de los castillos con salas de más. Se quedan como estaban los que no dependen
  del largo: una fortaleza colosal por ronda, un castillo con salas principal, los pulpos de cada poza y los enemigos y
  trampas del relleno por bandas de 50 m (que ya son por metro). Los intentos de rincones, calles, tanques y pulgas no
  bajan de los de 1200 m: con menos cuota se sigue igual de mal para encontrar sitio.
- **Botín**: rebuscables 240 en total y 50 por tramo (360 y 75), objetos sueltos 20-25 y 3 filas (30-38 y 4) y topes de
  conchas de 200, 27 y 8 para las de 1, 25 y 50 (300, 40 y 12; las de 100 se quedan en 2).
- **Tiempos**: límite de ronda 9 min (`RoundTimeLimitSeconds`; con 800 m fue 6 min, y cortaba rondas sin aviso: ver
  «Tiempo de ronda, bola del caparazón y red con 4 y 8 jugadores») y de sprint 4 min 30 s (`SprintTimeLimitSeconds`; fue
  3). Estimación de la ronda en la interfaz: 3,3 min (`AverageRaceSpeed`).
- **Tormenta** (`ATN_BeachStorm`): 10 s de gracia (15), a partir de los 160 s de marcha sube 45 cm/s por minuto (240 s y
  30) y alcanza a la última cuando le saca 120 m (180), hasta quedarse a 80 (120). La velocidad (1,8 m/s), la aceleración
  y el final (80 % del recorrido, 2,5 m/s) no cambian: la proporción con la carrera es la de antes.

| Por ronda en Normal (media de 24) | 1200 m | 800 m | Proporción |
|---|---|---|---|
| Elementos | 4834 | 3104 | 0,64 |
| Enemigos (cangrejos) | 141 (30) | 98 (21) | 0,69 (0,69) |
| Trampas que estorban | 261 | 167 | 0,64 |
| Ayudas (con fortalezas y cofres) | 112 | 73 | 0,65 |
| Cofres | 25,8 | 16,7 | 0,65 |
| Castillos con salas / enormes | 2,1 / 9,4 | 1,7 / 4,2 | 0,82 / 0,45 |
| Fortalezas (colosales) | 5,4 (1,0) | 4,0 (1,0) | 0,74 |
| Quads / gaviotas | 2,5 / 5,0 | 1,5 / 3,3 | 0,6 / 0,66 |
| Filas / rincones / piezas militares | 6,1 / 7,8 / 26 | 4,0 / 4,9 / 16 | 0,66 / 0,63 / 0,61 |
| Ermitaños / pulpos / pulgas / tanques | 6,5 / 16 / 10 / 9,9 | 6,3 / 11 / 6,7 / 6,6 | 0,97 / 0,7 / 0,67 / 0,67 |
| Catapultas / trampolines / plataformas móviles | 16 / 23 / 21 | 10 / 14,5 / 15 | 0,63 / 0,63 / 0,7 |
| Ocupación (media / primer tercio) | 49,5 % / 47,7 % | 48,3 % / 52,0 % | la misma densidad |

Los castillos enormes son los que menos caben (4,2 de los 6 que se piden): las fortalezas y el castillo con salas ocupan
lo que el terreno deja libre. Las cifras salen de compilar `TN_BeachLayout.h` y `Layout.Rules` fuera del motor
(medidas con esta misma prueba; ver «Pruebas»).

### Medidas (espacio del generador en cm: X hacia el mar, Y a lo ancho, el agua en Z = 0)

- **Recorrido**: línea de salida en X = 0 y filo del acantilado en X = 800 m (ondula ±2,5 m a lo ancho). Playa jugable
  `|Y| <= 140 m`. Muros invisibles a 148 m a cada lado (también en el agua), detrás de la salida (X = -32 m) y mar
  adentro (X = 1100 m), de -200 a +1200 m de alto.
- **Perfil**: 39,5 m sobre el agua en la salida y 15,5 m en el filo: cae 24 m (el 3 % del recorrido) como `(1 - t)^1,5`
  (4,5 % al principio, casi llana al final). Con el relieve encima, desde la salida se sigue viendo el mar por encima del
  filo (4,2 m de margen como poco, medido desde los cuatro huevos). Rejilla de 3 m en la playa (hasta 18 m lejos) en 40
  teselas de 40 x 40 casillas; con colisión, las que quedan a tiro. `M_ProcTerrain` con relieve y, en la arena (alfa 1), grano, guijarros y marcas del viento; tierra y
  hojarasca en los bancos; arena mojada alrededor de las pozas y húmeda en el fondo de las trincheras.
- **Relieve** (`ReliefZ`, fijo, sobre el perfil): entra entre 35 y 170 m y se apaga en los últimos 90 m antes de la
  roca. Dunas de 2,8 m de amplitud en el centro a 4,8 m junto a la selva (lomos, bultos y nudos) y ondulación a tres
  escalas (1,7 m cada ~130 m, 70 cm cada ~43 m y 25 cm cada ~16 m), más los corredores, las crestas, las pozas y las
  trincheras. Medido (con la prueba `Relief` compilada fuera del motor): desviación típica 1,20 m, de -5,0 a +4,5 m sobre
  el perfil, pendiente máxima 37° (rejilla de 7 m, diferencias de 1 m) y el 23 % de la playa con más de 10°.
  Todo se anda salvo las cornisas; lo empinado son las caras de las crestas que miran a la salida.
- **Corredores** (`CorridorAt`, 3): dos caminos más bajos que se separan (hasta 54 m entre ejes; 61 m con el tercero)
  y se vuelven a juntar, y un tercero en medio por tramos. 1,7 ± 0,7 m más hondos que lo de alrededor, con las dunas
  apagadas dentro: el camino natural (y el de algunos campos de minas).
- **Crestas** (`Ridges()`, 7; eran 10): dunas de 2,8-4,1 m de alto y 54-109 m de largo. Tres cruzan la playa sobre un
  corredor (X ≈ 292, 512 y 668 m), con un collado de 7-11 m donde pasa el corredor y, a veces, otro; dos separan
  corredores a lo largo (X ≈ 164 y 616 m); dos son medias lunas entre el corredor de fuera y la selva (X ≈ 572 y 708 m).
  Cara empinada (~30°, con el pie suavizado) hacia la salida y bajada suave (4 veces su alto) hacia el mar. Cuatro
  (las tres que cruzan y la media luna de los 572 m) llevan **cornisa** (`LipHeight`): un labio de arena de 95 cm en lo
  alto, donde la cresta pasa del 72 % de su alto (fuera de los collados y las puntas): se salta (la tortuga salta 1,2 m)
  o se pasa por el collado. Malla del generador (`FeatureMesh`, con colisión), asentada en la malla del suelo
  (`MeshGroundZ`).
- **Pozas** (`Pools()`, 7, con agua de verdad; eran 10): cuatro charcas entre las dunas (X ≈ 268, 320, 484 y 544 m;
  tres cortan un corredor) y tres pozas de marea con rocas alrededor en el último tercio (X ≈ 596, 690 y 720 m). El agua
  queda 30 cm por debajo de la arena más baja de su orilla (nunca rebosa: la orilla, 15-29 cm por encima), de 1,5 a
  2,4 m de hondo (`min(2,4 m, 20 %` del radio menor) y orillas de 28° como mucho: se nada y se sale andando. Nadable
  (`ATN_ProcWaterVolume::AddWaterBox`: cajas que siguen su forma, ~100 entre todas), superficie con
  `MI_ProcSeaAnim` (instancia con `DepthRange` 300 y `FoamWidth` 60) y chapuzón al entrar.
- **Trincheras** (`Trenches()`, 2): dos líneas en zigzag de lado a lado de la playa al 28-31 % (X ≈ 219-229 m y
  239-250 m). Canal de 3,2 m con el fondo 60 cm por debajo de la arena entre dos caballones de 45 cm (~1,05 m desde
  dentro: se sale de un salto), tablones por dentro, sacos terreros del lado del mar en grupos de 7 (dos capas, huecos de
  1,6 m), postes y tarimas en el fondo y dos puentes de tablones por línea. El terreno se cava 60 cm hasta 2,1 m del eje
  y vuelve a la arena natural a 6,6 m.
- **Bancos de la selva**: suben 38 m en los 110 m de fuera de la playa y 45 m más hasta 460 m, con colinas; detrás de la
  salida, 42 m y luego 30 m más.
- **Acantilado de roca**: repisa de caras planas en los últimos 24 m (enterrada al empezar, asoma 50 cm sobre la arena
  desde ~16 m antes del filo), filo limpio a 15,5-16 m del agua y pared casi vertical, socavada hasta 2,8 m (nunca
  sobresale del filo: se cae al agua); banda mojada oscura y algas bajo el agua; peñascos al pie de los cabos (fuera de
  donde se cae). En los bancos el acantilado sigue su altura (cabos de 40-80 m). El relieve se apaga antes: el borde se
  lee limpio.
- **Agua de meta**: 11 m de hondo al pie (18 m a 250 m, 35 m lejos). Nadable (`ATN_ProcWaterVolume`, local en cada
  máquina) de debajo de la repisa a 300 m mar adentro y 250 m a cada lado; superficie con `MI_ProcSeaAnim` (hondura y
  espuma a escala: `DepthRange` 1500, `FoamWidth` 160) hasta el horizonte (6 km).
- **Salida con huevos**: en el linde de la selva, entre las raíces de una ceiba colosal (~300 m; tronco de 26 m de radio
  en la base, 52 m detrás de la línea); sus dos raíces tabulares la enmarcan y seis plantas de hojas enormes la techan.
  Cartel «¡A LA META!» (por detrás, «TORTUNAVY») entre dos palos de madera a la deriva. Una fila de **cuatro huevos**
  (los del lobby y la salida del cooperativo: base y tapa de `TNCastleKit`, cada uno con su color) en X = -8 m,
  Y = -15, -5, 5 y 15 m, en un nido de arena (anillo de 1,3 a 2,8 m, con colisión); más filas cada 9 m por detrás si
  hay más de cuatro. Cada tortuga espera dentro del suyo (110 cm sobre el suelo, mirando al mar). Nada del reparto a
  menos de 15 m de la línea (23 m de los huevos; el salto de los huevos cae a ~7 m) y el relieve empieza a los 35 m.
- **Línea del sprint** (`SprintLineX()`, siempre la misma): lo más cerca de la mitad del recorrido donde los 12 sitios
  (4 en fila y dos filas detrás, como en la salida) caen en arena seca y casi llana (menos de 12°, fuera de pozas y
  trincheras y a 6 m de las cornisas): X ≈ 400 m (justo la mitad del recorrido).
- **Meta**: boyas con banderas a cuadros de 12 m en mástiles de 28 m, cada 40 m y a 26 m del filo, unidas por un cabo
  con boyas pequeñas (se mecen); el arco de neumático de la meta del mapa procedural cinco veces más grande (125 m de
  luz, 63 m sobre el agua) a 40 m del filo, con TORTUNAVY hacia la playa y TORTUNABO hacia el mar, banderines hasta dos
  mástiles en los cabos y banderolas por la ladera de la selva en los últimos 250 m. Zambullida
  (`IsCliffJumpZone`): los últimos 7,5 m antes del filo y 40 m sobre el vacío.
- **Selva**: vegetación del mapa procedural a escala: palmeras de 230-300 m (x26-34), árboles de copa, ceibas de
  250-340 m, casuarinas, pándanos y peñascos en rejilla de 48 m (espesa en los primeros 160 m, más clara hasta 460 m);
  una primera fila de palmeras algo más bajas (x22-29) cada 28-42 m; sotobosque de plataneras, palmitos, helechos
  arbóreos y uvas de playa de 50-100 m en los primeros 100 m. Las palmeras de la orilla se inclinan en diagonal sobre la
  arena y hacia el mar (30-60° de través): sus copas cubren los lados de la playa y dejan el centro a cielo abierto (de
  frente, las copas de los dos lados casi se juntaban sobre la arena vista desde arriba). Sin colisión (detrás de los
  muros), sin sombra y con viento solo a menos de 200 m de la cámara.
- **Huecos entre copas**: rejilla de 15 m por la selva; donde el tronco más cercano queda a más de 22 m y la mata más
  cercana a más de 15 m, enredaderas por el suelo (60 %), helechos o helechos arbóreos (50 %), matas de hojas enormes
  (65 %: tallo de 10 m y hojas de 12-18 m), lianas colgadas de tronco a tronco por encima del hueco (troncos a 3,5-11 m)
  y cortinas de lianas colgando de ellas. Mallas propias instanciadas con viento por vértice (se mecen), sin sombra,
  hasta 300-350 m de la cámara. Registro: `[Playa] selva: ... huecos entre copas con ... lianas y matas`.

### Sombras (sombras virtuales)

La selva no proyecta sombra dinámica. Las copas de la orilla, inclinadas sobre la arena, dejaban casi toda la playa a la
sombra aun con el sol alto (no se veía dónde iba a caer la gaviota ni dónde subirse), y la selva gigante, sin Nanite,
cubre muchísimo mapa de sombras virtuales (de ahí el aviso `[VSM] Non-Nanite Marking Job Queue overflow`). La sombra de
la playa la dan sus elementos, el relieve y las rocas:

- **Con sombra**: las teselas de la playa y del pie de los bancos (hasta 60 m fuera de la playa y desde 40 m detrás del
  muro de la salida: las dunas se leen por su sombra), el acantilado y sus rocas, la salida (ceiba colosal, raíces,
  plantas, nido y huevos), el arco de la meta, las mallas del relieve (trincheras con sus sacos y puentes, cornisas y
  rocas de las pozas de marea) y los elementos de la ronda (lo que diga cada clase).
- **Sin sombra**: toda la vegetación de la selva (árboles, sotobosque y las lianas, cortinas y matas de los huecos:
  `ShadowZone` en `BuildJungle` devuelve siempre false), el resto de las teselas de los bancos, las banderolas de la
  ladera, las boyas y sus banderas, el mar, el fondo, el agua de las pozas, los postes y tarimas de las trincheras y las
  huellas.
- **Valle del lobby** (`ATN_LobbyValley`): la vegetación de las montañas lejanas (a más de 155 m del centro) sin sombra;
  el resto, igual (misma semilla y mismas mallas).

### Reparto por ronda (`TNBeachLayout::GenerateRound`, determinista con la semilla y la dificultad)

Ronda 3 (`Docs/Plan_Carrera_Ronda3.md`, tareas 10 y 12): unos **5000 elementos** por ronda con 1200 m (antes ~1000) y
unos **3100** con los 800 m de ahora (ver «Recorrido de 800 m»; las cuotas de abajo, entre paréntesis las de 1200 m), con
todo lleno de estructuras (castillos, fortalezas, decorado militar), enemigos, trampas y ayudas, cofres y muchísima basura
y cachivaches, y con la **dificultad** que elige el general. `GenerateRound(Seed, Difficulty, Out)` corre en otro hilo
(`ATN_BeachRaceGenerator::MakeRoundLayout`): sin estáticos que cambien (las tablas fijas —crestas, pozas, trincheras,
reglas, la rejilla de la arena— se hacen una vez y no se tocan) ni UObjects.

Por pasadas, de lo grande y lo que tiene que verse a lo que rellena:

1. **Castillos con salas** (1-3; 1,7 de media, dos o más en el 60 % de las rondas; con 1200 m eran 2,1 y siempre dos o
   más): el principal entre el 42 y el 58 %, a ±39 m
   del centro, con dos alas en embudo hacia su entrada (barren 18 cm hacia la salida por metro) de decorado grande y
   alambre de espino hasta la selva (a 6 m de los muros): o se atraviesa o se rodea por un único hueco de 14 m con algas
   junto a la selva de un lado. Otro sin alas entre el 12 y el 36 % (el 60 % de las rondas) y otro entre el 62 y el 90 %
   (siempre si no hubo el primero; si no, el 60 %).
2. **Fortalezas colosales** (una; a veces dos con muchas ayudas, `0,5 (ayudas − 1)`): el terreno fijo solo les deja
   sitio entre los 15 y los 120 m y en la franja de los 280-440 m (que suele ser del castillo principal); con 1200 m eran
   tres: 200-265, 490-700 y 870-930 m. Van justo después del castillo principal y antes de los otros castillos con salas,
   con 400 intentos (baratos con la playa aún vacía). Hay una en todas las rondas medidas (en 23 de cada 24 con 1200 m),
   casi siempre entre los 50 y los 150 m.
3. **Pasos de quads**: 1 o 2 franjas (2 o 3 con 1200 m; la cuota por `LengthScale`, con redondeo al azar y una como
   mínimo) que cruzan la playa entera (`Extent` = 28000, Yaw 90°), una más en Difícil y una menos en Fácil (antes de
   escalar), entre el 15 y el 92 % y a 113 m como poco entre ellas (170 m con 1200 m); nada se pone encima (solo
   oscurecen la arena: rodadas), salvo las franjas de caída de las fortalezas, que sí cruzan.
4. **Las demás fortalezas**: 1,3 grandes y 2 medianas en Normal (2 y 3 con 1200 m, por `LengthScale`; por las ayudas:
   2-3 y 3-4 en Fácil), un tramo cada una al azar. Todas (`TryAddFortress`): Yaw 0 ± 10° (su +X, al mar), `SizeScale` 0,94-1,06 (la mediana, hasta 1,03: sus
   torres llegan a 21,3 m por tamaño), **rodeo** (25 m libres entre la muralla y la selva por cada lado,
   `FortressDetour`), 12 m hasta lo que ya hubiera (`FortressPad`) y su **franja de caída** libre y reservada: 16 m de
   ancho de 40 m a 100 m del centro hacia su +X (`FortressLandingZone`: el trampolín potenciado de la cima cae a 45-66 m
   y la catapulta a 80-90 m y aún rebota). La franja se apunta como `JumpArc` desde el borde de la muralla (así el botín
   no dibuja conchitas a través de la fortaleza); en su centro no hay `Summit`: la cima (conchas, cofre y lanzador) la
   pone su clase. Alrededor, sus **guardias** (`PlaceGuards`: 2, 3 o 5 según el tamaño, por los enemigos), sobre todo
   por delante: cangrejos, erizos, lagartos y algún tanque que patrulla a lo largo de la muralla.
5. **Castillos de arena enormes** (6-9 pedidos, 9-14 con 1200 m; salen unos 4 por ronda porque el terreno y las
   estructuras no dejan más sitio; a ±98 m del centro): uno por tramo con sus propios intentos y los que falten, donde
   quepan; la mitad (más con ayudas) con un trampolín delante para subirse.
6. **Zonas de gaviotas y pelícanos**: 3-4 por la raíz de los enemigos y `LengthScale` (2-3 en Fácil, 4-6 en Difícil;
   4-6, 3-5 y 6-9 con 1200 m), una por tramo del 10 al 97 %, en lados alternos, separadas `GullZoneSpacing` (100 m hasta
   tres y pico zonas, algo menos con más; 150 m con 1200 m) y cada una con su tamaño de círculo, distinto de las demás.
   Van por encima.
7. **La tropa de las trincheras**: sacos en las puntas de cada línea, una fila de erizos 18-26 m por delante de la del
   mar, a veces (60 %) un campo de minas más allá y un puesto por detrás de la de la salida.
8. **Filas que obligan a zigzaguear**: hasta 5 (al 11, 35, 62, 78 y 88,5 % del reparto; 7 con 1200 m, al 10, 18, 37, 64,
   72, 80 y 88,5 %), como antes: de selva a selva con un
   hueco de 14-22 m que cambia de sitio, de un tema (militar con alambre, restos de la marea o trastos de playa), algo
   inclinadas y combadas, alguna rendija para apurar, algas en el hueco y, el 60 % de las veces (más con ayudas), una
   catapulta o un trampolín delante. Las piezas de las filas, las alas y los rincones cierran el paso aunque sean
   pequeñas: se tocan.
9. **Calles del cangrejo ermitaño** (8 en Normal, 12 con 1200 m, por los enemigos): tramos rectos de 25-45 m (`Extent`) a ±25° de la
   bajada de la arena, **cuesta abajo** de su extremo -X local (donde espera el ermitaño) al +X
   (`LaneRollsDownhill`: siete puntos, cada uno 2 cm más bajo que el anterior, y 1,2 m o el 2,5 % del largo en total),
   sin cruzar cornisas, con su 60 % central libre (su núcleo: la calle).
10. **Pulgas de arena** (7 en Normal, 10 con 1200 m, por los enemigos) en **claros de arena abierta**: nada en el 75 % central de su
    huella (~9 m de radio) ni pozas ni trincheras, y el claro se reserva.
11. **Rincones escondidos** (4-6; 6-9 con 1200 m): herraduras de decorado grande junto a la selva con el hueco (6-9 m de radio) hacia el
    centro de la playa o hacia el mar y, al fondo del hueco, un **cofre** mirando a la entrada (el centro queda libre
    para el botín).
12. **Puestos militares**: 4-5 puestos (6-8 con 1200 m; red, parapeto de sacos, cajas, bidones, cascos y soldaditos), 1-2
    filas de erizos (2-3) y 2-3 campos de minas (3-4; por las trampas; 5-9 minas cada uno, también por las trampas).
13. **Tanques de juguete** (6 en Normal, 9 con 1200 m, por los enemigos): tramos de patrulla de 20-40 m (`Extent`) de través (Yaw
    90 ± 20°), a 15-35 m de lo militar (redes, erizos y sacos) o de las trincheras (por delante y por detrás de cada
    una), con su 60 % central libre.
14. **Ayudas y trampas destacadas**, en sus huecos (las cuotas de 1200 m, entre paréntesis, por `LengthScale`):
    catapultas 9-12 (14-18), trampolines 9-12 (14-18), plataformas móviles 12-16 (18-24) y palas 7-9 (10-14), por las
    ayudas; conchas que atrapan 12-16 (18-24), plataformas sobre hoyos 8-11 (12-16), cubos rotos 7-9 (10-14) y puertas
    de conchas 5-7 (8-11), por las trampas. Un tramo por pieza: casillas libres del tramo y, en cada una, el sitio que hay
    (`FreeRoomAt`); si cabe su núcleo (más pequeña si hace falta), se prueba. Lo que no cabe en el suyo, por toda la
    playa.
15. **Lanzadores** delante de lo alto (castillos con salas, crestas con cornisa y pozas que cortan un corredor), como
    antes, más con ayudas; **pasarelas guía**: una en la primera mitad (80 %) y otra más adelante (50 %).
16. **Pulpos de poza** (1-2 por poza según su tamaño, por los enemigos): dentro del agua (a menos del 55 % del radio de
    su orilla; `TerrainAllows` les exige el 70 %), separados entre sí. Su origen, en el fondo (`PlacementZ`); el agua de
    su poza, `Pools()[PoolAt(Pos)].Water`.
17. **Cofres** (12 en Normal, 18 con 1200 m, por las ayudas, además de los de los rincones), en sitios especiales barajados: tras una
    concha que atrapa (dos sitios por concha, a 1,5-4 m de su espalda), tras el alambre de las filas y las alas, a la
    espalda de los castillos enormes, las rocas grandes, los troncos y los restos de barco, junto a las trincheras (del
    lado del mar, tras los sacos), en medio de un campo de minas y pasado el arco de salto de los lanzadores (el premio
    de saltar). Su frente (X local) mira a por donde se llega.
18. **Relleno por bandas de 50 m, a la medida de los huecos** (desde los 15 m hasta 30 m antes del filo). Una rejilla
    de lo ocupado de 1 m (`FOccupancy`) da las casillas libres de la banda (`FFreeCells`, junto a la selva y en el
    centro, para respetar la preferencia de lado de cada elemento); en cada intento se toma una, se mide el sitio que
    hay (`FreeRoomAt`) y se elige algo de la fuente que quepa (`PickFitting`: su núcleo con su tamaño más pequeño),
    encogido si hace falta. Por banda, en este orden: **ayudas** `(1,6 + 1,6 t)` por las ayudas, **trampas**
    `(6 + 7 t)` por las trampas y **enemigos** `(4,5 + 5 t)` por los enemigos (con t de 0 en la salida a 1 en el mar;
    los corrillos cuentan como uno), y luego **decorado** hasta la ocupación de `BandCoverage` (del 66 al 76 %: no se
    llega; se acaban antes los huecos, ~50 %), el 80 % del **pequeño** (basura, conchas, cocos, cachivaches: huella de
    menos de 6 m, `SizeScale` 0,6-1,0, a 45 cm de otra pieza pequeña y 1,5 m de lo demás) y el resto del grande.
    La casilla en la que no cabe ni lo más pequeño sale de la lista.
19. **Catapultas garantizadas** (`MinCatapults` = 8, eran 12 con 1200 m: son de un solo uso): si faltan, los trampolines de delante de un
    obstáculo pasan a ser catapultas.
20. **Tapones** de las líneas rectas libres de más de 70 m, como antes (con la playa llena casi no hacen falta: 0-4).

**Dificultad** (`FDifficultyProfile`, `DifficultyProfileOf`; replicada con la ronda en `RoundNet.Difficulty`): los
cupos de cada grupo (`ScaleGroupOf`) se multiplican con redondeo al azar (`Scaled`: la misma media sin escalones) y los
topes por ronda (`Caps`) también. **Ayudas**: catapultas, trampolines, palas, plataformas móviles, fortalezas y cofres
(los rebuscables los escala el botín). **Trampas**: alambre, algas, plataformas sobre hoyos, cubos, puertas de conchas,
conchas que atrapan y minas. **Enemigos**: todos. El decorado no cambia. En Difícil los enemigos «de bulto» (cangrejos,
erizos, lagartos, pulpos y pulgas) se apiñan: su sitio (`Core`) encoge como 1/√2,5 (`CoreFractionOf`; su huella, por
donde patrullan, no cambia).

| | Fácil | Normal | Difícil |
|---|---|---|---|
| Multiplicadores (ayudas / trampas / enemigos) | x1,6 / x0,7 / x0,6 | x1 | x1,4 / x1,8 / x2,5 |
| Elementos | 2822-3286 (media 3096) | 2876-3271 (media 3104) | 2458-2939 (media 2750) |
| Decorado (el pequeño) | ~2820 (~2770) | ~2770 (~2720) | ~2210 (~2170) |
| Enemigos | 57-74 (66; x0,68) | 87-113 (98) | 217-264 (242; x2,5) |
| Trampas que estorban | 99-134 (114; x0,68) | 140-190 (167) | 189-246 (222; x1,33) |
| Ayudas (con fortalezas y cofres) | 88-110 (99; x1,35) | 65-81 (73) | 61-89 (77; x1,05) |
| Cangrejos / erizos / lagartos | 15 / 17 / 10 | 21 / 25 / 16 | 60 / 74 / 42 |
| Ermitaños / pulpos / pulgas / tanques | 4,7 / 7,1 / 4 / 4,7 | 6,3 / 11 / 6,7 / 6,6 | 8,5 / 26 / 16,7 / 7,8 |
| Quads / gaviotas | 1-2 / 2-3 | 1-2 / 3-4 | 2-3 / 4-6 |
| Fortalezas (medianas / grandes / colosales) | 2,8 / 1,4 / 1,25 | 2,0 / 1,0 / 1,0 | 2,3 / 0,9 / 1,1 |
| Cofres | 11-23 (17) | 15-18 (17) | 15-22 (19) |
| Catapultas / trampolines / plataformas móviles | 16 / 21 / 18 | 10 / 14,5 / 15 | 9,6 / 13 / 15,5 |
| Conchas que atrapan / minas / algas | 11 / 36 / 13 | 17 / 52 / 19,5 | 23 / 79 / 15 |
| Castillos con salas / enormes | 1,4 / 4,2 | 1,7 / 4,2 | 1,5 / 3,6 |
| Filas / rincones / piezas militares | 4,2 / 5 / 14 | 4,0 / 4,9 / 16 | 3,6 / 4,6 / 21 |
| Ocupación (núcleos / banda) | media 49 %, primer tercio 52 % | media 48 %, primer tercio 52 % | media 47 %, primer tercio 51 % |
| Línea recta libre más larga | 62-98 m | 54-94 m | 58-94 m |

(24 semillas por perfil, las de `Layout.Rules`, con la prueba compilada fuera del motor; con 1200 m, el puerto a Python
del reparto daba 4834 elementos de media en Normal: ver «Recorrido de 800 m». En juego, el resumen `[Playa] ronda N: ...`).
La playa ya está llena en Normal: en Difícil los cupos llevan el multiplicador entero, pero las trampas y las ayudas no
caben todas (x1,3 y x1,05); los enemigos sí (x2,5). En Fácil sobran huecos y salen x0,68, x0,68 y x1,35. Los cofres de
sitio especial están limitados por los sitios donde caben (unos 12 de los 60-90 que salen): salen casi los mismos en las
tres dificultades. La densidad se ajusta con `BandCoverage`, `FillEnemiesBase`/`Sea`, `FillAidsBase`/`Sea`,
`FillHazardsBase`/`Sea`, `SmallDecorShare`, `SmallDecorPad`, `ChestsBase`, los cupos de `PlaceFeaturedTraps` (todos por
`LengthScale`) y los pesos de `RuleOf`.

**Tiempo del reparto** (en el juego va en otro hilo). Medido con el puerto a Python y contando operaciones: ~8000
intentos de poner algo (antes ~11000 para 1000 elementos), ~0,9 millones de distancias en las cubetas, ~45000 lecturas
de la rejilla de la arena, ~0,6 millones de casillas del paso (con una búsqueda en profundidad que tira hacia el mar;
antes, en anchura, ~10 millones: era lo que más costaba) y ~4800 `SandZ` para los asientos. En C++, unos 60-70 ms por
ronda (el reparto de 1000 elementos tardaba 110-125 ms). La primera ronda de cada proceso hace además las tablas fijas,
entre ellas la rejilla de la arena de 2 m (`SandCache`, ~102000 `SandZ` con `ParallelFor`: unos ms). `Layout.Rules` da
la media en su registro (y avisa si pasa de 150 ms). Con 800 m (unos 3100 elementos) tarda menos: 20 ms de media medidos
fuera del motor, frente a 28 ms con 1200 m.

**Reglas de todas las pasadas**:

- **Sitio**: nada a menos de 15 m de la salida ni de 30 m del filo. El decorado y las trampas llegan hasta 4 m de los
  muros; los enemigos, con su zona de patrulla entera, a 5 m de la selva como poco (salvo los quads). 1,5 m entre
  huellas (`ItemPad`; 45 cm entre dos piezas de decorado pequeño; las piezas de las filas, las alas y los rincones se
  tocan). Cada elemento ocupa su **núcleo** (`Core`): la huella entera en el decorado y las trampas; en los enemigos, su
  cuerpo y su sitio (cangrejo 25 %, erizo 40 %, lagarto 33 %, pulpo y pulgas 40 %, calles y tramos 60 %).
- **Terreno**: nada dentro de las pozas (salvo los pulpos, que solo van dentro), sobre las trincheras ni sobre las
  cornisas (ni las calles de los ermitaños); lo redondo grande (14 m de radio o más) tampoco en lo alto de una cresta.
  Los asientos sin paredes miran la rejilla de la arena (`FastSandZ`).
- **Arcos de salto**: cada catapulta y cada trampolín suelto se pone solo con su arco libre (30 m y 22 m por delante de
  su borde, 10 m de ancho) y lo reserva; las fortalezas, su franja de caída. Los pulpos y los pasos de quads no miran lo
  reservado.
- **Paso libre**: lo que cierra el paso (decorado de 6 m de huella o más —salvo pasarelas y caminitos—, las piezas de
  los muros, alambre, castillos y fortalezas; no el decorado pequeño, ni las trampas que se pisan o se atraviesan, ni
  los enemigos) se infla 4 m en una rejilla de 2 m. Cada pieza que cierra se comprueba al ponerla (una ventana de ±60 m;
  los tapones, en toda la playa) y, en toda la playa, tras cada fila, cada banda y cada pasada que cierra, se quita lo
  último puesto hasta que haya camino: siempre queda un paso de 8 m, aunque sea sinuoso, y entre el decorado pequeño se
  pasa igual.

**Asiento en la arena** (el «sello» de la ronda): los hoyos los traen los elementos (la plataforma, su cráter). Cada
elemento del suelo (no los enemigos ni lo que va por encima) deja liso el suelo bajo su huella a la cota de la arena en
su centro (con el relieve), con un borde de 2,5-16 m hasta la arena natural. En las dunas, `SeatIsGentle` (dentro de
`TryAdd`) descarta el sitio si la arena de alrededor se aparta del nivel más del 40 % del borde: nada abre paredes. Los
pasos de quads no allanan: solo oscurecen la arena (rodadas). Se rehacen solo las teselas tocadas (en paralelo), en el
servidor y en cada cliente con la semilla replicada; `GetGroundHeightAt` lo da sin trazas (`SeatedZ`: con ~5000
asientos, solo los de la casilla de 25 m del punto, `FStampIndex`).

**Puntos interesantes** (`FRoundLayout::Interest`, para el botín; local, con la Z de la arena): `JumpArc` (arco de cada
lanzador, de `Pos` a `To`: libre; también la franja de caída de cada fortaleza, desde el borde de su muralla), `Summit` (lo alto de las crestas y de los castillos, con `Height`), `Shortcut`
(lanzadores delante de un obstáculo y pozas que cortan un corredor, de `Pos` a `To`), `Nook` (hueco de cada rincón),
`Trench` (tramos de las trincheras) y `Detour` (fondo de cada corredor donde se separa). `Source`, el elemento que lo
crea (`Count` si es del terreno).

### Interfaz (`ATN_BeachRaceGenerator`)

- Servidor: `GenerateRound(int32 Seed)` (destruye la ronda anterior, cierra los huevos y replica ya la semilla y la
  dificultad; el reparto, los asientos, el decorado local, los elementos replicados con `SpawnElement` y el botín
  (`TNBeachLoot::SpawnRoundLoot(*this)`) se montan por partes en los fotogramas siguientes: ver «Rendimiento y red»;
  `IsRoundReady` espera a que esté todo), la propiedad `Difficulty` (`ETNProcDifficulty`, la del general al empezar el
  mapa; se replica con cada ronda en `RoundNet.Difficulty` y `GetRoundDifficulty()`), `ClearRound()`, `ResetFinishWater()`,
  `OnTurtleReachedWater(ACharacter*)` / `OnTurtleReachedWaterNative` (una vez por tortuga y ronda, con los pies en el
  agua de meta).
- Servidor: `OpenStartEggs()` (BlueprintAuthorityOnly; lo llama `ATN_BeachRaceGameMode::BeginRace` al soltar a las
  tortugas, también en el sprint): se rompen los huevos de la salida o del sprint (ver abajo).
- Servidor: `SetStartEggsAtSprint(bool bAtSprint)` (BlueprintAuthorityOnly) y `AreStartEggsAtSprint()`: lleva el nido de
  huevos a la línea del sprint final o lo devuelve a la salida, con los huevos cerrados. `GenerateRound` y `ClearRound`
  los devuelven a la salida; el GameMode lo llama justo después de `GenerateRound` (`true` solo en el sprint).
- Servidor: `int32 ClearElementsAround(const FVector& WorldCenter, float Radius)` (BlueprintAuthorityOnly): quita los
  elementos de la ronda cuya huella (un disco o, en los alargados, una cápsula a lo largo de su X) toca el círculo y
  devuelve cuántos: destruye los replicados, quita el decorado local en todas las máquinas (el círculo se replica en
  `DecorCuts`) y sus rebuscables. Lo usa el GameMode para despejar el nido del sprint.
- Cualquier máquina: `IsRoundReady()`, `GetRoundNumber()`, `GetRoundSeed()`, `GetStartTransform(int32)`,
  `GetNumStartSpots()`, `AreStartEggsOpen()`, `GetSprintStartTransform(int32 Index)` (BlueprintPure: como
  `GetStartTransform`, pero en la línea del sprint: 4 en fila y filas detrás, 110 cm sobre la arena de la ronda,
  mirando al mar, en arena seca fuera de pozas y trincheras), `IsFinishWater(P)` (más allá del filo y a 30 cm del agua o
  menos: sirven los pies o el centro de la cápsula), `IsCliffJumpZone(P)`, `GetCliffEdgeDistance(P)` (negativa antes del
  filo), `GetCourseProgress(P)` (0-1), `GetGroundHeightAt(P)` (con el índice de asientos por casillas del reparto),
  `GetSeaDirection()`, `static Find(WorldContext)`, `GetRoundLayout()` (con los puntos interesantes), `GetDecorField()`
  (el decorado local, ver «Rendimiento y red»), `GetElementForItem(int32)` (servidor: el actor del elemento del reparto
  con ese índice; null en el decorado) y `GetLastRoundTimings()`.
- `OnRoundLayoutReady` (delegado C++, `FOnBeachRoundLayoutReady`): al quedar montada entera una ronda en esa máquina
  (`IsRoundReady`): en el servidor, con sus elementos y su botín; en cada cliente, con su decorado local (los elementos
  replicados le llegan solos, según la distancia); y con Preview Round en el editor.
- **Salida con huevos**: `OpenStartEggs` replica que están rotos y desde cuándo (`RoundNet.bStartOpen` y
  `StartOpenTime`, en tiempo del servidor). Las tapas saltan dando vueltas hacia fuera de la fila y algo hacia atrás, de
  las puntas al centro (0,08 s entre una y otra), y al posarse encogen en 0,3 s (cada máquina las anima).
  - **Pausa de 1 s en el huevo** (`TNEggHatch`, `World/TN_EggHatch.*`, común con el cooperativo): cada tortuga que esté
    en la salida (entre el muro de detrás y 15 m por delante) se queda quieta en su huevo roto; se agacha un instante y
    se pone de pie de un estirón (con rebote), se sacude la cáscara (giros rápidos del cuerpo y trocitos de cáscara del
    color de su huevo que saltan y caen, con un crujido) y gira hacia el mar (en la máquina de su jugador, también la
    cámara); justo antes del salto se encoge un poco. Es la malla de la tortuga, que se estira, se aplasta y gira sobre
    su transformación de siempre (se le devuelve al acabar); en caparazón, tumbada o en ragdoll no hay pose.
  - **Lanzamiento**: `TNEggHatch::PauseSeconds` (1 s) después de romperse, a la vez para todas (es una carrera), sale
    lanzada hacia el mar a 10 m/s y 6,5 m/s hacia arriba (~15 m por el aire, ya corriendo). Todo con el reloj del
    servidor: la pose sale de `StartOpenTime` en cada máquina, y la sujetan (`MOVE_None`) y la lanzan el servidor a todas
    y cada cliente a la suya al recibirlo, como en la salida de huevos del cooperativo. Un cliente al que le llegan más de
    2,5 s tarde (1 s de pausa y 1,5 de margen) los ve ya rotos, sin pausa ni salto. Cada ronda nueva los vuelve a cerrar
    (y a llevar a la salida) y quita cualquier pausa a medias.
  - **`TN.Beach.Egg`** (`ReplayStartEggs`, servidor): cierra otra vez los huevos de la línea en la que estén, mete a
    cada tortuga en uno (por orden de jugador), quieta y mirando al mar, y a los 1,5 s los rompe con `OpenStartEggs`. Para
    probar la salida sin empezar otra ronda (también en el sprint). Registro: `[Playa] TN.Beach.Egg: N tortugas otra vez
    en los huevos ...`.
- **Nido del sprint** (`TN_BeachRaceGenerator_Start.cpp`): con `SetStartEggsAtSprint(true)` se replica
  `RoundNet.bSprintEggs` y cada máquina hace en la línea del sprint (`TNBeachLayout::SprintSpot`, sobre la arena con los
  asientos de la ronda, como `GetSprintStartTransform`) las cuatro bases y el anillo de arena que se pisa, igual que el
  de la salida (`SprintNestMesh`, con colisión en el anillo), y muda allí las tapas, cerradas. `OpenStartEggs` las rompe
  igual y lanza a quien esté en la franja de esa línea (la de la salida, movida hasta `SprintLineX`). En la salida se
  quedan las bases abiertas. La ronda siguiente (o volver a jugar) vacía el nido del sprint y devuelve las tapas.
- Chapuzón en cada máquina al entrar una tortuga en el agua de meta o en una poza (gotas, espuma y ondas).
- Si nadie reparte, el servidor reparte una ronda al azar a los 3 s (sin el GameMode de la carrera) o a los 20 s (con
  él). Consola: `TN.Beach.ShowFootprints 1` enseña las huellas del reparto en juego. Registro: `[Playa] terreno fijo:
  ...` y `[Playa] selva: ...` al construir, `[Playa] agua nadable: ...` (pozas y cajas), `[Playa] ronda N: ...` en cada
  ronda (en el servidor, el resumen del reparto con la ocupación por banda; en cada máquina, los tiempos: ver
  «Rendimiento y red») y `[Playa] ronda N: se rompen los huevos de la salida.` (o `del sprint final`) y `[Playa] ronda N:
  los huevos de la salida van a la línea del sprint final (M m).`

### Rendimiento y red (unos 3100 elementos por ronda con 800 m; 5000 con 1200)

Con la ronda 3 el reparto pasó de ~1000 a ~5000 elementos (unos 3100 con los 800 m de ahora), el ≈80 % decorado. Con un actor replicado por pieza el
servidor escucha y los clientes no aguantaban, así que la ronda se monta de otra forma
(`TN_BeachRaceGenerator_Round.cpp`, `ATN_BeachDecorField`, `TN_BeachDecorKit.h`, `ATN_BeachElement` y `TN_BeachLoot`).

**Decorado local e instanciado** (`ATN_BeachDecorField`, `Public/World/Beach/TN_BeachDecorField.h`):

- El reparto es determinista con la semilla y la dificultad (`RoundNet.Seed` y `RoundNet.Difficulty`, replicadas), así
  que el servidor y cada cliente montan el mismo decorado sin replicar nada: nada de la categoría `Decor` es un actor en
  la ronda (`SpawnRoundElements` se lo salta). Lo que crea `TN.Beach.Place` sigue siendo un `ATN_BeachDecor`.
- Un campo por máquina (actor local, `RF_Transient`, pegado al generador y en su espacio) con mallas instanciadas
  jerárquicas (HISM) por elemento y variante (y por pieza de los tramos: pasarela y caminito). Las recetas, la
  colocación (giro, inclinación, hundimiento, tamaño), la colisión y la animación son las de siempre, compartidas con
  `ATN_BeachDecor` en `TNBeachDecorKit` (`Private/World/Beach/TN_BeachDecorKit.h`, definido en `TN_BeachDecor.cpp`):
  una pieza sale igual por los dos caminos. Unos 200-300 componentes en vez de ~4000 actores.
- **Colisión por instancia**: la de cada receta (cajas, esferas y cápsulas en el `BodySetup` de la malla compartida,
  `BlockAll`; la cámara solo choca con lo grande y macizo): se sube, se cubre y se mete debajo igual que antes. Lo que no
  tenía colisión sigue sin ella.
- **Sombra**: lo grande (10 m de huella o más con su tamaño) y los tramos, siempre; lo demás que da sombra, con una
  copia del lote que solo da sombra (sin pase principal ni de profundidad) hasta `ShadowNearDistance` = 90 m de la
  cámara. Todo con `ShadowCacheInvalidationBehavior = Rigid`. Menos instancias que marcar en las sombras virtuales (el
  aviso `[VSM] Non-Nanite Marking Job Queue overflow`). Lo pequeño deja de dibujarse a 60 veces su huella (120-600 m),
  como antes; lo grande, nunca.
- **Lo que se mueve** (medusa que respira, almeja, jirón de la vela, red, banderas y faldón de la red de camuflaje):
  lejos, quieto en su lote (ISM aparte) en la pose del instante 0; cerca de una cámara local (60-200 m según el tamaño,
  las `MaxAnimators` = 40 más cercanas) un componente de una reserva lo mueve con la misma pose que `ATN_BeachDecor`
  (`TNBeachDecorKit::AnimPose`) y su instancia quieta se esconde. Ni en el servidor dedicado ni en el editor.
- `CutCircle` quita piezas en esta máquina (rehace solo los lotes tocados): `ClearElementsAround` lo replica con
  `DecorCuts` (ronda y círculos) y cada máquina lo aplica al tener montado el decorado.
- Consultas: `HasItem(i)`, `GetSearchShape(i)` (la huella para rebuscar: la caja de su malla, girada como ese ejemplar;
  la sombrilla, su pie) y `GetItemBounds(i)`, con `i` el índice del reparto (`GetRoundLayout().Items`). Para buscar
  decorado (el lagarto que se esconde, las conchas de lo alto de los castillos), el reparto y el campo, no actores.

**La ronda por partes** (`StartRoundBuild`/`TickRoundBuild`, en el servidor y en cada cliente):

1. `GenerateRound` (servidor) replica la ronda ya; cada cliente empieza al recibirla. El reparto
   (`TNBeachLayout::GenerateRound`, lógica pura) y las alturas de las teselas del terreno con sus asientos se calculan en
   otro hilo (`UE::Tasks`), con todo copiado.
2. En el hilo de juego, `TN.Beach.BuildBudgetMs` (6 ms) por fotograma: se suben las teselas (con la colisión cocinada
   al momento), se monta el decorado local y, en el servidor, se crean los elementos replicados (unos pocos por
   fotograma) y, al final, el botín (`TNBeachLoot::SpawnRoundLoot`, con todo en su sitio para las trazas de las
   conchas). El nido del sprint se rehace con los asientos nuevos si se hizo antes de llegar el reparto.
3. `IsRoundReady` es false hasta que está todo (el GameMode ya lo espera; su límite son 20 s; después espera a que cada
   cliente diga que tiene montada la suya, `UTN_BeachRoundSyncComponent`, como mucho 12 s: ver «Seguridad: nunca bajo el
   mapa»). Registro en cada máquina:
   `[Playa] ronda N: reparto X ms, decorado Y ms, actores Z · asientos ... ms, elementos ... ms, botín ... ms · ...
   piezas de decorado instanciadas · F fotogramas, T ms de principio a fin (servidor|cliente).` El reparto va en otro
   hilo: sus milisegundos no congelan. `TN.Beach.AsyncBuild 0` lo hace todo en el mismo fotograma (como antes, para
   comparar).
4. `GetGroundHeightAt` usa el índice de asientos por casillas del reparto (`TNBeachLayout::SeatedZ`): con ~5000 (3000 ahora)
   asientos, mirarlos todos en cada consulta (los enemigos, a cada paso) era caro.

**Lo que se sigue replicando** (`ATN_BeachElement::ApplyRoundNetProfile`, que aplica `SpawnElement` antes de
`FinishSpawning` y manda sobre lo que ponga el constructor de cada clase):

- **Relevancia por distancia** en vez de toda la playa: `GetNetRelevanceDistance()` = 260 m + 3 veces lo que ocupa
  (huella y medio largo), hasta 450 m (una mina, ~270 m; una catapulta, ~290 m; el castillo con salas y las fortalezas,
  400-450 m; los pasos de quads, 450 m). La niebla empieza a los 300 m. Las estructuras enormes y quietas (20 m de huella
  o más: castillos con salas y fortalezas, `WantsAlwaysRelevant`) siguen siendo relevantes en toda la playa: son pocas,
  duermen y así cada cliente las monta en la espera de la ronda y no a mitad de carrera (sus mallas son las más caras)
  ni aparecen de golpe a lo lejos. Antes, el generador ponía `bAlwaysRelevant` en
  todo y la base cortaba a 1600 m: con ~1500 actores replicados, el servidor los miraba todos para cada cliente y cada
  cliente los tenía todos (con sus mallas) desde la salida; ahora le llegan según avanza. Nada del GameMode (servidor:
  los tiene todos) ni de las pantallas (el recuento va por el PlayerState) depende de ver elementos lejanos.
- **Dormancy** en lo quieto (`WantsNetDormancy`: todo menos los enemigos): `DORM_Initial` al aparecer (manda `Spec` y su
  estado una vez y se duerme) y como mucho 2 Hz (`DormantNetFrequency`). Cada trampa ya llama a `ForceNetUpdate()` tras
  cambiar su estado replicado; `ATN_BeachElement::ForceNetUpdate` la despierta (`DORM_Awake`), lo manda ya y la vuelve a
  dormir 3 s después del último cambio (el patrón de los rebuscables). Los multicast llegan igual (abren canal) y la
  destrucción de un actor dormido también. **Regla para las clases nuevas: cada cambio de una propiedad replicada, seguido
  de `ForceNetUpdate()`; lo que se mueve solo por la red, `WantsNetDormancy() = false`.**
- Enemigos: sin dormancy (se mueven), con su frecuencia de siempre (10 Hz cerca, menos lejos) y la misma relevancia por
  distancia. El cofre (`ATN_BeachChest`) no se replica: su `ATN_BeachChestSpot` va aparte (relevante a 400 m).

**Rebuscables ligeros** (`TN_BeachLoot`):

- **Registro de puntos** (servidor, `UTN_BeachLootSubsystem`): uno por pieza de decorado elegida (las reglas de siempre:
  `SearchChance`, uno por corrillo), con la huella de su malla en el campo de decorado. Hasta 240 por ronda y 50 por
  sexto del recorrido (360 y 75 con 1200 m; ×1,6 en fácil y ×1,4 en difícil, `SearchSpotScale`).
- **Estado replicado compacto**: `ATN_BeachSearchRegistry` (uno por mundo, siempre relevante y dormido salvo al cambiar)
  con `FTNBeachSearchNet`: ronda, tirada, número de puntos y un bit por punto (usado o libre). Cientos de rebuscables,
  unos pocos bytes.
- **Actor solo cerca**: cada 0,2 s el servidor crea el `ATN_BeachSearchSpot` de siempre (mantener E, aro, sonido, 70 % de
  suerte, saltito del objeto, chispitas y anillo dorado: nada cambia) en los puntos libres a menos de 50 m del borde de
  alguna tortuga y quita el de los que ya nadie tiene a menos de 70 m, salvo si alguien rebusca o si lo que soltó sigue
  sin coger. Rebuscado, el punto queda usado y no vuelve a salir. El actor es relevante a 90 m.
- **Montículos de arena** (ver «Botín en la playa», «Montículos de arena»): el registro replica también dónde va el
  montículo de cada punto (8 bytes por punto, una vez por ronda) y cada máquina los monta instanciados.
- Los objetos sueltos (35-45 con 800 m, 55-65 con 1200; dormidos, 150 m) y las conchas de puntos (hasta ~230 y ~350
  con 1200 m, 1 Hz; las del cofre, aparte) siguen
  siendo actores: su coste es pequeño al lado de lo de antes.

**Consola**: `TN.Beach.Perf` (en la ventana donde se escribe y, en PIE, también el servidor): tiempos de la última
ronda, decorado local (piezas, instancias fijas, con colisión y con sombra, partes que se mueven, copias de sombra,
componentes y partes animándose), actores de la playa (por categoría, con dormancy, siempre relevantes y relevancia
media), rebuscables (puntos, usados y actores ahora), objetos sueltos, conchas y la lista de red (replicados, activos y
dormidos). `TN.Beach.BuildBudgetMs` y `TN.Beach.AsyncBuild`.

**Cifras esperadas** (por medir con 2 jugadores: FPS en PIE, ancho de banda y avisos de VSM; entre paréntesis, las de
1200 m): ~2600 piezas de decorado (~4000) en ~200-300 componentes (antes, un actor replicado cada una); ~700-1000
actores replicados (~1000-1500), casi todos dormidos, y en cada cliente solo los que tiene a menos de 260-450 m; hasta
240 puntos rebuscables (360) con unos 5-20 actores a la vez; la ronda
montada en ~0,5-1,5 s repartidos (la primera, más: las recetas se montan una vez por partida) sin fotogramas de más de
unos 6-15 ms salvo al montar una receta grande o al destruir la ronda anterior.

### Nivel y capturas

- `Scripts/build_beach_race.py` (en el editor, con el C++ compilado) crea o abre `/Game/Maps/Run/LVL_BeachRace`: sol a
  la espalda de la salida, cielo, luz del cielo, niebla suave (desde 300 m), el generador «PlayaCarrera» en el origen,
  cuatro `PlayerStart` en las salidas (los que ya existan se llevan a la salida de ahora: la cota cambia con el largo
  del recorrido) y `TN_BeachRaceGameMode` en World Settings; lo guarda. El sol, si ya existe, no se toca.
- Para ver el reparto sin jugar: en el generador, Details > Beach|Editor > **Preview Round** (con `Editor Seed` o al
  azar) y **Clear Preview**; no se guarda con el nivel. Las huellas: amarillo decorado, naranja trampas, rojo enemigos,
  morado quads, celeste gaviotas, marrón pasarela guía, rosa los castillos (con salas y sus alas, y los enormes), verde
  azulado las filas, oliva lo militar, blanco azulado los lanzadores, marrón oscuro los rincones, gris los tapones, rosa
  oscuro las fortalezas, dorado los cofres y granate los enemigos de sitio fijo (pulpos, ermitaños, pulgas, tanques y
  guardias), con una flecha hacia su X local. La dificultad de Preview Round, la del generador (`Difficulty`). Los puntos interesantes: arcos de salto en blanco y atajos en fucsia (tiras) y rincones,
  cimas, trincheras y caminos alternativos en rombos (marrón, amarillo, oliva y azul).
- Qué capturar, con `vista(nombre)` del script (`BEACH_SKIP_MAIN = True` antes del `exec`): `salida`, `planta` (con
  Preview Round: el reparto entero y sus huellas), `castillo`, `acantilado`, `meta`, `selva` y las nuevas `huevos` (la
  fila en su nido), `dunas` (a ras de arena: relieve, corredores y crestas), `trinchera`, `poza`, `cresta` (la cornisa
  desde la cara empinada y un collado), `huecos` (lianas y hojas entre las copas) y `sprint`. Las cotas de las vistas
  salen del terreno fijo medido (con 800 m, a partir de las de 1200 m por 2/3 y del `GroundZ` de `TN_BeachLayout.h`): si
  alguna queda enterrada o alta, se retoca su Z. En juego: la salida (tapas
  y salto de las cuatro), un trampolín delante de un castillo, cruzar a nado una poza, pasar una trinchera y una
  cornisa, y el registro `[Playa] ronda N` (ocupación por banda y tiempos).
- Pruebas: `Automation RunTests Tortunabo.Beach`: `Terrain` (salida en la zona del salto de los huevos, 15 m libres
  por delante, línea del sprint a 30 m de la mitad con sus 12 sitios secos y llanos, meta, zambullida y asientos),
  `Relief` (pendiente máxima < 42°, desviación > 90 cm y más del 10 % por encima de 10°; corredores más hondos, crestas
  con cornisa, pozas sin rebosar, hondas, de orilla suave y nadables; trincheras cavadas junto al eje y con la arena
  natural lejos de todos los canales), `Layout.Determinism` (con cada dificultad, más de 2300 elementos —3500 con 1200 m—, iguales dos
  veces, puntos interesantes incluidos; otra dificultad, otro reparto), `Layout.Rules` (24 semillas en Normal y 8 en
  Fácil y en Difícil: límites, sin solapes, asientos suaves, arcos y franjas de caída libres, castillos, quads y
  gaviotas según la dificultad, cupos de elementos y enemigos, cangrejos, 3 catapultas o más (6 con 1200 m), trampolines, plataformas,
  filas, militar, ocupación ≥ 42 % (primer tercio ≥ 38 %), puntos interesantes, como mucho 2 filas con líneas rectas de
  más de 112 m, más huella hacia el mar, fortalezas con rodeo, al mar y en su huella —la colosal en el 80 % de las
  rondas y los tres tamaños en el 75 %—, cofres en sitios especiales, pulpos en las pozas, ermitaños cuesta abajo,
  pulgas en su claro, tanques junto a lo militar; y el tiempo medio del reparto en su registro) y `Layout.Difficulty`
  (los multiplicadores y lo que sale de verdad en 8 semillas: enemigos x0,75 o menos en Fácil y x2 o más en Difícil,
  trampas x0,8 / x1,2, ayudas x1,25 en Fácil y al menos las de Normal en Difícil, y cofres no menos del 90 % que en
  Normal). Los umbrales de cantidad son los de 1200 m por `LengthScale` (en la cabecera de `TN_BeachLayoutTest.cpp` y
  junto a cada uno, con lo medido); los de calidad —paso libre, sin solapes, líneas rectas, ocupación por metro cuadrado
  y terreno— no cambian.

## Reparto del trabajo (agentes)

| Parte | Archivos |
|---|---|
| Contrato común | `Public/World/Beach/TN_BeachTypes.h`, `TN_BeachElement.*`, `Game/TN_BeachRaceGameState.*`, este documento |
| Menú, flujo y reglas | `UI/Menu/MP_MainMenuWidget.*`, `Lobby/TN_HQGameMode.*` (viaje), `Game/TN_BeachRaceGameMode.*`, aturdir en vez de morir; cuenta atrás tras la primera, medias conchas, enganche del gusano de arena y sprint final (con el nido de huevos en su línea, `World/Beach/TN_BeachRaceGenerator_Start.cpp`, y su interfaz en `UI/Race/`); salto final y chapuzón (`World/Beach/TN_BeachFinishSplash.*`, `TN_BeachSplashSynthComponent.*`); misión con el general (`Lobby/TN_LobbyMission.*`, pestaña «Misión» de `UI/Briefing/TN_BriefingWidget.*`, pizarra de `Lobby/TN_GeneralBriefing.*`, `Lobby/TN_ProcModeSelector.*`) |
| Terreno, nivel y reparto | `World/Beach/TN_BeachRaceGenerator.*`, `TN_BeachLayout.h`, `Scripts/build_beach_race.py` |
| Decorado gigante | `World/Beach/TN_BeachDecor.*`, `TN_BeachPropMeshes.h` |
| Trampas e interacciones | `World/Beach/TN_BeachBarbedWire.*`, `Seaweed`, `WobblyPlatform`, `BrokenBucket`, `SpadeRamp`, `SandDungeon`, `ShellGate`, `ClamTrap`, `MovingPlatform`, `Catapult`, `Trampoline`, `TN_BeachRideKit.h`; fortalezas y lanzadores potenciados: `TN_BeachFortress.*`, `TN_BeachFortressKit.h`, `TN_BeachBoostKit.h` |
| Enemigos y tormenta | `World/Beach/TN_BeachGiantCrab.*`, `SeaUrchin`, `Lizard`, `QuadLane`, `GullZone`, `TN_BeachStorm.*` |
| Recuento, campeón y podio | `UI/Race/*`, poses de celebración en `Player/TN_TurtleAnimInstance.*`, escena del podio |
| Botín en la playa y brillo de lo que se coge | `World/Beach/TN_BeachLoot.*`, `TN_BeachLootShells.cpp`, `World/TN_PickupGlowComponent.*`, `Private/World/TN_LootGlowKit.h`; ganchos en `World/ProcMap/TN_ProcSearchSpot.*` y `World/TN_PickupInteractableBase.*` |
| Decorado militar y minas | `Private/World/Beach/TN_BeachMilitaryMeshes.h` (lo engancha `TN_BeachPropMeshes.h`), `World/Beach/TN_BeachMine.*`, `TN_BeachMineSynth.*` |

## Flujo de la carrera: menú, lobby, rondas y aturdimiento

### Menú principal y lobby

- **Menú** (`UMP_MainMenuWidget`): los mismos tres botones del Blueprint (mismo estilo; solo cambian los textos) en dos
  pasos. «Crear partida» → «Cooperativo» / «Carrera» / «Volver». «Unirse» no pregunta nada: el modo lo decide el
  anfitrión. El modo va a `UMP_GameInstance::SelectedProcMode` (`HostSessionWithMode`) y sobrevive a los viajes.
- **Lobby** (`ATN_HQGameMode::BeginMatchTravel`): Carrera → `BeachRaceMapPath` (`/Game/Maps/Run/LVL_BeachRace`; si el
  nivel aún no existe, error en el log y se juega la carrera del mapa procedural). Cooperativo y 2vs2 →
  `LVL_ProcMap`; Clásico → `LVL_Run`. En el lobby, el modo y la dificultad se cambian hablando con el General
  Galápago (pestaña «Misión», solo el anfitrión: ver «Misión con el general del lobby»); también en el menú, con
  «Cambiar de modo» al acabar la carrera y con `TN.Mode Coop|Race` en el anfitrión. Los selectores del lobby
  (`ATN_ProcModeSelector`), si el nivel los tiene, usan la misma lógica: «MODO: CARRERA» también va a la playa.

### Misión con el general del lobby (modo y dificultad)

Para no tener que subir a los selectores: al hablar con el general (`ATN_GeneralBriefing`) la sesión informativa
(`UTN_BriefingWidget`) se abre en la pestaña **«Misión»**, la primera de cinco («Misión», «Cómo se juega», «Modos de
juego», «Reglas» y «Controles»).

- **Qué se elige**: el **modo**, Cooperativo o Carrera (las pastillas; debajo, una línea de cada uno, las mismas que da
  el menú al crear partida), y la **dificultad**, Fácil, Normal o Difícil (con lo que cambia en el cooperativo; la playa
  de la carrera es siempre la misma). La elegida va en coral; abajo, la etiqueta «Orden del día: CARRERA · NORMAL» y,
  al cambiar, el general contesta en su bocadillo («¡Carrera! Todas contra todas hasta el agua…»).
- **Quién**: solo el anfitrión (`TNLobbyMission::CanLocalPlayerChoose`: servidor escucha o partida sola). Los demás ven
  lo mismo con las pastillas apagadas y el aviso «El modo y la dificultad los elige el anfitrión…»; si el anfitrión lo
  cambia mientras lo miran, se repinta y el general anuncia la nueva orden del día. Sin RPC: la interfaz del anfitrión
  corre en el servidor (como la pantalla del campeón) y un cliente no puede cambiarla.
- **Dónde vive**: donde siempre, en `UMP_GameInstance::SelectedProcMode`/`SelectedProcDifficulty` del anfitrión, que lee
  `ATN_HQGameMode` al viajar. Para que todos lo vean al momento se replica en el general (`MissionMode`,
  `MissionDifficulty`, con `ForceNetUpdate`), que lo escribe con tiza en una **pizarra** en su caballete junto a la mesa
  («ORDEN DEL DÍA / MISIÓN: CARRERA / DIFICULTAD: NORMAL»), y en las etiquetas de los selectores si los hay.
- **Lógica común** (`Lobby/TN_LobbyMission.h`, `namespace TNLobbyMission`): `MenuModes` (Coop y Carrera) y
  `Difficulties`, `ModeName`, `DifficultyName`, `ModeBlurb`, `DifficultyBlurb`, `NextSelectorMode` (el ciclo del
  selector: Clásico → Coop → Carrera → 2vs2 solo con cuatro), `CanLocalPlayerChoose`, `GetHostMode`/`GetHostDifficulty`
  y `SetMode`/`SetDifficulty` (anfitrión: cambian la GameInstance y llaman a `SyncLobby`, que copia la misión en el
  general y en los selectores; 2vs2 solo con cuatro jugadores). La usan el menú principal (textos de los modos), el
  general, los selectores (`ATN_ProcModeSelector::SyncFromGameInstance`) y `TN.Mode`.
- **Controles**: ratón, clic en la opción. Teclado y mando (anfitrión, en «Misión»): ↑/↓, W/S o cruceta arriba/abajo
  eligen la fila (modo o dificultad, marcada con «»»); ←/→, A/D o cruceta izquierda/derecha cambian la opción; las
  pestañas siguen con Q/E, Tab o LB/RB (y 1-5); Esc, Intro, A o B cierran. En el resto de pestañas, lo de siempre.

### GameMode y fases

`ATN_BeachRaceGameMode` hereda de `ATN_RunGameMode`, no de `ATN_ProcMapGameMode`: el bucle de rondas de este es privado
y va atado a `ATN_ProcMapGenerator` (lo crearía si el nivel no lo tiene). De la base se reutiliza la espera a los
jugadores tras el viaje, la meta (puesto, puntos y espectador), la vuelta al lobby (`LobbyReturnMapPath`) y el flujo
replicado del que tiran el huevo de la pantalla de carga («¡ADELANTE!») y la música de fin de partida. El bucle de
rondas es el de la carrera del mapa procedural, adaptado. GameState: `ATN_BeachRaceGameState` (`ProcMode = Race`,
`RoundTarget` = `WinsToWinMatch` = 3).

| Fase (`RacePhase`) | `MatchFlowState` | Qué pasa | Tiempo |
|---|---|---|---|
| `Waiting` | `WaitingForPlayers` | `GenerateRound(semilla)`; tortugas nuevas en la salida, cada una dentro de su huevo (escalonada; los sitios rotan cada ronda), quietas. Desde la ronda 2 (y en el sprint y en «Volver a jugar»), cada pantalla lo pasa tapada por el huevo negro con «RONDA N» y su 3, 2, 1 (ver «Entre ronda y ronda») | ≥ 2 s (`MinPreRoundSeconds`; como mucho 20 s esperando al generador y 12 s más a que cada cliente tenga montada su parte) + cuenta atrás de 3 s (`CountdownValue` y `PhaseSecondsLeft`), salvo en la primera ronda tras el viaje, cuya cuenta es el huevo |
| `Racing` | `InProgress` | al empezar se rompen los huevos y las tortugas salen lanzadas hacia el mar (`OpenStartEggs`); la primera que toca el agua de meta gana la ronda (`RoundWinner`) y arranca la cuenta atrás (`FinishCountdown` = `Counting`): quien llega dentro, media concha (`RoundHalfShells`). Cada una se queda a la vista en el agua con su chapuzón 0,8 s (`FinishSplashHoldSeconds`), con la postura de la zambullida congelada, y luego pasa a espectadora; su puesto va replicado (`RoundArrivals`) y en su pantalla se cierra el huevo negro con «Has quedado X.º» (ver «Llegada al agua»). Al acabar la cuenta, `TimeUp`: a cada una que no ha llegado se la come un gusano de arena (`ATN_BeachSandWorm::EatTurtle`) y todas quietas `EatSeconds` + 0,6 s (`SandWormMarginSeconds`); sin nadie a quien comer, o con todas dentro (`AllIn`), 1,6 s (`TimeUpHoldSeconds`), y en cualquier caso hasta que acaba la pantalla del puesto de la última en llegar (`ArrivalScreenHoldSeconds` = 3,3 s desde su llegada). La tormenta sigue hasta entonces | cuenta de 10 s (`FinishCountdownSeconds`); límite 9 min (`RoundTimeLimitSeconds`) sin nadie en el agua, con reloj en el HUD el último minuto y avisos a los 60 y 30 s: gana la más cerca del mar (con su «¡TIEMPO!» y el motivo en la cinta, `RoundEndReason = TimeLimit`) |
| `RoundResults` | `Countdown` | recuento: `RoundWinner` (entera), `RoundHalfShells` (medias) y `RaceShellHalves`; todas quietas | 7 s (`RoundResultsSeconds`) |
| `SprintIntro` | `Countdown` | empate en lo más alto con `WinsToWinMatch` conchas o más: `bSprintFinal` y `SprintFinalists`; título «¡SPRINT FINAL!» | 5 s (`SprintIntroSeconds`) |
| `Waiting` → `Racing` (sprint) | `WaitingForPlayers` → `InProgress` | las demás, a espectadoras; reparto nuevo con el nido de huevos en la línea del sprint (`SetStartEggsAtSprint`); el nido despejado (`ClearElementsAround`) y cada finalista, tortuga nueva dentro de su huevo (`RestartPlayerAtTransform` en `GetSprintStartTransform(i)`), quietas; 3, 2, 1 y los huevos se rompen (`OpenStartEggs`); la primera en el agua es campeona (sin cuenta de 10 s ni gusanos) | límite 4 min 30 s (`SprintTimeLimitSeconds`): «¡TIEMPO!» con su motivo 1,6 s y gana la más cerca del mar |
| `Champion` | `Results` | `Champion` y `Podium` (por conchas en medias; a igualdad, quien ganó una ronda más tarde); se espera al anfitrión | sin límite |

- El recuento sale siempre, también el de la tercera concha; el campeón (o el sprint) va después. Tras el sprint, el podio
  sale directamente.
- En `Champion`, `CountdownValue` se queda en 99 (no hay cuenta): así la música de fin de partida no se funde ni se
  cierra el huevo. El HUD de siempre (`UTN_CoopFlowHUDWidget`) enseña su panel de resultados en `Results`: la pantalla
  del campeón tiene que taparlo u ocultarlo en la playa.
- **Pantalla del campeón** (para la interfaz): `ATN_BeachRaceGameMode::RequestChampionChoice(this, Choice)` con
  `ETNBeachChampionChoice::PlayAgain`, `ChangeMode` o `Quit`, y `CanLocalPlayerChoose(this)` (true en el anfitrión con la
  partida acabada). Sin RPC: en servidor escucha la interfaz del anfitrión corre en el servidor. En un cliente solo
  `Quit` hace algo (sale él solo al menú).
  - **Volver a jugar**: conchas a cero y ronda 1 en la misma playa, sin viajar (con cuenta atrás).
  - **Cambiar de modo**: `SelectedProcMode = Coop` y vuelta al lobby; al ponerse listos, cooperativo.
  - **Salir**: el anfitrión vuelve al menú y la partida se cierra (los clientes vuelven al menú con «El host abandonó la
    partida»).
  - Al elegir, `CountdownValue` = 1 durante 1,2 s: el huevo se cierra en todas las pantallas, la música se funde y
    luego se viaja.
- **Desconexiones**: si alguien se va en plena carrera, la ronda sigue con las demás (gana la primera que toque el agua);
  si se va la última que corría durante la cuenta atrás, «¡TIEMPO!» sin esperar. Quien llegó conserva su concha mientras
  exista su PlayerState. Quien entra con la ronda en marcha sale desde la salida (en el sprint, mira); entre rondas, se
  queda quieta. En el sprint, si solo queda una finalista, es campeona (sin ninguna, la de más conchas). El podio
  conserva los PlayerState mientras existan (después llegan como null). Si se va el anfitrión, se acaba la partida.

### Cuenta atrás tras la primera y medias conchas

Lo que pidió el usuario: que la ronda no se cierre al llegar la primera, sino que salga una cuenta atrás grande de 10 s
y quien llegue dentro se lleve media concha.

- **Llegadas** (`ATN_BeachRaceGameMode::MarkPlayerFinished`, `Arrivals`): el puesto se decide en el instante del
  contacto. La primera: `RoundWinner`, concha entera (2 medias) y arranca la cuenta (`FinishCountdown` = `Counting`,
  `FinishCountdownEndTime` en hora del servidor y `FinishCountdownSeconds`, replicados; la interfaz calcula lo que queda
  con `GetFinishCountdownLeft`). Las siguientes, dentro de la cuenta: media (`RoundHalfShells`, en orden). Nada se
  cierra: la tormenta, los enemigos y el resto siguen, y quien aún corre puede llegar.
- **A la vista y luego espectadora**: cada una sale del caparazón, del aturdimiento y de quien la llevara y se queda en
  el agua `FinishSplashHoldSeconds` (0,8 s) con su chapuzón; pasado ese margen (`SettleArrivals`, en `WatchRacers`)
  pasa por la meta de la base (`ATN_RunGameMode::MarkPlayerFinished`: puesto, puntos, la oculta y
  `MovePlayerToSpectator`, la vía normal del espectador sobre la que va el fantasma). En su pantalla, todo eso pasa tapado
  por el huevo negro con «Has quedado X.º» (ver «Llegada al agua»).
- **Fin** (`FinishTimeUp`): al acabar la cuenta, o si ya no queda nadie corriendo (han llegado todas, se han ido o
  miran), `TimeUp` o `AllIn`: «¡TIEMPO!» (o «¡TODAS AL AGUA!»), todas quietas, la tormenta se para y, a los 1,6 s
  (`TimeUpHoldSeconds`; como poco hasta que acaba la pantalla del puesto de la última en llegar,
  `ArrivalScreenHoldSeconds` = 3,3 s desde su llegada), el recuento (`EndRound`): reparte `RaceShellHalves` (+2 la
  primera, que suma también `RoundWins`; +1 cada media) y pasa detrás del recuento por la meta de la base a quien aún
  estaba en el agua.
- **Gusano de arena** (lo pidió el usuario): solo cuando la cuenta llega a 0 (`OnFinishCountdownEnd` →
  `FinishTimeUp(ETNBeachRoundEnd::Countdown)`; ni con «¡TODAS AL AGUA!», ni en el límite de la ronda, ni en el sprint), en el servidor,
  `FeedSandWorms` saca a cada tortuga que aún corría de quien la llevara, del mareo, del derribo y del caparazón y llama a
  `ATN_BeachSandWorm::EatTurtle(Tortuga)` (la clase es de otro agente: sale de la arena, se la come y la deja quieta,
  sin control y oculta). Si ha salido algún gusano, el recuento espera `ATN_BeachSandWorm::EatSeconds` (3,2 s) +
  `SandWormMarginSeconds` (0,6 s); si no, los 1,6 s de siempre. Mientras, nada la toca: `WatchRacers` se la salta,
  `MarkPlayerDead` y el rescate no actúan con la ronda cerrada y `TNBeach::StunTurtle` / `KnockDownTurtle` no hacen nada
  con una tortuga en la boca de un gusano (`IsBeingEaten`). Nadie muere: la ronda siguiente las vuelve a crear en la
  salida (`CleanupRoundActors` y `PlacePlayersAtStart`). En pantalla, «¡TIEMPO!» se encoge y sube tras el golpe y quien
  corría se queda sin etiqueta, para ver el bocado.
- **Medias conchas**: `ATN_CoopPlayerState::RaceShellHalves` (replicado; 2 = una entera). `RoundWins` sigue contando
  las rondas ganadas enteras (desempate del podio). La partida es a `WinsToWinMatch` = 3 conchas (6 medias). La música
  (`UTN_MatchMusicSubsystem`) le pasa al director las medias (una media suena a ronda ganada) y, con campeón, solo él
  gana.

### Sprint final de desempate

- **Cuándo** (`AfterRoundResults`): la más alta en medias con `WinsToWinMatch` × 2 o más es campeona si está sola; si
  empatan varias ahí arriba (p. ej. 3 y 3), `EnterSprintIntro(finalistas)`: `bSprintFinal`, `SprintFinalists` y la fase
  `SprintIntro` (5 s, `SprintIntroSeconds`): título «¡SPRINT FINAL!» con las caras, «VS», fanfarria y confeti.
- **Preparación** (`StartSprint`, ronda nueva con `bSprint`): en la carrera no se muere ni se vuelve a la vida (volver
  desde un huevo con `TNGhost::ReviveIntoEgg` es, de momento, solo del cooperativo), así que el sprint sale como cada
  ronda, del nido de huevos, pero en su línea. Las que no corren se quedan sin tortuga y pasan a espectadoras por la vía
  normal (`SendOutOfSprint`: `bHasFinishedRun` y `MovePlayerToSpectator`; el fantasma es del agente del espectador) y las
  tortugas de las finalistas se cambian por otras nuevas al ponerlas en su huevo (sin limpiar todos los peones como
  entre rondas, para no quitar su vista a las espectadoras). Reparto nuevo (`GenerateRound`) y el nido a la línea del
  sprint (`SetStartEggsAtSprint(true)`); al estar listo (`PlaceSprintFinalists`), se despeja el nido
  (`ATN_BeachRaceGenerator::ClearElementsAround`, con `SprintClearMargin` = 15 m alrededor de sus sitios) y cada
  finalista, por su orden, recibe una tortuga nueva dentro de su huevo (`RestartPlayerAtTransform` en
  `GetSprintStartTransform(i)`, apoyada en el suelo), quieta durante el 3, 2, 1 (`PreRaceCountdownSeconds`). Al dar la
  salida, `OpenStartEggs`: las tapas saltan y las finalistas salen lanzadas hacia el mar. La tormenta sale por detrás
  del nido.
- **Carrera**: solo las finalistas; la primera en el agua es campeona (sin cuenta de 10 s ni gusanos: su chapuzón y el
  podio, `CompleteSprintWin`). Límite 4 min 30 s (`SprintTimeLimitSeconds`): la finalista más cerca del mar. Si una se queda
  sin tortuga al dar la salida, vuelve a su huevo.
- **Después**: la ronda siguiente o volver a jugar devuelven el nido a la salida (`GenerateRound` y
  `SetStartEggsAtSprint(false)`).

### Lo que el GameMode usa del generador y de la tormenta

- `ATN_BeachRaceGenerator`: `void GenerateRound(int32 Seed)`, `bool IsRoundReady() const`,
  `FTransform GetStartTransform(int32 PlayerIndex) const` (mirando hacia el mar; la cápsula se apoya en el suelo que
  haya debajo) y `bool IsFinishWater(const FVector& WorldLocation) const`. No hace falta aviso de meta: el GameMode
  mira `IsFinishWater` de cada tortuga diez veces por segundo. Si otra pieza quiere dar la meta, que llame a
  `GetAuthGameMode<ATN_RunGameMode>()->MarkPlayerFinished(PC)`.
- También del generador: `OpenStartEggs()` al dar la salida (también en el sprint), y para el sprint de desempate
  `SetStartEggsAtSprint(bool)` (el nido a su línea), `GetSprintStartTransform(int32)` (sitios a mitad de la playa, en
  arena seca y llana) y `ClearElementsAround(WorldCenter, Radius)` (quita los elementos de la ronda que tocan el círculo;
  devuelve cuántos).
- Del gusano de arena (`World/Beach/TN_BeachSandWorm.h`, de otro agente): `ATN_BeachSandWorm::EatTurtle`, `EatSeconds` e
  `IsBeingEaten`.
- `ATN_BeachStorm` (por nombre): se crea al dar la salida, `StormSpawnBehind` (30 m) detrás de ella y mirando hacia el
  mar. Si tiene `UFUNCTION() void StartStorm()` y `UFUNCTION() void StopStorm()` sin parámetros, se llaman al salir y al
  acabar la ronda; si no, arranca sola y se destruye al acabar. Al preparar la ronda siguiente se destruye siempre.

### No se muere: aturdimiento

- `TNBeach::StunTurtle(Tortuga, Segundos, Lanzamiento)` (`Private/World/Beach/TN_BeachStun.cpp`): suelta lo que lleve,
  sale del derribo y se mete en el caparazón como bola (`ForceEnterShell` sin cuerpo + `StartBody(Lanzamiento,
  lanzada)`; sin lanzamiento, cae donde está), con la salida bloqueada. Si ya estaba aturdida, se alarga hasta el mayor
  de los dos finales (y, con lanzamiento, la bola sale disparada otra vez). Al acabar se desbloquea y sale sola en
  cuanto la bola se para. El estado va en `UTN_BeachStunComponent` (`bStunned` y el final, replicados), que el servidor
  añade en ejecución la primera vez: el cooperativo no lo lleva. En todas las máquinas: la bola tiembla (la malla se
  agita sobre la caja física) y dan vueltas los pájaros del mareo.
- `TNBeach::IsNoDeathWorld`: el GameState es `ATN_BeachRaceGameState`.
- Todas las rutas de muerte acaban en `MarkPlayerDead` (zonas de muerte, `ATN_StormVolume`, caídas de más de
  `FatalFallHeight` y enemigos vía `ATortugaCharacter::RequestKill`); el de la playa nunca llama a la base:
  - en el agua de meta → llega (una caída larga al agua es llegar);
  - dentro de una zona de muerte o de tormenta, o por debajo del vacío → vuelve a su último sitio seguro (se apunta cada
    0,5 s si pisa suelo, fuera de zonas de muerte y sin aturdir; se usa el más nuevo con al menos 1 s) y queda aturdida
    2,5 s (`RescueStunSeconds`);
  - si no → aturdida 3 s donde está (`DeathStunSeconds`).
- Vacío: el suelo más bajo pisado en la ronda (o la salida) menos 150 m (`VoidDepth`), siempre por encima del `KillZ`
  del nivel. Si el motor destruye la tortuga igualmente, reaparece en su sitio seguro, aturdida.
- Mucho antes que el vacío, la **red de seguridad**: bajo la arena o cayendo sin suelo, vuelve encima en una bola corta
  (ver «Seguridad: nunca bajo el mapa»).
- El salto del acantilado de meta no aturde ni hace bola: ver «Salto final al agua».

### Salto final al agua

Lo que pidió el usuario: que el salto final no se haga bolita por los metros de caída, que caiga de cabeza al agua y que
se quede medio segundo largo dentro del agua para que se vea la salpicadura.

- **De dónde salía la bola**: no de la muerte por caída. `ATortugaCharacter::TickFallRules` (servidor) mete a la tortuga
  sola en el caparazón como bola con física a los `AutoShellFallHeight` = 5 m de caída libre; el acantilado mide 15,5 m.
  El golpe de `Landed` (`FatalFallHeight` = 35 m → `RequestKill` → `MarkPlayerDead` → aturdir) no llega a saltar: la
  caída es más corta y al entrar al agua el movimiento pasa a nadar, que deja de contar la caída.
- **Arreglo** (`ATN_BeachRaceGameMode::GuardCliffJump`, en `WatchRacers`, diez veces por segundo, también mientras se ve
  el chapuzón de la ganadora): si una tortuga cae (`MOVE_Falling`, fuera del caparazón y sin aturdir) dentro de
  `ATN_BeachRaceGenerator::IsCliffJumpZone` (los últimos 7,5 m de la repisa y el vacío sobre el agua), su caída pasa a
  inmune (`SetFallImmuneUntilLanded`, lo mismo que géiseres y palas): ni bola a los 5 m ni golpe al aterrizar hasta tocar
  suelo o agua. Vale si la caída empieza o pasa por la zona; el resto de caídas de la playa, igual que siempre. Sin
  tocar `TortugaCharacter`.
- **De cabeza**: la zambullida de `UTN_TurtleAnimInstance` (ver «Poses nuevas y zambullida») ya no se corta por la bola;
  y, si la caída entra en la zona ya lanzada (más de 0,35 s cayendo), empieza igual en cuanto cae a más de 10 m/s.
- **A la vista en el agua** (`MarkPlayerFinished`): el puesto se decide en el instante del contacto (la primera que el
  `Watch` ve en el agua de meta gana la ronda; ver «Cuenta atrás tras la primera y medias conchas») y la tortuga sale
  del caparazón, del aturdimiento y de quien la llevara. Se queda en el agua, visible en todas las máquinas,
  `FinishSplashHoldSeconds` = 0,8 s, sin aturdirla ni rescatarla; después pasa por la meta de la base (puesto, puntos,
  la oculta y la pasa a espectadora). La cámara no corta antes de ese margen.
- **Chapuzón** (en cada máquina, a partir del movimiento replicado, sin RPC): el generador ya pinta la corona de gotas,
  la espuma y las ondas al entrar los pies en el agua de meta (`ATN_BeachRaceGenerator::Splash`). Lo que faltaba lo
  añade `UTN_BeachFinishSplashSubsystem` (`World/Beach/TN_BeachFinishSplash.*`), que mira lo mismo: el **chorro** de
  agua que sube un instante después (gotas casi verticales y una columna de espuma blanca, `TNAmbientFX` en un actor
  local) y el **«¡chof!» sintetizado** (`UTN_BeachSplashSynthComponent`: chasquido del golpe, lámina de agua que baja de
  tono, «plom» grave de la cavidad, burbujas que suben de tono y la lluvia de gotas del chorro; sin archivos de audio,
  como el foley de las trampas). El tamaño sale de la velocidad de caída al tocar el agua (el acantilado es el 1).
- **Sin ponerse de pie** (ronda 4): el mismo subsistema, con la ronda en juego y sin cerrar (y en el sprint, solo la
  primera), congela la postura de la tortuga que entra en el agua de meta (`bPauseAnims` de su malla, cosmético y en cada
  máquina; no toca una malla ya pausada o con física, como la del derribo) hasta que la meta la oculta: nadie la ve ponerse
  de pie en el agua. Si sale del agua sin llegar, o sigue a la vista 2,5 s después (no era una llegada), recupera la
  animación.

### Llegada al agua: huevo negro y «Has quedado X.º» (ronda 4)

Lo que pidió el usuario: que la tortuga no se ponga de pie en el agua antes de volverse fantasma; en su lugar, el huevo
negro del cooperativo se cierra según se zambulle y, en la pantalla negra, el puesto con su premio y un mensaje gracioso.

- **Puesto** (servidor, `ATN_BeachRaceGameMode::MarkPlayerFinished`): el de siempre, en el instante del contacto (1 la
  primera, 2, 3… las de la cuenta atrás; en el sprint, la ganadora es la 1), replicado en
  `ATN_BeachRaceGameState::RoundArrivals` (`FTNBeachRoundArrival`: PlayerState y puesto; `GetArrivalPlace`,
  `AddRoundArrival`, con `ForceNetUpdate`). Solo llegadas de verdad: la concha del tiempo agotado no entra. Se vacía al
  preparar cada ronda y con «Volver a jugar».
- **Pantalla de quien llega** (`UTN_RaceScreensSubsystem`, solo en la suya):
  1. En cuanto su tortuga entra en el agua de meta (lo mismo que mira el servidor, su centro; sin esperar a la red) o, si
     no se ha visto aquí, en cuanto llega su puesto, la **cáscara oscura** (`UTN_GhostHatchWidget::ShowCurtain`) entra desde
     arriba y desde abajo de la pantalla y se cierra en **0,28 s** con un «¡clac!». Mientras, su tortuga sigue en el agua
     con la postura de la zambullida congelada (ver «Sin ponerse de pie») y la cámara con ella.
  2. Cerrada, y con el puesto del servidor, **«Has quedado X.º»** encima (`UTN_RaceArrivalWidget`, ZOrder 51): la cinta
     de la ronda («RONDA N» o «SPRINT FINAL»), «HAS QUEDADO» y el puesto enorme del color de su medalla, que entra de golpe;
     el premio cae desde arriba, se aplasta al posarse y se queda con su gesto; debajo, el nombre del premio y el mensaje.
     Dura **2,7 s** con su salida rápida (unos 3 s de pantalla negra en total). Detrás, el servidor ya la ha ocultado y
     pasado a espectadora (a los 0,8 s).
  3. Al acabar, la cáscara se rompe (fogonazo, las mitades despedidas entre trozos) y se ve lo que haya: el **fantasma
     espectador** siguiendo a otra tortuga si quedan corriendo, el gusano comiéndose a las que no llegaron, o el recuento o
     el podio si ya han salido. Con todas en el agua (`AllIn`) se espera tapada (como mucho 2,5 s más) a que salga el
     recuento, y se rompe directamente sobre él: el servidor retrasa el recuento hasta que acaba la pantalla de la última
     en llegar (`FinishTimeUp`, `ArrivalScreenHoldSeconds` = 3,3 s desde su llegada; antes eran 1,6 s).
  4. Si el puesto no llega en 1,6 s (no era una llegada: la ronda se cerraba justo), la cáscara se funde y no pasa nada.
- **Sprint final**: la ganadora ve su «Has quedado 1.º» con la cinta «SPRINT FINAL» mientras debajo entra la pantalla del
  campeón (que sigue saliendo a los 0,8 s para las demás); la cáscara se rompe sobre el podio.
- **Premios y mensajes** (dibujados en código, `Private/UI/Race/TN_RaceArrivalArt.h`, pegatinas del estilo del HUD; un
  mensaje al azar entre los de su puesto; textos con `NSLOCTEXT`, espacio `TNRace`, claves `Arrival1a`…`Arrival8c` y
  `Prize1`…`Prize8`):

| Puesto | Premio (tamaño en pantalla a 1080 p) | Efectos | Mensajes |
|---|---|---|---|
| 1.º | Corona de oro de cinco puntas con gema coral, dos turquesa y aro de piedras (enorme, 520 px) | Rayos dorados que giran, brillo, lluvia de confeti y destellos; fanfarria cuya nota larga cae con la corona y «¡plin!» de la concha reina | «¡Reina de la playa! Hasta las gaviotas te hacen reverencias.» · «¡Primera! El mar te esperaba con la alfombra de espuma puesta.» · «¡Oro! Los cangrejos ya están tallando tu estatua de arena.» · «¡Nadie te ha visto ni la cola! Esa corona te queda de miedo.» |
| 2.º | Corona de plata de tres puntas con una gema turquesa (330 px) | Rayos plateados, destellos y algo de confeti; «¡plin!» | «¡Casi! La arena aún quema de tus pasos.» · «¡Plata! A un aletazo del oro: la próxima es tuya.» · «¡Segunda y brillando! Hasta el sol se ha puesto celoso.» |
| 3.º | Corona de bronce enana, con una punta doblada, abolladura, grieta y los huecos de las gemas vacíos (168 px) | Rayos cobrizos pequeños y destellos; «¡plin!» más corto | «Podio. La corona es pequeña; el orgullo, no.» · «¡Bronce! Te cabe en una aleta, pero es toda tuya.» · «Tercera: corona de bolsillo, sonrisa de campeona.» |
| 4.º | Cubo de playa del revés (por corona), turquesa con borde amarillo, asa coral y estrella de mar | Se tambalea y se queda torcido; saltan granos de arena; «pom… pom» que bajan | «Cuarta. El cubo también es un trono… de arena.» · «Cuarta. A un paso del podio, con un cubo de sombrero.» · «Cuarta. Si entornas los ojos, el cubo casi parece una corona.» |
| 5.º | Media concha rota, sosa, con un mordisco, una grieta y el trocito caído | Foco gris; trombón triste | «Quinta. Ni frío ni calor: templadita.» · «Quinta. Media concha rota, medio aplauso.» · «Quinta. Justo en el medio, donde nadie mira.» |
| 6.º | Flotador pinchado a rayas, arrugado, con parche y el aire que se escapa | Se va desinflando; nubecita gris que le llueve encima; trombón triste más grave | «Sexta. Hasta el flotador se rindió antes que tú.» · «Sexta. Llegas desinflada, como tu premio.» · «Sexta. Psssss… eso que se escapa es tu orgullo.» |
| 7.º | Calcetín mojado lleno de arena, con agujero, gotas y el tufillo que sube | Se balancea; nube y lluvia; trombón más grave | «Séptima. El gusano ya había reservado mesa.» · «Séptima. Tu premio huele igual que tu carrera.» · «Séptima. Un calcetín con arena: útil para… nada.» |
| 8.º | Alga de peluca chorreando, con vesículas y una conchita enganchada | Mustia; lluvia más fuerte; el trombón más grave | «Octava. Las gaviotas ya te llaman por tu nombre.» · «Octava. Te has traído medio mar enganchado a la cabeza.» · «Octava. Última, pero has llegado. Poca cosa, pero algo.» |

  El número del puesto va en oro, plata y bronce en el podio y, abajo, en arena y grises cada vez más apagados y más
  torcido. Los nombres: «Corona de oro», «Corona de plata», «Corona de bronce (talla mini)», «Cubo de playa (del revés)»,
  «Media concha rota», «Flotador pinchado», «Calcetín mojado con arena» y «Alga de peluca». El puesto se escribe con
  `FText::Format("{0}.º", puesto)` (en otros idiomas, con su ordinal). Las texturas se dibujan de antemano mientras se juega
  (`WarmArt`, una por fotograma). El trombón triste es un aviso nuevo del sintetizador de las pantallas
  (`ETNRaceCue::SadTrombone`: si bemol, la, la bemol y un sol largo con sordina que tiembla y se cae; más grave cuanto peor).
- **La cáscara en modo carrera** (`UTN_GhostHatchWidget`, la del cooperativo, en `Docs/Fantasma_Espectador.md`):
  `ShowCurtain(PC, Cierre, EsperaMáxima)`, `Knock()`, `Open(bConFiesta)`, `IsClosed()`, `IsOpening()`; se rompe sola si nadie
  la abre (14 s en la llegada, 50 s entre rondas). ZOrder 50 (encima del HUD y de las pantallas de la carrera, debajo del
  menú de pausa y de la pantalla de carga); lo de encima, en el 51.

### Entre ronda y ronda: huevo negro y «RONDA N» (ronda 4)

Lo que pidió el usuario: que la espera entre rondas no quede pobre; la transición del huevo, salir directamente saltando
de los huevos como al empezar la carrera y un «RONDA 2» en grande con su animación. Solo en cada pantalla, sin tocar el
servidor (las fases y sus tiempos son los de siempre):

1. Al pasar a `Waiting` desde el recuento, el título del sprint o el podio («Volver a jugar»), la cáscara oscura se cierra
   desde arriba y desde abajo en 0,32 s sobre la pantalla que se va. No en la primera ronda tras el viaje: esa la tapa el
   huevo de la pantalla de carga.
2. Encima (`UTN_RaceRoundIntroWidget`, ZOrder 51): **«RONDA N»** en dorado (o **«SPRINT FINAL»** en coral) entra de golpe
   con un «¡pum!» y trocitos de cáscara y se mece; debajo, en una etiqueta de arena, una frase: «¡Esta ronda puede coronar a
   una campeona!» si a alguien le falta una concha o menos, «La primera en el agua se lleva la partida.» en el sprint,
   «Partida nueva: todas las conchas a cero.» en la ronda 1 de «Volver a jugar» y, si no, una al azar («La playa se ha vuelto
   a desordenar. ¡A por el agua!», «Las gaviotas han vuelto con hambre.», «Trampas nuevas, arena de siempre.», «La tormenta
   ya se está peinando.», «Nadie se acuerda de la ronda anterior. Bueno, casi nadie.»). Mientras el servidor prepara la
   ronda, «Colocando la playa…» latiendo.
3. Con las tortugas ya en sus huevos empieza la cuenta de salida de siempre (`PhaseSecondsLeft`, 3 s): por cada número,
   la cáscara da un «pum» desde dentro con su grieta de luz y el 3, el 2 y el 1 saltan en un medallón (crema, dorado y
   coral), con un respingo del título. La cuenta se descuenta en cada máquina entre valores replicados y solo cuenta tras
   haber visto la preparación (un resto del reloj del recuento no da golpes).
4. Al dar la salida (`Racing`) la cáscara se rompe y el título sale disparado: la tortuga ya está en su huevo abierto,
   con la pausa de 1 s de siempre (`TNEggHatch`) y el salto hacia el mar, y el «¡ADELANTE!» de la pantalla de carga sale
   encima. Si la partida se va por otro lado (el sprint sin rival va al podio) también se rompe.

### Pruebas

- Sin lobby: `open LVL_BeachRace?BeachSeed=42?BeachWins=1` (semilla fija y conchas para ganar).
- Consola en la ventana del anfitrión: `TN.Race.WinRound [jugador] [puesto]` (toca el agua: la primera arranca la cuenta
  atrás de 10 s; las siguientes, media concha; con `puesto`, su pantalla «Has quedado X.º» enseña ese puesto y su premio),
  `TN.Race.NextRound` (en plena carrera, la cierra ya con el recuento; en el recuento o el título del sprint, sigue sin
  esperar: para ver el paso entre rondas), `TN.Race.Champion [jugador]`, `TN.Race.Sprint [jugador] [jugador]…` (empate
  forzado a tres conchas y sprint final; por defecto 0 y 1; con uno solo también, para probarlo),
  `TN.Race.Stun [segundos] [jugador]`, `TN.Race.Kill [jugador]` (ruta de muerte), `TN.Race.Void [jugador]` (al vacío),
  `TN.Race.Bury [metros] [jugador]` (bajo la arena: la red de seguridad la devuelve encima), `TN.Race.SafetyNet 0|1`,
  `TN.Race.PlayAgain`, `TN.Race.ChangeMode`, `TN.Race.Menu` y `TN.Mode [Coop|Race]`. `jugador` es el índice en
  `PlayerArray` (0 por defecto, normalmente el anfitrión).
- En cualquier máquina y mapa: `TN.Race.Splash [tamaño]` (chorro y «¡chof!» delante de tu tortuga, solo en esa máquina;
  la corona de gotas del generador sale solo al entrar de verdad en el agua de meta).
- Salto final (PIE de 2 jugadores): saltar desde la repisa del acantilado → cae de cabeza, sin bola, y entra al agua con
  corona, chorro y «¡chof!»; desde la otra ventana se ve 0,8 s dentro del agua con la postura de la zambullida (sin
  ponerse de pie) y desaparece. Caerse por el resto de la playa sigue haciendo bola a los 5 m.
- Llegada al agua (ronda 4; PIE de 1 y de 2): en la ventana de quien salta, según entra en el agua la cáscara oscura se
  cierra desde arriba y desde abajo (nunca se ve a la tortuga de pie), «Has quedado 1.º» con la corona de oro, rayos,
  confeti y fanfarria; unos 3 s después se rompe. Con 1 jugador (o si ya han llegado todas) se rompe directamente sobre el
  recuento, que sale más tarde que antes (3,3 s tras la llegada). Con 2, la primera ve su corona y luego al fantasma
  siguiendo a la otra; la segunda, si llega en la cuenta, «Has quedado 2.º» con la corona de plata y, al romperse, el
  recuento. Los premios de abajo: `TN.Race.WinRound 1 6` en el anfitrión (el cliente llega con la pantalla del 6.º, desde
  la arena si hace falta) o `TN.Race.ArrivalPreview 6` en cualquier ventana.
- Entre rondas (ronda 4; PIE de 1 y de 2): acabar una ronda (`TN.Race.WinRound` y esperar, o `TN.Race.NextRound` dos veces)
  → al irse el recuento, la cáscara se cierra con «RONDA 2» y su frase, «Colocando la playa…», tres «pum» con 3, 2, 1 y se
  rompe justo cuando las tapas de los huevos saltan: la tortuga sale de su huevo con la pausa de 1 s y el «¡ADELANTE!»;
  en las dos ventanas a la vez. `TN.Race.Sprint 0 1`: tras el título, «SPRINT FINAL» con la cáscara (también en la
  ventana de una tercera que mira de fantasma). «Volver a jugar» en el podio: «RONDA 1».
- Cuenta atrás (PIE de 2): la primera llega → en las dos ventanas, la cinta «¡La primera ya está en el agua!» y el
  número de 10 a 1 con «¡toc!» que se acelera; la segunda llega dentro → «¡Media concha para ti!» y, como ya no queda
  nadie, «¡TODAS AL AGUA!» con silbato y el recuento, sin gusanos (la entera vuela y la media salta después). Sin llegar
  la segunda: «¡TIEMPO!» a los 10 s, le sale el gusano de arena, se la come y el recuento espera a que acabe (~3,8 s);
  en ese rato no la aturde, rescata ni mueve nada. En la ronda siguiente sale de su huevo como todas. Con tres, que la
  tercera fuera de tiempo no se lleve nada.
- Sprint (PIE de 2 o 3): `TN.Race.Sprint 0 1` en el anfitrión → título con fanfarria y «VS»; el nido de huevos aparece
  a mitad de la playa (en la salida quedan las bases abiertas) con las finalistas dentro, quietas; 3, 2, 1, las tapas
  saltan y salen lanzadas hacia el mar (en las dos ventanas); la primera en el agua sale en el podio como campeona
  («¡Gana el sprint final…!»), sin cuenta ni gusanos; una tercera jugadora mira de fantasma. Después, «Volver a jugar»:
  el nido vuelve a la salida. Probar también que un empate de verdad (dos a tres conchas en la misma ronda) lo lanza
  solo.
- Misión (PIE de 2 jugadores, en el lobby): el anfitrión habla con el general, cambia modo y dificultad con el ratón y
  con teclado o mando; la otra ventana lo ve al momento en el diálogo abierto y en la pizarra, y no puede cambiarlo.
  Al ponerse listos, se viaja al modo elegido.

## Seguridad: nunca bajo el mapa

Lo que reportó el usuario, jugando en red (anfitrión y cliente): «en el modo carrera, todo el rato nos está metiendo bajo
el mapa, o nos bugeamos, u ocurren cosas muy raras». No puede pasar nunca. Causas encontradas en el código y en los
registros (`Saved/Logs`), de más a menos probable, y lo que se ha hecho con cada una.

### Causas

1. **El cliente ignoraba todas las correcciones del servidor (casi segura: está en el registro).** En
   `Tortunabo_2-backup-2026.09.28-11.30.52.log` (el cliente de una carrera por Steam) hay 1855 avisos
   `ClientAdjustPosition_Implementation could not resolve the new relative movement base actor, ignoring server
   correction!` con la tortuga sobre `ProceduralMeshComponent_22…28` (teselas del terreno), 69 `CreateSavedMove: Hit
   limit of 96 saved moves` y 17 248 `FNetGUIDCache::SupportsObject: ProceduralMeshComponent … NOT Supported`.
   - **Por qué**: las teselas del terreno, el decorado local (`ATN_BeachDecorField`), el nido del sprint y las piezas que
     cada elemento monta en `ApplySpec` son componentes creados en ejecución (`NewObject`), sin nombre estable por red y
     con movilidad `Movable`. El motor los trata como bases que se mueven y manda las posiciones **relativas** a ellos; el
     otro lado los recibe nulos. El cliente tira la corrección entera (y como no se confirma ningún movimiento, se le
     amontonan hasta 96) y el servidor toma la posición relativa del cliente por absoluta.
   - **Qué pasaba**: desde la primera diferencia (el salto de los huevos, un ragdoll que se levanta en otro sitio en cada
     máquina, la salida de una bola, un teletransporte del servidor), el cliente corría donde él creía mientras el
     servidor movía su tortuga con las teclas del cliente desde otro sitio: contra paredes, metida en el decorado, bajo la
     arena o cayendo. Las correcciones solo entraban cuando la tortuga del servidor no pisaba una de esas bases (en el
     aire, cayendo bajo el mapa…): entonces el cliente aparecía de golpe allí. Y la tortuga del anfitrión vista desde el
     cliente llegaba con la base «sin resolver» (`FBasedMovementInfo::IsBaseUnresolved`): el motor no la simula ni la
     suaviza (quieta, a tirones o con la malla en otro sitio).
2. **La cápsula crecía en su sitio al acabar el panzazo (alta).** Tumbada, la cápsula mide 35 de semialtura; de pie, 88.
   `Multicast_OnDiveVisual(false)`, el final de `TickDive` y el corte del arrastre en `TickBellyPhase` la ponían de pie
   sin moverla: 53 cm por debajo del suelo, con el centro de la esfera de abajo 19 cm por debajo de la superficie de la
   malla fina del terreno. Al desincrustarse, el movimiento la empuja hacia abajo y cae por debajo del mapa. Pasa cada
   vez que el panzazo acaba sin que el movimiento la haya levantado: tope de tiempo reptando bajo algo
   (`DiveMaxSeconds`), derribo o bola durante el panzazo, agua, sujeta por un enemigo, parada en el aire; en el servidor
   y, al llegarle el fin del panzazo, en el cliente dueño (que además, por la causa 1, no se corregía).
3. **La salida no esperaba a los clientes (media).** El servidor soltaba a las tortugas en cuanto su generador decía
   `IsRoundReady`, pero cada cliente monta por su cuenta y en varios fotogramas los asientos de la ronda en las teselas y
   las ~4000 piezas del decorado local con su colisión (`TN.Beach.BuildBudgetMs`). Un cliente más lento corría un rato
   con el terreno sin asientos o sin decorado mientras en el servidor sí estaban: su movimiento predicho atravesaba (o
   chocaba con) lo que en el servidor era distinto.
4. **Física distinta en cada máquina (media; se notaba por la causa 1).** El ragdoll del derribo lo simula cada máquina y
   cada una levanta a la tortuga donde acabó el suyo (`FindStandSpotNear`); la salida de la bola se coloca con la caja que
   tiene cada máquina (`PlaceStandingFromBox`); los huevos lanzan en el servidor y en el cliente por separado. Diferencias
   de hasta metros que solo arregla la corrección del servidor (que ahora sí entra). Además, si la caja de la bola se
   quedaba hundida en la malla del terreno, la traza de 40 cm por encima empezaba ya bajo la superficie, no encontraba
   suelo y la tortuga se ponía de pie bajo la arena.
5. **El único rescate era el vacío (media, para la sensación de «bajo el mapa»).** `VoidDepth` = 150 m por debajo del
   suelo más bajo pisado: una tortuga que atravesaba el terreno caía varios segundos bajo el mapa antes de volver.
6. **Relevancia de las estructuras (revisada, sin cambios).** Los castillos con salas y las fortalezas son relevantes en
   toda la playa (`WantsAlwaysRelevant`); el resto de elementos, a 260-450 m del punto de vista del cliente (su tortuga o
   su cámara, que nunca está a más de ~30 m de ella), y una vez recibidos duermen y se quedan en el cliente. Las
   plataformas, el ascensor, la pala, la catapulta y la tabla que se tambalea son subobjetos por defecto (bases con nombre
   estable). Una tortuga no puede tocar nada que su cliente no tenga; lo que sí fallaba era la base de lo que se monta en
   `ApplySpec` (causa 1).

### Arreglos

- **Bases que no se encuentran por red** (`UTN_TurtleMovementComponent::IsNetResolvableBase`: sin nombre estable ni
  réplica, o de un actor que no llega a la otra máquina). Con ellas todo va en coordenadas del mundo y sin base:
  - cliente → servidor: `FTNTurtleNetworkMoveDataContainer` (`SetNetworkMoveDataContainer` en el constructor) manda la
    posición y la aceleración del mundo y la base nula (el servidor usa la suya para comparar, como con suelo quieto);
  - servidor → cliente: `ServerMoveHandleClientError` pasa la corrección al mundo y le quita la base; el cliente
    (`ClientAdjustPosition_Implementation`) la aplica entera y busca aquí su suelo para repetir los movimientos
    pendientes sin perder el primer paso;
  - a los demás clientes: `ATortugaCharacter::PreReplication` replica «sin base» (y sin `bServerHasBaseComponent`), así
    que el motor simula y suaviza a esa tortuga con su posición del mundo. Se acaban también los avisos `NOT Supported`.
  Las bases que sí se encuentran (la balsa, el ascensor, la tabla, la pala, la catapulta: subobjetos por defecto) siguen
  siendo relativas, como antes.
- **La cápsula nunca crece en su sitio**: `UTN_TurtleMovementComponent::RestoreStandingCapsule` la pone de pie con los pies
  donde están (`TryStandUp`: sin meterse en nada; si algo encima no deja, igual, sin barrer: ya no cruza el suelo, así que
  nunca se desincrusta hacia abajo). La llaman el fin del panzazo (`ATortugaCharacter::RestoreDiveCapsule`, en el
  servidor y el dueño; en las demás máquinas crece y sube lo mismo), el corte del arrastre y, por si acaso, cada movimiento
  fuera del panzazo con la cápsula aún encogida.
- **La salida espera a los clientes**: `UTN_BeachRoundSyncComponent` (en `Game/`, componente replicado que el GameMode añade
  al PlayerController de cada jugador) mira en el cliente dueño, cuatro veces por segundo, el generador y, cuando
  `IsRoundReady` de la ronda actual, lo dice al servidor (`ServerReportRoundReady`, fiable, una vez por ronda).
  `PollRoundReady`, con la ronda ya lista en el servidor, espera a todos los clientes que corren (en el sprint, las
  finalistas) como mucho `ClientRoundReadyTimeoutSeconds` = 12 s; pasado, se corre igual con aviso. Registro: `[Carrera]
  Ronda N lista en el servidor: esperando a que … la monten`, `[Playa] ronda N montada en este cliente…` (en el cliente),
  `[Carrera] … tiene montada la ronda N del generador.` y `[Carrera] Ronda N montada en todos los clientes (X s de
  espera).` (o el aviso del tope).
- **Salida de la bola**: sin suelo en la traza de siempre, `PlaceStandingFromBox` lo busca desde 2,5 m por encima de la
  caja (lo que se hunde, no un puente por encima) y la pone de pie encima. Y si con eso los pies quedan más de 30 cm por
  debajo de la malla del terreno de la playa (la caja la atravesó y la traza dio con lo enterrado de una pieza del
  decorado), sube hasta la superficie de verdad (`TNBeach::DepthUnderTerrain`; en el cooperativo no hace nada).
- **Red de seguridad** (`ATN_BeachRaceGameMode::GuardUnderSand`, en el servidor, en cada mirada de `WatchRacers`: diez
  veces por segundo, para cada tortuga que corre):
  - Dónde está de verdad (`BodyProbe`): la caja de la bola si va en bola, el cuerpo raíz del ragdoll si está derribada y,
    si no, los pies de la cápsula (sujeta por un enemigo, en brazos de otra o andando).
  - **Bajo la arena**: más de `UnderSandMargin` = 1,6 m por debajo de `ATN_BeachRaceGenerator::GetGroundHeightAt` (terreno
    fijo con pozas y trincheras cavadas y los asientos de la ronda), 2,6 m en las trincheras, **y también de la malla del
    terreno de verdad** en esa vertical (`ATN_BeachRaceGenerator::TraceTerrainAt`, una traza solo contra las teselas con
    colisión: en una depresión de la malla que el generador no conoce no está bajo el mapa, y la bola sale rodando sola;
    se avisa como mucho cada 10 s: `… bajo la arena del generador …, pero encima de la malla del terreno: no se
    rescata.`); confirmado en dos miradas seguidas (0,1 s) o al momento si pasa de 4 m. No mira a menos de 10 m del filo
    del acantilado ni más allá (la pared está socavada y ahí se cae al agua de meta), ni en las pozas, ni nadando, ni con la
    tortuga en la boca de un gusano (`WatchRacers` ya se la salta), ni en plena patada de la tormenta (la vigila la
    tormenta: ver «El bucle "torbellino" en la tormenta»), ni fuera de la carrera.
  - **Cayendo sin suelo**: la cápsula cayendo (sin bola ni ragdoll, a más de 2 m/s hacia abajo) más de
    `NoFloorFallSeconds` = 0,6 s sobre un punto donde, en la vertical de la arena (de 1,5 m por encima a 3 m por debajo),
    nada para a una tortuga: ahí falta la colisión.
  - **Rescate** (`RescueFromUnderSand`): `TeleportTurtle` (`TNBeach::RelocateTurtle`) la suelta del enemigo que la sujete
    (que además deja el ataque: `ATN_BeachEnemy::OnHoldAborted`), de otra tortuga, del caparazón, del aturdimiento y del
    derribo. Adónde, según los rescates de los últimos `SafetyNetRepeatSeconds` = 12 s (`ResolveRescueTarget`), para que
    nunca entre en bucle:
    - **nivel 0** (el primero): encima de la arena en ese mismo punto (`FindSandSpot`: lo primero que para a una tortuga
      desde 4 m por encima de la arena, con la cápsula de pie cabiendo: no dentro de una roca ni de una muralla), sin perder
      lo avanzado;
    - **nivel 1** (el segundo, o el mismo hoyo otra vez, o si caía sin suelo o ahí no cabe): su último sitio seguro, en
      arena abierta de verdad a menos de 4 m (`TNBeach::FindOpenSandSpot`: no encima de lo que se mueve o se rompe) y a más
      de `SafetyNetBadSpotRadius` = 8 m de los sitios malos;
    - **nivel 2** (del tercero en adelante): arena abierta a más de 12 m de todos los sitios malos, hasta 30 m alrededor de
      su último sitio seguro o de donde estaba; si nada vale, la salida. Al cuarto rescate en 12 s, un error en el registro
      (`rescatada N veces en 12 s … algo la sigue hundiendo ahí`).
    - Cada sitio de rescate es **malo** `SafetyNetBadSpotSeconds` = 20 s: los sitios seguros de la tortuga a menos de 8 m
      se olvidan y no se apuntan otros nuevos ahí.
    - Si el sitio queda dentro de la tormenta o a menos de 5 m de su frente, va por delante del frente
      (`ATN_BeachStorm::FindSpotAhead`).
    - **De pie, sin bola** (`SafetyNetStunSeconds` = 0; antes 0,8 s): una bola nueva donde algo la acababa de hundir se
      volvía a hundir y la red entraba en bucle. La corrección del movimiento lleva al cliente dueño la posición nueva.
      Después, `SafetyNetGraceSeconds` = 1,5 s reservada (`TNBeach::ClaimTurtle`: ni enemigos, ni trampas, ni aturdimientos,
      ni derribos la relanzan) y `SafetyNetStormGraceSeconds` = 3 s sin patadas de la tormenta.
    - El rescate del vacío (`RescueTurtle`) usa el nivel 1 (arena abierta, lejos de los sitios malos y fuera de la
      tormenta) y le da la misma gracia de la tormenta (más lo que dura su aturdimiento).
  - **Registro** (dos avisos): `[Carrera] Red de seguridad: <tortuga> 3.2 m bajo la arena en (x, y, z) m (arena a z m) ·
    <modo de movimiento, en bola, derribada en ragdoll, aturdida, panzazo, sujeta, en brazos, caída inmune, base y
    cápsula> · velocidad N cm/s (Z n) · la movía <la caja de la bola / el ragdoll / el enemigo que la sujeta / quien la
    lleva / su movimiento (modo)>.` y `[Carrera] Red de seguridad: <tortuga> vuelve encima de la arena en … (o a su
    último sitio seguro en …, a arena abierta lejos del hoyo en …, a la salida…) tras <causa> (rescate N en 12 s, nivel
    L).` Si un enemigo la sujetaba: `[Playa] <enemigo> suelta a <tortuga> (recolocada).`
  - Consola: `TN.Race.SafetyNet 0` la apaga (para comparar); `TN.Race.Bury [metros = 3] [jugador = 0]` mete a esa tortuga
    bajo la arena donde está.

### Otras rarezas (documentadas, sin cambios)

- Lo que la física de cada máquina decide por su cuenta (ragdoll, salida de la bola, huevos) sigue pudiendo acabar en
  sitios algo distintos: ahora lo arregla la corrección del servidor (un tirón corto al levantarse en el cliente dueño).
  Si molesta, el siguiente paso sería mandar desde el servidor el sitio donde se levanta.
- El ragdoll del cliente es solo suyo: si su física atravesara el terreno en su máquina, lo vería ahí hasta levantarse
  (`FindStandSpotNear` busca el suelo desde 30 m por encima); la tortuga del servidor, que es la que cuenta, la vigila la
  red de seguridad.
- `PutOnFloor` y `FindStandSpotNear` trazan por tipo de objeto (`WorldStatic`/`WorldDynamic`): pueden quedarse encima de
  un volumen sin colisión de bloqueo (agua, disparadores). Deja a la tortuga en el aire (cae), nunca debajo.
- La suelta de una sujeción desde la red de seguridad es del servidor: el cliente dueño puede verla en el pico o la boca
  hasta que su enemigo la suelte (como mucho unos segundos); la bola y la corrección la llevan luego a su sitio.
- En el editor (no en juego) salen avisos `ContainsPhysicsTriMeshData returned true, but GetPhysicsTriMeshData returned
  false` de mallas construidas en ejecución (las tapas de los huevos y alguna receta): sin colisión compleja, pero el
  movimiento usa la simple. El decorado de la ronda usa `CTF_UseSimpleAsComplex` y no lo tiene.

### Qué probar (red, 2 jugadores: anfitrión y cliente)

- En el registro del cliente: ni un `could not resolve the new relative movement base actor`, `Hit limit of 96 saved
  moves` ni `SupportsObject … NOT Supported` en toda la carrera (con `p.NetShowCorrections 1` casi no deben salir
  correcciones corriendo por la arena y el decorado).
- Desde el cliente, la tortuga del anfitrión se mueve suave por la arena (no quieta ni a tirones); y al revés.
- Panzazo: repetirlo por la playa, contra paredes, reptando bajo una silla o una mesa hasta que se acabe el tiempo, en
  una cuesta, cayendo al agua y con un derribo en pleno panzazo; al acabar, de pie encima de la arena en las dos
  pantallas (sin hundirse).
- Derribos, bolas (mina, catapulta, patada de la tormenta, gaviota que suelta) y sujeciones: al acabar, la tortuga del
  cliente queda donde la ve el anfitrión (como mucho un tirón corto).
- Salida: en el registro del servidor, `Ronda N lista en el servidor: esperando a que …` y `montada en todos los
  clientes`; la salida no se da hasta entonces (máximo 12 s más).
- `TN.Race.Bury 3 0` y `TN.Race.Bury 3 1` en el anfitrión: en ~0,2 s vuelve encima de la arena en el mismo sitio, de
  pie, en las dos pantallas, con los dos avisos `[Carrera] Red de seguridad`. Repetirlo con la tortuga en bola, en
  ragdoll (tras un derribo) y colgando de una gaviota. Tres veces seguidas en el mismo sitio: la segunda va a su último
  sitio seguro (nivel 1) y la tercera a arena abierta lejos (nivel 2); a la cuarta, el error de bucle.
- Con `TN.Race.SafetyNet 0`, lo mismo cae sin fin hasta el vacío (para comparar).

### El bucle «torbellino» en la tormenta

Lo que reportó el usuario (en red, anfitrión y cliente): «estás en la tormenta bugeado, atrapado en un bucle de ser
caparazón y clipearte en el suelo, como si estuvieras en un torbellino». Se reprodujo grabando el tráiler
(`Saved/Logs/Tortunabo.log`, PIE con paso fijo de 30 fps, rondas 2 y 3): 154 patadas y 400 avisos de la red de seguridad en
unos 4 minutos.

**Causa (dos bucles que se alimentan).**
1. **La patada no llegaba y se repetía cada vez más fuerte.** La patada era un lanzamiento balístico a ciegas: velocidad
   calculada hacia «20 m por delante del frente» sin mirar qué había en medio, ni dónde caía, ni si la bola nacía o
   estaba ya metida en algo. Contra una roca, una muralla o un decorado gigante (el reparto cubre ~50 % de la playa), la
   bola rebotaba hacia atrás; `KickedUntil` (vuelo + 0,6 s) caducaba y llegaba otra patada, con la tortuga cada vez más
   atrás (en el registro: 1, 5, 14, 12, 25, 29, 39, 47… 72 m detrás del frente en ronda 2; hasta 107 m en la 3). Cuanto
   más atrás, más velocidad pedía (hasta 50 m/s), y a esa velocidad la caja se metía por la malla fina del terreno.
   Aturdida vuelo + 0,8 s y pateada otra vez a los vuelo + 0,6 s: el jugador no recuperaba el control nunca.
2. **La red de seguridad la devolvía al mismo hoyo con otra bola.** Con la caja bajo el terreno, la red la sacaba encima
   de la arena en ese punto (o a su último sitio seguro, justo al borde) **con una bola nueva de 0,8 s**; esa bola se
   volvía a hundir en el mismo sitio en 0,2-0,4 s (en el registro, siempre entre (262-275, −56…−66) m, con la caja
   quieta 2-3 m por debajo de la arena), otra red, otra bola… y, por encima, la tormenta la seguía pateando en cuanto
   caducaba su espera. Cada sistema relanzaba lo que el otro acababa de poner.
Además: el pulpo lanza hacia la salida y la gaviota suelta hacia atrás (a la tormenta); un derribo levantado antes de
tiempo dejaba su temporizador vivo y la ponía a andar en plena bola; y ningún sistema sabía quién mandaba sobre la tortuga.

**La patada que no puede entrar en bucle** (`ATN_BeachStorm::KickTurtle`, `ServerTickFlights`):
- **Sitio resuelto en el servidor antes de patear**: `PointAhead` (KickAhead + lo que avanza el frente, a su altura de la
  playa, dentro de la playa jugable y a 30 m del filo) y `TNBeach::FindOpenSandSpot`: la primera superficie desde arriba
  es la malla del terreno (no algo encima), llana (normal ≥ 0,75), la cápsula de pie cabe (+15 cm), fuera del agua (pozas y
  balsas), de las pozas del generador, de las trincheras, del filo y de detrás del muro de la salida. Se prueban su altura
  y 4 y 8 m a cada lado; después lo mismo a `KickShortAhead` = 9 m del frente (arco corto: menos cosas por medio) y, si
  por delante no había ni un sitio, alrededor hasta 15 m. Sin sitio (fin del recorrido): no patea y vuelve a mirar en 1 s.
- **Arco comprobado**: para cada sitio, tres vuelos (el de siempre, 2,6 s y 3,2 s: más altos, para salvar lo de
  delante) con la velocidad exacta para llegar con la gravedad del mundo y la amortiguación de la caja
  (`BallisticLaunch`, en `TN_BeachStormKick.h`: x(t) = v/C·(1 − e^−Ct)); el arco se barre con una esfera de 24 cm en 8
  tramos contra lo que para una bola (`ECC_PhysicsBody`, sin tortugas ni el último 12 %; en el primer tramo no cuenta la
  pared en la que ya se apoya). El primero libre: bola por el aire.
- **Sin arco libre, la patada se ve igual** (#252; antes era un salto de teletransporte): si ningún arco está libre, si el
  sitio está a más de `KickMaxFlightDistance` = 45 m o si nada, la bola vuela alto (2,6 s como poco) **atravesando** lo
  que haya (`ATN_ShellBody::SetPassThrough`, replicado: la caja no choca con nada en ninguna máquina, y el servidor no la
  saca del agua ni de debajo del terreno) y vuelve a chocar al bajar sobre su sitio, en cuanto no toca nada pasada la
  mitad del vuelo o, como tarde, para el último 12 %. A una tortuga invulnerable por un objeto (la estrella) no se la
  patea: se vuelve a mirar en 1 s.
- **El vuelo acaba ahí sí o sí**: mientras vuela, la tormenta se reserva la tortuga (`TNBeach::ClaimTurtle(StormKick)`) y
  la vigila en cada fotograma: si se hunde más de 90 cm bajo la malla del terreno, si se atasca (menos de 1,2 m/s 0,35 s
  lejos del sitio), si sale de la bola antes de llegar (agua) o si al acabar el vuelo (+0,4 s) está a más de
  `KickLandTolerance` = 7 m del sitio o detrás del frente, **otra patada visible** la lleva desde donde está, atravesando
  (`RetryKick`, hasta `TNBeachStormKick::MaxHops` = 2). Solo sin bola posible o tras esas dos se la pone en su sitio sin
  vuelo (`PlaceKicked`: `TNBeach::RelocateTurtle`, sin bola ni aturdimiento, y polvo). Si llega bien, la bola sigue
  rodando y el aturdimiento acaba como siempre.
- **Después, `KickGraceSeconds` = 3 s sin patadas** (`TNBeach::GrantStormGrace`), y para entonces está ~20 m por delante.
- **A quién no patea** (`TNBeach::GetTurtleMover`): a la que lleva otra cosa, que la suelta sola: el pico o la boca de un
  enemigo, un gusano, los brazos de otra (patean a la que la lleva), el derribo, la bola de aturdida, un lanzamiento por el
  aire, la red de seguridad, o sin movimiento por otra cosa (una concha que atrapa). Si lo que acaba solo (derribo, bola,
  lanzamiento, concha) sigue más de 8 s detrás del frente, se patea igual. A la que va en su bola porque quiere, sí.
- Al parar o quitar la tormenta, las reservas se sueltan (la bola sigue su física). Registro: `[Playa] La tormenta patea a
  X: N m detrás del frente, vuela T s hasta su sitio, M m por delante (L m a lo ancho).`, `… sin arco libre: vuela T s
  atravesando hasta su sitio, M m por delante.`, si no llega `[Playa] Tormenta: X no ha llegado a su sitio (atascada por
  el camino / hundida bajo la arena / …): otra patada desde donde está, D m en T s.` y, como último recurso,
  `[Playa] Tormenta: X a su sitio en (x, y, z) m (…); 3 s sin patadas.`

**Quién mueve a la tortuga** (árbitro, `TN_BeachStun.h`): `TNBeach::ETNBeachMover`, de menos a más prioridad: su movimiento,
lanzamiento, bola, derribo, en brazos, sujeta por un enemigo, patada de la tormenta, red de seguridad, gusano.
`GetTurtleMover` dice quién manda ahora (la reserva vigente o lo que se ve de su estado). La patada y la red se la reservan
mientras la recolocan (`ClaimTurtle` / `ReleaseTurtle`, solo en el servidor, en `UTN_BeachStunComponent`) y, mientras:
- `TNBeach::StunTurtle` y `TNBeach::KnockDownTurtle` no hacen nada (enemigos, mina, alambre, muerte...);
- `ATN_BeachEnemy::CanBeHit` es falso (ningún enemigo la ataca ni la coge);
- `TNBeachRideKit::LaunchAsBall` no la lanza (catapulta, trampolín);
- la red de seguridad no mira a la que vuela en una patada (la tormenta la vigila) y la tormenta no patea a la que acaba
  de rescatar la red (gracia de 3 s y, si el sitio quedaba dentro, uno por delante del frente).
Y siempre:
- **Nada la lanza hacia la tormenta cerca del frente**: en `StunTurtle`, a menos de 25 m por delante del frente (o detrás),
  lo que la echaría hacia atrás se quita del lanzamiento (el pulpo hacia la salida, la gaviota al soltarla, la mina hacia
  atrás…); lo de lado y hacia arriba se queda.
- **Las trampas no cogen a quien mueve otro** (`TNBeachTrapKit::IsFreeTurtle`: tampoco sujeta por un enemigo ni en la boca
  de un gusano; esto se ve igual en todas las máquinas, así que el cliente dueño predice lo mismo). La mina, además, no
  lanza a la sujeta ni a la comida.
- **Quitarle la tortuga a un enemigo le hace dejar el ataque** (`ATN_BeachEnemy::OnHoldAborted`, desde
  `ServerReleaseHeldTurtle`): el lagarto la olvida y vuelve a su sitio, el pulpo se hunde a su sitio y la gaviota la da
  por soltada, sin lanzarla ni aturdirla (antes el lagarto la lanzaba al acabar el zarandeo desde donde la había dejado la
  red, el pulpo la lanzaba hacia la salida y la gaviota le daba otra bola hacia atrás). El lagarto y el pulpo también la
  sueltan, sin lanzarla, si otro la mueve ya (bola, derribo, aturdida, recolocada). El gusano de arena suelta al enemigo
  que la tuviera.
- **Teletransporte limpio** (`TNBeach::RelocateTurtle`, también `ATN_BeachRaceGameMode::TeleportTurtle`): suelta del
  enemigo, de la carga (lo que lleve y quien la lleve), levantada del derribo, fuera del aturdimiento y del caparazón (se
  le devuelven colisión de la cápsula, movimiento, suavizado y réplica), sin velocidad ni lanzamiento pendiente, de pie en
  el sitio y cayendo.
- **El derribo levantado antes de tiempo** (por una bola, un teletransporte, un rescate) ya no deja vivo su temporizador
  (`ATortugaCharacter::RecoverFromKnockdown` lo borra): antes saltaba más tarde, la ponía a andar en plena bola y volvía a
  sonar el «¡arriba!».

**Otros fallos encontrados y arreglados en esta caza** (tandas de enemigos, trampas y flujo):
- Sprint final: si una finalista se iba durante el título del sprint, `CheckSprintForfeit` veía las llegadas de la ronda
  anterior, creía que ya había ganadora y la otra corría el sprint sola hasta el tiempo límite (`TN_BeachRaceGameMode.cpp`:
  la comprobación de «ya hay ganadora» es solo con el sprint en marcha).
- Catapulta: `FiredAt` va en float y en el mismo fotograma del disparo, en el servidor, `T` podía salir un pelo negativa: la
  colisión del brazo y del cazo volvía a encenderse con las bolas recién nacidas dentro (lanzamientos torcidos o flojos)
  (`TN_BeachCatapult.cpp`).
- Mina: se pisaba con la carrera parada («¡TIEMPO!», recuento, podio): una tortuga congelada encima la hacía estallar cada
  vez que se rearmaba y la bola la soltaba de la congelación (`TN_BeachMine.cpp`, ahora solo con `IsRaceLive`).
- `TNBeachRideKit::LaunchAsBall`: si no se podía crear la caja, la tortuga se quedaba en el caparazón, bloqueada y quieta;
  ahora sale.

**Lo que queda (documentado, sin tocar)**:
- **Colisiones que se mueven sin barrido sobre bolas y tortugas**: la bandeja del ascensor (baja hasta 12 cm dentro de la
  arena), el mango de la pala al dar la vuelta, la valva de la concha al cerrarse (solo despide a las libres: una bola, una
  tortuga derribada o una que lleva a otra junto al borde se quedan dentro) y las mitades de la plataforma al romperse.
  Una bola atrapada entre ellas y la malla fina del terreno puede salir por debajo; la red de seguridad la saca (sin bucle
  ahora). Arreglo propuesto: antes del golpe, cierre o encendido de la colisión, mirar qué solapa y empujarlo hacia fuera
  y arriba; en el ascensor, `BottomTopZ = RideThick + 2`.
- **Cajas del cangrejo y del tanque**: bloquean las bolas y se mueven cada fotograma con el fondo a ras de arena; pueden
  aplastar una bola contra el terreno. Arreglo propuesto: que respondan `Overlap` a `ECC_PhysicsBody` (pierde el rebote de
  la bola contra ellos) o bajar la caja 1,5 m bajo la arena.
- ~~**Bola que nace dentro de algo al soltarla un enemigo**~~ (arreglado en la ronda 4: la caja nace en un sitio libre,
  `UTN_ShellComponent::FindFreeBodySpot`; ver «Segunda gaviota + caparazón = torbellino»).
- **La concha que atrapa no tiene «¿es su presa?» estático**: la tormenta la reconoce por estar sin movimiento
  (`MOVE_None`) y espera; si otro sistema la recolocara estando dentro, la concha podría seguir sujetándola.
- Tabla que se tambalea: cada máquina la ladea con lo que ve (hasta ~13 cm de diferencia en los bordes: correcciones). Se
  arreglaría dejando la colisión plana y ladeando solo la malla.
- `IsRaceLive` cuenta la fase `Waiting` como carrera (a propósito, por si la fase no cambiara): durante el 3, 2, 1 del
  sprint un enemigo o una mina fuera del radio despejado podrían tocar a las finalistas congeladas.
- ~~Suavizado de red de la tortuga soltada por un enemigo~~ (arreglado en la ronda 4: al salir de la bola vuelven el
  suavizado y la colisión de serie de la clase, no una copia de lo que hubiera al entrar).
- El empujón del lagarto huidizo va por multicast no fiable: si se pierde, el dueño recibe una corrección.
- En el tráiler se ve el sitio donde la bola se hundía siempre (≈(265, −62) m con las semillas 1854610542 y 1619440358):
  el reparto no pone nada ahí más que decorado pequeño (probado con el arnés del reparto), así que lo más probable es una
  pieza de decorado cuya colisión se mete bajo la arena y empuja a la caja a través de la malla. Ya no provoca bucle; si
  sigue saliendo el error de bucle en el registro con la misma posición, mirar ese decorado.

**Riesgos de compilación** (sin compilar): `TNBeach::ETNBeachMover` es un `enum class` de C++ (sin UHT) dentro del
namespace; el componente guarda la reserva como `uint8`. Funciones nuevas en `ATN_BeachStorm` (y `MulticastKickLand`, un
RPC nuevo), `ATN_BeachRaceGenerator::TraceTerrainAt`, `ATN_BeachEnemy::OnHoldAborted` (virtual, con `override` en el
lagarto, el pulpo y la gaviota), `ATN_BeachRaceGameMode::ResolveRescueTarget` y `ForgetSafeSpotsNear`.
`TN_BeachTrapKit.h` incluye ahora `TN_BeachEnemy.h` y `TN_BeachSandWorm.h`.

**Qué probar** (red, 2 jugadores: anfitrión y cliente; comandos en `Docs/Comandos_Prueba.md`):
1. `TN.Beach.Storm.Here` (la tuya) y `TN.Beach.Storm.Here 1` (la del cliente): patada; acaba de pie o rodando en arena
   abierta ~20 m por delante del frente, en las dos pantallas. En el registro, `vuela … hasta su sitio` o `salto a su
   sitio`, y nunca dos patadas seguidas a la misma tortuga en menos de ~3 s.
2. Lo mismo junto a una muralla, una fortaleza o un decorado gigante (de espaldas a él, que el arco choque): salto con
   polvo o, si vuela y rebota, `a su sitio (atascada por el camino)`.
3. `TN.Beach.Storm.Here 0 30` y `TN.Beach.Storm.Here 1 60`: patada larga (vuelo alto) y salto (más de 45 m).
4. Patada estando en bola (métete en el caparazón detrás del frente), derribada (un erizo dentro de la tormenta), colgando
   de una gaviota y nadando en una poza: la de la bola vuela; derribada, sujeta o en el agua espera a que la suelten (o 8 s
   como mucho) y entonces patea o salta.
5. `TN.Race.Bury 3 0` tres veces seguidas: niveles 0, 1 y 2 y, a la cuarta, el error de bucle. Nunca vuelve al mismo sitio
   las tres veces.
6. Un pulpo o una gaviota junto al frente: ya no la lanzan hacia atrás (lo de lado, sí).
7. En el registro de una carrera entera: ningún `Red de seguridad` repetido cada medio segundo en el mismo sitio, ninguna
   racha de `La tormenta patea` a la misma tortuga cada vez más atrás.

### Segunda gaviota + caparazón = torbellino (ronda 4, tarea 1)

Lo que reportó el usuario (29-09-2026): «la primera vez que te coge una gaviota todo va bien; si te coge otra y te metes
en el caparazón, la física se rompe y la tortuga queda atrapada en un bucle de bola y suelo», como el torbellino de la
tormenta. Registro: `Saved/Logs/Tortunabo.log` (anfitrión, un jugador): tres agarres seguidos de `TN_BeachGullZone_1`
(01:13:05, 01:13:32 y 01:13:47), patada de la tormenta a las 01:13:59, la tormenta la coloca a las 01:14:01 («aterrizó lejos
de su sitio») y a las 01:14:03 la red de seguridad encuentra una bola (`TN_ShellBody_14`) 2,4 m bajo la arena **sin ningún
«aturdida» entre medias**: esa bola no la pidió nadie.

**Causa raíz.**
1. **Recolocar no reiniciaba la caída.** `TNBeach::RelocateTurtle` (red de seguridad, la tortuga que la tormenta pone en su
   sitio, rescates) le ponía `MOVE_Falling` tras el teletransporte. Pero ya estaba cayendo: al sacarla de la bola o del
   derribo dentro de la propia recolocación, `ApplyBodyLocalState(false)` le pone la caída donde estaba la caja. Pedir otra
   vez el mismo modo no hace nada y `ATortugaCharacter` se quedaba con la altura de antes (`FallApexZ`: la caja en el vuelo
   de la patada, en lo alto de un castillo, en el pico). En su siguiente paso, `TickFallRules` veía «5 m de caída» y la
   metía sola en una bola (`ForceEnterShell(true, true)`: de pie y girando, como a mano) justo donde la acababan de poner
   de pie, y el aterrizaje contaba una caída mortal (en la carrera, otra bola de aturdida). Bola que nace donde no toca → se
   hunde → red de seguridad → de pie → otra bola: el bucle de bola y suelo. El torbellino de la tormenta se arregló quitando
   la bola del rescate (`SafetyNetStunSeconds` = 0), pero esta otra bola seguía saliendo después de cada recolocación desde
   arriba.
2. **Por qué a la segunda gaviota.** Cada agarre se lleva a la tortuga 15 m hacia la salida y la suelta empujándola 3,5 m/s
   hacia atrás, con la tormenta avanzando por detrás: a la segunda o tercera, la deja en el frente → patada → recolocación
   desde arriba → bola sola. A la primera aún queda lejos. No queda nada del primer agarre sin devolver (revisado: el árbitro
   no guarda ninguna reserva `Held`, la ve por `IsTurtleHeld` y se limpia al soltar; el prerrequisito de tick y las
   correcciones al dueño se devuelven; `bExitLocked` se limpia; `MOVE_None` quita la base y la velocidad pendiente).
3. **Meterse en el caparazón colgando** tenía dos agujeros que dan el mismo aspecto: la bola nacía con el enemigo aún
   sujetándola (en el servidor la soltaba en el mismo fotograma; en los clientes, la sujeción seguía hasta que llegaba la
   suelta: el pico colocaba a la tortuga y la caja la arrastraba a la vez, la caja girando alrededor de la cápsula clavada;
   el lagarto y el pulpo ni miran el caparazón en los clientes), y la caja nacía donde estuviera la tortuga, a veces dentro
   del decorado por el que la arrastra la gaviota, y salía empujada a través de la malla fina del terreno. Además, la tecla
   no hace nada con un objeto en la mano, y no se veía por qué (en esa partida llevaba la Tinta).
4. **De paso**: la bola guardaba la colisión de la cápsula y el suavizado de red al engancharse y los devolvía al salir; si
   en ese momento otro sistema los tenía cambiados (el ragdoll del derribo deja la cápsula sin colisión y su vuelta puede
   llegar a un cliente después que la bola), la tortuga salía de la bola sin chocar con el suelo. Y la vuelta de un derribo
   que llegaba con la bola ya puesta le encendía el movimiento en plena bola.

**Arreglos** (`TN_BeachStun`, `TN_BeachEnemy`, `TN_BeachGullZone`, `TN_ShellComponent`):
- `TNBeach::RelocateTurtle`: si ya caía, pasa por `MOVE_None` antes de `MOVE_Falling`: la caída se cuenta desde el sitio
  nuevo (sin bola sola ni caída mortal tras un teletransporte).
- **Se escurre sin dos que la muevan**: `UTN_ShellComponent::ServerToggleShell` → `TNBeach::SlipFromHolder` →
  `ATN_BeachEnemy::ServerSlipHeldTurtle` → `OnHeldTurtleSlips`: quien la sujeta la suelta **antes** de que nazca la bola.
  La gaviota, igual que al acabar el vuelo (`ReleaseCarried`: `EndHoldTurtle` y la bola de aturdida hacia la salida): el
  mismo camino que el primer agarre. Los demás la sueltan sin más y su lógica ve la bola en su paso siguiente (el lagarto,
  su mareo de 1 s, como antes). En la boca de un gusano no se entra en el caparazón.
- **La sujeción nunca a la vez que otra cosa** (`ATN_BeachEnemy::CanHoldTurtle`, lógica pura): ni metida en el caparazón,
  ni con su caja enganchada en esa máquina, ni en ragdoll, ni en brazos de otra, ni muerta. `BeginHoldTurtle` no la coge y
  `PlaceHeldTurtle` la suelta en cuanto pasa (en un cliente, en cuanto le llega la caja, aunque la suelta del enemigo llegue
  después): vale para la gaviota, el lagarto, el pulpo y el pelícano taxi.
- **Árbitro**: `TNBeach::StunTurtle` y `KnockDownTurtle` no pueden con la sujeción de un enemigo (`TNBeach::CanStunOver`,
  lógica pura; tampoco con la patada, la red ni el gusano, como antes): el enemigo la suelta él antes de aturdirla (todos lo
  hacen). `TNBeach::GetTurtleMover` sale ahora de `TNBeach::ResolveMover` (lógica pura, mismo orden que antes).
- `ATN_BeachEnemy::RestoreReleasedTurtle`: soltada en su caparazón según el servidor pero sin su caja aún en esa máquina,
  espera 0,6 s (`BallArrivalGrace`) a la caja en vez de hacerla caer por su cuenta: el cliente ya no alterna bola y caída.
- `UTN_ShellComponent`: la caja nace en un sitio libre (`FindFreeBodySpot`: si se mete en algo que para una caja de física,
  un poco más arriba o alrededor, hasta 2 m; con la cápsula de pie libre no busca nada); al salir de la bola, la cápsula y
  el suavizado vuelven a los **de serie de la clase** (si el ragdoll de esa máquina sigue, la cápsula queda sin colisión
  hasta que se levante); y mientras la mueve la caja, `EnforceBodyLocalState` (en cada `FollowBody`) vuelve a apagar el
  movimiento y a dejar la cápsula solo solapando si otro sistema se los ha devuelto.
- Registro: `[Playa] A <gaviota> se le escurre <tortuga>: se mete en su caparazón.` y `[Caparazón] <tortuga> no se mete en
  el caparazón: lleva un objeto en la mano` (o nadando, en pleno panzazo, derribada, muerta).
- Pruebas automáticas (`Private/Tests/TN_BeachHoldTest.cpp`): `Tortunabo.Beach.Hold.Arbiter` (quién la mueve y qué puede
  aturdirla), `Tortunabo.Beach.Hold.CanHold` y `Tortunabo.Beach.Hold.RepeatedGrabs` (dos agarres, el segundo metiéndose en el
  caparazón colgando, y cinco seguidos: nunca dos que la mueven y cada agarre deja todo como estaba).
- Comando para reproducirlo: `TN.Beach.Gull.Grab [veces=2] [jugador]` (la zona de gaviotas más cercana te coge N veces
  seguidas, cada una en cuanto estés libre; `Docs/Comandos_Prueba.md`).
- **Sin llevársela a la tormenta**: junto al frente en marcha (detrás o a menos de 25 m por delante,
  `TNBeachGull::StormNoCarryReach` = el vuelo de 15 m + 10), la zona caga en vez de picar y, si pica, falla
  (`ATN_BeachGullZone::IsNearStormFront`): el vuelo de 15 m hacia la salida la metía dentro y empezaba la cadena de patada,
  recolocación y red de seguridad.

**Lo que queda (sin tocar)**: `TickFallRules` no consulta al árbitro (si de verdad cae 5 m durante la reserva de la red de
seguridad, hace bola). Otros teletransportes que no pasan por `RelocateTurtle` podrían quedarse con la altura vieja si la
tortuga ya caía. `TN.Beach.Gull.Grab` junto al frente de la tormenta falla a propósito.

**Riesgos de compilación** (sin compilar): `ATN_BeachEnemy::OnHeldTurtleSlips` (virtual nueva, `override` en la gaviota),
`ServerSlipHeldTurtle` y `CanHoldTurtle` (estáticas); `TNBeach::FTNMoverView`, `ResolveMover` y `CanStunOver` (en línea en
`TN_BeachStun.h`), `SlipFromHolder` e `IsDodgingByBellyDive`; en `UTN_ShellComponent`, `FindFreeBodySpot` y
`EnforceBodyLocalState` y fuera los campos `Saved*` de la cápsula y del suavizado; `ATN_BeachGullZone::DebugGrab`,
`IsNearStormFront` y `ServerTrackAim(float, float, const FChasePlan&)` (la zona incluye ahora `TN_BeachStorm.h`, y las cabeceras de la zona y de la justiciera, `TN_BeachGullTuning.h`, con los `struct` `FChasePlan` y `FChaseState`); `TNBeachCrabTuning` en `TN_BeachGiantCrab.h`; `TN_BeachEnemyDebug.cpp`
incluye `Player/TortugaCharacter.h`; `TN_SeagullDroppingActor.h` tiene un `UPROPERTY` nuevo (`bBellyDiveDodges`).

**Qué probar** (anfitrión y, si se puede, cliente):
1. `TN.Beach.Gull.Grab 2` quieta: te coge, te suelta en bola, sales; te vuelve a coger y, colgando, pulsa el caparazón (sin
   objeto en la mano): cae en bola aturdida hacia la salida desde donde estaba, sin girar alrededor del pico ni hundirse. En
   el registro, `se le escurre`, `aturdida` y ningún `Red de seguridad`. Repetir con `TN.Beach.Gull.Grab 5`, pulsando unas
   veces sí y otras no.
2. Lo mismo desde el cliente (`TN.Beach.Gull.Grab 2 1` en el anfitrión): en la pantalla del cliente no se queda colgando
   del pico ni tiembla entre bola y caída.
3. Con un objeto en la mano, pulsar el caparazón colgando: no pasa nada y el registro dice `lleva un objeto en la mano`.
4. `TN.Beach.Storm.Here` estando encima de una fortaleza o de un castillo alto (que la patada la saque desde arriba): la
   tormenta la pone en su sitio de pie y **no** se mete sola en una bola (antes: bola sin «aturdida» y, a veces, `Red de
   seguridad … en bola con caja física`).
5. `TN.Race.Bury 3 0` en bola y en ragdoll: vuelve de pie, sin bola.
6. Un lagarto mordedor (`TN.Beach.Lizard mordedor`): métete en el caparazón en su boca; te suelta con el mareo corto de
   siempre, sin que la bola tiemble en su boca.
7. Junto a la tormenta: `TN.Beach.Storm.Start 10` (frente 10 m detrás) y en seguida `TN.Beach.Gull.Attack 2` quieta: el
   picado falla y pica la arena; en una ronda normal, junto al frente, las gaviotas solo cagan.

## Tiempo de ronda, bola del caparazón y red con 4 y 8 jugadores

Lo que reportó el usuario (29-09-2026, partida en red de dos): «me ha saltado como que se acaban los tiempos y no había
llegado nadie a la meta»; «cuando me hago bolita, como actúa físicamente, debe de haber turbulencias o algo constante que
hacen que la bola se vuelva loca»; y que la carrera vaya fluida y sincronizada con cuatro y preparada para ocho.

### «¡TIEMPO!» sin nadie en el agua

**Causa.** Solo hay dos caminos a «¡TIEMPO!»: la cuenta de 10 s (hace falta alguien en el agua, y su cinta lo dice con su
nombre) y el tope de la ronda. Fue el tope:
- Desde el recorte a 800 m era de 6 min (`RoundTimeLimitSeconds`), sin aviso en pantalla. En los registros de las pruebas
  del 28-09 (`Saved/Logs/Tortunabo*.log`, una tortuga) salta en todas las rondas: `[Carrera] ¡Tiempo! Ronda N sin llegadas:
  gana X, la más cerca del mar.` La playa llena de trampas, enemigos y la tormenta no se cruza en 3 min 20 s (la media a
  ~4 m/s). Los registros de la partida por Steam no estaban en la máquina.
- Y la pantalla engañaba: el tope ponía `FinishCountdown = TimeUp` sin `RoundWinner`, y la cuenta atrás
  (`UTN_RaceFinishCountdownWidget`) sacaba su cinta de siempre: «¡La primera ya está en el agua!», «Concha para Tortuga ·
  media concha para quien llegue…» y «¡TIEMPO!», con nadie en el agua.
- Falsas llegadas, descartadas: `IsFinishWaterLocal` pide haber pasado el filo (X > EdgeX + 50 cm) y el centro de la
  cápsula a 30 cm o menos sobre el agua; la playa está a 15-40 m sobre ella, y las pozas y balsas, a su altura. El nivel no
  tiene `ATN_FinishLineVolume` (solo el generador), y la red de seguridad, el vacío y la patada de la tormenta devuelven a
  arena abierta a 30 m o más del filo. Llegar al mar lanzada por una catapulta o soltada por una gaviota es llegar.

**Arreglo** (`ATN_BeachRaceGameMode`, `ATN_BeachRaceGameState`, `UI/Race/`):
- Tope de ronda **9 min** y de sprint **4 min 30 s**.
- Reloj replicado: `RoundEndServerTime` (hora del servidor; 0 si no cuenta), `RoundTimeLimitSeconds` y `GetRoundTimeLeft()`
  (-1 si no hay límite, si ya ha llegado alguien o fuera de `Racing`). La primera en el agua lo para (`StopRoundClock`):
  manda la cuenta de 10 s.
- **Reloj del último minuto** (`UTN_RaceRoundClockWidget`, nuevo; lo pone `UTN_RaceScreensSubsystem::TickRoundClock`):
  pastilla arriba en el centro con «TIEMPO DE RONDA» y los segundos, dorada desde los 30 s y coral latiendo en los 10
  últimos con un «¡toc!» por segundo. A los 60 y a los 30 s, cinta «¡Queda 1 minuto!» / «¡Quedan 30 segundos!» con la regla
  debajo («Si nadie llega al agua, la concha es para la más cerca del mar»; en el sprint, «gana la más cerca del mar») y un
  golpe con «¡toc!».
- **Motivo del fin de ronda**: `ETNBeachRoundEnd` (`None`, `Countdown`, `AllIn`, `TimeLimit`, `SprintTimeLimit`,
  `Forfeit`) replicado en `RoundEndReason`; `FinishTimeUp(ETNBeachRoundEnd)`. Con `TimeLimit` la cinta del «¡TIEMPO!» dice
  «¡Se acabó el tiempo de la ronda!» y «Nadie ha llegado al agua · concha para X, la más cerca del mar» (`RoundWinner` va
  puesto antes), y el recuento, «¡Se acabó el tiempo! Nadie llegó al agua: concha para X, la más cerca del mar.»
  (`FTNRaceTallySetup::bTimeLimit`). Sin gusanos, como antes. En el sprint, antes se saltaba al podio sin decir nada:
  ahora 1,6 s de «¡TIEMPO!» con «¡Se acabó el tiempo del sprint!» y luego el podio. `FinishCountdown` sigue en `TimeUp` en
  los dos casos (enemigos y lanzadores lo miran para `IsRaceLive`).
- Registro: `[Carrera] ═══ Ronda N en marcha · semilla S · tiempo de ronda 9:00 ═══`, `[Carrera] Ronda N: queda 1 minuto y
  nadie ha llegado al agua…`, `… quedan 30 s …`, `[Carrera] ¡Tiempo! Se acaban los 9.0 min de la ronda N sin nadie en el
  agua: gana X, la más cerca del mar.` y `[Carrera] ¡Tiempo de ronda agotado! Ronda N: nadie en el agua; concha para X…`.
- Consola: `TN.Race.TimeLeft [segundos = 65]` (anfitrión: deja ese tiempo de ronda) y `TN.Race.ClockPreview [segundos =
  62]` (vista previa del reloj, sus avisos y el «¡TIEMPO!», sin jugar).

### La bola del caparazón «se vuelve loca»

**Causas**, de más a menos:
1. **Réplica de la física en los clientes** (constante; la más visible, en la cámara de su dueño). La caja
   (`ATN_ShellBody`) simula en todas las máquinas y la réplica de siempre del motor (`EPhysicsReplicationMode::Default`,
   `FPhysicsReplicationAsync::DefaultReplication_DEPRECATED`) fija en cada paso de física la velocidad = la del servidor +
   error × 100 × dt y gira el 40 % hacia la orientación del servidor, que llega a 30 Hz, 50-100 ms tarde y sin extrapolar
   entre paquetes. Una caja que da vueltas se corrige a tirones en cada paso, toca el suelo con una orientación que no es la
   suya y rebota: temblor y saltos. En el anfitrión su propia bola no lo tiene (su física manda); sí la del otro jugador
   vista desde un cliente.
2. **Costuras y aristas del terreno.** Las teselas son mallas finas en componentes distintos: la caja tropieza en las
   costuras y en las aristas interiores (contactos fantasma): saltitos y vueltas sin motivo.
3. **Rebote 0,35 y giro sin tope**: tras un golpe contra decorado pequeño con colisión o contra una arista, vueltas locas.
4. **Salida disparada** de lo que solapa al nacer o al recolocarla (sin tope de velocidad de separación).
5. **Suavizado de red perdido**: si la bola (o el agarre de un enemigo) llegaba a un cliente después de un ragdoll,
   guardaba «sin suavizado» y lo devolvía así: la otra tortuga se veía a saltitos el resto de la ronda.

Descartado: ninguna fuerza por fotograma empuja la caja en la playa (solo lanzamientos puntuales: aturdir, trampolín,
catapulta, patada de la tormenta); la red de seguridad y el árbitro solo actúan bajo la arena o en una patada; el temblor
del aturdimiento es solo de la malla; la cápsula de su tortuga solo solapa y su malla no tiene colisión física.

**Arreglos**:
- `ATN_ShellBody`: réplica en **interpolación predictiva** (`SetPhysicsReplicationMode(PredictiveInterpolation)`: extrapola
  el estado del servidor y corrige con velocidad, sin girar a tirones); `TN.Shell.PhysicsRep 0` vuelve a la de siempre en
  las bolas nuevas (para comparar). **Aristas suavizadas** (`bSmoothEdgeCollisions`: quita los contactos fantasma entre
  componentes; caro, pero son como mucho ocho bolas), **giro máximo de 900 °/s**, amortiguación angular 1,4 (era 1,1; la
  lineal no cambia: la patada de la tormenta calcula su arco con ella), **rebote 0,2** (era 0,35) y separación de lo que
  solapa a 300 cm/s como mucho (`SetMaxDepenetrationVelocity`).
- `UTN_ShellComponent::ApplyBodyLocalState` y `ATN_BeachEnemy` (al agarrar): si el suavizado ya estaba apagado, guardan el
  de la tortuga (`Exponential`), no «apagado».
- No se ha hecho: subpasos de física (cambian todo el proyecto), una forma redonda (todo usa `UBoxComponent` y
  `BoxHalfExtent`) ni resimulación (necesita la predicción de física del proyecto). Si aún se ve rara, lo siguiente es
  ajustar `np2.PredictiveInterpolation.*` o probar una esfera como raíz de la caja.

### Red y rendimiento con 4 y 8 jugadores (servidor escucha)

**Arreglos de la auditoría**, por orden de impacto:
- **Voz** (lo que más pesaba): mu-law a 24 kHz = 24 KB/s por quien habla, reenviados por el anfitrión a cada oyente a
  menos de 25 m. Provisional: **16 kHz** (`VoiceDownsampleFactor` 3: 16 KB/s) y como mucho **4 oyentes**, los más cercanos
  (`MaxVoiceListeners`). Peor caso con ocho hablando juntos: ~0,5 MB/s de subida del anfitrión (antes ~1,3 MB/s).
  Pendiente: Opus.
- `UTN_StaminaComponent::SetSprintRequested`: el RPC fiable `ServerSetSprintRequested` iba **en cada fotograma** al moverse
  (riesgo de desbordar los fiables); ahora solo al cambiar.
- Cabeza: `ServerUpdateHeadRotation` iba en cada fotograma con dos floats a 60 Hz; ahora grados enteros (`int8`), como
  mucho 12 veces por segundo y solo si cambia (y una vez por segundo por si se perdió); `ReplicatedHeadYaw/Pitch`, un byte.
- Tortuga (`ATortugaCharacter`): 60/30 Hz → **30/10 Hz**. `CurrentStamina` solo al dueño; los demás reciben
  `StaminaShared`, un byte (fracción de `MaxStamina`) que solo se manda al cambiar.
- `ATN_CoopPlayerState::MulticastScoreShellCollected`, **no fiable** (es el estallido; los puntos van en `RaceScore`).
  Conchas de puntos (`ATN_ScorePickup`): **dormidas** (`DORM_DormantAll`; se despiertan al reaparecer o cambiar de valor)
  y relevantes a **200 m** (antes, siempre y en todo el mapa).
- `net.UseAdaptiveNetUpdateFrequency=1` (`Config/DefaultEngine.ini`, `[ConsoleVariables]`): lo que no cambia baja a su
  mínimo; el GameState se fuerza al cambiar de fase. Enemigos (también la zona de gaviotas y el paso de quads) relevantes a
  **200-300 m** (`ATN_BeachElement::EnemyMinNetRelevance`/`EnemyMaxNetRelevance`; antes 260-450 m).
- PlayerState: 30/15 Hz → **5/1 Hz**, con `ForceNetUpdate` al llegar, morir, derribo, revive, puntos y conchas de la
  carrera (recuento, volver a jugar, sprint).
- `UTN_CarryComponent::ServerSetStruggling`, **fiable** (solo va al cambiar: uno perdido dejaba el estado viejo).

**Cifras estimadas** (por medir con `stat net` en el anfitrión), recibido por cada cliente en carrera:
- Tortugas: con 4, 3 × 30 Hz × 25-40 B ≈ 2-4 KB/s (antes el doble); con 8, ≈ 5-8 KB/s. Cada bola que rueda: 30 Hz × ~40 B.
- PlayerState: 5 Hz como mucho y casi siempre 1 (antes 30). Conchas de puntos y trampas: nada mientras duermen.
- Enemigos a menos de 300 m: 10 Hz cerca, 3 Hz lejos, solo lo que cambia.
- Voz: 16 KB/s por cada una que te hable (te llegan las que te tengan entre sus 4 oyentes más cercanos). Subida del
  anfitrión con 8: juego ~40-60 KB/s más la voz (de 0 a ~0,5 MB/s según cuántas hablen a la vez). `MaxInternetClientRate`,
  200 KB/s por cliente.
- RPC por fotograma que quedan: ninguno de juego (la voz, ~12 paquetes por segundo por quien habla).

**Hecho para 8 jugadores**:
- Sesión: `UMP_GameInstance::MaxPlayers` = 8 (también en `DefaultGame.ini`) y `[/Script/Engine.GameSession] MaxPlayers=8`.
- Salida y sprint: nido de **ocho huevos** en dos filas de cuatro (`TNBeachLayout::MaxStartEggs`; la segunda, 9 m detrás,
  cada uno detrás de uno de la primera: el salto de ~15 m pasa por encima de la base rota, que no tiene colisión), con sus
  tapas, anillos, el nido del sprint y colores (`TNCastleKit::EggAccent`, 8; los 4 primeros, los de siempre).
  `GetStartTransform` ya ponía del quinto al octavo en esa fila; `GetNumStartSpots` = 8 (el despeje del nido del sprint
  cubre las dos filas). Los sitios rotan cada ronda.
- HUD: tripulación de hasta **7** compañeros con su color y su marca en la pista; con 4-5 filas se juntan y con 6-7 se
  encogen al 88 %. Las frases del chat rápido de los siete salen en su fila.
- Recuento y título del sprint: la fila se encoge para caber (8 columnas medían 2102 px) y los golpes del sprint entran en
  3,2 s como mucho. Vistas previas hasta 8.
- Algas: enganchan a 8 a la vez (eran 4).

**Pendiente para 8** (no cabía sin riesgo o es de otro agente):
- ~~Lobby, cooperativo y marcador de resultados del HUD de siempre~~ (hecho: pila de 8 huevos, 8 sitios en la salida del
  cooperativo, PlayerStart extra para el quinto al octavo y 8 filas en el marcador; ver «Ocho jugadores» en
  `Docs/Lobby_Castillo.md`).
- Menú de pausa (la fila de jugadores se sale con 8) y unirse a salas llenas (`OnFindSessionsComplete` no mira las plazas
  libres): del agente de salas.
- ~~Textos «de 1 a 4» de la sesión informativa, `RankScoreTable` del clásico y los topes del editor de
  `TN_BreakablePlatform` y `TN_BeachWobblyPlatform`~~ (hecho). Las 4 tortugas de la pantalla de carga son adorno, no
  jugadores: se quedan.
- Voz con Opus. El test del reparto (`TN_BeachLayoutTest`) sigue mirando solo la fila de 4.

### Qué probar

Con 2 (anfitrión y cliente):
1. `TN.Race.TimeLeft 65` en el anfitrión sin nadie en el agua: a los 60 s, el reloj arriba y «¡Queda 1 minuto!» con la
   regla; a los 30, «¡Quedan 30 segundos!»; dorado, coral y «¡toc!» los 10 últimos; a 0, «¡TIEMPO!» con «¡Se acabó el
   tiempo de la ronda!» y «Nadie ha llegado al agua · concha para X…», y el recuento con ese texto. En las dos pantallas.
2. Con el reloj en pantalla, que una llegue al agua: el reloj se va y sale la cuenta de 10 s de siempre.
3. `TN.Race.Sprint 0 1` y, ya en el sprint, `TN.Race.TimeLeft 5`: «¡Se acabó el tiempo del sprint!» 1,6 s y el podio.
4. Bola: meterse en la bola cuesta abajo, por encima de costuras del terreno y de decorado pequeño, en el cliente y en el
   anfitrión, y mirar la del otro: rueda sin temblar ni dar saltos sin motivo. `TN.Shell.PhysicsRep 0` (bolas nuevas) para
   comparar; con `p.Chaos.DebugDraw.Enabled 1` y `p.Net.DebugDraw.ShowRepMode 1`, se pinta en amarillo (predictiva).
5. Un derribo o un agarre y luego una bola: al acabar, la otra tortuga se ve suave (no a saltitos) en el cliente.
6. Esprintar y soltar muchas veces en el cliente: el anfitrión lo ve igual. La cabeza de la otra tortuga sigue a su cámara
   con suavidad. Las caras de la tripulación siguen su energía.
7. `stat net` en las dos: menos salida que antes; en el registro, ningún `reliable buffer overflow`.

Con 4 (y, cuando se pueda, con 5-8):
1. `stat net` en el anfitrión en plena carrera y con todos hablando a la vez: la voz se oye y la subida baja.
2. Salida con cinco o más: segunda fila de huevos, cada una en el suyo; el salto pasa por encima de los de delante. Sprint
   con 5-8 finalistas (`TN.Race.Sprint 0 1 2 3 4`): nido de dos filas y título que cabe.
3. HUD: tripulación compacta (`tn.HUD.CrewPreview 7`); `TN.Race.Tally 0 8` y `TN.Race.SprintPreview 8`.
4. Conchas de puntos: se ven al acercarse (200 m) y desaparecen al cogerlas en todas las máquinas.

**Riesgos de compilación** (sin compilar): `ETNBeachRoundEnd` (UENUM nuevo) y tres propiedades replicadas en el GameState;
`FinishTimeUp` cambia de firma; `UTN_RaceRoundClockWidget` (UCLASS nueva); `SetPhysicsReplicationMode` y
`EPhysicsReplicationMode` (`Engine/EngineTypes.h`); `FBodyInstance::bSmoothEdgeCollisions` y `SetMaxDepenetrationVelocity`;
`ServerUpdateHeadRotation(int8, int8)` y `ReplicatedHeadYaw/Pitch` en `int8`; `StaminaShared` con su `OnRep`;
`MulticastScoreShellCollected` y `ServerSetStruggling` cambian de fiabilidad (UHT las regenera); `MaxPlayers` pasa a
`Config`.

## Decorado gigante (`ATN_BeachDecor`)

| Archivo | Qué es |
|---|---|
| `Public/World/Beach/TN_BeachDecor.h`, `Private/World/Beach/TN_BeachDecor.cpp` | `ATN_BeachDecor`: una pieza de la categoría Decor como actor (`TN.Beach.Place`); y `TNBeachDecorKit` (caché de mallas, colocación, colisión, tramos y animación) |
| `Private/World/Beach/TN_BeachDecorKit.h` | La interfaz de `TNBeachDecorKit`, que comparten `ATN_BeachDecor` y el decorado de la ronda |
| `Public/World/Beach/TN_BeachDecorField.h`, `Private/World/Beach/TN_BeachDecorField.cpp` | `ATN_BeachDecorField`: el decorado de cada ronda, local en cada máquina e instanciado (ver «Rendimiento y red») |
| `Private/World/Beach/TN_BeachPropMeshes.h` | Recetas low-poly (`TNBeachProp`): primitivas, colisión simple, una receta por pieza y los kits de la pasarela y del caminito |

Desde la ronda 3, el decorado de cada ronda no son actores: lo monta cada máquina en su `ATN_BeachDecorField` con las
mismas recetas, colocación, colisión y movimiento que se cuentan aquí para `ATN_BeachDecor` (ver «Rendimiento y red»).

**Cómo funciona.** En `ApplySpec` la variante sale de `Spec.Seed` (4 por pieza; 8 rocas, 3 grupos de rocas) y la malla de
cada (elemento, variante) se construye **una vez por partida** (`static`, fuera del recolector, `RF_Transient |
RF_DuplicateTransient`, `M_CosmeticVertexColor` con el alfa del vértice como brillo) y la comparten todos los ejemplares:
el motor junta en una llamada de dibujo las mallas iguales con el mismo material. Cada ejemplar se coloca con la semilla
(giro libre salvo la silla, que mira a +X del actor con ±15°; inclinación de 0-8°; hundimiento en la arena según la
pieza, para que no flote en las dunas) y se escala a `Spec.SizeScale` (0,5-1,6) en su componente: **el actor no se escala**. La colisión va
en el `BodySetup` de la malla compartida: solo cajas, esferas y cápsulas (simple como compleja, nada que cocinar), con el
perfil `BlockAll`; la cámara solo choca con lo grande y macizo (rocas, grupos de rocas, troncos y castillos). Detalles
finos sin colisión (anillas, chapas, cáscaras, palitos, cuerdas, plumas, pajitas tumbadas). Lo pequeño deja de dibujarse
a 60 veces su huella (de 120 m a 600 m); lo de más de 10 m de huella, nunca.

**Subir y saltar.** Los escalones son de 80 cm (la tortuga salta 1,2 m; con `SizeScale` 1,4 siguen por debajo) y las
rampas de menos de 34° (lona de la vela 12-33°, toalla de la silla 33°, rampa de tablones 19°, losa 20°, pliegues de la
toalla 21-30°, lona de la sombrilla 13-31°, casquetes de la medusa y de la red 38°, montón de arena de la sombrilla 27°).

**Movimiento** (solo en las máquinas con cámara, nunca en el servidor dedicado ni en la ronda de prueba del editor): la
campana de la medusa respira y tiembla a ratos (escala), la valva de arriba de la almeja se abre y se cierra cada 7-12 s
por la bisagra (no si hay una tortuga encima al empezar el ciclo), el jirón de la vela ondea, el paño de red se mece y
la banderita de los castillos ondea. La parte que se mueve es un segundo componente sin colisión colgado de la malla
fija en su pivote. Un temporizador (cada 0,5 s) enciende el tick solo con una cámara local a menos de 60-200 m (según
el tamaño) y si se ha dibujado hace poco; si no, el actor no tiene tick.

**Tramos** (`Boardwalk`, `WoodenPostPath`): `Spec.Extent` es el largo (0 → 60 m; entre 15 m y el recorrido) por el eje X
local, centrado en el origen. Se montan con piezas instanciadas (`UInstancedStaticMeshComponent`, una por malla, con la
colisión de cada instancia); a lo largo y a lo ancho se escalan con `SizeScale`, el alto no.

| Pieza (`ETNBeachElement`) | Real | En el juego | Variantes | Colisión | Triángulos |
|---|---|---|---|---|---|
| Coco (`Coconut`) | 14-16 cm | 4-4,5 m de largo, 3,1-3,4 m de alto | pardo peludo, verde, partido en dos (una mitad boca arriba), germinando con brote | cápsula; las mitades, prisma y casquete | 100-290 |
| Medusa varada (`StrandedJellyfish`) | campana de 37 cm | 10,4 m de campana, 1,8 m de alto, brazos hasta 13 m | aurelia, aguamala, acalefo, clavel | casquete de 38° (se sube andando) | 770-900 |
| Anillas de latas (`SixPackRings`) | 21 × 14 cm | 6 × 4 m, 10 cm de grueso | casi todas cortadas, pocas cortadas, retorcidas | — | 700 |
| Sujetador rojo (`RedBra`) | copas de 14 cm | 11 m de ancho, copas de 3,9 m y 1,5 m de alto | liso, lunares, encaje con lazo y tirante en arco, una copa boca arriba | casquetes; la copa boca arriba, anillo de cajas | 530-600 |
| Almeja (`Clam`) | 5 cm | 1,4 m, 0,65 m de alto | crema, lila con rayos, naranja, gris con perla | caja | 390-440 |
| Concha de adorno (`DecorShell`) | 5-9 cm | 1,4-2,5 m | caracola, berberecho, porcelana moteada, vieira pálida | caja | 100-190 |
| Estrella de mar (`Starfish`) | 15 cm | 4 m, 0,55 m en el centro | naranja, roja con un brazo levantado, morada con un brazo corto, azul | casquete y cajas bajas en los brazos | 240 |
| Roca (`Rock`) | 35-45 cm | hasta 13 m, 3,2 m de alto | de estratos (4 escalones), losa inclinada, canto rodado, mesa con charco; arenisca, granito o pizarra | prismas por capa, caja inclinada o casquete | 70-240 |
| Grupo de rocas (`RockCluster`) | 1 m | 26 m, 4-4,8 m | 3 repartos: una grande de estratos, dos medianas de escalón, cantos y guijarros | prismas y casquetes | 1160-1220 |
| Restos de vela (`ShipSailWreck`) | mástil de 1,7 m | 48 m de mástil, lona hasta 8 m | 4 lonas con franja y remiendos | cápsula del mástil, cajón y 16 cajas por la lona (hueca por debajo) | 1290 |
| Tronco con musgo (`MossyLog`) | 85 × 12 cm | 23,5 × 3,4 m | en rampa sobre una piedra (de 0,9 a 6,1 m), medio enterrado (1,1 m), hueco para cruzarlo, con repisas de hongo (0,9 / 1,8 / 2,7 m) | cápsula, 8 cajas en tubo o cajas de las repisas | 200-510 |
| Tablones viejos (`OldPlanks`) | 50 × 9 × 2 cm | 14 × 2,5 × 0,56 m | pelados, azules con borde blanco, verde agua, rojos | una caja por tablón y el cajón (3,4 m) | 310-400 |
| Red de pesca (`FishingNet`) | 90 cm | 23 m, montón de 2,2 m, palo de 6 m | verde, azul, naranja, turquesa; bolas o corchos | casquete de 38° y el palo | 1780-2040 |
| Vaso de plástico (`PlasticCup`) | 9,5 cm | 2,7 m | rojo o transparente tumbado (se entra), de pie medio enterrado, aplastado | tubo de 8 cajas con suelo plano, anillo o caja | 170 |
| Botella (`Bottle`) | 25 cm | 7 m, 1,7 m de diámetro | verde tumbada, marrón clavada boca abajo, clara con un mensaje, verde clavada de culo | cápsulas | 310-380 |
| Chupachups (`Lollipop`) | 12-16 cm | 3,4-4,5 m | de bola con el envoltorio abierto, chupado con arena, espiral tumbada, espiral clavada (4 m) | esfera, caja o palo y disco | 140-220 |
| Corteza de sandía (`WatermelonRind`) | arco de 24 cm | 6,5 m de arco, 1 m de grueso | con carne, comida hasta lo blanco, de pie como un barquito, dos trozos | 4 cajas por el arco | 120-300 |
| Pajita (`Straw`) | 20 cm | 5,6 m | recta, doblada, de papel, clavada (4,5 m) | solo la clavada | 130-290 |
| Sombrilla clavada (`PlantedUmbrella`) | lona de 92 cm a 1,25 m | lona de 26 m a 35 m de alto | roja, azul, amarilla y naranja, turquesa (torcida 3-7°) | mástil, 16 cajas en la lona y el montón de arena del pie | 330 |
| Silla de playa (`BeachChair`) | 50 × 55 cm, respaldo de 68 cm | 14 × 15 m, asiento a 5,6 m, respaldo a 19 m | 4 lonas y toallas, bastidor de aluminio o blanco | patas, travesaño de atrás, asiento, respaldo, reposabrazos y la toalla-rampa | 720 |
| Castillo pequeño (`SandCastleSmall`) | 55 cm | 15 m, 3,2 m (bandera a 5,2 m) | 2 o 3 torrecillas, medio deshecho (bandera caída) | prismas y cajas | 790-1440 |
| Castillo enorme (`SandCastleHuge`) | 1,75 m | 49 m, 8,8 m (bandera a 12 m) | 4 colores de bandera | 100 formas: plataforma, rampa de 16°, murallas con puerta, torres, torreón, escalinatas | 3180 |
| Madera a la deriva (`Driftwood`) | 55 cm | 15 m | con horquilla, raíz, dos palos cruzados, rama en S | cápsulas por tramos | 140-420 |
| Pasarela (`Boardwalk`) | tablas de 50 × 9 cm | tramos de 12,8 m, 13 m de ancho, tablas a 1,1 m | tramo entero, sin tabla, rota, suelta levantada, movida; bajadas en los extremos | una caja por tabla y los pilotes | 160-190 por tramo |
| Caminito de palos (`WoodenPostPath`) | palos de 15 cm cada 20 cm | palos de 4,2 m cada 5,6 m, 5,4 m de ancho | palo recto, roto o con vueltas; cuerda, cabo azul o cinta de balizar | cápsula por palo (la cuerda se pasa por debajo) | 50-210 por palo, 72 por cuerda |
| Lata (`SodaCan`) | 12,2 cm | 3,4 m | roja, azul, aplastada, chafada de pie | cápsula, caja o prisma | 220 |
| Chapas (`BottleCaps`) | 3,2 cm | 0,9 m | 2-4, alguna boca arriba | — | 220-450 |
| Chanclas (`FlipFlop`) | 26 cm | 7,3 m | una, el par, del revés, con la tira rota | caja de la suela y cápsulas de la tira | 170-490 |
| Brick de zumo (`JuiceBox`) | 10,5 cm | 2,9 m | con pajita, de pie, aplastado, con la pajita envuelta | caja | 60-90 |
| Boya (`Buoy`) | 24 cm | 6,7 m | bola naranja con cabo, baliza de rayas, defensa blanca, bola amarilla con algas | esfera o cápsula | 290-460 |
| Toalla (`BeachTowel`) | 100 × 60 cm | 28 × 17 m | 1-3 pliegues, un extremo enrollado | caja plana, dos cajas por pliegue, cápsula del rollo | 780-870 |
| Crema solar (`SunscreenBottle`) | 17 cm | 4,8 m | tumbada con pegotes de crema, clavada del revés | caja | 190-290 |
| Palitos de helado (`PopsicleSticks`) | 11,4 cm | 3,2 m | 1-3, con restos de helado | — | 60-120 |
| Cáscaras (`SnackShells`) | 1-1,5 cm | 0,3-0,4 m | 8-13 de pipas, pistachos y cacahuetes | — | 170-270 |
| Trozo de cuerda (`RopePiece`) | 40 cm | 11 m | en S, en lazada, con nudo, cabo corto | — | 160-350 |
| Gafas de sol (`Sunglasses`) | 14 cm | 3,9 m de ancho, 1,4 m de alto | negras, rojas de espejo plegadas, de carey sin un cristal, de corazón | caja del frente y cápsulas de las patillas | 400-420 |
| Cubito de juguete (`ToyBucket`) | 9 cm | 2,5 m | con flan de estrella, flan de tortuga, molde de pez o rastrillo | prisma del cubo y el flan | 290-410 |
| Pelota hinchable (`BeachBall`) | 30 cm | 8,4 m | clásica, medio deshinchada, pastel, azul y blanca | esfera o casquete | 180 |
| Disco volador (`Frisbee`) | 27 cm | 7,6 m, 1 m de alto | boca abajo (se sube), boca arriba como un cuenco, de canto medio enterrado | prisma, cuenco o caja | 220 |
| Hueso de sepia (`Cuttlebone`) | 15 cm | 4,2 m | 4 (uno partido) | caja | 140 |
| Patito de goma (`RubberDuck`) | 9 cm | 2,5 m | amarillo, descolorido y tumbado, rosa, azul | cápsula y esfera | 330 |
| Pluma de gaviota (`GullFeather`) | 15 cm | 4,2 m | blanca con la punta negra, gris | — | 130 |

Las 17 últimas filas (de `SodaCan` a `GullFeather`) amplían el contrato: van en `ETNBeachElement` antes de `BarbedWire`
(cuentan como decorado) con su huella en `TNBeach::FootprintRadius`; el reparto las coge solas.

**Probar.** Una ronda con semilla fija (`open LVL_BeachRace?BeachSeed=42`) y recorrerla: subir a la medusa, la red, la
toalla de la silla (y colarse debajo del asiento), la losa y las rocas de estratos, el castillo enorme hasta la cima y la
vela hasta el mástil; entrar en el vaso tumbado y en el tronco hueco; pasar por la pasarela (y caer por la tabla que
falta) y chocar con los palos del caminito. Mirar que la almeja se abre, la medusa respira y la vela, la red y las
banderas se mueven solo de cerca, y que nada del decorado se sale de su huella ni flota en las dunas (hundimiento).

### Decorado militar (la tropa de Tortunavy)

Recetas en `Private/World/Beach/TN_BeachMilitaryMeshes.h`, que `TN_BeachPropMeshes.h` incluye al final: `BuildDecor` le
pasa los siete elementos militares (`BuildMilitaryDecor`) y `NumVariants` le pregunta sus variantes
(`NumMilitaryVariants`). Todo lo demás es lo del decorado: malla compartida por variante, colisión simple en el
`BodySetup`, hundimiento e inclinación por ejemplar y `SizeScale` en el componente. Paleta de la tienda del General
Galápago del cuartel (verde oliva, caqui, azul marino y oro). La **escarapela de Tortunavy** es una estrella dorada de
cinco puntas sobre un disco azul marino con aro dorado (cajas, bidones, cascos); la **bandera de Tortunavy**, azul marino
con la estrella dorada por las dos caras. Alturas para la tortuga (1,4 m; salta 1,2 m): escalones de 80 cm como mucho y
1,1-1,2 m para cubrirse.

| Pieza (`ETNBeachElement`) | Real | En el juego | Variantes | Colisión | Triángulos |
|---|---|---|---|---|---|
| Parapeto de sacos terreros (`Sandbags`) | sacos de 4,3 × 2,4 × 1,2 cm | sacos de 1,2 × 0,66 × 0,34 m; 4 hileras (1,2 m) con banqueta de 2 (63 cm); recto de 14,4 m, media luna de 5,4 m de radio | 6: recto; media luna; recto a medio desmoronar; esquina en L con la bandera; nido redondo de 3 hileras (91 cm, 8 m) con bandera, caja y un soldadito vigía; media luna a medio desmoronar | una caja por tramo de igual alto (extremos en escalera de 28 cm; en los arcos, tramos de 20° como mucho) y una por saco caído | 2300-3750 |
| Caja de munición (`AmmoCrate`) | 5,4 × 3,1 × 2,7 cm | 1,52 × 0,88 × 0,75 m; cartuchos de 76 cm; lata de 0,9 × 0,42 × 0,57 m | cerrada con una lata y cartuchos sueltos; tres pilas en escalera (0,75, 1,5 y 2,25 m); abierta y llena de cartuchos con la tapa de rampa (28°) hasta el canto; volcada con los cartuchos desparramados y otra caja con la lata encima | caja por caja (la abierta: paredes, fondo y el lecho de cartuchos a 44 cm) y la tapa | 700-2500 |
| Erizo antitanque (`TankTrap`) | barras de 28 cm (raíles de un tren de juguete) | barras de 7,8 m a 35°: cruce a 2,25 m (se pasa por debajo entre dos patas), puntas a 4,5 m | raíles oxidados de doble T; raíles con algas colgando; madera a la deriva atada con cuerda; pareja pequeña (raíl y madera) | una cápsula por barra | 280-800 |
| Casco militar (`MilitaryHelmet`) | 23,6 × 20 cm, 9,5 de hondo | 6,6 × 5,7 m, 2,65 m de hondo | boca abajo y medio enterrado con la escarapela (asoma 1,7 m; la banda de abajo es empinada, pero desde ~50 cm la cúpula se anda: se sube de un salto); boca arriba como un cuenco (borde a 90 cm, arena y un charco dentro a 18 cm); apoyado en un palo, con redecilla y una lata escondida debajo (la boca, a 2 m: se mete una debajo); de lado como una cueva (boca de 4,3 × 7 m, suelo de arena) | 8 cajas finas tangentes a la cúpula por banda de latitud (hueco por dentro), el suelo de arena y el palo | 520-720 |
| Red de camuflaje (`CamoNet`) | 71 × 56 cm sobre palos de 13-16 cm | 20 × 15,6 m, palos de 3,7-4,5 m | plana sobre cuatro palos y uno en medio (lados a 1,7 m) con un faldón que se mece; a dos aguas como un túnel (faldones de 26° que se suben hasta la cumbrera, a 4,3 m); caída por un lado (rampa de 16° hasta un techo a 3,8 m del que se salta); la plana sobre un puesto de vigía (anillo de sacos, caja y soldadito) | los palos; en el túnel, los faldones; en la caída, la rampa y el techo | 1200-3600 |
| Bidón (`Jerrycan`) | 7,7 × 2,9 × 10,7 cm | 2,15 × 0,8 × 3 m | de pie, verde oliva con la escarapela (3 m: para cubrirse); tumbado, caqui (80 cm: un escalón); dos tumbados en cruz, verde y rojo (80 y 160 cm); rojo de gasolina tumbado con el tapón abierto y un charco tornasolado delante | caja | 350-700 |
| Soldaditos de juguete (`ToySoldiers`) | 5 cm | 1,4 m (como la tortuga), peana de 64 × 48 cm | 8: fusil; prismáticos; bazuca de rodillas; tumbado; pareja; trío con uno volcado; uno volcado y otro mirando; patrulla de tres | una cápsula por soldadito (el tumbado, una caja de 40 cm que se sube) | 500-1600 |

- **Orientación**: los parapetos siguen la de los alargados del reparto: el largo por la X local, la cara alta hacia -Y
  y la banqueta y la bandera hacia +Y (lo que se defiende). No giran al azar (solo ±6°): así el reparto los pone en arco
  delante de un puesto. El resto gira libre.
- **Movimiento** (solo de cerca, como el resto del decorado): la bandera de Tortunavy ondea (esquina y nido) y el faldón
  suelto de la red se mece. La cámara atraviesa todo lo militar (es bajo o hueco).

**Probar.** `TN.Beach.Place Sandbags 1 0 <semilla>` (y `AmmoCrate`, `TankTrap`, `MilitaryHelmet`, `CamoNet`,
`Jerrycan`, `ToySoldiers`; la variante sale de la semilla, así que conviene probar varias). Subir al parapeto por la
banqueta y por los extremos en escalera y agacharse detrás; subir la escalera de cajas hasta 2,25 m y la tapa de la caja
abierta y meterse dentro; pasar por debajo del erizo grande entre dos patas; subir de un salto al casco enterrado,
meterse en el cuenco, debajo del casco apoyado en el palo y dentro del casco de lado; cruzar el túnel de red y subir por
sus faldones hasta la cumbrera, subir por la red caída y saltar desde el techo; subir a los bidones tumbados y cruzados.
Que nada se salga de su huella (`TN.Beach.ShowFootprints 1`) ni flote.

## Trampas e interacciones (`World/Beach/`)

| Archivo | Qué es |
|---|---|
| `TN_BeachBarbedWire.*` | `ATN_BeachBarbedWire`: alambre de espino enrollado; tocarlo aturde y empuja |
| `TN_BeachSeaweed.*` | `ATN_BeachSeaweed`: montón de algas que enredan (y algas que se enrollan a la tortuga) |
| `TN_BeachWobblyPlatform.*` | `ATN_BeachWobblyPlatform`: tabla o tapa de nevera sobre un hoyo; se tambalea y con dos se parte |
| `TN_BeachBrokenBucket.*` | `ATN_BeachBrokenBucket`: cubo tumbado y roto que se cruza como un túnel |
| `TN_BeachSpadeRamp.*` | `ATN_BeachSpadeRamp`: pala gigante, balancín con trampolín o puente-trampolín entre dos alturas |
| `TN_BeachShellGate.*` | `ATN_BeachShellGate`: puerta de dos hojas de conchas (en pared, entre rocas o desnuda) con interruptor |
| `TN_BeachSandDungeon.*` | `ATN_BeachSandDungeon`: castillo de arena con salas que se atraviesa |
| `TN_BeachFortress.*` | `ATN_BeachFortress`: fortaleza de arena que se sube (mediana, grande o colosal) con premio en la cima; consola `TN.Beach.PlaceBoosted` y `TN.Beach.Fortress.Top` |
| `Private/World/Beach/TN_BeachFortressKit.h` | Planta (medidas, subidas, atajos, huecos del pretil y premios) y malla de las fortalezas |
| `Private/World/Beach/TN_BeachBoostKit.h` | Lo potenciado: estrella, bandera y estandarte de Tortunavy, guirnaldas de banderines y fanfarria de los lanzadores de la cima |
| `Private/World/Beach/TN_BeachSignKit.h` | Cartel de madera de los lanzadores: tabla con el icono pintado, rótulo (TextRender), lado, rebote y brillo |
| `TN_BeachMine.*` | `ATN_BeachMine`: mina de juguete medio enterrada; al pisarla explota y lanza en bola hacia atrás |
| `TN_BeachMineSynth.*` | `UTN_BeachMineSynthComponent`: sonidos sintetizados de la mina (clic, pitido, explosión, lluvia de arena y rearme) |
| `TN_BeachClamTrap.*` | `ATN_BeachClamTrap`: almeja gigante que se cierra, atrapa unos segundos, tiembla echando humo y escupe mareada |
| `TN_BeachMovingPlatform.*` | `ATN_BeachMovingPlatform`: balsa que va y viene sobre un charco de verdad, o ascensor a una torre con catapulta |
| `TN_BeachCatapult.*` | `ATN_BeachCatapult`: cuchara sobre un tapón con cubito de contrapeso; lanza en bola hacia el mar una sola vez y queda partida (potenciada: dorada y mucho más lejos) |
| `TN_BeachTrampoline.*` | `ATN_BeachTrampoline`: medusa gorda, colchoneta, donut o sombrero de paja que rebotan y se deforman (potenciado: más alto y mucho más lejos) |
| `Private/World/Beach/TN_BeachRideKit.h` | Lo común de esas cuatro: hacia dónde queda el mar, quién puede montar, lanzar en bola, mareo y vaivén con el reloj del servidor |
| `TN_BeachTrapSynthComponent.*` | `UTN_BeachTrapSynthComponent`: sonidos sintetizados de las trampas (chispazo, «¡ay!», chof, crujido, chasquido, muelle, golpe sordo, conchas, roce de arena y clic) |
| `TN_BeachTrapCommon.*` | Reloj del servidor suavizado, estallidos de partículas low-poly y texto emergente («¡AY!», «¡CRAC!») |
| `Private/World/Beach/TN_BeachTrapKit.h` | Reglas comunes (quién simula a quién, quién puede caer en una trampa) y piezas de arena |
| `Private/World/Beach/TN_BeachDebugCommands.cpp` | Consola `TN.Beach.Place` |

**Convenciones** (las de `TN_BeachLayout.h`): origen en la arena, en el centro; X local = sentido de la carrera (se
entra por -X y se sale por +X); los alargados (el alambre) tienen el largo por X y la huella es su semigrosor en Y. Todo
cabe dentro de la huella del contrato con su `SizeScale` (lo que tiene medidas de tortuga, como puertas, pasillos y
peldaños de 40 cm, no se escala: se ajusta la planta). Mallas en ejecución con color de vértice
(`M_CosmeticVertexColor`, kit del parque del lobby), `RF_Transient | RF_DuplicateTransient`; colisión convexa por
piezas. Todas son relevantes por distancia y duermen en red (`ATN_BeachElement::ApplyRoundNetProfile`, ver «Rendimiento y
red») con `ForceNetUpdate` en cada cambio (las despierta). Solo
cuentan las tortugas vivas, fuera del caparazón y sin aturdir (`TNBeach::IsTurtleStunned`): los enemigos no caen en
trampas y una tortuga aturdida no se engancha ni se pincha. Variantes con la semilla del `Spec`.

**Qué significa `Spec.Extent`** en cada una: alambre = largo (0 → 24 m); algas = ancho máximo en Y (0 → libre; para
meterlas en un pasillo); puerta de conchas = ancho del hueco de una puerta «desnuda» (0 → con su pared o sus rocas); el
resto no lo usa.

### Alambre de espino (`ATN_BeachBarbedWire`)

- Concertina de rollos de 66·`SizeScale` cm de radio (52-84: ~1,3 m de alto) a lo largo de X, ±56 cm en Y, con pinchos
  en cruz, un hilo tenso por encima, estacas de madera cada ~7 m y trozos sueltos en los extremos. Bloquea (cajas por
  tramos de ~4 m; la cámara las atraviesa).
- **Servidor**: si la cápsula queda a menos de 14 cm de la colisión (a los lados o encima), `TNBeach::StunTurtle` 1,2 s
  (`StunSeconds`) con un empujón de 750 cm/s hacia el lado del que venía (si ya está encima, al contrario de su marcha)
  y 420 hacia arriba. Una vez por tortuga cada 2,7 s (aturdimiento + `HitCooldown` 1,5 s).
- Efectos en todas las máquinas (multicast no fiable): chispas, esquirlas, chispazo y «¡ay!» sintetizados y un «¡AY!»
  rojo que sale flotando sobre la tortuga.

### Algas que enredan (`ATN_BeachSeaweed`)

- Elipse de 0,9·huella (630 cm con `SizeScale` 1) en X y 78-100 % de eso en Y (o `Extent`/2), pila de 25-65 cm de
  gotas verdes y marrones, cintas onduladas con vesículas, siete tallos de pie que se mecen y mancha de arena mojada.
  Sin colisión: se vadea con las patas dentro.
- **Enganche** (servidor): quien pisa el 85 % central con los pies en el suelo se queda enganchada 3,2 s
  (`CatchSeconds`): anda a 55 cm/s (`HeldSpeed`, también en el aire y en el panzazo) y el salto no la levanta
  (`JumpZVelocity` = 0: cada intento es un tirón y la animación de salto). Cada salto adelanta la suelta 0,4 s
  (`JumpTug`) y cada meneo (cambiar de golpe la dirección del movimiento, como mucho uno cada 0,12 s) 0,2 s
  (`WiggleTug`): machacando el salto sale en ~1,3 s. Si la arrastran fuera de la elipse (×1,2), se suelta. Tras
  soltarse, 2,5 s de gracia (`ImmuneSeconds`); mientras siga dentro vadea a 320 cm/s (`WadeSpeed`). Hasta 4 a la vez.
- **Red**: `Catches` replicado (tortuga, hora de la suelta y tirones). El tope de velocidad (`UTN_StaminaComponent`) y
  el salto se tocan en el servidor y en el cliente dueño, como `TN_SlowZoneVolume`; el dueño predice el enganche al
  pisarla (si el servidor no lo confirma en 0,6 s, lo deshace) y siente sus tirones al momento. Las hebras que se
  enrollan (5 por tortuga: suben en espiral al engancharse, se sacuden con cada tirón y caen al soltarse) y los chofs
  salen del estado replicado en cada máquina. Si la aturden, se suelta sin tocar el tope del caparazón.

### Plataforma sobre un hoyo (`ATN_BeachWobblyPlatform`)

- **El hoyo va en la malla** (el terreno no se cava, `TN_BeachLayout.h`): cráter de arena amontonada con el fondo a ras
  del suelo. Con `SizeScale` 1: borde de fuera a 870 cm, cresta redondeada de 90 cm de ancho a 240 de alto (0,27·huella,
  180-240), ladera de fuera a 30° (se sube andando), pared de dentro empinada y hoyo de 364 de radio arriba; en el lado
  +Y, una brecha de ~2,4 m por la que se sale andando del fondo. Fondo mojado, guijarros, una concha y un banderín en
  la cresta.
- **Encima**, a lo largo de X y apoyada 70 cm en la cresta por cada lado: tabla vieja de tres tablones (868 x 220 x 24,
  con travesaños, clavos y una grieta pintada en el centro) o tapa de nevera (plástico blanco con reborde de color,
  bisagras y pegatina; hasta 300 de ancho y 28 de grueso), según la semilla.
- **Tambaleo** (cada máquina con las tortugas que ve encima; la tabla es una base móvil): se ladea 4,5° por tortuga
  según dónde pise (hasta 7°, `MaxRollDeg`), cabecea hasta 2°, se mece al andar y los aterrizajes (caída > 250 cm/s) la
  sacuden; muelle poco amortiguado (~1,2 Hz). Crujidos al pisar y al andar.
- **Rotura** (servidor): con 2 o más tortugas a la vez (`BreakRiders`) la grieta sube y en 1,1 s (`CrackSeconds`) se
  parte; si se bajan, baja a 0,45/s. Mientras, tiembla, se hunde unos centímetros y cruje cada vez más agudo y seguido,
  soltando astillas. Al partirse: chasquido, astillas, «¡CRAC!», las mitades resbalan 70 cm hacia dentro y caen (0,5 s,
  con un botecito) hasta el fondo y a los 0,8 s son rampas de ~34° de la cresta al fondo. Quien estuviera encima cae al
  hoyo y sale por la brecha o subiendo por una mitad. No se recompone en la ronda. `BrokenAt` (hora del servidor)
  replicado: quien llega tarde las ve ya caídas.

### Cubo roto (`ATN_BeachBrokenBucket`)

- Cubo de juguete de 18 cm a escala: 504 de largo, 238 de radio en la boca y 182 en el culo, pared de 26 (x0,7-1,15
  para caber en la huella). Tumbado a lo largo de X, apoyado en la arena: boca en -X con rampa de arena (170) al suelo
  de dentro (plano, a 69 cm), culo en +X con un agujero dentado del 74 % del radio y una lengua de arena (150) que baja.
  ~2,5 m libres en la salida: se cruza de pie. Asa caída sobre el lomo, nervios, reborde, pegatina de estrella, grietas y
  colores de juguete desteñidos. Estático (sin Tick).

### Pala (`ATN_BeachSpadeRamp`)

- Pala de 1120 cm con `SizeScale` 1 (entre 700 y 1150 según la huella): hoja del 36 % (336 de ancho) y mango de 84 de
  ancho y 34 de grueso por el que se anda. Dos variantes según la paridad de `Spec.Seed`:
- **Balancín** (par): sobre una piedra redonda (fulcro a 150 cm), la hoja en la arena (-X) y el mango en alto (+X,
  ~3,3 m; 17° en reposo). **Trampolín**: saltar en el último 20 % del mango lanza 1250 cm/s arriba y 700 hacia +X
  (conserva la mitad de la velocidad de lado). **Vuelta**: si una tortuga cae de un salto sobre la mitad del mango (a más
  de 60 cm del fulcro, con más de 380 cm/s de caída), la pala gira: el mango golpea la arena en 0,16 s (golpe sordo,
  muelle, polvo), se queda hasta 0,9 s y vuelve sola a los 2,2 s. A quien esté en la hoja lo lanza 1500 arriba y 1050
  hacia +X (más lejos que el trampolín). Inmunes a la caída hasta aterrizar.
- **Puente-trampolín** (impar): montículo de arena (mango, 70-100 cm de alto, laderas < 32°) y roca de 2,2-2,8 m (con un
  charco en medio); la pala sube de uno a otra (~20-30°) y el 70 % de la hoja asoma por fuera de la roca como un trampolín de
  piscina: saltar en su 30 % de la punta lanza igual que arriba.
- **Red**: el trampolín lo aplican el servidor y el cliente dueño dentro del mismo movimiento (el salto cambia el modo de
  movimiento y el lanzamiento entra en ese paso), como la medusa del lobby. La vuelta la decide el servidor (`FlipAt`
  replicado; cada máquina gira la pala desde esa hora) y lanza a las víctimas; el cliente de cada víctima aplica el mismo
  lanzamiento al recibir el aviso (fiable), así la corrección queda pequeña.

### Puerta de conchas (`ATN_BeachShellGate`)

- Hueco de 300 x 330 con dos hojas de 147 de ancho: mosaico de conchas de vieira en filas escalonadas por las dos caras,
  marco de cuerda y una perla de tirador; bastidor de madera de deriva con estrella. Según la semilla, en una pared de
  arena de ±(0,95·huella − 10) (±560 cm), 4,4 m de alto y 2 m de grueso con almenas y conchas, o entre dos rocas de
  ~4,2 m. Interruptor: concha grande en una peana de 85 cm de radio a 3,8 m por delante (-X) y a un lado, con un
  caminito de conchitas hasta la puerta.
- **Reglas** (servidor): se abre empujando 0,6 s (`PushSeconds`: andar contra una hoja cerrada, pegada a ella, desde
  cualquier lado; se abre hacia el otro) o pisando el interruptor (hacia +X). Se abre en 0,45 s hasta 95° (`OpenDeg`,
  con un pasito de más), sigue abierta mientras haya alguien en el hueco (±2,1 m) o en el interruptor y 3 s más
  (`OpenHold`), y se cierra en 0,7 s; si alguien entra mientras se cierra, se vuelve a abrir. Las hojas solo chocan
  cerradas. Castañeteo de conchas y roce de arena al moverse; clic al pisar el interruptor.
- **Desnuda** (`Extent` > 0, la usa el castillo): solo bastidor (con dintel hasta `BareDoorHeight` = 420), hojas e
  interruptor, siempre a -Y.

### Castillo de arena con salas (`ATN_BeachSandDungeon`)

- Con `SizeScale` 1: 57,6 x 44 m (el recorrido por dentro, ~70 m; rodearlo por el hueco del embudo de sus alas, más).
  Murallas de 2,4 m de grueso y 8,2 m de alto con almenas, cuatro torres de 11,5 m con banderas, zócalo de 50 cm (el
  suelo de dentro). Todo con colisión que también para la cámara.
- **Recorrido**: arco de entrada (4 x 4,6 m, a -Y) con rampita → **sala de las columnas** (planta baja, tres columnas de
  cubos) → **puerta de conchas** desnuda de 3,2 m con su interruptor en la sala → **pasillo de las algas** (3,6 m de
  ancho, techo a 4,3 m con lucernarios, algas que enredan) → **escalera** de 8 peldaños de 40 x 45 cm → **sala de las ventanas** (piso de arriba,
  +3,2 m, con la terraza sobre el pasillo): dos muretes de 70 cm que se saltan o se rodean por el hueco de cada uno y
  ventanas de 2 x 1,9 m (a 80 cm del suelo) al mar y a la playa; se puede saltar por ellas (~4 m) → **salida** por la
  puerta alta de la muralla +X (3,6 x 4 m) a una rampa de arena de 6,4 m (30°).
- Las piezas de dentro las crea el servidor con `ATN_BeachElement::SpawnElement` (puerta de conchas `Extent` 320;
  algas de `SizeScale` 0,3-0,6 y `Extent` 320) y las destruye con el castillo. Con `bSpawnEnemiesInside` (apagado)
  crearía además un erizo (0,35) junto a la escalera y un cangrejo (0,4) en la sala de las columnas: está apagado porque
  los enemigos de ahora no bajan de 0,75-0,8 de tamaño, se alejan 16-38 m de su sitio sin chocar con paredes y buscan
  el suelo desde arriba (se subirían al techo del pasillo).

### Fortalezas de arena (`ATN_BeachFortress`)

`FortressMedium`, `FortressLarge` y `FortressColossal` (huellas de 22, 34 y 50 m). Castillos inmensos que se suben
enteros andando y saltando, con premio en la cima. Planta y malla en `TN_BeachFortressKit.h`; X local = sentido de la
carrera (el reparto las gira con su +X hacia el mar, ±10°). `SizeScale` se recorta a 0,85-1,2: por debajo no caben los
pasillos de tortuga (el reparto usa 0,94-1,06). Todo escala con él salvo lo que tiene medidas de tortuga (anchos de
rampa y escalera, peldaños, cornisa, puertas y pretiles); el número de torrecillas sale de la altura que salvan.

**Qué es**: una muralla cuadrada con cuatro torres, adarve, puertas y patio y, en medio, una «mota» de terrazas macizas
de arena de molde, cada una más alta y más pequeña que la anterior; la última es la cima.

- **Muralla**: adarve arriba con pretil de 55 cm por fuera (se salta; no se pasa andando) y almenas de adorno, sin pretil
  por dentro. Puertas de 4,4 m de ancho a -X (de tierra) y a +X (del mar), con arco, concha de clave y dos estandartes de
  Tortunavy: el patio se cruza de una a otra. Torres en las esquinas con el adarve pasando por arriba, anillo de pretil
  por fuera y un cubo de juguete boca abajo de torreta (asa, nervios y pegatina de estrella) con la bandera de Tortunavy.
- **Patio**: franjas de 4,6 m (5,8 m en la colosal) entre la muralla y la primera terraza, con guijarros, charquitos y
  una estrella grande.
- **Terrazas**: ventanas y una puerta de adorno, estandartes en la cara del mar, conchas y estrellas incrustadas, pretil
  con almenas en sus bordes (con huecos donde llega cada subida), banderines en las esquinas de las de en medio y cuatro
  banderas de Tortunavy en las esquinas de la cima.

| Con `SizeScale` 1 | Mediana | Grande | Colosal |
|---|---|---|---|
| Muralla (lado, grosor, adarve) | 28 m, 3,8 m, a 5 m | 43 m, 4,4 m, a 6,5 m | 64 m, 5,2 m, a 8 m |
| Torres (radio; cubo y bandera hasta) | 2,9 m; 10,5 m | 3,4 m; 12,9 m | 4 m; 15,4 m |
| Terrazas (lado a altura) | — | 25 m a 11 m | 42 m a 12,5 m y 28 m a 19 m |
| Cima (lado a altura) | 11,2 m a 8,5 m | 12,6 m a 16,5 m | 14,8 m a 25,5 m |
| Con las banderas de la cima | ~14 m | ~22 m | ~32 m |
| Radio real (esquina de las torres) | 21,3 m | 32 m | 47,1 m |
| Puertas (ancho x alto) | 4,4 x 3,8 m | 4,4 x 4,8 m | 4,4 x 4,8 m |

**Subidas** (sin espejo; según la semilla todo se refleja en Y y la espiral gira al otro lado):

- **Por fuera**: rampa de 3,8 m de ancho y 20° pegada a la cara -Y de la muralla (sube hacia +X y acaba antes de la
  torre: 13,7 / 17,9 / 22 m de largo) y escalera de 3 m de ancho por la cara +Y (13 / 17 / 20 peldaños de 45 cm y
  38-40 cm de alto, sube hacia -X). Las dos llegan al adarve por un hueco del pretil.
- **Por dentro**: escalera de 2,6 m del patio al adarve de +X, junto a la puerta del mar (13 / 16 / 20 peldaños), y la
  **rampa del patio**, que llena la franja +Y y sube hacia +X del suelo a la primera terraza (28° / 20° / 14,5°; 16 /
  30 / 48 m). A media subida pasa a la altura del adarve de +Y y se cambia de uno a otro de lado.
- **De terraza en terraza**: rampas de 3,4 m pegadas a la cara de la de arriba, alternando -Y (subiendo hacia -X) y +Y
  (hacia +X), de 24-25°: la espiral. En la grande, una (11,8 m) a la cima; en la colosal, dos (14,6 y 14 m). En la
  terraza queda un paso de 2,8-3,6 m junto a cada rampa.
- **Atajos arriesgados**:
  - **Salto de torrecillas** (franja -Y del patio): torrecillas de cubo (radio 0,95 m; 1,55 m en la colosal) que se
    saltan del adarve de -Y a la primera terraza, alternando junto a la muralla y junto a la terraza: 6 / 8 / 8, cada una
    ~50 cm más alta, con ~1,1 m de hueco. Caerse es volver al patio; entre ellas se pasa en eslalon.
  - **La pala y la cornisa**: una pala de juguete tendida (hoja sobre el adarve de -X y mango de 70 cm de ancho de 3,9-5,1
    m sobre el patio, a la altura del adarve) lleva a una cornisa de 70 cm colgada de la cara -X de la primera terraza, que
    sube a 30° (6 / 7,8 / 7,8 m) hasta su borde.
  - **Colosal**: otra fila de 10 torrecillas (radio 1,6 m, ~59 cm cada salto) por la terraza +X de la primera a la segunda.

**Premio en la cima** (lo crea el servidor al construirla, `bSpawnPrizes`, y lo destruye con ella: ronda nueva o
`TN.Beach.Place clear`):

- **Lanzador potenciado** (`TNBeach::FlagBoosted`) en el borde +X, mirando al +X de la fortaleza (si no queda a ±45° del
  mar, se gira solo hacia él): catapulta el 55 % de las veces (tamaño 0,89 / 1,02 / 1,15: brazo de 9,8-11,5 m de lado a
  lado de la cima) y trampolín el resto (0,58 / 0,65 / 0,76). Ver «Catapulta» y «Trampolín».
- **Cofre** (`TreasureChest` → `ATN_BeachChest`, con `SpawnElement`) en el cuarto -X del lado contrario al cartel del
  lanzador (`TNBeachSignKit::SideOf` de la semilla del lanzador), con el frente hacia el mar: lo que suelta cae en la
  cima. Si aún no existe su clase, la cima va sin cofre
  (se registra).
- **Conchas de puntos** (`ATN_ScorePickup`, las del botín: suman a `RaceScore`), en las esquinas de la cima: 100 + 50
  (mediana), 100 + 50 + 50 (grande) y 100 + 100 + 50 + 50 (colosal). Además, una de 50 al final de cada atajo en las
  terrazas de en medio (grande: 2; colosal: 3). En total: 150, 300 y 450 puntos.

**Dónde cae lo que sale de la cima** (lanzadores potenciados, desde el centro de la fortaleza hacia su +X; el reparto
deja libre esa franja): catapulta, la bola cae a ~80-90 m (±6 m de lado) y aún rebota y rueda; trampolín, 45-55 m
andando y 55-66 m esprintando.

**Red**: como el resto de elementos, solo replica `Spec` (dormida: no cambia nada después). Cada máquina construye la
misma malla con la semilla (con su espejo); los cascos convexos (~110-190 según el tamaño) también paran la cámara.
Registro: `[Playa] Fortaleza ...: cima a N m con catapulta potenciado, cofre y K conchas (P puntos).` (y en `Verbose`,
medidas, cascos y triángulos).

**Probar**: `TN.Beach.Place FortressColossal 1 0 <semilla>` (y `FortressMedium`, `FortressLarge`), mirando hacia donde
quieras que lance: sale a ~54 m delante (la colosal) con su premio. `TN.Beach.Fortress.Top [jugador]` sube a esa tortuga a
la cima de la fortaleza más cercana, detrás del lanzador. Semillas seguidas para ver catapulta o trampolín y el espejo.

### Mina (`ATN_BeachMine`)

- **Aspecto** (con `SizeScale` 1; las mallas son de ese tamaño y las comparten todas las minas del mismo aspecto, que
  escalan sus componentes: en una ronda hay decenas): montoncito de arena removida de 1,28 m de radio con una mina de
  juguete de ~5 cm (1,4 m) medio enterrada: plato verde oliva con franja amarilla, bote gris de tres pinchos, plato
  oxidado con percebes o juguete caqui con botón rojo, según la semilla. Asoman la tapa (22 cm; el bote, 16) y el pincho
  de la espoleta (hasta 30 cm por encima); un piloto rojo da un destello cada 1,6 s. El 45 % lleva una banderita roja de
  aviso con franja blanca a 1,9 m (1,85 m de alto). Todo cabe en la huella (3,5 m). Sin colisión: se pisa.
- **Pisada** (servidor): una tortuga viva, fuera del caparazón y sin aturdir con los pies sobre la tapa (a menos de
  70 cm + el 45 % del radio de su cápsula en planta y entre 60 cm por debajo y 45 cm por encima de la tapa: también
  cayendo encima de un salto; saltándola por encima, no). «¡clic!» (doble clic de plástico y el texto), la tapa se hunde
  5 cm y parpadea en rojo pitando cada vez más deprisa (de 5 a 17 veces por segundo) durante `FuseSeconds` (0,4 s).
- **Explosión** (servidor): toda tortuga viva a menos de `BlastRadius` (2,3 m) —y la que la pisó hasta el doble, aunque
  se haya alejado durante la mecha— sale en bola hacia atrás en la carrera (contrario al mar del generador; sin
  generador, hacia -X de la mina): `TNBeach::StunTurtle` 2,4 s (`StunSeconds`) con 420 cm/s hacia atrás (`LaunchBack`),
  950 hacia arriba (`LaunchUp`) y hasta 160 de lado según dónde estaba (si la pisan dos, no caen juntas): ~1,9 s de vuelo
  y ~7-8 m hacia atrás. Las de alrededor, hasta `PushRadius` (7 m), en pie y sin aturdir, reciben un empujón sin
  aturdir hacia fuera y algo hacia atrás (a quien va delante no le da un acelerón): de 750 cm/s y 450 hacia arriba
  (`PushSpeed`, `PushUp`) junto a la explosión a un tercio en el borde. Lo aplica solo el servidor: el dueño lo recibe
  con el movimiento replicado (antes lo repetía al llegar un multicast y salía empujado dos veces). Las medidas escalan
  con `SizeScale`.
- **Efectos** (cada máquina con pantalla): fogonazo (bola de 2,4 m que crece y se apaga en 0,23 s y una luz naranja de
  0,28 s), bola de fuego, nube de arena, terrones, trozos de la carcasa, humo que sube 2-3 s, «¡BUM!», temblor de cámara
  (0,8 hasta 7 m, apagándose hasta 32 m) y, sintetizados (`UTN_BeachMineSynthComponent`), la explosión (chasquido que
  rasga, golpe grave que cae, retumbo y crepitar) y después la arena y las piedrecitas que caen. Las partículas se crean
  con la primera explosión de cada mina.
- **Cráter y rearme**: queda un cráter de adorno (suelo chamuscado de 1,5 m, reborde de arena de 26 cm hasta 2,6 m,
  rayas de quemado y trozos de la carcasa). **Decisión: se rearma.** A los `RearmSeconds` (9 s) la mina vuelve a asomar
  en medio del cráter con un botecito, un clic-clac y un poco de arena, y el cráter se queda de aviso: todas las
  tortugas se encuentran la misma playa, y mientras tanto quien viene justo detrás pasa sin peligro. Con
  `RearmSeconds` = 0 no se rearma en la ronda.
- **Red**: el servidor replica dos horas suyas, `TriggeredAt` (pisada) y `ExplodedAt` (explosión); cada máquina anima el
  parpadeo, los pitidos, la explosión, el cráter y el rearme con su reloj del servidor suavizado. Quien llega tarde ve el
  estado sin oírlo. Registro: `[Playa] Mina ...: explota (N en bola, M empujadas)` (y `Verbose`: aspecto y pisadas).
- **Probar**: `TN.Beach.Place Mine [Tamaño] 0 [Semilla]` (el aspecto y la bandera salen de la semilla). Pisarla andando,
  esprintando (sale lanzada igual) y cayendo encima; saltarla por encima; quedarse a 3-6 m de la que pisa otra (empujón
  sin bola); ver el cráter y el rearme a los 9 s; una tortuga aturdida o rodando en su caparazón no la pisa. Con un
  cliente y `p.NetShowCorrections 1`: la bola vuela igual en las dos pantallas y el empujón no se aplica dos veces ni da
  tirones de corrección.

### Segunda tanda: concha, plataforma móvil, catapulta y trampolín (comunes)

- **Orientación**: la catapulta, la plataforma móvil y el trampolín siguen su X local si ya mira al mar (±45°: el reparto
  los orienta así y les deja libre el arco de salto por delante); si no, se giran solos hacia el mar
  (`ATN_BeachRaceGenerator::GetSeaDirection`; sin generador, su +X). La concha se orienta hacia el mar ±25°. Todo gira
  dentro de la huella, que es redonda.
- **Quién monta**: tortugas vivas, fuera del caparazón, sin aturdir, sin derribar, sin que las lleve nadie y sin llevar a
  nadie (`TNBeachRideKit::IsFreeRider`); la catapulta, además, coge la bola de caparazón quieta en su cazo (ver «Catapulta»).
  La concha y la catapulta no actúan en el recuento ni en el podio.
- **Red**: nada replica posiciones. Las horas del servidor (cierre de la concha, disparo de la catapulta) se replican y
  cada máquina anima con el reloj del servidor suavizado; la plataforma se mueve con una función de la hora del servidor
  y una fase por la semilla. Los lanzamientos de la catapulta son bolas de caparazón con física (la caja se replica sola:
  sin predicción del movimiento). El rebote del trampolín y el saltito de la concha los aplican a la vez el servidor y el
  cliente dueño.
- **En cadena**: las bolas de la catapulta rebotan en los trampolines y en el charco salen del caparazón y nadan; la que
  para dentro de una concha y sale de la bola se la come; la torre del ascensor lleva una catapulta arriba.
- **Cartel** (`TN_BeachSignKit.h`; catapultas y trampolines, también los de la cima de las fortalezas y el del ascensor):
  para que se sepa de lejos qué es cada uno, una tabla de madera de 3,2 x 1,8 m en dos postes (arriba a 3,3 m) clavada
  en la arena **por el lado por el que se llega** (-X del marco) y a un lado (+Y o -Y según la semilla), sin colisión.
  Lleva el icono pintado con color de vértice y el rótulo en TextRender (58 cm de letra, crema sobre una franja marrón
  oscuro; se encoge si no cabe): el trampolín, una cúpula y una flecha roja que baja y rebota hacia arriba, «¡BOING!»;
  la catapulta, una palanca con su bolita y una flecha azul en arco, «¡CATAPULTA!». Los potenciados: tabla dorada con
  el marco, el icono y la franja azul marino, rótulo dorado, dos estrellitas en las esquinas y una estrella dorada de
  pie encima. Cuando la tortuga de esa máquina se acerca a menos de 9 m, el cartel da un botecito (se estira y se
  balancea ~1 s) y, a menos de 15 m, el rótulo se aclara poco a poco. Es cosmético y local de cada máquina.

### Concha que atrapa (`ATN_BeachClamTrap`)

- **Aspecto** (con `SizeScale` 1): almeja gigante de 32 cm reales, **9 x 6,5 m**, con la valva de abajo medio enterrada
  (labio en zigzag a 38 cm de la arena, montículo de arena alrededor que se sube andando, conchitas y guijarros), el manto
  de colores dentro (azul eléctrico, verde esmeralda con oro, morado con azul o dorado con ojos azules, según la semilla),
  sifón y una perla de ~50 cm que destella (estrella de cuatro puntas) cuando está lista. La valva de arriba, con cinco
  pliegues y anillos de crecimiento por fuera y nácar por dentro, está abierta **68°** sobre la charnela (a +Y o -Y según
  la semilla) y respira ±2°. Cerrada deja ~1,7 m libres dentro y una rendija de 5 cm entre los labios.
- **Cierre** (servidor): una tortuga libre que pisa el manto (el 72 % central de la elipse, con los pies en el suelo)
  la hace temblar **0,25 s** (`TellSeconds`: la valva sube 7° y castañetea) y cerrarse de golpe en **0,14 s**
  (`CloseSeconds`). Atrapa a la tortuga libre más cercana al centro que siga dentro (el 86 % central; una sola): quien
  corre y sale en el aviso se escapa. A las demás que estén en la valva las despide **700 cm/s** hacia fuera y **450**
  hacia arriba (su cliente aplica el mismo empujón). Sin nadie dentro se queda cerrada **1 s** y se abre.
- **Dentro** (**3,2-4 s** al azar, `HoldMin`/`HoldMax`): la presa queda en el centro, quieta y sin control (`MOVE_None`
  en el servidor y en su cliente a la vez, como el probador del lobby, y sin teclas de mover); su cámara pasa a la de la
  concha (fuera, del lado de la boca, a ~12 m y 4,7 m de alto) con fundido de 0,35 s. La concha vibra a sacudidas (la
  presa pataleando: una cada 0,62 s, +0-0,18 s) con golpes sordos, algún «¡ay!» ahogado y humo y burbujas de arena por la
  rendija; en los últimos 0,7 s tiembla sin parar y echa humo seguido. «¡ÑAM!» al atraparla.
- **Suelta**: se abre en **0,4 s** (con un pasito de más) y a los **0,16 s** (`SpitDelay`) la escupe de un saltito como
  al salir del huevo o del probador: **560 cm/s** hacia el lado de la boca y hacia el mar y **560** hacia arriba
  (~6 m), con «¡PTUI!» y burbujas. Queda **mareada 1 s** desde que aterriza (`DizzySeconds`; tope de 3 s desde el
  saltito): pajaritos del mareo y sin mover las patas; la cámara vuelve a ella en 0,4 s. No la vuelve a atrapar esa
  concha en 3 s.
- **Recarga**: **4,5 s** tras abrirse (`RechargeSeconds`) sin cerrarse, con el manto encogido y la perla apagada; al
  estar lista, el manto se abre, la perla destella y suena un «plin».
- **Se suelta antes** (sin saltito ni mareo) si la presa muere, la aturden, se mete en el caparazón, la derriban, la
  cogen o se desconecta; en el recuento o el podio la deja donde está (el GameMode la tiene congelada).
- **Red**: `State` replicado (`SnapAt`, `OpenAt`, `SpitAt`, `Captive`). El cliente de la presa la sujeta al recibir
  `Captive` y la suelta él solo a la hora `OpenAt + SpitDelay` (no espera a la réplica), igual que el servidor.
- **Probar**: `TN.Beach.Place ClamTrap [Tamaño] 0 [Semilla]`. Entrar andando al manto (se cierra y atrapa; la cámara sale
  fuera; tiembla y humea; escupe y mareo 1 s); cruzarla esprintando por el borde (escapar en el aviso); dos tortugas
  dentro (una atrapada, la otra despedida); pisarla durante la recarga (nada); `TN.Race.Stun` a la atrapada (se abre ya).
  Con un cliente atrapado: sin tirones al entrar ni al salir, y el saltito a la vez en las dos pantallas.

### Plataforma móvil (`ATN_BeachMovingPlatform`)

- **Balsa** (semilla par): charco dentro de un cráter de arena (cresta a **1,7 m**, taludes de 30° que se suben andando
  por fuera y por dentro), con agua de verdad **1,28 m** de honda (`ATN_ProcWaterVolume` local en cada máquina, solo en lo
  hondo: quien cae nada despacio y sale andando por el talud). Encima flota y va y viene a lo largo de X, de orilla a
  orilla (su punta se mete 60 cm en el talud: se sube y se baja andando), una chancla (6,8 x 2,5 m), una tabla de surf
  de juguete (6 x 2 m), un disco volador (5,6 m, gira 14°/s) o la tapa de una fiambrera (5,4 x 3,7 m), según la semilla
  (x0,85-1,15 con el tamaño). Recorrido **8 m** (`Spec.Extent`; se recorta para caber: charco de 12-14 m), **3,3 m/s**
  (`FerrySpeed`), **1,6 s** de espera en cada orilla (`FerryDwell`), meciéndose ±3 cm y ±1,2°. Andar encima en su
  sentido suma las dos velocidades. Embarcaderos de palos de polo en cada orilla; espuma, una hoja, una chapa y una
  concha flotando.
- **Ascensor** (semilla impar): torre cuadrada de arena de molde de **~13 m** de lado (0,54·huella de semilado) y
  **4,5 m** de alto (`Spec.Extent`, 2,5-4,8 m: saltar desde arriba no mete en el caparazón), con marcas de cubo, almenas en
  los lados ±Y, conchas y bandera. Por su cara -X sube y baja una bandeja o un disco volador de **3,9 m** colgado con
  cuatro cuerdas de una grúa de palos de polo con una chapa por polea: **1,7 m/s** (`LiftSpeed`), **2 s** abajo y
  **1,6 s** arriba. Arriba espera una **catapulta** (la crea el servidor, de tamaño 0,95·semilado/900 ≈ 0,69, y la
  destruye con la torre; `bCatapultOnTop`): desde 4,5 m lanza aún más lejos. Como todas, de un solo uso.
- **Base móvil**: la balsa y la bandeja son colisión convexa con nombre estable por red; quien va encima se mueve con ella
  sin resbalar (base de movimiento de UE: el cliente manda su posición relativa a la base, así que el desfase de reloj
  no corrige). Posición = `TNBeachRideKit::ShuttleAlpha(hora del servidor + fase por la semilla)`: arranca y frena
  suave, igual en todas las máquinas. Crujido o roce al salir; golpe o chapoteo al llegar, solo si la cámara local está a
  menos de 18 m (ver «Sonido de ambiente en la carrera»).
- **Probar**: `TN.Beach.Place MovingPlatform 1 0 2` (balsa) y `... 1 0 3` (ascensor); `... 1 1200 2` (balsa con 12 m de
  recorrido). Subir a la balsa en una orilla, cruzar (y andar encima), caer al agua y salir nadando por el talud; subir
  al ascensor, llegar arriba, usar la catapulta y saltar desde la torre. Con un cliente encima: sin resbalar ni tirones.

### Catapulta (`ATN_BeachCatapult`)

- **Aspecto** (con `SizeScale` 1): brazo de **11 m** (el 68 % del lado del cazo) apoyado como un balancín sobre un tapón
  de garrafa (rojo, azul, verde o blanco, con estrías y una cuna) encima de una piedra: eje a **2,5 m**. El brazo es,
  según la semilla, una cuchara de plástico de color, una de madera con vetas o dos palos de polo atados con gomas y un
  vasito de yogur por cazo. Cazo de **~3 x 2,2 m** y 30 cm de hondo (medidas de tortuga: no baja de 2,5 x 2 m), en
  reposo apoyado en la arena (el brazo a ~22°, se entra andando); en el otro extremo, un cubito de arena mojada
  (88 cm de radio, 70 de alto) y un palo de polo de pie que sujeta el mango en alto. Banderín verde (lista) o rojo.
- **Disparo** (servidor): una tortuga libre en el cazo (o su bola de caparazón quieta en él, ver más abajo) la arma: **1 s** de aviso (`WarnSeconds`: «¡AGÁRRATE!», el palo
  tiembla y cruje cada vez más agudo y seguido, el banderín parpadea); si el cazo se queda vacío 0,35 s, se desarma. Otra
  tortuga que sube por el mango y cae de un salto sobre el cubito (1 m por encima del mango) dispara al momento. Al
  disparar: el palo se parte y sale volando, el cubito cae, la cuchara da la vuelta en 0,16 s hasta -41° y rebota
  contra la arena (golpe, muelle, polvo, temblor de cámara, «¡ZAS!»). Las del cazo salen como **bolas de caparazón** a
  **2300 cm/s** y **44°** (±3°) hacia el mar con **±12°** de desvío y ±5 % de fuerza al azar (cada una la suya: no caen
  juntas); las del mango, al 55 %. La bola vuela ~30 m (más cuesta abajo o desde la torre), rebota, rueda y sale sola al
  pararse (o al caer al agua); no se puede salir en el aire. Ahorra camino, pero se cae donde toque: entre enemigos, en
  algas o en una concha.
- **El temblor del aviso es solo visual** (ronda 4; también en las potenciadas de las fortalezas y en la de la torre del
  ascensor: es la misma clase). Antes se sumaba a `ArmPivot`, que lleva las colisiones del brazo y del cazo, y el suelo se
  movía de verdad a ~10 Hz (±1,6° a 5,5 m del eje: ±15 cm, cientos de cm/s de suelo que empuja): despedía a la tortuga, o a
  la bola de caparazón, que esperaba en el cazo antes del disparo, y la posición de los pies en el espacio del eje
  (`WhereOnArm`) bailaba. Ahora `ArmPivot` y `BowlHinge` solo siguen el cabeceo real (`ArmPitchAt`, `BowlPitchAt`) y el
  temblor (`ApplyVisualShake`, solo en máquinas con pantalla) mueve las dos mallas visibles, `ArmMesh` y `BowlMesh` (sin
  colisión), respecto a sus padres: cabeceo hasta 0,65°, balanceo hasta 2,5° y guiñada hasta 0,5° sobre el eje, crecientes
  con el aviso (constantes `Shake*` del `.cpp`). El cazo, que cuelga de la bisagra del cuello, recibe el temblor llevado
  a su espacio (bisagra, temblor, bisagra inversa) para girar sobre el eje igual que el mango. El desfase con la colisión
  se queda en unos 5 cm, y al desarmarse o disparar las mallas vuelven exactas (identidad). El palo de polo y los
  banderines ya eran visuales. El cabeceo del disparo sí mueve las colisiones, a propósito (apagadas 0,55 s).
- **La bola de caparazón en el cazo también es pasajera** (ronda 4; la forma divertida de usarla: entrar en el cazo, meterse
  en el caparazón y esperar el disparo). Antes solo montaba quien estaba fuera del caparazón (`IsFreeRider`): la bola ni
  armaba ni salía lanzada, y cuando el temblor no la despedía, caía al apagarse la colisión del disparo. Ahora, en el
  servidor y sin tocar `TN_ShellBody.*` ni `TN_ShellComponent.*` (solo su API pública):
  - **Detección** (`BowlBallOf`): la tortuga metida en el caparazón con su caja física (`ATN_ShellBody`, la que se replica),
    viva, sin derribar, sin aturdir (la bola de aturdida no cuenta), sin que la lleve nadie, la sujete un enemigo o un
    gusano, o la recoloquen la tormenta o la red de seguridad; con la caja **quieta** (< 220 cm/s: la que pasa rodando no
    arma la catapulta) y su centro dentro del cazo, apoyada en su suelo (en el espacio del eje: `X` del cazo, `|Y|` < semiancho
    + 15 cm, `Z` de -40 a +110 cm del suelo del cazo). Cuenta como una de pie en el cazo (`Where` = 1): arma la
    catapulta, el aviso es el mismo (1 s) y dispara igual (o al momento si otra cae sobre el cubito).
  - **Quieta durante el aviso**: con la catapulta armada, cada tic del servidor pone a cero la velocidad lineal y angular de
    su caja (`SetPhysicsLinearVelocity` / `SetPhysicsAngularVelocityInRadians` de la primitiva `UBoxComponent`): ni la
    empuja otra tortuga que entra, ni resbala por el cazo inclinado. El temblor ya es solo visual. En los clientes no hace
    falta nada: su caja simula sola y la interpolación predictiva la lleva al estado del servidor, que está quieto.
  - **Disparo** (`LaunchBowlBall`, con las colisiones del brazo y del cazo ya apagadas): `SetExitLocked(true)`,
    `InitBody(tortuga, true)` (pública: fija que la caja salga sola al pararse y reinicia su edad, para el límite de 9 s; no
    crea otra caja) y `SetPhysicsLinearVelocity` con la velocidad de lanzamiento del cazo (`LaunchVelocity(1)`: 2300 cm/s a
    44° con desvío propio, o la potenciada) más el giro de volteretas de `StartBody` (7 rad/s). La réplica de física de la
    caja la lleva a los clientes, como cualquier empujón; no se recrea el cuerpo (`StartBody` haría `StopBody` y la
    levantaría del cazo) y, al pararse, sale del caparazón y se desbloquea como las demás lanzadas.
  - **Reserva del árbitro en vuelo** (`BeginFlight`, también para las que salen de pie): `TNBeach::ClaimTurtle(Launch, 10 s)`
    y se suelta al salir del caparazón (`TickFlights`; la catapulta gastada sigue con el Tick mientras haya lanzadas). Con
    la reserva `GetTurtleMover` dice `Launch` y no `Ball`, así que la tormenta no la patea como si rodara por gusto (si se
    alarga detrás del frente, la patada forzada de siempre). **Ojo**: la red de seguridad (`GuardUnderSand`) solo respeta la
    reserva `StormKick`, y solo actúa bajo la arena, no en el vuelo; una reserva `Launch` no la frena.
  - **Regla que no cambia**: la de pie sigue igual (`IsFreeRider`) y `TNBeachRideKit::LaunchAsBall` no se ha tocado.
- **Un solo uso** (`bSingleUse`, encendido; decisión de la ronda 3): la primera tortuga que la dispara la gasta y quien
  llega tarde se fastidia. Tras el golpe y el rebote (**0,62 s**) el brazo se parte por el cuello del cazo con un «¡CRAC!»,
  crujido y astillas: el cazo cae y se queda colgando de las astillas (~72° por debajo de la horizontal, con un vaivén
  que se apaga), el mango tumbado con el cubito en la arena, el palo en el suelo, astillas asomando del corte y **sin
  banderín** (ni verde ni rojo). No vuelve a armarse en toda la ronda; la colisión del cazo se apaga para siempre (la del
  mango se queda) y a los 4 s deja de hacer Tick. El brazo va en dos mallas (mango y cazo, este colgado de una bisagra en
  el cuello) y su colisión también. También la de la torre del ascensor y las de la cima de las fortalezas. Su cartel,
  con el crujido, se tuerce hacia fuera y hacia atrás (se pasa un poco y se asienta), lleva una cinta roja en aspa y
  dice **«¡ROTA!»** en rojo; quien llega tarde lo ve ya así.
- **Cartel**: a medio brazo largo, por el lado -X y a un lado del brazo (fuera del cazo, la piedra, los banderines y el
  arco, que va hacia +X), girado 20° hacia el centro para leerse al venir de frente.
- **Recarga** (solo con `bSingleUse` apagado): **4,6 s** (`ReloadSeconds`): rebote hasta 0,6 s, quieta hasta 1,1 s,
  vuelve a golpes de carraca (14 pasos, «clic» cada 0,2 s), el palo se pone de pie y al final el cazo se asienta con un
  botecito. La colisión del brazo se apaga 0,55 s al disparar (las bolas nacen dentro del cazo).
- **Potenciada** (`Spec.Flags & TNBeach::FlagBoosted`, la de la cima de las fortalezas): cuchara (o palos y vasito)
  dorada, cubito azul marino con reborde, asa y estrella doradas, tapón azul marino con estrías doradas, la bandera de
  Tortunavy en un segundo palo y una guirnalda de banderines entre los dos por encima del eje. Lanza a **3800 cm/s** y
  **40°** (±1,5°, ±3 % de fuerza) con **±4°** de desvío (`BoostedLaunchSpeed`, `BoostedLaunchPitch`,
  `BoostedDeviationDeg`): ~77 m en llano frente a los ~35 m de la normal (2,2 veces, con el rozamiento de la bola), y
  desde la cima de una fortaleza, 81-87 m. Al disparar: «¡ZAAAS!» dorado y grande, barrido más grave, muelle grave,
  golpe más fuerte, más polvo, temblor de cámara hasta 42 m y la **fanfarria** de los títulos de la carrera
  (sintetizada, a quien mire desde menos de 90 m, más fuerte cuanto más cerca). También de un solo uso.
- **Red**: `ArmedAt` y `FiredAt` (horas del servidor) replicados, con `ForceNetUpdate` en cada cambio; cada máquina anima
  el brazo, la rotura, el palo, el banderín y los sonidos desde ellas (quien llega tarde la ve ya rota, sin oírla). Lo
  potenciado sale de `Spec`.
- **Probar**: `TN.Beach.Place Catapult [Tamaño] 0 [Semilla]` mirando al mar y `TN.Beach.PlaceBoosted Catapult [Tamaño]
  [Semilla]` la potenciada. Meterse en el cazo (aviso y disparo; se parte y el cazo queda colgando), salir durante el
  aviso (se desarma), dos o tres en el cazo (salen abiertas), subir por el mango y saltar sobre el cubito con otra en el
  cazo, meterse en una partida (nada). Con la bola: entrar en el cazo, meterse en el caparazón y esperar (arma y sale
  lanzada; durante el aviso no se mueve), una bola en el cazo y otra tortuga que entra y la arma, una bola aturdida (no
  cuenta). Con un cliente lanzado: la bola vuela igual en las dos pantallas; un cliente que llega después la ve partida.

### Trampolín (`ATN_BeachTrampoline`)

- **Variantes** (por la semilla; con `SizeScale` 1, huella de 7 m): **medusa gorda varada** (campana de 10 m y 3,2 m de
  alto, rosa, lila o celeste, con trébol, motas, una cara que mira a la salida y ocho brazos orales tendidos en la arena);
  **colchoneta hinchable** (cinco tubos a rayas de 1,44 m y una almohada de 1,9 m, 9,8 x 6,9 m, con válvula); **flotador de
  donut** (12,3 m, 4,2 m de alto, glaseado rosa, de chocolate, celeste o menta con gotas y virutas; se puede caer en el
  agujero de 3,9 m); **sombrero de paja tenso** (ala de 13 m a 22-38 cm, que se pisa, y copa de 5,3 m y 2,2 m de alto
  con la tapa tensa, cinta y lazo). Como la medusa del lobby, rebota todo el cuerpo: cima, costados y borde, también de
  lado desde la arena (sin ir subiendo), con un «boing» sintetizado más grave en las grandes.
- **Rebote**: hacia arriba **1250 cm/s** (x1 medusa, x0,92 colchoneta, x1,08 donut, x0,96 sombrero; ~8 m de altura) más
  **0,55** por cada cm/s de caída por encima de 300, con tope de **2000** (~20 m): de trampolín en trampolín se sube cada
  vez más. Hacia delante se conserva el **75 %** de la velocidad horizontal y se suman **320 cm/s** hacia el mar (tope
  1100): 16 m andando, ~23 m esprintando. Sin meterse en el caparazón al caer. No rebota mientras sube a más de 150 cm/s
  (el rebote anterior o un salto); el boing y la deformación, como mucho uno cada 0,3 s por tortuga.
- **Deformación** (cada máquina con pantalla): se aplasta entera y rebota estirándose (10-26 %) y se hunde donde cae
  la tortuga (22-64 cm, en un radio de ~2,7 m) con una abolladura que vibra y se recupera en ~1 s (la malla procedural
  se actualiza solo mientras dura y si se ve). La medusa además respira.
- **Potenciado** (`Spec.Flags & TNBeach::FlagBoosted`, el de la cima de las fortalezas): hacia arriba **x1,25**
  (`BoostedUpScale`: 1560 cm/s en la medusa, tope `BoostedMaxUp` 2400) y hacia el mar **900 cm/s** de empujón
  (`BoostedForwardPush`, tope `BoostedMaxHorizontal` 1500): en llano, 39 m andando y 48 m esprintando (2,35 y 2 veces un
  trampolín normal); desde la cima de una fortaleza, 45-66 m. Aro dorado en la arena, cuatro palos con pomos dorados
  (uno con la bandera de Tortunavy) y guirnaldas de banderines de palo a palo por encima del borde. Cada rebote suma un
  boing más grave, un barrido de aire, destellos dorados y la fanfarria (como mucho una cada 3 s en cada máquina).
- **Cartel**: por el lado -X, 24° a un lado del eje (no delante del salto), a 80 cm por fuera de lo que rebota (en el
  potenciado, también por fuera de sus palos), con la tabla de cara hacia fuera.
- **Red** (#21): el rebote lo decide el movimiento de la tortuga (`UTN_TurtleMovementComponent::TickTrampolineBounce`) al
  empezar cada paso en que su cápsula toca el sensor (15 cm más grande), con las reglas puras de
  `TN_BeachTrampolineRules.h` y sin relojes: el servidor y el cliente dueño rebotan en el mismo paso, también al repetir
  pasos tras una corrección, y no hay corrección (antes llegaba por el golpe o el solape, un paso después o fuera del paso,
  con una espera según la hora del mundo de cada máquina). El resto lo ve por un multicast no fiable. Las bolas
  de caparazón rebotan también (las lanza el servidor, al 90 %). Lo potenciado sale de `Spec` (replicado): el servidor y
  el cliente dueño aplican el mismo impulso.
- **Probar**: `TN.Beach.Place Trampoline [Tamaño] 0 [Semilla]` (semillas seguidas para ver las cuatro) y
  `TN.Beach.PlaceBoosted Trampoline [Tamaño] [Semilla]` el potenciado. Caer encima, andar contra un costado, encadenar
  dos trampolines (el segundo rebote más alto), saltar desde uno por encima de un alambre; lanzar una bola con la
  catapulta encima de uno. Con un cliente: sin correcciones al rebotar (tampoco en el potenciado).

### Probar

- `TN.Beach.Place <Elemento|número> [Tamaño=1] [Extent=0] [Semilla]` en la ventana del anfitrión (desde un cliente del
  PIE se crea en el servidor del mismo proceso): lo pone apoyado en el suelo delante de tu tortuga y mirando hacia donde
  mira (su +X). Sirve para cualquier elemento de `ETNBeachElement`. `TN.Beach.Place clear` borra lo creado así.
  (`TN.Beach.Spawn` de la parte de enemigos hace lo mismo más simple, a 30 m.) Ejemplos:
  `TN.Beach.Place BarbedWire 1 2000`, `TN.Beach.Place Seaweed`, `TN.Beach.Place WobblyPlatform 1 0 2` (tabla) y
  `... 3` (tapa), `TN.Beach.Place SpadeRamp 1 0 2` (balancín) y `... 3` (puente), `TN.Beach.Place ShellGate 1 0 4`
  (pared) y `... 5` (rocas), `TN.Beach.Place BrokenBucket`, `TN.Beach.Place SandDungeon`.
- Con dos jugadores (anfitrión y cliente) y el log `LogTortunabo Verbose`:
  - **Alambre**: tocarlo andando y saltando encima desde los dos lados: bola aturdida 1,2 s lanzada hacia atrás, chispas
    y «¡AY!» en las dos pantallas; no vuelve a pinchar hasta 2,7 s después. Saltarlo si se llega.
  - **Algas**: entrar andando y cayendo; freno inmediato en el cliente sin tirones de corrección; hebras enrolladas en
    las dos pantallas; salir machacando el salto (~1,3 s), meneando o esperando 3,2 s; 2,5 s de gracia; aturdir a una
    enganchada (`TN.Race.Stun`) la suelta.
  - **Plataforma**: una tortuga la ladea y cruje sin romperla; dos a la vez la parten en ~1,1 s, caen al hoyo y salen por
    la brecha o por las mitades; el cliente ve la misma caída.
  - **Cubo**: entrar por la boca y salir por el agujero de pie, en los dos sentidos; la cámara dentro.
  - **Pala**: trampolín en la punta (sin corrección en el cliente); balancín: una en la hoja y otra saltando sobre el
    mango: la de la hoja sale volando, sin meterse en el caparazón al caer; la pala vuelve sola. Puente: subir por el
    mango y saltar desde la hoja.
  - **Puerta**: abrir empujando desde cada lado y con el interruptor; que no se cierre con alguien en medio; que se
    vuelva a abrir si alguien entra mientras se cierra.
  - **Castillo**: el recorrido entero con la puerta y las algas de dentro; saltar por una ventana; las piezas de dentro
    desaparecen con el castillo al cambiar de ronda.

## Enemigos y tormenta (`World/Beach/`)

| Archivo | Qué es |
|---|---|
| `TN_BeachEnemy.*` | `ATN_BeachEnemy` (abstracta, hija de `ATN_BeachElement`): base de los enemigos (red, tortugas, suelo, voz, golpes, apartarse, rodear el reparto, nivel de detalle, mareo por lo que se les lanza con sus pajaritos, `UTN_BeachEnemyDizzyComponent`, y tortuga sujeta en la boca o el pico) |
| `TN_BeachGiantCrab.*` | `ATN_BeachGiantCrab`: cangrejo gigante con una pinza enorme |
| `TN_BeachSeaUrchin.*` | `ATN_BeachSeaUrchin`: erizo de mar que rueda hacia ti |
| `TN_BeachLizard.*` | `ATN_BeachLizard`: lagarto que toma el sol; por semilla, huidizo, generoso (deja premio) o mordedor |
| `TN_BeachQuadLane.*` | `ATN_BeachQuadLane`: paso de quads que cruza la playa |
| `TN_BeachGullZone.*` | `ATN_BeachGullZone`: gaviotas y pelícanos que cagan (la cagada queda PINTADA en el caparazón con un decal y avisa con un signo de exclamación), bajan en picado y se llevan tortugas en el pico |
| `TN_BeachGullTuning.h` | Cifras y cuentas puras de las gaviotas (zona y justiciera): radios, persecución del blanco y su último tramo lanzado, plancha (ronda 4); las del cangrejo gigante, en `TNBeachCrabTuning` (`TN_BeachGiantCrab.h`); pruebas en `Private/Tests/TN_BeachGullTuningTest.cpp` |
| `TN_BeachStorm.*` | `ATN_BeachStorm`: la tormenta de bañistas (no es un elemento; la crea el GameMode) |
| `TN_BeachSandWorm.*`, `TN_BeachSandWormMeshes.h`, `TN_BeachSandWormSynth.*` | `ATN_BeachSandWorm`: el gusano de arena gigante que se come a las rezagadas al acabar la cuenta atrás (no es un elemento; lo crea el GameMode), sus mallas y sus sonidos |
| `TN_BeachCameraShake.*` | `UTN_BeachCameraShake`: temblor de cámara (no había ninguno en el proyecto) |
| `TN_BeachEnemySynth.*` | `UTN_BeachEnemySynthComponent`: sonidos sintetizados (golpes, graznidos, motor, viento) |
| `TN_BeachEnemyKit.h`, `TN_BeachEnemyMeshes.h` | Mallas low-poly por piezas, caché de mallas, sombras (también con borde nítido y opacidad por material), surcos en la arena, partículas propias, el pico de las aves, las motas y la cresta de los lagartos y el enganche a la espalda de la tortuga |
| `TN_BeachEnemyDebug.cpp` | Consola de pruebas |
| `TN_ThrowableItemActor.cpp`, `TN_InkProjectile.cpp` (fuera de `Beach/`) | Solo el gancho: lo lanzado que da a un enemigo de la playa lo marea (`ATN_BeachEnemy::FindProjectileHit`, `ApplyHitStun`) |

### Comunes

- **Escala**: todo a `TNBeach::Scale` (28). Se reutiliza la fauna low-poly (`TN_ProcMapFaunaMeshes.h`: gaviota, pelícano,
  cuadrúpedo del lagarto y sus ayudas de cuerpo y ojos), las partículas de `TNAmbientFX` (sin su registro global: cada
  actor lleva sus emisores), el velo de la tormenta del camino (`TNStormFX::BuildVeil`), la tos
  (`UTN_StormCoughComponent`) y el texto emergente de las trampas (`FTNTrapPopText`). Las mallas se construyen en
  ejecución y se comparten por paleta (referencia débil: el recolector las suelta cuando no queda ninguno). Colisión:
  solo el cuerpo del cangrejo (caja tipo Pawn); a los demás, lo que se les lanza les da por su cuerpo de golpes
  (`GetHitCapsule`, ver «Mareo por lo que se les lanza»).
- **Red** (servidor escucha): el servidor decide objetivos y golpes; los que andan replican `FTNBeachMoverRep` (suelo
  bajo el cuerpo cuantizado a 1 cm, giro en 16 bits, estado, hora del estado y un punto de interés) a 8-12 Hz y solo lo
  que cambia; los clientes interpolan con una pizca de extrapolación. El paso de quads, la zona de gaviotas y la
  tormenta replican solo horas y sentidos: cada máquina calcula la posición con el reloj del servidor. Los efectos van
  por multicast no fiable y el empujón del ragdoll, por multicast fiable. Siempre relevantes (con la distancia por
  defecto un cliente destruiría y rehará sus mallas cada vez que se aleja 150 m por la playa).
- **Carrera en marcha**: no atacan durante el recuento ni el podio (`RacePhase` = `RoundResults` o `Champion`); en
  `Waiting` sí (por si la fase no cambiara).
- **A quién se le da** (`ATN_BeachEnemy::CanBeHit`): viva, sin aturdir, sin derribar, sin ir en el pico de una gaviota
  o en la boca de un lagarto (`IsTurtleHeld`) y sin que la recoloquen la patada de la tormenta o la red de seguridad
  (`TNBeach::IsTurtleRelocating`). A quien ya está en el suelo no le da nadie (tampoco el empujón del lagarto). La
  tormenta patea a quien se queda detrás del frente y se mueve sola; lo que mueve otra cosa, al soltarlo (ver «Quién
  mueve a la tortuga» en «El bucle "torbellino" en la tormenta»). Cerca del frente, nada lanza a una tortuga hacia atrás.
  Si otro sistema le quita la tortuga a un enemigo (`ServerReleaseHeldTurtle`), el enemigo deja el ataque
  (`OnHoldAborted`).
- **Golpes variados**: no todo es la bola. El derribo (`ATN_BeachEnemy::KnockDownTurtle`, o `ServerKnockDown` desde la
  tormenta) llama a `TNBeach::KnockDownTurtle` (ragdoll y mareo de la piel de plátano; como poco 2,2 s tumbada y 0,75 s
  levantándose) con solo 0,6 m/s hacia abajo: lo que se le pasa lo aplica la cápsula al levantarse, porque queda
  pendiente en el movimiento mientras dura el ragdoll. El empujón de verdad va a los cuerpos del ragdoll en cada máquina
  (`FTNBeachRagdollPushes`: se guarda hasta que el ragdoll simula, un segundo como mucho), así sale lanzado igual en
  todas.

| Quién | Qué te pasa | Después |
|---|---|---|
| Cangrejo (mazazo) | despachurrada en bola aturdida 3,5 s (`StunTurtle`), empujoncito de 3,2 m/s hacia fuera | te ignora 6 s |
| Cangrejo (embestida) | derribo con ragdoll y mareo 2,4 s, lanzada a 9,5 m/s en su sentido, 2,6 hacia fuera y 4,8 hacia arriba dando vueltas (320°/s); «¡EMBESTIDA!» | te ignora 6 s |
| Erizo (pinchazo) | derribo con ragdoll y mareo 2,4 s, despedida a 6,2 m/s hacia fuera y 3,8 hacia arriba dando una vuelta hacia atrás (260°/s); «¡PINCHAZO!» | te ignora 4,5 s |
| Gaviota (cagada) | derribo con ragdoll y mareo 2,4 s, tumbada de espaldas (2,4 m/s hacia fuera y 1,2 hacia arriba) con la cagada pintada en el caparazón (12 s); «¡PLOF!» | la zona te deja 6 s |
| Gaviota o pelícano (picado) | colgada del pico 3,3 s, pataleando; al soltarte, bola aturdida lo que tardas en caer (~2,3 s) + 2 s; «¡ÑAC!» | la zona te deja 12 s |
| Quad (rueda) | derribo con ragdoll y mareo 3 s, lanzada a 9,5 m/s en su sentido, 3,8 de lado y 7,5 hacia arriba dando vueltas de campana (420°/s); «¡ATROPELLO!» | 1,2 s sin repetir |
| Tormenta (patada) | un bañista la manda a un sitio de arena abierta ~20 m por delante del frente, resuelto antes: en bola si el arco está libre (vuelo de 1,1-3,2 s, mareada el vuelo + 0,8 s) o de un salto con polvo; si la bola no llega, se la pone en su sitio; «¡PATADA!» | 3 s sin patadas |
| Lagarto huidizo (susto) | empujón de 6,5 m/s, sin derribar ni aturdir | — |
| Lagarto mordedor (mordisco) | en su boca 1,3 s, zarandeada, y lanzada de lado (6,5 m/s y 4,5 hacia arriba) en bola mareada 1,5 s; «¡ÑAM!» | te deja 10 s |

- **Muchos a la vez** (el reparto es ~3 veces más denso, con cangrejos en grupos): los que andan se apartan entre sí
  (`GetBodyRadius`: cangrejo 4,2 m, erizo 1,15 veces su radio de rodar, lagarto 3,8 m, todo por el tamaño; cada uno se
  aparta la mitad del solape en cada paso) y el cangrejo y el erizo rodean lo grande del reparto (trampas salvo las
  algas, y el decorado que cierra el paso con 3,8 m de huella o más; cápsulas al 85 % de su huella, las que quedan a
  menos de 1,6 huellas + 60 m de su sitio): se deslizan por el borde. El lagarto no (se mete debajo de las rocas). Sus
  paseos nunca eligen una meta dentro de un obstáculo y se rinden a los 7-12 s. El suelo de los que andan sale del
  generador (`ATN_BeachRaceGenerator::GetGroundHeightAt`, sin trazas y sin subirse encima del decorado); sin generador,
  traza.
- **Rodear en vez de empujar** (ronda 3, ~5000 piezas): `ATN_BeachEnemy::SteerAroundObstacles` mira por delante (el
  cuerpo + 2,5 m + lo que anda en 0,6 s, nunca más allá de la meta) y, si la sonda corta lo grande del reparto, se abre
  hacia el lado de su borde que más se parece a donde quiere ir (el mismo lado 0,8 s, para no dudar delante de una
  roca); cuanto más cerca, más de lado. Lo usa el cangrejo; lo tienen todos los enemigos que anden.
- **Nivel de detalle** (cangrejos, erizos y lagartos, `bThrottleWhenFar`): cada 0,5 s miran la tortuga más cercana y la
  cámara. Con una tortuga a su alcance (cangrejo: correa + vista + 15 m, ~75 m; erizo: correa + 22 m + 15 m, ~53 m;
  lagarto: 45 m) o la cámara a menos de 150 m, se mueven en cada fotograma; si no, cada 66 ms a la vista (hasta 300 m) o
  cada 250 ms, y la red baja a 3 Hz (8-12 Hz de cerca). `TN.Beach.Enemy.Stats` cuenta cuántos hay, cuántos van despacio
  y cuántos se apartan.
- **Temblor de cámara**: `UTN_BeachCameraShake::Kick` (golpe que se apaga) y `Rumble` (sostenido mientras se refresque),
  con la fuerza entera hasta un radio y apagándose hasta otro, para los jugadores locales.
- **Textos emergentes** (`ATN_BeachEnemy::ShowPop`): solo con la cámara a menos de 60 m.
- **Rendimiento**: sin pantalla (servidor dedicado) no hay mallas ni efectos; lejos de la cámara (300 m; 400-600 m el
  quad y las gaviotas) no se anima. Nada asigna memoria por fotograma.

### Mareo por lo que se les lanza (`ATN_BeachEnemy::ApplyHitStun`)

Lo que pidió el usuario: si lanzas objetos (piedras, el pulpo de tinta y lo demás) a un enemigo vivo, le pasa como a las
tortugas: se queda mareado un momento, con pajaritos y sin atacar.

- **Contrato** (`TN_BeachEnemy.h`): `ApplyHitStun(Segundos, Instigador)` (servidor; si ya lo estaba, alarga hasta el mayor
  de los dos finales; el mismo objeto no lo vuelve a marear en 0,6 s), `IsHitStunned()` y `GetHitStunLeft()` (cualquier
  máquina: el final va replicado en `HitStunEndTime`, reloj del servidor), `AcceptsHitStun()` (false en el paso de quads),
  `GetHitCapsule(A, B, Radio)` (el cuerpo que recibe lo lanzado; por defecto, una esfera del radio del cuerpo de los que
  andan), `GetHitStunAnchor()` y `GetHitStunScale()` (dónde y de qué tamaño van los pajaritos). Cada subclase mira
  `IsHitStunned` en su `ServerTick`; puede sobrescribir `ApplyHitStun` (llamando a la base) para soltar lo que lleve.
- **Qué le da**: `ATN_BeachEnemy::FindProjectileHit(Desde, Hasta, Radio)` busca el primer enemigo cuyo cuerpo corta el
  tramo recorrido (segmento contra cápsula). Tres ganchos:
  - `ATN_ThrowableItemActor` (piedra, bola...): en cada fotograma, en todas las máquinas, el tramo de su vuelo contra los
    cuerpos; rebota en él (hacia fuera, perdiendo fuerza: 45 % y un saltito) y, en el servidor y a 6 m/s o más (la misma
    velocidad mínima que para derribar a una tortuga), `ApplyHitStun` 3 s (`TNBeachHitStun::ThrownSeconds`). Si choca de
    verdad con el cangrejo (tiene colisión), lo mismo por `OnMeshHit`.
  - `ATN_InkProjectile` (el pulpo de tinta): en el servidor, antes de moverse, su tramo contra los cuerpos: marea 3 s y
    desaparece como al dar a cualquier cosa.
  - Bolas de caparazón: una vez por fotograma y mundo, el servidor mira las `ATN_ShellBody` que simulan a 9 m/s o más
    (una tortuga lanzada por otra, la de la patada de la tormenta...): marean 2,5 s al enemigo al que den.
- **Se ve igual en todas las máquinas**: al marearse, «¡TOING!», golpe seco y chasquido agudo y un temblor pequeño
  (multicast no fiable); mientras dura, los pajaritos y las estrellas de las tortugas (`UTN_BeachEnemyDizzyComponent`, hijo
  de `UTN_DizzyBirdsComponent`, con su sonido) a la escala del enemigo y sobre su cabeza, cada fotograma donde diga
  `GetHitStunAnchor`.
- **Cada uno**:

| Enemigo | Cuerpo que recibe | Mareado |
|---|---|---|
| Cangrejo | cápsula de costado a costado del caparazón (5 m) | se para en seco, ojos que dan vueltas, pinza caída; ni persigue ni ataca. También con la concha trampa (`ApplyStun` de la interfaz de siempre) y si se estampa embistiendo (1,6 s) |
| Erizo | esfera del cuerpo | quieto, tambaleándose; ni rueda ni pincha (se le puede pasar al lado) |
| Lagarto | del cuello a las caderas (escondido, nada) | tumbado de lado, cabeza caída y lengua fuera; si mordía, suelta a la tortuga (1 s de mareo). Luego huye (el mordedor, a su sitio) |
| Gaviota o pelícano | solo el que baja en picado (también picando en el sitio), el que lleva una tortuga en el pico y el ya mareado, con el cuerpo a menos de 18 m de la arena: cápsula del cuerpo a la cabeza | suelta a la tortuga (cae en bola), cae a la arena dando tumbos, se queda sentado con las alas caídas y los pajaritos; la zona no ataca mientras; luego despega a su círculo (1,8 s) |
| Quad | — | no se marea |

### Cangrejo gigante (`ATN_BeachGiantCrab`)

- Caparazón de 5 m de ancho (una cría de 18 cm) sobre ocho patas, ojos en pedúnculos y una pinza de ~6 m a la derecha.
  Cuatro paletas (rojo con pinza amarilla, violinista azul, violeta, fantasma de arena). `SizeScale` 0,8-1,2.
- **Cómo anda** (ronda 3): de lado, con las patas en dos grupos que se alternan (la 1 y la 3 de un lado con la 2 y la 4
  del otro, un ciclo cada 1,9 m recorridos): la que está en el aire se levanta y la apoyada se estira o se encoge según el
  cuerpo se aleja o se acerca a su pie, así no patina. El cuerpo se balancea de lado con cada paso, bota dos veces por
  ciclo y se inclina hacia donde va (más al embestir; al derrapar, hacia atrás). En el servidor la marcha tiene inercia:
  acelera a 7 m/s² y frena a 11 (por el tamaño), llega parado a los puntos de parada, el giro acelera y frena (170°/s como
  mucho, sin pasarse) y se queda con el costado que lleva salvo que el otro esté 50° más a mano. Si algo lo frena, se
  frena (no sigue empujando). Rodea lo grande del reparto mirando por delante (`SteerAroundObstacles`) y, si en 1 s se
  ha movido menos de 60 cm queriendo andar, sale 0,9 s de lado (a un lado y al otro por turnos); dos atascos seguidos
  en la patrulla, al siguiente punto.
- **Patrulla sin parar** su propio recorrido, de lado a 3 m/s (por el tamaño), empezando en un punto al azar para que
  los de un grupo no vayan a la par: entre las dos rocas, grupos de rocas, troncos, maderas, tablones, castillos
  pequeños, sacos terreros o erizos antitanque más separados en ángulo que tenga a menos de 34 m (el 35 % de las veces,
  si los hay: se para junto a cada uno
  y pasa por su sitio), un óvalo de 23 m por 11-16 m (dos paradas por vuelta) o una ida y vuelta de 23 m por una
  recta que pasa por su sitio (se para en los extremos). Las paradas duran 0,5-0,95 s, con la pinza en alto
  chasqueando deprisa. Ningún punto dentro de lo grande del reparto; si no llega a uno en 12 s, pasa al siguiente.
- **Vista y oído**: ve de frente (±70° hacia donde mira) a 22 m y oye alrededor a 10 m (por el tamaño): 13 m si la
  tortuga corre a más de 3 m/s (ronda 4; antes 6, que con las velocidades de verdad no se alcanzaba) y 6 m si va agachada,
  en bola, en panzazo o casi quieta (menos de 0,6 m/s). Tiene que estar dentro de su correa (38 m o 1,4 veces la huella
  desde su sitio). Entonces se da la vuelta (chasquido) y la persigue de lado a **3,5 m/s** por el tamaño, entre 3 y 3,7
  (`TNBeachCrabTuning::ChaseSpeedFor`, ronda 4; antes 5,6 × tamaño, más que la tortuga corriendo a 4 m/s con el
  Blueprint): a quien anda (2 m/s) la alcanza; corriendo se le escapa poco a poco (0,3-1 m/s), aunque a 8,5-13 m puede
  embestir (más rápido, pero en línea recta: se esquiva de lado). Si la pierde, vuelve a 4,2 m/s al punto más cercano de su
  recorrido y sigue patrullando. Decide con `TNCrabLogic::DecideChaseTransition`, la del cangrejo de siempre.
- **Mazazo** (alcance recortado a petición del usuario: «te dan desde un rango bastante lejos»): con la tortuga a su
  alcance (la pinza llega a 7,1 m del centro del cuerpo) más 1 m (`AttackSlack`, antes 3 m), o sea a unos 8,1 m, se para,
  levanta la pinza y tiembla 0,6 s. Durante el aviso solo puede recolocarse a 4 m/s (`WindUpSpeed`, antes 6,5), así que la
  sombra se apunta como mucho a ~9 m del cuerpo (antes 10,2). La sombra de dónde cae aparece donde estará la tortuga (0,2 s
  de adelanto, antes 0,3) y crece hasta 1,7 m de radio (`HitRadius`, antes 2,8: la mano y el dedo, no medio campo). Cae en
  0,14 s y **solo cuenta si la pinza toca de verdad**: el golpe se mide desde la punta del dedo al caer (delante del cuerpo
  a lo que alcanza, según cómo esté girado); si esa punta queda a más de 1,5 m de la sombra (no le dio tiempo a recolocarse),
  cuenta donde ha caído, no donde iba (`ResolveSlam`). Quien esté dentro (1,7 m + 0,4), a menos de 2,8 m de altura
  (`SlamHeight`, antes 4,5 m: subida a algo alto no llega) y **no la salte** queda despachurrada en bola aturdida 3,5 s con
  un empujoncito hacia fuera. **Saltarla** (ronda 4): antes se decía que saltando se libraba, pero un salto solo sube la
  cápsula 1,2 m, menos que los 2,8 de `SlamHeight`, así que no libraba nunca. Ahora la libra quien va por el aire con los
  pies a 60 cm o más de su suelo al caer la pinza (`TNBeachCrabTuning::ClearsSlamByJump`): un salto (485 cm/s) los tiene ahí
  del 0,15 al 0,85 s, y la pinza cae 0,74 s después de levantarse, así que saltar en cuanto la levanta (o a mitad del aviso)
  la libra; en el registro, `[Playa] <tortuga> salta por encima del mazazo de <cangrejo>`. También girando corriendo: la
  sombra se fija al levantar la pinza (con 0,2 s de adelanto por donde iba) y en 0,74 s corriendo hacia otro lado (3 m) se
  sale de ella; en línea recta, por los pelos o no (el adelanto la pone por delante); andando (1,5 m), no. Luego 1,1 s con la pinza clavada, 1,4 s sin poder repetir y a la golpeada la ignora 6 s. Arena,
  temblor fuerte a menos de 15 m.
- **Embestida** (a media distancia; alcance recortado): persiguiendo, con la tortuga a 8,5-13 m (por el tamaño; antes
  9-21 m: empieza donde acaba la pinza) y el camino libre de lo grande del reparto (en tramos de 2,5 m, sin salirse de su
  correa), un 55 % por segundo. Se agacha 0,55 s clavando las patas y temblando, con la pinza en guardia (chasquido grave y
  arena), y se pone de lado hacia ella. Sale disparado de lado en línea recta hacia donde estará (0,35 s de adelanto) y 4 m
  más allá (antes 7), acelerando a 32 m/s² hasta 11,5 m/s (más que la tortuga esprintando), como mucho 1,4 s (antes 1,5): a
  quien arrolla —su caja con las patas más 45 cm, el radio de la tortuga, antes 0,9 y 1,1 m de holgura, yendo a más de
  4,5 m/s— la derriba lanzada (tabla de arriba). Luego derrapa 0,8 s (frena a 20 m/s²) con las patas del lado hacia el que
  va clavadas: arena que salta por delante y surcos en la arena (dos cada 0,07 s, 16 a la vez, 5 s, se desvanecen). Si se
  estampa contra algo grande u otro enemigo, «¡CATAPLÁN!» y mareado 1,6 s. No vuelve a embestir en 5 s.
- Lo que se le lanza o la concha trampa lo marea (ver «Mareo por lo que se les lanza»); la tinta lo ciega (vuelve a su
  recorrido) (`ITN_EnemyTargetInterface`).

### Erizo de mar (`ATN_BeachSeaUrchin`)

- Bola violeta, negra, roja u oliva de 72 púas: rueda sobre un radio de 1,26 m (3 m de diámetro). `SizeScale` 0,75-1,35.
- Nota las vibraciones alrededor a 22 m (por la raíz del tamaño) y rueda girando hacia la tortuga más cercana a
  2,1 m/s (lento: solo pilla a quien se despista), sin salirse de 16 m de su sitio (o 1,35 veces la huella). Sin nadie,
  pasea casi sin parar (respiros de 0,6-1,6 s) a 1,2 m/s por el 80 % de su huella, rodeando lo grande del reparto.
- Tocarlo (a ~1,65 m del centro) pincha en cualquier estado salvo mareado: derribo con ragdoll y mareo (tabla de
  arriba); el erizo retrocede 0,8 s y la ignora 4,5 s. Mareado por algo lanzado, se tambalea en el sitio sin rodar ni
  pinchar.

### Lagarto (`ATN_BeachLizard`)

- Vida del ambiente: lagarto de 15 m (55 cm reales) sobre el cuadrúpedo de la fauna: iguana verde con cresta,
  turquesa de cabeza amarilla, ocelado con manchas azules o naranja de collar. Toma el sol 4-8 s (flexiones cada ~7 s,
  cabeceos, lengua y cola que se mece) y se va andando a 3,8 m/s a otro rincón de su zona (hasta el 60 % de su huella,
  nunca dentro de una roca); así siempre se mueve.
- A 30 m se pone alerta y mira a la tortuga (también andando). A 17 m: el 70 % de las veces da un susto (amago de
  0,35 s hacia ella, se hincha, tiembla, saca la lengua y bufa; a quien esté a menos de 8 m de la cabeza lo empuja
  6,5 m/s, sin aturdir) y luego huye; si no, huye sin más. A 9 m huye directamente.
- Huye a 15 m/s a la roca, grupo de rocas, tronco, madera, tablones, restos de vela, castillo pequeño, sacos terreros,
  caja de munición o red de camuflaje más cercano (hasta 45 m, nunca hacia la tortuga) y se mete debajo (anda sobre la arena del generador: no se sube encima); si no hay, da
  un arreón y se entierra sacudiéndose (1,3 s). Escondido 7-12 s; no sale con una tortuga a menos de 18 m. Sale y
  vuelve andando a su sitio.
- **Carácter** (ronda 3, `ETNBeachLizardTemper`, sale de la semilla: `ATN_BeachLizard::TemperOfSeed`, igual en todas las
  máquinas sin replicar nada): 45 % huidizo, 30 % generoso y 25 % mordedor. Se distinguen a la vista:
  - **Huidizo**: el de siempre (lo de arriba).
  - **Generoso**: motas doradas a lo largo del lomo y un collar dorado; de vez en cuando destella (chispitas doradas).
    Huye sin asustar a nadie y, la primera vez, deja un premio donde estaba («¡UN REGALO!», estallido dorado): el 60 %,
    un objeto del catálogo con los pesos de la carrera (como el botín suelto); si no, una concha de puntos de 25 (de 50,
    el 30 %). Si nadie lo coge, se va con el lagarto (la ronda siguiente reparte otro).
  - **Mordedor**: cresta roja de púas (la de la iguana) y la punta de la cola roja. No huye: con una tortuga de pie a
    menos de 15 m (por el tamaño) se lanza a por ella (13 m/s, bufido grave, polvo; 1,3 s como mucho); si la punta del
    hocico llega a 2,6 m de ella, la muerde por el caparazón («¡ÑAM!»): la tortuga queda atravesada en su boca,
    pataleando, y la zarandea 1,3 s de lado a lado (±38°, 3,2 veces por segundo, cabeza alta y la cola latigueando al
    revés); luego la lanza de lado en bola mareada 1,5 s y vuelve a su sitio. A la mordida la deja 10 s; si falla, bufa,
    vuelve a su sitio y a esa la deja 4 s. En bola, en brazos de otra o ya en el suelo no se la puede morder: la vigila.
  - **Red del mordisco**: el servidor decide y replica a quién tiene en la boca (`HeldVictim`); cada máquina la sujeta
    (`ATN_BeachEnemy::BeginHoldTurtle`: movimiento apagado, sin suavizado ni correcciones, pataleta) y la coloca en la
    punta del hocico con las mismas cuentas (`MouthAt`, `ShakeHeadRotation` con el reloj del servidor), después de su
    movimiento. Mientras muerde no se hunde ni culebrea (la boca va donde la cuentan todas las máquinas).
- **Consola**: `TN.Beach.Lizard <huidizo|generoso|mordedor>` crea uno de ese carácter 22 m delante de tu tortuga,
  mirándote (busca una semilla con ese carácter: `FindSeedForTemper`).

### Paso de quads (`ATN_BeachQuadLane`)

- Eje X local del actor (el generador lo gira 90° y lo cruza de lado a lado), `Extent` de largo (0 = 280 m). En la arena,
  dos rodadas que avisan por dónde pasa, pegadas a la arena (#259): con la ronda montada, la altura de la malla del terreno
  (`TraceTerrainAt`, o la del generador) cada 1,25 m a lo largo y en 5 puntos a lo ancho; no se trazan contra el mundo
  (los muros invisibles de los lados y el decorado las subían).
- Quad a escala con piloto: 56 m de largo, ruedas de 16,8 m de alto y 6,7 m de ancho, centros a ±8,7 m (las ruedas
  llegan a ±12 m: la huella), hueco de 10,6 m entre ruedas y 7,3 m de altura libre bajo el chasis. `SizeScale` 0,7-1,4
  escala todo.
- Primera pasada a los 5-14 s; después, cada 12-20 s. Aviso de 3,5 s: temblor creciente (0,12 → 0,57) a menos de 15 m
  del paso (se nota hasta 90 m), motor que se acerca y humo y hojas entre las palmeras del lado de salida. Cruza a
  42 m/s (unos 9 s de palmera a palmera, sale de 15 m dentro de la selva), revienta las palmeras al salir y al entrar.
- Atropello: una rueda que pasa por encima (±3,8 m a lo ancho, ±5 m a lo largo) derriba con ragdoll y lanza (tabla de
  arriba; lanzamiento moderado para que el ragdoll no atraviese la arena al caer); 1,2 s sin repetir con la misma.
  Temblor 0,85 a menos de 25 m del quad.
- **Ruedas cerradas** (ronda 4; `TNBeachMeshes::BuildQuadWheel` en `TN_BeachEnemyMeshes.h`): la cara interior de las ruedas
  (la que se ve desde el hueco entre ruedas, y también la exterior) se veía rota desde fuera. Causa: la banda de rodadura
  y el flanco de cada lado (un cono de R a 0,9 R) se generan sin tapas, y entre el borde interior del flanco (0,9 R) y la
  llanta (0,62 R) no había ninguna cara: un anillo abierto por el que, con el material de una cara del color de vértice
  (las caras de atrás no se pintan), se veía a través de la rueda y el interior del neumático por dentro, sin pintar. Los
  tacos eran vigas cuadradas (`AddBeam` no lleva tapas) abiertas en la punta, con el mismo defecto. Arreglo en la malla,
  sin poner el material a dos caras: un hombro plano por lado (corona de 0,5 R a 0,9 R en el plano del flanco, con los
  mismos 18 lados que el cono, así que los vértices coinciden y no hay grietas; llega por debajo de la llanta de 12 lados
  para no dejar rendijas) y una tapa en la punta de cada taco. El sentido de los triángulos sale de `AddTri`, que orienta
  cada cara hacia el lado que se le pide, así que las normales quedan coherentes hacia fuera. El tanque de juguete
  (`TN_BeachToyTank`) no comparte la función: usa `BuildTankWheel` (`TN_BeachCritterMeshes.h`), cilindros con tapas y cajas,
  que ya estaba cerrada y no se ha tocado.

### Gaviotas y pelícanos (`ATN_BeachGullZone`)

- 3-4 gaviotas (25 m de envergadura) y, el 60 % de las veces, un pelícano (40 m), **cada una en su círculo y a su
  altura**: óvalo con el centro desplazado del de la zona (12-60 % del radio de la zona, 35 m o más, repartidos con el
  ángulo áureo y derivando ±7 m), radio del 45-90 % (18 m como poco), achatado 0,65-1 y girado al azar, a 9-13 m/s (el
  pelícano, 7-9); el 30 % gira al revés. Alturas en capas de 8 m barajadas (32, 40, 48, 56 y 64 m, ±2 m): nunca dos a
  la misma. Graznan de vez en cuando abriendo el pico.
- **Sombras de los que vuelan** (ronda 3): la de verdad, bajo el cuerpo de cada pájaro (no bajo su raíz, que quedaba
  desplazada); cuanto más bajo va, más pequeña, más nítida (tres mallas de borde más o menos difuminado) y más oscura
  (opacidad del material, de 0,12 en lo alto a 0,5 a ras de arena).
- **Aviso duro en la arena** (tras probarlo en red: la sombra del picado no se veía): un disco negro de borde neto
  (`DiveMarker` y `DropShadow`: `ShadowDiscEdge(0,92)`, opacidad 0,88 del material, por encima de las demás sombras,
  tumbado sobre la cuesta de la arena y 25 cm levantado para que no se hunda en ella). Aparece en 0,35 s pequeño y crece a
  medida que el pájaro (o la cagada) baja, hasta lo que coge (2,4 m por el tamaño; antes 3,3) o la mancha (2 m; antes
  2,8): así se lee que viene a por ti. Revisado: con `M_ProcFXSoft`, su fundido por profundidad (80 cm) recortaba el disco, a 25 cm de la arena, a un
  35-70 % de su opacidad según el ángulo de la cámara; los dos discos (y el signo de la cagada) usan ahora `M_ProcFXHard`
  (`Scripts/create_poop_decal.py`), el mismo material sin ese fundido (0,88 entero); sin el asset, el suave de siempre. La
  sombra de la cagada nace con 90 cm de radio (antes 35) y crece antes (45 % lineal + 55 % acelerado, antes 30/70) para que
  se lea desde que se suelta; la del picado no cambia.
- **El blanco te sigue y, al final, va lanzado** (`ServerTrackAim` con `TNBeachGullTuning::StepAim`; ronda 4, ver «Nerf de
  la gaviota y de su caca»): el punto al que van el picado y la cagada (`FTNBeachGullAttack::Aim`, por la arena) va hacia
  la tortuga a 4,2 m/s como mucho (antes 6,25), algo más de lo que corre (4 m/s con el Blueprint); los últimos 1,5 s (el
  picado pliega las alas del todo; el «!» de la cagada se queda fijo) va lanzado por la línea que llevaba la tortuga: por
  ella la acompaña y hacia los lados corrige a 0,75 m/s. Andando o corriendo en línea recta te pilla; girando corriendo
  (60° o más) o dándote la vuelta en ese momento, o tirándote en plancha a tiempo, te libras. El servidor lo mueve y lo
  replica (10 Hz); cada cliente lo suaviza (`ShownAim`, sin saltos) y con él coloca el pájaro, la cagada y la sombra.
- Ataca cada 4-7 s (antes 3-6) a una tortuga al azar de las que están a menos del 80 % de su huella del centro (antes, su
  huella + 8 m; atacable y sin sombrilla); va el pájaro más cercano. La mitad de las veces caga una gaviota; si no,
  picado (el pelícano solo pica).
- **Cagada**: 1,5 s volando hasta encima (siguiendo al blanco); la suelta desde 30 m y cae acelerando en 2,1 s (tiempo
  para verla venir y apartarse corriendo), también siguiendo al blanco: un pegote de 1,6 m con su estela de gotitas, un
  silbido y la sombra dura que crece hasta la mancha. Sobre la tortuga a la que va, un signo de exclamación que parpadea
  cada vez más rápido y se queda fijo cuando la cagada ya cae por su línea (ver «Cagada pintada y aviso»). Al caer, la traza
  desde arriba da en el techo si lo hay (`ATN_BeachEnemy::TraceDropSurface`: canal de visibilidad, también fortalezas y
  castillos); la sombra y la cagada que cae van a ese mismo sitio en cada máquina, no a la arena de debajo (#255; igual en
  la gaviota justiciera). Quien esté
  dentro (2 m por el tamaño + 0,35, antes 2,8 + 0,45, y a menos de 3 m en altura: a cubierto la mancha cae encima) y no vaya
  tirada en plancha en ese momento (`TNBeach::IsDodgingByBellyDive`: en el aire o arrastrándose a 2,5 m/s o más) cae
  derribada (tabla de arriba) con la cagada PINTADA en el caparazón (12 s, ver abajo); gotas, «¡PLOF!» y la mancha en la
  arena 12 s.
- **Picado**: 1 s colocándose casi encima (a 18 m del blanco y 46 m de altura). Luego baja en picado 2,3 s
  (`DiveTime`: desde que aparece la sombra hay 2,3 s para reaccionar), acelerando con las alas medio recogidas y siguiendo
  a la tortuga por el aire con el blanco (los dos extremos de su bajada se mueven con él); los últimos 1,5 s pliega las
  alas del todo y va lanzado por la línea que ella llevaba: es el momento de girar. A 0,45 s de llegar abre el pico,
  abre las alas y adelanta las patas para frenar con el morro levantado; en ese momento, si la cubre algo (sombrilla,
  techo), fallará y picará encima. A los 3,3 s coge a la tortuga que esté bajo el pico (2,2 m por el tamaño + 0,25 del
  blanco; antes 3 + 0,45) si está de pie: ni en pleno panzazo (`IsBellyPoseActive`), ni en bola, ni en brazos de otra, ni
  a cubierto.
- **Si falla** (se aparta, panzazo, bola, a cubierto): el picado se ve entero igual. Baja en 0,14 s hasta clavar el
  pico en la arena donde iba (o en la sombrilla que la cubría), con el morro y la cabeza hacia abajo; pica dos veces
  hasta los 0,55 s («¡PIC!», arena y granos que saltan, golpe y chasquido, temblor pequeño) y remonta de largo hacia su
  círculo (`FTNBeachGullAttack::Hold` guarda dónde pica).
- **Agarre**: el pico se cierra (con chasquido, plumas, arena y «¡ÑAC!») en la espalda de su caparazón (el punto al 72 %
  del pico; en el pelícano, al 62 %, dentro de la bolsa). La tortuga queda colgando pataleando (pose de pataleta de
  `UTN_TurtleAnimInstance`, sin meterse en bola): 0,35 s de tirón, sube 26 m aleteando fuerte hasta los 2,2 s, vuela
  meciéndola y sacudiendo la cabeza y a los 3,3 s la suelta abriendo el pico, 15 m más hacia la salida: cae en bola
  aturdida (empujada 3,5 m/s hacia la salida). Si se mete en el caparazón mientras cuelga, se escurre y cae en bola
  aturdida igual que al acabar el vuelo: la gaviota la suelta antes de que nazca la bola (`OnHeldTurtleSlips`, ronda 4:
  ver «Segunda gaviota + caparazón = torbellino»). Si la suelta detrás del frente de la tormenta, la patada la saca. Si a
  la que la lleva le dan con algo, la suelta.
- **Red del agarre**: cada máquina coloca a la tortuga con el mismo camino (`FTNBeachGullAttack::Hold` y el reloj del
  servidor) con la sujeción de `ATN_BeachEnemy` (`BeginHoldTurtle`/`PlaceHeldTurtle`/`EndHoldTurtle`, la misma del
  lagarto mordedor y del pulpo): movimiento apagado (`MOVE_None`); con malla, por su hueso de la espalda (`Spine2`, 35 cm
  por delante del pico), y en un servidor dedicado, por la cápsula. El pájaro se coloca para que su pico quede justo ahí
  (su cuerpo, cabeza y pico: `TNBeachMeshes::BirdGeom`). Mientras cuelga, el servidor no corrige al dueño
  (`bIgnoreClientMovementErrorChecksAndCorrection`) y los demás clientes la ven sin suavizado de red. Al soltarla, el
  servidor apunta la hora (`FTNBeachGullAttack::ReleaseTime`, replicada: cada cliente la suelta en cuanto le llega, vaya
  como vaya su reloj) y la mete en bola.
- **Soltar a prueba de todo** (bug de la prueba en red: «me soltó y me quedé arriba dando vueltas, sin caer»). Causas
  que había en el código:
  - Al soltarla, solo se le devolvía la caída (`MOVE_Falling`) si en esa máquina no estaba ya en su caparazón, ni
    derribada, y con la carrera en marcha: se confiaba en que la bola del mareo la moviera. Si la bola no llegaba a
    engancharse en esa máquina (la caja llega por red antes o después que la referencia, o la ronda se cerraba), la
    tortuga se quedaba con el movimiento apagado donde la dejó el pico, en el aire, con los pajaritos y el temblor del
    mareo dando vueltas.
  - Los clientes decidían cuándo soltar solo con su reloj de servidor estimado: con el reloj por detrás, el dueño la
    seguía clavando en el pico unos fotogramas mientras su caja de la bola ya caía (la caja choca con la cápsula clavada
    y gira alrededor de ella).
  - La marca de «llevada» (`SetTurtleHeld`) se comparte entre gaviotas, lagartos, pulpos y gusanos: si otro sistema la
    quitaba, la tormenta o un enemigo podían actuar sobre una tortuga aún sujeta.
  - El suavizado de red de los demás clientes se quedaba apagado para siempre si la bola guardaba el valor que la
    sujeción ya había apagado.
- **El seguro** (`ATN_BeachEnemy::EndHoldTurtle` y `RestoreReleasedTurtle`, en todas las máquinas): al soltar y durante
  3 s, en cada fotograma, si nadie más la sujeta (otro enemigo, un gusano) y no ha llegado a la meta, el servidor le
  devuelve las correcciones al dueño sí o sí y se quita la pataleta; y si nada más la mueve (ni la caja de la bola
  enganchada en esa máquina, ni el ragdoll del derribo, ni otra tortuga que la lleve), se le enciende el movimiento y
  cae por su cuenta (`MOVE_Falling`), también en bola sin caja o con la ronda parada. Al acabar, el suavizado de red de
  los demás clientes vuelve al que tenía. Mientras la sujeta, la marca de llevada se vuelve a poner si otro la quita.
  Seguro de tiempo: una sujeción de más de 6 s se suelta sola (con aviso en el registro) y esa tortuga no se puede volver
  a sujetar en 2 s. Si al dueño se le quedara algo a medias, el servidor ya le corrige (correcciones devueltas) y la
  corrección le trae el modo de movimiento del servidor.

### Cagada pintada y aviso (`ATN_BeachGullZone`, `Scripts/create_poop_decal.py`)

Lo que pidió el usuario: la cagada se preveía mal y quedaba como «un objeto blanco encima, como un sombrero, no adaptado a
la tortuga». Era una malla de unos 44 cm enganchada a 50 cm por detrás del hueso de la espalda, flotando detrás del
caparazón (que está a ~24 cm: el tronco mide 42 cm de tripa a lomo con el hueso a 7 cm de la tripa). Debe ser una
**pintura sobre la propia tortuga**, y hace falta un aviso sobre la tortuga objetivo para poder esquivarla.

- **Decal**: al caer, en todas las máquinas y por cada tortuga manchada (`MulticastSplat`, ahora fiable para que no se
  pierda), `SpawnStain` crea un `UDecalComponent` con `M_PoopSplatDecal` (dominio Deferred Decal, mezcla Translucent: pinta
  color y brillo sin tocar la normal) sujeto al hueso `Spine2` de la malla de la tortuga. Va con el ragdoll y con la bola
  (la malla tumbada sobre la tripa lleva el hueso). Caja de proyección de 76 x 76 cm de cara y 60 cm de fondo, con el centro
  a 21 cm por detrás del hueso y 10 cm por encima (`StainBackCm`, `StainUpCm`, en la postura de referencia, como
  `AttachToTurtleBack`), mirando hacia delante y abajo (0; 0,85; -0,53): entra como cae, por detrás y desde arriba. Pinta el
  caparazón y salpica hombros y nuca; no llega a la arena (a ~1 m). Sin hueso o sin el material, plan B: el pegote de
  siempre (`BuildShellSplat`) a 0,6 de escala y pegado (24 cm por detrás, 6 por encima).
- **Material** (`M_PoopSplatDecal`, hecho por el script con un nodo Custom, sin texturas): mancha irregular con brazos de
  salpicadura (armónicos por ángulo y ruido), 14 gotas alargadas hacia fuera, corazón más oscuro, motas y veteado; blanco roto
  con un toque verdoso (lineal 0,80 / 0,86 / 0,68), brillante mientras está fresca. Parámetros: `Seed` (1-98, la forma; sale
  del número de serie del ataque y de cuál de las manchadas es, así que es la misma en todas las máquinas) y `Fade`.
- **Se va quitando**: 12 s en total (`StainLife`): entera hasta los 8 s y luego `Fade` baja de 1 a 0 en 4 s; el material la
  seca desde los bordes y lo fino hacia el centro y acaba desapareciendo, y a los 12 s se destruye el decal. También se
  quita si su tortuga desaparece; como mucho 12 a la vez por zona.
- **Aviso** (`WarnMark`, `TNBeachMeshes::BuildWarningMark`): un signo de exclamación amarillo con borde rojo oscuro (105 cm de
  alto), a 40 cm sobre la cabeza de la tortuga a la que va, de cara a la cámara de cada máquina, que empieza 0,5 s antes de
  soltar la cagada y parpadea cada vez más deprisa según cae (`Rate = lerp(2, 12, U²)`, encendido el 60 % de cada
  parpadeo) hasta que, los últimos 1,5 s (ronda 4), se queda **fijo**: la cagada ya cae por la línea que llevaba la tortuga y es
  el momento de girar. Lejos se agranda (distancia / 15 m, entre 1 y 3,5 veces). Lo ven todas las máquinas con pantalla
  (la del jugador al que va y las demás); no hace ruido.
- **Assets**: `Scripts/create_poop_decal.py`, dentro del editor, crea `M_PoopSplatDecal` y `M_ProcFXHard` en
  `/Game/ProcMap/Materials` (se puede volver a ejecutar para rehacerlos). Se cargan por ruta al usarlos; si faltan, plan B
  del pegote y avisos con el material suave.

### Nerf de la gaviota y de su caca (ronda 4, tarea 5)

Lo que pidió el usuario: casi no se podían esquivar; tiene que poderse corriendo, cambiando de dirección y tirándose en
plancha en el momento justo. Criterio: **andando te pilla; corriendo y cambiando de dirección en el momento justo, o
tirándote en plancha a tiempo, te libras**, del picado, de la cagada de la zona y de la gaviota justiciera. Todas las cifras,
con nombre, en `Public/World/Beach/TN_BeachGullTuning.h` (lógica pura: las usan `ATN_BeachGullZone` y `ATN_RaceGullStrike`, y
las pruebas `Tortunabo.Beach.Gull.*`); la caca del cooperativo (`ATN_SeagullDroppingActor`) tiene las suyas como `UPROPERTY`.

**Velocidades de verdad**: las de la tortuga que se juegan son las del Blueprint, **2 m/s andando y 4 m/s corriendo** (no los
4,5 y 8 del C++; `Docs/Biblia_Tortunavy.md` §13: 141 de 190 saltos de los registros del 28-09 salen a 400 cm/s exactos). La
plancha es un segundo salto en el aire: sale a 350 cm/s más la velocidad del salto (750 corriendo hacia delante) y 100 hacia
abajo; en el aire 0,3-0,4 s y luego se arrastra por la arena (de ~675 cm/s a menos de 250 en 0,29 s).

**Por qué no se podía**: el blanco seguía a la tortuga a 6,25 m/s hasta el mismo golpe (la justiciera, a 7): más de lo que
corre (4 m/s), así que ni corriendo ni cambiando de dirección se despegaba; el golpe alcanzaba 3,45 m (picado) y 3,25 m
(cagada); la plancha no libraba de la cagada. Y atacaban desde la huella de la zona + 8 m (32-47 m de radio) cada 3-6 s.

**Recalculado en #636** (GDD, ronda 4: «el seguimiento es más lento y la caca se esquiva con una plancha a tiempo»): con 4,2 m/s
(lo que se describe abajo) nadie se despegaba corriendo en línea recta. Ahora el blanco persigue a **2,5 m/s**
(`TNBeachGullTuning::GullChaseSpeed`, entre andar y correr) en los tres ataques, con el mismo tramo final de 1,5 s. Andando
(2 m/s) no se despega y le da; esprintando en línea recta (4 m/s) se le gana 1,5 m/s y al golpe queda a 3,8 m (picado), 4,3 m
(cagada) y 6,2 m (justiciera), fuera del alcance del pájaro más grande. Girar corriendo al lanzarse sigue librando; darse la vuelta
esprintando es cruzar la sombra (2,8 / 2,45 / 1,1 m). La ventana de la plancha está ahora en el código
(`BellyDiveDodgeWindow`): 0,59-0,69 s desde que despega corriendo y 0,48-0,58 s andando. Velocidades comprobadas en
`BP_TortugaCharacter` el 2026-10-04 (andar 200, correr 400, plancha 350, rozamiento en arena 800 y freno 1,5). Las tablas de abajo
son de la versión con 4,2 m/s; las cifras vigentes las comprueba `Tortunabo.Beach.Gull.*`.

**Cómo es ahora** (`TNBeachGullTuning::StepAim`): el blanco persigue a la tortuga a 4,2 m/s (un poco más de lo que corre: en
línea recta no se despega de nadie) y, los **últimos 1,5 s**, va **lanzado por la línea que llevaba la tortuga** en ese momento:
por esa línea la acompaña (lo que ella avance por ella, hasta su velocidad de entonces y nunca hacia atrás) y hacia los lados
solo corrige 0,75 m/s. Quien sigue recto (andando o corriendo) se lo come; quien gira corriendo 60° o más, se da la vuelta o
sale corriendo de parada justo al lanzarse, se libra; andando no da tiempo a salir del golpe. Cómo se ve cuándo: el picado
**pliega las alas del todo** (antes baja con ellas medio abiertas), el «!» de la cagada **deja de parpadear y se queda fijo**, y
la justiciera se lanza justo al soltar la cagada.

| Qué | Antes | Ahora |
|---|---|---|
| Radio de ataque de la zona | huella + 8 m (32-47 m) | 80 % de la huella (19-31 m) |
| Tiempo entre ataques | 3-6 s | 4-7 s |
| Picado: el blanco sigue a | 6,25 m/s hasta el golpe | 4,2 m/s; los últimos 1,5 s, lanzado por su línea y 0,75 m/s de lado |
| Picado: coge a menos de | 3 m × tamaño + 0,45 | 2,2 m × tamaño + 0,25 |
| Cagada: el blanco (1,5 s volando encima + 2,1 s cayendo) | 6,25 m/s hasta el golpe | 4,2 m/s; los últimos 1,5 s (el «!» fijo), lanzado por su línea y 0,75 m/s de lado |
| Cagada: mancha (y su sombra) y golpe | 2,8 m × tamaño + 0,45 | 2 m × tamaño + 0,35 |
| Cagada: plancha | no libraba | libra si va en plancha en el aire o arrastrándose a ≥ 2,5 m/s |
| Justiciera: el blanco (3,2 s llegando + 1,7 s cayendo) | 7 m/s hasta el golpe | 4,2 m/s; desde justo después de soltarla (1,5 s antes del golpe), lanzado por su línea y 0,75 m/s de lado |
| Justiciera: radio del impacto | 3,3 m | 2,4 m (su sombra y su mancha, igual) |
| Justiciera: plancha | no libraba | libra (igual que la cagada) |
| Caca del cooperativo: radio del impacto (`ImpactRadius`) | 100 cm | 75 cm |
| Caca del cooperativo: plancha (`bBellyDiveDodges`) | no libraba | libra |

Lo que queda igual: los tiempos (1 s colocándose + 2,3 s de picado; 1,5 s + 2,1 s de cagada; 3,2 s + 1,7 s la justiciera),
la sombra dura que crece hasta lo que alcanza, el panzazo que ya libraba del picado entero, la bola y la sombrilla. (El
primer ajuste de esta ronda, con 5,4 m/s, 0,6 s de tramo final y las velocidades del C++, no valía con las de verdad.)

**Resultado** (simulado a 60 pasos por segundo con la misma función que el servidor; distancia entre el blanco y la tortuga
al llegar el golpe; el golpe alcanza 1,9 / 2,45 / 3,1 m el picado y 1,85 / 2,35 / 2,95 m la cagada con tamaño 0,75 / 1 / 1,3, y
2,4 m la justiciera):

| Qué hace la tortuga | Picado | Cagada | Justiciera |
|---|---|---|---|
| Quieta, andando o corriendo en línea recta | 0 m: la coge | 0 m: le da | 0 m: le da |
| Corriendo y, al lanzarse, gira de lado (90°) | 4,7 m: se libra | 4,7 m: se libra | 4,7 m: se libra |
| Corriendo y, al lanzarse, gira 60° | 3,9 m: se libra | 3,9 m: se libra | 3,9 m: se libra |
| Corriendo y, al lanzarse, gira 45° | 3 m: se libra salvo la más grande | 3 m: se libra | 3 m: se libra |
| Corriendo y, al lanzarse, se da la vuelta | 4,7 m: se libra | 4,7 m: se libra | 4,7 m: se libra |
| Quieta y, al lanzarse, echa a correr | 4,7 m: se libra | 4,7 m: se libra | 4,7 m: se libra |
| Corriendo y gira a mitad del tramo final (0,75 s después) | 2,4 m: se libra en las pequeñas | 2,4 m: se libra salvo la más grande | 2,4 m: la coge |
| Corriendo y gira 0,2 s antes de que se lance | 0 m: la coge | 0 m: le da | 0 m: le da |
| Corriendo y gira demasiado tarde (0,4 s antes del golpe) | 1,25 m: la coge | 1,25 m: le da | 1,25 m: le da |
| Corriendo y se para al lanzarse | 0 m: la coge | 0 m: le da | 0 m: le da |
| Andando y, al lanzarse, gira de lado o se da la vuelta | 1,8 m: la coge | 1,8 m: le da | 1,8 m: le da |

**La plancha**: si pulsa el segundo salto 0,2-0,5 s después de saltar, va 0,3-0,4 s por el aire y se arrastra deprisa 0,3 s
más: **0,6-0,7 s en los que las cagadas le pasan por encima** (`TNBeach::IsDodgingByBellyDive`), y del picado libra todo el
panzazo (~1,2 s hasta ponerse de pie). Además se lleva a la tortuga 4-6 m a 7,5 m/s, más de lo que el blanco lanzado la sigue.
En el registro: `[Playa] <tortuga> esquiva la cagada de <zona> en plancha.` y `[Carrera] <tortuga> esquiva la gaviota
justiciera en plancha.`

**Nota**: `BP_SeagullDropping` (cooperativo) no cambia `ImpactRadius` (comprobado en el `.uasset`): vale el nuevo de serie.

**Qué probar**: `TN.Beach.Gull.Attack 2` quieta, andando y corriendo recto (te coge); corriendo y girando de golpe cuando la
gaviota pliega las alas (se libra y pica la arena); tirándote en plancha antes de que llegue (se libra). `TN.Beach.Gull.Attack
1`: andando o corriendo recto (le da); girando corriendo cuando el «!» se queda fijo, o en plancha justo antes de caer (se
libra, con el aviso en el registro). `TN.Race.ItemUse GullStrike 1` con dos jugadores, lo mismo con la justiciera (girar al
soltarla). Y una ronda normal: las gaviotas atacan solo bien dentro de su zona y con algo más de pausa.

### Tormenta de bañistas (`ATN_BeachStorm`)

- **Interfaz**: el GameMode la crea 30 m detrás de la salida, en el centro de la playa y mirando al mar, y llama por
  nombre a `StartStorm()` y `StopStorm()` (UFUNCTION sin parámetros). Desde C++: `StartStormAt(Desplazamiento,
  Velocidad, Gracia)`, `GetFrontDistance()` (distancia del frente al actor), `GetFrontSpeed()`, `GetFrontLocation()`,
  `IsLocationInside(Punto)`, `IsStormActive()` y `ATN_BeachStorm::FindStorm(Contexto)`. Parada, se queda quieta y a la
  vista (recuento); al destruirla desaparece.
- **A ras de arena** (antes salía «arriba del todo»): el suelo del frente se buscaba con trazas contra lo estático desde
  60 m por encima del último suelo encontrado; el muro invisible de detrás de la salida (de -200 a +1200 m de alto)
  devolvía el punto de partida y la cota subía 60 m en cada traza, así que velo, trastos y bañistas acababan a cientos
  de metros. Ahora la arena sale de `ATN_BeachRaceGenerator::GetGroundHeightAt` (sin trazas) para el frente, cada trasto
  y cada bañista; sin generador (otro mapa), traza por el canal de visibilidad, que los muros invisibles no bloquean.
- **Marcha justa** (más lenta desde la ronda 3, lo pidió el usuario): 10 s de gracia (`DefaultGrace`; 15 con 1200 m), de
  0 a 1,8 m/s en 6 s (`StartAccel` 30 cm/s²) y 1,8 m/s (`DefaultSpeed`: la tortuga anda a 4,5 y la media de la carrera
  es ~4 m/s, así que quien avanza con normalidad le saca ventaja clara). Solo acelera (a 15 cm/s², `SpeedChangeAccel`)
  al final: pasados 160 s de marcha (`LateStartSeconds`; 240 con 1200 m), +0,45 m/s por minuto (0,3) hasta 3 m/s
  (`SpeedRampPerMinute`, `MaxSpeed`); con la primera tortuga pasado el 80 % del recorrido, al menos 2,5 m/s
  (`EndRushProgress`, `EndRushSpeed`); y si la última le saca más de 120 m (180), a 2,8 m/s hasta quedarse a 80 m (120)
  (`CatchUpGap`, `CatchUpSpeed`, `CatchUpRelease`), para que siempre se note. Con el recorrido a 2/3, los tiempos y las
  distancias también (la ronda dura unos 200 s, no 300). Cada cambio empieza un tramo nuevo replicado desde donde está (desplazamiento, velocidad, aceleración con
  signo, velocidad a la que va, hora).
- **Aviso**: con el frente a menos de 25 m por detrás (`WarnDistance`), temblor creciente, viento, arena alrededor de la
  cámara y «¡QUE VIENE LA TORMENTA!» (una vez por acercamiento); al entrar, «¡CORRE!».
- **Patada** (ronda 3; sustituye al revolcón; rehecha para que no pueda entrar en bucle: ver «El bucle "torbellino" en la
  tormenta» en «Seguridad: nunca bajo el mapa»): nadie se puede quedar detrás del frente. A la tortuga que lleva 0,25 s
  (`KickDelay`) más de 1 m (`KickSlack`) por detrás del frente y que se mueve sola (de pie o en su bola porque quiere),
  un bañista le da una patada que la lleva a un sitio de arena abierta **resuelto antes** en el servidor, `KickAhead`
  (20 m) por delante del frente contando lo que avanza mientras vuela: en bola por el aire si el arco está libre (vuelo de
  1 s + 1 s por cada 28 m, entre 1,1 y 2,4 s, `KickMinFlight`/`KickMaxFlight`, o más alto, 2,6 o 3,2 s, para salvar lo de
  delante; mareada lo que vuela + 0,8 s, `KickStunExtra`) o, si no lo está, en bola igual, atravesando lo que haya hasta
  bajar sobre su sitio (nunca un salto sin vuelo). Si la bola no llega (atascada, hundida, en el agua, lejos o detrás del
  frente), otra patada desde donde está; solo tras dos se la pone en su sitio. Después, 3 s sin
  patadas (`KickGraceSeconds`). La que mueve otra cosa (el pico de una gaviota, la boca de un lagarto, un gusano, los
  brazos de otra —patean a la que la lleva—, el derribo, la bola de aturdida, un lanzamiento, la red de seguridad, una
  concha) espera a que la suelten (8 s como mucho con lo que acaba solo); con la ronda parada, nada. Se ve la pierna del
  bañista barriendo por detrás de la tortuga (0,25 s), arriba 0,2 s y fundiéndose (0,45 s), con pisotón, golpe, polvo,
  temblor y «¡PATADA!» (multicast no fiable; la bola va por la física replicada del caparazón, y si atraviesa, con ese
  estado replicado en su caja). Dentro (6 m por detrás del frente, `InsideMargin`): niebla y tinte de arena, viñeta, tos y
  viento.
- **Sonido** (a petición del usuario: «hay un sonido de los objetos de los bañistas rebotando todo el rato, pum, pum, pum; es
  súper molesto, quítalo. Que la tormenta tenga solo el ruido, igual que en el cooperativo»): el «pum, pum, pum» eran los
  pisotones de los bañistas (`ETNBeachSfx::Stomp` en cada paso, el del bañista más cercano, con una sacudida de cámara); los
  trastos que vuelan no tienen sonido. Ya no suena el pisotón por paso, ni el «pum» de aviso al acercarse el frente, ni el
  viento propio de la tormenta (`SetWind`, que sonaba a agua): el ruido es el del cooperativo, el paisaje sonoro
  (`UTN_AmbientSoundscapeComponent`: viento con ráfagas, silbido y truenos cada 7-20 s), que ahora también atiende a la
  tormenta de la playa. Sin generador busca la `ATN_BeachStorm` (cada 2 s mientras falte) y, con la cámara a más de 4 m por
  detrás del frente, sube el viento ×2,1, las ráfagas y el silbido, calla la fauna y da truenos; en los 70 m por delante del
  frente, una parte (`StormNear`). Solo queda el golpe de la patada («¡PATADA!»: `Stomp` + `Slam` al patear y al aterrizar),
  que avisa de algo que te pasa a ti y no se repite.
- **Por fuera, en el borde de verdad**: velo de arena de 55 m apoyado en la arena del frente (a la altura de la cámara a
  lo ancho). 24 trastos que nacen en el polvo del borde (1-7 m tras el frente) y se quedan por el borde: cada uno busca
  su sitio respecto al frente (de 2 m por detrás a 5 m por delante) con un muelle y va a la velocidad del frente, barriéndolo
  de lado (±6,5 m/s que se frenan poco a poco); los pequeños (cubos, palas, chanclas, pelotas) a 1,2-6 m, rebotando y
  rodando por la arena; los grandes (sombrillas de 50 m, sillas, toallas, flotadores) planeando a 5-22 m. Viven 3,5-7 s.
  Ocho bañistas con los pies justo dentro del borde del polvo (2,5-11,5 m tras el frente), pisando en el borde (solo polvo:
  sin sonido ni sacudida por paso, ver «Sonido»).
- **Nada aparece ni desaparece de golpe**: el velo sube su «Opacity» (material `M_ProcStormVeil`) y crece desde la arena
  con `FrontBlend` (la cámara a menos de 450 m del frente); cada trasto nace fundiéndose en 0,45 s y creciendo (del 45 %)
  y se va en 0,6 s encogiendo; los bañistas entran y salen en 0,7 s. Para la opacidad, mientras se funde la pieza lleva
  una copia translúcida de su malla (material `M_ProcFXSoft`, parámetro «Opacity»); entera, vuelve a la opaca con luz.

### Gusano de arena (`ATN_BeachSandWorm`)

Lo que pidió el usuario: si en los 10 s que se dan tras la primera no llegan las demás, que salga de debajo de la arena,
como una animación, un gusano de arena gigantesco que se las coma. Es un remate cómico: nadie muere; la tortuga se queda
dentro, oculta, hasta la ronda siguiente, que vuelve a crear a todas en la salida.

- **Contrato** (`Public/World/Beach/TN_BeachSandWorm.h`): `ATN_BeachSandWorm::EatTurtle(Tortuga)` (servidor; nullptr sin
  autoridad, con la tortuga nula o ya comida), `IsBeingEaten(Tortuga)` (en cualquier máquina, desde que sale su gusano
  hasta que la tortuga reaparece) y `EatSeconds` = 3,2 s. El GameMode llama a `EatTurtle` con cada rezagada al acabar la
  cuenta (`FeedSandWorms`, ver «Cuenta atrás tras la primera y medias conchas») y espera `EatSeconds` antes del recuento.
  No hace falta pasarla a espectadora: si otra cosa se lleva su cámara (el fantasma, el podio), el gusano no se la pelea.
- **Escena** (segundos de escena, iguales en todas las máquinas con el reloj del servidor):

| Tiempo | Qué pasa |
|---|---|
| 0-0,7 | Aviso: bajo la tortuga la arena se hunde en un remolino de espiral que gira cada vez más deprisa (hasta 4,3 m de radio), con polvo, piedrecitas que saltan y retumbar grave; temblor de cámara entero a 15 m y hasta 60 m. La tortuga tiembla y se hunde 35 cm en la arena. |
| 0,7 | Revienta la arena: nube, terrones y piedras hacia arriba, arena y rugido, y un golpe de temblor fuerte (hasta 90 m). |
| 0,7-1,3 | Sale en vertical con los cuatro labios abriéndose como una flor (dientes a la vista) y la tortuga dentro, pataleando (pose de pataleta) y dando vueltas; sube 25 m (se pasa un poco y vuelve) soltando arena del cuerpo. |
| 1,2-1,55 | Bocado: la tortuga sube un poco dentro de la boca, los labios se cierran en cúpula (1,43) y desaparece (1,55): «¡ÑAM!» grande sobre la boca, bocado y golpe de temblor. |
| 1,55-2,25 | Traga: mastica, se dobla en arco (72°) y un bulto baja por el cuerpo hasta la arena, con dos «glup». |
| 2,35 | Eructa: entreabre la boca y suelta una nube de arena hacia donde mira. |
| 2,6-3,0 | Se hunde acelerando por su agujero (terrones, polvo, arena y retumbar). |
| 3,0-3,2 | El cráter (5,2 m de radio, con reborde y agujero oscuro) se cierra. |

- **Varias rezagadas**: cada una tiene su gusano; cada uno sale 0,11 s después del anterior de la misma tanda (como
  mucho 0,3 s) y su escena va algo más deprisa para acabar igual, a `EatSeconds` de la llamada. No chocan con nada.
- **Mallas** (`TN_BeachSandWormMeshes.h`, caras planas con color de vértice, en caché por paleta, `RF_Transient |
  RF_DuplicateTransient`): boca de 6 m de diámetro (la tortuga mide 1,4 m) con collar, encía, garganta oscura y tres
  coronas de dientes (16, 12 y 9) que apuntan hacia dentro; cuatro labios-pétalo con la piel fuera, la carne rosa dentro
  y dientes en los bordes; 13 anillos del cuerpo (5,4 m de grosor, más finos hacia abajo) con surcos oscuros y
  verrugas; cráter y remolino, a ras de arena y apoyados en su cuesta (el terreno no se agujerea: el hoyo es el centro
  oscuro, y lo que se hunde de verdad, dentro de la arena, son la tortuga y el gusano). Tres paletas: arena tostada, rosa
  de lombriz y gris de duna. Sin esqueleto: cada fotograma los anillos se colocan a lo largo de una columna (recta
  desde la arena y en arco arriba, con una ondulación de lado), los labios giran sobre su bisagra y cabeza y anillos se
  hinchan (bocado, eructo, bulto del trago).
- **La tortuga comida**: antes de empezar, el servidor le quita aturdimiento, bola, derribo y carga (y lo vuelve a quitar
  si algo se lo pone mientras está en la boca), le para el movimiento y le quita la colisión; queda marcada como sujeta
  (`ATN_BeachEnemy::SetTurtleHeld`: ningún enemigo le da). Cada máquina la coloca con las mismas cuentas (de pie en el
  remolino, luego dentro de la boca) sin suavizado de red y sin correcciones al dueño; en el bocado se oculta
  (`SetActorHiddenInGame`, replicado) y se queda de pie, oculta, donde estaba. Su jugador pierde el control (sin input en
  su tortuga) y su cámara funde en 0,6 s a un lado del gusano (15 m durante el aviso y 32 m después, nunca dentro de
  una roca ni de una duna), mirando a media altura para que quepa entero con la boca. El espectador que la sigue (fija o
  libre) pasa a esa misma vista con otra mezcla de 0,6 s (`UTN_GhostCameraModifier` con
  `ATN_BeachSandWorm::GetSpectatorView`, #260); al cambiar de jugador, vuelve a la suya.
- **Hasta cuándo**: el gusano sigue vivo, sin nada que ver, hasta que la tortuga reaparece (alguien la vuelve a mostrar)
  o deja de existir (la ronda nueva la quita); entonces el servidor lo destruye y cada máquina devuelve lo que tocó
  (input, cámara si aún miraba al gusano, colisión y la marca de sujeta).
- **Red**: un actor replicado y siempre relevante con la tortuga, la hora de inicio, el desfase, el suelo, el sentido del
  arco y la semilla, todo fijado al crearlo; ningún RPC. Cada máquina anima el gusano y los efectos con esas cuentas.
- **Sonido** (`UTN_BeachSandWormSynthComponent`, sintetizado como el de los enemigos): retumbar continuo (aviso y
  hundirse), rugido grave, arena que revienta, bocado («¡ÑAM!»: dos mordiscos húmedos con chasquido de dientes), trago y
  eructo.
- **Consola**: `TN.Beach.Worm [jugador]` (en el anfitrión; `jugador` es el índice en `PlayerArray`, 0 por defecto): se
  come ya a esa tortuga, en cualquier fase. En plena carrera se queda comida hasta la ronda siguiente.

### Enemigos de la ronda 3: ermitaño, pulpo, pulgas y tanque

Lo que pidió el usuario (tarea 7 de `Docs/Plan_Carrera_Ronda3.md`): cuatro enemigos nuevos, todos hijos de
`ATN_BeachEnemy` (red, tortugas, suelo, nivel de detalle y mareo por lo que se les lanza de la base). Nadie muere: mareo,
ragdoll y bola (`StunTurtle`, `KnockDownTurtle`). El reparto los coloca (ver «Reparto por ronda»): el ermitaño en calles
cuesta abajo, el pulpo en el agua de las pozas, las pulgas en claros de arena abierta y el tanque en tramos de través
cerca de lo militar y de las trincheras.

| Archivo | Qué es |
|---|---|
| `TN_BeachHermitCrab.*` | `ATN_BeachHermitCrab`: cangrejo ermitaño que rueda cuesta abajo metido en su caracola |
| `TN_BeachPoolOctopus.*` | `ATN_BeachPoolOctopus`: pulpo que vive en una poza, agarra a quien nada y la lanza fuera |
| `TN_BeachSandFleas.*` | `ATN_BeachSandFleas`: enjambre de pulgas de arena que pica y hace saltar sin control |
| `TN_BeachToyTank.*` | `ATN_BeachToyTank`: tanque de juguete teledirigido que patrulla y dispara bolitas de espuma |
| `TN_BeachCritterSynth.*` | `UTN_BeachCritterSynthComponent`: sus sonidos sintetizados (15 golpes y 3 continuos: concha que rueda, enjambre y motor eléctrico), el patrón de `UTN_BeachEnemySynthComponent` |
| `TN_BeachCritterMeshes.h`, `TN_BeachCritterKit.h` | Sus mallas low-poly de caras planas con color de vértice (caracola, cangrejo por piezas, pulpo, tramo de brazo, silueta, charco, pulga, tanque por piezas, banderita y bolita) y las piezas con `RF_Transient \| RF_DuplicateTransient` |

- **Comunes**: mallas construidas en ejecución y compartidas por paleta (`TNBeachKit::CachedMesh`), punteros
  `UPROPERTY(Transient)`, nada en un servidor dedicado salvo la caja sólida del tanque. Lo numeroso va por instancias
  (brazos del pulpo, pulgas, ruedas del tanque y bolitas). Los cuatro tienen `bThrottleWhenFar`: con una tortuga a su
  alcance (ermitaño: su calle + 30 m; pulpo: 1,3 radios de su poza + 30 m; pulgas: correa + vista + 10 m; tanque: medio
  tramo + 40 m) o la cámara cerca se mueven en cada fotograma; si no, más despacio (lo de la base). Ninguno anima lejos
  de la cámara (ermitaño 360 m, tanque 300 m, pulpo 240 m y sus brazos 90 m, pulgas 120 m). Sus cuerpos para lo que se
  les lanza (`GetHitCapsule`) y dónde van los pajaritos (`GetHitStunAnchor`) son los suyos (tabla de abajo).
- **Red**: la del resto de enemigos (servidor escucha, `FTNBeachMoverRep` y reloj del servidor) con el perfil de red de
  la ronda de `ATN_BeachElement`: como enemigos, no duermen y son relevantes por distancia (260-450 m); un cliente que
  llega a mitad de un estado lo retoma con la hora del estado. El ermitaño no manda nada mientras rueda: la rodada es
  determinista en cada máquina. El pulpo y las pulgas replican a quién tienen (`Grabbed`, `Infested`, siempre junto a un
  cambio de estado o con `ForceNetUpdate`); el tanque manda cada disparo con su hora. Efectos por multicast no fiable.

| Quién | Qué te pasa | Después |
|---|---|---|
| Ermitaño (bola) | derribo con ragdoll y mareo 2,5 s, lanzada a 3,2 m/s + 55 % de la velocidad de la bola hacia donde va (13 m/s como mucho), 2,4 m/s hacia su lado y 4,3 hacia arriba, dando vueltas (320°/s); «¡BOLO!» y, desde la segunda de la misma rodada, «¡STRIKE!» | te ignora 3 s; la bola sigue |
| Pulpo (agarre) | agarrada 0,8 s (sube 2,3 m sobre el agua pataleando) y lanzada en bola fuera de la poza hacia la salida (9-34 m), mareada el vuelo + 1,2 s; «¡SLURP!» y «¡FUERA!» con tinta | te ignora 5 s tras lanzarte |
| Pulgas (picada) | 2 s de saltitos sin control y picor; luego bola mareada 1 s; «¡PICA, PICA!» | te ignoran 6 s |
| Tanque (bolita) | bola mareada 0,8 s, empujada a 5,2 m/s hacia donde iba la bolita y 2,6 hacia arriba; «¡PAF!» | otra en cuanto te recuperas |

| Enemigo | Cuerpo que recibe lo lanzado | Mareado |
|---|---|---|
| Ermitaño | la esfera de la caracola | rodando, se para en seco; asoma con los ojos dando vueltas y meciéndose (0,6 s como poco) y, al pasársele, vuelve andando a lo alto. Esperando, no rueda; escondiéndose, vuelve a asomar |
| Pulpo | cápsula vertical del cuerpo donde esté (también bajo el agua) | suelta a la agarrada (cae al agua); flota de lado a ras del agua meciéndose (0,8 s como poco) y luego se hunde y vuelve a su sitio |
| Pulgas | la nube (0,9 de su radio), sobre la arena o sobre la picada; dispersas, nada | se dispersan (y sueltan a la picada, sin mareo final) y no se mueven hasta que se les pasa |
| Tanque | cápsula del casco, de delante a atrás | se para, echa humo gris por el escape, el motor tose cada 0,9 s, la torreta cabecea y la antena da vueltas como una hélice (720°/s, inclinada 35°); al pasársele, «boing» y 0,7 s sin disparar |

#### Cangrejo ermitaño bola (`ATN_BeachHermitCrab`)

- Caracola de turbante de 2,24 m de diámetro (4 cm reales), con surco y bandas en espiral, nudos en el hombro y la boca
  con su labio; el ermitaño asoma con cabeza, ojos negros en pedúnculos, antenas largas, pinza grande (la izquierda) y
  pequeña y cuatro patas. Cuatro paletas. `SizeScale` 0,8-1,25.
- **Calle**: el eje X local, centrada en el actor, con `Extent` de largo (el reparto: 25-45 m; 0 = 40 m). Lo alto, donde
  espera, es el extremo de -X (el reparto la pone cuesta abajo hacia +X); solo si el de +X queda 60 cm o más por encima,
  rueda al revés.
- **Espera** asomado (la caracola cargada, inclinada 15°), mirando calle abajo: ojos que miran alrededor, antenas y la
  pinza que saluda cada 3,5 s. Salta con una tortuga atacable dentro de la calle: entre 2,5 m por delante de él y 2 m
  antes del final, a menos de 5,2 m del eje (por el tamaño) y 9 m en altura; lo mira cada 0,1 s. Tras volver arriba,
  1,2 s sin poder rodar.
- **Se mete** en la concha (0,55 s: «¡plop!», se inclina y tiembla) y **rueda**: arranca a 2,5 m/s y acelera 3,8 m/s² más
  9 m/s² por la pendiente (entre 3 m/s y 15 m/s por la raíz del tamaño). Bota con el relieve (gravedad de 12,5 m/s²;
  sigue el suelo y sale despedida en las crestas; rebota un 32 % si cae a 2,6 m/s o más) y da botes sueltos (1,7-3,3 m/s
  cada 0,55-1,1 s yendo a más de 5 m/s). Culebrea ±1,2 m en ondas de 15 m (crece en los primeros 5 m). En los últimos
  7 m frena (4,5 m/s² como poco) y se para al final. Arena, retumbar hueco con un golpe por vuelta y golpe en cada bote.
- **Derriba** a quien toca la bola (1,12 m + 0,7 m del tramo que recorre en ese fotograma, y a menos de 2,2 m en altura),
  sin pararse: a todas las que estén en fila (tabla de arriba).
- **Al final** asoma (0,35 s), se sacude la arena (hasta 1,45 s, con granos que saltan), se da la vuelta (hasta 2,1 s),
  vuelve andando a lo alto a 2,3 m/s (por la raíz del tamaño, patitas) y arriba se da la vuelta (0,9 s) y espera.
- **Red**: los estados son función de su hora (reloj del servidor), su punto de partida (`Mover.Location`) y su meta
  (`Mover.Aim`); el servidor solo manda los cambios de estado. La rodada es un camino calculado igual en cada máquina
  (pasos de 1/60 s guardados a 30 muestras por segundo, 14 s como mucho) con el mismo perfil del suelo y la misma semilla
  (la del actor y el número de estado); se ve donde está la del servidor. Los derribos, por el servidor.
- **Perfil del suelo**: tres líneas (el eje y ±1,5 m) cada 1,5 m a lo largo de la calle, una vez por calle y máquina (la
  primera vez que rueda o anda: ~90 consultas de `GetGroundHeightAt`); el camino y la vuelta andando van sobre él.

#### Pulpo de poza (`ATN_BeachPoolOctopus`)

- Pulpo de 1,5 m de cuerpo (5,5 cm reales) con ojos saltones de pupila de barra, manchas (o los anillos azules de uno de
  ellos), sifón y ocho brazos de 3,7 m de seis tramos cada uno (48 instancias). Cuatro paletas. `SizeScale` 0,8-1,3.
- **Poza**: la del generador que contiene su sitio (`TNBeachLayout::Pools`, radio normalizado menor de 1,35); se queda
  donde lo pone el reparto (si está dentro del 70 % de la poza; si no, en su centro). Sin poza (otro mapa o puesto en la
  arena), su propio charco translúcido de 7 m de radio (por el tamaño), con el agua 45 cm sobre la arena, y cualquier
  tortuga que entre cuenta como nadando.
- **Acecha** bajo el agua, tumbado, con los brazos abiertos enroscándose: el anillo de los brazos 25 cm sobre el fondo
  (entre 0,4 y 1,5 m bajo el agua). Se ve su silueta oscura a ras del agua y burbujas cada 1-2,5 s (suenan mucho menos:
  ver «Sonido de ambiente en la carrera»). Pasea a 0,8 m/s por el 35 % de la poza alrededor de su sitio (otro destino cada 4-7 s).
- **Nadadora**: atacable, dentro de la orilla y nadando (o con el centro a menos de 40 cm sobre el agua); la busca cada
  0,15 s. Se fija en ella 0,5 s (se gira, burbujas y dos puntas de brazo que asoman como aletas) y va a por ella bajo el
  agua, estirado, a 5,2 m/s (por la raíz del tamaño; arranca en 0,25 s), sin salirse del 85 % de la poza. Se rinde a los
  7 s o si ella sale.
- **Agarre** a 3 m (en planta, por el tamaño): saca la cabeza, tres brazos la envuelven y la sube en 0,35 s a 2,3 m sobre
  el agua (a mitad de camino hacia él), meciéndola; la sujeción es la de la base (`BeginHoldTurtle`: pataleta, sin
  movimiento propio ni correcciones del servidor, colocada igual en cada máquina con `PlaceHeldTurtle`).
- **Lanzamiento** a los 0,8 s: hacia la salida (contra el mar del generador; sin generador, hacia atrás del actor), hasta
  la orilla de ese lado más 6 m (entre 9 y 34 m), con un tiro parabólico a 42° desde donde está (gravedad de la bola del
  caparazón, un 8 % más por su rozamiento): `StunTurtle` con esa velocidad, mareada el vuelo + 1,2 s. Tinta: una nube que se
  extiende por el agua y un chorro hacia donde la lanza. A los 0,6 s se hunde y vuelve a su sitio a 3,2 m/s (3 s como
  mucho).

#### Enjambre de pulgas de arena (`ATN_BeachSandFleas`)

- 48 pulgas (anfípodos de ~18 cm: 0,65 cm reales) en una sola malla instanciada, sin colisión; dos tonos. `SizeScale`
  0,8-1,25. Cada una salta por su cuenta (0,28-0,48 s y 0,4-1,1 m de alto, con pausas de 0,05-0,35 s) dentro de la nube
  (1,7 m de radio por el tamaño); al perseguir, la nube se estira hacia delante. Una mancha oscura en la arena (2,1 m) y
  polvo para verla de lejos; chisporroteo continuo a menos de 40 m.
- **Pasea** a 0,7 m/s (por el tamaño) por el 60 % de su huella; **ve** a 18 m (por la raíz del tamaño) una tortuga
  atacable dentro de su correa (24 m o 1,8 huellas desde su sitio; la mira cada 0,2 s) y va hacia ella a 1,6 m/s (por la
  raíz del tamaño): se le escapa andando.
- **Picada**: con la tortuga a 1,7 m + 1 m (en planta) y a menos de 4 m en altura, se le suben encima (el 75 % sobre el
  caparazón, con saltitos cortos y frenéticos). 2 s de saltitos sin control: siete impulsos (a los 0,1 s y cada 0,3 s) de
  3,2-4,2 m/s hacia arriba y 1,4-2,6 m/s de lado al azar que sustituyen su velocidad. Picor: «¡PICA, PICA!» y, a los
  0,9 s, «¡QUÉ PICOR!» (y un temblor en su pantalla). Al final, bola mareada 1 s. Se corta antes (sin mareo) si la
  derriban, la aturden o la sujetan, o si marean al enjambre.
- **Se dispersa** (hasta 6,5 m en 1,2 s, con saltos largos) y se vuelve a juntar hasta los 3,2 s.
- **Red**: el centro del enjambre en `Mover` (8 Hz) y la picada en `Infested` y la hora del estado. Los saltitos son
  deterministas (hora y semilla): el servidor los da a su copia y el dueño de la tortuga a la suya a la misma hora (sin
  RPC por salto y sin correcciones); el que le llegue más de 0,25 s tarde no se da. Las pulgas son locales.
- **Rendimiento**: una actualización de 48 instancias por fotograma solo con la cámara a menos de 120 m; su suelo es un
  plano de tres consultas cada 0,4 s; en el servidor, una consulta cada 0,3 s mientras se mueve.

#### Tanque de juguete teledirigido (`ATN_BeachToyTank`)

- Tanque de plástico de 4,5 m de largo (16 cm reales), 2,5 m de ancho y 2 m de alto: orugas de goma con eslabones y cinco
  ruedas por lado que giran, guardabarros, glacis, rejilla del motor, faros, escapes, escarapelas de Tortunavy en los
  costados, estrellas doradas en la torreta, cañón de 2,7 m con la punta naranja de los juguetes y antena de látigo de
  3,2 m con la banderita de Tortunavy (azul marino con la estrella dorada) que ondea. Cuatro paletas (oliva, arena, gris
  de Tortunavy y camuflaje). `SizeScale` 0,8-1,2. Caja sólida tipo Pawn del tamaño del casco en todas las máquinas.
- **Patrulla** su tramo (eje X local centrado en el actor, `Extent` de largo: el reparto, 20-40 m de través; 0 = 24 m; sin
  0,6 medio casco en cada punta) a 2,6 m/s (por la raíz del tamaño, 3 m/s² de aceleración y frenando para llegar parado)
  y en cada punta gira sobre sí mismo a 150°/s con las orugas a contramano. El casco se inclina con el suelo.
- **Ve** a 25 m una tortuga atacable (a menos de 8 m en altura; mira cada 0,2 s patrullando o girando): se para
  frenando, gira la torreta a 110°/s y, con ella a menos de 7°, parado y a los 0,7 s de verla, dispara; luego cada 1,6 s.
  La deja a 29 m o cuando deja de ser atacable (la bolita que da la marea) y busca otra; sin nadie 1,2 s, vuelve a
  patrullar.
- **Bolita de espuma** (53 cm, naranja con franja amarilla): sale a 19 m/s con el tiro bajo (−12° a 45°) hacia donde
  estará la tortuga (el 60 % del adelanto), cae con 7 m/s² y deja una estela de humo; «¡pomp!», humo en la boca, el cañón
  recula 45 cm, el casco cabecea 5° y recula 20 cm y la antena se echa atrás. Bota (55 % en planta y 42 % hacia arriba)
  hasta 3 veces o 2,4 s; tras el primer bote o tras dar a alguien ya no hace nada. Da a quien pase a menos de 0,27 m +
  0,7 m de su recorrido (tabla de arriba), si no hay nada entre las dos, y rebota hacia atrás.
- **Choca con lo que hay por medio** (#247; antes solo la paraba el suelo sin trazas y cruzaba paredes, murallas y
  fortalezas): en cada paso, en todas las máquinas, se barre su esfera por el canal `WorldDynamic` sin lo que es `Pawn`
  (tortugas, enemigos, el propio tanque) ni las bolas de caparazón, sin contar lo que ya tocaba al salir. Si da en algo,
  se queda ahí y rebota (`TNBeachTankFoam::BounceOffWall`: 40 % hacia fuera, 60 % a lo largo) y ya no hace nada.
- **Red**: el casco en `Mover` (10 Hz); la torreta de cada máquina gira hacia `Mover.Aim` (la tortuga a la que apunta). Cada
  disparo va por multicast no fiable con su hora del servidor: la bolita del cliente sale adelantada lo que tardó el
  mensaje y va a la par que la del servidor. Si se pierde el mensaje, solo falta la bolita en esa pantalla (el golpe lo
  decide el servidor y llega por su multicast y por el aturdimiento). Suelo de la bolita sin trazas: entre el de la boca
  del cañón y el de donde apuntaba.
- **Sonido**: motor eléctrico que sube con la marcha y con el servo de la torreta, «¡pomp!», «¡paf!», tos y «boing».

#### Probar (PIE de 2)

- `TN.Beach.Place HermitCrab 1 2000` (queda 3 m delante, con la calle hacia donde miras): rodearlo y ponerse en su calle
  delante de él: se mete (plop), rueda acelerando, bota, culebrea y derriba (ragdoll lanzado). Ponerse las dos en fila:
  derriba a las dos («¡STRIKE!»). Apartarse de lado 3 m: pasa de largo. Mirar el final: frena, asoma, se sacude, se da la
  vuelta y vuelve andando; arriba se da la vuelta y vuelve a esperar. `TN.Beach.StunNearest` con la bola rodando: se para
  en seco y asoma mareado. Que la otra ventana vea la bola en el mismo sitio y los botes a la vez.
- `TN.Beach.Place PoolOctopus` desde la orilla de una poza (o en la arena, con su charco): entrar nadando: burbujas,
  puntas que asoman, va a por ti, te agarra y te sube pataleando, te lanza en bola fuera de la poza hacia la salida con
  tinta. Salir nadando antes de que llegue (se rinde). Con la otra tortuga, lanzarle una piedra mientras te sube: te
  suelta al agua. En las dos ventanas, la tortuga agarrada en el mismo sitio y sin tirones.
- `TN.Beach.Place SandFleas`: acercarse andando (van hacia ti despacio: se les escapa andando); quedarse quieta: saltitos
  sin control 2 s con «¡PICA, PICA!» y bola mareada 1 s; que la ventana de la picada no tenga correcciones (tirones) en
  los saltitos. Lanzarles una piedra: se dispersan.
- `TN.Beach.Place ToyTank`: acercarse a menos de 25 m: se para, gira la torreta y dispara bolitas con estela; esquivarlas
  corriendo de lado; que te dé: bola mareada y empujada, «¡PAF!». Chocar con él (es sólido). `TN.Beach.StunNearest`: humo,
  tos y antena que da vueltas. Que la otra ventana vea salir la bolita a la vez.
- `TN.Beach.Enemy.Debug 1`: calle del ermitaño (y su camino al rodar), poza y alcance del pulpo con la dirección del
  lanzamiento, radios de las pulgas y tramo, vista, torreta y bolitas del tanque.

### Probar

- `TN.Beach.Place GiantCrab` (y `SeaUrchin`, `Lizard`, `QuadLane`, `GullZone`), del generador, en cualquier mapa.
- **Gusano** (PIE de 2): `TN.Beach.Worm 1` en el anfitrión con la otra tortuga en la arena → en las dos ventanas,
  remolino, salida con la tortuga pataleando en la boca, «¡ÑAM!», bulto, eructo y cráter que se cierra, a la vez; la
  comida no se mueve ni usa nada y su cámara mira desde un lado (la otra ventana deja de verla en el bocado). Después, la
  cuenta de verdad: llegar con una y dejar a la otra en la arena 10 s; que el recuento espere al gusano y que la ronda
  siguiente la traiga con control y cámara normales. Probar también con la rezagada en bola, derribada, en brazos de
  otra o saltando al acabar la cuenta, y con 3 o más (que no salgan todos a la vez).
- **Cangrejo**: mirar que nunca se queda quieto más de un segundo (recorrido y chasquidos); que ande de lado con las
  patas alternas, el cuerpo balanceándose, que arranque y frene sin tirones y gire poco a poco, sin patinar; que en una
  zona llena de decorado rodee las rocas en vez de empujarlas y no se quede atascado. Acercarse por detrás andando (oye a
  10 m: se da la vuelta), corriendo (13 m) y en panzazo o agachada (6 m); de frente, a 22 m. Esprintar (se escapa);
  quedarse quieta en la sombra de la pinza (bola aturdida); salir de la sombra durante el aviso (falla; el círculo es de
  1,7 m y desde ~9 m de su cuerpo ya no ataca con la pinza; saltando por encima de la pinza se libra); alejarse más de
  38 m (vuelve a su recorrido). **Embestida**: quedarse a 9-13 m delante de él (a más de 13 m ya no embiste; pasar a 1 m de su costado embistiendo no
  arrolla): se agacha, sale disparado de lado,
  arrolla (ragdoll lanzado) y derrapa con surcos y arena; apartarse de lado a tiempo. Ponerse detrás de una roca grande
  (no embiste con algo en medio). Con un grupo de 2-3: que no se monten unos encima de otros.
- **Erizo**: quedarse quieta delante (rueda hasta pinchar: ragdoll lanzado y mareo), tocarlo andando por detrás.
- **Lagarto**: `TN.Beach.Lizard huidizo`: que pasee entre ratos al sol; acercarse despacio (alerta a 30 m), luego a 17 m
  (susto o huida), hasta que se meta bajo una roca (debajo, no encima) o se entierre; esperar lejos a que salga.
  `TN.Beach.Lizard generoso`: motas y collar dorados que destellan; al acercarse huye y deja un objeto o una concha; que
  la otra ventana vea el mismo premio. `TN.Beach.Lizard mordedor`: cresta roja; acercarse de pie: se lanza, muerde,
  zarandea con la tortuga en la boca y la lanza en bola mareada; en las dos ventanas, la tortuga en su boca a la vez.
  Repetir en bola (no puede morder) y apartándose en el último momento (falla y se va).
- **Quad**: `TN.Beach.Quad.Now`; notar el temblor y el humo del lado de salida; ponerse en una rodada (ragdoll lanzado
  dando vueltas), fuera del paso y en medio de las dos rodadas (sobrevive). Probar los dos sentidos.
- **Gaviotas**: mirar que cada una va por su círculo y a su altura, con su sombra bajo el cuerpo (más pequeña y oscura
  cuanto más baja). `TN.Beach.Gull.Attack 1` (cagada: un signo de exclamación amarillo parpadea sobre
  ti, cada vez más deprisa, y una sombra negra que nace de 90 cm y crece mientras cae, 2,1 s; andando te pilla, corriendo te
  libras; quedarse: ragdoll con la cagada PINTADA en el caparazón —salpicadura blanca con gotas, no un objeto encima—, que se
  ve en las dos ventanas, va con el ragdoll y en bola, se seca desde los 8 s y desaparece a los 12 s; ejecutar antes
  `Scripts/create_poop_decal.py`) y `TN.Beach.Gull.Attack 2` (picado:
  aparece una sombra negra diminuta bajo ti que crece mientras la gaviota baja siguiéndote, 2,3 s; andando te pilla;
  echar a correr al verla: te libras, baja igual, pica la arena con «¡PIC!» y vuelve a subir; quedarse: abre el pico, te
  coge por el caparazón, cuelgas pataleando, sube aleteando, vuela y te suelta en bola). **Al soltarte, siempre caes al
  suelo** (mirarlo en las dos ventanas, siendo el anfitrión y siendo el cliente): también metiéndote en bola colgando (se
  escurre), con la tormenta detrás (te suelta dentro y llega la patada) y dándole con una piedra mientras te lleva. Con
  una sombrilla encima: pica en la sombrilla.
- **Mareo**: lanzar una piedra (o el pulpo de tinta) al cangrejo, al erizo, al lagarto y a la gaviota que baja en picado
  (o a la que se lleva a la otra tortuga: la suelta): «¡TOING!», pajaritos y estrellas encima, sin atacar ~3 s; lanzar a
  la otra tortuga en brazos contra un cangrejo. `TN.Beach.StunNearest [s]` para verlo sin puntería. Que las dos ventanas
  vean los pajaritos a la vez y el rebote de la piedra en el erizo o el lagarto.
- **Tormenta**: en la carrera arranca sola y va a 1,8 m/s; `TN.Beach.Storm.Info` dice dónde va y a qué velocidad. En
  otro mapa, `TN.Beach.Storm.Start [metros por detrás] [cm/s]` y `TN.Beach.Storm.Stop`. Que el velo, los trastos y los
  bañistas estén sobre la arena y en el borde del frente (ni por delante ni por detrás), y que nada aparezca ni
  desaparezca de golpe (acercarse y alejarse del frente, mirar los trastos al nacer y al irse). `TN.Beach.Storm.Here`: la
  pierna de un bañista te patea y sales en bola hasta ~20 m por delante del frente; probar de pie, en bola, derribada y
  con una gaviota que te suelta detrás del frente (otra patada al caer). Sin «pum, pum, pum»: junto al borde solo se oye
  el viento del paisaje sonoro (y la patada cuando te la dan).
- En PIE con 2-3 jugadores: que los clientes vean lo mismo (posiciones suaves, golpes a la vez, el ragdoll lanzado igual
  en todas las pantallas, la tortuga en el pico o en la boca del lagarto sin tirones, la bola de la patada igual) y que
  no haya correcciones raras al empujar el lagarto, al atropellar, al colgar del pico o al morder (sobre todo en la
  pantalla de la mordida o de la que cuelga).
- Consola: `TN.Beach.Enemy.Debug 1` (radios de vista y de oído, correa, recorridos y golpe del cangrejo, cajas de las
  ruedas en el servidor) y `TN.Beach.Enemy.Stats`.

## Sonido de ambiente en la carrera (el «agua» molesta)

El usuario se quejó de un sonido «como de agua» constante y molesto en `LVL_BeachRace`, sin saber qué lo producía. Casi
todo se sintetiza en código, así que se repasó todo lo que emite sonido en la playa (los `USynthComponent` de
`World/Beach/`, el ambiente de `Audio/`, el foley de la tortuga) buscando lo que **se repite solo, sin que nadie lo
provoque, y se oye desde lejos**. En la carrera no hay `ATN_ProcMapGenerator`, así que el paisaje sonoro
(`UTN_AmbientSoundscapeComponent`) suena en su mezcla genérica (brisa y pájaros, sin oleaje ni agua corriente); las
pozas, el mar y la meta no tienen sonido continuo (el chapuzón de meta es un disparo). Los culpables:

| Qué | Por qué molestaba | Ahora |
|---|---|---|
| **Balsa** de la plataforma móvil (`ATN_BeachMovingPlatform`, ~6-8 por ronda con 800 m) | Va y viene sin parar: cada ~4 s un roce de arena al salir y un «chof» de agua (`Squelch`: ruido por un paso banda que gorgotea más un «blup» grave, a 0,7) al llegar, idénticos y oídos hasta a 41 m. Con varias por la playa, una fuente que no cesa | Los sonidos de salida y llegada solo suenan si la cámara local está a menos de 18 m (`TNBeachPlatformDetail::SoundReach`), más flojos cuanto más lejos (volumen 0,4-0,45 × 1..0,45; antes 0,6-0,7) y con el tono variado ±8 % cada vez. Alcance de la fuente 7 m + 24 m (antes 9 + 32). Las partículas de la llegada no cambian. Igual para el ascensor (crujido y golpe), que también es perpetuo |
| **Burbujas del pulpo de poza** (`ATN_BeachPoolOctopus`, 1-2 por poza en 7 pozas) | Acechando bajo el agua soltaba una burbuja sonora (`Bubble`: seno agudo de 380 a 1000 Hz, a 0,3 de pico) cada 1-2,5 s, y **todos** los pulpos a menos de 35 m de la cámara a la vez: un «glu, glu, glu» que no para | Suenan solo a menos de 24 m (`TNBeachOctopus::BubbleAudibleDistance`), solo las de los **2 pulpos más cercanos** (`BubbleMaxAudible`, presupuesto `TNBeachKit::AmbientVoiceTouch/Claim` en `TN_BeachEnemyKit.h`, por mundo), nunca dos a menos de 1,6 s, y cada pulpo una cada 3,5-7 s (las burbujas que se ven no cambian: suena una de cada tres o cuatro). Volumen 0,35 (antes 0,5) y el sonido de la burbuja, más grave (290-720 Hz), de ataque más blando y a 0,42 (antes 0,6). Voz propia (`BubbleVoice`, se crea la primera vez que suena) marcada `bAmbientBed`: baja con el volumen de **Ambiente** del menú de pausa (`UTN_GameSettingsSubsystem::ClassFor`); el resto de sonidos del pulpo (agarre, tinta, avisos) siguen en Efectos |
| **Viento y pisotones de la tormenta** (`TN_BeachStorm.cpp`; antes `UTN_BeachEnemySynthComponent::SetWind` y `Stomp`) | Siseo ancho de ruido por dos pasos banda (320 y 1050 Hz × ráfaga) siempre encendido detrás de la carrera: sonaba a agua corriente (ya atenuado en la vuelta anterior: banda alta a 760 Hz, nivel cuadrático). Y un pisotón por paso del bañista más cercano, «pum, pum, pum» a todas horas hasta a 200 m | Se quitan los dos: la tormenta no tiene voz propia continua ni rítmica y su ruido es el paisaje sonoro del cooperativo (`UTN_AmbientSoundscapeComponent` ahora atiende a `ATN_BeachStorm`: ver «Tormenta de bañistas», «Sonido»). `SetWind` sigue en el sintetizador, sin usar |
| **Enjambre de pulgas** (`ATN_BeachSandFleas`) | Chisporroteo agudo casi continuo (0,45) en los 40 m alrededor de cada enjambre suelto | 0,3 y solo a 30 m; picando a alguien, como antes (1,0) |

Lo demás se ha mirado y se queda: el ambiente genérico (brisa a 0,5 × 0,8 de `MasterVolume`, pájaros a ratos), los
graznidos de las gaviotas (cada 5-12 s por ave y solo si la cámara está cerca), los pasos del cangrejo gigante (avisan de
que viene), el motor del tanque (solo si se mueve, a 60 m) y el chapuzón de meta (es un disparo por tortuga y suena de
lejos a propósito).

**Categorías del menú de pausa.** Música: `UTN_MusicSynthComponent`. Ambiente: `UTN_AmbientSynthComponent` (paisaje
sonoro, cascadas, géiseres, lava) y, desde esto, los sintetizadores de criaturas con `bAmbientBed` (burbujas del pulpo).
Efectos: todo lo demás que se sintetiza en código (trampas, enemigos, tormenta, foley de la tortuga), que va a la clase por
defecto. La tormenta de la playa ya no pone viento ni pisadas en Efectos: su ruido es el paisaje sonoro (Ambiente, sube y
baja con ese volumen); en Efectos solo queda el golpe de la patada.

**Probar** (sin tormenta ni enemigos que molesten: `open LVL_BeachRace?BeachSeed=42` y `TN.Beach.Place`):
1. Acercarse a una balsa (`TN.Beach.Place MovingPlatform 1 0 2`) desde lejos: a más de 18 m, silencio; entre 18 y ~8 m,
   roce y «chof» flojos; encima de ella, algo más fuertes pero ya no taladran. Con el ascensor (`... 1 0 3`), igual.
2. Junto a una poza con pulpo (`TN.Beach.Place PoolOctopus`): una burbuja suave cada pocos segundos como mucho, nunca dos
   pozas a la vez a pleno ritmo. Con `TN.Beach.Place PoolOctopus` repetido tres o cuatro veces: siguen sonando solo dos.
3. Menú de pausa: bajar **Ambiente** a 0 silencia las burbujas (y el paisaje sonoro); bajar **Efectos** no las toca.
4. Correr con la tormenta detrás: ningún «pum» rítmico al pasar junto a los bañistas (solo polvo); el viento con ráfagas y
   truenos, el del cooperativo, se nota al meterse en los 70 m por delante del frente y sube dentro (`TN.Ambience.Debug 1`
   enseña «tormenta» en la mezcla). Bajar **Ambiente** lo baja.

## Botín en la playa (`TN_BeachLoot`)

Como en el cooperativo, en la playa se rebusca en el decorado, hay objetos y power-ups por el suelo (para ti o para
fastidiar a las demás) y conchas de puntos; todo lo que se coge lleva la marca común (anillo dorado que gira en el
suelo, columna de luz tenue, chispitas que suben y luz suave cerca: `Docs/Botin_Decorados.md`, «Brillo de lo que se
coge»). A petición del usuario, a rebosar: casi todo el decorado se rebusca y hay objetos por toda la playa.

| Archivo | Qué es |
|---|---|
| `Public/World/Beach/TN_BeachLoot.h`, `Private/World/Beach/TN_BeachLoot.cpp` | `TNBeachLoot` (reglas, pesos y `SpawnRoundLoot`), `ATN_BeachSearchSpot` (el rebuscable con las reglas de la carrera) y `UTN_BeachLootSubsystem` (reparte y quita el botín de cada ronda) |
| `Private/World/Beach/TN_BeachLootShells.cpp` | Las conchas de puntos de cada ronda (`UTN_BeachLootSubsystem::SpawnRoundShells`) |
| `Public/World/Beach/TN_BeachChest.h`, `Private/World/Beach/TN_BeachChest.cpp` | Los cofres (`ATN_BeachChest`, el elemento `TreasureChest` del reparto, y `ATN_BeachChestSpot`, el cofre que se abre) y `TN.Beach.Chest` |

### Cuándo se reparte

- En el servidor, en cada ronda y con su semilla (misma semilla, mismo reparto; `TN.Beach.Loot.Reroll` cambia la
  tirada). Al cambiar de ronda, o al quitarla, se va todo lo de la anterior: los rebuscables se llevan lo que salió de
  ellos y nadie cogió, y se quitan las conchas y los objetos sueltos que queden. Lo que ya está en un inventario se queda.
- `UTN_BeachLootSubsystem` (subsistema de mundo con tick; solo hace algo en el servidor y donde hay un
  `ATN_BeachRaceGenerator`) mira el generador sin tocarlo y reparte en cuanto ve una ronda nueva lista (el mismo
  fotograma o el siguiente). Para que salga exactamente con los elementos, el generador puede llamarlo él: en
  `ATN_BeachRaceGenerator::GenerateRound` (`TN_BeachRaceGenerator.cpp`), justo después de
  `const FString Missing = SpawnRoundElements();`, la línea `TNBeachLoot::SpawnRoundLoot(*this);` (con
  `#include "World/Beach/TN_BeachLoot.h"`). Con o sin ella, nunca se reparte dos veces la misma ronda.

### Rebuscables (`ATN_BeachSearchSpot`)

- El rebuscable del mapa procedural (mantener E 1,3 s, aro, «¡puf!» u «¡pof!», saltito del objeto, una vez para todas:
  la primera que llega) con las reglas de la carrera: **70 % de suerte** (55 % en el cooperativo: pararse en una carrera
  cuesta), los pesos de la carrera (abajo), polvo de arena y las pistas a la escala de la playa: chispitas desde 35 m y
  el anillo dorado de los objetos del suelo, fijo y centrado en el montículo de arena de ese punto (#214; a ras de su
  arena y algo mayor que él, 1,3-1,9 m de radio; el actor sabe el índice de su montículo, `SetMoundIndex`, y cada máquina
  saca el sitio del que ella monta: `ATN_BeachSearchRegistry::GetMoundFoot`). Se ve desde que aparece el actor, a 50 m de
  alguna tortuga, y no sigue a nadie. El alcance para rebuscar es el de la tortuga, 2,5 m (antes 3,5).
- **Huella real**: la caja de la malla del decorado (su cuerpo), girada, inclinada y escalada como ese ejemplar; cápsula
  a lo largo del lado largo (tablones, troncos, toallas, botellas...) y redonda en lo demás. La sombrilla se rebusca en el
  montón de arena de su pie (la lona está a 35 m).
- **Qué decorado** (`TNBeachLoot::SearchChance`, probabilidad por ronda):

| Probabilidad | Decorado |
|---|---|
| 100 % | castillos (pequeño y enorme), restos de vela, silla, sombrilla, red de pesca, grupo de rocas, tablones, tronco con musgo, cubito de juguete, toalla, sacos terreros, caja de munición, red de camuflaje |
| 85 % | roca, madera a la deriva, botella, vaso, boya, coco, corteza de sandía, sujetador, chanclas, pelota, disco volador, crema solar, bidón, erizo antitanque, soldaditos, casco, almeja |
| 60 % | lata, brick, gafas de sol, patito, chupachups, concha de adorno, estrella de mar, anillas de latas, trozo de cuerda, hueso de sepia |
| nunca | chapas, cáscaras, palitos de helado, pluma, pajita (diminutos o finos), medusa (pica), pasarela y caminito de palos (son camino) |

- **Uno por corrillo**: 9 m como poco entre centros y 3 m entre bordes; hasta 240 por ronda y 50 por sexto del recorrido
  (360 y 75 con 1200 m; ×1,6 en fácil y ×1,4 en difícil). Son puntos de un registro: el actor aparece solo cerca de alguna tortuga (ver
  «Rendimiento y red»).
- **Montículo de arena** junto a cada uno: dice «aquí se puede rebuscar» desde lejos (abajo).

### Montículos de arena (`ATN_BeachSearchRegistry`, `TN_BeachSearchMounds.cpp`)

Lo pidió el usuario: donde se puede rebuscar, un montículo de arena que vibre, para saber que ahí se rebusca.

- **Qué es**: un montón pequeño de arena removida (1,6-2,5 m de ancho y 30-48 cm de alto, a manchas, con terrones
  alrededor), en la arena junto al borde del decorado que se rebusca, algo metido bajo él, como escarbado de debajo.
  Variantes (`TNBeachDecorKit::SearchMoundMesh`): lisa, con una chapa roja de canto, con un palito de helado clavado y
  con un trozo de concha rosada asomando. Junto a la basura y lo del día anterior (sillas, sombrillas, toallas, vasos,
  latas...) salen chapas y palitos; junto a lo que trae el mar y los castillos, conchas; junto a lo militar, alguna chapa.
- **Dónde**: del lado por el que se llega (hacia la salida, ±50°; si ahí hay otra pieza, a los lados o detrás), en el
  punto del borde de la huella de rebuscar y 0,3 veces su radio por fuera, a la cota de la arena con los asientos. Lo
  decide el servidor al repartir el botín, con su propio azar (`MakeMound`), y va en el registro replicado
  (`FTNBeachSearchNet::Mounds`: X cada 2 cm, Y y Z cada cm, giro y aspecto; 8 bytes por punto, una vez por ronda). Así
  es igual en todas las máquinas sin depender de que el cliente haya acabado su reparto. Se prueban hasta ocho lados
  (`TNBeachLoot::PickMoundSide`: el de llegada, los costados, la espalda y las diagonales); **si ninguno está libre, ese
  decorado no es rebuscable** (antes el montículo se dejaba en el primer lado aunque estuviera dentro de otra pieza o en el
  agua de una poza, y el aviso salía sin montículo a la vista).
- **El rebuscable es el montículo** (#254): el actor interactivo del punto (`ATN_BeachSearchSpot`) se pone en el pie de su
  montículo, con la huella de su base (1,1 × 100 cm × su tamaño; `TNBeachLoot::FSearchAnchor`), y no en el centro del decorado.
  El aviso «Mantén para rebuscar», el alcance, las chispitas, el anillo y la tierra que salta salen así junto al montículo, no
  por cualquier lado de un decorado que puede dar la vuelta de más de 20 m (el castillo enorme, el barco, la sombrilla con
  3,8 m de radio). Como el montículo del tutorial (huella y alto 110 cm).
- **Cómo se ve**: lejos, quietos, en mallas instanciadas (una por variante y otra para los aplanados), sin sombra y
  hasta 120 m. Cerca de una cámara local (45 m, los 16 más cercanos), un componente de una reserva hace temblar el
  montículo **a ratos**: un temblor corto (0,35-0,6 s) cada 2,5-6 s, con 3 granitos de arena que saltan; con una
  **tortuga a menos de 12 m**, más largo y más fuerte (0,55-0,9 s) cada 0,4-1,2 s, con 7 granitos, y en cuanto llega
  una, enseguida. El temblor es un meneo de giro, escala y sitio (unos grados y unos centímetros), y los granitos,
  partículas de `TNAmbientFX`. Los que tiemblan dan sombra (están cerca).
- **Rebuscado**: se aplasta en 0,35 s (si tiembla cerca de una cámara; si no, de golpe) y queda **aplanado y quieto**:
  un disco de arena removida casi a ras con marcas de escarbar. Al quitar el decorado del nido del sprint, los suyos
  quedan aplanados.
- **Coste**: unas 5 mallas instanciadas con hasta ~380 instancias (240 × 1,6 en fácil; ~580 con 1200 m), 16 componentes como mucho que
  se mueven y un emisor de partículas; nada en el servidor dedicado. `TN.Beach.Perf` dice cuántos tiemblan.
- Probar: `open LVL_BeachRace?BeachSeed=42`, con 1 y con 2 jugadores. Desde lejos se ven los montículos junto al
  decorado; al acercarse, alguno tiembla con granitos y, a menos de 12 m, más a menudo. Rebuscar: el montículo se
  aplasta y queda aplanado en las dos ventanas (también para la que llega después). `TN.Beach.Loot.Reroll` los cambia.
  Con ~150 piezas de decorado por ronda salen del orden de 50-80 (el registro da el número exacto). El aviso «Mantén
  para rebuscar» solo sale a unos metros de un montículo, nunca al otro lado del decorado.

### Objetos sueltos

- **20-25 sueltos** (30-38 con 1200 m), uno por tramo igual del recorrido desde los 90 m (nada en la salida) hasta 30 m
  del filo, más hacia el centro que hacia la selva.
- **3 filas de lado a lado de la playa** (4 con 1200 m; hacia el 17, 50 y 83 % del recorrido, ±6 %; un objeto cada 32 m,
  de selva a selva), como las cajas de objetos de las carreras de karts: todas pasan por una. Si su sitio está ocupado, un poco más
  adelante o atrás.
- En arena libre: a 3 m de lo que ocupa cada elemento del reparto (también de los pasos de quads), fuera del agua de las
  pozas, a 2,5 m del borde de los rebuscables y a 6 m de otro objeto; apoyados en el suelo de la ronda
  (`GetGroundHeightAt`). En total, unos 35-45 por ronda (55-65 con 1200 m).
- **Cajas de objetos** (`ATN_RaceItemBox`, el «?» de las carreras de karts): en cada sitio hay una caja y lo que da se sortea
  al cogerla, **según el puesto de quien la coge** (ver «Objetos de carrera»). Antes eran objetos fijos del catálogo con su
  pickup (`PickupActorClass` + `InitializeFromInventoryItem`).
- **Nada suelto pasa a la ronda siguiente** (#71): lo que dejan las jugadoras (un objeto soltado con la tecla de soltar, el
  pickup que sale de una bola al pararse, una concha trampa y la reciclada que deja al gastarse) y las bolas en el aire se
  quitan al preparar la ronda siguiente, al volver a jugar y al empezar el sprint final
  (`ATN_BeachRaceGameMode::CleanupRoundLeftovers`, que llama `CleanupRoundActors`). Los pickups de serie (cajas, lo que sale
  de rebuscar) los vuelve a quitar el botín al cambiar la ronda. No se toca nada de lo colocado a mano en el nivel
  (`AActor::IsNetStartupActor`); `LVL_BeachRace` solo lleva el generador. Regla pura: `TNBeachRaceRules::ShouldClearRoundLeftover`
  (`Tortunabo.BeachRace.RoundLeftovers`).

### Pesos de la carrera (`TNBeachLoot::RaceWeight`, por el uso del objeto)

> Esta tabla ya no decide los rebuscables, las cajas ni los cofres: ahora pesan **según el puesto** y con los objetos de
> carrera incluidos (`TNRaceItems::RollLoot`, ver «Objetos de carrera», «Pesos por posición»). `RaceWeight` solo lo usa el
> lagarto generoso (`TN_BeachLizard.cpp`).

| Uso (`ETN_ItemUseType`) | Fila de `DT_Items` | Peso | Por qué |
|---|---|---|---|
| `SelfStaminaBoost` | StaminaBoost | 1,5 | energía sin fin unos segundos: correr y dejar atrás la tormenta |
| `SelfStaminaFull` | (ninguna todavía) | 1,2 | la barra llena de golpe |
| `Throwable` | ThrowableBall | 1,3 | derriba a la que alcanza |
| `InkThrower` | Tinta | 1,3 | ciega a la que alcanza |
| `Conch` | Conch | 1 | concha trampa para las de detrás |
| `BigHead` | BigHead | 0,3 | en la playa no protege de nada (las gaviotas de la carrera no la miran): solo la cabezota y el mareo al acabar |
| `Totem` | Totem | 0 (fuera) | en la carrera no se muere: no revive a nadie |

Las filas sin `PickupActorClass` o sin uso (`None`) no salen nunca, como en el cooperativo; un uso nuevo sale con peso 1.

### Conchas de puntos (`ATN_ScorePickup`, las del cooperativo)

Las mismas conchas (`TNScoreShells`: 1, 25, 50 y 100), que suman `RaceScore` a quien las coge, como en el cooperativo.
Muchos sitios salen de los puntos interesantes del reparto (`TNBeachLayout::FRoundLayout::Interest`: arcos de salto,
cimas, atajos, rincones, trincheras y caminos alternativos). Se planean en este orden (lo difícil primero, para que cada
sitio tenga la suya), con topes por ronda de 200 de 1, 27 de 25, 8 de 50 y 2 de 100 (300, 40 y 12 de 1, 25 y 50 con
1200 m), y 1 m entre conchitas y 3 m
alrededor de las demás:

- **Reinas de 100**: en la sala de arriba del castillo con salas, entre sus dos muretes (tras la puerta de conchas, las
  algas del pasillo y la escalera), y en lo alto del castillo enorme (si ya hay dos reinas, grande de 50).
- **Grandes de 50**: en los rincones escondidos (hasta 3), en las dos primeras trincheras, tras el alambre de espino
  (hasta 3, a 2,6 m hacia el mar), tras las dos primeras minas, en lo alto de dos castillos pequeños y encima de las
  plataformas móviles (hasta 2, sobre lo más alto del elemento al crearse: se coge montada en ella). Si se acaban, las
  de los rincones y las trincheras pasan a normales.
- **Normales de 25 junto a los peligros**: en medio de las algas (5), dentro de la concha que atrapa (4), en el fondo del
  hoyo de la plataforma que se rompe (3), dentro del cubo roto (3), dos por paso de quads en sus rodadas, en casa de
  cangrejos y erizos (5), bajo las gaviotas (3), tras los sacos terreros (4), tras las demás minas (60 %) y en las
  demás trincheras (hasta 5 en total); además, en la sala de las columnas del castillo, en lo alto de tres castillos
  pequeños más y en las cimas de las crestas de arena (6).
- **Conchitas de 1**: arcos de 7 que dibujan el vuelo de las palas (desde el mango en alto o la punta de la hoja, con su
  `TipUp`/`TipForward`) y de los trampolines y las catapultas (su arco del reparto, de lo alto del lanzador a donde cae;
  los que no lo tengan, con su impulso: `LaunchSpeed` y `LaunchPitch` de la catapulta, o `LaunchUp`/`LaunchForward` en
  cm/s si la clase los tiene, o 1600/650 y 1300/1500); rachas
  por los atajos (el hueco estrecho de las filas y, flotando sobre el agua, el de nadar las pozas que cortan un
  corredor), a la entrada de los caminos alternativos (7 hacia el mar), por encima de las pasarelas y por los caminitos
  de palos (hasta 14 por tramo y 110 en total), por el hueco con algas que rodea el castillo con salas (11) y
  serpenteando por los lados de la playa (6-9, la mitad de los tramos de 65 m).
- Nada a menos de 15 m de la salida (lo mismo que el reparto, `TNBeachLayout::ItemsStartX`). Lo del suelo va en arena
  libre, fuera del agua de las pozas (a 1-1,2 m de lo que ocupa cada elemento, salvo lo que va a propósito dentro de un
  peligro); lo que va encima o dentro de algo busca su suelo con trazas contra los elementos.
- La planta de las salas del castillo repite la de `TNBeachDungeonDetail::MakeLayout` (`TN_BeachSandDungeon.cpp`): si
  cambia allí, hay que cambiarla en `DungeonRooms` (`TN_BeachLootShells.cpp`).

### Cofres (`ATN_BeachChest`)

Como el cofre del lobby (`ATN_TreasureChest`), pero de la playa y para la carrera: se tarda en abrir, pero da de lo
mejor para avanzar. Los ponen el reparto (sitios especiales: tras una concha que atrapa, rincones escondidos,
trincheras) y las fortalezas (en su cima), con `ATN_BeachElement::SpawnElement` y el spec `TreasureChest` (huella de
350 cm; categoría de trampa en `TNBeach::CategoryOf`).

- **Dos actores**: `ATN_BeachChest` es el elemento del reparto y solo existe en el servidor (no se replica); al empezar
  crea en su sitio y con su giro el cofre de verdad, `ATN_BeachChestSpot` (hereda de `ATN_BeachSearchSpot`), que es lo que
  se ve, se abre y se replica. Al quitar el elemento (ronda nueva, `ClearElementsAround`, `TN.Beach.Place clear`) se va
  el cofre con lo que soltó y nadie cogió. El frente del cofre mira a su +X. En Preview Round del editor también sale
  (sin guardarse).
- **Aspecto**: el cofre del lobby 2,2 veces más grande (3,4 m de ancho, 2,2 m de fondo y 2,6 m de alto con la tapa),
  variante playera: madera blanqueada, flejes y asas de hierro oxidado, borde, cantoneras y cerradura dorados,
  percebes, algas colgando del borde y cruzando la tapa, una estrella de mar pegada a un lado y arena amontonada al pie.
  Dentro, monedas, gemas, una copa, perlas y una vieira. Las tres mallas (caja, tesoro y tapa) se hacen una vez y las
  comparten todos los cofres. Mientras está por abrir, una **columna de luz dorada** (la de lo que se coge, de 30 m, se
  ve a 450 m) y la luz de dentro que late por la rendija; y las chispitas (desde 50 m) y el anillo de los rebuscables,
  fijo alrededor de su huella (radio 2,07 m; se ve hasta 90 m).
- **Abrirlo**: mantener E **5,5 s** (no hace caso de `tn.Search.Seconds`), con el aro del HUD. La tapa cruje y se
  entreabre a tirones (de 12° a 48°, temblando) con la luz subiendo, y saltan monedas y chispas hacia la tortuga con el
  sonido de rebuscar agudo. Soltar antes cancela (la tapa cae con un «¡clonc!»). Mientras una tortuga lo abre, las demás
  no pueden (no les sale el aviso).
- **Premio** (siempre; no hace caso de `tn.Search.Luck`): «¡puf!», la tapa salta hacia atrás con un chorro de chispas y:
  - el objeto de siempre de los rebuscables, con su saltito hacia quien lo ha abierto (por delante del cofre);
  - **un objeto más** y **seis conchas de puntos** (cuatro de 25, una de 50 y otra de 50 o, el 40 % de las veces, una
    reina de 100: 200 o 250 puntos) que saltan de dentro del cofre en parábolas altas (0,8 s, una cada 0,08 s) y caen en
    corona alrededor, a 1-3 m del borde, dejando libre el frente de quien lo abrió. Buscan el suelo cerca de la cota del
    cofre (en lo alto de una fortaleza no caen al pie).
  - Los objetos, con **los pesos por posición sesgados a lo mejor** (`TNRaceItems::RollLoot` con la fuente `Chest`, según el
    puesto de quien lo abre; ver «Objetos de carrera»): el factor del cofre sube la energía sin fin (×2), la barra llena, el pelícano
    taxi, el coco dorado y el protector solar, y baja la concha trampa y el silbato; la cabezota y el tótem, nunca. Todo lleva
    su brillo (el de lo que se coge).
- **Después**: una vez por ronda. Queda **abierto y vacío** (sin el tesoro de dentro), con un **brillo dorado apagado**;
  la columna de luz se estrecha y se va en 0,8 s.
- **Red**: el estado de la búsqueda es el de `ATN_ProcSearchSpot` (replicado y dormido casi siempre: se despierta al
  empezar y al acabar). Con el resultado se replican los premios y dónde cae cada uno (`Prizes`, `PrizeLandings`): cada
  máquina anima los saltos con el reloj del servidor y los deja en el mismo sitio aunque un premio le llegue a medio
  salto. Relevante a 400 m (lo de serie son 150 m). Quien llega tarde lo ve ya abierto, vacío y con los premios en el
  suelo.
- **Consola**: `TN.Beach.Chest` pone un cofre delante de tu tortuga con el frente hacia ella (en el anfitrión; `TN.Beach.Place
  clear` lo quita; también `TN.Beach.Place TreasureChest`). Registro: `[Playa] cofre ... abierto por ...: <objeto> y, de
  más, <objeto> y 6 conchas (N puntos).`

### Consola, registro y pruebas

- `TN.Beach.Loot 0` (sin botín ni conchas desde la ronda siguiente; `1`, lo normal) y `TN.Beach.Loot.Reroll` (quita el
  botín de la ronda y lo reparte otra vez, en el anfitrión o desde un cliente del PIE). Los de siempre de los
  rebuscables: `tn.Search.Luck 1` (siempre sale algo), `tn.Search.Seconds`, `tn.Search.Show 1` (baliza y huella de
  cada rebuscable) y `TN.Debug.Interaction 1`.
- Registro del servidor en cada ronda: `[Playa] botín de la ronda N: X decorados para rebuscar (de Y candidatos, W sin
  sitio libre para su montículo) y Z objetos sueltos` y `[Playa] conchas de la ronda N: ... de 1, ... de 25, ... de 50 y ... de 100 (dónde)`.
- Probar (`open LVL_BeachRace?BeachSeed=42`, con 1 y con 2 jugadores):
  1. Desde la salida se ven las columnas doradas de los objetos sueltos y, más cerca, sus anillos; las filas de lado a
     lado. Cogerlos (el anillo se apaga con ellos) y usarlos: bola y tinta contra la otra tortuga, concha trampa detrás.
  2. Rebuscar en una silla, un castillo, unos tablones (por el lado largo y por la punta) y una sombrilla (en su pie):
     el anillo dorado está fijo en el montículo de arena (no se mueve al rodearlo) y gira más deprisa mientras se rebusca;
     con dos, a la otra no le sale el aviso mientras una rebusca y el objeto cae en el mismo sitio para las dos.
  3. Conchas: los arcos de una pala (saltar desde el mango) y de un trampolín, la reina de la sala de arriba del castillo
     con salas, la de lo alto del castillo enorme, las de tras el alambre y las rodadas de un paso de quads. Que sumen al
     contador y al recuento.
  4. Siguiente ronda (`TN.Race.WinRound`): desaparece lo que nadie cogió y sale el botín nuevo. `TN.Beach.Loot.Reroll`
     lo vuelve a repartir sin cambiar de ronda.
  5. Cofre (`TN.Beach.Chest`): la columna dorada y la luz que late; mantener E 5,5 s (soltar a los 3 s: se cierra con
     «¡clonc!»); al abrirse, el objeto hacia ti, otro objeto y seis conchas en corona, y queda abierto, vacío y con la luz
     apagada. Con dos: mientras una lo abre, a la otra no le sale el aviso; las dos ven los mismos saltos y los premios en
     el mismo sitio, y las conchas suman a quien las coge.
- Límites: lo que está en el inventario pasa a la ronda siguiente; si `RaceScore` se reinicia o no entre rondas lo decide
  el GameMode. Los arcos de trampolines y catapultas son una estimación mientras sus clases no den `LaunchUp` y
  `LaunchForward`; la concha de las plataformas móviles va sobre lo más alto del elemento al crearse.

## Objetos de carrera (tipo Mario Kart)

Por petición del usuario, la carrera tiene su propio juego de objetos, copiados en parte de las carreras de karts y
adaptados a la playa de las tortugas: turbos, un pelícano que te lleva por delante, un protector solar que hace de
estrella, proyectiles que persiguen, una mina, un rayo y un bumerán. Se reparten **según la posición**: a las de atrás les
tocan los que las hacen remontar y a las de delante, lo que se lanza y lo defensivo.

**Se definen solo desde código**: no hay filas nuevas en `DT_Items` ni hace falta ningún script del editor
(`Scripts/add_race_items.py` no existe porque no hace falta). Cada objeto es un `FTN_InventoryItem` con
`UseType == ETN_ItemUseType::RaceItem` e `ItemId` «Race_<nombre>» (`TNRaceItems::MakeItem`); su malla y su icono se
construyen en ejecución en cada máquina y se ponen a partir del `ItemId` (`TNRaceItems::ResolveVisuals`), así que nada se
replica ni se guarda (un objeto en ejecución no tiene nombre de red: llega nulo y el cliente lo rellena solo).

### Los objetos

| Objeto (`ETNRaceItem`) | Copia de | Qué hace |
|---|---|---|
| **Coco turbo** (`Coconut`) | champiñón | Turbo: la velocidad se multiplica por 2 **sobre la de correr** (800 → 1600 cm/s, aunque no se esprinte) durante **3 s**, con el triple de aceleración para que el empujón sea inmediato. Estela de rayas y arena, luz cálida, «¡fiuuum!» y un empujón al campo de visión de la tortuga local (+16°). |
| **Triple coco** (`TripleCoconut3/2/1`) | triple champiñón | Tres turbos: cada uso gasta uno y el icono pasa de 3 cocos a 2 y a 1 (se cambia el objeto de la mano en su sitio, sin tocar lo guardado). |
| **Coco dorado** (`GoldenCoconut`) | champiñón dorado | Turbo ×2 durante **7 s** con energía sin fin y **sin el cansancio de después**: turbos sin parar. Chispas doradas. |
| **Pelícano taxi** (`PelicanTaxi`) | bala | Un pelícano gigante baja en picado desde atrás, te coge por el caparazón y te lleva volando **por delante de todas** unos 120 m (22 m/s), y te suelta **de pie en arena abierta** por delante. Ver «Pelícano taxi». |
| **Protector solar** (`Sunscreen`) | estrella | **8 s** invulnerable (nada te aturde ni te derriba; los enemigos ni te miran), un 25 % más rápida, con brillo dorado, chispas y una luz que late. **Derriba a las tortugas que toca** (ragdoll de 2 s, empujadas hacia fuera; 2,5 s de respiro por víctima) y **marea 4 s a los enemigos** que toca. |
| **Cangrejo teledirigido** (`HomingCrab`) | concha roja | Un cangrejito rojo de juguete (1,1 m, con antena de mando que parpadea) sale corriendo con saltitos hacia **la tortuga más cercana por delante** (900 → 1600 cm/s, gira 420°/s) y, si la alcanza, la **derriba** 2,2 s. Sin tortuga por delante va a por el **enemigo más cercano por delante** (< 80 m) y lo marea 4 s. Vive 12 s; con el protector puesto, rebota sin efecto. Máximo 10 a la vez. |
| **Gaviota justiciera** (`GullStrike`) | concha azul | Una gaviota gigante (la de las zonas de gaviotas) vuela hasta ponerse sobre **la tortuga que va la primera** (solo si va por delante de quien la lanza) y le suelta una cagada: aviso de sombra negra que crece 1,7 s hasta 2,4 m (antes 3,3); el blanco sigue a la víctima a 250 cm/s (más que andando, menos que corriendo; #636) y, desde justo después de soltarla, cae por la línea que llevaba (antes, 700 hasta el golpe: ronda 4, «Nerf de la gaviota y de su caca») (**andando te pilla; esprintando en línea recta, girando corriendo al soltarla o tirándote en plancha a tiempo, te libras**); si alcanza, derriba 2,6 s y deja la mancha en la arena y el pegote en el caparazón. Sin líder por delante, va a por el enemigo más cercano por delante. Máximo 3 a la vez. |
| **Mina de arena** (`SandMine`) | bob-omb | Mina lanzable hacia donde mira la cámara: vuela con gravedad, rebota una vez y queda quieta; se arma a los 0,9 s (pitido y luz roja que late cada vez más deprisa) y salta cuando se acerca una tortuga (la de quien la lanzó, pasado 1,5 s) o un enemigo: mecha de 0,35 s y explosión que **aturde en bola 3 s** a las tortugas a menos de 5,5 m y **marea 5 s** a los enemigos a menos de 13 m. Explota sola a los 10 s de armada. Máximo 12 a la vez. |
| **Nube de tormenta** (`StormCloud`) | rayo | Una nube negra crece sobre **cada otra tortuga en carrera** (1,1 s de aviso, con sombra y truenos) y les cae un rayo que las **aturde en bola 2,2 s**. Con el protector puesto el rayo cae a su lado sin efecto. Necesita al menos otra víctima. Máximo 2 a la vez. |
| **Disco volador** (`Frisbee`) | bumerán | Sale hacia delante dibujando un arco (26 m, curvado 7 m), gira y **vuelve a la mano** de quien lo lanzó (2,9 s en total), **derribando 1,9 s** a las tortugas (una vez por pasada) y **mareando 4 s** a los enemigos que toca; no golpea a quien lo lanza. Máximo 6 a la vez. |
| **Silbato del sargento** (`Whistle`) | (propio) | **Aturde en área a los enemigos**: todos los que estén a menos de 55 m se marean 5 s con pajaritos (`ApplyHitStun`), sin apuntar. Onda de silbato que se expande por el suelo. |
| **Caja de objetos** (`Box`) | caja «?» | No se lleva: es el pickup del suelo. Cubo de juguete de colores con una «?» que flota y gira; al cogerla sale un objeto sorteado **según el puesto de quien la coge**. |

Los usos que no se pueden hacer (aturdida, en el caparazón, en el pico de un enemigo, en brazos, volando en el pelícano,
carrera parada, sin nadie a quien apuntar, sin sitio delante, demasiados a la vez) suenan «nop» y **el objeto se queda**.
Todo lo decide el servidor y todos ven los efectos; en la carrera nunca se muere: lo que golpea aturde (`TNBeach::StunTurtle`)
o derriba (`TNBeach::KnockDownTurtle`), y a los enemigos los marea `ATN_BeachEnemy::ApplyHitStun`.

**Con la carrera parada nada golpea** (#72): tras «¡TIEMPO!» o «¡TODAS AL AGUA!», en el recuento, en el título del sprint y en el
podio, los objetos lanzados siguen su camino pero no aplican efecto. La regla es una sola, `TNBeachRaceRules::IsRaceLive`
(la usan `ATN_BeachEnemy::IsRaceLive` y `TNBeachRideKit::IsRaceLive`; `Tortunabo.BeachRace.RaceLive`): la mina congela su
reloj, el cangrejo se disuelve y la gaviota cae sin víctimas; el disco (`ServerSweepHits`) y el protector solar
(`ServerStarContacts`) no derriban ni marean, y **la nube de tormenta ya anunciada cae igualmente** (el rayo se ve, como el
picotazo de la gaviota) pero no marea a nadie (`ServerStrike`; decisión de #72).

### Pelícano taxi (`ATN_RacePelicanTaxi`)

- **Línea de tiempo** (desde el uso): 1 s de aproximación (la tortuga queda sujeta y pataleando donde está mientras el
  pelícano baja desde 45 m atrás y 35 m arriba), 1,3 s de subida a la altura de crucero (el suelo más alto del camino +
  26 m), crucero a 22 m/s, 1,5 s de descenso, **suelta a 2,6 m sobre el sitio** (de pie, sin caída larga: se le pone
  `SetFallImmuneUntilLanded` y 3 s de gracia de la tormenta) y 2,5 s de despedida.
- **El sitio** se decide UNA vez en el servidor: 120 m por delante en la dirección de la carrera, recortado para quedar a
  55 m o más del filo (no se salta la meta), y se busca con `TNBeach::FindOpenSandSpot` (arena abierta, llana, con la cápsula
  de pie cabiendo, fuera del agua, de las pozas y de las trincheras); si no hay, se acorta un 20 % hasta 5 veces. Con menos
  de 30 m útiles por delante o sin sitio, el objeto no se usa («nop»).
- **Red**: el plan (víctima, salida, sitio, altura, hora) se replica una vez en un struct; el vuelo del pelícano y el
  punto de la espalda de la tortuga son fórmulas del tiempo del servidor, iguales en todas las máquinas (nada de
  posiciones por fotograma). Deriva de `ATN_BeachEnemy` para reutilizar su sujeción (`BeginHoldTurtle`,
  `PlaceHeldTurtle`, `EndHoldTurtle`, marca de llevada): si la red de seguridad, un rescate o un gusano le quitan la tortuga
  (`ServerReleaseHeldTurtle` → `OnHoldAborted`), el vuelo se aborta y se marcha sin soltarla en ningún sitio. Mientras la lleva,
  `UTN_RaceItemComponent::SetRiding(true)` la hace invulnerable y no puede usar objetos. No se marea ni le da nada.
- Máximo 8 taxis en el mundo y uno por tortuga.

### Invulnerabilidad (protector solar y pelícano)

`TNRaceItems::IsInvulnerable(Turtle)` (cualquier máquina; lo leen `UTN_RaceItemComponent`) es lo que respetan, con una línea
cada uno: `TNBeach::StunTurtle`, `TNBeach::KnockDownTurtle`, `ATortugaCharacter::ApplyKnockdown` (cáscara de plátano, bola
lanzada...) y `ATN_BeachEnemy::CanBeHit` (los enemigos no la eligen como blanco). Los objetos de carrera comprueban además
`TNRaceItems::CanBeHurt` antes de golpear. Lo que la mueve sin golpearla (la patada de la tormenta, la red de seguridad) sigue
funcionando.

### Pesos por posición

El puesto sale de lo que ha avanzado cada tortuga en carrera por la playa (`ATN_BeachRaceGenerator::GetCourseProgress`;
`TNRaceItems::GetRank`): `Norm` = 0 la primera, 1 la última, 0,5 si va sola. Cada objeto tiene tres pesos (primera / a medias
/ última) que se interpolan (`TNRaceItems::PositionWeight`). En el **cofre** se multiplican por un factor que sesga a lo mejor.
«Mín.» es el número de tortugas en carrera sin el cual no sale.

| Objeto | Primera | A medias | Última | Cofre × | Mín. |
|---|---|---|---|---|---|
| Coco turbo | 2,0 | 2,2 | 1,4 | 1,0 | 1 |
| Triple coco | 0 | 0,8 | 1,6 | 1,4 | 1 |
| Coco dorado | 0 | 0,1 | 0,9 | 2,0 | 1 |
| Pelícano taxi | 0 | 0,25 | 2,6 | 1,6 | 1 |
| Protector solar | 0 | 0,5 | 2,0 | 1,4 | 1 |
| Cangrejo teledirigido | 0,7 | 1,5 | 1,0 | 1,0 | 1 |
| Gaviota justiciera | 0 | 0,1 | 1,1 | 1,0 | 2 |
| Mina de arena | 1,8 | 1,2 | 0,5 | 0,8 | 1 |
| Nube de tormenta | 0 | 0,2 | 1,3 | 1,0 | 2 |
| Disco volador | 1,4 | 1,3 | 0,9 | 1,0 | 1 |
| Silbato del sargento | 1,0 | 1,0 | 0,8 | 0,6 | 1 |
| *Energía sin fin* (`SelfStaminaBoost`) | 1,0 | 1,5 | 1,8 | 2,0 | |
| *Barra llena* (`SelfStaminaFull`) | 1,4 | 1,2 | 1,0 | 1,6 | |
| *Bola lanzable* (`Throwable`) | 1,4 | 1,3 | 0,9 | 1,2 | |
| *Tinta de pulpo* (`InkThrower`) | 1,4 | 1,3 | 0,9 | 1,2 | |
| *Concha trampa* (`Conch`) | 1,6 | 1,0 | 0,4 | 0,6 | |
| *Cabezota* (`BigHead`) | 0,3 | 0,3 | 0,3 | 0 | |
| *Tótem* (`Totem`) | 0 | 0 | 0 | 0 | |

(Las filas en cursiva son los objetos de siempre de `DT_Items`, por su uso: `TNRaceItems::PositionWeightForUse`.) Un uso
nuevo de `DT_Items` sale con peso 1. Los triples de 2 y de 1 uso nunca salen del sorteo: son lo que queda del de 3.

### Dónde salen (los tres sitios usan el mismo sorteo, `TNRaceItems::RollLoot`)

- **Cajas de objetos** (los «objetos sueltos» de siempre): en vez de un objeto fijo, una `ATN_RaceItemBox` en cada sitio
  (20-25 sueltas más 3 filas de lado a lado); al cogerla se sortea por el puesto de quien la coge (fuente `Box`). No
  reaparecen durante la ronda. `ATN_RaceItemBox` es un `ATN_PickupInteractableBase` con su fila en el valor por defecto de la
  clase; cada máquina le pone la malla en `BeginPlay`.
- **Rebuscables** (`ATN_BeachSearchSpot`): `PickLoot` (ahora virtual en `ATN_ProcSearchSpot`, con la tortuga que rebusca)
  sortea por el puesto de quien rebusca (fuente `Search`).
- **Cofres** (`ATN_BeachChestSpot`): el objeto que cae hacia quien lo abre y el de más, ambos con la fuente `Chest`
  (factor de la tabla) según el puesto de quien lo abre.
- Los objetos que suelta el lagarto generoso siguen con los pesos de siempre (`TNBeachLoot::RaceWeight`, sin puesto).

### Red (resumen)

- **Servidor con autoridad** en todo: uso (`ATortugaCharacter::ServerUseEquippedItem` → `TNRaceItems::ServerUse`), golpes y
  aturdimientos. Los actores de los objetos (`ATN_RaceItemActor`: siempre relevantes, `Track` replicado a 20-30 Hz y suavizado
  en los clientes, reloj del servidor común) y el pelícano (`ATN_BeachEnemy`, plan replicado una vez) se ven igual en todas.
- **Efectos en la propia tortuga** (`UTN_RaceItemComponent`, componente dinámico replicado que el servidor añade la primera
  vez, como `UTN_BeachStunComponent`): turbo, protector y vuelo. Cada máquina pone el mismo multiplicador de velocidad en
  su `UTN_StaminaComponent` (`SetRaceSpeedMultiplier`) a partir del estado replicado, de modo que el dueño, el servidor y los
  demás usan el mismo `MaxWalkSpeed` (al empezar y acabar el turbo puede haber una pequeña corrección de movimiento de un
  par de fotogramas por la latencia).
- **Sonidos** sintetizados (`UTN_RaceItemSynthComponent`, 21 sonidos, sin archivos) y efectos puntuales locales
  (`ATN_RaceBurstFX`); nada en servidor dedicado.

### Archivos

| Archivo | Qué es |
|---|---|
| `Public/World/Beach/TN_RaceItems.h`, `Private/World/Beach/TN_RaceItems.cpp` | Catálogo (`ETNRaceItem`, `MakeItem`, `ResolveVisuals`), puesto y sorteo por posición, uso (`ServerUse`) e invulnerabilidad |
| `TN_RaceItemComponent.h/.cpp` | Efectos en la tortuga: turbo, protector solar, vuelo; sonidos y silbato para todas las máquinas |
| `TN_RaceItemActor.h/.cpp` | Base de los actores de objetos (red, `Track`, reloj, sonido, final) |
| `TN_RaceItemBox.h/.cpp` | La caja de objetos |
| `TN_RacePelicanTaxi.h/.cpp` | Pelícano taxi |
| `TN_RaceHomingCrab.h/.cpp`, `TN_RaceGullStrike.h/.cpp` | Cangrejo teledirigido y gaviota justiciera |
| `TN_RaceMine.h/.cpp`, `TN_RaceFrisbee.h/.cpp` | Mina de arena y disco volador |
| `TN_RaceStormCloud.h/.cpp`, `TN_RaceBurstFX.h/.cpp` | Nube de tormenta y efectos puntuales (onda del silbato, nubecillas, estrellas) |
| `TN_RaceItemSynth.h`, `Private/World/Beach/TN_RaceItemSynth.cpp` | Los sonidos sintetizados |
| `Private/World/Beach/TN_RaceItemArt.h/.cpp` | Mallas e iconos dibujados en código |
| `Private/World/Beach/TN_RaceItemCommands.cpp` | Comandos de consola (`Docs/Comandos_Prueba.md`) |
| Cambios mínimos en lo existente | `TN_InventoryTypes.h` (`RaceItem`), `TN_InventoryComponent.*` (resolver malla e icono; `TryReplaceEquippedItem`), `TN_PickupInteractableBase.cpp` (idem), `TortugaCharacter_Interaction.cpp` (rama `RaceItem`), `TortugaCharacter_Knockdown.cpp` y `TN_BeachStun.cpp` (invulnerabilidad), `TN_BeachEnemy.*` (`CanBeHit` y `GetMaxHoldSeconds`, virtual: el seguro de la sujeción es de 6 s y el pelícano taxi lo alarga a 20 s), `TN_StaminaComponent.*` (`SetRaceSpeedMultiplier`), `TN_ProcSearchSpot.*` (`PickLoot` virtual), `TN_BeachLoot.*` y `TN_BeachChest.*` (sorteo por puesto, cajas de objetos) |

### Probar

Comandos en `Docs/Comandos_Prueba.md`, «Objetos de carrera». Con una tortuga: `TN.Race.ItemUse Coconut` (los 3 s a doble
velocidad, con la vista más abierta), `Sunscreen` (atravesar un enemigo y otra tortuga), `PelicanTaxi` (todo el vuelo y que
la deja de pie sin caer al agua; probar cerca de la meta: `TN.Beach.Go acantilado` → «nop»), `SandMine`, `Frisbee`, `Whistle`
con enemigos cerca y `HomingCrab`. Con anfitrión y cliente: `GullStrike` y `StormCloud` (`TN.Race.ItemUse GullStrike 1`), y
mirar que las dos ventanas ven lo mismo; `TN.Race.ItemBox 4` y `TN.Race.ItemRank` para ver qué toca en cada puesto.

### Límites conocidos

- Todo este código se escribió sin compilar: puede haber pequeños errores de compilación en la primera vez.
- Las cajas de objetos no reaparecen en la ronda y no dicen lo que dan (como en las carreras de karts).
- `M_ProcFXHard` (material duro de las sombras de aviso, lo crea `Scripts/create_poop_decal.py`) no es imprescindible: sin
  él, la onda del silbato, el rayo y el aviso de la gaviota usan `M_ProcFXSoft`.
- El turbo y el protector cambian `MaxWalkSpeed` en cada máquina por su cuenta (como el resto de límites de velocidad):
  con mucha latencia el dueño puede notar una corrección al empezar o acabar.
- Las minas lanzadas solo miran el suelo, no chocan con el decorado.
- Los efectos (turbo, protector solar) y los actores lanzados se acaban solos al cambiar la ronda del generador; el pelícano
  dura como mucho 40 s.
- Si la tortuga se mete en el caparazón en pleno vuelo del pelícano, la suelta y cae en bola desde la altura del vuelo (sin
  penalización de caída larga, pero no aterriza de pie). Bloquear ese gesto mientras vuela queda anotado (`UTN_ShellComponent`).
- Los abortos del pelícano (rescate, red de seguridad, gusano) la sueltan donde estén.

## Recuento, campeón y podio (`UI/Race/`)

| Archivo | Qué es |
|---|---|
| `UI/Race/TN_RaceScreens.*` | `UTN_RaceScreensSubsystem`: subsistema de mundo en cada máquina con jugador; mira el estado replicado y pone o quita las pantallas (sin RPC ni cambios en el PlayerController); consola de vista previa |
| `UI/Race/TN_RaceFinishCountdownWidget.*` | `UTN_RaceFinishCountdownWidget`: cuenta atrás de 10 s tras la primera en el agua y «¡TIEMPO!» |
| `UI/Race/TN_RaceTallyWidget.*` | `UTN_RaceTallyWidget`: recuento de conchas tras cada ronda (con medias conchas) |
| `UI/Race/TN_RaceSprintWidget.*` | `UTN_RaceSprintWidget`: título «¡SPRINT FINAL!» con las finalistas |
| `UI/Race/TN_RaceCueSynthComponent.*` | `UTN_RaceCueSynthComponent`: «¡toc!» de la cuenta, silbato, fanfarria, «¡pum!» y trombón triste, sintetizados en 2D |
| `UI/Race/TN_RaceChampionWidget.*` | `UTN_RaceChampionWidget`: pantalla del campeón (botones a la izquierda, podio a la derecha) |
| `UI/Race/TN_RacePodiumStage.*` | `ATN_RacePodiumStage`: el podio en 3D, capturado a una textura |
| `UI/Race/TN_RaceArrivalWidget.*` | `UTN_RaceArrivalWidget`: «Has quedado X.º» con su premio y su mensaje, encima de la cáscara oscura (ronda 4) |
| `UI/Race/TN_RaceRoundIntroWidget.*` | `UTN_RaceRoundIntroWidget`: «RONDA N» o «SPRINT FINAL» y el 3, 2, 1 entre rondas, encima de la cáscara oscura (ronda 4) |
| `UI/HUD/TN_GhostHatchWidget.*` | la cáscara oscura del cooperativo; en modo carrera (`ShowCurtain`), la transición de la llegada y del paso entre rondas |
| `Private/UI/Race/TN_RaceArt.h`, `TN_RaceUIKit.h` | arte en código (fondos, huecos, corona, cielo, iconos, caras con el color de piel) y piezas de UMG |
| `Private/UI/Race/TN_RaceArrivalArt.h` | los premios de la pantalla del puesto: coronas de oro, plata y bronce, cubo, media concha rota, flotador pinchado, calcetín y alga |
| `Player/TN_TurtleAnimInstance.*` | poses Trofeo, Decepcionada y Pataleta (`SetCelebration`) y la zambullida del acantilado |

Todo en el estilo del HUD Tortunavy (`TN_HUDArt.h`, `TN_HUDFaces.h`, `TN_HUDStyle.h`) y por encima de él (cuenta atrás
15, recuento 20, campeón 21 y sprint 22: por encima del HUD, 4-10, y por debajo de las ruedas, 30; la cáscara oscura de la
llegada y del paso entre rondas, 50, y lo que va encima, 51). Sonidos: el «pom» y el «¡plin!» sintetizados de las
conchas de puntos (`UTN_ScoreShellSynthComponent`, en 2D en el PlayerController).

### Cuenta atrás (`Racing` con `FinishCountdown`)

- Sin tapar la carrera (no coge ratón ni teclado): arriba, la cinta «¡La primera ya está en el agua!» con su cara y la
  etiqueta «Concha para X · media concha para quien llegue antes del final»; en medio, el número (10… 1) en un medallón
  azul marino que late con cada número, se pone dorado a los 3 s y coral en el último y tiembla al final; debajo, lo que
  te toca: «¡Corre! Media concha si llegas», «¡Concha entera para ti!» o «¡Media concha para ti!» (si solo miras, nada).
- Sonido (`UTN_RaceCueSynthComponent`, en 2D): «¡toc!» de caja china cada segundo, cada medio desde los 5 s y cada cuarto
  desde los 2,5 s (más agudo en los 3 últimos). Al acabar, «¡TIEMPO!» (o «¡TODAS AL AGUA!») con el silbato del árbitro.
  Se va con un fundido cuando sale el recuento.

### Recuento (`RoundResults`)

- A pantalla completa: el mar azul marino con rayos de luz y burbujas que suben y, abajo, la orilla de arena. Arriba,
  la cinta «RONDA N» y un cartel que empieza en «Recuento de conchas» y pasa a «¡Concha para X!» (con medias: «¡Concha
  para X! Media para Y.» o «Medias para Y y Z.»), luego «¡X gana la partida!» o «¡Empate en lo más alto entre X y Y!
  ¡Sprint final!»; si nadie llegó, «¡Nadie ha llegado al agua! Esta vez no hay concha.». Abajo, «Siguiente ronda en N»
  (`PhaseSecondsLeft`), «¡Al podio en N!» o «¡Sprint final en N!».
- **Medias conchas**: cada hueco guarda dos medias; la media es la concha reina partida en diagonal con el corte en
  zigzag (`TNRaceArt::HalfShell`; las dos mitades encajan). Las que ya tenía salen con su «pom» (la media, más bajito);
  tras caer la entera de la ganadora, las medias de la cuenta atrás saltan una a una (cada 0,35 s) a su hueco con un
  «plin» pequeño y destellos, y la cara de quien la gana celebra con un salto más corto. Si completa una concha, la otra
  mitad llega desde abajo a la derecha y encaja, y queda entera con un rebote. Si la entera cae sobre una media, la
  completa y la otra mitad salta al hueco siguiente.
- Una columna por jugador, siempre en el orden de entrada a la partida: los huecos de concha (`RoundTarget`, 3) en
  zigzag de abajo arriba unidos por una cuerda, con la corona apagada en lo alto; debajo, su cara del HUD con su color
  de piel (`TNRaceArt::TurtleFaceFor` gira el verde de la piel y conserva luces, pecas y el filo) en un aro del color de
  su caparazón (o de su piel) y su nombre en una etiqueta de arena (dorada y con «TÚ» la tuya).
- Guion (segundos desde que sale): 0,1-0,5 entran las columnas con rebote; desde 0,55 aparecen una a una las conchas
  que ya tenía cada uno, con un «pom» que sube por la escala; hacia 1,3 nace en el centro la concha de la ronda (la
  reina de las de puntos: rosa y violeta con estrella) con destellos y un «plin» pequeño; 0,55 s después vuela en arco
  con estela hasta su hueco (0,65 s) y cae con «¡plin!» (el de la grande; el de la reina si corona), rebote y destellos.
  La cara del ganador pasa a ojos de estrella y salta con un brillo dorado detrás; si la concha completa el objetivo, la
  corona baja de lo alto a su cabeza. Si nadie ganó: «pom… pom» que bajan y todas las caras mareadas.
- Las conchas de la ronda llegan desde las que cada uno tenía al empezarla: el subsistema apunta `RaceShellHalves`
  mientras se prepara y en los 4 primeros segundos de carrera, así que da igual que llegue por red antes que la fase. Si
  `RoundWinner` llega tarde, la columna se corrige mientras la concha no haya salido volando. La campeona o el empate se
  deciden igual que en el servidor (`DecideVerdict`): la corona baja a la única en lo más alto con tres conchas o más; si
  hay empate ahí, las empatadas brillan en coral y el cartel anuncia el sprint.

### Sprint final (`SprintIntro`)

- A pantalla completa: fondo azul marino con rayos dorados y corales que giran, «¡SPRINT FINAL!» que entra de golpe y se
  mece, la cinta «¡Empate a 3 conchas! Solo corren las finalistas, desde la mitad de la playa: la primera en el agua se
  lleva la partida.», las caras de las finalistas (su piel, su aro y su nombre) que entran una a una de un «¡pum!» con un
  «VS» entre ellas, y confeti y arena cayendo. Suena la fanfarria sintetizada (metales «¡ta-ta-ta-taaan!», redoble,
  timbal y platillo). Abajo, «¡Tú corres!…» o «Tú lo miras de fantasma…» y «El sprint empieza en N».
- Tras el sprint, la pantalla del campeón sale directamente (sin el recuento de la concha que corona) y su cartel dice
  «¡Gana el sprint final y se lleva la partida!».

### Campeón (`Champion`)

- Tras el recuento de la tercera concha (7 s), o tras un recuento con la concha que corona si se salta directamente al
  campeón (`TN.Race.Champion`), entra con un fundido mientras el recuento se va por debajo. Tapa toda la pantalla,
  también el panel de resultados del HUD de siempre («Volviendo al lobby en: 99» de `CountdownValue`).
- Izquierda, sobre un panel azul marino que se funde con el fondo: la cinta «¡CAMPEONA DE LA PLAYA!», el cartel con su
  cara (su piel, corona, saltando), su nombre y sus conchas (aparecen con «pom») y los botones **Volver a jugar**,
  **Cambiar de modo** y **Salir** (etiquetas de arena con icono; se inflan bajo el ratón con un «pom» suave y suenan con
  «plin» al pulsar). Solo el anfitrión (`ATN_BeachRaceGameMode::CanLocalPlayerChoose`) tiene los dos primeros
  encendidos; los demás los ven apagados con el candado «Volver a jugar o cambiar de modo lo decide el anfitrión.» y
  pueden salir. Los botones solo piden: `ATN_BeachRaceGameMode::RequestChampionChoice(this, PlayAgain | ChangeMode |
  Quit)`; tras elegir, «¡Allá vamos!» o «¡Hasta la próxima!» y los botones se apagan. Mientras se ve, ratón a la vista
  (`FInputModeGameAndUI`); al quitarse, `FInputModeGameOnly`.
- Derecha, de fondo animado: el podio en 3D sobre un cielo pintado (degradado, sol que late y nubes que pasan), con
  «1.º X», «2.º Y» y «3.º Z» (colores de medalla) encima de cada tortuga y confeti que cae solo sobre el podio.
- Música: a los 2,2 s de la fase suena para todos la de victoria (`UTN_MatchMusicSubsystem::DebugPlayTrack(Victory)`).
  Antes, en `Results`, el director de la música ya ha puesto victoria a la campeona y derrota a las demás (lo fija a los
  1,6 s y no lo vuelve a tocar con `CountdownValue` = 99); al elegir, el director la funde como siempre.

### El podio (`ATN_RacePodiumStage`)

- **Decisión: escena capturada, no la cámara del jugador.** Un escenario 3D de verdad a 1,5 km sobre el mapa, capturado
  con `SceneCapture2D` (1600 × 900, `SCS_SceneColorHDR`, solo sus componentes) a un render target que la pantalla pinta
  con `M_UI_Preview` (el del escaparate de la tienda: el alfa es la cobertura, así que el cielo es el de la interfaz). No
  toca el `PlayerCameraManager` ni los peones (el GameMode los tiene congelados), se ve igual en todas las máquinas y en
  cualquier mapa (vista previa en el lobby) y deja el podio a la derecha de los botones sin mover la vista del juego.
- Local (sin réplica), visible solo en su captura y con el canal de luz 2 (ni el sol del nivel ni el escaparate de la
  tienda, que usa el 1): sol direccional cálido sin sombras, principal con sombras, relleno frío, contraluz y una luz
  rosa que late junto a la concha. Solo se captura y se anima mientras se ve la pantalla (`SetLive`).
- Todo a `TNBeach::Scale` (28 ×): 1.º **vaso de plástico** rojo boca abajo de 6 cm (1,68 m; aros en relieve y el borde
  blanco enrollado); 2.º **caja de zumo** de 10,5 × 6,3 cm tumbada y aplastada a 3,4 cm (0,95 m; abollada, una esquina
  chafada, naranja con franja blanca, una naranja dibujada y la **pajita** doblada a rayas); 3.º **chancla** de
  26 × 9,5 cm con 1,8 cm de suela (50 cm; el talón hacia la cámara y la tira rosa en Y detrás de la tortuga sentada).
  Alrededor, un **tapón** azul de 3 cm con estrías, una **lata** roja tumbada y medio enterrada, conchas, una estrella de
  mar y piedrecitas; la orilla a 15 m con espuma que va y viene, el mar hasta el horizonte (turquesa → azul hondo) y dos
  islitas a 2,5-3 km con selva y palmeras de 280 m (10 m reales). Mallas estáticas construidas en ejecución
  (`TNProcRuntimeMesh`, `M_CosmeticVertexColor`: el alfa del vértice es el brillo metálico).
- Tortugas: `TotugaDemo_Rig` a escala 2,5 con su aspecto (`UTN_CosmeticLook::ApplyLook`: casco, caparazón, piel y ojos
  de sus cosméticos, de `Podium`) y `UTN_TurtleAnimInstance` sin personaje: **Trofeo** la primera (la concha trofeo va
  entre sus manos, huesos `LeftHand` y `RightHand`, mirando a la cámara y meciéndose), **Decepcionada** la segunda y
  **Pataleta** la tercera, sentada en la chancla. La cara de `M_TurtleBody` acompaña: la primera, sonrisa enorme,
  colorete y un «>_<» de gusto en cada vuelta; la segunda, párpados caídos y boca pequeña que se abre en el suspiro; la
  tercera, ojos apretados, colorete rojo y boca gritando. Parpadean de vez en cuando. Con menos de tres jugadores, los
  puestos que faltan se quedan vacíos.
- Encuadre: cámara a 21,5 m (`TN.Race.PodiumDistance`, en cm), FOV 32° (`TN.Race.PodiumFOV`), girada para que el podio
  quede al 62 % del ancho. Exposición de la captura en pantalla: `TN.Race.PodiumExposure` (1,5).

### Poses nuevas y zambullida (`UTN_TurtleAnimInstance`)

- `SetCelebration(ETNTurtleCelebration)` (`None`, `Trophy`, `Disappointed`, `Tantrum`; también desde Blueprint),
  `GetCelebration` y `GetCelebrationTime`: una capa encima de todo, en espacio de malla sobre la postura en T, con un
  peso que entra y sale suave (al cambiar de una a otra, la anterior sale antes de que entre la nueva). Vale con
  personaje o sin él. Bucles exactos, como un GIF: Trofeo 1,6 s, Decepcionada 3,2 s y Pataleta 1,2 s (detalle en
  `Docs/Animacion_Tortuga.md`).
- **Zambullida de la meta**: si la tortuga despega o empieza a caer (en los primeros 0,35 s de la caída) dentro de
  `ATN_BeachRaceGenerator::IsCliffJumpZone` (la repisa final y el vacío sobre el agua), se pone de cabeza: cuerpo
  estirado, brazos por encima de la cabeza con las manos juntas y piernas juntas con las puntas de los pies; el cuerpo
  gira sobre la cadera siguiendo la trayectoria (tumbada en lo alto del salto, casi vertical, 165°, al caer deprisa) y
  entra así en el agua. Si la caída entra en la zona más tarde (lanzada desde más atrás), empieza igual en cuanto cae a
  más de 10 m/s (`CliffDiveLateFallSpeed`; un salto que vuelve a la repisa no llega a tanto). Se acaba al aterrizar o al
  nadar (y con el panzazo, el caparazón, el derribo o si la llevan). Cosmética y local en cada máquina a partir del
  movimiento replicado, sin RPC; fuera de la playa no hay generador y nunca pasa. `IsCliffDiving()` lo dice. Ya no la
  corta la bola de las caídas largas: el GameMode hace inmune esa caída (ver «Salto final al agua»).

### Probar sin jugar

En cualquier mapa y solo en la máquina que lo escribe:

- `TN.Race.Tally [ganador] [jugadores] [final] [medias]`: recuento con tu tortuga (columna 0) y otras de mentira (Coral,
  Bruma, Perla…, con pieles, caparazones, cascos y ojos del catálogo) y medias conchas de mentira. `ganador` es la
  columna a la que vuela la concha entera (-1 = nadie llega al agua; 0 por defecto), `jugadores` de 1 a 6 (4), `final 1`
  hace que la concha corone (baja la corona) y que después salga la pantalla del campeón, y `medias` cuántas columnas
  siguientes ganan media concha (1). Sin `final` se cierra sola a los 8 s.
- `TN.Race.CountdownPreview`: la cuenta atrás de 10 s con sus «¡toc!» y el «¡TIEMPO!» con silbato.
- `TN.Race.SprintPreview [finalistas]` (2-6): el título del sprint final con la fanfarria; se cierra solo a los 7 s.
- `TN.Race.Podium [jugadores]` (1-3): la pantalla del campeón con el podio y la música; cualquier botón la cierra.
- `TN.Race.ArrivalPreview [puesto] [sprint]` (1-8; `sprint 1` pone la cinta «SPRINT FINAL»): la llegada al agua: la
  cáscara oscura se cierra, «Has quedado X.º» con su premio, su mensaje (uno al azar cada vez) y su sonido, y se rompe.
- `TN.Race.RoundPreview [ronda] [sprint]`: el paso entre rondas: la cáscara, «RONDA N» (o «SPRINT FINAL» con `sprint 1`) con
  su frase (desde la ronda 4, la de la bola de partido), «Colocando la playa…», tres «pum» con 3, 2, 1 y se rompe.
- `TN.Race.PreviewOff`: cierra la vista previa.
- En partida: `TN.Race.WinRound [jugador] [puesto]` (llegada: la pantalla del puesto, cuenta atrás y luego recuento),
  `TN.Race.NextRound` (cierra la ronda o se salta el recuento: paso entre rondas), `TN.Race.Sprint` (sprint final) y
  `TN.Race.Champion` (recuento con la concha que corona y luego el podio).
- Zambullida: en `LVL_BeachRace`, saltar desde la repisa del acantilado de la meta (sin bola: ver «Salto final al agua»).
