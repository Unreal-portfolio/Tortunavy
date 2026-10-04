# Menú de pausa y ajustes

Tortunavy tiene un menú de pausa para todos los modos (lobby, mapa procedural, carrera en la playa y nivel de solo
terreno). No pausa nada: la partida es en red y sigue en marcha, pero tu tortuga se queda quieta mientras lo miras. Desde
él se vuelve a la partida, se cambian los ajustes (gráficos, sonido, voz y micrófono, controles y accesibilidad), se
cambian las teclas y los botones del mando y se sale (al lobby, al menú principal o al escritorio). Todo es código: la interfaz se monta en C++
con el estilo del HUD (`TN_HUDArt`, `TN_HUDStyle`, `TN_ShopArt`) y los ajustes se aplican sin assets nuevos.

## Piezas

| Pieza | Archivo | Qué hace |
|---|---|---|
| `UTN_GameSettingsSubsystem` | `Settings/TN_GameSettingsSubsystem.*` | `UGameInstanceSubsystem` + `FTickableGameObject`. Carga, aplica y guarda los ajustes; teclas del jugador (copia de `IMC_Player`); mete la tecla del menú en el PlayerController; abre y cierra el menú; getters de cámara para cualquier cámara (el espectador incluido). |
| `FTNKeyBinding`, `ETNRebindResult` | `Settings/TN_GameSettingsSubsystem.h` | Una fila de la lista de controles (acción o dirección, tecla de teclado y ratón y botón del mando, de ahora y de serie) y el resultado de cambiarla. |
| `UTN_SettingsFovModifier` | `Settings/TN_GameSettingsSubsystem.h` | Modificador de cámara (prioridad 250) que suma el campo de visión del jugador cuando mira a otra tortuga (espectador). |
| `FTNGameSettings`, `UTN_SettingsSaveGame` | `Settings/TN_SettingsSaveGame.h` | Los ajustes que no son de `UGameUserSettings` y su ranura de guardado (versión 3: teclas, micrófono, interfaz, idioma y ojo de pez). |
| `UTN_LanguageSettings`, `TNLanguage` | `Settings/TN_LanguageSettings.*` | La lista de idiomas (editable en `Config/DefaultGame.ini`, sin tocar código), el idioma del sistema en términos de esa lista y el cambio en caliente de la cultura. Ver [Localización](Localizacion.md). |
| `TNHUDFonts` | `Private/UI/HUD/TN_HUDFonts.*` | La fuente compuesta de la interfaz (`TNHUDStyle::Font`): la del motor más una fuente de reserva por idioma cuando su archivo está en `Content/Slate/Fonts`. |
| `UTN_PauseMenuWidget` | `UI/Pause/TN_PauseMenuWidget.*` | El menú: cabecera, portada, ajustes en cinco pestañas, página de controles (con el cambio de teclas), avisos y cuadro de confirmación. También sale desde el menú principal (ver «Ajustes desde el menú principal»). |
| Botón «Ajustes» del menú principal | `UI/Menu/MP_MainMenuWidget.*`, `UTN_GameSettingsSubsystem::OpenMainMenuSettings` | Un botón hecho en código junto a los del Blueprint que abre este mismo menú. |
| `UTN_PauseRow` | `UI/Pause/TN_PauseMenuWidget.*` | Fila enfocable: botón, deslizador, lista de opciones, texto, medidor o tecla. Es `Visible` (no `SelfHitTestInvisible`, el de serie de un `UUserWidget`): la navegación de Slate solo llega a lo que se puede tocar. |
| `UTN_FpsCounterWidget` | `UI/Pause/TN_PauseMenuWidget.*` | Contador de FPS (ajuste «Mostrar FPS»). |
| `UTN_TalkersWidget` | `UI/Pause/TN_PauseMenuWidget.*` | «Quién habla»: los nombres de quien se oye hablar por voz, a la derecha (accesibilidad). |
| `TNPauseArt` | `Private/UI/Pause/TN_PauseArt.h` | Iconos pintados en código: los de los botones de la portada, altavoz y micrófono (tachados si están silenciados) y la corona del anfitrión. |
| `UProximityVoiceComponent` | `Voice/ProximityVoiceComponent.*` | Añadido: `GetMicLevel`, `IsCapturing`, `SetTransmitEnabled` (medidor, silenciarse y pulsar para hablar) y el micrófono elegido (`SetPreferredCaptureDevice`, `GetCaptureDevices`, `GetOpenCaptureDevice`). |
| `FTNVoiceDeviceCapture` | `Private/Voice/TN_VoiceDeviceCapture.h` | Captura de un micrófono concreto (la del motor, `FAudioCaptureSynth`, solo abre el predeterminado). |
| `ATN_RunGameMode::ReturnToLobbyNow` | `Game/TN_RunGameMode.h` | Entrada pública para que el anfitrión lleve a todos al lobby (la misma vuelta que al acabar la ronda). |

No hace falta ningún módulo nuevo en `Tortunabo.Build.cs` (UMG, Slate, EnhancedInput, AudioMixer, AudioCaptureCore,
OnlineSubsystem y Engine ya estaban; `DeveloperSettings` y `GameplayTags` llegan por Engine).

## Abrir y cerrar

- **Escape** en el juego empaquetado o en standalone; **Tabulador** en el editor, donde Escape corta la partida (en el
  editor también vale Escape si le llega al juego); **Start** (Menú) del mando. La tecla y el botón se cambian en la
  página de controles («Abrir y cerrar este menú»); Escape vale siempre.
- No es el PlayerController quien lo escucha: `UTN_GameSettingsSubsystem` mete un `UInputComponent` propio (prioridad 100)
  en la pila de entrada del `AMP_GamePlayerController` local con `PushInputComponent`. Así vale jugando y de espectador,
  no hay que tocar el PlayerController y los viajes sin cortes lo conservan (tras uno con corte se vuelve a meter). En el
  menú principal (`AMP_MenuPlayerController`) Escape no abre nada: los mismos ajustes salen del botón «Ajustes» (ver «Ajustes
  desde el menú principal»).
