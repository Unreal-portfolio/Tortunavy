# Catálogo de mapas: un mapa por idioma, islas y arenas (Rally y Todos contra Todos)

Fecha: 2026-09-29. Petición del director: un mapa por idioma, más islas reales o ficticias y arenas abstractas, para Rally y Todos contra Todos (TcT). Base: `Docs/Rally_E01B_y_Biplaza.md` (§2) y `Docs/2026-09-29-Plan-Maestro-Modos-y-Mapas.md` (§2.5, §3.3, §3.4). Terminología: Rally = Rally Tortuga; TcT = Todos contra Todos; «meseta», «nido», «caparazón» como en el plan maestro.

## 0. Resumen y reglas comunes

- **25 mapas**: 13 por idioma (L01 a L13), 6 islas (I01 a I06) y 6 arenas (A01 a A06). Por modo: **10 Rally, 10 TcT, 5 ambos** (los «ambos» son dos variantes del mismo lugar, `-R` y `-T`, con generador distinto; se cuentan como un mapa del catálogo pero cuestan dos).
- **0 assets nuevos por mapa.** Solo se reutilizan: puentes colgantes y naturales de Mokius, túneles, catapultas, trampolines, gaviotas, cangrejos y la vegetación de la playa. Lo que sí es código nuevo se lista en §6 (no es asset).
- **Rally** (E01B): 120 s de ganador, eje de **2 400 m**, ≈ 80 km/h de media, copa de 3 carreras. Límites de trazado: pendiente ≤ 12°, radio ≥ 25 m, ancho ≥ 12 m.
- **TcT** (§3.4): rondas de 120 s, 2 a 8 jugadoras, caer al agua = baja con reaparición de 4 s. Tamaño de arena **supuesto** de 90 a 180 m de lado o diámetro para que haya un encuentro cada 15 a 20 s; a validar en playtest, porque el tamaño de P01 no se ha leído para este documento.
- **Fuentes MDE** (libres): Copernicus GLO-90 (paso 90 m), SRTM 1″ (30 m), ETOPO (batimetría de costa). Regla de resolución: el paso real dividido por la escala debe ser ≤ 6 m de juego; si no, se cambia a SRTM 1″. Remuestreo bicúbico más ruido fractal de 0,5 m de amplitud para quitar el aspecto de terraza.
- **Compresión vertical**: el eje y la banda de ±150 m llevan cotas de autor (como E01B §2.2); fuera de la banda manda el MDE con exageración E indicada. Rango Z máximo 64 m (128 niveles de 0,5 m, pregunta 4 de E01B).
- **Fronteras y política**: ningún mapa incluye una frontera disputada ni simbología política. Donde el borde real es una frontera (solo L01 con los Pirineos), el mapa la resuelve como muro de roca o costa, nunca como línea o bandera.
- **Estado por mapa**: MVP (piloto o de bajo coste), Later (tras el pipeline probado) o Cut? (candidato a recorte si falta tiempo). Ver §5.

## 1. Mapas por idioma

Escala «1 m = X km» significa que 1 m de juego equivale a X km reales. Tamaño = ventana del mapa en m de juego (incluye 250 m de margen por lado en Rally). Borde: costa, frontera (resuelta como muro, ver arriba), ambos o ninguno (muro de valle).

