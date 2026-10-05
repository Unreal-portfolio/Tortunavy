// Los castillos de la carrera dan una gran ventaja arriba (#741): subir a una fortaleza cuesta mucho, así que su cima lleva
// siempre una catapulta potenciada que lanza hacia delante, por la ruta, y un cofre con lo mejor de la carrera, sea cual sea
// el puesto de quien lo abre (ETNRaceLootSource::Summit: la tabla de las últimas para todas, así que hasta la primera puede
// sacar el pelícano taxi, el protector solar o el coco dorado).
//   Reparto: en cada ronda (varias semillas y las tres dificultades) ninguna fortaleza sale sin catapulta, sin cofre ni sin
//   conchas de premio, y su catapulta cae en la franja que el reparto le reserva.
//   Pesos: la cima pesa igual para cualquier puesto, como la última, y da mucho más de lo que hace remontar que un cofre
//   corriente a la primera.
//   Mundo: las tres fortalezas de verdad crean su catapulta potenciada y su cofre de cima (ninguna crea un trampolín).
// Correr desde Session Frontend (categoría "Tortunabo.Beach.SummitPrize") o sin ventana:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Beach.SummitPrize; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "World/Beach/TN_BeachCatapult.h"
#include "World/Beach/TN_BeachChest.h"
#include "World/Beach/TN_BeachFortress.h"
#include "World/Beach/TN_BeachLayout.h"
#include "World/Beach/TN_BeachTrampoline.h"
#include "World/Beach/TN_RaceItems.h"
#include "World/Beach/TN_BeachFortressKit.h"
#include "World/Beach/TN_BeachTrapKit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBeachSummitPrizeTest
{
	bool IsFortress(ETNBeachElement E)
	{
		return E == ETNBeachElement::FortressMedium || E == ETNBeachElement::FortressLarge || E == ETNBeachElement::FortressColossal;
	}

	/** Mundo de juego mínimo con BeginPlay ya hecho (los actores que se crean después lo reciben al aparecer). */
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

	template <typename T>
	int32 CountActors(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<T> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Reparto: ninguna fortaleza sin catapulta ni sin recompensa
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachSummitPrizeLayoutTest,
	"Tortunabo.Beach.SummitPrize.Layout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachSummitPrizeLayoutTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		ETNProcDifficulty Difficulty;
		const TCHAR* Name;
		int32 NumSeeds;
	};
	const FCase Cases[] = {
		{ ETNProcDifficulty::Normal, TEXT("Normal"), 24 },
		{ ETNProcDifficulty::Easy, TEXT("Fácil"), 8 },
		{ ETNProcDifficulty::Hard, TEXT("Difícil"), 8 },
	};
	// El cofre (ATN_BeachChest: 3,4 m de ancho por 2,2 m de fondo) tiene que caber entero en la cima.
	constexpr double ChestHalfWidth = 170.0;
	constexpr double ChestHalfDepth = 110.0;

	int32 Fortresses = 0;
	int32 Rounds = 0;
	uint8 SizesMask = 0;
	for (const FCase& Case : Cases)
	{
		for (int32 s = 0; s < Case.NumSeeds; ++s)
		{
			const int32 Seed = 1000 + s * 7919;
			const FString Ctx = FString::Printf(TEXT("%s, semilla %d"), Case.Name, Seed);
			TNBeachLayout::FRoundLayout L;
			TNBeachLayout::GenerateRound(Seed, Case.Difficulty, L);
			++Rounds;
			int32 InRound = 0;
			for (const TNBeachLayout::FItem& It : L.Items)
			{
				if (!TNBeachSummitPrizeTest::IsFortress(It.Element))
				{
					continue;
				}
				++Fortresses;
				++InRound;
				SizesMask |= static_cast<uint8>(1 << (static_cast<int32>(It.Element) - static_cast<int32>(ETNBeachElement::FortressMedium)));
				const FString Name = FString::Printf(TEXT("%s, %s"), *Ctx, *UEnum::GetValueAsString(It.Element));
				const TNBeachFortressKit::FPlan Plan = TNBeachFortressKit::MakePlan(It.Element, It.Spec.SizeScale, TNBeachTrapKit::SeedOf(It.Spec.Seed, 151u));
				const double Ks = Plan.Summit().K;

				// Arriba, una catapulta (ni un trampolín) con la medida de su brazo dentro de la cima.
				TestTrue(Name + TEXT(": lanzador de la cima = catapulta"), Plan.bCatapult);
				TestTrue(Name + TEXT(": catapulta con tamaño de catapulta"), Plan.LauncherSize >= 0.72f && Plan.LauncherSize <= 1.15f);
				TestTrue(Name + TEXT(": catapulta dentro de la cima"), FMath::Abs(Plan.LauncherAt.X) < Ks && Plan.LauncherAt.Z == Plan.Summit().Z);
				// Lo que lanza cae en la franja que el reparto le deja libre hacia delante (+X de la fortaleza, mirando al mar).
				TestTrue(Name + TEXT(": cae en su franja libre"), TNBeachLayout::FortressLandingAt >= TNBeachLayout::FortressLandingFrom
					&& TNBeachLayout::FortressLandingAt <= TNBeachLayout::FortressLandingReach);
				TestTrue(Name + TEXT(": mira al mar"), FMath::Abs(FRotator::NormalizeAxis(It.Yaw)) <= 10.01);

				// Y la recompensa: el cofre cabe en la cima y hay conchas de premio (la reina de 100 entre ellas).
				TestTrue(Name + TEXT(": cofre dentro de la cima"), FMath::Abs(Plan.ChestAt.X) + ChestHalfDepth <= Ks && FMath::Abs(Plan.ChestAt.Y) + ChestHalfWidth <= Ks
					&& Plan.ChestAt.Z == Plan.Summit().Z);
				int32 Points = 0;
				bool bQueen = false;
				for (const TNBeachFortressKit::FShellSpot& Shell : Plan.Shells)
				{
					Points += Shell.Value;
					bQueen |= Shell.Value >= 100;
				}
				TestTrue(FString::Printf(TEXT("%s: conchas de premio (%d puntos)"), *Name, Points), Points >= 150 && bQueen);
			}
			TestTrue(Ctx + TEXT(": hay alguna fortaleza"), InRound >= 1);
		}
	}
	TestTrue(FString::Printf(TEXT("Se han visto fortalezas de los tres tamaños (%d fortalezas en %d rondas)"), Fortresses, Rounds), SizesMask == 0b111);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pesos: la cima da la tabla de los mejores a cualquier puesto
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachSummitPrizeWeightsTest,
	"Tortunabo.Beach.SummitPrize.Weights",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachSummitPrizeWeightsTest::RunTest(const FString& Parameters)
{
	using TNRaceItems::PositionWeight;
	using TNRaceItems::PositionWeightForUse;
	constexpr int32 Racers = 4;
	const float Norms[] = { 0.f, 0.25f, 0.5f, 0.75f, 1.f };

	// Los que hacen remontar: lo mejor de la carrera.
	const TArray<ETNRaceItem> Comeback = { ETNRaceItem::TripleCoconut3, ETNRaceItem::GoldenCoconut, ETNRaceItem::PelicanTaxi, ETNRaceItem::Sunscreen };

	auto ComebackShare = [&](float Norm, ETNRaceLootSource Source)
	{
		float Best = 0.f;
		float Total = 0.f;
		for (int32 Index = static_cast<int32>(ETNRaceItem::Coconut); Index < static_cast<int32>(ETNRaceItem::Count); ++Index)
		{
			const ETNRaceItem Kind = static_cast<ETNRaceItem>(Index);
			const float Weight = PositionWeight(Kind, Norm, Racers, Source);
			Total += Weight;
			if (Comeback.Contains(Kind))
			{
				Best += Weight;
			}
		}
		return Total > 0.f ? Best / Total : 0.f;
	};

	for (const float Norm : Norms)
	{
		for (int32 Index = static_cast<int32>(ETNRaceItem::Coconut); Index < static_cast<int32>(ETNRaceItem::Count); ++Index)
		{
			const ETNRaceItem Kind = static_cast<ETNRaceItem>(Index);
			const FString What = FString::Printf(TEXT("%s con norma %.2f"), *TNRaceItems::CodeName(Kind), Norm);
			// Pesa igual para cualquier puesto, y como el cofre de la última.
			TestEqual(What + TEXT(": la cima no depende del puesto"), PositionWeight(Kind, Norm, Racers, ETNRaceLootSource::Summit),
				PositionWeight(Kind, 1.f, Racers, ETNRaceLootSource::Chest));
		}
		for (const ETN_ItemUseType Use : { ETN_ItemUseType::SelfStaminaBoost, ETN_ItemUseType::SelfStaminaFull, ETN_ItemUseType::Throwable, ETN_ItemUseType::InkThrower, ETN_ItemUseType::Conch })
		{
			TestEqual(FString::Printf(TEXT("Objeto de siempre %d con norma %.2f: la cima no depende del puesto"), static_cast<int32>(Use), Norm),
				PositionWeightForUse(Use, Norm, ETNRaceLootSource::Summit), PositionWeightForUse(Use, 1.f, ETNRaceLootSource::Chest));
		}
	}

	// A la primera, un cofre corriente no le da nada de lo que hace remontar; el de la cima, sí.
	for (const ETNRaceItem Kind : Comeback)
	{
		const FString What = TNRaceItems::CodeName(Kind);
		TestEqual(What + TEXT(": la primera no lo saca de un cofre corriente"), PositionWeight(Kind, 0.f, Racers, ETNRaceLootSource::Chest), 0.f);
		TestTrue(What + TEXT(": la primera lo saca del cofre de la cima"), PositionWeight(Kind, 0.f, Racers, ETNRaceLootSource::Summit) > 0.5f);
	}
	const float FirstChest = ComebackShare(0.f, ETNRaceLootSource::Chest);
	const float FirstSummit = ComebackShare(0.f, ETNRaceLootSource::Summit);
	TestTrue(FString::Printf(TEXT("A la primera, la cima da mucho más de lo mejor (%.0f %% frente a %.0f %%)"), FirstSummit * 100.f, FirstChest * 100.f),
		FirstSummit > 0.5f && FirstSummit > FirstChest + 0.3f);
	// A la última no le quita nada: es su tabla.
	TestTrue(TEXT("A la última, la cima es su tabla de siempre"), FMath::IsNearlyEqual(ComebackShare(1.f, ETNRaceLootSource::Summit), ComebackShare(1.f, ETNRaceLootSource::Chest)));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Con mundo: las fortalezas de verdad crean su catapulta potenciada y su cofre de cima
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachSummitPrizeWorldTest,
	"Tortunabo.Beach.SummitPrize.World",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachSummitPrizeWorldTest::RunTest(const FString& Parameters)
{
	const ETNBeachElement Sizes[] = { ETNBeachElement::FortressMedium, ETNBeachElement::FortressLarge, ETNBeachElement::FortressColossal };
	constexpr int32 SeedsPerSize = 6;
	for (const ETNBeachElement Element : Sizes)
	{
		for (int32 s = 0; s < SeedsPerSize; ++s)
		{
			FTNBeachElementSpec Spec;
			Spec.Element = Element;
			Spec.Seed = 4100 + s * 733;
			Spec.SizeScale = 1.f;
			const FString Name = FString::Printf(TEXT("%s, semilla %d"), *UEnum::GetValueAsString(Element), Spec.Seed);
			// Un mundo por fortaleza: lo que se cuenta es solo lo suyo.
			UWorld* World = TNBeachSummitPrizeTest::CreateGameWorld();
			if (!TestNotNull(Name + TEXT(": mundo de prueba"), World))
			{
				return false;
			}
			ATN_BeachElement* Fortress = ATN_BeachElement::SpawnElement(World, FTransform(FVector(0.0, 0.0, 0.0)), Spec);
			if (TestNotNull(Name + TEXT(": se crea la fortaleza"), Fortress))
			{
				// La catapulta de la cima es potenciada y no hay ningún trampolín.
				int32 Catapults = 0;
				int32 BoostedCatapults = 0;
				for (TActorIterator<ATN_BeachCatapult> It(World); It; ++It)
				{
					++Catapults;
					BoostedCatapults += (It->GetSpec().Flags & TNBeach::FlagBoosted) != 0 ? 1 : 0;
				}
				TestEqual(Name + TEXT(": una catapulta arriba"), Catapults, 1);
				TestEqual(Name + TEXT(": potenciada"), BoostedCatapults, 1);
				TestEqual(Name + TEXT(": ni un trampolín"), TNBeachSummitPrizeTest::CountActors<ATN_BeachTrampoline>(World), 0);
				// Y el cofre, el de la cima: da la tabla de los mejores.
				int32 Chests = 0;
				int32 SummitChests = 0;
				for (TActorIterator<ATN_BeachChestSpot> It(World); It; ++It)
				{
					++Chests;
					SummitChests += It->IsSummitPrize() ? 1 : 0;
				}
				TestEqual(Name + TEXT(": un cofre arriba"), Chests, 1);
				TestEqual(Name + TEXT(": es el cofre de la cima"), SummitChests, 1);
			}
			TNBeachSummitPrizeTest::DestroyGameWorld(World);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
