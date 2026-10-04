# Auditoría de cobertura de las peticiones del director (2026-09-29)

Rama: `macro-update` · Alcance: las 43 peticiones literales de la sesión 7e8d81ee (numeradas P01–P43 en orden de hora) contra los documentos del 29-09, el repositorio, el kanban `tortunabo` y la memoria del proyecto. Cada fila apunta a la tarea de `Docs/ROADMAP-macro-update.md` que la cubre.

Estados: **hecho** (entregado y en el repo), **en curso** (trabajo sin commitear o de otro agente activo), **planificado** (tarea con horas en el roadmap), **OLVIDADO → añadido** (no estaba en ningún documento o plan; se ha creado la tarea), **n/a** (no es una petición de trabajo).

## 1. Lo que se había olvidado (ya añadido al roadmap)

| # | Petición | Qué faltaba | Tarea nueva |
|---|---|---|---|
| 1 | P38 | Coliseo con bordes de agua: ni en el catálogo ni en `gen_terrain_inventados.py` ni en `terrain_shapes/` | E2-24 |
| 2 | P26, P27 | Qué pasa cuando un modelador retoca el terreno ya generado: el plan F0–F2 deja fuera `RebakeHeightsFromLevel` (desviación 6), así que el bake, la máscara y el rescate quedarían desfasados | E2-18 (y guía E3-09) |
| 3 | P07 | «Map rendering»: solo había presupuesto de malla; ninguna tarea de perfilado de render | E11-04 |
| 4 | P07 | «Item optimization»: solo el pool de objetos; nadie medía el coste por ítem activo con 8 | E11-05 |
| 5 | P01 | «Juntarlo todo en el main»: tras mover el trabajo a `macro-update` (P18, P21) ninguna tarea cerraba la fusión final | E0-16 |

Parciales completados: P40, P41 y P42 (aparición de objetos) estaban decididos en los anexos de las specs pero sin horas ni tarea (E7-12, E5-12, E6-12). P07 y P43 (voz y réplica a 8) no estaban en ningún plan hasta `Docs/Investigacion-8-Jugadores-Voz-2026-09-29.md`; sus 11 tareas entran como E10-09 a E10-17, E0-05, E11-01 y E11-02.

## 2. Tabla de las 43 peticiones