- **No se abre** si ya hay otra interfaz con el ratón a la vista (tienda, probador, general, ruedas de bailes y frases,
  campeón de la carrera...: esa manda y se cierra con su propio Escape), con la pantalla de carga del huevo a la vista
  (también el «¡ADELANTE!») o durante un viaje. Encima del recuento de la carrera sí (no tiene ratón ni teclas).
- **Se cierra** con Tabulador, Start o la tecla elegida para el menú (del todo), Escape, B o Retroceso (atrás: en la
  portada, cierra) o «Continuar».
- **Mientras está abierto**: modo de entrada interfaz y juego (`FInputModeGameAndUI`) con cursor, la tortuga quieta
  (`SetIgnoreMoveInput` y `SetIgnoreLookInput`, contados para devolverlos igual) y se sueltan las teclas que hubiera
  pulsadas (`FlushPressedKeys`, para que no siga corriendo). Las teclas se quedan en el menú (no salta, no se mete en el
  caparazón, no abre la tienda) salvo la consola y la tecla de pulsar para hablar; el stick izquierdo mueve el foco y
  los gatillos y el stick derecho no llegan al juego. Si algo le quita el cursor o el foco (reaparecer, cambiar de
  ventana), lo recupera solo.
- **Al cerrarse** (también si un viaje lo quita de la pantalla) devuelve la entrada: modo juego sin cursor o, si mientras
  tanto ha salido otra pantalla que se puede pulsar (p. ej. el campeón de la carrera), interfaz y juego con cursor.
- Capa 60 del viewport: por encima del HUD (4-10), las pantallas de la carrera (20-21), las ruedas (30-31) y la tienda
  (40); por debajo de la pantalla de carga (20000). El contador de FPS va en la 70 y «Quién habla» en la 55.

## Qué hay

**Cabecera**: cinta «PAUSA» sobre un cartel azul marino con el mapa o modo (lobby del castillo o del cuartel; carrera
en la playa con su ronda y las conchas para ganar; cooperativo con ronda, dificultad y semilla del mapa; 2 contra 2;
solo terreno; carrera clásica), la sala (su nombre, de quién es, pública o privada con su código, «3/4 tortugas» y
«cerrada» si lo está: [Salas](Salas.md); sin sala, la sesión como antes; sin sesión, si eres el anfitrión, un invitado
o una partida local) y los jugadores con su cara, su
nombre, «Tú», «Anfitrión» (corona) o su ping, y su icono de voz: micrófono para ti y altavoz para los demás, que late
cuando habla y sale tachado si está silenciado. Un invitado ve también su propio ping («Tú · 42 ms»); el anfitrión no
tiene ping contra nadie y una partida local tampoco (`TNPlayerRowRules`, #256). El ping se lee al hacer la lista (al abrir
el menú o cuando alguien entra o sale), no se refresca mientras está abierto.

**Portada**: Continuar, Ajustes, Controles, Créditos ([Créditos](Creditos.md)), Sala (en red), Volver al lobby, Menú principal (anfitrión) o Salir de la
partida (invitado) y Salir al escritorio. Abajo, la ayuda de la opción enfocada y los atajos.

**Sala** (partidas en red; ver [Salas](Salas.md)): nombre, pública o privada, el código (se copia), «Entrada: Abierta /
Cerrada» (el anfitrión la cambia), «Invitar a amigos de Steam» y las tortugas de la sala; el anfitrión tiene un «⋮» en
cada una para expulsarla (con confirmación).

| Opción | Anfitrión | Invitado |
|---|---|---|
| Volver al lobby | Solo en una partida (modos de `ATN_RunGameMode`: Run, mapa procedural, carrera). Confirma y llama a `ReturnToLobbyNow`: peones fuera y viaje sin cortes al lobby del que se salió; los invitados van detrás. | Al grupo lo mueve el anfitrión: el cuadro lo explica y, si confirma, sale él solo de la sesión al menú principal (lo mismo que «Salir de la partida»). |
| Menú principal / Salir de la partida | «Menú principal»: confirma y `UMP_GameInstance::HandleReturnToMenu` cierra la sesión para todos. | «Salir de la partida»: confirma y sale él solo de la sesión al menú; los demás siguen. |
| Salir al escritorio | Confirma (avisa de que la partida se acaba para todos si hay invitados) y `QuitGame`. | Confirma y `QuitGame`. |

En el lobby no sale «Volver al lobby» (ya se está) y en el nivel de solo terreno tampoco (no sale de un lobby).

**Controles**: las teclas del juego leídas de `IMC_Player` en ejecución (siempre las de verdad), con teclado y ratón y
mando por separado y los nombres en español, y se cambian ahí mismo (ver Controles). Debajo, hablar y el menú de pausa
(también se cambian), «Restablecer todos los controles» y lo que no se cambia: mirar y elegir en la rueda (ratón y
sticks), los controles del espectador y moverse por los menús.

**Moverse por el menú**: flechas, WASD, cruceta o stick izquierdo; Intro, Espacio o A pulsan; izquierda y derecha (A y
D) cambian los deslizadores y las listas y, en la barra de pestañas, abren la de al lado (arriba desde la lista lleva a
la pestaña abierta); Q y E o LB y RB cambian de pestaña; el ratón enfoca al pasar por encima,
pulsa con clic y arrastra los deslizadores; la rueda desplaza las listas. Cada fila suena al enfocarla («pom») y al
pulsar («plin»): los sonidos de las conchas (`UTN_ScoreShellSynthComponent`); al mover un deslizador el «pom» sube de
tono con el valor.

**Medidas**: todo está pensado a 1080 p de referencia; la escala de la interfaz del motor lo encoge a 720 p (cabe entero en
1280×720) y lo agranda en 4K. Además va dentro de un lienzo de 1920 × 1080 en una `UScaleBox` («encajar», solo hacia
abajo): si la pantalla en unidades de interfaz es más pequeña (tamaño de la interfaz grande, ventana 4:3 o 16:10) el menú
se encoge entero y sigue cabiendo; con la interfaz pequeña, se queda más pequeño y centrado. El velo y el del cuadro de
confirmación van a pantalla completa.

