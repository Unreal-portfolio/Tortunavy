#include "World/TN_SeagullSpawnZone.h"
#include "Core/TN_Log.h"
#include "World/TN_EnemySeagull.h"
#include "Player/TortugaCharacter.h"
#include "Engine/World.h"

ATN_SeagullSpawnZone::ATN_SeagullSpawnZone()
{
	// Defaults específicos de la gaviota (la base define el resto).
	SpawnInterval = 15.f;
	InitialDelay  = 3.f;
	MaxConcurrent = 3;
}

void ATN_SeagullSpawnZone::TrySpawn()
{
	if (!SeagullClass) { return; }
	if (GetActiveCount() >= MaxConcurrent) { return; }

	TArray<ATortugaCharacter*> Candidates;
	GetLivingPlayersInside(Candidates);
	if (Candidates.Num() == 0) { return; }

	ATortugaCharacter* Target = Candidates[FMath::RandRange(0, Candidates.Num() - 1)];

	// Si el jugador está en el borde, la gaviota quedaría (en XY) fuera del volumen.
	// Clampeamos al AABB para que el spawn siempre caiga dentro de la zona dueña.
	const FVector SpawnLoc = ClampXYToVolume(Target->GetActorLocation()) + FVector(0.f, 0.f, 400.f);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (ATN_EnemySeagull* Seagull = GetWorld()->SpawnActor<ATN_EnemySeagull>(
		SeagullClass, SpawnLoc, FRotator::ZeroRotator, Params))
	{
		Seagull->InitializeWithTarget(Target);
		RegisterActive(Seagull);

		UE_LOG(LogTortunabo, Verbose, TEXT("[SeagullSpawnZone] '%s': gaviota spawneada sobre '%s'."),
			*GetName(), *GetNameSafe(Target));
	}
}

void ATN_SeagullSpawnZone::SetSeagullClassIfMissing(TSubclassOf<ATN_EnemySeagull> InClass)
{
	if (!SeagullClass)
	{
		SeagullClass = InClass;
	}
}
