#include "Kart/TN_KartItemComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Kart/TN_KartShell.h"
#include "Net/UnrealNetwork.h"
#include "Rally/TN_RallyGameState.h"
#include "Rally/TN_RallyLogic.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_RallyProjectile.h"

namespace TNKartItemDetail
{
	/** Delante y detrás de la boca del kart desde donde sale la concha (cm) y altura (cm). */
	constexpr float ShellSpawnAheadCm = 320.f;
	constexpr float ShellSpawnUpCm = 60.f;
	/** Un mismo kart no recibe otro empujón de la estrella en este tiempo (s). */
	constexpr double StarBumpRepeatSeconds = 1.0;
}

UTN_KartItemComponent::UTN_KartItemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

void UTN_KartItemComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTN_KartItemComponent, Item);
	DOREPLIFETIME(UTN_KartItemComponent, Charges);
	DOREPLIFETIME(UTN_KartItemComponent, RouletteEndServerTime);
	DOREPLIFETIME(UTN_KartItemComponent, BoostEndServerTime);
	DOREPLIFETIME(UTN_KartItemComponent, StarEndServerTime);
}

ATN_Buggy* UTN_KartItemComponent::GetKart() const
{
	return Cast<ATN_Buggy>(GetOwner());
}

double UTN_KartItemComponent::GetServerNow() const
{
	const ATN_Buggy* Kart = GetKart();
	return Kart ? Kart->GetServerNow() : 0.0;
}

float UTN_KartItemComponent::GetRouletteSecondsLeft() const
{
	return FMath::Max(0.f, RouletteEndServerTime - static_cast<float>(GetServerNow()));
}

bool UTN_KartItemComponent::IsBoosting() const
{
	return BoostEndServerTime > GetServerNow();
}

float UTN_KartItemComponent::GetStarSecondsLeft() const
{
	return FMath::Max(0.f, StarEndServerTime - static_cast<float>(GetServerNow()));
}

void UTN_KartItemComponent::GetPlace(int32& OutPlace, int32& OutKarts) const
{
	OutPlace = 1;
	OutKarts = 1;
	const ATN_Buggy* Kart = GetKart();
	const ATN_RallyGameState* RallyState = GetWorld() ? GetWorld()->GetGameState<ATN_RallyGameState>() : nullptr;
	if (!Kart || !RallyState)
	{
		return;
	}
	int32 Active = 0;
	for (const FTNRallyStanding& Entry : RallyState->Standings)
	{
		Active += Entry.bRetired ? 0 : 1;
		if (Entry.Vehicle == Kart && Entry.Place > 0)
		{
			OutPlace = Entry.Place;
		}
	}
	OutKarts = FMath::Max(1, Active);
}

bool UTN_KartItemComponent::TryGiveFromBox(int32 Place, int32 NumKarts)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Item != ETNKartItem::None)
	{
		return false;
	}
	const ETNKartItem Rolled = TNKart::PickItem(TNKart::ItemWeightsForPlace(Place, NumKarts), FMath::FRand());
	if (Rolled == ETNKartItem::None)
	{
		return false;
	}
	GiveItem(Rolled, false);
	UE_LOG(LogTNRally, Verbose, TEXT("[KartItems] %s (%d.º de %d) saca %s."), *GetNameSafe(GetOwner()), Place, NumKarts,
		*UEnum::GetValueAsString(Rolled));
	return true;
}

void UTN_KartItemComponent::GiveItem(ETNKartItem NewItem, bool bSkipRoulette)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	Item = NewItem;
	Charges = TNKart::ItemCharges(NewItem);
	RouletteEndServerTime = bSkipRoulette ? 0.f : static_cast<float>(GetServerNow()) + TNKart::RouletteSeconds;
	BotHeldSeconds = 0.f;
	GetOwner()->ForceNetUpdate();
}

