# Documentación de Tortunavy

Un documento de referencia por tema. Desde el 06-10 el juego tiene un solo modo. Lo descartado (otros modos, mapas procedurales, VR, tutorial, fantasma actual y su documentación) está en la rama `chamber`. La memoria del equipo son las issues, no estos documentos.

## Plan y seguimiento

| Tema | Documento | Contenido |
|---|---|---|
| Plan vigente | [`2026-10-06-Plan-Maestro-Modo-Unico.md`](2026-10-06-Plan-Maestro-Modo-Unico.md) | Modo único, decisiones del director, Excel de diseño (enemigos, objetos, economía) y fases F0-F8. |
| Bitácora | [`Bitacora.md`](Bitacora.md) | Historial de sesiones. |
| Gaps de Steam | [`Analisis/2026-09-29/F_gaps_steam.md`](Analisis/2026-09-29/F_gaps_steam.md) | Auditoría de lo que falta para publicar. |
| Calidad de código | [`Calidad-Codigo-2026-09-29.md`](Calidad-Codigo-2026-09-29.md) | Auditoría de código vivo antes de Steam. |
| Pruebas de estrés | [`Estres-Monkey-2026-09-29.md`](Estres-Monkey-2026-09-29.md) | Monkey test y herramientas de estrés. |
| Render y GPU | [`Rendimiento_GPU_2026-10-02.md`](Rendimiento_GPU_2026-10-02.md) | Hilos de juego y render, GPU, draw calls y triángulos con ventana (Development y DebugGame). |
| Análisis del 03-10 | [`Analisis/`](Analisis/) | Estrés con caos, pico de 25 Hz y tirones del lobby. |

## Equipo y herramientas

| Tema | Documento | Contenido |
|---|---|---|
| Git y ramas | [`Flujo_Git.md`](Flujo_Git.md) | Flujo de ramas y reglas de `main`. |
| Comandos de prueba | [`Comandos_Prueba.md`](Comandos_Prueba.md) | Comandos de consola y tests. |
| Red local e informe de bug | [`Pruebas_Red_Local.md`](Pruebas_Red_Local.md) | 8 instancias locales con emulación de red, monkey en clientes remotos e informe con F8. |
| Localización | [`Localizacion.md`](Localizacion.md) | Idiomas, glosario y flujo de traducción. |
| Arte y assets | [`Arte_Assets.md`](Arte_Assets.md) | Pipeline de assets. |
| Mandos | [`Mandos.md`](Mandos.md) | Mandos y glifos. |
| Créditos | [`Creditos.md`](Creditos.md) | Créditos del juego. |

## Diseño

| Tema | Documento | Contenido |
|---|---|---|
| Biblia del juego | [`Biblia_Tortunavy.md`](Biblia_Tortunavy.md) | Referencia histórica de la tortuga y sus mecánicas. Donde contradiga al plan del modo único, manda el plan. |
| Investigación de red y voz | [`Investigacion-8-Jugadores-Voz-2026-09-29.md`](Investigacion-8-Jugadores-Voz-2026-09-29.md) | 8 jugadores con voz en listen server. |
| Modo local | [`Modo_Local.md`](Modo_Local.md) | Pantalla partida. |

## Mapas y terreno

| Tema | Documento | Contenido |
|---|---|---|
| Terreno Camino (C01) | [`Diseno_Terreno_CaminoPrimero.md`](Diseno_Terreno_CaminoPrimero.md) | Método «camino primero», el algoritmo del terreno del modo único. |

## Sistemas y pantallas

| Tema | Documento | Contenido |
|---|---|---|
| Animación | [`Animacion_Tortuga.md`](Animacion_Tortuga.md) | `UTN_TurtleAnimInstance` sin AnimBP. |
| Sonido | [`Sonido_Tortuga.md`](Sonido_Tortuga.md) | Audio sintetizado de la tortuga. |
| Objetos | [`Objetos_DT_Items.md`](Objetos_DT_Items.md) | Inventario y `DT_Items`. Solo valen los objetos del Excel (plan, §4). |
| Lobby | [`Lobby_Castillo.md`](Lobby_Castillo.md), [`Salas.md`](Salas.md), [`Tienda_Probador.md`](Tienda_Probador.md) | Castillo de arena (hasta integrar el lobby de Álvaro), salas y tienda. |
| Menús y carga | [`Menu_Pausa.md`](Menu_Pausa.md), [`Pantalla_Carga.md`](Pantalla_Carga.md) | Menú de pausa y pantalla de carga. |
