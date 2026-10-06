# Modo Cooperativo: especificación completa (base común de Coop, 2 vs 2 y Carrera)

Fecha: 2026-09-29 · Rama: `macro-update` · Estado: diseño (sin código) · Autor: diseño de juego (Claude).
Fuentes: `Docs/2026-09-29-Plan-Maestro-Modos-y-Mapas.md` (§2, §3.1, §3.5, §3.6, §7), `Docs/Catalogo-Puzzles-2026-09-29.md`, `Docs/Modos-UI-FX-2026-09-29.md`, `Docs/superpowers/specs/2026-09-29-foto-a-mapa-design.md`, `Docs/superpowers/plans/2026-09-29-F0-F2-Coop-Steam.md`, `Docs/Inventario-Objetos-Arte-2026-09-29.md`, `Art/Library/IA/INDEX.md` y las cabeceras de las clases de `Source/Tortunabo/Public`.

## 0. Cómo leer este documento

**Este documento es la BASE COMÚN.** El director aclara que 2 vs 2 y Coop son casi el mismo modo y que la Carrera es casi lo mismo con mapa recto, sin puzzles, caótico y rápido. Los documentos de 2 vs 2 y de Carrera escriben solo sus diferencias respecto a este. Por eso:

- Cada pieza lleva una marca de alcance: **[COMÚN]** = la usan Coop, 2 vs 2 y Carrera; **[COMÚN 2v2]** = Coop y 2 vs 2 (puzzles); **[COOP]** = solo Coop.
- Los identificadores son estables y otros documentos pueden citarlos: `E-nn` enemigos, `H-nn` peligros, `O-nn` objetos, `U-nn` interfaz, `F-nn` efectos, `N-nn` red, `T-nn` pruebas, `A-nn` assets, `R-nn` reglas.
- Los números de sección son estables (§1 a §12, los del encargo). Los apartados «comunes» van primero y los «solo Coop», después.

| Sección | Común | Solo Coop |
|---|---|---|
| §1 Fantasía y reglas | Estados de la tortuga, rescate, reglas de contacto (R-10 a R-14) | Victoria, derrota, vidas de equipo, duración |
| §2 Flujo | Lobby, viaje, carga, resultados | Progresión y recompensas |
| §3 Mapas | Reglas de diseño medibles (§3.4), estructura de tramos | Lista de mapas Coop, CP01 |
| §4 Enemigos y peligros | Toda la tabla (§4.1, §4.2) | Reglas de escala por jugadoras y enemigo guardián (§4.3) |
| §5 Objetos | §5.1 a §5.3 | §5.4 |
| §6 Puzzles | Piezas y escala | Plantillas de grupo |
| §7 Interfaz y efectos | §7.1 | §7.2 |
| §8 Red | §8.1 a §8.3 | §8.4 |
| §9 Optimización | Presupuestos y mediciones | Valores de CP01 |
| §10 Pruebas | Pruebas comunes | Pruebas de reglas Coop |
| §11 Assets | Toda la tabla | — |
| §12 Horas | Reparto común / Coop | — |

**Decisiones del director que este documento aplica** (2026-09-29): mapas volumétricos FIJOS por el método camino (`C01_camino`; CP01 propio de ~600 m), hechos a mano o con Foto→Mapa y editables; enemigos Y puzzles en los que coopera todo el grupo; mecánicas de la carrera (catapulta, tanque, quads, trampolín, gaviotas) y vegetación `ATN_BeachDecorField` en el Coop; protección fuera de zona; doble salto que lanza al agarrado, desliza en pendiente y se estampa (bola física); agarre solo a quien está en caparazón; todo sincronizado con servidor con autoridad; **minimizar assets nuevos**.

## 1. Fantasía y objetivo

### 1.1 Experiencia buscada

«Una tropa de crías de tortuga cruza una costa gigante y hostil, hacia el mar, y nadie se queda atrás.» La emoción principal es **resolver juntas**: el grupo se reparte papeles (una hace de bola, otra lanza, otra aguanta la placa) y se ríe del fallo. El castigo por fallar es perder tiempo y vidas de equipo, nunca ser expulsada de la partida.

Cómo se enseña: (1) el primer puzzle de cada tipo lleva un cartel de pictogramas que se apaga tras verse una vez (`TN_TutorialSaveGame`); (2) los prompts de `TN_InteractPromptWidget`; (3) el tramo 0 de todo mapa es un puzzle de dificultad 1 en el que solo se puede fallar sin consecuencias (sin vidas gastadas); (4) los enemigos se presentan de uno en uno, con una zona de aviso, antes de aparecer en pareja.

### 1.2 Jugadoras, duración y dificultad

| Parámetro | Valor (1.ª pasada) | Motivo |
|---|---|---|
| Jugadoras | **1 a 4** (`MaxPlayers` del Coop = 4) | Decisión del director. **Contradicción**: el catálogo de puzzles habla de «3 a 8» y `Modos-UI-FX` §2.1 pone «Coop 1 a 8»; se rebaja a 4 y se corrigen ambos documentos (R-01). |
| Duración objetivo | **12 a 18 min** en CP01 (mediana de un grupo de 3 que no conoce el mapa: 16 min; que ya lo conoce: 11 min) | Camino de ~1 000 m a 6 m/s son ~3 min de marcha pura; 6 puzzles de grupo ×60 s, 3 tramos de parkour ×20 s, 1 sala de pensar ×75 s y los encuentros suman el resto |
| Rondas | 1 (`CoopRounds = 1`) | Ya es el valor por defecto de `ATN_ProcMapGameMode` |
| Dificultad | Fácil / Normal / Difícil con el selector existente (`ETNProcDifficulty`) | Tabla en §1.4 |
| Límite de tiempo | Ninguno (no hay derrota por tiempo). El tiempo solo pesa en el bonus de recompensa | Un puzzle de grupo no debe hacerse con prisa; la presión viene de los enemigos y las vidas |
| Tormenta del camino (`ATN_PathStorm`) | Apagada por defecto; opción del mapa (`storm_speed`) para mapas «de huida» | El Coop procedural la usaba (`StormSpeed`); en mapas de puzzles fijos contradice el diseño |

### 1.3 Victoria, derrota y revivir

**R-02 Victoria.** La ronda termina en victoria cuando **todas las jugadoras conectadas han cruzado el volumen de meta** (`ATN_ProcFinishVolume`, `bHasFinishedRun`). Para no bloquear al grupo:

- Cuando cruza la primera, empieza una cuenta atrás de **60 s** (`UTN_RaceFinishCountdownWidget`, ya existe) para las demás. Al acabar, las rezagadas se teletransportan a la meta y cuentan como «rezagada» (recompensa ×0,5).
- Una jugadora en DBNO o muerta no bloquea: la cuenta la incluye igual y, si se agota, se la teletransporta.
- Una jugadora desconectada se saca de la lista de «conectadas» (los umbrales se recalculan, ver R-08).

**R-03 Vidas de equipo (nuevo, `TeamLives`).** Reserva compartida:

| Jugadoras (N) | Vidas iniciales | Motivo |
|---|---|---|
| 1 | 5 | En solitario cada caída es una vida; sin nadie que reviva |
| 2 | 7 | 3 + 2N |
| 3 | 9 | 3 + 2N |
| 4 | 11 | 3 + 2N |

- Una vida se gasta cuando una jugadora **muere de forma definitiva**: pasa el tiempo de DBNO sin que la revivan, o muere en un sitio sin DBNO posible (agua, barranco, remolino). El rescate por fuera de zona NO gasta vida (§8.3).
- Revivir a una compañera en DBNO **no gasta** vida.
- Al resolver cada puzzle de grupo se recupera **1 vida** (tope: las iniciales). Motivo: mapas de 16 min no deben acabar por acumulación de errores del tramo 1.
- Fácil: +3 vidas. Difícil: −2 vidas y no se regeneran. (Tabla §1.4.)

**R-04 Derrota.** Con `TeamLives == 0` y una muerte más, la ronda termina en derrota. Pantalla de resultados con % de camino recorrido. El anfitrión elige: **Continuar desde el último nido** (vidas restauradas, recompensa base −50 %, sin bonus) o **Volver al lobby**. Así la derrota es una pausa con coste, no un muro. `Attempts` se replica y se muestra en resultados.

**R-05 Estados de la jugadora.**

| Estado | Cómo se llega | Qué puede hacer | Salida |
|---|---|---|---|
| Activa | Por defecto | Todo | — |
| Aturdida / derribada | Golpe de enemigo o trampa (`TNBeach::StunTurtle`, `KnockDownTurtle`) | Nada durante 1 a 3 s; suelta lo que agarra | Sola; **no baja a DBNO ni cuesta vida** |
| Caída (DBNO) | Muerte por un peligro letal **en suelo permitido** (§4.2) | Arrastrarse; hacer emote; ver a las compañeras | Revivida por compañera, o muerte a los `DBNOBleedoutSeconds` |
| Muerta | DBNO agotado, o muerte sin DBNO posible (agua, barranco) | Espectador (`ATN_SpectatorGhost`) 2,5 s | Reaparece en el último nido alcanzado por el equipo (`TeamBestNest`) tras `RespawnDelaySeconds` (2,5 s) y consume una vida |
| Inmune | 2 s tras revivir o reaparecer (`ReviveImmunitySeconds`) | Todo; parpadeo del cuerpo | Fin del temporizador |

**R-06 Revivir.** Mantener **Interactuar 1,5 s** a ≤ 3 m de una compañera en DBNO (`UTN_HoldRingWidget` visible para las dos). El servidor valida la distancia y el estado. Revive con el 50 % de energía y 2 s de inmunidad. Dos compañeras a la vez reducen a 0,8 s. Valores de DBNO: `DBNOBleedoutSeconds` pasa de 8 s (`TN_RunGameMode.h:141`) a **12 s** en Coop (Normal), 16 s en Fácil, 8 s en Difícil. El revivir por *emote en rango* que existe hoy en `ATN_RunGameMode` se sustituye por este (pregunta abierta Q3). En solitario no hay DBNO: la muerte es directa.

**R-07 Peluche tótem.** El `BP_TotemInteractable` (`TryTotemAutoRevive`) se conserva como objeto de mapa: quien lo lleva se revive sola una vez (sin gastar vida). Ver O-13.

**R-08 Entrada y salida durante la partida.** Entrada tardía: aparece en el último nido alcanzado (`TeamBestNest`) con las vidas actuales y el estado de todos los puzzles (`PuzzleSolvedMask`). Salida: se recalculan los umbrales de los puzzles con las jugadoras que quedan (`TriggerThreshold = -1` ya significa «todas las vivas»). Si queda una sola, se activan las variantes en solitario (§6.3) **en el siguiente puzzle**, nunca a mitad de uno.

**R-09 Si el anfitrión se marcha**: la partida acaba y las clientas vuelven al menú (`OnTravelFailure`, tarea T3 del plan F0-F2). No hay migración de anfitrión.

### 1.4 Dificultad

| Parámetro | Fácil | Normal | Difícil |
|---|---|---|---|
| Vidas de equipo | 3 + 2N + 3 | 3 + 2N | 3 + 2N − 2, sin regeneración |
| DBNO (s) | 16 | 12 | 8 |
| Densidad de enemigos | ×0,7 | ×1,0 | ×1,3 |
| Aturdimiento de trampas y enemigos | ×0,7 | ×1,0 | ×1,2 |
| Ventanas de puzzle (palanca, puertas) | ×1,3 | ×1,0 | ×0,85 |
| Objetos por caja | pesos hacia utilidad | pesos base | pesos base sin Whistle |