| id | Idioma | Región | Modo | Escala 1 m = km | Tamaño (m) | Fuente MDE | Rasgos jugables emblemáticos (exagerados) | Borde | Estado |
|---|---|---|---|---|---|---|---|---|---|
| L01 | es-ES | España, Somport a Málaga (E01B, ya existe) | Rally | 0,4 | 1 330 × 2 190 | GLO-90 (hecho) | Túnel de los Pirineos con salida a +44 m; puente colgante sobre el Tajo; meta en la playa | ambos | Existe |
| L02 | en | Highlands de Escocia: Glen Coe, Ben Nevis, Great Glen | Rally | 0,03 | 1 600 × 2 900 | GLO-90 | Salto de catapulta «Nessie» sobre el lago (sin monstruo, solo una ola sospechosa); túnel de 80 m bajo el Ben Nevis; niebla de ovejas: filas de rocas redondas que parecen ovejas | costa (fiordo) | Later |
| L03 | fr | Provenza: gorges du Verdon, Sainte-Victoire, calanques de Marsella | Rally | 0,04 | 1 500 × 2 700 | GLO-90 | Puente natural del cañón; túnel bajo la Sainte-Victoire; acantilado final de las calanques con trampolín «de Cézanne» | costa | Later |
| L04 | de | Garganta del Rin medio, Bingen a Coblenza | Rally | 0,027 | 1 400 × 2 900 | GLO-90 | Curvas de horquilla entre castillos de roca (cajas de ítems en las almenas); puente colgante sobre el Rin; peña de la Loreley como rampa | ninguno (valle) | Later |
| L05 | it | Venecia y su laguna | TcT | 0,006 | 200 × 200 | referencia geográfica, sin MDE (llano) | Seis «sestieri» unidos por 9 puentes; «góndolas» como mesetas bajas; canal central de baja obligatoria | costa (laguna) | MVP (bajo coste) |
| L06 | pt-BR | Río de Janeiro: Copacabana, Pão de Açúcar, Corcovado, Tijuca | Ambos | 0,01 | R: 1 700 × 2 900 / T: 220 × 160 | SRTM 1″ | Teleférico como puente colgante entre dos cumbres; rampa de la playa; Cristo con brazos como dos mesetas enfrentadas | costa | Later |
| L07 | ru | Kamchatka: Koryaksky, Avachinsky, bahía de Avacha | Rally | 0,03 | 1 500 × 2 700 | GLO-90 | Géiseres como trampolines (columnas de vapor que lanzan); cráter volcánico como cuenco de paso; meta en la bahía tras bajar los dos volcanes | costa | Later |
| L08 | pl | Península de Hel, Báltico | Rally | 0,015 | 800 × 2 900 | GLO-90 más cotas de autor (dunas) | Carretera de un solo carril entre dos mares: cae a cualquier lado y a repetir; dunas móviles como saltos; faro final | costa (ambos lados) | Later |
| L09 | tr | Capadocia: valles de Göreme y Rojo | Ambos | R: 0,015 / T: 0,02 | R: 1 500 × 2 700 / T: 180 × 180 | SRTM 1″ / paramétrica | «Chimeneas de hadas» como columnas; ciudad subterránea = red de túneles con 3 salidas; bandadas de gaviotas a la altura de los «globos» (sin globos: no hay asset) | ninguno (valle) | Later |
| L10 | ja | Monte Fuji: bahía de Suruga a cráter | Rally | 0,012 | 1 400 × 2 700 | SRTM 1″ | Cono perfecto con subida en horquillas; túnel de lava en la ladera; trampolín al cráter; meta en la cima sobre las nubes | costa | **Piloto** |
| L11 | ko | Isla de Jeju: Seongsan a Hallasan | Ambos | R: 0,02 / T: 0,4 | R: 1 500 × 2 900 / T: 182 × 102 | GLO-90 / paramétrica | Túnel de lava largo (Manjanggul, 150 m); cráter con lago (Baengnokdam) como meta; T: isla elíptica con cono central | costa | Later |
| L12 | zh-Hans | Guilin y río Li: karst | Ambos | R: 0,035 / T: 0,1 | R: 1 400 × 2 900 / T: 160 × 160 | GLO-90 / paramétrica | Torres de karst como mesetas; puente natural por el arco de la Luna; río con meandros de 180° | ninguno (valle) | Later |
| L13 | zh-Hant | Cordillera Central y garganta de Taroko | Rally | 0,02 | 1 400 × 2 900 | SRTM 1″ | Túneles encadenados en la garganta (cuatro seguidos); puente colgante de 120 m; cima del paso con nubes y trampolín final hasta la costa este | costa (este) | Later |

Notas:
- L01: no se rediseña; entra en el catálogo para que la copa de idiomas lo incluya.
- L06, L09, L11, L12: la variante `-T` (TcT) es una arena pequeña que toma el emblema del lugar (Pão de Açúcar, chimeneas, Hallasan, torres de karst); no recorta el MDE del Rally.
- L05 es el único mapa por idioma solo de TcT; se elige Venecia porque sus islas unidas por puentes son un TcT natural.
- Nombres de L12 y L13: regiones y accidentes geográficos, sin denominaciones políticas en la interfaz.

## 2. Islas

Islas reales (I01, I02, I05, I06 como inspiración) y ficticias (I03, I04). El tipo `isla` es el generador paramétrico; `geo` usa el MDE.

| id | Isla | Tipo | Modo | Escala 1 m = km | Tamaño (m) | Fuente | Rasgos jugables emblemáticos | Borde | Estado |
|---|---|---|---|---|---|---|---|---|---|
| I01 | **Filipinas rara** («Archipiélago de las Mil Bolas») | isla | TcT | 0,3 (ficticia) | 180 × 240 | paramétrica | 7 islas de 3 tamaños, 9 puentes colgantes, volcán central perfecto (Mayon exagerado) que «humea» con gaviotas; la isla mayor tiene forma de tortuga vista desde el aire | costa | **Piloto** |
| I02 | Galápagos | isla | TcT | 0,5 | 200 × 160 | paramétrica | 5 islas, cada una con una «tortuga gigante» de roca (meseta redonda); Isla «Isabela» con dos volcanes gemelos; cangrejos rojos (los existentes) como peligro | costa | Later |
| I03 | **Tortuga Magna** (ficticia, isla con forma de tortuga gigante) | isla | Ambos | R: 0,02 / T: 0,25 | R: 1 400 × 1 100 / T: 150 × 180 | paramétrica | Caparazón hexagonal de 13 escamas a cotas distintas (T); cabeza con túnel de entrada (la boca); aletas como rampas; Rally: circuito por el borde del caparazón | costa | Later |
| I04 | Isla Volcán Hueco (ficticia) | isla | Rally | 0,02 | 1 300 × 1 300 | paramétrica | Circuito de un solo lazo por el borde de la caldera; túnel al interior; lago de lava = agua de muerte; meta al pie de la boca | costa | Cut? |
| I05 | Santorini | isla | TcT | 0,2 | 170 × 170 | paramétrica (anillo de caldera, batimetría ETOPO como referencia) | Anillo de acantilados con el centro hundido (agua); dos puentes naturales cruzando la caldera; «cúpulas azules» como mesetas pequeñas | costa | Later |
| I06 | Islas Feroe | geo | Rally | 0,012 | 1 300 × 2 700 | SRTM 1″ | Túnel submarino (cruce de fiordo) de 250 m; puentes entre islas; acantilados de 500 m con gaviotas por millares (las existentes, en cuadrilla) | costa | Cut? |

