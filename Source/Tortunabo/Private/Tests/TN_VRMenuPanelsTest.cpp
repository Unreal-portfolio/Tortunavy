// Menús que rodean al jugador (#916, VR/TN_VRMath.h): el menú se repite alrededor de la cabeza y el láser toca el panel que
// mire el jugador. Lógica pura, sin gafas.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.VR; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "VR/TN_VRMath.h"
#include "VR/TN_VRScreenWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVRMenuPanelsTest,
	"Tortunabo.VR.MenuPanels",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVRMenuPanelsTest::RunTest(const FString& Parameters)
{
	const FVector2D Size(UTN_VRScreenWidget::ScreenWidth, UTN_VRScreenWidget::ScreenHeight);

	// Reparto: el de delante a 0 y las copias a partes iguales; con uno solo, como antes.
	TestEqual(TEXT("Un solo panel: sin giro"), TNVRMath::MenuPanelYaw(0, 1), 0.f);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		TestTrue(FString::Printf(TEXT("Tres paneles: el %d a 120° por paso"), Index), FMath::IsNearlyEqual(TNVRMath::MenuPanelYaw(Index, 3), 120.f * Index, 1e-3f));
	}
	TestTrue(TEXT("Cuatro paneles: el tercero a 270°"), FMath::IsNearlyEqual(TNVRMath::MenuPanelYaw(3, 4), 270.f, 1e-3f));

	// Arco: el pedido si cabe en su hueco; si no, el hueco menos el margen (los paneles no se pisan).
	TestEqual(TEXT("Un panel: el arco pedido"), TNVRMath::MenuPanelArc(100.f, 1), 100.f);
	TestEqual(TEXT("Tres paneles: caben 100° en 120°"), TNVRMath::MenuPanelArc(100.f, 3), 100.f);
	TestTrue(TEXT("Cuatro paneles: 100° no caben en 90°"), FMath::IsNearlyEqual(TNVRMath::MenuPanelArc(100.f, 4), 82.f, 1e-3f));
	TestTrue(TEXT("Seis paneles: 52°"), FMath::IsNearlyEqual(TNVRMath::MenuPanelArc(100.f, 6), 52.f, 1e-3f));

	// Colocación: delante a 160 cm de los ojos (PlacePanel) y las copias giradas alrededor de ellos.
	constexpr float Arc = 100.f;
	constexpr float Distance = 160.f;
	const float Scale = TNVRMath::CurvedPanelScale(Distance, Arc, UTN_VRScreenWidget::ScreenWidth);
	const FVector Eyes(300.0, -120.0, 90.0);
	const FTransform Front(FRotator(0.0, 180.0, 0.0), Eyes + FVector(Distance, 0.0, 0.0), FVector(Scale));
	FVector Hit;
	FVector2D UV;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float Yaw = TNVRMath::MenuPanelYaw(Index, 3);
		const FTransform Panel = TNVRMath::MenuPanelTransform(Front, Eyes, Yaw);
		const FVector Direction = FRotator(0.0, Yaw, 0.0).Vector();
		TestTrue(FString::Printf(TEXT("El panel %d sigue a %.0f cm de los ojos"), Index, Distance),
			FMath::IsNearlyEqual(FVector::Dist(Eyes, Panel.GetLocation()), static_cast<double>(Distance), 1e-3));
		TestTrue(FString::Printf(TEXT("El panel %d mira a los ojos"), Index),
			FVector::DotProduct(Panel.GetRotation().GetForwardVector(), (Eyes - Panel.GetLocation()).GetSafeNormal()) > 0.9999);
		TestTrue(FString::Printf(TEXT("El panel %d conserva la escala"), Index), Panel.GetScale3D().Equals(Front.GetScale3D(), 1e-6));
		TestTrue(FString::Printf(TEXT("Rayo hacia el panel %d: toca su centro"), Index),
			TNVRMath::RayCurvedPanelHit(Eyes, Direction, Panel, Size, Arc, Hit, UV) && UV.Equals(FVector2D(0.5, 0.5), 1e-4));
		TestTrue(FString::Printf(TEXT("Rayo hacia el panel %d: a %.0f cm"), Index, Distance), FVector::Dist(Eyes, Hit) > Distance - 0.01 && FVector::Dist(Eyes, Hit) < Distance + 0.01);
	}

	// Entre dos paneles (a 60°) no hay nada; de espaldas (a 180°) tampoco con tres paneles: el hueco está ahí.
	bool bAnyHit = false;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FTransform Panel = TNVRMath::MenuPanelTransform(Front, Eyes, TNVRMath::MenuPanelYaw(Index, 3));
		bAnyHit |= TNVRMath::RayCurvedPanelHit(Eyes, FRotator(0.0, 60.0, 0.0).Vector(), Panel, Size, Arc, Hit, UV);
	}
	TestFalse(TEXT("En el hueco entre dos paneles el rayo no toca ninguno"), bAnyHit);

	// Con cuatro paneles cada lado del jugador tiene el suyo: mire a donde mire, un panel a menos de la mitad del reparto.
	for (const float Look : { 10.f, 100.f, 190.f, 280.f, 335.f })
	{
		float Nearest = 360.f;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Nearest = FMath::Min(Nearest, FMath::Abs(static_cast<float>(FRotator::NormalizeAxis(Look - TNVRMath::MenuPanelYaw(Index, 4)))));
		}
		TestTrue(FString::Printf(TEXT("Cuatro paneles: mirando a %.0f° hay uno a menos de 45°"), Look), Nearest <= 45.f + 1e-3f);
	}
	return true;
}

#endif
