#include "World/TN_MapPlacementSpawner.h"

#include "Core/TN_Log.h"
#include "World/Beach/TN_BeachDecorField.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/ProcMap/TN_ProcSearchSpot.h"
#include "World/TN_BreakablePlatform.h"
#include "World/TN_FishingPool.h"
#include "World/TN_InteractableBase.h"
#include "World/TN_PressurePlate.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/Crc.h"

const FName ATN_MapPlacementSpawner::ManualTag(TEXT("TN_Manual"));

namespace TNMapPlacementSpawnerDetail
{
	/** Radio (cm) alrededor de una entrada en el que algo puesto a mano en el nivel la deja fuera. */
	double ClearRadius(const TNMapPlacements::FPlacement& P)
	{
		using TNMapPlacements::ESpawn;
		const double Base = P.Spawn == ESpawn::Decor || P.Spawn == ESpawn::Vegetation ? 150.0 : 300.0;
		return FMath::Max3(Base, 0.5 * P.LengthCm, 0.5 * P.ExtentCm);
	}

	/** Piezas de juego que, puestas a mano en el nivel, reservan su sitio. */
	bool IsGameplayPiece(const AActor* Actor)
	{
		return Actor->IsA<ATN_BeachElement>() || Actor->IsA<ATN_PressurePlate>() || Actor->IsA<ATN_BreakablePlatform>() || Actor->IsA<ATN_InteractableBase>()
			|| Actor->IsA<APlayerStart>();
	}

	/** Nombre de la primera clase nativa (la de C++) de un actor, sin la A. */
	FName NativeClassName(const AActor* Actor)
	{
		const UClass* Class = Actor->GetClass();
		while (Class && !Class->HasAnyClassFlags(CLASS_Native))
		{
			Class = Class->GetSuperClass();
		}
		return Class ? Class->GetFName() : NAME_None;
	}
}

ATN_MapPlacementSpawner::ATN_MapPlacementSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;
}

void ATN_MapPlacementSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Al destruir el cargador (o recargar la variante) se va lo colocado; al cerrar el mundo ya se va solo.
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		for (const TWeakObjectPtr<AActor>& Actor : SpawnedActors)
		{
			if (Actor.IsValid() && (Actor->HasAuthority() || !Actor->GetIsReplicated()))
			{
				Actor->Destroy();
			}
		}
		if (IsValid(DecorField))
		{
			DecorField->Destroy();
		}
	}
	SpawnedActors.Reset();
	DecorField = nullptr;
	Super::EndPlay(EndPlayReason);
}

void ATN_MapPlacementSpawner::SetGround(const TArray<UPrimitiveComponent*>& InGround)
{
	Ground.Reset();
	for (UPrimitiveComponent* Component : InGround)
	{
		if (Component) { Ground.Add(Component); }
	}
}

FVector ATN_MapPlacementSpawner::Grounded(const FVector& At)
{
	const FVector Start = At + FVector(0.0, 0.0, TraceUp);
	const FVector End = At - FVector(0.0, 0.0, TraceDown);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNMapPlacementGround), false);
	bool bHit = false;
	double BestZ = At.Z;
	for (const TWeakObjectPtr<UPrimitiveComponent>& Weak : Ground)
	{
		UPrimitiveComponent* Component = Weak.Get();
		if (!Component || !Component->IsCollisionEnabled())
		{
			continue;
		}
		const FBox Box = Component->Bounds.GetBox();
		if (At.X < Box.Min.X || At.X > Box.Max.X || At.Y < Box.Min.Y || At.Y > Box.Max.Y)
		{
			continue;
		}
		FHitResult Hit;
		// La primera superficie desde arriba: la más alta de los trozos que tocan el punto.
		if (Component->LineTraceComponent(Hit, Start, End, Params) && (!bHit || Hit.ImpactPoint.Z > BestZ))
		{
			BestZ = Hit.ImpactPoint.Z;
			bHit = true;
		}
	}
	++(bHit ? Stats.GroundHits : Stats.GroundMisses);
	return FVector(At.X, At.Y, BestZ);
}

void ATN_MapPlacementSpawner::CollectLevelActors(bool bServer)
{
	using namespace TNMapPlacementSpawnerDetail;
	LevelSpots.Reset();
	UWorld* World = GetWorld();
	if (!World) { return; }
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor == this || Actor->GetOwner() == this || !Actor->GetRootComponent())
		{
			continue;
		}
		// Lo replicado que no es del nivel (lo que ha llegado del servidor o lo creado en partida) no cuenta, en el servidor
		// ni en el cliente: con el mismo criterio, el decorado local (con colisión) sale igual en todas las máquinas.
		if (Actor->GetIsReplicated() && !Actor->IsNetStartupActor())
		{
			continue;
		}
		if (Actor->ActorHasTag(ManualTag) || IsGameplayPiece(Actor))
		{
			LevelSpots.Add(Actor->GetActorLocation());
		}
	}
}

bool ATN_MapPlacementSpawner::IsTakenByLevel(const TNMapPlacements::FPlacement& P) const
{
	const double Radius = TNMapPlacementSpawnerDetail::ClearRadius(P);
	for (const FVector& Spot : LevelSpots)
	{
		if (FVector::DistSquared2D(Spot, P.Location) <= Radius * Radius)
		{
			return true;
		}
	}
	return false;
}

void ATN_MapPlacementSpawner::Track(AActor* Actor)
{
	if (!Actor) { return; }
	SpawnedActors.Add(Actor);
	++Stats.ActorsByClass.FindOrAdd(TNMapPlacementSpawnerDetail::NativeClassName(Actor));
}

