// Trampas del catálogo de Supervivencia sobre el mapa generado (#516). Dónde va cada una lo decide
// TNSurvivalCatalog::PlaceLooseTraps (TN_SurvivalTrapPlacement.h); aquí se crean los actores del Clásico.

#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "Core/TN_Log.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "World/TN_BananaPeel.h"
#include "World/TN_CrabActor.h"
#include "World/TN_CrabSpawnZone.h"
#include "World/TN_EnemySeagull.h"
#include "World/TN_JellyfishActor.h"
#include "World/TN_SeagullSpawnZone.h"
#include "World/TN_SlowZoneVolume.h"
#include "World/TN_UmbrellaInteractable.h"
#include "World/ProcMap/TN_ProcMapSurvival.h"

namespace
{
	/** El Blueprint de la trampa (con sus mallas y ajustes) o, si no carga, la clase de C++. */
	template <typename T>
	UClass* TrapClass(const TCHAR* Path)
	{
		UClass* Class = LoadClass<T>(nullptr, Path);
		return Class ? Class : T::StaticClass();
	}

	const TCHAR* BananaPath = TEXT("/Game/Blueprints/Gameplay/Hazards/BP_BananaPeel.BP_BananaPeel_C");
	const TCHAR* SlowZonePath = TEXT("/Game/Blueprints/Gameplay/Hazards/BP_SlowZoneVolume.BP_SlowZoneVolume_C");
	const TCHAR* JellyfishPath = TEXT("/Game/Blueprints/Gameplay/Items/BP_JellyfishActor.BP_JellyfishActor_C");
	const TCHAR* CrabZonePath = TEXT("/Game/Blueprints/Gameplay/Enemies/Crabs/BP_CrabSpawnZone.BP_CrabSpawnZone_C");
	const TCHAR* CrabPath = TEXT("/Game/Blueprints/Gameplay/Enemies/Crabs/BP_CrabActor.BP_CrabActor_C");
	const TCHAR* SeagullZonePath = TEXT("/Game/Blueprints/Gameplay/Enemies/Seagull/BP_SeagullSpawnZone.BP_SeagullSpawnZone_C");
	const TCHAR* SeagullPath = TEXT("/Game/Blueprints/Gameplay/Enemies/Seagull/BP_EnemySeagull.BP_EnemySeagull_C");
	const TCHAR* UmbrellaPath = TEXT("/Game/Blueprints/Gameplay/Interaction/BP_UmbrellaInteractable.BP_UmbrellaInteractable_C");

	/** Color del marcador de cada trampa (Debug Draw). El camino va en amarillo y las ramas en naranja. */
	FColor MarkerColor(const TNSurvivalCatalog::FTrapPlacement& P)
	{
		using TNSurvivalCatalog::ETrap;
		if (P.bUmbrella) { return FColor::Cyan; }
		switch (P.Trap)
		{
			case ETrap::BananaPeel: return FColor::Green;
			case ETrap::SlowZone: return FColor::Blue;
			case ETrap::Jellyfish: return FColor::Magenta;
			case ETrap::Crab: return FColor::Red;
			case ETrap::Seagull: return FColor::White;
			default: return FColor::Black;
		}
	}
}

void ATN_ProcMapGenerator::PlanSurvivalTraps()
{
	SurvivalTrapPlan.Reset();
	if (NetConfig.Mode != ETNProcGameMode::Survival)
	{
		return;
	}
	// La misma dificultad con la que BuildLayout genera el mapa.
	const int32 Difficulty = NetConfig.SurvivalDifficulty > 0 ? NetConfig.SurvivalDifficulty : 1 + 2 * static_cast<int32>(NetConfig.Difficulty);
	const uint32 Seed = static_cast<uint32>(NetConfig.Seed);
	const TNSurvivalCatalog::FMapEntry* Entry = TNSurvivalCatalog::FindMap(Seed);
	if (!Entry || Entry->Difficulty != Difficulty)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Supervivencia] Semilla %u con dificultad %d fuera del catálogo: mapa sin trampas."), Seed, Difficulty);
		return;
	}
	SurvivalTrapPlan = TNSurvivalCatalog::PlaceLooseTraps(Layout, Seed);
	UE_LOG(LogTortunabo, Log, TEXT("[Supervivencia] Mapa del catálogo «%s» (semilla %u, dificultad %d): %d trampas."),
		Entry->Name, Seed, Difficulty, SurvivalTrapPlan.Num());
}

