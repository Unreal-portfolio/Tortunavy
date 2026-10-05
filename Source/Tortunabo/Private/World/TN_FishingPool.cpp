#include "World/TN_FishingPool.h"
#include "../Game/TN_CoopItemArt.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/TN_CoopItems.h"

namespace TNFishingPoolDetail
{
	/** Color de las salpicaduras al pescar (el «polvo» del rebuscable): agua clara. */
	const FLinearColor Splash(0.55f, 0.82f, 0.95f);
	/** Alto (cm) de la huella del charco: el aviso sale a ras del suelo y algo por encima. */
	constexpr float PoolHeight = 80.f;
}

ATN_FishingPool::ATN_FishingPool()
{
	PromptText = NSLOCTEXT("Tortunabo", "FishingPoolPrompt", "Mantén para pescar");
	SearchSeconds = TNCoopItemTuning::FishSeconds;
	LootChance = 1.f;
	bRepeatable = true;
	RepeatCooldown = TNCoopItemTuning::FishCooldownSeconds;
	MaxLootLying = TNCoopItemTuning::FishMaxLootLying;
	// Más grave que la arena: chapoteo más que piedrecitas.
	RummagePitch = 0.6f;
	HintDistance = 2200.f;
	MarkerRadius = TNCoopItemTuning::PoolRadius + 20.f;

	PoolMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PoolMesh"));
	PoolMesh->SetupAttachment(SceneRoot);
	PoolMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PoolMesh->SetCanEverAffectNavigation(false);
	PoolMesh->SetGenerateOverlapEvents(false);
	PoolMesh->bReceivesDecals = false;
}

void ATN_FishingPool::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		// La huella es el agua con su orilla: el aviso sale al llegar al borde por cualquier lado.
		SetupSpot(TNCoopItemTuning::PoolRadius + 20.f, 0.f, TNFishingPoolDetail::PoolHeight, TNFishingPoolDetail::Splash);
	}
	if (UStaticMesh* PoolArt = TNCoopItemArt::GetPoolMesh())
	{
		PoolMesh->SetStaticMesh(PoolArt);
	}
}

ATN_FishingPool* ATN_FishingPool::ServerSpawn(UWorld* World, const FVector& Location, float YawDeg)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_FishingPool* Pool = World->SpawnActor<ATN_FishingPool>(ATN_FishingPool::StaticClass(), FTransform(FRotator(0.f, YawDeg, 0.f), Location), Params);
	UE_LOG(LogTortunabo, Log, TEXT("[Coop] Charco de pesca en %s: %s."), *Location.ToCompactString(), Pool ? TEXT("creado") : TEXT("no se ha podido crear"));
	return Pool;
}

float ATN_FishingPool::GetLuck() const
{
	// Pescar siempre da algo (no hace caso de tn.Search.Luck, como el cofre del lobby).
	return LootChance;
}

bool ATN_FishingPool::PickLoot(FTN_InventoryItem& OutItem, const APawn* Searcher) const
{
	return TNCoopItems::RollModeLoot(Searcher, GetLootTable(), [this](FName RowName, const FTN_InventoryItem& Row) { return GetLootWeight(RowName, Row); },
		OutItem);
}
