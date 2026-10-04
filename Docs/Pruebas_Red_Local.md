# Pruebas de red local e informe de bug

Herramientas para probar la red sin Steam y para reportar fallos: hasta 8 instancias del juego en un PC con emulación de red, el monkey en los clientes remotos para medir correcciones y el informe de bug con F8. Solo compilaciones que no son Shipping. Issues #65, #80 y #67 (lote #386). El monkey y el estrés están en [`Estres-Monkey-2026-09-29.md`](Estres-Monkey-2026-09-29.md).

## 8 instancias locales: `Scripts/tools/red_local.py`

Lanza un servidor listen y hasta 7 clientes (`-game`) del editor DebugGame. Todas van por `IpNetDriver` con el subsistema NULL (`-NetDriverOverrides=/Script/OnlineSubsystemUtils.IpNetDriver -ini:Engine:[OnlineSubsystem]:DefaultPlatformService=Null -NoSteam`). `[/Script/OnlineSubsystemUtils.IpNetDriver]` en `Config/DefaultEngine.ini` copia los valores de `SteamSocketsNetDriver` (60 Hz, mismo `MaxClientRate`, mismos tiempos de conexión), así que se mide lo mismo que con Steam.

```bash
# 1 servidor con ventana + 7 clientes sin render, 150 ms ± 30 y 2 % de pérdida en los clientes
uv run python Scripts/tools/red_local.py --clientes 7 --lag 150 --varianza 30 --perdida 2

# Medición sin ventanas: estrés heavy en el servidor, monkey en los clientes, informe F8 del cliente 1 a los 40 s,
# todo se cierra al acabar y se resume (sale con 1 si un cliente no conecta o un monkey falla)
uv run python Scripts/tools/red_local.py --clientes 7 --render ninguno --lag 150 --varianza 30 --perdida 2 \
  --monkey 60:9 --estres heavy --estres-segundos 120 --informe-tras 40 --salir-al-acabar --esperar 420

# Solo enseñar las órdenes
uv run python Scripts/tools/red_local.py --clientes 7 --mostrar
```

| Opción | Qué hace |
| --- | --- |
| `--clientes N` | Clientes además del servidor (0-7; por defecto 7). |
| `--mapa` | Mapa del servidor (por defecto `/Game/Maps/Run/LVL_BeachRace`). |
| `--puerto` | Puerto del servidor. Por defecto, el primer UDP libre desde 7777. Los clientes van al puerto en el que el servidor dice que escucha en su registro: si otro worktree tiene una partida en 7777, el motor salta al siguiente y un cliente que fuese al pedido entraría en la partida ajena. |
| `--render ninguno\|servidor\|todos` | Quién dibuja; el resto va con `-nullrhi -nosound`. Por defecto, solo el servidor (640×360). |
| `--lag`, `--varianza`, `--perdida` | `-PktLag`, `-PktLagVariance` (ms) y `-PktLoss` (%) en cada cliente: emulan su salida, así que el ping sube en `--lag`. |
| `--emular-servidor` | La misma emulación también en la salida del servidor. |
| `--monkey S:SEMILLA` | Monkey en los clientes (`-TNMonkey`, `-TNMonkeyNet=client`): arranca al entrar en la partida, no en el menú. Cada cliente usa SEMILLA + su número. |
| `--monkey-clientes N`, `--monkey-servidor`, `--monkey-espera S` | Cuántos clientes lo llevan, si el servidor también y la espera al entrar (10 s). |
| `--estres ESCENARIO`, `--estres-segundos` | `TN.Stress` en el servidor (`light`, `heavy`, `race8`, `control`). |
| `--informe-tras S` | El cliente 1 crea un informe de bug a los S segundos (`-TNBugReportAfter`). |
| `--salir-al-acabar` | `-TNQuitWhenDone` en las instancias con monkey o estrés. |
| `--esperar S` | Espera hasta S segundos, cierra lo que quede y escribe `resumen.json`: quién se ha conectado, informes del monkey (jugadores, `net_corrections`) y KB/s por conexión del estrés. |
| `--carpeta`, `--motor`, `--extra-servidor`, `--extra-cliente`, `--escalonar` | Carpeta de registros (por defecto `Saved/RedLocal/<fecha>/`, un `.log` por instancia), raíz del motor (o `UE_ROOT`), argumentos extra y segundos entre clientes. |

Notas:

- Hay que compilar `TortunaboEditor Win64 DebugGame` y cerrar el editor antes. Cada instancia `-nullrhi` ocupa ~1,5-2 GB de RAM.
- La emulación también se cambia en caliente con la consola: `NetEmulation.PktLag 150`, `NetEmulation.PktLoss 2`.
- Para probar con Steam de verdad hacen falta varios PC (una cuenta por sesión de Windows): ver `Investigacion-8-Jugadores-Voz-2026-09-29.md` §5.

## Monkey en un cliente remoto

Un cliente lanzado con la dirección del servidor (`127.0.0.1:7777 -game -TNMonkey=60:9`) que no llega a conectar (por ejemplo, porque el servidor aún no escucha: a los 20 s da `ConnectionTimeout`) vuelve al mapa por defecto (`LVL_Menu`). Antes, el monkey arrancaba en ese mundo, sin jugadores, y el informe salía vacío. Ahora:

- `-TNMonkeyNet=client|server|any` elige en qué mundo arranca: `client` solo en el mundo conectado (`NM_Client`), `server` solo en el que hace de servidor y `any` en cualquiera.
- Sin `-TNMonkeyNet`, si el primer argumento de la línea de órdenes es una dirección (IPv4 con o sin puerto, `localhost` o `steam.<id>`), espera al mundo de cliente; con un mapa, arranca donde siempre.
- A mano, dentro de la partida: `TN.Monkey 60 9` en la consola del cliente.

El informe (`-TNMonkeyOut` o `Saved/Monkey/<fecha>.json`) lleva `net_mode: Client`, `players` y `net_corrections` (las correcciones del servidor que registra `p.NetShowCorrections`, que el monkey enciende mientras dura). Lógica en `Testing/TN_MonkeyNetStart.h`, tests `Tortunabo.Monkey.NetStart`.

## Informe de bug con F8

F8 en la ventana del juego (también `TN.BugReport` en la consola, o `-TNBugReportAfter=<s>` en la línea de órdenes) crea `Saved/BugReports/<aaaa-mm-dd_hh-mm-ss>/` con:

| Fichero | Contenido |
| --- | --- |
| `informe.md` | Listo para pegar en una issue nueva con la plantilla «Fallo»: pasos, esperado y obtenido y criterios por rellenar; tabla de contexto (fecha, commit, compilación, mapa, modo, red, posición, semillas) y los últimos 15 errores y avisos del registro, con las IP, los SteamID y el usuario de las rutas tapados (`<ip>`, `<steamid>`, `<usuario>`). |
| `captura.png` | Captura del viewport de ese juego con la interfaz, en el momento (en PIE, la de su ventana). No hay en `-nullrhi` (el Markdown lo dice). |
| `log.txt` | Las últimas 2000 líneas del registro. |
| `partida.json` | Commit, mapa, modo, semillas, red (modo, conexiones, dirección del servidor, ping, `PktLag`/`PktLoss` activos), estado de la partida y propiedades del GameState declaradas en el juego. |
| `jugador.json` | Cada jugador local: controlador, pawn, posición y velocidad en metros, rotación, rol de red, modo de movimiento y las propiedades del juego del pawn y de su PlayerState. |

- El commit se lee de `.git` (también en worktrees: `abc1234ef (rama)`); en una build empaquetada sale «desconocido».
- Las semillas son las propiedades enteras con «Seed» en el nombre del GameState y del GameMode (este solo en el servidor), más la de la ronda de la carrera (`TN_BeachRaceGenerator.RoundSeed`, con su número de ronda) y la del monkey si está en marcha.
- F8 lo lee un `IInputProcessor` de Slate desde `UTN_BugReportSubsystem` (subsistema del GameInstance), antes que el PlayerController y el HUD. En PIE solo responde la ventana con el foco y la tecla no sigue: F8 ya no expulsa del pawn en PIE (el botón «Eject» de la barra sigue).
- No hay aviso en pantalla: la ruta sale en el registro (`[Informe] F8: informe de bug en …`).
- Solo fuera de Shipping (el subsistema no se crea en Shipping): Development, DebugGame y builds de prueba. El registro y los JSON llevan IP, SteamID y rutas locales; para playtests externos se reparte una build Development (decisión del 2026-10-03 en #67). Lógica pura en `Testing/TN_BugReport.h`, tests `Tortunabo.BugReport`.

## Medición de `MaxClientRate`

Medido el 2026-10-02 con el script: servidor y 7 clientes `-nullrhi` en un PC, `LVL_BeachRace`, 150 ± 30 ms y 2 % de pérdida en los clientes, `TN.Stress heavy` (120 s) en el servidor y monkey en los 7 clientes. Salida del servidor por conexión, en KB/s:

| Fase | Conexiones | Media | Máx. | Entrada media | Actores replicados |
| --- | --- | --- | --- | --- | --- |
| Base | 7 | 5,6 | 19,0 | 1,2 | 675 |
| + cangrejos | 7 | 11,4 | 14,2 | 3,1 | 782 |
| + gaviotas | 7 | 8,3 | 11,0 | 2,6 | 837 |
| + tanques | 7 | 11,4 | 12,9 | 2,6 | 887 |
| + cajas | 7 | 12,3 | 14,6 | 3,3 | 976 |
| + lanzables | 7 | 12,0 | 14,4 | 2,4 | 1 009 |

- Subida total del anfitrión: ~86 KB/s de media con todo creado (7 × 12,3) y ~102 KB/s en el peor muestreo. El máximo de 19 KB/s es la entrada de los clientes (carga inicial). Sin voz: en `-nullrhi` no hay micrófono.
- Correcciones de movimiento: 92-159 por cliente en unos 100 s de monkey (con 3 clientes y 60 s, 45-54). El servidor, que registra las de los 7, cuenta 1 387.

**Valor**: `MaxClientRate = MaxInternetClientRate = 200000` B/s por conexión, en `SteamSocketsNetDriver` y ahora también en `IpNetDriver` (en local se usaba el valor del motor: 100 000 y 30 Hz). El juego usa como mucho el 10 %. El tope de 100 000 que propone [`Investigacion-8-Jugadores-Voz-2026-09-29.md`](Investigacion-8-Jugadores-Voz-2026-09-29.md) §3 no cabe con la voz actual: un cliente que oye 7 voces µ-law recibe 119 KB/s más 12-19 de juego (~138 KB/s), por encima de 100 000 B/s (97,7 KB/s), y la conexión saturada retrasaría el movimiento. Se baja a 100 000 cuando entre la voz Opus (E10-09: 7 voces, 25 KB/s) y se vuelve a medir con `TN.Voice.FakeTalk`.
