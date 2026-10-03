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

- **`bank_deg`**: peralte de cada punto de `road_uu`, en grados. Positivo, el lado derecho de la marcha está más bajo (curva a la derecha); negativo, el izquierdo. |`bank_deg`| ≤ 15. El terreno ya lo lleva tallado; el juego todavía no lo lee. El siguiente paso en C++ puede usarlo para el piloto IA (velocidad de curva), la orientación de las puertas y los respawns y la colocación de la barrera.
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

- Con una barra entera de turbo, los saltos se pasan de la recepción y caen en la escapatoria llana. Cerca de 120 km/h, el choque es de unos 14 m/s (`impact_boost_ms`), dentro de la calzada. El juego no se ha probado en el editor.
- Las barreras de #303 las coloca el C++ a partir de `road_uu`; el validador solo comprueba que haya suelo a su cota.
