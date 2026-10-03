# Terreno «camino primero» — diseño

Fecha: 2026-09-25. Estado: pendiente de aprobación de Rodrigo.
Sustituye como base de trabajo al prototipo P01. Mapa01 y las 30 variantes anteriores se conservan tal cual, sin push hasta validar la primera versión de este diseño.

## 1. Objetivo

Generar mapas de Tortunabo donde el terreno se construye a partir del camino y no al revés. Cada mapa tiene:

- un único camino de inicio a fin;
- lazos que se separan y vuelven;
- bordes cerrados de forma natural;
- vistas vivas fuera del camino.

Es el terreno base: los diseñadores ajustan y añaden encima.

Primera entrega: un mapa. Rodrigo lo valida y después se generan 30 variantes, unas con parámetros distintos y otras solo con otra semilla.

## 2. Requisitos (de Rodrigo)

1. **Un solo camino principal** de inicio a fin.
2. **Lazos** que salen del camino principal y vuelven a él, en torno a 7 por mapa. Son grandes y están bien delimitados.
   - Pueden entrelazarse: uno empieza antes de que vuelva el anterior.
   - Puede haber lazos dentro de lazos.
   - Un lazo puede cruzar el camino principal por arriba (puente natural) o por abajo (túnel).
3. **Bordes bloqueados de forma natural.** El terreno sale del camino y nunca se pinta un camino sobre un terreno ya hecho.
   - Los ríos existen solo como tramo del camino.
4. **Túneles como parte del camino.**
5. **Sección del camino:**
   - suelo llano;
   - paredes en «cuadrado curvado»: esquina inferior redondeada y pared casi vertical, pero no del todo vertical;
   - **prohibido el cuenco**.
   - Se admiten variaciones de cota y tramos algo empinados, siempre con rampas lineales suaves.
6. **Anchura variable:** media, con estrechamientos y ensanches.
7. **Río natural y arcade:**
   - orillas irregulares;
   - islas alargadas en el sentido del flujo, de tamaños distintos y en posiciones distintas, nunca una ristra en el centro.
8. **Inicio claro. El final se ensancha** hasta cubrir la playa y llega al mar: no acaba lejos del agua ni en una forma artificial.
9. **Acantilados y bordes:**
   - poca altura y cima algo irregular;
   - no son paredes verticales;
   - la malla es suave, sin picos de polígonos.
10. **Agua cartoon** que interactúa con la arena, con oleaje estilizado y sin buscar realismo.
11. **Mapa de 400 × 400 m:** 4 × 4 trozos de 100 m.
12. **Vida fuera del camino:** dunas, lagos y dunas inundadas que se ven desde el camino y desde lo alto, pero no se pisan. Los lagos y las dunas inundadas son de lo que mejor sale ahora mismo.
13. **Castillos de arena:** se prueba a generarlos en el propio terreno, junto al camino o dentro de él, como en el boceto de referencia. Si no quedan bien a 1 m de resolución, pasan a ser assets de los diseñadores.
14. **Sin patrones artificiales:** ni rectas forzadas, ni giros de 90°, ni caos de laberinto.
15. **Secuencia de biomas** a lo largo del camino: acantilado → agua → dunas → playa. El bioma decide el tipo de borde del camino.

## 3. Arquitectura

Paquete nuevo `Scripts/terrain_path/`. El paquete `terrain_vol/` no se toca, para que Mapa01 y las 30 variantes sigan reproducibles.

| Módulo | Qué hace |
|---|---|
| `layout.py` | Rejilla parametrizable: `grid` × 100 m, 4 × 4 en este diseño. |
| `graph.py` | Camino principal, lazos (anidados incluidos) y cruces. |
| `profile.py` | Cota a lo largo de cada camino: llanos, rampas y separación vertical en los cruces. |
| `field.py` | Relieve 2D a partir del grafo (sección del camino, borde por bioma, fondo de vistas) y campos de túnel y puente para el 3D. |
| `river.py` | Tramo de río: cauce, orillas e islas alargadas. |
| `castles.py` | Castillos de arena como estampas del relieve. |
| `generate.py` | Generador de un mapa y de catálogos (CLI). |

