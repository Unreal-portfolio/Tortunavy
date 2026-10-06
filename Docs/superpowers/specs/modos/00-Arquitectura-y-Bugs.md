# Arquitectura común de los modos y sistema para encontrar bugs rápido

Fecha: 2026-09-29 · Rama: `macro-update` · Aplica a Coop, Carrera, Rally, Todos contra Todos y 2 vs 2.

Objetivo: que los cinco modos compartan el máximo de código, que añadir un objeto, un puzzle o un modo no obligue a tocar el núcleo, y que un bug se localice en minutos (quién, dónde, en qué máquina y con qué estado).

## 1. Patrones de diseño

| Patrón | Dónde | Regla |
|---|---|---|
| **Núcleo funcional, cáscara imperativa** (ya en uso: `*Decisions.h`) | Toda regla de juego: puntuación, atribución, dirección contraria, reaparición, rescate, loot | La decisión es una función pura sin UObject, con test de automatización; el actor solo recoge datos y aplica el resultado |
| **Plantilla + estrategia de modo** | `ATN_RunGameMode` como base común; cada modo aporta un `UTN_ModeRules` (UObject) con `OnMatchStart`, `OnPlayerScored`, `OnPlayerOut`, `IsMatchOver`, `BuildResults` | El GameMode no conoce modos concretos; Coop es la estrategia base; 2 vs 2 y Carrera heredan de ella y sobrescriben solo lo que cambia |
| **Máquina de estados de partida** | `ETNMatchPhase`: Lobby → Carga → Cuenta atrás → Juego → Cierre → Resultados → Siguiente | Transiciones solo en el servidor; la fase se replica en el GameState; cada estado tiene entrada, salida y tiempo máximo (nada se queda colgado) |
| **Datos, no código** | `UTN_ItemDef`, `UTN_PuzzleTemplate`, `UTN_ModeDef`, `UTN_MapDef` como `UPrimaryDataAsset` | Un objeto o un puzzle nuevo es un asset de datos más, como mucho, una clase de efecto; nada de `switch` por tipo en el núcleo |
| **Componentes componibles** | Progreso de carrera, inventario, carga/lanzamiento, caparazón, enderezar, reaparición | Un componente por responsabilidad; los modos los añaden al pawn o al PlayerState que necesiten |
| **Mensajería (observador)** | `UGameplayMessageSubsystem` o delegados del GameState: «objeto usado», «jugadora fuera», «puzzle resuelto», «sabotaje» | Interfaz, efectos, audio y telemetría escuchan; el gameplay no llama a la interfaz |
| **Interfaz de activación** | `ITN_Activatable` (placas, palancas, botones → puertas, géiseres, plataformas) | Cualquier emisor activa cualquier receptor; los puzzles se montan por datos |
| **Pool de objetos** | Proyectiles, cajas, efectos, marcas de rueda | Nada de spawn/destroy por disparo; tamaño del pool por mapa |
| **Servicios por subsistema** | Loot, rescate, telemetría, bug report | `UWorldSubsystem`/`UGameInstanceSubsystem`, nunca singletons globales |

## 2. Sistema para encontrar bugs rápido

| Pieza | Qué hace | Cómo se usa |
|---|---|---|
| **Categorías de log por sistema** | `LogTNMatch`, `LogTNItems`, `LogTNShell`, `LogTNNet`, `LogTNRally`, `LogTNPuzzle`… con prefijo de máquina (Servidor/Cliente N) | `log LogTNItems Verbose` en consola |
| **CVars de depuración por sistema** | `TN.Debug.<Sistema> 1` dibuja estado en pantalla (radios, dueños, autoridad, fases) | Sin recompilar; ya existe `TN.Shell.Debug` como patrón |
| **Invariantes** | `ensureMsgf` en desarrollo para estados imposibles (dos dueños, jugadora bajo el terreno, fase sin salida) | Cada invariante roto deja traza con el estado completo |
| **Detector de desincronía** | Cada 2 s el servidor envía un hash compacto del estado crítico (posiciones redondeadas, puntos, fase, objeto en mano); el cliente compara y registra `[Desync]` con el campo que difiere | Encuentra bugs de red sin mirar vídeos |
| **Informe de bug en el juego** | Tecla F8: guarda captura, últimos 2 000 registros de log, estado del GameState y del jugador en JSON, semilla, mapa y commit en `Saved/BugReports/<fecha>/` | Los playtesters pulsan una tecla y lo mandan |
| **Repeticiones** | Grabación con el `DemoNetDriver` en playtests (`demorec`) | Reproducir el bug fotograma a fotograma |
| **Monkey y estrés** | `TN.Monkey`, `TN.Stress` (en curso) con semilla reproducible | Cada noche en la rutina local o antes de cada PR grande |
| **Tests** | Decisiones puras + tests de integración headless por modo (partida corta de bots de principio a fin) | 97 tests hoy; objetivo: un test de partida completa por modo |
| **CI** | Build DebugGame + tests + pytest en cada PR a `macro-update` | Rutinas en la nube (revisión) + build local (compilación) |
| **Telemetría de playtest** | Eventos clave (muertes, rescates, uso de objetos, tiempos de tramo) a CSV local | Equilibrio y detección de zonas problemáticas |

## 3. Reglas para todo el código nuevo

1. Toda regla nueva nace como decisión pura con test que falla sin ella.
2. Todo estado replicado tiene dueño claro (servidor) y aparece en el hash de desincronía si afecta al resultado.
3. Todo sistema nuevo trae su categoría de log y su `TN.Debug.<Sistema>`.
4. Nada de `BlueprintImplementableEvent` para efectos: `UPROPERTY(EditDefaultsOnly)` + `SpawnSoundAtLocation`/`SpawnSystemAtLocation`.
5. Sin assets nuevos si hay uno existente reutilizable por tinte, escala o material.

## 4. Coste

Arquitectura común (estrategia de modo, fases, DataAssets, mensajería, `ITN_Activatable`): 30–40 h, se hace una vez y reduce el coste de cada modo. Sistema de bugs (logs, CVars, invariantes, desincronía, F8, repeticiones): 20–28 h. Entra al principio del plan F0–F2.
