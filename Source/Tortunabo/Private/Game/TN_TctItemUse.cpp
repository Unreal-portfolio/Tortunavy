// Objetos de Todos contra Todos: lo que hace cada uno al usarlo (servidor). El catálogo está en TN_TctItems.cpp.

#include "Game/TN_TctItems.h"
#include "Game/TN_TctItemComponent.h"
#include "Core/TN_Log.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/Beach/TN_RaceItems.h"
#include "World/TN_InkProjectile.h"
#include "World/TN_TctAlgaPuddle.h"
#include "World/TN_TctProjectile.h"
#include "World/TN_TctJellyPad.h"
#include "World/TN_TctThiefGull.h"

namespace TNTctItemUseDetail
{
	/** Un disparo en línea: de dónde sale, hasta dónde llega y a quién da. */
	struct FShot
	{
		FVector Start = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		FVector End = FVector::ZeroVector;
		ATortugaCharacter* Victim = nullptr;
		/** Ha dado en el escenario (en End). */
		bool bHitWorld = false;
	};

	/** Hacia dónde mira la tortuga: la cámara, con la inclinación recortada (no se dispara a los pies ni al cielo). */
	FVector CameraDirection(const ATortugaCharacter* Turtle)
	{
		FRotator Aim = Turtle->GetTurtleAimRotation();
		Aim.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Aim.Pitch), -15.f, 20.f);
		Aim.Roll = 0.f;
		return Aim.Vector();
	}

	FVector MuzzleOf(const ATortugaCharacter* Turtle, const FVector& Direction)
	{
		return Turtle->GetActorLocation() + FVector(0.0, 0.0, 35.0) + FVector(Direction.X, Direction.Y, 0.0).GetSafeNormal() * 45.0;
	}

	/**
	 * Hacia dónde va el disparo (#707): de la boca al punto que se ve en el centro de la pantalla (la mira), no en paralelo a la
	 * cámara, que va por detrás y por encima de la tortuga. Sin mira (VR), la dirección de la cámara.
	 */
	FVector AimDirection(const ATortugaCharacter* Turtle)
	{
		const FVector Camera = CameraDirection(Turtle);
		FVector Target;
		if (!Turtle->GetCrosshairPoint(Target))
		{
			return Camera;
		}
		return TNTctItemRules::AimToward(MuzzleOf(Turtle, Camera), Target, Camera);
	}

	/** Disparo en línea de Range con un grosor Radius: la primera tortuga que se puede golpear antes del escenario. */
	FShot TraceShot(ATortugaCharacter* Shooter, float Range, float Radius)
	{
		FShot Shot;
		UWorld* World = Shooter->GetWorld();
		Shot.Direction = AimDirection(Shooter);
		Shot.Start = MuzzleOf(Shooter, Shot.Direction);
		Shot.End = Shot.Start + Shot.Direction * Range;
		if (!World)
		{
			return Shot;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TctItemShot), false, Shooter);
		FHitResult WorldHit;
		if (World->LineTraceSingleByObjectType(WorldHit, Shot.Start, Shot.End, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			Shot.End = WorldHit.ImpactPoint;
			Shot.bHitWorld = true;
		}
		TArray<FHitResult> Hits;
		World->SweepMultiByObjectType(Hits, Shot.Start, Shot.End, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
			FCollisionShape::MakeSphere(Radius), Params);
		float Best = TNumericLimits<float>::Max();
		for (const FHitResult& Hit : Hits)
		{
			ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Hit.GetActor());
			if (Turtle && Turtle != Shooter && !Turtle->IsDead() && Hit.Distance < Best)
			{
				Best = Hit.Distance;
				Shot.Victim = Turtle;
			}
		}
		if (Shot.Victim)
		{
			Shot.End = Shot.Victim->GetActorLocation();
			Shot.bHitWorld = false;
		}
		return Shot;
	}

	/** Sin escenario entre From y To. */
	bool HasLineOfSight(const UWorld* World, const FVector& From, const FVector& To, const AActor* Ignore)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TctItemSight), false, Ignore);
		FHitResult Hit;
		return World && !World->LineTraceSingleByObjectType(Hit, From, To, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
	}

	void Trail(ATortugaCharacter* Shooter, ETNTctItem Kind, const FVector& From, const FVector& To)
	{
		if (UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOrAddOn(Shooter))
		{
			Effects->MulticastShot(static_cast<uint8>(Kind), From, To);
		}
	}

	bool UseKnockout(ATortugaCharacter* Turtle)
	{
		const FShot Shot = TraceShot(Turtle, TNTctItemTuning::KnockoutRange, TNTctItemTuning::KnockoutRadius);
		Trail(Turtle, ETNTctItem::KnockoutPistol, Shot.Start, Shot.End);
		TNTctItems::PlayCue(Turtle, ETNRaceSound::Zap, 0.8f);
		if (Shot.Victim && TNTctItems::CanAffect(Shot.Victim, false))
		{
			TNBeach::KnockDownTurtle(Shot.Victim, TNTctItemTuning::KnockoutSeconds, TNTctItemRules::KnockoutImpulse(Shot.Direction));
			UE_LOG(LogTortunabo, Log, TEXT("[TcT] %s noquea a %s."), *GetNameSafe(Turtle), *GetNameSafe(Shot.Victim));
		}
		// Fallar también gasta la carga.
		return true;
	}

	bool UseBlunderbuss(ATortugaCharacter* Turtle)
	{
		const FVector Direction = AimDirection(Turtle);
		const FVector Origin = MuzzleOf(Turtle, Direction);
		TArray<ATortugaCharacter*> Turtles;
		TNTctItems::GatherTurtles(Turtle, Turtle, Turtles);
		int32 Pushed = 0;
		for (ATortugaCharacter* Other : Turtles)
		{
			FVector Push;
			if (TNTctItems::CanAffect(Other, true) && TNTctItemRules::BlunderbussPush(Origin, Direction, Other->GetActorLocation(), Push)
				&& HasLineOfSight(Turtle->GetWorld(), Origin, Other->GetActorLocation(), Turtle))
			{
				UTN_TurtleMovementComponent::LaunchFromServer(Other, Push);
				++Pushed;
			}
		}
		UTN_TurtleMovementComponent::LaunchFromServer(Turtle, TNTctItemRules::BlunderbussRecoil(Direction));
		Trail(Turtle, ETNTctItem::AirBlunderbuss, Origin, Origin + FVector(Direction.X, Direction.Y, 0.0).GetSafeNormal() * TNTctItemTuning::BlunderbussRange);
		TNTctItems::PlayCue(Turtle, ETNRaceSound::Flap, 1.5f);
		UE_LOG(LogTortunabo, Log, TEXT("[TcT] %s dispara el trabuco de aire: %d empujadas."), *GetNameSafe(Turtle), Pushed);
		return true;
	}

	bool UseGrapple(ATortugaCharacter* Turtle)
	{
		const FShot Shot = TraceShot(Turtle, TNTctItemTuning::GrappleRange, TNTctItemTuning::GrappleRadius);
		if (Shot.Victim)
		{
			if (!TNTctItems::CanAffect(Shot.Victim, true))
			{
				// En el caparazón (o en brazos de otra) no se la puede arrastrar: el garfio rebota y no se gasta.
				return false;
			}
			UTN_TurtleMovementComponent::LaunchFromServer(Shot.Victim, TNTctItemRules::GrapplePull(Shot.Victim->GetActorLocation(), Turtle->GetActorLocation()));
			Trail(Turtle, ETNTctItem::Grapple, Shot.Start, Shot.End);
			TNTctItems::PlayCue(Turtle, ETNRaceSound::Catch, 0.9f);
			UE_LOG(LogTortunabo, Log, TEXT("[TcT] %s atrae con el garfio a %s."), *GetNameSafe(Turtle), *GetNameSafe(Shot.Victim));
			return true;
		}
		if (Shot.bHitWorld && FVector::Dist2D(Turtle->GetActorLocation(), Shot.End) > TNTctItemTuning::GrappleSelfMinDistance)
		{
			// Al escenario: te lleva a ti.
			UTN_TurtleMovementComponent::LaunchFromServer(Turtle, TNTctItemRules::GrapplePull(Turtle->GetActorLocation(), Shot.End));
			Trail(Turtle, ETNTctItem::Grapple, Shot.Start, Shot.End);
			TNTctItems::PlayCue(Turtle, ETNRaceSound::Catch, 1.1f);
			return true;
		}
		return false;
	}

	bool UseShovel(ATortugaCharacter* Turtle)
	{
		const FVector Direction = AimDirection(Turtle);
		const FVector Origin = Turtle->GetActorLocation();
		TArray<ATortugaCharacter*> Turtles;
		TNTctItems::GatherTurtles(Turtle, Turtle, Turtles);
		int32 Hit = 0;
		for (ATortugaCharacter* Other : Turtles)
		{
			FVector Push;
			if (TNTctItems::CanAffect(Other, true) && TNTctItemRules::ShovelHit(Origin, Direction, Other->GetActorLocation(), Push))
			{
				UTN_TurtleMovementComponent::LaunchFromServer(Other, Push);
				TNTctItems::PlayCue(Other, ETNRaceSound::Bonk, 1.f);
				++Hit;
			}
		}
		if (Hit == 0)
		{
			TNTctItems::PlayCue(Turtle, ETNRaceSound::Throw, 0.8f);
		}
		return true;
	}

	bool UseInkPistol(ATortugaCharacter* Turtle)
	{
		// La fila de tinta de DT_Items: su proyectil y su mancha de siempre.
		const UDataTable* Catalog = TNRaceItems::LoadCatalog();
		const FTN_InventoryItem* Ink = nullptr;
		if (Catalog)
		{
			Catalog->ForeachRow<FTN_InventoryItem>(TEXT("TNTctItems::UseInkPistol"), [&Ink](const FName&, const FTN_InventoryItem& Row)
			{
				if (!Ink && Row.UseType == ETN_ItemUseType::InkThrower && Row.InkData.ProjectileClass)
				{
					Ink = &Row;
				}
			});
		}
		if (!Ink)
		{
			return false;
		}
		// Hacia la mira (#707), con el arco justo para llegar al punto del centro de la pantalla.
		const FVector Fallback = Turtle->GetThrowDirection(Turtle->GetTurtleAimRotation());
		const FVector Muzzle = MuzzleOf(Turtle, Fallback);
		const FVector Direction = Turtle->GetThrowDirectionToCrosshair(Muzzle, Turtle->GetTurtleAimRotation(), TNTctItemTuning::InkPistolSpeed);
		ATN_InkProjectile::Spawn(Turtle, Ink->InkData.ProjectileClass, Muzzle, Direction, TNTctItemTuning::InkPistolSpeed);
		TNTctItems::PlayCue(Turtle, ETNRaceSound::Splat, 1.3f);
		return true;
	}

	bool UseProjectile(ATortugaCharacter* Turtle, ETNTctItem Kind)
	{
		float Pitch = TNTctItemTuning::BallPitchDeg;
		if (Kind == ETNTctItem::Anchor) { Pitch = TNTctItemTuning::AnchorPitchDeg; }
		if (Kind == ETNTctItem::JellyDart) { Pitch = TNTctItemTuning::DartPitchDeg; }
		if (Kind == ETNTctItem::Cocobomba) { Pitch = TNTctItemTuning::CocoPitchDeg; }
		if (Kind == ETNTctItem::Alga) { Pitch = TNTctItemTuning::AlgaPitchDeg; }
		const bool bLaunched = ATN_TctProjectile::ServerLaunch(Turtle, static_cast<uint8>(Kind), TNRaceItems::ThrowDirection(Turtle, Pitch));
		if (bLaunched)
		{
			TNTctItems::PlayCue(Turtle, ETNRaceSound::Throw, Kind == ETNTctItem::Anchor ? 0.6f : 1.1f);
			if (Kind == ETNTctItem::Cocobomba)
			{
				// La mecha encendida.
				TNTctItems::PlayCue(Turtle, ETNRaceSound::Beep, 1.2f);
			}
		}
		return bLaunched;
	}

	bool UseAlga(ATortugaCharacter* Turtle)
	{
		// Mirando al suelo, el charco se suelta a los pies; si no, se lanza corto.
		const float AimPitch = FRotator::NormalizeAxis(Turtle->GetTurtleAimRotation().Pitch);
		if (AimPitch >= TNTctItemTuning::AlgaDropPitchDeg)
		{
			return UseProjectile(Turtle, ETNTctItem::Alga);
		}
		if (!ATN_TctAlgaPuddle::ServerSpawn(Turtle->GetWorld(), Turtle->GetActorLocation(), Turtle))
		{
			return false;
		}
		TNTctItems::PlayCue(Turtle, ETNRaceSound::Splat, 0.8f);
		return true;
	}
}

