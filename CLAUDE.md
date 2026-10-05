# Tortunabo — guía para Claude

Juego cooperativo de 1 a 4 tortugas en Unreal Engine 5.6 con C++ (módulo `Source/Tortunabo`), listen server con Steam. Tres desarrolladores trabajan a la vez, cada uno con su Claude. Esta guía y las skills de `.claude/skills/` son iguales para los tres, y la memoria del equipo está en las issues: así las tres sesiones trabajan igual y saben lo mismo.

**El tablero lo mantiene Claude: cada vez que hagas algo, actualiza su estado con `tablero.py`, sin esperar a que te lo pidan** (coger, probar, entregar, revisar, fusionar, cerrar con **Resumen**, registrar fallos). El ciclo completo está en «Tablero».

El tablero es solo de desarrollo: código, pulido, bugs y revisión de assets. El diseño ya está decidido (plan maestro y decisiones); se pueden hacer prototipos, pero no tareas de «diseñar X».

## Equipo

| GitHub | Persona | Rol | Revisa su trabajo |
|---|---|---|---|
| SkiTemplar | Rodrigo | Director y aprobador | Mokius |
| Mokius | Mokius | Código y aprobador | SkiTemplar |
| Ruben-Besteiro | Rubi | Código | Mokius o SkiTemplar |

Las decisiones que no estén escritas las toman SkiTemplar o Mokius. Si te falta una, etiqueta la issue con `decision`, di qué hay que decidir en un comentario y sigue con otra tarea. `pendiente` enseña las decisiones abiertas y `coger` no coge una issue con `decision` sin consultarlo antes (`--forzar` para seguir sin esperar).

## Ramas

- `dev`: la rama de desarrollo hasta el final del juego. Todas las ramas salen de `origin/dev` y todas las PR van hacia `dev`. **Nunca se trabaja sobre `dev`**: toda issue o lote va en su rama y entra por PR, que puede fusionar cualquiera de los tres. Un ruleset de GitHub rechaza el push directo y el forzado.
- `main`: versión estable. Solo SkiTemplar y Mokius, por PR con la aprobación del otro (o su bypass). Aparte de eso, solo le llegan las copias de `.github/` que necesitan los cron.
- **Ramas por lote, no por tarjeta** (decisión en #282): las tarjetas de un lote se hacen y se prueban en la misma rama, `feat|fix/<primera issue>-<slug>`. La crea `tablero.py coger` con la primera tarjeta; las demás entran en ella con `coger <n> --rama <rama del lote>`. Una issue suelta es un lote de uno: su propia rama.
- **A `dev` solo entra lo que está en Done**: cada tarjeta se revisa (IA) y se prueba en el editor **en la rama de su lote**; con las dos validaciones pasa a Validada; con todo el lote en Validada, a Done; y solo entonces se pide y se fusiona la PR. Nada se fusiona para probarlo después en `dev`.
- Antes de abrir o actualizar una PR: `git fetch origin && git rebase origin/dev`. Ramas cortas: si una rama vive más de 2-3 días, rebase diario.

## Tablero: la única lista de tareas

GitHub Project «Tortunabo · Desarrollo» en vista Kanban: https://github.com/orgs/Unreal-portfolio/projects/2. Toda tarea es una issue del repo y vive en el tablero. **Lo mantiene Claude, sin esperar a que se lo pidan: cada paso de abajo se registra con `tablero.py` en el mismo turno.** No muevas tarjetas a mano.

| Estado | Significa |
|---|---|
| Backlog | Aún no aprobada para hacerse |
| Bloqueada | Espera a que se cierren las issues de las que depende (dependencias nativas de GitHub) |
| Ready | Aprobada: se puede trabajar |
| In progress | Alguien (o su Claude) la está haciendo o la dejó a medias; el asignado es quien está con ella |
| In review | Terminada: la revisa la IA de otro miembro del equipo (campo Revisor) |
| Revisiones | La revisión o la prueba encontraron un fallo, comentado en la propia issue |
| QA editor | Revisión IA aprobada: falta probarla en el editor (en su rama; lo fusionado antes de #282, en dev) |
| Validada | Aprobada y probada en el editor en su rama, sin fusionar: espera al resto de su lote (o a la fusión) |
| Done | Fusionada en dev, aprobada y probada en el editor; cerrada |

Dos validaciones por issue: **Revisión IA** (`Pendiente` / `Aprobada` / `Cambios pedidos`), que hace el Claude del revisor cruzado y nunca quien escribió el código, y **Editor** (`Sin probar` / `Funciona` / `Falla`), prueba real en el editor de Unreal.

Ciclo paso a paso:

1. **Coger** (`coger <n>`): In progress, asignada y rama desde `origin/dev`. `coger` rechaza las issues con bloqueantes abiertas y exige el árbol de trabajo sin cambios en ficheros versionados. Asignado significa «estoy con ella ahora»: si la dejas sin terminar, `soltar <n> --motivo "..."` la devuelve a Ready sin asignado.
2. **Probar mientras se trabaja** (`editor <n> funciona|falla`), en la rama del lote: si funciona, Editor = Funciona; si falla, se queda en In progress con el fallo comentado: no se manda algo que no funciona.
3. **Entregar** (`revision <n>`): In review con revisor cruzado. Si el autor no la ha probado, va con Editor = Sin probar y el comentario «Sin QA editor»: la prueba se hace igualmente en la rama, antes de fusionar. Con Editor = Falla, `revision` la rechaza.
4. **Revisar** (`ia <n> aprobada|cambios --revisor "<login> (Claude)"`): el campo Revisor pasa a ser quien ha revisado de verdad. Aprobada y sin probar pasa a QA editor; si ya se probó antes de la aprobación, se salta ese paso y va directa a Validada. Con cambios pasa a Revisiones con el fallo comentado. Lo normal es que el propio revisor lo arregle: la coge con `coger <n> --forzar` (In progress a su nombre) y la vuelve a entregar; la nueva revisión la hace otro.
5. **Validar y fusionar** (cualquiera de los tres): en QA editor, `editor <n> funciona` probado en la rama → Validada. Con todas las issues de la PR en Validada (`lote estado <lote>` en verde), pásalas a Done (`estado <n> Done`), fusiona la PR en `dev` (`gh pr merge --merge --delete-branch`) y `sync --aplicar`. Nunca se fusiona una PR con alguna issue sin probar; lo que se salte el ciclo lo detecta «Avisos del tablero».
6. **Cerrar** (`resumen <n>`): cada issue que llega a Done lleva su comentario **Resumen**.

Una prueba que falla en In review, QA editor o con la issue cerrada la lleva a Revisiones (la reabre si hace falta, con `regresion` si ya funcionaba). Regla única que aplican `ia`, `editor` y `sync`: Done solo con la PR en dev, Revisión IA = Aprobada y Editor = Funciona.

**Una rama y una PR por lote.** Las tarjetas que van juntas (un sistema, una tanda de bugs del mismo objeto) se agrupan en un lote desde el principio: una rama, una PR con `Closes` de todas, y `lote crear --titulo "…" <n> <n> …`, que crea la issue temporal `lote` («Lote: …»), depende de cada miembro y la enlaza a la PR con «Refs» (una issue citada con «Refs» no avanza ni cuenta como fusionada: solo `Closes` mueve las issues). Una issue que se suma después entra con `lote añadir <lote> <n>` (y su `Closes` en la PR), no con `bloquear`. Cada miembro se revisa y se prueba por separado en esa rama y, con las dos validaciones, espera en Validada. La PR del lote no se fusiona hasta que `lote estado <lote>` confirma que todos están en Validada y que ninguno tiene una `decision` pendiente; entonces pasan a Done, se fusiona y el lote se cierra con un **Resumen** del conjunto.

**Dependencias**: si una issue no puede empezar hasta que se cierren otras, `bloquear <n> --por <m>` la deja en Bloqueada; `sync --aplicar` la pasa a Ready cuando se cierran todas. En Backlog la dependencia se registra y la issue sigue en Backlog (bloquear no aprueba); al aprobarla con `estado <n> Ready`, si sus bloqueantes siguen abiertas va a Bloqueada.

**Colisiones y auditoría**: `colisiones --aplicar` crea una issue `colision` (P1, Revisiones) por cada par de PR abiertas que chocan al mezclarse (`git merge-tree` de sus cabezas, no solo ficheros en común), con las instrucciones para mezclarlas (si hay `.uasset`/`.umap`, `decision` y no se mezclan), y cierra con un **Resumen** las que ya no chocan o tienen alguna PR cerrada. Un par que choca solo en `Content/Localization/` no crea issue (y la que haya se cierra): la localización la regenera la PR que se fusione en segundo lugar; en un par mixto, la issue lista solo los ficheros de código. `sync` avisa igual de una PR con conflictos con `dev` solo en localización: «solo localización: regenerarla». `auditar --aplicar` revisa la organización sin tocar código: lo trivial (mover la tarjeta a su columna, rellenar un campo evidente) lo corrige y lo anota; lo que afecta al trabajo (PR de lote fusionada sin validar, estado incoherente con la PR, issue cerrada sin probar) lo pasa a Revisiones con P0 y lo explica; el resto (campos vacíos, forma de la issue, P0 en Backlog, asignado fuera de curso…) lo etiqueta `revisar-organizacion` con un comentario. Las issues `colision` y `revisar-organizacion` van antes que cualquier otra tarea. Sin `--aplicar`, ambos solo informan.

### Forma de una issue

Toda issue de trabajo cumple esto; `nueva` no crea una que no lo cumpla y `auditar` marca las que se crean a mano en GitHub:

- **Título** de 80 caracteres como mucho: el síntoma o la tarea, sin el detalle. Dentro de un objeto con varias tareas, «Objeto: tarea».
- **Cuerpo** con el contexto (en un fallo: pasos, esperado y obtenido, mapa y jugadores) y los **criterios de aceptación** como casillas `- [ ]` verificables en el editor o con tests.
- **Etiqueta de tipo**: `tarea` o `⚠️bug⚠️`.
- **Objeto** del que cuelga y los campos **Prioridad, Tamaño, Área y Fase** (F0–F8 del plan maestro; «Sin fase» si no es de ninguna).
- Un **P0** está en Ready o más allá, nunca en Backlog. Tamaño L solo si no se puede partir en tareas de 1-2 días.
- Un criterio que cambia mientras se trabaja se corrige en el cuerpo, no en un comentario suelto; si es una decisión de diseño, la registra un aprobador con `decidir`.

Las plantillas de `.github/ISSUE_TEMPLATE` piden lo mismo a quien crea la issue en la web (GitHub las lee de `main`, así que un cambio en ellas va también a `main`). Una issue creada en la web nace sin objeto ni campos: `auditar` la marca.

### Hablar en la issue

Para pedir, cambiar o preguntar algo sobre una issue se escribe en sus comentarios, como en un chat; no hace falta pasar por nadie. Quien trabaja una issue lee antes su conversación entera.

- Al comentar, el workflow «Conversación en las issues» (`.github/workflows/tablero-conversacion.yml`, también en `main`) mira quién habló el último: si no fue quien la tiene asignada, le pone la etiqueta `peticion` y `pendiente` se la enseña antes que su trabajo. Cuando el asignado contesta, la etiqueta se quita sola. `auditar` lo repasa cada mañana y `conversacion <n> --aplicar` lo hace a mano.
- Al atender lo que se pide: haz el cambio, corrige los criterios del cuerpo y contesta en la issue (o `atendida <n> --nota "..."`, que quita la etiqueta al momento).
- `pedir <n> --texto "..."` es lo mismo sin esperar a la mañana y, si la issue ya estaba entregada (In review, QA editor o Validada), la devuelve a Revisiones. La PR de un lote no se fusiona con un miembro con `peticion`.
- Un cambio de diseño lo registra un aprobador con `decidir`; una decisión que corrige otra se escribe como decisión nueva, no se edita la anterior.

Los comentarios automáticos del tablero son de una línea: dicen qué ha pasado, no cómo funciona el sistema.

### Objetos y sub-issues

Las tareas y los fallos se agrupan por **objeto**: un sistema o una pieza del juego (el Rally, el puente tambaleante, el HUD, las catapultas…). Un objeto es una issue padre con la etiqueta `objeto`, sin Status, que se ve en la vista «Objetos»; sus tareas y fallos cuelgan de él como sub-issues. Los sistemas grandes no se desglosan en épicas por fase: la fase va en el campo Fase. Las PR enlazan la sub-issue concreta, nunca el objeto.

Cuando el usuario dice «esto no funciona», Claude decide y registra sin preguntar (solo pregunta si duda de verdad a qué objeto pertenece), después de mirar los resúmenes del objeto (`resumenes <n>`):

- **El mismo fallo vuelve** (aunque su issue esté cerrada): `editor <n> falla` reactiva esa issue. No se abre otra.
- **Otro fallo del mismo objeto**: sub-issue nueva (`nueva --tipo bug --objeto "<objeto>"` con Prioridad, Tamaño y Área) que cita las issues parecidas.
- **Algo que aún no tiene objeto**: `nueva --objeto "<nombre>"` crea el objeto y cuelga de él la sub-issue. Si ya hay un objeto con nombre parecido, no lo crea y lo enseña: usa su título exacto o, si de verdad es otro sistema, `objeto "<nombre>" --nuevo`. Un objeto duplicado parte en dos la memoria de un sistema.

### Comandos

```bash
uv run python Scripts/tablero/tablero.py pendiente             # qué hay para mí (colisiones y organización primero)
uv run python Scripts/tablero/tablero.py coger <n> [--forzar]  # asignarme, In progress y rama
uv run python Scripts/tablero/tablero.py soltar <n> --motivo "..." # la dejo: sin asignado y de vuelta a Ready
uv run python Scripts/tablero/tablero.py editor <n> funciona|falla --como "PIE 4P" --nota "..."
uv run python Scripts/tablero/tablero.py revision <n>          # terminada: In review con revisor cruzado
uv run python Scripts/tablero/tablero.py ia <n> aprobada|cambios --revisor "<quién> (Claude)" --nota "..."
uv run python Scripts/tablero/tablero.py resumen <n> --que "<qué fallaba>" [--por-que "<causa>"] --como "<arreglo>" [--pr <n>]
uv run python Scripts/tablero/tablero.py resumenes <n>         # resúmenes de las demás sub-issues de su objeto
uv run python Scripts/tablero/tablero.py decidir <n> --texto "<decisión>"
uv run python Scripts/tablero/tablero.py pedir <n> --texto "<qué pido>" | atendida <n> --nota "<qué he hecho>"
uv run python Scripts/tablero/tablero.py lote crear --titulo "..." <n> <n> ... | lote estado <lote>
uv run python Scripts/tablero/tablero.py lote añadir <lote> <n> [<n> ...]   # meter miembros en un lote ya creado
uv run python Scripts/tablero/tablero.py bloquear <n> --por <m> [--por <k>]   # en Backlog se queda en Backlog
uv run python Scripts/tablero/tablero.py nueva --titulo "..." --tipo bug|tarea --cuerpo f.md --objeto "<objeto>" --prioridad P1 --tamano S --area Red [--fase F4 --estado Ready]
uv run python Scripts/tablero/tablero.py objeto "<nombre>" [--area X --descripcion "..." --nuevo] | colgar <hijo> <objeto>
uv run python Scripts/tablero/tablero.py estado <n> <estado> | campo <n> <campo> <valor>
uv run python Scripts/tablero/tablero.py sync|auditar|colisiones [--aplicar]
uv run python Scripts/tablero/tablero.py volcado [--publicar <issue>]   # tablero completo en Markdown
uv run python Scripts/tablero/tablero.py avisos [--aplicar] [--publicar 196 --parte 127]   # lo que entró en dev sin revisión y el parte, al director
```

Requiere `gh` autenticado con el scope de proyectos: `gh auth refresh -s project`. **Cada persona ejecuta `tablero.py` en local con su `gh`**: así sus comentarios y validaciones salen con su cuenta. El puente es para la rutina y para quien no tiene el repo a mano. Los comandos de una issue (`coger`, `soltar`, `estado`, `campo`, `revision`, `ia`, `editor`, `pedir`, `bloquear`) la leen con una consulta dirigida de 1 punto (los ids de los campos se guardan 24 h en `Scripts/tablero/.cache`, ignorado por git); `sync`, `auditar`, `colisiones`, `volcado` y `pendiente` leen el Project entero, unos 55 de los 5000 puntos por hora de la API de GraphQL de esa cuenta: no los lances en bucle. Si `coger` no puede cambiar de rama por cambios sin guardar, resuélvelo con la persona (commit o `git stash`); nunca los descartes.

### Organización diaria: el puente y la rutina

Tres automatismos mantienen el tablero cada mañana. Ninguno toca ni revisa código.

- **Puente del tablero** (`.github/workflows/tablero-puente.yml`, 7:15): ejecuta `sync`, `auditar` y `colisiones` con `--aplicar` y publica el volcado del tablero en la issue #131. Lanzado a mano con un comando (`estado 123 Ready`), lo ejecuta si está en la lista cerrada de `Scripts/tablero/volcado.py`; los comentarios que deja acaban en «Lanzado por <quién> a través del puente», porque todos salen con la cuenta del dueño del token. El volcado lleva una sección «Sin movimiento desde hace más de 3 días» con lo que espera a alguien (In progress, In review, Revisiones, QA editor y Validada). Está también en `main`, porque el cron solo se ejecuta desde la rama por defecto.
- **Rutina de Claude** (7:30): hace de responsable de calidad de las issues. Lee el volcado, corrige por el puente lo que es evidente, avisa de lo que no lo es y deja el parte del día en la issue #127. Su entorno solo llega a las rutas REST del repositorio: por eso lee y mueve el tablero a través del puente.
- **Avisos del tablero** (`.github/workflows/tablero-avisos.yml`, 8:30): el correo del director con el resultado del día. Ejecuta `avisos --aplicar`: cada push directo a `dev` con commits de código que no son de ninguna PR fusionada abre una issue `sin-revision` (In review, P0, revisor cruzado), sea quien sea el autor, y la issue se cierra sola cuando tiene las dos validaciones; cada PR fusionada en `dev` con una issue sin revisión aprobada (en un lote, también sin probar), o sin issue y con código, sale como incidencia. A `dev` solo se llega por PR y fusionarla puede cualquiera de los tres: lo que se controla es que el trabajo que lleva revisión y prueba las tenga (un push directo que se cuele, p. ej. uno anterior al ruleset, también se detecta). Lo que solo toca rutas de organización (`avisos.rutas_organizacion` en `Scripts/tablero/equipo.json`: tablero, skills, workflows, guía y documentación) va sin ellas a propósito. Después comenta en la issue #196 el aviso de cada destinatario (`avisos.destinatarios`, hoy SkiTemplar) con una mención: incidencias, PR nuevas, comentarios que le mencionan, lo que espera por él y el parte de la rutina; GitHub se lo manda por correo. Lo escribe github-actions, porque GitHub no avisa a nadie de lo que hace su propia cuenta. También está en `main`.

Un aviso nunca vive solo en el parte. Siempre queda como estado en el tablero, y `pendiente` lo enseña arriba hasta que se resuelve: etiqueta `revisar-organizacion` con su comentario (la pone `auditar` y la quita sola cuando deja de ver el problema), etiqueta `revisar-qa` con su comentario (la pone y la quita la rutina, para lo que un script no ve), etiqueta `peticion` (conversación sin contestar), Revisiones con P0, o etiqueta `decision` con la pregunta. El parte solo resume. Al abrir sesión, lee el último (`gh api repos/Unreal-portfolio/Tortunavy/issues/127/comments --jq '.[-1].body'`) y ejecuta los comandos que haya dejado pendientes.

Las issues #127, #131 y #196 no van al tablero. El puente actúa con el token de SkiTemplar, y GitHub suscribe a quien comenta: por eso termina con `tablero.py silenciar`, que lo da de baja de las issues abiertas (le siguen llegando las PR, las menciones y lo asignado). Si quien lanza el puente a mano tiene su propio secreto (`TABLERO_TOKEN_MOKIUS`, `TABLERO_TOKEN_RUBI`), el puente usa el suyo y sus comentarios salen con su cuenta. El token del secreto `TABLERO_TOKEN` es classic con `repo`, `project`, `read:org` y `notifications`; sin el último, `silenciar` avisa y no silencia, pero el puente no falla.

## Memoria del equipo: las issues

La memoria del equipo son las issues: su cuerpo y sus comentarios **Resumen** («Qué fallaba / Por qué / Cómo se arregló», con `resumen`) y **Decisión** («**Decisión** (fecha, quién): …», con `decidir`, en la issue u objeto afectado). Siempre resumidos: el comando rechaza más de 400 caracteres por campo. No hay otro registro. No hace falta leer todas las issues, sí las relacionadas: las del mismo objeto (`resumenes <n>`) y las abiertas en Revisiones o con `colision` o `revisar-organizacion`. Las decisiones de diseño de fondo están en el plan maestro (§1 y §7).

## Skills del proyecto

- `tortu-que-hacer`: «¿qué hago?», «¿qué hay pendiente?». Lee el tablero y propone (colisiones y organización primero).
- `tortu-coger`: empezar, retomar o arreglar una issue (también las `colision`).
- `tortu-entregar`: PR hacia dev, lote si hay varias issues y paso a revisión cruzada.
- `tortu-revisar`: revisión IA cruzada y fusión en `dev`; los aprobadores, además, deciden, auditan y desglosan objetos.
- `tortu-editor`: registrar lo que se prueba en el editor («esto no funciona», «esto ya va»).

## Reglas

- `Content/`, `.uasset` y `.umap` son binarios y no se pueden fusionar. El repo no usa Git LFS: antes de tocar un asset o un mapa, comprueba que ninguna issue en In progress lo nombra y escribe en tu issue qué assets vas a tocar.
- No toques `Deprecado/` ni `/Game/_Deprecado`.
- Todo asset nuevo de `Art/` se aplica en el juego en la misma PR o abre una issue que lo aplique (decisión del 01-10, #287). Esa issue se enlaza en `Scripts/tools/catalogo_pendientes.py` y sale en la nota de `Art/catalogo_assets.csv`. `uv run pytest` falla si el catálogo tiene un asset «sin usar» sin issue.
- Todo texto que se vea en el juego se puede traducir: `NSLOCTEXT`, recogido y traducido a los 13 idiomas antes de entregar (`Docs/Localizacion.md`). Nunca `FText::FromString` con un literal: lo que de verdad no se traduce va con `INVTEXT`. La pipeline rechaza la PR que lo incumple.
- Commits en español técnico con ortografía completa y conventional commits (`feat|fix|refactor|docs|test|chore|perf`), sin líneas `Co-Authored-By`.
- Todo texto nuevo o cambiado que se vea en pantalla es un `NSLOCTEXT`/`LOCTEXT` en español y **se traduce en la misma PR** a los 12 idiomas restantes: recoger (`Scripts\localization_gather_export.bat`), traducir las entradas nuevas de los `Game.po`, compilar (`Scripts\localization_import_compile.bat`) y commitear `Game.manifest`, `.archive`, `.po` y `.locres`. Claude lo hace sin que se lo pidan, con el glosario y las reglas de `Docs/Localizacion.md`. Un texto sin traducir sale en español.
- La PR enlaza su issue con `Closes #<n>` en el cuerpo.
- Nunca hagas push a `main` ni a `dev`, ni `--force` sobre ramas ajenas.

## Compilar y probar

- Editor: `Build.bat TortunaboEditor Win64 DebugGame "<ruta>\Tortunabo.uproject" -WaitMutex -NoHotReload`. Cierra el editor antes de compilar.
- Tests de C++: `Automation RunTests Tortunabo` (consola del editor o Session Frontend). Lista de comandos en `Docs/Comandos_Prueba.md`.
- Tests de Python (terreno y tablero): `uv run pytest` desde la raíz.
- Plan vigente: `Docs/2026-09-29-Plan-Maestro-Modos-y-Mapas.md`. Desglose de tareas: `Docs/ROADMAP-macro-update.md`. Historial: `Docs/Bitacora.md`.
