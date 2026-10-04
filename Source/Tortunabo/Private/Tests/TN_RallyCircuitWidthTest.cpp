// Ancho de la calzada por tramos de los circuitos generados (#622, «Tramos variados»): lectura de road_widths_m, interpolación
// por el arco, filas de cajas que caben en un tramo estrecho y barrera pegada al borde de cada tramo. Lógica pura. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Circuit.RoadWidth; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Rally/TN_RallyCircuit.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyTrackDressing.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyCircuitWidthTest
{
	/** Manifest mínimo con road_uu de Points puntos (cada metro por +Y) y WidthsField tal cual (vacío: sin road_widths_m). */
	FString Manifest(int32 Points, const FString& WidthsField)
	{
		FString Json = TEXT("{\"start_uu\":[0,0,0],\"end_uu\":[0,0,0],\"closed\":true,\"road_width_m\":20,")
			TEXT("\"checkpoints_uu\":[[0,0,0,90],[0,1000,0,90]],");
		if (!WidthsField.IsEmpty())
		{
			Json += FString::Printf(TEXT("\"road_widths_m\":%s,"), *WidthsField);
		}
		Json += TEXT("\"road_uu\":[");
		for (int32 Index = 0; Index < Points; ++Index)
		{
			Json += FString::Printf(TEXT("%s[0,%d,0]"), Index > 0 ? TEXT(",") : TEXT(""), Index * 100);
		}
		return Json + TEXT("]}");
	}

	/** Recta por +X de Count muestras cada StepCm, con el ancho de cada muestra de WidthOf(Index). */
	template <typename FWidthOf>
	TNRallyDressing::FTrackData Straight(int32 Count, double StepCm, double TrackWidthCm, FWidthOf WidthOf)
	{
		TNRallyDressing::FTrackData Track;
		Track.StepCm = StepCm;
		Track.LengthCm = (Count - 1) * StepCm;
		Track.RoadWidthCm = TrackWidthCm;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			TNRallyDressing::FAxisSample& Sample = Track.Samples.AddDefaulted_GetRef();
			Sample.Arc = Index * StepCm;
			Sample.Location = FVector(Sample.Arc, 0.0, 0.0);
			Sample.RoadWidthCm = WidthOf(Index);
		}
		return Track;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRoadWidthParseTest, "Tortunabo.Rally.Circuit.RoadWidth.Parse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyRoadWidthParseTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCircuitWidthTest;
	TNRally::FTrackSource Source;
	FString Error;
	TestTrue(TEXT("Se lee con road_widths_m"), TNRally::ParseTrackManifest(Manifest(3, TEXT("[10,14.5,20]")), Source, Error));
	TestEqual(TEXT("Un ancho por punto de road_uu"), Source.RoadWidthsCm.Num(), 3);
	if (Source.RoadWidthsCm.Num() == 3)
	{
		TestEqual(TEXT("Tramo estrecho en cm"), Source.RoadWidthsCm[0], 1000.0, 1e-6);
		TestEqual(TEXT("Tramo normal en cm"), Source.RoadWidthsCm[1], 1450.0, 1e-6);
		TestEqual(TEXT("Tramo ancho en cm"), Source.RoadWidthsCm[2], 2000.0, 1e-6);
	}
	TestEqual(TEXT("road_width_m sigue siendo el máximo"), Source.RoadWidthCm, 2000.0, 1e-6);

	TestTrue(TEXT("Sin road_widths_m también se lee"), TNRally::ParseTrackManifest(Manifest(3, FString()), Source, Error));
	TestEqual(TEXT("Sin road_widths_m, sin ancho por punto"), Source.RoadWidthsCm.Num(), 0);

	AddExpectedError(TEXT("road_widths_m trae 2 valores"), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("Otro largo que road_uu no rompe la lectura"), TNRally::ParseTrackManifest(Manifest(3, TEXT("[10,20]")), Source, Error));
	TestEqual(TEXT("Otro largo que road_uu se descarta"), Source.RoadWidthsCm.Num(), 0);

	TestFalse(TEXT("Un ancho que no es un número es un manifest no válido"),
		TNRally::ParseTrackManifest(Manifest(3, TEXT("[10,\"ancho\",20]")), Source, Error));
	TestTrue(TEXT("El error lo dice"), Error.Contains(TEXT("road_widths_m")));

	TestTrue(TEXT("Un ancho absurdo se recorta"), TNRally::ParseTrackManifest(Manifest(2, TEXT("[0.5,90]")), Source, Error));
	if (Source.RoadWidthsCm.Num() == 2)
	{
		TestEqual(TEXT("Recortado al mínimo"), Source.RoadWidthsCm[0], TNRallyCircuit::MinRoadWidthCm, 1e-6);
		TestEqual(TEXT("Recortado al máximo"), Source.RoadWidthsCm[1], TNRallyCircuit::MaxRoadWidthCm, 1e-6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRoadWidthArcTest, "Tortunabo.Rally.Circuit.RoadWidth.AtArcAndItemRows",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyRoadWidthArcTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCircuit;
	const TArray<double> Arcs = { 0.0, 1000.0, 2000.0 };
	const TArray<double> Widths = { 1000.0, 2000.0, 1400.0 };
	TestEqual(TEXT("En un punto, su ancho"), RoadWidthAtArc(Arcs, Widths, 1000.0, 3000.0, true, 0.0), 2000.0, 1e-6);
	TestEqual(TEXT("Entre dos puntos, interpolado"), RoadWidthAtArc(Arcs, Widths, 500.0, 3000.0, true, 0.0), 1500.0, 1e-6);
	TestEqual(TEXT("En circuito, del último al primero"), RoadWidthAtArc(Arcs, Widths, 2500.0, 3000.0, true, 0.0), 1200.0, 1e-6);
	TestEqual(TEXT("Sin muestras, el de road_width_m"), RoadWidthAtArc({}, {}, 500.0, 3000.0, true, 1400.0), 1400.0, 1e-6);

	TestEqual(TEXT("Calzada ancha: la separación preferida"), RowLateralSpacingCm(400.0, 2000.0, 4), 400.0, 1e-6);
	const double Narrow = RowLateralSpacingCm(400.0, 1000.0, 4);
	TestTrue(TEXT("Calzada estrecha: la fila se aprieta"), Narrow < 400.0);
	TestTrue(TEXT("Calzada estrecha: las cajas de los extremos dentro de la calzada con su margen"), 1.5 * Narrow <= 500.0 - 150.0 + 1e-6);
	TestEqual(TEXT("Sin ancho, la preferida"), RowLateralSpacingCm(400.0, 0.0, 4), 400.0, 1e-6);
	TestEqual(TEXT("Una sola caja, la preferida"), RowLateralSpacingCm(400.0, 1000.0, 1), 400.0, 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRoadWidthBarrierTest, "Tortunabo.Rally.Circuit.RoadWidth.BarrierFollowsSections",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyRoadWidthBarrierTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressing;
	const FBarrierParams Params;
	// 400 m de recta: la primera mitad de 10 m, transición de 24 m y la segunda de 20 m (como R01: rasante y luego horquilla).
	const FTrackData Track = TNRallyCircuitWidthTest::Straight(101, 400.0, 2000.0, [](int32 Index)
	{
		const double Arc = Index * 400.0;
		return FMath::GetMappedRangeValueClamped(FVector2D(18800.0, 21200.0), FVector2D(1000.0, 2000.0), Arc);
	});
	const FBarrierPlan Plan = PlanBarriers(Track, TArray<uint8>(), Params);
	const double Margin = Params.RoadEdgeMarginCm;
	for (int32 Side = LeftSide; Side <= RightSide; ++Side)
	{
		TestEqual(*FString::Printf(TEXT("Lado %d: en el tramo estrecho, a media calzada (5 m) más el margen"), Side),
			Plan.Sides[Side].OffsetCm[20], 500.0 + Margin, 1.0);
		TestEqual(*FString::Printf(TEXT("Lado %d: en el tramo ancho, a media calzada (10 m) más el margen"), Side),
			Plan.Sides[Side].OffsetCm[80], 1000.0 + Margin, 1.0);
		bool bOffRoad = true;
		for (int32 Index = 0; Index < Track.Samples.Num(); ++Index)
		{
			bOffRoad &= Plan.Sides[Side].OffsetCm[Index] >= 0.5 * Track.Samples[Index].RoadWidthCm + Margin - 1.0;
		}
		TestTrue(*FString::Printf(TEXT("Lado %d: la barrera nunca pisa la calzada, tampoco en la transición"), Side), bOffRoad);
	}

	// Sin ancho por muestra, como antes: todo el trazado con road_width_m.
	const FTrackData Uniform = TNRallyCircuitWidthTest::Straight(101, 400.0, 1400.0, [](int32) { return 0.0; });
	const FBarrierPlan UniformPlan = PlanBarriers(Uniform, TArray<uint8>(), Params);
	TestEqual(TEXT("Sin ancho por tramos, la barrera a 7 m más el margen"), UniformPlan.Sides[RightSide].OffsetCm[50], 700.0 + Margin, 1.0);
	TestEqual(TEXT("SampleRoadHalfCm sin ancho por muestra: el del trazado"), SampleRoadHalfCm(Uniform, 10, Params), 700.0, 1e-6);
	TestEqual(TEXT("SampleRoadHalfCm con ancho por muestra: el suyo"), SampleRoadHalfCm(Track, 10, Params), 500.0, 1e-6);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