Justificación de «Cut?»: I04 y I06 no aportan un rasgo que no tengan ya otros mapas (caldera en I05 y L10; túnel largo en L11 y L13). Se recortan primero.

## 3. Arenas abstractas TcT

Todas `arena` paramétrica, solo TcT. Agua de muerte a cota 0 m; cotas sobre el agua. Se diferencian de P01 (mesetas rectangulares unidas por puentes) en la forma global y en que la arena cambia durante la ronda.

| id | Nombre | Forma | Diámetro o lado (m) | Idea jugable | Hundimiento por ronda | Estado |
|---|---|---|---|---|---|---|
| A01 | **Diana** | 5 anillos concéntricos | 120 | «Rey de la colina»: quien esté en el centro (cota +12) tiene ventaja de tiro pero es el objetivo; 4 rampas radiales a 15° | El anillo exterior cae a los 40 s, el 4.º a los 80 s, el 3.º a los 105 s | **Piloto** |
| A02 | Dónut | Anillo con hueco central de agua | 130 (hueco 40) | Pasillo circular de 30 m: se corre en círculo persiguiendo; 4 puentes cruzan el hueco | El anillo pierde 4 sectores de 90° (uno cada 30 s) | Later |
| A03 | Espiral | Rampa en espiral de 2,5 vueltas | 100 | De 0 a +14 m en ≈ 470 m de rampa a 1,7°; se cae hacia el interior y se vuelve a subir al reaparecer en la base | La cola exterior se hunde a 60 s | Later |
| A04 | Reloj | Disco con 12 «horas» y dos agujas giratorias | 120 | Dos agujas de 50 m (hora y minuto) barren la arena a 1 vuelta/120 s y 1 vuelta/30 s; hay que saltarlas | Sin hundimiento: las agujas son el peligro | Later |
| A05 | Tablero de mesetas | Rejilla 5 × 5 de mesetas de 20 m unidas por rampas | 160 | Cota de cada meseta al azar en {+3, +6, +9} (semilla); las bajas son fáciles de tomar | Cada 30 s, una fila de mesetas baja 3 m | Later |
| A06 | Panal | 19 hexágonos de 24 m | 130 | Hexágonos con separación de 2 m (saltos de puente entre ellos); el central lleva cofre | Se hunden por anillos, del exterior al centro, cada 30 s | Cut? |

Nota: A03 y A06 se marcan menos prioritarias por la misma razón: A03 se parece a A01 con menos mordiente, A06 a A05.

## 4. Parámetros del generador y aceptación por mapa

Los generadores existentes (`Scripts/gen_terrain_platforms.py`, `gen_terrain_spain.py`) reciben un fichero de disposición (`--layout`), `--name` y `--seed`; cada fila de abajo es el contenido de esa disposición. Los nombres de los campos son orientativos hasta escribir el generador `isla` y `arena`. Distancias entre hitos: orientativas; las fija el script al proyectar el MDE.

Convención de posiciones. Rally: `t` = fracción del eje (0 salida, 1 meta) y entre paréntesis los metros. TcT: (r m, θ°) desde el centro de la arena.

### 4.1 Aceptación común

| Concepto | Rally | TcT |
|---|---|---|
| Pendiente máxima transitable | ≤ 12° (medida con `buggy_measure`) | ≤ 20° en rampas y puentes (igual que las rampas de 20° de P01); 0° de escalón > 1 m sin rampa |
| Ancho mínimo | ≥ 12 m en calzada, túneles y puentes | ≥ 8 m en mesetas; ≥ 3 m en puentes (supuesto, validar con P01) |
| Radio mínimo de curva | ≥ 25 m | no aplica |
| Longitud | eje 2 400 m ± 5 % | diámetro o lado según §3, ± 3 m |
| Tiempo de referencia | ganador del bot 110 a 130 s | encuentro entre jugadoras cada 15 a 20 s (telemetría) |
| Triángulos | ≤ 1,2 M | ≤ 0,35 M |
| StaticMesh (17 MB por M tri) | ≤ 21 MB | ≤ 6 MB |
| Bake (`bake.bin`) | ≤ 2,5 MB | ≤ 0,3 MB |
| Reproducibilidad | misma semilla, mismo hash del bake en 3 ejecuciones | ídem |
| Assets nuevos | 0 | 0 |
| Cofres, nidos y cajas | 7 filas de cajas y meta válida orientada según §3.1 del plan | 8 nidos a ≥ 15 m entre sí y a ≥ 5 m del agua; 1 cofre; 2 catapultas y 3 trampolines |

Cada mapa hereda toda la tabla; las tablas siguientes solo dan lo específico.

### 4.2 Rally por idioma y variantes `-R`

Tipo `geo` salvo I03-R e I04 (`isla`). E = exageración vertical fuera de la banda de ±150 m del eje. Todos con hitos en (lat, lon) y sinuosidad = eje 2 400 m / recta.

