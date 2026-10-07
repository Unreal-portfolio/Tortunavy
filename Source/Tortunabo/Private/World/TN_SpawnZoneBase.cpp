#include "World/TN_SpawnZoneBase.h"
#include "Player/TortugaCharacter.h"
#include "Core/TN_CoopPlayerState.h"
#include "Components/BoxComponent.h"
#include "TimerManager.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"

ATN_SpawnZoneBase::ATN_SpawnZoneBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // Solo lógica de servidor, sin replicación propia

	SpawnVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnVolume"));
	SetRootComponent(SpawnVolume);
	SpawnVolume->SetBoxExtent(FVector(500.f, 500.f, 300.f));
	SpawnVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ATN_SpawnZoneBase::BeginPlay()
{
	Super::BeginPlay();

	// NO usar HasAuthority(): devuelve true también en clientes para actores no
	// replicados (bReplicates=false — los chunks los crean localmente en cada
	// máquina vía ChildActorComponent, y los de nivel cargan con el mapa). Con
	// ese guard, cada cliente arrancaba su propio timer y spawneaba gaviotas/
	// cacas FANTASMA locales duplicadas. Patrón de TN_ItemSpawnZone.
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) { return; }

	// El timer llama al TrySpawn virtual → despacha a la subclase (SpawnActor concreto).
	GetWorld()->GetTimerManager().SetTimer(
		SpawnTimerHandle, this, &ATN_SpawnZoneBase::TrySpawn,
		SpawnInterval, /*bLoop=*/true, InitialDelay);
}

void ATN_SpawnZoneBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpawnTimerHandle);
	}

	// La zona muere con su chunk reciclado: destruir también lo que spawneó.
	// Vaciar solo el tracking dejaba gaviotas/cacas HUÉRFANAS ticando y
	// replicando el resto de la carrera (acumulativo con cada chunk).
	// En clientes el array siempre está vacío (BeginPlay no spawnea).
	for (const TWeakObjectPtr<AActor>& Instance : ActiveInstances)
	{
		if (Instance.IsValid())
		{
			Instance->Destroy();
		}
	}
	ActiveInstances.Empty();
	Super::EndPlay(EndPlayReason);
}

void ATN_SpawnZoneBase::GetLivingPlayersInside(TArray<ATortugaCharacter*>& OutPlayers) const
{
	if (!SpawnVolume) { return; }

	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GS) { return; }

	const FTransform ZoneTransform = SpawnVolume->GetComponentTransform();
	const FVector    HalfExtent    = SpawnVolume->GetUnscaledBoxExtent();

	// PlayerArray (≤8 jugadores) en vez de TActorIterator<ATortugaCharacter>:
	// el iterador barría TODOS los actores del mundo en cada disparo de timer.
	for (APlayerState* BasePS : GS->PlayerArray)
	{
		const ATN_CoopPlayerState* PS = Cast<ATN_CoopPlayerState>(BasePS);
		if (!PS || !PS->IsAliveAndPlaying()) { continue; }

		ATortugaCharacter* C = Cast<ATortugaCharacter>(PS->GetPawn());
		if (!C || !C->GetController()) { continue; }

		// Convertir posición del jugador al espacio local de la caja
		const FVector LocalPos = ZoneTransform.InverseTransformPosition(C->GetActorLocation());
		if (FMath::Abs(LocalPos.X) <= HalfExtent.X &&
			FMath::Abs(LocalPos.Y) <= HalfExtent.Y &&
			FMath::Abs(LocalPos.Z) <= HalfExtent.Z)
		{
			OutPlayers.Add(C);
		}
	}
}

FVector ATN_SpawnZoneBase::ClampXYToVolume(const FVector& WorldLoc) const
{
	if (!SpawnVolume) { return WorldLoc; }

	const FTransform ZoneTransform = SpawnVolume->GetComponentTransform();
	const FVector    HalfExtent    = SpawnVolume->GetUnscaledBoxExtent();

	FVector Local = ZoneTransform.InverseTransformPosition(WorldLoc);
	Local.X = FMath::Clamp(Local.X, -HalfExtent.X, HalfExtent.X);
	Local.Y = FMath::Clamp(Local.Y, -HalfExtent.Y, HalfExtent.Y);
	// No clampeamos Z local para no distorsionar la altura del mundo.
	FVector WorldClamped = ZoneTransform.TransformPosition(Local);
	// Preservar la Z original: una caja rotada trasladaría erróneamente la Z mundo
	// tras TransformPosition.
	WorldClamped.Z = WorldLoc.Z;
	return WorldClamped;
}

void ATN_SpawnZoneBase::RegisterActive(AActor* Instance)
{
	if (Instance)
	{
		ActiveInstances.Add(Instance);
	}
}

int32 ATN_SpawnZoneBase::GetActiveCount()
{
	ActiveInstances.RemoveAll([](const TWeakObjectPtr<AActor>& Ptr)
	{
		return !Ptr.IsValid();
	});
	return ActiveInstances.Num();
}
