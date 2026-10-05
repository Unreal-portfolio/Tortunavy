#include "Settings/TN_GameSettingsSubsystem.h"
#include "Core/TN_Log.h"
#include "Menu/MP_MenuPlayerController.h"
#include "Multiplayer/TN_LocalPlayRules.h"
#include "Multiplayer/TN_LocalPlaySubsystem.h"
#include "Multiplayer/TN_LocalPlayerProfile.h"
#include "Multiplayer/TN_LocalViews.h"
#include "Multiplayer/TN_SaveGameIO.h"
#include "Settings/TN_SettingsMigration.h"
#include "Audio/TN_AmbientSoundscape.h"
#include "Audio/TN_AmbientSynthComponent.h"
#include "Audio/TN_MusicSynthComponent.h"
#include "Player/MP_GamePlayerController.h"
#include "Player/TortugaCharacter.h"
#include "Rally/TN_RallyPlayerController.h"
#include "Settings/TN_LanguageSettings.h"
#include "UI/Loading/TN_LoadingScreenSubsystem.h"
#include "UI/Pause/TN_PauseMenuWidget.h"
#include "Voice/ProximityVoiceComponent.h"
#include "Voice/TN_VoiceRouting.h"
#include "World/Beach/TN_BeachCritterSynth.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Camera/CameraModifier.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/InputComponent.h"
#include "Components/SynthComponent.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Rendering/RenderingCommon.h"
#include "Scalability.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/UObjectHash.h"
#include "VR/TN_VRControls.h"
#include "VR/TN_VRMode.h"
#include "Settings/TN_InputDeviceSubsystem.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque.
namespace TNGameSettingsDetail
{
	/** PlayerController de una partida (tortugas o buggies del Rally): el que lleva el menú de pausa. */
	bool IsMatchPlayerController(const APlayerController* PC)
	{
		return PC && (PC->IsA<AMP_GamePlayerController>() || PC->IsA<ATN_RallyPlayerController>());
	}

	/** Ranura de guardado de los ajustes propios (Saved/SaveGames/TN_Settings.sav). */
	const TCHAR* const SlotName = TEXT("TN_Settings");
	constexpr int32 SlotUser = 0;

	/**
	 * Segundos sin cambios antes de guardar solos (con el menú abierto se guarda al cerrarlo) y tope de la espera entre
	 * reintentos si el guardado falla (se duplica con cada fallo: 3, 6, 12, 24, 48, 60 s).
	 */
	constexpr double AutoSaveDelay = TNSaveLogic::SETTINGS_AUTOSAVE_DELAY;
	constexpr double AutoSaveMaxDelay = TNSaveLogic::SETTINGS_AUTOSAVE_MAX_DELAY;

	/**
	 * Capas: el menú de pausa por encima del HUD (4-10), las ruedas (30-31), la tienda y el general (40) y las pantallas
	 * de la carrera (20-21); por debajo de la pantalla de carga (20000). Quién habla, debajo del menú; el contador de FPS,
	 * encima de todo eso.
	 */
	constexpr int32 PauseMenuZOrder = 60;
	constexpr int32 TalkersZOrder = 55;
	constexpr int32 FpsZOrder = 70;

	/** Prioridad de la entrada del menú en la pila del PlayerController (por delante de la tortuga y del propio PC). */
	constexpr int32 PauseInputPriority = 100;

	/** Volumen máximo de cada compañero (se puede subir a quien habla bajito). */
	constexpr float MaxPlayerVoice = 2.f;

	/** Tamaño de la interfaz: de un 75 % a un 130 % del de serie. */
	constexpr float MinUIScale = 0.75f;
	constexpr float MaxUIScale = 1.3f;

