// Todos contra Todos, #920: muchos más puntos de objetos y mucho más decorado, veneno visible y aviso diegético del nivel del agua.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tct.Scenery920; Quit" -nullrhi -unattended
// Las pruebas son de lógica pura (reparto, reglas y mallas) y un actor del aviso sobre un mundo de prueba sin ventana.

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/TN_TctItemRules.h"
#include "Game/TN_TctRules.h"
#include "World/Beach/TN_BeachTypes.h"
#include "World/TN_TctItemPad.h"
#include "World/TN_TctSceneryPlan.h"
#include "World/TN_TctShoreMarks.h"
#include "World/TN_TctShoreWarning.h"
#include "World/ProcMap/TN_TctPropMeshes.h"
#include "World/ProcMap/TN_TctShoreMeshes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTct920Test
{
	/** Meseta de 120 m a +2 m, un puente de 8 m al este y una rampa de 15° de 30 m al oeste; el resto, vacío. */
	bool MesaGround(double X, double Y, float& OutZ)
	{
		const double Ax = FMath::Abs(X);
		const double Ay = FMath::Abs(Y);
		if (Ax <= 6000.0 && Ay <= 6000.0) { OutZ = 200.f; return true; }
		if (X > 6000.0 && X <= 9000.0 && Ay <= 400.0) { OutZ = 200.f; return true; }
		if (X < -6000.0 && X >= -9000.0 && Ay <= 3000.0) { OutZ = 200.f - static_cast<float>((-6000.0 - X) * 0.2679); return true; }
		return false;
	}

	/** Una pendiente suave de la playa (2,9°): la cota sube con X. */
	bool SlopeGround(double X, double Y, float& OutZ)
	{
		if (FMath::Abs(X) <= 6000.0 && FMath::Abs(Y) <= 6000.0) { OutZ = static_cast<float>(X * 0.05); return true; }
		return false;
	}

	TNTctScenery::FHeightField MakeField(TFunctionRef<bool(double, double, float&)> Ground)
	{
		TNTctScenery::FHeightField Field;
		Field.Build(FVector2D(-10000.0, -10000.0), FVector2D(10000.0, 10000.0), 100.0, Ground);
		Field.ComputeOpen(10.f, 3500.f);
		return Field;
	}

	/** Salidas y nueve puntos de objetos en rejilla sobre la meseta: lo que el decorado no puede llenar. */
	TArray<TNTctScenery::FKeepOut> ExitsAndPads()
	{
		TArray<TNTctScenery::FKeepOut> Zones;
		Zones.Add({ FVector2D(0.0, 0.0), 900.f });
		Zones.Add({ FVector2D(4000.0, 4000.0), 900.f });
		Zones.Add({ FVector2D(-4000.0, -4000.0), 900.f });
		for (int32 X = 0; X < 3; ++X)
		{
			for (int32 Y = 0; Y < 3; ++Y)
			{
				Zones.Add({ FVector2D(-3000.0 + X * 3000.0, -3000.0 + Y * 3000.0), 450.f });
			}
		}
		return Zones;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTct920PadsTest,
	"Tortunabo.Tct.Scenery920.Pads",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTct920PadsTest::RunTest(const FString& Parameters)
{
	// Muchos más puntos: 36 en una arena de 140 m, separados y lejos de las salidas, con las tres rarezas por altura y exposición.
	TArray<FTNTctPadSpot> Spots;
	for (int32 X = -7000; X <= 7000; X += 250)
	{
		for (int32 Y = -7000; Y <= 7000; Y += 250)
		{
			FTNTctPadSpot& Spot = Spots.AddDefaulted_GetRef();
			Spot.Pos = FVector(X, Y, 200.0 + (X > 0 ? 800.0 : 0.0));
			Spot.HeightFrac = X > 2000 ? 1.f : (X > -2000 ? 0.45f : 0.05f);
			Spot.Exposure = (FMath::Abs(X) > 6000 || FMath::Abs(Y) > 6000) ? 1.f : 0.f;
		}
	}
	const TArray<FVector> Avoid = { FVector(-6000, -6000, 0), FVector(6000, -6000, 0), FVector(-6000, 6000, 0), FVector(6000, 6000, 0) };
	const TArray<FTNTctPadPick> Picks = TNTctItemRules::PlanPads(Spots, 36, Avoid, 700.f, 1000.f);
	TestEqual(TEXT("36 puntos de objetos"), Picks.Num(), 36);
	int32 PerRarity[3] = { 0, 0, 0 };
	for (int32 A = 0; A < Picks.Num(); ++A)
	{
		++PerRarity[static_cast<int32>(Picks[A].Rarity)];
		for (const FVector& Spot : Avoid)
		{
			TestTrue(TEXT("Lejos de las salidas"), FVector::Dist2D(Spots[Picks[A].Index].Pos, Spot) >= 700.0);
		}
		for (int32 B = A + 1; B < Picks.Num(); ++B)
		{
			TestTrue(TEXT("Separados"), FVector::Dist2D(Spots[Picks[A].Index].Pos, Spots[Picks[B].Index].Pos) >= 1000.0);
		}
		// La rareza sigue a la altura y la exposición: un sitio bajo y sin exposición nunca es épico.
		if (Picks[A].Rarity == ETNTctRarity::Epic)
		{
			TestTrue(TEXT("Un punto épico es alto o expuesto"), Spots[Picks[A].Index].HeightFrac >= 0.45f || Spots[Picks[A].Index].Exposure > 0.5f);
		}
	}
	TestTrue(TEXT("Hay de las tres rarezas"), PerRarity[0] > 0 && PerRarity[1] > 0 && PerRarity[2] > 0);
	TestTrue(TEXT("Más comunes que épicos"), PerRarity[0] > PerRarity[2]);

	// Se usan muchos más a la vez: cuatro por tortuga y cuatro más (de 10 a todos).
	TestEqual(TEXT("Cuatro tortugas: 20 puntos activos"), TNTctItemRules::ActivePadCount(4, 36), 20);
	TestEqual(TEXT("Ocho tortugas: los 36"), TNTctItemRules::ActivePadCount(8, 36), 36);

	// Cada punto se ve metido en el mundo: cofre (épico), nido o restos (raro), piedras o sombrilla (común); el haz es fino y corto.
	using namespace TNTctMesh;
	for (uint32 Seed = 0; Seed < 64; ++Seed)
	{
		TestEqual(TEXT("Épico: cofre"), PadStyleFor(2, Seed), EPadStyle::Chest);
		const EPadStyle Rare = PadStyleFor(1, Seed);
		TestTrue(TEXT("Raro: nido o restos de barco"), Rare == EPadStyle::Nest || Rare == EPadStyle::WreckPile);
		const EPadStyle Common = PadStyleFor(0, Seed);
		TestTrue(TEXT("Común: piedras o sombrilla"), Common == EPadStyle::Rocks || Common == EPadStyle::Parasol);
	}
	for (int32 Index = 0; Index < static_cast<int32>(EPadStyle::Count); ++Index)
	{
		const EPadStyle Style = static_cast<EPadStyle>(Index);
		TNProcMesh::FTNProcMeshBuffers Buffers;
		BuildPad(Buffers, Style, 0x920u + Index);
		TestFalse(FString::Printf(TEXT("%s tiene malla"), PadStyleName(Style)), Buffers.IsEmpty());
		TestEqual(TEXT("Buffers coherentes"), Buffers.Verts.Num(), Buffers.Colors.Num());
		TestEqual(TEXT("Normales"), Buffers.Verts.Num(), Buffers.Normals.Num());
		TestEqual(TEXT("Triángulos enteros"), Buffers.Tris.Num() % 3, 0);
		bool bInside = true;
		for (const FVector& V : Buffers.Verts)
		{
			// El objeto queda en el centro y la pieza no se sale de ~3 m ni flota ni se hunde de más.
			bInside &= FVector2D(V.X, V.Y).Size() <= 320.0 && V.Z >= -40.0 && V.Z <= 400.0;
		}
		TestTrue(FString::Printf(TEXT("%s: dentro de sus límites"), PadStyleName(Style)), bInside);
		TestTrue(TEXT("El destello queda sobre el suelo"), PadGlintOffset(Style).Z > 20.0);
	}
	TestTrue(TEXT("Haz: épico más alto que raro, común sin haz"),
		ATN_TctItemPad::BeamHeight(ETNTctRarity::Epic) > ATN_TctItemPad::BeamHeight(ETNTctRarity::Rare)
		&& ATN_TctItemPad::BeamHeight(ETNTctRarity::Common) <= 0.f);
	TestTrue(TEXT("Haz corto, no un pilar"), ATN_TctItemPad::BeamHeight(ETNTctRarity::Epic) <= 1000.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTct920ScenerytTest,
	"Tortunabo.Tct.Scenery920.Decor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTct920ScenerytTest::RunTest(const FString& Parameters)
{
	using namespace TNTctScenery;
	using namespace TNTct920Test;
	const FHeightField Field = MakeField(MesaGround);
	const TArray<FKeepOut> Zones = ExitsAndPads();

	// Mucha más vegetación que antes (densidad por defecto contra la del mapa generado) sobre el mismo suelo.
	FPlanOptions Old;
	Old.FloraDensity = 1.f;
	Old.MaxFlora = 0;
	const FPlan Sparse = MakePlan(Field, -400.f, Zones, ETNProcBiome::Beach, 4242u, Old);
	const FPlan Dense = MakePlan(Field, -400.f, Zones, ETNProcBiome::Beach, 4242u);
	AddInfo(FString::Printf(TEXT("Antes %d plantas; ahora %d, %d piezas y %d estructuras con colisión"), Sparse.Flora.Num(), Dense.Flora.Num(),
		Dense.Decor.Num(), Dense.Structures.Num()));
	TestTrue(TEXT("Bastante más vegetación"), Dense.Flora.Num() * 2 > Sparse.Flora.Num() * 3);
	TestTrue(TEXT("Hay decorado con colisión"), Dense.Decor.Num() >= 3);
	TestTrue(TEXT("Más tipos de decorado de playa que los cuatro de antes"), DecorElementsFor(ETNProcBiome::Beach).Num() >= 8);

	// Las especies nuevas están al final: los índices de las del mapa generado no se mueven y no pasan del tope de PlaceFloraRows.
	TArray<TNProcMap::FFloraSpecies> Base, Extended;
	TNProcMap::FloraSpeciesFor(ETNProcBiome::Beach, Base);
	SpeciesFor(ETNProcBiome::Beach, Extended);
	TestTrue(TEXT("Hay especies de playa nuevas"), Extended.Num() > Base.Num());
	TestTrue(TEXT("Sin pasar el tope de especies"), Extended.Num() <= 24);
	for (int32 Index = 0; Index < Base.Num(); ++Index)
	{
		TestTrue(TEXT("Las especies de siempre, en su sitio"), Extended[Index].Shape == Base[Index].Shape && Extended[Index].Prop == Base[Index].Prop);
	}
	bool bHasReeds = false;
	for (const TNProcMap::FFloraSpecies& Species : Extended) { bHasReeds |= Species.Shape == TNProcMap::EFloraShape::Reeds; }
	TestTrue(TEXT("Algas y juncos en la orilla"), bHasReeds);

	// Estructuras: con paso libre alrededor, lejos de salidas y puntos, sobre el suelo llano y sin pegarse a nada con colisión.
	int32 TotalStructures = 0;
	int32 Huts = 0;
	int32 Wrecks = 0;
	for (uint32 Seed = 1; Seed <= 8; ++Seed)
	{
		const FPlan Plan = MakePlan(Field, -400.f, Zones, ETNProcBiome::Beach, Seed * 7919u);
		TestEqual(TEXT("Misma semilla, mismo reparto"), MakePlan(Field, -400.f, Zones, ETNProcBiome::Beach, Seed * 7919u).Fingerprint, Plan.Fingerprint);
		TotalStructures += Plan.Structures.Num();
		for (int32 A = 0; A < Plan.Structures.Num(); ++A)
		{
			const FStructurePick& Pick = Plan.Structures[A];
			const FVector2D P(Pick.Location.X, Pick.Location.Y);
			const double Radius = StructureRadius(Pick.Kind) * Pick.Scale;
			(Pick.Kind == EStructureKind::Hut ? Huts : Wrecks) += 1;
			TestTrue(TEXT("Dentro de la meseta, no en el puente ni en la rampa"), FMath::Abs(P.X) < 6000.0 && FMath::Abs(P.Y) < 6000.0);
			TestTrue(TEXT("Con sitio de sobra alrededor"), Field.OpenAt(P) >= Radius + 500.0);
			TestTrue(TEXT("Lejos de las salidas y de los puntos de objetos"), KeepOutDistance(Zones, P) >= Radius + 240.0);
			TestTrue(TEXT("Sobre el suelo"), FMath::IsNearlyEqual(Pick.Location.Z, 200.0, 1.0));
			for (int32 B = A + 1; B < Plan.Structures.Num(); ++B)
			{
				const double Other = StructureRadius(Plan.Structures[B].Kind) * Plan.Structures[B].Scale;
				TestTrue(TEXT("Paso entre dos estructuras"), FVector2D::Distance(P, FVector2D(Plan.Structures[B].Location.X, Plan.Structures[B].Location.Y)) >= Radius + Other + 400.0);
			}
			for (const FDecorPick& Decor : Plan.Decor)
			{
				const double Other = TNBeach::FootprintRadius(Decor.Element) * Decor.Scale;
				TestTrue(TEXT("Paso entre una estructura y el decorado"), FVector2D::Distance(P, FVector2D(Decor.Location.X, Decor.Location.Y)) >= Radius + Other + 400.0);
			}
		}
		// Las plantas, igual que antes: sobre suelo, nunca en una salida o un punto de objetos, ni en la rampa.
		int32 Bad = 0;
		for (const TNProcMap::FFloraInstance& Instance : Plan.Flora)
		{
			const FVector2D P(Instance.Location.X, Instance.Location.Y);
			float Z = 0.f;
			Bad += (!Field.HeightAt(P, Z) || KeepOutDistance(Zones, P) < 0.0 || (P.X < -6000.0 && P.X > -9000.0)) ? 1 : 0;
		}
		TestEqual(TEXT("Ninguna planta sobre el vacío, la rampa, una salida o un punto de objetos"), Bad, 0);
		for (const FDecorPick& Decor : Plan.Decor)
		{
			const double Radius = TNBeach::FootprintRadius(Decor.Element) * Decor.Scale;
			TestTrue(TEXT("Decorado lejos de salidas y puntos"), KeepOutDistance(Zones, FVector2D(Decor.Location.X, Decor.Location.Y)) >= Radius + 240.0);
		}
		TestTrue(TEXT("Con tope de plantas"), Plan.Flora.Num() <= FPlanOptions().MaxFlora);
	}
	AddInfo(FString::Printf(TEXT("Estructuras en 8 semillas: %d casetas y %d barcos"), Huts, Wrecks));
	TestTrue(TEXT("Casetas y barcos varados por la arena"), TotalStructures >= 6);

	// Las mallas de las estructuras.
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		TNProcMesh::FTNProcMeshBuffers Hut, Wreck;
		TNTctMesh::BuildHut(Hut, Variant, 0x920u);
		TNTctMesh::BuildWreck(Wreck, Variant, 0x920u);
		TestFalse(TEXT("La caseta tiene malla"), Hut.IsEmpty());
		TestFalse(TEXT("El barco tiene malla"), Wreck.IsEmpty());
		bool bHutInside = true;
		bool bWreckInside = true;
		for (const FVector& V : Hut.Verts) { bHutInside &= FMath::Abs(V.X) <= 200.0 && FMath::Abs(V.Y) <= 160.0 && V.Z >= -20.0 && V.Z <= 280.0; }
		for (const FVector& V : Wreck.Verts) { bWreckInside &= FMath::Abs(V.X) <= 230.0 && FMath::Abs(V.Y) <= 140.0 && V.Z >= -40.0 && V.Z <= 200.0; }
		TestTrue(TEXT("La caseta cabe en su huella"), bHutInside);
		TestTrue(TEXT("El barco cabe en su huella"), bWreckInside);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTct920PoisonTest,
	"Tortunabo.Tct.Scenery920.Poison",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTct920PoisonTest::RunTest(const FString& Parameters)
{
	// El tinte verde entra rápido dentro del agua, sale despacio fuera, y nunca pasa de 0-1.
	float Fade = 0.f;
	Fade = TNTctRules::PoisonVisionStep(Fade, true, TNTctPoisonDefaults::VisionFadeIn * 0.5f);
	TestTrue(TEXT("A media entrada, a medias"), FMath::IsNearlyEqual(Fade, 0.5f, 0.001f));
	Fade = TNTctRules::PoisonVisionStep(Fade, true, 10.f);
	TestEqual(TEXT("Dentro del agua, entero"), Fade, 1.f);
	Fade = TNTctRules::PoisonVisionStep(Fade, false, TNTctPoisonDefaults::VisionFadeOut * 0.5f);
	TestTrue(TEXT("Fuera del agua, sale poco a poco"), FMath::IsNearlyEqual(Fade, 0.5f, 0.001f));
	Fade = TNTctRules::PoisonVisionStep(Fade, false, 10.f);
	TestEqual(TEXT("Sin agua, sin tinte"), Fade, 0.f);
	TestTrue(TEXT("Sale más despacio de lo que entra"), TNTctPoisonDefaults::VisionFadeOut > TNTctPoisonDefaults::VisionFadeIn);

	TestEqual(TEXT("Sin fundido, sin peso"), TNTctRules::PoisonVisionWeight(0.f, 1.f), 0.f);
	const float Light = TNTctRules::PoisonVisionWeight(1.f, 0.f);
	const float Heavy = TNTctRules::PoisonVisionWeight(1.f, 1.f);
	TestTrue(TEXT("Más intoxicada, más verde"), Heavy > Light);
	TestTrue(TEXT("Suave: nunca tapa la imagen"), Light > 0.2f && Heavy <= 0.8f);
	TestTrue(TEXT("El peso sigue al fundido"), TNTctRules::PoisonVisionWeight(0.5f, 0.5f) < TNTctRules::PoisonVisionWeight(1.f, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTct920ShoreTest,
	"Tortunabo.Tct.Scenery920.Shore",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTct920ShoreTest::RunTest(const FString& Parameters)
{
	using namespace TNTctScenery;
	using namespace TNTct920Test;

	// La cota del aviso es la del siguiente escalón del plan (la misma de la cuenta atrás), y aparece con antelación.
	FTNTctFloodPlan Plan;
	Plan.BaseZ = 0.f;
	Plan.Levels = { 300.f, 600.f, 900.f };
	Plan.SuddenDeathZ = 1200.f;
	float LastProgress = 0.f;
	int32 Checked = 0;
	for (float Elapsed = 0.f; Elapsed < 150.f; Elapsed += 0.5f)
	{
		const FTNTctNextRise Next = TNTctRules::NextRise(Plan, Elapsed);
		const float Progress = TNTctRules::ShoreWarnProgress(Next);
		TestTrue(TEXT("Avance entre 0 y 1"), Progress >= 0.f && Progress <= 1.f);
		if (Next.bRising)
		{
			TestEqual(TEXT("Subiendo: se ve entero"), Progress, 1.f);
			TestEqual(TEXT("Subiendo: la cota a la que llega"), TNTctRules::ShoreWarnTargetZ(Next), Next.RisingTargetZ);
		}
		else if (Progress > 0.f)
		{
			const float StepZ = Next.Step < Plan.Levels.Num() ? Plan.Levels[Next.Step] : Plan.SuddenDeathZ;
			TestEqual(TEXT("La cota del aviso es la del siguiente escalón"), TNTctRules::ShoreWarnTargetZ(Next), StepZ);
			TestTrue(TEXT("Con antelación: dentro del margen del aviso"), Next.SecondsLeft <= TNTctPoisonDefaults::ShoreWarnLead);
			TestTrue(TEXT("Crece hacia la subida"), Progress >= LastProgress - 0.0001f || LastProgress >= 0.99f);
			++Checked;
		}
		else if (Next.bUpcoming)
		{
			TestTrue(TEXT("Sin aviso hasta el margen"), Next.SecondsLeft >= TNTctPoisonDefaults::ShoreWarnLead);
		}
		LastProgress = Progress;
	}
	TestTrue(TEXT("Se comprobó el aviso antes de cada subida"), Checked > 40);
	TestEqual(TEXT("Sin más subidas, sin aviso"), TNTctRules::ShoreWarnProgress(TNTctRules::NextRise(Plan, 400.f)), 0.f);
	{
		// 15 s antes del primer escalón (a los 25 s), un 25 %; y 5 s antes, el 75 %.
		FTNTctNextRise Fifteen = TNTctRules::NextRise(Plan, Plan.StartDelay - 15.f);
		TestTrue(TEXT("Un cuarto a 15 s"), FMath::IsNearlyEqual(TNTctRules::ShoreWarnProgress(Fifteen), 0.25f, 0.01f));
		TestEqual(TEXT("El primer aviso marca el primer escalón"), TNTctRules::ShoreWarnTargetZ(Fifteen), Plan.Levels[0]);
		TestTrue(TEXT("A 5 s, el 75 %"), FMath::IsNearlyEqual(TNTctRules::ShoreWarnProgress(TNTctRules::NextRise(Plan, Plan.StartDelay - 5.f)), 0.75f, 0.01f));
	}

	// Las marcas: sobre la orilla futura (el suelo justo bajo esa cota), sin llenar salidas ni puntos, iguales en todas las máquinas.
	const FHeightField Slope = MakeField(SlopeGround);
	TArray<FKeepOut> Zones;
	Zones.Add({ FVector2D(1700.0, 0.0), 900.f });
	Zones.Add({ FVector2D(1700.0, 3000.0), 700.f });
	const float Target = 100.f;
	const TArray<FShoreMark> Marks = PlanShoreMarks(Slope, Target, Zones, 0x920u);
	AddInfo(FString::Printf(TEXT("Orilla a %.0f uu: %d marcas"), Target, Marks.Num()));
	TestTrue(TEXT("Hay marcas"), Marks.Num() > 100);
	TestTrue(TEXT("Con tope"), Marks.Num() <= 520);
	int32 Foam = 0, Algae = 0, Posts = 0;
	for (const FShoreMark& Mark : Marks)
	{
		TestTrue(TEXT("En la cota del siguiente escalón"), Mark.Location.Z >= Target - ShoreBandBelow - 1.f && Mark.Location.Z <= Target + ShoreBandAbove + 1.f);
		TestTrue(TEXT("Sobre el suelo medido"), FMath::Abs(Mark.Location.Z - Mark.Location.X * 0.05) < 2.0);
		TestTrue(TEXT("Lejos de salidas y puntos de objetos"), KeepOutDistance(Zones, FVector2D(Mark.Location.X, Mark.Location.Y)) >= 100.0);
		Foam += Mark.Kind == ShoreKindFoam;
		Algae += Mark.Kind == ShoreKindAlgae;
		Posts += Mark.Kind == ShoreKindPost;
	}
	TestTrue(TEXT("Espuma y algas"), Foam > 20 && Algae > 20);
	TestTrue(TEXT("Algún poste, no un bosque"), Posts >= 1 && Posts <= 24);
	// Los postes, separados.
	for (int32 A = 0; A < Marks.Num(); ++A)
	{
		for (int32 B = A + 1; B < Marks.Num() && Marks[A].Kind == ShoreKindPost; ++B)
		{
			if (Marks[B].Kind == ShoreKindPost)
			{
				TestTrue(TEXT("Postes separados"), FVector::Dist2D(Marks[A].Location, Marks[B].Location) >= 1000.0);
			}
		}
	}
	const TArray<FShoreMark> Again = PlanShoreMarks(MakeField(SlopeGround), Target, Zones, 0x920u);
	bool bSame = Again.Num() == Marks.Num();
	for (int32 Index = 0; bSame && Index < Marks.Num(); ++Index)
	{
		bSame &= Again[Index].Kind == Marks[Index].Kind && Again[Index].Location.Equals(Marks[Index].Location, 0.01);
	}
	TestTrue(TEXT("Misma entrada, mismas marcas"), bSame);
	const TArray<FShoreMark> Higher = PlanShoreMarks(Slope, 200.f, Zones, 0x920u);
	double MaxLow = -1.0e9, MinHigh = 1.0e9;
	for (const FShoreMark& Mark : Marks) { MaxLow = FMath::Max(MaxLow, Mark.Location.X); }
	for (const FShoreMark& Mark : Higher) { MinHigh = FMath::Min(MinHigh, Mark.Location.X); }
	TestTrue(TEXT("Otro escalón, otra orilla (más adentro)"), Higher.Num() > 0 && MinHigh > MaxLow);
	TestEqual(TEXT("Sin suelo, sin marcas"), PlanShoreMarks(FHeightField(), Target, Zones, 1u).Num(), 0);
	TestEqual(TEXT("Una cota que no toca el suelo, sin marcas"), PlanShoreMarks(Slope, 5000.f, Zones, 1u).Num(), 0);

	// Las mallas de las marcas.
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		TNProcMesh::FTNProcMeshBuffers FoamMesh, AlgaeMesh;
		TNTctMesh::BuildFoam(FoamMesh, Variant, 7u);
		TNTctMesh::BuildAlgae(AlgaeMesh, Variant, 7u);
		TestFalse(TEXT("La espuma tiene malla"), FoamMesh.IsEmpty());
		TestFalse(TEXT("Las algas tienen malla"), AlgaeMesh.IsEmpty());
		bool bLow = true;
		for (const FVector& V : FoamMesh.Verts) { bLow &= V.Z <= 25.0 && FVector2D(V.X, V.Y).Size() <= 140.0; }
		TestTrue(TEXT("La espuma va a ras de suelo"), bLow);
	}
	TNProcMesh::FTNProcMeshBuffers Post;
	TNTctMesh::BuildPost(Post);
	TestFalse(TEXT("El poste tiene malla"), Post.IsEmpty());
	for (const FVector& V : Post.Verts) { TestTrue(TEXT("El poste queda bajo su lámpara"), V.Z <= TNTctMesh::PostLampHeight); }

	// El actor del aviso: con la cota y el avance forzados, enseña marcas, más según crece y ninguna sin avance.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNTct920ShoreWorld"));
	if (!TestNotNull(TEXT("Mundo de prueba"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ATN_TctShoreWarning* Warning = World->SpawnActor<ATN_TctShoreWarning>(ATN_TctShoreWarning::StaticClass(), FTransform::Identity, Params))
	{
		TSharedPtr<FHeightField> Shared = MakeShared<FHeightField>(MakeField(SlopeGround));
		if (Warning->Init(Shared, Zones, 0x920u))
		{
			Warning->DebugSet(Target, 0.f, 20.f);
			Warning->Tick(0.f);
			TestEqual(TEXT("Sin avance, sin marcas"), Warning->GetNumShown(), 0);
			Warning->DebugSet(Target, 0.3f, 14.f);
			Warning->Tick(0.f);
			const int32 Early = Warning->GetNumShown();
			TestTrue(TEXT("A los 14 s ya asoman"), Early > 0);
			TestEqual(TEXT("Marca la cota del escalón"), Warning->GetTargetZ(), Target);
			Warning->DebugSet(Target, 1.f, 0.f);
			Warning->Tick(0.f);
			TestTrue(TEXT("Y crecen hasta la subida"), Warning->GetNumShown() > Early);
			Warning->DebugSet(Target, 0.f, 24.f);
			Warning->Tick(0.f);
			TestEqual(TEXT("Pasada la subida, se retiran"), Warning->GetNumShown(), 0);
		}
		else
		{
			AddInfo(TEXT("Sin pantalla (servidor o sin render): el actor del aviso no se monta, solo se prueba la lógica."));
		}
		Warning->Destroy();
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
