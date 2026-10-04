# Sonido de la tortuga: pasos, jadeo, panzazo y tos

Todo sintetizado en tiempo real, sin archivos de audio, como el resto del juego (ambiente, música, parque del lobby, tos
de la tormenta). Cartoon pero creíble: patas blandas (almohadilla y dedos), nada de botas.

| Qué | Dónde | Cuándo |
|---|---|---|
| Pasos andando, más rápidos y fuertes corriendo | `UTN_TurtleFoleyComponent` | Cada pisada de la animación, con el timbre de la superficie |
| Impulso al saltar y golpe al aterrizar | `UTN_TurtleFoleyComponent` | Al despegar (subiendo a más de 1,5 m/s) y al caer (más de 0,12 s en el aire o a más de 2,5 m/s) |
| Jadeo | `UTN_TurtleFoleyComponent` | Estamina por debajo del 45 %; a tope al agotarse; se calma al recuperarse |
| «Plaf» del panzazo | `UTN_TurtleFoleyComponent` | Al caer de tripa, más fuerte cuanto más rápido caía y corría |
| Arrastre sobre la tripa | `UTN_TurtleFoleyComponent` | Mientras se desliza tras el panzazo, con la fuerza de la velocidad y el timbre de la superficie |
| «Tonc» del caparazón | `UTN_TurtleFoleyComponent` | Al chocar contra algo arrastrándose (la velocidad cambia de golpe) |
| Brazadas | `UTN_TurtleFoleyComponent` | Nadando, una por ciclo de la brazada de la animación, más fuertes cuanto más rápido nada |
| Meterse y salir del caparazón | `UTN_TurtleFoleyComponent` (lo pide `UTN_ShellComponent`) | Al cambiar de estado, en cada máquina; los assets quedan de respaldo |
| Tos | `UTN_StormCoughComponent` (ya existía) | Dentro de la tormenta del Coop; en la tormenta el jadeo calla |

## Arquitectura

- `Player/TN_TurtleFoleyComponent.h/.cpp`: `USynthComponent` que `ATortugaCharacter::BeginPlay` crea con
  `FindOrAddTo(this)` en cada máquina con audio (nada en servidor dedicado; transitorio, adjunto a la cápsula).
- `Private/Player/TN_TurtleFoleyDSP.h`: el motor (`TNTurtleFoley::FEngine`), C++ puro (solo `CoreMinimal` y `<atomic>`),
  calibrado en un arnés fuera del motor. Corre en el hilo de render de audio: sin UObjects, asignaciones ni bloqueos.
- Estado local por máquina, sin RPC: cada fotograma lee lo replicado de la tortuga (velocidad, en el suelo o en el aire,
  nadando, sprint, estamina, agotada, derribada, muerta, caparazón, panzazo, llevar o ser llevada) y los huesos de su malla.
- La superficie la resuelve `Player/TN_TurtleSurface` (`TNTurtleSurface::Probe` / `Resolve`), compartida con el
  rozamiento del arrastre del panzazo (`UTN_TurtleMovementComponent`) y su polvo (`UTN_TurtleDustComponent`).
- Fuente 3D mono en la raíz de la tortuga: volumen pleno hasta 3 m, caída natural hasta 27 m y agudos que se apagan con
  la distancia. La tortuga local suena ×1,3.
- Volumen de Efectos de los Ajustes: el sintetizador se queda en la clase de sonido por defecto, la que baja
  `UTN_GameSettingsSubsystem` con Efectos (pasos, jadeo, panzazo, brazadas y caparazón).
- Hilos: el juego deja las pisadas en un anillo de un escritor (`FSharedParams::PushStep`) y los objetivos (volúmenes,
  jadeo, callar el jadeo) en atómicos. Cada generador lee el anillo con su propio cursor y solo si su arranque es el
  vigente (`RunId`, lo fija `Init`): el de un arranque anterior que aún suene no roba ni repite pasos. `Busy` vuelve
  del audio al juego para no parar el sintetizador a media respiración.
- Coste: el sintetizador arranca con la primera pisada o el primer jadeo y se para tras 2,5 s sin nada (y con el
  generador callado) o lejos del oyente; lejos, el componente pasa a tick de 4 Hz. Una traza de línea por pisada como
  mucho (con caché de 0,15 s / 30 cm) y solo para tortugas que se oyen.

## Pasos

**Sincronía con la zancada.** Se leen los huesos `LeftFoot` y `RightFoot` (Mixamo; también `foot_l`/`foot_r` o los
dedos si cambia la malla) en el espacio de la malla, en `TG_PostPhysics` (la pose ya está evaluada). El «suelo de los
pies» es el pie más bajo, con subida lenta (8 cm/s) para seguir el bamboleo de la cadera de la carrera. Un pie que sube
más de 2 cm queda armado y suena al volver por debajo de 0,9 cm o, si la zancada alargada (`Amplify` del clip de andar)
lo deja un poco en el aire, cuando deja de bajar cerca del suelo (talón). Así suena igual con el clip `Walking` que con
la carrera procedural de `UTN_TurtleAnimInstance` (que pisa en el punto más bajo). Mínimo 0,12 s entre pisadas del mismo
pie.

**Reloj de reserva.** Si la malla no tiene esos huesos, o lleva 0,8 s moviéndose sin que los huesos den pasos (malla sin
animar), un reloj al ritmo de la animación: la cadencia de la carrera (dos pasos por ciclo de `0,8 + v/300` ciclos/s,
entre 2 y 3,4) o, andando, el largo de paso aprendido de los propios huesos (64 cm al empezar).

