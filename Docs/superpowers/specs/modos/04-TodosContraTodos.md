# Especificación: Todos contra Todos (TcT), «Batalla de los Puentes» y arenas

Fecha: 2026-09-29. Rama `macro-update`. Estado: especificación para implementar. Sin código escrito ni commit.

Fuentes leídas: `Docs/2026-09-29-Plan-Maestro-Modos-y-Mapas.md` (§3.4, §3.5, §3.6, §7), `Docs/Analisis/2026-09-29/D_modos_juego.md` (eliminado) (§0, §1) y `G_doble_salto_fisico.md`, `Docs/Catalogo-Mapas-2026-09-29.md` (§3, §4.3, §6 y decisión del director), `Docs/Modos-UI-FX-2026-09-29.md` (§2.2 TcT), `Art/Library/IA/INDEX.md`. Cabeceras de código: `TortugaCharacter.h`, `TN_TurtleMovementComponent.h`, `TN_CarryComponent.h`, `TN_ThrowableItemActor.h`, `TN_InkProjectile.h`, `TN_BananaPeel.h`. `Catalogo-Puzzles-2026-09-29.md` no tiene filas legibles por patrón; ninguna plantilla de puzzle se usa como trampa en el MVP (el géiser orientable queda como opción posterior, §3.6).

Convenciones. **[1.ª pasada]** = hipótesis a validar en playtest. **[existe]** = valor leído en código. **[est. propia]** = horas sin medir. Estados: **MVP** / **Fase B** / **Después** / **Cortar**. Términos como en el plan: meseta, nido, caparazón, concha, baja.

## 0. Contradicciones y decisiones que necesitan al director

| # | Hallazgo | Propuesta en esta especificación |
|---|---|---|
| K1 | El plan §3.6 cuenta **7 iconos nuevos imprescindibles** para TcT. Existen ya 6 borradores IA de objetos (`Art/Library/IA/todos_contra_todos`, 5 carpetas, 6 mallas de objeto) y la malla `Piedra1`. | Los iconos se **renderizan** desde esas mallas con `render_preview.py`. Nuevos imprescindibles de TcT = **0** (§11). Rehacer los borradores en el equipo de arte es *deseable*. El plan debe bajar de 11 a 4 el total (Rally 1, Steam 3). |
| K2 | Plan §3.5: rebote con restitución 0,45 y 60 % tangencial. El código actual [existe]: `BellyBounceRestitution` 0,35 y `BellyBounceTangentKeep` 0,75. | Se conservan los del plan solo para el rebote **en vuelo**; el rebote de suelo no cambia. Confirmar. |
| K3 | El plan §3.4 no lista «sobrevivir sin caer +1» (D §1.2 sí). | Entra como **+1 opcional** (`bSurvivalPoint`, desactivado por defecto). Se decide tras el playtest. |
| K4 | Catálogo A01: los nidos están en r = 25 (anillo 3), que se hunde a los 105 s. | Tras hundirse un anillo, los nidos pasan a los anillos que quedan (§3.4). Corregir el catálogo. |
| K5 | Catálogo marca A03 y A06 como «Later/Cut?»; el director las nombra todas. | Se especifican las seis, en fases distintas (§3.3). A06 queda en «Después». |
| K6 | D §1.2 y plan: «muerte por entorno = +1 a la líder». Con hundimiento, la caída por hundimiento no es entorno hostil. | El hundimiento **no** cuenta como agresor: solo hay atribución si hay contacto en 6 s; si no, 0 puntos. Gaviota, cangrejo y aguja sí dan +1 a la líder. |
| K7 | Filipinas: el catálogo la llama I01 «rara», solo TcT; el director dice «adaptada». | Se usa I01 tal cual el catálogo; la «adaptación» es el ajuste de tamaño y cotas de §3.5. |

## 1. Fantasía, jugadores, rondas, puntuación, atribución y reaparición

**Fantasía.** Fiesta de empujones tipo Gang Beasts con física a medias: nadie muere de verdad, todo el mundo cae al agua y se ríe. Cruzas un puente de 70 m, ves a alguien de frente y eliges: esconderte en el caparazón (no te noquean, pero te pueden agarrar y lanzar) o embestir con el doble salto. **Experiencia buscada:** el golpe se entiende al instante (quién, con qué, por qué caí) y la venganza siempre está a un objeto de distancia.

**Cómo se enseña.** Sin tutorial de texto. La primera ronda de cada sesión arranca con un banner de 5 s con tres iconos (embestir, caparazón, agarrar) y cada objeto muestra su icono más una línea de 3 palabras al recogerlo la primera vez (ver §6). La pantalla de caída dice quién te tiró.

### 1.1 Jugadores y formato

| Parámetro | Valor | Razón |
|---|---|---|
| Jugadores | 2 a 8 | Decisión del plan |
| Partida | A 3 conchas (`ETNBeachRacePhase`, `RaceShellHalves`) | Reutiliza el final de la Carrera |
| Ronda | 120 s (90 s con 2–3 jugadores) [1.ª pasada] | D §1.8 |
| Cuenta atrás | 5 s, saliendo del huevo | Como la Carrera |
| Ganadora de ronda | Más puntos: 1 concha; con 4+ jugadores la segunda se lleva media concha | D §1.1 |
| Empate en cabeza | Muerte súbita de 45 s solo entre empatadas: sin reaparición, cada 8 s cae una bola de 1 punto sobre el centro | D §1.1 |
| Tope de rondas | 7. Si nadie llega a 3 conchas, gana quien más tenga; si empatan, una ronda extra de muerte súbita | Evita partidas eternas con 8 jugadores |
| Duración esperada | 10–12 min (4–5 rondas) | D §1.1 |
| Variante corta | 2 conchas | Opción del lobby |

### 1.2 Puntuación

| Evento | Puntos | Notas |
|---|---|---|
| Rival cae al agua y tu último contacto fue hace < 6 s | +1 a quien hizo el último contacto | Un solo punto por baja, aunque haya varios contactos |
| Caída sin contacto en 6 s (incluye hundimiento del suelo) | 0 y reaparición lenta (8 s) | No se resta: restar frustra al que va perdiendo |
| Gaviota, cangrejo o aguja te tiran | +1 a la líder actual | Anti «dejar que el entorno lo haga». Con empate en cabeza, +1 a todas las empatadas |
| Caída por tu propio dive, viento o rebote sin contacto de rival | 0 | Igual que sin contacto |
| Sobrevivir toda la ronda sin caer | +1 (`bSurvivalPoint`, apagado por defecto) | K3 |
| Muerte súbita: última en pie | Gana la ronda | Sin puntos extra |

