# 02 — Modo CARRERA (a pie, playa de Mokius)

Estado: especificación de diseño, 2026-09-29. Rama `macro-update`. Fuentes: plan maestro §3.2, §3.6 y §7; análisis `Docs/Analisis/2026-09-29/E_carrera_autor.md` (eliminado) (en adelante **[E]**); `Docs/Modo_Carrera.md`; `Docs/Modos-UI-FX-2026-09-29.md` §2.2; `Docs/Inventario-Objetos-Arte-2026-09-29.md`; código `TN_BeachLayout.h`, `TN_BeachRaceGenerator.h`, `TN_RaceItems.h`.

Marcas de alcance: **MVP** (primera entrega jugable), **Después**, **Cortado**.

## 00. Relación con el Coop (documento de diferencias)

Aclaración del director (2026-09-29): la Carrera es **casi lo mismo que el Coop pero con mapa recto, sin puzzles, caótica y rápida**. Este documento es por tanto un **delta sobre `01-Coop.md`** (base común, todavía en redacción cuando se escribió esto: las referencias a sus secciones son provisionales y hay que reconciliarlas al cerrarlo). Todo lo que no aparece como «propio» abajo se hereda tal cual.

| Área | Se hereda del Coop (no se especifica aquí) | Propio de la Carrera (se especifica aquí) |
|---|---|---|
| Enemigos y amenazas | Catálogo `ETNBeachElement` de enemigos, IA, `ApplyHitStun`, tormenta de bañistas | Densidad y colocación por banda (§3.5), halo de trampas |
| Objetos | Inventario (mano + caparazón), `DT_Items`, cajas y cofres, `RollLoot` | Objetos `TN_RaceItems`, pesos por posición (§5), 2 objetos nuevos |
| Interfaz y efectos | `UTN_RunHUDWidget`, avisos, aturdimiento, pajaritos, sintetizadores | Reloj de ronda, recuento, sprint, podio (ya existen); cero pantallas nuevas |
| Red | Servidor con autoridad, dormancy, relevancia, `UTN_BeachRoundSyncComponent` | `FTNBeachRoundNet` ampliada con curso y tramos (§7.1) |
| Mapa | Terreno volumétrico fijo con puzzles | **Pista recta de Mokius** (800 m, relieve fijo) + tramos de autor (§3); sin puzzles ni bifurcaciones |
| Progresión | Puzzles por compuertas, meta de grupo | Rondas y puntuación por conchas (§1-2); 8 jugadores compitiendo |
| Muerte | Muerte y revive del Coop | No se muere: se aturde (bola de caparazón) |

**Caótica y rápida, en números** (todo ya está en el juego; solo se fija el objetivo):

| Palanca | Valor | Efecto |
|---|---|---|
| Ronda media | 3:20 (tope 9:00) | Rápida: 4-6 rondas por partida |
| Cajas de objetos | 20-25 sueltas + 3 filas (hasta 8 tortugas las cogen a la vez) | Un objeto cada ~30 s de carrera por jugador |
| Objetos ofensivos por partida | Con 8 jugadores, 4-8 objetos ofensivos activos a la vez (topes globales de §8) | Caos legible sin saturar la red |
| Peligro constante | Sin recta libre al mar y respiro cada ~150 m (§3.5) | Presión constante con pausas |
| Sin puzzles | Cero elementos que obliguen a esperar a otra | La única cooperación posible es incidental |

## 0. Decisiones del director aplicadas

| Decisión | Consecuencia en este documento |
|---|---|
| Se queda el generador de Mokius; el relieve NO se toca | `TerrainSeed` sigue `constexpr`; el hash del heightfield debe salir idéntico (test) |
| Pulir el reparto (D1-D4 y ritmo de [E]) | §7 |
| Tramos de autor (DataAsset por tramo) | §3.2 |
| Ningún modo principal 100 % procedural en assets | Cada ronda lleva de 2 a 4 tramos de autor (§3.3) |
| Torbellino del caparazón ya arreglado | No se trata aquí; solo entra en el plan de pruebas como regresión (§9) |
| Todo sincronizado en red | §7.1 |
| Minimizar assets nuevos | Total de imprescindibles: **0** (§10) |

Contradicciones detectadas entre fuentes (no se resuelven en silencio):

1. `Modo_Carrera.md` línea 108: límite de ronda **9 min**; el texto de diseño del mapa (líneas 34-38) calcula ~3 min 20 s por ronda. No hay contradicción de fondo (9 min es el tope, 3:20 la media objetivo), pero el ritmo real hay que medirlo (§9).
2. `Modo_Carrera.md` línea 42 habla de «cuatro huevos» pero la salida real es un nido de **ocho** huevos en dos filas de cuatro (línea 1479). Este documento usa ocho.
3. El test del reparto solo mira la fila de 4 huevos (línea 1499). Con 8 jugadores no hay test de equidad de la segunda fila: se añade en §9.
4. Plan maestro §3.6 cuenta «Tramos de autor: 0 assets» y «Mallas propias de tramo: Deseable». Coherente con este documento (fase 2 = Después).

---

## 1. Fantasía, jugadores, duración, rondas y victoria

**Fantasía.** Eres una tortuga cría de ~5 cm en una playa a escala 28×, llena de basura humana, castillos de arena y fauna hostil. Todas salen a la vez del nido de huevos y corren hacia el mar. Es una carrera de karts a pie: se esquiva, se rebusca, se usan objetos contra las rivales y se salta un acantilado de 15,5 m al agua. En la carrera nadie muere: lo que mata en el Coop aquí aturde (bola de caparazón).

**Experiencia buscada** (y cómo se enseña):

| Sensación | Cómo se consigue | Cómo se enseña |
|---|---|---|
| «Cambio de rumbo constante, nunca una recta libre» | Reparto sin línea libre al mar, filas con hueco | Se aprende corriendo; la primera banda (15-110 m) tiene peligros telegrafiados y sin enemigos |
| «Me han hecho una jugarreta» | Objetos con atribución (quién te lo tiró) | Icono del objeto en el HUD al usarlo; el nombre del atacante sale en el aviso de aturdimiento (§6) |
| «Puedo remontar» | Pesos por posición, pelícano, cofres | Los cofres y fortalezas se ven desde lejos (columnas doradas de las cajas) |
| «Cada ronda es distinta pero reconocible» | Tramos de autor con nombre + relleno procedural | Los tramos tienen una seña visual fuerte (§3.4) que el jugador aprende a nombrar |

