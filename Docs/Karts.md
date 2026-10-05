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
| Alga resbaladiza | Charco detrás, apoyado en el suelo: poco agarre, menos velocidad y un derrape al entrar; quien lo suelta no lo pisa los primeros 2 s. | Sobre todo delante |
| Tinta de calamar | Tinta en la pantalla de todos los que van por delante. | Nunca a la primera |
| Estrella de mar | 7 s invulnerable y algo más rápida; aparta a los karts que toca. | Nunca a la primera |
| Mortero (#774) | Parábola por encima de los karts que cae delante del kart de delante (donde estará al caer); la explosión del mortero levanta a los que pilla. Sin nadie delante, 40 m por delante. | Sobre todo detrás |
| Ráfaga de erizos (#774) | 3 s disparando púas hacia delante (24, la cadencia de la torreta); se apunta con el propio kart. Cada púa: empujón lateral y bamboleo. | Sobre todo delante |
| Medusa saltarina (#774) | Bote propio de unos 3 m al pulsar (no en el aire); en el aire las conchas pasan por debajo y los charcos no le tocan. | Sobre todo detrás |
| Pez globo (#774) | Mina detrás, 15 s: se arma a los 0,5 s, quien la suelta es inmune 1,5 s y explota como el mortero con un kart a menos de 4 m. | Sobre todo delante |
| Arpón (#774) | Se clava en el kart de delante (a menos de 80 m; si no, sale recto) y remolca hacia él 2 s, hasta el 115 % de la punta. El escudo lo anula. | Solo detrás (nunca a la primera) |

Reparto: `TNKart::ItemWeightsForPlace` (cuanto más atrás, más objetos buenos). Los cinco de #774 reutilizan la munición de la torreta del Rally (`ATN_RallyProjectile::Launch`, `ATN_RallyPufferMine`, `ATN_RallyHarpoonTether`). Los bots los usan solos
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
| `UTN_KartItemComponent`, `ATN_KartItemBox`, `ATN_KartShell` | Objetos, cajas y conchas. |
| `UTN_KartTraversalComponent` | Géiseres, cascadas y agua. |
| `UTN_KartHUDWidget` | Objeto, ruleta, kilómetros que quedan y peso de la artillera. |
| `TNProcMap::FGenParams::bDrivable` | El camino del cooperativo hecho para el kart (sin ramas, huecos ni cosas de las tortugas a pie). |

Pruebas: `Tortunabo.Kart.*` (ruta, objetos, artillera, géiser y flotación), `Tortunabo.ProcMap.Drivable` y, con gafas,
`Tortunabo.VR.Vehicle.*`. Comandos de prueba en `Docs/Comandos_Prueba.md` («Karts en el mapa del cooperativo» y «Modo VR»).