	/**
	 * Ojo de pez leve: proyección Panini del motor (r.LensDistortion.Panini.D/S; en 5.6 se aplica en el pase de escalado o dentro del
	 * TSR, sobre la imagen del mundo, así que el HUD de UMG, que se pinta después, no se deforma). D va de 0 (sin efecto) a 1
	 * (estereográfica); con el campo de visión de la tortuga (72-82°) hasta 1 solo comprime el borde un 10 %, por eso el valor de serie
	 * es medio. S (compresión vertical) a 0. Se pueden probar en vivo: TN.Fisheye.D y TN.Fisheye.S.
	 */
	TAutoConsoleVariable<float> CVarFisheyeD(TEXT("TN.Fisheye.D"), 0.55f,
		TEXT("Ojo de pez leve (ajuste del menú de pausa): distancia Panini con todo encendido, de 0 (sin efecto) a 1 (estereográfica). Se afloja sola con el campo de visión."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarFisheyeS(TEXT("TN.Fisheye.S"), 0.f,
		TEXT("Ojo de pez leve: compresión vertical Panini de 0 a 1."), ECVF_Default);

	/** Segundos que tarda el ojo de pez en encenderse o apagarse del todo (así no da un salto al cambiar el ajuste). */
	constexpr float FisheyeFadeSeconds = 1.2f;

	/**
	 * Distancia Panini que deja el borde de la imagen igual de comprimido con el campo de visión de ahora (Fov) que BaseD con el de
	 * reposo (RestFov), para que correr (que abre el campo de visión) no se note más. La compresión del borde respecto a la
	 * proyección normal es c = (1 + d) cos(f) / (d + cos(f)), con f la mitad del campo de visión; se despeja d. Si el campo de visión
	 * no es mayor que el de reposo, o no hay dato, BaseD.
	 */
	float FisheyeDistanceForFov(float BaseD, float RestFov, float Fov)
	{
		if (BaseD <= KINDA_SMALL_NUMBER || RestFov < 20.f || Fov <= RestFov + 0.1f || Fov >= 175.f)
		{
			return BaseD;
		}
		const float CosRest = FMath::Cos(FMath::DegreesToRadians(RestFov * 0.5f));
		const float CosNow = FMath::Cos(FMath::DegreesToRadians(Fov * 0.5f));
		const float Edge = (1.f + BaseD) * CosRest / (BaseD + CosRest);
		if (Edge <= CosNow + KINDA_SMALL_NUMBER)
		{
			return BaseD;
		}
		return FMath::Clamp(CosNow * (1.f - Edge) / (Edge - CosNow), 0.f, BaseD);
	}

	/** Controles del jugador (los que pone ATortugaCharacter; es el único contexto de controles del juego). */
	const TCHAR* const PlayerMappingPath = TEXT("/Game/Blueprints/Gameplay/Controls/IMC_Player.IMC_Player");

	/** Filas de controles del propio juego (no están en IMC_Player). */
	const TCHAR* const TalkId = TEXT("Talk");
	const TCHAR* const PauseId = TEXT("Pause");
	/** «Cambiar de cámara» (tercera o primera persona sin gafas; la lee ATortugaCharacter cada fotograma). */
	const TCHAR* const CameraId = TEXT("Camera");

	/**
	 * Lo que había en el proceso antes del primer subsistema: en PIE con varios jugadores hay un subsistema por jugador y
	 * todos tocan lo mismo (gamma, escala de la interfaz, calidad...), así que se apunta con el primero y se devuelve con el
	 * último.
	 */
	struct FProcessBaseline
	{
		int32 Users = 0;
		float DisplayGamma = 2.2f;
		float ApplicationScale = 1.f;
		bool bEditor = false;
		Scalability::FQualityLevels QualityLevels;
		float MaxFPS = 0.f;
		int32 VSync = 0;
		/** Proyección Panini del motor (ojo de pez): lo que había antes de tocarla (0 = apagada). */
		float PaniniD = 0.f;
		float PaniniS = 0.f;
	};

	FProcessBaseline& Baseline()
	{
		static FProcessBaseline Value;
		return Value;
	}

	void ClampSettings(FTNGameSettings& S)
	{
		S.MasterVolume = FMath::Clamp(S.MasterVolume, 0.f, 1.f);
		S.MusicVolume = FMath::Clamp(S.MusicVolume, 0.f, 1.f);
		S.EffectsVolume = FMath::Clamp(S.EffectsVolume, 0.f, 1.f);
		S.AmbientVolume = FMath::Clamp(S.AmbientVolume, 0.f, 1.f);
		S.VoiceVolume = FMath::Clamp(S.VoiceVolume, 0.f, 1.f);
		for (TPair<FString, float>& Pair : S.PlayerVoiceVolumes) { Pair.Value = FMath::Clamp(Pair.Value, 0.f, MaxPlayerVoice); }
		S.MicSensitivity = FMath::Clamp(S.MicSensitivity, 0.f, 1.f);
		S.MicGain = FMath::Clamp(S.MicGain, 0.1f, 4.f);
		S.MouseSensitivity = FMath::Clamp(S.MouseSensitivity, 0.1f, 5.f);
		S.GamepadSensitivity = FMath::Clamp(S.GamepadSensitivity, 0.1f, 5.f);
		S.FieldOfViewOffset = FMath::Clamp(S.FieldOfViewOffset, -30.f, 30.f);
		S.ColorFilter = static_cast<uint8>(FMath::Clamp<int32>(S.ColorFilter, 0, 3));
		S.ColorFilterStrength = FMath::Clamp(S.ColorFilterStrength, 0.f, 1.f);
		S.Brightness = FMath::Clamp(S.Brightness, 0.f, 1.f);
		S.UIScale = FMath::Clamp(S.UIScale, MinUIScale, MaxUIScale);
		S.VRMode = static_cast<uint8>(FMath::Clamp<int32>(S.VRMode, 0, 2));
		S.VRTurn = static_cast<uint8>(FMath::Clamp<int32>(S.VRTurn, 0, 2));
		S.VRVignette = static_cast<uint8>(FMath::Clamp<int32>(S.VRVignette, 0, 2));
		S.CameraView = static_cast<uint8>(FMath::Clamp<int32>(S.CameraView, 0, 1));
		// Un idioma que ya no está en la lista (se quitó de la configuración): sin elegir, que toca el del sistema.
		if (!S.Language.IsEmpty() && TNLanguage::IndexOf(S.Language) == INDEX_NONE)
		{
			S.Language.Reset();
		}
		// Sin tecla (None) vale: una fila se puede quedar sin tecla al dársela a otra. Una que ya no existe, a la de serie.
		const FTNGameSettings Defaults;
		auto FixKey = [](FName& KeyName, const FName Fallback)
		{
			if (!KeyName.IsNone() && !FKey(KeyName).IsValid()) { KeyName = Fallback; }
		};
		FixKey(S.PushToTalkKey, Defaults.PushToTalkKey);
		FixKey(S.PushToTalkPadKey, Defaults.PushToTalkPadKey);
		FixKey(S.PauseKey, Defaults.PauseKey);
		FixKey(S.PausePadKey, Defaults.PausePadKey);
		FixKey(S.CameraKey, Defaults.CameraKey);
		FixKey(S.CameraPadKey, Defaults.CameraPadKey);
		// «Cambiar de cámara» nunca va con la tecla de hablar (con pulsar para hablar, hablar cambiaría la cámara): si
		// coinciden, la cámara se queda sin ella (se le pone otra en Controles).
		if (!S.CameraKey.IsNone() && S.CameraKey == S.PushToTalkKey) { S.CameraKey = NAME_None; }
		if (!S.CameraPadKey.IsNone() && S.CameraPadKey == S.PushToTalkPadKey) { S.CameraPadKey = NAME_None; }
		for (auto It = S.KeyOverrides.CreateIterator(); It; ++It)
		{
			if (!It.Value().IsNone() && !FKey(It.Value()).IsValid()) { It.RemoveCurrent(); }
		}
	}

	/** Un modificador de cámara que tiembla (el de serie del motor, UCameraModifier_CameraShake, o uno propio «...Shake...»). */
	bool IsShakeModifier(const UCameraModifier* Modifier)
	{
		return Modifier && Modifier->GetClass()->GetName().Contains(TEXT("Shake"));
	}

	// ── Controles ────────────────────────────────────────────────────────────

	/** 0 teclado y ratón, 1 mando. */
	int32 DeviceOf(const FKey& Key)
	{
		return Key.IsGamepadKey() ? 1 : 0;
	}

	/** El mismo gatillo como eje (así los lleva IMC_Player): pulsarlo da el botón y el eje a la vez. */
	FKey PhysicalKey(const FKey& Key)
	{
		if (Key == EKeys::Gamepad_LeftTrigger) { return EKeys::Gamepad_LeftTriggerAxis; }
		if (Key == EKeys::Gamepad_RightTrigger) { return EKeys::Gamepad_RightTriggerAxis; }
		return Key;
	}

	/** El mismo gatillo como botón (hablar y el menú se leen pulsados, y un eje no se «pulsa»). */
	FKey ButtonKey(const FKey& Key)
	{
		if (Key == EKeys::Gamepad_LeftTriggerAxis) { return EKeys::Gamepad_LeftTrigger; }
		if (Key == EKeys::Gamepad_RightTriggerAxis) { return EKeys::Gamepad_RightTrigger; }
		return Key;
	}

	bool SamePhysicalKey(const FKey& A, const FKey& B)
	{
		return A.IsValid() == B.IsValid() && (!A.IsValid() || PhysicalKey(A) == PhysicalKey(B));
	}

	bool IsGameRow(const FTNKeyBinding& Row)
	{
		return Row.Id == TalkId || Row.Id == PauseId || Row.Id == CameraId;
	}

	/** Tecla que forma fila (teclas, botones y gatillos); los ejes (stick, ratón, rueda) no se cambian. */
	bool IsRowKey(const FKey& Key)
	{
		if (!Key.IsValid() || Key.IsAxis2D() || Key.IsAxis3D() || Key.IsTouch() || Key.IsGesture())
		{
			return false;
		}
		if (Key.IsAxis1D())
		{
			return Key == EKeys::Gamepad_LeftTriggerAxis || Key == EKeys::Gamepad_RightTriggerAxis;
		}
		return true;
	}

	/** Clave de KeyOverrides de una fila y un aparato: «IA_Jump#0», «IA_Move:Y+#0». */
	FString OverrideName(const FString& Id, int32 Device)
	{
		return FString::Printf(TEXT("%s#%d"), *Id, Device);
	}

	/**
	 * Hacia dónde empuja una asignación de una acción de ejes: el valor de una tecla (1, 0, 0) pasado por sus
	 * modificadores (intercambiar ejes y negar), como hace Enhanced Input. «Y+», «X-»...; «+» o «-» en las de un eje;
	 * vacío en las de botón.
	 */
	FString MappingDirection(const FEnhancedActionKeyMapping& Mapping)
	{
		const UInputAction* Action = Mapping.Action;
		if (!Action || Action->ValueType == EInputActionValueType::Boolean)
		{
			return FString();
		}
		FVector Value(1.f, 0.f, 0.f);
		for (const TObjectPtr<UInputModifier>& Modifier : Mapping.Modifiers)
		{
			if (const UInputModifierSwizzleAxis* Swizzle = Cast<UInputModifierSwizzleAxis>(Modifier))
			{
				switch (Swizzle->Order)
				{
				case EInputAxisSwizzle::YXZ: Swap(Value.X, Value.Y); break;
				case EInputAxisSwizzle::ZYX: Swap(Value.X, Value.Z); break;
				case EInputAxisSwizzle::XZY: Swap(Value.Y, Value.Z); break;
				case EInputAxisSwizzle::YZX: Value = FVector(Value.Y, Value.Z, Value.X); break;
				case EInputAxisSwizzle::ZXY: Value = FVector(Value.Z, Value.X, Value.Y); break;
				default: break;
				}
			}
			else if (const UInputModifierNegate* Negate = Cast<UInputModifierNegate>(Modifier))
			{
				Value = FVector(Negate->bX ? -Value.X : Value.X, Negate->bY ? -Value.Y : Value.Y, Negate->bZ ? -Value.Z : Value.Z);
			}
		}
		if (Action->ValueType == EInputActionValueType::Axis1D)
		{
			return Value.X < 0.f ? TEXT("-") : TEXT("+");
		}
		const FVector Size = Value.GetAbs();
		if (Size.Y > Size.X && Size.Y >= Size.Z)
		{
			return Value.Y < 0.f ? TEXT("Y-") : TEXT("Y+");
		}
		if (Size.Z > Size.X)
		{
			return Value.Z < 0.f ? TEXT("Z-") : TEXT("Z+");
		}
		return Value.X < 0.f ? TEXT("X-") : TEXT("X+");
	}

	/** La asignación es la de esa fila en ese aparato (misma acción, misma dirección, tecla de ese aparato). */
	bool MappingBelongsTo(const FEnhancedActionKeyMapping& Mapping, const FTNKeyBinding& Row, int32 Device)
	{
		return Mapping.Action && Mapping.Action == Row.Action.Get() && IsRowKey(Mapping.Key) && DeviceOf(Mapping.Key) == Device
			&& MappingDirection(Mapping) == Row.Direction;
	}

	/** Orden de las acciones en la lista de controles (lo demás, detrás). */
	int32 ActionOrder(const FString& ActionName)
	{
		static const TArray<FString> Order = { TEXT("IA_Move"), TEXT("IA_Look"), TEXT("IA_Jump"), TEXT("IA_Sprint"), TEXT("IA_Interact"),
			TEXT("IA_Shell"), TEXT("IA_DropItem"), TEXT("IA_RotateInventory"), TEXT("IA_OpenEmoteWheel"), TEXT("IA_OpenChatWheel"),
			TEXT("IA_RadialNavigate") };
		const int32 Found = Order.IndexOfByKey(ActionName);
		return Found == INDEX_NONE ? 1000 : Found;
	}

	int32 DirectionOrder(const FString& Direction)
	{
		static const TArray<FString> Order = { TEXT(""), TEXT("Y+"), TEXT("Y-"), TEXT("X-"), TEXT("X+"), TEXT("+"), TEXT("-"), TEXT("Z+"), TEXT("Z-") };
		const int32 Found = Order.IndexOfByKey(Direction);
		return Found == INDEX_NONE ? 9 : Found;
	}

	/** Nombre de una fila: el de la acción o, en una dirección, qué hace («Avanzar», «Cambiar de objeto (siguiente)»). */
	FText BindingLabel(const FString& ActionName, const FString& Direction)
	{
		const FText Base = UTN_GameSettingsSubsystem::ActionLabel(ActionName);
		if (Direction.IsEmpty())
		{
			return Base;
		}
		if (ActionName == TEXT("IA_Move"))
		{
			if (Direction == TEXT("Y+")) { return NSLOCTEXT("TNSettings", "MoveForward", "Avanzar"); }
			if (Direction == TEXT("Y-")) { return NSLOCTEXT("TNSettings", "MoveBack", "Retroceder"); }
			if (Direction == TEXT("X-")) { return NSLOCTEXT("TNSettings", "MoveLeft", "Ir a la izquierda"); }
			if (Direction == TEXT("X+")) { return NSLOCTEXT("TNSettings", "MoveRight", "Ir a la derecha"); }
		}
		static const TMap<FString, FText> Parts = {
			{ TEXT("+"), NSLOCTEXT("TNSettings", "DirNext", "siguiente") }, { TEXT("-"), NSLOCTEXT("TNSettings", "DirPrevious", "anterior") },
			{ TEXT("Y+"), NSLOCTEXT("TNSettings", "DirForward", "adelante") }, { TEXT("Y-"), NSLOCTEXT("TNSettings", "DirBack", "atrás") },
			{ TEXT("X+"), NSLOCTEXT("TNSettings", "DirRight", "derecha") }, { TEXT("X-"), NSLOCTEXT("TNSettings", "DirLeft", "izquierda") },
			{ TEXT("Z+"), NSLOCTEXT("TNSettings", "DirUp", "arriba") }, { TEXT("Z-"), NSLOCTEXT("TNSettings", "DirDown", "abajo") },
		};
		const FText* Part = Parts.Find(Direction);
		return FText::Format(NSLOCTEXT("TNSettings", "DirectionFmt", "{0} ({1})"), Base, Part ? *Part : FText::AsCultureInvariant(Direction));
	}

	FText DeviceWord(int32 Device)
	{
		return Device == 1 ? NSLOCTEXT("TNSettings", "WordPad", "el mando") : NSLOCTEXT("TNSettings", "WordKeyboard", "el teclado y el ratón");
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Teclas retenidas
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Vigilante de teclas retenidas: un procesador de entrada de Slate que ve cada tecla antes que ningún widget.
 *
 * El problema: la B / Círculo del mando cierra el menú de pausa (y el resto de pantallas: tienda, resumen...) y, en juego, mete en el
 * caparazón. Con el menú a la vista, el menú se come la pulsación; si la tecla sigue apretada cuando el menú desaparece, el
 * motor vuelve a mandar repeticiones de esa tecla, ya al juego, y Enhanced Input (Input.AutoReconcilePressedEventsOnFirstRepeat)
 * las toma por una pulsación nueva: la tortuga se metería en el caparazón (o saltaría, con la A de «Continuar») sin que nadie
 * lo pidiera. Igual con el teclado (Intro, Espacio).
 *
 * La solución: si una tecla de una acción del juego se pulsa con un menú a la vista, se apunta; cuando el menú ya no está, sus
 * repeticiones se descartan hasta que se suelta. Con el menú a la vista pasan todas (el menú repite al mantener una flecha).
 */
class FTNHeldKeyGuard : public IInputProcessor
{
public:
	explicit FTNHeldKeyGuard(UTN_GameSettingsSubsystem* InOwner) : Owner(InOwner) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
	{
		// Sin la ventana activa se pierden los «soltar»: se olvida todo.
		if (Swallowing.Num() > 0 && !SlateApp.IsActive())
		{
			Swallowing.Reset();
		}
	}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		const UTN_GameSettingsSubsystem* Settings = Owner.Get();
		if (!Settings)
		{
			return false;
		}
		const FKey Key = InKeyEvent.GetKey();
		if (!InKeyEvent.IsRepeat())
		{
			// Pulsación nueva: con un menú a la vista es del menú; si lo cierra y la tecla sigue apretada, lo que repita no es del juego.
			if (Settings->IsMenuUp() && Settings->IsGameplayKey(Key)) { Swallowing.Add(Key); }
			else { Swallowing.Remove(Key); }
			return false;
		}
		return Swallowing.Contains(Key) && !Settings->IsMenuUp();
	}

	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		Swallowing.Remove(InKeyEvent.GetKey());
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("TNHeldKeyGuard"); }

private:
	TWeakObjectPtr<UTN_GameSettingsSubsystem> Owner;
	TSet<FKey> Swallowing;
};

// ─────────────────────────────────────────────────────────────────────────────
// Ciclo de vida
// ─────────────────────────────────────────────────────────────────────────────

UTN_GameSettingsSubsystem* UTN_GameSettingsSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext && GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UTN_GameSettingsSubsystem>() : nullptr;
}

bool UTN_GameSettingsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UTN_GameSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TNGameSettingsDetail::FProcessBaseline& Base = TNGameSettingsDetail::Baseline();
	if (Base.Users++ == 0)
	{
		Base.DisplayGamma = GEngine ? GEngine->DisplayGamma : 2.2f;
		Base.ApplicationScale = GetDefault<UUserInterfaceSettings>()->ApplicationScale;
		if (const IConsoleVariable* PaniniD = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LensDistortion.Panini.D"))) { Base.PaniniD = PaniniD->GetFloat(); }
		if (const IConsoleVariable* PaniniS = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LensDistortion.Panini.S"))) { Base.PaniniS = PaniniS->GetFloat(); }
#if WITH_EDITOR
		// En el editor, la calidad, los FPS y la sincronización son los del editor: se guardan para devolverlos al acabar.
		Base.bEditor = GIsEditor;
		if (GIsEditor)
		{
			Base.QualityLevels = Scalability::GetQualityLevels();
			if (const IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) { Base.MaxFPS = MaxFps->GetFloat(); }
			if (const IConsoleVariable* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"))) { Base.VSync = VSync->GetInt(); }
		}
#endif
	}
	BaseDisplayGamma = Base.DisplayGamma;
	LoadSettings();
	CreateSoundClasses();
	OriginalMapping = LoadObject<UInputMappingContext>(nullptr, TNGameSettingsDetail::PlayerMappingPath);
	BuildDefaultBindings();
	FreeCameraKeyConflicts();
	RebuildRemappedMapping(PrimaryInput, Settings);
	// El idioma va lo primero: antes de que salga ningún menú, ni siquiera el de carga.
	SystemLanguage = TNLanguage::FindSystemLanguage();
	ApplyLanguage();
	ApplyGlobalSettings();
	if (FSlateApplication::IsInitialized())
	{
		HeldKeyGuard = MakeShared<FTNHeldKeyGuard>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(HeldKeyGuard);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Ajustes] Cargados: general %.0f%%, música %.0f%%, efectos %.0f%%, ambiente %.0f%%, voz %.0f%%, %s, %d teclas cambiadas, interfaz %.0f%%, idioma %s, ojo de pez %s."),
		Settings.MasterVolume * 100.f, Settings.MusicVolume * 100.f, Settings.EffectsVolume * 100.f, Settings.AmbientVolume * 100.f,
		Settings.VoiceVolume * 100.f, Settings.bPushToTalk ? TEXT("pulsar para hablar") : TEXT("voz abierta"), Settings.KeyOverrides.Num(),
		Settings.UIScale * 100.f, *AppliedLanguage, Settings.bFisheye ? TEXT("sí") : TEXT("no"));
}

void UTN_GameSettingsSubsystem::Deinitialize()
{
	if (bVideoModePending) { FinishVideoModeChange(false); }
	SaveNow();
	if (HeldKeyGuard.IsValid())
	{
		if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().UnregisterInputPreProcessor(HeldKeyGuard); }
		HeldKeyGuard.Reset();
	}
	if (UTN_PauseMenuWidget* Menu = PauseMenu.Get())
	{
		PauseMenu = nullptr;
		Menu->RemoveFromParent();
	}
	if (FpsWidget)
	{
		FpsWidget->RemoveFromParent();
		FpsWidget = nullptr;
	}
	if (TalkersWidget)
	{
		TalkersWidget->RemoveFromParent();
		TalkersWidget = nullptr;
	}
	SetWorldPaused(nullptr, false);
	auto ReleaseState = [](FTNPlayerInputState& State)
	{
		if (APlayerController* Owner = State.PauseInputOwner.Get())
		{
			if (State.PauseInput) { Owner->PopInputComponent(State.PauseInput); }
			if (APlayerCameraManager* Camera = Owner->PlayerCameraManager)
			{
				if (UCameraModifier* Fov = Camera->FindCameraModifierByClass(UTN_SettingsFovModifier::StaticClass())) { Camera->RemoveCameraModifier(Fov); }
			}
		}
		State.PauseInput = nullptr;
		State.PauseInputOwner.Reset();
		for (const TWeakObjectPtr<UCameraModifier>& Shake : State.DisabledShakes)
		{
			if (UCameraModifier* Modifier = Shake.Get()) { Modifier->EnableModifier(); }
		}
		State.DisabledShakes.Reset();
	};
	ReleaseState(PrimaryInput);
	for (FTNPlayerInputState& Guest : GuestInputs) { ReleaseState(Guest); }
	GuestInputs.Reset();

	// Lo que es de todo el proceso vuelve a como estaba al quitarse el último subsistema (en el juego da igual, se cierra
	// con él; en el editor, no).
	TNGameSettingsDetail::FProcessBaseline& Base = TNGameSettingsDetail::Baseline();
	Base.Users = FMath::Max(Base.Users - 1, 0);
	if (Base.Users == 0)
	{
		if (GEngine) { GEngine->DisplayGamma = Base.DisplayGamma; }
		GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale = Base.ApplicationScale;
		// Ojo de pez: la proyección Panini vuelve a como estaba (apagada), para que los visores del editor no se queden con ella.
		if (IConsoleVariable* PaniniD = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LensDistortion.Panini.D"))) { PaniniD->Set(Base.PaniniD, ECVF_SetByGameSetting); }
		if (IConsoleVariable* PaniniS = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LensDistortion.Panini.S"))) { PaniniS->Set(Base.PaniniS, ECVF_SetByGameSetting); }
		if (AppliedColorFilter != 0)
		{
			UWidgetBlueprintLibrary::SetColorVisionDeficiencyType(EColorVisionDeficiency::NormalVision, 0.f, false, false);
		}
#if WITH_EDITOR
		if (Base.bEditor)
		{
			Scalability::SetQualityLevels(Base.QualityLevels);
			if (IConsoleVariable* MaxFPS = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) { MaxFPS->Set(Base.MaxFPS, ECVF_SetByGameSetting); }
			if (IConsoleVariable* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"))) { VSync->Set(Base.VSync, ECVF_SetByGameSetting); }
			if (FAudioDevice* MainDevice = GEngine ? GEngine->GetMainAudioDeviceRaw() : nullptr) { MainDevice->SetTransientPrimaryVolume(1.f); }
		}
#endif
	}
	AppliedColorFilter = 0;
	Super::Deinitialize();
}

ETickableTickType UTN_GameSettingsSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UTN_GameSettingsSubsystem::IsTickable() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance && GameInstance->GetGameViewportClient();
}

TStatId UTN_GameSettingsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTN_GameSettingsSubsystem, STATGROUP_Tickables);
}

UWorld* UTN_GameSettingsSubsystem::GetTickableGameObjectWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

void UTN_GameSettingsSubsystem::Tick(float DeltaTime)
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World || World->bIsTearingDown)
	{
		return;
	}
	ApplyAudioVolumes(World, false);
	UpdateSounds(World);
	// La interfaz, con la escala de la pantalla partida (cambia al entrar o salir un jugador local).
	ApplyUIScale();

	// Cada jugador local (en red, uno; en la partida local, hasta cuatro): sus teclas, su entrada del menú y su cámara. Las
	// teclas: la tortuga vuelve a poner IMC_Player al poseerse (ClearAllMappings) y aquí se cambia por la copia.
	GuestInputs.RemoveAll([](const FTNPlayerInputState& Guest) { return !Guest.Player.IsValid(); });
	TArray<APlayerController*> LocalControllers;
	TNLocalViews::GetLocalControllers(World, LocalControllers);
	for (APlayerController* LocalPC : LocalControllers)
	{
		FTNPlayerInputState* State = StateFor(LocalPC);
		const FTNGameSettings* Own = OwnSettingsFor(LocalPC);
		if (!State || !Own)
		{
			continue;
		}
		UpdateInputMapping(LocalPC->GetLocalPlayer(), *State);
		EnsurePauseInput(LocalPC, *State, *Own);
		UpdateCamera(LocalPC, *State, *Own);
	}

	APlayerController* PC = GameInstance->GetFirstLocalPlayerController(World);
	UpdateFisheye(PC, DeltaTime);
	// El paisaje sonoro del jugador (en su PlayerController): si no trae clase propia, la de Ambiente; así también sus
	// sonidos de sustitución (assets, AmbienceData) bajan con Ambiente en vez de con Efectos.
	if (UTN_AmbientSoundscapeComponent* Soundscape = PC ? PC->FindComponentByClass<UTN_AmbientSoundscapeComponent>() : nullptr)
	{
		if (!Soundscape->SoundClassOverride) { Soundscape->SoundClassOverride = AmbientClass; }
	}
	UpdateLocalVoice(PC);
	UpdateFpsCounter(PC);
	UpdateTalkers(PC);

	// Guardado diferido: con el menú abierto se espera a que se cierre. Los ajustes propios, tras un fallo, esperan más
	// con cada intento (SaveRetryDelay) en vez de reintentarse en cada fotograma; lo gráfico va aparte y no depende de eso.
	if ((bSettingsDirty || bGraphicsDirty) && !IsPauseMenuOpen())
	{
		const double Idle = FApp::GetCurrentTime() - DirtySince;
		if (bSettingsDirty && Idle > TNSaveLogic::SaveRetryDelay(SettingsSaveFailures, TNGameSettingsDetail::AutoSaveDelay, TNGameSettingsDetail::AutoSaveMaxDelay))
		{
			SaveSettingsFile();
		}
		if (bGraphicsDirty && Idle > TNGameSettingsDetail::AutoSaveDelay)
		{
			SaveGraphicsSettings();
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Ajustes: cargar, guardar, cambiar
// ─────────────────────────────────────────────────────────────────────────────

void UTN_GameSettingsSubsystem::LoadSettings()
{
	using namespace TNGameSettingsDetail;
	if (UGameplayStatics::DoesSaveGameExist(SlotName, SlotUser))
	{
		if (const UTN_SettingsSaveGame* Saved = Cast<UTN_SettingsSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, SlotUser)))
		{
			Settings = Saved->Settings;
			switch (TNSettingsMigration::Migrate(Settings, Saved->Version))
			{
			case TNSaveLogic::EMigration::Upgrade:
				// Se vuelve a guardar sellado con la versión actual (en el Tick, con el guardado diferido).
				UE_LOG(LogTortunabo, Log, TEXT("[Ajustes] Guardado de la versión %d: pasa a la %d."),
					TNSettingsMigration::ResolveSavedVersion(Saved->Version), TNSaveLogic::SETTINGS_SAVE_VERSION);
				MarkDirty(false);
				break;
			case TNSaveLogic::EMigration::FromNewerBuild:
				// No se reescribe por cargarlo (no se marca sucio). Si el jugador cambia algo, SaveSettingsFile lo copia
				// antes aparte: esta build solo conoce sus campos y al guardar pierde los que no conoce.
				NewerFileVersion = Saved->Version;
				UE_LOG(LogTortunabo, Warning, TEXT("[Ajustes] Guardado de una build más nueva (versión %d, esta es la %d): se usa tal cual y no se toca mientras no se cambie nada; si se cambia algo, antes se copia a '%s'."),
					Saved->Version, TNSaveLogic::SETTINGS_SAVE_VERSION, *TNSaveLogic::BuildNewerBuildBackupSlotName(SlotName, Saved->Version));
				break;
			default:
				break;
			}
		}
	}
	ClampSettings(Settings);
}

