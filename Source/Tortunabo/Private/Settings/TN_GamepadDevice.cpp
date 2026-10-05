#include "Settings/TN_GamepadDevice.h"
#include "Settings/TN_GamepadSettings.h"
#include "Core/TN_Log.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GameFramework/InputSettings.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "GenericPlatform/GenericInputDeviceMap.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IInputDevice.h"
#include "IInputDeviceModule.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Multiplayer/TN_SteamGamepadInput.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
// Abierto hasta el final del fichero: el lector usa tipos de Windows y de DirectInput en todo el código de abajo.
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
THIRD_PARTY_INCLUDES_END
#endif

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del bloque.
namespace TNGamepadDeviceDetail
{
	/** Clase del lector para el motor: InputClassName de los HardwareDevices de DefaultInput.ini. */
	const FName InterfaceName(TEXT("TNGamepad"));
	/** Fabricante de Valve: el Steam Controller y la Deck sin Steam son teclado y ratón, y el mando virtual de Steam va por XInput. */
	constexpr uint16 ValveVendorId = 0x28DE;
	/** Cada cuánto se mira si ha cambiado la lista de aparatos (s): enchufar o desenchufar un mando. */
	constexpr double DeviceCheckSeconds = 1.0;
	/** Espera tras un cambio antes de preguntar a DirectInput (s): el mando tarda un poco en estar listo. */
	constexpr double EnumerateDelaySeconds = 0.4;
	/** Cada cuánto se pregunta a Steam Input qué mandos traduce (s). */
	constexpr double SteamCheckSeconds = 2.0;
	/** Zonas muertas para mandar los ejes (las de XInput): por debajo, solo se manda el cambio. */
	constexpr float LeftStickEventDeadZone = 7849.f / 32767.f;
	constexpr float RightStickEventDeadZone = 8689.f / 32767.f;
	constexpr float TriggerEventDeadZone = 30.f / 255.f;

	TAutoConsoleVariable<int32> CVarMode(TEXT("TN.Input.DirectInput"), 1,
		TEXT("Mandos que no son de Xbox por DirectInput (#743): 0 apagado, 1 automático (sin los que traduce Steam Input), ")
		TEXT("2 siempre (también los que traduce Steam Input)."));
	TAutoConsoleVariable<int32> CVarDebug(TEXT("TN.Input.PadDebug"), 0,
		TEXT("1: escribe en el registro cada botón, eje y cruceta en crudo de los mandos de DirectInput (para hacer un perfil nuevo)."));

	FString HexId(uint16 VendorId, uint16 ProductId)
	{
		return FString::Printf(TEXT("%04X:%04X"), VendorId, ProductId);
	}

	FString AxisName(ETNPadAxis Axis)
	{
		const UEnum* Enum = StaticEnum<ETNPadAxis>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Axis)) : FString::FromInt(static_cast<int32>(Axis));
	}

	FString BindingText(const FTNPadAxisBinding& Binding)
	{
		return Binding.Axis == ETNPadAxis::None ? FString(TEXT("-")) : (Binding.bInvert ? TEXT("-") : TEXT("")) + AxisName(Binding.Axis);
	}

	const TCHAR* FamilyName(ETNPadFamily Family)
	{
		switch (Family)
		{
			case ETNPadFamily::PlayStation: return TEXT("PlayStation");
			case ETNPadFamily::SteamDeck:   return TEXT("Steam Deck");
			case ETNPadFamily::Switch:      return TEXT("Switch");
			default:                        return TEXT("Xbox");
		}
	}

	/** Lista de SDL de una variable de entorno; con «@fichero», la del fichero (como SDL). */
	TArray<uint32> ReadSteamList(const TCHAR* Variable, FString& OutText)
	{
		OutText = FPlatformMisc::GetEnvironmentVariable(Variable);
		FString List = OutText;
		if (List.StartsWith(TEXT("@")))
		{
			FFileHelper::LoadFileToString(List, *List.RightChop(1));
		}
		return TNGamepadRules::ParseDeviceList(List);
	}

	/** El modo que manda: 0 apagado, 1 automático, 2 siempre. Con bEnabled = false, el automático es apagado. */
	int32 EffectiveMode()
	{
		const int32 Mode = CVarMode.GetValueOnGameThread();
		if (Mode == 1 && !GetDefault<UTN_GamepadSettings>()->bEnabled)
		{
			return 0;
		}
		return FMath::Clamp(Mode, 0, 2);
	}

	TArray<FString> DescribeDevice();
	void Rescan();
}

#if PLATFORM_WINDOWS
namespace TNGamepadDeviceDetail
{
	FGuid ToGuid(const GUID& Guid)
	{
		return FGuid(Guid.Data1, (static_cast<uint32>(Guid.Data2) << 16) | Guid.Data3,
			(static_cast<uint32>(Guid.Data4[0]) << 24) | (static_cast<uint32>(Guid.Data4[1]) << 16) | (static_cast<uint32>(Guid.Data4[2]) << 8) | Guid.Data4[3],
			(static_cast<uint32>(Guid.Data4[4]) << 24) | (static_cast<uint32>(Guid.Data4[5]) << 16) | (static_cast<uint32>(Guid.Data4[6]) << 8) | Guid.Data4[7]);
	}

	/** Los aparatos de Raw Input (handles); false si Windows no la da. */
	bool ReadRawDeviceList(TArray<RAWINPUTDEVICELIST>& OutList)
	{
		for (int32 Attempt = 0; Attempt < 3; ++Attempt)
		{
			UINT Count = 0;
			if (::GetRawInputDeviceList(nullptr, &Count, sizeof(RAWINPUTDEVICELIST)) != 0)
			{
				return false;
			}
			OutList.SetNumZeroed(static_cast<int32>(Count));
			const UINT Read = ::GetRawInputDeviceList(OutList.GetData(), &Count, sizeof(RAWINPUTDEVICELIST));
			if (Read != static_cast<UINT>(-1))
			{
				OutList.SetNum(static_cast<int32>(Read));
				return true;
			}
		}
		return false;
	}

