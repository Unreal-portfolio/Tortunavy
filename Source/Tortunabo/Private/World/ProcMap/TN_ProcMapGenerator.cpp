// ─────────────────────────────────────────────────────────────────────────────
// ATN_ProcMapGenerator — núcleo: ciclo de vida, red y consultas.
// La construcción está repartida en TN_ProcMapGenerator_Build.cpp (terreno, agua,
// estructuras) y TN_ProcMapGenerator_Spawn.cpp (vegetación, actores, peligros).
// ─────────────────────────────────────────────────────────────────────────────

#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "TN_ProcMapAmbientFX.h"
#include "Components/PointLightComponent.h"
#include "World/ProcMap/TN_ProcMapGenerate.h"
#include "World/ProcMap/TN_ProcMapTerrain.h"
#include "World/ProcMap/TN_ProcEggNest.h"
#include "World/ProcMap/TN_ProcStartStructure.h"
#include "../../Art/TN_ArtPieces.h"
#include "Core/TN_Log.h"
#include "Player/MP_GamePlayerController.h"
#include "ProceduralMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "PCGComponent.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "World/ProcMap/TN_ProcMapSurvival.h"

ATN_ProcMapGenerator::ATN_ProcMapGenerator()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(2.f);

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	BasicPlane = PlaneFinder.Object;
	BasicCube = CubeFinder.Object;
	BasicCylinder = CylinderFinder.Object;
	BasicSphere = SphereFinder.Object;
	BasicCone = ConeFinder.Object;
}

void ATN_ProcMapGenerator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_ProcMapGenerator, NetConfig);
	DOREPLIFETIME_CONDITION(ATN_ProcMapGenerator, Settings, COND_InitialOnly);
}

void ATN_ProcMapGenerator::BeginPlay()
{
	Super::BeginPlay();

	// Cliente que entra con el mapa ya pedido: construir lo que diga la réplica. El generador colocado en el nivel trae la
	// NetConfig guardada en el mapa, que el servidor nunca usa (siempre genera una posterior): construirla costaba a quien
	// entra tarde ~25 s de mapa equivocado antes del bueno. Ese espera a su OnRep_NetConfig (#345).
	if (!HasAuthority() && !IsNetStartupActor() && NetConfig.Generation > 0 && BuiltGeneration != NetConfig.Generation)
	{
		BuildFromNetConfig();
	}
}

void ATN_ProcMapGenerator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// Brasas, bandadas y confeti (locales; nada en un servidor dedicado). El confeti estalla la primera
	// vez que cada personaje entra en el volumen de meta (desde la línea, ya en el agua).
	TNAmbientFX::TickOwner(this, DeltaTime);
	if (GetNetMode() != NM_DedicatedServer && bMapReady)
	{
		for (const TNProcMap::FFeature& F : Layout.Features)
		{
			if (F.Type != TNProcMap::EFeature::Finish) { continue; }
			static TSet<TWeakObjectPtr<AActor>> Celebrated;
			for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
			{
				const FVector P = WorldToMap(It->GetActorLocation());
				const bool bIn = P.Y >= F.Location.Y && P.Y <= F.Location.Y + F.Length && FMath::Abs(P.X - F.Location.X) <= F.Height * 0.5 + 1500.0;
				if (!bIn || Celebrated.Contains(*It)) { continue; }
				Celebrated.Add(*It);
				TNAmbientFX::FOwnerFX* FX = TNAmbientFX::Registry().Find(this);
				if (!FX) { continue; }
				for (TNAmbientFX::FEmitter& E : FX->Emitters)
				{
					if (E.Desc.Shape == TNAmbientFX::EShape::Flake) { TNAmbientFX::Burst(E, 60); }
				}
			}
			break;
		}
	}

	if (HasAuthority())
	{
		// Sin GameMode del mapa procedural (p. ej. el nivel abierto a mano): generar solo.
		if (NetConfig.Generation == 0 && bAutoGenerateIfIdle)
		{
			IdleTimer += DeltaTime;
			if (IdleTimer > 0.5f)
			{
				UE_LOG(LogTortunabo, Log, TEXT("[ProcMap] Ningún GameMode pidió mapa: generando con los valores de edición."));
				ServerGenerate(bEditorRandomSeed ? FMath::Rand() : EditorSeed, EditorMode, EditorDifficulty);
			}
		}
		return;
	}

	FreezeLocalPawnUntilReady();
	if (bMapReady && !bReportedReady)
	{
		ReportReadyToServer();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Generación
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGenerator::ServerGenerate(int32 InSeed, ETNProcGameMode InMode, ETNProcDifficulty InDifficulty)
{
	if (!HasAuthority())
	{
		return;
	}
	NetConfig.Seed = InSeed;
	NetConfig.Mode = InMode;
	NetConfig.Difficulty = InDifficulty;
	NetConfig.Generation += 1;
	BuildFromNetConfig();
	ForceNetUpdate();
}

void ATN_ProcMapGenerator::ServerGenerateSurvival(int32 InSeed, int32 InSurvivalDifficulty)
{
	if (!HasAuthority())
	{
		return;
	}
	NetConfig.SurvivalDifficulty = FMath::Clamp(InSurvivalDifficulty, TNProcMap::SurvivalMinDifficulty, TNProcMap::SurvivalMaxDifficulty);
	ServerGenerate(InSeed, ETNProcGameMode::Survival, ETNProcDifficulty::Normal);
}

void ATN_ProcMapGenerator::GenerateInEditor()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (World->IsGameWorld() && !HasAuthority())
	{
		return;
	}
	NetConfig.Seed = bEditorRandomSeed ? FMath::Rand() : EditorSeed;
	NetConfig.Mode = EditorMode;
	NetConfig.Difficulty = EditorDifficulty;
	NetConfig.SurvivalDifficulty = EditorSurvivalDifficulty;
	NetConfig.Generation += 1;
	BuildFromNetConfig();
}

