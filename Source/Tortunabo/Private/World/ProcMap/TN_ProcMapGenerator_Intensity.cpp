// Tabla de intensidad del cooperativo (#788): el plan por tramos y lo que coloca el generador con él.

#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "World/ProcMap/TN_CoopIntensity.h"
#include "World/ProcMap/TN_ProcWaterActors.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/TN_CrabSpawnZone.h"
#include "World/TN_SeagullSpawnZone.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"

namespace TNProcIntensityDetail
{
	/** Distancia mínima (cm) de lo que coloca la tabla a otro peligro ya puesto (más su radio). */
	constexpr double MIN_CLEARANCE = 1500.0;

	/** Desnivel máximo (cm) entre el suelo del sitio y el del camino: más, es un borde o una pared. */
	constexpr double MAX_STEP = 150.0;

	/** Tamaño de las algas de la tabla (huella nominal por este factor) y ancho máximo respecto al del camino. */
	constexpr float SEAWEED_SIZE = 0.6f;
	constexpr double SEAWEED_WIDTH_OF_PATH = 0.9;

	/** Parte del tramo donde se reparten (se deja libre el principio y el final, junto a los tramos vecinos). */
	constexpr double SPREAD_START = 0.1;
	constexpr double SPREAD_LENGTH = 0.8;

	/** Enemigos de los peligros por bioma que la dificultad del tramo filtra (lo demás va siempre). */
	bool IsEnemyHazard(const UClass* Class)
	{
		return Class && (Class->IsChildOf(ATN_CrabSpawnZone::StaticClass()) || Class->IsChildOf(ATN_SeagullSpawnZone::StaticClass())
			|| Class->IsChildOf(ATN_ProcWaterPredator::StaticClass()));
	}

	bool FarFromSpots(const TArray<FVector>& Spots, const FVector2D& P, double Radius)
	{
		for (const FVector& S : Spots)
		{
			if (FVector2D::Distance(FVector2D(S.X, S.Y), P) < S.Z + Radius + MIN_CLEARANCE) { return false; }
		}
		return true;
	}
}

void ATN_ProcMapGenerator::PlanCoopIntensity()
{
	IntensityPlan.Reset();
	if (NetConfig.Mode != ETNProcGameMode::Coop)
	{
		return;
	}
	const TNCoopIntensity::FTable* Table = TNCoopIntensity::GetDefaultTable();
	if (!Table)
	{
		return;
	}
	const int32 Round = FMath::Max(1, NetConfig.CoopRound);
	IntensityPlan = TNCoopIntensity::PlanRound(*Table, Round, static_cast<uint32>(NetConfig.Seed) * 2654435761u + 0x788u);

	// El registro, una vez: en el servidor (los clientes hacen el mismo plan con la misma réplica).
	const UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_Client)
	{
		return;
	}
	for (int32 t = 0; t < IntensityPlan.Num(); ++t)
	{
		const TNCoopIntensity::FTramoPlan& Tramo = IntensityPlan[t];
		int32 Enemies = 0;
		for (const TNCoopIntensity::FEnemyCount& E : Tramo.Enemies) { Enemies += E.Count; }
		UE_LOG(LogTortunabo, Log, TEXT("[Intensidad] Ronda %d · tramo %d: %s · módulo %d · intensidad %d · %d enemigos en la tabla."),
			Round, t + 1, TNCoopIntensity::DifficultyName(Tramo.Difficulty), Tramo.ModuleId, Tramo.Intensity, Enemies);
	}
	for (const FString& Unknown : TNCoopIntensity::UnknownEnemies(IntensityPlan))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Intensidad] El enemigo «%s» de la tabla no existe en el juego: se ignora."), *Unknown);
	}
}

int32 ATN_ProcMapGenerator::RouteStepOfSample(int32 BranchIndex, int32 PathIndex) const
{
	if (BranchIndex == INDEX_NONE)
	{
		return Layout.Main.IsValidIndex(PathIndex) ? Layout.Main[PathIndex].Step : INDEX_NONE;
	}
	if (!Layout.Branches.IsValidIndex(BranchIndex))
	{
		return INDEX_NONE;
	}
	const TNProcMap::FBranch& Branch = Layout.Branches[BranchIndex];
	if (Branch.Samples.IsValidIndex(PathIndex) && Branch.Samples[PathIndex].Step != INDEX_NONE)
	{
		return Branch.Samples[PathIndex].Step;
	}
	return Layout.Main.IsValidIndex(Branch.ForkSample) ? Layout.Main[Branch.ForkSample].Step : INDEX_NONE;
}