**Reutilización:**
- `terrain_vol/mesh.py`: marching cubes, Taubin, normales y color.
- `terrain_vol/export.py`: formato TNTM2 y manifest.
- `terrain_vol/noise.py`.
- El excavado 3D de túneles de `density.py`, extraído a una función común sin cambiar su resultado.
- Donde hoy dependen de las constantes 6 × 6, `mesh` y `export` reciben el layout como parámetro. El stash «wip tamanos variables» sirve de referencia.

**Unreal:**
- `ATN_MapVariantLoader` ya coloca los trozos según el manifest; se comprueba con 4 × 4.
- Los mapas nuevos van en `Scripts/terrain_volumes/PathMaps/<nombre>/`, con su propio `index.json`.
- El cargador suma esa lista a la de variantes.

## 4. Diseño por partes

### 4.1 Grafo del camino (`graph.py`)

**Camino principal:**
- Avance con rumbo que cambia de forma suave: la curvatura está acotada y sigue un ruido de baja frecuencia.
- Giro máximo acumulado de unos 50° en 10 m.
- Sin rectas de más de unos 25 m.
- Se mantiene a 25 m o más del borde del mapa.
- La separación mínima entre dos tramos lejanos es el ancho de dos caminos más el borde. Así nunca se tocan si no es a propósito.

**Longitud objetivo:** 700-900 m en 400 m de lado. El camino serpentea con naturalidad, sin llegar al caos.

**Lazos (unos 7):**
- Cada lazo tiene un punto de salida y otro de vuelta sobre el camino padre, a 40-160 m de arco.
- El trazado es una curva orgánica con sus propios meandros. Se admite que el lazo retroceda un tramo, como en el boceto bueno.
- El camino padre puede ser el principal o otro lazo (lazos en lazos).
- Los intervalos de lazos distintos pueden solaparse.

**Cruces:**
- Si un lazo cruza el principal o a otro lazo, el cruce se marca como paso a distinto nivel.
- Por defecto el lazo va por arriba (puente natural) o por abajo (túnel), según qué cota sea más fácil de alcanzar con rampas suaves.
- Solo se admite un cruce si el ángulo es de 40° o más y hay sitio para las rampas. Si no, se reintenta el lazo.

**Validación:**
- Todos los lazos vuelven a su padre.
- No hay cruces no deseados.
- Hay un único camino que llega al final: los lazos solo vuelven a su padre y no conectan con la salida por otro sitio.

### 4.2 Cota del camino (`profile.py`)

- Tramos llanos de 15-60 m unidos por rampas lineales de pendiente ≤ 20 %.
- Algún tramo empinado de hasta el 30 %, corto.
- Cruce a distinto nivel: el camino de arriba queda a 7,5 m o más sobre el de abajo, que es un túnel de 5 m más techo.
- Rampas de subida y bajada simétricas en torno al cruce.
- Al unirse a su padre, la cota del lazo coincide con la del padre en los puntos de salida y de vuelta.
- Tramo de agua: suelo a la cota de las orillas e islas, justo por encima del agua (sección 4.4).

### 4.3 Relieve a partir del camino (`field.py`)

**Distancia al camino.** Se usa la distancia a la red de caminos con su semiancho variable:
- media de 4 m;
- estrechamientos de 2,5 m;
- ensanches de hasta 8 m, a tramos de 20-60 m, siguiendo un ruido suave.

**Sección «cuadrado curvado»:**
- El suelo es llano a la cota del camino, con una rugosidad mínima de ±0,2 m.
- Cerca del borde, una esquina inferior redondeada de 1,5 m de radio.
- Encima, una pared de 70-80° de inclinación. En la roca puede llevar un escalón intermedio.
- Arriba, un remate con la cima algo irregular. Nunca un cuenco: el suelo no se curva hacia las paredes.

**Borde según el bioma del tramo:**

| Bioma | Borde |
|---|---|
| Acantilado | Roca arenosa de 4-9 m sobre el suelo, cima irregular y algún voladizo suave (densidad 3D). |
| Agua | Orillas bajas con cañaverales de arena, que cierran con dunas inundadas y agua honda (sección 4.4). |
| Dunas | Duna de 3-6 m con cara algo empinada, sin llegar a vertical. |
| Playa | Dunas bajas que se abren hasta el mar (sección 4.6). |