void ATN_ProcMapGenerator::OnRep_NetConfig()
{
	if (NetConfig.Generation > 0 && NetConfig.Generation != BuiltGeneration)
	{
		BuildFromNetConfig();
	}
}

void ATN_ProcMapGenerator::BuildFromNetConfig()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double T0 = FPlatformTime::Seconds();
	Clear();

	if (!BuildLayout())
	{
		UE_LOG(LogTortunabo, Error, TEXT("[ProcMap] No se pudo generar el layout (semilla %d): %s"), NetConfig.Seed, ANSI_TO_TCHAR(Layout.FailReason));
		return;
	}
	const double T1 = FPlatformTime::Seconds();

	BuildTerrain();
	const double T2 = FPlatformTime::Seconds();
	BuildWater();
	BuildStructures();
	if (!bSkipScatter && !bTerrainOnly)
	{
		BuildScatter();
		BuildFlora();
	}
	const double T3 = FPlatformTime::Seconds();

	const bool bGameWorld = World->IsGameWorld();
	if (bGameWorld)
	{
		SpawnTraversalActors();
		if (World->GetNetMode() != NM_Client)
		{
			SpawnServerActors();
		}
		if (!bTerrainOnly)
		{
			SpawnHazards();
			// Después de los peligros: las conchas del plan no pisan lo que estos han puesto (HazardSpots).
			SpawnShells();
			RunBiomePCG();
		}
	}
	BuildProgressIndex();

	bMapReady = true;
	bReportedReady = false;
	BuiltGeneration = NetConfig.Generation;

	if (bDebugDraw)
	{
		DrawDebug();
	}

	const double T4 = FPlatformTime::Seconds();
	UE_LOG(LogTortunabo, Log,
		TEXT("[ProcMap] Mapa listo · semilla %d · %s/%s · grid %dx%d · %d módulos en ruta · %d cruces · %d ramas · camino %.2f km (~%.0f min a 5,5 m/s) · layout %.2fs terreno %.2fs resto %.2fs total %.2fs · ajustes %s"),
		NetConfig.Seed, *UEnum::GetValueAsString(NetConfig.Mode), *UEnum::GetValueAsString(NetConfig.Difficulty),
		Layout.GridW(), Layout.Params.GridSize, Layout.UniqueModulesOnRoute, Layout.Crossings.Num(), Layout.Branches.Num(),
		Layout.MainLength() / 100000.0, EstimateTraversalMinutes(550.f), T1 - T0, T2 - T1, T4 - T2, T4 - T0,
		Settings ? *Settings->GetName() : TEXT("ninguno (greybox)"));

	OnMapGeneratedNative.Broadcast(BuiltGeneration);
	OnMapGenerated.Broadcast(BuiltGeneration);

	if (bGameWorld && World->GetNetMode() == NM_Client)
	{
		ReportReadyToServer();
	}
}