Todos son propiedades editables (`EditDefaultsOnly`) del GameMode y `FTNCoopDifficultyRow`; primera pasada, se ajustan en el primer playtest de 4.

### 1.5 Reglas de contacto entre compañeras [COMÚN 2v2]

El 2 vs 2 sobrescribe las que dice su documento (allí sí se agarra y lanza a rivales). En Coop:

| Id | Regla |
|---|---|
| R-10 | **Agarre solo a quien está en caparazón** (decisión del director). Se puede agarrar a compañeras; a un enemigo no |
| R-11 | **Fuego amistoso apagado**: bola, empujón, lanzamiento y estampado entre compañeras no derriban ni aturden por encima de 0,5 s. Los ítems ofensivos (mina, disco, cangrejo) ignoran a las jugadoras. Se implementa como un filtro `TNCoop::IsFriendly(A, B)` |
| R-12 | **Doble salto (dive)**: lanza al agarrado, desliza en pendiente y se estampa contra la pared con la bola física replicada (`StartBody`), plan maestro §3.5. En Coop el estampado entre compañeras es cosmético (polvo + pajaritos 0,5 s); contra un enemigo aturde 2 s a este |
| R-13 | **Escape del agarre**: machacar salto (existente). Una compañera agarrada puede quedarse quieta sin coste |
| R-14 | **Protección fuera de zona**: ver §8.3. Una bola lanzada que sale de zona se rescata como cualquier pawn |

## 2. Flujo de partida y progresión

### 2.1 Flujo (común)

| Paso | Qué ocurre | Clases | Red |
|---|---|---|---|
| 1. Lobby (`LVL_HQ`) | El anfitrión usa el selector de modo (Coop) y el de mapa (`ETNProcSelectorKind::Map`, «Al azar» primero) y la dificultad. La etiqueta 3D pone «Necesita 1 a 4» y se pone en rojo si sobran | `ATN_ProcModeSelector`, `UMP_GameInstance` | Elección en la `UMP_GameInstance` del anfitrión; etiqueta replicada |
| 2. Inicio | El anfitrión interactúa con la puerta de salida. `ATN_HQGameMode` hace `ServerTravel` a `/Game/Maps/Coop/LVL_Coop_<id>` (`TravelURL`) | `ATN_HQGameMode` | `OnTravelFailure` con vuelta al lobby (T3) |
| 3. Carga | Pantalla de carga que reconoce el Coop por GameMode (R8 del plan). Muestra id, nombre, duración estimada, consejos. Vista previa: deseable | `UTN_LoadingScreenSubsystem` | El servidor espera a que todas las clientas confirmen `IsMapReady` (`MapReadyTimeoutSeconds` 30 s) |
| 4. Comprobación | `PreparedMapId` debe coincidir en `OnRep`; si no, error en log y vuelta al lobby | `FTNProcMapNetConfig` | Replicado una vez |
| 5. Staging | Las jugadoras nacen en las pilas de huevos del nido 0 (`ATN_ProcEggNest`) con la estructura de salida cerrada. Cuenta «3, 2, 1, ¡ADELANTE!» (`StartStructureOpenDelaySeconds` 1,2 s) | `ATN_ProcStartStructure` | Servidor |
| 6. Tramos | Se recorre el mapa (§3). Cada nido activa el checkpoint del equipo (`NotifyEggNestReached`, `TeamBestNest` = el mayor) | `ATN_ProcMapGameMode` | `TeamBestNest` replicado |
| 7. Meta | R-02 | `ATN_ProcFinishVolume` | Servidor |
| 8. Resultados | Panel de `UTN_CoopFlowHUDWidget` (hasta 4 filas: nombre, tiempo, conchas, revivires, caídas) y `ATN_RacePodiumStage` si se quiere podio. Botones: Repetir, Cambiar mapa, Salir (solo anfitrión) | `UTN_CoopFlowHUDWidget` | `ATN_CoopPlayerState` |

### 2.2 Progresión y recompensas [COOP]

Moneda: **conchas** (`ATN_ScoreShells`, `RaceScore` del `ATN_CoopPlayerState`) que se gastan en la tienda (`ATN_ShopKeeper`) y en el probador. Los cosméticos son `DT_Skins`, `DT_Helmets` y las plantillas de caparazón (`ETNShellPattern`): **0 assets nuevos** (N skins = N filas).

| Concepto | Conchas (1.ª pasada) | Cómo se calcula |
|---|---|---|
| Recogidas en el mapa | 1 por concha (~60 a 80 en CP01) | `RaceScore` por jugadora; se recogen por separado |
| Completar el mapa | +20 | Todas en meta o teletransportadas |
| Bonus «Limpio» | +5 por puzzle | Puzzle de grupo resuelto sin `OnFailed` |
| Bonus «Nadie cae» | +10 | Ninguna vida gastada |
| Bonus «Sin prisa» / «Rápidos» | +10 | Tiempo < 12 min en CP01 (`estimated_minutes × 0,75`) |
| Rezagada | ×0,5 sobre el total | R-02 |
| Reviviste a una compañera | +3 por revive (tope 15) | `RevivesDone` |
| Derrota y continuar | Base −50 %, sin bonus | R-04 |

Estimación: un grupo normal cobra 80 a 130 conchas por mapa; un casco cuesta 150 a 400 (a alinear con la tienda actual, pregunta Q7).

Desbloqueos por progreso (todo con cosméticos existentes):

| Hito | Recompensa |
|---|---|
| 1.er mapa completado | Casco `Straw` |
| 3 mapas | Patrón de caparazón adicional (`ETNShellPattern`) |
| 5 mapas con «Nadie cae» | Casco `Halo` |
| Mapa completado en Difícil | Casco `Crown` |

**Autoridad**: el servidor calcula y concede; cada cliente guarda lo suyo en su `SaveGame`. **Dependencia**: los guardados versionados del plan Steam (auditoría 2026-08-18: «save frágil»); sin ellos, no se persiste (R-15).

## 3. Mapas

### 3.1 Lista de mapas Coop

| Id | Descripción | Origen | Estado | Tris. objetivo | Prioridad |
|---|---|---|---|---|---|
| **CP01_coop** | Mapa propio de 600 × 600 m, semilla `20260929`, camino de 900 a 1 200 m, 7 tramos, 2 bifurcaciones | `gen_terrain_path.solve` (`TN_PATH_GRID=6`), tarea T32 del plan | Planificado (F2) | ≤ 1,1 M | **MVP** |
| **C01_camino** | Mapa volumétrico de referencia (1,06 M tris) con la máscara/bake de T11 | Existente | Retocar puzzles y enemigos | 1,06 M | MVP (segundo mapa, sin coste de terreno) |
| **CP02_bahia** | Ejemplo del spec Foto→Mapa: 600 × 400 m, puente natural, túnel | Foto→Mapa (`MapSketch.json`, `mode: coop`) | Diseñar tras validar el flujo | ≤ 0,8 M | Posterior |
| **CP03…** | Mapas de autor a mano en el editor (Modeling Mode) sobre trozos ya importados | Modeladores | Bajo demanda | ≤ 1,1 M | Posterior |
| E01 / F01 | Países miniaturizados (Catálogo de mapas, decisión del director) | Rally | **No son mapas Coop** (plan §3.1 los citaba como candidatos; el Catálogo de mapas no incluye Coop) | — | Fuera |

Cada mapa es un **nivel `LVL_Coop_<id>` con subniveles** `_Terrain` (script), `_Markers` (el diseñador retoca) y `_Design` (no lo abre el script), un `DA_Coop_<id>` y una entrada del catálogo `DA_CoopMapCatalog`. `TN_REGENERATE=1` nunca mueve marcadores ni resucita los borrados (`ATN_CoopMapAnchor.KnownIds`). **Nada se genera en ejecución**: el nivel se edita a mano después de importar.

### 3.2 Estructura de un mapa

Un mapa se compone de **tramos** en serie a lo largo del camino principal (`Main`), con **ramales** opcionales.

| Elemento | Definición | Marcador (`ATN_CoopMarker`) |
|---|---|---|
| Camino principal | Serie de muestras ordenadas con progreso `S`, ancho ≥ 8 m | `path` del DA |
| Ramales | Camino alternativo que sale y vuelve a `Main` (bifurcación) | `path` con `kind: branch` |
| Tramo | Segmento con un tema y un evento principal; termina en un nido | `Zone` |
| Nido / checkpoint | Pila de huevos `ATN_ProcEggNest`; reaparición y curación | `Nest` |
| Puzzle | Plantilla de §6 expandida a sus piezas | `Puzzle` (`puzzle_id`, `kind`) |
| Encuentro | Grupo de enemigos de §4 con su zona de aviso | `BeachElement` con `Element = enemigo` |
| Mecánica de tránsito | Catapulta, trampolín, plataforma, géiser… (§5.2) | `BeachElement` |
| Vegetación | `ATN_BeachDecorField` con puntos del DA (`coop.vegetation.points`) | Campo |
| Botín | Caja de objetos, cofre, rebusca | `BeachElement` |
| Meta / salida | Volumen orientado por `Dir`, a la cota del marcador | `Finish`, `Start` |
| Kill boxes | Agua y barrancos | `kill_boxes_uu` |

**Bifurcaciones (R-16).** CP01 lleva 2. Cada una ofrece la **ruta A** (larga y segura: pasarelas, 1 encuentro de nivel 1, +25 % de longitud) y la **ruta B** (corta y peligrosa: parkour o 2 encuentros, 0 cofres). Todas las jugadoras de un grupo pueden tomar rutas distintas (no hay «voto»); los puzzles de grupo están siempre **en el tronco**, nunca en un ramal, para no dividir el grupo. Los ramales llevan cofre o caja de objetos como premio.

### 3.3 CP01: plano de tramos

Longitud de camino ≈ 1 050 m. Puzzles: 6 de grupo (una vez cada plantilla), 3 de parkour y 1 sala de pensar. Los kilómetros son aproximados y se fijan al generar (semilla `20260929`).

| Tramo | S (m) | Tema y bioma | Puzzle / evento | Enemigos | Mecánica de carrera | Nido |
|---|---|---|---|---|---|---|
| T0 Salida | 0 a 90 | Playa, huevos | `shell_gauntlet` (dif. 2; el primero, con cartel) | Lagarto huidizo (aviso) | Boardwalk | N0, N1 |
| T1 Dunas | 90 a 230 | Dunas y rocas | `throw_chain` (1 muro) | Erizo, Pulgas de arena | Trampolín | N2 |
| T2 Poza | 230 a 360 | Charcas y algas | `wobbly_run` (parkour) + `plate_balance` | Pulpo de poza, Cangrejo ermitaño en calle | Plataforma móvil sobre poza | N3 |
| T3 Cañón | 360 a 520 | Cañón y acantilado | `catapult_gap` + **Bifurcación 1** | Cangrejo gigante (guardián) | Catapulta | N4 |
| T4 Cala | 520 a 660 | Agua y arrecife | `platform_power` | Gaviotas (`GullZone`) | Plataforma ferri | N5 |
| T5 Talleres | 660 a 820 | Restos y basura | `basket_hold` + `breakable_chain` | Tanque de juguete, Quads | Paso de quads | N6 |
| T6 Faro | 820 a 960 | Roca y torre | `geyser_aim` + **Bifurcación 2** + `think_room` (variante `code`) | Gusano de arena, Cangrejo gigante | Géiser | N7 |
| Meta | 960 a 1 050 | Playa final, arco de neumático | Sin puzzle | Ninguno (descanso) | — | Meta |

