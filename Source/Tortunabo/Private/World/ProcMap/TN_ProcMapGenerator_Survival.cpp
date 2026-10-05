// Trampas del catálogo de Supervivencia sobre el mapa generado (#516 y #517). Dónde va cada una lo deciden
// TNSurvivalCatalog::PlaceLooseTraps y PlaceTerrainTraps (TN_SurvivalTrapPlacement.h); aquí se crean los actores.

#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "Core/TN_Log.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "World/TN_BananaPeel.h"
#include "World/TN_CrabActor.h"
#include "World/TN_CrabSpawnZone.h"
#include "World/TN_EnemySeagull.h"
#include "World/TN_JellyfishActor.h"
#include "World/TN_PressurePlate.h"
#include "World/TN_QuadActor.h"
#include "World/TN_SeagullSpawnZone.h"
#include "World/TN_SlowZoneVolume.h"
#include "World/TN_UmbrellaInteractable.h"
#include "World/Beach/TN_BeachCreatureRules.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachShelterVolume.h"
#include "World/Beach/TN_BeachTankTrap.h"
#include "World/TN_Quicksand.h"
#include "World/ProcMap/TN_ProcMapSurvival.h"
#include "World/ProcMap/TN_ProcMapTypes.h"
#include "World/ProcMap/TN_ProcPuzzleActors.h"
#include "World/ProcMap/TN_ProcSurvivalTraps.h"

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
	const TCHAR* QuadPath = TEXT("/Game/Blueprints/Gameplay/Enemies/Quad/BP_QuadActor.BP_QuadActor_C");
	const TCHAR* PlatePath = TEXT("/Game/Blueprints/Gameplay/Interaction/BP_PressurePlate.BP_PressurePlate_C");

	/** Tamaño (SizeScale de la huella de la carrera) de cada criatura del Excel en los caminos de Supervivencia (3-6 m). */
	float SurvivalSizeScale(TNSurvivalCatalog::ETrap Trap)
	{
		using TNSurvivalCatalog::ETrap;
		switch (Trap)
		{
			case ETrap::DragCrab:     return 0.9f;
			case ETrap::BurrowCrab:   return 0.7f;
			case ETrap::UrchinSpikes: return 0.5f;
			case ETrap::TrashPile:    return 0.45f;
			case ETrap::Trench:       return 0.5f;
			default:                  return 1.f;
		}
	}

	ETNBeachElement BeachElementOf(TNSurvivalCatalog::ETrap Trap)
	{
		using TNSurvivalCatalog::ETrap;
		switch (Trap)
		{
			case ETrap::DragCrab:     return ETNBeachElement::DragCrab;
			case ETrap::BurrowCrab:   return ETNBeachElement::BurrowCrab;
			case ETrap::UrchinSpikes: return ETNBeachElement::UrchinSpikes;
			case ETrap::TrashPile:    return ETNBeachElement::TrashPile;
			case ETrap::Trench:       return ETNBeachElement::Trench;
			default:                  return ETNBeachElement::Count;
		}
	}

	/** Alto de la compuerta del atajo (cm): no se salta. */
	constexpr float ShortcutGateHeight = 350.f;

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

int32 ATN_ProcMapGenerator::GetSurvivalTrapCount(FString* OutBreakdown) const
{
	using TNSurvivalCatalog::ETrap;
	// Por tipo, en el orden del enum; las sombrillas protegen de las gaviotas: no son trampas.
	TMap<ETrap, int32> ByTrap;
	for (const TNSurvivalCatalog::FTrapPlacement& P : SurvivalTrapPlan)
	{
		if (!P.bUmbrella) { ByTrap.FindOrAdd(P.Trap)++; }
	}
	if (SurvivalTerrainPlan.Quads.Num() > 0) { ByTrap.Add(ETrap::Quad, SurvivalTerrainPlan.Quads.Num()); }
	if (SurvivalTerrainPlan.Bridges.Num() > 0) { ByTrap.Add(ETrap::BreakableBridge, SurvivalTerrainPlan.Bridges.Num()); }

	int32 Total = 0;
	TArray<FString> Parts;
	ByTrap.KeySort([](ETrap A, ETrap B) { return static_cast<uint8>(A) < static_cast<uint8>(B); });
	for (const TPair<ETrap, int32>& Pair : ByTrap)
	{
		Total += Pair.Value;
		Parts.Add(FString::Printf(TEXT("%d %s"), Pair.Value, TNSurvivalCatalog::TrapName(Pair.Key)));
	}
	if (OutBreakdown)
	{
		*OutBreakdown = Parts.Num() > 0 ? FString::Join(Parts, TEXT(", ")) : FString(TEXT("ninguna"));
		if (SurvivalTerrainPlan.Shortcuts.Num() > 0)
		{
			*OutBreakdown += FString::Printf(TEXT("; y %d atajos con placas"), SurvivalTerrainPlan.Shortcuts.Num());
		}
	}
	return Total;
}

