# Biblioteca de borradores IA

Borradores para que el equipo de modelado los rehaga; **no están importados a Content**. Se regeneran con `powershell -File Art/Library/IA/_pipeline/build_library.ps1` (opcional `-Only slug1,slug2` o `-Category puzzles`), que también reescribe este índice.

Convenciones: FBX en cm (UnitScaleFactor 1), +X delante, Z arriba; origen en la base (props) o en el punto de agarre (objetos de mano, socket `Grip` en el origen); 1 material `M_TN_IAProp` con zonas en el color de vértice `Zone` (máscaras Trim/Paint/Detail/Dark/Light), sin texturas. Colisión simple en `UCX_*` cuando la hay. Import en Unreal: Static Mesh, Combine Meshes on, Import Vertex Color = Replace, Normal Import = Import Normals, Auto Generate Collision off.

| Categoría | Asset | Mallas (tris) | Total / presupuesto | Tamaño (cm, X×Y×Z) | Sockets | Herramienta | Estado | Validación | Lámina |
|---|---|---|---|---|---|---|---|---|---|
| puzzles | [Palanca con base](puzzles/palanca/manifest.json) | `SM_TN_PalancaBase` (220)<br>`SM_TN_PalancaBrazo` (148) | 368 / 800 | 74.36×52×52<br>15×15×73.1 | Pivot | bpy procedural | borrador IA | OK | [PNG](puzzles/palanca/palanca_lamina.png) |
| puzzles | [Placa de presión (subida y bajada)](puzzles/placa_presion/manifest.json) | `SM_TN_PlacaPresion_Subida` (196)<br>`SM_TN_PlacaPresion_Bajada` (188) | 384 / 800 | 124×124×12.2<br>124×124×8.4 | — | bpy procedural | borrador IA | OK | [PNG](puzzles/placa_presion/placa_presion_lamina.png) |
| decoracion | [Molino (de L04 Rin / pl)](decoracion/molino/manifest.json) | `SM_TN_MolinoTorre` (380)<br>`SM_TN_MolinoAspas` (522) | 902 / 1500 | 426.32×380.64×672<br>40×749.54×749.54 | Sails | bpy procedural | borrador IA | OK | [PNG](decoracion/molino/molino_lamina.png) |
| decoracion | [Pagoda (zh, L12 Guilin / L13 Taroko)](decoracion/pagoda/manifest.json) | `SM_TN_Pagoda` (838) | 838 / 1500 | 468×470×744 | — | bpy procedural | borrador IA | OK | [PNG](decoracion/pagoda/pagoda_lamina.png) |
| decoracion | [Torii (ja, L10 Fuji)](decoracion/torii/manifest.json) | `SM_TN_Torii` (300) | 300 / 1500 | 62×670×508 | — | bpy procedural | borrador IA | OK | [PNG](decoracion/torii/torii_lamina.png) |

5 assets, 8 mallas, 2792 triángulos en total.

Licencia de todos: original de Tortunabo, generado por script (sin fuentes externas). Los FBX y .blend no se versionan (el repositorio no tiene Git LFS para esas extensiones).
