#include "World/TN_ScorePickupWakeSubsystem.h"
#include "Multiplayer/TN_LocalViews.h"
#include "World/TN_ScorePickup.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

bool UTN_ScorePickupWakeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

bool UTN_ScorePickupWakeSubsystem::GetViewLocation(FVector& OutLocation) const
{
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
	if (!Camera)
	{
		return false;
	}
	OutLocation = Camera->GetCameraLocation();
	return true;
}

void UTN_ScorePickupWakeSubsystem::Evaluate(FEntry& Entry, bool bHasView, const FVector& ViewLocation) const
{
	ATN_ScorePickup* Pickup = Entry.Pickup.Get();
	if (!Pickup)
	{
		return;
	}
	// Con la pantalla partida (#311), la cámara local más cercana a esta concha.
	FVector View = ViewLocation;
	if (bHasView) { TNLocalViews::ClosestCamera(GetWorld(), Pickup->GetActorLocation(), View); }
	const bool bAwake = !bHasView || TNScorePickupWake::ShouldBeAwake(
		FVector::DistSquared(View, Pickup->GetActorLocation()), Pickup->GetWakeDistance(), Entry.bAwake);
	if (bAwake != Entry.bAwake)
	{
		Entry.bAwake = bAwake;
		Pickup->SetAwake(bAwake);
	}
}

void UTN_ScorePickupWakeSubsystem::Register(ATN_ScorePickup* Pickup)
{
	if (!Pickup)
	{
		return;
	}
	FEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Pickup = Pickup;
	// Nace dormida (sin Tick) y se decide ya, para que la de delante de la cámara gire desde el primer fotograma.
	Entry.bAwake = false;
	Pickup->SetAwake(false);
	FVector ViewLocation = FVector::ZeroVector;
	const bool bHasView = GetViewLocation(ViewLocation);
	Evaluate(Entry, bHasView, ViewLocation);
}

void UTN_ScorePickupWakeSubsystem::Unregister(ATN_ScorePickup* Pickup)
{
	Entries.RemoveAllSwap([Pickup](const FEntry& Entry) { return !Entry.Pickup.IsValid() || Entry.Pickup.Get() == Pickup; });
}

void UTN_ScorePickupWakeSubsystem::Tick(float DeltaTime)
{
	Clock -= DeltaTime;
	if (Clock > 0.f)
	{
		return;
	}
	Clock = TNScorePickupWake::CheckInterval;

	FVector ViewLocation = FVector::ZeroVector;
	const bool bHasView = GetViewLocation(ViewLocation);
	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		if (!Entries[Index].Pickup.IsValid())
		{
			Entries.RemoveAtSwap(Index);
			continue;
		}
		Evaluate(Entries[Index], bHasView, ViewLocation);
	}
}
