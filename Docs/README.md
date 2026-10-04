# Documentación de Tortunavy

Un documento de referencia por tema. Lo retirado o superado está en [`Archivo/`](Archivo/), cada fichero con su cabecera de obsoleto. La memoria del equipo son las issues, no estos documentos.

## Plan y seguimiento

| Tema | Documento | Contenido |
|---|---|---|
| Plan vigente | [`2026-09-29-Plan-Maestro-Modos-y-Mapas.md`](2026-09-29-Plan-Maestro-Modos-y-Mapas.md) | Modos, mapas, fases F0-F8 y decisiones del director. |
| Roadmap | [`ROADMAP-macro-update.md`](ROADMAP-macro-update.md) | Desglose de tareas con horas y dependencias. |
| Bitácora | [`Bitacora.md`](Bitacora.md) | Historial de sesiones. |
| Estado de cobertura | [`Auditoria-Cobertura-2026-09-29.md`](Auditoria-Cobertura-2026-09-29.md) | Las 43 peticiones del director contra el repositorio. |
| Gaps de Steam | [`Analisis/2026-09-29/F_gaps_steam.md`](Analisis/2026-09-29/F_gaps_steam.md) | Auditoría de lo que falta para publicar. |
| Calidad y limpieza de código | [`Calidad-Codigo-2026-09-29.md`](Calidad-Codigo-2026-09-29.md), [`Limpieza-2026-09-29.md`](Limpieza-2026-09-29.md) | Auditoría de código vivo y de código muerto antes de Steam. |
| Pruebas de estrés | [`Estres-Monkey-2026-09-29.md`](Estres-Monkey-2026-09-29.md) | Monkey test y herramientas de estrés. |
| Render y GPU | [`Rendimiento_GPU_2026-10-02.md`](Rendimiento_GPU_2026-10-02.md) | Hilos de juego y render, GPU, draw calls y triángulos con ventana (Development y DebugGame). |
| Integración de la nube | [`Reviews/nube/integracion-2026-09-29.md`](Reviews/nube/integracion-2026-09-29.md) | Estado de la PR #9 (tarea E0-01 del roadmap). |

## Equipo y herramientas

| Tema | Documento | Contenido |
|---|---|---|
| Git y ramas | [`Flujo_Git.md`](Flujo_Git.md) | Flujo de ramas y reglas de `main`. |
| Comandos de prueba | [`Comandos_Prueba.md`](Comandos_Prueba.md) | Comandos de consola y tests. |
| Red local e informe de bug | [`Pruebas_Red_Local.md`](Pruebas_Red_Local.md) | 8 instancias locales con emulación de red, monkey en clientes remotos e informe con F8. |
| Localización | [`Localizacion.md`](Localizacion.md) | Idiomas, glosario y flujo de traducción. |
| Arte y assets | [`Inventario-Objetos-Arte-2026-09-29.md`](Inventario-Objetos-Arte-2026-09-29.md) | Inventario de objetos y presupuesto de arte. |
| Inventario de scripts | [`Inventario_Scripts.md`](Inventario_Scripts.md) | Cabeceras C++ por dominio (apoyo a la defensa). |

## Diseño y modos

| Tema | Documento | Contenido |
|---|---|---|
| Biblia del juego | [`Biblia_Tortunavy.md`](Biblia_Tortunavy.md) | Referencia de diseño: modos, tortuga, objetos, mundo. |
| Especificaciones de los modos | [`superpowers/specs/modos/README.md`](superpowers/specs/modos/README.md) | Coop, Carrera, Rally, Todos contra Todos y 2 vs 2. |
| Modos: interfaz y efectos | [`Modos-UI-FX-2026-09-29.md`](Modos-UI-FX-2026-09-29.md) | Pantallas de los cinco modos y 2 vs 2. |
| Puzzles | [`Catalogo-Puzzles-2026-09-29.md`](Catalogo-Puzzles-2026-09-29.md) | Plantillas y piezas de puzzle. |
| Modo carrera | [`Modo_Carrera.md`](Modo_Carrera.md) | La playa de la carrera a pie, rondas y objetos. |
| Rally | [`superpowers/specs/modos/03-Rally.md`](superpowers/specs/modos/03-Rally.md) | Especificación del Rally; base en [`Rally_E01B_y_Biplaza.md`](Rally_E01B_y_Biplaza.md) (E01B y buggy biplaza). |
| Modo VR | [`Modo_VR.md`](Modo_VR.md) | Realidad virtual. |
| Investigación de red y voz | [`Investigacion-8-Jugadores-Voz-2026-09-29.md`](Investigacion-8-Jugadores-Voz-2026-09-29.md) | 8 jugadores con voz en listen server. |
| Coop sobre ProcMap | [`Mapa_Procedural.md`](Mapa_Procedural.md) | Framework `World/ProcMap` y `ATN_ProcMapGameMode`. |
| Análisis del 29-09 | [`Analisis/2026-09-29/`](Analisis/2026-09-29/) | Informes A-G que alimentan el plan maestro. |

## Mapas y terreno

| Tema | Documento | Contenido |
|---|---|---|
| Terreno volumétrico (C01) | [`Diseno_Terreno_CaminoPrimero.md`](Diseno_Terreno_CaminoPrimero.md) | Método «camino primero» y su ampliación v2. |
| España E01 | [`2026-09-29-Terreno-Espana.md`](2026-09-29-Terreno-Espana.md) | Relieve real de la Península. |
| Plataformas P01 | [`2026-09-29-Terreno-Plataformas.md`](2026-09-29-Terreno-Plataformas.md) | Mesetas y puentes colgantes. |
| Catálogo de mapas | [`Catalogo-Mapas-2026-09-29.md`](Catalogo-Mapas-2026-09-29.md) | Un mapa por idioma, islas y arenas. |
| Foto a mapa | [`superpowers/specs/2026-09-29-foto-a-mapa-design.md`](superpowers/specs/2026-09-29-foto-a-mapa-design.md) | Diseño de la conversión de fotos en mapas. |
| Plan F0-F2 | [`superpowers/plans/2026-09-29-F0-F2-Coop-Steam.md`](superpowers/plans/2026-09-29-F0-F2-Coop-Steam.md) | Base Steam, terreno preparado y Coop sobre mapas fijos. |

## Sistemas y pantallas

| Tema | Documento | Contenido |
|---|---|---|
| Animación | [`Animacion_Tortuga.md`](Animacion_Tortuga.md) | `UTN_TurtleAnimInstance` sin AnimBP. |
| Sonido | [`Sonido_Tortuga.md`](Sonido_Tortuga.md) | Audio sintetizado de la tortuga. |
| Fantasma espectador | [`Fantasma_Espectador.md`](Fantasma_Espectador.md) | Espectador y reaparición desde un huevo. |
| Lobby | [`Lobby_Castillo.md`](Lobby_Castillo.md), [`Salas.md`](Salas.md), [`Tienda_Probador.md`](Tienda_Probador.md) | Castillo de arena, salas y tienda. |
| Menús y carga | [`Menu_Pausa.md`](Menu_Pausa.md), [`Pantalla_Carga.md`](Pantalla_Carga.md), [`Tutorial.md`](Tutorial.md) | Menú de pausa, pantalla de carga y tutorial. |
| Botín y decorados | [`Botin_Decorados.md`](Botin_Decorados.md) | Objetos de botín y decorado. |
