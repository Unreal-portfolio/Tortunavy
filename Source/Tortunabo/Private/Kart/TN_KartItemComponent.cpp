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
#include "Vehicles/TN_RallyPufferMine.h"

namespace TNKartItemDetail
{
	/** Delante y detrás de la boca del kart desde donde sale la concha (cm) y altura (cm). */
	constexpr float ShellSpawnAheadCm = 320.f;
	constexpr float ShellSpawnUpCm = 60.f;
	/** Un mismo kart no recibe otro empujón de la estrella en este tiempo (s). */
	constexpr double StarBumpRepeatSeconds = 1.0;
	/** Altura sobre el kart desde la que se busca el suelo del charco de detrás (cm): cubre una cuesta que sube detrás. */
	constexpr float AlgaProbeUpCm = 250.f;
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
	// #774: la munición de la torreta del Rally.
	case ETNKartItem::Mortero:
		FireMortar();
		break;
	case ETNKartItem::Erizos:
		StartErizos();
		break;
	case ETNKartItem::Medusa:
		if (!Hop())
		{
			return false;
		}
		break;
	case ETNKartItem::PezGlobo:
		DropPuffer();
		break;
	case ETNKartItem::Arpon:
		FireHarpoon();
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
	const FVector Forward = Kart->GetActorForwardVector().GetSafeNormal2D();
	const FVector Behind = Kart->GetActorLocation() - Forward * TNKart::AlgaBehindCm;
	// Al suelo de detrás con su inclinación (#770), buscando desde arriba por si detrás sube; quien lo suelta no lo pisa
	// al soltarlo (cae a menos de su radio).
	ATN_RallyAlgaPuddle::SpawnOnGround(World, Behind + FVector(0.f, 0.f, TNKartItemDetail::AlgaProbeUpCm), Forward, Kart);
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
		TickErizos();
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
	// Medusa (#774): bota para esquivar una teledirigida que le persigue o un charco delante.
	const bool bHopThreat = Item == ETNKartItem::Medusa && TNRallyHazards::HopThreatNear(*Kart);
	if (TNKart::ShouldBotUseItem(Item, BotHeldSeconds, Ahead, Behind, bHopThreat))
	{
		UseItem(false);
	}
}

// ── Objetos que reutilizan la munición de la torreta del Rally (#774) ─────────

namespace TNKartItemAmmo
{
	/** Desde dónde sale lo que dispara el kart: por delante del morro y a esta altura (cm). */
	constexpr float MuzzleAheadCm = 320.f;
	constexpr float MuzzleUpCm = 90.f;
	/** Altura sobre el blanco a la que apunta el arpón (cm): el centro de la carrocería. */
	constexpr float HarpoonAimUpCm = 60.f;
	/** Subida desde la que se busca el suelo de la mina de detrás (cm), por si detrás sube. */
	constexpr float PufferProbeUpCm = 250.f;
}

ATN_Buggy* UTN_KartItemComponent::FindKartAtPlace(int32 Place) const
{
	const ATN_RallyGameState* RallyState = GetWorld() ? GetWorld()->GetGameState<ATN_RallyGameState>() : nullptr;
	if (!RallyState || Place <= 0)
	{
		return nullptr;
	}
	for (const FTNRallyStanding& Entry : RallyState->Standings)
	{
		if (Entry.Place == Place && !Entry.bRetired && !Entry.bFinished)
		{
			return Cast<ATN_Buggy>(Entry.Vehicle);
		}
	}
	return nullptr;
}

ATN_Buggy* UTN_KartItemComponent::FindKartAhead() const
{
	int32 Place = 1;
	int32 Karts = 1;
	GetPlace(Place, Karts);
	ATN_Buggy* Ahead = FindKartAtPlace(Place - 1);
	return Ahead != GetKart() ? Ahead : nullptr;
}

void UTN_KartItemComponent::FireMortar()
{
	ATN_Buggy* Kart = GetKart();
	UWorld* World = GetWorld();
	const FVector Forward = Kart->GetActorForwardVector().GetSafeNormal2D();
	const FVector Muzzle = Kart->GetActorLocation() + FVector(0.f, 0.f, TNKartItemAmmo::MuzzleUpCm);
	// Cae delante del de delante (donde estará al caer); sin nadie delante, a 40 m del propio.
	FVector Target = Kart->GetActorLocation() + Forward * TNKart::MortarNoTargetCm;
	float Flight = TNKart::MortarFlightSeconds(TNKart::MortarNoTargetCm);
	if (const ATN_Buggy* Ahead = FindKartAhead())
	{
		Flight = TNKart::MortarFlightSeconds(static_cast<float>(FVector::Dist2D(Ahead->GetActorLocation(), Muzzle)));
		Target = Ahead->GetActorLocation() + Ahead->GetVelocity() * Flight
			+ Ahead->GetActorForwardVector().GetSafeNormal2D() * TNKart::MortarLeadCm;
	}
	const float GravityZ = World->GetGravityZ() * TNRallyTurret::SpecFor(ETNRallyAmmo::Mortero).GravityScale;
	ATN_RallyProjectile::Launch(World, ETNRallyAmmo::Mortero, Muzzle, TNKart::MortarLaunchVelocity(Muzzle, Target, GravityZ, Flight), Kart);
}

