# Terreno «España» E01 — el relieve real de la Península y las Baleares

Fecha: 2026-09-29. Rama `feat/procgen-terrain`. Misma cadena que C01 y P01 (densidad 3D → marching cubes → trozos
TNTM2 → `ATN_MapVariantLoader`), con un modelo de densidad alimentado por un modelo digital de elevación (MDE).

## 1. Qué es

Un volumen de 6 × 6 trozos (600 × 600 m de juego) con la Península y las Baleares en su sitio y con su relieve
real. La frontera es la costa: Portugal, Francia y Marruecos son mar somero y la frontera terrestre cae al agua como un
acantilado, de modo que España queda como una isla. Canarias, Ceuta y Melilla quedan fuera de la ventana.

| Magnitud | Valor |
|---|---|
| Escala horizontal | 1 m de juego = 2 800 m Mercator ≈ 2,1 km de suelo a 40° N |
| Escala vertical | 3 480 m reales = 40 m de juego (`--peak`); tras el suavizado, Mulhacén queda a 29 m sobre el agua |
| Exageración vertical | ~ 20 × respecto de la horizontal |
| Mar | fondo a 1,5 m bajo el agua junto a la costa y hasta 5,5 m según la batimetría real |
| Proyección | Web Mercator, Norte arriba (la de los mapas habituales) |

## 2. Datos y flujo

```text
teselas terrarium z7 (SRTM, GMTED, ETOPO1) + Natural Earth 1:50m
        │  terrain_geo/fetch_spain.py  (descarga, se cachea en Saved/terrain_geo_cache/)
        ▼
terrain_geo/data/ES_dem.png (16 bits, elevación + 5000 m) + ES_mask.png (cobertura de España)   ← versionados
        │  gen_terrain_spain.py  (terrain_geo/model.py: SpainModel)
        ▼
terrain_volumes/Variants/E01_espana/ (+ index.json)
```

- Rasters de 1 200 × 1 200 px (0,5 m de juego por píxel) del volumen entero, Norte arriba en el PNG.
- Regenerar los datos: `uv run --with numpy --with scipy --with pillow python -m terrain_geo.fetch_spain` (desde `Scripts/`).
- Regenerar el mapa: `uv run --with numpy --with scipy --with pillow --with scikit-image python Scripts/gen_terrain_spain.py`
  (opciones `--peak`, `--start lat,lon`, `--end lat,lon`, `--name`; unos 25 s).
- Tests: `pytest Scripts/tests/test_terrain_spain.py`.
- Atribución: Mapzen/AWS Terrain Tiles (SRTM, GMTED, ETOPO1 y otras fuentes abiertas) y Natural Earth (dominio público).

## 3. Geometría

- **Relieve**: cota real ≥ 0 dentro de España, suavizada 1,5 m de juego (lo que el vóxel de 1 m no puede mostrar), con la
  pendiente reducida hacia un objetivo de 1,4 m por m (`limit_slope`: cada píxel cede un cuarto del exceso a sus vecinos
  más bajos, como un talud; tras 90 iteraciones el p99 queda en ~1,9) y multiplicada por `K = peak / 3480`. Sin ese paso,
  las cordilleras salían como agujas.
- **Relieve fino**: 4 octavas de ruido desde 48 m, de 0,12 m en llano a 1,1 m en sierra alta. Sin ruido 3D de pared: en un campo de
  alturas escalaba la ladera en rayas horizontales.
- **Fusión**: `altura = mar + (tierra − mar) · cobertura_suavizada` (σ 1,25 m de juego). En la costa da una playa (la tierra
  queda 0,45 m sobre el agua); en la frontera terrestre, un acantilado.
- **Color**: paleta de arena; playa clara bajo 3,5 m sobre el agua (valles del Guadalquivir y del Ebro, costas) y arena que
  se oscurece con la altura (sierras).

## 4. Manifest y nivel

- `kill_boxes_uu`: una caja sobre todo el mar (hasta 0,2 m sobre el agua). El mar no se nada: salir de España es morir.
- Inicio: Roncesvalles (43,009° N, −1,320° E); final: Santiago de Compostela (42,881° N, −8,545° E), el Camino Francés. Son
  supuestos: `--start` y `--end` los cambian.
- Se comprueba que el final se alcanza a pie desde el inicio (desnivel ≤ 1 m por m).
- El agua de los niveles (centro (0, 0), 1 200 m, cota −400 uu) cubre el volumen entero.
- En el editor: elegir `E01_espana` en el desplegable de `ATN_MapVariantLoader` y pulsar Recargar.

## 5. Salvedades conocidas

- Solo se alcanza a pie una parte de España desde el inicio (las sierras a 45° cortan el paso); los pasos naturales
  (Roncesvalles, O Cebreiro) sí se recorren.
- Los ríos no se resuelven (el MDE da ~1,2 km por píxel, 0,6 m de juego): no hay Ebro, Tajo ni Guadalquivir como agua, solo sus valles.
- Dentro de la ventana no hay Canarias: irían como recuadro aparte.
- La exageración vertical hace las sierras más abruptas que las reales; `--peak` la baja.
