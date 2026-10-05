// ─────────────────────────────────────────────────────────────────────────────
// ATN_ProcMapGenerator — vegetación, actores del mapa, peligros y PCG.
// ─────────────────────────────────────────────────────────────────────────────

#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "Settings/TN_GameplayAssetSettings.h"
#include "World/ProcMap/TN_ProcMapFeatures.h"
#include "World/ProcMap/TN_ProcMapTerrain.h"
#include "World/ProcMap/TN_ProcTraversalActors.h"
#include "World/ProcMap/TN_ProcWaterActors.h"
#include "World/ProcMap/TN_ProcPuzzleActors.h"
#include "World/ProcMap/TN_ProcEggNest.h"
#include "World/ProcMap/TN_ProcStartStructure.h"
#include "World/ProcMap/TN_ProcMapActorUtils.h"
#include "World/ProcMap/TN_ProcSearchSpot.h"
#include "World/ProcMap/TN_SurvivalSearchSpot.h"
#include "Game/TN_SurvivalLoot.h"
#include "World/ProcMap/TN_ProcMapShells.h"
#include "World/TN_CrabSpawnZone.h"
#include "World/TN_ScorePickup.h"
#include "World/TN_ScoreShells.h"
#include "World/TN_SeagullSpawnZone.h"
#include "TN_ProcMapKeepOut.h"
#include "Core/TN_Log.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "Audio/TN_AmbientSynthComponent.h"