void UTN_KartItemComponent::StartErizos()
{
	// 3 s de púas (24) sin tener que mantener nada: TickErizos las dispara a la cadencia de la torreta.
	ErizosBurst = TNRallyTurret::HoldBurst(TNRallyTurret::FBurst(), GetWorld()->GetTimeSeconds(), TNKart::ErizosSeconds,
		TNKart::ErizosSpikes);
	TickErizos();
}

void UTN_KartItemComponent::TickErizos()
{
	ATN_Buggy* Kart = GetKart();
	UWorld* World = GetWorld();
	if (!Kart || !World || !TNRallyTurret::IsBurstActive(ErizosBurst))
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now > ErizosBurst.HoldUntil)
	{
		ErizosBurst = TNRallyTurret::FBurst();
		return;
	}
	if (!TNRallyTurret::BurstSpikeDue(ErizosBurst, Now))
	{
		return;
	}
	ErizosBurst = TNRallyTurret::AfterBurstSpike(ErizosBurst, Now);
	// Se apunta con el kart: hacia donde mira, un poco hacia arriba.
	const FVector Dir = Kart->GetActorRotation().RotateVector(FRotator(TNKart::ErizosPitchDeg, 0.f, 0.f).Vector());
	const FVector Muzzle = Kart->GetActorLocation() + Kart->GetActorForwardVector() * TNKartItemAmmo::MuzzleAheadCm
		+ FVector(0.f, 0.f, TNKartItemAmmo::MuzzleUpCm);
	ATN_RallyProjectile::Launch(World, ETNRallyAmmo::Erizos, Muzzle, Dir * TNRallyTurret::ErizosSpeedCms + Kart->GetVelocity(), Kart);
	Kart->ApplyVelocityImpulse(TNRallyTurret::RecoilVelocity(Dir, TNRallyTurret::ErizosRecoilCms));
}

bool UTN_KartItemComponent::Hop()
{
	ATN_Buggy* Kart = GetKart();
	if (!TNRallyTurret::CanHop(Kart->IsAirborne()))
	{
		return false;
	}
	// El mismo bote que la medusa de la torreta: impulso vertical en el servidor.
	Kart->ApplyVelocityImpulse(FVector::UpVector * TNRallyTurret::JellyfishUpCms);
	ATN_RallyBurstFX::Broadcast(Kart, ETNRallyBurstKind::BubblePop, Kart->GetActorLocation(), 220.f);
	return true;
}

void UTN_KartItemComponent::DropPuffer()
{
	ATN_Buggy* Kart = GetKart();
	const FVector Behind = Kart->GetActorLocation() - Kart->GetActorForwardVector().GetSafeNormal2D() * TNKart::PufferBehindCm;
	ATN_RallyPufferMine::SpawnOnGround(GetWorld(), Behind + FVector(0.f, 0.f, TNKartItemAmmo::PufferProbeUpCm), Kart);
}

void UTN_KartItemComponent::FireHarpoon()
{
	ATN_Buggy* Kart = GetKart();
	UWorld* World = GetWorld();
	const FVector Muzzle = Kart->GetActorLocation() + Kart->GetActorForwardVector() * TNKartItemAmmo::MuzzleAheadCm
		+ FVector(0.f, 0.f, TNKartItemAmmo::MuzzleUpCm);
	FVector Dir = Kart->GetActorForwardVector();
	if (const ATN_Buggy* Ahead = FindKartAhead();
		Ahead && FVector::Dist(Ahead->GetActorLocation(), Muzzle) <= TNKart::HarpoonRangeCm)
	{
		// Se clava en el de delante: donde estará al llegar, contando con que el arpón hereda la velocidad del propio kart.
		const FVector Aim = Ahead->GetActorLocation() + FVector(0.f, 0.f, TNKartItemAmmo::HarpoonAimUpCm);
		const float Time = static_cast<float>(FVector::Dist(Aim, Muzzle)) / TNRallyTurret::HarpoonSpeedCms;
		Dir = (Aim + (Ahead->GetVelocity() - Kart->GetVelocity()) * Time - Muzzle).GetSafeNormal();
	}
	ATN_RallyProjectile::Launch(World, ETNRallyAmmo::Arpon, Muzzle, Dir * TNRallyTurret::HarpoonSpeedCms + Kart->GetVelocity(), Kart);
}