### 1.3 Atribución (`LastInstigator`)

Nuevos campos en `ATortugaCharacter` (no existen hoy): `LastInstigator` (`TWeakObjectPtr<APlayerState>`), `LastInstigatorTime` (tiempo de servidor) y `LastInstigatorCause` (enum: `Push`, `Item`, `Thrown`, `Splat`, `Hook`, `Bump`). Solo el servidor los escribe; no se replican (el resultado se comunica por el evento de baja).

| Contacto | Escribe `LastInstigator` | Ventana |
|---|---|---|
| Golpe de pala, dardo, piedra o balón que impacta | Quien disparó o lanzó | Se renueva 6 s |
| Bola (`ATN_ShellBody`) de X choca con Y a ≥ 300 cm/s | X | 6 s |
| Y agarrada y lanzada por X | X, y **se mantiene** mientras vuele y ruede (la ventana empieza al aterrizar o al detenerse la bola) | 6 s desde que toca suelo |
| Dive de X que toca a Y (cápsula con cápsula, vel. relativa ≥ 300) | X | 6 s |
| Estampado de Y contra pared tras empujón o lanzamiento de X | X (heredado) | 6 s |
| Garfio de X tira de Y | X | 6 s |
| Sacar de la mano a Y con la trampa de X (piel de plátano) | El dueño de la piel | 6 s |

Reglas: un contacto nuevo sustituye al anterior (gana el más reciente); un contacto de una jugadora **caída o en reaparición** no cuenta; si X se cae antes que Y, el punto se pierde para X pero Y sigue siendo atribuible a X (el punto es de X aunque X ya haya reaparecido); auto-contactos no cuentan. Tests: <6 s puntúa, ≥ 6 s no; lanzamiento largo puntúa al lanzador; el relevo de instigador; caída de la propia X = 0.

### 1.4 Reaparición

| Regla | Valor |
|---|---|
| Espera tras caer (con agresor) | 4 s: 1,5 s de repetición de la caída + huevo (`UTN_GhostHatchWidget`) |
| Espera sin agresor | 8 s |
| Lugar | Nido de su meseta; si hay rival a < 15 m, el nido libre más alejado de rivales; nunca sobre un sector hundido o en aviso de hundimiento |
| Inmunidad | 2 s (`ReviveImmunitySeconds`, `TN_RunGameMode.h:145`); no puede recibir noqueo ni agarre; parpadeo del cuerpo |
| Al caer | Suelta lo que carga y el objeto de la mano; conserva la ranura de reserva |
| Anti-encierro | Nunca dos reapariciones en el mismo nido en 3 s |
| Caída al agua | Baja con reaparición, no «muerte real con cuerpo y rescate» (D §0 C1) |
| Puntos | La baja no resta puntos a la caída |

## 2. Flujo de una partida

| Paso | Qué ocurre | Servidor / cliente |
|---|---|---|
| 1. Lobby | Se elige modo TcT y mapa (P01, arena, isla, país). Número de conchas | Servidor decide `ETNProcGameMode::Bridges` o GameMode propio |
| 2. Carga | Se generan los marcadores (nidos, cajas, viento, hundimiento) desde el mapa preparado | Servidor |
| 3. Cuenta atrás | 5 s, tortugas saliendo del huevo, inmovilizadas | Servidor fija hora de inicio |
| 4. Ronda | 120 s (90 s con 2–3). Puntos, objetos, hundimiento y viento por reloj del servidor | Servidor autoritativo |
| 5. Fin de ronda | Marcador `UTN_RaceTallyWidget` (por puntos), concha para la ganadora (media para la segunda con 4+) | Servidor |
| 6. ¿Alguien con 3 conchas? | Sí: campeona (`UTN_RaceChampionWidget`); no: siguiente ronda (mapa igual; arenas sin cambiar) | Servidor |
| 7. Empate en cabeza | Muerte súbita 45 s | Servidor |
| 8. Final | Podio y vuelta al lobby | Servidor |

**Casos límite.**

| Caso | Regla |
|---|---|
| Alguien se desconecta en ronda | Su tortuga se retira; su puntuación queda en el marcador; si quedan 1 jugadora, la ronda termina y gana ella |
| Entra alguien tarde (*late-join*) | Espectadora hasta la siguiente ronda (fantasma con cámara aérea fija) |
| Todas caen a la vez | Gana la de más puntos; si empatan, muerte súbita |
| Sólo 2 jugadoras | Ronda de 90 s, 5 mesetas activas en P01 (D §1.8) |
| Servidor anfitrión abandona | Migración no soportada: fin de partida (como el resto de modos) |

## 3. Mapas y reglas de arena

Regla común: agua de muerte a cota 0 m; una caída es una baja. Los tamaños se validan con el tiempo de cruce (objetivo: encuentro cada 15–20 s), dato **no medido** en el catálogo. Todas las mallas salen del pipeline volumétrico existente; **0 mallas nuevas por mapa**.

### 3.1 Mapa base: P01 «Batalla de los Puentes» (MVP)

Mapa del director: mesetas +20/+25/+30 sobre un río a 0 m, unidas por puentes colgantes.

| Elemento | Cantidad | Origen |
|---|---|---|
| Nidos | 8 (uno por meseta, salvo la +30 del cuello) | Marcadores en `LVL_Bridges_P01` |
| Cajas de objetos | 8 (una por meseta a 8–10 m del borde); reaparecen a los 20 s | Marcadores |
| Cofre | 1, en el cuello (+30), mejor botín | Marcador |
| Cobertura | 15 % de cada meseta: sacos, erizos, castillos pequeños | Assets existentes |
| Rampas de tierra | 2 de 20° (mesetas altas) | Marcadores |
| Catapultas / trampolines | 2 / 3 (aterrizaje siempre en meseta) | Actores existentes (`TN_BeachCatapult`) |
| Gaviotas | 2–3; suelta ajustada a tablero o meseta (~70 %) | `ATN_EnemySeagull` |
| Cangrejos | 1–2 por meseta grande | `TN_CrabActor` |
| Zona de muerte | Caja de agua y volumen de «fuera de mapa» de 50 m | Existente |

Escala por jugadores (D §1.8):

| Jugadoras | Mesetas activas | Cierre | Cajas | Ronda |
|---|---|---|---|---|
| 2–3 | 5 (cuello y 4 centrales) | 4 puentes con barricada | 5 | 90 s |
| 4–5 | 7 | 2 con barricada | 7 | 120 s |
| 6–8 | 9 | ninguno | 9 | 120 s |

