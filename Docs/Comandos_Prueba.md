# Comandos de prueba

Chuleta para probar cada cosa sin tener que llegar a ella. Se escriben en la consola de la ventana del **anfitrión**
(en PIE con varios jugadores, la del servidor). Los índices de jugador empiezan en 0 (0 = el anfitrión). Estado:
rama `claude/modo-carrera` a 28-09-2026.

## Abrir y moverse

| Comando | Qué hace |
|---|---|
| `open LVL_BeachRace?BeachSeed=42` | Carrera en la playa sin pasar por el lobby, con una semilla fija (misma ronda siempre). |
| `open LVL_BeachRace?BeachWins=1` | Partida a 1 concha (se llega antes al campeón). Se combinan: `?BeachSeed=42?BeachWins=1`. |
| `TN.Mode Coop` / `TN.Mode Race` | Modo de la próxima partida que salga del lobby (sin argumento dice el actual). También en la pestaña «Misión» del general. |
| `fly` / `walk` | Volar con la tortuga para ir a cualquier sitio y volver a andar (trucos del motor, en PIE). |
| `ghost` | Volar atravesando todo. |
| `teleport` | Te lleva a donde miras. |
| `slomo 0.3` / `slomo 1` | Cámara lenta para ver saltos, gusanos o gaviotas; `1` vuelve a velocidad normal. |
| `stat fps` / `stat unit` | Fotogramas y tiempos. |

## Reparto y dificultad de la playa

Van en la ventana del anfitrión o en la de un cliente del PIE (el servidor mueve su tortuga). A un cliente remoto de
verdad no le llegan: escríbelos en el anfitrión con el índice del jugador. La ronda se monta por partes: lo que la
necesita espera a que esté lista.

| Comando | Qué hace |
|---|---|
| `TN.Beach.Go <sitio> [jugador]` | Lleva tu tortuga (o la de ese jugador) allí, sobre arena libre y mirando a lo que toca: `salida` (su huevo), `fortaleza` (delante de la puerta de la siguiente por delante; si no hay más, la primera), `trinchera` (12 m antes de la siguiente), `poza` (en su orilla), `cresta` (al pie de la cara empinada de la siguiente con cornisa), `sprint` (su sitio de la línea del sprint final), `acantilado` (25 m antes del filo), `meta` (a 3,5 m del filo: un paso y al agua) o un número: esos metros desde la salida, de 0 a 800 (`TN.Beach.Go 400` es la mitad del recorrido, donde sale el sprint final). La saca de la bola o del caparazón. |
| `TN.Race.Difficulty <Easy\|Normal\|Hard>` | Dificultad del reparto desde la próxima ronda (también `fácil`, `normal`, `difícil`; sin argumento dice la de la ronda y la siguiente). Fácil: ayudas x1,6, trampas x0,7, enemigos x0,6; Difícil: enemigos x2,5, trampas x1,8, ayudas x1,4. |
| `TN.Beach.Reroll [semilla]` | Rehace la ronda ya con la dificultad actual (semilla al azar si no se da). Los huevos vuelven a cerrarse; cuando está lista, dice cuántos elementos, fortalezas y cofres tiene. `TN.Race.Difficulty Hard` y luego `TN.Beach.Reroll 42` para ver la misma semilla en Difícil. |
| `TN.Beach.ShowFootprints 1` | Huellas del reparto en el suelo (colores en `Docs/Modo_Carrera.md`, «Nivel y capturas»). |

## Poner cualquier pieza de la playa delante de ti

`TN.Beach.Place <Elemento|número> [Tamaño=1] [Extent=0] [Semilla]` crea el elemento delante de tu tortuga;
`TN.Beach.Place clear` borra los creados así. `TN.Beach.ShowFootprints 1` enseña las huellas del reparto.

