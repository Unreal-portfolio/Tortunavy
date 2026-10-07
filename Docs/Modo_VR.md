# Modo VR (gafas de realidad virtual)

Tortunavy se puede jugar entero con unas gafas de realidad virtual (probado para Meta Quest con OpenXR): en primera
persona, con la cabeza y las dos manos con seguimiento, el HUD curvo anclado a la vista y todos los menús curvos en el
mundo, que se apuntan con la aleta derecha. Se coge con las manos (agarre, muy cerca del objeto) y se lanza con el gesto
(soltar el agarre con impulso). El mismo modo se puede probar **sin gafas** (modo simulado), en la ventana del PC con el
ratón. Sin gafas también hay **primera persona** (tecla T o clic del stick derecho, fila «Cambiar de cámara» de los
controles), con la misma cámara en la cabeza.

Estado: rama `new-mota-vr` (29-09-2026) y `new-mota-vr-inmersion` (30-09-2026: primera persona con cuerpo, brazos que
siguen a los mandos, coger con física, gestos, interfaz curva y carga en 360). Escrito sin poder compilar (sesión en la
nube): la primera compilación puede pedir algún retoque.

## Resumen rápido

| Quiero... | Cómo |
|---|---|
| Probarlo sin gafas | En PIE, consola: `TN.VR 2`. O Ajustes > Juego > «Modo VR: Simulado sin gafas», o arrancar con `-vrsim`. `TN.VR -1` vuelve a lo normal. |
| Pruebas automáticas | `Automation RunTests Tortunabo.VR` (consola del editor o Session Frontend). |
| Jugar con las Quest conectadas al PC (recomendado) | Meta Quest Link (cable o Air Link) como runtime OpenXR; en el editor, Play > «VR Preview». En la build de Windows, `Tortunabo.exe -vr`. |
| Jugar en las Quest sin PC (experimental) | Empaquetar para Android (ya configurado para Meta Quest) e instalar el APK. Ver «Build para las Quest». |
| Ver qué está pasando | `TN.VR.Status` escribe en el registro el modo, si hay gafas y estéreo, el dispositivo y el estado del panel. |

## Modos

| Modo | Qué es |
|---|---|
| Apagado | El juego de siempre: tercera persona, HUD y menús en la pantalla. |
| Gafas | OpenXR con estéreo: primera persona con la cabeza y los mandos con seguimiento. |
| Simulado | Sin gafas, en la ventana: la misma primera persona, las mismas aletas (quietas delante) y la misma interfaz en el mundo; el ratón mueve la vista y hace de puntero en los menús. Sirve para probar todo el modo VR en el PC. |

Cómo se elige (lo de arriba manda sobre lo de abajo). `UTN_VRSubsystem` lo mira cada fotograma, así que cambia en caliente:

1. Consola `TN.VR`: `-1` (de serie: lo de abajo), `0` apagado, `1` gafas (enciende el HMD si hay gafas OpenXR), `2` simulado.
2. Línea de comandos: `-novr` (apagado) o `-vrsim` (simulado). Con `-vr` el motor arranca en las gafas y entra el modo gafas.
3. Ajuste **Ajustes > Juego > Realidad virtual > Modo VR**: Automático (de serie), Desactivado o Simulado sin gafas.
4. Automático: gafas si el motor está pintando en estéreo (VR Preview del editor, `-vr`, la build de Quest); si no, apagado.
   Sin gafas y sin tocar nada, el juego es exactamente el de siempre.

## Qué cambia en VR