### 3.2 Reglas de arena (actores comunes)

| Regla | Actor | Parámetros [1.ª pasada] | Autoridad y réplica | Estado |
|---|---|---|---|---|
| Hundimiento | `ATN_SinkingPlatform` (nuevo, código) | Aviso 5 s (temblor, agua turbia, silbido); baja 5 m en 5 s (mín. −3 m bajo el agua); la jugadora encima cae con la plataforma y nada = baja | El servidor guarda `SinkStartServerTime` y `SinkDuration`; la réplica es un `struct` de 2 floats y un enum `Idle/Warning/Sinking/Sunk`. Los clientes interpolan la cota con la hora de servidor. **Colisión decidida por el servidor** | MVP |
| Viento | `ATN_WindGust` (nuevo, código) | Cada 25–40 s, un puente (o sector) al azar; 4 s; empuje horizontal 250 cm/s² [1.ª pasada]; aviso 1,5 s (borde amarillo, silbido, polvo). Vence al caminar, no a la bola pesada; bola ligera se desvía la mitad | Estado (dirección, zona, hora de inicio) replicado por actor; la aceleración se suma en `UTN_TurtleMovementComponent` en dueño y servidor (misma función), para evitar correcciones | MVP |
| Barricadas | Sacos o puerta de conchas (existentes) | Puentes cerrados según jugadoras (§3.1) | Estado inicial de nivel | MVP |
| Corte de puentes | `ATN_BridgeSpan` (P4 del plan) sobre el puente de Mokius | Corte a los 60 y 90 s (los de fuera primero); aviso 3 s; el puente se sacude y cae en 1,2 s; nunca se corta con jugadoras encima sin aviso de 3 s. Los puentes **naturales** (tallados en el terreno) no se cortan | Enum `Intact/Warning/Falling/Broken` + hora; colisión desactivada en servidor y cliente al pasar a `Falling` | Fase B |
| Aguja giratoria | `ATN_SweepBar` (nuevo, código) | Barrera baja de 0,8 m; noquea a 2,0 s; hora 0,5 rev/min y minuto 2 rev/min; la jugadora debe saltarla | Ángulo = f(hora de servidor), sin replicar por tick | Después (A04) |
| Zona de nido bloqueado | Marcador | Un nido no es válido si su sector está en aviso o hundido | Servidor | MVP |

### 3.3 Arenas abstractas

Cotas del catálogo §3 y §4.3. Diámetro o lado ≤ 180 m.

| id | Arena | Forma y medidas | Reglas propias | Nidos | Fase |
|---|---|---|---|---|---|
| A01 | **Diana** | 5 anillos, r 0-6 (+12), 6-18 (+9), 18-32 (+6), 32-46 (+4), 46-60 (+2); 4 rampas radiales de 15° (θ 0, 90, 180, 270), ancho 6 m; 2 catapultas y 3 trampolines | Anillo 5 se hunde a 40 s, anillo 4 a 80 s, anillo 3 a 105 s. Centro = ventaja de tiro y objetivo. Regla de seguridad: las rampas y el anillo 3 existen hasta el final | 8 en r = 25 (anillo 3) → tras 105 s: 8 en r = 12 (anillo 2) | **MVP (piloto)** |
| A02 | Dónut | Anillo r 25–65, +4; 4 puentes de 3 m sobre el hueco; 2 catapultas y 3 trampolines | 4 sectores de 90° se hunden, uno cada 30 s (orden por semilla) | 8 en r = 45, solo en sectores en pie | Fase B |
| A05 | Tablero | 5 × 5 mesetas de 20 × 20 m, separación 9 m, cotas {+3, +6, +9} (9/9/7), diferencia ≤ 3 m entre vecinas | Cada 30 s una fila baja 3 m (semilla). Las bajas son fáciles de tomar | 8 en las mesetas que no bajan | Fase B |
| A03 | Espiral | 2,5 vueltas, r 10–50, 0 a +14 m, 1,7°, ancho 12 m; 2 catapultas y 3 trampolines | La última vuelta se hunde a 60 s. Caer hacia el interior = baja | 8 en la base (vueltas 1–2) | Después |
| A04 | Reloj | Disco r 60, +4, 12 pilares; 3 trampolines, catapulta central | Sin hundimiento; 2 agujas (§3.2) | 8 en r = 45 | Después |
| A06 | Panal | 19 hexágonos de lado 12, separación 2 m, central +6 con el cofre | Se hunde un anillo cada 30 s, del exterior al centro | 8 en el anillo 2 | Después (posible corte) |

Nota de balance: hundimiento estilo A01 con 8 jugadoras deja de 3 a 4 jugadoras cerca del centro a los 105 s; es intencionado (pico de caos final) pero exige la regla de nido de K4 y el aviso de 5 s.

### 3.4 Islas y países en variante TcT

| id | Mapa | Rasgos TcT | Fase |
|---|---|---|---|
| I01 | **Filipinas adaptada** («Archipiélago de las Mil Bolas») | Isla mayor elipse 90 × 60 (cono de 19,6°) y 6 más pequeñas sobre un anillo de r 85; 9 puentes colgantes de 25–45 m, 2 catapultas, 3 trampolines; se hunde una isla pequeña cada 30 s (3 en total); a los 90 s el volcán «erupciona»: 3 gaviotas con objetos. Adaptación: puentes naturales entre islas medianas; nidos en las 3 islas medianas y la mayor | **MVP (piloto)** |
| I02 | Galápagos | 5 islas, mesetas «caparazón», 2 cangrejos rojos; sin hundimiento | Después |
| I03-T | Tortuga Magna | 13 escamas hexagonales a +3/+6/+9; las exteriores se hunden a 60 s, las interiores a 100 s | Después |
| I05 | Santorini | Caldera con 2 puentes naturales; sin hundimiento | Después |
| L05 | Venecia | 6 islotes con 9 puentes, viento cada 40 s hacia el canal | Fase B |
| L06-T, L09-T, L11-T, L12-T | Variantes TcT de idioma | Ver catálogo §4.3 | Después |
| Países | 12 países miniaturizados **en variante TcT** (decisión del director): zona llana de 90–180 m elegida con criterio de juego dentro del país, con relieve del MDE y puentes naturales; borde = costa real o frontera resuelta como muro de roca (sin rótulos políticos) | Después; cada país lo produce `gen_terrain_country.py` (en curso, sin comprometer) |

