# Terreno «plataformas» P01 — mesetas de arena sobre un río, unidas por puentes colgantes

Fecha: 2026-09-29. Rama `feat/procgen-terrain`. Origen: plano dibujado a mano por Rodrigo (rejilla de 2 m).

## 1. Qué es

Mapa fijo de 200 × 200 m calcado del plano: nueve mesetas de arena (+20, +25 y +30 m sobre el agua), un río a 0 m que
las rodea y nueve puentes colgantes que las unen. Usa la misma cadena que C01 (densidad 3D → marching cubes → trozos
TNTM2 → `ATN_MapVariantLoader`), con un modelo de densidad propio en lugar del camino-primero.

| Clase del plano | Cota de la cima | Superficie |
|---|---|---|
| Plataforma arena +20 m | `WATER_M + 20` = 16 m | 2 plataformas |
| Plataforma arena +25 m | `WATER_M + 25` = 21 m | 5 plataformas |
| Plataforma arena +30 m | `WATER_M + 30` = 26 m | 1 plataforma con cuello + 1 en el borde sur |
| Ríos 0 m | agua a `WATER_M` = −4 m; lecho a −5,5 m | resto |
| Puentes colgantes | cara superior entre las cotas de sus dos extremos, con flecha | 9 |

Las cotas del plano se miden **sobre el agua** (el agua de Unreal está en −4 m).

## 2. Flujo

```text
plano.jpeg ──extract_layout.py──▶ terrain_platforms/P01_layout.png (400×400, 0,5 m/px, Norte arriba)
                                     │
                       gen_terrain_platforms.py ──▶ terrain_volumes/Variants/P01_plataformas/ (+ index.json)
```

- `uv run --with numpy --with scipy --with pillow python -m terrain_platforms.extract_layout <plano> ` (desde `Scripts/`):
  clases por color, mediana para quitar rejilla y trazos, motas descartadas.
- `uv run --with numpy --with scipy --with pillow --with scikit-image python Scripts/gen_terrain_platforms.py` (unos 8 s).
- Tests: `pytest Scripts/tests/test_terrain_platforms.py` (9).

Para otro plano: `--layout otro.png --name P02_otro` (mismo formato de PNG: paleta de 5 colores, índices 0 río, 1-3 plataformas
+20/+25/+30, 4 puente).

## 3. Geometría

- **Volumen 3 × 3 trozos (300 m)**: el plano ocupa el centro (0..200 m) y lo rodean 50 m de río. La malla no se cierra en el
  borde del volumen; con el marco, ninguna plataforma queda cortada ahí (una versión previa de 2 × 2 trozos dejaba paredes
  huecas y obligaba a recortar las plataformas 8 m).
- **Plataforma** = distancia con signo al contorno dibujado (suavizada 1,25 m) + inclinación del acantilado (0,15 m por metro
  de caída, unos 9°) + ruido de pared 3D (1,3 m a 7 m y 0,5 m a 2,8 m), unida con la cima por un mínimo suave (borde
  redondeado 0,9 m). La cima ondula ±0,45 m, salvo junto a un puente, donde se aplana para que el tablero entre a ras.
- **Puente**: eje sacado de la mancha naranja (cortes de 1 m por su eje principal); ancho del dibujo acotado a 3,6–9 m; tablero
  de 1,6 m de grosor que cuelga con flecha (`0,035 m` por metro de luz, entre 0,5 y 2,6 m) y va plano en los 5 m que se meten
  en cada plataforma.
- **El puente que cruza el cuello oscuro** son dos manchas del plano enfrentadas a ambos lados de una plataforma más alta cuyos
  extremos lejanos tienen la misma cota: se unen en un puente de 73 m y el modelo excava un **arco** de 4 m sobre el tablero
  en la roca del cuello (se atraviesa como un túnel).
- Color de vértice: arena en todo el mapa (paleta `sand`, pared más oscura por la pendiente); tablón oscuro sobre los tableros.

## 4. Manifest y nivel

- `kill_boxes_uu`: una caja sobre todo el río (de −6,5 a −2 m). **Caer al río es morir** (el agua no se nada y los
  acantilados no se suben); el `ATN_DeathZoneVolume` solo actúa con la partida en curso. Quitar la caja del manifest si se
  prefiere nadar o vadear.
- `start_uu`: centro de la plataforma +20 más grande (norte); `end_uu`: lóbulo sur de la plataforma +30 (con el cuello).
  Ambos son supuestos: el plano no marca inicio ni final.
- El agua del nivel (`LVL_MapVariants` / `LVL_Demo01`: centro (0, 0), 1200 m, cota −400 uu) cubre el volumen entero.
  No hay corona de terreno barato: el marco de río la sustituye.
- En el editor: elegir `P01_plataformas` en el desplegable de `ATN_MapVariantLoader` y pulsar Recargar.

## 5. Comprobaciones (sobre la malla, `gen_terrain_platforms.check`)

- el inicio llega a pie al final; las 9 plataformas y los 9 puentes se recorren; el tablero es andable en toda su longitud
  (también dentro del arco del cuello);
- las cimas quedan a menos de 1,5 m de la cota del plano a 5 m del contorno; el río queda bajo el agua y a menos de 2,5 m
  de profundidad; el borde del volumen es solo lecho.

## 6. Salvedades conocidas

- La plataforma +30 del borde sur toca la +25 contigua (en el plano no hay puente entre ellas). En el borde del plano,
  donde las dos coinciden con el recorte, la cresta baja de 30 a 24 m con una rampa de ~27° y se puede subir andando; el resto
  de la unión es un escalón de 5 m. Si debe ser un muro, separar los contornos en el layout.
- Los tableros son mallas lisas sin cuerdas, tablones ni barandillas: el aspecto de puente colgante lo pone un diseñador
  encima (assets), como los castillos de arena.
- La leyenda del plano (esquina superior derecha) tapaba río; se rellena con río.
