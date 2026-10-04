#include "World/ProcMap/TN_ProcMapTypes.h"
#include "Settings/TN_GameplayAssetSettings.h"
#include "World/ProcMap/TN_ProcMapGenerate.h"
#include "World/ProcMap/TN_ProcWaterActors.h"
#include "Engine/StaticMesh.h"

TNProcMap::FGenParams FTNProcMapProfile::ToGenParams(uint32 Seed) const
{
	TNProcMap::FGenParams P;
	P.Seed = Seed;
	P.GridSize = GridSize;
	P.ModuleSize = ModuleSize;
	P.Coverage = Coverage;
	P.Sinuosity = Sinuosity;
	P.PathWidthMin = PathWidthMin;
	P.PathWidthMax = PathWidthMax;
	P.NarrowChance = NarrowChance;
	P.NumCrossings = NumCrossings;
	P.NumBranches = NumBranches;
	P.NumLanes = NumLanes;
	P.BranchMaxModules = BranchMaxModules;
	P.GapsPerKm = GapsPerKm;
	P.GapMin = GapMin;
	P.GapMax = GapMax;
	P.ColossalHeightMin = ColossalHeightMin;
	P.ColossalHeightMax = ColossalHeightMax;
	P.EmptyMode = EmptyModuleMode;
	P.NumBiomeRegions = NumBiomeRegions;
	P.bRiver = bRiver;
	P.EggNestEveryNPortals = EggNestEveryNPortals;
	P.Difficulty01 = Difficulty01;
	return TNProcMap::SanitizeParams(P);
}

const UTN_ProcBiomeDataAsset* UTN_ProcMapSettings::FindBiome(ETNProcBiome Biome) const
{
	for (const UTN_ProcBiomeDataAsset* Asset : Biomes)
	{
		if (Asset && Asset->Biome == Biome) { return Asset; }
	}
	return nullptr;
}

FTNProcMapProfile UTN_ProcMapSettings::ResolveProfile(ETNProcGameMode Mode, ETNProcDifficulty Difficulty) const
{
	for (const FTNProcModeProfile& Entry : Profiles)
	{
		if (Entry.Mode == Mode && Entry.Difficulty == Difficulty) { return Entry.Profile; }
	}
	return TN_MakeDefaultProcProfile(Mode, Difficulty);
}

FTNProcMapProfile TN_MakeDefaultProcProfile(ETNProcGameMode Mode, ETNProcDifficulty Difficulty)
{
	const int32 D = static_cast<int32>(Difficulty);
	auto Pick = [D](auto Easy, auto Normal, auto Hard) { return D == 0 ? Easy : (D == 1 ? Normal : Hard); };

	FTNProcMapProfile P;
	P.Difficulty01 = Pick(0.2f, 0.5f, 0.9f);
	// Camino muy poblado de juego: bastantes más peligros, trampas, enemigos y saltos que al principio (se pidió
	// «muchísimos más» obstáculos que no sean decorado y, después, aún más saltos). GapsPerKm es la densidad real de
	// huecos donde caben (TNProcMap::PlaceGapsOn).
	P.HazardDensity = Pick(1.6f, 2.4f, 3.2f);
	P.GapsPerKm = Pick(9.0f, 13.0f, 17.0f);
	P.EggNestEveryNPortals = Pick(1, 2, 3);

	switch (Mode)
	{
		case ETNProcGameMode::Race:
			// Carrera: mapas cortos y rápidos, con atajos para fastidiarse.
			P.GridSize = Pick(2, 3, 4);
			P.Coverage = 0.9f;
			P.Sinuosity = 1.6f;
			P.NumCrossings = Pick(0, 1, 1);
			P.NumBranches = Pick(4, 7, 9);
			P.NumLanes = 0;
			P.StormSpeed = 0.f;
			break;

		case ETNProcGameMode::TwoVsTwo:
			// 2vs2: camino principal que se separa en carriles con puzles de lanzamiento.
			P.GridSize = Pick(2, 3, 4);
			P.Coverage = 0.9f;
			P.Sinuosity = 1.5f;
			P.NumCrossings = Pick(0, 0, 1);
			P.NumBranches = Pick(2, 3, 4);
			P.NumLanes = Pick(1, 2, 3);
			P.StormSpeed = 0.f;
			break;

		case ETNProcGameMode::Survival:
			// Supervivencia: el trazado sale de TNProcMap::MakeSurvivalParams; aquí solo lo que usa el actor (sin tormenta).
			P.StormSpeed = 0.f;
			break;

		case ETNProcGameMode::Coop:
		default:
			// Coop: mapa largo; más cruces y ramas con la dificultad.
			P.GridSize = Pick(3, 6, 8);
			P.Coverage = 0.78f;
			P.Sinuosity = 1.8f;
			P.NumCrossings = Pick(1, 2, 4);
			P.NumBranches = Pick(6, 12, 16);
			P.NumLanes = 0;
			// La tortuga anda a 200 cm/s: 80, 90 y 100 % de eso (y la partida la limita a su velocidad de andar).
			P.StormSpeed = Pick(160.f, 180.f, 200.f);
			P.StormGraceSeconds = Pick(90.f, 60.f, 45.f);
			break;
	}
	return P;
}