### 3.5 Reglas de puentes

| Tipo | Se puede cortar | Uso en TcT | Aviso |
|---|---|---|---|
| Colgante de Mokius (asset con anclajes) | Sí (`ATN_BridgeSpan`) | Corte por tiempo y «último en pie» | 3 s + sacudida |
| Natural del director (tallado en el terreno) | No | Zonas estrechas de emboscada | — |
| Mixto | Solo los de Mokius | P01 y países | — |

Regla de oro: **nunca** se rompe con jugadoras encima sin aviso de 3 s. El puente no se balancea ni se rompe por peso (recortado en D §1.4).

## 4. Movimiento y combate

Base del director: doble salto (dive), noqueo, caparazón, agarrar y lanzar (**solo** a quien está en caparazón). Todos los valores «existente» están leídos en la cabecera indicada; los marcados «nuevo» son de esta especificación.

### 4.1 Tabla de acciones

| Acción | Entrada | Reglas y números | Origen del valor | Fallo / borde |
|---|---|---|---|---|
| Caminar y saltar | Base | Salto de 1,2 m; no sube paredes de 5 m | Existente | Muros de mesetas: rampa, catapulta o trampolín |
| Doble salto (dive) | Saltar en el aire | `DiveForwardSpeed` 420 cm/s + inercia (`DiveMomentumForwardFactor` 1,0), tope `DiveMaxTotalSpeed` 1500; `DiveDownwardSpeed` 200; bloqueo mínimo 0,65 s; máx. 12 s | `TortugaCharacter.h:165-236` | En el aire; nada más |
| Deslizar en el vientre | Tras el dive | Rozamiento en arena 800; frena en < 0,8 s en llano; **desde pendiente de 12° sigue cayendo** (rozamiento ×0,3, freno ×0,4, pausa del temporizador; tope 6 s) | `TN_TurtleMovementComponent.h:153`; nuevo (G §2.2) | Sobre agua: 220 |
| Caparazón (Ctrl) | Ctrl | Entra y sale a voluntad; no noqueable por golpe de pala, dardo o piedra (inmune); **agarrable** | `TN_ShellComponent`; regla del director | Sin barra de aguante; salir cuesta 0,3 s |
| Bola (caparazón en movimiento) | Ctrl + impulso | `StartBody(v)`: pasa a `ATN_ShellBody` (física replicada); empuja a otras; derriba si v ≥ 600 (`MinKnockdownSpeed`) | Existente | Bajo 600, solo empuja |
| Agarrar | Interactuar sobre una tortuga **en caparazón** | Alcance 1,5 m; la agarrada puede escapar machacando salto durante `SecondsToEscape` = 2 s; no se agarra a quien está en inmunidad de reaparición | `TN_CarryComponent.h:133`; regla del director | Sobre una noqueada: no (decisión 5 del director: solo en caparazón) |
| Lanzar | Soltar el agarre | `ThrowSpeed` 1500 cm/s; con dive `DiveThrowCarryFactor` 0,6 y `DiveThrowJumpFactor` 0,5, tope 2300 cm/s; alcance 17,6 m (v = 15 m/s, 25°) | `TN_CarryComponent.h:104-129`; D §1.10 | La lanzada entra en bola (`StartBody`) |
| Noqueo | Golpe | `ApplyKnockdown(Duration)` en servidor; mínimo 2,2 s (`MinKnockdownSeconds`); ragdoll cosmético; **posición fijada por el servidor** (`ServerFreezeRagdoll` ampliado al noqueo, plan §3.5 punto 5) | `TortugaCharacter.h:1061, 1368` | Ver anti-encadenado |
| Anti-encadenado | Tras levantarse | 1,5 s de inmunidad al noqueo (empujones sí actúan) [nuevo, 1.ª pasada] | Nuevo | Sin esto, 3 jugadoras con pala pueden dejar a una en bucle |
| Estampado | Impacto en vuelo o rodando | Vel. normal ≥ 650 cm/s con `N.Z < 0,35`: se corta el dive, entra en bola con velocidad reflejada, restitución 0,45, 60 % tangencial, `Multicast_DiveSplatFX`; entre 400 y 650: rebote; en pendiente o si el objeto se aleja: nada | Plan §3.5, G §2.3 | El estampado no noquea; la bola puede acabar en el agua (deseado) |
| Empujón por bola | Choque | Impulso = 0,8 × velocidad relativa de la bola sobre el objetivo; noquea solo con v ≥ 600 | Nuevo (1.ª pasada) | v < 600: solo desplaza |
| Escapar de la bola | Salto | 0,5 s de cooldown tras aterrizar | Nuevo | — |

### 4.2 Números de referencia y fuerzas

| Concepto | Valor | Origen |
|---|---|---|
| Velocidad mínima de noqueo por golpe físico | 600 cm/s | `MinKnockdownSpeed` (`TN_ThrowableItemActor.h:99`) |
| Duración del noqueo | 2,2 s (piedra, dardo), 2,5 s (pala, más cerca), 2,0 s (aguja) | `MinKnockdownSeconds` 2,2; el resto nuevo |
| Velocidad de la piedra lanzada | 1200–1800 cm/s | `ThrowSpeed` de `TN_InventoryTypes.h:92, 133` |
| Proyectil de tinta | 1200 cm/s, radio 20, 4 s | `TN_InkProjectile.h:71-81` |
| Dardo de medusa | 900 cm/s (lento y visible), radio 20, 3 s | Nuevo (1.ª pasada) |
| Empuje del viento | 250 cm/s² | D §1.4 |

### 4.3 Estampado contra la pared con la bola física

1. La cápsula (o la bola) con velocidad normal ≥ 650 cm/s contra una superficie de `N.Z < 0,35` marca `PendingSplat`.
2. `TickDive` en servidor llama a `ServerDiveSplat`: el árbitro confirma, se acaba el dive, `ForceEnterShell(false)`, `StartBody(v reflejada)` y `Multicast_DiveSplatFX` (polvo, pajaritos, aplastado en código).
3. La bola resultante es física replicada y **la posición la dicta el servidor** (±5 cm en 4 máquinas).
4. La atribución se conserva: si la estampada venía de un empujón, el punto es del empujador.

No se crean actores dentro de `ServerMove`.

### 4.4 Coste y dependencias

El combate depende del plan §3.5: predicción del dive (obligatoria por la decisión 7), pendiente, rebote en vuelo, estampado y noqueo autoritativo. Sin el noqueo autoritativo **no hay dardo ni pala** (§5).

