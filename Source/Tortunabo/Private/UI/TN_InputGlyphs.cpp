#include "UI/TN_InputGlyphs.h"
#include "VR/TN_VRMode.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del bloque.
namespace TNInputGlyphsDetail
{
	// Valores de ESteamInputType (steam/isteaminput.h, SDK 1.57). Aquí sin la cabecera de Steam para que las reglas se
	// prueben en cualquier plataforma; TN_SteamGamepadInput.cpp comprueba con static_assert que siguen siendo estos.
	constexpr int32 SteamPS4Controller = 5;
	constexpr int32 SteamJoyConPair = 8;
	constexpr int32 SteamJoyConSingle = 9;
	constexpr int32 SteamSwitchProController = 10;
	constexpr int32 SteamPS3Controller = 12;
	constexpr int32 SteamPS5Controller = 13;
	constexpr int32 SteamDeckController = 14;

	// Colores de las letras de Xbox y de los símbolos de PlayStation (los que llevan impresos los mandos, en lineal).
	const FLinearColor XboxGreen = FLinearColor::FromSRGBColor(FColor(0x6C, 0xC2, 0x4A));
	const FLinearColor XboxRed = FLinearColor::FromSRGBColor(FColor(0xF0, 0x4E, 0x46));
	const FLinearColor XboxBlue = FLinearColor::FromSRGBColor(FColor(0x3E, 0x9B, 0xF4));
	const FLinearColor XboxYellow = FLinearColor::FromSRGBColor(FColor(0xFF, 0xC8, 0x2E));
	const FLinearColor PSCrossBlue = FLinearColor::FromSRGBColor(FColor(0x8F, 0xB4, 0xF0));
	const FLinearColor PSCircleRed = FLinearColor::FromSRGBColor(FColor(0xF2, 0x6D, 0x7D));
	const FLinearColor PSSquarePink = FLinearColor::FromSRGBColor(FColor(0xE6, 0x8F, 0xD2));
	const FLinearColor PSTriangleGreen = FLinearColor::FromSRGBColor(FColor(0x45, 0xD1, 0xB0));
	const FLinearColor Plain = FLinearColor::FromSRGBColor(FColor(0xFF, 0xFB, 0xF0));

	/** Botón de la cara: 0 abajo, 1 derecha, 2 izquierda, 3 arriba. */
	FTNGlyphSpec FaceGlyph(int32 Slot, ETNPadFamily Family)
	{
		FTNGlyphSpec Spec;
		Spec.Shape = ETNGlyphShape::Face;
		Spec.Index = Slot;
		if (Family == ETNPadFamily::PlayStation)
		{
			static const ETNGlyphSymbol Symbols[4] = { ETNGlyphSymbol::Cross, ETNGlyphSymbol::Circle, ETNGlyphSymbol::Square, ETNGlyphSymbol::Triangle };
			static const FLinearColor Colors[4] = { PSCrossBlue, PSCircleRed, PSSquarePink, PSTriangleGreen };
			Spec.Symbol = Symbols[Slot];
			Spec.Accent = Colors[Slot];
			return Spec;
		}
		if (Family == ETNPadFamily::Switch)
		{
			// Mismo sitio, otra letra: la de abajo es B y la de la derecha, A (las lleva impresas sin color).
			static const FText SwitchLetters[4] = { INVTEXT("B"), INVTEXT("A"), INVTEXT("Y"), INVTEXT("X") };
			Spec.Label = SwitchLetters[Slot];
			Spec.Accent = Plain;
			return Spec;
		}
		static const FText Letters[4] = { INVTEXT("A"), INVTEXT("B"), INVTEXT("X"), INVTEXT("Y") };
		static const FLinearColor Colors[4] = { XboxGreen, XboxRed, XboxBlue, XboxYellow };
		Spec.Label = Letters[Slot];
		Spec.Accent = Colors[Slot];
		return Spec;
	}