| Parámetro | Valor | Motivo |
|---|---|---|
| Jugadores | 1-8 (`UMP_GameInstance::MaxPlayers` = 8). Con 1, entrenamiento sin rivales: los objetos que exigen otra tortuga dan «nop» | Ya soportado |
| Recorrido | 800 m × 280 m jugables, relieve fijo | Existente |
| Duración de ronda | Objetivo 3:20 de media; tope 9:00 (`RoundTimeLimitSeconds`); cuenta atrás de llegada 10 s | Existente |
| Rondas | Se juega hasta que una tortuga llega a **3 conchas** (una concha = 2 medias). Estimación: 4-6 rondas (14-22 min con recuentos de ~20 s). **Por medir**; con 8 jugadores puede alargarse (ver pregunta P2) | Existente |
| Victoria | Primera en 3 conchas. Empate en lo más alto con 3 o más al cerrar ronda → **sprint final** (tope 4:30, `SprintTimeLimitSeconds`) solo entre las empatadas | Existente |

---

## 2. Flujo de partida y recompensas

MVP = lo existente, sin cambios de flujo. Los cambios de este documento son solo de contenido (pista, objetos) y de red.

1. **Lobby** (`LVL_Lobby`): el anfitrión elige Carrera y dificultad con el General Galápago o en el selector de modo (plan §2.1). Todos listos → `LVL_BeachRace`.
2. **Ronda N** (`ETNBeachRacePhase`): `RoundNet.Round++`, el servidor elige semilla y **curso de autor** (§3), genera, replica, cuenta atrás, se abren los huevos.
3. **Primera al agua**: +1 concha entera; arranca la cuenta atrás de 10 s; quien llega dentro: +½ concha. Al acabar: «¡TIEMPO!» y gusano gigante para quien no llegó (sin gusanos si llegaron todas). Sin nadie en el agua al agotar los 9 min: concha para la más cercana al mar.
4. **Recuento** (`RoundResults`): caras, tres huecos de concha por jugador.
5. **Sprint** si hay empate a 3+; **Campeón** con podio (`ATN_RacePodiumStage`) y menú Volver a jugar / Cambiar de modo / Salir (solo el anfitrión).

**Recompensas dentro de la ronda** (todas existentes):

| Recompensa | Fuente | Valor | Autoridad |
|---|---|---|---|
| Objeto de carrera | Caja (20-25 sueltas + 3 filas), rebuscable, cofre | Sorteo por posición (§5) | Servidor (`RollLoot`) |
| Conchas de puntos (1/25/50/100) | Rebuscables, cofres, cima de fortaleza | Puntos de la ronda (no cuentan para las conchas de victoria; desempate de posición dentro del recuento — ver P4) | Servidor |
| Concha de victoria | Llegar al agua | 1, ½ | Servidor (`RaceShellHalves`) |

Fallos y bordes: desconexión a mitad de ronda → su tortuga se retira de `GatherRacers`; las conchas ganadas se conservan para el recuento. Anfitrión abandona → fin de partida (comportamiento actual de servidor de escucha; no se cambia).

---

## 3. Pista: generador, tramos de autor, ritmo y dificultad

### 3.1 Generador de Mokius (existente)

`TNBeachLayout::GenerateRound(Seed, Difficulty[, bSprint])` reparte ~3100 elementos (80 % decorado local en HISM). Orden de pasadas: estructuras (castillo principal al 42-58 %, fortalezas, quads, gaviotas, trincheras) → filas → enemigos → militar → trampas y ayudas destacadas → lanzadores → cofres → relleno por bandas de 50 m → `EnsureCatapults` → tapones e interés. Regla de oro: siempre queda paso en ±60 m (`FPassGrid`) y ninguna recta libre al mar. El relieve no depende de la semilla.

### 3.2 Formato del DataAsset del tramo (MVP)

`UTN_BeachStretchAsset : UPrimaryDataAsset`, **un fichero por tramo** (los `.uasset` son binarios y Alvaro2rh trabaja en `main`: un tramo = un fichero evita conflictos de fusión). Es el vocabulario `ETNBeachElement` actual, así que **no hay ruta nueva de red ni de render**.

| Campo | Tipo | Regla / valor por defecto |
|---|---|---|
| `Id` | FName | Único, `Stretch_<Nombre>`; obligatorio |
| `Version`, `ContentHash` | int32, uint32 | El hash lo escribe *Export*; cambia si cambia el contenido |
| `DisplayName`, `Notes` | FText | Para Diseño; no se muestran al jugador |
| `LengthX` | float (cm) | 4 000-20 000 (40-200 m). Mínimo 40 m para que sea reconocible; máximo 200 m para no comerse el 25 % de la pista |
| `WidthY` | float (cm) | ≤ 28 000. Tramos más estrechos dejan ancho al relleno procedural |
| `Anchor` | enum `Pinned` / `Floating` | `Pinned`: X absoluta (posible porque el relieve es fijo). `Floating`: rango `[MinT, MaxT]` en fracción de pista |
| `PinnedX` / `FloatRangeT` | float / vec2 | Solo el que corresponda |
| `bAllowMirrorY` | bool | Por defecto verdadero. Se decide en servidor con el RNG del curso |
| `bSprintSafe` | bool | Si el tramo puede convivir con el nido y la línea del sprint (§3.3) |
| `DifficultyMask` | bitmask Fácil/Normal/Difícil | Tramo que solo aparece en Difícil, etc. |
| `Items[]` | array de `FTNStretchItem` | Ver abajo |
| `Reserve[]` | cápsulas (centro, radio, largo) | Zona donde el relleno **no entra** (se rellena sola al exportar con la envolvente de `Items`, editable) |
| `FillPolicy` | enum `None` / `DecorOnly` / `Full` | `None`: el tramo es todo lo que hay en su caja. `DecorOnly`: alrededor solo decorado sin reglas. `Full`: el relleno procedural normal fuera de `Reserve` |
| `bFlattenPad` / `Stamp` | bool / asiento | Sello de asiento único; o asiento por pieza (`MakeStamp`) |
| `Interest[]` | puntos de botín | Puntos donde el botín procedural puede colocar cajas/rebuscables (no obligatorio) |
| `FallbackX` | float | X de respaldo si es `bRequired` y no cabe |