## 5. Objetos y armas

Reglas comunes: se recoge de cajas y del cofre; una ranura activa más una de reserva; se elige con `RollLoot` con `Norm` por puntos (líder = 1, colista = 0); al caer, se pierde el de la mano. Las «pistolas» disparan proyectiles con el patrón de `ATN_ThrowableItemActor` (impacto decidido en el servidor, `TN_ThrowableItemActor.cpp:270`; trayectoria local desde `Multicast_InitializeThrow`; punto final por `Multicast_BallStopped`), sin *hitscan* ni ragdoll nuevo.

Regla de assets: cada objeto tiene **0 o 1** asset. Estados: EXISTENTE (en Content), REUTILIZADO (malla o actor ya existente en otro uso), BORRADOR IA (FBX en `Art/Library/IA`, no importado), NUEVO (a crear).

| # | Objeto | Efecto y números | Autoridad y réplica | Asset (0–1) | Estado del asset | Peso de loot (líder / colista) | h | Fase |
|---|---|---|---|---|---|---|---|---|
| 1 | **Piedra** | Lanzable; noquea si v ≥ 600 (2,2 s). Recarga: 1 uso | `ATN_ThrowableItemActor` sin cambios; un único pickup por lanzamiento | `Piedra1.uasset` | EXISTENTE | 3 / 2 | 2–3 | MVP |
| 2 | **Balón de playa** | Rebota mucho; empuja sin noquear (`PushImpulse` 900 cm/s) | Flag `bKnockdownOnHit=false`; `LaunchCharacter` en servidor y dueño (patrón `TN_BeachClamTrap.cpp:871-886`) | `SM_TN_BalonPlaya` (212 tris) | BORRADOR IA | 2 / 2 | 3–4 | MVP |
| 3 | **Pistola de tinta** | Ráfaga de 3 tintas (1200 cm/s) que ciega 3 s; sin noqueo | `ATN_InkProjectile` + `MulticastApplyInkEffect` (`TN_InkProjectile.h:111`) | `SM_TN_PistolaTinta` (434 tris) | BORRADOR IA | 2 / 3 | 2–3 | MVP |
| 4 | **Pala de mano** | Golpe cuerpo a cuerpo a < 1,5 m, cono de 90°; noquea 2,5 s; 3 golpes | Barrido en servidor + `ApplyKnockdown`; traza de visión: nunca a través de pared | `SM_TN_PalaMano` (336 tris) | BORRADOR IA | 2 / 3 | 4–6 | MVP (depende del noqueo autoritativo) |
| 5 | **Piel de plátano** | Trampa en el suelo: pisada = noqueo 2,0 s; se consume | `ATN_BananaPeel` (existe, `KnockdownDuration` 2,0); el dueño registra `LastInstigator` | `ATN_BananaPeel` (malla propia) | EXISTENTE | 3 / 3 | 1–2 | MVP |
| 6 | **Pistola de noqueo (dardo de medusa)** | Proyectil lento y visible (900 cm/s) que noquea 2,2 s; 3 disparos; ragdoll solo cosmético | `ApplyKnockdown` en servidor; **depende** del noqueo autoritativo (plan §3.5) | `SM_TN_PistolaNoqueo` + `SM_TN_DardoMedusa` (780 tris; una sola entrada de catálogo) | BORRADOR IA | 1 / 3 | 4–6 | Fase B |
| 7 | **Trabuco de aire** | Cono corto de 3 m: mete a la rival en caparazón y la lanza como bola (v = 1500) | Servidor: `TNBeach::ResolveMover`, `ForceEnterShell(false)` + `StartBody(v)`; se replica `ATN_ShellBody` | `SM_TN_TrabucoAire` (750 tris) | BORRADOR IA | 1 / 3 | 5–7 | Fase B |
| 8 | **Garfio** | Alcance 12 m: atrae a la rival; si está en caparazón, la deja a tiro de agarre | Servidor valida alcance y visión; `LaunchCharacter` (servidor y dueño) o `StartBody` en caparazón | `SM_TN_Garfio` + `SM_TN_Ancla` (706 tris) | BORRADOR IA | 1 / 2 | 8–12 | Fase B (riesgo: cable y colisión) |

Objetos del catálogo de carrera reutilizados (no cuentan como los 7): coco turbo/triple, protector solar (5 s), cangrejo teledirigido (a la rival más cercana), gaviota justiciera (a la líder), mina de arena, nube de tormenta (peso bajo), disco volador y silbato (peso bajo). Fuera en el MVP: coco dorado, pelícano taxi.

Total: **8 objetos** (7 del plan + piel de plátano); horas 29–43. La cifra de 28–41 h del plan sube 1–2 h por la piel.

Aceptación por objeto (PIE 4P, PktLag 150, 2 % de pérdida):

| Objeto | Aceptación |
|---|---|
| Piedra | 20 lanzamientos: noqueo solo si v ≥ 600; un único pickup por lanzamiento |
| Balón | 0 correcciones > 50 cm tras el empuje |
| Tinta | 3 impactos → 3 efectos en todas las máquinas |
| Pala | Nunca golpea a través de pared (test de traza); noquea solo a < 1,5 m |
| Plátano | Un solo noqueo por pisada, sin doble consumo |
| Dardo | Cápsula tras el noqueo igual en 4 máquinas (±10 cm) |
| Trabuco | Bola en la misma posición en 4 máquinas (±5 cm) |
| Garfio | 20 tirones sin atravesar geometría; agarre válido en 1 s |

### 5.1 Autoridad y réplica

| Aspecto | Regla |
|---|---|
| Recoger | Servidor valida (rango, ranura libre); réplica de ranura al dueño |
| Disparar / lanzar | Cliente pide `Server_UseItem`; el servidor crea el actor; `Multicast_InitializeThrow` da la trayectoria a los clientes (sin simular en cliente el impacto) |
| Impacto | Solo en el servidor; efecto de noqueo por `ApplyKnockdown` y FX por multicast |
| Munición | Entero replicado al dueño (`OwnerOnly`) |
| Pisada de la piel | Servidor; los clientes ocultan el actor con la réplica |
| Anti-abuso | Cooldown de uso 0,4 s; un objeto en la mano; sin disparar en reaparición |

## 6. Interfaz

Clases existentes de `Docs/Modos-UI-FX-2026-09-29.md` §2.2, sin widgets base nuevos.