- **Banda de bloqueo:** desde el borde del camino y hasta unos 15 m, la masa del borde impide salir: pared más alta que el salto.
- **Fondo de vistas:** más allá de la banda de bloqueo.
  - Dunas tupidas, lagos y dunas inundadas, que son el estilo que ya funciona en `terrain_vol`.
  - Siempre por debajo de la cresta de bloqueo o separado por agua honda, así que desde el camino no se puede entrar.
- **Mezcla entre lo cercano y lo lejano:** con un máximo suave (`soft_max`), sin aristas.
- **Túneles y puentes:** el camino de abajo se excava en 3D bajo el de arriba (arco natural), con el mismo excavado que se usa hoy. Algunos tramos del camino principal atraviesan un cerro como túnel aunque no haya cruce: se eligen donde el borde es alto.

### 4.4 Río como tramo del camino (`river.py`)

- **Cauce** que sigue el camino en el tramo de agua, con un ancho de 8-16 m que varía.
- **Orillas** irregulares, siguiendo ruido a lo largo del flujo.
- **Islas alargadas** en el sentido del flujo:
  - 3-12 m de largo y 1,5-4 m de ancho;
  - repartidas a ambos lados y a distancias irregulares, sin ristra central;
  - sin punta en los extremos.
- **Paso** andando y saltando: islas, bajíos y orillas. La separación máxima entre dos apoyos seguidos es la del salto, y el test lo comprueba.
- El agua de fuera del paso es honda, y la de los bajíos cubre poco.

### 4.5 Castillos de arena (`castles.py`)

- **Estampas en el relieve:** patio, muralla con puerta y 3-5 torres cilíndricas de 2-4 m, sin almenas porque a 1 m de resolución no se ven. Opcionalmente, un foso.
- **Dónde:** en ensanches del camino o junto a él, con la puerta mirando al camino. Entre 2 y 4 por mapa.
- **Se validan en la primera versión.** Si a esta resolución no quedan bien, se quitan y pasan a ser assets de los diseñadores (requisito 13).

### 4.6 Inicio y final

- **Inicio:** el camino arranca en un ensanche llano y reconocible, con un color de arena más claro. No tiene ramas cerca.
- **Final:** en los últimos 40-60 m el camino se ensancha poco a poco hasta cubrir toda la playa y se funde con la orilla. El mar está a la vista y se llega andando al agua.

### 4.7 Agua cartoon (Unreal)

Material nuevo `M_TortunaboWaterToon`. El actual queda de respaldo.

- **Color por profundidad** en 3 bandas duras: turquesa claro, turquesa y azul hondo, usando la distancia al fondo.
- **Espuma de orilla:**
  - banda blanca de borde neto (umbral, no degradado);
  - avanza y se retira sobre la arena con un periodo de unos 4 s, como una ola que moja;
  - deja una franja de arena mojada que se seca, pintada por el material del agua sobre el terreno (decal o máscara por distancia).
- **Ondas:**
  - pocas ondas de vértice, suaves;
  - líneas de brillo estilizadas que se desplazan sin reflejos realistas;
  - especular recortado.

Implementación: la espuma depende de la distancia a la superficie más cercana (campos de distancia, ya activos). El resto es material; no se toca C++ si no hace falta.

## 5. Parámetros (para las 30 variantes)

Todos los parámetros van en `PathStyle`, una dataclass congelada:

- número de lazos y de niveles de anidado;
- número de cruces;
- número de túneles «de cerro»;
- ancho medio del camino y rango de ancho;
- altura de los bordes por bioma;
- longitud del camino;
- fracciones de bioma;
- ancho del río y densidad de islas;
- dunas del fondo (tupidez y altura);
- tamaño de los lagos de vista;
- número de castillos.

## 6. Tests

- Se llega andando (y saltando) del inicio al final.
- Cada lazo sale de su padre y vuelve a él, y es recorrible.
- Solo hay un final.
- Los lazos no abren atajos no previstos: se recorre el grafo real sobre la rejilla pisable.
- Cada cruce tiene paso por arriba y por abajo, pisable y alcanzable.
- **Borde bloqueado:** desde el camino no se llega a ninguna celda del fondo de vistas.
- **Sección sin cuenco:** en el suelo, dentro del 80 % central del ancho, la variación de cota es ≤ 0,3 m.
- **Río:**
  - las islas son alargadas en el sentido del flujo (relación ≥ 1,8 y ángulo con el flujo ≤ 30°);
  - no hay más de 3 islas seguidas en el eje del río.
