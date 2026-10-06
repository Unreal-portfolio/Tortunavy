# Rally Tortuga: especificación completa

Fecha: 2026-09-29 · Rama: `macro-update` · Estado: borrador para revisión del director.
Consolida, sin sustituirlos: `Docs/Rally_E01B_y_Biplaza.md` (= [E]), `Docs/Archivo/Rally_Sistemas.md` (= [S]), `Docs/2026-09-29-Plan-Maestro-Modos-y-Mapas.md` §3.3 y §7 (= [P]), `Docs/Catalogo-Mapas-2026-09-29.md` con su decisión final «países enteros» (= [C]), `Docs/Analisis/2026-09-29/B_buggy_sync.md` (eliminado) (= [B]), `Docs/Modos-UI-FX-2026-09-29.md` (= [U]), `Art/Library/IA/INDEX.md` y el borrador `Art/Source/Vehicles/Buggy/`.
Notación: «1.ª pasada» = valor de ajuste para playtest; «est.» = estimación propia sin medir; estado **MVP / Después / Fuera**. Los conflictos entre documentos están en el Anexo A, no resueltos en silencio.

## Decisiones del director que rigen este documento

| # | Decisión | Efecto |
|---|---|---|
| D1 | Buggy de HellYeah **sin fichas** (se interpreta como sin `Cargo` ni fichas de carga; ver Anexo B, pregunta 1) | `ATN_Buggy` sin carga ni casos de carga |
| D2 | **Biplaza**: conductora y artillera detrás; sola, conduce y lanza | §3 |
| D3 | Skins **por tinte** | Un solo material, parámetros de color y patrón |
| D4 | **Copa de 3 carreras de ~2 min** | §1, §2 |
| D5 | **Sin boost recargable**: el único acelerón es el ítem `Coconut` | §3.2, §6 |
| D6 | Mapas = **países enteros miniaturizados** con relieve real (uno por idioma) más islas, con puentes (naturales y de Mokius), túneles y lazos/circuitos | §4 |
| D7 | No ir en dirección contraria; enderezar; derrape y boost con efectos | §5, §8 |
| D8 | Todo sincronizado, servidor con autoridad | §9 |
| D9 | **Minimizar assets nuevos** en todo | §6, §12 |

## 1. Fantasía, jugadores, duración, copa y puntos

**Fantasía.** Un país entero cabe en una mesa: montañas, costas y ríos reales a escala de maqueta, con una carretera de juguete tallada que lo cruza. Las tortugas lo atraviesan en buggy en dos minutos, por túneles, puentes de madera de Mokius y arcos de roca, y con una compañera detrás que dispara cocos, cangrejos y minas.

**Experiencia buscada.** Conductora: velocidad, trazadas, derrape y saltos. Artillera: leer la carrera, elegir el momento del ítem y apoyar el peso en las curvas. Sola: las dos cosas a la vez, con menos herramientas. Se enseña en la primera carrera con tres carteles de 4 s (frenar/derrapar, ítem, enderezar; §7.5) y una parrilla de práctica de 10 s sin puntos.

