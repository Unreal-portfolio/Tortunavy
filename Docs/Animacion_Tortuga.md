# Animación de la tortuga

`UTN_TurtleAnimInstance` (`Player/TN_TurtleAnimInstance`) anima a la tortuga del jugador sin AnimBP, en C++, sobre el
esqueleto Mixamo de `TotugaDemo_Rig`. `ATortugaCharacter::BeginPlay` la fuerza como clase de animación. Hereda de
`UTN_ProcAnimInstance`, así que los ajustes por hueso de los sistemas viejos se siguen aplicando al final.

## Cómo se monta la pose

La evaluación (`FTNTurtleAnimProxy::Evaluate`, que puede correr fuera del hilo de juego) hace, en orden:

1. **Locomoción con los clips.** `Old_Man_Idle`, `Walking` y `Drunk_Run_Forward`, mezclados por velocidad como
   `ABS_Walk`: andar entra hasta 1,5 m/s y correr entre 4,8 y 7,3 m/s. Las fases avanzan al ritmo de los pasos (andar
   sin patinar a 3,8 m/s y correr a 7,2 m/s) y la cadera queda en su sitio (sin avance propio de los clips).
   Los clips en bucle funden sus últimos 0,3 s con el principio (`SampleClip`, `LoopFadeSeconds`) salvo `Walking`, cuyo
   ciclo ya cierra (primer y último fotograma iguales): con el fundido se mezclaban dos momentos distintos de la
   zancada y uno de los dos pasos salía más corto.
2. **Fiesta** (emote 9): el clip `Yelling` con rebote.
3. **Poses de estado** sobre la postura en T, mezcladas con la de arriba por su peso (que entra y sale suave):
   salto (brazos arriba que aletean, piernas recogidas; más arriba al caer), panzazo en el aire (brazos por delante,
   piernas estiradas; el personaje ya tumba la malla) y arrastrándose sobre la tripa (ver «Panzazo: arrastre sobre la
   tripa»), nado (brazada y patada), llevar a otra tortuga en alto, ser llevada (patalea), tumbada (floja y con la
   cabeza caída) y los emotes 0-8 (saludar, aplauso, helicóptero, palmada potente, aplaudir, baile irlandés, flotar,
   señalar y modo loco). Encima de todas, las del modo carrera: la zambullida de cabeza desde el acantilado de la meta
   y las celebraciones del podio (ver «Modo carrera: celebraciones del podio y zambullida»).
4. **Aletas** (solo los huesos de cada brazo, desde el hombro: las piernas siguen andando): los brazos que sujetan lo
   que lleva (en una aleta, abrazado o por un extremo), la aleta que va a la espalda a guardar o sacar algo del
   caparazón y el lanzamiento (saque de banda con las dos aletas o golpe de la derecha). Ver «Objetos en las aletas» y
   «Lanzar: saque de banda y panzazo».
5. **Capas encima:** inclinación hacia delante al correr y hacia dentro en las curvas, cansancio (se encorva y
   jadea) y el caparazón: cabeza, brazos y patas encogen hacia el cuerpo y el cuerpo
   baja al suelo. Con caparazón físico (`bShellBody`, ver abajo) el cuerpo **no** baja: la malla ya va tumbada sobre la
   tripa y ese eje apunta hacia delante.
6. **Levantarse del ragdoll** (`BeginGetUp`): al acabar un derribo, la pose del ragdoll se mezcla hacia la pose de pie
   en 0,75 s (`GetUpW`, curva suave), con un empujón de brazos y rodillas dobladas a mitad de camino (`GetUpFlex`). El
   mismo empujón (`BellyGetUpW`, 0,45 s) al levantarse de la tripa tras el panzazo.

Las poses se escriben como giros alrededor de los ejes de la malla (mira a +Y, arriba +Z, su izquierda +X) en la
articulación de cada hueso; los hijos le siguen. Brazo izquierdo: abajo +Y, arriba -Y, adelante +Z (el derecho, al
revés en Y y en Z). Piernas: adelante +X, rodilla -X. Espalda hacia delante -X; cabeza arriba +X. Las poses nuevas de
las aletas llevan primero el brazo al frente (Z, +90 el izquierdo y -90 el derecho) y luego lo suben o bajan sobre X
(+ sube, - baja; más de 90 lo lleva detrás de la cabeza); el antebrazo se dobla hacia arriba con +X.

## Panzazo: arrastre sobre la tripa

Antes, al caer del panzazo la tortuga se paraba en seco (el frenado de andar la dejaba quieta en una décima) y se quedaba
tiesa hasta levantarse. Ahora se arrastra un poco sobre la tripa, con sonido (`Docs/Sonido_Tortuga.md`) y polvo.

### Decisión: arrastre dentro del movimiento, no física de verdad

Se descartó activar la física del cuerpo (ragdoll o una caja como la del caparazón, `ATN_ShellBody`):

- La física de cada máquina diverge (el ragdoll del derribo desactiva la réplica del movimiento y cada una simula lo
  suyo), y una caja replicada desde el servidor llega con retraso al cliente dueño: tirones al caer y al levantarse en
  cada panzazo, que es un gesto de todo el rato (huecos de panzazo del mapa).
- Al volver de la física hay que poner la cápsula de pie donde quedó el cuerpo, y ahí es donde se atraviesan paredes.

El arrastre es una fase del propio movimiento del personaje, `UTN_TurtleMovementComponent` (`Player/`), que sustituye al
`UCharacterMovementComponent` de serie (`ATortugaCharacter` lo pide con `SetDefaultSubobjectClass`; el Blueprint conserva
sus ajustes del movimiento: conviene abrir y guardar `BP_TortugaCharacter` una vez). Lo simulan igual el cliente dueño
(predicho) y el servidor; la cápsula barre como siempre (no atraviesa nada) y el resto de máquinas solo ven el
movimiento replicado.

### Fases (`ETNBellyPhase`)