Duración estimada por tramo (grupo de 3 que no conoce el mapa): T0 2 min, T1 2 min, T2 2,5 min, T3 2,5 min, T4 2 min, T5 2,5 min, T6 3 min, meta 0,5 min = **17 min** (dentro de 12 a 18).

### 3.4 Reglas de diseño medibles [COMÚN]

Las valida `Scripts/map_sketch/validate.py` (pytest) sobre el JSON y `Scripts/tests/test_cp01.py` sobre el manifest; las marcadas «PIE» se miden en partida.

| Id | Regla | Valor | Cómo se mide |
|---|---|---|---|
| D-01 | Longitud del camino principal | 900 a 1 200 m (Coop) | Manifest |
| D-02 | Ancho mínimo del camino principal / ramal | ≥ 8 m / ≥ 6 m | `path_dist_dm` |
| D-03 | Pendiente en muestras del camino principal | ≤ 25° en el 95 % (1.ª pasada; se verifica con `BellySlopeMinAngle` 12°) | Validador |
| D-04 | Distancia entre eventos (puzzle, encuentro, mecánica) | ≤ 150 m | Validador |
| D-05 | Nido cada 120 a 200 m y uno **antes** y otro **después** de cada puzzle de grupo | Obligatorio | Validador |
| D-06 | Puzzles de grupo por mapa | 5 a 7; el mismo `kind` no dos veces seguidas; los 6 kinds al menos una vez en CP01 | Validador |
| D-07 | Enemigos despiertos a la vez (relevantes a una jugadora) | ≤ 12 | PIE + contador |
| D-08 | Encuentros de enemigos | Uno cada 80 a 120 m; ninguno a < 15 m de piezas de puzzle salvo «puzzle bajo presión» (máx. 1 por mapa) | Validador |
| D-09 | Cajas de objetos / cofres / rebuscas | 1 cada 100 m / 2 por mapa / 8 a 12 por mapa | Validador |
| D-10 | Vegetación | ≤ 2 000 puntos; ninguna a < 4 m del eje del camino principal | `path_dist_dm` |
| D-11 | Terreno | ≤ 1,1 M triángulos tras decimar; error de decimado ≤ 15 uu | `preview` + manifest |
| D-12 | Todo el camino recorrible a pie o saltando de salida a meta | 100 % (BFS) | Validador |
| D-13 | Sin «soft-lock»: cada puzzle se reinicia con `Reset()` y tiene salida | Todos | Prueba T-09 |
| D-14 | Mecánicas de carrera por mapa | Catapulta ≥ 1, trampolín ≥ 1, plataforma móvil ≥ 1, `GullZone` ≥ 1, `QuadLane` ≤ 2, `ToyTank` ≤ 2 | Validador |
| D-15 | Rutas alternativas | Cada bifurcación: ruta B ≥ 20 % más corta que A | Validador |
| D-16 | Cota de elementos | Z = suelo caminable de `WalkableZAt` (no el techo de los túneles) | Marcadores |
| D-17 | Cada tipo de elemento usado existe en `ETNBeachElement` o es una clase de mundo existente; un tipo desconocido es error | 100 % | Esquema |

## 4. Enemigos y peligros

Convenciones: **Estado** EXISTENTE / EXISTENTE-AJUSTE (parámetros o filtros nuevos) / NUEVO. **Coste Coop** = horas de adaptación, primera pasada, sin depurar red. **Autoridad**: todos los enemigos deciden en el servidor (`ATN_BeachEnemy`); los movimientos deterministas se calculan en cada máquina con el reloj del servidor. En Coop **ningún enemigo mata directamente**: aturde o derriba; lo letal es el entorno (§4.2).

### 4.1 Enemigos [COMÚN]

| Id | Enemigo (clase) | Estado | Comportamiento (según la cabecera del código) | Solución de grupo (Coop) | Red | Coste Coop (h) |
|---|---|---|---|---|---|---|
| E-01 | Cangrejo gigante `ATN_BeachGiantCrab` | EXISTENTE | Patrulla (huella 25 m); mazazo de cerca (sombra de aviso 0,6 s), embestida a media distancia; mareado si se le lanza algo o con la concha trampa; la tinta lo ciega | Una lo distrae (persigue a la más cercana); otra le lanza un ítem (`ApplyHitStun`) y el grupo pasa | Estado y objetivo replicados; golpes en servidor | 2 |
| E-02 | Erizo de mar `ATN_BeachSeaUrchin` | EXISTENTE | Bola de púas lenta que rueda hacia la más cercana; pincha (derribo + mareo) | Se atrae a un extremo y las demás pasan; en bola se libra el pinchazo | Servidor | 1 |
| E-03 | Lagarto `ATN_BeachLizard` | EXISTENTE | Tres perfiles: huidizo (susto), generoso (deja premio), mordedor (agarra a quien va de pie y la lanza) | Quien va en caparazón se libra; el generoso da botín al grupo | Servidor; la agarrada la coloca cada máquina | 1 |
| E-04 | Paso de quads `ATN_BeachQuadLane` | EXISTENTE | Cada 12 a 20 s cruza un quad gigante a 42 m/s con 3,5 s de aviso; hueco seguro de 10 m bajo el chasis | Cruzar juntas por el hueco o en grupo tras el aviso; la coordinación del tiempo | Solo se replican la hora y el sentido | 1 |
| E-05 | Cangrejo ermitaño bola `ATN_BeachHermitCrab` | EXISTENTE | Rueda calle abajo hasta 15 m/s y derriba a todas las que estén en fila | Salir de la calle de una en una o subir por el lado; derribo no se apila | Rodada determinista; nada replicado mientras rueda | 1 |
| E-06 | Pulpo de poza `ATN_BeachPoolOctopus` | EXISTENTE | Agarra a quien nada en su poza y la lanza fuera | Una nada de cebo, las demás cruzan por un puente; o se le aturde con un ítem | Servidor + `Grabbed` | 1 |
| E-07 | Pulgas de arena `ATN_BeachSandFleas` | EXISTENTE | Enjambre lento (1,6 m/s) que da 2 s de saltitos y 1 s de mareo | Huir andando; el silbato o un golpe lo dispersa | Servidor; saltitos deterministas | 1 |
| E-08 | Tanque de juguete `ATN_BeachToyTank` | EXISTENTE | Patrulla un tramo y dispara bolitas de espuma que empujan y marean | Una atrae su fuego; el resto avanza en ángulo muerto | Servidor | 2 |
| E-09 | Gusano de arena `ATN_BeachSandWorm` | EXISTENTE | Emboscada desde la arena (cabecera propia del enemigo) | Atravesar por rocas o por la pasarela | Servidor | 1 |
| E-10 | Gaviotas y pelícanos `ATN_BeachGullZone` | EXISTENTE-AJUSTE | Baja en picado sobre quien va de pie; **a quien va en caparazón la coge**, la sube y la suelta aturdida; lo lanzado la marea | El paraguas (`PlantedUmbrella`) da cobertura; unas vigilan, otras cruzan; lo lanzado la aturde. Vuelta por el camino (`CourseBackAt`) | Un ataque replicado (`FTNBeachGullAttack`) | 3 |
| E-11 | Gaviota picotazo `ATN_EnemySeagull` | **CORTE** | Cuenta atrás y picotazo que mata | Duplica a E-10 y mata a un objetivo; deprecada en la práctica | — | 0 |

### 4.2 Peligros y entorno [COMÚN]

| Id | Peligro (clase) | Estado | Efecto | Letal | Red | Coste Coop (h) |
|---|---|---|---|---|---|---|
| H-01 | Alambre de espino `ATN_BeachBarbedWire` | EXISTENTE | Aturde y empuja hacia atrás; se salta (rollo de 1,3 m) | No | Sensor en servidor, FX multicast | 0 |
| H-02 | Algas que enredan `ATN_BeachSeaweed` | EXISTENTE | Enreda y retiene | No | Servidor | 0 |
| H-03 | Mina de juguete `ATN_BeachMine` | EXISTENTE | Lanza hacia atrás 7 a 8 m en bola; se rearma | No | `TriggeredAt`, `ExplodedAt` | 0 |
| H-04 | Concha que atrapa `ATN_BeachClamTrap` | EXISTENTE | Atrapa entre `HoldMin` y `HoldMax` y suelta | No | `FTNClamCatch` replicado | 0 |
| H-05 | Cubo roto `ATN_BeachBrokenBucket` | EXISTENTE | Túnel recorrible (estático) | No | Estático | 0 |
| H-06 | Remolino `ATN_ProcWhirlpool` | EXISTENTE | Atrae; 2,5 s en el ojo ahoga | Sí (sin DBNO) | Local, determinista | 1 |
| H-07 | Depredador de agua `ATN_ProcWaterPredator` | EXISTENTE | Patrulla y mata a nadadoras | Sí (sin DBNO) | Movimiento replicado | 1 |
| H-08 | Kill boxes de agua y barrancos (`ATN_ProcKillVolume`, `kill_boxes_uu`) | EXISTENTE | Muerte en el agua profunda/barrancos | Sí (sin DBNO) | Servidor | 0 |
| H-09 | Tormenta de camino `ATN_PathStorm` | EXISTENTE (opt-in) | Cuenta atrás de muerte a quien queda tras el frente | Sí (con DBNO) | Frente replicado | 0 |
| H-10 | Tormenta de playa `ATN_BeachStorm` | **FUERA del Coop** | Es de la Carrera | — | — | 0 |
| H-11 | Fauna ambiental `ATN_ProcFauna` | EXISTENTE | Decorado vivo: no interactúa | No | Local | 0 |
| H-12 | Fuera de zona / suelo no permitido (§8.3) | NUEVO (F1) | Rescate, no muerte | No | Servidor | (plan T15-T18) |

**Regla de muerte**: solo H-06 a H-09 y las kill boxes matan. H-06, H-07 y H-08 no dan DBNO (la compañera no puede llegar a un cuerpo en el agua): reaparición directa. H-09 sí lo da. Los golpes de E-01 a E-10 nunca gastan vida.

### 4.3 Escala por jugadoras y enemigo guardián [COOP]

**Densidad por N (multiplica el número de elementos de cada encuentro, redondeado; mínimo 1 por encuentro):**

| N | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| Multiplicador | ×0,6 | ×0,8 | ×1,0 | ×1,2 |

Se decide al arrancar la ronda (no cambia si alguien entra o sale) y se aplica con `Spec.Enabled`: los elementos «de más» se colocan siempre en el nivel pero se desactivan en el servidor.

**E-12 Cangrejo guardián (NUEVO, variante de E-01, sin asset).** Guarda un puzzle o un cruce. Es la pieza que hace que **todo el grupo** coopere:

- Estado inicial: en su puesto, sin poder ser evitado (cierra el paso con la embestida).
- Cada golpe de ítem (`ApplyHitStun`) lo deja mareado 6 s. Debe recibir **3 golpes de al menos 2 jugadoras distintas** para que «huya» y despeje el paso para siempre (`Defeated`).
- Con N = 1 hacen falta 2 golpes; con N = 2, 3 golpes (la misma jugadora puede repetir).
- Si nadie lo golpea en 45 s, se «cansa» y se marca desbloqueable (para evitar bloqueos de grupo sin ítems: siempre hay una caja de objetos a < 60 m).
- Red: `Defeated` (bool) y `StunEndsAt` replicados; nada más.
- Coste 8 a 10 h. **Toca `TN_BeachGiantCrab.*`, que el plan F0-F2 marca como fichero de otro agente**: se hace cuando ese trabajo esté fusionado.