| id | Semilla | Hitos H0 a Hn | Recta / sinuosidad | Cotas del eje `t:cota (m sobre el agua)` | Puentes `t (longitud m)` | Túneles `t (longitud m)` | Catapulta y trampolín | E |
|---|---|---|---|---|---|---|---|---|
| L01 | ver E01B | Somport, Zaragoza, Guadalajara, Toledo, Despeñaperros, Málaga | 2 097 m / 1,14 | según E01B §2.4 | según E01B §2.5 | según E01B §2.5 | según E01B §2.5 | 3 a 4 |
| L02 | 2002 | Glen Coe (56,68; −5,10), Ben Nevis (56,80; −5,00), Loch Lochy (57,00; −4,90), Fort Augustus (57,14; −4,68) | 2 000 m / 1,20 | 0:+2, 0,25:+18, 0,50:+42, 0,75:+20, 1:+1 | 0,35 (48) colgante; 0,62 (30) natural | 0,50 (80) | catapulta 0,88; trampolín 0,97 sobre el lago | 1,5 |
| L03 | 2003 | Verdon (43,75; 6,33), Sainte-Victoire (43,53; 5,61), Aix (43,53; 5,45), Cassis (43,21; 5,53) | 2 100 m / 1,14 | 0:+30, 0,30:+45, 0,55:+38, 0,85:+22, 1:+2 | 0,15 (60) natural; 0,70 (36) colgante | 0,45 (120) | trampolín 0,96 (acantilado) | 2,0 |
| L04 | 2004 | Bingen (49,97; 7,90), Loreley (50,15; 7,73), Coblenza (50,36; 7,60) | 2 000 m / 1,20 | 0:+3, 0,30:+14, 0,55:+22, 0,80:+12, 1:+3 | 0,45 (56) colgante | 0,25 (70) | catapulta 0,53 (Loreley); trampolín 0,90 | 2,5 |
| L06-R | 2006 | Leme (−22,96; −43,17), Corcovado (−22,95; −43,21), Pico da Tijuca (−22,94; −43,29), Pedra da Gávea (−23,00; −43,28), Barra (−23,01; −43,36) | 2 000 m / 1,20 | 0:+1, 0,30:+34, 0,55:+42, 0,80:+20, 1:+1 | 0,20 (72) colgante «teleférico»; 0,65 (40) natural | 0,40 (90) | trampolín 0,30 y 0,97 | 2,0 |
| L07 | 2007 | Elizovo (53,18; 158,39), Koryaksky (53,32; 158,71), Avachinsky (53,26; 158,83), bahía de Avacha (52,85; 158,60) | 1 750 m / 1,37 | 0:+6, 0,30:+30, 0,50:+44, 0,75:+16, 1:+1 | 0,40 (44) natural | no | 3 trampolines «géiser» en 0,20, 0,55 y 0,80 | 1,5 |
| L08 | 2008 | Władysławowo (54,79; 18,42), Jastarnia (54,70; 18,68), cabo de Hel (54,60; 18,80) | 2 270 m / 1,06 | 0:+2, 0,40:+9 (duna), 0,70:+4, 1:+2 | ninguno | ninguno | 3 trampolines en las dunas: 0,25, 0,50, 0,78 | dunas 8 (autor) |
| L09-R | 2009 | Avanos (38,72; 34,85), Göreme (38,64; 34,83), Uçhisar (38,63; 34,81), Ürgüp (38,63; 34,91), Derinkuyu (38,37; 34,74) | 2 000 m / 1,20 | 0:+8, 0,25:+22, 0,55:+30, 0,85:+12, 1:+6 | 0,35 (40) natural | 0,45 (110), 0,70 (70) | catapulta 0,60 | 2,5 |
| L10 | 2010 | Fuji ciudad (35,13; 138,68), Fujinomiya 5.ª estación (35,32; 138,73), cráter (35,36; 138,73) | 2 250 m / 1,07 | 0:+1, 0,25:+10, 0,50:+24, 0,75:+38, 1:+48 (compresión vertical 0,15) | 0,30 (50) colgante; 0,80 (36) natural | 0,55 (110) túnel de lava | trampolín 0,95 al cráter | 0,15 |
| L11-R | 2011 | Seongsan (33,46; 126,94), Manjanggul (33,53; 126,77), Baengnokdam (33,36; 126,53) | 2 250 m / 1,07 | 0:+2, 0,30:+8, 0,55:+26, 0,85:+46, 1:+44 | 0,75 (38) natural | 0,35 (150) | trampolín 0,20 | 1,5 |
| L12-R | 2012 | Guilin (25,28; 110,29), meandros del río Li, Yangshuo (24,78; 110,49) | 1 700 m / 1,41 | 0:+2, 0,30:+6, 0,65:+9, 1:+2 (rodeando torres de +20 a +34) | 0,20 (52) natural; 0,60 (44) natural; 0,85 (30) colgante | 0,75 (60) | catapulta 0,45; trampolín 0,93 | 2,0 |
| L13 | 2013 | Qingshui (24,20; 121,70), garganta de Taroko (24,17; 121,57), Tianxiang (24,18; 121,48), Hehuanshan (24,14; 121,28) | 2 350 m / 1,02 | 0:+1, 0,30:+14, 0,60:+30, 1:+50 | 0,50 (120) colgante | 0,15 (80), 0,25 (90), 0,35 (60), 0,42 (70) | trampolín 0,98 | 1,2 |
| I03-R | 2103 | isla `isla`: lazo elíptico semiejes 460 × 280 m | circuito cerrado de 1 vuelta, perímetro 2 390 m | escamas: 0:+6 con oscilación ±3 m cada 400 m | ninguno | 0,00 y 0,50 (boca, 60) | catapulta 0,25 y 0,75 | no aplica |
| I04 | 2104 | `isla`: caldera circular radio 382 m | circuito cerrado, 2 400 m | borde +18, interior de la caldera −4 (agua de lava) | 0,50 (30) natural sobre la grieta | 0,62 (100) al interior | trampolín 0,90 | no aplica |
| I06 | 2106 | Sørvágur (62,07; −7,31), Vestmanna (62,16; −7,17), Tórshavn (62,01; −6,77) | 2 000 m / 1,20 | 0:+3, 0,30:+26, 0,60:+40, 0,85:+14, 1:+2 | 0,20 (60) natural; 0,55 (80) colgante | 0,40 (250) submarino | catapulta 0,70 | 2,0 |