void ATN_ProcMapGenerator::PlanSurvivalTraps()
{
	SurvivalTrapPlan.Reset();
	SurvivalTerrainPlan = TNSurvivalCatalog::FTerrainTrapPlan();
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
	// Con la densidad de trampas de la dificultad elegida (#730): la replica NetConfig, así todas colocan las mismas.
	const int32 DensityPct = NetConfig.SurvivalTrapDensityPct > 0 ? NetConfig.SurvivalTrapDensityPct : 100;
	SurvivalTrapPlan = TNSurvivalCatalog::PlaceLooseTraps(Layout, Seed, DensityPct);
	SurvivalTerrainPlan = TNSurvivalCatalog::PlaceTerrainTraps(Layout, Seed, DensityPct);
	FString Breakdown;
	const int32 TrapCount = GetSurvivalTrapCount(&Breakdown);
	UE_LOG(LogTortunabo, Log, TEXT("[Supervivencia] Mapa del catálogo «%s» (semilla %u, dificultad %d, trampas al %d %%): %d trampas (%s)."),
		Entry->Name, Seed, Difficulty, DensityPct, TrapCount, *Breakdown);
}

bool ATN_ProcMapGenerator::IsSurvivalBreakableGap(int32 Feature) const
{
	for (const TNSurvivalCatalog::FBreakableBridge& B : SurvivalTerrainPlan.Bridges)
	{
		if (B.Feature == Feature) { return true; }
	}
	return false;
}