**Fuerza.** Andando, de 0,35 a 0,66 según la velocidad; corriendo, de 0,88 a 1; agotada, un 6 % más pesados; llevando
a otra tortuga en alto, más graves y pesados. Aterrizaje de 0,45 a 1,35 según la velocidad de caída (las dos patas casi
a la vez). Cada pisada varía al azar tono (±8 %), nivel (de -1,5 a +0,7 dB), brillo y separación almohadilla-dedos; la pata derecha suena
un pelín más aguda y cada tortuga tiene su tono de pata (sale de su semilla).

**Sin pasos** en el aire, nadando, en el caparazón, en panzazo, llevada, derribada o muerta, ni por debajo de 60 cm/s.

**Superficie** (pesos que se mezclan en el sintetizador: arena, tierra, roca, madera, agua):

- Mapa procedural, terreno: mezcla de biomas del sitio (`FLayout::BiomeWeightsAt`).

  | Bioma | Suena a |
  |---|---|
  | Playa, desierto | Arena |
  | Agua con isletas | Arena húmeda (80 % arena, 20 % tierra) |
  | Selva | Tierra y hojarasca (15 % arena) |
  | Manglar | Fango (70 % tierra, 30 % agua) |
  | Volcán | Roca con ceniza y grava (45 % arena) |
  | Acantilados | Roca (20 % arena) |
  | Zona humana | Tierra pisada y empedrado (50/50) |

  En pendientes (normal por debajo de 0,9, del todo a 0,7), hasta un 80 % de roca, como se pinta el talud.
- Agua poco profunda: pisando terreno que queda bajo el nivel del agua del mapa (`TNProcMap::SeaLevel`, el mismo para
  mar, lagunas y río), chapoteo desde 2 cm de profundidad y del todo desde 13 cm. Solo sobre el terreno (un puente no
  chapotea). Las pozas de los toboganes tienen su propio nivel y no se detectan.
- Estructuras del mapa (a más de 35 cm del terreno): la sección 1 de `StructureMesh` son los tablones (madera); el resto
  (piedra, hierro pintado), roca. Hace falta la traza compleja con índice de cara.
- Decorado con malla estática del mapa y todo lo de fuera del mapa: por palabras en el nombre del componente, la clase y
  el nombre del actor, la malla y el material (agua, arena, madera, tierra, roca, por ese orden; el castillo de arena del
  lobby suena a arena y el puente bamboleante a madera). Sin palabras conocidas, roca (el «pat» neutro). Encima de otra
  tortuga, su caparazón suena hueco (madera).

**Síntesis de una pisada.** Fuerza de apoyo en dos contactos (almohadilla y dedos, 18-45 ms entre ellos; más juntos
corriendo) que excita el golpe sordo de la pata (seno grave que cae de ~150 a ~95 Hz) y un «pat» apagado, más la capa de
cada superficie: arena (granos densos de ~1 ms por un paso banda de 2,4-3,4 kHz sobre un «shhh», que se hunde),
tierra (golpe muy apagado a ~300 Hz y crujidos sueltos de hojas), roca (chasquido suave, un «toc» corto a 1,1-1,7 kHz y
arenilla), madera (tablón libre golpeado con algo blando: modos 1 : 2,76 : 5,4 sobre ~200 Hz) y agua (chapoteo que
baja de 3 kHz a 1 kHz, masa de agua grave y hasta tres burbujas que suben de tono).

## Panzazo

El movimiento del arrastre está en `Docs/Animacion_Tortuga.md` («Panzazo: arrastre sobre la tripa»). El sonido solo lee
`ATortugaCharacter::IsBellyPoseActive` / `IsBellyOnGround` y la velocidad replicada, así que suena igual en todas las
máquinas. Durante el panzazo no hay pasos; en cuanto se levanta, vuelven.

- **«Plaf»** (`StepKind::Belly`), al caer de tripa: fuerza de 0,5 a 1,4 (0,55 más la caída por encima de 1,5 m/s entre 9
  más la velocidad entre 25 m/s). Toda la tripa a la vez: los dos contactos casi juntos (4-12 ms), las caídas 1,8 veces
  más largas, el golpe sordo mucho más grave (×0,62) y un chasquido de carne blanda (ruido por un paso alto de
  0,8-1,2 kHz que dura lo que el golpe). Encima, la capa de la superficie: en el agua, un chapuzón con burbujas.
- **Arrastre** (`FDragVoice`, continuo): fuerza = (velocidad sobre la tripa − 25 cm/s) hasta 650 cm/s (`MinDragSpeed`,
  `DragFullSpeed`) con curva suave, y viveza = velocidad / 845 cm/s. Entra en 30 ms y se apaga en 100 ms (el cuerpo aún
  roza al pararse). Más deprisa, filtros más agudos y granos más seguidos. Fondo grave del cuerpo que roza (dos pasos
  bajos a 160 Hz) en todas las superficies, más la de cada una por su peso:

  | Superficie | Suena a |
  |---|---|
  | Arena | «Shhh» granulado (paso banda de 1,9 a 3,6 kHz con granos de 1 ms, de 700 a 2600 por segundo) y siseo de fondo |
  | Tierra | Roce sordo (0,5-1 kHz) y crujidos de hojas sueltas (10-45 por segundo, 3,8 kHz) |
  | Roca | Raspado a trompicones, que se pega y se suelta (1,4-2,6 kHz, 9-40 tirones por segundo) y arenilla aguda |
  | Madera | Roce (0,65-1,4 kHz) y zumbido del tablón (modos a 190 y 520 Hz excitados por el ruido) |
  | Agua | Siseo de la estela (0,9-2,4 kHz), masa de agua que empuja (380 Hz) y burbujas sueltas que suben de tono |

  Unos baches (el nivel sube y baja un 22 % cada 40-110 ms) para que no suene a ruido plano. La superficie se mira con
  la misma traza y caché que los pasos (0,15 s o 30 cm).