	/** Firma barata de la lista de aparatos: cambia al enchufar o desenchufar algo. */
	uint64 DeviceListSignature()
	{
		TArray<RAWINPUTDEVICELIST> List;
		if (!ReadRawDeviceList(List))
		{
			return 0;
		}
		uint64 Signature = static_cast<uint64>(List.Num()) << 48;
		for (const RAWINPUTDEVICELIST& Device : List)
		{
			Signature ^= reinterpret_cast<uint64>(Device.hDevice) * 0x9E3779B97F4A7C15ull;
		}
		return Signature;
	}

	/**
	 * VendorID/ProductID de los mandos que también son XInput (su interfaz HID lleva «IG_» en el nombre): esos los lee
	 * XInputDevice y aquí se saltan, para no tener cada pulsación dos veces.
	 */
	TSet<uint32> CollectXInputIds()
	{
		TSet<uint32> Ids;
		TArray<RAWINPUTDEVICELIST> List;
		if (!ReadRawDeviceList(List))
		{
			return Ids;
		}
		for (const RAWINPUTDEVICELIST& Device : List)
		{
			if (Device.dwType != RIM_TYPEHID)
			{
				continue;
			}
			UINT NameLength = 0;
			::GetRawInputDeviceInfoW(Device.hDevice, RIDI_DEVICENAME, nullptr, &NameLength);
			if (NameLength == 0)
			{
				continue;
			}
			TArray<WCHAR> Name;
			Name.SetNumZeroed(static_cast<int32>(NameLength) + 1);
			if (::GetRawInputDeviceInfoW(Device.hDevice, RIDI_DEVICENAME, Name.GetData(), &NameLength) == static_cast<UINT>(-1))
			{
				continue;
			}
			if (!FString(Name.GetData()).Contains(TEXT("IG_")))
			{
				continue;
			}
			RID_DEVICE_INFO Info = {};
			Info.cbSize = sizeof(Info);
			UINT InfoSize = sizeof(Info);
			if (::GetRawInputDeviceInfoW(Device.hDevice, RIDI_DEVICEINFO, &Info, &InfoSize) != static_cast<UINT>(-1))
			{
				Ids.Add((static_cast<uint32>(Info.hid.dwVendorId & 0xFFFF) << 16) | (Info.hid.dwProductId & 0xFFFF));
			}
		}
		return Ids;
	}

	/**
	 * true si hay algún mando XInput conectado: la prueba de que Steam Input le está dando al juego su mando de Xbox virtual
	 * (por sus ganchos en XInput). Sin XInput a mano se da por hecho que sí (mejor no duplicar pulsaciones).
	 */
	bool IsAnyXInputPadConnected()
	{
		using FXInputGetState = DWORD(WINAPI*)(DWORD, void*);
		static FXInputGetState GetState = nullptr;
		static bool bLooked = false;
		if (!bLooked)
		{
			bLooked = true;
			// La misma DLL que carga XInputDevice; GetDllExport da void*, sin la conversión insegura desde FARPROC.
			void* Module = FPlatformProcess::GetDllHandle(TEXT("xinput1_4.dll"));
			GetState = Module ? reinterpret_cast<FXInputGetState>(FPlatformProcess::GetDllExport(Module, TEXT("XInputGetState"))) : nullptr;
		}
		if (!GetState)
		{
			return true;
		}
		// XINPUT_STATE: número de paquete y XINPUT_GAMEPAD (12 bytes); aquí solo importa si contesta.
		struct FXInputStateBlob { DWORD Packet; BYTE Gamepad[12]; } State;
		for (DWORD Index = 0; Index < 4; ++Index)
		{
			if (GetState(Index, &State) == ERROR_SUCCESS)
			{
				return true;
			}
		}
		return false;
	}

	BOOL CALLBACK CollectInstance(LPCDIDEVICEINSTANCEW Instance, LPVOID Context)
	{
		static_cast<TArray<DIDEVICEINSTANCEW>*>(Context)->Add(*Instance);
		return DIENUM_CONTINUE;
	}

	/** Un mando de DirectInput que el lector usa. */
	struct FPad
	{
		IDirectInputDevice8W* Device = nullptr;
		FGuid Instance;
		FString Name;
		uint16 VendorId = 0;
		uint16 ProductId = 0;
		FTNPadProfile Profile;
		/** «perfil N» de la configuración o «de serie». */
		FString ProfileSource;
		ETNPadFamily Family = ETNPadFamily::Xbox;
		int32 Handle = 0;
		int32 NumButtons = 0;
		int32 NumPovs = 0;
		FTNPadRawState Raw;
		FTNPadRawState Logged;
		/** Conectado al motor (con usuario y FInputDeviceId) y mandando eventos. */
		bool bActive = false;
		/** Ya se ha escrito que se ha perdido (para no repetirlo mientras se reintenta). */
		bool bLost = false;
		/** No vacío: no se lee porque Steam Input ya lo traduce (el motivo). */
		FString MuteReason;
		FInputDeviceId DeviceId;
		FPlatformUserId LastUser;
		FTNPadFrame Last;
		/** Teclas apretadas y cuándo toca la siguiente repetición. */
		TMap<FName, double> NextRepeat;
	};