void UTN_GameSettingsSubsystem::SaveNow()
{
	SaveSettingsFile();
	SaveGraphicsSettings();
}

void UTN_GameSettingsSubsystem::SaveSettingsFile()
{
	using namespace TNGameSettingsDetail;
	if (!bSettingsDirty)
	{
		return;
	}

	// Un fichero de una build más nueva se copia aparte antes de escribir encima (esta build no conoce todos sus campos).
	// Si la copia falla no se bloquea el guardado: el jugador cambió algo y tiene que quedar. Hasta que se escriba bien,
	// cada intento repite la copia (el fichero de la build nueva sigue en disco).
	if (NewerFileVersion > 0)
	{
		TNSaveGameIO::BackupSlot(SlotName, TNSaveLogic::BuildNewerBuildBackupSlotName(SlotName, NewerFileVersion), SlotUser, TEXT("Ajustes"));
	}

	bool bSaved = false;
	if (UTN_SettingsSaveGame* Save = Cast<UTN_SettingsSaveGame>(UGameplayStatics::CreateSaveGameObject(UTN_SettingsSaveGame::StaticClass())))
	{
		Save->Settings = Settings;
		Save->StampCurrentVersion();
		// Un reintento inmediato dentro y error en el log (SaveChecked); los reintentos espaciados van abajo.
		bSaved = TNSaveGameIO::SaveChecked(Save, SlotName, SlotUser, TEXT("Ajustes"));
	}

	if (bSaved)
	{
		if (SettingsSaveFailures > 0)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Ajustes] Guardados de nuevo tras %d fallos seguidos."), SettingsSaveFailures);
		}
		bSettingsDirty = false;
		SettingsSaveFailures = 0;
		// El fichero en disco ya es de esta build: la copia de la más nueva (si la había) se queda como está.
		NewerFileVersion = 0;
		return;
	}

	// Sigue sucio. La espera del Tick vuelve a contar desde ahora y se duplica con cada fallo hasta AutoSaveMaxDelay: si no,
	// reintentaría en cada fotograma (dos escrituras síncronas y dos líneas de log cada vez). El aviso sale una vez por
	// intento, no por fotograma. Un guardado forzado (SaveNow) no espera, pero cuenta igual.
	++SettingsSaveFailures;
	DirtySince = FApp::GetCurrentTime();
	UE_LOG(LogTortunabo, Warning, TEXT("[Ajustes] No se han podido guardar (fallo %d seguido); el siguiente intento automático es dentro de %.0f s."),
		SettingsSaveFailures, TNSaveLogic::SaveRetryDelay(SettingsSaveFailures, AutoSaveDelay, AutoSaveMaxDelay));
}

void UTN_GameSettingsSubsystem::SaveGraphicsSettings()
{
	// Una resolución sin confirmar no se guarda (si el juego se cerrase con ella, arrancaría otra vez así).
	if (bGraphicsDirty && !bVideoModePending)
	{
		if (UGameUserSettings* GUS = UGameUserSettings::GetGameUserSettings()) { GUS->SaveSettings(); }
		bGraphicsDirty = false;
	}
}

void UTN_GameSettingsSubsystem::MarkDirty(bool bGraphics)
{
	(bGraphics ? bGraphicsDirty : bSettingsDirty) = true;
	DirtySince = FApp::GetCurrentTime();
}

void UTN_GameSettingsSubsystem::EditSettings(TFunctionRef<void(FTNGameSettings&)> Edit)
{
	// Un invitado de la partida local: solo sus ajustes de jugador, en memoria (se aplican cada fotograma a su cámara).
	if (UTN_LocalPlayerProfile* Guest = GetEditedGuest())
	{
		FTNGameSettings& Own = Guest->GetGuestSettings();
		FTNGameSettings Edited = Own;
		Edit(Edited);
		TNGameSettingsDetail::ClampSettings(Edited);
		TNLocalPlay::CopyPerPlayerSettings(Edited, Own);
		return;
	}
	Edit(Settings);
	TNGameSettingsDetail::ClampSettings(Settings);
	MarkDirty(false);
	ApplyGlobalSettings();
}

const FTNGameSettings& UTN_GameSettingsSubsystem::GetEditedSettings() const
{
	if (const UTN_LocalPlayerProfile* Guest = GetEditedGuest())
	{
		return Guest->GetGuestSettings();
	}
	return Settings;
}

bool UTN_GameSettingsSubsystem::IsEditingGuest() const
{
	return GetEditedGuest() != nullptr;
}

FTNGameSettings UTN_GameSettingsSubsystem::GetSettingsFor(const APlayerController* PC) const
{
	const FTNGameSettings* Own = OwnSettingsFor(PC);
	return Own && Own != &Settings ? TNLocalPlay::EffectiveSettings(Settings, *Own) : Settings;
}

UTN_LocalPlayerProfile* UTN_GameSettingsSubsystem::GetEditedGuest() const
{
	APlayerController* Owner = GetPauseMenuOwner();
	return Owner && UTN_LocalPlaySubsystem::IsGuest(Owner) ? UTN_LocalPlayerProfile::Get(Owner) : nullptr;
}

FTNGameSettings& UTN_GameSettingsSubsystem::EditTarget()
{
	if (UTN_LocalPlayerProfile* Guest = GetEditedGuest())
	{
		return Guest->GetGuestSettings();
	}
	return Settings;
}

const FTNGameSettings* UTN_GameSettingsSubsystem::OwnSettingsFor(const APlayerController* PC) const
{
	if (!PC || !PC->IsLocalController())
	{
		return nullptr;
	}
	if (UTN_LocalPlaySubsystem::IsGuest(PC))
	{
		const UTN_LocalPlayerProfile* Guest = UTN_LocalPlayerProfile::Get(PC);
		return Guest ? &Guest->GetGuestSettings() : nullptr;
	}
	// El jugador 1 (en red, el único de la máquina). Otros jugadores locales fuera de la partida local (pruebas del
	// monkey): sin ajustes propios, como antes.
	return UTN_LocalPlaySubsystem::IsPrimaryPlayer(PC) ? &Settings : nullptr;
}

FTNPlayerInputState* UTN_GameSettingsSubsystem::StateFor(const APlayerController* PC)
{
	return const_cast<FTNPlayerInputState*>(static_cast<const UTN_GameSettingsSubsystem*>(this)->StateFor(PC));
}

const FTNPlayerInputState* UTN_GameSettingsSubsystem::StateFor(const APlayerController* PC) const
{
	if (!PC || !PC->IsLocalController())
	{
		return nullptr;
	}
	if (UTN_LocalPlaySubsystem::IsGuest(PC))
	{
		const ULocalPlayer* Player = PC->GetLocalPlayer();
		if (const FTNPlayerInputState* Found = GuestInputs.FindByPredicate([Player](const FTNPlayerInputState& Guest) { return Guest.Player.Get() == Player; }))
		{
			return Found;
		}
		// Primera vez que se ve a este invitado: su estado, con la copia de IMC_Player de sus teclas.
		UTN_GameSettingsSubsystem* Self = const_cast<UTN_GameSettingsSubsystem*>(this);
		FTNPlayerInputState& Added = Self->GuestInputs.AddDefaulted_GetRef();
		Added.Player = const_cast<ULocalPlayer*>(Player);
		if (const FTNGameSettings* Own = OwnSettingsFor(PC))
		{
			Self->RebuildRemappedMapping(Added, *Own);
		}
		return &Self->GuestInputs.Last();
	}
	return UTN_LocalPlaySubsystem::IsPrimaryPlayer(PC) ? &PrimaryInput : nullptr;
}

void UTN_GameSettingsSubsystem::ResetGroup(ETNSettingsGroup Group)
{
	const FTNGameSettings Defaults;
	// Los del menú: los del jugador 1 o, con un invitado de la partida local, los suyos (solo cuentan sus ajustes de jugador).
	const bool bGuest = IsEditingGuest();
	FTNGameSettings& Target = EditTarget();
	switch (Group)
	{
	case ETNSettingsGroup::Sound:
		Target.MasterVolume = Defaults.MasterVolume;
		Target.MusicVolume = Defaults.MusicVolume;
		Target.EffectsVolume = Defaults.EffectsVolume;
		Target.AmbientVolume = Defaults.AmbientVolume;
		Target.bMuteInBackground = Defaults.bMuteInBackground;
		break;
	case ETNSettingsGroup::Voice:
		Target.VoiceVolume = Defaults.VoiceVolume;
		Target.PlayerVoiceVolumes.Reset();
		Target.MutedPlayers.Reset();
		Target.bPushToTalk = Defaults.bPushToTalk;
		Target.bMicMuted = Defaults.bMicMuted;
		Target.MicSensitivity = Defaults.MicSensitivity;
		Target.MicGain = Defaults.MicGain;
		Target.CaptureDeviceId = Defaults.CaptureDeviceId;
		break;
	case ETNSettingsGroup::Controls:
		// Las teclas tienen su propio «Restablecer» (página de controles).
		Target.MouseSensitivity = Defaults.MouseSensitivity;
		Target.GamepadSensitivity = Defaults.GamepadSensitivity;
		Target.bInvertMouseY = Defaults.bInvertMouseY;
		Target.bInvertGamepadY = Defaults.bInvertGamepadY;
		Target.bGamepadVibration = Defaults.bGamepadVibration;
		break;
	case ETNSettingsGroup::Game:
		Target.bCameraShake = Defaults.bCameraShake;
		Target.FieldOfViewOffset = Defaults.FieldOfViewOffset;
		Target.ColorFilter = Defaults.ColorFilter;
		Target.ColorFilterStrength = Defaults.ColorFilterStrength;
		Target.UIScale = Defaults.UIScale;
		Target.bShowTalkers = Defaults.bShowTalkers;
		// Idioma sin elegir (el del sistema, o el español) y el ojo de pez de serie.
		Target.Language = Defaults.Language;
		Target.bFisheye = Defaults.bFisheye;
		Target.VRMode = Defaults.VRMode;
		Target.VRTurn = Defaults.VRTurn;
		Target.VRVignette = Defaults.VRVignette;
		Target.bVRHaptics = Defaults.bVRHaptics;
		Target.CameraView = Defaults.CameraView;
		break;
	default:
		// Gráficos: el brillo y el contador; la calidad se elige con «Calidad recomendada» (UGameUserSettings).
		Target.Brightness = Defaults.Brightness;
		Target.bShowFps = Defaults.bShowFps;
		break;
	}
	if (bGuest)
	{
		return;
	}
	MarkDirty(false);
	ApplyGlobalSettings();
}

void UTN_GameSettingsSubsystem::ResetAll()
{
	// Un invitado de la partida local: sus ajustes de jugador de serie.
	if (UTN_LocalPlayerProfile* Guest = GetEditedGuest())
	{
		Guest->GetGuestSettings() = FTNGameSettings();
		OnKeyBindingsChanged();
		return;
	}
	Settings = FTNGameSettings();
	UE_LOG(LogTortunabo, Log, TEXT("[Ajustes] Todos los ajustes propios, a los de serie."));
	// Teclas (y copia de IMC_Player), guardado y el resto aplicado.
	OnKeyBindingsChanged();
	ApplyGlobalSettings();
}

void UTN_GameSettingsSubsystem::ApplyGraphicsChange()
{
	if (UGameUserSettings* GUS = UGameUserSettings::GetGameUserSettings())
	{
		GUS->ApplyNonResolutionSettings();
		MarkDirty(true);
	}
}

bool UTN_GameSettingsSubsystem::CanChangeVideoMode()
{
	// En el editor la ventana es la del editor (y la de PIE): la resolución y el modo solo se cambian en el juego.
	return !GIsEditor;
}