- **«Tonc»** (`StepKind::Bump`), al chocar arrastrándose (la velocidad horizontal cambia más de 2,6 m/s de un fotograma
  a otro yendo a más de 1,8 m/s; como mucho uno cada 0,25 s): el golpe sordo del cuerpo (×0,75) y los modos del tablón
  afinados al caparazón (270-340 Hz), sin la textura del suelo.

Los «plaf» y «tonc» suenan aunque el Blueprint tenga `FootstepSound` (no son pasos).

## Guardar y sacar del caparazón

Al cambiar de ranura del inventario (o al sacar lo guardado porque se ha gastado lo de la mano), la aleta va a la
espalda y el objeto entra o sale del caparazón (`Docs/Animacion_Tortuga.md`, «Objetos en las aletas»). En ese momento
`UTN_InventoryComponent` pide `UTN_TurtleFoleyComponent::PlayStash`, en cada máquina y sin red: un «toc» hueco y corto
(`StepKind::Stash`) con los mismos modos del caparazón que el «tonc», sin suelo, más agudos (330-380 Hz al guardar,
440-520 Hz al sacar), más flojos (fuerza 0,6 y 0,5) y algo más cortos. Suena aunque el Blueprint tenga `FootstepSound`.

## Nado: brazadas

Mientras nada (`IsSwimming`, sin estar muerta, derribada, en el caparazón ni llevada), una brazada (`StepKind::Stroke`)
por cada ciclo de la brazada de `UTN_TurtleAnimInstance` (`PoseSwim`, 0,85 ciclos/s: una cada 1,18 s), cuando su fase
(`GetSwimStrokePhase`) pasa por 0,25, con la aleta estirada (`TNTurtleFoley::CrossedPhase`). Si la animación no se
evalúa (malla sin ver), un reloj propio al mismo ritmo sigue desde la última fase leída.

- Solo agua, sin pata ni suelo: chapoteo de banda que entra despacio (45-70 ms) y baja de 1,5-2,2 kHz a 550-700 Hz, masa
  de agua grave (260-380 Hz) con cola de 0,11-0,17 s y una a tres burbujas detrás de la aleta. Fundido de 60 ms al final.
- Fuerza de 0,35 flotando en el sitio a 0,85 a la velocidad máxima nadando (`MaxSwimSpeed`); con más fuerza, más agua y
  más viva.
- Suena aunque el Blueprint tenga `FootstepSound` (no son pasos). Volumen: `StepLoudness`.

## Meterse y salir del caparazón

`UTN_ShellComponent::ApplyShellState`, que corre en todas las máquinas al cambiar de estado, pide
`UTN_TurtleFoleyComponent::PlayShell` (`StepKind::Shell`, sin red):

- Meterse: la piel roza hacia dentro (ruido de banda que barre de 2,2-2,8 kHz a 600-800 Hz en 80-100 ms) y acaba en un
  «tonc» grave del cuerpo contra la concha (modos del caparazón a 230-280 Hz). Fuerza 0,75.
- Salir: un «pop» que sube de tono y el roce hacia fuera (de 700-900 Hz a 2,2-2,8 kHz), con la concha más aguda
  (340-400 Hz). Fuerza 0,6.
- Respaldo: con `bSynthShellSounds` a false en el Blueprint del componente (o sin sintetizador, como en un servidor
  dedicado) suenan `EnterShellSound` y `ExitShellSound`, como antes.

## Jadeo

- Objetivo 0..1: nada por encima de `PantBelowStamina` (45 % de la estamina máxima con el peso que lleva), sube con curva
  suave hasta el 5 % y a tope al agotarse (`IsExhausted`). Sube en medio segundo y se calma con `PantCalmSeconds` (2,5 s):
  la estamina se recarga deprisa, el jadeo no.
- Respiraciones por la boca con el modelo de la tos (aire y voz por tres formantes): espiración «hah» con la boca
  abierta y la lengua fuera, inspiración «hhh» más aguda y floja. De ~1 ciclo por segundo con el jadeo flojo a ~2,4 al
  agotarse, cada vez más fuerte y, desde la mitad, con algo de voz («huh»). Al calmarse tras un jadeo fuerte, suspira.
- La voz sale de la semilla del jugador con la misma cuenta que la tos (`PlayerId`): jadeo y tos son la misma tortuga.
- En la tormenta manda la tos: mientras `UTN_StormCoughComponent` tose (intensidad > 0 o aún sonando su último
  carraspeo) el jadeo calla al instante (`PantHush`) y vuelve después si sigue cansada. Muerta o derribada, tampoco.

## Tos de la tormenta (comprobación)

La tos ya estaba hecha y enganchada; revisado el camino entero sin encontrar fallos:

1. `ATN_ProcMapGameMode::StartStormIfNeeded` crea `ATN_PathStorm` solo en Coop y con `StormSpeed > 0` en el perfil (en
   el nivel de solo terreno no hay tormenta ni tos).
2. `ATN_PathStorm::Tick` → `TickCough` cada 0,1 s en cada máquina con audio: a cada tortuga viva con
   `ATN_CoopPlayerState` le da su `UTN_StormCoughComponent` (`FindOrAddTo`) y le pasa si está dentro según el frente
   replicado y qué fracción lleva del tiempo que mata; a las muertas las calla.
3. El componente arranca su sintetizador al toser (siempre en la tortuga local; las demás si el oyente está a tiro) y se
   para al callar o lejos.

Con `TN.Voice.Debug 1` la línea de cada tortuga dice `sin tos` (aún no tiene el componente), `tos 0.00` (lo tiene, fuera
de la tormenta) o `tos 0.45 sonando`. Nota: con `TNStorm <sitio>` la tormenta es inofensiva solo en el servidor
(`DebugPlaceFront` no replica `SecondsInsideToDie`): en un cliente la tos llega a lo más fuerte a los 5 s dentro, en el
servidor a los 12 s.

