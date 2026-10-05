#include "Settings/TN_GamepadSettings.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "Misc/Parse.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del bloque.
namespace TNGamepadRulesDetail
{
	// Valores de ESteamInputType (steam/isteaminput.h, SDK 1.57) sin la cabecera de Steam; TN_SteamGamepadInput.cpp comprueba
	// con static_assert que siguen siendo estos.
	constexpr int32 SteamGenericGamepad = 4;
	constexpr int32 SteamPS4Controller = 5;
	constexpr int32 SteamJoyConPair = 8;
	constexpr int32 SteamJoyConSingle = 9;
	constexpr int32 SteamSwitchPro = 10;
	constexpr int32 SteamPS3Controller = 12;
	constexpr int32 SteamPS5Controller = 13;

	FTNPadAxisBinding Bind(ETNPadAxis Axis, bool bInvert)
	{
		FTNPadAxisBinding Binding;
		Binding.Axis = Axis;
		Binding.bInvert = bInvert;
		return Binding;
	}

	bool IsBound(const FTNPadAxisBinding& Binding, const FTNPadRawState& Raw)
	{
		const int32 Index = static_cast<int32>(Binding.Axis) - 1;
		return Index >= 0 && Index < FTNPadRawState::NumAxes && Raw.bHasAxis[Index];
	}

	int32 RawOf(const FTNPadAxisBinding& Binding, const FTNPadRawState& Raw)
	{
		return Raw.Axes[static_cast<int32>(Binding.Axis) - 1];
	}

	float StickOf(const FTNPadAxisBinding& Binding, const FTNPadRawState& Raw)
	{
		return IsBound(Binding, Raw) ? TNGamepadRules::NormalizeStick(RawOf(Binding, Raw), Binding.bInvert) : 0.f;
	}

	void Press(FTNPadFrame& Frame, FName Key)
	{
		if (!Key.IsNone())
		{
			Frame.Pressed.AddUnique(Key);
		}
	}

	/** Gatillo: del eje si lo hay; si no, 0 o 1 según su botón (Gamepad_LeftTrigger en Buttons). */
	float TriggerOf(const FTNPadAxisBinding& Binding, const FTNPadRawState& Raw, const FTNPadFrame& Frame, FName ButtonKey)
	{
		if (IsBound(Binding, Raw))
		{
			return TNGamepadRules::NormalizeTrigger(RawOf(Binding, Raw), Binding.bInvert);
		}
		return Frame.Pressed.Contains(ButtonKey) ? 1.f : 0.f;
	}

	/** Las cuatro direcciones de un stick como botones, con el umbral de XInput. */
	void PressStick(FTNPadFrame& Frame, float X, float Y, FName Up, FName Down, FName Left, FName Right)
	{
		const float T = TNGamepadRules::StickButtonThreshold;
		if (Y > T) { Press(Frame, Up); }
		if (Y < -T) { Press(Frame, Down); }
		if (X < -T) { Press(Frame, Left); }
		if (X > T) { Press(Frame, Right); }
	}
}

int32 TNGamepadRules::ParseHexId(const FString& Text)
{
	FString Hex = Text.TrimStartAndEnd();
	if (Hex.StartsWith(TEXT("0x"), ESearchCase::IgnoreCase))
	{
		Hex.RightChopInline(2);
	}
	if (Hex.IsEmpty() || Hex.Len() > 4)
	{
		return -1;
	}
	int32 Value = 0;
	for (const TCHAR Char : Hex)
	{
		if (!FChar::IsHexDigit(Char))
		{
			return -1;
		}
		Value = Value * 16 + FParse::HexDigit(Char);
	}
	return Value;
}