Comprobaciones específicas de aceptación Rally:
- Pendiente máxima del eje de cada mapa: cálculo sobre la tabla; L13 sube 49 m en 2 350 m de eje (pendiente media 1,2°; el tramo 0,60 a 1 sube 20 m en 940 m, 1,2°) y el techo de 12° solo se roza en horquillas; L10 sube 47 m en 2 250 m (1,2° medio) y exige 6 horquillas de radio ≥ 25 m para no superar 12° (aceptación: 0 muestras del eje > 12°).
- Túneles: cada uno con fachada de altura válida en la entrada (plan §2.2) y ancho ≥ 12 m; L13 con cuatro túneles suma 300 m de los 2 350 (12,8 %), comprobar que no supera 15 % del eje.
- L08: la calzada de 12 m entre dos mares debe mantener ≥ 4 m de arcén a cada lado sobre el agua; el bake devuelve `path_dist` a la costa ≥ 10 m en todo el eje.
- L07: las 3 columnas de vapor son trampolines existentes con la vegetación de la playa ajustada; 0 efectos nuevos.

### 4.3 TcT: islas, arenas y variantes `-T`

Tipos: `isla` (formas orgánicas, plataformas de contorno libre), `arena` (geometría paramétrica: anillos, rejilla, hexágonos). Cotas en m sobre el agua (agua de muerte a 0 m). «Nidos» = 8 puntos de reaparición.