| Elemento | Reutiliza | Cambio |
|---|---|---|
| Reloj de ronda | `UTN_RaceRoundClockWidget` | 120 s; parpadea en los últimos 10 s |
| Marcador por jugadora | `UTN_RunHUDWidget` sin pista + caras `TN_HUDFaces.h` | Puntos ordenados, la propia resaltada |
| Objeto activo | Ranura de objeto | Icono renderizado desde la malla; reserva en pequeño |
| «Quién te empujó» | Nuevo texto en pantalla de caída | «X te tiró», cara y flecha 3 s (`LastInstigator`) |
| +1 flotante | Nuevo, sobre la propia cabeza | Al puntuar |
| Flechas de rivales | Nuevo | A < 30 m fuera de pantalla; también tras 10 s sin ver a nadie |
| Aviso de viento | Borde amarillo 1,5 s antes | Silbido |
| Aviso de hundimiento | Barra de color en el borde de la pantalla y cuenta atrás en el suelo de 5 s | Nuevo |
| Reaparición | `UTN_GhostHatchWidget` | 4 s, luego 2 s de inmunidad |
| Fin de ronda / partida | `UTN_RaceTallyWidget`, `UTN_RaceChampionWidget` | Por puntos, no por puestos |
| Cámara | Tercera persona | +15 % de altura base en puentes [1.ª pasada]; espectadora: cámara aérea fija |
| Accesibilidad | Colores + símbolos, sin depender solo del color; texto ≥ 24 px a 1080p | Igual que los otros modos |

## 7. Efectos reutilizados

Principio del plan §3.6: los SFX salen de los sintetizadores existentes y los VFX de componentes existentes; **0 efectos nuevos con asset**. Solo se escribe lógica.

| Momento | VFX | SFX | Componente existente |
|---|---|---|---|
| Estampado | Polvo, pajaritos, aplastado en código | Choque de bola | `TN_TurtleDustComponent`, `TN_DizzyBirdsComponent`; `UTN_ShellImpactSynthComponent` |
| Caída al río | Salpicadura | Chapuzón | `TN_BeachFinishSplash`; `UTN_BeachSplashSynthComponent` |
| Noqueo | Pajaritos sobre la cabeza | Golpe seco | `TN_DizzyBirdsComponent`; `UTN_ShellImpactSynthComponent` |
| Golpe de pala / choque de bola | Chispa de polvo | Impacto | `UTN_ShellImpactFXComponent` |
| Disparo de pistola | Fogonazo (partículas ya usadas en tinta) | Chasquido | `UTN_RaceItemSynthComponent` |
| Tinta | Efecto de ciego | — | `MulticastApplyInkEffect` |
| Piel de plátano | Resbalón | Silbido | `ATN_BananaPeel` (existente) |
| Viento | Polvo lateral | Ráfaga | `TN_TurtleDustComponent`, `UTN_AmbientSynthComponent` |
| Hundimiento | Temblor de cámara suave y agua turbia (material de agua existente) | Crujido grave y burbujas | `UTN_AmbientSynthComponent`, `UTN_BeachSplashSynthComponent` |
| +1 | Texto flotante | Notita de moneda | `UTN_RaceCueSynthComponent` |
| Corte de puente | Polvo y astillas de código | Crujido | `TN_TurtleDustComponent`, `UTN_AmbientSynthComponent` |

Regla de convención del proyecto: efectos por `UPROPERTY(EditDefaultsOnly)` con `SpawnSoundAtLocation` / `SpawnSystemAtLocation` en C++, **nunca** `BlueprintImplementableEvent`.

## 8. Backend y red

Perfil de prueba: **PktLag 150 ms y 2 % de pérdida**, 4 y 8 instancias.

### 8.1 Modelo de autoridad

| Sistema | Quién decide | Qué se replica | Nota |
|---|---|---|---|
| Puntos, rondas, conchas | Servidor (`GameState`) | Puntos por jugadora, fase, reloj (hora de servidor) | Un evento de baja por caída: `Multicast_PlayerEliminated(Victim, Instigator, Cause)` |
| `LastInstigator` | Servidor | No se replica | Solo se usa en el servidor |
| Noqueo | Servidor (`ApplyKnockdown`) | Flag de noqueo y posición fijada (`RagdollFrozenLoc`) | Ragdoll cosmético en cada máquina |
| Bola (`ATN_ShellBody`) | Servidor | Física replicada | Sin simulación autoritativa en clientes |
| Dive | Predicción del dueño + corrección del servidor | `DiveDir` en `FTNTurtleNetworkMoveDataContainer` | Plan §3.5 punto 4; obligatoria |
| Estampado | Servidor (`ServerDiveSplat`) | Multicast de FX | El cliente no decide |
| Objetos | Servidor | Actor + `Multicast_InitializeThrow` | Impacto solo en servidor |
| Viento, hundimiento, corte | Servidor | Estado + hora de inicio | Sin réplica de cota por tick |
| Agarre/lanzamiento | Servidor (`TN_CarryComponent`) | Existente | La caja lanzada no choca con la portadora |
| Cajas y cofre | Servidor | Estado de disponibilidad | Reaparición por reloj de servidor |

### 8.2 Latencia de 150 ms y pérdida del 2 %

| Riesgo | Mitigación | Aceptación |
|---|---|---|
| El dardo llega tarde y «esquiva» en el cliente | Dardo lento (900 cm/s) y **visible**; el servidor es la verdad, sin compensación de latencia; el cliente lanza el disparo sin esperar (FX local, sin impacto local) | Revisión visual: 20 disparos con 150 ms, ninguno «atraviesa» |
| El golpe de pala parece fallar | Barrido en servidor con margen de +25 cm sobre el alcance a 150 ms; cono de 90° | Test: 20 golpes a 1,4 m con 150 ms: ≥ 18 aciertos; ninguno a 1,7 m |
| Paquete perdido en el evento de baja | Evento fiable (`Reliable`); marcador se corrige por réplica de estado | 100 % de bajas contadas en 200 caídas con 2 % de pérdida |
| Recuento de puntos desincronizado | Los puntos van en el `GameState` replicado, no en eventos | Marcador igual en 4 máquinas al fin de ronda |
| Hundimiento visto distinto | Cota = f(hora de servidor); con 150 ms de retraso el cliente ve la plataforma un instante más alta | Aviso de 5 s absorbe el desfase; sin bajas con cota > 30 cm de diferencia |
| Viento distinto en dueño y servidor | Misma función de aceleración en ambos; cuando la corrección supera 50 cm, se reconcilia | 0 correcciones > 50 cm por viento |
| Garfio con pérdida | El servidor confirma el resultado; el cliente lanza la animación y espera | 20 tirones sin atravesar geometría |
| Reaparición doble | Token de reaparición (número de secuencia) | 0 dobles en 100 caídas |