| P | Hora | Petición (resumen) | Dónde está cubierta | Estado |
|---|---|---|---|---|
| P01 | 15:14 | Megapulir Coop y Carrera; terreno volumétrico en ambos; mapa Coop del tamaño de C01 diseñado por Claude con los assets (cañones, zonas profundas); gaviotas que dejan en zonas no permitidas; analizar el mapa de puentes; puntos 1–4 (Coop, Carrera recta con leve rasante, compatibilidad, buggy de HellYeah en mapas tipo España); juntar todo en main | Coop: Plan §3.1, F0–F2, E2-01…E2-17, E4-01…E4-12, CP01 E4-07/E4-27; gaviotas: E2-07, E2-10; P01 analizado: Plan §3.4 (TcT, E7-13); compatibilidad: auditoría R10 (E4-04) y CI (E1-07); buggy: `B_buggy_sync.md` (eliminado), 03-Rally, E6; Carrera volumétrica: **contradicción C1** → E5-18 condicional; main: merge `41a0ee8a5` hecho, fusión final E0-16 | Coop y buggy **planificado**; análisis **hecho**; Carrera **contradicción**; fusión final **OLVIDADO → añadido** |
| P02 | 15:47 | Skins y pulido del buggy; mapas Coop a mano desde bocetos y uno diseñado por Claude; doble salto que lanza a la agarrada, sigue en pendiente y se estampa contra la pared | Skins E6-24, pulido E6-10; bocetos E3 (Foto→Mapa), CP01 E4-07; doble salto Plan §3.5, E9-01…E9-07 | planificado |
| P03 | 15:49 | Qué modos: uno con coches, uno Gang Beasts, quizá parkour; pocos y que aporten | Plan §0–1, specs 00–05 (5 modos); parkour descartado como modo y convertido en plantillas (Catálogo de puzzles §4) | hecho (decisión) |
| P04 | 15:56 | Doble salto, noqueo y caparazón ya existen para TcT; objetos nuevos (pistolas, piedras); bug del torbellino en caparazón que atraviesa el mapa | Objetos: 04-TcT §5, E7-10, E7-15; torbellino: kanban «Arreglo torbellino caparazón» (Done), regresión E9-08 | objetos **planificado**; torbellino **hecho** (falta verificación PIE E9-08) |
| P05 | 15:58 | ¿Ítems en el Rally? Todo asset lo hace arte: optimizar assets y efectos | Plan §3.3 (4 ítems), 03-Rally §6, E6-11; Inventario de arte; README de modos (2 assets imprescindibles) | hecho (spec) / planificado |
| P06 | 15:59 | `/compact` | — | n/a |
| P07 | 16:01 | Lista de objetos (decoración e interactuables) y cuántos hace arte; optimización; quitar un par de rutinas de Explored y añadir rutinas de PR; pulir: bugs, voz, ítems, render de mapas | Inventario (234 objetos, 5 encargos) **hecho**; rutinas de PR activas (PR #9 y #10 de la nube, `Docs/Reviews/nube/`) **hecho** (la configuración vive fuera del repo); bugs E0-02…E0-08; voz E10-09…E10-17; render E11-04; ítems E11-05 | inventario y rutinas **hecho**; voz **planificado**; render e ítems **OLVIDADO → añadido** |
| P08 | 16:07 | Usar el volumétrico propio (SkiTemplar), no Explored; ¿rutinas con repo privado? | Memoria del plan maestro (confirmado); rutinas funcionando con PR a la rama | hecho |
| P09 | 16:11 | Run/Demo fuera; segunda tortuga detrás (artillera); E01B a criterio, partidas rápidas según la velocidad del buggy; puentes de Mokius y naturales; asset low poly propio | Plan §7.1–7.4, §7.8; Run: E4-10…E4-12, E14-04; artillera E6-19…E6-21; E01B 03-Rally §4.3, E6-15; puentes E2-19, E2-20; low poly E13-01 (`Art/Source/Vehicles/Buggy`, sin commitear) | decisiones **hecho**; low poly **en curso**; resto **planificado** |
| P10 | 16:16 | TcT: estampado y pistolas con bola física; la pistola de ragdoll es de noqueo | Plan §7.7; E9-03, E9-05, E7-15 | planificado |
| P11 | 16:17 | Seguir puliendo, juntando y limpiando basura | E14 | planificado |
| P12 | 16:20 | Limpieza y optimización a criterio de Claude | Limpieza lotes 1–3 (commits `8d156488a`…`ea903c504`); resto E14 | hecho (lotes 1–3) / planificado |
| P13 | 16:22 | Un mapa por idioma, islas reales o ficticias para Rally y TcT, Filipinas rara, diana, originalidad | Catálogo de mapas (25); pilotos L10, I01, A01 (`48d1c92c3`); arenas A02–A06 (`65bad29aa`); países E2-21 | pilotos **hecho**; países **en curso** |
| P14 | 16:25 | ¿Más por analizar? Lista de assets, código deprecado o cutre | Inventario, Limpieza, Calidad (0 CRITICAL, 4 HIGH cerrados) | hecho (auditorías) |
| P15 | 16:30 | Limpieza: lo viejo a una carpeta DEPRECADO, lo cutre se arregla, no borrar diseño | Limpieza lotes 1–3 (`Deprecado/`, `/Game/_Deprecado`); pospuestos E14-01…E14-08 | hecho / planificado |
| P16 | 16:31 | Eliminar solo lo garantizado | Limpieza 1.1, 1.2, 2.9 borrados con garantía | hecho |
| P17 | 16:47 | Capturas y rama actual | Respondido en la sesión | n/a |
| P18 | 16:49 | Sacar todos los pushes a una rama | Rama `macro-update` | hecho |
| P19 | 16:51 | Seguimos trabajando | — | n/a |
| P20 | 17:04 | Mapa01 ya no se usa; el oficial es camino | Limpieza §Decisión (`6a1f1dd9e`), test que usa C01 (`7904c9849`); mover a `_Deprecado` E14-01; memoria desactualizada (C9) | decisión **hecho**; traslado **planificado** |
| P21 | 17:05 | Rutinas con PR a la rama `macro-update` | PR #9 rebasada a `macro-update`, PR #10 fusionada | hecho |
| P22 | 17:09 | Estado de modos, flujo, rama y rutinas; optimización en todo; Foto→Mapa preparado por Claude | Specs de modos; spec Foto→Mapa (`322110b19`); implementación E3-01…E3-09; optimización E11 | diseño **hecho**; implementación **planificado** |
| P23 | 17:09 | Qué mapa usa cada modo | Plan §0.2–0.3, README de modos | hecho |
| P24 | 17:10 | Todo lo colocado es editable, también el terreno | Foto→Mapa §5; importador con subniveles `_Terrain/_Markers/_Design` y `TN_REGENERATE` (E2-16); re-horneado E2-18 | planificado |
| P25 | 17:12 | 2 vs 2 volumétrico de puzzles por parejas, bifurcaciones más grandes y menos, carrera y putear al rival | Plan §7.9, 05-2vs2, Catálogo de puzzles §6, E8 | planificado |
| P26 | 17:16 | Puzzles posibles; Foto→Mapa aprobado; ¿qué pasa si un diseñador modela el terreno generado? | Catálogo de puzzles (15 plantillas) **hecho**; re-horneado **E2-18** | puzzles **hecho**; terreno retocado **OLVIDADO → añadido** |
| P27 | 17:20 | Los mapas se generan antes y luego los modeladores tocan lo que sea | E2-18, E3-09 | OLVIDADO → añadido |
| P28 | 17:23 | «Me gusta» | — | n/a |
| P29 | 17:32 | Modos, optimización, sistema de mapas, mapas nuevos, UI/FX, arreglos de física, buscar bugs, monkey, estrés al límite, sincronización, objetos nuevos, puzzles; permiso para assets IA en la biblioteca | Monkey E11-01 (`Source/.../Testing/`, sin commitear); estrés E11-02; biblioteca IA 20 assets (`65965cd20`…`acc516964`); UI/FX E12; red E10; objetos E5-10, E7-10, E7-15 | biblioteca **hecho**; monkey **en curso**; resto **planificado** |
| P30 | 17:33 | Japón y Filipinas deben copiar el relieve como España | `L10_japon_v2` (sin commitear), `countries.py`; E2-22 | en curso |
| P31 | 17:35 | Filipinas «rara» = adaptada a TcT o Rally, sin elevar el relieve | `I01_filipinas_v2` en `countries.py`; E2-22 | en curso |
| P32 | 17:35 | ¿Cuántos mapas? | Catálogo (25) + lote inventado N01–N16 + 13 países | hecho (respuesta) / en curso |
| P33 | 17:37 | Países enteros miniaturizados | Catálogo §Decisión; 03-Rally D6; `country.py` (6 de 13 países); E2-21 | en curso |
| P34 | 17:43 | Rally: no ir en dirección contraria, enderezar el coche, efectos de derrape y boost, interfaz, assets | `Rally_Sistemas.md`, 03-Rally §5, §7, §8, §12; E6-07, E6-08, E6-22, E6-23 | planificado |
| P35 | 17:44 | En 2 vs 2 se puede lanzar a los rivales | 05-2vs2 §5.4, UI-FX decisión 2; E8-02 | planificado |
| P36 | 17:46 | Spec completa de todos los modos con objetos; ahorrar assets en todo | specs 00–05 y README (2 assets imprescindibles) | hecho |
| P37 | 17:49 | 2 vs 2 ≈ Coop; Carrera ≈ Coop recto sin puzzles, caótico y rápido; TcT y Rally distintos | 01-Coop como base común, 02 y 05 como diferencias; C1 | hecho |
| P38 | 17:50 | Generar los otros países y muchos otros mapas: islas inventadas, coliseo con bordes de agua, más sitios | Países E2-21, inventados E2-23 (kit sin commitear), **coliseo E2-24** | países e inventados **en curso**; coliseo **OLVIDADO → añadido** |
| P39 | 17:52 | «161 horas mis huevos» | Estimación rehecha: 1 721–2 287 h MVP (roadmap §2) | n/a |
| P40 | 17:54 | TcT: ¿cómo aparecen los objetos? ¿Del cielo? | 04-TcT anexo; E7-12 | planificado (horas añadidas) |
| P41 | 17:55 | Carrera: ¿van apareciendo? | 02-Carrera anexo; E5-12 | planificado (horas añadidas) |
| P42 | 17:56 | Rally; patrones de diseño y sistema para encontrar bugs rápido | 03-Rally anexo D, E6-12; 00-Arquitectura, E0-02…E0-15 | planificado |
| P43 | 17:58 | Gran lista de TODOs con pulido y update; viabilidad de voz con 8; réplica a 8; buscar bugs; auditoría de lo olvidado | `Docs/ROADMAP-macro-update.md`; `Docs/Investigacion-8-Jugadores-Voz-2026-09-29.md` (viable con condiciones); E10; E0; este documento; auditoría final E16-12 | roadmap, investigación y auditoría **hecho**; tareas **planificado** |

Recuento (43): hecho 16 (P03, P05, P08, P12–P16, P18, P20–P23, P36, P37, P43; incluye decisiones y respuestas), en curso 5 (P29–P33), planificado 12 (P02, P04, P09–P11, P24, P25, P34, P35, P40–P42), con partes OLVIDADAS ya añadidas 5 (P01, P07, P26, P27, P38), n/a 5 (P06, P17, P19, P28, P39). Las peticiones que mezclan estados se cuentan por el que domina; una OLVIDADA cuenta como tal aunque el resto esté cubierto.

## 3. Contradicciones entre documentos y resolución vigente

| C | Tema | Documentos en conflicto | Resolución vigente | Acción |
|---|---|---|---|---|
| C1 | Terreno de la Carrera | P01 pide Carrera volumétrica «recta con leve cambio de rasante»; Plan §1 dec. 3, 02-Carrera §0, 01-Coop Anexo B Q2 y memoria: generador de Mokius y relieve intacto | **Generador de Mokius** (decisión posterior registrada a las 15:45) | **Confirmar con Rodrigo**; si reabre, E5-18 (16–24 h, condicional) |
| C2 | Jugadoras del Coop | Catálogo de puzzles «3 a 8»; UI-FX §2.1 «Coop 1 a 8»; 05-2vs2 §0.2 «3 a 8»; 01-Coop «1 a 4» | **Coop 1–4**, 2 vs 2 exactamente 4, Carrera/Rally/TcT 1–8 (2–8 en TcT) (01-Coop Anexo B Q1) | E0-17 corrige los tres documentos |
| C3 | Cierre tras el primero | 03-Rally §1 30 s; Anexo C 20 s «igual que Carrera y 2 vs 2»; 02-Carrera cuenta atrás de llegada 10 s; 05-2vs2 termina al instante | **Rally 20 s**, **Carrera 10 s** (existente), **2 vs 2 al instante** con metros de margen | E0-17 corrige la frase del Anexo C |
| C4 | Número de modos | Plan §0.1 «cuatro modos, nada más»; Plan §7.9 añade 2 vs 2 | **Cinco modos** | E0-17 |
| C5 | Assets nuevos imprescindibles | Plan §3.6: 11; Inventario: 5 encargos; 04-TcT K1: 4; README de modos: 2 + 5 de producto | **2 del juego** (kit del buggy biplaza, `M_BuggyPaint`) **+ 5 encargos de producto** (README) | E0-17 |
| C6 | Objetos de TcT | Plan §3.4: 7 (sin plátano); 04-TcT §5: 8 (con plátano) | **8** (04-TcT) | — |
| C7 | Torbellino del caparazón | Plan §4 y §6: «diagnóstico en curso»; 02-Carrera §0 y kanban: arreglado | **Arreglado**; falta regresión en PIE | E9-08 |
| C8 | Opción E de red del Rally | Plan §3.3 y 03-Rally §9: «solo si la medición lo exige»; Investigación de 8 jugadores: condición para 8 | **Planificada en el MVP** (E6-29), validar con la sonda de E6-26 | — |
| C9 | Mapa01 | Memoria `project_terreno_modular` y `MEMORY.md`: «vigente»; director (P20) y Limpieza: obsoleto, oficial camino | **Camino (C01)**; memoria desactualizada | E0-17 (memoria), E14-01 |
| C10 | Puentes con física de Mokius | P01: existen en el procedural de Mokius; Plan (Q6) y memoria: no están en `Content/` | **No están en el repo** | E2-19 (pedirlos a Mokius), E2-20 |
| C11 | Escala de E01B | Catálogo §1: 0,4 km/m, ventana 1 330 × 2 190 (región); 03-Rally §4.2: 0,42 km/m, 2 380 × 2 024 (país) | **0,42 km/m, país entero** (decisión «países enteros») | E2-25 |
| C12 | Mapas por idioma | Catálogo §1: regiones (Escocia, Provenza, Rin…); decisión del director (P33): países enteros | **Países enteros** | E2-25 reescribe §1 |
| C13 | Agarrar a rivales en 2 vs 2 | UI-FX §4.2: no en el MVP; director (P35): sí | **Sí, a quien está en caparazón** | — |
| C14 | Mapas del Coop | Plan §3.1: C01, E01, F01, CP01; Catálogo: E01 y F01 son de Rally | **C01 y CP01** (01-Coop Q9) | — |
| C15 | Rebote del doble salto | Plan §3.5: 0,45 y 60 %; código: 0,35 y 0,75 | **Plan solo en vuelo; suelo sin cambios** (04-TcT K2) | Confirmar en E9-07 |
| C16 | Nidos de la Diana | Catálogo A01: nidos en r = 25, que se hunde a 105 s | **Los nidos se mueven a los anillos que quedan** (04-TcT K4) | E2-25 |
| C17 | Parrilla del Rally | Diseño E01B: invertida; Rally_Sistemas: por posición o aleatoria | **Carrera 1 al azar; 2 y 3 invertida por puntos de copa** (Anexo C) | — |
| C18 | AppID propio | Plan §4 y F_gaps: P0 inmediato; director (29-09 tarde): más tarde | **Al final**, sin bloquear (sigue 480) | E1-04, E1-05 en la ola 13 |
| C19 | `OnTravelFailure` | F_gaps N-E: severidad alta; director: hueco de robustez con Seamless Travel (`OnNetworkFailure` ya en `MP_GameInstance.cpp:108`) | **Hueco de robustez, 2 h** | E1-03 |
| C20 | Duración del Rally | Plan §3.3: «carrera de 8–10 min»; 03-Rally: 120 s | **Copa de 3 × ~2 min ≈ 8 min de sesión** | — |
| C21 | Guardados versionados | F_gaps #4: sin versión; commit `0d393c6f6`: versión, migración y cuarentena | **Hechos**; faltan escritura asíncrona y Steam Cloud | E1-11 |
| C22 | `MapsToCook` | F_gaps #10: abierto y peor; Limpieza 2.8 (`ab4744e14`): lista explícita | **Hecho**; falta un cocinado completo | E1-09 |
| C23 | Pistola de ragdoll | Plan §3.4 (primera versión) y P04: «activa ragdolls»; director (P10): pistola de **noqueo** | **Noqueo en el servidor; ragdoll solo cosmético** | E7-15 |
| C24 | Ragdoll de muerte | Memoria `project_ragdoll_red`: ragdoll físico que rueda y revive donde queda; Plan §3.5: ragdoll cosmético | **Noqueo cosmético**; la muerte puede rodar con posición final del servidor | E9-09, confirmar con Rodrigo |
| C25 | Dirección contraria del Rally | `Rally_Sistemas.md`: `TNRally::IsWrongWay` «puro, testeado»; repo: no existe | **No existe** (0 coincidencias en `Source/`) | E6-07 |
| C26 | Puntos por entorno en TcT | Plan y análisis D: muerte por entorno = +1 a la líder; hundimiento de arenas | **El hundimiento no puntúa**; gaviota, cangrejo y aguja sí (04-TcT K6) | — |
| C27 | Tamaño del trabajo | Plan §0.9: 549–752 h (solo código F0–F8); P39: «161 horas» | **1 721–2 287 h MVP + 311–460 h Después** (suma de specs, roadmap §2) | — |

## 4. Otras comprobaciones contra el repositorio

- Del plan F0–F2 no hay nada implementado salvo la T2 (`Source/Logs`): `OnTravelFailure`, `UTN_CoopMapData`, `ATN_CoopMarker`, `ITN_SafeGroundProvider`, `ATN_MatchGameModeBase`, `walk_with_parents` y `fit_budget` tienen 0 coincidencias; no existen `Scripts/ci/` ni `.github/`.
- Arquitectura común y sistema de bugs sin empezar: `UTN_ModeRules`, `ETNMatchPhase`, `ITN_Activatable`, `LogTNMatch` y el informe F8 tienen 0 coincidencias; `TN.Stress` solo aparece en documentos.
- En curso sin commitear (otros agentes): `Source/Tortunabo/{Public,Private}/Testing/` (monkey), `Art/Source/Vehicles/Buggy/` (kit low poly), `Scripts/gen_terrain_country.py` y `terrain_geo/countries.py` (6 de 13 países), `Scripts/gen_terrain_inventados.py` (N01–N16 por definir) y un cambio de 3 líneas en `TortugaCharacter.h`.
- PR #9 de la nube (bugs) espera compilación local (E0-01).
- Kanban: la tarjeta «Ejecutar plan F0-F2 (37 tareas)» y el gate de playtest siguen abiertos; conviene sustituir las tarjetas por las olas del roadmap.
- Memoria que hay que actualizar: `project_terreno_modular` (Mapa01 obsoleto), `project_demo_c01` (LVL_Demo01 y RunGameMode se retiran) y `MEMORY.md` (entrada «Terreno: mapa volumétrico fijo»).
