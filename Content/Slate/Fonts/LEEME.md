# Fuentes de reserva de los idiomas CJK

Las usa `TNHUDFonts` (`Source/Tortunabo/Private/UI/HUD/TN_HUDFonts.*`) para el japonés, el coreano y el chino, solo cuando el
juego está en ese idioma; la lista de archivos está en `Config/DefaultGame.ini` (`[/Script/Tortunabo.TN_LanguageSettings]`).
Se empaquetan como archivos sueltos (`DirectoriesToAlwaysStageAsNonUFS`). Al arrancar, el log dice
«[Fuentes] ja: fuente de reserva NotoSansJP-Regular.ttf» (y lo mismo en ko, zh-Hans y zh-Hant).

| Archivo | Idioma | Peso |
|---|---|---|
| `NotoSansJP-Regular.ttf`, `NotoSansJP-Bold.ttf` | Japonés | 400 y 700 |
| `NotoSansKR-Regular.ttf`, `NotoSansKR-Bold.ttf` | Coreano | 400 y 700 |
| `NotoSansSC-Regular.ttf`, `NotoSansSC-Bold.ttf` | Chino simplificado | 400 y 700 |
| `NotoSansTC-Regular.ttf`, `NotoSansTC-Bold.ttf` | Chino tradicional | 400 y 700 |

**Origen.** Repositorio oficial `google/fonts` (rama `main`, carpetas `ofl/notosansjp`, `ofl/notosanskr`, `ofl/notosanssc` y
`ofl/notosanstc`), descargado el 2026-10-02. Allí solo está la versión de peso variable (`NotoSans??[wght].ttf`, eje `wght`
100-900 con 100 por defecto): Unreal dibuja una fuente variable con su peso por defecto (Thin), así que se sacaron dos
instancias estáticas, 400 (Regular) y 700 (Bold), con `fontTools` 4.x:

```python
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
instancer.instantiateVariableFont(TTFont("NotoSansJP[wght].ttf"), {"wght": 400}, updateFontNames=True).save("NotoSansJP-Regular.ttf")
```

No se recortaron: cada archivo pesa entre 5,8 y 10,6 MB (59 MB en total), por debajo del límite de 20 MB por archivo.
Cobertura comprobada con `uv run --with fonttools python Scripts/tools/check_font_coverage.py`: los cuatro idiomas, sin
caracteres que dependan de la reserva del motor.

**Licencia.** SIL Open Font License 1.1 (texto completo en `OFL.txt`, el mismo en las cuatro carpetas de origen): uso comercial
y redistribución permitidos, también modificadas, siempre que vaya el texto de la licencia y no se vendan sueltas. Las
instancias estáticas son una modificación permitida; no usan el nombre reservado («Source»). En los créditos del juego hay que
citar «Noto Sans JP/KR/SC/TC, Copyright 2014-2021 Adobe, SIL Open Font License 1.1» (la línea de copyright de `OFL.txt`).