	/** Botón superior (bTrigger = false) o gatillo, izquierdo o derecho. */
	FTNGlyphSpec ShoulderGlyph(bool bTrigger, bool bRight, ETNPadFamily Family)
	{
		FTNGlyphSpec Spec;
		Spec.Shape = bTrigger ? ETNGlyphShape::Trigger : ETNGlyphShape::Shoulder;
		Spec.Index = bRight ? 1 : 0;
		Spec.Accent = Plain;
		if (Family == ETNPadFamily::Xbox)
		{
			static const FText Names[4] = { INVTEXT("LB"), INVTEXT("RB"), INVTEXT("LT"), INVTEXT("RT") };
			Spec.Label = Names[(bTrigger ? 2 : 0) + (bRight ? 1 : 0)];
			return Spec;
		}
		if (Family == ETNPadFamily::Switch)
		{
			static const FText SwitchNames[4] = { INVTEXT("L"), INVTEXT("R"), INVTEXT("ZL"), INVTEXT("ZR") };
			Spec.Label = SwitchNames[(bTrigger ? 2 : 0) + (bRight ? 1 : 0)];
			return Spec;
		}
		static const FText Names[4] = { INVTEXT("L1"), INVTEXT("R1"), INVTEXT("L2"), INVTEXT("R2") };
		Spec.Label = Names[(bTrigger ? 2 : 0) + (bRight ? 1 : 0)];
		return Spec;
	}

	/** Stick izquierdo o derecho: el eje lleva solo la letra; el clic, L3/R3 (LS/RS en Xbox). */
	FTNGlyphSpec StickGlyph(bool bRight, bool bClick, ETNPadFamily Family)
	{
		FTNGlyphSpec Spec;
		Spec.Shape = ETNGlyphShape::Stick;
		Spec.Index = bRight ? 1 : 0;
		Spec.bClick = bClick;
		Spec.Accent = Plain;
		if (!bClick)
		{
			Spec.Label = bRight ? INVTEXT("R") : INVTEXT("L");
			return Spec;
		}
		if (Family == ETNPadFamily::Xbox || Family == ETNPadFamily::Switch)
		{
			Spec.Label = bRight ? INVTEXT("RS") : INVTEXT("LS");
			return Spec;
		}
		Spec.Label = bRight ? INVTEXT("R3") : INVTEXT("L3");
		return Spec;
	}

	FTNGlyphSpec SimpleGlyph(ETNGlyphShape Shape, int32 Index)
	{
		FTNGlyphSpec Spec;
		Spec.Shape = Shape;
		Spec.Index = Index;
		Spec.Accent = Plain;
		return Spec;
	}
}

ETNInputDevice TNInputGlyphs::DeviceOfKey(const FKey& Key)
{
	return Key.IsGamepadKey() ? ETNInputDevice::Gamepad : ETNInputDevice::KeyboardMouse;
}

bool TNInputGlyphs::IsAnalogDeviceSwitch(const FKey& Key, float Value)
{
	return Key.IsGamepadKey() && FMath::Abs(Value) >= AnalogSwitchThreshold;
}

bool TNInputGlyphs::IsMouseDeviceSwitch(const FVector2D& CursorDelta)
{
	return CursorDelta.SizeSquared() >= FMath::Square(MouseSwitchThreshold);
}

FKey TNInputGlyphs::AsButton(const FKey& Key)
{
	if (Key == EKeys::Gamepad_LeftTriggerAxis) { return EKeys::Gamepad_LeftTrigger; }
	if (Key == EKeys::Gamepad_RightTriggerAxis) { return EKeys::Gamepad_RightTrigger; }
	return Key;
}

FKey TNInputGlyphs::PickKey(const TArray<FKey>& Keys, ETNInputDevice Device)
{
	FKey Other;
	for (const FKey& Key : Keys)
	{
		if (!Key.IsValid())
		{
			continue;
		}
		// Los botones de los Touch son «mando» para el motor, pero un aviso con mando no debe enseñar el gatillo de las gafas
		// (y con gafas, no el A del mando): cada aparato lee los suyos.
		const ETNInputDevice KeyDevice = FTNVRKeys::IsVRKey(Key) ? ETNInputDevice::VR : DeviceOfKey(Key);
		if (KeyDevice == Device)
		{
			return AsButton(Key);
		}
		if (!Other.IsValid())
		{
			Other = AsButton(Key);
		}
	}
	return Other;
}

