# Tienda y probador del lobby

Sistema real de cosméticos de Tortunavy: la tienda de Don Tortugo (catálogo y compra) y los probadores de botella
(elegir lo desbloqueado y ponérselo). Sustituye al catálogo de «pulsar y aplicar» y a las estatuas de prueba de
`LVL_HQ` (`BP_SkinStatue`, `BP_HatStatue`), que quedan como prototipos.

## Flujo

1. **Tienda** (`ATN_ShopKeeper`). Al hablar con el tendero se abre `UTN_ShopWidget` en el cliente que interactúa:
   a la izquierda, tu tortuga posando y girando en una peana (arrastrar con el ratón la gira); a la derecha, el
   tendero habla en su bocadillo, cinco pestañas (cascos, caparazones, colores, ojos y buggy) y el catálogo con miniaturas
   y precio (las de ojos, con primer plano de la cara).
   Al elegir algo, la tortuga se lo prueba encima de lo que lleva y saluda. **Comprar** desbloquea el cosmético
   con los puntos de final de partida (cascos gratis; skins de 100 a 400 puntos según la rareza, #873) y lo guarda en el
   `SaveGame` local; el servidor recibe la lista de desbloqueados
   (`ServerSyncUnlockedHelmets` / `ServerSyncUnlockedSkins`).
2. **Probador** (`ATN_ChangingBooth`): botella de cristal de mar de unos 3 m, puesta boca abajo.
   - El techo es el culo de una botella de refresco de litro y medio, con sus cinco lóbulos.
   - Tiene una etiqueta de papel naranja y crema que la rodea, con «PROBADOR» impreso siguiendo la curva.
   - Por dentro tiene suelo de tablas con una alfombrilla.
   - La puerta redonda es el tapón, una chapa de corona roja con estrella.

   Al entrar, el servidor mete a la
   tortuga dentro y cierra la chapa (`Occupant` replicado: todos la ven cerrarse y la botella se menea mientras se
   cambia). El cliente aleja la cámara
   a la de la botella y abre `UTN_BoothWidget`: cuatro filas (casco, caparazón, color y ojos) que se cambian con las
   flechas; solo salen los cosméticos desbloqueados. **¡Listo!** pide al servidor los cambios (`RequestEquipHelmet`,
   `RequestEquipShell`, `RequestEquipSkin`, `RequestEquipEyes`); **Cancelar** sale sin cambios. En los dos casos la puerta se abre y la
   tortuga sale de un saltito (el servidor y el cliente dueño la lanzan a la vez).
3. **Replicación del aspecto.** El `PlayerState` replica `EquippedHelmetId`, `EquippedShellId`, `EquippedSkinId` y
   `EquippedEyesId`; cada `OnRep` llama al personaje, que guarda el conjunto (`FTN_TurtleLook`) y lo aplica entero con
   `UTN_CosmeticLook`. Los ojos se guardan también en el `SaveGame` (`UMP_GameInstance::EquipEyes`) y el servidor los
   valida contra `DT_Skins` y los desbloqueos (`ServerSetEquippedEyes`).

Teclado y mando: flechas o WASD para elegir, Q/E (o gatillos) para las pestañas, Intro para comprar o aceptar y
Escape para salir. El menú bloquea el control del juego mientras está abierto (`FInputModeUIOnly`).

Al acercarse a cualquier interactuable (tendero, probador, selectores, objetos), el HUD enseña abajo un cartel con la
tecla de la acción de interactuar (la que tenga asignada en Enhanced Input) y el texto del interactuable
(`UTN_RunHUDWidget::TickPrompt`). Consola de pruebas: `TNShop` abre la tienda y `TNBooth` entra en el probador libre
más cercano.

## Clases

| Clase | Archivo | Qué hace |
|---|---|---|
| `UTN_CosmeticLook` | `Core/TN_CosmeticLook` | Viste a una tortuga con un `FTN_TurtleLook`: casco en la cabeza, color y caparazón. Lo usan el personaje, el tendero y las vistas previas. |
| `ATN_CosmeticPreview` | `Lobby/TN_CosmeticPreview` | Escaparate local (no se replica): tortuga en una peana con luces de estudio y cámara que pinta en una textura. Hace las miniaturas del catálogo. |
| `ATN_ShopKeeper` | `Lobby/TN_ShopKeeper` | Tendero con su conjunto; puesto con mostrador, toldo de rayas con volante, cartel de pie y guirnalda de banderines. Se gira hacia el jugador local y le saluda. |
| `ATN_ChangingBooth` | `Lobby/TN_ChangingBooth` | Media botella boca abajo con la chapa de puerta; mete y saca a la tortuga. |
| `UTN_ShopWidget`, `UTN_BoothWidget` | `UI/Shop/TN_ShopWidgets` | Pantallas de la tienda y del probador, hechas en código con el estilo del HUD. |

En `LVL_Lobby` la tienda y los cuatro probadores ya están colocados en el castillo (ver `Docs/Lobby_Castillo.md`).
- **Tienda**: pegada a la muralla, con un mostrador de 5,2 m y a escala 1 (`StallScale`: por encima, el mostrador tapa
  al tendero). Detrás tiene una estantería con lo que se vende (botes de pintura, caparazones, tarros de ojos y cascos)
  y a los lados, perchero, barril, cajas, cofre y farol.
- **Editor**: tienda y probadores se construyen también en `OnConstruction`, así que se ven sin darle al Play.
- **Interacción**: la distancia se mide desde `GetInteractionPoint()`: delante del mostrador en la tienda y delante de
  la chapa en el probador.

### Música

- **Tienda.** La radio del puesto (`Radio`, un `UTN_MusicSynthComponent`) toca la pista `Shop` en 3D desde el puesto,
  a 1,8 m de altura (`RadioVolume` 0,8).
- **Menús.** Al abrir la tienda o el probador, el menú toca en 2D la pista `Shop` o la `Booth`. Mientras está abierto,
  las radios de las tiendas bajan al 15 % (`ATN_ShopKeeper::SetRadiosDucked`). Al cerrar, todo vuelve a su volumen.

### Colocación automática

`ATN_HQGameMode::SpawnLobbyShops` coloca la tienda y los probadores si el nivel no los trae puestos. Busca primero
actores con la etiqueta `TN_ShopAnchor` o `TN_BoothAnchor`; si no hay, usa la maqueta de `LVL_Lobby`: el tendero es la
tortuga grande (`SkeletalMeshActor` con `TotugaDemo_Rig` y escala ≥ 3,2) más cercana a la carpa, y los probadores son
las botellas `BP_VestidorBotella_*` (la puerta mira a la puerta de prueba `BP_ShellDoor` si hay una al lado y, si no,
al `PlayerStart`). Las piezas de maqueta sustituidas se esconden en cada máquina. Para colocarlos a mano basta con
arrastrar `TN_ShopKeeper` o `TN_ChangingBooth` al nivel.

## Contenido

`Scripts/build_cosmetics.py` (se ejecuta dentro del editor) crea o rehace:

- `/Game/Cosmetics/Materials/M_CosmeticVertexColor`: cascos y mallas del puesto (color de vértice; el alfa es el brillo metálico).
- `/Game/Cosmetics/Materials/M_TurtleBody`: cuerpo y caparazón de la tortuga de demo (ver abajo).
- `/Game/Cosmetics/Materials/M_TurtleHelmetSlot`: la ranura del casco de serie; recorta el casco (con uno de la tienda)
  y esconde la lengua rígida de la malla.
- `/Game/Cosmetics/Materials/M_TurtleFaceParts`: la lengua y las gotas de sudor procedurales de
  `UTN_TurtleFaceComponent` (color de vértice; su alfa es lo mojado: más brillo; un poco de luz propia).
- `/Game/UI/Shop/M_UI_Preview`: pinta en la UI las capturas del escaparate.
- `/Game/Cosmetics/Helmets/SM_Helmet_*`: los doce cascos, modelados en `Scripts/cosmetics_meshes.py` (Python puro)
  sobre la coronilla de la tortuga: sombrero de paja, tricornio, corona, gorra de capitán, gorro de marinero, gorro de
  fiesta, gorro de hélice, flor tropical, estrella de mar, cangrejo, sombrero medusa y aureola.
- Las filas de `DT_Helmets` y `DT_Skins` (diez caparazones, doce colores y nueve ojos), con precio 0 y la frase del
  tendero. Todas las filas de `DT_Skins` llevan la columna `EyeStyle` (hay que volver a ejecutar el script después de
  compilar, para que la tabla tenga la columna nueva).

### La tortuga de demo

`TotugaDemo_Rig` tiene dos ranuras de material: `lambert2` (casco rojo de serie y lengua) y `lambert4` (cuerpo, ojos
y caparazón juntos). Por eso el color y el caparazón se pintan con un solo material, `M_TurtleBody`, que separa las
zonas por la posición local antes del skinning (unidades de la malla; mira a +Y y mide unos 53 de alto). Las ranuras
se buscan por nombre (Ajustes del proyecto > Tortunavy > Arte > Tortuga > Cosméticos): una malla de Arte sin ellas se
queda con sus materiales, sin colores de la tienda ni cara animada (`Docs/Arte_Assets.md`, §10).

- **Caparazón:** detrás del torso (`y` menor que un frente que va de 0,4 a 3,2 según la altura), entre `z` 22,3 y 38,4
  y con `|x|` < 7,2. Coincide con la pieza del caparazón de la malla.
- **Barriga:** delante del torso, entre `z` 21 y 36,6.
- **Lengua** (en `M_TurtleHelmetSlot`): `|x|` < 2,3, `y` > 9,6 y `z` entre 39,8 y 43,4; el resto de la ranura es el
  casco de serie y sus correas. Es rígida (no tiene hueso) y sale recta hacia delante; `UTN_CosmeticLook` pone siempre
  una instancia de este material en la ranura con `HideTongue` = 1 (y `HideHelmet` = 1 si se lleva un casco de la
  tienda), así que el casco de serie se pinta con `HelmetColor` (el difuso de `M_TortugaDemo`) y la lengua del jugador
  es la procedural de `UTN_TurtleFaceComponent` (ver `Docs/Animacion_Tortuga.md`).
- **Boca:** la malla tiene un hueco bajo la nariz (`|x|` < 1,7; `z` 41,3-43,9; el fondo en `y` ~ 10 y el borde en
  `y` ~ 13,7) por donde salía la lengua. `M_TurtleBody` pinta alrededor, en el plano de la cara (x, z), una boca de
  dibujo: `MouthOpen` (0 casi cerrada, 1 abierta del todo; de serie 0,3) y `MouthSmile` (1 sonrisa con el borde de
  arriba recto, justo bajo la nariz; 0 óvalo) dan la forma; dentro va granate `#6B1B26` con la lengua rosa al fondo,
  alrededor un filo oscuro (la piel al 25 %) y lo del hueco que queda fuera de la forma, piel en sombra.
- **Mofletes:** `FaceBlush` pinta colorete rosa `#FF8FA3` bajo los ojos, centrado en x = ±5 y z = 41,1 (delante de la cara).
- **Ojos:** las dos esferas de la malla, centradas en (±4,47; 8,46; 46,06) con radio ≤ 4,12; solo se pinta el casquete
  que asoma (dirección (±0,66; 0,62; 0,42)). La mirada va hacia (0,25; 0,93; 0,27) normalizado y el iris mide 0,56 del
  radio. Parámetros: `EyeStyle`, `EyeColor`, `EyeColor2`, `EyeGlow`, `EyeBlink` (0 abiertos, 1 cerrados: el párpado
  baja con su pestaña) y `EyeDizzy` (espiral de noqueada). `UTN_CosmeticLook` pone siempre `M_TurtleBody` en el cuerpo:
  con el material original los ojos salían del color de la piel.
- **Cansancio de los ojos** (como las caras del HUD): `EyeTired` 0,5 = cansada (el párpado tapa el tercio de arriba) y
  1 = jadeando (algo más de la mitad); el párpado cae un poco más por fuera y la mirada baja (pupilas, iris y formas
  se desplazan hacia abajo), así que sirve igual para todos los tipos de ojo. `EyeSqueeze` = 1 cierra los ojos
  apretados «>_<»: párpado cerrado con un galón de tinta que apunta a la nariz. Los anima `UTN_TurtleFaceComponent`.

### Tipos de ojo

`ETNEyeStyle`: clásicos (los de serie), iris de color, pupila de estrella, de corazón, de dibujo, espiral, de gato y
galaxia (con luz propia). Filas `Eyes_*` de `DT_Skins`: azul mar, esmeralda, miel, gato, estrella, corazón, dibujo,
hipnóticos y galaxia. El personaje parpadea y se marea con `SetEyeState` (ver `Docs/Animacion_Tortuga.md`).

El casco va en el socket `Sombrero` de la malla si existe; si no, en el hueso `Head`, colocado en la coronilla de la
postura de referencia (0; 5,5; 51) para que siga la animación de la cabeza. Con la malla unificada de cinco ranuras
(barriga, brillo de ojos, ojos y boca, piel, caparazón) se usan los materiales por ranura de las filas.

## Añadir un cosmético

- **Casco:** añadir una receta en `Scripts/cosmetics_meshes.py` (lista `HELMETS`) y volver a ejecutar
  `Scripts/build_cosmetics.py`; o crear una fila en `DT_Helmets` con una malla de arte (`DisplayMesh`) y ajustar
  `MeshOffset`, `MeshRotation` y `MeshScale` desde la coronilla.
- **Caparazón o color:** añadir una fila a `SHELLS` o `BODIES` en `Scripts/cosmetics_skins.py`, con su rareza (o directamente en
  `DT_Skins`) con `Category`, `Color`, `Color2`, `Pattern`, `PatternScale`, `Shine` y `Glow`.
- **Ojos:** añadir una fila a `EYES` en `Scripts/cosmetics_skins.py` con el tipo (`EyeStyle`), el color del iris o la
  pupila, el segundo color y el brillo. Un tipo nuevo necesita su rama en el HLSL de ojos del script y su valor en
  `ETNEyeStyle`.

Las filas de `DT_Skins` salen de `Scripts/cosmetics_skins.py` (`Scripts/build_points_economy.py` las vuelca sin tocar
`DT_Helmets`). Cada skin lleva su rareza (`Rarity`) y su precio (`Price`).

## Puntos, precios y caja sorpresa (#873)

- **Saldo:** `UTN_CosmeticSaveGame::ShopPoints` (perfil v2). Sube con los puntos de final de partida que calcula el
  servidor en Results (`ATN_CoopGameState::AwardEndScores`, fórmula en `TN_CoopScore.h`) y que cada máquina suma a su
  perfil (`UMP_GameInstance::AddCoopScore`). Al migrar un perfil v1, el saldo empieza con los puntos ya ganados
  (`AccumulatedCoopScore`); las conchas viejas (`AccumulatedRaceScore`) no se convierten.
- **Precios sueltos:** común 100, rara 200, épica 400 (`PRICE_BY_RARITY` en `Scripts/cosmetics_skins.py`).
- **Caja sorpresa:** botón «CAJA SORPRESA» o tecla C. Cobra 150 puntos, elige la rareza con sus pesos (70/25/5) entre
  las que tienen filas y luego una skin de esa rareza; si ya la tenías, devuelve el 50 % (75 puntos). La carta gira
  pasando skins y se para en la que sale (`UTN_MysteryBoxPanel`). Reglas puras en `TN_MysteryBox.h`
  (`Tortunabo.Shop.MysteryBox.*`).
- **Valores en datos:** `DA_PointsEconomy` (`/Game/Blueprints/Gameplay/Economy`, apuntado por
  `[/Script/Tortunabo.TN_EconomySettings]` en `DefaultGame.ini`): fórmula de los puntos, tiempo objetivo por nivel y caja
  sorpresa. Si no carga, valen los de serie (los de la decisión del 07-10).
- **Saldo insuficiente:** «Comprar» y «Caja sorpresa» se deshabilitan y debajo sale cuántos puntos faltan.
- **Probar:** `TN.Shop.AddPoints [puntos]` (1000 si no se dice).

## Buggy del Rally (#297, #114, #115)

El aspecto del buggy del Rally es un modelo de carrocería y una pintura (`FTN_BuggyLook`). El de serie, gratis, es el
buggy de Art/Source (`SM_TN_BuggyBody`, #290); las tres carrocerías de tortuga son modelos de pago y las 22 pinturas
valen para los cuatro (decisión del 03-10 en #297). Se compran en la pestaña **BUGGY** de la tienda, que mezcla modelos y
pinturas; el escaparate enseña el buggy en miniatura con la tortuga del jugador al volante y se prueba lo que se mira. Se
ponen en la página **BUGGY** del probador (Q/E o los botones TORTUGA y BUGGY), con dos filas: modelo y pintura. En el
Rally, cada buggy lleva el de su conductora; los de la IA y los vacíos, el de serie con la skin de su equipo.

### Catálogo

En C++, sin DataTable (`Vehicles/TN_BuggyCosmetics.h/.cpp`): el servidor y los tests lo leen sin assets. Ids
`BuggyModel_*` y `BuggyPaint_*`; `NAME_None` es el de serie. Los textos son `NSLOCTEXT` (claves `BuggyModel*` y
`BuggyPaint*`).

| Modelo | Conchas | Carrocería |
|---|---|---|
| Buggy de Serie (de serie) | 0 | El de Art/Source (`SM_TN_BuggyBody` y `SM_TN_BuggyTire`): chasis de tubos, pontones con dorsal y jaula |
| Buggy Clásico (`BuggyModel_Clasico`) | 900 | Tortuga común: caparazón de placas hexagonales, aletas por guardabarros, ojos-faro, cola de escape cromada y alerón de vieira |
| Caimán Todoterreno (`BuggyModel_Caiman`) | 1200 | Tortuga caimán: placas con pinchos en tres quillas, ceño, colmillos, defensa tubular con faros, arco con barra de luces, tubo de buceo, faldillas y rueda de repuesto |
| Laúd Bólido (`BuggyModel_Laud`) | 1500 | Tortuga laúd: caparazón bajo de siete crestas, cabeza en cuña con mirada de concentración, sillín sobre una torreta, alerón de carreras, faldón y escapes laterales |

Pinturas (colores de carrocería, placas y piel, dibujo, brillo y luz propia): Pintura de serie (0: en el de serie, la
skin del equipo; en las tortugas, su verde), Coral Bravo (300),
Azul Marino (400, olas), Arena Dorada (300), Sandía Veraniega (500), Ajedrez de Meta (800), Lava Volcánica (1200, grietas
que brillan), Noche Estrellada (1000, estrellas que brillan), Mariquita (450), Medusa Rosa (500), Oro Pirata (2000,
metal), Plata Pulida (1500, metal), Camuflaje de Alga (600), Llamas Infernales (1200), Rayas de Carreras (600), Abisal
Luminosa (1400, lunares que brillan), Carey (700), Hielo Polar (500), Atardecer Tropical (700), Tiburón (400), Pulpo
Morado (550) y Caramelo de Feria (500).

### Red y guardado

- `ATN_CoopPlayerState::EquippedBuggyLook` se replica; su OnRep avisa (`OnAnyBuggyLookChanged`) y el buggy de esa
  conductora se repinta al momento en cada máquina (y, por si acaso, cada medio segundo con los asientos).
- Lo escribe solo el servidor tras validarlo contra el catálogo y lo comprado (`TNBuggyCosmetics::CanEquip`): en el
  lobby, `ServerSyncUnlockedBuggy` y `ServerSetEquippedBuggyLook` (`AMP_GamePlayerController::RequestEquipBuggyLook`); en
  el Rally, dentro del lote de `ServerSyncCosmetics`. Cotas: 256 Ids en la RPC y 64 aceptados.
- Perfil cosmético: `UnlockedBuggyIds` y `EquippedBuggyLook` (sin cambio de versión: un perfil viejo los lee vacíos; un
  Id que ya no existe se lee como el de serie).

### Carrocería

- **El de serie** es el modelo de `ATN_Buggy` tal cual: con la pintura de serie, la skin de su equipo (`ApplyTint`: Mar,
  Alga o Medusa con el color del equipo); con una pintura de la tienda, `M_BuggyPaint` en la carrocería y los neumáticos
  y una antena con el banderín del equipo en el parachoques trasero, para que se siga viendo el equipo.
- **Las tortugas**: `Vehicles/TN_BuggyArt.cpp` construye en C++, una vez por pieza y modelo, mallas de caras planas (el
  estilo low poly del juego) que comparten todos los buggies. Cada pieza tiene un nombre estable
  `Rally.Buggy.<Pieza>.<Modelo>` (piezas `Chassis`, `Cockpit`, `Shell`, `Head`, `Fenders`, `Tail`, `Rear`, `Extras`,
  `Wheel` y `Antenna`; modelos `Clasico`, `Caiman` y `Laud`) para poder cambiarla por arte con el catálogo de #319.
- Las tortugas se sientan igual en los cuatro modelos (sockets `Seat_Driver` y `Seat_Gunner` de `SM_TN_BuggyBody`, que
  sigue puesta aunque oculta) y la torreta es la de `ATN_Buggy` (`TNBuggyTurretMesh`). Las carrocerías de tortuga se
  hacen a esas medidas: bañera con la conductora en el centro, el timón donde el volante del de serie, cúpula por debajo
  del cojín de la artillera con un hueco para sus pies (reposapiés a 90 cm), sillín con respaldo y barandillas de latón
  a 145 cm, a las que se sujeta el aro de la torreta.
- La cabeza estira el cuello por delante de los pies de la conductora; los ojos son los faros (brillan), con el iris del
  color del equipo. Las aletas salen del cuerpo y abrazan la rueda (con su paso de rueda). En la cabina: asiento, timón
  de barco, salpicadero con relojes y una caja de cocos (la munición).
- `UTN_BuggyLookComponent` viste la carrocería y los neumáticos del `ATN_Buggy` (con una tortuga, la carrocería de serie
  se oculta y los neumáticos llevan la rueda del modelo, con la cara de fuera a -Y como `SM_TN_BuggyTire`), monta las
  piezas y mueve la antena con los acelerones. La llama del turbo sale por la cola de cada tortuga. Solo visual: la
  física, la colisión y el chasis no cambian.
- `Tortunabo.Rally.Buggy.Art.Pieces` comprueba que las carrocerías dejan sitio a las dos tortugas sentadas y al barrido
  del aro, del carro y del cañón de la torreta, con las medidas de las ruedas y de las barandillas.

### Material

`/Game/Vehicles/Buggy/M_BuggyPaint` (`Scripts/build_buggy_paint.py`, sin interfaz con `-run=pythonscript`; la carpeta se
cocina siempre). Pinta dos clases de malla según `ZoneScheme`:

- **1, el de serie**: lee las zonas de `M_TN_BuggyZones` (el RGB marca pintura, detalle, chasis, neumático o luz y el
  alfa es el sombreado). La pintura lleva `PlateColor` y el dibujo; el detalle (asientos, amortiguadores, radios),
  `AccentColor`; el chasis y la jaula, `BaseColor` con un punto de metal; el neumático es goma y los faros brillan como
  en su material. Sus skins no se tocan: con la pintura de serie sigue su `MI_TN_Buggy_*`.
- **0, las tortugas**: el alfa del color de vértice es la zona, en octavos: 8 pintura, 7 pintura sin dibujo (llantas),
  6 equipo, 4 luz, 2 metal, 0 mate. En la pintura, el RGB son las máscaras de carrocería (`BaseColor`), placas
  (`PlateColor`) y piel (`AccentColor`) con su sombreado.

Parámetros comunes: `Pattern` (0 liso, 1 escamas, 2 lunares, 3 olas, 4 estrellas, 5 lava, 6 ajedrez, 7 sandía, como el
caparazón de la tortuga, y 8 franjas, 9 llamas, 10 camuflaje), `PatternColor`, `PatternScale`, `Shine`, `Glow`,
`TeamColor` y `LightGlow`. Si falta el material, las tortugas salen con los colores horneados en
`M_CosmeticVertexColor` (sin dibujo) y el de serie, con la skin de su equipo.

### Añadir

- **Pintura:** una fila en `TNBuggyCosmetics::Paints()` con su `NSLOCTEXT`, precio, colores (sRGB hexadecimal), dibujo,
  brillo y luz.
- **Modelo:** una fila en `Models()` con un `ETNBuggyBodyStyle` nuevo y su rama en las piezas de `TNBuggyArt` (cúpula,
  cabeza, aletas, detrás y extras), a las medidas de `TNBuggyArt::Frame`. `Tortunabo.Rally.Buggy.*` comprueba el
  catálogo y las medidas.
- **Fotos de prueba:** `TN.Buggy.Photos` (escaparate: los cuatro modelos, las pinturas en el de serie y en el clásico y
  las miniaturas de la tienda) y `TN.Rally.DebugBuggy` con `TN.Buggy.WorldShots` (en el Rally); ver
  `Docs/Comandos_Prueba.md`.