`FTNStretchItem`: `Element` (`ETNBeachElement`), `LocalPos` (X, Y en el marco del tramo), `Yaw`, `SizeScale`, `Extent`, `SpecSeed`, `Flags` (bloquea el paso / no bloquea; rebuscable; sin basura alrededor) y `DifficultyMask` propia.

`UTN_BeachRaceCourseAsset` (el «curso»; una partida usa uno):

| Campo | Regla |
|---|---|
| `Slots[]` | Cada uno: `Stretch` (asset concreto **o** `Pool` de assets), `XRange` o `PinnedX`, `bRequired`, `Chance` (0-1), `bAllowMirror`, `bSprintSafe` |
| `ProceduralPassMask` | Qué pasadas procedurales corren (castillo principal, fortalezas, filas, colosales, enemigos, trampas destacadas, relleno) |
| `GapSlots[]` | Slots **sin tramo**: `Theme`, `DensityMul`, `XRange` para huecos procedurales con carácter propio |
| `CooldownRounds` | Rondas antes de repetir un tramo del pool (por defecto 2) |
| `MinAuthored`, `MaxAuthored` | 2 y 4 (§3.3) |

Preajustes de curso:

| Curso | Uso | Máscara |
|---|---|---|
| `Course_Beach_Default` | Producción (**MVP**) | Todo procedural activo, 2-4 tramos de autor por ronda |
| `Course_Beach_Fixed` | Carrera «de autor» para pruebas, torneos y comparar tiempos (**Después**, casi gratis con el mismo código) | Todos los slots de autor, máscara casi vacía |
| Curso vacío | Prueba de regresión | Reparto idéntico bit a bit al actual (test dorado, §9) |

### 3.3 Cómo se mezcla con lo procedural

Orden de generación (dentro de `GenerateRound`, tras los cambios P1/P9 de [E]):

1. El servidor elige el curso, la semilla y la dificultad. Elige los tramos de cada slot con el historial de la partida (enfriamiento `CooldownRounds`, sin repetir los de las últimas 2 rondas) y decide el espejo. Con un `Pool` de tamaño ≤ `CooldownRounds` el enfriamiento se relaja (se repite el más antiguo).
2. `PlaceAuthored()` se ejecuta **antes** de `PlaceMainDungeon`, con su propio subflujo de RNG (así añadir o quitar un tramo no cambia el resto de la ronda; corrige D9 de forma parcial sin esperar a P9). Resuelve la X de los `Floating` probando candidatos contra `TerrainAllows` y anota los intervalos reservados (`ReservedIntervals`).
3. `AddAuthored` marca rejilla, ocupación y cubetas con `Role = Authored`. `RestorePassage` **salta** lo autoral: solo se quita lo procedural.
4. Las pasadas con ancla fija (castillo principal al 42-58 %, filas, colosales que solo caben en 15-120 y 280-440 m) consultan `IsXReserved` y se desplazan u omiten. Si el castillo principal no cabe, la ronda **no** fuerza uno (los tests actuales exigen 1-3 castillos: se parametrizan por curso).
5. **El relieve no se toca.** Si un tramo opcional no cabe se omite con aviso en el registro; uno `bRequired` va a su `FallbackX`; si tampoco cabe, la ronda falla en `Validate` (nunca en juego: los tramos se validan al exportar).
6. **Sprint**: `bSprint = true` reserva el nido y aparta el castillo de `[SprintX − 80, SprintX + 30]` m (P1). Los tramos que caen en esa ventana solo se colocan si `bSprintSafe`; el resto se omite en esa ronda.
7. **Paso libre**: lo de autor que bloquea no se puede quitar. Un tramo que por sí solo cierra el paso se rechaza al exportar.

**Cuántos tramos de autor por ronda:**

| Concepto | Valor | Motivo |
|---|---|---|
| Mínimo / máximo por ronda | **2 / 4** | Cumple «ningún modo 100 % procedural» sin renunciar a la variedad del generador |
| Cobertura máxima de longitud | ≤ 25 % (200 m de 800) | El resto se queda para lo procedural, con su cadena de pasos y densidad |
| Ubicación recomendada | 1 en la banda 60-200 m (aprendizaje), 1-2 en 450-650 m, 1 en 660-740 m (antesala del acantilado) | Evita el castillo principal y la zona del sprint |
| Pool objetivo del primer lote | 6 tramos (2 por zona) para que con `CooldownRounds = 2` no se repita nunca en 4 rondas | Contenido de Diseño; aquí se define el mínimo, no el arte |

### 3.4 Catálogo inicial de tramos (propuesta para Diseño; todo con elementos existentes)

| Id | Zona | Idea | Elementos (todos `ETNBeachElement` existentes) | Seña visual |
|---|---|---|---|---|
| `Stretch_Boardwalk` | 60-200 m | Pasarela que corre en diagonal con hueco y plataformas móviles | `Boardwalk`, `MovingPlatform`, `WobblyPlatform`, cajas de objetos en las esquinas | Tablones alineados |
| `Stretch_ToyArmy` | 450-650 m | Trincheras de juguete con tanque y sacos | `Sandbags`, `TankTrap`, `ToyTank`, `ToySoldiers`, `AmmoCrate` | Verde militar |
| `Stretch_BottleAlley` | 200-450 m | Callejón de botellas y latas con ermitaño bola | `Bottle`, `SodaCan`, `HermitCrab`, `Boardwalk` | Reflejos de cristal |
| `Stretch_ShellGarden` | 450-650 m | Jardín de conchas con `ClamTrap` y premio | `ClamTrap`, `ShellGate`, `TreasureChest` | Conchas de colores |
| `Stretch_TrampolineRun` | 660-740 m | Salto de plataformas con trampolines antes del acantilado | `Trampoline`, `Catapult`, `Seaweed`, `SeaUrchin` | Colchonetas |
| `Stretch_GullShade` | 200-450 m | Sombras de sombrillas, respiro con botín y una zona de gaviotas | `PlantedUmbrella`, `BeachChair`, `GullZone`, cajas | Sombrillas |

Cada uno pasa por *Validate* (§7.4) antes de subirse. Los tramos y el `Course_Beach_Default` son contenido de Diseño; los pesos y posiciones exactas se afinan en la banca de pruebas.

### 3.5 Ritmo y dificultad

Curva de intensidad objetivo por banda de 50 m (0 = respiro, 1 = pico), después de P4/P5 de [E]:

| Tramo de pista | Intensidad | Contenido típico | Densidad de decorado objetivo (de `BandCoverage`) |
|---|---|---|---|
| 0-15 m | 0 | Nada de reparto (salida) | 0 % |
| 15-110 m | 0,3 | Ayudas al centro/espejo (equidad), decorado suave, sin enemigos | 66-70 % |
| 110-300 m | 0,5-0,7 | Primera fila de obstáculos, enemigos ligeros, 1er tramo de autor | 70-75 % |
| 300-450 m | 0,8-1,0 | Castillo principal, fila con barrera, cofres, pico | 75-80 % |
| 450-650 m | 0,7-0,9 | Fortalezas, quads, gaviotas, 2º tramo de autor | 72-78 % |
| 650-800 m | 0,6 → 0,9 | Antesala del acantilado, trampolines, tormenta detrás | 66-72 % |

Reglas de ritmo (corrigen D5 y D6):

- Filas cada 60-110 m **sin posiciones fijas**, sin repetir tema ni lado seguido; nunca tres barreras a todo lo ancho en menos de 200 m.
- Un respiro (banda de intensidad ≤ 0,3, con botín) cada ~150 m.
- La cobertura de decorado **crece hacia el mar** y el objetivo es alcanzable: primer tercio ≥ 66 % (hoy 52 %).
- Halo de 2,5-3 m sin basura alrededor de trampas y lanzadores; 6 m limpios delante de las filas (legibilidad del peligro, D8).

Dificultad (existente, sin cambios de reglas): Fácil / Normal / Difícil escala cupos por banda (`(base + mar·t) × multiplicador`), enemigos y trampas; en Difícil los enemigos se apiñan. Los tramos con `DifficultyMask` aportan los picos de Difícil.

---

## 4. Obstáculos y elementos de playa

Todos son entradas de `ETNBeachElement` (ya existen; **0 assets nuevos**). Autoridad: el servidor crea el actor de juego (`SpawnElement`) y lo replica; el decorado sin reglas es local en cada máquina (HISM en `ATN_BeachDecorField`), reconstruido con la semilla. Colocación: `TryAdd` (`InBounds` → `Fits` → `TerrainAllows` → `SeatIsGentle` → paso ±60 m; lanzadores reservan su arco).

| Grupo | Elementos | Efecto para la jugadora | Reglas de colocación clave | Autoridad / réplica |
|---|---|---|---|---|
| Decorado (bloquea o guía) | Cocos, rocas, grupos de rocas, troncos, tablones, redes, sombrillas, sillas, restos de vela, madera, castillos pequeños y enormes | Obstáculo físico y esconde botín | Huella + `ItemPad` 1,5 m, 45 cm entre basura; halo en trampas | Local (no replicado) |
| Basura menuda | Latas, chapas, chanclas, brick, boya, toalla, crema, palitos, cáscaras, cuerda, gafas, cubito, pelota, frisbi, hueso, pato, pluma | Solo ambiente | Rellena hasta `BandCoverage` | Local |
| Guías | `Boardwalk`, `WoodenPostPath` | Camino sin obstáculos hacia el mar | Extent = largo; no marcan salida ni meta | Local |
| Militar | Sacos, cajas de munición, erizos antitanque, cascos, redes de camuflaje, bidones, soldaditos | Parapetos que bloquean | Filas de la tropa; respetan paso | Local |
| Trampas pasivas | Alambre de espino, algas, plataforma sobre hoyo, cubo roto, mina, concha que atrapa | Enredan, aturden o derriban unos segundos | Halo de 2,5-3 m; ranuras destacadas respetan `MaxT` (D10) | Servidor + réplica de estado (aturdimiento por `TNBeach::StunTurtle`) |
| Interacciones | Pala (trampolín/puente), plataforma móvil, catapulta, trampolín, puerta de conchas | Ayudan a saltar y atajar | Lanzadores reservan arco (22 m trampolín, 30 m catapulta); `OwnerItem` en la reserva (§7) | Servidor + réplica |
| Estructuras | `SandDungeon`, `FortressMedium/Large/Colossal`, `TreasureChest` | Recorrido interior, premios en la cima | Colosales solo en 15-120 y 280-440 m; castillo principal al 42-58 % | Servidor |
| Enemigos | Cangrejo gigante, erizo, lagarto, paso de quads, gaviotas/pelícanos, ermitaño bola, pulpo de poza, pulgas, tanque de juguete | Aturden o derriban; los objetos de carrera los marean | Enemigos apiñados en Difícil; ninguno en 0-110 m | Servidor con IA; réplica de transformación (relevantes a 200-300 m) |
| Amenaza global | Tormenta de bañistas | Empuja desde atrás | Más lenta que la carrera | Servidor |
| Final | Acantilado de 15,5 m y agua | Salto y llegada | Fijo | Servidor (llegada) |

Cambios de este documento sobre la tabla: (1) el halo de trampas (P6), (2) `OwnerItem` en reservas (P2), (3) el castillo principal no se borra en el sprint (P1), (4) los tramos de autor entran por `Role = Authored` (§3.3).

---

## 5. Objetos de carrera (`TN_RaceItems`)

**Total: 12 objetos jugables** (`Box` + 11 usables, con el triple coco contado como uno) y 3 propuestos (2 en MVP, 1 Después). El enum `ETNRaceItem` tiene 14 valores útiles (`Box`, `Coconut`, `TripleCoconut3/2/1`, `GoldenCoconut`, `PelicanTaxi`, `Sunscreen`, `HomingCrab`, `GullStrike`, `SandMine`, `StormCloud`, `Frisbee`, `Whistle`); los triples de 2 y de 1 usos no salen del sorteo.

### 5.1 Tabla completa

Estado de asset (regla del encargo): las mallas e iconos de los objetos de carrera **se construyen en código** en ejecución (`TN_RaceItemArt`, `ResolveVisuals`; icono de 128²), sin archivos. Por tanto todos son **EXISTENTE (procedural en código)**. «Pesos» = primera / a medias / última, valores de `PositionWeight` (cofre × = factor del cofre). «Mín.» = tortugas en carrera necesarias.