void TNTctItems::ServerUse(ATortugaCharacter* Turtle, const FTN_InventoryItem& Item)
{
	using namespace TNTctItemUseDetail;
	if (!Turtle || !Turtle->HasAuthority())
	{
		return;
	}
	const ETNTctItem Kind = KindOf(Item);
	UTN_InventoryComponent* Inventory = Turtle->GetInventoryComponent();
	if (Kind == ETNTctItem::None || !Inventory)
	{
		return;
	}
	if (!TNRaceItems::CanUseNow(Turtle))
	{
		PlayCue(Turtle, ETNRaceSound::Nope);
		return;
	}

	bool bUsed = false;
	switch (Kind)
	{
	case ETNTctItem::KnockoutPistol: bUsed = UseKnockout(Turtle); break;
	case ETNTctItem::AirBlunderbuss: bUsed = UseBlunderbuss(Turtle); break;
	case ETNTctItem::Grapple:        bUsed = UseGrapple(Turtle); break;
	case ETNTctItem::Shovel:         bUsed = UseShovel(Turtle); break;
	case ETNTctItem::InkPistol:      bUsed = UseInkPistol(Turtle); break;
	case ETNTctItem::BeachBall:
	case ETNTctItem::Anchor:
	case ETNTctItem::JellyDart:
	case ETNTctItem::Cocobomba:      bUsed = UseProjectile(Turtle, Kind); break;
	case ETNTctItem::Alga:           bUsed = UseAlga(Turtle); break;
	case ETNTctItem::GaviotaLadrona: bUsed = ATN_TctThiefGull::ServerLaunch(Turtle); break;
	case ETNTctItem::MedusaTrampolin: bUsed = ATN_TctJellyPad::ServerPlant(Turtle) != nullptr; break;
	case ETNTctItem::Flotador:
	{
		// Si llega a la mano (no debería: se cuelga del caparazón al cogerlo), usarlo es colgárselo.
		UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOrAddOn(Turtle);
		bUsed = Effects && Effects->ServerGrantFloat();
		break;
	}
	default: break;
	}
	if (!bUsed)
	{
		PlayCue(Turtle, ETNRaceSound::Nope);
		UE_LOG(LogTortunabo, Log, TEXT("[TcT] %s no puede usar %s ahora."), *GetNameSafe(Turtle), TNTctItemRules::Spec(Kind).Code);
		return;
	}

	// Una carga menos: con la última, se gasta.
	const FName Next = TNTctItemRules::ItemIdAfterUse(Item.ItemId);
	if (Next.IsNone())
	{
		FTN_InventoryItem Consumed;
		Inventory->TryConsumeEquippedItem(Consumed);
	}
	else
	{
		FTN_InventoryItem Remaining = Item;
		Remaining.ItemId = Next;
		Remaining.ItemIcon = nullptr;
		ResolveVisuals(Remaining);
		Inventory->TryReplaceEquippedItem(Remaining);
	}
	Turtle->MulticastItemThrowAnim();
}