- **Trampas:**
  - `BarbedWire`: alambre de espino (Extent = largo en cm).
  - `Seaweed`: algas.
  - `WobblyPlatform`: plataforma sobre un hoyo.
  - `BrokenBucket`: cubo roto.
  - `SpadeRamp`: pala.
  - `SandDungeon`: castillo con salas.
  - `ShellGate`: puerta de conchas.
  - `ClamTrap`: concha que atrapa.
  - `MovingPlatform`: plataforma móvil. Con semilla par sale la balsa (`TN.Beach.Place MovingPlatform 1 0 2`) y con impar, el ascensor con catapulta (`… 1 0 3`).
  - `Catapult`: catapulta (de un solo uso: tras el primer disparo queda partida hasta la ronda siguiente; para otra, ponla otra vez).
    - Bola de caparazón (ronda 4), sola: `TN.Beach.Place Catapult`, entra en el cazo andando y pulsa la tecla del caparazón. La bola arma la catapulta como una tortuga de pie: 1 s de aviso («¡AGÁRRATE!») con la bola clavada en su sitio (tiembla solo la madera del brazo y del cazo) y sale lanzada hacia el mar dando volteretas, rueda y sale sola del caparazón al pararse. Igual con `TN.Beach.PlaceBoosted Catapult` (mucho más lejos).
    - Con dos tortugas (o dos ventanas / un cliente): una metida en su caparazón dentro del cazo y la otra que entra andando y arma la catapulta. La bola no debe moverse aunque la otra la roce, y las dos salen lanzadas. En el cliente la bola debe volar igual que en el anfitrión, sin tirones ni saltos al salir. Una bola aturdida (la que deja un golpe de mina o de enemigo) no cuenta. En el log del anfitrión: `[Playa] Catapulta … dispara: N lanzadas (M ya en su bola)`.
  - `Mine`: mina.
  - `Trampoline`: trampolín (4 variantes por semilla).
  - `FortressMedium`, `FortressLarge`, `FortressColossal`: fortaleza de arena con premio en la cima (lanzador potenciado, cofre y conchas de 50 y 100). Mira hacia donde miras (lanza hacia allí) y sale lejos: la colosal, a ~54 m. Por ejemplo `TN.Beach.Place FortressColossal 1 0 7`; semillas seguidas cambian catapulta o trampolín y el lado de la espiral.
- **Lanzadores potenciados** (los de la cima de las fortalezas): `TN.Beach.PlaceBoosted <Catapult|Trampoline> [Tamaño=1] [Semilla]`, delante de ti y mirando hacia donde miras. `TN.Beach.Place clear` también los borra.
- **Subir a la cima:** `TN.Beach.Fortress.Top [jugador=0]` lleva a esa tortuga a la cima de la fortaleza más cercana, detrás del lanzador y mirando hacia él.
  - `TreasureChest`: cofre (mira hacia donde mira tu tortuga; `TN.Beach.Chest` lo pone con el frente hacia ti).
- **Enemigos:**
  - `GiantCrab`: cangrejo gigante.
  - `SeaUrchin`: erizo.
  - `Lizard`: lagarto.
  - `QuadLane`: paso de quads (Extent = ancho).
    - Ruedas (ronda 4): `TN.Beach.Place QuadLane` y `TN.Beach.Quad.Now` para que pase ya. Mira las ruedas desde el hueco entre ellas y desde fuera: la cara de dentro y la de fuera deben verse enteras (neumático oscuro, llanta clara, buje y tornillos), sin huecos ni el interior del neumático sin pintar, y los tacos con la punta cerrada.
  - `GullZone`: zona de gaviotas.
  - `HermitCrab`: cangrejo ermitaño bola (Extent = largo de su calle; 0 = 40 m). La calle va hacia donde miras, centrada 13 m delante; lo alto (donde espera) es el extremo más cercano a ti. Con `TN.Beach.Place HermitCrab 1 2000` queda 3 m delante y la calle llega a 23 m: rodéalo y ponte en su calle, delante de él.
  - `PoolOctopus`: pulpo de poza. Dentro de una poza vive en ella (si cae a menos del 70 % de la orilla, se queda ahí); en la arena hace su propio charco de 7 m de radio y cualquier tortuga que entre cuenta como nadando.
  - `SandFleas`: enjambre de pulgas de arena.
  - `ToyTank`: tanque de juguete (Extent = tramo de patrulla; 0 = 24 m, hacia donde miras).
- **Decorado militar:** `Sandbags`, `AmmoCrate`, `TankTrap`, `MilitaryHelmet`, `CamoNet`, `Jerrycan`, `ToySoldiers`.
- **Decorado:**
  - Castillos y estructuras: `SandCastleSmall`, `SandCastleHuge`, `ShipSailWreck`, `Boardwalk`, `WoodenPostPath`.
  - Piedras y restos de playa: `Coconut`, `Rock`, `RockCluster`, `MossyLog`, `OldPlanks`, `FishingNet`, `Driftwood`.
  - Basura y objetos: `PlasticCup`, `Bottle`, `SodaCan`, `BeachBall`, `ToyBucket`, `BeachChair`, `PlantedUmbrella`, `RubberDuck`… (lista completa en `TN_BeachTypes.h`).

## Enemigos, tormenta y gusano