| # | Objeto | Efecto | Pesos (1.ª / medias / última) | Cofre × | Mín. | Autoridad y réplica | Asset (malla · icono · sonido) | Nuevos |
|---|---|---|---|---|---|---|---|---|
| 1 | Caja de objetos (`Box`) | No se lleva; al cogerla sale un objeto según puesto | — | — | — | Servidor sortea (`RollLoot`, fuente `Box`); pickup replicado, no reaparece en la ronda | EXISTENTE: cubo «?» en código | 0 |
| 2 | Coco turbo (`Coconut`) | ×2 sobre la velocidad de correr, 3 s, triple aceleración, FOV +16° | 2,0 / 2,2 / 1,4 | 1,0 | 1 | Servidor decide; estado replicado en `UTN_RaceItemComponent`; cada máquina aplica el mismo `SetRaceSpeedMultiplier` | EXISTENTE (código) · `RaceItemSynth` | 0 |
| 3 | Triple coco (`TripleCoconut3/2/1`) | Tres turbos, uno por uso; el icono pasa de 3 a 2 a 1 | 0 / 0,8 / 1,6 | 1,4 | 1 | Igual que el coco; el servidor cambia el objeto de la mano | EXISTENTE (código) | 0 |
| 4 | Coco dorado (`GoldenCoconut`) | Turbo ×2 durante 7 s con energía sin fin y sin cansancio posterior | 0 / 0,1 / 0,9 | 2,0 | 1 | Igual que el coco | EXISTENTE (código) | 0 |
| 5 | Pelícano taxi (`PelicanTaxi`) | Te lleva volando ~120 m por delante (22 m/s) y te suelta de pie; invulnerable durante el vuelo | 0 / 0,25 / 2,6 | 1,6 | 1 | Servidor; el plan se replica **una vez** en un struct; vuelo por fórmula del reloj del servidor; máx. 8, uno por tortuga; deriva de `ATN_BeachEnemy` | EXISTENTE (código; ave = borrador IA pendiente del inventario, no bloquea) · sintetizado | 0 |
| 6 | Protector solar (`Sunscreen`) | 8 s invulnerable, +25 % velocidad; derriba tortugas 2 s (2,5 s de respiro por víctima) y marea 4 s a enemigos | 0 / 0,5 / 2,0 | 1,4 | 1 | Servidor; estado en `UTN_RaceItemComponent` (`IsInvulnerable` legible en todas las máquinas) | EXISTENTE (código) | 0 |
| 7 | Cangrejo teledirigido (`HomingCrab`) | Persigue a la tortuga más cercana **por delante** (900→1600 cm/s), derriba 2,2 s; sin tortuga, marea al enemigo más cercano; vive 12 s | 0,7 / 1,5 / 1,0 | 1,0 | 1 | Servidor (`ATN_RaceItemActor`, siempre relevante, `Track` a 20-30 Hz suavizado); máx. 10 | EXISTENTE (código) | 0 |
| 8 | Gaviota justiciera (`GullStrike`) | Sombra que sigue a **la líder** 1,7 s y cagada que derriba 2,6 s (se esquiva con plancha o girando); solo si la líder va por delante | 0 / 0,1 / 1,1 | 1,0 | 2 | Servidor; máx. 3 | EXISTENTE (código; comparte gaviota con `GullZone`) | 0 |
| 9 | Mina de arena (`SandMine`) | Lanzable; se arma a los 0,9 s; aturde en bola 3 s a <5,5 m y marea 5 s a enemigos; explota sola a los 10 s | 1,8 / 1,2 / 0,5 | 0,8 | 1 | Servidor; máx. 12 | EXISTENTE (código) | 0 |
| 10 | Nube de tormenta (`StormCloud`) | Rayo sobre cada otra tortuga tras 1,1 s de aviso; aturde en bola 2,2 s; con protector no hace nada | 0 / 0,2 / 1,3 | 1,0 | 2 | Servidor; máx. 2 | EXISTENTE (código) | 0 |
| 11 | Disco volador (`Frisbee`) | Arco de 26 m que vuelve a la mano (2,9 s), derriba 1,9 s (una vez por pasada) y marea 4 s a enemigos | 1,4 / 1,3 / 0,9 | 1,0 | 1 | Servidor; máx. 6 | EXISTENTE (código) | 0 |
| 12 | Silbato del sargento (`Whistle`) | Marea 5 s a todos los enemigos a <55 m | 1,0 / 1,0 / 0,8 | 0,6 | 1 | Servidor; onda solo cosmética local | EXISTENTE (código) | 0 |

Objetos de siempre de `DT_Items` que también salen en la carrera (por su uso; no cambian): energía sin fin 1,0/1,5/1,8 (×2,0), barra llena 1,4/1,2/1,0 (×1,6), bola lanzable y tinta de pulpo 1,4/1,3/0,9 (×1,2), concha trampa 1,6/1,0/0,4 (×0,6), cabezota 0,3 fijo (×0). El tótem no sale.

**Reglas comunes**: los usos imposibles (aturdida, en caparazón, en el pico, en brazos, volando en el pelícano, carrera parada, sin objetivo, demasiados a la vez) suenan «nop» y el objeto se queda. El servidor decide todo; los clientes ven los efectos. Inventario: 1 objeto en mano + 1 en el caparazón.

**Balance a vigilar** (sin cambios de números en el MVP, ver P3 de §12): a 8 jugadores el `Norm` de la líder es siempre 0 → la líder nunca recibe pelícano, protector, dorado ni gaviota; correcto. La suma de pesos de la última puesta (≈ 13,9) frente a la de la primera (≈ 8,8) da ~1,6 veces más remontada; objetivo medido en §9.

### 5.2 Propuestas de objetos nuevos (baratos, sin arte nuevo)

Criterio: aportan una decisión que hoy falta (el catálogo no tiene defensa **pasiva de posición** ni «atajo elegido» y depende demasiado de lanzar). Cada uno reutiliza malla, sonido y VFX existentes: **0 assets nuevos**.

| # | Objeto propuesto | Efecto | Pesos (1.ª / medias / última) | Mín. | Autoridad y réplica | Reutiliza | Nuevos | Alcance |
|---|---|---|---|---|---|---|---|---|
| N1 | **Trampolín de bolsillo** (`PocketTrampoline`) | Suelta un trampolín 6 m por delante de quien lo usa; lanza 22 m hacia el mar a la primera que lo pise (incluida la dueña tras 1 s). Dura 12 s. | 0,2 / 1,0 / 1,6 | 1 | Servidor crea `ATN_BeachTrampoline` con un flag de vida limitada; máx. 4 en el mundo; el actor ya se replica | `ATN_BeachTrampoline`, mallas de `TNBeachProp` | 0 | **MVP** |
| N2 | **Escudo de concha** (`ShellGuard`) | Absorbe **un** aturdimiento o derribo en los siguientes 10 s (un golpe, no invulnerabilidad); se rompe con «cling» | 1,6 / 1,0 / 0,3 | 1 | Servidor; contador de 1 carga en `UTN_RaceItemComponent`, replicado como bool; respeta `IsInvulnerable` | Concha de `DecorShell`, sonido de `TN_ShellImpactSynth` | 0 | **MVP** |
| N3 | **Cubo tapón** (`SandBucketTrap`) | Deja detrás un cubo volcado que envuelve a la primera tortuga que lo pise (efecto del alga durante 2 s) | 1,2 / 1,0 / 0,4 | 1 | Servidor; reutiliza el efecto de `ATN_BeachSeaweed`; máx. 6 | `ToyBucket`, `BrokenBucket` | 0 | Después |

