# Créditos y licencias

Pantalla de créditos de Tortunavy (issue #351): el equipo, el arte de terceros, las fuentes tipográficas con su licencia y el
aviso del motor. Sale desde el **menú principal** (botón «Créditos», entre «Ajustes» y «Salir») y desde la **portada del menú de
pausa** (cuarto botón, tras «Controles»). Todo es código: no hay `.uasset` nuevos.

## Dónde están los datos

| Fichero | Qué es |
|---|---|
| `Content/Credits/Credits.json` | Los créditos. Se edita a mano, sin tocar C++. |
| `Content/Credits/OFL-1.1.txt` | Texto completo de la SIL Open Font License 1.1 (copiado de `Engine/Source/ThirdParty/Licenses/Noto_License.txt`). Se enseña entero al final de la sección de fuentes. |

`Content/Credits` se empaqueta dentro del `.pak` (`+DirectoriesToAlwaysStageAsUFS=(Path="Credits")` en `Config/DefaultGame.ini`)
y el juego lo lee por ruta (`TNCredits::DefaultPath`, `FPaths::ProjectContentDir()/Credits/Credits.json`).

## Formato

```json
{
	"format": 1,
	"sections": [
		{
			"id": "fonts",
			"entries": [ { "name": "Roboto", "roles": [], "source": "Google", "license": "Apache License 2.0" } ],
			"notes": [ "Aviso legal que se enseña tal cual." ],
			"licenseFile": "OFL-1.1.txt"
		}
	]
}
```

- `id`: clave de la sección. Su título se traduce en `TNCredits::SectionTitle` (`NSLOCTEXT("TNCredits", "Section…")`). Claves
  conocidas: `team` (EQUIPO), `art` (ARTE DE TERCEROS), `audio` (AUDIO DE TERCEROS), `fonts` (FUENTES TIPOGRÁFICAS), `maps`
  (DATOS GEOGRÁFICOS, preparada para la #112) y `engine` (MOTOR). Una clave nueva necesita su `NSLOCTEXT` y su traducción; si
  no, sale la clave en mayúsculas (y el test lo marca).
- `name`, `source`, `license` y `notes`: nombres propios, orígenes, licencias y avisos legales. **No se traducen**: se enseñan
  igual en los 13 idiomas (`TNLocText::Literal`). Los avisos legales van en el idioma en que los exige su dueño (inglés).
- `roles`: claves de papel que se traducen en `TNCredits::RoleName`: `director` (Dirección), `code` (Programación), `art` (Arte),
  `levels` (Niveles), `audio` (Música y sonido), `animations` (Animaciones). Varias se unen con « · ».
- `licenseFile`: nombre de un fichero **en la misma carpeta** (una ruta con `/`, `\`, `:` o `..` se rechaza con un aviso).

Cada línea enseña el nombre a la izquierda y, en dos columnas, los papeles (o el origen) y la licencia (o el origen, si hay
papeles). Un fichero que falta o está mal deja la página con «No se han podido cargar los créditos.» y un error en el registro.

## Qué se cita y de dónde sale

Solo lo verificable en el repositorio, en la documentación o en el motor instalado:

- **Equipo**: los de la guía del proyecto (`CLAUDE.md`) y los autores del repositorio (`Docs/Limpieza-2026-09-29.md`, `git
  shortlog`), con el papel que se ve en sus commits.
- **Arte de terceros**: animaciones de Mixamo (esqueleto Mixamo de `TotugaDemo_Rig`, `Docs/Inventario-Objetos-Arte-2026-09-29.md`).
- **Fuentes**: las que trae la fuente compuesta del motor (`Engine/Content/Slate/Fonts`, `Docs/Localizacion.md`): Roboto y Droid
  Sans Fallback (Apache 2.0) y Noto Naskh Arabic UI (OFL 1.1), con el texto de la OFL.
- **Motor**: el aviso de marca de Epic Games.

**Pendiente de registrar** (no se cita hasta saber su origen y licencia): la música de los emotes (`Content/Audio/DanceSounds`),
el pack de pasos `FootstepsMiniPack` y el resto de `Content/Audio/EffectSounds` y `Content/Audio/Music`. Las fuentes Noto Sans
JP/KR/SC/TC (#352) y los datos geográficos del Rally (#112, sección `maps`, con las atribuciones de
`Scripts/terrain_volumes/Variants/*/CREDITS.txt`) se añaden en sus PR.

## Código

| Pieza | Archivo |
|---|---|
| Lectura del JSON, títulos y papeles traducidos | `UI/Credits/TN_CreditsData.*` (`TNCredits::LoadFile`, `ParseJson`, `SectionTitle`, `RoleName`) |
| Pantalla y página | `UI/Credits/TN_CreditsWidget.*` (`UTN_CreditsWidget::OpenOver`, `CreatePage`, `AddMenuButton`) |
| Botón del menú principal | `UMP_MainMenuWidget::NativeConstruct` y `OnCreditsClicked` |
| Página del menú de pausa | `ETNPausePage::Credits` en `UTN_PauseMenuWidget` |
| Pruebas | `Tortunabo.UI.Credits.File` y `Tortunabo.UI.Credits.Parse` (`Private/Tests/TN_CreditsTest.cpp`) |

**Controles**: la lista no tiene filas enfocables; el foco se queda en la pantalla, que se desplaza con flechas, W y S, cruceta,
los dos sticks (continuo), Re Pág y Av Pág o LB y RB, e Inicio y Fin; la rueda del ratón mueve la lista. Escape, B o Retroceso
vuelven (en la pausa, a la portada; en el menú principal, al menú con el foco en «Créditos»).

**Depuración** (no en Shipping): `TN.Credits.Open [Desplazamiento] [Ruta de captura]` pulsa el botón «Créditos» del menú que
esté en pantalla (o abre la pantalla encima de lo que haya), desplaza la lista y, con una ruta, guarda una captura con la
interfaz.