// ─────────────────────────────────────────────────────────────────────────────
// Vegetación y props
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGenerator::BuildScatter()
{
	using namespace TNProcMap;
	const double World = Layout.WorldSize;
	const double WorldX = Layout.WorldSizeX;

	// Exclusiones: estructuras, huevos, géiseres, huecos, puzles y tramos no tallados del camino.
	FTNProcKeepOut Keep;
	Keep.AddLayout(Layout);
	// La vegetación, las rocas y los objetos sueltos los pone BuildFlora (mallas propias): con ella no
	// queda ninguna capa de formas básicas del motor (greybox).
	const bool bFlora = !Settings || Settings->bProceduralFlora;

	// Caja envolvente de cada bioma (en el raster de módulos) para no recorrer todo el mapa por capa.
	FVector2D BMin[NumBiomes], BMax[NumBiomes];
	for (int32 b = 0; b < NumBiomes; ++b) { BMin[b] = FVector2D(1e18, 1e18); BMax[b] = FVector2D(-1e18, -1e18); }
	for (int32 y = 0; y < Layout.RasterH; ++y)
	{
		for (int32 x = 0; x < Layout.RasterW; ++x)
		{
			const int32 Mod = Layout.ModuleOfCell[Layout.CellIndex(x, y)];
			if (Mod < 0) { continue; }
			const int32 b = BiomeIndex(Layout.Modules[Mod].Biome);
			const FVector2D C = Layout.CellCenter(x, y);
			BMin[b] = FVector2D(FMath::Min(BMin[b].X, C.X), FMath::Min(BMin[b].Y, C.Y));
			BMax[b] = FVector2D(FMath::Max(BMax[b].X, C.X), FMath::Max(BMax[b].Y, C.Y));
		}
	}

	const FVector2D LatMin = LatticeOrigin;
	const FVector2D LatMax = LatticeOrigin + FVector2D((LatticeNX - 1) * LatticeSpacing, (LatticeNY - 1) * LatticeSpacing);
	int32 TotalInstances = 0;

	for (int32 b = 0; b < NumBiomes; ++b)
	{
		if (BMin[b].X > BMax[b].X) { continue; }
		const ETNProcBiome Biome = BiomeFromIndex(b);
		TArray<FTNProcScatterLayer> ScatterLayers;
		if (const UTN_ProcBiomeDataAsset* Asset = Settings ? Settings->FindBiome(Biome) : nullptr)
		{
			ScatterLayers = Asset->Scatter;
		}
		else
		{
			TN_DefaultBiomeScatter(Biome, ScatterLayers);
		}

		for (int32 LayerIdx = 0; LayerIdx < ScatterLayers.Num(); ++LayerIdx)
		{
			const FTNProcScatterLayer& Layer = ScatterLayers[LayerIdx];
			if (!Layer.Mesh || Layer.DensityPer100m2 <= 0.f) { continue; }
			if (bFlora && Layer.Mesh->GetPathName().StartsWith(TEXT("/Engine/BasicShapes/")))
			{
				continue;
			}

			const bool bWalls = Layer.Zone == ETNProcScatterZone::Walls;
			const double Blend = 6000.0;
			FVector2D Min = bWalls ? LatMin : FVector2D(FMath::Max(0.0, BMin[b].X - Blend), FMath::Max(0.0, BMin[b].Y - Blend));
			FVector2D Max = bWalls ? LatMax : FVector2D(FMath::Min(WorldX, BMax[b].X + Blend), FMath::Min(World, BMax[b].Y + Blend));
			if (bWalls)
			{
				// En los muros solo el bioma dominante de esa zona del borde.
				Min = FVector2D(FMath::Max(LatMin.X, BMin[b].X - 40000.0), FMath::Max(LatMin.Y, BMin[b].Y - 40000.0));
				Max = FVector2D(FMath::Min(LatMax.X, BMax[b].X + 40000.0), FMath::Min(LatMax.Y, BMax[b].Y + 40000.0));
			}

			const double Spacing = FMath::Sqrt(1.0e6 / static_cast<double>(Layer.DensityPer100m2));
			const int32 CX = FMath::Max(1, FMath::CeilToInt((Max.X - Min.X) / Spacing));
			const int32 CY = FMath::Max(1, FMath::CeilToInt((Max.Y - Min.Y) / Spacing));
			FRng Rng(static_cast<uint64>(Layout.Params.Seed) * 1000003ull + static_cast<uint64>(b) * 7919ull + static_cast<uint64>(LayerIdx) * 104729ull);
			const double MaxSlopeCos = FMath::Cos(FMath::DegreesToRadians(static_cast<double>(Layer.MaxSlopeDeg)));

			TArray<FTransform> Transforms;
			for (int32 cy = 0; cy < CY; ++cy)
			{
				for (int32 cx = 0; cx < CX; ++cx)
				{
					const FVector2D P = Min + FVector2D((cx + Rng.Unit()) * Spacing, (cy + Rng.Unit()) * Spacing);
					const double Pick = Rng.Unit();
					const double Yaw = Rng.Range(0.0, 360.0);
					const double ScaleT = Rng.Unit();

					// Mezcla natural en las transiciones: el bioma se sortea con sus pesos.
					double W[NumBiomes];
					Layout.BiomeWeightsAt(P, W);
					double Acc = 0.0;
					int32 Chosen = NumBiomes - 1;
					for (int32 k = 0; k < NumBiomes; ++k)
					{
						Acc += W[k];
						if (Pick < Acc) { Chosen = k; break; }
					}
					if (Chosen != b) { continue; }

					const double H = TerrainHeightMap(P);
					const FVector N = TerrainNormalMap(P);
					const double Edge = PathDistanceMap(P);
					const bool bInside = P.X >= 0.0 && P.Y >= 0.0 && P.X <= WorldX && P.Y <= Layout.CoastY(P.X);
					const double EdgeDist = FMath::Min(FMath::Min(P.X, WorldX - P.X), P.Y);
					const bool bWallZone = !bInside || EdgeDist < Layout.WallInset(P.X) + 1000.0;

					bool bOk = false;
					switch (Layer.Zone)
					{
						case ETNProcScatterZone::OffPath:
							bOk = bInside && Edge >= Layer.MinPathDistance && H > 40.0 && N.Z >= MaxSlopeCos;
							break;
						case ETNProcScatterZone::PathEdge:
							bOk = bInside && Edge >= Layer.MinPathDistance && Edge <= Layer.MinPathDistance + 500.0 && H > 15.0 && N.Z >= MaxSlopeCos;
							break;
						case ETNProcScatterZone::Walls:
							bOk = bWallZone && H > 300.0 && N.Z >= MaxSlopeCos;
							break;
						case ETNProcScatterZone::Shallows:
							bOk = bInside && H < 10.0 && H > -160.0 && Edge >= Layer.MinPathDistance;
							break;
					}
					if (!bOk || Keep.Blocked(P)) { continue; }

					const double S = FMath::Lerp(static_cast<double>(Layer.ScaleRange.X), static_cast<double>(Layer.ScaleRange.Y), ScaleT);
					FQuat Rot = FQuat(FRotator(0.0, Yaw, 0.0));
					if (Layer.bAlignToNormal)
					{
						Rot = FQuat::FindBetweenNormals(FVector::UpVector, N) * Rot;
					}
					const FVector Loc = FVector(P.X, P.Y, H + Layer.ZOffset);
					Transforms.Add(FTransform(Rot, Loc, Layer.ScaleAxes * S));
				}
			}
			if (Transforms.Num() == 0) { continue; }

			UHierarchicalInstancedStaticMeshComponent* HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
			HISM->SetupAttachment(RootComponent);
			HISM->SetStaticMesh(Layer.Mesh);
			HISM->SetCollisionProfileName(Layer.bCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
			HISM->SetCanEverAffectNavigation(Layer.bCollision);
			if (Layer.CullDistance > 0.f)
			{
				HISM->SetCullDistances(FMath::RoundToInt(Layer.CullDistance * 0.8f), FMath::RoundToInt(Layer.CullDistance));
			}
			HISM->RegisterComponent();
			if (Layer.Material)
			{
				HISM->SetMaterial(0, Layer.Material);
			}
			if (Layer.bApplyTint)
			{
				// Con material propio solo se tiñe si lo admite; sin él, material greybox tintable.
				TNProcActors::Tint(HISM, Layer.Tint, Layer.Material == nullptr);
			}
			// Transformadas en espacio del mapa = espacio local del generador.
			HISM->AddInstances(Transforms, false, false);
			ScatterComponents.Add(HISM);
			TotalInstances += Transforms.Num();
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[ProcMap] Vegetación y props: %d instancias en %d capas."), TotalInstances, ScatterComponents.Num());
}

// ─────────────────────────────────────────────────────────────────────────────
// Actores de recorrido (todas las máquinas)
// ─────────────────────────────────────────────────────────────────────────────

AActor* ATN_ProcMapGenerator::SpawnMapActor(UClass* Class, const FTransform& Transform, bool bTrackAsServer)
{
	UWorld* World = GetWorld();
	if (!World || !Class)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;
	AActor* Actor = World->SpawnActor<AActor>(Class, Transform, Params);
	if (Actor)
	{
		SpawnedActors.Add(Actor);
	}
	return Actor;
}

void ATN_ProcMapGenerator::SpawnTraversalActors()
{
	using namespace TNProcMap;
	const TArray<FPathSample>& M = Layout.Main;
	const double Yaw0 = GetActorRotation().Yaw;

	// Puntos de salida alrededor del claro inicial, mirando al camino.
	StartTransforms.Reset();
	if (M.Num() > 0)
	{
		const FVector2D Ahead = M[FMath::Min(25, M.Num() - 1)].P;
		const FVector2D Face = (Ahead - Layout.StartPoint).GetSafeNormal();
		const double FaceYaw = FMath::RadiansToDegrees(AngleOf(Face)) + Yaw0;
		for (int32 i = 0; i < 8; ++i)
		{
			const double A = TwoPi * i / 8.0;
			const FVector2D P = Layout.StartPoint + DirFromAngle(A) * (450.0 + (i % 2) * 350.0);
			const FVector Loc = MapToWorld2D(P, TerrainHeightMap(P) + 110.0);
			StartTransforms.Add(FTransform(FRotator(0.0, FaceYaw, 0.0), Loc));
		}
	}

	UClass* GeyserClass = (Settings && Settings->GeyserClass) ? Settings->GeyserClass.Get() : ATN_ProcGeyser::StaticClass();

	for (const FFeature& F : Layout.Features)
	{
		const FVector2D C(F.Location.X, F.Location.Y);
		const double Yaw = FMath::RadiansToDegrees(AngleOf(F.Dir)) + Yaw0;
		switch (F.Type)
		{
			case EFeature::Geyser:
			{
				const FVector Loc = MapToWorld2D(C, TerrainHeightMap(C));
				if (ATN_ProcGeyser* Geyser = Cast<ATN_ProcGeyser>(SpawnMapActor(GeyserClass, FTransform(Loc), false)))
				{
					Geyser->SetTarget(MapToWorld(F.Target));
					// Dentro de la torre de entrada: sube por el hueco del forjado (sobre el géiser, a la cota de la cima).
					if (F.Aux2 == TNProcMap::TowerDims::HollowBit) { Geyser->SetShaft(MapToWorld(FVector(C, F.Target.Z))); }
				}
				break;
			}
			case EFeature::SlideZone:
			{
				const TArray<FPathSample>& S = F.BranchIndex == INDEX_NONE ? M : Layout.Branches[F.BranchIndex].Samples;
				TArray<FVector> Points;
				for (int32 i = FMath::Clamp(F.PathIndex, 0, S.Num() - 1); i <= FMath::Clamp(F.Aux + 1, 0, S.Num() - 1); ++i)
				{
					Points.Add(MapToWorld2D(S[i].P, S[i].Z));
				}
				if (ATN_ProcSlideZone* Slide = Cast<ATN_ProcSlideZone>(SpawnMapActor(ATN_ProcSlideZone::StaticClass(), GetActorTransform(), false)))
				{
					FVector2D PoolC, Foot, Flow;
					double PoolR = 0.0, PoolZ = 0.0;
					SlidePool(F, PoolC, PoolR, PoolZ, Foot, Flow);
					Slide->InitFromPoints(Points, static_cast<float>(F.Width), MapToWorld2D(PoolC, PoolZ), static_cast<float>(PoolR), MapToWorld2D(Foot, PoolZ));
				}
				break;
			}
			case EFeature::Gap:
			{
				// Fondo de la zanja: caer en un hueco es morir y reaparecer en los huevos. La zona
				// cubre el fondo sin asomar por encima de los labios (zanjas poco hondas junto al agua);
				// en un río de lava, justo bajo su superficie (tocarla ya mata).
				const bool bLava = TNProcMap::IsLavaGap(F);
				const FVector Loc = MapToWorld2D(C, bLava ? F.Location.Z - 260.0 : FMath::Min(TNProcMap::GapFloorZ(F) + 150.0, F.Location.Z - 400.0));
				if (ATN_ProcKillVolume* Kill = Cast<ATN_ProcKillVolume>(SpawnMapActor(ATN_ProcKillVolume::StaticClass(),
					FTransform(FRotator(0.0, Yaw, 0.0), Loc), false)))
				{
					Kill->SetExtent(FVector(F.Height * 0.5, F.Width * 0.5 + TNProcMap::GapTrenchSideOf(F), bLava ? 110.0 : 250.0));
					if (bLava) { UTN_AmbientSynthComponent::AttachPointSound(Kill, ETNAmbientSourceKind::LavaPool, 0.7f, 400.f, 2500.f); }
				}
				break;
			}
			case EFeature::LavaPool:
			{
				const FVector Loc = MapToWorld2D(C, F.Location.Z - 120.0);
				if (ATN_ProcKillVolume* Kill = Cast<ATN_ProcKillVolume>(SpawnMapActor(ATN_ProcKillVolume::StaticClass(), FTransform(Loc), false)))
				{
					Kill->SetExtent(FVector(F.Radius * 0.9, F.Radius * 0.9, 150.0));
					UTN_AmbientSynthComponent::AttachPointSound(Kill, ETNAmbientSourceKind::LavaPool, 1.f, static_cast<float>(F.Radius), 3000.f);
				}
				break;
			}
			default:
				break;
		}
	}

	// Caerse de un puente colosal mata: cajas de muerte del layout (bajo los tableros).
	for (const FKillBox& K : Layout.KillBoxes)
	{
		if (ATN_ProcKillVolume* Kill = Cast<ATN_ProcKillVolume>(SpawnMapActor(ATN_ProcKillVolume::StaticClass(),
			FTransform(FRotator(0.0, FMath::RadiansToDegrees(AngleOf(K.Dir)) + Yaw0, 0.0), MapToWorld(K.Center)), false)))
		{
			Kill->SetExtent(K.Half);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Actores del servidor (replicados o solo-servidor)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGenerator::SpawnStartStructure()
{
	using namespace TNProcMap;
	StartStructure.Reset();
	UWorld* World = GetWorld();
	if (!bSpawnStartStructure || bTerrainOnly || !World || Layout.Main.Num() == 0)
	{
		return;
	}

	// Hacia dónde sale el camino del claro (mapa): del punto de salida a la primera muestra que queda fuera del claro
	// (aguanta que el camino haga curva dentro). Detrás (-Dir) solo hay talud.
	const double ClearingRadius = Layout.Params.StartClearingRadius;
	FVector2D Dir = Layout.Main[0].Dir;
	for (const FPathSample& Sample : Layout.Main)
	{
		if (FVector2D::Distance(Sample.P, Layout.StartPoint) >= ClearingRadius)
		{
			Dir = Sample.P - Layout.StartPoint;
			break;
		}
	}
	Dir = Dir.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector2D(0.0, 1.0);
	}
	// Ejes de la estructura en el mapa: +Y local = Dir (hacia el camino); +X local = Dir girada -90°.
	const FVector2D Side(Dir.Y, -Dir.X);
	const FVector2D Origin = Layout.StartPoint - Dir * ATN_ProcStartStructure::GetBackDistance(StartStructureStyle, ClearingRadius);

	// Cota: la del terreno bajo la huella (el suelo del claro es casi plano, ±10 cm).
	TArray<FVector2D> Footprint;
	bool bUseHighest = true;
	ATN_ProcStartStructure::GetFootprintSamples(StartStructureStyle, Footprint, bUseHighest);
	double BaseZ = TerrainHeightMap(Origin);
	for (const FVector2D& LocalPoint : Footprint)
	{
		const double Height = TerrainHeightMap(Origin + Side * LocalPoint.X + Dir * LocalPoint.Y);
		BaseZ = bUseHighest ? FMath::Max(BaseZ, Height) : FMath::Min(BaseZ, Height);
	}

	// Guiñada: la que lleva +X a Side (la de Dir menos 90°), más la del generador.
	const double Yaw = FMath::RadiansToDegrees(AngleOf(Dir)) - 90.0 + GetActorRotation().Yaw;
	const FTransform Where(FRotator(0.0, Yaw, 0.0), MapToWorld(FVector(Origin.X, Origin.Y, BaseZ)));
	// Diferida: el estilo tiene que estar puesto antes de su BeginPlay, que es donde construye las mallas.
	ATN_ProcStartStructure* Structure = World->SpawnActorDeferred<ATN_ProcStartStructure>(ATN_ProcStartStructure::StaticClass(), Where, this,
		nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Structure)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[ProcMap] No se pudo crear la estructura de salida: se sale del anillo del claro."));
		return;
	}
	Structure->SetStyle(StartStructureStyle);
	Structure->FinishSpawning(Where);
	SpawnedActors.Add(Structure);
	StartStructure = Structure;
	UE_LOG(LogTortunabo, Log, TEXT("[ProcMap] Estructura de salida (%s) al fondo del claro, a %.1f m de su centro."),
		StartStructureStyle == ETNMatchStartStyle::Eggs ? TEXT("huevos") : TEXT("puerta doble"),
		ATN_ProcStartStructure::GetBackDistance(StartStructureStyle, ClearingRadius) / 100.0);
}

void ATN_ProcMapGenerator::SpawnServerActors()
{
	using namespace TNProcMap;
	const double Yaw0 = GetActorRotation().Yaw;

	// La estructura de salida va antes que los PlayerStart: los primeros quedan dentro de ella (GetStartTransform).
	SpawnStartStructure();
	StartPlayerStarts.Reset();
	StartPlayerStarts.SetNum(StartTransforms.Num());
	for (int32 i = 0; i < StartTransforms.Num(); ++i)
	{
		if (APlayerStart* Start = Cast<APlayerStart>(SpawnMapActor(APlayerStart::StaticClass(), GetStartTransform(i), true)))
		{
			Start->PlayerStartTag = TEXT("TNProcStart");
			StartPlayerStarts[i] = Start;
		}
	}

	UClass* NestClass = (Settings && Settings->EggNestClass) ? Settings->EggNestClass.Get() : ATN_ProcEggNest::StaticClass();
	UClass* WallClass = (Settings && Settings->ThrowWallClass) ? Settings->ThrowWallClass.Get() : ATN_ProcThrowWall::StaticClass();
	UClass* GateClass = (Settings && Settings->SabotageGateClass) ? Settings->SabotageGateClass.Get() : ATN_ProcSabotageGate::StaticClass();
	UClass* SwitchClass = (Settings && Settings->SwitchClass) ? Settings->SwitchClass.Get() : ATN_ProcSwitch::StaticClass();

	TMap<int32, ATN_ProcSabotageGate*> GateByFeature;
	// Karts: ni recompensas, ni medusas, ni huevos, ni la meta del cooperativo (la carrera pone la suya), ni rebuscables.
	const bool bKarts = IsKartMap();
	for (int32 f = 0; f < Layout.Features.Num() && !bKarts; ++f)
	{
		const FFeature& F = Layout.Features[f];
		const FVector2D C(F.Location.X, F.Location.Y);
		const FRotator Rot(0.0, FMath::RadiansToDegrees(AngleOf(F.Dir)) + Yaw0, 0.0);
		switch (F.Type)
		{
			case EFeature::BonusPickup:
			{
				if (bTerrainOnly) { break; }
				// Recompensa en un sitio concreto: la cima de una atalaya o de un parkour.
				if (UClass* Score = UTN_GameplayAssetSettings::GetScorePickupClass())
				{
					SpawnMapActor(Score, FTransform(Rot, MapToWorld(F.Location)), true);
				}
				break;
			}
			case EFeature::Bouncer:
			{
				if (bTerrainOnly) { break; }
				// Medusa saltarina del juego; sin su Blueprint, la criatura rebotadora de las lagunas.
				UClass* Jelly = LoadClass<AActor>(nullptr, TEXT("/Game/Blueprints/Gameplay/Items/BP_JellyfishActor.BP_JellyfishActor_C"));
				SpawnMapActor(Jelly ? Jelly : ATN_ProcWaterBouncer::StaticClass(), FTransform(Rot, MapToWorld(F.Location)), true);
				break;
			}
			case EFeature::EggNest:
			{
				if (bTerrainOnly) { break; }
				const FVector Loc = MapToWorld2D(C, TerrainHeightMap(C));
				if (ATN_ProcEggNest* Nest = Cast<ATN_ProcEggNest>(SpawnMapActor(NestClass, FTransform(Rot, Loc), true)))
				{
					const double Progress = Layout.Main.IsValidIndex(F.PathIndex) ? Layout.Main[F.PathIndex].S : 0.0;
					Nest->InitNest(F.Aux, static_cast<float>(Progress));
					EggNests.Add(Nest);
				}
				break;
			}
			case EFeature::Finish:
			{
				// Empieza en la línea de meta (ya en el agua) y cubre toda la boca de la playa hasta el
				// fondo del volumen, donde los brazos se han abierto más.
				const FVector Loc = MapToWorld2D(C + F.Dir.GetSafeNormal() * (F.Length * 0.5), TNProcMap::SeaLevel - 300.0);
				if (ATN_ProcFinishVolume* Finish = Cast<ATN_ProcFinishVolume>(SpawnMapActor(ATN_ProcFinishVolume::StaticClass(), FTransform(Rot, Loc), true)))
				{
					Finish->SetExtent(FVector(F.Length * 0.5, F.Height * 0.5 + 1500.0, 900.0));
				}
				break;
			}
			case EFeature::ThrowWall:
			{
				const FVector Loc = MapToWorld2D(C, TerrainHeightMap(C) - 20.0);
				if (ATN_ProcThrowWall* Wall = Cast<ATN_ProcThrowWall>(SpawnMapActor(WallClass, FTransform(Rot, Loc), true)))
				{
					Wall->Setup(static_cast<float>(F.Width), static_cast<float>(F.Height), 1200.f);
					if (ATN_ProcSwitch* Switch = Cast<ATN_ProcSwitch>(SpawnMapActor(SwitchClass, FTransform(Rot, Wall->GetSwitchLocation() + FVector(0.0, 0.0, 10.0)), true)))
					{
						Switch->SetTarget(Wall, 8.f);
					}
				}
				break;
			}
			case EFeature::SabotageGate:
			{
				const FVector Loc = MapToWorld2D(C, TerrainHeightMap(C));
				if (ATN_ProcSabotageGate* Gate = Cast<ATN_ProcSabotageGate>(SpawnMapActor(GateClass, FTransform(Rot, Loc), true)))
				{
					Gate->Setup(static_cast<float>(F.Width), static_cast<float>(F.Height));
					GateByFeature.Add(f, Gate);
				}
				break;
			}
			default:
				break;
		}
	}

	// Interruptores de sabotaje: cada uno levanta la compuerta del otro carril.
	for (const FFeature& F : Layout.Features)
	{
		if (F.Type != EFeature::SabotageSwitch) { continue; }
		const FVector2D C(F.Location.X, F.Location.Y);
		ATN_ProcSabotageGate* const* Gate = GateByFeature.Find(F.Aux);
		if (!Gate || !*Gate) { continue; }
		const FVector Loc = MapToWorld2D(C, TerrainHeightMap(C) + 10.0);
		if (ATN_ProcSwitch* Switch = Cast<ATN_ProcSwitch>(SpawnMapActor(SwitchClass, FTransform(Loc), true)))
		{
			Switch->SetTarget(*Gate, 6.f);
		}
	}

	// Estatuas, rocas grandes, barcas, cajas... que se pueden rebuscar (al final del fichero).
	if (!bKarts)
	{
		SpawnSearchSpots();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Peligros y enemigos por bioma
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGenerator::SpawnHazards()
{
	using namespace TNProcMap;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const bool bServer = World->GetNetMode() != NM_Client;
	const bool bSurvival = NetConfig.Mode == ETNProcGameMode::Survival;
	const double Yaw0 = GetActorRotation().Yaw;

	struct FRuleSource
	{
		FTNProcHazardEntry Entry;
		ETNProcBiome Biome = ETNProcBiome::Jungle;
		UStaticMesh* BouncerMesh = nullptr;
		FLinearColor BouncerColor = FLinearColor(0.9f, 0.5f, 0.9f);
	};
	TArray<FRuleSource> Sources;
	TArray<FHazardRule> Rules;

	for (int32 b = 0; b < NumBiomes; ++b)
	{
		const ETNProcBiome Biome = BiomeFromIndex(b);
		TArray<FTNProcHazardEntry> Entries;
		UStaticMesh* BouncerMesh = nullptr;
		FLinearColor BouncerColor(0.9f, 0.5f, 0.9f);
		if (const UTN_ProcBiomeDataAsset* Asset = Settings ? Settings->FindBiome(Biome) : nullptr)
		{
			Entries = Asset->Hazards;
			BouncerMesh = Asset->WaterBouncerMesh;
			BouncerColor = Asset->WaterBouncerColor;
		}
		else
		{
			TN_DefaultBiomeHazards(Biome, Entries);
		}
		for (const FTNProcHazardEntry& E : Entries)
		{
			if (!E.ActorClass) { continue; }
			// Supervivencia: sin los cangrejos pequeños ni las gaviotas de antes; sus trampas son el cangrejo gigante y las
			// gaviotas de la playa del catálogo (#733, #734).
			if (bSurvival && (E.ActorClass->IsChildOf(ATN_CrabSpawnZone::StaticClass()) || E.ActorClass->IsChildOf(ATN_SeagullSpawnZone::StaticClass())))
			{
				continue;
			}
			FRuleSource Src;
			Src.Entry = E;
			Src.Biome = Biome;
			Src.BouncerMesh = BouncerMesh;
			Src.BouncerColor = BouncerColor;
			FHazardRule R;
			R.Id = Sources.Add(Src);
			R.PerKm = E.PerKm;
			R.Placement = static_cast<EHazardPlacement>(static_cast<uint8>(E.Placement));
			R.BiomeMask = 1u << b;
			// Umbrales alineados con los perfiles por defecto (fácil 0.2, normal 0.5, difícil 0.9).
			static const double DifficultyGate[3] = { 0.0, 0.35, 0.75 };
			R.MinDifficulty01 = DifficultyGate[FMath::Clamp(static_cast<int32>(E.MinDifficulty), 0, 2)];
			R.Clearance = E.Clearance;
			R.bMainOnly = E.bMainPathOnly;
			Rules.Add(R);
		}
	}

	const TArray<FHazardSpawn> Spawns = PlanHazards(Layout, Rules, ActiveProfile.HazardDensity, 0x4A2Aull);
	int32 Count = 0;
	for (const FHazardSpawn& H : Spawns)
	{
		const FRuleSource& Src = Sources[H.RuleId];
		UClass* Class = Src.Entry.ActorClass;

		// Solo la fauna de movimiento (corrientes, remolinos) vive en todas las máquinas;
		// el resto (enemigos, spawners, pickups) lo crea el servidor y replica si procede.
		const bool bLocalEverywhere = Class->IsChildOf(ATN_ProcWaterCurrent::StaticClass()) || Class->IsChildOf(ATN_ProcWhirlpool::StaticClass());
		if (!bLocalEverywhere && !bServer) { continue; }

		const double Ground = TerrainHeightMap(H.P);
		double Z = Ground + Src.Entry.ZOffset;
		const bool bWaterFauna = Src.Entry.Placement == ETNProcHazardPlacement::InWater;
		if (bWaterFauna)
		{
			// Necesita agua de verdad bajo ella.
			const double Need = Class->IsChildOf(ATN_ProcWhirlpool::StaticClass()) ? -220.0
				: (Class->IsChildOf(ATN_ProcWaterPredator::StaticClass()) ? -170.0 : -70.0);
			if (Ground > Need) { continue; }
			Z = SeaLevel + (Class->IsChildOf(ATN_ProcWaterPredator::StaticClass()) ? -45.0 : 5.0) + Src.Entry.ZOffset;
		}
		else if (Src.Entry.Placement == ETNProcHazardPlacement::AbovePath)
		{
			Z = Ground + 800.0 + Src.Entry.ZOffset;
		}
		else if (Ground < 0.0)
		{
			continue;
		}

		const FVector Loc = MapToWorld2D(H.P, Z);
		AActor* Actor = SpawnMapActor(Class, FTransform(FRotator(0.0, H.Yaw + Yaw0, 0.0), Loc), true);
		if (!Actor) { continue; }
		++Count;
		// Lo del camino (conchas normales, medusas, zonas de objetos...) lo apartan las conchas del plan (SpawnShells).
		if (bServer && !bWaterFauna && Src.Entry.Placement != ETNProcHazardPlacement::AbovePath)
		{
			HazardSpots.Add(FVector(H.P.X, H.P.Y, 300.0));
		}

		if (ATN_ProcWaterBouncer* Bouncer = Cast<ATN_ProcWaterBouncer>(Actor))
		{
			Bouncer->SetVariant(Src.BouncerMesh, Src.BouncerColor);
		}
		else if (ATN_ProcWaterCurrent* Current = Cast<ATN_ProcWaterCurrent>(Actor))
		{
			// Arrastra lejos del camino: volver cuesta.
			const TArray<FPathSample>& Samples = H.BranchIndex == INDEX_NONE ? Layout.Main : Layout.Branches[H.BranchIndex].Samples;
			const FVector2D PathP = Samples.IsValidIndex(H.PathIndex) ? Samples[H.PathIndex].P : H.P;
			const FVector2D Away = (H.P - PathP).GetSafeNormal();
			Current->Setup(FVector(900.0, 500.0, 300.0), GetActorTransform().TransformVectorNoScale(FVector(Away.X, Away.Y, 0.0)), 900.f);
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[ProcMap] Peligros y enemigos: %d de %d planificados (%s)."), Count, Spawns.Num(), bServer ? TEXT("servidor") : TEXT("cliente"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Conchas de puntos: rachas de 1, arcos de salto, cornisas y especiales de 50 y 100
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGenerator::SpawnShells()
{
	SpecialShellSpots.Reset();
	ShellSummary.Reset();
	UWorld* ShellWorld = GetWorld();
	if (bTerrainOnly || !ShellWorld || ShellWorld->GetNetMode() == NM_Client || !Layout.bValid)
	{
		return;
	}
	// El Blueprint de siempre (la vieira de código, con su valor puesto antes de aparecer); sin él, la clase nativa.
	UClass* ShellClass = UTN_GameplayAssetSettings::GetScorePickupClass();

	// Plan puro (determinista): no pisa lo que ya han puesto los peligros (conchas normales, medusas, zonas...).
	TArray<TNProcMap::FShellSpawn> Plan;
	TNProcMap::PlanShells(Layout, HazardSpots, Plan);

	// Tramos hundidos de los puentes colosales (dependen de su malla, BuildStructures): en el más largo de cada puente, una
	// reina de 100 en lo pisable del medio (en los dos primeros puentes; en los demás, una grande de 50).
	{
		TMap<int32, const FBrokenSpanPrize*> LongestByCrossing;
		for (const FBrokenSpanPrize& Prize : BrokenSpanPrizes)
		{
			const FBrokenSpanPrize*& Longest = LongestByCrossing.FindOrAdd(Prize.Crossing, nullptr);
			if (!Longest || Prize.Length > Longest->Length)
			{
				Longest = &Prize;
			}
		}
		LongestByCrossing.KeySort(TLess<int32>());
		int32 BridgeGrands = 0;
		for (const TPair<int32, const FBrokenSpanPrize*>& Pair : LongestByCrossing)
		{
			TNProcMap::FShellSpawn Spawn;
			Spawn.Location = Pair.Value->Point + FVector(0.0, 0.0, TNScoreShells::Hover);
			Spawn.bOnGround = false;
			Spawn.Spot = TNProcMap::EShellSpot::BrokenSpan;
			Spawn.Tier = BridgeGrands < 2 ? TNScoreShells::ETier::Grand : TNScoreShells::ETier::Big;
			BridgeGrands += Spawn.Tier == TNScoreShells::ETier::Grand ? 1 : 0;
			Spawn.Approach = Pair.Value->Stand;
			Spawn.Facing = Pair.Value->Facing;
			Plan.Add(Spawn);
		}
	}

	const double Yaw0 = GetActorRotation().Yaw;
	int32 TierCounts[TNScoreShells::NumTiers] = {};
	int32 SpotCounts[static_cast<int32>(TNProcMap::EShellSpot::Count)] = {};
	FString SpecialList;
	for (const TNProcMap::FShellSpawn& Spawn : Plan)
	{
		const FVector2D At(Spawn.Location.X, Spawn.Location.Y);
		// Las del suelo, asentadas en el terreno de verdad; las del aire (arcos, murallas, puentes), donde dice el plan.
		const double ShellZ = Spawn.bOnGround ? TerrainHeightMap(At) + TNScoreShells::Hover : Spawn.Location.Z;
		const FVector2D Face = Spawn.Facing.IsNearlyZero() ? FVector2D(1.0, 0.0) : Spawn.Facing.GetSafeNormal();
		const FTransform Where(FRotator(0.0, FMath::RadiansToDegrees(TNProcMap::AngleOf(Face)) + Yaw0, 0.0), MapToWorld(FVector(At, ShellZ)));
		ATN_ScorePickup* Shell = ShellWorld->SpawnActorDeferred<ATN_ScorePickup>(ShellClass, Where, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Shell)
		{
			continue;
		}
		// El valor, antes de que aparezca: nace ya con su tamaño y su aspecto en todas las máquinas.
		const int32 Value = TNScoreShells::ValueOf(Spawn.Tier);
		Shell->SetScoreValue(Value);
		Shell->FinishSpawning(Where);
		SpawnedActors.Add(Shell);
		++TierCounts[static_cast<int32>(Spawn.Tier)];
		++SpotCounts[static_cast<int32>(Spawn.Spot)];
		if (!TNProcMap::IsSpecialShellSpot(Spawn.Spot))
		{
			continue;
		}
		// Especiales: dónde ponerse para ir a por ella (TNShells Especial).
		const double StandZ = Spawn.bOnGround ? TerrainHeightMap(FVector2D(Spawn.Approach.X, Spawn.Approach.Y)) : Spawn.Approach.Z;
		FTNShellSpot Spot;
		Spot.Shell = Where.GetLocation();
		Spot.Stand = MapToWorld(FVector(Spawn.Approach.X, Spawn.Approach.Y, StandZ + 110.0));
		Spot.Facing = GetActorTransform().TransformVectorNoScale(FVector(Face.X, Face.Y, 0.0));
		Spot.Value = Value;
		Spot.Where = TNProcMap::ShellSpotName(Spawn.Spot);
		SpecialShellSpots.Add(Spot);
		SpecialList += FString::Printf(TEXT("%s%d en %s"), SpecialList.IsEmpty() ? TEXT("") : TEXT(", "), Value, TNProcMap::ShellSpotName(Spawn.Spot));
	}

	// En orden por el camino principal (la muestra más cercana), para recorrerlas con TNShells.
	auto ProgressOf = [this](const FVector& WorldPoint)
	{
		const FVector MapPoint = WorldToMap(WorldPoint);
		const FVector2D Flat(MapPoint.X, MapPoint.Y);
		double Best = 1e300;
		double BestS = 0.0;
		for (const TNProcMap::FPathSample& Sample : Layout.Main)
		{
			const double D = FVector2D::DistSquared(Sample.P, Flat);
			if (D < Best)
			{
				Best = D;
				BestS = Sample.S;
			}
		}
		return BestS;
	};
	SpecialShellSpots.StableSort([&ProgressOf](const FTNShellSpot& A, const FTNShellSpot& B) { return ProgressOf(A.Shell) < ProgressOf(B.Shell); });

	using TNProcMap::EShellSpot;
	ShellSummary = FString::Printf(
		TEXT("%d pequeñas de 1 (rachas %d, desvíos %d, arcos de salto %d, cornisas %d), %d grandes de 50 y %d reinas de 100%s%s. Las normales de 25 salen con los peligros y las atalayas."),
		TierCounts[0], SpotCounts[static_cast<int32>(EShellSpot::Trail)], SpotCounts[static_cast<int32>(EShellSpot::Detour)],
		SpotCounts[static_cast<int32>(EShellSpot::JumpArc)], SpotCounts[static_cast<int32>(EShellSpot::WallLedge)],
		TierCounts[2], TierCounts[3], SpecialList.IsEmpty() ? TEXT("") : TEXT(": "), *SpecialList);
	UE_LOG(LogTortunabo, Log, TEXT("[ProcMap] Conchas: %s"), *ShellSummary);
}

// ─────────────────────────────────────────────────────────────────────────────
// PCG opcional por bioma
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGenerator::RunBiomePCG()
{
	if (!Settings)
	{
		return;
	}
	for (const UTN_ProcBiomeDataAsset* Asset : Settings->Biomes)
	{
		if (!Asset || !Asset->PCGGraph)
		{
			continue;
		}
		UPCGComponent* PCG = NewObject<UPCGComponent>(this, NAME_None, RF_Transient);
		PCG->RegisterComponent();
		PCG->SetGraph(Asset->PCGGraph);
		PCG->GenerateLocal(true);
		PCGComponents.Add(PCG);
		UE_LOG(LogTortunabo, Log, TEXT("[ProcMap] PCG del bioma %s lanzado."), *UEnum::GetValueAsString(Asset->Biome));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Decorados que se pueden rebuscar (ATN_ProcSearchSpot)
// ─────────────────────────────────────────────────────────────────────────────

namespace TNSearchSpotPlan
{
	/** Distancia mínima (cm) entre dos decorados buscables: uno por tramo, no una hilera. */
	constexpr double MinSpacing = 7000.0;

	/** Huella de rebusca de un decorado (cápsula en planta a lo largo de su Dir), probabilidad de serlo y preferencia. */
	struct FSearchable
	{
		double Radius = 0.0;
		double HalfLength = 0.0;
		double Height = 0.0;
		double Chance = 0.0;
		/** Orden al repartir (0 primero): formaciones, objetos del camino, agujas y peñascos grandes. */
		int32 Priority = 0;
	};

	/**
	 * Si tiene sentido rebuscar en este decorado del camino y con qué huella (aproximada a su malla,
	 * TN_ProcMapFormationMeshes.h y TN_ProcMapPropMeshes.h). Fuera: arcos, hitos lejanos, vegetación (troncos,
	 * árboles, setas), la fumarola (quema), vallas y filas de conos, y los peñascos pequeños.
	 */
	inline bool SearchableOf(const TNProcMap::FFeature& F, FSearchable& Out)
	{
		using TNProcMap::EFeature;
		using TNProcMap::EFormation;
		using TNProcMap::EPathProp;
		const double R = F.Radius;
		Out = FSearchable();
		Out.Height = F.Height;
		Out.Radius = R;
		switch (F.Type)
		{
			case EFeature::Formation:
			{
				const EFormation Kind = static_cast<EFormation>(F.Aux);
				if (TNProcMap::IsArchFormation(Kind) || TNProcMap::IsLandmarkFormation(Kind) || Kind == EFormation::Fumarole)
				{
					return false;
				}
				Out.Priority = 0;
				Out.Chance = 1.0;
				switch (Kind)
				{
					case EFormation::Shipwreck:      Out.HalfLength = R * 0.7; Out.Radius = R * 0.42; break;
					case EFormation::BalancedRock:   Out.Radius = R * 0.9; break;
					case EFormation::Wagon:          Out.HalfLength = 130.0; Out.Radius = 115.0; break;
					case EFormation::Cannon:         Out.HalfLength = 80.0; Out.Radius = 110.0; break;
					case EFormation::Bunker:         Out.HalfLength = R * 0.15; Out.Radius = R * 0.85; break;
					case EFormation::WatchTower:     Out.Radius = 150.0; break;
					case EFormation::TankWreck:      Out.HalfLength = 110.0; Out.Radius = 195.0; break;
					case EFormation::Anchor:         Out.Radius = R * 0.6; break;
					// El altar bajo del centro del círculo de piedras.
					case EFormation::StoneCircle:    Out.Radius = 200.0; Out.Height = 120.0; break;
					case EFormation::FossilSkull:    Out.HalfLength = R * 0.45; Out.Radius = R * 0.6; break;
					case EFormation::ObsidianSpires: Out.Radius = R * 0.8; Out.Chance = 0.7; break;
					case EFormation::ColossalTurtle: Out.HalfLength = R * 0.2; Out.Radius = R * 0.95; break;
					case EFormation::WaterTower:     Out.Radius = R * 0.85; break;
					// Cabeza de piedra, basalto, chimenea de hadas, sacos terreros, caracola y obelisco: redondos.
					default: break;
				}
				return true;
			}
			case EFeature::PathProp:
			{
				Out.Priority = 1;
				switch (static_cast<EPathProp>(F.Aux))
				{
					case EPathProp::CrateStack:    Out.Chance = 0.65; break;
					case EPathProp::BarrelGroup:   Out.Chance = 0.65; break;
					case EPathProp::HayBales:      Out.Chance = 0.6; break;
					case EPathProp::Sandcastle:    Out.Chance = 0.7; break;
					case EPathProp::Rowboat:       Out.Radius = 90.0; Out.HalfLength = FMath::Max(0.0, F.Length * 0.5 - 85.0); Out.Chance = 0.7; break;
					case EPathProp::BeachSet:      Out.Radius = R * 0.75; Out.Chance = 0.6; break;
					case EPathProp::Totem:         Out.Chance = 0.6; break;
					case EPathProp::RuinColumn:    Out.Radius = R * 0.85; Out.Chance = 0.6; break;
					case EPathProp::SkullRock:     Out.Chance = 0.65; break;
					case EPathProp::PotteryJars:   Out.Chance = 0.8; break;
					case EPathProp::CrystalSpikes: Out.Radius = R * 0.8; Out.Chance = 0.45; break;
					case EPathProp::Cairn:         Out.Chance = 0.6; break;
					case EPathProp::MineCart:      Out.Radius = 95.0; Out.HalfLength = FMath::Max(0.0, F.Length * 0.5 - 90.0); Out.Chance = 0.75; break;
					case EPathProp::CrabTraps:     Out.Chance = 0.7; break;
					case EPathProp::MarketStall:   Out.Radius = R * 0.8; Out.Chance = 0.7; break;
					// Vallas, filas de conos y setas gigantes.
					default: return false;
				}
				return true;
			}
			case EFeature::RockSpire:
				Out.Priority = 2;
				Out.Radius = R * 0.9;
				Out.Chance = 0.45;
				return true;
			case EFeature::Boulder:
				// Solo las piedras grandes.
				if (R < 140.0)
				{
					return false;
				}
				Out.Priority = 3;
				Out.Radius = R * 0.95;
				Out.Chance = 0.3;
				return true;
			default:
				return false;
		}
	}
}

void ATN_ProcMapGenerator::SpawnSearchSpots()
{
	UWorld* SpotWorld = GetWorld();
	if (bTerrainOnly || !SpotWorld || SpotWorld->GetNetMode() == NM_Client)
	{
		return;
	}
	const double Yaw0 = GetActorRotation().Yaw;
	// Supervivencia (#724): la densidad de la playa (casi todo el decorado, a 9 m) y su propia lista de objetos.
	const bool bSurvival = NetConfig.Mode == ETNProcGameMode::Survival;
	UClass* SpotClass = bSurvival ? ATN_SurvivalSearchSpot::StaticClass() : ATN_ProcSearchSpot::StaticClass();

	// Candidatos por orden de preferencia: las formaciones (grandes y raras) se quedan su sitio antes que los objetos
	// del camino, las agujas y los peñascos.
	struct FSpotCandidate
	{
		int32 Feature = INDEX_NONE;
		TNSearchSpotPlan::FSearchable Spec;
	};
	TArray<FSpotCandidate> Candidates;
	for (int32 f = 0; f < Layout.Features.Num(); ++f)
	{
		FSpotCandidate Candidate;
		if (TNSearchSpotPlan::SearchableOf(Layout.Features[f], Candidate.Spec))
		{
			if (bSurvival)
			{
				// Los objetos del camino añadidos para rebuscar (PlanSurvivalSearchProps) lo son siempre y van primero.
				const bool bAdded = SurvivalSearchProps.Contains(f);
				Candidate.Spec.Chance = bAdded ? 1.0 : TNSurvivalLoot::SpotChance(Candidate.Spec.Priority);
				Candidate.Spec.Priority = bAdded ? -1 : Candidate.Spec.Priority;
			}
			Candidate.Feature = f;
			Candidates.Add(Candidate);
		}
	}
	Candidates.StableSort([](const FSpotCandidate& A, const FSpotCandidate& B) { return A.Spec.Priority < B.Spec.Priority; });

	TNProcMap::FRng SpotRng(static_cast<uint64>(Layout.Params.Seed) * 2654435761ull + 0x5EA7C4ull);
	// Centro y alcance (radio más semilargo) de los ya elegidos.
	TArray<FVector2D> Taken;
	TArray<double> TakenReach;
	int32 NumSpots = 0;
	for (const FSpotCandidate& Candidate : Candidates)
	{
		const TNProcMap::FFeature& F = Layout.Features[Candidate.Feature];
		if (!SpotRng.Chance(Candidate.Spec.Chance))
		{
			continue;
		}
		const FVector2D C(F.Location.X, F.Location.Y);
		const double Reach = Candidate.Spec.Radius + Candidate.Spec.HalfLength;
		bool bCrowded = false;
		for (int32 t = 0; t < Taken.Num(); ++t)
		{
			// En Supervivencia, como en la playa: de centro a centro y de borde a borde; si no, uno por tramo.
			const double Need = bSurvival ? FMath::Max(TNSurvivalLoot::MinSpacing, Reach + TakenReach[t] + TNSurvivalLoot::MinRimGap)
				: TNSearchSpotPlan::MinSpacing;
			if (FVector2D::DistSquared(C, Taken[t]) < FMath::Square(Need))
			{
				bCrowded = true;
				break;
			}
		}
		if (bCrowded)
		{
			continue;
		}

		// En el suelo del centro del decorado (las formaciones de explanada, a la cota del camino, como su malla), con
		// su +X a lo largo de su Dir (el eje de la huella).
		const double BaseZ = F.Type == TNProcMap::EFeature::Formation ? F.Location.Z : TerrainHeightMap(C);
		const FVector2D Axis = F.Dir.GetSafeNormal().IsNearlyZero() ? FVector2D(1.0, 0.0) : F.Dir.GetSafeNormal();
		const double Yaw = FMath::RadiansToDegrees(TNProcMap::AngleOf(Axis)) + Yaw0;
		ATN_ProcSearchSpot* Spot = Cast<ATN_ProcSearchSpot>(SpawnMapActor(SpotClass,
			FTransform(FRotator(0.0, Yaw, 0.0), MapToWorld(FVector(C, BaseZ))), true));
		if (!Spot)
		{
			continue;
		}

		// Polvo del color del camino del bioma, algo más claro (arena, tierra, polvo de roca); en el volcán, ceniza.
		FLinearColor GroundC, PathC, RockC, BedC;
		ResolveBiomeColors(F.Biome, GroundC, PathC, RockC, BedC);
		const FLinearColor DustC = F.Biome == ETNProcBiome::Volcanic
			? FMath::Lerp(PathC, FLinearColor(0.42f, 0.4f, 0.39f), 0.65f)
			: FMath::Lerp(PathC, FLinearColor(0.9f, 0.86f, 0.78f), 0.35f);
		Spot->SetupSpot(static_cast<float>(Candidate.Spec.Radius), static_cast<float>(Candidate.Spec.HalfLength),
			static_cast<float>(Candidate.Spec.Height), DustC);
		Taken.Add(C);
		TakenReach.Add(Reach);
		++NumSpots;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[ProcMap] Decorados para rebuscar: %d de %d candidatos (a %.0f m como mínimo entre sí)."),
		NumSpots, Candidates.Num(), (bSurvival ? TNSurvivalLoot::MinSpacing : TNSearchSpotPlan::MinSpacing) / 100.0);
}