Por qué N1 y N2: N1 da a las últimas una segunda herramienta de remontada distinta del pelícano (más barata y con decisión de lugar); N2 da a la líder un contrajuego que no depende de tener que lanzar. Con ellos, cada posición tiene al menos 5 objetos con peso ≥ 1,0.

Aceptación de las propuestas: mismo patrón que el resto (sintetizador, icono generado, comando `TN.Race.ItemUse`).

---

## 6. Interfaz y efectos (todo reutilizado)

Según `Modos-UI-FX-2026-09-29.md` §2.2: **no hay pantalla ni efecto nuevo**.

| Aspecto | Reutiliza | Cambio de este documento |
|---|---|---|
| HUD de pista | `UTN_RunHUDWidget` (playa→mar), `UTN_RaceRoundClockWidget`, `UTN_RaceRoundIntroWidget`, `UTN_RaceSprintWidget`, `UTN_RaceFinishCountdownWidget` | Ninguno. Los tramos de autor no añaden HUD |
| Marcadores | Tortugas de colores en la pista (hasta 7 compañeras) | Ninguno |
| Aviso de atacante | Aviso de aturdimiento existente | **MVP (pequeño)**: al ser golpeada por un objeto, mostrar el color/cara de quien lo lanzó 1,5 s (dato ya en el servidor; hay que replicarlo en el struct de aturdimiento). 2-3 h |
| Fin de ronda | `UTN_RaceArrivalWidget`, `UTN_RaceTallyWidget`, `UTN_RaceChampionWidget`, `ATN_RacePodiumStage` | Ninguno |
| VFX | `UTN_BeachFinishSplashSubsystem`, `UTN_TurtleDustComponent`, aturdimiento/pajaritos, `ATN_RaceBurstFX` | Escudo de concha: «cling» = `ATN_RaceBurstFX` con estrellas ya existentes |
| SFX | `UTN_RaceCueSynthComponent`, `UTN_RaceItemSynthComponent` (21 sonidos), `UTN_BeachSplashSynthComponent`, `UTN_BeachTrapSynthComponent` | Añadir 2 sonidos sintetizados en C++ para N1 y N2 (sin archivos) |
| Accesibilidad | Textos y avisos del HUD existente | Los avisos de peligro no dependen solo del color (sombra + sonido ya presentes) |

Enseñanza: el objeto en mano tiene icono y nombre (`DisplayName`); el primer uso de cada objeto muestra un tooltip corto de una línea, una sola vez por perfil (**Después**, 3 h).

---

## 7. Backend y red

### 7.1 Sincronización de los tramos de autor

Principio: **el servidor decide, el cliente recibe la colocación ya resuelta**. Nada de RNG duplicado que pueda diverger.

`FTNBeachRoundNet` (`TN_BeachRaceGenerator.h:30-61`) gana:

| Campo | Tipo | Bytes | Uso |
|---|---|---|---|
| `CourseId` | FName/hash 16 bit | 2 | Curso usado |
| `CourseHash` | uint32 | 4 | Detecta versiones o parches distintos |
| `Placements[]` | `{StretchIndex uint8, X int16 (cuantizado a 10 cm), bMirror bool}` × ≤ 4 | ≤ 16 | Tramos ya resueltos |

Con esto el cliente rehace `PlaceAuthored()` sin decisiones propias. Si el `CourseHash` local no coincide, se registra el error y `UTN_BeachRoundSyncComponent` avisa (mensaje en pantalla al anfitrión y al cliente; no se cierra la sesión). Los elementos de juego de autor van por `SpawnElement` (replicados como siempre); el decorado va al `ATN_BeachDecorField` local. El coste de red añadido por ronda es ~22 bytes.

Autoridad de cada cosa:

| Cosa | Autoridad | Réplica |
|---|---|---|
| Semilla, dificultad, curso, tramos elegidos | Servidor | `FTNBeachRoundNet` (una vez por ronda) |
| Decorado sin reglas | Cliente (local, determinista) | Ninguna |
| Elementos con reglas y enemigos | Servidor | Actores replicados |
| Uso de objetos, golpes, aturdimientos | Servidor | RPC de uso, estado en componente |
| Puesto y pesos de objetos | Servidor (`GetRank`) | Nada (solo el resultado) |

### 7.2 Bugs pendientes del informe E y su arreglo

| # | Bug | Arreglo | Aceptación | h |
|---|---|---|---|---|
| D1 | El sprint borra el castillo principal (`ClearElementsAround`) | `GenerateRound(..., bSprint)` reserva el nido y aparta el castillo de `[SprintX − 80, SprintX + 30]`. Se retira `ClearElementsAround` | Castillo intacto y sin conchas flotantes tras el sprint en las 72 semillas del dorado | 5-7 |
| D2 | `Remove` no deshace `Reserved` ni `Interest` (arcos fantasma, botín de cosas que ya no están) | `OwnerItem` en `Reserved` e `Interest`; `Remove` los libera; `Finish` filtra huérfanos | 0 reservas o intereses huérfanos en el test | 6-8 (con D3) |
| D3 | Trampolín ante plataforma o castillo enorme sobrevive a `RestorePassage` | El lanzador cae con su objetivo (mismo `OwnerItem`) | 0 lanzadores huérfanos | (incluido) |
| D4 | `EnsureCatapults` cambia trampolín (22 m) por catapulta (30 m) sin validar el arco ni actualizar `Interest.To` | `TryAddWithArc`, `Interest.To` actualizado; el test deja de excluir `Launcher` (`TN_BeachLayoutTest.cpp:715`) | Todas las catapultas con arco libre en 72 semillas | 2-3 |
| D5 | Ritmo predecible (filas fijas 11/35/62/78/88,5 %, lado cíclico, tema repetible) | Curva de intensidad por banda, sin posiciones fijas, sin repetir tema ni lado, respiro cada ~150 m (P4) | Métricas de ritmo del test de §9 | 10-14 |
| D6 | Rampa de densidad invertida (primer tercio 52 %, media 48 %) | Presupuesto de decorado alcanzable y creciente (P5) | Primer tercio ≥ 66 % | 4-6 |
| D7 | Ocupación medida por `Pos.X` | Ocupación por cápsula (P8) | Métrica nueva en el test | (en P8) |
| D8 | Trampas poco legibles (basura a 45 cm) | Halo 2,5-3 m; 6 m limpios delante de filas (P6) | Disco libre mayor ≥ 2,5 m alrededor de cada trampa | 6-8 |
| D9 | Un único RNG en cadena | Subflujo propio para lo autoral en el MVP; subflujos por pasada (P9) como commit aparte con nuevo dorado | Con curso vacío, salida idéntica bit a bit | 4-6 (P9) |
| D10 | Latente: ranuras de trampas destacadas ignoran `MaxT` | Comprobar `MaxT` en `L.h:3537` | Test de ranuras | 1 |
| P7 | Ayudas de la 1.ª banda desiguales entre huevos | En el centro o en espejo, con test de equidad para las **8** posiciones de salida | Diferencia de distancia a la primera ayuda ≤ 10 % entre huevos | 3-4 |

