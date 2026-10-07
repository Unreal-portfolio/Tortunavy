// Colocación del bloque "placements" del manifest al cargar un mapa de terreno fijo (#652): lectura y clasificación
// (TN_MapPlacements.h) y, con un mundo de juego sin terreno, lo que coloca ATN_MapPlacementSpawner con el manifest de
// prueba Scripts/tests/fixtures/manifest_placements.json (una entrada de cada pieza), contado por clase.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.World.MapPlacements; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/ProcMap/TN_ProcSearchSpot.h"
#include "World/TN_MapPlacementSpawner.h"
#include "World/TN_MapPlacements.h"
#include "World/TN_MapVariantLoader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNMapPlacementsTestDetail
{
	const TCHAR* FixturePath = TEXT("Scripts/tests/fixtures/manifest_placements.json");
	const TCHAR* C01Path = TEXT("Scripts/terrain_volumes/Variants/C01_camino/manifest.json");

	TSharedPtr<FJsonObject> LoadJson(const FString& RelativePath)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectDir() / RelativePath)))
		{
			return nullptr;
		}
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
	}

	UWorld* CreateGameWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		return World;
	}

	void DestroyGameWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	int32 CountSpawn(const TNMapPlacements::FParseResult& Parsed, TNMapPlacements::ESpawn Spawn)
	{
		return Parsed.Placements.FilterByPredicate([Spawn](const TNMapPlacements::FPlacement& P) { return P.Spawn == Spawn; }).Num();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMapPlacementsParseTest,
	"Tortunabo.World.MapPlacements.Parse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMapPlacementsParseTest::RunTest(const FString& Parameters)
{
	using namespace TNMapPlacements;
	using namespace TNMapPlacementsTestDetail;

	// Clasificación.
	ETNBeachElement Element = ETNBeachElement::Coconut;
	TestTrue(TEXT("Rock es un elemento"), ElementFromName(TEXT("Rock"), Element) && Element == ETNBeachElement::Rock);
	TestFalse(TEXT("Dragon no es un elemento"), ElementFromName(TEXT("Dragon"), Element));
	TestFalse(TEXT("Count no es un elemento"), ElementFromName(TEXT("Count"), Element));
	TestTrue(TEXT("Algas de obstáculo → elemento replicado"), SpawnOf(TEXT("obstacle"), TEXT("Seaweed"), Element) == ESpawn::BeachElement);
	TestTrue(TEXT("Algas como decorado → sin pieza"), SpawnOf(TEXT("decor"), TEXT("Seaweed"), Element) == ESpawn::Unsupported);
	TestTrue(TEXT("Alambre (fuera del juego) → sin pieza"), SpawnOf(TEXT("obstacle"), TEXT("BarbedWire"), Element) == ESpawn::Unsupported);
	TestTrue(TEXT("Concha de puntos (fuera del juego) → sin pieza"), SpawnOf(TEXT("loot"), TEXT("ScoreShell"), Element) == ESpawn::Unsupported);
	TestTrue(TEXT("Pasarela de mecánica → decorado local"), SpawnOf(TEXT("mechanic"), TEXT("Boardwalk"), Element) == ESpawn::Decor);
	TestTrue(TEXT("Rebuscable de botín → su actor"), SpawnOf(TEXT("loot"), TEXT("SearchSpot"), Element) == ESpawn::SearchSpot);
	TestTrue(TEXT("Géiser (fuera del juego) → sin pieza"), SpawnOf(TEXT("mechanic"), TEXT("Geyser"), Element) == ESpawn::Unsupported);
	TestTrue(TEXT("Muro de lanzamiento (fuera del juego) → sin pieza"), SpawnOf(TEXT("puzzle"), TEXT("throw_chain"), Element) == ESpawn::Unsupported);
	TestTrue(TEXT("Charco de pesca → su actor"), SpawnOf(TEXT("loot"), TEXT("FishingPool"), Element) == ESpawn::FishingPool);
	TestEqual(TEXT("Charco de pesca: nombre en el registro"), FString(SpawnName(ESpawn::FishingPool)), FString(TEXT("FishingPool")));
	TestTrue(TEXT("Puzle pendiente → sin pieza"), SpawnOf(TEXT("puzzle"), TEXT("think_room"), Element) == ESpawn::Unsupported);

	// Reparto por la polilínea (en L: 10 m al este y 10 m al norte).
	FPlacement Bent;
	Bent.Path = { FVector(0.0, 0.0, 0.0), FVector(1000.0, 0.0, 0.0), FVector(1000.0, 1000.0, 0.0) };
	double Yaw = 0.0;
	TestTrue(TEXT("Mitad de la polilínea en el codo"), PointAlong(Bent, 0.5, Yaw).Equals(FVector(1000.0, 0.0, 0.0), 1.0));
	TestTrue(TEXT("Tres cuartos, en el segundo tramo y mirando al norte"),
		PointAlong(Bent, 0.75, Yaw).Equals(FVector(1000.0, 500.0, 0.0), 1.0) && FMath::IsNearlyEqual(Yaw, 90.0, 0.1));
	FPlacement Straight;
	Straight.Location = FVector(500.0, 0.0, 0.0);
	Straight.LengthCm = 1000.0;
	TestTrue(TEXT("Sin polilínea, en recta por su yaw"), PointAlong(Straight, 0.0, Yaw).Equals(FVector(0.0, 0.0, 0.0), 1.0));

	// Manifest de prueba.
	const TSharedPtr<FJsonObject> Fixture = LoadJson(FixturePath);
	if (!TestNotNull(TEXT("Manifest de prueba"), Fixture.Get()))
	{
		return false;
	}
	FParseResult Parsed;
	TestTrue(TEXT("Se lee"), ParseBlock(*Fixture, Parsed) && Parsed.bHasBlock);
	TestEqual(TEXT("Una sin location_uu, inválida"), Parsed.Invalid, 1);
	TestEqual(TEXT("Una suprimida"), Parsed.Suppressed, 1);
	TestEqual(TEXT("16 automáticas y 1 manual"), Parsed.Placements.Num(), 17);
	TestEqual(TEXT("Dos sin pieza (puzle pendiente y kind desconocido)"), CountSpawn(Parsed, ESpawn::Unsupported), 2);
	const FPlacement* Manual = Parsed.Placements.FindByPredicate([](const FPlacement& P) { return P.Source == TEXT("manual"); });
	TestTrue(TEXT("La manual, con id inventado y su elemento"), Manual && Manual->Id == TEXT("manual-0") && Manual->Element == ETNBeachElement::DragCrab);

	// Bloque desfasado: solo lo manual.
	const TSharedPtr<FJsonObject> Stale = LoadJson(FixturePath);
	Stale->GetObjectField(TEXT("placements"))->SetBoolField(TEXT("stale"), true);
	FParseResult StaleParsed;
	ParseBlock(*Stale, StaleParsed);
	TestTrue(TEXT("Desfasado: solo la manual"), StaleParsed.bStale && StaleParsed.Placements.Num() == 1 && StaleParsed.SkippedStale == 18);

	// Sin bloque no es un error; un bloque que no es un objeto, sí.
	FJsonObject Empty;
	FParseResult EmptyParsed;
	TestTrue(TEXT("Sin bloque: nada que colocar"), ParseBlock(Empty, EmptyParsed) && !EmptyParsed.bHasBlock);
	Empty.SetStringField(TEXT("placements"), TEXT("roto"));
	TestFalse(TEXT("Bloque roto: error"), ParseBlock(Empty, EmptyParsed));

	// C01 de verdad: todo lo que coloca el generador tiene pieza en el juego.
	const TSharedPtr<FJsonObject> C01 = LoadJson(C01Path);
	if (TestNotNull(TEXT("Manifest de C01"), C01.Get()))
	{
		FParseResult C01Parsed;
		TestTrue(TEXT("C01 se lee"), ParseBlock(*C01, C01Parsed) && C01Parsed.bHasBlock && !C01Parsed.bStale);
		TestEqual(TEXT("C01: 170 colocaciones"), C01Parsed.Placements.Num(), 170);
		TestEqual(TEXT("C01: ninguna sin pieza"), CountSpawn(C01Parsed, ESpawn::Unsupported), 0);
		TestEqual(TEXT("C01: ninguna inválida"), C01Parsed.Invalid, 0);
		const int32 WithoutProgress = C01Parsed.Placements.FilterByPredicate([](const FPlacement& P) { return P.ProgressM < 0.0; }).Num();
		TestEqual(TEXT("C01: todas traen progress_m"), WithoutProgress, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMapPlacementsSpawnTest,
	"Tortunabo.World.MapPlacements.Spawn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMapPlacementsSpawnTest::RunTest(const FString& Parameters)
{
	using namespace TNMapPlacementsTestDetail;
	const TSharedPtr<FJsonObject> Fixture = LoadJson(FixturePath);
	TNMapPlacements::FParseResult Parsed;
	if (!TestNotNull(TEXT("Manifest de prueba"), Fixture.Get()) || !TestTrue(TEXT("Se lee"), TNMapPlacements::ParseBlock(*Fixture, Parsed)))
	{
		return false;
	}
	const TNMapPlacements::FPlacement* Covered = Parsed.Placements.FindByPredicate(
		[](const TNMapPlacements::FPlacement& P) { return P.Id == TEXT("t-tapado"); });
	if (!TestNotNull(TEXT("Entrada tapada en el manifest"), Covered))
	{
		return false;
	}

	UWorld* World = CreateGameWorld();
	// Puesto a mano en el nivel a 1 m del cangrejo del manifest: el cangrejo no se coloca y el rebuscable sigue donde estaba.
	const FVector HandSpot = Covered->Location + FVector(100.0, 0.0, 0.0);
	ATN_ProcSearchSpot* HandPlaced = World->SpawnActor<ATN_ProcSearchSpot>(ATN_ProcSearchSpot::StaticClass(), FTransform(HandSpot));
	ATN_MapPlacementSpawner* Spawner = World->SpawnActor<ATN_MapPlacementSpawner>(ATN_MapPlacementSpawner::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("Rebuscable puesto a mano"), HandPlaced) || !TestNotNull(TEXT("Colocador"), Spawner))
	{
		DestroyGameWorld(World);
		return false;
	}
	// Como si viniera del nivel (ULevel::InitializeNetworkActors): lo replicado creado en partida no tapa nada.
	HandPlaced->bNetStartup = true;
	Spawner->Populate(Parsed, true, true);
	const FTNMapPlacementStats& Stats = Spawner->GetStats();

	// Elementos de la playa (replicados).
	TestEqual(TEXT("Erizo enterrado"), Stats.Actors(TEXT("TN_BeachUrchinSpikes")), 1);
	TestEqual(TEXT("Algas"), Stats.Actors(TEXT("TN_BeachSeaweed")), 1);
	TestEqual(TEXT("Trampolines (mecánica y el del camino que se rompe)"), Stats.Actors(TEXT("TN_BeachTrampoline")), 2);
	TestEqual(TEXT("Plataformas tambaleantes"), Stats.Actors(TEXT("TN_BeachWobblyPlatform")), 5);
	TestEqual(TEXT("Cangrejo arrastrador (manual)"), Stats.Actors(TEXT("TN_BeachDragCrab")), 1);
	TestEqual(TEXT("Cangrejo tapado por el nivel"), Stats.Actors(TEXT("TN_BeachBurrowCrab")), 0);
	// Resto de piezas.
	TestEqual(TEXT("Rebuscable"), Stats.Actors(TEXT("TN_ProcSearchSpot")), 1);
	TestEqual(TEXT("Placas"), Stats.Actors(TEXT("TN_PressurePlate")), 3);
	TestEqual(TEXT("Gestor de las placas"), Stats.Actors(TEXT("TN_PressurePlateGroupManager")), 1);
	TestEqual(TEXT("Plataformas que se rompen"), Stats.Actors(TEXT("TN_BreakablePlatform")), 6);
	// Local.
	TestEqual(TEXT("Decorado (pasarela, roca y estrella)"), Stats.DecorItems, 3);
	TestEqual(TEXT("Vegetación"), Stats.VegetationInstances, 3);
	// Lo que no se coloca.
	TestEqual(TEXT("Tapadas por el nivel"), Stats.SkippedByLevel, 1);
	TestEqual(TEXT("Sin pieza"), Stats.Unsupported, 2);
	TestEqual(TEXT("Fallidas"), Stats.Failed, 0);
	TestEqual(TEXT("Falta la puerta de plate_balance"), Stats.MissingPieces, 1);
	TestTrue(TEXT("El rebuscable puesto a mano sigue en su sitio"), IsValid(HandPlaced) && HandPlaced->GetActorLocation().Equals(HandSpot, 1.0));

	// Una segunda pasada no duplica nada.
	const int32 Before = Stats.ActorsByClass.Num();
	Spawner->Populate(Parsed, true, true);
	TestEqual(TEXT("Populate solo una vez"), Spawner->GetStats().ActorsByClass.Num(), Before);

	DestroyGameWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMapPlacementsClientTest,
	"Tortunabo.World.MapPlacements.ClientOnlyLocal",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMapPlacementsClientTest::RunTest(const FString& Parameters)
{
	using namespace TNMapPlacementsTestDetail;
	const TSharedPtr<FJsonObject> Fixture = LoadJson(FixturePath);
	TNMapPlacements::FParseResult Parsed;
	if (!TestNotNull(TEXT("Manifest de prueba"), Fixture.Get()) || !TestTrue(TEXT("Se lee"), TNMapPlacements::ParseBlock(*Fixture, Parsed)))
	{
		return false;
	}
	UWorld* World = CreateGameWorld();
	ATN_MapPlacementSpawner* Spawner = World->SpawnActor<ATN_MapPlacementSpawner>(ATN_MapPlacementSpawner::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("Colocador"), Spawner))
	{
		DestroyGameWorld(World);
		return false;
	}
	// Como un cliente: lo replicado lo manda el servidor; aquí solo lo local.
	Spawner->Populate(Parsed, false, true);
	const FTNMapPlacementStats& Stats = Spawner->GetStats();
	int32 Actors = 0;
	for (const TPair<FName, int32>& Pair : Stats.ActorsByClass)
	{
		Actors += Pair.Value;
	}
	TestEqual(TEXT("Cliente: ningún actor"), Actors, 0);
	TestEqual(TEXT("Cliente: el decorado igual que el servidor"), Stats.DecorItems, 3);
	TestEqual(TEXT("Cliente: la vegetación"), Stats.VegetationInstances, 3);
	DestroyGameWorld(World);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// C01 de verdad: el cargador construye el terreno, lee el bloque y lo coloca todo con la cota del terreno.
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMapPlacementsC01Test,
	"Tortunabo.World.MapPlacements.C01",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMapPlacementsC01Test::RunTest(const FString& Parameters)
{
	using namespace TNMapPlacementsTestDetail;
	UWorld* World = CreateGameWorld();
	FActorSpawnParameters Params;
	Params.bDeferConstruction = true;
	ATN_MapVariantLoader* Loader = World->SpawnActor<ATN_MapVariantLoader>(ATN_MapVariantLoader::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Cargador"), Loader))
	{
		DestroyGameWorld(World);
		return false;
	}
	Loader->Variant = TEXT("C01_camino");
	Loader->FinishSpawning(FTransform::Identity);

	const ATN_MapPlacementSpawner* Spawner = nullptr;
	for (TActorIterator<ATN_MapPlacementSpawner> It(World); It; ++It)
	{
		Spawner = *It;
	}
	if (!TestNotNull(TEXT("El cargador crea el colocador"), Spawner))
	{
		DestroyGameWorld(World);
		return false;
	}
	const FTNMapPlacementStats& Stats = Spawner->GetStats();
	TestEqual(TEXT("C01: ninguna fallida"), Stats.Failed, 0);
	TestEqual(TEXT("C01: ninguna sin pieza"), Stats.Unsupported, 0);
	TestEqual(TEXT("C01: nada tapado (sin nada puesto a mano)"), Stats.SkippedByLevel, 0);
	TestEqual(TEXT("C01: 109 piezas de decorado"), Stats.DecorItems, 109);
	TestEqual(TEXT("C01: 49 matas"), Stats.VegetationInstances, 49);
	TestEqual(TEXT("C01: 8 plataformas que se rompen"), Stats.Actors(TEXT("TN_BreakablePlatform")), 8);
	// La cota sale del terreno, no del manifest: casi todas las trazas dan con él (alguna del río puede quedar fuera).
	const int32 Traces = Stats.GroundHits + Stats.GroundMisses;
	AddInfo(FString::Printf(TEXT("C01: cota del terreno en %d de %d trazas."), Stats.GroundHits, Traces));
	TestTrue(TEXT("C01: al menos el 95 % de las trazas dan con el terreno"), Traces > 0 && Stats.GroundHits * 100 >= Traces * 95);
	DestroyGameWorld(World);
	return true;
}

#endif