void TN_DefaultBiomeColors(ETNProcBiome Biome, FLinearColor& OutGround, FLinearColor& OutPath, FLinearColor& OutRock, FLinearColor& OutBed)
{
	OutBed = FLinearColor(0.55f, 0.5f, 0.36f);
	switch (Biome)
	{
		case ETNProcBiome::Jungle:
			OutGround = FLinearColor(0.10f, 0.32f, 0.08f); OutPath = FLinearColor(0.36f, 0.24f, 0.12f); OutRock = FLinearColor(0.22f, 0.24f, 0.18f); break;
		case ETNProcBiome::Beach:
			OutGround = FLinearColor(0.86f, 0.76f, 0.52f); OutPath = FLinearColor(0.74f, 0.62f, 0.40f); OutRock = FLinearColor(0.55f, 0.5f, 0.42f); OutBed = FLinearColor(0.8f, 0.72f, 0.5f); break;
		case ETNProcBiome::Desert:
			OutGround = FLinearColor(0.78f, 0.56f, 0.28f); OutPath = FLinearColor(0.62f, 0.44f, 0.22f); OutRock = FLinearColor(0.55f, 0.35f, 0.2f); break;
		case ETNProcBiome::Volcanic:
			OutGround = FLinearColor(0.12f, 0.08f, 0.07f); OutPath = FLinearColor(0.3f, 0.2f, 0.16f); OutRock = FLinearColor(0.08f, 0.06f, 0.06f); OutBed = FLinearColor(0.1f, 0.08f, 0.08f); break;
		case ETNProcBiome::Water:
			OutGround = FLinearColor(0.7f, 0.66f, 0.46f); OutPath = FLinearColor(0.8f, 0.72f, 0.5f); OutRock = FLinearColor(0.45f, 0.45f, 0.4f); OutBed = FLinearColor(0.35f, 0.5f, 0.45f); break;
		case ETNProcBiome::Rocky:
			OutGround = FLinearColor(0.38f, 0.37f, 0.35f); OutPath = FLinearColor(0.5f, 0.46f, 0.4f); OutRock = FLinearColor(0.26f, 0.26f, 0.27f); break;
		case ETNProcBiome::Mangrove:
			OutGround = FLinearColor(0.2f, 0.26f, 0.12f); OutPath = FLinearColor(0.3f, 0.25f, 0.15f); OutRock = FLinearColor(0.22f, 0.2f, 0.15f); OutBed = FLinearColor(0.2f, 0.18f, 0.1f); break;
		case ETNProcBiome::Human:
		default:
			OutGround = FLinearColor(0.62f, 0.58f, 0.45f); OutPath = FLinearColor(0.45f, 0.45f, 0.45f); OutRock = FLinearColor(0.5f, 0.45f, 0.4f); break;
	}
}

