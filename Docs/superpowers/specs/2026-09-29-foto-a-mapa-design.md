# Foto → Mapa: diseño

Fecha: 2026-09-29 · Rama: `macro-update` · Estado: aprobado por el director (pendiente de revisión del texto)

## 1. Objetivo

Convertir una foto o un boceto (papel, pizarra, captura) en un **mapa jugable de partida** que los diseñadores y modeladores terminan a mano. Claude analiza la imagen con visión, la traduce a una descripción estructurada y el pipeline genera terreno, assets y puzzles colocados. Sirve para Coop, 2 vs 2, Rally y Todos contra Todos.

Criterio de éxito: de una imagen a un nivel abierto en el editor, con terreno transitable, salida y meta, assets y puzzles colocados, en menos de 30 min de trabajo de Claude, y que un diseñador pueda editarlo todo sin tocar el generador.

## 2. Decisiones del director

1. **Flujo en una sola dirección.** El mapa se genera una vez y se entrega; después es de los modeladores. El generador no vuelve a tocar ese mapa. Una nueva versión desde la imagen se genera como mapa aparte.
2. **Todo lo generado es editable**, terreno incluido, sea cual sea su origen (camino, isla, arena).
3. **Método de terreno oficial: camino** (`Scripts/gen_terrain_path.py`, `Scripts/terrain_path/`). Mapa01 está obsoleto.
4. **Assets del catálogo existente**, sin assets nuevos (los hace el equipo de arte).

## 3. Flujo

```text
imagen ──(Claude, visión)──▶ MapSketch.json ──▶ validador ──▶ generador (camino / isla / arena)
                                                              │
                              lámina de comparación ◀─────────┤
                                                              ▼
                                       trozos TNTM2 ──▶ import_terrain_mesh.py ──▶ StaticMesh por trozo
                                       marcadores   ──▶ script de editor headless ──▶ actores en el nivel
                                                              ▼
                                                LVL_<id>.umap + FBX del terreno ──▶ modeladores
```

1. **Lectura.** Claude mira la imagen, decide escala y modo, y escribe `MapSketch.json` (esquema en §4). Documenta en el propio JSON las dudas de interpretación (`notes`).
2. **Validación del boceto** (pytest): esquema, cotas dentro del rango vertical (128 niveles), caminos con ancho y pendiente del modo, salida y meta presentes, puzzles con sus piezas completas.
3. **Generación.** El tipo de boceto elige el generador: `path` (camino, por defecto), `island` o `arena` (Scripts/terrain_shapes, en curso). Salen trozos TNTM2 de 100 m, `manifest.json` y `markers.json`.
4. **Validación del terreno**: alcanzable a pie o saltando de salida a meta, pendiente y ancho mínimos, sin grietas entre trozos, presupuesto de triángulos y MB del plan maestro §2.5.
5. **Comparación.** Lámina PNG (cenital con cotas + perspectiva) junto a la imagen original. Claude las compara y, si la forma no se parece, corrige el JSON y regenera (máximo 3 vueltas; si no converge, lo dice y entrega igual con las diferencias anotadas).
6. **Entrega editable** (§5).

## 4. `MapSketch.json`

Unidades en metros; origen en la esquina inferior izquierda de la imagen; +X a la derecha, +Y hacia arriba en la imagen.

```json
{
  "id": "CP02_bahia",
  "mode": "coop",
  "generator": "path",
  "size_m": [600, 400],
  "water_level_m": 0,
  "source_image": "Docs/Mapas/fuentes/CP02_bahia.jpg",
  "notes": ["La mancha gris del centro se interpreta como acantilado de 18 m."],
  "zones":    [{"kind": "plateau", "polygon": [[40, 60], [180, 60], [180, 200], [40, 200]], "height_m": 20}],
  "paths":    [{"kind": "main", "points": [[20, 30], [150, 120], [420, 300]], "width_m": 8}],
  "water":    [{"kind": "river", "points": [[0, 210], [600, 190]], "width_m": 14}],
  "bridges":  [{"kind": "mokius", "from": [300, 180], "to": [300, 225]},
               {"kind": "natural", "from": [120, 190], "to": [120, 230], "width_m": 6}],
  "tunnels":  [{"points": [[450, 100], [520, 140]], "width_m": 6, "height_m": 5}],
  "start": [20, 30], "finish": [580, 380],
  "checkpoints": [[200, 130], [400, 280]],
  "assets":   [{"type": "Catapult", "at": [260, 150], "yaw_deg": 90},
               {"type": "decor_field", "polygon": [[0, 0], [100, 0], [100, 60]], "density": 0.6}],
  "puzzles":  [{"kind": "plate_balance", "at": [350, 240], "players": 3}],
  "hazards":  [{"type": "GullZone", "polygon": [[400, 300], [500, 300], [500, 380]]}],
  "safety":   {"out_of_bounds_rescue": true}
}
```