void ATN_ProcMapGenerator::SpawnSurvivalTraps()
{
	using namespace TNSurvivalCatalog;
	UWorld* World = GetWorld();
	if (!World || (SurvivalTrapPlan.Num() == 0 && SurvivalTerrainPlan.Quads.Num() == 0 && SurvivalTerrainPlan.Bridges.Num() == 0
		&& SurvivalTerrainPlan.Shortcuts.Num() == 0))
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
				// Centrada en el suelo: la zona hace nacer los cangrejos a la altura de su centro (y ellos se pegan al suelo).
				if (!bServer) { break; }
				if (ATN_CrabSpawnZone* Zone = Cast<ATN_CrabSpawnZone>(SpawnMapActor(TrapClass<ATN_CrabSpawnZone>(CrabZonePath),
					FTransform(Rot, MapToWorld2D(Where, Ground)), true)))
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
			case ETrap::Quicksand:
				// Como la zona lenta: no se replica, la crea cada máquina (la del servidor decide quién queda atrapada).
				if (ATN_Quicksand* Sand = Cast<ATN_Quicksand>(SpawnMapActor(ATN_Quicksand::StaticClass(), FTransform(Rot, MapToWorld2D(Where, Ground)), false)))
				{
					Sand->SetQuicksandRadius(static_cast<float>(P.Extent.X));
				}
				break;
			case ETrap::TankTrap:
				if (bServer)
				{
					if (ATN_BeachTankTrap* Hog = ATN_BeachTankTrap::SpawnStandalone(World, FTransform(Rot, MapToWorld2D(Where, Ground)),
						static_cast<float>(TankTrapRadius)))
					{
						SpawnedActors.Add(Hog);
					}
				}
				break;
			case ETrap::DragCrab:
			case ETrap::BurrowCrab:
			case ETrap::UrchinSpikes:
			case ETrap::TrashPile:
			case ETrap::Trench:
			{
				if (!bServer) { break; }
				FTNBeachElementSpec Spec;
				Spec.Element = BeachElementOf(P.Trap);
				Spec.Seed = static_cast<int32>(NetConfig.Seed) * 31 + P.Sample;
				Spec.SizeScale = SurvivalSizeScale(P.Trap);
				if (ATN_BeachElement* Element = ATN_BeachElement::SpawnElement(World, FTransform(Rot, MapToWorld2D(Where, Ground)), Spec))
				{
					SpawnedActors.Add(Element);
				}
				break;
			}
			default:
				break;
		}
	}

	// #517: todo replicado o de lógica de servidor, así que solo en el servidor.
	if (!bServer)
	{
		return;
	}
	for (const FBreakableBridge& B : SurvivalTerrainPlan.Bridges)
	{
		// La cara de arriba 5 cm sobre el camino, como la viga que sustituye.
		const double CenterZ = B.Location.Z + 5.0 - ATN_ProcBreakableBridge::BoardThickness * 0.5;
		const FTransform T(FRotator(0.0, B.YawDeg + Yaw0, 0.0), MapToWorld2D(FVector2D(B.Location.X, B.Location.Y), CenterZ));
		if (ATN_ProcBreakableBridge* Bridge = Cast<ATN_ProcBreakableBridge>(SpawnMapActor(ATN_ProcBreakableBridge::StaticClass(), T, true)))
		{
			Bridge->SetBoardSize(FVector(B.Length, ATN_ProcBreakableBridge::BoardWidth, ATN_ProcBreakableBridge::BoardThickness));
		}
	}
	UClass* QuadClass = LoadClass<ATN_QuadActor>(nullptr, QuadPath);
	for (int32 k = 0; k < SurvivalTerrainPlan.Quads.Num(); ++k)
	{
		const FQuadCrossing& Q = SurvivalTerrainPlan.Quads[k];
		const FTransform T(FRotator(0.0, Q.YawDeg + Yaw0, 0.0), MapToWorld2D(FVector2D(Q.Location.X, Q.Location.Y), Q.Location.Z));
		if (ATN_ProcQuadCrossing* Crossing = Cast<ATN_ProcQuadCrossing>(SpawnMapActor(ATN_ProcQuadCrossing::StaticClass(), T, true)))
		{
			// Dos cruces seguidos no pasan a la vez.
			Crossing->Setup(static_cast<float>(Q.HalfSpan), static_cast<float>(Q.PathHalfWidth), 6.f + 3.5f * k, QuadClass);
		}
	}
	UClass* GateClass = (Settings && Settings->SabotageGateClass) ? Settings->SabotageGateClass.Get() : ATN_ProcSabotageGate::StaticClass();
	UClass* PlateClass = TrapClass<ATN_PressurePlate>(PlatePath);
	for (const FPlateShortcut& Sc : SurvivalTerrainPlan.Shortcuts)
	{
		const FVector2D G(Sc.Gate.X, Sc.Gate.Y);
		ATN_ProcSabotageGate* Gate = Cast<ATN_ProcSabotageGate>(SpawnMapActor(GateClass,
			FTransform(FRotator(0.0, Sc.GateYawDeg + Yaw0, 0.0), MapToWorld2D(G, TerrainHeightMap(G))), true));
		if (Gate) { Gate->Setup(static_cast<float>(Sc.GateWidth), ShortcutGateHeight); }
		TArray<ATN_PressurePlate*> Plates;
		for (const FVector& P : Sc.Plates)
		{
			const FVector2D P2(P.X, P.Y);
			if (ATN_PressurePlate* Plate = Cast<ATN_PressurePlate>(SpawnMapActor(PlateClass,
				FTransform(FRotator(0.0, Sc.GateYawDeg + Yaw0, 0.0), MapToWorld2D(P2, TerrainHeightMap(P2))), true)))
			{
				// Se resuelve con un solo jugador: cada placa queda pisada aunque se baje.
				Plate->SetMode(EPressurePlateMode::Latched);
				Plates.Add(Plate);
			}
		}
		if (ATN_ProcShortcutLock* Lock = Cast<ATN_ProcShortcutLock>(SpawnMapActor(ATN_ProcShortcutLock::StaticClass(), FTransform(MapToWorld2D(G, 0.0)), true)))
		{
			Lock->Setup(Plates, Gate);
		}
	}
}

