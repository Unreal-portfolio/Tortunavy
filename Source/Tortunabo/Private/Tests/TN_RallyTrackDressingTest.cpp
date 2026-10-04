// Reglas puras del decorado del trazado del Rally (Rally/TN_RallyTrackDressing.h): curvatura, dónde van los límites y a
// qué distancia del eje, atajos, horquillas, reparto a lo largo de una polilínea, decorado fuera del corredor y público.
// Correr desde Session Frontend (categoría "Tortunabo.Rally.Dressing") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Dressing; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Rally/TN_RallyTrackDressing.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyDressingTestHelpers
{
	using namespace TNRallyDressing;

	/** Recta por +X desde el origen, punto a punto. */
	static FTrackData Straight(double LengthCm, double StepCm = 400.0)
	{
		FTrackData Track;
		Track.StepCm = StepCm;
		Track.LengthCm = LengthCm;
		const int32 Count = FMath::RoundToInt32(LengthCm / StepCm) + 1;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FAxisSample& Sample = Track.Samples.AddDefaulted_GetRef();
			Sample.Arc = Index * StepCm;
			Sample.Location = FVector(Sample.Arc, 0.0, 0.0);
			Sample.Direction = FVector::ForwardVector;
		}
		return Track;
	}

	/** Circuito circular de radio RadiusCm que sale del origen hacia +X y gira a la derecha (+Y) o a la izquierda. */
	static FTrackData Circle(double RadiusCm, bool bRightTurn, double StepCm = 400.0)
	{
		FTrackData Track;
		Track.bClosed = true;
		Track.LengthCm = 2.0 * UE_DOUBLE_PI * RadiusCm;
		const int32 Count = FMath::RoundToInt32(Track.LengthCm / StepCm);
		Track.StepCm = Track.LengthCm / Count;
		const double Turn = bRightTurn ? 1.0 : -1.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FAxisSample& Sample = Track.Samples.AddDefaulted_GetRef();
			Sample.Arc = Index * Track.StepCm;
			const double Angle = Sample.Arc / RadiusCm;
			Sample.Location = FVector(RadiusCm * FMath::Sin(Angle), Turn * RadiusCm * (1.0 - FMath::Cos(Angle)), 0.0);
			Sample.Direction = FVector(FMath::Cos(Angle), Turn * FMath::Sin(Angle), 0.0);
		}
		return Track;
	}

	/** Horquilla: recta por +X, media vuelta a la derecha y vuelta por -X a SeparationCm del primer tramo. */
	static FTrackData Hairpin(double SeparationCm, double StraightCm, double StepCm = 400.0)
	{
		FTrackData Track;
		const double Radius = 0.5 * SeparationCm;
		Track.LengthCm = 2.0 * StraightCm + UE_DOUBLE_PI * Radius;
		Track.StepCm = StepCm;
		for (double Arc = 0.0; Arc <= Track.LengthCm; Arc += StepCm)
		{
			FAxisSample& Sample = Track.Samples.AddDefaulted_GetRef();
			Sample.Arc = Arc;
			if (Arc <= StraightCm)
			{
				Sample.Location = FVector(Arc, 0.0, 0.0);
				Sample.Direction = FVector::ForwardVector;
				continue;
			}
			const double Angle = FMath::Min(UE_DOUBLE_PI, (Arc - StraightCm) / Radius);
			const double Back = FMath::Max(0.0, Arc - StraightCm - UE_DOUBLE_PI * Radius);
			Sample.Location = Back > 0.0 ? FVector(StraightCm - Back, SeparationCm, 0.0)
				: FVector(StraightCm + Radius * FMath::Sin(Angle), Radius * (1.0 - FMath::Cos(Angle)), 0.0);
			Sample.Direction = Back > 0.0 ? -FVector::ForwardVector : FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
		}
		return Track;
	}

	static TArray<uint8> DropsOn(int32 Num, int32 Side, int32 First, int32 Last)
	{
		TArray<uint8> Mask;
		Mask.Init(0, Num);
		for (int32 Index = FMath::Max(0, First); Index <= Last && Index < Num; ++Index)
		{
			Mask[Index] |= static_cast<uint8>(1u << Side);
		}
		return Mask;
	}

	static int32 SampleNearX(const FTrackData& Track, double X, double Y)
	{
		int32 Best = 0;
		for (int32 Index = 0; Index < Track.Samples.Num(); ++Index)
		{
			if (FVector::Dist2D(Track.Samples[Index].Location, FVector(X, Y, 0.0)) < FVector::Dist2D(Track.Samples[Best].Location, FVector(X, Y, 0.0)))
			{
				Best = Index;
			}
		}
		return Best;
	}

	static TArray<FTNRallyDecorEntry> Entries()
	{
		TArray<FTNRallyDecorEntry> Result;
		for (const ETNBeachElement Element : { ETNBeachElement::PlantedUmbrella, ETNBeachElement::BeachTowel, ETNBeachElement::SandCastleSmall })
		{
			FTNRallyDecorEntry& Entry = Result.AddDefaulted_GetRef();
			Entry.Element = Element;
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDressingCurvatureTest, "Tortunabo.Rally.Dressing.Curvature",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDressingCurvatureTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressingTestHelpers;
	const TArray<double> Right = SignedCurvature(Circle(10000.0, true).Samples, true, 2000.0);
	const TArray<double> Left = SignedCurvature(Circle(10000.0, false).Samples, true, 2000.0);
	const TArray<double> Flat = SignedCurvature(Straight(20000.0).Samples, false, 2000.0);
	TestTrue(TEXT("Curva a la derecha: curvatura positiva de 1/R"), FMath::IsNearlyEqual(Right[10], 1.0 / 10000.0, 1e-6));
	TestTrue(TEXT("Curva a la izquierda: curvatura negativa de 1/R"), FMath::IsNearlyEqual(Left[10], -1.0 / 10000.0, 1e-6));
	TestTrue(TEXT("Recta: curvatura nula"), FMath::IsNearlyZero(Flat[20], 1e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDressingStraightTest, "Tortunabo.Rally.Dressing.StraightFlatHasNoLimits",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDressingStraightTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressingTestHelpers;
	// Reglas de antes de #303 (límite solo en curvas y caídas, con arcén), que siguen valiendo con bContinuous y bHugRoad a false.
	FBarrierParams Sparse;
	Sparse.bContinuous = false;
	Sparse.bHugRoad = false;
	const FTrackData Track = Straight(50000.0);
	const FBarrierPlan Plan = PlanBarriers(Track, TArray<uint8>(), Sparse);
	TestEqual(TEXT("Recta llana: sin límite a la izquierda"), Plan.Sides[LeftSide].Runs.Num(), 0);
	TestEqual(TEXT("Recta llana: sin límite a la derecha"), Plan.Sides[RightSide].Runs.Num(), 0);
	TestEqual(TEXT("Base: 14 m de calzada + arcén, al menos fuera de los postes (15 m)"), Plan.BaseOffsetCm, 1500.0);
	TestEqual(TEXT("Sin límite, el corredor llega a la base"), Plan.EdgeCm(RightSide, 10), 1500.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDressingContinuousTest, "Tortunabo.Rally.Dressing.ContinuousBarrierNoGaps",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDressingContinuousTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressingTestHelpers;
	// #303 (director, 03-10): barrera continua a los dos lados, sin huecos más anchos que una tortuga.
	const FBarrierParams Params;
	TestTrue(TEXT("La barrera continua es la de por defecto"), Params.bContinuous);
	const FTrackData Track = Straight(50000.0);
	const FBarrierPlan Plan = PlanBarriers(Track, TArray<uint8>(), Params);
	for (int32 Side = LeftSide; Side <= RightSide; ++Side)
	{
		TestEqual(TEXT("Recta: un solo tramo de punta a punta"), Plan.Sides[Side].Runs.Num(), 1);
		TestEqual(TEXT("...que cubre todas las muestras"), Plan.Sides[Side].Runs[0].Num(), Track.Samples.Num());
		TestEqual(TEXT("...pegada al borde de la calzada (7 m + el margen)"), Plan.Sides[Side].OffsetCm[60], 700.0 + Params.RoadEdgeMarginCm);
		TestEqual(TEXT("Sin huecos"), BarrierGapsCm(Track, Plan, Side).Num(), 0);
	}
	TestEqual(TEXT("Circuito cerrado entero: sin huecos"), BarrierGapsCm(Circle(8000.0, true), PlanBarriers(Circle(8000.0, true), TArray<uint8>(), Params), LeftSide).Num(), 0);

	// La comprobación sí ve los huecos: con las reglas de antes, la recta llana queda abierta entera.
	FBarrierParams Sparse = Params;
	Sparse.bContinuous = false;
	const TArray<double> Open = BarrierGapsCm(Track, PlanBarriers(Track, TArray<uint8>(), Sparse), RightSide);
	TestTrue(TEXT("Sin barrera continua, el hueco es la recta entera"), Open.Num() == 1 && Open[0] > TurtleWidthCm);

	// Un atajo abre un hueco del largo del atajo (no cuenta como fallo de la barrera: se pide a propósito).
	FTrackData WithShortcut = Track;
	FTNRallyDressingGap Shortcut;
	Shortcut.StartArcCm = 14000.f;
	Shortcut.EndArcCm = 18000.f;
	Shortcut.Side = 1;
	WithShortcut.Gaps.Add(Shortcut);
	const TArray<double> ShortcutGaps = BarrierGapsCm(WithShortcut, PlanBarriers(WithShortcut, TArray<uint8>(), Params), RightSide);
	// Muestras cada 4 m: la última con límite antes del atajo está en 136 m y la primera después, en 184 m.
	TestTrue(TEXT("El atajo deja un hueco de 48 m"), ShortcutGaps.Num() == 1 && FMath::IsNearlyEqual(ShortcutGaps[0], 4800.0, 1.0));
	TestEqual(TEXT("...y solo en su lado"), BarrierGapsCm(WithShortcut, PlanBarriers(WithShortcut, TArray<uint8>(), Params), LeftSide).Num(), 0);

	// Los trozos de estilo comparten el punto de corte: la barrera no se abre al cambiar de vallas a neumáticos.
	TArray<FVector> Line;
	for (int32 Index = 0; Index <= 10; ++Index)
	{
		Line.Add(FVector(1000.0 * Index, 0.0, 0.0));
	}
	const TArray<TArray<FVector>> Chunks = ChunkPolyline(Line, 3000.0);
	TestEqual(TEXT("100 m en trozos de 30 m: 4"), Chunks.Num(), 4);
	bool bTouching = Chunks.Num() > 0 && Chunks[0][0].Equals(Line[0]) && Chunks.Last().Last().Equals(Line.Last());
	for (int32 Index = 1; Index < Chunks.Num(); ++Index)
	{
		bTouching &= Chunks[Index][0].Equals(Chunks[Index - 1].Last());
	}
	TestTrue(TEXT("Cada trozo empieza donde acaba el anterior y cubren toda la línea"), bTouching);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDressingCurveTest, "Tortunabo.Rally.Dressing.CurveLimitsBothSides",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDressingCurveTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressingTestHelpers;
	// Reglas de escapatoria de antes de #303 (bHugRoad = false); la barrera pegada a la calzada se prueba en RoadEdgeBarrier.
	FBarrierParams Params;
	Params.bHugRoad = false;
	const FTrackData Tight = Circle(2000.0, true);
	const FBarrierPlan Plan = PlanBarriers(Tight, TArray<uint8>(), Params);
	TestEqual(TEXT("Circuito en curva: un solo tramo por fuera que da la vuelta entera"), Plan.Sides[LeftSide].Runs.Num(), 1);
	TestEqual(TEXT("Circuito en curva: un solo tramo por dentro"), Plan.Sides[RightSide].Runs.Num(), 1);
	TestEqual(TEXT("El tramo cubre todas las muestras"), Plan.Sides[LeftSide].Runs[0].Num(), Tight.Samples.Num());
	TestTrue(TEXT("Curva cerrada: por fuera, base + toda la escapatoria"), FMath::IsNearlyEqual(Plan.Sides[LeftSide].OffsetCm[5], 2300.0, 1.0));
	TestTrue(TEXT("Curva cerrada: por dentro, acotado al 60 % del radio"), FMath::IsNearlyEqual(Plan.Sides[RightSide].OffsetCm[5], 1200.0, 1.0));

	const FBarrierPlan Wide = PlanBarriers(Circle(8000.0, true), TArray<uint8>(), Params);
	TestTrue(TEXT("Curva abierta: por fuera, más que la base y menos que la escapatoria entera"),
		Wide.Sides[LeftSide].OffsetCm[5] > 1500.0 && Wide.Sides[LeftSide].OffsetCm[5] < 2300.0);
	TestTrue(TEXT("Curva abierta: por dentro, la base"), FMath::IsNearlyEqual(Wide.Sides[RightSide].OffsetCm[5], 1500.0, 1.0));

	TestTrue(TEXT("Por dentro nunca se come la calzada"), BarrierOffsetCm(1.0 / 500.0, RightSide, 700.0, Params) >= 700.0 + Params.InsideMinMarginCm);
	TestTrue(TEXT("Curva a la izquierda: el exterior es la derecha"), BarrierOffsetCm(-1.0 / 2000.0, RightSide, 700.0, Params) > 2000.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDressingRoadEdgeTest, "Tortunabo.Rally.Dressing.RoadEdgeBarrier",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDressingRoadEdgeTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressingTestHelpers;
	// #303 (director, 03-10): la barrera dibuja el ancho de la calzada, en recta, en curva, por dentro, por fuera y en las caídas.
	const FBarrierParams Params;
	TestTrue(TEXT("Pegada al borde es la de por defecto"), Params.bHugRoad);
	for (const double Width : { 1000.0, 1400.0, 2000.0 })
	{
		const double Expected = 0.5 * Width + Params.RoadEdgeMarginCm;
		for (FTrackData Track : { Straight(30000.0), Circle(2000.0, true), Circle(8000.0, false) })
		{
			Track.RoadWidthCm = Width;
			const FBarrierPlan Plan = PlanBarriers(Track, DropsOn(Track.Samples.Num(), RightSide, 5, 15), Params);
			TestEqual(*FString::Printf(TEXT("Calzada de %.0f m: el borde del corredor es la barrera"), Width / 100.0), Plan.BaseOffsetCm, Expected);
			double Farthest = 0.0;
			double Nearest = TNumericLimits<double>::Max();
			for (int32 Side = LeftSide; Side <= RightSide; ++Side)
			{
				TestEqual(TEXT("Un solo tramo por lado"), Plan.Sides[Side].Runs.Num(), 1);
				for (const double Offset : Plan.Sides[Side].OffsetCm)
				{
					Farthest = FMath::Max(Farthest, Offset);
					Nearest = FMath::Min(Nearest, Offset);
				}
			}
			TestTrue(*FString::Printf(TEXT("Calzada de %.0f m: todas las muestras a media calzada + %.0f cm (de %.1f a %.1f)"), Width / 100.0,
				Params.RoadEdgeMarginCm, Nearest, Farthest), FMath::IsNearlyEqual(Nearest, Expected, 1.0) && FMath::IsNearlyEqual(Farthest, Expected, 1.0));
		}
	}
	TestEqual(TEXT("Sin road_width_m, la calzada por defecto (14 m)"), RoadHalfWidthCm(FTrackData(), Params), 700.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDressingDropTest, "Tortunabo.Rally.Dressing.DropLimitsOneSide",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDressingDropTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressingTestHelpers;
	const FTrackData Track = Straight(50000.0);
	FBarrierParams Sparse;
	Sparse.bContinuous = false;
	Sparse.bHugRoad = false;
	const FBarrierPlan Plan = PlanBarriers(Track, DropsOn(Track.Samples.Num(), RightSide, 40, 60), Sparse);
	TestEqual(TEXT("Caída a la derecha: sin límite a la izquierda"), Plan.Sides[LeftSide].Runs.Num(), 0);
	TestEqual(TEXT("Caída a la derecha: un tramo a la derecha"), Plan.Sides[RightSide].Runs.Num(), 1);
	TestEqual(TEXT("El tramo se alarga 15 m antes de la caída"), Plan.Sides[RightSide].Runs[0][0], 36);
	TestEqual(TEXT("...y 15 m después"), Plan.Sides[RightSide].Runs[0].Last(), 64);
	TestTrue(TEXT("En la caída, el límite va al borde de la calzada (7 m + 2,5 m)"), FMath::IsNearlyEqual(Plan.Sides[RightSide].OffsetCm[50], 950.0, 1.0));

	// Con la barrera continua (#303) la caída solo acerca el límite al borde: el resto de la recta también va cerrado.
	FBarrierParams WithShoulder;
	WithShoulder.bHugRoad = false;
	const FBarrierPlan Continuous = PlanBarriers(Track, DropsOn(Track.Samples.Num(), RightSide, 40, 60), WithShoulder);
	TestEqual(TEXT("Continua: la izquierda también tiene límite"), Continuous.Sides[LeftSide].Runs.Num(), 1);
	TestTrue(TEXT("Continua: en la caída, al borde de la calzada"), FMath::IsNearlyEqual(Continuous.Sides[RightSide].OffsetCm[50], 950.0, 1.0));
	TestTrue(TEXT("Continua: lejos de la caída, a la base"), FMath::IsNearlyEqual(Continuous.Sides[RightSide].OffsetCm[100], 1500.0, 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDressingGapTest, "Tortunabo.Rally.Dressing.ShortcutGapsAndShortHoles",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDressingGapTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressingTestHelpers;
	FTrackData Track = Straight(50000.0);
	TArray<uint8> Drops = DropsOn(Track.Samples.Num(), RightSide, 10, 30);
	for (int32 Index = 45; Index <= 70; ++Index)
	{
		Drops[Index] |= 1u << RightSide;
	}
	const FBarrierPlan Joined = PlanBarriers(Track, Drops, FBarrierParams());
	TestEqual(TEXT("Un hueco de 24 m entre dos tramos se cierra"), Joined.Sides[RightSide].Runs.Num(), 1);

	FTNRallyDressingGap OtherSide;
	OtherSide.StartArcCm = 14000.f;
	OtherSide.EndArcCm = 18000.f;
	OtherSide.Side = -1;
	Track.Gaps.Add(OtherSide);
	TestEqual(TEXT("Un atajo del otro lado no abre este"), PlanBarriers(Track, Drops, FBarrierParams()).Sides[RightSide].Runs.Num(), 1);

	FTNRallyDressingGap Shortcut = OtherSide;
	Shortcut.Side = 1;
	Track.Gaps.Add(Shortcut);
	const FBarrierPlan Open = PlanBarriers(Track, Drops, FBarrierParams());
	TestEqual(TEXT("El atajo parte el límite en dos"), Open.Sides[RightSide].Runs.Num(), 2);
	TestFalse(TEXT("Sin límite dentro del atajo"), Open.Sides[RightSide].IsLimited(40));
	TestTrue(TEXT("Con límite justo antes del atajo"), Open.Sides[RightSide].IsLimited(34));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDressingHairpinTest, "Tortunabo.Rally.Dressing.OtherSectionClearance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDressingHairpinTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressingTestHelpers;
	FBarrierParams Params;
	Params.DropEdgeMarginCm = 1.0e6;
	Params.bContinuous = false;
	Params.bHugRoad = false;
	auto MiddleOffset = [&Params](double Separation)
	{
		const FTrackData Track = Hairpin(Separation, 30000.0);
		const FBarrierPlan Plan = PlanBarriers(Track, DropsOn(Track.Samples.Num(), RightSide, 0, Track.Samples.Num()), Params);
		return Plan.Sides[RightSide].OffsetCm[SampleNearX(Track, 15000.0, 0.0)];
	};
	TestTrue(TEXT("Tramos a 30 m: el límite se queda a 15 m"), FMath::IsNearlyEqual(MiddleOffset(3000.0), 1500.0, 1.0));
	TestTrue(TEXT("Tramos a 20 m: el límite va a la mitad"), FMath::IsNearlyEqual(MiddleOffset(2000.0), 1000.0, 1.0));
	TestEqual(TEXT("Tramos a 15 m: no cabe límite entre los dos"), MiddleOffset(1500.0), 0.0);
	// Continua (#303): mientras no pise la calzada, el límite separa los dos tramos (un hueco los uniría).
	Params.bContinuous = true;
	TestTrue(TEXT("Continua, tramos a 15 m: el límite va a la mitad (7,5 m)"), FMath::IsNearlyEqual(MiddleOffset(1500.0), 750.0, 1.0));
	TestEqual(TEXT("Continua, tramos a 12 m (las calzadas se pisan): sin límite"), MiddleOffset(1200.0), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDressingResampleTest, "Tortunabo.Rally.Dressing.ResampleSpacing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDressingResampleTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressing;
	const TArray<FPolySpot> Spots = ResamplePolyline({ FVector(0.0, 0.0, 0.0), FVector(1000.0, 0.0, 100.0), FVector(1000.0, 1000.0, 100.0) }, 300.0);
	TestEqual(TEXT("20 m cada 3 m como mucho: 7 piezas"), Spots.Num(), 7);
	TestTrue(TEXT("Separación igual y no mayor que la pedida"), FMath::IsNearlyEqual(Spots[0].SeparationCm, 2000.0 / 7.0, 0.01));
	TestTrue(TEXT("La primera, a media separación del inicio"), FMath::IsNearlyEqual(Spots[0].Location.X, 1000.0 / 7.0, 0.01));
	TestTrue(TEXT("Cota interpolada a lo largo del tramo"), FMath::IsNearlyEqual(Spots[0].Location.Z, 100.0 / 7.0, 0.01));
	TestTrue(TEXT("Primer tramo: rumbo 0"), FMath::IsNearlyZero(Spots[0].YawDeg, 0.01));
	TestTrue(TEXT("Último tramo: rumbo 90"), FMath::IsNearlyEqual(Spots.Last().YawDeg, 90.0, 0.01));
	TestEqual(TEXT("Sin polilínea, nada"), ResamplePolyline({ FVector::ZeroVector }, 300.0).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDressingDecorTest, "Tortunabo.Rally.Dressing.DecorDeterministicAndOutside",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDressingDecorTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressingTestHelpers;
	const FTrackData Track = Circle(20000.0, true);
	const FBarrierPlan Plan = PlanBarriers(Track, TArray<uint8>(), FBarrierParams());
	const FDecorParams Params;
	const TArray<FSpot> First = PlanDecor(Track, Plan, Entries(), Params, 7);
	const TArray<FSpot> Again = PlanDecor(Track, Plan, Entries(), Params, 7);
	const TArray<FSpot> Other = PlanDecor(Track, Plan, Entries(), Params, 8);
	TestTrue(TEXT("Hay decorado"), First.Num() > 10);
	TestEqual(TEXT("Misma semilla, mismo número de piezas"), Again.Num(), First.Num());
	bool bSame = Again.Num() == First.Num();
	for (int32 Index = 0; bSame && Index < First.Num(); ++Index)
	{
		bSame = First[Index].Location.Equals(Again[Index].Location) && First[Index].Entry == Again[Index].Entry && First[Index].Seed == Again[Index].Seed;
	}
	TestTrue(TEXT("Misma semilla, mismo decorado en todas las máquinas"), bSame);
	bool bDiffers = Other.Num() != First.Num();
	for (int32 Index = 0; !bDiffers && Index < First.Num(); ++Index)
	{
		bDiffers = !First[Index].Location.Equals(Other[Index].Location);
	}
	TestTrue(TEXT("Otra semilla, otro decorado"), bDiffers);
	for (int32 Index = 0; Index < First.Num(); ++Index)
	{
		const FSpot& Spot = First[Index];
		TestTrue(TEXT("Fuera del corredor"), IsClearOfTrack(Track, Spot.Location, Plan.BaseOffsetCm + Spot.RadiusCm));
		for (int32 OtherIndex = Index + 1; OtherIndex < First.Num(); ++OtherIndex)
		{
			TestTrue(TEXT("Sin solaparse"), FVector::Dist2D(Spot.Location, First[OtherIndex].Location) >= Spot.RadiusCm + First[OtherIndex].RadiusCm);
		}
	}
	FDecorParams Empty = Params;
	Empty.BeachPerKm = 0.0;
	Empty.CrabsPerKm = 0.0;
	TestEqual(TEXT("Densidad 0: sin decorado"), PlanDecor(Track, Plan, Entries(), Empty, 7).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyDressingSpectatorTest, "Tortunabo.Rally.Dressing.SpectatorsInCurvesAndFinish",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyDressingSpectatorTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressingTestHelpers;
	FTrackData Track = Straight(30000.0);
	Track.Gates = { FTransform(FVector::ZeroVector), FTransform(FVector(30000.0, 0.0, 0.0)) };
	Track.FinishGate = 1;
	const FDecorParams Params;
	const TArray<FSpot> Finish = PlanSpectators(Track, PlanBarriers(Track, TArray<uint8>(), FBarrierParams()), Params, 3);
	TestEqual(TEXT("Recta: solo el público de la meta, a los dos lados"), Finish.Num(), Params.SpectatorsAtFinish);
	for (const FSpot& Spot : Finish)
	{
		const FVector Facing = FRotator(0.0, Spot.YawDeg, 0.0).Vector();
		const FVector ToAxis(0.0, -FMath::Sign(Spot.Location.Y), 0.0);
		TestTrue(TEXT("Mirando a la calzada"), FVector::DotProduct(Facing, ToAxis) > 0.9);
		TestTrue(TEXT("Detrás del borde del corredor"), FMath::Abs(Spot.Location.Y) >= 700.0 + FBarrierParams().RoadEdgeMarginCm + Params.SpectatorSetbackCm - 1.0);
	}

	const double Radius = 8000.0;
	const FTrackData Ring = Circle(Radius, true);
	const TArray<FSpot> Curve = PlanSpectators(Ring, PlanBarriers(Ring, TArray<uint8>(), FBarrierParams()), Params, 3);
	TestTrue(TEXT("Curva cerrada: hay grupos de público"), Curve.Num() >= Params.SpectatorsPerGroup);
	for (const FSpot& Spot : Curve)
	{
		TestTrue(TEXT("Por fuera de la curva y detrás del límite"),
			FVector::Dist2D(Spot.Location, FVector(0.0, Radius, 0.0)) > Radius + 700.0 + FBarrierParams().RoadEdgeMarginCm);
	}
	return true;
}

#endif