| id | Tipo | Semilla | Radio, lados o forma | Cotas | Anchos | Puentes | Túneles | Catapultas y trampolines | Hundimiento y otras reglas |
|---|---|---|---|---|---|---|---|---|---|
| L05 | isla | 5005 | 6 islotes irregulares de radio 28 a 36 m sobre un anillo de radio 62 m, θ = 0°, 60°, … 300° | islotes +3; canal central de agua de 14 m de ancho | canal 14 m; puentes 4 m | 6 perimetrales entre islotes vecinos (20 m) y 3 diagonales (θ 0-180, 60-240, 120-300; 44 m) | ninguno | catapultas en θ 0° y 180°; trampolines en θ 90°, 210°, 330° | Sin hundimiento; cada 40 s el viento (existente) empuja hacia el canal |
| L06-T | arena | 6006 | 2 mesetas enfrentadas de 30 m de radio a (±55, 0) | +18 cada una; base común del mar | 30 m | 1 colgante «teleférico» de 80 m, ancho 4 m, entre ambas | ninguno | trampolín en cada cima | El puente aguanta 4 pasos de corte (P4) y se corta a los 90 s |
| L09-T | arena | 9009 | 12 columnas («chimeneas») de radio 5 m repartidas sobre un círculo de radio 65 m con jitter ±6 m; columna central mayor (radio 8 m) | +8, +12 y +16 según semilla; central +20 | 10 a 16 m de diámetro | 8 puentes naturales o colgantes de 20 a 40 m entre columnas vecinas | ninguno | catapulta en la columna central; 3 trampolines | Cada 30 s, la columna más alta baja 4 m |
| L11-T | isla | 1111 | Elipse 91 × 51 m, cono central (Hallasan) de radio 28 m y cima +18 con cráter de radio 6 m (lago de agua, muerte) | orilla +2; ladera +2 a +18 con pendiente 19° | orilla 18 m de ancho | ninguno | 1 túnel de lava de 40 m a través del cono, ancho 12 m | 3 trampolines en el borde; catapulta en la cima | Sin hundimiento; el lago del cráter es muerte |
| L12-T | arena | 1212 | 9 torres de karst de radio 8 a 12 m sobre una rejilla 3 × 3 de paso 46 m con jitter ±5 m | +8 a +30 según semilla (la más alta al centro) | radio 8 a 12 m | 8 naturales o colgantes | ninguno | 2 catapultas en las torres bajas; 3 trampolines | Las torres bajan 4 m de una en una a los 40, 80 y 105 s (la más baja primero) |
| I01 | isla | 1001 | Isla mayor «Tortuga»: elipse 90 × 60 m; 3 medianas de radio 18 m y 3 pequeñas de radio 10 m sobre un anillo de radio 85 m; volcán en el centro de la mayor | mayor +4; volcán: base radio 34 m, cima plana radio 6 m, altura +14 (pendiente 19,6°); medianas +3; pequeñas +2 | 4 m en puentes | 9 colgantes de 25 a 45 m | ninguno | 2 catapultas (mayor a medianas), 3 trampolines (pequeñas) | Cada 30 s se hunde una isla pequeña (3 en total); a los 90 s el volcán «erupciona»: 3 gaviotas caen con ítems |
| I02 | isla | 1002 | 5 islas: Isabela (2 conos gemelos de radio 20 m, cima +12), Santa Cruz (radio 24), San Cristóbal (radio 20), Floreana (radio 14), Española (radio 14); cada una con una meseta redonda «caparazón» de radio 6 m | base +2 a +4; caparazones +5 | 4 m | 6 (3 colgantes y 3 naturales) | ninguno | 2 catapultas, 3 trampolines | Sin hundimiento; 2 cangrejos rojos en Floreana y Española; loot mejor en los caparazones |
| I03-T | isla | 1103 | Caparazón elíptico 110 × 80 m con 13 escamas hexagonales de lado 12 m (1 central, 6 en anillo interior, 6 en el exterior); cabeza y 4 aletas | central +9, anillo interior +6, exterior +3 | surco entre escamas de 1,5 m (saltable) | ninguno | boca de 30 m de largo y 14 m de ancho | 4 aletas como rampas a 15°; 3 trampolines en el borde | Las escamas exteriores se hunden a los 60 s; las interiores, a los 100 s |
| I05 | isla | 1005 | Anillo de caldera: radio exterior 75 m, interior 30 m (agua); 8 «cúpulas» de radio 5 m sobre el borde | acantilado +10 en el borde interior, exterior +2 con caída suave a 15° | anillo 45 m | 2 naturales de 40 m que cruzan la caldera (θ 0-180 y 90-270) | ninguno | 2 catapultas en el borde interior; 3 trampolines | Sin hundimiento; las cúpulas son cobertura y respawn |
| A01 | arena | 3001 | 5 anillos: r 0-6, 6-18, 18-32, 32-46, 46-60 | +12, +9, +6, +4, +2 | anillos de 12 a 14 m (el central r 6) | 4 rampas radiales a θ 0°, 90°, 180°, 270°: ancho 6 m, pendiente 15° | ninguno | catapultas a (25, 45°) y (25, 225°); trampolines a (39, 0°), (39, 120°), (39, 240°) | Anillo 5 cae a 40 s, anillo 4 a 80 s, anillo 3 a 105 s (bajan 5 m en 5 s con aviso de 5 s); nidos: 8 en r = 25, cada 45° |
| A02 | arena | 3002 | Anillo de radio exterior 65 m e interior 25 m | +4 uniforme | 40 m | 4 puentes de 3 m de ancho cruzando el hueco (θ 0°, 90°, 180°, 270°) | ninguno | 2 catapultas (θ 45°, 225°); 3 trampolines | Cada 30 s se hunde un sector de 90° (orden por semilla); nidos: 8 en r = 45 |
| A03 | arena | 3003 | Espiral de 2,5 vueltas, r 10 a 50 m, paso de 16 m por vuelta | de 0 a +14 (≈ 470 m de rampa, 1,7°) | 12 m | ninguno | ninguno | 2 catapultas (arranque de la 2.ª y 3.ª vuelta); 3 trampolines | La cola exterior (última vuelta) se hunde a 60 s |
| A04 | arena | 3004 | Disco de radio 60 m, 12 pilares de 3 m en las horas | +4 | 120 m | ninguno | ninguno | 3 trampolines (horas 4, 8, 12); catapulta central | Sin hundimiento; 2 agujas: hora (50 m, 0,5 rev/min) y minuto (50 m, 2 rev/min), barrera baja de 0,8 m que noquea; nidos: 8 en r = 45 |
| A05 | arena | 3005 | Rejilla 5 × 5 de mesetas de 20 × 20 m con separación de 9 m (lado total 136 m) | {+3, +6, +9}, reparto forzado 9/9/7; diferencia máxima entre vecinas ≤ 3 m | 20 m | rampas o puentes de 9 m, pendiente ≤ 18,4° | ninguno | 2 catapultas y 3 trampolines en mesetas elegidas por semilla | Cada 30 s baja 3 m una fila (orden por semilla) |
| A06 | arena | 3006 | 19 hexágonos de lado 12 m en 3 anillos (1 + 6 + 12), separación de 2 m | +4; central +6 con el cofre | 24 m entre caras | ninguno (el hueco de 2 m se salta) | ninguno | 3 trampolines en el anillo exterior | Se hunde un anillo cada 30 s, del exterior al centro |

Comprobaciones específicas de aceptación TcT:
- I01: el cono no supera 20° (medido 19,6°) y desde cualquier isla se llega a otra en ≤ 3 puentes.
- A01: los 4 escalones de 2 a 3 m solo se salvan por rampa o por catapulta; ninguna jugadora en un anillo hundido queda sin ruta al siguiente (rampas y anillo 3 presentes hasta el final).
- L11-T: el cono da un solo lugar alto; el túnel de lava evita que domine sin coste.
- Ninguna arena supera 180 m de lado o diámetro; los tiempos de cruce a la velocidad de la tortuga caminando (a medir; la cifra no se ha leído) deben permitir un encuentro cada 15 a 20 s.

## 5. Orden de producción por lotes

Coste en horas: estimación propia (no medida) tras tener el generador de cada tipo. Un mapa Rally `geo` reutiliza el pipeline de E01B (`gen_terrain_spain.py`); el trabajo real es escribir los hitos, las cotas de autor y las marcas de puentes y túneles. Las arenas y las islas necesitan el generador paramétrico nuevo (§6).