int32 ATN_ProcMapGenerator::IntensityTramoOfSample(int32 BranchIndex, int32 PathIndex) const
{
	const int32 Step = RouteStepOfSample(BranchIndex, PathIndex);
	if (IntensityPlan.Num() == 0 || Step == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	return TNCoopIntensity::TramoOfStep(Step, Layout.Route.Num(), IntensityPlan.Num());
}

bool ATN_ProcMapGenerator::IntensityAllowsHazard(int32 BranchIndex, int32 PathIndex, const UClass* Class, int32 MinDifficulty) const
{
	if (IntensityPlan.Num() == 0 || !TNProcIntensityDetail::IsEnemyHazard(Class))
	{
		return true;
	}
	const int32 Tramo = IntensityTramoOfSample(BranchIndex, PathIndex);
	return Tramo == INDEX_NONE || TNCoopIntensity::AllowsHazard(IntensityPlan[Tramo].Difficulty, true, MinDifficulty);
}

void ATN_ProcMapGenerator::CollectTramoSamples(int32 Tramo, TArray<int32>& OutSamples) const
{
	OutSamples.Reset();
	for (int32 i = 0; i < Layout.Main.Num(); ++i)
	{
		if ((Layout.Main[i].Flags & TNProcMap::PathFlags::Special) != 0) { continue; }
		if (IntensityTramoOfSample(INDEX_NONE, i) == Tramo) { OutSamples.Add(i); }
	}
}

bool ATN_ProcMapGenerator::PathSideSpot(int32 Sample, double Side, FVector2D& OutPoint, double& OutGround) const
{
	if (!Layout.Main.IsValidIndex(Sample))
	{
		return false;
	}
	const TNProcMap::FPathSample& S = Layout.Main[Sample];
	const FVector2D Normal(-S.Dir.Y, S.Dir.X);
	OutPoint = S.P + Normal * (FMath::Clamp(Side, -1.0, 1.0) * S.Width * 0.5);
	OutGround = TerrainHeightMap(OutPoint);
	// Sin agua debajo (lo de la tabla va a pie) y sin desnivel con el camino.
	return OutGround >= 0.0 && FMath::Abs(OutGround - S.Z) <= TNProcIntensityDetail::MAX_STEP;
}

void ATN_ProcMapGenerator::SpawnIntensityEnemies()
{
	using namespace TNProcIntensityDetail;
	UWorld* World = GetWorld();
	if (IntensityPlan.Num() == 0 || !World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	const double Yaw0 = GetActorRotation().Yaw;
	const double SeaweedRadius = TNBeach::FootprintRadius(ETNBeachElement::Seaweed) * SEAWEED_SIZE;
	TArray<int32> Samples;
	for (int32 t = 0; t < IntensityPlan.Num(); ++t)
	{
		int32 Wanted = 0;
		for (const TNCoopIntensity::FEnemyCount& E : IntensityPlan[t].Enemies)
		{
			if (TNCoopIntensity::ResolveEnemy(E.Name) == TNCoopIntensity::EKnownEnemy::Seaweed) { Wanted += E.Count; }
		}
		CollectTramoSamples(t, Samples);
		if (Wanted == 0 || Samples.Num() == 0)
		{
			continue;
		}
		int32 Placed = 0;
		for (int32 k = 0; k < Wanted; ++k)
		{
			const double Frac = SPREAD_START + SPREAD_LENGTH * (static_cast<double>(k) + 0.5) / static_cast<double>(Wanted);
			const int32 Sample = Samples[FMath::Clamp(FMath::FloorToInt32(Frac * Samples.Num()), 0, Samples.Num() - 1)];
			// En el camino, algo descentradas a uno y otro lado: se vadean o se rodean.
			const double Side = (k % 3 - 1) * 0.3;
			FVector2D Where;
			double Ground = 0.0;
			if (!PathSideSpot(Sample, Side, Where, Ground) || !FarFromSpots(HazardSpots, Where, SeaweedRadius))
			{
				continue;
			}
			const TNProcMap::FPathSample& S = Layout.Main[Sample];
			FTNBeachElementSpec Spec;
			Spec.Element = ETNBeachElement::Seaweed;
			Spec.Seed = NetConfig.Seed * 31 + 7880 + t * 97 + k;
			Spec.SizeScale = SEAWEED_SIZE;
			Spec.Extent = static_cast<float>(S.Width * SEAWEED_WIDTH_OF_PATH);
			const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(S.Dir.Y, S.Dir.X)) + Yaw0;
			if (ATN_BeachElement* Element = ATN_BeachElement::SpawnElement(World, FTransform(FRotator(0.0, Yaw, 0.0), MapToWorld2D(Where, Ground)), Spec))
			{
				SpawnedActors.Add(Element);
				HazardSpots.Add(FVector(Where.X, Where.Y, SeaweedRadius));
				++Placed;
			}
		}
		UE_LOG(LogTortunabo, Log, TEXT("[Intensidad] Tramo %d (%s): %d de %d algas de la tabla colocadas."),
			t + 1, TNCoopIntensity::DifficultyName(IntensityPlan[t].Difficulty), Placed, Wanted);
	}
}