void UTN_GameSettingsSubsystem::ApplyVideoMode()
{
	UGameUserSettings* GUS = UGameUserSettings::GetGameUserSettings();
	if (!GUS || !CanChangeVideoMode())
	{
		return;
	}
	GUS->ApplyResolutionSettings(false);
	bVideoModePending = true;
	MarkDirty(true);
}

void UTN_GameSettingsSubsystem::FinishVideoModeChange(bool bKeep)
{
	UGameUserSettings* GUS = UGameUserSettings::GetGameUserSettings();
	if (!GUS || !bVideoModePending)
	{
		return;
	}
	bVideoModePending = false;
	if (bKeep)
	{
		GUS->ConfirmVideoMode();
		GUS->SaveSettings();
	}
	else
	{
		// Vuelve a la última resolución y modo confirmados.
		GUS->RevertVideoMode();
		GUS->ApplyResolutionSettings(false);
	}
}

float UTN_GameSettingsSubsystem::SensitivityToThreshold(float Sensitivity, float BaseThreshold)
{
	// 0,5 → el umbral de serie; 0 → +20 dB (hay que hablar más alto); 1 → -20 dB (se oye hasta lo bajito).
	const float Db = FMath::Lerp(20.f, -20.f, FMath::Clamp(Sensitivity, 0.f, 1.f));
	return FMath::Max(BaseThreshold, 1e-5f) * FMath::Pow(10.f, Db / 20.f);
}

float UTN_GameSettingsSubsystem::GetSpeakingThreshold() const
{
	// El umbral de serie es el del componente de voz (su plantilla, por si la tortuga trae otro).
	const UProximityVoiceComponent* Voice = GetLocalVoice();
	const UProximityVoiceComponent* Template = Voice ? Cast<UProximityVoiceComponent>(Voice->GetArchetype()) : nullptr;
	const UProximityVoiceComponent* Base = Template ? Template : GetDefault<UProximityVoiceComponent>();
	return SensitivityToThreshold(Settings.MicSensitivity, Base->SpeakingThreshold);
}

