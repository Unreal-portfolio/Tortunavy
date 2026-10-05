// Objeto del coop lanzado en arco (cáscara resbaladiza). Ver TN_CoopThrownItem.h.

#include "World/TN_CoopThrownItem.h"
#include "../Game/TN_CoopItemArt.h"
#include "Components/StaticMeshComponent.h"
#include "Core/ITN_EnemyTargetInterface.h"
#include "Core/TN_Log.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/TN_CoopItemRules.h"
#include "Game/TN_TctItems.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_RaceItems.h"

namespace TNCoopThrownItemDetail
{
	/** Cada cuánto (s) mira el servidor si alguien pisa el parche. */
	constexpr float PeelScanSeconds = 0.1f;
	/** Vueltas por segundo (grados) mientras vuela. */
	constexpr float SpinDegPerSecond = 720.f;
	/** Altura (cm) sobre el suelo a la que queda el parche. */
	constexpr float RestHeight = 3.f;

	/** El suelo bajo Where (WorldStatic), o Where si no hay suelo a mano. */
	FVector GroundUnder(const UWorld* World, const FVector& Where, const AActor* Ignore)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CoopThrowGround), false, Ignore);
		if (World && World->LineTraceSingleByObjectType(Hit, Where + FVector(0.0, 0.0, 300.0), Where - FVector(0.0, 0.0, 3000.0),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			return Hit.ImpactPoint;
		}
		return Where;
	}

	/** El enemigo del coop (ITN_EnemyTargetInterface, no una tortuga) sin aturdir más cercano a Center a menos de Radius; null si no hay. */
	ITN_EnemyTargetInterface* FindInterfaceEnemy(const UWorld* World, const FVector& Center, float Radius, const AActor* Ignore)
	{
		if (!World)
		{
			return nullptr;
		}
		TArray<FOverlapResult> Overlaps;
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius),
			FCollisionQueryParams(SCENE_QUERY_STAT(CoopItemEnemies), false, Ignore));
		ITN_EnemyTargetInterface* Best = nullptr;
		double BestDistSq = TNumericLimits<double>::Max();
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* Actor = Overlap.GetActor();
			ITN_EnemyTargetInterface* Enemy = Cast<ITN_EnemyTargetInterface>(Actor);
			if (!Enemy || Cast<ATortugaCharacter>(Actor) || Enemy->IsStunned())
			{
				continue;
			}
			const double DistSq = FVector::DistSquared(Actor->GetActorLocation(), Center);
			if (DistSq < BestDistSq)
			{
				Best = Enemy;
				BestDistSq = DistSq;
			}
		}
		return Best;
	}

	/** Dónde quiere tirarlo Thrower: donde su puntería da en el escenario o, si no da, a Range de sus ojos. */
	FVector AimPoint(const ATortugaCharacter* Thrower, float Range)
	{
		const UWorld* World = Thrower->GetWorld();
		const FVector Eye = Thrower->GetActorLocation() + FVector(0.0, 0.0, 60.0);
		const FVector Dir = Thrower->GetTurtleAimRotation().Vector();
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CoopThrowAim), false, Thrower);
		if (World && World->LineTraceSingleByObjectType(Hit, Eye, Eye + Dir * (Range * 1.5f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			return Hit.ImpactPoint;
		}
		return Eye + Dir * Range;
	}
}

ATN_CoopThrownItem::ATN_CoopThrownItem()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// Cada máquina pone el arco desde el lanzamiento replicado: sin movimiento replicado.
	SetReplicateMovement(false);
	SetNetCullDistanceSquared(FMath::Square(20000.f));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetGenerateOverlapEvents(false);
	RootComponent = Mesh;
}

void ATN_CoopThrownItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ATN_CoopThrownItem, Throw, COND_InitialOnly);
}

double ATN_CoopThrownItem::Now() const
{
	return TNRaceItems::ServerNow(GetWorld());
}

float ATN_CoopThrownItem::FlightAlpha() const
{
	return Throw.FlightSeconds > 0.f ? FMath::Clamp(static_cast<float>((Now() - Throw.StartTime) / Throw.FlightSeconds), 0.f, 1.f) : 1.f;
}

bool ATN_CoopThrownItem::HasLanded() const
{
	return FlightAlpha() >= 1.f;
}

bool ATN_CoopThrownItem::ServerThrow(ATortugaCharacter* Thrower, uint8 Kind, float MaxRange)
{
	using namespace TNCoopThrownItemDetail;
	UWorld* World = Thrower ? Thrower->GetWorld() : nullptr;
	if (!World || !Thrower->HasAuthority())
	{
		return false;
	}
	const FVector Forward = Thrower->GetActorForwardVector();
	const FVector Flat = FVector(Forward.X, Forward.Y, 0.0).GetSafeNormal();
	const FVector Origin = Thrower->GetActorLocation() + FVector(0.0, 0.0, 50.0) + Flat * 60.0;
	const FVector Clamped = TNCoopItemRules::ClampThrowTarget(Origin, AimPoint(Thrower, MaxRange), MaxRange);
	const FVector Target = GroundUnder(World, Clamped, Thrower) + FVector(0.0, 0.0, RestHeight);
	const float Distance = static_cast<float>(FVector::Dist(Origin, Target));

	FActorSpawnParameters Params;
	Params.Owner = Thrower;
	Params.Instigator = Thrower;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	const FTransform Start(Flat.Rotation(), Origin);
	ATN_CoopThrownItem* Item = World->SpawnActor<ATN_CoopThrownItem>(ATN_CoopThrownItem::StaticClass(), Start, Params);
	if (!Item)
	{
		return false;
	}
	// En el primer paquete: los clientes nacen ya con el lanzamiento.
	Item->Throw.Kind = Kind;
	Item->Throw.Origin = Origin;
	Item->Throw.Target = Target;
	Item->Throw.StartTime = static_cast<float>(TNRaceItems::ServerNow(World));
	Item->Throw.FlightSeconds = TNCoopItemRules::ThrowFlightSeconds(Distance);
	Item->Throw.ArcHeight = TNCoopItemRules::ThrowArcHeight(Distance);
	Item->FinishSpawning(Start);
	UE_LOG(LogTortunabo, Log, TEXT("[Coop] %s lanza %s a %.0f cm."), *GetNameSafe(Thrower), TNCoopItemRules::Spec(static_cast<ETNCoopItem>(Kind)).Code,
		FVector::Dist2D(Origin, Target));
	return true;
}

