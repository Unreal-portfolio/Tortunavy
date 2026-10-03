// Interfaz de la conductora y pantallita del Rally (#299): el salpicadero y el cartel del arco se leen a 1080p con la cámara
// de persecución (proyección de las cifras con el brazo y el FOV de TNBuggy::DefaultDriverCamera), y las filas de cajas de
// munición salen en el perfil de los próximos 400 m. Correr con "Automation RunTests Tortunabo.Rally.Dashboard".

#include "Misc/AutomationTest.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyPaceNotes.h"
#include "Rally/UI/TN_RallyDashboard.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyMath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyDashboardTest
{
	const FIntPoint Screen1080p(1920, 1080);
	/** Cifras legibles sin esfuerzo con la cámara parada y aún legibles en el peor caso (brazo largo, FOV del turbo). */
	constexpr float MinGlyphPxAtRest = 24.f;
	constexpr float MinGlyphPxWorst = 16.f;

	/** Distancia de la cámara al panel: el brazo más lo que el panel queda por delante del pivote (eje X del buggy). */
	float CameraDistanceCm(const TNRallyDashboard::FPanelLayout& Layout, float ArmCm)
	{
		return ArmCm + static_cast<float>(ATN_Buggy::DriverSeatLocal.X + Layout.OffsetFromDriverSeat.X);
	}

	void CheckLegible(FAutomationTestBase& Test, const TCHAR* Name, const TNRallyDashboard::FPanelLayout& Layout)
	{
		const TNBuggy::FDriverCameraTuning& Camera = TNBuggy::DefaultDriverCamera();
		const float Glyph = TNRallyDashboard::MainGlyphCm(Layout);
		const float AtRest = TNRallyDashboard::ProjectedGlyphPx(Glyph, CameraDistanceCm(Layout, Camera.BaseArmCm), Camera.BaseFov, Screen1080p);
		const float Worst = TNRallyDashboard::ProjectedGlyphPx(Glyph, CameraDistanceCm(Layout, Camera.MaxArmCm),
			Camera.MaxFov + Camera.BoostFovDeg, Screen1080p);
		Test.TestTrue(FString::Printf(TEXT("%s: cifras de %.1f px parado (mín. %.0f)"), Name, AtRest, MinGlyphPxAtRest), AtRest >= MinGlyphPxAtRest);
		Test.TestTrue(FString::Printf(TEXT("%s: cifras de %.1f px a toda velocidad con turbo (mín. %.0f)"), Name, Worst, MinGlyphPxWorst),
			Worst >= MinGlyphPxWorst);
		Test.TestTrue(FString::Printf(TEXT("%s: mira hacia atrás (a la cámara)"), Name), FMath::IsNearlyEqual(FMath::Abs(Layout.Rotation.Yaw), 180.0));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDashboardProjectionTest, "Tortunabo.Rally.Dashboard.Projection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyDashboardProjectionTest::RunTest(const FString& Parameters)
{
	// 90° de FOV horizontal a 1920 px: a 10 m, 1 cm del mundo son 0,96 px.
	TestTrue(TEXT("A 10 m con 90°: 0,96 px/cm"), FMath::IsNearlyEqual(TNRallyDashboard::ProjectedGlyphPx(100.f, 1000.f, 90.f,
		TNRallyDashboardTest::Screen1080p), 96.f, 0.1f));
	TestTrue(TEXT("Más lejos, más pequeño"), TNRallyDashboard::ProjectedGlyphPx(10.f, 2000.f, 90.f, TNRallyDashboardTest::Screen1080p)
		< TNRallyDashboard::ProjectedGlyphPx(10.f, 1000.f, 90.f, TNRallyDashboardTest::Screen1080p));
	TestEqual(TEXT("Distancia nula: 0"), TNRallyDashboard::ProjectedGlyphPx(10.f, 0.f, 90.f, TNRallyDashboardTest::Screen1080p), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDashboardLegibleTest, "Tortunabo.Rally.Dashboard.LegibleAt1080p",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyDashboardLegibleTest::RunTest(const FString& Parameters)
{
	TNRallyDashboardTest::CheckLegible(*this, TEXT("Salpicadero (velocidad)"), TNRallyDashboard::DashLayout());
	TNRallyDashboardTest::CheckLegible(*this, TEXT("Cartel del arco (puesto)"), TNRallyDashboard::RollBarLayout());
	TNRallyDashboardTest::CheckLegible(*this, TEXT("Placa de notas (lado y grado)"), TNRallyDashboard::CallLayout());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDashboardAmmoRowsTest, "Tortunabo.Rally.Dashboard.AmmoRowsAhead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTNRallyDashboardAmmoRowsTest::RunTest(const FString& Parameters)
{
	// Punto a punto de 6 puertas cada 500 m (2500 m): filas tras la puerta 2 y la 4 y a mitad de los tramos 1→2 y 3→4; la
	// del tramo 5 no (es la meta) y ninguna en los últimos 355 m.
	const TArray<double> Gates = { 0.0, 50000.0, 100000.0, 150000.0, 200000.0, 250000.0 };
	const TArray<double> Rows = TNRally::AmmoRowArcs(Gates, 250000.0, false, 1500.0, 35500.0);
	TestEqual(TEXT("Cuatro filas en el punto a punto"), Rows.Num(), 4);
	if (Rows.Num() == 4)
	{
		TestEqual(TEXT("Mitad del tramo 1→2"), Rows[0], 75000.0);
		TestEqual(TEXT("Tras la puerta 2"), Rows[1], 101500.0);
		TestEqual(TEXT("Mitad del tramo 3→4"), Rows[2], 175000.0);
		TestEqual(TEXT("Tras la puerta 4"), Rows[3], 201500.0);
	}
	// En circuito sí hay fila a mitad del último tramo (de vuelta a la salida).
	const TArray<double> Loop = TNRally::AmmoRowArcs(Gates, 300000.0, true, 1500.0, 35500.0);
	TestTrue(TEXT("Circuito: fila a mitad del tramo 5→0"), Loop.Contains(275000.0));

	// La tableta marca las de los próximos 400 m, en orden, también al dar la vuelta.
	const TArray<double> Ahead = TNRallyPaceNotes::ArcsAhead(Loop, 300000.0, true, 290000.0, 40000.0);
	TestEqual(TEXT("Dando la vuelta: ninguna entre 2900 y 3300 m"), Ahead.Num(), 0);
	const TArray<double> Near = TNRallyPaceNotes::ArcsAhead(Loop, 300000.0, true, 60000.0, 40000.0);
	// Desde 600 m: la de 750 m entra (a 150 m); la de 1015 m queda a 415 m, fuera.
	if (TestEqual(TEXT("Desde 600 m: una fila en 400 m"), Near.Num(), 1))
	{
		TestEqual(TEXT("A 150 m"), Near[0], 15000.0);
	}
	const TArray<double> Wrap = TNRallyPaceNotes::ArcsAhead({ 1000.0 }, 300000.0, true, 299000.0, 40000.0);
	TestTrue(TEXT("Circuito: la fila de 10 m tras la salida está a 20 m"), Wrap.Num() == 1 && FMath::IsNearlyEqual(Wrap[0], 2000.0));
	TestEqual(TEXT("Punto a punto: nada detrás"), TNRallyPaceNotes::ArcsAhead({ 1000.0 }, 300000.0, false, 2000.0, 40000.0).Num(), 0);
	return true;
}

#endif
