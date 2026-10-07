# Ragdoll del derribo en red (#153)

Objetivo de diseño (decidido): al noquear a una tortuga, el cuerpo cae como un ragdoll físico que rueda y se asienta, se ve en el mismo sitio en todas las máquinas y, al acabar el noqueo, la tortuga se levanta donde quedó el cuerpo, también si otra tortuga lo ha empujado.

Código: `Source/Tortunabo/Public/Player/TN_RagdollNet.h` (datos, ajustes y decisiones puras), `Private/Player/TortugaCharacter_RagdollNet.cpp` (servidor y clientes) y `Private/Player/TortugaCharacter_Knockdown.cpp` (entrada y salida del ragdoll). Pruebas: `Tortunabo.RagdollNet.*`.

## Quién decide y quién simula

| Qué | Quién | Cómo llega a los demás |
|---|---|---|
| Noquear y levantarse | Servidor (`ApplyKnockdown`, `RecoverFromKnockdown`, solo con autoridad) | `bIsKnockedDown` (`OnRep_IsKnockedDown`) |
| Ragdoll que manda | Servidor: simula, lo empujan las demás tortugas y decide cuándo se asienta | `KnockdownRootPose` |
| Ragdoll que se ve en un cliente | Cada cliente simula el suyo (aspecto físico local) y lo corrige hacia la pose del servidor | — |
| Punto donde se levanta | Servidor (suelo bajo el cuerpo + `FindTeleportSpot`, redondeado a 0,1 cm) | `KnockdownStandLocation`, en la misma actualización que `bIsKnockedDown = false` |

Durante el derribo la tortuga no replica su movimiento (`SetReplicateMovement(false)`, como el ragdoll de la muerte): la posición del cuerpo viaja solo en la pose raíz. Al levantarse, todas las máquinas colocan la cápsula en `KnockdownStandLocation` y el servidor vuelve a replicar el movimiento. El dueño no recibe ninguna corrección al dar el primer paso, porque su cápsula está en el mismo punto que la del servidor.

## Qué se replica y a qué frecuencia

`FTNRagdollRootPose` (cuerpo raíz del Physics Asset), con `NetSerialize` propio y menos de 30 bytes por envío (prueba `Tortunabo.RagdollNet.PoseWire`):

- posición con 0,1 cm (como `FVector_NetQuantize10`), giro con 16 bits por eje y velocidad con 1 cm/s (no viaja si está asentado);
- `bActive` (hay ragdoll en el servidor) y `bSettled` (asentado).

El servidor la manda a 15 Hz (`RagdollNetTuning.SendRateHz`) mientras el cuerpo se mueve más de 1 cm, y una vez más en el momento de asentarse o de volver a moverse (`ForceNetUpdate`). La tortuga replica a 30 Hz (`SetNetUpdateFrequency(30)`), así que cabe. Asentado y quieto no manda nada. La primera pose y la última viajan en la misma actualización que `bIsKnockedDown`, de modo que un cliente nunca persigue la pose de un derribo anterior.

## Corrección en los clientes

Cada fotograma (`ClientCorrectKnockdownRagdoll`), con el cuerpo raíz local frente a la pose recibida adelantada con su velocidad (antigüedad + media latencia, con un tope de 0,25 s):

1. **Más de 2 m** (entrar tarde, una pérdida larga): todo el ragdoll se traslada y gira alrededor del raíz de golpe y toma la velocidad del servidor.
2. **Asentado en el servidor**: el ragdoll se traslada al punto exacto (sin girar, para no meter las patas en el suelo), sin velocidad, y se duerme (`PutAllRigidBodiesToSleep`, sin dejar de simular, como el cadáver congelado). Un empujón en el servidor lo despierta.
3. **En marcha**: un cambio de velocidad a todos los cuerpos hacia la velocidad del servidor más 6 cm/s por cm de error (muelle amortiguado). Zona muerta de 3 cm en horizontal y de 25 cm en vertical: la altura del cuerpo raíz depende de la postura en que caiga, y corregirla haría flotar el cuerpo.

Corregir con velocidad, y no teletransportando, deja que la colisión continua y el contacto con el suelo sigan mandando: la corrección nunca empuja el cuerpo a través del terreno. El traslado de golpe solo pasa en los casos 1 y 2 y se descuenta de la sonda que devuelve el cuerpo encima del suelo si lo atraviesa (`TickKnockdownRagdoll`).

Resultado medido en la prueba `Tortunabo.RagdollNet.LaggedFollow` (150 ms de ida y vuelta, envíos a 15 Hz, el cliente empieza a 40 cm): menos de 10 cm de error mientras resbala y el mismo punto (< 0,1 cm) al asentarse.

## Empujar el cuerpo

El perfil de colisión `Ragdoll` no choca con cápsulas, así que el empuje lo decide el servidor (`ServerPushKnockdownRagdoll`): una tortuga a menos de 75 cm en horizontal, a una altura parecida (120 cm) y que camina hacia el cuerpo a más de 50 cm/s le da una aceleración de 1800 cm/s² hacia fuera, sin llevarlo nunca más rápido que ella. El empujón despierta el cuerpo si estaba asentado. Los clientes lo ven llegar con la pose; en el anfitrión es inmediato.

## Asentado

En el servidor, 0,4 s seguidos con el raíz por debajo de 15 cm/s y 45 °/s: velocidad cero y a dormir. Así el cuerpo no rueda indefinidamente por microsacudidas y todas las máquinas lo dejan en el mismo punto.

## Alternativas descartadas

- **Solo el servidor simula y los clientes ponen la pose de todos los huesos** (replicar los ~15 cuerpos): 10 veces más datos, y con 150 ms el ragdoll del cliente se ve a saltos o con retraso. La pose raíz mantiene la física local fluida y la posición del servidor.
- **Simulación determinista igual en todas las máquinas** (como la bola, `Multicast_InitializeThrow`): Chaos no es determinista entre máquinas con contactos y articulaciones; la bola sí funciona porque es un proyectil sin contactos hasta que para.
- **Volver a replicar el movimiento de la cápsula durante el ragdoll**: el cliente intenta reconciliar una malla simulada («Attempting to move a fully simulated skeletal mesh»); por eso el ragdoll de la muerte ya lo apaga.
- **Congelar el ragdoll como el de la muerte** (`ServerFreezeRagdoll`, parar la simulación): el cuerpo ya no se podría empujar. Aquí solo se duerme.
- **Que cada máquina calcule dónde se levanta con su ragdoll** (lo de antes): cada una lo levantaba en un sitio distinto y el dueño recibía una corrección del movimiento al dar el primer paso.

## Sobre patrones anteriores

- KIRK-01 («el derribo no usa ragdoll, solo tilt») quedó superado antes de esta issue: el derribo ya usa ragdoll con `bUsePhysicsRagdoll` y el tilt solo queda sin Physics Asset (ruta B, cuyo movimiento sigue replicándose con el de la cápsula).
- Se mantienen `SetReplicateMovement(false)` durante el ragdoll, la colisión continua en todos los cuerpos y la sonda contra el suelo.

## Lo que falta probar en el editor

PIE 2P y 4P con `NetEmulation.PktLag 150` y `NetEmulation.PktLoss 2`: noquear, empujar el cuerpo con otra tortuga, ver el desfase al asentarse (`[Ragdoll]` en el registro) y levantarse. El Physics Asset de la tortuga no se ha tocado.
