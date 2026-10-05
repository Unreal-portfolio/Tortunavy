// Cuentas del modo VR sin mundo ni actores (VR/TN_VRMath.h, Docs/Modo_VR.md): el rayo del puntero contra el panel de la
// interfaz, el HUD que sigue a la cabeza con retraso, el giro por pasos, la distancia y la escala del panel, los botones de
// los mandos en los menús, el umbral de los gatillos, la velocidad de la mano respecto del cuerpo, el arco del menú sin
// gafas y la tecla de cambiar de cámara. Se pueden correr sin gafas.
// Correr desde Session Frontend (categoría "Tortunabo.VR") o sin ventana:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.VR; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputTriggers.h"
#include "Settings/TN_SettingsSaveGame.h"
#include "UObject/Package.h"
#include "VR/TN_VRMath.h"
#include "VR/TN_VRMode.h"
#include "VR/TN_VRInputTriggers.h"
#include "VR/TN_VRRig.h"


#if WITH_DEV_AUTOMATION_TESTS

namespace TNVRTest
{
	/** Tamaño de dibujo del panel de la interfaz (UTN_VRScreenWidget: 1920 × 1080). */
	const FVector2D VRPanelSize(1920.0, 1080.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Puntero contra el panel
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRRayPanelHitTest,
	"Tortunabo.VR.RayPanelHit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRRayPanelHitTest::RunTest(const FString& Parameters)
{
	using namespace TNVRTest;
	FVector Hit;
	FVector2D UV;

	// Panel en el origen mirando a +X; el rayo viene de delante.
	const FTransform Identity = FTransform::Identity;
	TestTrue(TEXT("De frente al centro: toca"), TNVRMath::RayPanelHit(FVector(100.0, 0.0, 0.0), FVector(-1.0, 0.0, 0.0), Identity, VRPanelSize, Hit, UV));
	TestTrue(TEXT("Centro → UV (0,5; 0,5)"), UV.Equals(FVector2D(0.5, 0.5), 1e-4));
	TestTrue(TEXT("Punto tocado en el plano del panel"), Hit.Equals(FVector::ZeroVector, 1e-3));

	// Visto desde delante, la derecha es -Y y arriba +Z.
	TestTrue(TEXT("Arriba a la derecha: toca"), TNVRMath::RayPanelHit(FVector(100.0, -480.0, 270.0), FVector(-1.0, 0.0, 0.0), Identity, VRPanelSize, Hit, UV));
	TestTrue(TEXT("Arriba a la derecha → UV (0,75; 0,25)"), UV.Equals(FVector2D(0.75, 0.25), 1e-4));

	TestFalse(TEXT("Rayo que se aleja del panel: no toca"), TNVRMath::RayPanelHit(FVector(100.0, 0.0, 0.0), FVector(1.0, 0.0, 0.0), Identity, VRPanelSize, Hit, UV));
	TestFalse(TEXT("Rayo paralelo al panel: no toca"), TNVRMath::RayPanelHit(FVector(100.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), Identity, VRPanelSize, Hit, UV));
	TestFalse(TEXT("Fuera del borde: no toca"), TNVRMath::RayPanelHit(FVector(100.0, 1000.0, 0.0), FVector(-1.0, 0.0, 0.0), Identity, VRPanelSize, Hit, UV));

	// Con escala (el panel mide unos centímetros en el mundo) y girado: las cuentas van en el espacio del panel.
	const FTransform Placed(FRotator(0.0, 90.0, 0.0), FVector(0.0, 200.0, 150.0), FVector(0.1));
	// Girado 90° el panel mira a +Y; su derecha (vista desde delante) queda en +X.
	TestTrue(TEXT("Panel girado y escalado: toca"), TNVRMath::RayPanelHit(FVector(48.0, 300.0, 150.0), FVector(0.0, -1.0, 0.0), Placed, VRPanelSize, Hit, UV));
	TestTrue(TEXT("Panel girado y escalado → UV (0,75; 0,5)"), UV.Equals(FVector2D(0.75, 0.5), 1e-3));
	TestTrue(TEXT("Punto tocado en el mundo"), Hit.Equals(FVector(48.0, 200.0, 150.0), 1e-2));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// HUD que sigue a la cabeza
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRLazyFollowTest,
	"Tortunabo.VR.LazyFollowYaw",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRLazyFollowTest::RunTest(const FString& Parameters)
{
	constexpr float Dt = 1.f / 72.f;
	bool bFollowing = false;

	// Mirar un poco a un lado no mueve el HUD.
	float Yaw = TNVRMath::LazyFollowYaw(0.f, 10.f, Dt, bFollowing);
	TestEqual(TEXT("Cabeza a 10°: el HUD no se mueve"), Yaw, 0.f);
	TestFalse(TEXT("Cabeza a 10°: no empieza a seguir"), bFollowing);

	// Girar la cabeza del todo: el HUD va detrás y se para cerca.
	Yaw = TNVRMath::LazyFollowYaw(0.f, 40.f, Dt, bFollowing);
	TestTrue(TEXT("Cabeza a 40°: empieza a seguir"), bFollowing);
	TestTrue(TEXT("Cabeza a 40°: se mueve hacia ella"), Yaw > 0.f && Yaw < 40.f);
	for (int32 Step = 0; Step < 200 && bFollowing; ++Step)
	{
		Yaw = TNVRMath::LazyFollowYaw(Yaw, 40.f, Dt, bFollowing);
	}
	TestFalse(TEXT("Llega y deja de seguir"), bFollowing);
	TestTrue(TEXT("Se para a menos de 2° de la cabeza"), FMath::Abs(Yaw - 40.f) < 2.f);

	// Por el lado corto al pasar de 180 a -180.
	bFollowing = false;
	Yaw = TNVRMath::LazyFollowYaw(170.f, -150.f, Dt, bFollowing);
	TestTrue(TEXT("De 170° a -150°: sigue"), bFollowing);
	TestTrue(TEXT("De 170° a -150°: gira por el lado corto (hacia 180)"), Yaw > 170.f || Yaw < -170.f);
	for (int32 Step = 0; Step < 200 && bFollowing; ++Step)
	{
		Yaw = TNVRMath::LazyFollowYaw(Yaw, -150.f, Dt, bFollowing);
	}
	TestTrue(TEXT("De 170° a -150°: acaba junto a -150°"), FMath::Abs(FRotator::NormalizeAxis(static_cast<double>(Yaw) + 150.0)) < 2.0);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Giro por pasos
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRSnapTurnTest,
	"Tortunabo.VR.SnapTurnStep",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRSnapTurnTest::RunTest(const FString& Parameters)
{
	bool bLatched = false;
	TestEqual(TEXT("Stick casi quieto: no gira"), TNVRMath::SnapTurnStep(0.3f, bLatched), 0);
	TestEqual(TEXT("Stick a la derecha: un paso a la derecha"), TNVRMath::SnapTurnStep(0.8f, bLatched), 1);
	TestEqual(TEXT("Sin soltar: no da otro"), TNVRMath::SnapTurnStep(0.95f, bLatched), 0);
	TestEqual(TEXT("A medio volver (0,5): tampoco"), TNVRMath::SnapTurnStep(0.5f, bLatched), 0);
	TestEqual(TEXT("Suelto: nada"), TNVRMath::SnapTurnStep(0.1f, bLatched), 0);
	TestFalse(TEXT("Suelto: listo para otro"), bLatched);
	TestEqual(TEXT("Stick a la izquierda: un paso a la izquierda"), TNVRMath::SnapTurnStep(-0.9f, bLatched), -1);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Distancia y escala del panel
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRPanelPlacementTest,
	"Tortunabo.VR.PanelPlacement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRPanelPlacementTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Sin nada delante: a la distancia pedida"), TNVRMath::PanelDistance(140.f, false, 0.f), 140.f);
	TestEqual(TEXT("Pared a 100 cm: justo delante de ella"), TNVRMath::PanelDistance(140.f, true, 100.f), 92.f);
	TestEqual(TEXT("Pared pegada: nunca más cerca que el mínimo"), TNVRMath::PanelDistance(140.f, true, 10.f), 40.f);
	TestEqual(TEXT("Algo más lejos que el panel: no cambia"), TNVRMath::PanelDistance(140.f, true, 500.f), 140.f);

	// 50° de ancho a 140 cm: 2 · 140 · tan 25° ≈ 130,6 cm repartidos en 1920 píxeles.
	const float Scale = TNVRMath::PanelScale(140.f, 50.f, 1920.f);
	TestTrue(TEXT("Escala del HUD: ~130,6 cm de ancho"), FMath::IsNearlyEqual(Scale * 1920.f, 130.56f, 0.1f));
	TestTrue(TEXT("Más lejos, más grande (mismo ángulo)"), TNVRMath::PanelScale(280.f, 50.f, 1920.f) > Scale * 1.99f);
	TestTrue(TEXT("Ancho de dibujo 0: sin dividir por cero"), FMath::IsFinite(TNVRMath::PanelScale(140.f, 50.f, 0.f)));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mandos en los menús
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRMenuKeysTest,
	"Tortunabo.VR.MenuKeys",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRMenuKeysTest::RunTest(const FString& Parameters)
{
	// Los sticks mueven por el menú en cuatro direcciones.
	TestEqual(TEXT("Stick arriba"), TNVRMath::StickDirection(FVector2D(0.0, 0.9)), 1);
	TestEqual(TEXT("Stick abajo"), TNVRMath::StickDirection(FVector2D(0.1, -0.9)), 2);
	TestEqual(TEXT("Stick izquierda"), TNVRMath::StickDirection(FVector2D(-0.9, 0.2)), 3);
	TestEqual(TEXT("Stick derecha"), TNVRMath::StickDirection(FVector2D(0.9, -0.2)), 4);
	TestEqual(TEXT("Stick poco movido: nada"), TNVRMath::StickDirection(FVector2D(0.3, 0.3)), 0);
	TestTrue(TEXT("Dirección 1 → cruceta arriba"), TNVRMath::DirectionKey(1) == EKeys::Gamepad_DPad_Up);
	TestTrue(TEXT("Dirección 4 → cruceta derecha"), TNVRMath::DirectionKey(4) == EKeys::Gamepad_DPad_Right);
	TestFalse(TEXT("Sin dirección → ninguna tecla"), TNVRMath::DirectionKey(0).IsValid());

	// Los botones hacen lo mismo que los del mando en todos los menús.
	TestTrue(TEXT("A → aceptar"), TNVRMath::MenuKeyFor(FTNVRKeys::A) == EKeys::Gamepad_FaceButton_Bottom);
	TestTrue(TEXT("X → aceptar"), TNVRMath::MenuKeyFor(FTNVRKeys::X) == EKeys::Gamepad_FaceButton_Bottom);
	TestTrue(TEXT("B → atrás"), TNVRMath::MenuKeyFor(FTNVRKeys::B) == EKeys::Gamepad_FaceButton_Right);
	TestTrue(TEXT("Y → atrás"), TNVRMath::MenuKeyFor(FTNVRKeys::Y) == EKeys::Gamepad_FaceButton_Right);
	TestTrue(TEXT("Agarre izquierdo → pestaña anterior"), TNVRMath::MenuKeyFor(FTNVRKeys::LeftGrip) == EKeys::Gamepad_LeftShoulder);
	TestTrue(TEXT("Agarre derecho → pestaña siguiente"), TNVRMath::MenuKeyFor(FTNVRKeys::RightGrip) == EKeys::Gamepad_RightShoulder);
	TestTrue(TEXT("Menú → Start"), TNVRMath::MenuKeyFor(FTNVRKeys::Menu) == EKeys::Gamepad_Special_Right);
	TestTrue(TEXT("Stick derecho abajo → cruceta abajo"), TNVRMath::MenuKeyFor(FTNVRKeys::RightStickDown) == EKeys::Gamepad_DPad_Down);
	TestFalse(TEXT("Gatillo: es el clic del puntero, no una tecla"), TNVRMath::MenuKeyFor(FTNVRKeys::RightTrigger).IsValid());

	// X e Y tienen una segunda acción antes de caer en aceptar o atrás (#648): borrar, refrescar, quitar una tecla.
	TestTrue(TEXT("X: la X del mando primero (borrar)"), TNVRMath::SecondaryMenuKeyFor(FTNVRKeys::X) == EKeys::Gamepad_FaceButton_Left);
	TestTrue(TEXT("Y: la Y del mando primero (refrescar, quitar tecla)"), TNVRMath::SecondaryMenuKeyFor(FTNVRKeys::Y) == EKeys::Gamepad_FaceButton_Top);
	TestFalse(TEXT("A no tiene segunda acción"), TNVRMath::SecondaryMenuKeyFor(FTNVRKeys::A).IsValid());
	TestFalse(TEXT("B no tiene segunda acción"), TNVRMath::SecondaryMenuKeyFor(FTNVRKeys::B).IsValid());

	// El stick derecho gira la tortuga de la tienda y el probador: sin cruceta mientras lo reservan.
	TestTrue(TEXT("Stick derecho a la izquierda es una dirección del stick derecho"), TNVRMath::IsRightStickDirection(FTNVRKeys::RightStickLeft));
	TestFalse(TEXT("El stick izquierdo no lo es"), TNVRMath::IsRightStickDirection(FTNVRKeys::LeftStickLeft));
	TestEqual(TEXT("Zona muerta: no gira"), TNVRMath::StickSpinDegrees(0.15f, 0.1f), 0.f);
	TestTrue(TEXT("Stick a la derecha gira en negativo, como arrastrar el ratón a la derecha"), TNVRMath::StickSpinDegrees(1.f, 0.1f) < 0.f);
	TestTrue(TEXT("Stick a la izquierda gira en positivo"), TNVRMath::StickSpinDegrees(-1.f, 0.1f) > 0.f);
	TestEqual(TEXT("A fondo, 160 grados por segundo"), TNVRMath::StickSpinDegrees(1.f, 1.f), -160.f);
	TestEqual(TEXT("Sin tiempo no gira"), TNVRMath::StickSpinDegrees(1.f, 0.f), 0.f);

	TestTrue(TEXT("A es un botón de los mandos VR"), FTNVRKeys::IsVRKey(FTNVRKeys::A));
	TestFalse(TEXT("Un botón del mando normal no lo es"), FTNVRKeys::IsVRKey(EKeys::Gamepad_FaceButton_Bottom));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Panel curvo (menús y HUD alrededor de los ojos)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRCurvedPanelTest,
	"Tortunabo.VR.CurvedPanel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRCurvedPanelTest::RunTest(const FString& Parameters)
{
	using namespace TNVRTest;
	constexpr float Arc = 90.f;
	const double Radius = TNVRMath::CurvedPanelRadius(VRPanelSize, Arc);
	TestTrue(TEXT("Radio = ancho / arco"), FMath::IsNearlyEqual(Radius, 1920.0 / (UE_DOUBLE_PI * 0.5), 1e-6));

	// Los puntos del panel están a Radius del eje (Radius, 0): el panel rodea los ojos.
	for (const FVector2D UV : { FVector2D(0.0, 0.5), FVector2D(0.25, 0.1), FVector2D(0.5, 0.5), FVector2D(1.0, 0.9) })
	{
		const FVector P = TNVRMath::CurvedPanelPoint(UV, VRPanelSize, Arc);
		TestTrue(TEXT("Punto del panel a un radio del eje"), FMath::IsNearlyEqual(FVector2D(P.X - Radius, P.Y).Size(), Radius, 1e-6));
	}
	TestTrue(TEXT("El centro del panel en el origen"), TNVRMath::CurvedPanelPoint(FVector2D(0.5, 0.5), VRPanelSize, Arc).Equals(FVector::ZeroVector, 1e-6));
	const FVector Left = TNVRMath::CurvedPanelPoint(FVector2D(0.0, 0.5), VRPanelSize, Arc);
	TestTrue(TEXT("El borde izquierdo (vista desde delante) está en +Y y hacia los ojos (+X)"), Left.Y > 0.0 && Left.X > 0.0);

	// Desde el eje (los ojos), cada rayo toca el punto que le corresponde y devuelve su UV.
	const FVector Eye(Radius, 0.0, 0.0);
	FVector Hit;
	FVector2D UV;
	for (const FVector2D Want : { FVector2D(0.5, 0.5), FVector2D(0.1, 0.2), FVector2D(0.9, 0.8), FVector2D(0.02, 0.5) })
	{
		const FVector Target = TNVRMath::CurvedPanelPoint(Want, VRPanelSize, Arc);
		TestTrue(TEXT("Rayo desde los ojos: toca"), TNVRMath::RayCurvedPanelHit(Eye, (Target - Eye).GetSafeNormal(), FTransform::Identity, VRPanelSize, Arc, Hit, UV));
		TestTrue(TEXT("Rayo desde los ojos → su UV"), UV.Equals(Want, 1e-6));
		TestTrue(TEXT("Rayo desde los ojos → su punto"), Hit.Equals(Target, 1e-3));
	}
	TestFalse(TEXT("Hacia atrás: no toca"), TNVRMath::RayCurvedPanelHit(Eye, FVector(1.0, 0.0, 0.0), FTransform::Identity, VRPanelSize, Arc, Hit, UV));
	TestFalse(TEXT("Por encima del panel: no toca"), TNVRMath::RayCurvedPanelHit(Eye, FVector(-1.0, 0.0, 2.0).GetSafeNormal(), FTransform::Identity, VRPanelSize, Arc, Hit, UV));
	TestFalse(TEXT("Fuera del arco (a 60° con 90° de arco): no toca"), TNVRMath::RayCurvedPanelHit(Eye, FRotator(0.0, 180.0 - 60.0, 0.0).Vector(), FTransform::Identity, VRPanelSize, Arc, Hit, UV));

	// Con la escala de CurvedPanelScale, el radio en el mundo es la distancia pedida: los ojos, en el eje.
	const float Scale = TNVRMath::CurvedPanelScale(160.f, Arc, static_cast<float>(VRPanelSize.X));
	TestTrue(TEXT("Radio en el mundo = distancia"), FMath::IsNearlyEqual(Radius * Scale, 160.0, 1e-3));
	const FTransform Placed(FRotator(0.0, 180.0, 0.0), FVector(160.0, 0.0, 0.0), FVector(Scale));
	TestTrue(TEXT("Panel colocado: desde los ojos al frente toca el centro"),
		TNVRMath::RayCurvedPanelHit(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), Placed, VRPanelSize, Arc, Hit, UV));
	TestTrue(TEXT("Panel colocado → UV (0,5; 0,5)"), UV.Equals(FVector2D(0.5, 0.5), 1e-6));
	TestTrue(TEXT("Panel colocado: a 160 cm"), Hit.Equals(FVector(160.0, 0.0, 0.0), 1e-3));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Gatillos y agarres analógicos, y lo que se suelta de la mano
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRAnalogButtonTest,
	"Tortunabo.VR.AnalogButton",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRAnalogButtonTest::RunTest(const FString& Parameters)
{
	bool bHeld = false;
	TestEqual(TEXT("Dedo apoyado (0,2): nada"), TNVRMath::AnalogButton(0.2f, bHeld), 0);
	TestEqual(TEXT("Apretar (0,6): pulsa"), TNVRMath::AnalogButton(0.6f, bHeld), 1);
	TestTrue(TEXT("Queda apretado"), bHeld);
	TestEqual(TEXT("Mantener: nada"), TNVRMath::AnalogButton(0.9f, bHeld), 0);
	TestEqual(TEXT("Aflojar un poco (0,45): sigue apretado"), TNVRMath::AnalogButton(0.45f, bHeld), 0);
	TestEqual(TEXT("Soltar (0,1): suelta"), TNVRMath::AnalogButton(0.1f, bHeld), -1);
	TestFalse(TEXT("Queda suelto"), bHeld);
	TestEqual(TEXT("Rozar el umbral (0,5): nada"), TNVRMath::AnalogButton(0.5f, bHeld), 0);

	TestTrue(TEXT("Mano lenta: sale con su velocidad"), TNVRMath::ThrowVelocity(FVector(300.0, 0.0, 100.0)).Equals(FVector(300.0, 0.0, 100.0)));
	TestTrue(TEXT("Mano muy rápida: con tope"), FMath::IsNearlyEqual(TNVRMath::ThrowVelocity(FVector(0.0, 5000.0, 0.0)).Size(), 1600.0, 1e-3));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Gatillo: umbral del 55 % en el juego y nada de clics al abrir un menú con él apretado
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRTriggerThresholdTest,
	"Tortunabo.VR.TriggerThreshold",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRTriggerThresholdTest::RunTest(const FString& Parameters)
{
	// El disparador de las asignaciones de los gatillos (IA_Interact, IA_OpenChatWheel): «Down» al 55 % con histéresis. Las
	// acciones son booleanas, pero Enhanced Input les pasa el valor del gatillo tal cual (0,3 sigue siendo 0,3) y el
	// disparador lo mira.
	UInputTrigger* Trigger = ATN_VRRig::MakeAnalogPressTrigger(GetTransientPackage());
	UTN_InputTriggerAnalogDown* Analog = Cast<UTN_InputTriggerAnalogDown>(Trigger);
	TestTrue(TEXT("Es un disparador «Down» con histéresis"), Analog != nullptr);
	if (!Trigger || !Analog)
	{
		return false;
	}
	TestEqual(TEXT("Umbral: el del botón analógico (55 %)"), Trigger->ActuationThreshold, TNVRMath::AnalogPressThreshold);
	const auto Pressed = [Trigger](float TriggerValue)
	{
		return Trigger->IsActuated(FInputActionValue(EInputActionValueType::Boolean, FVector(static_cast<double>(TriggerValue), 0.0, 0.0)));
	};
	TestFalse(TEXT("Dedo apoyado (0,1): no interactúa"), Pressed(0.1f));
	TestFalse(TEXT("Medio gatillo (0,5): no interactúa"), Pressed(0.5f));
	TestTrue(TEXT("Apretado (0,6): interactúa"), Pressed(0.6f));
	TestTrue(TEXT("A fondo (1): interactúa"), Pressed(1.f));

	// Mantener (rebuscar, cofres): el gatillo ronda el 55 % sin soltarse. Con un «Down» sin histéresis, bajar a 0,5 cortaba
	// lo que se mantenía; ahora sigue hasta bajar del 35 %.
	const auto Update = [Analog](float TriggerValue)
	{
		return Analog->UpdateState(nullptr, FInputActionValue(EInputActionValueType::Boolean, FVector(static_cast<double>(TriggerValue), 0.0, 0.0)), 1.f / 72.f);
	};
	TestEqual(TEXT("Medio gatillo sin haberlo apretado: nada"), Update(0.5f), ETriggerState::None);
	TestEqual(TEXT("Apretado (0,6): activo"), Update(0.6f), ETriggerState::Triggered);
	TestEqual(TEXT("Ronda el umbral (0,5): sigue activo"), Update(0.5f), ETriggerState::Triggered);
	TestEqual(TEXT("Aflojado a 0,4: sigue activo"), Update(0.4f), ETriggerState::Triggered);
	TestEqual(TEXT("Soltado (0,2): nada"), Update(0.2f), ETriggerState::None);
	TestEqual(TEXT("Otra vez a medias (0,5): nada"), Update(0.5f), ETriggerState::None);

	// Menús (FTNVRInputProcessor): el estado del gatillo se sigue también jugando. Abrir un menú con el gatillo ya apretado
	// no hace clic; hay que soltarlo (por debajo del 35 %) y volver a apretar.
	bool bHeld = false;
	TestEqual(TEXT("Jugando, apretar: se apunta (sin actuar)"), TNVRMath::AnalogButton(0.9f, bHeld), 1);
	TestEqual(TEXT("Ya en el menú, sigue apretado: ningún clic"), TNVRMath::AnalogButton(0.95f, bHeld), 0);
	TestEqual(TEXT("Aflojar a 0,5: tampoco"), TNVRMath::AnalogButton(0.5f, bHeld), 0);
	TestEqual(TEXT("Soltar: sin clic (no se pulsó en el menú)"), TNVRMath::AnalogButton(0.2f, bHeld), -1);
	TestEqual(TEXT("Volver a apretar: ahora sí, clic"), TNVRMath::AnalogButton(0.7f, bHeld), 1);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Velocidad de la mano respecto del cuerpo (lanzar con el gesto)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRHandVelocityTest,
	"Tortunabo.VR.HandVelocity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRHandVelocityTest::RunTest(const FString& Parameters)
{
	constexpr float Dt = 1.f / 72.f;
	constexpr float ThrowSpeed = 250.f;
	// La mano quieta respecto de los ojos: 40 cm delante y 30 abajo.
	const FVector HandLocal(40.0, 0.0, -30.0);
	const FTransform Start(FRotator(0.0, 20.0, 0.0), FVector(100.0, 200.0, 150.0));

	// Andando a 450 cm/s (más que VRThrowSpeed) con la mano quieta: la mano no se mueve respecto del cuerpo.
	const FTransform Walked(Start.GetRotation(), Start.GetLocation() + Start.GetRotation().GetForwardVector() * 450.0 * Dt);
	const FVector Walking = TNVRMath::RelativeHandVelocity(Start, Start.TransformPosition(HandLocal), Walked, Walked.TransformPosition(HandLocal), Dt);
	TestTrue(TEXT("Andando con la mano quieta: velocidad 0"), Walking.IsNearlyZero(1e-2));
	TestFalse(TEXT("Andando con la mano quieta: soltar no lanza"), TNVRMath::IsThrowSwing(Walking, ThrowSpeed));
	// Lo que se medía antes (la mano en el mundo) sí habría lanzado.
	const FVector WorldVelocity = (Walked.TransformPosition(HandLocal) - Start.TransformPosition(HandLocal)) / Dt;
	TestTrue(TEXT("En el mundo la mano iba a 450 cm/s"), FMath::IsNearlyEqual(WorldVelocity.Size(), 450.0, 0.5));

	// Saltando (el origen sube a 500 cm/s): tampoco.
	const FTransform Jumped(Start.GetRotation(), Start.GetLocation() + FVector(0.0, 0.0, 500.0 * Dt));
	TestTrue(TEXT("Saltando con la mano quieta: velocidad 0"),
		TNVRMath::RelativeHandVelocity(Start, Start.TransformPosition(HandLocal), Jumped, Jumped.TransformPosition(HandLocal), Dt).IsNearlyZero(1e-2));

	// Giro de 30° con el stick de un fotograma a otro: la mano, a 50 cm del origen, salta ~26 cm en el mundo; respecto del
	// cuerpo no se ha movido.
	const FTransform Turned(FRotator(0.0, 50.0, 0.0), Start.GetLocation());
	const FVector Turning = TNVRMath::RelativeHandVelocity(Start, Start.TransformPosition(HandLocal), Turned, Turned.TransformPosition(HandLocal), Dt);
	TestTrue(TEXT("Giro a pasos con la mano quieta: velocidad 0"), Turning.IsNearlyZero(1e-2));

	// Un gesto de lanzar de verdad (la mano a 400 cm/s hacia delante respecto del cuerpo) mientras se anda: 400 cm/s hacia
	// donde miran los ojos ahora.
	const FVector Swung = HandLocal + FVector(400.0 * Dt, 0.0, 0.0);
	const FVector Swing = TNVRMath::RelativeHandVelocity(Start, Start.TransformPosition(HandLocal), Walked, Walked.TransformPosition(Swung), Dt);
	TestTrue(TEXT("Gesto de lanzar andando: 400 cm/s"), FMath::IsNearlyEqual(Swing.Size(), 400.0, 0.5));
	TestTrue(TEXT("Gesto de lanzar andando: hacia donde miran los ojos"), Swing.GetSafeNormal().Equals(Walked.GetRotation().GetForwardVector(), 1e-3));
	TestTrue(TEXT("Gesto de lanzar: soltar lanza"), TNVRMath::IsThrowSwing(Swing, ThrowSpeed));
	TestTrue(TEXT("Sin tiempo: velocidad 0"), TNVRMath::RelativeHandVelocity(Start, FVector::ZeroVector, Walked, FVector::OneVector, 0.f).IsZero());

	// Al soltar un objeto con física se suma lo que llevaba el cuerpo (con el mismo tope); lo que se lanza con el gatillo o al
	// compañero, no.
	const FVector Body(450.0, 0.0, 0.0);
	TestTrue(TEXT("Objeto con física: mano + cuerpo"), TNVRMath::ReleaseVelocity(FVector(0.0, 100.0, 0.0), Body, true).Equals(FVector(450.0, 100.0, 0.0)));
	TestTrue(TEXT("Sin el cuerpo: solo la mano"), TNVRMath::ReleaseVelocity(FVector(0.0, 100.0, 0.0), Body, false).Equals(FVector(0.0, 100.0, 0.0)));
	TestTrue(TEXT("Mano + cuerpo: con tope"), FMath::IsNearlyEqual(TNVRMath::ReleaseVelocity(FVector(1500.0, 0.0, 0.0), Body, true).Size(), 1600.0, 1e-3));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Menú sin gafas: el arco que cabe en la ventana
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRSimulatedMenuArcTest,
	"Tortunabo.VR.SimulatedMenuArc",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRSimulatedMenuArcTest::RunTest(const FString& Parameters)
{
	// Cámara de 90° en una ventana 16:9: los 100° del menú no caben; unos 76°, sí.
	const float Arc = TNVRMath::SimulatedMenuArc(100.f, 90.f, 16.f / 9.f);
	TestTrue(TEXT("90° y 16:9: entre 70° y 80°"), Arc > 70.f && Arc < 80.f);
	TestTrue(TEXT("Lo que se pide, si cabe"), FMath::IsNearlyEqual(TNVRMath::SimulatedMenuArc(60.f, 90.f, 16.f / 9.f), 60.f, 1e-3f));
	TestTrue(TEXT("Ventana más alta (4:3): cabe más"), TNVRMath::SimulatedMenuArc(100.f, 90.f, 4.f / 3.f) > Arc);
	TestTrue(TEXT("Panorámica (21:9): cabe menos"), TNVRMath::SimulatedMenuArc(100.f, 90.f, 21.f / 9.f) < Arc);
	TestTrue(TEXT("Por debajo de los ojos (como con gafas): cabe menos"), TNVRMath::SimulatedMenuArc(100.f, 90.f, 16.f / 9.f, 0.1f) < Arc);

	// El que sale cabe (con el margen) y uno un poco mayor ya no.
	const double TanH = FMath::Tan(FMath::DegreesToRadians(45.0)) * 0.85;
	const double TanV = TanH / (16.0 / 9.0);
	const double DrawAspect = 1080.0 / 1920.0;
	TestTrue(TEXT("El arco que sale cabe"), TNVRMath::CurvedPanelFits(FMath::DegreesToRadians(static_cast<double>(Arc)), TanH, TanV, DrawAspect, 0.0));
	TestFalse(TEXT("Dos grados más ya no"), TNVRMath::CurvedPanelFits(FMath::DegreesToRadians(static_cast<double>(Arc) + 2.0), TanH, TanV, DrawAspect, 0.0));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tecla de cambiar de cámara (primera persona sin gafas)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRCameraKeyTest,
	"Tortunabo.VR.CameraKey",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRCameraKeyTest::RunTest(const FString& Parameters)
{
	const FTNGameSettings Defaults;
	const FKey Keyboard(Defaults.CameraKey);
	const FKey Pad(Defaults.CameraPadKey);
	TestTrue(TEXT("De serie tiene tecla"), Keyboard.IsValid());
	TestTrue(TEXT("De serie tiene botón del mando"), Pad.IsValid() && Pad.IsGamepadKey());
	TestFalse(TEXT("La tecla no es un botón del mando"), Keyboard.IsGamepadKey());
	TestTrue(TEXT("Nunca la de hablar (teclado)"), Keyboard != FKey(Defaults.PushToTalkKey));
	TestTrue(TEXT("Nunca la de hablar (mando)"), Pad != FKey(Defaults.PushToTalkPadKey));
	TestTrue(TEXT("Ni la del menú (teclado)"), Keyboard != FKey(Defaults.PauseKey));
	TestTrue(TEXT("Ni la del menú (mando)"), Pad != FKey(Defaults.PausePadKey));
	TestTrue(TEXT("Ni V (la de hablar de serie, que pide el tutorial)"), Keyboard != EKeys::V);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