**Reglas comunes de enemigos (código compartido; las hace un solo desarrollador):**

| Id | Cambio | Coste (h) |
|---|---|---|
| X-1 | Correa por zona: un enemigo no persigue a una jugadora fuera de `IsAllowed` (evita que salgan del mapa y vuelvan mal) | 6 a 8 |
| X-2 | Desactivar `Tick` cuando no hay jugadoras a < 90 m (`SetActorTickEnabled(false)`) y volver a activarlo | 4 a 6 |
| X-3 | Multiplicador de densidad por N y dificultad (`Spec.Enabled`) | 3 a 4 |
| X-4 | Filtro `IsFriendly` para golpes entre jugadoras (R-11) | 3 a 4 |
| X-5 | El silbato (O-09) y los ítems aturden a los enemigos con `ITN_EnemyTargetInterface` (ya existe) | 0 |

Total común de enemigos ≈ **16 a 22 h** (+ 8 a 10 h de E-12) más las columnas de coste por enemigo (≈ 15 h).

## 5. Objetos e ítems

Estado del asset: **EXISTENTE** (ya en el repo tal cual), **REUTILIZADO** (otra malla o clase con tinte, `M_CosmeticVertexColor` o instancia de material), **BORRADOR IA** (de `Art/Library/IA`, solo provisional), **NUEVO** (arte a crear). Regla: cada objeto trae 0 o 1 asset nuevo. Las mallas «en código» (`TNBeachProp::Build*`, `TN_BeachPropMeshes.h`) **no cuentan como asset de arte**: cuestan horas de C++.

Búsqueda en `Art/Library/IA/INDEX.md`: tiene 6 borradores, **todos de Todos contra Todos** (balón de playa, garfio con ancla, pala de mano, pistola de noqueo con dardo, pistola de tinta, trabuco de aire). **Ninguno se necesita en Coop.**

### 5.1 Ítems y pickups [COMÚN]

Pool de una caja de objetos en Coop (`ETNRaceLootSource::Box`; el cofre da lo mejor). Se retiran de la reserva de Coop `PelicanTaxi` (adelanta a todas, rompe el orden de los puzzles), `GullStrike` (va a por la líder) y `StormCloud` (marea a todas las demás): no tienen sentido cooperando.

| Id | Objeto | Qué hace en Coop | Cómo se obtiene | Autoridad y red | Asset | Peso en caja |
|---|---|---|---|---|---|---|
| O-01 | Caja de objetos `ATN_RaceItemBox` | Da un ítem al cogerla | Suelo, cada 100 m; reaparece a los 40 s | Servidor decide; `FTNInventoryItem` al inventario | EXISTENTE | — |
| O-02 | Coco turbo (`ETNRaceItem::Coconut`) | Aceleración ×1,4 durante 3 s: salvar un hueco o una emboscada | Caja, rebusca | Servidor aplica el turbo; el cliente ve el efecto | EXISTENTE (malla e icono por código) | 22 |
| O-03 | Triple coco (3/2/1) | Tres turbos | Caja, cofre | Ídem | EXISTENTE | 12 |
| O-04 | Coco dorado | Turbo largo (6 s); solo cofre | Cofre | Ídem | EXISTENTE | 0 (solo cofre) |
| O-05 | Protector solar `Sunscreen` | Invulnerable a enemigos 6 s y derriba lo que toca; a las compañeras no | Caja, cofre | Servidor | EXISTENTE | 10 |
| O-06 | Cangrejo teledirigido `HomingCrab` | **Cambio**: persigue al enemigo más cercano (en carrera va a por una tortuga). Aturde 4 s | Caja | Servidor decide objetivo | EXISTENTE (cambio de objetivo: 4 a 6 h) | 14 |
| O-07 | Mina de arena `SandMine` | Lanzable; aturde 4 s a un enemigo; ignora a las jugadoras | Caja | Servidor | EXISTENTE | 12 |
| O-08 | Disco volador `Frisbee` | Va y vuelve; aturde 2 s al primer enemigo | Caja | Servidor | EXISTENTE | 12 |
| O-09 | Silbato del sargento `Whistle` | Aturde a los enemigos en 25 m | Caja, cofre | Servidor | EXISTENTE | 8 |
| O-10 | Piedra lanzable `ATN_ThrowableItemActor` / `BP_GenericThrowable` | Aturde 2 s a un enemigo; se lanza con la trayectoria de siempre | Rebusca, suelo | `Multicast_InitializeThrow` | EXISTENTE | — |
| O-11 | Tinta de calamar `BP_InkProjectile` | Ciega al cangrejo gigante (vuelve a su recorrido) | Rebusca | Servidor | EXISTENTE | — |
| O-12 | Medusa lanzable `BP_JellyfishActor` | Aturde 3 s; sirve de cebo | Rebusca | Servidor | REUTILIZADO (`TNBeachProp::BuildJellyfish` sustituye el cilindro y la esfera de editor) | — |
| O-13 | Peluche tótem `BP_TotemInteractable` | Auto-revive una vez sin gastar vida (R-07) | Cofre, rebusca rara | `TryTotemAutoRevive` en servidor | EXISTENTE | — |
| O-14 | Cofre `ATN_BeachChest` | Objeto y conchas de calidad; tarda en abrirse | Mapa: 2 por mapa | Servidor; estado replicado | EXISTENTE | — |
| O-15 | Rebusca `ATN_BeachSearchSpot`, `ATN_ProcSearchSpot` | Objeto o concha | Mapa: 8 a 12 por mapa | Servidor; durmientes | EXISTENTE | — |
| O-16 | Concha de puntos `ATN_ScoreShells` / `BP_ScorePickup` | +1 concha a quien la coge | Mapa: 60 a 80 | Servidor; cada una es local | EXISTENTE con **arreglo P0**: quitar la malla de editor `EditorHelp` | — |

Los pesos suman 100 (22 + 12 + 10 + 14 + 12 + 12 + 8 = 90 más 10 de «vacío/piedra» de relleno). Se ajustan en el primer playtest.

### 5.2 Mecánicas de tránsito y de terreno [COMÚN]

Se colocan como `ATN_BeachElement` con `SpawnElement`, con el perfil de red de la ronda (`ApplyRoundNetProfile`: relevancia por distancia, dormancy y frecuencia).

| Id | Objeto (clase) | Qué hace | Cómo se coloca | Autoridad y red | Asset |
|---|---|---|---|---|---|
| O-17 | Nido de huevos `ATN_ProcEggNest` | Checkpoint y reaparición | Marcador `Nest` | Servidor; `IsActivated` replicado | REUTILIZADO (malla del huevo fantasma en vez de BasicShapes, P0 del inventario) |
| O-18 | Volumen de meta `ATN_ProcFinishVolume` | Fin de la ronda; confeti (`UTN_BeachFinishSplashSubsystem`) | Marcador `Finish` con `Dir` | Servidor | EXISTENTE (arco de neumático de la meta) |
| O-19 | Catapulta `ATN_BeachCatapult` | Lanza a quien se sube (arco validado por `TryAddWithArc`) | Marcador | Disparo decidido en servidor | EXISTENTE |
| O-20 | Trampolín `ATN_BeachTrampoline` y `ATN_JellyfishTrampoline` | Rebote hacia arriba y adelante | Marcador | Servidor | EXISTENTE |
| O-21 | Plataforma móvil `ATN_BeachMovingPlatform` | Ferri (3,3 m/s) o ascensor (1,7 m/s) | Marcador | Curva por tiempo de servidor | EXISTENTE (`bRequiresPower` de M2 para puzzle) |
| O-22 | Plataforma tambaleante `ATN_BeachWobblyPlatform` | Se inclina por el peso | Marcador | Inclinación replicada | EXISTENTE |
| O-23 | Plataforma rompible `ATN_BreakablePlatform` | Se rompe y reaparece a los 6 s | Marcador | Estado replicado | EXISTENTE |
| O-24 | Puerta de conchas `ATN_BeachShellGate` | Se abre 3 s al empujarla una bola ≥ 0,6 s | Marcador | `bOpen`, `ChangedAt` | EXISTENTE |
| O-25 | Géiser `ATN_ProcGeyser` | Chorro de 2,6 a 10,5 m, ciclo de 4,2 s | Marcador | Fase por tiempo de servidor | EXISTENTE (M3 para orientarlo) |
| O-26 | Deslizadero `ATN_ProcSlideZone` | Zona de deslizamiento | Marcador | Servidor | EXISTENTE |
| O-27 | Pala rampa `ATN_BeachSpadeRamp` | Trampolín o puente | Marcador | Servidor | EXISTENTE |
| O-28 | Pasarela y caminito (`Boardwalk`, `WoodenPostPath`) | Tramo recorrible de madera | Marcador (`Extent` = largo) | Estático | EXISTENTE (módulo por código) |
| O-29 | Fortaleza de arena `ATN_BeachFortress` | Torre subible con cofre en la cima | Marcador (3 tamaños) | Servidor | EXISTENTE |
| O-30 | Corriente `ATN_ProcWaterCurrent` | Arrastre de 900 cm/s² en el agua | Marcador | Local, determinista | EXISTENTE |
| O-31 | Rebote de agua `ATN_ProcWaterBouncer` | Devuelve a tierra | Marcador | Servidor | EXISTENTE |
| O-32 | Campo de vegetación `ATN_BeachDecorField` | Palmeras, matorral, hierba con puntos del DA, instanciado | `coop.vegetation.points` | Local (no replica), cota fija | EXISTENTE (formas de `TN_ProcMapFlora.h`; subconjunto tropical) |

### 5.3 Piezas de puzzle [COMÚN 2v2]

Detalle en §6. Todas son actores del nivel con estado replicado con `OnRep` (la entrada tardía ve el estado real).

| Id | Objeto | Qué hace | Autoridad y red | Asset |
|---|---|---|---|---|
| O-33 | Placa de presión `ATN_PressurePlate` | `Momentary` / `Latched`; peso por ocupante (M1) | Overlap en servidor; `bOccupied` replicado | EXISTENTE; sonido de editor en `BP_PressurePlate` a cambiar por `TN_BeachTrapSynth` |
| O-34 | Gestor de placas `ATN_PressurePlateGroupManager` | Umbral, `HoldDurationRequired` 2 s | Servidor | EXISTENTE (sin visual) |
| O-35 | Botón `ATN_ButtonInteractable` | Pulsar, `PressesRequired` | `OnInteracted` validado en servidor | EXISTENTE |
| O-36 | Gestor de botones `ATN_ButtonGroupManager` | Umbral | Servidor | EXISTENTE (sin visual) |
| O-37 | Muro de lanzamiento `ATN_ProcThrowWall` | 4,8 m + rampa de 7,5 m | `bRampDown` replicado | EXISTENTE |
| O-38 | Interruptor `ATN_ProcSwitch` | Baja la rampa 8 s; M4 acepta cualquier `ITN_Activatable` | Servidor | EXISTENTE |
| O-39 | Palanca temporizada `ATN_TimedLever` (N1) | Estados `Idle`/`Pulled`; `HoldSeconds`; `PulledAtServerTime` | Servidor | **NUEVO en código** (malla `TNBeachProp`); 0 arte |
| O-40 | Cesta `ATN_ShellBasket` (N2) | Receptor de bolas sobre poste de 3 m | Servidor; `bOccupied` | **NUEVO en código**; 0 arte |
| O-41 | Puerta de puzzle `ATN_PuzzleDoor` (N4) | Abierta/cerrada, temporizada o pegada | Servidor; `Closed/Open` replicado | **NUEVO en código**; 0 arte |
| O-42 | Lógica de puzzle `ATN_PuzzleLogic` (N3) | Modos `Window`, `Sequence`, `Code` | Servidor; el código **no se replica** | **NUEVO en código** (sin visual) |
| O-43 | Cartel de pictogramas | Enseña el primer puzzle de cada tipo | Local; se apaga con `TN_TutorialSaveGame` | **NUEVO en código** (`TNBeachProp` con color de vértice) |

