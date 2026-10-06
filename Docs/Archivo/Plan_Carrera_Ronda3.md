> **Obsoleto** (2026-10-02): las rondas del modo carrera se siguen desde `Docs/Modo_Carrera.md`, `Docs/superpowers/specs/modos/02-Carrera.md` y las issues del tablero.

# Plan de la ronda 3 del modo carrera (y ajustes comunes)

Planificado el 28-09-2026 con las notas del usuario tras probar la ronda 2. **Sin empezar**: se ejecuta cuando el
usuario diga «haz todo lo de la lista». Rama `claude/modo-carrera`; lo marcado como *común* se lleva después al
cooperativo (`claude/elegant-fermi-6n1hxy`), como el brillo, el menú de pausa y el fantasma.

## Decisiones tomadas

- **Unos 5000 elementos por ronda** (ahora ~1000–1100). Con un actor replicado por pieza, el servidor escucha y los
  clientes no aguantan, así que se cambia cómo se crea la ronda:
  - El decorado (≈80 %) deja de replicarse: el reparto ya es determinista con la semilla, así que cada máquina lo crea
    igual por su cuenta y lo agrupa en mallas instanciadas por tipo y variante. Colisión por instancia.
  - Solo se replican las piezas con estado (trampas, enemigos, cofres, conchas, objetos sueltos), con *dormancy* de red
    en las quietas y relevancia por distancia.
  - Los rebuscables pasan a ser ligeros: un registro por índice dentro de un gestor, con un actor que se crea solo
    cuando alguien se acerca. Así caben cientos.
  - Medir antes y después: tiempo de reparto, FPS en PIE con 2 jugadores, ancho de banda y avisos de VSM.
- **Salida libre** de 60 m → 15 m.
- **Dificultad** (la que elige el general, `SelectedProcDifficulty`) aplicada al reparto con perfiles:
  - Fácil: ayudas ×1,6 (trampolines, catapultas, fortalezas con premio, rebuscables, cofres), trampas ×0,7,
    enemigos ×0,6.
  - Normal: ×1 (la densidad nueva).
  - Difícil: enemigos ×2,5, trampas ×1,8 y también ayudas ×1,4. Está lleno de todo.
- **Catapultas de un solo uso**: quien la usa primero la dispara y el brazo queda partido; fastidia a quien llega tarde.
  Las potenciadas de las cimas de las fortalezas, igual.
- **Revivir en un huevo** sigue siendo solo del cooperativo (en la carrera nunca se muere).

## Tareas

1. **Salida de los huevos con 1 s de pausa** (carrera y cooperativo, *común*).
   - Al romperse la tapa, la tortuga se ve 1 s en el huevo (se pone de pie y se sacude la cáscara) y luego sale lanzada.
   - Carrera: `ATN_BeachRaceGenerator::OpenStartEggs` (`TN_BeachRaceGenerator_Start.cpp`).
   - Cooperativo: salida con huevos del mapa procedural (`TN.Proc.StartStyle 1`).
2. **Tormenta.**
   - Los objetos voladores y los bañistas aparecen y desaparecen con un fundido de opacidad y escala; nada de aparecer o
     desaparecer de golpe.
   - Todo va por el borde real de la tormenta (su frente), ni por delante ni por detrás.
   - Si estás dentro: patada (empujón en bola) que te deja 20 m por delante del frente; no se puede quedar nadie
     detrás. También si una gaviota te suelta dentro.
3. **Gaviotas.**
   - El picado se ve siempre entero: si falla, baja igual, pica en el sitio al que iba (arena que salta) y vuelve a
     subir.
   - El marcador es una sombra real bajo la gaviota, alineada con la tortuga objetivo (ahora sale desplazada), que encoge
     y se oscurece según baja.
4. **Cangrejos: mejor movimiento.**
   - Andan de lado con las patas alternas, giran suave, aceleran y frenan.
   - Embisten arrastrando arena.
   - Sin deslizarse ni atascarse.
5. **Lagartos con carácter**, tres tipos por semilla:
   - huidizo (el de ahora);
   - generoso: se va y deja un premio (objeto o concha);
   - mordedor: te muerde, te zarandea y te deja mareada ~1,5 s.