- **Calibración de la cabeza** (#916): la cámara es origen del seguimiento + pose de las gafas, y ese origen no tiene por qué
  estar en la cabeza (el recentrado del arranque puede no hacerse; el jugador se mueve o se quita las gafas). Pasados unos
  fotogramas con las gafas puestas, y de nuevo al recentrar, al ponérselas y al reaparecer, se guarda dónde están las gafas
  (`FTNVRHeadCalibration`) y el origen se corre para que esa posición caiga en los ojos de la tortuga: lo que la cabeza se
  mueva después se ve. Girar con el stick no la saca de los ojos. El log dice `cabeza calibrada` con la posición medida.
- **Primera persona.** Cámara VR en la cabeza de la tortuga, con el seguimiento de la cabeza: de pie, a una altura fija
  (45 cm por encima del centro de la cápsula, `VREyeOffset`, la de los ojos de la malla: que el paso no menee la vista);
  **tumbada, en el ragdoll y derribada, en los ojos de la cabeza**, esté donde esté el cuerpo. En el modo simulado
  (`TN.VR 2`), de pie también en los ojos, como la primera persona sin gafas. Del cuerpo propio se ve todo **menos la cabeza**:
  al mirar abajo se ven el cuerpo, los brazos, la lengua y las gotas de sudor. Los demás te ven normal. La tortuga mira
  hacia donde mira tu cabeza, también en las máquinas de los demás (`bVRPlayer` replicado). Andar va hacia donde miras.
- **Brazos que siguen a los mandos.** Las manos del cuerpo de la tortuga van a donde están los mandos (IK de brazo y
  antebrazo en `UTN_TurtleAnimInstance`) y la aleta apunta hacia donde apunta el mando (la muñeca se dobla hacia él, 75° como
  mucho, #916), salvo bailando, en el caparazón, tumbada o llevando a otra tortuga. Los mandos solo tienen seguimiento si el
  rig tiene un dueño local (`AActor::HasLocalNetOwner`, que mira el controlador o el peón del dueño): `ATN_VRRig` lo tiene
  desde #916 y `TN.VR.Status` dice si cada mando tiene seguimiento. Los demás
  también lo ven (las manos se mandan al servidor unas 15 veces por segundo). Las aletas sueltas de los mandos solo salen
  sin tortuga (menú principal, espectador) o dentro del caparazón.
- **Caparazón.** Se ve desde dentro, en su centro y sin girar con la bola, **mucho más oscuro** (tono de concha y viñeta;
  `TN.FirstPerson.ShellLight`, 0,2 de serie).
- **Giro** con el stick derecho: a pasos de 30° (de serie), de 45° o suave (Ajustes > Juego > «Giro en VR»). El clic del
  stick derecho recentra la vista y vuelve a poner delante el HUD.
- **Aletas.** Una aleta de tortuga en cada mando (malla procedural, verde con la manga de caparazón). El objeto que
  llevas en la mano va en la aleta derecha, delante de tus ojos (los demás lo ven en la aleta de tu tortuga).
- **Coger y lanzar con las manos** (ver «Coger y lanzar»): el agarre coge lo que está **muy cerca de esa mano** (40 cm,
  `VRHandReach`) y, con al menos un mando con seguimiento, el gatillo ya no coge lo del suelo ni a los compañeros como en
  tercera persona (`TNVRHands::GripOnlyTakes`, #916); soltar el agarre con impulso lo lanza hacia donde va la mano. Los objetos del suelo solo se cogen con
  la mano (sin VR, por cercanía del cuerpo, como siempre).
- **Usar y lanzar** con el gatillo derecho, **apuntando con la aleta derecha**: lanzar al compañero que llevas, los
  objetos arrojadizos, la tinta y los objetos de la carrera salen hacia donde apunta el mando. El cliente manda esa
  dirección al servidor (fiable) justo antes de la acción; sin gafas se apunta con la cámara, como siempre.
- **Gatillos y agarres por su valor.** Con OpenXR los Touch solo dan el valor del gatillo y del agarre (no un «clic»):
  cuentan como pulsados a partir del 55 % y sueltos por debajo del 35 % (en los menús y en los agarres, con histéresis;
  en las acciones del juego, un disparador «Down» con la misma histéresis en la asignación, `UTN_InputTriggerAnalogDown`:
  un gatillo que ronda el 55 % no corta lo que se mantiene, como rebuscar). Si un menú se abre con el gatillo o el
  agarre ya apretados, no hacen clic ni cambian de pestaña hasta soltarlos y volver a apretar.
- **HUD** curvo y **anclado a la cámara**: siempre fijo en la vista, como en la pantalla (1,5 m, 80° de arco, el eje del
  cilindro en los ojos). Si hay una pared delante, o el suelo bajo su borde de abajo o en sus lados, se acerca lo justo para
  que no quede tapado (se mira el centro, los lados y el borde de abajo); lo que se lleva en las manos no lo acerca.
  `TN.VR.HudFollow 1` lo deja suelto delante siguiendo a la cabeza con retraso (a quien le maree el anclado).

- **Menús** (pausa, tienda, probador, general, salas, menú principal, campeón...): un panel curvo que te rodea, quieto en
  el mundo (1,6 m, 100° de arco), **repetido alrededor de la cabeza** (#916): con gafas, `TN.VR.MenuPanels` paneles (3 de
  serie, a 120°; hasta 6, 1 deja solo el de delante) con el mismo material, así que el menú está delante mires a donde
  mires; el láser apunta al que toque antes (`ATN_VRRig::MenuCopies`, `TNVRMath::MenuPanelTransform`). Sin gafas (simulado), centrado en la vista y con el arco que cabe en la ventana con el
  campo de visión de la cámara (unos 76° con 90° y 16:9; `TNVRMath::SimulatedMenuArc`). La aleta derecha apunta con un láser y el gatillo es el clic. También con botones: A/X
  aceptar, B/Y atrás, agarres = pestaña anterior/siguiente, sticks = moverse por el menú, botón de menú = cerrar.
- **Interfaz integrada.** El panel del motor (`UWidgetComponent`) queda plano e invisible (dibuja la interfaz en su
  textura y sirve al puntero); lo que se ve es una malla curva con su mismo material (`CurvedPanel`).
- **Ruedas** de emotes (Y) y de frases (gatillo izquierdo): mantener y elegir con el stick derecho, como con el mando.
- **Pantalla de carga en 360**: mientras sale el huevo, una playa en 360 rodea la cabeza (cielo, horizonte, mar y arena;
  `TN.VR.LoadingDomeRadius`) con el huevo en el panel curvo. Con gafas, mientras está cerrado, además capas de carga de las
  gafas: un cubo con la misma playa y el huevo delante. Las pinta el compositor de las gafas, así que no se congela la
  imagen mientras se carga un mapa.
- **Confort** (marea menos):
  - sin temblores de cámara, sin ojo de pez, sin desenfoque de movimiento, sin aberración cromática ni profundidad de campo;
  - cortes secos en vez de fundidos de cámara (cambiar de tortuga como espectador, entrar y salir del probador);
  - la almeja y el gusano de la playa no se llevan la cámara (se sigue en primera persona);
  - de fantasma espectador, con gafas (#646): ni cámara fija ni libre. La cabeza sigue la posición de la tortuga seguida
    (detrás y arriba, sin retardo) y la base de la vista mira a un rumbo fijo (el de su cámara al empezar a mirarla), así que
    solo gira con tu cabeza: el brazo de cámara de la otra tortuga llegaba tarde y giraba cuando giraba ese jugador
    (`UTN_GhostCameraModifier`, `TNVRMath::GhostViewLocation`). El rig se queda ahí (no colgado del brazo de la otra tortuga) y
    el HUD va suelto delante, siguiendo a la cabeza con retraso. Simulado (`TN.VR 2`): la cámara de la tortuga seguida, como antes;
  - la cáscara de revivir tapa toda la vista (#646): el panel de la interfaz abarca unos 80°, así que, además de pintarse en él,
    pide una esfera oscura alrededor de la cabeza (`TNVR::SetViewCover`, `ATN_VRRig::CoverDome`) que se aclara al abrirse.
- **Voz**: con «Pulsar para hablar», el clic del stick izquierdo.
- **Manos que no atraviesan el escenario**: la punta de cada aleta se queda en la superficie de paredes, suelo y rocas
  (lo que para la cámara, `ECC_Camera`), con los brazos del cuerpo y lo que se coge; no se coge ni se pulsa nada a través
  de una pared. Lo que se coge, las tortugas y los interactuables no paran la mano.
- **Solo se coge o se toca lo que se ve**: la mano coge a 22 cm de la punta y toca a 40, así que con la punta parada en una
  pared fina llegaba a lo de detrás. Cada candidato (objeto con física, interactuable o compañero) tiene que verse desde los
  ojos: un trazo `ECC_Camera` hasta su punto más cercano a la mano, sin contar el propio candidato, la tortuga, lo que lleva
  encima ni lo que tiene en las manos (`UTN_VRGrabComponent::CanReach`). Con la mano dentro de la esfera de escaneo de un
  decorado que se rebusca, el punto es el del decorado. El servidor lo vuelve a mirar al aceptar un agarre: desde los ojos
  del peón o pasando por la mano que le manda el cliente (los ojos de verdad pueden estar algo apartados de la cápsula).
- **Pulsar botones con la punta de la aleta**: los botones (`ATN_ButtonInteractable`) y los interruptores del mapa
  procedural (`ATN_ProcSwitch`) se pulsan tocándolos con la punta yendo hacia ellos (a más de 40 cm/s), sin apretar nada;
  dejar la mano apoyada no los repite (hay que alejarla 10 cm). Lo demás (cofres, puestos, objetos) sigue con el agarre o
  el gatillo (`TNVRHands::UpdatePoke`).
- **Vibración de los mandos** (si los mandos vibran): al coger o interactuar con la mano, al soltar y al lanzar, al tocar
  una pared o el suelo, cuando lo cogido se engancha y tira de la mano (más fuerte cuanto más se separa) o se escapa, al ser
  derribada (las dos manos) y con el láser al pasar por un botón y al pulsar. Ajustes > Juego > Realidad virtual >
  «Vibración de los mandos VR» (encendida o apagada) y `TN.VR.Haptics` (0-1, 1 de serie); la consola, si se toca, manda
  sobre el ajuste (#647).
- **Viñeta de confort**: los bordes de la vista se oscurecen al andar deprisa, caer, salir lanzado (catapulta) o con el
  giro suave; parada, girando a pasos o con una rueda abierta, nada. Nunca baja de la que ya hay (la de la escena, 0,4 del
  motor y los volúmenes como el de las tormentas, o la del caparazón): se nota cuando la supera, a partir de unos 4 m/s o
  45°/s de giro suave. Ajustes > Juego > Realidad virtual > «Viñeta de confort» (Apagada, Normal o Fuerte: 0, 1 o 2) y
  `TN.VR.ComfortVignette` (0 la quita, 1 de serie, 2 el doble); si se toca la variable de consola, manda sobre el ajuste
  (#647).

## Controles (Meta Quest Touch)

| Botón | Jugando | En un menú |
|---|---|---|
| Stick izquierdo | Andar (hacia donde miras) | Moverse por el menú |
| Stick derecho | Girar (pasos o suave); en las ruedas, elegir; de espectador, cambiar de tortuga | Moverse por el menú |
| Clic stick derecho | Recentrar la vista | — |
| Clic stick izquierdo | Pulsar para hablar | — |
| Gatillo derecho | Interactuar / usar / lanzar (apunta la aleta derecha) | Clic del láser |
| Gatillo izquierdo | Rueda de frases (mantener) | Clic del láser |
| Agarre derecho | Coger con esa mano; soltar con impulso = lanzar; sin nada cerca y con un objeto en la aleta, soltar despacio = dejarlo caer | Pestaña siguiente |
| Agarre izquierdo | Coger con esa mano; sin nada cerca, correr (mantener) | Pestaña anterior |
| A | Saltar | Aceptar |
| B | Caparazón | Atrás |
| X | Cambiar de objeto | Aceptar |
| Y | Rueda de emotes (mantener) | Atrás |
| Menú (mando izquierdo) | Menú de pausa | Cerrar |

Los avisos del juego nombran estos botones (#644): con el modo VR puesto, el aparato de los avisos
(`UTN_InputDeviceSubsystem`, `ETNInputDevice::VR`) es siempre el de las gafas, y el aviso de interactuar dice «Gatillo
derecho»; igual el tutorial, el HUD del fantasma («Stick derecho  cambiar de tortuga»), los karts («Usar: B», «Usar: Gatillo
izquierdo», «Peso (cabeza · Stick izquierdo)»), el Rally («Mantén Y para volver a la pista», «Stick derecho: cambiar de vista»)
y la lista de controles del briefing. Los nombres salen de `TNVRControls::KeyName` (también lo usa `KeyDisplayName`, así que
Ajustes los dice igual) y el botón de cada acción, de `TNVRControls::KeyForAction`. Ajustes > Controles los enseña en solo
lectura al final («Realidad virtual (mandos Touch)», `TNVRControls::GetGuide`). Sin VR no cambia nada.

## Vehículos: buggy del Rally y karts (con gafas)

Decisión de Mokius (04-10-2026): los vehículos se juegan con las manos, programado en `ATN_Buggy` y `ATN_BuggyGunnerPawn`
para que el Rally y los karts lo hereden (#529).

- **Asiento.** La conductora y la artillera llevan un `UTN_VRSeatComponent` en los ojos de su tortuga sentada (55 cm sobre
  la cadera y 14 por delante). Con gafas es el origen del seguimiento: el rig se engancha ahí y la cabeza mueve la cámara
  dentro de él. Al sentarse con gafas se **recentra solo** (la cabeza queda en los ojos de la tortuga, mirando al morro);
  el clic del stick derecho vuelve a recentrar.
- **Cámara.** La del asiento sustituye a la de siempre: sin brazo, sin retardo, sin FOV dinámico, sin balanceo ni temblor
  (`ATN_Buggy::UpdateCamera` no corre) y, en la artillera, sin girar con el apuntado ni empujones. La cabeza propia y el
  casco no se ven (solo en tu máquina). Sin gafas (`TN.VR 2`), la cámara del asiento mira hacia el ratón (karts) o el
  apuntado (artillera).
- **Volante** (conductora): cerrar el agarre a menos de 22 cm del aro lo coge, con una mano o con las dos. Con una mano
  manda su ángulo alrededor del centro; con dos, el de la recta entre ellas. Coger o soltar una mano no hace saltar el
  volante; llega a ±90° (dirección a tope) y, sin manos, vuelve solo al centro. La dirección sale suavizada
  (`TNVRVehicle::StepWheel`). Sin cogerlo, el stick izquierdo.
- **Asas** (artillera): un manillar en el carro de la torreta, delante de su pecho, que gira con ella (solo en guiñada) y
  solo se ve con una artillera con gafas. Cerrar el agarre a menos de 30 cm de un puño lo coge y, mientras se tenga, el
  apuntado sale de hacia donde apuntan las manos que agarran (en los ejes del buggy y limitado como siempre,
  `TNRallyTurret::ClampAim`); se manda al servidor con `ServerSetAim`, como el del ratón. Sin asas, el stick derecho.
- **Peso** (artillera de los karts): la cabeza apartada a un lado del asiento inclina hacia ese lado (nada hasta 4 cm, a
  tope a 20 cm; se suma al stick) y va al servidor con `ServerSetLean`.
- **Kart con una sola tortuga**: mira con la cabeza; la torreta sigue a la mirada y dispara hacia allí.
- **Brazos para los demás**: las manos que se ven (en el aro, en los puños o en los mandos) van al servidor sin fiabilidad
  unas 15 veces por segundo y los brazos de la tortuga sentada las siguen (IK de brazo y antebrazo en
  `UTN_BuggyRiderAnimComponent`).
- **Red**: el servidor decide como siempre. La dirección va con la entrada del vehículo (Chaos), el apuntado y el peso con
  sus RPC de siempre (limitados en frecuencia y validados), y el asiento solo manda si va con gafas y dónde están sus manos
  (recortadas a 1,5 m de los ojos).

| Botón | Conductora | Artillera |
|---|---|---|
| Agarres | Coger el volante (una o dos manos) | Coger las asas (apuntar con la mano) |
| Gatillo derecho | Acelerar | Disparar |
| Gatillo izquierdo | Frenar y marcha atrás | Especial del Rally · usar el objeto (karts) |
| Stick izquierdo | Girar sin el volante · hacia atrás: «hacia atrás» | Peso (karts) · hacia atrás: «hacia atrás» |
| Stick derecho | Hacia delante: disparar sola · hacia atrás: disparar atrás / «hacia atrás» | Apuntar sin las asas |
| A | Turbo | — |
| B | Usar el objeto (karts) | «Hacia atrás» (karts, mantener) |
| X | Freno de mano | — |
| Y | Enderezar (mantener: reaparecer) | Enderezar (mantener: reaparecer) |
| Clic del stick derecho / izquierdo | Recentrar / hablar | Recentrar / hablar |

Fuera de esto (otras tarjetas): la munición, las notas y la tableta del Rally, la pausa y la salida en el Rally y los karts,
y el rendimiento en estéreo. Los nombres de los botones Touch en los HUD ya están (#644, ver «Controles»).

Piezas: `UTN_VRSeatComponent` (`VR/TN_VRSeatComponent.*`), `TNVRVehicle` (`VR/TN_VRVehicleMath.h`), `ATN_Buggy`
(`Vehicles/TN_Buggy_VR.cpp`), `ATN_BuggyGunnerPawn` (`Vehicles/TN_BuggyGunnerPawn_VR.cpp`), las asas en
`TNBuggyTurretMesh::BuildHandles` y los botones Touch en `UTN_BuggyInputSet` y `UTN_KartInputSet`. El rig quita sus mandos
de la tortuga (`IMC_VR`) mientras se va sentada.

## Gestos físicos (con gafas, #918)

Los botones siguen funcionando; estos gestos son un extra. Las cuentas están en `VR/TN_VRGestures.h` (`Tortunabo.VR.Gestures`).

- **Guantazo**: mover la **mano derecha de lado a lado** muy deprisa (más de 420 cm/s respecto del cuerpo, al menos el 70 % de lado) da el guantazo de la aleta (#832) con la mano abierta y sin objeto. Sale hacia donde va la mano y lo decide el servidor como con el botón. Enfriamiento de 0,8 s; justo después de soltar un agarre no cuenta.
- **Lanzar**: ya era con el movimiento del brazo (ver «Coger y lanzar»).
- **Volante** del Rally y de los karts: ya se agarra con el agarre (una o dos manos) y se gira (ver «Vehículos»).
- **Caparazón**: **agachar la cabeza** 28 cm (rápido, desde la altura de siempre) mete o saca del caparazón, como B. Sentarse o encogerse poco a poco no cuenta; el menú abierto, tampoco. Log: `agacha la cabeza`.
- **Cabeza adelantada**: con gafas, el cuello va 14° hacia delante y la cabeza 10° enderezada (todas las máquinas), y `VREyeOffset` es (24, 0, 43), girado con la tortuga: los ojos de la malla, con la lengua unos 10 cm delante y 10 debajo, como la propia. Si marea al girar la cabeza (la tortuga gira alrededor de su cápsula), bajar la X de `VREyeOffset`.

## Coger y lanzar (con gafas)

Todo se decide por la mano que aprieta el agarre, no por el cuerpo (`ATortugaCharacter::VRGripPressed` y
`VRGripReleased`, `ATN_VRRig::UpdateGrips`). La velocidad de la mano se mide respecto del origen de la vista (lo que va
con el cuerpo): andar, saltar o girar con el stick no la cambian, así que soltar andando con la mano quieta no lanza
(`TNVRMath::RelativeHandVelocity`; tras cada giro de golpe o al recentrar se empieza a medir de cero). Al apretar, en este
orden:

1. **Algo al alcance de esa mano** (40 cm): un objeto del suelo o cualquier cosa con la que se interactúa (botones,
   cofres, puestos). Se coge o se usa como con la E.
2. **Un compañero** en el caparazón o aturdido al alcance de la mano: se coge (como con la E).
3. **Un objeto con física** (pelotas, cajas, decorado suelto; hasta 250 kg, nunca tortugas, enemigos ni caparazones):
   va pegado a la mano (`UTN_VRGrabComponent`, un `UPhysicsHandleComponent`) y sigue chocando con lo demás. Si el actor
   se replica, lo mueve el servidor (la mano le llega unas 30 veces por segundo) y todos lo ven; si no, solo en tu máquina.
   En un cliente lo replicado no simula física (`ATN_PhysicsObjectActor` solo simula en el servidor): se elige por clase
   (`ATN_PhysicsObjectActor` sin `bUseKinematicPush`, su malla) o con la etiqueta de actor `VRGrab`, y el servidor lo
   acepta (con física, móvil, al alcance) o lo rechaza (`ClientGrabRejected`). Mientras se lleva, el servidor lo tiene
   despierto en red (`SetNetDormancy(DORM_Awake)`); al soltarlo vuelve a dormirse cuando se para, con su temporizador de
   siempre (`ATN_PhysicsObjectActor::SetExternallyHeld`). Un actor replicado que no replica su movimiento no se coge; la
   etiqueta `NoVRGrab` lo impide siempre.
4. Con el agarre derecho y un objeto ya en la aleta: se «agarra» ese objeto.
5. Nada: con el izquierdo, correr mientras se mantiene.

Robustez de los agarres (revisión del 03-10-2026, `UTN_VRGrabComponent`):

- **Lo que lleva otra tortuga no se le quita**: el servidor rechaza el agarre (y la máquina que lo coge en local no lo
  busca). La misma tortuga sí puede cogerlo con las dos manos.
- **Sin poder usar las manos** (derribada, muerta, en el caparazón o llevada por otra): no se coge nada; lo que llevaba con
  física se suelta (también en el servidor si el cliente no lo ha soltado aún).
- **Enganchado**: si lo cogido se queda a más de 45 cm de donde debería estar en la mano durante 0,35 s (detrás de una
  pared, sujeto por algo) o a más de 1,5 m de golpe, se suelta solo y vibra (`TNVRHands::ShouldBreakGrab`). No se queda
  tirando de la mano ni atraviesa la pared.
- **La cápsula propia no choca con lo que lleva**: no se puede subir encima de la caja que se tiene en la mano ni empujarla
  andando (lo hacen el dueño y el servidor, para que no haya correcciones). Lo cogido pasa a usar CCD (no atraviesa paredes
  finas al lanzarlo).
- **Menú, rueda o derribo con el agarre apretado**: el agarre se anula (lo de física se suelta; el compañero y el objeto de
  la aleta se quedan como estaban) y no vuelve a contar hasta abrir la mano: cerrar el menú con el agarre apretado ya no
  coge lo que haya al alcance ni tira el objeto de la aleta.
- **Velocidad para lanzar**: la media de los últimos 70 ms (`TNVRHands::FHandVelocityWindow`), igual a 72, 90 o 120 Hz; un
  tirón suelto del seguimiento ya no lanza.

Al soltar el agarre:

| Tenías cogido | Con impulso (la mano a más de 2,5 m/s, `VRThrowSpeed`) | Despacio |
|---|---|---|
| Un compañero | Lo lanzas hacia donde va la mano | Lo dejas en el suelo |
| Un objeto recién cogido | Si es arrojadizo (proyectil, tinta, objeto de carrera, concha): sale hacia donde va la mano | Se queda en la aleta (el gatillo lo usa) |
| El objeto que ya llevabas | Igual: si es arrojadizo, lo lanzas | Se te cae al suelo |
| Algo con física | Sale con la velocidad de la mano más la del cuerpo (con tope de 16 m/s) | Se queda donde lo sueltas (andando, sigue con tu velocidad) |

Sin gafas nada de esto cambia: E, clic y las teclas de siempre.

## Primera persona (sin gafas)

Ajustes > Juego > **Cámara**: Tercera persona (de serie) o Primera persona; también con **T** o el **clic del stick
derecho** en el juego (fila «Cambiar de cámara» de Controles, en «Jugando»: se cambian como las demás y nunca coinciden con
la de hablar, V de serie, que pide la estación de voz del tutorial), y con `TN.Camera` en la consola (manda sobre el
ajuste). Con un menú o una rueda a la vista no cambia. Es la misma cámara que en VR (`TortugaCharacter_FirstPerson.cpp`):

- En los ojos de la tortuga, también tumbada en el ragdoll y derribada (`TN_FirstPersonEyes.h`): entre los dos ojos de
  la malla, unos 20 cm por encima del hueso `Head`, que en `TotugaDemo_Rig` está en la base del cuello (a 6 cm de él, la
  vista salía del cuello y la lengua se veía por encima). De pie va con el hueso y el giro del cuerpo, no con el de la
  cabeza (la espera la gira y la ladea), suavizada para que el paso no menee; tumbada, con la cabeza girada como esté. Una
  malla de arte con otra cara puede llevar un socket `Eyes`. La lengua queda debajo: se ve al mirar abajo.
- Del cuerpo propio se ve todo menos la cabeza (se oculta el hueso `Head`, el casco y las piezas de Arte pegadas a él, como
  `Turtle.Eyes`, solo en tu máquina): al mirar abajo,
  el cuerpo, las aletas, la lengua y las gotas de sudor. Solo mientras la vista es la de tu tortuga
  (`ATortugaCharacter::IsLocalViewTarget`): en el probador, con la almeja o el gusano, o durante un fundido hacia otra
  vista, se pinta entera (ocultar un hueso vale para todas las cámaras). Con pantalla dividida la cabeza no se oculta (la
  otra vista la vería sin cabeza).
- Los ojos (la cámara y, en VR, el origen del seguimiento) van colgados de la cápsula y se ponen respecto de ella cada
  fotograma: lo que mueva la cápsula después (una base que se mueve, una corrección de red) los lleva consigo.
- La tortuga mira hacia donde mira la cámara, también para los demás (`bFirstPersonPlayer` replicado).
- En el caparazón, la vista es desde dentro y mucho más oscura, como en VR.

## Cómo probar sin gafas

1. **Pruebas automáticas** (lógica pura, sin mundo): `Automation RunTests Tortunabo.VR`. Once pruebas: rayo del puntero
   contra el panel (`RayPanelHit`), HUD que sigue a la cabeza (`LazyFollowYaw`), giro por pasos (`SnapTurnStep`),
   distancia y escala del panel (`PanelPlacement`), botones de los mandos en los menús (`MenuKeys`), panel curvo
   (`CurvedPanel`), gatillos y agarres analógicos (`AnalogButton`), umbral del gatillo en el juego y gatillo apretado al
   abrir un menú (`TriggerThreshold`), velocidad de la mano respecto del cuerpo (`HandVelocity`), arco del menú sin gafas
   (`SimulatedMenuArc`) y tecla de cambiar de cámara (`CameraKey`). Y doce de las manos (`TN_VRHandsTest.cpp`): media de la
   velocidad para lanzar (`HandVelocityWindow`), agarre enganchado (`GrabStrain`), viñeta de confort (`ComfortVignette`) sin
   bajar la de la escena ni pisar la del caparazón ni quedarse puesta en pausa (`ComfortVignetteLayer`), sitio del HUD
   (`HudProbe`), botones con la punta (`Poke`), gatillo entre el menú y el juego (`TriggerMenuLatch`) y, con un mundo de
   prueba, mano contra una pared (`HandBlock`), objeto que lleva otro (`GrabHolder`), nada que coger al otro lado de una
   pared fina (`GrabThroughWall`), la caja más cercana que se ve y no la de detrás de la pared (`GrabNearestVisible`) y
   objeto destruido en la mano, que sale del registro (`GrabDestroyedInHand`). Y las de los vehículos, `Tortunabo.VR.Vehicle.*`:
   volante con una o dos manos (`WheelAngle`), apuntado con la mano (`HandAim`), inclinación con la cabeza (`HeadLean`)
   y volante y asas a mano desde los ojos (`Reach`). Y la altura de los ojos de la primera persona respecto del hueso de la
   cabeza, por encima de la boca, y la de las gafas (`FirstPersonEyes`). Sin ventana:
   `UnrealEditor-Cmd Tortunabo.uproject -ExecCmds="Automation RunTests Tortunabo.VR; Quit" -nullrhi -unattended`.
2. **Modo simulado** en PIE (1 o 2 jugadores): consola `TN.VR 2` en la ventana que quieras probar. Lista de pruebas abajo.
3. **Meta XR Simulator** (opcional, para probar el modo gafas de verdad sin gafas): el simulador de Meta hace de gafas y
   mandos OpenXR con el teclado y el ratón (o un mando de consola). Se descarga de la web de desarrolladores de Meta, se
   activa como runtime OpenXR (su guía explica cómo; en resumen, apuntar el runtime OpenXR activo a su archivo `.json`) y
   luego Play > «VR Preview». Sirve para ver la vista estéreo, el láser, los botones y la capa de carga.

## Con las Quest

### A) Conectadas al PC: Meta Quest Link (recomendado)

La forma más rápida y fiable: el juego corre en el PC con todos los gráficos (Lumen, sombras virtuales) y las gafas son la
pantalla. El multijugador por Steam funciona igual que siempre.

1. En el PC (Windows): instalar la app **Meta Quest Link** (antes «Oculus»). En su Ajustes > General: **runtime OpenXR**,
   ponerla como activa, y **Orígenes desconocidos**, activado. Sin este último, al lanzar el juego las gafas enseñan un
   aviso de «dispositivo o aplicación externa» que manda a Ajustes y el juego no se ve en ellas.
2. En las gafas: activar **Link** (cable USB-C bueno) o **Air Link** (misma red wifi, 5 GHz).
3. **Editor**: abrir el proyecto (la primera vez compila el módulo con los plugins nuevos, OpenXR y XRBase, y compila
   shaders) y Play > **VR Preview**. El modo VR se enciende solo (Automático). `TN.VR.Status` debe decir «gafas».
4. **Build**: Plataformas > Windows > Empaquetar proyecto (Development). Arrancar con Link activo:
   `Tortunabo.exe -vr` (un acceso directo con `-vr` al final del destino). Sin `-vr` sale el juego plano de siempre.
5. Si va a tirones: Ajustes > Gráficos a Media o Alta, y en la consola `vr.PixelDensity 0.8`. La opción de proyecto
   **Instanced Stereo** (Ajustes del proyecto > Motor > Renderizado > VR) acelera mucho, pero obliga a recompilar todos los
   shaders: no está puesta de serie.

Nota: con el plugin OpenXR activo y SteamVR instalado, algún PC puede abrir SteamVR al arrancar el juego plano. Si molesta,
arrancar con `-nohmd`.

### B) En las propias Quest, sin PC (experimental)

Ya está configurado para empaquetar para Meta Quest:

- `Config/DefaultEngine.ini`, `[/Script/AndroidRuntimeSettings.AndroidRuntimeSettings]`: paquete `com.mokius.tortunavy`,
  `bPackageForMetaQuest`, Vulkan, arm64, SDK 32 y permiso de micrófono.
- `Config/Android/AndroidEngine.ini`: las dos vistas en una pasada (`vr.MobileMultiView`, sin HDR móvil), MSAA x4, subsistema
  en línea NULL (Steam no existe en Android) y `IpNetDriver`.
- `Config/Android/AndroidGame.ini`: `bStartInVR` (arranca en las gafas).
- `Tortunabo.Build.cs`: el módulo de Steam solo se carga en escritorio.

Pasos:

1. Instalar Android Studio (la versión que pide UE 5.6: Koala 2024.1.2), abrir su SDK Manager una vez y ejecutar
   `Engine/Extras/Android/SetupAndroid.bat` del motor (instala el SDK, las build-tools y el NDK r27c). Reiniciar el editor.
2. Gafas en modo desarrollador (app Meta Horizon del móvil > Dispositivos > Modo desarrollador), conectadas por USB y
   aceptada la depuración USB en las gafas.
3. En el editor: Plataformas > Android > formato **ASTC** > Empaquetar proyecto. Instalar con el
   `Install_Tortunabo-arm64.bat` que deja al lado (o `adb install`). También se puede lanzar directamente a las gafas desde
   Plataformas > Android > (las Quest) > Lanzar.
4. El juego sale en «Biblioteca > Orígenes desconocidos».

Limitaciones: el renderizador móvil no tiene Lumen, sombras virtuales ni trazado de rayos (se verá más plano); el mapa
procedural se genera en las gafas (tarda más y puede ir justo de rendimiento); las partidas son solo en red local (sin
Steam, así que no se juega con los del PC). Para mañana, mejor la opción A.

## Piezas

| Pieza | Dónde | Qué hace |
|---|---|---|
| `ETNVRMode`, `TNVR::*`, `FTNVRKeys` | `VR/TN_VRMode.*` | El modo actual y las ayudas que usa todo el juego (ver «Reglas para código nuevo»); los botones de los Touch por nombre. |
| `UTN_VRSubsystem` | `VR/TN_VRSubsystem.*` | Decide el modo cada fotograma, crea el rig en cada mundo de juego, pone y quita los ajustes de confort, registra el procesador de entrada, la capa de carga de las gafas y los comandos `TN.VR*`. |
| `UTN_VRSeatComponent` | `VR/TN_VRSeatComponent.*` | Asiento de los vehículos con gafas (ver «Vehículos»): origen del seguimiento, cámara sentada, manos que llegan del rig y las que ven los demás. |
| `ATN_VRRig` | `VR/TN_VRRig.*` | El jugador local en VR (solo en su máquina): manos con `UMotionControllerComponent` (LeftGrip, RightGrip, RightAim, LeftAim), panel de la interfaz (`UWidgetComponent` plano e invisible) y su malla curva (`CurvedPanel`), la playa en 360 de la carga (`LoadingDome`), láser (`UWidgetInteractionComponent` con rayo propio), el contexto de entrada `IMC_VR` (prioridad 10) sobre las acciones de siempre, los agarres (coger y lanzar), el giro y el recentrado. Sin peón (menú principal), la vista es su cámara. |
| `UTN_VRGrabComponent` | `VR/TN_VRGrabComponent.*` | En la tortuga: coger objetos con física con la mano (servidor si el actor se replica, local si no), quién lleva cada objeto, agarres enganchados y cápsula que no choca con lo que lleva. |
| Manos del rig | `VR/TN_VRRigHands.cpp` | Agarres (coger, lanzar, anular), manos contra el escenario (`ATN_VRRig::BlockHandLocation`), vibración de los mandos y viñeta de confort. |
| `TNVRHands` | `VR/TN_VRHandMath.h` | Cuentas de las manos sin mundo: ventana de velocidad, agarre enganchado, toques de vibración, viñeta y sitio del HUD; la fuerza de la viñeta y la vibración desde los ajustes. |
| `TNVRControls` | `VR/TN_VRControls.*` | Nombre en pantalla de cada botón Touch, botón de cada acción y guía de controles de Ajustes (#644, #647). |
| Primera persona | `Player/TortugaCharacter_FirstPerson.cpp` | Cámara en la cabeza (con y sin gafas), cuerpo sin cabeza (solo vista desde ella), caparazón oscuro, tecla de «Cambiar de cámara» (`FTNGameSettings::CameraKey`/`CameraPadKey`, fila `Camera` de `UTN_GameSettingsSubsystem`) y `TN.Camera`. |
| IK de los brazos | `Player/TN_TurtleAnimInstance.cpp` (`ReachArm`) | Las manos del cuerpo van a los mandos en VR. |
| `UTN_VRScreenWidget` | `VR/TN_VRScreenWidget.*` | La pantalla VR: lienzo de 1920 × 1080 donde van todos los widgets de pantalla completa con su ZOrder. Se quitan con `RemoveFromParent` de siempre. |
| `FTNVRInputProcessor` | `Private/VR/TN_VRInputProcessor.*` | Preprocesador de Slate: con un menú delante convierte los botones VR en las teclas de mando que ya entienden todos los menús y los gatillos en clics del láser; simulado, el clic y la rueda del ratón sobre la imagen del juego van al láser. Jugando no toca nada. |
| `TNVRMath` | `VR/TN_VRMath.h` | Cuentas sin mundo (las prueba `Tortunabo.VR.*`). |
| Tortuga | `Player/TortugaCharacter_VR.cpp` | `SetVRView` (cámara VR, cuerpo oculto, giro con la cabeza), `AddVRYaw`, `TickVRView`, `GetTurtleAimRotation`, `ServerSetVRAim`, `bVRPlayer`. |
| Entrada OpenXR | `Config/DefaultInput.ini` | Asignaciones clásicas `TNVR_*` con los botones de los Touch: OpenXR crea sus acciones con ellas (sin ellas no llegan los botones). |
| Ajustes | `FTNGameSettings::VRMode`, `VRTurn`, `VRVignette`, `bVRHaptics` | Ajustes > Juego > Realidad virtual. |
| Fantasma | `Player/TN_GhostCameraModifier.*`, `ATN_VRRig::IsGhostVRView` | La vista del fantasma con gafas (#646). |

Comandos: `TN.VR`, `TN.VR.Status`, `TN.VR.Recenter`, `TN.VR.HudDistance` (150), `TN.VR.HudFov` (80, arco del HUD),
`TN.VR.HudFollow` (0 anclado a la cámara), `TN.VR.MenuDistance` (160), `TN.VR.MenuFov` (100, arco de los menús),
`TN.VR.LoadingDomeRadius` (300), `TN.VR.SmoothTurnSpeed` (120), `TN.VR.Haptics` (1), `TN.VR.ComfortVignette` (1),
`TN.Camera` (-1), `TN.FirstPerson.ShellLight` (0,2). `TN.VR.Status` dice también qué hace cada mano (libre o parada por el
escenario, qué agarra), la viñeta de confort y la vibración de cada mando.

## Reglas para código nuevo

Para que todo lo nuevo se vea y funcione en VR:

- Widgets de pantalla: `TNVR::AddToScreen(Widget, ZOrder)` en lugar de `AddToViewport`, y `TNVR::IsOnScreen(Widget)` en
  lugar de `IsInViewport`. Quitar, con `RemoveFromParent` de siempre. Slate suelto: `TNVR::AddSlateToScreen` /
  `TNVR::RemoveSlateFromScreen`. (Lo que se cuele al viewport lo recoge el rig al empezar el mundo, pero no se ve en las
  gafas si se añade después.)
- Un menú debe enseñar el cursor (`SetShowMouseCursor(true)`): así el panel pasa a modo menú y sale el láser.
- Cambiar de vista: `SetViewTargetWithBlend(X, TNVR::ViewBlendTime(segundos))`; una cámara de escena que se mueve sola,
  solo si `!TNVR::KeepFirstPersonView()`.
- Hacia dónde se lanza o se usa algo: `ATortugaCharacter::GetTurtleAimRotation()` (no `GetControlRotation()`), y en el
  cliente llamar a `SendVRAimToServer()` antes del RPC al servidor que lo usa.
- Efectos de cámara nuevos: los modificadores con «Shake» en el nombre se apagan solos en VR.

## Límites conocidos

- Las direcciones del stick como botones (`Thumbstick_Up`...) no se declaran para OpenXR; los menús usan el eje del stick
  (con repetición al mantener).
- Los objetos del inventario van siempre a la aleta derecha, aunque se cojan con la izquierda.
- El HUD es el de siempre en un panel curvo (no está repartido por el mundo). La pantalla dividida no está pensada para VR.
- Los brazos de la tortuga son más cortos que los de una persona: si el mando está más lejos, la aleta se estira hacia él
  hasta donde llega.
- Un objeto con física que se replica llega a la mano con el retraso de la red (lo mueve el servidor).
- Un actor replicado cogido por la etiqueta `VRGrab` (no un `ATN_PhysicsObjectActor`) se queda despierto en red tras
  soltarlo: no tiene temporizador para volver a dormirse.
- La capa de carga de las gafas es un huevo cerrado quieto (el de verdad, con su animación, sale en el panel).
- Las cámaras de escena de la almeja y el gusano no se ven en VR (se sigue en primera persona).

## Pruebas

Sin gafas (modo simulado, PIE):

1. `Automation RunTests Tortunabo.VR`: las diecinueve pasan (también `TriggerThreshold`, `HandVelocity`, `SimulatedMenuArc`,
   `CameraKey` y las ocho de las manos).
2. Menú principal con `-vrsim` (o `TN.VR 2` en la consola y volver al menú): el menú sale en un panel delante; el ratón
   mueve el puntero sobre el panel y el clic pulsa los botones; la rueda baja las listas; «Ajustes» y «Crear partida» van.
   Los botones del editor (parar PIE) se siguen pudiendo pulsar con el menú abierto.
3. Lobby con `TN.VR 2`: primera persona a la altura de la tortuga, sin ver el propio cuerpo (sí la sombra); dos aletas
   delante que se mecen un poco; el HUD en un panel delante; el ratón gira la vista y la tortuga gira con ella; andar va
   hacia donde miras. `TN.VR 0`: vuelve la tercera persona de siempre con el HUD en pantalla.
4. Coger un objeto: sale en la aleta derecha. Lanzarlo: sale hacia donde mira la cámara. Con 2 jugadores, el otro ve tu
   tortuga girar hacia donde miras y el objeto en su aleta.
5. Menú de pausa (Escape/Tabulador): el panel se queda quieto delante, entero en la ventana (sin cortar los lados ni las
   esquinas, también mirando arriba o abajo al abrirlo); el ratón apunta y pulsa lo que hay bajo el cursor; cerrarlo:
   vuelve el HUD. Tienda, probador (sin fundido) y general: igual.
   Con `TN.VR 2`, E junto a un objeto del suelo lo coge (por cercanía, como sin VR: las aletas simuladas no llegan al suelo).
6. Ruedas de emotes y de frases: salen en el panel y se eligen como siempre.
7. Carrera: cuenta atrás, reloj de ronda, recuento, campeón y «¡ADELANTE!» salen en el panel; el huevo de carga también.
8. Espectador/fantasma: se ve desde la cámara de la tortuga seguida, sin cámara libre; ←/→ cambian de tortuga sin fundido.
   Con gafas (#646): la cabeza va sobre la tortuga seguida sin retardo y, aunque esa tortuga se vuelva o gire su jugador, la
   vista no gira; solo con tu cabeza. Al revivir, la cáscara tapa toda la vista (no solo el panel) y se aclara al abrirse.
9. Ajustes > Juego > Realidad virtual: «Modo VR» Simulado/Desactivado cambia en el acto; «Giro en VR» se guarda.
   «Restablecer esta pestaña» los deja en Automático y 30°. «Viñeta de confort» (Apagada/Normal/Fuerte) y «Vibración de los
   mandos VR» se guardan y se notan en el acto (#647); Ajustes > Controles, al final, lista los botones Touch (solo lectura).
   Con `TN.VR 2`, el aviso de interactuar dice «Gatillo derecho» y el tutorial nombra los botones Touch (#644).
10. Temblor de cámara y ojo de pez no se notan con el modo VR puesto, aunque estén encendidos.

Con las Quest (VR Preview o `-vr`):

11. `TN.VR.Status`: «Modo VR: gafas · OpenXR: sí · gafas conectadas: sí · estéreo: sí».
12. Mirar alrededor: la vista sigue a la cabeza sin retraso; al agacharse, la vista baja. Clic del stick derecho: recentra.
13. Las aletas siguen a los mandos; si un mando se apaga, su aleta desaparece.
14. Stick izquierdo anda hacia donde miras; stick derecho gira a pasos de 30° (probar 45° y suave en Ajustes).
15. A salta, B caparazón, agarre izquierdo corre, gatillo derecho interactúa. Coger un objeto: sale en la aleta derecha;
    lanzarlo apuntando con la aleta a un lado: sale hacia allí (también al compañero que llevas, y la tinta).
16. Menú (botón del mando izquierdo): la pausa sale delante y quieta; el láser de la aleta derecha apunta (punto en el
    panel) y el gatillo pulsa; A/B aceptan y van atrás; los agarres cambian de pestaña; el stick se mueve por las filas.
    Menú otra vez: se cierra.
17. Y mantenido: rueda de emotes; elegir con el stick derecho y soltar.
18. Viajar del lobby a la partida: el huevo cerrado sale en las gafas mientras carga (sin imagen congelada) y se rompe en
    el panel al empezar.
19. Nada tiembla ni se deforma; no hay fundidos de cámara.
20. Gatillo derecho junto a un botón o un cofre: interactúa (también sin apretar a fondo). En un menú, el gatillo hace clic.
21. Agarre con la mano pegada a un objeto del suelo: se coge; con la mano lejos (aunque el cuerpo esté cerca), no.
    Soltarlo con un gesto de lanzar: sale hacia donde iba la mano. Agarre derecho sin nada cerca y soltar despacio: se cae.
22. Agarre junto a una pelota o una caja con física: va con la mano y choca con lo demás; lanzarla con el gesto.
23. Agarre junto a un compañero en el caparazón: se coge; soltar con impulso lo lanza hacia allí, despacio lo deja.
24. Mirar abajo: se ven el cuerpo, los brazos siguiendo a los mandos, la lengua y el sudor; no la cabeza. Bailando, en el
    caparazón y tumbada, los brazos van con su animación. Otro jugador ve tus brazos moviéndose con tus manos.
25. Derribo con ragdoll: la vista va con la cabeza. Caparazón: se ve desde dentro, oscuro.
26. HUD: fijo en la vista, curvo y grande; pegado a una pared, se acerca. Menús: curvos y rodeándote. Carga: la playa en 360.

Sin gafas (primera persona):

27. T (o clic del stick derecho): cambia a primera persona y vuelve. Ajustes > Juego > «Cámara» igual, y se guarda. V con
    pulsar para hablar solo habla (no cambia la cámara). En Controles, «Cambiar de cámara» (en «Jugando») se cambia a otra
    tecla; ponerle V la cambia con «Hablar» (se intercambian). Con el menú de pausa abierto, T no cambia la cámara.
28. Mirar abajo: cuerpo, aletas, lengua y sudor; sin cabeza ni casco. Andar de lado: la tortuga mira a la cámara; el otro
    jugador la ve girar igual.
29. Derribo con ragdoll: la vista va pegada a la cabeza. Caparazón: desde dentro y muy oscuro; al salir se aclara.
30. En primera persona, entrar al probador: te ves entera, con cabeza y casco en su sitio; al salir vuelve la primera
    persona sin cabeza. Igual con la almeja y el gusano de la playa (sin gafas).
31. En primera persona, subirse a algo que se mueve (plataforma, balsa): la vista va con la tortuga sin temblar ni ir a
    tirones.

Con las Quest, lo arreglado en la revisión del 30-09-2026:

32. Gatillo derecho apoyado o a medias (menos del 55 %) junto a un cofre o un botón: no interactúa; apretado, sí. Igual
    el gatillo izquierdo con la rueda de frases.
33. Abrir un menú con el gatillo (interactuar con la tienda o el general) y mantenerlo apretado: el menú no hace clic en lo
    que haya bajo el láser; soltar y volver a apretar sí. Abrir un menú con un agarre apretado: no cambia de pestaña.
34. Llevando un objeto del suelo en la mano, andar o saltar con la mano quieta y abrir el agarre: se suelta (no se lanza).
    Girar a pasos y soltar: tampoco lo lanza. Un gesto de lanzar andando: sale hacia donde va la mano.
35. En un cliente (no el anfitrión), coger con la mano una pelota o una caja con física (`ATN_PhysicsObjectActor`, las que
    ruedan): va con la mano en su pantalla y en la de los demás; al soltarla sigue rodando para todos y, parada, se queda
    en el mismo sitio en todas las máquinas. Un cubo de empujar (sin física) no se coge.
36. Soltar despacio una caja con física andando: cae y sigue un poco hacia delante (con la velocidad del cuerpo).
37. Entrar al probador con gafas: se ven las aletas sueltas y el láser; al salir, los brazos del cuerpo vuelven a los
    mandos y la cabeza propia no se ve.

Con las Quest, el pulido de las manos del 03-10-2026:

38. Meter la mano en una pared o bajarla al suelo: la aleta (y el brazo de la tortuga) se queda en la superficie, con una
    vibración corta al tocarla. Con la mano metida en una pared fina (una valla, la pared de una caseta), al otro lado no se
    coge una caja, no se recoge un objeto, no se rebusca, no se coge a un compañero ni se pulsa nada; asomando la mano por
    encima o por un lado, sí. Lo mismo desde un cliente (el servidor también lo comprueba).
39. Coger una caja con física y empujarla contra una pared: vibra cada vez más y, si se queda enganchada, se suelta sola
    (vibración fuerte); no atraviesa la pared. Lanzarla fuerte contra una pared fina: rebota, no la atraviesa.
40. Con la caja en la mano, ponerla bajo los pies o delante y andar: la tortuga no se sube encima ni la empuja.
41. Dos jugadores con gafas: uno coge una caja; el otro intenta cogerla: no puede (vibra al rechazarlo). Al soltarla, sí.
42. Con algo en la mano, que te derriben o meterte en el caparazón: se suelta (en las dos máquinas) y vibran los dos mandos.
43. Abrir la pausa con el agarre derecho apretado y un objeto en la aleta: no se cae al suelo. Cerrarla con el agarre aún
    apretado: no coge nada; abrir y volver a apretar, sí.
44. Soltar despacio un objeto de la aleta con la mano temblando: se cae (no se lanza). Un gesto de lanzar corto y rápido:
    se lanza. Comparar a 72 y a 90 Hz (Ajustes de las Quest).
45. Vibración: coger, soltar, lanzar, tocar la pared, el láser al pasar por un botón y al pulsar. `TN.VR.Haptics 0` la quita.
46. Andar deprisa, caer desde alto, salir lanzado en una catapulta y el giro suave: los bordes se oscurecen; parada o a
    pasos, no. Al empezar a andar despacio, los bordes no se aclaran ni un momento. Girando suave, abrir la rueda de emotes:
    los bordes vuelven a su sitio mientras está abierta. `TN.VR.ComfortVignette 0` la quita y `2` la dobla. Comprobar que se nota sin molestar (si es poca, subir
    `TNVRHands::VignetteMaxIntensity`).
47. HUD: mirar abajo andando por la playa: el suelo no tapa la parte de abajo del HUD (se acerca un poco). Con una caja en la
    mano delante de la cara, el HUD no se viene a la cara.
48. Botón del mapa (o interruptor del procedural): tocarlo con la punta de la aleta lo pulsa una vez (vibra); dejar la mano
    apoyada no lo vuelve a pulsar; apartarla y volver, sí. Pasar la mano despacio por encima no lo pulsa.
49. Abrir la tienda (o el general) con el gatillo y cerrarla con el clic del láser en «Cerrar» sin soltar el gatillo: no se
    vuelve a abrir; soltar y apretar otra vez junto al tendero, sí. Abrirla con el gatillo y soltarlo dentro: al cerrarla con
    B, el gatillo no interactúa solo.

Vehículos (#529) sin gafas: `LVL_ProcMap?game=Karts?ProcSeed=11` (o con `?BotDriver` para ir de artillera), `TN.VR 2`:

50. La vista va en el asiento, sin brazo de cámara; la cabeza y el casco propios no se ven; el HUD, en el panel.
51. `TN.VR.SeatPose 30`: la conductora gira el volante 30° y el registro dice dirección 0,33 y las ruedas giradas. De
    artillera, `TN.VR.SeatPose 0 60 10`: el apuntado y la torreta del servidor quedan en 60° y 10°; las asas se ven.
52. `TN.VR 0`: vuelve la cámara de persecución de siempre; sin gafas el kart conduce, usa objetos y dispara como antes.

Vehículos (#529) con las Quest:

53. Al sentarse, la vista queda en los ojos de la tortuga mirando al morro (recentrado solo); el clic del stick derecho
    recentra sentada. Nada tiembla ni cambia el FOV con el turbo o al aterrizar.
54. Volante con las dos manos: gira con ellas, llega a tope a un cuarto de vuelta y, al soltarlo, vuelve al centro. Con
    una sola mano también. Coger o soltar una mano no da tirones. Sin cogerlo, el stick izquierdo gira.
55. Gatillo derecho acelera y el izquierdo frena (y da marcha atrás parado). A turbo, X freno de mano, Y enderezar
    (mantener: reaparecer), B usar el objeto, stick derecho adelante disparar sola, cualquier stick atrás «hacia atrás».
56. Artillera: las asas se ven delante del pecho y giran con la torreta; al cogerlas, la torreta apunta hacia donde
    apuntan las manos (también arriba y abajo, limitada); gatillo derecho dispara, el izquierdo usa el objeto.
57. Artillera de los karts: apartar la cabeza a un lado del asiento inclina el kart hacia ese lado (el HUD lo enseña).
58. Kart con una sola tortuga: la torreta sigue a la cabeza y dispara hacia donde se mira.
59. Con dos jugadores (uno con gafas y otro sin ellas, en el mismo kart y en karts distintos): cada uno ve los brazos de la
    tortuga con gafas en el volante o en las asas, sin tirones; quien va sin gafas juega como siempre.

## Dudas para la prueba con gafas

Cosas que no se pueden comprobar sin gafas y que pueden fallar (anotadas en la revisión del 30-09-2026). Si alguna falla,
abrir un fallo del objeto «Modo VR» con lo que se vio.

- **Gatillo y agarre al cerrar un menú**: con el menú delante el procesador deja pasar al juego la suelta de los ejes
  (que no se quede con el valor de antes del menú) y, si el gatillo se apretó en el menú, no deja pasar nada de él hasta
  soltarlo (`TNVRHands::ShouldEatTriggerAxis`). Depende de que OpenXR mande el eje cuando cambia: comprobar la prueba 49.

- **Stick mantenido al abrir un menú**: el primer fotograma puede mover el foco un paso.
- **Gatillo rondando el 55 % en el juego**: ya tiene histéresis (suelto por debajo del 35 %); comprobar rebuscando con el
  gatillo a medias que no se corta y que soltarlo del todo sí lo corta.

- **Velocidad para lanzar**: es la media de los últimos 70 ms; comprobar que un lanzamiento rápido y corto pasa de
  `VRThrowSpeed` (250 cm/s) y que la dirección es la del gesto (si se queda corto, bajar
  `FHandVelocityWindow::DefaultWindowSeconds`).
- **Recentrar con algo en la mano**: el seguimiento salta; la velocidad se descarta, pero el objeto con física puede dar un
  tirón hasta la nueva posición de la mano.
- **Coger por red con mucho ping**: el servidor acepta la mano con 60 cm de margen (`ServerGrabSlack`); si lo rechaza, el
  cliente lo suelta (`ClientGrabRejected`), pero puede verse cogido un instante.
- **Actores con la etiqueta `VRGrab`** que no son `ATN_PhysicsObjectActor`: se quedan despiertos en red tras soltarlos.
- **`bVRPlayer` y `bFirstPersonPlayer` solo para los demás (SkipOwner)**: comprobar con dos jugadores que el otro ve girar
  la tortuga con la cabeza al entrar y salir de VR o de la primera persona deprisa, y que al pasar a fantasma la tortuga
  que se queda no sigue girando sola.
- **Ojos colgados de la cápsula**: de pie con gafas no debería cambiar nada; comprobar en balsas y plataformas que la vista
  no tiembla y que tumbada (ragdoll) sigue pegada a la cabeza.
- **Fundido al probador sin gafas**: el cuerpo se pinta entero desde el primer fotograma del fundido; la cámara sale desde
  dentro de la cabeza y puede verse el casco un instante.
- **Manos del cuerpo**: comprobar que el esqueleto tiene los huesos `LeftHand` y `RightHand`; si no, el IK no actúa y no
  se ve ninguna mano (las aletas sueltas se ocultan cuando hay tortuga).
- **HUD tapado**: el HUD fijo a 150 cm respeta la profundidad; el suelo y las paredes ya lo acercan (centro, lados y borde
  de abajo), pero el cuerpo propio (`ECC_Visibility` lo ignora) aún puede tapar un poco su parte de abajo al mirar abajo.
- **Manos y escenario**: lo que no para la cámara (`ECC_Camera` en `Ignore`: algún decorado, el agua) no para la mano; y
  una pared con la cabeza metida dentro no para nada (no se sabe dónde está el otro lado).
- **Vibración**: OpenXR la mantiene un fotograma y el rig la vuelve a pedir cada uno; comprobar que los toques cortos
  (20-40 ms) se notan en los Touch y que no se queda vibrando al abrir un menú o al cambiar de mapa.

- **Probador con gafas**: la cámara del probador tiene `bLockToHmd` y el visor le pisa la posición (ya pasaba antes).
- **Agarres**: apoyar el dedo en el agarre derecho y soltarlo despacio puede tirar el objeto; llevando a un compañero
  cogido con un agarre, el de la otra mano ya no lo toca (el izquierdo corre), pero si se cogió con E o el gatillo,
  cualquier agarre lo suelta; si llegan a la vez el clic y el eje del agarre, la pestaña del menú puede cambiar dos veces.

- **Coger y lanzar muy deprisa**: si el objeto nuevo aún no ha llegado al inventario, se puede lanzar el anterior.
- **Alcance de la mano**: el servidor valida la interacción por la distancia al cuerpo, no a la mano, y puede rechazarla
  sin aviso.
- **Cúpula de la pantalla de carga**: el suelo tapa su mitad de abajo.
- **Vehículos (#529)**: los ojos van a una altura fija sobre la cadera (55 cm y 14 por delante,
  `UTN_VRSeatComponent::EyeAboveHip`): si la vista queda baja o dentro del caparazón, se ajusta ahí. El volante está donde
  caen las manos de la tortuga sentada (52 cm por delante y 25 por encima de la cadera); si cuesta cogerlo, subir
  `WheelGrabReachCm` (22 cm, `TN_Buggy_VR.cpp`). La torreta apunta con la pose de apuntar del mando, que va algo hacia
  abajo respecto del puño: comprobar que apuntar al horizonte con las asas cogidas no deja el cañón mirando al suelo.
  Recentrar al sentarse usa el mismo recentrado del motor que el clic del stick: si alguien se sienta de pie, la vista
  queda a su altura hasta que recentra sentado.
