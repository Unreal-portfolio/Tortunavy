// Artillera de los karts (#295): inclinación de la artillera y vuelta de la cámara al centro. Lógica pura. Las cajas «?» y
// las conchas están en TN_RallyItemBoxTest.cpp. Correr desde Session Frontend (categoría
// "Tortunabo.Kart") o headless con UnrealEditor-Win64-DebugGame-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Kart; Quit".

#include "Misc/AutomationTest.h"
#include "Kart/TN_KartBuggy.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNKartGunnerLeanTest, "Tortunabo.Kart.Gunner.LeanAndLook",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNKartGunnerLeanTest::RunTest(const FString& Parameters)
{
	using namespace TNKart;
	TestEqual(TEXT("Sin inclinarse, el giro de siempre"), LeanSteerMultiplier(0.f, 1.f), 1.f);
	TestEqual(TEXT("Sin girar, la inclinación no hace nada"), LeanSteerMultiplier(1.f, 0.f), 1.f);
	TestEqual(TEXT("Inclinada hacia dentro de la curva: gira más"), LeanSteerMultiplier(1.f, 1.f), 1.f + LeanSteerGain, 0.001f);
	TestEqual(TEXT("Inclinada hacia fuera: gira menos"), LeanSteerMultiplier(-1.f, 1.f), 1.f - LeanSteerGain, 0.001f);
	TestEqual(TEXT("A la izquierda, igual que a la derecha"), LeanSteerMultiplier(-1.f, -1.f), 1.f + LeanSteerGain, 0.001f);
	TestTrue(TEXT("Media inclinación, medio efecto"), FMath::IsNearlyEqual(LeanSteerMultiplier(0.5f, 1.f), 1.f + 0.5f * LeanSteerGain, 0.001f));

	TestEqual(TEXT("La cámara vuelve al centro poco a poco"), RecenterLook(90.f, 120.f, 0.25f), 60.f, 0.001f);
	TestEqual(TEXT("Sin pasarse del centro"), RecenterLook(10.f, 120.f, 0.25f), 0.f);
	TestEqual(TEXT("Desde el otro lado, igual"), RecenterLook(-90.f, 120.f, 0.25f), -60.f, 0.001f);
	return true;
}

#endif