bool ATN_ProcMapGenerator::BuildLayout()
{
	ActiveProfile = Settings ? Settings->ResolveProfile(NetConfig.Mode, NetConfig.Difficulty)
		: TN_MakeDefaultProcProfile(NetConfig.Mode, NetConfig.Difficulty);

	// Supervivencia (#273): su propio perfil, alargado y lineal, con la dificultad 1–5 del nivel.
	const bool bSurvival = NetConfig.Mode == ETNProcGameMode::Survival;
	const int32 SurvivalDifficulty = NetConfig.SurvivalDifficulty > 0 ? NetConfig.SurvivalDifficulty
		: 1 + 2 * static_cast<int32>(NetConfig.Difficulty);
	if (bSurvival)
	{
		return TNProcMap::GenerateSurvivalLayout(static_cast<uint32>(NetConfig.Seed), SurvivalDifficulty, Layout) != 0;
	}

	// Reintentos deterministas: todas las máquinas prueban la misma secuencia de semillas.
	for (int32 Attempt = 0; Attempt < 5; ++Attempt)
	{
		const uint32 Seed = static_cast<uint32>(NetConfig.Seed) + static_cast<uint32>(Attempt) * 7919u;
		if (TNProcMap::GenerateLayout(ActiveProfile.ToGenParams(Seed), Layout))
		{
			return true;
		}
		UE_LOG(LogTortunabo, Warning, TEXT("[ProcMap] Intento %d sin layout (%s), reintentando."), Attempt, ANSI_TO_TCHAR(Layout.FailReason));
	}
	return false;
}

void ATN_ProcMapGenerator::Clear()
{
	TNAmbientFX::RemoveOwner(this);
	for (UProceduralMeshComponent* Tile : TerrainTiles)
	{
		if (Tile) { Tile->DestroyComponent(); }
	}
	TerrainTiles.Reset();

	// Las mallas de arte de las estructuras (las pone BuildStructures, hijas de StructureMesh) se van con ellas.
	TNArt::ClearPieceArt(this, TEXT("ProcMapStructures"));
	if (StructureMesh) { StructureMesh->DestroyComponent(); StructureMesh = nullptr; }
	if (DecorMesh) { DecorMesh->DestroyComponent(); DecorMesh = nullptr; }
	if (WaterPlane) { WaterPlane->DestroyComponent(); WaterPlane = nullptr; }

	for (UHierarchicalInstancedStaticMeshComponent* Comp : ScatterComponents)
	{
		if (Comp) { Comp->DestroyComponent(); }
	}
	ScatterComponents.Reset();
	// Gemelos invisibles que guardan la colisión de los objetos macizos con sustituto de arte (TNArt::ApplyToInstances en
	// BuildFlora). Sin sustitutos no hay ninguno.
	{
		TInlineComponentArray<UInstancedStaticMeshComponent*> Instanced(this);
		for (UInstancedStaticMeshComponent* Comp : Instanced)
		{
			if (IsValid(Comp) && TNArt::IsCollisionTwin(Comp)) { Comp->DestroyComponent(); }
		}
	}
	FloraMeshes.Reset();

	for (UPointLightComponent* Light : CaveLights)
	{
		if (Light) { Light->DestroyComponent(); }
	}
	CaveLights.Reset();

	for (UPCGComponent* Comp : PCGComponents)
	{
		if (Comp) { Comp->DestroyComponent(); }
	}
	PCGComponents.Reset();

	for (UPrimitiveComponent* Comp : BoundaryWalls)
	{
		if (Comp) { Comp->DestroyComponent(); }
	}
	BoundaryWalls.Reset();

	for (AActor* Actor : SpawnedActors)
	{
		if (IsValid(Actor)) { Actor->Destroy(); }
	}
	SpawnedActors.Reset();
	EggNests.Reset();
	StartTransforms.Reset();
	BrokenSpanPrizes.Reset();
	HazardSpots.Reset();
	SpecialShellSpots.Reset();
	ShellSummary.Reset();
	// La estructura de salida y los PlayerStart iban en SpawnedActors: ya están destruidos.
	StartStructure.Reset();
	StartPlayerStarts.Reset();

	Heights.Reset();
	PathMask.Reset();
	PathDist.Reset();
	TerrainDetail = TNProcMap::FTerrainDetail();
	ProgressPoints.Reset();
	ProgressBuckets.Reset();
	Layout = TNProcMap::FLayout();
	bMapReady = false;

	if (UWorld* World = GetWorld())
	{
		FlushPersistentDebugLines(World);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Red: sincronía de clientes
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcMapGenerator::ReportReadyToServer()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() != NM_Client)
	{
		return;
	}
	if (AMP_GamePlayerController* PC = Cast<AMP_GamePlayerController>(World->GetFirstPlayerController()))
	{
		PC->ServerReportProcMapReady(BuiltGeneration);
		bReportedReady = true;
	}
}

