// Presentación de la pantallita del Rally (#334): en la pantalla se encaja como antes de poder alojarla en el mundo (el
// juego plano no cambia) y en un UWidgetComponent ocupa el panel entero, centrada y a escala. Correr con
// "Automation RunTests Tortunabo.Rally.Tablet".

#include "Misc/AutomationTest.h"
#include "Rally/UI/TN_RallyCopilotTablet.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyTabletTest
{
	using namespace TNRallyTabletLayout;

	const FVector2f Screen1080p(1920.f, 1080.f);

	bool Near(const FVector2f& A, const FVector2f& B)
	{
		return FMath::IsNearlyEqual(A.X, B.X, 0.01f) && FMath::IsNearlyEqual(A.Y, B.Y, 0.01f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTabletScreenTest, "Tortunabo.Rally.Tablet.ScreenUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyTabletScreenTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTabletTest;
	// Grande a 1080p: el alto manda (0,66 × 1080 / 720 = 0,99), centrada y a 24 px del borde inferior.
	const FPlacement Full = Place(Screen1080p, FitFor(ETNRallyTabletPresentation::Screen, ETNRallyTabletView::Full));
	TestTrue(TEXT("Grande: escala 0,99"), FMath::IsNearlyEqual(Full.Scale, 0.99f, 0.001f));
	TestTrue(TEXT("Grande: centrada abajo"), Near(Full.Origin, FVector2f((1920.f - 1280.f * 0.99f) * 0.5f, 1080.f - 24.f - 720.f * 0.99f)));
	// Compacta: el alto manda (0,5 × 1080 / 560 = 0,964), arriba a la derecha a 32 px de los bordes.
	const FFit CompactFit = FitFor(ETNRallyTabletPresentation::Screen, ETNRallyTabletView::Compact);
	const FPlacement Compact = Place(Screen1080p, CompactFit);
	TestTrue(TEXT("Compacta: escala 0,964"), FMath::IsNearlyEqual(Compact.Scale, 0.5f * 1080.f / 560.f, 0.001f));
	TestTrue(TEXT("Compacta: arriba a la derecha"), Near(Compact.Origin, FVector2f(1920.f - 32.f - 360.f * Compact.Scale, 32.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTabletWorldTest, "Tortunabo.Rally.Tablet.WorldPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyTabletWorldTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTabletTest;
	// Panel del tamaño de la maqueta: la tableta se pinta 1:1 y sin margen, igual que se diseñó.
	for (const ETNRallyTabletView View : { ETNRallyTabletView::Full, ETNRallyTabletView::Compact })
	{
		const FVector2f Design = DesignSize(View);
		const FPlacement Placement = Place(Design, FitFor(ETNRallyTabletPresentation::World, View));
		TestTrue(FString::Printf(TEXT("Vista %d: escala 1 en un panel de su tamaño"), static_cast<int32>(View)),
			FMath::IsNearlyEqual(Placement.Scale, 1.f, 0.001f));
		TestTrue(FString::Printf(TEXT("Vista %d: sin desplazamiento"), static_cast<int32>(View)), Near(Placement.Origin, FVector2f::ZeroVector));
	}
	// En un panel con otra proporción se encaja entera y centrada.
	const FPlacement Narrow = Place(FVector2f(640.f, 720.f), FitFor(ETNRallyTabletPresentation::World, ETNRallyTabletView::Full));
	TestTrue(TEXT("Panel estrecho: escala 0,5"), FMath::IsNearlyEqual(Narrow.Scale, 0.5f, 0.001f));
	TestTrue(TEXT("Panel estrecho: centrada en vertical"), Near(Narrow.Origin, FVector2f(0.f, 180.f)));
	return true;
}

#endif
