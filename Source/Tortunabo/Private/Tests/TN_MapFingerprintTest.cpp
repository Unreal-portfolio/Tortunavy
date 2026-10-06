// Huella del mapa (#828): cada mapa se monta dos veces en mundos de juego sin ventana y en condiciones
// distintas (calidad gráfica Baja frente a Épica, de una vez frente a por partes, y la segunda vez después de haber montado
// otra cosa, como el anfitrión en la segunda ronda frente al cliente que acaba de entrar). Todo lo que bloquea tiene que dar
// la misma huella (TNMapFingerprint): si no, el cliente vería dunas, rocas o decorado distintos que el anfitrión.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Map.Fingerprint; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Lobby/TN_LobbyValley.h"
#include "Lobby/TN_SandCastleLobby.h"
#include "Lobby/TN_TutorialCourse.h"
#include "Misc/Paths.h"
#include "Scalability.h"
#include "World/TN_MapBlockerRelevance.h"
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

		/** Un cargador de variantes con la variante puesta antes de OnConstruction (como en partida). */
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

	// ── Variantes del disco (el camino del modo único) ──

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMapFingerprintVariantTest,
	"Tortunabo.Map.Fingerprint.Variant",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMapFingerprintVariantTest::RunTest(const FString& Parameters)
{
	using namespace TNMapFingerprintTest;
	// C01_camino trae placements (decorado con colisión). Montada de una vez en Épica, frente a montada, recargada (como un
	// cliente al que la variante le llega por la red después de su BeginPlay) y en Baja.
	const FName Camino(TEXT("C01_camino"));
	if (!HasVariant(Camino))
	{
		AddWarning(TEXT("Sin la variante C01_camino (build cocinada): se salta."));
		return true;
	}
	const TNMapFingerprint::FResult Direct = VariantFingerprint(Camino, NAME_None, 3);
	const TNMapFingerprint::FResult Reloaded = VariantFingerprint(Camino, Camino, 0);
	ExpectSame(*this, TEXT("Variante C01 (directa / recargada)"), Direct, Reloaded);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMapBlockerRelevanceTest,
	"Tortunabo.Map.Fingerprint.BlockerRelevance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMapBlockerRelevanceTest::RunTest(const FString& Parameters)
{
	using namespace TNMapFingerprintTest;
	// Un actor replicado creado en partida que bloquea pasa a ser siempre relevante (el cliente lo tiene antes de chocar);
	// uno que solo solapa, uno con movimiento replicado o uno sin replicar, no.
	FTestWorld Test(TEXT("TNMapBlockerRelevance"));
	auto Make = [&Test](FName Profile, bool bReplicates, bool bReplicateMovement)
	{
		AActor* Actor = Test.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Box"));
		Box->SetBoxExtent(FVector(100.0));
		Box->SetCollisionProfileName(Profile);
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		Actor->SetReplicates(bReplicates);
		Actor->SetReplicateMovement(bReplicateMovement);
		return Actor;
	};
	AActor* Wall = Make(UCollisionProfile::BlockAll_ProfileName, true, false);
	TestTrue(TEXT("Muro replicado: siempre relevante"), UTN_MapBlockerRelevanceSubsystem::KeepRelevant(Wall) && Wall->bAlwaysRelevant);
	AActor* Trigger = Make(TEXT("OverlapAll"), true, false);
	TestFalse(TEXT("Disparador: no"), UTN_MapBlockerRelevanceSubsystem::KeepRelevant(Trigger) || Trigger->bAlwaysRelevant);
	AActor* Mover = Make(UCollisionProfile::BlockAll_ProfileName, true, true);
	TestFalse(TEXT("Con movimiento replicado: no"), UTN_MapBlockerRelevanceSubsystem::KeepRelevant(Mover) || Mover->bAlwaysRelevant);
	AActor* Local = Make(UCollisionProfile::BlockAll_ProfileName, false, false);
	TestFalse(TEXT("Sin replicar: no"), UTN_MapBlockerRelevanceSubsystem::KeepRelevant(Local) || Local->bAlwaysRelevant);
	return true;
}

#endif
