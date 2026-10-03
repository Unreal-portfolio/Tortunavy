#include "Multiplayer/TN_LocalViews.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

void TNLocalViews::GetLocalControllers(const UWorld* World, TArray<APlayerController*>& Out)
{
	Out.Reset();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		return;
	}
	for (const ULocalPlayer* Player : GameInstance->GetLocalPlayers())
	{
		APlayerController* PC = Player ? Player->GetPlayerController(World) : nullptr;
		if (PC && PC->IsLocalController())
		{
			Out.Add(PC);
		}
	}
}

int32 TNLocalViews::NumLocalPlayers(const UWorld* World)
{
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetNumLocalPlayers() : 0;
}

bool TNLocalViews::ClosestCamera(const UWorld* World, const FVector& At, FVector& OutLocation, FRotator* OutRotation, APlayerController** OutController)
{
	// Copia: At puede ser la misma variable que OutLocation (se pide «la cámara más cercana a esto» y se escribe encima).
	const FVector Target = At;
	TArray<APlayerController*> Controllers;
	GetLocalControllers(World, Controllers);
	double Best = TNumericLimits<double>::Max();
	bool bFound = false;
	for (APlayerController* PC : Controllers)
	{
		const APlayerCameraManager* Camera = PC->PlayerCameraManager;
		if (!Camera)
		{
			continue;
		}
		const FVector Location = Camera->GetCameraLocation();
		const double Distance = FVector::DistSquared(Location, Target);
		if (!bFound || Distance < Best)
		{
			bFound = true;
			Best = Distance;
			OutLocation = Location;
			if (OutRotation) { *OutRotation = Camera->GetCameraRotation(); }
			if (OutController) { *OutController = PC; }
		}
	}
	return bFound;
}

double TNLocalViews::ClosestCameraDistance(const UWorld* World, const FVector& At)
{
	FVector Location;
	return ClosestCamera(World, At, Location) ? FVector::Dist(Location, At) : 1.0e9;
}

APawn* TNLocalViews::ClosestLocalPawn(const UWorld* World, const FVector& At, double* OutDistance)
{
	TArray<APlayerController*> Controllers;
	GetLocalControllers(World, Controllers);
	APawn* Best = nullptr;
	double BestDistance = 1.0e9;
	for (APlayerController* PC : Controllers)
	{
		APawn* Pawn = PC->GetPawn();
		if (!Pawn)
		{
			continue;
		}
		const double Distance = FVector::Dist(Pawn->GetActorLocation(), At);
		if (!Best || Distance < BestDistance)
		{
			Best = Pawn;
			BestDistance = Distance;
		}
	}
	if (OutDistance) { *OutDistance = BestDistance; }
	return Best;
}

bool TNLocalViews::IsLocalPawn(const APawn* Pawn)
{
	return Pawn && Pawn->IsLocallyControlled();
}
