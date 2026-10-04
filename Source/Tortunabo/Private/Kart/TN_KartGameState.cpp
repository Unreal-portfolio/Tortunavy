#include "Kart/TN_KartGameState.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kart/TN_KartGameMode.h"
#include "Kart/TN_KartTrack.h"
#include "Net/UnrealNetwork.h"
#include "Rally/TN_RallyLogic.h"
#include "TimerManager.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"

namespace TNKartState
{
	/** Cada cuánto vuelve a mirar un cliente si ya tiene el mapa (por si el generador llega después que el estado). */
	constexpr float RetrySeconds = 0.5f;
	/** Separación de las muestras de colisión a lo largo de la pista (cm) y espera entre comprobaciones (s). */
	constexpr double CollisionProbeStepCm = 25000.0;
	constexpr double CollisionProbeIntervalSeconds = 0.5;
}

void ATN_KartGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_KartGameState, MapGeneration);
	DOREPLIFETIME(ATN_KartGameState, Difficulty);
	DOREPLIFETIME(ATN_KartGameState, MapSeed);
}

void ATN_KartGameState::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		GetWorldTimerManager().SetTimer(RetryHandle, this, &ATN_KartGameState::TryBuildClientTrack, TNKartState::RetrySeconds, true);
		TryBuildClientTrack();
	}
}

void ATN_KartGameState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(RetryHandle);
	if (ATN_ProcMapGenerator* Generator = BoundGenerator.Get())
	{
		Generator->OnMapGeneratedNative.Remove(MapGeneratedHandle);
	}
	Super::EndPlay(EndPlayReason);
}

ATN_KartTrack* ATN_KartGameState::GetKartTrack() const
{
	return Cast<ATN_KartTrack>(GetTrack());
}

ATN_ProcMapGenerator* ATN_KartGameState::FindGenerator() const
{
	if (ATN_ProcMapGenerator* Bound = BoundGenerator.Get())
	{
		return Bound;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ATN_ProcMapGenerator> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

ATN_KartTrack* ATN_KartGameState::FindOrSpawnKartTrack()
{
	if (ATN_KartTrack* Existing = GetKartTrack())
	{
		return Existing;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ATN_KartTrack> It(World); It; ++It)
	{
		Track = *It;
		return *It;
	}
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	ATN_KartTrack* Spawned = World->SpawnActor<ATN_KartTrack>(ATN_KartTrack::StaticClass(), FTransform::Identity, Params);
	Track = Spawned;
	return Spawned;
}

ATN_RallyTrack* ATN_KartGameState::PrepareTrack(FName InVariant)
{
	if (!HasAuthority())
	{
		TryBuildClientTrack();
		return GetTrack();
	}
	ATN_KartGameMode* KartMode = GetWorld() ? GetWorld()->GetAuthGameMode<ATN_KartGameMode>() : nullptr;
	ATN_ProcMapGenerator* Generator = KartMode ? KartMode->GenerateMap() : nullptr;
	ATN_KartTrack* KartTrack = Generator ? FindOrSpawnKartTrack() : nullptr;
	if (!KartTrack || !KartTrack->BuildFromMap(*Generator))
	{
		UE_LOG(LogTNRally, Error, TEXT("[Karts] Sin GameMode de karts, sin generador o sin camino: no hay pista."));
		return nullptr;
	}
	Difficulty = KartMode->GetProcDifficulty();
	MapSeed = KartMode->GetMapSeed();
	MapGeneration = KartTrack->GetMapGeneration();
	bCollisionConfirmed = false;
	ForceNetUpdate();
	OnTrackReady.Broadcast();
	return KartTrack;
}

void ATN_KartGameState::OnRep_MapGeneration()
{
	TryBuildClientTrack();
}

void ATN_KartGameState::HandleMapGenerated(int32 Generation)
{
	TryBuildClientTrack();
}

void ATN_KartGameState::TryBuildClientTrack()
{
	if (HasAuthority())
	{
		return;
	}
	ATN_ProcMapGenerator* Generator = FindGenerator();
	if (Generator && BoundGenerator.Get() != Generator)
	{
		BoundGenerator = Generator;
		MapGeneratedHandle = Generator->OnMapGeneratedNative.AddUObject(this, &ATN_KartGameState::HandleMapGenerated);
	}
	const ATN_KartTrack* Current = GetKartTrack();
	const bool bWanted = MapGeneration > 0 && (!Current || Current->GetMapGeneration() != MapGeneration);
	if (!bWanted || !Generator || !Generator->IsMapReady() || Generator->GetBuiltGeneration() != MapGeneration)
	{
		return;
	}
	ATN_KartTrack* KartTrack = FindOrSpawnKartTrack();
	if (!KartTrack || !KartTrack->BuildFromMap(*Generator))
	{
		UE_LOG(LogTNRally, Error, TEXT("[Karts] Este cliente no ha podido hacer la pista de la generación %d."), MapGeneration);
		return;
	}
	bCollisionConfirmed = false;
	GetWorldTimerManager().ClearTimer(RetryHandle);
	UE_LOG(LogTNRally, Log, TEXT("[Karts] Pista del cliente lista (generación %d, %d puertas, %.2f km)."), MapGeneration,
		KartTrack->GetGateCount(), KartTrack->GetTrackLengthCm() / 100000.0);
	OnTrackReady.Broadcast();
}

bool ATN_KartGameState::IsLocalTrackPlayable() const
{
	const ATN_KartTrack* KartTrack = GetKartTrack();
	if (!KartTrack || !KartTrack->IsBuilt() || KartTrack->GetMapGeneration() != MapGeneration || MapGeneration <= 0)
	{
		return false;
	}
	if (bCollisionConfirmed)
	{
		return true;
	}
	const UWorld* World = GetWorld();
	const ATN_ProcMapGenerator* Generator = FindGenerator();
	if (!World || !Generator)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	if (Now < NextCollisionProbeTime)
	{
		return false;
	}
	NextCollisionProbeTime = Now + TNKartState::CollisionProbeIntervalSeconds;
	// La colisión del terreno se cocina en segundo plano: la parrilla y una muestra cada 250 m de la pista.
	for (int32 Slot = 0; Slot < TNRally::MaxGridSlots; ++Slot)
	{
		if (!Generator->MapCollisionUnder(KartTrack->GetGridSlotTransform(Slot, 0.0).GetLocation()))
		{
			return false;
		}
	}
	const double Length = KartTrack->GetTrackLengthCm();
	for (double Arc = 0.0; Arc < Length; Arc += TNKartState::CollisionProbeStepCm)
	{
		if (!Generator->MapCollisionUnder(KartTrack->GetLocationAtArc(Arc)))
		{
			return false;
		}
	}
	bCollisionConfirmed = true;
	return true;
}