1. **En el aire**: como antes (`Server_StartDive`: 350 cm/s en el BP más la velocidad del salto, bajando, sin control).
   El servidor sube `DiveSerial` (número del panzazo, replicado) al empezar cada uno.
2. **Arrastre** (`Slide`), al caer de tripa (`ProcessLanded`, antes del aterrizaje normal: el resto de ese movimiento ya
   se arrastra):
   - Inercia: la velocidad a lo largo del suelo tocado (en una bajada, parte de la caída se convierte en arrastre; en
     una subida se pierde), un 90 % (`BellyLandingKeep`) y como mucho 850 cm/s (`BellyMaxEntrySpeed`).
   - Frenado: rozamiento seco por superficie (`TNTurtleSurface`, la misma de los pasos: arena 800, tierra 480, roca 400,
     madera 310 y agua poco profunda o fango 220 cm/s²), más un freno por velocidad (`BellyDrag` 1,5/s). Con la entrada
     típica (720 cm/s) se para en 0,57 s y 1,8 m en arena, 0,79 s en tierra, 0,87 s en roca, 1 s en madera y 1,18 s
     y 3,1 m en el agua. Desde 1,2 s el rozamiento crece (×3,5 al segundo) y a los 2,6 s se levanta igualmente.
   - Pendientes: la gravedad a lo largo del suelo (×1,15, `BellySlopeGravity`): cuesta abajo acelera (hasta 1000 cm/s),
     cuesta arriba frena antes y en las suaves el rozamiento puede más y se queda quieta.
   - Rebote: contra paredes y obstáculos (y otras tortugas), si iba contra ellos a más de 120 cm/s, devuelve un 35 % de
     esa velocidad hacia fuera y conserva un 75 % de la que llevaba a lo largo (`HandleImpact` apunta la pared y
     `OnMovementUpdated` rebota al final del movimiento).
   - El cuerpo gira despacio hacia donde se desliza (220°/s) salvo tras un rebote hacia atrás (se aleja mirando la pared).
   - Sin control: el jugador no dirige. Si cae por un borde sigue sobre la tripa y al volver al suelo continúa.
3. **Levantarse**: pasados 0,3 s y por debajo de 60 cm/s, o a propósito por debajo de 180 cm/s (`BellyExitSpeed`)
   moviéndose (el movimiento solo llega entonces, `ATortugaCharacter::Move`) o saltando (un brinco: la cápsula se pone
   de pie y salta). Quien lleva el avance pulsado todo el panzazo se levanta ahí: se pierde solo el final lento. La cápsula vuelve a su altura como `UnCrouch` con la base fija: si se metería en algo (un techo
   bajo), no se levanta.
   - **Reptar** (`Rest`): sin sitio para ponerse de pie, sigue sobre la tripa y se mueve a 150 cm/s hasta que quepa. Ni
     salta ni atraviesa nada.
   - **De pie** (`GetUp`): 0,35 s con la velocidad máxima subiendo del 35 % a la normal. En cuanto el movimiento la pone
     de pie, el servidor acaba el panzazo (`TickDive` → `EndDive`); el dueño y el servidor quitan la pose al momento,
     las demás máquinas al llegar el fin del panzazo.

### El cuerpo tumbado choca entero

**Causa de que se metiera en las paredes.** El movimiento del personaje solo sabe mover una cápsula vertical (radio 34;
tumbada, semialtura 35). La malla giraba -80° sobre sus pies, que están en el centro de la cápsula, así que el cuerpo
tumbado (~1,4 m) quedaba entero por delante: la cápsula tapaba las patas y la cabeza, las aletas y medio caparazón
entraban en las paredes, las murallas y las fortalezas antes de que la cápsula chocara. Una cápsula más gorda no sirve
(la del movimiento es siempre vertical y su altura no puede ser menor que su radio: una que cubriera 1,4 m tendría
1,4 m de alto y no pasaría por debajo de nada).

**Arreglo** (en el propio movimiento, predicho igual en el cliente dueño y el servidor):

- La malla se echa hacia atrás `DiveBodyCenterShift` (70 cm) con el panzazo completo (`TickDive`): tumbada, la tripa y
  el caparazón quedan sobre la cápsula (gira sobre la tripa, no sobre los pies) y la cápsula tapa lo más gordo. Al
  levantarse, el desplazamiento vuelve a 0 con el giro (los pies se recogen bajo el cuerpo durante el empujón).
- Tumbada (panzazo en el aire, arrastre y reptar), tras cada movimiento `UTN_TurtleMovementComponent::
  KeepBellyBodyOutOfWalls` barre una esfera (`BellyBodyRadius` 14 cm, a `BellyBodyProbeHeight` 32 cm de la base de la
  cápsula, a la altura del caparazón) desde el centro hacia la cabeza (`BellyBodyReachFront` 66 cm) y hacia las patas
  (`BellyBodyReachBack` 62 cm). Si una punta se metería en una pared, la tortuga se aparta lo justo (con barrido: la
  cápsula tampoco atraviesa lo que tenga detrás), deja de ir contra la pared y, arrastrándose, rebota como si hubiera
  chocado la cápsula (mismo «tonc» y la misma bocanada). También al girar junto a una pared o al empezar el panzazo de
  espaldas a una: las patas se echan atrás y la empujan hacia delante.
- No cuentan como pared el suelo ni las cuestas por las que se anda (normal por encima de la del suelo andable), los
  techos, lo que quede por debajo de 18 cm (bordillos y baches: los pisa la cápsula como siempre), otras tortugas ni
  cuerpos con física (se mueven distinto en cada máquina).
- Todo va dentro de `PerformMovement` (`OnMovementUpdated`): se repite igual al corregir y el servidor llega al mismo
  sitio que el cliente. `TN.Dive.Body 0` lo apaga para comparar; con `TN.Dive.Debug 1` se dibuja el cuerpo que choca
  (celeste; rojo con una flecha cuando se aparta).

