// Bordes de la pista homogéneos (#666): con el estilo de siempre (neumáticos, BarrierStyles = {Tires}), las pilas se reparten con
// la misma separación a lo largo de todo el borde de cada tramo, sin trocearlo por ChunkPolyline, en los circuitos generados
// (R01, R02), E01B e I03R. Pista del manifest en un mundo vacío (ATN_RallyTrack::BuildFromVariant), muestreada y planificada
// como el decorado (TNRallyDressing::SampleTrack, PlanBarriers, RunEdge, BarrierSections y ResamplePolyline).
// Solo editor: Scripts/ no se empaqueta. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Dressing.Spacing; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyTrack.h"
#include "Rally/TN_RallyTrackDressing.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyBarrierSpacingTest
{
	/** ATN_RallyTrackDressing: SampleStepCm, TireDiameterCm (la separación es 1,02 diámetros) y StyleSectionCm por defecto. */
	constexpr double SampleStepCm = 400.0;
	constexpr double TireSpacingCm = 120.0 * 1.02;
	constexpr double StyleSectionCm = 12000.0;
	/** Diferencia de separación (cm) que se admite entre dos pilas del mismo tramo: redondeo, nada más. */
	constexpr double SpacingToleranceCm = 0.01;

	const TCHAR* const Variants[] = { TEXT("R01_circuito_dunas"), TEXT("R02_circuito_tierra"), TEXT("E01B_espana_rally"), TEXT("I03R_tortuga_magna") };

	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNRallyBarrierSpacingTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** Mayor diferencia de separación entre las pilas de los trozos, como si fueran un solo borde. */
	double SpacingSpread(const TArray<TArray<FVector>>& Sections)
	{
		double Lowest = TNumericLimits<double>::Max();
		double Highest = 0.0;
		for (const TArray<FVector>& Section : Sections)
		{
			for (const TNRallyDressing::FPolySpot& Spot : TNRallyDressing::ResamplePolyline(Section, TireSpacingCm))
			{
				Lowest = FMath::Min(Lowest, Spot.SeparationCm);
				Highest = FMath::Max(Highest, Spot.SeparationCm);
			}
		}
		return Highest >= Lowest ? Highest - Lowest : 0.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyBarrierSpacingTest, "Tortunabo.Rally.Dressing.Spacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyBarrierSpacingTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressing;
	using namespace TNRallyBarrierSpacingTest;
	FScopedTestWorld Scoped;
	if (!TestNotNull(TEXT("Mundo de prueba"), Scoped.World))
	{
		return false;
	}
	for (const TCHAR* VariantName : Variants)
	{
		const FName Variant(VariantName);
		if (!TestTrue(*FString::Printf(TEXT("Manifest de %s"), VariantName), FPaths::FileExists(TNRally::VariantManifestPath(Variant))))
		{
			continue;
		}
		ATN_RallyTrack* Track = Scoped.World->SpawnActor<ATN_RallyTrack>();
		if (!TestNotNull(TEXT("Pista"), Track) || !TestTrue(*FString::Printf(TEXT("La pista de %s se construye"), VariantName), Track->BuildFromVariant(Variant)))
		{
			continue;
		}
		const FTrackData Data = SampleTrack(*Track, SampleStepCm);
		Track->ClearTrack();
		Track->Destroy();
		const FBarrierPlan Plan = PlanBarriers(Data, TArray<uint8>(), FBarrierParams());
		int32 Runs = 0;
		int32 LongRuns = 0;
		double WorstSpread = 0.0;
		double WorstChunkedSpread = 0.0;
		for (int32 Side = LeftSide; Side <= RightSide; ++Side)
		{
			for (const TArray<int32>& Run : Plan.Sides[Side].Runs)
			{
				const TArray<FVector> Edge = RunEdge(Data, Plan.Sides[Side], Run, Side);
				if (Edge.Num() < 2)
				{
					continue;
				}
				++Runs;
				const TArray<TArray<FVector>> Sections = BarrierSections(Edge, 1, StyleSectionCm);
				TestEqual(*FString::Printf(TEXT("%s: con un solo estilo, el tramo no se trocea"), VariantName), Sections.Num(), 1);
				WorstSpread = FMath::Max(WorstSpread, SpacingSpread(Sections));
				// Caso negativo: troceado como antes (varios estilos), un tramo largo sale con separaciones distintas.
				const TArray<TArray<FVector>> Chunked = BarrierSections(Edge, 2, StyleSectionCm);
				if (Chunked.Num() > 1)
				{
					++LongRuns;
					WorstChunkedSpread = FMath::Max(WorstChunkedSpread, SpacingSpread(Chunked));
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%s: %d tramos de barrera; diferencia de separación %.4f cm (troceado: %.2f cm)."), VariantName, Runs,
			WorstSpread, WorstChunkedSpread));
		TestTrue(*FString::Printf(TEXT("%s: hay barrera"), VariantName), Runs > 0);
		TestTrue(*FString::Printf(TEXT("%s: misma separación entre pilas en cada tramo (%.4f cm)"), VariantName, WorstSpread),
			WorstSpread <= SpacingToleranceCm);
		if (LongRuns > 0)
		{
			TestTrue(*FString::Printf(TEXT("%s: troceado, la separación varía (la prueba lo detecta)"), VariantName),
				WorstChunkedSpread > SpacingToleranceCm);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
