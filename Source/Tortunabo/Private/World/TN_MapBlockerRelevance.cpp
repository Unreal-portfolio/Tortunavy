// Actores replicados que bloquean: siempre relevantes (#828). Ver TN_MapBlockerRelevance.h.

#include "World/TN_MapBlockerRelevance.h"

#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "World/TN_MapFingerprint.h"

bool UTN_MapBlockerRelevanceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	// Solo donde se decide qué se replica: el servidor (escucha o dedicado). Un cliente o la partida sola no tienen nada que mandar.
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UTN_MapBlockerRelevanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UWorld* World = GetWorld();
	if (!World) { return; }
	SpawnedHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UTN_MapBlockerRelevanceSubsystem::OnActorSpawned));
	PostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &UTN_MapBlockerRelevanceSubsystem::OnPostActorTick);
}

void UTN_MapBlockerRelevanceSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->RemoveOnActorSpawnedHandler(SpawnedHandle);
	}
	FWorldDelegates::OnWorldPostActorTick.Remove(PostTickHandle);
	Pending.Reset();
	Super::Deinitialize();
}

void UTN_MapBlockerRelevanceSubsystem::OnActorSpawned(AActor* Actor)
{
	// La colisión se pone al construir o en BeginPlay (a veces después de aparecer): se mira al final del fotograma.
	const UWorld* World = GetWorld();
	if (Actor && World && World->GetNetMode() != NM_Client && World->GetNetMode() != NM_Standalone)
	{
		Pending.Add(Actor);
	}
}

void UTN_MapBlockerRelevanceSubsystem::OnPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld() || Pending.Num() == 0) { return; }
	// Antes del envío de este fotograma (TickFlush va después): sale ya como siempre relevante.
	TArray<TWeakObjectPtr<AActor>> Batch = MoveTemp(Pending);
	Pending.Reset();
	int32 Changed = 0;
	for (const TWeakObjectPtr<AActor>& Weak : Batch)
	{
		Changed += KeepRelevant(Weak.Get()) ? 1 : 0;
	}
	if (Changed > 0)
	{
		MadeRelevant += Changed;
		UE_LOG(LogTortunabo, Verbose, TEXT("[Red] %d actores replicados que bloquean, siempre relevantes (%d en total)."), Changed, MadeRelevant);
	}
}

bool UTN_MapBlockerRelevanceSubsystem::ShouldBeAlwaysRelevant(const AActor* Actor)
{
	if (!IsValid(Actor) || Actor->bAlwaysRelevant || !Actor->GetIsReplicated() || Actor->IsNetStartupActor())
	{
		return false;
	}
	// Lo de un jugador o de un dueño concreto, no: no es mapa (y quien lo ve lo decide su dueño).
	if (Actor->bOnlyRelevantToOwner || Actor->bNetUseOwnerRelevancy)
	{
		return false;
	}
	return TNMapFingerprint::IsMapActor(Actor) && TNMapFingerprint::ActorBlocksMovement(Actor);
}

bool UTN_MapBlockerRelevanceSubsystem::KeepRelevant(AActor* Actor)
{
	if (!ShouldBeAlwaysRelevant(Actor))
	{
		return false;
	}
	Actor->bAlwaysRelevant = true;
	return true;
}