**Avisos**: una línea dorada encima de la ayuda cuenta lo que acaba de pasar (qué tecla se ha puesto, a qué fila se le ha
quitado, el micrófono nuevo...) y se apaga sola.

## Ajustes desde el menú principal

Hasta la fase 2 de la localización el idioma (y el resto de ajustes) solo se cambiaba dentro del lobby o de una partida. El menú
principal tiene ahora un botón **«Ajustes»**, entre «Unirse» y «Salir», que abre las mismas páginas de ajustes que el menú de
pausa, con teclado, ratón y mando.

- **El botón** (`UMP_MainMenuWidget::BuildSettingsButton`, en `NativeConstruct`). `WBP_MainMenuWidget` no cambia: los tres
  botones del Blueprint (`HostButton`, `FindButton`, `QuitButton`) están en una caja vertical, y el botón nuevo copia de
  «Unirse» el estilo del botón, sus colores, su tipo de pulsación, el estilo del rótulo (fuente, color, sombra) y la
  colocación en la caja (margen, tamaño, alineación). UMG solo sabe insertar en su lista, no en la caja de Slate
  (`InsertChildAt` no reordena `SVerticalBox`), así que lo que va desde «Salir» en adelante se quita y se vuelve a poner detrás
  del botón nuevo, con su colocación. Si los botones no estuvieran en una caja, el botón se pone abajo en el centro del lienzo.
  El rótulo es `NSLOCTEXT("TNRooms", "MenuSettings", "Ajustes")`.
- **Qué abre**: `UTN_GameSettingsSubsystem::OpenMainMenuSettings(PC)` (solo con el controlador del menú principal, local y con
  el menú de pausa cerrado) crea el mismo `UTN_PauseMenuWidget` en la capa 60 y le da la entrada. Es el mismo objeto que
  usa `IsPauseMenuOpen`/`ClosePauseMenu`. El idioma se elige en Ajustes > Juego > «Idioma / Language».
- **El menú principal mientras tanto**: se queda a la vista detrás del velo, pero en `HitTestInvisible` (sin clics ni foco: el mando
  no se escapa a sus botones, como con las pantallas de salas). `NativeTick` mira `IsPauseMenuOpen`; al cerrarse devuelve la
  visibilidad, vuelve a leer el saludo (por si ha cambiado el idioma) y enfoca «Ajustes». Los rótulos de los botones son
  `FText` y se traducen solos.
- **La entrada**: el menú principal usa solo interfaz con el cursor (`FInputModeUIOnly`, `AMP_MenuPlayerController`). Al
  cerrarse, `NotifyPauseMenuClosed` la devuelve a ese modo (y no al de juego, que escondería el cursor).
- **Se guarda igual que en la partida** (`TN_Settings.sav` al cerrar; el idioma, el volumen, las teclas...).
- **Estado del modo «menú principal» dentro de `UTN_PauseMenuWidget`** (portada con «Ajustes», «Controles» y «Volver» en vez de
  Sala, Volver al lobby, Menú principal y Salir al escritorio; cabecera «AJUSTES» sin mapa, sala ni jugadores; pie con «Volver»; el
  widget lo reconoce por el controlador que lo crea): **pendiente** de que se liberen los archivos reservados por el tutorial
  (29-09-2026). Mientras tanto el botón abre el menú de pausa completo, que funciona.

## Ajustes

Se aplican al momento. Los propios (`FTNGameSettings`) se guardan en `Saved/SaveGames/TN_Settings.sav` al cerrar el menú
(o a los 3 s del último cambio con el menú cerrado) y se cargan al crearse la GameInstance, es decir, al arrancar el
juego. Los gráficos van en `UGameUserSettings` (`Saved/Config/<Plataforma>/GameUserSettings.ini`), que el motor aplica
solo al arrancar.

### Gráficos

| Ajuste | Cómo se aplica |
|---|---|
| Modo de ventana (pantalla completa, sin bordes, ventana) | `SetFullscreenMode` + `ApplyResolutionSettings`; sin bordes va a la resolución del escritorio. Sale un cuadro «¿Mantener esta pantalla?» y, si no se confirma en 12 s, se deshace (`RevertVideoMode`). Una resolución sin confirmar no se guarda. |
| Resolución | Pantalla completa: `GetSupportedFullscreenResolutions`; ventana: `GetConvenientWindowedResolutions`. Igual que el modo, con confirmación. |
| Escala de resolución | `SetResolutionScaleValueEx` (del mínimo al máximo que da `GetResolutionScaleInformationEx`). |
| Sincronización vertical | `SetVSyncEnabled`. |
| Límite de fotogramas | `SetFrameRateLimit`: 30, 60, 90, 120, 144, 165, 240 o sin límite. |
| Brillo | `GEngine->DisplayGamma` (la gamma que usa el tonemapper): 50 % es la de serie, cada extremo la mueve 0,7. |
| Mostrar FPS | `UTN_FpsCounterWidget` abajo a la derecha: FPS y el peor fotograma del último medio segundo en ms; verde, dorado o coral según vaya. Se vuelve a poner tras cada viaje. |
| Calidad general (Baja, Media, Alta, Épica) | `SetOverallScalabilityLevel`; si luego se cambia una parte, sale «Personalizada» (y «Cine» si algo está en 4). |
| Sombras, efectos, vegetación, distancia de visión, antialiasing, texturas, postprocesado, iluminación global y reflejos | `Set...Quality` de cada una. |
| Calidad recomendada | `RunHardwareBenchmark` + `ApplyHardwareBenchmarkResults` (la imagen se congela un momento). |

Cada cambio llama a `ApplyNonResolutionSettings` y se guarda con `SaveSettings` al cerrar el menú.

### Sonido