void TN_DefaultBiomeScatter(ETNProcBiome Biome, TArray<FTNProcScatterLayer>& Out)
{
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));

	auto Layer = [&Out](UStaticMesh* Mesh, ETNProcScatterZone Zone, float Density, FVector2D Scale, FVector Axes, bool bCollision,
		FLinearColor Tint, float MinPathDist = 300.f, float MaxSlope = 35.f, float ZOffset = -10.f)
	{
		if (!Mesh) { return; }
		FTNProcScatterLayer L;
		L.Mesh = Mesh;
		L.Zone = Zone;
		L.DensityPer100m2 = Density;
		L.ScaleRange = Scale;
		L.ScaleAxes = Axes;
		L.bCollision = bCollision;
		L.bApplyTint = true;
		L.Tint = Tint;
		L.MinPathDistance = MinPathDist;
		L.MaxSlopeDeg = MaxSlope;
		L.ZOffset = ZOffset;
		Out.Add(L);
	};

	switch (Biome)
	{
		case ETNProcBiome::Jungle:
			Layer(Cone, ETNProcScatterZone::OffPath, 1.2f, FVector2D(2.8, 4.5), FVector(1, 1, 2.4), true, FLinearColor(0.05f, 0.25f, 0.05f), 500.f);
			Layer(Sphere, ETNProcScatterZone::OffPath, 3.0f, FVector2D(1.2, 2.4), FVector(1, 1, 0.6), false, FLinearColor(0.1f, 0.4f, 0.08f), 150.f, 45.f);
			Layer(Cone, ETNProcScatterZone::Walls, 2.0f, FVector2D(4.0, 7.0), FVector(1, 1, 2.6), true, FLinearColor(0.04f, 0.2f, 0.05f), 0.f, 70.f);
			break;
		case ETNProcBiome::Beach:
			Layer(Cylinder, ETNProcScatterZone::OffPath, 0.35f, FVector2D(0.28, 0.4), FVector(1, 1, 16), true, FLinearColor(0.45f, 0.32f, 0.18f), 600.f, 25.f);
			Layer(Cube, ETNProcScatterZone::PathEdge, 1.2f, FVector2D(0.15, 0.4), FVector(1, 0.7, 0.5), false, FLinearColor(0.3f, 0.45f, 0.7f), 60.f, 30.f, 0.f);
			Layer(Sphere, ETNProcScatterZone::PathEdge, 1.5f, FVector2D(0.1, 0.2), FVector(1, 1, 0.5), false, FLinearColor(0.95f, 0.9f, 0.85f), 30.f, 30.f, 0.f);
			Layer(Sphere, ETNProcScatterZone::Walls, 0.8f, FVector2D(3.0, 6.0), FVector(1.3, 1, 0.8), true, FLinearColor(0.6f, 0.55f, 0.45f), 0.f, 70.f);
			break;
		case ETNProcBiome::Desert:
			Layer(Cylinder, ETNProcScatterZone::OffPath, 0.5f, FVector2D(0.35, 0.55), FVector(1, 1, 5), true, FLinearColor(0.2f, 0.45f, 0.15f), 400.f, 30.f);
			Layer(Sphere, ETNProcScatterZone::OffPath, 0.6f, FVector2D(1.2, 3.2), FVector(1.4, 1, 0.7), true, FLinearColor(0.6f, 0.38f, 0.2f), 300.f, 40.f);
			Layer(Sphere, ETNProcScatterZone::Walls, 0.7f, FVector2D(4.0, 8.0), FVector(1.4, 1, 0.8), true, FLinearColor(0.55f, 0.35f, 0.18f), 0.f, 75.f);
			break;
		case ETNProcBiome::Volcanic:
			Layer(Cone, ETNProcScatterZone::OffPath, 0.8f, FVector2D(2.0, 4.0), FVector(1, 1, 1.3), true, FLinearColor(0.06f, 0.05f, 0.05f), 400.f, 45.f);
			Layer(Sphere, ETNProcScatterZone::OffPath, 2.0f, FVector2D(0.4, 1.0), FVector(1.2, 1, 0.8), false, FLinearColor(0.12f, 0.08f, 0.07f), 100.f, 50.f);
			Layer(Cone, ETNProcScatterZone::Walls, 1.0f, FVector2D(5.0, 9.0), FVector(1, 1, 1.8), true, FLinearColor(0.08f, 0.05f, 0.05f), 0.f, 75.f);
			break;
		case ETNProcBiome::Water:
			Layer(Cylinder, ETNProcScatterZone::Shallows, 3.0f, FVector2D(0.05, 0.09), FVector(1, 1, 3.5), false, FLinearColor(0.3f, 0.55f, 0.2f), 150.f, 60.f);
			Layer(Cylinder, ETNProcScatterZone::OffPath, 0.4f, FVector2D(0.25, 0.35), FVector(1, 1, 14), true, FLinearColor(0.45f, 0.32f, 0.18f), 400.f, 30.f);
			break;
		case ETNProcBiome::Rocky:
			Layer(Sphere, ETNProcScatterZone::OffPath, 0.7f, FVector2D(2.0, 5.0), FVector(1.2, 1, 0.9), true, FLinearColor(0.35f, 0.35f, 0.36f), 300.f, 60.f);
			Layer(Cone, ETNProcScatterZone::OffPath, 0.2f, FVector2D(2.0, 3.5), FVector(1, 1, 3), true, FLinearColor(0.3f, 0.3f, 0.32f), 500.f, 50.f);
			Layer(Sphere, ETNProcScatterZone::Walls, 1.0f, FVector2D(5.0, 10.0), FVector(1.2, 1, 1.0), true, FLinearColor(0.28f, 0.28f, 0.3f), 0.f, 80.f);
			break;
		case ETNProcBiome::Mangrove:
			Layer(Cylinder, ETNProcScatterZone::Shallows, 1.4f, FVector2D(0.4, 0.6), FVector(1, 1, 5), true, FLinearColor(0.25f, 0.2f, 0.12f), 250.f, 60.f);
			Layer(Sphere, ETNProcScatterZone::Shallows, 1.0f, FVector2D(3.0, 5.0), FVector(1, 1, 0.45), false, FLinearColor(0.1f, 0.3f, 0.1f), 400.f, 60.f, 500.f);
			Layer(Cylinder, ETNProcScatterZone::Shallows, 2.5f, FVector2D(0.05, 0.09), FVector(1, 1, 3), false, FLinearColor(0.3f, 0.5f, 0.2f), 120.f, 60.f);
			break;
		case ETNProcBiome::Human:
		default:
			Layer(Cube, ETNProcScatterZone::OffPath, 0.4f, FVector2D(1.0, 1.6), FVector(1, 1, 1), true, FLinearColor(0.5f, 0.33f, 0.15f), 300.f, 25.f, 0.f);
			Layer(Cube, ETNProcScatterZone::PathEdge, 0.6f, FVector2D(1.0, 1.2), FVector(2.5, 0.25, 0.9), true, FLinearColor(0.85f, 0.2f, 0.15f), 80.f, 20.f, 0.f);
			Layer(Cone, ETNProcScatterZone::OffPath, 0.3f, FVector2D(1.6, 2.2), FVector(1, 1, 0.25), false, FLinearColor(0.95f, 0.8f, 0.2f), 200.f, 20.f, 180.f);
			Layer(Cube, ETNProcScatterZone::Walls, 0.5f, FVector2D(3.0, 5.0), FVector(2.0, 1, 1.0), true, FLinearColor(0.4f, 0.42f, 0.45f), 0.f, 70.f);
			break;
	}
}