float UTN_GameSettingsSubsystem::BrightnessToGamma(float Brightness) const
{
	// 0,5 es la gamma de serie del motor; cada extremo la mueve 0,7 (más gamma, imagen más clara).
	return FMath::Clamp(BaseDisplayGamma + (Brightness - 0.5f) * 1.4f, 1.2f, 3.6f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Idioma
// ─────────────────────────────────────────────────────────────────────────────

FString UTN_GameSettingsSubsystem::GetLanguage() const
{
	return AppliedLanguage.IsEmpty() ? (TNLanguage::IndexOf(Settings.Language) != INDEX_NONE ? Settings.Language : SystemLanguage) : AppliedLanguage;
}

void UTN_GameSettingsSubsystem::SetLanguage(const FString& Culture)
{
	const int32 Found = TNLanguage::IndexOf(Culture);
	const FString Chosen = Found != INDEX_NONE ? TNLanguage::GetLanguages()[Found].Culture : FString();
	EditSettings([&Chosen](FTNGameSettings& S) { S.Language = Chosen; });
	SaveNow();
}

void UTN_GameSettingsSubsystem::ApplyLanguage()
{
	// El guardado si sigue en la lista; sin elegir (o si se quitó de la lista), el del sistema (calculado al crearse).
	const int32 Found = TNLanguage::IndexOf(Settings.Language);
	const FString Wanted = Found != INDEX_NONE ? TNLanguage::GetLanguages()[Found].Culture : (SystemLanguage.IsEmpty() ? TNLanguage::GetNativeCulture() : SystemLanguage);
	if (Wanted == AppliedLanguage)
	{
		return;
	}
	AppliedLanguage = Wanted;
	TNLanguage::Apply(Wanted);
	UE_LOG(LogTortunabo, Log, TEXT("[Ajustes] Idioma: %s (%s)."), *Wanted, Found != INDEX_NONE ? TEXT("elegido") : TEXT("el del sistema"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Ojo de pez leve
// ─────────────────────────────────────────────────────────────────────────────

void UTN_GameSettingsSubsystem::UpdateFisheye(APlayerController* PC, float DeltaTime)
{
	using namespace TNGameSettingsDetail;
	static IConsoleVariable* const PaniniD = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LensDistortion.Panini.D"));
	static IConsoleVariable* const PaniniS = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LensDistortion.Panini.S"));
	if (!PaniniD)
	{
		return;
	}

	// Se enciende y se apaga poco a poco (así no salta al cambiar el ajuste). En VR nunca: deformar la imagen con gafas marea.
	FisheyeAmount = TNVR::IsEnabled() ? 0.f
		: FMath::FInterpConstantTo(FisheyeAmount, Settings.bFisheye ? 1.f : 0.f, DeltaTime, 1.f / FisheyeFadeSeconds);

	float D = 0.f;
	float S = 0.f;
	if (FisheyeAmount > KINDA_SMALL_NUMBER)
	{
		// Correr abre el campo de visión: la distancia se afloja para que el borde se comprima igual que en reposo.
		float BaseD = FMath::Clamp(CVarFisheyeD.GetValueOnGameThread(), 0.f, 1.f);
		const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager.Get() : nullptr;
		const ATortugaCharacter* Turtle = PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
		if (Camera && Turtle)
		{
			BaseD = FisheyeDistanceForFov(BaseD, Turtle->GetCameraFOVDefault(), Camera->GetFOVAngle());
		}
		const float Eased = FMath::SmoothStep(0.f, 1.f, FisheyeAmount);
		D = BaseD * Eased;
		S = FMath::Clamp(CVarFisheyeS.GetValueOnGameThread(), 0.f, 1.f) * Eased;
	}
	if (!FMath::IsNearlyEqual(D, AppliedPaniniD, 0.0005f))
	{
		AppliedPaniniD = D;
		PaniniD->Set(D, ECVF_SetByGameSetting);
		if (PaniniS) { PaniniS->Set(S, ECVF_SetByGameSetting); }
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Cámara (getters para cualquier cámara, también la del espectador)
// ─────────────────────────────────────────────────────────────────────────────

float UTN_GameSettingsSubsystem::GetLookSensitivity(bool bGamepad) const
{
	return bGamepad ? Settings.GamepadSensitivity : Settings.MouseSensitivity;
}

bool UTN_GameSettingsSubsystem::IsLookYInverted(bool bGamepad) const
{
	return bGamepad ? Settings.bInvertGamepadY : Settings.bInvertMouseY;
}

bool UTN_GameSettingsSubsystem::IsUsingGamepad(const APlayerController* PC)
{
	// El mismo aparato que los avisos de botones (#347): el UInputDeviceSubsystem del motor no volvía a «teclado» después
	// de tocar el mando (el teclado y el primer mando son el mismo aparato 0).
	const UTN_InputDeviceSubsystem* Devices = UTN_InputDeviceSubsystem::Get(PC);
	return PC && Devices && Devices->IsUsingGamepad(PC);
}

float UTN_GameSettingsSubsystem::GetLookSensitivityFor(const APlayerController* PC, bool bGamepad) const
{
	const FTNGameSettings* Own = OwnSettingsFor(PC);
	const FTNGameSettings& Use = Own ? *Own : Settings;
	return bGamepad ? Use.GamepadSensitivity : Use.MouseSensitivity;
}

bool UTN_GameSettingsSubsystem::IsLookYInvertedFor(const APlayerController* PC, bool bGamepad) const
{
	const FTNGameSettings* Own = OwnSettingsFor(PC);
	const FTNGameSettings& Use = Own ? *Own : Settings;
	return bGamepad ? Use.bInvertGamepadY : Use.bInvertMouseY;
}

float UTN_GameSettingsSubsystem::GetFieldOfViewOffsetFor(const APlayerController* PC) const
{
	const FTNGameSettings* Own = OwnSettingsFor(PC);
	return Own ? Own->FieldOfViewOffset : Settings.FieldOfViewOffset;
}

FVector2D UTN_GameSettingsSubsystem::ApplyLookSettings(const APlayerController* PC, const FVector2D& RawLook) const
{
	const bool bPad = IsUsingGamepad(PC);
	const float Sensitivity = GetLookSensitivityFor(PC, bPad);
	return FVector2D(RawLook.X * Sensitivity, RawLook.Y * Sensitivity * (IsLookYInvertedFor(PC, bPad) ? -1.f : 1.f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Sonido
// ─────────────────────────────────────────────────────────────────────────────

void UTN_GameSettingsSubsystem::CreateSoundClasses()
{
	auto MakeClass = [this](const TCHAR* Name)
	{
		// Raíces propias (sin padre): la mezcla de los efectos, que baja la clase por defecto del motor, no las toca.
		USoundClass* Class = NewObject<USoundClass>(this, FName(Name), RF_Transient);
		Class->Properties.Volume = 1.f;
		return Class;
	};
	MusicClass = MakeClass(TEXT("TN_Music"));
	MusicClass->Properties.bIsMusic = true;
	AmbientClass = MakeClass(TEXT("TN_Ambient"));
	VoiceClass = MakeClass(TEXT("TN_Voice"));
	EffectsMix = NewObject<USoundMix>(this, TEXT("TN_EffectsMix"), RF_Transient);
	// Las clases creadas en ejecución no se apuntan solas en los dispositivos de audio (solo las que se cargan).
	if (FAudioDeviceManager* Devices = GEngine ? GEngine->GetAudioDeviceManager() : nullptr)
	{
		Devices->RegisterSoundClass(MusicClass);
		Devices->RegisterSoundClass(AmbientClass);
		Devices->RegisterSoundClass(VoiceClass);
	}
}

void UTN_GameSettingsSubsystem::ApplyGlobalSettings()
{
	// Idioma (solo hace algo si ha cambiado).
	ApplyLanguage();

	// Clases propias: el dispositivo lee su volumen en cada actualización.
	if (MusicClass) { MusicClass->Properties.Volume = Settings.MusicVolume; }
	if (AmbientClass) { AmbientClass->Properties.Volume = Settings.AmbientVolume; }
	if (VoiceClass) { VoiceClass->Properties.Volume = Settings.VoiceVolume; }
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		ApplyAudioVolumes(GameInstance->GetWorld(), false);
	}

	// Brillo: gamma de salida del motor (la que usa el tonemapper).
	if (GEngine) { GEngine->DisplayGamma = BrightnessToGamma(Settings.Brightness); }

	// Filtro para daltónicos (Slate lo aplica a la imagen final de la ventana: el juego y el HUD, marcadores incluidos). Sin
	// filtro no se toca nada (en el editor se respeta la vista previa de daltonismo que tenga puesta).
	const bool bFilterTypeChanged = AppliedColorFilter != Settings.ColorFilter;
	const bool bFilterStrengthChanged = Settings.ColorFilter != 0 && !FMath::IsNearlyEqual(AppliedColorFilterStrength, Settings.ColorFilterStrength);
	if (bFilterTypeChanged || bFilterStrengthChanged)
	{
		AppliedColorFilter = Settings.ColorFilter;
		AppliedColorFilterStrength = Settings.ColorFilterStrength;
		const EColorVisionDeficiency Type = static_cast<EColorVisionDeficiency>(Settings.ColorFilter);
		UWidgetBlueprintLibrary::SetColorVisionDeficiencyType(Type, Type == EColorVisionDeficiency::NormalVision ? 0.f : Settings.ColorFilterStrength,
			Type != EColorVisionDeficiency::NormalVision, false);
	}

	ApplyUIScale();

	// Micrófono elegido: lo abre la voz al empezar (reaparecer, cambiar de mapa).
	UProximityVoiceComponent::SetPreferredCaptureDevice(Settings.CaptureDeviceId);
}

void UTN_GameSettingsSubsystem::ApplyUIScale()
{
	// Tamaño de la interfaz: la escala extra de UUserInterfaceSettings, que solo multiplica la escala DPI del viewport del
	// juego (la interfaz del editor tiene la suya). Se lee cada fotograma: cambia al momento. Con la pantalla partida de la
	// partida local, más pequeña (TNLocalPlay::UIScaleForViews): el HUD de cada jugador cabe en su trozo.
	const UTN_LocalPlaySubsystem* LocalPlay = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTN_LocalPlaySubsystem>() : nullptr;
	const float Split = LocalPlay ? LocalPlay->GetSplitUIScale() : 1.f;
	const float WantedScale = TNGameSettingsDetail::Baseline().ApplicationScale * Settings.UIScale * Split;
	if (!FMath::IsNearlyEqual(AppliedUIScale, WantedScale))
	{
		GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale = WantedScale;
		AppliedUIScale = WantedScale;
	}
}

void UTN_GameSettingsSubsystem::ApplyAudioVolumes(UWorld* World, bool bForce)
{
	if (!World || World->bIsTearingDown)
	{
		return;
	}
	const bool bNewWorld = AudioWorld.Get() != World;
	FAudioDevice* Device = World->GetAudioDeviceRaw();
	if (bNewWorld)
	{
		// Mundo nuevo (viaje, otra partida en el editor): las clases propias en su dispositivo y la mezcla de los efectos.
		if (FAudioDeviceManager* Devices = GEngine ? GEngine->GetAudioDeviceManager() : nullptr)
		{
			if (MusicClass) { Devices->RegisterSoundClass(MusicClass); }
			if (AmbientClass) { Devices->RegisterSoundClass(AmbientClass); }
			if (VoiceClass) { Devices->RegisterSoundClass(VoiceClass); }
		}
		if (EffectsMix) { UGameplayStatics::PushSoundMixModifier(World, EffectsMix); }
		AudioWorld = World;
	}
	// Con «Silenciar sin el foco de la ventana», a 0 mientras la ventana del juego no está activa.
	const bool bMutedByFocus = Settings.bMuteInBackground && FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsActive();
	const float Master = bMutedByFocus ? 0.f : Settings.MasterVolume;
	if (Device && (bNewWorld || bForce || !FMath::IsNearlyEqual(AppliedMasterVolume, Master)))
	{
		Device->SetTransientPrimaryVolume(Master);
		AppliedMasterVolume = Master;
	}
	if (EffectsMix && (bNewWorld || bForce || !FMath::IsNearlyEqual(AppliedEffectsVolume, Settings.EffectsVolume)))
	{
		// Efectos: todo lo que se queda en la clase por defecto del motor (y sus hijas).
		if (USoundClass* DefaultClass = GetDefault<UAudioSettings>()->GetDefaultSoundClass())
		{
			UGameplayStatics::SetSoundMixClassOverride(World, EffectsMix, DefaultClass, Settings.EffectsVolume, 1.f, 0.05f, true);
		}
		AppliedEffectsVolume = Settings.EffectsVolume;
	}
}

USoundClass* UTN_GameSettingsSubsystem::ClassFor(const UAudioComponent* Component) const
{
	const UObject* Outer = Component ? Component->GetOuter() : nullptr;
	if (!Outer)
	{
		return nullptr;
	}
	// El UAudioComponent de un sintetizador es suyo (USynthComponent lo crea con él de Outer).
	if (Outer->IsA<UTN_MusicSynthComponent>())
	{
		return MusicClass;
	}
	if (Outer->IsA<UTN_AmbientSynthComponent>())
	{
		return AmbientClass;
	}
	// Sonidos de fondo de la carrera hechos con el sintetizador de las criaturas (burbujas del pulpo): son ambiente.
	if (Outer->IsA<UTN_BeachCritterSynthComponent>())
	{
		if (static_cast<const UTN_BeachCritterSynthComponent*>(Outer)->bAmbientBed)
		{
			return AmbientClass;
		}
		return nullptr;
	}
	// La voz de un compañero: onda procedural del grupo de voz (UProximityVoiceComponent::SetupPlayback).
	const USoundWaveProcedural* Wave = Cast<USoundWaveProcedural>(Component->Sound);
	if (Wave && Wave->SoundGroup == SOUNDGROUP_Voice && !Outer->IsA<USynthComponent>())
	{
		return VoiceClass;
	}
	return nullptr;
}

void UTN_GameSettingsSubsystem::UpdateSounds(UWorld* World)
{
	// Cada fotograma: los sonidos hechos en código (no los assets, que no se tocan) van a su clase, y la voz de cada
	// compañero, a su volumen. El dispositivo lee la clase del sonido en cada actualización, así que vale aunque ya suene
	// y para los sintetizadores que se crean más tarde (tienda, probador, fin de partida).
	ForEachObjectOfClass(UAudioComponent::StaticClass(), [this, World](UObject* Object)
	{
		UAudioComponent* Component = static_cast<UAudioComponent*>(Object);
		if (!IsValid(Component) || Component->GetWorld() != World)
		{
			return;
		}
		USoundBase* Sound = Component->Sound;
		if (!Sound || Sound->IsAsset())
		{
			return;
		}
		USoundClass* Wanted = ClassFor(Component);
		if (!Wanted)
		{
			return;
		}
		if (Sound->SoundClassObject != Wanted)
		{
			Sound->SoundClassObject = Wanted;
		}
		// Un sintetizador con clase propia la pone de sustituta en su componente (USynthComponent::Start) y esa manda: se
		// cambia también (las nuestras no traen ninguna).
		if (Component->SoundClassOverride && Component->SoundClassOverride != Wanted && Wanted != VoiceClass)
		{
			Component->SoundClassOverride = Wanted;
		}
		if (Wanted == VoiceClass)
		{
			const APawn* Speaker = Cast<APawn>(Component->GetOwner());
			const UProximityVoiceComponent* Voice = Speaker ? Speaker->FindComponentByClass<UProximityVoiceComponent>() : nullptr;
			if (!Voice)
			{
				return;
			}
			// El jugador de esa voz: el del peón o, si el peón ya no tiene (sin poseer, o aún sin replicar), el último que tuvo;
			// con la clave vacía la voz saldría sin silenciar a volumen pleno (#248).
			const FString Key = PlayerKey(Voice->GetSpeakerState());
			const float Target = Voice->PlaybackVolume * (IsPlayerMuted(Key) ? 0.f : GetPlayerVoiceVolume(Key));
			if (!FMath::IsNearlyEqual(Component->VolumeMultiplier, Target, 0.001f))
			{
				Component->SetVolumeMultiplier(Target);
			}
		}
	}, true, RF_ClassDefaultObject | RF_ArchetypeObject);
}

// ─────────────────────────────────────────────────────────────────────────────
// Voz
// ─────────────────────────────────────────────────────────────────────────────

FString UTN_GameSettingsSubsystem::PlayerKey(const APlayerState* PlayerState)
{
	if (!PlayerState)
	{
		return FString();
	}
	const FUniqueNetIdRepl& Id = PlayerState->GetUniqueId();
	if (Id.IsValid())
	{
		return Id.ToString();
	}
	return FString(TEXT("Nombre:")) + PlayerState->GetPlayerName();
}

float UTN_GameSettingsSubsystem::GetPlayerVoiceVolume(const FString& Key) const
{
	const float* Found = Key.IsEmpty() ? nullptr : Settings.PlayerVoiceVolumes.Find(Key);
	return Found ? *Found : 1.f;
}

void UTN_GameSettingsSubsystem::SetPlayerVoiceVolume(const FString& Key, float Volume)
{
	if (Key.IsEmpty())
	{
		return;
	}
	EditSettings([&Key, Volume](FTNGameSettings& S)
	{
		if (FMath::IsNearlyEqual(Volume, 1.f)) { S.PlayerVoiceVolumes.Remove(Key); }
		else { S.PlayerVoiceVolumes.Add(Key, Volume); }
	});
}

bool UTN_GameSettingsSubsystem::IsPlayerMuted(const FString& Key) const
{
	return !Key.IsEmpty() && Settings.MutedPlayers.Contains(Key);
}

void UTN_GameSettingsSubsystem::SetPlayerMuted(const FString& Key, bool bMuted)
{
	if (Key.IsEmpty())
	{
		return;
	}
	EditSettings([&Key, bMuted](FTNGameSettings& S)
	{
		if (bMuted) { S.MutedPlayers.AddUnique(Key); }
		else { S.MutedPlayers.Remove(Key); }
	});
}

UProximityVoiceComponent* UTN_GameSettingsSubsystem::GetLocalVoice() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	const APlayerController* PC = World ? GameInstance->GetFirstLocalPlayerController(World) : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	return Pawn ? Pawn->FindComponentByClass<UProximityVoiceComponent>() : nullptr;
}

float UTN_GameSettingsSubsystem::GetMicLevel() const
{
	const UProximityVoiceComponent* Voice = GetLocalVoice();
	return Voice ? Voice->GetMicLevel() : 0.f;
}

bool UTN_GameSettingsSubsystem::IsMicCapturing() const
{
	const UProximityVoiceComponent* Voice = GetLocalVoice();
	return Voice && Voice->IsCapturing();
}

void UTN_GameSettingsSubsystem::SetCaptureDevice(const FString& DeviceId)
{
	EditSettings([&DeviceId](FTNGameSettings& S) { S.CaptureDeviceId = DeviceId; });
	UE_LOG(LogTortunabo, Log, TEXT("[Ajustes] Micrófono elegido: %s (se abre al reaparecer o al cambiar de mapa)."),
		DeviceId.IsEmpty() ? TEXT("el predeterminado de Windows") : *DeviceId);
}

bool UTN_GameSettingsSubsystem::IsCaptureDeviceChangePending() const
{
	const UProximityVoiceComponent* Voice = GetLocalVoice();
	return Voice && Voice->IsCapturing() && Voice->GetOpenCaptureDevice() != Settings.CaptureDeviceId;
}

void UTN_GameSettingsSubsystem::UpdateLocalVoice(APlayerController* PC)
{
	// ¿Sale la voz? Silenciado, no; con pulsar para hablar, solo con la tecla (o el botón del mando) pulsada.
	bool bTalkKeyDown = false;
	if (PC && Settings.bPushToTalk)
	{
		const FKey Key(Settings.PushToTalkKey);
		const FKey PadKey(Settings.PushToTalkPadKey);
		bTalkKeyDown = (Key.IsValid() && PC->IsInputKeyDown(Key)) || (PadKey.IsValid() && PC->IsInputKeyDown(PadKey));
		// En VR, pulsando el stick izquierdo (Docs/Modo_VR.md).
		bTalkKeyDown = bTalkKeyDown || (TNVR::IsEnabled() && FTNVRKeys::LeftStickClick.IsValid() && PC->IsInputKeyDown(FTNVRKeys::LeftStickClick));
	}
	bTransmitAllowed = !Settings.bMicMuted && (!Settings.bPushToTalk || bTalkKeyDown);

	UProximityVoiceComponent* Voice = GetLocalVoice();
	if (!Voice)
	{
		return;
	}
	Voice->SetTransmitEnabled(bTransmitAllowed);
	// Umbral y ganancia: los de serie del componente (su plantilla) con la sensibilidad y la ganancia del jugador.
	const UProximityVoiceComponent* Template = Cast<UProximityVoiceComponent>(Voice->GetArchetype());
	const UProximityVoiceComponent* Base = Template ? Template : GetDefault<UProximityVoiceComponent>();
	Voice->SpeakingThreshold = SensitivityToThreshold(Settings.MicSensitivity, Base->SpeakingThreshold);
	Voice->VoiceGain = Base->VoiceGain * Settings.MicGain;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cámara, contador de FPS y quién habla
// ─────────────────────────────────────────────────────────────────────────────

void UTN_GameSettingsSubsystem::UpdateCamera(APlayerController* PC, FTNPlayerInputState& State, const FTNGameSettings& Own)
{
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	// Sensibilidad e inversión: escalas de giro del PlayerController (UInputSettings::bEnableLegacyInputScales está
	// activo en DefaultInput.ini), con los valores del último aparato usado. Valen para la tortuga y para cualquier
	// cámara que gire con AddControllerYawInput/AddControllerPitchInput (la cámara libre del fantasma lee los getters).
	// Con la pantalla partida, cada jugador los suyos (Own).
	const bool bPad = IsUsingGamepad(PC);
	const APlayerController* Defaults = PC->GetClass()->GetDefaultObject<APlayerController>();
	const float Sensitivity = bPad ? Own.GamepadSensitivity : Own.MouseSensitivity;
	const bool bInvert = bPad ? Own.bInvertGamepadY : Own.bInvertMouseY;
	PC->InputYawScale_DEPRECATED = Defaults->InputYawScale_DEPRECATED * Sensitivity;
	PC->InputPitchScale_DEPRECATED = Defaults->InputPitchScale_DEPRECATED * Sensitivity * (bInvert ? -1.f : 1.f);

	// Campo de visión: el de la clase de la tortuga más el desplazamiento (en reposo y al correr).
	if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PC->GetPawn()))
	{
		const ATortugaCharacter* TurtleDefaults = Turtle->GetClass()->GetDefaultObject<ATortugaCharacter>();
		Turtle->SetCameraFOVs(FMath::Clamp(TurtleDefaults->GetCameraFOVDefault() + Own.FieldOfViewOffset, 40.f, 120.f),
			FMath::Clamp(TurtleDefaults->GetCameraFOVSprint() + Own.FieldOfViewOffset, 40.f, 120.f));
	}

	APlayerCameraManager* Camera = PC->PlayerCameraManager;
	if (!Camera)
	{
		return;
	}
	// Mirando a otra tortuga (espectador): el desplazamiento lo suma un modificador propio, el último de la lista.
	if (!Camera->FindCameraModifierByClass(UTN_SettingsFovModifier::StaticClass()))
	{
		if (UTN_SettingsFovModifier* Fov = Cast<UTN_SettingsFovModifier>(Camera->AddNewCameraModifier(UTN_SettingsFovModifier::StaticClass())))
		{
			Fov->SetSettings(this);
		}
	}

	// Temblor de cámara: los modificadores que tiemblan, apagados (o encendidos otra vez los que se apagaron aquí). En VR,
	// siempre apagados: mover la vista sin mover la cabeza marea.
	if (!Own.bCameraShake || TNVR::IsEnabled())
	{
		Camera->ForEachCameraModifier([&State](UCameraModifier* Modifier)
		{
			if (TNGameSettingsDetail::IsShakeModifier(Modifier) && !Modifier->IsDisabled())
			{
				Modifier->DisableModifier(true);
				State.DisabledShakes.AddUnique(TWeakObjectPtr<UCameraModifier>(Modifier));
			}
			return true;
		});
	}
	else if (State.DisabledShakes.Num() > 0)
	{
		for (const TWeakObjectPtr<UCameraModifier>& Shake : State.DisabledShakes)
		{
			if (UCameraModifier* Modifier = Shake.Get()) { Modifier->EnableModifier(); }
		}
		State.DisabledShakes.Reset();
	}
}

void UTN_GameSettingsSubsystem::UpdateFpsCounter(APlayerController* PC)
{
	if (!Settings.bShowFps)
	{
		if (FpsWidget && TNVR::IsOnScreen(FpsWidget)) { FpsWidget->RemoveFromParent(); }
		return;
	}
	if (!FpsWidget)
	{
		FpsWidget = CreateWidget<UTN_FpsCounterWidget>(GetGameInstance(), UTN_FpsCounterWidget::StaticClass());
	}
	// Tras un viaje el mundo quita todos los widgets: se vuelve a poner.
	if (FpsWidget && !TNVR::IsOnScreen(FpsWidget))
	{
		TNVR::AddToFullScreen(FpsWidget, TNGameSettingsDetail::FpsZOrder);
	}
}

void UTN_GameSettingsSubsystem::UpdateTalkers(APlayerController* PC)
{
	// Solo en la partida (lobby y Rally incluidos: los controladores que reciben voz), no en el menú principal.
	if (!Settings.bShowTalkers || !Cast<ITN_VoiceListener>(PC))
	{
		if (TalkersWidget && TNVR::IsOnScreen(TalkersWidget)) { TalkersWidget->RemoveFromParent(); }
		return;
	}
	if (!TalkersWidget)
	{
		TalkersWidget = CreateWidget<UTN_TalkersWidget>(GetGameInstance(), UTN_TalkersWidget::StaticClass());
	}
	if (TalkersWidget && !TNVR::IsOnScreen(TalkersWidget))
	{
		TNVR::AddToFullScreen(TalkersWidget, TNGameSettingsDetail::TalkersZOrder);
	}
}

UTN_SettingsFovModifier::UTN_SettingsFovModifier()
{
	// El último: después de la cámara del fantasma (0) y de los temblores.
	Priority = 250;
}

bool UTN_SettingsFovModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	const UTN_GameSettingsSubsystem* Owner = SettingsOwner.Get();
	const APlayerController* PC = CameraOwner ? CameraOwner->GetOwningPlayerController() : nullptr;
	const ATortugaCharacter* Watched = Cast<ATortugaCharacter>(CameraOwner ? CameraOwner->GetViewTarget() : nullptr);
	if (IsDisabled() || !Owner || !PC || !Watched || Watched->IsLocallyControlled())
	{
		return false;
	}
	// Solo la tortuga de otro jugador: la propia (también su cuerpo tras caer) ya lleva el desplazamiento en su cámara.
	const APlayerState* WatchedState = Watched->GetPlayerState();
	const float Offset = Owner->GetFieldOfViewOffsetFor(PC);
	if (WatchedState && WatchedState != PC->PlayerState && !FMath::IsNearlyZero(Offset))
	{
		InOutPOV.FOV = FMath::Clamp(InOutPOV.FOV + Offset, 40.f, 130.f);
	}
	return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Teclas y botones
// ─────────────────────────────────────────────────────────────────────────────

FText UTN_GameSettingsSubsystem::KeyDisplayName(const FKey& Key)
{
	if (!Key.IsValid())
	{
		return NSLOCTEXT("TNSettings", "NoKey", "—");
	}
	// Los botones de los mandos Touch de las gafas (OculusTouch_*): «Gatillo derecho», «A», «Stick izquierdo»... (#644).
	if (FTNVRKeys::IsVRKey(Key))
	{
		const FText VRName = TNVRControls::KeyName(Key);
		if (!VRName.IsEmpty())
		{
			return VRName;
		}
	}
	// Los nombres de las teclas del juego, por su nombre de tecla. Una entrada por texto: las que se llaman igual comparten clave.
	// Estático local (no de archivo): los NSLOCTEXT se crean con el sistema de localización ya en marcha.
	static const TMap<FName, FText> Names = {
		{ TEXT("SpaceBar"), NSLOCTEXT("TNKeys", "Space", "Espacio") },
		{ TEXT("LeftShift"), NSLOCTEXT("TNKeys", "LeftShift", "Mayús izq.") }, { TEXT("RightShift"), NSLOCTEXT("TNKeys", "RightShift", "Mayús der.") },
		{ TEXT("LeftControl"), NSLOCTEXT("TNKeys", "LeftCtrl", "Ctrl izq.") }, { TEXT("RightControl"), NSLOCTEXT("TNKeys", "RightCtrl", "Ctrl der.") },
		{ TEXT("LeftAlt"), NSLOCTEXT("TNKeys", "Alt", "Alt") }, { TEXT("RightAlt"), NSLOCTEXT("TNKeys", "AltGr", "Alt Gr") },
		{ TEXT("BackSpace"), NSLOCTEXT("TNKeys", "Backspace", "Retroceso") }, { TEXT("Escape"), NSLOCTEXT("TNKeys", "Escape", "Esc") },
		{ TEXT("Tab"), NSLOCTEXT("TNKeys", "Tab", "Tab") }, { TEXT("Enter"), NSLOCTEXT("TNKeys", "Enter", "Intro") },
		{ TEXT("CapsLock"), NSLOCTEXT("TNKeys", "CapsLock", "Bloq Mayús") },
		{ TEXT("PageUp"), NSLOCTEXT("TNKeys", "PageUp", "Re Pág") }, { TEXT("PageDown"), NSLOCTEXT("TNKeys", "PageDown", "Av Pág") },
		{ TEXT("Delete"), NSLOCTEXT("TNKeys", "Delete", "Supr") }, { TEXT("Insert"), NSLOCTEXT("TNKeys", "Insert", "Insert") },
		{ TEXT("Home"), NSLOCTEXT("TNKeys", "Home", "Inicio") }, { TEXT("End"), NSLOCTEXT("TNKeys", "End", "Fin") },
		{ TEXT("Up"), NSLOCTEXT("TNKeys", "ArrowUp", "Flecha arriba") }, { TEXT("Down"), NSLOCTEXT("TNKeys", "ArrowDown", "Flecha abajo") },
		{ TEXT("Left"), NSLOCTEXT("TNKeys", "ArrowLeft", "Flecha izquierda") }, { TEXT("Right"), NSLOCTEXT("TNKeys", "ArrowRight", "Flecha derecha") },
		{ TEXT("Mouse2D"), NSLOCTEXT("TNKeys", "Mouse", "Ratón") }, { TEXT("MouseX"), NSLOCTEXT("TNKeys", "Mouse", "Ratón") },
		{ TEXT("MouseY"), NSLOCTEXT("TNKeys", "Mouse", "Ratón") },
		{ TEXT("LeftMouseButton"), NSLOCTEXT("TNKeys", "MouseLeft", "Clic izquierdo") }, { TEXT("RightMouseButton"), NSLOCTEXT("TNKeys", "MouseRight", "Clic derecho") },
		{ TEXT("MiddleMouseButton"), NSLOCTEXT("TNKeys", "MouseMiddle", "Clic de la rueda") },
		{ TEXT("ThumbMouseButton"), NSLOCTEXT("TNKeys", "MouseSide1", "Botón lateral 1") }, { TEXT("ThumbMouseButton2"), NSLOCTEXT("TNKeys", "MouseSide2", "Botón lateral 2") },
		{ TEXT("MouseScrollUp"), NSLOCTEXT("TNKeys", "Wheel", "Rueda") }, { TEXT("MouseScrollDown"), NSLOCTEXT("TNKeys", "Wheel", "Rueda") },
		{ TEXT("MouseWheelAxis"), NSLOCTEXT("TNKeys", "Wheel", "Rueda") },
		{ TEXT("Gamepad_Left2D"), NSLOCTEXT("TNKeys", "PadLeftStick", "Stick izquierdo") }, { TEXT("Gamepad_Right2D"), NSLOCTEXT("TNKeys", "PadRightStick", "Stick derecho") },
		{ TEXT("Gamepad_LeftX"), NSLOCTEXT("TNKeys", "PadLeftStick", "Stick izquierdo") }, { TEXT("Gamepad_LeftY"), NSLOCTEXT("TNKeys", "PadLeftStick", "Stick izquierdo") },
		{ TEXT("Gamepad_RightX"), NSLOCTEXT("TNKeys", "PadRightStick", "Stick derecho") }, { TEXT("Gamepad_RightY"), NSLOCTEXT("TNKeys", "PadRightStick", "Stick derecho") },
		{ TEXT("Gamepad_FaceButton_Bottom"), NSLOCTEXT("TNKeys", "PadFaceBottom", "A / Cruz") }, { TEXT("Gamepad_FaceButton_Right"), NSLOCTEXT("TNKeys", "PadFaceRight", "B / Círculo") },
		{ TEXT("Gamepad_FaceButton_Left"), NSLOCTEXT("TNKeys", "PadFaceLeft", "X / Cuadrado") }, { TEXT("Gamepad_FaceButton_Top"), NSLOCTEXT("TNKeys", "PadFaceTop", "Y / Triángulo") },
		{ TEXT("Gamepad_LeftShoulder"), NSLOCTEXT("TNKeys", "PadLeftShoulder", "LB / L1") }, { TEXT("Gamepad_RightShoulder"), NSLOCTEXT("TNKeys", "PadRightShoulder", "RB / R1") },
		{ TEXT("Gamepad_LeftTrigger"), NSLOCTEXT("TNKeys", "PadLeftTrigger", "LT / L2") }, { TEXT("Gamepad_RightTrigger"), NSLOCTEXT("TNKeys", "PadRightTrigger", "RT / R2") },
		{ TEXT("Gamepad_LeftTriggerAxis"), NSLOCTEXT("TNKeys", "PadLeftTrigger", "LT / L2") }, { TEXT("Gamepad_RightTriggerAxis"), NSLOCTEXT("TNKeys", "PadRightTrigger", "RT / R2") },
		{ TEXT("Gamepad_DPad_Up"), NSLOCTEXT("TNKeys", "PadDpadUp", "Cruceta arriba") }, { TEXT("Gamepad_DPad_Down"), NSLOCTEXT("TNKeys", "PadDpadDown", "Cruceta abajo") },
		{ TEXT("Gamepad_DPad_Left"), NSLOCTEXT("TNKeys", "PadDpadLeft", "Cruceta izquierda") }, { TEXT("Gamepad_DPad_Right"), NSLOCTEXT("TNKeys", "PadDpadRight", "Cruceta derecha") },
		{ TEXT("Gamepad_LeftThumbstick"), NSLOCTEXT("TNKeys", "PadLeftStickClick", "Clic stick izquierdo") },
		{ TEXT("Gamepad_RightThumbstick"), NSLOCTEXT("TNKeys", "PadRightStickClick", "Clic stick derecho") },
		{ TEXT("Gamepad_Special_Right"), NSLOCTEXT("TNKeys", "PadStart", "Start / Menú") }, { TEXT("Gamepad_Special_Left"), NSLOCTEXT("TNKeys", "PadSelect", "Select / Vista") },
	};
	if (const FText* Found = Names.Find(Key.GetFName()))
	{
		return *Found;
	}
	// Cualquier otra tecla (letras, números, F1...): el nombre que da el motor, que ya viene localizado.
	return Key.GetDisplayName();
}

FText UTN_GameSettingsSubsystem::ActionLabel(const FString& ActionName)
{
	static const TMap<FString, FText> Names = {
		{ TEXT("IA_Move"), NSLOCTEXT("TNSettings", "ActMove", "Moverse") },
		{ TEXT("IA_Look"), NSLOCTEXT("TNSettings", "ActLook", "Mover la cámara") },
		{ TEXT("IA_Jump"), NSLOCTEXT("TNSettings", "ActJump", "Saltar") },
		{ TEXT("IA_Sprint"), NSLOCTEXT("TNSettings", "ActSprint", "Correr") },
		{ TEXT("IA_Interact"), NSLOCTEXT("TNSettings", "ActInteract", "Usar, coger y lanzar") },
		{ TEXT("IA_Shell"), NSLOCTEXT("TNSettings", "ActShell", "Meterse en el caparazón") },
		{ TEXT("IA_DropItem"), NSLOCTEXT("TNSettings", "ActDrop", "Soltar el objeto") },
		{ TEXT("IA_RotateInventory"), NSLOCTEXT("TNSettings", "ActRotate", "Cambiar de objeto") },
		{ TEXT("IA_OpenEmoteWheel"), NSLOCTEXT("TNSettings", "ActEmotes", "Rueda de bailes") },
		{ TEXT("IA_OpenChatWheel"), NSLOCTEXT("TNSettings", "ActChat", "Frases rápidas") },
		{ TEXT("IA_RadialNavigate"), NSLOCTEXT("TNSettings", "ActWheelPick", "Elegir en la rueda") },
	};
	if (const FText* Found = Names.Find(ActionName))
	{
		return *Found;
	}
	// Acción sin nombre conocido: el nombre del asset, tal cual (no es texto del juego: no se traduce).
	FString Clean = ActionName;
	Clean.RemoveFromStart(TEXT("IA_"));
	return FText::AsCultureInvariant(Clean);
}

bool UTN_GameSettingsSubsystem::IsIgnoredWhileCapturing(const FKey& Key)
{
	if (!Key.IsValid() || Key.IsAxis2D() || Key.IsAxis3D() || Key.IsTouch() || Key.IsGesture())
	{
		return true;
	}
	// Ejes sueltos (ratón, sticks), menos los gatillos.
	if (Key.IsAxis1D() && TNGameSettingsDetail::PhysicalKey(Key) != EKeys::Gamepad_LeftTriggerAxis
		&& TNGameSettingsDetail::PhysicalKey(Key) != EKeys::Gamepad_RightTriggerAxis)
	{
		return true;
	}
	// Mover un stick también llega como «tecla» (Gamepad_LeftStick_Up...): se ignora para que un roce no la cambie.
	const FString Name = Key.GetFName().ToString();
	return Name.StartsWith(TEXT("Gamepad_LeftStick_")) || Name.StartsWith(TEXT("Gamepad_RightStick_")) || Name.StartsWith(TEXT("Virtual_"))
		|| Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown || Key == EKeys::AnyKey;
}

bool UTN_GameSettingsSubsystem::IsBindableKey(const FKey& InKey)
{
	const FKey Key = TNGameSettingsDetail::PhysicalKey(InKey);
	if (!TNGameSettingsDetail::IsRowKey(Key) || IsIgnoredWhileCapturing(Key))
	{
		return false;
	}
	// Escape abre el menú (y en el editor corta la partida), Select/Vista cancela la captura, el Tabulador abre el menú en
	// el editor y la consola es de la consola.
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Left || (GIsEditor && Key == EKeys::Tab))
	{
		return false;
	}
	return !GetDefault<UInputSettings>()->ConsoleKeys.Contains(Key);
}

void UTN_GameSettingsSubsystem::BuildDefaultBindings()
{
	using namespace TNGameSettingsDetail;
	DefaultBindings.Reset();
	FixedControls.Reset();
	if (OriginalMapping)
	{
		// Filas: una por acción de botón y una por dirección de las de ejes, con la primera tecla de cada aparato.
		for (const FEnhancedActionKeyMapping& Mapping : OriginalMapping->GetMappings())
		{
			const UInputAction* Action = Mapping.Action;
			if (!Action || !IsRowKey(Mapping.Key))
			{
				continue;
			}
			const FString Direction = MappingDirection(Mapping);
			const FString ActionName = Action->GetName();
			const FString Id = Direction.IsEmpty() ? ActionName : ActionName + TEXT(":") + Direction;
			FTNKeyBinding* Row = DefaultBindings.FindByPredicate([&Id](const FTNKeyBinding& Existing) { return Existing.Id == Id; });
			if (!Row)
			{
				Row = &DefaultBindings.AddDefaulted_GetRef();
				Row->Id = Id;
				Row->Label = BindingLabel(ActionName, Direction);
				Row->Action = Action;
				Row->Direction = Direction;
				Row->Order = ActionOrder(ActionName) * 10 + DirectionOrder(Direction);
			}
			const int32 Device = DeviceOf(Mapping.Key);
			if (!Row->Defaults[Device].IsValid()) { Row->Defaults[Device] = Mapping.Key; }
		}
		// Lo que va con ejes (stick, ratón): en las filas de esa acción se enseña en el aparato sin tecla (moverse con el
		// stick); las acciones que solo van con ejes (mirar, elegir en la rueda) se listan aparte, sin cambiarse.
		for (const FEnhancedActionKeyMapping& Mapping : OriginalMapping->GetMappings())
		{
			const UInputAction* Action = Mapping.Action;
			if (!Action || !Mapping.Key.IsValid() || IsRowKey(Mapping.Key))
			{
				continue;
			}
			const int32 Device = DeviceOf(Mapping.Key);
			bool bHasRows = false;
			for (FTNKeyBinding& Row : DefaultBindings)
			{
				if (Row.Action.Get() != Action)
				{
					continue;
				}
				bHasRows = true;
				if (!Row.Defaults[Device].IsValid() && !Row.FixedKeys[Device].IsValid()) { Row.FixedKeys[Device] = Mapping.Key; }
			}
			if (!bHasRows)
			{
				const FString ActionName = Action->GetName();
				FTNKeyBinding* Fixed = FixedControls.FindByPredicate([Action](const FTNKeyBinding& Existing) { return Existing.Action.Get() == Action; });
				if (!Fixed)
				{
					Fixed = &FixedControls.AddDefaulted_GetRef();
					Fixed->Id = ActionName;
					Fixed->Label = ActionLabel(ActionName);
					Fixed->Action = Action;
					Fixed->Order = ActionOrder(ActionName) * 10;
					Fixed->bEditable[0] = Fixed->bEditable[1] = false;
				}
				if (!Fixed->FixedKeys[Device].IsValid()) { Fixed->FixedKeys[Device] = Mapping.Key; }
			}
		}
		// Un aparato sin tecla: a una acción de botón se le puede poner; a una dirección, no (el mando se mueve con el stick).
		for (FTNKeyBinding& Row : DefaultBindings)
		{
			for (int32 Device = 0; Device < 2; ++Device)
			{
				Row.bEditable[Device] = Row.Defaults[Device].IsValid() || (Row.Direction.IsEmpty() && !Row.FixedKeys[Device].IsValid());
			}
		}
	}
	else
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Ajustes] No se pudo cargar IMC_Player: no se pueden cambiar las teclas."));
	}

	// Las del propio juego: cambiar de cámara (en «Jugando», detrás de las de IMC_Player), hablar (con pulsar para hablar)
	// y el menú de pausa.
	const FTNGameSettings Defaults;
	FTNKeyBinding& Camera = DefaultBindings.AddDefaulted_GetRef();
	Camera.Id = CameraId;
	Camera.Label = NSLOCTEXT("TNSettings", "CameraRow", "Cambiar de cámara");
	Camera.Defaults[0] = FKey(Defaults.CameraKey);
	Camera.Defaults[1] = FKey(Defaults.CameraPadKey);
	Camera.Order = 15000;
	FTNKeyBinding& Talk = DefaultBindings.AddDefaulted_GetRef();
	Talk.Id = TalkId;
	Talk.Label = NSLOCTEXT("TNSettings", "TalkRow", "Hablar (pulsar para hablar)");
	Talk.Defaults[0] = FKey(Defaults.PushToTalkKey);
	Talk.Defaults[1] = FKey(Defaults.PushToTalkPadKey);
	Talk.Order = 20000;
	FTNKeyBinding& Pause = DefaultBindings.AddDefaulted_GetRef();
	Pause.Id = PauseId;
	Pause.Label = NSLOCTEXT("TNSettings", "PauseRow", "Abrir y cerrar este menú");
	Pause.Defaults[0] = FKey(Defaults.PauseKey);
	Pause.Defaults[1] = FKey(Defaults.PausePadKey);
	Pause.Order = 20010;

	// Teclas de serie que el código pone hasta que IMC_Player las traiga (Scripts/imc_player_shell_b.py): B / Círculo del mando para
	// meterse en el caparazón. Si el asset ya tiene una tecla de mando en esa acción, o esa tecla la usa otra fila del mando, no se
	// hace nada (y con el asset al día, esta lista queda vacía).
	PendingCodeDefaults.Reset();
	auto AddCodeDefault = [this](const TCHAR* RowId, int32 Device, const FKey& Key)
	{
		FTNKeyBinding* Row = DefaultBindings.FindByPredicate([RowId](const FTNKeyBinding& Existing) { return Existing.Id == RowId; });
		if (!Row || !Row->Direction.IsEmpty() || Row->Defaults[Device].IsValid() || Row->FixedKeys[Device].IsValid())
		{
			return;
		}
		for (const FTNKeyBinding& Other : DefaultBindings)
		{
			if (Other.Id != Row->Id && SamePhysicalKey(Other.Defaults[Device], Key))
			{
				return;
			}
		}
		Row->Defaults[Device] = Key;
		Row->bEditable[Device] = true;
		FCodeDefaultKey Pending;
		Pending.RowId = Row->Id;
		Pending.Device = Device;
		Pending.Key = Key;
		PendingCodeDefaults.Add(MoveTemp(Pending));
	};
	AddCodeDefault(TEXT("IA_Shell"), 1, EKeys::Gamepad_FaceButton_Right);

	DefaultBindings.StableSort([](const FTNKeyBinding& A, const FTNKeyBinding& B) { return A.Order < B.Order; });
	FixedControls.StableSort([](const FTNKeyBinding& A, const FTNKeyBinding& B) { return A.Order < B.Order; });
	for (FTNKeyBinding& Row : DefaultBindings)
	{
		Row.Keys[0] = Row.Defaults[0];
		Row.Keys[1] = Row.Defaults[1];
	}
}

const FTNKeyBinding* UTN_GameSettingsSubsystem::FindDefaultBinding(const FString& Id) const
{
	return DefaultBindings.FindByPredicate([&Id](const FTNKeyBinding& Row) { return Row.Id == Id; });
}

FKey UTN_GameSettingsSubsystem::GetBindingKey(const FTNGameSettings& Own, const FTNKeyBinding& Row, int32 Device) const
{
	using namespace TNGameSettingsDetail;
	const int32 Slot = FMath::Clamp(Device, 0, 1);
	if (Row.Id == TalkId) { return FKey(Slot == 0 ? Own.PushToTalkKey : Own.PushToTalkPadKey); }
	if (Row.Id == PauseId) { return FKey(Slot == 0 ? Own.PauseKey : Own.PausePadKey); }
	if (Row.Id == CameraId) { return FKey(Slot == 0 ? Own.CameraKey : Own.CameraPadKey); }
	if (const FName* Override = Own.KeyOverrides.Find(OverrideName(Row.Id, Slot)))
	{
		return FKey(*Override);
	}
	return Row.Defaults[Slot];
}

void UTN_GameSettingsSubsystem::SetBindingKey(FTNGameSettings& Own, const FTNKeyBinding& Row, int32 Device, const FKey& Key)
{
	using namespace TNGameSettingsDetail;
	const int32 Slot = FMath::Clamp(Device, 0, 1);
	const FName NewName = Key.IsValid() ? Key.GetFName() : NAME_None;
	if (Row.Id == TalkId)
	{
		(Slot == 0 ? Own.PushToTalkKey : Own.PushToTalkPadKey) = NewName;
		return;
	}
	if (Row.Id == PauseId)
	{
		(Slot == 0 ? Own.PauseKey : Own.PausePadKey) = NewName;
		return;
	}
	if (Row.Id == CameraId)
	{
		(Slot == 0 ? Own.CameraKey : Own.CameraPadKey) = NewName;
		return;
	}
	const FString OverrideKey = OverrideName(Row.Id, Slot);
	if (SamePhysicalKey(Key, Row.Defaults[Slot]))
	{
		Own.KeyOverrides.Remove(OverrideKey);
	}
	else
	{
		Own.KeyOverrides.Add(OverrideKey, NewName);
	}
}

ETNRebindResult UTN_GameSettingsSubsystem::AssignKey(FTNGameSettings& Own, const FTNKeyBinding& Row, int32 Device, const FKey& InKey, bool bValidate, FText& OutMessage)
{
	using namespace TNGameSettingsDetail;
	const int32 Slot = FMath::Clamp(Device, 0, 1);
	// Las filas de IMC_Player llevan los gatillos como eje (como IMC_Player); hablar y el menú, como botón.
	auto ForRow = [](const FTNKeyBinding& Target, const FKey& Key)
	{
		return !Key.IsValid() ? FKey() : (IsGameRow(Target) ? ButtonKey(Key) : PhysicalKey(Key));
	};
	const FKey NewKey = ForRow(Row, InKey);
	if (bValidate)
	{
		if (!Row.bEditable[Slot])
		{
			OutMessage = FText::Format(NSLOCTEXT("TNSettings", "NotEditable", "«{0}» no se cambia con {1}: va con {2}."), Row.Label, DeviceWord(Slot),
				KeyDisplayName(Row.FixedKeys[Slot]));
			return ETNRebindResult::Refused;
		}
		if (!IsBindableKey(NewKey))
		{
			OutMessage = FText::Format(NSLOCTEXT("TNSettings", "Reserved", "{0} está reservada (menú, consola o editor). Prueba con otra."), KeyDisplayName(InKey));
			return ETNRebindResult::Refused;
		}
	}
	const FKey OldKey = GetBindingKey(Own, Row, Slot);
	if (SamePhysicalKey(OldKey, NewKey))
	{
		OutMessage = FText::Format(NSLOCTEXT("TNSettings", "Unchanged", "«{0}» ya iba con {1}."), Row.Label, KeyDisplayName(NewKey));
		return ETNRebindResult::Unchanged;
	}

	// Si otra fila del mismo aparato la tenía, se queda con la que tenía esta (si le vale) o sin tecla.
	OutMessage = FText::GetEmpty();
	if (NewKey.IsValid())
	{
		for (const FTNKeyBinding& Other : DefaultBindings)
		{
			if (Other.Id == Row.Id || !SamePhysicalKey(GetBindingKey(Own, Other, Slot), NewKey))
			{
				continue;
			}
			const FKey GiveBack = OldKey.IsValid() && IsBindableKey(OldKey) && Other.bEditable[Slot] ? ForRow(Other, OldKey) : FKey();
			SetBindingKey(Own, Other, Slot, GiveBack);
			OutMessage = GiveBack.IsValid()
				? FText::Format(NSLOCTEXT("TNSettings", "Swapped", "{0} estaba en «{1}»: ahora «{1}» va con {2}."), KeyDisplayName(NewKey), Other.Label,
					KeyDisplayName(GiveBack))
				: FText::Format(NSLOCTEXT("TNSettings", "Emptied", "{0} estaba en «{1}», que se queda sin tecla en {2}."), KeyDisplayName(NewKey), Other.Label,
					DeviceWord(Slot));
		}
	}
	SetBindingKey(Own, Row, Slot, NewKey);
	if (OutMessage.IsEmpty())
	{
		OutMessage = FText::Format(NSLOCTEXT("TNSettings", "Assigned", "«{0}»: {1}."), Row.Label, KeyDisplayName(NewKey));
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Ajustes] Controles: %s (%s) → %s."), *Row.Id, Slot == 0 ? TEXT("teclado") : TEXT("mando"),
		NewKey.IsValid() ? *NewKey.ToString() : TEXT("sin tecla"));
	return ETNRebindResult::Changed;
}

TArray<FTNKeyBinding> UTN_GameSettingsSubsystem::BuildKeyBindings(const FTNGameSettings& Own) const
{
	TArray<FTNKeyBinding> Out = DefaultBindings;
	for (FTNKeyBinding& Row : Out)
	{
		Row.Keys[0] = GetBindingKey(Own, Row, 0);
		Row.Keys[1] = GetBindingKey(Own, Row, 1);
	}
	return Out;
}

TArray<FTNKeyBinding> UTN_GameSettingsSubsystem::GetKeyBindings() const
{
	return BuildKeyBindings(GetEditedSettings());
}

TArray<FTNKeyBinding> UTN_GameSettingsSubsystem::GetKeyBindingsFor(const APlayerController* PC) const
{
	const FTNGameSettings* Own = OwnSettingsFor(PC);
	return BuildKeyBindings(Own ? *Own : Settings);
}

ETNRebindResult UTN_GameSettingsSubsystem::RebindKey(const FString& Id, const FKey& Key, FText& OutMessage)
{
	const FTNKeyBinding* Row = FindDefaultBinding(Id);
	if (!Row || !Key.IsValid())
	{
		return ETNRebindResult::Refused;
	}
	const ETNRebindResult Result = AssignKey(EditTarget(), *Row, TNGameSettingsDetail::DeviceOf(Key), Key, true, OutMessage);
	if (Result == ETNRebindResult::Changed)
	{
		OnKeyBindingsChanged();
	}
	return Result;
}

void UTN_GameSettingsSubsystem::ResetKeyBinding(const FString& Id, FText& OutMessage)
{
	OutMessage = FText::GetEmpty();
	const FTNKeyBinding* Row = FindDefaultBinding(Id);
	if (!Row)
	{
		return;
	}
	bool bChanged = false;
	TArray<FText> Messages;
	for (int32 Device = 0; Device < 2; ++Device)
	{
		FText Message;
		// Sin validar: la de serie vale siempre (el menú va con Escape, que no se puede elegir).
		if (AssignKey(EditTarget(), *Row, Device, Row->Defaults[Device], false, Message) == ETNRebindResult::Changed)
		{
			bChanged = true;
			Messages.Add(Message);
		}
	}
	// Los mensajes de cada aparato, uno detrás de otro y separados por un espacio (sin pasar por FString: se siguen traduciendo).
	OutMessage = bChanged ? FText::Join(INVTEXT(" "), Messages)
		: FText::Format(NSLOCTEXT("TNSettings", "AlreadyDefault", "«{0}» ya iba con las de serie."), Row->Label);
	if (bChanged)
	{
		OnKeyBindingsChanged();
	}
}

void UTN_GameSettingsSubsystem::ResetAllKeyBindings()
{
	const FTNGameSettings Defaults;
	FTNGameSettings& Target = EditTarget();
	Target.KeyOverrides.Reset();
	Target.PushToTalkKey = Defaults.PushToTalkKey;
	Target.PushToTalkPadKey = Defaults.PushToTalkPadKey;
	Target.PauseKey = Defaults.PauseKey;
	Target.PausePadKey = Defaults.PausePadKey;
	Target.CameraKey = Defaults.CameraKey;
	Target.CameraPadKey = Defaults.CameraPadKey;
	OnKeyBindingsChanged();
}

bool UTN_GameSettingsSubsystem::HasCustomKeys() const
{
	const FTNGameSettings Defaults;
	const FTNGameSettings& Target = GetEditedSettings();
	return Target.KeyOverrides.Num() > 0 || Target.PushToTalkKey != Defaults.PushToTalkKey || Target.PushToTalkPadKey != Defaults.PushToTalkPadKey
		|| Target.PauseKey != Defaults.PauseKey || Target.PausePadKey != Defaults.PausePadKey
		|| Target.CameraKey != Defaults.CameraKey || Target.CameraPadKey != Defaults.CameraPadKey;
}

FKey UTN_GameSettingsSubsystem::GetCameraToggleKey(bool bGamepad) const
{
	return FKey(bGamepad ? Settings.CameraPadKey : Settings.CameraKey);
}

void UTN_GameSettingsSubsystem::FreeCameraKeyConflicts()
{
	using namespace TNGameSettingsDetail;
	const FTNKeyBinding* Camera = FindDefaultBinding(CameraId);
	if (!Camera)
	{
		return;
	}
	for (int32 Device = 0; Device < 2; ++Device)
	{
		const FKey Key = GetBindingKey(Settings, *Camera, Device);
		if (!Key.IsValid())
		{
			continue;
		}
		for (const FTNKeyBinding& Other : DefaultBindings)
		{
			if (Other.Id != Camera->Id && SamePhysicalKey(GetBindingKey(Settings, Other, Device), Key))
			{
				// Ajustes de antes de que existiera esta fila: otra ya iba con esa tecla, y esa manda. La cámara se queda sin
				// ella en ese aparato (se le pone otra en Controles).
				SetBindingKey(Settings, *Camera, Device, FKey());
				UE_LOG(LogTortunabo, Log, TEXT("[Ajustes] Controles: %s ya es de %s; «Cambiar de cámara» se queda sin tecla en el %s."),
					*Key.ToString(), *Other.Id, Device == 0 ? TEXT("teclado") : TEXT("mando"));
				break;
			}
		}
	}
}

void UTN_GameSettingsSubsystem::OnKeyBindingsChanged()
{
	// Las de un invitado de la partida local: su copia de IMC_Player, sin guardar (la pone el siguiente fotograma).
	if (UTN_LocalPlayerProfile* Guest = GetEditedGuest())
	{
		FTNGameSettings& Own = Guest->GetGuestSettings();
		TNGameSettingsDetail::ClampSettings(Own);
		if (FTNPlayerInputState* State = StateFor(GetPauseMenuOwner()))
		{
			RebuildRemappedMapping(*State, Own);
			UpdateInputMapping(State->Player.Get(), *State);
		}
		return;
	}
	TNGameSettingsDetail::ClampSettings(Settings);
	MarkDirty(false);
	RebuildRemappedMapping(PrimaryInput, Settings);
	// En el acto (sin esperar al siguiente fotograma); la entrada del menú se rehace sola con sus teclas nuevas.
	const UGameInstance* GameInstance = GetGameInstance();
	UpdateInputMapping(GameInstance ? GameInstance->GetFirstGamePlayer() : nullptr, PrimaryInput);
}

void UTN_GameSettingsSubsystem::RebuildRemappedMapping(FTNPlayerInputState& State, const FTNGameSettings& Own)
{
	using namespace TNGameSettingsDetail;
	UInputMappingContext* Previous = State.RemappedMapping;
	State.RemappedMapping = nullptr;
	if (OriginalMapping && (Own.KeyOverrides.Num() > 0 || PendingCodeDefaults.Num() > 0))
	{
		// Copia transitoria de IMC_Player (con sus modificadores y disparadores) con las teclas del jugador.
		UInputMappingContext* Copy = DuplicateObject<UInputMappingContext>(OriginalMapping, this,
			MakeUniqueObjectName(this, UInputMappingContext::StaticClass(), TEXT("IMC_Player_Jugador")));
		Copy->SetFlags(RF_Transient);
		// Las teclas de serie que solo pone el código (IMC_Player aún no las trae), salvo que el jugador ya haya decidido esa fila.
		for (const FCodeDefaultKey& Pending : PendingCodeDefaults)
		{
			const FTNKeyBinding* Row = FindDefaultBinding(Pending.RowId);
			if (Row && Row->Action.Get() && !Own.KeyOverrides.Contains(OverrideName(Pending.RowId, Pending.Device)))
			{
				Copy->MapKey(Row->Action.Get(), Pending.Key);
			}
		}
		for (const FTNKeyBinding& Row : DefaultBindings)
		{
			const UInputAction* Action = Row.Action.Get();
			if (!Action)
			{
				continue;
			}
			for (int32 Device = 0; Device < 2; ++Device)
			{
				const FName* Override = Own.KeyOverrides.Find(OverrideName(Row.Id, Device));
				if (!Override)
				{
					continue;
				}
				const FKey NewKey(*Override);
				TArray<int32> Indices;
				const TArray<FEnhancedActionKeyMapping>& Mappings = Copy->GetMappings();
				for (int32 Index = 0; Index < Mappings.Num(); ++Index)
				{
					if (MappingBelongsTo(Mappings[Index], Row, Device)) { Indices.Add(Index); }
				}
				if (Indices.Num() == 0)
				{
					// La acción no tenía tecla en ese aparato: se le añade (solo las de botón, ver bEditable).
					if (NewKey.IsValid()) { Copy->MapKey(Action, NewKey); }
					continue;
				}
				// La primera, con la tecla nueva (conserva sus modificadores: la dirección sigue siendo la misma); las demás
				// de esa fila y ese aparato, fuera (la fila va con una sola).
				TArray<FKey> ToRemove;
				for (int32 Pos = 0; Pos < Indices.Num(); ++Pos)
				{
					if (Pos == 0 && NewKey.IsValid()) { Copy->GetMapping(Indices[Pos]).Key = NewKey; }
					else { ToRemove.Add(Mappings[Indices[Pos]].Key); }
				}
				for (const FKey& Gone : ToRemove) { Copy->UnmapKey(Action, Gone); }
			}
		}
		State.RemappedMapping = Copy;
	}
	if (Previous && Previous != State.RemappedMapping)
	{
		State.RetiredMappings.AddUnique(Previous);
	}
}

void UTN_GameSettingsSubsystem::UpdateInputMapping(const ULocalPlayer* Player, FTNPlayerInputState& State)
{
	UEnhancedInputLocalPlayerSubsystem* Input = Player ? Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Input || !OriginalMapping)
	{
		return;
	}
	// Donde esté puesto IMC_Player (la tortuga lo pone al poseerse, en el lobby y en la partida) o una copia vieja, la
	// copia de ahora con la misma prioridad. Sin esperar a soltar las teclas: lo que se tenga pulsado sigue valiendo.
	const UInputMappingContext* Wanted = State.RemappedMapping ? State.RemappedMapping.Get() : OriginalMapping.Get();
	FModifyContextOptions Options;
	Options.bIgnoreAllPressedKeysUntilRelease = false;
	auto Replace = [Input, Wanted, &Options](const UInputMappingContext* Stale)
	{
		int32 Priority = 0;
		if (!Stale || Stale == Wanted || !Input->HasMappingContext(Stale, Priority))
		{
			return;
		}
		Input->RemoveMappingContext(Stale, Options);
		if (!Input->HasMappingContext(Wanted))
		{
			Input->AddMappingContext(Wanted, Priority, Options);
		}
	};
	Replace(OriginalMapping);
	for (const TObjectPtr<UInputMappingContext>& Old : State.RetiredMappings)
	{
		Replace(Old);
	}
	State.RetiredMappings.RemoveAll([Input](const TObjectPtr<UInputMappingContext>& Old) { return !Old || !Input->HasMappingContext(Old); });
}

const UInputMappingContext* UTN_GameSettingsSubsystem::ResolveMappingContext(const UObject* WorldContext, const UInputMappingContext* Mapping)
{
	const UTN_GameSettingsSubsystem* Subsystem = Get(WorldContext);
	if (!Subsystem || !Mapping || Mapping != Subsystem->OriginalMapping)
	{
		return Mapping;
	}
	// La copia del jugador de WorldContext (un peón o un PlayerController); si no se sabe de quién es, la del jugador 1.
	const APlayerController* PC = Cast<APlayerController>(WorldContext);
	if (!PC)
	{
		if (const APawn* Pawn = Cast<APawn>(WorldContext)) { PC = Cast<APlayerController>(Pawn->GetController()); }
	}
	const FTNPlayerInputState* State = PC ? Subsystem->StateFor(PC) : nullptr;
	if (!State) { State = &Subsystem->PrimaryInput; }
	return State->RemappedMapping ? State->RemappedMapping.Get() : Mapping;
}

bool UTN_GameSettingsSubsystem::IsGameplayKey(const FKey& Key) const
{
	// Una tecla del juego de cualquier jugador local (el teclado es del jugador 1; los mandos, de quien los lleve).
	auto InMapping = [&Key](const UInputMappingContext* Mapping)
	{
		if (!Mapping)
		{
			return false;
		}
		for (const FEnhancedActionKeyMapping& Entry : Mapping->GetMappings())
		{
			if (Entry.Action && TNGameSettingsDetail::SamePhysicalKey(Entry.Key, Key))
			{
				return true;
			}
		}
		return false;
	};
	if (InMapping(PrimaryInput.RemappedMapping ? PrimaryInput.RemappedMapping.Get() : OriginalMapping.Get()))
	{
		return true;
	}
	for (const FTNPlayerInputState& Guest : GuestInputs)
	{
		if (Guest.RemappedMapping && InMapping(Guest.RemappedMapping)) { return true; }
	}
	return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Menú de pausa
// ─────────────────────────────────────────────────────────────────────────────

bool UTN_GameSettingsSubsystem::IsMenuUp() const
{
	if (IsPauseMenuOpen())
	{
		return true;
	}
	// Otra interfaz que se pulsa (tienda, probador, resumen de la carrera...): el juego enseña el cursor. Con la pantalla
	// partida, la de cualquier jugador local.
	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	TArray<APlayerController*> LocalControllers;
	TNLocalViews::GetLocalControllers(World, LocalControllers);
	for (const APlayerController* PC : LocalControllers)
	{
		if (PC->ShouldShowMouseCursor())
		{
			return true;
		}
	}
	return false;
}

void UTN_GameSettingsSubsystem::EnsurePauseInput(APlayerController* PC, FTNPlayerInputState& State, const FTNGameSettings& Own)
{
	// Solo el PlayerController de la partida (no el del menú principal; también el del Rally) y el local.
	APlayerController* GamePC = TNGameSettingsDetail::IsMatchPlayerController(PC) ? PC : nullptr;
	if (!GamePC || !GamePC->IsLocalController())
	{
		return;
	}
	const bool bKeysChanged = State.BoundPauseKey != Own.PauseKey || State.BoundPausePadKey != Own.PausePadKey;
	if (State.PauseInput && State.PauseInputOwner.Get() == GamePC && !bKeysChanged)
	{
		// Por si algo vació la pila del PlayerController.
		if (!GamePC->IsInputComponentInStack(State.PauseInput)) { GamePC->PushInputComponent(State.PauseInput); }
		return;
	}
	if (APlayerController* Old = State.PauseInputOwner.Get())
	{
		if (State.PauseInput) { Old->PopInputComponent(State.PauseInput); }
	}
	// Un UInputComponent propio en lo alto de la pila del PlayerController: sirve jugando y de espectador, sin tocar el
	// PlayerController. Los viajes sin cortes conservan el PlayerController y su pila; tras uno con corte hay otro y se
	// vuelve a meter (y también si el jugador cambia la tecla). Con la pantalla partida, uno por jugador con sus teclas.
	State.PauseInput = NewObject<UInputComponent>(GamePC, UInputComponent::StaticClass(), NAME_None, RF_Transient);
	State.PauseInput->Priority = TNGameSettingsDetail::PauseInputPriority;
	// Escape siempre; la tecla y el botón elegidos (Start de serie); en el editor, donde Escape corta la partida
	// (atajo del editor), también el Tabulador. Sin repetir: una tecla atada dos veces abriría y cerraría a la vez.
	TArray<FKey> Keys;
	Keys.Add(EKeys::Escape);
	for (const FName KeyName : { Own.PauseKey, Own.PausePadKey })
	{
		const FKey Key(KeyName);
		if (Key.IsValid()) { Keys.AddUnique(Key); }
	}
	if (GIsEditor) { Keys.AddUnique(EKeys::Tab); }
	// El botón de menú del mando izquierdo de las gafas (si el motor tiene los mandos de Meta; sin OpenXR no existe).
	if (FTNVRKeys::Menu.IsValid()) { Keys.AddUnique(FTNVRKeys::Menu); }
	TWeakObjectPtr<APlayerController> WeakPC(GamePC);
	for (const FKey& Key : Keys)
	{
		// Cada tecla abre o cierra el menú de su jugador; también con la partida parada (la pausa de la partida local).
		FInputKeyBinding Binding(FInputChord(Key), IE_Pressed);
		Binding.bExecuteWhenPaused = true;
		Binding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, WeakPC]()
		{
			if (APlayerController* Owner = WeakPC.Get()) { TogglePauseMenu(Owner); }
		});
		State.PauseInput->KeyBindings.Add(MoveTemp(Binding));
	}
	GamePC->PushInputComponent(State.PauseInput);
	State.PauseInputOwner = GamePC;
	State.BoundPauseKey = Own.PauseKey;
	State.BoundPausePadKey = Own.PausePadKey;
}

bool UTN_GameSettingsSubsystem::CanOpenPauseMenu(const APlayerController* PC) const
{
	if (!PC || !PC->IsLocalController() || !TNGameSettingsDetail::IsMatchPlayerController(PC))
	{
		return false;
	}
	const UWorld* World = PC->GetWorld();
	if (!World || World->bIsTearingDown || World->IsInSeamlessTravel())
	{
		return false;
	}
	// Encima de la pantalla de carga (el huevo), no.
	if (const UTN_LoadingScreenSubsystem* Loading = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTN_LoadingScreenSubsystem>() : nullptr)
	{
		if (Loading->IsShowing())
		{
			return false;
		}
	}
	// Otro menú con el ratón a la vista (tienda, probador, general, ruedas, campeón de la carrera...): ese manda y se
	// cierra con su propio Escape.
	return !PC->ShouldShowMouseCursor();
}

void UTN_GameSettingsSubsystem::TogglePauseMenu(APlayerController* PC)
{
	if (IsPauseMenuOpen())
	{
		// Con la pantalla partida, solo lo cierra quien lo abrió (los demás esperan con la partida parada).
		if (GetPauseMenuOwner() == PC)
		{
			ClosePauseMenu();
		}
	}
	else
	{
		OpenPauseMenu(PC);
	}
}

void UTN_GameSettingsSubsystem::OpenPauseMenu(APlayerController* PC)
{
	if (IsPauseMenuOpen() || !CanOpenPauseMenu(PC))
	{
		return;
	}
	UTN_PauseMenuWidget* Menu = CreateWidget<UTN_PauseMenuWidget>(PC, UTN_PauseMenuWidget::StaticClass());
	if (!Menu)
	{
		return;
	}
	PauseMenu = Menu;
	// A toda la pantalla, por encima de las vistas de la pantalla partida.
	TNVR::AddToFullScreen(Menu, TNGameSettingsDetail::PauseMenuZOrder);
	Menu->TakeInput();
	// Partida local: la partida se para para todos mientras está abierto.
	if (UTN_LocalPlaySubsystem::IsLocalGame(PC))
	{
		SetWorldPaused(PC, true);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Pausa] Menú abierto (%s%s)."), *GetNameSafe(PC->GetWorld()),
		UTN_LocalPlaySubsystem::IsLocalGame(PC) ? *FString::Printf(TEXT(", jugador %d"), UTN_LocalPlaySubsystem::GetPlayerNumber(PC)) : TEXT(""));
}

