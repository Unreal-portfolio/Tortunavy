# Rally: circuitos por vueltas generados (#622)

Generador offline de circuitos cerrados para el Rally (decisión del 03-10 en #622): con una semilla saca un lazo con saltos con recepción, curvas peraltadas, horquillas, chicane, cambios de rasante y rectas de velocidad, rodeado de terreno volumétrico (campo de alturas que pasa por el marching cubes de `terrain_vol`, como E01B). La variante se escribe en `Scripts/terrain_volumes/Variants/<id>/` y el juego la carga con `LVL_Rally?Variant=<id>`, sin importar `.uasset`. E01B e I03R se conservan junto a los circuitos nuevos.

## Cómo se genera

```bash
uv run --with pyfqmr --with matplotlib python Scripts/gen_terrain_rally_circuit.py            # R01_circuito_dunas, semilla 622
uv run --with pyfqmr --with matplotlib python Scripts/gen_terrain_rally_circuit.py --seed 7 --name R02_circuito_<algo>
uv run pytest Scripts/tests/test_terrain_rally_circuit.py                                    # criterios sobre la variante escrita
```

`--no-decimate` evita pyfqmr y `--no-sheet` evita matplotlib. La generación tarda unos 100 s. Escribe los trozos TNTM2, `manifest.json`, `preview.png`, `preview_debug.png`, `CREDITS.txt` y `lamina.png` (planta, perfil de alturas con los vuelos y velocidad y peralte por el arco), y añade la entrada a `index.json`. Los trozos se versionan como los de E01B: R01 pesa 1,5 MB.

| Módulo | Qué hace |
|---|---|
| `Scripts/terrain_geo/rally_circuit_plan.py` | Planta: sorteo de piezas, orden por reglas, cierre por mínimos cuadrados y separación entre tramos |
| `Scripts/terrain_geo/rally_circuit_physics.py` | Física de punto del buggy: perfil de velocidad, turbo, vuelo y despegue sobre un perfil medido |
| `Scripts/terrain_geo/rally_circuit_elements.py` | Saltos (mesa con recepción), rasantes y peralte, dimensionados con esa física |
| `Scripts/terrain_geo/rally_circuit.py` | Perfil del eje, velocidades y terreno (calzada tallada y peraltada, berma, talud y dunas) |
| `Scripts/terrain_geo/rally_circuit_check.py` | Validador sobre la malla ya escrita |
| `Scripts/terrain_geo/rally_circuit_sheet.py` | Lámina de revisión |

No confundir con `Scripts/terrain_shapes/rally_circuit.py`, que hace los lazos de autor de I03R, I04 e I06 sobre el kit de formas.

## Reglas de colocación

- La recta de salida va primero; la línea está a 75 m de su principio, con la parrilla 2 × 4 detrás (10 m a la primera fila, 8 m entre filas, ±3,5 m: `TNRally::GridSlotOffset`), llana y sin peralte en [−60, +25] m.
- El resto, en orden sorteado: cada pieza de giro con la recta con elemento que la sigue. Las curvas peraltadas (4-5, radio 38-70 m, peralte 9-15°) giran todas hacia el lado del lazo, así que el lazo de base es convexo. Las dos horquillas (radio 18-22 m, 150-170°: sus ramas se abren en V) forman un zigzag con un salto entre ellas. La chicane tiene giro neto nulo.
- Los saltos van detrás de una pieza lenta (horquilla o chicane): se llega a ellos a velocidad media, no a fondo. Los rasantes, detrás de curvas peraltadas.
- Entre dos piezas hay siempre un enlace recto de longitud libre; esas longitudes se resuelven por mínimos cuadrados con cotas para que el lazo cierre. Dos tramos a más de 150 m por el arco quedan a 45 m o más en planta. Si un sorteo no cumple, se prueba otro orden y luego el intento siguiente; de lo que vale, el lazo más corto.

## Física del buggy (BuggySpec)

| Valor | Cifra | Fuente |
|---|---|---|
| Punta | 30,5 m/s (110 km/h) | `TNRallyTurret::BuggyTopSpeedCms` |
| Arranque | 8,3 m/s² hasta 60 km/h (0-60 en 2 s) | `UTN_BuggyData::MaxTorque` y `TNBuggy::TorqueCurveKeys` |
| Medio | 4,9 m/s² a 60 km/h, lineal hasta 0 en la punta (0-100 en 6,6 s) | Medida en I03R (`Rally_MVP.md`) |
| Frenada | 10,6 m/s² (de 60 km/h en 13 m) | Medida en I03R y E01B |
| Agarre lateral | 1,2 g; con peralte, v² = g·R·(µ + tan b) / (1 − µ·tan b) | `Rally_E01B_y_Biplaza.md` §1.2 |
| Turbo | punta × 1,15, par × 1,35 y +3 m/s², una barra de 3 s | `UTN_BuggyData` |

La velocidad de llegada a cada salto es la de la línea ideal en vuelta lanzada (acelera con la pendiente hasta el límite de curva y la punta, y frena hacia atrás). La de turbo supone una barra entera pisada en los 3 s anteriores al labio.

## Elementos

- **Salto**: rampa (transición de 10 m más un tramo recto a 5-14°), labio, mesa llana, rodilla, recepción recta (14-18°) y salida suave. El ángulo del labio da 0,6-1 m de vuelo sobre él a la velocidad de llegada, y la mesa mide lo justo para que ese vuelo caiga en el 78-90 % de su largo. Más lento se cae antes en la mesa; con turbo se pasa la rodilla. No se apunta a la rodilla: con una trayectoria tan tendida, unos centímetros de redondeo en la malla mueven el contacto 15 m. La recepción baja hasta la cota del pie (como mucho 5 m más abajo) y las subidas suaves de los enlaces recuperan esa cota.
- **Curva peraltada**: peralte en todo el arco y rampas de 14 m a cada lado; como mucho 15°.
- **Cambio de rasante**: joroba de coseno de 75-100 m y 2-3,5 m, con el radio vertical en la cima por encima de 1,15 · v² / g a la velocidad con turbo: ni con turbo se despega.
- **Sección**: calzada de 14 m y arcenes de 3 m con el peralte, berma llana a la cota del borde hasta 26 m del eje (las barreras de #303 van a 15-23 m) y talud de 33° hasta las dunas.

## Campos nuevos del manifest

Lo común de `write_map` y lo que ya lee `ATN_RallyTrack` (`mode` "rally", `closed` true, `laps`, `road_uu`, `road_width_m`, `checkpoints_uu`, `start_uu` = `end_uu`, `start_yaw`, `kill_boxes_uu`) siguen igual. En un circuito, `road_uu` no repite el primer punto al final (el lazo lo cierra `closed`), y la primera puerta de `checkpoints_uu` está en la línea de salida. Campos nuevos:

- **`bank_deg`**: peralte de cada punto de `road_uu`, en grados. Positivo, el lado derecho de la marcha está más bajo (curva a la derecha); negativo, el izquierdo. |`bank_deg`| ≤ 15. El terreno ya lo lleva tallado y el juego lo usa para inclinar las puertas con la calzada (`Docs/Rally_MVP.md`, «Circuitos por vueltas»).
- **`road_widths_m`** (tramos variados, director 04-10): ancho de la calzada de cada punto de `road_uu`, en metros, de 10 a 20 m, con transiciones lineales de 24 m. `road_width_m` pasa a ser el máximo, para que quien solo lea ese campo deje las barreras fuera de toda la calzada. Reglas de ritmo (`terrain_geo/rally_circuit_width.py`, generador propio `[seed, 622, 1]`, así que la planta y el perfil de una semilla no cambian): recta de salida ancha (17-18,5 m, adelantamiento y parrilla), horquillas anchas (18,5-20 m, desde 30 m antes, en la frenada), curvas peraltadas cerradas (radio < 50 m) algo anchas (15-16,5 m) y rápidas estrechas (12-13 m), saltos de 15-16 m (margen lateral en el aterrizaje), chicane, rasantes, baches y badén estrechos (10-12 m) y enlaces de 14 m. `width_sections` lista los tramos (`s_m`, `length_m`, `width_m`, `class` estrecho/normal/ancho y la pieza que lo pide) y cada elemento lleva su `width_m`. El juego lo lee (`TNRallyCircuit::ReadRoadWidths`, `ATN_RallyTrack::GetRoadWidthAtArcCm`): la barrera de #303 va pegada al borde de cada tramo y las filas de cajas «?» se aprietan en los estrechos. El validador (`rally_circuit_check_width.py`) exige el rango, transiciones de 0,5 m por metro como mucho, dos tramos estrechos y dos anchos de 30 m o más, 5 m entre el más estrecho y el más ancho, horquillas de 16,5 m o más, y mide los aterrizajes y la parrilla contra la media calzada del punto.
- **`elements`**: lista de elementos con `type` (`recta`, `curva_peraltada`, `horquilla`, `chicane`, `salto`, `rasante`), `id` y `s_m` (arco [inicio, fin] en metros desde la línea de salida, medido por `road_uu`). Las curvas llevan `radius_m`, `angle_deg` y `side`, y las peraltadas también `bank_deg` y `bank_signed_deg`. Los saltos llevan `lip_s_m`, `lip_deg`, `height_m`, `table_m`, `landing_deg`, `landing_s_m` (zona de aterrizaje), `land_design_s_m` y `land_boost_s_m`, `v_design_kmh` y `v_boost_kmh`, `airtime_s`, `impact_ms` e `impact_boost_ms`, y el diseño completo en `design`. Los rasantes llevan `crest_s_m`, `height_m` y `radius_m`.
- **`markers_uu`**: `parrilla` (los 8 huecos), `salto_N_labio`, `salto_N_aterrizaje` y `rasante_N_cima`.
- **`physics`**: la BuggySpec con la que se ha dimensionado. **`lap`**: largo y vuelta ideal. **`checks`**: el informe del validador y su veredicto (`recorrible` = todo en verde).

## Validador (sobre la malla escrita)

`rally_circuit_check.circuit_report` mide la cara superior de los trozos con colisión. El veredicto lo comprueba `Scripts/tests/test_terrain_rally_circuit.py`:

- Lazo cerrado y continuo: muestras de 0,5-1,5 m, también entre la última y la primera.
- Sin escalones de más de 0,4 m entre muestras de 1 m fuera de los saltos. Pendiente sostenida (30 m) ≤ 8°.
- Peralte ≤ 15° en el manifest y ≤ 15,5° medido a ±5 m del eje. Al menos 4 curvas con peralte ≥ 6°, cuyo peralte medido en el centro está a ±2° del nominal.
- Al menos 3 saltos. En cada uno, la velocidad de llegada se recalcula sobre la malla (error ≤ 8 % frente al manifest). El despegue es el primer punto en el que la curvatura convexa pide más aceleración de la que da la gravedad. A esa velocidad, el contacto cae en la zona de aterrizaje, a ≤ 5,5 m del eje y con un choque ≤ 5 m/s. Con turbo, toca dentro de la calzada y antes del final de la recta del salto.
- Al menos 2 rasantes de ≥ 1,5 m que no despegan con turbo.
- Radio ≥ 15 m, separación ≥ 40 m entre tramos alejados por el arco y parrilla de 8 huecos con ≤ 3° de pendiente.
- El suelo bajo las dos barreras, colocado con la fórmula de `FBarrierParams`, está a ≤ 1 m del borde de la plataforma.
- Puertas: la primera en la salida y separaciones de 120-300 m.

## R01_circuito_dunas (semilla 622)

Vuelta de 1 830 m (3 vueltas) en una rejilla de 8 × 6 trozos y 74 000 triángulos. La vuelta ideal dura 68 s. Lleva 4 curvas peraltadas (15°, 15°, 11° y 14°), 2 horquillas en zigzag, una chicane y 2 rasantes (2,3 y 2,6 m). Los 3 saltos se toman a 89-94 km/h, con 0,8 s en el aire y un aterrizaje en la mesa a unos 20 m del labio. El validador sale todo en verde.

Límites conocidos:

- Uso en el juego (saltos, rasantes y horquillas para el copiloto y el piloto IA): `Docs/Rally_MVP.md`, «Circuitos por vueltas».
- Con una barra entera de turbo, los saltos se pasan de la recepción y caen en la escapatoria llana. Cerca de 120 km/h, el choque es de unos 14 m/s (`impact_boost_ms`), dentro de la calzada. El juego no se ha probado en el editor; sin editor, el piloto IA da 5 vueltas sin atascarse.
- Las barreras de #303 las coloca el C++ a partir de `road_uu`; el validador solo comprueba que haya suelo a su cota.

## Circuitos de tierra (#682)

`--profile tierra` amplía el generador con elementos de circuito de tierra (por defecto, `R02_circuito_tierra`, semilla 682):

```bash
uv run --with pyfqmr --with matplotlib python Scripts/gen_terrain_rally_circuit.py --profile tierra   # unos 170 s
uv run pytest Scripts/tests/test_terrain_rally_tierra.py
```

El perfil `dunas` (R01) no cambia: los sorteos nuevos van con su propio generador (`[seed, 682]`) y misma semilla da el mismo trazado.

| Módulo | Qué hace |
|---|---|
| `Scripts/terrain_geo/rally_circuit_jumps.py` | Saltos de forma: doble, cresta y salto largo sobre hueco |
| `Scripts/terrain_geo/rally_circuit_dirt.py` | Baches (whoops y tabla de lavar), badén con barro, banqueta y la suspensión del buggy |
| `Scripts/terrain_geo/rally_circuit_check_dirt.py` | Validador de esos elementos sobre la malla escrita |

### Reglas de colocación

- Cuatro saltos: la **doble** y la **cresta** en el zigzag de las horquillas, la **mesa** de #622 tras la chicane y el **salto largo sobre hueco** detrás de una curva peraltada, donde se llega más rápido.
- Tras las curvas peraltadas (todas llevan algo detrás) se reparten el hueco (el primero de su curva), los dos rasantes, las dos rectas de baches y el badén.
- Baches y badén son rectas propias, con 10-12 m llanos a cada lado: nunca caen en curva (el validador lo mide en `road_uu`).
- Una recta de baches nunca va justo antes de una horquilla (#696, `rally_circuit_plan.bumps_before_hairpin`): sus baches caerían en la frenada. El orden de piezas que lo hace se descarta, sin gastar sorteos, así que los trazados que ya cumplían no cambian (las semillas 6938, 6923 y 7001 lo hacían).
- Las dos horquillas llevan banqueta: peralte de 10-13° y caballón por fuera.

### Elementos

- **Saltos de forma**: rampa como la de la mesa hasta el labio (7-13°) y después, en la doble y el hueco, cara trasera de 24°, vaguada (a la cota del pie en la doble y 2 m por debajo en el hueco) y cara de subida hasta la cresta de la recepción, 0,3 m (doble) o 0,9 m (hueco) por debajo del labio. La cresta está donde la trayectoria a 0,8 · v pasa 0,4 m por encima: el piloto IA, que llega a 0,9 · v, salva el hueco. La cara de recepción es convexa (de 2° a 12-20°) y se busca para el menor choque a v y a 0,9 · v; si pasa de 4 m/s (4,5 a la velocidad de la IA), se baja el labio de grado en grado. La cresta no tiene hueco: la cima redondeada cae directa a la cara. Las caras del hueco se suben andando si alguien cae dentro.
- **Baches**: ondas `A · (1 − cos(2πx/λ))` (montículos de 2A de pico a pico). Whoops: A 0,14-0,19 m, λ 8-11 m; tabla de lavar: A 0,07-0,10 m, λ 4,5-6 m. Límites: A ≤ 0,8 · min(SuspensionMaxRaise, SuspensionMaxDrop) = 0,20 m (25 cm en `TN_BuggyWheel.cpp`, que lee el test), radio del valle ≥ radio de la rueda (0,504 m), λ ≥ 4,5 m (vóxel de 1 m). Los trozos con baches o badén se deciman a 0,02 m y la calzada de los trenes de baches (con 4 m más a cada lado) no pasa por el suavizado de Taubin de `terrain_vol/mesh.py` (`RallyCircuitModel.unsmoothed_mask`, #696): el suavizado es un paso bajo y dejaba la tabla de lavar al 31-60 % de su amplitud (peor cuanto más corta y más alineada con la rejilla); sin él queda al 74-89 %. El marching cubes de un campo de alturas ya pone los vértices a la cota exacta.
- **Badén**: coseno hacia abajo de 0,45-0,65 m con el largo justo para 0,8 g de curvatura vertical a la velocidad con turbo (ni despega ni hunde la suspensión); el color de los vértices pasa a barro (`mud_mask`, `terrain_vol/mesh.py`).
- **Banqueta**: caballón de 0,9 m que sube desde 6 m del eje hasta 10,5 m y vuelve a la berma en 14 m, antes de las barreras de #303.

### Campos nuevos del manifest

- Saltos: `jump_kind` (`mesa`, `doble`, `cresta`, `hueco`); los de forma, `gap_s_m` (labio y cresta de la recepción), `v_ai_kmh`, `land_ai_s_m` e `impact_ai_ms`. Siguen siendo `type: "salto"`, así que el copiloto y el piloto IA los leen como antes.
- `baches` (`pattern`, `amplitude_m`, `wavelength_m`, `count`, `peak_to_peak_m`, `train_s_m`, `liftoff_kmh`), `baden` (`depth_m`, `length_m`, `dip_s_m`, `surface: "barro"`, `curvature_g`) y `banqueta` (`outside`, `bank_deg`, `berm_rise_m`). El C++ los lee como `EElementKind::Other`: no frenan a la IA ni tienen nota del copiloto.
- `suspension` (subida, bajada, radio de rueda y amplitud máxima) y `generator.profile: "tierra"`.

### Validador

Además de lo de #622, con el perfil tierra el veredicto pide (`rally_circuit_check_dirt.tierra_verdict`): al menos 4 saltos de al menos 2 tipos y, en la doble y el hueco, que el vuelo medido a 0,9 · v caiga en la cara de recepción con un choque ≤ 6 m/s; dos tramos de baches (whoops y tabla de lavar) en recta (curvatura ≤ 1/400 m), con la amplitud medida en la malla a −4, 0 y +4 m del eje entre el 60 % del diseño y el límite de la suspensión (+2 cm); el badén con la profundidad medida a ±0,15 m y ≤ 0,95 g con turbo; y la banqueta con al menos el 60 % del caballón medido y peralte ≥ 8°. Desde #696, `bumps_braking`: cada tren de baches acaba antes de donde el piloto IA empieza a frenar para la siguiente horquilla (de la velocidad de la línea ideal al final del tren, como mucho 90 km/h, a la de la horquilla con 0,6 g de lateral, frenando a 0,5 g: los números de `ATN_RallyAIController`, que un test lee del C++). Lo comprueba `Scripts/tests/test_terrain_rally_baches.py` en R02 a R06.

### R02_circuito_tierra (semilla 682)

Vuelta de 2 281 m (3 vueltas) en 9 × 8 trozos y 124 000 triángulos; la vuelta ideal dura 82 s. Saltos: doble a 94 km/h (hueco de 16,5 m, 1,5 s en el aire), cresta a 93 km/h (1,2 s), mesa a 94 km/h (0,8 s) y hueco a 104 km/h (hueco de 20,6 m, 1,5 s); choque medido de 3,3-3,8 m/s y, a la velocidad de la IA, 2,9-4,4 m/s. Whoops de 7 ondas (A 0,18 m, λ 9,6 m, medida 0,17 m) y tabla de lavar de 11 (A 0,08 m, λ 5,9 m, medida 0,07 m); badén de 0,61 m y 43 m; banquetas de 12,4° y 11,6° con 0,77-0,81 m de caballón medido. Cinco curvas peraltadas (9-13°) y dos rasantes (2,9 y 2,4 m). El validador sale todo en verde. Sin editor (04-10, 1 bot, `-server -nullrhi`), el piloto IA da 3 vueltas en 336 s (112 s por vuelta) con `terminados 1/1, atascos 0, vuelcos 0, caidas 0`. No se ha probado en el editor.

`open LVL_Rally?Variant=R02_circuito_tierra` (con `?Laps=N` y `?Bots=N`) la carga como R01. `Automation RunTests Tortunabo.Rally.Tierra` comprueba que se construye como circuito con 4 saltos, sus notas, la frenada de la IA y la barrera continua.

## Catálogo del Rally: R03 a R06 (#692)

Desde #692 el selector del Rally (sala, Misión, general y pausa) solo ofrece circuitos del generador de vueltas: `TNLobbyMission::IsRallyCircuitManifest` exige `generator.generator == "rally_circuit_vueltas"`, y E01B, I03R, I04 e I06 salieron del selector y del repo (ningún otro modo las usaba). El orden es R01 a R06 y el circuito por defecto, R01.

Los circuitos nuevos van en `terrain_geo/rally_circuit_themes.CIRCUITS` (semilla, perfil y tema) y se regeneran con `gen_terrain_rally_circuit.py --all-circuits` (o `--circuit <nombre>`). El tema cambia el relieve de alrededor, la paleta de arena de los colores de vértice, el color de la calzada y el del barro; no toca el trazado ni los elementos. El tema `base` reproduce R01 y R02. Todos con el perfil tierra y el validador en verde (`Scripts/tests/test_terrain_rally_catalogo.py`).

| Circuito | Semilla | Tema | Vuelta | Saltos (llegada) | Baches | Badén |
|---|---|---|---|---|---|---|
| R03_circuito_dunas_costeras («Dunas Costeras») | 6928 | dunas altas, arena de playa | 2 398 m, 85 s ideal | mesa 96, hueco 107, doble 91, cresta 94 km/h | tabla A 0,08 m λ 5,7 m; whoops A 0,14 m λ 10,9 m | 0,46 m |
| R04_circuito_cantera («La Cantera») | 6929 | lomas cortas y altas con estratos | 2 358 m, 84 s | mesa 93, hueco 105, doble 91, cresta 96 km/h | whoops A 0,16 m λ 8,8 m; tabla A 0,09 m λ 5,5 m | 0,54 m |
| R05_circuito_marismas («Marismas») | 6927 | llano de arena húmeda, más barro | 2 388 m, 86 s | doble 88, cresta 98, hueco 106, mesa 94 km/h | tabla A 0,09 m λ 5,0 m; whoops A 0,18 m λ 10,2 m | 0,65 m |
| R06_circuito_lomas («Lomas Secas») | 6933 | colinas redondas, pista clara | 2 276 m, 82 s | hueco 105, doble 89, cresta 97, mesa 95 km/h | whoops A 0,14 m λ 8,9 m; tabla A 0,08 m λ 5,4 m | 0,52 m |

Semillas descartadas: 6923 (puertas a 99 m y tabla de lavar medida al 31 %), 6939 (tabla medida al 55 %) y 6938 (whoops justo antes de la primera horquilla: en la primera vuelta el piloto IA se pasó la horquilla, cruzó la puerta 7 por fuera y tardó 277 s en volver).

Piloto IA sin editor (04-10, 1 bot, 3 vueltas, `-server -nullrhi`, `LVL_Rally?Variant=V?Bots=1?AutoStart?Races=1?Laps=3`): todos terminan sin atascos ni caídas.

| Circuito | Tiempo (3 vueltas) | Atascos | Vuelcos | Fuera de pista |
|---|---|---|---|---|
| R03 | 385 s | 0 | 5 | 0 |
| R04 | 379 s | 0 | 3 | 0 |
| R05 | 389 s | 0 | 8 | 0 |
| R06 | 372 s | 0 | 5 | 0 |
| R02 (control, mismo día) | 367 s | 0 | 1 | 0 |

Los vuelcos son momentáneos (el ritmo por vuelta no cambia y no hay reapariciones) y se repiten en los mismos sitios: curvas peraltadas a 40-55 km/h, la salida de horquillas con banqueta y, en R06, la mesa tras la chicane a 55 km/h (diseñada para 95). Falta verlos en el editor. No se ha probado en el editor.

**Vuelcos (#695).** La causa no era el trazado: los 39 vuelcos de dos tandas de carreras (R02 a R06) llegaron todos 0,2-0,4 s después de que el bot gastara un mortero (37 disparos). El bot lo dispara hacia delante cuando lo ha guardado 8 s sin nadie a tiro, y el retroceso del mortero levantaba el morro 560 cm/s (700 · 0,8): a 50 km/h el buggy daba la vuelta. Peraltes, banquetas y la mesa de R06 solo eran el sitio donde se le acababan esos 8 s. El levantamiento del retroceso tiene ahora tope (`TNRallyTurret::MaxRecoilLiftCms`, 200 cm/s, el de la concha; el frenazo horizontal no cambia) y lo mide `Tortunabo.Rally.Measure.RecoilNoFlip`.

Tras #695 y #696, con R01 a R06 regenerados (04-10, mismas condiciones):

| Circuito | Tiempo (3 vueltas) | Atascos | Vuelcos | Morteros gastados | Tabla de lavar medida |
|---|---|---|---|---|---|
| R01 | 300 s | 0 | 0 | 1 | (sin baches) |
| R02 | 368 s | 0 | 0 | 1 | 87 % |
| R03 | 383 s | 0 | 0 | 3 | 82 % |
| R04 | 377 s | 0 | 0 | 2 | 76 % |
| R05 | 382 s | 0 | 0 | 1 | 74 % |
| R06 | 370 s | 0 | 0 | 3 | 84 % |

La regeneración solo cambia los trozos de los baches (3-6 por circuito) y los campos nuevos del veredicto; el trazado, los elementos y R01 quedan igual. No se ha probado en el editor.