Seguridad: la fase, su tiempo y el número de panzazo del que viene viajan en cada movimiento guardado del cliente
(`FTNSavedMove_Turtle`) y se repiten tras una corrección, cápsula encogida incluida; `DiveSerial` impide volver a
arrastrarse con el mismo panzazo mientras llega su fin. Con `TN.Dive.Slide 0` todo vuelve a ser como antes. Tope de
seguridad de todo el panzazo: 12 s (`DiveMaxSeconds`). Ni más rápido que correr (entra a 850 como mucho y frena en
seguida: encadenando salto, panzazo, arrastre y levantarse se va a unos 3,9 m/s andando y 5,7 m/s esprintando, por
debajo de los 4,5 y 8 m/s de ir corriendo) ni atravesar paredes (barrido de la cápsula y comprobación de sitio al
levantarse).

### Pendiente: cuesta abajo sigue cayendo (#62)

Con el rozamiento de la arena (800 cm/s²) el arrastre solo aceleraba por encima de 45°, que ya no es suelo andable: en la
playa no se deslizaba por ninguna cuesta. Ahora, cuesta abajo desde `BellySlopeMinAngle` (12°), el rozamiento se
multiplica por `BellySlopeFrictionScale` (0,3) y el freno por velocidad por `BellySlopeDragScale` (0,4): en arena a 25°
sigue a unos 250-470 cm/s tras 2 s (según entre parada o a 720 cm/s) y en llano no cambia nada (se para en 0,57 s).
Cuesta abajo el tiempo del arrastre no corre (ni la rampa de rozamiento ni `BellyMaxSeconds`); el tope es
`BellySlopeMaxSeconds` (6 s) con todo el tiempo sobre la tripa. Casi parada en una cuesta solo se levanta si la cuesta no
la va a llevar a más de `BellyStopSpeed` (en arena a 13° no: se levanta como en llano). Al caer de tripa en una bajada, la
caída a lo largo de la cuesta cuenta entera (módulo 3D), con tope `BellyMaxEntrySpeedDownhill` (1000 cm/s). Subiendo, nada
cambia. Las cuentas están en `TNDiveLogic` (`Player/TN_DiveDecisions.h`) y las prueban `Tortunabo.Dive.Slope.*`; van dentro
del movimiento (el tiempo cuesta abajo viaja en el movimiento guardado), así que el cliente lo predice igual que el
servidor. `TN.Dive.SlopeFall 0` lo apaga.

### Rebote en el vuelo del panzazo (#63)

Volando de tripa (antes de tocar el suelo) contra una pared, rebota: de la velocidad horizontal contra la pared vuelve el
45 % (`DiveWallRestitution`) y de la de a lo largo queda el 60 % (`DiveWallTangentKeep`); la vertical sigue cayendo. Cuenta
como pared una normal con Z por debajo de 0,35 (`DiveWallMaxNormalZ`): el suelo y las pendientes no rebotan. Desde
`BellyBounceMinSpeed` (120 cm/s) de velocidad contra la pared, relativa a lo que se toca (un objeto que se aleja igual de
deprisa no la hace rebotar); otras tortugas y cuerpos con física no cuentan. Lo detectan el choque de la cápsula y el del
cuerpo tumbado (la cabeza llega antes), y se aplica al final del movimiento, igual en el servidor y en el dueño. El rebote
arrastrándose en el suelo no cambia (0,35 y 75 %). Pruebas: `Tortunabo.Dive.Wall.*`. `TN.Dive.WallBounce 0` lo apaga. El
estampado contra la pared (desde 650 cm/s, E9-03) aún no está.

### Inicio del panzazo predicho (#24)

Antes el dueño mandaba `Server_StartDive` y el servidor lanzaba a la tortuga: el dueño lo recibía como corrección una ida y
vuelta después (el tirón al empezar, de 50 a 110 cm con 150 ms de latencia). Ahora el segundo salto pide el panzazo
(`UTN_TurtleMovementComponent::RequestDive`) y la petición va en el siguiente movimiento guardado: la marca
`FSavedMove_Character::FLAG_Custom_2` y el giro en 16 bits en los datos del movimiento (`FTNTurtleNetworkMoveData`). En ese
movimiento, el dueño y el servidor deciden con las mismas reglas (`TNDiveLogic::DecideDiveStart`: solo en el aire, sin otro
panzazo, libre, sin otro lanzamiento en el paso y con velocidad) y, si empieza, los dos lanzan con la misma velocidad
(`TNDiveLogic::DiveForwardSpeed`, la inercia del salto viaja en el movimiento guardado), encogen la cápsula y cuentan el
panzazo; al repetir movimientos tras una corrección, otra vez. El giro hacia la dirección del panzazo también va dentro del
movimiento. Si el servidor decide otra cosa, su corrección lleva su estado del panzazo y su cápsula
(`FTNTurtleMoveResponseDataContainer`) y el dueño repite desde ahí. Lo que no es movimiento lo hace solo el servidor: cancelar
el emote, el aviso a todos y lanzar a quien lleve en brazos (en el siguiente `TickDive`: nada se crea dentro del movimiento
del cliente). Pruebas: `Tortunabo.Dive.Start.*` (`SavedMove` falla si la petición deja de ir en el movimiento guardado).

### Pose

- `PoseBellySlide` (sobre la tripa en el suelo, `SlideW`): cabeza levantada mirando adelante y a los lados, brazos
  abiertos por delante que rozan el suelo y tiemblan con los baches, piernas algo abiertas con las rodillas dobladas y
  los pies arriba pataleando, la espalda arqueada y la cadera que rueda sobre la tripa y culea. Más deprisa
  (`SlideSpeed`, 0..1 a 7 m/s), más vibra; casi parada, rema con los brazos. Los golpes (`SlideImpact`: caer de tripa,
  chocar) sacuden brazos, pies y cabeza.
- En el aire sigue `PoseDive`; las dos se reparten el peso del panzazo por `SlideW`.
- Al levantarse del suelo, el empujón de brazos y rodillas (`PoseGetUpFlex`, `BellyGetUpW` 0,45 s) mientras
  `TickDive` endereza la malla algo más despacio (`DiveGetUpTiltSpeed` 7/s en vez de 12). La subida de la malla sobre la
  tripa se calcula con la cápsula que haya en cada momento: al ponerse de pie ya no pega un salto.