ETNPadFamily TNInputGlyphs::FamilyFromSteamInputType(int32 SteamInputType)
{
	using namespace TNInputGlyphsDetail;
	switch (SteamInputType)
	{
		case SteamPS3Controller:
		case SteamPS4Controller:
		case SteamPS5Controller:
			return ETNPadFamily::PlayStation;
		case SteamDeckController:
			return ETNPadFamily::SteamDeck;
		case SteamJoyConPair:
		case SteamJoyConSingle:
		case SteamSwitchProController:
			return ETNPadFamily::Switch;
		default:
			return ETNPadFamily::Xbox;
	}
}

ETNPadFamily TNInputGlyphs::FamilyFromHardwareName(const FString& HardwareName)
{
	static const TCHAR* PlayStationNames[] = { TEXT("DualSense"), TEXT("DualShock"), TEXT("PS4"), TEXT("PS5"), TEXT("PlayStation") };
	for (const TCHAR* Name : PlayStationNames)
	{
		if (HardwareName.Contains(Name))
		{
			return ETNPadFamily::PlayStation;
		}
	}
	static const TCHAR* SwitchNames[] = { TEXT("Switch"), TEXT("Nintendo"), TEXT("JoyCon"), TEXT("Joy-Con") };
	for (const TCHAR* Name : SwitchNames)
	{
		if (HardwareName.Contains(Name))
		{
			return ETNPadFamily::Switch;
		}
	}
	return HardwareName.Contains(TEXT("SteamDeck")) ? ETNPadFamily::SteamDeck : ETNPadFamily::Xbox;
}

FTNGlyphSpec TNInputGlyphs::GlyphFor(const FKey& Key, ETNPadFamily Family)
{
	using namespace TNInputGlyphsDetail;
	if (!Key.IsValid() || !Key.IsGamepadKey())
	{
		return FTNGlyphSpec();
	}
	const FKey Button = AsButton(Key);
	if (Button == EKeys::Gamepad_FaceButton_Bottom) { return FaceGlyph(0, Family); }
	if (Button == EKeys::Gamepad_FaceButton_Right) { return FaceGlyph(1, Family); }
	if (Button == EKeys::Gamepad_FaceButton_Left) { return FaceGlyph(2, Family); }
	if (Button == EKeys::Gamepad_FaceButton_Top) { return FaceGlyph(3, Family); }
	if (Button == EKeys::Gamepad_LeftShoulder || Button == EKeys::Gamepad_RightShoulder)
	{
		return ShoulderGlyph(false, Button == EKeys::Gamepad_RightShoulder, Family);
	}
	if (Button == EKeys::Gamepad_LeftTrigger || Button == EKeys::Gamepad_RightTrigger)
	{
		return ShoulderGlyph(true, Button == EKeys::Gamepad_RightTrigger, Family);
	}
	if (Button == EKeys::Gamepad_LeftThumbstick || Button == EKeys::Gamepad_RightThumbstick)
	{
		return StickGlyph(Button == EKeys::Gamepad_RightThumbstick, true, Family);
	}
	static const FKey LeftStickKeys[] = { EKeys::Gamepad_Left2D, EKeys::Gamepad_LeftX, EKeys::Gamepad_LeftY,
		EKeys::Gamepad_LeftStick_Up, EKeys::Gamepad_LeftStick_Down, EKeys::Gamepad_LeftStick_Left, EKeys::Gamepad_LeftStick_Right };
	static const FKey RightStickKeys[] = { EKeys::Gamepad_Right2D, EKeys::Gamepad_RightX, EKeys::Gamepad_RightY,
		EKeys::Gamepad_RightStick_Up, EKeys::Gamepad_RightStick_Down, EKeys::Gamepad_RightStick_Left, EKeys::Gamepad_RightStick_Right };
	for (const FKey& Stick : LeftStickKeys) { if (Button == Stick) { return StickGlyph(false, false, Family); } }
	for (const FKey& Stick : RightStickKeys) { if (Button == Stick) { return StickGlyph(true, false, Family); } }
	static const FKey DPadKeys[4] = { EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Left };
	for (int32 Dir = 0; Dir < 4; ++Dir)
	{
		if (Button == DPadKeys[Dir]) { return SimpleGlyph(ETNGlyphShape::DPad, Dir); }
	}
	if (Button == EKeys::Gamepad_Special_Right) { return SimpleGlyph(ETNGlyphShape::Menu, 0); }
	if (Button == EKeys::Gamepad_Special_Left) { return SimpleGlyph(ETNGlyphShape::View, 0); }
	return FTNGlyphSpec();
}