Orden de trabajo: T0 (test dorado) primero, luego D3/D2/D4, luego D1, luego el híbrido con salida idéntica, y P9 al final en un commit aparte.

### 7.3 Objetos: puntos de red que revisar

- N1 y N2 no añaden RPCs; usan `ServerUseEquippedItem`.
- La atribución del atacante (§6) añade 1 byte (índice de jugador) al struct de aturdimiento, replicado al dueño.

### 7.4 Herramienta de editor para Diseño (MVP mínimo)

- Nivel `LVL_BeachStretch_Workbench` con `PreviewRound`.
- `ATN_BeachStretchProxy` (solo editor): `Element`, `SizeScale`, `Extent`, `Seed` editables, con la malla del mismo `TNBeachDecorKit` (hoy `Spec` no se puede editar, `TN_BeachElement.h:99`).
- `ATN_BeachStretchVolume`: caja, ancla, espejo y botones `CallInEditor` *Export*, *Import*, *Validate*.
- *Validate* comprueba límites, terreno, asiento, arcos, paso del tramo aislado y dibuja las huellas.
- Test de automatización sobre todos los tramos, ejecutable sin editor (`-game` headless).

---

## 8. Optimización

Presupuesto de partida ([E]): ~20 ms de 150 de margen en la generación; ~3100 elementos por ronda, 80 % decorado local.

| Área | Regla | Valor |
|---|---|---|
| Decorado | HISM por tipo en `ATN_BeachDecorField`; los tramos de autor usan el mismo kit y batch (`BatchFor`) | Cero actores nuevos por decorado de autor |
| Decorado lejano | Mantener el corte de distancia actual; el vaivén de algas solo cerca de la cámara (ya hecho, `66c8a93ad`) | — |
| Tramos | Un tramo aporta ≤ 250 elementos de juego y ≤ 1200 de decorado | Con 4 tramos, ≤ 1000 + 4800 → dentro del techo de 5000 previsto para 1200 m; se mide |
| Tick | Ningún objeto nuevo hace Tick propio si no está activo. Objetos lanzados: `Track` a 20-30 Hz. Máximos globales ya definidos (10 cangrejos, 12 minas, 6 discos, 3 gaviotas, 2 nubes, 8 taxis) | + 4 trampolines, 1 escudo por tortuga |
| Generación | En el hilo de juego solo la conversión del asset a POD; el reparto sigue sin UObjects | Coste añadido < 5 ms |
| Red con 8 jugadores | Tortuga 30/10 Hz; PlayerState 5/1 Hz; enemigos relevantes a 200-300 m; conchas dormidas | Estimado 5-8 KB/s por cliente (por medir con `stat net`); añadido de los tramos: ~22 B por ronda |
| Memoria | Tramos referenciados por soft pointer; el curso carga solo los del pool activo | ≤ 30 KB por tramo |

Objetivos medibles: generación de ronda ≤ 170 ms en el equipo de referencia; 60 fps en 1080p con 8 tortugas; ancho de banda de subida del anfitrión ≤ 60 KB/s sin voz.

---

## 9. Pruebas y aceptación medible

Pruebas automáticas (ampliación de `TN_BeachLayoutTest`, `TN_BeachLayoutTest.cpp:586-850`):

| Id | Prueba | Umbral |
|---|---|---|
| T0 | **Dorado**: 24 semillas × 3 dificultades, captura el reparto actual antes de tocar nada | Con curso vacío, salida idéntica bit a bit tras todos los cambios (salvo P9, que regenera el dorado en su commit) |
| A1 | Sprint conserva el castillo principal | 72/72 semillas con castillo intacto (sin conchas flotando) |
| A2 | Lanzadores huérfanos | 0 en 72 semillas |
| A3 | Arco de catapulta y trampolín libre | 100 % (el test ya no excluye `Launcher`) |
| A4 | Cobertura del primer tercio | ≥ 66 % (hoy 52 %) |
| A5 | Ritmo | Ninguna ventana de 200 m con ≥ 3 barreras de ancho completo; ningún hueco > 180 m sin fila; tema y lado no repetidos en dos filas seguidas |
| A6 | Equidad | Diferencia ≤ 10 % en la distancia a la primera ayuda desde cada uno de los 8 huevos |
| A7 | Tramos | Todos los `.uasset` de tramo pasan *Validate*; cada uno, aislado, deja paso |
| A8 | Curso por defecto | En 72 semillas: 2-4 tramos de autor colocados; cobertura de autor ≤ 25 %; enfriamiento respetado (no se repite ningún tramo en dos rondas seguidas con pool ≥ 3) |
| A9 | Sprint con autoral | Ningún tramo no `bSprintSafe` cae en `[SprintX − 80, SprintX + 30]` |
| A10 | Relieve | Hash del heightfield idéntico al de la rama base |
| A11 | Red | Con 2 y 8 instancias, servidor y clientes obtienen el mismo `ContentHash` y colocaciones iguales; hash distinto → aviso en registro |
| A12 | Objetos | Sorteo 10 000 veces por puesto: frecuencias dentro de ±5 % de los pesos; ningún objeto con peso 0 en su puesto sale |
| A13 | Regresión de torbellino | Segunda gaviota + caparazón (procedimiento de `Modo_Carrera.md` línea 1268): 0 bucles |