| Ajuste | Cómo se aplica |
|---|---|
| General | Volumen principal del dispositivo de audio del mundo (`FAudioDevice::SetTransientPrimaryVolume`): todo, voces incluidas. |
| Silenciar sin el foco de la ventana | El mismo volumen principal a 0 mientras la aplicación no está activa (`FSlateApplication::IsActive`); vuelve al volver. |
| Música | Clase de sonido `TN_Music` creada en ejecución (`Properties.Volume`). |
| Ambiente | Clase de sonido `TN_Ambient`. |
| Efectos | Mezcla `TN_EffectsMix` creada en ejecución y empujada en el dispositivo de cada mundo, con `SetSoundMixClassOverride` sobre la clase de sonido por defecto del motor (y sus hijas). |

**Qué cae en cada categoría.** El proyecto sintetiza casi todo en código y no hay clases de sonido en el contenido, así
que `UTN_GameSettingsSubsystem` reparte cada fotograma los sonidos generados en código (los `UAudioComponent` cuyo
sonido no es un asset: los `USynthSound` de los sintetizadores y las ondas procedurales de la voz) poniendo
`SoundClassObject` en su sonido; el dispositivo lee la clase en cada actualización, así que vale aunque ya estén sonando:

| Categoría | Qué |
|---|---|
| Música | `UTN_MusicSynthComponent`: tienda, probador, victoria, derrota, eliminado y las radios 3D del tendero. |
| Ambiente | `UTN_AmbientSynthComponent`: el paisaje sonoro por bioma (`TN_AmbientSoundscape`), cascadas y fuentes puntuales. Además, si el `UTN_AmbientSoundscapeComponent` del PlayerController no trae clase propia, se le pone la de Ambiente en su `SoundClassOverride`, así que sus sonidos de sustitución (assets de `AmbienceData`, hoy ninguno) también van aquí. |
| Voz | La voz de los compañeros (`USoundWaveProcedural` del grupo `SOUNDGROUP_Voice` de `UProximityVoiceComponent`). |
| Efectos | Todo lo demás, que se queda en la clase por defecto: pasos y ruidos de la tortuga (`TN_TurtleFoleyComponent`), trampas y enemigos de la playa, rebuscar, tos de la tormenta, pájaros del mareo, piezas del patio del lobby, conchas y los sonidos de los menús, el huevo de la pantalla de carga y los sonidos de asset (saltos, bailes, lanzar, pisadas en arena). |

Los sintetizadores que se crean más tarde (la música de la tienda al abrirla, la del fin de partida, un paisaje sonoro
nuevo tras un viaje) entran en su categoría en el primer fotograma en que suenan. Si un sintetizador trae su propia
`SoundClass`, `USynthComponent::Start` la pone de sustituta en su componente y esa mandaría: el subsistema la cambia también
por la de su categoría (vale desde que vuelve a empezar; los del proyecto no traen ninguna).

**Qué queda fuera.** Los sonidos de asset con su propia clase de sonido no se tocan (hoy no hay ninguno), los assets
nunca se modifican y, si alguien pone su propia clase en el `SoundClassOverride` del paisaje sonoro, se respeta la suya
(no baja con Ambiente). Un sintetizador nuevo cae solo en Efectos; si es música o ambiente, que herede de
`UTN_MusicSynthComponent` o `UTN_AmbientSynthComponent` o que se añada su clase en `UTN_GameSettingsSubsystem::ClassFor`.
Un sonido 2D de la interfaz que se cree y se acabe dentro del mismo fotograma podría sonar ese fotograma sin su clase (no
pasa con los sintetizadores del proyecto, que viven mucho más).

### Voz

