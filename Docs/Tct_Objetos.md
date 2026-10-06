# Todos contra Todos: objetos, puntos de objetos y agua venenosa

Objetos del modo Todos contra Todos (TcT). Cada objeto tiene una ventaja clara y un coste o riesgo; los puntos donde salen reparten
lo mejor en los sitios más altos o expuestos. Reglas puras en `TN_TctItemRules.h` y `TN_TctRules.h`; pruebas `Tortunabo.Tct.*`.

## Objetos nuevos (#830)

Todos se usan con el botón de usar objeto, con servidor autoritativo; los efectos con duración replican una hora de fin
(`UTN_TctItemComponent`, `FTNTctFxState`) y cada máquina pone los mismos límites de movimiento a la tortuga. Valores en
`TNTctItemTuning`.

| Objeto | Rareza | Ventaja | Coste o riesgo |
|---|---|---|---|
| Cohete de feria | Rara | Te lanza lejos y alto hacia donde miras: cruza huecos y sube de piso. | Te quema: 2 s casi sin poder andar, y si apuntas mal caes donde no querías. |
| Botas de muelle | Común | 12 s con el doble de altura de salto: suben de piso sin rampa y esquivan. | Vas a paso lento (tope de velocidad) mientras las llevas. |
| Aletas | Rara | 12 s con el veneno del agua al 30 %: puedes vadear y cruzar el agua. | En tierra firme vas torpe (tope de velocidad). |
| Cambiazo del pulpo | Épica | Cambia tu sitio con la tortuga más cercana (a menos de 12 m): te saca del agua o del borde. | Las dos quedáis mareadas 1,2 s y puede tocarte una tortuga más baja: sin rival cerca no se usa. |
| Burbuja | Épica | 10 s sin que nada te empuje, derribe ni maree. | Flotas lenta y sin apenas gravedad, y no te protege del agua. |
| Púas de erizo | Rara | 8 s lanzando a quien se te acerca (menos de 1,9 m). | Pesas más: poco salto y poca velocidad, así que no escapas. |
| Red de pesca | Común | Proyectil: clava 2,5 s (sin andar ni saltar) a quien toca; 2 cargas. | Poco alcance y vuelo lento que se ve venir; si falla, se pierde la carga. |
| Remolino de arena | Común | Torbellino plantado 6 s que lanza al aire a quien pasa: despeja un paso o un punto de objetos. | También lanza a quien lo plantó si se queda cerca. |
| Tapón de marea | Épico | Retrasa 10 s el agua (baja lo que haya subido y aplaza la siguiente subida). | Da tiempo a todas, también a quien va ganando. |
| Paraguas | Común | 7 s planeando (caída muy lenta): cruza huecos y alcanza pisos lejanos. | Casi sin velocidad ni salto mientras planeas. |

Los objetos anteriores (pistola de noqueo, trabuco, garfio, pala, balón, ancla, dardo, tinta, bola, concha, cabezota, mina, disco,
cocobomba, alga, gaviota, flotador, medusa) siguen igual y tienen su rareza en `FTNTctItemSpec::Rarity`.

## Puntos de objetos por rareza (#830)

- Cada punto de objetos (`ATN_TctItemPad`) tiene una rareza: épica, rara o común, según su altura sobre el agua y lo cerca que
  está del vacío (`TNTctItemRules::PadRarityFor`; la exposición la mide `ATN_TctArena::Survey`). El reparto
  (`TNTctItemRules::PlanPads`) pone un quinto de épicos en lo más alto o expuesto, un tercio de raros y el resto comunes, lejos de
  las salidas y separados, y entrelaza las rarezas para que con pocas jugadoras ya haya mezcla.
- Un punto común saca sobre todo objetos comunes; uno épico, lo épico (`PadItemWeight`). Según avanza la ronda (la parte del agua
  ya subida, `TNTctRules::RoundProgress`) sale más de lo raro y lo épico.
- Se ven de lejos: el disco y un haz de luz del color de su rareza (naranja, azul, violeta; 7, 16 y 32 m de alto) mientras hay un
  objeto puesto.

## Agua venenosa (#831)

- Tocar el agua no mata: con los pies bajo la superficie el veneno sube 0,2 por segundo (cinco segundos seguidos lo llenan) y fuera
  baja 0,07; al llegar al máximo, la tortuga queda eliminada. Caer fuera de la arena sigue eliminando. El flotador salta al llegar a
  la mitad del veneno; las aletas lo reducen al 30 %.
- El mar de la arena es verde tóxico y no hay zonas de muerte del fondo (`bSpawnKillZones` apagado en `ATN_TctArena`).
- En los 5 s antes de cada subida (y mientras sube) un plano verde translúcido marca la altura a la que llegará. El HUD enseña
  siempre la próxima subida con su cuenta atrás y el tramo («Tramo 2 de 5», el último es la marea final), una cinta con la cuenta
  atrás en los segundos de aviso y la barra de veneno propia.
- Los tiempos son los de `FTNTctFloodPlan` (#778): empieza a los 25 s, un escalón cada 24 s, 7 s por subida, marea final de 40 s.

## Decorado vivo de las arenas (#829)

Cada arena conserva su forma, alturas y estructuras (la variante de `Scripts/terrain_volumes/Variants`); encima se reparten, con el
sistema del mapa generado de ProcMap y Coop, la vegetación, las rocas, los troncos y la fauna (`ATN_TctScenery`, `TN_TctSceneryPlan.h`).

- **Reparto**: `TNTctScenery::MakePlan` mide el suelo (trazas cada metro contra la malla de la arena) y reparte con
  `TNProcMap::PlaceFloraRows` (las mismas especies por bioma y las mismas mallas que el generador), anclas para `ATN_ProcFauna::InitCustom`
  (animales que huyen de las tortugas y se esconden cuando les llega el agua) y decorado con colisión de `ATN_BeachDecorField`
  (rocas, troncos, castillos de arena, sacos...). Cada arena tiene su bioma (`PrimaryBiome`: diana y atolón, playa; coliseo, pueblo;
  volcán-arena, volcánico...) con manchas de otro bioma.
- **Sin tapar nada**: las salidas y los puntos de objetos (radio de 9 y 7 m) no se llenan; lo que tiene colisión solo va donde sobran 6 m
  de paso alrededor y el suelo es casi llano (rampas, puentes y bordes quedan libres); la vegetación no crece en rampas ni sobre
  salidas y puntos de objetos y se atraviesa, como en el mapa generado.
- **Igual en todas las máquinas** (#828): el servidor fija una vez la semilla de la partida y los sitios libres
  (`ATN_TctArena::ServerSetScenery`, replicado) y cada máquina reparte lo mismo con la misma función pura, sobre el mismo suelo, sin
  nada que dependa de la calidad gráfica, los fotogramas ni el orden de carga. La huella de lo que tiene colisión sale en el log de cada
  máquina (`[TcT] Decorado de «arena»... huella XXXXXXXX`) para compararla.
- **Rendimiento**: instancias jerárquicas con las distancias de corte del generador, sin colisión en la vegetación, 150 animales como
  mucho y nada de lo visual en un servidor dedicado.
