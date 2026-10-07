#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "InputCoreTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Settings/TN_SettingsSaveGame.h"
#include "TN_GameSettingsSubsystem.generated.h"

class APlayerController;
class APlayerState;
class IInputProcessor;
class UAudioComponent;
class UInputAction;
class UInputComponent;
class UInputMappingContext;
class UProximityVoiceComponent;
class USoundClass;
class USoundMix;
class UTN_FpsCounterWidget;
class UTN_PauseMenuWidget;
class UTN_TalkersWidget;
class UTN_GameSettingsSubsystem;
class UTN_LocalPlayerProfile;
class ULocalPlayer;

/** Grupos de ajustes que se pueden restablecer por separado (cada pestaña del menú de pausa). */
enum class ETNSettingsGroup : uint8
{
	Graphics,
	Sound,
	Voice,
	Controls,
	Game,
};

/** Resultado de cambiar la tecla de una fila de controles. */
enum class ETNRebindResult : uint8
{
	/** Cambiada (y guardada); si se quitó de otra fila, el mensaje lo cuenta. */
	Changed,
	/** Ya era esa. */
	Unchanged,
	/** No vale (reservada, o ese aparato no se cambia en esa fila): se puede probar con otra. */
	Refused,
};

/**
 * Una fila de la lista de controles (UTN_GameSettingsSubsystem::GetKeyBindings): una acción de IMC_Player, una dirección de
 * una acción de ejes (moverse hacia delante, hacia atrás...) o una de las teclas del propio juego (hablar y el menú).
 */
struct FTNKeyBinding
{
	/** «IA_Jump», «IA_Move:Y+» (una dirección) o las del juego: «Talk» (pulsar para hablar) y «Pause» (menú de pausa). */
	FString Id;

	/** Nombre para el jugador («Saltar», «Avanzar»...). */
	FText Label;

	/** [0] teclado y ratón, [1] mando: la tecla de ahora y la de serie (inválida: sin tecla). */
	FKey Keys[2];
	FKey Defaults[2];

	/** Se le puede poner tecla en ese aparato (a una dirección de moverse no se le pone botón: el mando va con el stick). */
	bool bEditable[2] = { true, true };

	/** Lo que se enseña en un aparato que no se cambia (el stick, el ratón). */
	FKey FixedKeys[2];

	/** Acción de IMC_Player (null en las del juego) y dirección («Y+», «X-», «+», «-»; vacía en las de botón). */
	TWeakObjectPtr<const UInputAction> Action;
	FString Direction;

	/** Orden en la lista. */
	int32 Order = 0;
};

/**
 * Lo que el subsistema de ajustes pone en cada jugador local: su copia de IMC_Player con sus teclas (y las viejas por
 * quitar), la entrada de su menú de pausa y los temblores de cámara que le ha apagado. Uno para el jugador 1 y uno por
 * invitado de la partida local (#311).
 */
USTRUCT()
struct FTNPlayerInputState
{
	GENERATED_BODY()

	/** El jugador local de este estado (los invitados; el del jugador 1 no lo necesita). */
	TWeakObjectPtr<ULocalPlayer> Player;

	/** Copia de IMC_Player con las teclas del jugador (null sin cambios) y copias viejas por quitar. */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> RemappedMapping;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputMappingContext>> RetiredMappings;

	/** Entrada del menú de pausa, metida en la pila de su PlayerController, y las teclas que lleva. */
	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> PauseInput;

	TWeakObjectPtr<APlayerController> PauseInputOwner;
	FName BoundPauseKey;
	FName BoundPausePadKey;

	/** Modificadores de temblor apagados por el ajuste (para volver a encenderlos). */
	TArray<TWeakObjectPtr<UCameraModifier>> DisabledShakes;
};