- El final se abre y toca el agua.
- Sin picos: los tests actuales de puntas en la malla.
- Costura entre trozos y formato TNTM2 de ida y vuelta.
- UE: el cargador coloca 16 trozos a partir de un manifest 4 × 4.

## 7. Fuera de alcance

Huellas y marcas humanas (irían como decals), follaje, puzles de `ATN_ChunkManager` y tamaños de mapa distintos de 400 m. En Unreal se muestran los mapas nuevos en `LVL_MapVariants` y no se crea ningún nivel nuevo.

## 8. Entrega

1. Un mapa `C01` con la semilla fija, capturas en el editor y vista cenital de control.
2. Tras la validación de Rodrigo: 30 variantes, push de la rama y limpieza de memoria con revisión de Astra.


---

## Ampliación v2 (2026-09-26): cruces, puentes, barranco y paredes naturales

> Fusionado desde `2026-09-26-Terreno-CaminoPrimero-v2-Design.md`. Los encabezados de abajo bajan un nivel.

Fecha: 2026-09-26. Estado: aprobado en conversación por Rodrigo; pendiente de revisión del texto.
Parte de `Docs/Diseno_Terreno_CaminoPrimero.md` y del mapa C01 (`Scripts/terrain_path/`). Todo lo no mencionado aquí se conserva.

### 1. Motivo

Feedback de Rodrigo sobre C01:

- el agua gusta y se queda;
- los puentes generados casi nunca aparecen en el mapa;
- las paredes suben y se vuelven irregulares, pero todas acaban a la misma distancia del camino; parecen mesetas;
- el suelo y las paredes tienen la misma textura.

### 2. Qué no cambia

- El camino principal, los lazos, las bifurcaciones y las islas interiores que rodean los lazos.
- Las dunas normales fuera del trayecto (fondo de vistas), los lagos y el mar.
- El río del camino principal y el agua cartoon.
- El pie de la pared: 3–3,5 m casi verticales que cierran el paso.
- No se sale a las vistas.

### 3. Cruces y puentes

- De 1 a 4 cruces por mapa (`PathStyle.crossings` pasa a ser un rango).
- Cada cruce elige tipo al azar con una proporción del estilo (`bridge_share`):
  - **túnel bajo cerro**: el camino de arriba pasa sobre un montículo; el de abajo lo atraviesa con ≥ 8 m de roca sobre su suelo;
  - **puente fino**: el de arriba se estrecha a un tablero de 3–4 m de ancho sobre la luz del de abajo más 3 m por lado; el de abajo pasa al aire libre con ≥ 5 m de hueco libre.
- El tablero es una **pieza propia en la densidad 3D**: losa de 1,5–2 m con cara inferior en arco. Se suma después de excavar, así que la excavación ya no se lo come.
- El tablero se **funde con el terreno**: unión suave (soft max) con las paredes, que hacen de estribos; no debe leerse como una pieza pegada.
- Los arcos sueltos actuales sobre el camino pasan a ser esta misma pieza, sin camino encima.

### 4. Barranco

- 0 o 1 por mapa, según el estilo (`canyon`: `none`, `deadly`, `walkable`).
- Atraviesa el mapa de este a oeste serpenteando como un cauce natural: 20–35 m de ancho y 8–14 m por debajo del camino que lo cruza, con paredes de roca y el remate natural de la sección 5.
- El camino principal lo cruza por un puente fino (sección 3) con más luz.
- Fondo: río ancho con orillas de arena y el agua cartoon.
- **`deadly`**: ningún camino baja. El manifest trae `kill_boxes_uu` (cajas que cubren el fondo) y `ATN_MapVariantLoader` pone un `ATN_DeathZoneVolume` en cada una.
- **`walkable`**: un lazo baja al fondo por rampas, lo recorre por la orilla y vuelve a subir.

### 5. Paredes naturales

- Sobre el pie vertical, la parte alta es natural: sin norma fija (pico, corte, rugosidad, voladizo hacia dentro) y **nunca meseta**.
- La altura de la cresta y la distancia a la que acaba la pared varían a lo largo del camino. Donde la pared es baja, se ven las dunas de alrededor.
- Implementación: la banda de bloqueo (`block_band_m`) y la bajada a las vistas pasan a depender de un ruido de escala grande; la cima usa ruido multiescala (no un lomo senoidal de perfil fijo).
- El estilo fija el carácter por mapa: rango de altura extra, rango de profundidad y rugosidad.

