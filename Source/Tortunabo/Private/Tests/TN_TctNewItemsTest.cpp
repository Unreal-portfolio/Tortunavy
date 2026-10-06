// Más objetos de Todos contra Todos con ventaja y coste, y mejores puntos de objetos (#830): fichas y costes de los diez objetos
// nuevos, rareza y reparto por rareza y por cómo va la ronda, los puntos de objetos en la arena de verdad (altura y exposición),
// y los efectos en un mundo de juego sin ventana.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tct.Items; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/TN_TctItemComponent.h"
#include "Game/TN_TctItemRules.h"
#include "Game/TN_TctItems.h"
#include "Game/TN_TctRules.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_RaceItems.h"
#include "World/TN_TctArena.h"
#include "World/TN_TctWhirlwind.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTctNewItemsTest
{
	const ETNTctItem NewKinds[] = { ETNTctItem::Cohete, ETNTctItem::BotasMuelle, ETNTctItem::Aletas, ETNTctItem::Cambiazo,
		ETNTctItem::Burbuja, ETNTctItem::Puas, ETNTctItem::Red, ETNTctItem::Remolino, ETNTctItem::TaponMarea, ETNTctItem::Paraguas };

	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNTctNewItemsTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
		}

		~FPlayWorld()
		{
			if (World->HasBegunPlay()) { World->EndPlay(EEndPlayReason::Quit); }
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		void Advance(float Seconds)
		{
			for (float Left = Seconds; Left > 0.f; Left -= 0.25f) { World->Tick(LEVELTICK_All, FMath::Min(0.25f, Left)); }
		}
	};

	ATortugaCharacter* SpawnTurtle(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), Where, FRotator::ZeroRotator, Params);
	}

	/** Tres pisos como la diana: el centro alto, un anillo medio y un anillo bajo y expuesto; ~25 sitios por piso. */
	TArray<FTNTctPadSpot> RingSpots()
	{
		TArray<FTNTctPadSpot> Spots;
		const float Radii[3] = { 300.f, 1800.f, 3600.f };
		const float Heights[3] = { 1.f, 0.55f, 0.15f };
		const float Exposures[3] = { 0.f, 0.1f, 0.5f };
		for (int32 Ring = 0; Ring < 3; ++Ring)
		{
			for (int32 Index = 0; Index < (Ring == 0 ? 4 : 24); ++Index)
			{
				const float Angle = UE_TWO_PI * Index / (Ring == 0 ? 4 : 24);
				FTNTctPadSpot Spot;
				Spot.Pos = FVector(FMath::Cos(Angle) * Radii[Ring], FMath::Sin(Angle) * Radii[Ring], 0.0);
				Spot.HeightFrac = Heights[Ring];
				Spot.Exposure = Exposures[Ring];
				Spots.Add(Spot);
			}
		}
		return Spots;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctNewItemsSpecTest,
	"Tortunabo.Tct.Items.Nuevos",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctNewItemsSpecTest::RunTest(const FString& Parameters)
{
	using namespace TNTctItemRules;
	using namespace TNTctNewItemsTest;
	TestTrue(TEXT("Al menos 8 objetos nuevos"), UE_ARRAY_COUNT(NewKinds) >= 8);

	TSet<FString> Codes;
	for (const ETNTctItem Kind : TNTctItemRules::AllKinds())
	{
		TestFalse(FString::Printf(TEXT("%s: código único"), Spec(Kind).Code), Codes.Contains(Spec(Kind).Code));
		Codes.Add(Spec(Kind).Code);
	}
	for (const ETNTctItem Kind : NewKinds)
	{
		const FTNTctItemSpec& Item = Spec(Kind);
		TestEqual(FString::Printf(TEXT("%s: de código"), Item.Code), Item.Source, ETNTctItemSource::Code);
		TestTrue(FString::Printf(TEXT("%s: al menos una carga"), Item.Code), Item.Charges >= 1);
		TestTrue(FString::Printf(TEXT("%s: sale en los puntos"), Item.Code), Item.PadWeight > 0.f);
		TestFalse(FString::Printf(TEXT("%s: tiene nombre"), Item.Code), TNTctItems::DisplayName(Kind).IsEmpty());
		FTN_InventoryItem Held;
		TestTrue(FString::Printf(TEXT("%s: se puede dar"), Item.Code), TNTctItems::MakeItem(Kind, Held));
		TestEqual(FString::Printf(TEXT("%s: objeto de la fila"), Item.Code), TNTctItems::KindOf(Held), Kind);
	}

	// Cada efecto con duración tiene un coste de verdad (algo que limita) y una duración.
	for (int32 Index = 0; Index < static_cast<int32>(ETNTctFx::Count); ++Index)
	{
		const ETNTctFx Fx = static_cast<ETNTctFx>(Index);
		const FTNTctFxLimits Limits = FxLimits(Fx);
		const float NoCap = TNumericLimits<float>::Max();
		TestTrue(FString::Printf(TEXT("Efecto %d: dura"), Index), FxSeconds(Fx) > 0.f);
		TestTrue(FString::Printf(TEXT("Efecto %d: lleva un coste (velocidad topada)"), Index), Limits.SpeedCap < NoCap);
	}
	TestTrue(TEXT("Botas: salto mayor y más lenta"), FxLimits(ETNTctFx::Spring).JumpMultiplier > 1.f && FxLimits(ETNTctFx::Spring).SpeedCap < 450.f);
	TestTrue(TEXT("Burbuja: flota (menos gravedad) y es lenta"), FxLimits(ETNTctFx::Bubble).Gravity < 1.f && FxLimits(ETNTctFx::Bubble).SpeedCap < 450.f);
	TestTrue(TEXT("Paraguas: casi sin gravedad pero sin salto"), FxLimits(ETNTctFx::Glide).Gravity < 0.3f && FxLimits(ETNTctFx::Glide).JumpMultiplier < 1.f);
	TestTrue(TEXT("Red: ni salta ni anda"), FxLimits(ETNTctFx::Net).JumpCap == 0.f && FxLimits(ETNTctFx::Net).SpeedCap < 100.f);
	TestTrue(TEXT("Aletas: reducen el veneno a menos de la mitad"), TNTctItemTuning::FinsPoisonScale < 0.5f);

	// Cohete: hacia donde se mira, alto y rápido.
	const FVector Launch = CoheteLaunch(FVector(0.0, 1.0, 0.3));
	TestTrue(TEXT("Cohete: hacia delante"), Launch.Y > 1000.0 && FMath::Abs(Launch.X) < 1.0);
	TestTrue(TEXT("Cohete: y hacia arriba"), Launch.Z >= 800.0);

	// Púas: empujan hacia fuera a quien está en el radio, no a quien está fuera.
	FVector Push;
	TestTrue(TEXT("Púas: a 1 m, empujan"), SpikesPush(FVector::ZeroVector, FVector(100.0, 0.0, 0.0), Push));
	TestTrue(TEXT("Púas: hacia fuera y arriba"), Push.X > 500.0 && Push.Z > 0.0);
	TestFalse(TEXT("Púas: a 4 m, no"), SpikesPush(FVector::ZeroVector, FVector(400.0, 0.0, 0.0), Push));

	// Remolino: dentro de su radio y a ras de suelo, lanza hacia arriba y de lado.
	TestTrue(TEXT("Remolino: dentro, lanza"), WhirlKick(FVector::ZeroVector, FVector(120.0, 0.0, 0.0), Push));
	TestTrue(TEXT("Remolino: arriba, y de lado (gira)"), Push.Z >= 700.0 && FMath::Abs(Push.Y) > 100.0);
	TestFalse(TEXT("Remolino: fuera del radio, no"), WhirlKick(FVector::ZeroVector, FVector(500.0, 0.0, 0.0), Push));
	TestFalse(TEXT("Remolino: muy por encima, no"), WhirlKick(FVector::ZeroVector, FVector(50.0, 0.0, 400.0), Push));

	// Tapón de marea: retrasar el reloj baja el agua y aplaza lo que falta, sin ir nunca al futuro.
	TestEqual(TEXT("Tapón: 10 s más tarde"), TNTctRules::DelayedFloodStart(100.f, 10.f, 130.f), 110.f);
	TestEqual(TEXT("Tapón: nunca en el futuro"), TNTctRules::DelayedFloodStart(100.f, 10.f, 105.f), 105.f);
	FTNTctFloodPlan Plan;
	Plan.BaseZ = 0.f;
	Plan.Levels = { 100.f, 300.f };
	Plan.SuddenDeathZ = 900.f;
	const float Before = TNTctRules::WaterZAt(Plan, 35.f);
	const float After = TNTctRules::WaterZAt(Plan, 35.f - TNTctItemTuning::PlugDelaySeconds);
	TestTrue(TEXT("Tapón: el agua baja o se queda"), After <= Before);
	TestEqual(TEXT("Tapón: la próxima subida se aplaza 10 s"), TNTctRules::NextRise(Plan, 40.f - TNTctItemTuning::PlugDelaySeconds).SecondsLeft,
		TNTctRules::NextRise(Plan, 40.f).SecondsLeft + TNTctItemTuning::PlugDelaySeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctPadRarityTest,
	"Tortunabo.Tct.Items.Rareza",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctPadRarityTest::RunTest(const FString& Parameters)
{
	using namespace TNTctItemRules;
	using namespace TNTctNewItemsTest;

	// La rareza del punto crece con la altura y con la exposición.
	TestEqual(TEXT("Piso bajo y seguro: común"), PadRarityFor(0.1f, 0.f), ETNTctRarity::Common);
	TestEqual(TEXT("Piso medio: raro"), PadRarityFor(0.5f, 0.1f), ETNTctRarity::Rare);
	TestEqual(TEXT("La cima: épico"), PadRarityFor(1.f, 0.f), ETNTctRarity::Epic);
	TestEqual(TEXT("Bajo pero pegado al vacío: raro"), PadRarityFor(0.15f, 1.f), ETNTctRarity::Rare);
	TestTrue(TEXT("Más altura nunca baja la rareza"), PadRarityFor(0.9f, 0.2f) >= PadRarityFor(0.5f, 0.2f));
	TestTrue(TEXT("Más exposición nunca baja la rareza"), PadRarityFor(0.5f, 0.9f) >= PadRarityFor(0.5f, 0.1f));

	// Hay de todas las rarezas en el catálogo, y las épicas son las menos.
	int32 Count[3] = { 0, 0, 0 };
	for (const ETNTctItem Kind : AllKinds()) { ++Count[static_cast<int32>(Spec(Kind).Rarity)]; }
	TestTrue(TEXT("Comunes de sobra"), Count[0] >= 8);
	TestTrue(TEXT("Raros"), Count[1] >= 4);
	TestTrue(TEXT("Épicos (pocos)"), Count[2] >= 3 && Count[2] < Count[1] + Count[0]);

	// Un punto común da sobre todo objetos comunes; uno épico, lo épico; y según avanza la ronda sale más de lo raro.
	const TArray<ETNTctItem> All = AllKinds();
	auto EpicShare = [&All](ETNTctRarity Pad, float Progress)
	{
		int32 Epic = 0;
		constexpr int32 Rolls = 2000;
		for (int32 Roll = 0; Roll < Rolls; ++Roll)
		{
			const ETNTctItem Kind = PickPadItem(All, ETNTctItem::None, (Roll + 0.5f) / Rolls, Pad, Progress);
			Epic += Spec(Kind).Rarity == ETNTctRarity::Epic ? 1 : 0;
		}
		return static_cast<float>(Epic) / Rolls;
	};
	const float CommonPad = EpicShare(ETNTctRarity::Common, 0.f);
	const float RarePad = EpicShare(ETNTctRarity::Rare, 0.f);
	const float EpicPad = EpicShare(ETNTctRarity::Epic, 0.f);
	AddInfo(FString::Printf(TEXT("Parte de objetos épicos: punto común %.0f %%, raro %.0f %%, épico %.0f %%"), CommonPad * 100.f, RarePad * 100.f, EpicPad * 100.f));
	TestTrue(TEXT("Más épicos cuanto más épico es el punto"), CommonPad < RarePad && RarePad < EpicPad);
	TestTrue(TEXT("En un punto común, los épicos son rareza"), CommonPad < 0.05f);
	TestTrue(TEXT("En un punto épico, lo más habitual es un épico"), EpicPad > 0.35f);
	TestTrue(TEXT("Según avanza la ronda, salen más épicos"), EpicShare(ETNTctRarity::Rare, 1.f) > RarePad + 0.03f);
	for (const ETNTctItem Kind : NewKinds)
	{
		TestTrue(FString::Printf(TEXT("%s: sale en puntos de su rareza"), Spec(Kind).Code),
			PadItemWeight(Kind, Spec(Kind).Rarity, 0.f) > 0.f);
	}

	// Nunca el mismo objeto dos veces seguidas en un punto.
	for (int32 Roll = 0; Roll < 200; ++Roll)
	{
		TestTrue(TEXT("No repite"), PickPadItem(All, ETNTctItem::Shovel, Roll / 200.f, ETNTctRarity::Common, 0.f) != ETNTctItem::Shovel);
	}
	// El progreso de la ronda sale del plan del agua.
	FTNTctFloodPlan Plan;
	Plan.Levels = { 0.f, 100.f, 200.f, 300.f };
	TestEqual(TEXT("Al empezar, 0"), TNTctRules::RoundProgress(Plan, 5.f), 0.f);
	TestEqual(TEXT("En la marea final, 1"), TNTctRules::RoundProgress(Plan, 130.f), 1.f);
	const float Half = TNTctRules::RoundProgress(Plan, Plan.StartDelay + 2.f * Plan.StepSeconds);
	TestTrue(TEXT("A la mitad, la mitad"), FMath::IsNearlyEqual(Half, 0.5f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctPadPlanTest,
	"Tortunabo.Tct.Items.PadsPlan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctPadPlanTest::RunTest(const FString& Parameters)
{
	using namespace TNTctItemRules;
	using namespace TNTctNewItemsTest;
	const TArray<FTNTctPadSpot> Spots = RingSpots();
	const TArray<FVector> Spawns = { FVector(3600.0, 0.0, 0.0), FVector(-3600.0, 0.0, 0.0) };

	const TArray<FTNTctPadPick> Picks = PlanPads(Spots, 10, Spawns, 700.f, 1500.f);
	TestEqual(TEXT("Diez puntos"), Picks.Num(), 10);
	int32 Count[3] = { 0, 0, 0 };
	float Height[3] = { 0.f, 0.f, 0.f };
	TSet<int32> Used;
	for (const FTNTctPadPick& Pick : Picks)
	{
		TestFalse(TEXT("Sin repetir sitio"), Used.Contains(Pick.Index));
		Used.Add(Pick.Index);
		++Count[static_cast<int32>(Pick.Rarity)];
		Height[static_cast<int32>(Pick.Rarity)] += Spots[Pick.Index].HeightFrac;
		TestEqual(TEXT("La rareza es la del sitio"), Pick.Rarity, PadRarityFor(Spots[Pick.Index].HeightFrac, Spots[Pick.Index].Exposure));
		for (const FVector& Spawn : Spawns)
		{
			TestTrue(TEXT("Lejos de las salidas"), FVector::Dist2D(Spots[Pick.Index].Pos, Spawn) >= 700.0);
		}
	}
	AddInfo(FString::Printf(TEXT("Puntos: %d épicos, %d raros, %d comunes"), Count[2], Count[1], Count[0]));
	TestTrue(TEXT("Hay épicos (en lo más alto)"), Count[2] >= 1);
	TestTrue(TEXT("Hay raros y comunes"), Count[1] >= 1 && Count[0] >= 1);
	TestTrue(TEXT("Los épicos son los menos"), Count[2] <= Count[1] && Count[1] <= Count[0] + 1);
	if (Count[2] > 0 && Count[0] > 0)
	{
		TestTrue(TEXT("Los épicos, más arriba que los comunes"), Height[2] / Count[2] > Height[0] / Count[0]);
	}

	// Repartidos: ninguno pegado a otro (más de 15 m si caben).
	double Closest = TNumericLimits<double>::Max();
	for (int32 A = 0; A < Picks.Num(); ++A)
	{
		for (int32 B = A + 1; B < Picks.Num(); ++B)
		{
			Closest = FMath::Min(Closest, FVector::Dist2D(Spots[Picks[A].Index].Pos, Spots[Picks[B].Index].Pos));
		}
	}
	TestTrue(TEXT("Repartidos (más de 8 m entre dos)"), Closest > 800.0);

	// Con pocas jugadoras solo se usan los primeros: ya son una mezcla.
	TSet<uint8> FirstFour;
	for (int32 Index = 0; Index < 4; ++Index) { FirstFour.Add(static_cast<uint8>(Picks[Index].Rarity)); }
	TestTrue(TEXT("Los cuatro primeros mezclan rarezas"), FirstFour.Num() >= 2);

	// Determinista, y con más puntos que sitios, todos los sitios.
	const TArray<FTNTctPadPick> Again = PlanPads(Spots, 10, Spawns, 700.f, 1500.f);
	bool bSame = Again.Num() == Picks.Num();
	for (int32 Index = 0; bSame && Index < Picks.Num(); ++Index) { bSame = Again[Index].Index == Picks[Index].Index; }
	TestTrue(TEXT("El mismo reparto cada vez"), bSame);
	TestEqual(TEXT("Más puntos que sitios: todos los sitios"), PlanPads(Spots, 500, {}, 0.f, 0.f).Num(), Spots.Num());
	TestEqual(TEXT("Sin sitios: nada"), PlanPads({}, 10, {}, 0.f, 0.f).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctDianaPadRarityTest,
	"Tortunabo.Tct.Items.PadsDiana",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctDianaPadRarityTest::RunTest(const FString& Parameters)
{
	// Los puntos de objetos en la arena de verdad (A01_diana), como los reparte ATN_TctGameMode::CreateItemPads (#830).
	const FName Diana(TEXT("A01_diana"));
	if (!ATN_TctArena::VariantExists(Diana))
	{
		AddWarning(TEXT("Sin Scripts/terrain_volumes/Variants/A01_diana (build cocinada): se salta."));
		return true;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNTctPadsDianaWorld"));
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
		const TArray<FVector>& Candidates = Arena->GetSpawnCandidates();
		const TArray<float>& Exposure = Arena->GetSpawnExposure();
		TestEqual(TEXT("Una exposición por sitio"), Exposure.Num(), Candidates.Num());
		TArray<FVector> Spawns;
		for (const FTransform& Spawn : Arena->PickSpawnTransforms(TNTctRules::MaxPlayers, 120.f)) { Spawns.Add(Spawn.GetLocation()); }

		const float BaseZ = Arena->GetBaseWaterZ();
		const float Span = FMath::Max(1.f, Arena->GetTopZ() - BaseZ);
		TArray<FTNTctPadSpot> Spots;
		float MaxExposure = 0.f;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			FTNTctPadSpot& Spot = Spots.AddDefaulted_GetRef();
			Spot.Pos = Candidates[Index];
			Spot.HeightFrac = FMath::Clamp((static_cast<float>(Candidates[Index].Z) - BaseZ) / Span, 0.f, 1.f);
			Spot.Exposure = Exposure.IsValidIndex(Index) ? Exposure[Index] : 0.f;
			MaxExposure = FMath::Max(MaxExposure, Spot.Exposure);
		}
		TestTrue(TEXT("Hay sitios expuestos (cerca del borde de un piso)"), MaxExposure > 0.5f);
		const TArray<FTNTctPadPick> Picks = TNTctItemRules::PlanPads(Spots, 10, Spawns, 700.f, 1500.f);
		TestEqual(TEXT("Diez puntos"), Picks.Num(), 10);
		int32 Count[3] = { 0, 0, 0 };
		float EpicHeight = 0.f;
		float CommonHeight = 0.f;
		for (const FTNTctPadPick& Pick : Picks)
		{
			++Count[static_cast<int32>(Pick.Rarity)];
			if (Pick.Rarity == ETNTctRarity::Epic) { EpicHeight += Spots[Pick.Index].HeightFrac; }
			else if (Pick.Rarity == ETNTctRarity::Common) { CommonHeight += Spots[Pick.Index].HeightFrac; }
		}
		AddInfo(FString::Printf(TEXT("Diana: %d épicos, %d raros, %d comunes"), Count[2], Count[1], Count[0]));
		TestTrue(TEXT("La diana tiene puntos épicos"), Count[2] >= 1);
		TestTrue(TEXT("Y comunes"), Count[0] >= 1);
		if (Count[2] > 0 && Count[0] > 0)
		{
			TestTrue(TEXT("Los épicos están más arriba que los comunes"), EpicHeight / Count[2] > CommonHeight / Count[0]);
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctNewItemsWorldTest,
	"Tortunabo.Tct.Items.Efectos",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctNewItemsWorldTest::RunTest(const FString& Parameters)
{
	using namespace TNTctNewItemsTest;
	FPlayWorld Play;
	ATortugaCharacter* Turtle = SpawnTurtle(Play.World, FVector(0.0, 0.0, 200.0));
	UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOrAddOn(Turtle);
	if (!TestNotNull(TEXT("Tortuga"), Turtle) || !TestNotNull(TEXT("Componente"), Effects))
	{
		return false;
	}

	// Cada efecto con su hora de fin: se pone, dura y se quita al empezar la ronda.
	for (int32 Index = 0; Index < static_cast<int32>(ETNTctFx::Count); ++Index)
	{
		const ETNTctFx Fx = static_cast<ETNTctFx>(Index);
		TestFalse(FString::Printf(TEXT("Efecto %d: libre"), Index), Effects->IsFxActive(Fx));
		Effects->GrantFx(Fx, TNTctItemRules::FxSeconds(Fx));
		TestTrue(FString::Printf(TEXT("Efecto %d: puesto"), Index), Effects->IsFxActive(Fx));
	}
	Effects->ClearEffects();
	for (int32 Index = 0; Index < static_cast<int32>(ETNTctFx::Count); ++Index)
	{
		TestFalse(FString::Printf(TEXT("Efecto %d: quitado en la ronda nueva"), Index), Effects->IsFxActive(static_cast<ETNTctFx>(Index)));
	}

	// Botas: el tope de velocidad entra y sale con el efecto.
	Effects->GrantFx(ETNTctFx::Spring, 1.f);
	Play.Advance(0.5f);
	TestTrue(TEXT("Botas puestas a los 0,5 s"), Effects->IsFxActive(ETNTctFx::Spring));
	Play.Advance(1.f);
	TestFalse(TEXT("Botas acabadas a los 1,5 s"), Effects->IsFxActive(ETNTctFx::Spring));

	// Aletas: el veneno del agua sube mucho más despacio.
	UTN_TctItemComponent* Plain = UTN_TctItemComponent::FindOrAddOn(SpawnTurtle(Play.World, FVector(2000.0, 0.0, 200.0)));
	if (!TestNotNull(TEXT("Otra tortuga"), Plain))
	{
		return false;
	}
	Effects->GrantFx(ETNTctFx::Fins, 20.f);
	Effects->ServerTickWater(true);
	Plain->ServerTickWater(true);
	Play.Advance(3.f);
	AddInfo(FString::Printf(TEXT("Veneno a los 3 s: con aletas %.2f, sin ellas %.2f"), Effects->GetPoison(), Plain->GetPoison()));
	TestTrue(TEXT("Con aletas, menos de la mitad de veneno"), Effects->GetPoison() < Plain->GetPoison() * 0.5f);

	// Burbuja: nada la empuja ni la derriba (TNRaceItems::IsInvulnerable) y se acaba con el tiempo.
	Effects->ClearEffects();
	TestFalse(TEXT("Sin burbuja, vulnerable"), TNRaceItems::IsInvulnerable(Turtle));
	TestTrue(TEXT("Sin burbuja, se la puede empujar"), TNTctItems::CanAffect(Turtle, true));
	Effects->GrantFx(ETNTctFx::Bubble, 1.f);
	TestTrue(TEXT("Con burbuja, invulnerable"), TNRaceItems::IsInvulnerable(Turtle));
	TestFalse(TEXT("Con burbuja, nadie la empuja"), TNTctItems::CanAffect(Turtle, true));
	Play.Advance(1.5f);
	TestTrue(TEXT("Acabada la burbuja, otra vez vulnerable"), TNTctItems::CanAffect(Turtle, true));

	// Remolino: lanza a quien está dentro (también a quien lo plantó) y no vuelve a hacerlo hasta pasada la espera.
	FActorSpawnParameters Params;
	Params.Instigator = Turtle;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Feet = Turtle->GetActorLocation() - FVector(0.0, 0.0, Turtle->GetSimpleCollisionHalfHeight());
	ATN_TctWhirlwind* Whirl = Play.World->SpawnActor<ATN_TctWhirlwind>(ATN_TctWhirlwind::StaticClass(), FTransform(Feet), Params);
	UCharacterMovementComponent* Movement = Turtle->GetCharacterMovement();
	if (TestNotNull(TEXT("Remolino"), Whirl) && TestNotNull(TEXT("Movimiento"), Movement))
	{
		Movement->SetMovementMode(MOVE_Walking);
		TestEqual(TEXT("Lanza a quien lo plantó"), Whirl->ServerKickTurtles(), 1);
		AddInfo(FString::Printf(TEXT("Lanzamiento pendiente: %.0f cm/s hacia arriba"), Movement->PendingLaunchVelocity.Z));
		TestTrue(TEXT("Hacia arriba"), Movement->PendingLaunchVelocity.Z >= TNTctItemTuning::WhirlUp - 1.f);
		TestEqual(TEXT("No la vuelve a lanzar enseguida"), Whirl->ServerKickTurtles(), 0);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