	class FDevice : public IInputDevice
	{
	public:
		explicit FDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
			: MessageHandler(InMessageHandler)
		{
			GConfig->GetFloat(TEXT("/Script/Engine.InputSettings"), TEXT("InitialButtonRepeatDelay"), InitialRepeatDelay, GInputIni);
			GConfig->GetFloat(TEXT("/Script/Engine.InputSettings"), TEXT("ButtonRepeatDelay"), RepeatDelay, GInputIni);
			IgnoreList = ReadSteamList(TEXT("SDL_GAMECONTROLLER_IGNORE_DEVICES"), IgnoreText);
			ExceptList = ReadSteamList(TEXT("SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"), ExceptText);
		}

		virtual ~FDevice() override
		{
			for (TUniquePtr<FPad>& Pad : Pads)
			{
				ReleaseDevice(*Pad);
			}
			if (DirectInput)
			{
				DirectInput->Release();
				DirectInput = nullptr;
			}
		}

		bool Init()
		{
			const HRESULT Result = ::DirectInput8Create(::GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
				reinterpret_cast<void**>(&DirectInput), nullptr);
			if (FAILED(Result) || !DirectInput)
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[Mandos] DirectInput no está disponible (0x%08X): solo mandos XInput."), static_cast<uint32>(Result));
				DirectInput = nullptr;
				return false;
			}
			const UTN_GamepadSettings* Settings = GetDefault<UTN_GamepadSettings>();
			UE_LOG(LogTortunabo, Log, TEXT("[Mandos] Lector DirectInput listo: %d perfiles en la configuración; Steam: %s."),
				Settings->Profiles.Num(), IgnoreText.IsEmpty() && ExceptText.IsEmpty() ? TEXT("sin lista de mandos traducidos")
				: *FString::Printf(TEXT("ignorar «%s», salvo «%s»"), *IgnoreText, *ExceptText));
			return true;
		}

		void RequestRescan()
		{
			for (TUniquePtr<FPad>& Pad : Pads)
			{
				Deactivate(*Pad);
				ReleaseDevice(*Pad);
			}
			Pads.Reset();
			bNeedEnumerate = true;
			EnumerateAt = 0.0;
		}

		// IInputDevice
		virtual void Tick(float DeltaTime) override {}

		virtual void SendControllerEvents() override
		{
			const double Now = FPlatformTime::Seconds();
			const int32 Mode = EffectiveMode();
			if (Mode == 0)
			{
				if (bWasOn)
				{
					for (TUniquePtr<FPad>& Pad : Pads) { Deactivate(*Pad); }
					UE_LOG(LogTortunabo, Log, TEXT("[Mandos] Lector DirectInput apagado (TN.Input.DirectInput 0 o bEnabled=False)."));
				}
				bWasOn = false;
				return;
			}
			if (!bWasOn)
			{
				bWasOn = true;
				bNeedEnumerate = true;
				EnumerateAt = 0.0;
			}
			if (Now >= NextDeviceCheck)
			{
				NextDeviceCheck = Now + DeviceCheckSeconds;
				const uint64 Signature = DeviceListSignature();
				if (Signature != LastSignature)
				{
					LastSignature = Signature;
					bNeedEnumerate = true;
					EnumerateAt = FMath::Max(EnumerateAt, Now + (bEnumeratedOnce ? EnumerateDelaySeconds : 0.0));
				}
			}
			if (bNeedEnumerate && Now >= EnumerateAt)
			{
				Enumerate();
			}
			if (Now >= NextSteamCheck)
			{
				NextSteamCheck = Now + SteamCheckSeconds;
				RefreshSteam(Mode);
			}
			for (TUniquePtr<FPad>& Pad : Pads)
			{
				ProcessPad(*Pad, Now);
			}
		}

		virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override
		{
			MessageHandler = InMessageHandler;
		}

		virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override { return false; }
		// Sin vibración: DirectInput no la da en estos mandos sin su protocolo propio.
		virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override {}
		virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values) override {}
		virtual bool SupportsForceFeedback(int32 ControllerId) override { return false; }

		virtual bool IsGamepadAttached() const override
		{
			for (const TUniquePtr<FPad>& Pad : Pads)
			{
				if (Pad->bActive) { return true; }
			}
			return false;
		}

		TArray<FString> Describe() const
		{
			TArray<FString> Lines;
			Lines.Add(FString::Printf(TEXT("Steam: lista de ignorados «%s», excepto «%s»; Steam Input traduce los tipos [%s] (%s)."),
				*IgnoreText, *ExceptText, *FString::JoinBy(SteamTypes, TEXT(", "), [](int32 Type) { return FString::FromInt(Type); }),
				bXInputPadSeen ? TEXT("con mando XInput conectado") : TEXT("sin mando XInput conectado")));
			Lines.Add(FString::Printf(TEXT("Mandos de DirectInput (%d en uso o en espera, %d vistos en total):"), Pads.Num(), Seen.Num() + Pads.Num()));
			for (const TUniquePtr<FPad>& PadPtr : Pads)
			{
				const FPad& Pad = *PadPtr;
				FString Axes;
				for (int32 Axis = 0; Axis < FTNPadRawState::NumAxes; ++Axis)
				{
					if (Pad.Raw.bHasAxis[Axis]) { Axes += AxisName(static_cast<ETNPadAxis>(Axis + 1)) + TEXT(" "); }
				}
				Lines.Add(FString::Printf(TEXT("  · %s (%s): %s, %s [%s, familia %s]; %d botones, %d hats, ejes %s; aparato %d, usuario %d."),
					*Pad.Name, *HexId(Pad.VendorId, Pad.ProductId),
					Pad.bActive ? TEXT("en uso") : (Pad.MuteReason.IsEmpty() ? TEXT("sin leer") : *FString::Printf(TEXT("callado: %s"), *Pad.MuteReason)),
					*Pad.Profile.Name, *Pad.ProfileSource, FamilyName(Pad.Family), Pad.NumButtons, Pad.NumPovs, *Axes.TrimEnd(),
					Pad.DeviceId.GetId(), Pad.LastUser.GetInternalId()));
			}
			for (const FString& Line : Seen)
			{
				Lines.Add(TEXT("  · ") + Line);
			}
			FString XInputText;
			for (const uint32 Id : XInputIds)
			{
				XInputText += HexId(static_cast<uint16>(Id >> 16), static_cast<uint16>(Id & 0xFFFF)) + TEXT(" ");
			}
			Lines.Add(FString::Printf(TEXT("Interfaces HID de mandos XInput (las lee XInputDevice): %s"),
				XInputText.IsEmpty() ? TEXT("ninguna") : *XInputText.TrimEnd()));
			return Lines;
		}

	private:
		TSharedRef<FGenericApplicationMessageHandler> MessageHandler;
		IDirectInput8W* DirectInput = nullptr;
		TArray<TUniquePtr<FPad>> Pads;
		/** Mandos vistos que no se leen (XInput, Valve), para TN.Input.Pads. */
		TArray<FString> Seen;
		TSet<uint32> XInputIds;
		/** FInputDeviceId de cada mando (por su instancia de DirectInput): al volver a enchufarlo, el mismo. */
		TInputDeviceMap<FGuid> DeviceIds;
		int32 NextHandle = 0;
		float InitialRepeatDelay = 0.2f;
		float RepeatDelay = 0.1f;
		bool bWasOn = false;
		bool bNeedEnumerate = true;
		bool bEnumeratedOnce = false;
		double EnumerateAt = 0.0;
		double NextDeviceCheck = 0.0;
		double NextSteamCheck = 0.0;
		uint64 LastSignature = 0;
		TArray<uint32> IgnoreList;
		TArray<uint32> ExceptList;
		FString IgnoreText;
		FString ExceptText;
		TArray<int32> SteamTypes;
		bool bXInputPadSeen = false;

		static void ReleaseDevice(FPad& Pad)
		{
			if (Pad.Device)
			{
				Pad.Device->Unacquire();
				Pad.Device->Release();
				Pad.Device = nullptr;
			}
		}

		void Enumerate()
		{
			bNeedEnumerate = false;
			bEnumeratedOnce = true;
			if (!DirectInput)
			{
				return;
			}
			XInputIds = CollectXInputIds();
			TArray<DIDEVICEINSTANCEW> Instances;
			DirectInput->EnumDevices(DI8DEVCLASS_GAMECTRL, &CollectInstance, &Instances, DIEDFL_ATTACHEDONLY);

			TSet<FGuid> Present;
			TArray<FString> NewSeen;
			for (const DIDEVICEINSTANCEW& Instance : Instances)
			{
				const FGuid Guid = ToGuid(Instance.guidInstance);
				// guidProduct.Data1 = MAKELONG(VendorID, ProductID) en los aparatos HID.
				const uint16 VendorId = static_cast<uint16>(Instance.guidProduct.Data1 & 0xFFFF);
				const uint16 ProductId = static_cast<uint16>(Instance.guidProduct.Data1 >> 16);
				const FString Name(Instance.tszProductName);
				if (VendorId == ValveVendorId)
				{
					NewSeen.Add(FString::Printf(TEXT("%s (%s): de Valve, no se lee (Steam lo da por XInput)."), *Name, *HexId(VendorId, ProductId)));
					continue;
				}
				if (XInputIds.Contains((static_cast<uint32>(VendorId) << 16) | ProductId))
				{
					NewSeen.Add(FString::Printf(TEXT("%s (%s): mando XInput, lo lee XInputDevice."), *Name, *HexId(VendorId, ProductId)));
					continue;
				}
				Present.Add(Guid);
				TUniquePtr<FPad>* Existing = Pads.FindByPredicate([&Guid](const TUniquePtr<FPad>& Pad) { return Pad->Instance == Guid; });
				if (Existing)
				{
					if (!(*Existing)->Device)
					{
						OpenDevice(**Existing, Instance);
					}
					continue;
				}
				TUniquePtr<FPad> Pad = MakeUnique<FPad>();
				Pad->Instance = Guid;
				Pad->Name = Name;
				Pad->VendorId = VendorId;
				Pad->ProductId = ProductId;
				Pad->Handle = NextHandle++;
				ChooseProfile(*Pad);
				if (OpenDevice(*Pad, Instance))
				{
					UpdateMute(*Pad, EffectiveMode());
					UE_LOG(LogTortunabo, Log, TEXT("[Mandos] Mando %s (%s): %s %s (%s, familia %s)%s."), *Pad->Name, *HexId(VendorId, ProductId),
						*Pad->Profile.Name, *Pad->ProfileSource, *Pad->Profile.HardwareId, FamilyName(Pad->Family),
						Pad->MuteReason.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("; callado: %s"), *Pad->MuteReason));
					Pads.Add(MoveTemp(Pad));
				}
			}
			if (NewSeen != Seen)
			{
				for (const FString& Line : NewSeen)
				{
					UE_LOG(LogTortunabo, Log, TEXT("[Mandos] %s"), *Line);
				}
				Seen = MoveTemp(NewSeen);
			}
			for (int32 Index = Pads.Num() - 1; Index >= 0; --Index)
			{
				FPad& Pad = *Pads[Index];
				if (!Present.Contains(Pad.Instance))
				{
					UE_LOG(LogTortunabo, Log, TEXT("[Mandos] Desconectado %s (%s)."), *Pad.Name, *HexId(Pad.VendorId, Pad.ProductId));
					Deactivate(Pad);
					ReleaseDevice(Pad);
					Pads.RemoveAt(Index);
				}
			}
		}

		void ChooseProfile(FPad& Pad) const
		{
			const TArray<FTNPadProfile>& Profiles = GetDefault<UTN_GamepadSettings>()->Profiles;
			const int32 Index = TNGamepadRules::FindProfile(Profiles, Pad.VendorId, Pad.ProductId);
			Pad.Profile = Profiles.IsValidIndex(Index) ? Profiles[Index] : TNGamepadRules::MakeGenericProfile();
			Pad.ProfileSource = Profiles.IsValidIndex(Index) ? FString::Printf(TEXT("(perfil %d de la configuración)"), Index) : TEXT("(de serie: ningún perfil encaja)");
			if (Pad.Profile.HardwareId.IsEmpty())
			{
				Pad.Profile.HardwareId = TEXT("GenericGamepad");
			}
			Pad.Family = TNGamepadRules::FamilyOf(Pad.Profile);
		}

		bool OpenDevice(FPad& Pad, const DIDEVICEINSTANCEW& Instance) const
		{
			IDirectInputDevice8W* Device = nullptr;
			if (FAILED(DirectInput->CreateDevice(Instance.guidInstance, &Device, nullptr)) || !Device)
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[Mandos] No se puede abrir %s."), *Pad.Name);
				return false;
			}
			if (FAILED(Device->SetDataFormat(&c_dfDIJoystick2)))
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[Mandos] %s no acepta el formato de mando de DirectInput."), *Pad.Name);
				Device->Release();
				return false;
			}
			// Todos los ejes en -32768..32767 y sin zona muerta del controlador: la ponen las reglas y los IMC.
			DIPROPRANGE Range = {};
			Range.diph.dwSize = sizeof(DIPROPRANGE);
			Range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
			Range.diph.dwHow = DIPH_DEVICE;
			Range.lMin = -32768;
			Range.lMax = 32767;
			Device->SetProperty(DIPROP_RANGE, &Range.diph);
			DIPROPDWORD DeadZone = {};
			DeadZone.diph.dwSize = sizeof(DIPROPDWORD);
			DeadZone.diph.dwHeaderSize = sizeof(DIPROPHEADER);
			DeadZone.diph.dwHow = DIPH_DEVICE;
			DeadZone.dwData = 0;
			Device->SetProperty(DIPROP_DEADZONE, &DeadZone.diph);

			const DWORD Offsets[FTNPadRawState::NumAxes] = {
				static_cast<DWORD>(DIJOFS_X), static_cast<DWORD>(DIJOFS_Y), static_cast<DWORD>(DIJOFS_Z), static_cast<DWORD>(DIJOFS_RX),
				static_cast<DWORD>(DIJOFS_RY), static_cast<DWORD>(DIJOFS_RZ), static_cast<DWORD>(DIJOFS_SLIDER(0)), static_cast<DWORD>(DIJOFS_SLIDER(1)) };
			for (int32 Axis = 0; Axis < FTNPadRawState::NumAxes; ++Axis)
			{
				DIDEVICEOBJECTINSTANCEW Object = {};
				Object.dwSize = sizeof(Object);
				Pad.Raw.bHasAxis[Axis] = SUCCEEDED(Device->GetObjectInfo(&Object, Offsets[Axis], DIPH_BYOFFSET));
			}
			DIDEVCAPS Caps = {};
			Caps.dwSize = sizeof(Caps);
			if (SUCCEEDED(Device->GetCapabilities(&Caps)))
			{
				Pad.NumButtons = static_cast<int32>(Caps.dwButtons);
				Pad.NumPovs = static_cast<int32>(Caps.dwPOVs);
			}
			HRESULT Acquired = Device->Acquire();
			if (FAILED(Acquired))
			{
				// Sin nivel de cooperación, DirectInput lee el mando en segundo plano; si este aparato no se deja, se ata a la
				// ventana del juego (sin exclusiva: los demás programas lo siguen leyendo).
				if (HWND Window = ::GetActiveWindow())
				{
					Device->SetCooperativeLevel(Window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
					Acquired = Device->Acquire();
				}
				if (FAILED(Acquired))
				{
					UE_LOG(LogTortunabo, Warning, TEXT("[Mandos] %s no se deja leer todavía (0x%08X): se reintenta."), *Pad.Name, static_cast<uint32>(Acquired));
				}
			}
			Pad.Device = Device;
			Pad.Logged = Pad.Raw;
			return true;
		}

		void RefreshSteam(int32 Mode)
		{
			const bool bSkip = Mode == 1 && GetDefault<UTN_GamepadSettings>()->bSkipSteamInputControllers;
			const bool bHasList = IgnoreList.Num() > 0 || ExceptList.Num() > 0;
			// Sin la lista de Steam: los tipos que Steam Input dice tener, solo si de verdad da un mando XInput al juego.
			SteamTypes = bSkip && !bHasList ? TNSteamGamepadInput::GetConnectedPadInputTypes() : TArray<int32>();
			bXInputPadSeen = SteamTypes.Num() > 0 && IsAnyXInputPadConnected();
			for (TUniquePtr<FPad>& Pad : Pads)
			{
				UpdateMute(*Pad, Mode);
			}
		}

		void UpdateMute(FPad& Pad, int32 Mode) const
		{
			FString Reason;
			if (Mode == 1 && GetDefault<UTN_GamepadSettings>()->bSkipSteamInputControllers)
			{
				if (IgnoreList.Num() > 0 || ExceptList.Num() > 0)
				{
					if (TNGamepadRules::IsInSteamIgnoreList(Pad.VendorId, Pad.ProductId, IgnoreList, ExceptList))
					{
						Reason = TEXT("Steam Input lo traduce (SDL_GAMECONTROLLER_IGNORE_DEVICES)");
					}
				}
				else if (bXInputPadSeen && TNGamepadRules::IsFamilyTranslatedBySteam(Pad.Family, SteamTypes))
				{
					Reason = TEXT("Steam Input traduce los mandos de su familia");
				}
			}
			if (Reason != Pad.MuteReason)
			{
				UE_LOG(LogTortunabo, Log, TEXT("[Mandos] %s (%s): %s."), *Pad.Name, *HexId(Pad.VendorId, Pad.ProductId),
					Reason.IsEmpty() ? TEXT("se vuelve a leer") : *FString::Printf(TEXT("callado, %s"), *Reason));
				Pad.MuteReason = Reason;
			}
		}

		/** Lee el estado del mando; false si se ha perdido (desenchufado, sin batería...). */
		bool Read(FPad& Pad)
		{
			if (!Pad.Device)
			{
				return false;
			}
			HRESULT Result = Pad.Device->Poll();
			if (FAILED(Result))
			{
				Result = Pad.Device->Acquire();
				if (FAILED(Result))
				{
					return LoseDevice(Pad);
				}
				Pad.Device->Poll();
			}
			DIJOYSTATE2 State = {};
			Result = Pad.Device->GetDeviceState(sizeof(DIJOYSTATE2), &State);
			if (Result == DIERR_INPUTLOST || Result == DIERR_NOTACQUIRED)
			{
				if (FAILED(Pad.Device->Acquire()) || FAILED(Pad.Device->GetDeviceState(sizeof(DIJOYSTATE2), &State)))
				{
					return LoseDevice(Pad);
				}
			}
			else if (FAILED(Result))
			{
				return LoseDevice(Pad);
			}
			const LONG Axes[FTNPadRawState::NumAxes] = { State.lX, State.lY, State.lZ, State.lRx, State.lRy, State.lRz, State.rglSlider[0], State.rglSlider[1] };
			for (int32 Axis = 0; Axis < FTNPadRawState::NumAxes; ++Axis)
			{
				Pad.Raw.Axes[Axis] = static_cast<int32>(Axes[Axis]);
			}
			Pad.Raw.Pov = Pad.NumPovs > 0 ? static_cast<uint32>(State.rgdwPOV[0]) : 0xFFFFFFFFu;
			for (int32 Button = 0; Button < FTNPadRawState::MaxButtons; ++Button)
			{
				Pad.Raw.Buttons[Button] = (State.rgbButtons[Button] & 0x80) != 0;
			}
			Pad.bLost = false;
			return true;
		}

		bool LoseDevice(FPad& Pad)
		{
			if (!Pad.bLost)
			{
				UE_LOG(LogTortunabo, Log, TEXT("[Mandos] Se pierde %s: se vuelve a buscar."), *Pad.Name);
				Pad.bLost = true;
			}
			ReleaseDevice(Pad);
			bNeedEnumerate = true;
			EnumerateAt = FPlatformTime::Seconds() + EnumerateDelaySeconds;
			return false;
		}

		void ProcessPad(FPad& Pad, double Now)
		{
			const bool bRead = Read(Pad);
			FInputDeviceScope InputScope(this, InterfaceName, Pad.Handle, Pad.Profile.HardwareId);
			if (!bRead || !Pad.MuteReason.IsEmpty())
			{
				Deactivate(Pad);
				return;
			}
			IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
			if (!Pad.bActive)
			{
				Pad.DeviceId = DeviceIds.GetOrCreateDeviceId(Pad.Instance);
				Mapper.Internal_MapInputDeviceToUser(Pad.DeviceId, Mapper.GetPlatformUserForNewlyConnectedDevice(), EInputDeviceConnectionState::Connected);
				Pad.bActive = true;
				Pad.Last = FTNPadFrame();
				Pad.NextRepeat.Reset();
			}
			const FPlatformUserId User = Mapper.GetUserForInputDevice(Pad.DeviceId);
			if (!User.IsValid())
			{
				return;
			}
			Pad.LastUser = User;
			if (CVarDebug.GetValueOnGameThread() != 0)
			{
				LogRaw(Pad);
			}
			Emit(Pad, TNGamepadRules::Translate(Pad.Profile, Pad.Raw), User, Now);
		}

		void Emit(FPad& Pad, const FTNPadFrame& Frame, FPlatformUserId User, double Now)
		{
			auto Analog = [this, &Pad, User](FName Key, float New, float& Old, float DeadZone)
			{
				if (New != Old || FMath::Abs(New) > DeadZone)
				{
					MessageHandler->OnControllerAnalog(Key, User, Pad.DeviceId, New);
				}
				Old = New;
			};
			Analog(FGamepadKeyNames::LeftAnalogX, Frame.LeftX, Pad.Last.LeftX, LeftStickEventDeadZone);
			Analog(FGamepadKeyNames::LeftAnalogY, Frame.LeftY, Pad.Last.LeftY, LeftStickEventDeadZone);
			Analog(FGamepadKeyNames::RightAnalogX, Frame.RightX, Pad.Last.RightX, RightStickEventDeadZone);
			Analog(FGamepadKeyNames::RightAnalogY, Frame.RightY, Pad.Last.RightY, RightStickEventDeadZone);
			Analog(FGamepadKeyNames::LeftTriggerAnalog, Frame.LeftTrigger, Pad.Last.LeftTrigger, TriggerEventDeadZone);
			Analog(FGamepadKeyNames::RightTriggerAnalog, Frame.RightTrigger, Pad.Last.RightTrigger, TriggerEventDeadZone);

			for (auto It = Pad.NextRepeat.CreateIterator(); It; ++It)
			{
				if (!Frame.Pressed.Contains(It.Key()))
				{
					MessageHandler->OnControllerButtonReleased(It.Key(), User, Pad.DeviceId, false);
					It.RemoveCurrent();
				}
			}
			for (const FName Key : Frame.Pressed)
			{
				if (double* Next = Pad.NextRepeat.Find(Key))
				{
					if (Now >= *Next)
					{
						MessageHandler->OnControllerButtonPressed(Key, User, Pad.DeviceId, true);
						*Next = Now + RepeatDelay;
					}
					continue;
				}
				MessageHandler->OnControllerButtonPressed(Key, User, Pad.DeviceId, false);
				Pad.NextRepeat.Add(Key, Now + InitialRepeatDelay);
			}
		}

		/** Suelta todo lo apretado y quita el mando del motor (desenchufado, callado o lector apagado). */
		void Deactivate(FPad& Pad)
		{
			if (!Pad.bActive)
			{
				return;
			}
			IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
			const FPlatformUserId User = Pad.LastUser.IsValid() ? Pad.LastUser : Mapper.GetUserForInputDevice(Pad.DeviceId);
			if (User.IsValid())
			{
				FInputDeviceScope InputScope(this, InterfaceName, Pad.Handle, Pad.Profile.HardwareId);
				Emit(Pad, FTNPadFrame(), User, FPlatformTime::Seconds());
			}
			Mapper.Internal_MapInputDeviceToUser(Pad.DeviceId, Mapper.GetUserForUnpairedInputDevices(), EInputDeviceConnectionState::Disconnected);
			Pad.bActive = false;
			Pad.NextRepeat.Reset();
			Pad.Last = FTNPadFrame();
		}

		/** TN.Input.PadDebug 1: lo que cambia en crudo (botón por número, ejes pasado un cuarto de recorrido, hat). */
		void LogRaw(FPad& Pad)
		{
			for (int32 Button = 0; Button < FMath::Min(Pad.NumButtons, FTNPadRawState::MaxButtons); ++Button)
			{
				if (Pad.Raw.Buttons[Button] != Pad.Logged.Buttons[Button])
				{
					const FName Key = Pad.Profile.Buttons.IsValidIndex(Button) ? Pad.Profile.Buttons[Button] : NAME_None;
					UE_LOG(LogTortunabo, Display, TEXT("[Mandos] %s: botón %d %s (%s)."), *Pad.Name, Button,
						Pad.Raw.Buttons[Button] ? TEXT("apretado") : TEXT("suelto"), *Key.ToString());
				}
			}
			for (int32 Axis = 0; Axis < FTNPadRawState::NumAxes; ++Axis)
			{
				if (Pad.Raw.bHasAxis[Axis] && FMath::Abs(Pad.Raw.Axes[Axis] - Pad.Logged.Axes[Axis]) > 16384)
				{
					UE_LOG(LogTortunabo, Display, TEXT("[Mandos] %s: eje %s = %d."), *Pad.Name, *AxisName(static_cast<ETNPadAxis>(Axis + 1)), Pad.Raw.Axes[Axis]);
					Pad.Logged.Axes[Axis] = Pad.Raw.Axes[Axis];
				}
			}
			if (Pad.Raw.Pov != Pad.Logged.Pov)
			{
				UE_LOG(LogTortunabo, Display, TEXT("[Mandos] %s: hat = %d."), *Pad.Name,
					(Pad.Raw.Pov & 0xFFFF) == 0xFFFF ? -1 : static_cast<int32>(Pad.Raw.Pov));
			}
			FMemory::Memcpy(Pad.Logged.Buttons, Pad.Raw.Buttons, sizeof(Pad.Raw.Buttons));
			Pad.Logged.Pov = Pad.Raw.Pov;
		}
	};

	TWeakPtr<FDevice> GDevice;

	TArray<FString> DescribeDevice()
	{
		const TSharedPtr<FDevice> Device = GDevice.Pin();
		return Device ? Device->Describe() : TArray<FString>{ TEXT("El lector DirectInput aún no está creado (se crea en el primer fotograma con Slate).") };
	}

	void Rescan()
	{
		if (const TSharedPtr<FDevice> Device = GDevice.Pin())
		{
			Device->RequestRescan();
		}
	}
}
#else
namespace TNGamepadDeviceDetail
{
	TArray<FString> DescribeDevice() { return { TEXT("Solo en Windows: aquí no hay lector DirectInput.") }; }
	void Rescan() {}
}
#endif

