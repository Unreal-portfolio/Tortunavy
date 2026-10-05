# Biblia de Tortunavy

> Documento maestro del juego. Recoge **qué es Tortunavy, cómo se juega cada modo, cómo se pasa de una pantalla a otra, cómo son los
> mapas, cómo funciona la red, cómo se controla la tortuga, qué hace cada mecánica, cada objeto, cada trampa y cada enemigo, cómo se
> puntúa, qué se ve en pantalla, qué se puede ajustar y por qué, y cómo suena**. Está pensado para consultarlo como criterio y como
> lista completa de capacidades: todo sale del código (`Source/Tortunabo`) y de los documentos de `Docs/`, no de memoria, y lleva
> referencias a los archivos. Une en un solo texto las tres partes que se escribieron sobre el commit `b4629c3a` (el juego y la
> interfaz; la tortuga y los objetos; el mundo, las trampas y los enemigos) y las pone al día con la **ronda 4** del modo carrera.

**Estado del código que describe**: rama `claude/modo-carrera`, commit `af3f7229` (29-09-2026), que incluye toda la ronda 4
(`8efe23c2` gaviotas y torbellino, `85c622ce` ruedas del quad y catapulta, `cfbaaeba` llegada al agua y paso entre rondas,
`7dfa0b99` música de la carrera y golpes del caparazón, `af3f7229` idioma, ojo de pez y Círculo/B). El árbol de trabajo tiene además
cosas de otros agentes **sin subir** (el tutorial de primera partida y la fase 2 de la localización, con el botón «Ajustes» del menú
principal): no están recogidas aquí salvo donde se dice «en curso». **Donde un documento y el código no coinciden, manda el código**
y se avisa con «Discrepancia» (lista completa en el §41).

**Cómo está organizado**: cuatro partes con numeración de secciones continua.

| Parte | Secciones | Qué contiene |
|---|---|---|
| I. El juego, los modos, el flujo y la interfaz | §1 a §11 | Qué es el juego, los modos, el flujo del arranque a la vuelta, los mapas a alto nivel, la red, los controles, la interfaz, la accesibilidad, el sonido, la localización y el tutorial |
| II. La tortuga, sus mecánicas, los objetos, la puntuación y los cosméticos | §12 a §31 | Todo lo que hace la tortuga del jugador, el inventario, los objetos de siempre y los de carrera, las conchas y los puntos, los cosméticos y la tienda |
| III. El mundo, el decorado, los elementos de juego, las trampas y los enemigos | §32 a §37 | El mapa procedural del cooperativo, la playa de la carrera, sus elementos de juego, trampas y enemigos, y el reparto por ronda |
| IV. Apéndices | §38 a §44 | Comandos de prueba, cifras clave, huecos detectados, discrepancias, lo no confirmado, mapa de archivos y documentos de referencia |

## Índice general

- [Cómo leer este documento](#cómo-leer-este-documento)

**[Parte I — El juego, los modos, el flujo y la interfaz](#parte-i--el-juego-los-modos-el-flujo-y-la-interfaz)**

- [1. Visión del juego](#1-visión-del-juego)
  - [1.1 Qué es](#11-qué-es)
  - [1.2 Tono y estilo](#12-tono-y-estilo)
  - [1.3 Público](#13-público)
  - [1.4 El nombre](#14-el-nombre)
  - [1.5 Pilares de diseño (vigentes)](#15-pilares-de-diseño-vigentes)
  - [1.6 Qué sigue vigente del LDD (v3.0, mayo 2026)](#16-qué-sigue-vigente-del-ldd-v30-mayo-2026)
- [2. Modos de juego](#2-modos-de-juego)
  - [2.1 Vista general](#21-vista-general)
  - [2.2 Cooperativo (mapa procedural)](#22-cooperativo-mapa-procedural)
  - [2.3 Carrera en la playa](#23-carrera-en-la-playa)
  - [2.4 Carrera en el mapa procedural (reserva)](#24-carrera-en-el-mapa-procedural-reserva)
  - [2.5 2 vs 2](#25-2-vs-2)
  - [2.6 Clásico](#26-clásico)
  - [2.7 Tutorial y práctica](#27-tutorial-y-práctica)
  - [2.8 Qué se gana y qué se pierde](#28-qué-se-gana-y-qué-se-pierde)
- [3. Flujo completo: del arranque a la vuelta](#3-flujo-completo-del-arranque-a-la-vuelta)
  - [3.1 Vista de conjunto](#31-vista-de-conjunto)
  - [3.2 Arranque](#32-arranque)
  - [3.3 Menú principal](#33-menú-principal)
  - [3.4 Salas: crear, buscar, código, cerrar, expulsar](#34-salas-crear-buscar-código-cerrar-expulsar)
  - [3.5 Lobby: el castillo de arena](#35-lobby-el-castillo-de-arena)
  - [3.6 HQ: el lobby antiguo](#36-hq-el-lobby-antiguo)
  - [3.7 Viajes y clientes](#37-viajes-y-clientes)
  - [3.8 Pantallas de carga (el huevo)](#38-pantallas-de-carga-el-huevo)
  - [3.9 Fin de partida y vuelta](#39-fin-de-partida-y-vuelta)
  - [3.10 Casos límite](#310-casos-límite)
- [4. Mapas: visión general](#4-mapas-visión-general)
  - [4.1 Los mapas del proyecto](#41-los-mapas-del-proyecto)
  - [4.2 El lobby: castillo y valle](#42-el-lobby-castillo-y-valle)
  - [4.3 Otros mapas](#43-otros-mapas)
  - [4.4 Qué cambia de una ronda a otra](#44-qué-cambia-de-una-ronda-a-otra)
- [5. Red y límites](#5-red-y-límites)
  - [5.1 Modelo](#51-modelo)
  - [5.2 Límites](#52-límites)
  - [5.3 Qué se replica y cómo](#53-qué-se-replica-y-cómo)
  - [5.4 Presupuesto de red (estimado, aún sin medir con `stat net`)](#54-presupuesto-de-red-estimado-aún-sin-medir-con-stat-net)
  - [5.5 Voz de proximidad](#55-voz-de-proximidad)
  - [5.6 Correcciones de movimiento y red de seguridad](#56-correcciones-de-movimiento-y-red-de-seguridad)
  - [5.7 Preparado para ocho](#57-preparado-para-ocho)
- [6. Controles](#6-controles)
  - [6.1 Jugando (tortuga)](#61-jugando-tortuga)
  - [6.2 Espectador (fantasma)](#62-espectador-fantasma)
  - [6.3 Menús](#63-menús)
  - [6.4 Cómo se cambian las teclas](#64-cómo-se-cambian-las-teclas)
  - [6.5 Consola](#65-consola)
- [7. Interfaz](#7-interfaz)
  - [7.1 Principios y arquitectura](#71-principios-y-arquitectura)
  - [7.2 Menú principal y salas](#72-menú-principal-y-salas)
  - [7.3 Lobby](#73-lobby)
  - [7.4 HUD de la tortuga (todos los modos con mapa)](#74-hud-de-la-tortuga-todos-los-modos-con-mapa)
  - [7.5 HUD del cooperativo (mapa procedural)](#75-hud-del-cooperativo-mapa-procedural)
  - [7.6 Pantallas de la carrera en la playa](#76-pantallas-de-la-carrera-en-la-playa)
  - [7.7 Menú de pausa completo](#77-menú-de-pausa-completo)
  - [7.8 Tienda y probador](#78-tienda-y-probador)
  - [7.9 Pantalla de carga](#79-pantalla-de-carga)
  - [7.10 Espectador (fantasma)](#710-espectador-fantasma)
- [8. Accesibilidad y ajustes](#8-accesibilidad-y-ajustes)
  - [8.1 Cómo leer esta sección](#81-cómo-leer-esta-sección)
  - [8.2 Visión](#82-visión)
  - [8.3 Audición y comunicación](#83-audición-y-comunicación)
  - [8.4 Control y motricidad](#84-control-y-motricidad)
  - [8.5 Cognición, mareo y asistencia](#85-cognición-mareo-y-asistencia)
- [9. Sonido y música](#9-sonido-y-música)
  - [9.1 Filosofía](#91-filosofía)
  - [9.2 Categorías de volumen del menú de pausa](#92-categorías-de-volumen-del-menú-de-pausa)
  - [9.3 Música](#93-música)
  - [9.4 Ambiente](#94-ambiente)
  - [9.5 Efectos sintetizados](#95-efectos-sintetizados)
  - [9.6 Sonido de la tortuga](#96-sonido-de-la-tortuga)
  - [9.7 Golpes del caparazón](#97-golpes-del-caparazón)
  - [9.8 Voz](#98-voz)
- [10. Localización](#10-localización)
  - [10.1 Cómo funciona el idioma](#101-cómo-funciona-el-idioma)
  - [10.2 Nombres de sala y avisos de las salas](#102-nombres-de-sala-y-avisos-de-las-salas)
  - [10.3 Fuentes](#103-fuentes)
  - [10.4 Lo que hay que tener en cuenta al traducir](#104-lo-que-hay-que-tener-en-cuenta-al-traducir)
  - [10.5 Localización: estado de las traducciones](#105-localización-estado-de-las-traducciones)
- [11. Tutorial de primera partida](#11-tutorial-de-primera-partida)

**[Parte II — La tortuga, sus mecánicas, los objetos, la puntuación y los cosméticos](#parte-ii--la-tortuga-sus-mecánicas-los-objetos-la-puntuación-y-los-cosméticos)**

- [12. La tortuga de un vistazo](#12-la-tortuga-de-un-vistazo)
- [13. Valores del código y del Blueprint](#13-valores-del-código-y-del-blueprint)
- [14. Movimiento](#14-movimiento)
  - [14.1 Andar, correr y girar](#141-andar-correr-y-girar)
  - [14.2 Saltar](#142-saltar)
  - [14.3 Cámara](#143-cámara)
- [15. Estamina](#15-estamina)
- [16. El caparazón y la bola física](#16-el-caparazón-y-la-bola-física)
  - [16.1 Entrar y salir](#161-entrar-y-salir)
  - [16.2 La caja física](#162-la-caja-física-atn_shellbody)
  - [16.3 Qué hace y qué no hace la tortuga metida](#163-qué-hace-y-qué-no-hace-la-tortuga-metida)
  - [16.4 Caída larga, lanzamientos y aturdimiento](#164-caída-larga-lanzamientos-y-aturdimiento)
- [17. Plancha o panzazo](#17-plancha-o-panzazo)
  - [17.1 Cómo se hace](#171-cómo-se-hace)
  - [17.2 Las fases del arrastre](#172-las-fases-del-arrastre-etnbellyphase)
  - [17.3 Para qué sirve](#173-para-qué-sirve)
  - [17.4 Pose, polvo y sonido](#174-pose-polvo-y-sonido)
  - [17.5 Cifras que dependen de la velocidad de correr](#175-cifras-que-dependen-de-la-velocidad-de-correr)
  - [17.6 Consola de la plancha](#176-consola-de-la-plancha-tndive-afectan-a-la-simulación)
- [18. Nadar y bucear](#18-nadar-y-bucear)
- [19. Cargar, lanzar y liberarse](#19-cargar-lanzar-y-liberarse)
  - [19.1 Coger](#191-coger)
  - [19.2 Liberarse](#192-liberarse)
  - [19.3 Lanzar con la E: el saque de banda](#193-lanzar-con-la-e-el-saque-de-banda)
  - [19.4 Lanzar con la plancha](#194-lanzar-con-la-plancha)
  - [19.5 Soltar sin lanzar](#195-soltar-sin-lanzar)
  - [19.6 Detalles](#196-detalles)
- [20. Derribo, ragdoll y recuperación](#20-derribo-ragdoll-y-recuperación)
  - [20.1 Qué la derriba](#201-qué-la-derriba)
  - [20.2 Cómo se ve y qué pasa](#202-cómo-se-ve-y-qué-pasa)
  - [20.3 Reanimar a una compañera derribada (con un emote)](#203-reanimar-a-una-compañera-derribada-con-un-emote)
  - [20.4 Recuperación y `RecoverFromKnockdown`](#204-recuperación-y-recoverfromknockdown)
- [21. Aturdida (carrera) y agarres de enemigos](#21-aturdida-carrera-y-agarres-de-enemigos)
- [22. Muerte, reaparición en huevos y fantasma espectador](#22-muerte-reaparición-en-huevos-y-fantasma-espectador)
  - [22.1 Cuándo se muere y qué ocurre según el modo](#221-cuándo-se-muere-y-qué-ocurre-según-el-modo)
  - [22.2 Reaparecer en la pila de huevos (mapa procedural)](#222-reaparecer-en-la-pila-de-huevos-mapa-procedural)
  - [22.3 Muerte real, cuerpo y rescate](#223-muerte-real-cuerpo-y-rescate)
  - [22.4 El fantasma espectador](#224-el-fantasma-espectador-docsfantasma_espectadormd)
  - [22.5 Volver a la vida desde un huevo: `TNGhost::ReviveIntoEgg`](#225-volver-a-la-vida-desde-un-huevo-tnghostreviveintoegg)
  - [22.6 Llegar a la meta en la carrera de la playa](#226-llegar-a-la-meta-en-la-carrera-de-la-playa)
  - [22.7 Consola](#227-consola)
- [23. Interacciones y empujones](#23-interacciones-y-empujones)
  - [23.1 Interactuar (E)](#231-interactuar-e)
  - [23.2 Empujones y contactos](#232-empujones-y-contactos)
- [24. Cabeza que mira, cara y caras del HUD](#24-cabeza-que-mira-cara-y-caras-del-hud)
  - [24.1 La cabeza que mira](#241-la-cabeza-que-mira-tickheadlook)
  - [24.2 La cara 3D](#242-la-cara-3d-utn_turtlefacecomponent)
  - [24.3 Las caras del HUD](#243-las-caras-del-hud-uihudtn_hudfacesh-facefor-en-tn_runhudwidgetcpp)
- [25. Emotes y chat rápido](#25-emotes-y-chat-rápido)
  - [25.1 Emotes](#251-emotes)
  - [25.2 Chat rápido](#252-chat-rápido)
- [26. Voz de proximidad](#26-voz-de-proximidad)
- [27. Inventario](#27-inventario)
  - [27.1 Las dos ranuras](#271-las-dos-ranuras)
  - [27.2 Recoger](#272-recoger)
  - [27.3 Cambiar de ranura (G / RB)](#273-cambiar-de-ranura-g--rb)
  - [27.4 Usar (E sin interactuable a mano)](#274-usar-e-sin-interactuable-a-mano)
  - [27.5 Soltar (X / Y)](#275-soltar-x--y)
  - [27.6 El objeto en las aletas](#276-el-objeto-en-las-aletas)
  - [27.7 Lanzar objetos](#277-lanzar-objetos)
  - [27.8 Aturdir enemigos lanzando objetos](#278-aturdir-enemigos-lanzando-objetos)
- [28. Objetos de siempre](#28-objetos-de-siempre)
  - [28.1 El catálogo `DT_Items` ([DT], leído del `.uasset` de HEAD)](#281-el-catálogo-dt_items-dt-leído-del-uasset-de-head)
  - [28.2 Qué hace cada uno](#282-qué-hace-cada-uno)
  - [28.3 Objetos del mundo relacionados (no van al inventario)](#283-objetos-del-mundo-relacionados-no-van-al-inventario)
  - [28.4 Dónde salen los objetos de siempre](#284-dónde-salen-los-objetos-de-siempre)
- [29. Objetos de carrera](#29-objetos-de-carrera)
  - [29.1 Cómo se definen](#291-cómo-se-definen)
  - [29.2 Uso: reglas comunes](#292-uso-reglas-comunes)
  - [29.3 Los objetos](#293-los-objetos)
  - [29.4 Qué hace cada uno, con cifras y con lo que no se puede](#294-qué-hace-cada-uno-con-cifras-y-con-lo-que-no-se-puede)
  - [29.5 Pesos por puesto](#295-pesos-por-puesto)
  - [29.6 Dónde salen (los tres sitios usan el mismo sorteo, `TNRaceItems::RollLoot`)](#296-dónde-salen-los-tres-sitios-usan-el-mismo-sorteo-tnraceitemsrollloot)
  - [29.7 Invulnerabilidad y efectos sobre las demás](#297-invulnerabilidad-y-efectos-sobre-las-demás)
  - [29.8 Red](#298-red)
  - [29.9 Consola de los objetos de carrera](#299-consola-de-los-objetos-de-carrera)
  - [29.10 Límites conocidos](#2910-límites-conocidos)
- [30. Puntuación](#30-puntuación)
  - [30.1 Conchas de puntos](#301-conchas-de-puntos-atn_scorepickup-worldtn_scoreshellsh)
  - [30.2 Puntos por puesto](#302-puntos-por-puesto-racescore-al-llegar-a-la-meta)
  - [30.3 El `RaceScore` en el tiempo](#303-el-racescore-en-el-tiempo)
  - [30.4 Media concha y conchas de la carrera](#304-media-concha-y-conchas-de-la-carrera-raceshellhalves)
  - [30.5 Salto final al agua (mecánica de la tortuga)](#305-salto-final-al-agua-mecánica-de-la-tortuga)
  - [30.6 Llegada al agua: «Has quedado X.º» y paso entre rondas](#306-llegada-al-agua-has-quedado-xº-y-paso-entre-rondas)
  - [30.7 Qué se guarda (perfil del jugador)](#307-qué-se-guarda-perfil-del-jugador)
- [31. Cosméticos y tienda](#31-cosméticos-y-tienda)
  - [31.1 Categorías](#311-categorías-etncosmeticcategory)
  - [31.2 Catálogo completo](#312-catálogo-completo)
  - [31.3 Precios y desbloqueo](#313-precios-y-desbloqueo)
  - [31.4 La tienda](#314-la-tienda-atn_shopkeeper-utn_shopwidget)
  - [31.5 El probador](#315-el-probador-atn_changingbooth-utn_boothwidget)
  - [31.6 Guardado y elección en partida](#316-guardado-y-elección-en-partida)
  - [31.7 Cómo se ven en red](#317-cómo-se-ven-en-red)

**[Parte III — El mundo, el decorado, los elementos de juego, las trampas y los enemigos](#parte-iii--el-mundo-el-decorado-los-elementos-de-juego-las-trampas-y-los-enemigos)**

- [32. El mapa procedural del cooperativo](#32-el-mapa-procedural-del-cooperativo-lvl_procmap)
  - [32.1 Cómo se genera](#321-cómo-se-genera)
  - [32.2 Biomas (ocho, por regiones de varios módulos)](#322-biomas-ocho-por-regiones-de-varios-módulos)
  - [32.3 Vegetación procedural (solo visual, sin colisión)](#323-vegetación-procedural-solo-visual-sin-colisión)
  - [32.4 El camino: anchura, saltos, ramas y obstáculos](#324-el-camino-anchura-saltos-ramas-y-obstáculos)
  - [32.5 Estructuras colosales](#325-estructuras-colosales)
  - [32.6 Cuevas](#326-cuevas)
  - [32.7 Agua](#327-agua)
  - [32.8 Formaciones temáticas](#328-formaciones-temáticas-eformation-worldprocmaptn_procmapformationsh)
  - [32.9 Fauna ambiental (solo visual)](#329-fauna-ambiental-solo-visual)
  - [32.10 Clima: la tormenta del camino y el ambiente](#3210-clima-la-tormenta-del-camino-y-el-ambiente)
  - [32.11 La salida, las pilas de huevos y la meta](#3211-la-salida-las-pilas-de-huevos-y-la-meta)
  - [32.12 Rebuscables del mapa procedural](#3212-rebuscables-del-mapa-procedural-atn_procsearchspot)
  - [32.13 Conchas de puntos del mapa](#3213-conchas-de-puntos-del-mapa-tnprocmapplanshells)
  - [32.14 Puzles del 2vs2 (solo en ese modo, `World/ProcMap/TN_ProcPuzzleActors.*`)](#3214-puzles-del-2vs2-solo-en-ese-modo-worldprocmaptn_procpuzzleactors)
  - [32.15 Qué es solo visual y qué tiene colisión (cooperativo)](#3215-qué-es-solo-visual-y-qué-tiene-colisión-cooperativo)
  - [32.16 Límites conocidos](#3216-límites-conocidos)
- [33. La playa de la carrera](#33-la-playa-de-la-carrera-lvl_beachrace)
  - [33.1 Medidas generales](#331-medidas-generales)
  - [33.2 El terreno fijo, pieza a pieza](#332-el-terreno-fijo-pieza-a-pieza)
  - [33.3 El decorado gigante (local e instanciado)](#333-el-decorado-gigante-local-e-instanciado)
  - [33.4 Qué es solo visual, qué tiene colisión y qué se puede rebuscar (playa)](#334-qué-es-solo-visual-qué-tiene-colisión-y-qué-se-puede-rebuscar-playa)
- [34. Elementos de juego de la playa](#34-elementos-de-juego-de-la-playa)
  - [34.1 Reglas comunes de los elementos](#341-reglas-comunes-de-los-elementos)
  - [34.2 Trampolín](#342-trampolín-atn_beachtrampoline-worldbeachtn_beachtrampoline)
  - [34.3 Catapulta](#343-catapulta-atn_beachcatapult-worldbeachtn_beachcatapult)
  - [34.4 Pala](#344-pala-atn_beachspaderamp-worldbeachtn_beachspaderamp)
  - [34.5 Plataformas móviles](#345-plataformas-móviles-atn_beachmovingplatform-worldbeachtn_beachmovingplatform)
  - [34.6 Plataforma sobre un hoyo](#346-plataforma-sobre-un-hoyo-atn_beachwobblyplatform-worldbeachtn_beachwobblyplatform)
  - [34.7 Puerta de conchas](#347-puerta-de-conchas-atn_beachshellgate-worldbeachtn_beachshellgate)
  - [34.8 Algas que enredan](#348-algas-que-enredan-atn_beachseaweed-worldbeachtn_beachseaweed)
  - [34.9 Castillo de arena con salas](#349-castillo-de-arena-con-salas-atn_beachsanddungeon-worldbeachtn_beachsanddungeon)
  - [34.10 Fortalezas de arena](#3410-fortalezas-de-arena-atn_beachfortress-worldbeachtn_beachfortress-plantas-en-tn_beachfortresskith)
  - [34.11 Cofres](#3411-cofres-atn_beachchest-y-atn_beachchestspot-worldbeachtn_beachchest)
  - [34.12 Rebuscables y montículos que vibran](#3412-rebuscables-y-montículos-que-vibran-tn_beachloot-worldbeachtn_beachloot-tn_beachsearchmoundscpp)
  - [34.13 Conchas de puntos de la playa](#3413-conchas-de-puntos-de-la-playa-atn_scorepickup-worldbeachtn_beachlootshellscpp)
  - [34.14 Cajas de objetos](#3414-cajas-de-objetos-atn_raceitembox-worldbeachtn_raceitembox)
  - [34.15 Carteles de madera de los lanzadores](#3415-carteles-de-madera-de-los-lanzadores-tn_beachsignkith)
- [35. Trampas de la playa](#35-trampas-de-la-playa)
  - [35.1 Resumen](#351-resumen)
  - [35.2 Alambre de espino](#352-alambre-de-espino-atn_beachbarbedwire)
  - [35.3 Cubo roto](#353-cubo-roto-atn_beachbrokenbucket)
  - [35.4 Concha que atrapa](#354-concha-que-atrapa-atn_beachclamtrap--la-almeja-trampa)
  - [35.5 Mina de la playa](#355-mina-de-la-playa-atn_beachmine)
  - [35.6 Lo que estorba sin ser una trampa](#356-lo-que-estorba-sin-ser-una-trampa)
- [36. Enemigos y amenazas](#36-enemigos-y-amenazas)
  - [36.1 Resumen de la carrera (nueve enemigos del reparto, la tormenta y el gusano)](#361-resumen-de-la-carrera-nueve-enemigos-del-reparto-la-tormenta-y-el-gusano)
  - [36.2 Reglas comunes](#362-reglas-comunes-atn_beachenemy-worldbeachtn_beachenemy)
  - [36.3 Cangrejo gigante](#363-cangrejo-gigante-atn_beachgiantcrab-worldbeachtn_beachgiantcrab)
  - [36.4 Erizo de mar](#364-erizo-de-mar-atn_beachseaurchin-worldbeachtn_beachseaurchin)
  - [36.5 Lagarto](#365-lagarto-atn_beachlizard-worldbeachtn_beachlizard)
  - [36.6 Paso de quads](#366-paso-de-quads-atn_beachquadlane-worldbeachtn_beachquadlane)
  - [36.7 Gaviotas y pelícanos](#367-gaviotas-y-pelícanos-atn_beachgullzone-worldbeachtn_beachgullzone)
  - [36.8 Cangrejo ermitaño bola](#368-cangrejo-ermitaño-bola-atn_beachhermitcrab-worldbeachtn_beachhermitcrab)
  - [36.9 Pulpo de poza](#369-pulpo-de-poza-atn_beachpooloctopus-worldbeachtn_beachpooloctopus)
  - [36.10 Enjambre de pulgas de arena](#3610-enjambre-de-pulgas-de-arena-atn_beachsandfleas-worldbeachtn_beachsandfleas)
  - [36.11 Tanque de juguete teledirigido](#3611-tanque-de-juguete-teledirigido-atn_beachtoytank-worldbeachtn_beachtoytank)
  - [36.12 Tormenta de bañistas](#3612-tormenta-de-bañistas-atn_beachstorm-worldbeachtn_beachstorm)
  - [36.13 Gusano de arena gigante](#3613-gusano-de-arena-gigante-atn_beachsandworm-worldbeachtn_beachsandworm)
  - [36.14 Amenazas que lanzan las tortugas (objetos de carrera)](#3614-amenazas-que-lanzan-las-tortugas-objetos-de-carrera)
  - [36.15 Cooperativo: enemigos y peligros del mapa procedural](#3615-cooperativo-enemigos-y-peligros-del-mapa-procedural)
  - [36.16 Modo clásico (chunks, `LVL_Run`)](#3616-modo-clásico-chunks-lvl_run)
- [37. Reparto por ronda](#37-reparto-por-ronda)
  - [37.1 Carrera: `TNBeachLayout::GenerateRound(Seed, Difficulty, Out)`](#371-carrera-tnbeachlayoutgenerateroundseed-difficulty-out-worldbeachtn_beachlayouth)
  - [37.2 Cooperativo: peligros, enemigos y botín](#372-cooperativo-peligros-enemigos-y-botín-tnprocmapplanhazards-tnprocmapplanshells)
  - [37.3 Dificultad: qué cambia y qué no](#373-dificultad-qué-cambia-y-qué-no)

**[Parte IV — Apéndices](#parte-iv--apéndices)**

- [38. Comandos de prueba](#38-comandos-de-prueba)
- [39. Cifras clave](#39-cifras-clave)
- [40. Huecos detectados](#40-huecos-detectados)
  - [40.1 Accesibilidad](#401-accesibilidad)
  - [40.2 Juego](#402-juego)
- [41. Discrepancias entre documentos y código](#41-discrepancias-entre-documentos-y-código)
  - [41.1 Velocidades y cifras que dependen de ellas](#411-velocidades-y-cifras-que-dependen-de-ellas)
  - [41.2 Mecánicas, controles y comentarios](#412-mecánicas-controles-y-comentarios)
  - [41.3 Documentos y configuración desactualizados](#413-documentos-y-configuración-desactualizados)
- [42. Lo no confirmado](#42-lo-no-confirmado)
  - [42.1 Del diseño y del proyecto](#421-del-diseño-y-del-proyecto)
  - [42.2 De los assets y de los valores](#422-de-los-assets-y-de-los-valores)
  - [42.3 De las mecánicas](#423-de-las-mecánicas)
  - [42.4 De la ronda 4 y del estado del árbol](#424-de-la-ronda-4-y-del-estado-del-árbol)
- [43. Mapa de archivos](#43-mapa-de-archivos)
  - [43.1 Flujo, salas, lobby, interfaz y ajustes](#431-flujo-salas-lobby-interfaz-y-ajustes)
  - [43.2 La tortuga, los objetos y los cosméticos](#432-la-tortuga-los-objetos-y-los-cosméticos)
  - [43.3 El mundo](#433-el-mundo)
  - [43.4 Archivos que añadió la ronda 4](#434-archivos-que-añadió-la-ronda-4)
- [44. Documentos de referencia](#44-documentos-de-referencia)

## Cómo leer este documento

**Rutas.** Las rutas van entre comillas de código y son relativas a `Source/Tortunabo/` (`Public/…`, `Private/…`), salvo las que
empiezan por `Docs/`, `Config/`, `Scripts/`, `Tools/` o `Content/`, que son relativas a la raíz del proyecto. `Player/TN_ShellComponent`
significa `Public/Player/TN_ShellComponent.h` y `Private/Player/TN_ShellComponent.cpp`. `X.*` es cabecera y fuente.

**Unidades.** El motor trabaja en **centímetros** (cm y cm/s; 100 cm = 1 m). En el texto van metros cuando se dice. `TN.` y `tn.` son
comandos y variables de consola (lista completa en el §38 y en [`Docs/Comandos_Prueba.md`](Comandos_Prueba.md)).

**Nombres.** «Tortunavy» es el título y todo lo que ve el jugador; «Tortunabo» es el nombre técnico (módulo, `.uproject`, rutas,
categorías de log y de tests) y un guiño en la meta (§1.4).

**Origen de cada cifra.** Cuando un dato viene de un sitio distinto del código C++ lleva su etiqueta:

| Etiqueta | Significa |
|---|---|
| **[C]** | Valor por defecto escrito en el código C++ (`UPROPERTY`, `constexpr`, un `switch`). Es el que manda si el Blueprint no lo cambia |
| **[BP]** | Valor que guarda `Content/Blueprints/Characters/BP_TortugaCharacter.uasset` (o su tabla) por encima del código. Se leyó **directamente del `.uasset` de HEAD** con un lector propio; solo las velocidades y la plancha están confirmadas por los registros de las pruebas del 28-09 (§13) |
| **[DT]** | Fila de una DataTable leída del `.uasset` (`DT_Items`, `DT_Skins`, `DT_Helmets`) |
| **[D]** | Lo dice un documento de `Docs/` (contrastado con el código cuando ha sido posible; si no coinciden, manda el código y se avisa) |
| **[calc]** | Calculado a partir de otras cifras del propio código |

**Marcas.** **Discrepancia**: dos fuentes no coinciden (todas, en el §41). **No confirmado**: no se ha podido comprobar en el código ni
en un documento (todo, en el §42). **En curso**: existe en el árbol de trabajo pero no está subido a git.

**Referencias.** `§N` y `§N.M` remiten a las secciones de este mismo documento. Los enlaces a otros documentos son relativos a `Docs/`.

**Velocidades de la tortuga.** Todo lo que depende de la velocidad usa las **reales**: andar **200 cm/s** y correr **400 cm/s** (el
Blueprint pisa a los 450 y 800 del código; §13). Donde una cifra de diseño de otro documento asumía 450 y 800, aquí está recalculada.

# Parte I — El juego, los modos, el flujo y la interfaz

## 1. Visión del juego

### 1.1 Qué es

**Tortunavy** es un juego multijugador de **1 a 8 jugadores** en **tercera persona**, hecho con **Unreal Engine 5.6**
(C++ en el módulo `Tortunabo`, más Blueprints para el cableado). Se juega con **tortugas marinas antropomórficas** recién
salidas del huevo que tienen que **llegar al mar**. Lo que las separa del agua cambia según el modo: un mapa largo y
procedural con una tormenta detrás (cooperativo) o una playa gigante y hostil llena de trampas, enemigos y objetos
(carrera). Ver README, «Tortunavy», y [`Docs/LDD_Tortunabo.md` (eliminado)](LDD_Tortunabo.md (eliminado)).

Lo que hace a Tortunavy reconocible:

- **Salir de un huevo**. Las partidas de cooperativo y de carrera empiezan con las tortugas metidas en huevos (en el
  cooperativo, también puede ser la sala de la puerta doble del castillo) que se rompen a la vez; cada una sale de un saltito hacia
  el mar. También es el primer gesto del lobby: «ponerse listo» es meterse en un huevo.
- **Escala jugable**. En la carrera la tortuga es una cría de unos 5 cm y el mundo está a `TNBeach::Scale` = 28 veces su
  tamaño real (una palmera de 10 m mide 280 m; una lata, un edificio). La playa está llena de basura humana, juguetes y
  restos de una «tropa» de soldaditos de plástico (`Public/World/Beach/TN_BeachTypes.h`).
- **Cuerpo-caparazón con física**. La tortuga se mete en el caparazón y rueda como una bola con física de verdad; otras
  tortugas pueden cogerla y lanzarla. En la carrera, lo que en el cooperativo mata aquí aturde (bola temblando unos
  segundos): **nadie muere** ([`Docs/Modo_Carrera.md`](Modo_Carrera.md), «No se muere: aturdimiento»).
- **Voz de proximidad** entre jugadores (se oye a quien tienes cerca) y ruedas de bailes y de frases rápidas, que son la única
  comunicación escrita: no hay chat de texto libre (§5.5, §7.4).
- **Todo en código**. Casi todo el contenido se construye en ejecución: mallas low-poly con color de vértice, interfaz en
  Slate/UMG desde C++, y **audio sintetizado** sin archivos (§9). El arte que no se genera son los cosméticos base, unos
  pocos efectos de sonido de asset y la música del menú.

### 1.2 Tono y estilo

- **Frenético y ligero, con humor**. «Las muertes son cómicas, no punitivas» ([`Docs/LDD_Tortunabo.md` (eliminado)](LDD_Tortunabo.md (eliminado)), «Tone»). La regla se
  ha llevado más lejos en la carrera: no hay muerte, hay bola temblorosa con pajaritos.
- **Humor en la superficie**: nombres de sala graciosos (242, localizables y adaptados al inglés, `Private/Multiplayer/TN_RoomNames.cpp`; §10.2), el
  General Galápago y su cuartel, Don Tortugo y «La Concha Dorada», frases de ánimo en «¡ADELANTE!» (una de ocho al azar), premios
  del podio hechos de basura de playa (vaso, caja de zumo, chancla), un gusano de arena gigantesco que se come a quien no ha
  llegado.
- **Estética**: luz intensa, arena dorada, azul marino y crema en la interfaz («estilo Tortunavy»: salvavidas, conchas,
  cintas de arena, cartel azul marino; `Private/UI/HUD/TN_HUDArt.h`, `TN_HUDStyle.h`), personajes cartoon, mundo low-poly. Motivo
  militar de playa (sacos terreros, erizos antitanque, tanques de juguete, redes de camuflaje, soldaditos verdes) como
  tropa de Tortunavy.
- **Legibilidad sin texto**: «cada elemento del entorno comunica sus reglas visualmente antes de que el jugador interactúe»
  (LDD). Ejemplos vigentes: la sombra que crece de la gaviota y su «!», la nube de humo y el temblor de los quads, los
  montículos de arena que vibran donde se puede rebuscar, el aro de progreso al mantener E.
- **Sonido cartoon pero creíble**: patas blandas, no botas ([`Docs/Sonido_Tortuga.md`](Sonido_Tortuga.md)).

### 1.3 Público

El repositorio **no contiene un documento de público objetivo**; lo siguiente se deduce del diseño y de las decisiones del
equipo:

- **Grupos de amigos** (2–8) que juegan por Steam con voz de proximidad. Salas privadas con código y por invitación, cierre de
  sala, expulsión y silencio individual de la voz (§3.4, §8) apuntan a partidas entre conocidos, no a emparejamiento anónimo.
- **Jugadores casuales**. Rondas de playa de unos 4 a 5 minutos (el juego anuncia 3,3, porque `AverageRaceSpeed` supone una media de 4 m/s y la real es de ~2,8 m/s; §33.1), sin muerte, con
  objetos tipo karts que dan opciones a quien va detrás (`TNRaceItems`, pesos por posición), y una dificultad de tres niveles
  que elige el anfitrión.
- **Quien quiere cooperar**: el cooperativo exige lectura del terreno y coordinación; la LDD lo plantea como «caos
  cooperativo» ([`Docs/LDD_Tortunabo.md` (eliminado)](LDD_Tortunabo.md (eliminado)), «Themes»).
- **Contexto de proyecto**: entrega T-Day de U-tad; equipo formado por Rodrigo Fernández y José Antonio Mota (Mokius); red de
  pruebas con el AppId 480 de Steam (README).

### 1.4 El nombre

| Nombre | Dónde va |
|---|---|
| **Tortunavy** | El título del juego y todo lo que ve un jugador: ventana (`ProjectDisplayedTitle` en `Config/DefaultGame.ini`), interfaz, rótulos («TORTUNAVY» en la puerta doble y en la cara del arco de meta que se ve al llegar), documentación. El estilo visual de la interfaz también se llama «Tortunavy». |
| **Tortunabo** | Lo técnico: módulo C++ y `.uproject`, rutas de contenido, categorías de log (`LogTortunabo`) y de tests (`Tortunabo.*`), espacios de localización, la clave de búsqueda de sesiones `TortunaboLobby` (cambiarla rompería la compatibilidad entre versiones) y un **guiño**: el flanco del arco de meta que mira al mar dice «TORTUNABO». No hay que «corregirlo». |

### 1.5 Pilares de diseño (vigentes)

Del LDD ([`Docs/LDD_Tortunabo.md` (eliminado)](LDD_Tortunabo.md (eliminado)), «Design Principles»), con lo que sigue valiendo:

1. **Cooperación emergente**: el juego no manda cooperar; monta situaciones en las que cooperar es lo obvio (coger y lanzar a
   un compañero, puzles de dos placas, mecanismos remotos).
2. **Legibilidad sin texto**: si un peligro necesita un párrafo para entenderse, el diseño visual ha fallado. Si un elemento
   rompe la legibilidad para ser más cooperativo, gana la legibilidad.
3. **Presión justa**: la dificultad viene de información y coordinación, no de daño impredecible; los peligros tienen radio y
   señal (radios de detección, sombras, temporizadores que se pueden interiorizar).
4. **Variedad sin imprevisibilidad**: generación procedural con semilla, dentro de reglas conocidas.

Principios que se deducen del desarrollo posterior (el 5 sale de una petición explícita del usuario; el 6 y el 7 son deducción de este documento, no
están escritos como pilares en ningún documento):

5. **Nunca bajo el mapa**: en red no puede pasar que una tortuga quede atrapada o hundida; hay una red de seguridad
   (§5.6, [`Docs/Modo_Carrera.md`](Modo_Carrera.md), «Seguridad: nunca bajo el mapa»).
6. **Sin tutorial de texto obligatorio**: la explicación es opcional y vive en el General Galápago (§2.7).
7. **Lo determinista se calcula en cada máquina**: la semilla viaja, el decorado no (§5.3).

### 1.6 Qué sigue vigente del LDD (v3.0, mayo 2026)

El LDD describe un diseño **anterior**: cinco rondas de seis módulos hechos con chunks (`ATN_ChunkManager`), bañistas que
avanzan por detrás, gaviotas de dos modos, puzles de cajas y placas, campo de tiro y campo de entrenamiento en el lobby. Hoy:

| El LDD dice | Hoy |
|---|---|
| Corredor lineal cooperativo de 5 rondas × 6 módulos, perder reinicia en la ronda 1 | Ese es el modo **Clásico** (`LVL_Run`, chunks) y ya no es el principal. El cooperativo es **una ronda** sobre un mapa procedural por módulos de 400 m (§2.2, §32). |
| Los bañistas alcanzan y eliminan | En el cooperativo, la **tormenta** que avanza por el camino (5 s dentro y se muere). En la carrera, la **tormenta de bañistas** aturde, no mata. |
| Lobby con torres de vigilancia, campo de tiro y campo de entrenamiento | Lobby de **castillo de arena redondo** con valle de biomas, general, tienda, probadores, patio de pruebas con mini parkour y cofre (§3.5). |
| Puntos por partida para comprar cosméticos | Sigue vigente en forma de **conchas de puntos** (`RaceScore`), pero hoy todo cuesta 0 (§2.8). |
| Objetivo: llegar al agua | Sigue siendo el objetivo de todos los modos. |
| Gaviota agarra si estás quieto más de 2 s | En la carrera: gaviotas y pelícanos con círculo propio; cagada, picado con sombra y agarre, con el nerf de la ronda 4 (§36.7). |

## 2. Modos de juego

### 2.1 Vista general

`ETNProcGameMode` (`Public/World/ProcMap/TN_ProcMapEnums.h`): `Coop`, `Race`, `TwoVsTwo`, `Classic`. El modo vive en
`UMP_GameInstance::SelectedProcMode` del anfitrión (por defecto `Coop`) y sobrevive a los viajes
(`Public/Multiplayer/MP_GameInstance.h`).

| Modo | Estado | Jugadores | Mapa y GameMode | Se elige | Objetivo | Muerte |
|---|---|---|---|---|---|---|
| **Cooperativo** | Principal | 1–8 | `LVL_ProcMap` · `ATN_ProcMapGameMode` (BP `BP_ProcMapGameMode`) | «Crear partida» o General Galápago | Que el equipo llegue al mar antes de que la tormenta le alcance | **Sí**: la tormenta, las zonas de muerte y las caídas grandes; se reaparece o se rescata |
| **Carrera** (playa) | Principal | 1–8 | `LVL_BeachRace` · `ATN_BeachRaceGameMode` | «Crear partida» o General Galápago | Ser la primera en tocar el agua tras saltar el acantilado; la partida es de **tres conchas** | **Nunca**: se aturde en bola |
| Carrera en el mapa procedural | Reserva | 1–8 | `LVL_ProcMap` con `ProcMode=Race` | `?ProcMode=Race`, o si falta `LVL_BeachRace` | Primero en la meta gana la ronda; primero a 3 rondas gana la partida | Sí, con reaparición en las pilas de huevos propias |
| **2 vs 2** | Implementado, **sin entrada normal** | Exactamente 4 | `LVL_ProcMap` con `ProcMode=2v2` | Selector del lobby antiguo (`LVL_HQ`) o URL | Gana la ronda la pareja cuyos **dos** miembros llegan antes | Sí, con reaparición en huevos propios |
| **Clásico** | Heredado | Hasta 8 | `LVL_Run` · `ATN_RunGameMode` + `ATN_ChunkManager` | Selector del lobby antiguo | Recorrido por chunks hasta la meta | Sí (DBNO desactivado en zonas de muerte, rescate) |
| **Tutorial** | **No existe como modo** | — | Ver §2.7 | — | — | — |

**Qué ofrece cada pantalla de elección** (`Public/Lobby/TN_LobbyMission.h`, `TNLobbyMission::MenuModes`):

- El menú «Crear partida» y la pestaña «Misión» del general ofrecen solo **Cooperativo** y **Carrera**.
- Los selectores físicos del lobby antiguo recorren **Clásico → Cooperativo → Carrera → 2 vs 2** (este solo si hay exactamente
  cuatro conectados; `NextSelectorMode`).
- Si al salir del lobby el modo es 2 vs 2 y ya no hay cuatro, se juega Carrera (`ATN_HQGameMode::BeginMatchTravel`).

**Qué es común a todos**: salir de huevo (o de la sala de la puerta doble en el mapa procedural), el HUD de tortuga, la voz de
proximidad, las ruedas, el menú de pausa, el fantasma espectador al terminar, y la vuelta al lobby.

### 2.2 Cooperativo (mapa procedural)

**Idea**: el equipo recorre un camino largo y natural, generado por semilla, desde un claro de salida hasta una playa con un arco
de neumático gigante; una **tormenta** avanza por el camino detrás de todos y nunca es más rápida que una tortuga andando.
Quien se queda atrás, la respira (`ATN_PathStorm`, `Public/World/ProcMap/TN_PathStorm.h`).

**Reglas** (`Public/Game/TN_ProcMapGameMode.h`, `Public/Game/TN_RunGameMode.h`):

| Regla | Valor | Fuente |
|---|---|---|
| Rondas por partida | 1 (`CoopRounds`): una ronda es la partida | `TN_ProcMapGameMode.h` |
| Tormenta | Arranca tras una gracia de 90 / 60 / 45 s (Fácil / Normal / Difícil) y avanza por el camino a 160 / 180 / 200 cm/s | `Private/World/ProcMap/TN_ProcMapTypes.cpp` |
| Muerte por tormenta | 5 s dentro del frente (`SecondsInsideToDie`); `InsideMargin` = 400 cm es el margen detrás del frente antes de contar como dentro | `TN_PathStorm.h` |
| Otras muertes | Zonas de muerte (cuenta de 3 s), caída libre de más de 35 m, caer de un puente o muralla colosal; la fauna del agua «muerde» (§36.15) | [`Docs/Mapa_Procedural.md`](Mapa_Procedural.md) §5, `TN_RunGameMode` |
| Caída de más de 5 m | La tortuga se mete sola en el caparazón (no muere) | [`Docs/Mapa_Procedural.md`](Mapa_Procedural.md) §5 |
| Reaparición | 2,5 s después (`RespawnDelaySeconds`) en la **pila de huevos más lejana que haya alcanzado el equipo** y que quede al menos 30 m de camino por delante de la tormenta (`StormRespawnMargin` 3000 cm) | `TN_ProcMapGameMode.h` |
| Sin pila válida | Muerte normal: queda un objeto de rescate en el punto de caída; un compañero lo usa (Interactuar) y la revive; si no, pasa a fantasma espectador | `TN_RunGameMode.cpp` (`MarkPlayerDead`), `TN_RescuePickup` |
| Tótem | Un tótem en el inventario cancela la muerte y se gasta | `TN_RunGameMode.cpp` (`TryTotemAutoRevive`) |
| Fin de la ronda | Cuando **todos** están resueltos (llegados o eliminados) o no queda nadie vivo | `TN_RunGameMode::UpdateRoundProgressAndMaybeFinish` |
| Quien llega antes | Pasa a **fantasma espectador** y sigue mirando a los demás | [`Docs/Fantasma_Espectador.md`](Fantasma_Espectador.md) |
| Resultados | 8 s (`ResultsDurationSeconds`), tabla de hasta 8 filas, luego vuelta al lobby | `TN_RunGameMode.h`, `TN_CoopFlowHUDWidget.h` |
| Victoria del equipo (para la música) | Al menos una tortuga llega a la meta (`bTeamReachedGoal`) | `Private/Audio/TN_MatchMusicDirector.h` |
| Espera a los clientes | La ronda no arranca hasta que cada cliente avisa de que tiene el mapa construido; tope de 30 s (`MapReadyTimeoutSeconds`) | `TN_ProcMapGameMode.h` |
| Aparición | En el mismo sitio en que se pusieron listos en el lobby: la sala de la puerta doble o la pila de huevos, con hasta 8 sitios | §3.5, §32 |

**Dificultad** (`TN_MakeDefaultProcProfile`, `Private/World/ProcMap/TN_ProcMapTypes.cpp`; editable en el asset
`DA_ProcMapSettings`, que guarda sus perfiles):

| Parámetro | Fácil | Normal | Difícil |
|---|---|---|---|
| Rejilla de módulos de 400 m | 3 × 3 | 6 × 6 | 8 × 8 |
| Longitud aproximada del camino | ~3 km | ~12 km | ~21 km |
| Cobertura de la rejilla | 0,78 | 0,78 | 0,78 |
| Cruces colosales | 1 | 2 | 4 |
| Ramas | 6 | 12 | 16 |
| Densidad de peligros | 1,6 | 2,4 | 3,2 |
| Huecos de salto por km | 9 | 13 | 17 |
| Pila de huevos cada N cruces de módulo | 1 | 2 | 3 |
| Tormenta (cm/s) | 160 | 180 | 200 |
| Gracia de la tormenta (s) | 90 | 60 | 45 |
| Duración estimada | Proporcional a la longitud | ~35-40 min a 5,5 m/s ([`Docs/Mapa_Procedural.md`](Mapa_Procedural.md) §7) | Proporcional a la longitud |

La duración de cada mapa la escribe el generador en el registro: `[ProcMap] Mapa listo · semilla … · longitud … · minutos
estimados` (a 5,5 m/s). La propia doc reconoce que Normal se pasa de los 10–20 minutos objetivo y que se dejó así a propósito
para probar ([`Docs/Mapa_Procedural.md`](Mapa_Procedural.md) §7).

**Puntos en el cooperativo**: las conchas de puntos (1, 25, 50, 100) suman `RaceScore`; al llegar se suma además el puesto
(400 / 300 / 200 / 100 / 80 / 65 / 55 / 50) y un bonus por tiempo (§2.8).

**Pendiente**: `TNGhost::ReviveIntoEgg` (revivir a un fantasma metiéndolo en un huevo con animación) está hecho pero **solo lo usa
la orden de prueba `TN.Ghost.Revive`**; la reaparición real del cooperativo es la de las pilas de huevos, que teletransporta a la
tortuga. Los «nidos» del futuro no existen ([`Docs/Fantasma_Espectador.md`](Fantasma_Espectador.md), «Pendiente y notas»).

### 2.3 Carrera en la playa

**Idea**: todas contra todas en una playa fija de 800 m; gana la ronda quien primero salta el acantilado de la orilla y toca el agua.
Se juega a **tres conchas**. Rama `claude/modo-carrera`, GameMode `ATN_BeachRaceGameMode` (hereda de `ATN_RunGameMode`), GameState
`ATN_BeachRaceGameState` (`Public/Game/TN_BeachRaceGameMode.h`, `TN_BeachRaceGameState.h`; detalle en [`Docs/Modo_Carrera.md`](Modo_Carrera.md)).

**Bucle de una ronda** (fases `ETNBeachRacePhase`; tiempos por defecto de `TN_BeachRaceGameMode.h`):

| Fase | Qué pasa | Tiempo |
|---|---|---|
| `Waiting` | El servidor reparte la ronda (`GenerateRound(semilla)`); cada tortuga aparece dentro de su huevo en la salida, quieta. Espera a que el generador y **cada cliente** hayan montado la ronda | ≥ 2 s (`MinPreRoundSeconds`) y como mucho 20 s al generador + 12 s a cada cliente (`ClientRoundReadyTimeoutSeconds`); luego cuenta 3-2-1 (`PreRaceCountdownSeconds` 3 s). En la primera ronda tras el viaje la «cuenta» es la pantalla de carga del huevo; desde la ronda 2 (y en el sprint y en «Volver a jugar») cada pantalla lo pasa tapada por el huevo negro con «RONDA N», «Colocando la playa…» y el 3-2-1 (§7.6) |
| `Racing` | Se rompen los huevos; 1 s en el huevo roto (se pone de pie, se sacude la cáscara) y sale lanzada hacia el mar a 10 m/s. Arranca la **tormenta de bañistas** 30 m por detrás. La primera que toca el agua gana la ronda (`RoundWinner`, **una concha entera**) y arranca una **cuenta atrás de 10 s** para las demás: quien llega dentro se lleva **media concha**. El puesto de cada llegada se decide en el contacto; la tortuga se queda 0,8 s a la vista en el agua con la postura de la zambullida congelada y, en su pantalla, se cierra el huevo negro con «Has quedado X.º» y su premio (§30.6) | Cuenta de 10 s (`FinishCountdownSeconds`); límite de ronda **9 min** (`RoundTimeLimitSeconds` 540 s) |
| Fin de la ronda | Al acabar la cuenta: «¡TIEMPO!» (silbato); a cada tortuga que no ha llegado **le sale un gusano de arena gigante de la arena y se la come** (sin matarla); si han llegado todas, «¡TODAS AL AGUA!» sin gusanos | 1,6 s (`TimeUpHoldSeconds`); con gusanos, 3,2 s + 0,6 s (`EatSeconds`, `SandWormMarginSeconds`); en cualquier caso, hasta que acaba la pantalla del puesto de la última en llegar (3,3 s desde su llegada, `ArrivalScreenHoldSeconds`) |
| `RoundResults` | **Recuento**: cada jugador con tres huecos de concha; vuela la concha entera de la ganadora y saltan las medias | 7 s (`RoundResultsSeconds`) |
| Siguiente ronda | Mismo terreno, elementos recolocados con otra semilla; huevos cerrados otra vez | — |
| `SprintIntro` + sprint | Si al cerrar una ronda hay **empate en lo más alto con tres conchas o más**: título «¡SPRINT FINAL!» y ronda corta solo para las empatadas | 5 s de título (`SprintIntroSeconds`); límite del sprint **4 min 30 s** (`SprintTimeLimitSeconds` 270 s) |
| `Champion` | Pantalla del campeón con podio 3D y menú del anfitrión | Sin límite: espera a que elija el anfitrión |

**Reglas exactas**

- **Conchas** (`ATN_CoopPlayerState::RaceShellHalves`, 2 medias = 1 concha). Ganar la ronda = +2; cada llegada dentro de la cuenta = +1.
  `WinsToWinMatch` = 3 conchas = 6 medias. `RoundWins` cuenta rondas ganadas enteras y desempata el podio.
- **Si nadie llega al agua**: al agotarse la ronda (9 min) «¡TIEMPO!» y la concha es para **la más cerca del mar**; con avisos en pantalla
  a los 60 s y 30 s (§7.6). En el sprint, gana la finalista más cerca del mar.
- **Sprint**: solo corren las finalistas, desde una **línea a mitad de la playa** (`SprintLineX`, X ≈ 400 m) con un nido de huevos
  propio; el resto pasa a fantasma espectador. La primera en el agua es campeona (sin cuenta de 10 s ni gusanos). Si solo queda
  una finalista, es campeona.
- **Campeón**: con una sola tortuga a 3 conchas (o al ganar el sprint). Menú: **Volver a jugar** (conchas a cero y ronda 1 en la misma
  playa, sin viajar), **Cambiar de modo** (pasa a Cooperativo y vuelve al lobby) y **Salir** (el anfitrión cierra la partida; los
  invitados vuelven al menú con «El host abandonó la partida»). Solo el anfitrión tiene activos los dos primeros.
- **Nadie muere** (`TNBeach::StunTurtle`, `Private/World/Beach/TN_BeachStun.cpp`): lo que mataría aturde en bola. Si el golpe viene de una
  zona de muerte, de la tormenta o del vacío, la tortuga **vuelve a su último sitio seguro** (se apunta cada 0,5 s) y queda aturdida 2,5 s
  (`RescueStunSeconds`); en cualquier otro caso queda aturdida 3 s donde está (`DeathStunSeconds`). El vacío está 150 m por debajo del suelo
  más bajo pisado (`VoidDepth`). Antes que el vacío actúa la red de seguridad de §5.6.
- **Salto final**: el acantilado mide 15,5 m; la caída no hace bola ni aturde (`GuardCliffJump`) y la tortuga entra **de cabeza**;
  se queda a la vista 0,8 s en el agua (`FinishSplashHoldSeconds`), con la postura de la zambullida congelada (nunca se la ve ponerse de pie), antes de pasar a espectadora.
- **Desconexiones**: si alguien se va en plena carrera, la ronda sigue con las demás. Quien entra con la ronda en marcha sale desde
  la salida (en el sprint, mira); entre rondas se queda quieta. Si se va el anfitrión, se acaba la partida.

**Dificultad de la carrera**. La elige el anfitrión (Fácil / Normal / Difícil) y **solo cambia el reparto** de la playa, no el
terreno: ayudas ×1,6 / trampas ×0,7 / enemigos ×0,6 en Fácil; ×1 en Normal; ayudas ×1,4 / trampas ×1,8 / enemigos ×2,5 en Difícil
(`FDifficultyProfile`, `Public/World/Beach/TN_BeachLayout.h`; tabla completa en §37.3).

**Llegada al agua y paso entre rondas.** Al tocar el agua, el **huevo negro** (la cáscara oscura del fantasma) se cierra desde arriba y
desde abajo de la pantalla en 0,28 s, sale «Has quedado X.º» con un premio y un mensaje gracioso según el puesto (corona de oro enorme para
el 1.º, cada vez más humillante hasta el alga del 8.º) y el huevo se rompe sobre el fantasma espectador, el recuento o el podio; la tortuga
no se pone nunca de pie (§30.6). Entre rondas, el mismo huevo se cierra sobre el recuento con un «RONDA N» en grande, «Colocando la playa…»
y el 3-2-1, y se rompe al saltar de los huevos de salida (§7.6). En la carrera suena además una música de fondo (§9.3), la bola del
caparazón suena al chocar (§9.7), las gaviotas se pueden esquivar (§36.7) y el segundo agarre de una gaviota ya no deja un torbellino (§21).

### 2.4 Carrera en el mapa procedural (reserva)

`ATN_ProcMapGameMode` con `ProcMode=Race`: mapas cortos y rápidos (rejilla 2 / 3 / 4, cobertura 0,9, 0 / 1 / 1 cruces, 4 / 7 / 9 ramas),
el primero en la meta gana la ronda y gana la partida quien llega a 3 rondas ganadas (`WinsToWinMatch`). Límite de ronda 15 min
(`CompetitiveRoundTimeLimitSeconds` 900 s): al agotarse gana el más adelantado por el camino. Reaparición en las pilas de huevos **del
propio jugador**. Se usa cuando se abre `LVL_ProcMap?ProcMode=Race` o cuando `LVL_BeachRace` no existe
(`ATN_HQGameMode::BeginMatchTravel`).

### 2.5 2 vs 2

Por parejas, con **exactamente cuatro** jugadores (si no, se juega Carrera). Mapa procedural corto con **carriles** paralelos con
puzles de lanzamiento (`ATN_ProcThrowWall`: muro que se supera lanzando al compañero o bajando una rampa con un interruptor,
`ATN_ProcSwitch`) y compuertas de sabotaje para la otra pareja (`ATN_ProcSabotageGate`). Gana la ronda la pareja cuyos dos miembros llegan
antes; las parejas rotan cada ronda (AB|CD → AC|BD → AD|BC) y las victorias cuentan por jugador (primero a 3). Perfil: rejilla 2 / 3 / 4,
0 / 0 / 1 cruces, 2 / 3 / 4 ramas, 1 / 2 / 3 carriles. Si alguien se va, la siguiente ronda se juega como Carrera. **No tiene entrada en el
flujo normal** (ni el menú de crear ni el general lo ofrecen). Fuente: [`Docs/Mapa_Procedural.md`](Mapa_Procedural.md) §4 y §7, `Public/Game/TN_ProcMapGameMode.h`.

### 2.6 Clásico

El recorrido original por **chunks** (`ATN_ChunkManager`, `LVL_Run`, `ATN_RunGameMode`): cadena de módulos de dificultad Easy / Medium /
Hard, con el modelo de resultados por puesto y bonus (§2.8). Se mantiene «intacto» para comparar y para los veteranos del cuartel
([`Docs/Mapa_Procedural.md`](Mapa_Procedural.md), primera sección; texto del general). Solo se llega por el selector del lobby antiguo. El diseño de la LDD
(5 rondas de 6 módulos) es este modo.

### 2.7 Tutorial y práctica

**No hay un modo tutorial jugable hoy**. Lo que existe, y hace de tutorial:

1. **El General Galápago** (`ATN_GeneralBriefing`, `UTN_BriefingWidget`; §3.5, §7.3): sesión informativa de cinco pestañas —«Misión»,
   «Cómo se juega», «Modos de juego», «Reglas» y «Controles» (con las teclas reales del jugador)—. Es opcional.
2. **Patio de pruebas del lobby**: muro interior con mini parkour, escalones, postes, pala giratoria, túnel, toboganes, puente
   bamboleante y medusas trampolín, para probar movimiento sin consecuencias.
3. **Cofre del tesoro** en lo alto de la torre del homenaje: un objeto seguro cada vez, útil para practicar objetos.
4. **Ruta del primer arranque (latente)**: `ATN_HQGameMode` comprueba `UMP_GameInstance::HasCompletedTutorial` (`UTN_TutorialSaveGame`, ranura
   `TutorialState_0`) y, la primera vez del anfitrión, haría aparecer a todos en un `PlayerStart` con la etiqueta `TutorialStart`;
   `ATN_TutorialEntryInteractable` («Repetir Tutorial») teletransportaría a esa zona. **En los mapas actuales no hay ningún `PlayerStart` con
   esa etiqueta ni ese interactuable** (comprobado en `LVL_Lobby.umap` y `LVL_HQ.umap`): el código está, el contenido no. El primer arranque cae en el spawn normal del lobby.
5. **En curso (sin subir)**: un pasillo de tutorial en el cielo, muy por encima del lobby, para la primera partida de cada persona (§11).

### 2.8 Qué se gana y qué se pierde

**Dos monedas distintas** (no hay que confundirlas):

| | **Conchas de partida** | **Puntos de carrera** (`RaceScore`) |
|---|---|---|
| Qué son | Los huecos del recuento: quién gana la partida de carrera | La cuenta que sube al coger conchas de puntos y al llegar |
| Dónde | Solo en la carrera en la playa (`RaceShellHalves`, `RoundWins`) | Todos los modos con mapa (`ATN_CoopPlayerState::RaceScore`) |
| Se ganan | Ganar una ronda (+2 medias), llegar dentro de la cuenta atrás (+1 media) | Recoger conchas de 1 / 25 / 50 / 100 puntos (`TNScoreShells`); al llegar, puesto + bonus |
| Se pierden | Al «Volver a jugar» o al volver al lobby | Nunca se restan en partida; se gastan en la tienda |
| Para qué sirven | Ser campeón | Persisten en el perfil local (`AccumulatedRaceScore` en `UTN_CosmeticSaveGame`, ranura `Cosmetics_Local`) y son la **moneda de la tienda** (hoy todo cuesta 0) |

**Puntos al llegar**: el puesto (1.º 400 … 8.º 50) más un bonus por tiempo (que en la playa casi nunca suma), y las conchas de puntos (1,
25, 50 y 100) que se cojan por el camino. Tablas y reglas exactas en el §30; el contador que las recibe, en el §7.4.

**Qué se gana y se pierde en cada modo**

| Modo | Se gana | Se pierde | Al terminar |
|---|---|---|---|
| Cooperativo | Llegar al mar; puntos (puesto, bonus, conchas); desbloquear con puntos | Acabar eliminada (fantasma) si no hay pila de huevos o compañero que rescate; el tiempo: la tormenta | Resultados 8 s y lobby |
| Carrera (playa) | Conchas (enteras y medias), ser campeona, puntos | **Nada permanente**: no se muere ni se elimina a nadie. Se pierde tiempo (bola, gusano) y la ronda | Campeón (podio) y elección del anfitrión |
| Carrera y 2 vs 2 en mapa procedural | Rondas ganadas | Reaparecer (se pierde camino) | Tabla por rondas ganadas |
| Clásico | Puntos y puesto | Como el cooperativo | Resultados y lobby |

**Revivir con huevo**: solo cooperativo (y solo por la orden de prueba, §2.2). En la carrera no se usa: los finalistas del sprint
salen de los huevos de salida normales ([`Docs/Fantasma_Espectador.md`](Fantasma_Espectador.md)).

## 3. Flujo completo: del arranque a la vuelta

### 3.1 Vista de conjunto

```
ARRANQUE  ──►  MENÚ PRINCIPAL (LVL_Menu)
                 │  «Crear partida» ─► pantalla de crear (modo, pública/privada, plazas, nombre, código)
                 │                     └─► HostRoom ─► sesión (Steam; NULL en el editor) ─► viaje a LVL_Lobby como servidor escucha
                 │  «Unirse»        ─► código de 5 caracteres  o  lista de salas públicas ─► JoinSession ─► conecta al anfitrión
                 ▼
LOBBY (LVL_Lobby, castillo de arena)  ◄────────────────────────────────────────────────┐
   tienda · probadores · General Galápago (modo y dificultad) · patio de pruebas · cofre │
   ponerse listo: huevo de la plaza  ó  sala de la puerta doble                          │
   todos listos ─► cuenta atrás 3 s ─► pausa «Cinematic» 2 s ─► viaje sin cortes         │
                 ▼                                                                        │
PARTIDA   Cooperativo ─► LVL_ProcMap    Carrera ─► LVL_BeachRace    Clásico ─► LVL_Run    │
   la pantalla de carga del huevo se rompe con «¡ADELANTE!»                              │
   …rondas…                                                                              │
   fin: resultados (coop, 8 s) ó campeón y podio (carrera, elige el anfitrión) ──────────┘
                 └─► «Salir» / «Menú principal» ─► LVL_Menu (con aviso si procede)
```

El viaje entre lobby y partida es **seamless travel** (sin cortes): los PlayerControllers, los PlayerStates y la GameInstance
sobreviven; los peones se destruyen antes de viajar ([`Docs/Traspaso_Sesion_Cloud.md`](Traspaso_Sesion_Cloud.md), memoria del proyecto).

### 3.2 Arranque

| Qué | Cómo | Dónde |
|---|---|---|
| Mapa de arranque | `LVL_Menu` (`GameDefaultMap`, `EditorStartupMap`); GameMode global `BP_MenuGameMode` (`MP_MenuGameMode`) | `Config/DefaultEngine.ini` |
| GameInstance | `BP_GameInstance`, subclase Blueprint de `UMP_GameInstance` | `Config/DefaultEngine.ini`, `Public/Multiplayer/MP_GameInstance.h`. **Discrepancia**: el README dice `/Script/Tortunabo.MP_GameInstance` |
| `UMP_GameInstance::Init` | Registra los delegados de sesión (crear, buscar, unirse, destruir, invitaciones de Steam, fallos de red), carga los perfiles locales (cosméticos `Cosmetics_Local`, tutorial `TutorialState_0`) y (fuera de Shipping) escribe `steam_appid.txt`. Da soporte a las salas: `RoomTick` cada segundo y PreLogin/PostLogin del servidor | `Private/Multiplayer/MP_GameInstance.cpp` |
| Ajustes | `UTN_GameSettingsSubsystem` los carga de `Saved/SaveGames/TN_Settings.sav` y de `GameUserSettings.ini` al crearse la GameInstance: valen ya en el menú | [`Docs/Menu_Pausa.md`](Menu_Pausa.md) |
| Pantalla de carga | `UTN_LoadingScreenSubsystem` pinta una vez el arte del huevo (unas décimas de segundo); la primera carga dice «Cargando» | [`Docs/Pantalla_Carga.md`](Pantalla_Carga.md) |
| Steam | `DefaultPlatformService=Steam`, `SteamDevAppId=480` (AppId de pruebas compartido; por eso la palabra clave `TortunaboLobby`), `bInitServerOnClient`. Sin Steam abierto, la sala avisa: «No hay conexión con Steam: ábrelo y vuelve a intentarlo.» En el editor se usa el subsistema NULL | `Config/DefaultEngine.ini` |
| Menú | `AMP_MenuPlayerController`: cursor visible y `FInputModeUIOnly`. Escenografía sencilla (cielo, luz, malla) y música de asset | `Private/Menu/MP_MenuPlayerController.cpp`, `Content/Maps/Lobby/LVL_Menu.umap` |

### 3.3 Menú principal

`UMP_MainMenuWidget` (`Private/UI/Menu/MP_MainMenuWidget.cpp`, sobre el Blueprint `WBP_MainMenuWidget`):

- **Tres botones**: «Crear partida», «Unirse» y «Salir» (cierra el juego). Debajo, una línea de estado: «Listo. Crea una partida o únete a
  una.», o el mensaje de la GameInstance mientras conecta (`OnStatusChanged`).
- **Avisos al volver**: si se llega al menú porque el anfitrión te ha expulsado, la sala estaba cerrada o llena, o el anfitrión se
  fue, el aviso sale arriba (`PendingMenuNotice`, `ConsumeMenuNotice`). Si el rechazo vino al intentar entrar en una sala, se
  reabre la pantalla «Unirse».
- **No hay ajustes ni menú de pausa en el menú principal** ([`Docs/Menu_Pausa.md`](Menu_Pausa.md): en `MP_MenuPlayerController` no hay menú
  de pausa). Los ajustes se cambian dentro del lobby o de una partida. Ver el hueco en §40.1 (en curso, sin subir: un botón «Ajustes» en el
  menú principal que abre el mismo menú).

### 3.4 Salas: crear, buscar, código, cerrar, expulsar

Una partida es una **sala**: la sesión de Steam que crea el anfitrión ([`Docs/Salas.md`](Salas.md)). Cada sala tiene **nombre**, **código**,
**modo**, **plazas** y es **pública** o **privada**.

| Pieza | Archivo | Qué hace |
|---|---|---|
| `UMP_GameInstance` | `Multiplayer/MP_GameInstance.*` | `HostRoom`, `RefreshRoomList`, `JoinRoomByCode`, `JoinListedRoom`, `SetRoomLocked`, `KickFromRoom`, rechazo en `HandleGameModePreLogin`, y `RoomTick` (1 s) que mantiene el anuncio al día |
| `TNRoomKeys`, `TNRoomCode`, `FTNRoomConfig`… | `Multiplayer/TN_RoomTypes.*` | Claves de la sesión, códigos y estructuras |
| `TNRoomNames` | `Multiplayer/TN_RoomNames.*` | 242 nombres localizables (`NSLOCTEXT`, espacio `TNRoomNames`, clave `Room_NNN`); la sesión anuncia solo el **índice** (el orden de la tabla no se cambia nunca; los nuevos van al final) |
| `ATN_RoomInfo` | `Multiplayer/TN_RoomInfo.*` | Actor replicado (siempre relevante) con nombre, código, privada, cerrada, plazas, anfitrión y expulsados; lo lee el menú de pausa |
| `UTN_RoomMenuWidget`, `UTN_RoomCodeField` | `UI/Menu/TN_RoomMenuWidget.*` | Pantallas «Crear partida» y «Unirse» |
| `UTN_PauseMenuWidget` | `UI/Pause/TN_PauseMenuWidget.*` | Cabecera con la sala y página «Sala» (cerrar, invitar, expulsar) |

**Crear** (pantalla «CREAR PARTIDA»; lo último elegido en esta ejecución, con un nombre y un código nuevos):

| Opción | Valores |
|---|---|
| Modo | Cooperativo o Carrera (con su explicación debajo) |
| Sala | Pública o Privada |
| Plazas | 4, 6 u 8 tortugas, el anfitrión incluido (sin pasar de `MaxPlayers` de `Config/DefaultGame.ini`, que es 8) |
| Nombre de la sala | Al azar; «Otro nombre» (Intro, A o clic) saca otro distinto |
| Código de la sala | Solo en la privada: 5 caracteres de `ABCDEFGHJKMNPQRSTUVWXYZ23456789` (sin O, 0, I, 1 ni L); «Copiar» (Intro, A o clic) |

«Crear partida» guarda la elección, vacía las listas de miembros y expulsados de la sala anterior, crea la sesión y viaja al lobby con
la pantalla de carga. `HostSession` y `HostSessionWithMode` (Blueprint) siguen valiendo: sin sala elegida crean una **pública con nombre y
código al azar** (así también en «Play As Listen Server» del editor, con 8 plazas).

**Ajustes anunciados en la sesión** (`EOnlineDataAdvertisementType::ViaOnlineService`; con Steam, datos del lobby con sufijo de tipo):
`SEARCH_KEYWORDS=TortunaboLobby`, `ROOMNAME_ID`, `ROOMCODE` (todas lo llevan; solo se enseña en las privadas), `PRIVATE`, `LOCKED`, `MODE` y
`PLAYERS`. `NumPublicConnections` = plazas; `bAllowJoinInProgress`, `bAllowJoinViaPresence` y `bAllowInvites` siempre a `true`, **también
en las privadas y las cerradas**: un lobby privado de Steam no sale en ninguna búsqueda (la búsqueda por código no lo encontraría) y uno «no unible»
tampoco (una sala cerrada dejaría de salir con su candado). El cierre lo aplica el servidor. Steam sí esconde los lobbies **llenos**.

**Unirse** (pantalla «UNIRSE A UNA PARTIDA»):

- **Con código**: campo de cinco casillas y «Entrar». Se escribe directamente (minúsculas a mayúsculas; una O, 0, I, 1 o L avisa),
  Retroceso y Supr borran, ← → cambian de casilla, Ctrl+V, Mayús+Insert, clic derecho o «Pegar» pegan (de «Código: K7M2P» saca «K7M2P»), Intro
  entra. Con mando: A empieza a escribir; ↑ ↓ cambian la letra, ← → la casilla, X borra, A o B terminan. La rueda del ratón sobre una casilla
  cambia su letra.
- **Partidas públicas**: lista (hasta 200 salas) con el nombre en el idioma que has elegido, modo, anfitrión, «3/4» y si está cerrada (candado) o llena. Primero
  las que tienen sitio y, entre ellas, las más llenas. Se busca al abrir, cada 20 s con la pantalla abierta y con «Actualizar» (F5 o Y del mando).
- **Búsquedas**: solo una a la vez; a los 25 s sin respuesta se da por fallida. Con Steam se filtra en el servidor (`SEARCH_KEYWORDS`, y `PRIVATE=0`
  para la lista o `ROOMCODE` para el código); el filtro también se repite aquí para el NULL.
- Antes de intentar entrar se mira el anuncio: cerrada o llena se dice sin intentarlo.

| Caso | Mensaje |
|---|---|
| Código incompleto | «El código tiene 5 letras y números (nunca lleva O, 0, I, 1 ni L).» |
| Código que no existe | «No hay ninguna sala con el código K7M2P. Revisa el código (con Steam, una sala llena tampoco aparece).» |
| Cerrada | «La sala está cerrada: el anfitrión no deja entrar a nadie más.» |
| Llena | «La sala está llena: no queda sitio para otra tortuga.» |
| Expulsado | «El anfitrión te ha expulsado de esta sala: no puedes volver a entrar.» |
| La sala ya no existe | «Esa sala ya no existe: puede que el anfitrión se haya ido. Actualiza la lista.» |
| Sin Steam | «No hay conexión con Steam: ábrelo y vuelve a intentarlo.» |

**Dentro de la sala** (menú de pausa, en todos los mapas en red): cabecera `«Tortugas al horno» de Mokius · privada · código K7M2P · 3/4
tortugas · cerrada`; botón **Sala** con nombre, si es pública o privada, el código (copiable), **Entrada: Abierta / Cerrada** (el anfitrión; los
invitados lo ven como texto), «Invitar a amigos de Steam» y las **tortugas en la sala** con su ping. El anfitrión ve un «⋮» en los demás:
Expulsar → «¿Expulsar a X?» → «Sí, expulsar».

- **Cerrar** (`SetRoomLocked`): no entra nadie nuevo, ni por lista, ni por código, ni por invitación. Los que ya estaban (`RoomMemberIds`)
  **pueden volver** si se les cae la conexión.
- **Expulsar** (`KickFromRoom`): se apunta su `PlayerId` en `ATN_RoomInfo`; su cliente lo ve y se va solo al menú con el aviso «El anfitrión te
  ha expulsado de la sala «X».»; si en 2,5 s sigue conectado, el servidor lo echa (`AGameSession::KickPlayer`). No puede volver mientras dure la sala.
- **Servidor: rechazo al entrar** (`FGameModeEvents::GameModePreLoginEvent`): en orden, expulsado → `TNRoom:Kicked`; cerrada y no miembro →
  `TNRoom:Locked`; no miembro y ya hay tantas tortugas como plazas → `TNRoom:Full`. El cliente traduce el texto de fallo de red a su idioma
  y vuelve al menú.
- **Nombres**: cada jugador lee el nombre de la sala en el idioma que ha elegido **en el juego** (no en el de Windows ni en el del motor; §10.2). Sin traducción, sale en español.
- **El anfitrión se va**: los invitados vuelven al menú con «Se ha acabado la partida: el anfitrión se ha ido o se ha perdido la conexión.».
- **Invitaciones de Steam**: siguen igual (`InviteFriends`, `OnSessionUserInviteAccepted`) y valen también en las privadas.
- **Límite de Steam**: una sala llena no sale en la lista ni por código (Steam oculta los lobbies llenos). Pendiente conocido.

### 3.5 Lobby: el castillo de arena

`LVL_Lobby`, GameMode `BP_HQGameMode` (`ATN_HQGameMode`). Es un **castillo de arena redondo** (`ATN_SandCastleLobby`, radio de muralla 24 m) al fondo de
un **valle** cerrado por montañas (`ATN_LobbyValley`) con un bioma por cada hora del reloj (§4.2). Todo se construye en código y se ve ya en
el editor; solo se replica el estado de la puerta y de los huevos ([`Docs/Lobby_Castillo.md`](Lobby_Castillo.md)). Se orienta como un reloj visto desde arriba: las 12 son
+Y (la puerta).

| Elemento | Dónde | Para qué |
|---|---|---|
| **Puerta doble** (`GateName` «TORTUNAVY») | Las 12; sala entre dos puertas | Uno de los dos sitios para ponerse listo. La puerta 1 se abre hacia la plaza si alguien se acerca a menos de 7,5 m; la puerta 2 siempre cerrada en el lobby |
| **Pila de huevos** (`TNCastleKit::BuildEggMound`) | Plaza, centro (0, 700) | El otro sitio para ponerse listo. Un huevo por plaza de la sesión: 8 (siete en el piso bajo, uno arriba), colores turquesa, coral, amarillo, lila, verde lima, rosa, azul y naranja |
| **General Galápago** (`ATN_GeneralBriefing`, «Cuartel General») | 1:07, pegado a la muralla | Sesión informativa de cinco pestañas y, para el anfitrión, elegir **modo y dificultad** |
| **Don Tortugo** (`ATN_ShopKeeper`, «La Concha Dorada») | 10:53 | Tienda de cosméticos (§7.8) |
| **Probadores** ×4 (`ATN_ChangingBooth`) | 2:04, 2:32, 3:00 y 3:26 | Botellas de cristal de mar para ponerse lo comprado |
| **Torre del homenaje** con escalera de caracol, túnel, balcón y **cofre del tesoro** (`ATN_TreasureChest`) | Las 6, en (0, −800) | Mirador de todo el valle. El cofre (azotea) se rebusca 5 s, siempre da un objeto de `DT_Items` y se repite tras 2,5 s |
| **Patio de pruebas** (`ATN_PlaygroundPiece` ×8, `ATN_WobblyBridge`, medusas trampolín ×7) | Al sur del muro interior | Mini parkour: escalones de polo, galleta, postes de cubo, pala giratoria, túnel, tobogán de concha, puente bamboleante |
| **Muro interior** con adarves y toboganes | y = −800 | Separa la plaza del patio; un tobogán rojo baja a la plaza y uno turquesa al patio |
| **Aparición** | 4 `PlayerStart` `Salida_1…4` | Con más de cuatro jugadores se crean `PlayerStart` extra junto a los existentes (`TN_PickSpreadPlayerStart`) |

**Ponerse listo** (`ATN_HQGameMode::SetPlayerReadyState`): dos formas **a propósito** (prueba A/B pedida por el usuario; no se quita ninguna sin que
él lo decida):

1. **Huevo**: meterse en un huevo (a menos de 95 cm de su eje y entre su base y 3,2 m por encima) baja su tapa y marca listo; salir lo quita.
2. **Sala de la puerta doble**: estar dentro cuenta como listo. Con todos dentro, la puerta 1 se cierra.

**Cuenta atrás** (`ATN_HQGameMode`): cuando **todos los conectados** están listos (mínimo `LobbyMinPlayersForStart` = 1, así que se puede probar en solitario), arranca
una cuenta de 3 s (`CountdownStartValue`). Si alguien sale, se cancela (la pantalla de carga se abre sin romperse). A los 0, el flujo pasa a
`Cinematic` durante 2 s (`CinematicDelaySeconds`) y se viaja; a partir de ahí ya no se cancela. El marcador del lobby dice «Sala: X/Y» (Y = plazas de la sesión).

**Cómo empezará la partida**: `ATN_SandCastleLobby::GetStartStyle()` elige por dónde haya **más listos**, sala o huevos; en empate, la puerta. Se guarda en
`UMP_GameInstance::PendingStartStyle` y sale como `?ProcStart=Gate|Eggs` en la URL. Solo lo usa el **mapa procedural**; la carrera en la playa
siempre sale de una fila de huevos.

**Misión con el General Galápago** (`Public/Lobby/TN_LobbyMission.h`, `UTN_BriefingWidget`): hablando con él (E) se abre la sesión en la pestaña
**«Misión»**, la primera de cinco (Misión, Cómo se juega, Modos de juego, Reglas, Controles).

- **Qué se elige**: el **modo** (Cooperativo o Carrera) y la **dificultad** (Fácil, Normal, Difícil), con su explicación de una línea. Abajo, «Orden del día:
  CARRERA · NORMAL». El general contesta en su bocadillo al cambiar.
- **Quién**: solo el anfitrión (servidor escucha o partida sola). Los demás ven lo mismo con las pastillas apagadas y el aviso «El modo y la
  dificultad los elige el anfitrión…». Se replica en el general (`MissionMode`, `MissionDifficulty`) y se **escribe con tiza en una pizarra** junto a
  la mesa: «ORDEN DEL DÍA / MISIÓN: CARRERA / DIFICULTAD: NORMAL».
- **Controles**: ratón; o teclado y mando: ↑/↓ (W/S, cruceta) eligen la fila, ←/→ (A/D, cruceta) cambian la opción, pestañas con Q/E, Tab o LB/RB (o 1–5),
  Esc, Intro, A o B cierran.
- Para pruebas se puede cambiar también con la consola del anfitrión: `TN.Mode Coop|Race` (modo) y `TN.Race.Difficulty Easy|Normal|Hard` (reparto de la playa desde la ronda siguiente).

### 3.6 HQ: el lobby antiguo

«HQ» significa tres cosas en el código; conviene no confundirlas:

| HQ | Qué es |
|---|---|
| `LVL_HQ` (`Content/Maps/Lobby/LVL_HQ.umap`) | El **lobby antiguo**: una maqueta con `ATN_LobbyReadyZone` para ponerse listo y **dos `ATN_ProcModeSelector`** (modo y dificultad) junto a la zona. Ya no es el destino de «Crear partida» (`GameMapPath` = `LVL_Lobby`), pero se mantiene: es donde se pueden elegir **Clásico** y **2 vs 2**, está en la lista de cocinado y el README lo describe como lobby. El archivo tiene cambios locales sin confirmar en git |
| `ATN_HQGameMode` / `BP_HQGameMode` | El GameMode del lobby. **Lo usan `LVL_HQ` y `LVL_Lobby`**: ready-up, cuenta atrás, viaje, tienda y probadores automáticos (`SpawnLobbyShops`), desvío al tutorial |
| «Cuartel General» | La tienda de campaña del General Galápago dentro del castillo (`ATN_GeneralBriefing`) |

### 3.7 Viajes y clientes

**Lobby → partida** (`ATN_HQGameMode::BeginMatchTravel`):

1. Guarda `PendingTravelPlayerCount` (cuántos esperar), `PendingStartStyle` y el lobby de origen (`LobbyReturnMapPath`).
2. Elige destino:

| `SelectedProcMode` | Destino | Nota |
|---|---|---|
| `Race` | `/Game/Maps/Run/LVL_BeachRace` (`BeachRaceMapPath`) | Si el nivel no existe: error en el registro y se juega la carrera del mapa procedural |
| `Coop`, `TwoVsTwo` | `/Game/Maps/Run/LVL_ProcMap?ProcStart=Gate` o `?ProcStart=Eggs` | 2 vs 2 con distinto de 4 jugadores pasa a Carrera |
| `Classic` | `/Game/Maps/Run/LVL_Run` (`MatchMapPath`) | |

3. **Destruye todos los peones** antes de viajar (para que la voz cierre WASAPI con seguridad) y hace `ServerTravel` **sin `?listen`** (reutiliza el NetDriver).

**Cliente**: viaja solo con el servidor. `NotifyClientPendingTravel` marca «viaje legítimo»; si la conexión se pierde durante el viaje, se intenta
**reconectar** a la sesión de Steam (`MaxAutoRejoinRetries` = 8). Al llegar al mapa nuevo, `ATN_RunGameMode` espera a los jugadores hasta 15 s
(`WaitingForPlayersTimeoutSeconds`).

**Esperas antes de soltar a nadie**

- Mapa procedural: cada cliente avisa (`ServerReportProcMapReady`) de que ha construido la generación pedida; tope 30 s (`MapReadyTimeoutSeconds`).
- Playa: `UTN_BeachRoundSyncComponent` avisa en cada cliente cuando su generador tiene la ronda montada (`ServerReportRoundReady`); el servidor espera a
  todos hasta 12 s (`ClientRoundReadyTimeoutSeconds`) y luego corre igual con un aviso. Esto evitó correr con un terreno distinto (§5.6).
- La pantalla de carga no se rompe hasta que el mapa está listo **y** la ronda en juego (§3.8).

### 3.8 Pantallas de carga (el huevo)

[`Docs/Pantalla_Carga.md`](Pantalla_Carga.md). La pantalla de carga es un **huevo** que ocupa toda la pantalla: dos mitades de cáscara entran desde arriba y abajo y se
estampan con un «¡clac!»; encima van «Tortunavy», el estado con puntos animados, un consejo y cuatro tortugas andando; al estar listo el mapa el huevo
tiembla, se agrieta y revienta con un «¡PUM!» (o, al salir de la ronda, con un «¡ADELANTE!» enorme y una de ocho frases de ánimo).

| Momento | Qué pasa |
|---|---|
| Crear o unirse | Se cierra en el acto; el viaje sale cuando está cerrado; se rompe cuando el lobby está listo. Si falla, se abre sin romperse |
| Todos listos en el lobby | Se cierra en todas las pantallas («¡Todos listos! Salimos en 3», «Preparando la expedición») y no se abre hasta que la partida está cargada |
| Cuenta atrás cancelada | Se abre sin romperse (las mitades se retiran con un «fiuu»), salvo si ya está en `Cinematic` |
| Resultados → cuartel | En el último segundo de los resultados se cierra y viaja cerrado |
| Cualquier `LoadMap` | Cerrado en seco; fuera del editor, `MoviePlayer` pinta una copia del mismo huevo en su hilo |
| Ronda en juego | En el mapa procedural el huevo espera a `InProgress`; rompe con «¡ADELANTE!» a los 1,05 s y a los 1,2 s se abre la salida. Tope de espera 40 s |
| Rondas siguientes | Sin huevo de carga: el rótulo «¡ADELANTE!» sale solo (`STN_GoBanner`); en la carrera lo precede el huevo negro con «RONDA N» (§7.6) |
| Tiempo máximo | Si el viaje no llega, se abre solo: 45 s (crear/unirse), 60 s (lobby), 20 s (resultados) |

El huevo va en la capa 20000 del viewport (por encima de todo) y bloquea los clics salvo cuando solo queda el «¡ADELANTE!». Mensajes: «Rumbo al cuartel»,
«Incubando la partida», «Volviendo al menú». Sonidos sintetizados (`UTN_EggSynthComponent`). Entre rondas de la carrera no sale este huevo
sino la cáscara oscura del fantasma (el «huevo negro»), con «RONDA N» encima (§7.6).

### 3.9 Fin de partida y vuelta

| Modo | Fin | Vuelta |
|---|---|---|
| Cooperativo y Clásico | Cuando todos están resueltos: **resultados** 8 s con la tabla de hasta 8 filas y «Volviendo al lobby en: N» | Automática a `LobbyReturnMapPath` (el castillo) |
| Carrera en la playa | Recuento de 7 s tras cada ronda; al ser campeona una jugadora, la **pantalla del campeón** sin límite | La elige el anfitrión: *Volver a jugar* (misma playa), *Cambiar de modo* (pasa a Cooperativo y vuelve al lobby) o *Salir* (menú principal). Un cliente solo puede salir |
| Menú de pausa | El anfitrión puede pulsar «Volver al lobby» en cualquier partida (`ReturnToLobbyNow`) | Todos van al lobby de origen |
| Salir | «Salir de la partida» (invitado) o «Menú principal» (anfitrión) | El invitado solo sale él; el anfitrión cierra la partida para todos |

La música de fin de partida (victoria o derrota) suena para cada jugador según su resultado y se funde a silencio en el último segundo de la cuenta atrás
(§9.3).

### 3.10 Casos límite

| Situación | Qué pasa |
|---|---|
| Se va el anfitrión | La partida se acaba; los invitados vuelven al menú con «Se ha acabado la partida: el anfitrión se ha ido o se ha perdido la conexión.» |
| Se cae un invitado | Si la sala estaba cerrada, sigue siendo miembro y puede volver (`RoomMemberIds`) |
| Se cae la conexión durante un viaje | Reconexión automática a la sesión de Steam (hasta 8 intentos) |
| Alguien entra con la carrera en marcha | Sale desde la salida (en el sprint, mira); entre rondas, quieta. `bAllowJoinInProgress` está siempre activo |
| Alguien sale con la cuenta atrás del lobby en marcha | Se cancela hasta que vuelvan a estar todos listos |
| Se va la última tortuga que corría durante la cuenta de 10 s | «¡TIEMPO!» sin esperar |
| Solo queda una finalista en el sprint | Es campeona |
| `NetChecksumMismatch` (builds distintas) | Se gestiona en `HandleChecksumMismatch` (no se ha verificado qué muestra al jugador) |
| Expulsado | Vuelve al menú con aviso; no puede volver a esa sala |

## 4. Mapas: visión general

Aquí solo va la **estructura general** de los mapas del proyecto y del lobby. El generador del mapa procedural del cooperativo, con sus
biomas, retos y peligros, está en el §32; la playa de la carrera, con su terreno fijo y su decorado, en el §33; el reparto de elementos por
ronda y por dificultad, en el §37; y las piezas de la playa, las trampas y los enemigos, uno a uno, en los §34, §35 y §36.

### 4.1 Los mapas del proyecto

| Mapa | Archivo (`Content/Maps/…`) | GameMode | Para qué | Notas |
|---|---|---|---|---|
| `LVL_Menu` | `Lobby/LVL_Menu.umap` | `BP_MenuGameMode` | Menú principal | Música de asset; sin menú de pausa |
| `LVL_Lobby` | `Lobby/LVL_Lobby.umap` | `BP_HQGameMode` | **Lobby actual**: castillo de arena y valle | Destino de «Crear partida» (`GameMapPath`) |
| `LVL_HQ` | `Lobby/LVL_HQ.umap` | `BP_HQGameMode` | Lobby antiguo con zona de listos y dos selectores | Único sitio donde se eligen Clásico y 2 vs 2 |
| `LVL_ProcMap` | `Run/LVL_ProcMap.umap` | `BP_ProcMapGameMode` | Cooperativo, 2 vs 2 y carrera procedural | Contenido de `/Game/ProcMap` generado con `Scripts/build_procmap_assets.py` |
| `LVL_BeachRace` | `Run/LVL_BeachRace.umap` | `TN_BeachRaceGameMode` (C++ directo en World Settings) | Carrera en la playa | Creado con `Scripts/build_beach_race.py` |
| `LVL_Run` | `Run/LVL_Run.umap` | `BP_RunGameMode` | Clásico (chunks) | `ATN_ChunkManager` |
| `LVL_ProcMap_Terrain` | `Run/LVL_ProcMap_Terrain.umap` | `ATN_TerrainViewGameMode` | Solo terreno, para comparar generadores (`bTerrainOnly`, `TNRegen [semilla]`) | Depuración |
| `LVL_TestMap`, `LVL_ProcGenDemo`, `LVL_LevelMetrics` | `Run/`, `Maps/` | — | Pruebas y métricas | Sin uso en el flujo |
| `LVL_Transition` | `Maps/LVL_Transition.umap` | — | `TransitionMap` de los viajes con corte (no seamless) | |

### 4.2 El lobby: castillo y valle

- **Castillo** (`ATN_SandCastleLobby`): suelo redondo de radio 26,2 m, muralla de radio interior 24 m y ~6 m de alto con almenas, 9 torres sin simetría de 7,6 a 12,5 m, puerta doble,
  muro interior que separa la plaza del patio de pruebas, torre del homenaje de 9 m con escalera de caracol de 36 peldaños. Barreras invisibles: nadie se cae. Mallas: `CastleMesh` (con
  colisión), `DecorMesh` (sin colisión) y `BarrierMesh`. Consola: `TN.Lobby.Castle 0`.
- **Valle** (`ATN_LobbyValley`): terreno de rejilla polar (34 080 triángulos, sin colisión), sierra que sube desde 120 m hasta la cresta (205 ± 14 m) y cordillera lejana a 350 m; un bioma por hora
  del reloj, con vegetación (unas 80 HISM), 53 animales de 25 especies (locales y cosméticos), pájaros que van de almena en almena y 48 pájaros en 10 bandadas. Consola: `TN.Lobby.Valley 0`.

| Hora | Bioma | Qué hay |
|---|---|---|
| 12 | Laguna (agua con isletas) | Cinco isletas y palafito; al fondo, cascada de unos 30 m |
| 1 | Playa | Palmeras, cabo con faro, caracola gigante, barco varado |
| 2 | Dunas (desierto) | Saguaros, tortuga colosal de piedra, obelisco, cráneo fósil |
| 3 | Cañón (desierto) | Mesas en terrazas, chimeneas de hadas, roca en equilibrio |
| 4 | Volcán | Cono de ~57 m con lava, tres ríos de lava, humo y brasas |
| 5 | Acantilados (rocas) | Terrazas grises, agujas, castillo en ruinas |
| 6 | Cumbres nevadas (rocas) | Sierra más alta, detrás de la torre del homenaje |
| 7 | Bosque | Pinos, abetos, abedules, círculo de piedras |
| 8 | Pueblo (zona humana) | Diez casitas de colores, molino |
| 9 | Granjas (zona humana) | Parcelas con setos, granero rojo |
| 10 | Selva | Copas, ceibas, bambú, pirámide escalonada, cabeza de piedra |
| 11 | Manglar | Llanura de fango, mangles, palafito |

La puerta doble da a la laguna. Desde la azotea de la torre del homenaje se ven todos los biomas de un vistazo. El paisaje sonoro del lobby es el de siempre.

### 4.3 Otros mapas

- **Clásico** (`LVL_Run`): cadena de chunks (`ATN_ChunkManager`) con dificultades Easy / Medium / Hard; ver `project_chunk_system` en la
  memoria del proyecto y [`Docs/LDD_Tortunabo.md` (eliminado)](LDD_Tortunabo.md (eliminado)) (módulos de 300 m y 35 m de ancho en inicio y fin).
- **Solo terreno** (`LVL_ProcMap_Terrain`): el mismo generador sin vegetación, fauna, hitos, recompensas, huevos, peligros ni efectos, para comparar terrenos.
- **Menú** (`LVL_Menu`): escenario sencillo con cielo y luz.

### 4.4 Qué cambia de una ronda a otra

| | Mapa procedural | Playa |
|---|---|---|
| Semilla | Nueva en cada ronda (`bRegenerateEachRound`; `FixedSeed` para repetir); en el cooperativo hay una sola ronda | Nueva en cada ronda (`RoundNet.Seed`); `?BeachSeed=N` la fija |
| Terreno | Nuevo | **Igual siempre** |
| Sitios de salida | — | Rotan cada ronda |
| Dificultad | Fija al empezar el mapa | La del general al empezar el mapa; `TN.Race.Difficulty` cambia la siguiente |

## 5. Red y límites

### 5.1 Modelo

- **Servidor escucha (listen server)**: el anfitrión es servidor y jugador a la vez; los demás son clientes. El proyecto se juega y se
  prueba así; varias piezas evitan construir lo visual y lo sonoro en un servidor dedicado, pero no hay
  ningún documento ni configuración que lo declare soportado (no confirmado). **Steam Sockets** (`SteamSocketsNetDriver`, con `IpNetDriver`
  de reserva) y sesiones de Steam (subsistema NULL en el editor).
- **Antes de las salas**: `SteamDevAppId=480` (Spacewar de pruebas, compartido con otros proyectos); todo lo del juego se distingue por la clave `TortunaboLobby`.
- **Viajes**: seamless travel entre lobby y partida (§3.7). Los peones se destruyen antes de viajar.
- **Autoridad**: el servidor decide las reglas (rondas, puntos, muertes, conchas, entradas a la sala). Los RPC del cliente llevan validación
  (p. ej. voz: `Server_SendVoiceData_Validate`, tope 8192 B) y las frases rápidas
  tienen enfriamiento de 2 s (`QuickChatCooldownSeconds`). No hay sistema antitrampas.

### 5.2 Límites

| Límite | Valor | Fuente |
|---|---|---|
| Jugadores por sesión | **8** (`MaxPlayers`), 1–16 posible por config | `Config/DefaultGame.ini`, `MP_GameInstance.h` |
| Plazas elegibles al crear | 4, 6 u 8 | `TNRoomLimits::Options` |
| 2 vs 2 | Exactamente 4 | `TNLobbyMission::NextSelectorMode` |
| Mínimo para salir del lobby | 1 (para probar en solitario) | `LobbyMinPlayersForStart` |
| Salas en la lista | Hasta 200 | [`Docs/Salas.md`](Salas.md) |
| Huevos del lobby y de la salida | 8 (`NumEggs`); sitios de la sala de la puerta: 8 | [`Docs/Lobby_Castillo.md`](Lobby_Castillo.md) |
| Oyentes de voz por paquete | 4 como mucho, dentro de 25 m | `UProximityVoiceComponent::MaxVoiceListeners`, `OuterRadius` |
| Tasa de red | `NetServerMaxTickRate` 60, `MaxClientRate`/`MaxInternetClientRate`/`ConfiguredInternetSpeed` 200 000 B/s por cliente | `Config/DefaultEngine.ini` |
| Timeouts | Conexión inicial 30 s, conexión 45 s | `Config/DefaultEngine.ini` |
| Reintentos | Escucha 5; reconexión tras viaje 8 | `MP_GameInstance.h` |
| Frecuencia adaptativa | `net.UseAdaptiveNetUpdateFrequency=1` | `Config/DefaultEngine.ini` |

### 5.3 Qué se replica y cómo

| Capa | Qué | Cómo |
|---|---|---|
| **GameState del lobby y flujo** (`ATN_CoopGameState`) | `MatchFlowState`, `CountdownValue`, conectados, listos, esperados, terminados, tabla de resultados | Propiedades replicadas; el servidor (anfitrión) avisa a mano porque `OnRep` no corre en él |
| **GameState de rondas** (`ATN_ProcMapGameState`) | Modo, dificultad, ronda actual, objetivo, texto de resultado, semilla, minutos estimados | Con `OnRep_RoundInfo` |
| **GameState de la carrera** (`ATN_BeachRaceGameState`) | Fase, ganadora, medias conchas, campeón, podio, cuenta atrás y su hora final, tiempo de ronda, motivo del fin, sprint y finalistas | Propiedades replicadas; las horas van en hora del servidor y cada máquina calcula lo que queda |
| **PlayerState** (`ATN_CoopPlayerState`) | Listo, vivo, terminado, puesto, `RaceScore`, `RoundWins`, `RaceShellHalves`, cosméticos equipados (casco, caparazón, piel, ojos) | 5 Hz como mucho y 1 Hz en reposo; `ForceNetUpdate` al llegar, morir, revivir, puntos y conchas |
| **Generador de la playa** | `RoundNet` (semilla, dificultad, huevos abiertos y desde cuándo, nido del sprint, cortes de decorado) | Replicado; el resto se calcula igual en cada máquina |
| **Decorado de la playa** (~80 % del reparto) | **Nada**: cada máquina lo monta igual con la semilla en un campo de mallas instanciadas | Determinismo; unos 200–300 componentes en vez de ~4000 actores |
| **Elementos con estado** (trampas, enemigos, cofres, conchas, objetos) | Actores replicados (~1000–1500 por ronda), casi todos **dormidos** | *Dormancy* y relevancia por distancia: enemigos a 200–300 m; el resto a 260–450 m del punto de vista; castillos con salas y fortalezas, en toda la playa |
| **Rebuscables** | Registro ligero por índice; un actor solo cuando alguien se acerca | Hasta 240 puntos con unos 5–20 actores a la vez |
| **Conchas de puntos** | Dormidas, relevantes a 200 m | El aviso de recogida va por el PlayerState (fiable), no por la concha, que se destruye |
| **Tortuga** (`ATortugaCharacter`) | Movimiento, estado, cabeza (grados enteros, 12 veces por segundo como mucho), energía (un byte para los demás, `StaminaShared`) | Hasta 30 Hz y 10 Hz cuando no cambia (frecuencia adaptativa); el RPC de correr solo va al cambiar |
| **Movimiento** | Correcciones del servidor con suelos creados en ejecución en coordenadas de mundo y sin base (§5.6) | `FTNTurtleNetworkMoveDataContainer` |
| **Caparazón** (`ATN_ShellBody`) | Física en todas las máquinas con réplica en *interpolación predictiva* | Menos temblor y saltos |
| **Sala** (`ATN_RoomInfo`) | Nombre, código, cerrada, plazas, expulsados | Siempre relevante |
| **Fantasma espectador** (`ATN_SpectatorGhost`) | Cámara y a quién sigue | Siempre relevante, 15 Hz, `COND_SkipOwner` |
| **Voz** | Paquetes comprimidos | RPC no fiable al servidor, que los reenvía (§5.5) |
| **Chat rápido** | Entradas individuales | Multicast (no se replica el array entero) |

### 5.4 Presupuesto de red (estimado, aún sin medir con `stat net`)

Por cliente en una carrera de 4: tortugas ≈ 2–4 KB/s (5–8 KB/s con 8), PlayerState casi 1 Hz, enemigos a menos de 300 m a 10 Hz cerca y 3 Hz
lejos, voz 16 KB/s por cada una que te hable. Subida del anfitrión con 8: juego ~40–60 KB/s más la voz (de 0 a ~0,5 MB/s
según cuántas hablen a la vez). **Pendiente**: medir con `stat net` con 4 y con 8 y pasar la voz a Opus (`Docs/Plan_Carrera_Ronda4.md` (eliminado), «Pendiente de antes»).

### 5.5 Voz de proximidad

La voz **no** usa el VOIP del motor (`[Voice] bEnabled=false`): `UProximityVoiceComponent` captura por WASAPI a 48 kHz, baja a 16 kHz,
comprime a μ-law de 8 bits y manda paquetes cada 0,08 s al servidor por RPC **no fiable**; el servidor los reenvía solo a los 4 oyentes más
cercanos dentro de 25 m (16 KB/s por quien habla). Detalle, ajustes y límites en el §26. No se debe cerrar la captura de WASAPI durante un
viaje: por eso los peones se destruyen antes de `ServerTravel` y el micrófono elegido solo cambia al reaparecer o al cambiar de mapa.

### 5.6 Correcciones de movimiento y red de seguridad

Lo que pasó y cómo se arregló ([`Docs/Modo_Carrera.md`](Modo_Carrera.md), «Seguridad: nunca bajo el mapa» y «El bucle torbellino»):

- Las teselas, el decorado local y las piezas creadas en ejecución **no tienen nombre estable por red** y son `Movable`, así que el cliente
  **ignoraba todas las correcciones del servidor** y acababa hundido o metido en el decorado. Ahora esas bases se
  tratan como «sin base» (`UTN_TurtleMovementComponent::IsNetResolvableBase`) y todo va en coordenadas de mundo.
- La cápsula ya no crece de pie dentro del suelo al acabar el panzazo (`RestoreStandingCapsule`).
- La salida espera a que **cada cliente** haya montado la ronda (§3.7).
- **Red de seguridad bajo la arena** (`ATN_BeachRaceGameMode::GuardUnderSand`, servidor, 10 veces por segundo): si una tortuga está más de
  1,6 m (2,6 m en trincheras) por debajo de la arena **y** de la malla del terreno, o cae más de 0,6 s sin suelo, la devuelve
  encima, en tres niveles para no entrar nunca en bucle (mismo punto → su último sitio seguro → arena abierta lejos), con 1,5 s de gracia y
  3 s sin patadas de la tormenta. Consola: `TN.Race.Bury`, `TN.Race.SafetyNet 0|1`.
- **Red de seguridad bajo el terreno en Coop y Clásico** (#633, `UTN_UnderTerrainGuardComponent` de `ATN_RunGameMode` y, con él, de
  `ATN_ProcMapGameMode`; servidor, 10 veces por segundo): la misma regla de confirmación que la de la playa (`TNUnderTerrain::RegisterLook`:
  dos miradas seguidas a más de 1,6 m bajo la superficie, o una a más de 4 m) y el mismo punto que se mira (`TNUnderTerrain::BodyProbe`).
  Sin generador de arena, la superficie se busca con trazas (hasta 30 m por encima) y solo cuenta si debajo no hay suelo (bajo un puente o
  en una cueva no se toca). No salva a quien nada, está en una zona de muerte o cae al vacío sin nada encima. La pone de pie en la
  superficie más cercana por encima (o alrededor, hasta 10 m) con `TNBeach::RelocateTurtle`, sin caída ni daño; si se vuelve a hundir en
  5 s, en su último sitio seguro. Registro: `[Red de seguridad]`. Consola: `TN.SafetyNet.Bury`, `TN.SafetyNet.UnderTerrain 0|1`. Pruebas:
  `Tortunabo.SafetyNet.UnderTerrain.*`.
- **Tormenta de bañistas**: su patada busca siempre arena abierta ~20 m por delante del frente; no puede dejar a nadie atrás.
- **Bola del caparazón**: réplica predictiva, aristas suavizadas, giro máximo de 900 °/s, rebote 0,2.
- **Recolocar ya no deja una caída vieja** (ronda 4): `TNBeach::RelocateTurtle` pasa por `MOVE_None` antes de `MOVE_Falling` si la tortuga
  ya caía. Pedir otra vez el mismo modo no hace nada y el personaje conservaba la altura de la caída anterior: en su paso siguiente contaba
  «5 m de caída» y metía sola en bola a quien acababan de dejar de pie (bola, hundida, red de seguridad, bola: el «torbellino» de la segunda
  gaviota; §21).

### 5.7 Preparado para ocho

Hecho: sesión con 8 plazas; nido de ocho huevos (dos filas de cuatro) en la playa y en el sprint; HUD con hasta 7 compañeros; recuento y
título del sprint que se encogen; lobby con 8 huevos, 8 sitios en la salida del cooperativo, `PlayerStart` extra y marcador de 8 filas.
Pendiente: voz con Opus; el test del reparto solo mira la fila de 4; medir el ancho de banda real; en el menú de pausa, los jugadores de la
cabecera van en **una sola fila horizontal** (`UHorizontalBox`) y [`Docs/Modo_Carrera.md`](Modo_Carrera.md) la deja como pendiente («se sale
con 8»); no se ha podido comprobar en pantalla (§42).

## 6. Controles

Enhanced Input. Las acciones son assets `IA_*` en `Content/Blueprints/Gameplay/Controls/` y sus teclas de serie están en el contexto `IMC_Player`. Las teclas de la tabla de abajo salen de leer ese
asset (no de un documento); el menú de pausa las lista siempre desde el `IMC_Player` real ([`Docs/Menu_Pausa.md`](Menu_Pausa.md), «Controles»).

### 6.1 Jugando (tortuga)

| Acción (`IA_*`) | Teclado y ratón | Mando | Tipo | Notas |
|---|---|---|---|---|
| Moverse (`IA_Move`) | **W A S D** | Stick izquierdo | Continuo | En el menú de pausa sale por direcciones: Avanzar, Retroceder, Ir a la izquierda, Ir a la derecha (se reasignan las del teclado; el stick no) |
| Mover la cámara (`IA_Look`) | **Ratón** | Stick derecho | Continuo | Sensibilidad e inversión del eje Y en los ajustes; no se reasigna |
| Saltar (`IA_Jump`) | **Espacio** | **A** (Cruz) | Pulsar | En el aire, otra vez: **panzazo** (plancha) |
| Correr (`IA_Sprint`) | **Mayús izquierda** | **Gatillo derecho** (RT) | **Mantener** | Gasta energía (salvavidas del HUD) |
| Usar, coger y lanzar (`IA_Interact`) | **E** | **X** (Cuadrado) | Pulsar / **mantener** | Interactuar con lo que hay delante; coger y lanzar tortugas en caparazón; usar el objeto de la mano si no hay nada que coger; mantener para rebuscar decorados (1,3 s), el cofre del lobby (5 s) y el de la playa (5,5 s) |
| Meterse en el caparazón (`IA_Shell`) | **Ctrl izquierdo** | **B** (Círculo) | Pulsar (alterna) | No hace nada con un objeto en la mano. El botón del mando llegó en la ronda 4 (ver el punto de abajo) |
| Soltar el objeto (`IA_DropItem`) | **X** | **Y** (Triángulo) | Pulsar | También suelta a quien llevas |
| Cambiar de objeto (`IA_RotateInventory`) | **G** | **RB** | Pulsar | Alterna entre los dos huecos |
| Rueda de bailes (`IA_OpenEmoteWheel`) | **Q** | **Gatillo izquierdo** (LT) | **Mantener** | Se elige con el ratón o el stick derecho y se confirma al soltar |
| Rueda de frases rápidas (`IA_OpenChatWheel`) | **C** | **LB** | **Mantener** | Igual; enfriamiento de 2 s entre frases |
| Elegir en la rueda (`IA_RadialNavigate`) | Ratón | Stick derecho | — | Fija |
| Hablar (pulsar para hablar) | **V** | **Cruceta abajo** | Mantener | Solo con el modo «Pulsar para hablar» (Ajustes > Voz) |
| Menú de pausa | **Esc** (Tab en el editor) | **Start** | Pulsar | Se reasigna; Esc vale siempre |

- Los emotes directos `IA_Emote` e `IA_Emote1…9` están **cableados en el código** (`ATortugaCharacter`, `EmoteActions`) y el general los
  lista en «Controles», pero **`IMC_Player` no les da ninguna tecla**: hoy los bailes solo salen por la rueda.
- Retroceso **ya no saca de la partida**: `IA_Quit` se quitó de `IMC_Player` el 28-09-2026 para no volver al menú sin preguntar. Salir va por el menú de pausa, que confirma.
- En la carrera, «usar el objeto» es la misma tecla que interactuar (E); si hay algo que coger cerca, coge.
- **Reanimar no va con E.** A una tortuga derribada la levanta una compañera **haciendo un emote** a menos de 300 cm durante 3 s (§20.3); a
  una eliminada, el rescate del cooperativo se hace con E **sin mantener** sobre su cuerpo (§22.3).
- **B / Círculo mete y saca del caparazón** (ronda 4). B no hacía nada en juego (en los menús es «volver»). El asset `IMC_Player` la trae
  desde #637 (`Scripts/imc_player_shell_b.py`, ejecutado en el editor sin ventana; el test `Tortunabo.Settings.PlayerInputMapping` lo
  comprueba). `UTN_GameSettingsSubsystem` la añadía como tecla de serie de `IA_Shell` (`PendingCodeDefaults`); con el asset al día esa lista queda vacía.
  La lista de controles del menú la enseña sola, porque sale del `IMC_Player` en ejecución.
- **Una tecla mantenida al cerrar un menú no se repite en el juego** (ronda 4). Con el menú a la vista, el menú se come la pulsación; si la
  tecla sigue apretada cuando desaparece, el motor manda repeticiones y Enhanced Input las toma por una pulsación nueva (B metería en el
  caparazón, A saltaría). `FTNHeldKeyGuard`, un procesador de entrada de Slate, descarta esas repeticiones hasta que se suelta la tecla;
  vale para todas las pantallas con ratón (pausa, tienda, probador, general, campeón...).
- **Comportamiento de las teclas** (detalle en el §14 y el §23): la tortuga se orienta hacia donde avanza (360°/s) y el avance es relativo a
  la cámara; correr solo funciona con entrada de movimiento (zona muerta 0,25) y estamina; hay un solo salto y la segunda pulsación en el
  aire es la plancha; la sensibilidad por defecto del ratón es 0,7 en X y 0,3 en Y [BP] y el menú de pausa la escala (20–300 %) e invierte
  el eje Y; E tiene un orden de prioridades (interactuable, coger, usar el objeto de la mano; §23.1); X (o Y del mando) con una tortuga en
  alto la deja delante sin lanzarla (§19.5).

### 6.2 Espectador (fantasma)

Teclas fijas en código (`ATN_SpectatorGhost`, entrada propia de prioridad 5); el menú de pausa las enseña pero no las cambia.

| Acción | Teclado y ratón | Mando |
|---|---|---|
| Cambiar de tortuga | ← / → · Re Pág / Av Pág · rueda con la cámara fija | LB / RB · cruceta ← / → |
| Cámara libre ⇄ fija | **C** | **R3** (clic del stick derecho) |
| Girar la cámara libre | Ratón | Stick derecho |
| Acercar / alejar (libre) | Rueda | Gatillo derecho / izquierdo |

La cámara libre usa la sensibilidad e inversión del menú de ajustes, y no gira con el cursor a la vista (rueda o menú de pausa abiertos).

### 6.3 Menús

| Pantalla | Teclado y ratón | Mando |
|---|---|---|
| **Menú de pausa** | Flechas o WASD mueven el foco; Intro, Espacio o clic pulsan; ← → (A D) cambian deslizadores y listas; Q / E cambian de pestaña; rueda desplaza listas; ratón enfoca al pasar y arrastra deslizadores; Esc, Retroceso: atrás (en la portada, cierra); Tab o la tecla elegida: cierra del todo | Stick izquierdo o cruceta; A pulsa; LB / RB pestañas; B: atrás; Start: cierra del todo |
| **Salas** (crear / unirse) | Intro elige; ← → cambian; Ctrl+V pega el código; F5 actualiza; Esc vuelve | A elige; Y actualiza; B vuelve. En el campo del código: A escribe; ↑ ↓ cambian la letra; ← → la casilla; X borra |
| **General Galápago** | Ratón; ↑ ↓ / W S filas; ← → / A D opciones; Q / E / Tab / flechas pestañas; 1–5 pestañas; Esc o Intro cierran | Cruceta; LB / RB pestañas; A o B cierran |
| **Tienda** | Flechas o WASD: elegir (rejilla de 4 columnas); Q / E o Tab: pestaña; Intro o Espacio: comprar; Esc: salir; arrastrar con el ratón gira la tortuga | Cruceta: elegir; LB / RB: pestaña; A: comprar; B o Start: salir |
| **Probador** | ↑ ↓ (W S) fila; ← → (A D) cambiar; Intro o Espacio: ¡LISTO!; Esc: CANCELAR | Cruceta: fila y cambiar; A: ¡LISTO!; B o Start: CANCELAR |
| **Campeón de la carrera** | Ratón sobre los botones | El código no define navegación propia de mando o teclado (**no confirmado**) |

### 6.4 Cómo se cambian las teclas

Menú de pausa > **Controles** (o Ajustes > Controles > «Cambiar teclas y botones»). [`Docs/Menu_Pausa.md`](Menu_Pausa.md).

- Cada acción de botón es una fila con dos columnas, **teclado y ratón** y **mando**. Cambiar: Intro, Espacio, A o clic → «Pulsa una tecla…»
  → la primera tecla, botón del ratón o botón del mando que se pulse (el aparato sale de la tecla). Esc o Select/Vista lo cancelan; a los 8
  s sin
  pulsar, también. Un roce del stick no cuenta. No valen Escape, la consola, Select/Vista, la rueda ni, en el editor, el Tabulador.
- **Conflictos**: si otra fila del mismo aparato tenía esa tecla, se **intercambian** («Espacio estaba en «Saltar»: ahora «Saltar» va con Mayús izq.»).
- **De serie**: Supr, Y del mando o clic derecho en la fila; «Restablecer todos los controles» (con confirmación).
- Se guarda en `KeyOverrides` de `FTNGameSettings` (`TN_Settings.sav`). Técnicamente, con una tecla cambiada el subsistema pone cada
  fotograma una **copia** de `IMC_Player` con esas teclas en lugar del original (`ResolveMappingContext`), así que vale en lobby, partida y
  espectador.
- **No se cambian**: mirar y elegir en la rueda (ratón y sticks), moverse con el mando (stick izquierdo), los controles del espectador y
  moverse por los menús. Además hay dos filas propias: **Hablar** y **Abrir y cerrar este menú**.

### 6.5 Consola

Comandos `TN.*`, `tn.*` y de motor (`fly`, `ghost`, `slomo`, `stat fps`) para pruebas; se escriben en la ventana del anfitrión. Lista
completa en [`Docs/Comandos_Prueba.md`](Comandos_Prueba.md) (carrera, playa, enemigos, objetos, fantasma, pantalla de carga, cooperativo,
cara y HUD, música).

## 7. Interfaz

### 7.1 Principios y arquitectura

- **Todo en código**: la interfaz se monta en C++ con Slate y UMG en ejecución (`bUseCodeHUD = true` en `AMP_GamePlayerController`), con el
  estilo Tortunavy (`Private/UI/HUD/TN_HUDArt.h`, `TN_HUDStyle.h`, `TN_HUDFaces.h`, `TN_ShopArt.h`, `TN_RaceArt.h`, `TN_PauseArt.h`). Unos
  pocos materiales
  de interfaz (`M_UI_TurtleBadge`, `M_UI_RadialWheel`, `M_UI_Preview`) se crean con `Scripts/build_ui_assets.py` y `build_cosmetics.py`.
- **Referencia de 1080p**: el HUD y los menús están pensados a 1920 × 1080; la escala de interfaz del motor los encoge a 720p y los agranda
  en 4K, y el ajuste «Tamaño de la interfaz» (75–130 %) multiplica esa escala.
- **Capas del viewport** ([`Docs/Menu_Pausa.md`](Menu_Pausa.md), `TN_RaceScreens.cpp`, `MP_GamePlayerController.cpp`):

| Capa (Z) | Qué |
|---|---|
| 4 | HUD de la tortuga (`UTN_RunHUDWidget`) |
| 5 | Flujo de partida (`UTN_RunFlowHUDWidget`: tripulación, resultados) |
| 10 | Indicador de voz suelto (solo con el HUD de Blueprint; con el HUD en código habla la cara del distintivo) |
| 15 | Cuenta atrás de 10 s de la carrera |
| 20 / 21 / 22 | Recuento / campeón / título del sprint |
| 30 / 31 | Rueda de bailes / rueda de frases |
| 40 | Tienda y probador |
| 50 | Cáscara oscura (el «huevo negro»: llegada al agua y paso entre rondas de la carrera, `UTN_GhostHatchWidget`) |
| 51 | «Has quedado X.º» y «RONDA N» |
| 55 | «Quién habla» |
| 60 | Menú de pausa |
| 70 | Contador de FPS |
| 19990 | «¡ADELANTE!» solo |
| 20000 | Huevo de carga |

### 7.2 Menú principal y salas

Tres botones (Crear partida, Unirse, Salir) sobre una escena sencilla con música; pantallas de salas encima (§3.3, §3.4). Estilo del menú de pausa (`ETNPauseRowStyle`: filas enfocables con sonido).

### 7.3 Lobby

- **Marcador de sala** (cartel de estado del flujo): «Sala: X/Y | Zona: a/b» esperando; «Todos listos! Empieza en: N» en la cuenta;
  «Preparando…» en la pausa. Son cadenas de `UTN_CoopFlowHUDWidget` (`BuildPrimaryText`), aún sin localizar en `af3f7229` (fase 2 de la
  localización en curso, §10.4).
- **Aviso de interacción**: abajo, un cartel con la tecla de la acción de interactuar (la que tenga asignada el jugador) y el texto del
  interactuable; con las acciones de mantener, un **aro de progreso** alrededor de la tecla (`UTN_HoldRingWidget`).
- **General Galápago** (`UTN_BriefingWidget`, estilo tienda): a la izquierda el general, que habla letra a letra, con su bocadillo; a la
  derecha cinco pestañas: **Misión** (modo y dificultad; solo el anfitrión los cambia), **Cómo se juega**
  (objetivo, moverse, caparazón, cuidado), **Modos de juego**, **Reglas** y **Controles** (las teclas reales de cada acción, del teclado y
  del mando; incluye los emotes directos sin tecla). Cierra con Esc, Intro o «¡Entendido!».
- **Pizarra del general** con la orden del día en 3D.
- **Tienda y probador**: §7.8.
- **Fantasma en el lobby**: solo para probar (`TN.Ghost.Become`).

### 7.4 HUD de la tortuga (todos los modos con mapa)

`UTN_RunHUDWidget` (`Public/UI/HUD/TN_RunHUDWidget.h`), «estilo Tortunavy», con lo siguiente:

| Elemento | Dónde | Qué muestra |
|---|---|---|
| **Distintivo** | Abajo a la izquierda | La **cara cartoon** de tu tortuga según cómo va (feliz, cansada, jadeando con la lengua fuera, caparazón cerrado, mareada, ojos de estrella al llegar; umbrales y prioridad en el §24.3) rodeada de un **salvavidas** que es la **energía** (sin número; del verde al rojo según se vacía; parte se tiñe por el peso que llevas). «¡SIN ALIENTO!» al agotarla. Debajo, una cinta con tu nombre. Cuando hablas por voz, la cara rebota y sale un bocadillo con barras de volumen |
| **Inventario** | Abajo en el centro | Dos burbujas iguales: «EN LA ALETA» (con aro de cuerda) y «EN EL CAPARAZÓN»; el aro rueda a la otra al cambiar de objeto |
| **Contador de conchas** | Arriba a la derecha | La concha de puntos con tu `RaceScore`. Al coger una, iconos de su tamaño (hasta 15) salen de donde estaba la concha, dan un saltito y **vuelan en arco** al contador; cada uno suma su parte con un rebote y un «pom» que sube por la escala pentatónica; «+N» dorado debajo. El número acaba siempre en la puntuación real |
| **Aviso de interacción** | Abajo en el centro (por encima del inventario) | Tecla + texto, más el aro de mantener |
| **Avisos** | Tormenta arriba en el centro (bajo la pista); panza arriba y reanimando, en el centro | Tormenta («¡La tormenta te alcanza! ¡Al agua! N»), panza arriba («¡Panza arriba! Un compañero puede darte la vuelta: N s»), reanimando («Dando la vuelta a tu compañero…») en carteles azul marino con ola |
| **Tripulación** | Izquierda | Cada compañero (hasta **7**): su cara según cómo va, en un **aro de su color** (el mismo que su caparazón), con su nombre y un bocadillo de voz. Con 4–5 filas se juntan y con 6–7 se encogen al 88 %. Los fantasmas salen con su cara de fantasma |
| **Bocadillos del chat rápido** | Junto a la cara de quien habla | Las frases de la rueda salen ahí, no en un chat global (las tuyas, junto a tu distintivo) |
| **Ruedas** | Centro | «EMOTES» y «FRASES»: salvavidas azul marino con gajos, el gajo apuntado se ilumina, la opción elegida abajo («Apunta con el ratón» si no hay ninguna) |
| **«Quién habla»** (opcional) | A la derecha | «X habla» / «Tú hablas» por cada jugador que se oye (accesibilidad) |
| **Contador de FPS** (opcional) | Abajo a la derecha | FPS y peor fotograma del último medio segundo |

El HUD de tortuga lo sigue el espectador: muestra la interfaz de la tortuga a la que mira, no la suya.

### 7.5 HUD del cooperativo (mapa procedural)

- **Pista al mar** (`SeaTrack`): del nido con la tortuguita asomando a la ola con la bandera de meta; tu cara avanza por la arena, los
  compañeros son **caparazones de colores** y la **nube de tormenta** te persigue oscureciendo la arena que ya se ha tragado. Solo existe
  con un `ATN_ProcMapGenerator` (mapa procedural).
- Durante la carrera el **cartel de estado** se oculta: el objetivo ya lo cuenta la pista. Si mueres sin remedio: «¡Fuera de carrera! Anima a los demás hasta que lleguen al agua.» con la cara mareada.
- **Zona de muerte**: «Peligro: sal de la zona de muerte (N s)».
- **Resultados** (8 s): título por puesto («¡Primero!», «Segundo», «Tercero», «Puesto #n», «¡Eliminado!»), tiempo, y tabla de hasta 8 filas
  (puesto, nombre, tiempo, puntos; ✗ si eliminado) con «Volviendo al lobby en: N». La cara pasa a ojos de estrella si llegaste, mareada si
  no.
- Los textos de resultados y del cartel de estado son `FString::Printf` aún sin localizar en `af3f7229` (§10.4).

### 7.6 Pantallas de la carrera en la playa

Las pone y quita `UTN_RaceScreensSubsystem` (`UI/Race/TN_RaceScreens.*`), un subsistema de mundo por máquina que mira el estado replicado:
**sin RPC** ni cambios en el PlayerController. Desde la ronda 4 también monta la llegada al agua y el paso entre rondas (las dos últimas
filas de la tabla). Sonidos sintetizados en 2D (`UTN_RaceCueSynthComponent` y las conchas). Detalle en
[`Docs/Modo_Carrera.md`](Modo_Carrera.md),
«Recuento, campeón y podio».

| Pantalla | Cuándo | Qué muestra | Interacción |
|---|---|---|---|
| **Reloj de ronda** (`UTN_RaceRoundClockWidget`) | Solo el **último minuto** si nadie ha llegado al agua | Pastilla arriba «TIEMPO DE RONDA» con los segundos: dorada desde 30 s, coral latiendo en los 10 últimos con un «¡toc!» por segundo. Cintas «¡Queda 1 minuto!» y «¡Quedan 30 segundos!» con la regla («Si nadie llega al agua, la concha es para la más cerca del mar») | Ninguna |
| **Cuenta atrás de 10 s** (`UTN_RaceFinishCountdownWidget`, Z 15) | Tras llegar la primera | Cinta «¡La primera ya está en el agua!» con su cara y «Concha para X · media concha para quien llegue antes del final»; el número 10…1 en un medallón azul marino que late, dorado a los 3 s y coral en el último; debajo lo que te toca («¡Corre! Media concha si llegas», «¡Concha entera para ti!», «¡Media concha para ti!»). «¡toc!» de caja china cada segundo, más rápido al final; al acabar, «¡TIEMPO!» (o «¡TODAS AL AGUA!») con silbato. Si el fin es por el tope de 9 min: «¡Se acabó el tiempo de la ronda!» y «Nadie ha llegado al agua · concha para X, la más cerca del mar» | No coge ratón ni teclado |
| **Recuento** (`UTN_RaceTallyWidget`, Z 20) | Tras cada ronda, 7 s | Fondo del mar azul marino con rayos y burbujas; cinta «RONDA N»; cartel «Recuento de conchas» → «¡Concha para X!»; una columna por jugador (en orden de entrada) con **tres huecos de concha** en zigzag, su cara con el color de su piel y su nombre (tú, con «TÚ»). Vuela la concha entera de la ganadora; las **medias conchas** saltan una a una a su hueco; si completa la tercera, baja la corona. «Siguiente ronda en N» / «¡Al podio en N!» / «¡Sprint final en N!» | No coge ratón |
| **Sprint final** (`UTN_RaceSprintWidget`, Z 22) | Empate a 3+ conchas, 5 s | «¡SPRINT FINAL!» a pantalla completa con rayos giratorios, cinta «¡Empate a 3 conchas! Solo corren las finalistas, desde la mitad de la playa: la primera en el agua se lleva la partida.», las caras de las finalistas con «VS», confeti, fanfarria. «¡Tú corres!…» o «Tú lo miras de fantasma…» | No coge ratón |
| **Campeón** (`UTN_RaceChampionWidget`, Z 21) | Al terminar la partida | Izquierda: cinta «¡CAMPEONA DE LA PLAYA!», su cara con corona, nombre y conchas, y los botones **Volver a jugar**, **Cambiar de modo** y **Salir**. Derecha: el **podio 3D** (`ATN_RacePodiumStage`, capturado a una textura) con 1.º **vaso** de plástico rojo (Trofeo: sostiene la concha con las dos manos), 2.º **caja de zumo** aplastada (Decepcionada) y 3.º **chancla** (Pataleta, sentada y enfadadísima), en bucles cortos tipo GIF | Ratón. Solo el anfitrión tiene activos los dos primeros botones (los demás, con el candado «Volver a jugar o cambiar de modo lo decide el anfitrión.») |
| **Fantasma / «¡Eres un fantasma!»** | Tras llegar o no correr el sprint | Cartel del fantasma abajo a la derecha con la cámara y los controles; «Te mira *Nombre*» en quienes siguen corriendo (§22.4) | Controles del espectador (§6.2) |
| **Llegada: «Has quedado X.º»** (`UTN_RaceArrivalWidget`, Z 51, sobre la cáscara oscura de Z 50) | Cuando la tortuga del jugador toca el agua de meta (una vez por ronda; en el sprint, la ganadora ve su «1.º» con la cinta «SPRINT FINAL») | La cáscara oscura se cierra en 0,28 s desde arriba y desde abajo con un «¡clac!»; encima, la cinta de la ronda, «HAS QUEDADO», el puesto enorme del color de su medalla y el premio, que cae y se aplasta (coronas de oro, plata y bronce; cubo del revés, media concha rota, flotador pinchado, calcetín mojado y alga de peluca) con su mensaje al azar. Dura 2,7 s (unos 3 s de pantalla negra) y la cáscara se rompe sobre el fantasma, el recuento o el podio (§30.6) | No coge ratón |
| **Paso entre rondas: «RONDA N»** (`UTN_RaceRoundIntroWidget`, Z 51) | Al pasar a `Waiting` desde el recuento, el título del sprint o el podio («Volver a jugar»); no en la primera ronda tras el viaje, que la tapa el huevo de la pantalla de carga | La cáscara oscura se cierra en 0,32 s; «RONDA N» (o «SPRINT FINAL») entra de golpe con un «¡pum!» y una frase debajo; «Colocando la playa…» mientras el servidor prepara la ronda; el 3-2-1 da un «pum» por número; al dar la salida la cáscara se rompe y el título sale disparado mientras la tortuga salta de su huevo | No coge ratón |

Ausencias (no existen en el código): **no hay pista de progreso ni indicador de puesto** durante la carrera en la playa (la pista exige un
`ATN_ProcMapGenerator`); el puesto en carrera solo se calcula para los pesos de objetos (`TNRaceItems::GetRank`). El objetivo, el mar, se ve
desde la salida (margen de 4,2 m sobre el filo) y la sección «Cómo se juega» del general lo explica. El puesto solo se enseña al llegar, en
«Has quedado X.º».

### 7.7 Menú de pausa completo

`UTN_PauseMenuWidget` (`UI/Pause/TN_PauseMenuWidget.*`), [`Docs/Menu_Pausa.md`](Menu_Pausa.md). Existe en el lobby, el mapa procedural, la
carrera y el nivel de solo terreno; **no pausa la partida** (es en red): tu tortuga se queda quieta (`SetIgnoreMoveInput`,
`SetIgnoreLookInput`) mientras miras el menú, y el resto sigue.

**Abrir y cerrar**

- Abrir: Esc (juego empaquetado o standalone), Tab (editor), Start (mando) o la tecla que elijas. No abre con otra interfaz de ratón a la
  vista (tienda, probador, general, ruedas, campeón), con la pantalla de carga a la vista ni durante un viaje. Sí encima del recuento de la
  carrera.
- Cerrar: Tab, Start o la tecla elegida (del todo); Esc, B o Retroceso (atrás; en la portada, cierra); «Continuar».
- Al abrirse: modo de entrada interfaz y juego con cursor; se sueltan las teclas pulsadas. Al cerrarse se devuelve la entrada.

**Cabecera**: cinta «PAUSA» sobre un cartel azul marino con el mapa o modo (lobby del castillo o del cuartel; carrera en la playa con su
ronda y las conchas para ganar; cooperativo con ronda, dificultad y semilla; 2 contra 2; solo terreno; carrera clásica), la sala
(nombre, de quién es, pública o privada con su código, «3/4 tortugas», «cerrada») y los **jugadores** con su cara, nombre, «Tú», «Anfitrión»
(corona) o su ping, y un icono de voz que late cuando habla y sale tachado si está silenciado.

**Portada**

| Botón | Anfitrión | Invitado |
|---|---|---|
| Continuar | Vuelve a la partida | Igual |
| Ajustes | Cinco pestañas (abajo) | Igual |
| Controles | Todas las teclas y botones, cambiables | Igual |
| Sala | (en red) Página de la sala | (en red) Igual, sin cerrar ni expulsar |
| Volver al lobby | Solo en una partida: confirma y lleva a todos al lobby (`ReturnToLobbyNow`) | El cuadro explica que al grupo lo lleva el anfitrión; si confirma, sale él solo al menú principal |
| Menú principal / Salir de la partida | «Menú principal»: cierra la partida para todos | «Salir de la partida»: sale él solo |
| Salir al escritorio | Confirma (avisa de que la partida se acaba para todos si hay invitados) y cierra el juego | Confirma y cierra |

Abajo, la ayuda de la opción enfocada y los atajos. Una línea dorada de **avisos** cuenta lo último que ha pasado (qué tecla se ha puesto, el micrófono nuevo…).

**Ajustes (todos se aplican al momento; los propios se guardan al cerrar el menú o a los 3 s del último cambio)**

*Pestaña GRÁFICOS* (van en `GameUserSettings.ini`; el motor los aplica solo al arrancar)

| Sección | Opción | Valores y efecto |
|---|---|---|
| Pantalla | Modo de ventana | Pantalla completa · Ventana sin bordes · Ventana. Sale «¿Mantener esta pantalla?» y se deshace sola a los 12 s si no se confirma. Apagado en el editor |
| | Resolución | Las que admite la pantalla (o las cómodas en ventana); la de sin bordes es la del escritorio |
| | Escala de resolución | Del mínimo al máximo del motor, de 5 % en 5 %: pinta a menos resolución y estira |
| | Sincronización vertical | Sí / No |
| | Límite de fotogramas | 30, 60, 90, 120, 144, 165, 240 o sin límite |
| | Brillo | 0–100 %; 50 % = el de siempre (gamma del motor, ±0,7) |
| | Mostrar FPS | Sí / No |
| Calidad | Calidad general | Baja · Media · Alta · Épica; «Personalizada» si luego se toca una parte |
| | Sombras · Efectos · Vegetación · Distancia de visión · Antialiasing · Texturas · Postprocesado · Iluminación global · Reflejos | Baja · Media · Alta · Épica cada una |
| | Calidad recomendada | Botón: mide el equipo (la imagen se congela un momento) y elige la calidad que aguanta |

*Pestaña SONIDO*

| Opción | Efecto |
|---|---|
| General | Volumen principal del dispositivo de audio del mundo: todo, voces incluidas |
| Música | Música sintetizada (tienda, probador, victoria, derrota, eliminado, las radios 3D del tendero y la música de fondo de la carrera) |
| Efectos | Pasos, saltos, trampas, enemigos, conchas, bailes y sonidos de los menús |
| Ambiente | Olas, viento, selva, cascadas y el resto del paisaje sonoro |
| Silenciar sin el foco de la ventana | El juego se calla (voces incluidas) si cambias a otro programa |
| Restablecer el sonido | Todos los volúmenes al 100 % y suena también sin el foco |

*Pestaña VOZ*

| Sección | Opción | Efecto |
|---|---|---|
| Compañeros | Voz de los compañeros | Volumen de todas las voces ajenas |
| | Voz de *X* (una fila por compañero) | 0–200 %; solo cambia cómo lo oyes tú; se recuerda entre partidas (por id de Steam o nombre) |
| | Silenciar a *X* | Dejas de oírle; no se entera nadie |
| Micrófono | Silenciar mi micrófono | No se envía nada; la captura sigue (el medidor vive) |
| | Modo | Voz abierta · Pulsar para hablar |
| | Hablar (tecla y botón) | V y cruceta abajo de serie; cambiables |
| | Micrófono | Lista de los micrófonos activos de Windows más «Predeterminado de Windows»; se abre al reaparecer o al cambiar de mapa (no en caliente) |
| | Sensibilidad | 0–100 %; 50 % = −40 dB; cada extremo mueve 20 dB |
| | Ganancia | 25–300 %; 100 % = la de siempre |
| | Nivel del micrófono | Medidor con raya dorada del umbral: «¡Se te oye!», «En silencio», «Silenciado», «Mantén V», «Micro nuevo al reaparecer», «Sin micrófono» |
| | Restablecer la voz | Voz abierta, micrófono predeterminado, sensibilidad y ganancia de siempre y nadie silenciado |

*Pestaña CONTROLES*

| Opción | Efecto |
|---|---|
| Sensibilidad del ratón · del mando | 20–300 % (100 % = la de siempre); se usa la del último aparato tocado |
| Invertir eje Y (ratón) · (mando) | Sí / No |
| Vibración del mando | Sí / No (**Sí de serie**): al recibir un golpe (derribo, aturdimiento que mete en el caparazón, impacto de un lanzable) el mando vibra, más fuerte y más largo cuanto más fuerte es el golpe; solo en la máquina de quien lo recibe |
| Cambiar teclas y botones | Abre la página de controles |
| Restablecer los controles | Sensibilidad al 100 %, sin invertir y con vibración (las teclas se restablecen en su página) |

*Pestaña JUEGO*

| Sección | Opción | Efecto |
|---|---|---|
| Cámara | Temblor de cámara | Sí / No: apagado, se desactivan los modificadores de temblor (golpes, quads, tormenta…) |
| | Campo de visión | −15 a +20° sobre el de la tortuga (se enseña en grados); al correr se abre lo mismo que antes; vale también mirando a otra tortuga |
| | Ojo de pez leve | Sí / No (**No de serie**, #634): proyección Panini suave del motor sobre la imagen del mundo; no deforma el HUD ni los menús; se enciende y se apaga en 1,2 s y se afloja con el campo de visión (§14.3) |
| Interfaz | Tamaño de la interfaz | 75–130 %: agranda o achica el HUD y los menús (el editor no cambia) |
| Accesibilidad | Filtro para daltónicos | No · Deuteranopía (verde) · Protanopía (rojo) · Tritanopía (azul) |
| | Intensidad del filtro | 0–100 % |
| | Quién habla (texto) | Sí / No |
| | Idioma / Language | Primera fila de la pestaña. Lista de los 13 idiomas con su nombre en su idioma; se aplica en caliente; sin elegir, el del sistema si está en la lista y, si no, el español (§10.1) |
| Reinicios | Restablecer esta pestaña | Temblor encendido, ojo de pez apagado, campo de visión e interfaz de siempre, sin filtro, sin «Quién habla» y con el idioma «sin elegir» (el del sistema) |
| | Restablecer todos los ajustes | Con confirmación: sonido, voz, micrófono, controles (teclas incluidas), juego, brillo y FPS; **no** toca calidad gráfica ni pantalla |

Cada pestaña (salvo Gráficos) tiene «Restablecer».

**Página Controles**: tres bloques. «JUGANDO» (una fila por acción cambiable, con teclado y ratón y mando), «VOZ Y MENÚ» (Hablar y Abrir y
cerrar este menú) y «SIEMPRE IGUAL» (lo que no se cambia: mirar, elegir en la rueda, espectador, moverse por los menús).

**Página Sala**: §3.4.

**En el editor**: la resolución y el modo de ventana salen apagados; al acabar la partida se devuelve lo que tenía el editor (calidad, límite de fotogramas, vsync, gamma, filtro y volumen).

### 7.8 Tienda y probador

Las pantallas de la **tienda «La Concha Dorada»** (`UTN_ShopWidget`) y del **probador** (`UTN_BoothWidget`), con su catálogo, su música, sus
controles y su consola (`TNShop`, `TNBooth`), están descritas en el §31.4 y el §31.5; las teclas y botones de todas las pantallas, en el
§6.3. Al acercarse a cualquier interactuable, el HUD enseña el cartel con la tecla de interactuar (§7.3).

### 7.9 Pantalla de carga

§3.8. Es un elemento de interfaz de primer orden: el huevo, el estado con puntos animados, un consejo, las cuatro tortugas andando, el «¡PUM!» y el «¡ADELANTE!».

### 7.10 Espectador (fantasma)

Quien está de espectador es un **fantasmita de tortuga** en todos los modos. Su pantalla, lo que ven los demás, las cámaras, la red y el
contrato de volver a la vida desde un huevo (`TNGhost::ReviveIntoEgg`, solo cooperativo y solo por `TN.Ghost.Revive` hoy) están en el §22.4
y el §22.5; los controles, en el §6.2. En la carrera, la misma cáscara oscura de la pantalla de quien vuelve a la vida hace de «huevo negro»
en la llegada al agua y en el paso entre rondas (§30.6).

## 8. Accesibilidad y ajustes

### 8.1 Cómo leer esta sección

No hay un documento de accesibilidad previo en el repositorio. Cada opción tiene en pantalla una **descripción** (en el menú de pausa, al
enfocarla) que da su razón principal; lo que se añade aquí en «Por qué está / a quién ayuda» es esa razón más lo que se deduce del diseño.
Lo que **no existe** va aparte, en §40.1, y
no se cuenta como existente en ningún sitio.

Los ajustes viven en el menú de pausa (§7.7): Ajustes con cinco pestañas (Gráficos, Sonido, Voz, Controles, Juego) y la página de Controles.
Se aplican al momento, se guardan en `Saved/SaveGames/TN_Settings.sav` (los propios, `FTNGameSettings`) y en `GameUserSettings.ini` (los
gráficos) y se cargan al
arrancar el juego, antes de que empiece la voz. Los valores de serie dejan el juego «como estaba antes del menú de pausa»
(`Public/Settings/TN_SettingsSaveGame.h`). Cada pestaña tiene «Restablecer»; en Juego, «Restablecer todos los ajustes» (con confirmación; no
toca gráficos ni pantalla).

### 8.2 Visión

| Opción | Dónde | Qué hace | Por qué está / a quién ayuda |
|---|---|---|---|
| **Filtro para daltónicos**: No · Deuteranopía (verde) · Protanopía (rojo) · Tritanopía (azul) | Juego > Accesibilidad | `UWidgetBlueprintLibrary::SetColorVisionDeficiencyType` en modo corrección: Slate lo aplica a la **imagen final de la ventana**, así que corrige a la vez el mapa, el HUD y sus marcadores sin tocar colores uno a uno | El juego usa el color para informar: energía del salvavidas de verde a rojo, aros de color por jugador en la tripulación y la pista, oro y coral en la cuenta atrás, colores de huevos y conchas. Ayuda a jugadores con daltonismo |
| **Intensidad del filtro** 0–100 % | Juego > Accesibilidad | Cuánto corrige | Ajustar al gusto: un filtro entero cansa o distorsiona la escena |
| **Tamaño de la interfaz** 75–130 % | Juego > Interfaz | Multiplica la escala de la interfaz del motor (`UUserInterfaceSettings::ApplicationScale`): cambia el HUD y los menús de UMG al momento, no el editor. El menú de pausa se encoge si no cabe | Baja visión, pantallas de sofá o de portátil, resoluciones extremas (4:3, 16:10, 4K) |
| **Brillo** 0–100 % (50 % = el de siempre) | Gráficos > Pantalla | Gamma de salida del motor (`GEngine->DisplayGamma`), ±0,7 | Pantallas mal calibradas, salas con reflejos; la playa es muy luminosa |
| **Campo de visión** −15° a +20° | Juego > Cámara | Suma o resta a los grados de la cámara de la tortuga (reposo y correr); vale también mirando a otra tortuga | Ver más a los lados, o menos ángulo para reducir la distorsión y el mareo |
| **Ojo de pez leve** Sí / No (No de serie, #634) | Juego > Cámara | Curva un poco los bordes de la imagen del mundo (Panini) y se afloja al abrirse el campo de visión; no toca el HUD | Sensación de inmensidad; **puede marear a quien es sensible a la distorsión de lente**: por eso viene apagado y se enciende en la misma pestaña (hueco n.º 14 del §40.1) |
| **Temblor de cámara** Sí / No | Juego > Cámara | Apagado, se desactivan los modificadores de temblor (golpes, quads de la carrera, tormenta…) | «Apágalo si te marea»: sensibilidad al movimiento, fotosensibilidad parcial |
| **Contador de FPS** | Gráficos > Pantalla | FPS y peor fotograma | Diagnóstico para quien ajuste la calidad |
| **Calidad general / partes / escala de resolución / límite de FPS / vsync / modo de ventana / resolución** | Gráficos | Niveles Baja–Épica por sombras, efectos, vegetación, distancia de visión, antialiasing, texturas, postprocesado, iluminación global y reflejos; «Calidad recomendada» mide el equipo | Accesibilidad **de hardware**: equipos modestos (la vegetación llega a ~500 000 instancias). El cambio de modo de ventana o resolución pide confirmar en 12 s y **se deshace solo**, para no dejar al jugador ante una imagen que no ve |

**Redundancias visuales ya presentes en el diseño** (no son opciones, pero ayudan): la energía se lee también por la **cara** del distintivo
(feliz, cansada, jadeando, mareada) y por el texto «¡SIN ALIENTO!»; la cuenta atrás de la carrera es **un número** además de color y sonido;
los peligros se anuncian con
forma (sombra de gaviota, «!», nube de quads, montículos que vibran); el HUD nombra a cada compañero; los avisos importantes son **texto**.

### 8.3 Audición y comunicación

| Opción | Dónde | Qué hace | Por qué está / a quién ayuda |
|---|---|---|---|
| **Quién habla (texto)** Sí / No | Juego > Accesibilidad | «X habla» (o «Tú hablas») a la derecha por cada jugador que se oye, sin los silenciados (`UTN_TalkersWidget`) | «Para jugar sin sonido o si oyes mal»: sordera, audición reducida, auriculares averiados, jugar sin volumen |
| **Ruedas de bailes y de frases rápidas** | Jugando (Q, C; LT, LB) | Comunican sin voz: frases de texto que salen en un bocadillo junto a la cara de quien las dice, y bailes | Alternativa a la voz para quien no puede o no quiere hablar o escuchar; las frases son **texto** (no se necesita oírlas) |
| **General · Música · Efectos · Ambiente** 0–100 % | Sonido > Volumen | Cuatro volúmenes por categoría más el principal ([`Docs/Menu_Pausa.md`](Menu_Pausa.md), «Sonido»); el reparto por categoría es automático (§9.2) | Hiperacusis o sensibilidad al ruido; bajar música o ambiente y dejar efectos y voz; audición con pérdida en unas frecuencias |
| **Silenciar sin el foco de la ventana** | Sonido | El juego se calla (voces incluidas) mientras la ventana no está activa | Streamers, quien comparte pantalla o salta a otros programas |
| **Voz de los compañeros** 0–100 % | Voz > Compañeros | Volumen de todas las voces | Igual que arriba |
| **Voz de *X*** 0–200 % · **Silenciar a *X*** | Voz > Compañeros (una fila por compañero) | Multiplicador de volumen individual, recordado entre partidas por id de Steam; solo cambia cómo lo oyes tú | Compañeros que hablan muy bajito o muy alto; moderación sin drama («Dejas de oírle. No se entera nadie») |
| **Silenciar mi micrófono** | Voz > Micrófono | No se envía nada, pero se sigue capturando y el medidor vive | Privacidad, ruido momentáneo |
| **Modo: Voz abierta / Pulsar para hablar** (+ tecla y botón) | Voz > Micrófono | Con pulsar para hablar, la voz solo sale con la tecla pulsada (V, cruceta abajo) y superando el umbral | Entornos ruidosos, familias, quien no quiere que se le oiga todo |
| **Micrófono** (lista de Windows) | Voz > Micrófono | Elige el dispositivo; se abre al reaparecer o al cambiar de mapa (la captura abierta no se puede cambiar sin riesgo de colgar el juego) | Cascos y micrófonos externos |
| **Sensibilidad** 0–100 % | Voz > Micrófono | Mueve el umbral ±20 dB (50 % = −40 dB) | Voces bajas (que se corten menos) o ruido de fondo (que no se cuele) |
| **Ganancia** 25–300 % | Voz > Micrófono | Sube o baja la voz antes de enviarla | Micrófonos flojos o saturados |
| **Nivel del micrófono** (medidor) | Voz > Micrófono | Barra en vivo con la raya dorada del umbral y un texto («¡Se te oye!»…) | Comprobar sin preguntar a nadie |

**Sin subtítulos**: no hay voces grabadas ni diálogos hablados; la voz de los jugadores tiene «Quién habla» y las frases rápidas son texto
([`Docs/Menu_Pausa.md`](Menu_Pausa.md), «Fuera y por qué»). Los textos del general y de la tienda se escriben en pantalla letra a letra.

### 8.4 Control y motricidad

| Opción | Dónde | Qué hace | Por qué está / a quién ayuda |
|---|---|---|---|
| **Cambiar teclas y botones** (teclado y ratón; mando) | Controles | Reasigna toda acción de botón, con intercambio automático si hay conflicto, restablecer por fila (Supr, Y, clic derecho) y global | Manos, teclados AZERTY o Dvorak, mandos adaptativos, zurdos, conflictos con otros programas. La captura tiene salida limpia (Esc, Select, 8 s) |
| **Sensibilidad del ratón / del mando** 20–300 % | Controles > Cámara | Escala de giro; se usa la del último aparato tocado | Pulso, DPI, sticks duros o blandos |
| **Invertir eje Y** (ratón / mando) | Controles > Cámara | Subir mira hacia abajo | Costumbre de vuelo o de otros juegos |
| **Navegación de menús con teclado, ratón y mando** | Menús | Flechas o WASD o cruceta o stick; Intro, Espacio o A; Q/E o LB/RB para pestañas; ratón enfoca al pasar; **cada fila enfocada explica lo que hace** | Quien no usa ratón, o solo mando |
| **Tabulador / Start / la tecla elegida** para abrir el menú | Controles | El menú tiene su propia tecla y botón, reasignables | Comodidad y conflictos |
| **La tortuga se queda quieta con el menú abierto** | Menú de pausa | Aunque la partida siga en red, no te mueves ni giras la cámara mientras ajustas | Cambiar ajustes sin perder la posición ni chocar por accidente |
| **Confirmación** en salir, volver al lobby, restablecer y cambiar el modo de vídeo | Menús | Cuadros de confirmación (Sí / Cancelar) | Evitar acciones irreversibles por un pulso torpe: «Retroceso ya no saca de la partida» |

### 8.5 Cognición, mareo y asistencia

| Elemento | Qué hace | Por qué |
|---|---|---|
| **Ayuda contextual bajo cada fila** del menú de pausa y de las salas | Una frase por opción, más una línea dorada de avisos que cuenta lo que acaba de pasar (qué tecla se ha puesto, a qué fila se le ha quitado…) | Aprender sin manual; evitar el «¿qué ha pasado?» |
| **General Galápago** | Explicación opcional del juego, los modos, las reglas y **las teclas reales** del jugador (del teclado y del mando) | Tutorial sin obligar (§2.7) |
| **Dificultad Fácil / Normal / Difícil** | La elige el anfitrión: en el cooperativo, mapa más pequeño, menos peligros y tormenta más lenta; en la carrera, más ayudas y menos enemigos y trampas | Asistencia por sala, no por persona |
| **En la carrera nadie muere** | Lo que mataría aturda unos segundos; nadie queda eliminado | Frustración: la diferencia de habilidad no expulsa a nadie de la partida |
| **Objetos tipo karts** | Los pesos favorecen a quien va detrás | Equilibrio entre niveles |
| **Cuenta atrás y reloj visibles** | 3-2-1 de salida, 10 s tras la primera, reloj de ronda el último minuto con avisos a 60 y 30 s | Que el tiempo nunca sea una sorpresa («cortaba rondas sin aviso» fue un fallo corregido) |
| **Fantasma espectador** | Al llegar o caer se sigue mirando la partida con cámaras libre y fija | No quedarse mirando una pantalla vacía |
| **Campo de visión y temblor** | Ver §8.2 | Mareo por movimiento |
| **Salas privadas, cierre, expulsión, silencio y sin chat libre** | Solo frases prefabricadas y voz de proximidad; nombres de sala de una lista revisada | Convivencia y seguridad para jóvenes y familias; evitar acoso (no hay texto libre que moderar) |

## 9. Sonido y música

### 9.1 Filosofía

**Casi todo se sintetiza en código, en tiempo real, sin archivos de audio**: son `USynthComponent` con un generador (`ISoundGenerator`) en
el hilo de audio, C++ puro sin asignaciones ni bloqueos, con parámetros atómicos que escribe el hilo de juego. Se calibra fuera del motor
con un arnés. Dos ventajas: el sonido depende del
estado (velocidad, superficie, estamina…) y no hay archivos que pesar ni doblar. La salida del motor es a 48 kHz (`AudioSampleRate` en `Config/DefaultEngine.ini`). Estilo: cartoon pero creíble.

### 9.2 Categorías de volumen del menú de pausa

`UTN_GameSettingsSubsystem` reparte **cada fotograma** los sonidos generados en código poniendo una `SoundClass` en su sonido ([`Docs/Menu_Pausa.md`](Menu_Pausa.md), «Sonido»):

| Categoría (ajuste) | Qué cae |
|---|---|
| **Música** (clase `TN_Music`) | `UTN_MusicSynthComponent`: tienda, probador, victoria, derrota, eliminado, las radios 3D del tendero y, como hija suya, `UTN_RaceMusicComponent` (la Marcha de la Playa) |
| **Ambiente** (`TN_Ambient`) | `UTN_AmbientSynthComponent`: paisaje sonoro por bioma, cascadas, géiseres, lava; y los sintetizadores de criaturas con `bAmbientBed` (burbujas del pulpo, ruido de la tormenta de la playa) |
| **Voz** (`TN_Voice`, en la pestaña Voz) | Las ondas procedurales de la voz de los compañeros |
| **Efectos** (clase por defecto, mezcla `TN_EffectsMix`) | Todo lo demás: pasos y ruidos de la tortuga, trampas y enemigos, rebuscar, tos, pajaritos, patio del lobby, conchas, golpes del caparazón, sonidos de los menús, huevo de carga, y los sonidos de asset |
| **General** | Volumen principal del dispositivo de audio del mundo: todo, voces incluidas |

Un sintetizador nuevo cae solo en Efectos; para que sea música o ambiente debe heredar de `UTN_MusicSynthComponent` o `UTN_AmbientSynthComponent`. Los sonidos de asset con su propia clase no se tocan.

### 9.3 Música

| Pista | Cuándo | Cómo es (`Public/Audio/TN_MusicSynthComponent.h`) |
|---|---|---|
| **Tienda** «La Concha Dorada» | Radio del puesto en 3D; en 2D al abrir la tienda | Calipso tropical alegre, Fa mayor, 112 BPM, AABA de 32 compases, con steel pan, marimba, bajo pizzicato, congas, shaker y un «ding» de caja registradora |
| **Probador** | Al abrir el probador | Lounge/bossa juguetón, Sol mayor, 96 BPM, ABA de 24 compases, piano eléctrico FM tipo Rhodes, escobillas, silbido y arpegios de brillo mágico |
| **Victoria** | Al ganar (cooperativo: alguien llega a la meta; carrera: campeona) | Fanfarria «¡ta-ta-ta-tááán!» de 5 s y bucle festivo de calipso, Si bemol mayor, 120 BPM |
| **Derrota** | Si no | «Wah-wah-wah-waaah» de trombón con sordina de 3,5 s y bucle tristón y gracioso, Re menor, 72 BPM |
| **Eliminado** | Al quedar eliminado con la partida en marcha | Jingle de 2 s sin bucle (silbato de émbolo que se desploma, «¡toc!», «uh-oh» de kalimba y un «ploc») |
| **Marcha de la Playa** | De fondo en la carrera de la playa, solo para el jugador local | Sintetizada en C++ en tiempo real (`Private/Audio/TN_RaceMusicDSP.h`, `UTN_RaceMusicComponent`): Mi bemol mayor, 116 BPM, 32 compases (66 s) con relevo de solistas, así que el bucle dura 132 s sin costura. Banda militar de vacaciones: bajo «oom-pah», caja de marcha, congas, shaker, marimbas, steel pan, flauta de banda y corneta |
| **Menú principal** | En `LVL_Menu` | Asset `Content/Audio/Music/TotugasLobbyMusica`, reproducido por el Blueprint del nivel (`PlaySound2D`). Categoría a la que responde: **no confirmado** |

- **Director** (`TNMatchMusic::FDirector`, `Private/Audio/TN_MatchMusicDirector.h`, con `UTN_MatchMusicSubsystem`): decide la pista a partir
  del estado replicado (flujo, jugador local, equipo, rondas ganadas); sin RPC. Solo actúa en un mundo donde ha visto la carrera
  `InProgress`; el lobby no suena. Espera 0,35 s a decidir el resultado, lo reevalúa hasta 1,6 s y luego lo fija; el último segundo de la
  cuenta atrás funde a silencio. En la
  carrera, con campeón, solo él gana y una media concha suena a ronda ganada.
- **Música de fondo de la carrera («Marcha de la Playa»)** (ronda 4, [`Docs/Sonido_Tortuga.md`](Sonido_Tortuga.md)). Categoría **Música**,
  por debajo de efectos, voces y avisos: RMS medido de −29 dBFS con el volumen a 1 (el deslizador solo puede bajarla; `TN.Race.Music.Volume`
  la ajusta aparte). Tiene una capa de **tensión** (0 a 1: redoble de caja, bajo a corcheas, timbal, shaker, ostinato de kalimba, metales y
  hasta un 6 % más de tempo; sube en 2 s y baja en 3 s) y un **«ducking»** (hasta −10 dB y un paso bajo de 16 a 2,2 kHz; entra en 0,3 s y
  sale en 0,9 s). `UTN_RaceMusicSubsystem` decide diez veces por segundo, sin RPC y sin tocar el GameMode, leyendo `RacePhase`,
  `bSprintFinal`, `PhaseSecondsLeft`, `FinishCountdown` y `GetRoundTimeLeft()` del GameState de la carrera. El cooperativo sigue sin música
  de fondo (solo ambiente).

| Momento | Música | Tensión | «Ducking» |
|---|---|---|---|
| Preparando la ronda (`Waiting` sin cuenta; la primera ronda tras el viaje sale del huevo de la pantalla de carga) | Callada | | |
| Cuenta 3-2-1 de salida | Entra (fundido de 1,5 s, con dos compases de introducción) | 0 (0,5 en el sprint final) | 0,55 |
| Carrera (`Racing`) | Suena entera; se abre al dar la salida | 0 | 0 |
| Último minuto del tiempo de la ronda | Igual | de 0,35 a 1 | 0 |
| Cuenta de 10 s tras la primera en el agua | Igual | de 0,7 a 1 | 0,5 (0,65 los últimos 3 s) |
| Sprint final | Igual | al menos 0,5 | 0 |
| «¡TIEMPO!» / «¡TODAS AL AGUA!» | Se calla (0,8 s) | | |
| Recuento, título del sprint y podio | Se calla (1 s): llevan su música de victoria o derrota | | |
| El jugador local ya ha llegado | Se calla (1 s): empieza su música de victoria | | |
| Suena la música de fin de partida del jugador | Se calla y espera 1,6 s tras su final | | |

  Archivos: `Private/Audio/TN_RaceMusicDSP.h` (motor y composición, C++ puro), `TN_RaceMusicDirector.h` (la lógica de «cuándo y cuánto»,
  probada fuera del motor), `UTN_RaceMusicComponent` (hereda de `UTN_MusicSynthComponent` solo para caer en la categoría Música) y
  `Tools/RaceMusic/` (arnés fuera del motor: `build.bat` compila `render.exe`, que escribe WAV para oírla y medirla).
- **Consola**: `TN.Music.Play Victoria|Derrota|Eliminado|Tienda|Probador|Silencio`, `TN.Music.MatchVolume` y, para la de la carrera, `TN.Race.Music.*` (§38).

### 9.4 Ambiente

`UTN_AmbientSoundscapeComponent` (en el PlayerController) y `UTN_AmbientSynthComponent`: capas por bioma (viento, oleaje, aves, cigarras,
grillos, ranas…) que cambian en degradado, tormenta, cuevas amortiguadas, y fuentes 3D en géiseres (siguen el chorro), cascadas y lava.
Fauna esporádica y con silencios para que no canse. Sustituible por sonidos reales con `TN_AmbienceDataAsset` (hoy no
hay ninguno). Consola: `TN.Ambience.Debug`, `TN.Ambience.Volume`.

En la **carrera** no hay generador procedural: suena la mezcla genérica (brisa y pájaros, sin oleaje ni agua corriente). Se recortó a
propósito lo que «se repite solo, sin que nadie lo provoque, y se oye desde lejos»: la balsa (solo a menos de 18 m), las burbujas del pulpo
(solo los dos más cercanos, a menos de 24 m), el viento y los pisotones de la
tormenta (quitados; su ruido es el paisaje sonoro) y las pulgas (0,3 y a 30 m) ([`Docs/Modo_Carrera.md`](Modo_Carrera.md), «Sonido de ambiente en la carrera»).

### 9.5 Efectos sintetizados

| Componente | Archivo | Qué |
|---|---|---|
| `UTN_TurtleFoleyComponent` | `Player/TN_TurtleFoleyComponent.*` | Pasos por superficie (arena, tierra, roca, madera, agua), salto, aterrizaje, jadeo, «plaf» y arrastre del panzazo, «tonc» del caparazón, guardar objeto. Fuente 3D: pleno hasta 3 m, caída hasta 27 m; la tortuga local, ×1,3 ([`Docs/Sonido_Tortuga.md`](Sonido_Tortuga.md)) |
| `UTN_StormCoughComponent` | `World/ProcMap/TN_StormCough.h` | Tos de cada tortuga dentro de la tormenta, con su voz propia; calla el jadeo |
| `UTN_DizzySynthComponent` | `Player/TN_DizzyBirdsComponent.h` | Pajaritos y cuerdas mareadas |
| `UTN_ScoreShellSynthComponent` | `Audio/TN_ScoreShellSynthComponent.h` | «¡Plin!» (campanitas de cristal según el tamaño de la concha) y el «pom» del contador |
| `UTN_EggSynthComponent` | `UI/Loading/TN_LoadingScreenSubsystem.h` | «Clac», crujidos, «¡pum!» y «fiuu» del huevo |
| `UTN_RaceCueSynthComponent` | `UI/Race/TN_RaceCueSynthComponent.h` | «Toc» de la cuenta, silbato, fanfarria y «pum» |
| `UTN_BeachSplashSynthComponent` | `World/Beach/TN_BeachSplashSynthComponent.h` | El «¡chof!» del chapuzón de meta |
| `UTN_BeachTrapSynthComponent`, `UTN_BeachEnemySynthComponent`, `UTN_BeachCritterSynthComponent`, `UTN_BeachMineSynthComponent`, `UTN_BeachSandWormSynthComponent`, `UTN_RaceItemSynthComponent` | `World/Beach/` | Trampas, enemigos, bichos, minas, el gusano de arena y los objetos de carrera |
| `UTN_SearchSynthComponent` | `World/ProcMap/TN_ProcSearchSpot.h` | Rebuscar en los decorados |
| `UTN_PlaygroundSynthComponent` | `Lobby/Playground/` | El patio de pruebas del lobby |
| `UTN_MusicSynthComponent`, `UTN_AmbientSynthComponent`, `UTN_RaceMusicComponent` | `Audio/` | Música (tienda, probador, fin de partida y fondo de la carrera) y ambiente |
| `UTN_ShellImpactSynthComponent` | `Audio/TN_ShellImpactSynth.h` | Golpes del caparazón (§9.7) |

**Sonidos de asset** (`Content/Audio/`): 10 sonidos de baile (`DanceSounds/0…9`, mp3), efectos de `Consume`, `Pickup`, `Kill`, `Throw`
(`SC_Throw` y tres `ThrowSound`), pisadas de arena y de camino de tierra del pack `FootstepsMiniPack`, y la música del menú. Según la doc,
los pasos sintetizados callan si el Blueprint asigna el `FootstepSound` de siempre (hoy vacío).

### 9.6 Sonido de la tortuga

**Todo está sintetizado en tiempo real, sin archivos de audio** (como el ambiente y la música), **salvo las canciones de los
emotes** (diez `SoundWave` de `/Game/Audio/DanceSounds/`). Los sonidos del propio personaje que el Blueprint podría asignar
(pasos, salto, derribo, muerte, coger, lanzar, consumir, entrar y salir del caparazón, latido, éxito de reanimación, tótem) están
**todos vacíos**. Fuente: [`Docs/Sonido_Tortuga.md`](Sonido_Tortuga.md) y el código de `Player/TN_TurtleFoley*`.

| Qué | Cuándo | Cómo |
|---|---|---|
| **Pasos** | Cada pisada de la animación; más rápidos y fuertes corriendo | Sintetizados por superficie (arena, tierra, roca, madera, agua); fuerza 0,35–0,66 andando y 0,88–1 corriendo; tono propio por tortuga (±8 %); pata derecha algo más aguda |
| **Impulso del salto** | Al despegar subiendo a más de 1,5 m/s | Sintetizado |
| **Aterrizaje** | Al caer tras más de 0,12 s en el aire o a más de 2,5 m/s | Fuerza 0,45–1,35 según la caída |
| **Jadeo** | Estamina por debajo del 45 %; a tope al agotarse; se calma en 2,5 s; suspira | Respiraciones «hah/hhh» con formantes y algo de voz; ~1 ciclo/s a ~2,4 al agotarse; la voz sale de la semilla del jugador |
| **«Plaf» del panzazo** | Al caer de tripa | Fuerza 0,5–1,4; golpe grave más un chasquido de carne blanda; en el agua, un chapuzón con burbujas |
| **Arrastre** | Mientras se desliza | Continuo, por superficie (siseo granulado en la arena, roce sordo en la tierra, raspado a trompicones en la roca, zumbido de tablón, estela de agua); con la fuerza de la velocidad |
| **«Tonc» del caparazón** | Al chocar arrastrándose (cambio de velocidad > 2,6 m/s, como mucho uno cada 0,25 s) | Golpe sordo del cuerpo y modos de tablón (270–340 Hz) |
| **«Toc» de guardar y sacar** | Al cambiar de ranura | Hueco y corto: 330–380 Hz al guardar, 440–520 Hz al sacar |
| **Golpes de la bola** | Al chocar en bola contra algo a más de 260 cm/s | Sonido y mini efecto por material, sin red (§9.7) |
| **Pajaritos y cuerdas mareadas** | Derribada o aturdida | `UTN_DizzySynthComponent` |
| **Tos** | Dentro de la tormenta del cooperativo | `UTN_StormCoughComponent`; el jadeo calla mientras tose |
| **Emote** | Mientras dura | La canción `DanceSounds/<ID>` en bucle, 3D |
| **Conchas de puntos** | Al cogerlas | «Pom» que sube (2D) y «¡plin!» del estallido |
| **Objetos de carrera** | Al usarlos | 21 sonidos sintetizados (turbo, dorado, protector, «nop», mecha de la mina…) |
| **Voz de los jugadores** | Al hablar | Proximidad, 3D, mu-law a 16 kHz (§26) |
| **Chapuzón de la meta** | Al entrar en el agua de la meta | «¡Chof!» sintetizado (gotas, burbujas y cavidad) |

Sin sonido hoy: nadar, meterse y salir del caparazón, los pasos con `FootstepSound` vacío del Blueprint (no
suenan: los sintetizados los sustituyen; si el Blueprint asignara uno, los sintetizados callarían para no doblarse).

**Fuente 3D de la tortuga**: mono en la raíz; volumen pleno hasta 3 m (`InnerRadius` 300), caída natural hasta 27 m (`FalloffDistance`
2400) y agudos que se apagan con la distancia. La tortuga local suena ×1,3 (`LocalPlayerBoost`). El sintetizador arranca con la primera
pisada o el primer jadeo y se para tras 2,5 s sin nada; lejos del oyente pasa a 4 Hz. No hay nada en el servidor dedicado.

**Niveles medidos en el arnés** (Master 1): paso andando −16 a −19 dBFS (madera y agua 1–2 dB más); corriendo −11 a −14; aterrizaje
fuerte −6 a −10; impulso del salto −19 a −23; jadeo flojo / fuerte / agotada −22 / −16 / −13 dBFS. Limitador de salida a −2 dBFS.
Todo queda por debajo de la tos (−7 a −1,5 dBFS). El panzazo aún no pasó por el arnés: el arrastre está estimado a ~−28 dBFS eficaces.

**Ajustes y consola**: `Loudness`, `StepLoudness`, `BreathLoudness`, `LocalPlayerBoost`, `PantBelowStamina` 0,45, `PantCalmSeconds` 2,5,
`MinStepSpeed` 60, `DragLoudness`, `MinDragSpeed` 25, `DragFullSpeed` 650; `TN.Voice.Volume`, `TN.Voice.Surface -1..4`,
`TN.Voice.Steps 0|1|2`, `TN.Voice.Pant 0|1|2`, `TN.Voice.Drag 0|1|2`, `TN.Voice.Debug 1`, `TN.Storm.Cough 0|1|2`.

### 9.7 Golpes del caparazón

Con la tortuga en bola (`ATN_ShellBody`, la caja de física; §16), cada choque suena y levanta un mini efecto según contra qué choca, con
la fuerza que da la velocidad del impacto ([`Docs/Sonido_Tortuga.md`](Sonido_Tortuga.md), «Golpes del caparazón»). **Sin red**: cada máquina lo detecta en su
copia de la bola (los clientes también simulan la caja y la réplica la corrige), así que todos los jugadores cercanos lo ven y oyen sin un
byte más; un multicast habría costado hasta 8 mensajes por segundo y bola con 8 jugadores. El servidor dedicado no crea el componente.

- **Cómo se engancha**: `UTN_ShellImpactFXComponent` (`Player/TN_ShellImpactFXComponent`) lo crea `ATortugaCharacter::BeginPlay` con
  `FindOrAddTo`. Cada fotograma mira `UTN_ShellComponent::GetBody()`; cuando aparece una caja activa en ella «Simulation Generates Hit
  Events» y se apunta a su `OnComponentHit`. No toca `TN_ShellBody.*` ni `TN_ShellComponent.*`.
- **Velocidad del impacto**: el mayor de (impulso normal del motor / masa de la caja) y de lo que la bola llevaba contra la superficie al
  empezar el fotograma. Por debajo de `MinImpactSpeed` (**260 cm/s**) no pasa nada, así que rodar y los botes pequeños no suenan; a
  `FullImpactSpeed` (**1800 cm/s**) es el golpe máximo. La fuerza va de 0,1 a 1 con una curva suave.
- **Separación**: 0,07 s entre contactos (el motor manda varios por golpe) y `MinInterval` (0,14 s) entre golpes de una bola, que sube
  hasta 0,42 s si el golpe es flojo salvo que llegue uno claramente más fuerte. Como mucho **8 sonidos por segundo entre todas las bolas
  del mundo** (después solo pasan los fuertes, hasta 12): es la lección del «pum, pum, pum» de los bañistas, nada suena solo, en bucle ni
  sin fuerza. Bola contra bola: los dos avisan, suena uno. El sonido es pleno hasta 4 m y se apaga hacia los 36 m; no se crean partículas
  a más de 60 m de la cámara local.
- **Categoría de volumen**: Efectos (§9.2).

| Timbre | Cuándo | Síntesis |
|---|---|---|
| Otra tortuga | Otra bola o una tortuga de pie | «¡Clonc!»: caparazón contra caparazón, tres modos huecos (~310, 610 y 985 Hz) |
| Enemigo | `ATN_BeachEnemy` y sus hijos | «¡Boing!»: tono que cae con vaivén de goma y un fogonazo de ruido |
| Arena | El terreno de la playa y los castillos y fortalezas de arena | «¡Fum!»: golpe grave que cae de tono y un soplo apagado |
| Roca | El acantilado y cualquier talud con la normal por debajo de 0,55 | «¡Tac!»: chasquido seco y tres resonancias de piedra (~1,2, 2,4 y 3,3 kHz) |
| Madera | El bosquecillo, las catapultas, las plataformas, los cofres y las puertas de conchas | «¡Toc!» hueco de tablón (modos sobre ~235 Hz) |
| Trastos de los bañistas | El resto de elementos de la playa (cubos, palas, alambre, trampolines, minas), el decorado y los objetos de carrera | «¡Pok!» de plástico y lata (~640, 1500 y 2600 Hz) |
| Agua | Entrar en un volumen de agua por encima de la velocidad mínima (la caja sale del caparazón al agua) | «¡Plof!»: chapoteo, burbuja que sube de tono y, con fuerza, hasta 4 gotas agudas |

Fuera de la playa (mapa procedural, lobby) usa `TNTurtleSurface::Resolve`, la misma superficie que los pasos y el polvo. **Efecto visual**:
dos emisores de partículas por timbre (`TNAmbientFX`, sin Niagara): la nube y los trocitos (granos de arena, esquirlas de roca, astillas,
gotas, chispas amarillas para la tortuga y naranjas para el enemigo, confeti de plástico); hasta 14 por tortuga, creados la primera vez
que hacen falta. Ajustes del componente: `MinImpactSpeed`, `FullImpactSpeed`, `MinInterval`, `FXAmount`, `SoundLoudness` y
`MaxViewDistance` (6000 cm). Consola: `TN.Shell.Impact 0|1`, `TN.Shell.Impact.Debug 1`, `TN.Shell.Impact.Volume`,
`TN.Shell.Impact.MinSpeed` y `TN.Shell.Impact.Test <timbre|todos> [fuerza]` (§38). Los timbres se pueden oír sin abrir el editor con
`Tools/RaceMusic/build.bat` (`shell.exe`).

### 9.8 Voz

La voz de proximidad (captura, compresión, alcance, ajustes y límites) está en el §26. Se oye en 3D atenuada por distancia, con volumen
por compañero, silencio individual y la clase de sonido `TN_Voice` (§9.2).

## 10. Localización

Detalle, flujo y reglas de escritura en [Docs/Localizacion.md](Localizacion.md). Aquí, lo que hay en el commit `af3f7229` y lo que
importa para jugar y para tocar textos.

### 10.1 Cómo funciona el idioma

- **Idioma de origen**: español de España (`es-ES`), con tono cercano y humor propio (los textos de la sala, los premios y las frases de
  ánimo están escritos con gracia en español; al traducir se **adapta el humor**, no se traduce literal). «Tortunavy» no se traduce.
- **Canal del motor**: los textos visibles son `FText` (`NSLOCTEXT`, unos 1000 usos en el código; ningún `LOCTEXT`), en espacios de
  nombres como `Tortunabo`, `TNPause`, `TNSettings`, `TNKeys`, `TNRooms`, `TNRoomNames`, `TNHUD`, `TNRace`, `TNBeach`, `TNGhost` y
  `TNLoading`. Se recogen con el objetivo de localización «Game» (`Config/Localization/Game_*.ini`, origen es-ES), se exportan a `.po`,
  se traducen, se importan y se compilan a `.locres` (`Scripts/localization_gather_export.bat`, `Scripts/localization_import_compile.bat`).
- **El selector** es la primera fila de Ajustes > Juego (§7.7), llamada «Idioma / Language» en todos los idiomas para que se encuentre
  aunque no se lea el idioma puesto. Muestra los **13 idiomas** con su nombre escrito en su propio idioma. La lista es
  `UTN_LanguageSettings` (`Settings/TN_LanguageSettings.*`, editable en `Config/DefaultGame.ini` o en Ajustes del proyecto sin tocar
  código) y se puede ampliar con una línea (`+Languages=(Culture="ar")`).

| Cultura | Nombre en el ajuste | Cultura | Nombre en el ajuste |
|---|---|---|---|
| `es-ES` | Español (España), origen | `pl` | Polski |
| `en` | English | `tr` | Türkçe |
| `fr` | Français | `ja` | 日本語 |
| `de` | Deutsch | `ko` | 한국어 |
| `it` | Italiano | `zh-Hans` | 简体中文 |
| `pt-BR` | Português (Brasil) | `zh-Hant` | 繁體中文 (Taiwán) |
| `ru` | Русский | | |

- **Qué idioma se usa**: el guardado en `FTNGameSettings::Language` (`TN_Settings.sav`, versión 3). Sin elegir, el **idioma del sistema**
  si está en la lista (es-MX pasa a es-ES, pt-PT a pt-BR, en-GB a en, zh-TW, zh-HK y zh-MO a zh-Hant, zh-CN a zh-Hans) y, si no, el
  español. Se aplica al crearse la `GameInstance`, antes de que salga ningún menú. «Restablecer esta pestaña» y «Restablecer todos los
  ajustes» lo dejan en «sin elegir».
- **Cambio en caliente** (`TNLanguage::Apply`): en el juego, `SetCurrentCulture` sin guardarla en `GameUserSettings.ini` (manda el
  ajuste propio, para no tener dos fuentes de verdad); en el editor, solo los textos del juego, con la previsualización del idioma del
  juego, y únicamente cuando ya existe `Content/Localization/Game/Game.locmeta`. `-culture=xx` o `-language=xx` mandan sobre el ajuste;
  `-culture=LEET` pone en «leet» todo lo localizable (lo que se ve normal no lo es) y `-culture=keys` enseña la clave de cada texto.
- **Voz y audio**: no hay voces grabadas ni diálogos hablados, así que no hay doblaje ni subtítulos que traducir (§8.3). La voz de los
  jugadores no depende del idioma.

### 10.2 Nombres de sala y avisos de las salas

Los **242 nombres de sala** (`Multiplayer/TN_RoomNames.*`) son texto origen en español en el canal de localización
(`NSLOCTEXT("TNRoomNames", "Room_000", …)`, una clave estable por índice; el orden de la tabla no cambia nunca). La sesión anuncia solo el
**índice** y cada jugador lo lee en el idioma que ha elegido **en el juego**, no en el de Windows ni en el del motor (antes
`TNRoomNames::IsSpanish()` miraba la cultura del motor y en el editor salía en inglés aunque el sistema estuviera en español; era el
fallo 2 de la ronda 4). Sin traducción, el nombre sale en español. Las adaptaciones inglesas de antes (no traducciones: mismo humor) están
en `Tools/Localization/room_names_en.csv`, listas para meterse como traducción al inglés; cada nombre tiene como mucho **28 caracteres**
y no se repite dentro de un idioma. Los avisos de rechazo de las salas (`TNRoomText::RefusedMessage`: expulsado, cerrada, llena) son
`NSLOCTEXT` del espacio `TNRooms` y siguen el mismo idioma (§3.4).

### 10.3 Fuentes

Todos los widgets de código sacan la fuente de `TNHUDStyle::Font`, que usa `TNHUDFonts::Make` (`Private/UI/HUD/TN_HUDFonts.*`): la
fuente compuesta del motor (Roboto para latín, latín extendido y cirílico; Droid Sans Fallback como reserva de último recurso) más, por
idioma de la lista, una **fuente de reserva propia si su archivo está en `Content/Slate/Fonts`** (Noto Sans JP, KR, SC y TC, Regular y
Bold, previstas y **no incluidas** en el repositorio). Sin ellas, el japonés, el coreano y el chino se ven, pero con la reserva del
motor: un solo peso (las negritas salen finas), un único diseño de hanzi y trazo pobre a tamaños pequeños. Roboto no tiene la ẞ
mayúscula (evitarla). La pantalla de carga del huevo y el «¡PUM!» del huevo fantasma usan la fuente del motor directamente.
Comprobación de cobertura: `Scripts/tools/check_font_coverage.py`.

### 10.4 Lo que hay que tener en cuenta al traducir

- Las teclas se leen del `IMC_Player` real y se nombran en `TN_GameSettingsSubsystem.cpp` (`KeyDisplayName`, espacio `TNKeys`).
- Los rótulos pintados o modelados en código («¡ADELANTE!», «TORTUNAVY», «CUARTEL GENERAL», «¡A LA META!», «PROBADOR», la pizarra del
  general, «Has quedado X.º» y los premios) deben pasar por `FText`; los carteles 3D se repintan al cambiar el idioma
  (`TNLanguage::OnApplied`).
- Los textos largos (alemán, ruso, polaco) tienen que caber en los rótulos de una línea (52 px a 19-20 pt): se abrevia antes que romper.
- Las frases del chat rápido son para **oír hablar** a la tortuga: breves (como mucho cuatro palabras).
- Los ordinales de la llegada («1.º») se forman con `FText::Format("{0}.º", puesto)`; en otros idiomas, con su ordinal.
- **En curso (sin subir)**: la **fase 2** de la localización —auditar y pasar a `FText` los `FText::FromString` y `Printf` de texto visible
  que quedaban (unos 100 usos de `FromString` en 35 archivos en `af3f7229`, muchos de ellos nombres de jugador, que se marcan como dato), las
  ayudas `TNLocText` y el botón «Ajustes» del menú principal— está en el árbol de trabajo. Hasta que se suba, los textos del HUD del
  flujo de la partida, la pantalla de carga y los avisos de interacción siguen mezclando `FText` y cadenas sueltas.

### 10.5 Localización: estado de las traducciones

Pendiente de completar.

## 11. Tutorial de primera partida

Pendiente de completar.

*Lo ha hecho otro agente y está en [`Docs/Tutorial.md`](Tutorial.md), sin subir a git (punto 14 de `Docs/Plan_Carrera_Ronda4.md` (eliminado): pasillo de tutorial en
el cielo, por jugador, con cascada final que deja caer al lobby). Este apartado se completará a partir de ese documento; mientras, lo que existe
y hace de tutorial en `af3f7229` es lo que cuenta el §2.7.*

# Parte II — La tortuga, sus mecánicas, los objetos, la puntuación y los cosméticos

## 12. La tortuga de un vistazo

Es una tortuga marina antropomórfica en tercera persona, de 1 a 8 jugadores por red (servidor escucha), que anda, esprinta
gastando una barra de energía, salta, se tira en plancha en el aire, se mete en el caparazón y rueda como una bola
física, nada, coge y lanza a sus compañeros, lleva dos objetos y hace emotes. Casi todo es **servidor-autoritativo**; el
cliente dueño predice el movimiento (`UTN_TurtleMovementComponent`) y el resto lo ve replicado.

| Dato | Valor | Origen |
|---|---|---|
| Malla | `TotugaDemo_Rig` (esqueleto Mixamo), 2 ranuras de material (`lambert2` casco y lengua; `lambert4` cuerpo, ojos y caparazón) | [BP], [`Docs/Tienda_Probador.md`](Tienda_Probador.md) |
| Escala de la malla | 2,5, con Z relativa −70 y giro de −90° | [BP] |
| Alto aproximado | 53 unidades de malla × 2,5 ≈ 132 cm | [D] + [calc] |
| Cápsula | radio 34, semialtura 88 (la de `ACharacter`: ni el C++ ni el Blueprint la tocan); tumbada en el panzazo, semialtura 35 | [C] `DiveCapsuleHalfHeight`, [D] |
| Animación | `UTN_TurtleAnimInstance` (C++, sin AnimBP; el `ABP_Normal` del Blueprint se sustituye en `BeginPlay`) | [C] `TortugaCharacter.cpp` |
| Actualización de red | 30 Hz (10 Hz como mínimo), **siempre relevante** para todos los clientes | [C] |
| Jugadores | hasta 8 | [D] [`Docs/Modo_Carrera.md`](Modo_Carrera.md) |
| Escala del mundo en la playa de la carrera | la tortuga es una cría de ~5 cm; el mundo va ×28 (`TNBeach::Scale`) | [D] |

**Piezas del personaje** (todas en `Player/` salvo indicación):

| Pieza | Archivo | Qué hace |
|---|---|---|
| `ATortugaCharacter` | `Player/TortugaCharacter` + `TortugaCharacter_Cosmetics/_Dive/_Emote/_Interaction/_Knockdown/_Revive.cpp` | El personaje: entrada, cámara, salto, panzazo, derribo, muerte visual, emotes, revive, uso de objetos |
| `UTN_TurtleMovementComponent` | `Player/TN_TurtleMovementComponent` | Sustituye al movimiento de serie: arrastre del panzazo predicho, cuerpo tumbado que no atraviesa paredes |
| `UTN_StaminaComponent` | `Player/TN_StaminaComponent` | Estamina, velocidad de andar y correr, topes de velocidad, multiplicador de la carrera |
| `UTN_ShellComponent` + `ATN_ShellBody` | `Player/TN_ShellComponent`, `Player/TN_ShellBody`, `Player/TN_ShellDecisions.h` | Estado de caparazón y la caja física en la que se convierte |
| `UTN_CarryComponent` | `Player/TN_CarryComponent` | Coger, llevar, lanzar y soltar a otra tortuga; forcejeo de 2 s |
| `UTN_InventoryComponent` | `Player/TN_InventoryComponent`, `Player/TN_InventoryDecisions.h` | Dos ranuras replicadas; objeto en las aletas; guardar y sacar del caparazón |
| `UTN_TurtleAnimInstance` | `Player/TN_TurtleAnimInstance` | Locomoción con clips, poses de estado, aletas, celebraciones del podio, zambullida |
| `UTN_TurtleFaceComponent` | `Player/TN_TurtleFaceComponent` | Lengua con física, caras de cansancio, sudor, boca que habla |
| `UTN_TurtleFoleyComponent` | `Player/TN_TurtleFoleyComponent`, `Player/TN_TurtleFoleyDSP.h` | Pasos, salto, aterrizaje, jadeo, panzazo, arrastre, «toc» del caparazón (sintetizado) |
| `UTN_TurtleDustComponent` | `Player/TN_TurtleDustComponent` | Polvo del panzazo según la superficie |
| `UTN_ShellImpactFXComponent` | `Player/TN_ShellImpactFXComponent` | Sonido y mini efecto de los golpes de la bola del caparazón (ronda 4; §9.7) |
| `UTN_DizzyBirdsComponent` | `Player/TN_DizzyBirdsComponent` | Pajaritos y estrellitas del mareo con su sonido |
| `UProximityVoiceComponent` | `Voice/ProximityVoiceComponent` | Voz de proximidad |
| `UTN_RaceItemComponent`, `UTN_BeachStunComponent` | `World/Beach/` | Efectos de los objetos de carrera y aturdimiento de la carrera (los añade el servidor cuando hacen falta) |
| `ATN_SpectatorGhost`, `TNGhost` | `Player/TN_SpectatorGhost`, `Player/TN_Ghost` | Fantasma espectador y contrato de volver a la vida desde un huevo |
| `UTN_CosmeticLook` | `Core/TN_CosmeticLook` | Viste a la tortuga con un `FTN_TurtleLook` (casco, caparazón, color, ojos) |

## 13. Valores del código y del Blueprint

`BP_TortugaCharacter` (el peón por defecto de **todos** los modos: el lobby `BP_HQGameMode`, el clásico `BP_RunGameMode`, y el mapa
procedural y la carrera de la playa, que lo cargan por ruta en `TN_ProcMapGameMode.cpp` y `TN_BeachRaceGameMode.cpp`) guarda valores que
pisan al código. Se leyeron del `.uasset` de HEAD (guardado por última vez el 22-09-2026, commit `bc760ed7`; el 17-09 hubo otro llamado
«métricas cambiadas»).

| Parámetro | Código [C] | Blueprint [BP] (el que rige) | Dónde |
|---|---|---|---|
| Velocidad de andar (`WalkSpeed`) | **450** cm/s | **200** cm/s | `Player/TN_StaminaComponent.h` |
| Velocidad de correr (`SprintSpeed`) | **800** cm/s | **400** cm/s | ídem |
| Velocidad de salto (`JumpZVelocity`) | (motor) | **485** cm/s | componente de movimiento |
| Panzazo: velocidad horizontal base (`DiveForwardSpeed`) | 420 | **350** | `Player/TortugaCharacter.h` |
| Panzazo: velocidad hacia abajo (`DiveDownwardSpeed`) | 200 | **100** | ídem |
| Dash aéreo horizontal / vertical (sin uso, ver §14.2) | 1400 / 300 | 200 / 500 | ídem |
| Brazo de la cámara en reposo / corriendo | 170 / 240 | **250 / 300** | ídem |
| Sensibilidad del ratón X / Y | 1,0 / 0,5 | **0,7 / 0,3** | ídem |
| Sonidos de emote | vacío | 10 canciones `/Game/Audio/DanceSounds/0…9` | ídem |
| Catálogo de emotes | — | `DA_EmoteWheelCatalog` | ídem |
| Sonidos de pasos, salto, derribo, muerte, coger, lanzar, consumir | vacíos | **vacíos** (no hay ninguno asignado) | ídem |

**Por qué importa, y confirmado con los registros de juego.** Los números con los que se diseñó la carrera de la playa
(«andar a 4,5 m/s y esprintar a 8 m/s», [`Docs/Modo_Carrera.md`](Modo_Carrera.md), [`Docs/Animacion_Tortuga.md`](Animacion_Tortuga.md), el comentario de
`TN_BeachStorm.h`) son los del **código**. Pero el Blueprint fija **2 m/s y 4 m/s**, y **eso es lo que
se juega**:

1. **Los registros de las pruebas del 28-09** (`Saved/Logs/Tortunabo*.log`): cada salto escribe
   `[Jump] … captured horizontal velocity=… (speed=N)` con la velocidad horizontal al despegar. De **190 saltos**, **141 salen a
   400 cm/s exactos** (corriendo), **22 a 200** (andando), 25 en valores intermedios (acelerando: 268, 302…) y 2 justo por debajo de 400;
   **ninguno pasa de 400**. Están en el lobby y en `LVL_BeachRace` (p. ej. `Tortunabo-backup-2026.09.28-22.45.59.log`: los saltos de
   después de cargar la playa son de 400 y 200).
2. El generador del mapa procedural usa exactamente los del Blueprint (`TNProcMap::TurtleJump` en `World/ProcMap/TN_ProcMapLayout.h`:
   `JumpZ 485`, `Gravity 980`, `WalkSpeed 200`, `SprintSpeed 400`, `CapsuleRadius 34`), y `ATN_ProcMapGameMode::GetTurtleWalkSpeed`
   acota la tormenta a la velocidad de andar de la tortuga real (el perfil Difícil, 200 cm/s, coincide con andar).
3. [`Docs/Traspaso_Sesion_Cloud.md`](Traspaso_Sesion_Cloud.md) ya dice que «en el lobby es 200 cm/s; en C++ vale 450 y el sprint 800».
4. Las pruebas del 28-09 con una tortuga agotaban el límite de ronda de la carrera en todas las rondas ([`Docs/Modo_Carrera.md`](Modo_Carrera.md),
   «¡TIEMPO! sin nadie en el agua»): 800 m a la media de ~2,8 m/s (60 % andando a 2 y 40 % esprintando a 4) son **≈ 4 min 46 s sin
   contar trampas ni enemigos** [calc], no los 3 min 20 s del diseño.

**Consecuencias de que mande el Blueprint** [calc] (las cifras de otros documentos que dependen de la velocidad están
recalculadas; entre paréntesis, lo que dicen los documentos):

| Cosa | Con el Blueprint | Con el código |
|---|---|---|
| Altura máxima del salto | 485² / (2·980) = **120 cm** | — |
| Tiempo en el aire de un salto en llano | 2·485 / 980 = **0,99 s** | — |
| Alcance de un salto en llano, andando / corriendo / con turbo | **198 cm** / **396 cm** / 792 cm | 2·485/980·450 = 445 cm / 792 cm |
| Turbo de coco (`max(base, correr) × 2`) | **800 cm/s** | 1600 cm/s (lo que dice el documento) |
| Protector solar (`max(base, correr) × 1,25`) | **500 cm/s** | 1000 cm/s |
| Turbo + protector (tope 2,4×) | **960 cm/s** | 1920 cm/s |
| Metros por barra de estamina corriendo (13,3 s) | **53 m** | 107 m |
| Gaviota justiciera (el blanco sigue a 250 cm/s y, desde soltarla, cae por su línea; #636) | corriendo (400) **se libra** en línea recta (6,2 m al golpe); andando, solo con la plancha en el momento justo | corriendo (800) se libra |
| Gaviotas de la zona (el blanco sigue a 250 cm/s; los últimos 1,5 s, lanzado por su línea; #636) | corriendo (400) se le gana 1,5 m/s a la sombra y en línea recta se libra (3,8 m el picado, 4,3 m la cagada); andando, libran la plancha en el momento justo, el caparazón (picado) y el techo. Ver el §36.7 | corriendo (800) se libra en línea recta |
| Cangrejo gigante (persigue a 560 cm/s) | más rápido que correr (400): solo se le escapa saliendo de su correa (38 m) o de su vista, o con turbo (800) | más lento que correr (800) |
| Erizo de mar (rueda a 210 cm/s) | algo más rápido que andar (200) | más lento que andar (450) |
| Tormenta de bañistas Normal (180 cm/s) | 90 % de la velocidad de andar | 40 % |
| Carga de un compañero (tope 330 cm/s) | entre andar (200) y correr (400): al correr se frena a 330 | más lento que andar (450): al cargar se frena incluso andando |

Ojo con lo que **no** cambia: el arrastre del panzazo (su entrada máxima, 850 cm/s, es independiente de correr), el tope de
carga (330), el de mareo (250), el nado (625), la caja del caparazón y todo lo que va en cm/s dentro de los objetos
(minas, discos, cangrejos…). El resto de este documento da los dos valores cuando dependen de la velocidad.

## 14. Movimiento

### 14.1 Andar, correr y girar

- **Entrada** (`ATortugaCharacter::Move`): dos ejes relativos a la cámara (giro horizontal del mando). Con
  `bOrientRotationToMovement` y `RotationRate` de 360°/s [C], el cuerpo gira hacia donde avanza; la cámara no lo arrastra.
- **Velocidad** (`UTN_StaminaComponent::ApplyMovementSpeed`, **el único sitio que escribe `MaxWalkSpeed`**):
  1. base = correr si `bIsSprinting`, si no andar;
  2. × 0,75 durante la penalización posterior a la energía sin fin (`PostBoostSpeedMultiplier`);
  3. si hay multiplicador de la carrera > 1 (coco, protector): `max(base, correr) × multiplicador` (el turbo funciona **aunque no
     se esprinte**);
  4. `min(resultado, tope)` con el tope activo (`SetSpeedCap`).
- **Correr** (`RefreshSprintRequest`): mantener Mayús con entrada de movimiento mayor que 0,25 (en cualquier dirección: lateral y
  diagonal valen) y **estamina > 0**. Se corta al soltar el stick, al meterse en el caparazón, al derribarla o al morir.
  `StartSprint` no hace nada en pleno panzazo.
- **Topes de velocidad** (`SetSpeedCap(quién, tope)`, se toma el menor): cada sistema pone y quita **el suyo**
  (`TN_MovementLimits.h`, tests `Tortunabo.Movement.Limits`); acabar el mareo ya no quita el tope de llevar a otra. El salto
  (`SetJumpLimit`: sirope, agua, alga) y la gravedad del sirope (`SetGravityScaleOverride`) van igual: la base se guarda con el
  primer límite y vuelve al quitar el último, en cualquier orden.

| Tope | Valor | Cuándo | Origen |
|---|---|---|---|
| Caparazón | 0 | metida en el caparazón (el freno entra por aquí) | [C] `TN_ShellComponent.cpp` |
| Llevando a alguien | 330 cm/s | mientras carga a un compañero | [C] `CarrySpeedCap` |
| Mareo de la cabezota | 250 cm/s | 3 s al acabarse (o perder) la cabezota | [C] `MareoSpeedCap`, `MareoDurationSeconds` |
| Zona lenta | el de la zona | dentro de un `TN_SlowZoneVolume` | [D] `Docs/Inventario_Scripts.md` (eliminado) |
| Algas, etc. | el suyo | trampas de la playa (§35) | [D] |

- **Qué bloquea moverse**: derribada (`Move` sale), los 0,75 s de levantarse del derribo (`GetUpLockUntil`), llevada por otra
  tortuga (moverse es forcejear, §19), en pleno panzazo (salvo reptando o casi parada, §17), aturdida, en un
  agarre de un enemigo o del pelícano, atrapada por una concha trampa (`DisableMovement` 2,5 s).
- **Moverse cancela el emote en curso**, salvo el 5 y el 6, que se pueden hacer andando.

### 14.2 Saltar

- **Salto** (`ATortugaCharacter::Jump`): `JumpZVelocity` 485 [BP], gravedad 980 [calc, `TurtleJump::Gravity`]: **120 cm de altura y
  0,99 s en el aire**. Pulsar y soltar no cambia la altura (no hay salto variable). No se puede saltar derribada, muerta,
  metida en el caparazón, llevada por otra tortuga ni durante los 0,75 s de levantarse del derribo.
- **No hay doble salto.** La segunda pulsación de salto **en el aire** es la plancha (§17). El «dash aéreo»
  (`PerformAirDashLocally`, `ServerPerformAirDash`, `bCanAirDash`) sigue en el código, con valores en el Blueprint, pero
  **nada lo llama**: es código muerto.
- **Casi parada sobre la tripa**, el salto es un brinco que levanta a la tortuga; deprisa no hace nada (§17).
- **Nadando**, saltar es el salto del agua (§18).

**Caídas.**

| Regla | Valor | Qué pasa | Origen |
|---|---|---|---|
| Caída libre que la mete en el caparazón | > 500 cm (5 m) | Solo el servidor. Se hace bola con física y sale sola al pararse. No si ya lleva a alguien o la llevan a ella | [C] `AutoShellFallHeight` |
| Caída que la rompe | > 3500 cm (35 m) al aterrizar | `RequestKill`: en el cooperativo, muerte; en la carrera, aturdimiento ([`Docs/Modo_Carrera.md`](Modo_Carrera.md)) | [C] `FatalFallHeight` |
| Caída «inmune» | hasta aterrizar o entrar al agua | Géiseres, toboganes, palas, catapultas, trampolines, el salto del acantilado de la meta, el pelícano: ni bola ni muerte (`SetFallImmuneUntilLanded`) | [C] |
| El agua | — | Amortigua cualquier caída y termina los vuelos de lanzamiento | [C] `OnMovementModeChanged` |

### 14.3 Cámara

Tercera persona con brazo de resorte (`ATortugaCharacter`, `TickCameraInterp`, solo en el jugador local):

| Ajuste | Valor | Origen |
|---|---|---|
| Longitud del brazo, reposo / corriendo | 250 / 300 (170 / 240 en código), interpolada a 6/s | [BP] / [C] |
| Campo de visión, reposo / corriendo | 72° / 82°, interpolado a 5/s (el menú de pausa lo cambia) | [C] |
| Desplazamiento del hombro | (0, 80, 70) | [C] `CameraSocketOffset` |
| Pivote del brazo | 55 cm sobre el centro | [C] `CameraBoomRelativeOffset` |
| Inclinación de la cámara respecto del mando | −14° (el mando apunta 14° por encima de lo que se ve) | [C] `CameraAimPitchOffset` |
| Retraso de posición / de giro | 14 / 20 (velocidades del resorte) | [C] |
| Colisión | sonda de 30 cm por el canal de cámara; no choca con otras tortugas ni con cajas de caparazón | [C] |
| Suelo | si la cámara queda a menos de 40 cm del suelo, sube (interpolación a 10/s) | [C] `TickCameraInterp` |
| Turbo del coco | empujón de +16° de campo de visión a velocidad 40 | [C] `TN_RaceItemComponent.cpp` |
| Ojo de pez leve | Proyección Panini del motor (`r.LensDistortion.Panini.D` = 0,55 con todo encendido; `TN.Fisheye.D`/`.S`). Se enciende y apaga en 1,2 s y se afloja con el campo de visión (al correr, 82°, baja hasta ~0,35) para que el borde se comprima igual que en reposo. No toca el HUD, que se pinta después. Ajuste «Ojo de pez leve» de la pestaña Juego, desactivado por defecto (`bFisheye`, #634) | [C] `TN_GameSettingsSubsystem.cpp` |

El espectador tiene su propia cámara libre o fija (§22.4).

## 15. Estamina

`UTN_StaminaComponent` (`Player/TN_StaminaComponent`), autoritativo en el servidor.

| Parámetro | Valor | Origen |
|---|---|---|
| Estamina máxima (`MaxStamina`) | **200** | [C] |
| Gasto al correr (`SprintDrainPerSecond`) | 15 por segundo | [C] |
| Espera antes de recargar (`RechargeDelaySeconds`) | 0,8 s desde que se deja de correr | [C] |
| Recarga (`RechargeBasePerSecond`, `RechargeExponentGrowth`) | `6 · e^(1,1·t)` por segundo, con `t` = segundos recargando (empieza suave y acelera) | [C] |
| Penalización por agotarse (`ExhaustionPenaltySeconds`) | 1 s sin recargar, además de la espera | [C] |
| Peso (`StaminaPerWeightUnit`) | −20 de estamina máxima por cada unidad de peso llevada | [C] |
| Energía sin fin: penalización posterior | por defecto 4 s (`PostBoostExhaustionSeconds`); cada objeto pone la suya | [C] |
| Durante la penalización | velocidad × 0,75 y gasto × 2 (la recarga no se bloquea) | [C] |
| Tope de la energía sin fin pedida por un cliente | 15 s (el servidor lo recorta) | [C] `MaxGrantDuration` |
| Multiplicador de velocidad de la carrera | hasta 2,4 (`MaxSpeedMultiplier`); en cada movimiento guardado (`FTNSavedMove_Turtle`); la aceleración sube con él (`MaxAcceleration × (1 + (m−1)·2)`) | [C] `TNMovementLimits::RaceBoostAcceleration` |

**Cifras derivadas** [calc]:

| Cosa | Resultado |
|---|---|
| Correr sin parar con la barra llena | 200 / 15 = **13,3 s** |
| Distancia con una barra llena corriendo | 53 m con el Blueprint (400 cm/s), 107 m con el código (800 cm/s) |
| Recarga de 0 a 200 tras dejar de correr | 0,8 s de espera + **3,3 s** = 4,1 s (5,1 s si se había agotado: +1 s de penalización, que además reinicia la espera) |
| Recarga de 100 a 200 | 0,8 + 2,7 s |
| Con un objeto de la mano | la barra máxima baja: bola o cabezota −40, concha o energía sin fin −20, tinta −10, **tótem −100** (la mitad). Cuentan las dos ranuras. Los objetos de carrera pesan 0 |

**Agotarse.** Al llegar a 0 mientras se corre, `bIsExhausted` (replicado a todos) dura al menos 1 s: no se puede correr ni
recargar; el jadeo, la cara y el sonido lo leen. La estamina baja de golpe si se recoge algo pesado (el techo dinámico
recorta el valor actual).

**Barra de energía sin fin** (`StaminaBoost`, `GrantUnlimitedStamina`): la barra queda llena y no gasta durante los segundos del
objeto; al acabar entra la penalización.

**Red.** `CurrentStamina` viaja solo al dueño; los demás (espectador, caras del HUD, sonido) reciben `StaminaShared`, un byte con
la fracción, solo cuando cambia. `bIsSprinting` y `bIsExhausted`, a todos. El RPC de correr solo sale al cambiar de estado.

**Ver la estamina.** El HUD dibuja un salvavidas de energía con la cara de la tortuga (§24); un espectador ve la de la
tortuga que sigue.

## 16. El caparazón y la bola física

`UTN_ShellComponent` (`Player/TN_ShellComponent`) lleva el estado; `ATN_ShellBody` (`Player/TN_ShellBody`) es la caja con física en
la que se convierte la tortuga; las condiciones puras están en `Player/TN_ShellDecisions.h` (pruebas `Tortunabo.Shell`).

### 16.1 Entrar y salir

| Regla | Detalle | Origen |
|---|---|---|
| Tecla | La misma para entrar y salir: Ctrl izquierdo y B / Círculo del mando (§6.1) | [BP], [D] |
| Para entrar | Viva; no derribada; no en pleno panzazo; **no nadando**; **con las manos vacías** (sin objeto en la mano: lo guardado en el caparazón no cuenta); no llevando ni llevada; no volando en el pelícano taxi. Sí se puede en el aire (la bolita en pleno salto sigue con su velocidad) | [C] `CanEnterShell`, `ToggleShell`, `ServerToggleShell` |
| Para salir | Han pasado 0,3 s desde que entró (`MinTimeInShellSeconds`) y la salida no está bloqueada | [C] `CanExitShell` |
| Salida bloqueada | Mientras la llevan, mientras vuela tras un lanzamiento (hasta que la caja se pare) y mientras dura un aturdimiento de la carrera | [C] `SetExitLocked` |
| Qué hace al entrar | Cancela el emote y el correr; el tope de velocidad pasa a 0; las extremidades y la cola encogen al 5 % en 0,12 s (`ShellRetractSeconds`); al salir se estiran en 0,35 s (`ShellExtendSeconds`) | [C] `TickShellVisual` |
| Sonido de entrar y salir | `EnterShellSound` y `ExitShellSound` **sin asignar**: hoy no suena nada al meterse ni al salir (los golpes de la bola sí suenan, §9.7) | [C], [BP] |
| Salidas forzadas | Un derribo, la muerte, el agua, parar la caja tras un lanzamiento o una caída, o el fin de un aturdimiento | [C] `ForceExitShell` |

Se replica `bIsInShell` a todos; la caja (`Body`) se replica como actor aparte con la relevancia de su tortuga.

### 16.2 La caja física (`ATN_ShellBody`)

Metida en el caparazón y suelta (nadie la lleva), la tortuga **es** una caja con física que rueda, resbala y rebota. La
cápsula deja de chocar (solo solapa: zonas, agua y disparadores la siguen viendo), el movimiento del personaje se apaga y
cápsula y malla siguen a la caja en todas las máquinas (`PlaceOnShellBody`, tras la física).

| Parámetro | Valor | Origen |
|---|---|---|
| Tamaño de la caja | **55 × 46 × 42 cm** (semiejes 27,5 × 23 × 21): largo cola-cabeza, ancho, alto tripa-lomo | [C] `BoxHalfExtent` |
| Masa | 38 kg | [C] |
| Fricción / rebote | 0,25 / **0,2** (era 0,35; **Discrepancia**: [`Docs/Animacion_Tortuga.md`](Animacion_Tortuga.md) sigue diciendo 0,35) | [C] `BeginPlay` |
| Amortiguación lineal / angular | 0,25 / 1,4 (la lineal no se toca: la patada de la tormenta calcula su arco con ella) | [C] |
| Giro máximo | 900°/s (~2,5 vueltas por segundo) | [C] |
| Separación de lo que solapa al nacer | 300 cm/s como mucho | [C] |
| Colisión | perfil `PhysicsActor`, CCD, aristas suavizadas (no tropieza en las costuras de las teselas del terreno); la cámara la ignora | [C] |
| Red | física replicada en interpolación predictiva (`TN.Shell.PhysicsRep 0` vuelve a la de siempre), 30 Hz (10 Hz mínimo) | [C] |
| Parada | por debajo de 60 cm/s y 1,5 rad/s durante 0,35 s | [C] `RestLinearSpeed`, `RestAngularSpeed`, `RestSeconds` |
| Tope de tiempo si debe salir al pararse | 9 s | [C] `MaxSecondsBeforeExit` |
| Aspecto | la malla va tumbada sobre la tripa con la cabeza hacia +X de la caja; el caparazón cubre a la tortuga metida (~40 × 36 × 30 cm) | [C] `MeshWorldTransform` |

**Cómo nace** (`UTN_ShellComponent::StartBody`):

- *A mano* (Ctrl estando de pie o andando): la caja nace de pie en el tronco con la velocidad que llevaba la tortuga y se vuelca
  hacia delante sobre la tripa (giro inicial de 3 rad/s). **No sale al pararse**: se queda hasta que el jugador salga.
- *Lanzada, soltada, escapada, aturdida o por caída larga*: nace tumbada en el sitio del actor; si va rápido (> 150 cm/s), da
  volteretas (7 rad/s) y sale mirando hacia donde va. Sale **sola** al pararse (o a los 9 s).
- *Al agua*: la caja detecta un volumen de agua (cada 0,1 s), la tortuga sale y nada.
- *Mientras la llevan* no hay caja: va enganchada al que la lleva.

**Al salir** (`StopBody`, `PlaceStandingFromBox`): se pone de pie donde quedó la caja, mirando hacia donde apuntaba la
cabeza, sobre el suelo de debajo (con una segunda búsqueda desde 2,5 m más arriba y, en la playa, comprobando que no está
hundida en el terreno).

### 16.3 Qué hace y qué no hace la tortuga metida

- **No puede**: moverse, saltar, esprintar, hacer la plancha, hacer emotes (el servidor los rechaza), interactuar con E
  (`TryInteract` sale), coger a nadie, usar el objeto de la mano (no lo hay), nadar (sale al agua).
- **Puede**: mirar con la cámara, hablar por la voz, cambiar de ranura (cambia sin animación), ser cogida y lanzada, ser
  empujada por otras tortugas y por lo que golpee la caja, y salir con la tecla.
- **El caparazón como refugio**: el picado de la gaviota de la zona **no coge** a quien va en bola (ni en plancha, ni llevada, ni a
  cubierto): `ATN_BeachGullZone` comprueba `IsBellyPoseActive() || IsInShell()` antes de agarrar, y quien se mete en el caparazón
  **colgando** de un pico se escurre y cae en bola aturdida (§21). La caca de gaviota sí derriba, salvo a quien se tira en plancha en el
  momento justo (§17.3).
- La caja **marea a los enemigos** de la playa si va a 900 cm/s (9 m/s) o más: 2,5 s (`TNBeachHitStun::ShellSeconds`, `ShellMinSpeed`) [C].
- El caparazón **no protege** del rebote ni de la tormenta: la caja se puede lanzar (catapultas, trampolines, patada de la tormenta, minas).
  La catapulta **sí** lanza a propósito a una bola quieta dentro de su cazo: cuenta como pasajera, arma la catapulta y sale despedida dando
  volteretas (ronda 4; el temblor del aviso ya es solo visual y no la expulsa antes; §34.3).
- **Sonido y efectos de los golpes** (ronda 4): un sonido y un mini efecto por material contra el que choca (arena, roca, madera, agua, otra
  tortuga, enemigo, objetos de los bañistas), con fuerza según la velocidad y un mínimo entre golpes (§9.7).

### 16.4 Caída larga, lanzamientos y aturdimiento

- **Caída de más de 5 m**: se mete sola, cae con física, rebota, rueda y sale sola al pararse (`TickFallRules`).
- **Lanzada por una compañera**: sale volando como caja, no puede salir hasta que se para (§19).
- **Aturdida en la carrera**: bola con la salida bloqueada, temblor y pajaritos (§21).

## 17. Plancha o panzazo

La plancha es un gesto de todo el rato: sirve para ir más lejos en el aire, para esquivar por lo alto (caca de gaviota) y
para lanzar a la compañera que se lleva en alto. Código: `Player/TortugaCharacter_Dive.cpp` (la pose y el disparo) y
`Player/TN_TurtleMovementComponent` (el arrastre, predicho en el cliente dueño). Documento: [`Docs/Animacion_Tortuga.md`](Animacion_Tortuga.md).

### 17.1 Cómo se hace

- **Segunda pulsación de salto en el aire** (`Jump` → `TryDive`): en cualquier momento de una caída o un salto. No cuesta
  estamina, no tiene enfriamiento y no hay límite de veces, pero hay que caer entre una y otra (`bIsDiving` mientras dura).
- **No se puede**: derribada, muerta, en el caparazón, ya en plancha, nadando (allí el salto es otra cosa).
- **Dirección**: hacia donde mira la **cámara** en horizontal (no hacia donde se movía), con giro suave del cuerpo a 720°/s
  (`DiveYawInterpSpeed`).
- **Velocidad al lanzarse** (`Server_StartDive`): `DiveForwardSpeed` (350 [BP]; 420 [C]) + un **bono de impulso** = velocidad
  horizontal que llevaba al saltar × un factor según a dónde mira la cámara respecto de la dirección del salto: 1,0 si coincide,
  0 de lado, **−0,5** si es opuesta (el bono puede restar). Total acotado a ±1500 cm/s (`DiveMaxTotalSpeed`), más
  `DiveDownwardSpeed` (100 [BP]; 200 [C]) hacia abajo. Ejemplos con el Blueprint [calc]: saltando corriendo (400) y mirando
  hacia delante, 750 cm/s; saltando andando (200), 550; parada, 350; corriendo y mirando hacia atrás, 150 (los registros
  `[Dive] Momentum` de las pruebas del 28-09 dan justo 750, 550 y 350: confirmado).
- **Cuerpo**: la cápsula se acuesta (semialtura 35), la malla se inclina −80° a 12/s (7/s al levantarse), se echa 70 cm hacia
  atrás (`DiveBodyCenterShift`), sube para apoyar la tripa (`DiveBellyPivotHeight` 11) y se aplasta (1,08 · 0,8 · 1,05).
- **Emote**: se cancela.
- **Llevando a una compañera en alto**: la plancha la lanza (§19.4).

### 17.2 Las fases del arrastre (`ETNBellyPhase`)

| Fase | Qué pasa | Cifras |
|---|---|---|
| En el aire | Como desde el lanzamiento: sin control, cayendo | |
| Arrastre (`Slide`) | Al caer de tripa, el resto del movimiento se arrastra. Inercia: la velocidad a lo largo del suelo, al 90 % y como mucho a **850 cm/s**; en bajada parte de la caída se convierte en arrastre. Rozamiento seco por superficie más un freno por velocidad. Sin control | ver la tabla siguiente |
| Levantarse | Pasados 0,3 s: por debajo de 60 cm/s, o a propósito por debajo de 180 cm/s **moviéndose** o **saltando** (un brinco). De pie en 0,35 s con la velocidad máxima subiendo del 35 % a la normal | `BellyMinSeconds` 0,3 · `BellyStopSpeed` 60 · `BellyExitSpeed` 180 · `BellyGetUpSeconds` 0,35 |
| Reptar (`Rest`) | Sin sitio para ponerse de pie (techo bajo): sigue sobre la tripa moviéndose a 150 cm/s hasta que quepa. No salta ni atraviesa nada | `BellyCrawlSpeed` |

| Superficie | Rozamiento (cm/s²) | Parada con entrada de 720 cm/s [D] |
|---|---|---|
| Arena | 800 | 0,57 s y 1,8 m |
| Tierra | 480 | 0,79 s |
| Roca | 400 | 0,87 s |
| Madera | 310 | 1 s |
| Agua poco profunda o fango | 220 | 1,18 s y 3,1 m |

El resto: freno por velocidad `BellyDrag` 1,5/s; desde 1,2 s el rozamiento crece (×3,5 por segundo) y a los **2,6 s** se levanta
igualmente (`BellyMaxSeconds`); tope de seguridad de todo el panzazo, 12 s (`DiveMaxSeconds`). En cuestas la gravedad a lo
largo del suelo pesa ×1,15 (`BellySlopeGravity`): cuesta abajo acelera (hasta 1000 cm/s, `BellyMaxSpeed`) y cuesta arriba
frena antes. La superficie sale de `Player/TN_TurtleSurface` (la misma que los pasos).

**Rebotes.** Contra paredes, obstáculos y otras tortugas, si iba contra ellos a más de 120 cm/s, devuelve un 35 % de esa
velocidad hacia fuera y conserva un 75 % de la que llevaba a lo largo; el cuerpo gira despacio hacia donde se desliza (220°/s)
salvo tras un rebote hacia atrás. **El cuerpo tumbado choca entero**: tras cada movimiento se barre una esfera de 14 cm a 32 cm
de la base de la cápsula desde el centro hacia la cabeza (66 cm) y hacia las patas (62 cm); si una punta se metería en una
pared, la tortuga se aparta lo justo. No cuentan como pared el suelo, las cuestas andables, los techos, lo que queda por
debajo de 18 cm, otras tortugas ni los cuerpos con física.

### 17.3 Para qué sirve

- **Esquivar por arriba** las cagadas de gaviota (la de la zona, la de la gaviota justiciera y la del cooperativo): la plancha libra si se
  hace **en el momento justo**, es decir, en el aire o arrastrándose aún a 250 cm/s o más (`TNBeach::IsDodgingByBellyDive`,
  `World/Beach/TN_BeachGullTuning.h`); tumbarse a esperar no vale (§36.7).
- **Librarse del agarre de las gaviotas**: la gaviota que picotea no coge a quien esté en plancha.
- **Atravesar en el aire** un hueco o una trampa y aterrizar deslizando por la arena sin pararse.
- **Lanzar** a la compañera que se lleva.
- **Levantarse rápido**: moverse o saltar con el arrastre por debajo de 180 cm/s.

### 17.4 Pose, polvo y sonido

Pose `PoseDive` en el aire y `PoseBellySlide` en el suelo (cabeza levantada, brazos abiertos que rozan el suelo, pies
pataleando; más deprisa, más vibra), con el empujón de brazos y rodillas al levantarse (0,45 s). Polvo local según la
superficie (nube y granos en la arena, terrones en la tierra, arenilla en la roca, serrín en la madera, rocío en el agua; hasta 8
emisores por tortuga, nada a más de 50 m). Sonido: «plaf» al caer, arrastre por superficie y «tonc» al chocar (§9.6).

### 17.5 Cifras que dependen de la velocidad de correr

El documento original ([`Docs/Animacion_Tortuga.md`](Animacion_Tortuga.md)) calculó que encadenar salto, plancha, arrastre y levantarse da **3,9 m/s
andando y 5,7 m/s esprintando, por debajo de los 4,5 y 8 m/s de ir corriendo**. Eso vale para las velocidades del código (450 y
800). Con las del Blueprint (200 y 400), la plancha se lanza a 350 más la velocidad del salto (550 a 750 cm/s) y **sale más
rápida que correr**. Una estimación gruesa [calc] (salto, plancha en el punto más alto, arrastre en arena hasta 180 cm/s y
levantarse en 0,35 s; sin tiempos de reacción) da un ciclo de ~737 cm en 1,6 s: **~4,6 m/s de media frente a 4 m/s corriendo** con
el Blueprint, y 7,6 frente a 8 con el código. No reproduce los 5,7 m/s del documento, así que la comparación hay que medirla en
juego. **No confirmado**.

### 17.6 Consola de la plancha (`TN.Dive.*`, afectan a la simulación)

| Comando | Qué hace |
|---|---|
| `TN.Dive.Slide 0\|1` | 0 = se para en seco al caer, como antes |
| `TN.Dive.Friction <x>` | Multiplica el rozamiento (0,5 resbala el doble; 2 se para antes) |
| `TN.Dive.Slope <x>` | Multiplica cuánto tiran las pendientes (0 = como en llano) |
| `TN.Dive.SlopeFall 0\|1` | 0 = cuesta abajo frena como en llano (antes de #62); 1 = desde 12° sigue cayendo |
| `TN.Dive.WallBounce 0\|1` | 0 = en el vuelo del panzazo resbala por las paredes (antes de #63); 1 = rebota (0,45 y 60 %) |
| `TN.Dive.MaxTime <s>` | Tope de segundos arrastrándose (0 = el del componente, 2,6 s) |
| `TN.Dive.Body 0\|1` | 0 = solo choca la cápsula (la cabeza y las patas vuelven a meterse en las paredes) |
| `TN.Dive.Debug 1` | Fase, tiempo, velocidad, superficie y rozamiento; flechas de velocidad y pendiente; el cuerpo que choca |

## 18. Nadar y bucear

- **Agua nadable**: un volumen de agua (`APhysicsVolume` con `bWaterVolume`; en el mapa procedural `ATN_ProcWaterVolume`, que
  considera «dentro» cuando el **centro de la cápsula** está bajo la superficie: en aguas someras se vadea; en la playa, las
  charcas y pozas de marea y el mar de la meta). El modo de nado lo predicen servidor y cliente.
- **Velocidad** (`SwimSpeed` [C]): **625 cm/s**, con la flotabilidad en 1,08 (`SwimBuoyancy`: sube a la superficie). Con el
  Blueprint es **más rápido que esprintar (400)**; con el código estaba entre andar (450) y esprintar (800). Correr no acelera
  el nado (fija la velocidad máxima de andar, no la de nadar) [inferido del código].
- **Salto del agua** (`PerformSwimHop`): impulso de **640 cm/s hacia arriba y 250 hacia delante**, cada 0,6 s como mucho, para
  subir a orillas e islotes. Nadando la plancha no existe: el salto es este.
- **No se puede** entrar en el caparazón nadando (la bola se hundiría y saldría en el acto). La bola que cae al agua sale y nada.
  Un panzazo que acaba en el agua termina y nada. Las caídas en el agua no rompen.
- **Corrientes, remolinos, depredadores y criaturas que rebotan** son actores del mapa procedural (§36.15): empujan a quien nada.
- **Sonido**: chapoteo en los pasos por agua poco profunda (desde 2 cm de profundidad; del todo desde 13 cm) y chapuzón en el
  «plaf» del panzazo (§9.6). No hay sonido de nado propio.
- **Bucear**: **no existe como mecánica**. No hay control vertical (el avance es horizontal por la cámara), la flotabilidad
  mantiene a la tortuga en la superficie y no hay aire ni nada bajo el agua. La zambullida de cabeza del acantilado de la meta
  es solo una pose en el aire (§30.5). **No confirmado en juego**.

## 19. Cargar, lanzar y liberarse

`UTN_CarryComponent` (`Player/TN_CarryComponent`). Servidor-autoritativo: `CarriedTurtle` (en quien carga) y `CarriedBy` (en la
cargada) se replican y cada máquina aplica el enganche.

### 19.1 Coger

| Regla | Valor | Origen |
|---|---|---|
| Tecla | E, sin interactuable a mano (orden de prioridades en el §23) | [C] `TryInteract` |
| Alcance | 240 cm (el servidor admite 80 cm más por la latencia) | [C] `GrabRange` |
| Delante | el producto escalar con el rumbo de quien coge es > 0,2, o está a menos de 120 cm | [C] `TryGrabNearest` |
| Quién puede coger | Viva, no derribada, **no metida en el caparazón**, sin cargar ni estar cargada | [C] |
| A quién se puede coger | A cualquier tortuga (también rival) **metida en el caparazón o derribada**, viva, y que no cargue ni esté cargada | [C] `CanBeGrabbed` |
| Al ser cogida | Si estaba derribada, se recupera y entra en el caparazón sin caja física; la salida queda bloqueada | [C] `ServerGrab` |
| Cómo va | Enganchada 120 cm por encima de quien carga (`CarryHeight`); las dos ignoran la colisión mutua; la cargada no puede saltar, ni meterse o salir del caparazón, ni moverse por su cuenta | [C] |
| Quien carga | Velocidad máxima **330 cm/s** (`CarrySpeedCap`); brazos en alto en la animación; no puede entrar en el caparazón | [C] |

### 19.2 Liberarse

Moverse (entrada de movimiento mayor que 0,3) es **forcejear**. Si el forcejeo es continuo durante **2 s**
(`SecondsToEscape`), el servidor la libera (`ForceRelease(true)`): sale por un lado al azar a 90 cm de quien la lleva, con un
impulso de 350 cm/s de lado y 450 hacia arriba, como caparazón que **sale sola al pararse**. Soltar la tecla reinicia la
cuenta. Mientras forcejea, a quien carga **le tiembla la cámara** (2,5° a 41 y 33 rad/s) y su lanzamiento sale con el
**45 %** de la fuerza (`StruggleThrowMultiplier`).

Se suelta también sin lanzar (`ForceRelease(false)`) si quien carga o la cargada muere, si quien carga cae derribada, y en el
mapa procedural al reaparecer o llegar (`ATN_ProcMapGameMode::ReleaseCarry`). Si **la cargada** cae derribada (el lanzable de
un tercero, la piel de plátano, el DBNO), quien la lleva la suelta antes y ella cae derribada como en el suelo, fuera del
caparazón (`TNCarryRules::KnockdownDropsFromCarrier`, #68); aturdida (la bola de la carrera) sigue en sus brazos.

### 19.3 Lanzar con la E: el saque de banda

- Con la cargada en alto, E no la suelta al momento: durante **0,18 s** (`ThrowWindupSeconds`) las dos aletas se echan detrás de la
  cabeza con la espalda arqueada y vuelven a subir, y al acabar (`FinishThrowWindup`, servidor) la suelta por encima de la
  cabeza; luego los brazos acompañan hacia delante y abajo (0,35 s). No se puede empezar otra toma de impulso mientras hay una.
- **Velocidad**: **1500 cm/s** (`ThrowSpeed`), ×0,45 si forcejea (675 cm/s).
- **Ángulo de todos los lanzamientos** (`ATortugaCharacter::GetThrowDirection`, sirve también para objetos y tinta): rumbo de
  la cámara y, sobre la horizontal, `ThrowBasePitchDeg` **25°** con la cámara a nivel, más el **40 %** de lo que se mire arriba o
  abajo (`ThrowAimPitchFactor`), entre **10°** y **45°**. La cámara mira 14° por debajo del mando: cámara 20° hacia abajo → 17°;
  30° hacia arriba → 37°.
- **Salida**: 70 cm por delante de quien lanza y a 140 cm de altura (`CarryHeight + 20`).
- La lanzada sale como caja: vuela, da volteretas, rebota y rueda; **no puede salir hasta que se para** (o toca el agua); al
  pararse sale sola y se pone de pie.

### 19.4 Lanzar con la plancha

Llevándola en alto, saltar y hacer la plancha la lanza (`Server_StartDive` → `ThrowWithDive`) hacia donde se tira, con el
lanzamiento de siempre más el **60 %** del impulso horizontal de la plancha (`DiveThrowCarryFactor`, que ya lleva la carrera) y el
**50 %** de la velocidad hacia arriba del salto (`DiveThrowJumpFactor`), con un máximo de **2300 cm/s** (`DiveThrowMaxSpeed`).
La portadora sigue con su plancha y sus brazos hacen el final del saque de banda mientras se tumba.

### 19.5 Soltar sin lanzar

X (o Y del mando) con una tortuga en alto: la deja 130 cm por delante y 30 cm más arriba, **sin velocidad**, y puede salir del
caparazón cuando quiera.

### 19.6 Detalles

- El rebote vertical de 560 cm/s (`BounceVelocity`) de la lanzada solo queda para quien **no** iba en el caparazón (no ocurre).
- La colisión con quien la llevaba vuelve 0,45 s después de soltarla.
- Las tortugas aturdidas de la carrera (que van en bola) **también se pueden coger** (§21).
- La bola de caparazón lanzada, al chocar con otra tortuga de pie, la empuja por la física, pero **no la derriba** (no hay código
  que lo haga). No confirmado en juego.

## 20. Derribo, ragdoll y recuperación

El derribo es lo que le pasa a la tortuga cuando algo la tumba: se queda **panza arriba como un muñeco de trapo** con
pajaritos, no puede hacer nada y se levanta sola. Código: `Player/TortugaCharacter_Knockdown.cpp` (`ApplyKnockdown`,
`RecoverFromKnockdown`, `ApplyKnockdownVisual`, `TickKnockdownRagdoll`).

### 20.1 Qué la derriba

| Fuente | Duración pedida | Detalles | Origen |
|---|---|---|---|
| Cáscara de plátano (`ATN_BananaPeel`) | 2,0 s | Resbala: empujón en la dirección de su velocidad (mínimo 350 cm/s, +400 hacia arriba) | [C] `KnockdownDuration`, `MinSlideForce`, `SlideVerticalForce` |
| Bola lanzada (`ATN_ThrowableItemActor`) | 2,0 s | Solo si va a 600 cm/s o más; una vez por lanzamiento y jugadora | [C] `KnockbackDuration`, `MinKnockdownSpeed` |
| Objetos de carrera (`TNBeach::KnockDownTurtle`) | disco 1,9 s · cangrejo 2,2 s · gaviota justiciera 2,6 s · protector solar 2,0 s | Ver el §29 | [C] |
| Golpes de la playa (erizo, quad, cagada de gaviota 2,4 s…) | los suyos | §35 y §36 | [C], [D] |
| Estado «casi muerta» (DBNO, `EnterDBNO`) | Desangrado de 8 s (`DBNOBleedoutSeconds`); derribo de 13 s | **Desactivado**: nadie llama a `EnterDBNO` | [C] |

Toda fuente pasa por `ApplyKnockdown` (solo servidor): la duración se sube al mínimo de **2,2 s** (`MinKnockdownSeconds`); si
ya estaba derribada, solo reinicia el temporizador; **no la derriba** quien tenga el protector solar ni la que vuela en el pelícano
(`TNRaceItems::IsInvulnerable`); saca a la tortuga del caparazón; corta la plancha; y le da un empujón: el de la fuente o,
si no, su velocidad de entonces × 1,0 con −400 cm/s hacia abajo (`KnockdownDownwardForce`).

### 20.2 Cómo se ve y qué pasa

- **Ragdoll físico** (perfil `Ragdoll`, choca con el mundo e ignora las cápsulas; velocidad de arranque limitada a 2200 cm/s; se
  devuelve encima del suelo si lo atraviesa, con un margen de 25 cm). Cada máquina simula el suyo. Se replica como el «emote 100»
  (`KNOCKDOWN_EMOTE_ID`) más el multicast del cuerpo tumbado.
- **Pajaritos**: tres pájaros y tres estrellitas dando vueltas sobre la cabeza con su piar y sus cuerdas mareadas
  (`UTN_DizzyBirdsComponent`); **ojos en espiral** mientras dure.
- **No puede**: moverse, saltar, interactuar, meterse en el caparazón, hacer emotes ni usar objetos. **Sí puede ser cogida** (la
  meten en el caparazón y la llevan) y **reanimada**.
- **Se levanta sola** al acabar: 0,75 s (`GetUpSeconds`) de mezcla de la pose del ragdoll a la de pie, con empujón de brazos y
  rodillas, **sin moverse ni saltar**. En total, **al menos 2,2 + 0,75 ≈ 3 s** fuera de juego [calc]. Al levantarse, la cápsula
  recupera la colisión antes de buscar sitio de pie (`FindTeleportSpot`) y se pone de pie justo al lado de una roca o una
  muralla.

### 20.3 Reanimar a una compañera derribada (con un emote)

Una tortuga derribada la puede levantar antes una compañera **haciendo un emote junto a ella** (canal de reanimación de
`Player/TortugaCharacter_Revive.cpp`; el HUD enseña a quien reanima una barra de progreso, «dando la vuelta a un compañero»):

| Regla | Valor |
|---|---|
| Empieza | Al empezar un emote (cualquiera de la rueda) con alguien derribado a menos de **300 cm** (`ReviveRadiusCm`); se elige a la más cercana |
| Dura | **3 s** (`ReviveDurationSeconds`), en pasos de 0,1 s |
| Condiciones para que siga | Quien reanima sigue con el emote, no está derribada ni ha llegado a la meta ni muerto; la derribada sigue derribada y a menos de 300 cm |
| Resultado | `ATN_RunGameMode::RevivePlayer`: si solo estaba derribada, llama a `RecoverFromKnockdown` en el acto (se levanta con la mezcla de 0,75 s) |
| Quién lo ve | `ReviveProgress` (0–1) se replica solo a quien reanima para su HUD; el sonido de canal sería 3D, pleno hasta 3 m y apagado a 25 m, si el Blueprint lo tuviera asignado (hoy no) |
| Cuidado | Solo los emotes 5 (RUN) y 6 (SUPERKIRK) se mantienen andando; con los demás hay que quedarse quieta |

Es la única reanimación de una **derribada** y **no** va con E. A una **eliminada** se la rescata con E, sin mantener, sobre su cuerpo (§22.3).

### 20.4 Recuperación y `RecoverFromKnockdown`

Devuelve el movimiento a `Walking`, cancela el emote 100 en todas las máquinas, restaura la rotación del cuerpo, para el latido
local y lanza el sonido de éxito de reanimación (ambos sin asset en el Blueprint: hoy mudos). Lo llaman el temporizador del derribo, `RevivePlayer` (la reanimación por emote), `BeginRespawn`, el
aturdimiento de la carrera y coger a una derribada.

## 21. Aturdida (carrera) y agarres de enemigos

**En la carrera de la playa no se muere**: lo que en el cooperativo mata, aquí aturde (`TNBeach::IsNoDeathWorld`,
[`Docs/Modo_Carrera.md`](Modo_Carrera.md), «No se muere: aturdimiento»). El aturdimiento (`TNBeach::StunTurtle`,
`World/Beach/TN_BeachStun`) **es distinto del derribo**: la tortuga queda en bola.

| Aspecto | Detalle | Origen |
|---|---|---|
| Qué es | Suelta lo que lleve (si carga a alguien), sale del derribo, entra en el caparazón como bola con caja física (`ForceEnterShell` sin caja + `StartBody`, lanzada si hay lanzamiento) y **con la salida bloqueada** | [C] `UTN_BeachStunComponent::StartStun` |
| Estado | `UTN_BeachStunComponent` (replicado, lo añade el servidor la primera vez; el cooperativo no lo lleva) con `bStunned` y el final | [C] |
| Al acabar | Desbloquea la salida y sale sola en cuanto la bola se para | [C] |
| Si le llega otro aturdimiento | Se alarga hasta el mayor de los dos finales (y, con lanzamiento, la bola sale disparada otra vez) | [C] |
| Cómo se ve | La bola **tiembla** (2,5 cm y 4°, solo la malla sobre la caja; se apaga en los últimos 0,6 s) y dan vueltas los pajaritos | [C] `TrembleAmplitude`, `TrembleDegrees`, `TrembleFadeSeconds` |
| Qué no puede | Usar objetos (`CanUseNow` falla), moverse, salir; **puede ser cogida y lanzada** | [C] |
| Quién no la aturde | El protector solar, el pelícano taxi, la que se está comiendo el gusano de arena; ni la que ya la recoloca la tormenta o la red de seguridad | [C] `StunTurtle` |
| Muerte convertida | Zonas de muerte, tormenta y vacío: vuelve a su último sitio seguro (se anota cada 0,5 s pisando suelo) y queda aturdida **2,5 s** (`RescueStunSeconds`); cualquier otra muerte: aturdida **3 s** donde está (`DeathStunSeconds`) | [D] |
| Vacío | El suelo más bajo pisado menos 150 m | [D] |

Duraciones de algunas fuentes: mina de arena (objeto de carrera) 3 s, nube de tormenta 2,2 s, mazazo del cangrejo gigante 3,5 s [D],
gaviota: al soltarla tras el agarre, 2 s más de aturdimiento (`AfterDropStun`) [C]. El resto, en los §35 y §36.

**Agarres de enemigos** (la gaviota, el pico del lagarto mordedor, el pulpo, el gusano, el pelícano taxi; base común
`ATN_BeachEnemy::BeginHoldTurtle`): la tortuga queda colgando pataleando, con el movimiento apagado (`MOVE_None`), y no puede
hacer nada. Reglas que le tocan a la tortuga:

- La **gaviota de la zona** (`ATN_BeachGullZone`) solo agarra a una tortuga **de pie**: **no** a quien va en plancha (`IsBellyPoseActive`), en bola, llevada, con sombrilla o a cubierto.
- Si se mete en el caparazón mientras cuelga, se escurre y cae en bola [D].
- Al soltarla cae en bola aturdida; si la suelta detrás de la tormenta, la patada la saca.
- Una sujeción de más de 6 s se suelta sola (el pelícano taxi la alarga a 20 s) y esa tortuga no se puede volver a sujetar en 2 s.
- **Meterse en el caparazón colgando** (ronda 4; era el «torbellino» de la segunda gaviota): quien la sujeta la **suelta antes** de que nazca la
  bola (`TNBeach::SlipFromHolder` → `ATN_BeachEnemy::OnHeldTurtleSlips`) y ella cae en bola aturdida hacia la salida, como al acabar el vuelo.
  Ningún enemigo sujeta a una tortuga en bola, en ragdoll o en brazos de otra (`ATN_BeachEnemy::CanHoldTurtle`: gaviota, lagarto, pulpo y
  pelícano taxi) y nada aturde ni derriba a una tortuga sujeta (`TNBeach::CanStunOver`). La causa raíz estaba en otro sitio: `RelocateTurtle`
  pedía `MOVE_Falling` a quien ya caía y el personaje conservaba la altura de la caída anterior, así que metía sola en bola a quien acababan
  de dejar de pie (§5.6). La tecla del caparazón no hace nada con un objeto en la mano (el registro lo dice), y junto al frente de la tormenta
  la zona de gaviotas caga en vez de picar. Detalle en el §36.7.

## 22. Muerte, reaparición en huevos y fantasma espectador

### 22.1 Cuándo se muere y qué ocurre según el modo

| Modo | Muerte | Reaparición |
|---|---|---|
| Cooperativo (mapa procedural) | Zonas de muerte con cuenta atrás, tormenta del camino, caída de más de 35 m, picotazo de gaviota (sin sombrilla ni cabezota), etc. (§36.15) | 1.º **pila de huevos** (§22.2) si la hay por delante de la tormenta; 2.º si no, **muerte real** (§22.3) |
| Carrera y 2 vs 2 en el mapa procedural | Igual, con pilas **por jugador** (la más avanzada que él ha alcanzado) | Igual |
| Carrera en la playa | **No se muere**: aturde (§21) | Vuelve al último sitio seguro |
| Clásico (`LVL_Run`) | Muerte real con cuerpo y rescate (§22.3) | Rescate de una compañera o tótem |

**Tótem** (`ETN_ItemUseType::Totem`): si lleva uno (mano o caparazón) al morir, `ATN_RunGameMode::TryTotemAutoRevive` lo consume,
**cancela la muerte** y muestra el efecto (sonido y efecto del Blueprint, hoy sin asignar). Usado a mano, revive a una
compañera eliminada al azar, la teletransporta 150 cm a la derecha del que lo usa; si nadie está eliminado, no se gasta.

### 22.2 Reaparecer en la pila de huevos (mapa procedural)

`ATN_ProcMapGameMode::MarkPlayerDead` → `BeginRespawn` → `FinishRespawn`:

1. Al «caer», se busca dónde reaparecer (`FindRespawnTransform`): la **pila de huevos más avanzada** que el equipo ha alcanzado
   (cooperativo) o él ha alcanzado (carrera, 2 vs 2), y que quede por delante de la tormenta por **al menos 3000 cm de camino**
   (`StormRespawnMargin`). Sin pila, la salida, mientras la tormenta no la haya alcanzado.
2. La tortuga sale del caparazón, suelta lo que cargue, **se esconde y se queda sin movimiento 2,5 s**
   (`RespawnDelaySeconds`).
3. Reaparece en el hueco de la pila que le toca (8 huecos alrededor, cada 45°), con **2 s de inmunidad**
   (`ReviveImmunitySeconds`); si en esos 2,5 s la tormenta ha pasado la pila, se recalcula y, si ya no hay ninguna, muere de
   verdad.
4. **Las pilas se activan al pasar cerca** (disparador esférico de 420 cm de radio, `ATN_ProcEggNest`): para el equipo entero en
   el cooperativo, para el jugador en carrera y 2 vs 2. Se activan una vez (brillan).

### 22.3 Muerte real, cuerpo y rescate

- **El cuerpo**: la tortuga hace ragdoll con las extremidades y el casco ocultos (`SetDeadVisual`) y queda como cadáver visible;
  el ragdoll se **congela a los 2 s** (`RagdollFreezeDelay`) en la posición del servidor; el peón pasa a ser del GameMode y sigue
  relevante para todos.
- **El rescate**: un `ATN_RescuePickup` invisible sigue la pelvis del cuerpo. Una compañera a 300 cm ve «Rescatar» y, con E,
  la revive **donde está el cuerpo**: conserva las conchas ganadas y tiene 2 s de inmunidad.
- **Mientras tanto** la jugadora pasa a espectadora: **fantasma** (§22.4).
- Si mueren todas o llegan, se acaba la ronda o la partida (§2.2).

### 22.4 El fantasma espectador (`Docs/Fantasma_Espectador.md`)

Quien está de espectador es un **fantasmita de tortuga**, en todos los modos: muerta en el cooperativo; en la carrera (donde no
se muere nunca), quien ya ha llegado a la meta o no corre el sprint final; y en el lobby, solo para probar.

| Cosa | Detalle |
|---|---|
| Qué ve el espectador | La interfaz de la tortuga que sigue (energía con su cara, objetos, puesto, conchas, avisos, chat rápido); en la tripulación, a todas menos la tortuga seguida (ella sale con cara de fantasma); abajo a la derecha, su icono de fantasma con su nombre y un cartel («¡Eres un fantasma!», «Mirando a *Nombre*», cámara, controles, «También miran…») |
| Qué ven los demás | Un fantasmita de ~1,1 m flotando donde está la cámara del espectador, mirando a la tortuga que sigue: cabeza, caparazón, dos aletas que baten, cola que se enrosca con una onda. Blanco azulado translúcido; se desvanece si la cámara del jugador pasa a menos de 2,3 m (no se ve a menos de 0,7 m); nunca a más de 11 m de la tortuga seguida. Abajo a la izquierda: «Te mira *Ana*» |
| Cambia sola | Si la seguida deja de valer (llega, cae, se va, se oculta o pasa a fantasma), a los 0,35 s pasa a la siguiente; sin nadie: «No queda nadie a quien mirar» |
| Cámara fija | La vista de la tortuga seguida (brazo, retraso, colisión y FOV suyos) |
| Cámara libre | Órbita alrededor de la tortuga: a 55 cm sobre su centro, de **1,7 a 9 m** (4,2 al empezar), cabeceo −70° a +30°, esfera de 14 cm contra paredes, nunca a menos de 40 cm del suelo; mezcla de 0,45 s entre fija y libre |
| Sensibilidad | La del menú (ratón o mando, eje Y invertido incluido) |
| Red | El servidor lo crea (`EnterSpectateMode`), siempre relevante a 15 Hz, sin movimiento replicado; el dueño manda su cámara (12 veces por segundo como mucho, y cada 0,5 s aunque no se mueva) y el servidor la limita a 11 m de la seguida |
| Se acaba | Al poseer una tortuga (huevo, rescate, ronda nueva): se desvanece en 0,4 s |
| Voz | **Un fantasma no habla por la voz de proximidad** (va con la tortuga) |

### 22.5 Volver a la vida desde un huevo: `TNGhost::ReviveIntoEgg`

Contrato de `Player/TN_Ghost.h`, **del cooperativo y aún sin enganchar** (se revivirá en los nidos, la «torre de nidos» que no
existe): `TNGhost::ReviveIntoEgg(PC, EggTransform)`, `OnHatched()`, `IsReviving`, `IsGhost`. En la carrera no se usa
(`TN.Ghost.Revive` solo avisa). Lo que hace:

- Si es fantasma: **vuelo en U de 1,2 s** hasta el huevo, se mete de cabeza, el huevo vibra 1 s con tres «pum» y eclosiona
  (2,2 s en total). Si aún tiene tortuga: se le quita y el huevo aparece, vibra y eclosiona (1,4 s).
- La tortuga se crea al eclosionar con `RestartPlayerAtTransform` dentro del huevo, de pie, y sale de un saltito de **380 cm/s
  hacia delante y 620 hacia arriba**. La tortuga que dejó al hacerse fantasma se destruye, y su rescate también.
- Pantalla de quien vuelve (`UTN_GhostHatchWidget`): negro, «¡PUM!», cáscara oscura que se resquebraja con luz dorada y se abre. La misma
  cáscara hace de **«huevo negro»** en la carrera, en otro modo (`ShowCurtain`, `Knock`, `Open`, `IsClosed`, `IsOpening`): sin fundido a
  negro ni «¡PUM!», las dos mitades entran desde arriba y desde abajo cada vez más deprisa y se juntan con un «¡clac!»; al abrirse
  (`Open(true)`) se rompe con fogonazo y trozos, o se funde en 0,25 s (`Open(false)`); si nadie la abre, se rompe sola (14 s en la llegada,
  50 s entre rondas). Se usa en la llegada al agua y en el paso entre rondas (§30.6).
- No toca `bIsAlive`, `bIsEliminated`, `bHasFinishedRun` ni las listas del GameMode: eso lo decide quien llama, en `OnHatched`.

### 22.6 Llegar a la meta en la carrera de la playa

1. Contacto con el agua de la meta: el puesto se decide en ese instante (`MarkPlayerFinished`) y se replica (`RoundArrivals`).
2. La tortuga sale del caparazón, del aturdimiento y de quien la lleve y **se queda a la vista en el agua 0,8 s**
   (`FinishSplashHoldSeconds`) con su chapuzón y la postura de la zambullida congelada (`bPauseAnims`, cosmético y en cada máquina): nadie
   la ve ponerse de pie.
3. En su pantalla, según entra en el agua se cierra el huevo negro (0,28 s) y sale «Has quedado X.º» con su premio (§30.6).
4. Pasa por la meta de la base (puesto, puntos, la oculta) y a **fantasma espectador**, tapado por el huevo negro; al romperse ya es fantasma (o ve el recuento o el podio si ya no queda nadie).

### 22.7 Consola

`TN.Ghost.Become [jugador]` (cualquier modo, también el lobby) y `TN.Ghost.Revive [jugador]` (cooperativo y lobby).

## 23. Interacciones y empujones

### 23.1 Interactuar (E)

- **Detección**: cada 0,1 s (`InteractionScanInterval`) se buscan actores `ATN_InteractableBase` en una esfera de **250 cm**
  (`MaxInteractionDistance`, bajada de 350 en #214; sale de `ATortugaCharacter::DefaultInteractionDistance`) alrededor de la
  tortuga; gana el más cercano por su punto de interacción (`GetInteractionPointFor`). El HUD muestra la tecla y el texto del
  interactuable (`UTN_RunHUDWidget::TickPrompt`).
- **Validación en el servidor** (`ServerTryInteract`): distancia ≤ `max(250, distancia del interactuable) + 100 + holgura por
  ping` (el 25 % del ping, hasta 120 cm). La distancia propia del interactuable (`InteractionDistance`: 250 de serie) solo
  cuenta aquí y solo si supera a la de la tortuga; ningún interactuable la supera ya (rescates y entrada del tutorial bajaron
  de 300 a 250).
- **Orden de prioridades al pulsar E** (`ATortugaCharacter::TryInteract`):
  1. Derribada o en el caparazón: nada.
  2. **Llevando a alguien**: lo lanza (§19).
  3. Hay un interactuable a mano: si es de **mantener** (rebuscar, cofres), empieza a mantener; si no, lo usa.
  4. No hay interactuable: intenta **coger** a la tortuga cogible que tenga delante (§19).
  5. Si tampoco: **usa el objeto de la mano** (§27).
  6. Caso especial: si el interactuable a mano es un objeto que no cabe (las dos ranuras llenas) y hay un objeto equipado, el
     servidor **usa el objeto de la mano** en vez de perder la pulsación.
- **Interacción de mantener** (`ServerBeginHoldInteract` / `ServerEndHoldInteract`): el aro del HUD se llena mientras se mantiene
  E; soltar antes cancela; el servidor cuenta el tiempo y vigila que la tortuga siga cerca (alcance + 0,85 m) y en condiciones
  (no se meta en el caparazón, no quede tumbada, no muera, no la cojan). Rebuscar: 1,3 s; cofre del lobby: 5 s; cofre de la
  playa: 5,5 s (§28.4).
- **Los interactuables** (§34, §36.15): botones, placas, tienda y probador, tótem, sombrilla, rescates, objetos del suelo,
  rebuscables, cofres, selectores y el general del lobby.
- Consola: `TN.Debug.Interaction 1` (esfera y líneas de depuración y registro de cada pulsación).

### 23.2 Empujones y contactos

- **Físicos**: el movimiento tiene `bEnablePhysicsInteraction` y, al empezar, se fuerzan `PushForceFactor` y `TouchForceFactor`
  a 1,0 (con 2,0 atravesaban objetos; con 0,5 no movían): la tortuga empuja cajas, balones (`ATN_BouncePhysicsObject`,
  `ATN_PhysicsObjectActor`) y **las cajas de caparazón de otras**.
- **Entre tortugas**: las cápsulas se bloquean como peones; quien lleva y la llevada se ignoran mutuamente; la plancha rebota
  contra otras tortugas (§17). Las cajas de caparazón lanzadas o en movimiento empujan a las que están de pie por física.
- **Golpes y empujones dirigidos**: el protector solar derriba a las que toca (2 s, empuja 650 y levanta 320 cm/s), el disco,
  el cangrejo teledirigido y la gaviota justiciera, la mina, la patada de los bañistas, el susto del lagarto (6,5 m/s), etc.
- **No hay un botón de empujar** ni un placaje propio.

## 24. Cabeza que mira, cara y caras del HUD

### 24.1 La cabeza que mira (#623, `UTN_TurtleAnimInstance` y `TNHeadLook`)

- En tercera persona la cabeza sigue a la **cámara** del jugador (el giro del mando respecto del cuerpo): guiñada hasta
  **±70°** y cabeceo de **-35° a +45°** (positivo mirando arriba). Pasado el tope se queda en él hasta **95°**; de ahí a
  **140°** vuelve al frente con una curva suave y, mirando hacia atrás, mira al frente (sin saltar de un lado a otro).
- El giro se reparte **40 % en el cuello y 60 % en la cabeza** (huesos `Neck` y `Head`), encima de la pose de todo lo demás, y
  lo sigue un **muelle crítico** de 0,2 s (unos 10 por segundo). El casco y la cara, enganchados a la cabeza, la siguen.
- **Red**: el servidor conoce el giro del mando de cada cliente por su movimiento (ServerMove) y escribe la guiñada relativa en
  un byte (`ReplicatedViewYaw`, pasos de 1,4°, sin el dueño) solo cuando se mueve 2 pasos o más; el cabeceo es el del motor
  (`RemoteViewPitch16`). Sin RPC propio.
- **No se aplica** (su peso se funde a 0): ragdoll, derribo y levantarse, caparazón y bola, panzazo y levantarse de la tripa,
  emotes, celebraciones del podio, zambullida del acantilado, primera persona y VR (manda el visor), dentro del probador y en
  lo que no es un jugador (copias del tendero, el general, el podio y el escaparate, tortugas de práctica). Pruebas:
  `TN.HeadLook.Shots`, `TN.HeadLook.Sweep` y `TN.HeadLook.Log` (`Docs/Comandos_Prueba.md`).

### 24.2 La cara 3D (`UTN_TurtleFaceComponent`)

Cosmético y local en cada máquina, a partir de estado ya replicado (estamina, correr, derribo, caparazón, emote, chat rápido, voz).
Solo actúa con la malla de demo. Umbrales (los mismos que el HUD, con histéresis para no parpadear):

| Ánimo | Cuándo | Cara |
|---|---|---|
| Feliz | energía ≥ 0,5 (≥ 0,6 si venía de cansada) | Sonrisa abierta pequeña; quieta, de vez en cuando asoma la punta de la lengua |
| Cansada | energía < 0,5 (sale a 0,6) | Párpados a media asta (`EyeTired` 0,5), mirada baja, boca pequeña, colorete suave y una gota de sudor (cada 1,7 s) |
| Jadeando | agotada o energía < 0,22 (sale a 0,3) | Párpados más caídos (`EyeTired` 1), boca muy abierta al ritmo del jadeo (2,4 por segundo), lengua colgando, colorete fuerte, dos gotas a contratiempo (cada 1,15 s) y a ratos ojos apretados «>_<» |
| Tumbada | derribada o muerta | Ojos en espiral, boca torcida, lengua floja por un lado |
| Caparazón | metida | Sin lengua ni sudor |

Además: «>_<» casi un segundo al agotarse; lengua al viento al esprintar o en plancha (sale por la comisura, del lado de fuera
en una curva de más de 80°/s); caras propias de los emotes WAZAAA (boca abierta, lengua meneándose), HAPPIE (sonrisa
enorme), modo loco (lengua al aire cambiando de lado) y fiesta (boca a gritos y ojos apretados).

**La lengua** es una malla procedural de 12 anillos de 16 vértices y una punta (rosa `#FF6F8E`, surco `#D94A6A`), simulada
con una cadena de 8 puntos (gravedad, rozamiento con el aire, aleteo y bombeo del jadeo, choque con la cara), a pasos de 1/120 s
(hasta 4 por fotograma); fuera de cámara no se simula. **El sudor** son dos gotas procedurales. **Hablar**: la boca se mueve
por sílabas (0,09–0,17 s) mientras suena un chat rápido (0,4 s + 0,07 s por letra, de 1 a 4,2 s) o mientras se oye su voz.

**Ajustes**: `SprintTongueLength` (20 cm), `PantTongueLength`, `TongueHalfWidth`, `TongueShapeStiffness`, `TongueGravity`,
`TongueAirDrag`, `TongueFlap`, `bIdleBlep`. **Consola**: `tn.Face.Mood 0-3` (feliz, cansada, jadeando, tumbada; −1 = la real),
`tn.Face.Tongue 0-3` (dentro, al viento, colgando, la punta), `tn.Face.Talk 1`.

**Ojos**: parpadean cada 2,5–5,5 s en 0,16 s (a veces dos seguidas) y se ponen en espiral derribada o muerta. Son parámetros de
`M_TurtleBody` (`EyeBlink`, `EyeDizzy`, `EyeTired`, `EyeSqueeze`, `MouthOpen`, `MouthSmile`, `FaceBlush`).

### 24.3 Las caras del HUD (`UI/HUD/TN_HUDFaces.h`, `FaceFor` en `TN_RunHUDWidget.cpp`)

El distintivo de abajo a la izquierda y la fila de la tripulación enseñan una cara de tortuga que sale del estado. Prioridad de
arriba abajo:

| Cara (`ETNTurtleFace`) | Cuándo | Dibujo |
|---|---|---|
| **Victoria** (`Win`) | Ha llegado a la meta y no está eliminada | Ojos de estrella |
| **Mareada** (`Down`) | Eliminada (o en DBNO, hoy inactivo). **No** sale al quedar derribada | Ojos en cruz y estrellitas |
| **Caparazón** (`Shell`) | Metida en el caparazón | Caparazón cerrado |
| **Jadeando** (`Panting`) | Agotada o energía < 0,22 (sale a 0,3) | Lengua fuera |
| **Cansada** (`Tired`) | Energía < 0,5 (sale a 0,6) | Párpados a media asta y una gota de sudor |
| **Feliz** (`Happy`) | El resto | Sonrisa |

Consola: `tn.HUD.Face 0-5` (0 feliz, 1 cansada, 2 jadeando, 3 caparazón, 4 mareada, 5 victoria; −1 = la real), `tn.HUD.Energy 0.3`,
`tn.HUD.Talk 1`, `tn.HUD.CrewPreview 3`. Con 8 jugadores la tripulación es de hasta 7 filas (a partir de 6 filas, al 88 %). La interfaz de
la tortuga seguida por un espectador es la del HUD en código; con el HUD de Blueprint solo sigue la energía y el inventario.

## 25. Emotes y chat rápido

### 25.1 Emotes

Se hacen con la **rueda de emotes** (Q o LT, mantener y soltar; elegir con ratón o stick derecho). El catálogo es
`DA_EmoteWheelCatalog` ([DT], 8 entradas, todas con **enfriamiento de 0,5 s**); la animación de cada emote está en
`ATortugaCharacter::TickEmote` (huesos en espacio de malla). El nombre de la rueda y el del código no coinciden:

| ID | Nombre en la rueda | Nombre en el código | Qué hace | Duración | ¿Se puede andar? |
|---|---|---|---|---|---|
| 0 | WAZAAA | Saludar | Brazo derecho a 90° que aletea ±75°, izquierdo abajo, cabeceo, cola | 2,5 s | no |
| 1 | HAPPIE | Aplauso | Brazos en V cerrada, palmadas a 4 Hz | 3 s | no |
| 2 | PARACOPTER | Helicóptero | Brazos y piernas giran | bucle | no |
| 3 | SAX-O | Palmada potente | Ambos brazos, golpe rápido abajo y lento arriba | bucle | no |
| 4 | *(no está en la rueda: no se puede lanzar)* | Aplaudir | Brazo derecho, palmada; cola girando | 2 s | no |
| 5 | RUN | Baile irlandés | Los brazos se juntan por detrás | bucle | **sí** |
| 6 | SUPERKIRK | Flotar (Superman) | Pose de vuelo | 5 s | **sí** |
| 7 | *(no está en la rueda: no se puede lanzar)* | Señalar | Un brazo apunta | 1,2 s | no |
| 8 | MISTIK | Modo loco 2 | Caos total con traslaciones | bucle | no |
| 9 | PATRICK | Fiesta (modo loco) | Caos de rotaciones; clip `Yelling` con rebote | bucle | no |
| 100 | *(interno)* | Derribo | El «emote» del derribo (`KNOCKDOWN_EMOTE_ID`) | mientras dure | — |

- Cada emote reproduce la **canción** `/Game/Audio/DanceSounds/<ID>` en bucle mientras dure, en 3D con atenuación de voz
  (círculo interior 300 cm, exterior 2500 cm) [BP].
- **Reglas del servidor** (`ServerSetEmote`): el ID tiene que existir en el catálogo; se rechaza si la jugadora no está viva, está
  en DBNO, derribada o en el caparazón; el enfriamiento es por ID de emote y jugadora. Se replica a todos menos al dueño (que ya lo
  arrancó localmente).
- **Se cancela**: al moverse (salvo 5 y 6), al hacer la plancha, al meterse en el caparazón, al derribarla, al terminar la
  duración. La salida mezcla en 0,25 s (`EmoteBlendOutDuration`).
- **Un emote es también la orden de reanimar** a una derribada cercana (§20.3).
- No hay teclas de número (§6).

### 25.2 Chat rápido

Rueda con C o LB (mantener y soltar). Diez frases fijas de `DA_QuickChatWheelCatalog` ([DT]): 0 «¡Vamos!», 1 «Buen juego», 2 «Lo
siento», 3 «Gracias», 4 «¡Necesito ayuda!», 5 «¡Cuidado!», 6 «Nos vemos en la meta», 7 «¡Eso estuvo bien!», 8 «OOOOHHHHH»,
9 «WAZAAA!!». Se envían al servidor (`ServerSendQuickChat`), que valida el ID y aplica **2 s de enfriamiento** por jugadora
(`QuickChatCooldownSeconds`; las frases no lo cambian: `CooldownOverride` 0) y lo escribe en el GameState
(`AddQuickChatEntry`, multicast a todas las máquinas): sale como bocadillo sobre la tripulación del HUD y la tortuga mueve la boca.
Las frases son `FText` del catálogo (`DA_QuickChatWheelCatalog`) y salen en el idioma elegido (§10).

## 26. Voz de proximidad

`UProximityVoiceComponent` (`Voice/ProximityVoiceComponent`), propio del proyecto (el VOIP del motor está apagado,
`[Voice] bEnabled=false`). Va con la tortuga: **un fantasma no habla**.

| Etapa | Cómo funciona | Cifras |
|---|---|---|
| Captura | El micrófono elegido (o el predeterminado de Windows; se abre al empezar la voz y no se cambia en caliente) a 48 kHz mono | `VoiceSampleRate` 48000 |
| Detección | Habla si el nivel RMS supera el umbral; se mantiene 0,3 s tras el último sonido | `SpeakingThreshold` 0,01 (−40 dB), `SilenceHoldOffSeconds` 0,3 |
| Ganancia | La del componente × 25–300 % del menú | `VoiceGain` 6 |
| Compresión | Se baja a **16 kHz** con 48 kHz de captura (factor entero `max(1, captura / VoiceTargetSampleRate)` sobre la frecuencia real medida, `VoiceTargetSampleRate` 16000; filtro de caja) y se comprime a **mu-law de 8 bits** (1 byte por muestra): **16 KB/s** por quien habla | |
| Envío | Paquetes cada 80 ms (12,5 por segundo); el servidor acepta como mucho 25 por segundo y rechaza más de 8192 bytes | `SendInterval` 0,08; `MinVoicePacketInterval` 0,04 |
| Retransmisión | El servidor reenvía solo a quien esté a menos de **25 m** (`OuterRadius` 2500) y, si son más de **4**, a los 4 más cercanos (`MaxVoiceListeners`) | Peor caso con ocho hablando juntos: ~0,5 MB/s de subida del anfitrión |
| Reproducción | Onda procedural 3D en la tortuga que habla: volumen pleno hasta **3 m** (`InnerRadius` 300), caída natural hasta los 25 m | `PlaybackVolume` 3 |
| Estado | `bIsSpeaking` (replicado) y `IsHeardSpeaking()` (oída hace menos de 0,35 s) mueven la boca y el bocadillo con barras del HUD | |

**Ajustes del jugador** ([`Docs/Menu_Pausa.md`](Menu_Pausa.md)): voz abierta (por defecto) o **pulsar para hablar** (V / cruceta abajo);
silenciar mi micrófono (se sigue capturando, no se envía nada, la tortuga deja de «hablar» al acto); volumen de todas las voces
(clase `TN_Voice`); **volumen de cada compañero de 0 a 200 % y silenciarlo** (se recuerda por su id de plataforma o su nombre);
micrófono; sensibilidad (±20 dB sobre el umbral); ganancia; medidor en vivo; y «Quién habla» (accesibilidad: los nombres de
quien se oye hablar).

**Límites**: 8 jugadores; solo se oye a menos de 25 m y a los 4 más cercanos que hablen; cada clip lleva la calidad de un teléfono
bueno; **Opus** queda pendiente (medir con `stat net` con 4 y 8). Sin micrófono, no se emite nada. En los viajes la captura
abierta nunca se cierra (cerrar una captura WASAPI abierta cuelga el juego). Depuración: `TN.Voice.Debug 1`.

## 27. Inventario

`UTN_InventoryComponent` (`Player/TN_InventoryComponent`; decisiones puras en `Player/TN_InventoryDecisions.h`, pruebas
`Tortunabo.Inventory`). Servidor-autoritativo: todo cambia en el servidor y se replica (`EquippedItem`, `StoredItem`, sus
banderas y `StashSerial/StashKind`); la única excepción es cambiar de ranura, que un cliente pide con un RPC.

### 27.1 Las dos ranuras

| Ranura | Dónde está | Qué se ve |
|---|---|---|
| **Equipada** («la mano») | En las aletas delanteras | El objeto en las aletas (§27.6); su icono en el HUD, abajo en el centro |
| **Guardada** | Dentro del caparazón | Nada en el mundo; icono en el HUD |

El **peso** de las dos cuenta para la estamina máxima (§15). Los datos de un objeto son un `FTN_InventoryItem`
(`Core/TN_InventoryTypes.h`): `ItemId`, icono, malla y su escala y giro, `UseType`, peso, sonidos, la clase del pickup del suelo
y un subtipo de parámetros según el uso. **No tiene nombre visible**: el HUD solo enseña el icono (los de `DT_Items` son
texturas de relleno del motor, [DT]).

### 27.2 Recoger

- Con **E** cerca de un objeto del suelo (`ATN_PickupInteractableBase`, esfera de 250 cm como cualquier interactuable).
- **Destino** (`TNInventoryLogic::DecideAddSlot`): mano libre → la mano; si no, y el caparazón está libre → al caparazón (con la
  animación de guardar); con **las dos llenas no se recoge** (`CanReceiveItem(…, false)`: el objeto se queda en el suelo y esa
  pulsación de E **usa el objeto de la mano**, §23).
- El objeto del suelo se destruye al recogerlo (el actor desaparece en el fotograma siguiente para que la destrucción replicada
  sea fiable).
- Sonido: `PickupSound` del personaje (sin asignar hoy). Al aparecer en la aleta hace un «pop» de 0,2 s.
- Sustituir lo de la mano (`TryAddOrReplaceEquipped(…, true)`) solo lo hacen el código de pruebas y los objetos de carrera
  (`TN.Race.Item`, triple coco).

### 27.3 Cambiar de ranura (G / RB)

`RotateItems`: intercambia mano y caparazón. Con **un solo objeto** lo mete en el caparazón (o lo saca): es la forma de **vaciar
las manos** para poder meterse en la bola. Con ambas vacías no hace nada. Si el cambio sale de un cliente va con un RPC fiable.

### 27.4 Usar (E sin interactuable a mano)

`ServerUseEquippedItem`: no vale derribada ni muerta. Reparte por `ETN_ItemUseType` (una rama por uso: validar → gastar → efecto):

| Uso (`ETN_ItemUseType`) | Rama | Fila de `DT_Items` |
|---|---|---|
| `SelfStaminaBoost` | Energía sin fin | `StaminaBoost` |
| `SelfStaminaFull` | Barra llena de golpe (recupera al máximo sin penalización) | **ninguna** (existe en el código y en los pesos, pero no hay fila) |
| `Throwable` | Lanzar la bola | `ThrowableBall` |
| `BigHead` | Cabezota | `BigHead` |
| `Conch` | Colocar la concha trampa | `Conch` |
| `InkThrower` | Lanzar tinta | `Tinta` |
| `Totem` | Revivir a una compañera eliminada | `Totem` |
| `RaceItem` | Objeto de carrera (`TNRaceItems::ServerUse`) | — (definidos en código) |
| `None` | Sin uso | `Score` (relleno) |

Al **gastar** la mano, si había algo guardado **sube a la mano** (sale del caparazón con su animación, 0,3 s después).

### 27.5 Soltar (X / Y)

`ServerDropEquippedItem`: pone el objeto en el suelo, 120 cm delante y 40 arriba, buscando el suelo (`FindGroundBelow`), como
un pickup nuevo con la clase de la fila (`PickupActorClass`). **Valida antes de gastar** (si no hay clase o falla el spawn, no
pierde el objeto). Con una compañera en alto, la tecla la suelta sin lanzarla (§19).

### 27.6 El objeto en las aletas

Cosmético y local en cada máquina, a partir de lo replicado (nada en el servidor dedicado). El objeto se engancha a los huesos de
las aletas (`RightHand`; abrazado, `Spine2`) y cada fotograma, tras la animación, se coloca según su forma (`ETNItemHold`):

| Forma | Cuándo (tamaño en el mundo = malla × `EquippedMeshScale`) | Dónde |
|---|---|---|
| En una aleta | Lo pequeño | Apoyado sobre la aleta derecha, de pie y de frente, como una bandeja |
| Abrazado | Lado mayor ≥ `HugMinSize` **34 cm** o peso ≥ `HugMinWeight` **3** | Entre las dos aletas, delante de la tripa; la apertura se ajusta sola al ancho del objeto |
| Por un extremo | Largo ≥ `ByEndMinLength` **30 cm** y ≥ **2,2** veces su ancho | Eje largo hacia delante y arriba (40°) con la punta de atrás en la aleta derecha |

`HoldOverrides` (por `ItemId`, en el Blueprint) fuerza una forma. Los brazos lo sujetan andando, corriendo y saltando; nadando,
en plancha, con un emote o levantándose las aletas hacen lo suyo y el objeto las sigue. **Metida en el caparazón, llevando a otra
tortuga, llevada o muerta, el objeto encoge y desaparece**; al salir vuelve.

**Guardar y sacar** (`StashSerial`, `StashKind`: bits 1 = entra lo de la mano, 2 = sale lo guardado, 4 = tras gastar lo de la mano):
la aleta derecha va a la espalda por encima del hombro (0,5 s, `StashSeconds`), lo de la mano encoge y entra con un «toc» hueco,
el guardado sale creciendo con un «toc» más agudo (330–380 Hz al guardar, 440–520 Hz al sacar; [`Docs/Sonido_Tortuga.md`](Sonido_Tortuga.md)). Si solo
sale lo guardado (tras lanzar o gastar), empieza 0,3 s después. Metida en el caparazón o llevando, cambia sin animación.

### 27.7 Lanzar objetos

La bola y la tinta salen **hacia donde mira la cámara con el ángulo común de todos los lanzamientos** (25° con la cámara a nivel,
entre 10° y 45°; §19.3) desde 120 cm por delante de la tortuga y 40 cm por encima del suelo (`GetItemSpawnLocation`). La aleta
derecha hace el golpe de lanzar (`MulticastItemThrowAnim`, cosmético, no fiable). Los objetos de carrera lanzables (mina y disco) usan
28° y 6° respectivamente.

### 27.8 Aturdir enemigos lanzando objetos

Lo lanzado (y algunas cosas más) **marea a los enemigos**; a los de la playa se les aplica `ATN_BeachEnemy::ApplyHitStun`
(pajaritos, sin atacar; si ya lo estaban, se alarga hasta el mayor de los dos finales; el mismo objeto no repite en 0,6 s):

| Lo que golpea | Efecto en un enemigo de la playa | Efecto en los enemigos del cooperativo | Origen |
|---|---|---|---|
| Bola o piedra (a 600 cm/s o más) | **3 s** de mareo (`ThrownSeconds`); la bola rebota en su cuerpo (45 % de la velocidad y un saltito) | `ApplyStun(2 s)` si el enemigo implementa la interfaz | [C] `ATN_ThrowableItemActor` |
| Tinta | 3 s de mareo | `ApplyBlind(5 s)` | [C] `ATN_InkProjectile` |
| Concha trampa colocada | Atrapa si lo pisa | `ApplyStun(2,5 s)` | [C] `ATN_ConchPickup` |
| Caja de caparazón a 9 m/s o más | 2,5 s de mareo | — | [C] |
| Mina de arena / silbato / disco / cangrejo / gaviota justiciera / protector | 5 s / 5 s / 4 s / 4 s / 4 s / 4 s | — | [C] §29 |

Quién lo acepta: **todos** los `ATN_BeachEnemy` salvo el paso de quads (`AcceptsHitStun()` es falso) y, en el cooperativo, solo el
cangrejo (`ATN_CrabActor` implementa `ITN_EnemyTargetInterface`; la gaviota del cooperativo no).

## 28. Objetos de siempre

«De siempre» = los del cooperativo y el clásico, los que están en la tabla `DT_Items`
(`Content/Blueprints/Gameplay/Items/DT_Items.uasset`) y los objetos del mundo que se relacionan con ellos. Sus efectos se aplican
también en la carrera (con otros pesos, §29.5).

### 28.1 El catálogo `DT_Items` ([DT], leído del `.uasset` de HEAD)

Siete filas. Todas usan el pickup `BP_GenericPickup`. Los iconos y las mallas son **provisionales**: iconos de relleno del motor
(`AICON-Green`, `AnalogHat`…) y mallas de prueba (un cilindro, una piedra, una bola, un peluche, un calamar).

| Fila | `ItemId` | Nombre de trabajo | Uso | Peso | Malla (escala) | Parámetros |
|---|---|---|---|---|---|---|
| `StaminaBoost` | `StaminaBoost` | Energía sin fin (en el código, «Barrita Energética») | `SelfStaminaBoost` | **1** | cilindro (0,25) | 4 s de energía sin fin; 2 s de penalización posterior |
| `ThrowableBall` | `ThrowableBall` | Bola (piedra) | `Throwable` | **2** | `Piedra1` (1) | velocidad 1800 cm/s; proyectil `BP_GenericThrowable` |
| `BigHead` | `BigHead` | Cabezota | `BigHead` | **2** | bola (0,25) | (los del personaje: ×3,5 durante 8 s) |
| `Conch` | `Conch` | Concha trampa | `Conch` | **1** | `ConchaCerrada` (0,5) | pickup `BP_ConchPickUp` |
| `Tinta` | `Tinta` | Tinta de calamar | `InkThrower` | **0,5** | `Calamar` (0,5) | velocidad 1200 cm/s; proyectil `BP_InkProjectile` |
| `Totem` | `Totem` | Tótem | `Totem` | **5** | `Peluche1` (1, girado 180°) | — |
| `Score` | `Score` | (relleno) | `None` | 0 | ayuda del editor | **No sale nunca** (sin uso) |

### 28.2 Qué hace cada uno

**Energía sin fin (`StaminaBoost`)** — `HandleUseSelfStaminaBoost`.
- Efecto: la estamina queda llena y **no gasta durante 4 s** al correr. Al acabar entra una penalización de **2 s**
  (`PostBoostExhaustionSeconds` de la fila): velocidad × 0,75 y gasto × 2.
- Cifra útil [calc]: 4 s corriendo sin gasto = 16 m con el Blueprint (400 cm/s).
- El coco dorado de la carrera pone la penalización a 0 (§29).
- Tope: un cliente no puede pedir más de 15 s.
- Peso 1: −20 de estamina máxima mientras se lleva.

**Bola / piedra (`Throwable`)** — `HandleUseThrowable` → `ATN_ThrowableItemActor`.
- Sale a **1800 cm/s** con el ángulo común de los lanzamientos (25° ± 40 % de la cámara, 10°–45°), girando a 360°/s, desde 120 cm por
  delante y 40 arriba; ignora a quien la lanza. La trayectoria es **determinista**: todas las máquinas parten de las mismas
  condiciones y no se replica su posición.
- Rebote (`Bounciness` 0,5612 [BP], `RollingFriction` 0,714 [BP]); vida máxima 8 s.
- **Derriba a una tortuga** si la toca a 600 cm/s o más (2 s, `KnockbackDuration`); una sola vez por lanzamiento y jugadora; no
  a quien la lanza. **Marea a un enemigo** (§27.8). Si va más lenta, solo rebota.
- **Al pararse** (o al acabar su vida) se convierte otra vez en un pickup en el sitio, con el brillo de lo que se coge: **la
  bola se reutiliza**.
- Peso 2: −40 de estamina máxima.

**Cabezota (`BigHead`)** — `HandleUseBigHead`.
- La cabeza se escala ×3,5 (`BigHeadScale`) durante **8 s** (`BigHeadDurationSeconds`); se replica a todos.
- **En el cooperativo**: la próxima gaviota que la pique **no mata**, se lleva la cabezota (`RemoveBigHeadEffect`) con un efecto
  y un sonido. Si además hay una sombrilla abierta, manda la sombrilla (la gaviota se retira y la cabezota se conserva).
- **Al acabarse (o perderse)**: **mareo de 3 s** (`MareoDurationSeconds`) con la velocidad limitada a **250 cm/s**
  (`MareoSpeedCap`) y un evento de mareo para efectos locales (cámara, audio).
- En la playa no protege de nada (las gaviotas de la carrera no la miran): solo la cabezota y el mareo.
- Peso 2: −40 de estamina máxima.

**Concha trampa (`Conch`)** — `HandleUseConch` → `ATN_ConchPickup::PlaceAsTrap`.
- Se coloca **en el suelo, justo debajo de la tortuga**, y se arma (efecto y sonido, hoy sonidos de relleno del motor).
- **Cualquier tortuga que la pise** (radio de 60 cm, `TrapRadius`) queda **inmovilizada 2,5 s** (`TrapDurationSeconds`,
  `DisableMovement`); un enemigo que la pise queda aturdido 2,5 s. El código **no exime a quien la puso**.
- Tras usarse, la concha se convierte en un pickup nuevo en el sitio (se puede reciclar); la variante persistente se rearma
  tras 1 s.
- Peso 1: −20.

**Tinta de calamar (`InkThrower`)** — `HandleUseInkThrower` → `ATN_InkProjectile::Spawn`.
- Proyectil de 1200 cm/s con gravedad 980, radio 20 cm, vida 4 s, con el ángulo común de los lanzamientos.
- A una **tortuga**: superpone el material de tinta a **su pantalla 5 s** (`InkDurationSeconds`, solo en la máquina de la
  afectada). **Hallazgo**: el Blueprint asigna al efecto `InkOverlayMaterial` el `DefaultPostProcessMaterial` del motor,
  que es un material en blanco: si es así, **la tinta no tapa nada** hoy. No confirmado en juego.
- A un **enemigo**: en la playa lo marea 3 s; en el cooperativo lo ciega 5 s (`ApplyBlind`).
- Peso 0,5: −10.

**Tótem (`Totem`)** — §22.1. Con uno en el inventario, la muerte se cancela sola. Usado a mano, revive a una eliminada al
azar. Peso 5: **−100 de estamina** (la mitad), por lo que se lleva con mala idea. En la carrera no sale (nadie muere).

### 28.3 Objetos del mundo relacionados (no van al inventario)

| Objeto | Qué hace | Cifras | Origen |
|---|---|---|---|
| **Cáscara de plátano** (`ATN_BananaPeel`) | Al pisarla, derriba y empuja en su dirección | Derribo 2 s; empuje ≥ 350 cm/s y 400 hacia arriba | [C] |
| **Sombrilla** (`ATN_UmbrellaInteractable`) | E la abre: protege de la gaviota y de su caca. E con ella abierta la cierra; se puede volver a abrir al instante. No se gasta | **8 s** abierta | [C] |
| **Tótem del nivel** (`ATN_TotemInteractable`) | Activarlo revive a una eliminada al azar, que aparece a 150 cm; se destruye tras un uso (configurable) | 150 cm | [C] |
| **Rescate** (`ATN_RescuePickup`) | El cuerpo de una eliminada: E la revive donde está (§22.3) | 300 cm | [C] |
| **Concha trampa colocada** | Ver arriba | 2,5 s · radio 60 | [C] |
| **Conchas de puntos** (`ATN_ScorePickup`) | Dan puntos al pasar (§30) | 1/25/50/100 | [C] |

La sombrilla solo la comprueban las gaviotas y su caca (`ATN_EnemySeagull`, `ATN_SeagullDroppingActor`,
`ATN_BeachGullZone`); el comentario de `ATN_StormVolume` dice que también protege de la tormenta, pero **el código no lo
mira**. En la playa las sombrillas clavadas son solo decorado (se rebuscan).

### 28.4 Dónde salen los objetos de siempre

| Fuente | Modo | Reglas |
|---|---|---|
| **Zonas de objetos** (`ATN_ItemSpawnZone`) | Clásico, chunks | Al empezar, en el servidor: `SpawnCount` (5 por defecto, 1–50) objetos elegidos al azar entre `ItemRowNames`, separados 100 cm como mínimo, sobre suelo válido, hasta 10 reintentos |
| **Rebuscables** (mantener E, `ATN_ProcSearchSpot`) | Cooperativo | 1,3 s manteniendo E; **55 % de suerte**; una vez para todo el grupo; peso 1 para cada fila con uso, **0,3 para el tótem** |
| **Cofre del lobby** (`ATN_TreasureChest`) | Lobby | 5 s manteniendo E; **siempre** sale algo; las veces que se quiera con 2,5 s de respiro; hasta 6 objetos sin recoger |
| **Lo que se suelta o se lanza** | Todos | Soltar (X), la bola que se para, la concha reciclada |
| **Rebuscables, cajas, cofres, lagarto** | Carrera | Con otros pesos y con los objetos de carrera (§29.5) |

**Probabilidad por rebuscada en el cooperativo** [calc]: `0,55 × peso / 5,3` (la suma de pesos de las seis filas con uso: 1 + 1 + 1 + 1 + 1
+ 0,3): **10,4 %** cada una de barrita, bola, cabezota, concha y tinta, **3,1 %** el tótem, y un 45 % de «¡pof!».

Rebuscar y el cofre: marca común de lo que se coge (anillo dorado que gira en el suelo, columna de luz de 3,6 m visible a 150 m,
chispitas a menos de 30 m, luz a menos de 18 m; el objeto sube 14 cm, flota ±6 cm y gira a 0,2 vueltas/s a menos de 40 m;
`UTN_PickupGlowComponent`, [`Docs/Botin_Decorados.md`](Botin_Decorados.md)). En los rebuscables, el anillo está fijo
alrededor del decorado y abarca su huella (en la playa, centrado en su montículo de arena), sin seguir a nadie.

## 29. Objetos de carrera

Por petición del usuario, la carrera de la playa tiene su **propio juego de objetos, copiados en parte de las carreras de karts**:
turbos, un pelícano que te lleva por delante, un protector solar que hace de estrella, proyectiles que persiguen, una mina, un
rayo y un bumerán. Se reparten **según la posición**: a las de atrás les tocan los que las hacen remontar y a las de delante, lo
que se lanza y lo defensivo. Código: `World/Beach/TN_RaceItems` (catálogo, puesto, sorteo, uso), `TN_RaceItemComponent`,
`TN_RaceItemActor`, `TN_RaceItemBox`, `TN_RacePelicanTaxi`, `TN_RaceHomingCrab`, `TN_RaceGullStrike`, `TN_RaceMine`,
`TN_RaceFrisbee`, `TN_RaceStormCloud`, `TN_RaceBurstFX`, `TN_RaceItemSynth` (21 sonidos), `TN_RaceItemArt` (mallas e iconos).
Documento: [`Docs/Modo_Carrera.md`](Modo_Carrera.md), «Objetos de carrera».

### 29.1 Cómo se definen

**Solo desde código.** No hay filas nuevas en `DT_Items` ni script del editor. Cada objeto es un `FTN_InventoryItem` con
`UseType == RaceItem`, `ItemId` «Race_<Código>» (`TNRaceItems::MakeItem`), **peso 0** y el pickup base; su malla y su icono se
construyen en ejecución en cada máquina a partir del `ItemId` (`TNRaceItems::ResolveVisuals`), así que nada de eso se replica
ni se guarda. Solo hay objetos de carrera en la carrera de la playa (y en las pruebas con `TN.Race.Item`).

### 29.2 Uso: reglas comunes

- Se usan con **E sin interactuable a mano** (§23.1). El servidor manda (`TNRaceItems::ServerUse`) y todos ven los efectos.
- **No se pueden usar** (suena el «nop» y **el objeto se queda**) si la tortuga está: muerta, derribada, **metida en el caparazón**,
  aturdida, **en el pico de un enemigo**, comida por el gusano, recolocada por la tormenta o la red de seguridad, **volando en
  el pelícano**, **llevando o llevada**, o con la carrera parada (recuento de conchas, título del sprint, podio, y tras «¡TIEMPO!» o «¡TODAS AL AGUA!»; `ATN_BeachEnemy::IsRaceLive`). Tampoco
  si el propio objeto no tiene a quién ni dónde (cada uno lo dice abajo).
- Al usarse con éxito el objeto se **gasta**; el **triple coco** pasa a tener un uso menos (la mano cambia de icono sin tocar
  el caparazón).
- Una caja de objetos solo se puede coger si **cabe** (hay una ranura libre): si no, E usa el objeto de la mano.

### 29.3 Los objetos

| Objeto | Código (`ETNRaceItem`) | Se parece a | Alias de consola |
|---|---|---|---|
| Coco turbo | `Coconut` | champiñón | `coco`, `turbo` |
| Triple coco | `TripleCoconut3` (2 y 1 son lo que queda) | triple champiñón | `triple`, `triple2`, `triple1` |
| Coco dorado | `GoldenCoconut` | champiñón dorado | `dorado`, `oro` |
| Pelícano taxi | `PelicanTaxi` | bala | `pelicano`, `taxi` |
| Protector solar | `Sunscreen` | estrella | `protector`, `estrella` |
| Cangrejo teledirigido | `HomingCrab` | concha roja | `cangrejo`, `roja` |
| Gaviota justiciera | `GullStrike` | concha azul | `gaviota`, `azul` |
| Mina de arena | `SandMine` | bob-omb | `mina`, `bomba` |
| Nube de tormenta | `StormCloud` | rayo | `nube`, `rayo` |
| Disco volador | `Frisbee` | bumerán | `disco`, `boomerang` |
| Silbato del sargento | `Whistle` | (propio) | `silbato`, `sargento` |
| Caja de objetos | `Box` | caja «?» | `caja` |

### 29.4 Qué hace cada uno, con cifras y con lo que no se puede

Las velocidades dependen de la de correr: se dan con el **Blueprint** (400 cm/s) y, entre paréntesis, con el **código** (800).

**Coco turbo** — Turbo: la velocidad se multiplica por **2 sobre la de correr** durante **3 s**, aunque no se esprinte
(`TurboMultiplier` 2, `TurboSeconds` 3): **800 cm/s** (1600). La aceleración se triplica para que el empujón sea inmediato, y la
cámara de quien lo usa se abre +16°. Estela de rayas y arena, luz cálida y «¡fiuuum!». *Encadenar cocos* alarga el final al
mayor de los dos (no lo suma). Sin restricciones propias.

**Triple coco** — Tres turbos: cada uso gasta uno y el icono pasa de 3 cocos a 2 y a 1. Solo sale con 3 usos; los de 2 y 1 no
salen del sorteo.

**Coco dorado** — Turbo ×2 durante **7 s** (`GoldenSeconds`) **con energía sin fin y sin el cansancio de después** (pone la
penalización posterior a 0): turbos sin parar. Chispas doradas.

**Pelícano taxi** (`ATN_RacePelicanTaxi`) — Un pelícano gigante (24× de escala, 41 m de envergadura) baja en picado desde atrás, te
coge por el caparazón y te lleva **volando por delante de todas 120 m a 22 m/s** y te suelta **de pie en arena abierta**.
Línea de tiempo desde el uso: 1 s de aproximación (la tortuga queda sujeta y pataleando donde está mientras el pelícano baja
desde 45 m atrás y 35 m arriba), 1,3 s de subida a la altura de crucero (el suelo más alto del camino + 26 m), crucero, 1,5 s de
descenso, **suelta a 2,6 m sobre el sitio** (de pie, sin caída larga: se le pone `SetFallImmuneUntilLanded` y 3 s de gracia de
la tormenta) y 2,5 s de despedida; en total **~9,3 s** [calc]. El sitio se decide **una vez** en el servidor: 120 m por delante en la
dirección de la carrera, recortado para quedar a **55 m o más del filo** (no se salta la meta), y se busca con
`TNBeach::FindOpenSandSpot` (arena abierta, llana, cápsula de pie cabiendo, fuera del agua, de las pozas y de las trincheras); si
no hay, se acorta un 20 % hasta 5 veces. *No se puede* con menos de 30 m útiles por delante o sin sitio (cerca de la meta, «nop»),
con 8 taxis en el mundo, ni con uno ya (uno por tortuga). Mientras la lleva es **invulnerable** y **no puede usar objetos ni
meterse en el caparazón**. Si la red de seguridad, un rescate o un gusano le quitan a la tortuga, el vuelo se aborta y se
marcha sin soltarla en ningún sitio. Si se mete en el caparazón en pleno vuelo (se evita en el servidor), cae en bola desde la
altura. Vive como mucho 40 s; el seguro de la sujeción se alarga a 20 s.

**Protector solar** (`Sunscreen`) — **8 s** (`StarSeconds`) *invulnerable* (nada la aturde ni la derriba; los enemigos ni la miran),
con **velocidad ×1,25 de la de correr** (500 cm/s; 1000) y brillo dorado, chispas y una luz que late. **Derriba a las tortugas que
toca** (a menos de 320 cm: ragdoll de 2 s, empujadas 650 cm/s y 320 hacia arriba; **2,5 s de respiro por víctima**) y **marea 4 s
a los enemigos** que toca. Con el turbo puesto, los multiplicadores se multiplican con **tope de 2,4×** (960 cm/s; 1920). Lo que la
mueve sin golpearla (la patada de la tormenta, la red de seguridad) sigue funcionando.

**Cangrejo teledirigido** (`ATN_RaceHomingCrab`) — Un cangrejito rojo de juguete (1,1 m de alto, escala 0,2, con antena de mando
que parpadea) nace 250 cm por delante de quien lo lanza y sale corriendo con saltitos (0,45 s, 120 cm) hacia la **tortuga más
cercana por delante**: 900 cm/s que suben a 1600 en 0,8 s, girando 420°/s. Si la alcanza (radio 260 cm), la **derriba 2,2 s** (empuja 700
cm/s, 400 hacia arriba). Sin tortuga por delante, va a por el **enemigo más cercano por delante** (a menos de 80 m) y lo marea 4 s.
Vive **12 s**; si la víctima tiene el protector, rebota sin efecto. *No se puede* con 10 cangrejos en el mundo ni sin objetivo
(«nop»). Para probarlo con una sola tortuga hace falta un enemigo por delante: `TN.Race.ItemUse HomingCrab`.

**Gaviota justiciera** (`ATN_RaceGullStrike`) — Una gaviota gigante (la de las zonas de gaviotas, 25 m de envergadura) nace 90 m detrás y 70
m sobre **la tortuga que va la primera** (solo si va por delante de quien la lanza), se coloca sobre ella y le suelta una cagada: aviso de
sombra negra que **crece 1,7 s** (`StrikeFallSeconds`); suelta a los **3,2 s** del uso (`StrikeReleaseAge`) y el impacto llega a los **4,9
s**. Mientras tanto el blanco sigue a la víctima como mucho a **600 cm/s** hasta soltar y a **450 cm/s** mientras cae, y **se queda quieto
los últimos 0,5 s** (`World/Beach/TN_BeachGullTuning.h`, ronda 4; antes 700 cm/s hasta el golpe). Si alcanza (radio de impacto de **240
cm**, antes 330), **derriba 2,6 s** (empuja 260 cm/s, 150 arriba) y deja la mancha en la arena (8 s) y el pegote en el caparazón (8 s). **La
plancha en el momento justo la libra** (en el aire o arrastrándose a 250 cm/s o más; en el registro, `esquiva la gaviota justiciera en
plancha`), igual que a la cagada de la zona. Con las velocidades reales el blanco es más rápido que quien corre (400): solo se libra con la
plancha o con el turbo del coco (800). Sin líder por delante, va a por el **enemigo más cercano por delante** (a menos de 120 m) y lo marea
4 s. Se va a los 9 s. Máximo 3 a la vez; **no sale con menos de 2 corredoras**. *No se puede* sin objetivo.

**Mina de arena** (`ATN_RaceMine`) — Se lanza hacia donde mira la cámara a **28°** y **1500 cm/s**, desde 120 cm por delante y 60 por
encima: vuela con gravedad ×1,3, rebota **una vez** (0,35) y queda quieta (solo mira el suelo, no choca con el decorado). Se arma a
los **0,9 s** (pitido y luz roja que late cada vez más deprisa) y **salta cuando se acerca una tortuga a 300 cm** (la de quien la
lanzó, pasado 1,5 s) o un enemigo (a menos de 250 cm de su cuerpo): mecha de **0,35 s** y explosión de radio **550 cm** que **aturde en bola 3 s** a las tortugas
(las lanza 420 cm/s hacia fuera y 950 hacia arriba) y **marea 5 s** a los enemigos a menos de 13 m de su cuerpo. Explota sola **10 s** después
de armada (avisa con el parpadeo los últimos 3 s). Cráter de 3,5 s. Máximo **12** a la vez. *No se puede* nada más allá de las reglas comunes.

**Nube de tormenta** (`ATN_RaceStormCloud`) — Una nube negra crece 0,9 s a 20 m sobre **cada otra tortuga en carrera** que se pueda
golpear (1,1 s de aviso con sombra, truenos y tres pitidos) y le cae un rayo que la **aturde en bola 2,2 s**. Con el protector puesto
el rayo cae 320 cm a un lado sin efecto. *No se puede* sin al menos otra víctima, ni con 2 nubes en el mundo; **no sale con menos de 2
corredoras**.

**Disco volador** (`ATN_RaceFrisbee`) — Sale hacia delante a **6°**, dibuja un arco (**26 m**, curvado 7 m), gira y **vuelve a la mano** de
quien lo lanzó: 1,25 s de ida, 0,25 s de giro y 1,35 s de vuelta (**2,85 s**); a 90 cm de una tortuga la **derriba 1,9 s** (empuja
550 cm/s, 300 arriba; una vez por pasada), a los enemigos los **marea 4 s**; **no golpea a quien lo lanza**. Máximo 6 en el mundo.

**Silbato del sargento** — **Aturde en área a los enemigos**: todos los que estén a menos de **55 m** se marean **5 s** (con
pajaritos), sin apuntar; onda de silbato que se expande por el suelo. Siempre se puede usar (aunque no haya enemigos). Los enemigos
que no aceptan el mareo (los quads) no se enteran.

**Caja de objetos** (`ATN_RaceItemBox`) — No se lleva: es el pickup del suelo. Cubo de juguete de colores con una «?» que flota
y gira. Al cogerla (E, solo si cabe) sale un objeto **sorteado según el puesto de quien la coge** (y suena su «caja»). **No
reaparecen** durante la ronda y no dicen lo que dan.

### 29.5 Pesos por puesto

El puesto sale de lo que ha avanzado cada tortuga en carrera por la playa (`ATN_BeachRaceGenerator::GetCourseProgress`;
`TNRaceItems::GetRank`): `Place` = cuántas van por delante; **`Norm` = `Place` / (corredoras − 1)** (0 = va la primera, 1 = la
última; 0,5 si va sola). Cada objeto tiene tres pesos (primera / a medias / última) que se **interpolan linealmente** entre `Norm`
0, 0,5 y 1. En el **cofre** se multiplican por un factor que sesga a lo mejor. «Mín.» es el número de corredoras sin el cual no
sale.

| Objeto | Primera | A medias | Última | Cofre × | Mín. |
|---|---|---|---|---|---|
| Coco turbo | 2,0 | 2,2 | 1,4 | 1,0 | 1 |
| Triple coco | 0 | 0,8 | 1,6 | 1,4 | 1 |
| Coco dorado | 0 | 0,1 | 0,9 | 2,0 | 1 |
| Pelícano taxi | 0 | 0,25 | 2,6 | 1,6 | 1 |
| Protector solar | 0 | 0,5 | 2,0 | 1,4 | 1 |
| Cangrejo teledirigido | 0,7 | 1,5 | 1,0 | 1,0 | 1 |
| Gaviota justiciera | 0 | 0,1 | 1,1 | 1,0 | 2 |
| Mina de arena | 1,8 | 1,2 | 0,5 | 0,8 | 1 |
| Nube de tormenta | 0 | 0,2 | 1,3 | 1,0 | 2 |
| Disco volador | 1,4 | 1,3 | 0,9 | 1,0 | 1 |
| Silbato del sargento | 1,0 | 1,0 | 0,8 | 0,6 | 1 |
| *Energía sin fin* (`SelfStaminaBoost`, fila `StaminaBoost`) | 1,0 | 1,5 | 1,8 | 2,0 | |
| *Barra llena* (`SelfStaminaFull`, **sin fila**: no sale) | 1,4 | 1,2 | 1,0 | 1,6 | |
| *Bola lanzable* (`Throwable`) | 1,4 | 1,3 | 0,9 | 1,2 | |
| *Tinta de calamar* (`InkThrower`) | 1,4 | 1,3 | 0,9 | 1,2 | |
| *Concha trampa* (`Conch`) | 1,6 | 1,0 | 0,4 | 0,6 | |
| *Cabezota* (`BigHead`) | 0,3 | 0,3 | 0,3 | 0 | |
| *Tótem* (`Totem`) | 0 | 0 | 0 | 0 | |

(En cursiva, los objetos de siempre de `DT_Items`, por su uso: `TNRaceItems::PositionWeightForUse`. Los triples de 2 y 1 usos no
salen del sorteo.) La probabilidad de cada uno es su peso entre la suma de pesos [calc, con las filas útiles de `DT_Items`: energía sin
fin, bola, tinta, concha y cabezota]:

| Objeto | 1.ª (`Norm` 0) | A medias (0,5) | Última (1) | Cofre 1.ª | Cofre a medias | Cofre última |
|---|---|---|---|---|---|---|
| Coco turbo | 15,9 % | 15,1 % | 7,6 % | 16,1 % | 13,8 % | 5,9 % |
| Triple coco | — | 5,5 % | 8,7 % | — | 7,0 % | 9,5 % |
| Coco dorado | — | 0,7 % | 4,9 % | — | 1,2 % | 7,6 % |
| Pelícano taxi | — | 1,7 % | 14,1 % | — | 2,5 % | 17,6 % |
| Protector solar | — | 3,4 % | 10,9 % | — | 4,4 % | 11,9 % |
| Cangrejo teledirigido | 5,6 % | 10,3 % | 5,4 % | 5,6 % | 9,4 % | 4,2 % |
| Gaviota justiciera | — | 0,7 % | 6,0 % | — | 0,6 % | 4,7 % |
| Mina de arena | 14,3 % | 8,2 % | 2,7 % | 11,6 % | 6,0 % | 1,7 % |
| Nube de tormenta | — | 1,4 % | 7,1 % | — | 1,2 % | 5,5 % |
| Disco volador | 11,1 % | 8,9 % | 4,9 % | 11,2 % | 8,1 % | 3,8 % |
| Silbato del sargento | 7,9 % | 6,9 % | 4,3 % | 4,8 % | 3,8 % | 2,0 % |
| Energía sin fin | 7,9 % | 10,3 % | 9,8 % | 16,1 % | 18,8 % | 15,3 % |
| Bola | 11,1 % | 8,9 % | 4,9 % | 13,5 % | 9,8 % | 4,6 % |
| Tinta | 11,1 % | 8,9 % | 4,9 % | 13,5 % | 9,8 % | 4,6 % |
| Concha trampa | 12,7 % | 6,9 % | 2,2 % | 7,7 % | 3,8 % | 1,0 % |
| Cabezota | 2,4 % | 2,1 % | 1,6 % | — | — | — |

(Con 8 corredoras. Sola, o con `Norm` 0,5 y una sola corredora, no salen la gaviota ni la nube y el resto sube un poco: turbo 15,4 %,
cangrejo 10,5 %, energía sin fin 10,5 %.)

### 29.6 Dónde salen (los tres sitios usan el mismo sorteo, `TNRaceItems::RollLoot`)

| Fuente | Cuántas y cómo | Fuente del sorteo |
|---|---|---|
| **Cajas de objetos** | **20–25 sueltas** por tramo igual del recorrido desde los 90 m hasta 30 m del filo, más **3 filas de lado a lado** de la playa (a ~17, 50 y 83 % del recorrido, un objeto cada 32 m), como las cajas de karts: todas pasan por una. En total ~35–45 por ronda. No reaparecen | `Box` |
| **Rebuscables** (`ATN_BeachSearchSpot`) | El decorado de la playa (hasta 240 por ronda, uno por corrillo). Mantener E **1,3 s**, **70 % de suerte** (55 % en el cooperativo). Una vez para todas | `Search` |
| **Cofres** (`ATN_BeachChestSpot`) | Sitios especiales (tras la concha que atrapa, rincones escondidos, trincheras) y la cima de las fortalezas. E **5,5 s**, siempre premio, una vez por ronda para todas: **un objeto hacia quien lo abre + otro objeto + seis conchas de puntos** | `Chest` (factor de la tabla) |
| **Lagarto generoso** (`ATN_BeachLizard`) | El 30 % de los lagartos; al huir deja «¡UN REGALO!»: **60 %** un objeto de `DT_Items` con `TNBeachLoot::RaceWeight` (energía sin fin 1,5 · bola 1,3 · tinta 1,3 · concha 1 · cabezota 0,3 · tótem 0), si no una concha de puntos de 25 (de 50 el 30 %) | sin puesto; **solo objetos de siempre** |

Los objetos de carrera **no** salen fuera de la carrera de la playa.

Detalle de cada fuente en el §34.11 (cofres), el §34.12 (rebuscables), el §34.13 (conchas) y el §34.14 (cajas).

### 29.7 Invulnerabilidad y efectos sobre las demás

`TNRaceItems::IsInvulnerable(Turtle)` es lo que respetan `StunTurtle`, `KnockDownTurtle`, `ApplyKnockdown` (cáscara de plátano,
bola lanzada…) y `ATN_BeachEnemy::CanBeHit` (los enemigos no la eligen). Solo dan invulnerabilidad el **protector solar** y el
**pelícano** (mientras lleva). Los objetos de carrera comprueban además `TNRaceItems::CanBeHurt` antes de golpear.

### 29.8 Red

Servidor con autoridad en todo. Los actores lanzados (`ATN_RaceItemActor`, siempre relevantes, `Track` replicado a 20–30 Hz y
suavizado en los clientes, reloj del servidor común) y el pelícano (plan replicado una vez, vuelo por fórmulas del tiempo del servidor)
se ven igual en todas. Los efectos sobre la tortuga (turbo, protector, vuelo) van en `UTN_RaceItemComponent`, un componente dinámico
replicado que el servidor añade la primera vez; cada máquina pone el mismo multiplicador de velocidad en su
`UTN_StaminaComponent`, así que dueño, servidor y demás usan el mismo `MaxWalkSpeed` (puede haber una pequeña corrección de
movimiento al empezar o acabar por la latencia). 21 sonidos sintetizados (`UTN_RaceItemSynthComponent`) y efectos locales
(`ATN_RaceBurstFX`); nada en servidor dedicado.

### 29.9 Consola de los objetos de carrera

| Comando | Qué hace |
|---|---|
| `TN.Race.Item <objeto\|list> [jugador]` | Da ese objeto a la mano (si está llena, sustituye) |
| `TN.Race.ItemUse <objeto> [jugador]` | Se lo da y lo usa en el acto (si no se puede, suena el «nop») |
| `TN.Race.ItemBox [n]` / `clear` | `n` cajas en fila delante de ti |
| `TN.Race.ItemRank [jugador]` | Puesto en la carrera y peso de cada objeto (rebuscar, caja y cofre) |
| `TN.Race.Boost [s] [mult] [jugador]` | Turbo sin gastar objeto |
| `TN.Race.Star [s] [jugador]` | Protector sin gastar objeto |
| `TN.Race.ItemClear` | Quita lo lanzado y cancela los efectos |

Los nombres valen en inglés, en español o por número. La gaviota justiciera y la nube necesitan a otra tortuga: con un cliente,
`TN.Race.ItemUse GullStrike 1`.

### 29.10 Límites conocidos

Todo este código se escribió sin compilar (puede haber errores de compilación la primera vez); las cajas no reaparecen ni dicen lo
que dan; las minas lanzadas solo miran el suelo; el turbo y el protector van en la predicción del movimiento
(issue #22: sin corrección al empezar o acabar); los efectos se acaban solos al cambiar la ronda; los abortos del
pelícano la sueltan donde estén.

## 30. Puntuación

Hay **dos monedas** que no hay que confundir (tabla comparativa en el §2.8): los **puntos** (`RaceScore`: conchas de puntos + puesto + bonus de tiempo), que se
acumulan en el perfil y son lo que se gasta en la tienda; y las **conchas de la partida** de la carrera de la playa
(`RaceShellHalves`: media concha, concha entera), que deciden quién gana la partida y **no se guardan**.

### 30.1 Conchas de puntos (`ATN_ScorePickup`, `World/TN_ScoreShells.h`)

Coleccionables que se cogen **al pasar** (sin tecla) y suman su valor al `RaceScore` de quien las coge. Cuatro tamaños según
`ScoreValue`; el tamaño sale del valor (hasta 5, pequeña; hasta 37, normal; hasta 75, grande; más, reina).

| | Pequeña | Normal | Grande | Reina |
|---|---|---|---|---|
| **Puntos** | **1** | **25** (la de siempre) | **50** | **100** |
| Aspecto | oro claro | dorada con destellos | nácar turquesa, halo, luz y columna | rosa y violeta con filo dorado, más grande |
| Escala de la vieira | 0,8 | 1,5 | 2,0 | 2,5 |
| Radio de recogida | 48 cm | 60 cm | 72 cm | 80 cm |
| Altura de la malla sobre el centro | 30 cm | 55 cm | 70 cm | 85 cm |
| Giro (vueltas por segundo) | 0,6 | 0,45 | 0,35 | 0,3 |
| Se despiertan (giran y destellan) a menos de | 70 m | 90 m | 250 m | 250 m |
| Color del brillo (lineal) | (1, 0,93, 0,62) | (1, 0,92, 0,55) | (0,45, 1, 0,92) | (1, 0,45, 0,85) |

El centro y la esfera de recogida van a 60 cm del suelo. Al cogerla: el estallido local (destello, chispas y «¡plin!»,
`ATN_ScoreShellBurst`, no fiable) y los **iconos que vuelan al contador del HUD**: `2·√valor` iconos con un máximo de 15
(1 → 1, 25 → 10, 50 → 14, 100 → 15) que suman exactamente el valor; el «pom» sube por una escala pentatónica hasta dos octavas.
`bRespawn` (apagado por defecto) permite que reaparezcan a los 10 s. Con ocho jugadores están **dormidas** y son relevantes a
200 m.

**Dónde están**: el reparto en el mapa procedural del cooperativo, en el §32.13; el de la playa (topes por ronda de **200 de 1, 27 de 25, 8
de 50 y 2 de 100**, cofres y lagarto generoso), en el §34.13 y el §34.11.

### 30.2 Puntos por puesto (`RaceScore` al llegar a la meta)

`ATN_RunGameMode::MarkPlayerFinished` (la usan el clásico, el mapa procedural y, por debajo, la carrera de la playa). Al cruzar
la meta, a lo que ya lleva (conchas y bonus de zonas) se le suman **puesto + bonus de tiempo**:

| Puesto | 1.º | 2.º | 3.º | 4.º | 5.º | 6.º | 7.º | 8.º | 9.º o más | Eliminada |
|---|---|---|---|---|---|---|---|---|---|---|
| **Puntos** | **400** | **300** | **200** | **100** | **80** | **65** | **55** | **50** | 50 | 0 |

(`RankScoreTable` de `TN_RunGameMode.cpp`; antes eran cuatro puestos y «el resto 50»; las eliminadas no consumen puesto.)

**Bonus de tiempo**: `max(0, floor((120 − tiempo de llegada) × 5))`: 5 puntos por cada segundo por debajo de **120 s**
(`TimeBonusBaselineSeconds`, `TimeBonusPointsPerSecond`); máximo 600 si se llegara en 0 s [calc]. En la carrera de la playa el
tiempo se cuenta **desde el inicio de cada ronda** (`MatchStartServerTime` se reinicia), pero una ronda dura minutos, así que el
bonus es **0 en la práctica** [calc].

`FinishRank` (1, 2, 3…) se asigna por el orden en que cada tortuga pasa por `MarkPlayerFinished`. En la playa eso ocurre 0,8 s
**después** del contacto con el agua (§22.6). La tabla de resultados del HUD ordena por puesto, luego las sin puesto y las
eliminadas al final (`TNScoreLogic::ComputeResultSortKey`).

### 30.3 El `RaceScore` en el tiempo

| Cosa | Comportamiento |
|---|---|
| Arranque | `ResetForNewRace` lo pone a 0 al empezar cada carrera y **cada ronda** de la carrera y del 2 vs 2 |
| Revivir | **No** lo borra: conserva las conchas ganadas |
| Cuándo se guarda en el perfil | `ATN_CoopGameState::PersistLocalPlayerScoreIfResults`: al entrar en el estado **Resultados**, solo para el jugador local, **por diferencia** (idempotente: `RaceScore − PersistedScoreThisRace`) y se reinicia al salir de Resultados |
| En la carrera de la playa | Resultados = la pantalla del campeón. Como el `RaceScore` se pone a 0 al empezar **cada ronda**, lo que se guarda es lo de la **última ronda** (conchas cogidas en ella + puntos por puesto); lo de las rondas anteriores no se guarda [deducido del código, no probado] |
| Cooperativo | Una ronda por partida (`CoopRounds` = 1): se guarda todo lo cogido más los bonus de zona de meta |
| Bonus de zonas | `GoalReachedBonusScore` de una zona de meta: se suma a todas las activas (no eliminadas ni llegadas) |

### 30.4 Media concha y conchas de la carrera (`RaceShellHalves`)

Solo en la carrera de la playa (`ATN_BeachRaceGameMode`, [`Docs/Modo_Carrera.md`](Modo_Carrera.md)). **Dos medias = una concha entera.** Gana la
partida quien llega primero a **3 conchas (6 medias)** (`WinsToWinMatch`).

| Momento | Qué se lleva |
|---|---|
| Primera en tocar el agua de la meta | **+2 medias** (concha entera) y suma también `RoundWins`; arranca la **cuenta atrás de 10 s** |
| Cada una que llega en los 10 s | **+1 media** (por orden de llegada, `RoundHalfShells`) |
| Las que no llegan | Nada; al acabar los 10 s, **«¡TIEMPO!» y un gusano de arena se come** a cada una que seguía corriendo (no en el límite de ronda ni en el sprint) |
| Todas dentro antes de los 10 s | «¡TODAS AL AGUA!» sin gusanos |
| Nadie llega en 9 min | «¡TIEMPO!» (`TimeLimit`): **gana la que va más cerca del mar** |
| Empate en lo más alto con 3 o más conchas | **Sprint final** (4 min 30 s de límite): solo las empatadas corren desde la mitad de la playa; la primera en el agua gana |
| Podio | Por conchas en medias; a igualdad, quien ganó una ronda más tarde |

En el recuento (7 s) cada jugador tiene tres huecos de concha (cada uno, dos medias) y vuela la entera de la ganadora; después,
las medias de la cuenta atrás saltan una a una (0,35 s). En el 2 vs 2 y la carrera procedural cuenta `RoundWins` (gana quien llega
a 3), no las medias.

### 30.5 Salto final al agua (mecánica de la tortuga)

La meta de la playa es un **acantilado de 15,5 m** (`TNBeach::CliffHeight`): se salta y se gana al tocar el agua.

- La tortuga se mete sola en bola a los 5 m de caída (§14.2), pero el salto del acantilado **no**: dentro de
  `IsCliffJumpZone` (los últimos 7,5 m de la repisa y el vacío sobre el agua) la caída pasa a inmune hasta tocar suelo o agua
  (`GuardCliffJump`, diez veces por segundo).
- **Zambullida de cabeza** (`PoseCliffDive`): si despega o empieza a caer y, en los primeros 0,35 s de la caída
  (`CliffDiveStartWindow`), está en la zona, se pone de cabeza (cuerpo estirado, brazos por encima con las manos juntas, piernas
  juntas) y gira sobre la cadera siguiendo la trayectoria (de 40° a 165°). Se acaba al aterrizar, al nadar, en plancha, en
  bola, derribada o llevada. Es cosmética y local.
- Al entrar en el agua: corona de gotas, chorro y «¡chof!» sintetizados; se queda 0,8 s a la vista con la postura de la zambullida congelada (§22.6).

### 30.6 Llegada al agua: «Has quedado X.º» y paso entre rondas

Cuando la tortuga del jugador toca el agua de meta, no se pone de pie ni se vuelve fantasma a la vista: el **huevo negro** (la cáscara
oscura del fantasma, `UTN_GhostHatchWidget::ShowCurtain`) se cierra sobre su pantalla y, en la pantalla negra, sale el puesto con un premio y un
mensaje gracioso ([`Docs/Modo_Carrera.md`](Modo_Carrera.md), «Llegada al agua»; [`Docs/Fantasma_Espectador.md`](Fantasma_Espectador.md), «La cáscara oscura en la carrera»).

- **Puesto** (servidor, `ATN_BeachRaceGameMode::MarkPlayerFinished`): el de siempre, en el instante del contacto (1 la primera, 2, 3… las de la
  cuenta atrás; en el sprint, la ganadora es la 1). Se replica en `ATN_BeachRaceGameState::RoundArrivals` (`FTNBeachRoundArrival`:
  `PlayerState` y puesto). Solo cuentan las llegadas de verdad: la concha del tiempo agotado no entra. Se vacía al preparar cada ronda y
  con «Volver a jugar».
- **Pantalla de quien llega** (`UTN_RaceScreensSubsystem`, solo en la suya): en cuanto su tortuga entra en el agua de meta (lo mismo que mira el
  servidor, sin esperar a la red) la cáscara entra desde arriba y desde abajo y se cierra en **0,28 s** con un «¡clac!», mientras su tortuga
  sigue en el agua con la postura de la zambullida congelada y la cámara con ella. Cerrada, y con el puesto del servidor, sale **«HAS QUEDADO
  X.º»** (`UTN_RaceArrivalWidget`, Z 51): la cinta de la ronda («RONDA N» o «SPRINT FINAL»), el puesto enorme del color de su medalla, el premio,
  que cae desde arriba y se aplasta al posarse, su nombre y el mensaje. Dura **2,7 s** (unos 3 s de pantalla negra); detrás, el servidor ya la
  ha ocultado y pasado a espectadora (a los 0,8 s). Al acabar, la cáscara se rompe (fogonazo, mitades despedidas) y se ve el **fantasma
  espectador** siguiendo a otra tortuga, el gusano comiéndose a las que no llegaron, o el recuento o el podio si ya han salido.
- **Si llegan todas**: la cáscara espera tapada (como mucho 2,5 s más) a que salga el recuento y se rompe directamente sobre él; el servidor
  retrasa el recuento hasta que acaba la pantalla de la última en llegar (`ArrivalScreenHoldSeconds` = 3,3 s desde su llegada; antes 1,6 s).
  Si el puesto no llega en 1,6 s (no era una llegada), la cáscara se funde y no pasa nada. En el **sprint final**, la ganadora ve su «1.º» con la
  cinta «SPRINT FINAL» mientras debajo entra la pantalla del campeón, y la cáscara se rompe sobre el podio.
- **Premios y mensajes**: dibujados en código (`Private/UI/Race/TN_RaceArrivalArt.h`, pegatinas del estilo del HUD), con un mensaje al azar entre
  los de su puesto (`NSLOCTEXT` del espacio `TNRace`, claves `Arrival1a`…`Arrival8c` y `Prize1`…`Prize8`). El puesto se escribe con
  `FText::Format("{0}.º", puesto)`. Las texturas se dibujan de antemano mientras se juega (`WarmArt`, una por fotograma).

| Puesto | Premio (tamaño a 1080 p) | Efectos | Mensajes (uno al azar) |
|---|---|---|---|
| 1.º | Corona de oro de cinco puntas con gema coral, dos turquesa y aro de piedras (enorme, 520 px) | Rayos dorados que giran, brillo, lluvia de confeti y destellos; fanfarria cuya nota larga cae con la corona y «¡plin!» de la concha reina | «¡Reina de la playa! Hasta las gaviotas te hacen reverencias.» · «¡Primera! El mar te esperaba con la alfombra de espuma puesta.» · «¡Oro! Los cangrejos ya están tallando tu estatua de arena.» · «¡Nadie te ha visto ni la cola! Esa corona te queda de miedo.» |
| 2.º | Corona de plata de tres puntas con una gema turquesa (330 px) | Rayos plateados, destellos y algo de confeti; «¡plin!» | «¡Casi! La arena aún quema de tus pasos.» · «¡Plata! A un aletazo del oro: la próxima es tuya.» · «¡Segunda y brillando! Hasta el sol se ha puesto celoso.» |
| 3.º | Corona de bronce enana, con una punta doblada, abolladura, grieta y los huecos de las gemas vacíos (168 px) | Rayos cobrizos pequeños y destellos; «¡plin!» más corto | «Podio. La corona es pequeña; el orgullo, no.» · «¡Bronce! Te cabe en una aleta, pero es toda tuya.» · «Tercera: corona de bolsillo, sonrisa de campeona.» |
| 4.º | Cubo de playa del revés (por corona), turquesa con borde amarillo, asa coral y estrella de mar | Se tambalea y se queda torcido; saltan granos de arena; «pom… pom» que bajan | «Cuarta. El cubo también es un trono… de arena.» · «Cuarta. A un paso del podio, con un cubo de sombrero.» · «Cuarta. Si entornas los ojos, el cubo casi parece una corona.» |
| 5.º | Media concha rota, sosa, con un mordisco, una grieta y el trocito caído | Foco gris; trombón triste | «Quinta. Ni frío ni calor: templadita.» · «Quinta. Media concha rota, medio aplauso.» · «Quinta. Justo en el medio, donde nadie mira.» |
| 6.º | Flotador pinchado a rayas, arrugado, con parche y el aire que se escapa | Se va desinflando; nubecita gris que le llueve encima; trombón triste más grave | «Sexta. Hasta el flotador se rindió antes que tú.» · «Sexta. Llegas desinflada, como tu premio.» · «Sexta. Psssss… eso que se escapa es tu orgullo.» |
| 7.º | Calcetín mojado lleno de arena, con agujero, gotas y el tufillo que sube | Se balancea; nube y lluvia; trombón más grave | «Séptima. El gusano ya había reservado mesa.» · «Séptima. Tu premio huele igual que tu carrera.» · «Séptima. Un calcetín con arena: útil para… nada.» |
| 8.º | Alga de peluca chorreando, con vesículas y una conchita enganchada | Mustia; lluvia más fuerte; el trombón más grave | «Octava. Las gaviotas ya te llaman por tu nombre.» · «Octava. Te has traído medio mar enganchado a la cabeza.» · «Octava. Última, pero has llegado. Poca cosa, pero algo.» |

El número del puesto va en oro, plata y bronce en el podio y, abajo, en arena y grises cada vez más apagados y más torcido. Los nombres de los
premios son «Corona de oro», «Corona de plata», «Corona de bronce (talla mini)», «Cubo de playa (del revés)», «Media concha rota», «Flotador
pinchado», «Calcetín mojado con arena» y «Alga de peluca». El trombón triste es un aviso del sintetizador de las pantallas
(`ETNRaceCue::SadTrombone`: si bemol, la, la bemol y un sol largo con sordina que tiembla y se cae; más grave cuanto peor).

**Paso entre rondas** (misma cáscara, sin tocar el servidor). Al pasar a `Waiting` desde el recuento, el título del sprint o el podio («Volver
a jugar»), la cáscara se cierra en 0,32 s sobre la pantalla que se va (no en la primera ronda tras el viaje: esa la tapa el huevo de la pantalla
de carga). Encima entra **«RONDA N»** en dorado (o **«SPRINT FINAL»** en coral) con un «¡pum!» y trocitos de cáscara, y debajo una frase:
«¡Esta ronda puede coronar a una campeona!» si a alguien le falta una concha o menos, «La primera en el agua se lleva la partida.» en el
sprint, «Partida nueva: todas las conchas a cero.» en la ronda 1 de «Volver a jugar» y, si no, una al azar («La playa se ha vuelto a
desordenar. ¡A por el agua!», «Las gaviotas han vuelto con hambre.», «Trampas nuevas, arena de siempre.», «La tormenta ya se está
peinando.», «Nadie se acuerda de la ronda anterior. Bueno, casi nadie.»). Mientras el servidor prepara la ronda, «Colocando la playa…»
latiendo. Con las tortugas ya en sus huevos empieza la cuenta de salida de siempre (3 s): por cada número la cáscara da un «pum» desde dentro
con su grieta de luz y el 3, el 2 y el 1 saltan en un medallón (crema, dorado y coral). Al dar la salida la cáscara se rompe, el título sale
disparado y la tortuga sale de su huevo con la pausa de 1 s de siempre y el «¡ADELANTE!» de la pantalla de carga encima
(`UTN_RaceRoundIntroWidget`, Z 51).

### 30.7 Qué se guarda (perfil del jugador)

| Guardado (`USaveGame`) | Ranura | Contenido |
|---|---|---|
| `UTN_CosmeticSaveGame` | `Cosmetics_Local` (usuario 0; un perfil por máquina) | Cascos desbloqueados y equipado; colores y caparazones desbloqueados; color, caparazón y ojos equipados; **`AccumulatedRaceScore`** |
| `UTN_TutorialSaveGame` | `TutorialState_0` | Si ha completado el tutorial |
| `UTN_SettingsSaveGame` | `TN_Settings` | Ajustes, teclas cambiadas, volumen por compañero, micrófono (versión 3: incluye el idioma y el ojo de pez) |

**No se guarda**: las conchas de la partida (`RaceShellHalves`), `RoundWins`, el inventario, el puesto, las partidas jugadas. No
hay estadísticas ni logros.

## 31. Cosméticos y tienda

Sistema real de cosméticos ([`Docs/Tienda_Probador.md`](Tienda_Probador.md)): la **tienda** de Don Tortugo («La Concha Dorada») y los **probadores** de
botella, en el castillo del lobby (`LVL_Lobby`; se colocan solos si el nivel no los trae, `ATN_HQGameMode::SpawnLobbyShops`).

### 31.1 Categorías (`ETNCosmeticCategory`)

| Categoría | Qué es | Tabla | Cuántos |
|---|---|---|---|
| **Casco** (`Helmet`) | Un objeto sobre la cabeza (socket `Sombrero`, o el hueso `Head` en la coronilla) | `DT_Helmets` | **12** |
| **Caparazón** (`Shell`) | Color, dibujo, brillo y luz propia del caparazón | `DT_Skins` (`Category = Shell`) | **10** |
| **Color** (`Body`) | Color del cuerpo y de la barriga | `DT_Skins` (`Category = Body`) | **12** |
| **Ojos** (`Eyes`) | Tipo de ojo, color del iris o la pupila y luz propia | `DT_Skins` (`Category = Eyes`) | **9** |

Total: 43 cosméticos. Se equipan por separado (`EquippedHelmetId`, `EquippedShellId`, `EquippedSkinId` —el color— y
`EquippedEyesId`). Los caparazones y colores son solo materiales: todo se pinta con `M_TurtleBody` separando las zonas por la
posición local de la malla de demo (`Scripts/build_cosmetics.py`).

### 31.2 Catálogo completo

**Cascos** (`Scripts/cosmetics_meshes.py`, modelados sobre la coronilla): Sombrero de paja (`Helmet_Straw`), Tricornio pirata
(`Helmet_Pirate`), Corona real (`Helmet_Crown`), Gorra de capitán (`Helmet_Captain`), Gorro de marinero (`Helmet_Sailor`), Gorro de
fiesta (`Helmet_Party`), Gorro de hélice (`Helmet_Propeller`), Flor tropical (`Helmet_Hibiscus`), Estrella de mar
(`Helmet_Starfish`), Cangrejo de compañía (`Helmet_Crab`), Sombrero medusa (`Helmet_Jelly`), Aureola (`Helmet_Halo`).

**Caparazones** (`Shell_*`; dibujo, brillo, luz propia): Escamas clásicas (`Scutes`, escamas), Coral con lunares (`Coral`, lunares),
Oleaje (`Waves`, olas), **Oro pirata** (`Gold`, escamas, brillo 1), **Volcán** (`Lava`, grietas de lava, luz 3), **Galaxia** (`Galaxy`,
estrellas, luz 4), Sandía (`Melon`), Tablero (`Checker`, ajedrez), Musgo (`Moss`, lunares ×0,7), Algodón de azúcar (`Candy`, olas ×0,8).

**Colores** (`Body_*`; color y barriga): Azul océano (`Ocean`, `#3A8FD9`), Rosa chicle (`Bubblegum`, `#F28DB2`), Lavanda
(`Lavender`, `#9B6BD6`), Amarillo sol (`Sunny`, `#F4C542`), Rojo coral (`Coral`, `#E8574A`), Menta (`Mint`, `#7FE0C0`), Naranja
(`Orange`, `#F28C38`), Blanco nieve (`Snow`, `#EDEFF2`), Carbón (`Charcoal`, `#3B3F4A`), Arena (`Sand`, `#E3C79A`), Lima (`Lime`,
`#9ED94B`), Verde bosque (`Forest`, `#2E6B3A`).

**Ojos** (`Eyes_*`; `ETNEyeStyle`): Iris azul mar (`Ocean`, iris), Iris esmeralda (`Emerald`, iris), Iris miel (`Honey`, iris), Ojos de gato
(`Cat`, pupila de rendija), Pupilas de estrella (`Star`), Pupilas de corazón (`Heart`), Ojos de dibujo (`Toon`, pupilas enormes),
Hipnóticos (`Spiral`, espiral), **Galaxia** (`Galaxy`, luz 3, brilla en la oscuridad). Los clásicos son los de serie. Los ojos
parpadean y se ponen en espiral al derribarla (§24.2).

Cada fila lleva el texto que dice el tendero. Añadir uno: una receta en `HELMETS` de `cosmetics_meshes.py`, o una fila en `SHELLS`,
`BODIES` o `EYES` de `build_cosmetics.py`, y volver a ejecutar el script (un tipo de ojo nuevo necesita su rama en el HLSL y su valor
en `ETNEyeStyle`).

### 31.3 Precios y desbloqueo

- **Precio (`Price`)**: **0 en las 43 filas** [DT] («hasta que llegue la economía»). La tienda enseña «GRATIS»; con precio
  enseñaría «{0} conchas» y el botón «COMPRAR · {0}».
- **La moneda** es `AccumulatedRaceScore` (los puntos acumulados, §30): la cartera de la tienda enseña ese número.
- **Nada viene desbloqueado**: `DefaultUnlockedHelmets` está vacío. Lo único siempre disponible es «no llevar nada» (el casco, el
  caparazón o el color de serie, `NAME_None`). Para verlo en el probador hay que **comprarlo antes en la tienda** (gratis hoy). Las
  estatuas del lobby (prototipos) desbloquean al equipar (`ForceEquipHelmet`).
- **Comprar** (`UMP_GameInstance::PurchaseCosmetic`): si ya es tuyo, ok; si el precio supera los puntos, falla («te faltan conchas»);
  si no, descuenta, desbloquea y **guarda al momento** el perfil (`SaveCosmeticProfile`).
- La caja de cascos aleatoria (`OpenHelmetCrate`) existe pero su tabla `HelmetCrateTable` está vacía: no se usa.

### 31.4 La tienda (`ATN_ShopKeeper`, `UTN_ShopWidget`)

Al hablar con el tendero se abre la pantalla en el cliente que interactúa: a la izquierda, tu tortuga posando y girando en una peana
(arrastrar con el ratón la gira; escaparate local `ATN_CosmeticPreview` con luces de estudio y una cámara que pinta a una textura,
que también hace las miniaturas); a la derecha, el tendero hablando en su bocadillo («¡Hola, *Nombre*! Pasa, pasa: hoy en La Concha
Dorada todo es gratis…»), cuatro pestañas —**CASCOS, CAPARAZONES, COLORES, OJOS**— y el catálogo con
miniaturas y precio (las de ojos, con primer plano de la cara). Al elegir algo, la tortuga se lo prueba encima de lo que lleva y
saluda. Etiquetas: «PUESTO» (lo que llevas), «¡TUYO!», «GRATIS», «{0} conchas». Botones: «COMPRAR · GRATIS», «¡YA ES TUYO!», «SALIR».
Teclado y mando: flechas o WASD para elegir, Q/E (o gatillos) para las pestañas, Intro para comprar, Escape para salir; el menú bloquea
el control del juego (`FInputModeUIOnly`). Música: la radio del puesto suena en 3D (`Shop`); al abrir el menú, en 2D, y las radios
bajan al 15 %. Consola: `TNShop`.

### 31.5 El probador (`ATN_ChangingBooth`, `UTN_BoothWidget`)

Media botella de cristal de mar de unos 3 m, boca abajo, con la etiqueta «PROBADOR» y el tapón de corona como puerta; **cuatro**
probadores en el lobby. Al entrar, el servidor mete a la tortuga dentro y cierra la puerta (`Occupant` replicado: todos la ven
cerrarse y la botella se menea). La cámara se aleja a la de la botella y sale el menú con **cuatro filas** (CASCO, CAPARAZÓN, COLOR,
OJOS) que se cambian con las flechas; **solo salen los desbloqueados**. «¡LISTO!» pide los cambios (`RequestEquipHelmet`,
`RequestEquipShell`, `RequestEquipSkin`, `RequestEquipEyes`); «CANCELAR» sale sin cambios; en los dos casos la puerta se abre y la
tortuga sale de un saltito. Consola: `TNBooth`.

### 31.6 Guardado y elección en partida

Cada cambio se guarda en el perfil en el acto. Al aparecer el peón (`AMP_GamePlayerController::OnPossess`), el cliente manda al servidor
las listas de desbloqueados (`ServerSyncUnlockedHelmets`, con tope de 50 elementos, y `ServerSyncUnlockedSkins`, con tope de 100) y lo equipado
(`ServerSetEquippedHelmet/Skin/Shell/Eyes`).

### 31.7 Cómo se ven en red

- **El servidor valida**: el casco, el color, el caparazón y los ojos tienen que existir en `DT_Helmets` o `DT_Skins`, ser de la
  categoría pedida y estar en la lista de desbloqueados que ese cliente mandó; si no, se descarta con un aviso en el registro.
- **`ATN_CoopPlayerState` replica** `EquippedHelmetId`, `EquippedShellId`, `EquippedSkinId` y `EquippedEyesId`. Cada `OnRep` llama al
  personaje, que guarda el conjunto (`FTN_TurtleLook`) y lo aplica entero con `UTN_CosmeticLook::ApplyLook`. Hay multicast de
  refuerzo para el casco y el color (quien entra tarde o llega antes que el peón; se reintenta con temporizador hasta que haya
  `PlayerState`).
- El **caparazón manda** sobre la ranura del caparazón que ponga el color. Los cascos y mallas de la tienda usan
  `M_CosmeticVertexColor` (color de vértice; el alfa es el brillo metálico). El podio de la carrera y la pantalla del campeón
  reutilizan el mismo aspecto (`Podium`).
- Los ojos se guardan además en el perfil (`UMP_GameInstance::EquipEyes`) y el servidor los valida contra `DT_Skins`.

# Parte III — El mundo, el decorado, los elementos de juego, las trampas y los enemigos

Todo lo que hay «en el mundo» de Tortunavy: el mapa procedural del cooperativo (`LVL_ProcMap`, §32), la playa fija de la carrera
(`LVL_BeachRace`, §33), su decorado pieza a pieza, los elementos de juego de la playa (§34), las trampas (§35), los enemigos del
cooperativo, de la carrera y del modo clásico (§36) y el reparto por ronda y dificultad (§37). La tortuga, sus mecánicas, los objetos y la
puntuación están en la parte II; el flujo de partida, la red y la interfaz, en la parte I. Las cifras se han contrastado con las
constantes del código (cabeceras y `.cpp`); cuando un documento y el código discrepan manda el código y se anota en el §41.

**Convenciones de esta parte**

| Convención | Significado |
|---|---|
| Rutas entre paréntesis | Relativas a `Source/Tortunabo/Public/` y `Private/`. `X.*` = cabecera y fuente. Las mallas y los sonidos sintetizados de la playa viven en cabeceras privadas (`*Kit.h`, `*Meshes.h`) de `Private/World/Beach/` |
| Unidades | cm en el código; en el texto, metros salvo que se diga. `SizeScale` = tamaño relativo de un ejemplar (1 = nominal) |
| Escala de la playa | `TNBeach::Scale` = 28 (`World/Beach/TN_BeachTypes.h`): la tortuga es una cría de ~5 cm y en el juego mide ~1,4 m; un coco de 15 cm mide 4,2 m y una palmera de 10 m mide 280 m. El cooperativo va a escala 1 (el mapa se mide para una tortuga de ~1,4 m) |
| Velocidades | Las de la tortuga son las reales: **andar 2 m/s, correr 4 m/s** (§13). Las comparaciones «más rápido que la tortuga» de los enemigos y de las trampas usan esas cifras |
| Valores por defecto | En el cooperativo muchos actores son Blueprints (`Content/Blueprints/Gameplay/…`) hijos de clases C++: se dan los valores por defecto del C++ y **el Blueprint puede haberlos cambiado** (los `.uasset` son binarios y no se han podido leer). En la playa todo está en C++ y las cifras son las del código |
| Servidor y clientes | Servidor escucha (el anfitrión juega). «Local» = cada máquina lo calcula por su cuenta con la semilla; «replicado» = viaja por la red |

**Vocabulario mínimo del reparto de la playa**

| Término | Qué es |
|---|---|
| Elemento | Cada cosa que el reparto pone en una ronda (`ETNBeachElement`, 74 tipos: 49 de decorado —23 civiles y de mar, 2 caminos, 17 de basura y cachivaches y 7 militares—, 16 de trampas y ayudas y 9 de enemigos) |
| Spec | `FTNBeachElementSpec`: `Element`, `Seed` (variante), `SizeScale` (0,5–1,6), `Extent` (parámetro propio, p. ej. largo en cm) y `Flags` (`FlagBoosted` = lanzador potenciado). Es lo único que viaja por la red de cada elemento |
| Huella | Radio en planta que ocupa (`TNBeach::FootprintRadius`), sin solapes con otras |
| Núcleo (`Core`) | Fracción de la huella que ocupa de verdad (en enemigos, su cuerpo y su sitio; el resto es por donde patrullan) |
| Asiento | Suelo liso bajo un elemento, a la cota de la arena en su centro |
| X local | Sentido de la carrera: se entra por −X y se sale por +X |

## 32. El mapa procedural del cooperativo (`LVL_ProcMap`)

Un mapa nuevo por partida (y por ronda si se regenera), hecho al vuelo con una **semilla replicada**. Convive con el modo
clásico de chunks (§36.16), que queda intacto. Documento de referencia: [`Docs/Mapa_Procedural.md`](Mapa_Procedural.md).

### 32.1 Cómo se genera

| Pieza | Qué hace | Archivo |
|---|---|---|
| Rejilla de módulos | `GridSize × GridSize` módulos irregulares de **400 m** (`ModuleSize` = 40000 cm), siempre conexos (semillas con *jitter* + Dijkstra multifuente). El camino recorre `Coverage` (0,78: 28 de 36 en 6×6) | `World/ProcMap/TN_ProcMapModules.h` |
| Ruta | DFS aleatorio con Warnsdorff y poda por alcanzabilidad; reserva los pasos de los cruces colosales | `TN_ProcMapRoute.h` |
| Camino | Portales por PCA en las fronteras, «caminante» con meandros (`Sinuosity` 1,8), suavizado Chaikin, anchos por tramos, perfil de alturas con límite de pendiente, ramas y carriles | `TN_ProcMapPath.h` |
| Rasgos | Biomas por regiones, huecos saltables, isletas, pasarelas, pilas de huevos, puzles 2vs2, río, decoración, peligros | `TN_ProcMapFeatures.h` |
| Terreno | Altura por vértice con *domain warp*, pasillo del camino con arcén y taludes de 55–75°, torres, cuevas, volcanes, muros del borde, costa y mar al norte | `TN_ProcMapTerrain.h`, `TN_ProcMapTerrainDetail.h` |
| Cuevas, formaciones, flora, conchas | Lógica pura y determinista | `TN_ProcMapCaves.h`, `TN_ProcMapFormations.h`, `TN_ProcMapFlora.h`, `TN_ProcMapShells.h` |
| Orquesta | `GenerateLayout(params)` valida y reintenta | `TN_ProcMapGenerate.h` |
| Traducción al mundo | `ATN_ProcMapGenerator`: terreno en teselas de `UProceduralMeshComponent` (rejilla de 1,5 m con detalle de 0,5 m donde hace falta: pie y borde de los taludes, crestas, bocas de cueva), agua, estructuras, vegetación instanciada (HISM) y todos los actores de juego | `World/ProcMap/TN_ProcMapGenerator*.cpp` |

Toda la lógica pura vive en `namespace TNProcMap` (solo cabeceras, sin UObjects, determinista); sus pruebas son
`Tortunabo.ProcMap.*` (`LayoutInvariants`, `ModulesConnected`, `Determinism`, `Terrain`, `WallBreaches`, `Shells`).

**Perfiles por modo y dificultad** (`TN_MakeDefaultProcProfile`, `World/ProcMap/TN_ProcMapTypes.cpp`; el asset
`DA_ProcMapSettings` guarda sus propios perfiles y manda si los tiene):

| | Cooperativo (F / N / D) | Carrera del mapa procedural | 2vs2 |
|---|---|---|---|
| Rejilla (módulos por lado) | 3 / 6 / 8 (1,2 / 2,4 / 3,2 km de lado) | 2 / 3 / 4 | 2 / 3 / 4 |
| Cobertura | 0,78 | 0,90 | 0,90 |
| Sinuosidad | 1,8 | 1,6 | 1,5 |
| Cruces colosales | 1 / 2 / 4 | 0 / 1 / 1 | 0 / 0 / 1 |
| Ramas | 6 / 12 / 16 | 4 / 7 / 9 | 2 / 3 / 4 |
| Carriles | 0 | 0 | 1 / 2 / 3 |
| Tormenta (cm/s; gracia en s) | 160 / 180 / 200; 90 / 60 / 45 | sin tormenta | sin tormenta |

Comunes por dificultad (F / N / D): `Difficulty01` 0,2 / 0,5 / 0,9; densidad de peligros (`HazardDensity`) 1,6 / 2,4 / 3,2;
huecos de salto por km (`GapsPerKm`) 9 / 13 / 17; una pila de huevos cada 1 / 2 / 3 cruces de módulo (`EggNestEveryNPortals`).
Otros valores de la estructura del perfil: ancho del camino 4–35 m (`PathWidthMin` 400 / `PathWidthMax` 3500 cm),
`NarrowChance` 0,22, ramas de hasta 3 módulos, tablero de los cruces colosales a 40–55 m (`ColossalHeightMin/Max`
4000–5500 cm). Con 6×6 el camino mide unos 12 km: el registro del generador estima 35–40 min a 5,5 m/s (a propósito, para probar), pero esa
velocidad no es ni la de andar (2 m/s) ni la de correr (4 m/s); con la media real de ~2,8 m/s [calc] serían más de 70 min (§41).

> Nota sobre el menú: «Carrera» del menú lleva a la playa (`LVL_BeachRace`); la carrera y el 2vs2 del mapa procedural
> siguen existiendo (`open LVL_ProcMap?ProcMode=Race|2v2`) pero no son el modo que se ofrece hoy.

**Ver el mapa sin jugar.** En el editor, seleccionar el `ProcMapGenerator` y pulsar *Generate In Editor* (semilla, modo y dificultad en
*ProcMap|Editor*; `bDebugDraw` dibuja camino, ramas y módulos). `bTerrainOnly` genera solo el terreno y lo integrado en el camino (cuevas,
puentes, murallas, huecos, troncos, obstáculos, géiseres y cascadas), sin vegetación, fauna, formaciones, hitos, recompensas, huevos, peligros ni
efectos: lo usan `ATN_TerrainViewGameMode` y el nivel `LVL_ProcMap_Terrain` (`TNRegen [semilla]` lo regenera).

**Red y determinismo.** Solo se replica `FTNProcMapNetConfig` (semilla, modo, dificultad y número de generación); cada
máquina genera el mismo mapa. Los actores que afectan al movimiento (géiser, tobogán, agua, corrientes, remolinos, zonas de
muerte) se crean **en todas las máquinas** para que la predicción cuadre; los que tienen estado (enemigos, huevos, puzles,
meta, PlayerStarts, conchas, rebuscables) solo en el servidor y se replican. Un cliente sin su mapa tiene el peón congelado
hasta avisar con `ServerReportProcMapReady`. Semilla fija para reproducir: `FixedSeed` en el GameMode o
`open LVL_ProcMap?ProcMode=Coop?ProcDifficulty=Hard?ProcSeed=42`.

### 32.2 Biomas (ocho, por regiones de varios módulos)

`ETNProcBiome` (`World/ProcMap/TN_ProcMapEnums.h`): selva, playa, desierto, volcánico, agua con isletas, acantilados
rocosos, manglar y zona humana. Se reparten por regiones contiguas, al azar y con transición natural (mezcla por pesos en
las fronteras); **el último módulo es siempre playa con mar abierto**. Cada bioma tiene su asset `DA_Biome_*`
(`Content/ProcMap/Biomes/`) con colores, capas de decorado, peligros y la criatura flotante del agua; sin asset sale el
*greybox* de `TN_ProcMapTypes.cpp`.

| Bioma | Color del sendero (`TNTrailColor`) | Lo que lo distingue |
|---|---|---|
| Selva | tierra anaranjada clara | árboles de copa, ceibas, bambú; cuevas de selva; puentes colgantes |
| Playa | arena mojada | mar y farallones; ancla, caracola, costillar de ballena; es el bioma de la meta |
| Desierto | arcilla roja | cañones, saguaros, cráneos y obelisco; puentes de piedra y de caballete |
| Volcánico | ceniza rojiza | volcanes con lago de lava; cuevas de tubo de lava; brasas |
| Agua con isletas | tablas oscuras | pozas alrededor del camino, pasarelas e isletas, fauna acuática peligrosa |
| Acantilados rocosos | grava ocre oscura | pinos, abetos, cabras; vagonetas y cañones |
| Manglar | barro claro | mangles de raíces zancudas, secuoyas gigantes, pozas con corriente |
| Zona humana | adoquín pizarra | cajas, barriles, farolas; búnker, torre de vigía, carro de combate |

(El sendero se mezcla un 20 % con el color del asset del bioma; el terreno se pinta con color de vértice —suelo, camino,
roca y lecho: `TN_DefaultBiomeColors`— y `M_ProcTerrain` añade en coordenadas de mundo grano, guijarros y marcas del viento,
sin texturas. Las paredes van en degradado por pendiente y la playa de la meta conserva su arena.)

### 32.3 Vegetación procedural (solo visual, sin colisión)

`bProceduralFlora` (por defecto sí) y `FloraDensity` (× de la densidad). 27 formas low-poly × 3 variantes por bioma (mallas
estáticas hechas en ejecución, `M_ProcFoliage` con viento por el alfa del vértice), en manchas de bosque, sotobosque,
pradera y pedregal, con tamaños muy variados; en los taludes junto al camino, **densidad doble** y enredaderas pegadas a la
pared. **No tiene colisión** (crece fuera del suelo del camino) y usa *culling* por tamaño. Tabla fija por bioma en
`FloraSpeciesFor` (`World/ProcMap/TN_ProcMapFlora.h`); entre paréntesis, instancias por cada 100 m² donde su mancha es plena.

| Bioma | Árboles y grandes | Sotobosque y suelo | Objetos sueltos junto al camino (mancha «rincón», ~25 m) |
|---|---|---|---|
| Selva | árbol de copa (2,8), ceiba (0,15), helecho arbóreo (0,7), bambú (0,35), palmera (0,45), peñasco (0,2) | platanera (1,2), helecho (6), arbusto (4,5), hierba (11), flores (2), enredadera de pared (7) | setas (1,0), vasija (0,35), antorcha tiki (0,6), poste con calavera (0,3), tocón (0,4) |
| Playa | palmera (1,0), casuarina (0,25), peñasco (0,45) | uva de playa (0,8), pándano (0,35), palmito (0,9), hierba (10), arbusto (1,2), piedras (2,5) | concha (1,4), estrella de mar (0,9), cocos (0,5), madera a la deriva (0,4), cubo (0,6), toalla (0,7), sombrilla (0,5), tabla de surf (0,5), salvavidas (0,4) |
| Desierto | saguaro (0,4), árbol de Josué (0,2), acacia (0,15), árbol seco (0,08), peñasco (0,8) | cactus barril (1,6), matojo seco (3,2), hierba (3), piedras (2,5) | planta rodadora (0,5), cráneo de vaca (0,25), huesos (0,3), ánfora (0,3), rueda de carro (0,35), poste indicador (0,3), cristales (0,15) |
| Volcánico | árbol calcinado (0,8), árbol seco (0,3), pino (0,25), peñasco (1,2) | arbusto de ceniza (3,2), helecho (2,4), piedras (3), enredadera (2) | cristales (0,7), tocón (0,8), huesos (0,25), hito de piedras (0,15) |
| Agua con isletas | sauce (0,6), árbol de copa (0,9), abedul (0,35), ciprés en el agua (0,3), peñasco (0,2) | juncos (10), arbusto (3), hierba (8), flores (1,8), enredadera (4) | nasa (0,8), madera a la deriva (0,4), farol (0,4), tocón (0,3), poste indicador (0,25) |
| Acantilados rocosos | pino (1,8), abeto (0,9), abedul (0,3), peñasco (1,3) | arbusto (3,2), hierba (8), flores (1,5), piedras (3), enredadera (3) | hito (0,5), cristales (0,3), tocón (0,3), caja (0,5), barril (0,4), farol (0,4), poste (0,3) |
| Manglar | mangle (3,0, también en agua honda), secuoya joven (0,45), ciprés (0,8), pándano (0,4), helecho arbóreo (0,3), palmera (0,15) | juncos (9), helecho (5), arbusto (3,5), hierba (7), flores (1,4), enredadera (6) | nasa (0,8), madera a la deriva (0,3), farol (0,4), tocón (0,2), barril (0,3) |
| Zona humana | árbol ornamental (0,6), árbol de copa (0,4), abedul (0,25), palmera (0,2) | sombrilla (0,25), seto (0,9), palmito (0,4), arbusto (2), hierba (7), flores (2,5), enredadera (3) | caja (0,8), barril (0,6), sacos (0,5), barricada (0,5), cono (0,7), paca (0,35), banco (0,3), farola (0,6), buzón (0,35), maceta (0,4) |

Además, **34 tipos de objetos sueltos** (`EPropKind`) × 3 variantes: cajas, barriles, vallas de obra, conos, pacas, bancos,
farolas, buzones, sacos y macetas (zona humana); conchas, estrellas de mar, cubos, toallas, tablas de surf, sombrillas,
madera a la deriva, cocos y salvavidas (playa); setas, vasijas, antorchas tiki, postes con calavera y tocones (selva);
calaveras de vaca, huesos, ánforas, ruedas de carro, postes indicadores, plantas rodadoras y cristales (desierto); tocones,
huesos e hitos (volcán); hitos, cristales, cajas, barriles, faroles y postes (roca); nasas, troncos, faroles y tocones (agua
y manglar). Los macizos (cajas, barriles, pacas, bancos, farolas, buzones, vallas…) llevan colisión de caja; el resto, no.
Los peñascos, agujas y mogotes del camino tienen estilo por bioma (peñascos redondos, losas inclinadas, partidos, apilados,
de estratos, columnas de basalto, con musgo, con cristales o de coral; agujas con sombrero, inclinadas, gemelas, chimeneas de
hadas, pilares kársticos, órganos de basalto; mogotes, *tors* de bloques, mesas de estratos y domos de lava) y sí tienen
colisión.

### 32.4 El camino: anchura, saltos, ramas y obstáculos

- **Anchura por tramos** (3,5–60 m): desfiladeros de 3,5–5 m, pasos cerrados de 6–10 m, tramos normales, anchos de 20–35 m y
  explanadas de 40–60 m (más cañones en desierto y roca, más arenales abiertos en la playa). Entre dos partes del camino
  queda siempre un muro de 18 m.
- **Relieve**: lomas de 0,6–2,2 m cada 50–110 m (12–22 m de subida por km; nada en el agua, al salir ni en la llegada). Los
  cambios grandes de altura solo ocurren al cruzar de módulo, con **géiser** (sube) o **cascada-tobogán** (baja).
- **Huecos de salto** del camino principal: 1,3–3,9 m, con labios de madera, sillería o basalto según el bioma; a 30 m como
  mínimo entre sí. Algunos son más largos (hasta 1,8 veces) con **postes** en rejilla que los parten en saltos cortos
  (troncos, pilotes, basalto o columnas) y otros llevan **troncos de equilibrio** de labio a labio. Las ramas arriesgadas
  llevan el doble. Desde Normal, parte de los huecos son **saltos de panzazo** (`EGapStyle::Dive`, 2,7–3,7 m): en el camino
  principal siempre hay al menos uno. El labio de llegada lleva tres chevrones amarillos y rojos y, fuera del camino, un
  cartel con «!». En el manglar las pasarelas van en trozos de 12–32 m con hueco entre trozos el 57 / 67 / 81 % de las veces
  (F / N / D).
- **Medidas de la tortuga que usa el generador** (`TNProcMap::TurtleJump`, `TN_ProcMapLayout.h`): salto 485 cm/s con
  gravedad 980 (sube 1,2 m y está 0,99 s en el aire), andar 200 y esprintar 400 cm/s (1,98 m y 3,96 m de salto en llano), cápsula de 34 cm
  de radio. Son las velocidades reales del Blueprint (§13); el componente de estamina trae 450 / 800 por defecto.
- **Torres de escalada** junto al borde en tramos anchos: bloques del bioma (cajas con aspa, tocones, sillares, losas o
  basalto) de 3–4 m con escalones de 1 m, banderín, recompensa de puntos arriba y una **medusa al pie** para subir de un
  bote. Salen en el 24 % de los turnos de obstáculo de los tramos de 13 m o más.
- **Ramas** (hasta 1–3 módulos, se vuelven a unir): *tranquila* (larga y holgada), *arriesgada* (cornisa de 3,5–5 m con el
  doble de huecos), *ruta alta* (rampa por una loma y tobogán de vuelta) y *rodeo* corto alrededor de un peñasco. Además,
  hasta un tercio de las ramas son **desvíos** largos por módulos vacíos (el módulo deja de ser macizo; por una meseta es un
  desfiladero). En 2vs2 hay **carriles** paralelos con puzles.
- **Módulos vacíos** (`ETNProcEmptyModuleMode`): paisaje elevado inaccesible, ramas y paisaje, explorable o mezcla.
- **Obstáculos de objetos** dentro del camino (18 tipos, con colisión y siempre con carril libre): un turno cada 15–36 m
  (se aparta 24 m de géiseres, toboganes, torres, puertas, cuevas, portales y uniones y 12 m de los huecos). Troncos caídos
  que se saltan en el 50 % de los turnos en selva, manglar y volcán (28 % de troncos a la deriva en la playa); agujas y
  mogotes en el 30 % de las explanadas.

| Bioma | Obstáculos de objetos del camino (`EPathProp`, `TN_ProcMapFeatures.h`) |
|---|---|
| Selva | tótem, columna en ruinas, setas gigantes, tinajas, pila de cajas |
| Playa | castillo de arena, barca volcada, rincón de playa (sombrilla y tumbonas), pila de cajas, grupo de barriles, nasas |
| Desierto | roca calavera, tinajas, barriles, pila de cajas, cristales gigantes, mojón |
| Volcánico | cristales gigantes, roca calavera, mojón |
| Acantilados rocosos | mojón, vagoneta, pila de cajas, barriles, cristales gigantes |
| Zona humana | pila de cajas, barriles, barricada, pacas de paja, puesto de mercado, fila de conos |
| Agua y manglar | ninguno (solo peñascos, agujas y troncos) |

### 32.5 Estructuras colosales

**Cruces a distinto nivel** (`ETNProcCrossingType`): un tramo pasa por encima o por debajo de un módulo ya recorrido. Se llega
por géiser o tobogán y **caerse de un cruce colosal mata**.

- **Torres**: sillería en talud con pretil y almenas. La de entrada es **hueca**: el camino llega en embudo a su puerta (túnel
  recto de 4,6 m de ancho con bóveda, suelo enlosado, portada con impostas y clave, rastrillo levantado y dos antorchas); dentro,
  el géiser lanza en vertical por un hueco del forjado hasta la cima. La de salida lleva el tobogán.
- **Puentes colosales de cuatro estilos** según el bioma del cruce: colgante de cuerda (selva, manglar, agua, playa),
  viaducto de piedra con arcos rebajados (roca, desierto, zona humana), caballete de madera (desierto, playa) y hierro con
  pórticos y cadenas (volcán, zona humana). Los de piedra y hierro tienen a media altura una **plaza** redonda (pretil,
  fuente o farol, bancos y una atalaya de 3 m con recompensa y medusa).
- **Tramos hundidos**: los puentes de más de 42 m entre torres tienen de 1 a 3 tramos sin tablero (uno por cada 22 m útiles;
  el primero de 11–15 m y los demás de 9–13 m, con ≥7 m de tablero entero entre ellos). Se cruzan por **vigas** de 60 cm en
  zigzag, **postes** cuadrados de 1,1 m al tresbolillo (saltos de ~1,3 m) o dos **cornisas** de 60 cm con un hueco de 1,8 m
  y un tablón atravesado. Debajo, cajas de muerte; en el más largo de cada puente hay una concha reina (dos primeros
  puentes) o grande.
- **Murallas con puerta altísima** y **adarve roto** (`TNProcMap::BuildWallBreaches`, `WallBreachDims`): mordiscos solo en lo
  alto (faltan parapeto y adarve; el muro queda hundido 2,8–4,8 m). Brechas que se saltan de 1,19–1,66 m (F), 1,33–1,9 m (N) y
  1,5–2,22 m (D); cornisas de 85 / 78 / 68 cm de ancho y 4,9–7,8 / 5,5–9 / 6,3–10,6 m de largo; hasta 3 / 5 / 7 grupos por
  muralla; los tipos (brecha / cornisa / cornisa y brecha) salen 55/45/0 % en Fácil y 45/35/20 % desde Normal. **Caer mata**
  (cajas de muerte); sobre cada parapeto entero hay barreras invisibles para que nadie se suba a rodear.
- **Géiser** (`ATN_ProcGeyser`, `World/ProcMap/TN_ProcTraversalActors.*`): monte de sínter con poza turquesa; columna de agua
  de 2,6 m en reposo a 10,5 m en el chorro (`JetLow` 260, `JetHigh` 1050, ciclo de 4,2 s); lanza a quien lo pisa hasta un punto
  de aterrizaje (parábola con `ApexExtra` 450); un solo sentido. Local en todas las máquinas (servidor y cliente dueño aplican
  el mismo impulso).
- **Cascada-tobogán** (`ATN_ProcSlideZone`): lámina de agua con flujo (`M_ProcCascade`) y empuje ladera abajo
  (`BoostAcceleration` 700 cm/s²); la caída es inmune (no cuenta como caída larga). Abajo, una poza pegada a la cascada.
- **Volcanes** asentados en la cota del relieve (percentil 75 de un anillo a 3/4 de su radio) sobre una llanura volcánica, con
  cráter y lago de lava (los que envuelven una cueva, de 70–150 m de base); **lava** = zona de muerte (`ATN_ProcKillVolume`).

### 32.6 Cuevas

1–5 por mapa (volcán, roca, selva y desierto): túneles de **120–260 m** en tramos que cruzan terreno alto, con una
**tapa de montaña** encima y una loma. Cada boca se alcanza por un desfiladero de 25–40 m de paredes a plomo. El camino se
estrecha a 11–15 m en la boca; dentro, pasos de 4–7 m y una o dos cámaras de 16–26 m. Cinco interiores
(`TN_ProcMapCaveDecor.h`): **caliza** (estalagmitas, columnas, poza, lucernario), **cristales** (racimos gigantes que
brillan, con colisión), **selva** (raíces, lianas, setas luminosas), **templo** (pilares, antorchas, vasijas, portada con la
cabeza de la tortuga) y **tubo de lava** (obsidiana, basalto, grietas incandescentes). En todas, la estatua de la tortuga con
ofrendas en la cámara más ancha, estelas y símbolos pintados o luminosos y luces sin sombras; nada invade el carril central.
Las del volcán van dentro de un volcán y tienen un río de lava que se salta (caer mata) y una **cámara de magma** (lago de
lava a un lado del camino, mata al tocarlo). Las cuevas no se ponen bajo un tablero colosal.

### 32.7 Agua

- **Pozas, no lagos**: en lagunas y manglar el agua son pozas de 25–60 m alrededor de cada tramo, con acantilado al borde;
  entre tramos alejados (>90 m) y junto a la costa queda tierra alta, así que **no se puede atajar nadando**.
- **Nado** (`ATN_ProcWaterVolume`, `World/ProcMap/TN_ProcWaterActors.*`): un `APhysicsVolume` con muchas cajas, local en todas
  las máquinas; se nada cuando el centro de la cápsula está bajo la superficie. `SwimSpeed` 625 cm/s, flotabilidad 1,08,
  salto nadando para subir a orillas. Agua animada (`M_ProcWaterAnim`).
- **Río opcional** (`bRiver`): los puentes de madera de más de 16 m están **rotos** 3 de cada 4 veces (falta el centro, 7 m);
  se baja por el hueco, se cruza por piedras (cima a 35 cm del agua) o nadando hasta una escalera de madera (peldaños de 30 cm).
- **Isletas y pasarelas** de madera en lagunas; bordes con muros altos irregulares que llevan el contenido del bioma.
- **Criaturas y corrientes**: ver §36.15 (rebotador tipo medusa, corriente, remolino y depredador).

### 32.8 Formaciones temáticas (`EFormation`, `World/ProcMap/TN_ProcMapFormations.h`)

Se colocan **antes** que los obstáculos. Tres clases:

| Clase | Formaciones (biomas donde salen) |
|---|---|
| Arcos que cruzan el camino (se pasa por debajo) | arco de roca (arenisca, granito, musgo u obsidiana según el bioma), costillar de ballena (playa), raíces gigantes en arco (manglar), pórtico de templo (selva), tronco colosal caído con raíces y lianas (selva, manglar), acueducto en ruinas (desierto, roca, zona humana, aquí la mitad de las veces) |
| Piezas en explanadas (con carriles libres a los lados) | barco varado, caracola gigante y ancla (playa); cabeza colosal de piedra, círculo de piedras (selva, roca) y tortuga colosal de piedra (selva, desierto); columnas de basalto, fumarola y agujas de obsidiana (volcán); chimenea de hadas, roca en equilibrio (desierto, roca), carreta, obelisco y cráneo fósil (desierto); cañón antiguo (roca, «guerra»); sacos terreros, búnker, torre de vigía, carro de combate y depósito de agua (zona humana) |
| Hitos lejanos (sin colisión) | pirámide escalonada (selva), faro (playa), mesa (desierto), farallón (playa, roca), castillo en ruinas (roca), molino (zona humana), palafito (manglar, agua) |

La pieza de explanada se sortea primero y espera hasta 40 m a un tramo donde quepa. Los arcos, las piezas de explanada y
los obstáculos tienen colisión; los hitos lejanos, no. Las formaciones **se rebuscan** (§32.12).

### 32.9 Fauna ambiental (solo visual)

`ATN_ProcFauna` (`World/ProcMap/TN_ProcFauna.*`, mallas en `TN_ProcMapFaunaMeshes.h`): 31 especies low-poly de caras
planas, hasta **900** a la vez (`MaxAnimals`, `Density` 2,6), sin colisión, sin red (cada máquina simula los suyos, con el
mismo reparto inicial por la semilla) y nada en servidor dedicado. **No es un enemigo**: no ataca ni estorba. Solo se
simulan los animales a menos de 80 m (`WakeRadius`) de una cámara local; huyen al entrar un jugador en su rango (se
entierran, trepan, vuelan, se sumergen o corren) y reaparecen por delante. Reparto por módulo y bioma
(`TNFaunaBiomeTableOf`, entre paréntesis el peso):

| Bioma | Animales por módulo | Especies (peso) |
|---|---|---|
| Selva | 11 | mono (3), tucán (2), rana dardo (2), capibara (2) |
| Playa | 12 | cangrejo rojo (3), cría de tortuga (2), gaviota (3), correlimos (2), pez (1) |
| Desierto | 9 | lagartija (3), suricato (3), correcaminos (2), buitre (2) |
| Volcánico | 8 | salamandra (3), escarabajo de fuego (3), murciélago (2), iguana marina (2) |
| Agua | 9 | pez (3), tortuga marina (2), pelícano (2), flamenco (2) |
| Roca | 9 | cabra montés (3), marmota (3), águila (1), lagartija (2) |
| Manglar | 11 | cangrejo violinista (3), garza (2), rana de ojos rojos (2), pez del fango (2), flamenco (1), pez (1) |
| Zona humana | 11 | gato (2), paloma (3), gallina (3), conejo (2) |

Van pegadas a los caminos (bastantes en el propio camino; dos de cada tres a menos de 9 m de su borde; el resto hasta 25 m,
60 m las de agua). Consola: `TN.Fauna.Enable 0|1`, `TN.Fauna.Stats 1`. Otros efectos ambientales solo visuales
(`TN_ProcMapAmbientFX.h`): gotas, vapor y brasas instanciadas, brasas sobre lava, bandadas de gaviotas en la costa y la meta,
guacamayos en la selva y aves sobre bosque y roca, buitres en el desierto.

### 32.10 Clima: la tormenta del camino y el ambiente

- **Tormenta del cooperativo** (`ATN_PathStorm`, `World/ProcMap/TN_PathStorm.*`, aspecto en `TN_PathStormFX.h`): a diferencia
  de la caja recta del modo clásico, su frente es una **distancia sobre el camino principal** (progreso en cm; en 3D, para
  distinguir puentes y cuevas). Empieza a −30 m (`FrontProgress` −3000) y, pasada la gracia (90 / 60 / 45 s en F / N / D),
  avanza a velocidad constante 160 / 180 / 200 cm/s, **nunca más rápida que la tortuga andando** (la del jugador más lento).
  Quien queda más de 4 m (`InsideMargin` 400 cm) por detrás del frente empieza la cuenta atrás de muerte de **5 s**
  (`SecondsInsideToDie`). Entre rondas se para y se reinicia. Reaparecer solo vale en huevos que queden por delante del
  frente (+30 m, `StormRespawnMargin`).
- **Aspecto**: velo translúcido animado (`M_ProcStormVeil`), nubes que ruedan (`M_ProcFXCloud`) y partículas que dependen del
  bioma (arena, hojas, brasas y ceniza, espuma, lluvia, polvo, humo, papeles), mezcladas en degradado al cambiar de bioma;
  dentro, la niebla del nivel se cierra y la imagen se tiñe según el bioma del jugador.
- **Tos** (`UTN_StormCoughComponent`, `World/ProcMap/TN_StormCough.*`, sintetizada): cada tortuga dentro de la tormenta tose
  con voz propia: carraspeos al entrar, ataques de tos cada vez más seguidos (12 s hasta la peor) y jadeos, un último
  carraspeo al salir y silencio al morir. Sin tormenta: `TN.Storm.Cough 0|1|2`.
- **Sonido ambiente** (`UTN_AmbientSoundscapeComponent`, `Audio/`): capas por bioma (viento, oleaje, aves, cigarras, grillos,
  ranas…) que cambian en degradado, tormenta, cuevas amortiguadas y fuentes 3D en géiseres, cascadas y lava. Aves, cigarras,
  grillos y ranas cantan «a ratos» para que el fondo no canse. Todo sintetizado, sin archivos.
- **Música de fin de partida** (`UTN_MatchMusicSubsystem`): victoria (si♭ mayor, 120 BPM), derrota (re menor, 72 BPM) y una
  cortinilla de eliminado.
- **Probar la tormenta en cualquier sitio**: `TNStorm <Selva|Playa|Desierto|Volcan|Agua|Rocas|Manglar|Pueblo|Geiser|Cascada|Off>`
  lleva a la tortuga al tramo más largo de ese bioma y pone el frente 9 m por detrás, inofensivo.

### 32.11 La salida, las pilas de huevos y la meta

- **Salida de la ronda** (`ATN_ProcStartStructure`, `World/ProcMap/TN_ProcStartStructure.*`, kit `Lobby/TN_CastleKit.h`): la misma
  pieza en la que los jugadores se pusieron listos en el lobby. **Puerta doble** (`ETNMatchStartStyle::Gate`): sala entre
  dos puertas contra el talud, ocho sitios en dos filas de cuatro a 2,3 m; al abrirse (a los 1,2 s del «¡ADELANTE!»,
  `StartStructureOpenDelaySeconds`) las hojas de la puerta 2 giran 100° en 1,25 s. **Huevos** (`Eggs`): montículo de dos
  alturas con la pila de ocho huevos (siete abajo, uno arriba); cada jugador dentro de su huevo con la tapa puesta; se
  rompen uno tras otro cada 0,12 s, **1 s de pausa** dentro (se agacha, se pone de pie, se sacude la cáscara y mira al
  camino) y sale despedido (3,8 m/s horizontal y 6,2 m/s arriba). Elige el estilo `GetStartStyle()` del lobby (más listos
  en la sala o en los huevos; empate = puerta); `TN.Proc.StartStyle 0|1|-1` lo fuerza y `TN.Proc.Egg` repite la salida.
- **Pilas de huevos de reaparición** (`ATN_ProcEggNest`, `World/ProcMap/TN_ProcEggNest.*`): en los cruces entre módulos (una
  cada 1 / 2 / 3 cruces). Al pasar junto a una queda activada (visual replicado, se pone dorada); quien muere después
  reaparece en la más avanzada que tenga activada y que quede por delante de la tormenta.
- **Meta**: el camino llega recto a 55–80 m de la costa y sus brazos se abren en campana; la línea de meta cruza la boca (75–110 m)
  3,5 m mar adentro (agua por la rodilla). Encima, un **neumático gigante en arco** con TORTUNAVY en el flanco que se ve
  al llegar y, de guiño, TORTUNABO en el que mira al mar, pasarela a cuadros con el cartel META, rótulo «¡AL AGUA!» y
  banderas; boyas de lado a lado y banderines en la arena (`ATN_ProcFinishVolume`, `TN_ProcMapFinishMeshes.h`).

### 32.12 Rebuscables del mapa procedural (`ATN_ProcSearchSpot`)

Mantener E ~1,3 s junto a un decorado: con un **55 %** sale un objeto del catálogo de siempre (nubecilla, «¡puf!», saltito);
si no, una nube del color del suelo y un «¡pof!». **Cada decorado se rebusca una sola vez para todo el grupo.** Los elige
`SpawnSearchSpots` (`TN_ProcMapGenerator_Spawn.cpp`) con semilla propia, a **70 m como mínimo** entre sí:

| Decorado | Probabilidad |
|---|---|
| Formaciones de explanada (barco, cabezas, basalto, chimenea de hadas, peñasco en equilibrio, carreta, cañón, sacos, búnker, torre, carro de combate, caracola, ancla, círculo de piedras, obelisco, cráneo, tortuga colosal, depósito) | 100 % (agujas de obsidiana, 70 %) |
| Obstáculos de objetos del camino (cajas, barriles, pacas, castillo de arena, barca, rincón de playa, tótem, columna, roca calavera, tinajas, cristales, mojón, vagoneta, nasas, puesto de mercado) | 45–80 % según el tipo |
| Agujas y mogotes de roca | 45 % |
| Peñascos (solo los grandes, radio ≥ 1,4 m) | 30 % |

Fuera: arcos, hitos lejanos, vegetación, fumarola, vallas y filas de conos. Más detalle (aro, red, comandos) en
[`Docs/Botin_Decorados.md`](Botin_Decorados.md).

### 32.13 Conchas de puntos del mapa (`TNProcMap::PlanShells`)

Cuatro tamaños (`TNScoreShells`, `World/TN_ScoreShells.h`): pequeña de **1** (oro claro, radio de recogida 48 cm, se dibuja hasta
70 m), normal de **25** (60 cm), grande de **50** (72 cm, nácar turquesa) y **reina de 100** (80 cm, rosa y violeta, columna de
luz de 13 m). Se cogen al pasar. Reparto: normales de 25 con los peligros de cada bioma (`BP_ScorePickup`, 4 por km ×
densidad, a 30 m entre sí) y en lo alto de atalayas y torres; **conchitas de 1** (tope 600): rachas de 5–7 por el camino
(una cada 220–340 m), rachas de ramas y desvíos (cada 120–190 m, tope 200), arcos de 5–6 sobre los saltos (tope 120) y 2–6
por las cornisas de los adarves (tope 40); **especiales** (una por muralla o rama, ≥60 m entre sí): una reina (dos si el
camino pasa de 15 km) y una grande cada ~3 km, en sitios difíciles (brecha del adarve 3+, rama arriesgada 2,2, ruta alta
1,6, salto de panzazo más largo 1,4, desvío 1,3–1,8, rama tranquila 1). Cada mapa lo dice en el registro (`[ProcMap]
Conchas: …`); consola `TNShells <1|25|50|100|Especial|Lista>`.

### 32.14 Puzles del 2vs2 (solo en ese modo, `World/ProcMap/TN_ProcPuzzleActors.*`)

- **Muro de lanzamiento** (`ATN_ProcThrowWall`): bloque de roca de 4,8 m (por defecto 12 × 10 × 4,8 m) demasiado alto para
  saltar; un compañero lanza al otro arriba, el de arriba pulsa el interruptor y baja la rampa (`RampRun` 750 cm) para que
  suba el otro. La rampa se vuelve a levantar sola.
- **Compuerta de sabotaje** (`ATN_ProcSabotageGate`, por defecto 10 × 3,5 m): enterrada; cuando la pareja del otro carril
  pulsa su interruptor sale del suelo y corta el paso.
- **Interruptor** (`ATN_ProcSwitch`, `EffectSeconds` 8): baja la rampa de un muro o levanta la compuerta del otro carril.

### 32.15 Qué es solo visual y qué tiene colisión (cooperativo)

| Solo visual (sin colisión) | Con colisión |
|---|---|
| Vegetación procedural, fauna ambiental, efectos ambientales, hitos lejanos, bandadas de aves, la niebla y el velo de la tormenta, agua animada (la superficie) | Terreno (teselas), murallas, torres, puentes, formaciones de explanada y arcos, obstáculos de objetos, peñascos, agujas y mogotes, troncos, objetos sueltos macizos, cristales de cuevas |
| — | Con efecto de juego sin ser sólido: agua nadable (volumen local), géiser, tobogán, corrientes, remolinos, zonas de muerte (lava, zanjas, cajas bajo los puentes), rebuscables (esfera invisible de consulta), conchas (esfera de recogida), pilas de huevos (disparador) |

### 32.16 Límites conocidos

Sin Nanite en las mallas de runtime; el PCG de Unreal es un gancho sin grafos incluidos; la vegetación no tiene colisión; con unas 500 000
instancias hay que vigilar los equipos modestos (`FloraDensity`); y el cooperativo Normal dura más de lo previsto (la propia documentación
lo reconoce: se dejó así a propósito para probar; §32.1).

## 33. La playa de la carrera (`LVL_BeachRace`)

Un único tramo recto de playa de arena que baja hacia el mar. **El terreno es siempre el mismo**; en cada ronda solo se
vuelven a repartir decorado, trampas, ayudas y enemigos (§37). El mapa de colores de las huellas y la vista previa en el editor están en
[`Docs/Modo_Carrera.md`](Modo_Carrera.md), «Nivel y capturas». Nivel: `/Game/Maps/Run/LVL_BeachRace`, creado por
`Scripts/build_beach_race.py` (sol a la espalda de la salida, cielo, luz del cielo, niebla suave desde 300 m, el generador
«PlayaCarrera» en el origen, cuatro `PlayerStart` y `ATN_BeachRaceGameMode` en World Settings). Actor central:
`ATN_BeachRaceGenerator` (`World/Beach/TN_BeachRaceGenerator.*`; construcción en `_Build`, `_Features`, `_Start`, `_Scenery`,
`_Round`, `_Debug`); la lógica pura del terreno y del reparto, en `World/Beach/TN_BeachLayout.h` con sus pruebas
`Tortunabo.Beach.*` (`Terrain`, `Relief`, `Layout.Determinism`, `Layout.Rules`, `Layout.Difficulty`).

### 33.1 Medidas generales

| Dato | Valor | Origen |
|---|---|---|
| Escala | todo ×28 (`TNBeach::Scale`) | `TN_BeachTypes.h` |
| Recorrido | **800 m** de la línea de salida (X = 0) al filo del acantilado (`CourseLength` 80000 cm; eran 1200 m); la mitad, donde sale el sprint final, a 400 m | `TN_BeachTypes.h` |
| Ancho jugable | 280 m (Y entre −140 y +140 m) | `CourseWidth` |
| Proporción con los 1200 m antiguos | 2/3 (`CourseLengthScale`), por la que se multiplican las cuotas «por ronda» | `TN_BeachTypes.h` |
| Perfil vertical | 39,5 m sobre el agua en la salida y 15,5 m en el filo (`CliffHeight` 1550): cae 24 m (el 3 % del recorrido, `BeachDrop`) como (1 − t)^1,5 (4,5 % al principio, casi llana al final); desde la salida se ve siempre el mar (4,2 m de margen como poco) | `TN_BeachLayout.h` |
| Duración prevista | El juego anuncia ~3,3 min (`AverageRaceSpeed` 400 cm/s: una media de 4 m/s, pensada con andar a 4,5 y esprintar a 8 m/s). Con las velocidades reales (andar 2, esprintar 4 m/s, ~40 % de esprint: media de ~2,8 m/s [calc]) son **≈ 4 min 46 s**, sin contar trampas ni enemigos; el límite de ronda es de 9 min | `Game/TN_BeachRaceGameMode.cpp` |
| Muros invisibles | a 148 m a cada lado (también en el agua), a −32 m detrás de la salida y a 1100 m mar adentro; de −200 m a +1200 m de alto | `BackWallX`, `SideWallY` |
| Vacío | el suelo más bajo pisado en la ronda (o la salida) menos 150 m (`VoidDepth`), siempre por encima del `KillZ` | `Game/TN_BeachRaceGameMode.h` |
| Malla del terreno | rejilla de 3 m en la playa (hasta 18 m lejos) en 40 teselas de 40 × 40 casillas; con colisión las que quedan a tiro; `M_ProcTerrain` con relieve y, en la arena, grano, guijarros y marcas del viento; tierra y hojarasca en los bancos; arena mojada alrededor de las pozas y húmeda en el fondo de las trincheras | `TN_BeachRaceGenerator_Build.cpp` |

### 33.2 El terreno fijo, pieza a pieza

**Salida con huevos.** En el linde de la selva, entre las raíces de una **ceiba colosal** (~300 m de alto; tronco de ~26 m
de radio en la base, a 52 m detrás de la línea: `TrunkX` −5200); sus dos raíces tabulares enmarcan la salida y seis plantas
de hojas enormes la techan. Cartel «¡A LA META!» (por detrás, «TORTUNAVY») entre dos palos de madera a la deriva. Una **fila de
cuatro huevos** (los del lobby y de la salida del cooperativo: base y tapa de `TNCastleKit`, cada uno con su color) en
X = −8 m (`StartSpotX`), Y = −15, −5, 5 y 15 m (`StartSpotSpacing` 10 m), sobre un **nido de arena** (anillo de 1,3 a 2,8 m
con colisión). Para ocho jugadores hay una **segunda fila** de cuatro 9 m detrás (`StartRowSpacing`), cada huevo detrás de uno
de la primera (`MaxStartEggs` = 8): el salto de ~15 m pasa por encima de la base rota, que no tiene colisión. Cada tortuga
espera dentro del suyo (110 cm sobre el suelo, mirando al mar); los sitios rotan cada ronda. **Nada del reparto a menos de
15 m de la línea de salida** (23 m de los huevos; el salto cae a ~7 m) y el relieve empieza a los 35 m. Colisión: el tronco,
las raíces, los tallos de las plantas, los palos del cartel y el anillo del nido; las hojas, no.

**Relieve** (`ReliefZ`, fijo, sobre el perfil): entra entre 35 y 170 m y se apaga en los últimos 90 m antes de la roca.
Dunas de **2,8 m** de amplitud en el centro a **4,8 m** junto a la selva y ondulación a tres escalas (1,7 m cada ~130 m, 70 cm
cada ~43 m y 25 cm cada ~16 m). Medido: desviación típica 1,20 m, de −5,0 a +4,5 m sobre el perfil, pendiente máxima 37° y
el 23 % de la playa con más de 10°. **Todo se anda salvo las cornisas.**

**Corredores** (`CorridorAt`, 3): dos caminos más bajos que se separan (hasta 54 m entre ejes; 61 m con el tercero) y se
vuelven a juntar, y un tercero en medio por tramos; 1,7 ± 0,7 m más hondos que lo de alrededor, con las dunas apagadas
dentro. Son el camino natural (y el de algunos campos de minas).

**Siete crestas** (`Ridges()`, dunas con cresta; cara empinada de ~30° hacia la salida y bajada suave —4 veces su alto— hacia
el mar). Malla del generador (`FeatureMesh`, con colisión), asentada en la del suelo:

| Cresta | X aprox. | Tipo | Altura nominal | Largo nominal | Cornisa |
|---|---|---|---|---|---|
| 1 | 164 m | separa corredores a lo largo | 3,8 m | ~84 m | no |
| 2 | 292 m | cruza la playa sobre un corredor (collado de 7–11 m) | 4,2 m | ~112 m | sí |
| 3 | 512 m | cruza | 4,0 m | ~100 m | sí |
| 4 | 572 m | media luna entre el corredor de fuera y la selva | 3,8 m | ~72 m | sí |
| 5 | 616 m | separa corredores | 4,2 m | ~72 m | no |
| 6 | 668 m | cruza | 3,8 m | ~100 m | sí |
| 7 | 708 m | media luna | 2,6 m | ~60 m | no |

(Medido en el terreno: dunas de 2,8–4,1 m de alto y 54–109 m de largo.) La **cornisa** (`LipHeight`) es un labio de arena de
95 cm en lo alto, donde la cresta pasa del 72 % de su alto (fuera de collados y puntas): se salta (la tortuga salta 1,2 m) o
se pasa por el collado.

**Siete pozas con agua de verdad** (`Pools()`): cuatro charcas entre las dunas (tres cortan un corredor) y tres pozas de
marea con rocas alrededor en el último tercio. El agua queda 30 cm por debajo de la arena más baja de su orilla (nunca
rebosa), de 1,5 a 2,4 m de hondo (`min(2,4 m, 20 % del radio menor)`), orillas de 28° como mucho: se nada y se sale andando.
Nadable con cajas que siguen su forma (~100 en total), superficie con `MI_ProcSeaAnim` (`DepthRange` 300, `FoamWidth` 60) y
chapuzón al entrar.

| Poza | X aprox. | Semiejes nominales | Tipo | Corta un corredor |
|---|---|---|---|---|
| 1 | 268 m | 11 × 18 m | charca | sí (corredor 1) |
| 2 | 320 m | 9 × 7 m | charca | no |
| 3 | 484 m | 10 × 16 m | charca | sí (corredor 1) |
| 4 | 544 m | 9 × 15 m | charca | sí (corredor 2) |
| 5 | 596 m | 14 × 11 m | marea | no |
| 6 | 690 m | 10 × 13 m | marea | no |
| 7 | 720 m | 13 × 21 m | marea | sí (corredor 1) |

**Dos trincheras** (`Trenches()`): líneas en zigzag de lado a lado al 28–31 % del recorrido (X ≈ 219–229 m y 239–250 m). Canal
de 3,2 m con el fondo 60 cm por debajo de la arena entre dos caballones de 45 cm (~1,05 m desde dentro: se sale de un salto),
tablones por dentro, sacos terreros del lado del mar en grupos de 7 (dos capas, huecos de 1,6 m), postes y tarimas en el fondo
y dos puentes de tablones por línea. El terreno se cava 60 cm hasta 2,1 m del eje y vuelve a la arena natural a 6,6 m.

**Bancos de la selva**: suben 38 m en los 110 m de fuera de la playa y 45 m más hasta 460 m, con colinas; detrás de la salida,
42 m y luego 30 m más.

**Acantilado de meta**: repisa de caras planas en los últimos 24 m (enterrada al empezar, asoma 50 cm sobre la arena desde
~16 m antes del filo), **filo limpio a 15,5–16 m del agua** (~55 cm reales, 5–6 veces la tortuga) y pared casi vertical,
socavada hasta 2,8 m (nunca sobresale del filo: se cae al agua); banda mojada oscura y algas bajo el agua; peñascos al pie de
los cabos (fuera de donde se cae). El filo ondula ±2,5 m a lo ancho. El relieve se apaga antes para que el borde se lea limpio.
Nada del reparto a menos de 30 m del filo.

**Agua de meta**: 11 m de hondo al pie (18 m a 250 m, 35 m lejos), nadable (`ATN_ProcWaterVolume` local en cada máquina) de
debajo de la repisa a 300 m mar adentro y 250 m a cada lado; superficie con `MI_ProcSeaAnim` (`DepthRange` 1500, `FoamWidth`
160) hasta el horizonte (6 km). **Se gana al tocarla** tras saltar el filo (`IsFinishWater`: más allá del filo +50 cm y el
centro de la cápsula a 30 cm del agua o menos). Zona de zambullida (`IsCliffJumpZone`): los últimos 7,5 m antes del filo y
40 m sobre el vacío (el salto de cabeza sin bola ni golpe). Al zambullirse, la tortuga se queda 0,8 s a la vista en el agua con la postura
de la zambullida congelada (nunca se pone de pie) mientras, en su pantalla, un huevo negro tapa todo con «Has quedado X.º», su corona o
premio y un mensaje gracioso (`FTNBeachRoundArrival`, `RoundArrivals` en el GameState; §30.6).

**Meta decorada** (`_Scenery.cpp`; el arco tiene colisión, el resto es adorno): boyas con banderas a cuadros de 12 m en mástiles
de 28 m cada 40 m y a 26 m del filo, unidas por un cabo con boyas pequeñas que se mecen; el **arco de neumático** de la meta
del mapa procedural cinco veces más grande (125 m de luz, 63 m sobre el agua) a 40 m del filo, con TORTUNAVY hacia la playa y
TORTUNABO hacia el mar; banderines hasta dos mástiles en los cabos y banderolas por la ladera de la selva en los últimos 250 m.

**Línea del sprint** (`SprintLineX()`): lo más cerca de la mitad del recorrido donde los 12 sitios (4 en fila y dos filas
detrás) caen en arena seca y casi llana (menos de 12°, fuera de pozas y trincheras y a 6 m de las cornisas): X ≈ 400 m.

**Selva de los bordes** (sin colisión, sin sombra, con viento solo a menos de 200 m de la cámara; detrás de los muros):
palmeras de 230–300 m (×26–34), árboles de copa, ceibas de 250–340 m, casuarinas, pándanos y peñascos en rejilla de 48 m
(espesa en los primeros 160 m, más clara hasta 460 m); una primera fila de palmeras algo más bajas (×22–29) cada 28–42 m;
sotobosque de plataneras, palmitos, helechos arbóreos y uvas de playa de 50–100 m en los primeros 100 m. Las palmeras de la
orilla se inclinan 30–60° sobre la arena y hacia el mar, y dejan el centro a cielo abierto.
**Huecos entre copas**: rejilla de 15 m; donde el tronco más cercano queda a más de 22 m y la mata a más de 15 m salen
enredaderas por el suelo (60 %), helechos (50 %), matas de hojas enormes (65 %: tallo de 10 m y hojas de 12–18 m), lianas
colgadas de tronco a tronco (3,5–11 m) y cortinas de lianas; mallas propias instanciadas con viento por vértice, sin sombra,
visibles hasta 300–350 m de la cámara.

**Sombras** (por rendimiento, `[VSM] Non-Nanite Marking Job Queue overflow`): con sombra las teselas de la playa y del pie de
los bancos, el acantilado y sus rocas, la salida, el arco de la meta, las mallas del relieve (trincheras, cornisas, rocas de
las pozas de marea) y los elementos de la ronda; sin sombra toda la vegetación de la selva, las banderolas, las boyas, el mar,
el agua de las pozas, los postes y tarimas de las trincheras y las huellas.

### 33.3 El decorado gigante (local e instanciado)

**Cómo se monta.** Desde la ronda 3 el decorado **no son actores replicados**: cada máquina lo monta en su propio
`ATN_BeachDecorField` (`World/Beach/TN_BeachDecorField.*`, actor local `RF_Transient`) con mallas instanciadas jerárquicas
(HISM) por elemento y variante (~200–300 componentes en vez de ~4000 actores). Las recetas, la colocación, la colisión y la
animación son las de `ATN_BeachDecor` (`World/Beach/TN_BeachDecor.*`, que se sigue usando para `TN.Beach.Place`) y viven en
`TNBeachDecorKit` (`Private/World/Beach/TN_BeachDecorKit.h`) y en las recetas low-poly de `TN_BeachPropMeshes.h` y
`TN_BeachMilitaryMeshes.h`.

- **Variantes**: la semilla del elemento (`Spec.Seed`) elige entre 4 por pieza (8 rocas, 3 grupos de rocas). La malla de cada
  (elemento, variante) se construye **una vez por partida** con `M_CosmeticVertexColor` (el alfa del vértice es brillo) y la
  comparten todos los ejemplares.
- **Colocación**: giro libre (la silla mira a +X con ±15°; los parapetos, ±6°), inclinación 0–8°, hundimiento en la arena según
  la pieza (para que no flote en las dunas) y escala `Spec.SizeScale` (0,5–1,6) **en el componente**; el actor no se escala.
- **Colisión por instancia**: solo cajas, esferas y cápsulas en el `BodySetup` de la malla compartida (perfil `BlockAll`, simple
  como compleja); la cámara solo choca con lo grande y macizo (rocas, grupos de rocas, troncos y castillos). Los detalles finos
  no tienen colisión.
- **Subir y saltar**: escalones de 80 cm (la tortuga salta 1,2 m) y rampas de menos de 34° (lona de la vela 12–33°, toalla de
  la silla 33°, rampa de tablones 19°, losa 20°, pliegues de la toalla 21–30°, lona de la sombrilla 13–31°, casquetes de la
  medusa y de la red 38°, montón de arena de la sombrilla 27°).
- **Sombra**: lo grande (huella de 10 m o más) y los tramos, siempre; lo demás, con una copia que solo da sombra hasta 90 m de la
  cámara (`ShadowNearDistance`). Lo pequeño deja de dibujarse a 60 veces su huella (120–600 m); lo grande, nunca.
- **Lo que se mueve** (solo con cámara local a 60–200 m según el tamaño, las 40 más cercanas; nunca en servidor dedicado): la
  campana de la medusa respira, la valva de la almeja se abre y cierra cada 7–12 s (no si hay una tortuga encima al empezar),
  el jirón de la vela ondea, la red se mece, las banderitas de los castillos y de lo militar ondean y el faldón de la red de
  camuflaje se mece. Lejos, todo está quieto en su lote.
- **Tramos** (`Boardwalk`, `WoodenPostPath`): `Spec.Extent` es el largo (0 → 60 m; entre 15 m y el recorrido) por el eje X local;
  se montan con piezas instanciadas, escaladas a lo largo y a lo ancho con `SizeScale`, no en alto.
- **Cantidad**: ~2600 piezas de decorado por ronda con 800 m (el ~80 % del reparto), casi todo el decorado pequeño.
- **Lo que arrastran los bañistas no es decorado del reparto**: sombrillas de 50 m, cubos, sillas, toallas, flotadores, palas, chanclas y
  pelotas (`EStormItem`, 24 trastos) y los ocho bañistas de la tormenta son piezas propias de `ATN_BeachStorm` (§36.12): mallas hechas en
  ejecución, sin colisión y con vida de 3,5–7 s. Nada de eso se rebusca.

**Decorado civil y de mar** (huella = radio en planta; «Rebusca» = probabilidad de ser rebuscable por ronda, §34.11):

| Pieza (`ETNBeachElement`) | Huella | En el juego | Variantes | Colisión | Rebusca |
|---|---|---|---|---|---|
| Coco (`Coconut`) | 2,6 m | 4–4,5 m de largo, 3,1–3,4 m de alto | pardo peludo, verde, partido en dos, germinando con brote | cápsula; mitades: prisma y casquete | 85 % |
| Medusa varada (`StrandedJellyfish`) | 7 m | campana de 10,4 m y 1,8 m de alto, brazos hasta 13 m | aurelia, aguamala, acalefo, clavel | casquete de 38° (se sube andando); la campana respira | nunca (pica) |
| Anillas de latas (`SixPackRings`) | 4,5 m | 6 × 4 m, 10 cm de grueso | casi todas cortadas, pocas cortadas, retorcidas | no | 60 % |
| Sujetador rojo (`RedBra`) | 5,5 m | 11 m de ancho, copas de 3,9 m y 1,5 m de alto | liso, lunares, encaje con lazo, una copa boca arriba | casquetes; la copa boca arriba, anillo de cajas | 85 % (uno por ronda, entre el 20 y el 80 %) |
| Almeja (`Clam`) | 1,5 m | 1,4 m, 0,65 m de alto | crema, lila con rayos, naranja, gris con perla | caja | 85 % |
| Concha de adorno (`DecorShell`) | 1,8 m | 1,4–2,5 m | caracola, berberecho, porcelana moteada, vieira pálida | caja | 60 % |
| Estrella de mar (`Starfish`) | 2,5 m | 4 m, 0,55 m en el centro | naranja, roja con un brazo levantado, morada, azul | casquete y cajas bajas | 60 % |
| Roca (`Rock`) | 7 m | hasta 13 m, 3,2 m de alto | de estratos (4 escalones), losa inclinada, canto rodado, mesa con charco; arenisca, granito o pizarra | prismas por capa, caja inclinada o casquete | 85 % |
| Grupo de rocas (`RockCluster`) | 16 m | 26 m, 4–4,8 m | 3 repartos: una grande de estratos, dos medianas de escalón, cantos y guijarros | prismas y casquetes | 100 % |
| Restos de vela (`ShipSailWreck`) | 35 m | mástil de 48 m, lona hasta 8 m | 4 lonas con franja y remiendos | cápsula del mástil, cajón y 16 cajas por la lona (hueca por debajo) | 100 % (uno por ronda, desde el 30 %) |
| Tronco con musgo (`MossyLog`) | 14 m | 23,5 × 3,4 m | en rampa sobre una piedra (0,9–6,1 m), medio enterrado (1,1 m), con hueco para cruzarlo, con repisas de hongo (0,9 / 1,8 / 2,7 m) | cápsula, 8 cajas en tubo o cajas de las repisas | 100 % |
| Tablones viejos (`OldPlanks`) | 9 m | 14 × 2,5 × 0,56 m | pelados, azules con borde blanco, verde agua, rojos | una caja por tablón y el cajón (3,4 m) | 100 % |
| Red de pesca (`FishingNet`) | 13 m | 23 m, montón de 2,2 m, palo de 6 m | verde, azul, naranja, turquesa; bolas o corchos | casquete de 38° y el palo (se sube); el paño se mece | 100 % |
| Vaso de plástico (`PlasticCup`) | 2,5 m | 2,7 m | rojo o transparente tumbado (se entra), de pie medio enterrado, aplastado | tubo de 8 cajas con suelo plano, anillo o caja | 85 % |
| Botella (`Bottle`) | 4 m | 7 m, 1,7 m de diámetro | verde tumbada, marrón clavada boca abajo, clara con mensaje, verde clavada de culo | cápsulas | 85 % |
| Chupachups (`Lollipop`) | 3,5 m | 3,4–4,5 m | de bola con envoltorio abierto, chupado con arena, espiral tumbada, espiral clavada (4 m) | esfera, caja o palo y disco | 60 % |
| Corteza de sandía (`WatermelonRind`) | 5 m | arco de 6,5 m, 1 m de grueso | con carne, comida hasta lo blanco, de pie como un barquito, dos trozos | 4 cajas por el arco | 85 % |
| Pajita (`Straw`) | 4 m | 5,6 m | recta, doblada, de papel, clavada (4,5 m) | solo la clavada | nunca |
| Sombrilla clavada (`PlantedUmbrella`) | 16 m | lona de 26 m a 35 m de alto | roja, azul, amarilla y naranja, turquesa (torcida 3–7°) | mástil, 16 cajas en la lona y el montón de arena del pie | 100 % (se rebusca en el montón del pie) |
| Silla de playa (`BeachChair`) | 15 m | 14 × 15 m, asiento a 5,6 m, respaldo a 19 m | 4 lonas y toallas, bastidor de aluminio o blanco | patas, travesaño, asiento, respaldo, reposabrazos y la toalla-rampa | 100 % |
| Castillo pequeño (`SandCastleSmall`) | 8 m | 15 m, 3,2 m (bandera a 5,2 m) | 2 o 3 torrecillas, medio deshecho (bandera caída) | prismas y cajas | 100 % |
| Castillo enorme (`SandCastleHuge`) | 26 m | 49 m, 8,8 m (bandera a 12 m) | 4 colores de bandera | ~100 formas: plataforma, rampa de 16°, murallas con puerta, torres, torreón, escalinatas | 100 % |
| Madera a la deriva (`Driftwood`) | 9 m | 15 m | con horquilla, raíz, dos palos cruzados, rama en S | cápsulas por tramos | 85 % |
| Pasarela (`Boardwalk`) | 7 m | tramos de 12,8 m, 13 m de ancho, tablas a 1,1 m; largo 25–60 m | entero, sin tabla, roto, con una tabla suelta levantada, movida; bajadas en los extremos | una caja por tabla y los pilotes | nunca (es camino) |
| Caminito de palos (`WoodenPostPath`) | 3 m | palos de 4,2 m cada 5,6 m, 5,4 m de ancho; largo 30–80 m | palo recto, roto o con vueltas; cuerda, cabo azul o cinta de balizar | cápsula por palo (la cuerda se pasa por debajo) | nunca (es camino) |
| Lata (`SodaCan`) | 2,2 m | 3,4 m | roja, azul, aplastada, chafada de pie | cápsula, caja o prisma | 60 % |
| Chapas de botella (`BottleCaps`) | 1,5 m | 0,9 m | 2–4, alguna boca arriba | no | nunca |
| Chanclas (`FlipFlop`) | 5,5 m | 7,3 m | una, el par, del revés, con la tira rota | caja de la suela y cápsulas de la tira | 85 % |
| Brick de zumo (`JuiceBox`) | 2,5 m | 2,9 m | con pajita, de pie, aplastado, con la pajita envuelta | caja | 60 % |
| Boya (`Buoy`) | 5,5 m | 6,7 m | bola naranja con cabo, baliza de rayas, defensa blanca, bola amarilla con algas | esfera o cápsula | 85 % |
| Toalla (`BeachTowel`) | 18 m | 28 × 17 m | 1–3 pliegues, un extremo enrollado | caja plana, dos cajas por pliegue, cápsula del rollo | 100 % |
| Crema solar (`SunscreenBottle`) | 3 m | 4,8 m | tumbada con pegotes de crema, clavada del revés | caja | 85 % |
| Palitos de helado (`PopsicleSticks`) | 2,5 m | 3,2 m | 1–3, con restos de helado | no | nunca |
| Cáscaras (`SnackShells`) | 3 m | 0,3–0,4 m | 8–13 de pipas, pistachos y cacahuetes | no | nunca |
| Trozo de cuerda (`RopePiece`) | 6 m | 11 m | en S, en lazada, con nudo, cabo corto | no | 60 % |
| Gafas de sol (`Sunglasses`) | 3 m | 3,9 m de ancho, 1,4 m de alto | negras, rojas de espejo plegadas, de carey sin un cristal, de corazón | caja del frente y cápsulas de las patillas | 60 % |
| Cubito de juguete (`ToyBucket`) | 4,5 m | 2,5 m | con flan de estrella, flan de tortuga, molde de pez o rastrillo | prisma del cubo y el flan | 100 % |
| Pelota hinchable (`BeachBall`) | 5,5 m | 8,4 m | clásica, medio deshinchada, pastel, azul y blanca | esfera o casquete | 85 % |
| Disco volador (`Frisbee`) | 4 m | 7,6 m, 1 m de alto | boca abajo (se sube), boca arriba como cuenco, de canto medio enterrado | prisma, cuenco o caja | 85 % |
| Hueso de sepia (`Cuttlebone`) | 2,5 m | 4,2 m | 4 (uno partido) | caja | 60 % |
| Patito de goma (`RubberDuck`) | 2 m | 2,5 m | amarillo, descolorido y tumbado, rosa, azul | cápsula y esfera | 60 % |
| Pluma de gaviota (`GullFeather`) | 3,5 m | 4,2 m | blanca con la punta negra, gris | no | nunca |

**Decorado militar (la tropa de Tortunavy)** (`Private/World/Beach/TN_BeachMilitaryMeshes.h`; paleta de la tienda del General
Galápago: verde oliva, caqui, azul marino y oro; la **escarapela** es una estrella dorada de cinco puntas sobre disco azul
marino con aro dorado). Alturas pensadas para la tortuga (1,4 m; salta 1,2 m): escalones de 80 cm y 1,1–1,2 m para cubrirse.
La cámara atraviesa todo lo militar (es bajo o hueco). Los parapetos, las redes y los erizos solo salen en grupos (sacos en las
trincheras y los puestos, redes en los puestos, erizos en filas); el resto también suelto.

| Pieza | Huella | En el juego | Variantes | Colisión | Rebusca |
|---|---|---|---|---|---|
| Parapeto de sacos (`Sandbags`) | 11 m | sacos de 1,2 × 0,66 × 0,34 m; 4 hileras (1,2 m) con banqueta de 2 (63 cm); recto de 14,4 m, media luna de 5,4 m de radio | 6: recto; media luna; recto a medio desmoronar; esquina en L con bandera; nido redondo de 3 hileras con bandera, caja y soldadito vigía; media luna desmoronada | una caja por tramo de igual alto (extremos en escalera de 28 cm) y una por saco caído | 100 % |
| Caja de munición (`AmmoCrate`) | 7 m | 1,52 × 0,88 × 0,75 m; cartuchos de 76 cm | cerrada con una lata; tres pilas en escalera (0,75 / 1,5 / 2,25 m); abierta y llena con la tapa de rampa (28°); volcada | caja por caja (la abierta: paredes, fondo y lecho de cartuchos a 44 cm) y la tapa | 100 % |
| Erizo antitanque (`TankTrap`) | 8 m | barras de 7,8 m a 35°: cruce a 2,25 m (se pasa por debajo entre dos patas), puntas a 4,5 m | raíles oxidados; con algas colgando; madera a la deriva atada; pareja pequeña | una cápsula por barra | 85 % |
| Casco militar (`MilitaryHelmet`) | 4,5 m | 6,6 × 5,7 m, 2,65 m de hondo | boca abajo medio enterrado (asoma 1,7 m; se sube de un salto); boca arriba como cuenco (borde a 90 cm); apoyado en un palo con una lata debajo; de lado como cueva (boca de 4,3 × 7 m) | 8 cajas finas por banda, el suelo de arena y el palo | 85 % |
| Red de camuflaje (`CamoNet`) | 15 m | 20 × 15,6 m, palos de 3,7–4,5 m | plana con faldón que se mece; a dos aguas (túnel, cumbrera a 4,3 m); caída por un lado (rampa de 16° a un techo a 3,8 m); plana sobre puesto de vigía | los palos; en el túnel, los faldones; en la caída, la rampa y el techo | 100 % |
| Bidón (`Jerrycan`) | 5 m | 2,15 × 0,8 × 3 m | de pie con escarapela (3 m); tumbado caqui (80 cm); dos en cruz (80 y 160 cm); rojo de gasolina con tapón abierto y charco | caja | 85 % |
| Soldaditos de juguete (`ToySoldiers`) | 6 m | 1,4 m (como la tortuga), peana de 64 × 48 cm | 8 posturas: fusil, prismáticos, bazuca, tumbado, pareja, trío, volcado, patrulla | una cápsula por soldadito (el tumbado, una caja de 40 cm que se sube) | 85 % |

### 33.4 Qué es solo visual, qué tiene colisión y qué se puede rebuscar (playa)

| Categoría | Solo visual (sin colisión) | Con colisión | Rebuscable |
|---|---|---|---|
| Terreno | selva de los bordes, huecos entre copas, banderolas, boyas, cabos, mar y fondo, hojas de las plantas de la salida | teselas de arena, crestas y cornisas, trincheras con sus sacos y puentes, rocas de las pozas, acantilado, tronco/raíces de la ceiba, arco de la meta, anillo del nido | no |
| Decorado civil y militar | anillas, chapas, cáscaras, palitos, plumas, pajitas tumbadas, cuerdas, faldón de las redes, lona (parte que se mece), hitos pequeños | casi todo lo demás (ver columna «Colisión» de las tablas) | el 100 %, 85 % y 60 % de las tablas (nunca: chapas, cáscaras, palitos, pluma, pajita, medusa, pasarela y caminito) |
| Trampas y ayudas | algas (se vadean), mina (se pisa), cráter de la mina, cartel de los lanzadores | alambre, plataformas (móviles y tambaleantes), cubo roto, castillo con salas, fortalezas, catapulta, trampolín, puerta de conchas (cerrada), conchas que atrapan (valva) | los cofres se abren; las minas y algas no |
| Enemigos | sombras/marcas de aviso, rodadas del quad, cráter del gusano | cuerpo sólido del cangrejo y del tanque; a los demás les da lo lanzado por su cuerpo de golpes (`GetHitCapsule`) | no |

Todo lo que es «con colisión y se mueve» (balsa, ascensor, tabla, pala, catapulta) es un subobjeto con nombre estable por red
para que la tortuga pueda ir montada sin correcciones (ver §34.1).

## 34. Elementos de juego de la playa

Ayudas, estructuras, botín y adornos con reglas. Las trampas que castigan (alambre, cubo roto, concha que atrapa, mina) están
en la §35; las algas y la plataforma que se parte, que son las dos cosas a la vez, se describen aquí y se citan allí.

### 34.1 Reglas comunes de los elementos

| Regla | Detalle |
|---|---|
| Contrato | `ATN_BeachElement` (`World/Beach/TN_BeachElement.*`) es la base de todo lo que el reparto crea; cada clase aplica su `Spec` (`ApplySpec`) antes de aparecer y construye su malla igual en todas las máquinas. Clases y huellas: `TN_BeachTypes.h` |
| Orientación | X local = sentido de la carrera. La catapulta, la plataforma móvil y el trampolín siguen su X local si ya mira al mar (±45°: el reparto los orienta así y les deja libre el arco de salto); si no, se giran solos al mar (`GetSeaDirection`). La concha se orienta al mar ±25°. Las medidas con escala de tortuga (puertas, pasillos, peldaños de 40 cm) no se escalan |
| Quién cuenta | Solo tortugas **vivas, fuera del caparazón y sin aturdir** (`TNBeach::IsTurtleStunned`): los enemigos no caen en trampas y una tortuga aturdida no se engancha ni se pincha. Quien monta (`TNBeachRideKit::IsFreeRider`): además sin derribar, sin que la lleve nadie y sin llevar a nadie. La concha y la catapulta no actúan en el recuento ni en el podio. Las trampas no cogen a quien mueve otro sistema (`TNBeachTrapKit::IsFreeTurtle`: tampoco sujeta por un enemigo ni en la boca de un gusano) |
| Aturdida / derribada / sujeta | **Aturdida** (`TNBeach::StunTurtle`, `World/Beach/TN_BeachStun.*`): suelta lo que lleve y entra en el caparazón como **bola** (con salida bloqueada hasta que acaba el tiempo y la bola se para), opcionalmente lanzada; si ya lo estaba, se alarga al mayor de los dos finales. **Derribada** (`TNBeach::KnockDownTurtle`): ragdoll con mareo de cáscara de plátano (como poco 2,2 s tumbada y 0,75 s levantándose). **Sujeta** (`MOVE_None`): pico de gaviota, boca de lagarto, brazos del pulpo, valva de la concha, gusano. Un **árbitro** (`TNBeach::ETNBeachMover`) decide quién manda sobre una tortuga (su movimiento < lanzamiento < bola < derribo < en brazos < sujeta por un enemigo < patada de la tormenta < red de seguridad < gusano) |
| No se muere | Lo que en el cooperativo mata (zonas de muerte, tormenta, caídas de más de 35 m, enemigos) aquí aturde: en el agua de meta → llega; dentro de una zona de muerte o de tormenta o bajo el vacío → vuelve a su último sitio seguro (se apunta cada 0,5 s) y queda aturdida **2,5 s** (`RescueStunSeconds`); si no → aturdida **3 s** donde está (`DeathStunSeconds`) |
| Nadie las lanza contra la tormenta | A menos de 25 m por delante del frente (o detrás), lo que la echaría hacia atrás se quita del lanzamiento (pulpo, gaviota al soltar, mina hacia atrás) |
| Arcos de salto | Catapultas y trampolines sueltos solo se ponen con su arco libre: 30 m (catapulta) y 22 m (trampolín) por delante de su borde y 10 m de ancho (`CatapultArc`, `TrampolineArc`, `JumpArcHalfWidth`); las fortalezas, su franja de caída |
| Red | Relevantes por distancia (260 m + 3 veces lo que ocupan, hasta 450 m; una mina ~270 m; una catapulta ~290 m); los enemigos, 200–300 m; los enormes y quietos (huella ≥20 m: castillos con salas y fortalezas) son relevantes en toda la playa. **Dormancy** en lo quieto (`DORM_Initial`, como mucho 2 Hz; `ForceNetUpdate()` tras cada cambio de propiedad replicada, que la despierta 3 s); nada replica posiciones: se replican horas del servidor y cada máquina anima con su reloj suavizado |
| Sonido | Todo sintetizado, sin archivos: `UTN_BeachTrapSynthComponent` (chispazo, «¡ay!», chof, crujido, chasquido, muelle, golpe sordo, conchas, roce de arena, clic); textos emergentes «¡AY!», «¡CRAC!» (`World/Beach/TN_BeachTrapCommon.*`) |
| Mallas | En ejecución, con color de vértice (`M_CosmeticVertexColor`), `RF_Transient \| RF_DuplicateTransient`, colisión convexa por piezas |
| Consola | `TN.Beach.Place <Elemento> [Tamaño] [Extent] [Semilla]` (`World/Beach/TN_BeachDebugCommands.cpp`) |

`Spec.Extent` significa: alambre = largo (0 → 24 m); algas = ancho máximo en Y; puerta de conchas = ancho del hueco de la puerta
«desnuda»; pasarela y caminito = largo; paso de quads = ancho; ermitaño = largo de su calle; tanque = tramo de patrulla; balsa =
recorrido; ascensor = alto. El resto no lo usa.

### 34.2 Trampolín (`ATN_BeachTrampoline`, `World/Beach/TN_BeachTrampoline.*`)

Rebota hacia arriba y adelante; sirve para atajos y para subirse a castillos. Cuatro variantes según la semilla (huella de 7 m):

| Variante | Medidas con `SizeScale` 1 | Rebote relativo |
|---|---|---|
| Medusa gorda varada | campana de 10 m y 3,2 m de alto (rosa, lila o celeste, con trébol, motas o cara), ocho brazos orales tendidos | ×1 |
| Colchoneta hinchable | cinco tubos a rayas de 1,44 m y una almohada de 1,9 m: 9,8 × 6,9 m, con válvula | ×0,92 |
| Flotador de donut | 12,3 m, 4,2 m de alto, glaseado rosa/chocolate/celeste/menta con virutas; se puede caer en el agujero de 3,9 m | ×1,08 |
| Sombrero de paja tenso | ala de 13 m a 22–38 cm (se pisa) y copa de 5,3 m y 2,2 m con la tapa tensa | ×0,96 |

- **Rebote**: hacia arriba **1250 cm/s** (`BaseUp`; ~8 m de altura) más 0,55 por cada cm/s de caída por encima de 300
  (`FallGain`), con tope **2000** (`MaxUp`, ~20 m): de trampolín en trampolín se sube cada vez más. Hacia delante conserva el
  **75 %** de la velocidad horizontal (`KeepHorizontal`) y suma **320 cm/s** hacia el mar (`ForwardPush`, tope 1100): ~12 m
  andando, ~16 m esprintando (vuelo de 2,55 s; [calc] con las velocidades reales; con las del código serían ~16 y ~23 m). No mete en el
  caparazón al caer. 0,3 s entre dos rebotes de la misma (`BounceCooldown`). Rebota
  todo el cuerpo (cima, costados y borde, también de lado desde la arena) y las bolas de caparazón (al 90 %).
- **Deformación** visual local: se aplasta entera y rebota estirándose (10–26 %) y se hunde donde cae la tortuga (22–64 cm en
  ~2,7 m) con una abolladura que vibra ~1 s.
- **Potenciado** (`Spec.Flags & FlagBoosted`, el de la cima de las fortalezas): hacia arriba ×1,25 (`BoostedUpScale`; tope 2400) y
  **900 cm/s** de empujón al mar (tope 1500): en llano, ~34 m andando y ~38 m esprintando (2,8 y 2,4 veces uno normal) [calc con las velocidades reales; con las del código serían 39 y 48 m]; desde la
  cima de una fortaleza, ~38–53 m [calc] (45–66 m con las del código). Aro dorado en la arena, cuatro palos con pomos dorados (uno con la bandera de Tortunavy) y
  guirnaldas de banderines; cada rebote suma boing grave, barrido de aire, destellos dorados y la fanfarria (una cada 3 s por
  máquina como mucho).
- **Red**: el rebote lo decide el movimiento de la tortuga al empezar cada paso en que su cápsula toca el sensor (15 cm más
  grande): el servidor y el cliente dueño rebotan en el mismo paso, sin corrección (#21); el resto lo ve por un multicast no
  fiable. Lo potenciado sale de `Spec`.
- **Cómo se usa**: caer encima o andar contra un costado; encadenar dos para subir más; saltar desde uno por encima de un alambre.
  Trampolín delante de un castillo grande = subida rápida.

### 34.3 Catapulta (`ATN_BeachCatapult`, `World/Beach/TN_BeachCatapult.*`)

Cuchara de playa sobre un tapón de garrafa: lanza en **bola** hacia el mar **una sola vez** (después queda partida).

- **Aspecto** (`SizeScale` 1): brazo de 11 m apoyado como balancín sobre un tapón (rojo, azul, verde o blanco) encima de una
  piedra (eje a 2,5 m); según la semilla, una cuchara de plástico, una de madera o dos palos de polo con gomas y un vasito de
  yogur por cazo. Cazo de ~3 × 2,2 m y 30 cm de hondo (no baja de 2,5 × 2 m); en el otro extremo un cubito de arena mojada
  (88 cm de radio, 70 de alto) y un palo de polo que sujeta el mango en alto. Banderín verde (lista) o rojo. Cartel «¡CATAPULTA!».
- **Disparo** (servidor): una tortuga libre en el cazo la arma: **1 s** de aviso (`WarnSeconds`, «¡AGÁRRATE!», el palo tiembla y
  cruje, el banderín parpadea); si el cazo se queda vacío 0,35 s, se desarma. Otra tortuga que sube por el mango y cae de un
  salto sobre el cubito (1 m por encima del mango) dispara al momento. Las del cazo salen a **2300 cm/s y 44°** (±3°) hacia el
  mar con **±12°** de desvío y ±5 % de fuerza (`LaunchSpeed`, `LaunchPitch`, `DeviationDeg`); las del mango, al 55 %
  (`HandleLaunchFraction`). La bola vuela ~30 m (~35 m en llano contando el rodar), rebota y sale sola al pararse (o al caer al
  agua); no se puede salir en el aire. Ahorra camino, pero cae donde toque: entre enemigos, en algas o en una concha.
- **Un solo uso** (`bSingleUse`, decisión de la ronda 3): la primera tortuga que la dispara la gasta. Tras el golpe (**0,62 s**)
  el brazo se parte por el cuello del cazo («¡CRAC!»): el cazo queda colgando, sin banderín, y el cartel se tuerce y dice
  **«¡ROTA!»**. Quien llega tarde la ve ya rota. Hay **al menos 8** por ronda (`MinCatapults`); si faltan, los trampolines de delante
  de un obstáculo pasan a ser catapultas. Recarga (solo con `bSingleUse` apagado): 4,6 s.
- **Potenciada** (la de la cima de las fortalezas): dorada, con la bandera de Tortunavy y guirnalda; lanza a **3800 cm/s y 40°**
  (±1,5°, ±3 %, desvío ±4°): ~77 m en llano, 81–87 m desde una cima; «¡ZAAAS!» dorado, temblor de cámara hasta 42 m y la
  fanfarria (a quien mire desde menos de 90 m).
- **Ascensor**: la torre de la plataforma móvil (§34.5) lleva una catapulta arriba (tamaño ~0,69), que lanza aún más lejos.
- **El temblor del aviso es solo visual** (ronda 4; también en las potenciadas de las fortalezas y en la de la torre del ascensor). Antes se
  sumaba a `ArmPivot`, que lleva las colisiones del brazo y del cazo, y el suelo se movía de verdad a ~10 Hz (±1,6° a 5,5 m del eje: ±15
  cm): despedía a la tortuga, o a su bola de caparazón, que esperaba en el cazo antes del disparo. Ahora `ArmPivot` y `BowlHinge` solo
  siguen el cabeceo real y `ApplyVisualShake` (solo en máquinas con pantalla) mueve las mallas visibles `ArmMesh` y `BowlMesh`, sin
  colisión: cabeceo hasta 0,65°, balanceo hasta 2,5° y guiñada hasta 0,5°, crecientes con el aviso. El cabeceo del disparo sí mueve las
  colisiones, a propósito.
- **La bola de caparazón quieta en el cazo también es pasajera** (ronda 4; la forma divertida de usarla: entrar en el cazo, meterse en el
  caparazón y esperar). En el servidor (`BowlBallOf`): tortuga en su caja física (`ATN_ShellBody`), viva, sin derribar ni aturdir (la bola
  de aturdida no cuenta), sin que la lleve nadie o la sujete un enemigo, con la caja **quieta** (< 220 cm/s: la que pasa rodando no arma la
  catapulta) y su centro dentro del cazo. Arma la catapulta como una de pie (aviso de 1 s), su caja queda quieta durante el aviso (velocidad
  a cero en cada tic del servidor) y sale lanzada con la velocidad del cazo más el giro de volteretas de 7 rad/s (`LaunchBowlBall`); sale
  sola del caparazón al pararse. En vuelo, el árbitro la reserva como `Launch` (10 s) para que la patada de la tormenta no la trate como si
  rodara por gusto. La de pie sigue igual (`IsFreeRider`).
- **Red**: `ArmedAt` y `FiredAt` (horas del servidor) replicadas; las bolas son cajas de caparazón con física (la caja se
  replica sola, sin predicción del movimiento).

### 34.4 Pala (`ATN_BeachSpadeRamp`, `World/Beach/TN_BeachSpadeRamp.*`)

Pala gigante de 11,2 m con `SizeScale` 1 (entre 7 y 11,5 m según la huella): hoja del 36 % (3,4 m de ancho) y mango de 84 cm
de ancho y 34 de grueso por el que se anda. Según la paridad de `Spec.Seed`:

- **Balancín** (par): sobre una piedra redonda (fulcro a 150 cm), la hoja en la arena (−X) y el mango en alto (+X, ~3,3 m; 17° en
  reposo). **Trampolín**: saltar en el último 20 % del mango lanza **1250 cm/s arriba y 700 hacia +X** (`TipUp`, `TipForward`;
  conserva la mitad de la velocidad de lado). **Vuelta**: si una tortuga cae de un salto sobre la mitad del mango (a >60 cm del
  fulcro, con >380 cm/s de caída, `FlipImpactSpeed`), la pala gira: el mango golpea la arena en 0,16 s, se queda hasta 0,9 s y
  vuelve sola a los 2,2 s; **a quien esté en la hoja lo lanza 1500 arriba y 1050 hacia +X** (`CatapultUp`, `CatapultForward`), más
  lejos que el trampolín. Inmunes a la caída hasta aterrizar.
- **Puente-trampolín** (impar): montículo de arena (mango, 70–100 cm de alto, laderas <32°) y roca de 2,2–2,8 m con un charco; la
  pala sube de uno a otra (~20–30°) y el 70 % de la hoja asoma como trampolín de piscina: saltar en el 30 % de la punta lanza igual.
- **Red**: el trampolín lo aplican el servidor y el cliente dueño dentro del mismo movimiento; la vuelta la decide el servidor
  (`FlipAt` replicado) y el cliente de cada víctima aplica el mismo lanzamiento al recibir el aviso.
- La pala de las fortalezas (atajo de la cornisa) es otra pieza (§34.10).

### 34.5 Plataformas móviles (`ATN_BeachMovingPlatform`, `World/Beach/TN_BeachMovingPlatform.*`)

- **Balsa** (semilla par): charco dentro de un cráter de arena (cresta a 1,7 m, taludes de 30° que se suben andando) con agua de
  verdad de **1,28 m** (`ATN_ProcWaterVolume` local en cada máquina; quien cae nada despacio y sale por el talud). Encima flota y
  va y viene a lo largo de X una chancla (6,8 × 2,5 m), una tabla de surf de juguete (6 × 2 m), un disco volador (5,6 m, gira
  14°/s) o la tapa de una fiambrera (5,4 × 3,7 m). Recorrido **8 m** (`Spec.Extent`, se recorta para caber), **3,3 m/s**
  (`FerrySpeed`), **1,6 s** de espera en cada orilla (`FerryDwell`), meciéndose ±3 cm y ±1,2°; andar encima en su sentido suma las
  velocidades. Embarcaderos de palos de polo, espuma, una hoja, una chapa y una concha flotando.
- **Ascensor** (semilla impar): torre cuadrada de arena de molde de ~13 m de lado y **4,5 m** de alto (`Spec.Extent`, 2,5–4,8 m:
  saltar desde arriba no mete en el caparazón). Por su cara −X sube y baja una bandeja o un disco volador de 3,9 m colgado con
  cuatro cuerdas de una grúa de palos de polo: **1,7 m/s** (`LiftSpeed`), **2 s** abajo (`LiftDwellBottom`) y **1,6 s** arriba
  (`LiftDwellTop`). Arriba espera una **catapulta** de un solo uso (§34.3).
- **Base móvil**: la balsa y la bandeja son colisión convexa con nombre estable por red; quien va encima se mueve con ella sin
  resbalar. Posición = `TNBeachRideKit::ShuttleAlpha(hora del servidor + fase por la semilla)`: arranca y frena suave, igual en
  todas las máquinas. Sus sonidos de salida y llegada solo suenan con la cámara a menos de 18 m.
- **Premio**: una concha de 50 encima de las plataformas móviles (hasta 2 por ronda), que se coge montada.

### 34.6 Plataforma sobre un hoyo (`ATN_BeachWobblyPlatform`, `World/Beach/TN_BeachWobblyPlatform.*`)

Una tabla vieja de tres tablones (868 × 220 × 24 cm, con travesaños, clavos y una grieta pintada) o una tapa de nevera (plástico
blanco con reborde de color y bisagras; hasta 300 de ancho) tendida sobre un **hoyo** de arena amontonada (cráter con cresta
redondeada de 90 cm de ancho a 240 de alto, ladera exterior de 30°, pared interior empinada, hoyo de 364 cm de radio arriba y
una brecha de ~2,4 m por el lado +Y por la que se sale andando del fondo). El hoyo va en la malla del elemento (el terreno no se cava).

- **Tambaleo** (la tabla es una base móvil; el muelle lo mueve el servidor y manda la pose y el estado del muelle en siete
  bytes hasta 15 veces por segundo, y en el acto al aterrizar; cada cliente mueve el mismo muelle desde la última muestra): se ladea 4,5° por tortuga según dónde pise (tope 7°, `MaxRollDeg`), cabecea hasta
  2°, se mece al andar y los aterrizajes (caída >250 cm/s) la sacuden; muelle poco amortiguado (~1,2 Hz) y crujidos.
- **Rotura** (servidor): con **2 o más tortugas** a la vez (`BreakRiders`) la grieta sube y en **1,1 s** (`CrackSeconds`) se parte;
  si se bajan, baja a 0,45/s. Al partirse: chasquido, astillas, «¡CRAC!»; las mitades caen (0,5 s) y a los 0,8 s son rampas de
  ~34° de la cresta al fondo. Quien estuviera encima cae al hoyo y sale por la brecha o subiendo por una mitad. **No se recompone
  en la ronda.** Una sola tortuga la cruza sin romperla (solo se ladea y cruje).
- **Cómo se evita**: cruzarla de una en una o rodear el cráter; si se rompe, salir por la brecha o subir por una mitad. Premio: 25
  puntos en el fondo del hoyo.

### 34.7 Puerta de conchas (`ATN_BeachShellGate`, `World/Beach/TN_BeachShellGate.*`)

Hueco de 300 × 330 cm con dos hojas de 147 cm de mosaico de conchas de vieira, marco de cuerda y perla de tirador; bastidor de
madera de deriva con estrella. Según la semilla, en una **pared de arena** (~11,2 m de ancho, 4,4 m de alto y 2 m de grueso, con
almenas) o **entre dos rocas** de ~4,2 m. **Interruptor**: concha grande sobre una peana de 85 cm de radio a 3,8 m por delante
(−X) y a un lado, con un caminito de conchitas hasta la puerta.

- **Reglas**: se abre empujando **0,6 s** (`PushSeconds`: andar contra una hoja cerrada, pegada a ella, desde cualquier lado) o
  pisando el interruptor; se abre en 0,45 s hasta 95° (`OpenDeg`), sigue abierta mientras haya alguien en el hueco (±2,1 m) o en el
  interruptor y **3 s más** (`OpenHold`), y se cierra en 0,7 s; si alguien entra mientras se cierra, se vuelve a abrir. Las hojas
  solo chocan cerradas.
- **Desnuda** (`Extent` > 0, la usa el castillo con salas): solo bastidor (dintel hasta 4,2 m), hojas e interruptor.
- No hace daño: solo cuesta 0,6 s de empuje (o pisar el interruptor).

### 34.8 Algas que enredan (`ATN_BeachSeaweed`, `World/Beach/TN_BeachSeaweed.*`)

Pila de gotas verdes y marrones de 25–65 cm con cintas onduladas y siete tallos que se mecen sobre una mancha de arena mojada;
elipse de 0,9 × huella (6,3 m con `SizeScale` 1; ancho máximo en Y con `Extent`). **Sin colisión**: se vadea con las patas dentro.
Salen en campos de 2–5 manchas (peso 2,4, hasta 240 por ronda).

- **Enganche** (servidor): quien pisa el 85 % central (`CatchFraction`) con los pies en el suelo se queda enganchada **3,2 s**
  (`CatchSeconds`): anda a **55 cm/s** (`HeldSpeed`, también en el aire y en el panzazo) y el salto no la levanta (cada intento es un
  tirón). Cada salto adelanta la suelta 0,4 s (`JumpTug`) y cada meneo (cambiar de golpe de dirección, como mucho uno cada 0,12 s)
  0,2 s (`WiggleTug`): **machacando el salto sale en ~1,3 s**. Si la arrastran fuera de la elipse (×1,2), se suelta. Tras soltarse,
  **2,5 s de gracia** (`ImmuneSeconds`); mientras siga dentro vadea a 320 cm/s (`WadeSpeed`). **Hasta 8 a la vez** (`MaxCatches`;
  eran 4). Si la aturden, se suelta.
- **Red**: `Catches` replicado (tortuga, hora de suelta y tirones); el dueño predice el enganche al pisarla (si el servidor no lo
  confirma en 0,6 s, lo deshace). Las 5 hebras por tortuga que la envuelven (suben en espiral) y los chofs salen del estado replicado.
- **Cómo se evita**: rodearlas (van a menudo en campos), saltarlas a tiempo o, ya dentro, machacar el salto y menear.

### 34.9 Castillo de arena con salas (`ATN_BeachSandDungeon`, `World/Beach/TN_BeachSandDungeon.*`)

Con `SizeScale` 1: **57,6 × 44 m**; murallas de 2,4 m de grueso y 8,2 m de alto con almenas, cuatro torres de 11,5 m con banderas,
zócalo de 50 cm (el suelo de dentro). Todo con colisión, también para la cámara. Recorrido por dentro de ~70 m (rodearlo por el
hueco del embudo de sus alas, más). **Recorrido**: arco de entrada (4 × 4,6 m, a −Y) con rampita → **sala de las columnas** (tres
columnas de cubos) → **puerta de conchas desnuda** de 3,2 m con su interruptor en la sala → **pasillo de las algas** (3,6 m de
ancho, techo a 4,3 m con lucernarios, algas que enredan) → **escalera** de 8 peldaños de 40 × 45 cm → **sala de las ventanas**
(piso de arriba, +3,2 m; dos muretes de 70 cm que se saltan o se rodean; ventanas de 2 × 1,9 m a 80 cm del suelo, se puede saltar
por ellas ~4 m) → **salida** por la puerta alta de la muralla +X (3,6 × 4 m) a una rampa de arena de 6,4 m (30°).

- Las piezas de dentro (puerta de conchas `Extent` 320, algas de tamaño 0,3–0,6) las crea el servidor y se destruyen con el
  castillo. `bSpawnEnemiesInside` (un erizo junto a la escalera y un cangrejo en la sala de las columnas) está **apagado**.
- **Alrededor**: el castillo principal (entre el 42 y el 58 %, ±39 m del centro) tiene dos alas en embudo de decorado grande y
  alambre de espino hasta la selva: o se atraviesa o se rodea por un único hueco de 14 m con algas junto a la selva.
- **Premios**: concha reina de 100 en la sala de arriba (entre los dos muretes), de 25 en la sala de las columnas.

### 34.10 Fortalezas de arena (`ATN_BeachFortress`, `World/Beach/TN_BeachFortress.*`, plantas en `TN_BeachFortressKit.h`)

Castillos inmensos que se suben enteros andando y saltando, con **premio en la cima**. Tres tamaños (`FortressMedium`,
`FortressLarge`, `FortressColossal`; huellas de 22, 34 y 50 m). Una muralla cuadrada con cuatro torres, adarve, puertas y patio y,
en medio, una «mota» de terrazas macizas de arena de molde, cada una más alta y pequeña que la anterior (la última es la cima).
`SizeScale` 0,85–1,2 (el reparto usa 0,94–1,06); lo que tiene medidas de tortuga no se escala.

| Con `SizeScale` 1 | Mediana | Grande | Colosal |
|---|---|---|---|
| Muralla (lado, grosor, adarve) | 28 m, 3,8 m, a 5 m | 43 m, 4,4 m, a 6,5 m | 64 m, 5,2 m, a 8 m |
| Torres (radio; cubo y bandera hasta) | 2,9 m; 10,5 m | 3,4 m; 12,9 m | 4 m; 15,4 m |
| Terrazas (lado a altura) | — | 25 m a 11 m | 42 m a 12,5 m y 28 m a 19 m |
| Cima (lado a altura) | 11,2 m a 8,5 m | 12,6 m a 16,5 m | 14,8 m a 25,5 m |
| Radio real (esquina de las torres) | 21,3 m | 32 m | 47,1 m |
| Puertas (ancho × alto) | 4,4 × 3,8 m | 4,4 × 4,8 m | 4,4 × 4,8 m |
| Premio de puntos en total | 150 | 300 | 450 |

- **Muralla**: adarve con pretil de 55 cm por fuera (se salta) y sin pretil por dentro; puertas de 4,4 m a −X (de tierra) y a +X (del
  mar) con arco y dos estandartes de Tortunavy: el patio se cruza de una a otra. Torres con un cubo de juguete boca abajo de
  torreta con la bandera de Tortunavy. Patio con franjas de 4,6 m (5,8 m en la colosal).
- **Subidas**: por fuera, rampa de 3,8 m y 20° pegada a la cara −Y y escalera de 3 m por la cara +Y (13 / 17 / 20 peldaños de 45 cm);
  por dentro, escalera de 2,6 m del patio al adarve y la **rampa del patio** (28° / 20° / 14,5°); de terraza en terraza, rampas de
  3,4 m de 24–25° alternando lados (la espiral). Todo se refleja en Y según la semilla.
- **Atajos arriesgados**: **salto de torrecillas** (6 / 8 / 8 torrecillas de cubo, cada una ~50 cm más alta; caerse es volver al
  patio), **la pala y la cornisa** (una pala de juguete tendida lleva a una cornisa de 70 cm colgada de la primera terraza que sube
  a 30°) y, en la colosal, otra fila de 10 torrecillas por la terraza +X.
- **Premio en la cima** (lo crea el servidor al construirla y lo destruye con ella): una **catapulta potenciada** en el borde +X
  (siempre, #741; tamaño 0,89 / 1,02 / 1,15), un **cofre** (§34.11) que da lo mejor de la carrera para cualquier puesto (fuente
  `Summit`: la tabla de las últimas), y
  **conchas de puntos**: 100 + 50 (mediana), 100 + 50 + 50 (grande), 100 + 100 + 50 + 50 (colosal) en las esquinas de la cima, más
  una de 50 al final de cada atajo de las terrazas de en medio (grande 2, colosal 3).
- **Caída del lanzador**: el reparto reserva una franja de 16 m de ancho entre 40 y 100 m del centro hacia su +X: la catapulta
  potenciada cae a 80–90 m (±6 m) y aún rebota. (El trampolín, que ya no sale en las cimas, caía a ~38–47 m andando y ~44–53 m esprintando.)
- **Guardias**: en cada fortaleza, 2, 3 o 5 enemigos alrededor (cangrejos, erizos, lagartos y algún tanque) sobre todo por delante.
- **Rodeo**: 25 m libres entre la muralla y la selva por cada lado (`FortressDetour`) y 12 m hasta lo que ya hubiera (`FortressPad`).
- **Pruebas**: `TN.Beach.Place FortressColossal 1 0 <semilla>`, `TN.Beach.Fortress.Top [jugador]`, `TN.Beach.PlaceBoosted`.

### 34.11 Cofres (`ATN_BeachChest` y `ATN_BeachChestSpot`, `World/Beach/TN_BeachChest.*`)

Como el cofre del lobby, pero de la playa: **se tarda en abrir y da de lo mejor**. Los ponen el reparto (tras una concha que
atrapa, rincones escondidos, junto a las trincheras, en medio de un campo de minas, pasado el arco de un lanzador, a la espalda
de castillos, rocas grandes, troncos y restos de barco, tras el alambre de las filas y las alas) y las fortalezas (en su cima).
Dos actores: `ATN_BeachChest` es el elemento del reparto (solo servidor, no se replica) y crea el cofre de verdad,
`ATN_BeachChestSpot` (hereda de `ATN_BeachSearchSpot`), que es lo que se ve, se abre y se replica.

- **Aspecto**: el cofre del lobby 2,2 veces más grande (3,4 m de ancho, 2,2 m de fondo, 2,6 m de alto con la tapa), madera
  blanqueada, herrajes oxidados, percebes, algas, estrella de mar y arena al pie; dentro, monedas, gemas, una copa, perlas y una
  vieira. Por abrir: **columna de luz dorada** (se ve a 450 m) y luz que late por la rendija.
- **Abrirlo**: mantener E **5,5 s** (`OpenSeconds`; no hace caso de `tn.Search.Seconds`). La tapa cruje y se entreabre a tirones
  (de 12° a 48°); soltar antes cancela (cae con «¡clonc!»). Mientras una tortuga lo abre, las demás no pueden.
- **Premio** (siempre; no hace caso de `tn.Search.Luck`): «¡puf!» y la tapa salta abierta (104°) con chispas: **un objeto** hacia
  quien lo abrió, **un objeto más** y **seis conchas de puntos** (cuatro de 25, una de 50 y otra de 50 o, el 40 % de las veces, una
  reina de 100: 200 o 250 puntos) que saltan en parábolas altas y caen en corona a 1–3 m del borde, dejando libre el frente de
  quien lo abrió. Los objetos se sortean **por el puesto** de quien abre (`TNRaceItems::RollLoot`, fuente `Chest`): energía sin fin
  ×2, barra llena ×1,6, pelícano taxi ×1,6, coco dorado ×2, protector solar ×1,4 y triple coco ×1,4; concha trampa y silbato ×0,6; la
  cabezota y el tótem, nunca (pesos completos en el §29.5). El cofre de la **cima de una fortaleza** (`TNBeach::FlagSummitPrize`,
  fuente `Summit`, #741) sortea con la tabla de las últimas para cualquier puesto: hasta la primera puede sacar el pelícano taxi,
  el protector solar, el triple coco o el coco dorado (~63 % de los pesos de los objetos de carrera con 4 tortugas).
- **Después**: una vez por ronda; queda abierto y vacío, con brillo dorado apagado; la columna se estrecha y se va en 0,8 s.
- **Cantidad**: 12 de sitio especial en Normal (`ChestsBase` = 18 × 2/3, por las ayudas de la dificultad; los limitan los sitios
  donde caben) más los de los rincones y los de las cimas de las fortalezas: en total 15–18 en Normal (media 17), 11–23 en
  Fácil y 15–22 en Difícil.
- **Red**: relevante a 400 m (lo de serie son 150); los premios y dónde cae cada uno van replicados.

### 34.12 Rebuscables y montículos que vibran (`TN_BeachLoot`, `World/Beach/TN_BeachLoot.*`, `TN_BeachSearchMounds.cpp`)

En la playa se rebusca en casi todo el decorado (tablas de §33.3) con mantener E **1,3 s**: **70 % de suerte** (55 % en el
cooperativo; `SearchLuck`), pesos por puesto de quien rebusca, saltito del objeto, anillo dorado fijo en su montículo de arena
y chispitas desde 35 m. La huella es la caja real de la malla (cápsula a lo largo del lado largo; la sombrilla se rebusca en el
montón de arena de su pie). **Uno por corrillo** (9 m entre centros, 3 m entre bordes), **hasta 240 por ronda y 50 por sexto del
recorrido** (360 y 75 con 1200 m), ×1,6 en Fácil y ×1,4 en Difícil.

- **Montículo de arena que vibra** (`ATN_BeachSearchRegistry`): junto a cada rebuscable hay un montón de arena removida de
  1,6–2,5 m de ancho y 30–48 cm de alto (lisa, con una chapa roja de canto, con un palito de helado o con un trozo de concha), del
  lado por el que se llega. Lejos está quieto e instanciado (sin sombra, hasta 120 m); cerca de una cámara local (45 m, los 16 más
  cercanos) **tiembla a ratos** (0,35–0,6 s cada 2,5–6 s, 3 granitos) y con una **tortuga a menos de 12 m** más fuerte y a menudo
  (0,55–0,9 s cada 0,4–1,2 s, 7 granitos). Rebuscado, se aplasta en 0,35 s y queda **aplanado y quieto**. Son del orden de 50–80 por
  ronda. Solo existen en la playa (en el cooperativo, pendiente).
- **Estado replicado**: un bit por punto en el registro (siempre relevante, dormido salvo al cambiar); el actor del rebuscable solo
  existe cuando hay una tortuga a menos de 50 m (se quita a 70 m) y es relevante a 90 m.
- **Objetos sueltos**: 20–25 por tramos iguales (desde 90 m hasta 30 m del filo, más hacia el centro que hacia la selva) más **3
  filas de lado a lado** (al 17, 50 y 83 %, ±6 %; un objeto cada 32 m de selva a selva); son las **cajas de objetos** (§34.14).

### 34.13 Conchas de puntos de la playa (`ATN_ScorePickup`, `World/Beach/TN_BeachLootShells.cpp`)

Las mismas conchas que el cooperativo (1, 25, 50 y 100; tabla en §32.13) que suman `RaceScore`. Topes por ronda: **200 de 1, 27
de 25, 8 de 50 y 2 de 100**. Se planean lo difícil primero, con 1 m entre conchitas y 3 m alrededor de las demás:

| Tamaño | Dónde |
|---|---|
| Reinas de 100 | sala de arriba del castillo con salas; lo alto del castillo enorme |
| Grandes de 50 | rincones escondidos (hasta 3), primeras trincheras, tras el alambre (hasta 3, a 2,6 m hacia el mar), tras las dos primeras minas, lo alto de dos castillos pequeños, encima de las plataformas móviles (hasta 2) |
| Normales de 25 | entre las algas (5), dentro de la concha que atrapa (4), en el fondo del hoyo de la plataforma (3), dentro del cubo roto (3), dos por paso de quads en sus rodadas, en casa de cangrejos y erizos (5), bajo las gaviotas (3), tras los sacos (4), tras las demás minas (60 %), sala de las columnas, lo alto de tres castillos pequeños y las cimas de las crestas (6) |
| Conchitas de 1 | arcos de 7 que dibujan el vuelo de palas, trampolines y catapultas; rachas por los atajos, los caminos alternativos, encima de las pasarelas y por los caminitos de palos (hasta 14 por tramo y 110 en total), por el hueco con algas del castillo (11), serpenteando por los lados (6–9) |

Nada a menos de 15 m de la salida. Las conchas de un cofre son aparte. Aproximadamente 230 por ronda, dormidas y relevantes a
200 m (1 Hz cuando cambian).

### 34.14 Cajas de objetos (`ATN_RaceItemBox`, `World/Beach/TN_RaceItemBox.*`)

El «?» de las carreras de karts: cubo de juguete de colores con una «?» que flota y gira; en cada sitio de objeto suelto hay una
(unas 35–45 por ronda). Al cogerla sale un objeto **sorteado según el puesto** de quien la coge (fuente `Box`; tabla en el §29.5). No
reaparecen durante la ronda y no dicen lo que dan. Es un `ATN_PickupInteractableBase` con brillo dorado de «aquí hay algo».

### 34.15 Carteles de madera de los lanzadores (`TN_BeachSignKit.h`)

Para saber de lejos qué es cada catapulta o trampolín (también los de las cimas y el del ascensor): tabla de madera de **3,2 × 1,8 m** en
dos postes (arriba a 3,3 m), clavada en la arena **por el lado por el que se llega** (−X) y a un lado (+Y o −Y según la semilla),
**sin colisión**. Icono pintado con color de vértice y rótulo en `TextRender` (58 cm de letra, crema sobre franja marrón): el
trampolín, cúpula y flecha roja que rebota, **«¡BOING!»**; la catapulta, palanca con bolita y flecha azul, **«¡CATAPULTA!»**;
partida, cinta roja en aspa y **«¡ROTA!»**. Los potenciados: tabla dorada, estrellitas y una estrella dorada encima. Da un botecito
cuando la tortuga local se acerca a menos de 9 m y aclara el rótulo a menos de 15 m. Es cosmético y local.

## 35. Trampas de la playa

Categoría «trampa» del contrato (`ETNBeachCategory::Trap`): 16 tipos, de los que 8 son **ayudas** para la dificultad (catapulta, trampolín,
pala, plataforma móvil, las tres fortalezas y el cofre; §34.2–§34.11), uno es una estructura (el castillo con salas, §34.9) y 7 molestan o
castigan (alambre, algas, plataforma sobre un hoyo, cubo roto, puerta de conchas, concha que atrapa y mina). Se reparten con `SeaBias`
0,25 (algo más hacia el mar) y no cuentan como cierre del paso salvo el alambre, el castillo con salas y las fortalezas. Con la dificultad, las trampas se multiplican por 0,7 / 1 / 1,8 (§37.3).

### 35.1 Resumen

| Trampa | Qué hace | Cifras clave | Cómo se evita | Cantidad por ronda (F / N / D) | Archivo |
|---|---|---|---|---|---|
| Alambre de espino (`BarbedWire`) | aturde y empuja hacia atrás | 1,2 s de bola, empujón 750 cm/s + 420 arriba; 2,7 s sin repetir | saltarlo con carrerilla o con un trampolín, rodearlo por la punta; van en filas y alas | dentro de las filas y las alas del castillo | `World/Beach/TN_BeachBarbedWire.*` |
| Cubo roto (`BrokenBucket`) | nada: es un túnel que se cruza de pie (atajo, cobijo y 3 conchas de 25 dentro) | 5 m de largo, ~2,5 m libres en la salida | — | 7–9 en Normal en su pasada | `World/Beach/TN_BeachBrokenBucket.*` |
| Concha que atrapa (`ClamTrap`) | se cierra y retiene 3,2–4 s; escupe mareada | temblor 0,25 s, cierre 0,14 s, recarga 4,5 s | cruzar por el borde esprintando (quien sale en el aviso se escapa) | 11 / 17 / 23 | `World/Beach/TN_BeachClamTrap.*` |
| Mina (`Mine`) | tras un «clic», explota a los 0,4 s y lanza en bola hacia atrás | radio 2,3 m; bola 2,4 s; ~7–8 m atrás; se rearma a los 9 s | verla (bandera roja en el 45 %, piloto rojo), saltarla, rodearla | 36 / 52 / 79 | `World/Beach/TN_BeachMine.*` |
| Algas que enredan (`Seaweed`) | frenan a 55 cm/s durante 3,2 s | ver §34.8 | machacar el salto, rodearlas | 13 / 19,5 / 15 | `World/Beach/TN_BeachSeaweed.*` |
| Plataforma sobre un hoyo (`WobblyPlatform`) | se parte con 2 o más encima; caída al hoyo | ver §34.6 | cruzar de una en una | 8–11 en su pasada (Normal) | `World/Beach/TN_BeachWobblyPlatform.*` |
| Puerta de conchas (`ShellGate`) | molesta: hay que empujar 0,6 s | ver §34.7 | empujar o pisar el interruptor | 5–7 en su pasada (Normal) | `World/Beach/TN_BeachShellGate.*` |
| Erizo de mar (`SeaUrchin`) | rueda hacia ti y pincha (derribo con ragdoll) | 2,1 m/s | andar más deprisa; es un enemigo (§36.4) | 17 / 25 / 74 | `World/Beach/TN_BeachSeaUrchin.*` |
| Erizo antitanque (`TankTrap`) | solo estorba (decorado militar) | barras de 7,8 m a 35°, cruce a 2,25 m | pasar por debajo entre dos patas o rodearlo | en filas (1–2 por ronda) y en las trincheras | `TN_BeachMilitaryMeshes.h` |

### 35.2 Alambre de espino (`ATN_BeachBarbedWire`)

- **Aspecto**: concertina de rollos de 66 × `SizeScale` cm de radio (52–84 cm: ~1,3 m de alto) a lo largo de X, ±56 cm en Y, con
  pinchos en cruz, un hilo tenso por encima, estacas de madera cada ~7 m (`StakeSpacing`) y trozos sueltos en los extremos. El largo
  es `Spec.Extent` (por defecto 24 m; el reparto lo pone entre 22 y 60 m, de través ±25°). Bloquea: cajas de colisión por tramos de
  ~4 m (la cámara las atraviesa).
- **Servidor**: si la cápsula queda a menos de **14 cm** de la colisión (a los lados o encima), `TNBeach::StunTurtle` **1,2 s**
  (`StunSeconds`) con un empujón de **750 cm/s** hacia el lado del que venía (si ya está encima, al contrario de su marcha) y **420**
  hacia arriba (`PushSpeed`, `PushUp`). Una vez por tortuga cada 2,7 s (aturdimiento + `HitCooldown` 1,5 s).
- **Efectos** (todas las máquinas, multicast no fiable): chispas, esquirlas, chispazo y «¡ay!» sintetizados y un «¡AY!» rojo flotando.
- **Dónde va**: peso 0,9 y `SeaBias` 0,4; en las filas que obligan a zigzaguear, en las alas del castillo con salas hasta la
  selva, y tras él se esconde un cofre o una concha de 50 (hasta 3, a 2,6 m hacia el mar). Con un trampolín o una catapulta se
  sobrevuela.

### 35.3 Cubo roto (`ATN_BeachBrokenBucket`)

Cubo de juguete de 18 cm a escala tumbado y roto: 504 cm de largo, 238 de radio en la boca y 182 en el culo, pared de 26 (×0,7–1,15
para caber en la huella de 4,5 m). Boca en −X con una **rampa de arena** (170) al suelo de dentro (plano, a 69 cm) y culo en +X con un
**agujero dentado** del 74 % del radio y una lengua de arena (150) que baja: ~2,5 m libres en la salida, se cruza de pie en los
dos sentidos. Asa caída sobre el lomo, nervios, reborde, pegatina de estrella, grietas y colores de juguete desteñidos. Estático (sin
Tick). No hace daño: es un pasillo con cobijo (y 3 conchas de 25 dentro).

### 35.4 Concha que atrapa (`ATN_BeachClamTrap`) — la «almeja trampa»

- **Aspecto** (`SizeScale` 1): almeja gigante de **9 × 6,5 m** con la valva de abajo medio enterrada (labio en zigzag a 38 cm de la
  arena, montículo que se sube andando), manto de colores dentro (azul eléctrico, verde con oro, morado con azul o dorado con ojos
  azules), sifón y una **perla** de ~50 cm que destella cuando está lista. La valva de arriba, abierta **68°**, respira ±2°. Cerrada
  deja ~1,7 m libres dentro y una rendija de 5 cm.
- **Cierre** (servidor): una tortuga libre que pisa el manto (el 72 % central de la elipse, con los pies en el suelo) la hace
  temblar **0,25 s** (`TellSeconds`: la valva sube 7° y castañetea) y cerrarse en **0,14 s** (`CloseSeconds`). Atrapa a la tortuga libre
  más cercana al centro que siga dentro (el 86 % central; solo una): **quien corre y sale durante el aviso se escapa**. A las demás
  que estén en la valva las despide a **700 cm/s** hacia fuera y **450** arriba (`ShoveOut`, `ShoveUp`). Sin nadie dentro se queda
  cerrada **1 s** y se abre.
- **Dentro** (**3,2–4 s** al azar, `HoldMin`/`HoldMax`): la presa queda quieta y sin control (`MOVE_None`), su cámara pasa a la de la
  concha (fuera, ~12 m y 4,7 m de alto, fundido de 0,35 s); la concha vibra a sacudidas, con golpes sordos, «¡ay!» ahogados y humo;
  «¡ÑAM!» al atrapar.
- **Suelta**: se abre en **0,4 s** y a los **0,16 s** (`SpitDelay`) la escupe de un saltito (**560 cm/s** hacia la boca y el mar y **560**
  arriba, ~6 m; «¡PTUI!»). Queda **mareada 1 s** desde que aterriza (`DizzySeconds`; tope de 3 s desde el saltito). Esa concha no
  la vuelve a atrapar en 3 s. **Recarga**: **4,5 s** tras abrirse, con el manto encogido y la perla apagada; al estar lista, «plin».
- **Se suelta antes** (sin saltito ni mareo) si la presa muere, la aturden, se mete en el caparazón, la derriban, la cogen o se
  desconecta; en el recuento o el podio la deja donde está.
- **Red**: `State` replicado (`SnapAt`, `OpenAt`, `SpitAt`, `Captive`); el cliente de la presa la sujeta al recibir `Captive` y la
  suelta solo a la hora `OpenAt + SpitDelay`. Una concha que se come una bola que para dentro (catapulta) también la retiene.
- **Premio**: 25 puntos dentro de la concha (4 por ronda) y un cofre detrás (dos sitios por concha, a 1,5–4 m de su espalda).
- Peso 1,2, hasta 60 por ronda, con `SeaBias` 0,6.

### 35.5 Mina de la playa (`ATN_BeachMine`)

- **Aspecto** (`SizeScale` 1): montoncito de arena removida de 1,28 m de radio con una mina de juguete de ~5 cm (1,4 m) medio
  enterrada: plato verde oliva con franja amarilla, bote gris de tres pinchos, plato oxidado con percebes o juguete caqui con botón
  rojo (según la semilla). Asoman la tapa (22 cm; el bote, 16) y el pincho de la espoleta (hasta 30 cm); un **piloto rojo** da un
  destello cada 1,6 s. El **45 %** lleva una banderita roja de aviso con franja blanca a 1,9 m. **Sin colisión: se pisa.**
- **Pisada** (servidor): una tortuga viva, fuera del caparazón y sin aturdir con los pies sobre la tapa (a menos de 70 cm + el 45 %
  del radio de su cápsula en planta y entre 60 cm por debajo y 45 cm por encima de la tapa: también cayendo encima de un salto;
  saltándola por encima, no). «¡clic!», la tapa se hunde 5 cm y parpadea pitando cada vez más deprisa durante **0,4 s**
  (`FuseSeconds`). Solo con la carrera en marcha (`IsRaceLive`).
- **Explosión**: toda tortuga viva a menos de **2,3 m** (`BlastRadius`; la que la pisó, hasta el doble) sale **en bola hacia atrás**
  (contrario al mar): aturdida **2,4 s** (`StunSeconds`) con **420 cm/s hacia atrás**, **950 arriba** y hasta 160 de lado (~1,9 s de
  vuelo y ~7–8 m hacia atrás). Las de alrededor, hasta **7 m** (`PushRadius`), en pie y sin aturdir, reciben un empujón de **750
  cm/s** y 450 arriba, un tercio en el borde. Fogonazo, bola de fuego, arena, «¡BUM!», temblor de cámara (0,8 hasta 7 m, apagándose
  hasta 32 m) y sonidos sintetizados (`TN_BeachMineSynth.*`). La mina no lanza a la tortuga sujeta ni a la comida por un gusano.
- **Cráter y rearme**: queda un cráter de adorno (suelo chamuscado de 1,5 m, reborde de 26 cm hasta 2,6 m); a los **9 s**
  (`RearmSeconds`) la mina vuelve a asomar en medio con un botecito y un clic-clac. Todas las tortugas se encuentran la misma
  playa; quien viene justo detrás pasa sin peligro. `RearmSeconds` = 0 la deja gastada.
- **Dónde**: peso 0,35, en corrillos de hasta 3 (30 %), hasta 150 por ronda, y **campos de minas** (2–3 por ronda en Normal
  con 5–9 minas cada uno, tras la línea de erizos de las trincheras y en medio de los cofres). Concha de 50 tras las dos primeras.
- **Red**: `TriggeredAt` y `ExplodedAt` replicados; cada máquina anima parpadeo, pitidos, explosión, cráter y rearme con su reloj
  suavizado; quien llega tarde ve el estado sin oírlo.
- No confundir con la **mina de arena** del objeto de carrera (§36.14): esa la lanza una tortuga.

### 35.6 Lo que estorba sin ser una trampa

- **Erizo antitanque** (decorado militar, `TankTrap`): barras de 7,8 m a 35° cruzadas a 2,25 m, con colisión por barra: se pasa por
  debajo entre dos patas o se rodea; el reparto los pone en filas 18–26 m por delante de la trinchera del mar. **No es el erizo
  de mar**, que rueda (§36.4).
- **Medusa varada** (decorado, `StrandedJellyfish`): se sube andando (casquete de 38°); no daña ni se rebusca.
- **Túneles y cobijos que se cruzan de pie**: vaso de plástico tumbado, tronco hueco, cubo roto, casco de lado, red de camuflaje
  a dos aguas, castillos con salas. Lo que tiene un techo por encima (la lona de la sombrilla clavada, el castillo con salas…)
  da **cobertura frente a la gaviota** (§36.7).

## 36. Enemigos y amenazas

### 36.1 Resumen de la carrera (nueve enemigos del reparto, la tormenta y el gusano)

Ninguno mata: **aturden** (bola temblando), **derriban** (ragdoll con mareo) o **sujetan** un rato. La dificultad solo cambia
**cuántos** hay (multiplicador de enemigos ×0,6 / ×1 / ×2,5; §37.3), no cómo se comportan. Cantidades medias por ronda
(Fácil / Normal / Difícil, 24 semillas medidas con 800 m):

| Enemigo | Cuándo y dónde aparece | Cómo detecta | Cómo ataca (cifras) | Cómo se evita o se aturde | Cantidad (F / N / D) |
|---|---|---|---|---|---|
| Cangrejo gigante (§36.3) | desde los 16 m, en el relleno y de guardia; en grupos de 2–3 | vista de frente ±70° a 22 m; oído 10 m (13 m corriendo, 6 m agachada o casi quieta); correa 38 m | mazazo de pinza a ~8,1 m (bola aturdida 3,5 s) o embestida a 8,5–13 m (derribo 2,4 s, lanzada a 9,5 m/s) | salir de la sombra en el aviso de 0,6 s, salir de su vista o de su correa, ponerse tras una roca; persigue a 5,6 m/s, más que una tortuga esprintando (4 m/s) | 15 / 21 / 60 |
| Erizo de mar (§36.4) | a partir de los 64 m, más hacia el mar | vibraciones a 22 m | rueda a 2,1 m/s y pincha a ~1,65 m: derribo 2,4 s | correr (4 m/s; andando a 2 m/s apenas le saca ventaja) o rodearlo; lo lanzado lo marea | 17 / 25 / 74 |
| Lagarto (§36.5) | por los lados de la playa, antes del 88 % | alerta a 30 m; susto a 17 m; huye a 9 m | huidizo: empujón 6,5 m/s; mordedor: mordisco a 2,6 m y zarandeo 1,3 s | mordedor: ir en bola, agachada o en el aire; huidizo/generoso: dejarlo huir | 10 / 16 / 42 |
| Paso de quads (§36.6) | 1–2 franjas de lado a lado (2–3 en Difícil) | aviso de 3,5 s (temblor, humo, motor) | rueda que pasa por encima: derribo 3 s, lanzada 9,5 m/s | fuera de su paso o en el hueco entre ruedas (10,6 m); no se marea | 1–2 / 1–2 / 2–3 |
| Gaviotas y pelícanos (§36.7) | 3–4 zonas por ronda, por encima; atacan cada 4–7 s | tortuga a menos del 80 % de la huella desde el centro (con techo encima, el ataque falla) | cagada (derribo 2,4 s + mancha 12 s) o picado con agarre (colgada 3,3 s, bola 4 s) | plancha en el momento justo, caparazón (picado) o cubrirse; el blanco te sigue a 2,5 m/s (más que andando, menos que corriendo: esprintando en línea recta se libra, #636) | 2–3 / 3–4 / 4–6 zonas |
| Ermitaño bola (§36.8) | en calles cuesta abajo de 25–45 m | tortuga en su calle a menos de 5,2 m del eje | rueda a 3–15 m/s y derriba a todas las de la fila (2,5 s) | apartarse 3 m de lado, subir el terreno de la calle | 4,7 / 6,3 / 8,5 |
| Pulpo de poza (§36.9) | dentro del agua de las 7 pozas | nadadora atacable dentro de la orilla | agarre a 3 m (0,8 s en el aire) y lanzamiento en bola de 9–34 m hacia la salida | salir nadando antes de que llegue, no nadar | 7,1 / 11 / 26 |
| Pulgas de arena (§36.10) | en claros de arena abierta | tortuga a 18 m dentro de su correa (24 m) | picada a 2,7 m: 2 s de saltitos sin control + bola 1 s | andar (van a 1,6 m/s), lanzarles algo | 4 / 6,7 / 16,7 |
| Tanque de juguete (§36.11) | tramos de través de 20–40 m junto a lo militar y las trincheras | vista de 25 m | bolita de espuma cada 4 s: bola mareada 0,8 s | correr de lado, esconderse; es sólido | 4,7 / 6,6 / 7,8 |
| Tormenta de bañistas (§36.12) | sale 30 m detrás de la salida al dar la salida | por posición (quien queda detrás del frente) | patada a arena abierta ~20 m por delante del frente | ir por delante de 1,8 m/s | una por ronda |
| Gusano de arena (§36.13) | al acabar la cuenta de 10 s, uno por rezagada | todas las que no han llegado | se las come (3,2 s), sin daño; reaparecen en la siguiente ronda | llegar al agua antes de que acabe la cuenta | 0–N según rezagadas |

### 36.2 Reglas comunes (`ATN_BeachEnemy`, `World/Beach/TN_BeachEnemy.*`)

- **Base**: todos los enemigos del reparto son hijos de `ATN_BeachEnemy` (abstracta, hija de `ATN_BeachElement`): red, tortugas, suelo,
  voz, golpes, apartarse, rodear el reparto, nivel de detalle, mareo por lo lanzado y la tortuga sujeta en la boca o el pico. Kit
  de mallas y sonidos en `TN_BeachEnemyKit.h`, `TN_BeachEnemyMeshes.h` y `TN_BeachEnemySynth.*`; consola en `TN_BeachEnemyDebug.cpp`.
- **Escala**: todo a `TNBeach::Scale` (28). Se reutiliza la fauna low-poly del mapa procedural (gaviota, pelícano, cuadrúpedo del
  lagarto), `TNAmbientFX` y la tos de la tormenta.
- **Carrera en marcha** (`IsRaceLive`): no atacan durante el recuento ni el podio (`RoundResults` o `Champion`); en `Waiting` sí.
- **A quién se le da** (`CanBeHit`): viva, sin aturdir, sin derribar, sin ir en el pico o la boca de un enemigo (`IsTurtleHeld`), sin
  que la recoloquen la patada de la tormenta o la red de seguridad, y sin ser invulnerable (protector solar, pelícano taxi). A quien
  ya está en el suelo no le da nadie. Si otro sistema le quita la tortuga a un enemigo, este deja el ataque (`OnHoldAborted`).
- **Golpes variados** (`KnockDownTurtle`, `StunTurtle`; el empujón del ragdoll va a los cuerpos en cada máquina para que salga
  igual en todas):

| Quién | Qué te pasa | Después |
|---|---|---|
| Cangrejo (mazazo) | despachurrada en bola aturdida 3,5 s, empujoncito de 3,2 m/s hacia fuera | te ignora 6 s |
| Cangrejo (embestida) | derribo con mareo 2,4 s, lanzada a 9,5 m/s en su sentido, 2,6 hacia fuera y 4,8 arriba, dando vueltas (320°/s); «¡EMBESTIDA!» | te ignora 6 s |
| Erizo (pinchazo) | derribo 2,4 s, despedida a 6,2 m/s hacia fuera y 3,8 arriba con una vuelta hacia atrás (260°/s); «¡PINCHAZO!» | te ignora 4,5 s |
| Gaviota (cagada) | derribo 2,4 s, tumbada de espaldas (2,4 m/s hacia fuera y 1,2 arriba), cagada pintada 12 s; «¡PLOF!» | la zona te deja 6 s |
| Gaviota o pelícano (picado) | colgada del pico 3,3 s, pataleando; al soltarte, bola aturdida lo que tardas en caer (~2,3 s) + 2 s; «¡ÑAC!» | la zona te deja 12 s |
| Quad (rueda) | derribo 3 s, lanzada a 9,5 m/s en su sentido, 3,8 de lado y 7,5 arriba, vueltas de campana (420°/s); «¡ATROPELLO!» | 1,2 s sin repetir |
| Tormenta (patada) | un bañista la manda a arena abierta ~20 m por delante del frente: en bola si el arco está libre (vuelo de 1,1–3,2 s, mareada el vuelo + 0,8 s) o de un salto con polvo; «¡PATADA!» | 3 s sin patadas |
| Lagarto huidizo (susto) | empujón de 6,5 m/s, sin derribar ni aturdir | — |
| Lagarto mordedor (mordisco) | en su boca 1,3 s, zarandeada y lanzada de lado (6,5 m/s y 4,5 arriba) en bola mareada 1,5 s; «¡ÑAM!» | te deja 10 s |
| Ermitaño (bola) | derribo 2,5 s, lanzada a 3,2 m/s + 55 % de la velocidad de la bola (13 m/s como mucho), 2,4 de lado y 4,3 arriba (320°/s); «¡BOLO!» y, desde la segunda, «¡STRIKE!» | te ignora 3 s |
| Pulpo (agarre) | agarrada 0,8 s (2,3 m sobre el agua) y lanzada en bola fuera de la poza hacia la salida (9–34 m), mareada el vuelo + 1,2 s; «¡SLURP!», «¡FUERA!» | te ignora 5 s |
| Pulgas (picada) | 2 s de saltitos sin control y picor, luego bola mareada 1 s; «¡PICA, PICA!» | te ignoran 6 s |
| Tanque (bolita) | bola mareada 0,8 s, empujada a 5,2 m/s y 2,6 arriba; «¡PAF!» | otra en cuanto te recuperas |

- **Muchos a la vez**: los que andan se apartan entre sí (`GetBodyRadius`: cangrejo 4,2 m, erizo 1,15 veces su radio de rodar,
  lagarto 3,8 m); el cangrejo y el erizo **rodean lo grande del reparto** (trampas salvo algas y decorado que cierra el paso, de
  3,8 m de huella o más) con `SteerAroundObstacles` (sonda por delante, se abre hacia el lado que más se parece a donde quiere
  ir); el lagarto no (se mete debajo de las rocas). Sus paseos nunca eligen una meta dentro de un obstáculo y se rinden a los
  7–12 s. El suelo sale del generador (`GetGroundHeightAt`, sin trazas).
- **Nivel de detalle** (cangrejos, erizos y lagartos, `bThrottleWhenFar`): cada 0,5 s miran la tortuga más cercana y la cámara. Con
  una tortuga a su alcance (cangrejo ~75 m, erizo ~53 m, lagarto 45 m) o la cámara a menos de 150 m se mueven cada fotograma; si
  no, cada 66 ms a la vista (hasta 300 m) o cada 250 ms, y la red baja a 3 Hz (8–12 Hz de cerca).
- **Mareo por lo que se les lanza** (`ApplyHitStun`): una piedra, la bola lanzable o el pulpo de tinta a un enemigo vivo: «¡TOING!»,
  pajaritos y estrellas sobre su cabeza y **no ataca ~3 s** (`ThrownSeconds`; una bola de caparazón a ≥9 m/s, 2,5 s). Lo mismo el
  silbato del sargento (5 s), el disco (4 s), el protector solar (4 s), la mina de arena (5 s) y el cangrejo teledirigido (4 s).

| Enemigo | Cuerpo que recibe lo lanzado | Mareado |
|---|---|---|
| Cangrejo | cápsula de costado a costado del caparazón (5 m) | se para en seco, ojos que dan vueltas, pinza caída; ni persigue ni ataca. También con la concha trampa y si se estampa embistiendo (1,6 s) |
| Erizo | esfera del cuerpo | quieto, tambaleándose; ni rueda ni pincha |
| Lagarto | del cuello a las caderas (escondido, nada) | tumbado de lado, cabeza caída y lengua fuera; si mordía, suelta a la tortuga; luego huye |
| Gaviota o pelícano | solo el que baja en picado, el que lleva una tortuga y el ya mareado, con el cuerpo a menos de 18 m de la arena | suelta a la tortuga (cae en bola), cae a la arena dando tumbos, se queda sentado con las alas caídas; luego despega (1,8 s) |
| Ermitaño | esfera de la caracola | rodando, se para en seco; asoma mareado (0,6 s como poco) y vuelve andando a lo alto |
| Pulpo | cápsula vertical del cuerpo (también bajo el agua) | suelta a la agarrada (cae al agua); flota de lado (0,8 s como poco) y se hunde |
| Pulgas | la nube (0,9 de su radio) | se dispersan (y sueltan a la picada) y no se mueven hasta que se les pasa |
| Tanque | cápsula del casco | se para, humo gris, el motor tose cada 0,9 s, la antena da vueltas (720°/s); al pasársele, «boing» y 0,7 s sin disparar |
| Quad y gusano | — | no se marean |

- **Red**: servidor escucha; los que andan replican `FTNBeachMoverRep` (suelo bajo el cuerpo a 1 cm, giro de 16 bits, estado, hora del
  estado y punto de interés) a 8–12 Hz y solo lo que cambia; los clientes interpolan. El paso de quads, la zona de gaviotas, la
  tormenta y el gusano replican solo horas y sentidos. Relevancia 200–300 m, sin dormancy. Efectos por multicast no fiable.
- **Temblor de cámara** (`UTN_BeachCameraShake`: `Kick` y `Rumble`), textos emergentes (solo con la cámara a menos de 60 m) y
  rendimiento (sin pantalla no hay mallas; no se anima lejos: 300 m, 400–600 m quad y gaviotas).

### 36.3 Cangrejo gigante (`ATN_BeachGiantCrab`, `World/Beach/TN_BeachGiantCrab.*`)

Caparazón de 5 m de ancho (una cría de 18 cm) sobre ocho patas, ojos en pedúnculos y una pinza de ~6 m a la derecha. Cuatro
paletas (rojo con pinza amarilla, violinista azul, violeta, fantasma de arena). `SizeScale` 0,8–1,2 (las cifras «por el tamaño»
escalan con él). Huella 25 m, peso 3 (el que más), hasta 120 por ronda, en grupos de 2–3 (40 %), `SeaBias` 0,6, núcleo 25 %.

- **Cómo anda**: de lado, con las patas en dos grupos que se alternan (un ciclo cada 1,9 m); acelera a 7 m/s² y frena a 11, gira como
  mucho a 170°/s, rodea lo grande del reparto y, si en 1 s se ha movido menos de 60 cm queriendo andar, sale 0,9 s de lado.
- **Patrulla sin parar** a 3 m/s (300 cm/s): entre rocas, troncos, maderas, tablones, castillos pequeños, sacos o erizos antitanque
  (35 %), un óvalo de 23 m × 11–16 m (dos paradas por vuelta) o una ida y vuelta de 23 m. Paradas de 0,5–0,95 s con la pinza en alto.
- **Vista y oído**: ve de frente (±70°) a 22 m; oye alrededor a 10 m (13 m si la tortuga corre a más de 6 m/s; 6 m si va agachada,
  en bola, en panzazo o casi quieta, <0,6 m/s). Tiene que estar dentro de su correa (38 m o 1,4 veces la huella). Entonces se da la
  vuelta (chasquido) y la persigue de lado a **5,6 m/s**, más que una tortuga esprintando (4 m/s): solo se le escapa saliendo de su correa o
  de su vista, o con el turbo del coco (8 m/s). Si la pierde, vuelve a 4,2 m/s.
- **Mazazo** (alcance recortado a petición del usuario): con la tortuga a su alcance (la pinza llega a 7,1 m del centro) más 1 m
  (`AttackSlack`), o sea ~8,1 m, se para, levanta la pinza y tiembla **0,6 s** (`WindUpTime`). Durante el aviso solo puede recolocarse
  a 4 m/s, así que la **sombra** se apunta como mucho a ~9 m del cuerpo; aparece donde estará la tortuga (0,2 s de adelanto) y crece
  hasta **1,7 m de radio** (`HitRadius`). Cae en **0,14 s** y solo cuenta si la punta del dedo toca de verdad (si queda a más de 1,5 m
  de la sombra, cuenta donde ha caído). Quien esté dentro (1,7 m + 0,4) y a menos de **2,8 m de altura** (`SlamHeight`: diferencia entre el
  centro de la tortuga y el suelo del golpe; el diseño dice que saltando por encima de la pinza se libra, pero un salto llano solo levanta
  el centro de la cápsula a ~2,1 m [calc]: no confirmado en juego, §42) queda despachurrada en bola aturdida **3,5 s** con un empujoncito.
  Luego 1,1 s con la pinza
  clavada, 1,4 s sin poder repetir y a la golpeada la ignora **6 s**. Temblor fuerte a menos de 15 m.
- **Embestida** (a media distancia): persiguiendo, con la tortuga a **8,5–13 m** y el camino libre de lo grande, un **55 % por
  segundo**. Se agacha 0,55 s clavando las patas, se pone de lado y sale disparado en línea recta hacia donde estará, acelerando a
  32 m/s² hasta **11,5 m/s** (más que la tortuga esprintando), como mucho 1,4 s. A quien arrolla (a más de 4,5 m/s, su caja con las
  patas + 45 cm) la derriba lanzada. Luego derrapa 0,8 s con surcos en la arena. Si se estampa contra algo grande u otro enemigo,
  «¡CATAPLÁN!» y mareado 1,6 s. No vuelve a embestir en 5 s. Con algo grande en medio (una roca) no embiste.
- Lo que se le lanza o la concha trampa lo marea; la tinta lo ciega (vuelve a su recorrido).
- **Premio**: 25 puntos en casa de cangrejos y erizos (5) del botín.

### 36.4 Erizo de mar (`ATN_BeachSeaUrchin`, `World/Beach/TN_BeachSeaUrchin.*`)

Bola violeta, negra, roja u oliva de 72 púas: rueda sobre un radio de 1,26 m (3 m de diámetro). `SizeScale` 0,75–1,35. Huella 12 m,
núcleo 40 %, peso 1, hasta 70 por ronda, a partir del 8 % del recorrido y más hacia el mar.

- Nota las vibraciones a **22 m** y rueda girando hacia la tortuga más cercana a **2,1 m/s** (lento: solo pilla a quien se despista),
  sin salirse de 16 m de su sitio (o 1,35 veces la huella). Sin nadie, pasea casi sin parar (respiros de 0,6–1,6 s) a 1,2 m/s por el
  80 % de su huella, rodeando lo grande.
- Tocarlo (~1,65 m del centro) pincha en cualquier estado salvo mareado: derribo con ragdoll 2,4 s (tabla de §36.2); el erizo
  retrocede 0,8 s y la ignora 4,5 s. **Mareado por algo lanzado, se tambalea en el sitio sin rodar ni pinchar.**
- Se evita: correr (4 m/s; andando, a 2 m/s, casi no se le saca ventaja porque rueda a 2,1 m/s) o rodearlo; quedarse quieta delante es lo que le funciona.

### 36.5 Lagarto (`ATN_BeachLizard`, `World/Beach/TN_BeachLizard.*`)

Lagarto de 15 m (55 cm reales): iguana verde con cresta, turquesa de cabeza amarilla, ocelado con manchas azules o naranja de
collar. Toma el sol 4–8 s (flexiones cada ~7 s, cabeceos, lengua) y se va andando a 3,8 m/s a otro rincón de su zona (hasta el
60 % de su huella, nunca dentro de una roca): siempre se mueve. Huella 15 m, peso 0,8, hasta 50, hasta el 88 % del recorrido y
preferencia por los lados de la playa.

**Carácter** (`ETNBeachLizardTemper`, sale de la semilla y es igual en todas las máquinas sin replicar): **45 % huidizo, 30 %
generoso, 25 % mordedor**. Se distinguen a la vista.

| | Huidizo (45 %) | Generoso (30 %) | Mordedor (25 %) |
|---|---|---|---|
| Señales | el de siempre | motas doradas a lo largo del lomo y collar dorado, destellos | cresta roja de púas y punta de la cola roja |
| A 30 m | se pone alerta y mira | ídem | ídem |
| A 17 m | el 70 %: **susto** (amago de 0,35 s, se hincha, bufa; a quien esté a menos de 8 m de la cabeza la empuja 6,5 m/s sin aturdir); luego huye | huye sin asustar | no huye: con una tortuga **de pie** a menos de 15 m se lanza (13 m/s, 1,3 s como mucho) |
| A 9 m | huye directamente | ídem | — |
| Ataque | — | — | si la punta del hocico llega a 2,6 m, la muerde por el caparazón («¡ÑAM!»): queda en su boca pataleando, la zarandea 1,3 s (±38°, 3,2 veces por segundo) y la lanza de lado en bola mareada 1,5 s |
| Premio | — | la primera vez que huye deja donde estaba «¡UN REGALO!»: el 60 % un objeto (pesos de la carrera), si no una concha de 25 (de 50, el 30 %) | — |
| Después | vuelve a su sitio | ídem | a la mordida la deja 10 s; si falla, bufa y a esa la deja 4 s |

- **Huida**: a 15 m/s a la roca, grupo de rocas, tronco, madera, tablones, restos de vela, castillo pequeño, sacos terreros, caja de
  munición o red de camuflaje más cercano (hasta 45 m, nunca hacia la tortuga) y **se mete debajo**; si no hay, da un arreón y se
  entierra (1,3 s). Escondido 7–12 s; no sale con una tortuga a menos de 18 m.
- **En bola, en brazos de otra o ya en el suelo no se la puede morder**: el mordedor la vigila.
- Mareado por algo lanzado: tumbado de lado; si mordía, suelta a la tortuga (1 s de mareo).
- Consola: `TN.Beach.Lizard <huidizo|generoso|mordedor>` (22 m delante, mirándote).

### 36.6 Paso de quads (`ATN_BeachQuadLane`, `World/Beach/TN_BeachQuadLane.*`)

- **Franja**: eje X local (el generador la gira 90° y la cruza de lado a lado), `Extent` de largo (0 = 280 m). En la arena, **dos
  rodadas** avisan por dónde pasa (oscurecen la arena; nada se pone encima salvo las franjas de caída de las fortalezas, que sí
  cruzan). En la selva, palmeras que revienta al salir y entrar.
- **Quad a escala con piloto**: 56 m de largo, ruedas de **16,8 m de alto y 6,7 m de ancho**, centros a ±8,7 m (las ruedas llegan a ±12 m:
  la huella), **hueco de 10,6 m entre ruedas** y 7,3 m de altura libre bajo el chasis. `SizeScale` 0,7–1,4.
- **Primera pasada a los 5–14 s**; después, cada 12–20 s. **Aviso de 3,5 s**: temblor creciente (0,12 → 0,57) a menos de 15 m del paso
  (se nota hasta 90 m), motor que se acerca, humo y hojas entre las palmeras del lado de salida. Cruza a **42 m/s** (unos 9 s de
  palmera a palmera; sale de 15 m dentro de la selva).
- **Atropello**: una rueda que pasa por encima (±3,8 m a lo ancho, ±5 m a lo largo) derriba con ragdoll 3 s y lanza a 9,5 m/s en su
  sentido, 3,8 de lado y 7,5 arriba con vueltas de campana; 1,2 s sin repetir con la misma. Temblor 0,85 a menos de 25 m del quad.
  Salvación: estar fuera del paso, **en el hueco entre las ruedas** (entre las dos rodadas se sobrevive) o saltar/estar muy alto.
- **No se marea** (`AcceptsHitStun` = false). Peso especial: 1–2 pasos por ronda en Normal (una más en Difícil, una menos en Fácil;
  mínimo 1), entre el 15 y el 92 %, a 113 m como poco entre ellos.
- **Ruedas cerradas** (ronda 4; `TNBeachMeshes::BuildQuadWheel`, `TN_BeachEnemyMeshes.h`): la cara interior de las ruedas (la que se ve
  desde el hueco entre ruedas, y también la exterior) se veía rota desde fuera. La banda de rodadura y el flanco de cada lado (un cono de R
  a 0,9 R) se generaban sin tapas y entre el borde interior del flanco (0,9 R) y la llanta (0,62 R) no había ninguna cara: con el material
  de una cara del color de vértice (las caras de atrás no se pintan) se veía a través de la rueda y el interior del neumático sin pintar;
  los tacos, vigas cuadradas sin tapas, estaban abiertos en la punta. Arreglo en la malla, sin poner el material a dos caras: un hombro
  plano por lado (corona de 0,5 R a 0,9 R con los mismos 18 lados que el cono, así que no hay grietas) y una tapa en la punta de cada taco.
  Sin cambios de juego. El tanque de juguete usa otra malla (`BuildTankWheel`) que ya estaba cerrada.
- Consola: `TN.Beach.Quad.Now`, `TN.Beach.Place QuadLane`.

### 36.7 Gaviotas y pelícanos (`ATN_BeachGullZone`, `World/Beach/TN_BeachGullZone.*`)

Una **zona** (huella nominal de 30 m, `SizeScale` 0,8–1,3, cada una con un círculo de tamaño distinto) que va **por encima**: no ocupa
suelo. **3–4 zonas por ronda** en Normal (2–3 en Fácil, 4–6 en Difícil), una por tramo del 10 al 97 %, en lados alternos y separadas
`GullZoneSpacing` (100 m hasta tres y pico zonas). Cada zona lleva **3–4 gaviotas** (25 m de envergadura) y, el **60 %** de las veces,
un **pelícano** (40 m), **cada una en su círculo y a su altura**: óvalo con el centro desplazado del de la zona (12–60 % del
radio, 35 m o más, derivando ±7 m), radio del 45–90 % (18 m como poco), achatado 0,65–1 y girado al azar, a **9–13 m/s** (pelícano
7–9); el 30 % gira al revés. Alturas en capas de 8 m (32, 40, 48, 56 y 64 m, ±2 m), nunca dos a la misma. Graznan de vez en cuando.
Las **sombras** son la de verdad, bajo el cuerpo: cuanto más bajo va, más pequeña, más nítida y más oscura (opacidad de 0,12 a 0,5).

Todas las cifras del ataque están en `World/Beach/TN_BeachGullTuning.h` (lógica pura, usada por la zona y por la gaviota justiciera y
probada por `Tortunabo.Beach.Gull.*`). La ronda 4 las **rebajó** porque casi no se podían esquivar; entre paréntesis, lo de antes.

- **Cuándo ataca**: cada **4–7 s** (3–6) a una tortuga al azar de las que están a menos del **80 % de la huella** de la zona desde su
  centro (19–31 m; antes huella + 8 m, 32–47 m), atacable y sin la protección de sombrilla. Un techo por encima no impide que la elija: hace
  que el ataque falle. Va el pájaro más cercano. La mitad de las veces **caga** una gaviota; si no, **picado** (el pelícano solo pica).
  **Junto al frente de la tormenta** (detrás o a menos de 25 m por delante, `StormNoCarryReach`) la zona caga en vez de picar y, si pica,
  falla (`IsNearStormFront`): el vuelo de 15 m hacia la salida metía a la tortuga en la tormenta y empezaba la cadena de patada,
  recolocación y red de seguridad.
- **El blanco te sigue, menos que antes** (#636, `TNBeachGullTuning::GullChaseSpeed`). El punto de la arena al que van el picado y la
  cagada (`FTNBeachGullAttack::Aim`) va hacia la tortuga a **2,5 m/s** como mucho (antes 6,25 y luego 4,2): más que andando (2 m/s) y
  menos que corriendo (4 m/s). Los **últimos 1,5 s** ya va lanzado por la línea que llevaba la tortuga y hacia los lados apenas corrige
  (0,75 m/s). Andando no se despega; **esprintando en línea recta se le gana 1,5 m/s** y al golpe queda a 3,8 m (picado), 4,3 m (cagada)
  o 6,2 m (justiciera), más de lo que alcanza el pájaro más grande. El servidor lo mueve y lo replica (10 Hz); cada cliente lo suaviza. Aviso duro en la arena: un disco negro de borde neto que nace pequeño y crece a
  medida que baja, hasta lo que coge o la mancha.
- **Cagada**: 1,5 s volando hasta encima; la suelta desde 30 m y cae acelerando en **2,1 s**: un pegote de 1,6 m con estela de gotitas y
  silbido; sobre la tortuga a la que va, un **signo de exclamación** amarillo de 105 cm sobre la cabeza que empieza 0,5 s antes de soltar y
  parpadea de 2 a 12 veces por segundo. Al caer, la traza desde arriba da en el techo si lo hay (**a cubierto, la mancha cae encima**).
  Quien esté dentro (**2 m por el tamaño + 0,25 m**; antes 2,8 m + 0,45) y a menos de 3 m de altura, y **no vaya en plancha en ese
  momento** (en el aire o arrastrándose a 2,5 m/s o más: `TNBeach::IsDodgingByBellyDive`), cae derribada 2,4 s con la cagada **pintada en el
  caparazón** (decal sobre el hueso `Spine2`, 12 s: entera hasta los 8 s y se seca hasta desaparecer); «¡PLOF!» y mancha en la arena 12 s.
  La zona te deja 6 s.
- **Picado**: 1 s colocándose (a 18 m del blanco y 46 m de altura), baja en picado **2,3 s** siguiendo a la tortuga por el aire; a 0,45 s
  de llegar abre pico y alas y adelanta las patas; a los 3,3 s **coge** a la tortuga que esté bajo el pico (**2,2 m por el tamaño + 0,25 m**;
  antes 3 m + 0,45) si está **de pie**: no en pleno panzazo, ni en bola, ni en brazos de otra, ni a cubierto (si la cubre algo, falla y pica
  encima).
- **Si falla**: el picado se ve entero igual: baja en 0,14 s hasta clavar el pico donde iba (o en el techo), pica dos veces hasta los
  0,55 s («¡PIC!», arena, temblor pequeño) y remonta.
- **Agarre**: el pico se cierra en la espalda del caparazón («¡ÑAC!»); la tortuga cuelga pataleando (sin meterse en bola): 0,35 s de tirón,
  sube 26 m hasta los 2,2 s, vuela meciéndola y a los **3,3 s** la suelta abriendo el pico, 15 m más hacia la salida: **cae en bola
  aturdida** (empujada 3,5 m/s hacia la salida), ~2,3 s de caída + 2 s. Si a la que la lleva le dan con algo, la suelta. La zona te deja
  12 s. **Al soltarte, siempre caes al suelo** (seguro de la sujeción: correcciones devueltas y `MOVE_Falling`; una sujeción de más de 6 s
  se suelta sola).
- **Segundo agarre + caparazón = «torbellino»** (arreglado en la ronda 4). La primera vez que una gaviota cogía todo iba bien; con otra
  seguida, si la tortuga se metía en el caparazón, la física se rompía en un bucle de bola y suelo. **Causa raíz**: `TNBeach::RelocateTurtle`
  (red de seguridad, patada de la tormenta, rescates) pedía `MOVE_Falling` a una tortuga que ya caía; el personaje ignora pedir el mismo
  modo y conservaba la altura de la caída anterior (`FallApexZ`), así que en su paso siguiente `TickFallRules` veía «5 m de caída» y la
  metía sola en bola donde la acababan de dejar de pie (bola, hundida, red de seguridad, bola). A la segunda o tercera gaviota se notaba
  porque cada agarre se lleva a la tortuga 15 m hacia la salida y la suelta empujándola hacia atrás, con la tormenta avanzando: la dejaba en
  el frente. **Arreglos** (`TN_BeachStun`, `TN_BeachEnemy`, `TN_BeachGullZone`, `TN_ShellComponent`): `RelocateTurtle` pasa por `MOVE_None`
  antes de `MOVE_Falling`; quien se mete en el caparazón colgando se escurre (`SlipFromHolder` → `OnHeldTurtleSlips`) y el enemigo la suelta
  antes de que nazca la bola; ningún enemigo sujeta a una tortuga en bola, en ragdoll o en brazos (`CanHoldTurtle`) y nada aturde ni derriba
  a una sujeta (`CanStunOver`, `ResolveMover`); la caja nace en un sitio libre (`FindFreeBodySpot`) y al salir de la bola la cápsula y el
  suavizado vuelven a los de serie; en un cliente, una tortuga soltada en su caparazón espera 0,6 s a su caja (`BallArrivalGrace`) en vez de
  caer por su cuenta. Pruebas: `Tortunabo.Beach.Hold.*`; comando `TN.Beach.Gull.Grab`. **Sin tocar**: `TickFallRules` no consulta al
  árbitro (si de verdad cae 5 m durante la reserva de la red de seguridad, hace bola) y otros teletransportes que no pasan por
  `RelocateTurtle` podrían conservar una altura vieja.
- **Se evita**: tirarse en **plancha en el momento justo** (libra de la cagada y, como siempre, del picado), meterse en el caparazón (el
  picado no coge a quien va en bola), refugiarse bajo techo (sombrilla clavada, castillos con salas), lanzarle una piedra al que baja (lo
  marea y suelta a la tortuga) o echar a correr. **Corriendo** (#636, recalculado con las velocidades reales comprobadas en
  `BP_TortugaCharacter`: 2 m/s andando y 4 m/s corriendo): esprintando en línea recta se libra de todo; andando, aunque gire al lanzarse,
  no. Esprintando y dándose la vuelta al lanzarse se cruza la sombra (queda a 2,8 m en el picado, 2,45 en la cagada y 1,1 en la justiciera).
  **Ventana de la plancha** (`TNBeachGullTuning::BellyDiveDodgeWindow`): libra mientras va por el aire (0,3-0,4 s) y mientras se arrastra
  a 2,5 m/s o más: 0,59-0,69 s desde que despega corriendo y 0,48-0,58 s andando; la cagada tiene que caer dentro. Pruebas:
  `Tortunabo.Beach.Gull.*` (`PoopWalkSprintDive` simula andar, esprintar y la plancha frente a la caca). **No confirmado en juego**.
- Consola: `TN.Beach.Gull.Attack 1|2` (1 = cagada, 2 = picado; sin número, al azar), `TN.Beach.Gull.Grab [veces=2] [jugador]`,
  `TN.Beach.Place GullZone`.

### 36.8 Cangrejo ermitaño bola (`ATN_BeachHermitCrab`, `World/Beach/TN_BeachHermitCrab.*`)

Caracola de turbante de 2,24 m de diámetro (4 cm reales) con el ermitaño asomando (cabeza, ojos en pedúnculos, antenas, pinza grande
—la izquierda— y pequeña, cuatro patas). Cuatro paletas, `SizeScale` 0,8–1,25.

- **Calle**: el eje X local, centrada en el actor, con `Extent` de largo (el reparto: 25–45 m; 0 = 40 m), **cuesta abajo** de su extremo
  −X (donde espera) al +X (`LaneRollsDownhill`: 1,2 m o el 2,5 % del largo de desnivel), a ±25° de la bajada de la arena y sin cruzar
  cornisas; su 60 % central queda libre. Si el extremo +X quedara 60 cm o más por encima, rueda al revés.
- **Espera** asomado, mirando calle abajo (ojos que miran alrededor, la pinza saluda cada 3,5 s). **Salta** con una tortuga atacable
  en la calle: entre 2,5 m por delante de él y 2 m antes del final, a menos de 5,2 m del eje (por el tamaño) y 9 m en altura;
  lo mira cada 0,1 s. Tras volver arriba, 1,2 s sin poder rodar.
- **Se mete** en la concha (0,55 s, «¡plop!») y **rueda**: arranca a 2,5 m/s y acelera 3,8 m/s² más 9 m/s² por la pendiente (entre 3
  y **15 m/s**). Bota con el relieve (gravedad 12,5 m/s², rebote 32 %) y da botes sueltos; culebrea ±1,2 m en ondas de 15 m; en los
  últimos 7 m frena y se para al final.
- **Derriba** a quien toca la bola (1,12 m + 0,7 m del tramo que recorre en ese fotograma, y a menos de 2,2 m en altura), **sin
  pararse**: a todas las que estén en fila. Tabla de §36.2 («¡BOLO!»/«¡STRIKE!»).
- **Es sólido** cuando no rueda (esperando, metiéndose, asomando, andando o mareado): la tortuga choca con él. Rodando no
  bloquea: derriba.
- **Al final** asoma (0,35 s), se sacude la arena, se da la vuelta y **vuelve andando** a lo alto a 2,3 m/s (por la raíz del tamaño).
- **Red**: los estados son función de su hora (reloj del servidor); la rodada es un camino calculado igual en cada máquina (pasos de
  1/60 s, 14 s como mucho). Los derribos, por el servidor.
- **Se evita**: apartarse 3 m de lado (pasa de largo); no ponerse en fila en su calle; lanzarle algo con la bola rodando (se para en
  seco).

### 36.9 Pulpo de poza (`ATN_BeachPoolOctopus`, `World/Beach/TN_BeachPoolOctopus.*`)

Pulpo de 1,5 m de cuerpo (5,5 cm reales) con ojos saltones, manchas (o anillos azules), sifón y **ocho brazos de 3,7 m de seis
tramos** (48 instancias). `SizeScale` 0,8–1,3. **1–2 por poza** según su tamaño, **dentro del agua** (a menos del 55 % del radio de
su orilla) y separados. Sin poza (otro mapa o en la arena) hace su propio charco translúcido de 7 m de radio.

- **Acecha** bajo el agua, tumbado con los brazos abiertos (el anillo de los brazos 25 cm sobre el fondo, entre 0,4 y 1,5 m bajo el
  agua); se ve su silueta oscura a ras del agua y burbujas cada 1–2,5 s (suenan mucho menos: solo los 2 más cercanos, a menos de 24
  m, una cada 3,5–7 s por pulpo). Pasea a 0,8 m/s por el 35 % de la poza alrededor de su sitio.
- **Nadadora**: atacable, dentro de la orilla y nadando (o con el centro a menos de 40 cm sobre el agua); la busca cada 0,15 s. Se fija
  en ella **0,5 s** (se gira, burbujas y dos puntas de brazo que asoman como aletas) y va a por ella bajo el agua, estirado, a **5,2 m/s**
  (por la raíz del tamaño), sin salirse del 85 % de la poza. **Se rinde a los 7 s o si ella sale.**
- **Agarre** a 3 m: saca la cabeza, tres brazos la envuelven y la suben en 0,35 s a **2,3 m** sobre el agua, meciéndola (pataleando).
- **Lanzamiento** a los **0,8 s**: hacia la salida (contra el mar), hasta la orilla de ese lado + 6 m (**entre 9 y 34 m**), tiro parabólico a 42°:
  `StunTurtle` mareada el vuelo + 1,2 s; tinta (nube y chorro). A los 0,6 s se hunde y vuelve a su sitio a 3,2 m/s (3 s como mucho).
  Te ignora 5 s tras lanzarte.
- **Se evita**: no nadar (las pozas se rodean o se cruzan de un salto), salir nadando antes de que llegue; si te agarra, lanzarle
  una piedra (te suelta al agua). Nada lo lanza contra la tormenta cerca del frente.
- **Ojo**: 4 de las 7 pozas cortan un corredor: cruzarlas nadando es un atajo con pulpo.

### 36.10 Enjambre de pulgas de arena (`ATN_BeachSandFleas`, `World/Beach/TN_BeachSandFleas.*`)

**48 pulgas** (anfípodos de ~18 cm) en una sola malla instanciada, sin colisión, dos tonos; `SizeScale` 0,8–1,25. Cada una salta por su
cuenta (0,28–0,48 s, 0,4–1,1 m de alto) dentro de una nube de 1,7 m de radio; una mancha oscura de 2,1 m en la arena y polvo la
delatan de lejos. Va en **claros de arena abierta** (nada en el 75 % central de su huella de ~9 m ni pozas ni trincheras).

- **Pasea** a 0,7 m/s; **ve** a **18 m** (por la raíz del tamaño) una tortuga atacable dentro de su correa (24 m o 1,8 huellas) y va hacia
  ella a **1,6 m/s** (se le escapa andando).
- **Picada** con la tortuga a 1,7 m + 1 m (en planta) y a menos de 4 m en altura: se le suben encima (el 75 % sobre el caparazón). **2 s**
  de saltitos sin control (siete impulsos de 3,2–4,2 m/s arriba y 1,4–2,6 m/s de lado al azar que sustituyen su velocidad), «¡PICA, PICA!»
  y a los 0,9 s «¡QUÉ PICOR!»; al final, bola mareada 1 s. Se corta antes si la derriban, la aturden o la sujetan, o si marean al
  enjambre. Te ignoran 6 s.
- **Se dispersan** (hasta 6,5 m en 1,2 s) y vuelven a juntarse hasta los 3,2 s si les lanzas algo.
- **Red**: el centro del enjambre a 8 Hz; los saltitos son deterministas (hora y semilla) y los da el dueño a su copia a la misma
  hora, sin RPC. Se evita andando.

### 36.11 Tanque de juguete teledirigido (`ATN_BeachToyTank`, `World/Beach/TN_BeachToyTank.*`)

Tanque de plástico de **4,5 m de largo**, 2,5 m de ancho y 2 m de alto (16 cm reales): orugas de goma con eslabones, cinco ruedas por
lado, faros, escarapelas de Tortunavy, cañón de 2,7 m con la punta naranja y antena de látigo de 3,2 m con la banderita. Cuatro
paletas (oliva, arena, gris de Tortunavy y camuflaje). `SizeScale` 0,8–1,2. **Caja sólida** tipo Pawn del tamaño del casco en todas las
máquinas (la única colisión de un enemigo, con el cangrejo).

- **Patrulla** su tramo de 20–40 m (0 = 24 m), de través, a **2,6 m/s** (por la raíz del tamaño), y en cada punta gira sobre sí mismo a
  150°/s con las orugas a contramano.
- **Ve** a **25 m** una tortuga atacable (a menos de 8 m en altura): se para, gira la torreta a 110°/s y, con ella a menos de 7°,
  dispara a los **0,7 s** de verla; luego cada **1,6 s**. La deja a 29 m o cuando deja de ser atacable y busca otra; sin nadie 1,2 s,
  vuelve a patrullar.
- **Bolita de espuma** (53 cm, naranja con franja amarilla): sale a **19 m/s** con el tiro bajo hacia donde estará la tortuga (el 60 %
  del adelanto), cae con 7 m/s² y deja una estela de humo; bota hasta 3 veces o 2,4 s. Da a quien pase a menos de 0,27 m + 0,7 m de su
  recorrido: bola mareada **0,8 s**, empujada a 5,2 m/s y 2,6 arriba («¡PAF!»).
- **Se evita**: correr de lado esquivando la bolita; ponerse detrás de algo; lanzarle algo (se para, echa humo, la antena da vueltas
  y no dispara 0,7 s más). Es sólido: se choca con él.
- **Dónde**: junto a lo militar (redes, erizos, sacos) y las trincheras (15–35 m), en tramos de través; los guardias de las fortalezas
  también.

### 36.12 Tormenta de bañistas (`ATN_BeachStorm`, `World/Beach/TN_BeachStorm.*`)

No es un elemento del reparto: la crea el GameMode al dar la salida **30 m detrás de la línea** (`StormSpawnBehind`), en el centro de la
playa y mirando al mar (llama por nombre a `StartStorm()` y `StopStorm()`). Un frente ancho (`HalfWidth` 220 m) que avanza por la
playa **a ras de arena** (la cota sale de `GetGroundHeightAt`, sin trazas).

- **Marcha** (con 800 m; entre paréntesis lo de 1200 m): **10 s de gracia** (`DefaultGrace`; 15), luego de 0 a **1,8 m/s** en 6 s
  (`StartAccel` 30 cm/s²) y 1,8 m/s (`DefaultSpeed`: la tortuga anda a 2 m/s y esprinta a 4; la media de la carrera es de ~2,8 m/s [calc], no los ~4 m/s que suponía el diseño). Solo acelera al
  final: pasados **160 s** de marcha (`LateStartSeconds`; 240), **+0,45 m/s por minuto** hasta **3 m/s** (`SpeedRampPerMinute`,
  `MaxSpeed`); con la primera tortuga pasado el 80 % del recorrido, al menos **2,5 m/s** (`EndRushProgress`, `EndRushSpeed`); y si
  la última le saca más de **120 m** (180), a **2,8 m/s** hasta quedarse a 80 m (120) (`CatchUpGap`, `CatchUpSpeed`, `CatchUpRelease`).
  La ronda dura unos 200 s en el diseño y ~290 s con las velocidades reales [calc]; con ellas, la tormenta (1,8 m/s) va al 90 % de la
  velocidad de andar y, ya acelerada (hasta 3 m/s), alcanza a quien anda; a quien esprinta (4 m/s), no. Cada cambio empieza un tramo
  replicado (desplazamiento, velocidad, aceleración, hora).
- **Aviso**: con el frente a menos de 25 m por detrás (`WarnDistance`), temblor creciente, viento, arena alrededor de la cámara y «¡QUE
  VIENE LA TORMENTA!» (una vez por acercamiento); al entrar, «¡CORRE!». Dentro (6 m por detrás del frente, `InsideMargin`): niebla y tinte
  de arena, viñeta, tos y viento (el del cooperativo, no uno propio).
- **Patada** (nadie se puede quedar detrás del frente): a la tortuga que lleva 0,25 s (`KickDelay`) más de 1 m (`KickSlack`) por detrás
  del frente y que se mueve sola (de pie o en su bola porque quiere), un bañista le da una patada **a un sitio de arena abierta
  resuelto antes** en el servidor, `KickAhead` = **20 m** por delante del frente (contando lo que avanza mientras vuela): en **bola por
  el aire** si el arco está libre (vuelo de 1,1–2,4 s, o 2,6 o 3,2 s más alto, para salvar lo de delante; mareada lo que vuela + 0,8 s)
  o de un **salto de teletransporte con polvo** si no lo está (o si el sitio está a más de 45 m). Si la bola no llega (atascada,
  hundida, en el agua, lejos o detrás del frente), se la pone en su sitio. Después **3 s sin patadas** (`KickGraceSeconds`). A la que
  mueve otra cosa (pico, boca, gusano, brazos de otra, derribo, bola de aturdida, lanzamiento, red de seguridad, concha) no la patea:
  espera a que la suelten (8 s como mucho con lo que acaba solo). Se ve la pierna del bañista (0,25 s), pisotón, golpe, polvo,
  temblor y «¡PATADA!». Nada la lanza hacia atrás cerca del frente.
- **Aspecto** («nada aparece ni desaparece de golpe»): velo de arena de 55 m; **24 trastos** que nacen en el polvo del borde y se quedan
  por el borde (sombrillas de 50 m, cubos, sillas, toallas, flotadores, palas, chanclas y pelotas; viven 3,5–7 s, nacen fundiéndose
  en 0,45 s y se van en 0,6 s) y **8 bañistas** (caderas y dos piernas) pisando en el borde del polvo. Sin pisotón por paso ni «pum,
  pum, pum»: su ruido es el paisaje sonoro del cooperativo.
- **Cómo se evita**: no quedarse detrás del frente: la tortuga anda a 2 m/s y esprinta a 4, y la tormenta va a 1,8 m/s (a 3 m/s como mucho,
  pasados 160 s de marcha): quien anda sin parar apenas le saca ventaja. La tormenta acelera al final
  (a partir de los 160 s) y, si todas le sacan más de 120 m, sube a 2,8 m/s hasta acercarse a 80 m, para que siempre se note.
- Consola: `TN.Beach.Storm.Start [metros=30] [cm/s=180]`, `Stop`, `Info`, `Here [jugador] [metros=4]`.

### 36.13 Gusano de arena gigante (`ATN_BeachSandWorm`, `World/Beach/TN_BeachSandWorm.*`)

Lo crea el GameMode (no es un elemento): **solo cuando la cuenta de 10 s tras la primera llegada llega a 0** (no con «¡TODAS AL AGUA!», ni
en el límite de la ronda ni en el sprint), sale de la arena bajo **cada tortuga que aún corría** y se la come. Es un remate cómico: nadie
muere; la tortuga queda dentro, oculta, hasta la ronda siguiente, que la recrea en la salida.

| Tiempo | Qué pasa |
|---|---|
| 0–0,7 s | Aviso: bajo la tortuga la arena se hunde en un remolino (hasta 4,3 m de radio), con polvo y retumbar; temblor de cámara entero a 15 m y hasta 60 m. La tortuga tiembla y se hunde 35 cm |
| 0,7 | Revienta la arena: nube, terrones y piedras; golpe de temblor fuerte (hasta 90 m) |
| 0,7–1,3 | Sale en vertical con los cuatro labios abriéndose como una flor (dientes a la vista) y la tortuga dentro, pataleando; sube 25 m |
| 1,2–1,55 | Bocado: los labios se cierran en cúpula y desaparece: «¡ÑAM!» grande y golpe de temblor |
| 1,55–2,25 | Traga: mastica, se dobla en arco y un bulto baja por el cuerpo, con dos «glup» |
| 2,35 | Eructa una nube de arena |
| 2,6–3,0 | Se hunde por su agujero |
| 3,0–3,2 | El cráter (5,2 m de radio) se cierra |

- Boca de 6 m de diámetro (la tortuga mide 1,4 m) con tres coronas de dientes (16, 12 y 9), cuatro labios-pétalo y 13 anillos de cuerpo
  de 5,4 m de grosor; tres paletas (arena tostada, rosa de lombriz, gris de duna). Sin esqueleto: los anillos se colocan cada
  fotograma a lo largo de una columna con ondulación.
- **Varias rezagadas**: cada una tiene su gusano; cada uno sale 0,11 s después del anterior (0,3 s como mucho). `EatSeconds` = **3,2 s**;
  el recuento espera esos 3,2 s + 0,6 s de margen (`SandWormMarginSeconds`).
- **La tortuga comida**: pierde aturdimiento, bola, derribo y carga; se le para el movimiento y se le quita la colisión; queda marcada
  como sujeta (ningún enemigo le da); en el bocado se oculta y su cámara funde en 0,6 s a un lado del gusano. `TNBeach::StunTurtle` y
  `KnockDownTurtle` no hacen nada con una tortuga en su boca.
- **Se evita**: llegar al agua antes de que acabe la cuenta de 10 s.
- **Red**: un actor replicado y siempre relevante con la tortuga, la hora de inicio, el desfase, el suelo, el sentido del arco y la
  semilla; sin RPC. Sonido sintetizado (`TN_BeachSandWormSynth.*`).
- Consola: `TN.Beach.Worm [jugador]` (se come ya a esa tortuga en cualquier fase).

### 36.14 Amenazas que lanzan las tortugas (objetos de carrera)

Cuando alguien usa un objeto de carrera (`ATN_RaceItemActor`, siempre relevantes, `Track` a 20–30 Hz; catálogo, pesos y red en el §29),
aparecen amenazas que se comportan como enemigos temporales. Todas respetan la invulnerabilidad del protector solar y
mueven la tortuga con `StunTurtle`/`KnockDownTurtle`:

| Amenaza | Qué hace | Cifras | Se evita |
|---|---|---|---|
| Cangrejo teledirigido (`ATN_RaceHomingCrab`) | cangrejito rojo de 1,1 m que persigue a la tortuga más cercana por delante y la derriba; sin tortuga, va a por el enemigo más cercano por delante (<80 m) y lo marea | 900 → 1600 cm/s, gira 420°/s, vive 12 s, derriba 2,2 s, marea 4 s; máx. 10 | protector solar (rebota sin efecto) o ponerse a cubierto; corriendo no basta (va a 9–16 m/s) |
| Gaviota justiciera (`ATN_RaceGullStrike`) | vuela sobre **la tortuga que va la primera** (si va por delante de quien la lanza) y suelta una cagada con aviso de sombra | aviso 3,2 s de llegada + 1,7 s de caída; el blanco sigue a la víctima a 6 m/s (4,5 m/s al caer) y queda fijo los últimos 0,5 s; impacto de 2,4 m (antes 3,3); derriba 2,6 s; máx. 3 | plancha en el momento justo, cubrirse o turbo del coco (corriendo a 4 m/s no basta) |
| Mina de arena (`ATN_RaceMine`) | mina lanzada que rebota una vez, se arma a los 0,9 s y salta al acercarse una tortuga (la de quien la lanzó, tras 1,5 s) o un enemigo | mecha 0,35 s; aturde en bola 3 s a <5,5 m; marea 5 s a enemigos a <13 m; explota sola a los 10 s; máx. 12 | rodearla; se ve el piloto rojo |
| Nube de tormenta (`ATN_RaceStormCloud`) | nube negra sobre **cada otra tortuga en carrera** con 1,1 s de aviso y un rayo | aturde en bola 2,2 s; máx. 2 | protector solar (el rayo cae a su lado sin efecto) |
| Disco volador (`ATN_RaceFrisbee`) | arco de 26 m que vuelve a la mano de quien lo lanzó (2,9 s en total) | derriba 1,9 s (una vez por pasada), marea 4 s a enemigos; máx. 6 | esquivarlo de lado |
| Silbato del sargento | aturde a los **enemigos** en 55 m con pajaritos | 5 s | — (no afecta a tortugas) |
| Pelícano taxi (`ATN_RacePelicanTaxi`) | **no es una amenaza para quien lo usa**: te coge y te lleva 120 m por delante (22 m/s) | 1 s de aproximación, 1,3 s de subida, crucero, 1,5 s de descenso, suelta a 2,6 m sobre arena abierta; invulnerable en el vuelo; máx. 8 | — |

El protector solar (invulnerabilidad de 8 s) además derriba a las tortugas que toca (2 s) y marea a los enemigos 4 s.

### 36.15 Cooperativo: enemigos y peligros del mapa procedural

Aquí sí se muere (rescate y huevos: §22). Los peligros los coloca `SpawnHazards` con las reglas por bioma de §37.2; los actores son
Blueprints hijos de clases C++ (valores por defecto del C++, el Blueprint puede haberlos cambiado).

| Peligro | Clase y archivo | Cómo detecta y ataca | Cifras (C++) | Cómo se evita |
|---|---|---|---|---|
| Cangrejo | `ATN_CrabActor`, `ATN_CrabSpawnZone` (`World/TN_CrabActor.*`, `TN_CrabSpawnZone.*`) | patrulla entre puntos; persigue al jugador más cercano dentro de la esfera de detección; a distancia de ataque lo tumba | patrulla 250 cm/s, persecución 450; detección 800 cm; correa 2000; ataque a 120 cm: knockdown 2,5 s con impulso 600, enfriamiento 2 s; cuerpo de 80 cm de radio recibe golpes. La zona crea el cangrejo **cuando entra un jugador** (perezosa, una vez): 5 por defecto | salir de su correa (2000 cm) o esquivar su ruta; esprintando (400 cm/s reales) no se le saca ventaja, porque persigue a 450: solo con turbo; se aturde con la concha trampa (`ApplyStun`) y se ciega con la tinta (`ApplyBlind`) |
| Gaviota dinámica | `ATN_EnemySeagull`, `ATN_SeagullSpawnZone` (`World/TN_EnemySeagull.*`) | la zona elige cada 10 s (tras 3 s) a un jugador vivo dentro de su volumen (máx. 2 a la vez) y crea una gaviota encima; la gaviota lo sigue proyectando un círculo que se encoge | sigue a 350 cm/s a 400 cm de altura; cronómetro de **8 s**; círculo de 500 → 150 cm (`MinKillRadius`); picotazo físico en 0,15 s; se retira si el jugador está fuera del círculo **3 s** o hay techo entre ambos (se comprueba cada 0,25 s) | salir del círculo, ponerse bajo techo, sombrilla (`HasUmbrellaProtection`, 8 s) o cabezota (`BigHead`) que absorben el picotazo |
| Caca de gaviota | `ATN_SeagullDroppingActor`, `ATN_DroppingSpawnZone` | cae en línea recta desde 12 m a 600 cm/s (2 s) sobre un jugador; sombra que se encoge de 300 a 40 cm | hitbox de 75 cm (antes 100; `ImpactRadius`, editable), **mata**; no persigue | moverse, tirarse en plancha en el momento justo (`bBellyDiveDodges`) o sombrilla, que la bloquea |
| Medusa saltarina | `ATN_JellyfishActor` (`World/TN_JellyfishActor.*`) | no ataca: pisarla por arriba rebota | 1200 cm/s hacia arriba (sustituye la Z, sin acumular), 0,5 s entre rebotes, se aplasta al 60 % | — (es una ayuda: sube a atalayas) |
| Cáscara de plátano | `ATN_BananaPeel` (`World/TN_BananaPeel.*`) | trampa pasiva: al pisarla, derribo y deslizamiento | 2 s de derribo, deslizamiento con la velocidad que llevaba (mínimo 350 cm/s, 400 cm/s arriba); se destruye | saltarla o rodearla |
| Zona lenta / sirope | `ATN_SlowZoneVolume` (`World/TN_SlowZoneVolume.*`) | local en cada máquina: limita velocidad y salto | tope 300 cm/s, subida 120, caída 200, salto 150, gravedad ×0,35 (con >1 es arena movediza) | rodearla |
| Tormenta del camino | `ATN_PathStorm` | ver §32.10 | mata a los 5 s dentro | ir por delante |
| Depredador (tiburón/morena) | `ATN_ProcWaterPredator` (`World/ProcMap/TN_ProcWaterActors.*`) | patrulla su zona de agua y persigue a quien nade cerca; si alcanza, **mata** | patrulla 260 cm/s (radio 1500), detección 2000, persecución 700 (más que nadar: 625), correa 3200, mordisco a 170; aleta visible | no nadar fuera del camino |
| Remolino | `ATN_ProcWhirlpool` | atrae y hace girar a quien nada cerca; **en el ojo mata** | radio 1400, tracción 800, giro 700 cm/s², 2,5 s en el ojo | salir nadando |
| Corriente | `ATN_ProcWaterCurrent` | arrastra lejos del camino a quien nada dentro | 900 cm/s² | no soltarse del camino |
| Rebotador flotante | `ATN_ProcWaterBouncer` | pisarlo por arriba rebota; tocarlo nadando empuja y aturde un momento | rebote 1150, empujón 750; una variante por bioma (medusa, nenúfar gigante, boya, piedra pómez) | pisarlo desde arriba |
| Zonas de muerte | `ATN_ProcKillVolume`, `ATN_DeathZoneVolume` | fondo de zanjas, lava, cajas bajo los puentes colosales y bajo las brechas del adarve | muerte a los 3 s en `ATN_DeathZoneVolume`; inmediata en las cajas de muerte del mapa | saltar bien |
| Caídas | `ATortugaCharacter` | más de 5 m de caída libre → caparazón; más de 35 m → muerte; géiseres, toboganes y el agua no cuentan | `AutoShellFallHeight` 500, `FatalFallHeight` 3500 | — |

Distribución por bioma y dificultad: §37.2. La fauna ambiental (§32.9) **no** ataca.

### 36.16 Modo clásico (chunks, `LVL_Run`)

El modo clásico (`ATN_ChunkManager`, `World/TN_ChunkManager.*`, chunks `BP_Chunk_Easy_01…05`, `Medium_02…04`, `Hard`, `Hard_02/03` y
`Final`) usa los mismos actores del cooperativo con reglas de diseño de [`Docs/LDD_Tortunabo.md` (eliminado)](LDD_Tortunabo.md (eliminado)): 5 rondas de 6 módulos (M1–M4 de dificultad
creciente, M5 puzle y M6 victoria), con bañistas que avanzan por detrás (`ATN_StormVolume`: caja que crece en línea recta a 150 cm/s y mata a
los 5 s dentro). Elementos y su nivel: algas y rampas (Fácil); erizos estáticos con veneno (Fácil, señal: silueta espinosa); arenas movedizas,
alambre de espino, cangrejo dinámico que patrulla y persigue a ~300 cm y cangrejo enterrado (Medio); gaviota (estándar y de hostigamiento
continuo, Difícil), plataformas rompibles, cajas y placas de presión, quads bloqueadores, botones y varas rotatorias (Difícil). En código
existen: `ATN_CrabActor`, `ATN_EnemySeagull` (ambas con `TNCrabLogic`/`TNSeagullLogic` puras y probadas), `ATN_QuadActor` y `ATN_QuadSpawner`
(quad que cruza a 800 cm/s con dos ruedas cápsula de 90 cm de radio y 55 de semiancho a ±280 cm; **mata al tocar**; cada 20 s, primera a los
3 s), `ATN_BreakablePlatform` (aguanta 2 s con `PlayerThreshold` jugadores —1 individual, 2+ cooperativa—, tiembla 1 s, reaparece a los 6 s;
modos reversible o irrevocable), `ATN_SlowZoneVolume`, `ATN_DeathZoneVolume` (3 s), `ATN_ScriptedDeathZone`, `ATN_PressurePlate`,
`ATN_ButtonInteractable`, `ATN_ButtonGroupManager`, `ATN_CollectionZone`, `ATN_GoalZone` y `ATN_PhysicsObjectActor`. No hay clase C++ propia del erizo
venenoso ni del cangrejo enterrado (sin confirmar si son Blueprints).

## 37. Reparto por ronda

### 37.1 Carrera: `TNBeachLayout::GenerateRound(Seed, Difficulty, Out)` (`World/Beach/TN_BeachLayout.h`)

**Cuánto.** Unos **5000 elementos** por ronda con los 1200 m antiguos (antes de la ronda 3, ~1000) y **unos 3100 con los 800 m de ahora**
(media de 24 semillas en Normal: 3104; Fácil 3096; Difícil 2750), con el ≈80 % decorado (~2770 en Normal, de ellos ~2720 pequeño). Se
reparte en otro hilo (`UE::Tasks`; ~20 ms de media medidos fuera del motor, 60–70 ms en C++ con 1200 m), sin estáticos que cambien ni
UObjects. La primera ronda de cada proceso hace además las tablas fijas (crestas, pozas, trincheras, reglas, rejilla de la arena de
2 m —`SandCache`, ~102000 `SandZ` con `ParallelFor`—).

**Semilla y determinismo.**

| Aspecto | Regla |
|---|---|
| Semilla de la ronda | Al azar (reloj UTC ^ `FMath::Rand` ^ ronda × φ); con `open LVL_BeachRace?BeachSeed=N` (o `FixedSeed` en el GameMode) es `N + (ronda − 1)`: rondas distintas, pero la misma partida reproducible |
| Función pura | Misma semilla y misma dificultad → mismos elementos, huellas, variantes y puntos interesantes (prueba `Layout.Determinism`: más de 2300 elementos, iguales dos veces; otra dificultad → otro reparto) |
| Qué viaja | `FTNBeachRoundNet` en el generador: `Seed`, `Round`, `bCleared`, `bStartOpen`/`StartOpenTime`, `bSprintEggs` y `Difficulty`. El terreno fijo no viaja (semilla constante `TerrainSeed`) |
| Quién monta | El servidor y **cada cliente**, con la semilla recibida: reparto, asientos del suelo y decorado son locales y coinciden; los elementos con estado (trampas, ayudas, enemigos) los crea el servidor con `SpawnElement` y se replican |

**Las pasadas**, de lo grande y lo que tiene que verse a lo que rellena (cuotas con 800 m, entre paréntesis las de 1200 m; `LengthScale` = 2/3):

| # | Pasada | Cuota en Normal | Detalle |
|---|---|---|---|
| 1 | Castillos con salas | 1–3 (1,7 de media; dos o más en el 60 %) | el principal entre el 42 y el 58 %, ±39 m del centro, con dos alas en embudo; otro sin alas entre el 12 y el 36 % (60 %) y otro entre el 62 y el 90 % |
| 2 | Fortalezas colosales | 1 (a veces 2 con muchas ayudas) | solo caben entre 15 y 120 m y en 280–440 m; 400 intentos; hay una en todas las rondas medidas |
| 3 | Pasos de quads | 1–2 (2–3) | cruzan la playa entera, entre el 15 y el 92 %, a 113 m como poco entre ellos |
| 4 | Fortalezas grandes y medianas | 1,3 grandes y 2 medianas (2 y 3) | un tramo cada una; rodeo de 25 m, franja de caída de 16 m; guardias |
| 5 | Castillos enormes | 6–9 pedidos (9–14), salen ~4 | a ±98 m del centro; la mitad con un trampolín delante |
| 6 | Zonas de gaviotas y pelícanos | 3–4 (4–6) | una por tramo del 10 al 97 %, en lados alternos |
| 7 | La tropa de las trincheras | sacos en las puntas, fila de erizos antitanque, 60 % campo de minas y un puesto | 18–26 m por delante de la trinchera del mar |
| 8 | Filas que obligan a zigzaguear | hasta 5 (7), al 11, 35, 62, 78 y 88,5 % | de selva a selva con un hueco de 14–22 m que cambia de sitio; de un tema (militar con alambre, restos de la marea o trastos); a veces catapulta o trampolín delante |
| 9 | Calles de ermitaños | 8 (12), por los enemigos | tramos rectos de 25–45 m cuesta abajo |
| 10 | Pulgas de arena | 7 (10) | en claros de arena abierta |
| 11 | Rincones escondidos | 4–6 (6–9) | herraduras de decorado grande junto a la selva, con un cofre al fondo |
| 12 | Puestos militares y campos de minas | 4–5 puestos (6–8), 1–2 filas de erizos, 2–3 campos de minas de 5–9 minas | red, parapeto de sacos, cajas, bidones, cascos y soldaditos |
| 13 | Tanques de juguete | 6 (9) | tramos de 20–40 m de través, a 15–35 m de lo militar o de las trincheras |
| 14 | Ayudas y trampas destacadas | catapultas 9–12 (14–18), trampolines 9–12, plataformas móviles 12–16 (18–24), palas 7–9, conchas que atrapan 12–16, plataformas sobre hoyos 8–11, cubos rotos 7–9, puertas de conchas 5–7 | un tramo por pieza, donde quepa su núcleo; el resto, por toda la playa |
| 15 | Lanzadores delante de lo alto y pasarelas guía | delante de castillos con salas, crestas con cornisa y pozas que cortan un corredor; una pasarela en la 1.ª mitad (80 %) y otra más adelante (50 %) | |
| 16 | Pulpos de poza | 1–2 por poza | dentro del agua |
| 17 | Cofres de sitio especial | 12 (18) | tras una concha que atrapa, tras el alambre, a la espalda de castillos, rocas grandes, troncos y restos de barco, junto a las trincheras, en medio de un campo de minas y tras el arco de un lanzador |
| 18 | Relleno por bandas de 50 m (desde los 15 m hasta 30 m antes del filo) | por banda: ayudas (1,6 + 1,6 t), trampas (6 + 7 t), enemigos (4,5 + 5 t), con t de 0 en la salida a 1 en el mar, por las multiplicadoras de dificultad; luego decorado hasta `BandCoverage` (66–76 %; en la práctica ~50 %): el 80 % pequeño y el resto grande | cada intento mide el sitio que hay y elige algo que quepa |
| 19 | Catapultas garantizadas | al menos 8 (12) | si faltan, los trampolines de delante de un obstáculo pasan a catapultas |
| 20 | Tapones | 0–4 | rellenan las líneas rectas libres de más de 70 m |

**Reglas de todas las pasadas.**

- **Sitio**: nada a menos de **15 m de la salida** (`ItemsStartX`, 23 m de los huevos) ni de **30 m del filo** (`ItemsEndX`). El decorado y las
  trampas llegan hasta 4 m de los muros; los enemigos, con su zona de patrulla entera, a 5 m de la selva como poco (salvo los quads).
  **1,5 m entre huellas** (`ItemPad`; 45 cm entre dos piezas pequeñas; las piezas de las filas, las alas y los rincones se tocan).
  Cada elemento ocupa su **núcleo**: la huella entera en el decorado y las trampas; en los enemigos, cangrejo 25 %, erizo 40 %, lagarto
  33 %, pulpo y pulgas 40 %, calles y tramos 60 %.
- **Terreno**: nada dentro de las pozas (salvo los pulpos), sobre las trincheras ni sobre las cornisas (ni las calles de los
  ermitaños); lo redondo grande (14 m de radio o más) tampoco en lo alto de una cresta.
- **Arcos de salto**: cada catapulta y cada trampolín suelto se pone solo con su arco libre y lo reserva.
- **Paso libre**: lo que cierra el paso (decorado de 6 m de huella o más salvo pasarelas y caminitos, las piezas de los muros, alambre,
  castillos y fortalezas; no el decorado pequeño, ni las trampas que se pisan, ni los enemigos) se infla 4 m; en toda la playa, tras cada
  fila, banda y pasada que cierra, se quita lo último puesto hasta que haya camino: **siempre queda un paso de 8 m**, aunque sea sinuoso, y
  **ninguna línea recta libre de más de 70 m** hacia el mar (`MaxStraightRun`): hay que cambiar de rumbo sin parar.
- **Asiento en la arena** (el «sello» de la ronda): cada elemento del suelo (no los enemigos ni lo que va por encima) deja liso el suelo bajo
  su huella a la cota de la arena en su centro, con un borde de 2,5–16 m hasta la arena natural; `SeatIsGentle` descarta el sitio si la arena
  de alrededor se aparta del nivel más del 40 % del borde. Los pasos de quads no allanan. Se rehacen solo las teselas tocadas, en el
  servidor y en cada cliente. `GetGroundHeightAt` lo da sin trazas (`SeatedZ`).
- **Puntos interesantes** (`FRoundLayout::Interest`, local, para el botín): arcos de salto, cimas, atajos, rincones, trincheras y caminos
  alternativos.

**Reglas por elemento** (`RuleOf`, `TN_BeachLayout.h`; «Máx.» = tope por ronda):

| Elemento | Peso | Tramo del recorrido | Sesgo (mar / lados) | Máx. | Tamaño | Notas |
|---|---|---|---|---|---|---|
| Decorado (por huella) | 1,1 (<5 m), 1,0 (<15 m), 0,55 (<30 m), 0,25 (≥30 m) | todo | — | 1000; 2 si huella ≥30 m | 0,75–1,3 (<15 m), 0,9–1,1; el pequeño (<6 m) 0,6–1,0 | pequeño en corrillos (40 %, hasta 2 más) y no cierra el paso |
| Coco | 1,5 | todo | lados 0,8 | — | — | |
| Sujetador rojo | 0,5 | 20–80 % | — | 1 | — | |
| Restos de vela | 0,8 | desde el 30 % | — | 1 | — | |
| Castillo pequeño / enorme | 1,5 / 0,6 | todo | — | — / 24 | — | el enorme, además de su pasada |
| Pasarela / caminito | 0,35 / 0,4 | todo | — | 10 / 12 | largo 25–60 / 30–80 m, giro ±35°/±30° | no cierran el paso |
| Alambre | 0,9 | todo | mar 0,4 | — | 0,9–1,1, largo 22–60 m, de través ±25° | cierra el paso |
| Algas | 2,4 | todo | mar 0,8 | 240 | — | en campos de 2–5 (55 %) |
| Plataforma sobre hoyo | 0,8 | desde el 8 % | mar 0,25 | 36 | 0,85–1,2 | |
| Cubo roto / pala / puerta | 0,8 / 0,8 / 0,6 | todo | mar 0,25 | — | — | |
| Concha que atrapa | 1,2 | todo | mar 0,6 | 60 | — | corrillo 20 % (hasta 1 más) |
| Plataforma móvil | 1,3 | todo | mar 0,25 | 50 | — | |
| Catapulta / trampolín | 1,4 / 1,4 | todo | mar 0,25 / 0,1 | 50 / 90 | — | lanzadores con arco reservado |
| Mina | 0,35 | todo | mar 0,25 | 150 | — | corrillo 30 % (hasta 2 más) |
| Cangrejo | 3,0 | desde el 2 % | mar 0,6 | 120 | 0,9–1,15 | núcleo 25 %; grupos de 2–3 (40 %) |
| Erizo | 1,0 | desde el 8 % | mar 1,0 | 70 | 0,9–1,15 | núcleo 40 % |
| Lagarto | 0,8 | 2–88 % | lados 0,9; mar −0,3 | 50 | 0,9–1,15 | núcleo 33 % |
| Ermitaño / pulpo / pulgas / tanque | especial | ermitaño desde el 4 % | — | 100 | 0,9–1,1 | por sus pasadas |

**Densidad**: se ajusta con `BandCoverage`, `FillEnemiesBase/Sea`, `FillAidsBase/Sea`, `FillHazardsBase/Sea`, `SmallDecorShare`, `SmallDecorPad`,
`ChestsBase`, los cupos de `PlaceFeaturedTraps` (todos por `LengthScale`) y los pesos de `RuleOf`. Ocupación media (núcleos) de ~48–49 % en
Normal (52 % en el primer tercio).

**Medias por ronda en Normal** (24 semillas, 800 m): 3104 elementos; 98 enemigos (21 cangrejos); 167 trampas que estorban; 73 ayudas (con
fortalezas y cofres); 16,7 cofres; 1,7 castillos con salas y 4,2 enormes; 4,0 fortalezas (1,0 colosal); 1,5 quads y 3,3 zonas de gaviotas; 4,0
filas, 4,9 rincones y 16 piezas militares; 6,3 ermitaños, 11 pulpos, 6,7 pulgas y 6,6 tanques; 10 catapultas, 14,5 trampolines y 15 plataformas
móviles; ocupación 48,3 %. La línea recta libre más larga: 54–94 m.

**Botín de la ronda** (`TN_BeachLoot`, servidor): rebuscables (hasta 240), objetos sueltos (20–25 más 3 filas), conchas de puntos (topes
200 / 27 / 8 / 2) y cofres, todos con la semilla de la ronda (`TN.Beach.Loot.Reroll` cambia la tirada). Al cambiar de ronda se va lo que nadie cogió;
lo que ya está en un inventario se queda.

**Montaje por partes y red** (`TN_BeachRaceGenerator_Round.cpp`): el servidor replica la ronda (semilla y dificultad) ya; el reparto y las alturas
de las teselas se calculan en otro hilo; en el hilo de juego, `TN.Beach.BuildBudgetMs` (6 ms) por fotograma: teselas (con colisión cocinada), decorado
local y, en el servidor, los elementos replicados (unos pocos por fotograma) y el botín. `IsRoundReady` es false hasta que está todo; el GameMode
espera a que el generador esté listo (20 s como mucho) y a que **cada cliente diga que tiene montada su ronda**
(`UTN_BeachRoundSyncComponent`, 12 s como mucho). Cifras esperadas: ~700–1000 actores replicados casi todos dormidos, 200–300 componentes de decorado,
la ronda montada en ~0,5–1,5 s repartidos.

| Qué | Replicado o local |
|---|---|
| Semilla, dificultad, ronda, huevos rotos, nido del sprint | replicado (`FTNBeachRoundNet`) |
| Decorado (49 tipos, civiles y militares) | **local** en cada máquina (`ATN_BeachDecorField`); `DecorCuts` replica los círculos que se quitan (nido del sprint) |
| Asientos del suelo | local (con la semilla) |
| Trampas, ayudas, enemigos, estructuras | actores replicados (`SpawnElement`), relevancia por distancia, dormidos salvo enemigos |
| Cofre (`ATN_BeachChest`) | solo servidor; `ATN_BeachChestSpot` replicado (400 m) |
| Rebuscables | registro replicado compacto (`ATN_BeachSearchRegistry`: un bit por punto, montículos a 8 bytes por punto); el actor de cada uno, solo cerca de una tortuga |
| Objetos sueltos, cajas de objetos, conchas de puntos | actores (dormidos; 150 m y 200 m) |
| Agua de meta y de las pozas, fauna del paisaje, sombras, efectos | local |
| Enemigos que andan | `FTNBeachMoverRep` a 8–12 Hz; quads, gaviotas, tormenta, gusano: solo horas y sentidos |
| Físicas | la caja de la bola de caparazón se replica por física predictiva (`PredictiveInterpolation`) |

### 37.2 Cooperativo: peligros, enemigos y botín (`TNProcMap::PlanHazards`, `TNProcMap::PlanShells`)

`SpawnHazards` (`World/ProcMap/TN_ProcMapGenerator_Spawn.cpp`) recorre las muestras del camino principal y de las ramas (una cada 4 m,
`SampleSpacing`), y por cada regla del bioma (`TN_DefaultBiomeHazards`, o las del `DA_Biome_*` si existe) tira con probabilidad
`PerKm × HazardDensity × 4 m / 1000 m`: en Normal (×2,4), 1,5 por km da unos 3,6 por km. No se ponen en muestras especiales (huecos,
estructuras, puentes, torres, cuevas, géiseres, toboganes, portales, uniones), salvo los de agua y pasarela, ni a menos de `Clearance` de otro
del mismo tipo, ni en las zonas reservadas (géiser, pila de huevos, hueco, muro de lanzamiento, compuerta, interruptor, inicio, meta: 16 m;
peñascos, agujas, objetos del camino y troncos: su radio o semilargo + 2–2,5 m). El **gate de dificultad** compara `Difficulty01` (0,2 / 0,5 /
0,9) con 0 / 0,35 / 0,75: las reglas «Fácil» salen siempre, las «Normal» desde Normal y las «Difícil» solo en Difícil. Colocación
(`ETNProcHazardPlacement`): sobre el camino (±35 % del ancho), en el borde, junto al camino (20–42 % del ancho, lado al azar), en el agua (a 2–6 m
del borde del camino, en la poza), en el aire (a 8 m sobre el suelo más su `ZOffset`: las gaviotas, 6 m más) y lejos del camino.

| Bioma | Peligros por defecto (PerKm, colocación, dificultad mínima, separación mínima) |
|---|---|
| **Todos** | zona de objetos `BP_ItemSpawnZone` (1,5, sobre el camino, Fácil, 120 m); concha de 25 `BP_ScorePickup` (4, sobre el camino, Fácil, 30 m, a 60 cm) |
| Selva y volcánico | zona de cangrejos (0,8, junto al camino, **Difícil**, 100 m) |
| Playa | cangrejos (1,5, junto, Normal, 80 m); gaviotas (0,6, aire, **Difícil**, 150 m); medusa saltarina (2, borde, Fácil, 40 m); cáscara de plátano (1,5, sobre, Fácil, 40 m) |
| Desierto | cangrejos (1, junto, Normal, 90 m); zona lenta (1,2, sobre, Fácil, 70 m) |
| Agua con isletas | rebotador flotante (6, en el agua, Fácil, 18 m); corriente (2, agua, Fácil, 50 m); depredador (1,5, agua, Normal, 90 m); remolino (1, agua, Normal, 90 m); medusa (1,5, borde, Fácil, 50 m) |
| Rocosos | cangrejos (1,2, junto, Normal, 90 m); gaviotas (0,8, aire, Normal, 120 m) |
| Manglar | rebotador (4, agua, Fácil, 20 m); corriente (1, agua, Normal, 60 m); depredador (1, agua, **Difícil**, 100 m); zona lenta (1, sobre, Fácil, 80 m) |
| Zona humana | cáscara de plátano (2,5, sobre, Fácil, 30 m); gaviotas (0,8, aire, Normal, 120 m) |

Los del agua necesitan agua de verdad debajo (el suelo al menos 70 cm bajo el nivel del mar; 1,7 m el depredador, que va 45 cm bajo la
superficie, y 2,2 m el remolino); los de tierra se descartan si el suelo queda por debajo del nivel del mar. Las corrientes empujan
**lejos** del camino (900 cm/s²). Los sitios de peligro en tierra se apuntan (`HazardSpots`) para que
las conchas del plan no se pongan a menos de 3 m. El reparto sale de la semilla del layout (`Seed × 0x9E37 + 0x4A2A`): **mismo mapa, mismos
peligros**, pero se crean solo en el servidor salvo corrientes y remolinos (locales en todas las máquinas).

**Cuánto hay** (estimación con los perfiles por defecto, sin contar ramas): en Normal el camino principal de ~12 km lleva del orden de 3,6
zonas de objetos por km y 9,6 conchas de 25 por km, más los cangrejos, gaviotas y demás de los biomas por los que pase (el registro `[ProcMap]
Peligros y enemigos: N de M planificados` dice el número real). Conchas: §32.13 (Fácil: 150–250 conchitas, 1 grande y hasta 1–2 reinas;
Normal: 500–600, 4 grandes y 1–3 reinas; Difícil: el tope de 600, 5 grandes y 2–4 reinas). Rebuscables: §32.12.

### 37.3 Dificultad: qué cambia y qué no

**Carrera** (`FDifficultyProfile`, `DifficultyProfileOf`; el general la elige en el lobby y viaja con la ronda):

| | Fácil | Normal | Difícil |
|---|---|---|---|
| Multiplicadores (ayudas / trampas / enemigos) | ×1,6 / ×0,7 / ×0,6 | ×1 | ×1,4 / ×1,8 / ×2,5 |
| Elementos | 2822–3286 (media 3096) | 2876–3271 (3104) | 2458–2939 (2750) |
| Decorado (el pequeño) | ~2820 (~2770) | ~2770 (~2720) | ~2210 (~2170) |
| Enemigos | 57–74 (66; ×0,68) | 87–113 (98) | 217–264 (242; ×2,5) |
| Trampas que estorban | 99–134 (114; ×0,68) | 140–190 (167) | 189–246 (222; ×1,33) |
| Ayudas (con fortalezas y cofres) | 88–110 (99; ×1,35) | 65–81 (73) | 61–89 (77; ×1,05) |
| Cangrejos / erizos / lagartos | 15 / 17 / 10 | 21 / 25 / 16 | 60 / 74 / 42 |
| Ermitaños / pulpos / pulgas / tanques | 4,7 / 7,1 / 4 / 4,7 | 6,3 / 11 / 6,7 / 6,6 | 8,5 / 26 / 16,7 / 7,8 |
| Quads / zonas de gaviotas | 1–2 / 2–3 | 1–2 / 3–4 | 2–3 / 4–6 |
| Fortalezas (medianas / grandes / colosales) | 2,8 / 1,4 / 1,25 | 2,0 / 1,0 / 1,0 | 2,3 / 0,9 / 1,1 |
| Cofres | 11–23 (17) | 15–18 (17) | 15–22 (19) |
| Catapultas / trampolines / plataformas móviles | 16 / 21 / 18 | 10 / 14,5 / 15 | 9,6 / 13 / 15,5 |
| Conchas que atrapan / minas / algas | 11 / 36 / 13 | 17 / 52 / 19,5 | 23 / 79 / 15 |
| Castillos con salas / enormes | 1,4 / 4,2 | 1,7 / 4,2 | 1,5 / 3,6 |
| Filas / rincones / piezas militares | 4,2 / 5 / 14 | 4,0 / 4,9 / 16 | 3,6 / 4,6 / 21 |
| Línea recta libre más larga | 62–98 m | 54–94 m | 58–94 m |
| Rebuscables | ×1,6 | ×1 | ×1,4 |

La playa ya está llena en Normal: en Difícil los cupos llevan el multiplicador entero, pero las trampas y las ayudas no caben todas (×1,3 y ×1,05);
los enemigos sí (×2,5). En Fácil sobran huecos (×0,68, ×0,68 y ×1,35). En Difícil, los enemigos «de bulto» (cangrejos, erizos, lagartos, pulpos y
pulgas) se apiñan: su núcleo encoge como 1/√2,5 (`CoreFractionOf`); su huella de patrulla no cambia. **El comportamiento de los enemigos no
cambia con la dificultad** (solo cuántos hay); tampoco la tormenta, el terreno ni el decorado.

**Cooperativo**: `Difficulty01` (0,2 / 0,5 / 0,9) y el perfil de §32.1 escalan la rejilla (3 / 6 / 8), los cruces (1 / 2 / 4), las ramas (6 / 12 / 16),
la densidad de peligros (1,6 / 2,4 / 3,2), los huecos por km (9 / 13 / 17), la frecuencia de pilas de huevos (cada 1 / 2 / 3 cruces), la
velocidad y la gracia de la tormenta (160 / 180 / 200 cm/s; 90 / 60 / 45 s), las brechas (1,19–1,66 / 1,33–1,9 / 1,5–2,22 m) y cornisas (85 / 78 /
68 cm) del adarve de las murallas, y qué peligros salen (gate de §37.2: gaviotas de la playa, depredadores del manglar y cangrejos de selva y volcán
solo en Difícil; cangrejos de playa, desierto y roca, y depredador y remolino del agua, desde Normal).

# Parte IV — Apéndices

## 38. Comandos de prueba

Resumen por sistema. **La lista completa, con todos los argumentos y las pruebas paso a paso, está en
[`Docs/Comandos_Prueba.md`](Comandos_Prueba.md)**; los comandos de cada pieza se citan además en su sección de este documento, en
[`Docs/Modo_Carrera.md`](Modo_Carrera.md) (apartado «Probar» de cada elemento) y en [`Docs/Botin_Decorados.md`](Botin_Decorados.md). Se
escriben en la consola del **anfitrión** (en PIE con varios jugadores, la del servidor; los efectos solo locales, en la ventana de quien
escucha). Los índices de jugador empiezan en 0 (0 = el anfitrión).

| Sistema | Comandos principales | Para qué |
|---|---|---|
| Abrir y moverse | `open LVL_BeachRace?BeachSeed=42` (`?BeachWins=1`), `open LVL_ProcMap?ProcMode=Coop?ProcDifficulty=Hard?ProcSeed=42`, `TN.Mode Coop\|Race`, `fly`, `ghost`, `teleport`, `slomo`, `stat fps\|unit` | Jugar sin lobby con semilla fija |
| Reparto de la playa | `TN.Beach.Go <sitio\|metros> [jugador]` (salida, fortaleza, trinchera, poza, cresta, sprint, acantilado, meta), `TN.Race.Difficulty`, `TN.Beach.Reroll [semilla]`, `TN.Beach.ShowFootprints 1` | Ir a un sitio, cambiar la dificultad, rehacer la ronda y ver las huellas |
| Poner piezas | `TN.Beach.Place <Elemento> [Tamaño] [Extent] [Semilla]` (`clear` borra), `TN.Beach.PlaceBoosted`, `TN.Beach.Fortress.Top`, `TN.Beach.Chest` | Cualquier elemento de `ETNBeachElement` delante de ti |
| Enemigos | `TN.Beach.Gull.Attack 1\|2`, `TN.Beach.Gull.Grab [veces=2] [jugador]`, `TN.Beach.Quad.Now`, `TN.Beach.Lizard <huidizo\|generoso\|mordedor>`, `TN.Beach.StunNearest [s]`, `TN.Beach.Enemy.Stats`, `TN.Beach.Enemy.Debug 1` | Probar cada enemigo sin buscarlo; `Gull.Grab` reproduce el «segundo agarre» |
| Tormenta y gusano | `TN.Beach.Storm.Start\|Stop\|Info\|Here`, `TN.Beach.Worm [jugador]`, `TN.Race.Bury [m] [j]`, `TN.Race.SafetyNet 0\|1` | Frente, patada, gusano y red de seguridad |
| Carrera | `TN.Race.WinRound [j] [puesto]`, `TN.Race.NextRound`, `TN.Race.Sprint [j]…`, `TN.Race.Champion [j]`, `TN.Race.Stun [s] [j]`, `TN.Race.Kill [j]`, `TN.Race.Void [j]`, `TN.Race.TimeLeft`, `TN.Race.Splash`, `TN.Beach.Egg`, `TN.Race.PlayAgain`, `TN.Race.ChangeMode`, `TN.Race.Menu` | Llegadas, paso entre rondas, sprint, aturdir y salida de los huevos; con `puesto` (1-8) la pantalla «Has quedado X.º» enseña ese premio |
| Vistas previas | `TN.Race.ArrivalPreview [puesto] [1 = sprint]`, `TN.Race.RoundPreview [ronda] [1 = sprint]`, `TN.Race.CountdownPreview`, `TN.Race.ClockPreview`, `TN.Race.Tally`, `TN.Race.SprintPreview`, `TN.Race.Podium`, `TN.Race.PreviewOff` | Pantallas sin jugar |
| Botín | `tn.Search.Luck\|Seconds\|Show`, `TN.Beach.Loot.Reroll`, `TN.Beach.Loot 0\|1`, `TN.Beach.Chest` | Rebuscar, cofres y conchas |
| Objetos de carrera | `TN.Race.Item <objeto\|list> [j]`, `TN.Race.ItemUse`, `TN.Race.ItemBox [n]\|clear`, `TN.Race.ItemRank`, `TN.Race.Boost`, `TN.Race.Star`, `TN.Race.ItemClear` (§29.9) | Dar y usar objetos, ver los pesos por puesto |
| Plancha y caparazón | `TN.Dive.Slide`, `.Friction`, `.Slope`, `.MaxTime`, `.Body`, `.Debug` (§17.6); `TN.Shell.PhysicsRep 0\|1`; `TN.Shell.Impact 0\|1`, `.Debug`, `.Volume`, `.MinSpeed`, `.Test <timbre\|todos> [fuerza]` (§9.7) | Simulación de la plancha, réplica de la bola y golpes del caparazón |
| Fantasma | `TN.Ghost.Become [j]` (cualquier modo, también el lobby), `TN.Ghost.Revive [j]` (cooperativo y lobby) | Espectador y volver a la vida desde un huevo |
| Cara y HUD | `tn.Face.Mood 0-3`, `tn.Face.Tongue 0-3`, `tn.Face.Talk 1`, `tn.HUD.Face 0-5`, `tn.HUD.Energy`, `tn.HUD.Talk`, `tn.HUD.CrewPreview` | Caras de la tortuga y del HUD |
| Sonido y música | `TN.Voice.*` (`Volume`, `Surface`, `Steps`, `Pant`, `Drag`, `Debug`), `TN.Music.Play <pista>`, `TN.Music.MatchVolume`, `TN.Race.Music.Play\|Stop\|Auto\|Restart\|Status\|Tension\|Duck\|Layers\|Force\|Volume\|Debug`, `TN.Ambience.Debug\|Volume`, `TN.Storm.Cough 0\|1\|2` | Foley, voz, música de fin de partida y de la carrera (funciona en cualquier mapa) |
| Ajustes e idioma | `TN.Fisheye.D`, `TN.Fisheye.S`; en la línea de comandos, `-culture=xx`, `-language=xx`, `-culture=LEET`, `-culture=keys` | Intensidad del ojo de pez y probar idiomas y textos sin localizar (§10.1) |
| Interacción | `TN.Debug.Interaction 1` | Esfera y líneas de depuración de la interacción |
| Tienda y lobby | `TNShop`, `TNBooth`, `TN.Lobby.Castle 0`, `TN.Lobby.Valley 0` | Abrir la tienda y el probador; apagar el castillo y el valle |
| Cooperativo | `TNStorm <bioma\|Geiser\|Cascada\|Off>`, `TNShells <1\|25\|50\|100\|Especial\|Lista>`, `TNRegen`, `TN.Proc.StartStyle 0\|1\|-1`, `TN.Proc.Egg`, `TN.Fauna.Enable\|Stats` | Tormenta, conchas, salida, fauna |
| Rendimiento | `TN.Beach.Perf`, `TN.Beach.BuildBudgetMs`, `TN.Beach.AsyncBuild` | Tiempos de montaje y red |
| Editor | `Preview Round` / `Clear Preview` en el generador de la playa; `Generate In Editor` en el generador procedural | Ver el reparto sin jugar |
| Pruebas automáticas | `Automation RunTests Tortunabo.Beach` (`Terrain`, `Relief`, `Layout.Determinism\|Rules\|Difficulty`, `Hold.*`, `Gull.*`), `Tortunabo.ProcMap.*`, `Tortunabo.Shell`, `Tortunabo.Inventory`, `Tortunabo.Settings.Language.*`, `Tortunabo.Multiplayer.RoomNames.*` | Terreno, reparto, mapa, sujeción de enemigos, nerf de las gaviotas, idiomas y nombres de sala |

## 39. Cifras clave

| Cifra | Valor | Fuente |
|---|---|---|
| Jugadores | 1–8; salas de 4, 6 u 8 | `DefaultGame.ini`, `TNRoomLimits` |
| Código de sala | 5 caracteres de 31 posibles | `TN_RoomTypes.h` |
| Nombres de sala | 242, localizables (en el idioma elegido en el juego) | `TN_RoomNames.cpp` |
| Idiomas | 13 (es-ES origen, en, fr, de, it, pt-BR, ru, pl, tr, ja, ko, zh-Hans, zh-Hant); por defecto el del sistema si está en la lista y, si no, el español | `TN_LanguageSettings.*` |
| Cuenta atrás del lobby / pausa antes del viaje | 3 s / 2 s | `TN_HQGameMode.h` |
| Espera a jugadores tras el viaje | 15 s | `TN_RunGameMode.h` |
| Espera al mapa procedural / a los clientes en la playa | 30 s / 12 s | `TN_ProcMapGameMode.h`, `TN_BeachRaceGameMode.h` |
| Resultados del cooperativo | 8 s | `TN_RunGameMode.h` |
| Tormenta del cooperativo | 160 / 180 / 200 cm/s; gracia 90 / 60 / 45 s; 5 s dentro para morir | `TN_ProcMapTypes.cpp`, `TN_PathStorm.h` |
| Reaparición en el cooperativo | 2,5 s; pila a ≥30 m por delante de la tormenta | `TN_ProcMapGameMode.h` |
| Playa | 800 × 280 m, escala ×28, acantilado 15,5 m, sprint a 400 m | `TN_BeachTypes.h`, `TN_BeachLayout.h` |
| Elementos por ronda de playa | ~3100 (Normal) | [`Docs/Modo_Carrera.md`](Modo_Carrera.md) |
| Conchas para ganar | 3 (6 medias) | `WinsToWinMatch` |
| Ronda de playa: previa / cuenta 3-2-1 / cuenta tras la primera / recuento | ≥2 s / 3 s / 10 s / 7 s | `TN_BeachRaceGameMode.h` |
| Límites de tiempo | Ronda 9 min; sprint 4 min 30 s; carrera procedural 15 min | `TN_BeachRaceGameMode.h`, `TN_ProcMapGameMode.h` |
| Aturdimiento | 3 s (golpe), 2,5 s (recolocada) | `TN_BeachRaceGameMode.h` |
| Llegada al agua | Cáscara oscura en 0,28 s; «Has quedado X.º» 2,7 s; el recuento espera 3,3 s tras la última llegada; paso entre rondas: cáscara en 0,32 s | `TN_RaceArrivalWidget`, `ArrivalScreenHoldSeconds` |
| Gaviotas | Ataque cada 4–7 s a menos del 80 % de la huella; blanco a 5,4 m/s (últimos 0,6 s a 1,2 m/s); coge a 2,2 m × tamaño + 0,25 m; la plancha libra de la cagada si va en el aire o a ≥ 2,5 m/s | `TN_BeachGullTuning.h` |
| Música de la carrera | Mi bemol mayor, 116 BPM, bucle de 132 s; RMS −29 dBFS; categoría Música | `TN_RaceMusicDSP.h` |
| Golpes del caparazón | Desde 260 cm/s (máximo a 1800); 8 sonidos por segundo en todo el mundo como mucho | `TN_ShellImpactFXComponent` |
| Ojo de pez | Panini `r.LensDistortion.Panini.D` = 0,55, apagado de serie (#634) | `TN_GameSettingsSubsystem.cpp` |
| Velocidad de la tortuga | **andar 200 y correr 400 cm/s** reales (el Blueprint pisa los 450 y 800 del código; §13); salto 485 cm/s (120 cm de alto, 0,99 s) | `BP_TortugaCharacter`, `TN_StaminaComponent.h` |
| Energía | 200 máx.; correr gasta 15/s (≈13 s, 53 m con la velocidad real) | `TN_StaminaComponent.h` |
| Duración de la ronda de playa | ≈ 4 min 46 s con la media real de ~2,8 m/s [calc]; el juego anuncia 3,3 min | `AverageRaceSpeed` |
| Voz | 16 kHz, μ-law 8 bit, 4 oyentes, 3 m pleno y 25 m máximo | `ProximityVoiceComponent.h` |
| Red | 60 Hz de servidor, 200 000 B/s por cliente, 30/45 s de timeout | `DefaultEngine.ini` |
| Tienda | 12 cascos, 10 caparazones, 12 colores, 9 ojos; precio 0 | [`Docs/Tienda_Probador.md`](Tienda_Probador.md) |

## 40. Huecos detectados

Lo siguiente **no existe** en el código actual, o existe a medias. Va separado en accesibilidad (§40.1) y juego (§40.2), de más a menos
importante para quien lo necesite. Ninguno se cuenta como existente en el resto del documento.

### 40.1 Accesibilidad

| # | Hueco | Evidencia | Por qué importa | Idea mínima |
|---|---|---|---|---|
| 1 | **Sin ajustes en el menú principal**: hasta entrar en un lobby no se pueden cambiar el filtro de daltonismo, el tamaño de la interfaz, los volúmenes, el micrófono ni el idioma. **En curso (sin subir)**: un botón «Ajustes» que abre el mismo menú; el modo «menú principal» dentro de ese menú (portada reducida, cabecera «AJUSTES») sigue pendiente | `AMP_MenuPlayerController` no monta menú de pausa; los botones del Blueprint son Crear, Unirse y Salir ([`Docs/Menu_Pausa.md`](Menu_Pausa.md), «Ajustes desde el menú principal») | Quien lo necesita para **empezar** ni ve el menú a su gusto; un idioma que no se lee no se puede cambiar | Terminar el botón «Ajustes» con el mismo `UTN_PauseMenuWidget` |
| 2 | **Solo mantener, nunca alternar**: correr (Mayús) y las ruedas (Q, C) se mantienen; rebuscar (1,3 s), el cofre del lobby (5 s) y el de la playa (5,5 s) piden mantener E | Enlaces de `IA_Sprint` (Started/Completed/Canceled) e `IA_Interact` (Completed = suelta); `FTNGameSettings` no tiene ese ajuste | Motricidad, dolor de manos, fatiga | Ajuste «Mantener / Alternar» por acción y duración reducida de las interacciones |
| 3 | **Sin opción de reducir destellos y efectos parpadeantes** (solo hay «Temblor de cámara») | Estallido de concha con luz de hasta 8000 lm en 0,35 s ([`Docs/Mapa_Procedural.md`](Mapa_Procedural.md)), fogonazo del «¡PUM!» del huevo ([`Docs/Pantalla_Carga.md`](Pantalla_Carga.md)), rayos giratorios y confeti del sprint y del campeón, y (ronda 4) el fogonazo al romperse el huevo negro y los rayos y el confeti de la corona de oro ([`Docs/Modo_Carrera.md`](Modo_Carrera.md)) | Fotosensibilidad | Ajuste «Reducir destellos» que apague fogonazos y baje la luz de los estallidos |
| 4 | **Sonidos que avisan y no tienen equivalente visual dirigido** | Pasos del cangrejo gigante («avisan de que viene»), motor del tanque, graznido; [`Docs/Modo_Carrera.md`](Modo_Carrera.md), «Sonido de ambiente». Hay visuales para gaviotas y quads, no para todo | Sordera y audición reducida | Indicadores direccionales o subtítulos de sonido |
| 5 | **Sin alto contraste ni contornos** de enemigos, objetos y jugadores | El filtro de daltónicos corrige colores, no aumenta el contraste; todo es color de vértice de mundo low-poly | Baja visión | Modo de alto contraste o contorno de lo interactuable |
| 6 | **Sin tamaño de texto propio**: solo la escala global 75–130 %; hay textos pequeños en el HUD (etiquetas de 11–13 pt a 1080p) | `TN_RunHUDWidget.cpp` (`SlotTags` 11 pt, nombres 13–16 pt) | Baja visión | Ampliar el rango o escalar solo el texto |
| 7 | **Mando sin zona muerta, curva ni umbral de gatillo**; sticks y elección en la rueda no reasignables; sin vibración. (B / Círculo ya mete en el caparazón desde la ronda 4: §6.1) | `IMC_Player`; [`Docs/Menu_Pausa.md`](Menu_Pausa.md) («no hay `ForceFeedback`») | Motricidad, hardware variado | Zona muerta y curvas por stick, vibración opcional |
| 8 | **Los emotes directos no tienen tecla**: `IA_Emote…` existen y están cableadas pero `IMC_Player` no las mapea | §6.1 | Quien no puede mantener una rueda | Mapearlas o exponerlas en Controles |
| 9 | **Pantalla del campeón solo con ratón** (sin evidencia de foco de teclado o mando en sus botones) | `TN_RaceChampionWidget.cpp` (botones con `OnClicked`/`OnHovered`, sin `NativeOnKeyDown`) | Quien juega solo con mando o teclado ve la pantalla pero no podría elegir; el menú de pausa sí abre encima. **No confirmado en juego** | Foco y navegación con mando |
| 10 | **Sin ayuda de orientación en la carrera**: ni pista de progreso ni indicador de puesto ni flecha a la meta; solo se ve el mar (el puesto solo sale al llegar) | `TickTrack` exige `ATN_ProcMapGenerator`; el cartel de estado se oculta en `InProgress` | Dificultades cognitivas o de orientación | Indicador opcional de distancia al filo y de puesto |
| 11 | **Sin chat de texto libre, ni voz a texto, ni texto a voz** | Solo frases prefabricadas (`TN_QuickChatWheelDataAsset`) | Quien no puede hablar ni oír queda limitado a las frases de la rueda | Transcripción opcional de la voz |
| 12 | **Sin lector de pantalla ni exposición de la interfaz a tecnologías de asistencia** | Los menús son widgets de UMG hechos en código con dibujo propio (`Private/UI/...`); no hay rastro de accesibilidad de UMG | Ceguera | Fuera de alcance realista para un juego de acción visual; se cita por completitud |
| 13 | **Idiomas: selector hecho, traducciones pendientes**: los 13 idiomas se pueden elegir (ronda 4), pero mientras los `.po` no estén traducidos todo sale en español, y para japonés, coreano y chino faltan fuentes propias (la reserva del motor solo tiene un peso) | §10 | Barrera de acceso | Traducir (§10.5) y dejar las fuentes Noto Sans en `Content/Slate/Fonts` |
| 14 | **Resuelto (#634): el «Ojo de pez leve» venía activado por defecto** y es una distorsión de lente que puede marear | `bFisheye = false` desde #634 (`TN_SettingsSaveGame.h`); Panini 0,55 (`TN_GameSettingsSubsystem.cpp`) | Mareo | Se apaga en la misma pestaña; comprobar en juego que no marea con el campo de visión al correr y valorar que respete el ajuste de temblor |
| 15 | **No hay asistencia de partida por persona** (velocidad, tiempo extra, ayudas) | La dificultad es de la sala | Motricidad | Opcional; hoy la Fácil ayuda a todos por igual |

### 40.2 Juego

| # | Hueco | Evidencia | Estado o idea |
|---|---|---|---|
| 1 | **Bucear no existe** como mecánica: no hay control vertical ni aire | §18 | Deducido del código (no confirmado en juego) |
| 2 | **Volver a la vida desde un huevo no está enganchado**: `TNGhost::ReviveIntoEgg` solo lo usa `TN.Ghost.Revive`; la reaparición real del cooperativo teletransporta a la tortuga a una pila de huevos, y los «nidos» del futuro no existen | §22.5, [`Docs/Fantasma_Espectador.md`](Fantasma_Espectador.md) | Enganchar en los nidos o descartar |
| 3 | **El estado «casi muerta» (DBNO) está desactivado**: nadie llama a `EnterDBNO`; la cara «mareada» y el cartel «¡Panza arriba!» dependen de él | §20.1, §24.3 | Reactivar o quitar el código |
| 4 | **Tutorial**: no hay modo tutorial; la ruta del primer arranque (`TutorialStart`, `ATN_TutorialEntryInteractable`) está en el código sin contenido en los mapas | §2.7 | En curso un pasillo de tutorial en el cielo (§11) |
| 5 | **Las cajas de objetos no reaparecen ni dicen lo que dan**; las minas lanzadas solo miran el suelo | §29.10 | Decisión de diseño pendiente |
| 6 | **Sin estadísticas ni logros**: solo se guarda `AccumulatedRaceScore` | §30.7 | Diseñar el perfil |
| 7 | **Sin economía**: los 43 cosméticos cuestan 0; `OpenHelmetCrate` existe con la tabla `HelmetCrateTable` vacía; la fila `Score` de `DT_Items` no sale nunca | §31.3, §28.1 | Diseñar precios y cajas |
| 8 | **La barra llena (`SelfStaminaFull`) está en el código y en los pesos, pero no hay fila en `DT_Items`**: no sale | §28.1, §29.5 | Añadir la fila o quitar el uso |
| 9 | **Los objetos de siempre no tienen nombre visible** (solo icono, y de relleno del motor) | §27.1 | Nombres e iconos definitivos |
| 10 | **La sombrilla no protege de la tormenta**, aunque el comentario de `ATN_StormVolume` lo promete | §28.3 | Implementar o quitar el comentario |
| 11 | **Los montículos que vibran solo existen en la playa** (en el cooperativo, pendiente) | §34.12 | Llevarlos al mapa procedural |
| 12 | **El 2 vs 2 no tiene entrada normal** (ni el menú de crear ni el general lo ofrecen) | §2.5 | Ofrecerlo o retirarlo |
| 13 | **Sin sonido de nadar ni de meterse y salir del caparazón**; los sonidos del personaje que el Blueprint podría asignar están todos vacíos | §9.6 | Asignar o sintetizar |
| 14 | **Lo común de la carrera aún no está en el cooperativo**: salas, menú de pausa, fantasma, pausa del huevo, brillo de objetos (y ahora idioma, ojo de pez y Círculo/B) | `Docs/Plan_Carrera_Ronda4.md` (eliminado), «Pendiente de antes» | Llevarlo al cooperativo |
| 15 | **Servidor dedicado sin declarar**: varias piezas evitan construir lo visual y lo sonoro en un servidor dedicado, pero ningún documento ni configuración lo da por soportado | §5.1 | Decidir |
| 16 | **Voz sin Opus** y ancho de banda sin medir con `stat net` (con 4 y con 8) | §5.4, §26 | Pendiente |
| 17 | **Con Steam, una sala llena no sale en la lista ni por código** (Steam oculta los lobbies llenos) | §3.4 | Pendiente conocido |
| 18 | **Clases del modo clásico que solo describe el LDD**: erizo venenoso, arenas movedizas y cangrejo enterrado no tienen clase C++ propia (podrían ser Blueprints) | §36.16 | Confirmar en el editor |
| 19 | **Pantalla dividida**: `bUseSplitscreen=True` en `DefaultEngine.ini` sin soporte documentado | §42 | Confirmar si funciona o quitarlo |

## 41. Discrepancias entre documentos y código

Dónde una fuente dice una cosa y el código otra. **Manda el código**; la columna de la derecha dice cómo queda resuelto en este documento.

### 41.1 Velocidades y cifras que dependen de ellas

| # | Dónde | Dice | El código dice / cómo queda |
|---|---|---|---|
| 1 | `TN_StaminaComponent.h`, [`Docs/Modo_Carrera.md`](Modo_Carrera.md), [`Docs/Animacion_Tortuga.md`](Animacion_Tortuga.md), comentarios de `TN_BeachStorm.h` y `TN_BeachGullTuning.h`, y la primera versión de la parte 1 de esta biblia | Andar **450** cm/s (4,5 m/s) y correr **800** cm/s (8 m/s) | `BP_TortugaCharacter` fija **200 y 400 cm/s** y es lo que se juega: de 190 saltos en los registros del 28-09, 141 salen a 400 exactos y ninguno pasa de 400; el generador del mapa procedural usa 200 y 400 (`TNProcMap::TurtleJump`); [`Docs/Mapa_Procedural.md`](Mapa_Procedural.md) y [`Docs/Traspaso_Sesion_Cloud.md`](Traspaso_Sesion_Cloud.md) ya lo decían. **Resuelto aquí**: todo va con 200 y 400 (§13). Faltan por corregir los comentarios de código y los documentos de origen |
| 2 | `AverageRaceSpeed` = 400 cm/s (`TN_BeachRaceGameMode.cpp`) y [`Docs/Modo_Carrera.md`](Modo_Carrera.md) | Ronda de playa de ~3 min 20 s (el GameState anuncia 3,3 min) | Con la media real de ~2,8 m/s [calc] son ≈ 4 min 46 s, sin trampas ni enemigos (§33.1). Las pruebas de una tortuga agotaban el límite de ronda ([`Docs/Modo_Carrera.md`](Modo_Carrera.md), «¡TIEMPO! sin nadie en el agua») |
| 3 | [`Docs/Mapa_Procedural.md`](Mapa_Procedural.md) §7 | Cooperativo Normal de ~35–40 min «a 5,5 m/s» | 5,5 m/s no es ni andar (2) ni correr (4): unos 70 min con 2,8 m/s [calc] para ~12 km (§32.1) |
| 4 | [`Docs/Modo_Carrera.md`](Modo_Carrera.md), «Nerf de la gaviota y de su caca»; comentario de `TN_BeachGullTuning.h` | Las cuentas del nerf suponen 450 y 800: quien corre en línea recta se libra con 8 a 11 m de ventaja | Con 200/400 el blanco (5,4 m/s) es más rápido que quien corre y solo libran la plancha, el caparazón (picado), el techo y el turbo (§36.7). **Sin confirmar en juego** |
| 5 | [`Docs/Modo_Carrera.md`](Modo_Carrera.md), «Cangrejo gigante» y `TN_BeachGiantCrab.h` | «La esprintando (8 m/s) se escapa» del cangrejo (5,6 m/s) | Esprintando se va a 4 m/s: el cangrejo la alcanza; solo el turbo del coco (8 m/s) la deja atrás (§36.3) |
| 6 | `TN_BeachGiantCrab.h` (`SlamHeight` 280 cm) | «Saltando por encima de la pinza se libra» | Un salto llano levanta el centro de la cápsula a ~2,1 m [calc] (88 cm de semialtura + 120 cm de salto), por debajo de 2,8 m: no confirmado en juego (§36.3) |
| 7 | [`Docs/Modo_Carrera.md`](Modo_Carrera.md), «Tormenta de bañistas» | La tormenta (1,8 m/s) es lenta frente a una tortuga que anda a 4,5 m/s | Andando a 2 m/s le saca solo el 10 %, y ya acelerada (hasta 3 m/s) alcanza a quien anda (§36.12) |
| 8 | [`Docs/Modo_Carrera.md`](Modo_Carrera.md), trampolín y catapultas | Alcances del trampolín (~16 m andando, ~23 m esprintando; potenciado 39 y 48 m) | Con 200/400: ~12 y ~16 m; potenciado ~34 y ~38 m [calc] (§34.2) |

### 41.2 Mecánicas, controles y comentarios

| # | Dónde | Dice | El código dice |
|---|---|---|---|
| 9 | Primera versión de la parte 1 (controles) | Reanimar se hace manteniendo E | A una **derribada** la reanima un compañero con un **emote** a menos de 300 cm durante 3 s (`ReviveRadiusCm`, `ReviveDurationSeconds`); a una **eliminada** se la rescata con E **sin mantener** sobre su cuerpo (`ATN_RescuePickup`). Resuelto en §6.1, §20.3 y §22.3 |
| 10 | [`Docs/Animacion_Tortuga.md`](Animacion_Tortuga.md) | Rebote de la bola 0,35 | 0,2 (`ATN_ShellBody`, [`Docs/Modo_Carrera.md`](Modo_Carrera.md)) |
| 11 | [`Docs/Animacion_Tortuga.md`](Animacion_Tortuga.md) | Cambiar de ranura con R | G (`IMC_Player`) |
| 12 | Comentario de `ATN_StormVolume` | La sombrilla protege de la tormenta | Nada en el código lo comprueba (solo gaviotas y su caca) |
| 13 | [`Docs/Menu_Pausa.md`](Menu_Pausa.md) (según la primera versión de la parte 1) | Retroceso saca de la partida (`IA_Quit`) | `IA_Quit` se quitó de `IMC_Player` el 28-09-2026; [`Docs/Menu_Pausa.md`](Menu_Pausa.md) ya lo recoge («Retroceso ya no saca de la partida») |
| 14 | Comentario de la cara «mareada» del HUD | «Panza arriba o eliminada» | Solo en DBNO (inactivo) o eliminada; una derribada no cambia de cara ni ve el cartel «¡Panza arriba!» |
| 15 | Comentario de `TortugaCharacter.h` | Las teclas 0–9 deberían mapearse a los emotes | No están en `IMC_Player`; los bailes solo salen por la rueda |
| 16 | Pestaña «Reglas» del general (`TN_BriefingWidget.cpp`) | «Cada ronda dura como mucho 6 minutos»; reaparición «en Carrera y 2 vs 2, la tuya» | 9 min (`RoundTimeLimitSeconds` 540); en la playa no se muere |
| 17 | `Docs/Plan_Carrera_Ronda4.md` (eliminado), tarea 10 | Ojo de pez con `r.Upscale.Panini.D` y `.S` | En UE 5.6 los cvars son `r.LensDistortion.Panini.*` ([`Docs/Menu_Pausa.md`](Menu_Pausa.md)) |
| 18 | `Docs/Plan_Carrera_Ronda4.md` (eliminado), tarea 11 | Círculo/B «de serie» en `IMC_Player` | Resuelto en #637: el asset ya la trae (`Scripts/imc_player_shell_b.py`, §6.1) |
| 19 | [`Docs/Modo_Carrera.md`](Modo_Carrera.md), «Algas» | «Hasta 4 a la vez» | `MaxCatches = 8` (y el propio documento, en «Hecho para 8 jugadores», dice 8) |
| 20 | `TN_CrabSpawnZone.h` (comentario) | «Default 3» | `SpawnCountOnEnter = 5` |
| 21 | LDD | Gaviota con «bombardeo en zona aleatoria cada ~8 s» y «agarra si quieto más de 2 s» | `ATN_EnemySeagull` tiene cronómetro de 8 s y círculo que se encoge; no hay regla de «quieto 2 s» |

### 41.3 Documentos y configuración desactualizados

| # | Dónde | Dice | El código dice |
|---|---|---|---|
| 22 | [`Docs/Mapa_Procedural.md`](Mapa_Procedural.md) §4; memoria del proyecto (`project_overview`) | Cooperativo y carrera de «1–4» jugadores | 1–8 (`MaxPlayers` 8, salidas con 8 sitios) |
| 23 | [`Docs/Lobby_Castillo.md`](Lobby_Castillo.md) | El general tiene cuatro pestañas | Cinco (`NumTabs`), con «Misión» |
| 24 | [`Docs/Modo_Carrera.md`](Modo_Carrera.md), «Bucle de juego» 1 | El menú pregunta el modo en dos pasos | La pantalla «Crear partida» ([`Docs/Salas.md`](Salas.md)) |
| 25 | [`Docs/Tienda_Probador.md`](Tienda_Probador.md) | Pestañas con «gatillos» | LB/RB (`TN_ShopWidgets.cpp`) |
| 26 | README | `GameInstanceClass=/Script/Tortunabo.MP_GameInstance`; «73 archivos .h» | `BP_GameInstance` (subclase Blueprint); 286 cabeceras (Public + Private) y 236 `.cpp` |
| 27 | `Docs/Inventario_Scripts.md` (eliminado) | Inventario de 73 cabeceras | Desactualizado |
| 28 | [`Docs/Mapa_Procedural.md`](Mapa_Procedural.md) §1.7 | `LVL_ProcMap` y `Content/ProcMap` no están en el repositorio | Los `.umap` de `LVL_ProcMap` y `LVL_ProcMap_Terrain` y `Content/ProcMap` están versionados |
| 29 | [`Docs/LDD_Tortunabo.md` (eliminado)](LDD_Tortunabo.md (eliminado)) | Diseño de 5 rondas de 6 módulos con bañistas | Es el modo Clásico (§1.6) |
| 30 | `Config/DefaultEngine.ini` | `bUseSplitscreen=True` con tres diseños | No hay soporte documentado para pantalla dividida (no confirmado que funcione) |

## 42. Lo no confirmado

Lo que no se ha podido comprobar en el código, en un documento o en pantalla. Nada de esto se da por hecho en el resto del documento.

### 42.1 Del diseño y del proyecto

1. **Público objetivo**: no hay documento; el §1.3 es deducción.
2. **Servidor dedicado**: no hay evidencia de que se use ni se soporte (§5.1).
3. **Pantalla dividida** (`bUseSplitscreen=True` en `DefaultEngine.ini`): sin soporte documentado.
4. **Entrada a media partida en el cooperativo**: la sesión lo permite (`bAllowJoinInProgress`); el comportamiento del cooperativo con un
   jugador que llega tarde no está descrito.
5. **Tutorial**: por qué no hay `PlayerStart` etiquetado `TutorialStart` en los mapas actuales (puede haberse quitado a propósito al pasar al
   castillo); el tutorial nuevo está en curso (§11).
6. **Qué muestra `HandleChecksumMismatch`** al jugador con builds distintas.
7. **Consejos de la pantalla de carga**: no se ha revisado su lista.
8. **Categoría de volumen** de la música del menú principal y de los sonidos de asset (bailes, lanzar…): [`Docs/Menu_Pausa.md`](Menu_Pausa.md) dice que los de
   asset se quedan en Efectos por defecto; no se ha comprobado en el mezclador.

### 42.2 De los assets y de los valores

9. **Valores de los Blueprints y de los assets binarios**: `BP_CrabActor`, `BP_EnemySeagull`, `BP_SeagullSpawnZone`, `BP_ProcMapGameMode`, `DA_Biome_*`,
   `DA_ProcMapSettings` (guarda sus perfiles y puede diferir de `TN_MakeDefaultProcProfile`), los chunks del modo clásico. Las cifras del
   cooperativo y del clásico son las del C++ por defecto; un Blueprint puede haberlas cambiado. De `BP_TortugaCharacter`, las
   velocidades y la plancha están confirmadas por los registros; el resto (salto, cámara, sensibilidades…) se leyó del `.uasset` sin verlo en pantalla.
10. **Contenido de las ruedas**: los bailes y las frases están en assets de datos (`DA_EmoteWheelCatalog`, `DA_QuickChatWheelCatalog`); las
    probabilidades de los objetos suponen que `DT_Items` tiene solo las siete filas leídas (archivo de 27-04-2026, sin cambios en HEAD).
11. **`IMC_Player` en disco**: trae B / Círculo para el caparazón desde #637 (`Scripts/imc_player_shell_b.py`, comprobado por
    `Tortunabo.Settings.PlayerInputMapping`).
12. **Nombre visible de los objetos de siempre**: `FTN_InventoryItem` no lo tiene; los de las tablas son los de trabajo de los comentarios y los
    documentos. Cuántos objetos puede abrazar o llevar por un extremo cada malla concreta depende del tamaño de cada una.
13. **Clases del clásico**: el LDD describe erizos venenosos, arenas movedizas y cangrejo enterrado; en C++ solo hay `ATN_SlowZoneVolume` y el
    cangrejo dinámico. No consta si existen como Blueprints en los chunks.

### 42.3 De las mecánicas

14. **Cifras [calc] que dependen de la velocidad** (§13): la velocidad en sí (200 y 400 cm/s) está confirmada por los registros; los valores
    derivados (alcances, distancias por barra de estamina, duración de la ronda, tormenta…) son cálculos.
15. **Que encadenar salto y plancha supere a correr** con las velocidades del Blueprint (§17.5): la estimación da ~4,6 m/s frente a 4 m/s, sin medir en juego.
16. **Bucear**: no se ha encontrado ninguna mecánica; la ausencia está deducida del código (§18).
17. **La tinta no se ve** si el material del Blueprint es el de posproceso en blanco (§28.2).
18. **Que una bola de caparazón lanzada no derribe a otra tortuga de pie** (no hay código que lo haga; podría haber efecto por física).
19. **Que quien pone la concha trampa no se quede atrapado** al colocarla (el código no lo exime; depende de si el solapamiento se dispara al
    crear el actor).
20. **Persistencia del `RaceScore` en la carrera de la playa**: deducida (solo se guarda lo de la última ronda, §30.3).
21. **Quién oye a un espectador o a un fantasma**: el reenvío de voz usa el peón del oyente; lo que oye un espectador sin peón no está comprobado.
22. **Fila de jugadores de la cabecera del menú de pausa con 8**: [`Docs/Modo_Carrera.md`](Modo_Carrera.md) la deja pendiente («se sale con 8»); no se ha visto en pantalla.
23. **Pantalla del campeón con mando o teclado**: el código no define navegación propia; podría funcionar por el foco de UMG.
24. **Que un salto llano libre el mazazo del cangrejo** (`SlamHeight` 2,8 m frente a ~2,1 m del centro de la cápsula en el punto más alto, §36.3).

### 42.4 De la ronda 4 y del estado del árbol

25. **La ronda 4 está sin probar en juego por el usuario**: las llegadas con el huevo negro, el paso entre rondas, la música, los golpes del
    caparazón, el ojo de pez, Círculo/B y las gaviotas se han descrito desde el código y los documentos ([`Docs/Modo_Carrera.md`](Modo_Carrera.md),
    [`Docs/Sonido_Tortuga.md`](Sonido_Tortuga.md), [`Docs/Menu_Pausa.md`](Menu_Pausa.md)), no desde una partida. Buena parte del código de la carrera se escribió sin compilar y hay
    riesgos de compilación anotados en los documentos.
26. **Los números del nerf de las gaviotas con las velocidades reales** (§36.7): son un cálculo de este documento sobre las constantes de `TN_BeachGullTuning.h`
    y suponen que el blanco parte de los pies de la tortuga; hay que recalcularlos y probarlos.
27. **Rendimiento y ancho de banda** con 2, 4 y 8 jugadores (`stat net`), FPS en PIE y avisos de VSM ([`Docs/Modo_Carrera.md`](Modo_Carrera.md), «Cifras
    esperadas»).
28. **Lo que hay en el árbol de trabajo sin subir** (tutorial de primera partida, fase 2 de la localización, botón «Ajustes» del menú
    principal, `LVL_HQ.umap` y `M_CosmeticVertexColor.uasset` modificados): no está descrito salvo donde se dice «en curso».
29. **Fuentes CJK**: con la reserva del motor se ven, según las pruebas de cobertura de `fontTools`, pero no se ha mirado en pantalla; las Noto
    Sans previstas no están en el repositorio.

## 43. Mapa de archivos

Rutas relativas a `Source/Tortunabo/` (`Public/…` y `Private/…`) salvo las que empiezan por `Docs/`, `Config/`, `Scripts/`, `Tools/` o `Content/`, que lo son a la raíz del proyecto.

### 43.1 Flujo, salas, lobby, interfaz y ajustes

| Sistema | Archivos principales |
|---|---|
| Flujo y salas | `Multiplayer/MP_GameInstance.*`, `TN_RoomTypes.*`, `TN_RoomNames.*`, `TN_RoomInfo.*`; `UI/Menu/MP_MainMenuWidget.*`, `TN_RoomMenuWidget.*`; `Menu/MP_Menu*`; [`Docs/Salas.md`](Salas.md) |
| Lobby | `Lobby/TN_HQGameMode.*`, `TN_SandCastleLobby.*`, `TN_LobbyValley*.*`, `TN_GeneralBriefing.*`, `TN_LobbyMission.*`, `TN_ProcModeSelector.*`, `TN_TreasureChest.*`, `TN_ShopKeeper.*`, `TN_ChangingBooth.*`, `Lobby/Playground/`; [`Docs/Lobby_Castillo.md`](Lobby_Castillo.md), [`Docs/Tienda_Probador.md`](Tienda_Probador.md) |
| Modos y reglas | `Game/TN_RunGameMode.*`, `TN_ProcMapGameMode.*`, `TN_BeachRaceGameMode.*`, `TN_BeachRaceGameState.*`, `TN_BeachRoundSyncComponent.*`, `TN_TerrainViewGameMode.*`; `Core/TN_CoopGameState.*`, `TN_CoopPlayerState.*`, `TN_MatchFlowTypes.h`, `TN_ScoreDecisions.h` |
| Mapa procedural | `World/ProcMap/*`, [`Docs/Mapa_Procedural.md`](Mapa_Procedural.md), `Scripts/build_procmap_assets.py` |
| Playa de la carrera | `World/Beach/*` (`TN_BeachLayout.h`, `TN_BeachRaceGenerator*.*`, `TN_BeachTypes.h`…), [`Docs/Modo_Carrera.md`](Modo_Carrera.md), `Scripts/build_beach_race.py` |
| Ajustes y menú de pausa | `Settings/TN_GameSettingsSubsystem.*`, `TN_SettingsSaveGame.h`; `UI/Pause/TN_PauseMenuWidget.*`; [`Docs/Menu_Pausa.md`](Menu_Pausa.md) |
| Controles | `Player/MP_GamePlayerController.*`, `Player/TortugaCharacter.*`; `Content/Blueprints/Gameplay/Controls/IA_*`, `IMC_Player` |
| HUD | `UI/HUD/TN_RunHUDWidget.*`, `TN_CoopFlowHUDWidget.*`, `TN_PlayerHUDWidget.*`, `TN_GhostHUDWidget.*`, `TN_RadialWheelWidgetBase.*` |
| Pantallas de carrera | `UI/Race/*` |
| Carga | `UI/Loading/*`; [`Docs/Pantalla_Carga.md`](Pantalla_Carga.md) |
| Audio | `Audio/*`, `Private/Audio/TN_MatchMusic*.*`, `Player/TN_TurtleFoleyComponent.*`; [`Docs/Sonido_Tortuga.md`](Sonido_Tortuga.md) |
| Pruebas | `Private/Tests/*` (`Tortunabo.*`); [`Docs/Comandos_Prueba.md`](Comandos_Prueba.md) |

### 43.2 La tortuga, los objetos y los cosméticos

| Área | Archivos (relativos a `Source/Tortunabo/`) |
|---|---|
| Personaje | `Player/TortugaCharacter` (+ `_Cosmetics`, `_Dive`, `_Emote`, `_Interaction`, `_Knockdown`, `_Revive`), `Player/TortugaFirstPersonCharacter`, `Player/MP_GamePlayerController` |
| Movimiento y estamina | `Player/TN_TurtleMovementComponent`, `Player/TN_TurtleSurface`, `Player/TN_StaminaComponent` |
| Caparazón | `Player/TN_ShellComponent`, `Player/TN_ShellBody`, `Player/TN_ShellDecisions.h` |
| Cargar y lanzar | `Player/TN_CarryComponent` |
| Animación y cara | `Player/TN_TurtleAnimInstance`, `Player/TN_ProcAnimInstance`, `Player/TN_TurtleFaceComponent`, `Player/TN_DizzyBirdsComponent`, `Player/TN_TurtleDustComponent`, `UI/HUD/TN_HUDFaces.h` |
| Sonido | `Player/TN_TurtleFoleyComponent`, `Player/TN_TurtleFoleyDSP.h`, `Voice/ProximityVoiceComponent`, `UI/Voice/VoiceIndicatorWidget` |
| Fantasma | `Player/TN_SpectatorGhost`, `Player/TN_Ghost`, `Player/TN_GhostEgg`, `Player/TN_GhostCameraModifier`, `UI/HUD/TN_GhostHUDWidget`, `UI/HUD/TN_GhostHatchWidget` |
| Inventario | `Player/TN_InventoryComponent`, `Player/TN_InventoryDecisions.h`, `Core/TN_InventoryTypes.h`, `World/TN_PickupInteractableBase`, `World/TN_ThrowableItemActor`, `World/TN_ConchPickup`, `World/TN_InkProjectile`, `World/TN_BananaPeel`, `World/TN_UmbrellaInteractable`, `World/TN_TotemInteractable`, `World/TN_RescuePickup`, `World/TN_ItemSpawnZone`, `World/ProcMap/TN_ProcSearchSpot`, `Lobby/TN_TreasureChest`, `World/TN_PickupGlowComponent` |
| Objetos de carrera | `World/Beach/TN_RaceItems`, `TN_RaceItemComponent`, `TN_RaceItemActor`, `TN_RaceItemBox`, `TN_RacePelicanTaxi`, `TN_RaceHomingCrab`, `TN_RaceGullStrike`, `TN_RaceMine`, `TN_RaceFrisbee`, `TN_RaceStormCloud`, `TN_RaceBurstFX`, `TN_RaceItemSynth`, `TN_RaceItemArt`, `TN_RaceItemCommands`, `World/Beach/TN_BeachLoot`, `TN_BeachChest`, `TN_BeachLootShells` |
| Aturdimiento y muerte | `World/Beach/TN_BeachStun`, `World/Beach/TN_BeachStunComponent`, `Game/TN_RunGameMode`, `Game/TN_ProcMapGameMode`, `World/ProcMap/TN_ProcEggNest` |
| Puntuación | `World/TN_ScorePickup`, `World/TN_ScoreShells.h`, `World/TN_ScoreShellBurst`, `Audio/TN_ScoreShellSynthComponent`, `Core/TN_CoopPlayerState`, `Core/TN_CoopGameState`, `Core/TN_ScoreDecisions.h`, `Multiplayer/TN_CosmeticSaveGame`, `Multiplayer/MP_GameInstance`, `Game/TN_BeachRaceGameMode` |
| Cosméticos y tienda | `Core/TN_CosmeticLook`, `Core/TN_CosmeticsTypes.h`, `Lobby/TN_ShopKeeper`, `Lobby/TN_ChangingBooth`, `Lobby/TN_CosmeticPreview`, `UI/Shop/TN_ShopWidgets`, `Scripts/build_cosmetics.py`, `Scripts/cosmetics_meshes.py` |
| Emotes y chat | `UI/HUD/TN_EmoteWheelDataAsset`, `UI/HUD/TN_QuickChatWheelDataAsset`, `UI/HUD/TN_RadialWheelWidgetBase` |
| Datos (Content) | `Content/Blueprints/Characters/BP_TortugaCharacter`, `Content/Blueprints/Gameplay/Controls/IMC_Player`, `…/Items/DT_Items`, `…/Cosmetics/DT_Helmets`, `…/Cosmetics/DT_Skins`, `…/Data/DA_EmoteWheelCatalog`, `…/Data/DA_QuickChatWheelCatalog` |

### 43.3 El mundo

| Tema | Archivos (`Source/Tortunabo/Public/` y `Private/`) |
|---|---|
| Mapa procedural (lógica pura) | `World/ProcMap/TN_ProcMap{Math,Layout,Modules,Route,Path,Features,Terrain,TerrainDetail,Caves,Formations,Flora,Shells,Generate,Types,Enums}.h` |
| Mapa procedural (mundo) | `World/ProcMap/TN_ProcMapGenerator*.{h,cpp}`, `TN_ProcWaterActors.*`, `TN_ProcTraversalActors.*`, `TN_ProcPuzzleActors.*`, `TN_ProcEggNest.*`, `TN_ProcStartStructure.*`, `TN_ProcSearchSpot.*`, `TN_ProcFauna.*`, `TN_PathStorm.*`, `TN_StormCough.*`, mallas `TN_ProcMap*Meshes.h` |
| Cooperativo y clásico: enemigos y peligros | `World/TN_CrabActor.*`, `TN_CrabSpawnZone.*`, `TN_EnemySeagull.*`, `TN_SeagullSpawnZone.*`, `TN_SeagullDroppingActor.*`, `TN_DroppingSpawnZone.*`, `TN_SpawnZoneBase.*`, `TN_JellyfishActor.*`, `TN_BananaPeel.*`, `TN_SlowZoneVolume.*`, `TN_BreakablePlatform.*`, `TN_QuadActor.*`, `TN_StormVolume.*`, `TN_DeathZoneVolume.*`, `TN_ScriptedDeathZone.*`, `TN_EnemyDecisions.h`, `TN_ScoreShells.h`, `TN_ScorePickup.*` |
| Playa: tipos y reparto | `World/Beach/TN_BeachTypes.h`, `TN_BeachLayout.h`, `TN_BeachElement.*`, `TN_BeachRaceGenerator.*` (+ `_Build`, `_Features`, `_Start`, `_Scenery`, `_Round`, `_Debug`), `TN_BeachDecorField.*`, `TN_BeachDecor.*`, `TN_BeachPropMeshes.h`, `TN_BeachMilitaryMeshes.h`, `TN_BeachDecorKit.h` |
| Playa: elementos de juego | `World/Beach/TN_Beach{Trampoline,Catapult,SpadeRamp,MovingPlatform,WobblyPlatform,ShellGate,Seaweed,SandDungeon,Fortress,Chest,Loot,SearchMounds}.*`, `TN_BeachRideKit.h`, `TN_BeachBoostKit.h`, `TN_BeachSignKit.h`, `TN_BeachFortressKit.h`, `TN_BeachTrapKit.h`, `TN_BeachTrapCommon.*`, `TN_BeachTrapSynthComponent.*` |
| Playa: trampas | `World/Beach/TN_Beach{BarbedWire,BrokenBucket,ClamTrap,Mine,MineSynth}.*` |
| Playa: enemigos | `World/Beach/TN_BeachEnemy.*`, `TN_Beach{GiantCrab,SeaUrchin,Lizard,QuadLane,GullZone,HermitCrab,PoolOctopus,SandFleas,ToyTank,Storm,SandWorm}.*`, `TN_BeachGullTuning.h`,  `TN_BeachEnemyKit.h`, `TN_BeachEnemyMeshes.h`, `TN_BeachCritter*.{h,cpp}`, `TN_BeachCameraShake.*`, `TN_BeachStun.*`, `TN_BeachStunComponent.*` |

### 43.4 Archivos que añadió la ronda 4

| Tema | Archivos |
|---|---|
| Gaviotas (nerf y sujeción) | `World/Beach/TN_BeachGullTuning.h`, `TN_BeachGullZone.*`, `TN_RaceGullStrike.*`, `World/TN_SeagullDroppingActor.*`, `World/Beach/TN_BeachStun.*`, `TN_BeachEnemy.*`, `Player/TN_ShellComponent.*`; pruebas `Private/Tests/TN_BeachGullTuningTest.cpp` y `TN_BeachHoldTest.cpp` |
| Catapulta y ruedas del quad | `World/Beach/TN_BeachCatapult.*`, `Private/World/Beach/TN_BeachEnemyMeshes.h` |
| Llegada al agua y paso entre rondas | `UI/Race/TN_RaceArrivalWidget.*`, `Private/UI/Race/TN_RaceArrivalArt.h`, `UI/Race/TN_RaceRoundIntroWidget.*`, `UI/Race/TN_RaceScreens.*`, `UI/Race/TN_RaceCueSynthComponent.*`, `UI/HUD/TN_GhostHatchWidget.*`, `Game/TN_BeachRaceGameMode.*`, `Game/TN_BeachRaceGameState.*`, `World/Beach/TN_BeachFinishSplash.*` |
| Música de la carrera y golpes del caparazón | `Audio/TN_RaceMusicComponent.*`, `Private/Audio/TN_RaceMusicDSP.h`, `TN_RaceMusicDirector.h`, `TN_RaceMusicSubsystem.*`, `Player/TN_ShellImpactFXComponent.*`, `Audio/TN_ShellImpactSynth.*`, `Private/Audio/TN_ShellImpactDSP.h`; arnés `Tools/RaceMusic/` |
| Idioma, ojo de pez y Círculo/B | `Settings/TN_LanguageSettings.*`, `Settings/TN_GameSettingsSubsystem.*`, `Settings/TN_SettingsSaveGame.h`, `UI/HUD/TN_HUDFonts.*`, `Multiplayer/TN_RoomNames.*`, `Config/Localization/`, `Scripts/localization_gather_export.bat`, `Scripts/localization_import_compile.bat`, `Scripts/tools/check_font_coverage.py`, `Scripts/imc_player_shell_b.py`, `Tools/Localization/room_names_en.csv`; pruebas `Private/Tests/TN_LanguageTest.cpp` y `TN_RoomNamesTest.cpp` |

## 44. Documentos de referencia

Enlaces relativos a `Docs/`.

| Documento | Contenido |
|---|---|
| [`Docs/LDD_Tortunabo.md` (eliminado)](LDD_Tortunabo.md (eliminado)) | Diseño original (mayo 2026): módulos, tiers, enemigos, lobby, pilares |
| [`Docs/Mapa_Procedural.md`](Mapa_Procedural.md) | Generador por módulos, biomas, cruces, cuevas, conchas de puntos, modos, salida |
| [`Docs/Modo_Carrera.md`](Modo_Carrera.md) | Todo el modo carrera en la playa (más de 3400 líneas), con la ronda 4: llegada al agua, paso entre rondas, nerf de las gaviotas y torbellino |
| [`Docs/Salas.md`](Salas.md) | Salas públicas y privadas; idioma de los nombres y de los avisos |
| [`Docs/Menu_Pausa.md`](Menu_Pausa.md) | Menú de pausa y ajustes (idioma, ojo de pez, Círculo/B, botón «Ajustes» del menú principal en curso) |
| [`Docs/Localizacion.md`](Localizacion.md) | Idiomas, flujo de traducción, glosario, fuentes y auditoría de textos |
| [`Docs/Lobby_Castillo.md`](Lobby_Castillo.md) | Castillo, valle, tienda, general |
| [`Docs/Pantalla_Carga.md`](Pantalla_Carga.md) | El huevo |
| [`Docs/Tienda_Probador.md`](Tienda_Probador.md) | Cosméticos, tienda y probadores |
| [`Docs/Fantasma_Espectador.md`](Fantasma_Espectador.md) | Espectador, volver a la vida y la cáscara oscura en la carrera |
| [`Docs/Sonido_Tortuga.md`](Sonido_Tortuga.md) | Foley de la tortuga, música de fondo de la carrera («Marcha de la Playa») y golpes del caparazón |
| [`Docs/Animacion_Tortuga.md`](Animacion_Tortuga.md), [`Docs/Botin_Decorados.md`](Botin_Decorados.md) | Animación y plancha de la tortuga; botín de los decorados |
| [`Docs/Comandos_Prueba.md`](Comandos_Prueba.md) | Todos los comandos de consola |
| [`Docs/Plan_Carrera_Ronda3.md`](Plan_Carrera_Ronda3.md), `Docs/Plan_Carrera_Ronda4.md` (eliminado) | Planes de la carrera (la ronda 4 está hecha; quedan los puntos de «Pendiente de antes») |
| [`Docs/Tutorial.md`](Tutorial.md) | Tutorial de primera partida: recorrido por dos islas sobre el lobby (sin subir; §11) |
| [`Docs/Plan_Correccion_Fases.md`](Plan_Correccion_Fases.md), [`Docs/Traspaso_Sesion_Cloud.md`](Traspaso_Sesion_Cloud.md), [`Docs/Mapa_Procedural_Traspaso.md`](Mapa_Procedural_Traspaso.md), `Docs/Inventario_Scripts.md` (eliminado) | Históricos |
