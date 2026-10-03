#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Multiplayer/TN_SaveGameDecisions.h"
#include "TN_SettingsSaveGame.generated.h"

/**
 * Ajustes del jugador que no guarda UGameUserSettings: sonido por categorías, voz de los compañeros, micrófono,
 * controles, juego y accesibilidad, más el brillo y el contador de FPS. Los aplica UTN_GameSettingsSubsystem y se
 * guardan en la ranura TN_Settings (Saved/SaveGames/TN_Settings.sav). La parte gráfica (resolución, ventana, calidad,
 * sincronización vertical, límite de fotogramas y escala de resolución) va en UGameUserSettings (GameUserSettings.ini).
 *
 * Cada volumen va de 0 a 1 (la voz de cada compañero, hasta 2). Los valores por defecto dejan el juego como estaba antes
 * del menú de pausa.
 */
USTRUCT()
struct FTNGameSettings
{
	GENERATED_BODY()

	// ── Sonido ───────────────────────────────────────────────────────────────

	/** Volumen general: todo lo que suena, voz incluida (volumen principal del dispositivo de audio). */
	UPROPERTY()
	float MasterVolume = 1.f;

	/** Música sintetizada (tienda, probador, victoria, derrota...). */
	UPROPERTY()
	float MusicVolume = 1.f;

	/** Efectos: pasos, trampas, enemigos, conchas, bailes, interfaz... (todo lo que no es música, ambiente ni voz). */
	UPROPERTY()
	float EffectsVolume = 1.f;

	/** Paisaje sonoro: olas, viento, selva, cascadas... */
	UPROPERTY()
	float AmbientVolume = 1.f;

	// ── Voz de los compañeros ────────────────────────────────────────────────

	/** Volumen de la voz de todos los compañeros. */
	UPROPERTY()
	float VoiceVolume = 1.f;

	/** Volumen de cada compañero (0..2), por su clave (UTN_GameSettingsSubsystem::PlayerKey). Sin entrada = 1. */
	UPROPERTY()
	TMap<FString, float> PlayerVoiceVolumes;

	/** Compañeros silenciados, por su clave. */
	UPROPERTY()
	TArray<FString> MutedPlayers;

	// ── Micrófono ────────────────────────────────────────────────────────────

	/** true: solo se habla con la tecla pulsada; false: voz abierta (se habla al superar el umbral). */
	UPROPERTY()
	bool bPushToTalk = false;

	/** Tecla (teclado o ratón) de pulsar para hablar. */
	UPROPERTY()
	FName PushToTalkKey = TEXT("V");

	/** Botón del mando de pulsar para hablar. */
	UPROPERTY()
	FName PushToTalkPadKey = TEXT("Gamepad_DPad_Down");

	/** Micrófono silenciado: no se envía nada. */
	UPROPERTY()
	bool bMicMuted = false;

	/** Sensibilidad (0..1): más alta, voces más bajas abren el micrófono. 0,5 = el umbral de siempre (-40 dB). */
	UPROPERTY()
	float MicSensitivity = 0.5f;

	/** Ganancia del micrófono (multiplica la del componente de voz; 1 = la de siempre). */
	UPROPERTY()
	float MicGain = 1.f;

	/** Micrófono elegido (id del dispositivo de Windows); vacío = el predeterminado. Se abre al empezar la voz. */
	UPROPERTY()
	FString CaptureDeviceId;

	// ── Controles ────────────────────────────────────────────────────────────

	/** Sensibilidad de la cámara con el ratón (1 = la de siempre). */
	UPROPERTY()
	float MouseSensitivity = 1.f;

	/** Sensibilidad de la cámara con el mando (1 = la de siempre). */
	UPROPERTY()
	float GamepadSensitivity = 1.f;

	UPROPERTY()
	bool bInvertMouseY = false;

	UPROPERTY()
	bool bInvertGamepadY = false;

	/** Vibración del mando al recibir un golpe (derribo, aturdimiento, impacto de un lanzable). Desde la versión 4. */
	UPROPERTY()
	bool bGamepadVibration = true;

	/**
	 * Teclas y botones reasignados, por fila de controles y aparato: «IA_Jump#0» (teclado y ratón) o «IA_Move:Y+#1»
	 * (mando) → tecla nueva. Lo que no está aquí va con la tecla de serie de IMC_Player.
	 */
	UPROPERTY()
	TMap<FString, FName> KeyOverrides;

	/** Tecla y botón del menú de pausa (en el editor, además, el Tabulador). */
	UPROPERTY()
	FName PauseKey = TEXT("Escape");

	UPROPERTY()
	FName PausePadKey = TEXT("Gamepad_Special_Right");

