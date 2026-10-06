# Plan maestro: modo único de Tortunabo

Fecha: 2026-10-06. Sustituye a `2026-09-29-Plan-Maestro-Modos-y-Mapas.md` y a `ROADMAP-macro-update.md`. Los dos siguen en la rama `chamber`.
Fuentes de diseño, por orden de prioridad:
1. Las decisiones del director (§1).
2. La hoja `Excel_DayT` del equipo de diseño: Stats, EnemyAndObstacleData, ObjectsData, Economía y Puntuación.
3. El documento de diseño del prototipo «mota», que resume la Biblia de Tortunavy; solo vale en lo que no contradiga lo anterior.

Lo que no figura en estas fuentes no entra en el juego.

## 0. Resumen

1. **Un solo modo de juego, sin nombre**, porque no hay otro del que distinguirlo. Las tortugas recorren un mapa hecho a mano sobre el terreno del algoritmo Camino, con vida, veneno, hidratación y estamina, enemigos que matan o noquean, y una economía de chapas.
2. **El terreno de partida es C01** (`C01_camino`, `LVL_Demo01`). Más adelante, diseño entregará el mapa dibujado en papel y se pasará a terreno con el mismo algoritmo. No hay módulos, ni intensidad, ni generación por partida.
3. **Rama `chamber`.** Todo lo demás queda en ella (copia de `dev` en `61cd791c7`), y sus issues se cierran como «not planned» con la etiqueta `chamber`:
   - los modos Rally, Karts, Carrera, Todos contra Todos, Coop y Supervivencia procedurales y Clásico;
   - los mapas procedurales, volumétricos, de países e inventados;
   - VR, primera persona, tutorial, cofres, conchas de puntos, el fantasma actual y los puzles que no figuran en el Excel.
4. **Enemigos, obstáculos y objetos: solo los del Excel**, más la chapa.

## 1. Decisiones del director (2026-10-06)