| Lote | Mapas | Motivo | Coste estimado |
|---|---|---|---|
| 0. Pilotos (ya encargados) | L10 Japón Fuji (Rally), I01 Filipinas rara (TcT), A01 Diana (TcT) | Validan los tres pipelines: geo con compresión vertical, isla paramétrica y arena con hundimiento | L10 14 a 20 h; I01 10 a 14 h; A01 8 a 12 h; más 16 a 24 h del generador `isla`/`arena` (compartido) |
| 1. Copa mínima y arenas cortas | L01 (ya existe), L02 Escocia, L13 Taroko (Rally, copa de 3 carreras); A02 Dónut, A05 Tablero (TcT) | Completa una primera copa de Rally y refuerza el TcT con arenas baratas | Rally 10 a 14 h cada uno; arenas 4 a 6 h |
| 2. Islas y TcT de idioma | I02 Galápagos, I05 Santorini, I03 Tortuga Magna (-T y -R), L05 Venecia | Todo el TcT restante sobre el generador `isla` ya probado | Islas 6 a 10 h; I03 doble 14 a 18 h; L05 6 a 8 h |
| 3. Rally por idioma restante | L03 Provenza, L04 Rin, L07 Kamchatka, L08 Hel | Rally puro; se ordenan por dificultad de trazado (L08 es el más simple, L07 el más complejo por los géiseres) | 10 a 14 h cada uno |
| 4. Mixtos y tardíos | L06 Río, L09 Capadocia, L11 Jeju, L12 Guilin (-R y -T cada uno); A03 Espiral, A04 Reloj | Cada uno pide dos variantes; A04 depende de la aguja giratoria (§6) | 14 a 20 h cada mixto; A03 4 a 6 h; A04 8 a 12 h |
| 5. Candidatos a recorte | I04 Volcán Hueco, I06 Feroe, A06 Panal | Duplican rasgos de otros mapas | I04 8 a 10 h; I06 10 a 14 h; A06 4 a 6 h |

Copas sugeridas (3 carreras): Copa del Pacífico (L10, L13, L12-R), Copa del Atlántico (L01, L02, L06-R), Copa Volcánica (L07, L11-R, I04). Las demás copas se forman con los mapas del lote 3. Estas agrupaciones son una propuesta; el contenido de las copas lo decide el director.

MVP: lote 0 y lote 1. Later: lotes 2 a 4. Cut?: lote 5. Solo el lote 0 y la mitad del lote 1 caben en el hito de demostración del plan maestro; el total del catálogo (25 mapas) es una cifra de varios meses de trabajo y no tiene fecha comprometida.

## 6. Requisitos de código (no son assets) y preguntas

Lo que el catálogo exige y hoy no existe (estimación propia; a validar con el equipo):

| Requisito | Para | Coste | Estado |
|---|---|---|---|
| Generador `isla` (formas orgánicas con puentes colgantes) | I01, I02, I03, I05, L05, L11-T | 12 a 16 h | Nuevo |
| Generador `arena` (anillos, rejilla, hexágonos, espiral) | A01 a A06, L06-T, L09-T, L12-T | 12 a 16 h | Nuevo |
| Actor de hundimiento por cota (baja una plataforma o sector con aviso y tiempo) | A01, A02, A03, A05, A06, I01, I03-T, L09-T, L12-T | 10 a 14 h (servidor autoritativo, réplica de la cota) | Nuevo (código) |
| Actor de aguja giratoria (barrera baja que noquea) | A04 | 8 a 10 h | Nuevo (código) |
| Modo de generación `geo` con compresión vertical (factor 0,15) | L10, L13 | 4 a 6 h sobre `gen_terrain_spain.py` | Extensión |

Preguntas para el director:
1. Cifra del catálogo: 25 mapas son 10 Rally, 10 TcT y 5 ambos. ¿Se aprueba recortar el lote 5 (3 mapas) y dejar 22?
2. Hundimiento durante la ronda (como en A01) o entre rondas: aquí se ha diseñado durante la ronda (a los 30 a 105 s). Cambiar a entre rondas quita el actor de hundimiento pero pierde la tensión final.
3. Rally I03-R e I04 son circuitos de 1 vuelta; los demás son punto a punto. ¿Se acepta mezclar? E01B no fija circuito (ver su §2.3).
4. Tamaño de las arenas TcT (90 a 180 m): se ha supuesto sin haber medido P01. Confirmar contra el tamaño y la velocidad reales antes de generar A01.
5. Nombres en la interfaz: L12 («Guilin») y L13 («Cordillera Central y Taroko») usan solo nombres geográficos. ¿Algún nombre más que se quiera evitar?
6. Humor de los rasgos («ovejas de roca», «Nessie», «Cézanne»): ¿se traduce por idioma o se deja el nombre local con nota?

## Decisión del director (2026-09-29): países enteros miniaturizados

Los mapas por idioma dejan de ser regiones emblemáticas: cada uno es el **país entero miniaturizado** con MDE real y su contorno (costa y frontera como borde), como E01 España, adaptado a su modo (Rally: calzada tallada que cruza el país; Todos contra Todos: zonas llanas y puentes naturales). Asignación: es-ES España (E01/E01B), en Reino Unido, fr Francia, de Alemania, it Italia, pt-BR Brasil, ru Rusia (muy compactada en longitud), pl Polonia, tr Turquía, ja Japón, ko Corea del Sur, zh-Hans China continental (solo geografía, sin fronteras internas ni rótulos políticos), zh-Hant isla de Taiwán (nombre geográfico). Las regiones del catálogo anterior pueden reutilizarse como tramos o puntos emblemáticos dentro de cada país. Relieve con la exageración calibrada de E01; sin exagerar de más.

## 7. Catálogo completado: arenas A02 a A06 (lote C1, 2026-09-29)