namespace TNGamepadDeviceDetail
{
	/** El lector como dispositivo de entrada para el motor (FWindowsApplication lo crea en su primer PollGameDeviceState). */
	class FFeature : public IInputDeviceModule
	{
	public:
		virtual TSharedPtr<IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override
		{
#if PLATFORM_WINDOWS
			TSharedPtr<FDevice> Device = MakeShared<FDevice>(InMessageHandler);
			if (!Device->Init())
			{
				return nullptr;
			}
			GDevice = Device;
			return Device;
#else
			return nullptr;
#endif
		}
	};

	TUniquePtr<FFeature> GFeature;

	void LogPads()
	{
		for (const FString& Line : TNGamepadDevice::Describe())
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Mandos] %s"), *Line);
		}
	}

	FAutoConsoleCommand CmdPads(TEXT("TN.Input.Pads"),
		TEXT("Escribe en el registro los ajustes de los mandos (#743) y los mandos conocidos: perfiles, aparatos del motor y mandos de DirectInput."),
		FConsoleCommandDelegate::CreateStatic(&LogPads));

	FAutoConsoleCommand CmdRescan(TEXT("TN.Input.Pads.Rescan"),
		TEXT("Vuelve a buscar los mandos de DirectInput y a elegir su perfil (tras cambiar los perfiles en Ajustes del proyecto)."),
		FConsoleCommandDelegate::CreateStatic(&Rescan));
}