void ATN_CoopThrownItem::BeginPlay()
{
	Super::BeginPlay();
	ApplyThrow();
	if (HasAuthority())
	{
		SetLifeSpan(Throw.FlightSeconds + TNCoopItemTuning::PeelLifeSeconds);
	}
}

void ATN_CoopThrownItem::OnRep_Throw()
{
	ApplyThrow();
}

void ATN_CoopThrownItem::ApplyThrow()
{
	if (bThrowApplied || Throw.Kind == 0)
	{
		return;
	}
	bThrowApplied = true;
	TNCoopItemArt::FHeldLook Look;
	if (TNCoopItemArt::GetHeldLook(static_cast<ETNCoopItem>(Throw.Kind), Look))
	{
		Mesh->SetStaticMesh(Look.Mesh);
	}
	SetActorLocation(TNCoopItemRules::ArcPoint(Throw.Origin, Throw.Target, FlightAlpha(), Throw.ArcHeight));
}

void ATN_CoopThrownItem::Tick(float DeltaSeconds)
{
	using namespace TNCoopThrownItemDetail;
	Super::Tick(DeltaSeconds);
	if (!bThrowApplied || bSpent)
	{
		return;
	}
	if (!bLandedHere)
	{
		const float Alpha = FlightAlpha();
		SetActorLocation(TNCoopItemRules::ArcPoint(Throw.Origin, Throw.Target, Alpha, Throw.ArcHeight));
		AddActorWorldRotation(FRotator(0.f, SpinDegPerSecond * DeltaSeconds, 0.f));
		if (Alpha >= 1.f)
		{
			// En el suelo, plano y quieto.
			bLandedHere = true;
			SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
			if (!HasAuthority())
			{
				SetActorTickEnabled(false);
			}
		}
		return;
	}
	if (HasAuthority())
	{
		PeelScanClock -= DeltaSeconds;
		if (PeelScanClock <= 0.f)
		{
			PeelScanClock = PeelScanSeconds;
			ServerTickPeel();
		}
	}
}

void ATN_CoopThrownItem::ServerTickPeel()
{
	UWorld* World = GetWorld();
	if (!World || bSpent || Now() - (Throw.StartTime + Throw.FlightSeconds) < TNCoopItemTuning::PeelArmSeconds)
	{
		return;
	}
	const FVector Patch = Throw.Target;
	// Tortugas: la primera que lo pisa resbala (también quien lo lanzó). Protegidas, en el caparazón o en brazos, no.
	for (TActorIterator<ATortugaCharacter> It(World); It; ++It)
	{
		ATortugaCharacter* Turtle = *It;
		const FVector Feet = Turtle ? Turtle->GetActorLocation() - FVector(0.0, 0.0, Turtle->GetSimpleCollisionHalfHeight()) : FVector::ZeroVector;
		if (!Turtle || !TNCoopItemRules::IsOnPatch(Patch, Feet) || !TNTctItems::CanAffect(Turtle, true))
		{
			continue;
		}
		UTN_TurtleMovementComponent::LaunchFromServer(Turtle, TNCoopItemRules::SlipVelocity(Turtle->GetVelocity(), Turtle->GetActorForwardVector()));
		TNTctItems::PlayCue(Turtle, ETNRaceSound::Splat, 1.4f);
		UE_LOG(LogTortunabo, Log, TEXT("[Coop] %s resbala en una cáscara."), *GetNameSafe(Turtle));
		MulticastSlip(Patch);
		return;
	}
	// Enemigos de la playa: se marean un momento.
	ATN_BeachEnemy* Enemy = ATN_BeachEnemy::FindProjectileHit(this, Patch + FVector(0.0, 0.0, 100.0), Patch + FVector(0.0, 0.0, 5.0), TNCoopItemTuning::PeelRadius);
	if (Enemy && Enemy->AcceptsHitStun())
	{
		Enemy->ApplyHitStun(TNCoopItemTuning::PeelEnemyStunSeconds, this);
		UE_LOG(LogTortunabo, Log, TEXT("[Coop] %s resbala en una cáscara."), *GetNameSafe(Enemy));
		MulticastSlip(Patch);
		return;
	}
	// Los enemigos del coop (cangrejos...): aturdidos un momento.
	if (ITN_EnemyTargetInterface* Target = TNCoopThrownItemDetail::FindInterfaceEnemy(World, Patch + FVector(0.0, 0.0, 40.0), TNCoopItemTuning::PeelRadius, this))
	{
		Target->ApplyStun(TNCoopItemTuning::PeelEnemyStunSeconds);
		MulticastSlip(Patch);
	}
}

void ATN_CoopThrownItem::MulticastSlip_Implementation(FVector_NetQuantize Where)
{
	// Gastado: se esconde ya en todas las máquinas y el servidor lo quita enseguida.
	bSpent = true;
	SetActorHiddenInGame(true);
	if (HasAuthority())
	{
		SetLifeSpan(0.2f);
	}
}
