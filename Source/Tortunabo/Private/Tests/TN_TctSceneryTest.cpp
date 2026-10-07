// Decorado vivo de las arenas de Todos contra Todos (#829): el reparto sobre una arena inventada (meseta, puente y rampa) y sobre la
// de verdad (A01_diana): determinista, sin tapar salidas, puntos de objetos ni caminos, y con colisión solo donde sobra sitio.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tct.Scenery; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "World/Beach/TN_BeachTypes.h"
#include "World/ProcMap/TN_ProcMapFlora.h"
#include "World/TN_TctArena.h"
#include "World/TN_TctScenery.h"
#include "World/TN_TctSceneryPlan.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTctSceneryTest
{
	/** Meseta de 120 m a +2 m, un puente de 8 m al este y una rampa de 15° de 30 m al oeste; el resto, vacío. */
	bool SyntheticGround(double X, double Y, float& OutZ)
	{
		const double Ax = FMath::Abs(X);
		const double Ay = FMath::Abs(Y);
		if (Ax <= 6000.0 && Ay <= 6000.0) { OutZ = 200.f; return true; }
		if (X > 6000.0 && X <= 9000.0 && Ay <= 400.0) { OutZ = 200.f; return true; }
		if (X < -6000.0 && X >= -9000.0 && Ay <= 3000.0) { OutZ = 200.f - static_cast<float>((-6000.0 - X) * 0.2679); return true; }
		return false;
	}

	TNTctScenery::FHeightField MakeField()
	{
		TNTctScenery::FHeightField Field;
		Field.Build(FVector2D(-10000.0, -10000.0), FVector2D(10000.0, 10000.0), 100.0, SyntheticGround);
		Field.ComputeOpen(10.f, 3500.f);
		return Field;
	}

	TArray<TNTctScenery::FKeepOut> KeepOuts()
	{
		TArray<TNTctScenery::FKeepOut> Zones;
		Zones.Add({ FVector2D(0.0, 0.0), 900.f });
		Zones.Add({ FVector2D(4000.0, 4000.0), 700.f });
		Zones.Add({ FVector2D(-4000.0, 3000.0), 900.f });
		return Zones;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctSceneryPlanTest,
	"Tortunabo.Tct.Scenery.Plan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctSceneryPlanTest::RunTest(const FString& Parameters)
{
	using namespace TNTctScenery;
	using namespace TNTctSceneryTest;
	const FHeightField Field = MakeField();
	const TArray<FKeepOut> Zones = KeepOuts();
	TestTrue(TEXT("Suelo medido"), Field.IsValid());
	TestTrue(TEXT("La meseta es abierta; la rampa, no"), Field.OpenAt(FVector2D(0.0, 0.0)) > 2000.f && Field.OpenAt(FVector2D(-7500.0, 0.0)) == 0.f);
	TestEqual(TEXT("El puente (8 m) apenas tiene sitio libre"), Field.OpenAt(FVector2D(7500.0, 0.0)) < 500.f, true);

	const FPlan Plan = MakePlan(Field, -400.f, Zones, ETNProcBiome::Beach, 12345u);
	AddInfo(FString::Printf(TEXT("Meseta de prueba: %d plantas, %d piezas con colisión, %d anclas de fauna, huella %08X"),
		Plan.Flora.Num(), Plan.Decor.Num(), Plan.Fauna.Num(), Plan.Fingerprint));
	TestTrue(TEXT("Hay vegetación"), Plan.Flora.Num() > 300);
	TestTrue(TEXT("Hay decorado con colisión"), Plan.Decor.Num() >= 3);
	TestTrue(TEXT("Hay fauna"), Plan.Fauna.Num() > 20);

	// Misma entrada, mismo reparto (también si se rehace el suelo medido).
	const FPlan Again = MakePlan(MakeField(), -400.f, Zones, ETNProcBiome::Beach, 12345u);
	TestEqual(TEXT("Misma huella"), Again.Fingerprint, Plan.Fingerprint);
	TestEqual(TEXT("Mismas plantas"), Again.Flora.Num(), Plan.Flora.Num());
	TestEqual(TEXT("Mismas piezas"), Again.Decor.Num(), Plan.Decor.Num());
	TestEqual(TEXT("Mismos animales"), Again.Fauna.Num(), Plan.Fauna.Num());
	TestTrue(TEXT("Otra semilla, otro reparto"), MakePlan(Field, -400.f, Zones, ETNProcBiome::Beach, 777u).Fingerprint != Plan.Fingerprint);
	TestTrue(TEXT("Otra variante, otra semilla"), MakeSeed(3001u, FName(TEXT("A01_diana")), 5u) != MakeSeed(3001u, FName(TEXT("A02_donut")), 5u));
	TestEqual(TEXT("La semilla no depende de nada más"), MakeSeed(3001u, FName(TEXT("A01_diana")), 5u), MakeSeed(3001u, FName(TEXT("A01_diana")), 5u));

	// Lo que tiene colisión: solo donde sobra sitio, lejos de salidas y puntos de objetos, y sin pegarse entre sí.
	for (int32 Index = 0; Index < Plan.Decor.Num(); ++Index)
	{
		const FDecorPick& Pick = Plan.Decor[Index];
		const FVector2D P(Pick.Location.X, Pick.Location.Y);
		const double Radius = TNBeach::FootprintRadius(Pick.Element) * Pick.Scale;
		TestTrue(TEXT("Dentro de la meseta, no en el puente ni en la rampa"), FMath::Abs(P.X) < 6000.0 && FMath::Abs(P.Y) < 6000.0);
		TestTrue(TEXT("Con sitio de sobra alrededor (6 m de paso como mínimo)"), Field.OpenAt(P) >= Radius + 600.0);
		TestTrue(TEXT("Lejos de las salidas y los puntos de objetos"), KeepOutDistance(Zones, P) >= Radius + 240.0);
		TestTrue(TEXT("Sobre el suelo"), FMath::IsNearlyEqual(Pick.Location.Z, 200.0, 1.0));
		for (int32 Other = Index + 1; Other < Plan.Decor.Num(); ++Other)
		{
			const double OtherRadius = TNBeach::FootprintRadius(Plan.Decor[Other].Element) * Plan.Decor[Other].Scale;
			TestTrue(TEXT("Con paso entre dos piezas"), FVector2D::Distance(P, FVector2D(Plan.Decor[Other].Location.X, Plan.Decor[Other].Location.Y)) >= Radius + OtherRadius + 400.0);
		}
	}

	// Las plantas: sobre suelo, nunca en una salida o un punto de objetos, ni en la rampa; solo de los dos biomas de la arena.
	int32 OffGround = 0;
	int32 InKeepOut = 0;
	int32 OnRamp = 0;
	int32 WrongBiome = 0;
	for (const TNProcMap::FFloraInstance& Instance : Plan.Flora)
	{
		const FVector2D P(Instance.Location.X, Instance.Location.Y);
		float Z = 0.f;
		OffGround += Field.HeightAt(P, Z) ? 0 : 1;
		InKeepOut += KeepOutDistance(Zones, P) < 0.0 ? 1 : 0;
		OnRamp += (P.X < -6000.0 && P.X > -9000.0) ? 1 : 0;
		WrongBiome += (Instance.Biome != TNProcMap::BiomeIndex(ETNProcBiome::Beach) && Instance.Biome != TNProcMap::BiomeIndex(SecondaryBiome(ETNProcBiome::Beach))) ? 1 : 0;
	}
	TestEqual(TEXT("Ninguna planta sobre el vacío"), OffGround, 0);
	TestEqual(TEXT("Ninguna sobre una salida ni un punto de objetos"), InKeepOut, 0);
	TestEqual(TEXT("Ninguna sobre la rampa"), OnRamp, 0);
	TestEqual(TEXT("Solo los biomas de la arena"), WrongBiome, 0);
	for (const FFaunaAnchor& Anchor : Plan.Fauna)
	{
		float Z = 0.f;
		TestTrue(TEXT("Anclas de fauna sobre suelo y fuera de los sitios libres"), Field.HeightAt(Anchor.P, Z) && KeepOutDistance(Zones, Anchor.P) >= 0.0);
	}

	// Cada arena, su bioma.
	TestEqual(TEXT("Diana: playa"), PrimaryBiome(FName(TEXT("A01_diana"))), ETNProcBiome::Beach);
	TestEqual(TEXT("Volcán-arena: volcánico"), PrimaryBiome(FName(TEXT("N03_volcan_arena"))), ETNProcBiome::Volcanic);
	TestEqual(TEXT("Coliseo: pueblo"), PrimaryBiome(FName(TEXT("N01_coliseo"))), ETNProcBiome::Human);
	TestEqual(TEXT("Desconocida: playa"), PrimaryBiome(FName(TEXT("Z99_nueva"))), ETNProcBiome::Beach);
	for (const ETNProcBiome Biome : { ETNProcBiome::Beach, ETNProcBiome::Jungle, ETNProcBiome::Desert, ETNProcBiome::Volcanic, ETNProcBiome::Human })
	{
		TestTrue(TEXT("Cada bioma tiene decorado con colisión"), DecorElementsFor(Biome).Num() > 0);
	}
	TestEqual(TEXT("Sin suelo: nada"), MakePlan(FHeightField(), 0.f, Zones, ETNProcBiome::Beach, 1u).Flora.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctSceneryArenaTest,
	"Tortunabo.Tct.Scenery.Arena",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctSceneryArenaTest::RunTest(const FString& Parameters)
{
	// El decorado sobre la arena de verdad (A01_diana): lo monta la arena al fijar la semilla y los sitios libres.
	const FName Diana(TEXT("A01_diana"));
	if (!ATN_TctArena::VariantExists(Diana))
	{
		AddWarning(TEXT("Sin Scripts/terrain_volumes/Variants/A01_diana (build cocinada): se salta."));
		return true;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNTctSceneryArenaWorld"));
	if (!TestNotNull(TEXT("Mundo de prueba"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_TctArena* Arena = World->SpawnActor<ATN_TctArena>(ATN_TctArena::StaticClass(), FTransform::Identity, Params);
	if (TestNotNull(TEXT("Arena"), Arena))
	{
		Arena->Variant = Diana;
		Arena->ServerSetArenaVariant(Diana);
		TestTrue(TEXT("Hay suelo pisable"), Arena->Survey(250.f));
		TArray<FIntVector> KeepOut;
		for (const FTransform& Spawn : Arena->PickSpawnTransforms(8, 120.f))
		{
			KeepOut.Add(FIntVector(FMath::RoundToInt(Spawn.GetLocation().X), FMath::RoundToInt(Spawn.GetLocation().Y), 900));
		}
		TestEqual(TEXT("Ocho salidas"), KeepOut.Num(), 8);

		TestNull(TEXT("Sin semilla, sin decorado"), Arena->GetScenery());
		Arena->ServerSetScenery(0xC0FFEEu, KeepOut);
		ATN_TctScenery* Scenery = Arena->GetScenery();
		if (TestNotNull(TEXT("La arena monta el decorado al recibir la semilla"), Scenery))
		{
			const TNTctScenery::FPlan& Plan = Scenery->GetPlan();
			AddInfo(FString::Printf(TEXT("Diana: %d plantas, %d piezas con colisión (huella %08X), %d animales"), Scenery->GetNumFlora(), Plan.Decor.Num(), Plan.Fingerprint, Scenery->GetNumAnimals()));
			TestTrue(TEXT("Vegetación sobre la diana"), Plan.Flora.Num() > 500);
			TestTrue(TEXT("Piezas con colisión sobre la diana"), Plan.Decor.Num() >= 4);
			TestEqual(TEXT("Todas las piezas con colisión se montan"), Scenery->GetNumDecorBuilt(), Plan.Decor.Num());
			for (const TNTctScenery::FDecorPick& Pick : Plan.Decor)
			{
				for (const FIntVector& Zone : KeepOut)
				{
					TestTrue(TEXT("Ninguna pieza con colisión en una salida"), FVector2D::Distance(FVector2D(Pick.Location.X, Pick.Location.Y), FVector2D(Zone.X, Zone.Y)) > Zone.Z);
				}
			}

			// El mismo servidor con la misma semilla da la misma huella; otra semilla, otra.
			const uint32 First = Plan.Fingerprint;
			Arena->ServerSetScenery(0xC0FFEEu, KeepOut);
			TestTrue(TEXT("La misma semilla no rehace nada"), Arena->GetScenery() == Scenery);
			Arena->ServerSetScenery(0xBADF00Du, KeepOut);
			if (TestNotNull(TEXT("Otra semilla: otro decorado"), Arena->GetScenery()))
			{
				TestTrue(TEXT("Otra semilla, otra huella"), Arena->GetScenery()->GetPlan().Fingerprint != First);
			}
			Arena->ServerSetScenery(0xC0FFEEu, KeepOut);
			if (TestNotNull(TEXT("Y la primera otra vez"), Arena->GetScenery()))
			{
				TestEqual(TEXT("Misma semilla, misma huella (como en el cliente)"), Arena->GetScenery()->GetPlan().Fingerprint, First);
			}
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