void TNGamepadDevice::Register()
{
	using namespace TNGamepadDeviceDetail;
	if (!GFeature)
	{
		GFeature = MakeUnique<FFeature>();
		IModularFeatures::Get().RegisterModularFeature(IInputDeviceModule::GetModularFeatureName(), GFeature.Get());
	}
}

void TNGamepadDevice::Unregister()
{
	using namespace TNGamepadDeviceDetail;
	if (GFeature)
	{
		IModularFeatures::Get().UnregisterModularFeature(IInputDeviceModule::GetModularFeatureName(), GFeature.Get());
		GFeature.Reset();
	}
}

bool TNGamepadDevice::IsRegistered()
{
	using namespace TNGamepadDeviceDetail;
	if (!GFeature)
	{
		return false;
	}
	const TArray<IInputDeviceModule*> Features =
		IModularFeatures::Get().GetModularFeatureImplementations<IInputDeviceModule>(IInputDeviceModule::GetModularFeatureName());
	return Features.Contains(GFeature.Get());
}

bool TNGamepadDevice::IsCreated()
{
#if PLATFORM_WINDOWS
	return TNGamepadDeviceDetail::GDevice.IsValid();
#else
	return false;
#endif
}

TArray<FString> TNGamepadDevice::Describe()
{
	using namespace TNGamepadDeviceDetail;
	TArray<FString> Lines;
	const UTN_GamepadSettings* Settings = GetDefault<UTN_GamepadSettings>();
	Lines.Add(FString::Printf(TEXT("Ajustes: bEnabled=%s, bSkipSteamInputControllers=%s, TN.Input.DirectInput=%d (efectivo %d), %s."),
		Settings->bEnabled ? TEXT("True") : TEXT("False"), Settings->bSkipSteamInputControllers ? TEXT("True") : TEXT("False"),
		CVarMode.GetValueOnGameThread(), EffectiveMode(), IsRegistered() ? TEXT("dado de alta en el motor") : TEXT("SIN dar de alta en el motor")));
	Lines.Add(FString::Printf(TEXT("Perfiles (%d, gana el primero que encaja):"), Settings->Profiles.Num()));
	for (int32 Index = 0; Index < Settings->Profiles.Num(); ++Index)
	{
		const FTNPadProfile& Profile = Settings->Profiles[Index];
		int32 Mapped = 0;
		for (const FName Key : Profile.Buttons) { Mapped += Key.IsNone() ? 0 : 1; }
		Lines.Add(FString::Printf(TEXT("  %d. %s: VendorID %s, ProductID [%s], HardwareId %s (familia %s); %d botones con tecla de %d; ")
			TEXT("sticks %s,%s / %s,%s; gatillos %s / %s; cruceta %s."), Index, *Profile.Name,
			Profile.VendorId.IsEmpty() ? TEXT("cualquiera") : *Profile.VendorId,
			Profile.ProductIds.Num() == 0 ? TEXT("cualquiera") : *FString::Join(Profile.ProductIds, TEXT(", ")),
			*Profile.HardwareId, FamilyName(TNGamepadRules::FamilyOf(Profile)), Mapped, Profile.Buttons.Num(),
			*BindingText(Profile.LeftX), *BindingText(Profile.LeftY), *BindingText(Profile.RightX), *BindingText(Profile.RightY),
			*BindingText(Profile.LeftTrigger), *BindingText(Profile.RightTrigger), Profile.bHatIsDPad ? TEXT("hat") : TEXT("botones")));
	}
	FString Hardware;
	if (const UInputPlatformSettings* Platform = UInputPlatformSettings::Get())
	{
		for (const FHardwareDeviceIdentifier& Device : Platform->GetHardwareDevices())
		{
			if (Device.InputClassName == InterfaceName || Device.InputClassName == TEXT("XInputInterface"))
			{
				Hardware += FString::Printf(TEXT("%s/%s (%s) "), *Device.InputClassName.ToString(), *Device.HardwareDeviceIdentifier.ToString(),
					*UEnum::GetValueAsString(Device.PrimaryDeviceType));
			}
		}
	}
	Lines.Add(FString::Printf(TEXT("Aparatos de mando registrados en el motor (InputPlatformSettings): %s"), Hardware.IsEmpty() ? TEXT("ninguno") : *Hardware.TrimEnd()));

	TArray<FInputDeviceId> Connected;
	IPlatformInputDeviceMapper::Get().GetAllConnectedInputDevices(Connected);
	const UInputDeviceSubsystem* Devices = UInputDeviceSubsystem::Get();
	FString ConnectedText;
	for (const FInputDeviceId Device : Connected)
	{
		const FHardwareDeviceIdentifier Known = Devices ? Devices->GetInputDeviceHardwareIdentifier(Device) : FHardwareDeviceIdentifier::Invalid;
		ConnectedText += FString::Printf(TEXT("%d→usuario %d (%s) "), Device.GetId(),
			IPlatformInputDeviceMapper::Get().GetUserForInputDevice(Device).GetInternalId(),
			Known != FHardwareDeviceIdentifier::Invalid ? *Known.ToString() : TEXT("sin usar aún"));
	}
	Lines.Add(FString::Printf(TEXT("Aparatos conectados en el motor: %s"), ConnectedText.IsEmpty() ? TEXT("ninguno") : *ConnectedText.TrimEnd()));
	Lines.Append(DescribeDevice());
	return Lines;
}

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif
