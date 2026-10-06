#include "TN_BeachCastlePrizes.h"
#include "TN_BeachSignKit.h"
#include "TN_BeachTrapKit.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "World/Beach/TN_BeachElement.h"

namespace TNBeachCastlePrizesDetail
{
	/** Suelo del patio del castillo enorme: la plataforma, un escalón de 80 cm (BuildSandCastleHuge). */
	constexpr double CourtFloor = 80.0;
	/** Franjas del patio entre el torreón (media anchura 548,8) y la muralla (cara de dentro a 1148): su centro. */
	constexpr double StripCenter = 848.0;
	/** Cuánto se corre la catapulta dentro de su franja para dejar sitio al cartel. */
	constexpr double SignShift = 87.0;
}

TNBeachCastlePrizes::FHugeCastlePlan TNBeachCastlePrizes::PlanHugeCastle(int32 CatapultSpecSeed)
{
	using namespace TNBeachCastlePrizesDetail;
	FHugeCastlePlan Plan;
	// La catapulta va girada 180° (su +X es el -X del castillo, hacia el mar con la puerta a quien llega): su +Y es el -Y del castillo,
	// así que el cartel (a SideOf·313 de su eje) cae hacia el lado contrario de SideOf.
	const double Side = TNBeachSignKit::SideOf(CatapultSpecSeed);
	Plan.CatapultAt = FVector(0.0, StripCenter + SignShift * Side, CourtFloor);
	Plan.CatapultYaw = 180.0;
	Plan.ChestAt = FVector(-StripCenter, 0.0, CourtFloor);
	Plan.ChestYaw = 0.0;
	return Plan;
}

int32 TNBeachCastlePrizes::CatapultSeedOf(int32 CastleSeed)
{
	return static_cast<int32>(TNBeachTrapKit::SeedOf(CastleSeed, 331u) & 0x7FFFFFFFu);
}

int32 TNBeachCastlePrizes::ChestSeedOf(int32 CastleSeed)
{
	return static_cast<int32>(TNBeachTrapKit::SeedOf(CastleSeed, 337u) & 0x7FFFFFFFu);
}

void TNBeachCastlePrizes::SpawnSummit(UWorld* World, const FTransform& CatapultXf, int32 CatapultSeed, const FTransform& ChestXf, int32 ChestSeed,
	TArray<ATN_BeachElement*>& OutSpawned)
{
	if (!World)
	{
		return;
	}
	FTNBeachElementSpec Launcher;
	Launcher.Element = ETNBeachElement::Catapult;
	Launcher.Seed = CatapultSeed;
	Launcher.SizeScale = CatapultSize;
	Launcher.Flags = TNBeach::FlagBoosted;
	if (ATN_BeachElement* Catapult = ATN_BeachElement::SpawnElement(World, CatapultXf, Launcher))
	{
		OutSpawned.Add(Catapult);
	}
	FTNBeachElementSpec ChestSpec;
	ChestSpec.Element = ETNBeachElement::TreasureChest;
	ChestSpec.Seed = ChestSeed;
	ChestSpec.SizeScale = 1.f;
	ChestSpec.Flags = TNBeach::FlagSummitPrize;
	if (ATN_BeachElement* Chest = ATN_BeachElement::SpawnElement(World, ChestXf, ChestSpec))
	{
		OutSpawned.Add(Chest);
	}
}

void TNBeachCastlePrizes::SpawnHugeCastle(UWorld* World, const FTransform& CastleXf, int32 CastleSeed, TArray<ATN_BeachElement*>& OutSpawned)
{
	if (!World)
	{
		return;
	}
	const int32 CatapultSeed = CatapultSeedOf(CastleSeed);
	const FHugeCastlePlan Plan = PlanHugeCastle(CatapultSeed);
	const FQuat Facing = CastleXf.GetRotation();
	// El suelo de verdad bajo el sitio (con el hundimiento de su ejemplar): una traza corta contra el castillo; sin ella, el calculado.
	const auto Place = [World, &CastleXf, &Facing](const FVector& Local, double YawDeg)
	{
		FVector At = CastleXf.TransformPosition(Local);
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(TN_BeachCastlePrizeFloor), false);
		if (World->LineTraceSingleByObjectType(Hit, At + FVector(0.0, 0.0, 250.0), At - FVector(0.0, 0.0, 250.0), FCollisionObjectQueryParams(ECC_WorldStatic), Query)
			&& FMath::Abs(Hit.ImpactPoint.Z - At.Z) < 60.0 && Hit.ImpactNormal.Z > 0.9)
		{
			At.Z = Hit.ImpactPoint.Z;
		}
		return FTransform(Facing * FRotator(0.0, YawDeg, 0.0).Quaternion(), At);
	};
	SpawnSummit(World, Place(Plan.CatapultAt, Plan.CatapultYaw), CatapultSeed, Place(Plan.ChestAt, Plan.ChestYaw), ChestSeedOf(CastleSeed), OutSpawned);
}