void ATN_ProcMapGenerator::SpawnShelters()
{
	using TNProcMap::EFeature;
	using TNProcMap::EFormation;
	const double Yaw0 = GetActorRotation().Yaw;
	int32 Made = 0;
	for (const TNProcMap::FFeature& F : Layout.Features)
	{
		if (F.Type != EFeature::Formation || static_cast<EFormation>(F.Aux) != EFormation::Bunker)
		{
			continue;
		}
		// Mismo marco que la malla de la formación: origen en el suelo del camino, +X por su eje.
		const FVector2D Dx = F.Dir.GetSafeNormal().IsNearlyZero() ? FVector2D(1.0, 0.0) : F.Dir.GetSafeNormal();
		const TNBeachCreatureRules::Shelter::FBunkerDims Dims = TNBeachCreatureRules::Shelter::FormationBunker(F.Radius, F.Height);
		const FRotator Rot(0.0, FMath::RadiansToDegrees(FMath::Atan2(Dx.Y, Dx.X)) + Yaw0, 0.0);
		const FTransform Xf(Rot, MapToWorld2D(FVector2D(F.Location.X, F.Location.Y), F.Location.Z));
		if (ATN_BeachShelterVolume* Shelter = Cast<ATN_BeachShelterVolume>(SpawnMapActor(ATN_BeachShelterVolume::StaticClass(), Xf, false)))
		{
			Shelter->SetShelterExtent(FVector(Dims.Interior.X, Dims.Interior.Y, Dims.Interior.Z * 0.5));
			++Made;
		}
	}
	if (Made > 0)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[ProcMap] %d búnkeres con refugio."), Made);
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
			// La de cangrejos va centrada en el suelo; las demás, apoyadas en él.
			const double Lift = P.Trap == TNSurvivalCatalog::ETrap::Crab ? 0.0 : P.Extent.Z;
			DrawDebugBox(World, MapToWorld2D(Where, Ground + Lift), P.Extent, Rot, Color, true, -1.f, 0, 20.f);
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

	// #517: cruces de quads (naranja, de extremo a extremo y la franja), puentes que se rompen (marrón) y atajos
	// (amarillo: compuerta y placas).
	for (const TNSurvivalCatalog::FQuadCrossing& Q : SurvivalTerrainPlan.Quads)
	{
		const FVector C = MapToWorld2D(FVector2D(Q.Location.X, Q.Location.Y), Q.Location.Z + 50.0);
		const FRotator R(0.0, Q.YawDeg + Yaw0, 0.0);
		DrawDebugLine(World, C - R.Vector() * Q.HalfSpan, C + R.Vector() * Q.HalfSpan, FColor::Orange, true, -1.f, 0, 40.f);
		DrawDebugBox(World, C, FVector(Q.PathHalfWidth, ATN_ProcQuadCrossing::StripeHalfDepth, 50.0), R.Quaternion(), FColor::Orange, true, -1.f, 0, 15.f);
	}
	for (const TNSurvivalCatalog::FBreakableBridge& B : SurvivalTerrainPlan.Bridges)
	{
		DrawDebugBox(World, MapToWorld2D(FVector2D(B.Location.X, B.Location.Y), B.Location.Z),
			FVector(B.Length * 0.5, ATN_ProcBreakableBridge::BoardWidth * 0.5, ATN_ProcBreakableBridge::BoardThickness * 0.5),
			FRotator(0.0, B.YawDeg + Yaw0, 0.0).Quaternion(), FColor(140, 80, 30), true, -1.f, 0, 20.f);
	}
	for (const TNSurvivalCatalog::FPlateShortcut& Sc : SurvivalTerrainPlan.Shortcuts)
	{
		const FVector2D G(Sc.Gate.X, Sc.Gate.Y);
		DrawDebugBox(World, MapToWorld2D(G, TerrainHeightMap(G) + ShortcutGateHeight * 0.5), FVector(60.0, Sc.GateWidth * 0.5, ShortcutGateHeight * 0.5),
			FRotator(0.0, Sc.GateYawDeg + Yaw0, 0.0).Quaternion(), FColor::Yellow, true, -1.f, 0, 20.f);
		for (const FVector& P : Sc.Plates)
		{
			const FVector2D P2(P.X, P.Y);
			DrawDebugCylinder(World, MapToWorld2D(P2, TerrainHeightMap(P2)), MapToWorld2D(P2, TerrainHeightMap(P2) + 30.0), 80.f, 16,
				FColor::Yellow, true, -1.f, 0, 10.f);
		}
	}
}