void UTN_GameSettingsSubsystem::SetWorldPaused(APlayerController* PC, bool bPause)
{
	if (bPause)
	{
		UWorld* World = PC ? PC->GetWorld() : nullptr;
		if (World && !World->IsPaused() && UGameplayStatics::SetGamePaused(PC, true))
		{
			PausedWorld = World;
		}
		return;
	}
	UWorld* World = PausedWorld.Get();
	PausedWorld.Reset();
	if (World && !World->bIsTearingDown && World->IsPaused())
	{
		UGameplayStatics::SetGamePaused(World, false);
	}
}

void UTN_GameSettingsSubsystem::OpenMainMenuSettings(APlayerController* PC)
{
	// Solo el controlador del menú principal, local y con el menú de pausa cerrado.
	if (IsPauseMenuOpen() || !PC || !PC->IsLocalController() || !PC->IsA<AMP_MenuPlayerController>())
	{
		return;
	}
	UTN_PauseMenuWidget* Menu = CreateWidget<UTN_PauseMenuWidget>(PC, UTN_PauseMenuWidget::StaticClass());
	if (!Menu)
	{
		return;
	}
	PauseMenu = Menu;
	TNVR::AddToFullScreen(Menu, TNGameSettingsDetail::PauseMenuZOrder);
	Menu->TakeInput();
	UE_LOG(LogTortunabo, Log, TEXT("[Pausa] Ajustes abiertos desde el menú principal."));
}