- `mode`: `coop` | `2v2` | `rally` | `ffa`. Fija límites por defecto (Rally: pendiente ≤ 12°, radio ≥ 25 m, ancho ≥ 12 m; resto: los del camino).
- `assets.type`: valores de `ETNBeachElement` (`Source/Tortunabo/Public/World/Beach/TN_BeachTypes.h`) y actores de mundo existentes (catapulta, tanque, trampolín, quads, géiser, plataformas móviles, tambaleantes y rompibles). Un tipo desconocido es error de validación, no se inventa.
- `puzzles.kind`: plantillas del catálogo de puzzles (§6). Cada plantilla se expande a sus piezas (placas, botones, `ATN_ProcSwitch`, puertas, cestas, `ATN_BeachShellGate`, `ATN_ProcSabotageGate`) con posiciones relativas.
- `safety.out_of_bounds_rescue`: coloca volúmenes de rescate donde gaviotas u otros efectos puedan dejar al jugador en zonas no permitidas (plan maestro §2.3).

## 5. Entrega editable

- **Terreno:** cada trozo de 100 m como `StaticMesh` en `/Game/Maps/<Modo>/<id>/Terrain/SM_<id>_<fila>_<col>` (vía `Scripts/import_terrain_mesh.py`), colocado en `LVL_<id>`. Se edita con Modeling Mode de Unreal. Además, `Art/Source/Maps/<id>/<id>_terrain.fbx` para quien prefiera Blender; al reimportar sustituye las mallas sin mover los actores.
- **Assets, puzzles, peligros, salida, meta y checkpoints:** actores normales del nivel, con carpetas de outliner por tipo (`Terreno`, `Assets`, `Puzzles`, `Peligros`, `Juego`).
- **Sin dependencia del generador:** el nivel no carga TNTM2 en ejecución ni referencia el JSON. El JSON y la lámina se guardan en `Docs/Mapas/<id>/` solo como registro.
- **Agua:** plano de agua existente a `water_level_m`.
- **Registro:** entrada en el catálogo de mapas con autor «Foto→Mapa», fecha e imagen de origen.

## 6. Puzzles como elementos del boceto

Las plantillas salen del catálogo de puzzles (documento aparte, `Docs/Catalogo-Puzzles-2026-09-29.md`, pendiente) y reutilizan piezas existentes. Primeras plantillas:

| Plantilla | Modo | Piezas |
|---|---|---|
| `plate_balance` | Coop | 3 placas de presión en grupo; la bola cuenta doble |
| `basket_hold` | Coop | cesta, palanca con temporizador, puerta |
| `throw_chain` | Coop | muro alto, botón al otro lado, rampa |
| `geyser_aim` | Coop | géiser con boquilla orientable por palanca |
| `lever_relay` | 2 vs 2 | 2 palancas a 30 m con ventana de 1,5 s |
| `counterweight_lift` | 2 vs 2 | placa, ascensor, rampa |
| `sabotage` | 2 vs 2 | botones que cierran la puerta de sabotaje del rival 5 s |
| `think_room` | 2 vs 2 | 4 placas + 2 botones y pistas en la pared |

Si falta una pieza en C++ (p. ej. cesta o palanca como actor propio), la plantilla se marca «pendiente» y el validador la rechaza hasta que exista.

## 7. Componentes

| Componente | Ruta | Responsabilidad |
|---|---|---|
| Esquema y validador del boceto | `Scripts/map_sketch/schema.py`, `validate.py` | Cargar y validar `MapSketch.json` |
| Adaptadores de generador | `Scripts/map_sketch/to_path.py`, `to_island.py`, `to_arena.py` | Traducir el boceto a la entrada de cada generador |
| Marcadores | `Scripts/map_sketch/markers.py` | Expandir assets y plantillas de puzzle a una lista de actores con clase, posición y rotación |
| Lámina | reutiliza `Scripts/terrain_vol/sheet.py` | Vista cenital con cotas + perspectiva, junto a la imagen |
| Montaje del nivel | `Scripts/editor/build_map_from_sketch.py` (editor headless, `-ExecCmds="py …"`) | Importar trozos, crear `LVL_<id>`, colocar actores por carpetas, exportar FBX |
| Orden | `Scripts/foto_a_mapa.py` | Orquestar validación → generación → lámina → montaje |
| Skill | `/foto-mapa <imagen> [modo]` | Guía para Claude: leer la imagen, escribir el JSON, iterar con la lámina |

## 8. Errores y límites

- Imagen ambigua: Claude elige una interpretación, la anota en `notes` y la señala en la lámina; no pregunta por cada detalle.
- Boceto no transitable: el validador dice qué tramo falla (coordenadas y motivo) y Claude corrige el JSON.
- Presupuesto de malla superado: se reduce la resolución del voxelizado o se divide el mapa, y se avisa.
- Tipo de asset o puzzle inexistente: error de validación con la lista de tipos válidos.

## 9. Pruebas

- pytest: esquema (válidos e inválidos), expansión de plantillas de puzzle, adaptadores (un boceto mínimo por generador produce terreno alcanzable), marcadores dentro del mapa y sobre el suelo.
- Editor headless: el nivel generado abre sin «Failed to load», tiene los actores esperados por carpeta y MapCheck da 0 errores.
- Piloto: una foto real de un boceto de Rodrigo (P01 como referencia) reproducida con Foto→Mapa y comparada con P01.

## 10. Fuera de alcance

- Regenerar sobre un mapa ya entregado.
- Assets nuevos o texturas nuevas.
- Diseño de nuevas mecánicas de puzzle (va en el catálogo de puzzles).