/**
 * @brief Ajustes del jugador y menú de pausa (para todos los modos: lobby, mapa procedural, carrera y solo terreno).
 *
 * Ajustes (FTNGameSettings, en la ranura TN_Settings; la parte gráfica, en UGameUserSettings): se cargan al crearse la
 * GameInstance y se aplican de verdad, sin tocar assets:
 *  - Volumen general: volumen principal del dispositivo de audio del mundo (SetTransientPrimaryVolume); a 0 con la
 *    ventana sin foco si el jugador lo pide.
 *  - Música, ambiente y voz: tres USoundClass creadas en tiempo de ejecución. Cada fotograma se reparten los sonidos
 *    generados en código (los UAudioComponent cuyo sonido no es un asset): la música sintetizada
 *    (UTN_MusicSynthComponent) a Música, el paisaje sonoro (UTN_AmbientSynthComponent) a Ambiente y la voz de los
 *    compañeros (USoundWaveProcedural del grupo Voice) a Voz.
 *  - Efectos: todo lo demás (sintetizadores de pasos, trampas, enemigos, conchas... y los sonidos de asset) se queda en
 *    la clase de sonido por defecto del motor, que baja con una USoundMix propia (SetSoundMixClassOverride).
 *  - Voz de cada compañero y silenciar: multiplicador de volumen de su componente de reproducción.
 *  - Micrófono: umbral (SpeakingThreshold) y ganancia (VoiceGain) del UProximityVoiceComponent propio, su salida
 *    (SetTransmitEnabled) según silenciado y pulsar para hablar, y el micrófono elegido (se abre al empezar la voz).
 *  - Teclas y botones: un UInputMappingContext transitorio, copia de IMC_Player con las teclas del jugador, que
 *    sustituye cada fotograma a IMC_Player en el subsistema de Enhanced Input del jugador local (la tortuga lo vuelve a
 *    poner al poseerse; el lobby y el espectador usan el mismo). Hablar y el menú de pausa, con sus teclas propias.
 *  - Sensibilidad e inversión de la cámara: escalas de giro del PlayerController (UInputSettings::bEnableLegacyInputScales
 *    está activo), con la sensibilidad del ratón o la del mando según el último dispositivo usado.
 *  - Campo de visión: CameraFOVDefault y CameraFOVSprint de la tortuga propia (su valor de clase + el desplazamiento) y,
 *    mirando a otra tortuga (espectador), un modificador de cámara que suma el desplazamiento (UTN_SettingsFovModifier).
 *  - Temblor de cámara: apaga los modificadores de cámara cuya clase se llama «...Shake...».
 *  - Brillo: gamma de salida del motor (GEngine->DisplayGamma); filtro para daltónicos: el de Slate, sobre toda la
 *    imagen (HUD y marcadores incluidos).
 *  - Tamaño de la interfaz: UUserInterfaceSettings::ApplicationScale, que solo multiplica la escala DPI del viewport del
 *    juego (UMG), no la interfaz del editor.
 *  - Quién habla: lista de texto con los jugadores que se oyen hablar (UTN_TalkersWidget).
 *  - Idioma: el guardado (o, sin elegir, el del sistema si está en la lista y, si no, el español) se aplica al crearse la
 *    GameInstance, antes de que salga ningún menú, y se cambia en caliente (TNLanguage::Apply); Docs/Localizacion.md.
 *  - Ojo de pez leve: la proyección Panini del motor (r.LensDistortion.Panini.D) con un valor suave, que se suaviza al
 *    encender o apagar y se afloja con el campo de visión para que correr no la note; el HUD no se deforma (se pinta después).
 *  - Teclas retenidas: si el jugador pulsa una tecla del juego mientras hay un menú a la vista (Intro, A, B...), lo que esa tecla
 *    repite al mantenerla no llega al juego cuando el menú se cierra (la B del mando cierra un menú y mete en el caparazón).
 *
 * Menú de pausa: mete en la pila de entrada del PlayerController local (AMP_GamePlayerController, también de
 * espectador) un UInputComponent propio con Escape, la tecla y el botón elegidos (Start de serie) y el Tabulador en el
 * editor (donde Escape corta la partida), que abre y cierra UTN_PauseMenuWidget; así no hay que tocar el
 * PlayerController. No pausa el mundo: el juego es en red.
 *
 * Lo que es de todo el proceso (gamma, escala de la interfaz, filtro de color y, en el editor, calidad gráfica, límite de
 * fotogramas y sincronización vertical) se apunta antes del primer subsistema y se devuelve al quitarse el último (en PIE
 * con varios jugadores hay uno por jugador).
 *
 * Partida local (#311): cada jugador tiene sus ajustes de jugador (cámara, controles, tecla del menú, temblor y campo de
 * visión: TNLocalPlay::CopyPerPlayerSettings); el resto es del PC y lo decide el jugador 1. Los del jugador 1 son los de
 * siempre y se guardan; los de un invitado viven en su UTN_LocalPlayerProfile y duran la partida. El menú de pausa lo abre
 * cualquiera, a pantalla completa, y para la partida de todos (SetGamePaused); solo lo maneja quien lo abrió y lo que cambia
 * es suyo (GetEditedSettings). La escala de la interfaz se multiplica por la de la pantalla partida.
 */