| Concepto | Valor | Motivo |
|---|---|---|
| Jugadoras | 1 a 8 (cada una en su buggy, o en parejas: hasta 4 biplazas). Mezcla libre; solo en lobby | [U] «Rally 1 a 8 (biplaza si hay pareja)» |
| Buggies por carrera | 1 a 8 (con 1 jugadora, contra bots del piloto IA: **Después**; MVP exige 2 buggies o modo contrarreloj) | Sin bots no hay carrera |
| Duración | Ganador **120 s** (aceptable 105 a 135 s). Cierre a `FinishGraceSeconds` = **30 s** tras el primero (Anexo A #1) | [E] §2.1, [S] §1 |
| Eje del trazado | **2 400 m** ± 5 % (a 20 m/s de media real) | [E] §2.1 |
| Copa | **3 carreras** en 3 mapas distintos del catálogo (elegidos por el anfitrión o al azar); también carrera suelta. Entre carreras: pantalla de resultados 12 s + carga | D4, [E] «Decisiones» 1 |
| Puntos por carrera (por buggy; las dos tortugas de una biplaza puntúan lo mismo) | 1.º 10 · 2.º 8 · 3.º 6 · 4.º 5 · 5.º 4 · 6.º 3 · 7.º 2 · 8.º 1 · no llega al cierre 0 | [S] §1 |
| Desempate de copa | Mejor puesto en la última carrera; luego menor suma de tiempos | Determinista |
| Duración de sesión | 3 × 2 min + 3 × 30 s de pantallas ≈ **8 min** | Sesión corta |
| Ítems por buggy y carrera | 5 a 6 (7 filas de cajas; §6) | [E] §2.1 |

Solo/biplaza, equilibrio: el buggy solo tiene los mismos 2 huecos de ítem y **anti-vuelco pasivo +0,10 g** (mitad del efecto de Contrapeso) para compensar que conduce y dispara [1.ª pasada]. La biplaza gana Contrapeso (A2) y lanza sin soltar el volante. Si la telemetría muestra >60 % de victorias de un tipo en 10 copas, se ajusta el pasivo.

## 2. Flujo

1. **Lobby** (`ATN_ProcModeSelector`, posición 3 de 5 en [U]): anfitrión elige Copa o Carrera suelta, y los mapas. Cada jugadora elige plaza: Sola / Conductora / Artillera; una Conductora sin Artillera libre va Sola. Un buggy con dos plazas exige que las dos estén ocupadas al empezar (si una desconecta antes, la otra pasa a Sola).
2. **Carga** del mapa (volumétrico preparado, §10); pantalla con el contorno del país.
3. **Presentación** 4 s (`UTN_RaceRoundIntroWidget`): nombre del país, ruta, número de carrera de la copa.
4. **Parrilla** 2 × 4, 8 m entre filas. Carrera 1 al azar; carreras 2 y 3 por **puntos de copa ascendentes** (el último de la copa sale delante; Anexo A #2).
5. **Semáforo** de 3 s (3 luces, la última en verde = salida). Servidor fija `StartServerTime` replicado; el buggy está bloqueado (freno) hasta entonces.
6. **Carrera**: checkpoints, ítems, enderezado, reaparición (§5).
7. **Cierre**: el primero cruza la meta → cuenta atrás de `FinishGraceSeconds` (30 s) visible (`UTN_RaceFinishCountdownWidget`). Los que no llegan puntúan 0 y se ordenan por progreso.
8. **Resultados** de la carrera (`UTN_RaceTallyWidget`) → siguiente carrera → **campeona** (`UTN_RaceChampionWidget` + `ATN_RacePodiumStage` con el buggy de la ganadora, variación de malla).
9. Vuelta al lobby.

Casos límite: si el anfitrión desconecta se aborta la copa; si una jugadora desconecta, su buggy queda parado en la parrilla (carrera 1) o se retira (en marcha) y su plaza de biplaza se libera (la compañera sigue Sola); una jugadora que entra tarde espera a la siguiente carrera de la copa (late-join: **Fuera** del MVP).

## 3. Buggy

### 3.1 Base y cambios respecto a HellYeah

`HYBuggy` pasa a `ATN_Buggy` (`Vehicles/`, plugin `ChaosVehiclesPlugin`) [P] §3.3, [B] §1. Cambios: **sin `Cargo`** (D1), rutas a `UPROPERTY(EditDefaultsOnly)`, **sin boost recargable** (D5: se eliminan `BoostDuration/Recharge/TorqueMultiplier` como mecánica propia; el `Coconut` reutiliza los valores). Física **síncrona con subpasos** (`MaxSubstepDeltaTime = 0,016667`); asíncrona solo si `buggy_measure` lo exige.

### 3.2 Parámetros (valores de HellYeah, [E] §1.1; masa por medir)

| Parámetro | Valor | Nota |
|---|---|---|
| Par máximo / régimen máximo | 850 N·m / 3 400 rpm | punta ≈ 110 km/h; medida 91 a 99 en recta |
| Relación final | 2,0 | 0 a 60 km/h ≈ 3,0 s |
| Fricción delantera / trasera / freno de mano | 3,0 / 2,6 / 1,4 | el freno de mano provoca el derrape |
| Rueda | radio 51 cm, ancho 35 cm, rigidez de curva 750 | |
| Suspensión | recorrido 25 cm, amortiguación 0,25, muelle 100, precarga 100 | |
| Freno | 6 000 N·m por rueda | |
| Dirección | 40° × `AngleRatio` 0,7 = 28° efectivos | radio mínimo geométrico 5,7 m; manda el agarre |
| Batalla / centro de masas | 3,03 m / (0, 0, 40 cm) | Contrapeso lo desplaza (A2) |
| Masa | Sale del PhysicsAsset; medir con `Chassis->GetMass()` | pendiente |
| **Coconut (turbo)** | **2,0 s**, par ×1,8, empuje +5 m/s² | sin recarga; solo con el ítem |
| Atropello | ≥ 600 cm/s de velocidad relativa | solo a tortugas a pie/en caparazón |
| Enderezado | botón tras 2 s volcado; automático a los 4 s (§5.6) | |

Velocidades de diseño (est. [E] §1.2): recta 95 a 105 km/h; radio 25 m 55 a 60 km/h; radio 50 m 85 km/h; subida 6° 85 km/h; media de carrera 72 km/h (20 m/s). Aceleración lateral de diseño 1,2 g.

**Derrape.** Freno de mano (Espacio) baja la fricción trasera a 1,4; el buggy desliza sin ganar turbo (no hay boost recargable). El derrape solo da control y efectos (§8). Un derrape prolongado > 2 s con velocidad < 30 km/h se corta solo (evita el peonza infinito).

**Controles (teclado y ratón / mando).** W/S o RT/LT acelerar y frenar/marcha atrás; A/D o stick dirección; Espacio o B freno de mano/derrape; R o Y enderezar; C o clic del stick mirar atrás; clic izq. o RB usar ítem (conductora sola; artillera en biplaza); bocina H (sin efecto de juego).

### 3.3 Biplaza

- **Geometría** (borrador `SM_TN_BuggyBody`, 1 640 triángulos, 422,3 × 244,7 × 139,5 cm; `SM_TN_BuggyTire`, 384): `Seat_Driver` (22, 0, 92,4 cm), `Seat_Gunner` (−80, 0, 127,4 cm), `Muzzle_Gunner` (−15,4, 0, 169,4 cm).
- **Asiento.** Conductora: el PC posee el buggy (pawn = buggy). Artillera: **fuera del caparazón**, visible, attach al socket con el patrón de `TN_CarryComponent.cpp:457-466`, `DisableMovement`, cámara propia libre alrededor del buggy; Ctrl (caparazón) bloqueado sentada ([E] §3.1, decisión 5; contradice [B] §2, Anexo A #3).
- **Sola.** Conductora conduce y usa A1; mirar atrás (C) mantenido + clic lanza hacia atrás. Sin A2.
- **Expulsión y reenganche** (choque fuerte, volcado > 4 s): **Después**. En el MVP la artillera va fija en el asiento (menos código y sin rama de rescate); volcado sin remedio = reaparición del buggy entero (§5.7).
- **Desconexión de la artillera**: libera el asiento; la conductora sigue como Sola. Desconexión de la conductora: el buggy queda sin control y la artillera pasa a conducir (`PawnLeavingGame` no destruye el buggy) [B] §2.
- **Skins por tinte** (D3): `FTN_BuggyLook` en el PlayerState de la conductora, replicado; campos `PaintId` (color `Color`/`Color2`, `Shine`, `Pattern` = `ETNShellPattern`). Sin mallas alternativas en el MVP. Llantas y decoración: **Después** (socket `SOCKET_Deco_*`). `M_BuggyPaint` es una instancia con los parámetros de `M_TurtleBody`; en biplaza manda el tinte de la conductora.

### 3.4 Acciones de la artillera

Autoridad: RPC del cliente de la artillera al servidor; el servidor valida (ocupa el asiento 1, cooldown, ítem en el inventario, ángulo dentro de límite: guiñada libre, cabeceo −10° a +45°) y replica un estado mínimo (bits de acción, `LeanDir` int8: 1 a 2 B). Efectos sobre el chasis son impulso autoritativo del servidor.

| # | Acción | Control | Efecto (1.ª pasada) | Cooldown / coste | Estado | h |
|---|---|---|---|---|---|---|
| A1 | **Lanzar ítem** | Clic izq. / RT; el ángulo de cámara relativo al buggy <90° = adelante, >90° = atrás | Usa el ítem equipado. Proyectil a velocidad del buggy + 25 m/s en la dirección de apuntado. `Coconut` no es proyectil: el turbo va al propio buggy. `SandMine` cae detrás. `HomingCrab` persigue al de delante (no se puede lanzar siendo 1.º: «clic») | 0,4 s entre lanzamientos; 2 huecos | **MVP** | 8 a 12 |
| A2 | **Contrapeso** | Mantener Q / LB; inclinación con A/D | Centro de masas +35 cm en Y hacia el lado interior y −10 cm en Z (`CenterOfMassOverride`, `HYBuggy.cpp:212`). Sube 0,25 g el límite de vuelco lateral. Mientras se apoya no lanza | Recurso «equilibrio» 100: gasta 40/s (2,5 s); recarga 25/s tras 1 s sin usarlo (4 s completo). Agotada, suelta y no puede volver a pulsar 1 s | **MVP** | 10 a 14 |
| A3 | **Caracola** (susto) | H / Y | Cono 60° y 30 m: la artillera rival no puede lanzar ni apoyarse 1,5 s. Sin efecto sobre la física. No afecta a buggies en túnel o puente | 10 s | Después | 5 a 7 |
| A4 | **Palanca** | Machacar E / X con el buggy volcado | Cada pulsación resta 0,2 s al enderezado (mínimo 0,8 s); máx. 8 pulsaciones válidas en 2 s | Sin cooldown; solo si `up.z` < 0,3 y v < 3 km/h | Después | 3 a 5 |
| A5 | **Abordaje** | Doble salto apuntando a buggy a ≤ 8 m y velocidad relativa ≤ 30 km/h | Salta a la trasera del rival; roba un ítem o pisa el capó 3 s | 20 s | Después | 20 a 28 |
| A6 | Torpedo (artillera-bola) | — | — | — | **Fuera** ([E] §3.2) | — |

Casos límite: sin ítem A1 suena un «clic» y no gasta cooldown. Con `Sunscreen` activo el buggy ignora `SandMine`, `HomingCrab` y atropello. A2 no se puede usar en el aire ni volcado. La cámara de la artillera usa `bDoCollisionTest` para no atravesar túneles.

## 4. Mapas y circuitos

### 4.1 Reglas de mapa (medibles)

Cada mapa del Rally es un **país o isla entero miniaturizado** con MDE real y contorno propio (costa y frontera como borde) [C] «Decisión del director». Una carretera tallada lo cruza.

| Regla | Valor | Cómo se mide |
|---|---|---|
| Escala | 1 m de juego = X km reales, con **lado mayor ≤ 2,4 km** (ventana con 250 m de margen) | Manifest del generador |
| Eje | 2 400 m ± 5 % de longitud; sinuosidad (eje / suma de rectas entre hitos) entre 1,05 y 1,5 | Validador |
| Pendiente | ≤ 12° sostenida (diseño ≤ 8°); ≤ 20° en tramo corto (una rampa por mapa como máximo, 14° en 25 m) | `buggy_measure` + validador |
| Radio mínimo | ≥ 25 m | Validador |
| Ancho | ≥ 12 m en calzada, túneles y puentes; 14 m de diseño | Validador |
| Peralte | ≤ 5° (8° en curva) | Validador |
| Escalón entre muestras de 1 m | ≤ 0,4 m en el eje | Validador |
| Checkpoints | Cada 150 a 250 m; no dentro de túneles, sobre puentes ni en la mitad de una bifurcación | Validador |
| Puentes | ≥ 1 por travesía; ancho ≥ 12 m; sin oscilación | Validador |
| Túneles | ≥ 1 por travesía; ≤ 15 % del eje en total; 14 × 8 m; fachada de altura válida en la entrada ([P] §2.2) | Validador |
| Rango Z | ≤ 64 m (128 niveles de 0,5 m) | Generador |
| Triángulos / StaticMesh | ≤ 1,2 M / ≤ 21 MB | Bake |
| Reproducibilidad | Misma semilla, mismo hash del bake en 3 ejecuciones | Test |
| Fuera de la calzada | Muro de roca en fronteras terrestres (sin línea ni bandera), agua de muerte en la costa | Generador |

**Relieve.** Se mantiene la exageración calibrada de E01 (3 a 6×), sin pasarse: `E` se ajusta por país para que el punto más alto del eje quede entre +25 y +50 m sobre el mar y la pendiente p50 fuera del corredor sea ≤ 12° (la p90 puede pasar de 45°: son paredes). Donde el MDE da poco desnivel (países grandes: a 2,4 km/m, 5 000 m reales son 2 m de juego), el eje lleva **cotas de autor** derivadas del perfil real comprimido y suavizadas a ≤ 8°. El MDE se elige por tamaño: GLO-90 hasta 1 500 km de lado, SRTM 1″ para islas y ETOPO 60″ (1,85 km) para ru, zh-Hans y pt-BR; siempre paso real / escala ≪ 6 m de juego.

**Elementos de trazado (definiciones).**
- **Puente de Mokius**: asset existente colocado con `ATN_BridgeSpan` (marcadores `BridgeStart/BridgeEnd`, aún sin crear). Primero se mide: si <12 m de ancho o su cubierta no admite vehículos, se escala o se sustituye por natural (E01B «Decisiones» 3).
- **Puente natural**: arco de roca tallado en el campo de densidad (resta de cilindro en `terrain_vol/density.py`).
- **Túnel**: resta de cilindro recto de 14 × 8 m, `safe_low/safe_high` y `WalkableZAt` (riesgo R4). Sin checkpoint dentro.
- **Lazo**: curva de ≥ 270° cuya calzada vuelve a cruzar la propia por **paso a distinto nivel** (puente sobre calzada o túnel bajo ella). Separación vertical ≥ 10 m (8 m de gálibo + 2 m de losa). Sin caja de ítems ni checkpoint en el cruce. Radio ≥ 25 m.
- **Circuito**: eje cerrado con `Laps` = 1 a 3; perímetro × vueltas = 2 400 m ± 5 %. Meta = salida. Sin puertas ocultas (§5.2).
- **Travesía**: punto a punto, `Laps` = 1.

### 4.2 Lista de mapas (uno por idioma más islas)

Sustituye la columna «Región» de [C] §1 (las regiones pasan a ser tramos emblemáticos). Escala y tamaño: est. hasta que el generador proyecte el MDE. «Recta» = suma de rectas entre hitos.

| id | Idioma | País (contorno) | Escala km/m | Ventana (m) | Ruta (hitos) | Recta / sinuosidad | Emblemas del eje | Estado |
|---|---|---|---|---|---|---|---|---|
| L01 | es-ES | España peninsular (E01B) | 0,42 | 2 380 × 2 024 | Somport → Zaragoza → Guadalajara → Toledo → Despeñaperros → Málaga | 1 998 / 1,20 | Túnel de 120 m, puente de Mokius sobre el Ebro, puente natural del Tajo, viaducto, salto | **MVP** (existe el diseño, §4.3) |
| L02 | en | Reino Unido (Gran Bretaña e Irlanda del Norte) | 0,42 | 1 430 × 2 380 | John o' Groats → Glen Coe → Lake District → Cotswolds → Land's End | 2 260 / 1,06 | Túnel bajo el Ben Nevis, colgante y natural, lazo en Glen Coe | Después |
| L03 | fr | Francia metropolitana | 0,42 | 2 260 × 2 260 | Dunkerque → París → Verdon → Aix → Cassis | 2 070 / 1,16 | Natural del cañón, túnel bajo la Sainte-Victoire, trampolín final | Después |
| L04 | de | Alemania | 0,36 | 1 780 × 2 360 | Flensburgo → Rin medio (Loreley) → Múnich → Garmisch | 2 220 / 1,08 | Colgante sobre el Rin, horquillas, túnel | Después |
| L05 | it | Italia (con Sicilia) | 0,50 | 1 600 × 2 400 | Dolomitas → Venecia (laguna) → Florencia → Roma → Nápoles → Mesina → Etna | 2 100 / 1,14 | Puente de Mokius largo en el estrecho de Mesina, puentes en la laguna | Después |
| L06 | pt-BR | Brasil | 1,9 | 2 260 × 2 260 | Porto Alegre → Rio → Salvador → Fortaleza | 1 790 / 1,34 | Teleférico colgante, natural, túnel | Después |
| L07 | ru | Rusia (compactada: X 4,0 / Y 2,5 km/m) | 4,0 / 2,5 | 2 250 × 1 400 | Moscú → Kazán → Urales → Novosibirsk → Baikal → Vladivostok | 1 600 / 1,50 | Túnel de los Urales, puente sobre el Volga | Después |
| L08 | pl | Polonia | 0,30 | 2 300 × 2 170 | Hel → Gdansk → Varsovia → Cracovia → Zakopane | 2 030 / 1,18 | Salida por la carretera entre dos mares, dunas como saltos | Después |
| L09 | tr | Turquía | 0,70 | 2 290 × 860 | Edirne → Estambul → Ankara → Capadocia → Kars | 2 000 / 1,20 | Puente de Mokius sobre el Bósforo, red de túneles de Capadocia | Después |
| L10 | ja | Japón (cuatro islas mayores) | 0,70 | 2 040 × 2 290 | Sendai → Tokio → Fuji → Kioto → Hiroshima → Fukuoka | 2 000 / 1,20 | Espiral (lazo) del Fuji, puentes entre islas, túnel de lava | **MVP** (piloto de países grandes) |
| L11 | ko | Corea del Sur (con Jeju) | 0,27 | 1 190 × 2 300 | Paju → Seúl → Gangneung → Daegu → Busan | 2 000 / 1,20 | Túneles encadenados, natural | Después |
| L12 | zh-Hans | China continental (solo relieve y costa, sin fronteras internas ni rótulos políticos) | 2,4 | 2 330 × 1 630 | Harbin → Pekín → Xi'an → Chengdú → Guilin → Shenzhen | 2 040 / 1,18 | Karst de Guilin, natural, túnel | Después |
| L13 | zh-Hant | Isla de Taiwán | 0,17 | 850 × 2 320 | Keelung → Taipei → Taroko → Alishan → Kaohsiung | 2 060 / 1,17 | Cuatro túneles encadenados en Taroko, colgante de 120 m, lazo | Después |
| I03-R | (isla) | **Tortuga Magna** (ficticia, circuito cerrado) | 0,02 | 1 400 × 1 100 | Lazo elíptico semiejes 460 × 280 m con **lazo de la boca** (la calzada cruza sobre el túnel de la boca) | Perímetro 2 390 m, `Laps` = 1 | Túnel de la boca, lazo, escamas ±3 m | **MVP** (circuito + lazo) |
| I04 | (isla) | Volcán Hueco (ficticia, caldera) | 0,02 | 1 300 × 1 300 | Borde de la caldera | 2 400 m, `Laps` = 1 | Natural sobre la grieta, túnel al interior | Fuera del MVP (Cut?) |
| I06 | (isla) | Islas Feroe | 0,012 | 1 300 × 2 700 | Sørvágur → Vestmanna → Tórshavn | 2 000 / 1,20 | Túnel submarino de 250 m (supera el 15 %: recortar a 200 m), puentes | Fuera del MVP (Cut?) |

MVP de la copa: L01 (travesía) + L10 (travesía larga con islas) + I03-R (circuito con lazo) = 3 mapas. Ningún país incluye frontera disputada ni simbología política; nombres solo geográficos.

### 4.3 E01B (España): contrato de trazado

La secuencia de tramos, cotas, longitudes y elementos de [E] §2.4 y §2.5 **se conserva** como contrato por arco (T1 Pirineos 0 a 450, T2 Ebro/meseta 450 a 1 070, T3 Tajo 1 070 a 1 410 con bifurcación A/B, T4 Sierra Morena 1 410 a 1 850, T5 costa 1 850 a 2 400). Cambia solo la escala del mapa (0,4 → 0,42 km/m por la ventana de país entero; sinuosidad media 1,14 → 1,20; Anexo A #4). Criterios de aceptación por arco, no por distancia entre hitos.

| Tramo | Arco (m) | Elementos | Estado |
|---|---|---|---|
| T1 | 0 a 450 | Salida (60 m recta, parrilla 2 × 4), herradura 1 (r25, −8°), túnel 210 a 330, herradura 2 | MVP |
| T2 | 450 a 1 070 | Puente de Mokius sobre el Ebro 520 a 620 (cota +10,6), subida 6°, rampa de salto 830 a 855 (+14°, aterrizaje 60 m) | Puente MVP; rampa Después |
| T3 | 1 070 a 1 410 | Bifurcación 1 210 a 1 370: A puente natural (1,3 s más rápida, caída si te sales), B cañón (caja doble) | Después |
| T4 | 1 410 a 1 850 | Subida 5°, paso de Despeñaperros (12 m) | MVP |
| T5 | 1 850 a 2 400 | Viaducto de Mokius 1 850 a 1 930, dos herraduras, recta costera, playa 20 m, meta 2 400 | Viaducto Después |

## 5. Sistemas de carrera

Principio [S]: el **servidor** decide posición, vuelta, dirección, reaparición y penalizaciones; los clientes solo muestran.

### 5.1 Trazado

`ATN_RallyTrack`: spline del eje (`checkpoints_uu` + eje de la calzada) escrita por el generador; datos idénticos en todas las máquinas. Cada punto guarda arco `s`, tangente, ancho y nivel (`Level` 0/1 para lazos y puentes/túneles superpuestos).

### 5.2 Checkpoints y vueltas

| Regla | Valor |
|---|---|
| Puertas | Volumen de caja (ancho = calzada + arcén, 24 m; alto 10 m; profundidad 4 m), en orden |
| Valida una puerta | Cruzar el volumen con la puerta anterior ya validada y la dirección no contraria |
| Saltarse una puerta | No cuenta; la siguiente no valida hasta pasar la omitida (o reaparecer en la última válida) |
| Vuelta | Cruzar la meta con todas las puertas de la vuelta; `CurrentLap` y `NextCheckpoint` replicados en el PlayerState |
| Circuito | La meta es también la puerta 0; en el primer paso (salida) no cuenta vuelta |
| Bifurcación | Ambos ramales conducen a la misma puerta; ningún ramal tiene puerta interior |
| Lazo y niveles | La proyección al eje usa **ventana de arco**: solo busca en `[s_prev − 20 m, s_prev + 120 m]`, con el `Level` coherente; así un buggy que pasa bajo otro tramo no salta de arco |

### 5.3 Posiciones

Orden por (vuelta, puerta, arco de progreso `s` dentro de la puerta) calculado en el servidor a **5 Hz**; array ordenado replicado en el GameState (8 × 1 B). Empate exacto: menor `PlayerId`. Un buggy con dos tortugas cuenta como uno. Tras cruzar la meta el puesto queda fijado.

### 5.4 Dirección contraria

`TNRally::IsWrongWay` (pura, testeada): `dot(velocidad, tangente)` < −0,5 durante 1,5 s a > 20 km/h → estado `WrongWay` replicado; el cliente muestra el aviso y suena un pitido. Si dura **4 s** el servidor **gira el buggy** hacia la tangente en el sitio (mismo mecanismo que el enderezado, sin penalización, 1 s de fantasma) — así no se puede ir de vuelta por la carretera. No aplica dentro de un ramal de bifurcación que pase por debajo de otro nivel (se evalúa con la tangente del nivel activo). Marcha atrás < 20 km/h no dispara.

### 5.5 Atajos ilegales

Si entre dos puertas consecutivas el buggy recorre menos del **60 %** de la longitud de spline **del ramal más corto declarado**, la puerta no cuenta. Al reaparecer vuelve a la última puerta válida.

### 5.6 Enderezar el coche

Volcado (`up.z` < 0,3) o parado > 2 s con las ruedas fuera del suelo: botón de enderezar (R); **automático a los 4 s** ([B] §7). El servidor aplica par y elevación de 1 m con barrido, `ForceNetUpdate`, mantiene velocidad ≈ 0. Con A4 (Después) desde 0,8 s. Sin artillera: 4 s.

### 5.7 Reaparición

Disparadores: fuera de pista (distancia al eje > 40 m sobre suelo no transitable), agua/mar (`kill_boxes_uu`), bajo el terreno, o 8 s atascado con progreso < 5 m. Resultado: vuelve a la **última puerta válida** (en la bifurcación, a la puerta anterior), orientada a la spline, con **espera de 3 s** inmóvil (penalización natural) y **2 s de fantasma** sin colisión con buggies. La artillera reaparece con el buggy.

### 5.8 Salida y fin

Parrilla y semáforo en §2. **Salida anticipada** (acelerar antes del verde): motor cortado 1 s. Fin: primero cruza → `FinishGraceSeconds` = 30 s [1.ª pasada] → resultados y puntos (§1).

### 5.9 Rebufo, ítems y ayuda al último

Rebufo +15 % de par a < 30 m detrás de otro buggy y ángulo < 20° respecto a su trasera. Ítems ponderados por puesto (§6.2). Sin cajas en los últimos 355 m de una travesía (final por conducción).

### 5.10 Suelo

Fricción por material: calzada 1,0, arena 0,8, hierba 0,7, barro 0,55, agua somera 0,5 (frena y salpica) [1.ª pasada]; la tabla reutiliza la de tipos de suelo del terreno volumétrico (canal de material del bake).

## 6. Objetos e ítems

Regla D9: **0 o 1 asset nuevo por objeto**; primero existente, luego reutilizado, luego borrador IA, y solo al final nuevo. Clases: EXISTENTE (ya en el proyecto o en el equipo), REUTILIZADO (existente con otro uso, tinte o código), BORRADOR IA (en `Art/`, sin importar), NUEVO.

### 6.1 Tabla completa (26 objetos)

| # | Objeto | Uso en el Rally | Clase | Origen concreto | Nuevo |
|---|---|---|---|---|---|
| O01 | Carrocería biplaza | Cuerpo del buggy con `Seat_Driver`, `Seat_Gunner`, `Muzzle_Gunner` | BORRADOR IA | `Art/Source/Vehicles/Buggy/` → `SM_TN_BuggyBody` (1 640 tri, ≤ 3 000 por `validate_buggy.py`); pasa a final por el equipo | 1 (kit) |
| O02 | Neumático | 4 ruedas | BORRADOR IA | `SM_TN_BuggyTire` (384 tri, mismo kit que O01) | 0 (en el kit) |
| O03 | Pintura del buggy | Tinte y patrón (D3) | NUEVO | `M_BuggyPaint`: **instancia** de `M_TurtleBody` con `ETNShellPattern`; sin textura nueva | 1 |
| O04 | Tortuga conductora y artillera | Pasajeras visibles | EXISTENTE | `SKM` de tortuga + `UTN_CosmeticLook`; pose de caparazón/lanzar existentes | 0 |
| O05 | Caja de ítems | 4 por fila, 7 filas | EXISTENTE | `ATN_RaceItemBox` (`TN_RaceItemBox.h`) con máscara por buggy | 0 |
| O06 | `Coconut` | Turbo 2 s del propio buggy | EXISTENTE | `TN_RaceItems`, `BoostMath` | 0 |
| O07 | `SandMine` | Mina detrás | EXISTENTE | `TN_RaceMine` | 0 |
| O08 | `HomingCrab` | Cangrejo que persigue | EXISTENTE | `TN_RaceHomingCrab` | 0 |
| O09 | `Sunscreen` | Escudo 4 s | EXISTENTE | `TN_RaceItems` | 0 |
| O10 | Proyectil lanzado | Mina y cangrejo en el aire | REUTILIZADO | Mallas de O07 y O08 | 0 |
| O11 | Arco de salida, checkpoint y meta | Una sola pieza con tres usos (tinte distinto) | REUTILIZADO | Arco de neumático de la meta de la Carrera ([P] §3.6, prioridad 0; a verificar en `TN_BeachRaceGenerator_Scenery`) | 0 |
| O12 | Puente de Mokius | Vanos con anclajes | EXISTENTE | Asset del equipo de Mokius + actor `ATN_BridgeSpan` (código) | 0 |
| O13 | Puente natural | Arco de roca | REUTILIZADO | Resta de cilindro en `terrain_vol/density.py` | 0 |
| O14 | Túnel | Recto 14 × 8 m | REUTILIZADO | Resta de cilindro en el campo de densidad | 0 |
| O15 | Rampa de salto | 14° en 25 m | REUTILIZADO | Geometría del terreno volumétrico | 0 |
| O16 | Trampolín / catapulta de vehículo | Salto de catálogo [C] | REUTILIZADO | Mallas de `TN_BeachTrampoline` y catapulta; hoy usan `LaunchCharacter`, no valen para el chasis: **Después**, con un impulso al chasis en código | 0 |
| O17 | Río, mar y agua de muerte | Cauce, costa | EXISTENTE | `M_GridWater` con cota propia; `kill_boxes_uu` | 0 |
| O18 | Vegetación y decoración de ruta | Relleno visual | EXISTENTE | Vegetación de playa y `TN_ProcMapFlora` (tinte por país) | 0 |
| O19 | Muro de roca de frontera | Borde terrestre | REUTILIZADO | Campo de densidad | 0 |
| O20 | Volumen de puerta | Checkpoint invisible | REUTILIZADO | `UBoxComponent` en `ATN_RallyTrack` (sin malla) | 0 |
| O21 | Semáforo de salida | 3 luces + verde | REUTILIZADO | Widget en `UTN_RaceRoundIntroWidget` (sin malla) | 0 |
| O22 | Iconos de ítem del HUD | Panel de artillera | EXISTENTE | Iconos de `TN_RaceItemArt` (a verificar) | 0 |
| O23 | Podio con buggy | Fin de copa | REUTILIZADO | `ATN_RacePodiumStage` con O01 en lugar de la tortuga | 0 |
| O24 | Caracola de la artillera (A3) | Susto | REUTILIZADO | Solo SFX sintetizado (`TN_RaceItemSynth`); sin objeto | 0 |
| O25 | Contorno del país en carga y minimapa | Vista previa | REUTILIZADO | Render del generador, spline de `ATN_RallyTrack` | 0 |
| O26 | Marcas de derrape | Decal | NUEVO (deseable, **Después**) | Sin ellas: polvo teñido por material | 0 (MVP) |

Recuento: EXISTENTE 10, REUTILIZADO 13, BORRADOR IA 2 (un solo kit), NUEVO 2 (O03 imprescindible, O26 deseable). Los «borradores IA de caja, caracola, rampa, pórtico y checkpoint» que cita [S] §4 **no figuran en `Art/Library/IA/INDEX.md`** (solo hay 6 assets de Todos contra Todos): este diseño no depende de ellos (Anexo A #5).

### 6.2 Ítems

Cuatro ítems ([P] §3.3). Fuera: coco dorado, gaviota, pelícano, disco, silbato. Los usa la artillera; sola, la conductora. 2 huecos por buggy; el ítem pertenece al buggy y se replica al dueño(s).

| Ítem | Efecto sobre el buggy [1.ª pasada] | Autoridad y réplica |
|---|---|---|
| `Coconut` | Turbo de **2 s**, par ×1,8, +5 m/s²; no se acumula (si ya hay turbo, se guarda) | Fuerza en el servidor; `bBoosting` replicado con `ForceNetUpdate`; el dueño solo predice lo visual |
| `SandMine` | Suelta detrás; al pisarla: impulso vertical, agarre al 50 % durante 1 s, sin vuelco forzado; vive 15 s o hasta 1 golpe | Solo servidor (`AddImpulse`); sin empujón local ([B] §4) |
| `HomingCrab` | Persigue al buggy inmediatamente por delante a 130 % de su velocidad; al impactar, velocidad ×0,5 durante 1,5 s; vive 8 s | Movimiento y frenazo en el servidor |
| `Sunscreen` | Escudo de 4 s frente a los otros tres y al atropello | Estado replicado; el servidor filtra impactos |

**Reparto por puesto** (`RollLoot`, `TN_RaceItems.h:153`; pesos %, 1.ª pasada):

| Puesto | Coconut | SandMine | HomingCrab | Sunscreen |
|---|---|---|---|---|
| 1.º | 15 | 40 | 0 (no hay nadie delante) | 45 |
| 2.º a 5.º | 25 | 25 | 25 | 25 |
| 6.º a 8.º | 40 | 20 | 35 | 5 |

**Cajas.** 7 filas de 4 cajas (separación 3 m, 9 m de ancho), repartidas entre el 15 % y el 85 % del eje, ≥ 200 m entre filas; ninguna en puentes, túneles, saltos ni cruce de lazo; +1 caja «premio» en el ramal arriesgado si hay bifurcación. Cada caja tiene una **máscara por buggy** (1 B): recogida por un buggy, sigue disponible para los demás y reaparece para él a los 6 s. Recoge la conductora (contacto del chasis) y el ítem va al inventario del buggy. E01B fija las filas en arcos 445, 700, 960, 1 150, 1 440, 1 720 y 2 045 más el premio en 1 290 ([E] §2.5).

## 7. Interfaz

Reutiliza [U] §2.2 y [S] §3. Nuevo en código: velocímetro, contador de checkpoint, panel de artillera, aviso de dirección contraria.

### 7.1 HUD de carrera (todas)

| Elemento | Contenido | Actualización | Base |
|---|---|---|---|
| Posición | «2.º / 8» | A cada cambio (5 Hz) | Nuevo, texto C++ |
| Vuelta | «Vuelta 2/3» (oculto si `Laps` = 1) | Al cruzar meta | Nuevo |
| Velocímetro | km/h, entero, esquina inferior derecha | 10 Hz local | Nuevo, texto C++ |
| Checkpoint | «Arco 3/13» + distancia al siguiente (m) | A cada puerta | Nuevo |
| Reloj | Tiempo de carrera y de vuelta; mejor vuelta si `Laps` > 1 | 10 Hz | `UTN_RunHUDWidget` |
| Pista lineal | Barra con los buggies como marcadores, sobre la spline | 5 Hz | `UTN_RunHUDWidget` |
| Minimapa | Spline del circuito y punto por buggy (color del tinte) | 5 Hz | Reutilizado (O25) |
| Flecha de borde | Al siguiente arco si está fuera de pantalla | Cada frame | `TN_HUDFaces.h` (U3) |
| Nombre e icono | Del buggy delante y detrás a < 60 m | 5 Hz | Reutilizado |
| Dirección contraria | «¡CONTRAMANO!» centrado + pitido; se apaga al corregir | Estado replicado | Nuevo |
| Enderezar | «Pulsa R para enderezar» tras 2 s volcado | Estado local | Nuevo |
| Fin | Cuenta atrás de cierre (30 s) | 1 Hz | `UTN_RaceFinishCountdownWidget` |

### 7.2 Panel de artillera

2 huecos de ítem con icono (O22) y cuenta atrás de cooldown de A1; anillo de equilibrio para A2 (`UTN_HoldRingWidget`, rojo < 20 %); en Después: barra de la caracola. La conductora (biplaza) ve solo el ítem equipado como icono pequeño. Sola: ambos.

### 7.3 Pantallas

Semáforo 3 luces (O21), presentación de carrera (`UTN_RaceRoundIntroWidget`), resultados (`UTN_RaceTallyWidget`, con puntos de la carrera y total de copa), campeona (`UTN_RaceChampionWidget` + podio O23). Lobby: selector de país/isla con contorno (O25), Copa o suelta, plaza Sola/Conductora/Artillera, requisito de jugadoras en rojo si no se cumple (Rally exige ≥ 2 buggies).

### 7.4 Accesibilidad y red

Color nunca es la única señal (icono más color); texto ≥ 24 px a 1080p; el aviso de contramano tiene pitido; cada cartel de pantalla dispone de clave de localización. En red, todo texto sale de estado replicado (sin RPC por frame).

### 7.5 Cómo se enseña

Primera carrera: carteles de 4 s antes del semáforo (acelerar/derrape, uso de ítem según plaza, enderezar). Cada cartel se muestra una vez por perfil (`UTN_CosmeticSaveGame` sin campo nuevo: bit en el guardado de tutorial existente). El aviso de contramano y el de enderezar son la enseñanza en contexto.

## 8. Efectos (reutilizados)

Sin Niagara nuevo. Todo se dispara en cada máquina a partir de estado replicado o de la velocidad replicada, sin RPC por frame.

| Efecto | Disparo | Recurso | Parámetros [1.ª pasada] |
|---|---|---|---|
| Derrape | \|velocidad lateral\| > 3 m/s (calculada de la velocidad replicada) o freno de mano con v > 30 km/h | `UTN_TurtleDustComponent` teñido por material de suelo; sonido de `UTN_BuggyEngineSynth` (chirrido) | Densidad ∝ deslizamiento; máx. 4 emisores activos por buggy |
| Marcas de derrape | Idem | Decal en pool de 64 | **Después** (O26) |
| Turbo (coco) | `bBoosting` replicado | Estela: mismo polvo con densidad ×3; FOV +8° en la cámara del buggy propio durante 0,3 s de subida y bajada; sonido del sintetizador con tono +4 semitonos | 2 s |
| Motor | Rpm y carga (de velocidad replicada) | `UTN_BuggyEngineSynth` (procedural C++, patrón `ISoundGenerator`) | Máx. 4 buggies con síntesis completa (los más cercanos); resto, versión simple |
| Salto y aterrizaje | Ruedas en el aire > 0,3 s | Polvo al aterrizar; sacudida de cámara leve (≤ 2° de amplitud) | |
| Agua somera | Rueda en agua | `UTN_BeachFinishSplashSubsystem` | |
| Choque | Impulso > umbral | Destello + `UTN_ShellImpactSynthComponent` | Umbral: Δv > 20 km/h |
| Contramano | `WrongWay` | Aviso en pantalla + pitido | |
| Enderezar | Evento replicado | Nube de polvo + «¡hop!» (`RaceCueSynth`) | |
| Checkpoint | Puerta validada | Nota ascendente por arco (`UTN_RaceCueSynthComponent`) | |
| Túnel | Entrar en volumen de túnel | Reverberación en el bus de audio | Cámara con `bDoCollisionTest` probada |
| Ítems | Uso/impacto | `TN_RaceItemSynth`; caracola: solo SFX | |
| Bocina | H | `UTN_BuggyEngineSynth` (`OnHorn` hoy vacío) | Sin efecto de juego |

## 9. Backend y red

### 9.1 Modelo

**Opción A** de [B] §3: servidor con autoridad, entradas del conductor por `ServerUpdateState`, `PredictiveInterpolation` en proxies. Los 2,4 m de HellYeah son el retraso normal de un proxy. La opción E (autoridad del cliente conductor validada, 20 a 30 h) queda **Después** y solo se activa si la medición falla (criterio en §11).

| Estado | Autoridad | Réplica |
|---|---|---|
| Chasis (posición, rotación, velocidades) | Servidor | Movimiento replicado de Chaos, 30 Hz cerca, 10 Hz lejos |
| Entradas del conductor | Cliente → servidor | `ServerUpdateState` 30 Hz, ≈ 24 B, no fiable |
| Turbo, escudo, `WrongWay`, `Scared` | Servidor | Bits en una `uint8`, `ForceNetUpdate` |
| `LeanDir` (Contrapeso), bits de acción | Servidor | int8 a 15 Hz |
| Checkpoint, vuelta, puesto | Servidor | PlayerState al cambiar; array de puestos a 5 Hz en el GameState |
| Ítems del buggy | Servidor | Al cambiar (2 huecos = 2 B) |
| Cajas (máscara por buggy) | Servidor | 28 B una vez, luego deltas |
| Proyectiles (mina, cangrejo) | Servidor | Actor replicado; cangrejo a 10 Hz |
| Skin (`FTN_BuggyLook`) | Cliente (propone), servidor valida | PlayerState, una vez |
| Hora de salida | Servidor | Una vez (`StartServerTime`) |

`ForceNetUpdate` en choque, turbo, enderezado, reaparición y entrada a un ramal de bifurcación (`net.UseAdaptiveNetUpdateFrequency=1` está activo).

### 9.2 Presupuesto de ancho de banda (est.; base 85 B por actualización de buggy, ≈ 5 KB/s a 60 Hz, medido por HellYeah)

Anfitrión con 7 clientes remotos y 8 buggies:

| Concepto | Cálculo | KB/s |
|---|---|---|
| Buggies visibles por cliente: 3 cerca a 30 Hz, 4 lejos a 10 Hz | 3 × 2,55 + 4 × 0,85 = 11,05 por cliente × 7 | 77 |
| Cabeceras y acks | ≈ 15 % | 12 |
| Ítems, proyectiles y bits de acción | ≈ 1 por cliente × 7 | 7 |
| GameState/PlayerState/cajas | | 2 |
| **Total subida del anfitrión (régimen)** | | **≈ 98** |
| Salida (todos a < 60 m durante 5 s): tope a 20 Hz | 7 × 7 × 1,7 + 21 | ≈ 104 |
| Objetivo de aceptación | | ≤ 140 |
| Bajada de un cliente | 7 buggies + cabeceras | ≈ 15 |
| Subida de un cliente | 30 Hz × 24 B + cabeceras | ≈ 1 |

Relevancia: más de 300 m del jugador → 5 Hz. Sin conexión con 8 buggies a 60 Hz completa (280 KB/s) en ningún caso.

### 9.3 Casos de red

- Latencia hasta 120 ms de ida y vuelta: conducción aceptable con opción A; a 150 ms con 2 % de pérdida se comprueba con `p.NetShowCorrections` (criterio §11).
- Choques entre buggies: el servidor resuelve el rebote arcade; el rival se ve 1 a 2,4 m detrás de su posición real (aceptado).
- Enderezado y reaparición: el servidor teletransporta y `ForceNetUpdate`; los clientes ven un salto de hasta 1 m; no hay predicción.
- `AddImpulse` de minas solo en el servidor; sin empujón local (evita el doble empujón de `TN_BeachMine.cpp:522`).
- Late-join: **Fuera** (§2).

## 10. Optimización

| Aspecto | Presupuesto / medida | Cómo |
|---|---|---|
| Malla del mapa | ≤ 1,2 M triángulos; ≈ 100 trozos de 100 m con contenido; banda de ±150 m del eje a ≈ 1 M tri/km², el resto a voxel de 2 a 4 m y ≈ 0,2 M tri/km² | Decimado en `terrain_vol/mesh.py` |
| Memoria de malla / bake | ≤ 21 MB / ≤ 2,5 MB | Verificación del bake |
| Voxelizado | 128 niveles en Z; +45 % de memoria de densidad en el generador | Aceptado en [E] «Decisiones» 4 |
| Streaming | Solo cargan los trozos en `[s_min − 200 m, s_max + 400 m]` (el pelotón); resto descargado; el servidor conserva colisión de todo el rango del pelotón | Carrera lineal |
| Colisión del túnel | Colisión simple decimada | [P] §6 (riesgo Chaos con malla de triángulos) |
| Física | Subpasos a 60 Hz síncrona; objetivo del anfitrión con 8 buggies + tortugas ≤ 16,6 ms de frame | `buggy_measure` + `stat unit` |
| Clientes | Los proxies no simulan: no necesitan colisión lejana del chasis | Opción A |
| Progreso | Proyección con ventana de arco (O(1) por buggy y tick); posiciones a 5 Hz, no a cada frame | §5.3 |
| Audio | Síntesis completa en 4 buggies cercanos; simple en el resto | §8 |
| VFX | Polvo hasta 80 m de la cámara; ≤ 4 emisores por buggy | §8 |
| Decals | Pool de 64 (Después) | |
| Tinte | Un `M_BuggyPaint` instanciado con parámetros dinámicos: 1 material base | Sin duplicar materiales |

## 11. Pruebas y aceptación

### 11.1 Automáticas

| # | Prueba | Tipo | Criterio |
|---|---|---|---|
| T1 | `TNRally::IsWrongWay` | Unitaria pura | Casos: adelante, contra, marcha atrás < 20 km/h, umbral 1,5 s |
| T2 | Orden de posiciones | Unitaria | Vuelta > puerta > arco; empate por `PlayerId`; lazos con niveles |
| T3 | Atajo ilegal (60 %) | Unitaria | Puerta no cuenta; bifurcación con ramal corto declarado |
| T4 | Reaparición (punto y orientación) | Unitaria | Última puerta válida, spline, 3 s + 2 s de fantasma |
| T5 | Puntos de copa y desempate | Unitaria | 10-8-6-5-4-3-2-1; DNF = 0 |
| T6 | Validador del corredor | pytest | Todo §4.1 con las excepciones declaradas (rampa 14°) |
| T7 | `BuggySpec` | Automatizada | Sin `Cargo`, sin boost recargable |
| T8 | Reproducibilidad del bake | Script | Mismo hash en 3 ejecuciones |
| T9 | Piloto IA headless | Simulación | **10 recorridos por mapa**: 0 atascos y 0 vuelcos sin enderezar; tiempo 107,7 s ± 8 % (E01B) o 110 a 130 s |
| T10 | `buggy_measure` | Medición | 0 a 60 km/h ≈ 3 s; punta ≈ 95 km/h; aceleración lateral en círculo de 25 m; masa registrada |
| T11 | A2 Contrapeso | Simulación | Círculo de 25 m a 70 km/h: vuelca ≥ 50 % sin él y ≤ 10 % con él |

### 11.2 Manuales (PIE 4P con 150 ms y 2 % de pérdida, y un 8P en máquina de anfitrión)

| # | Escenario | Aceptación |
|---|---|---|
| M1 | Posiciones y vueltas | Iguales en todas las máquinas en cada cambio |
| M2 | Enderezar y reaparecer | Sin saltos de más de 50 cm en el resto de máquinas (salvo el propio teletransporte) |
| M3 | Correcciones del conductor | `p.NetShowCorrections`: < 10 correcciones de > 50 cm por minuto. Si se supera, se activa la opción E |
| M4 | Ancho de banda | Subida del anfitrión ≤ 140 KB/s con 8 buggies |
| M5 | Skin | La misma en 4 máquinas y persiste al reiniciar |
| M6 | Duración de la copa | Ganador entre 105 y 135 s; el último llega a menos de 60 s del ganador |
| M7 | Túnel y cámara | La cámara no atraviesa el techo en ningún túnel de los 3 mapas MVP |
| M8 | Contramano | Aviso a los 1,5 s; giro automático a los 4 s; sin exploit por lazo |
| M9 | Equilibrio solo/biplaza | Ningún tipo gana > 60 % en 10 copas |
| M10 | Semáforo y salida anticipada | Motor cortado 1 s; nadie sale antes del verde |

## 12. Presupuesto de assets

| Concepto | Clase | ¿Imprescindible? | Cuenta como nuevo |
|---|---|---|---|
| Kit de mallas del buggy biplaza (`SM_TN_BuggyBody` + `SM_TN_BuggyTire`) | BORRADOR IA → final del equipo de arte | **Sí** | **1** |
| `M_BuggyPaint` (instancia de `M_TurtleBody`) | NUEVO | **Sí** | **1** |
| Marcas de derrape (decal) | NUEVO | Deseable (Después) | 0 |
| Poses de inclinarse y aterrizar (A2, A5) | NUEVO | Deseable (Después) | 0 |
| Llantas y decoración (`SOCKET_Deco_*`) | BORRADOR IA (`buggy.py`) | Después | 0 |
| Túnel, puente natural, rampa, muro | REUTILIZADO (densidad) | No | 0 |
| Puentes de Mokius | EXISTENTE | No | 0 |
| Arco de meta/checkpoint/salida | REUTILIZADO | No | 0 |
| Caja de ítems y 4 ítems | EXISTENTE | No | 0 |
| Río, mar, vegetación | EXISTENTE | No | 0 |
| Motor, derrape, bocina, caracola | Código (sintetizador) | No | 0 |
| VFX (polvo, estela, salpicadura) | REUTILIZADO | No | 0 |
| HUD, minimapa, semáforo | Código y widgets existentes | No | 0 |
| **TOTAL de assets nuevos imprescindibles** | | | **2** (1 kit de 2 mallas con borrador ya hecho + 1 instancia de material) |

El plan [P] §3.6 y [E] §4 contaban 2: coincide. Deseables: 3 (decal, dos poses).

## 13. Horas y dependencias

### 13.1 Horas MVP (estimación propia, primera pasada)

| Bloque | Incluye | Horas |
|---|---|---|
| B1 Buggy y física | Port del núcleo 6 a 8; tests y `buggy_measure` 3 a 4; subpasos y medida 4 a 6; pawn buggy con tortuga visual 8 a 12; sync de turbo 4 y mina 2 | 27 a 36 |
| B2 Biplaza | Asientos 12 a 16; A1 Lanzar 8 a 12; A2 Contrapeso 10 a 14 | 30 a 42 |
| B3 Backend de carrera [S] | Trazado, puertas, vueltas, posiciones, contramano, enderezado, reaparición (incluye muerte por mar y KillZ), atajos, salida, fin, rebufo, anti-vuelco base | 45 a 60 |
| B4 Pulido de conducción | Colisiones y atropello 6 a 8; fricción por material 8 a 12; cámara 4 a 5 | 18 a 25 |
| B5 Efectos y audio | `UTN_BuggyEngineSynth` 10 a 14 (F2) y el resto de §8 | 15 a 20 |
| B6 Interfaz | U7 (12 a 16), U3 (parte), lobby y pantallas | 25 a 35 |
| B7 Skins y carrocería | Datos, save y réplica 5 a 6; material 3 a 4; tienda 6 a 8; importar el kit y pipeline Blender 7 a 10 | 21 a 28 |
| B8 Red | 8 buggies a 30/10 Hz, sonda de correcciones | 6 |
| B9 Mapas | E01B añadido (túnel 6 a 8, puente 8 a 12, cauce 4 a 6, piloto IA 6 a 8) 24 a 34; lazo, circuito, `Laps` y ventana de arco 12 a 16; L10 Japón 8 a 12; I03-R 8 a 12 | 52 a 74 |
| B10 Pruebas | Unitarias y manuales | 10 |
| **Total MVP** | | **249 a 336** |

Fuera de esa suma y ya contadas en el plan: **F6b** (GeoRegion, ruta por hitos, tallado, validador) 63 a 99 h, que incluye el corredor de E01B; asientos [B] §6 #10.

### 13.2 Después (no bloquea)

E01B Después (bifurcación con puente natural 8 a 10, rampa 3 a 4, viaducto 2 a 3) 13 a 17 · A3 5 a 7 · A4 3 a 5 · A5 20 a 28 · expulsión y reenganche (est.) 10 a 14 · 11 países restantes a 8 a 12 h cada uno (est.) 88 a 132 · trampolín/catapulta de vehículo 6 a 8 · marcas de derrape 6 a 8 · opción E (condicional) 20 a 30. **Total Después: 171 a 249 h.**

### 13.3 Dependencias

1. **F6a / F6c** desbloqueadas por la decisión 2 del director (biplaza) [P] §7.
2. **F6b** (GeoRegion y corredor) antes de cualquier mapa; el generador `isla` (compartido con Todos contra Todos) antes de I03-R.
3. Medir el **puente de Mokius** (≥ 12 m, cubierta apta para vehículos) antes de colocar `ATN_BridgeSpan`.
4. **Rango Z de 128 niveles** en el voxelizado antes de E01B.
5. `TN_RunGameMode` retirado / Coop unificado no bloquea el Rally, pero `ATN_RallyGameMode` no debe heredar de él.
6. `TN_RaceItemComponent` con objetivo vehículo y `RollLoot` por puesto listos antes de A1.
7. `UTN_BuggyEngineSynth` (F2 de [U]) antes de la primera prueba de sensación.
8. Orden: B1 → B3 → B9 (E01B) → B2 (A1) → B6/B5 → A2 → B8 → mapas 2 y 3 → B10.

## Anexo A. Contradicciones entre documentos (no resueltas en silencio)

| # | Documentos | Contradicción | Valor usado aquí | Necesita al director |
|---|---|---|---|---|
| 1 | [E] §2.1 vs [S] §1 | Cierre de carrera: «corte a los 60 s del ganador» y «último ≈ 150 s» (E) frente a «20 s» (S) | `FinishGraceSeconds` = 30 s, parametrizable | Sí |
| 2 | [E] §2.3 vs [S] §1 | Parrilla: «invertida por puntos» (E) frente a «por posición de la carrera anterior o aleatoria» (S) | Carrera 1 aleatoria; 2 y 3 por puntos de copa ascendentes | Sí |
| 3 | [B] §2 vs [E] §3.1 | Pasajeras «en caparazón sin física» frente a artillera visible fuera del caparazón | E (decisión del director) | No |
| 4 | [E] §2.2 vs [C] decisión final | E01B a 0,4 km/m con ventana de 1,33 × 2,19 km (corredor) frente a país entero | 0,42 km/m, ventana de país 2,38 × 2,02 km, sinuosidad 1,20 | Sí (¿basta cambiar la escala?) |
| 5 | [S] §4 vs `INDEX.md` | Borradores IA de Rally «en curso» frente a un índice sin ninguno | No se depende de ellos | No |
| 6 | [P] §3.3 vs [E] §5 | Boost de `Coconut` 2 s frente a 2,5 s de HellYeah | 2,0 s | No |
| 7 | [P] §3.3 vs [E] §2.1 | «Carrera de 8 a 10 min» frente a 120 s | Copa de 3 carreras; ítems mantenidos | No |
| 8 | [C] §4.2 vs `TN_BeachTrampoline` | Catapulta y trampolín de catálogo: hoy usan `LaunchCharacter` | Solo rampas talladas en el MVP | No |
| 9 | [C] §4.2 (exageración 1,2 a 2,5) vs decisión final (calibrada de E01) | Exageración por región frente a la de E01 | 3 a 6×, calibrada por país | No |
| 10 | [E] §3.1 vs este documento | Expulsión y reenganche de la artillera en el MVP | Movido a **Después** | No (recorte de alcance) |
| 11 | [C] I06 vs regla del 15 % de túneles | Túnel submarino de 250 m (12,5 % de 2 000 m, pero > 15 % si se acorta el eje) | Recortar a 200 m si el mapa entra | No |

## Anexo B. Preguntas para el director

1. **«Sin fichas».** ¿Es correcto interpretar «fichas» como el `Cargo` y las fichas de carga de HellYeah (`HYBuggy.h` tiene 5 y `HYBuggy.cpp` 20 referencias a Cargo/Chip/Token)? Si se refería a otra cosa (p. ej. monedas del HUD), decirlo.
2. **Cierre de carrera** y **parrilla** (Anexo A #1 y #2): ¿30 s y parrilla invertida por puntos?
3. **Escala 0,42 km/m** para España (Anexo A #4) y ventana de país completa de hasta 2,4 km de lado: ¿aceptable el tope de memoria (≈ 21 MB por mapa)?
4. **Copa por defecto.** ¿Tres mapas al azar, elegidos por el anfitrión, o un orden fijo (España → Japón → Tortuga Magna)?
5. **Rusia compactada** (X 4,0 / Y 2,5 km/m): ¿se acepta la deformación del contorno, o se recorta a la parte europea?
6. **Jugadora sola sin rivales**: ¿contrarreloj en el MVP o exigir 2 buggies?
7. **Solo vs. biplaza**: ¿anti-vuelco pasivo +0,10 g o algún otro compensador?


## Anexo C. Respuestas (2026-09-29, por delegación del director)

1. Sí: «sin fichas» = sin `Cargo` ni fichas/tokens de carga de HellYeah.
2. Cierre a **20 s** tras el primero (igual que Carrera y 2 vs 2; sustituye a los 30 s de §1) y parrilla invertida por puntos de copa.
3. Aceptable como tope, pero el objetivo es **≤ 12 MB por mapa** tras decimar (plan §2.5): con ~40 mapas el paquete no debe pasar de ~500 MB de terreno.
4. Copa por defecto: el anfitrión elige; si no elige, 3 mapas al azar sin repetir.
5. Rusia compactada con deformación aceptada (se reconoce la forma).
6. Jugadora sola: contrarreloj en el MVP.
7. Sí: anti-vuelco pasivo +0,10 g para el buggy de una sola tortuga.

## Anexo D (2026-09-29): cómo aparecen los objetos

1. **Filas de cajas en la calzada** (7 por carrera): cruzan todo el ancho; se cogen atravesándolas con el buggy y **reaparecen a los 3 s** (los buggies van rápido y el pelotón llega junto).
2. **Cajas dobles en rutas alternativas** (bifurcaciones y atajos arriesgados): dan 2 objetos; reaparecen a los 10 s. Premian arriesgar.
3. **Caja de remontada del cielo**: cada 30–40 s una gaviota existente la suelta sobre la calzada 60–100 m por delante de los 2 últimos buggies, con sombra de aviso de 2 s; se recoge al pasar por encima; solo cuenta para la mitad de atrás.
Todo lo decide el servidor con `RollLoot` por posición; sin assets nuevos (misma caja).