Pruebas manuales (PIE 4P listen y build empaquetada):

1. Ronda completa con 8 jugadores (o 8 instancias headless): sin desincronizaciones visibles, los tramos aparecen en la misma X para todos.
2. Sprint: sin castillo roto y con la ventana despejada.
3. Cada objeto (12 + 2 nuevos) con `TN.Race.ItemUse` en solitario y con anfitrión + cliente (igual que la lista de «Probar» del documento de carrera).
4. Ritmo: 3 rondas sin repetir un tramo; medir tiempos de ronda (objetivo mediana 3:00-4:00; ningún tope de 9 min agotado).

Criterio global de cierre del MVP: A0-A12 verdes, 0 CRITICAL/HIGH abiertos, y partida completa a 3 conchas en ≤ 25 min con 4 jugadores.

---

## 10. Presupuesto de assets

| Concepto | Estado | Nuevos imprescindibles |
|---|---|---|
| Relieve | Existente, no se toca | 0 |
| Decorado y elementos de playa | Existente (`ETNBeachElement`, `TNBeachDecorKit`) | 0 |
| Tramos de autor (DataAssets) | Contenido de Diseño; no son assets de arte | 0 |
| Mallas de los 12 objetos | Existentes (código) | 0 |
| Iconos 128² | Existentes (código) | 0 |
| SFX de los objetos | Sintetizados (`TN_RaceItemSynth`) | 0 |
| VFX | Reutilizados | 0 |
| UI | Sin pantallas nuevas | 0 |
| Objetos N1/N2 | Reutilizan trampolín y concha | 0 |
| Ave de pelícano/gaviota | Borrador IA pendiente en el inventario (una malla sirve a 4 implementaciones); **no bloquea** | 0 (opcional) |
| Mallas propias de tramo | Fase 2 (T8) | **Deseable** |
| Tooltip de primer uso | Texto | 0 |

**TOTAL de assets nuevos imprescindibles del modo Carrera: 0.**

Deseables (no bloquean): mallas propias de tramo por `BatchFor` (T8), ave compartida de gaviota/pelícano del inventario de arte. Los 11 imprescindibles del plan maestro (Rally 1, TcT 7, Steam 3) no incluyen ninguno de este modo.

---

## 11. Estimación en horas

| Bloque | Contenido | h |
|---|---|---|
| T0 | Test dorado | 2 |
| T1 | D3/D2, D4, D1 (P1-P3) | 13-18 |
| T2 | `UTN_BeachStretchAsset`, `UTN_BeachRaceCourseAsset`, POD | 6-8 |
| T3 | `PlaceAuthored`, intervalos, rol `Authored`, pasadas fijas y máscara | 12-16 |
| T4 | Red, selección con enfriamiento y hash | 6-8 |
| T5 | Editor: proxy, volumen, Export/Import/Validate | 16-22 |
| T6 | Tests de tramos, curso fijo y sprint | 6-8 |
| **Subtotal T0-T6 (mínimo útil)** | | **61-82** |
| T7 | Pulido P4-P8 (ritmo, densidad, halo, equidad, métricas) | 29-40 |
| T9 | P9 y nuevo dorado | 4-6 |
| Objetos N1 + N2 | Actores, sonidos, pesos, pruebas | 14-18 |
| Atribución del atacante | Struct + HUD | 2-3 |
| Contenido | 6 tramos del primer lote con el editor (≈ 2 h cada uno) | 12 |
| **Total MVP + pulido** | | **122-161** |
| T8 (Después) | Mallas y BP propios de tramo | 14-20 |
| N3 y tooltip (Después) | Cubo tapón y ayuda de primer uso | 6-9 |
| **Total con todo** | | **142-190** |

Riesgos que mueven la estimación: pasadas de ancla fija que no encuentran sitio (mitigado por parametrizar los castillos por curso); vista previa distinta del juego si el proxy no usa el mismo kit; conflictos con `main` (Alvaro2rh toca assets: los `.uasset` de tramo van uno por fichero).

---

## 12. Preguntas abiertas y decisiones para el director

| # | Pregunta | Recomendación |
|---|---|---|
| P1 | ¿Contenido de los 6 tramos lo escribe Diseño o lo hago yo con la herramienta? | Diseño (usa el editor T5); esta especificación solo fija formato y catálogo inicial |
| P2 | La duración de la partida con 8 jugadores y 3 conchas puede superar 25 min | Medir en el primer playtest; si pasa, bajar a 2 conchas con 6+ jugadores (una línea de configuración) |
| P3 | ¿Aceptar N1 y N2 en el MVP? Añaden 14-18 h | Sí: sin ellos la líder no tiene contrajuego pasivo |
| P4 | Las conchas de puntos, ¿deben influir en algo más que el desempate de posición dentro del recuento? La fuente no lo deja claro | Mantener como está; confirmar |
| P5 | `Course_Beach_Fixed` (carrera 100 % de autor) para torneos: ¿se ofrece al jugador? | Después; solo como opción de anfitrión |
| P6 | La huella de 8 huevos: el test del reparto solo cubre la fila de 4 | Ampliar en A6 (ya incluido) |

Resumen de riesgos: alto = red de los tramos (mitigado por hash y colocación ya resuelta), medio = tiempos de ronda con 8 jugadores, bajo = objetos nuevos.

## Anexo (2026-09-29): cómo aparecen los objetos

1. **Filas y cajas en la pista** (lo ya especificado: 20–25 sueltas + 3 filas): al cogerlas **reaparecen a los 5 s** en el mismo sitio, para que el pelotón de detrás también tenga objeto (estilo Mario Kart). Las sueltas de fuera del camino ideal reaparecen a los 15 s.
2. **Caja de remontada del cielo**: cada 30–40 s una gaviota existente suelta una caja 20–40 m por delante de las 2 últimas tortugas (la del último puesto tiene prioridad), con aviso de sombra de 2 s. Solo puede abrirla quien vaya en la mitad de atrás; para el resto es un obstáculo que se puede empujar. Misma malla de caja, sin assets nuevos.
3. **Cofres y rebuscables** (existentes): fijos por tramo; no reaparecen dentro de la misma ronda.

Todo lo decide el servidor (posición, reaparición y contenido con `RollLoot` por posición).
