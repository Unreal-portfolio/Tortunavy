# Karts en el mapa del cooperativo

Modo aparte del Rally de `LVL_Rally` (que no cambia): carreras de karts —el buggy del Rally, `ATN_Buggy`— por el camino del
mapa generado del cooperativo, con cajas de objetos al estilo de las carreras de karts, géiseres, cascadas y agua. Decisión
de Mokius del 03-10-2026 (#40). Issues: #291 (modo), #304 (objetos), #293 (géiseres, cascadas y agua), #295 (artillera).

## Cómo se juega

- **Se elige** como los demás modos: «Karts» en el menú al crear sala, en la sala o con el General Galápago (pestaña
  «Misión»). El anfitrión elige también la dificultad (la del cooperativo) y si va **una tortuga por kart** o **por
  parejas** (la segunda de cada kart es la artillera). Al salir del lobby se viaja a `LVL_ProcMap?game=Karts`.
- **Salida**: nadie se sienta hasta que todas las máquinas tienen el mapa y el suelo con colisión (como mucho 40 s); luego el
  calentamiento y el semáforo del Rally. Los bots completan la parrilla hasta 4 karts.
- **Pista**: puertas cada 250 m (regla del 60 %), con alas de rocas a los lados para que nadie se las salte por fuera; la
  salida donde cabe una puerta, la meta en la playa final. Contramano, fuera de pista, atascos y reaparición, los del Rally.
- **Al acabar** (45 s tras la primera en llegar y los resultados), todas vuelven al lobby. El anfitrión también puede
  volver desde el menú de pausa.

## Objetos (#304)

Cajas «?» en filas por el camino (de 2 a 6 según el ancho): flotan, giran y brillan; al pasar se abren y vuelven a los 3 s.
El HUD de los karts hace una ruleta y enseña lo que ha tocado. Con artillera, los usa ella; si no, la conductora.

| Objeto | Qué hace | Delante / detrás |
|---|---|---|
| Coco turbo | Acelerón de 1,3 s. | Mucho delante |
| Triple coco turbo | Tres acelerones, uno por pulsación. | Solo detrás |
| Concha | Sale recta (hacia atrás con Q/B o Q/LB), rebota en las paredes y hace trompear al primero que toca. | Delante |
| Concha teledirigida | Persigue al kart de justo delante. | Sobre todo detrás |
| Alga resbaladiza | Charco detrás: poco agarre y menos velocidad. | Sobre todo delante |
| Tinta de calamar | Tinta en la pantalla de todos los que van por delante. | Nunca a la primera |
| Estrella de mar | 7 s invulnerable y algo más rápida; aparta a los karts que toca. | Nunca a la primera |

Reparto: `TNKart::ItemWeightsForPlace` (cuanto más atrás, más objetos buenos). Los bots los usan solos
(`TNKart::ShouldBotUseItem`).

## Artillera (#295)

- Torreta y objetos. Su **peso** (A/D o el stick izquierdo) cambia cuánto gira el kart: hacia dentro de la curva cierra el
  giro (hasta 45 grados de rueda, sobre los 38 del buggy); hacia fuera lo abre hasta un 35 %. El HUD le enseña hacia
  dónde carga.
- **Conductora sola**: mira alrededor con el ratón o el stick derecho (vuelve al centro al soltar) y la torreta sigue a la
  cámara; dispara (clic izquierdo, RB) hacia donde mira, con un poco de ayuda al apuntar.

## Géiseres, cascadas y agua (#293)

- Los desniveles grandes del mapa se suben en **géiser**: al pasar por su boca, el kart sale despedido hasta la cima (la
  misma parábola que las tortugas) y se mantiene derecho en el aire.
- Se bajan por la **cascada**: empujón ladera abajo, el morro hacia donde baja el agua y sin vueltas de campana; abajo, la poza.
- **Agua** (canales abiertos del camino, el mar y las pozas): el kart pliega las ruedas hacia abajo y flota como una balsa;
  acelerador y dirección lo mueven y en la orilla sube la rampa sobre sus ruedas. Salpicaduras al entrar y estela.

## Conducción (#742)

El kart de Karts conduce distinto del buggy del Rally (que sigue igual en `LVL_Rally`: `ATN_RallyKartBuggy` no lleva este ajuste).
Medido con `Tortunabo.Kart.Measure.*` (llano, sin turbo; Rally → Karts):

| | Rally | Karts |
|---|---|---|
| Punta | 111 km/h | 145 km/h (+30 %) |
| 0-100 km/h | 3,5 s | 2,65 s (+33 % de aceleración media) |
| 0-60 km/h | 1,9 s | 1,6 s (lo limita el agarre de salida) |

- **Más punta y aceleración**: el régimen del motor y el par suben un 30 % y el par de la parte alta de la curva (el que fija la
  punta) un 70 % (`TNKart::ApplyKartTuning`). El turbo, el acelerón del objeto y la estrella siguen a la punta nueva. El
  antivuelco deja de corregir el alabeo a una velocidad un 30 % mayor.
- **Dirección progresiva**: el buggy tiene tanto agarre que a 90 km/h un 10 % del volante ya daba 1,3 g. El ángulo de las ruedas
  de la conductora humana baja con la velocidad (`TNKart::SpeedSteerMultiplier`: 1 parado, la mitad a 58 km/h, 0,13 a 125 km/h) y
  ahora un 10 % del volante da 0,3-0,4 g y el volante a tope 1,5-2 g a cualquier velocidad. La IA no lo lleva: acota su giro por
  aceleración lateral.
- **Derrape con el freno de mano**: antes acababa en trompo (deriva media de 100° con el volante a tope). Ahora el freno de mano
  conserva un cuarto de su par, las traseras agarran algo más (1,4 → 2,2) y, con él puesto, se devuelve el morro hacia la
  velocidad pasados 22° de deriva: el derrape se sostiene a unos 20-35° con el volante a tope. Un derrape de más de 0,7 s (con el
  freno puesto, en el suelo, a más de 22 km/h y girando o deslizando) da **mini-turbo al soltar el freno de mano**: 0,6 s a los
  0,7 s de derrape, 1 s a los 1,4 s y 1,5 s a los 2,2 s (`TNKart::AdvanceDrift`, `ATN_Buggy::GrantTimedBoost`). Llama, sonido y
  cámara son los del turbo, y la barra del turbo no se gasta.
- **Para comparar**: `TN.Kart.Tuning 0` (antes de la partida) deja el kart como el buggy del Rally. `TN.Kart.SpeedScale` (1,3) y
  `TN.Kart.TopEndTorque` (1,7) mueven la punta sin recompilar. Los bots no cambian de velocidad (74/84/94 km/h por dificultad).

## Controles propios de los karts

| Acción | Conductora | Artillera |
|---|---|---|
| Usar objeto | E · clic derecho · LB | E · clic derecho · LT |
| Objeto hacia atrás (mantener) | Q · B | Q · LB |
| Mirar alrededor | Ratón · stick derecho | (su cámara de siempre) |
| Disparar (sola) | Clic izquierdo · RB | (su disparo de siempre) |
| Peso | — | A/D · stick izquierdo |

El resto, los del Rally (`Docs/Rally_MVP.md`).

### Con gafas (#529)

La vista va en los ojos de la tortuga sentada (sin la cámara de persecución) y las manos hacen de manos: la conductora
coge el volante y la artillera, las asas de la torreta. Los demás ven sus brazos en el volante o en las asas. Mapeo
completo y cómo funciona en `Docs/Modo_VR.md` («Vehículos»).

| Acción | Conductora | Artillera |
|---|---|---|
| Girar | Agarres en el volante (una o dos manos) · stick izquierdo sin cogerlo | — |
| Acelerar / frenar y marcha atrás | Gatillo derecho / gatillo izquierdo | — |
| Apuntar | La cabeza, si va sola | Agarres en las asas: hacia donde apuntan las manos · stick derecho sin ellas |
| Disparar | Stick derecho hacia delante (sola) | Gatillo derecho |
| Usar objeto | B | Gatillo izquierdo |
| Objeto hacia atrás (mantener) | Cualquier stick hacia atrás | B · stick izquierdo hacia atrás |
| Peso | — | La cabeza hacia un lado (y el stick izquierdo) |
| Turbo · freno de mano · enderezar (mantener: reaparecer) | A · X · Y | Y (enderezar) |

## Código

| Pieza | Qué es |
|---|---|
| `ATN_KartGameMode` | Hereda la carrera del Rally: mapa, espera a las máquinas, bots, cajas, vuelta al lobby. |
| `ATN_KartGameState` | Hace la pista con el generador (`PrepareTrack`, el único gancho nuevo en el Rally). |
| `ATN_KartTrack`, `TNKart::PlanRouteFromPath` | Pista desde el camino: puertas, línea del piloto IA, alas y cajas. |
| `ATN_KartBuggy`, `ATN_KartGunnerPawn` | El buggy y la artillera de SkiTemplar con objetos, peso y cámara de la conductora sola. |
| `TNKart::ApplyKartTuning`, `AdvanceDrift` | Conducción de los karts (#742): ajuste del buggy, dirección según la velocidad y mini-turbo del derrape. |
| `UTN_KartItemComponent`, `ATN_KartItemBox`, `ATN_KartShell` | Objetos, cajas y conchas. |
| `UTN_KartTraversalComponent` | Géiseres, cascadas y agua. |
| `UTN_KartHUDWidget` | Objeto, ruleta, kilómetros que quedan y peso de la artillera. |
| `TNProcMap::FGenParams::bDrivable` | El camino del cooperativo hecho para el kart (sin ramas, huecos ni cosas de las tortugas a pie). |

Pruebas: `Tortunabo.Kart.*` (ruta, objetos, artillera, géiser y flotación, `Tuning` y, con física y sin ventana, `Measure`), `Tortunabo.ProcMap.Drivable` y, con gafas,
`Tortunabo.VR.Vehicle.*`. Comandos de prueba en `Docs/Comandos_Prueba.md` («Karts en el mapa del cooperativo» y «Modo VR»).