Compuerta de sabotaje `ATN_ProcSabotageGate`: **[2v2]**, fuera del Coop.

### 5.4 Objetos solo Coop

No hay objetos exclusivos de Coop más allá de E-12 (§4.3), el cartel O-43 y las **reglas de reserva de caja** (§5.1). La única pieza de código propia es la tabla `FTNCoopLootPool` (ítems permitidos y pesos por dificultad): 4 a 6 h.

**Recuento de objetos del Coop:** 43 (O-01 a O-43), de los cuales 36 existen (O-16 con un arreglo P0 de malla de editor), 2 reutilizan malla (O-12 y O-17) y 5 son piezas nuevas en código (O-39 a O-43), **0 con arte nuevo**.

## 6. Puzzles

### 6.1 Plantillas que usa el Coop

Detalle de cada una en `Docs/Catalogo-Puzzles-2026-09-29.md` §3 a §5 (no se repite). Aquí: cuáles, con qué frecuencia y cómo escalan.

| # | `kind` | Jugadoras del catálogo | Frecuencia por mapa (CP01) | Pieza pendiente | Dificultad |
|---|---|---|---|---|---|
| 1 | `plate_balance` | 3 a 8 → **1 a 4** | 1 | N4, M1 | 1 |
| 2 | `basket_hold` | 3 a 8 → **1 a 4** | 1 | N1, N2, N4 | 2 |
| 3 | `throw_chain` | 3 a 8 → **1 a 4** (1 o 2 muros) | 1 | Ninguna | 2 |
| 4 | `geyser_aim` | 2 a 8 → **1 a 4** | 1 | N1, N6, M3 | 2 |
| 5 | `shell_gauntlet` | 3 a 8 → **1 a 4** | 1 | Ninguna | 2 |
| 6 | `platform_power` | 3 a 8 → **1 a 4** | 1 | N6, M2 | 2 |
| 7 | `wobbly_run` (parkour) | 1 a 8 → 1 a 4 | 1 a 2 | Ninguna | 2 |
| 8 | `breakable_chain` (parkour) | 1 a 8 → 1 a 4 | 1 | Ninguna | 2 |
| 9 | `catapult_gap` (parkour) | 1 a 8 → 1 a 4 | 1 | Ninguna | 1 |
| 10 | `think_room` | `code` 2 a 4; `sequence` 2 a 8 | 1 (variante `code` en T6) | N3, N4, N5 | 3 |

Las plantillas de 2 vs 2 (`lever_relay`, `counterweight_lift`, `throw_wall_pair`, `geyser_ferry`, `sabotage`) no se usan en Coop.

**Frecuencias:** un puzzle de grupo por cada 150 a 170 m de camino (6 a 7 por mapa); el parkour entre puzzles, uno cada 300 m; `think_room` como máximo 1 por mapa (dificultad 3) y nunca en el primer tercio. **Tiempo objetivo por puzzle** (catálogo): grupo, 30 a 90 s.

### 6.2 Piezas C++ que faltan

Del catálogo §2.2 y §2.3, íntegras (son comunes a Coop y 2 vs 2; se cuentan una sola vez):

| Id | Pieza | Coste (h) | Necesaria para |
|---|---|---|---|
| N1 | `ATN_TimedLever` | 5 a 7 | `basket_hold`, `geyser_aim`, `platform_power` |
| N2 | `ATN_ShellBasket` | 6 a 8 | `basket_hold` |
| N3 | `ATN_PuzzleLogic` (`Window`/`Sequence`/`Code`) | 10 a 14 | `think_room` |
| N4 | `ATN_PuzzleDoor` | 3 a 4 | `plate_balance`, `basket_hold`, `think_room` |
| N5 | `UTN_PuzzleStateComponent` (`PuzzleId`, `Progress`, `Reset()`, `OnSolved`, `OnFailed`) | 6 a 8 | Todas (HUD y `PuzzleSolvedMask`) |
| N6 | `ITN_Activatable` | 3 a 4 | `platform_power`, `geyser_aim` |
| M1 | Peso por ocupante en `ATN_PressurePlate` | 3 a 4 | `plate_balance` |
| M2 | `bRequiresPower` en `ATN_BeachMovingPlatform` | 3 a 4 | `platform_power` |
| M3 | `SetAimIndex` en `ATN_ProcGeyser` | 2 a 3 | `geyser_aim` |
| M4 | `SetTarget` genérico + `OwnerTeam` en `ATN_ProcSwitch` | 2 a 3 | `throw_chain` |

Total: **43 a 59 h** (6 piezas + 4 modificaciones). Nuevo en este documento (sin asset): **P-1** `UTN_PuzzleScaleComponent`, que aplica la tabla de §6.3 al reiniciar cada puzzle (4 a 6 h), y **P-2** la unión `PuzzleSolvedMask` en el `GameState` (2 a 3 h).

### 6.3 Escala por número de jugadoras [COOP]

Los catálogos estaban pensados para 3 a 8. Con 1 a 4 hay que sustituir lo que exige más cuerpos que jugadoras. Lo decide el servidor al **reiniciar** el puzzle (`Reset()`), nunca a mitad; los parámetros salen de `puzzle_id` y N.

| `kind` | N = 1 | N = 2 | N = 3 | N = 4 |
|---|---|---|---|---|
| `plate_balance` | 2 placas, peso requerido 2 (una en bola sobre una placa vale 2) | 4 placas, peso 3 | 4 placas, peso 4 | 5 placas, peso 5 |
| `basket_hold` | Sin cesta: puerta con palanca de 25 s (`lever_hold_s` 25, `latch` false) | Ventana 10 s, cesta | 8 s, cesta | 8 s, cesta |
| `throw_chain` | Un trampolín (`ATN_JellyfishTrampoline`) al pie del muro, activo solo con N = 1 | 1 muro | 1 muro | 2 muros |
| `geyser_aim` | Igual (una sola opera la palanca; la posición se mantiene) | Igual | Igual | Igual |
| `shell_gauntlet` | Igual (la bola cruza tras empujar; `open_hold_s` 3) | Igual | Igual | Igual |
| `platform_power` | Energía residual `power_linger_s` 12 tras soltar la placa | 0 | 0 | 0 |
| `think_room` (`code`) | Placas `Latched` (una sola persona hace los dos pasos) | Como el catálogo | Como el catálogo | Dos pasos a la vez posibles |
| Parkour | Igual | Igual | Igual | Igual |

El peso requerido se lee del gestor con `EvaluateCondition` sumando `OccupantWeight` (M1). Esto es lo que evita que un grupo pequeño quede bloqueado.

**Regla de no bloqueo.** Todo puzzle de grupo tiene una salida de emergencia: si tras 4 min el grupo no lo ha resuelto, aparece una **caja de objetos** cercana y, tras 6 min, un cartel «¿Atascadas? Pulsad `T` para reiniciar el puzzle» que hace `Reset()`. No hay opción de saltárselo.

### 6.4 Autoridad y datos del boceto

- Servidor: toda activación, temporizador y reinicio. Clientas: solo `OnInteracted` (distancia validada) o pisar una placa.
- `puzzle_id` estable en el boceto y persistente en `PuzzleSolvedMask` para reconexiones.
- Expansión de plantillas: `Scripts/map_sketch/markers.py` (Python; no genera C++); el validador rechaza plantillas con piezas pendientes hasta que existan.

## 7. Interfaz y efectos

Todo se construye en código (widgets C++ y sintetizadores) y **no añade texturas**. Los widgets leen estado replicado (sondeo cada `RefreshInterval` y delegado de respaldo, patrón de `UTN_CoopFlowHUDWidget`); ninguno envía datos de juego, solo peticiones.

### 7.1 Interfaz [COMÚN]

| Id | Elemento | Reutiliza | Nuevo | Coste (h) |
|---|---|---|---|---|
| U-01 | HUD base: cara, salvavidas de energía, pista de la salida a la meta (con `path_dist_dm`) | `UTN_RunHUDWidget`, `TN_HUDFaces.h` | Alimentar la pista con el progreso del camino | 3 a 4 |
| U-02 | Marcador de compañeras: nombre y cara flotando, flecha de borde de pantalla a > 40 m (alimentada por `PathProgressDm`, sin replicar posiciones lejanas) | `TN_HUDFaces.h` | Widget de flechas (U3 de `Modos-UI-FX`) | 6 a 8 |
| U-03 | Anillo de progreso de puzzle: billboard sobre la pieza principal; verde al resolver, rojo al fallar | `UTN_HoldRingWidget` | Enlace con `UTN_PuzzleStateComponent.Progress` (U2) | 4 a 6 |
| U-04 | Prompts de interacción («Tirar de la palanca», «Reanimar») | `TN_InteractPromptWidget` | — | 0 a 1 |
| U-05 | Cartel de primera vez con pictogramas | `TN_TutorialWidget`, `TN_TutorialSaveGame` | Malla de cartel (O-43) | 4 a 6 |
| U-06 | Aviso «fuera de zona» al ser rescatada: viñeta breve + texto «Te has salido del camino» | `UTN_CoopFlowHUDWidget` | Un mensaje | 2 a 3 |
| U-07 | Anillo de revivir sobre la compañera caída (visible para todas) | `UTN_HoldRingWidget` | Icono de DBNO con cuenta atrás desde `DBNOEndsAt` | 3 a 4 |
| U-08 | Selector de mapa del lobby (con etiqueta de modo, duración, dificultad) | `ATN_ProcModeSelector` | `ETNProcSelectorKind::Map` y catálogo (U5, ya en T31) | (T31) |
| U-09 | Pantalla de carga con el Coop reconocido | `UTN_LoadingScreenSubsystem` | R8 del plan (T22) | (T22) |
| U-10 | Rueda de emotes y quick chat | `UTN_RadialWheelWidgetBase`, `UTN_CoopFlowHUDWidget` | — | 0 |
| U-11 | Panel de resultados de hasta 4 filas | `UTN_CoopFlowHUDWidget`, `ATN_RacePodiumStage` | Columnas nuevas (revivires, caídas) | 3 a 4 |

Subtotal común: **25 a 36 h** (los dos ya planificados en T22 y T31 no se cuentan aquí).

### 7.2 Interfaz solo Coop [COOP]

| Id | Elemento | Diseño | Coste (h) |
|---|---|---|---|
| U-20 | **Vidas de equipo** | Fila de caparazones en la esquina superior izquierda: N llenos y el resto vacíos. Al perder una, parpadea y cae; al ganar una, brilla. Texto «Vidas 7/9» accesible | 3 a 4 |
| U-21 | **Estado del puzzle** | Texto «Placas 2/3» y cuenta atrás de puerta o palanca (desde `PulledAtServerTime`, sin RPC) | 3 a 4 |
| U-22 | **Cuenta atrás de meta** | «Faltan 43 s», `UTN_RaceFinishCountdownWidget` con texto de Coop | 1 a 2 |
| U-23 | **Barra de DBNO propia** | Barra roja de `DBNOEndsAt`; texto «Te revivirán… ¡aguanta!» | 2 a 3 |
| U-24 | **Recompensas en resultados** | Desglose de la tabla de §2.2 por jugadora, con la suma | 3 a 4 |
| U-25 | **Pantalla de derrota** | «El grupo ha caído en el tramo 4»; % de camino; botones Continuar desde el nido / Volver al lobby (solo anfitrión) | 2 a 3 |

