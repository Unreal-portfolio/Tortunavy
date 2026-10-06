---
name: tortu-editor
description: Use in Tortunabo whenever the user reports what they saw while testing in the Unreal editor or a build — something broken ("esto no funciona", "se cae", "falla el salto") or something confirmed working ("ya va", "funciona", "probado"). Also use at the end of a testing session to record implicit validations.
---

# Registrar pruebas en el editor

El tablero lo mantienes tú: en cuanto el usuario cuente lo que ha visto, regístralo en ese mismo turno. Ciclo y estados: «Tablero» en `CLAUDE.md`.

## Cuando algo falla

Lo decides y lo haces tú, sin preguntar; solo preguntas, en una línea, si dudas de verdad a qué objeto pertenece.

1. Identifica el objeto (sistema o pieza del juego: «Puente y plataforma tambaleantes», «HUD y menús»…). Si el fallo es de algo descartado (etiqueta `chamber`), no se registra: díselo a la persona. Si es de un modo fuera de la línea principal (Todos contra Todos, Carrera, Rally), la issue nueva lleva su etiqueta `modo:tct|carrera|rally` y se probó en su `dev-<modo>` (dilo en `--como`). Objetos: vista «Objetos» o `gh issue list --label objeto`. Lee los resúmenes de sus sub-issues (`tablero.py resumenes <objeto>`) y busca con `gh issue list --state all --search "<palabras>"`, cerradas incluidas: no hace falta leer todas las issues, sí las de ese objeto y las abiertas en Revisiones.
2. **Mismo fallo** que una issue existente (misma causa o síntoma, aunque esté cerrada): `tablero.py editor <n> falla --como "<PIE 4P, Standalone…>" --nota "<qué pasa y cómo reproducirlo>"`. En In progress se queda ahí con el fallo comentado; en otro estado pasa a Revisiones, se reabre si estaba cerrada y lleva `regresion` si ya funcionaba. No abras otra.
3. **Fallo distinto del mismo objeto**: sub-issue nueva (`tablero.py nueva --tipo bug --estado Ready --objeto "<objeto>" --prioridad <P0-P3> --tamano <XS-L> --area <Área> [--fase <F0-F8>] --titulo "..." --cuerpo f.md`) con pasos, esperado y obtenido, mapa y jugadores, citando las issues parecidas con su resumen. `nueva` la rechaza si el título pasa de 80 caracteres o el cuerpo no trae al menos un criterio de aceptación como casilla (`- [ ] Ya no pasa X en PIE 4P`). Si el objeto no existe, `nueva --objeto "<nombre>"` lo crea (nombre de sistema, no de síntoma); si hay uno parecido, lo enseña y no crea nada: casi siempre es ese, usa su título exacto.
4. Varios fallos seguidos: uno por issue. Di en una línea qué has hecho.
5. Si lo que dice el usuario no es un fallo sino un cambio de opinión sobre una issue de otro («mejor 4 s que 5»), no abras nada: `tablero.py pedir <n> --texto "..."` (o `decidir` si es una decisión de diseño de un aprobador).

## Cuando algo funciona

`tablero.py editor <n> funciona --como "<cómo lo probó>"`:

- Se prueba **en la rama del lote**, antes de fusionar (#282). Una issue `refactor` no necesita esta prueba para fusionarse, pero si alguien la prueba se registra igual.
- In progress o In review: Editor = Funciona.
- Con la revisión aprobada y la PR sin fusionar (lote o issue suelta): pasa a Validada. Con todo el lote en Validada, a Done y se fusiona (`tortu-revisar`).
- En QA editor (revisión IA aprobada, falta la prueba): sin fusionar pasa a Validada; lo fusionado antes de #282 pasa a Done y se cierra. Deja el resumen: `tablero.py resumen <n> --que "<qué fallaba>" [--por-que "<causa>"] --como "<arreglo>"`.

## Validación implícita

Si el usuario reportó un fallo, se corrigió y siguió probando sin volver a mencionarlo, está validado: al acabar la tanda, regístralas con `--como "validación implícita"` y una nota de lo que probó, y díselo en una línea. No marques `Funciona` algo que nadie ha probado en el editor: la revisión de código va en `tablero.py ia`.
