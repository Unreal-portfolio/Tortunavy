#include "Settings/TN_InputDeviceSubsystem.h"
#include "Core/TN_Log.h"
#include "Multiplayer/TN_SteamGamepadInput.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del bloque.
namespace TNInputDeviceDetail
{
	/** Cada cuánto se vuelve a preguntar qué mando hay (s): cambiar de mando no avisa. */
	constexpr double FamilyRefreshSeconds = 3.0;

	TAutoConsoleVariable<int32> CVarDevice(TEXT("TN.Input.Device"), 0,
		TEXT("Aparato de los avisos de botones: 0 el último usado, 1 teclado y ratón, 2 mando."), ECVF_Cheat);
	TAutoConsoleVariable<int32> CVarPadFamily(TEXT("TN.Input.PadFamily"), 0,
		TEXT("Botones que se dibujan con mando: 0 los del mando conectado, 1 Xbox, 2 PlayStation, 3 Steam Deck, 4 Switch."), ECVF_Cheat);

	/** Lee todo lo que entra por Slate (sin quedarse nada) y apunta el aparato en el subsistema. */
	class FTracker : public IInputProcessor
	{
	public:
		explicit FTracker(UTN_InputDeviceSubsystem* InOwner) : Owner(InOwner) {}

		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			Note(InKeyEvent.GetUserIndex(), TNInputGlyphs::DeviceOfKey(InKeyEvent.GetKey()), InKeyEvent.GetKey().GetFName());
			return false;
		}

		virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
		{
			if (TNInputGlyphs::IsAnalogDeviceSwitch(InAnalogInputEvent.GetKey(), InAnalogInputEvent.GetAnalogValue()))
			{
				Note(InAnalogInputEvent.GetUserIndex(), ETNInputDevice::Gamepad, InAnalogInputEvent.GetKey().GetFName());
			}
			return false;
		}

		virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			if (TNInputGlyphs::IsMouseDeviceSwitch(FVector2D(MouseEvent.GetCursorDelta())))
			{
				Note(MouseEvent.GetUserIndex(), ETNInputDevice::KeyboardMouse, TEXT("MouseMove"));
			}
			return false;
		}

		virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			Note(MouseEvent.GetUserIndex(), ETNInputDevice::KeyboardMouse, MouseEvent.GetEffectingButton().GetFName());
			return false;
		}

		virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent,
			const FPointerEvent* InGestureEvent) override
		{
			Note(InWheelEvent.GetUserIndex(), ETNInputDevice::KeyboardMouse, TEXT("MouseWheel"));
			return false;
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("TN_InputDeviceTracker"); }

	private:
		TWeakObjectPtr<UTN_InputDeviceSubsystem> Owner;

		void Note(int32 UserIndex, ETNInputDevice Device, FName Cause) const
		{
			if (UTN_InputDeviceSubsystem* Subsystem = Owner.Get())
			{
				Subsystem->NoteDevice(UserIndex, Device, Cause);
			}
		}
	};

	/**
	 * Familia por el nombre del último mando que ha visto el motor (sin Steam): XInputController (Xbox) o el HardwareId del
	 * perfil del lector DirectInput (DualShock4, DualSense, SwitchPro, GenericGamepad; TN_GamepadDevice, #743).
	 */
	ETNPadFamily FamilyFromEngine()
	{
		const UInputDeviceSubsystem* Devices = UInputDeviceSubsystem::Get();
		if (!Devices)
		{
			return ETNPadFamily::Xbox;
		}
		const FHardwareDeviceIdentifier Hardware = Devices->GetMostRecentlyUsedHardwareDevice(FPlatformMisc::GetPlatformUserForUserIndex(0));
		return TNInputGlyphs::FamilyFromHardwareName(Hardware.InputClassName.ToString() + TEXT(" ") + Hardware.HardwareDeviceIdentifier.ToString());
	}
}

UTN_InputDeviceSubsystem* UTN_InputDeviceSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UTN_InputDeviceSubsystem>() : nullptr;
}

void UTN_InputDeviceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// En la Steam Deck se juega con los mandos de la consola: los avisos empiezan con sus botones.
	StartDevice = TNSteamGamepadInput::IsOnSteamDeck() ? ETNInputDevice::Gamepad : ETNInputDevice::KeyboardMouse;
	if (FSlateApplication::IsInitialized())
	{
		Tracker = MakeShared<TNInputDeviceDetail::FTracker>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(Tracker);
	}
}

void UTN_InputDeviceSubsystem::Deinitialize()
{
	if (Tracker && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Tracker);
	}
	Tracker.Reset();
	Super::Deinitialize();
}

void UTN_InputDeviceSubsystem::NoteDevice(int32 UserIndex, ETNInputDevice Device, FName Cause)
{
	const ETNInputDevice* Known = LastDevices.Find(UserIndex);
	if ((Known ? *Known : StartDevice) == Device)
	{
		return;
	}
	LastDevices.Add(UserIndex, Device);
	// Al coger el mando, se vuelve a mirar cuál es (puede ser otro que el de antes).
	FamilyCheckedAt = -1.0;
	UE_LOG(LogTortunabo, Log, TEXT("[Entrada] Jugador %d: avisos con %s (%s)."), UserIndex,
		Device == ETNInputDevice::Gamepad ? TEXT("mando") : TEXT("teclado y ratón"), *Cause.ToString());
}

ETNInputDevice UTN_InputDeviceSubsystem::GetDevice(const APlayerController* PC) const
{
	switch (TNInputDeviceDetail::CVarDevice.GetValueOnGameThread())
	{
		case 1:  return ETNInputDevice::KeyboardMouse;
		case 2:  return ETNInputDevice::Gamepad;
		default: break;
	}
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	const int32 UserIndex = LocalPlayer ? LocalPlayer->GetPlatformUserIndex() : 0;
	const ETNInputDevice* Known = LastDevices.Find(UserIndex);
	return Known ? *Known : StartDevice;
}

ETNPadFamily UTN_InputDeviceSubsystem::GetPadFamily() const
{
	switch (TNInputDeviceDetail::CVarPadFamily.GetValueOnGameThread())
	{
		case 1:  return ETNPadFamily::Xbox;
		case 2:  return ETNPadFamily::PlayStation;
		case 3:  return ETNPadFamily::SteamDeck;
		case 4:  return ETNPadFamily::Switch;
		default: break;
	}
	const double Now = FPlatformTime::Seconds();
	if (FamilyCheckedAt < 0.0 || Now - FamilyCheckedAt >= TNInputDeviceDetail::FamilyRefreshSeconds)
	{
		FamilyCheckedAt = Now;
		CachedFamily = DetectPadFamily();
	}
	return CachedFamily;
}

ETNPadFamily UTN_InputDeviceSubsystem::DetectPadFamily() const
{
	if (TNSteamGamepadInput::IsSteamActive())
	{
		const int32 SteamType = TNSteamGamepadInput::GetPadInputType();
		if (SteamType != 0)
		{
			return TNInputGlyphs::FamilyFromSteamInputType(SteamType);
		}
		if (TNSteamGamepadInput::IsOnSteamDeck())
		{
			return ETNPadFamily::SteamDeck;
		}
	}
	return TNInputDeviceDetail::FamilyFromEngine();
}

FKey UTN_InputDeviceSubsystem::KeyForAction(const APlayerController* PC, const UInputAction* Action) const
{
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	const UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Input || !Action)
	{
		return FKey();
	}
	return TNInputGlyphs::PickKey(Input->QueryKeysMappedToAction(Action), GetDevice(PC));
}