| Comando | Qué hace |
|---|---|
| `TN.Beach.Gull.Attack 1` | Cada zona de gaviotas suelta una cagada sobre la tortuga más cercana (ragdoll y mancha). Quieta, andando o corriendo en línea recta te da; si al quedarse fijo el «!» (el último 1,5 s: ya cae por la línea que llevabas) giras corriendo 60° o más o te das la vuelta, o te tiras en plancha justo antes de que caiga, te libras (`esquiva la cagada … en plancha` en el registro). |
| `TN.Beach.Gull.Attack 2` | Picado con agarre: sombra negra que nace diminuta y crece mientras baja siguiéndote (2,3 s). Andando o corriendo en línea recta te pilla: te sube pataleando y te suelta en bola (siempre caes al suelo). Si cuando pliega las alas del todo (el último 1,5 s: ya va lanzada por tu línea) giras corriendo 60° o más o te das la vuelta, te tiras en plancha o te metes en bola, baja igual, pica la arena y vuelve a subir. Sin número, al azar. |
| `TN.Beach.Gull.Grab [veces=2] [jugador]` | La zona de gaviotas más cercana coge a tu tortuga (o a la del jugador N, índice en `PlayerArray` del anfitrión) con el pico N veces seguidas: cada vez que estés libre (de pie, sin bola ni derribo), un picado que ya va por su último medio segundo y te coge si no te mueves. Para el fallo «segunda gaviota + caparazón»: colgando, pulsa el caparazón (sin objeto en la mano) y cae en bola aturdida, como al acabar el vuelo; en el registro, `se le escurre` y ningún `Red de seguridad`. Ver `Docs/Modo_Carrera.md`, «Segunda gaviota + caparazón = torbellino». |
| `Automation RunTests Tortunabo.Beach.Hold` / `Automation RunTests Tortunabo.Beach.Gull` / `Automation RunTests Tortunabo.Beach.Crab` | Pruebas automáticas de lógica pura (en la consola del editor o en Session Frontend), con las velocidades de verdad (2 y 4 m/s): quién mueve a la tortuga y la sujeción de los enemigos con agarres seguidos; el nerf de las gaviotas (andando o corriendo recto te pilla; girando corriendo al lanzarse te libras; la plancha libra de la cagada); el cangrejo gigante (persigue entre andar y correr, su mazazo se salta). |
| `TN.Beach.Place GiantCrab` | Un cangrejo gigante delante: andando te alcanza, corriendo se te escapa poco a poco; al levantar la pinza, salta (lo pasa por encima: `salta por encima del mazazo` en el registro) o corre hacia otro lado. |
| `TN.Beach.Quad.Now` | Todos los pasos de quads avisan y pasan ya. |
| `TN.Beach.Storm.Start [metros detrás=30] [cm/s=180]` | Arranca la tormenta de bañistas. |
| `TN.Beach.Storm.Here [jugador] [metros=4]` | Pone el frente de la tormenta 4 m (o `metros`) por delante de tu tortuga o de la del jugador N (índice en `PlayerArray`: así se prueba la del cliente desde el anfitrión): se queda dentro y un bañista le da la patada. Acaba sí o sí en arena abierta ~20 m por delante del frente, siempre en bola por el aire: con arco libre, chocando; si no (una pared delante, nadando o a más de 45 m), atravesando lo que haya hasta bajar sobre su sitio. Con `metros` = 30 o 60 se prueban la patada larga y la que atraviesa; pegada a la pared de un castillo o de una fortaleza, la que no tiene arco libre. Sin tormenta, crea una. Ver `Docs/Modo_Carrera.md`, «La patada que no puede entrar en bucle». |
| `TN.Beach.Storm.Stop` | La para. |
| `TN.Beach.Storm.Info` | Distancia y velocidad de la tormenta respecto a la última tortuga. |
| `TN.Beach.Lizard <huidizo\|generoso\|mordedor>` | Un lagarto de ese carácter 22 m delante de ti, mirándote. El generoso (motas doradas) deja premio al huir; el mordedor (cresta roja) se lanza a morderte. `TN.Beach.Place clear` lo quita. |
| `TN.Beach.StunNearest [segundos=3]` | Marea al enemigo más cercano a tu tortuga (pajaritos, sin atacar). Los quads, no. Con el ermitaño rodando lo para en seco; con el pulpo agarrando, suelta; las pulgas se dispersan; el tanque echa humo y la antena da vueltas. |
| `TN.Beach.Worm [jugador=0]` | Un gusano de arena se come ya a esa tortuga. |
| `TN.Beach.Mine.Blast <jugador> [metros=0] [veces=1] [cada=6] [espera=cada]` | Solo en el servidor. Pone una mina a esos metros detrás de la tortuga del jugador (índice en `PlayerArray` del anfitrión: 1 = el primer cliente) y la hace saltar; lo repite «veces» veces cada «cada» segundos (la primera, a los «espera»), esperando a que la tortuga esté libre. Con 0 m sale en bola; con 2-6 m, empujón. Para la red (#18): con `p.NetShowCorrections 1` en las dos máquinas y `NetEmulation.PktLag 120` en el cliente, el empujón no debe dar ninguna línea `*** Client: Error` ni `*** Server: Error` tras `explota`. Con `-game` (donde `-ExecCmds` no corre), `-TNMineBlast=1_3_6_6_30` en la línea de órdenes del servidor. |
| `TN.Beach.Enemy.Stats` | Cuántos enemigos hay y cuántos van a ritmo lento por estar lejos. |
| `TN.Beach.Enemy.Debug 1` | Dibuja radios de visión, oído y patrulla, y estados. |

## Carrera: rondas, meta y pantallas

| Comando | Qué hace |
|---|---|
| `TN.Race.WinRound [jugador=0] [puesto]` | Ese jugador «toca el agua». La primera gana la concha y arranca la cuenta de 10 s; repítelo con otro índice para la media concha; deja a alguien sin llegar para ver el gusano. En su pantalla se cierra el huevo negro con «Has quedado X.º»; con `puesto` (1-8), esa pantalla enseña ese puesto y su premio aunque sean pocos (las conchas van por el orden de verdad): `TN.Race.WinRound 1 8` le da al cliente el alga de peluca. |
| `TN.Race.NextRound` | Salta a la ronda siguiente: en plena carrera la cierra ya con su recuento (conchas para quien haya llegado); en el recuento o en el título del sprint, sigue sin esperar. Dos veces seguidas en plena carrera: el paso entre rondas con el huevo negro, «RONDA N» y el 3, 2, 1. |
| `TN.Race.Sprint [jugador] [jugador]…` | Empate forzado a 3 conchas y sprint final (por defecto, 0 y 1). |
| `TN.Race.Champion [jugador=0]` | Salta directo a la pantalla del campeón con ese ganador. |
| `TN.Race.PlayAgain` / `TN.Race.ChangeMode` / `TN.Race.Menu` | Los botones de la pantalla del campeón. |
| `TN.Race.Stun [segundos=3] [jugador=0]` | Aturde en bola a esa tortuga. |
| `TN.Race.Kill [jugador=0]` | Pasa por la ruta de «muerte» (en la carrera, aturde). |
| `TN.Race.Void [jugador=0]` | La tira al vacío: vuelve a su último sitio seguro aturdida. |
| `TN.Race.Bury [metros=3] [jugador=0]` | La mete bajo la arena donde está: la red de seguridad la devuelve encima en ~0,2 s, de pie (sin bola), con dos avisos `[Carrera] Red de seguridad` en el registro. Repetido tres veces seguidas en el mismo sitio: la segunda va a su último sitio seguro y la tercera a arena abierta lejos, con un aviso de bucle (ver `Docs/Modo_Carrera.md`, «Seguridad: nunca bajo el mapa»). |
| `TN.Race.SafetyNet 0\|1` | Apaga o enciende esa red de seguridad (en el anfitrión; para comparar). |
| `TN.Race.Splash [tamaño=1]` | Chapuzón de meta delante de ti (solo en tu pantalla). |
| `TN.Beach.Egg` | Cierra otra vez los huevos (de la salida o del sprint) con cada tortuga dentro y a los 1,5 s repite la salida: se rompen, 1 s en el huevo (de pie, sacudiéndose la cáscara y mirando al mar) y salen lanzadas. Sin cambiar de ronda. |

### Vistas previas sin jugar

| Comando | Qué enseña |
|---|---|
| `TN.Race.CountdownPreview` | La cuenta de 10 s con su «¡TIEMPO!». |
| `TN.Race.Tally [ganador 0-5, -1 nadie] [jugadores 1-6] [1 = corona y podio] [medias=1]` | El recuento, con las medias conchas; por ejemplo `TN.Race.Tally 0 4 0 2`. |
| `TN.Race.SprintPreview [finalistas 2-6]` | El título del sprint final con el «VS». |
| `TN.Race.Podium [jugadores 1-3]` | La pantalla del campeón con el podio animado. |
| `TN.Race.ArrivalPreview [puesto 1-8 = 1] [1 = sprint]` | La llegada al agua: el huevo negro se cierra desde arriba y desde abajo, «Has quedado X.º» con su premio (coronas de oro, plata y bronce; cubo, media concha rota, flotador pinchado, calcetín mojado, alga), un mensaje al azar y su sonido, y se rompe. |
| `TN.Race.RoundPreview [ronda = 2] [1 = sprint]` | El paso entre rondas: el huevo negro, «RONDA N» (o «SPRINT FINAL») con su frase, «Colocando la playa…», tres «pum» con 3, 2, 1 y se rompe. Desde la ronda 4, la frase de la bola de partido. |
| `TN.Race.PreviewOff` | Cierra cualquier vista previa. |

## Botín, brillo y conchas

| Comando | Qué hace |
|---|---|
| `tn.Search.Luck 1` | Rebuscar siempre da objeto; con `0` nunca da. `-1` vuelve a la probabilidad del actor. |
| `tn.Search.Seconds 0.3` | Rebuscar dura 0,3 s en lugar de lo normal; `-1` vuelve a lo del actor. |
| `tn.Search.Show 1` | Baliza y huella en cada decorado rebuscable a menos de 300 m (en la playa, solo los que tienen su actor ahora: los que están cerca de alguna tortuga). |
| `TN.Beach.Loot.Reroll` | Quita el botín de la ronda (rebuscables, objetos y conchas) y lo vuelve a repartir. |
| `TN.Beach.Loot 0` | Sin botín desde la ronda siguiente; con `1` vuelve. |
| `TN.Beach.Chest` | Un cofre de la playa delante de ti, con el frente hacia tu tortuga: 5,5 s manteniendo E, dos objetos de los mejores y seis conchas de puntos. `TN.Beach.Place clear` lo quita. |

### Rendimiento de la playa

| Comando | Qué hace |
|---|---|
| `TN.Beach.Perf` | Tiempos de la última ronda (reparto, asientos, decorado, actores, botín y fotogramas), decorado local (piezas, instancias, con colisión y con sombra, partes que se mueven, componentes), actores de la playa (con dormancy, siempre relevantes, relevancia media), rebuscables (puntos, usados, actores ahora), objetos, conchas y la lista de red. En la ventana donde se escribe y, en PIE, también el servidor. |
| `TN.Beach.BuildBudgetMs 6` | Milisegundos por fotograma para montar la ronda (asientos, decorado local y actores). |
| `TN.Beach.AsyncBuild 0` | Monta la ronda entera en un fotograma, como antes (para comparar); `1` vuelve a por partes. |
| `TN.Perf.BeachTickWake 0` | Minas, algas y puertas de conchas con el Tick siempre encendido, como antes de #59 (para comparar); con `1` (lo normal) lo apagan sin tortuga, caparazón ni cámara cerca (40 m las minas, ~100 m las algas, ~35 m las puertas) y lo mantienen mientras tienen algo en marcha (mecha, explosión, tortuga enganchada, puerta abierta). En `-game`: `-dpcvars=TN.Perf.BeachTickWake=0`. Actores con Tick en reposo en `TN.Stress control` (Saved/Stress, `ticking_actors`). |

## Objetos de carrera (tipo Mario Kart)

Se escriben en la ventana del anfitrión (o de un cliente del PIE: actúan en el mundo del servidor). Los objetos se llaman por
su nombre en inglés, en español o por su número: `Coconut`/`coco`, `TripleCoconut3`/`triple`, `GoldenCoconut`/`dorado`,
`PelicanTaxi`/`pelicano`/`taxi`, `Sunscreen`/`protector`/`estrella`, `HomingCrab`/`cangrejo`, `GullStrike`/`gaviota`,
`SandMine`/`mina`, `StormCloud`/`nube`/`rayo`, `Frisbee`/`disco`, `Whistle`/`silbato`. `TN.Race.Item list` enseña todos.
Para usarlos, la tecla de siempre de usar el objeto de la mano (E si no hay nada que coger cerca).

| Comando | Qué hace |
|---|---|
| `TN.Race.Item <objeto\|list> [jugador=0]` | Da ese objeto a la tortuga (a la mano; si la mochila está llena, sustituye lo de la mano). |
| `TN.Race.ItemUse <objeto> [jugador=0]` | Se lo da y lo usa en el acto (para ver el efecto sin más). Si no se puede usar (sin nadie a quien apuntar, sin sitio...), suena el «nop». |
| `TN.Race.ItemBox [n=1]` / `TN.Race.ItemBox clear` | `n` cajas de objetos en fila delante de ti; `clear` quita las puestas así. |
| `TN.Race.ItemRank [jugador=0]` | Puesto en la carrera y peso de cada objeto de carrera para ese puesto (rebuscar, caja y cofre). |
| `TN.Race.Boost [segundos=3] [multiplicador=2] [jugador=0]` | Turbo sin gastar objeto. |
| `TN.Race.Star [segundos=8] [jugador=0]` | Protector solar sin gastar objeto. |
| `TN.Race.ItemClear` | Quita lo lanzado (cangrejos, minas, discos, gaviotas, nubes, pelícanos) y cancela los efectos de todas las tortugas. |

Pruebas con una sola tortuga: `TN.Race.ItemUse Coconut`, `Sunscreen`, `PelicanTaxi`, `SandMine`, `Frisbee`, `Whistle` (con enemigos
cerca) y `HomingCrab` (contra el enemigo más cercano por delante). La gaviota justiciera y la nube de tormenta necesitan a otra
tortuga: con el anfitrión y un cliente, `TN.Race.ItemUse GullStrike 1` da la gaviota al cliente y va a por quien vaya delante de él.
Con dos jugadores, `TN.Beach.Go 200 0` y `TN.Beach.Go 100 1` colocan al anfitrión por delante para ver quién recibe qué en las cajas.

## Música de fondo de la carrera

Detalle en `Docs/Sonido_Tortuga.md` («Música de fondo de la carrera»). Suena sola en la carrera; estos comandos sirven para oírla
y probarla en **cualquier mapa** (también en el lobby o el cooperativo) sin jugar una ronda. Van en la ventana de quien escucha.

| Comando | Qué hace |
|---|---|
| `TN.Race.Music.Play [tensión] [duck]` | La hace sonar ya, con la introducción y el tema. `TN.Race.Music.Play 0 0` = la base; `TN.Race.Music.Play 1` = a tope de tensión; `TN.Race.Music.Play 0 0.55` = como en la cuenta de salida; `TN.Race.Music.Play 0.7 0.5` = como en la cuenta de 10 s. |
| `TN.Race.Music.Tension <0..1>` / `TN.Race.Music.Duck <0..1>` | Mueven la tensión o el «ducking» (`-1` = los decide la partida). Suben y bajan suaves (2 s / 0,3 s): probar `Tension 0`, `0.5`, `1` y `Duck 0`, `1` con la música sonando. |
| `TN.Race.Music.Layers <máscara>` | Capas: 1 ritmo, 2 armonía, 4 melodía, 8 corneta, 16 tensión (31 = todas). `TN.Race.Music.Layers 16` con `Tension 1` es la capa de tensión sola; `Layers 4`, solo la melodía. |
| `TN.Race.Music.Restart` | Vuelve a la introducción. |
| `TN.Race.Music.Status` | Qué decide el director y cómo va el motor (compás de 32, vuelta de 2, tensión y «ducking» suavizados). |
| `TN.Race.Music.Volume <0..1,5>` | Volumen propio de esta música (1 por defecto), aparte del deslizador de Música. |
| `TN.Race.Music.Stop` / `TN.Race.Music.Auto` | Calla (fundido) / vuelve al modo normal (la decide la partida y se quitan `Tension`, `Duck`, `Layers`). |
| `TN.Race.Music.Debug 1` | Un aviso en el registro por cada cambio de decisión (cuenta de salida, carrera, último minuto, cuenta de 10 s, recuento...). |

Para verlo en una ronda de verdad (PIE de 2, ventana del anfitrión): `TN.Race.Music.Debug 1`, salir (suena apartada durante la cuenta
3, 2, 1 y se abre al dar la salida), `TN.Race.WinRound 1` (llega el otro jugador y tú sigues corriendo: cuenta de 10 s, sube la
tensión y se aparta) y esperar el «¡TIEMPO!» (se calla). Si llegas tú, se calla a propósito: empieza tu música de victoria. Para el
último minuto del tiempo de la ronda (el límite son 9 min), `TN.Race.Music.Tension 0.6`. El recuento, el título del sprint
(`TN.Race.Sprint`) y el podio (`TN.Race.Champion`) también la callan porque ya tienen su música.

## Golpes del caparazón

Detalle en `Docs/Sonido_Tortuga.md` («Golpes del caparazón»). Sonido y mini efecto cuando la bola choca, en todas las máquinas
(cada una en su copia de la bola, sin red).

| Comando | Qué hace |
|---|---|
| `TN.Shell.Impact.Test [arena, roca, madera, agua, tortuga, enemigo, trasto o todos] [fuerza 0..1]` | Un golpe de ese timbre delante de tu tortuga (sonido y partículas). Sin argumentos, los siete uno cada 0,9 s. Prueba `0.15`, `0.5` y `1` de cada uno. |
| `TN.Shell.Impact.Debug 1` | Una línea por golpe real: timbre, velocidad del impacto, fuerza y contra qué choca. |
| `TN.Shell.Impact.MinSpeed <cm/s>` | Velocidad mínima para que suene (260 de serie; `100` para oír más roces, `600` para menos). |
| `TN.Shell.Impact.Volume <x>` / `TN.Shell.Impact 0` | Volumen de los golpes / apagarlos y encenderlos (`1`). |

Con física de verdad (PIE de 2, anfitrión y cliente): meterse en el caparazón (la tecla de siempre) y rodar por una cuesta o
lanzarse con una catapulta, el trampolín o `TN.Race.Stun 3` (bola aturdida); la otra ventana debe ver y oír lo mismo. Contra
la arena de la playa, el acantilado (`TN.Beach.Go acantilado`), una fortaleza (`TN.Beach.Place FortressMedium`), una catapulta,
la plataforma móvil, un enemigo (`TN.Beach.Place GiantCrab`), un cubo roto y otra tortuga en bola, y cayendo al mar. Rodar
despacio por la arena no debe sonar; un bote de más de 2,6 m/s, sí.

## Fantasma espectador y volver a la vida

| Comando | Qué hace |
|---|---|
| `TN.Ghost.Become [jugador=0]` | Ese jugador pasa a fantasma espectador en cualquier modo, también en el lobby. Controles: ←/→ o LB/RB cambian de tortuga; C o R3 alterna cámara libre y fija. |
| `TN.Ghost.Revive [jugador]` | Solo en el cooperativo y el lobby: el fantasma vuela en U, se mete en un huevo y sale. En la carrera solo avisa. |

## Menú de pausa

Escape en el juego y Tabulador en el editor (PIE); Start en el mando. No tiene comandos. La lista de pruebas está en
`Docs/Menu_Pausa.md`.

## Modo VR

Detalle, controles y pruebas en `Docs/Modo_VR.md`. Se escriben en la ventana de **quien lo prueba** (el modo VR es de cada
máquina).

| Comando | Qué hace |
|---|---|
| `TN.VR 2` | Modo VR simulado sin gafas: primera persona, aletas quietas delante, HUD y menús en un panel del mundo; el ratón mira y apunta. También `-vrsim` al arrancar o Ajustes > Juego > «Modo VR». |
| `TN.VR 1` | Modo gafas: enciende las gafas OpenXR si las hay (Meta Quest Link o el Meta XR Simulator como runtime). |
| `TN.VR 0` / `TN.VR -1` | Apagado a la fuerza / lo que diga el ajuste (Automático: gafas solo si el motor pinta en estéreo). |
| `TN.VR.Status` | Escribe el modo, si hay OpenXR, gafas y estéreo, el dispositivo, el rig y si hay un menú delante. |
| `TN.VR.Recenter` | Recentra la vista (con gafas) y vuelve a poner delante el HUD o el menú. Con los mandos, clic del stick derecho. |
| `TN.VR.HudDistance 140` / `TN.VR.HudFov 50` | Distancia (cm) y ancho (grados) del HUD. |
| `TN.VR.MenuDistance 160` / `TN.VR.MenuFov 58` | Lo mismo para los menús. |
| `TN.VR.SmoothTurnSpeed 120` | Grados por segundo del giro suave. |
| `Automation RunTests Tortunabo.VR` | Pruebas automáticas del modo VR (puntero, HUD, giro, panel, botones de los menús). |

## Pantalla de carga del huevo

| Comando | Qué hace |
|---|---|
| `TN.Loading.Test` | Cierra el huevo y lo rompe a los 3 s. |
| `TN.Loading.Test.Close` | Lo cierra y lo deja cerrado. |
| `TN.Loading.Test.Break` | Rompe el huevo que esté a la vista. |
| `TN.Loading.Test.Open` | Lo abre sin romperlo. |
| `TN.Loading.Test.Go` | Cierra y rompe con «¡ADELANTE!». |
| `TN.Loading.Test.GoOnly` | Solo el «¡ADELANTE!». |

## Salas del menú

No existen en la build Shipping.

| Comando | Qué hace |
|---|---|
| `TN.Rooms.FakeError <locked\|full\|kicked\|other>` | Simula que el servidor no te deja entrar (sala cerrada, llena, expulsado u otro motivo): pantalla de vuelta al menú y el aviso en «Unirse». Sin segunda instancia. |
| `TN.Rooms.FakeError <joinfull\|gone\|noaddress>` | Simula que falla la entrada en la sesión (llena, ya no existe, sin dirección del anfitrión): el aviso sale en el menú de salas sin recargarlo. |
| `TN.Travel.Fail [/Game/Ruta/Mapa \| motor] [segundos]` | Solo en el anfitrión: pide un `ServerTravel` a un mapa que no existe (por defecto `/Game/Maps/TN_MapaQueNoExiste`), que `CanServerTravel` para sin mandar a los invitados; con `motor`, simula un fallo de `UEngine::OnTravelFailure` (un mapa que existe pero no carga), con la desconexión que pide el motor. Con segundos, lo hace pasado ese tiempo (para que entren invitados en una prueba sin ventana con `-ExecCmds`). En los dos casos, el registro debe dar un solo `Fallo de viaje ... (fallo 1 seguido)` y el anfitrión debe seguir en el lobby (o recargarlo) con su sesión. Antes `net.AllowPIESeamlessTravel 1` si se prueba en PIE el viaje sin cortes. Ver [Salas](Salas.md#viaje-de-mapa-fallido). |
| `Automation RunTests Tortunabo.Net.TravelFailure` | Prueba automática de lo que se hace ante un viaje fallido (anfitrión, invitado, menú, lobby en pie, segundo fallo) y de si el `ServerTravel` ha arrancado de verdad. |

## Cooperativo (mapa procedural y lobby)

Con ocho jugadores (PIE con 8 jugadores y modo «Listen server», o `-game` con ocho clientes): en el lobby, los cuatro primeros
aparecen en las `Salida_*` y del quinto al octavo, junto a ellas (`[Lobby] Todos los PlayerStart ocupados: sitio nuevo…` en
el log del anfitrión); ocho huevos en la pila y «Sala: 8/8»; al viajar, ocho sitios en la sala o en los huevos y, al acabar,
los resultados con ocho filas. `TN.Proc.StartStyle 0|1` fuerza cómo se sale.

| Comando | Qué hace |
|---|---|
| `TN.Proc.StartStyle 0` / `TN.Proc.StartStyle 1` | Salida por puerta doble (`0`) o con huevos (`1`) desde la siguiente generación del mapa; `-1` = lo del lobby. |
| `TN.Proc.Egg` | Con la salida con huevos: los cierra otra vez con cada tortuga dentro y a los 1,5 s los vuelve a romper (1 s en el huevo y salen despedidas), sin regenerar el mapa. |
| `TN.Fauna.Enable 0` / `TN.Fauna.Stats 1` | Esconde la fauna ambiental / saca sus métricas en el log. |
| `TN.Lobby.Castle 0` / `TN.Lobby.Valley 0` | Esconde el castillo o el valle del lobby (al recargarlo). |
| `TN.Storm.Cough 1` / `TN.Storm.Cough 2` | Carraspeos sueltos (`1`) o tos fuerte (`2`) de la tormenta sin tormenta; `0` la apaga. |

## Tortuga: cara, voz, HUD y panzazo

| Comando | Qué hace |
|---|---|
| `tn.Face.Mood 0-3` | Fuerza la cara: 0 feliz, 1 cansada, 2 jadeando, 3 tumbada. `-1` = la real. |
| `tn.Face.Tongue 0-3` | Fuerza la lengua: 0 dentro, 1 al viento, 2 colgando, 3 la punta. `-1` = la real. |
| `tn.Face.Talk 1` | Todas mueven la boca como si hablaran. |
| `tn.HUD.Face 0-5` | Fuerza la cara del HUD: 0 feliz, 1 cansada, 2 jadeando, 3 caparazón, 4 mareada, 5 victoria. `-1` = la real. |
| `tn.HUD.Energy 0.3` | Fuerza la energía del salvavidas (0-1). `-1` = la real. |
| `tn.HUD.Talk 1` | Fuerza el bocadillo de voz; `0` lo apaga. `-1` = el real. |
| `tn.HUD.CrewPreview 3` | Rellena 3 filas de tripulación contigo para ver el diseño. |
| `TN.Voice.Steps 1` / `TN.Voice.Steps 2` | Pasos de prueba en el sitio: `1` andando, `2` corriendo; `0` los apaga. |
| `TN.Voice.Pant 1` / `TN.Voice.Pant 2` | Jadeo de prueba: `1` suave, `2` agotada; `0` lo apaga. |
| `TN.Voice.Drag 1` / `TN.Voice.Drag 2` | Arrastre de panzazo de prueba: `1` lento, `2` rápido; `0` lo apaga. |
| `TN.SlopeTilt.Enable 0` | El modelo ya no se inclina con la pendiente (para comparar); `1` (lo normal) lo inclina. En `-game`: `-dpcvars=TN.SlopeTilt.Enable=0`. |
| `TN.SlopeTilt.Find [lado\|frente] [espera_s] [captura]` | Pone tu tortuga en la cuesta de 15-30° más cercana (hasta 120 m), de lado (por defecto) o de frente, con la cámara donde se ve la inclinación. Con `captura` = 1, a los 2,5 s escribe el estado y hace `HighResShot`. En el anfitrión o sin red; `espera_s` sirve para lanzarlo con `-ExecCmds`. |
| `TN.SlopeTilt.Dump [espera_s]` | Escribe en el registro la inclinación de cada tortuga en esa máquina (actual, objetivo y la de la malla), para comparar el anfitrión con un cliente. |
| `TN.SlopeTilt.DiveProbe [abajo\|arriba] [espera_s] [captura]` | Corre hacia la cuesta de 15-30° más cercana (cuesta abajo por defecto), salta y hace el panzazo; escribe por fotograma en el registro la malla respecto al suelo en el vuelo, el aterrizaje y el deslizamiento, y al final el mayor giro en un fotograma. Con `captura` = 1, una `HighResShot` en el vuelo y otra deslizando (la captura frena ese fotograma: las cifras, sin ella). |
| `TN.Dive.Debug 1` | Datos del deslizamiento del panzazo. |
| `TN.Music.Play Victoria` | Hace sonar una pista: `Victoria`, `Derrota`, `Eliminado`, `Tienda`, `Probador` o `Silencio`. |

## Tutorial de la primera partida

Estos se escriben en la ventana de **quien lo prueba** (anfitrión o cliente): cada ventana de PIE tiene su guardado. Para
reiniciarlo al arrancar sin consola, `Saved/ResetTutorial.txt` (vacío = todas las ventanas; `0 1` = solo esas). Detalle en
`Docs/Tutorial.md`.

| Comando | Qué hace |
|---|---|
| `TN.Tutorial.Reset` | Deja el tutorial por hacer en esta ventana: empieza al llegar al siguiente lobby. |
| `TN.Tutorial.Start` | En el lobby: empieza ahora desde la salida (aunque ya esté hecho o se esté dentro). |
| `TN.Tutorial.Skip` | Lo salta como el menú de pausa: baja a la plaza del castillo y queda apuntado como hecho. |
| `TN.Tutorial.Station 12` | Lleva a la estación 12 (1-19) y mete en el tutorial si hace falta. Sin número, escribe la lista. |
| `TN.Tutorial.Info` | Ranura del guardado, hecho o no, dentro o fuera, estación y cuántos hay dentro (en el servidor). |