## Ajustes y consola

`UPROPERTY` del componente (se pueden tocar en caliente en el panel de detalles durante PIE): `Loudness`,
`StepLoudness`, `BreathLoudness`, `LocalPlayerBoost` (1,3), `PantBelowStamina` (0,45), `PantCalmSeconds` (2,5),
`MinStepSpeed` (60 cm/s), `DragLoudness` (1: arrastre, «plaf» y «tonc»), `MinDragSpeed` (25 cm/s), `DragFullSpeed`
(650 cm/s), `InnerRadius` (300 cm) y `FalloffDistance` (2400 cm).

| Consola | Qué hace |
|---|---|
| `TN.Voice.Volume <x>` | Volumen de pasos y jadeo de todas las tortugas (1 normal, 0 apagado) |
| `TN.Voice.Surface <-1..4>` | Fuerza la superficie: -1 la de verdad, 0 arena, 1 tierra, 2 roca, 3 madera, 4 agua |
| `TN.Voice.Steps <0\|1\|2>` | Pasos de prueba en el sitio en la tortuga local: 0 apagados, 1 andando, 2 corriendo |
| `TN.Voice.Pant <0\|1\|2>` | Jadeo de prueba en la tortuga local: 0 manda la estamina, 1 suave, 2 agotada |
| `TN.Voice.Drag <0\|1\|2>` | Arrastre de prueba en el sitio en la tortuga local: 0 manda el panzazo, 1 lento, 2 rápido (con `TN.Voice.Surface` para comparar superficies) |
| `TN.Voice.Swim <0\|1\|2>` | Brazadas de prueba en el sitio en la tortuga local, al ritmo del nado: 0 manda el nado, 1 flotando, 2 a toda velocidad |
| `TN.Voice.Shell <0\|1>` | Sonido de meterse (1) o salir (0) del caparazón en la tortuga local, sin cambiar su estado |
| `TN.Voice.Debug 1` | Una línea por tortuga que se oye: velocidad, pasos por huesos o por reloj, superficie, estamina, jadeo y tos |
| `TN.Storm.Cough <0\|1\|2>` | (ya existía) Tos de prueba en la tortuga local sin tormenta |

Compatibilidad: si el Blueprint asigna el `FootstepSound` de siempre (hoy vacío en `BP_TortugaCharacter`), los pasos
sintetizados callan para no doblarse; el salto, el aterrizaje sintetizados y el jadeo siguen.

## Niveles (arnés fuera del motor, Master 1)

| Sonido | Pico |
|---|---|
| Paso andando (fuerza 0,65) | -16 a -19 dBFS según superficie (madera y agua, 1-2 dB más) |
| Paso corriendo (0,95) | -11 a -14 dBFS |
| Aterrizaje fuerte (1,2) | -6 a -10 dBFS |
| Impulso del salto | -19 a -23 dBFS |
| Jadeo flojo (0,3) / fuerte (0,7) / agotada (1) | -22 / -16 / -13 dBFS |
| Brazada flotando (0,35) / a toda velocidad (0,85) | -35 a -31 / -23 a -18 dBFS |
| Meterse / salir del caparazón | -15 a -13 / -20 a -16 dBFS |

Las brazadas y el caparazón los mide el test `Tortunabo.Audio.TurtleFoley.SwimAndShellLevels` (doce semillas por caso):
sin NaN, por debajo del limitador con Master 1, audibles y en silencio al acabar.

El limitador de salida (-2 dBFS) solo actúa con el refuerzo de la tortuga local en las caídas más fuertes. Todo por
debajo de la tos (golpes de tos fuerte de -7 a -1,5 dBFS).

El panzazo aún no ha pasado por el arnés: el arrastre está estimado a unos -28 dBFS eficaces a fondo (por el ancho de
banda de cada filtro, `DragTrim`), el «plaf» parte del aterrizaje fuerte (golpe más largo y grave más el chasquido,
`Trim::Slap` 0,3) y el «tonc» del tablón (`Trim::Shell` 0,009). Se afinan de oído con `DragLoudness` y `TN.Voice.Drag`.

## Probar en PIE

1. `TN.Voice.Debug 1` y andar: la línea local dice `pasos: huesos` (si dice `reloj`, la malla no da pasos: ver arriba) y
   la superficie de debajo. Correr (sprint): pasos más seguidos, fuertes y vivos.
2. Mapa procedural: pasar por playa (arena), selva (tierra), acantilados (roca), un puente de tablones (madera) y la
   orilla metiéndose en el agua poco profunda (chapoteo). `TN.Voice.Surface 0..4` con `TN.Voice.Steps 1` para comparar
   las cinco superficies en el sitio.
3. Saltar desde alto: impulso al despegar y golpe al caer, más fuerte cuanto más alto.
4. Esprintar hasta agotarse: jadeo cada vez más seguido y con voz al agotarse; al parar se calma en unos segundos y
   suspira. `TN.Voice.Pant 1` / `2` para oírlo sin correr.
5. Coop con tormenta: dejar que el frente alcance a la tortuga (o `TNStorm desierto`): tose y, si venía jadeando, el
   jadeo calla; al salir, un último carraspeo y vuelve el jadeo si sigue cansada.
6. Con dos jugadores: los pasos y el jadeo del otro se oyen en 3D y se apagan con la distancia; los propios, algo más altos.
7. Panzazo en arena, tierra, roca, un puente de tablones y la orilla: «plaf» al caer y el arrastre de cada superficie,
   que se apaga al pararse; contra una pared, «tonc». En el sitio: `TN.Voice.Drag 2` con `TN.Voice.Surface 0..4`
   (`TN.Voice.Drag 0` para volver).
