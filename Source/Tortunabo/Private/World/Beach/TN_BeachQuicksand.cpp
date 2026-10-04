#include "World/Beach/TN_BeachQuicksand.h"

#include "Engine/World.h"
#include "TN_BeachTrapKit.h"
#include "World/TN_Quicksand.h"

ATN_BeachQuicksand::ATN_BeachQuicksand()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ATN_BeachQuicksand::ApplySpec()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	if (!IsValid(Zone))
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		Zone = World->SpawnActor<ATN_Quicksand>(ATN_Quicksand::StaticClass(), GetActorTransform(), Params);
	}
	if (IsValid(Zone))
	{
		Zone->SetActorTransform(GetActorTransform());
		Zone->SetQuicksandRadius(static_cast<float>(0.85 * TNBeachTrapKit::FitRadius(Spec.Element, Spec.SizeScale)));
	}
}

void ATN_BeachQuicksand::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(Zone))
	{
		Zone->Destroy();
	}
	Zone = nullptr;
	Super::EndPlay(EndPlayReason);
}
