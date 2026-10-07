#include "World/TN_CrabSpawnZone.h"
#include "Core/TN_Log.h"
#include "World/TN_CrabActor.h"
#include "Player/TortugaCharacter.h"
#include "Core/TN_CoopPlayerState.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"

ATN_CrabSpawnZone::ATN_CrabSpawnZone()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // Solo lógica de servidor

	ProximityVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("ProximityVolume"));
	SetRootComponent(ProximityVolume);
	ProximityVolume->SetBoxExtent(FVector(600.f, 600.f, 200.f));
	ProximityVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ProximityVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	ProximityVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void ATN_CrabSpawnZone::BeginPlay()
{
	Super::BeginPlay();

	// NO usar HasAuthority(): devuelve true también en clientes para actores no
	// replicados creados localmente (ChildActorComponent del chunk). Con ese
	// guard, cada cliente bindeaba el overlap y spawneaba cangrejos FANTASMA
	// locales duplicados. Patrón de TN_ItemSpawnZone.
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) { return; }

	ProximityVolume->OnComponentBeginOverlap.AddDynamic(
		this, &ATN_CrabSpawnZone::OnProximityBeginOverlap);
}

void ATN_CrabSpawnZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// La zona muere con su chunk reciclado: destruir también sus cangrejos.
	// Sin esto quedaban huérfanos ticando y replicando el resto de la carrera.
	for (const TWeakObjectPtr<AActor>& Crab : SpawnedCrabs)
	{
		if (Crab.IsValid())
		{
			Crab->Destroy();
		}
	}
	SpawnedCrabs.Empty();
	Super::EndPlay(EndPlayReason);
}

void ATN_CrabSpawnZone::OnProximityBeginOverlap(UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority()) { return; }
	if (bOneShot && bAlreadySpawned) { return; }

	ATortugaCharacter* Char = Cast<ATortugaCharacter>(OtherActor);
	if (!Char) { return; }

	const ATN_CoopPlayerState* PS = Char->GetPlayerState<ATN_CoopPlayerState>();
	if (!PS || !PS->IsAliveAndPlaying()) { return; }

	SpawnCrab();
}

void ATN_CrabSpawnZone::SpawnCrab()
{
	if (!CrabClass)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[CrabSpawnZone] '%s': CrabClass no asignada."), *GetName());
		return;
	}

	// Extents en espacio local del volumen (sin escala del actor aplicada)
	const FTransform VolumeTransform = ProximityVolume->GetComponentTransform();
	const FVector    HalfExtent      = ProximityVolume->GetUnscaledBoxExtent();

	int32 SpawnedCount = 0;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	for (int32 i = 0; i < SpawnCountOnEnter; ++i)
	{
		// Posición aleatoria dentro del volumen (local), luego a world space.
		// Z=0 local → el cangrejo queda en el plano del volumen; el actor ajusta Z si tiene navmesh.
		const FVector LocalRandom(
			FMath::RandRange(-HalfExtent.X, HalfExtent.X),
			FMath::RandRange(-HalfExtent.Y, HalfExtent.Y),
			0.f
		);
		const FVector WorldSpawnLoc = VolumeTransform.TransformPosition(LocalRandom);

		// Yaw aleatorio para que los cangrejos no se queden todos mirando la misma dirección
		const FRotator SpawnRot(0.f, FMath::RandRange(0.f, 359.f), 0.f);

		ATN_CrabActor* Crab = GetWorld()->SpawnActor<ATN_CrabActor>(
			CrabClass, WorldSpawnLoc, SpawnRot, Params);

		if (Crab)
		{
			++SpawnedCount;
			SpawnedCrabs.Add(Crab);
		}
	}

	if (SpawnedCount > 0)
	{
		bAlreadySpawned = true;
		UE_LOG(LogTortunabo, Log, TEXT("[CrabSpawnZone] '%s': %d/%d cangrejos spawneados."),
			*GetName(), SpawnedCount, SpawnCountOnEnter);

		// Desactivar detección en one-shot para no volver a disparar
		if (bOneShot)
		{
			ProximityVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}
