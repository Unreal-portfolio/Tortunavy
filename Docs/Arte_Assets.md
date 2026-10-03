# Arte: sustituir las mallas del lobby y de los mapas procedurales

Issue #319. Casi todo el lobby (castillo, valle, parque, tutorial, puestos) y los mapas procedurales (el cooperativo y la
carrera de la playa) se dibujan con mallas que el código genera al montar el nivel. Cada pieza visible tiene un **nombre
estable** (`Lobby.Castle.Tower`, `ProcMap.Rock.RoundBoulder`...) y Arte la puede cambiar por su malla final **sin tocar código**,
desde un data asset. Lo que no se cambia sigue generándose como siempre.

La tortuga es aparte (§10): su cuerpo es la malla esquelética de `BP_TortugaCharacter` y las piezas que se le pegan a los
huesos (caparazón, casco, ojos, lengua) van en su propio catálogo, `DA_Arte_Tortuga`.

Lo funcional no cambia: la colisión generada se queda (invisible), y los activadores, las zonas de listo, la interacción,
las animaciones por código (puertas, tapas, el muelle de la medusa, el bamboleo del puente) y la colocación procedural
(dónde, cuántas, escala y semilla) siguen igual. Solo cambia lo que se dibuja.

## 1. Cómo se sustituye una pieza, paso a paso

1. **Abre el catálogo de la zona** en `/Game/Art`:
   - `DA_Arte_Lobby`: piezas `Lobby.*`.
   - `DA_Arte_ProcMap`: piezas `ProcMap.*` (mapa procedural del cooperativo) y `Beach.*` (carrera de la playa).
   - `DA_Arte_Tortuga`: piezas `Turtle.*`, pegadas a los huesos de la tortuga (§10).

   Si no existen, o si el código ha añadido piezas nuevas, ejecuta `Scripts/arte/rellenar_catalogos.py` (§6): crea los tres
   catálogos y mete todas las piezas vacías. No pisa nada de lo que ya hayas puesto.
2. **Busca la pieza** en `Pieces` por su nombre (lista completa en §4). Su campo `Info` dice qué es, su tamaño, su pivote y
   el fichero de C++ que la genera. En el juego, `TN.Art.Slots` (o `TN.Art.Slots Lobby.Castle`) lista las piezas, si se han
   visto en la sesión, lo que dibujan y su sustituto.
3. **Mesh**: importa la malla final con el pivote y la escala que dice la tabla (cm, +Z arriba; los ejes de cada pieza
   están en la columna «Pivote») y ponla en `Mesh`. Con eso ya se usa.
4. **Materials** (opcional): un material por ranura de tu malla. Vacío, o una ranura vacía, usa los de la malla.
5. **Adjust** (opcional): desplazamiento, giro y escala de tu malla respecto al pivote de la pieza generada, para encajarla
   sin volver a exportar (se aplica en los ejes de la pieza, antes de colocarla).
6. **bUseArtCollision**: déjalo en falso. La pieza conserva la colisión generada, invisible, y se juega exactamente igual.
   Ponlo en cierto solo si tu malla trae su propia colisión simple y quieres que se choque con ella: entonces la generada
   se quita para esa pieza y cambia cómo se juega (pruébalo y avisa a programación).
7. **Comprueba**: dale al Play. Cada partida (también el PIE y cada viaje) vuelve a leer los catálogos tal como estén en
   ese momento. En el editor, sin jugar, el castillo y el valle se rehacen solos con cada cambio que hagas en el panel del
   catálogo; las piezas de los mapas procedurales, del tutorial y los componentes que se guardan con el nivel solo se ven
   al jugar (§2). Si cambias el catálogo con Python o lo recargas (un catálogo traído de git con el editor abierto no se ve
   hasta recargarlo, Asset Actions > Reload, o reiniciar el editor, como cualquier asset), el Play ya lo ve; para verlo en
   el editor sin jugar, pulsa **Aplicar cambios** en el catálogo (o `TN.Art.Reload`). `TN.Art.Enabled 0` y recargar el
   nivel enseña lo generado para comparar. Si una pieza con malla no sale, mira el log: `[Arte]` avisa de las mallas que
   no cargan y de los nombres de pieza que no existen.
8. **Sube** el catálogo y tus mallas. Son binarios: antes de tocar un asset, comprueba que ninguna issue en curso lo nombra
   y escribe en tu issue qué assets cambias (CLAUDE.md, «Reglas»).

Si dos catálogos tienen la misma pieza, gana el primero de la lista de Ajustes del proyecto > Tortunavy > Arte. Una
entrada sin malla no hace nada (así llegan del script).

## 2. Tipos de pieza

La columna «Tipo» de la lista dice cómo se pone tu malla:

- **Pieza**: parte de una malla grande combinada (el castillo, el valle, el recorrido del tutorial, las estructuras del
  mapa procedural). Se quita de la malla combinada y tu malla se pone en el sitio de cada copia, como instancias. Si las
  copias tienen tamaños distintos, la tabla da un tamaño de referencia (escala 1) y cada copia se estira a su tamaño, para
  que coincida con su colisión (por ejemplo, `Lobby.Castle.Tower`: radio 250 y 1000 de alto a escala 1).
- **Componente**: la malla de un componente suelto (una hoja de puerta, una tapa, el cofre, un puesto). Tu malla va de hija
  del componente y se mueve, se esconde y se enseña con él: las animaciones por código siguen funcionando. En el editor,
  sin jugar, los componentes que se guardan con el nivel (las hojas de la puerta del castillo, por ejemplo) se siguen
  viendo generados: la malla de arte sale al darle al Play.
- **Instancias**: la malla de un grupo de instancias (flora, objetos, fauna). Cambia la malla de todas; el ajuste se aplica
  a cada instancia. La fauna se anima por partes (cuerpo, cola, aletas...): cada parte es una pieza.
- **Hueso**: una malla que no existe sin Arte y que va pegada a un hueso de la tortuga (`Turtle.*`, §10): sigue su
  animación en el jugador y en todas sus copias. `Bone` cambia el hueso.

## 3. Reglas para las mallas finales

- Unidades en cm, +Z arriba, el pivote donde dice la tabla. Si no coincide, corrígelo con `Adjust`.
- Tamaño parecido al de la pieza generada: la colisión sigue siendo la generada (salvo `bUseArtCollision`), así que una
  malla mucho más grande o más pequeña deja partes que se atraviesan o choques invisibles.
- Las piezas generadas se pintan con color de vértice (`M_CosmeticVertexColor` en el lobby, `M_ProcFoliage` y compañía en
  el mapa procedural; §5): una malla final con sus propios materiales y texturas no necesita color de vértice.
- La flora se mueve con el viento por el alfa del color de vértice y la posición (WPO del material): con un material
  propio, el viento es cosa de ese material.
- Colisión simple solo si vas a usar `bUseArtCollision`.

## 4. Lista de piezas

848 piezas a 03-10-2026: 234 del lobby (castillo 21, valle 85, tutorial 106, parque y puestos 22), 414 del mapa
procedural, 196 de la carrera de la playa y 4 de la tortuga. Generada desde la tabla del código (`Source/Tortunabo/Private/Art/TN_ArtSlots_*.inl`)
con `uv run python Scripts/arte/rellenar_catalogos.py --doc`. El fichero es relativo a `Source/Tortunabo/Private/`; «Escala 1»
quiere decir que cada copia se estira a su tamaño respecto a ese (§2).

<!-- piezas: inicio (lo escribe Scripts/arte/rellenar_catalogos.py --doc) -->

### Lobby (DA_Arte_Lobby) — 234 piezas

#### Lobby.Castle

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Lobby.Castle.Floor` | Pieza | Suelo de arena redondo de dentro del castillo, con manchas | Disco de 5240 de diámetro | Centro del castillo, Z = 2 (cara de arriba del suelo); +Y hacia la puerta doble | `Lobby/TN_SandCastleLobby.cpp` |
| `Lobby.Castle.OuterBeach` | Pieza | Playa plana de fuera de la muralla (sin colisión) | Anillo de radio 2610 a 3900 | Centro del castillo, Z = 4 | `Lobby/TN_SandCastleLobby.cpp` |
| `Lobby.Castle.Sea` | Pieza | Orilla y mar de alrededor; no sale si el valle del lobby ocupa su sitio | Orilla de radio 3900 a 4400 y plano de 600 x 600 m | Centro del castillo, Z = -34 (nivel del mar) | `Lobby/TN_SandCastleLobby.cpp` |
| `Lobby.Castle.Wall` | Pieza | Muralla redonda con adarve, marcas de cubo y almenas, con el hueco de la puerta doble | Anillo de radio 2400 a 2590, 600 de alto (ondula 70) | Centro del castillo a ras de suelo; +Y hacia el hueco de la puerta | `Lobby/TN_SandCastleLobby.cpp` |
| `Lobby.Castle.Gatehouse` | Pieza | Puerta doble entera: suelo, dos fachadas con arco y cartel, sala con conchas y antorchas, dos torres grandes y dos torreones (las hojas van aparte) | 2060 de ancho con las torres, 700 de fondo, fachadas de 760 y torres de 1350 | Centro del umbral de la puerta 1 a ras de suelo; +Y hacia la puerta 2 (fuera del castillo) | `Lobby/TN_CastleKit.h` |
| `Lobby.Castle.Tower` | Pieza | Torre de cubo de la muralla (9 copias) con franjas, almenas, cono o azotea y bandera | Escala 1: radio 250 y 1000 hasta el adarve; cada copia se estira a su radio (200 a 300) y su alto (760 a 1250) | Centro de la base; +X hacia fuera del castillo | `Lobby/TN_CastleKit.h` |
| `Lobby.Castle.InnerWall` | Pieza | Cada mitad del muro interior recto (2 copias), con adarve, marcas y almenas | 1940 de largo, 260 de grueso, 600 de alto más almenas | Centro del tramo a ras de suelo; +X hacia la muralla redonda | `Lobby/TN_SandCastleLobby.cpp` |
| `Lobby.Castle.Slide` | Pieza | Tobogán de plástico del adarve (2 copias: coral a la plaza y turquesa al patio), con barandillas y pilares | 560 de recorrido, 226 de ancho, sale a 600 de alto | Boca del tobogán en la cara del muro, a ras de suelo; +X hacia donde baja | `Lobby/TN_SandCastleLobby.cpp` |
| `Lobby.Castle.Lookout` | Pieza | Mirador del adarve derecho: catalejo en trípode, cubo con pala y banderón turquesa | Unos 300 x 200, mástil de 420 | Pie del trípode en el adarve (Z = 600); ejes del castillo | `Lobby/TN_SandCastleLobby.cpp` |
| `Lobby.Castle.Keep` | Pieza | Torre del homenaje: cilindro con franjas, azotea con almenas y paso de norte a sur con arcos y dos antorchas | Radio 400, 900 hasta la azotea; paso de 260 de ancho y 300 de alto | Eje de la torre a ras de suelo; +Y hacia la plaza | `Lobby/TN_SandCastleLobby.cpp` |
| `Lobby.Castle.KeepTurret` | Pieza | Torreón sobre la azotea de la torre del homenaje, con cono y bandera | Radio 150, 430 de alto más el cono | Centro de la base sobre la azotea (Z = 900) | `Lobby/TN_CastleKit.h` |
| `Lobby.Castle.TreasureDais` | Pieza | Tarima redonda del cofre del tesoro en la azotea, con conchas | Radio 125, 24 de alto | Centro de la tarima a la altura de la azotea (Z = 900); el cofre va encima | `Lobby/TN_SandCastleLobby.cpp` |
| `Lobby.Castle.KeepStairs` | Pieza | Escalera de caracol de la torre del homenaje, sus dos rellanos y las dos escaleras rectas a los adarves, con bolardos | Anillo de radio 400 a 610 hasta 900 de alto; escaleras rectas de 440 | Eje de la torre a ras de suelo; +Y hacia la plaza | `Lobby/TN_SandCastleLobby.cpp` |
| `Lobby.Castle.EggMound` | Pieza | Montículo de dos alturas de la pila de huevos, con el escalón de la concha | Radio 480 y 60 de alto; piso alto de radio 175 y 200 de alto | Centro del montículo en el suelo; +X hacia la puerta doble | `Lobby/TN_CastleKit.h` |
| `Lobby.Castle.EggCup` | Pieza | Base de cada huevo de listo (8): media cáscara con borde en zigzag y nido de paja | Radio 97, 110 de alto | Centro de la base del huevo | `Lobby/TN_CastleKit.h` |
| `Lobby.Castle.EggLid` | Componente | Tapa de cada huevo de listo (8); baja y se cierra cuando alguien se mete | Radio 96, 128 de alto | Centro de la costura (el borde de abajo) | `Lobby/TN_SandCastleLobby.cpp` |
| `Lobby.Castle.GateLeaf` | Componente | Hoja de madera de la puerta doble (4); gira sobre su bisagra al abrirse | 430 de ancho, 540 de alto, 30 de grueso | Bisagra (borde de abajo); la hoja va hacia +X | `Lobby/TN_CastleKit.h` |
| `Lobby.Castle.Shell` | Pieza | Concha de vieira de adorno en muros, tarima y suelo | Escala 1: 30 de alto (las copias van de 9 a 30) | Centro de la concha; +X hacia donde mira su cara, +Z hacia el borde del abanico | `Lobby/TN_CastleKit.h` |
| `Lobby.Castle.Starfish` | Pieza | Estrella de mar plana de adorno en el suelo | Escala 1: puntas a 30 del centro (las copias van de 22 a 32) | Centro en el suelo; +X hacia la primera punta | `Lobby/TN_CastleKit.h` |
| `Lobby.Castle.Bunting` | Pieza | Guirnalda de banderines sobre el muro interior con sus mástiles (2 copias) | 950 de largo, mástiles de 170 | Pie del primer mástil en el adarve (Z = 690); +X a lo largo de la guirnalda | `Lobby/TN_SandCastleLobby.cpp` |
| `Lobby.Castle.Torch` | Pieza | Antorcha de pared del paso de la torre del homenaje (la luz es aparte) | Unos 40 x 30 x 80 | Punto del muro donde se clava; +X hacia fuera del muro | `Lobby/TN_CastleKit.h` |

#### Lobby.Valley

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Lobby.Valley.Landmark.StiltHut` | Pieza | Palafito de pescador (2 copias: isleta de la laguna y manglar) | Radio 420, 650 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.Lighthouse` | Pieza | Faro a rayas en el cabo de la playa (la 1) | Radio 400, 3100 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.GiantShell` | Pieza | Caracola gigante de pie en la playa | Radio 380, 720 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.Shipwreck` | Pieza | Barco varado de costado con el mástil roto, en la playa | Radio 850 (unos 1700 de largo), 450 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.ColossalTurtle` | Pieza | Tortuga colosal de piedra sobre su pedestal, en las dunas (las 2) | Radio 760, 830 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.Obelisk` | Pieza | Obelisco con bandas de símbolos y punta dorada, en las dunas | Radio 190, 1400 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.FossilSkull` | Pieza | Cráneo fósil gigante medio enterrado, con cuernos, en las dunas | Radio 430, 340 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.Hoodoo` | Pieza | Chimenea de hadas: roca en capas con sombrero, en el cañón (3 copias) | Escala 1: radio 230 y 1000 de alto; las otras copias se estiran a 200 x 820 y 260 x 1250 | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.BalancedRock` | Pieza | Peñasco en equilibrio sobre un pedestal, en el cañón | Radio 260, 720 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.Mesa` | Pieza | Mesa de techo plano con estratos al fondo del cañón (las 3) | Radio 2600, 2400 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.ObsidianSpires` | Pieza | Agujas de obsidiana del volcán (las 4) | Radio 450, 950 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.BasaltColumns` | Pieza | Columnas hexagonales de basalto del volcán | Radio 520, 620 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.Fumarole` | Pieza | Cono de fumarola con azufre, en el volcán | Radio 320, 240 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.CastleRuin` | Pieza | Castillo en ruinas con torre en lo alto de los acantilados (las 5) | Radio 1700, 1350 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.StoneCircle` | Pieza | Círculo de piedras en pie con dinteles y altar, en un claro del bosque (las 7) | Radio 650, 380 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.Windmill` | Pieza | Molino de viento (2 copias: colina del pueblo y loma de las granjas) | Escala 1: radio 420 y 1700 de alto; la de las granjas se estira a 1500 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.WaterTower` | Pieza | Depósito de agua de madera sobre patas, en el pueblo (las 8) | Radio 300, 1000 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.Pyramid` | Pieza | Pirámide escalonada con escalinata, en la selva (las 10) | Radio 1900, 1600 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Landmark.StoneHead` | Pieza | Cabeza colosal de piedra, en la selva | Radio 320, 560 de alto | Centro de la formación a la altura del terreno en su centro; +X hacia el castillo | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `Lobby.Valley.Rock.TwinSpire` | Pieza | Aguja doble de roca de los acantilados | Radio 380, 1800 de alto | Centro de la base, en lo más bajo de su pie; ejes del valle (+X hacia las 9, +Y hacia las 12) | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `Lobby.Valley.Rock.LeaningSpire` | Pieza | Aguja inclinada de roca de los acantilados | Radio 320, 1500 de alto | Centro de la base, en lo más bajo de su pie; ejes del valle (+X hacia las 9, +Y hacia las 12) | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `Lobby.Valley.Rock.Spire` | Pieza | Aguja de roca de los acantilados con un peñasco en la cima, donde se posa el águila (a 1,05 radios sobre su alto menos 95: la malla de arte debe acabar ahí) | Radio 300, 2000 de alto | Centro de la base, en lo más bajo de su pie; ejes del valle (+X hacia las 9, +Y hacia las 12) | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `Lobby.Valley.Rock.Tor` | Pieza | Peñasco de bolos apilados de las cumbres nevadas (2 copias) | Escala 1: radio 420 y 700 de alto; la otra copia se estira a 380 x 620 | Centro de la base, en lo más bajo de su pie; ejes del valle (+X hacia las 9, +Y hacia las 12) | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `Lobby.Valley.Rock.KarstPillar` | Pieza | Pilar kárstico de la selva (3 copias) | Escala 1: radio 400 y 1900 de alto; las otras copias se estiran a 350 x 1600 y 380 x 2100 | Centro de la base, en lo más bajo de su pie; ejes del valle (+X hacia las 9, +Y hacia las 12) | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `Lobby.Valley.Rock.LavaDome` | Pieza | Domo de lava en la sierra del volcán | Radio 700, 900 de alto | Centro de la base, en lo más bajo de su pie; ejes del valle (+X hacia las 9, +Y hacia las 12) | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `Lobby.Valley.Cottage` | Pieza | Casita encalada del pueblo y cabañas de la nieve y del bosque (12): zócalo de piedra, tejado a dos aguas, puerta, ventanas y chimenea; dos echan humo desde (-0,22 del ancho, 0,2 del fondo, 80 sobre la cumbrera) | Escala 1: planta de 460 x 360, pared de 280 y tejado de 210 (unos 560 hasta la chimenea); el zócalo baja hasta el terreno; las copias van de 0,9 a 1,15 | Centro de la planta a la altura del suelo de la casa; +X hacia la puerta (la cumbrera va a lo largo de X) | `Lobby/TN_LobbyValley.cpp` |
| `Lobby.Valley.Barn` | Pieza | Granero rojo de las granjas: zócalo, tejado a dos aguas, portón y puerta del pajar | Escala 1: planta de 700 x 520, pared de 380 y tejado de 300; va a escala 1,25 | Centro de la planta a la altura del suelo; +X hacia el portón (la cumbrera va a lo largo de X) | `Lobby/TN_LobbyValley.cpp` |
| `Lobby.Valley.Flora.Willow` | Instancias | Sauce llorón (laguna) | Unos 850 de alto y 600 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.BroadTree` | Instancias | Árbol de copa redonda (laguna, pueblo, granjas y selva) | 820 a 1020 de alto, 500 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Birch` | Instancias | Abedul de tronco blanco (laguna, acantilados, bosque y pueblo) | 850 a 1150 de alto, 400 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Palm` | Instancias | Palmera de tronco curvo (laguna, playa, selva y manglar) | 780 a 980 de alto, la copa se desplaza 120 a 260 hacia +X; hojas de 340 a 440 | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Reeds` | Instancias | Juncos y eneas de la orilla y el agua somera (laguna y manglar) | 150 a 250 de alto, 80 de ancho | Base en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Cypress` | Instancias | Ciprés de pantano, cónico (laguna y manglar) | 1150 a 1400 de alto, 300 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Bush` | Instancias | Arbusto de varias masas (laguna, playa, acantilados, nieve, bosque, pueblo y granjas) | Unos 150 de alto y 200 de ancho | Base en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Rock` | Instancias | Peñasco suelto (todos los sectores y la sierra), del color de la roca de su bioma | 200 de ancho, 55 a 130 de alto (la variante baja lleva otra piedra al lado) | Centro de la base en el suelo (se hunde un poco); giro al azar en Z; cada copia lleva su escala (0,6 a 2,8) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Casuarina` | Instancias | Casuarina de ramillas colgantes (playa) | 850 a 1150 de alto, 400 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.SeaGrape` | Instancias | Uva de playa: arbolito bajo de hojas redondas (playa) | 220 a 320 de alto, 300 de ancho | Base en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.FanPalm` | Instancias | Palmito de hojas en abanico (playa) | 150 a 250 de alto, 300 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Pandanus` | Instancias | Pándano de raíces zancudas y penachos de hojas afiladas (playa y manglar) | 500 a 650 de alto, 400 de ancho | Base de las raíces en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Saguaro` | Instancias | Cactus columnar con brazos (dunas y cañón) | 460 a 620 de alto, radio 40 | Base en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.JoshuaTree` | Instancias | Árbol de Josué (dunas y cañón) | 450 a 650 de alto, 400 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.BarrelCactus` | Instancias | Cactus barril con flor (dunas y cañón) | Radio 42, 75 de alto | Base en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.DryBush` | Instancias | Matojo seco de ramitas (dunas y cañón) | Unos 120 de ancho y 60 de alto | Base en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.DeadTree` | Instancias | Árbol seco sin hojas (dunas y volcán) | 600 a 800 de alto | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Acacia` | Instancias | Acacia de copa plana (cañón) | 600 a 720 de alto, 600 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.CharredTree` | Instancias | Árbol calcinado con brasas en las grietas (volcán) | 500 a 700 de alto | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.AshBush` | Instancias | Arbusto de ceniza con ramitas secas (volcán) | Unos 150 de alto y 200 de ancho | Base en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Pine` | Instancias | Pino de pisos (volcán, acantilados, nieve y bosque) | 1000 a 1250 de alto, 560 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Fir` | Instancias | Abeto estrecho y denso (acantilados, nieve y bosque) | 1200 a 1450 de alto, 400 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Fern` | Instancias | Helecho de frondas arqueadas (bosque) | Unos 100 de alto y 250 de ancho | Base en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Ornamental` | Instancias | Árbol de parque de copa esférica (pueblo y granjas) | Unos 580 de alto y 340 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Hedge` | Instancias | Seto recortado (pueblo y granjas) | 220 de largo, 90 de ancho y 130 de alto | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Ceiba` | Instancias | Ceiba gigante de raíces tabulares y copa en parasol (selva) | 2100 a 2500 de alto, copa de 1400 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.TreeFern` | Instancias | Helecho arbóreo (selva y manglar) | Tronco de 280 a 420 y frondas de unos 220 | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.Bamboo` | Instancias | Mata de bambú con nudos y penachos (selva) | 620 a 1050 de alto, 150 de ancho | Base en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.BananaPlant` | Instancias | Platanera de hojas enormes (selva) | Unos 300 de alto y 400 de ancho | Base en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.MangroveTree` | Instancias | Mangle de raíces zancudas en arco (manglar, también en el agua) | 650 a 820 de alto, raíces de 500 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala (más grande lejos del castillo) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Valley.Flora.HayBale` | Instancias | Pila de pacas de paja rectangulares de las granjas | Pacas de unos 90 x 45 x 45 apiladas | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala (1,6 a 2,2) | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Valley.Fauna.Flamingo` | Instancias | Flamenco de la laguna, con las alas plegadas | 130 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3 para que se vea desde el castillo) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Pelican` | Instancias | Pelícano flotando en la laguna | 130 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Raíz bajo el cuerpo, en la superficie del agua más su calado; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Fish` | Instancias | Pez que salta del agua en arco en la laguna (escondido entre salto y salto) | 45 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Raíz bajo el cuerpo; +X hacia la cabeza (se inclina siguiendo el salto) | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.BabyTurtle` | Instancias | Cría de tortuga de la playa | 22 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Crab` | Instancias | Cangrejo rojo de la playa (anda de lado) | 22 de ancho y 16 de fondo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia los ojos | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Gull` | Instancias | Gaviota en el suelo de la playa y gaviotas posadas en las almenas del castillo, con las alas plegadas | 45 de largo a escala 1; las de la playa, x1,3 a x2,3; las del castillo, x1,35 | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Meerkat` | Instancias | Suricato de las dunas (se pone de pie a vigilar) | 50 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Roadrunner` | Instancias | Correcaminos de las dunas | 55 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Lizard` | Instancias | Lagartija turquesa del cañón | 45 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Vulture` | Instancias | Buitre en el suelo del cañón | 75 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.FireBeetle` | Instancias | Escarabajo de fuego del volcán (con sustituto, las rayas que brillan van en la malla de arte: no se pone la parte que brilla) | 25 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Salamander` | Instancias | Salamandra de fuego del volcán (con sustituto, las manchas que brillan van en la malla de arte: no se pone la parte que brilla) | 45 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Ibex` | Instancias | Cabra montés de los acantilados y las cumbres nevadas | 130 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Eagle` | Instancias | Águila posada en la cima de la aguja de los acantilados | 80 de largo a escala 1; va a x1,3 a x2,3 | Entre las patas, en la cima; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Marmot` | Instancias | Marmota de las cumbres nevadas | 55 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Rabbit` | Instancias | Conejo del bosque (va a saltitos) | 40 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Hen` | Instancias | Gallina del pueblo y de las granjas | 45 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Cat` | Instancias | Gato del pueblo | 75 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Pigeon` | Instancias | Paloma de las granjas y palomas posadas en las almenas del castillo, con las alas plegadas | 32 de largo a escala 1; las de las granjas, x1,3 a x2,3; las del castillo, x1,2 | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Monkey` | Instancias | Mono de la selva | 70 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Toucan` | Instancias | Tucán de la selva (va a saltitos) | 50 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Capybara` | Instancias | Capibara de la selva | 110 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.Heron` | Instancias | Garza blanca del agua somera del manglar | 100 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.FiddlerCrab` | Instancias | Cangrejo violinista del manglar, con una pinza enorme | 15 de ancho a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia los ojos | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.TreeFrog` | Instancias | Rana verde de ojos rojos del manglar | 18 de largo a escala 1; cada copia lleva su escala (x1,3 a x2,3) | Entre las patas, en el suelo; +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Valley.Fauna.GullFlying` | Instancias | Gaviota volando de almena en almena por el castillo; el aleteo lo hace el material M_ProcBird con el peso en el alfa del color de vértice | 100 de envergadura y 52 de largo a escala 1; va a x0,95 | Centro del cuerpo; +X hacia el pico, las alas a lo largo de Y | `World/ProcMap/TN_ProcMapAmbientFX.h` |
| `Lobby.Valley.Fauna.PigeonFlying` | Instancias | Paloma volando de almena en almena por el castillo; el aleteo lo hace el material M_ProcBird con el peso en el alfa del color de vértice | 100 de envergadura y 52 de largo a escala 1; va a x0,62 | Centro del cuerpo; +X hacia el pico, las alas a lo largo de Y | `World/ProcMap/TN_ProcMapAmbientFX.h` |

