"""Regenera la biblioteca de borradores Art/Library/IA (o una parte) y reescribe INDEX.md.

    blender -b --factory-startup --python-exit-code 1 --python Art/Library/IA/_pipeline/build.py -- [--only a,b] [--category c]
(o build_library.ps1). Por asset: <categoría>/<slug>/{<slug>.blend, SM_*.fbx, <slug>_lamina.png, manifest.json}.
Sale con código 1 si algún asset no pasa la validación (presupuesto, manifold, pivote, sockets, FBX reimportado).
"""
import argparse
import json
import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ia_pipeline  # noqa: E402
import assets_tct  # noqa: E402
import assets_puzzles  # noqa: E402
import assets_deco  # noqa: E402

MODULES = (assets_tct, assets_puzzles, assets_deco)
CATEGORY_ORDER = ['todos_contra_todos', 'puzzles', 'decoracion']


def _args():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument('--only', default='')
    parser.add_argument('--category', default='')
    return parser.parse_args(argv)


def write_index():
    rows = []
    for cat in CATEGORY_ORDER:
        root = os.path.join(ia_pipeline.LIBRARY_ROOT, cat)
        if not os.path.isdir(root):
            continue
        for slug in sorted(os.listdir(root)):
            path = os.path.join(root, slug, 'manifest.json')
            if os.path.isfile(path):
                with open(path, encoding='utf-8') as f:
                    rows.append(json.load(f))
    lines = [
        '# Biblioteca de borradores IA',
        '',
        'Borradores para que el equipo de modelado los rehaga; **no están importados a Content**. '
        'Se regeneran con `powershell -File Art/Library/IA/_pipeline/build_library.ps1` '
        '(opcional `-Only slug1,slug2` o `-Category puzzles`), que también reescribe este índice.',
        '',
        'Convenciones: FBX en cm (UnitScaleFactor 1), +X delante, Z arriba; origen en la base (props) o en el punto de '
        'agarre (objetos de mano, socket `Grip` en el origen); 1 material `M_TN_IAProp` con zonas en el color de '
        'vértice `Zone` (máscaras Trim/Paint/Detail/Dark/Light), sin texturas. '
        'Colisión simple en `UCX_*` cuando la hay. Import en Unreal: Static Mesh, Combine Meshes on, Import Vertex '
        'Color = Replace, Normal Import = Import Normals, Auto Generate Collision off.',
        '',
        '| Categoría | Asset | Mallas (tris) | Total / presupuesto | Tamaño (cm, X×Y×Z) | Sockets | Herramienta | '
        'Estado | Validación | Lámina |',
        '|---|---|---|---|---|---|---|---|---|---|',
    ]
    for m in rows:
        meshes = '<br>'.join(f"`{p['name']}` ({p['tris']})" for p in m['parts'])
        sizes = '<br>'.join('×'.join(f'{v:g}' for v in p['bbox_cm']['size']) for p in m['parts'])
        socks = sorted({s for p in m['parts'] for s in p['sockets_cm_deg']})
        ok = 'OK' if m['validation']['passed'] else 'FALLO: ' + '; '.join(m['validation']['failures'])
        folder = f"{m['category']}/{m['name']}"
        lines.append(f"| {m['category']} | [{m['title']}]({folder}/manifest.json) | {meshes} | "
                     f"{m['tris_total']} / {m['budget_tris']} | {sizes} | {', '.join(socks) or '—'} | "
                     f"{m['tool']['used']} | {m['status']} | {ok} | [PNG]({folder}/{m['name']}_lamina.png) |")
    total = sum(m['tris_total'] for m in rows)
    lines += ['', f'{len(rows)} assets, {sum(len(m["parts"]) for m in rows)} mallas, {total} triángulos en total.',
              '', 'Licencia de todos: original de Tortunabo, generado por script (sin fuentes externas). '
              'Los FBX y .blend no se versionan (el repositorio no tiene Git LFS para esas extensiones).', '']
    with open(os.path.join(ia_pipeline.LIBRARY_ROOT, 'INDEX.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))


def main():
    args = _args()
    only = {s for s in args.only.split(',') if s}
    failures = {}
    for module in MODULES:
        generator = ia_pipeline.rel(module.__file__)
        for asset in module.ASSETS:
            if only and asset['slug'] not in only:
                continue
            if args.category and asset['category'] != args.category:
                continue
            try:
                manifest, fails = ia_pipeline.run_asset(asset, generator)
            except Exception:  # un asset roto no para la biblioteca; se informa y sale con 1
                traceback.print_exc()
                failures[asset['slug']] = ['excepción (ver traza)']
                continue
            parts = ', '.join(f"{p['name']}={p['tris']}" for p in manifest['parts'])
            print(f"[ia] {'OK   ' if not fails else 'FALLO'} {asset['slug']}: {parts} "
                  f"total={manifest['tris_total']}/{asset['budget']} lámina={manifest['sheet_kb']} KB")
            for f in fails:
                print(f'[ia]       - {f}')
            if fails:
                failures[asset['slug']] = fails
    write_index()
    print(f'[ia] INDEX.md reescrito; fallos: {len(failures)}')
    if failures:
        sys.exit(1)


if __name__ == '__main__':
    main()