bool UTN_KartItemComponent::UseItem(bool bBackward)
{
	ATN_Buggy* Kart = GetKart();
	UWorld* World = GetWorld();
	if (!Kart || !World || !Kart->HasAuthority() || !CanUseItem() || Kart->IsEngineLocked())
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	if (Now - LastUseTime < TNKart::UseCooldownSeconds)
	{
		return false;
	}
	LastUseTime = Now;
	const ETNKartItem Used = Item;
	switch (Used)
	{
	case ETNKartItem::Coco:
	case ETNKartItem::TripleCoco:
		BoostEndServerTime = static_cast<float>(GetServerNow()) + TNKart::BoostSeconds;
		Kart->ApplyVelocityImpulse(Kart->GetActorForwardVector() * TNKart::BoostImpulseCms);
		break;
	case ETNKartItem::Concha:
		FireShell(false, bBackward);
		break;
	case ETNKartItem::ConchaGuiada:
		FireShell(true, bBackward);
		break;
	case ETNKartItem::Alga:
		DropAlga();
		break;
	case ETNKartItem::Tinta:
		SpillInk();
		break;
	case ETNKartItem::Estrella:
		StarEndServerTime = static_cast<float>(GetServerNow()) + TNKart::StarSeconds;
		Kart->GrantShield();
		StarBumped.Reset();
		break;
	default:
		return false;
	}
	Charges = FMath::Max(0, Charges - 1);
	if (Charges == 0)
	{
		Item = ETNKartItem::None;
	}
	BotHeldSeconds = 0.f;
	Kart->ForceNetUpdate();
	UE_LOG(LogTNRally, Verbose, TEXT("[KartItems] %s usa %s%s."), *Kart->GetName(), *UEnum::GetValueAsString(Used),
		bBackward ? TEXT(" hacia atrás") : TEXT(""));
	return true;
}

void UTN_KartItemComponent::FireShell(bool bHoming, bool bBackward)
{
	ATN_Buggy* Kart = GetKart();
	UWorld* World = GetWorld();
	const FVector Forward = Kart->GetActorForwardVector().GetSafeNormal2D();
	const FVector Dir = bBackward ? -Forward : Forward;
	ATN_Buggy* Target = nullptr;
	if (bHoming && !bBackward)
	{
		int32 Place = 1;
		int32 Karts = 1;
		GetPlace(Place, Karts);
		const int32 TargetPlace = TNKart::HomingTargetPlace(Place);
		if (const ATN_RallyGameState* RallyState = World->GetGameState<ATN_RallyGameState>())
		{
			for (const FTNRallyStanding& Entry : RallyState->Standings)
			{
				if (Entry.Place == TargetPlace && !Entry.bRetired && !Entry.bFinished)
				{
					Target = Cast<ATN_Buggy>(Entry.Vehicle);
				}
			}
		}
	}
	const FVector Where = Kart->GetActorLocation() + Dir * TNKartItemDetail::ShellSpawnAheadCm
		+ FVector(0.f, 0.f, TNKartItemDetail::ShellSpawnUpCm);
	ATN_KartShell::LaunchShell(World, Kart, Where, Dir, Target, bHoming);
}

void UTN_KartItemComponent::DropAlga()
{
	ATN_Buggy* Kart = GetKart();
	UWorld* World = GetWorld();
	const FVector Behind = Kart->GetActorLocation() - Kart->GetActorForwardVector().GetSafeNormal2D() * TNKart::AlgaBehindCm;
	FVector Ground = Behind;
	FHitResult Down;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNKartAlga), false, Kart);
	if (World->LineTraceSingleByChannel(Down, Behind + FVector(0.f, 0.f, 200.f), Behind - FVector(0.f, 0.f, 1500.f), ECC_WorldStatic, Params))
	{
		Ground = Down.ImpactPoint;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World->SpawnActor<ATN_RallyAlgaPuddle>(ATN_RallyAlgaPuddle::StaticClass(), FTransform(Ground), Spawn);
}

void UTN_KartItemComponent::SpillInk()
{
	ATN_Buggy* Kart = GetKart();
	const ATN_RallyGameState* RallyState = GetWorld()->GetGameState<ATN_RallyGameState>();
	if (!RallyState)
	{
		return;
	}
	int32 Place = 1;
	int32 Karts = 1;
	GetPlace(Place, Karts);
	for (const FTNRallyStanding& Entry : RallyState->Standings)
	{
		ATN_Buggy* Other = Cast<ATN_Buggy>(Entry.Vehicle);
		if (Other && Other != Kart && Entry.Place > 0 && Entry.Place < Place && !Entry.bFinished && !Other->IsRespawnProtected())
		{
			Other->ApplyInk();
			ATN_RallyBurstFX::Broadcast(Other, ETNRallyBurstKind::Ink, Other->GetActorLocation() + FVector(0.f, 0.f, 120.f), 220.f);
		}
	}
}

