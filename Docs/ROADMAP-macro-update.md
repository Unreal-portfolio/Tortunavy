# ROADMAP de la macro update de Tortunabo

Fecha: 2026-09-29 · Rama: `macro-update` · Lista maestra de tareas: pulido actual y desarrollo de la macro update. Sustituye como índice de trabajo a las listas sueltas de las specs; el detalle de cada tarea vive en el documento de la columna «Ref.».

Fuentes: 43 peticiones literales del director (sesión 7e8d81ee), `Docs/2026-09-29-Plan-Maestro-Modos-y-Mapas.md` (§7 respuestas), `Docs/superpowers/specs/modos/00–05`, `Docs/superpowers/specs/2026-09-29-foto-a-mapa-design.md`, `Docs/superpowers/plans/2026-09-29-F0-F2-Coop-Steam.md` (37 tareas), `Docs/Rally_*.md`, `Docs/Catalogo-Mapas-2026-09-29.md`, `Docs/Catalogo-Puzzles-2026-09-29.md`, `Docs/Modos-UI-FX-2026-09-29.md`, `Docs/Inventario-Objetos-Arte-2026-09-29.md`, `Docs/Limpieza-2026-09-29.md`, `Docs/Calidad-Codigo-2026-09-29.md`, `Docs/Analisis/2026-09-29/F_gaps_steam.md`, `Docs/Investigacion-8-Jugadores-Voz-2026-09-29.md`, `Docs/Reviews/nube/`, `Art/Library/IA/INDEX.md`, kanban `tortunabo` y memoria del proyecto (contrastada con el repositorio). Auditoría de cobertura: `Docs/Auditoria-Cobertura-2026-09-29.md`.

## 0. Añadido por la auditoría de cobertura

Tareas que no estaban en ningún plan o estaban sin horas, y decisiones que hay que confirmar:

| Id | Motivo |
|---|---|
| E2-24 | Coliseo con bordes de agua (P38): no aparecía en ningún documento ni script |
| E2-18 | Re-horneado del bake tras el retoque manual del terreno (P26–P27): el plan F0–F2 lo dejaba fuera (desviación 6) |
| E11-04 | Perfilado del render de los mapas (P07, «map rendering»): solo había presupuesto de malla |
| E11-05 | Coste de ítems y VFX con 8 (P07, «item optimization»): solo había pool de objetos |
| E0-16 | Fusión final de `macro-update` en `main` (P01, «juntarlo todo en el main»): ninguna tarea la cerraba |
| E5-12, E6-12, E7-12 | Aparición de objetos (P40–P42): decidida en los anexos de las specs, sin horas |
| E10-09 a E10-17 | Red a 8 y voz (P07 y P43): tareas de la investigación de 8 jugadores |
| E2-19 | El puente con física de Mokius (P01) no está en `Content/`: hay que pedírselo |
| E5-18 | Rasante volumétrica leve en la Carrera (P01): **condicional**, contradice la decisión 3 vigente |
| E6-29 | Opción E de red del Rally: pasa de condicional a MVP por la investigación de 8 jugadores |
| E0-17 | Sincronizar los documentos con las decisiones vigentes (27 contradicciones de la auditoría) |

Precisiones del director aplicadas (2026-09-29, tarde): el AppID propio se hace **al final** (E1-04, E1-05 en la ola 13; sigue el 480 y no bloquea nada), y `OnTravelFailure` es un **hueco de robustez** con Seamless Travel (`OnNetworkFailure` ya está enlazado en `MP_GameInstance.cpp:108`), no un bug confirmado: 2 h (E1-03).

## 1. Cómo se ejecuta

**Épicas.** E0 infraestructura y bugs · E1 Steam y producto · E2 terreno y mapas · E3 Foto→Mapa · E4 Coop · E5 Carrera · E6 Rally · E7 Todos contra Todos · E8 2 vs 2 · E9 física de la tortuga · E10 red a 8 y voz · E11 optimización y estrés · E12 interfaz y efectos comunes · E13 assets · E14 limpieza y calidad · E15 localización, logros y ajustes · E16 playtests y QA.

**Quién.** `UE` = agente `unreal-engine-engineer` (C++/BP); «agente Python» = implementador de `Scripts/` (terreno, generadores, pytest); `technical-artist` = Blender y láminas; `game-designer` = contenido, mapas y tramos; `test-automator` = tests; `devops-engineer` = CI, PR y fusiones; `code-reviewer`/`architect-reviewer` = revisión; `debugger` primero ante build rota o bug que resiste; **Rodrigo** = PIE, Steam, decisiones y aprobación de láminas; «equipo de arte» = modelos finales (0 h de agente).

**Carriles** (ficheros disjuntos; plan maestro §5): `PY` Python terreno · `CP` ProcMap/Coop · `PL` jugador · `BR` playa/Carrera · `NET` mecánicas de red y voz · `VH` vehículos · `TC` Todos contra Todos · `DV` 2 vs 2 · `FW` arquitectura común, bugs y estrés · `UI` interfaz · `ST` Steam/config · `CL` limpieza · `LOC` localización · `ART` arte · `DOC` documentación · `QA` playtests · `INT` integración.

**Reglas de sesión.**

1. Una sesión = un objetivo cerrado de ~4–6 h de agente dentro de una épica y un carril; se cierra con commit propio (convencional, uno por unidad) y la casilla del kanban.
2. Compilación: los agentes de sesiones C no compilan a la vez; un integrador compila `TortunaboEditor Win64 DebugGame` y pasa `Automation RunTests Tortunabo` al cerrar cada sesión C. Como máximo dos sesiones C en paralelo y siempre de carriles distintos. Las sesiones P (Python, arte, documentación) van en paralelo sin límite.
3. Serializar siempre: `TN_BeachGullZone`/`TN_BeachStun` (carriles CP y NET), `MP_GameInstance`, `TN_PauseMenuWidget`, `TortugaCharacter.h` y los `.umap` que toca Alvaro2rh.
4. Sesiones R: preparar antes lo automatizable y dejar a Rodrigo una lista de pasos de PIE o una lámina; el resultado se anota en el checklist de playtest.
5. Todo sistema nuevo nace con su categoría de log, su `TN.Debug.<Sistema>` y un test que falla sin él (00-Arquitectura §3).
6. Sesión que se queda sin turnos: estado y siguiente paso en un fichero, y nueva sesión con contexto limpio.

**Orden.** Las olas respetan dependencias; dentro de una ola, los carriles distintos avanzan en paralelo. Ruta crítica: E0-01 → F1 (E2-05…E2-17) → F2 (E4-01…E4-11) → arquitectura común (E0-09…E0-14) → reglas y puzzles del Coop → 2 vs 2. En paralelo desde la ola 2: tortuga (PL), red y voz (NET), Carrera (BR), Rally (VH) y todos los mapas (PY).


## 2. Totales

| Épica | Tareas | Hechas | MVP (h) | Después (h) | Condicional (h) | Sesiones |
|---|---|---|---|---|---|---|
| E0 Infraestructura, arquitectura común y sistema de bugs | 17 | 0 | 63–87 | 0 | 0 | 20 |
| E1 Steam y producto | 16 | 1 | 67,5–76,5 | 0 | 0 | 17 |
| E2 Terreno y mapas | 25 | 0 | 121,5–151,5 | 0 | 0 | 31 |
| E3 Foto→Mapa | 9 | 0 | 41–59 | 0 | 0 | 12 |
| E4 Coop | 29 | 0 | 263–339 | 0 | 0 | 64 |
| E5 Carrera | 18 | 0 | 133–175 | 20–29 | 16–24 | 41 |
| E6 Rally Tortuga | 31 | 0 | 337–465 | 151–219 | 0 | 119 |
| E7 Todos contra Todos | 20 | 0 | 153–217 | 68–110 | 0 | 58 |
| E8 2 vs 2 | 9 | 0 | 113–158 | 47–67 | 0 | 41 |
| E9 Física de la tortuga | 9 | 0 | 38–54 | 0 | 0 | 10 |
| E10 Red, sincronización hasta 8 y voz | 18 | 1 | 66–80 | 0 | 0 | 17 |
| E11 Optimización y estrés | 8 | 0 | 28–39 | 0 | 0 | 9 |
| E12 Interfaz y efectos comunes | 8 | 0 | 63–88 | 22–31 | 0 | 22 |
| E13 Assets (IA → modeladores) | 7 | 0 | 25–37 | 3–4 | 0 | 8 |
| E14 Limpieza y calidad de código | 13 | 0 | 80–120 | 0 | 0 | 22 |
| E15 Localización, logros y ajustes | 7 | 0 | 41–49 | 0 | 0 | 9 |
| E16 Playtests y QA | 12 | 0 | 88–92 | 0 | 0 | 19 |
| **Total** | **256** | **2** | **1721–2287** | **311–460** | **16–24** | **519** |

Global comprometido (MVP + Después, sin condicionales): **2032–2747 h**. Punto medio MVP ≈ 2004 h; con Después ≈ 2390 h. Sesiones de ~4–6 h: **519** (444 hasta la 1.0 y 75 de Después). Necesitan a Rodrigo: 52. Compilan UE: 442; sin compilar (Python, arte, documentación, manuales): 77.

Las horas son estimaciones de primera pasada de cada spec (sin depuración de red salvo donde se indica). Solape conocido sin descontar: interfaz y efectos comunes entre 01-Coop §7 y 05-2vs2 §13 (≈ 10–20 h), declarado en las propias specs. Las 16 h por iteración de playtest Steam salen de F_gaps_steam §4.1.


## 3. Tareas por épica

Estado: planificado, en curso, hecho o condicional (fuera del total hasta que el director lo confirme). Sesión: número de la sección 4 donde se hace.


### E0. Infraestructura, arquitectura común y sistema de bugs (63–87 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E0-01 | Integrar PR #9 (bugs de la nube) | Compilar DebugGame, pasar tests y fusionar en macro-update | 2–3 | — | UE + Rodrigo (PIE 4P) | Build verde, tests Tortunabo.* verdes, PIE 4P sin errores nuevos en log | Docs/Reviews/nube/integracion-2026-09-29.md | planificado | MVP | 1 |
| E0-02 | Categorías de log por sistema | LogTNMatch, LogTNItems, LogTNShell, LogTNNet, LogTNRally, LogTNPuzzle con prefijo de máquina | 2–3 | E0-01 | UE | `log LogTNItems Verbose` filtra; cada línea lleva Servidor/Cliente N | specs/modos/00 §2 | planificado | MVP | 13 |
| E0-03 | CVars TN.Debug.<Sistema> | Dibujo de estado en pantalla por sistema (patrón TN.Shell.Debug) | 3–4 | E0-02 | UE | ≥ 5 sistemas con `TN.Debug.X 1` sin recompilar | specs/modos/00 §2 | planificado | MVP | 13 |
| E0-04 | Invariantes con ensureMsgf | Dos dueños, pawn bajo el terreno, fase sin salida | 2–3 | E0-02 | UE | Test que fuerza cada invariante y deja traza con estado completo | specs/modos/00 §2 | planificado | MVP | 14 |
| E0-05 | Detector de desincronía | Hash del estado crítico cada 2 s; el cliente registra [Desync] con el campo | 8–10 | E0-02 | UE | Desfase provocado por cvar → [Desync] con el campo correcto en PIE 2P | specs/modos/00 §2; Investigacion-8-Jugadores §7 #9 | planificado | MVP | 15,16 |
| E0-06 | Informe de bug con F8 | Captura, 2000 líneas de log, JSON de GameState y jugador, semilla, mapa, commit | 4–6 | E0-02 | UE | F8 crea Saved/BugReports/<fecha>/ con los 5 ficheros en PIE y Shipping | specs/modos/00 §2 | planificado | MVP | 17 |
| E0-07 | Repeticiones en playtest | demorec/demoplay documentados y cvar de grabación automática | 1–2 | — | UE | Una partida de 4 grabada y reproducida | specs/modos/00 §2 | planificado | MVP | 18 |
| E0-08 | Telemetría de playtest a CSV | Muertes, rescates, objetos, tiempos de tramo | 3–4 | E0-14 | UE | CSV con ≥ 6 tipos de evento tras una partida | specs/modos/00 §2 | planificado | MVP | 73 |
| E0-09 | Máquina de fases de partida | ETNMatchPhase Lobby→Carga→Cuenta→Juego→Cierre→Resultados→Siguiente, replicada y con tiempo máximo | 6–8 | E4-11 | UE | Test: cada fase sale por tiempo máximo; fase replicada en 4 máquinas | specs/modos/00 §1 | planificado | MVP | 74,75 |
| E0-10 | Estrategia de modo UTN_ModeRules | Coop base; Carrera y 2 vs 2 heredan; el GameMode no conoce modos | 8–10 | E0-09 | UE | Coop y Carrera arrancan con su UTN_ModeRules; 0 switch por modo en el núcleo | specs/modos/00 §1 | planificado | MVP | 76,77 |
| E0-11 | DataAssets de datos | UTN_ItemDef, UTN_PuzzleTemplate, UTN_ModeDef, UTN_MapDef | 6–8 | E0-10 | UE | Un objeto nuevo = un DataAsset; test de carga de los 4 tipos | specs/modos/00 §1 | planificado | MVP | 78,79 |
| E0-12 | Mensajería de gameplay | Objeto usado, jugadora fuera, puzzle resuelto, sabotaje; UI/FX/telemetría escuchan | 4–6 | E0-10 | UE | Gameplay sin llamadas directas a UI en los 4 eventos | specs/modos/00 §1 | planificado | MVP | 80 |
| E0-13 | Interfaz ITN_Activatable | Placas, palancas, botones → puertas, géiseres, plataformas | 3–4 | E0-11 | UE | Placa activa puerta y géiser sin código específico (test) | specs/modos/00 §1 | planificado | MVP | 81 |
| E0-14 | Pool de objetos | Proyectiles, cajas, efectos, marcas de rueda | 3–4 | E0-11 | UE | 0 spawn/destroy por disparo en stat; tamaño de pool por mapa | specs/modos/00 §1 | planificado | MVP | 82 |
| E0-15 | Test de partida completa headless por modo | Bots de principio a fin, uno por modo | 4–6 | E0-10 | test-automator | 1 test verde por modo implementado | specs/modos/00 §2 | planificado | MVP | 83 |
| E0-16 | Fusión final macro-update → main | PR con historial completo, resumen y plan de pruebas | 3–4 | E16-12 | devops-engineer + Rodrigo | PR aprobada, CI verde, main compila | CLAUDE global (Git) | planificado | MVP | 412 |
| E0-17 | Sincronizar documentos con las decisiones vigentes | Jugadoras por modo, cierres de carrera, nº de modos, assets imprescindibles, objetos TcT, torbellino, Mapa01 en memoria | 1–2 | — | agente de documentación | 0 contradicciones abiertas de la tabla C de la auditoría | Docs/Auditoria-Cobertura-2026-09-29.md §3 | planificado | MVP | 2 |

### E1. Steam y producto (67,5–76,5 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E1-01 | F0-T1 Guardia de cocinado | Pytest que exige mapas de juego y /Game/Coop en el paquete | 1 | — | UE | Pytest rojo si falta LVL_Lobby o LVL_BeachRace | plans/F0-F2 T1 | planificado | MVP | 3 |
| E1-02 | F0-T2 Source/Logs fuera del repo | Verificación | 0 | — | — | Hecho en macro-update | plans/F0-F2 T2 | hecho | MVP | — |
| E1-03 | F0-T3 OnTravelFailure (hueco de robustez) | Seamless Travel: enlazar GEngine->OnTravelFailure (OnNetworkFailure ya está, MP_GameInstance.cpp:108) y volver al lobby con aviso | 2 | E0-01 | UE | Viaje a mapa inexistente → lobby/menú con aviso en 2 máquinas | plans/F0-F2 T3 | planificado | MVP | 3 |
| E1-04 | F0-T4 AppID configurable y bloqueo del 480 | Más tarde (decisión del director): sigue el 480 hasta entonces; no bloquea nada | 3 | — | UE | Shipping con 480 falla al arrancar; filtro eliminado | plans/F0-F2 T4 | planificado | MVP | 413 |
| E1-05 | Trámite Steamworks | Más tarde (decisión del director): cuenta, pago y AppID propio; no bloquea nada | 2–4 | — | Rodrigo | AppID propio asignado | Plan maestro §4 P0 | planificado | MVP | 413 |
| E1-06 | F0-T5 N-A depuración remota | RPC del fantasma solo en editor o con cvar | 1,5 | — | UE | Invitado en Development no ejecuta comandos sin cvar | plans/F0-F2 T5 | planificado | MVP | 3 |
| E1-07 | F0-T7 CI local | build_check.bat relativo, BuildCookRun + Automation por PR a macro-update | 8 | E1-01 | devops-engineer | CI verde con los 97 tests en un PR de prueba | plans/F0-F2 T7 | planificado | MVP | 4,5 |
| E1-08 | F0-T8 Playtest Shipping con dos máquinas | Lobby → carrera → menú en build empaquetada | 4 | E1-01,E1-03,E1-07 | Rodrigo + UE | Checklist completo sin cuelgues | plans/F0-F2 T8 | planificado | MVP | 7 |
| E1-09 | Cocinado completo verificado | Cook entero y revisar LoadObject fallidos (Limpieza 2.8, Calidad H4) | 2–3 | E1-01 | UE | 0 «Failed to load» en la build cocinada | Limpieza §2.8; Calidad H4 | planificado | MVP | 6 |
| E1-10 | N-D ServerGoToStation validado | Validar índice en el servidor | 1 | — | UE | Índice fuera de rango rechazado (test) | F_gaps_steam §3 | planificado | MVP | 6 |
| E1-11 | Guardado asíncrono y Steam Cloud | Resto de P2 de guardados (versión ya hecha en 0d393c6f6) | 3–4 | — | UE | Guardado sin bloqueo del hilo; Cloud activo en Steamworks | F_gaps_steam #4 | planificado | MVP | 414 |
| E1-12 | Steam Deck y mando | Teclado virtual para el código de sala, glifos, prueba en Deck | 20 | E1-08 | UE + Rodrigo (Deck) | Partida completa en Deck solo con mando | F_gaps_steam #7 | planificado | MVP | 415,416,417,418 |
| E1-13 | Créditos y licencias | Noto OFL, audio de terceros, fuentes MDE, HellYeah | 8 | — | UE | Pantalla de créditos con todas las fuentes listadas | F_gaps_steam #12; Plan §6 | planificado | MVP | 419,420 |
| E1-14 | Icono y splash | Build/Windows con .ico y splash | 4 | E13-05 | UE + equipo de arte | Shipping muestra icono y splash propios | F_gaps_steam #9 | planificado | MVP | 421 |
| E1-15 | Página de tienda | Textos en 13 idiomas, capturas, tráiler, cápsulas | 6–10 | E13-05 | Rodrigo + game-designer | Página enviada a revisión de Valve | Plan maestro §3.6 | planificado | MVP | 422,423 |
| E1-16 | Perfil cosmético por cuenta de Steam (L3) | Slot por SteamID en vez de _Local | 2–3 | — | UE | Dos cuentas en el mismo PC no comparten desbloqueos | Calidad L3 | planificado | MVP | 424 |

