// Cuentas de la cabeza que sigue a la cámara en tercera persona (#623, Player/TN_HeadLook.h), sin mundo ni actores: el giro
// respecto del cuerpo con sus topes, la vuelta al frente mirando hacia atrás (sin saltos de un lado a otro) y la guiñada
// en un byte de ida y vuelta.
// Correr desde Session Frontend (categoría "Tortunabo.HeadLook") o sin ventana:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.HeadLook; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_HeadLook.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNHeadLookTargetTest,
	"Tortunabo.HeadLook.Target",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNHeadLookTargetTest::RunTest(const FString& Parameters)
{
	using namespace TNHeadLook;
	constexpr float Tol = 1e-3f;

	// Dentro de los topes, la cabeza sigue a la vista tal cual.
	TestEqual(TEXT("Vista al frente: cabeza al frente (guiñada)"), Target(0.f, 0.f).Yaw, 0.f, Tol);
	TestEqual(TEXT("Vista al frente: cabeza al frente (cabeceo)"), Target(0.f, 0.f).Pitch, 0.f, Tol);
	TestEqual(TEXT("45° a la derecha"), Target(45.f, 0.f).Yaw, 45.f, Tol);
	TestEqual(TEXT("60° a la izquierda"), Target(-60.f, 0.f).Yaw, -60.f, Tol);
	TestEqual(TEXT("Mirando arriba 30°"), Target(0.f, 30.f).Pitch, 30.f, Tol);
	TestEqual(TEXT("Mirando abajo 20°"), Target(0.f, -20.f).Pitch, -20.f, Tol);

	// Topes: guiñada ±MaxYaw y cabeceo de MinPitch a MaxPitch.
	TestEqual(TEXT("80° a la derecha: en el tope"), Target(80.f, 0.f).Yaw, MaxYaw, Tol);
	TestEqual(TEXT("90° a la izquierda: en el tope"), Target(-90.f, 0.f).Yaw, -MaxYaw, Tol);
	TestEqual(TEXT("Mirando muy arriba: en el tope de arriba"), Target(0.f, 80.f).Pitch, MaxPitch, Tol);
	TestEqual(TEXT("Mirando muy abajo: en el tope de abajo"), Target(0.f, -80.f).Pitch, MinPitch, Tol);
	// El cabeceo del mando llega de 0 a 360 (hacia abajo, 330 = -30).
	TestEqual(TEXT("Cabeceo 330 = 30° hacia abajo"), Target(0.f, 330.f).Pitch, -30.f, Tol);
	// La guiñada sin normalizar (giros de más de una vuelta) da lo mismo.
	TestEqual(TEXT("405° = 45° a la derecha"), Target(405.f, 0.f).Yaw, 45.f, Tol);
	TestEqual(TEXT("-315° = 45° a la derecha"), Target(-315.f, 0.f).Yaw, 45.f, Tol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNHeadLookBehindTest,
	"Tortunabo.HeadLook.Behind",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNHeadLookBehindTest::RunTest(const FString& Parameters)
{
	using namespace TNHeadLook;
	constexpr float Tol = 1e-3f;

	// Pasado el tope, se queda en él hasta HoldYaw.
	TestEqual(TEXT("En HoldYaw sigue en el tope"), Target(HoldYaw, 0.f).Yaw, MaxYaw, Tol);
	TestEqual(TEXT("En -HoldYaw sigue en el tope"), Target(-HoldYaw, 0.f).Yaw, -MaxYaw, Tol);

	// Mirando hacia atrás, al frente: guiñada y cabeceo.
	TestEqual(TEXT("Justo detrás: al frente"), Target(180.f, 0.f).Yaw, 0.f, Tol);
	TestEqual(TEXT("Justo detrás (-180): al frente"), Target(-180.f, 0.f).Yaw, 0.f, Tol);
	TestEqual(TEXT("Detrás y mirando arriba: cabeceo al frente"), Target(170.f, 40.f).Pitch, 0.f, Tol);
	TestEqual(TEXT("En FrontYaw: al frente"), Target(FrontYaw, 0.f).Yaw, 0.f, Tol);

	// Sin saltos de un lado a otro: a un lado y a otro de la espalda, casi lo mismo (al frente).
	const float JustRight = Target(179.f, 20.f).Yaw;
	const float JustLeft = Target(-179.f, 20.f).Yaw;
	TestTrue(TEXT("179° y -179°: sin salto de un lado a otro"), FMath::Abs(JustRight - JustLeft) < 0.5f);

	// Continua: al barrer la vista de -180 a 180 en pasos de 0,5°, la cabeza nunca da un salto de más de 2°.
	float MaxJump = 0.f;
	float MaxPitchJump = 0.f;
	FAngles Prev = Target(-180.f, 30.f);
	for (float View = -179.5f; View <= 180.f; View += 0.5f)
	{
		const FAngles Now = Target(View, 30.f);
		MaxJump = FMath::Max(MaxJump, FMath::Abs(Now.Yaw - Prev.Yaw));
		MaxPitchJump = FMath::Max(MaxPitchJump, FMath::Abs(Now.Pitch - Prev.Pitch));
		Prev = Now;
	}
	TestTrue(FString::Printf(TEXT("Guiñada continua (salto máximo %.2f°)"), MaxJump), MaxJump < 2.f);
	TestTrue(FString::Printf(TEXT("Cabeceo continuo (salto máximo %.2f°)"), MaxPitchJump), MaxPitchJump < 2.f);

	// Al volver al frente, cada vez menos girada (nunca se pasa al otro lado).
	float Last = MaxYaw;
	bool bMonotonic = true;
	for (float View = HoldYaw; View <= 180.f; View += 1.f)
	{
		const float Yaw = Target(View, 0.f).Yaw;
		bMonotonic &= Yaw <= Last + Tol && Yaw >= -Tol;
		Last = Yaw;
	}
	TestTrue(TEXT("De HoldYaw a la espalda, la cabeza vuelve al frente sin pasarse"), bMonotonic);

	// El reparto de FrontBlend.
	TestEqual(TEXT("FrontBlend al frente"), FrontBlend(0.f), 1.f, Tol);
	TestEqual(TEXT("FrontBlend detrás"), FrontBlend(180.f), 0.f, Tol);
	TestEqual(TEXT("FrontBlend a mitad del camino"), FrontBlend((HoldYaw + FrontYaw) * 0.5f), 0.5f, Tol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNHeadLookQuantizeTest,
	"Tortunabo.HeadLook.Quantize",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNHeadLookQuantizeTest::RunTest(const FString& Parameters)
{
	using namespace TNHeadLook;

	// Ida y vuelta: error de medio paso como mucho (360/512 = 0,70°) en toda la vuelta.
	float MaxError = 0.f;
	for (float Yaw = -180.f; Yaw <= 180.f; Yaw += 0.37f)
	{
		const float Back = DecodeYaw(EncodeYaw(Yaw));
		MaxError = FMath::Max(MaxError, FMath::Abs(FRotator3f::NormalizeAxis(Back - Yaw)));
		TestTrue(TEXT("La vuelta queda en (-180, 180]"), Back > -180.f - 1e-3f && Back <= 180.f + 1e-3f);
	}
	TestTrue(FString::Printf(TEXT("Error máximo de ida y vuelta %.3f° <= 0,71°"), MaxError), MaxError <= 0.71f);

	// Valores exactos.
	TestEqual(TEXT("0° → 0"), static_cast<int32>(EncodeYaw(0.f)), 0);
	TestEqual(TEXT("90° → 64"), static_cast<int32>(EncodeYaw(90.f)), 64);
	TestEqual(TEXT("-90° → 192"), static_cast<int32>(EncodeYaw(-90.f)), 192);
	TestEqual(TEXT("180° y -180° → 128"), static_cast<int32>(EncodeYaw(-180.f)), 128);
	TestEqual(TEXT("128 → 180° (detrás: la cabeza al frente)"), Target(DecodeYaw(128), 0.f).Yaw, 0.f, 1e-3f);
	TestEqual(TEXT("-45° de ida y vuelta"), DecodeYaw(EncodeYaw(-45.f)), -45.f, 1e-3f);

	// Lo replicado da casi la misma cabeza que la vista de verdad (dentro de los topes).
	for (float Yaw = -70.f; Yaw <= 70.f; Yaw += 5.f)
	{
		TestTrue(FString::Printf(TEXT("Cabeza con la vista replicada (%.0f°)"), Yaw),
			FMath::Abs(Target(DecodeYaw(EncodeYaw(Yaw)), 0.f).Yaw - Target(Yaw, 0.f).Yaw) <= 0.71f);
	}

	// Solo se vuelve a mandar al moverse SendSteps pasos o más, también al cruzar la espalda (127 ↔ 129).
	TestFalse(TEXT("Mismo valor: no se manda"), ShouldSend(10, 10));
	TestFalse(TEXT("Un paso: no se manda"), ShouldSend(10, 11));
	TestTrue(TEXT("Dos pasos: se manda"), ShouldSend(10, 12));
	TestTrue(TEXT("Dos pasos hacia atrás: se manda"), ShouldSend(10, 8));
	TestFalse(TEXT("Un paso cruzando el 0 (255 → 0): no se manda"), ShouldSend(255, 0));
	TestTrue(TEXT("Dos pasos cruzando el 0 (255 → 1): se manda"), ShouldSend(255, 1));
	TestTrue(TEXT("Dos pasos cruzando la espalda (127 → 129): se manda"), ShouldSend(127, 129));
	TestTrue(TEXT("Media vuelta: se manda"), ShouldSend(0, 128));
	return true;
}

#endif