bool UTN_KartItemComponent::SpinOut(ATN_Buggy& Victim, const FVector& HitDir)
{
	// La misma que la de las conchas de la torreta (respeta la estrella, el escudo y el fantasma).
	return ATN_KartShell::SpinOut(Victim, HitDir);
}

void UTN_KartItemComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ATN_Buggy* Kart = GetKart();
	if (!Kart)
	{
		return;
	}
	if (Kart->HasAuthority() || Kart->IsLocallyControlled())
	{
		ApplyPush();
	}
	if (Kart->HasAuthority())
	{
		TickStar(DeltaTime);
		TickBot(DeltaTime);
	}
}

void UTN_KartItemComponent::ApplyPush()
{
	ATN_Buggy* Kart = GetKart();
	USkeletalMeshComponent* Chassis = Kart->GetMesh();
	const bool bBoost = IsBoosting();
	const bool bStar = GetStarSecondsLeft() > 0.f;
	if ((!bBoost && !bStar) || !Chassis || !Chassis->IsSimulatingPhysics() || Kart->IsEngineLocked())
	{
		return;
	}
	const FVector Forward = Kart->GetActorForwardVector();
	const float Speed = static_cast<float>(FVector::DotProduct(Kart->GetVelocity(), Forward));
	const float Top = bBoost ? TNKart::BoostTopSpeedCms : TNKart::StarTopSpeedCms;
	if (Speed >= Top)
	{
		return;
	}
	const float Accel = (bBoost ? TNKart::BoostAccelCms2 : 0.f) + (bStar ? TNKart::StarAccelCms2 : 0.f);
	Chassis->AddForce(Forward * Accel, NAME_None, true);
}

void UTN_KartItemComponent::TickStar(float DeltaTime)
{
	ATN_Buggy* Kart = GetKart();
	const bool bStar = GetStarSecondsLeft() > 0.f;
	if (!bStar)
	{
		if (bStarWasActive)
		{
			// Se acaba la estrella: fuera el escudo que la sostenía.
			bStarWasActive = false;
			Kart->TryConsumeShield();
		}
		return;
	}
	bStarWasActive = true;
	if (!Kart->IsShielded())
	{
		Kart->GrantShield();
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const FVector Center = Kart->GetActorLocation();
	for (TActorIterator<ATN_Buggy> It(GetWorld()); It; ++It)
	{
		ATN_Buggy* Other = *It;
		if (Other == Kart || FVector::DistSquared(Other->GetActorLocation(), Center) > FMath::Square(TNKart::StarBumpRadiusCm))
		{
			continue;
		}
		const double* Last = StarBumped.Find(Other);
		if (Last && Now - *Last < TNKartItemDetail::StarBumpRepeatSeconds)
		{
			continue;
		}
		StarBumped.Add(Other, Now);
		SpinOut(*Other, Other->GetActorLocation() - Center);
	}
}

void UTN_KartItemComponent::TickBot(float DeltaTime)
{
	ATN_Buggy* Kart = GetKart();
	const AController* Driver = Kart->GetSeatController(ETNRallySeat::Driver);
	const bool bBotDriven = Driver && !Driver->IsPlayerController() && !Kart->GetSeatController(ETNRallySeat::Gunner);
	if (!bBotDriven || !CanUseItem())
	{
		return;
	}
	BotHeldSeconds += DeltaTime;
	const ATN_RallyGameState* RallyState = GetWorld()->GetGameState<ATN_RallyGameState>();
	if (!RallyState || (RallyState->Phase != ETNRallyPhase::Racing && RallyState->Phase != ETNRallyPhase::Finishing))
	{
		return;
	}
	int32 Place = 1;
	int32 Karts = 1;
	GetPlace(Place, Karts);
	float Ahead = -1.f;
	float Behind = -1.f;
	for (const FTNRallyStanding& Entry : RallyState->Standings)
	{
		if (!Entry.Vehicle || Entry.Vehicle == Kart)
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist(Entry.Vehicle->GetActorLocation(), Kart->GetActorLocation()));
		if (Entry.Place == Place - 1)
		{
			Ahead = Distance;
		}
		else if (Entry.Place == Place + 1)
		{
			Behind = Distance;
		}
	}
	if (TNKart::ShouldBotUseItem(Item, BotHeldSeconds, Ahead, Behind))
	{
		UseItem(false);
	}
}
