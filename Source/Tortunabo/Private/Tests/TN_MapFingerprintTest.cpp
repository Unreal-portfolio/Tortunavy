// Huella del mapa en todos los modos (#828): cada mapa se monta dos veces en mundos de juego sin ventana y en condiciones
// distintas (calidad gráfica Baja frente a Épica, de una vez frente a por partes, y la segunda vez después de haber montado
// otra cosa, como el anfitrión en la segunda ronda frente al cliente que acaba de entrar). Todo lo que bloquea tiene que dar
// la misma huella (TNMapFingerprint): si no, el cliente vería dunas, rocas o decorado distintos que el anfitrión.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Map.Fingerprint; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Lobby/TN_LobbyValley.h"
#include "Lobby/TN_SandCastleLobby.h"
#include "Lobby/TN_TutorialCourse.h"
#include "Misc/Paths.h"
#include "Rally/TN_RallyTrack.h"
#include "Rally/TN_RallyTrackDressing.h"
#include "Scalability.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "World/ProcMap/TN_ProcMapTypes.h"
#include "World/TN_MapFingerprint.h"
#include "World/TN_MapVariantLoader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNMapFingerprintTest
{
	/** Mundo de juego vacío, sin ventana. */
	struct FTestWorld
	{
		UWorld* World = nullptr;

		explicit FTestWorld(const TCHAR* Name)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		template <class T>
		T* Spawn(const FTransform& Where = FTransform::Identity)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<T>(T::StaticClass(), Where, Params);
		}

		/** Un cargador de variantes con la variante puesta antes de OnConstruction (como ATN_RallyGameState::PrepareTrack). */
		ATN_MapVariantLoader* SpawnLoader(FName Variant)
		{
			ATN_MapVariantLoader* Loader = World->SpawnActorDeferred<ATN_MapVariantLoader>(ATN_MapVariantLoader::StaticClass(),
				FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Loader)
			{
				Loader->Variant = Variant;
				Loader->FinishSpawning(FTransform::Identity);
			}
			return Loader;
		}
	};

	/** Calidad gráfica de todo (0 = Baja, 3 = Épica), como el menú de ajustes; se deja como estaba al salir. */
	struct FScopedQuality
	{
		Scalability::FQualityLevels Original;

		explicit FScopedQuality(int32 Level)
		{
			Original = Scalability::GetQualityLevels();
			Scalability::FQualityLevels Levels = Original;
			Levels.SetFromSingleQualityLevel(Level);
			Scalability::SetQualityLevels(Levels, true);
		}

		~FScopedQuality()
		{
			Scalability::SetQualityLevels(Original, true);
		}
	};

	/** Una variable de consola con otro valor mientras dura. */
	struct FScopedCVar
	{
		IConsoleVariable* Var = nullptr;
		FString Original;

		FScopedCVar(const TCHAR* Name, const TCHAR* Value)
		{
			Var = IConsoleManager::Get().FindConsoleVariable(Name);
			if (Var)
			{
				Original = Var->GetString();
				Var->Set(Value, ECVF_SetByCode);
			}
		}

		~FScopedCVar()
		{
			if (Var) { Var->Set(*Original, ECVF_SetByCode); }
		}
	};

	/**
	 * Las dos huellas iguales; si no, qué clases cambian. Con bLocalOnly, solo lo que cada máquina genera por su cuenta
	 * (los actores replicados los crea el servidor una vez para todas; el botín, por ejemplo, sale al azar en cada partida).
	 */
	bool ExpectSame(FAutomationTestBase& Test, const FString& What, const TNMapFingerprint::FResult& A, const TNMapFingerprint::FResult& B,
		bool bLocalOnly = false)
	{
		Test.AddInfo(FString::Printf(TEXT("%s: %s / %s (generado en cada máquina %s / %s; %d / %d piezas)"), *What, *TNMapFingerprint::ToHex(A.Total),
			*TNMapFingerprint::ToHex(B.Total), *TNMapFingerprint::ToHex(A.Local), *TNMapFingerprint::ToHex(B.Local), A.Pieces, B.Pieces));
		if (!Test.TestTrue(What + TEXT(": hay algo que bloquea"), A.Pieces > 0 && B.Pieces > 0))
		{
			return false;
		}
		if (bLocalOnly ? A.Local == B.Local : A.Total == B.Total)
		{
			return true;
		}
		TMap<FString, const TNMapFingerprint::FCategory*> InB;
		for (const TNMapFingerprint::FCategory& Cat : B.Categories)
		{
			if (!bLocalOnly || !Cat.bReplicated) { InB.Add(Cat.Name, &Cat); }
		}
		for (const TNMapFingerprint::FCategory& Cat : A.Categories)
		{
			if (bLocalOnly && Cat.bReplicated) { continue; }
			const TNMapFingerprint::FCategory* const* Other = InB.Find(Cat.Name);
			if (!Other)
			{
				Test.AddError(FString::Printf(TEXT("%s: %s solo en la primera (%d piezas)"), *What, *Cat.Name, Cat.Pieces));
			}
			else if ((*Other)->Hash != Cat.Hash)
			{
				Test.AddError(FString::Printf(TEXT("%s: %s cambia (%d frente a %d piezas)"), *What, *Cat.Name, Cat.Pieces, (*Other)->Pieces));
			}
			InB.Remove(Cat.Name);
		}
		for (const TPair<FString, const TNMapFingerprint::FCategory*>& Left : InB)
		{
			Test.AddError(FString::Printf(TEXT("%s: %s solo en la segunda (%d piezas)"), *What, *Left.Key, Left.Value->Pieces));
		}
		Test.AddError(What + TEXT(": la huella del mapa cambia"));
		return false;
	}

	bool HasVariant(FName Variant)
	{
		return FPaths::FileExists(FPaths::ProjectDir() / TEXT("Scripts/terrain_volumes/Variants") / Variant.ToString() / TEXT("manifest.json"));
	}

	// ── Coop, Supervivencia y Karts (ATN_ProcMapGenerator) ──

	/** Un mapa del generador del Coop, opcionalmente después de otro (el anfitrión en la segunda ronda). */
	TNMapFingerprint::FResult ProcMapFingerprint(ETNProcGameMode Mode, int32 Seed, int32 SurvivalDifficulty, int32 Quality, int32 SeedBefore)
	{
		FScopedQuality ScopedQuality(Quality);
		FTestWorld Test(TEXT("TNMapFingerprintProcMap"));
		ATN_ProcMapGenerator* Gen = Test.Spawn<ATN_ProcMapGenerator>();
		if (!Gen) { return TNMapFingerprint::FResult(); }
		Gen->SetSettingsIfMissing(LoadObject<UTN_ProcMapSettings>(nullptr, TEXT("/Game/ProcMap/DA_ProcMapSettings.DA_ProcMapSettings"), nullptr, LOAD_NoWarn));
		auto Generate = [&](int32 InSeed)
		{
			if (Mode == ETNProcGameMode::Survival) { Gen->ServerGenerateSurvival(InSeed, SurvivalDifficulty); }
			else { Gen->ServerGenerate(InSeed, Mode, ETNProcDifficulty::Normal); }
		};
		if (SeedBefore != 0) { Generate(SeedBefore); }
		Generate(Seed);
		return Gen->IsMapReady() ? TNMapFingerprint::Compute(Test.World) : TNMapFingerprint::FResult();
	}

	// ── Carrera en la playa (ATN_BeachRaceGenerator) ──

	/**
	 * Una ronda de la playa montada entera. bAtOnce: el reparto y todo lo demás en el mismo fotograma; si no, el reparto en
	 * otro hilo y el resto por partes con 0,5 ms por fotograma (como un PC lento). SeedBefore: otra ronda antes (sus asientos
	 * y su decorado se tienen que ir del todo).
	 */
	TNMapFingerprint::FResult BeachFingerprint(int32 Seed, int32 Quality, bool bAtOnce, int32 SeedBefore)
	{
		FScopedQuality ScopedQuality(Quality);
		FScopedCVar Async(TEXT("TN.Beach.AsyncBuild"), bAtOnce ? TEXT("0") : TEXT("1"));
		FScopedCVar Budget(TEXT("TN.Beach.BuildBudgetMs"), bAtOnce ? TEXT("6") : TEXT("0.5"));
		FTestWorld Test(TEXT("TNMapFingerprintBeach"));
		ATN_BeachRaceGenerator* Gen = Test.Spawn<ATN_BeachRaceGenerator>();
		if (!Gen) { return TNMapFingerprint::FResult(); }
		auto Build = [Gen](int32 InSeed)
		{
			Gen->GenerateRound(InSeed);
			const double Start = FPlatformTime::Seconds();
			while (!Gen->IsRoundReady())
			{
				if (FPlatformTime::Seconds() - Start > 300.0) { return false; }
				Gen->Tick(1.f / 30.f);
				FPlatformProcess::Sleep(0.001f);
			}
			return true;
		};
		if ((SeedBefore != 0 && !Build(SeedBefore)) || !Build(Seed))
		{
			return TNMapFingerprint::FResult();
		}
		return TNMapFingerprint::Compute(Test.World);
	}

	// ── Rally (ATN_MapVariantLoader + ATN_RallyTrack + ATN_RallyTrackDressing) ──

	/**
	 * Terreno, pista y decorado de un circuito. Con bLateActor, antes del decorado hay una losa con colisión que el servidor
	 * habría creado en partida y replicado (a un cliente le llega cuando le llega): el decorado no se apoya en ella.
	 */
	TNMapFingerprint::FResult RallyFingerprint(FName Variant, int32 Quality, bool bLateActor)
	{
		FScopedQuality ScopedQuality(Quality);
		FTestWorld Test(TEXT("TNMapFingerprintRally"));
		Test.SpawnLoader(Variant);
		ATN_RallyTrack* Track = Test.Spawn<ATN_RallyTrack>();
		if (!Track || !Track->BuildFromVariant(Variant)) { return TNMapFingerprint::FResult(); }
		AActor* Slab = nullptr;
		if (bLateActor)
		{
			const TNRallyDressing::FTrackData Data = TNRallyDressing::SampleTrack(*Track, 500.0);
			if (Data.Samples.Num() > 0)
			{
				// 200 m de lado, un metro por encima de la salida: la tapa entera para las sondas de suelo de alrededor.
				Slab = Test.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Data.Samples[0].Location + FVector(0.0, 0.0, 100.0)));
				UBoxComponent* Box = NewObject<UBoxComponent>(Slab, TEXT("Slab"));
				Box->SetBoxExtent(FVector(10000.0, 10000.0, 20.0));
				Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
				Slab->SetRootComponent(Box);
				Box->RegisterComponent();
				Slab->SetActorLocation(Data.Samples[0].Location + FVector(0.0, 0.0, 100.0));
				Slab->SetReplicates(true);
			}
		}
		ATN_RallyTrackDressing* Dressing = Test.Spawn<ATN_RallyTrackDressing>();
		if (!Dressing || !Dressing->BuildFromTrack(Track, static_cast<int32>(FCrc::StrCrc32(*Variant.ToString()))))
		{
			return TNMapFingerprint::FResult();
		}
		TNMapFingerprint::FOptions Options;
		Options.Filter = [Slab](const AActor* Actor) { return Actor != Slab; };
		return TNMapFingerprint::Compute(Test.World, Options);
	}

	// ── Variantes del disco (Rally y Todos contra Todos) ──

	/** Un cargador en partida con Variant; con Before, primero monta esa (la guardada en el nivel) y luego cambia. */
	TNMapFingerprint::FResult VariantFingerprint(FName Variant, FName Before, int32 Quality)
	{
		FScopedQuality ScopedQuality(Quality);
		FTestWorld Test(TEXT("TNMapFingerprintVariant"));
		ATN_MapVariantLoader* Loader = Test.SpawnLoader(Before.IsNone() ? Variant : Before);
		if (!Loader) { return TNMapFingerprint::FResult(); }
		// Partida empezada, como en el juego: lo que el cargador cree después también hace su BeginPlay (sus mallas y colisión).
		Test.World->SetBegunPlay(true);
		Loader->DispatchBeginPlay();
		if (!Before.IsNone())
		{
			// Como un cliente: la variante de la partida llega por la red después de su BeginPlay.
			Loader->Variant = Variant;
			Loader->Recargar();
		}
		return TNMapFingerprint::Compute(Test.World);
	}

	// ── Lobby ──

	/** El castillo, el valle y el recorrido del tutorial; con bTutorialFirst, el recorrido antes que lo demás. */
	TNMapFingerprint::FResult LobbyFingerprint(int32 Quality, bool bTutorialFirst)
	{
		FScopedQuality ScopedQuality(Quality);
		FTestWorld Test(TEXT("TNMapFingerprintLobby"));
		auto Tutorial = [&Test]()
		{
			if (ATN_TutorialCourse* Course = ATN_TutorialCourse::SpawnAbove(Test.World, FVector(3000.0, 0.0, 0.0), -90.f))
			{
				Course->EnsureBuilt();
			}
		};
		if (bTutorialFirst) { Tutorial(); }
		Test.Spawn<ATN_SandCastleLobby>();
		Test.Spawn<ATN_LobbyValley>();
		if (!bTutorialFirst) { Tutorial(); }
		return TNMapFingerprint::Compute(Test.World);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMapFingerprintProcMapTest,
	"Tortunabo.Map.Fingerprint.ProcMap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMapFingerprintProcMapTest::RunTest(const FString& Parameters)
{
	using namespace TNMapFingerprintTest;
	struct FCase { const TCHAR* Name; ETNProcGameMode Mode; int32 Seed; int32 SurvivalDifficulty; };
	const FCase Cases[] = {
		{ TEXT("Coop"), ETNProcGameMode::Coop, 21, 0 },
		{ TEXT("Supervivencia"), ETNProcGameMode::Survival, 7, 3 },
		{ TEXT("Karts"), ETNProcGameMode::Karts, 11, 0 },
	};
	for (const FCase& C : Cases)
	{
		const TNMapFingerprint::FResult Epic = ProcMapFingerprint(C.Mode, C.Seed, C.SurvivalDifficulty, 3, 0);
		const TNMapFingerprint::FResult LowAfter = ProcMapFingerprint(C.Mode, C.Seed, C.SurvivalDifficulty, 0, C.Seed + 1000);
		ExpectSame(*this, FString::Printf(TEXT("%s, semilla %d"), C.Name, C.Seed), Epic, LowAfter);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMapFingerprintBeachTest,
	"Tortunabo.Map.Fingerprint.Beach",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMapFingerprintBeachTest::RunTest(const FString& Parameters)
{
	using namespace TNMapFingerprintTest;
	// De una vez y en Épica, frente a por partes, en Baja y después de otra ronda (los asientos y el decorado de la anterior
	// se van del todo). Lo replicado (elementos y botín) lo crea el servidor una vez para todas: cuenta lo de cada máquina.
	const TNMapFingerprint::FResult AtOnce = BeachFingerprint(42, 3, true, 0);
	ExpectSame(*this, TEXT("Playa, semilla 42, por partes y en Baja"), AtOnce, BeachFingerprint(42, 0, false, 0), true);
	ExpectSame(*this, TEXT("Playa, semilla 42, después de la ronda de la semilla 7"), AtOnce, BeachFingerprint(42, 3, true, 7), true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMapFingerprintRallyTest,
	"Tortunabo.Map.Fingerprint.Rally",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMapFingerprintRallyTest::RunTest(const FString& Parameters)
{
	using namespace TNMapFingerprintTest;
	const FName Variant(TEXT("R01_circuito_dunas"));
	if (!HasVariant(Variant))
	{
		AddWarning(TEXT("Sin Scripts/terrain_volumes/Variants/R01_circuito_dunas (build cocinada): se salta."));
		return true;
	}
	const TNMapFingerprint::FResult Plain = RallyFingerprint(Variant, 3, false);
	const TNMapFingerprint::FResult WithLateActor = RallyFingerprint(Variant, 0, true);
	ExpectSame(*this, TEXT("Rally R01"), Plain, WithLateActor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMapFingerprintVariantTest,
	"Tortunabo.Map.Fingerprint.Variant",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMapFingerprintVariantTest::RunTest(const FString& Parameters)
{
	using namespace TNMapFingerprintTest;
	// C01_camino trae placements (decorado con colisión); A01_diana es la arena guardada en LVL_Tct.
	const FName Camino(TEXT("C01_camino"));
	const FName Diana(TEXT("A01_diana"));
	if (!HasVariant(Camino) || !HasVariant(Diana))
	{
		AddWarning(TEXT("Sin las variantes C01_camino y A01_diana (build cocinada): se salta."));
		return true;
	}
	const TNMapFingerprint::FResult Direct = VariantFingerprint(Camino, NAME_None, 3);
	const TNMapFingerprint::FResult AfterOther = VariantFingerprint(Camino, Diana, 0);
	ExpectSame(*this, TEXT("Variante C01 (directa / tras montar A01)"), Direct, AfterOther);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMapFingerprintLobbyTest,
	"Tortunabo.Map.Fingerprint.Lobby",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMapFingerprintLobbyTest::RunTest(const FString& Parameters)
{
	using namespace TNMapFingerprintTest;
	const TNMapFingerprint::FResult Epic = LobbyFingerprint(3, false);
	const TNMapFingerprint::FResult LowTutorialFirst = LobbyFingerprint(0, true);
	ExpectSame(*this, TEXT("Lobby"), Epic, LowTutorialFirst);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMapFingerprintSelfTest,
	"Tortunabo.Map.Fingerprint.Detects",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMapFingerprintSelfTest::RunTest(const FString& Parameters)
{
	using namespace TNMapFingerprintTest;
	// La huella ve lo que importa: una caja que bloquea 2 cm más allá cambia la huella; una que solo solapa, no.
	auto OneBox = [](const FVector& Where, bool bBlocks, bool bExtraTrigger)
	{
		FTestWorld Test(TEXT("TNMapFingerprintSelf"));
		auto Add = [&Test](const FVector& At, FName Profile)
		{
			AActor* Actor = Test.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(At));
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Box"));
			Box->SetBoxExtent(FVector(100.0));
			Box->SetCollisionProfileName(Profile);
			Actor->SetRootComponent(Box);
			Box->RegisterComponent();
			Actor->SetActorLocation(At);
		};
		Add(Where, bBlocks ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
		if (bExtraTrigger) { Add(FVector(500.0, 0.0, 0.0), TEXT("OverlapAll")); }
		return TNMapFingerprint::Compute(Test.World);
	};
	const TNMapFingerprint::FResult Base = OneBox(FVector::ZeroVector, true, false);
	TestEqual(TEXT("Una caja que bloquea es una pieza"), Base.Pieces, 1);
	TestNotEqual(TEXT("Movida 2 cm, otra huella"), OneBox(FVector(2.0, 0.0, 0.0), true, false).Total, Base.Total);
	TestEqual(TEXT("Un disparador más no cambia la huella"), OneBox(FVector::ZeroVector, true, true).Total, Base.Total);
	TestEqual(TEXT("Sin colisión no hay piezas"), OneBox(FVector::ZeroVector, false, false).Pieces, 0);
	return true;
}

#endif