Subtotal Coop: **14 a 20 h**.

### 7.3 Efectos [COMÚN]

| Id | Efecto | Reutiliza | Nuevo | Coste (h) |
|---|---|---|---|---|
| F-01 | Rescate fuera de zona | Aturdimiento de `TNBeach::StunTurtle` y `UTN_DizzyBirdsComponent` | — | 0 |
| F-02 | Puzzle resuelto: destello + polvo | `UTN_TurtleDustComponent`, `UTN_BeachFinishSplashSubsystem` | Destello en código (F3) | 3 a 5 |
| F-03 | Fallo: luz roja de 1 s y zumbido | `UTN_PuzzleCueSynth` | Incluido en F1 | 0 |
| F-04 | Revivir: halo dorado + polvo | `UTN_TurtleDustComponent` teñido | Tinte | 1 |
| F-05 | Estampado: polvo + pajaritos + aplastado (plan §3.5) | `UTN_TurtleDustComponent`, `UTN_DizzyBirdsComponent`, `UTN_ShellImpactSynthComponent` | Aplastado en código | 2 a 3 |
| F-06 | Nido activado: brillo | Brillo de pickups (`PickupGlow`) | — | 0 |
| F-07 | Vida perdida: viñeta rojiza de 0,5 s | Viñeta del HUD | Un material dinámico existente | 1 |
| S-01 | Cues de puzzle (éxito de 3 notas, fallo, placa, palanca, puerta) | `UTN_RaceCueSynthComponent`, `UTN_BeachTrapSynthComponent` | **`UTN_PuzzleCueSynth`** (F1 de `Modos-UI-FX`) | 8 a 10 |
| S-02 | Impactos, estampado, bola | `UTN_ShellImpactSynthComponent` | — | 0 |
| S-03 | Agua y poza | `UTN_BeachSplashSynthComponent` | — | 0 |
| S-04 | Meta y conchas | `UTN_ScoreShellSynthComponent`, `UTN_RaceCueSynthComponent` | — | 0 |
| S-05 | Ambiente del bioma | `UTN_AmbientSynthComponent` | — | 0 |
| S-06 | Voz de proximidad | VOIP existente | — | 0 |

**Accesibilidad**: equipos por color + patrón de caparazón; éxito y fallo con sonido además de luz; texto con contraste ≥ 4,5:1 sobre el panel azul marino del HUD Tortunavy. Sin sonidos de editor (`EditorSounds`, `VREditor`) en `BP_ConchPickUp`, `BP_PressurePlate`, `BP_JellyfishActor` ni `DT_Items` (P0 del inventario): sustituidos por sintetizadores.

## 8. Backend y red

### 8.1 Clases y estado replicado [COMÚN]

| Rol | Clase | Notas |
|---|---|---|
| GameMode | `ATN_ProcMapGameMode` con `Source = Prepared` en `ATN_ProcMapGenerator`; hereda de `ATN_RunGameMode`, que pasa a `ATN_MatchGameModeBase` (abstracta, T36) | Servidor solamente. Los métodos de vidas, derrota y recompensa (R-03, R-04, §2.2) van aquí |
| GameState | `ATN_ProcMapGameState : ATN_CoopGameState` | Estado de ronda y de equipo |
| PlayerState | `ATN_CoopPlayerState` | Estado por jugadora |
| Generador | `ATN_ProcMapGenerator` con `UTN_CoopMapData` | Único replicado: `FTNProcMapNetConfig` (+ `PreparedMapId`) |
| Nivel | `LVL_Coop_<id>` con subniveles | Marcadores y DA van en el paquete, no se replican |
| Subsistema | `UTN_SafeGroundSubsystem` | Rescate y `WalkableZAt` |

**Propiedades replicadas** (todo lo demás es local o determinista):

| Id | Propiedad | Clase | Tipo | Condición | Frecuencia | Bytes |
|---|---|---|---|---|---|---|
| N-01 | `PreparedMapId`, `MapSeed` | `FTNProcMapNetConfig` | `uint16` + `int32` | Inicial | 1 vez | 8 |
| N-02 | `TeamLives`, `TeamLivesMax`, `Attempts`, `EndState` | GameState | 3 × `uint8` + enum | Todas | Al cambiar (`OnRep`) | 4 |
| N-03 | `TeamBestNest` | GameState | `int8` | Todas | Al cambiar | 1 |
| N-04 | `PuzzleSolvedMask` | GameState | `uint32` | Todas | Al cambiar | 4 |
| N-05 | `RoundStartServerTime`, `FinishCountdownEndsAt` | GameState | 2 × `float` | Todas | Al cambiar | 8 |
| N-06 | `bIsAlive`, `bIsDBNO`, `DBNOEndsAt` (hora del servidor, **sustituye** a `DBNOBleedoutTimeRemaining`) | PlayerState | bool ×2, `float` | Todas | Al cambiar | 6 |
| N-07 | `PathProgressDm` | PlayerState | `uint16` | Todas | 1 Hz | 2 |
| N-08 | `RaceScore`, `RevivesDone`, `bHasFinishedRun`, `FinishTimeSeconds` | PlayerState | `int32`, `uint8`, bool, `float` | Todas | Al cambiar | 10 |
| N-09 | Estado de piezas de puzzle (`bRampDown`, `bOccupied`, `PulledAtServerTime`, `Closed/Open`, `AimIndex`) | Cada pieza | Pequeños | Todas | `OnRep` + dormancy | 1 a 8 |
| N-10 | Elementos (`Spec`, estado) | `ATN_BeachElement` | Según elemento | Todas | Perfil de la ronda (dormancy) | — |
| N-11 | Bola física / cuerpo lanzado (`StartBody`) | `ATN_ShellBody` | Posición + velocidad | Todas | 10 a 15 Hz | 24 por actualización |
| N-12 | Inventario | Componente del jugador | `FTNInventoryItem` | Solo dueña | Al cambiar | 16 |

**RPC**:

| Id | RPC | Tipo | Validación |
|---|---|---|---|
| N-20 | `ServerReviveStart(TargetPS)` / `ServerReviveCancel` | Cliente → servidor, fiable | Distancia ≤ 3 m, objetivo en DBNO, estado de quien revive activo |
| N-21 | `ServerInteract` (de `ATN_DirectInteractableBase.OnInteracted`) | Cliente → servidor, fiable | Distancia, estado de la pieza y cooldown |
| N-22 | `ServerUseItem` | Cliente → servidor, fiable | El objeto está en su inventario |
| N-23 | `ServerRequestContinueFromNest` | Cliente → servidor, fiable | Solo anfitrión y solo en `EndState = Defeat` |
| N-24 | `Multicast_PuzzleFX(PuzzleId, Result)`, `Multicast_RescueFX`, `Multicast_DiveSplatFX`, `Multicast_LifeChanged` | Servidor → todas, **no fiable** | Cosmética; el estado real va por `OnRep` |

**Reglas de red (R-30):** (1) ningún RPC por fotograma; (2) los temporizadores viajan como **hora del servidor**, no como tiempo restante (una réplica por cambio en vez de una por tic); (3) los movimientos de piezas interpolan entre dos estados replicados; (4) el código de `think_room` nunca se replica; (5) todo lo que puede desfasar (estampado, bola, agarre) usa la física replicada de `ATN_ShellBody` con corrección del servidor; (6) los enemigos deterministas (ermitaño, quads, gaviotas) no replican movimiento; (7) `bReplicateMovement` solo en lo que se ve a cualquier distancia (gaviota de caza).

### 8.2 Relevancia [COMÚN]

| Actor | Distancia de relevancia | Dormancy | Frecuencia |
|---|---|---|---|
| Pawns de jugadoras | 250 m (el HUD lejano usa `PathProgressDm`, no la posición) | No | Movimiento predicho de siempre |
| Enemigos (`ATN_BeachEnemy`) | 150 m | Despiertos solo con jugadoras a < 90 m (X-2) | 10 Hz cerca, 2 Hz mínima (valores actuales) |
| Piezas de puzzle | 120 m | `DORM_Initial`; despiertan con `ForceNetUpdate` 2 s (patrón de `ATN_BeachElement`) | 1 Hz |
| Mecánicas de tránsito | Perfil de la ronda | Sí (`WantsNetDormancy`) | 1 a 5 Hz |
| Cuerpos lanzados (bola) | 250 m | No | 10 a 15 Hz |
| Nidos, meta | 200 m | Sí | 1 Hz |
| Vegetación, decorado | No se replica | — | — |
| `AGameState` y PlayerStates | Siempre | No | 5 Hz mínima |

### 8.3 Fuera de zona y rescate [COMÚN]

Todo en el servidor (plan maestro §2.3; tareas T15 a T18). Constantes ya fijadas en el plan: periodo 0,25 s, gracia 1,5 s en suelo no permitido, fuera de límites = rescate inmediato, límite inferior `z0 − 200 uu`, anillos de 150 uu, separación mínima 120 uu, aturdimiento 1,0 s, reserva `SafetyNet` 0,75 s.

| Id | Regla | Detalle |
|---|---|---|
| R-20 | Permitido | `safe_low ≤ k(Z) ≤ safe_high + 1` dentro de `bounds_uu` |
| R-21 | Vigilancia | Cada 0,25 s y por pawn; en suelo permitido guarda `LastSafe` |
| R-22 | Disparo | Más de 1,5 s en suelo no permitido, o fuera de límites |
| R-23 | Destino | Muestra de `Main` con mayor `S ≤ LastSafe.Progress`, con anillos si está ocupada. `TNBeach::ReleaseTurtle` + aturdimiento de 1 s |
| R-24 | Coste | **No gasta vida**. Si la jugadora estaba en DBNO, sigue en DBNO en su destino |
| R-25 | Cuerpos lanzados | Una bola que sale de zona se rescata igual; la compañera que la lanzó no recibe penalización |
| R-26 | Gaviotas | `CourseBackAt(P)`; una suelta en zona no permitida se rescata al aterrizar |
| R-27 | Anti-bucle (nuevo) | Si una jugadora es rescatada 3 veces en 30 s en el mismo destino, el siguiente destino retrocede 2 muestras (≈ 12 m) y se muestra el consejo «Prueba otra ruta» |
| R-28 | Puzzle | Un rescate mientras se sostiene una placa o una palanca reinicia la pieza con `Reset()` si quedó en estado inconsistente |
| R-29 | Enemigos | La correa X-1 impide que persigan fuera de `IsAllowed` |
| R-30b | Aviso | U-06 durante 2 s y F-01 |

Coste por muestreo: 4 pawns × 4 Hz = 16 consultas por segundo al proveedor de suelo (`WalkableZAt` traza desde `Z + 2 m`). Presupuesto: < 0,1 ms por consulta.

### 8.4 Red propia del Coop [COOP]