UCLASS()
class TORTUNABO_API UTN_GameSettingsSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** El subsistema de la GameInstance de WorldContext (null en servidor dedicado o sin GameInstance). */
	static UTN_GameSettingsSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual UWorld* GetTickableGameObjectWorld() const override;

	// ── Ajustes ──────────────────────────────────────────────────────────────

	/** Los ajustes de siempre: los del jugador 1 (los que se guardan; en red, los únicos). */
	const FTNGameSettings& GetSettings() const { return Settings; }

	/**
	 * Los que enseña y cambia el menú de pausa: los de quien lo tiene abierto. Un invitado de la partida local (#311) ve los
	 * suyos (solo cuentan sus ajustes de jugador y no se guardan); si no, los de siempre.
	 */
	const FTNGameSettings& GetEditedSettings() const;

	/** true si el menú de pausa lo maneja un invitado de la partida local (solo sus ajustes de jugador; no se guardan). */
	bool IsEditingGuest() const;

	/** Ajustes con los que juega PC: los del PC con su cámara y sus controles encima (TNLocalPlay::EffectiveSettings). */
	FTNGameSettings GetSettingsFor(const APlayerController* PC) const;

	/**
	 * Cambia los ajustes que enseña el menú (GetEditedSettings) y los aplica en el acto; los del jugador 1 se guardan al cerrar
	 * el menú (o a los pocos segundos); los de un invitado, solo sus ajustes de jugador y sin guardar.
	 */
	void EditSettings(TFunctionRef<void(FTNGameSettings&)> Edit);

	/** Vuelve a los valores de serie de una pestaña (en la gráfica, solo el brillo y el contador de FPS). */
	void ResetGroup(ETNSettingsGroup Group);

	// ── Idioma ───────────────────────────────────────────────────────────────

	/** El idioma que se está usando (cultura de la lista de idiomas: «es-ES», «en», «pt-BR»...). */
	FString GetLanguage() const;

	/**
	 * Elige el idioma (una cultura de la lista de idiomas) y lo pone en caliente; vacío = el del sistema si está en la lista y,
	 * si no, el español. Se guarda con el resto de ajustes.
	 */
	void SetLanguage(const FString& Culture);

	/**
	 * Vuelve a los valores de serie todos los ajustes propios: sonido, voz, micrófono, controles (teclas incluidas), juego,
	 * brillo y FPS. La calidad gráfica y la pantalla no se tocan (para eso está «Calidad recomendada»).
	 */
	void ResetAll();

	/**
	 * Guarda ya los ajustes pendientes (los propios y los de UGameUserSettings). Es un guardado forzado: no espera a que
	 * pase la espera de los reintentos tras un fallo (esa solo la respeta el guardado automático del Tick).
	 */
	void SaveNow();

	/** La parte gráfica cambió en UGameUserSettings: la aplica (sin la resolución) y la guardará. */
	void ApplyGraphicsChange();

	/** Aplica la resolución y el modo de ventana de UGameUserSettings (fuera del editor); se confirma o se deshace. */
	void ApplyVideoMode();

	/** Confirma (bKeep) o deshace la última resolución o modo de ventana aplicados. */
	void FinishVideoModeChange(bool bKeep);

	/** true si en este proceso se puede cambiar la ventana (no en el editor, donde la ventana es la suya). */
	static bool CanChangeVideoMode();

	/**
	 * Umbral de voz (RMS, ya con la ganancia) para una sensibilidad 0..1: 0,5 es el umbral de serie (Base) y cada extremo
	 * lo mueve 20 dB (0: diez veces más alto; 1: diez veces más bajo).
	 */
	static float SensitivityToThreshold(float Sensitivity, float BaseThreshold = 0.01f);

	/** Umbral de voz que se está usando ahora (el de serie del componente de voz con la sensibilidad elegida). */
	float GetSpeakingThreshold() const;

	// ── Cámara (cualquier cámara del juego, también la del espectador) ────────

	/**
	 * Sensibilidad de la cámara (1 = la de serie) y eje Y invertido, para el ratón (bGamepad = false) o para el mando.
	 * OJO: lo que gira con AddControllerYawInput/AddControllerPitchInput (o APlayerController::AddYawInput/AddPitchInput)
	 * ya las lleva aplicadas, porque el subsistema pone las escalas de giro del PlayerController; úsalas solo en una
	 * cámara que gire a mano con el valor crudo de la acción (o con ApplyLookSettings), para no aplicarlas dos veces.
	 */
	float GetLookSensitivity(bool bGamepad) const;
	bool IsLookYInverted(bool bGamepad) const;

	/** Lo mismo con los de PC (con la pantalla partida, cada jugador los suyos). */
	float GetLookSensitivityFor(const APlayerController* PC, bool bGamepad) const;
	bool IsLookYInvertedFor(const APlayerController* PC, bool bGamepad) const;

	/** true si el último aparato que ha tocado el jugador de PC es un mando (así se elige la sensibilidad). */
	static bool IsUsingGamepad(const APlayerController* PC);

	/**
	 * Valor crudo de mirar (X giro, Y cabeceo, el de IA_Look) con la sensibilidad y la inversión del aparato en uso de PC.
	 * Solo para cámaras que no pasan por AddYawInput/AddPitchInput (ver GetLookSensitivity).
	 */
	FVector2D ApplyLookSettings(const APlayerController* PC, const FVector2D& RawLook) const;

	/** Temblor de cámara permitido (el subsistema ya apaga los modificadores «...Shake...» del PlayerCameraManager). */
	bool IsCameraShakeEnabled() const { return Settings.bCameraShake; }

	/** Efectos del clima (0..1, accesibilidad): cuánto se ve la tormenta de arena del coop (#790). */
	float GetWeatherEffects() const { return Settings.WeatherEffects; }

	/**
	 * Grados que el jugador suma al campo de visión. La tortuga propia ya lo lleva y, mirando a otra tortuga, lo suma
	 * UTN_SettingsFovModifier: no hace falta sumarlo en otras cámaras.
	 */
	float GetFieldOfViewOffset() const { return Settings.FieldOfViewOffset; }

	/** El de PC (con la pantalla partida, cada jugador el suyo). */
	float GetFieldOfViewOffsetFor(const APlayerController* PC) const;

	// ── Teclas y botones ─────────────────────────────────────────────────────

	/** Filas de controles que se pueden cambiar, en el orden de la lista, con la tecla de ahora de cada aparato (las del menú: GetEditedSettings). */
	TArray<FTNKeyBinding> GetKeyBindings() const;

	/** Las mismas filas con las teclas de PC (los carteles las enseñan a cada jugador). */
	TArray<FTNKeyBinding> GetKeyBindingsFor(const APlayerController* PC) const;

	/** Acciones de IMC_Player que no se cambian (van con el ratón o los sticks, como mirar): solo para enseñarlas. */
	const TArray<FTNKeyBinding>& GetFixedControls() const { return FixedControls; }

	/**
	 * Pone Key (de teclado y ratón o de mando: el aparato sale de la tecla) en la fila Id. Si otra fila del mismo aparato
	 * la tenía, esa se queda con la que tenía Id (o sin tecla, si Id no tenía): OutMessage lo cuenta. Se aplica y se guarda.
	 */
	ETNRebindResult RebindKey(const FString& Id, const FKey& Key, FText& OutMessage);

	/** Vuelve a la tecla y el botón de serie de una fila (con el mismo cambio de la otra fila si alguna la tenía). */
	void ResetKeyBinding(const FString& Id, FText& OutMessage);

	/** Todas las filas a su tecla de serie (hablar y el menú de pausa incluidos). */
	void ResetAllKeyBindings();

	/** true si alguna fila no va con su tecla de serie. */
	bool HasCustomKeys() const;

	/** Tecla que se puede poner en una fila (no Escape, la consola, los sticks, la rueda, el Tabulador en el editor...). */
	static bool IsBindableKey(const FKey& Key);

	/** Tecla que la captura de «Pulsa una tecla» ignora sin decir nada (mover un stick, ejes, teclas virtuales). */
	static bool IsIgnoredWhileCapturing(const FKey& Key);

	/** Nombre de una tecla como lo lee un jugador en España (Espacio, Clic izquierdo, A / Cruz...). */
	static FText KeyDisplayName(const FKey& Key);

	/** Nombre de una acción de IMC_Player para el jugador («IA_Jump» → «Saltar»). */
	static FText ActionLabel(const FString& ActionName);

	/** IMC_Player tal cual (las teclas de serie). */
	UInputMappingContext* GetPlayerMapping() const { return OriginalMapping; }

	/** true si Key es una tecla o botón de una acción del juego (de IMC_Player, con las teclas del jugador). */
	bool IsGameplayKey(const FKey& Key) const;

	/**
	 * El contexto de controles que hay que añadir en vez de Mapping: si es IMC_Player y el jugador ha cambiado teclas,
	 * la copia con sus teclas; si no, el mismo. Para quien añada IMC_Player por su cuenta (el subsistema, de todas formas,
	 * cambia IMC_Player por la copia en cuanto lo ve puesto).
	 */
	static const UInputMappingContext* ResolveMappingContext(const UObject* WorldContext, const UInputMappingContext* Mapping);

	// ── Voz ──────────────────────────────────────────────────────────────────

	/** Clave estable de un jugador para sus ajustes de voz: su id de la plataforma o, si no hay, su nombre. */
	static FString PlayerKey(const APlayerState* PlayerState);

	float GetPlayerVoiceVolume(const FString& Key) const;
	void SetPlayerVoiceVolume(const FString& Key, float Volume);
	bool IsPlayerMuted(const FString& Key) const;
	void SetPlayerMuted(const FString& Key, bool bMuted);

	/** Componente de voz de la tortuga local (null si no hay tortuga o aún no tiene voz). */
	UProximityVoiceComponent* GetLocalVoice() const;

	/** Nivel del micrófono propio (RMS) y si hay micrófono capturando. */
	float GetMicLevel() const;
	bool IsMicCapturing() const;

	/** true si ahora mismo sale tu voz (no silenciado y, con pulsar para hablar, con la tecla pulsada). */
	bool IsTransmitAllowed() const { return bTransmitAllowed; }

	/**
	 * Elige el micrófono (id de Windows; vacío = el predeterminado). La captura abierta no se cambia en caliente (cerrar
	 * una captura WASAPI abierta cuelga el juego al viajar): se usa al empezar la voz, al reaparecer o al cambiar de mapa.
	 */
	void SetCaptureDevice(const FString& DeviceId);

	/** true si la voz propia está abierta con otro micrófono que el elegido (se cambiará al reaparecer o viajar). */
	bool IsCaptureDeviceChangePending() const;

	// ── Menú de pausa ────────────────────────────────────────────────────────

	/** Abre o cierra el menú de pausa del jugador local PC (no lo abre encima de otros menús o de la pantalla de carga). */
	void TogglePauseMenu(APlayerController* PC);
	void OpenPauseMenu(APlayerController* PC);
	/**
	 * Menú principal: abre los mismos ajustes que el menú de pausa (UTN_PauseMenuWidget, que en el controlador del menú principal
	 * se monta sin las filas de partida) encima del menú principal. Solo con el controlador del menú (AMP_MenuPlayerController).
	 * Se cierra con ClosePauseMenu o con el propio menú; IsPauseMenuOpen dice si sigue abierto. Docs/Menu_Pausa.md.
	 */
	void OpenMainMenuSettings(APlayerController* PC);
	void ClosePauseMenu();
	bool IsPauseMenuOpen() const;

	/** El jugador que tiene abierto el menú de pausa (null si está cerrado). Con la pantalla partida, solo él lo maneja. */
	APlayerController* GetPauseMenuOwner() const;

	/** true si ahora se puede abrir el menú de pausa para PC. */
	bool CanOpenPauseMenu(const APlayerController* PC) const;

	/** true si hay una interfaz que se puede pulsar a la vista: el menú de pausa u otra pantalla con el cursor del jugador local. */
	bool IsMenuUp() const;

	/** Lo llama el menú al quitarse de la pantalla (también en un viaje): guarda lo pendiente. */
	void NotifyPauseMenuClosed(UTN_PauseMenuWidget* Menu);

