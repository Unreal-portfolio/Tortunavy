// Nombres de los botones de los mandos Touch en los avisos (VR/TN_VRControls.h, #644), el tercer aparato «VR» de los avisos
// (Settings/TN_InputDeviceSubsystem.h), la vista del fantasma con gafas (#646) y los ajustes de VR (#647). Sin gafas ni mundo.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.VR; Quit" -nullrhi -unattended

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "Settings/TN_InputDeviceSubsystem.h"
#include "Settings/TN_SettingsSaveGame.h"
#include "UI/TN_InputGlyphs.h"
#include "VR/TN_VRControls.h"
#include "VR/TN_VRHandMath.h"
#include "VR/TN_VRMath.h"
#include "VR/TN_VRMode.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNVRControlsTest
{
	/** Todos los botones Touch que conoce el juego (FTNVRKeys). */
	TArray<FKey> AllKeys()
	{
		return { FTNVRKeys::LeftStickX, FTNVRKeys::LeftStickY, FTNVRKeys::RightStickX, FTNVRKeys::RightStickY, FTNVRKeys::LeftStickUp,
			FTNVRKeys::LeftStickDown, FTNVRKeys::LeftStickLeft, FTNVRKeys::LeftStickRight, FTNVRKeys::RightStickUp,
			FTNVRKeys::RightStickDown, FTNVRKeys::RightStickLeft, FTNVRKeys::RightStickRight, FTNVRKeys::LeftStickClick,
			FTNVRKeys::RightStickClick, FTNVRKeys::LeftTrigger, FTNVRKeys::RightTrigger, FTNVRKeys::LeftTriggerAxis,
			FTNVRKeys::RightTriggerAxis, FTNVRKeys::LeftGrip, FTNVRKeys::RightGrip, FTNVRKeys::LeftGripAxis, FTNVRKeys::RightGripAxis,
			FTNVRKeys::A, FTNVRKeys::B, FTNVRKeys::X, FTNVRKeys::Y, FTNVRKeys::Menu };
	}

	/** Pone el modo VR y lo devuelve al salir del ámbito. */
	struct FScopedMode
	{
		explicit FScopedMode(ETNVRMode Mode) : Previous(TNVR::GetMode()) { TNVR::SetMode(Mode); }
		~FScopedMode() { TNVR::SetMode(Previous); }
		ETNVRMode Previous;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRControlsKeyNamesTest,
	"Tortunabo.VR.Controls.KeyNames",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRControlsKeyNamesTest::RunTest(const FString& Parameters)
{
	for (const FKey& Key : TNVRControlsTest::AllKeys())
	{
		TestTrue(*FString::Printf(TEXT("%s es un botón Touch"), *Key.ToString()), FTNVRKeys::IsVRKey(Key));
		TestFalse(*FString::Printf(TEXT("%s tiene nombre"), *Key.ToString()), TNVRControls::KeyName(Key).IsEmpty());
		// KeyDisplayName (el que usan todos los avisos) da el mismo nombre, no el de «Oculus Touch (R) Trigger» del motor.
		TestTrue(*FString::Printf(TEXT("%s: KeyDisplayName conoce los Touch"), *Key.ToString()),
			UTN_GameSettingsSubsystem::KeyDisplayName(Key).EqualTo(TNVRControls::KeyName(Key)));
	}
	TestEqual(TEXT("A"), TNVRControls::KeyName(FTNVRKeys::A).ToString(), FString(TEXT("A")));
	TestEqual(TEXT("B"), TNVRControls::KeyName(FTNVRKeys::B).ToString(), FString(TEXT("B")));
	TestEqual(TEXT("X"), TNVRControls::KeyName(FTNVRKeys::X).ToString(), FString(TEXT("X")));
	TestEqual(TEXT("Y"), TNVRControls::KeyName(FTNVRKeys::Y).ToString(), FString(TEXT("Y")));
	// El gatillo como botón y como eje se llaman igual, y cada lado, distinto.
	TestTrue(TEXT("Gatillo derecho: botón y eje"), TNVRControls::KeyName(FTNVRKeys::RightTrigger).EqualTo(TNVRControls::KeyName(FTNVRKeys::RightTriggerAxis)));
	TestFalse(TEXT("Gatillo derecho e izquierdo"), TNVRControls::KeyName(FTNVRKeys::RightTrigger).EqualTo(TNVRControls::KeyName(FTNVRKeys::LeftTrigger)));
	TestTrue(TEXT("Agarre derecho: botón y eje"), TNVRControls::KeyName(FTNVRKeys::RightGrip).EqualTo(TNVRControls::KeyName(FTNVRKeys::RightGripAxis)));
	TestFalse(TEXT("Agarre y gatillo"), TNVRControls::KeyName(FTNVRKeys::RightGrip).EqualTo(TNVRControls::KeyName(FTNVRKeys::RightTrigger)));
	TestTrue(TEXT("El stick y sus direcciones se llaman igual"), TNVRControls::KeyName(FTNVRKeys::LeftStickX).EqualTo(TNVRControls::KeyName(FTNVRKeys::LeftStickUp)));
	TestFalse(TEXT("Stick izquierdo y derecho"), TNVRControls::KeyName(FTNVRKeys::LeftStickX).EqualTo(TNVRControls::KeyName(FTNVRKeys::RightStickX)));
	TestFalse(TEXT("El stick y su clic"), TNVRControls::KeyName(FTNVRKeys::LeftStickX).EqualTo(TNVRControls::KeyName(FTNVRKeys::LeftStickClick)));
	// Lo que no es de los Touch no lo nombra esta tabla (lo nombra KeyDisplayName).
	TestTrue(TEXT("Una tecla no es Touch"), TNVRControls::KeyName(EKeys::E).IsEmpty());
	TestTrue(TEXT("Un botón del mando no es Touch"), TNVRControls::KeyName(EKeys::Gamepad_FaceButton_Bottom).IsEmpty());
	TestTrue(TEXT("Sin tecla, sin nombre"), TNVRControls::KeyName(FKey()).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRControlsActionKeysTest,
	"Tortunabo.VR.Controls.ActionKeys",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRControlsActionKeysTest::RunTest(const FString& Parameters)
{
	// Lo que dice Docs/Modo_VR.md («Controles») y lo que asigna ATN_VRRig::EnsureVRMapping.
	TestTrue(TEXT("Interactuar: gatillo derecho"), TNVRControls::KeyForAction(TEXT("IA_Interact")) == FTNVRKeys::RightTrigger);
	TestTrue(TEXT("Saltar: A"), TNVRControls::KeyForAction(TEXT("IA_Jump")) == FTNVRKeys::A);
	TestTrue(TEXT("Caparazón: B"), TNVRControls::KeyForAction(TEXT("IA_Shell")) == FTNVRKeys::B);
	TestTrue(TEXT("Cambiar de objeto: X"), TNVRControls::KeyForAction(TEXT("IA_RotateInventory")) == FTNVRKeys::X);
	TestTrue(TEXT("Bailes: Y"), TNVRControls::KeyForAction(TEXT("IA_OpenEmoteWheel")) == FTNVRKeys::Y);
	TestTrue(TEXT("Frases: gatillo izquierdo"), TNVRControls::KeyForAction(TEXT("IA_OpenChatWheel")) == FTNVRKeys::LeftTrigger);
	TestTrue(TEXT("Correr: agarre izquierdo"), TNVRControls::KeyForAction(TEXT("IA_Sprint")) == FTNVRKeys::LeftGrip);
	TestTrue(TEXT("Moverse: stick izquierdo"), TNVRControls::KeyForAction(TEXT("IA_Move")) == FTNVRKeys::LeftStickX);
	TestTrue(TEXT("Girar: stick derecho"), TNVRControls::KeyForAction(TEXT("IA_Look")) == FTNVRKeys::RightStickX);
	TestTrue(TEXT("Hablar: clic del stick izquierdo"), TNVRControls::KeyForAction(TEXT("Talk")) == FTNVRKeys::LeftStickClick);
	TestTrue(TEXT("Menú: el botón de menú"), TNVRControls::KeyForAction(TEXT("Pause")) == FTNVRKeys::Menu);
	TestFalse(TEXT("Una acción que no existe no tiene botón"), TNVRControls::KeyForAction(TEXT("IA_Nada")).IsValid());
	for (const TCHAR* Id : { TEXT("IA_Move"), TEXT("IA_Look"), TEXT("IA_Jump"), TEXT("IA_Shell"), TEXT("IA_Interact"), TEXT("IA_Sprint"),
		TEXT("IA_DropItem"), TEXT("IA_RotateInventory"), TEXT("IA_OpenEmoteWheel"), TEXT("IA_OpenChatWheel"), TEXT("IA_RadialNavigate"),
		TEXT("Talk"), TEXT("Pause") })
	{
		const FKey Key = TNVRControls::KeyForAction(Id);
		TestTrue(*FString::Printf(TEXT("%s tiene un botón Touch"), Id), Key.IsValid() && FTNVRKeys::IsVRKey(Key));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRControlsGuideTest,
	"Tortunabo.VR.Controls.Guide",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRControlsGuideTest::RunTest(const FString& Parameters)
{
	// La guía de Ajustes > Controles (#647): cada línea dice qué se hace y con qué botón, y ninguna se repite.
	const TArray<TNVRControls::FGuideLine> Guide = TNVRControls::GetGuide();
	TestTrue(TEXT("Hay una línea por cada cosa que se hace con los mandos"), Guide.Num() >= 12);
	TSet<FString> Labels;
	for (const TNVRControls::FGuideLine& Line : Guide)
	{
		TestFalse(TEXT("Cada línea tiene texto"), Line.Label.IsEmpty());
		TestFalse(*FString::Printf(TEXT("«%s» tiene botones"), *Line.Label.ToString()), Line.Buttons.IsEmpty());
		TestFalse(*FString::Printf(TEXT("«%s» no se repite"), *Line.Label.ToString()), Labels.Contains(Line.Label.ToString()));
		Labels.Add(Line.Label.ToString());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRControlsPickKeyTest,
	"Tortunabo.VR.Controls.PickKey",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRControlsPickKeyTest::RunTest(const FString& Parameters)
{
	// IA_Interact con IMC_Player y el IMC_VR de las gafas puestos: la tecla, el botón del mando y los dos del gatillo Touch.
	const TArray<FKey> Keys = { EKeys::E, EKeys::Gamepad_FaceButton_Left, FTNVRKeys::RightTrigger, FTNVRKeys::RightTriggerAxis };
	TestTrue(TEXT("Con gafas: el gatillo Touch"), TNInputGlyphs::PickKey(Keys, ETNInputDevice::VR) == FTNVRKeys::RightTrigger);
	TestTrue(TEXT("Con mando: su botón, no el de las gafas"), TNInputGlyphs::PickKey(Keys, ETNInputDevice::Gamepad) == EKeys::Gamepad_FaceButton_Left);
	TestTrue(TEXT("Con teclado: la tecla"), TNInputGlyphs::PickKey(Keys, ETNInputDevice::KeyboardMouse) == EKeys::E);
	// Solo las del teclado y el mando (sin gafas, el caso de siempre): con gafas se enseña la que haya.
	const TArray<FKey> NoVR = { EKeys::E, EKeys::Gamepad_FaceButton_Left };
	TestTrue(TEXT("Sin botón Touch con gafas, el primero que haya"), TNInputGlyphs::PickKey(NoVR, ETNInputDevice::VR) == EKeys::E);
	TestTrue(TEXT("Sin gafas, nada cambia (mando)"), TNInputGlyphs::PickKey(NoVR, ETNInputDevice::Gamepad) == EKeys::Gamepad_FaceButton_Left);
	TestTrue(TEXT("Sin gafas, nada cambia (teclado)"), TNInputGlyphs::PickKey(NoVR, ETNInputDevice::KeyboardMouse) == EKeys::E);
	// Un gatillo Touch como eje sale como el botón (el nombre es el mismo).
	const TArray<FKey> AxisOnly = { FTNVRKeys::LeftTriggerAxis };
	TestTrue(TEXT("El eje del gatillo Touch no se convierte en el del mando"), TNInputGlyphs::PickKey(AxisOnly, ETNInputDevice::VR) == FTNVRKeys::LeftTriggerAxis);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRControlsDeviceTest,
	"Tortunabo.VR.Controls.Device",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRControlsDeviceTest::RunTest(const FString& Parameters)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	UTN_InputDeviceSubsystem* Devices = NewObject<UTN_InputDeviceSubsystem>(GameInstance);
	{
		TNVRControlsTest::FScopedMode Off(ETNVRMode::Off);
		TestTrue(TEXT("Sin VR: teclado, como siempre"), Devices->GetDevice() == ETNInputDevice::KeyboardMouse);
		Devices->NoteDevice(0, ETNInputDevice::Gamepad);
		TestTrue(TEXT("Sin VR: el mando sigue siendo mando"), Devices->GetDevice() == ETNInputDevice::Gamepad && Devices->IsUsingGamepad());
		TestFalse(TEXT("Sin VR: no es VR"), Devices->IsUsingVR());
	}
	for (const ETNVRMode Mode : { ETNVRMode::Headset, ETNVRMode::Simulated })
	{
		TNVRControlsTest::FScopedMode Scoped(Mode);
		TestTrue(TEXT("Con VR el aparato es VR (aunque se haya tocado el mando)"), Devices->GetDevice() == ETNInputDevice::VR);
		TestTrue(TEXT("IsUsingVR"), Devices->IsUsingVR());
		TestFalse(TEXT("Con VR no cuenta como mando (sensibilidad, ruedas)"), Devices->IsUsingGamepad());
	}
	TestTrue(TEXT("Al apagar VR, vuelve el mando"), Devices->GetDevice() == ETNInputDevice::Gamepad);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRGhostViewTest,
	"Tortunabo.VR.GhostView",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRGhostViewTest::RunTest(const FString& Parameters)
{
	using namespace TNVRMath;
	const FVector Turtle(1000.0, -500.0, 40.0);
	// Rumbo fijo hacia +X: la cabeza detrás (-X) y arriba, sobre la tortuga.
	const FVector Behind = GhostViewLocation(Turtle, 0.f);
	TestTrue(TEXT("Detrás del rumbo"), FMath::IsNearlyEqual(Behind.X, Turtle.X - GhostViewBack, 0.01));
	TestTrue(TEXT("Misma Y"), FMath::IsNearlyEqual(Behind.Y, Turtle.Y, 0.01));
	TestTrue(TEXT("Más alta"), FMath::IsNearlyEqual(Behind.Z, Turtle.Z + GhostViewUp, 0.01));
	// Rumbo hacia +Y: detrás es -Y.
	const FVector BehindY = GhostViewLocation(Turtle, 90.f);
	TestTrue(TEXT("Detrás del rumbo (90°)"), FMath::IsNearlyEqual(BehindY.Y, Turtle.Y - GhostViewBack, 0.01) && FMath::IsNearlyEqual(BehindY.X, Turtle.X, 0.01));
	// La posición sigue a la tortuga 1 a 1 y sin retardo; el rumbo no depende de ella (solo del rumbo fijo).
	const FVector Moved = GhostViewLocation(Turtle + FVector(300.0, 0.0, 0.0), 0.f);
	TestTrue(TEXT("Sigue a la tortuga al instante"), (Moved - Behind).Equals(FVector(300.0, 0.0, 0.0), 0.01));
	// La base de la vista solo tiene rumbo: el cabeceo y el alabeo los pone la cabeza del jugador.
	const FRotator Rotation = GhostViewRotation(135.f);
	TestTrue(TEXT("Rumbo fijo"), FMath::IsNearlyEqual(Rotation.Yaw, 135.0, 0.01));
	TestTrue(TEXT("Sin cabeceo"), FMath::IsNearlyZero(Rotation.Pitch));
	TestTrue(TEXT("Sin alabeo"), FMath::IsNearlyZero(Rotation.Roll));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRViewCoverTest,
	"Tortunabo.VR.ViewCover",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRViewCoverTest::RunTest(const FString& Parameters)
{
	// La cáscara de pantalla entera pide a la esfera del rig cuánto tapar (0-1).
	const float Previous = TNVR::GetViewCover();
	TNVR::SetViewCover(0.5f);
	TestTrue(TEXT("Lo que se pide"), FMath::IsNearlyEqual(TNVR::GetViewCover(), 0.5f));
	TNVR::SetViewCover(3.f);
	TestTrue(TEXT("No pasa de 1"), FMath::IsNearlyEqual(TNVR::GetViewCover(), 1.f));
	TNVR::SetViewCover(-1.f);
	TestTrue(TEXT("No baja de 0"), FMath::IsNearlyZero(TNVR::GetViewCover()));
	TNVR::SetViewCover(Previous);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRSettingsTest,
	"Tortunabo.VR.Settings",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRSettingsTest::RunTest(const FString& Parameters)
{
	using namespace TNVRHands;
	// De serie: la viñeta normal y la vibración encendida (como las variables de consola de antes).
	const FTNGameSettings Defaults;
	TestEqual(TEXT("Viñeta de serie: normal"), static_cast<int32>(Defaults.VRVignette), 1);
	TestTrue(TEXT("Vibración de serie: encendida"), Defaults.bVRHaptics);
	TestEqual(TEXT("Normal = 1"), VignetteStrengthFromSetting(1), 1.f);
	TestEqual(TEXT("Apagada = 0"), VignetteStrengthFromSetting(0), 0.f);
	TestEqual(TEXT("Fuerte = 2"), VignetteStrengthFromSetting(2), 2.f);
	TestEqual(TEXT("Un valor guardado de más se recorta"), VignetteStrengthFromSetting(9), 2.f);
	TestEqual(TEXT("Vibración encendida"), HapticScaleFromSetting(true), 1.f);
	TestEqual(TEXT("Vibración apagada"), HapticScaleFromSetting(false), 0.f);
	// La consola manda si se ha tocado; si no, el ajuste.
	TestEqual(TEXT("Consola sin tocar: el ajuste"), ConsoleOrSetting(1.f, false, 2.f), 2.f);
	TestEqual(TEXT("Consola tocada: la consola"), ConsoleOrSetting(0.f, true, 2.f), 0.f);
	// El ajuste apagado apaga la viñeta de verdad (ComfortVignette con fuerza 0 es 0) y el doble la dobla.
	TestEqual(TEXT("Apagada: sin viñeta"), ComfortVignette(3000.f, 0.f, VignetteStrengthFromSetting(0)), 0.f);
	TestTrue(TEXT("Fuerte: el doble que normal"), FMath::IsNearlyEqual(ComfortVignette(3000.f, 0.f, VignetteStrengthFromSetting(2)),
		2.f * ComfortVignette(3000.f, 0.f, VignetteStrengthFromSetting(1))));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