### 8.3 Coste de red

Objetivo del anfitrión: **≤ 140 KB/s con 8 jugadores** (mismo listón que el Rally). Estimación propia, sin medir: personajes 8 × 6 KB/s = 48 KB/s; bolas y proyectiles activos ≤ 8 × 3 KB/s = 24 KB/s; entorno < 6 KB/s; margen 60 KB/s. Se mide con la telemetría de red en el playtest 8P.

## 9. Optimización con 8 jugadores

| Partida | Límite duro | Motivo |
|---|---|---|
| Actores replicados de objetos vivos | ≤ 12 (máx. 8 proyectiles, 4 trampas) | La lanzada se destruye a los 4 s |
| Bolas físicas simultáneas | ≤ 8 | Una por jugadora; un `StartBody` nuevo expulsa el anterior |
| Cangrejos y gaviotas | ≤ 10 en total | Se apagan las patrullas fuera de las mesetas activas |
| Ragdolls cosméticos | ≤ 4 en pantalla; el resto usa animación de caída | Coste de física |
| FX de impacto | Máx. 6 por segundo, agrupados | Evita saturar en peleas de 8 |
| Actualización de red | `NetUpdateFrequency` 30 Hz en personajes y 10 Hz en objetos; **dormancia** para cajas, plataformas y barricadas (`FlushNetDormancy` al cambiar) | Ahorra ancho de banda |
| Relevancia | Distancia de red de 150 m (todo el mapa cabe en 180 m) | Toda la arena es relevante: el coste depende del número de actores |
| Hundimiento | Un actor por sector, movimiento en cliente por hora | Sin datos por tick |
| Malla de terreno | Presupuesto de malla del plan §2.5; las arenas usan menos triángulos que un mapa geográfico | Arenas ≤ 180 m de lado |
| Objetivo de rendimiento | 60 fps en 8P en el hardware de referencia del proyecto; 16,6 ms de cuadro | A confirmar con Unreal Insights en el playtest 8P |

Orden de sacrificio si no se llega: 1) menos ragdolls; 2) menos cangrejos; 3) menor frecuencia de los objetos; 4) nada de gameplay.

## 10. Pruebas y aceptación

### 10.1 Pruebas automáticas (`Tortunabo.TcT.*`)

| Prueba | Caso | Resultado esperado |
|---|---|---|
| Atribución | X toca a Y y Y cae a los 5,9 s | +1 a X |
| Atribución (negativa) | X toca a Y y Y cae a los 6,1 s | 0 puntos, 8 s de espera |
| Atribución lanzada | X lanza a Y; Y rueda 4 s y cae | +1 a X (ventana desde el aterrizaje) |
| Relevo | X toca a Y; Z toca a Y a los 2 s; Y cae | +1 a Z |
| Entorno | Gaviota tira a Y, líder = W | +1 a W |
| Hundimiento | Y cae por hundimiento sin contacto | 0 puntos |
| Reaparición | Rival a < 15 m del nido propio | Nido libre más alejado |
| Nido hundido | Sector en aviso | No se elige ese nido |
| Loot por puntos | Líder vs colista, 1000 tiradas | Pesos dentro del ±5 % de la tabla |
| Anti-encadenado | Pala dos veces en 1 s | El segundo golpe no noquea; empuja |
| Agarre | Intentar agarrar a una tortuga fuera de caparazón | No se agarra |
| Estampado | Pared a 400, 700; pendiente; objeto que se aleja | Rebote; estampado; nada; nada |
| Fin de partida | 3 conchas / 7 rondas / empate | Campeona; recuento; muerte súbita |
| Desconexión | 1 de 2 jugadoras abandona | Gana la que queda |

### 10.2 Pruebas de campo

| Prueba | Criterio |
|---|---|
| PIE 4P, PktLag 150, 2 % de pérdida | Ronda completa sin errores en el log; todos los objetos cumplen su aceptación (§5) |
| PIE 8P | Ronda a 8 jugadoras sin errores; 60 fps; ≤ 140 KB/s medido |
| Telemetría de encuentros | Encuentro cada 15–20 s (playtest, ≥ 3 partidas) |
| Comprensión | El 80 % de los testers explica «quién me tiró» sin ayuda |
| Arenas | Ninguna jugadora queda sin ruta al siguiente anillo en A01 hasta el final |
| Tiempo de cruce | Medir tiempo de cruce de A01 y P01 (dato pendiente del catálogo) |
| Sesión larga | 5 rondas seguidas sin fugas de actores (`obj list`) |

### 10.3 Criterios de aceptación del modo

1. Ronda a 8 jugadoras sin errores de red (PktLag 150, 2 %).
2. Puntos iguales en todas las máquinas al terminar la ronda.
3. Cada objeto cumple su columna de §5.
4. Dive con 0 correcciones > 50 cm al iniciarlo (tras la predicción).
5. La bola tras el estampado, en la misma posición en 4 máquinas (±5 cm).
6. Ninguna caída injusta al reaparecer (nunca sobre sector en aviso, nunca sin inmunidad).
7. Validación del director en el playtest de la tarea 0 del plan [G §4].

## 11. Presupuesto de assets

Criterio: reutilizar antes de crear (plan §3.6). Este modo no crea ningún asset imprescindible.

