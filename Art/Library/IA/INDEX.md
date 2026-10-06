# Biblioteca de borradores IA

Borradores para que el equipo de modelado los rehaga; **no están importados a Content**. Se regeneran con `powershell -File Art/Library/IA/_pipeline/build_library.ps1` (opcional `-Only slug1,slug2` o `-Category puzzles`), que también reescribe este índice.

Convenciones: FBX en cm (UnitScaleFactor 1), +X delante, Z arriba; origen en la base (props) o en el punto de agarre (objetos de mano, socket `Grip` en el origen); 1 material `M_TN_IAProp` con zonas en el color de vértice `Zone` (máscaras Trim/Paint/Detail/Dark/Light), sin texturas. Colisión simple en `UCX_*` cuando la hay. Import en Unreal: Static Mesh, Combine Meshes on, Import Vertex Color = Replace, Normal Import = Import Normals, Auto Generate Collision off.

| Categoría | Asset | Mallas (tris) | Total / presupuesto | Tamaño (cm, X×Y×Z) | Sockets | Herramienta | Estado | Validación | Lámina |
|---|---|---|---|---|---|---|---|---|---|
| todos_contra_todos | [Balón de playa](todos_contra_todos/balon_playa/manifest.json) | `SM_TN_BalonPlaya` (212) | 212 / 400 | 49.24×49.24×51.2 | Grip | bpy procedural | borrador IA | OK | [PNG](todos_contra_todos/balon_playa/balon_playa_lamina.png) |
| todos_contra_todos | [Garfio con ancla](todos_contra_todos/garfio_ancla/manifest.json) | `SM_TN_Garfio` (382)<br>`SM_TN_Ancla` (324) | 706 / 1200 | 50.8×14.8×24<br>25.7×15.08×25.2 | Grip, Muzzle, Rope | bpy procedural | borrador IA | OK | [PNG](todos_contra_todos/garfio_ancla/garfio_ancla_lamina.png) |
| todos_contra_todos | [Pala de mano](todos_contra_todos/pala_mano/manifest.json) | `SM_TN_PalaMano` (336) | 336 / 800 | 72.85×20×5.2 | Grip, Muzzle | bpy procedural | borrador IA | OK | [PNG](todos_contra_todos/pala_mano/pala_mano_lamina.png) |
| todos_contra_todos | [Pistola de noqueo con dardo de medusa](todos_contra_todos/pistola_noqueo/manifest.json) | `SM_TN_PistolaNoqueo` (566)<br>`SM_TN_DardoMedusa` (214) | 780 / 1800 | 35×13×30.4<br>21.15×8×8 | Grip, Muzzle | bpy procedural | borrador IA | OK | [PNG](todos_contra_todos/pistola_noqueo/pistola_noqueo_lamina.png) |
| todos_contra_todos | [Pistola de tinta](todos_contra_todos/pistola_tinta/manifest.json) | `SM_TN_PistolaTinta` (434) | 434 / 1500 | 45×8.88×32 | Grip, Muzzle | bpy procedural | borrador IA | OK | [PNG](todos_contra_todos/pistola_tinta/pistola_tinta_lamina.png) |
| todos_contra_todos | [Trabuco de aire](todos_contra_todos/trabuco_aire/manifest.json) | `SM_TN_TrabucoAire` (750) | 750 / 1500 | 67.2×12.8×27.1 | Grip, Muzzle | bpy procedural | borrador IA | OK | [PNG](todos_contra_todos/trabuco_aire/trabuco_aire_lamina.png) |
| puzzles | [Ascensor de contrapeso](puzzles/ascensor_contrapeso/manifest.json) | `SM_TN_AscensorMarco` (268)<br>`SM_TN_AscensorPlataforma` (232)<br>`SM_TN_AscensorContrapeso` (336) | 836 / 1500 | 190×304×685.02<br>208×258×251.8<br>64.68×69.06×103.3 | Platform, PulleyPlatform, PulleyWeight, Rope, Weight | bpy procedural | borrador IA | OK | [PNG](puzzles/ascensor_contrapeso/ascensor_contrapeso_lamina.png) |
| puzzles | [Boquilla de géiser orientable](puzzles/boquilla_geiser/manifest.json) | `SM_TN_GeiserBase` (230)<br>`SM_TN_GeiserBoquilla` (232) | 462 / 800 | 135.16×136.55×56<br>27.58×44.5×67 | Muzzle, Pivot | bpy procedural | borrador IA | OK | [PNG](puzzles/boquilla_geiser/boquilla_geiser_lamina.png) |
| puzzles | [Cesta de playa con aro](puzzles/cesta_aro/manifest.json) | `SM_TN_CestaAro` (536) | 536 / 1200 | 116.06×85×330 | Goal | bpy procedural | borrador IA | OK | [PNG](puzzles/cesta_aro/cesta_aro_lamina.png) |
| puzzles | [Palanca con base](puzzles/palanca/manifest.json) | `SM_TN_PalancaBase` (220)<br>`SM_TN_PalancaBrazo` (148) | 368 / 800 | 74.36×52×52<br>15×15×73.1 | Pivot | bpy procedural | borrador IA | OK | [PNG](puzzles/palanca/palanca_lamina.png) |
| puzzles | [Placa de presión (subida y bajada)](puzzles/placa_presion/manifest.json) | `SM_TN_PlacaPresion_Subida` (196)<br>`SM_TN_PlacaPresion_Bajada` (188) | 384 / 800 | 124×124×12.2<br>124×124×8.4 | — | bpy procedural | borrador IA | OK | [PNG](puzzles/placa_presion/placa_presion_lamina.png) |
| puzzles | [Puerta de puzzle de madera y conchas](puzzles/puerta_puzzle/manifest.json) | `SM_TN_PuertaMarco` (674)<br>`SM_TN_PuertaHoja` (352) | 1026 / 1500 | 46.8×332×325.76<br>14.9×218.8×251.38 | Leaf, LeafOpen | bpy procedural | borrador IA | OK | [PNG](puzzles/puerta_puzzle/puerta_puzzle_lamina.png) |
| decoracion | [Molino (de L04 Rin / pl)](decoracion/molino/manifest.json) | `SM_TN_MolinoTorre` (380)<br>`SM_TN_MolinoAspas` (522) | 902 / 1500 | 426.32×380.64×672<br>40×749.54×749.54 | Sails | bpy procedural | borrador IA | OK | [PNG](decoracion/molino/molino_lamina.png) |
| decoracion | [Pagoda (zh, L12 Guilin / L13 Taroko)](decoracion/pagoda/manifest.json) | `SM_TN_Pagoda` (838) | 838 / 1500 | 468×470×744 | — | bpy procedural | borrador IA | OK | [PNG](decoracion/pagoda/pagoda_lamina.png) |
| decoracion | [Torii (ja, L10 Fuji)](decoracion/torii/manifest.json) | `SM_TN_Torii` (300) | 300 / 1500 | 62×670×508 | — | bpy procedural | borrador IA | OK | [PNG](decoracion/torii/torii_lamina.png) |

15 assets, 24 mallas, 8870 triángulos en total.

Licencia de todos: original de Tortunabo, generado por script (sin fuentes externas). Los FBX y .blend no se versionan (el repositorio no tiene Git LFS para esas extensiones).
