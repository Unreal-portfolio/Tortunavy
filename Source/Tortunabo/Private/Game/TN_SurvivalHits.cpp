#include "Game/TN_SurvivalHits.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/TN_SurvivalGameMode.h"
#include "Player/TortugaCharacter.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"

bool TNSurvivalHits::IsSurvival(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}
	if (World->GetAuthGameMode<ATN_SurvivalGameMode>())
	{
		return true;
	}
	// El banco de pruebas (LVL_ProcMap?ProcMode=Survival) va con el GameMode del mapa procedural: cuenta su mapa.
	for (TActorIterator<ATN_ProcMapGenerator> It(World); It; ++It)
	{
		if (It->GetNetConfig().Mode == ETNProcGameMode::Survival)
		{
			return true;
		}
	}
	return false;
}

bool TNSurvivalHits::KillInSurvival(AActor* Victim, AActor* Killer)
{
	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Victim);
	if (!Turtle || Turtle->IsDead() || !Turtle->HasAuthority() || !IsSurvival(Turtle))
	{
		return false;
	}
	Turtle->RequestKill(Killer);
	return true;
}