| Id | Tema | Decisión |
|---|---|---|
| N-30 | Vidas | Solo el servidor las modifica; `TeamLives` y `EndState` por `OnRep` |
| N-31 | Reintento desde el nido | El servidor teletransporta a todas a `TeamBestNest`, reinicia enemigos de ese tramo y **no** reinicia `PuzzleSolvedMask` |
| N-32 | Umbrales de puzzle | Se recalculan al `Reset()` con las vivas (`TriggerThreshold = -1`) |
| N-33 | Recompensa | Se calcula en el servidor al final; el servidor manda a cada cliente el desglose (RPC fiable al dueño) |
| N-34 | Anfitrión | El anfitrión ve su propio `OnRep` con una llamada manual del GameMode (los `OnRep` no corren en el servidor) |
| N-35 | Entrada tardía | Ver R-08; el estado de las piezas ya viene por `OnRep` |
| N-36 | Semilla | `Seed` decide el reparto de conchas, cajas y peligros aleatorios; no se regenera el terreno |

**Presupuesto de red por jugadora (1.ª pasada, a medir con `stat net`):**

| Concepto (cliente, 4 jugadoras) | KB/s |
|---|---|
| 3 pawns remotos | 7,5 |
| Estados de equipo y jugadoras | 1,0 |
| Hasta 8 enemigos relevantes | 4,0 |
| Piezas de puzzle y mecánicas (dormidas) | 1,5 |
| 2 cuerpos físicos (bola) | 3,0 |
| Inventario, chat, emotes | 0,5 |
| Reserva | 1,5 |
| **Total medio hacia una cliente** | **≈ 19** |
| **Tope medio / percentil 95** | **24 / 40** |
| Subida de una cliente | ≤ 6 |
| Subida del anfitrión (3 clientas) | ≈ 57 (≈ 0,45 Mbps) |

`net.MaxClientRate` y `ConfiguredInternetSpeed` deben permitir ≥ 32 KB/s (comprobar `DefaultEngine.ini`). La voz (VOIP) va aparte y no entra en estos números.

## 9. Optimización

### 9.1 Presupuestos [COMÚN]

Referencia de hardware para las cifras (supuesto, a confirmar con el director, pregunta Q5): GPU de gama media de 2020 (equivalente a GTX 1660 / RTX 2060), 16 GB, 1080p, 60 fps.

| Id | Concepto | Presupuesto (1.ª pasada) | Cómo se garantiza |
|---|---|---|---|
| B-01 | Terreno Coop | ≤ 1,1 M triángulos (CP01), Nanite y colisión compleja, decimado con error ≤ 15 uu | D-11, `preview.png` y test |
| B-02 | Props visibles en pantalla | ≤ 300 k triángulos; ≤ 900 llamadas de dibujo | Mallas de props ≤ 4 k tris (inventario); ISM/HISM en decorado |
| B-03 | Vegetación | ≤ 2 000 puntos instanciados, corte a 120 m, 0 tics | `ATN_BeachDecorField` |
| B-04 | Actores con `Tick` activos en el servidor | ≤ 60 (4 pawns, ≤ 12 enemigos despiertos, ≤ 6 piezas móviles activas, resto en eventos) | Contador de `stat game`; X-2 |
| B-05 | Piezas de puzzle | 0 `Tick` en reposo (temporizadores y `OnRep`) | Regla de implementación de N1 a N6 |
| B-06 | Actores del nivel (total) | ≤ 500 (sin vegetación) | `obj list` |
| B-07 | Frecuencias de red | Ver §8.2 (10 Hz enemigos, 1 Hz piezas, 10 a 15 Hz bolas) | Perfil de la ronda |
| B-08 | Pooling | Proyectiles/ítems lanzados (`ATN_ThrowableItemActor`): 16; componentes de polvo y pajaritos: 8; anillos de puzzle: 4; conchas: sin actor propio si es posible (`ATN_ScoreShells`) | `TN_ActorPool` (nuevo, 4 a 6 h) |
| B-09 | Memoria | Streaming por subniveles; pico ≤ 6 GB de RAM en 4P | `stat memory` |
| B-10 | Servidor | ≤ 8 ms por fotograma en el percentil 95 con 4 jugadoras y 12 enemigos | `stat unit` en el anfitrión |
| B-11 | Cliente | ≥ 60 fps de media, percentil 1 ≥ 45 fps | `stat unit`, CSV Profiler |
| B-12 | Colisión | La cocina el paquete; **ningún cliente cocina colisión** (log sin `TN_MapVariantLoader.cpp:358`) | Aceptación del plan §3.1 |
| B-13 | Sonido | Sintetizadores con `ISoundGenerator` desactivados si no hay nadie a < 60 m | Regla de implementación |

### 9.2 Valores de CP01 [COOP]

| Concepto | CP01 |
|---|---|
| Enemigos colocados / despiertos a la vez | ≈ 40 / ≤ 12 |
| Piezas de puzzle | ≈ 60 (10 puzzles) |
| Mecánicas de tránsito | ≈ 20 |
| Cajas, cofres, rebuscas | ≈ 10 / 2 / 10 |
| Conchas de puntos | 60 a 80 |
| Puntos de vegetación | ≈ 1 800 |
| Actores totales (sin vegetación) | ≈ 350 |

### 9.3 Qué se mide en las pruebas de estrés

| Medida | Herramienta | Umbral |
|---|---|---|
| Tráfico por cliente (medio y p95) | `stat net`, `net.PktLag`, Network Profiler | § 8.4 |
| Tiempo de fotograma del anfitrión | `stat unit`, `stat game` | B-10 |
| Actores con `Tick` en el servidor | `stat game` (Tick Time por grupo) | B-04 |
| Triángulos y llamadas de dibujo | `stat scenerendering`, `r.Nanite.Visualize` | B-01, B-02 |
| Consumo de RAM y crecimiento en 30 min | `stat memory`, `memreport -full` | Crecimiento ≤ 50 MB |
| Tiempo de entrada tardía | Cronómetro en el log (`OnPostLogin` → `IsMapReady`) | ≤ 8 s |
| Rescates por minuto y falsos positivos | Log del vigilante | Falsos positivos = 0 |
| Correcciones de movimiento > 50 cm | `p.NetShowCorrections` | 0 al iniciar un dive |
| Desfase de la bola tras estampado | Log de 4 máquinas | ±5 cm |

## 10. Pruebas y criterios de aceptación

### 10.1 Automatización (pruebas puras, `Tortunabo.Coop.*`)

La lógica pura se escribe en `Public/Game/TN_CoopRulesDecisions.h` (namespace `TNCoop`, sin `UObject`) con pruebas en `Private/Tests/TN_CoopRulesTest.cpp`.

| Id | Prueba | Criterio |
|---|---|---|
| T-01 | `Tortunabo.Coop.Lives` | Vidas iniciales = 5 / 7 / 9 / 11 para N = 1 a 4; +1 al resolver puzzle con tope; muerte a 0 = derrota; revive no gasta. Caso negativo: rescate no gasta |
| T-02 | `Tortunabo.Coop.Victory` | Victoria con todas conectadas en meta; cuenta atrás de 60 s; rezagadas ×0,5; desconexión recalcula |
| T-03 | `Tortunabo.Coop.Revive` | Distancia ≤ 3 m, tiempo 1,5 s (0,8 s con 2), objetivo no DBNO rechaza |
| T-04 | `Tortunabo.Coop.PuzzleScale` | La tabla de §6.3 para N = 1..4; el peso requerido nunca supera 2N |
| T-05 | `Tortunabo.Coop.ItemPool` | `PelicanTaxi`, `GullStrike` y `StormCloud` nunca salen; suma de pesos = 100 |
| T-06 | `Tortunabo.Coop.FriendlyFire` | `IsFriendly` verdadero entre jugadoras; ítems ofensivos las ignoran |
| T-07 | `Tortunabo.Coop.Reward` | Cálculo de la tabla de §2.2 con casos de borde (rezagada, derrota) |
| T-08 | `Tortunabo.Coop.Rescue` | Constantes del plan; anti-bucle R-27 |
| T-09 | `Tortunabo.Coop.PuzzleReset` | Cada plantilla vuelve a su estado inicial con `Reset()` y no queda sin salida |
| T-10 | `Tortunabo.Coop.Guardian` | E-12: 3 golpes de 2 jugadoras distintas lo derrotan; 45 s sin golpes lo cansa |
| T-11 | `Tortunabo.Coop.Finish` | Meta orientada a −X y +Y (T20 del plan) |
| T-12 | pytest `test_cp01.py` y `map_sketch` | D-01 a D-17 sobre CP01 y C01 |

### 10.2 Partida (PIE 4P *listen*, PktLag 150 ms, 2 % de pérdida)

| Id | Prueba | Criterio de aceptación |
|---|---|---|
| T-20 | Recorrido completo de CP01 con 4 jugadoras (guiadas) | Se llega a meta en 12 a 18 min; 0 pawns congelados > 1 s al aparecer |
| T-21 | Cada plantilla resuelta 20 veces con N = 1, 2, 3 y 4 | 100 % resuelve; 0 estados inconsistentes tras `Reset()` |
| T-22 | 20 lanzamientos forzados fuera de zona | 20 rescates en ≤ 1,75 s a < 1 m del camino; 0 gastan vida |
| T-23 | Estampado y bola tras lanzar a una compañera | Misma posición en 4 máquinas (±5 cm); la caja lanzada no choca con la portadora |
| T-24 | Entrada tardía en el tramo 4 | Estado de puzzles correcto en ≤ 8 s; vidas actuales |
| T-25 | Un cliente desconectado y otro entra | Umbrales recalculados; el puzzle en curso no se rompe |
| T-26 | Todas caen (5 muertes seguidas en solitario o 11 en 4) | Derrota, Continuar desde el nido funciona, la recompensa baja al 50 % |
| T-27 | Partida de 30 minutos con 4 clientes (*soak*) | Sin caída; crecimiento de memoria ≤ 50 MB; tráfico ≤ B de §8.4 |
| T-28 | Cada mecánica de carrera activada al menos una vez | Sin `Find` nulo (R10 del plan) |
| T-29 | Nivel abierto sin «Failed to load» y MapCheck | 0 errores |
| T-30 | `PreparedMapId` alterado | Error en el log y vuelta al lobby |
| T-31 | Fauna sin `Layout.Modules` | Se genera sin error (R5) |
| T-32 | Jugar 3 puzzles de cada tipo en solitario (N = 1) | Ninguno se bloquea; tiempo ≤ 2× el de un grupo |

### 10.3 Playtest de diseño (no automatizable)

| Id | Pregunta | Método |
|---|---|---|
| T-40 | ¿Se entiende el primer puzzle sin ayuda del desarrollador? | 3 grupos nuevos, sin explicación; observación |
| T-41 | ¿Las vidas son justas? | Vidas restantes al final: mediana entre 30 y 60 % |
| T-42 | ¿Se reparten papeles o manda una sola? | Conteo de acciones por jugadora en resultados |
| T-43 | ¿Duración y ritmo? | 12 a 18 min; sin tramo > 4 min sin evento |

## 11. Presupuesto de assets

Regla: 0 o 1 asset nuevo por objeto. «Nuevo en código» = malla procedural (`TNBeachProp`) o sintetizador (horas de C++), no arte. **Ningún borrador de `Art/Library/IA` se necesita en Coop** (los 6 son de Todos contra Todos).