int32 TNGamepadRules::FindProfile(const TArray<FTNPadProfile>& Profiles, uint16 VendorId, uint16 ProductId)
{
	for (int32 Index = 0; Index < Profiles.Num(); ++Index)
	{
		const FTNPadProfile& Profile = Profiles[Index];
		if (!Profile.VendorId.TrimStartAndEnd().IsEmpty() && ParseHexId(Profile.VendorId) != VendorId)
		{
			continue;
		}
		bool bProductMatches = Profile.ProductIds.Num() == 0;
		for (const FString& Product : Profile.ProductIds)
		{
			bProductMatches |= ParseHexId(Product) == ProductId;
		}
		if (bProductMatches)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FTNPadProfile TNGamepadRules::MakeGenericProfile()
{
	FTNPadProfile Profile;
	Profile.Name = TEXT("Mando genérico (de serie)");
	Profile.HardwareId = TEXT("GenericGamepad");
	// La distribución de DirectInput más común (Logitech en modo D, la de PlayStation): 1 izquierda, 2 abajo, 3 derecha, 4 arriba.
	Profile.Buttons = {
		FGamepadKeyNames::FaceButtonLeft, FGamepadKeyNames::FaceButtonBottom, FGamepadKeyNames::FaceButtonRight,
		FGamepadKeyNames::FaceButtonTop, FGamepadKeyNames::LeftShoulder, FGamepadKeyNames::RightShoulder,
		FGamepadKeyNames::LeftTriggerThreshold, FGamepadKeyNames::RightTriggerThreshold, FGamepadKeyNames::SpecialLeft,
		FGamepadKeyNames::SpecialRight, FGamepadKeyNames::LeftThumb, FGamepadKeyNames::RightThumb };
	Profile.LeftX = TNGamepadRulesDetail::Bind(ETNPadAxis::X, false);
	Profile.LeftY = TNGamepadRulesDetail::Bind(ETNPadAxis::Y, true);
	Profile.RightX = TNGamepadRulesDetail::Bind(ETNPadAxis::Z, false);
	Profile.RightY = TNGamepadRulesDetail::Bind(ETNPadAxis::RZ, true);
	return Profile;
}

ETNPadFamily TNGamepadRules::FamilyOf(const FTNPadProfile& Profile)
{
	return TNInputGlyphs::FamilyFromHardwareName(Profile.HardwareId);
}

float TNGamepadRules::NormalizeStick(int32 Raw, bool bInvert)
{
	float Value = Raw >= 0 ? static_cast<float>(Raw) / 32767.f : static_cast<float>(Raw) / 32768.f;
	Value = FMath::Clamp(bInvert ? -Value : Value, -1.f, 1.f);
	return FMath::Abs(Value) < StickSnap ? 0.f : Value;
}

float TNGamepadRules::NormalizeTrigger(int32 Raw, bool bInvert)
{
	const float Value = FMath::Clamp((static_cast<float>(Raw) + 32768.f) / 65535.f, 0.f, 1.f);
	return bInvert ? 1.f - Value : Value;
}

void TNGamepadRules::HatToDPad(uint32 Pov, bool& bUp, bool& bRight, bool& bDown, bool& bLeft)
{
	bUp = bRight = bDown = bLeft = false;
	if ((Pov & 0xFFFFu) == 0xFFFFu || Pov >= 36000u)
	{
		return;
	}
	// Ocho sectores de 45° centrados en cada dirección: una diagonal (4500, 13500...) enciende las dos de al lado.
	bUp = Pov < 6750u || Pov > 29250u;
	bRight = Pov > 2250u && Pov < 15750u;
	bDown = Pov > 11250u && Pov < 24750u;
	bLeft = Pov > 20250u && Pov < 33750u;
}

FTNPadFrame TNGamepadRules::Translate(const FTNPadProfile& Profile, const FTNPadRawState& Raw)
{
	using namespace TNGamepadRulesDetail;
	FTNPadFrame Frame;
	const int32 NumButtons = FMath::Min(Profile.Buttons.Num(), FTNPadRawState::MaxButtons);
	for (int32 Index = 0; Index < NumButtons; ++Index)
	{
		if (Raw.Buttons[Index])
		{
			Press(Frame, Profile.Buttons[Index]);
		}
	}
	if (Profile.bHatIsDPad)
	{
		bool bUp, bRight, bDown, bLeft;
		HatToDPad(Raw.Pov, bUp, bRight, bDown, bLeft);
		if (bUp) { Press(Frame, FGamepadKeyNames::DPadUp); }
		if (bRight) { Press(Frame, FGamepadKeyNames::DPadRight); }
		if (bDown) { Press(Frame, FGamepadKeyNames::DPadDown); }
		if (bLeft) { Press(Frame, FGamepadKeyNames::DPadLeft); }
	}

	Frame.LeftX = StickOf(Profile.LeftX, Raw);
	Frame.LeftY = StickOf(Profile.LeftY, Raw);
	Frame.RightX = StickOf(Profile.RightX, Raw);
	Frame.RightY = StickOf(Profile.RightY, Raw);
	PressStick(Frame, Frame.LeftX, Frame.LeftY, FGamepadKeyNames::LeftStickUp, FGamepadKeyNames::LeftStickDown,
		FGamepadKeyNames::LeftStickLeft, FGamepadKeyNames::LeftStickRight);
	PressStick(Frame, Frame.RightX, Frame.RightY, FGamepadKeyNames::RightStickUp, FGamepadKeyNames::RightStickDown,
		FGamepadKeyNames::RightStickLeft, FGamepadKeyNames::RightStickRight);

	Frame.LeftTrigger = TriggerOf(Profile.LeftTrigger, Raw, Frame, FGamepadKeyNames::LeftTriggerThreshold);
	Frame.RightTrigger = TriggerOf(Profile.RightTrigger, Raw, Frame, FGamepadKeyNames::RightTriggerThreshold);
	// Con gatillo analógico, el botón sale del umbral (como en XInput), no de su botón digital.
	if (IsBound(Profile.LeftTrigger, Raw) && Frame.LeftTrigger > TriggerButtonThreshold)
	{
		Press(Frame, FGamepadKeyNames::LeftTriggerThreshold);
	}
	if (IsBound(Profile.RightTrigger, Raw) && Frame.RightTrigger > TriggerButtonThreshold)
	{
		Press(Frame, FGamepadKeyNames::RightTriggerThreshold);
	}
	return Frame;
}

TArray<uint32> TNGamepadRules::ParseDeviceList(const FString& List)
{
	TArray<uint32> Devices;
	TArray<FString> Entries;
	List.ParseIntoArray(Entries, TEXT(","), true);
	for (const FString& Entry : Entries)
	{
		FString Vendor, Product;
		if (!Entry.Split(TEXT("/"), &Vendor, &Product))
		{
			continue;
		}
		const int32 VendorId = ParseHexId(Vendor);
		const int32 ProductId = ParseHexId(Product);
		if (VendorId >= 0 && ProductId >= 0)
		{
			Devices.AddUnique((static_cast<uint32>(VendorId) << 16) | static_cast<uint32>(ProductId));
		}
	}
	return Devices;
}

bool TNGamepadRules::IsInSteamIgnoreList(uint16 VendorId, uint16 ProductId, const TArray<uint32>& Ignore, const TArray<uint32>& Except)
{
	const uint32 Id = (static_cast<uint32>(VendorId) << 16) | ProductId;
	if (Except.Num() > 0)
	{
		return !Except.Contains(Id);
	}
	return Ignore.Contains(Id);
}

bool TNGamepadRules::IsFamilyTranslatedBySteam(ETNPadFamily Family, const TArray<int32>& SteamInputTypes)
{
	using namespace TNGamepadRulesDetail;
	for (const int32 Type : SteamInputTypes)
	{
		switch (Type)
		{
			case SteamPS3Controller:
			case SteamPS4Controller:
			case SteamPS5Controller:
				if (Family == ETNPadFamily::PlayStation) { return true; }
				break;
			case SteamJoyConPair:
			case SteamJoyConSingle:
			case SteamSwitchPro:
				if (Family == ETNPadFamily::Switch) { return true; }
				break;
			case SteamGenericGamepad:
				// Por aquí no llega ningún mando XInput: lo que se dibuja como Xbox es un mando genérico de DirectInput.
				if (Family == ETNPadFamily::Xbox) { return true; }
				break;
			default:
				break;
		}
	}
	return false;
}