### E2. Terreno y mapas (121,5–151,5 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E2-01 | F1-T9 BFS con padres | Alcance con cota mínima y camino ordenado de P01 | 4 | — | agente Python | Pytest del camino ordenado verde | plans/F0-F2 T9 | planificado | MVP | 19 |
| E2-02 | F1-T10 Máscara y bake.bin | Muestreo del camino, ancho, safe_low/high | 4 | E2-01 | agente Python | Pytest del formato TNCB verde | plans/F0-F2 T10 | planificado | MVP | 20 |
| E2-03 | F1-T11 Manifest v2 (C01, P01) | Bloque coop y gen_terrain_coop.py | 5 | E2-02 | agente Python | Manifest v2 de C01 y P01 validado | plans/F0-F2 T11 | planificado | MVP | 21 |
| E2-04 | F1-T12 Decimado cuádrico | Presupuesto de malla con borde fijo | 5 | — | agente Python | C01 ≤ 1,1 M tri sin grietas en costuras | plans/F0-F2 T12 | planificado | MVP | 22 |
| E2-05 | F1-T13 Rejilla horneada C++ | ParseBake, IsAllowed, suelo en túnel | 4 | E0-01 | UE | Tests IsAllowed y WalkableZAt en túnel verdes | plans/F0-F2 T13 | planificado | MVP | 32 |
| E2-06 | F1-T14 UTN_CoopMapData | LoadFromManifest | 5 | E2-05 | UE | Test de carga del manifest de C01 | plans/F0-F2 T14 | planificado | MVP | 33 |
| E2-07 | F1-T15 Vigilante puro | Lógica de rescate sin UObject | 3 | — | UE | Tests StepWatch y PickRescueSlot verdes | plans/F0-F2 T15 | planificado | MVP | 34 |
| E2-08 | F1-T16 Proveedor de suelo y Source=Prepared | ITN_SafeGroundProvider y subsistema | 5 | E2-05,E2-06,E2-07 | UE | Subsistema resuelve el proveedor en C01 | plans/F0-F2 T16 | planificado | MVP | 35 |
| E2-09 | F1-T17 FindOpenSandSpot consulta al proveedor | — | 3 | E2-08 | UE | Test de aparición en túnel sobre el suelo | plans/F0-F2 T17 | planificado | MVP | 36 |
| E2-10 | F1-T18 Vigilante de servidor y gaviotas por el camino | CourseBackAt | 4 | E2-07,E2-08 | UE | 20 lanzamientos fuera de zona → 20 rescates ≤ 1,75 s | plans/F0-F2 T18 | planificado | MVP | 37 |
| E2-11 | F1-T19 R1 terreno por etiqueta | — | 1,5 | E2-05 | UE | Ningún pawn congelado > 1 s al aparecer | plans/F0-F2 T19 | planificado | MVP | 37 |
| E2-12 | F1-T20 R2/R3 meta orientada y a cota | — | 2 | E2-11 | UE | Meta válida hacia −X y +Y (test) | plans/F0-F2 T20 | planificado | MVP | 38 |
| E2-13 | F1-T21 R5 SpawnFauna extraída | — | 3 | — | UE | Fauna sin Layout.Modules (test) | plans/F0-F2 T21 | planificado | MVP | 38 |
| E2-14 | F1-T22 R8 pantalla de carga reconoce Coop | — | 1,5 | — | UE | Pantalla de carga correcta al viajar a LVL_Coop_C01 | plans/F0-F2 T22 | planificado | MVP | 39 |
| E2-15 | F1-T23 Marcadores y ancla | ATN_CoopMarker, ATN_CoopMapAnchor | 2,5 | E2-11 | UE | Marcadores con id estable tras reimportar | plans/F0-F2 T23 | planificado | MVP | 39 |
| E2-16 | F1-T24 Importador headless | Subniveles _Terrain/_Markers/_Design, lápidas, TN_REGENERATE | 7 | E1-01,E2-03,E2-06,E2-08,E2-15 | UE + agente Python | Reimportar no mueve marcadores ni resucita borrados | plans/F0-F2 T24 | planificado | MVP | 40,41 |
| E2-17 | F1-T25 R9 y smoke de LVL_Coop_C01 (gate F1) | Colisión de la malla fuente | 2 | E2-16 | UE | Smoke headless sin ProcMesh; ningún cliente cocina colisión | plans/F0-F2 T25 | planificado | MVP | 41 |
| E2-18 | Re-horneado tras retoque manual del terreno | RebakeHeightsFromLevel: bake y máscara desde el terreno que editan los modeladores | 8–12 | E2-17 | UE + agente Python | Terreno editado a mano → bake nuevo; rescate correcto en la zona editada | Foto→Mapa §5; P26–P27 | planificado | MVP | 62,63 |
| E2-19 | Puente de Mokius: localizar el asset con física | No está en Content; pedirlo a Mokius e importarlo | 1–2 | — | Rodrigo + Mokius | Asset en /Game con anclajes inicio/fin medidos | Plan §7.4; Coop Q6 | planificado | MVP | 42 |
| E2-20 | ATN_BridgeSpan sobre el puente de Mokius | Puente entre anclajes, cortable, replicado | 16–24 | E2-19 | UE | Puente de 70 m colocado por marcador; corte igual en 4 máquinas | Plan §5 P4; TcT §3.5 | planificado | MVP | 224,225,226,227 |
| E2-21 | Países enteros miniaturizados (13) | Terminar country.py/countries.py (hoy 6 de 13) y validarlos | 10–14 | — | agente Python | 13 países con lámina y validadores verdes | Catálogo §Decisión países; P33 | en curso | MVP | 23,24,25 |
| E2-22 | Japón y Filipinas v2 | Relieve real de Japón; Filipinas adaptada a TcT/Rally sin elevar relieve | 4–6 | E2-21 | agente Python | Láminas L10_japon_v2 e I01_filipinas_v2 aprobadas por Rodrigo | P30–P31 | en curso | MVP | 26 |
| E2-23 | Mapas inventados N01-N16 | Kit de formas con semilla (lote C1 A02-A06 hecho) | 16–24 | — | agente Python | 16 mapas con lámina y validadores verdes | gen_terrain_inventados.py; P38 | en curso | MVP | 27,28,29,30 |
| E2-24 | Coliseo con bordes de agua | Arena TcT redonda con gradas y foso de agua | 4–6 | E2-23 | agente Python | Mapa con lámina, 8 nidos y validadores verdes | P38 (OLVIDADO) | planificado | MVP | 31 |
| E2-25 | Actualizar el Catálogo de mapas | Países enteros, lote N, coliseo, estados reales | 1–2 | E2-21,E2-23 | agente de documentación | Catálogo sin contradicciones con specs 03/04 | Catalogo-Mapas | planificado | MVP | 353 |

### E3. Foto→Mapa (41–59 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E3-01 | Esquema y validador de MapSketch.json | schema.py y validate.py | 4–6 | E2-03 | agente Python | Pytest con 5 bocetos válidos y 5 inválidos | Foto→Mapa §4, §7 | planificado | MVP | 354 |
| E3-02 | Análisis de imagen → boceto | Guía y ejemplos para que Claude lea la foto y escriba MapSketch.json | 4–6 | E3-01 | game-designer + agente Python | 3 fotos → 3 bocetos válidos revisados por Rodrigo | Foto→Mapa §3 | planificado | MVP | 355 |
| E3-03 | Adaptador to_path | Boceto → generador camino | 4–6 | E3-01 | agente Python | Boceto de prueba → mapa camino con validadores verdes | Foto→Mapa §7 | planificado | MVP | 356 |
| E3-04 | Adaptadores to_island y to_arena | — | 6–8 | E3-01 | agente Python | Un boceto de isla y uno de arena generan mapa válido | Foto→Mapa §7 | planificado | MVP | 357,358 |
| E3-05 | Expansor de marcadores y plantillas de puzzle | markers.py | 6–8 | E3-01,E0-11 | agente Python | Las 15 plantillas del catálogo se expanden a actores | Foto→Mapa §6-7; Catálogo puzzles §7 | planificado | MVP | 359,360 |
| E3-06 | Montaje del nivel desde el boceto | build_map_from_sketch.py headless: LVL_<id>, carpetas, FBX | 8–12 | E2-16,E3-05 | UE + agente Python | Boceto → nivel abrible con marcadores en su carpeta | Foto→Mapa §7 | planificado | MVP | 363,364 |
| E3-07 | Pruebas de Foto→Mapa | — | 3–4 | E3-06 | test-automator | Pruebas de §9 verdes | Foto→Mapa §9 | planificado | MVP | 361 |
| E3-08 | Piloto: boceto de Rodrigo → mapa Coop | Mapa Coop a partir de un dibujo del director | 4–6 | E3-06 | game-designer + Rodrigo (lámina) | Rodrigo aprueba la lámina; recorrible en PIE | Foto→Mapa §1; P22 | planificado | MVP | 362 |
| E3-09 | Guía para diseñadores y modeladores | Generar → retocar → re-hornear, sin que regenerar pise lo diseñado | 2–3 | E2-18,E3-06 | agente de documentación | Guía en Docs con el flujo completo | P24–P27 | planificado | MVP | 365 |

### E4. Coop (263–339 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E4-01 | F2-T26 Layout sintético | LayoutFromCoopData, R7 | 4 | E2-15 | UE | Test del layout desde el DA | plans/F0-F2 T26 | planificado | MVP | 64 |
| E4-02 | F2-T27 Construcción Prepared | R4, PreparedMapId, agua, fauna | 6 | E2-12,E2-13,E2-17,E4-01 | UE | PreparedMapId alterado → error en log | plans/F0-F2 T27 | planificado | MVP | 65 |
| E4-03 | F2-T28 R6 SeaNear desde la rejilla | — | 2 | E4-02 | UE | Test de SeaNear en C01 | plans/F0-F2 T28 | planificado | MVP | 66 |
| E4-04 | F2-T29 Mecánicas desde marcadores y auditoría R10 | SpawnElement: catapulta, tanque, quad, trampolín, gaviotas | 3 | E4-02 | UE | Cada mecánica activada en PIE 4P sin Find nulo | plans/F0-F2 T29 | planificado | MVP | 66 |
| E4-05 | F2-T30 Vegetación en ATN_BeachDecorField | — | 4 | E4-02 | UE | Vegetación a cota fija en C01 | plans/F0-F2 T30 | planificado | MVP | 67 |
| E4-06 | F2-T31 Catálogo Coop, elección y viaje | UTN_CoopMapCatalog, TravelURL | 5 | E4-02 | UE | Lobby → LVL_Coop_C01 y CP01 | plans/F0-F2 T31 | planificado | MVP | 68 |
| E4-07 | F2-T32 CP01 (mapa propio de Claude) | 600 × 600 m con generador camino y puentes naturales | 8 | E2-03,E2-04 | agente Python + game-designer | CP01 ≤ 1,1 M tri y recorrible | plans/F0-F2 T32; P02 | planificado | MVP | 43,44 |
| E4-08 | F2-T33 Importar CP01 y C01 | — | 3 | E2-16,E4-06,E4-07 | UE | Ambos en el catálogo y cocinados | plans/F0-F2 T33 | planificado | MVP | 69 |
| E4-09 | F2-T34 PIE 4P gate F2 | PktLag 150 | 6 | E4-03,E4-04,E4-05,E4-08 | Rodrigo + UE | Aceptación del plan §3.1 completa | plans/F0-F2 T34 | planificado | MVP | 72 |
| E4-10 | F2-T35 Clásico fuera del selector | LVL_Run sale del cocinado | 2 | E1-01 | UE | Selector sin Clásico; cook sin LVL_Run | plans/F0-F2 T35 | anulada (Decisión en #143: Supervivencia conserva LVL_Run y los chunks) | MVP | 69 |
| E4-11 | F2-T36 ATN_MatchGameModeBase | Run → base abstracta; rescatar ReviveImmunitySeconds | 3 | E4-10 | UE | ProcMap y BeachRace heredan de la base nueva; tests verdes | plans/F0-F2 T36 | planificado | MVP | 70 |
| E4-12 | F2-T37 LVL_Demo01 fuera | Y resolver el stash wip LVL_Demo01 | 1 | E4-08 | UE | Mapa y script borrados; stash resuelto | plans/F0-F2 T37 | planificado | MVP | 70 |
| E4-13 | Botones de mapa en la pizarra del general | Desviación 3 del plan F0-F2 | 4 | E4-06 | UE | Elegir mapa Coop sin consola | plans/F0-F2 desviación 3 | planificado | MVP | 71 |
| E4-14 | C-1 Revivir y DBNO | Interactuar sobre el huevo fantasma | 8–10 | E4-11 | UE | Tests de revivir verdes; revivir en PIE 4P | 01-Coop §1.3, §12 | planificado | MVP | 154,155 |
| E4-15 | K-1 Reglas Coop | Derrota, victoria, continuar (−50 %), dificultad, estado | 18–24 | E0-10,E4-14 | UE | Tests T-01 a T-07 verdes | 01-Coop §12 | planificado | MVP | 156,157,158,159 |
| E4-16 | C-2a Piezas de puzzle N4, N1, M1, M3 | Orden de menor coste | 22–30 | E0-13 | UE | Cada pieza con test y en una plantilla del catálogo | 01-Coop §6.2; Catálogo puzzles §2 | planificado | MVP | 160,161,162,163,164 |
| E4-17 | C-2b Piezas de puzzle M4, M2, N6, N2, N5, N3 | — | 21–29 | E4-16 | UE | Las 15 plantillas montables por datos | 01-Coop §6.2; Catálogo puzzles §2 | planificado | MVP | 165,166,167,168,169 |
| E4-18 | K-2 Escala de puzzles por jugadoras | P-1, P-2 | 6–9 | E4-17 | UE | Plantillas válidas con 1, 2, 3 y 4 jugadoras (test) | 01-Coop §6.3 | planificado | MVP | 170,171 |
| E4-19 | Fusión de enemigos intocables (E-12, X-1..X-4) | Tras la fusión de TN_BeachGiantCrab/Enemy/ToyTank | 2–3 | E4-09 | UE + Rodrigo (coordina) | Ficheros liberados sin conflicto | 01-Coop Q10 | planificado | MVP | 172 |
| E4-20 | C-3 Enemigos X-1 a X-4 y ajustes | ≈ 16 ajustes por enemigo y O-06 | 35–43 | E4-19 | UE | Cada enemigo con test de decisión y visto en PIE | 01-Coop §4, §12 | planificado | MVP | 173,174,175,176,177,178,179,180 |
| E4-21 | K-3 Cangrejo guardián | E-12 | 8–10 | E4-20 | UE | Guardián escala con jugadoras (test) | 01-Coop §4.3 | planificado | MVP | 181,182 |
| E4-22 | K-4 Reserva de la caja | FTNCoopLootPool | 4–6 | E0-11 | UE | Test de reparto de la reserva | 01-Coop §5 | planificado | MVP | 183 |
| E4-23 | K-5 Interfaz Coop | §7.2 | 14–20 | E12-02 | UE | Pantallas de §7.2 con estado replicado | 01-Coop §7.2 | planificado | MVP | 203,204,205,206 |
| E4-24 | K-6 Progresión y recompensas | Cascos y conchas persistentes | 10–14 | E4-15 | UE | Recompensa persiste tras reiniciar | 01-Coop §2.2 | planificado | MVP | 184,185,186 |
| E4-25 | C-6 Red, presupuestos e instrumentación del Coop | Pooling en E0-14 | 16–22 | E4-15 | UE | Presupuestos de §9 cumplidos en PIE 4P | 01-Coop §8-9 | planificado | MVP | 187,188,189,190 |
| E4-26 | C-7 Pruebas comunes | T-08 a T-12, T-20 a T-32 | 12–16 | E4-17 | test-automator | Tests verdes | 01-Coop §10 | planificado | MVP | 191,192,193 |
| E4-27 | K-7 Contenido de CP01 | Puzzles, enemigos, cajas y retoque a mano | 20–28 | E4-17,E4-20 | game-designer + UE | Duración 12-18 min con 3 jugadoras (telemetría) | 01-Coop §3.3, §12 | planificado | MVP | 194,195,196,197,198 |
| E4-28 | K-8 Retoque de C01 | — | 8–12 | E4-27 | game-designer + UE | C01 cumple las reglas medibles de §3.4 | 01-Coop §3.4 | planificado | MVP | 199,200 |
| E4-29 | K-9 Pruebas de reglas Coop | T-01..T-07, T-10, T-40..T-43 | 8–12 | E4-15 | test-automator | Tests verdes | 01-Coop §10 | planificado | MVP | 201,202 |

### E5. Carrera (133–175 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E5-01 | F0-T6 Dorado T0 del reparto | 24 semillas × 3 dificultades | 3 | — | UE | Test dorado verde antes de tocar el reparto | plans/F0-F2 T6; 02-Carrera §11 | planificado | MVP | 8 |
| E5-02 | T1 Correcciones P1-P3 | Castillo tras sprint, lanzadores huérfanos, arcos de catapulta | 13–18 | E5-01 | UE | 72 semillas: castillo intacto y 0 huérfanos | 02-Carrera §7.2 | planificado | MVP | 84,85,86 |
| E5-03 | T2 Assets de tramo y curso | UTN_BeachStretchAsset, UTN_BeachRaceCourseAsset | 6–8 | E5-02 | UE | Tramo cargable desde DataAsset (test) | 02-Carrera §3.2 | planificado | MVP | 87,88 |
| E5-04 | T3 PlaceAuthored | Intervalos, rol Authored, pasadas fijas, máscara | 12–16 | E5-03 | UE | Curso vacío → reparto idéntico bit a bit | 02-Carrera §3.3 | planificado | MVP | 89,90,91 |
| E5-05 | T4 Red de tramos | Selección con enfriamiento y hash | 6–8 | E5-04 | UE | Hash distinto → aviso; 4 máquinas iguales | 02-Carrera §7.1 | planificado | MVP | 92,93 |
| E5-06 | T5 Editor de tramos | Proxy, volumen, Export/Import/Validate | 16–22 | E5-04 | UE | Tramo que corta el paso rechazado al exportar | 02-Carrera §7.4 | planificado | MVP | 94,95,96,97 |
| E5-07 | T6 Tests de tramos, curso fijo y sprint | — | 6–8 | E5-05 | test-automator | Tests verdes | 02-Carrera §9 | planificado | MVP | 98,99 |
| E5-08 | T7 Pulido P4-P8 | Ritmo, densidad, halo, equidad de las 2 filas, métricas | 29–40 | E5-07 | UE | Cobertura primer tercio ≥ 66 %; equidad de 8 huevos (test) | 02-Carrera §3.5, §9 | planificado | MVP | 100,101,102,103,104,105,106 |
| E5-09 | T9 P9 subflujos de RNG y nuevo dorado | — | 4–6 | E5-08 | UE | Nuevo dorado en commit aparte | 02-Carrera §11 | planificado | MVP | 107 |
| E5-10 | Objetos N1 y N2 | Actores, sonidos, pesos, pruebas | 14–18 | E5-02 | UE | Ambos objetos en PIE 4P con atribución | 02-Carrera §5.2 | planificado | MVP | 108,109,110 |
| E5-11 | Atribución del atacante | Struct + HUD | 2–3 | E5-10 | UE | Aviso de aturdimiento con nombre del atacante | 02-Carrera §6 | planificado | MVP | 111 |
| E5-12 | Aparición de objetos en la Carrera | Reaparición 5 s/15 s y caja de remontada con gaviota | 6–8 | E5-10 | UE | Caja cae ante las 2 últimas cada 30-40 s (telemetría) | 02-Carrera anexo; P41 | planificado | MVP | 111,112 |
| E5-13 | Proveedor de suelo seguro en la playa | ATN_BeachRaceGenerator implementa ITN_SafeGroundProvider | 3–4 | E2-08,E5-04 | UE | Rescate de la playa por el proveedor (test) | plans/F0-F2 desviación 1 | planificado | MVP | 113 |
| E5-14 | Contenido: 6 tramos del primer lote | Con el editor T5 | 12 | E5-06 | game-designer | 6 tramos validados y en rotación | 02-Carrera §3.4 | planificado | MVP | 114,115,116 |
| E5-15 | Tope de conchas con 6+ jugadores | Si la partida pasa de 25 min | 1 | E16-06 | UE | Partida de 8 ≤ 25 min | 02-Carrera P2 | planificado | MVP | 425 |
| E5-16 | T8 Mallas y BP propios de tramo | — | 14–20 | E5-14 | UE + equipo de arte | Tramos con seña visual propia | 02-Carrera §11 | planificado | Después | 445,446,447,448 |
| E5-17 | N3 y ayuda de primer uso | Cubo tapón y tooltip | 6–9 | E5-10 | UE | — | 02-Carrera §11 | planificado | Después | 449,450 |
| E5-18 | Rasante volumétrica leve en la Carrera | P01 literal; solo si Rodrigo reabre la decisión 3 | 16–24 | — | UE + agente Python | Recta con cambio de rasante sin romper el dorado | P01; Plan §1 dec. 3 | condicional | Después | — |

### E6. Rally Tortuga (337–465 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E6-01 | B1 Port del núcleo del buggy sin Cargo | HYBuggy → ATN_Buggy en Vehicles/ | 6–8 | E0-01 | UE | 0 referencias a Cargo/Chip/Token; compila | 03-Rally §3.1, §13 | planificado | MVP | 126,127 |
| E6-02 | B1 BuggySpec y buggy_measure | — | 3–4 | E6-01 | test-automator | Tests verdes y medida en pista de prueba | 03-Rally §11.1 | planificado | MVP | 128 |
| E6-03 | B1 Física síncrona con subpasos | MaxSubstepDeltaTime 0,016667 | 4–6 | E6-02 | UE | Caparazón, catapulta y trampolín sin regresión | 03-Rally §3; Plan §3.3 | planificado | MVP | 129 |
| E6-04 | B1 Pawn buggy con tortuga visual | UTN_CosmeticLook | 8–12 | E6-01 | UE | 4 buggies en PIE con tortugas visibles | 03-Rally §3 | planificado | MVP | 130,131 |
| E6-05 | B1 Sync de turbo y mina | — | 6 | E6-04 | UE | Correcciones < 50 cm tras turbo (p.NetShowCorrections) | 03-Rally §9 | planificado | MVP | 132 |
| E6-06 | B3 Trazado, puertas, vueltas, posiciones | — | 16–20 | E6-04 | UE | Posiciones correctas con 4 buggies (test) | 03-Rally §5.1-5.3 | planificado | MVP | 133,134,135,136 |
| E6-07 | B3 Dirección contraria y atajos ilegales | TNRally::IsWrongWay puro | 8–12 | E6-06 | UE | Test: contramano 1,5 s a > 20 km/h → aviso | 03-Rally §5.4-5.5; Rally_Sistemas | planificado | MVP | 137,138 |
| E6-08 | B3 Enderezar, reaparición y anti-vuelco | Flip a 4 s, mar/KillZ → último arco | 10–14 | E6-06 | UE | Buggy volcado se endereza en ≤ 4 s | 03-Rally §5.6-5.7 | planificado | MVP | 139,140,141 |
| E6-09 | B3 Salida, fin (20 s), rebufo y copa | Parrilla invertida por puntos; contrarreloj con 1 | 11–14 | E6-06 | UE | Copa de 3 carreras completa en PIE | 03-Rally §5.8-5.9, anexo C | planificado | MVP | 142,143,144 |
| E6-10 | B4 Colisiones, atropello, fricción y cámara | — | 18–25 | E6-04 | UE | Fricción por material medida en buggy_measure | 03-Rally §13 | planificado | MVP | 145,146,147,148 |
| E6-11 | 4 ítems sobre vehículo | Coconut, SandMine, HomingCrab, Sunscreen; RollLoot por puesto | 10–14 | E6-06 | UE | Cada ítem cumple su fila de autoridad en PIE 4P | Plan §3.3; 03-Rally §6.2 | planificado | MVP | 149,150,151 |
| E6-12 | Aparición de cajas del Rally | Filas 3 s, dobles 10 s, caja de remontada | 6–8 | E6-11 | UE | Filas reaparecen a 3 s (test) | 03-Rally anexo D | planificado | MVP | 152,153 |
| E6-13 | F6b Ruta por hitos, tallado y checkpoints | Corredor 12-16 m, pendiente ≤ 12°, checkpoints_uu | 30–45 | E2-03 | agente Python | Validador del corredor verde en E01B | Plan §3.3; 03-Rally §4.3 | planificado | MVP | 119,120,121,122,123,124,125 |
| E6-14 | F6b Presupuesto de malla del Rally | Voxel adaptativo, colisión simple decimada, streaming | 20–30 | E6-13,E2-04 | agente Python + UE | ≤ 12 MB por mapa y ≤ 1 M tri/km² | Plan §2.5; 03-Rally anexo C | planificado | MVP | 301,302,303,304,305 |
| E6-15 | B9 E01B completo | Túnel, puente, cauce y piloto IA | 24–34 | E6-13,E6-08 | UE + agente Python | Piloto IA: 0 atascos y 0 vuelcos en 10 recorridos | 03-Rally §4.3, §13 | planificado | MVP | 307,308,309,310,311,312 |
| E6-16 | B9 Lazo, circuito, Laps y ventana de arco | — | 12–16 | E6-15 | UE | I03-R con 1 vuelta completa | 03-Rally §5.2 | planificado | MVP | 313,314,315 |
| E6-17 | B9 L10 Japón Rally | — | 8–12 | E6-16,E2-22 | agente Python + UE | Validador del corredor verde | 03-Rally §4.2 | planificado | MVP | 316,317 |
| E6-18 | B9 I03-R Tortuga Magna | — | 8–12 | E6-16 | agente Python + UE | Circuito con lazo recorrible | 03-Rally §4.2 | planificado | MVP | 318,319 |
| E6-19 | B2 Asientos biplaza | Conductora y artillera | 12–16 | E6-06 | UE | 2 tortugas en un buggy en 4 máquinas | 03-Rally §3.3; Plan §7.2 | planificado | MVP | 320,321,322 |
| E6-20 | B2 A1 Lanzar (artillera) | — | 8–12 | E6-19,E6-11 | UE | La artillera lanza los 4 ítems | 03-Rally §3.4 | planificado | MVP | 323,324 |
| E6-21 | B2 A2 Contrapeso | — | 10–14 | E6-19 | UE | Anti-vuelco +0,20 g medido | 03-Rally §3.4 | planificado | MVP | 325,326,327 |
| E6-22 | B5 Motor, derrape y boost (FX y SFX) | UTN_BuggyEngineSynth y efectos de §8 | 15–20 | E6-04 | UE | Motor, derrape, boost y choque audibles y visibles | 03-Rally §8; UI-FX F2 | planificado | MVP | 328,329,330,331 |
| E6-23 | B6 Interfaz del Rally | U7 velocímetro, posición, checkpoint, panel de artillera, pantallas | 25–35 | E6-09 | UE | HUD completo con estado replicado | 03-Rally §7; UI-FX U7 | planificado | MVP | 345,346,347,348,349,350 |
| E6-24 | B7 Skins del buggy | DT_BuggySkins, FTN_BuggyLook, M_BuggyPaint, pestaña de tienda | 14–18 | E6-04 | UE | Skin comprada igual en 4 máquinas y persiste | 03-Rally §12; P02 | planificado | MVP | 332,333,334 |
| E6-25 | B7 Importar el kit del buggy low poly | Pipeline Art/Source/Vehicles/Buggy | 7–10 | E13-01 | UE + technical-artist | Kit importado con LOD y colisión | 03-Rally §12; Plan §7.8 | planificado | MVP | 335,336 |
| E6-26 | B8 Red de 8 buggies | 30/10 Hz y sonda de correcciones | 6 | E6-15 | UE | Subida del anfitrión ≤ 140 KB/s con 8 | 03-Rally §9.2 | planificado | MVP | 337 |
| E6-27 | Atribuciones de MDE en el Rally | Créditos por mapa geográfico | 2 | E6-13 | agente Python | Cada mapa geo con su fuente en créditos | Plan §3.3 | planificado | MVP | 306 |
| E6-28 | B10 Pruebas del Rally | Unitarias y manuales | 10 | E6-26 | test-automator + Rodrigo | Tests verdes; PIE 4P con 150 ms y 2 % | 03-Rally §11 | planificado | MVP | 338,339 |
| E6-29 | Opción E de red (autoridad del conductor) | Autoridad del conductor validada por el servidor y 30/10 Hz: condición de la investigación de 8 jugadores | 20–30 | E6-26 | UE | Correcciones del conductor aceptadas | 03-Rally §9; B_buggy_sync; Investigacion-8-Jugadores §Veredicto | planificado | MVP | 340,341,342,343,344 |
| E6-30 | Países de Rally restantes (11) | 8-12 h cada uno | 88–132 | E6-17 | agente Python + UE | Cada país con corredor validado | 03-Rally §13.2 | planificado | Después | 451,452,453,454,455,456,457,458,459,460,461,462,463,464,465,466,467,468,469,470 |
| E6-31 | Resto de Después del Rally | A3-A5, expulsión y reenganche, trampolín de vehículo, marcas de derrape, bifurcación de E01B | 63–87 | E6-28 | UE | — | 03-Rally §13.2 | planificado | Después | 471,472,473,474,475,476,477,478,479,480,481,482,483,484 |

### E7. Todos contra Todos (153–217 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E7-01 | Modo TcT: fases, reloj, conchas, muerte súbita | Y desconexión | 14–18 | E0-10 | UE | Ronda a 8 sin errores en PIE | 04-TcT §1-2, §12 | planificado | MVP | 228,229,230 |
| E7-02 | LastInstigator y atribución | Tests < 6 s puntúa, ≥ 6 s no | 4–6 | E7-01 | UE | Tests de atribución verdes | 04-TcT §1.3 | planificado | MVP | 231 |
| E7-03 | Reaparición en nidos con inmunidad | — | 4–6 | E7-01 | UE | Reaparece en el nido más alejado si hay rival a < 15 m (test) | 04-TcT §1.4 | planificado | MVP | 232 |
| E7-04 | Loot por puntos | RollLoot con Norm | 3–4 | E7-01 | UE | Test de pesos por puntos | 04-TcT §5 | planificado | MVP | 233 |
| E7-05 | HUD de TcT (U8) | Marcador, «X te tiró», +1, flechas, avisos | 10–14 | E7-02 | UE | Aviso de quién te tiró en 4 máquinas | 04-TcT §6; UI-FX U8 | planificado | MVP | 260,261,262 |
| E7-06 | Tally y Champion por puntos | — | 4–6 | E7-01 | UE | Pantalla de fin por puntos | 04-TcT §12 | planificado | MVP | 263 |
| E7-07 | Viento (ATN_WindGust) | — | 5–7 | E7-01 | UE | Ráfaga igual en 4 máquinas | 04-TcT §3.2 | planificado | MVP | 234 |
| E7-08 | Barricadas y marcadores de P01 | LVL_Bridges_P01 | 2–3 | E2-16 | UE | P01 con 8 nidos, 8 cajas, cofre, rampas | 04-TcT §3.1 | planificado | MVP | 235 |
| E7-09 | Hundimiento (ATN_SinkingPlatform) | Servidor autoritativo con aviso de 5 s | 10–14 | E7-01 | UE | Anillo baja a la vez en 4 máquinas (±5 cm) | 04-TcT §3.3; Catálogo §6 | planificado | MVP | 236,237,238 |
| E7-10 | Objetos 1-5 | Piedra, balón, tinta, pala, plátano | 12–18 | E7-04,E9-05 | UE | Cada objeto cumple su aceptación del plan §3.4 | 04-TcT §5; Plan §3.4 | planificado | MVP | 239,240,241 |
| E7-11 | Iconos por render | render_preview.py sobre los borradores IA | 3–4 | E7-10 | technical-artist | 7 iconos PNG en Content | 04-TcT K1 | planificado | MVP | 264 |
| E7-12 | Aparición: caja del cielo con gaviota y cofre central | Máx. 2 cajas vivas; cofre a los 60 s | 6–8 | E7-04 | UE | Caja cae cada 25-35 s cerca del grupo (telemetría) | 04-TcT anexo; P40 | planificado | MVP | 242,243 |
| E7-13 | Mapas MVP: P01, A01 Diana, I01 Filipinas | — | 24–34 | E7-08,E7-09,E2-22 | game-designer + UE | Encuentro cada 15-20 s en playtest (telemetría) | 04-TcT §12.3 | planificado | MVP | 244,245,246,247,248,249 |
| E7-14 | Pruebas y playtest 4P/8P | — | 12–16 | E7-13 | test-automator + Rodrigo | Tests Tortunabo.TcT.* verdes | 04-TcT §10 | planificado | MVP | 250,251,252 |
| E7-15 | Objetos 6-8: dardo de noqueo, trabuco, garfio | Pistola de noqueo = ApplyKnockdown en servidor | 17–25 | E7-10,E9-05 | UE | Cápsula tras noqueo igual en 4 máquinas (±10 cm) | 04-TcT §5; Plan §7.7 | planificado | MVP | 253,254,255,256 |
| E7-16 | Gaviotas con suelta ajustada y cangrejos | — | 5–8 | E7-01 | UE | Suelta sobre tablero o meseta ≈ 70 % | 04-TcT §12 | planificado | MVP | 257,258 |
| E7-17 | Corte de puentes en TcT | Lógica de modo sobre ATN_BridgeSpan | 4–6 | E2-20,E7-01 | UE | Puente cortado deja caer a quien está encima en 4 máquinas | 04-TcT §3.5 | planificado | MVP | 259 |
| E7-18 | Mapas fase B: A02, A05, L05 Venecia | — | 14–20 | E7-13 | game-designer + UE | Validadores y encuentro en playtest | 04-TcT §12.3 | planificado | MVP | 366,367,368,369 |
| E7-19 | Aguja giratoria (A04) | — | 8–10 | E7-01 | UE | Aguja noquea igual en 4 máquinas | 04-TcT §12 | planificado | Después | 485,486 |
| E7-20 | Mapas TcT Después | A03, A04, A06, I02, I03-T, I05, países TcT | 60–100 | E7-18 | game-designer + UE | — | 04-TcT §12.3 | planificado | Después | 487,488,489,490,491,492,493,494,495,496,497,498,499,500,501 |

### E8. 2 vs 2 (113–158 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E8-01 | Lógica del 2 vs 2 | GameMode, GameState, equipos, rondas, puntuación, sabotaje, abandono (45 s) | 30–42 | E0-10,E4-15 | UE | Encuentro al mejor de 3 completo con 4 en PIE | 05-2vs2 §1, §13 | planificado | MVP | 268,269,270,271,272,273,274 |
| E8-02 | Agarre de rivales y zonas de contacto | ATN_TeamContactZone e inmunidades | 10–14 | E8-01 | UE | Lanzar a rival en caparazón solo en zona común (test) | 05-2vs2 §5.4; P35 | planificado | MVP | 275,276,277 |
| E8-03 | Empuje de pawn y RaiseEndTime en la compuerta | N5 y M4 vienen de E4-17 | 3 | E4-17 | UE | Compuerta de sabotaje sube 5 s (test) | 05-2vs2 §4.4 | planificado | MVP | 278 |
| E8-04 | Interfaz 2 vs 2 | U1, U4, U6 y parte de U9 (U2/U3 en E12-02) | 29–41 | E8-01,E12-02 | UE | Marcador de 2 equipos y alerta de sabotaje en 4 máquinas | 05-2vs2 §7; UI-FX §3.1 | planificado | MVP | 291,292,293,294,295,296,297 |
| E8-05 | Efectos 2 vs 2 | F1 UTN_PuzzleCueSynth y F3 destello | 11–15 | E8-01 | UE | Éxito/fallo/placa/palanca audibles | 05-2vs2 §8; UI-FX F1, F3 | planificado | MVP | 279,280,281 |
| E8-06 | Mapa DV01_canal y validador de simetría | — | 14–20 | E3-06,E8-01 | game-designer + agente Python | Carriles simétricos (validador) y ronda de 4-6 min en PIE | 05-2vs2 §3.5-3.6 | planificado | MVP | 282,283,284,285 |
| E8-07 | Pruebas y PIE 4P | — | 14–20 | E8-06 | test-automator + Rodrigo | Tests del modo verdes y encuentro de 4 completo | 05-2vs2 §11 | planificado | MVP | 286,287,288,289 |
| E8-08 | Retirar el 2 vs 2 procedural de carriles | ETNProcGameMode::TwoVsTwo a Deprecado | 2–3 | E8-07 | UE | Nada del 2 vs 2 viejo en el selector ni en el cocinado | UI-FX decisiones 1 | planificado | MVP | 290 |
| E8-09 | 2 vs 2 Después | S2-S4, DV02, DV03, «¡Ya!», cartel, sala de práctica, pegamento de plantillas | 47–67 | E8-07 | UE + game-designer | — | 05-2vs2 §13.2 | planificado | Después | 502,503,504,505,506,507,508,509,510,511,512 |

### E9. Física de la tortuga (38–54 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E9-01 | Pendiente en arena (CMC predicho) | Desde 12°, rozamiento ×0,3, freno ×0,4 | 5–7 | E0-01 | UE | Arena a 25°: > 200 cm/s tras 2 s; llano para < 0,8 s | Plan §3.5; G | planificado | MVP | 45 |
| E9-02 | Rebote predicho en vuelo | Restitución 0,45, 60 % tangencial (solo en vuelo) | 4–6 | E9-01 | UE | Pared a 400 → rebote (test) | Plan §3.5; 04-TcT K2 | planificado | MVP | 46 |
| E9-03 | Estampado autoritativo | ServerDiveSplat → StartBody con v reflejada | 5–7 | E9-02 | UE | Pared a 700 → bola en la misma posición en 4 máquinas (±5 cm) | Plan §3.5, §7.7 | planificado | MVP | 47 |
| E9-04 | Predicción del dive | DiveDir en el saved move | 6 | E9-01 | UE | 0 correcciones > 50 cm al iniciar el dive | Plan §3.5 | planificado | MVP | 48 |
| E9-05 | Noqueo autoritativo | ServerFreezeRagdoll ampliado al noqueo; ragdoll cosmético | 6–10 | E9-04 | UE | Cápsula tras noqueo igual en 4 máquinas (±10 cm) | Plan §3.5 punto 5 | planificado | MVP | 49,50 |
| E9-06 | Tests Tortunabo.Dive.* | Incluye caso negativo con parámetros actuales | 2–3 | E9-03 | test-automator | Tests verdes | Plan §3.5 | planificado | MVP | 51 |
| E9-07 | Playtest del doble salto con el director | TN.Dive.Debug 1 y NetEmulation | 2 | E9-06 | Rodrigo | Rodrigo valida lanzar, deslizar y estamparse | Plan §3.5; P02 | planificado | MVP | 52 |
| E9-08 | Regresión del torbellino del caparazón | Test y PIE del fix (kanban Done) | 2–3 | E0-01 | UE + Rodrigo (PIE) | TN.Beach.Gull.Grab 2 + caparazón sin torbellino ni atravesar el mapa | Kanban; P04 | planificado | MVP | 51 |
| E9-09 | Ragdoll de muerte que rueda y se asienta | Posición final del servidor; revive donde queda | 6–10 | E9-05 | UE | Posición final igual en 4 máquinas (±10 cm) | memoria project_ragdoll_red | planificado | MVP | 373,374 |

### E10. Red, sincronización hasta 8 y voz (66–80 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E10-01 | Puente tambaleante con Excitation del servidor | uint8 a 10 Hz; Dips visuales | 4 | E0-01 | UE | p.NetShowCorrections sin correcciones en el puente 4P | Plan §4 P1 | planificado | MVP | 54 |
| E10-02 | Plataforma tambaleante del servidor | int8 a 15 Hz | 3 | E10-01 | UE | Desfase < 5 cm (hoy 19-25) | Plan §4 P2 | planificado | MVP | 55 |
| E10-03 | Mina sin empujón local | — | 2 | — | UE | Sin doble empujón en PIE 4P | Plan §4 P2 | planificado | MVP | 55 |
| E10-04 | Boost de ítems en FTNSavedMove_Turtle | — | 4 | E9-04 | UE | Corrección < 5 cm (hoy ≈ 30) | Plan §4 P2 | planificado | MVP | 53 |
| E10-05 | Trampolín: rebote en el CMC | — | 4 | E10-01 | UE | 0 correcciones tras el rebote | Plan §4 P2 | planificado | MVP | 56 |
| E10-06 | Quad: compensación de lag y retirar ATN_QuadActor | — | 2 | E10-01 | UE | FTNTrapClock visual; ATN_QuadActor fuera | Plan §4 P2 | planificado | MVP | 56 |
| E10-07 | Estado persistente por OnRep (N-C, M4) | Mancha del caparazón y dobles caminos OnRep + multicast | 4–6 | E10-01 | UE | Quien entra tarde ve la mancha | F_gaps N-C; Calidad M4 | planificado | MVP | 375 |
| E10-08 | Investigación de 8 jugadores con voz | Veredicto: viable con Opus, pulsar para hablar y opción E del Rally | 0 | — | Opus (hecho) | Documento entregado | Investigacion-8-Jugadores-Voz-2026-09-29 §7 | hecho | MVP | — |
| E10-09 | Voz con Opus | Build.cs, componente, byte de versión y test de ida y vuelta del códec (hoy µ-law ≈ 17 KB/s por flujo) | 6–8 | E0-01 | UE | Test de códec verde; KB/s por flujo medido y bajo el objetivo de la investigación | Investigacion-8-Jugadores-Voz-2026-09-29 §7 #1 | planificado | MVP | 57,58 |
| E10-10 | Pulsar para hablar por defecto y puerta de umbral | — | 1 | E10-09 | UE | PTT activo en un perfil nuevo | Investigacion-8-Jugadores-Voz-2026-09-29 §7 #2 | planificado | MVP | 58 |
| E10-11 | Config IpNetDriver igual que Steam y 8 instancias con emulación | Scripts de lanzamiento; tope MaxClientRate tras medir | 2–3 | E10-09 | devops-engineer | 8 instancias locales con PktLag por script | Investigacion-8-Jugadores-Voz-2026-09-29 §7 #5 | planificado | MVP | 59 |
| E10-12 | Reglas de reenvío de voz | Tope de voces por oyente, canal de equipo 2 vs 2, espectador, silenciar, descarte por saturación | 8–10 | E10-10 | UE | Con 8 hablando, cada oyente recibe ≤ tope (test) | Investigacion-8-Jugadores-Voz-2026-09-29 §7 #3 | planificado | MVP | 376,377 |
| E10-13 | Frecuencia y relevancia de actores siempre relevantes | Auditoría y medida con stat net | 3–4 | E10-11 | UE | Tabla de KB/s por actor antes y después | Investigacion-8-Jugadores-Voz-2026-09-29 §7 #4 | planificado | MVP | 378 |
| E10-14 | TN.Voice.FakeTalk | Voz sintética para pruebas | 2–3 | E10-09 | UE | 8 hablantes simulados en PIE | Investigacion-8-Jugadores-Voz-2026-09-29 §7 #6 | planificado | MVP | 378 |
| E10-15 | Telemetría LogTNNet y aviso de subida del anfitrión | — | 3–4 | E0-02 | UE | Aviso en pantalla si la subida supera el presupuesto | Investigacion-8-Jugadores-Voz-2026-09-29 §7 #8 | planificado | MVP | 379 |
| E10-16 | Voz del conductor en el Rally | Voz en el pawn buggy y relevancia | 2 | E6-19 | UE | Voz audible entre buggies en PIE 4P | Investigacion-8-Jugadores-Voz-2026-09-29 §7 #10 | planificado | MVP | 351 |
| E10-17 | Sesiones de prueba a 8 | PIE a 8, 8 locales, máquinas virtuales y 2 playtests Steam con 8 personas | 10–12 | E10-12,E10-13,E11-02 | Rodrigo + UE | 15 min con 8 sin [Desync] y subida del anfitrión dentro del presupuesto | Investigacion-8-Jugadores-Voz-2026-09-29 §7 #11 | planificado | MVP | 382,383 |
| E10-18 | Entrada tardía y reconexión en los modos nuevos | Coop, Rally, TcT, 2 vs 2 | 6–8 | E0-09 | UE | Reconexión a mitad de ronda en cada modo sin estado roto | Plan §3.1 Red; auditoría 08-18 #5 | planificado | MVP | 380,381 |

### E11. Optimización y estrés (28–39 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E11-01 | Monkey: terminar y commitear | TN_Monkey* y TN_TestReport en curso | 4–6 | E0-01 | UE | TN.Monkey 10 min en C01 y BeachRace sin ensure; informe en Saved | 00 §2; Investigacion-8-Jugadores §7 #7 | en curso | MVP | 9 |
| E11-02 | TN.Stress: llevar los sistemas al límite | TN.Stress race8: 8 pawns, objetos y mecánicas; medir ms, KB/s, draw calls | 6–8 | E11-01 | UE | Informe con el primer sistema que rompe presupuesto | 00 §2; Investigacion-8-Jugadores §7 #7 | planificado | MVP | 60,61 |
| E11-03 | Rutina nocturna local de monkey y estrés | Antes de cada PR grande | 2–3 | E11-02,E1-07 | devops-engineer | Informe diario en Saved/Reports | 00 §2 | planificado | MVP | 61 |
| E11-04 | Perfilado de render de mapas | Nanite, colisión, draw calls, sombras en C01, CP01, P01 y E01B | 6–8 | E4-08 | UE | stat unit ≤ 16,6 ms en la gama mínima fijada | P07 (map rendering) | planificado | MVP | 384,385 |
| E11-05 | Coste de ítems y VFX con 8 jugadoras | — | 3–4 | E0-14 | UE | ms por ítem activo medido; ninguno > 0,2 ms | P07 (item optimization) | planificado | MVP | 386 |
| E11-06 | Presupuestos por modo en Shipping | Coop §9, TcT §9, Rally §10 | 4–6 | E11-04 | UE | Todos los presupuestos cumplidos o con tarea abierta | 01-Coop §9; 04-TcT §9; 03-Rally §10 | planificado | MVP | 426 |
| E11-07 | Tamaño del paquete | ≤ 12 MB por mapa y ≤ 500 MB de terreno | 2–3 | E6-14 | UE | Informe del .pak por carpeta | 03-Rally anexo C | planificado | MVP | 427 |
| E11-08 | Gama mínima de hardware | Fijar la referencia de los presupuestos | 1 | — | Rodrigo | Gama mínima escrita en Docs | 01-Coop Q5 | planificado | MVP | 10 |

### E12. Interfaz y efectos comunes (63–88 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E12-01 | U5 Selector de modo y mapa | Cinco modos, requisito de jugadoras en rojo | 13–17 | E4-10 | UE | Los 5 modos seleccionables desde el HQ | UI-FX §2.1, §3.1 | planificado | MVP | 207,208,209 |
| E12-02 | C-4 Interfaz común (incluye U2, U3) | Anillo de progreso, marcadores de compañera, flechas | 25–36 | E0-12 | UE | Pantallas de 01-Coop §7.1 con estado replicado | 01-Coop §7.1; UI-FX U2-U3 | planificado | MVP | 210,211,212,213,214,215 |
| E12-03 | C-5 Efectos y sonido comunes | §7.3 (F1 y F3 en E8-05) | 14–19 | E0-12 | UE | Efectos de §7.3 disparados por mensajería | 01-Coop §7.3 | planificado | MVP | 216,217,218 |
| E12-04 | U9 Tally y Champion por equipos | Arrival con dos caras, podio de pareja | 10–14 | E7-06 | UE | Podio de pareja en 2 vs 2 | UI-FX U9 | planificado | MVP | 298,299,300 |
| E12-05 | Textos de Briefing y Pausa | Quitar Clásico y 2 vs 2 viejo | 1–2 | E4-10 | UE | 0 menciones al Clásico | UI-FX §0.3 | anulada (Decisión en #143: Supervivencia conserva LVL_Run y los chunks) | MVP | 219 |
| E12-06 | U10 Cartel de primera vez | Pictogramas de puzzle | 4–6 | E12-02 | UE | — | UI-FX U10 | planificado | Después | 513 |
| E12-07 | F4 +1 flotante y marcas de derrape F5 | — | 8–11 | E7-05 | UE | — | UI-FX F4-F5 | planificado | Después | 514,515 |
| E12-08 | Vista previa del mapa en la carga y menú con miniaturas | — | 10–14 | E12-01 | UE | — | Plan §3.6; UI-FX §4.6 | planificado | Después | 516,517,518 |

### E13. Assets (IA → modeladores) (25–37 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E13-01 | Buggy biplaza low poly (kit) | Cerrar Art/Source/Vehicles/Buggy con lámina | 4–6 | — | technical-artist + Rodrigo (lámina) | Presupuesto de tris y bbox del manifest; lámina aprobada | Plan §7.8; P09 | en curso | MVP | 11 |
| E13-02 | Borradores IA pendientes | Decoración por idioma de los 10 países sin prop y piezas de puzzle sin borrador | 8–12 | E2-21 | technical-artist | Cada borrador con validación OK y lámina en INDEX.md | Art/Library/IA/INDEX.md; P29 | planificado | MVP | 370,371 |
| E13-03 | Lámina de la biblioteca IA para Rodrigo | — | 1 | E13-02 | technical-artist + Rodrigo | Rodrigo marca qué pasa al equipo de arte | INDEX.md | planificado | MVP | 371 |
| E13-04 | Encargo priorizado al equipo de arte | 5 encargos del inventario y borradores elegidos | 2–3 | E13-03 | Rodrigo + game-designer | Lista enviada con plazos | Inventario §7 | planificado | MVP | 372 |
| E13-05 | Encargos de producto Steam | Key art, logotipo, plantilla de icono, cápsulas, kit del HQ | 0 | E13-04 | equipo de arte | Entregados | Inventario §7; README modos | planificado | MVP | — |
| E13-06 | Sustituir borradores por modelos finales | Importar, LOD, colisión, referencias | 10–15 | E13-05 | UE + technical-artist | 0 referencias a SM_TN_* de borrador en Content | INDEX.md | planificado | MVP | 428,429,430 |
| E13-07 | Pose de la tortuga al volante | Deseable | 3–4 | E6-19 | technical-artist | — | Plan §3.6 | planificado | Después | 519 |

### E14. Limpieza y calidad de código (80–120 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E14-01 | Mapa01 a Deprecado | LVL_Mapa01, mallas y Scripts/terrain_volumes/Mapa01 | 2–3 | — | UE | Cook sin Mapa01; generadores de terrain_vol verdes | Limpieza §Decisión; P20 | planificado | MVP | 391 |
| E14-02 | Retirar C++ de la rejilla (2.1) | Tras aceptar borrar los BP movidos | 3–4 | E14-01 | UE + Rodrigo (decide) | 6.059 líneas fuera; build y tests verdes | Limpieza 2.1 | planificado | MVP | 391 |
| E14-03 | Python pospuesto (2.1) | gen_terrain_biomes, terrain_gen; chaikin/generate_path a terrain_vol | 2–3 | — | agente Python | Pytest verde | Limpieza 2.1 | planificado | MVP | 390 |
| E14-04 | Run por chunks y LVL_Run (2.2, 2.3, 4.5) | Decisión de equipo | 4–6 | E4-11 | UE + Rodrigo + equipo | LVL_Run y 14 BP en _Deprecado; build verde | Limpieza 2.2-2.3, 4.5 | anulada (Decisión en #143: Supervivencia conserva LVL_Run y los chunks) | MVP | 392 |
| E14-05 | MapVariantLoader y TerrainMeshTile (2.4, 4.8) | Tras F1 | 2–3 | E4-12 | UE | 603 líneas fuera; vista previa por importador | Limpieza 2.4, 4.8 | planificado | MVP | 393 |
| E14-06 | SeagullActor, QuadActor y EnemySeagull (2.5, 2.6, Q11) | — | 2–3 | E14-04 | UE | Clases en Deprecado; build verde | Limpieza 2.5-2.6; 01-Coop Q11 | planificado | MVP | 393 |
| E14-07 | Visor de terreno (2.7) y LVL_TestMap (4.6) | Reanalizar huérfanos (~2.300 líneas) | 3–5 | E14-04 | UE | Informe de huérfanos y movidos | Limpieza 2.7, 4.6 | planificado | MVP | 394 |
| E14-08 | Propuestas a Alvaro y Mokius (4.1-4.4, 4.9-4.11) | — | 1–2 | — | Rodrigo | Respuesta de cada autor | Limpieza lote 4 | planificado | MVP | 411 |
| E14-09 | Calidad M2, M3, M9, M12 | Tormenta por nombre, ButtonGroup, versión de ajustes, cargas síncronas | 6–10 | — | UE | Cada punto cerrado con test o log | Calidad-Codigo | planificado | MVP | 395,396 |
| E14-10 | Calidad M5 y M6 | TortugaCharacter.h y cabeceras con implementación | 16–24 | E9-05 | UE | Unidades que incluyen TortugaCharacter.h < 40 | Calidad M5-M6 | planificado | MVP | 397,398,399,400 |
| E14-11 | Calidad M7, M8, M11 | Equilibrado a datos, emotes, rutas /Game duplicadas | 14–20 | E0-11 | UE | 0 literales /Game duplicados | Calidad M7, M8, M11 | planificado | MVP | 401,402,403,404 |
| E14-12 | Calidad M13 y splits de ficheros | BuildStructures, PauseMenu, BeachRaceGameMode, MP_GameInstance, Settings, GullZone, ProcFauna | 22–32 | E14-10 | UE | Ningún fichero tocado > 800 líneas | Calidad top 10 | planificado | MVP | 405,406,407,408,409 |
| E14-13 | Calidad LOW (L1, L2, L4, L5, L7, L8) | — | 3–5 | — | UE | Cada punto cerrado | Calidad LOW | planificado | MVP | 410 |

### E15. Localización, logros y ajustes (41–49 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E15-01 | Textos nuevos de los 5 modos en 13 idiomas | LOCTEXT + .po | 8–12 | E12-01 | UE | 0 FText::FromString visibles nuevos | Localizacion.md; P34–P36 | planificado | MVP | 431,432 |
| E15-02 | Restos sin localizar (M10, L6, 37 FromString) | — | 4–6 | — | UE | Auditoría de textos visibles a 0 | F_gaps #3; Calidad M10, L6 | planificado | MVP | 433 |
| E15-03 | Fuentes CJK Noto | Descargar e integrar | 2–3 | — | UE | ja, ko, zh sin tofu | F_gaps #3 | planificado | MVP | 434 |
| E15-04 | Nombres de mapas y humor por idioma | Sin rótulos políticos | 1–2 | E2-25 | game-designer | Lista revisada | Catálogo §6 preguntas 5-6 | planificado | MVP | 434 |
| E15-05 | LQA nativa | Coordinar revisores externos | 2 | E15-01 | Rodrigo | Informe LQA por idioma | F_gaps #3 | planificado | MVP | 434 |
| E15-06 | Validar ajustes y audio | — | 4 | — | UE + Rodrigo | Checklist de ajustes completo | F_gaps #2 | planificado | MVP | 435 |
| E15-07 | Logros y rich presence | Estado «En la carrera, ronda N» y unirse desde amigos | 20 | E1-05 | UE | ≥ 10 logros desbloqueables en Steam real | F_gaps #8 | planificado | MVP | 436,437,438,439 |

### E16. Playtests y QA (88–92 h MVP)

| Id | Tarea | Descripción | h | Depende de | Quién | Hecho cuando | Ref. | Estado | Fase | Ses. |
|---|---|---|---|---|---|---|---|---|---|---|
| E16-01 | Gate playtest PIE 4P + Standalone loopback | Kanban In Progress (checklist 10 puntos) | 3–4 | E0-01 | Rodrigo | Checklist marcado con commit | Kanban; memoria playtest | en curso | MVP | 12 |
| E16-02 | Recorrer P01 y E01 en PIE | Kanban Backlog | 2 | — | Rodrigo | Notas de ajuste por mapa | Kanban | planificado | MVP | 12 |
| E16-03 | Actualizar el checklist de playtest con los modos nuevos | — | 2–3 | E16-01 | game-designer | Checklist con secciones de los 5 modos | memoria project_playtest_checklist | planificado | MVP | 117 |
| E16-04 | Playtest Shipping en Steam, iteración 1 (tras F2) | 16 h por iteración | 16 | E4-09 | Rodrigo + UE | Checklist completo en Steam real con 4 | F_gaps §4.1 | planificado | MVP | 220,221,222 |
| E16-05 | Playtest Shipping en Steam, iteración 2 (Carrera y tortuga) | — | 16 | E5-09,E9-07 | Rodrigo + UE | Checklist completo | F_gaps §4.1 | planificado | MVP | 265,266,267 |
| E16-06 | Playtest Shipping en Steam, iteración 3 (TcT, 2 vs 2, Rally) | — | 16 | E7-14,E8-07,E6-28,E10-17 | Rodrigo + UE | Checklist completo | F_gaps §4.1 | planificado | MVP | 387,388,389 |
| E16-07 | Playtest Shipping en Steam, iteración 4 (release candidate) | — | 16 | E15-07,E1-12 | Rodrigo + UE | Checklist completo con 8 | F_gaps §4.1 | planificado | MVP | 441,442,443 |
| E16-08 | Playtest de diseño Coop (3-4) | — | 3–4 | E4-28 | Rodrigo | Duración y dificultad medidas | 01-Coop §10.3 | planificado | MVP | 223 |
| E16-09 | Playtest de ritmo de la Carrera con 8 | — | 3 | E5-08 | Rodrigo | Duración de partida medida | 02-Carrera P2 | planificado | MVP | 118 |
| E16-10 | Playtest de la copa de Rally | — | 3 | E6-28 | Rodrigo | 10 copas: < 60 % de victorias de un tipo (solo/biplaza) | 03-Rally §1 | planificado | MVP | 352 |
| E16-11 | Revisión adversarial por épica | Codex adversarial-review en cada fusión grande (6) | 6 | — | code-reviewer + codex | 0 CRITICAL/HIGH abiertos por épica | CLAUDE global (delegación) | planificado | MVP | 444 |
| E16-12 | Auditoría de cobertura final | Repetir la auditoría de peticiones antes de fusionar a main | 2–3 | E16-07 | architect-reviewer | 0 peticiones OLVIDADAS | Docs/Auditoria-Cobertura-2026-09-29.md | planificado | MVP | 440 |

## 4. Sesiones de trabajo

Cada sesión = un objetivo cerrado de ~4–6 h de agente, dentro de una épica y un carril. Columna «Paralelo»: **P** = no compila UE, puede ir en paralelo con cualquier sesión; **C** = compila UE: en paralelo solo con sesiones C de otro carril (ficheros disjuntos) y la compilación la serializa el integrador en DebugGame; **R** = necesita a Rodrigo (PIE, Steam, decisión o lámina); **A** = depende de entregas del equipo de arte. Las tareas partidas llevan (n/m).


### Ola 1. Arranque: integrar lo pendiente y base Steam (F0)

| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |
|---|---|---|---|---|---|---|
| 1 | E0 | INT | E0-01 | Integrar PR #9 (bugs de la nube) | 2,5 | C · R |
| 2 | E0 | DOC | E0-17 | Sincronizar documentos con las decisiones vigentes | 1,5 | P |
| 3 | E1 | ST | E1-01, E1-03, E1-06 | F0-T1 Guardia de cocinado; F0-T3 OnTravelFailure (hueco de robustez); F0-T5 N-A depuración remota | 4,5 | C |
| 4 | E1 | ST | E1-07 (1/2) | F0-T7 CI local | 4,0 | C |
| 5 | E1 | ST | E1-07 (2/2) | F0-T7 CI local | 4,0 | C |
| 6 | E1 | ST | E1-09, E1-10 | Cocinado completo verificado; N-D ServerGoToStation validado | 3,5 | C |
| 7 | E1 | QA | E1-08 | F0-T8 Playtest Shipping con dos máquinas | 4,0 | P · R |
| 8 | E5 | BR | E5-01 | F0-T6 Dorado T0 del reparto | 3,0 | C |
| 9 | E11 | FW | E11-01 | Monkey: terminar y commitear | 5,0 | C |
| 10 | E11 | DOC | E11-08 | Gama mínima de hardware | 1,0 | P · R |
| 11 | E13 | ART | E13-01 | Buggy biplaza low poly (kit) | 5,0 | P · R |
| 12 | E16 | QA | E16-01, E16-02 | Gate playtest PIE 4P + Standalone loopback; Recorrer P01 y E01 en PIE | 5,5 | P · R |

### Ola 2. Terreno preparado (F1), sistema de bugs, tortuga y red en paralelo

| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |
|---|---|---|---|---|---|---|
| 13 | E0 | FW | E0-02, E0-03 | Categorías de log por sistema; CVars TN,Debug,<Sistema> | 6,0 | C |
| 14 | E0 | FW | E0-04 | Invariantes con ensureMsgf | 2,5 | C |
| 15 | E0 | FW | E0-05 (1/2) | Detector de desincronía | 4,5 | C |
| 16 | E0 | FW | E0-05 (2/2) | Detector de desincronía | 4,5 | C |
| 17 | E0 | FW | E0-06 | Informe de bug con F8 | 5,0 | C |
| 18 | E0 | FW | E0-07 | Repeticiones en playtest | 1,5 | C |
| 19 | E2 | PY | E2-01 | F1-T9 BFS con padres | 4,0 | P |
| 20 | E2 | PY | E2-02 | F1-T10 Máscara y bake,bin | 4,0 | P |
| 21 | E2 | PY | E2-03 | F1-T11 Manifest v2 (C01, P01) | 5,0 | P |
| 22 | E2 | PY | E2-04 | F1-T12 Decimado cuádrico | 5,0 | P |
| 23 | E2 | PY | E2-21 (1/3) | Países enteros miniaturizados (13) | 4,0 | P |
| 24 | E2 | PY | E2-21 (2/3) | Países enteros miniaturizados (13) | 4,0 | P |
| 25 | E2 | PY | E2-21 (3/3) | Países enteros miniaturizados (13) | 4,0 | P |
| 26 | E2 | PY | E2-22 | Japón y Filipinas v2 | 5,0 | P |
| 27 | E2 | PY | E2-23 (1/4) | Mapas inventados N01-N16 | 5,0 | P |
| 28 | E2 | PY | E2-23 (2/4) | Mapas inventados N01-N16 | 5,0 | P |
| 29 | E2 | PY | E2-23 (3/4) | Mapas inventados N01-N16 | 5,0 | P |
| 30 | E2 | PY | E2-23 (4/4) | Mapas inventados N01-N16 | 5,0 | P |
| 31 | E2 | PY | E2-24 | Coliseo con bordes de agua | 5,0 | P |
| 32 | E2 | CP | E2-05 | F1-T13 Rejilla horneada C++ | 4,0 | C |
| 33 | E2 | CP | E2-06 | F1-T14 UTN_CoopMapData | 5,0 | C |
| 34 | E2 | CP | E2-07 | F1-T15 Vigilante puro | 3,0 | C |
| 35 | E2 | CP | E2-08 | F1-T16 Proveedor de suelo y Source=Prepared | 5,0 | C |
| 36 | E2 | CP | E2-09 | F1-T17 FindOpenSandSpot consulta al proveedor | 3,0 | C |
| 37 | E2 | CP | E2-10, E2-11 | F1-T18 Vigilante de servidor y gaviotas por el camino; F1-T19 R1 terreno por etiqueta | 5,5 | C |
| 38 | E2 | CP | E2-12, E2-13 | F1-T20 R2/R3 meta orientada y a cota; F1-T21 R5 SpawnFauna extraída | 5,0 | C |
| 39 | E2 | CP | E2-14, E2-15 | F1-T22 R8 pantalla de carga reconoce Coop; F1-T23 Marcadores y ancla | 4,0 | C |
| 40 | E2 | CP | E2-16 (1/2) | F1-T24 Importador headless | 3,5 | C |
| 41 | E2 | CP | E2-16 (2/2), E2-17 | F1-T24 Importador headless; F1-T25 R9 y smoke de LVL_Coop_C01 (gate F1) | 5,5 | C |
| 42 | E2 | CP | E2-19 | Puente de Mokius: localizar el asset con física | 1,5 | P · R |
| 43 | E4 | PY | E4-07 (1/2) | F2-T32 CP01 (mapa propio de Claude) | 4,0 | P |
| 44 | E4 | PY | E4-07 (2/2) | F2-T32 CP01 (mapa propio de Claude) | 4,0 | P |
| 45 | E9 | PL | E9-01 | Pendiente en arena (CMC predicho) | 6,0 | C |
| 46 | E9 | PL | E9-02 | Rebote predicho en vuelo | 5,0 | C |
| 47 | E9 | PL | E9-03 | Estampado autoritativo | 6,0 | C |
| 48 | E9 | PL | E9-04 | Predicción del dive | 6,0 | C |
| 49 | E9 | PL | E9-05 (1/2) | Noqueo autoritativo | 4,0 | C |
| 50 | E9 | PL | E9-05 (2/2) | Noqueo autoritativo | 4,0 | C |
| 51 | E9 | PL | E9-06, E9-08 | Tests Tortunabo,Dive,*; Regresión del torbellino del caparazón | 5,0 | C · R |
| 52 | E9 | QA | E9-07 | Playtest del doble salto con el director | 2,0 | P · R |
| 53 | E10 | PL | E10-04 | Boost de ítems en FTNSavedMove_Turtle | 4,0 | C |
| 54 | E10 | NET | E10-01 | Puente tambaleante con Excitation del servidor | 4,0 | C |
| 55 | E10 | NET | E10-02, E10-03 | Plataforma tambaleante del servidor; Mina sin empujón local | 5,0 | C |
| 56 | E10 | NET | E10-05, E10-06 | Trampolín: rebote en el CMC; Quad: compensación de lag y retirar ATN_QuadActor | 6,0 | C |
| 57 | E10 | NET | E10-09 (1/2) | Voz con Opus | 3,5 | C |
| 58 | E10 | NET | E10-09 (2/2), E10-10 | Voz con Opus; Pulsar para hablar por defecto y puerta de umbral | 4,5 | C |
| 59 | E10 | NET | E10-11 | Config IpNetDriver igual que Steam y 8 instancias con emulación | 2,5 | C |
| 60 | E11 | FW | E11-02 (1/2) | TN,Stress: llevar los sistemas al límite | 3,5 | C |
| 61 | E11 | FW | E11-02 (2/2), E11-03 | TN,Stress: llevar los sistemas al límite; Rutina nocturna local de monkey y estrés | 6,0 | C |

### Ola 3. Coop Prepared (F2) y retirada de Run

| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |
|---|---|---|---|---|---|---|
| 62 | E2 | CP | E2-18 (1/2) | Re-horneado tras retoque manual del terreno | 5,0 | C |
| 63 | E2 | CP | E2-18 (2/2) | Re-horneado tras retoque manual del terreno | 5,0 | C |
| 64 | E4 | CP | E4-01 | F2-T26 Layout sintético | 4,0 | C |
| 65 | E4 | CP | E4-02 | F2-T27 Construcción Prepared | 6,0 | C |
| 66 | E4 | CP | E4-03, E4-04 | F2-T28 R6 SeaNear desde la rejilla; F2-T29 Mecánicas desde marcadores y auditoría R10 | 5,0 | C |
| 67 | E4 | CP | E4-05 | F2-T30 Vegetación en ATN_BeachDecorField | 4,0 | C |
| 68 | E4 | CP | E4-06 | F2-T31 Catálogo Coop, elección y viaje | 5,0 | C |
| 69 | E4 | CP | E4-08, E4-10 | F2-T33 Importar CP01 y C01; F2-T35 Clásico fuera del selector | 5,0 | C |
| 70 | E4 | CP | E4-11, E4-12 | F2-T36 ATN_MatchGameModeBase; F2-T37 LVL_Demo01 fuera | 4,0 | C |
| 71 | E4 | CP | E4-13 | Botones de mapa en la pizarra del general | 4,0 | C |
| 72 | E4 | QA | E4-09 | F2-T34 PIE 4P gate F2 | 6,0 | P · R |

### Ola 4. Arquitectura común de modos y Carrera

| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |
|---|---|---|---|---|---|---|
| 73 | E0 | FW | E0-08 | Telemetría de playtest a CSV | 3,5 | C |
| 74 | E0 | FW | E0-09 (1/2) | Máquina de fases de partida | 3,5 | C |
| 75 | E0 | FW | E0-09 (2/2) | Máquina de fases de partida | 3,5 | C |
| 76 | E0 | FW | E0-10 (1/2) | Estrategia de modo UTN_ModeRules | 4,5 | C |
| 77 | E0 | FW | E0-10 (2/2) | Estrategia de modo UTN_ModeRules | 4,5 | C |
| 78 | E0 | FW | E0-11 (1/2) | DataAssets de datos | 3,5 | C |
| 79 | E0 | FW | E0-11 (2/2) | DataAssets de datos | 3,5 | C |
| 80 | E0 | FW | E0-12 | Mensajería de gameplay | 5,0 | C |
| 81 | E0 | FW | E0-13 | Interfaz ITN_Activatable | 3,5 | C |
| 82 | E0 | FW | E0-14 | Pool de objetos | 3,5 | C |
| 83 | E0 | FW | E0-15 | Test de partida completa headless por modo | 5,0 | C |
| 84 | E5 | BR | E5-02 (1/3) | T1 Correcciones P1-P3 | 5,2 | C |
| 85 | E5 | BR | E5-02 (2/3) | T1 Correcciones P1-P3 | 5,2 | C |
| 86 | E5 | BR | E5-02 (3/3) | T1 Correcciones P1-P3 | 5,2 | C |
| 87 | E5 | BR | E5-03 (1/2) | T2 Assets de tramo y curso | 3,5 | C |
| 88 | E5 | BR | E5-03 (2/2) | T2 Assets de tramo y curso | 3,5 | C |
| 89 | E5 | BR | E5-04 (1/3) | T3 PlaceAuthored | 4,7 | C |
| 90 | E5 | BR | E5-04 (2/3) | T3 PlaceAuthored | 4,7 | C |
| 91 | E5 | BR | E5-04 (3/3) | T3 PlaceAuthored | 4,7 | C |
| 92 | E5 | BR | E5-05 (1/2) | T4 Red de tramos | 3,5 | C |
| 93 | E5 | BR | E5-05 (2/2) | T4 Red de tramos | 3,5 | C |
| 94 | E5 | BR | E5-06 (1/4) | T5 Editor de tramos | 4,8 | C |
| 95 | E5 | BR | E5-06 (2/4) | T5 Editor de tramos | 4,8 | C |
| 96 | E5 | BR | E5-06 (3/4) | T5 Editor de tramos | 4,8 | C |
| 97 | E5 | BR | E5-06 (4/4) | T5 Editor de tramos | 4,8 | C |
| 98 | E5 | BR | E5-07 (1/2) | T6 Tests de tramos, curso fijo y sprint | 3,5 | C |
| 99 | E5 | BR | E5-07 (2/2) | T6 Tests de tramos, curso fijo y sprint | 3,5 | C |
| 100 | E5 | BR | E5-08 (1/7) | T7 Pulido P4-P8 | 4,9 | C |
| 101 | E5 | BR | E5-08 (2/7) | T7 Pulido P4-P8 | 4,9 | C |
| 102 | E5 | BR | E5-08 (3/7) | T7 Pulido P4-P8 | 4,9 | C |
| 103 | E5 | BR | E5-08 (4/7) | T7 Pulido P4-P8 | 4,9 | C |
| 104 | E5 | BR | E5-08 (5/7) | T7 Pulido P4-P8 | 4,9 | C |
| 105 | E5 | BR | E5-08 (6/7) | T7 Pulido P4-P8 | 4,9 | C |
| 106 | E5 | BR | E5-08 (7/7) | T7 Pulido P4-P8 | 4,9 | C |
| 107 | E5 | BR | E5-09 | T9 P9 subflujos de RNG y nuevo dorado | 5,0 | C |
| 108 | E5 | BR | E5-10 (1/3) | Objetos N1 y N2 | 5,3 | C |
| 109 | E5 | BR | E5-10 (2/3) | Objetos N1 y N2 | 5,3 | C |
| 110 | E5 | BR | E5-10 (3/3) | Objetos N1 y N2 | 5,3 | C |
| 111 | E5 | BR | E5-11, E5-12 (1/2) | Atribución del atacante; Aparición de objetos en la Carrera | 6,0 | C |
| 112 | E5 | BR | E5-12 (2/2) | Aparición de objetos en la Carrera | 3,5 | C |
| 113 | E5 | BR | E5-13 | Proveedor de suelo seguro en la playa | 3,5 | C |
| 114 | E5 | BR | E5-14 (1/3) | Contenido: 6 tramos del primer lote | 4,0 | C |
| 115 | E5 | BR | E5-14 (2/3) | Contenido: 6 tramos del primer lote | 4,0 | C |
| 116 | E5 | BR | E5-14 (3/3) | Contenido: 6 tramos del primer lote | 4,0 | C |
| 117 | E16 | DOC | E16-03 | Actualizar el checklist de playtest con los modos nuevos | 2,5 | P |
| 118 | E16 | QA | E16-09 | Playtest de ritmo de la Carrera con 8 | 3,0 | P · R |

### Ola 5. Rally: núcleo del buggy y backend

| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |
|---|---|---|---|---|---|---|
| 119 | E6 | PY | E6-13 (1/7) | F6b Ruta por hitos, tallado y checkpoints | 5,4 | P |
| 120 | E6 | PY | E6-13 (2/7) | F6b Ruta por hitos, tallado y checkpoints | 5,4 | P |
| 121 | E6 | PY | E6-13 (3/7) | F6b Ruta por hitos, tallado y checkpoints | 5,4 | P |
| 122 | E6 | PY | E6-13 (4/7) | F6b Ruta por hitos, tallado y checkpoints | 5,4 | P |
| 123 | E6 | PY | E6-13 (5/7) | F6b Ruta por hitos, tallado y checkpoints | 5,4 | P |
| 124 | E6 | PY | E6-13 (6/7) | F6b Ruta por hitos, tallado y checkpoints | 5,4 | P |
| 125 | E6 | PY | E6-13 (7/7) | F6b Ruta por hitos, tallado y checkpoints | 5,4 | P |
| 126 | E6 | VH | E6-01 (1/2) | B1 Port del núcleo del buggy sin Cargo | 3,5 | C |
| 127 | E6 | VH | E6-01 (2/2) | B1 Port del núcleo del buggy sin Cargo | 3,5 | C |
| 128 | E6 | VH | E6-02 | B1 BuggySpec y buggy_measure | 3,5 | C |
| 129 | E6 | VH | E6-03 | B1 Física síncrona con subpasos | 5,0 | C |
| 130 | E6 | VH | E6-04 (1/2) | B1 Pawn buggy con tortuga visual | 5,0 | C |
| 131 | E6 | VH | E6-04 (2/2) | B1 Pawn buggy con tortuga visual | 5,0 | C |
| 132 | E6 | VH | E6-05 | B1 Sync de turbo y mina | 6,0 | C |
| 133 | E6 | VH | E6-06 (1/4) | B3 Trazado, puertas, vueltas, posiciones | 4,5 | C |
| 134 | E6 | VH | E6-06 (2/4) | B3 Trazado, puertas, vueltas, posiciones | 4,5 | C |
| 135 | E6 | VH | E6-06 (3/4) | B3 Trazado, puertas, vueltas, posiciones | 4,5 | C |
| 136 | E6 | VH | E6-06 (4/4) | B3 Trazado, puertas, vueltas, posiciones | 4,5 | C |
| 137 | E6 | VH | E6-07 (1/2) | B3 Dirección contraria y atajos ilegales | 5,0 | C |
| 138 | E6 | VH | E6-07 (2/2) | B3 Dirección contraria y atajos ilegales | 5,0 | C |
| 139 | E6 | VH | E6-08 (1/3) | B3 Enderezar, reaparición y anti-vuelco | 4,0 | C |
| 140 | E6 | VH | E6-08 (2/3) | B3 Enderezar, reaparición y anti-vuelco | 4,0 | C |
| 141 | E6 | VH | E6-08 (3/3) | B3 Enderezar, reaparición y anti-vuelco | 4,0 | C |
| 142 | E6 | VH | E6-09 (1/3) | B3 Salida, fin (20 s), rebufo y copa | 4,2 | C |
| 143 | E6 | VH | E6-09 (2/3) | B3 Salida, fin (20 s), rebufo y copa | 4,2 | C |
| 144 | E6 | VH | E6-09 (3/3) | B3 Salida, fin (20 s), rebufo y copa | 4,2 | C |
| 145 | E6 | VH | E6-10 (1/4) | B4 Colisiones, atropello, fricción y cámara | 5,4 | C |
| 146 | E6 | VH | E6-10 (2/4) | B4 Colisiones, atropello, fricción y cámara | 5,4 | C |
| 147 | E6 | VH | E6-10 (3/4) | B4 Colisiones, atropello, fricción y cámara | 5,4 | C |
| 148 | E6 | VH | E6-10 (4/4) | B4 Colisiones, atropello, fricción y cámara | 5,4 | C |
| 149 | E6 | VH | E6-11 (1/3) | 4 ítems sobre vehículo | 4,0 | C |
| 150 | E6 | VH | E6-11 (2/3) | 4 ítems sobre vehículo | 4,0 | C |
| 151 | E6 | VH | E6-11 (3/3) | 4 ítems sobre vehículo | 4,0 | C |
| 152 | E6 | VH | E6-12 (1/2) | Aparición de cajas del Rally | 3,5 | C |
| 153 | E6 | VH | E6-12 (2/2) | Aparición de cajas del Rally | 3,5 | C |

### Ola 6. Coop: reglas, puzzles, enemigos e interfaz

| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |
|---|---|---|---|---|---|---|
| 154 | E4 | CP | E4-14 (1/2) | C-1 Revivir y DBNO | 4,5 | C |
| 155 | E4 | CP | E4-14 (2/2) | C-1 Revivir y DBNO | 4,5 | C |
| 156 | E4 | CP | E4-15 (1/4) | K-1 Reglas Coop | 5,2 | C |
| 157 | E4 | CP | E4-15 (2/4) | K-1 Reglas Coop | 5,2 | C |
| 158 | E4 | CP | E4-15 (3/4) | K-1 Reglas Coop | 5,2 | C |
| 159 | E4 | CP | E4-15 (4/4) | K-1 Reglas Coop | 5,2 | C |
| 160 | E4 | CP | E4-16 (1/5) | C-2a Piezas de puzzle N4, N1, M1, M3 | 5,2 | C |
| 161 | E4 | CP | E4-16 (2/5) | C-2a Piezas de puzzle N4, N1, M1, M3 | 5,2 | C |
| 162 | E4 | CP | E4-16 (3/5) | C-2a Piezas de puzzle N4, N1, M1, M3 | 5,2 | C |
| 163 | E4 | CP | E4-16 (4/5) | C-2a Piezas de puzzle N4, N1, M1, M3 | 5,2 | C |
| 164 | E4 | CP | E4-16 (5/5) | C-2a Piezas de puzzle N4, N1, M1, M3 | 5,2 | C |
| 165 | E4 | CP | E4-17 (1/5) | C-2b Piezas de puzzle M4, M2, N6, N2, N5, N3 | 5,0 | C |
| 166 | E4 | CP | E4-17 (2/5) | C-2b Piezas de puzzle M4, M2, N6, N2, N5, N3 | 5,0 | C |
| 167 | E4 | CP | E4-17 (3/5) | C-2b Piezas de puzzle M4, M2, N6, N2, N5, N3 | 5,0 | C |
| 168 | E4 | CP | E4-17 (4/5) | C-2b Piezas de puzzle M4, M2, N6, N2, N5, N3 | 5,0 | C |
| 169 | E4 | CP | E4-17 (5/5) | C-2b Piezas de puzzle M4, M2, N6, N2, N5, N3 | 5,0 | C |
| 170 | E4 | CP | E4-18 (1/2) | K-2 Escala de puzzles por jugadoras | 3,8 | C |
| 171 | E4 | CP | E4-18 (2/2) | K-2 Escala de puzzles por jugadoras | 3,8 | C |
| 172 | E4 | CP | E4-19 | Fusión de enemigos intocables (E-12, X-1,,X-4) | 2,5 | C · R |
| 173 | E4 | CP | E4-20 (1/8) | C-3 Enemigos X-1 a X-4 y ajustes | 4,9 | C |
| 174 | E4 | CP | E4-20 (2/8) | C-3 Enemigos X-1 a X-4 y ajustes | 4,9 | C |
| 175 | E4 | CP | E4-20 (3/8) | C-3 Enemigos X-1 a X-4 y ajustes | 4,9 | C |
| 176 | E4 | CP | E4-20 (4/8) | C-3 Enemigos X-1 a X-4 y ajustes | 4,9 | C |
| 177 | E4 | CP | E4-20 (5/8) | C-3 Enemigos X-1 a X-4 y ajustes | 4,9 | C |
| 178 | E4 | CP | E4-20 (6/8) | C-3 Enemigos X-1 a X-4 y ajustes | 4,9 | C |
| 179 | E4 | CP | E4-20 (7/8) | C-3 Enemigos X-1 a X-4 y ajustes | 4,9 | C |
| 180 | E4 | CP | E4-20 (8/8) | C-3 Enemigos X-1 a X-4 y ajustes | 4,9 | C |
| 181 | E4 | CP | E4-21 (1/2) | K-3 Cangrejo guardián | 4,5 | C |
| 182 | E4 | CP | E4-21 (2/2) | K-3 Cangrejo guardián | 4,5 | C |
| 183 | E4 | CP | E4-22 | K-4 Reserva de la caja | 5,0 | C |
| 184 | E4 | CP | E4-24 (1/3) | K-6 Progresión y recompensas | 4,0 | C |
| 185 | E4 | CP | E4-24 (2/3) | K-6 Progresión y recompensas | 4,0 | C |
| 186 | E4 | CP | E4-24 (3/3) | K-6 Progresión y recompensas | 4,0 | C |
| 187 | E4 | CP | E4-25 (1/4) | C-6 Red, presupuestos e instrumentación del Coop | 4,8 | C |
| 188 | E4 | CP | E4-25 (2/4) | C-6 Red, presupuestos e instrumentación del Coop | 4,8 | C |
| 189 | E4 | CP | E4-25 (3/4) | C-6 Red, presupuestos e instrumentación del Coop | 4,8 | C |
| 190 | E4 | CP | E4-25 (4/4) | C-6 Red, presupuestos e instrumentación del Coop | 4,8 | C |
| 191 | E4 | CP | E4-26 (1/3) | C-7 Pruebas comunes | 4,7 | C |
| 192 | E4 | CP | E4-26 (2/3) | C-7 Pruebas comunes | 4,7 | C |
| 193 | E4 | CP | E4-26 (3/3) | C-7 Pruebas comunes | 4,7 | C |
| 194 | E4 | CP | E4-27 (1/5) | K-7 Contenido de CP01 | 4,8 | C |
| 195 | E4 | CP | E4-27 (2/5) | K-7 Contenido de CP01 | 4,8 | C |
| 196 | E4 | CP | E4-27 (3/5) | K-7 Contenido de CP01 | 4,8 | C |
| 197 | E4 | CP | E4-27 (4/5) | K-7 Contenido de CP01 | 4,8 | C |
| 198 | E4 | CP | E4-27 (5/5) | K-7 Contenido de CP01 | 4,8 | C |
| 199 | E4 | CP | E4-28 (1/2) | K-8 Retoque de C01 | 5,0 | C |
| 200 | E4 | CP | E4-28 (2/2) | K-8 Retoque de C01 | 5,0 | C |
| 201 | E4 | CP | E4-29 (1/2) | K-9 Pruebas de reglas Coop | 5,0 | C |
| 202 | E4 | CP | E4-29 (2/2) | K-9 Pruebas de reglas Coop | 5,0 | C |
| 203 | E4 | UI | E4-23 (1/4) | K-5 Interfaz Coop | 4,2 | C |
| 204 | E4 | UI | E4-23 (2/4) | K-5 Interfaz Coop | 4,2 | C |
| 205 | E4 | UI | E4-23 (3/4) | K-5 Interfaz Coop | 4,2 | C |
| 206 | E4 | UI | E4-23 (4/4) | K-5 Interfaz Coop | 4,2 | C |
| 207 | E12 | UI | E12-01 (1/3) | U5 Selector de modo y mapa | 5,0 | C |
| 208 | E12 | UI | E12-01 (2/3) | U5 Selector de modo y mapa | 5,0 | C |
| 209 | E12 | UI | E12-01 (3/3) | U5 Selector de modo y mapa | 5,0 | C |
| 210 | E12 | UI | E12-02 (1/6) | C-4 Interfaz común (incluye U2, U3) | 5,1 | C |
| 211 | E12 | UI | E12-02 (2/6) | C-4 Interfaz común (incluye U2, U3) | 5,1 | C |
| 212 | E12 | UI | E12-02 (3/6) | C-4 Interfaz común (incluye U2, U3) | 5,1 | C |
| 213 | E12 | UI | E12-02 (4/6) | C-4 Interfaz común (incluye U2, U3) | 5,1 | C |
| 214 | E12 | UI | E12-02 (5/6) | C-4 Interfaz común (incluye U2, U3) | 5,1 | C |
| 215 | E12 | UI | E12-02 (6/6) | C-4 Interfaz común (incluye U2, U3) | 5,1 | C |
| 216 | E12 | UI | E12-03 (1/3) | C-5 Efectos y sonido comunes | 5,5 | C |
| 217 | E12 | UI | E12-03 (2/3) | C-5 Efectos y sonido comunes | 5,5 | C |
| 218 | E12 | UI | E12-03 (3/3) | C-5 Efectos y sonido comunes | 5,5 | C |
| 219 | E12 | UI | E12-05 | Textos de Briefing y Pausa | 1,5 | C |
| 220 | E16 | QA | E16-04 (1/3) | Playtest Shipping en Steam, iteración 1 (tras F2) | 5,3 | P · R |
| 221 | E16 | QA | E16-04 (2/3) | Playtest Shipping en Steam, iteración 1 (tras F2) | 5,3 | P · R |
| 222 | E16 | QA | E16-04 (3/3) | Playtest Shipping en Steam, iteración 1 (tras F2) | 5,3 | P · R |
| 223 | E16 | QA | E16-08 | Playtest de diseño Coop (3-4) | 3,5 | P · R |

### Ola 7. Todos contra Todos (MVP)

| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |
|---|---|---|---|---|---|---|
| 224 | E2 | CP | E2-20 (1/4) | ATN_BridgeSpan sobre el puente de Mokius | 5,0 | C |
| 225 | E2 | CP | E2-20 (2/4) | ATN_BridgeSpan sobre el puente de Mokius | 5,0 | C |
| 226 | E2 | CP | E2-20 (3/4) | ATN_BridgeSpan sobre el puente de Mokius | 5,0 | C |
| 227 | E2 | CP | E2-20 (4/4) | ATN_BridgeSpan sobre el puente de Mokius | 5,0 | C |
| 228 | E7 | TC | E7-01 (1/3) | Modo TcT: fases, reloj, conchas, muerte súbita | 5,3 | C |
| 229 | E7 | TC | E7-01 (2/3) | Modo TcT: fases, reloj, conchas, muerte súbita | 5,3 | C |
| 230 | E7 | TC | E7-01 (3/3) | Modo TcT: fases, reloj, conchas, muerte súbita | 5,3 | C |
| 231 | E7 | TC | E7-02 | LastInstigator y atribución | 5,0 | C |
| 232 | E7 | TC | E7-03 | Reaparición en nidos con inmunidad | 5,0 | C |
| 233 | E7 | TC | E7-04 | Loot por puntos | 3,5 | C |
| 234 | E7 | TC | E7-07 | Viento (ATN_WindGust) | 6,0 | C |
| 235 | E7 | TC | E7-08 | Barricadas y marcadores de P01 | 2,5 | C |
| 236 | E7 | TC | E7-09 (1/3) | Hundimiento (ATN_SinkingPlatform) | 4,0 | C |
| 237 | E7 | TC | E7-09 (2/3) | Hundimiento (ATN_SinkingPlatform) | 4,0 | C |
| 238 | E7 | TC | E7-09 (3/3) | Hundimiento (ATN_SinkingPlatform) | 4,0 | C |
| 239 | E7 | TC | E7-10 (1/3) | Objetos 1-5 | 5,0 | C |
| 240 | E7 | TC | E7-10 (2/3) | Objetos 1-5 | 5,0 | C |
| 241 | E7 | TC | E7-10 (3/3) | Objetos 1-5 | 5,0 | C |
| 242 | E7 | TC | E7-12 (1/2) | Aparición: caja del cielo con gaviota y cofre central | 3,5 | C |
| 243 | E7 | TC | E7-12 (2/2) | Aparición: caja del cielo con gaviota y cofre central | 3,5 | C |
| 244 | E7 | TC | E7-13 (1/6) | Mapas MVP: P01, A01 Diana, I01 Filipinas | 4,8 | C |
| 245 | E7 | TC | E7-13 (2/6) | Mapas MVP: P01, A01 Diana, I01 Filipinas | 4,8 | C |
| 246 | E7 | TC | E7-13 (3/6) | Mapas MVP: P01, A01 Diana, I01 Filipinas | 4,8 | C |
| 247 | E7 | TC | E7-13 (4/6) | Mapas MVP: P01, A01 Diana, I01 Filipinas | 4,8 | C |
| 248 | E7 | TC | E7-13 (5/6) | Mapas MVP: P01, A01 Diana, I01 Filipinas | 4,8 | C |
| 249 | E7 | TC | E7-13 (6/6) | Mapas MVP: P01, A01 Diana, I01 Filipinas | 4,8 | C |
| 250 | E7 | TC | E7-14 (1/3) | Pruebas y playtest 4P/8P | 4,7 | C · R |
| 251 | E7 | TC | E7-14 (2/3) | Pruebas y playtest 4P/8P | 4,7 | C · R |
| 252 | E7 | TC | E7-14 (3/3) | Pruebas y playtest 4P/8P | 4,7 | C · R |
| 253 | E7 | TC | E7-15 (1/4) | Objetos 6-8: dardo de noqueo, trabuco, garfio | 5,2 | C |
| 254 | E7 | TC | E7-15 (2/4) | Objetos 6-8: dardo de noqueo, trabuco, garfio | 5,2 | C |
| 255 | E7 | TC | E7-15 (3/4) | Objetos 6-8: dardo de noqueo, trabuco, garfio | 5,2 | C |
| 256 | E7 | TC | E7-15 (4/4) | Objetos 6-8: dardo de noqueo, trabuco, garfio | 5,2 | C |
| 257 | E7 | TC | E7-16 (1/2) | Gaviotas con suelta ajustada y cangrejos | 3,2 | C |
| 258 | E7 | TC | E7-16 (2/2) | Gaviotas con suelta ajustada y cangrejos | 3,2 | C |
| 259 | E7 | TC | E7-17 | Corte de puentes en TcT | 5,0 | C |
| 260 | E7 | UI | E7-05 (1/3) | HUD de TcT (U8) | 4,0 | C |
| 261 | E7 | UI | E7-05 (2/3) | HUD de TcT (U8) | 4,0 | C |
| 262 | E7 | UI | E7-05 (3/3) | HUD de TcT (U8) | 4,0 | C |
| 263 | E7 | UI | E7-06 | Tally y Champion por puntos | 5,0 | C |
| 264 | E7 | ART | E7-11 | Iconos por render | 3,5 | P |
| 265 | E16 | QA | E16-05 (1/3) | Playtest Shipping en Steam, iteración 2 (Carrera y tortuga) | 5,3 | P · R |
| 266 | E16 | QA | E16-05 (2/3) | Playtest Shipping en Steam, iteración 2 (Carrera y tortuga) | 5,3 | P · R |
| 267 | E16 | QA | E16-05 (3/3) | Playtest Shipping en Steam, iteración 2 (Carrera y tortuga) | 5,3 | P · R |

### Ola 8. 2 vs 2 (MVP)

| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |
|---|---|---|---|---|---|---|
| 268 | E8 | DV | E8-01 (1/7) | Lógica del 2 vs 2 | 5,1 | C |
| 269 | E8 | DV | E8-01 (2/7) | Lógica del 2 vs 2 | 5,1 | C |
| 270 | E8 | DV | E8-01 (3/7) | Lógica del 2 vs 2 | 5,1 | C |
| 271 | E8 | DV | E8-01 (4/7) | Lógica del 2 vs 2 | 5,1 | C |
| 272 | E8 | DV | E8-01 (5/7) | Lógica del 2 vs 2 | 5,1 | C |
| 273 | E8 | DV | E8-01 (6/7) | Lógica del 2 vs 2 | 5,1 | C |
| 274 | E8 | DV | E8-01 (7/7) | Lógica del 2 vs 2 | 5,1 | C |
| 275 | E8 | DV | E8-02 (1/3) | Agarre de rivales y zonas de contacto | 4,0 | C |
| 276 | E8 | DV | E8-02 (2/3) | Agarre de rivales y zonas de contacto | 4,0 | C |
| 277 | E8 | DV | E8-02 (3/3) | Agarre de rivales y zonas de contacto | 4,0 | C |
| 278 | E8 | DV | E8-03 | Empuje de pawn y RaiseEndTime en la compuerta | 3,0 | C |
| 279 | E8 | DV | E8-05 (1/3) | Efectos 2 vs 2 | 4,3 | C |
| 280 | E8 | DV | E8-05 (2/3) | Efectos 2 vs 2 | 4,3 | C |
| 281 | E8 | DV | E8-05 (3/3) | Efectos 2 vs 2 | 4,3 | C |
| 282 | E8 | DV | E8-06 (1/4) | Mapa DV01_canal y validador de simetría | 4,2 | C |
| 283 | E8 | DV | E8-06 (2/4) | Mapa DV01_canal y validador de simetría | 4,2 | C |
| 284 | E8 | DV | E8-06 (3/4) | Mapa DV01_canal y validador de simetría | 4,2 | C |
| 285 | E8 | DV | E8-06 (4/4) | Mapa DV01_canal y validador de simetría | 4,2 | C |
| 286 | E8 | DV | E8-07 (1/4) | Pruebas y PIE 4P | 4,2 | C · R |
| 287 | E8 | DV | E8-07 (2/4) | Pruebas y PIE 4P | 4,2 | C · R |
| 288 | E8 | DV | E8-07 (3/4) | Pruebas y PIE 4P | 4,2 | C · R |
| 289 | E8 | DV | E8-07 (4/4) | Pruebas y PIE 4P | 4,2 | C · R |
| 290 | E8 | DV | E8-08 | Retirar el 2 vs 2 procedural de carriles | 2,5 | C |
| 291 | E8 | UI | E8-04 (1/7) | Interfaz 2 vs 2 | 5,0 | C |
| 292 | E8 | UI | E8-04 (2/7) | Interfaz 2 vs 2 | 5,0 | C |
| 293 | E8 | UI | E8-04 (3/7) | Interfaz 2 vs 2 | 5,0 | C |
| 294 | E8 | UI | E8-04 (4/7) | Interfaz 2 vs 2 | 5,0 | C |
| 295 | E8 | UI | E8-04 (5/7) | Interfaz 2 vs 2 | 5,0 | C |
| 296 | E8 | UI | E8-04 (6/7) | Interfaz 2 vs 2 | 5,0 | C |
| 297 | E8 | UI | E8-04 (7/7) | Interfaz 2 vs 2 | 5,0 | C |
| 298 | E12 | UI | E12-04 (1/3) | U9 Tally y Champion por equipos | 4,0 | C |
| 299 | E12 | UI | E12-04 (2/3) | U9 Tally y Champion por equipos | 4,0 | C |
| 300 | E12 | UI | E12-04 (3/3) | U9 Tally y Champion por equipos | 4,0 | C |

### Ola 9. Rally: mapas, biplaza y pulido

| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |
|---|---|---|---|---|---|---|
| 301 | E6 | PY | E6-14 (1/5) | F6b Presupuesto de malla del Rally | 5,0 | P |
| 302 | E6 | PY | E6-14 (2/5) | F6b Presupuesto de malla del Rally | 5,0 | P |
| 303 | E6 | PY | E6-14 (3/5) | F6b Presupuesto de malla del Rally | 5,0 | P |
| 304 | E6 | PY | E6-14 (4/5) | F6b Presupuesto de malla del Rally | 5,0 | P |
| 305 | E6 | PY | E6-14 (5/5) | F6b Presupuesto de malla del Rally | 5,0 | P |
| 306 | E6 | PY | E6-27 | Atribuciones de MDE en el Rally | 2,0 | P |
| 307 | E6 | VH | E6-15 (1/6) | B9 E01B completo | 4,8 | C |
| 308 | E6 | VH | E6-15 (2/6) | B9 E01B completo | 4,8 | C |
| 309 | E6 | VH | E6-15 (3/6) | B9 E01B completo | 4,8 | C |
| 310 | E6 | VH | E6-15 (4/6) | B9 E01B completo | 4,8 | C |
| 311 | E6 | VH | E6-15 (5/6) | B9 E01B completo | 4,8 | C |
| 312 | E6 | VH | E6-15 (6/6) | B9 E01B completo | 4,8 | C |
| 313 | E6 | VH | E6-16 (1/3) | B9 Lazo, circuito, Laps y ventana de arco | 4,7 | C |
| 314 | E6 | VH | E6-16 (2/3) | B9 Lazo, circuito, Laps y ventana de arco | 4,7 | C |
| 315 | E6 | VH | E6-16 (3/3) | B9 Lazo, circuito, Laps y ventana de arco | 4,7 | C |
| 316 | E6 | VH | E6-17 (1/2) | B9 L10 Japón Rally | 5,0 | C |
| 317 | E6 | VH | E6-17 (2/2) | B9 L10 Japón Rally | 5,0 | C |
| 318 | E6 | VH | E6-18 (1/2) | B9 I03-R Tortuga Magna | 5,0 | C |
| 319 | E6 | VH | E6-18 (2/2) | B9 I03-R Tortuga Magna | 5,0 | C |
| 320 | E6 | VH | E6-19 (1/3) | B2 Asientos biplaza | 4,7 | C |
| 321 | E6 | VH | E6-19 (2/3) | B2 Asientos biplaza | 4,7 | C |
| 322 | E6 | VH | E6-19 (3/3) | B2 Asientos biplaza | 4,7 | C |
| 323 | E6 | VH | E6-20 (1/2) | B2 A1 Lanzar (artillera) | 5,0 | C |
| 324 | E6 | VH | E6-20 (2/2) | B2 A1 Lanzar (artillera) | 5,0 | C |
| 325 | E6 | VH | E6-21 (1/3) | B2 A2 Contrapeso | 4,0 | C |
| 326 | E6 | VH | E6-21 (2/3) | B2 A2 Contrapeso | 4,0 | C |
| 327 | E6 | VH | E6-21 (3/3) | B2 A2 Contrapeso | 4,0 | C |
| 328 | E6 | VH | E6-22 (1/4) | B5 Motor, derrape y boost (FX y SFX) | 4,4 | C |
| 329 | E6 | VH | E6-22 (2/4) | B5 Motor, derrape y boost (FX y SFX) | 4,4 | C |
| 330 | E6 | VH | E6-22 (3/4) | B5 Motor, derrape y boost (FX y SFX) | 4,4 | C |
| 331 | E6 | VH | E6-22 (4/4) | B5 Motor, derrape y boost (FX y SFX) | 4,4 | C |
| 332 | E6 | VH | E6-24 (1/3) | B7 Skins del buggy | 5,3 | C |
| 333 | E6 | VH | E6-24 (2/3) | B7 Skins del buggy | 5,3 | C |
| 334 | E6 | VH | E6-24 (3/3) | B7 Skins del buggy | 5,3 | C |
| 335 | E6 | VH | E6-25 (1/2) | B7 Importar el kit del buggy low poly | 4,2 | C |
| 336 | E6 | VH | E6-25 (2/2) | B7 Importar el kit del buggy low poly | 4,2 | C |
| 337 | E6 | VH | E6-26 | B8 Red de 8 buggies | 6,0 | C |
| 338 | E6 | VH | E6-28 (1/2) | B10 Pruebas del Rally | 5,0 | C · R |
| 339 | E6 | VH | E6-28 (2/2) | B10 Pruebas del Rally | 5,0 | C · R |
| 340 | E6 | VH | E6-29 (1/5) | Opción E de red (autoridad del conductor) | 5,0 | C |
| 341 | E6 | VH | E6-29 (2/5) | Opción E de red (autoridad del conductor) | 5,0 | C |
| 342 | E6 | VH | E6-29 (3/5) | Opción E de red (autoridad del conductor) | 5,0 | C |
| 343 | E6 | VH | E6-29 (4/5) | Opción E de red (autoridad del conductor) | 5,0 | C |
| 344 | E6 | VH | E6-29 (5/5) | Opción E de red (autoridad del conductor) | 5,0 | C |
| 345 | E6 | UI | E6-23 (1/6) | B6 Interfaz del Rally | 5,0 | C |
| 346 | E6 | UI | E6-23 (2/6) | B6 Interfaz del Rally | 5,0 | C |
| 347 | E6 | UI | E6-23 (3/6) | B6 Interfaz del Rally | 5,0 | C |
| 348 | E6 | UI | E6-23 (4/6) | B6 Interfaz del Rally | 5,0 | C |
| 349 | E6 | UI | E6-23 (5/6) | B6 Interfaz del Rally | 5,0 | C |
| 350 | E6 | UI | E6-23 (6/6) | B6 Interfaz del Rally | 5,0 | C |
| 351 | E10 | VH | E10-16 | Voz del conductor en el Rally | 2,0 | C |
| 352 | E16 | QA | E16-10 | Playtest de la copa de Rally | 3,0 | P · R |

### Ola 10. Mapas, Foto→Mapa y contenido

| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |
|---|---|---|---|---|---|---|
| 353 | E2 | DOC | E2-25 | Actualizar el Catálogo de mapas | 1,5 | P |
| 354 | E3 | PY | E3-01 | Esquema y validador de MapSketch,json | 5,0 | P |
| 355 | E3 | PY | E3-02 | Análisis de imagen → boceto | 5,0 | P |
| 356 | E3 | PY | E3-03 | Adaptador to_path | 5,0 | P |
| 357 | E3 | PY | E3-04 (1/2) | Adaptadores to_island y to_arena | 3,5 | P |
| 358 | E3 | PY | E3-04 (2/2) | Adaptadores to_island y to_arena | 3,5 | P |
| 359 | E3 | PY | E3-05 (1/2) | Expansor de marcadores y plantillas de puzzle | 3,5 | P |
| 360 | E3 | PY | E3-05 (2/2) | Expansor de marcadores y plantillas de puzzle | 3,5 | P |
| 361 | E3 | PY | E3-07 | Pruebas de Foto→Mapa | 3,5 | P |
| 362 | E3 | PY | E3-08 | Piloto: boceto de Rodrigo → mapa Coop | 5,0 | P · R |
| 363 | E3 | CP | E3-06 (1/2) | Montaje del nivel desde el boceto | 5,0 | C |
| 364 | E3 | CP | E3-06 (2/2) | Montaje del nivel desde el boceto | 5,0 | C |
| 365 | E3 | DOC | E3-09 | Guía para diseñadores y modeladores | 2,5 | P |
| 366 | E7 | TC | E7-18 (1/4) | Mapas fase B: A02, A05, L05 Venecia | 4,2 | C |
| 367 | E7 | TC | E7-18 (2/4) | Mapas fase B: A02, A05, L05 Venecia | 4,2 | C |
| 368 | E7 | TC | E7-18 (3/4) | Mapas fase B: A02, A05, L05 Venecia | 4,2 | C |
| 369 | E7 | TC | E7-18 (4/4) | Mapas fase B: A02, A05, L05 Venecia | 4,2 | C |
| 370 | E13 | ART | E13-02 (1/2) | Borradores IA pendientes | 5,0 | P |
| 371 | E13 | ART | E13-02 (2/2), E13-03 | Borradores IA pendientes; Lámina de la biblioteca IA para Rodrigo | 6,0 | P · R |
| 372 | E13 | DOC | E13-04 | Encargo priorizado al equipo de arte | 2,5 | P · R |

### Ola 11. Optimización, red a 8 y voz

| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |
|---|---|---|---|---|---|---|
| 373 | E9 | PL | E9-09 (1/2) | Ragdoll de muerte que rueda y se asienta | 4,0 | C |
| 374 | E9 | PL | E9-09 (2/2) | Ragdoll de muerte que rueda y se asienta | 4,0 | C |
| 375 | E10 | NET | E10-07 | Estado persistente por OnRep (N-C, M4) | 5,0 | C |
| 376 | E10 | NET | E10-12 (1/2) | Reglas de reenvío de voz | 4,5 | C |
| 377 | E10 | NET | E10-12 (2/2) | Reglas de reenvío de voz | 4,5 | C |
| 378 | E10 | NET | E10-13, E10-14 | Frecuencia y relevancia de actores siempre relevantes; TN,Voice,FakeTalk | 6,0 | C |
| 379 | E10 | NET | E10-15 | Telemetría LogTNNet y aviso de subida del anfitrión | 3,5 | C |
| 380 | E10 | NET | E10-18 (1/2) | Entrada tardía y reconexión en los modos nuevos | 3,5 | C |
| 381 | E10 | NET | E10-18 (2/2) | Entrada tardía y reconexión en los modos nuevos | 3,5 | C |
| 382 | E10 | QA | E10-17 (1/2) | Sesiones de prueba a 8 | 5,5 | P · R |
| 383 | E10 | QA | E10-17 (2/2) | Sesiones de prueba a 8 | 5,5 | P · R |
| 384 | E11 | FW | E11-04 (1/2) | Perfilado de render de mapas | 3,5 | C |
| 385 | E11 | FW | E11-04 (2/2) | Perfilado de render de mapas | 3,5 | C |
| 386 | E11 | FW | E11-05 | Coste de ítems y VFX con 8 jugadoras | 3,5 | C |
| 387 | E16 | QA | E16-06 (1/3) | Playtest Shipping en Steam, iteración 3 (TcT, 2 vs 2, Rally) | 5,3 | P · R |
| 388 | E16 | QA | E16-06 (2/3) | Playtest Shipping en Steam, iteración 3 (TcT, 2 vs 2, Rally) | 5,3 | P · R |
| 389 | E16 | QA | E16-06 (3/3) | Playtest Shipping en Steam, iteración 3 (TcT, 2 vs 2, Rally) | 5,3 | P · R |

### Ola 12. Limpieza y calidad

| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |
|---|---|---|---|---|---|---|
| 390 | E14 | PY | E14-03 | Python pospuesto (2,1) | 2,5 | P |
| 391 | E14 | CL | E14-01, E14-02 | Mapa01 a Deprecado; Retirar C++ de la rejilla (2,1) | 6,0 | C · R |
| 392 | E14 | CL | E14-04 | Run por chunks y LVL_Run (2,2, 2,3, 4,5) | 5,0 | C · R |
| 393 | E14 | CL | E14-05, E14-06 | MapVariantLoader y TerrainMeshTile (2,4, 4,8); SeagullActor, QuadActor y EnemySeagull (2,5, 2,6, Q11) | 5,0 | C |
| 394 | E14 | CL | E14-07 | Visor de terreno (2,7) y LVL_TestMap (4,6) | 4,0 | C |
| 395 | E14 | CL | E14-09 (1/2) | Calidad M2, M3, M9, M12 | 4,0 | C |
| 396 | E14 | CL | E14-09 (2/2) | Calidad M2, M3, M9, M12 | 4,0 | C |
| 397 | E14 | CL | E14-10 (1/4) | Calidad M5 y M6 | 5,0 | C |
| 398 | E14 | CL | E14-10 (2/4) | Calidad M5 y M6 | 5,0 | C |
| 399 | E14 | CL | E14-10 (3/4) | Calidad M5 y M6 | 5,0 | C |
| 400 | E14 | CL | E14-10 (4/4) | Calidad M5 y M6 | 5,0 | C |
| 401 | E14 | CL | E14-11 (1/4) | Calidad M7, M8, M11 | 4,2 | C |
| 402 | E14 | CL | E14-11 (2/4) | Calidad M7, M8, M11 | 4,2 | C |
| 403 | E14 | CL | E14-11 (3/4) | Calidad M7, M8, M11 | 4,2 | C |
| 404 | E14 | CL | E14-11 (4/4) | Calidad M7, M8, M11 | 4,2 | C |
| 405 | E14 | CL | E14-12 (1/5) | Calidad M13 y splits de ficheros | 5,4 | C |
| 406 | E14 | CL | E14-12 (2/5) | Calidad M13 y splits de ficheros | 5,4 | C |
| 407 | E14 | CL | E14-12 (3/5) | Calidad M13 y splits de ficheros | 5,4 | C |
| 408 | E14 | CL | E14-12 (4/5) | Calidad M13 y splits de ficheros | 5,4 | C |
| 409 | E14 | CL | E14-12 (5/5) | Calidad M13 y splits de ficheros | 5,4 | C |
| 410 | E14 | CL | E14-13 | Calidad LOW (L1, L2, L4, L5, L7, L8) | 4,0 | C |
| 411 | E14 | DOC | E14-08 | Propuestas a Alvaro y Mokius (4,1-4,4, 4,9-4,11) | 1,5 | P · R |

### Ola 13. Cierre Steam, localización y playtests finales

| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |
|---|---|---|---|---|---|---|
| 412 | E0 | INT | E0-16 | Fusión final macro-update → main | 3,5 | P · R |
| 413 | E1 | ST | E1-04, E1-05 | F0-T4 AppID configurable y bloqueo del 480; Trámite Steamworks | 6,0 | C · R |
| 414 | E1 | ST | E1-11 | Guardado asíncrono y Steam Cloud | 3,5 | C |
| 415 | E1 | ST | E1-12 (1/4) | Steam Deck y mando | 5,0 | C · R |
| 416 | E1 | ST | E1-12 (2/4) | Steam Deck y mando | 5,0 | C · R |
| 417 | E1 | ST | E1-12 (3/4) | Steam Deck y mando | 5,0 | C · R |
| 418 | E1 | ST | E1-12 (4/4) | Steam Deck y mando | 5,0 | C · R |
| 419 | E1 | ST | E1-13 (1/2) | Créditos y licencias | 4,0 | C |
| 420 | E1 | ST | E1-13 (2/2) | Créditos y licencias | 4,0 | C |
| 421 | E1 | ST | E1-14 | Icono y splash | 4,0 | C · A |
| 422 | E1 | ST | E1-15 (1/2) | Página de tienda | 4,0 | P · R |
| 423 | E1 | ST | E1-15 (2/2) | Página de tienda | 4,0 | P · R |
| 424 | E1 | ST | E1-16 | Perfil cosmético por cuenta de Steam (L3) | 2,5 | C |
| 425 | E5 | BR | E5-15 | Tope de conchas con 6+ jugadores | 1,0 | C |
| 426 | E11 | FW | E11-06 | Presupuestos por modo en Shipping | 5,0 | C |
| 427 | E11 | FW | E11-07 | Tamaño del paquete | 2,5 | C |
| 428 | E13 | ART | E13-06 (1/3) | Sustituir borradores por modelos finales | 4,2 | C |
| 429 | E13 | ART | E13-06 (2/3) | Sustituir borradores por modelos finales | 4,2 | C |
| 430 | E13 | ART | E13-06 (3/3) | Sustituir borradores por modelos finales | 4,2 | C |
| 431 | E15 | LOC | E15-01 (1/2) | Textos nuevos de los 5 modos en 13 idiomas | 5,0 | C |
| 432 | E15 | LOC | E15-01 (2/2) | Textos nuevos de los 5 modos en 13 idiomas | 5,0 | C |
| 433 | E15 | LOC | E15-02 | Restos sin localizar (M10, L6, 37 FromString) | 5,0 | C |
| 434 | E15 | LOC | E15-03, E15-04, E15-05 | Fuentes CJK Noto; Nombres de mapas y humor por idioma; LQA nativa | 6,0 | C · R |
| 435 | E15 | LOC | E15-06 | Validar ajustes y audio | 4,0 | C · R |
| 436 | E15 | LOC | E15-07 (1/4) | Logros y rich presence | 5,0 | C |
| 437 | E15 | LOC | E15-07 (2/4) | Logros y rich presence | 5,0 | C |
| 438 | E15 | LOC | E15-07 (3/4) | Logros y rich presence | 5,0 | C |
| 439 | E15 | LOC | E15-07 (4/4) | Logros y rich presence | 5,0 | C |
| 440 | E16 | DOC | E16-12 | Auditoría de cobertura final | 2,5 | P |
| 441 | E16 | QA | E16-07 (1/3) | Playtest Shipping en Steam, iteración 4 (release candidate) | 5,3 | P · R |
| 442 | E16 | QA | E16-07 (2/3) | Playtest Shipping en Steam, iteración 4 (release candidate) | 5,3 | P · R |
| 443 | E16 | QA | E16-07 (3/3) | Playtest Shipping en Steam, iteración 4 (release candidate) | 5,3 | P · R |
| 444 | E16 | QA | E16-11 | Revisión adversarial por épica | 6,0 | P |

### Ola 14. Después (no bloquea la 1.0)

| Sesión | Épica | Carril | Tareas | Objetivo | h | Paralelo |
|---|---|---|---|---|---|---|
| 445 | E5 | BR | E5-16 (1/4) | T8 Mallas y BP propios de tramo | 4,2 | C · A |
| 446 | E5 | BR | E5-16 (2/4) | T8 Mallas y BP propios de tramo | 4,2 | C · A |
| 447 | E5 | BR | E5-16 (3/4) | T8 Mallas y BP propios de tramo | 4,2 | C · A |
| 448 | E5 | BR | E5-16 (4/4) | T8 Mallas y BP propios de tramo | 4,2 | C · A |
| 449 | E5 | BR | E5-17 (1/2) | N3 y ayuda de primer uso | 3,8 | C |
| 450 | E5 | BR | E5-17 (2/2) | N3 y ayuda de primer uso | 3,8 | C |
| 451 | E6 | VH | E6-30 (1/20) | Países de Rally restantes (11) | 5,5 | C |
| 452 | E6 | VH | E6-30 (2/20) | Países de Rally restantes (11) | 5,5 | C |
| 453 | E6 | VH | E6-30 (3/20) | Países de Rally restantes (11) | 5,5 | C |
| 454 | E6 | VH | E6-30 (4/20) | Países de Rally restantes (11) | 5,5 | C |
| 455 | E6 | VH | E6-30 (5/20) | Países de Rally restantes (11) | 5,5 | C |
| 456 | E6 | VH | E6-30 (6/20) | Países de Rally restantes (11) | 5,5 | C |
| 457 | E6 | VH | E6-30 (7/20) | Países de Rally restantes (11) | 5,5 | C |
| 458 | E6 | VH | E6-30 (8/20) | Países de Rally restantes (11) | 5,5 | C |
| 459 | E6 | VH | E6-30 (9/20) | Países de Rally restantes (11) | 5,5 | C |
| 460 | E6 | VH | E6-30 (10/20) | Países de Rally restantes (11) | 5,5 | C |
| 461 | E6 | VH | E6-30 (11/20) | Países de Rally restantes (11) | 5,5 | C |
| 462 | E6 | VH | E6-30 (12/20) | Países de Rally restantes (11) | 5,5 | C |
| 463 | E6 | VH | E6-30 (13/20) | Países de Rally restantes (11) | 5,5 | C |
| 464 | E6 | VH | E6-30 (14/20) | Países de Rally restantes (11) | 5,5 | C |
| 465 | E6 | VH | E6-30 (15/20) | Países de Rally restantes (11) | 5,5 | C |
| 466 | E6 | VH | E6-30 (16/20) | Países de Rally restantes (11) | 5,5 | C |
| 467 | E6 | VH | E6-30 (17/20) | Países de Rally restantes (11) | 5,5 | C |
| 468 | E6 | VH | E6-30 (18/20) | Países de Rally restantes (11) | 5,5 | C |
| 469 | E6 | VH | E6-30 (19/20) | Países de Rally restantes (11) | 5,5 | C |
| 470 | E6 | VH | E6-30 (20/20) | Países de Rally restantes (11) | 5,5 | C |
| 471 | E6 | VH | E6-31 (1/14) | Resto de Después del Rally | 5,4 | C |
| 472 | E6 | VH | E6-31 (2/14) | Resto de Después del Rally | 5,4 | C |
| 473 | E6 | VH | E6-31 (3/14) | Resto de Después del Rally | 5,4 | C |
| 474 | E6 | VH | E6-31 (4/14) | Resto de Después del Rally | 5,4 | C |
| 475 | E6 | VH | E6-31 (5/14) | Resto de Después del Rally | 5,4 | C |
| 476 | E6 | VH | E6-31 (6/14) | Resto de Después del Rally | 5,4 | C |
| 477 | E6 | VH | E6-31 (7/14) | Resto de Después del Rally | 5,4 | C |
| 478 | E6 | VH | E6-31 (8/14) | Resto de Después del Rally | 5,4 | C |
| 479 | E6 | VH | E6-31 (9/14) | Resto de Después del Rally | 5,4 | C |
| 480 | E6 | VH | E6-31 (10/14) | Resto de Después del Rally | 5,4 | C |
| 481 | E6 | VH | E6-31 (11/14) | Resto de Después del Rally | 5,4 | C |
| 482 | E6 | VH | E6-31 (12/14) | Resto de Después del Rally | 5,4 | C |
| 483 | E6 | VH | E6-31 (13/14) | Resto de Después del Rally | 5,4 | C |
| 484 | E6 | VH | E6-31 (14/14) | Resto de Después del Rally | 5,4 | C |
| 485 | E7 | TC | E7-19 (1/2) | Aguja giratoria (A04) | 4,5 | C |
| 486 | E7 | TC | E7-19 (2/2) | Aguja giratoria (A04) | 4,5 | C |
| 487 | E7 | TC | E7-20 (1/15) | Mapas TcT Después | 5,3 | C |
| 488 | E7 | TC | E7-20 (2/15) | Mapas TcT Después | 5,3 | C |
| 489 | E7 | TC | E7-20 (3/15) | Mapas TcT Después | 5,3 | C |
| 490 | E7 | TC | E7-20 (4/15) | Mapas TcT Después | 5,3 | C |
| 491 | E7 | TC | E7-20 (5/15) | Mapas TcT Después | 5,3 | C |
| 492 | E7 | TC | E7-20 (6/15) | Mapas TcT Después | 5,3 | C |
| 493 | E7 | TC | E7-20 (7/15) | Mapas TcT Después | 5,3 | C |
| 494 | E7 | TC | E7-20 (8/15) | Mapas TcT Después | 5,3 | C |
| 495 | E7 | TC | E7-20 (9/15) | Mapas TcT Después | 5,3 | C |
| 496 | E7 | TC | E7-20 (10/15) | Mapas TcT Después | 5,3 | C |
| 497 | E7 | TC | E7-20 (11/15) | Mapas TcT Después | 5,3 | C |
| 498 | E7 | TC | E7-20 (12/15) | Mapas TcT Después | 5,3 | C |
| 499 | E7 | TC | E7-20 (13/15) | Mapas TcT Después | 5,3 | C |
| 500 | E7 | TC | E7-20 (14/15) | Mapas TcT Después | 5,3 | C |
| 501 | E7 | TC | E7-20 (15/15) | Mapas TcT Después | 5,3 | C |
| 502 | E8 | DV | E8-09 (1/11) | 2 vs 2 Después | 5,2 | C |
| 503 | E8 | DV | E8-09 (2/11) | 2 vs 2 Después | 5,2 | C |
| 504 | E8 | DV | E8-09 (3/11) | 2 vs 2 Después | 5,2 | C |
| 505 | E8 | DV | E8-09 (4/11) | 2 vs 2 Después | 5,2 | C |
| 506 | E8 | DV | E8-09 (5/11) | 2 vs 2 Después | 5,2 | C |
| 507 | E8 | DV | E8-09 (6/11) | 2 vs 2 Después | 5,2 | C |
| 508 | E8 | DV | E8-09 (7/11) | 2 vs 2 Después | 5,2 | C |
| 509 | E8 | DV | E8-09 (8/11) | 2 vs 2 Después | 5,2 | C |
| 510 | E8 | DV | E8-09 (9/11) | 2 vs 2 Después | 5,2 | C |
| 511 | E8 | DV | E8-09 (10/11) | 2 vs 2 Después | 5,2 | C |
| 512 | E8 | DV | E8-09 (11/11) | 2 vs 2 Después | 5,2 | C |
| 513 | E12 | UI | E12-06 | U10 Cartel de primera vez | 5,0 | C |
| 514 | E12 | UI | E12-07 (1/2) | F4 +1 flotante y marcas de derrape F5 | 4,8 | C |
| 515 | E12 | UI | E12-07 (2/2) | F4 +1 flotante y marcas de derrape F5 | 4,8 | C |
| 516 | E12 | UI | E12-08 (1/3) | Vista previa del mapa en la carga y menú con miniaturas | 4,0 | C |
| 517 | E12 | UI | E12-08 (2/3) | Vista previa del mapa en la carga y menú con miniaturas | 4,0 | C |
| 518 | E12 | UI | E12-08 (3/3) | Vista previa del mapa en la carga y menú con miniaturas | 4,0 | C |
| 519 | E13 | ART | E13-07 | Pose de la tortuga al volante | 3,5 | P |

## Notas del coordinador (2026-09-29)

- **C1 resuelta:** la Carrera se queda con el generador de Mokius (respuestas del director «Dejar la carrera como está» y «para carrera es mejor el de Mokius»). E5-18 se cancela.
- **Horas y sesiones:** las cifras de este documento son horas de persona sacadas de las specs. La velocidad real con agentes (rama de Mokius hecha en un fin de semana) es del orden de 15–25×: estimación operativa **20–30 sesiones largas (3–5 semanas)**, a recalibrar tras las 2–3 primeras sesiones de F0–F2 midiendo tareas cerradas por sesión.
- Regenerar el documento: `uv run python Scripts/roadmap/gen_roadmap.py` (datos en `Scripts/roadmap/roadmap_data.py`).
