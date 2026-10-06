# Integración de la nube: 29-09-2026

Rama de integración: `macro-update`.

## Fusionadas

Ninguna.

## No fusionadas

### #9: fix: revisión de bugs de la nube del 29-09-2026 (`nube/bugs-2026-09-29`)

- Estaba abierta contra `main`; se le cambió la base a `macro-update`. La rama ya partía de `macro-update`, sin conflictos.
- Toca `Source/`: `TN_CarryComponent` (nuevo `EndPlay`), `TortugaCharacter_Dive`, `TN_BeachRoundSyncComponent`, `TN_TutorialPlayerComponent`, `MP_GameInstance` y `TN_MapVariantLoader`. Por eso lleva la etiqueta `necesita-unreal` y queda pendiente de la sesión local.
- Revisión estática sin fallos serios: la autoridad y la validación de los RPC están bien (NaN en `ServerThrow`, ronda futura en `ServerReportRoundReady`, guardas del panzazo en el servidor), y los filtros de PIE con varias ventanas son correctos.
- No toca `Content/`, `.uasset`, `.umap` ni `Deprecado/`. Los commits son conformes.
- Pendiente en local: compilar DebugGame Editor, pasar los tests de automatización `Tortunabo.*` y probar en PIE con 4 jugadores. Los casos concretos están en la revisión de la PR.

## Comprobaciones

- Pytest de `Scripts/tests`: no aplica, porque ninguna PR toca `Scripts/`.
