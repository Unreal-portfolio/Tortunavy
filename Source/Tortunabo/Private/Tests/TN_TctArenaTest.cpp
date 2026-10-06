// Arena de Todos contra Todos con datos de verdad (#651): carga A01_diana de Scripts/terrain_volumes/Variants en un mundo de
// juego sin ventana, la mide (ATN_TctArena::Survey) y comprueba lo que sale para el modo: escalones del agua y salidas.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tct; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/TN_TctRules.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "World/TN_TctArena.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctArenaDianaSurveyTest,
	"Tortunabo.Tct.Arena.DianaSurvey",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctArenaDianaSurveyTest::RunTest(const FString& Parameters)
{
	const FName Diana(TEXT("A01_diana"));
	if (!ATN_TctArena::VariantExists(Diana))
	{
		AddWarning(TEXT("Sin Scripts/terrain_volumes/Variants/A01_diana (build cocinada): se salta."));
		return true;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNTctArenaTestWorld"));
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
		// Como la de LVL_Tct al cargarse en partida: con la variante ya puesta en el nivel y sin malla (es transitoria).
		Arena->Variant = Diana;
		Arena->ServerSetArenaVariant(Diana);
		TestEqual(TEXT("Variante replicada"), Arena->GetArenaVariant(), Diana);
		// Medir en el mismo fotograma de la carga (como en StartPlay): la colisión se cocina síncrona.
		TestTrue(TEXT("Hay suelo pisable"), Arena->Survey(250.f));
		TestEqual(TEXT("Mar de la diana a -4 m"), Arena->GetBaseWaterZ(), -400.f);

		const TArray<float> Levels = TNTctRules::ComputeFloodLevels(Arena->GetSurveyHeights(), 4, 0.08f, 150.f, 60.f);
		AddInfo(FString::Printf(TEXT("Muestras: %d · escalones: %d"), Arena->GetSurveyHeights().Num(), Levels.Num()));
		for (const float Level : Levels)
		{
			AddInfo(FString::Printf(TEXT("Escalón a %.0f"), Level));
		}
		// Anillos a +2, +4, +6, +9 y +12 m sobre el mar (-200, 0, 200, 500 y 800): se inundan los de fuera y queda el centro.
		TestTrue(TEXT("Dos o tres escalones (anillos de fuera)"), Levels.Num() >= 2 && Levels.Num() <= 3);
		if (Levels.Num() > 0)
		{
			TestTrue(TEXT("El primero cubre el anillo exterior (-200) y no el segundo (0)"), Levels[0] > -200.f && Levels[0] < -20.f);
			TestTrue(TEXT("El último deja seco el centro (+12 m)"), Levels.Last() < 800.f);
		}

		const TArray<FTransform> Spawns = Arena->PickSpawnTransforms(TNTctRules::MaxPlayers, 120.f);
		TestEqual(TEXT("Ocho salidas"), Spawns.Num(), TNTctRules::MaxPlayers);
		float Closest = TNumericLimits<float>::Max();
		for (int32 A = 0; A < Spawns.Num(); ++A)
		{
			for (int32 B = A + 1; B < Spawns.Num(); ++B)
			{
				Closest = FMath::Min(Closest, static_cast<float>(FVector::Dist2D(Spawns[A].GetLocation(), Spawns[B].GetLocation())));
			}
		}
		AddInfo(FString::Printf(TEXT("Salidas: la más cercana a otra, a %.0f uu"), Closest));
		TestTrue(TEXT("Salidas separadas (más de 10 m entre dos)"), Closest > 1000.f);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Arena de playa en el terreno (#779)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctArenaSandMaterialTest,
	"Tortunabo.Tct.Arena.SandMaterial",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctArenaSandMaterialTest::RunTest(const FString& Parameters)
{
	const FString SandPath = TEXT("/Game/Blueprints/Gameplay/GridMap/M_GridTerrainWet.M_GridTerrainWet");
	TestEqual(TEXT("La arena de TcT usa la arena de playa del Rally y del Coop"), FString(ATN_TctArena::SandMaterialPath()), SandPath);
	UMaterialInterface* Sand = LoadObject<UMaterialInterface>(nullptr, *SandPath);
	if (!TestNotNull(TEXT("El material de arena está en el proyecto"), Sand))
	{
		return false;
	}
	const UMaterialInterface* ArenaDefault = GetDefault<ATN_TctArena>()->TerrainMaterial;
	TestEqual(TEXT("ATN_TctArena nace con la arena de playa"), ArenaDefault ? ArenaDefault->GetPathName() : FString(), SandPath);
	const UMaterialInterface* LoaderDefault = GetDefault<ATN_MapVariantLoader>()->TerrainMaterial;
	TestTrue(TEXT("El cargador de los demás modos conserva su material"), LoaderDefault && LoaderDefault->GetPathName() != SandPath);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNTctArenaSandWorld"));
	if (!TestNotNull(TEXT("Mundo de prueba"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	// Todas las arenas de TcT (también las que llegan con ?Arena=), aunque la arena del nivel traiga otro material.
	const TCHAR* const Arenas[] = { TEXT("A01_diana"), TEXT("N01_coliseo"), TEXT("N02_anfiteatro"), TEXT("N03_volcan_arena"),
		TEXT("N04_atolon"), TEXT("N17_fortaleza_estrella"), TEXT("N18_yin_yang"), TEXT("A07_panal_piramide"), TEXT("A08_panal_roto"),
		TEXT("A09_colmena"), TEXT("A10_ajedrez"), TEXT("A11_zigurat"), TEXT("A12_damas") };
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_TctArena* Arena = World->SpawnActor<ATN_TctArena>(ATN_TctArena::StaticClass(), FTransform::Identity, Params);
	if (TestNotNull(TEXT("Arena"), Arena))
	{
		Arena->TerrainMaterial = const_cast<UMaterialInterface*>(LoaderDefault);
		int32 Checked = 0;
		for (const TCHAR* Name : Arenas)
		{
			if (!ATN_TctArena::VariantExists(FName(Name)))
			{
				AddWarning(FString::Printf(TEXT("Sin la variante %s: se salta."), Name));
				continue;
			}
			Arena->ServerSetArenaVariant(FName(Name));
			TArray<UProceduralMeshComponent*> Meshes;
			Arena->GetComponents(Meshes);
			int32 Sandy = 0;
			for (const UProceduralMeshComponent* Mesh : Meshes)
			{
				Sandy += (Mesh && Mesh->GetMaterial(0) == Sand) ? 1 : 0;
			}
			TestTrue(FString::Printf(TEXT("%s tiene terreno"), Name), Meshes.Num() > 0);
			TestEqual(FString::Printf(TEXT("%s: todo el terreno con arena de playa"), Name), Sandy, Meshes.Num());
			++Checked;
		}
		AddInfo(FString::Printf(TEXT("Arenas comprobadas: %d"), Checked));
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