- `ATortugaCharacter::IsBellyPoseActive` (pose de panzazo hasta levantarse) e `IsBellyOnGround` (sobre la tripa en el
  suelo) es lo que leen la animación, el sonido y el polvo.

### Polvo

`UTN_TurtleDustComponent` (`Player/`, local y cosmético, nada en servidor dedicado): partículas de caras planas
(`TNAmbientFX`, las de los géiseres y el rebuscar) del color y el material de debajo de la tripa: nube clara y granos en
la arena, polvo marrón y terrones en la tierra, polvo gris y arenilla en la roca, serrín y astillas en la madera, rocío y
salpicaduras en el agua. En el terreno del mapa, arena, tierra y roca se tiñen con el camino (o la roca) del bioma.
Bocanada al caer de tripa (más grande cuanto más fuerte), rastro hacia atrás y arriba mientras se arrastra (más cuanto
más deprisa, a tope a 6,5 m/s) y bocanada pequeña al chocar. Hasta 8 emisores por tortuga que se crean al hacer falta y
solo se mueven con partículas vivas; nada a más de 50 m de la cámara. Ajustes: `DustAmount`, `MaxViewDistance` y
`FullDustSpeed`.

### Consola (afecta a la simulación: igual en el servidor y los clientes; en PIE es un solo proceso)

| Consola | Qué hace |
|---|---|
| `TN.Dive.Slide 0\|1` | 0 = se para en seco al caer, como antes |
| `TN.Dive.Friction <x>` | Multiplica el rozamiento en todas las superficies (0,5 = resbala el doble; 2 = se para antes) |
| `TN.Dive.Slope <x>` | Multiplica cuánto tiran las pendientes (0 = como en llano) |
| `TN.Dive.SlopeFall 0\|1` | 0 = cuesta abajo frena como en llano, como antes de #62 |
| `TN.Dive.WallBounce 0\|1` | 0 = en el vuelo del panzazo resbala por las paredes, como antes de #63 |
| `TN.Net.DivePredict 0\|1` | En quien la controla. 0 = el panzazo lo pide `Server_StartDive` y lo lanza el servidor (tirón al empezar), como antes de #24 |
| `TN.Dive.MaxTime <s>` | Tope de segundos arrastrándose (0 = el del componente, 2,6 s) |
| `TN.Dive.Body 0\|1` | 0 = solo choca la cápsula (la cabeza y las patas vuelven a meterse en las paredes), como antes |
| `TN.Dive.Debug 1` | Por cada tortuga simulada en esta máquina: fase, tiempo, velocidad, superficie y rozamiento; flecha verde de la velocidad y naranja de la pendiente; el cuerpo tumbado que choca (celeste, rojo al apartarse) |

Los ajustes finos son `UPROPERTY` del movimiento (`Belly Slide`: rozamientos, `BellyDrag`, entrada, topes, tiempos,
salida, rebote y giro; `Belly Slide|Body`: medidas del cuerpo tumbado) y del personaje (`DiveGetUpTiltSpeed`,
`DiveMaxSeconds`, `DiveBodyCenterShift`).

### Probar en PIE

1. Escuchando más un cliente. `TN.Dive.Debug 1` y `TN.Voice.Debug 1`.
2. Panzazo en llano de arena (playa): cae de tripa con «plaf» y bocanada, se arrastra ~1,8 m con siseo y polvo claro,
   brazos y pies moviéndose, y se levanta con el empujón de brazos. Repetir en tierra (selva), roca (acantilados),
   tablones de un puente y agua poco profunda de la orilla: más o menos arrastre, otro sonido y otro polvo.
3. Panzazo cuesta abajo de 15° o más (dunas, laderas de arena): sigue cayendo hasta el llano (con `TN.Dive.Debug 1`,
   «bajando» y los segundos cuesta abajo); cuesta arriba frena antes que en llano. En una cuesta suave se queda quieta.
4. Panzazo en el aire contra una pared (saltar a 2-3 m de ella y lanzarse): rebota hacia atrás antes de caer (con
   `TN.Dive.Debug 1`, «rebote en vuelo a … cm/s»); contra una cuesta, no. Panzazo arrastrándose contra una pared: rebota un poco hacia atrás con un «tonc» y una bocanada, **con la cabeza tocando la
   pared, sin meterse** (antes entraba medio cuerpo). Lo mismo contra una muralla de fortaleza de la playa, contra una
   roca y de lado, a lo largo de una pared (el cuerpo resbala por ella sin que la cabeza entre). Empezar el panzazo de
   espaldas a una pared: las patas no se meten, la tortuga sale un poco hacia delante. `TN.Dive.Body 0` para comparar.