| Ajuste | Cómo se aplica |
|---|---|
| Voz de los compañeros | Clase `TN_Voice` (todas las voces). |
| Voz de cada compañero (0-200 %) y silenciarlo | Multiplicador de volumen del componente de reproducción de su voz (`PlaybackVolume` × el tuyo; 0 si está silenciado), cada fotograma. Además, la voz de un silenciado ni se descodifica ni se reproduce (`UProximityVoiceComponent::PlayRemoteVoice`), y su jugador se saca del peón o, si este no tiene `PlayerState`, del último que tuvo (`GetSpeakerState`, #248). Se guarda por su id de la plataforma (Steam) o, si no hay, por su nombre, así que se recuerda entre partidas. |
| Silenciar mi micrófono | `UProximityVoiceComponent::SetTransmitEnabled(false)`: se sigue capturando (el medidor vive) pero no se envía nada y la tortuga deja de «hablar» en el acto. |
| Modo: voz abierta o pulsar para hablar | Con pulsar para hablar, la salida solo se abre con la tecla pulsada (`IsInputKeyDown`); encima sigue haciendo falta superar el umbral. |
| Tecla y botón para hablar | Una fila de tecla (la misma que en la página de controles): V y cruceta abajo de serie, y cualquier otra que se pulse. Si otra acción la tenía, se cambian entre sí (ver Controles). |
| Micrófono | Lista de los micrófonos activos de Windows (`FAudioCapture::GetCaptureDevicesAvailable`) más «Predeterminado de Windows». Se guarda su id y se abre al **empezar la voz**: al reaparecer, al cambiar de mapa o al volver a abrir el juego. La captura abierta no se cambia en caliente (cerrar una captura WASAPI abierta es lo que cuelga el juego al viajar); el aviso y el medidor («Micro nuevo al reaparecer») lo dicen. Si el elegido ya no está conectado, se usa el predeterminado y la fila dice «No conectado». |
| Sensibilidad | `SpeakingThreshold` del componente propio: 50 % es el umbral de serie del componente (el de su plantilla; 0,01 RMS, -40 dB) y cada extremo lo mueve 20 dB (0 %: hay que hablar diez veces más fuerte; 100 %: se oye hasta lo bajito). |
| Ganancia | `VoiceGain` del componente propio (la de serie de su plantilla × 25-300 %). |
| Nivel del micrófono | Medidor en vivo con `GetMicLevel` (RMS del último bloque, con la ganancia) en dB de -60 a 0, con la raya dorada del umbral; dice «¡Se te oye!», «En silencio», «Silenciado», «Mantén V», «Micro nuevo al reaparecer» o «Sin micrófono». |

Las variables `voice.*` del motor no sirven aquí: son del VOIP del motor, que está apagado (`[Voice] bEnabled=false`);
el proyecto usa su propio `UProximityVoiceComponent`.

**Micrófono elegido, por dentro.** `FAudioCaptureSynth` solo sabe abrir el predeterminado, así que el componente abre el
elegido con `FTNVoiceDeviceCapture`: un `Audio::FAudioCapture` con `DeviceIndex` (el índice de los micrófonos activos,
en el mismo orden que la lista) que mezcla a mono en un búfer con cerrojo, como el del motor. Usa `OpenCaptureStream`
(obsoleta desde 5.3, con los avisos apagados alrededor) porque `OpenAudioCaptureStream` no se exporta del módulo.
Igual que la del motor, nunca se para ni se destruye: al limpiar el componente se suelta (el búfer no pasa de 2 s). El
subsistema pone el id elegido con `UProximityVoiceComponent::SetPreferredCaptureDevice` al arrancar y en cada cambio.

### Controles

| Ajuste | Cómo se aplica |
|---|---|
| Sensibilidad del ratón y del mando (20-300 %) | Escalas de giro del PlayerController (`InputYawScale_DEPRECATED` e `InputPitchScale_DEPRECATED`, que se usan porque `bEnableLegacyInputScales=True` en `DefaultInput.ini`), multiplicando las de su clase; la del ratón o la del mando según el último aparato usado (`UInputDeviceSubsystem::GetMostRecentlyUsedHardwareDevice`). |
| Invertir eje Y (ratón / mando) | La misma escala de cabeceo, en negativo. |

Valen para todo lo que gira con `AddControllerYawInput`/`AddControllerPitchInput`: la tortuga (que además aplica su
`LookSensitivityX/Y`) y la cámara del espectador. Para una cámara que gire a mano con el valor crudo de la acción, el
subsistema da `GetLookSensitivity`, `IsLookYInverted`, `IsUsingGamepad` y `ApplyLookSettings`; `IsCameraShakeEnabled` y
`GetFieldOfViewOffset` para el temblor y el campo de visión (`UTN_GameSettingsSubsystem::Get(this)`). Ojo: si la cámara
ya pasa por `AddYawInput`/`AddPitchInput`, la sensibilidad va aplicada y no hay que multiplicarla otra vez.

La cámara libre del fantasma espectador lee `GetLookSensitivity` e `IsLookYInverted` con el valor crudo (no pasa por las
escalas del PlayerController), así que la sensibilidad se aplica una sola vez también de espectador.

**Cambiar teclas y botones** (página de controles; en Ajustes > Controles, «Cambiar teclas y botones»):

- Cada acción de `IMC_Player` con tecla o botón es una fila; las de ejes (moverse) salen por direcciones: Avanzar,
  Retroceder, Ir a la izquierda e Ir a la derecha (la dirección sale de pasar un 1 por los modificadores de la
  asignación: intercambiar ejes y negar). Moverse con el mando va con el stick izquierdo y esa columna no se cambia;
  mirar y elegir en la rueda (ratón y sticks) tampoco. Además, **hablar** y **abrir y cerrar este menú**.
- **Cambiar**: Intro, Espacio, A o clic en la fila; dice «Pulsa una tecla...» y la primera tecla, botón del ratón o botón
  del mando que se pulse es la nueva (el aparato sale de la tecla: una del teclado va a la columna de teclado y ratón; un
  botón, a la del mando). Esc o Select/Vista lo dejan como estaba; a los 8 s sin nada, también. Un roce del stick no
  cuenta. No valen Escape, la consola, Select/Vista, la rueda ni, en el editor, el Tabulador: lo dice y sigue esperando.
- **Conflictos**: si otra fila del mismo aparato tenía esa tecla, se cambian entre sí («Espacio estaba en «Saltar»: ahora
  «Saltar» va con Mayús izq.»); si la fila no tenía tecla en ese aparato, la otra se queda sin ella y el aviso lo dice.
- **De serie**: Supr, Y del mando o clic derecho en la fila (con el mismo cambio si otra la tenía); «Restablecer todos
  los controles» (con confirmación) las devuelve todas.
- **Se guarda** en `KeyOverrides` de `FTNGameSettings` («IA_Jump#0» teclado, «IA_Jump#1» mando, «IA_Move:Y+#0»...; sin
  entrada = la de serie); hablar y el menú, en `PushToTalkKey`/`PushToTalkPadKey` y `PauseKey`/`PausePadKey`.

Por dentro, sin assets nuevos: con alguna tecla cambiada, el subsistema hace una **copia transitoria de `IMC_Player`**
(`DuplicateObject`, con sus modificadores y disparadores), le cambia la tecla a la asignación de cada fila (o le añade una
si la acción no tenía en ese aparato) y, **cada fotograma**, si ve `IMC_Player` (o una copia vieja) puesto en el
`UEnhancedInputLocalPlayerSubsystem` del jugador local, lo quita y pone la copia con la misma prioridad (sin esperar a
soltar las teclas). Así se resuelve el `ClearAllMappings` + `AddMappingContext(IMC_Player)` que hace la tortuga al
poseerse sin tocar `ATortugaCharacter`, y vale en el lobby, en la partida y de espectador (el contexto sigue puesto). Las
ruedas del PlayerController y las indicaciones del HUD que preguntan por las teclas de una acción
(`QueryKeysMappedToAction`) ven las nuevas solas. Quien añada `IMC_Player` por su cuenta puede pedir el bueno con
`UTN_GameSettingsSubsystem::ResolveMappingContext` (si no, se cambia al fotograma siguiente). Los controles del
fantasma espectador (C, flechas, LB/RB, rueda...) y la rueda y Re Pág/Av Pág del PlayerController son teclas fijas en
código: se enseñan, no se cambian.

### Juego y accesibilidad

| Ajuste | Cómo se aplica |
|---|---|
| Temblor de cámara | Apagado, el subsistema desactiva cada fotograma (`DisableModifier`) los modificadores del `PlayerCameraManager` cuya clase se llama «...Shake...»: el de serie del motor (`UCameraModifier_CameraShake`) y el de la playa (`UTN_BeachCameraShake`: quads, cangrejo, tormenta...), sin depender de él. Encendido otra vez, los reactiva. |
| Campo de visión (-15 a +20°) | `CameraFOVDefault` y `CameraFOVSprint` de la tortuga local = los de su clase + el desplazamiento (al correr se abre lo mismo que antes). Mirando a otra tortuga (espectador, cámara fija o libre del fantasma), `UTN_SettingsFovModifier` suma el desplazamiento al final (la cámara de la tortuga ajena no lo lleva). Las cámaras de escena (tienda, probador) no se tocan. Se enseña en grados. |
| Tamaño de la interfaz (75-130 %) | `UUserInterfaceSettings::ApplicationScale` = el que había × el elegido. Es la escala extra que el motor multiplica a la escala DPI del viewport del juego (`SGameLayerManager`), así que cambia el HUD y los menús de UMG al momento y no la interfaz del editor. El menú de pausa se encoge si no cabe. |
| Filtro para daltónicos (deuteranopía, protanopía, tritanopía) e intensidad | `UWidgetBlueprintLibrary::SetColorVisionDeficiencyType` en modo corrección: Slate lo aplica a la imagen final de la ventana, así que corrige a la vez el mapa, el HUD y sus marcadores (no hace falta tocar sus colores uno a uno). |
| Quién habla (texto) | `UTN_TalkersWidget` a la derecha: «X habla» (o «Tú hablas») por cada jugador que se oye hablar (`IsHeardSpeaking`), sin los silenciados. Para jugar sin sonido o si se oye mal. Los mensajes de las frases rápidas ya son de texto. |
| Idioma (primera fila de la pestaña, con el título «IDIOMA / LANGUAGE» para que se encuentre aunque no se lea el idioma puesto) | Lista con los idiomas de `UTN_LanguageSettings`, cada uno con su nombre escrito en su idioma (Español, English, Français, Deutsch, Italiano, Português, Русский, Polski, Türkçe, 日本語, 한국어, 简体中文, 繁體中文). Se pone en caliente (`UTN_GameSettingsSubsystem::SetLanguage` → `TNLanguage::Apply`: en el juego, `SetCurrentCulture` sin guardarla en la configuración del motor; en el editor, solo los textos del juego con la previsualización del idioma del juego, para no cambiar el idioma del editor) y se guarda en `Language`. Sin elegir, al arrancar se usa el idioma del sistema si está en la lista (es-MX → es-ES, pt-PT → pt-BR, zh-TW → zh-Hant...) y, si no, el español; se aplica al crearse la GameInstance, antes de que salga ningún menú. «Restablecer esta pestaña» vuelve a «sin elegir». El menú refresca lo que compone como texto (cabecera, pie, ayuda); los rótulos son `FText` y se traducen solos. |
| Ojo de pez leve (desactivado de serie, #634) | Proyección Panini del motor: `r.LensDistortion.Panini.D` (en UE 5.6 los cvars son `r.LensDistortion.Panini.*`, no `r.Upscale.Panini.*`). Se aplica sobre la imagen del mundo, en el pase de escalado o dentro del TSR (`r.TSR.LensDistortion`), así que el HUD de UMG, que se pinta después, no se deforma. Valor suave (`TN.Fisheye.D` = 0,55 con todo encendido; con el campo de visión de la tortuga, 72-82°, la proyección Panini entera solo comprime el borde un 10 %), que se enciende y apaga en 1,2 s y se afloja con el campo de visión (`FisheyeDistanceForFov`: al correr, 82°, baja hasta ~0,35 para que el borde se comprima igual que en reposo). Ajustable en vivo con `TN.Fisheye.D` y `TN.Fisheye.S` (compresión vertical). Al acabar la partida en el editor, los cvars vuelven a como estaban. Las capturas de escena (probador, podio) también lo llevan: es un cvar de todo el proceso. |

| Modo VR (sección «REALIDAD VIRTUAL») | `FTNGameSettings::VRMode`: Automático (gafas si el juego arranca con ellas: `-vr`, «VR Preview», la build de Quest), Desactivado (con gafas, la pantalla plana) o Simulado sin gafas (el modo VR en la ventana, con el ratón). Lo lee `UTN_VRSubsystem` cada fotograma: cambia en el acto. La consola `TN.VR` y `-vrsim`/`-novr` mandan sobre él. Ver `Docs/Modo_VR.md`. |
| Giro en VR | `FTNGameSettings::VRTurn`: a pasos de 30° (de serie), de 45° o suave, con el stick derecho de las gafas. |

Cada pestaña (menos la de gráficos) tiene «Restablecer». En gráficos, «Calidad recomendada». En Juego, además,
**«Restablecer todos los ajustes»** (con confirmación): sonido, voz, micrófono, controles con sus teclas, juego, brillo y
FPS a los de serie; la calidad gráfica y la pantalla no se tocan.

## En el editor

- La resolución y el modo de ventana salen apagados (la ventana es la del editor).
- Al acabar la partida se devuelven la calidad gráfica (`Scalability::SetQualityLevels`), el límite de fotogramas, la
  sincronización vertical, la gamma, el filtro de color y el volumen principal que tenía el editor: se pueden probar
  los gráficos en PIE sin que el editor se quede así. Los gráficos del juego (`GameUserSettings.ini` del editor) se
  guardan igual.
- Con varios jugadores en un proceso, cada ventana tiene su GameInstance, su subsistema y su menú; los ajustes propios
  comparten archivo (el último que guarda manda) y la gamma, la escala de la interfaz y el filtro de color son del
  proceso entero. Lo que había antes se apunta con el primer subsistema y se devuelve al quitarse el último (antes, el
  segundo jugador tomaba como base el brillo que ya había puesto el primero y lo sumaba dos veces).
- El tamaño de la interfaz se nota también en el diseñador de UMG mientras dura la partida; al acabar vuelve.

## Fuera y por qué

- **Cambiar el micrófono en caliente**: se cambia al reaparecer o al viajar (ver Voz).
- **Salida de audio (auriculares, altavoces)**: el dispositivo de audio del motor se abre al arrancar y cambiarlo es
  rehacer el mezclador; se usa la salida predeterminada de Windows.
- **Vibración del mando**: el juego no usa vibración (no hay `ForceFeedback`).
- **Idioma en el menú principal**: se cambia con el botón «Ajustes» del menú principal (fase 2 de la localización); el del sistema se aplica desde el primer fotograma.
- **Subtítulos**: no hay voces grabadas ni diálogos; la voz de los jugadores tiene «Quién habla» y las frases rápidas ya
  son texto.
- **Cambiar las teclas del espectador**: son fijas en código (`ATN_SpectatorGhost`); se enseñan en la lista.

## Revisión de lo que ya había

- Volúmenes por categoría: se reparten cada fotograma, así que valen para los sintetizadores creados después de abrir o
  cerrar el menú y tras un viaje (en el mundo nuevo se vuelven a apuntar las clases y a empujar la mezcla); arreglado el
  caso de un sintetizador con clase propia (ver Sonido).
- Voz por compañero: multiplicador del componente de reproducción cada fotograma, por clave estable (id de la
  plataforma); vale tras un viaje (componentes nuevos) y se guarda.
- Umbral y ganancia del micrófono: ahora parten de los de la plantilla del componente de voz (antes, de valores fijos).
- Sensibilidad del espectador: la cámara libre del fantasma lee los getters con el valor crudo; la fija es la de la
  tortuga seguida. Campo de visión del espectador: añadido (antes no se aplicaba mirando a otros).
- PIE con varios jugadores: la base del brillo, la escala de la interfaz y lo que se devuelve al editor se apuntan una
  vez por proceso (arreglado el brillo sumado dos veces).
- Viajes: la entrada del menú, el contador de FPS, «Quién habla», el temblor, el campo de visión y las teclas se vuelven
  a poner en el PlayerController, la tortuga y el viewport nuevos.
- Cerrar y abrir el juego: todo sale de `TN_Settings.sav` (y `GameUserSettings.ini`) al crearse la GameInstance, antes de
  que empiece la voz, así que el micrófono elegido y las teclas valen desde el primer momento.

## Mando: B / Círculo para meterse en el caparazón

Meterse o salir del caparazón (`IA_Shell`) solo iba con Ctrl izquierdo. Ahora también va con **B / Círculo** del mando
(`Gamepad_FaceButton_Right`):

- **Qué hacía B en juego**: nada. `IMC_Player` no la usaba (solo A/Cruz, X/Cuadrado, Y/Triángulo, los hombros, los gatillos y
  los sticks) y ningún otro asset ni código de juego la lee; B solo era «volver» en los menús (pausa, tienda, briefing, salas,
  campeón de la carrera). No había un choque de acciones.
- **Dónde se pone**: en `IMC_Player`, con `Scripts/imc_player_shell_b.py` (se ejecuta una vez desde el editor abierto: mira lo que
  hay, se niega si B es de otra acción y guarda el asset). Ejecutado en #637: el asset ya la trae. Mientras el asset no la traiga, `UTN_GameSettingsSubsystem` la pone como
  tecla de serie de `IA_Shell` (`PendingCodeDefaults`: la copia transitoria de `IMC_Player` la añade), así que ya vale sin ejecutar
  el script; con el asset al día esa lista queda vacía y manda el asset. La lista de controles («Meterse en el caparazón») y el
  cambio de teclas la enseñan sola en la columna del mando: sale de `IMC_Player` en ejecución.
- **El choque de verdad: B cierra un menú y, al mantenerla, mete en el caparazón.** Con el menú a la vista, el menú se come la
  pulsación; si la tecla sigue apretada cuando el menú desaparece, el motor manda repeticiones de esa tecla ya al juego y
  Enhanced Input las toma por una pulsación nueva (`Input.AutoReconcilePressedEventsOnFirstRepeat`): la tortuga se metería en el
  caparazón sin que nadie lo pidiera. Lo mismo pasa con la A/Cruz al pulsar «Continuar» (saltaba) y con Intro o Espacio.
  `UTN_GameSettingsSubsystem` registra un procesador de entrada de Slate (`FTNHeldKeyGuard`, el primero en ver cada tecla): si una
  tecla de una acción del juego se pulsa con un menú a la vista (el de pausa u otra interfaz con el cursor: tienda, probador,
  resumen de la carrera...), sus repeticiones se descartan cuando el menú ya no está y hasta que se suelta. Con el menú a la vista
  pasan todas (mantener una flecha o A/D sigue repitiendo en las listas). Sirve para todas las pantallas, no solo esta.

## Retroceso ya no saca de la partida

`IA_Quit` estaba en `IMC_Player` con Retroceso y `BP_GamePlayerController` lo usa como `ReturnToMenuAction`: pulsarla
jugando volvía al menú sin preguntar y, en el anfitrión, cerraba la partida para todos. Se quitó esa asignación de
`IMC_Player` (28-09-2026): salir va ahora por el menú de pausa, que pide confirmación. Con el menú abierto, Retroceso
solo va hacia atrás.

## Pruebas

1. PIE con 1 jugador en el lobby: Tabulador abre y cierra; Start del mando también; la tortuga no se mueve ni gira la
   cámara con el menú abierto y vuelve a moverse al cerrarlo; Escape dentro del menú va hacia atrás y cierra desde la
   portada.
2. Standalone (o el juego empaquetado): Escape abre y cierra.
3. Tienda, probador y general: con uno abierto, Escape y Tabulador siguen siendo suyos; el menú de pausa no sale.
4. Ruedas de bailes y frases: con la rueda abierta no sale.
5. Mapa procedural, carrera y solo terreno: se abre; en la carrera sale la ronda y las conchas; en el cooperativo, la
   semilla; en solo terreno, sin «Volver al lobby».
6. Espectador (fantasma tras caer o tras la meta): se abre y se cierra; con el menú abierto la cámara libre no gira;
   al cerrarlo gira con la sensibilidad elegida (ratón y mando, invertir Y); campo de visión a +20° mirando a otra
   tortuga (fija y libre) se nota.
7. Sonido: bajar Música con la tienda cerrada, abrirla después (la canción ya suena baja); Ambiente en el mapa
   procedural; Efectos (pasos, saltos, conchas y los «pom» del menú); General; «Silenciar sin el foco»: cambiar a otro
   programa y volver.
8. Voz con 2 jugadores (PIE, dos ventanas, «Play As Listen Server», o dos standalone): el medidor se mueve al hablar y
   la raya dorada con la sensibilidad; «Silenciar mi micrófono»; pulsar para hablar con V; «Voz de X» al 0 % o
   «Silenciar a X» en el otro; los iconos de voz de la cabecera laten; «Quién habla» enseña «X habla» en el otro.
9. Micrófono: con dos micrófonos, elegir el otro en Voz (aviso «se abre al reaparecer»; el medidor dice «Micro nuevo al
   reaparecer»); viajar lobby → partida (o reaparecer): el medidor reacciona al micrófono nuevo y el otro jugador te oye
   por él. Desconectarlo y volver a entrar: fila «No conectado», se usa el predeterminado.
10. Teclas: en Controles, Saltar → pulsar Intro y luego F: salta con F (y el HUD enseña F si indica esa acción);
    Correr → Espacio: el aviso dice que Espacio estaba en Saltar y Saltar pasa a Mayús izq.; Supr en Correr la
    devuelve; con el mando, Saltar → X y probarlo; Avanzar → Flecha arriba; Esc a media captura no cambia nada;
    Escape y (en el editor) Tab no se aceptan. Hablar → B (teclado) y probarlo con pulsar para hablar. Menú → P: P abre
    y cierra; Escape sigue abriéndolo. «Restablecer todos los controles».
11. Teclas y viajes: con teclas cambiadas, lobby → partida → lobby (se sigue saltando con la nueva tras cada
    posesión), de espectador las ruedas siguen con sus teclas, y cerrar y abrir el juego.
12. Tamaño de la interfaz al 130 % y al 75 % en el lobby y en la partida: el HUD cambia al momento; el menú sigue
    cabiendo entero a 1280×720; el editor no cambia; al acabar PIE vuelve a su tamaño.
13. Juego: temblor de cámara apagado en la carrera (quads y cangrejo sin sacudida); campo de visión; filtro para
    daltónicos (el HUD y los marcadores también cambian); «Restablecer todos los ajustes» (confirma y todo vuelve,
    menos los gráficos).
14. Gráficos: calidad general Baja y Épica (se nota), una parte suelta (sale «Personalizada»), escala de resolución,
    límite de 30 FPS, brillo (en PIE con 2 jugadores, el mismo en las dos ventanas), Mostrar FPS (se mantiene tras un
    viaje). Fuera del editor: pantalla completa y otra resolución, dejar pasar los 12 s (se deshace) y luego «Mantener».
15. Cerrar el juego y abrirlo: todo lo anterior sigue igual (volúmenes, voz por compañero, micrófono, teclas,
    interfaz, accesibilidad, gráficos).
16. Anfitrión en una partida con 2 jugadores: «Volver al lobby» lleva a los dos al lobby; en el invitado, «Volver al
    lobby» y «Salir de la partida» avisan y lo sacan solo a él al menú principal (el anfitrión sigue).
17. «Menú principal» en el anfitrión cierra la partida para los dos; «Salir al escritorio» cierra el juego.
18. Abrir el menú con el recuento de la carrera en pantalla y cerrar cuando sale el campeón: al cerrar, el cursor sigue
    para pulsar sus botones.
19. Idioma: Ajustes > Juego > «Idioma / Language» (primera fila). Cambiar a English y a Deutsch: los rótulos del menú, la cabecera
    y la ayuda cambian al momento (hasta que no haya traducciones, los textos siguen en español: es lo normal); el nombre de la sala
    sale en el idioma elegido, no en el de Windows. Cerrar y abrir el juego: sigue el elegido. «Restablecer esta pestaña»: vuelve al
    del sistema. Con Windows en español y sin elegir: español. En la fila se leen los trece nombres (日本語, 한국어, Русский...).
20. Ojo de pez leve: viene apagado (#634); encendido (Ajustes > Juego > «Ojo de pez leve» = Sí) se nota en los bordes (los árboles y la orilla se curvan un poco) y no en el HUD ni en el
    menú. Ajustes > Juego > «Ojo de pez leve» = No: se vuelve a apagar en poco más de un segundo. Correr (el campo de visión se abre): el
    borde no salta ni marea. `TN.Fisheye.D 0.3` / `0.8` para comparar intensidades. Con el editor, al acabar PIE, el visor normal
    no queda deformado.
21. B / Círculo: con el mando, B mete en el caparazón y saca (y Ctrl izquierdo sigue valiendo). Abrir el menú con Start, cerrar con B
    manteniéndola un segundo: la tortuga NO se mete en el caparazón. Igual con A al pulsar «Continuar» (no salta) y en la
    tienda. En Ajustes > Controles > «Cambiar teclas y botones» la fila «Meterse en el caparazón» enseña «B / Círculo» en el mando.
22. Menú principal > «Ajustes» (ratón, teclado y mando): el botón sale entre «Unirse» y «Salir» con el mismo aspecto; Intro/A o
    clic lo abre; el foco cae en la primera fila; en Ajustes > Juego > «Idioma / Language» elegir English: los rótulos cambian
    al momento (con traducciones; sin ellas, siguen en español); Escape/B vuelve atrás y, desde la portada, cierra y deja el
    cursor a la vista y el foco en «Ajustes»; «Crear partida» y «Unirse» siguen funcionando y su pantalla usa el idioma nuevo;
    cambiar el volumen y las teclas y entrar en el lobby: siguen.