8. Nadar: una brazada por cada ciclo de las aletas, más fuerte nadando deprisa que flotando. En el sitio: `TN.Voice.Swim 1`
   y `2` (`TN.Voice.Swim 0` para volver).
9. Meterse y salir del caparazón (Ctrl, B o Círculo): roce hacia dentro y «tonc» grave; al salir, «pop» y roce hacia
   fuera. `TN.Voice.Shell 1` / `0` para oírlo sin cambiar de estado.

## Música de fondo de la carrera («Marcha de la Playa»)

Tarea 8 de `Docs/Archivo/Plan_Carrera_Ronda4.md` (eliminado). Sintetizada en C++ en tiempo real, sin archivos de audio, como el resto de la música
del juego, y solo en la carrera de la playa. Suena de fondo (por debajo de efectos, voces y avisos), en la categoría **Música**
del menú de pausa, con una capa de tensión y un «ducking» suave que sigue el estado que ya replica `ATN_BeachRaceGameState`.

**Por qué en el motor y no en WAV importados** (la otra vía que proponía el plan): la tensión y el «ducking» se mueven sin
costuras con el estado de la partida (con WAV harían falta varios *stems* sincronizados y volúmenes por script), el bucle no
tiene costura por construcción (es un secuenciador que sigue sonando, no un archivo que se corta), no hay script de
importación ni assets binarios que versionar, y comparte instrumentos, categoría de volumen y forma de probarse con la música
de la tienda y de fin de partida. Se ha medido fuera del motor (ver «Arnés»).

### La pieza