6. **Aturdir enemigos lanzándoles cosas.** Piedras, pulpos y los demás objetos lanzables que dan a un enemigo vivo
   (cangrejo, lagarto, erizo, gaviota en picado y los nuevos) lo marean un momento: pajaritos y sin atacar, como a las
   tortugas.
7. **Enemigos nuevos:**
   - Cangrejo ermitaño bola: se mete en su concha y rueda cuesta abajo por calles; derriba a las tortugas como bolos.
   - Pulpo de poza: vive en las pozas, agarra a quien nada y la lanza fuera, hacia atrás.
   - Enjambre de pulgas de arena: una nube que salta y, si te alcanza, te hace dar saltitos sin control y te marea.
   - Tanque de juguete teledirigido (motivo militar): patrulla un tramo y dispara bolitas de espuma que empujan y marean.
     Se le puede aturdir.
8. **Fortalezas de arena.**
   - Castillos inmensos de varios tamaños (mediano, grande y colosal), que sean verdaderas fortalezas:
     - rampas, escaleras y torres;
     - murallas con adarve, puertas y patio;
     - enemigos alrededor.
   - Subir compensa. Arriba hay:
     - conchas valiosas;
     - un cofre;
     - lanzadores potenciados: una catapulta o un trampolín que lanza mucho más lejos hacia delante que los normales.
9. **Cofres.**
   - Como el del lobby (`ATN_TreasureChest`), en las cimas de las fortalezas y en sitios especiales: tras una concha que
     atrapa, en rincones escondidos y en trincheras.
   - Tardan más en abrirse (~5–6 s) y dan los mejores objetos para avanzar, más conchas de puntos.
10. **Densidad ×5 y reparto.**
    - Unos 5000 elementos por ronda: todo lleno de estructuras, enemigos, rebuscables y trampas.
    - Salida libre de 15 m y perfiles de dificultad.
    - Tests del reparto al día.
11. **Rendimiento y red para 5000**: lo de «Decisiones».
12. **Comandos nuevos de prueba:**
    - `TN.Beach.Go <salida|fortaleza|trinchera|poza|cresta|sprint|acantilado|meta|metros>`: lleva allí a tu tortuga.
    - `TN.Race.Difficulty <Easy|Normal|Hard>` y `TN.Beach.Reroll [semilla]`: rehace la ronda con esa dificultad.
    - `TN.Beach.Chest`: cofre delante.
    - `TN.Beach.Lizard <huidizo|generoso|mordedor>`.
    - `TN.Beach.Storm.Here`: la tormenta encima, para probar la patada.
    - `TN.Beach.Egg`: repite la salida de los huevos.
    - `TN.Beach.StunNearest`: marea al enemigo más cercano.
    - Los enemigos nuevos, también con `TN.Beach.Place`.
    - Todo, apuntado en `Docs/Comandos_Prueba.md`.

## Orden y reparto de agentes

**Primero, el contrato (el agente principal).** En `ETNBeachElement` se añaden:
- fortalezas por tamaño;
- cofre;
- los cuatro enemigos nuevos;
- lanzadores potenciados como variante de la catapulta y del trampolín.

Además, los perfiles de dificultad y la interfaz del decorado local e instanciado y de los rebuscables ligeros.

**Agentes a la vez (6):**
1. **Red y rendimiento:** decorado local e instanciado, rebuscables ligeros, *dormancy* y medidas. Va primero en el
   tiempo, porque cambia cómo se crean los elementos; los demás programan contra el contrato.
2. **Reparto:** ×5, dificultad, salida de 15 m, fortalezas y cofres en el reparto, tests, `TN.Beach.Go`, `Reroll` y
   `Difficulty`.
3. **Enemigos existentes:** tormenta, gaviotas, cangrejos, lagartos y aturdirlos con objetos lanzados.
4. **Enemigos nuevos:** ermitaño, pulpo, pulgas y tanque.
5. **Fortalezas de arena:** lanzadores potenciados y catapulta de un solo uso.
6. **Cofres:** con la salida de los huevos de 1 s (carrera y cooperativo).

**Al acabar:**
- compilar;
- tests;
- capturas;
- medir rendimiento con 2 jugadores;
- commits por tema;
- lista de pruebas para el usuario;
- llevar lo *común* al cooperativo.
