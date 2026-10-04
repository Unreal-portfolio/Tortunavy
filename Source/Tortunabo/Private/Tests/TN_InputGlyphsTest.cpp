// Reglas de los avisos de botones (UI/TN_InputGlyphs.h, #347) y del teclado de Steam para el código de sala
// (Multiplayer/TN_SteamGamepadInput.h, #354): qué aparato cuenta como el último usado, qué tecla de una acción se enseña con
// teclado o con mando (también reasignada), cómo se dibuja cada botón con mando de Xbox, de PlayStation o con la Steam Deck,
// y qué teclado de Steam se prueba antes. Correr desde Session Frontend (categoría "Tortunabo.UI") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.UI.InputGlyphs; Quit" -nullrhi -unattended

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Multiplayer/TN_SteamGamepadInput.h"
#include "Settings/TN_InputDeviceSubsystem.h"
#include "UI/TN_InputGlyphs.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNInputGlyphsTest
{
	/** Valores de ESteamInputType (steam/isteaminput.h, SDK 1.57) que el juego distingue. */
	constexpr int32 SteamUnknown = 0;
	constexpr int32 SteamXboxOne = 3;
	constexpr int32 SteamPS4 = 5;
	constexpr int32 SteamSwitchPro = 10;
	constexpr int32 SteamPS3 = 12;
	constexpr int32 SteamPS5 = 13;
	constexpr int32 SteamDeck = 14;

	/** Comparación de enumerados y teclas (TestEqual solo sabe escribir números y textos). */
	template <typename T>
	bool Same(FAutomationTestBase& Test, const TCHAR* What, const T& Actual, const T& Expected)
	{
		return Test.TestTrue(What, Actual == Expected);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNInputGlyphsDeviceTest, "Tortunabo.UI.InputGlyphs.Device",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNInputGlyphsDeviceTest::RunTest(const FString& Parameters)
{
	using namespace TNInputGlyphs;
	TNInputGlyphsTest::Same(*this, TEXT("A del mando es mando"), DeviceOfKey(EKeys::Gamepad_FaceButton_Bottom), ETNInputDevice::Gamepad);
	TNInputGlyphsTest::Same(*this, TEXT("El gatillo como eje es mando"), DeviceOfKey(EKeys::Gamepad_RightTriggerAxis), ETNInputDevice::Gamepad);
	TNInputGlyphsTest::Same(*this, TEXT("E es teclado"), DeviceOfKey(EKeys::E), ETNInputDevice::KeyboardMouse);
	TNInputGlyphsTest::Same(*this, TEXT("El clic es ratón"), DeviceOfKey(EKeys::LeftMouseButton), ETNInputDevice::KeyboardMouse);

	TestFalse(TEXT("La deriva del stick no cambia a mando"), IsAnalogDeviceSwitch(EKeys::Gamepad_LeftX, 0.12f));
	TestTrue(TEXT("Empujar el stick cambia a mando"), IsAnalogDeviceSwitch(EKeys::Gamepad_LeftX, -0.8f));
	TestTrue(TEXT("Apretar el gatillo cambia a mando"), IsAnalogDeviceSwitch(EKeys::Gamepad_LeftTriggerAxis, 0.6f));
	TestFalse(TEXT("Un eje del ratón no cuenta como mando"), IsAnalogDeviceSwitch(EKeys::MouseX, 5.f));

	TestFalse(TEXT("Un temblor del ratón no cambia a teclado"), IsMouseDeviceSwitch(FVector2D(1.f, -1.f)));
	TestTrue(TEXT("Mover el ratón cambia a teclado"), IsMouseDeviceSwitch(FVector2D(12.f, 0.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNInputGlyphsPickKeyTest, "Tortunabo.UI.InputGlyphs.PickKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNInputGlyphsPickKeyTest::RunTest(const FString& Parameters)
{
	using namespace TNInputGlyphs;
	const TArray<FKey> Interact = { EKeys::E, EKeys::Gamepad_FaceButton_Left };
	TNInputGlyphsTest::Same(*this, TEXT("Con teclado sale la tecla"), PickKey(Interact, ETNInputDevice::KeyboardMouse), EKeys::E);
	TNInputGlyphsTest::Same(*this, TEXT("Con mando sale el botón (hoy se descartaba)"), PickKey(Interact, ETNInputDevice::Gamepad), EKeys::Gamepad_FaceButton_Left);

	// Reasignado en Ajustes: el sistema de entrada devuelve la tecla nueva y es la que se enseña.
	const TArray<FKey> Rebound = { EKeys::F, EKeys::Gamepad_RightShoulder };
	TNInputGlyphsTest::Same(*this, TEXT("Botón reasignado"), PickKey(Rebound, ETNInputDevice::Gamepad), EKeys::Gamepad_RightShoulder);

	const TArray<FKey> Sprint = { EKeys::LeftShift, EKeys::Gamepad_RightTriggerAxis };
	TNInputGlyphsTest::Same(*this, TEXT("El gatillo sale como botón"), PickKey(Sprint, ETNInputDevice::Gamepad), EKeys::Gamepad_RightTrigger);

	const TArray<FKey> OnlyKeyboard = { FKey(), EKeys::G };
	TNInputGlyphsTest::Same(*this, TEXT("Sin botón de mando, la tecla"), PickKey(OnlyKeyboard, ETNInputDevice::Gamepad), EKeys::G);
	TestFalse(TEXT("Sin teclas, nada"), PickKey({}, ETNInputDevice::Gamepad).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNInputGlyphsFamilyTest, "Tortunabo.UI.InputGlyphs.Family",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNInputGlyphsFamilyTest::RunTest(const FString& Parameters)
{
	using namespace TNInputGlyphs;
	using namespace TNInputGlyphsTest;
	TNInputGlyphsTest::Same(*this, TEXT("Steam no lo sabe: Xbox"), FamilyFromSteamInputType(SteamUnknown), ETNPadFamily::Xbox);
	TNInputGlyphsTest::Same(*this, TEXT("Xbox One"), FamilyFromSteamInputType(SteamXboxOne), ETNPadFamily::Xbox);
	TNInputGlyphsTest::Same(*this, TEXT("DualShock 4"), FamilyFromSteamInputType(SteamPS4), ETNPadFamily::PlayStation);
	TNInputGlyphsTest::Same(*this, TEXT("DualShock 3"), FamilyFromSteamInputType(SteamPS3), ETNPadFamily::PlayStation);
	TNInputGlyphsTest::Same(*this, TEXT("DualSense"), FamilyFromSteamInputType(SteamPS5), ETNPadFamily::PlayStation);
	TNInputGlyphsTest::Same(*this, TEXT("Steam Deck"), FamilyFromSteamInputType(SteamDeck), ETNPadFamily::SteamDeck);
	TNInputGlyphsTest::Same(*this, TEXT("Switch Pro: letras como Xbox"), FamilyFromSteamInputType(SteamSwitchPro), ETNPadFamily::Xbox);
	TNInputGlyphsTest::Same(*this, TEXT("Tipo desconocido de un SDK futuro"), FamilyFromSteamInputType(999), ETNPadFamily::Xbox);

	TNInputGlyphsTest::Same(*this, TEXT("Nombre DualSense"), FamilyFromHardwareName(TEXT("DualSense Wireless Controller")), ETNPadFamily::PlayStation);
	TNInputGlyphsTest::Same(*this, TEXT("Nombre PS4"), FamilyFromHardwareName(TEXT("PS4Controller")), ETNPadFamily::PlayStation);
	TNInputGlyphsTest::Same(*this, TEXT("XInput"), FamilyFromHardwareName(TEXT("XInputController")), ETNPadFamily::Xbox);
	TNInputGlyphsTest::Same(*this, TEXT("Sin nombre"), FamilyFromHardwareName(FString()), ETNPadFamily::Xbox);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNInputGlyphsSpecTest, "Tortunabo.UI.InputGlyphs.Glyph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNInputGlyphsSpecTest::RunTest(const FString& Parameters)
{
	using namespace TNInputGlyphs;
	const FTNGlyphSpec XboxA = GlyphFor(EKeys::Gamepad_FaceButton_Bottom, ETNPadFamily::Xbox);
	TNInputGlyphsTest::Same(*this, TEXT("A de Xbox: botón de la cara"), XboxA.Shape, ETNGlyphShape::Face);
	TestEqual(TEXT("A de Xbox: letra A"), XboxA.Label.ToString(), FString(TEXT("A")));
	TNInputGlyphsTest::Same(*this, TEXT("A de Xbox: sin símbolo"), XboxA.Symbol, ETNGlyphSymbol::None);

	const FTNGlyphSpec PSBottom = GlyphFor(EKeys::Gamepad_FaceButton_Bottom, ETNPadFamily::PlayStation);
	TNInputGlyphsTest::Same(*this, TEXT("Abajo de PlayStation: cruz"), PSBottom.Symbol, ETNGlyphSymbol::Cross);
	TestTrue(TEXT("Abajo de PlayStation: sin letra"), PSBottom.Label.IsEmpty());
	TNInputGlyphsTest::Same(*this, TEXT("Derecha de PlayStation: círculo"), GlyphFor(EKeys::Gamepad_FaceButton_Right, ETNPadFamily::PlayStation).Symbol, ETNGlyphSymbol::Circle);
	TNInputGlyphsTest::Same(*this, TEXT("Izquierda de PlayStation: cuadrado"), GlyphFor(EKeys::Gamepad_FaceButton_Left, ETNPadFamily::PlayStation).Symbol, ETNGlyphSymbol::Square);
	TNInputGlyphsTest::Same(*this, TEXT("Arriba de PlayStation: triángulo"), GlyphFor(EKeys::Gamepad_FaceButton_Top, ETNPadFamily::PlayStation).Symbol, ETNGlyphSymbol::Triangle);
	TestEqual(TEXT("Izquierda de la Deck: X"), GlyphFor(EKeys::Gamepad_FaceButton_Left, ETNPadFamily::SteamDeck).Label.ToString(), FString(TEXT("X")));

	TestEqual(TEXT("LB de Xbox"), GlyphFor(EKeys::Gamepad_LeftShoulder, ETNPadFamily::Xbox).Label.ToString(), FString(TEXT("LB")));
	TestEqual(TEXT("L1 de PlayStation"), GlyphFor(EKeys::Gamepad_LeftShoulder, ETNPadFamily::PlayStation).Label.ToString(), FString(TEXT("L1")));
	TestEqual(TEXT("R1 de la Deck"), GlyphFor(EKeys::Gamepad_RightShoulder, ETNPadFamily::SteamDeck).Label.ToString(), FString(TEXT("R1")));
	const FTNGlyphSpec RT = GlyphFor(EKeys::Gamepad_RightTriggerAxis, ETNPadFamily::Xbox);
	TNInputGlyphsTest::Same(*this, TEXT("El gatillo como eje se dibuja como gatillo"), RT.Shape, ETNGlyphShape::Trigger);
	TestEqual(TEXT("RT de Xbox"), RT.Label.ToString(), FString(TEXT("RT")));
	TestEqual(TEXT("L2 de PlayStation"), GlyphFor(EKeys::Gamepad_LeftTrigger, ETNPadFamily::PlayStation).Label.ToString(), FString(TEXT("L2")));

	const FTNGlyphSpec R3 = GlyphFor(EKeys::Gamepad_RightThumbstick, ETNPadFamily::PlayStation);
	TNInputGlyphsTest::Same(*this, TEXT("Clic del stick: stick"), R3.Shape, ETNGlyphShape::Stick);
	TestTrue(TEXT("Clic del stick: es clic"), R3.bClick);
	TestEqual(TEXT("R3 de PlayStation"), R3.Label.ToString(), FString(TEXT("R3")));
	TestEqual(TEXT("RS de Xbox"), GlyphFor(EKeys::Gamepad_RightThumbstick, ETNPadFamily::Xbox).Label.ToString(), FString(TEXT("RS")));
	const FTNGlyphSpec Move = GlyphFor(EKeys::Gamepad_Left2D, ETNPadFamily::Xbox);
	TNInputGlyphsTest::Same(*this, TEXT("Stick izquierdo"), Move.Shape, ETNGlyphShape::Stick);
	TestEqual(TEXT("Stick izquierdo: índice 0"), Move.Index, 0);
	TestFalse(TEXT("Stick izquierdo: no es clic"), Move.bClick);

	const FTNGlyphSpec Down = GlyphFor(EKeys::Gamepad_DPad_Down, ETNPadFamily::Xbox);
	TNInputGlyphsTest::Same(*this, TEXT("Cruceta"), Down.Shape, ETNGlyphShape::DPad);
	TestEqual(TEXT("Cruceta abajo: dirección 2"), Down.Index, 2);
	TNInputGlyphsTest::Same(*this, TEXT("Start: menú"), GlyphFor(EKeys::Gamepad_Special_Right, ETNPadFamily::PlayStation).Shape, ETNGlyphShape::Menu);
	TNInputGlyphsTest::Same(*this, TEXT("Select: vista"), GlyphFor(EKeys::Gamepad_Special_Left, ETNPadFamily::Xbox).Shape, ETNGlyphShape::View);

	TNInputGlyphsTest::Same(*this, TEXT("Una tecla no tiene dibujo de mando"), GlyphFor(EKeys::E, ETNPadFamily::Xbox).Shape, ETNGlyphShape::None);
	TNInputGlyphsTest::Same(*this, TEXT("Una tecla vacía tampoco"), GlyphFor(FKey(), ETNPadFamily::Xbox).Shape, ETNGlyphShape::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSteamKeyboardOrderTest, "Tortunabo.UI.InputGlyphs.SteamKeyboard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNSteamKeyboardOrderTest::RunTest(const FString& Parameters)
{
	using namespace TNSteamGamepadInput;
	TestEqual(TEXT("Sin Steam no hay teclado de Steam"), KeyboardOrder(false, true, true).Num(), 0);
	TestEqual(TEXT("Con teclado no hace falta"), KeyboardOrder(true, true, false).Num(), 0);

	const TArray<ETNSteamKeyboard> Deck = KeyboardOrder(true, true, true);
	TestEqual(TEXT("En la Deck: dos intentos"), Deck.Num(), 2);
	if (Deck.Num() == 2)
	{
		TNInputGlyphsTest::Same(*this, TEXT("En la Deck, primero el flotante"), Deck[0], ETNSteamKeyboard::Floating);
		TNInputGlyphsTest::Same(*this, TEXT("En la Deck, después el de pantalla completa"), Deck[1], ETNSteamKeyboard::Overlay);
	}
	const TArray<ETNSteamKeyboard> BigPicture = KeyboardOrder(true, false, true);
	TestEqual(TEXT("En el PC: dos intentos"), BigPicture.Num(), 2);
	if (BigPicture.Num() == 2)
	{
		TNInputGlyphsTest::Same(*this, TEXT("En el PC (Big Picture), primero el de pantalla completa"), BigPicture[0], ETNSteamKeyboard::Overlay);
		TNInputGlyphsTest::Same(*this, TEXT("En el PC, después el flotante"), BigPicture[1], ETNSteamKeyboard::Floating);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNInputDeviceMemoryTest, "Tortunabo.UI.InputGlyphs.DeviceMemory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNInputDeviceMemoryTest::RunTest(const FString& Parameters)
{
	// El último aparato de cada jugador: el teclado devuelve los avisos a «teclado» después de tocar el mando (el subsistema
	// del motor no lo hacía) y, a pantalla partida, el mando de un invitado no cambia los avisos del jugador 1.
	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	UTN_InputDeviceSubsystem* Devices = NewObject<UTN_InputDeviceSubsystem>(GameInstance);
	TNInputGlyphsTest::Same(*this, TEXT("Empieza con teclado"), Devices->GetDevice(), ETNInputDevice::KeyboardMouse);
	Devices->NoteDevice(0, ETNInputDevice::Gamepad);
	TNInputGlyphsTest::Same(*this, TEXT("Coge el mando"), Devices->GetDevice(), ETNInputDevice::Gamepad);
	Devices->NoteDevice(0, ETNInputDevice::KeyboardMouse);
	TNInputGlyphsTest::Same(*this, TEXT("Vuelve al teclado"), Devices->GetDevice(), ETNInputDevice::KeyboardMouse);
	Devices->NoteDevice(1, ETNInputDevice::Gamepad);
	TNInputGlyphsTest::Same(*this, TEXT("El mando del jugador 2 no cambia los del 1"), Devices->GetDevice(), ETNInputDevice::KeyboardMouse);
	return true;
}

#endif