### 6. Camino

- Un poco más ancho: moda del semiancho de 5,5 a 6,5 m (`width_m` = (4.0, 6.5, 10.5)).

### 7. Texturas

- Suelo: arena clara de grano fino.
- Pared: arena más oscura y compacta con vetas horizontales.
- Mezcla completa según la inclinación (a partir de ~35°), no el 60 % actual. El material usa un grano distinto en pared y suelo.

### 8. Catálogo de 30 mapas

- El agua cartoon vive en `LVL_MapVariants`: los 30 la tienen.
- Por mapa cambian la forma del camino, los cruces (1–4) y su tipo, las bifurcaciones y lazos, el barranco, el carácter de las paredes, el ancho y el río.
- Se generan después de que Rodrigo valide C01.

### 9. Pruebas

Automáticas, por mapa (tests de `Scripts/tests/test_terrain_path.py` y `check()` de `gen_terrain_path.py`):

- hueco libre bajo cada puente en toda la sección del camino de abajo;
- tablero andable y continuo;
- cada túnel tiene techo;
- recorrido de inicio a fin con todos los lazos, sin pisar las vistas;
- la altura de la cresta varía a lo largo del camino (sin mesetas);
- barranco `walkable`: el fondo se recorre; `deadly`: las cajas cubren todo el fondo.

Visual: Rodrigo valida C01 en `LVL_MapVariants`.


---

## Colocación por reglas de diseño (#652, 2026-10-04)

Generador offline que, sobre el grafo de un mapa «camino primero» (C01 y el catálogo), coloca puzles del catálogo (`Docs/Catalogo-Puzzles-2026-09-29.md`), mecánicas, enemigos, obstáculos, nidos de reaparición, botín y decorado. No toca el procedural de Mokius.

```bash
uv run python Scripts/place_terrain_path.py                 # C01_camino, semilla 652: manifest + placements.png
uv run python Scripts/place_terrain_path.py --comprobar     # valida el bloque escrito, sin tocar nada
uv run python Scripts/place_terrain_path.py --simular       # genera y valida, sin escribir
```

Módulos en `Scripts/terrain_path/`: `placement_site.py` (caminos, tramos bloqueados, uniones y grafo de distancias por el camino), `placement_catalog.py` (catálogo y cifras de las reglas), `placement_rules.py` (validador), `placement.py` y `placement_extras.py` (generador), `placement_io.py` (bloque del manifest) y `placement_sheet.py` (lámina). Tests: `Scripts/tests/test_terrain_placement.py`.

**Reglas que comprueba el validador** (distancias por el camino, no en línea recta):

| Regla | Valor |
|---|---|
| Separación entre puzles | ≥ 90 m |
| Calma junto a un puzle | sin enemigos ni obstáculos a media huella + 25 m |
| Densidad por ruta | en cada bifurcación, la ruta corta lleva más peligro por metro que la larga (diferencia de longitud ≥ 15 %); un atajo con ≥ 30 m libres lleva al menos uno. El parkour cuenta doble |
| Curva de intensidad | tramos de 50 m; tras un tramo ≥ 4 (pico) va uno ≤ 2 (calma); el primero y el último del principal, ≤ 2 |
| Exclusiones | nada de juego a < 40 m de la salida, 50 m de la meta, 12 m de una unión, 15 m de un cruce o 12 m de un nido |
| Posición | dentro del camino, fuera de túneles, tableros, arcos y escalones de medusa; en el río solo lo que vive en el agua; anchura mínima de cada plantilla; decorado a ≥ 4 m del eje |
| Alcanzable | se llega desde la salida y se sigue hasta la meta |
| Nidos | ≤ 200 m entre nidos del principal; uno entre 10 y 80 m antes de cada puzle de grupo del principal |
| Secuencia | dos puzles seguidos del principal no son del mismo tipo |

**Lo hecho a mano no se pisa.** El bloque `placements` del manifest tiene `auto` (lo rehace la CLI), `manual` (de los diseñadores: la CLI no lo toca y lo usa como restricción) y `suppressed` (ids de `auto` borrados a mano, que no vuelven). Regenerar el terreno con `gen_terrain_path.py` conserva el bloque; si cambia la semilla del mapa, lo marca `stale` y `--comprobar` falla hasta rehacerlo.

