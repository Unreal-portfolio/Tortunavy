---
name: tortu-entregar
description: Use when work on a Tortunabo issue is finished and needs to go up ("ya está", "súbelo", "haz la PR", "listo para revisar").
---

# Entregar una issue

El tablero lo mantienes tú, sin esperar a que te lo pidan. Ciclo y estados: «Tablero» en `CLAUDE.md`.

1. Si consta Editor = Falla, no se entrega: arréglalo y pruébalo antes (`revision` la rechaza).
2. Verifica lo que se pueda aquí: compila `TortunaboEditor` DebugGame si has tocado C++ y ejecuta los tests afectados (`Automation RunTests Tortunabo...` o `uv run pytest`). Si no has podido compilar, dilo en la PR.
3. `git fetch origin && git rebase origin/dev`. Conflictos en C++ o texto: resuélvelos con el usuario. En `.uasset`/`.umap`: para y avisa, no se fusionan.
4. Push y PR hacia `dev` con la plantilla (`Closes #<n>`, qué cambia, cómo probarlo): **una PR por lote** (#282), con la rama del lote (número de la primera issue), una línea `Closes` por issue y su lote: `tablero.py lote crear --titulo "<qué agrupa>" <n> <n> ...` (lo enlaza a la PR con «Refs»). Si después se suma una issue al lote: `tablero.py lote añadir <lote> <n>` y su línea `Closes` en la PR (no `bloquear`: no deja el comentario en la issue ni la casilla en el lote). Una issue suelta, su propia PR. La PR se abre para revisar y probar en su rama: no se fusiona hasta que todas sus issues estén en Done.
5. `uv run python Scripts/tablero/tablero.py revision <n>` (una vez por issue): In review con el revisor cruzado de `equipo.json` (lo de Rubi, Mokius o SkiTemplar; lo de SkiTemplar, Mokius; lo de Mokius, SkiTemplar). Si no se probó en el editor, queda Editor = Sin probar con el aviso «Sin QA editor»: hay que probarla en la rama antes de fusionar (con la revisión aprobada pasa a QA editor; probada, Validada → Done → fusión).
6. No te revises a ti mismo: la revisión IA la hace el Claude del revisor con `tortu-revisar` (`/codex:review` vale como comprobación previa, no la sustituye). Di al usuario quién la revisa. Si cambiaste un criterio de la issue mientras la hacías, no lo dejes en un comentario suelto: actualiza el cuerpo y, si es una decisión de diseño, etiqueta `decision` para que un aprobador la registre.
