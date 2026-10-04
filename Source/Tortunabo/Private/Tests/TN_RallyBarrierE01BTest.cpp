// Barrera del trazado de E01B (#303, director, 03-10): de neumáticos, continua a los dos lados, pegada al borde de la calzada
// (road_width_m del manifest) y apoyada en el suelo.
//  - BarrierHasNoGaps: pista del manifest de E01B_espana_rally en un mundo vacío (ATN_RallyTrack::BuildFromVariant), muestreada
//    como el decorado (TNRallyDressing::SampleTrack); ningún hueco de la barrera planificada más ancho que una tortuga.
//  - TiresOnGround: con el terreno de la variante (ATN_MapVariantLoader) y el decorado construido como en la carrera, ninguna
//    pila de neumáticos a más de 5 cm por encima del suelo y ningún hueco entre pilas más ancho que una tortuga.
// Solo editor: Scripts/ no se empaqueta. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Dressing.E01B; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyTrack.h"
#include "Rally/TN_RallyTrackDressing.h"
#include "World/TN_MapVariantLoader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyBarrierE01BTest
{
	const TCHAR* const VariantName = TEXT("E01B_espana_rally");
	/** El mismo paso que ATN_RallyTrackDressing::SampleStepCm por defecto. */
	constexpr double SampleStepCm = 400.0;
	/** ATN_RallyTrackDressing::TireDiameterCm por defecto. */
	constexpr double TireDiameterCm = 120.0;
	/** Lo más que puede quedar la cara de abajo de una pila por encima del suelo (cm). */
	constexpr double MaxAboveGroundCm = 5.0;
	/** road_width_m de E01B (m). */
	constexpr double RoadWidthM = 14.0;

	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNRallyBarrierE01BTestWorld"));
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyBarrierE01BTest, "Tortunabo.Rally.Dressing.E01B.BarrierHasNoGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyBarrierE01BTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressing;
	const FString Manifest = TNRally::VariantManifestPath(FName(TNRallyBarrierE01BTest::VariantName));
	if (!TestTrue(*FString::Printf(TEXT("Manifest de E01B en %s"), *Manifest), FPaths::FileExists(Manifest)))
	{
		return false;
	}
	TNRallyBarrierE01BTest::FScopedTestWorld Scoped;
	if (!TestNotNull(TEXT("Mundo de prueba"), Scoped.World))
	{
		return false;
	}
	ATN_RallyTrack* Track = Scoped.World->SpawnActor<ATN_RallyTrack>();
	if (!TestNotNull(TEXT("Pista"), Track) || !TestTrue(TEXT("La pista de E01B se construye"), Track->BuildFromVariant(FName(TNRallyBarrierE01BTest::VariantName))))
	{
		return false;
	}
	const FTrackData Data = SampleTrack(*Track, TNRallyBarrierE01BTest::SampleStepCm);
	Track->ClearTrack();
	if (!TestTrue(TEXT("Trazado muestreado"), Data.Samples.Num() >= 3))
	{
		return false;
	}
	TestEqual(TEXT("El ancho de la calzada sale de road_width_m del manifest"), Data.RoadWidthCm, TNRallyBarrierE01BTest::RoadWidthM * 100.0, 0.01);
	// Sin terreno no hay sondas de caída: las caídas solo acercan la barrera al borde, no la abren ni la cierran.
	const FBarrierPlan Plan = PlanBarriers(Data, TArray<uint8>(), FBarrierParams());
	for (int32 Side = LeftSide; Side <= RightSide; ++Side)
	{
		const TCHAR* SideName = Side == LeftSide ? TEXT("izquierda") : TEXT("derecha");
		const TArray<double> Gaps = BarrierGapsCm(Data, Plan, Side);
		double Widest = 0.0;
		int32 TooWide = 0;
		for (const double Gap : Gaps)
		{
			Widest = FMath::Max(Widest, Gap);
			TooWide += Gap > TurtleWidthCm ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("E01B, barrera %s: %d tramos, %d huecos, el mayor de %.0f cm (%.1f km de trazado)."), SideName,
			Plan.Sides[Side].Runs.Num(), Gaps.Num(), Widest, Data.LengthCm / 100000.0));
		TestEqual(*FString::Printf(TEXT("E01B, barrera %s: huecos más anchos que una tortuga (%.0f cm)"), SideName, TurtleWidthCm), TooWide, 0);
	}
	// Pegada al borde: media calzada más el radio de la pila y la holgura, en todo el trazado (salvo junto a otro tramo).
	TestEqual(TEXT("E01B: la barrera va a media calzada + su margen"), Plan.BaseOffsetCm, 0.5 * Data.RoadWidthCm + FBarrierParams().RoadEdgeMarginCm, 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTiresOnGroundE01BTest, "Tortunabo.Rally.Dressing.E01B.TiresOnGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyTiresOnGroundE01BTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyDressing;
	const FName Variant(TNRallyBarrierE01BTest::VariantName);
	if (!TestTrue(TEXT("Manifest de E01B"), FPaths::FileExists(TNRally::VariantManifestPath(Variant))))
	{
		return false;
	}
	TNRallyBarrierE01BTest::FScopedTestWorld Scoped;
	UWorld* World = Scoped.World;
	if (!TestNotNull(TEXT("Mundo de prueba"), World))
	{
		return false;
	}
	// El terreno de la variante, como ATN_RallyGameState::PrepareTrack: la variante puesta antes de OnConstruction.
	ATN_MapVariantLoader* Loader = World->SpawnActorDeferred<ATN_MapVariantLoader>(ATN_MapVariantLoader::StaticClass(), FTransform::Identity,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Cargador del terreno"), Loader))
	{
		return false;
	}
	Loader->Variant = Variant;
	Loader->FinishSpawning(FTransform::Identity);
	ATN_RallyTrack* Track = World->SpawnActor<ATN_RallyTrack>();
	if (!TestNotNull(TEXT("Pista"), Track) || !TestTrue(TEXT("La pista de E01B se construye"), Track->BuildFromVariant(Variant)))
	{
		return false;
	}
	ATN_RallyTrackDressing* Dressing = World->SpawnActor<ATN_RallyTrackDressing>();
	if (!TestNotNull(TEXT("Decorado"), Dressing)
		|| !TestTrue(TEXT("El decorado se construye"), Dressing->BuildFromTrack(Track, static_cast<int32>(FCrc::StrCrc32(*Variant.ToString())))))
	{
		return false;
	}
	const FTrackData Data = SampleTrack(*Track, TNRallyBarrierE01BTest::SampleStepCm);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(RallyTiresOnGroundTest), true);
	Query.AddIgnoredActor(Dressing);
	Query.AddIgnoredActor(Track);
	for (int32 Side = LeftSide; Side <= RightSide; ++Side)
	{
		const TCHAR* SideName = Side == LeftSide ? TEXT("izquierda") : TEXT("derecha");
		const TArray<FVector>& Bases = Dressing->GetTireStackBases(Side);
		// Una pila cada 1,02 diámetros a lo largo de todo el trazado (la barrera va algo más larga por fuera de las curvas).
		const int32 Expected = FMath::FloorToInt32(Data.LengthCm / (1.02 * TNRallyBarrierE01BTest::TireDiameterCm));
		TestTrue(*FString::Printf(TEXT("E01B, %s: pilas de neumáticos en todo el trazado (%d, al menos %d)"), SideName, Bases.Num(), Expected * 9 / 10),
			Bases.Num() >= Expected * 9 / 10);
		int32 TooHigh = 0;
		int32 OnWater = 0;
		int32 NoGround = 0;
		double Highest = -TNumericLimits<double>::Max();
		for (const FVector& Base : Bases)
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByObjectType(Hit, Base + FVector(0.0, 0.0, 800.0), Base - FVector(0.0, 0.0, 3000.0),
				FCollisionObjectQueryParams(ECC_WorldStatic), Query))
			{
				++NoGround;
				continue;
			}
			if (Data.bHasWater && Hit.ImpactPoint.Z <= Data.WaterZ + 10.0)
			{
				++OnWater;
				continue;
			}
			const double Above = Base.Z - Hit.ImpactPoint.Z;
			Highest = FMath::Max(Highest, Above);
			TooHigh += Above > TNRallyBarrierE01BTest::MaxAboveGroundCm ? 1 : 0;
		}
		int32 TooWide = 0;
		double Widest = 0.0;
		for (int32 Index = 1; Index < Bases.Num(); ++Index)
		{
			const double Gap = FVector::Dist2D(Bases[Index - 1], Bases[Index]) - TNRallyBarrierE01BTest::TireDiameterCm;
			Widest = FMath::Max(Widest, Gap);
			TooWide += Gap > TurtleWidthCm ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("E01B, neumáticos %s: %d pilas, la más alta a %.1f cm del suelo, %d sobre el agua, hueco mayor %.0f cm."),
			SideName, Bases.Num(), Highest, OnWater, Widest));
		TestEqual(*FString::Printf(TEXT("E01B, %s: pilas a más de %.0f cm por encima del suelo"), SideName, TNRallyBarrierE01BTest::MaxAboveGroundCm),
			TooHigh, 0);
		TestEqual(*FString::Printf(TEXT("E01B, %s: pilas sin suelo debajo"), SideName), NoGround, 0);
		TestEqual(*FString::Printf(TEXT("E01B, %s: huecos entre pilas más anchos que una tortuga (%.0f cm)"), SideName, TurtleWidthCm), TooWide, 0);
	}
	Dressing->ClearDressing();
	Track->ClearTrack();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