**C01 (semilla 652):** 5 puzles (1 de grupo en el principal, 2 de grupo en rodeos, parkour en un atajo y la catapulta de un meandro), 4 mecánicas, 18 enemigos y obstáculos, 4 nidos, 92 de botín y 158 de decorado y vegetación, sin violaciones. El principal de C01 está casi lleno de túneles, río, puente del barranco, escalones y 14 uniones: solo cabe un puzle de grupo en él; los otros dos van en rodeos que el grupo puede saltarse.

**Colocación al cargar el mapa.** `ATN_MapVariantLoader` (con `bSpawnPlacements`, activo por defecto) lee el bloque en `BeginPlay` y lo coloca con `ATN_MapPlacementSpawner` (`Source/Tortunabo/.../World/TN_MapPlacementSpawner*.cpp`; lectura en `TN_MapPlacements.cpp`). La cota se ajusta con una traza contra los trozos del terreno (3 m por encima y 15 m por debajo de la del manifest). Lo puesto a mano en el nivel manda: una entrada con una pieza de juego del nivel (o un actor con la etiqueta `TN_Manual`) dentro de su radio no se coloca. Un bloque `stale` solo coloca lo manual.

| Entrada | Pieza | Red |
|---|---|---|
| Enemigos, obstáculos, trampolín, pala, catapulta, plataforma móvil, cofre | `ATN_BeachElement::SpawnElement` | servidor, replicado |
| Decorado y pasarela | `ATN_BeachDecorField::BeginBuildPlaced` | local en cada máquina |
| Vegetación (`Palm`, `Shrub`, `Grass`) | flora del mapa procedural, instanciada y sin colisión | local; no en servidor dedicado |
| Géiser | `ATN_ProcGeyser` con `target_uu` | local (así es la clase) |
| Nidos | `ATN_ProcEggNest` numerados por `progress_m` (avance por el recorrido, comparable entre el principal y los lazos) | servidor |
| Caja de objetos, rebuscable, concha | `ATN_RaceItemBox`, `ATN_ProcSearchSpot`, `BP_ScorePickup` | servidor |
| `throw_chain` | `ATN_ProcThrowWall` + `ATN_ProcSwitch` | servidor |
| `plate_balance` | `BP_PressurePlate` ×3-5 + `BP_PressurePlateGroupManager`; **sin puerta** hasta que exista `ATN_PuzzleDoor` (N4) | servidor |
| `breakable_chain` | `ATN_BreakablePlatform` en zigzag (losa de cubo de serie) + trampolín en el hueco del medio | servidor |
| `shell_gauntlet`, `wobbly_run` | `ShellGate`, `WobblyPlatform` en fila | servidor |
| `catapult_gap` | sus piezas son la catapulta y el trampolín del bloque | — |

Las piezas de un tramo se reparten por `path_uu`, la polilínea del camino bajo la huella que escribe `placement_io.py`. Los puzles pendientes (`basket_hold`, `geyser_aim`, `think_room`) y los kinds desconocidos se registran en el log y se saltan. Tests sin ventana: `Automation RunTests Tortunabo.World.MapPlacements` (manifest de prueba `Scripts/tests/fixtures/manifest_placements.json`).

**Reaparición en los nidos.** Cada entrada automática lleva `progress_m`: la distancia por el camino a la salida entre la suma de las distancias a la salida y a la meta, por la longitud del principal (en el principal sin atajos es `s_m`). Los nidos se numeran por él, así que uno de un lazo va entre los del principal que lo rodean; uno manual sin `progress_m` toma el de la entrada más cercana. `ATN_ProcMapGameMode` reaparece sin generador procedural en el nido alcanzado más avanzado del mundo (`ATN_ProcEggNest::GatherWorldNests` y `PickRespawnNest`) y, sin ninguno alcanzado, en el `PlayerStart` de la salida. `BP_RunGameMode`, el de `LVL_Demo01`, no tiene reaparición en nidos: allí siguen sin efecto hasta que el Coop pase al modo del mapa procedural con un mapa fijo.

**Pendiente:** `build_demo_level.py` aún no lee el bloque; la puerta N4 de `plate_balance`; la reaparición en los nidos con `BP_RunGameMode`.