void ATN_ProcMapGenerator::SpawnSurvivalTraps()
{
	using namespace TNSurvivalCatalog;
	UWorld* World = GetWorld();
	if (!World || SurvivalTrapPlan.Num() == 0)
	{
		return;
	}
	// Cáscaras, medusas y sombrillas se replican; las zonas de cangrejos y gaviotas son lógica de servidor. Las
	// zonas lentas no se replican y frenan en cada máquina a su tortuga: las crea cada una, como hacían los chunks.
	const bool bServer = World->GetNetMode() != NM_Client;
	const double Yaw0 = GetActorRotation().Yaw;

	for (const FTrapPlacement& P : SurvivalTrapPlan)
	{
		const FVector2D Where(P.Location.X, P.Location.Y);
		const double Ground = TerrainHeightMap(Where);
		const FRotator Rot(0.0, P.YawDeg + Yaw0, 0.0);

		if (P.bUmbrella)
		{
			if (bServer) { SpawnMapActor(TrapClass<ATN_UmbrellaInteractable>(UmbrellaPath), FTransform(Rot, MapToWorld2D(Where, Ground)), true); }
			continue;
		}
		switch (P.Trap)
		{
			case ETrap::BananaPeel:
				if (bServer) { SpawnMapActor(TrapClass<ATN_BananaPeel>(BananaPath), FTransform(Rot, MapToWorld2D(Where, Ground)), true); }
				break;
			case ETrap::Jellyfish:
				if (bServer) { SpawnMapActor(TrapClass<ATN_JellyfishActor>(JellyfishPath), FTransform(Rot, MapToWorld2D(Where, Ground)), true); }
				break;
			case ETrap::SlowZone:
				if (ATN_SlowZoneVolume* Zone = Cast<ATN_SlowZoneVolume>(SpawnMapActor(TrapClass<ATN_SlowZoneVolume>(SlowZonePath),
					FTransform(Rot, MapToWorld2D(Where, Ground + P.Extent.Z)), false)))
				{
					Zone->SetZoneExtent(P.Extent);
				}
				break;
			case ETrap::Crab:
				if (!bServer) { break; }
				if (ATN_CrabSpawnZone* Zone = Cast<ATN_CrabSpawnZone>(SpawnMapActor(TrapClass<ATN_CrabSpawnZone>(CrabZonePath),
					FTransform(Rot, MapToWorld2D(Where, Ground + P.Extent.Z)), true)))
				{
					Zone->ConfigureZone(P.Extent, P.Count, LoadClass<ATN_CrabActor>(nullptr, CrabPath));
				}
				break;
			case ETrap::Seagull:
				// La zona va en los ejes del mapa (cubre el tramo entero).
				if (!bServer) { break; }
				if (ATN_SeagullSpawnZone* Zone = Cast<ATN_SeagullSpawnZone>(SpawnMapActor(TrapClass<ATN_SeagullSpawnZone>(SeagullZonePath),
					FTransform(FRotator(0.0, Yaw0, 0.0), MapToWorld2D(Where, P.Location.Z + P.Extent.Z)), true)))
				{
					Zone->SetZoneExtent(P.Extent);
					Zone->SetSeagullClassIfMissing(LoadClass<ATN_EnemySeagull>(nullptr, SeagullPath));
				}
				break;
			default:
				break;
		}
	}
}

void ATN_ProcMapGenerator::DrawSurvivalTrapPlan() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Yaw0 = GetActorRotation().Yaw;
	for (const TNSurvivalCatalog::FTrapPlacement& P : SurvivalTrapPlan)
	{
		const FVector2D Where(P.Location.X, P.Location.Y);
		const FColor Color = MarkerColor(P);
		if (!P.Extent.IsNearlyZero())
		{
			// Zonas: su caja (las de gaviotas, en los ejes del mapa).
			const double Ground = P.Trap == TNSurvivalCatalog::ETrap::Seagull ? P.Location.Z : TerrainHeightMap(Where);
			const FQuat Rot = FRotator(0.0, P.YawDeg + Yaw0, 0.0).Quaternion();
			DrawDebugBox(World, MapToWorld2D(Where, Ground + P.Extent.Z), P.Extent, Rot, Color, true, -1.f, 0, 20.f);
		}
		else
		{
			// Obstáculos: una esfera de su tamaño y un poste para verla desde lejos.
			const double Ground = TerrainHeightMap(Where);
			const FVector Base = MapToWorld2D(Where, Ground);
			DrawDebugSphere(World, Base + FVector(0, 0, 100), FMath::Max(80.0, TNSurvivalCatalog::ObstacleRadius(P)), 12, Color, true, -1.f, 0, 10.f);
			DrawDebugLine(World, Base, Base + FVector(0, 0, 800), Color, true, -1.f, 0, 15.f);
		}
	}
}