| Id | Asset | Tipo | Reutiliza / nuevo | Prioridad |
|---|---|---|---|---|
| A-01 | Terreno de CP01_coop | Malla volumétrica generada | Nuevo por **pipeline** (`gen_terrain_path.py`), sin modelador | P0 |
| A-02 | Terreno de C01_camino | Malla | EXISTENTE | P0 |
| A-03 | Lámina de agua | Malla/material | REUTILIZA `SM_WaterSurface`, `M_TortunaboWater` (sacar `DA_WaterSurface` del cocinado) | P0 |
| A-04 | Vegetación tropical | Mallas procedurales + material | REUTILIZA `TN_ProcMapFloraMeshes.h`, `M_ProcFoliage` | P0 |
| A-05 | Cesta de caparazón (O-40) | Malla | NUEVO en código (`TNBeachProp`) | P0 |
| A-06 | Palanca temporizada (O-39) | Malla | NUEVO en código | P0 |
| A-07 | Puerta de puzzle (O-41) | Malla | NUEVO en código | P0 |
| A-08 | Cartel de pictogramas (O-43) | Malla | NUEVO en código | P1 |
| A-09 | Cangrejo guardián (E-12) | Tinte | REUTILIZA malla de E-01 con una instancia de `M_CosmeticVertexColor` | P0 |
| A-10 | Huevo del nido (O-17) | Malla | REUTILIZA huevo fantasma / `BP_TurtleEgg` | P0 |
| A-11 | Medusa lanzable y pickup de concha (O-12, O-16) | Malla | REUTILIZA `BuildJellyfish`, huevo; quitar mallas de editor | P0 |
| A-12 | Iconos de `DT_Items` | Textura | REUTILIZA `render_preview.py` (256²) | P0 |
| A-13 | Cues de puzzle | SFX | NUEVO en código (`UTN_PuzzleCueSynth`) | P0 |
| A-14 | Destello de puzzle y polvo | VFX | REUTILIZA `UTN_TurtleDustComponent` y `FinishSplash` | P0 |
| A-15 | Cosméticos de recompensa | Cascos y patrones | EXISTENTES (`SM_Helmet_*`, `ETNShellPattern`) | P1 |
| A-16 | Vista previa del mapa en la carga | UI | REUTILIZA render del generador (1024×576) | P2 |
| A-17 | Ave con esqueleto (gaviota compartida) | Malla animada | NUEVO de arte, compartido con Carrera y TcT | P2 (no imprescindible) |
| A-18 | Kits de trampas, restos y playa (encargos «P2» del inventario) | Mallas | NUEVO de arte, futuro | P2 |
| A-19 | Iconos HUD de vidas | UI | REUTILIZA `TN_HUDFaces.h` y una forma de caparazón en código | P1 |

**TOTAL de assets de arte nuevos imprescindibles del Coop: 0.** Nuevos en código: 4 mallas procedurales (A-05 a A-08), 1 sintetizador (A-13) y 1 instancia de material (A-09). Nuevos de arte no imprescindibles (P2): 2 (A-17, A-18, a decisión del equipo de arte).

**Arreglos previos de contenido (0 arte, P0 del inventario)** para que el Coop no empaquete material de editor: `BP_ScorePickup` (malla `EditorHelp`), `BP_JellyfishActor`, `BP_RescuePickUp`, `BP_ConchPickUp`, `BP_PressurePlate` y `DT_Items` (sonidos e iconos de editor), `ATN_ProcEggNest` (BasicShapes).

## 12. Estimación en horas y dependencias

Horas de primera pasada, sin depuración de red ni playtests.

### 12.1 Reparto

| Bloque | Ámbito | Horas |
|---|---|---|
| C-1 Revivir y DBNO (R-06, U-07, `DBNOEndsAt`) | Común | 8 a 10 |
| C-2 Piezas de puzzle N1 a N6 y M1 a M4 | Común 2v2 | 43 a 59 |
| C-3 Enemigos: X-1 a X-4, ajustes por enemigo (≈ 16) y O-06 | Común | 35 a 43 |
| C-4 Interfaz común (§7.1) | Común | 25 a 36 |
| C-5 Efectos y sonido (§7.3) | Común | 14 a 19 |
| C-6 Red, presupuestos, instrumentación y pooling (§8, §9) | Común | 20 a 28 |
| C-7 Pruebas comunes (T-08 a T-12, T-20 a T-32) | Común | 12 a 16 |
| **Subtotal común** | | **157 a 211** |
| K-1 Reglas Coop: vidas, derrota, victoria, continuar, dificultad, campos de estado | Coop | 18 a 24 |
| K-2 Escala de puzzles (P-1, P-2) | Coop | 6 a 9 |
| K-3 Cangrejo guardián (E-12) | Coop | 8 a 10 |
| K-4 Reserva de la caja (`FTNCoopLootPool`) | Coop | 4 a 6 |
| K-5 Interfaz Coop (§7.2) | Coop | 14 a 20 |
| K-6 Progresión y recompensas (§2.2), sin contar los guardados versionados | Coop | 10 a 14 |
| K-7 Contenido de CP01 (colocar puzzles, enemigos, cajas; retoque a mano) | Coop | 20 a 28 |
| K-8 Retoque de C01 | Coop | 8 a 12 |
| K-9 Pruebas de reglas Coop (T-01 a T-07, T-10, T-40 a T-43) | Coop | 8 a 12 |
| **Subtotal Coop** | | **96 a 135** |
| **TOTAL de este documento** | | **≈ 253 a 346 h** |
| Plan F0-F2 (ya planificado, incluye terreno, rescate, mecánicas, vegetación, catálogo, CP01) | | ≈ 134 (110 a 160) |

Las 43 a 59 h de C-2 se repiten en el documento de 2 vs 2; no se suman dos veces. El contenido de mapa (K-7, K-8) se reduce en cuanto exista Foto→Mapa, que es otro plan.

### 12.2 Dependencias con el plan F0-F2

| Bloque de este documento | Depende de (tareas del plan) | Motivo |
|---|---|---|
| Todo el Coop | T13 a T14 (rejilla y DA), T16 (proveedor de suelo), T27 (construcción Prepared) | Sin `UTN_CoopMapData` no hay mapa |
| R-06, R-03, R-04, R-09 | T35 a T36 (`ATN_RunGameMode` → `ATN_MatchGameModeBase`), T3 (`OnTravelFailure`) | El DBNO y las vidas van en la base nueva; evitar tocar dos veces |
| §8.3 rescate | T15, T18, T19 | El vigilante, `CourseBackAt` y R1 |
| §5.2 mecánicas | T29 (`SpawnElement` desde marcadores) y T23 (marcadores) | Colocar catapultas, quads, tanque, gaviotas |
| O-32 vegetación | T30 | `ATN_BeachDecorField` con cota fija |
| Selector de mapa (U-08) | T31 | Catálogo y viaje |
| CP01 y contenido | T32, T33 (importación) y T34 (gate F2, manual) | Las pruebas de estrés empiezan tras el gate |
| K-7 contenido de CP01 | T32 | El terreno debe existir |
| E-12 y X-1 a X-4 | **Fusión de las tareas de otros agentes sobre `TN_BeachGiantCrab.*`, `TN_BeachEnemy.*`, `TN_BeachToyTank.*`** (F0-F2 las declara intocables) | Evitar pisar el trabajo |
| §2.2 persistencia | Guardados versionados (plan maestro §0.8, Steam) | Sin ello no se guardan cascos |
| §7.1 pantalla de carga | T22 (R8) | Reconocer el Coop |
| `ETNProcSelectorKind` | T35 (Clásico fuera) | El enum cambia dos veces si no se coordina |

### 12.3 Orden de trabajo recomendado

1. **C0 (tras T34):** K-1 y C-1 sobre la base nueva de T36 (≈ 28 h).
2. **C1:** C-2 en su orden de menor coste (N4 → N1 → M1 → M3 → M4 → M2 → N6 → N2 → N5 → N3) y K-2 (≈ 50 h).
3. **C2:** C-3, K-3, K-4 con los enemigos ya fusionados (≈ 55 h).
4. **C3:** C-4, K-5 y C-5 (≈ 55 h).
5. **C4:** K-7, K-8 y K-6 con el mapa importado (≈ 40 h).
6. **C5:** C-6, C-7, K-9 y el playtest de 4 (≈ 45 h).

## Anexo A. Preguntas abiertas y contradicciones (para el director)

| Id | Pregunta o contradicción | Recomendación |
|---|---|---|
| Q1 | **Contradicción de jugadoras**: el director fija 1 a 4; el catálogo de puzzles dice 3 a 8 y `Modos-UI-FX` §2.1 dice «Coop 1 a 8» | Fijar `MaxPlayers = 4` en Coop y corregir ambos documentos (R-01). Es lo que asume todo este texto |
| Q2 | **Carrera**: el plan maestro §3.2 conserva el generador de Mokius y el heightfield; el director ahora dice que Carrera es «casi lo mismo» con mapa recto y sin puzzles. ¿Pasa a usar los mapas volumétricos preparados? | Si sí, la Carrera hereda §3, §8.3 y §9 de este documento; si no, solo hereda §5.1, §5.2, §7 y §8.1 |
| Q3 | Revivir hoy es «por emote en rango» (`ATN_RunGameMode`); este documento propone «mantener Interactuar» (R-06). ¿Se acepta? | Sí; más claro y accesible |
| Q4 | **Vidas de equipo** (R-03) y **continuar tras la derrota** (R-04): ¿aceptan el coste (recompensa −50 %) o se quiere derrota definitiva? | Como está: la derrota definitiva frustra a un grupo tras 15 min |
| Q5 | Hardware de referencia de los presupuestos (§9) | Confirmar la gama mínima de Steam prevista |
| Q6 | **Puente de Mokius no existe** en `Content/` (restricciones globales del plan F0-F2): CP01 usa puentes naturales y `BridgeAnchor` queda listo | Aceptado |
| Q7 | Precios de la tienda y valor de las recompensas (§2.2) | Alinear con `ATN_ShopKeeper` en el primer playtest |
| Q8 | **Fuego amistoso apagado** (R-11) en Coop: ¿también en 2 vs 2 entre compañeras? El documento de 2 vs 2 ya dice que se puede lanzar a rivales | Sí, entre compañeras; sí a rivales en 2 vs 2 |
| Q9 | El plan maestro §3.1 nombra E01 y F01 como candidatos Coop, pero el Catálogo de mapas los dedica al Rally | Fuera del Coop (§3.1) |
| Q10 | E-12 y X-1 a X-4 tocan ficheros que el plan F0-F2 declara intocables | Esperar a su fusión (§12.2) |
| Q11 | `ATN_EnemySeagull` (E-11): se propone retirarlo del Coop y marcarlo como deprecado | Sí; duplica a `ATN_BeachGullZone` |
| Q12 | Guardados versionados como requisito de las recompensas persistentes | Prioridad P0 del plan Steam |

## Anexo B. Respuestas (2026-09-29, por delegación del director)

- Q1: Coop **1–4** jugadoras; 2 vs 2 exactamente 4; Carrera, Rally y Todos contra Todos 1–8. Se corrigen el catálogo de puzzles (plantillas Coop para 1–4) y el lobby.
- Q2: la Carrera **se queda con el generador de Mokius** (decisión del director); hereda de este documento las piezas comunes (enemigos, objetos, interfaz, efectos, red), no el mapa.
- Q3: revivir manteniendo interactuar sobre el huevo fantasma existente (`ATN_GhostEgg`); sin vidas de equipo; «continuar» tras derrota total con −50 % de conchas.
- Q10: las tareas E-12 y X-1..X-4 se incorporan como tareas nuevas al final del plan F0–F2 (no se tocan esos ficheros dentro de las tareas que los declaran intocables).