private:
	FTNGameSettings Settings;

	/** Hay cambios sin guardar (propios o de UGameUserSettings) y desde cuándo (segundos de la aplicación). */
	bool bSettingsDirty = false;
	bool bGraphicsDirty = false;
	double DirtySince = 0.0;

	/**
	 * Veces seguidas que ha fallado escribir el fichero de ajustes (0 = el último guardado salió bien o no hubo). Alarga la
	 * espera del guardado automático (TNSaveLogic::SaveRetryDelay) en vez de reintentar en cada fotograma.
	 */
	int32 SettingsSaveFailures = 0;

	/**
	 * Versión con la que guardó el fichero de ajustes una build más nueva, mientras siga en disco sin reescribir
	 * (0 = no aplica). Antes de reescribirlo se copia aparte: esta build pierde los campos que no conoce.
	 */
	int32 NewerFileVersion = 0;

	/** Resolución o modo de ventana aplicados y aún sin confirmar (no se guardan hasta confirmarlos). */
	bool bVideoModePending = false;

	/** Clases de sonido creadas en tiempo de ejecución y la mezcla de los efectos. */
	UPROPERTY(Transient)
	TObjectPtr<USoundClass> MusicClass;

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> AmbientClass;

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> VoiceClass;

	UPROPERTY(Transient)
	TObjectPtr<USoundMix> EffectsMix;

	/** Mundo para el que se prepararon el dispositivo de audio (mezcla, volumen principal) y el último volumen puesto. */
	TWeakObjectPtr<UWorld> AudioWorld;
	float AppliedMasterVolume = -1.f;
	float AppliedEffectsVolume = -1.f;

	/** Lo que se pone en el jugador 1 (su copia de IMC_Player, la entrada del menú, los temblores apagados). */
	UPROPERTY(Transient)
	FTNPlayerInputState PrimaryInput;

	/** Lo mismo de cada invitado de la partida local (#311). */
	UPROPERTY(Transient)
	TArray<FTNPlayerInputState> GuestInputs;

	/** El menú de pausa de la partida local para el mundo (SetGamePaused): se quita al cerrarlo. */
	TWeakObjectPtr<UWorld> PausedWorld;

	UPROPERTY(Transient)
	TObjectPtr<UTN_PauseMenuWidget> PauseMenu;

	UPROPERTY(Transient)
	TObjectPtr<UTN_FpsCounterWidget> FpsWidget;

	UPROPERTY(Transient)
	TObjectPtr<UTN_TalkersWidget> TalkersWidget;

	/** Controles: IMC_Player tal cual (las copias con las teclas de cada jugador van en FTNPlayerInputState). */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> OriginalMapping;

	/** Filas de controles con sus teclas de serie (de IMC_Player, más hablar y el menú) y las que no se cambian. */
	TArray<FTNKeyBinding> DefaultBindings;
	TArray<FTNKeyBinding> FixedControls;

	/**
	 * Teclas de serie que el código pone porque IMC_Player todavía no las trae (fila, aparato 0/1 y tecla): la copia de IMC_Player las
	 * añade. Con el asset al día (Scripts/imc_player_shell_b.py) está vacía.
	 */
	struct FCodeDefaultKey
	{
		FString RowId;
		int32 Device = 0;
		FKey Key;
	};
	TArray<FCodeDefaultKey> PendingCodeDefaults;

	/** Voz propia: si sale (último cálculo). */
	bool bTransmitAllowed = true;

	/** Gamma de salida del motor antes de tocar el brillo (la del 0,5). */
	float BaseDisplayGamma = 2.2f;

	/** Filtro para daltónicos y tamaño de la interfaz puestos ahora (para no tocarlos cada fotograma). */
	uint8 AppliedColorFilter = 0;
	float AppliedColorFilterStrength = -1.f;
	float AppliedUIScale = -1.f;

	/** Idioma puesto ahora (vacío: ninguno todavía) y el del sistema en términos de la lista de idiomas (se calcula al crearse). */
	FString AppliedLanguage;
	FString SystemLanguage;

	/** Ojo de pez: cuánto está encendido (0..1, se suaviza al encender o apagar) y la última distancia Panini escrita. */
	float FisheyeAmount = 0.f;
	float AppliedPaniniD = -1.f;

	/** Vigilante de teclas retenidas (procesador de entrada de Slate; ver TN_GameSettingsSubsystem.cpp). */
	TSharedPtr<IInputProcessor> HeldKeyGuard;

	void LoadSettings();
	void MarkDirty(bool bGraphics);

	/** Escribe el fichero de ajustes propios si hay cambios. Si falla, sigue sucio y cuenta el fallo para la espera. */
	void SaveSettingsFile();

	/** Guarda lo gráfico de UGameUserSettings si hay cambios (no con una resolución sin confirmar). */
	void SaveGraphicsSettings();

	void CreateSoundClasses();

	/** Aplica lo que no depende del mundo (brillo, filtro de color, interfaz, micrófono) y lo de audio del mundo actual. */
	void ApplyGlobalSettings();
	void ApplyAudioVolumes(UWorld* World, bool bForce);

	void EnsurePauseInput(APlayerController* PC, FTNPlayerInputState& State, const FTNGameSettings& Own);

	void UpdateSounds(UWorld* World);
	void UpdateLocalVoice(APlayerController* PC);
	void UpdateCamera(APlayerController* PC, FTNPlayerInputState& State, const FTNGameSettings& Own);
	void UpdateFisheye(APlayerController* PC, float DeltaTime);
	void UpdateFpsCounter(APlayerController* PC);
	void UpdateTalkers(APlayerController* PC);

	/** Clase de sonido que toca a un componente (null: se queda en la de por defecto, efectos). */
	USoundClass* ClassFor(const UAudioComponent* Component) const;

	float BrightnessToGamma(float Brightness) const;

	/** Pone el idioma de los ajustes si ha cambiado (la lista de idiomas, el del sistema y el nativo se resuelven en TNLanguage). */
	void ApplyLanguage();

	/** Tamaño de la interfaz: el del jugador 1 por la escala de la pantalla partida (UTN_LocalPlaySubsystem::GetSplitUIScale). */
	void ApplyUIScale();

	// Jugadores locales (#311)
	/** El perfil del invitado que tiene abierto el menú de pausa (null si lo tiene el jugador 1 o está cerrado). */
	UTN_LocalPlayerProfile* GetEditedGuest() const;
	/** Los ajustes que cambia el menú: los del invitado que lo tiene abierto o los del jugador 1. */
	FTNGameSettings& EditTarget();
	/** El estado de PC (jugador 1 o invitado; null para cualquier otro PlayerController). */
	FTNPlayerInputState* StateFor(const APlayerController* PC);
	const FTNPlayerInputState* StateFor(const APlayerController* PC) const;
	/** Los ajustes propios de PC (los del jugador 1 o los del invitado; null para cualquier otro). */
	const FTNGameSettings* OwnSettingsFor(const APlayerController* PC) const;
	/** Para (o deja seguir) la partida de todos con el menú de pausa de la partida local. */
	void SetWorldPaused(APlayerController* PC, bool bPause);

	// Controles
	void BuildDefaultBindings();
	void RebuildRemappedMapping(FTNPlayerInputState& State, const FTNGameSettings& Own);
	void UpdateInputMapping(const ULocalPlayer* Player, FTNPlayerInputState& State);
	const FTNKeyBinding* FindDefaultBinding(const FString& Id) const;
	FKey GetBindingKey(const FTNGameSettings& Own, const FTNKeyBinding& Row, int32 Device) const;
	void SetBindingKey(FTNGameSettings& Own, const FTNKeyBinding& Row, int32 Device, const FKey& Key);
	ETNRebindResult AssignKey(FTNGameSettings& Own, const FTNKeyBinding& Row, int32 Device, const FKey& Key, bool bValidate, FText& OutMessage);
	TArray<FTNKeyBinding> BuildKeyBindings(const FTNGameSettings& Own) const;
	/** Las teclas del menú han cambiado: se rehace la copia de IMC_Player de quien lo maneja (y se guarda si es el jugador 1). */
	void OnKeyBindingsChanged();
};

/**
 * @brief Suma el campo de visión del jugador (UTN_GameSettingsSubsystem::GetFieldOfViewOffset) cuando la cámara mira a
 * la tortuga de otro jugador. La tortuga propia ya lo lleva en su cámara y las cámaras
 * de escena (tienda, probador) no se tocan. Va la última (prioridad 250), después de los temblores.
 */
UCLASS()
class TORTUNABO_API UTN_SettingsFovModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	UTN_SettingsFovModifier();

	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

	void SetSettings(UTN_GameSettingsSubsystem* InSettings) { SettingsOwner = InSettings; }

private:
	TWeakObjectPtr<UTN_GameSettingsSubsystem> SettingsOwner;
};