| Categoría | Elemento | Estado | Origen | Prioridad |
|---|---|---|---|---|
| Objetos | Piedra | EXISTENTE | `Piedra1.uasset` | 0 |
| Objetos | Piel de plátano | EXISTENTE | `ATN_BananaPeel` | 0 |
| Objetos | Balón de playa | BORRADOR IA | `SM_TN_BalonPlaya` (212 tris) | Deseable (rehacer) |
| Objetos | Pistola de tinta | BORRADOR IA | `SM_TN_PistolaTinta` (434) | Deseable |
| Objetos | Pala de mano | BORRADOR IA | `SM_TN_PalaMano` (336) | Deseable |
| Objetos | Pistola de noqueo y dardo | BORRADOR IA | `SM_TN_PistolaNoqueo` + `SM_TN_DardoMedusa` (780) | Deseable |
| Objetos | Trabuco de aire | BORRADOR IA | `SM_TN_TrabucoAire` (750) | Deseable |
| Objetos | Garfio y ancla | BORRADOR IA | `SM_TN_Garfio` + `SM_TN_Ancla` (706) | Deseable |
| Objetos | Iconos de los 8 objetos | UI | `render_preview.py` sobre las mallas | 0 |
| Mapas | P01, arenas, islas, países | Mallas | Pipeline volumétrico (código) | 0 |
| Mapas | Cobertura, barricadas, rampas, catapultas | Mallas | Sacos, erizos, castillos, puerta de conchas, `TN_BeachCatapult` | 0 |
| Mapas | Puentes | Mallas | Puentes de Mokius y naturales | 0 |
| Entorno | Hundimiento, viento, aguja | Código | Actores nuevos sin malla propia (usan la malla del terreno o el puente) | 0 |
| Interfaz | Marcador, aviso, +1, flechas | UI | HUD de carrera con caras | 0 |
| Animación | Disparo | Animación | La de lanzar | Deseable (pose propia) |
| Efectos | Todos | VFX/SFX | Ver §7 | 0 |
| Arte de tienda | Icono y splash de Steam; cápsulas | Imagen | Pendiente de otro documento | No imputable a TcT |

**TOTAL de assets nuevos imprescindibles de TcT: 0.** Los 6 borradores IA se importan como *placeholder* (Import Vertex Color = Replace, `M_TN_IAProp`) y bastan para jugar. El presupuesto del plan §3.6 contaba 7 iconos: se sustituye por renders de esas mallas (K1). Coste de la conversión de los iconos: 3–4 h (ver §12).

Deseables (no bloquean): rehacer los 6 borradores a mano (arte), pose de disparo propia, decorar el puente colgante del corte con astillas.

## 12. Horas

Estimación propia, sin medir, en horas de un desarrollador; sin incluir la revisión del director.

### 12.1 Trabajo propio del modo

| Bloque | h | Fase |
|---|---|---|
| Modo: fases, reloj, conchas, muerte súbita, desconexión | 14–18 | MVP |
| `LastInstigator`, eventos de baja y tests de atribución | 4–6 | MVP |
| Reaparición: nidos, inmunidad, reglas de sector | 4–6 | MVP |
| Loot por puntos (`RollLoot` con `Norm`) | 3–4 | MVP |
| HUD: marcador, «X te tiró», +1, flechas, avisos | 10–14 | MVP |
| Adaptación del `RaceTally` y `RaceChampion` a puntos | 4–6 | MVP |
| Viento (`ATN_WindGust`) | 5–7 | MVP |
| Barricadas y colocación de marcadores en P01 | 2–3 | MVP |
| Hundimiento (`ATN_SinkingPlatform`) | 10–14 | MVP |
| Objetos 1–5 (piedra, balón, tinta, pala, plátano) | 12–18 | MVP |
| Objetos 6–8 (dardo, trabuco, garfio) | 17–25 | Fase B |
| Iconos por render | 3–4 | MVP |
| Gaviotas con suelta ajustada y cangrejos | 5–8 | Fase B |
| Corte de puentes (`ATN_BridgeSpan`) | 12–16 | Fase B |
| Aguja giratoria (A04) | 8–10 | Después |
| Pruebas automáticas, playtest 4P y 8P, corrección de errores | 12–16 | Continuo |
| **Total del modo** | **125–175** | |

Reparto: MVP ≈ 71–100 h; Fase B ≈ 34–49 h; Después ≈ 8–10 h; pruebas ≈ 12–16 h.

### 12.2 Dependencias compartidas (no se suman al total)

| Pieza | Fuente | h |
|---|---|---|
| Noqueo autoritativo (`ServerFreezeRagdoll` ampliado) | Plan §3.5 punto 5 | 6–10 |
| Doble salto físico (pendiente, rebote, estampado, predicción del dive) | Plan §3.5 | Estimación propia; la tarea 0 del plan la mide |
| Generador `arena` y generador `isla` | Catálogo §6 | 24–32 (12–16 cada uno) |
| Puentes como actores (`ATN_BridgeSpan`) | D §1.9 | Incluido arriba (Fase B) |
| Extensión de `RaceShellHalves` a puntos | Plan | Incluida en 12.1 |

### 12.3 Mapas por lote

| Lote | Mapas | h |
|---|---|---|
| MVP | P01 (marcadores), A01 Diana, I01 Filipinas | 24–34 |
| Fase B | A02 Dónut, A05 Tablero, L05 Venecia | 14–20 |
| Después | A03, A04, A06, I02, I03-T, I05, países | 60–100 (sin fecha) |

### 12.4 Hitos

1. **Hito 1 (MVP jugable):** modo, puntos, atribución, P01 y A01, objetos 1–5, viento y hundimiento. Requiere noqueo autoritativo y predicción del dive.
2. **Hito 2 (Fase B):** dardo, trabuco, garfio, corte de puentes, I01, A02 y A05.
3. **Hito 3 (después):** resto de arenas, islas y países.

Decisiones que necesita el director: K1 (0 iconos nuevos y bajar el plan de 11 a 4 imprescindibles), K3 (punto por sobrevivir), K4 (corregir nidos de A01), K5 (A06 en «Después») y confirmar el anti-encadenado de 1,5 s.

## Anexo (2026-09-29): cómo aparecen los objetos

Tres fuentes, todas decididas en el servidor y sin assets nuevos:

1. **Cajas fijas** (lo ya especificado): 8 cajas, una por meseta, que reaparecen a los 20 s en su sitio.
2. **Lanzamientos desde el cielo con gaviota**: cada 25–35 s una gaviota existente cruza el mapa y suelta una caja de objetos (la misma malla de caja) sobre una zona elegida por el servidor, preferentemente lejos de la líder y cerca de donde haya más jugadoras (provoca peleas). Aviso de 3 s: sombra/círculo en el suelo y graznido. La caja cae con física, puede rebotar y caer al agua; se abre al tocarla. Contiene un objeto de la tabla con peso de loot por puntos, o un objeto «raro» (garfio, trabuco) con más probabilidad que en las cajas fijas. Máximo 2 cajas del cielo vivas a la vez.
3. **Cofre central**: un cofre en el punto de más tensión del mapa (cuello de P01, centro de la Diana) que se abre a los 60 s de ronda con 2 objetos raros.

Red: posición de caída elegida en el servidor y replicada antes del aviso; la caída es simulación física del servidor con réplica de movimiento; apertura por servidor. Muerte súbita mantiene su bola de 1 punto que cae sobre el centro cada 8 s.