Mi bemol mayor, 116 BPM, 4/4, **32 compases** (A – A' – B – A'', 66 s) que se repiten con relevo de solistas: en la segunda
vuelta la flauta y el steel pan se cambian los papeles, así que la pieza dura **132 s** antes de sonar igual. Una banda
militar de vacaciones en la playa:

| Capa | Qué suena |
|---|---|
| Ritmo (1) | «Oom-pah» de marcha en el bajo pizzicato (negras con la quinta en el contratiempo; saltarín en A'; a mitad de tiempo en el puente), caja de marcha con notas fantasma y redobles de relleno (último tiempo del compás 4, media medida del 8; en el puente cede el sitio a una caja china en los tiempos 2 y 4), shaker en corcheas, congas en tresillo, platillo suave al empezar cada sección (menos el puente) y, al empezar el puente, la campana del barco («ding-ding») |
| Armonía (2) | Marimbas a contratiempo (tercera y quinta del acorde), arpegio de corcheas en A' (marimba y kalimba), golpes sueltos en el puente |
| Melodía (4) | Steel pan (con trémolo en las notas largas) y flauta de banda (piccolo suave con «chiff» y vibrato tardío); en A'' de la primera vuelta van los dos juntos. Tema de A: arpegios de corneta (Mib-Sol-Sib) y respuesta que resuelve en la tónica; tema del puente: corcheas en arco |
| Corneta (8) | Toque Sib-Mib-Sol-Sib al final de A' y de A'', justo antes de que vuelva el tema una octava por encima, y en la introducción |
| Tensión (16) | Ver abajo |

La melodía es diatónica a Mi bemol mayor, sin saltos de más de una sexta, y en A todos los tiempos fuertes caen en notas del acorde; en el puente y en el compás 4 de A' hay unas pocas séptimas y novenas que resuelven por grados conjuntos (comprobado con un script). Al entrar
desde el silencio empieza con **dos compases de introducción** (redoble que crece, timbal y toque de corneta) y el tema
entra en el compás 1. Sala pequeña (Freeverb, ~0,8 s) para que suene lejos, de fondo.

**Tensión (`Tension` 0..1)**, capa que se suma a la base según sube el parámetro (el motor lo suaviza: sube en 2 s, baja en
3 s; con menos de 0,05 no cuesta ni una voz): redoble continuo de caja en semicorcheas, bajo a corcheas (desde 0,14), timbal
de pulso a negras (0,22), shaker en semicorcheas (0,30), ostinato de kalimba en tresillo (0,38), metales en los compases pares
(0,62) y hasta un 6 % más de tempo. Las notas nuevas entran con velocidad creciente: nada se corta al subir ni al bajar.

**«Ducking» (`Duck` 0..1)**: baja hasta -10 dB y cierra un paso bajo (de 16 kHz a 2,2 kHz), entra en 0,3 s y sale en 0,9 s.

Niveles medidos (arnés, RMS / pico en dBFS con el volumen de Música y `TN.Race.Music.Volume` a 1): base -29 / -10;
tensión 0,5: -28 / -9; tensión 1: -26 / -7; base con «ducking» 0,5: -34 / -15; con «ducking» 1: -40 / -21. Para comparar,
los pasos andando tienen picos de -16 a -19 dBFS y la música de fin de partida ronda los -18 a -22 dBFS de RMS: esta va unos 8 a 10 dB
por debajo. El deslizador de Música solo puede bajarla (1 es lo máximo del menú); si se quedara corta o larga, se retoca
`kOutputTrim` en `TN_RaceMusicDSP.h` (un solo número) o se prueba antes con `TN.Race.Music.Volume`.

### Cuándo suena (`TNRaceMusic::FDirector`, `TN_RaceMusicDirector.h`)

En cada máquina con audio y solo para su jugador local. Decide diez veces por segundo a partir de `RacePhase`,
`bSprintFinal`, `PhaseSecondsLeft`, `FinishCountdown` y `GetRoundTimeLeft()` del GameState y del PlayerState local.

| Momento | Música | Tensión | «Ducking» |
|---|---|---|---|
| Preparando la ronda (`Waiting` sin cuenta; la primera ronda tras el viaje sale del huevo de la pantalla de carga) | Callada | | |
| Cuenta 3, 2, 1 de salida (`Waiting` con `PhaseSecondsLeft` > 0) | Entra (fundido de 1,5 s, con la introducción) | 0 (0,5 en el sprint final) | 0,55 |
| Carrera (`Racing`) | Suena entera; se abre al dar la salida | 0 | 0 |
| Último minuto del tiempo de la ronda | Igual | de 0,35 a 1 | 0 |
| Cuenta de 10 s tras la primera en el agua | Igual | de 0,7 a 1 | 0,5 (0,65 los últimos 3 s) |
| Sprint final (todo el `Racing` del sprint) | Igual | al menos 0,5 | 0 |
| «¡TIEMPO!» / «¡TODAS AL AGUA!» (`TimeUp`, `AllIn`) | Se calla (0,8 s) | | |
| Recuento (`RoundResults`), título del sprint (`SprintIntro`) y podio (`Champion`) | Se calla (1 s): ya llevan su música de victoria o derrota | | |
| El jugador local ya ha llegado (`bHasFinishedRun` sin eliminado) | Se calla (1 s) | | |
| Suena la música de fin de partida del jugador (victoria, derrota o eliminado) | Se calla y espera 1,6 s tras su final | | |

El podio no la deja de fondo bajita, sino que se aparta del todo: bajo la victoria o la derrota del podio, otra música en otra
tonalidad sería un choque. Si prefieres que el podio la deje sonar muy baja, es cuestión de cambiar el caso `Champion` del
director. Un corte de la decisión por una fase transitoria (entre la cuenta de salida y `Racing` hay un instante con `Waiting`
y cuenta a 0) se aguanta 0,45 s antes de callar.

### Archivos

| Archivo | Qué es |
|---|---|
| `Private/Audio/TN_RaceMusicDSP.h` | Motor y composición en C++ puro (sin UObjects): `TNRaceMusic::FEngine`. Reutiliza las voces de `TN_MusicSynthDSP.h` (resonadores modales, caja, metal, ruido, platillo, limitador) y añade la flauta, la sala, el secuenciador de semicorcheas, la tensión y el «ducking» |
| `Private/Audio/TN_RaceMusicDirector.h` | Lógica pura de «cuándo y cuánto» (`FDirector`), probada fuera del motor |
| `Public/Audio/TN_RaceMusicComponent.h`, `Private/Audio/TN_RaceMusicComponent.cpp` | `UTN_RaceMusicComponent`: sintetizador 2D. Hereda de `UTN_MusicSynthComponent` solo para caer en la categoría Música de `UTN_GameSettingsSubsystem::ClassFor` (`IsA<UTN_MusicSynthComponent>`) sin tocar ese archivo; el motor de temas de la base no se usa |
| `Private/Audio/TN_RaceMusicSubsystem.h/.cpp` | `UTN_RaceMusicSubsystem` (subsistema de mundo): vigila el GameState, aplica lo que decide el director y crea el componente en el PlayerController local. No toca el GameMode ni replica nada. Consola de pruebas |
| `Tools/RaceMusic/` | Arnés fuera del motor (ver abajo) |

Hilos: el hilo de juego solo escribe atómicos (`FRaceMusicParams`: sonar sí o no y fundido, volumen, tensión, «ducking», capas);
el hilo de render de audio lee una vez por bloque de 32 muestras y hace todo lo demás, sin asignaciones ni bloqueos. El
componente se crea la primera vez que hace falta sonar, se destruye tras 20 s en silencio y al desmontarse el mundo (el
PlayerController viaja al lobby sin ella). Un solo sintetizador estéreo; con la música callada no calcula nada.

### Consola

| Comando o variable | Qué hace |
|---|---|
| `TN.Race.Music.Play [tensión] [duck]` | La hace sonar en **cualquier mapa** (también en el lobby o el cooperativo) con esa tensión y ese «ducking» (0 a 1); sin argumentos, los que decida la partida (0 y 0 fuera de la carrera) |
| `TN.Race.Music.Stop` | La calla (fundido) |
| `TN.Race.Music.Auto` | Vuelve al modo normal: quita `Force`, `Tension`, `Duck` y `Layers` |
| `TN.Race.Music.Restart` | Vuelve a empezar desde la introducción |
| `TN.Race.Music.Status` | Escribe en el registro qué decide el director y cómo va el motor (compás, vuelta, tensión y «ducking» suavizados) |
| `TN.Race.Music.Tension <0..1>` / `TN.Race.Music.Duck <0..1>` | Fuerzan la capa de tensión o el «ducking» (`-1` = automático) |
| `TN.Race.Music.Layers <máscara>` | Capas sonando: 1 ritmo, 2 armonía, 4 melodía, 8 corneta, 16 tensión; 31 = todas. `TN.Race.Music.Layers 16` es la capa de tensión sola |
| `TN.Race.Music.Force -1`, `0` o `1` | -1 automático, 0 callada, 1 sonando siempre |
| `TN.Race.Music.Volume <0..1,5>` | Volumen de esta música además del deslizador de Música (1 por defecto) |
| `TN.Race.Music.Debug 1` | Un aviso en el registro por cada cambio de decisión y, cada 5 s, el estado del motor |

Para oírla sola: `TN.Race.Music.Play` en cualquier mapa y, en el menú de pausa, Efectos, Ambiente y Voces a 0.

### Arnés fuera del motor

`Tools/RaceMusic/build.bat` (MSVC de Visual Studio 2022) compila `render.exe`, que usa `TN_RaceMusicDSP.h` y
`TN_RaceMusicDirector.h` tal cual y escribe WAV de 16 bits para oírla y medirla sin abrir el editor:
`render const musica.wav 140 0 0` (140 s con tensión 0 y «ducking» 0), `render const t1.wav 70 1 0`, `render scenario ronda.wav`
(una ronda entera: cuenta de salida apartada, carrera, último minuto, cuenta de 10 s, silencio y la ronda siguiente) y
`render director` (la lógica de decisión, con su lista de casos). Se comprobó sin NaN ni infinitos a 24, 44,1, 48 y 96 kHz y
con bloques de 1, 333, 480 y 1024 muestras; sin voces robadas en los depósitos.

## Golpes del caparazón

Tarea 9 de `Docs/Archivo/Plan_Carrera_Ronda4.md` (eliminado). Con la tortuga en bola (`ATN_ShellBody`, la caja de física), cada choque suena y
levanta un mini efecto según contra qué choca, con la fuerza que da la velocidad del impacto. **Sin red**: cada máquina lo
detecta en su copia de la bola (los clientes también simulan la caja y la réplica la corrige), así que todos los jugadores
cercanos lo ven y oyen sin un byte más y sin multicast; uno no fiable habría costado hasta 8 mensajes por segundo y bola con
8 jugadores para lo mismo. El servidor dedicado no crea el componente ni activa los eventos de golpe.

### Cómo se engancha (sin tocar `TN_ShellBody.*` ni `TN_ShellComponent.*`)

`UTN_ShellImpactFXComponent` (`Public/Player/TN_ShellImpactFXComponent.h`) lo crea `ATortugaCharacter::BeginPlay` con
`FindOrAddTo(this)` (tres líneas en `TortugaCharacter.cpp`, junto al foley y el polvo). Cada fotograma (antes de la física)
mira `UTN_ShellComponent::GetBody()`; cuando aparece una caja, activa en ella «Simulation Generates Hit Events»
(`SetNotifyRigidBodyCollision(true)`) y se apunta a su `OnComponentHit`; cuando cambia o desaparece, se suelta. De paso anota
la velocidad de la caja al entrar en la física del fotograma.

### Fuerza y límites

- **Velocidad del impacto**: el mayor de (impulso normal del motor / masa de la caja) y de lo que la bola llevaba contra la
  superficie al empezar el fotograma. Por debajo de `MinImpactSpeed` (260 cm/s) no pasa nada, así que rodar y los botes pequeños
  no suenan; a `FullImpactSpeed` (1800 cm/s) es el golpe máximo. La fuerza va de 0,1 a 1 con una curva suave.
- **Separación**: 0,07 s entre contactos (el motor manda varios por golpe) y `MinInterval` (0,14 s) entre golpes de una bola, que
  sube hasta 0,42 s cuando el golpe es flojo, salvo que llegue uno claramente más fuerte que el anterior. Como mucho **8
  sonidos por segundo entre todas las bolas del mundo** (de ahí en adelante solo pasan los fuertes, hasta 12). Es la lección
  del «pum, pum, pum» de los bañistas: nada suena solo, en bucle ni sin fuerza.
- **Bola contra bola**: los dos avisan, solo suena uno.
- **Alcance**: el sonido se apaga por distancia (pleno hasta 4 m, caída hasta ~36 m) y no se crean partículas a más de 60 m de
  la cámara local.

### Contra qué choca

| Timbre | Cuándo |
|---|---|
| Otra tortuga | Otra bola o una tortuga de pie (`ATN_ShellBody`, `ATortugaCharacter`) |
| Enemigo | `ATN_BeachEnemy` y sus hijos (cangrejos, gaviotas, pulpo, tanque, pelícano...) |
| Arena | El terreno de la playa (`ATN_BeachRaceGenerator`) y los castillos y fortalezas de arena |
| Roca | El acantilado (`CliffMesh`) y cualquier talud de la playa con la normal por debajo de 0,55 |
| Madera | El bosquecillo (`GroveSolidMesh`), las catapultas, plataformas, cofres y puertas de conchas |
| Trastos de los bañistas | El resto de elementos de la playa (cubos, palas, alambre, trampolines, minas) y el decorado (`ATN_BeachDecorField`), y los objetos de carrera |
| Agua | Al entrar en un volumen de agua a más de la velocidad mínima (la caja sale del caparazón al agua: chapoteo con la velocidad de entrada) |

Fuera de la playa (mapa procedural, lobby) usa `TNTurtleSurface::Resolve`, la misma superficie que los pasos y el polvo (arena
y tierra = arena, roca, madera, agua).

### Sonido (`UTN_ShellImpactSynthComponent`, `TN_ShellImpactDSP.h`)

Sintetizado, sin archivos de audio, con el patrón de `UTN_RaceItemSynthComponent`: mono y espacializado con la atenuación en
código, una cola de disparos sin bloqueos, hasta 8 voces, y solo funciona mientras suena algo (se para a los 2 s de silencio).
Categoría de volumen: **Efectos**. Cada golpe dura de 0,07 a 0,55 s; la fuerza cambia la energía, el brillo y la duración, y el
tono cae un poco con la fuerza y varía un ±7 % al azar.

| Timbre | Síntesis |
|---|---|
| Arena | «¡Fum!»: golpe grave que cae de tono y un soplo de ruido apagado que se cierra hacia lo grave; sin resonancia |
| Roca | «¡Tac!»: chasquido seco y tres resonancias de piedra (~1,2, 2,4 y 3,3 kHz) con el tono al azar; con fuerza, un segundo crujido a los 10 ms |
| Madera | «¡Toc!» hueco: tablón libre (modos 1 : 2,76 : 5,4 sobre ~235 Hz), cuerpo grave y chasquido blando |
| Agua | «¡Plof!»: chapoteo de ruido, una burbuja que sube de tono, «gloop» grave y, con fuerza, hasta 4 gotas agudas |
| Otra tortuga | «¡Clonc!»: caparazón contra caparazón, tres modos huecos (~310, 610 y 985 Hz) y cola de 0,2 a 0,3 s |
| Enemigo | «¡Boing!»: tono que cae con vaivén de goma y un fogonazo de ruido |
| Trastos | «¡Pok!» de plástico y lata: modos altos y secos (~640, 1500 y 2600 Hz) |

Niveles en bruto (arnés, antes de la ganancia del componente, que los deja unos 8 dB más bajos): con fuerza 1, picos de -1 a
-5 dBFS; con 0,5, de -6 a -13; con 0,15, de -16 a -21. Con la ganancia del componente, un golpe máximo queda en torno a -8 dBFS,
por encima de los pasos y por debajo de un aterrizaje duro.

### Efecto visual

Dos emisores de partículas por timbre (`TNAmbientFX`, instancias de mallas de caras planas, las mismas del polvo del panzazo y
los géiseres; sin Niagara): la nube (blanda, del color de lo golpeado) y los trocitos: granos de arena, esquirlas de roca,
astillas de madera, gotas, chispas amarillas (tortuga) o naranjas (enemigo) y confeti de plástico. Se crean la primera vez que
hace falta cada uno (14 como mucho por tortuga), nacen de golpe en el punto del choque (de 3 a 11 de nube y de 4 a 18 de
trocitos según la fuerza) y solo se mueven con partículas vivas: un golpe sin efecto activo no cuesta nada.

### Ajustes y consola

`UPROPERTY` del componente (se pueden tocar en caliente en el panel de detalles durante PIE): `MinImpactSpeed` (260 cm/s),
`FullImpactSpeed` (1800), `MinInterval` (0,14 s), `FXAmount` (1), `SoundLoudness` (1) y `MaxViewDistance` (6000 cm).

| Consola | Qué hace |
|---|---|
| `TN.Shell.Impact 0` o `1` | Apaga o enciende los golpes (suelta las cajas al apagarlos) |
| `TN.Shell.Impact.Debug 1` | Una línea por golpe: timbre, velocidad (y de dónde sale), fuerza y contra qué choca |
| `TN.Shell.Impact.Volume <x>` | Volumen de los golpes (1 por defecto, 0 mudos) |
| `TN.Shell.Impact.MinSpeed <cm/s>` | Cambia la velocidad mínima (0 = la del componente) |
| `TN.Shell.Impact.Test <arena, roca, madera, agua, tortuga, enemigo, trasto o todos> [fuerza]` | Un golpe de ese timbre delante de tu tortuga (sonido y partículas), sin física ni límites; sin argumentos, los siete uno cada 0,9 s |

Los timbres también se pueden oír sin el juego: `Tools/RaceMusic/build.bat` compila `shell.exe`, que escribe un WAV con los
siete timbres a tres fuerzas y da el pico, el nivel y la duración de cada uno.

## Acciones: derribo, muerte, recoger, lanzar, consumir, latido, reanimar y tótem

Cada acción es un `UPROPERTY(EditDefaultsOnly)` de `ATortugaCharacter` (`KnockdownSound`, `KillSound`, `PickupSound`,
`ThrowSound`, `ConsumeSound`, `DBNOHeartbeatSound`, `ReviveSuccessSound`, `TotemSelfReviveSound`) y de
`UTN_CarryComponent` (`GrabSound`, `ThrowSound`) que el Blueprint puede cambiar (`Player/TN_TurtleActionSfx.h`).

| Acción | Suena | De dónde |
|---|---|---|
| Muerte | `Audio/EffectSounds/Kill/KillSound` | De serie en el constructor de C++ |
| Recoger un objeto y coger a otra tortuga | `Audio/EffectSounds/Pickup/Pickup` | Ídem (no hay un sonido propio de coger a otra tortuga: el de recoger es el más cercano) |
| Lanzar un objeto o a otra tortuga | `Audio/EffectSounds/Throw/SC_Throw` | Ídem |
| Consumir | `Audio/EffectSounds/Consume/Consume` | Ídem |
| Derribo | «¡Clonc!» de caparazón (`UTN_ShellImpactSynthComponent`, timbre Otra tortuga, fuerza 0,85, tono ×0,85) | Sintetizado: no hay archivo |
| Levantarse o reanimar | Arpegio de campanitas del «¡plin!» de las conchas grandes (`UTN_ScoreShellSynthComponent`), 5 semitonos más grave para no confundirlo | Sintetizado |
| Tótem | El mismo arpegio, el de las conchas reina | Sintetizado |
| Latido del derribo | «Pom-pom» del contador dos octavas abajo, cada 0,9 s, en 2D y solo para el jugador derribado | Sintetizado |

- Los sintetizados los lleva `UTN_TurtleActionSynthComponent`, que la tortuga crea la primera vez que hace falta (solo en
  máquinas con audio). Si el Blueprint asigna un recurso, suena el recurso.
- Los recursos de `Content/Audio/EffectSounds` no traen atenuación: sin ella, el motor los tocaba en 2D y los oía todo el
  mapa igual de fuerte. `TNTurtleActionSfx::PlayAt` les pone la natural de los sonidos del derribo (pleno hasta 3 m,
  silencio a 25 m: `ReviveAudioInnerRadius` y `ReviveAudioOuterRadius`).
- El test `Tortunabo.Audio.TurtleActionSounds` falla si alguna acción queda sin recurso en el CDO de `BP_TortugaCharacter`
  y sin sustituto sintetizado; `Tortunabo.Audio.TurtleActionAssets`, si se mueve un recurso.
- Para oírlo: `TN.Debug.Knockdown` (derribo, latido y levantarse) y `TN.Race.Item <objeto>` (recoger).