5. Saltar o moverse casi parada: sale antes (el salto, con brinco). Deprisa, ni lo uno ni lo otro.
6. Panzazo que acabe bajo algo bajo (un tablón, una rampa): repta hasta salir y se levanta sin atravesar nada.
7. En el cliente: lo mismo sin tirones al caer ni al levantarse (con `TN.Dive.Debug 1`, la fase del cliente y la del
   servidor van a la par). Con latencia (`NetEmulation.PktLag 150`) tampoco al empezar el panzazo (#24): el contador de
   correcciones de `TN.Dive.Debug 1` no sube. `TN.Net.DivePredict 0` en el cliente vuelve al tirón de antes.
8. `TN.Dive.Friction 0.5` y `2`, `TN.Dive.Slide 0` para comparar con lo de antes.

## Caparazón con física propia

Metida en el caparazón y suelta, la tortuga es una caja física replicada (`ATN_ShellBody`, 55 × 46 × 42 cm, 38 kg,
fricción 0,25 y rebote 0,35) que rueda, resbala y rebota. `UTN_ShellComponent` la crea (`StartBody`) y la quita
(`StopBody`), y en cada máquina `ApplyBodyLocalState` apaga el movimiento del personaje y deja la cápsula solo con
solapamientos. En `TG_PostPhysics`, `ATortugaCharacter::PlaceOnShellBody` pone la cápsula de pie sobre la caja y la
malla tumbada sobre la tripa (rotación de la malla `FQuat(FMatrix((0,-1,0), (0,0,-1), (1,0,0)))`: la cabeza a +X de la
caja, la tripa abajo); `ResetMeshTransform` la devuelve a su sitio al salir.

- Entrar a mano: la caja nace de pie en el tronco y se vuelca hacia delante sobre la tripa.
- Lanzada o escapando del que la lleva: nace tumbada con volteretas y la tortuga sale sola cuando la caja se para.
- Caída de más de 5 m: se hace bola con física y sale al pararse. Al agua: sale y nada.
- Mientras la llevan no hay caja (va enganchada al que la lleva).
- La caja ya cubre la tortuga metida (el caparazón mide ~40 × 36 × 30 cm y lo demás encoge dentro): sus choques son
  los de la caja, sin cabeza ni patas que sobresalgan. No ha hecho falta tocarla.

## Derribo, pajaritos y ojos

- Todas las fuentes de derribo pasan por el ragdoll de `ApplyKnockdownVisual` y se quedan al menos
  `MinKnockdownSeconds` (2,2 s) en el suelo. Luego se levanta con la mezcla del paso 6.
- Choques del ragdoll: los cuerpos llevan el perfil `Ragdoll` (tipo PhysicsBody; bloquea WorldStatic y WorldDynamic e
  ignora las cápsulas), con colisión continua y la sonda anti-túnel del terreno (`TickKnockdownRagdoll`). El terreno,
  las rocas y el decorado de la playa son `BlockAll` (WorldStatic) y las murallas y fortalezas, `BlockAllDynamic`
  (WorldDynamic, cascos convexos): todo bloquea al ragdoll. Si algún cuerpo del Physics Asset tuviera la colisión
  desactivada, el primer derribo de cada tortuga lo avisa en el registro (`[Ragdoll] ... no chocan con el mundo`).
- Al levantarse, la cápsula vuelve a tener colisión **antes** de buscar sitio de pie: `FindTeleportSpot` solo aparta
  una cápsula con colisión de consulta y, con la del ragdoll apagada, no hacía nada; junto a una roca o una muralla la
  tortuga se ponía de pie con media cápsula (y la malla) dentro y luego el movimiento la sacaba a saltos. Ahora se
  pone de pie justo al lado.
- `UTN_DizzyBirdsComponent`: tres pájaros y tres estrellitas dando vueltas sobre el hueso `Head` mientras está
  noqueada, con su sonido sintetizado (`UTN_DizzySynthComponent`: trinos y cuerdas mareadas). Se encienden en todas
  las máquinas con el derribo.
- Ojos (`ATortugaCharacter::TickEyes` → `UTN_CosmeticLook::SetEyeState`): parpadea cada 2,5-5,5 s en 0,16 s (a veces
  dos veces seguidas) y pone los ojos en espiral (`EyeDizzy`) mientras está noqueada o muerta. Son dos parámetros de
  `M_TurtleBody`; los tipos de ojo están en `Docs/Tienda_Probador.md`.

## Lanzar: saque de banda y panzazo

### Ángulo de todos los lanzamientos

Antes cada lanzamiento subía a su manera: al compañero, el cabeceo del mando más 40° (entre 28° y 72°); los objetos, la
dirección del mando más 15°. Como la cámara mira 14° por debajo del mando (`CameraAimPitchOffset`), con la cámara a
nivel el compañero salía a ~54° y los objetos a ~29°, y al mirar un poco arriba, por las nubes.

Ahora todos (objetos, tinta y el compañero, con la E o con el panzazo) usan `ATortugaCharacter::GetThrowDirection`: el
rumbo de la cámara y, sobre la horizontal, `ThrowBasePitchDeg` (25°) con la cámara a nivel más solo una parte de lo
que se mire arriba o abajo (`ThrowAimPitchFactor` 0,4), entre `ThrowMinPitchDeg` (10°) y `ThrowMaxPitchDeg` (45°). Con
la cámara 20° hacia abajo sale a 17°; 30° hacia arriba, a 37°. Se calcula en el servidor con el giro del mando que ya le
llega (sin RPC nuevas). Sustituye a `ThrowUpAngleDeg` (personaje) y a `MinThrowPitch`/`MaxThrowPitch` (carga), que
ningún Blueprint tenía cambiados.

### Saque de banda con la E

Llevando a una tortuga en alto (las dos aletas ya por encima de la cabeza), la E no la suelta al momento: durante
`ThrowWindupSeconds` (0,18 s) las dos aletas se echan detrás de la cabeza con la espalda arqueada y vuelven a subir, y
al acabar (`UTN_CarryComponent::FinishThrowWindup`, en el servidor) la suelta por encima de la cabeza; luego los brazos
acompañan hacia delante y abajo con el cuerpo inclinado (0,35 s). El dueño empieza la toma de impulso al pulsar; las
demás máquinas, al recibir `ThrowWindupSerial` (replicado a todos menos al dueño). Si mientras tanto la suelta por
otra cosa (derribo, se escapa, panzazo), la toma de impulso se cancela. Con `ThrowWindupSeconds` a 0, como antes.

### Con el panzazo

Llevándola en alto, saltar y hacer el panzazo la lanza (`Server_StartDive` → `UTN_CarryComponent::ThrowWithDive`)
hacia donde se tira, con el lanzamiento de siempre (25°) más parte del impulso del panzazo en horizontal (que ya lleva
la carrera; `DiveThrowCarryFactor` 0,6) y de la velocidad hacia arriba del salto (`DiveThrowJumpFactor` 0,5), como
mucho `DiveThrowMaxSpeed` (23 m/s). Sale como caparazón con física, igual que con la E. La portadora sigue su panzazo
y sus brazos hacen el final del saque de banda mientras se tumba. Todo en el servidor; la caja de la lanzada se replica
como siempre.

### Animación (`UTN_TurtleAnimInstance`, capa de aletas)

`ThrowKeyAt(U)` interpola cinco momentos del saque de banda (U de -1 a 1): en alto como al llevarla (-1), detrás de la
cabeza con la espalda arqueada (-0,4), soltando por encima de la cabeza (0), al frente con el cuerpo hacia delante (0,5)
y acompañando hacia abajo (1). La toma de impulso recorre de -1 a 0 (`GetThrowWindupAlpha`); al soltarla, de 0 a 1.
Un objeto lanzado (bola, tinta) hace lo mismo con la aleta derecha desde detrás de la cabeza (-0,4 a 1): lo pide el
servidor con `MulticastItemThrowAnim` (no fiable, cosmético).

## Objetos en las aletas

**Causa de que salieran en los pies.** El Blueprint guarda `EquippedAttachSocket = EspaldaSocket` de la malla vieja;
`TotugaDemo_Rig` no tiene sockets, así que el objeto se enganchaba al origen de la malla (sus pies) y el giro de la
malla (yaw -90) le daba cualquier orientación.

**Ahora** (`UTN_InventoryComponent`, cosmético y local en cada máquina a partir de lo replicado; nada en el servidor
dedicado) el objeto va enganchado a los huesos de las aletas (`RightHand`; abrazado, a `Spine2`) y cada fotograma, tras
la animación de la malla (su tick depende del de la malla), se coloca según cómo se lleva (`ETNItemHold`):

| Cómo | Cuándo (tamaño en el mundo: malla × `EquippedMeshScale`, con `EquippedMeshRotation`) | Dónde |
|---|---|---|
| En una aleta | lo demás (pequeño) | Apoyado encima de la aleta derecha, de pie y de frente; brazo abajo con el codo junto a la cadera y el antebrazo al frente, como una bandeja |
| Abrazado | lado mayor ≥ `HugMinSize` (34 cm) o `ItemWeight` ≥ `HugMinWeight` (3) | Entre las dos aletas por delante de la tripa (sin meterse en ella); brazos al frente y abajo con los antebrazos que lo rodean. La apertura se ajusta sola hasta que el hueco entre las manos es su ancho (`HugOpen`), y el cuerpo se echa un poco atrás |
| Por un extremo | largo ≥ `ByEndMinLength` (30 cm) y ≥ `ByEndMinRatio` (2,2) veces su ancho | Su eje largo apunta hacia delante y arriba (40°, algo abierto hacia fuera) con la punta de atrás en la aleta derecha; antebrazo algo más alto |

- `HoldOverrides` (por `ItemId`, en el Blueprint) fuerza la forma de un objeto concreto si el tamaño no acierta.
- El centro del objeto (el de su caja, aunque la malla tenga el pivote en la base) es lo que se coloca; su tamaño en el
  mundo es el mismo que en el suelo.
- Los brazos lo sujetan andando, corriendo y saltando. Nadando, en el panzazo, con un emote o levantándose, las aletas
  hacen lo suyo y el objeto las sigue tal como estaba en la mano (lo abrazado sigue abrazado también ahí). Metida en el
  caparazón, llevando a otra tortuga o muerta, encoge y desaparece; al salir, vuelve.
- Al cogerlo del suelo aparece con un pequeño «pop» (0,2 s).
- Mallas sin esos huesos: el socket `EquippedAttachSocket` si existe o la raíz con `EquippedRelativeLocation/Rotation`,
  como antes.

### Guardar y sacar del caparazón

Al cambiar de ranura (`RotateItems`) o al sacar lo guardado porque se ha gastado lo de la mano, el servidor sube
`StashSerial` y apunta qué ha pasado en `StashKind` (bits: 1 = entra lo de la mano, 2 = sale lo guardado, 4 = tras gastar lo de la mano);
cada máquina lo anima al recibirlo (`StashSeconds`, 0,5 s): la aleta derecha va a la espalda por encima del hombro (el
pecho gira un poco y mira por encima del hombro), lo de la mano encoge y entra con un «toc» hueco (`PlayStash`, ver
`Docs/Sonido_Tortuga.md`), sale lo guardado creciendo con un «toc» más agudo y la aleta vuelve a su sitio. Si solo sale
lo guardado (tras lanzar o comerse lo de la mano), empieza 0,3 s después, cuando la aleta ha acabado. Metida en el
caparazón o llevando a alguien, cambia sin animación. Lo primero que llega (al aparecer o entrar a media partida) se
pone sin animar.

### Probar en PIE (lanzar y objetos; escuchando más un cliente, mirando desde las dos ventanas)

1. Coger a la otra tortuga metida en el caparazón y lanzarla con la E: las dos aletas van detrás de la cabeza, vuelven
   arriba, la sueltan y acompañan hacia delante; sale baja (~25° con la cámara a nivel). Mirando arriba, algo más alta.
2. Cogida, saltar y hacer el panzazo: sale lanzada hacia donde se tira, más lejos que con la E (corriendo, más aún) y
   la portadora hace el panzazo normal.
3. Lanzar una bola y la tinta: salen bajas y la aleta derecha hace el golpe de lanzar.
4. Coger cada objeto del catálogo: pequeño encima de la aleta derecha, grande abrazado con las dos (las aletas lo
   rodean) y alargado por un extremo apuntando adelante y arriba. Andar, correr, saltar, nadar, panzazo y un emote con
   él; meterse en el caparazón (desaparece y vuelve). Si alguno no se clasifica bien, `HoldOverrides` en el Blueprint.
5. Con dos objetos, cambiar de ranura (R): la aleta va a la espalda, uno encoge y entra con «toc», el otro sale con
   un «toc» más agudo. Lanzar el de la mano teniendo otro guardado: al poco, la aleta va a la espalda y lo saca.
6. Todo lo anterior se ve igual desde la otra ventana (cliente y anfitrión).

## Cara: lengua, cansancio, sudor y boca

`UTN_TurtleFaceComponent` (`Player/TN_TurtleFaceComponent`, subobjeto `TurtleFace` del personaje) anima la cara. Es
cosmético y local en cada máquina: solo lee estado que ya se replica (estamina, sprint, derribo, caparazón, emote,
chat rápido y voz), así que no manda nada por la red. En el servidor dedicado no hace nada. Solo actúa con la malla de
demo (`UTN_CosmeticLook::IsDemoTurtle`: dos ranuras); con otra malla se apaga en su primer fotograma.

### Ánimo (como las caras del HUD)

Con los mismos umbrales que `TN_RunHUDWidget` (energía = `CurrentStamina / MaxStamina`, con margen para no parpadear):

| Ánimo | Cuándo | Cara |
|---|---|---|
| Feliz | energía ≥ 0,5 | Sonrisa abierta pequeña. Quieta, de vez en cuando asoma la punta de la lengua un segundo. |
| Cansada | energía < 0,5 | Párpados a media asta y mirada baja (`EyeTired` 0,5), boca pequeña entreabierta, colorete suave y una gota de sudor. |
| Jadeando | agotada o energía < 0,22 | Párpados más caídos (`EyeTired` 1), boca muy abierta al ritmo del jadeo (2,4 por segundo), la lengua colgando por delante de la barbilla, colorete fuerte, dos gotas de sudor y, a ratos, ojos apretados «>_<». |
| Tumbada | noqueada o muerta | Ojos en espiral (`TickEyes`), boca torcida y la lengua cayendo floja por un lado. |
| Caparazón | metida dentro | Sin lengua ni sudor. |

Además: al agotarse del todo, «>_<» casi un segundo; al esprintar o en el panzazo, la lengua al viento; y emotes con
cara propia: WAZAAA (boca abierta y la lengua fuera meneándose), HAPPIE (sonrisa enorme), modo loco (lengua al aire
cambiando de lado) y fiesta (boca a gritos y ojos apretados).

### La lengua

La lengua rígida de la malla (ranura `lambert2`, sin hueso) se esconde con `HideTongue` de `M_TurtleHelmetSlot`. La
sustituye una malla procedural (`UProceduralMeshComponent`, 12 anillos de 16 vértices y la punta, rosa `#FF6F8E` con
el surco `#D94A6A`, más oscura por debajo y en la raíz) que se reconstruye cada fotograma desde una cadena de 8 puntos:

- **Ancla.** Un `USceneComponent` enganchado al hueso `Head` con la inversa de su postura de referencia: sus hijos se
  colocan en coordenadas de la malla (mira a +Y, arriba +Z, su izquierda +X) y siguen a la cabeza animada, también en
  el ragdoll. La raíz está dentro del hueco de la boca de la malla (`|x|` < 1,7; z 41,3-43,9), en (0; 12,8; 42,1), o
  0,7 hacia una comisura.
- **Simulación** (en el mundo, pasos de ~1/120 s, hasta 4 por fotograma): gravedad, rozamiento con el aire (a la
  carrera empuja la lengua hacia atrás), aleteo (una onda de la raíz a la punta, más rápida y fuerte con el viento) y
  el bombeo del jadeo; muelles de forma que llevan cada tramo hacia su dirección de reposo (del primer tramo, la
  salida de la boca, al último; más blandos hacia la punta) y largo fijo (cada punto sigue al de delante).
- **Choque con la cara.** Una tabla de alturas de la cara vista de frente (medida sobre la malla: z 32-47, `|x|` 0-10,
  el hueco de la boca tapado) saca los puntos que se meten por la normal de la superficie: en las mejillas, hacia el
  lado. Así, al viento, la lengua resbala por la mejilla. La raíz y el primer punto no chocan.
- **Formas de reposo.** Al viento: sale por la comisura, se abre hacia el lado y se dobla hacia atrás. Colgando: por
  delante de la barbilla. Tumbada: floja hacia un lado. Punta: corta y firme, meneándose.
- **Lado al viento.** Al empezar a esprintar, uno al azar; en una curva cerrada (más de 80°/s), el de fuera (girar a
  la derecha la lanza a su izquierda). Al cambiar de lado cruza por delante de la boca.
- Fuera de cámara (`WasRecentlyRendered`) no se simula ni se reconstruye; al volver a verse, o tras un teletransporte,
  la cadena se recoloca en reposo.

Ajustes (propiedades del componente): `SprintTongueLength` (8 unidades de la malla, 20 cm), `PantTongueLength` (6,5),
`TongueHalfWidth`, `TongueShapeStiffness`, `TongueGravity`, `TongueAirDrag`, `TongueFlap` y `bIdleBlep`.

### Boca, ojos y colorete

Son parámetros de `M_TurtleBody` que el componente escribe en la instancia del cuerpo (`UTN_CosmeticLook::GetBodyMaterial`;
si `ApplyLook` crea otra, los vuelve a escribir): `EyeTired`, `EyeSqueeze`, `MouthOpen`, `MouthSmile` y `FaceBlush`
(ver `Docs/Tienda_Probador.md`). La boca se pinta alrededor del hueco de la malla; se abre y se cierra con suavidad.

### Hablar

- **Chat rápido.** El componente escucha `ATN_CoopGameState::OnQuickChatReceived` (el multicast que también saca el
  bocadillo del HUD, en todas las máquinas). Si el mensaje es de su jugador, mira lo largo que es la frase en el
  catálogo del mando local (`ResolveQuickChatDisplayData`) y habla 0,4 s + 0,07 s por letra (entre 1 y 4,2 s).
- **Voz de proximidad.** Mientras `UProximityVoiceComponent::IsHeardSpeaking()` (lo mismo que enseña el bocadillo con
  barras del HUD).
- La boca va por sílabas: de 0,09 a 0,17 s cada una, abriendo de un tercio a del todo, con alguna pausa entre palabras;
  encima de la cara que tenga (cansada o jadeando también habla).

### Sudor

Dos gotas procedurales (media esfera con un cono hasta la punta, celestes con brillo) junto al casco, a cada lado de
la cabeza: aparecen con un saltito, resbalan y se encogen. Una cansada (cada 1,7 s); dos jadeando, a contratiempo
(cada 1,15 s).

### Pruebas por consola (solo en la máquina que las escribe)

- `tn.Face.Mood 0|1|2|3`: fuerza feliz, cansada, jadeando o tumbada (-1 = la real).
- `tn.Face.Tongue 0|1|2|3`: lengua dentro, al viento (como al esprintar), colgando o asomando la punta (-1 = la real).
- `tn.Face.Talk 1`: todas las tortugas mueven la boca como si hablaran.

## Modo carrera: celebraciones del podio y zambullida

### Celebraciones (`SetCelebration`)

`UTN_TurtleAnimInstance::SetCelebration(ETNTurtleCelebration)` (también desde Blueprint; `GetCelebration`,
`GetCelebrationTime`) pone una pose encima de todo lo demás, con peso que entra y sale suave (6/s). Si se pide otra,
la que había sale del todo antes de que entre la nueva. Funciona con personaje o sin él: el podio de la pantalla del
campeón (`ATN_RacePodiumStage`, `Docs/Modo_Carrera.md`) usa esta animación en mallas sueltas de `TotugaDemo_Rig`, sin
peón (todo lo demás se queda en reposo). Cada una es un bucle exacto, como un GIF:

| Pose | Bucle | Qué hace |
|---|---|---|
| `Trophy` (la primera) | 1,6 s | Los dos brazos arriba con los codos hacia dentro y las manos juntas sobre la cabeza (donde el podio pone la concha, entre `LeftHand` y `RightHand`); dos saltitos por vuelta en los que estira los brazos para subirla y dobla las rodillas al caer; el pecho fuera, la cabeza mirando la concha y un meneo de lado a lado. |
| `Disappointed` (la segunda) | 3,2 s | Hombros caídos (clavículas `LeftShoulder`/`RightShoulder` abajo), brazos colgando flojos algo por delante, espalda encorvada y cabeza gacha. Coge aire (0-0,9 s: el pecho y la cabeza suben), lo suelta de golpe y se hunde más; luego niega despacio con la cabeza y arrastra un pie por la arena. |
| `Tantrum` (la tercera) | 1,2 s | Sentada: la cadera baja hasta apoyar el culete (la altura de la cadera en la postura de referencia menos 5 unidades; 18 si no es razonable), echada un poco atrás y meciéndose; piernas estiradas al frente, algo abiertas, pataleando alternas (los talones golpean el suelo); puños que aporrean el suelo a los lados, uno y otro; la barbilla arriba gritando y la cabeza sacudiéndose. |

La cara (boca, ojos, colorete) no es de la animación: en el podio la pone `ATN_RacePodiumStage` en `M_TurtleBody`,
acompasada con `GetCelebrationTime`.

### Zambullida del acantilado de la meta

En la playa del modo carrera la meta es un acantilado (`TNBeach::CliffHeight` = 15,5 m): se salta de cabeza al agua.

- **Cuándo**: si la tortuga despega o empieza a caer y, en los primeros 0,35 s de la caída (`CliffDiveStartWindow`),
  está en `ATN_BeachRaceGenerator::IsCliffJumpZone` (los últimos 7,5 m de la repisa y el vacío sobre el agua). El
  generador se busca una vez por animación y se guarda con un puntero débil; si no hay (cualquier otro mapa), se vuelve a
  buscar cada 5 s y nunca hay zambullida. Se acaba al aterrizar o al nadar, y también si empieza el panzazo, se mete en
  el caparazón, la derriban o la llevan. Cosmética y local en cada máquina, con el movimiento replicado (sin RPC).
- **Pose** (`PoseCliffDive`, peso que entra a 9/s y sale a 14/s): cuerpo estirado, brazos por encima de la cabeza
  cruzando un poco para juntar las manos (tiemblan con el aire), la cabeza entre los brazos con la barbilla algo
  metida, piernas juntas y estiradas hacia atrás con las puntas de los pies (un leve aleteo). Al final, todo el cuerpo
  gira hacia delante sobre la cadera (`CliffDivePitch`): sigue la trayectoria, 90° + el ángulo de caída (tumbada en lo
  alto del salto, cabeza hacia el agua al caer deprisa), entre 40° y 165°, suavizado (4/s). Entra así en el agua.
- `IsCliffDiving()` dice si está en ella (para el sonido o las salpicaduras, si alguien los quiere).

## Estado que lee

Del personaje: velocidad, `IsFalling`/`IsSwimming` del movimiento, `IsBellyPoseActive` e `IsBellyOnGround` (el
panzazo en el aire y sobre la tripa), `IsInShell` (y si el caparazón tiene caja física), `IsKnockedDown`, la pose guardada al levantarse, el emote activo y su tiempo (`GetActiveEmoteIndex`,
`GetEmoteTime`); del `UTN_CarryComponent`, si lleva o la llevan (al soltar se hace el lanzamiento) y la toma de
impulso del saque de banda (`GetThrowWindupAlpha`); del `UTN_InventoryComponent`, cómo lleva lo de las aletas
(`GetShownHold`), cuánto abre abrazando (`GetHugOpen`) y la aleta a la espalda (`GetStashReach`); el lanzamiento de un
objeto llega con `PlayThrow`; del `UTN_StaminaComponent`, si está agotada; en la playa del modo carrera, la zona de la zambullida
(`ATN_BeachRaceGenerator::IsCliffJumpZone`). La celebración del podio la pide quien la use (`SetCelebration`). La cara (`UTN_TurtleFaceComponent`) lee además la estamina, el sprint, la
velocidad, el giro, el chat rápido y la voz.

## Para añadir un clip

Cargarlo en `NativeInitializeAnimation`, pasarlo al proxy en `NativeUpdateAnimation` y mezclarlo en `Evaluate` con
`SampleClip` y `BlendInto` (como `Yelling` en la fiesta).
