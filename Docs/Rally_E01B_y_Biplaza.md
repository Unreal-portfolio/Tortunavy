# Rally Tortuga: E01B (España conducible) y buggy biplaza con artillera

Fecha: 2026-09-29 · Base: `main` 41a0ee8a5 · Complementa `Docs/2026-09-29-Plan-Maestro-Modos-y-Mapas.md` §3.3 y §7 (respuestas 2, 3, 4 y 8).
Notación: `HellYeah:` = `C:\Users\Rodrigo\PERSONAL\ProyectosPersonales\HellYeah\HellYeah\Source\HellYeah\Vehicles\`; `[B]`/`[C]` = informes en `Docs/Analisis/2026-09-29/`; «est.» = estimación propia sin medir; `[1.ª pasada]` = valor de ajuste para playtest.
Estado de cada función: **MVP** (entra en la primera versión jugable), **Después** (diseñada, no bloquea), **Fuera** (descartada).

## 0. Resumen

| Tema | Decisión |
|---|---|
| Velocidad media del buggy | Línea ideal 80 km/h (22,3 m/s); con errores y choques 72 km/h (20 m/s). Recta 95–105 km/h; herradura de radio 25 m, 55 km/h; subida de 6°, 85 km/h |
| Duración de carrera | 120 s para el ganador; el último, unos 150 s (corte a los 60 s del ganador) |
| Trazado | 2 400 m de eje, 5 tramos, 13 checkpoints |
| Formato | **Punto a punto**, de los Pirineos a la Costa del Sol |
| Escala del mapa | 1 m de juego = 0,4 km (mapa de 1,33 × 2,19 km; malla detallada solo en una banda de ±150 m del eje) |
| Artillera | 5 acciones + 1 descartada; **MVP: Lanzar ítem y Contrapeso** |
| Assets nuevos del Rally | 2 imprescindibles (`M_BuggyPaint`, carrocería biplaza), 0 por acción, 3 deseables |

## 1. Valores reales del buggy y velocidades deducidas

### 1.1 Datos de HellYeah (rama `feat/f0-f1`)

| Parámetro | Valor | Fuente |
|---|---|---|
| Par máximo | 850 N·m | `HellYeah:HYBuggyData.h:20` |
| Régimen máximo | 3 400 rpm (el par cae a cero ahí; punta ≈ 110 km/h sin boost, extrapolada) | `HYBuggyData.h:29`, comentario `:23-26` |
| Relación final | 2,0 (0–60 km/h en 3,0 s, patinaje inicial 0,2) | `HYBuggyData.h:41`, comentario `:33-38` |
| Fricción delantera / trasera / freno de mano | 3,0 / 2,6 / 1,4 | `HYBuggyData.h:45, 49, 53` |
| Punta medida en recta (140 m de carril) | 91–99 km/h | `HellYeah:../../docs/spike-f0.md:41-42` |
| Aceleración esperada | 0–60 km/h en unos 3 s; se aplana hacia 110 km/h | `docs/verificacion-manual.md:89-90` |
| Boost | 2,5 s de duración, recarga de 6 s, par ×1,8, empuje extra 500 cm/s² (5 m/s²) | `HYBuggyData.h:81, 85, 89, 97` |
| Boost medido | «más de 100 km/h» subiendo un escalón de 25 cm | `docs/verificacion-manual.md:97` |
| Rueda | radio 51 cm, ancho 35 cm, rigidez de curva 750 | `HYBuggyWheel.cpp:13-15` |
| Suspensión | recorrido 25 cm arriba y abajo; amortiguación 0,25; muelle 100; precarga 100 | `HYBuggyWheel.cpp:16-21` |
| Freno | 6 000 N·m por rueda | `HYBuggyWheel.cpp:23` |
| Dirección | 40° en la rueda delantera × `AngleRatio` 0,7 = 28° efectivos (*inferido* de la semántica de Chaos) | `HYBuggyWheel.cpp:32`, `HYBuggy.cpp:228-229` |
| Batalla | 3,03 m | `HellYeah:../../Tools/Blender/props/buggy.py:10` |
| Centro de masas | (0, 0, 40 cm); altura de chasis 160 cm; resistencia 0,1 | `HYBuggy.cpp:209-212` |
| Golpe de rueda al subir un escalón | mínimo 4 cm; 15 % de la velocidad teórica; tope 200 cm/s | `HYBuggyData.h:105, 109, 113` |
| Atropello | a partir de 600 cm/s de velocidad relativa (21,6 km/h) | `HYBuggyData.h:73` |
| Enderezado | 2 s volcado antes de poder enderezar | `HYBuggyData.h:77` |
| **Masa** | **No está en el código.** Sale del PhysicsAsset de `SKM_Offroad` (no leído). `Chassis->GetMass()` en `HYBuggy.cpp:434` la da en ejecución | — |

### 1.2 Velocidades deducidas (est. propia; el radio y la pendiente se comprueban con `buggy_measure`)

Radio mínimo geométrico: 3,03 m / tan 28° = 5,7 m. No es el límite real: manda el agarre lateral. Con una aceleración lateral de 1,2 g (est.; coincide con [B] `B_buggy_sync.md:116-117` (eliminado): «≥ 25 m a 60 km/h, ≥ 50 m a 90 km/h»), v = √(a·r).

| Situación | Cálculo | Velocidad de diseño |
|---|---|---|
| Recta llana | Punta medida 91–99, teórica 110 | 95–105 km/h |
| Recta con boost | +5 m/s² durante 2,5 s, con el límite de régimen | 115–125 km/h (est.); ahorra 0,5–0,8 s por uso |
| Curva de radio 25 m | √(11,8 × 25) = 17,2 m/s = 62 km/h; con frenada y salida | 55–60 km/h |
| Curva de radio 50 m | √(11,8 × 50) = 24,3 m/s = 87 km/h | 85 km/h |
| Curva de radio ≥ 80 m | Manda la punta | 100–105 km/h |
| Subida de 6° | La rampa resta g·sen 6° = 1,0 m/s² a una aceleración disponible de 2–3 m/s² a 90 km/h (est.) | 85 km/h |
| Subida de 9° | Resta 1,6 m/s² | 75 km/h (est.) |
| Bajada de 8° | Sin límite por pendiente; frena el conductor | 55 km/h en herradura, 90–100 en recta |
| Salida | 0–60 km/h en 3 s cubre unos 25 m | 60 m de recta de salida en 4,2 s |

Velocidad media: la línea ideal del §2.4 da 22,3 m/s (80 km/h). Con un 12 % más de tiempo por trazadas imperfectas, choques y cajas, la media de carrera es 20 m/s (72 km/h). Esa cifra dimensiona el trazado.

## 2. E01B: España conducible

### 2.1 Duración y longitud

| Concepto | Valor | Motivo |
|---|---|---|
| Ganador | 120 s | Director: «partidas MUY rápidas». [C §3] proponía 3–6 min; queda superado |
| Último | ≈ 150 s | Corte a los 60 s del ganador: nadie espera |
| Longitud del eje | 120 s × 20 m/s = **2 400 m** | Media de carrera del §1.2 |
| Ítems por jugador | 5–6 por carrera | 7 filas de cajas, se recogen ~80 % |

Contradicción a resolver: el plan §3.3 justifica los ítems con «una carrera de 8–10 min». Con 2 min el catch-up sigue funcionando (unos 5 ítems por artillera), pero la sesión de juego debe ser una **copa de 3 carreras** (E01B, E01B con arranque en cuadrícula invertida por puntos y otro mapa). Ver pregunta 1.

### 2.2 Escala y tamaño

- Escala **1 m de juego = 0,4 km** (dentro de la horquilla 0,5–0,7 km de [B §5], algo más ajustada). A 0,5 km la sinuosidad necesaria para llegar a 2 400 m sería 1,43 de media; a 0,4 km es 1,14, natural para una carretera.
- Hitos y distancias en línea recta:

| Hito | Lat, Lon | Recta hasta el siguiente (km reales → m de juego) |
|---|---|---|
| H0 Somport (Pirineo aragonés) | 42,79, −0,53 | 128 → 320 |
| H1 Zaragoza (Ebro) | 41,66, −0,88 | 226 → 565 |
| H2 Guadalajara (meseta) | 40,63, −3,17 | 112 → 280 |
| H3 Toledo (garganta del Tajo) | 39,86, −4,02 | 174 → 435 |
| H4 Despeñaperros (Sierra Morena) | 38,35, −3,50 | 199 → 497 |
| H5 Málaga, playa (Costa del Sol) | 36,72, −4,42 | — |

Total en recta 2 097 m; eje de 2 400 m, sinuosidad media 1,14.

- Ventana geográfica: 0,83 × 1,69 km más 250 m de margen por lado = **1,33 × 2,19 km** (2,9 km²). Cumple «lado ≤ 2,4 km» y «centrado en el origen» del plan §2.5.
- Malla: banda de ±150 m del eje (2 400 × 300 m = 0,72 km²) a 1 M tri/km² ≈ 0,72 M; el resto (≈ 2,2 km²) a voxel de 2–4 m y 0,2 M tri/km² ≈ 0,44 M. **Total ≈ 1,2 M triángulos, ≈ 20 MB de StaticMesh de editor** (17 MB por millón [A §3]). Unos 100 trozos de 100 m con contenido (el resto, vacío o de fondo).
- Altura: el relieve real a 1 m = 0,4 km apenas se nota (exageración vertical E = 3,2 daría cimas de 27 m). El eje y el terreno próximo llevan **cotas de autor** (§2.4); fuera de la banda manda el MDE con E = 3–4.
- Rango Z del voxelizado: hoy 89 niveles de 0,5 m = 44,5 m ([C §1]). E01B llega a +44 m en la salida y con la roca sobre el túnel a ≈ +55 m: **128 niveles (64 m)**. Ver pregunta 4.

### 2.3 Punto a punto o circuito

| Criterio | Punto a punto | Circuito de 2 vueltas |
|---|---|---|
| Puntos emblemáticos de España | Los 5 aparecen una vez | Se ven 2 veces; cabe la mitad de variedad |
| Malla y memoria | 2 400 m de corredor únicos | 1 200 m únicos, pero la vuelta pide cerrar el lazo y el mapa pierde su geografía |
| Sensación de viaje | Salida en la montaña, meta en la playa | Sin meta natural |
| Adelantamientos | Se producen en el tramo; el líder puede escapar | Se cruzan doblados: más contacto, más caos |
| Catch-up | Cajas + rebufo; sin cierre del lazo | Igual, más lapeo |
| Red (8 buggies) | Igual | Igual |
| Coste de trazado | Ruta por hitos (`route.py`) | Ruta cerrada + bifurcación de la línea de meta |

**Elección: punto a punto.** Cuenta la geografía de España de arriba abajo, cabe en la malla del §2.2 y la meta en la arena de la playa cierra la carrera. La cuadrícula de salida invertida por puntos entre carreras compensa la ventaja del líder. **Después:** variante «reversa» (de la costa a los Pirineos) y una copa con E01B recortado a los tramos 3–5.

### 2.4 Secuencia de tramos

Cotas en metros de juego sobre el agua del mar (0). Los tiempos son de línea ideal; la carrera real es un 12 % más lenta.

| # | Tramo | Arco (m) | Longitud | Recta hitos → sinuosidad | Cota (entrada → salida) | Pendiente máx. sostenida | Radio mín. | Ancho | Velocidad | Tiempo |
|---|---|---|---|---|---|---|---|---|---|---|
| T1 | **Pirineos**: herraduras y túnel | 0–450 | 450 | 320 → 1,41 | +44 → +10,6 | 8° bajada | 25 m | 16 m en la salida (60 m), luego 14 m; túnel 14 m | 55–85 km/h | 25,7 s |
| T2 | **Valle del Ebro y meseta**: puente de Mokius, subida y salto | 450–1070 | 620 | 565 → 1,10 | +10,6 → +30,1 | 6° subida; rampa de 14° en 25 m | 50 m | 18 m (puente 14 m) | 85–105 km/h | 22,9 s |
| T3 | **Tajo**: garganta, bifurcación con puente natural | 1070–1410 | 340 | 280 → 1,21 | +30,1 → +23,1 | 8° (ruta B) | 25 m (ruta B) | 14 m (ruta B), 12 m (puente natural) | 65–90 km/h | 14,1 s (A) / 15,4 s (B) |
| T4 | **Sierra Morena**: subida y paso estrecho | 1410–1850 | 440 | 435 → 1,01 (eses de amplitud ≤ 30 m) | +23,1 → +36,2 → +21,5 | 5° | 25 m (paso de Despeñaperros) | 14 m, 12 m en el paso | 60–95 km/h | 19,2 s |
| T5 | **Bajada a la costa**: viaducto, herraduras, playa | 1850–2400 | 550 | 497 → 1,11 | +21,5 → +0,8 | 7° | 25 m | 14 m, 20 m en la playa | 55–110 km/h | 25,2 s |
| | **Total** | | **2 400** | 2 097 → 1,14 | | 8° | 25 m | ≥ 12 m | media 80 km/h | **107,7 s** ideal → ≈ 120 s real |

Desglose de elementos por arco (m):

| Tramo | Elementos |
|---|---|
| T1 | 0–60 recta de salida (parrilla 2 × 4, 8 m entre filas, 60 m de longitud) · 60–145 herradura 1 (r 25, −8°) · 145–210 recta (−2°) · **210–330 túnel** de 120 m (recto, r ≥ 120 dentro, 14 m de ancho, 8 m de alto, −3°) · 330–415 herradura 2 (r 25, −8°) · 415–450 recta |
| T2 | 450–520 aproximación · **520–620 puente de Mokius sobre el Ebro** (100 m, nivel +10,6, cauce a 0, ancho 14 m) · 620–770 subida a la meseta (+6°, r ≥ 50) · 770–830 recta · **830–855 rampa de salto** (+14°, 25 m) · 855–915 aterrizaje (−6°, 60 m) · 915–1070 recta rápida de meseta (+1,5°, r ≥ 80) |
| T3 | 1070–1150 bajada a la garganta (−5°, r ≥ 50) · 1150–1210 curvas (r ≥ 40) · **1210–1370 bifurcación** de 160 m: ruta A por el **puente natural** (nivel +23,1, arco de roca tallado, barranco de 22 m, sin barandilla, 12 m de ancho, rápida) o ruta B por el cañón (−8° en 70 m, 20 m llanos, +8° en 70 m, r ≥ 25) · 1370–1410 unión |
| T4 | 1410–1560 subida (+5°, r ≥ 60) · 1560–1640 paso de Despeñaperros (roca a ambos lados, r 25–60, 12 m) · 1640–1850 bajada (−4°, r ≥ 60) |
| T5 | **1850–1930 viaducto de Mokius** (80 m, nivel +21,5, barranco de ≈ 22 m) · 1930–2010 herradura 1 (r 25, −7°) · 2010–2080 recta (−1°) · 2080–2160 herradura 2 (r 25, −7°) · 2160–2300 recta costera (llana, 140 m, +0,8) · 2300–2400 playa (arena, 20 m de ancho); meta bajo el arco de neumático a los 2 400 m |

Diferencia A/B de la bifurcación: A es 1,3 s más rápida (6,4 s frente a 7,7 s) y cuesta una caída si se sale del puente (rescate + 3 s, ver más abajo); B lleva una caja doble. Ningún camino domina.

### 2.5 Puentes, túnel, cajas y checkpoints

**Puentes** (decisión 4 del director, los dos tipos):

| Tipo | Dónde | Cómo | Estado |
|---|---|---|---|
| Puente de Mokius | Ebro (520–620), viaducto (1850–1930) | Marcadores `BridgeStart`/`BridgeEnd` en el manifest; un script coloca el asset con los anclajes de inicio y fin (`ATN_BridgeSpan`, aún sin crear: `Grep` sin resultados en `Public/`). La colisión de la cubierta ha de responder a Vehicle | **MVP**: Ebro. **Después**: viaducto |
| Puente natural | Tajo, ruta A (1290–1350 aprox.) | Arco de roca tallado en el campo de densidad: sólido en el arco, hueco bajo él y barranco de 22 m; se define como una resta de un cilindro en `terrain_vol/density.py` | **Después** (con la bifurcación) |

El ancho del asset de Mokius debe ser ≥ 12 m (pregunta 3). Sin oscilación: el «puente tambaleante» queda para TcT.

**Túnel** (210–330): resta de un cilindro (túnel recto de 14 × 8 m) del campo de densidad, con `safe_low/safe_high` y `WalkableZAt` del plan §2.2 (riesgo R4: sin ello, `TerrainHeightMap` devuelve el techo). **MVP**. Cámara: el `SpringArm` con `bDoCollisionTest` (`HYBuggy.cpp:202`) se acerca dentro del túnel; hay que probar que no atraviese el techo.

**Cajas de ítems**: 7 filas de 4 cajas (separación 3 m, 9 m de ancho en pistas de ≥ 12 m). Ninguna sobre puentes, túnel ni saltos.

| Fila | Arco (m) | Zona |
|---|---|---|
| 1 | 445 | Salida de la herradura 2, antes del Ebro |
| 2 | 700 | Tras el puente |
| 3 | 960 | Tras el aterrizaje |
| 4 | 1 150 | Antes de la bifurcación |
| 5 | 1 440 | Subida de Sierra Morena |
| 6 | 1 720 | Salida del paso |
| 7 | 2 045 | Entre herraduras |
| Premio | 1 290 | Sobre el puente natural (ruta A), 1 caja extra |

Sin cajas en los últimos 355 m: el final se decide por conducción y por los ítems ya guardados. `RollLoot` pondera por puesto (`TN_RaceItems.h:153`).

**Checkpoints** (13, `checkpoints_uu` del manifest; regla de [C §4.5]: cada 150–250 m):

| # | Arco (m) | # | Arco (m) |
|---|---|---|---|
| 1 | 150 | 8 | 1 390 |
| 2 | 345 | 9 | 1 540 |
| 3 | 500 | 10 | 1 700 |
| 4 | 650 | 11 | 1 840 |
| 5 | 820 | 12 | 2 090 |
| 6 | 1 000 | 13 | 2 280 |
| 7 | 1 180 | Meta | 2 400 |

Separación máxima 250 m (viaducto y herraduras). Sin checkpoint dentro del túnel, sobre puentes ni en el interior de la bifurcación (los dos ramales confluyen en el 8). Caída, mar o volcado sin remedio: reaparición en el último checkpoint con **3 s de penalización** [1.ª pasada] y `SelfRight` a los 4 s ([B §7]). En la bifurcación, el punto de reaparición es el 7.

### 2.6 Cumplimiento de los límites de §3.3

| Límite (`Plan §3.3`, [B §5]) | Valor del diseño | Cumple |
|---|---|---|
| Pendiente sostenida ≤ 12° | 8° máximo (herraduras y ruta B) | Sí |
| Pendiente corta ≤ 20° | Rampa de salto de 14° en 25 m | Sí (excepción declarada al validador: una sola rampa, con 60 m de aterrizaje) |
| Radio ≥ 25 m | 25 m en las 5 herraduras y el paso | Sí |
| Ancho ≥ 12 m | 12 m en el puente natural y el paso; 14 m en el resto | Sí |
| Peralte ≤ 5° (8° en curva) | Tallado con ese tope | Por validar |
| Escalón ≤ 0,4 m entre muestras de 1 m | Solo en el eje; fuera se relaja | Por validar |
| Checkpoint cada 150–250 m | 13 puntos, máx. 250 m | Sí |

### 2.7 Generación y coste

Pipeline (Python, `Scripts/`): `gen_terrain_spain.py` (hoy con el mapa E01: `DEFAULT_START` Roncesvalles, `DEFAULT_END` Santiago, `CLIMB_M = 1.0`, líneas 32-34) gana una variante `E01B_rally` que lee un `GeoRegion` (plan §3.3), traza el eje por los 6 hitos con `terrain_vol/route.py`, talla el corredor (gaussiana σ 8–10 m, arcén de 10 m), añade túnel y arco natural al campo de densidad y escribe `checkpoints_uu`, marcadores de puente y `kill_boxes_uu`. Por tratarse de terreno volumétrico propio (decisión del director), no se usa Landscape.

| Tarea | Estado | Horas (est.) |
|---|---|---|
| `GeoRegion` + ruta por hitos + tallado del corredor + validador | **MVP** | ya en F6b (63–99 h) |
| Túnel recto en el campo de densidad | **MVP** | 6–8 |
| Puente de Mokius (marcadores + colocación + colisión Vehicle) | **MVP** (Ebro) | 8–12 |
| Cauce del Ebro con lámina de agua local (reutiliza `M_GridWater`, sin plugin Water) y `kill_boxes_uu` propio | **MVP** | 4–6 |
| Checkpoints, meta y actores de reaparición | **MVP** | ya en [C §6 #6] (10–16 h) |
| Bifurcación A/B con puente natural | Después | 8–10 |
| Rampa de salto + validación de aterrizaje | Después | 3–4 |
| Viaducto de Mokius | Después | 2–3 (reutiliza el actor del Ebro) |
| Piloto IA que recorre los 2 400 m (0 atascos y 0 vuelcos en 10 recorridos) | **MVP** | 6–8 |

Total añadido sobre el plan: **MVP 24–34 h, Después 13–17 h**.

Riesgos: rango Z (pregunta 4); Chaos sobre malla de triángulos con túnel (colisión simple decimada, plan §6); el techo del túnel a ≈ +55 m fuerza el rango; el cauce no está en el modelo y se crea a mano.

## 3. La artillera

### 3.1 Papel y reglas comunes

**Experiencia buscada:** la conductora lleva la línea; la artillera lee la carrera y decide cuándo dispara, cuándo se apoya y cuándo hace el ganso. Las dos comparten victoria; el fallo de una arruina a la otra.

**Asiento.** Trasero, la tortuga **no va en el caparazón**. Contradice [B §2] («van en la caja metidos en el caparazón sin física»): con acciones activas hace falta que se vea y se mueva. Va posada en el asiento (attach de `TN_CarryComponent.cpp:457-466`), con cámara propia libre alrededor del buggy y `DisableMovement`. Ctrl (caparazón) queda bloqueado sentada. En expulsión sí pasa a caparazón (`StartBody`, [B §2]).

**Sola.** La conductora hace las dos cosas: solo está disponible **Lanzar ítem** (A1). Dirección: adelante por defecto y atrás manteniendo «mirar atrás». Las acciones A2–A5 no existen sin pasajera.

**Reenganche.** Expulsada, la artillera vuelve al asiento a los 3 s si el buggy está a menos de 30 m; si no, en el siguiente checkpoint. La conductora no cambia.

**Autoridad (todas las acciones).** RPC del cliente de la artillera al servidor; el servidor comprueba: es la ocupante del asiento 1, el cooldown ha terminado, el ítem está en su inventario y el ángulo de apuntado está limitado. El servidor aplica el efecto y replica un estado mínimo del buggy (bits de acción, `LeanDir` en int8, 1–2 bytes). Con la opción E de [B §3] (autoridad del conductor), los efectos sobre el chasis viajan como impulso RPC `Client` al conductor.

### 3.2 Acciones

Controles: teclado y ratón / mando. La artillera tiene su propio `IMC` (prioridad mayor, como el del buggy, plan [B §1]).

| # | Acción | Controles | Efecto (1.ª pasada) | Cooldown / coste | Autoridad y réplica | Estado | Horas (est.) |
|---|---|---|---|---|---|---|---|
| A1 | **Lanzar ítem** | Clic izq. / RT. La cámara apunta; ángulo relativo al buggy < 90° = adelante, > 90° = atrás | Usa el ítem del slot equipado: `Coconut` (turbo del propio buggy), `SandMine` (cae detrás), `HomingCrab` (persigue al de delante), `Sunscreen` (escudo de 4 s). El proyectil hereda la velocidad del buggy | 0,4 s entre lanzamientos; 2 slots (`TN_InventoryComponent`) | Servidor crea el proyectil y aplica el efecto; el cliente solo anima | **MVP** | 8–12 |
| A2 | **Contrapeso** | Mantener Q / LB. Inclinación con A/D o stick | Se cuelga del lado interior de la curva: centro de masas +35 cm en Y hacia ese lado y algo más bajo (−10 cm en Z) sobre `CenterOfMassOverride` (`HYBuggy.cpp:212`). Objetivo: subir en 0,25 g el límite de vuelco lateral. Mientras se apoya no lanza | Recurso «equilibrio» 100: gasta 40/s (2,5 s), recarga 25/s tras 1 s sin usarlo (4 s completo). Reutiliza `TN_StaminaComponent` | Servidor cambia el centro de masas; `LeanDir` replicado a 15 Hz | **MVP** | 10–14 |
| A3 | **Caracola** | H / Y (la conductora conserva la bocina) | Cono de 60° y 30 m hacia delante: la artillera rival y su conductor se sobresaltan 1,5 s. La rival no puede lanzar ni apoyarse; sin efecto sobre la física del buggy. Es un ruido, no un golpe | 10 s | Servidor consulta los buggies del cono; estado `Scared` (bool) replicado; sonido sintetizado en C++ (sin BIE) | Después | 5–7 |
| A4 | **Palanca** (volteo) | Machacar E / X con el buggy volcado | Cada pulsación resta 0,2 s al tiempo de enderezado (2 s de base, `HYBuggyData.h:77`); mínimo 0,8 s. Sin artillera, el enderezado tarda 4 s ([B §7 #3]) | Sin cooldown; solo si `up.z` < 0,3 y velocidad < 3 km/h | El servidor cuenta las pulsaciones (máx. 8 en 2 s) y llama a `ServerSelfRight` | Después | 3–5 |
| A5 | **Abordaje** | Doble salto (Espacio ×2) apuntando a un buggy a ≤ 8 m lateral con velocidad relativa ≤ 30 km/h | Salta del buggy a la parte trasera del rival. Durante 3 s puede robarle un ítem (E) o pisarle el capó; luego doble salto de vuelta, o cae en caparazón | 20 s. Falla: noqueo (`MinKnockdownSpeed`) y reenganche del §3.1 | Servidor valida distancia y velocidad, hace el attach sobre el rival; el cliente solo predice el salto | Después | 20–28 |
| A6 | **Torpedo** (caparazón lanzada por delante) | — | La artillera sale rodando como una bola por delante del buggy y derriba a lo que toque | — | Depende de `ATN_ShellBody`/`StartBody` y de agarrar/lanzar (el agarre solo a quien está en caparazón, decisión del director 2026-09-18) | **Fuera**: no hay garantía de que la bola se reenganche sin trampas, choca con el rescate y el buggy queda sin artillera 3–10 s | 24–32 |

**MVP: A1 y A2** (18–26 h). Motivos: A1 da función a la biplaza y reutiliza el sistema de ítems (plan §3.3); A2 resuelve el vuelco en curva (la mayor queja de [B §7], «anti-vuelco»), da un juego propio a la artillera y no depende de otros jugadores. Ambas se prueban con `buggy_measure` y un solo buggy. Suman a los asientos de [B §6 #10] (12–16 h), que ya están en el plan.

Casos límite:

- Sin ítem, A1 no hace nada y suena un «clic».
- Con `Sunscreen` activo el buggy no recibe `SandMine`, `HomingCrab` ni atropello.
- Si el equilibrio se agota apoyada, suelta y no puede volver a pulsar 1 s.
- La caracola no afecta a los buggies que están dentro del túnel o del puente (evita empujar a un rival fuera de la pista por un susto).
- La desconexión de la artillera libera el asiento; la conductora sigue como si fuera sola.
- El enderezado de A4 lo ignora la cadena de la desconexión (el servidor lo restaura al pasar a 4 s).

## 4. Presupuesto de assets nuevos del Rally

El plan §3.6 ya contaba 1 asset imprescindible del Rally (`M_BuggyPaint`). Con el biplaza y E01B:

| Asset | Tipo | Imprescindible | Origen |
|---|---|---|---|
| `M_BuggyPaint` | Material | **Sí (1)** | Ya en el plan |
| Carrocería biplaza (`SM_BuggyBody_Biplaza`, dos asientos y barra antivuelco) | Malla | **Sí (2)** | Decisión 8 del director; variante de `buggy.py` con `validate.py` (≤ 3 000 triángulos) |
| Túnel y arco natural | Terreno | No | Campo de densidad (0 assets) |
| Puentes de Mokius | Actor | No | Asset existente del equipo de Mokius; solo se coloca |
| Arco de meta y de checkpoint | Malla | No | Arco de neumático de la meta ([D §2.1]) |
| Río del Ebro | Material | No | `M_GridWater` con cota propia |

| Acción | Assets nuevos | Detalle |
|---|---|---|
| A1 Lanzar ítem | 0 | Proyectiles y animación de lanzar existentes |
| A2 Contrapeso | 0 (deseable: pose de inclinarse) | Se inclina por transformación de hueso en código |
| A3 Caracola | 0 | SFX sintetizado (`TN_RaceItemSynth`) |
| A4 Palanca | 0 | Animación de machacar reutiliza el escape de agarre |
| A5 Abordaje | 0 (deseable: pose de aterrizar) | Reutiliza el doble salto |
| A6 Torpedo | — | Fuera |

**Total: 2 imprescindibles y 2–3 deseables** (pose de inclinarse, pose de aterrizar, decal de derrape ya en el plan). Todo lo demás se reutiliza o se genera en código.

## 5. Contradicciones con documentos existentes y decisiones que necesitan al director

| # | Documento | Contradicción | Resolución propuesta |
|---|---|---|---|
| 1 | Plan §3.3 (ítems) | «Carrera de 8–10 min» frente a 120 s | Copa de 3 carreras; los ítems se mantienen |
| 2 | [B §2] | Pasajeras en caparazón sin física frente a artillera activa | Sentada fuera del caparazón (§3.1); en el caparazón solo al ser expulsada |
| 3 | [B §5] | E01B a 1 m = 0,5–0,7 km y 324–576 trozos | 0,4 km por metro y ≈ 100 trozos con contenido, gracias a la banda de ±150 m |
| 4 | [C §3] | Landscape para mapas de más de 1 km | Descartado por decisión 5 del plan y del director; malla decimada |
| 5 | Plan §3.3 (boost) | El `Coconut` da un boost de 2 s, pero HellYeah tiene un boost recargable (`HYBuggyData.h:81-97`) | Se conserva solo el del `Coconut`; el recargable se elimina (pregunta 2) |
| 6 | [C §1] | 89 niveles Z (44,5 m) | 128 niveles para E01B (pregunta 4) |

## 6. Preguntas para el director

1. **Formato de sesión.** ¿Copa de 3 carreras de 2 min o carrera suelta? Cambia el peso de los ítems y el tamaño del catálogo de mapas.
2. **Boost.** ¿Se elimina el boost recargable de HellYeah (queda solo el `Coconut`)? Recomendado, para que la artillera tenga el poder del turbo.
3. **Puente de Mokius.** ¿Ancho útil del asset ≥ 12 m y colisión de cubierta apta para vehículos? Si no, hay que escalarlo (2–4 h).
4. **Rango Z del voxelizado.** Subir a 128 niveles (≈ +45 % de memoria de densidad en el generador). Alternativa: bajar la salida de los Pirineos de +44 a +36 m y recortar la roca sobre el túnel.
5. **Río del Ebro.** ¿Basta una lámina de agua local con muerte, o se quiere un vado transitable? Recomendado: lámina con muerte.
6. **Caracola.** ¿Aceptas que la acción con efecto sobre rivales sea un susto sin impacto físico? Es lo que hace que la broma no frustre.

## 7. Verificación de esta especificación

- `buggy_measure`: 0–60 km/h ≈ 3 s, punta ≈ 95 km/h y aceleración lateral en un círculo de 25 m (calibra §1.2).
- Piloto IA: recorre los 2 400 m en 107,7 s ± 8 %, 0 atascos y 0 vuelcos en 10 recorridos.
- Validador del corredor: los límites del §2.6, con las excepciones declaradas (rampa de 14°).
- A2: en un círculo de 25 m a 70 km/h, el buggy vuelca ≥ 50 % sin contrapeso y ≤ 10 % con él.
- Playtest de la copa: el ganador entre 105 y 135 s; el último, a menos de 60 s.

## Decisiones tomadas (2026-09-29)

Resueltas por delegación del director («confío en ti»):

1. **Formato:** copa de 3 carreras de unos 2 min cada una, con los mapas del catálogo (Docs/Catalogo-Mapas-2026-09-29.md); también se puede jugar una carrera suelta.
2. **Boost:** se elimina el boost recargable de HellYeah; el único acelerón es el ítem `Coconut`.
3. **Puente de Mokius:** se medirá el asset antes de usarlo en el Rally; si mide menos de 12 m de ancho o su cubierta no admite vehículos, se escala o se usa un puente natural en ese tramo.
4. **Voxelizado:** se sube el rango vertical a 128 niveles para cubrir la salida a +44 m y la roca sobre el túnel.
5. **Artillera:** va sentada fuera del caparazón, visible, con su propia animación; sustituye la tortuga solo visual de [B §2].