AActor* ATN_MapPlacementSpawner::SpawnClass(UClass* Class, const FVector& At, double YawDeg)
{
	UWorld* World = GetWorld();
	if (!World || !Class) { return nullptr; }
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;
	AActor* Actor = World->SpawnActor<AActor>(Class, FTransform(FRotator(0.0, YawDeg, 0.0), At), Params);
	Track(Actor);
	return Actor;
}

AActor* ATN_MapPlacementSpawner::SpawnBeachElement(ETNBeachElement Element, const FVector& At, double YawDeg,
	const FString& SeedKey, float SizeScale, double ExtentCm)
{
	FTNBeachElementSpec Spec;
	Spec.Element = Element;
	Spec.Seed = static_cast<int32>(FCrc::StrCrc32(*SeedKey));
	Spec.SizeScale = SizeScale;
	Spec.Extent = static_cast<float>(ExtentCm);
	ATN_BeachElement* Actor = ATN_BeachElement::SpawnElement(GetWorld(), FTransform(FRotator(0.0, YawDeg, 0.0), At), Spec);
	Track(Actor);
	return Actor;
}

void ATN_MapPlacementSpawner::Populate(const TNMapPlacements::FParseResult& Parsed, bool bServer, bool bLocal)
{
	using TNMapPlacements::ESpawn;
	if (bPopulated) { return; }
	bPopulated = true;
	CollectLevelActors(bServer);

	for (const TNMapPlacements::FPlacement& P : Parsed.Placements)
	{
		if (P.Spawn == ESpawn::Unsupported)
		{
			++Stats.Unsupported;
			UE_LOG(LogTortunabo, Log, TEXT("[MapPlacements] '%s' (%s %s): sin pieza en el juego todavía, no se coloca."),
				*P.Id, *P.Category, *P.Kind);
			continue;
		}
		if (IsTakenByLevel(P))
		{
			++Stats.SkippedByLevel;
			UE_LOG(LogTortunabo, Log, TEXT("[MapPlacements] '%s': hay algo puesto a mano en el nivel en su sitio, no se coloca."), *P.Id);
			continue;
		}
		if (SpawnOne(P, bServer, bLocal))
		{
			++Stats.PlacedBySpawn.FindOrAdd(FName(TNMapPlacements::SpawnName(P.Spawn)));
		}
	}
	if (bLocal)
	{
		BuildDecor();
		BuildVegetation();
	}

	UE_LOG(LogTortunabo, Log,
		TEXT("[MapPlacements] %s: %d actores, %d piezas de decorado, %d matas; cota del terreno en %d de %d; %d tapadas por el nivel, %d sin pieza, %d fallidas, %d suprimidas%s."),
		bServer ? TEXT("servidor") : TEXT("cliente"), SpawnedActors.Num(), Stats.DecorItems, Stats.VegetationInstances,
		Stats.GroundHits, Stats.GroundHits + Stats.GroundMisses,
		Stats.SkippedByLevel, Stats.Unsupported, Stats.Failed, Parsed.Suppressed,
		Parsed.bStale ? *FString::Printf(TEXT("; bloque desfasado: %d automáticas sin colocar"), Parsed.SkippedStale) : TEXT(""));
}

bool ATN_MapPlacementSpawner::SpawnOne(const TNMapPlacements::FPlacement& P, bool bServer, bool bLocal)
{
	using TNMapPlacements::ESpawn;
	switch (P.Spawn)
	{
	case ESpawn::Decor:
		if (bLocal) { QueueDecor(P); }
		return bLocal;
	case ESpawn::Vegetation:
		if (bLocal) { QueueVegetation(P); }
		return bLocal;
	default:
		break;
	}
	if (!bServer)
	{
		return false;
	}
	bool bOk = false;
	switch (P.Spawn)
	{
	case ESpawn::BeachElement:
		bOk = SpawnBeachElement(P.Element, Grounded(P.Location), P.YawDeg, P.Id, P.SizeScale, P.ExtentCm) != nullptr;
		break;
	case ESpawn::SearchSpot:
		if (ATN_ProcSearchSpot* Spot = Cast<ATN_ProcSearchSpot>(SpawnClass(ATN_ProcSearchSpot::StaticClass(), Grounded(P.Location), P.YawDeg)))
		{
			// Rebuscable suelto en la arena: huella de 1,5 m, sin decorado propio, polvo de arena.
			Spot->SetupSpot(150.f, 0.f, 100.f, FLinearColor(0.85f, 0.78f, 0.62f));
			bOk = true;
		}
		break;
	case ESpawn::FishingPool:
		// La huella y la malla las pone el charco al empezar (ATN_FishingPool::BeginPlay).
		bOk = SpawnClass(ATN_FishingPool::StaticClass(), Grounded(P.Location), P.YawDeg) != nullptr;
		break;
	case ESpawn::PlateBalance:
		bOk = SpawnPlateBalance(P);
		break;
	case ESpawn::BreakableChain:
		bOk = SpawnBreakableChain(P);
		break;
	case ESpawn::WobblyRun:
		bOk = SpawnElementRow(P, ETNBeachElement::WobblyPlatform, FMath::Clamp(FMath::RoundToInt(P.Param(TEXT("platforms"), 6.0)), 2, 10), 0.0);
		break;
	default:
		break;
	}
	if (!bOk)
	{
		++Stats.Failed;
		UE_LOG(LogTortunabo, Warning, TEXT("[MapPlacements] '%s' (%s %s): no se ha podido crear."), *P.Id, *P.Category, *P.Kind);
	}
	return bOk;
}
