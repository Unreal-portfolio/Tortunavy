# Tienda y probador del lobby

Sistema real de cosméticos de Tortunavy: la tienda de Don Tortugo (catálogo y compra) y los probadores de botella
(elegir lo desbloqueado y ponérselo). Sustituye al catálogo de «pulsar y aplicar» y a las estatuas de prueba de
`LVL_HQ` (`BP_SkinStatue`, `BP_HatStatue`), que quedan como prototipos.

## Flujo

1. **Tienda** (`ATN_ShopKeeper`). Al hablar con el tendero se abre `UTN_ShopWidget` en el cliente que interactúa:
   a la izquierda, tu tortuga posando y girando en una peana (arrastrar con el ratón la gira); a la derecha, el
   tendero habla en su bocadillo, cuatro pestañas (cascos, caparazones, colores y ojos) y el catálogo con miniaturas
   y precio (las de ojos, con primer plano de la cara).
   Al elegir algo, la tortuga se lo prueba encima de lo que lleva y saluda. **Comprar** desbloquea el cosmético
   (hoy todo cuesta 0 conchas) y lo guarda en el `SaveGame` local; el servidor recibe la lista de desbloqueados
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
- **Caparazón o color:** añadir una fila a `SHELLS` o `BODIES` en `Scripts/build_cosmetics.py` (o directamente en
  `DT_Skins`) con `Category`, `Color`, `Color2`, `Pattern`, `PatternScale`, `Shine` y `Glow`.
- **Ojos:** añadir una fila a `EYES` en `Scripts/build_cosmetics.py` con el tipo (`EyeStyle`), el color del iris o la
  pupila, el segundo color y el brillo. Un tipo nuevo necesita su rama en el HLSL de ojos del script y su valor en
  `ETNEyeStyle`.

El precio (`Price`) ya se descuenta de las conchas acumuladas (`AccumulatedRaceScore`); la economía de conchas o
estrellas queda para más adelante.