void TN_DefaultBiomeHazards(ETNProcBiome Biome, TArray<FTNProcHazardEntry>& Out)
{
	auto Add = [&Out](UClass* Class, float PerKm, ETNProcHazardPlacement Placement, ETNProcDifficulty MinDiff, float Clearance, float ZOffset = 0.f)
	{
		if (!Class) { return; }
		FTNProcHazardEntry E;
		E.ActorClass = Class;
		E.PerKm = PerKm;
		E.Placement = Placement;
		E.MinDifficulty = MinDiff;
		E.Clearance = Clearance;
		E.ZOffset = ZOffset;
		Out.Add(E);
	};
	auto BP = [](const TCHAR* Path) -> UClass* { return LoadClass<AActor>(nullptr, Path); };

	UClass* Items = BP(TEXT("/Game/Blueprints/Gameplay/Items/BP_ItemSpawnZone.BP_ItemSpawnZone_C"));
	UClass* Score = UTN_GameplayAssetSettings::GetScorePickupClass();
	UClass* Crabs = BP(TEXT("/Game/Blueprints/Gameplay/Enemies/Crabs/BP_CrabSpawnZone.BP_CrabSpawnZone_C"));
	UClass* Gulls = BP(TEXT("/Game/Blueprints/Gameplay/Enemies/Seagull/BP_SeagullSpawnZone.BP_SeagullSpawnZone_C"));
	UClass* Jelly = BP(TEXT("/Game/Blueprints/Gameplay/Items/BP_JellyfishActor.BP_JellyfishActor_C"));
	UClass* Banana = BP(TEXT("/Game/Blueprints/Gameplay/Hazards/BP_BananaPeel.BP_BananaPeel_C"));
	UClass* Slow = BP(TEXT("/Game/Blueprints/Gameplay/Hazards/BP_SlowZoneVolume.BP_SlowZoneVolume_C"));

	Add(Items, 1.5f, ETNProcHazardPlacement::OnPath, ETNProcDifficulty::Easy, 12000.f);
	Add(Score, 4.f, ETNProcHazardPlacement::OnPath, ETNProcDifficulty::Easy, 3000.f, 60.f);

	switch (Biome)
	{
		case ETNProcBiome::Water:
			Add(ATN_ProcWaterBouncer::StaticClass(), 6.f, ETNProcHazardPlacement::InWater, ETNProcDifficulty::Easy, 1800.f);
			Add(ATN_ProcWaterCurrent::StaticClass(), 2.f, ETNProcHazardPlacement::InWater, ETNProcDifficulty::Easy, 5000.f);
			Add(ATN_ProcWaterPredator::StaticClass(), 1.5f, ETNProcHazardPlacement::InWater, ETNProcDifficulty::Normal, 9000.f);
			Add(ATN_ProcWhirlpool::StaticClass(), 1.f, ETNProcHazardPlacement::InWater, ETNProcDifficulty::Normal, 9000.f);
			Add(Jelly, 1.5f, ETNProcHazardPlacement::PathEdge, ETNProcDifficulty::Easy, 5000.f);
			break;
		case ETNProcBiome::Mangrove:
			Add(ATN_ProcWaterBouncer::StaticClass(), 4.f, ETNProcHazardPlacement::InWater, ETNProcDifficulty::Easy, 2000.f);
			Add(ATN_ProcWaterCurrent::StaticClass(), 1.f, ETNProcHazardPlacement::InWater, ETNProcDifficulty::Normal, 6000.f);
			Add(ATN_ProcWaterPredator::StaticClass(), 1.f, ETNProcHazardPlacement::InWater, ETNProcDifficulty::Hard, 10000.f);
			Add(Slow, 1.f, ETNProcHazardPlacement::OnPath, ETNProcDifficulty::Easy, 8000.f);
			break;
		case ETNProcBiome::Beach:
			Add(Crabs, 1.5f, ETNProcHazardPlacement::NearPath, ETNProcDifficulty::Normal, 8000.f);
			Add(Gulls, 0.6f, ETNProcHazardPlacement::AbovePath, ETNProcDifficulty::Hard, 15000.f, 600.f);
			Add(Jelly, 2.f, ETNProcHazardPlacement::PathEdge, ETNProcDifficulty::Easy, 4000.f);
			Add(Banana, 1.5f, ETNProcHazardPlacement::OnPath, ETNProcDifficulty::Easy, 4000.f);
			break;
		case ETNProcBiome::Desert:
			Add(Crabs, 1.f, ETNProcHazardPlacement::NearPath, ETNProcDifficulty::Normal, 9000.f);
			Add(Slow, 1.2f, ETNProcHazardPlacement::OnPath, ETNProcDifficulty::Easy, 7000.f);
			break;
		case ETNProcBiome::Rocky:
			Add(Crabs, 1.2f, ETNProcHazardPlacement::NearPath, ETNProcDifficulty::Normal, 9000.f);
			Add(Gulls, 0.8f, ETNProcHazardPlacement::AbovePath, ETNProcDifficulty::Normal, 12000.f, 600.f);
			break;
		case ETNProcBiome::Human:
			Add(Banana, 2.5f, ETNProcHazardPlacement::OnPath, ETNProcDifficulty::Easy, 3000.f);
			Add(Gulls, 0.8f, ETNProcHazardPlacement::AbovePath, ETNProcDifficulty::Normal, 12000.f, 600.f);
			break;
		case ETNProcBiome::Volcanic:
		case ETNProcBiome::Jungle:
		default:
			Add(Crabs, 0.8f, ETNProcHazardPlacement::NearPath, ETNProcDifficulty::Hard, 10000.f);
			break;
	}
}