void ATN_ProcMapGenerator::FreezeLocalPawnUntilReady()
{
	// Mientras el terreno de este cliente no existe, su pawn no debe caer al vacío.
	UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	ACharacter* Character = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Move)
	{
		return;
	}
	bool bWaiting = !bMapReady || BuiltGeneration != NetConfig.Generation;
	if (!bWaiting && bFrozeLocalPawn)
	{
		// La colisión del terreno se cocina en segundo plano: se suelta cuando ya hay suelo del mapa bajo el pawn
		// (como mucho 10 s después de tener el mapa, por si está sobre algo sin colisión).
		const double Now = World->GetTimeSeconds();
		if (ReadySince < 0.0) { ReadySince = Now; }
		if (Now - ReadySince < 10.0 && !MapCollisionUnder(Character->GetActorLocation())) { bWaiting = true; }
	}
	if (bWaiting && Move->MovementMode != MOVE_None)
	{
		Move->DisableMovement();
		bFrozeLocalPawn = true;
	}
	else if (!bWaiting && bFrozeLocalPawn)
	{
		Move->SetMovementMode(MOVE_Falling);
		bFrozeLocalPawn = false;
		ReadySince = -1.0;
	}
}

bool ATN_ProcMapGenerator::MapCollisionUnder(const FVector& WorldLocation) const
{
	UWorld* World = GetWorld();
	if (!World) { return false; }
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(TNProcMapGround), false);
	const bool bHit = World->LineTraceSingleByChannel(Hit, WorldLocation + FVector(0.0, 0.0, 200.0), WorldLocation - FVector(0.0, 0.0, 30000.0), ECC_WorldStatic, Params);
	// El suelo de la estructura de salida (sala o montículo, replicada) también sostiene: su colisión es síncrona.
	return bHit && (Hit.GetActor() == this || Cast<ATN_ProcStartStructure>(Hit.GetActor()) != nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Consultas
// ─────────────────────────────────────────────────────────────────────────────

FVector ATN_ProcMapGenerator::MapToWorld(const FVector& MapPoint) const
{
	return GetActorTransform().TransformPosition(MapPoint);
}

FVector ATN_ProcMapGenerator::WorldToMap(const FVector& WorldPoint) const
{
	return GetActorTransform().InverseTransformPosition(WorldPoint);
}

double ATN_ProcMapGenerator::TerrainHeightMap(const FVector2D& MapPoint) const
{
	if (LatticeNX < 2 || LatticeNY < 2 || Heights.Num() != LatticeNX * LatticeNY)
	{
		return 0.0;
	}
	const double Fx = FMath::Clamp((MapPoint.X - LatticeOrigin.X) / LatticeSpacing, 0.0, static_cast<double>(LatticeNX - 1) - 1e-6);
	const double Fy = FMath::Clamp((MapPoint.Y - LatticeOrigin.Y) / LatticeSpacing, 0.0, static_cast<double>(LatticeNY - 1) - 1e-6);
	const int32 X0 = FMath::FloorToInt(Fx);
	const int32 Y0 = FMath::FloorToInt(Fy);
	// Los mismos triángulos que la malla y su colisión (con su detalle fino), no bilineal: en laderas
	// empinadas la bilineal se separa decenas de cm de la superficie real.
	if (TerrainDetail.IsActive())
	{
		return TerrainDetail.SurfaceAt(Fx, Fy, Heights);
	}
	const double Tx = Fx - X0;
	const double Ty = Fy - Y0;
	return TNProcMap::TerrainQuadHeight(Heights[Y0 * LatticeNX + X0], Heights[Y0 * LatticeNX + X0 + 1],
		Heights[(Y0 + 1) * LatticeNX + X0], Heights[(Y0 + 1) * LatticeNX + X0 + 1], Tx, Ty);
}

FVector ATN_ProcMapGenerator::TerrainNormalMap(const FVector2D& MapPoint) const
{
	const double E = LatticeSpacing;
	const double Hx = TerrainHeightMap(MapPoint + FVector2D(E, 0.0)) - TerrainHeightMap(MapPoint - FVector2D(E, 0.0));
	const double Hy = TerrainHeightMap(MapPoint + FVector2D(0.0, E)) - TerrainHeightMap(MapPoint - FVector2D(0.0, E));
	return FVector(-Hx, -Hy, 2.0 * E).GetSafeNormal();
}

double ATN_ProcMapGenerator::PathDistanceMap(const FVector2D& MapPoint) const
{
	if (PathDist.Num() != LatticeNX * LatticeNY || LatticeNX < 1)
	{
		return 1e9;
	}
	const int32 X = FMath::Clamp(FMath::RoundToInt((MapPoint.X - LatticeOrigin.X) / LatticeSpacing), 0, LatticeNX - 1);
	const int32 Y = FMath::Clamp(FMath::RoundToInt((MapPoint.Y - LatticeOrigin.Y) / LatticeSpacing), 0, LatticeNY - 1);
	return PathDist[Y * LatticeNX + X];
}

float ATN_ProcMapGenerator::GetTerrainHeightAt(const FVector& WorldLocation) const
{
	const FVector Map = WorldToMap(WorldLocation);
	return static_cast<float>(MapToWorld(FVector(Map.X, Map.Y, TerrainHeightMap(FVector2D(Map.X, Map.Y)))).Z);
}

FTransform ATN_ProcMapGenerator::GetStartTransform(int32 PlayerIndex) const
{
	// Con estructura de salida (servidor), los primeros sitios están dentro de ella; el resto, en el anillo.
	if (const ATN_ProcStartStructure* Structure = StartStructure.Get())
	{
		FTransform InStructure;
		if (Structure->GetSpawnTransform(PlayerIndex, ATN_ProcStartStructure::DefaultSpawnHalfHeight, InStructure))
		{
			return InStructure;
		}
	}
	if (StartTransforms.Num() == 0)
	{
		return GetActorTransform();
	}
	return StartTransforms[FMath::Abs(PlayerIndex) % StartTransforms.Num()];
}

ATN_ProcStartStructure* ATN_ProcMapGenerator::GetStartStructure() const
{
	return StartStructure.Get();
}

APlayerStart* ATN_ProcMapGenerator::GetStartPlayerStart(int32 Index) const
{
	return StartPlayerStarts.IsValidIndex(Index) ? StartPlayerStarts[Index].Get() : nullptr;
}

float ATN_ProcMapGenerator::GetMainPathLength() const
{
	return static_cast<float>(Layout.MainLength());
}

float ATN_ProcMapGenerator::EstimateTraversalMinutes(float AverageSpeedCmPerSec) const
{
	return static_cast<float>(TNProcMap::EstimateTraversalSeconds(Layout, AverageSpeedCmPerSec) / 60.0);
}

FVector ATN_ProcMapGenerator::GetPathLocationAtProgress(float Progress, FVector& OutDirection) const
{
	if (Layout.Main.Num() == 0)
	{
		OutDirection = GetActorForwardVector();
		return GetActorLocation();
	}
	FVector2D Dir;
	int32 Index = 0;
	const FVector2D P = TNProcMap::PathDetail::MainPointAt(Layout.Main, Progress, &Dir, &Index);
	const int32 Next = FMath::Min(Index + 1, Layout.Main.Num() - 1);
	const double Span = FMath::Max(1e-3, Layout.Main[Next].S - Layout.Main[Index].S);
	const double T = FMath::Clamp((Progress - Layout.Main[Index].S) / Span, 0.0, 1.0);
	const double Z = FMath::Lerp(Layout.Main[Index].Z, Layout.Main[Next].Z, T);
	OutDirection = GetActorTransform().TransformVectorNoScale(FVector(Dir.X, Dir.Y, 0.0));
	return MapToWorld(FVector(P.X, P.Y, Z));
}

void ATN_ProcMapGenerator::BuildProgressIndex()
{
	ProgressPoints.Reset();
	for (const TNProcMap::FPathSample& S : Layout.Main)
	{
		ProgressPoints.Add({ FVector(S.P.X, S.P.Y, S.Z), static_cast<float>(S.S) });
	}
	for (const TNProcMap::FBranch& B : Layout.Branches)
	{
		const double S0 = Layout.Main[B.ForkSample].S;
		const double S1 = Layout.Main[B.RejoinSample].S;
		const double Len = FMath::Max(1.0, B.Samples.Last().S);
		for (const TNProcMap::FPathSample& S : B.Samples)
		{
			ProgressPoints.Add({ FVector(S.P.X, S.P.Y, S.Z), static_cast<float>(FMath::Lerp(S0, S1, S.S / Len)) });
		}
	}

	ProgressOrigin = FVector2D(-20000.0, -20000.0);
	ProgressW = FMath::Max(1, FMath::CeilToInt((Layout.MaxExtent() + 40000.0) / ProgressCell));
	ProgressH = ProgressW;
	ProgressBuckets.Reset();
	ProgressBuckets.SetNum(ProgressW * ProgressH);
	for (int32 i = 0; i < ProgressPoints.Num(); ++i)
	{
		const int32 X = FMath::Clamp(FMath::FloorToInt((ProgressPoints[i].P.X - ProgressOrigin.X) / ProgressCell), 0, ProgressW - 1);
		const int32 Y = FMath::Clamp(FMath::FloorToInt((ProgressPoints[i].P.Y - ProgressOrigin.Y) / ProgressCell), 0, ProgressH - 1);
		ProgressBuckets[Y * ProgressW + X].Add(i);
	}
}

float ATN_ProcMapGenerator::GetPathProgress(const FVector& WorldLocation) const
{
	if (ProgressPoints.Num() == 0 || ProgressW == 0)
	{
		return 0.f;
	}
	const FVector Map = WorldToMap(WorldLocation);
	const int32 CX = FMath::Clamp(FMath::FloorToInt((Map.X - ProgressOrigin.X) / ProgressCell), 0, ProgressW - 1);
	const int32 CY = FMath::Clamp(FMath::FloorToInt((Map.Y - ProgressOrigin.Y) / ProgressCell), 0, ProgressH - 1);

	// Distancia 3D con la altura reforzada: distingue el tramo del puente del de abajo.
	double Best = TNumericLimits<double>::Max();
	float BestProgress = 0.f;
	for (int32 Ring = 1; Ring <= 6; ++Ring)
	{
		for (int32 Y = FMath::Max(0, CY - Ring); Y <= FMath::Min(ProgressH - 1, CY + Ring); ++Y)
		{
			for (int32 X = FMath::Max(0, CX - Ring); X <= FMath::Min(ProgressW - 1, CX + Ring); ++X)
			{
				for (const int32 Idx : ProgressBuckets[Y * ProgressW + X])
				{
					const FVector D = ProgressPoints[Idx].P - Map;
					const double Score = D.X * D.X + D.Y * D.Y + 2.25 * D.Z * D.Z;
					if (Score < Best)
					{
						Best = Score;
						BestProgress = ProgressPoints[Idx].Progress;
					}
				}
			}
		}
		if (Best < TNumericLimits<double>::Max())
		{
			break;
		}
	}
	return BestProgress;
}

void ATN_ProcMapGenerator::DrawDebug() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (int32 i = 1; i < Layout.Main.Num(); ++i)
	{
		const TNProcMap::FPathSample& A = Layout.Main[i - 1];
		const TNProcMap::FPathSample& B = Layout.Main[i];
		const bool bHigh = (B.Flags & (TNProcMap::PathFlags::Elevated | TNProcMap::PathFlags::Colossal)) != 0;
		DrawDebugLine(World, MapToWorld2D(A.P, A.Z + 150.0), MapToWorld2D(B.P, B.Z + 150.0), bHigh ? FColor::Magenta : FColor::Yellow, true, -1.f, 0, 25.f);
	}
	for (const TNProcMap::FBranch& Br : Layout.Branches)
	{
		for (int32 i = 1; i < Br.Samples.Num(); ++i)
		{
			DrawDebugLine(World, MapToWorld2D(Br.Samples[i - 1].P, Br.Samples[i - 1].Z + 150.0), MapToWorld2D(Br.Samples[i].P, Br.Samples[i].Z + 150.0),
				FColor::Orange, true, -1.f, 0, 20.f);
		}
	}
}
