// Mandos que no son de Xbox (#743, Settings/TN_GamepadSettings.h y Settings/TN_GamepadDevice.h): perfiles de
// Config/DefaultInput.ini por VendorID/ProductID, traducción de botones, ejes y cruceta a las teclas Gamepad_* de serie, lista
// de Steam (SDL_GAMECONTROLLER_IGNORE_DEVICES) y familia de los avisos de botones. Correr desde Session Frontend (categoría
// "Tortunabo.Input") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Input.Gamepad; Quit" -nullrhi -unattended -nosound

#include "GameFramework/InputSettings.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "Misc/AutomationTest.h"
#include "Settings/TN_GamepadDevice.h"
#include "Settings/TN_GamepadSettings.h"
#include "UI/TN_InputGlyphs.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNGamepadRulesTest
{
	/** Valores de ESteamInputType (steam/isteaminput.h, SDK 1.57). */
	constexpr int32 SteamXboxOne = 3;
	constexpr int32 SteamGeneric = 4;
	constexpr int32 SteamPS4 = 5;
	constexpr int32 SteamSwitchPro = 10;
	constexpr int32 SteamPS5 = 13;

	template <typename T>
	bool Same(FAutomationTestBase& Test, const TCHAR* What, const T& Actual, const T& Expected)
	{
		return Test.TestTrue(What, Actual == Expected);
	}

	/** Estado en reposo de un mando con los ejes dados (sticks centrados, gatillos sueltos). */
	FTNPadRawState Rest(std::initializer_list<ETNPadAxis> Axes, std::initializer_list<ETNPadAxis> Triggers = {})
	{
		FTNPadRawState Raw;
		for (const ETNPadAxis Axis : Axes)
		{
			Raw.bHasAxis[static_cast<int32>(Axis) - 1] = true;
		}
		for (const ETNPadAxis Axis : Triggers)
		{
			Raw.bHasAxis[static_cast<int32>(Axis) - 1] = true;
			Raw.Axes[static_cast<int32>(Axis) - 1] = -32768;
		}
		return Raw;
	}

	void SetAxis(FTNPadRawState& Raw, ETNPadAxis Axis, int32 Value)
	{
		Raw.Axes[static_cast<int32>(Axis) - 1] = Value;
	}

	const FTNPadProfile* ProfileFor(const TArray<FTNPadProfile>& Profiles, uint16 VendorId, uint16 ProductId)
	{
		const int32 Index = TNGamepadRules::FindProfile(Profiles, VendorId, ProductId);
		return Profiles.IsValidIndex(Index) ? &Profiles[Index] : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNGamepadParseTest, "Tortunabo.Input.Gamepad.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNGamepadParseTest::RunTest(const FString& Parameters)
{
	using namespace TNGamepadRules;
	TestEqual(TEXT("054C"), ParseHexId(TEXT("054C")), 0x054C);
	TestEqual(TEXT("0x0ce6 en minúsculas"), ParseHexId(TEXT("0x0ce6")), 0x0CE6);
	TestEqual(TEXT("Con espacios"), ParseHexId(TEXT(" 2009 ")), 0x2009);
	TestEqual(TEXT("Vacío"), ParseHexId(FString()), -1);
	TestEqual(TEXT("No es hexadecimal"), ParseHexId(TEXT("05G4")), -1);
	TestEqual(TEXT("Más de 16 bits"), ParseHexId(TEXT("12345")), -1);

	// Formato de SDL que pone Steam al lanzar el juego con Steam Input.
	const TArray<uint32> List = ParseDeviceList(TEXT("0x054C/0x09CC, 0x057e/0x2009,basura,0x28DE/"));
	TestEqual(TEXT("Dos mandos válidos (lo demás se salta)"), List.Num(), 2);
	TestTrue(TEXT("DualShock 4 en la lista"), List.Contains(0x054C09CCu));
	TestTrue(TEXT("Switch Pro en la lista"), List.Contains(0x057E2009u));

	TestTrue(TEXT("En la lista de ignorados: no se lee"), IsInSteamIgnoreList(0x054C, 0x09CC, List, {}));
	TestFalse(TEXT("Fuera de la lista: se lee"), IsInSteamIgnoreList(0x054C, 0x0CE6, List, {}));
	const TArray<uint32> Except = { 0x054C0CE6u };
	TestFalse(TEXT("Con lista _EXCEPT, el de la lista se lee"), IsInSteamIgnoreList(0x054C, 0x0CE6, List, Except));
	TestTrue(TEXT("Con lista _EXCEPT, el resto no"), IsInSteamIgnoreList(0x046D, 0xC216, {}, Except));
	TestFalse(TEXT("Sin listas, se lee todo"), IsInSteamIgnoreList(0x054C, 0x09CC, {}, {}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNGamepadConfigTest, "Tortunabo.Input.Gamepad.Config",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNGamepadConfigTest::RunTest(const FString& Parameters)
{
	using namespace TNGamepadRulesTest;
	// Los perfiles salen de Config/DefaultInput.ini: si no se leen, aquí no hay ninguno.
	const UTN_GamepadSettings* Settings = GetDefault<UTN_GamepadSettings>();
	TestTrue(TEXT("Lector encendido en la configuración"), Settings->bEnabled);
	TestTrue(TEXT("Se calla lo que traduce Steam Input"), Settings->bSkipSteamInputControllers);
	const TArray<FTNPadProfile>& Profiles = Settings->Profiles;
	TestTrue(TEXT("Perfiles leídos de DefaultInput.ini"), Profiles.Num() >= 5);

	struct FExpected { uint16 Vendor; uint16 Product; const TCHAR* HardwareId; ETNPadFamily Family; };
	const FExpected Expected[] = {
		{ 0x054C, 0x05C4, TEXT("DualShock4"), ETNPadFamily::PlayStation },
		{ 0x054C, 0x09CC, TEXT("DualShock4"), ETNPadFamily::PlayStation },
		{ 0x054C, 0x0CE6, TEXT("DualSense"), ETNPadFamily::PlayStation },
		{ 0x054C, 0x0DF2, TEXT("DualSenseEdge"), ETNPadFamily::PlayStation },
		{ 0x057E, 0x2009, TEXT("SwitchPro"), ETNPadFamily::Switch },
		{ 0x0079, 0x0006, TEXT("GenericGamepad"), ETNPadFamily::Xbox },
		{ 0x054C, 0x0268, TEXT("GenericGamepad"), ETNPadFamily::Xbox },
	};
	const UInputPlatformSettings* Platform = UInputPlatformSettings::Get();
	for (const FExpected& Case : Expected)
	{
		const FString What = FString::Printf(TEXT("%04X:%04X"), Case.Vendor, Case.Product);
		const FTNPadProfile* Profile = ProfileFor(Profiles, Case.Vendor, Case.Product);
		if (!TestNotNull(*(What + TEXT(" tiene perfil")), Profile))
		{
			continue;
		}
		TestEqual(*(What + TEXT(": aparato")), Profile->HardwareId, FString(Case.HardwareId));
		Same(*this, *(What + TEXT(": familia de los avisos")), TNGamepadRules::FamilyOf(*Profile), Case.Family);
		// El motor lo tiene por mando (UInputDeviceSubsystem, modo local y avisos).
		const FHardwareDeviceIdentifier* Hardware = Platform ? Platform->GetHardwareDeviceForClassName(FName(Case.HardwareId)) : nullptr;
		if (TestNotNull(*(What + TEXT(": registrado en InputPlatformSettings")), Hardware))
		{
			TestEqual(*(What + TEXT(": clase del lector")), Hardware->InputClassName.ToString(), FString(TEXT("TNGamepad")));
			Same(*this, *(What + TEXT(": es un mando")), Hardware->PrimaryDeviceType, EHardwareDevicePrimaryType::Gamepad);
		}
	}

	// Cada botón con tecla lleva una tecla de mando que existe (un nombre mal escrito en el ini no haría nada).
	for (const FTNPadProfile& Profile : Profiles)
	{
		for (int32 Index = 0; Index < Profile.Buttons.Num(); ++Index)
		{
			const FName Key = Profile.Buttons[Index];
			if (!Key.IsNone())
			{
				const FKey Engine(Key);
				TestTrue(*FString::Printf(TEXT("%s, botón %d (%s): tecla de mando"), *Profile.Name, Index, *Key.ToString()),
					Engine.IsValid() && Engine.IsGamepadKey());
			}
		}
		TestTrue(*(Profile.Name + TEXT(": tiene stick izquierdo")), Profile.LeftX.Axis != ETNPadAxis::None && Profile.LeftY.Axis != ETNPadAxis::None);
	}
	TestTrue(TEXT("El último perfil vale para cualquier mando"), Profiles.Num() > 0
		&& Profiles.Last().VendorId.IsEmpty() && Profiles.Last().ProductIds.Num() == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNGamepadTranslateTest, "Tortunabo.Input.Gamepad.Translate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNGamepadTranslateTest::RunTest(const FString& Parameters)
{
	using namespace TNGamepadRules;
	using namespace TNGamepadRulesTest;
	const TArray<FTNPadProfile>& Profiles = GetDefault<UTN_GamepadSettings>()->Profiles;
	const FTNPadProfile* DS4 = ProfileFor(Profiles, 0x054C, 0x09CC);
	const FTNPadProfile* Pro = ProfileFor(Profiles, 0x057E, 0x2009);
	if (!TestNotNull(TEXT("Perfil del DualShock 4"), DS4) || !TestNotNull(TEXT("Perfil del Switch Pro"), Pro))
	{
		return false;
	}

	// DualShock 4 en reposo: nada apretado, sticks a 0 y gatillos a 0.
	FTNPadRawState Raw = Rest({ ETNPadAxis::X, ETNPadAxis::Y, ETNPadAxis::Z, ETNPadAxis::RZ }, { ETNPadAxis::RX, ETNPadAxis::RY });
	SetAxis(Raw, ETNPadAxis::X, 300);
	FTNPadFrame Frame = Translate(*DS4, Raw);
	TestEqual(TEXT("En reposo no hay teclas"), Frame.Pressed.Num(), 0);
	TestEqual(TEXT("La deriva del stick vale 0"), Frame.LeftX, 0.f);
	TestEqual(TEXT("Gatillo suelto: 0"), Frame.LeftTrigger, 0.f);

	// Cruz (botón 1) es el de abajo; cuadrado (0), el de la izquierda; Options (9), el menú; el panel táctil (13), la vista.
	Raw.Buttons[1] = true;
	Raw.Buttons[9] = true;
	Raw.Buttons[13] = true;
	Frame = Translate(*DS4, Raw);
	TestTrue(TEXT("Cruz → Gamepad_FaceButton_Bottom"), Frame.Pressed.Contains(FGamepadKeyNames::FaceButtonBottom));
	TestTrue(TEXT("Options → Gamepad_Special_Right"), Frame.Pressed.Contains(FGamepadKeyNames::SpecialRight));
	TestTrue(TEXT("Panel táctil → Gamepad_Special_Left"), Frame.Pressed.Contains(FGamepadKeyNames::SpecialLeft));
	TestFalse(TEXT("Cuadrado sin apretar"), Frame.Pressed.Contains(FGamepadKeyNames::FaceButtonLeft));
	Raw.Buttons[1] = Raw.Buttons[9] = Raw.Buttons[13] = false;

	// Stick izquierdo arriba del todo (en HID la Y crece hacia abajo) y gatillo L2 apretado (eje RX).
	SetAxis(Raw, ETNPadAxis::Y, -32768);
	SetAxis(Raw, ETNPadAxis::RX, 32767);
	Raw.Buttons[6] = true;  // L2 digital: no cuenta, el gatillo va por el eje.
	Frame = Translate(*DS4, Raw);
	TestEqual(TEXT("Arriba es Y positiva"), Frame.LeftY, 1.f);
	TestTrue(TEXT("Stick arriba como botón"), Frame.Pressed.Contains(FGamepadKeyNames::LeftStickUp));
	TestEqual(TEXT("L2 a fondo: 1"), Frame.LeftTrigger, 1.f);
	TestTrue(TEXT("L2 como botón (umbral de XInput)"), Frame.Pressed.Contains(FGamepadKeyNames::LeftTriggerThreshold));
	TestEqual(TEXT("L2 una sola vez"), Frame.Pressed.FilterByPredicate([](FName Key) { return Key == FGamepadKeyNames::LeftTriggerThreshold; }).Num(), 1);
	TestEqual(TEXT("R2 suelto"), Frame.RightTrigger, 0.f);

	// Cruceta por el hat: derecha y la diagonal arriba-derecha.
	Raw = Rest({ ETNPadAxis::X, ETNPadAxis::Y });
	Raw.Pov = 9000;
	Frame = Translate(*DS4, Raw);
	TestTrue(TEXT("Hat 90° → derecha"), Frame.Pressed.Contains(FGamepadKeyNames::DPadRight));
	TestFalse(TEXT("Hat 90° no es arriba"), Frame.Pressed.Contains(FGamepadKeyNames::DPadUp));
	Raw.Pov = 4500;
	Frame = Translate(*DS4, Raw);
	TestTrue(TEXT("Diagonal: arriba"), Frame.Pressed.Contains(FGamepadKeyNames::DPadUp));
	TestTrue(TEXT("Diagonal: derecha"), Frame.Pressed.Contains(FGamepadKeyNames::DPadRight));
	Raw.Pov = 0xFFFFFFFFu;
	TestEqual(TEXT("Hat centrado: sin cruceta"), Translate(*DS4, Raw).Pressed.Num(), 0);

	// Switch Pro: B (0) abajo, A (1) derecha, ZL (6) es el gatillo como botón y el eje vale 1; sin eje Z, el stick derecho
	// va por RX/RY.
	Raw = Rest({ ETNPadAxis::X, ETNPadAxis::Y, ETNPadAxis::RX, ETNPadAxis::RY });
	Raw.Buttons[0] = true;
	Raw.Buttons[6] = true;
	SetAxis(Raw, ETNPadAxis::RX, 32767);
	Frame = Translate(*Pro, Raw);
	TestTrue(TEXT("Switch B → abajo"), Frame.Pressed.Contains(FGamepadKeyNames::FaceButtonBottom));
	TestTrue(TEXT("Switch ZL → Gamepad_LeftTrigger"), Frame.Pressed.Contains(FGamepadKeyNames::LeftTriggerThreshold));
	TestEqual(TEXT("Switch ZL: eje del gatillo a 1"), Frame.LeftTrigger, 1.f);
	TestEqual(TEXT("Switch: stick derecho por RX"), Frame.RightX, 1.f);
	TestTrue(TEXT("Switch: stick derecho a la derecha como botón"), Frame.Pressed.Contains(FGamepadKeyNames::RightStickRight));

	// Un mando sin el eje del perfil no se mueve solo (un eje que no existe lee 0, que en un gatillo sería «medio apretado»).
	Raw = Rest({ ETNPadAxis::X, ETNPadAxis::Y });
	Frame = Translate(*DS4, Raw);
	TestEqual(TEXT("Sin eje RX, gatillo a 0"), Frame.LeftTrigger, 0.f);
	TestEqual(TEXT("Sin eje Z, stick derecho a 0"), Frame.RightX, 0.f);

	// El perfil de serie (si se borran todos del ini) es el genérico.
	const FTNPadProfile Generic = MakeGenericProfile();
	Same(*this, TEXT("De serie: familia Xbox"), FamilyOf(Generic), ETNPadFamily::Xbox);
	Same(*this, TEXT("De serie: botón 1 abajo"), Generic.Buttons[1], FName(FGamepadKeyNames::FaceButtonBottom));

	TestEqual(TEXT("Stick a fondo a la izquierda"), NormalizeStick(-32768, false), -1.f);
	TestEqual(TEXT("Stick al revés"), NormalizeStick(32767, true), -1.f);
	TestEqual(TEXT("Gatillo al revés suelto"), NormalizeTrigger(32767, true), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNGamepadSteamTest, "Tortunabo.Input.Gamepad.Steam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNGamepadSteamTest::RunTest(const FString& Parameters)
{
	using namespace TNGamepadRules;
	using namespace TNGamepadRulesTest;
	TestTrue(TEXT("Steam traduce un DualSense: se calla el DualShock/DualSense"), IsFamilyTranslatedBySteam(ETNPadFamily::PlayStation, { SteamPS5 }));
	TestTrue(TEXT("Steam traduce un Switch Pro"), IsFamilyTranslatedBySteam(ETNPadFamily::Switch, { SteamXboxOne, SteamSwitchPro }));
	TestTrue(TEXT("Steam traduce un genérico de DirectInput"), IsFamilyTranslatedBySteam(ETNPadFamily::Xbox, { SteamGeneric }));
	TestFalse(TEXT("Un mando de Xbox en Steam no calla los genéricos"), IsFamilyTranslatedBySteam(ETNPadFamily::Xbox, { SteamXboxOne }));
	TestFalse(TEXT("Un DualShock en Steam no calla el Switch Pro"), IsFamilyTranslatedBySteam(ETNPadFamily::Switch, { SteamPS4 }));
	TestFalse(TEXT("Sin Steam Input, nada"), IsFamilyTranslatedBySteam(ETNPadFamily::PlayStation, {}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNGamepadDeviceTest, "Tortunabo.Input.Gamepad.Device",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNGamepadDeviceTest::RunTest(const FString& Parameters)
{
	// El lector está dado de alta como dispositivo de entrada del motor (lo crea FWindowsApplication en su primer fotograma).
	TestTrue(TEXT("Lector dado de alta en el motor"), TNGamepadDevice::IsRegistered());
	const TArray<FString> Lines = TNGamepadDevice::Describe();
	TestTrue(TEXT("TN.Input.Pads escribe los ajustes y los perfiles"), Lines.Num() >= 3);
	for (const FString& Line : Lines)
	{
		AddInfo(Line);
	}
	AddInfo(TNGamepadDevice::IsCreated() ? TEXT("Lector DirectInput creado por el motor.") : TEXT("Lector DirectInput aún sin crear (sin Slate)."));
	return true;
}

#endif