void UTN_GameSettingsSubsystem::ClosePauseMenu()
{
	if (UTN_PauseMenuWidget* Menu = PauseMenu.Get())
	{
		PauseMenu = nullptr;
		// Al quitarse, el menú devuelve la entrada (NativeDestruct) y avisa (NotifyPauseMenuClosed).
		Menu->RemoveFromParent();
	}
	SetWorldPaused(nullptr, false);
}

bool UTN_GameSettingsSubsystem::IsPauseMenuOpen() const
{
	return PauseMenu && TNVR::IsOnScreen(PauseMenu);
}

APlayerController* UTN_GameSettingsSubsystem::GetPauseMenuOwner() const
{
	return IsPauseMenuOpen() ? PauseMenu->GetOwningPlayer() : nullptr;
}

void UTN_GameSettingsSubsystem::NotifyPauseMenuClosed(UTN_PauseMenuWidget* Menu)
{
	if (PauseMenu == Menu)
	{
		PauseMenu = nullptr;
	}
	// La partida local sigue (también si el menú se va por un viaje).
	SetWorldPaused(nullptr, false);
	// Ajustes abiertos desde el menú principal: al cerrarse, la entrada vuelve a ser la de ese menú (solo interfaz, con el cursor a
	// la vista), no la de una partida.
	APlayerController* MenuPC = Menu ? Menu->GetOwningPlayer() : nullptr;
	if (IsValid(MenuPC) && MenuPC->IsLocalController() && MenuPC->IsA<AMP_MenuPlayerController>())
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		MenuPC->SetInputMode(InputMode);
		MenuPC->SetShowMouseCursor(true);
	}
	// Si el menú se va con una resolución a medio confirmar (un viaje, por ejemplo), se deshace.
	if (bVideoModePending)
	{
		FinishVideoModeChange(false);
	}
	SaveNow();
}