#### Lobby.Tutorial

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Lobby.Tutorial.IslandA` | Pieza | Isla A del tutorial entera, de la salida al cañón de la catapulta: pasillo, taludes, meseta y la roca que cuelga debajo (la colisión de arriba se queda tal cual: la malla de arte debe seguir su suelo) | 13400 de largo (X de -900 a 12500), 1600 a 3700 de ancho; la roca baja hasta 2800 | Origen del recorrido: Z = 0 el suelo del pasillo junto a la cascada, Y = 0 el eje de las islas, +X a lo largo del pasillo hacia la cascada | `Lobby/TN_TutorialCourse_Build.cpp` |
| `Lobby.Tutorial.IslandB` | Pieza | Isla B del tutorial entera, del cañón a la cascada: pasillo con la laguna y el arroyo, taludes, meseta y la roca que cuelga debajo (la colisión de arriba se queda tal cual: la malla de arte debe seguir su suelo) | 7900 de largo (X de 13600 a 21500), 1600 a 3700 de ancho; la roca baja hasta 2800 | Origen del recorrido: Z = 0 el suelo del pasillo junto a la cascada, Y = 0 el eje de las islas, +X a lo largo del pasillo hacia la cascada | `Lobby/TN_TutorialCourse_Build.cpp` |
| `Lobby.Tutorial.BalanceLog` | Pieza | Tronco de equilibrio sobre el cañón con sus cuatro estacas (choca: la colisión generada se queda) | 1430 de largo, radio 42; estacas de 110 | Eje del tronco en su punta de la terraza de la catapulta; +X a lo largo del tronco hacia la isla B (baja 190) | `Lobby/TN_TutorialCourse_Build.cpp` |
| `Lobby.Tutorial.SearchMound` | Pieza | Montículo de arena de la estación de rebuscar, con una pala y un cubo de juguete (rebuscar va aparte) | Radio 125, 95 de alto | Centro del montículo en el suelo; ejes del recorrido (+X hacia la cascada) | `Lobby/TN_TutorialCourse_Build.cpp` |
| `Lobby.Tutorial.DummyDais` | Pieza | Peana de arena del cangrejo de prácticas con la diana pintada encima | Radio 250, 36 de alto | Centro de la peana en el suelo; ejes del recorrido (+X hacia la cascada) | `Lobby/TN_TutorialCourse_Build.cpp` |
| `Lobby.Tutorial.Chevrons` | Pieza | Chevrones amarillos y rojos pintados en el suelo antes de la zanja del panzazo | Cinco flechas de 70 x 90 en una fila de 530 | Centro de la fila en el suelo; +X hacia la zanja | `Lobby/TN_TutorialCourse_Build.cpp` |
| `Lobby.Tutorial.Sign` | Pieza | Cartel de madera de cada estación (19): dos postes, tabla con marco y chapa redonda del número (los rótulos son textos aparte, delante de la tabla) | 300 de ancho y 280 de alto; tabla de 280 x 104 a 170 de alto, chapa de radio 30 a 250 | Entre los postes, en el suelo; +X hacia donde mira el cartel (los rótulos van a 6,5 por delante) | `Lobby/TN_TutorialCourse_Build.cpp` |
| `Lobby.Tutorial.LipBoulder` | Pieza | Peñasco a cada lado del borde de la cascada (2; choca: la colisión generada se queda) | Radio 70, 60 de alto | Centro de la base en el suelo; ejes del recorrido | `Lobby/TN_TutorialCourse_Build.cpp` |
| `Lobby.Tutorial.Cloud` | Pieza | Nube de bolas bajo y alrededor de las islas (material translúcido M_ProcFXCloud) | Escala 1: unos 3700 x 2900 x 1300; las copias van de 0,5 a 1,2 | Centro de la nube; ejes del recorrido | `Lobby/TN_TutorialCourse_Build.cpp` |
| `Lobby.Tutorial.CascadeMist` | Pieza | Jirón de bruma donde se deshace la cascada (7, cada uno más tenue: el alfa va en el color de vértice) | Escala 1: unos 1850 x 1450 x 650; las copias van de 0,7 a 1,3 | Centro del jirón; ejes del recorrido | `Lobby/TN_TutorialCourse_Build.cpp` |
| `Lobby.Tutorial.Flora.BroadTree` | Instancias | Árbol de copa redonda (selva, laguna y pueblo) | 820 a 1020 de alto, 500 de ancho | Base del tronco en el suelo; giro al azar en Z (alguno se inclina con la pendiente); cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Ceiba` | Instancias | Ceiba gigante de raíces tabulares y copa en parasol (selva) | 2100 a 2500 de alto, copa de 1400 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Palm` | Instancias | Palmera de tronco curvo (selva, playa, manglar y pueblo) | 780 a 980 de alto, la copa se desplaza 120 a 260 hacia +X; hojas de 340 a 440 | Base del tronco en el suelo; giro al azar en Z (alguna se inclina con la pendiente); cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.MangroveTree` | Instancias | Mangle de raíces zancudas en arco (manglar) | 650 a 820 de alto, raíces de 500 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.YoungSequoia` | Instancias | Secuoya joven de corteza roja (manglar) | 1500 a 1800 de alto, 800 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Cypress` | Instancias | Ciprés de pantano, cónico (laguna y manglar) | 1150 a 1400 de alto, 300 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Pine` | Instancias | Pino de pisos (roca) | 1000 a 1250 de alto, 560 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Fir` | Instancias | Abeto estrecho y denso (roca) | 1200 a 1450 de alto, 400 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Willow` | Instancias | Sauce llorón (laguna) | Unos 850 de alto y 600 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Acacia` | Instancias | Acacia de copa plana (desierto) | 600 a 720 de alto, 600 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.DeadTree` | Instancias | Árbol seco sin hojas (desierto) | 600 a 800 de alto | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Ornamental` | Instancias | Árbol de parque de copa esférica (pueblo) | Unos 580 de alto y 340 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Fern` | Instancias | Helecho de frondas arqueadas (selva y manglar) | Unos 100 de alto y 250 de ancho | Base en el suelo; giro al azar en Z, inclinado con la pendiente; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Bush` | Instancias | Arbusto de varias masas (todos los biomas del recorrido) | Unos 150 de alto y 200 de ancho | Base en el suelo; giro al azar en Z, inclinado con la pendiente; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Grass` | Instancias | Mata de hierba (todos los biomas del recorrido) | 45 a 90 de alto, 60 de ancho | Base en el suelo; giro al azar en Z, inclinada con la pendiente; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Flowers` | Instancias | Mata de hierba con flores de colores (selva, laguna, roca, manglar y pueblo) | Unos 60 de alto y 60 de ancho | Base en el suelo; giro al azar en Z, inclinada con la pendiente; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Reeds` | Instancias | Juncos y eneas de la orilla y el agua somera (laguna y manglar) | 150 a 250 de alto, 80 de ancho | Base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Saguaro` | Instancias | Cactus columnar con brazos (desierto) | 460 a 620 de alto, radio 40 | Base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.BarrelCactus` | Instancias | Cactus barril con flor (desierto) | Radio 42, 75 de alto | Base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.DryBush` | Instancias | Matojo seco de ramitas (desierto) | Unos 120 de ancho y 60 de alto | Base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Hedge` | Instancias | Seto recortado (pueblo) | 220 de largo, 90 de ancho y 130 de alto | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Umbrella` | Instancias | Sombrilla de terraza con mástil y peana (pueblo) | 300 de ancho, 250 de alto | Centro de la peana en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Creeper` | Instancias | Enredadera o musgo: manta de hojas pegada a las paredes de los taludes | Unos 150 de ancho y 12 de grueso | Centro de la manta; Z sale de la pared (la copia se orienta con su normal); cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.BananaPlant` | Instancias | Platanera de hojas enormes (selva) | Unos 300 de alto y 400 de ancho | Base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Bamboo` | Instancias | Mata de bambú con nudos y penachos (selva) | 620 a 1050 de alto, 150 de ancho | Base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.TreeFern` | Instancias | Helecho arbóreo (selva y manglar) | Tronco de 280 a 420 y frondas de unos 220 | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.SeaGrape` | Instancias | Uva de playa: arbolito bajo de hojas redondas (playa) | 220 a 320 de alto, 300 de ancho | Base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Pandanus` | Instancias | Pándano de raíces zancudas y penachos de hojas afiladas (playa y manglar) | 500 a 650 de alto, 400 de ancho | Base de las raíces en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.FanPalm` | Instancias | Palmito de hojas en abanico (playa y pueblo) | 150 a 250 de alto, 300 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Casuarina` | Instancias | Casuarina de ramillas colgantes (playa) | 850 a 1150 de alto, 400 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.JoshuaTree` | Instancias | Árbol de Josué (desierto) | 450 a 650 de alto, 400 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Birch` | Instancias | Abedul de tronco blanco (laguna, roca y pueblo) | 850 a 1150 de alto, 400 de ancho | Base del tronco en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Rock` | Instancias | Peñasco suelto (selva, playa, desierto, laguna y roca), del color de la roca de su bioma | 200 de ancho, 55 a 130 de alto (la variante baja lleva otra piedra al lado) | Centro de la base en el suelo; giro al azar en Z, inclinado con la pendiente; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Stones` | Instancias | Corro de piedras pequeñas (playa, desierto y roca) | Unos 100 de ancho y 25 de alto | Centro del corro en el suelo; giro al azar en Z, inclinado con la pendiente; cada copia lleva su escala | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `Lobby.Tutorial.Flora.Crate` | Instancias | Caja de madera con cantoneras (roca y pueblo; maciza: la colisión generada se queda) | Cubo de 80 a 120 | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.WoodBarrel` | Instancias | Barril de madera con aros (roca, manglar y pueblo; macizo) | Radio 40, unos 100 de alto | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Barricade` | Instancias | Valla de obra a franjas sobre dos caballetes, con luz naranja (pueblo; maciza) | 170 a 250 de largo, unos 125 de alto | Centro de la base en el suelo; +X a lo largo de la valla; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.TrafficCone` | Instancias | Cono de tráfico naranja, solo o con otro al lado (pueblo; macizo) | Base de 44, unos 75 de alto | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.HayBale` | Instancias | Paca de paja: redonda tumbada o pila de rectangulares (pueblo; maciza) | Redonda: radio 70 y 120 de largo; rectangulares de unos 90 x 45 x 45 | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Bench` | Instancias | Banco de parque de tablas con patas de hierro (pueblo; macizo) | 120 a 170 de largo, unos 85 de alto | Centro de la base en el suelo; +X a lo largo del banco; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.LampPost` | Instancias | Farola con farol cálido (pueblo; maciza) | Poste de 330 a 390, unos 420 en total | Pie del poste en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Mailbox` | Instancias | Buzón de poste con techo redondo y banderita (pueblo; macizo) | Unos 150 de alto | Pie del poste en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Sacks` | Instancias | Dos o tres sacos de arpillera atados (pueblo; macizos) | Unos 110 de ancho, 55 a 75 de alto | Centro del grupo en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.FlowerPot` | Instancias | Maceta de barro con un arbusto redondo y flores (pueblo; maciza) | Radio 33, unos 100 de alto | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Shell` | Instancias | Concha: caracola, vieira o grupo de conchitas (playa) | 20 a 45 | Centro en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Starfish` | Instancias | Estrella de mar de cinco brazos (playa) | 44 de punta a punta | Centro en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.SandBucket` | Instancias | Cubo de playa con asa y pala, junto a un montoncito de arena (playa) | Unos 100 x 60, 28 de alto | Centro del cubo en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.BeachTowel` | Instancias | Toalla de playa a franjas con un borde doblado (playa) | Unos 180 x 90 | Centro en el suelo; +X a lo largo de la toalla; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Surfboard` | Instancias | Tabla de surf clavada en la arena, algo inclinada (playa) | 210 de largo | Pie de la tabla en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Parasol` | Instancias | Sombrilla de playa a gajos con hamaca (playa) | 250 de ancho, unos 230 de alto | Pie del mástil en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Driftwood` | Instancias | Tronco a la deriva blanqueado por el sol (playa, laguna y manglar) | 140 a 260 de largo | Centro en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Coconuts` | Instancias | Dos a cuatro cocos y uno abierto (playa) | Unos 60 de ancho | Centro del grupo en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Lifebuoy` | Instancias | Salvavidas colgado de un poste (playa) | Poste de unos 170, aro de 78 | Pie del poste en el suelo; +X hacia el aro; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Mushrooms` | Instancias | Grupo de tres a seis setas (selva) | Unos 60 de ancho y 34 de alto | Centro del grupo en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.ClayPot` | Instancias | Vasija de barro, a veces rota con trozos (selva; maciza) | 50 de ancho, 58 de alto | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.TikiTorch` | Instancias | Antorcha tiki de caña con cesta y llama (selva) | 170 a 210 de caña, unos 260 con la llama | Pie de la caña en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.SkullPost` | Instancias | Poste tribal con calavera y plumas (selva) | 150 a 200 de alto | Pie del poste en el suelo; +X hacia donde mira la calavera; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.CattleSkull` | Instancias | Calavera de vaca con cuernos tirada en el suelo (desierto) | Unos 60 de ancho (hasta 85) | Centro en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Bones` | Instancias | Huesos sueltos: costillar en arco y fémures (desierto) | Unos 150 de largo | Centro en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Amphora` | Instancias | Ánfora de dos asas, de pie o tumbada (desierto; maciza) | 76 de alto, 42 de ancho | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.WagonWheel` | Instancias | Rueda de carro apoyada, tumbada o rota (desierto) | 110 de diámetro | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Signpost` | Instancias | Poste indicador con dos o tres flechas (desierto, laguna y roca) | Unos 200 de alto | Pie del poste en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Tumbleweed` | Instancias | Planta rodadora: bola de ramitas secas (desierto) | Radio 28 a 44 | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Crystals` | Instancias | Grupo de cristales del color del bioma (desierto y roca) | Aguja mayor de 70 a 90 | Centro del grupo en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Stump` | Instancias | Tocón con raíces y anillos (selva, laguna, roca y manglar; macizo) | Radio 26 a 38, 30 a 66 de alto | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Cairn` | Instancias | Hito de piedras apiladas (roca; macizo) | Unos 60 de ancho y 80 de alto | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.Lantern` | Instancias | Farol colgado de un poste con brazo (laguna, roca y manglar) | 190 a 230 de alto, brazo de 40 | Pie del poste en el suelo; +X hacia el farol; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Flora.CrabTrap` | Instancias | Nasa de pescador de listones con red y un flotador (laguna y manglar; maciza) | 70 x 50 x 45 | Centro de la base en el suelo; giro al azar en Z; cada copia lleva su escala | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `Lobby.Tutorial.Fauna.Monkey` | Instancias | Mono de la selva del tutorial | 70 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Toucan` | Instancias | Tucán de la selva del tutorial | 50 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.DartFrog` | Instancias | Rana dardo azul de la selva del tutorial | 16 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Capybara` | Instancias | Capibara de la selva del tutorial | 110 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Crab` | Instancias | Cangrejo rojo de la playa del tutorial | 22 de ancho y 16 de fondo a escala 1; cada copia lleva su escala | Centro del caparazón (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia los ojos | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.BabyTurtle` | Instancias | Cría de tortuga de la playa del tutorial | 22 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Gull` | Instancias | Gaviota de la playa del tutorial (con sustituto no abre las alas al huir) | 45 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Sandpiper` | Instancias | Correlimos de la playa del tutorial | 25 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Lizard` | Instancias | Lagartija turquesa de la roca y el desierto del tutorial | 45 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Meerkat` | Instancias | Suricato del desierto del tutorial | 50 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Roadrunner` | Instancias | Correcaminos del desierto del tutorial | 55 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Vulture` | Instancias | Buitre del desierto del tutorial | 75 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Ibex` | Instancias | Cabra montés de la roca del tutorial | 130 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Marmot` | Instancias | Marmota de la roca del tutorial | 55 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Eagle` | Instancias | Águila de la roca del tutorial | 80 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.FiddlerCrab` | Instancias | Cangrejo violinista del manglar del tutorial | 15 de ancho a escala 1; cada copia lleva su escala | Centro del caparazón (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia los ojos | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.TreeFrog` | Instancias | Rana de ojos rojos del manglar del tutorial | 18 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Mudskipper` | Instancias | Pez del fango que brinca por la orilla del manglar del tutorial | 22 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Cat` | Instancias | Gato del pueblo del tutorial | 75 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Pigeon` | Instancias | Paloma del pueblo del tutorial (con sustituto no abre las alas al huir) | 32 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Hen` | Instancias | Gallina del pueblo del tutorial | 45 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.Fauna.Rabbit` | Instancias | Conejo del pueblo del tutorial | 40 de largo a escala 1; cada copia lleva su escala | Centro del cuerpo (el pivote de su pieza Body, a la altura BodyZ de la ficha sobre los pies); +X hacia la cabeza | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.DummyCrab.Shell` | Componente | Caparazón con ojos del cangrejo de prácticas: va de lado a lado y se marea (la colisión es una caja aparte) | 22 x 16 x 10 a escala 1; el muñeco va a escala 5 (unos 110 x 80) | Centro del caparazón; +X hacia los ojos (hacia quien llega) | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.DummyCrab.LegsLeft` | Componente | Las tres patas del lado izquierdo del cangrejo de prácticas (se mueven juntas) | Unos 16 x 14 a escala 1 (el muñeco, x5) | Cadera, a la izquierda del caparazón; +X hacia los ojos | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.DummyCrab.LegsRight` | Componente | Las tres patas del lado derecho del cangrejo de prácticas (se mueven juntas) | Unos 16 x 14 a escala 1 (el muñeco, x5) | Cadera, a la derecha del caparazón; +X hacia los ojos | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.DummyCrab.ClawLeft` | Componente | Pinza izquierda del cangrejo de prácticas (se abre y se cierra) | Unos 12 de largo a escala 1 (el muñeco, x5) | Hombro de la pinza; +X hacia delante (gira en Y) | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.DummyCrab.ClawRight` | Componente | Pinza derecha del cangrejo de prácticas (se abre y se cierra) | Unos 12 de largo a escala 1 (el muñeco, x5) | Hombro de la pinza; +X hacia delante (gira en Y) | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `Lobby.Tutorial.DummyCrab.Stars` | Componente | Corro de tres estrellas doradas que gira sobre el cangrejo de prácticas cuando está mareado (material que brilla) | Unos 130 de ancho | Centro del corro (gira sobre Z) | `Lobby/TN_TutorialPractice.cpp` |

#### Lobby.Playground

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Lobby.Playground.BucketPost` | Componente | Poste de cubos de playa boca abajo apilados (colores alternos, asa y nervios) con una estrella de mar en la cima | Radio 64 abajo y 49 arriba, 180 de alto (3 cubos de 60); se estira en alto con PostHeight | Centro de la base en el suelo; +X hacia delante del actor | `Lobby/Playground/TN_PlaygroundPiece.cpp` |
| `Lobby.Playground.PopsicleSteps` | Componente | Escalón de 3 bloques forrados de palitos de helado (algunos teñidos), cada uno 36 más alto que el anterior | 180 de largo (3 x 60), 120 de ancho y 108 de alto; se estira a StepCount x StepDepth, StepWidth y StepCount x StepHeight | Borde de abajo del primer escalón (X = 0) a ras de suelo y centrado en el ancho; +X hacia donde suben los escalones | `Lobby/Playground/TN_PlaygroundPiece.cpp` |
| `Lobby.Playground.ShellSlide` | Componente | Tobogán de vieira tumbada (nácar arriba, costillas de color por debajo) sobre un montón de arena, con plataforma de arena en la charnela, almenas, banderín y escalera de arena hacia atrás | Concha de 151 de largo y 160 de alto en la charnela (46 grados), labio de 190 de ancho; plataforma de 90 de fondo y 140 de ancho; escalera de 3 peldaños de 45; se estira con SlideHeight, SlideAngle y SlideWidth | Charnela de la concha (X = 0) a ras de suelo y centrada en Y; +X cuesta abajo hacia el labio, la plataforma y la escalera quedan en -X | `Lobby/Playground/TN_PlaygroundPiece.cpp` |
| `Lobby.Playground.ShellSlideNoSteps` | Componente | El mismo tobogán de vieira pero sin la escalera de arena de detrás (bSlideSteps desactivado) | Igual que ShellSlide sin la escalera: concha de 151 de largo y 160 de alto, plataforma de 90 de fondo | Igual que ShellSlide: charnela (X = 0) a ras de suelo; +X cuesta abajo | `Lobby/Playground/TN_PlaygroundPiece.cpp` |
| `Lobby.Playground.CookiePlatform` | Componente | Galleta de chocolate grande con pepitas y azúcar sobre una columna de galletas rellenas de crema | Galleta de 230 de diámetro y 17 de grueso sobre columna de 104 de diámetro, 140 de alto; se estira con CookieRadius y PlatformHeight | Centro de la columna en el suelo; +X hacia delante del actor | `Lobby/Playground/TN_PlaygroundPiece.cpp` |
| `Lobby.Playground.CastleTunnel` | Componente | Túnel de castillo de arena: muro con almenas y marcas de cubo, bocas en arco con reborde y concha, bóveda lisa por dentro, camino de piedrecitas y banderín en lo alto | 220 de largo, 264 de ancho y 262 de alto (hueco de 184 de ancho y 178 a 225 de alto), con banderín hasta 372; se estira en largo con TunnelLength | Centro del túnel en el suelo; el túnel va a lo largo de X (bocas en X = ±110) | `Lobby/Playground/TN_PlaygroundPiece.cpp` |
| `Lobby.Playground.SpadeSpinnerHub` | Componente | Cubo del centro de la barra giratoria de pala: tronco de cono de color con reborde (no gira; las palas, el collar y la tapa son SpadeSpinnerArms) | Radio 40 abajo y 32 arriba, 50 de alto | Centro de la base en el suelo; eje de giro de las palas | `Lobby/Playground/TN_PlaygroundPiece.cpp` |
| `Lobby.Playground.SpadeSpinnerArms1` | Componente | Parte que gira de la barra de pala con 1 brazo: collar amarillo alrededor del cubo, tapa con remolino, mango con puño y cuello y hoja vertical redondeada | Brazo de 230 desde el eje, mango a 30 del suelo y hoja de 46 de largo y 39 de alto; se estira con ArmLength y ArmHeight | Eje de giro (centro del cubo) a ras de suelo; el brazo sale hacia +X en el ángulo de inicio y el código gira toda la pieza | `Lobby/Playground/TN_PlaygroundPiece.cpp` |
| `Lobby.Playground.SpadeSpinnerArms2` | Componente | Parte que gira de la barra de pala con 2 brazos (la de serie): collar amarillo, tapa con remolino y dos mangos con hoja redondeada, opuestos | Dos brazos de 230 desde el eje (460 de punta a punta), mango a 30 del suelo y hojas de 46 x 39; se estira con ArmLength y ArmHeight | Eje de giro (centro del cubo) a ras de suelo; un brazo hacia +X y el otro hacia -X, el código gira toda la pieza | `Lobby/Playground/TN_PlaygroundPiece.cpp` |
| `Lobby.Playground.SpadeSpinnerArms3` | Componente | Parte que gira de la barra de pala con 3 brazos: collar amarillo, tapa con remolino y tres mangos con hoja redondeada a 120 grados | Tres brazos de 230 desde el eje, mango a 30 del suelo y hojas de 46 x 39; se estira con ArmLength y ArmHeight | Eje de giro (centro del cubo) a ras de suelo; un brazo hacia +X y los otros a 120 y 240 grados, el código gira toda la pieza | `Lobby/Playground/TN_PlaygroundPiece.cpp` |
| `Lobby.Playground.Jellyfish.Bell` | Componente | Campana de gelatina de la medusa cama elástica con cara, motas, trébol de órganos y brillo (los tentáculos se mecen con una malla que se deforma en cada fotograma y no son sustituibles) | Radio 110 en el borde y 82 de alto sobre el borde, con Size 1; se escala con Size (0,5 a 2,5) | Centro del borde de la campana (a 42 de la arena con Size 1); +X hacia la cara; la campana se aplasta y rebota sobre este punto | `Lobby/Playground/TN_JellyfishTrampoline.cpp` |
| `Lobby.Playground.Bridge.Frame` | Componente | Marco del puente colgante: 4 postes con cuerda enrollada, bola y banderín, 2 torres de arena con almenas y estrellas, concha en la cara de fuera y escalera de arena (el tablero y las cuerdas se mueven en cada fotograma y no son sustituibles) | Con SpanLength 900, DeckHeight 220 y DeckWidth 110: 1630 de largo con las escaleras de 5 peldaños de 45, torres de 140 de fondo y 170 de ancho con la cima a 220, postes hasta 400; se estira con los tres | Centro del vano en el suelo; el puente va a lo largo de X (postes en X = ±462, tablero entre X = ±450 a 220 de alto) | `Lobby/Playground/TN_WobblyBridge.cpp` |
| `Lobby.Playground.Bridge.FrameNoStairs` | Componente | Marco del puente colgante con las torres de arena pero sin las escaleras (bStairs desactivado) | Igual que Bridge.Frame sin escaleras: 1180 de largo con SpanLength 900 | Igual que Bridge.Frame: centro del vano en el suelo, el puente a lo largo de X | `Lobby/Playground/TN_WobblyBridge.cpp` |
| `Lobby.Playground.Bridge.FramePosts` | Componente | Marco del puente colgante sin torres (bSandTowers desactivado): solo los 4 postes con cuerda, bola y banderín, que nacen del suelo | 4 postes de 330 de alto (hasta 400 con el banderín) a ±462 en X con SpanLength 900 | Igual que Bridge.Frame: centro del vano en el suelo, el puente a lo largo de X | `Lobby/Playground/TN_WobblyBridge.cpp` |

#### Lobby.Booth

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Lobby.Booth.Bottle` | Componente | Probador: media botella de plástico verde azulado boca abajo, con la pared curva, el hueco redondo de la puerta con su boca de vidrio grueso, el techo con el culo de cinco pies, la etiqueta de refresco (las letras son aparte), el canto del corte y dentro la tarima de tablones con una alfombra redonda | Radio 150 y 305 de alto en la pared, 375 con los pies del techo; hueco de la puerta de radio 88 centrado a 114 | Centro de la botella a ras de suelo; la puerta mira a +X | `Lobby/TN_ChangingBooth.cpp` |
| `Lobby.Booth.Door` | Componente | Puerta del probador: chapa de corona roja con anillo crema, estrella dorada en relieve, falda rizada de 21 pliegues y forro de dentro; gira sobre su bisagra al abrirse | Disco de unos 190 de diámetro y unos 25 de fondo con el relieve y el forro | Bisagra a ras de suelo, en el lado -Y de la chapa sobre la pared; ejes de la botella (cerrada, la cara mira a +X); se abre girando -108 grados en Z | `Lobby/TN_ChangingBooth.cpp` |

#### Lobby.Shop

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Lobby.Shop.Stall` | Componente | Puesto del tendero entero: mostrador de tablones con franja coral, toldo de rayas con volante de picos, cartel azul con marco dorado (el texto es aparte), guirnalda de banderines, estantería del fondo con botes de pintura, caparazones, tarros con ojos y cascos, perchero con sombreros, barril con pala, cajas con conchas, farol y cofre con monedas | Unos 800 de ancho, 360 de fondo y 380 de alto con StallScale 1 (el componente se escala con StallScale) | Suelo bajo el tendero (en el origen); la tienda mira a +X hacia los clientes; el mostrador está en X = 125 | `Lobby/TN_ShopKeeper.cpp` |

#### Lobby.Chest

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Lobby.Chest.Body` | Componente | Caja del cofre del tesoro: tablones, zócalo, borde dorado, cantoneras, flejes de hierro con remaches, cerradura dorada, asas y dentro la cama de monedas con montones, gemas y una copa | 100 de fondo, 156 de ancho y 64 de alto (la cerradura y los flejes asoman unos 6) | Centro del cofre a ras de suelo; +X es el frente (la cerradura) | `Lobby/TN_TreasureChest.cpp` |
| `Lobby.Chest.Lid` | Componente | Tapa de medio cañón del cofre: tablones, testeros con filo dorado, flejes con remaches, labio dorado, pasador, bisagras y una vieira dorada de emblema; se entreabre al rebuscar y salta hasta 108 grados al salir el objeto | 106 de fondo, 162 de ancho y 53 de alto cerrada (radio 53) | Bisagra: borde de atrás de la caja (50 por detrás del centro) a 64 de alto, centrada en Y; la tapa se extiende hacia +X y se abre girando en cabeceo (pitch) hacia atrás | `Lobby/TN_TreasureChest.cpp` |

#### Lobby.Briefing

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Lobby.Briefing.Tent` | Componente | Tienda militar del General Galápago en una sola malla: lona verde oliva con camuflaje, sacos terreros, cajas, vientos y postes; mesa con la maqueta del lobby sobre un tapete azul, pizarra de la orden del día en su caballete, farol, taza, puntero, cartel azul del frontón y bandera (los textos de la pizarra y del cartel son aparte) | Unos 640 de ancho, 455 de fondo (X de -175 a 280) y 440 hasta la cumbrera; el poste de la bandera llega a 670; mesa de 140 x 250 y 92 de alto | Suelo bajo el general (en el origen); la tienda mira a +X hacia los reclutas; la mesa está en X = 150; el texto de la pizarra va en (X 242, Y -215, Z 150) y el del cartel en (X 296, Z 295) | `Lobby/TN_GeneralBriefing.cpp` |

#### Lobby.ModeSelector

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Lobby.ModeSelector.Pedestal` | Componente | Peana de los dos selectores de modo y de dificultad de la misión (hoy el cilindro del motor); la etiqueta de texto flota encima | Modela como el cilindro del motor: 100 x 100 x 100 con el pivote en el centro; el componente lo escala (0,9; 0,9; 1,1) y sale de 90 x 90 x 110 | Centro del cilindro, a 55 del suelo; la hija de arte hereda la escala del componente | `Lobby/TN_ProcModeSelector.cpp` |

#### Lobby.CosmeticPreview

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Lobby.CosmeticPreview.Pedestal` | Componente | Islita de arena con canto de roca, una estrella de mar y dos conchas sobre la que posa la tortuga de la vista previa de cosméticos (solo se ve en la captura del escaparate) | Unos 132 de diámetro y 48 de alto (de -34 a 14 en Z) | Eje del giratorio del escaparate; la cara de arriba, donde pisa la tortuga, está a 12 sobre el origen; +X mira a la cámara | `Lobby/TN_CosmeticPreview.cpp` |

### Mapa procedural (DA_Arte_ProcMap) — 414 piezas

#### ProcMap.Tower

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Tower.Hollow` | Pieza | Torre hueca de entrada de un cruce: sillería en talud, puerta con túnel abovedado, rastrillo, dovelas y antorchas, sala con forjado y hueco del géiser, saeteras, pretil almenado y estandarte | Escala 1: radio 1100 (más 220 de vuelo arriba) y 3000 del suelo de la puerta a la cima; cada copia se estira a su radio y su alto (los muros bajan 3 m bajo el terreno) | Centro del suelo de la puerta (dentro de la torre); +X hacia la puerta | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Tower.Solid` | Pieza | Torre de salida de un cruce (puente o muralla): forro de sillería en talud sobre el pilar del terreno, cima enlosada, pretil almenado abierto al tablero o al adarve y estandarte | Escala 1: radio 1100 (más 220 de vuelo); pretil de 190 sobre la cima; baja hasta 3 m bajo el terreno (alto variable) | Centro de la cima (cota del tablero o del adarve); +X hacia donde va el camino | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |

#### ProcMap.Bridge

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Bridge.RopeDeck` | Pieza | Puente colgante: tablones sobre largueros y barandilla de cuerda con postes, con sus tramos hundidos (vigas, postes o cornisas de parkour) | Tan largo como el cruce; escala X 1 = 100 m de principio a fin; ancho del camino | Principio del tablero a su cota; +X hacia el final del tablero (sigue el camino, puede curvarse) | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.TrestleDeck` | Pieza | Puente de caballetes: tablones y barandilla rígida de madera, con sus tramos hundidos | Tan largo como el cruce; escala X 1 = 100 m de principio a fin; ancho del camino | Principio del tablero a su cota; +X hacia el final del tablero (sigue el camino, puede curvarse) | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.IronDeck` | Pieza | Puente de hierro: planchas y barandilla de hierro (abierta en la plaza), con sus tramos hundidos | Tan largo como el cruce; escala X 1 = 100 m de principio a fin; ancho del camino | Principio del tablero a su cota; +X hacia el final del tablero (sigue el camino, puede curvarse) | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.StoneDeck` | Pieza | Viaducto: losas y pretiles de piedra con albardilla (cortados en la plaza), con sus tramos hundidos de sillares | Tan largo como el cruce; escala X 1 = 100 m de principio a fin; ancho del camino más 64, pretiles de 105 | Principio del tablero a su cota; +X hacia el final del tablero (sigue el camino, puede curvarse) | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.StonePlaza` | Pieza | Plaza redonda de un viaducto: suelo de losas en anillos, pretil abierto al tablero, ménsula hasta la pila y fuente con la estatua de la tortuga y su agua (las farolas, los bancos y la atalaya van aparte) | Escala 1: radio 1000 (las copias van de 750 a unos 1300); ménsula 420 por debajo | Centro de la plaza a la cota del tablero; +X a lo largo del tablero | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.IronPlaza` | Pieza | Plaza redonda de un puente de hierro: suelo de chapa, barandilla de hierro, puntales hasta la pila y farol alto en el centro (las farolas, los bancos y la atalaya van aparte) | Escala 1: radio 1000 (las copias van de 750 a unos 1300); farol de 560 | Centro de la plaza a la cota del tablero; +X a lo largo del tablero | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.PlazaLamp` | Pieza | Farola de las plazas de los puentes, con su luz | Poste de 330 con farol hasta 380 | Pie de la farola en el suelo de la plaza; ejes del mapa | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.PlazaBench` | Pieza | Banco de las plazas de los puentes, mirando al paisaje | 170 x 50, 90 de alto | Centro del banco en el suelo; respaldo hacia +Y | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.PlazaLookout` | Pieza | Atalaya de la plaza: tres bloques de 1 m apilados, dos escalones de 1 m, banderín y la almohadilla de la medusa a su pie | 180 x 180 de planta y 300 de alto (banderín hasta 520); escalones hasta 270 hacia -X; almohadilla de radio 110 a 300 hacia +X | Centro de su base en la plaza; +X a lo largo del tablero (los escalones bajan hacia -X) | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.StoneArch` | Pieza | Arco rebajado de sillería bajo el viaducto entre dos pilas, con su rosca y sus tímpanos | Escala X 1 = 36 m de luz; flecha según el suelo (150 a 3200); ancho del tablero más 60 | Arranque del arco a la cota del tablero; +X hacia la otra pila | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.StonePier` | Pieza | Pila de sillería del viaducto entre dos arcos, con zócalo | 360 de grueso; escala 1 = 600 de ancho y 2000 hasta el pie | Centro de la cima de la pila (32 bajo el tablero); +X a lo largo del puente | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.TrestleBent` | Pieza | Caballete de vigas bajo el puente de caballetes: pies en talud, travesaño, riostras y cruces hasta el suelo | Escala 1 = 600 de ancho arriba y 2000 hasta el suelo (se abre hacia abajo) | Centro arriba, a la cota del tablero; +X a lo largo del puente | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.RopeMasts` | Pieza | Par de mástiles de madera en cada apoyo del puente colgante | Mástiles de 32 x 32 y 750 de alto (60 enterrados); escala Y 1 = 300 del eje a cada mástil | Centro del tablero en el apoyo; +X a lo largo del puente | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.IronPortal` | Pieza | Pórtico de hierro en cada apoyo del puente de hierro: dos mástiles y dintel | Mástiles de 32 x 32 y 750 de alto; escala Y 1 = 300 del eje a cada mástil | Centro del tablero en el apoyo; +X a lo largo del puente | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.RopeCables` | Pieza | Cables principales de un vano del puente colgante (los dos lados) con sus péndolas cada 3 m | Escala X 1 = 30 m de vano; cables de 750 en los apoyos a 130 en el centro; escala Y 1 = 300 del eje a cada cable | Tablero en el apoyo anterior; +X hacia el siguiente apoyo | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Bridge.IronChains` | Pieza | Cadenas de un vano del puente de hierro (los dos lados) con sus péndolas | Escala X 1 = 30 m de vano; cadenas de 750 en los apoyos a 130 en el centro; escala Y 1 = 300 del eje a cada cadena | Tablero en el apoyo anterior; +X hacia el siguiente apoyo | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |

#### ProcMap.Wall

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Wall.Rampart` | Pieza | Muralla colosal de un cruce: caras en talud en hiladas, adarve enlosado entre parapetos almenados, puerta altísima con bóveda y los mordiscos del adarve con sus escombros | Tan larga como el cruce; escala X 1 = 100 m de torre a torre; parapetos de 190 sobre el adarve; baja hasta 3 m bajo el terreno | Principio del adarve a su cota (junto a la torre de entrada); +X hacia la torre de salida (sigue el camino, puede curvarse) | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |

#### ProcMap.Lagoon

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Lagoon.Islet` | Pieza | Isleta de la laguna: prisma de roca de 12 lados entre los que se salta | Escala 1 = 10 x 10 m de planta (largo por el camino y ancho); baja hasta 13 m bajo el mar | Centro de la cima; +X a lo largo del camino | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Lagoon.Boardwalk` | Pieza | Pasarela de tablones sobre postes hundidos en el agua, con barandilla de cuerda | Escala X 1 = 20 m de principio a fin; ancho del camino; postes hasta 320 bajo el tablero | Principio de la pasarela a la cota del camino; +X hacia el final (sigue el camino) | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Lagoon.Algae` | Pieza | Mancha de algas flotando en las pozas del manglar y de las lagunas | Escala 1 = radio 160 (las copias van de 60 a 260); 6 de grueso | Centro de la mancha a ras del agua | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Lagoon.LilyPads` | Pieza | Mancha de nenúfares flotando en las pozas de las lagunas | Escala 1 = radio 160 (las copias van de 60 a 260); 6 de grueso | Centro de la mancha a ras del agua | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |

#### ProcMap.Gap

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Gap.WoodLip` | Pieza | Labio de madera de un hueco de salto (uno a cada lado): tablero que reduce la zanja al hueco exacto (selva, playa, pueblos) | Escala 1 = 300 de largo y 1000 de ancho (el del camino), 120 de alto | Centro de su cara de arriba (a ras del camino); +X hacia el hueco | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Gap.StoneLip` | Pieza | Labio de sillería de un hueco de salto (desierto, roca, volcán): bloques con junta y el borde del salto más claro | Escala 1 = 300 de largo y 1000 de ancho (el del camino), 120 de alto | Centro de su cara de arriba (a ras del camino); +X hacia el hueco | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Gap.LavaLip` | Pieza | Labio de roca del río de lava de una cueva, de pared a pared | Escala 1 = 300 de largo y 1000 de ancho (más 250 por cada lado), 180 de alto | Centro de su cara de arriba; +X hacia el río | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Gap.TrunkPost` | Pieza | Poste de un hueco con postes (selva, manglar): tronco con sombrero de musgo | Escala 1 = radio 50 y 500 de la cima al fondo de la zanja | Centro de la cima (a ras del camino, más o menos 30); +X a lo largo del hueco | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Gap.Piling` | Pieza | Poste de un hueco con postes (playa, pueblos): pilote con dos anillos de cuerda | Escala 1 = radio 50 y 500 de la cima al fondo de la zanja | Centro de la cima (a ras del camino, más o menos 30); +X a lo largo del hueco | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Gap.BasaltPost` | Pieza | Poste de un hueco con postes (volcán): columna hexagonal de basalto | Escala 1 = radio 50 y 500 de la cima al fondo de la zanja | Centro de la cima (a ras del camino, más o menos 30); +X a lo largo del hueco | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Gap.StonePost` | Pieza | Poste de un hueco con postes (resto de biomas): columna de sillería con capitel | Escala 1 = radio 50 y 500 de la cima al fondo de la zanja | Centro de la cima (a ras del camino, más o menos 30); +X a lo largo del hueco | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Gap.BalanceLog` | Pieza | Tronco de equilibrio de labio a labio (uno o dos por hueco), con la cima 10 sobre el camino | Radio 32; escala X 1 = 600 de largo | Centro del eje del tronco; +X de labio a labio | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Gap.DiveSign` | Pieza | Aviso del salto de panzazo: tres chevrones amarillos y rojos en el labio de llegada y cartel amarillo con una exclamación roja | Chevrones de hasta 640 de ancho; cartel de 110 x 90 en un poste de 205 | Centro del hueco a la cota del labio; +X a lo largo del hueco (chevrones y cartel hacia -X) | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Gap.LavaRiver` | Pieza | Superficie del río de lava de una cueva (lo que mata va aparte) | Escala 1 = 10 x 10 m; 10 de grueso | Centro de la superficie; +X a lo largo del hueco | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |

#### ProcMap.Lava

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Lava.Pool` | Pieza | Lago de lava (en un volcán o el lago de magma de una cueva) | Escala 1 = radio 500; 10 de grueso | Centro de la superficie | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |

#### ProcMap.River

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.River.Bridge` | Pieza | Puente de madera sobre el río: tablero y barandillas | Escala 1 = 2000 de largo y 600 de ancho; tablero de 90 de canto y barandillas de 90 | Centro del tablero a su cota (cara de arriba); +X a lo largo del puente | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.River.BrokenBridgeLeft` | Pieza | Puente del río roto con la escalera a la izquierda: dos medios tableros con bordes astillados y bandas de aviso, escalera de madera pegada al puente por +Y que baja al río (con rellano y puntales), barandilla abierta en el rellano y piedras en el agua | Escala 1 = 2000 de largo y 600 de ancho; hueco de 700 en el centro; la escalera baja hasta el lecho | Centro del tablero (en el hueco) a su cota; +X a lo largo del puente; el rellano queda hacia +X por el lado +Y | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.River.BrokenBridgeRight` | Pieza | Puente del río roto con la escalera a la derecha: igual que BrokenBridgeLeft con la escalera por -Y | Escala 1 = 2000 de largo y 600 de ancho; hueco de 700 en el centro; la escalera baja hasta el lecho | Centro del tablero (en el hueco) a su cota; +X a lo largo del puente; el rellano queda hacia +X por el lado -Y | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |

#### ProcMap.ClimbTower

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.ClimbTower.Crates` | Pieza | Torre de escalada de playa y pueblos: cajas de madera con cantoneras y aspas, escalones de 1 m, banderín y almohadilla de la medusa a su pie | 180 x 180 de planta; escala 1 = cuatro bloques de 100 (la de tres va a 0,75 de alto); escalones hasta 270 hacia -X; almohadilla de radio 110 a 300 hacia +X | Centro de su base en el suelo; +X hacia la medusa (los escalones bajan hacia -X) | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.ClimbTower.Stumps` | Pieza | Torre de escalada de selva y manglar: tocones anchos con musgo, escalones de 1 m, banderín y almohadilla de la medusa | 180 x 180 de planta; escala 1 = cuatro bloques de 100 (la de tres va a 0,75 de alto); escalones hasta 270 hacia -X; almohadilla de radio 110 a 300 hacia +X | Centro de su base en el suelo; +X hacia la medusa (los escalones bajan hacia -X) | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.ClimbTower.Basalt` | Pieza | Torre de escalada del volcán: columnas de basalto, escalones de 1 m, banderín y almohadilla de la medusa | 180 x 180 de planta; escala 1 = cuatro bloques de 100 (la de tres va a 0,75 de alto); escalones hasta 270 hacia -X; almohadilla de radio 110 a 300 hacia +X | Centro de su base en el suelo; +X hacia la medusa (los escalones bajan hacia -X) | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.ClimbTower.Slabs` | Pieza | Torre de escalada del bioma rocoso: losas de roca, escalones de 1 m, banderín y almohadilla de la medusa | 180 x 180 de planta; escala 1 = cuatro bloques de 100 (la de tres va a 0,75 de alto); escalones hasta 270 hacia -X; almohadilla de radio 110 a 300 hacia +X | Centro de su base en el suelo; +X hacia la medusa (los escalones bajan hacia -X) | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.ClimbTower.Sandstone` | Pieza | Torre de escalada del desierto y el resto de biomas: sillares de arenisca con cornisa, escalones de 1 m, banderín y almohadilla de la medusa | 180 x 180 de planta; escala 1 = cuatro bloques de 100 (la de tres va a 0,75 de alto); escalones hasta 270 hacia -X; almohadilla de radio 110 a 300 hacia +X | Centro de su base en el suelo; +X hacia la medusa (los escalones bajan hacia -X) | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |

#### ProcMap.PathProp

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.PathProp.CrateStack` | Pieza | Pila de cajas de madera en el camino | Escala 1 = semihuella 140 y 165 de alto (las copias van de 110 a 170 y de 100 a 230) | Centro en el suelo; +X por su eje | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.BarrelGroup` | Pieza | Grupo de barriles en el camino | Escala 1 = semihuella 125 y 105 de alto | Centro en el suelo; +X por su eje | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.Barricade` | Pieza | Vallas de obra atravesadas desde un borde del camino | Escala 1 = semilargo 165 (largo 260 a 400) y 125 de alto | Centro en el suelo; +X a lo largo de la valla (atravesada al camino) | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.HayBales` | Pieza | Pacas de paja en el camino | Escala 1 = semihuella 150 y 115 de alto | Centro en el suelo; +X por su eje | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.Sandcastle` | Pieza | Castillo de arena: muralla baja, cuatro torres almenadas, torre del homenaje y banderita | Escala 1 = semihuella 140 y 130 de alto | Centro en el suelo; +X por su eje | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.Rowboat` | Pieza | Barca de remos volcada (quilla arriba) con los remos al lado | Escala 1 = semilargo 210 (largo 380 a 460) y 80 de alto | Centro en el suelo; +X a lo largo de la barca | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.BeachSet` | Pieza | Rincón de playa: sombrilla con hamaca, toalla, cubo con pala y nevera | Escala 1 = semihuella 180 y 260 de alto | Centro en el suelo; +X por su eje | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.Totem` | Pieza | Tótem tallado: bloques pintados con caras, alas y pico arriba | Escala 1 = semihuella 70 y 350 de alto (280 a 420) | Centro en el suelo; +X hacia donde mira | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.RuinColumn` | Pieza | Columna en ruinas: basa, fuste acanalado roto y tambores caídos alrededor | Escala 1 = semihuella 180 y 270 de alto | Centro en el suelo; +X por su eje | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.GiantMushrooms` | Pieza | Corro de setas gigantes en el camino | Escala 1 = semihuella 180 y 230 de alto | Centro en el suelo; +X por su eje | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.SkullRock` | Pieza | Tres rocas con una calavera de vaca gigante encima | Escala 1 = semihuella 165 y 150 de alto | Centro en el suelo; +X hacia donde mira | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.PotteryJars` | Pieza | Grupo de vasijas y ánforas | Escala 1 = semihuella 110 y 100 de alto | Centro en el suelo; +X por su eje | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.CrystalSpikes` | Pieza | Agujas de cristal del color del bioma | Escala 1 = semihuella 140 y 230 de alto | Centro en el suelo; +X por su eje | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.Cairn` | Pieza | Hitos de piedras apiladas en el camino | Escala 1 = semihuella 90 y 210 de alto | Centro en el suelo; +X por su eje | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.MineCart` | Pieza | Vagoneta de mina sobre sus vías, con mineral y un pico apoyado | Escala 1 = semilargo 140 (largo 280) y 150 de alto | Centro en el suelo; +X a lo largo de las vías | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.CrabTraps` | Pieza | Nasas de pescador apiladas | Escala 1 = semihuella 120 y 110 de alto | Centro en el suelo; +X por su eje | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.MarketStall` | Pieza | Puesto de mercado: cuatro postes, toldo a franjas, mostrador con fruta y cajas debajo | Escala 1 = semihuella 200 y 270 de alto | Centro en el suelo; +X hacia el mostrador | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.PathProp.ConeLine` | Pieza | Fila de conos de tráfico atravesada desde un borde del camino | Escala 1 = semilargo 190 (largo 300 a 450) y 70 de alto | Centro en el suelo; +X a lo largo de la fila (atravesada al camino) | `World/ProcMap/TN_ProcMapPropMeshes.h` |

#### ProcMap.Rock

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Rock.RoundBoulder` | Pieza | Peñasco redondeado | Escala 1 = radio 150 y 180 de alto (radios de 70 a 220, alto de 0,8 a 1,7 radios) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.SlabBoulder` | Pieza | Peñasco de losas inclinadas | Escala 1 = radio 150 y 180 de alto (radios de 70 a 220) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.SplitBoulder` | Pieza | Peñasco partido en dos | Escala 1 = radio 150 y 180 de alto (radios de 70 a 220) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.StackedBoulder` | Pieza | Peñascos apilados | Escala 1 = radio 150 y 180 de alto (radios de 70 a 220) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.StrataBoulder` | Pieza | Peñasco de estratos de colores | Escala 1 = radio 150 y 180 de alto (radios de 70 a 220) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.BasaltBoulder` | Pieza | Grupo de columnas de basalto (volcán) | Escala 1 = radio 150 y 180 de alto (radios de 70 a 220) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.MossyBoulder` | Pieza | Peñasco con musgo encima (selva) | Escala 1 = radio 150 y 180 de alto (radios de 70 a 220) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.CrystalBoulder` | Pieza | Peñasco con cristales | Escala 1 = radio 150 y 180 de alto (radios de 70 a 220) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.CoralBoulder` | Pieza | Roca de coral (playa) | Escala 1 = radio 150 y 180 de alto (radios de 70 a 220) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.Spire` | Pieza | Aguja de roca con sombrero en mitad de una explanada | Escala 1 = radio 240 y 950 de alto (radios de 150 a 320, alto de 500 a 1400) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.LeaningSpire` | Pieza | Aguja de roca inclinada | Escala 1 = radio 240 y 950 de alto (radios de 150 a 320, alto de 500 a 1400) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.TwinSpire` | Pieza | Agujas de roca gemelas | Escala 1 = radio 240 y 950 de alto (radios de 150 a 320, alto de 500 a 1400) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.HoodooSpire` | Pieza | Chimenea de hadas: roca en capas con sombrero | Escala 1 = radio 240 y 950 de alto (radios de 150 a 320, alto de 500 a 1400) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.KarstSpire` | Pieza | Pilar kárstico con vegetación | Escala 1 = radio 240 y 950 de alto (radios de 150 a 320, alto de 500 a 1400) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.OrganSpire` | Pieza | Órgano de basalto (columnas altas) | Escala 1 = radio 240 y 950 de alto (radios de 150 a 320, alto de 500 a 1400) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.Mogote` | Pieza | Mogote: roca baja y ancha de cima redondeada | Escala 1 = radio 540 y 410 de alto (radios de 380 a 700, alto de 220 a 600) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.Tor` | Pieza | Tor: bloques de granito apilados | Escala 1 = radio 540 y 410 de alto (radios de 380 a 700, alto de 220 a 600) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.StrataMesa` | Pieza | Mesa baja de estratos | Escala 1 = radio 540 y 410 de alto (radios de 380 a 700, alto de 220 a 600) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |
| `ProcMap.Rock.LavaDome` | Pieza | Domo de lava (volcán) | Escala 1 = radio 540 y 410 de alto (radios de 380 a 700, alto de 220 a 600) | Centro de la base sobre el terreno, con un giro al azar | `World/ProcMap/TN_ProcMapRockMeshes.h` |

#### ProcMap.Tree

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Tree.FallenLog` | Pieza | Tronco caído atravesado en el camino (se salta); carbonizado en el volcán | Escala 1 = 400 de largo y radio 45 (largo según el ancho del camino, radio de 35 a 55) | Centro del eje del tronco; +X a lo largo, con su inclinación | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Tree.GiantSequoia` | Pieza | Secuoya gigante del manglar: tronco con base ensanchada, raíces zancudas en arco y copa cónica por capas | Escala 1 = radio 200 y 3900 de alto (radios de 90 a 320, alto de 17 a 22 radios); raíces hasta 3,8 radios | Centro del tronco a ras del suelo | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |

#### ProcMap.Formation

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Formation.StoneArch` | Pieza | Arco natural de roca que cruza el camino (arenisca, granito, musgo u obsidiana según el bioma) | Escala 1 = 350 de fondo (X), 1700 entre pies (Y) y 1100 de alto | Centro en el suelo del camino bajo el arco; +X a lo largo del camino | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.WhaleRibs` | Pieza | Costillar de ballena que cruza el camino (playa) | Escala 1 = 1100 de fondo (X), 1700 entre pies (Y) y 1100 de alto | Centro en el suelo del camino bajo el arco; +X a lo largo del camino | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.RootArch` | Pieza | Raíces gigantes en arco sobre el camino (manglar) | Escala 1 = 350 de fondo (X), 1700 entre pies (Y) y 1100 de alto | Centro en el suelo del camino bajo el arco; +X a lo largo del camino | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.TempleGate` | Pieza | Pórtico de templo en ruinas sobre el camino (selva) | Escala 1 = 350 de fondo (X), 1700 entre pies (Y) y 1100 de alto | Centro en el suelo del camino bajo el arco; +X a lo largo del camino | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.FallenTrunk` | Pieza | Tronco colosal caído de pared a pared sobre el camino, con raíces, musgo y lianas | Escala 1 = 350 de fondo (X), 1700 entre pies (Y) y 1100 de alto | Centro en el suelo del camino bajo el tronco; +X a lo largo del camino | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.RuinedAqueduct` | Pieza | Tramo de acueducto en ruinas que cruza el cañón | Escala 1 = 350 de fondo (X), 1700 entre pies (Y) y 1100 de alto | Centro en el suelo del camino bajo el arco; +X a lo largo del camino | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.Shipwreck` | Pieza | Barco varado de costado con el mástil roto (playa) | Escala 1 = radio 800 y 450 de alto | Centro en el suelo del camino; +X por su eje | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.StoneHead` | Pieza | Cabeza colosal de piedra (selva) | Escala 1 = radio 300 y 520 de alto | Centro en el suelo del camino; +X hacia donde mira | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.BasaltColumns` | Pieza | Columnas hexagonales de basalto (volcán) | Escala 1 = radio 420 y 520 de alto | Centro en el suelo del camino; +X por su eje | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.Fumarole` | Pieza | Cono de fumarola con azufre (volcán) | Escala 1 = radio 320 y 220 de alto | Centro en el suelo del camino; +X por su eje | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.Hoodoo` | Pieza | Chimenea de hadas: roca en capas con sombrero (desierto, roca) | Escala 1 = radio 200 y 850 de alto | Centro en el suelo del camino; +X por su eje | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.BalancedRock` | Pieza | Peñasco en equilibrio sobre un pedestal (desierto, roca) | Escala 1 = radio 250 y 650 de alto | Centro en el suelo del camino; +X por su eje | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.Wagon` | Pieza | Carreta de lona abandonada (desierto) | Escala 1 = radio 300 y 300 de alto | Centro en el suelo del camino; +X a lo largo de la carreta | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.Cannon` | Pieza | Cañón antiguo con balas apiladas | Escala 1 = radio 260 y 200 de alto | Centro en el suelo del camino; +X hacia donde apunta | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.Sandbags` | Pieza | Parapeto de sacos terreros | Escala 1 = radio 380 y 110 de alto | Centro en el suelo del camino; +X por su eje | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.Bunker` | Pieza | Búnker de hormigón con tronera | Escala 1 = radio 400 y 260 de alto | Centro en el suelo del camino; +X hacia la tronera | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.WatchTower` | Pieza | Torre de vigía de madera | Escala 1 = radio 250 y 850 de alto | Centro en el suelo del camino; +X por su eje | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.TankWreck` | Pieza | Carro de combate abandonado | Escala 1 = radio 380 y 280 de alto | Centro en el suelo del camino; +X hacia delante del carro | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.GiantShell` | Pieza | Caracola gigante de pie sobre su boca (playa) | Escala 1 = radio 360 y 650 de alto | Centro en el suelo del camino; +X por su eje | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.Anchor` | Pieza | Ancla oxidada clavada en la arena con su cadena (playa) | Escala 1 = radio 300 y 600 de alto | Centro en el suelo del camino; +X por su eje | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.StoneCircle` | Pieza | Círculo de piedras en pie con dinteles y altar (se cruza por los huecos) | Escala 1 = radio 680 y 380 de alto | Centro del círculo en el suelo del camino; +X por su eje | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.Obelisk` | Pieza | Obelisco con bandas de símbolos y punta dorada (desierto) | Escala 1 = radio 180 y 1150 de alto | Centro en el suelo del camino; +X por su eje | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.FossilSkull` | Pieza | Cráneo fósil gigante medio enterrado, con cuernos (desierto) | Escala 1 = radio 400 y 320 de alto | Centro en el suelo del camino; +X hacia donde mira | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.ObsidianSpires` | Pieza | Agujas de obsidiana (volcán) | Escala 1 = radio 390 y 750 de alto | Centro en el suelo del camino; +X por su eje | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.ColossalTurtle` | Pieza | Tortuga colosal de piedra sobre su pedestal (selva, desierto) | Escala 1 = radio 420 y 460 de alto | Centro en el suelo del camino; +X hacia donde mira | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.WaterTower` | Pieza | Depósito de agua de madera sobre patas (zona humana) | Escala 1 = radio 300 y 1000 de alto | Centro en el suelo del camino; +X por su eje | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.Pyramid` | Pieza | Pirámide escalonada con escalinata, hito lejano sin colisión (selva) | Escala 1 = radio 2000 y 1650 de alto | Centro en el terreno; +X hacia el camino más cercano | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.Lighthouse` | Pieza | Faro a rayas junto a la costa, hito lejano sin colisión (playa) | Escala 1 = radio 380 y 3000 de alto | Centro en el terreno; +X hacia el camino más cercano | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.Mesa` | Pieza | Mesa de techo plano con estratos, hito lejano sin colisión (desierto) | Escala 1 = radio 3750 y 2650 de alto | Centro en el terreno; +X hacia el camino más cercano | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.SeaStack` | Pieza | Farallón en el mar, hito lejano sin colisión (playa, roca) | Escala 1 = radio 650 y 2500 de alto (sale del agua) | Centro en el terreno (el fondo del mar); +X hacia el camino más cercano | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.CastleRuin` | Pieza | Castillo en ruinas con torre, hito lejano sin colisión (roca) | Escala 1 = radio 1850 y 1300 de alto | Centro en el terreno; +X hacia el camino más cercano | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.Windmill` | Pieza | Molino de viento, hito lejano sin colisión (zona humana) | Escala 1 = radio 420 y 1600 de alto | Centro en el terreno; +X hacia el camino más cercano | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Formation.StiltHut` | Pieza | Palafito de pescador, hito lejano sin colisión (manglar, lagunas) | Escala 1 = radio 420 y 650 de alto | Centro en el terreno; +X hacia el camino más cercano | `World/ProcMap/TN_ProcMapFormationMeshes.h` |

#### ProcMap.Finish

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Finish.Gate` | Pieza | Meta: neumático en arco sobre la línea con rótulos, pasarela a cuadros con el cartel de META, rótulo colgante, banderas a cuadros, boyas en toda la boca, banderines y banderolas por la playa | Arco del radio de la meta y boca de la playa de lado a lado (decenas de metros) | Centro de la línea a nivel del mar; +X a lo largo de la meta (hacia el mar) | `World/ProcMap/TN_ProcMapFinishMeshes.h` |

#### ProcMap.Slide

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Slide.WaterSheet` | Pieza | Lámina de agua del tobogán sobre la ladera, con UV de flujo para M_ProcCascade y espuma en los bordes | Escala X 1 = 20 m en planta; 78 % del ancho del camino; sigue la ladera | Labio del tobogán a su cota; +X hacia el aterrizaje | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Slide.Pool` | Pieza | Poza al pie del tobogán: disco de agua con espuma en la orilla | Escala 1 = radio 350 | Centro a la cota del agua; +X hacia donde corre el agua | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |

#### ProcMap.Cave

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Cave.Roof` | Pieza | Techo de roca del túnel de una cueva: bóveda sobre el camino de boca a boca | Tan largo como el túnel; escala X 1 = 50 m de boca a boca; ancho del camino | Suelo de la boca de entrada; +X hacia la boca de salida (sigue el camino) | `World/ProcMap/TN_ProcMapCaveMeshes.h` |
| `ProcMap.Cave.MountainCap` | Pieza | Tapa de montaña sobre el túnel: une las laderas de los dos lados por encima del techo, con hierba en lo llano y roca en lo empinado | Tan larga como el túnel; escala X 1 = 50 m de boca a boca; ancho del camino más 14 m | Suelo de la boca de entrada; +X hacia la boca de salida (sigue el camino) | `World/ProcMap/TN_ProcMapGenerator_Build.cpp` |
| `ProcMap.Cave.TurtleStatue` | Pieza | Estatua de la tortuga en la cámara más ancha: pedestal escalonado, caparazón con escudos, ojos que brillan, corona según el estilo y ofrendas | Unos 280 x 250 de planta y 250 de alto a escala 1 (las copias van de 0,85 a 1,25) | Centro de la base; +X hacia donde mira | `World/ProcMap/TN_ProcMapFormationMeshes.h` |
| `ProcMap.Cave.Skylight` | Pieza | Lucernario: hueco luminoso en la clave, haz de luz translúcido hasta el suelo y claro de hierba con flores donde da | Hueco de radio 125; haz de radio 80 arriba a 175 abajo; claro de radio 190; escala Z 1 = 1000 del suelo a la clave | Centro del suelo bajo el hueco; +X a lo largo del túnel | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.Pool` | Pieza | Poza de la cueva: disco de agua con orilla de espuma y borde de piedras planas | Escala 1 = radio 240 | Centro de la poza en el suelo | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.Stele` | Pieza | Estela contra la pared: losa de canto redondeado sobre un zócalo, con marco y símbolos | Escala 1 = 120 de ancho y 190 de alto (más 12 de zócalo); 16 de grueso | Pie de la estela; +X hacia donde mira su cara (dentro del túnel) | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.CrystalCluster` | Pieza | Racimo de cristales que brillan, inclinados hacia fuera de la pared | Escala 1 = cristal mayor de 200 (los de los pasos, 50 a 90; los de las cámaras, 160 a 340) | Pie del racimo; +X hacia dentro del túnel | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.Column` | Pieza | Columna de caliza del suelo a la bóveda (estalactita y estalagmita unidas) | Radio de 40 a 75; escala Z 1 = 600 del suelo a donde entra en la bóveda | Pie de la columna | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.Stalagmite` | Pieza | Estalagmita: cono irregular de punta fina | Escala 1 = 150 de alto (radio de un 20 %); las copias van de 35 a 340 | Pie de la estalagmita | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.Stalactite` | Pieza | Estalactita grande que cuelga de la bóveda de las cámaras | Escala 1 = 230 de largo (50 dentro de la roca) | Donde nace, 50 por dentro de la bóveda; cuelga hacia -Z | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.GlowMushrooms` | Pieza | Corro de setas que brillan (selva): pies claros y sombreros luminosos | Escala 1 = 80 (la seta mayor llega a ese alto; corro de radio 50) | Centro del corro en el suelo | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.Vine` | Pieza | Liana con hojitas que cuelga de la bóveda o de la boca | Escala 1 = 150 de largo | Donde cuelga; baja hacia -Z | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.TemplePillar` | Pieza | Pilar de templo hasta la bóveda: basa, fuste con dos bandas y capitel | Fuste de 64 x 64; escala Z 1 = 600 hasta la bóveda | Pie del pilar; +X a lo largo del túnel | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.TemplePillarBroken` | Pieza | Pilar de templo roto: basa, medio fuste con pedazos arriba y un tambor caído al lado | Fuste de 64 x 64; escala Z 1 = 600 (la altura del pilar entero) | Pie del pilar; +X a lo largo del túnel (el tambor cae hacia +X) | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.Pilaster` | Pieza | Pilastra pegada a la pared en los pasos del templo, con capitel | 68 x 36; escala Z 1 = 600 hasta la bóveda | Pie de la pilastra; +X a lo largo del túnel | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.WallTorch` | Pieza | Antorcha de pared con su llama (la luz y las brasas van aparte) | Soporte de 16 x 20 x 32, palo de 45 y llama de 30 | Pie del soporte en la pared; +X hacia dentro del túnel | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.ClayPot` | Pieza | Vasija de barro contra la pared del templo (a veces rota) | 58 de alto a escala 1 (las de los pasos, a 0,8) | Pie de la vasija, con su giro | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.Amphora` | Pieza | Ánfora de dos asas contra la pared del templo (de pie o tumbada) | 76 de alto a escala 1 (las de los pasos, a 0,8) | Pie del ánfora, con su giro | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.Bones` | Pieza | Huesos sueltos contra la pared del templo: costillar y fémures | Unos 80 x 70 a escala 1 (los de los pasos, a 0,8) | Centro del grupo en el suelo, con su giro | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.ObsidianShards` | Pieza | Esquirlas de obsidiana contra la pared del tubo de lava (de tres a seis) | Escala 1 = esquirla mayor de 150 (las copias van de 40 a 260) | Pie de la esquirla mayor; +X hacia dentro del túnel | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.BasaltColumn` | Pieza | Columna hexagonal de basalto contra la pared del tubo de lava o a los lados de su boca | Escala 1 = radio 36 y 250 de alto, medidos en su caja (radios de 26 a 50, alto de 80 a 480) | Pie de la columna (centro de su caja), sin giro | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.LooseStone` | Pieza | Piedra suelta al pie de la pared | Escala 1 = radio 25 y 25 de alto, medidos en su caja (las copias van de 12 a 34) | Centro de la base (de su caja), sin giro | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.MagmaRock` | Pieza | Roca de basalto del anillo del lago de magma | Escala 1 = radio 65 y 65 de alto, medidos en su caja (las copias van de 40 a 95) | Centro de la base (de su caja), sin giro | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.TempleGate` | Pieza | Portada de la boca del templo: dos pilares, dintel con símbolos y cabeza de la tortuga en la clave | Escala 1 = 700 del eje a cada pilar y 800 de alto bajo el dintel (dintel de 140 de canto) | Centro del umbral en el suelo, 70 fuera de la boca; +X hacia fuera de la cueva | `World/ProcMap/TN_ProcMapCaveDecor.h` |
| `ProcMap.Cave.MouthBoulder` | Pieza | Peñasco a los lados de la boca de una cueva | Escala 1 = radio 70 y 70 de alto, medidos en su caja (radios de 45 a 95) | Centro de la base (de su caja), sin giro | `World/ProcMap/TN_ProcMapCaveDecor.h` |

#### ProcMap.Flora

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Flora.BroadTree` | Instancias | Árbol de copa redonda (selva, lagunas, parques) | 820 a 1020 de alto, copa de radio 400 (escala 1) | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Ceiba` | Instancias | Ceiba, el gigante de la selva: raíces tabulares y copa en parasol | 2100 a 2500 de alto, copa de radio 800, raíces hasta 320 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Palm` | Instancias | Palmera de tronco curvo con cocos | 780 a 980 de alto, copa inclinada 120 a 260 hacia +X, frondas de 400 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.MangroveTree` | Instancias | Mangle: raíces zancudas en arco y copa ancha y baja | 650 a 820 de alto, raíces hasta 260, copa de radio 450 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.YoungSequoia` | Instancias | Secuoya joven de tronco rojizo y copa cónica | 1500 a 1800 de alto, copa de radio 420 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Cypress` | Instancias | Ciprés de pantano, cónico y esbelto, con neumatóforos | 1150 a 1400 de alto, radio 150 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Pine` | Instancias | Pino de pisos | 1000 a 1250 de alto, radio 280 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Fir` | Instancias | Abeto estrecho y denso | 1200 a 1450 de alto, radio 200 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Willow` | Instancias | Sauce llorón | 780 a 900 de alto, copa de radio 300 con ramas colgantes | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Acacia` | Instancias | Acacia de copa plana (desierto) | 600 a 720 de alto, copa de radio 450 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.DeadTree` | Instancias | Árbol seco sin hojas | 600 a 800 de alto, ramas de 230 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.CharredTree` | Instancias | Árbol calcinado con brasas en las grietas (volcán) | 510 a 680 de alto | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Ornamental` | Instancias | Árbol de parque de copa esférica con el tronco encalado | 580 de alto, copa de radio 170 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Fern` | Instancias | Helecho de frondas arqueadas | Radio 90 a 130, unos 80 de alto | Centro en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Bush` | Instancias | Arbusto de varias masas (a veces con bayas) | Radio 150, unos 150 de alto | Centro en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Grass` | Instancias | Mata de hierba | 45 a 88 de alto, radio 40 | Centro en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Flowers` | Instancias | Mata con flores de colores | Hasta 70 de alto, radio 40 | Centro en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Reeds` | Instancias | Juncos y eneas de la orilla y el agua somera | 150 a 250 de alto, radio 50 | Centro en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Saguaro` | Instancias | Cactus columnar con brazos | 460 a 620 de alto, brazos hasta 95 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.BarrelCactus` | Instancias | Cactus barril con flor | Radio 42, 75 de alto | Centro en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.DryBush` | Instancias | Matojo seco de ramitas | Radio 60, 30 de alto | Centro en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.AshBush` | Instancias | Arbusto de ceniza con ramitas secas (volcán) | Radio 150, unos 150 de alto | Centro en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Hedge` | Instancias | Seto recortado (zona humana) | 220 x 90, 125 de alto | Centro en el suelo; +X a lo largo del seto (cada ejemplar va girado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Umbrella` | Instancias | Sombrilla de playa con mástil y base (zona humana) | 250 de alto, radio 150 | Pie del mástil en el suelo; Z arriba (cada ejemplar va girado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Creeper` | Instancias | Enredadera o musgo: manta de hojas pegada a la pared | Radio 120, 15 de grueso | Centro de la manta; Z por la normal de la pared (la instancia la pega a ella) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.BananaPlant` | Instancias | Platanera de hojas enormes (selva), a veces con racimo | 190 a 240 de tronco y hojas de 170 a 230 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Bamboo` | Instancias | Mata de bambú con nudos y penachos | Cañas de 620 a 1050, mata de radio 50 | Centro de la mata en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.TreeFern` | Instancias | Helecho arbóreo | 280 a 420 de tronco, frondas de 190 a 250 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.SeaGrape` | Instancias | Uva de playa: arbolito bajo de hojas redondas | 220 a 320 de alto, copa de radio 220 | Centro en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Pandanus` | Instancias | Pándano de raíces zancudas y penachos de hojas afiladas | 420 a 560 de alto, penachos de 200 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.FanPalm` | Instancias | Palmito de hojas en abanico | Tronco de 70 a 150, hojas hasta 250 de alto | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Casuarina` | Instancias | Casuarina de ramillas colgantes (costa) | 850 a 1150 de alto, copa de radio 200 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.JoshuaTree` | Instancias | Árbol de Josué (desierto) | 420 a 600 de alto | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Birch` | Instancias | Abedul de tronco blanco (copa dorada en una variante) | 850 a 1150 de alto, copa de radio 200 | Pie del tronco en el suelo; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Rock` | Instancias | Peñasco suelto (con una piedra al lado en una variante) | Radio 100, 55 a 130 de alto | Centro de la base; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |
| `ProcMap.Flora.Stones` | Instancias | Corro de piedras pequeñas | Radio 60, piedras de 12 a 28 | Centro del corro; Z arriba (cada ejemplar va girado, inclinado y escalado) | `World/ProcMap/TN_ProcMapFloraMeshes.h` |

#### ProcMap.Prop

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Prop.Crate` | Instancias | Caja de madera con cantoneras y aspa (con colisión de caja) | Unos 100 de lado | Centro de la base; Z arriba (cada ejemplar va girado y escalado) | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.WoodBarrel` | Instancias | Barril de madera con aros (con colisión de caja) | Radio 40, 104 de alto | Centro de la base; Z arriba (cada ejemplar va girado y escalado) | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Barricade` | Instancias | Valla de obra a franjas con luz naranja (con colisión de caja) | 170 a 250 de largo, 122 de alto | Centro de la base; +X a lo largo de la valla | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.TrafficCone` | Instancias | Cono de tráfico (uno o dos; con colisión de caja) | 70 de alto, base de 44 | Centro de la base; Z arriba (cada ejemplar va girado y escalado) | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.HayBale` | Instancias | Paca de paja redonda o rectangular (con colisión de caja) | Rodillo de radio 70 y 120 de largo, o paca de 110 x 56 x 45 a 90 | Centro de la base; Z arriba (cada ejemplar va girado y escalado) | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Bench` | Instancias | Banco de parque con patas de hierro (con colisión de caja) | 170 (o 120) x 50, 90 de alto | Centro de la base; respaldo hacia +Y | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.LampPost` | Instancias | Farola con farol cálido o de brazo (con colisión de caja) | 330 a 390 de alto | Pie del poste; Z arriba | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Mailbox` | Instancias | Buzón de poste con banderita (con colisión de caja) | 150 de alto | Pie del poste; +X a lo largo del buzón | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Sacks` | Instancias | Dos o tres sacos de arpillera atados (con colisión de caja) | Grupo de radio 50, 55 a 75 de alto | Centro del grupo en el suelo | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.FlowerPot` | Instancias | Maceta de barro con arbusto y flores (con colisión de caja) | Radio 33, 85 de alto | Centro de la base | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Shell` | Instancias | Concha en la arena: caracola, vieira o grupo de conchitas | Unos 40 | Centro en el suelo; Z arriba (cada ejemplar va girado y escalado) | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Starfish` | Instancias | Estrella de mar de cinco brazos | Puntas a 22 del centro, 6 de alto | Centro en el suelo; +X hacia la primera punta | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.SandBucket` | Instancias | Cubo de playa con asa, pala y montoncito de arena | Cubo de radio 16 y 28 de alto; todo unos 100 x 50 | Centro del cubo en el suelo | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.BeachTowel` | Instancias | Toalla de playa a franjas con un borde doblado | 180 x 90 | Centro de la toalla en el suelo; +X a lo largo | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Surfboard` | Instancias | Tabla de surf clavada en la arena, algo inclinada | 210 de largo, 54 de ancho | Donde entra en la arena; se inclina hacia +X | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Parasol` | Instancias | Sombrilla de playa a gajos con hamaca | 230 de alto, radio 125; hamaca a 95 hacia +X | Pie del mástil en la arena | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Driftwood` | Instancias | Tronco a la deriva blanqueado por el sol | 140 a 260 de largo, radio 13 a 19 | Centro del tronco en el suelo; +X a lo largo | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Coconuts` | Instancias | Dos a cuatro cocos y uno abierto | Grupo de unos 60, cocos de radio 11 | Centro del grupo en el suelo | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Lifebuoy` | Instancias | Salvavidas colgado de un poste | Poste de 150 con aro de radio 30 | Pie del poste; el aro mira a +X | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Mushrooms` | Instancias | Grupo de tres a seis setas (rojas con motas, pardas o moradas) | 34 de alto, grupo de radio 30 | Centro del grupo en el suelo | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.ClayPot` | Instancias | Vasija de barro (rota en una variante; con colisión de caja) | 58 de alto, radio 25 | Centro de la base | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.TikiTorch` | Instancias | Antorcha tiki: caña, cesta y llama | 170 a 210 de caña y llama de 40 | Pie de la caña | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.SkullPost` | Instancias | Poste tribal con calavera y plumas | 150 a 200 de poste y calavera de 40 | Pie del poste; la calavera mira a +X | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.CattleSkull` | Instancias | Calavera de vaca con cuernos tirada en el suelo | Unos 70 de ancho con los cuernos (grande en una variante) | Centro en el suelo; el hocico hacia +X | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Bones` | Instancias | Huesos sueltos: costillar en arco y fémures | Unos 80 x 70 | Centro del grupo en el suelo | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Amphora` | Instancias | Ánfora de dos asas, de pie o tumbada (con colisión de caja) | 76 de alto (de pie) | Pie del ánfora | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.WagonWheel` | Instancias | Rueda de carro apoyada, tumbada o rota | Radio 55 | Punto de apoyo en el suelo | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Signpost` | Instancias | Poste indicador con dos o tres flechas | 200 de alto, flechas de 72 | Pie del poste | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Tumbleweed` | Instancias | Planta rodadora: bola de ramitas secas | Radio 28 a 44 | Punto de apoyo en el suelo | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Crystals` | Instancias | Grupo de agujas de cristal del color del bioma | 70 a 130 de alto | Centro del grupo en el suelo | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Stump` | Instancias | Tocón con raíces y anillos (carbonizado en el volcán; con colisión de caja) | Radio 26 a 38, 30 a 66 de alto | Centro de la base | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Cairn` | Instancias | Hito de piedras apiladas (con colisión de caja) | 60 a 110 de alto, radio 30 | Centro de la base | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.Lantern` | Instancias | Farol colgado de un poste con brazo | 190 a 230 de alto, farol a 40 hacia +X | Pie del poste | `World/ProcMap/TN_ProcMapPropMeshes.h` |
| `ProcMap.Prop.CrabTrap` | Instancias | Nasa de pescador con red y flotador (con colisión de caja) | 70 x 50 x 40 a 50, flotador a 45 hacia +X | Centro de la base | `World/ProcMap/TN_ProcMapPropMeshes.h` |

#### ProcMap.Start

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Start.Gatehouse` | Pieza | Puerta doble de la salida: suelo, dos fachadas con arco y cartel, sala con conchas y antorchas, dos torres grandes y dos torreones (las hojas van aparte) | 2060 de ancho con las torres, 700 de fondo, fachadas de 760 y torres de 1350; los muros bajan 450 bajo el suelo | Centro del umbral de la puerta 1 en el origen de la estructura; +Y hacia la puerta 2 (el camino) | `Lobby/TN_CastleKit.h` |
| `ProcMap.Start.EggMound` | Pieza | Montículo de dos alturas de la pila de huevos de la salida, con el escalón de la concha | Radio 480 y 60 de alto; piso alto de radio 175 y 200 de alto | Centro del montículo en el suelo (Z = 4); +X hacia el camino | `Lobby/TN_CastleKit.h` |
| `ProcMap.Start.EggCup` | Pieza | Base de cada huevo de la salida (8): media cáscara con borde en zigzag y nido de paja | Radio 97, 110 de alto | Centro de la base del huevo | `Lobby/TN_CastleKit.h` |
| `ProcMap.Start.EggLid` | Componente | Tapa de cada huevo de la salida (8); salta dando vueltas y se esfuma al romperse el huevo | Radio 96, 128 de alto | Centro de la costura (el borde de abajo) | `Lobby/TN_CastleKit.h` |
| `ProcMap.Start.GateLeaf` | Componente | Hoja de madera de la puerta doble de la salida (4); las de la puerta 2 giran al abrirse | 430 de ancho, 540 de alto, 30 de grueso | Bisagra (borde de abajo); la hoja va hacia +X | `Lobby/TN_CastleKit.h` |

#### ProcMap.Nest

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Nest.Base` | Componente | Base del nido de huevos (punto de reaparición); hoy un nido de arena de código con el hueco oscuro, dos vueltas de paja y dos estrellas y dos vieiras alrededor (la colisión es un cilindro invisible aparte) | Montículo de radio 155 y unos 40 de alto con la paja; con las estrellas y las vieiras, unos 420 de ancho. Escala 1 | Centro del nido en el suelo (origen del actor) | `World/ProcMap/TN_ProcEggNest.cpp` |
| `ProcMap.Nest.Egg` | Componente | Huevo del nido (8 en pila); hoy el huevo de código del lobby con su banda de color, que cambia de cáscara al activarse el nido (el cambio no llega a la malla de arte) | Huevo de radio 97 y 241 de alto con escala 0,3 (CodeArtEggScale; unos 58 x 72): la malla de arte hereda esa escala | Centro de la base del huevo | `World/ProcMap/TN_ProcEggNest.cpp` |

#### ProcMap.Puzzle

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Puzzle.ThrowWallBlock` | Componente | Muro de lanzamiento: el bloque (hoy el cubo del motor) | Cubo de 100 escalado a largo x ancho x alto del muro (desde 300 x 300 x 200): modélalo como un cubo de 100, la malla de arte hereda esa escala | Centro del cubo | `World/ProcMap/TN_ProcPuzzleActors.cpp` |
| `ProcMap.Puzzle.ThrowWallRamp` | Componente | Rampa del muro de lanzamiento que baja al pulsar el interruptor (hoy el cubo del motor); gira sobre su bisagra | Cubo de 100 escalado al largo de la rampa x 45 % del ancho del muro x 30: la malla de arte hereda esa escala | Centro del cubo (la bisagra es su padre). Sin colisión de arte: el código enciende y apaga la suya | `World/ProcMap/TN_ProcPuzzleActors.cpp` |
| `ProcMap.Puzzle.SabotageGate` | Componente | Compuerta de sabotaje que sube del suelo (hoy el cubo del motor) | Cubo de 100 escalado a 120 x ancho x alto de la compuerta: la malla de arte hereda esa escala | Centro del cubo | `World/ProcMap/TN_ProcPuzzleActors.cpp` |
| `ProcMap.Puzzle.Switch` | Componente | Interruptor de los puzles (hoy el cilindro del motor, rojo o verde según su objetivo; el tinte no llega a la malla de arte) | Cilindro de 100 x 100 con escala 0,6 x 0,6 x 0,4: la malla de arte hereda esa escala | Centro del cilindro (raíz del actor) | `World/ProcMap/TN_ProcPuzzleActors.cpp` |

#### ProcMap.Geyser

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Geyser.Mound` | Componente | Montículo de sínter del géiser: terrazas concéntricas, poza turquesa, boca y piedras alrededor (con colisión: se sube como un escalón) | Radio 300 (piedras hasta 350), 41 de alto | Centro de la base en el suelo | `World/ProcMap/TN_ProcTraversalActors.cpp` |
| `ProcMap.Geyser.Jet` | Componente | Chorro de agua del géiser (M_ProcCascade, agua que corre hacia arriba); el código lo estira con el pulso | Radio 95 en la base y 100 de alto a escala 1: el Tick lo escala a la altura del chorro | Centro de la base, 30 sobre el suelo; Z arriba | `World/ProcMap/TN_ProcTraversalActors.cpp` |
| `ProcMap.Geyser.FoamCap` | Componente | Corona de espuma en lo alto del chorro (sube y crece con él) | Racimo de bolas de radio 62, bolas de unos 50 | Centro del racimo, en la cima del chorro | `World/ProcMap/TN_ProcTraversalActors.cpp` |
| `ProcMap.Geyser.FoamRing` | Componente | Espuma de la boca del géiser (late con el chorro) | Anillo de radio 128 con bolas de unos 40 | Centro de la boca, 36 sobre el suelo | `World/ProcMap/TN_ProcTraversalActors.cpp` |

#### ProcMap.Water

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Water.CurrentMarker` | Componente | Marca de una corriente de agua: cono tumbado que avanza con ella (hoy el cono del motor; 3 por corriente) | Cono de 100 con escala 0,35 x 0,35 x 0,5: la malla de arte hereda esa escala | Centro del cono; el código lo tumba para que apunte con la corriente | `World/ProcMap/TN_ProcWaterActors.cpp` |
| `ProcMap.Water.WhirlpoolDisc` | Componente | Disco oscuro del remolino que gira (hoy el cilindro del motor) | Cilindro de 100 escalado a radio/50 x radio/50 x 0,02 (el diámetro del remolino y 2 de alto): la malla de arte hereda esa escala | Centro del disco, 3 sobre el agua | `World/ProcMap/TN_ProcWaterActors.cpp` |
| `ProcMap.Water.PredatorFin` | Componente | Aleta del depredador del agua (hoy el cono del motor) | Cono de 100 con escala 0,9 x 0,25 x 0,9: la malla de arte hereda esa escala | Centro, 45 sobre la raíz; +X hacia donde nada | `World/ProcMap/TN_ProcWaterActors.cpp` |
| `ProcMap.Water.PredatorBody` | Componente | Cuerpo del depredador bajo el agua (hoy la esfera del motor) | Esfera de 100 con escala 3,2 x 1,1 x 0,9: la malla de arte hereda esa escala | Centro, 40 bajo la raíz; +X hacia donde nada | `World/ProcMap/TN_ProcWaterActors.cpp` |
| `ProcMap.Water.Jellyfish` | Componente | Medusa saltarina (trampolín por arriba); hoy la esfera del motor o la malla de su variante, teñida (el tinte no llega a la malla de arte) | Esfera de 100 con escala 2,4 x 2,4 x 1,2 que se balancea y se aplasta: la malla de arte hereda esa escala | Centro del cuerpo | `World/ProcMap/TN_ProcWaterActors.cpp` |

#### ProcMap.Fauna

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `ProcMap.Fauna.Crab.Body` | Instancias | Cangrejo rojo de playa: cuerpo (pieza animada por código) | Escala 1: caparazón de 22 de ancho | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Crab.LegFL` | Instancias | Cangrejo rojo de playa: patas del lado izquierdo (pieza animada por código) | Escala 1: caparazón de 22 de ancho | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Crab.LegFR` | Instancias | Cangrejo rojo de playa: patas del lado derecho (pieza animada por código) | Escala 1: caparazón de 22 de ancho | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Crab.ClawL` | Instancias | Cangrejo rojo de playa: pinza izquierda (pieza animada por código) | Escala 1: caparazón de 22 de ancho | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Crab.ClawR` | Instancias | Cangrejo rojo de playa: pinza derecha (pieza animada por código) | Escala 1: caparazón de 22 de ancho | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.FiddlerCrab.Body` | Instancias | Cangrejo violinista (una pinza enorme): cuerpo (pieza animada por código) | Escala 1: caparazón de 15 de ancho | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.FiddlerCrab.LegFL` | Instancias | Cangrejo violinista (una pinza enorme): patas del lado izquierdo (pieza animada por código) | Escala 1: caparazón de 15 de ancho | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.FiddlerCrab.LegFR` | Instancias | Cangrejo violinista (una pinza enorme): patas del lado derecho (pieza animada por código) | Escala 1: caparazón de 15 de ancho | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.FiddlerCrab.ClawL` | Instancias | Cangrejo violinista (una pinza enorme): pinza izquierda (pieza animada por código) | Escala 1: caparazón de 15 de ancho | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.FiddlerCrab.ClawR` | Instancias | Cangrejo violinista (una pinza enorme): pinza derecha (pieza animada por código) | Escala 1: caparazón de 15 de ancho | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.BabyTurtle.Body` | Instancias | Cría de tortuga marina: cuerpo (pieza animada por código) | Escala 1: caparazón de 16 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.BabyTurtle.Head` | Instancias | Cría de tortuga marina: cabeza con el cuello (pieza animada por código) | Escala 1: caparazón de 16 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.BabyTurtle.LegFL` | Instancias | Cría de tortuga marina: aleta delantera izquierda (pieza animada por código) | Escala 1: caparazón de 16 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.BabyTurtle.LegFR` | Instancias | Cría de tortuga marina: aleta delantera derecha (pieza animada por código) | Escala 1: caparazón de 16 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.BabyTurtle.LegBL` | Instancias | Cría de tortuga marina: aleta trasera izquierda (pieza animada por código) | Escala 1: caparazón de 16 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.BabyTurtle.LegBR` | Instancias | Cría de tortuga marina: aleta trasera derecha (pieza animada por código) | Escala 1: caparazón de 16 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.SeaTurtle.Body` | Instancias | Tortuga marina: cuerpo (pieza animada por código) | Escala 1: caparazón de 100 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.SeaTurtle.Head` | Instancias | Tortuga marina: cabeza con el cuello (pieza animada por código) | Escala 1: caparazón de 100 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.SeaTurtle.LegFL` | Instancias | Tortuga marina: aleta delantera izquierda (pieza animada por código) | Escala 1: caparazón de 100 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.SeaTurtle.LegFR` | Instancias | Tortuga marina: aleta delantera derecha (pieza animada por código) | Escala 1: caparazón de 100 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.SeaTurtle.LegBL` | Instancias | Tortuga marina: aleta trasera izquierda (pieza animada por código) | Escala 1: caparazón de 100 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.SeaTurtle.LegBR` | Instancias | Tortuga marina: aleta trasera derecha (pieza animada por código) | Escala 1: caparazón de 100 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Gull.Body` | Instancias | Gaviota: cuerpo (con la cola en abanico) (pieza animada por código) | Escala 1: cuerpo de 32 de largo, envergadura 90 | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Gull.Head` | Instancias | Gaviota: cabeza con el cuello y el pico (pieza animada por código) | Escala 1: cuerpo de 32 de largo, envergadura 90 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Gull.WingL` | Instancias | Gaviota: ala izquierda (abierta) (pieza animada por código) | Escala 1: cuerpo de 32 de largo, envergadura 90 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Gull.WingR` | Instancias | Gaviota: ala derecha (abierta) (pieza animada por código) | Escala 1: cuerpo de 32 de largo, envergadura 90 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Gull.LegBL` | Instancias | Gaviota: pata izquierda (pieza animada por código) | Escala 1: cuerpo de 32 de largo, envergadura 90 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Gull.LegBR` | Instancias | Gaviota: pata derecha (pieza animada por código) | Escala 1: cuerpo de 32 de largo, envergadura 90 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Sandpiper.Body` | Instancias | Correlimos: cuerpo (con la cola en abanico) (pieza animada por código) | Escala 1: cuerpo de 18 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Sandpiper.Head` | Instancias | Correlimos: cabeza con el cuello y el pico (pieza animada por código) | Escala 1: cuerpo de 18 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Sandpiper.WingL` | Instancias | Correlimos: ala izquierda (abierta) (pieza animada por código) | Escala 1: cuerpo de 18 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Sandpiper.WingR` | Instancias | Correlimos: ala derecha (abierta) (pieza animada por código) | Escala 1: cuerpo de 18 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Sandpiper.LegBL` | Instancias | Correlimos: pata izquierda (pieza animada por código) | Escala 1: cuerpo de 18 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Sandpiper.LegBR` | Instancias | Correlimos: pata derecha (pieza animada por código) | Escala 1: cuerpo de 18 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Toucan.Body` | Instancias | Tucán: cuerpo (con la cola en abanico) (pieza animada por código) | Escala 1: cuerpo de 26 de largo, pico de 17 | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Toucan.Head` | Instancias | Tucán: cabeza con el cuello y el pico (pieza animada por código) | Escala 1: cuerpo de 26 de largo, pico de 17 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Toucan.WingL` | Instancias | Tucán: ala izquierda (abierta) (pieza animada por código) | Escala 1: cuerpo de 26 de largo, pico de 17 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Toucan.WingR` | Instancias | Tucán: ala derecha (abierta) (pieza animada por código) | Escala 1: cuerpo de 26 de largo, pico de 17 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Toucan.LegBL` | Instancias | Tucán: pata izquierda (pieza animada por código) | Escala 1: cuerpo de 26 de largo, pico de 17 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Toucan.LegBR` | Instancias | Tucán: pata derecha (pieza animada por código) | Escala 1: cuerpo de 26 de largo, pico de 17 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Heron.Body` | Instancias | Garza blanca: cuerpo (con la cola en abanico) (pieza animada por código) | Escala 1: cuerpo de 34 de largo y 110 de alto | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Heron.Head` | Instancias | Garza blanca: cabeza con el cuello y el pico (pieza animada por código) | Escala 1: cuerpo de 34 de largo y 110 de alto | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Heron.WingL` | Instancias | Garza blanca: ala izquierda (abierta) (pieza animada por código) | Escala 1: cuerpo de 34 de largo y 110 de alto | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Heron.WingR` | Instancias | Garza blanca: ala derecha (abierta) (pieza animada por código) | Escala 1: cuerpo de 34 de largo y 110 de alto | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Heron.LegBL` | Instancias | Garza blanca: pata izquierda (pieza animada por código) | Escala 1: cuerpo de 34 de largo y 110 de alto | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Heron.LegBR` | Instancias | Garza blanca: pata derecha (pieza animada por código) | Escala 1: cuerpo de 34 de largo y 110 de alto | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Flamingo.Body` | Instancias | Flamenco: cuerpo (con la cola en abanico) (pieza animada por código) | Escala 1: cuerpo de 40 de largo y 140 de alto | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Flamingo.Head` | Instancias | Flamenco: cabeza con el cuello y el pico (pieza animada por código) | Escala 1: cuerpo de 40 de largo y 140 de alto | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Flamingo.WingL` | Instancias | Flamenco: ala izquierda (abierta) (pieza animada por código) | Escala 1: cuerpo de 40 de largo y 140 de alto | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Flamingo.WingR` | Instancias | Flamenco: ala derecha (abierta) (pieza animada por código) | Escala 1: cuerpo de 40 de largo y 140 de alto | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Flamingo.LegBL` | Instancias | Flamenco: pata izquierda (pieza animada por código) | Escala 1: cuerpo de 40 de largo y 140 de alto | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Flamingo.LegBR` | Instancias | Flamenco: pata derecha (pieza animada por código) | Escala 1: cuerpo de 40 de largo y 140 de alto | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Pelican.Body` | Instancias | Pelícano: cuerpo (con la cola en abanico) (pieza animada por código) | Escala 1: cuerpo de 64 de largo, pico de 34 | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Pelican.Head` | Instancias | Pelícano: cabeza con el cuello y el pico (pieza animada por código) | Escala 1: cuerpo de 64 de largo, pico de 34 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Pelican.WingL` | Instancias | Pelícano: ala izquierda (abierta) (pieza animada por código) | Escala 1: cuerpo de 64 de largo, pico de 34 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Pelican.WingR` | Instancias | Pelícano: ala derecha (abierta) (pieza animada por código) | Escala 1: cuerpo de 64 de largo, pico de 34 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Pelican.LegBL` | Instancias | Pelícano: pata izquierda (pieza animada por código) | Escala 1: cuerpo de 64 de largo, pico de 34 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Pelican.LegBR` | Instancias | Pelícano: pata derecha (pieza animada por código) | Escala 1: cuerpo de 64 de largo, pico de 34 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Vulture.Body` | Instancias | Buitre: cuerpo (con la cola en abanico) (pieza animada por código) | Escala 1: cuerpo de 52 de largo, envergadura 160 | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Vulture.Head` | Instancias | Buitre: cabeza con el cuello y el pico (pieza animada por código) | Escala 1: cuerpo de 52 de largo, envergadura 160 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Vulture.WingL` | Instancias | Buitre: ala izquierda (abierta) (pieza animada por código) | Escala 1: cuerpo de 52 de largo, envergadura 160 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Vulture.WingR` | Instancias | Buitre: ala derecha (abierta) (pieza animada por código) | Escala 1: cuerpo de 52 de largo, envergadura 160 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Vulture.LegBL` | Instancias | Buitre: pata izquierda (pieza animada por código) | Escala 1: cuerpo de 52 de largo, envergadura 160 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Vulture.LegBR` | Instancias | Buitre: pata derecha (pieza animada por código) | Escala 1: cuerpo de 52 de largo, envergadura 160 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Eagle.Body` | Instancias | Águila: cuerpo (con la cola en abanico) (pieza animada por código) | Escala 1: cuerpo de 52 de largo, envergadura 170 | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Eagle.Head` | Instancias | Águila: cabeza con el cuello y el pico (pieza animada por código) | Escala 1: cuerpo de 52 de largo, envergadura 170 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Eagle.WingL` | Instancias | Águila: ala izquierda (abierta) (pieza animada por código) | Escala 1: cuerpo de 52 de largo, envergadura 170 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Eagle.WingR` | Instancias | Águila: ala derecha (abierta) (pieza animada por código) | Escala 1: cuerpo de 52 de largo, envergadura 170 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Eagle.LegBL` | Instancias | Águila: pata izquierda (pieza animada por código) | Escala 1: cuerpo de 52 de largo, envergadura 170 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Eagle.LegBR` | Instancias | Águila: pata derecha (pieza animada por código) | Escala 1: cuerpo de 52 de largo, envergadura 170 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Pigeon.Body` | Instancias | Paloma: cuerpo (con la cola en abanico) (pieza animada por código) | Escala 1: cuerpo de 26 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Pigeon.Head` | Instancias | Paloma: cabeza con el cuello y el pico (pieza animada por código) | Escala 1: cuerpo de 26 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Pigeon.WingL` | Instancias | Paloma: ala izquierda (abierta) (pieza animada por código) | Escala 1: cuerpo de 26 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Pigeon.WingR` | Instancias | Paloma: ala derecha (abierta) (pieza animada por código) | Escala 1: cuerpo de 26 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Pigeon.LegBL` | Instancias | Paloma: pata izquierda (pieza animada por código) | Escala 1: cuerpo de 26 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Pigeon.LegBR` | Instancias | Paloma: pata derecha (pieza animada por código) | Escala 1: cuerpo de 26 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Hen.Body` | Instancias | Gallina: cuerpo (con la cola en abanico) (pieza animada por código) | Escala 1: cuerpo de 36 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Hen.Head` | Instancias | Gallina: cabeza con el cuello y el pico (pieza animada por código) | Escala 1: cuerpo de 36 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Hen.WingL` | Instancias | Gallina: ala izquierda (abierta) (pieza animada por código) | Escala 1: cuerpo de 36 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Hen.WingR` | Instancias | Gallina: ala derecha (abierta) (pieza animada por código) | Escala 1: cuerpo de 36 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Hen.LegBL` | Instancias | Gallina: pata izquierda (pieza animada por código) | Escala 1: cuerpo de 36 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Hen.LegBR` | Instancias | Gallina: pata derecha (pieza animada por código) | Escala 1: cuerpo de 36 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Roadrunner.Body` | Instancias | Correcaminos: cuerpo (con la cola en abanico) (pieza animada por código) | Escala 1: cuerpo de 32 de largo, cola de 28 | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Roadrunner.Head` | Instancias | Correcaminos: cabeza con el cuello y el pico (pieza animada por código) | Escala 1: cuerpo de 32 de largo, cola de 28 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Roadrunner.WingL` | Instancias | Correcaminos: ala izquierda (abierta) (pieza animada por código) | Escala 1: cuerpo de 32 de largo, cola de 28 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Roadrunner.WingR` | Instancias | Correcaminos: ala derecha (abierta) (pieza animada por código) | Escala 1: cuerpo de 32 de largo, cola de 28 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Roadrunner.LegBL` | Instancias | Correcaminos: pata izquierda (pieza animada por código) | Escala 1: cuerpo de 32 de largo, cola de 28 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Roadrunner.LegBR` | Instancias | Correcaminos: pata derecha (pieza animada por código) | Escala 1: cuerpo de 32 de largo, cola de 28 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Monkey.Body` | Instancias | Mono: cuerpo (pieza animada por código) | Escala 1: cuerpo de 32 de largo, cola de 48 | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Monkey.Head` | Instancias | Mono: cabeza con el cuello (pieza animada por código) | Escala 1: cuerpo de 32 de largo, cola de 48 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Monkey.LegFL` | Instancias | Mono: pata delantera izquierda (pieza animada por código) | Escala 1: cuerpo de 32 de largo, cola de 48 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Monkey.LegFR` | Instancias | Mono: pata delantera derecha (pieza animada por código) | Escala 1: cuerpo de 32 de largo, cola de 48 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Monkey.LegBL` | Instancias | Mono: pata trasera izquierda (pieza animada por código) | Escala 1: cuerpo de 32 de largo, cola de 48 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Monkey.LegBR` | Instancias | Mono: pata trasera derecha (pieza animada por código) | Escala 1: cuerpo de 32 de largo, cola de 48 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Monkey.Tail` | Instancias | Mono: cola (pieza animada por código) | Escala 1: cuerpo de 32 de largo, cola de 48 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Capybara.Body` | Instancias | Capibara: cuerpo (pieza animada por código) | Escala 1: cuerpo de 80 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Capybara.Head` | Instancias | Capibara: cabeza con el cuello (pieza animada por código) | Escala 1: cuerpo de 80 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Capybara.LegFL` | Instancias | Capibara: pata delantera izquierda (pieza animada por código) | Escala 1: cuerpo de 80 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Capybara.LegFR` | Instancias | Capibara: pata delantera derecha (pieza animada por código) | Escala 1: cuerpo de 80 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Capybara.LegBL` | Instancias | Capibara: pata trasera izquierda (pieza animada por código) | Escala 1: cuerpo de 80 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Capybara.LegBR` | Instancias | Capibara: pata trasera derecha (pieza animada por código) | Escala 1: cuerpo de 80 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Meerkat.Body` | Instancias | Suricato: cuerpo (pieza animada por código) | Escala 1: cuerpo de 24 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Meerkat.Head` | Instancias | Suricato: cabeza con el cuello (pieza animada por código) | Escala 1: cuerpo de 24 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Meerkat.LegFL` | Instancias | Suricato: pata delantera izquierda (pieza animada por código) | Escala 1: cuerpo de 24 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Meerkat.LegFR` | Instancias | Suricato: pata delantera derecha (pieza animada por código) | Escala 1: cuerpo de 24 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Meerkat.LegBL` | Instancias | Suricato: pata trasera izquierda (pieza animada por código) | Escala 1: cuerpo de 24 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Meerkat.LegBR` | Instancias | Suricato: pata trasera derecha (pieza animada por código) | Escala 1: cuerpo de 24 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Meerkat.Tail` | Instancias | Suricato: cola (pieza animada por código) | Escala 1: cuerpo de 24 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Ibex.Body` | Instancias | Cabra montés: cuerpo (pieza animada por código) | Escala 1: cuerpo de 84 de largo, patas de 48 | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Ibex.Head` | Instancias | Cabra montés: cabeza con el cuello (pieza animada por código) | Escala 1: cuerpo de 84 de largo, patas de 48 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Ibex.LegFL` | Instancias | Cabra montés: pata delantera izquierda (pieza animada por código) | Escala 1: cuerpo de 84 de largo, patas de 48 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Ibex.LegFR` | Instancias | Cabra montés: pata delantera derecha (pieza animada por código) | Escala 1: cuerpo de 84 de largo, patas de 48 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Ibex.LegBL` | Instancias | Cabra montés: pata trasera izquierda (pieza animada por código) | Escala 1: cuerpo de 84 de largo, patas de 48 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Ibex.LegBR` | Instancias | Cabra montés: pata trasera derecha (pieza animada por código) | Escala 1: cuerpo de 84 de largo, patas de 48 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Ibex.Tail` | Instancias | Cabra montés: cola (pieza animada por código) | Escala 1: cuerpo de 84 de largo, patas de 48 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Marmot.Body` | Instancias | Marmota: cuerpo (pieza animada por código) | Escala 1: cuerpo de 40 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Marmot.Head` | Instancias | Marmota: cabeza con el cuello (pieza animada por código) | Escala 1: cuerpo de 40 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Marmot.LegFL` | Instancias | Marmota: pata delantera izquierda (pieza animada por código) | Escala 1: cuerpo de 40 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Marmot.LegFR` | Instancias | Marmota: pata delantera derecha (pieza animada por código) | Escala 1: cuerpo de 40 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Marmot.LegBL` | Instancias | Marmota: pata trasera izquierda (pieza animada por código) | Escala 1: cuerpo de 40 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Marmot.LegBR` | Instancias | Marmota: pata trasera derecha (pieza animada por código) | Escala 1: cuerpo de 40 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Marmot.Tail` | Instancias | Marmota: cola (pieza animada por código) | Escala 1: cuerpo de 40 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Cat.Body` | Instancias | Gato: cuerpo (pieza animada por código) | Escala 1: cuerpo de 42 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Cat.Head` | Instancias | Gato: cabeza con el cuello (pieza animada por código) | Escala 1: cuerpo de 42 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Cat.LegFL` | Instancias | Gato: pata delantera izquierda (pieza animada por código) | Escala 1: cuerpo de 42 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Cat.LegFR` | Instancias | Gato: pata delantera derecha (pieza animada por código) | Escala 1: cuerpo de 42 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Cat.LegBL` | Instancias | Gato: pata trasera izquierda (pieza animada por código) | Escala 1: cuerpo de 42 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Cat.LegBR` | Instancias | Gato: pata trasera derecha (pieza animada por código) | Escala 1: cuerpo de 42 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Cat.Tail` | Instancias | Gato: cola (pieza animada por código) | Escala 1: cuerpo de 42 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Rabbit.Body` | Instancias | Conejo: cuerpo (pieza animada por código) | Escala 1: cuerpo de 30 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Rabbit.Head` | Instancias | Conejo: cabeza con el cuello (pieza animada por código) | Escala 1: cuerpo de 30 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Rabbit.LegFL` | Instancias | Conejo: pata delantera izquierda (pieza animada por código) | Escala 1: cuerpo de 30 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Rabbit.LegFR` | Instancias | Conejo: pata delantera derecha (pieza animada por código) | Escala 1: cuerpo de 30 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Rabbit.LegBL` | Instancias | Conejo: pata trasera izquierda (pieza animada por código) | Escala 1: cuerpo de 30 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Rabbit.LegBR` | Instancias | Conejo: pata trasera derecha (pieza animada por código) | Escala 1: cuerpo de 30 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Rabbit.Tail` | Instancias | Conejo: cola (pieza animada por código) | Escala 1: cuerpo de 30 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Lizard.Body` | Instancias | Lagartija turquesa: cuerpo (pieza animada por código) | Escala 1: cuerpo de 22 de largo, cola de 28 | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Lizard.Head` | Instancias | Lagartija turquesa: cabeza con el cuello (pieza animada por código) | Escala 1: cuerpo de 22 de largo, cola de 28 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Lizard.LegFL` | Instancias | Lagartija turquesa: pata delantera izquierda (pieza animada por código) | Escala 1: cuerpo de 22 de largo, cola de 28 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Lizard.LegFR` | Instancias | Lagartija turquesa: pata delantera derecha (pieza animada por código) | Escala 1: cuerpo de 22 de largo, cola de 28 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Lizard.LegBL` | Instancias | Lagartija turquesa: pata trasera izquierda (pieza animada por código) | Escala 1: cuerpo de 22 de largo, cola de 28 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Lizard.LegBR` | Instancias | Lagartija turquesa: pata trasera derecha (pieza animada por código) | Escala 1: cuerpo de 22 de largo, cola de 28 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Lizard.Tail` | Instancias | Lagartija turquesa: cola (pieza animada por código) | Escala 1: cuerpo de 22 de largo, cola de 28 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Salamander.Body` | Instancias | Salamandra de fuego: cuerpo (pieza animada por código) | Escala 1: cuerpo de 26 de largo, cola de 24 | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Salamander.BodyGlow` | Instancias | Salamandra de fuego: manchas o rayas que brillan del lomo (M_ProcGlow) (pieza animada por código) | Escala 1: cuerpo de 26 de largo, cola de 24 | Centro del cuerpo (como el cuerpo); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Salamander.Head` | Instancias | Salamandra de fuego: cabeza con el cuello (pieza animada por código) | Escala 1: cuerpo de 26 de largo, cola de 24 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Salamander.LegFL` | Instancias | Salamandra de fuego: pata delantera izquierda (pieza animada por código) | Escala 1: cuerpo de 26 de largo, cola de 24 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Salamander.LegFR` | Instancias | Salamandra de fuego: pata delantera derecha (pieza animada por código) | Escala 1: cuerpo de 26 de largo, cola de 24 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Salamander.LegBL` | Instancias | Salamandra de fuego: pata trasera izquierda (pieza animada por código) | Escala 1: cuerpo de 26 de largo, cola de 24 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Salamander.LegBR` | Instancias | Salamandra de fuego: pata trasera derecha (pieza animada por código) | Escala 1: cuerpo de 26 de largo, cola de 24 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Salamander.Tail` | Instancias | Salamandra de fuego: cola (pieza animada por código) | Escala 1: cuerpo de 26 de largo, cola de 24 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Salamander.TailGlow` | Instancias | Salamandra de fuego: manchas que brillan de la cola (M_ProcGlow) (pieza animada por código) | Escala 1: cuerpo de 26 de largo, cola de 24 | Base de la cola (como la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.MarineIguana.Body` | Instancias | Iguana marina: cuerpo (pieza animada por código) | Escala 1: cuerpo de 48 de largo, cola de 45 | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.MarineIguana.Head` | Instancias | Iguana marina: cabeza con el cuello (pieza animada por código) | Escala 1: cuerpo de 48 de largo, cola de 45 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.MarineIguana.LegFL` | Instancias | Iguana marina: pata delantera izquierda (pieza animada por código) | Escala 1: cuerpo de 48 de largo, cola de 45 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.MarineIguana.LegFR` | Instancias | Iguana marina: pata delantera derecha (pieza animada por código) | Escala 1: cuerpo de 48 de largo, cola de 45 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.MarineIguana.LegBL` | Instancias | Iguana marina: pata trasera izquierda (pieza animada por código) | Escala 1: cuerpo de 48 de largo, cola de 45 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.MarineIguana.LegBR` | Instancias | Iguana marina: pata trasera derecha (pieza animada por código) | Escala 1: cuerpo de 48 de largo, cola de 45 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.MarineIguana.Tail` | Instancias | Iguana marina: cola (pieza animada por código) | Escala 1: cuerpo de 48 de largo, cola de 45 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.DartFrog.Body` | Instancias | Rana dardo azul: cuerpo (pieza animada por código) | Escala 1: cuerpo de 14 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.DartFrog.LegFL` | Instancias | Rana dardo azul: pata delantera izquierda (pieza animada por código) | Escala 1: cuerpo de 14 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.DartFrog.LegFR` | Instancias | Rana dardo azul: pata delantera derecha (pieza animada por código) | Escala 1: cuerpo de 14 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.DartFrog.LegBL` | Instancias | Rana dardo azul: pata trasera izquierda (pieza animada por código) | Escala 1: cuerpo de 14 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.DartFrog.LegBR` | Instancias | Rana dardo azul: pata trasera derecha (pieza animada por código) | Escala 1: cuerpo de 14 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.TreeFrog.Body` | Instancias | Rana verde de ojos rojos: cuerpo (pieza animada por código) | Escala 1: cuerpo de 16 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.TreeFrog.LegFL` | Instancias | Rana verde de ojos rojos: pata delantera izquierda (pieza animada por código) | Escala 1: cuerpo de 16 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.TreeFrog.LegFR` | Instancias | Rana verde de ojos rojos: pata delantera derecha (pieza animada por código) | Escala 1: cuerpo de 16 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.TreeFrog.LegBL` | Instancias | Rana verde de ojos rojos: pata trasera izquierda (pieza animada por código) | Escala 1: cuerpo de 16 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.TreeFrog.LegBR` | Instancias | Rana verde de ojos rojos: pata trasera derecha (pieza animada por código) | Escala 1: cuerpo de 16 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Fish.Body` | Instancias | Pez saltarín: cuerpo (pieza animada por código) | Escala 1: cuerpo de 44 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Fish.Tail` | Instancias | Pez saltarín: aleta de la cola (pieza animada por código) | Escala 1: cuerpo de 44 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Mudskipper.Body` | Instancias | Pez del fango: cuerpo (pieza animada por código) | Escala 1: cuerpo de 20 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Mudskipper.Tail` | Instancias | Pez del fango: aleta de la cola (pieza animada por código) | Escala 1: cuerpo de 20 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Mudskipper.LegFL` | Instancias | Pez del fango: aleta pectoral izquierda (pieza animada por código) | Escala 1: cuerpo de 20 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Mudskipper.LegFR` | Instancias | Pez del fango: aleta pectoral derecha (pieza animada por código) | Escala 1: cuerpo de 20 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.FireBeetle.Body` | Instancias | Escarabajo de fuego: cuerpo (pieza animada por código) | Escala 1: cuerpo de 18 de largo | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.FireBeetle.BodyGlow` | Instancias | Escarabajo de fuego: manchas o rayas que brillan del lomo (M_ProcGlow) (pieza animada por código) | Escala 1: cuerpo de 18 de largo | Centro del cuerpo (como el cuerpo); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.FireBeetle.LegFL` | Instancias | Escarabajo de fuego: patas del lado izquierdo (pieza animada por código) | Escala 1: cuerpo de 18 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.FireBeetle.LegFR` | Instancias | Escarabajo de fuego: patas del lado derecho (pieza animada por código) | Escala 1: cuerpo de 18 de largo | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Bat.Body` | Instancias | Murciélago: cuerpo (pieza animada por código) | Escala 1: cuerpo de 10, envergadura 52 | Centro del cuerpo; +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Bat.WingL` | Instancias | Murciélago: ala izquierda (abierta) (pieza animada por código) | Escala 1: cuerpo de 10, envergadura 52 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |
| `ProcMap.Fauna.Bat.WingR` | Instancias | Murciélago: ala derecha (abierta) (pieza animada por código) | Escala 1: cuerpo de 10, envergadura 52 | Articulación donde gira (cuello, hombro, cadera o base de la cola); +X hacia delante, Z arriba | `World/ProcMap/TN_ProcMapFaunaMeshes.h` |

### Carrera de la playa (también en DA_Arte_ProcMap) — 196 piezas

#### Beach.Start

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Start.GiantTree` | Pieza | Árbol colosal de la salida (ceiba): tronco, copa de nueve ramas con follaje y siete raíces tabulares, dos de ellas enmarcando la salida | Tronco de 280 m y 26 m de radio en la base; copa de unos 150 m de ancho; raíces de hasta 90 m | Pie del tronco a ras de suelo (X = -5200, detrás de la salida); ejes del generador: +X hacia la meta | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Start.GiantPlant` | Pieza | Planta de hojas enormes junto a la salida (6 copias): tallo y 3-4 hojas que caen como un techo | Escala 1: tallo de 2400 y hojas de 5000 x 1500; cada copia se estira a su tallo (1600-3600) y sus hojas (4200-6000) | Pie del tallo en el suelo; +X hacia la meta | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Start.Banner` | Pieza | Cartel de salida: dos palos de madera a la deriva, travesaño y lona con ¡A LA META! hacia la salida y TORTUNAVY por detrás | 54 m entre palos, travesaño a 15 m, lona de 49 x 6 m | Centro entre los dos palos a ras de suelo (X = 300); +X hacia la meta (el lado de ¡A LA META! mira a -X) | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Start.EggCup` | Pieza | Base de cada huevo de la salida (8, y las del nido del sprint final): la del lobby, medio enterrada | Radio 97, 110 de alto | Centro de la base del huevo (8 bajo la arena); ejes del generador | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Start.NestMound` | Pieza | Anillo de arena removida alrededor de cada huevo de la salida y del sprint final (con colisión: se pisa) | Anillo de radio 130 a 280, 30 de alto | Centro del nido a ras de suelo; ejes del generador | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Start.EggLid` | Componente | Tapa de cada huevo de la salida (8); salta dando vueltas al dar la salida y se esfuma | Radio 96, 128 de alto | Centro de la costura (el borde de abajo); va girada 45° por huevo | `World/Beach/TN_BeachRaceGenerator_Start.cpp` |

#### Beach.Finish

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Finish.Arch` | Pieza | Arco de neumático de la meta (el del mapa procedural a escala 5) con TORTUNAVY hacia la playa, banderines, mástiles en los cabos y banderolas por la ladera | 125 m de luz y 63 m sobre el agua; banderolas a lo largo de los últimos 250 m | Centro del arco al nivel del agua, 40 m más allá del filo; +X hacia el mar | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Finish.FlagBuoy` | Pieza | Boya grande con mástil y bandera a cuadros (7 en fila, a 26 m del filo); se mece con el agua | Boya de radio 260, mástil de 2800, bandera de 12 x 5 m | Centro de la boya a 40 sobre el agua; +X hacia el mar | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Finish.Buoy` | Pieza | Boya pequeña del cabo que une las boyas grandes (3 entre cada dos); se mece con el agua | Radio 90 | Centro de la boya; +X hacia el mar | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |

#### Beach.Jungle

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Jungle.Palm` | Instancias | Palmera (las de la orilla se inclinan hacia la playa): todas sus variantes y biomas (sin colisión ni sombra; viento por vértice cerca de la cámara) | Instancias a escala 22-34 de la del mapa procedural (~9 m de alto a escala 1) | Pie de la planta en el suelo (hundido un poco); +X hacia donde se inclina | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.Casuarina` | Instancias | Casuarina: todas sus variantes y biomas (sin colisión ni sombra; viento por vértice cerca de la cámara) | Instancias a escala 20-26 (~8,5 m a escala 1) | Pie de la planta en el suelo (hundido un poco); +X hacia donde se inclina | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.BroadTree` | Instancias | Árbol de copa ancha de la selva: todas sus variantes y biomas (sin colisión ni sombra; viento por vértice cerca de la cámara) | Instancias a escala 22-30 (~9,5 m a escala 1) | Pie de la planta en el suelo (hundido un poco); +X hacia donde se inclina | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.Ceiba` | Instancias | Ceiba con aletas: todas sus variantes y biomas (sin colisión ni sombra; viento por vértice cerca de la cámara) | Instancias a escala 11-14 (~23 m a escala 1) | Pie de la planta en el suelo (hundido un poco); +X hacia donde se inclina | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.Pandanus` | Instancias | Pandano con raíces zancudas: todas sus variantes y biomas (sin colisión ni sombra; viento por vértice cerca de la cámara) | Instancias a escala 22-30 (~4,5 m a escala 1) | Pie de la planta en el suelo (hundido un poco); +X hacia donde se inclina | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.Rock` | Instancias | Peñasco de la selva: todas sus variantes y biomas (sin colisión ni sombra; viento por vértice cerca de la cámara) | Instancias a escala 14-30 | Pie de la planta en el suelo (hundido un poco); +X hacia donde se inclina | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.BananaPlant` | Instancias | Platanera del sotobosque: todas sus variantes y biomas (sin colisión ni sombra; viento por vértice cerca de la cámara) | Instancias a escala 28-40 | Pie de la planta en el suelo (hundido un poco); +X hacia donde se inclina | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.FanPalm` | Instancias | Palmito de abanico: todas sus variantes y biomas (sin colisión ni sombra; viento por vértice cerca de la cámara) | Instancias a escala 30-45 | Pie de la planta en el suelo (hundido un poco); +X hacia donde se inclina | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.TreeFern` | Instancias | Helecho arborescente: todas sus variantes y biomas (sin colisión ni sombra; viento por vértice cerca de la cámara) | Instancias a escala 20-30 | Pie de la planta en el suelo (hundido un poco); +X hacia donde se inclina | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.SeaGrape` | Instancias | Uva de playa (arbusto): todas sus variantes y biomas (sin colisión ni sombra; viento por vértice cerca de la cámara) | Instancias a escala 22-30 | Pie de la planta en el suelo (hundido un poco); +X hacia donde se inclina | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.Bush` | Instancias | Mata de la selva: todas sus variantes y biomas (sin colisión ni sombra; viento por vértice cerca de la cámara) | Instancias a escala 20-30 | Pie de la planta en el suelo (hundido un poco); +X hacia donde se inclina | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.Fern` | Instancias | Helecho: todas sus variantes y biomas (sin colisión ni sombra; viento por vértice cerca de la cámara) | Instancias a escala 30-42 | Pie de la planta en el suelo (hundido un poco); +X hacia donde se inclina | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.Creeper` | Instancias | Enredadera por el suelo: todas sus variantes y biomas (sin colisión ni sombra; viento por vértice cerca de la cámara) | Instancias a escala 26-34 | Pie de la planta en el suelo (hundido un poco); +X hacia donde se inclina | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.LianaDrape` | Instancias | Liana colgada de un tronco a otro, con hojas y colgajos (2 variantes) | Escala 1: 100 m de largo y 22 m de comba; cada copia se escala a la distancia entre troncos | Punto de donde cuelga en el primer tronco; +X hacia el otro tronco (con su cabeceo) | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.LianaCurtain` | Instancias | Cortina de tres lianas con hojas colgando de una liana o de una copa (2 variantes) | Escala 1: 120 m de caída; las copias van de 0,4 a 1,8 | Punto de arriba de donde cuelga; cae hacia -Z | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |
| `Beach.Jungle.LeafClump` | Instancias | Mata de hojas enormes en los huecos de la selva (2 variantes) | Tallo de 10 m y 5-7 hojas de 12-18 m; escala 0,9-1,5 | Pie del tallo en el suelo | `World/Beach/TN_BeachRaceGenerator_Scenery.cpp` |

#### Beach.Rock

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Rock.Boulder` | Pieza | Peñasco (TNProcAddBoulder): al pie de los cabos, sobre la repisa junto a la selva y en la orilla de las pozas de marea (con colisión) | Escala 1: 500 de radio en planta y 1000 de alto sobre la base; las copias van de 150 a 1000 de radio y de 110 a 2000 de alto | Centro de la base del peñasco (se hunde 25); ejes del generador | `World/Beach/TN_BeachRaceGenerator_Build.cpp` |

#### Beach.Trench

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Trench.Sandbag` | Pieza | Saco terrero en el lomo de las trincheras del lado del mar (con colisión) | 58 x 32 x 30 | Centro de la base del saco; +X a lo largo de la trinchera | `World/Beach/TN_BeachRaceGenerator_Features.cpp` |
| `Beach.Trench.Post` | Pieza | Poste de los tablones de la cara de dentro de las trincheras | 14 x 14; escala 1 = 150 de alto (cada copia se estira a su alto) | Pie del poste; +X a lo largo de la trinchera | `World/Beach/TN_BeachRaceGenerator_Features.cpp` |
| `Beach.Trench.FloorBoard` | Pieza | Tablón de la tarima del fondo de las trincheras | 270 x 22 x 6 | Centro de la cara de abajo; +X de lado a lado de la trinchera | `World/Beach/TN_BeachRaceGenerator_Features.cpp` |
| `Beach.Trench.Bridge` | Pieza | Puente de cuatro tablones de lomo a lomo de una trinchera (2 por trinchera, con colisión) | 660 x 116 x 10 | Centro de la cara de abajo de los tablones; +X de un lomo al otro | `World/Beach/TN_BeachRaceGenerator_Features.cpp` |

#### Beach.Decor

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Decor.Coconut` | Instancias | Coco; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 260 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Jellyfish` | Instancias | Medusa varada (la campana que respira es Beach.Decor.JellyfishBell); sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 700 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.SixPackRings` | Instancias | Anillas de latas cortadas; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 450 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.RedBra` | Instancias | Sujetador rojo; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 550 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Clam` | Instancias | Almeja de adorno (la valva de arriba es Beach.Decor.ClamTop); sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 150 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Shell` | Instancias | Concha de adorno; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 180 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Starfish` | Instancias | Estrella de mar; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 250 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Rock` | Instancias | Roca (8 variantes); sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 700 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.RockCluster` | Instancias | Grupo de rocas (3 variantes); sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 1600 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.ShipSailWreck` | Instancias | Restos de vela de barco con su mástil (el jirón que ondea es Beach.Decor.SailRag); sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 3500 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.MossyLog` | Instancias | Tronco con musgo; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 1400 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.OldPlanks` | Instancias | Tablones viejos; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 900 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.FishingNet` | Instancias | Red de pesca (el faldón que se mece es Beach.Decor.FishingNetFlap); sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 1300 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.PlasticCup` | Instancias | Vaso de plástico; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 250 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Bottle` | Instancias | Botella; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 400 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Lollipop` | Instancias | Chupachups; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 350 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.WatermelonRind` | Instancias | Corteza de sandía roída; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 500 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Straw` | Instancias | Pajita; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 400 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Umbrella` | Instancias | Sombrilla clavada (lona a 35 m de alto); sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 1600 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.BeachChair` | Instancias | Silla de playa; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 1500 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.SandCastleSmall` | Instancias | Castillo de arena pequeño con escalones (la banderita que ondea es Beach.Decor.SandCastleSmallFlag); sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 800 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.SandCastleHuge` | Instancias | Castillo de arena enorme con escalones (la bandera que ondea es Beach.Decor.SandCastleHugeFlag); sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 2600 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Driftwood` | Instancias | Madera a la deriva; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 900 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.SodaCan` | Instancias | Lata; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 220 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.BottleCaps` | Instancias | Chapas de botella; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 150 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.FlipFlop` | Instancias | Chanclas; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 550 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.JuiceBox` | Instancias | Brick de zumo; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 250 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Buoy` | Instancias | Boya varada; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 550 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.BeachTowel` | Instancias | Toalla; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 1800 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Sunscreen` | Instancias | Bote de crema solar; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 300 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.PopsicleSticks` | Instancias | Palitos de helado; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 250 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.SnackShells` | Instancias | Cáscaras de pipas y pistachos; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 300 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.RopePiece` | Instancias | Trozo de cuerda; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 600 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Sunglasses` | Instancias | Gafas de sol; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 300 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.ToyBucket` | Instancias | Cubito de juguete; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 450 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.BeachBall` | Instancias | Pelota hinchable; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 550 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Frisbee` | Instancias | Disco volador; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 400 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Cuttlebone` | Instancias | Hueso de sepia; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 250 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.RubberDuck` | Instancias | Patito de goma; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 200 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.GullFeather` | Instancias | Pluma de gaviota; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 350 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.Sandbags` | Instancias | Parapeto de sacos terreros (algunas variantes con bandera: Beach.Decor.SandbagsFlag); sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 1100 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachMilitaryMeshes.h` |
| `Beach.Decor.AmmoCrate` | Instancias | Caja de munición; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 700 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachMilitaryMeshes.h` |
| `Beach.Decor.TankTrap` | Instancias | Erizo antitanque; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 800 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachMilitaryMeshes.h` |
| `Beach.Decor.MilitaryHelmet` | Instancias | Casco militar; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 450 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachMilitaryMeshes.h` |
| `Beach.Decor.CamoNet` | Instancias | Red de camuflaje con sus palos (la red que se mece es Beach.Decor.CamoNetSheet); sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 1500 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachMilitaryMeshes.h` |
| `Beach.Decor.Jerrycan` | Instancias | Bidón; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 500 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachMilitaryMeshes.h` |
| `Beach.Decor.ToySoldiers` | Instancias | Soldaditos de juguete; sus variantes comparten pieza (con la colisión simple de la receta) | Huella de radio 600 con tamaño 1 | Origen del elemento en la arena (cada ejemplar va girado, inclinado, hundido y escalado 0,5-1,6 por su instancia); +X hacia su frente | `World/Beach/TN_BeachMilitaryMeshes.h` |
| `Beach.Decor.JellyfishBell` | Instancias | Campana de la medusa varada: respira (se ensancha y se aplasta) y a ratos tiembla | Radio 520, 180 de alto | Centro de la campana en la arena; escala desde ahí | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.ClamTop` | Instancias | Valva de arriba de la almeja de adorno: se abre y se cierra de golpe | Unos 300 x 200 | Bisagra de la almeja (gira alrededor de su eje Y) | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.FishingNetFlap` | Instancias | Faldón de red con un flotador colgado del palo de la red de pesca: se mece | Unos 420 x 250 | Punta del palo de donde cuelga (se mece alrededor de ella) | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.SailRag` | Instancias | Jirón de vela colgado de la punta del mástil de los restos del barco: ondea | Unos 400 x 450 | Punto del mástil de donde cuelga (gira alrededor del mástil, +X a lo largo del mástil) | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.SandCastleSmallFlag` | Instancias | Banderita de la torre mayor del castillo de arena pequeño: ondea | 130 x 85 | Punta del palillo (ondea alrededor de +Z) | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.SandCastleHugeFlag` | Instancias | Bandera de la torre del homenaje del castillo de arena enorme: ondea | 200 x 125 | Punta del palillo (ondea alrededor de +Z) | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.SandbagsFlag` | Instancias | Bandera azul marino con estrella del parapeto de sacos: ondea | 170 x 105 | Punta del mástil (ondea alrededor de +Z) | `World/Beach/TN_BeachMilitaryMeshes.h` |
| `Beach.Decor.CamoNetSheet` | Instancias | Red de camuflaje tendida entre sus palos: se mece con el viento | Unos 1500 x 1000 | Punto de anclaje de la animación de la red (el de la receta) | `World/Beach/TN_BeachMilitaryMeshes.h` |
| `Beach.Decor.BoardwalkModule` | Instancias | Tramo recto de la pasarela de madera vieja (cuatro tablas sobre dos largueros y pilotes; entero, sin una tabla, roto, con una suelta o con tablas movidas), con colisión | 1280 x 1300, cubierta a 110 de alto (a lo largo y a lo ancho, al tamaño del ejemplar) | Centro del tramo en la arena; +X a lo largo de la pasarela | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.BoardwalkRamp` | Instancias | Bajada a la arena de cada extremo de la pasarela (la de -X va girada 180°) | 1280 x 1300, de 110 de alto a la arena | Centro del tramo en la arena; baja hacia +X | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.PathPost` | Instancias | Palo de madera del caminito hacia el mar (recto, roto o con vueltas de cuerda), algo torcido | Radio 22, 420 de alto (el roto, 260) | Pie del palo en la arena (enterrado 150) | `World/Beach/TN_BeachPropMeshes.h` |
| `Beach.Decor.PathRope` | Instancias | Tramo de cuerda de palo a palo del caminito (cáñamo, cabo azul o cinta de balizar) | Escala 1: 560 de largo con 55 de comba; cada tramo se estira en X hasta el palo siguiente | Punto donde se ata al primer palo; +X hacia el siguiente | `World/Beach/TN_BeachPropMeshes.h` |

#### Beach.Search

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Search.Mound` | Instancias | Montículo de arena removida junto a lo que se puede rebuscar (liso, con una chapa, con un palito o con una concha asomando); tiembla cerca de las tortugas | Radio 100, 38 de alto con tamaño 1 (cada ejemplar, a su tamaño) | Centro del montículo en la arena | `World/Beach/TN_BeachDecor.cpp` |
| `Beach.Search.MoundFlat` | Instancias | El montículo ya rebuscado: arena aplanada con marcas de escarbar | Radio 115, 5 de alto con tamaño 1 | Centro en la arena | `World/Beach/TN_BeachDecor.cpp` |

#### Beach.Fortress

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Fortress.Tower` | Pieza | Torre de una esquina con sus marcas, pretil con almenas, cubo de juguete boca abajo de torreta y bandera (4 por fortaleza) | Escala 1: radio 290 y 500 hasta el adarve (cubo y bandera por encima); cada copia se estira a su radio y su alto | Centro de la base a ras de suelo (enterrada 200); +X hacia fuera, en diagonal | `World/Beach/TN_BeachFortressKit.h` |
| `Beach.Fortress.Pillar` | Pieza | Torrecilla de cubo de arena de las subidas, con una concha o una estrella pegada | Escala 1: radio 100 y 200 de alto; cada copia a su radio y su alto | Centro de la base; ejes de la fortaleza | `World/Beach/TN_BeachFortressKit.h` |
| `Beach.Fortress.Flag` | Pieza | Bandera de Tortunavy en su mástil, en las torres y en las esquinas de la cima | Escala 1: mástil de 300 (tela de 0,36-0,6 veces el mástil) | Pie del mástil; +X hacia donde ondea | `World/Beach/TN_BeachFortressKit.h` |
| `Beach.Fortress.Banner` | Pieza | Estandarte de Tortunavy colgado de una pared (junto a las puertas y en la cara del mar de las terrazas) | Escala 1: 150 de ancho y 420 de alto (cada copia a su ancho y su alto) | Centro del borde de arriba; +X hacia fuera de la pared | `World/Beach/TN_BeachFortressKit.h` |
| `Beach.Fortress.Pennant` | Pieza | Palo con banderín de color en las esquinas de las terrazas de en medio | Palo de 260, banderín de 130 x 80 | Pie del palo; +X hacia donde ondea | `World/Beach/TN_BeachFortressKit.h` |
| `Beach.Fortress.Spade` | Pieza | Pala de juguete tendida como puente del atajo hasta la cornisa | Hoja de hasta 240 x 170 y mango de 70 de ancho (el largo cambia con la fortaleza) | Donde la hoja se une al mango, a la altura de su cara de arriba; +X hacia el mango | `World/Beach/TN_BeachFortressKit.h` |

#### Beach.SandCastle

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.SandCastle.Shell` | Pieza | Concha de abanico pegada a las caras de fuera de las fortalezas y del castillo con salas (y la grande de su entrada) | Escala 1: 50 de radio (las copias van de 38 a 110) | Centro de la concha; +X hacia fuera de la pared | `World/Beach/TN_BeachFortressKit.h` |
| `Beach.SandCastle.Starfish` | Pieza | Estrella de mar pegada a las caras de fuera de las fortalezas y del castillo con salas | Escala 1: 50 de radio (las copias van de 50 a 70) | Centro de la estrella; +X hacia fuera de la pared | `World/Beach/TN_BeachFortressKit.h` |

#### Beach.Dungeon

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Dungeon.Tower` | Pieza | Torre de una esquina con marcas, cornisa, almenas y banderín (4 por castillo) | Escala 1: radio 240 (200-320 según el tamaño); 1150 de alto más almenas y palo de 420 | Centro de la base a ras de suelo; +X hacia fuera, en diagonal | `World/Beach/TN_BeachSandDungeon.cpp` |
| `Beach.Dungeon.Column` | Pieza | Columna de cubos apilados con capitel de la sala de la entrada (3) | Radio 90, 600 de alto más el capitel | Centro de la base sobre el suelo de dentro (Z = 50) | `World/Beach/TN_BeachSandDungeon.cpp` |

#### Beach.Catapult

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Catapult.Base` | Componente | Fulcro de la catapulta: piedra, tapón de garrafa con estrías, arena removida y palillo de los banderines (la potenciada, con bandera y guirnalda) | Piedra de radio 140 y 110-145 de alto, tapón de radio 78 y 90 de alto | Origen de la catapulta en la arena; +X hacia donde lanza | `World/Beach/TN_BeachCatapult.cpp` |
| `Beach.Catapult.Arm` | Componente | Brazo de la catapulta (cuchara o palo con el cubito en el mango); gira en el fulcro | 820-1150 de largo, 30 de grueso | Fulcro (el eje de giro); +X hacia el mango, el cazo hacia -X | `World/Beach/TN_BeachCatapult.cpp` |
| `Beach.Catapult.Bowl` | Componente | Cazo del brazo, que se parte y cuelga en las de un solo uso | 250-320 x 200-240, 30 de hondo | Línea de rotura del brazo (bisagra del cazo); el cazo hacia -X | `World/Beach/TN_BeachCatapult.cpp` |
| `Beach.Catapult.Splinters` | Componente | Astillas de la rotura del brazo (se ven cuando está partida) | Unos 60 x 60 | Fulcro del brazo (como Beach.Catapult.Arm) | `World/Beach/TN_BeachCatapult.cpp` |
| `Beach.Catapult.Prop` | Componente | Palo de polo de pie que sostiene el mango | 48 x 12, alto hasta el mango | Pie del palo en la arena | `World/Beach/TN_BeachCatapult.cpp` |
| `Beach.Catapult.FlagReady` | Componente | Banderín verde (lista) | 95 x 60 | Origen de la catapulta (el banderín está en la punta del palillo) | `World/Beach/TN_BeachCatapult.cpp` |
| `Beach.Catapult.FlagArmed` | Componente | Banderín rojo (armada o recargando) | 95 x 60 | Origen de la catapulta (el banderín está en la punta del palillo) | `World/Beach/TN_BeachCatapult.cpp` |
| `Beach.Catapult.Sign` | Componente | Cartel de la catapulta con el icono del arco (el rótulo ¡CATAPULTA! es un texto aparte) | Unos 300 de ancho y 400 de alto | Pie del cartel; la tabla mira a -X (a quien llega) | `World/Beach/TN_BeachSignKit.h` |

#### Beach.Sign

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Sign.BrokenCross` | Componente | Aspa roja que tacha el cartel de una catapulta partida | Unos 200 x 200 | Pie del cartel (como el cartel) | `World/Beach/TN_BeachSignKit.h` |

#### Beach.ClamTrap

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.ClamTrap.Mound` | Componente | Montículo de arena donde se asienta la concha que atrapa | Unos 1100 x 850 | Centro de la concha en la arena; +X hacia el mar | `World/Beach/TN_BeachClamTrap.cpp` |
| `Beach.ClamTrap.LowerShell` | Componente | Valva de abajo de la concha que atrapa (por dentro, nácar) | Unos 900 x 650 | Centro de la concha en la arena; +X hacia el mar | `World/Beach/TN_BeachClamTrap.cpp` |
| `Beach.ClamTrap.Mantle` | Componente | Manto (la carne) de la concha que atrapa | Unos 800 x 550 | Centro de la concha en la arena; +X hacia el mar | `World/Beach/TN_BeachClamTrap.cpp` |
| `Beach.ClamTrap.Pearl` | Componente | Perla de la concha que atrapa | Radio 38-60 | Centro de la perla | `World/Beach/TN_BeachClamTrap.cpp` |
| `Beach.ClamTrap.UpperShell` | Componente | Valva de arriba: se abre y se cierra de golpe sobre su bisagra | Unos 900 x 650 | Bisagra de la concha (gira con ella) | `World/Beach/TN_BeachClamTrap.cpp` |

#### Beach.BarbedWire

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.BarbedWire.Wire` | Componente | Alambre de espino en rollos sobre estacas; el largo cambia por ejemplar (la malla de arte no se estira) | Rollos de radio 52-84; largo según el ejemplar (300 o más) | Centro del tramo en la arena; +X a lo largo del alambre | `World/Beach/TN_BeachBarbedWire.cpp` |

#### Beach.BrokenBucket

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.BrokenBucket.Bucket` | Componente | Cubo de playa roto tumbado (se entra por la boca y se sale por el agujero), con rampas | 350-580 de largo, boca de radio 170-275 | Centro del cubo en la arena; +X hacia el fondo roto | `World/Beach/TN_BeachBrokenBucket.cpp` |

#### Beach.Seaweed

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Seaweed.Patch` | Componente | Montículo de las algas que enredan con las bases de las frondas (las frondas se mueven y son aparte) | Elipse de unos 1300 x 1000, 25-65 de alto | Centro de la mancha en la arena | `World/Beach/TN_BeachSeaweed.cpp` |

#### Beach.ShellGate

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.ShellGate.Frame` | Componente | Marco de la puerta de conchas (desnuda, entre rocas o en una pared) con el pedestal del interruptor | Hueco de 160-300 x 330 | Centro del hueco en el suelo; +X hacia donde se pasa | `World/Beach/TN_BeachShellGate.cpp` |
| `Beach.ShellGate.Switch` | Componente | Concha del interruptor que abre la puerta | Radio 60-90 | Centro del interruptor en el suelo | `World/Beach/TN_BeachShellGate.cpp` |
| `Beach.ShellGate.Leaf` | Componente | Hoja de la puerta de conchas (2); gira sobre su bisagra al abrirse | 80-150 de ancho, 320 de alto | Bisagra (borde de abajo); la hoja va hacia +Y | `World/Beach/TN_BeachShellGate.cpp` |

#### Beach.SpadeRamp

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.SpadeRamp.Spade` | Componente | Pala de juguete (trampolín o balancín); se inclina | 700-1150 de largo, hoja de 240-340 de ancho | Origen del marco de la pala (la punta o el fulcro según el modo) | `World/Beach/TN_BeachSpadeRamp.cpp` |
| `Beach.SpadeRamp.Base` | Componente | Apoyos de arena de la pala (rampa o fulcro) | Unos 600 x 400 | Origen del elemento en la arena | `World/Beach/TN_BeachSpadeRamp.cpp` |

#### Beach.Trampoline

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Trampoline.Decor` | Componente | Adornos del trampolín alrededor del cuerpo que rebota (palos, banderines; el cuerpo se deforma y no se sustituye) | Radio de unos 700 | Centro del trampolín en la arena; +X hacia donde lanza | `World/Beach/TN_BeachTrampoline.cpp` |
| `Beach.Trampoline.Sign` | Componente | Cartel del trampolín con el icono del rebote (el rótulo ¡BOING! es un texto aparte) | Unos 300 de ancho y 400 de alto | Pie del cartel; la tabla mira a -X (a quien llega) | `World/Beach/TN_BeachSignKit.h` |

#### Beach.WobblyPlatform

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.WobblyPlatform.Crater` | Componente | Hoyo de arena con su borde levantado sobre el que está la tabla | Radio de unos 900 | Centro del hoyo en la arena | `World/Beach/TN_BeachWobblyPlatform.cpp` |
| `Beach.WobblyPlatform.Board` | Componente | Tabla entera (o tapa) que se tambalea sobre el hoyo | Unos 600 x 220-300, 24-28 de grueso | Bisagra central (en el borde del hoyo); +X a lo largo de la tabla | `World/Beach/TN_BeachWobblyPlatform.cpp` |
| `Beach.WobblyPlatform.BoardHalf` | Componente | Cada mitad de la tabla rota (2); cae girando sobre su borde | Unos 300 x 220-300 | Punta de fuera de la mitad (su bisagra); +X hacia dentro del hoyo | `World/Beach/TN_BeachWobblyPlatform.cpp` |

#### Beach.MovingPlatform

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.MovingPlatform.FerryBase` | Componente | Charca de la balsa: talud de arena alrededor del agua y embarcaderos de palos de polo | Unos 1600 x 900 | Centro de la charca en la arena | `World/Beach/TN_BeachMovingPlatform.cpp` |
| `Beach.MovingPlatform.Raft` | Componente | Balsa que va y viene (colchoneta, tabla, flotador o tapa según la variante) | 540-680 x 200-560 | Centro de la balsa | `World/Beach/TN_BeachMovingPlatform.cpp` |
| `Beach.MovingPlatform.ElevatorBase` | Componente | Torre de arena del ascensor con la grúa de palos de polo y la arena mojada | Torre de 250-480 de alto | Origen del elemento en la arena | `World/Beach/TN_BeachMovingPlatform.cpp` |
| `Beach.MovingPlatform.Lift` | Componente | Plataforma del ascensor que sube y baja (cuadrada o redonda) | 390 x 390, 34 de grueso | Centro de su cara de arriba | `World/Beach/TN_BeachMovingPlatform.cpp` |
| `Beach.MovingPlatform.Rope` | Componente | Cuerda del ascensor (4): varilla que se estira cada fotograma de la grúa a la plataforma | Escala 1: 100 de largo y radio 3,5 | Extremo de la varilla; +X a lo largo | `World/Beach/TN_BeachMovingPlatform.cpp` |

#### Beach.Mine

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Mine.Body` | Componente | Mina de juguete medio enterrada (4 aspectos) | Radio 70 con tamaño 1 (escala 0,5-1,6) | Centro de la mina en la arena | `World/Beach/TN_BeachMine.cpp` |
| `Beach.Mine.Cover` | Componente | Tapa que parpadea al pisar la mina | Radio 70 | Centro de la mina en la arena | `World/Beach/TN_BeachMine.cpp` |
| `Beach.Mine.Led` | Componente | Lucecita de la mina | Unos 8 | Centro de la mina en la arena (la luz está en su sitio) | `World/Beach/TN_BeachMine.cpp` |
| `Beach.Mine.Flag` | Componente | Banderita de aviso clavada junto a la mina (en algo menos de la mitad) | Palillo de unos 120 | Centro de la mina en la arena; gira con la semilla | `World/Beach/TN_BeachMine.cpp` |
| `Beach.Mine.Crater` | Componente | Cráter que deja la mina al explotar | Radio de unos 150 | Centro de la mina en la arena | `World/Beach/TN_BeachMine.cpp` |

#### Beach.Chest

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Chest.Body` | Componente | Caja del cofre del tesoro de la playa (el del lobby a escala 2,2) | Unos 260 x 180 x 150 | Centro de la base; +X hacia su frente | `World/Beach/TN_BeachChest.cpp` |
| `Beach.Chest.Treasure` | Componente | Tesoro de dentro del cofre (monedas y joyas); se ve al abrirse | Unos 230 x 150 | Centro de la base del cofre | `World/Beach/TN_BeachChest.cpp` |
| `Beach.Chest.Lid` | Componente | Tapa del cofre; gira sobre su bisagra al abrirse | Unos 260 x 180 x 90 | Bisagra (borde de atrás, arriba de la caja); la tapa va hacia +X | `World/Beach/TN_BeachChest.cpp` |

#### Beach.GiantCrab

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.GiantCrab.Body` | Componente | Caparazón y cuerpo del cangrejo gigante (los 4 colores comparten pieza) | Unos 250 x 175 x 120 a escala 1 (el actor lo escala con su tamaño) | Centro del cuerpo; +X hacia delante | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.GiantCrab.Eye` | Componente | Ojo con su pedúnculo (2) (los 4 colores comparten pieza) | Unos 15 x 40 | Base del pedúnculo | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.GiantCrab.LegLeft` | Componente | Pata del lado izquierdo (4) (los 4 colores comparten pieza) | Unos 200 de largo | Cadera; la pata va hacia -Y | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.GiantCrab.LegRight` | Componente | Pata del lado derecho (4) (los 4 colores comparten pieza) | Unos 200 de largo | Cadera; la pata va hacia +Y | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.GiantCrab.BigArm` | Componente | Brazo de la pinza grande (los 4 colores comparten pieza) | Unos 150 de largo | Hombro | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.GiantCrab.BigHand` | Componente | Mano de la pinza grande (los 4 colores comparten pieza) | Unos 120 de largo | Codo | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.GiantCrab.BigFinger` | Componente | Dedo móvil de la pinza grande (los 4 colores comparten pieza) | Unos 80 de largo | Nudillo | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.GiantCrab.SmallClaw` | Componente | Pinza pequeña (los 4 colores comparten pieza) | Unos 90 de largo | Hombro | `World/Beach/TN_BeachEnemyMeshes.h` |

#### Beach.HermitCrab

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.HermitCrab.Shell` | Componente | Concha del cangrejo ermitaño (rueda como una bola) (los 4 colores comparten pieza) | Radio de unos 90 (escala del actor) | Centro de la concha | `World/Beach/TN_BeachCritterMeshes.h` |
| `Beach.HermitCrab.Head` | Componente | Cabeza y cuerpo que asoman de la concha (los 4 colores comparten pieza) | Unos 40 | Boca de la concha | `World/Beach/TN_BeachCritterMeshes.h` |
| `Beach.HermitCrab.Antennae` | Componente | Antenas (los 4 colores comparten pieza) | Unos 40 | Base de las antenas | `World/Beach/TN_BeachCritterMeshes.h` |
| `Beach.HermitCrab.Eye` | Componente | Ojo (2) (los 4 colores comparten pieza) | Unos 10 | Base del ojo | `World/Beach/TN_BeachCritterMeshes.h` |
| `Beach.HermitCrab.BigClaw` | Componente | Pinza grande (los 4 colores comparten pieza) | Unos 50 | Hombro | `World/Beach/TN_BeachCritterMeshes.h` |
| `Beach.HermitCrab.SmallClaw` | Componente | Pinza pequeña (los 4 colores comparten pieza) | Unos 30 | Hombro | `World/Beach/TN_BeachCritterMeshes.h` |
| `Beach.HermitCrab.LegLeft` | Componente | Pata izquierda (2) (los 4 colores comparten pieza) | Unos 40 | Cadera; va hacia -Y | `World/Beach/TN_BeachCritterMeshes.h` |
| `Beach.HermitCrab.LegRight` | Componente | Pata derecha (2) (los 4 colores comparten pieza) | Unos 40 | Cadera; va hacia +Y | `World/Beach/TN_BeachCritterMeshes.h` |

#### Beach.SeaUrchin

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.SeaUrchin.Ball` | Componente | Erizo de mar que rueda (bola con púas; los 4 colores comparten pieza) | Radio de rodadura 125 con tamaño 1 | Centro de la bola | `World/Beach/TN_BeachEnemyMeshes.h` |

#### Beach.Lizard

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Lizard.Body` | Componente | Cuerpo del lagarto que se esconde (colores y temperamentos comparten pieza) | Unos 120 de largo a escala 1 (el actor lo escala) | Centro del cuerpo; +X hacia la cabeza | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.Lizard.Head` | Componente | Cabeza (colores y temperamentos comparten pieza) | Unos 30 | Cuello | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.Lizard.Tail` | Componente | Cola (colores y temperamentos comparten pieza) | Unos 120 | Base de la cola | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.Lizard.LegFrontLeft` | Componente | Pata delantera izquierda (colores y temperamentos comparten pieza) | Unos 25 | Hombro | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.Lizard.LegFrontRight` | Componente | Pata delantera derecha (colores y temperamentos comparten pieza) | Unos 25 | Hombro | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.Lizard.LegBackLeft` | Componente | Pata trasera izquierda (colores y temperamentos comparten pieza) | Unos 25 | Cadera | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.Lizard.LegBackRight` | Componente | Pata trasera derecha (colores y temperamentos comparten pieza) | Unos 25 | Cadera | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.Lizard.Tongue` | Componente | Lengua que sale disparada (se estira en X) (colores y temperamentos comparten pieza) | Unos 60 estirada | Base de la lengua en la boca; +X hacia fuera | `World/Beach/TN_BeachEnemyMeshes.h` |

#### Beach.QuadLane

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.QuadLane.Quad` | Componente | Quad que cruza el paso de quads (los 5 colores comparten pieza) | 200 x 125 por TNBeach::Scale (28) | Centro del quad en el suelo; +X hacia delante | `World/Beach/TN_BeachEnemyMeshes.h` |
| `Beach.QuadLane.Wheel` | Componente | Rueda del quad (4) | Radio 30 y 24 de ancho por TNBeach::Scale (28) | Eje de la rueda | `World/Beach/TN_BeachEnemyMeshes.h` |

#### Beach.Gull

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Gull.Body` | Componente | Cuerpo de la gaviota de la zona de gaviotas | La gaviota del mapa procedural a la escala de la playa | Centro del cuerpo; +X hacia la cabeza | `World/Beach/TN_BeachGullZone.cpp` |
| `Beach.Gull.Head` | Componente | Cabeza con el pico de arriba de la gaviota de la zona de gaviotas | La gaviota del mapa procedural a la escala de la playa | Cuello | `World/Beach/TN_BeachGullZone.cpp` |
| `Beach.Gull.Jaw` | Componente | Mandíbula de abajo (se abre para coger a la tortuga y para graznar) de la gaviota de la zona de gaviotas | La gaviota del mapa procedural a la escala de la playa | Base del pico | `World/Beach/TN_BeachGullZone.cpp` |
| `Beach.Gull.WingLeft` | Componente | Ala izquierda (aletea) de la gaviota de la zona de gaviotas | La gaviota del mapa procedural a la escala de la playa | Hombro; el ala va hacia -Y | `World/Beach/TN_BeachGullZone.cpp` |
| `Beach.Gull.WingRight` | Componente | Ala derecha (aletea) de la gaviota de la zona de gaviotas | La gaviota del mapa procedural a la escala de la playa | Hombro; el ala va hacia +Y | `World/Beach/TN_BeachGullZone.cpp` |
| `Beach.Gull.LegLeft` | Componente | Pata izquierda de la gaviota de la zona de gaviotas | La gaviota del mapa procedural a la escala de la playa | Cadera | `World/Beach/TN_BeachGullZone.cpp` |
| `Beach.Gull.LegRight` | Componente | Pata derecha de la gaviota de la zona de gaviotas | La gaviota del mapa procedural a la escala de la playa | Cadera | `World/Beach/TN_BeachGullZone.cpp` |

#### Beach.Pelican

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.Pelican.Body` | Componente | Cuerpo del pelícano de la zona de gaviotas | El pelícano del mapa procedural a la escala de la playa | Centro del cuerpo; +X hacia la cabeza | `World/Beach/TN_BeachGullZone.cpp` |
| `Beach.Pelican.Head` | Componente | Cabeza con el pico de arriba del pelícano de la zona de gaviotas | El pelícano del mapa procedural a la escala de la playa | Cuello | `World/Beach/TN_BeachGullZone.cpp` |
| `Beach.Pelican.Jaw` | Componente | Mandíbula de abajo (se abre para coger a la tortuga y para graznar) del pelícano de la zona de gaviotas | El pelícano del mapa procedural a la escala de la playa | Base del pico | `World/Beach/TN_BeachGullZone.cpp` |
| `Beach.Pelican.WingLeft` | Componente | Ala izquierda (aletea) del pelícano de la zona de gaviotas | El pelícano del mapa procedural a la escala de la playa | Hombro; el ala va hacia -Y | `World/Beach/TN_BeachGullZone.cpp` |
| `Beach.Pelican.WingRight` | Componente | Ala derecha (aletea) del pelícano de la zona de gaviotas | El pelícano del mapa procedural a la escala de la playa | Hombro; el ala va hacia +Y | `World/Beach/TN_BeachGullZone.cpp` |
| `Beach.Pelican.LegLeft` | Componente | Pata izquierda del pelícano de la zona de gaviotas | El pelícano del mapa procedural a la escala de la playa | Cadera | `World/Beach/TN_BeachGullZone.cpp` |
| `Beach.Pelican.LegRight` | Componente | Pata derecha del pelícano de la zona de gaviotas | El pelícano del mapa procedural a la escala de la playa | Cadera | `World/Beach/TN_BeachGullZone.cpp` |

#### Beach.PoolOctopus

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.PoolOctopus.Body` | Componente | Cabeza y manto del pulpo de poza (los 4 colores comparten pieza) | Manto de unos 150 (escala del actor) | Centro del cuerpo | `World/Beach/TN_BeachCritterMeshes.h` |
| `Beach.PoolOctopus.ArmSegment` | Instancias | Tramo de brazo del pulpo (8 brazos de 6 tramos); se mueven cada fotograma | Unos 60 de largo | Arranque del tramo; +X a lo largo del brazo | `World/Beach/TN_BeachCritterMeshes.h` |

#### Beach.SandFleas

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.SandFleas.Flea` | Instancias | Pulga del enjambre de pulgas de arena (2 variantes); saltan cada fotograma | Unos 10 | Centro de la pulga | `World/Beach/TN_BeachCritterMeshes.h` |

#### Beach.ToyTank

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.ToyTank.Hull` | Componente | Casco del tanque de juguete con camuflaje (los 4 colores comparten pieza) | Unos 160 x 100 x 50 (escala del actor) | Centro del casco en el suelo; +X hacia delante | `World/Beach/TN_BeachCritterMeshes.h` |
| `Beach.ToyTank.Wheel` | Instancias | Rueda de las orugas (10) | Radio de unos 15 | Eje de la rueda | `World/Beach/TN_BeachCritterMeshes.h` |
| `Beach.ToyTank.Turret` | Componente | Torreta; gira | Unos 80 | Eje de giro de la torreta | `World/Beach/TN_BeachCritterMeshes.h` |
| `Beach.ToyTank.Barrel` | Componente | Cañón; sube y baja | Unos 90 de largo | Muñón del cañón; +X hacia la boca | `World/Beach/TN_BeachCritterMeshes.h` |
| `Beach.ToyTank.Antenna` | Componente | Antena; se mece | Unos 120 | Base de la antena | `World/Beach/TN_BeachCritterMeshes.h` |
| `Beach.ToyTank.Flag` | Componente | Banderita en la punta de la antena | Unos 30 x 20 | Punta de la antena | `World/Beach/TN_BeachCritterMeshes.h` |
| `Beach.ToyTank.FoamBall` | Instancias | Bolita de espuma que dispara el tanque (en vuelo) | Radio de unos 20 | Centro de la bola | `World/Beach/TN_BeachCritterMeshes.h` |

#### Beach.SandWorm

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Beach.SandWorm.Head` | Componente | Cabeza del gusano de arena con la boca | 280 de largo | Centro de la cabeza; +X hacia la boca | `World/Beach/TN_BeachSandWormMeshes.h` |
| `Beach.SandWorm.Lip` | Componente | Labio de la boca (4); se abren y se cierran | Unos 120 | Bisagra del labio en la cabeza | `World/Beach/TN_BeachSandWormMeshes.h` |
| `Beach.SandWorm.Segment` | Componente | Anillo del cuerpo (13) | 300 de largo | Centro del anillo; +X a lo largo del cuerpo | `World/Beach/TN_BeachSandWormMeshes.h` |
| `Beach.SandWorm.Crater` | Componente | Cráter de arena por donde sale el gusano | Radio de unos 500 | Centro del cráter en la arena | `World/Beach/TN_BeachSandWormMeshes.h` |

### Piezas de la tortuga (DA_Arte_Tortuga) — 4 piezas

#### Turtle

| Pieza | Tipo | Qué es | Tamaño (cm) | Pivote | Fichero |
|---|---|---|---|---|---|
| `Turtle.Shell` | Hueso | Caparazón de la tortuga; sigue al hueso Spine1 (la mitad de la espalda). No lleva los colores ni el dibujo de los caparazones de la tienda | El de la malla del personaje: la de demo mide 53 de alto (133 cm en el juego, que la escala x2,5); su caparazón va de Z = 22 a 38 y de Y = -9 a 3 | El origen de la malla del personaje (entre los pies, +Z arriba, mira a +Y como la de demo): exporta la pieza junto al cuerpo, sin mover el pivote | `Art/TN_TurtleArt.cpp` |
| `Turtle.Helmet` | Hueso | Casco de serie; sigue al hueso Head. Sustituye al casco rojo pintado en la malla de demo y se esconde mientras se lleva un casco de la tienda | El de la malla del personaje: en la de demo la coronilla está a Z = 51 | El origen de la malla del personaje, como Turtle.Shell | `Art/TN_TurtleArt.cpp` |
| `Turtle.Eyes` | Hueso | Ojos; siguen al hueso Head. Fijos: no parpadean ni cambian con los ojos de la tienda (eso solo lo hace la malla de demo, que los lleva pintados) | El de la malla del personaje: en la de demo los ojos van de Z = 42 a 50 | El origen de la malla del personaje, como Turtle.Shell | `Art/TN_TurtleArt.cpp` |
| `Turtle.Tongue` | Hueso | Lengua fija; sigue al hueso Head. Con la malla de demo la cara dibuja además su lengua animada | El de la malla del personaje: en la de demo la boca está en Z = 41 a 44, delante en Y = 10 a 14 | El origen de la malla del personaje, como Turtle.Shell | `Art/TN_TurtleArt.cpp` |

<!-- piezas: fin -->

## 5. Materiales, texturas y assets que ya usa la generación

Ninguna pieza generada lleva texturas: todo es color de vértice (la paleta está en el código) sobre unos pocos materiales.
Si un material no existe, el código usa `/Engine/EngineDebugMaterials/VertexColorMaterial`.

| Material | Lo usan |
|---|---|
| `/Game/Cosmetics/Materials/M_CosmeticVertexColor` | Lobby: castillo, parque (piezas, medusa, puente), probador, tienda y tendero, cofre del tesoro, cuartel del general, pedestal de cosméticos. Playa: decoración, trampas, enemigos y bichos. El alfa del color de vértice es el brillo (0 mate, más alto, más metálico). |
| `/Game/ProcMap/Materials/M_ProcFoliage` | Flora, rocas sueltas, objetos y fauna del mapa procedural; árboles, rocas y fauna del valle del lobby y del tutorial. El alfa es el peso del viento (WPO). |
| `/Game/ProcMap/Materials/M_ProcGlow` | Lo que brilla: cristales y setas de las cuevas, ojos y luces de la fauna, adornos emisivos del valle y del tutorial. |
| `/Game/ProcMap/Materials/M_ProcFlat` y sus MI (`MI_ProcRock`, `MI_ProcWood`, `MI_ProcLava`, `MI_ProcSlideWater`, `MI_ProcSlideWaterAnim`) | Estructuras del mapa procedural (roca, madera, lava y agua de los toboganes) cuando `DA_ProcMapSettings` los nombra. |
| `/Game/ProcMap/Materials/M_ProcTerrain` | Terreno del mapa procedural, del valle y del tutorial (color de vértice con relieve). |
| `/Game/ProcMap/Materials/M_ProcWater`, `M_ProcWaterAnim`, `MI_ProcSea`, `MI_ProcSeaAnim` | Mar y lagunas (mapa procedural, valle, tutorial). |
| `/Game/ProcMap/Materials/M_ProcCascade` | Cascadas y chorros de géiser (mapa procedural, valle, tutorial). |
| `/Game/ProcMap/Materials/M_ProcFXSoft`, `M_ProcFXHard`, `M_ProcFXCloud`, `M_ProcBird`, `M_ProcStormVeil`, `M_PoopSplatDecal` | Efectos: polvo, chispas, nubes, bandadas lejanas, el velo de la tormenta y las cagadas de gaviota. |

Assets binarios que ya existen y usa la generación:

- `/Game/ProcMap/DA_ProcMapSettings` (`UTN_ProcMapSettings`): materiales del terreno, el agua, la lava, la roca, la madera, el
  agua de los toboganes y la flora; los biomas (`/Game/ProcMap/Biomes/DA_Biome_<Bioma>`, 8, con sus colores, sus capas de
  dispersión con malla propia y la malla de la criatura flotante del agua); los 9 perfiles de generación (cooperativo,
  carrera y 2 contra 2 por dificultad) y las clases de los elementos (géiser, nido de huevos, muros, interruptores...).
  Las capas de dispersión de los biomas (`Scatter`) y `WaterBouncerMesh` ya son mallas de asset: se cambian ahí, no en el
  catálogo de arte.
- `/Game/Art/DA_Arte_Lobby`, `/Game/Art/DA_Arte_ProcMap` y `/Game/Art/DA_Arte_Tortuga` (`UTN_ArtCatalog`): los catálogos de
  este documento. `DA_Arte_Tortuga` no existe hasta que se ejecuta `rellenar_catalogos.py`; mientras, la tortuga va sin
  piezas (y sin avisos).
- `/Game/Blueprints/Characters/BP_TortugaCharacter`: la malla de la tortuga (§10).

## 6. Herramientas

| Qué | Cómo |
|---|---|
| Crear los catálogos y meter todas las piezas vacías | En el editor: `exec(open(r"<repo>/Scripts/arte/rellenar_catalogos.py", encoding="utf-8").read())`. Sin interfaz, con el editor cerrado: `UnrealEditor-Cmd.exe "<repo>/Tortunabo.uproject" -run=pythonscript -script="<repo>/Scripts/arte/rellenar_catalogos.py"` (rutas con `/`). |
| Lista de piezas en el juego | `TN.Art.Slots [prefijo] [segundos]`: nombre, tipo, si se ha visto en esta sesión (y cuántas copias), lo que dibuja y su sustituto; con segundos, sale pasado ese tiempo (para lo que se construye al empezar). Avisa si el código usa una pieza que no está en la tabla. Ejemplo: `TN.Art.Slots Lobby.Castle`. |
| Volver a leer los catálogos | Botón **Aplicar cambios** del catálogo (desde Python, `catalog.apply_changes()`) o `TN.Art.Reload`: vuelven a leerlos, avisan de las piezas con malla que no existen y rehacen en el editor el castillo y el valle del nivel abierto. En una partida, lo ya construido cambia al volver a cargar el nivel. No hace falta para el Play: cada partida los vuelve a leer. |
| Cambiar el catálogo con Python | `catalog.get_editor_property("pieces")` y cada `pieces[nombre]` escriben directamente en el asset, sin avisar al editor (volver a asignar el mapa con `set_editor_property` tampoco avisa: el valor ya es el mismo). Guarda con `unreal.EditorAssetLibrary.save_loaded_asset(catalog)` y llama a `catalog.apply_changes()` para verlo en el editor sin jugar. |
| Fotos del lobby sin abrir el editor | `UnrealEditor-Win64-DebugGame.exe <uproject> /Game/Maps/Lobby/LVL_Lobby -game -RenderOffScreen -ResX=1600 -ResY=900 -NoSteam -ExecCmds="TN.Art.Shots C:/ruta"`: fotos desde puntos fijos alrededor del castillo (entrada, plaza, puestos, patio, valle), `TN.Art.Slots Lobby` en el log y cierra el juego. También vale en el PIE. Fuera de Shipping. |
| Comparar con lo generado | `TN.Art.Enabled 0` y recargar el nivel; `TN.Art.Enabled 1` para volver. |
| Lista de piezas en Markdown | `uv run python Scripts/arte/rellenar_catalogos.py --markdown`, o `--doc` para poner al día §4. |
| Fotos de la tortuga sin abrir el editor | `UnrealEditor-Win64-DebugGame.exe <uproject> /Game/Maps/Lobby/LVL_Lobby -game -RenderOffScreen -ResX=1600 -ResY=900 -NoSteam -UseFixedTimeStep -FPS=30 -ExecCmds="TN.Art.TurtleShots C:/ruta"`: el jugador de frente, de espaldas y con la pataleta del podio en dos momentos, el tendero y el general (sin su puesto delante), lo que dibujan el escaparate de cosméticos (y dos miniaturas) y el podio; en el log, `TN.Art.Slots Turtle` y qué malla, escala y piezas lleva cada tortuga. Cierra el juego. También vale en el PIE. Fuera de Shipping. |
| Dónde están los catálogos | Ajustes del proyecto > Tortunavy > Arte (`[/Script/Tortunabo.TN_ArtSettings]` de `Config/DefaultGame.ini`). Ahí también la tortuga: su Blueprint, sus animaciones y las ranuras que pintan los cosméticos (§10). |

## 7. Qué no se puede cambiar desde el catálogo

| Qué | Por qué | Cómo se cambia |
|---|---|---|
| Terreno del mapa procedural, del valle del lobby y del tutorial | Es un mapa de alturas que se genera con cada semilla (forma, caminos, taludes); no hay una malla que poner encima. | Su aspecto, con el material (`M_ProcTerrain`, o `TerrainMaterial` de `DA_ProcMapSettings`) y los colores de cada bioma (`DA_Biome_*`). |
| Mar, lagunas, lava y cascadas generadas como superficie | Siguen la forma del terreno y de cada poza o río generado. | Con sus materiales (§5). |
| Mallas que se deforman cada fotograma | Se reconstruyen en cada fotograma (tablas que ondulan, tentáculos que se doblan): una malla rígida no puede seguirlas. | Lista en las notas de cada zona, más abajo. |
| Efectos (polvo, chispas, burbujas, nubes, velo de la tormenta, salpicaduras) | Son partículas o mallas de un solo uso, no objetos. | Sus materiales (§5). |
| Barreras y colisiones invisibles | No se ven: son la colisión del juego. | Programación. |
| Textos (carteles con nombre), luces y sonidos | No son mallas: `TextRender`, luces y síntesis de sonido en código. | Programación. |
| Capas de dispersión de los biomas y criatura flotante del agua | Ya son assets: se cambian en `DA_Biome_*`. | `DA_Biome_*` (§5). |
| El cuerpo de la tortuga | Es una malla esquelética (huesos y animación), no una pieza generada. | El componente Mesh de `BP_TortugaCharacter` (§10); las piezas rígidas que van encima, sí, en `DA_Arte_Tortuga`. |
| Cosméticos, objetos de juego comunes (conchas de puntos, huevos de eclosión, objetos de la carrera) | Tienen sus propios assets o su propio sistema (cosméticos). | Sus Blueprints y assets. |

Notas por zona (lo que no sale en la lista y lo que conviene saber al sustituir):

- **Castillo**: las barreras que impiden caerse, los rótulos de las puertas y las luces de las antorchas no son mallas.
  `Lobby.Castle.Sea` no se ve con el valle puesto (el valle ocupa su sitio).
- **Valle**: el terreno, la laguna y el manglar, la lava de los cráteres y la cascada son superficies. Las bandadas lejanas y
  las partículas, efectos. Con sustituto, cada especie de fauna es una sola malla por animal (incluye lo que brilla: ojos,
  luces); el aleteo de los pájaros lo hace `M_ProcBird`, no la malla. Las formaciones, agujas y casas se estiran a su tamaño
  respecto a la primera de su tipo.
- **Tutorial**: la laguna, el arroyo y la cascada son superficies. Con sustituto en la fauna, el animal entero va en el
  cuerpo y se mueve rígido con él. Las islas, el tronco y los peñascos conservan su colisión: la malla de arte tiene que
  seguir ese suelo.
- **Parque**: los tentáculos de la medusa y el tablero con las cuerdas del puente se deforman cada fotograma y se quedan
  generados; los colores de la medusa no llegan a la malla de arte. Cada pieza se estira con sus propiedades (alto, largo,
  radio) respecto a las medidas por defecto que da la tabla.
- **Mapa procedural**: los símbolos pintados de las cuevas, las raíces que siguen la bóveda, el musgo, las vetas de lava y
  las grietas que brillan siguen la forma de cada cueva y se quedan generados; el velo de la tormenta cambia cada fotograma.
  El aro del rebuscable es del kit común de botín. Los tintes de estado (nido activado, interruptor rojo o verde, medusa)
  no llegan a la malla de arte. En los actores con mallas del motor (nido, puzles, agua) la malla de arte hereda la escala
  del componente. No pongas `bUseArtCollision` en la rampa de los puzles: el código enciende y apaga su colisión. Los dos
  puentes rotos del río son dos piezas (`Left`, `Right`) porque la escalera cambia de lado.
- **Carrera de la playa**: la arena, el mar, el acantilado, los caballones de las trincheras, las cornisas, el agua de las
  pozas y las murallas, terrazas, rampas y escaleras de los castillos siguen el terreno o la planta de cada ejemplar. El
  cuerpo del trampolín y las frondas de las algas se deforman. Los trastos y bañistas de la tormenta se desvanecen con una
  copia translúcida y se quedan generados. Al sustituir: el alambre de espino no se estira con su largo, las fortalezas en
  espejo no reflejan la malla de arte (solo la colocan) y el decorado no pasa su distancia de corte a la malla de arte.
- **Las mismas especies en varias zonas**: la flora, las rocas y la fauna del valle, del tutorial, del mapa procedural y de
  la selva de la playa salen de los mismos generadores, pero cada zona tiene sus piezas (`Lobby.Valley.Flora.Palm`,
  `Lobby.Tutorial.Flora.Palm`, `ProcMap.Flora.Palm`, `Beach.Jungle.Palm`): pon la misma malla en cada una si quieres que se
  vean iguales.

## 8. Red y cocinado

Es solo visual. Cada máquina construye el nivel igual (misma semilla) y busca el sustituto de cada pieza en los mismos
catálogos, así que no se replica nada. `/Game/Art` se cocina entero (`DirectoriesToAlwaysCook`), con las mallas y los
materiales que nombran los catálogos: en la build empaquetada se ve lo mismo que en el editor. Si un catálogo no existe o
una malla no carga, esa pieza se dibuja generada (y el log lo dice una vez con un Warning `[Arte]`).

## 9. Para programación: añadir una pieza

- Nombre con `TN_ART("Zona.Grupo.Pieza")` (zona `Lobby`, `ProcMap` o `Beach`, partes en PascalCase), siempre con el
  literal dentro de la macro. Las de la tortuga son `Turtle.Pieza`, tipo `Hueso`, y se añaden a `TNTurtleArt::GetPieces`
  (`Private/Art/TN_TurtleArt.cpp`) con su hueso por defecto.
- Una tortuga nueva que no sea el personaje (otra copia como el tendero) no carga la malla por su ruta:
  `TNTurtleArt::ApplyBody` (malla, materiales y escala del personaje), `TNTurtleArt::GetClip` (animaciones de los ajustes) y
  `UTN_CosmeticLook::ApplyLook` (aspecto y piezas). Si la dibuja un captor con lista, añade `TNTurtleArt::GetPieceComponents`.
  Para reconocer tortugas de maqueta, `TNTurtleArt::IsTurtleMesh`.
- Según cómo se genera (`Public/Art/TN_Art.h`, `Private/Art/TN_ArtPieces.h`):
  - pieza dentro de buffers combinados: `TNArt::FPieceScope` alrededor de lo que la dibuja, `TNArt::UploadSection` en vez
    de `CreateMeshSection_LinearColor` y `TNArt::SpawnPieceArt` al final;
  - componente suelto: `TNArt::SetMesh(Comp, Malla, TN_ART(...))` en vez de `SetStaticMesh`, o `TNArt::ApplyToComponent`;
  - ISM/HISM: `TNArt::ApplyToInstances` tras añadir las instancias y `TNArt::UpdateInstances` si se mueven.
- Una línea en la tabla `Private/Art/TN_ArtSlots_<Parte>.inl` con tipo, fichero, qué es, tamaño y pivote.
- `Tortunabo.Art.SlotsInCode` falla si un `TN_ART` no está en la tabla o si una entrada de la tabla no la usa el código;
  `Tortunabo.Art.SlotTable`, si un nombre se repite, no es válido o su fichero no existe.
- Ejecuta `rellenar_catalogos.py` (añade la pieza vacía a su catálogo) y `--doc` (la añade aquí).

## 10. La tortuga

Issue #581. La tortuga no se genera desde C++: su cuerpo es una malla esquelética (con huesos y animación) y lo que Arte
quiera añadirle por encima (caparazón, casco, ojos, lengua) son mallas estáticas pegadas a sus huesos.

### Por qué `Tortuga_V1` no sirve tal cual

`Tortuga_V1` y las mallas de `PartesTortuga_V1` (rama `ArteDev`) se importaron como **Static Mesh**: no tienen huesos ni
pesos, así que no se pueden animar y el componente Mesh de `BP_TortugaCharacter` (un Skeletal Mesh Component) no las
acepta. Por eso, al cambiarla, «se quedaba la misma». En su importación guardan el esqueleto `TotugaDemo_Rig_Skeleton`: se
intentó importar con él, pero el FBX no traía pesos (o se importó forzando «Static Mesh») y salieron estáticas. Además
miden unos 280 cm de alto con el origen a la altura del pecho, y la de demo mide 53 unidades con el origen entre los pies
(el Blueprint la escala x2,5 hasta 133 cm). Hay que volver a exportarla desde el programa 3D; desde Unreal no se arregla.

### Cambiar el cuerpo

1. **En el programa 3D**, une el cuerpo al rig de `TotugaDemo_Rig` (esqueleto Mixamo: mismos huesos y mismos nombres, lista
   abajo) con pesos (skinning), a la misma escala que el rig: unas 53 unidades de alto, el origen entre los pies, +Z arriba y
   mirando a +Y, como la de demo. Exporta en FBX la malla con su esqueleto.
2. **En Unreal**, importa el FBX como **Skeletal Mesh** (no Static Mesh) y en *Skeleton* elige `TotugaDemo_Rig_Skeleton`
   (sin importar animaciones). Si Unreal avisa de que los huesos no coinciden, el rig no es el mismo. Con ese esqueleto valen
   tal cual las animaciones de la tortuga y su Physics Asset (`TotugaDemo_Rig_PhysicsAsset`, el del ragdoll: pónselo a la
   malla nueva o hazle uno).
3. Abre **`BP_TortugaCharacter` > componente Mesh > Skeletal Mesh Asset** y pon tu malla. Compila y guarda. Es la única
   fuente de la tortuga: el jugador y todas sus copias (el tendero, el general, el escaparate de cosméticos de la tienda y
   del probador, el podio de la carrera y las tortugas de práctica del tutorial) la usan. Ninguna carga ya la malla por su
   ruta, así que también se puede mover la carpeta.
4. Si tu malla tiene otro pivote, otra orientación u otra escala, corrígelo en ese mismo componente (la de demo va en
   `(0, 0, -70)`, giro `-90` y escala `2,5`): las copias aplican la misma diferencia. Los materiales que le pongas al
   componente (*Override Materials*) también los copian.
5. **Comprueba** con `TN.Art.TurtleShots` (§6): fotos del jugador, el tendero, el general, el escaparate y el podio, y en el
   log qué malla y qué escala lleva cada uno.

Qué cambia sin la malla de demo:

- **Cosméticos.** Los colores, el dibujo del caparazón, los ojos de la tienda, el parpadeo y la boca se pintan con
  `M_TurtleBody` y `M_TurtleHelmetSlot`, medidos sobre la malla de demo, en las ranuras de material `lambert4` (cuerpo) y
  `lambert2` (casco de serie y lengua). Si tu malla no tiene ranuras con esos nombres, se queda con sus propios materiales:
  no se rompe nada, pero no lleva los colores de la tienda ni la cara animada. Los nombres se cambian en Ajustes del
  proyecto > Tortunavy > Arte > Tortuga > Cosméticos. Los cascos de la tienda van al socket `Sombrero` de la malla (o del
  esqueleto) si existe; si no, a la coronilla de la de demo sobre el hueso `Head`: con otra cabeza, añade ese socket.
- **Animaciones.** Las de la tortuga (espera, andar, celebrar, saludar) se configuran en Ajustes del proyecto > Tortunavy >
  Arte > Tortuga > Animaciones (`[/Script/Tortunabo.TN_ArtSettings]` de `Config/DefaultGame.ini`). Encima, el código mueve los
  huesos de la lista de abajo (correr, panzazo, caparazón, lanzar, poses del podio...).
- **Otro esqueleto.** Si no se puede usar el rig de `TotugaDemo_Rig`, el esqueleto nuevo tiene que tener los mismos nombres
  de hueso (si no, esas partes se quedan quietas) y hay que cambiar las cuatro animaciones de los ajustes por unas suyas:
  retargetiza las de `/Game/Animations/Character/TortugaDemo/Anim` (IK Retargeter) o haz otras. Avisa a programación: las
  poses en código están pensadas para las proporciones del rig de demo.

### Huesos que usa el código

| Hueso | Para qué |
|---|---|
| `Hips`, `Spine`, `Spine1`, `Spine2`, `Neck`, `Head` | Animación en C++ (`UTN_TurtleAnimInstance`): andar, correr, saltar, panzazo, nado, caparazón, llevar y lanzar, poses y emotes. |
| `LeftShoulder`, `RightShoulder`, `LeftArm`, `RightArm`, `LeftForeArm`, `RightForeArm` | Animación en C++ y brazos que sujetan lo que lleva. |
| `LeftUpLeg`, `RightUpLeg`, `LeftLeg`, `RightLeg`, `LeftFoot`, `RightFoot` | Animación en C++ (zancada, sentarse, patalear). |
| `LeftHand`, `RightHand` | Lo que lleva en las aletas (`UTN_InventoryComponent`), el trofeo del podio y las manos en VR. |
| `Spine2` | El pecho (lo que lleva abrazado) y la espalda: la gaviota y los enemigos de la playa la agarran por ahí y ahí va la mancha. |
| `Head`, `HeadTop_End` | La cara (lengua y sudor, `UTN_TurtleFaceComponent`), el casco de la tienda, los pájaros del mareo y la etiqueta del podio. |
| `LeftFoot`/`RightFoot` o `LeftToeBase`/`RightToeBase` | Pasos (`UTN_TurtleFoleyComponent`). |
| `Spine1`, `Head` | Huesos por defecto de las piezas `Turtle.*` (abajo). |
| Socket `Sombrero` (opcional) | Dónde va el casco de la tienda. |

El hueso que sigue el ragdoll de muerte es `RagdollTrackingBone` en `BP_TortugaCharacter` (Knockdown).

### Piezas sueltas: caparazón, casco, ojos, lengua

Si el cuerpo va por un lado y las piezas rígidas por otro (como en `PartesTortuga_V1`), las piezas no hace falta pesarlas:
se ponen como mallas estáticas en el catálogo `DA_Arte_Tortuga` y van pegadas a un hueso, siguiendo su animación.

1. Importa cada pieza como **Static Mesh** exportada **en el mismo espacio que el cuerpo**: misma escala, mismo origen (entre
   los pies) y misma orientación, sin mover el pivote. Así queda en su sitio sin tocar nada.
2. Si `DA_Arte_Tortuga` no existe, ejecuta `Scripts/arte/rellenar_catalogos.py` (§6): lo crea con sus piezas vacías.
3. En `Pieces`, pon la malla en `Turtle.Shell` (caparazón), `Turtle.Helmet` (casco de serie), `Turtle.Eyes` (ojos) o
   `Turtle.Tongue` (lengua) (lista en §4). Opcional: `Materials`, y `Adjust` si la pieza no está en el espacio del cuerpo
   (desplazamiento, giro y escala en unidades de la malla; para `PartesTortuga_V1` sobre la de demo, escala 0,205 y
   subirla 23). `Bone` cambia el hueso o socket que sigue (por defecto, `Spine1` el caparazón y `Head` lo demás): la pieza
   se coloca igual, solo cambia a qué parte del cuerpo acompaña.
4. Dale al Play: van en el jugador y en todas sus copias, siguen la animación y el ragdoll, se esconden con la tortuga y no
   chocan con nada. `TN.Art.TurtleShots` las enseña (también la pataleta en dos momentos, para ver que siguen los huesos).

Reglas de las piezas:

- El casco de la tienda manda: con uno puesto, `Turtle.Helmet` se esconde. Sin él, `Turtle.Helmet` es el casco de serie y
  quita el rojo pintado de la malla de demo.
- Los colores y dibujos de la tienda no llegan a las piezas (el caparazón de Arte lleva sus materiales) y los ojos de Arte
  no parpadean. Con la malla de demo, la cara sigue dibujando su lengua animada aunque haya `Turtle.Tongue`.
- Son visuales y locales: cada máquina las pone igual a partir del catálogo (se cocina) y del aspecto ya replicado. No hay
  nada que replicar ni cambia cómo se juega.