| # | Decisión |
|---|---|
| 1 | Un solo modo, sin nombre. Todos los demás modos van a `chamber` |
| 2 | Terreno con el algoritmo Camino. De momento se usa C01. Diseño entrega un dibujo, Claude lo pasa a terreno y monta el nivel a mano con las cajas manuales: hace de diseñador de nivel con lo disponible |
| 3 | No hay módulos ni intensidad. Las hojas Intensity e IntensityData del Excel no se aplican |
| 4 | Enemigos y obstáculos: solo los de EnemyAndObstacleData. Fuera los erizos que ruedan, los lagartos y todo lo demás |
| 5 | Objetos: solo los de ObjectsData, más la chapa. Fuera los que no estén en el Excel. Claude puede hacer las miniaturas de los objetos |
| 6 | **Chapa.** Objeto físico y apilable, que se coge, se lanza y se tira como otros. Sirve para comprar objetos en zonas concretas: se lanza en vertical, como un frisbee, y tiene que entrar por la ranura de una máquina expendedora. Solo vale dentro de la partida |
| 7 | No hay conchas de puntos. Las skins se compran con los puntos que se ganan al acabar la partida según el tiempo, la posición y otros factores. La fórmula está por definir; mientras tanto se usa la hoja Puntuación |
| 8 | Sin cofres. Hay cajas de suministros que caen (airdrop), cajas repartidas por el mapa para abrir y las propias chapas |
| 9 | Vida, veneno e hidratación, además de la estamina. Unos enemigos matan y otros noquean (columna «Tipo de muerte» del Excel) |
| 10 | Revivir con chapas (4 chapas, según la hoja Economía) **se suma** a la reanimación con baile, el rescate y el desangrado actuales |
| 11 | **Fantasma.** El actual se retira. Al morir se ve la cámara de otro jugador, también cuando ese jugador coge tu cadáver. Se hace en una issue propia |
| 12 | Puzles: no se crean nuevos y salen los que ya estaban hechos. Se quedan las placas de presión y los botones con sus gestores, y los elementos del Excel que hacen de puzle: plataforma rompible, puente y medusa trampolín. Los puzles van integrados en el mapa |
| 13 | **Lobby.** Se integra el de Álvaro (rama `BLockouts`) y se le aplican los sistemas de Mokius: zona de listos, tienda, probador y playground. Hasta entonces sigue el castillo actual, sin tutorial, sin cofre y sin selector de modo |
| 14 | Esqueleto: por ahora `TotugaDemo_Rig`. Los emotes se adaptan a él y más adelante se unificarán los esqueletos |
| 15 | Se conservan el doble salto, el caparazón, el ragdoll, coger y lanzar, nadar, los emotes y las frases rápidas |
| 16 | VR y primera persona fuera. El modo local (pantalla partida) se queda. El tutorial se va |
| 17 | La estamina desaparece de la interfaz. El estado del jugador se leerá de maneras más originales; Claude tiene permiso para idearlas |
| 18 | Las trincheras y los quads del Excel existen, pero no encajan en C01 por cómo se generó. Por ahora se queda C01 sin ellos |
| 19 | La programación de los objetos es de Rubi (#846, #847). Claude hace el recorte |
| 20 | La sombrilla se queda y protege de las gaviotas. Los huevos de salida se quedan |

## 2. Estadísticas de la tortuga (Excel, hoja Stats)

| | Velocidad | Altura del salto | Distancia del salto | Tiempo en el aire | Estamina | Panzazo |
|---|---|---|---|---|---|---|
| Andar | 2 m/s | 1,2 m | 1,98 m | 0,99 s | — | +2 m |
| Correr | 4 m/s | 1,2 m | 3,96 m | 0,99 s | 10 s | +2 m |

La tortuga mide 1,4 m. Hay que cuadrar estos valores con los de `TN_StaminaComponent` y `TN_TurtleMovementComponent`, en una issue de ajuste aparte.

## 3. Enemigos y obstáculos (Excel, EnemyAndObstacleData)

La columna «Base en el código» indica la clase que se adapta. El daño y el veneno no existen todavía: dependen de los vitales (F1).

| Elemento | Facción | Efecto (Excel) | Tipo de muerte | Base en el código |
|---|---|---|---|---|
| Anélido poliqueto | Aliado | Emerge al cazarlo y cura +25 | No letal | `ATN_ProcAnnelid` (hoy repone estamina) |
| Charco de pesca | Aliado | Se pesca para conseguir objetos | No letal | `ATN_FishingPool` |
| Gaviota 1 | Enemigo | Sombra en el suelo: coge al jugador, lo sube y lo suelta | Caída mortal | `ATN_BeachGullZone` (hoy lo suelta aturdido) |
| Gaviota 2 (caca) | Enemigo | Proyectil de área al entrar en el trigger | 30 de daño | `ATN_SeagullDroppingActor` |
| Cangrejo 1 | Enemigo | Patrulla de lado a lado, rápido | 20 por contacto | `ATN_CrabActor` |
| Cangrejo 2 | Enemigo | Persigue, engancha y arrastra a una zona de muerte | Mortal (arrastre) | `ATN_BeachDragCrab` |
| Medusa (cabeza) | Aliado | Trampolín | No letal | `ATN_JellyfishActor`, `ATN_BeachTrampoline` |
| Medusa (tentáculos) | Enemigo | Veneno al tocarlos | 5/s de veneno | Ídem, con veneno |
| Erizo | Enemigo | Pinchos que salen del suelo al pisar | 15 + veneno | `ATN_BeachUrchinSpikes` |
| Cangrejo 3 | Enemigo | Pinza que sale del suelo | 40 | `ATN_BeachBurrowCrab` |
| Arenas movedizas | Enemigo | Hunde al jugador | Mortal | `ATN_Quicksand` (hoy no mata) |
| Quad | Enemigo | Rueda gigante horizontal | Instantánea | `ATN_BeachQuadLane` (no encaja en C01) |
| Tormenta | Enemigo | Ralentiza un 40 % y quita visión | Desgaste 2/s | Se rehace parecida a la tormenta de bañistas de la Carrera (`ATN_BeachStorm` y su patada, que se conservan como base) |
| Algas | Neutral | Frenan un 25 %; se cortan de un golpe | — | `ATN_BeachSeaweed` |
| Basura | Neutral | Bloqueo ligero; se rompe de un golpe | — | `ATN_BeachTrashPile` |
| Plataformas rompibles | Neutral | Ceden en 1,5 s | Caída | `ATN_BreakablePlatform` |
| Tormenta de arena | Neutral | Reduce mucho la visibilidad; ráfagas de 8 m/s | — | `ATN_SandStorm` |
| Puente | Neutral | Durabilidad 100 | Caída si se rompe | `ATN_ProcBreakableBridge`, `ATN_WobblyBridge` |
| Agujero de trinchera | Neutral | Desnivel, cobertura | 10 por la caída | `ATN_BeachTrench` (no encaja en C01) |
| Búnker | Neutral | Zona segura | — | `ATN_BeachBunker` |
| Erizos checos | Neutral | Bloqueo | 15 al chocar | `ATN_BeachTankTrap` |

## 4. Objetos (Excel, ObjectsData y Economía)

| Objeto | Uso | Efecto | Peso | Usos | Apilado | Precio (chapas) | Base en el código |
|---|---|---|---|---|---|---|---|
| Medusa | Consumir | Estamina ilimitada 10 s; luego resta velocidad y envenena | 15 % | 1 | 2 | 2 | «Energía sin fin» (StaminaBoost) |
| Pez globo | Consumir | Protección ×2 5 s; luego marea | 15 % | 1 | 1 | — | `ETNCoopItem::PufferFish` |
| Barrita de algas | Consumir | Estamina ×2 5 s | 20 % | 1 | 2 | — | `SelfStaminaFull` |
| Alga antioxidante | Consumir | Cura el veneno | 10 % | 1 | 1 | 2 | No existe |
| Vendas | Consumir | Cura vida (a sí o a un aliado) | 10 % | 1 | 3 | 1 | No existe |
| Botiquín de playa | Consumir | Vida completa | 25 % | 1 | 2 | 4 | No existe |
| Coral | Lanzar (12 m) | Más vida ×2 5 s en área a aliados | 15 % | 1 | 1 | 4 | No existe |
| Conchas | Lanzar (10 m) | Aturden | 10 % | 1 | 3 | — | `ETNCoopItem::StunShell` |
| Tótem tortuga | Consumir | Salva de la muerte | 30 % | 1 | 1 | 2 | `Totem` |
| Cáscaras resbaladizas | Lanzar (8 m) | Desestabilizan a jugadores y enemigos | 15 % | 1 | 2 | — | `ETNCoopItem::SlipperyPeel` |
| Muñeco tortuga | — | Coleccionable; desbloquea logro | 20 % | — | 1 | — | `ATN_TurtleDoll` |
| Arpón | Usar (15 m) | Pesca objetos y rescata | 30 % | 15 | 1 | 5 | `ETNCoopItem::Harpoon` |
| Mochila | Usar | Almacena objetos | 40 % | Permanente | 1 | 10 | `UTN_InventoryComponent` |
| Biberón | Consumir | Hidratación ×2 | 25 % | Personalizado | 1 | 2 | No existe |
| Chapa | Lanzar (frisbee vertical) | Moneda de la partida; se mete por la ranura de una máquina | — | — | Apilable | — | No existe |

Economía (hoja Economía):
- Precio pivote 1.
- Cada máquina tiene 4 objetos a la venta.
- Factor de dificultad 1.
- Revivir cuesta 4 chapas.

Probabilidad de que cada fuente dé 1, 2 o 3 chapas:

| Fuente | 1 chapa | 2 chapas | 3 chapas |
|---|---|---|---|
| Caja de suministros | 0,25 | 0,07 | 0 |
| Airdrop | 0,7 | 0,3 | 0,15 |
| Exploración (por zona) | 0,8 | 0,5 | 0,2 |

Puntuación (hoja Puntuación): puntos al final de la partida por los muñecos recogidos, por lo recogido frente al total del nivel, por terminar la partida y por la eficiencia en los puzles. Hay además títulos: Saltarín (más saltos), Tesorero (más chapas) y Curandero (más jugadores curados).

## 5. Hoja de ruta

El campo **Fase** del tablero sigue estas fases. Desde el 06-10, las F0–F8 del plan del 29-09 ya no significan nada. Las horas se estimarán al desglosar cada fase.

| Fase | Contenido | Quién | Depende de |
|---|---|---|---|
| F0 | Recorte a modo único (lote #852), etiqueta `chamber` y guía (#853) | Claude (SkiTemplar) | — |
| F1 | Vitales: vida, veneno e hidratación replicados; daño y muerte o noqueo por enemigo; la estamina sin interfaz | Por asignar | F0 |
| F2 | Objetos del Excel (#846, #847) y sus miniaturas | Rubi; miniaturas, Claude | F1 |
| F3 | Economía: chapa física y apilable, máquina expendedora con ranura, airdrop, cajas por el mapa, revivir con 4 chapas | Por asignar | F1 |
| F4 | Enemigos y obstáculos del Excel con su daño, su veneno y su tipo de muerte | Por asignar | F1 |
| F5 | Nivel sobre C01, montado a mano con enemigos, cajas, máquinas y la decoración de la Carrera | Claude | F3, F4 |
| F6 | Lobby de Álvaro con los sistemas de Mokius; tienda de skins pagada con los puntos de final de partida | Por asignar | F0 |
| F7 | Muerte y espectador: ver la cámara de otro jugador; lectura del estado del jugador sin interfaz clásica | Por asignar | F1 |
| F8 | Cierre: Steam, localización, rendimiento, emotes sobre `TotugaDemo_Rig`, modo local | Por asignar | — |

## 6. Preguntas abiertas para el director

- La fórmula de los puntos de final de partida (tiempo, posición y lo de la hoja Puntuación).
- Qué objetos vende cada máquina y dónde se colocan.
- Cómo se lee el estado del jugador: Claude propondrá ideas antes de implementarlas.
