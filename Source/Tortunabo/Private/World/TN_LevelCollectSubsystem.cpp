#include "World/TN_LevelCollectSubsystem.h"

#include "Core/TN_CoopPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

bool UTN_LevelCollectSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UTN_LevelCollectSubsystem* UTN_LevelCollectSubsystem::ForServer(const AActor* Item)
{
	UWorld* World = Item ? Item->GetWorld() : nullptr;
	if (!World || !Item->HasAuthority() || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	return World->GetSubsystem<UTN_LevelCollectSubsystem>();
}

void UTN_LevelCollectSubsystem::Register(const AActor* Item)
{
	if (UTN_LevelCollectSubsystem* Self = ForServer(Item))
	{
		Self->Items.FindOrAdd(FObjectKey(Item), false);
	}
}

void UTN_LevelCollectSubsystem::Forget(const AActor* Item)
{
	if (UTN_LevelCollectSubsystem* Self = ForServer(Item))
	{
		Self->Items.Remove(FObjectKey(Item));
	}
}

void UTN_LevelCollectSubsystem::NotifyCollected(const AActor* Item, const APawn* Collector)
{
	UTN_LevelCollectSubsystem* Self = ForServer(Item);
	bool* bTaken = Self ? Self->Items.Find(FObjectKey(Item)) : nullptr;
	if (!bTaken || *bTaken)
	{
		return;
	}
	*bTaken = true;
	if (ATN_CoopPlayerState* PS = Collector ? Collector->GetPlayerState<ATN_CoopPlayerState>() : nullptr)
	{
		PS->AddLevelItemCollected();
	}
}

int32 UTN_LevelCollectSubsystem::GetCollectedCount() const
{
	int32 Count = 0;
	for (const TPair<FObjectKey, bool>& Pair : Items)
	{
		Count += Pair.Value ? 1 : 0;
	}
	return Count;
}