	/**
	 * Tecla y botón de «Cambiar de cámara» (tercera o primera persona sin gafas; Docs/Modo_VR.md, «Primera persona»): T y el
	 * clic del stick derecho de serie, que no usa ninguna otra fila. Nunca la de hablar: si coincidieran, esta se queda sin.
	 */
	UPROPERTY()
	FName CameraKey = TEXT("T");

	UPROPERTY()
	FName CameraPadKey = TEXT("Gamepad_RightThumbstick");

	// ── Juego y accesibilidad ────────────────────────────────────────────────

	/** Temblor de cámara (golpes, quads de la carrera, tormenta...). */
	UPROPERTY()
	bool bCameraShake = true;

	/** Grados que se suman al campo de visión de la tortuga (en reposo y al correr). */
	UPROPERTY()
	float FieldOfViewOffset = 0.f;

	/** Filtro para daltónicos (EColorVisionDeficiency: 0 ninguno, 1 deuteranopía, 2 protanopía, 3 tritanopía). */
	UPROPERTY()
	uint8 ColorFilter = 0;

	/** Intensidad del filtro para daltónicos (0..1). */
	UPROPERTY()
	float ColorFilterStrength = 1.f;

	/** Tamaño de la interfaz del juego (multiplica la escala de la pantalla; 1 = la de siempre). */
	UPROPERTY()
	float UIScale = 1.f;

	/** Aviso de texto con quién está hablando por voz (para jugar sin sonido o con dificultades de oído). */
	UPROPERTY()
	bool bShowTalkers = false;

	/**
	 * Idioma del juego: código de cultura de la lista de idiomas (UTN_LanguageSettings; «es-ES», «en», «pt-BR»...). Vacío =
	 * todavía sin elegir: se usa el idioma del sistema si está en la lista y, si no, el español.
	 */
	UPROPERTY()
	FString Language;

	/** Ojo de pez leve (proyección Panini muy suave): todo se ve algo más inmenso. Encendido de serie. */
	UPROPERTY()
	bool bFisheye = true;

	/** Silenciar el juego cuando la ventana no está activa. */
	UPROPERTY()
	bool bMuteInBackground = false;

	/**
	 * Modo VR (Docs/Modo_VR.md): 0 automático (primera persona con gafas si el juego arranca con ellas), 1 desactivado
	 * (con gafas, la pantalla plana de siempre), 2 simulado sin gafas (primera persona, aletas e interfaz en el mundo con el
	 * ratón, para probar el modo en el PC). La variable de consola TN.VR y -vrsim / -novr mandan sobre él.
	 */
	UPROPERTY()
	uint8 VRMode = 0;

	/** Giro con el stick derecho en VR: 0 a pasos de 30°, 1 a pasos de 45°, 2 suave. */
	UPROPERTY()
	uint8 VRTurn = 0;

	/**
	 * Cámara sin gafas (Docs/Modo_VR.md, «Primera persona»): 0 tercera persona (la de siempre), 1 primera persona (en la
	 * cabeza, viendo el cuerpo propio sin la cabeza). Se cambia también con CameraKey / CameraPadKey; TN.Camera manda.
	 */
	UPROPERTY()
	uint8 CameraView = 0;

	// ── Pantalla (lo que no guarda UGameUserSettings) ────────────────────────

	/** Brillo (0..1; 0,5 = el de siempre): cambia la gamma de salida del motor. */
	UPROPERTY()
	float Brightness = 0.5f;

	/** Contador de fotogramas por segundo en la esquina de abajo a la derecha. */
	UPROPERTY()
	bool bShowFps = false;
};

/** Ranura de guardado de FTNGameSettings. */
UCLASS()
class TORTUNABO_API UTN_SettingsSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/**
	 * Versión del formato (TNSaveLogic::SETTINGS_SAVE_VERSION; la 4 añade la vibración del mando); al cargar,
	 * TNSettingsMigration la lleva a la actual. 0 = sin número. UE solo escribe en el fichero lo que difiere del valor
	 * por defecto de la clase: cuando este valía la versión de entonces (1, 2 o 3), no se escribía nunca y no se podía saber de qué versión era un guardado. Por eso el
	 * valor por defecto es 0 y se sella al guardar (StampCurrentVersion), como el perfil cosmético y el tutorial.
	 * Siempre se sella con la versión de esta build, también si el fichero venía de una más nueva: el número dice cómo
	 * es el contenido escrito (solo los campos que esta build conoce), y así la build nueva vuelve a migrarlo. Lo que
	 * esta build pierde al reescribirlo se conserva en una copia (UTN_GameSettingsSubsystem::SaveSettingsFile).
	 */
	UPROPERTY()
	int32 Version = 0;

	UPROPERTY()
	FTNGameSettings Settings;

	/** @brief Sella el guardado con la versión actual (antes de escribirlo). */
	void StampCurrentVersion() { Version = TNSaveLogic::SETTINGS_SAVE_VERSION; }
};