Generador: `Scripts/gen_terrain_inventados.py --lot C1` (A02, A03 y A05 con `terrain_shapes/arena.py`; A04 y A06 con `terrain_shapes/arena_extra.py`). Validadores en verde: transitable desde el inicio, pasarelas de ≥ 3 m a ≤ 20°, sin islas inalcanzables, costuras sin grietas, presupuesto TcT (≤ 0,35 M triángulos, ≤ 6 MB) y 8 nidos a ≥ 15 m entre sí y ≥ 5 m del agua (`nests_uu` en el manifest). Lámina en `Docs/Mapas/<id>.png`.

| id | Nombre | Modo | Tamaño (m) | Triángulos | MB | Rasgos generados | Diferencias con §3 |
|---|---|---|---|---|---|---|---|
| A02 | Dónut | TcT | 130 (hueco 50) | 99 640 | 0,39 | Anillo a +4 m, 2 puentes naturales que cruzan el hueco | 2 puentes de lado a lado (equivalen a los 4 radiales de §3) |
| A03 | Espiral | TcT | 115 | 121 066 | 0,56 | Rampa de 2,5 vueltas de +0,8 a +14 m entre fosos de agua | Ninguna |
| A04 | Reloj | TcT | 120 | 97 522 | 0,39 | Disco a +4 m, 12 pilares de las horas (+7 m; el de las 12, +8 m), 48 marcas de minuto, estrado del eje; marcas `eje_agujas`, `catapulta` y 3 `trampolin` | Las agujas son actores (§6), no terreno |
| A05 | Tablero de mesetas | TcT | 136 | 154 408 | 0,75 | Rejilla 5 × 5 a +3, +6 y +9 m unida por rampas naturales | Ninguna |
| A06 | Panal | TcT | 114 | 114 690 | 0,52 | 19 hexágonos de 12 m de lado a +4 m (central a +6 m con el cofre), huecos de 2 m de agua; 26 istmos de 4 m (árbol + 8 al azar) y el resto de huecos marcados como `salto` | Istmos añadidos para que ningún hexágono dependa solo del salto |

## 8. Catálogo completado: islas I02 a I06 (lote C2, 2026-09-29)

Generador: `Scripts/gen_terrain_inventados.py --lot C2` (TcT en `terrain_shapes/lots_islands.py`; Rally en `terrain_shapes/lots_rally.py` sobre `terrain_shapes/rally_circuit.py`). Criterio «camino primero» (director, 2026-09-29): en Rally el terreno se recorta al prisma de la calzada con desmonte y terraplén ≤ 31° (validador `taludes`: p99 ≤ 35°); túnel solo con ≥ 8 m de roca sobre la bóveda (gálibo ≥ 5 m, ancho ≥ 12 m, validador `cobertura`), si no, collado o trinchera; bocas con fachada a 60°. Rally: calzada de 14 m con arcenes de 5 m, radio ≥ 25 m, pendiente ≤ 12°, `checkpoints_uu` cada 200 m; circuitos de 1,2 a 1,5 km (2 vueltas ≈ 2,3 a 3 km).

| id | Nombre | Modo | Tamaño (m) | Triángulos | MB | Rasgos generados | Diferencias con §2 y §4 |
|---|---|---|---|---|---|---|---|
| I02 | Galápagos | TcT | 200 × 200 | 118 504 | 0,75 | 5 islas; Isabela con dos conos de +11 m a 18°; cúpula «caparazón» de +2,5 m en cada isla; 5 puentes naturales (3 marcados `puente_colgante`); `cangrejo_rojo` en Floreana y Española | 5 puentes, no 6: el sexto se cruzaría con otro |
| I03-T | Tortuga Magna | TcT | 200 × 200 (isla 175 × 170) | 103 388 | 0,52 | Caparazón de 13 escamas (+9, +6, +3) con surcos secos de 0,3 m, 5 rampas a 15°, 4 aletas-rampa desde la playa, cola y boca en túnel de 14 m por la cabeza (+11,5 m); marcas de hundimiento a 60 y 100 s | Surcos de 0,3 m en vez de 1,5 m de hueco (a 20° de marcha no se cruzaría) |
| I03-R | Tortuga Magna | Rally | 600 × 600 | 971 356 | 5,15 | Lazo de 1 291 m por el borde del caparazón (+3 a +9 m), túneles bajo la cabeza y la cola con ≥ 8 m de roca, caparazón escalonado hasta +18 m | Vuelta de 1,3 km (2 vueltas) en vez de 2,4 km: el presupuesto de 1,2 M triángulos limita a 600 m de lado |
| I04 | Volcán Hueco | Rally | 600 × 600 | 1 040 536 | 5,46 | Sube por el flanco al borde (+18 m), baja por el otro, túnel bajo el borde, viaducto a +6 m sobre el lago de lava y túnel de salida al sur | Vuelta de 1,5 km; el borde de la caldera a +21 m (sin exagerar) obliga a cruzarlo por debajo a +6 m |
| I05 | Santorini | TcT | 200 × 200 (anillo 150) | 130 308 | 0,82 | Anillo r 30 a 75 m, acantilado interior a +10 m y bajada a +2 m, 2 puentes que cruzan la caldera, Thirasia separada por dos canales (uno con puente), 8 cúpulas | Ninguna |
| I06 | Islas Feroe | Rally | 600 × 600 | 969 048 | 5,14 | Paramétrica (sin MDE): tres islas alargadas con cresta hasta +20 m; el lazo de 1 153 m salta los estrechos en 4 viaductos detectados sobre el agua y cruza cada isla por su collado | Sin MDE ni túnel submarino (el agua es muerte); sin túnel: la cresta no deja 8 m de roca |
