// Reglas puras del decorado lejano del Rally (TNRallyDressing::PlanFarDecor, #303): franja separada del borde, nunca sobre la
// pista ni sobre otro tramo, nunca en el agua ni en una ladera, presupuesto de piezas y de distancia de dibujado, semilla.
// El suelo se simula con una función. Correr desde Session Frontend (categoría "Tortunabo.Rally.Dressing.Far") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Dressing.Far; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Rally/TN_RallyTrackDressing.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyFarDecorTestHelpers
{
	using namespace TNRallyDressing;

	/** Recta por +X desde el origen, punto a punto. */
	static FTrackData FarStraight(double LengthCm, double StepCm = 400.0)
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

	/** Circuito circular de radio RadiusCm que sale del origen hacia +X y gira a la derecha (+Y). */
	static FTrackData FarCircle(double RadiusCm, double StepCm = 400.0)
	{
		FTrackData Track;
		Track.bClosed = true;
		Track.LengthCm = 2.0 * UE_DOUBLE_PI * RadiusCm;
		const int32 Count = FMath::RoundToInt32(Track.LengthCm / StepCm);
		Track.StepCm = Track.LengthCm / Count;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FAxisSample& Sample = Track.Samples.AddDefaulted_GetRef();
			Sample.Arc = Index * Track.StepCm;
			const double Angle = Sample.Arc / RadiusCm;
			Sample.Location = FVector(RadiusCm * FMath::Sin(Angle), RadiusCm * (1.0 - FMath::Cos(Angle)), 0.0);
			Sample.Direction = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
		}
		return Track;
	}

	/** Horquilla: recta por +X, media vuelta a la derecha y vuelta por -X a SeparationCm del primer tramo. */
	static FTrackData FarHairpin(double SeparationCm, double StraightCm, double StepCm = 400.0)
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

	static TArray<FTNRallyFarDecorEntry> FarEntries()
	{
		TArray<FTNRallyFarDecorEntry> Result;
		for (const float Radius : { 600.f, 1500.f, 3000.f })
		{
			FTNRallyFarDecorEntry& Entry = Result.AddDefaulted_GetRef();
			Entry.MinRadiusCm = 0.7f * Radius;
			Entry.MaxRadiusCm = Radius;
		}
		return Result;
	}

	/** Suelo llano a cota 0 en todas partes. */
	const auto FlatGround = [](const FVector& /*Probe*/, double& OutZ)
	{
		OutZ = 0.0;
		return true;
	};

	/** Distancia en planta de Point a la muestra más cercana del eje. */
	static double AxisDistance(const FTrackData& Track, const FVector& Point)
	{
		double Best = TNumericLimits<double>::Max();
		for (const FAxisSample& Sample : Track.Samples)
		{
			Best = FMath::Min(Best, FVector::Dist2D(Sample.Location, Point));
		}
		return Best;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyFarDecorClearanceTest, "Tortunabo.Rally.Dressing.FarDecorNeverNearTheAxis",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyFarDecorClearanceTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyFarDecorTestHelpers;
	const FFarDecorParams Params;
	for (const FTrackData& Track : { FarCircle(15000.0), FarStraight(100000.0), FarHairpin(9000.0, 40000.0) })
	{
		const FBarrierPlan Plan = PlanBarriers(Track, TArray<uint8>(), FBarrierParams());
		const TArray<FSpot> Spots = PlanFarDecor(Track, Plan, FarEntries(), Params, 11, FlatGround);
		TestTrue(TEXT("Hay decorado lejano"), Spots.Num() > 4);
		// Borde base (la barrera, #303) + 15 m de hueco: ninguna huella más cerca del eje, tampoco de otro tramo de la horquilla.
		const double MinClear = FarMinAxisClearanceCm(Plan, Params);
		TestTrue(TEXT("Hueco mínimo: el borde base más 15 m"), FMath::IsNearlyEqual(MinClear, Plan.BaseOffsetCm + 1500.0, 1.0));
		int32 Inside = 0;
		for (const FSpot& Spot : Spots)
		{
			Inside += AxisDistance(Track, Spot.Location) - Spot.RadiusCm < MinClear - 1.0 ? 1 : 0;
			TestTrue(TEXT("Pieza lejana"), Spot.Kind == ESpotKind::Far);
		}
		TestEqual(TEXT("Ninguna huella dentro del hueco mínimo"), Inside, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyFarDecorBandTest, "Tortunabo.Rally.Dressing.FarDecorInBandAndNoOverlap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyFarDecorBandTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyFarDecorTestHelpers;
	const FTrackData Track = FarStraight(100000.0);
	const FBarrierPlan Plan = PlanBarriers(Track, TArray<uint8>(), FBarrierParams());
	const FFarDecorParams Params;
	const TArray<FSpot> Spots = PlanFarDecor(Track, Plan, FarEntries(), Params, 5, FlatGround);
	bool bLeft = false;
	bool bRight = false;
	for (int32 Index = 0; Index < Spots.Num(); ++Index)
	{
		const FSpot& Spot = Spots[Index];
		const double Lateral = FMath::Abs(Spot.Location.Y);
		bLeft |= Spot.Location.Y < 0.0;
		bRight |= Spot.Location.Y > 0.0;
		TestTrue(TEXT("Dentro de la franja: borde + hueco + huella + como mucho la franja"),
			Lateral >= Plan.BaseOffsetCm + Params.MinFromEdgeCm + Spot.RadiusCm - 1.0
			&& Lateral <= Plan.BaseOffsetCm + Params.MinFromEdgeCm + Spot.RadiusCm + Params.BandCm + 1.0);
		TestTrue(TEXT("Grande: radio de la entrada"), Spot.RadiusCm >= 0.7 * 600.0 - 1.0 && Spot.RadiusCm <= 3000.0 + 1.0);
		for (int32 Other = Index + 1; Other < Spots.Num(); ++Other)
		{
			TestTrue(TEXT("Sin solaparse"), FVector::Dist2D(Spot.Location, Spots[Other].Location) >= Spot.RadiusCm + Spots[Other].RadiusCm);
		}
	}
	TestTrue(TEXT("A los dos lados"), bLeft && bRight);

	// El decorado cercano no pisa el lejano.
	const TArray<FSpot> Near = PlanDecor(Track, Plan, { FTNRallyDecorEntry() }, FDecorParams(), 5, Spots);
	int32 Overlaps = 0;
	for (const FSpot& NearSpot : Near)
	{
		for (const FSpot& FarSpot : Spots)
		{
			Overlaps += FVector::Dist2D(NearSpot.Location, FarSpot.Location) < NearSpot.RadiusCm + FarSpot.RadiusCm ? 1 : 0;
		}
	}
	TestTrue(TEXT("Hay decorado cercano"), Near.Num() > 0);
	TestEqual(TEXT("El cercano no pisa el lejano"), Overlaps, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyFarDecorWaterTest, "Tortunabo.Rally.Dressing.FarDecorNotInWaterNorSlopes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyFarDecorWaterTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyFarDecorTestHelpers;
	FTrackData Track = FarStraight(100000.0);
	Track.bHasWater = true;
	Track.WaterZ = 0.0;
	const FBarrierPlan Plan = PlanBarriers(Track, TArray<uint8>(), FBarrierParams());
	const FFarDecorParams Params;
	// A la derecha (+Y) el mar: arena hundida bajo el agua. A la izquierda, arena a +2 m salvo una zanja en X 30-60 m.
	auto Coast = [](const FVector& Probe, double& OutZ)
	{
		if (Probe.Y > 0.0)
		{
			OutZ = -300.0;
			return true;
		}
		OutZ = (Probe.X > 30000.0 && Probe.X < 60000.0) ? -20.0 : 200.0;
		return true;
	};
	const TArray<FSpot> Spots = PlanFarDecor(Track, Plan, FarEntries(), Params, 3, Coast);
	TestTrue(TEXT("Hay decorado en tierra"), Spots.Num() > 2);
	int32 InWater = 0;
	for (const FSpot& Spot : Spots)
	{
		InWater += Spot.Location.Y > 0.0 || Spot.Location.Z <= Track.WaterZ + Params.WaterMarginCm ? 1 : 0;
		const double Reach = Spot.RadiusCm * Params.ProbeRadiusFraction;
		InWater += Spot.Location.X + Reach > 30000.0 && Spot.Location.X - Reach < 60000.0 ? 1 : 0;
		TestTrue(TEXT("Apoyada en la cota del suelo"), FMath::IsNearlyEqual(Spot.Location.Z, 200.0));
	}
	TestEqual(TEXT("Ninguna pieza (ni el contorno de su huella) en el agua"), InWater, 0);

	// Ladera: cada metro hacia fuera sube 50 cm; con un desnivel máximo de 1 m bajo la huella no cabe ninguna.
	FFarDecorParams Steep = Params;
	Steep.MaxGroundStepCm = 100.0;
	auto Slope = [](const FVector& Probe, double& OutZ)
	{
		OutZ = 0.5 * FMath::Abs(Probe.Y);
		return true;
	};
	TestEqual(TEXT("En una ladera empinada no se pone nada"), PlanFarDecor(Track, Plan, FarEntries(), Steep, 3, Slope).Num(), 0);
	auto Void = [](const FVector& /*Probe*/, double& OutZ)
	{
		OutZ = 0.0;
		return false;
	};
	TestEqual(TEXT("Sin suelo no se pone nada"), PlanFarDecor(Track, Plan, FarEntries(), Params, 3, Void).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyFarDecorBudgetTest, "Tortunabo.Rally.Dressing.FarDecorBudgetAndCull",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyFarDecorBudgetTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyFarDecorTestHelpers;
	const FTrackData Track = FarStraight(400000.0);
	const FBarrierPlan Plan = PlanBarriers(Track, TArray<uint8>(), FBarrierParams());
	FFarDecorParams Params;
	Params.PerKm = 40.0;
	Params.MaxPieces = 24;
	const TArray<FSpot> Spots = PlanFarDecor(Track, Plan, FarEntries(), Params, 9, FlatGround);
	TestTrue(TEXT("Presupuesto: no pasa de MaxPieces"), Spots.Num() <= Params.MaxPieces);
	TestTrue(TEXT("Presupuesto: lo agota casi entero en suelo llano"), Spots.Num() >= Params.MaxPieces - 4);
	double MaxX = 0.0;
	for (const FSpot& Spot : Spots)
	{
		MaxX = FMath::Max(MaxX, Spot.Location.X);
	}
	TestTrue(TEXT("Presupuesto repartido por todo el trazado, no gastado al principio"), MaxX > 0.5 * Track.LengthCm);

	FFarDecorParams Zero = Params;
	Zero.MaxPieces = 0;
	TestEqual(TEXT("Presupuesto 0: nada"), PlanFarDecor(Track, Plan, FarEntries(), Zero, 9, FlatGround).Num(), 0);
	Zero = Params;
	Zero.PerKm = 0.0;
	TestEqual(TEXT("Densidad 0: nada"), PlanFarDecor(Track, Plan, FarEntries(), Zero, 9, FlatGround).Num(), 0);

	const FFarDecorParams Defaults;
	TestTrue(TEXT("Cull: nunca por encima del máximo"), FarCullDistanceCm(1.0e6, Defaults) <= Defaults.MaxCullCm);
	TestTrue(TEXT("Cull: nunca por debajo del mínimo ni 0 (siempre se recorta)"), FarCullDistanceCm(0.0, Defaults) >= Defaults.MinCullCm && Defaults.MinCullCm > 0.0);
	TestTrue(TEXT("Cull: lo más grande se ve desde más lejos"), FarCullDistanceCm(3000.0, Defaults) > FarCullDistanceCm(600.0, Defaults));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyFarDecorSeedTest, "Tortunabo.Rally.Dressing.FarDecorDeterministic",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyFarDecorSeedTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyFarDecorTestHelpers;
	const FTrackData Track = FarCircle(20000.0);
	const FBarrierPlan Plan = PlanBarriers(Track, TArray<uint8>(), FBarrierParams());
	const FFarDecorParams Params;
	const TArray<FSpot> First = PlanFarDecor(Track, Plan, FarEntries(), Params, 21, FlatGround);
	const TArray<FSpot> Again = PlanFarDecor(Track, Plan, FarEntries(), Params, 21, FlatGround);
	const TArray<FSpot> Other = PlanFarDecor(Track, Plan, FarEntries(), Params, 22, FlatGround);
	bool bSame = First.Num() == Again.Num() && First.Num() > 0;
	for (int32 Index = 0; bSame && Index < First.Num(); ++Index)
	{
		bSame = First[Index].Location.Equals(Again[Index].Location) && First[Index].Entry == Again[Index].Entry
			&& First[Index].Seed == Again[Index].Seed && FMath::IsNearlyEqual(First[Index].RadiusCm, Again[Index].RadiusCm);
	}
	TestTrue(TEXT("Misma semilla, mismo decorado lejano en todas las máquinas"), bSame);
	bool bDiffers = Other.Num() != First.Num();
	for (int32 Index = 0; !bDiffers && Index < First.Num(); ++Index)
	{
		bDiffers = !First[Index].Location.Equals(Other[Index].Location);
	}
	TestTrue(TEXT("Otra semilla, otro decorado lejano"), bDiffers);
	TestEqual(TEXT("Sin entradas, nada"), PlanFarDecor(Track, Plan, TArray<FTNRallyFarDecorEntry>(), Params, 21, FlatGround).Num(), 0);
	return true;
}

#endif