void UTN_ProcBiomeDataAsset::ResetToGreyboxDefaults()
{
	TN_DefaultBiomeColors(Biome, GroundColor, PathColor, RockColor, BedColor);
	Scatter.Reset();
	Hazards.Reset();
	TN_DefaultBiomeScatter(Biome, Scatter);
	TN_DefaultBiomeHazards(Biome, Hazards);
	WaterBouncerMesh = nullptr;
	WaterBouncerColor = FLinearColor(0.9f, 0.5f, 0.9f);
	MarkPackageDirty();
}

void UTN_ProcMapSettings::FillDefaultProfiles()
{
	Profiles.Reset();
	for (int32 M = 0; M < static_cast<int32>(ETNProcGameMode::Count); ++M)
	{
		const ETNProcGameMode Mode = static_cast<ETNProcGameMode>(M);
		if (Mode == ETNProcGameMode::Classic || Mode == ETNProcGameMode::Survival || Mode == ETNProcGameMode::Karts
			|| Mode == ETNProcGameMode::FreeForAll)
		{
			continue; // viajan a LVL_Run o a LVL_Tct (o, los karts, usan los perfiles del cooperativo)
		}
		for (int32 D = 0; D < static_cast<int32>(ETNProcDifficulty::Count); ++D)
		{
			FTNProcModeProfile& Entry = Profiles.AddDefaulted_GetRef();
			Entry.Mode = Mode;
			Entry.Difficulty = static_cast<ETNProcDifficulty>(D);
			Entry.Profile = TN_MakeDefaultProcProfile(Entry.Mode, Entry.Difficulty);
		}
	}
	MarkPackageDirty();
}
