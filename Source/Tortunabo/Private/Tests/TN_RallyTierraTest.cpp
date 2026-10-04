// Circuito de tierra generado (#682, Docs/Rally_Circuitos_Vueltas.md): R02_circuito_tierra se construye como circuito por
// vueltas con sus cuatro saltos (doble, cresta, mesa y salto largo sobre hueco) para el copiloto y el piloto IA; los
// elementos nuevos del manifest (baches, badén, banqueta) se leen como «otros» y no frenan a la IA. Construye la pista en un
// mundo vacío (solo editor: Scripts/ no se empaqueta). Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Tierra; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Rally/TN_RallyCircuit.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyPaceNotes.h"
#include "Rally/TN_RallyTrack.h"
#include "Rally/TN_RallyTrackDressing.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyTierraTest
{
	const TCHAR* const Variant = TEXT("R02_circuito_tierra");

	struct FScopedWorld
	{
		UWorld* World = nullptr;

		FScopedWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNRallyTierraTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FScopedWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTierraR02Test, "Tortunabo.Rally.Tierra.R02BuildsWithJumpsAndDirtElements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTNRallyTierraR02Test::RunTest(const FString& Parameters)
{
	using namespace TNRallyCircuit;
	const FName Variant(TNRallyTierraTest::Variant);
	FString ManifestText;
	if (!TestTrue(TEXT("manifest de R02"), FFileHelper::LoadFileToString(ManifestText, *TNRally::VariantManifestPath(Variant))))
	{
		return false;
	}
	TNRally::FTrackSource Source;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("manifest válido (%s)"), *Error), TNRally::ParseTrackManifest(ManifestText, Source, Error)))
	{
		return false;
	}
	TestTrue(TEXT("circuito cerrado"), TNRally::IsCircuit(Source));
	TestEqual(TEXT("un peralte por punto de road_uu"), Source.RoadBankDeg.Num(), Source.Road.Num());

	TNRallyTierraTest::FScopedWorld Scoped;
	if (!TestNotNull(TEXT("mundo de prueba"), Scoped.World))
	{
		return false;
	}
	ATN_RallyTrack* Track = Scoped.World->SpawnActor<ATN_RallyTrack>();
	if (!TestNotNull(TEXT("pista"), Track) || !TestTrue(TEXT("la pista de R02 se construye"), Track->BuildFromVariant(Variant)))
	{
		return false;
	}
	TestTrue(TEXT("es un circuito por vueltas"), Track->IsCircuit());
	TestEqual(TEXT("vueltas del manifest"), Track->GetManifestLaps(), 3);
	TestTrue(TEXT("al menos 9 puertas"), Track->GetGateCount() >= 9);

	const TArray<FFeatureArc>& Features = Track->GetFeatures();
	int32 Jumps = 0;
	int32 Hairpins = 0;
	int32 Others = 0;
	for (const FFeatureArc& Feature : Features)
	{
		Jumps += Feature.Kind == EElementKind::Jump ? 1 : 0;
		Hairpins += Feature.Kind == EElementKind::Hairpin ? 1 : 0;
		Others += Feature.Kind == EElementKind::Other ? 1 : 0;
	}
	TestEqual(TEXT("4 saltos (doble, cresta, mesa y hueco)"), Jumps, 4);
	TestEqual(TEXT("2 horquillas con banqueta"), Hairpins, 2);
	TestTrue(TEXT("baches, badén y banquetas como elementos sin frenada (al menos 5)"), Others >= 5);

	// El piloto IA llega a cada labio a la velocidad de diseño por el factor; los demás elementos no frenan.
	const FBrakeTuning Tuning;
	const double Length = Track->GetTrackLengthCm();
	for (const FFeatureArc& Feature : Features)
	{
		if (Feature.Kind == EElementKind::Jump)
		{
			const FVector Lip = Track->GetLocationAtArc(Feature.KeyCm);
			TestTrue(TEXT("el labio está más alto que 30 m antes"), Lip.Z > Track->GetLocationAtArc(Feature.KeyCm - 3000.0).Z);
			const float AtLip = FeatureSpeedLimitKmh(Features, Feature.KeyCm, Length, true, 30000.0, 600.0, 200.f, Tuning);
			TestTrue(*FString::Printf(TEXT("en el labio, %.0f km/h <= diseño x factor"), AtLip),
				AtLip <= Feature.DesignKmh * Tuning.JumpLipSpeedFactor + 0.5);
		}
	}
	TArray<FFeatureArc> OnlyOthers;
	for (const FFeatureArc& Feature : Features)
	{
		if (Feature.Kind == EElementKind::Other)
		{
			OnlyOthers.Add(Feature);
		}
	}
	if (OnlyOthers.Num() > 0)
	{
		TestEqual(TEXT("baches y badén no frenan a la IA"),
			FeatureSpeedLimitKmh(OnlyOthers, OnlyOthers[0].KeyCm, Length, true, 30000.0, 600.0, 200.f, Tuning), 200.f);
	}

	const TNRallyPaceNotes::FTrackNotes Notes = TNRallyPaceNotes::BuildForTrack(*Track);
	int32 JumpNotes = 0;
	for (const TNRallyPaceNotes::FPaceNote& Note : Notes.Notes)
	{
		JumpNotes += Note.Kind == TNRallyPaceNotes::ENoteKind::Jump ? 1 : 0;
	}
	TestEqual(TEXT("una nota de salto por salto"), JumpNotes, 4);

	const TNRallyDressing::FTrackData Data = TNRallyDressing::SampleTrack(*Track, 400.0);
	const TNRallyDressing::FBarrierPlan Plan = TNRallyDressing::PlanBarriers(Data, TArray<uint8>(), TNRallyDressing::FBarrierParams());
	for (int32 Side = TNRallyDressing::LeftSide; Side <= TNRallyDressing::RightSide; ++Side)
	{
		int32 TooWide = 0;
		for (const double Gap : TNRallyDressing::BarrierGapsCm(Data, Plan, Side))
		{
			TooWide += Gap > TNRallyDressing::TurtleWidthCm ? 1 : 0;
		}
		TestEqual(*FString::Printf(TEXT("barrera %d sin huecos de más de una tortuga"), Side), TooWide, 0);
	}
	Track->ClearTrack();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
