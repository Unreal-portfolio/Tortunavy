#include "World/TN_Quicksand.h"
#include "World/TN_HazardEffects.h"

#include "Beach/TN_BeachTrapKit.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Player/TN_StaminaComponent.h"
#include "World/Beach/TN_BeachCreatureRules.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachTrapStatusComponent.h"

namespace TNQuicksandDetail
{
	/** Charco oscuro con borde de arena mojada y unas burbujas: radio R, a ras del suelo. */
	void BuildPuddle(TNBeachTrapKit::FBuffers& B, double R, uint32 Seed)
	{
		const FLinearColor Dark = TNPlaygroundKit::Rgb(0x6A5232, 0.25f);
		const FLinearColor Mid = TNPlaygroundKit::Rgb(0x7E6440, 0.2f);
		const FLinearColor Rim = TNBeachTrapKit::SandWet();
		TNPlaygroundKit::AddAnnulus(B, FVector(0.0, 0.0, 2.0), FVector::UpVector, R * 0.9, R * 1.06, 32, Rim);
		TNPlaygroundKit::AddAnnulus(B, FVector(0.0, 0.0, 2.6), FVector::UpVector, R * 0.55, R * 0.9, 32, Mid);
		TNPlaygroundKit::AddDisc(B, FVector(0.0, 0.0, 3.0), FVector::UpVector, R * 0.55, 28, Dark);
		for (int32 k = 0; k < 7; ++k)
		{
			const double A = TNPlaygroundKit::KitTwoPi * TNPlaygroundKit::Hash01(k, 1, Seed);
			const double D = R * (0.15 + 0.7 * TNPlaygroundKit::Hash01(k, 2, Seed));
			const double Rb = 8.0 + 10.0 * TNPlaygroundKit::Hash01(k, 3, Seed);
			TNPlaygroundKit::AddBall(B, FVector(FMath::Cos(A) * D, FMath::Sin(A) * D, 2.0), Rb, 6, TNPlaygroundKit::Shade(Mid, 1.15));
		}
	}
}

ATN_Quicksand::ATN_Quicksand()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;
	// La ralentización la pone la parte progresiva; la de la base no frena más que andar.
	MaxSlowSpeed = 450.f;
	MaxUpwardVelocity = 260.f;
	MaxFallVelocity = 600.f;
	JumpVelocityInZone = 260.f;
	GravityScaleInZone = 1.6f;

	PuddleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PuddleMesh"));
	PuddleMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(PuddleMesh);
	PuddleMesh->SetCastShadow(false);
}

void ATN_Quicksand::SetQuicksandRadius(float Radius)
{
	QuicksandRadius = FMath::Max(100.f, Radius);
	TriggerBox->SetBoxExtent(FVector(QuicksandRadius, QuicksandRadius, 150.f));
	// La caja va centrada en el suelo: la malla, a ras de arena.
	PuddleMesh->SetRelativeLocation(FVector(0.0, 0.0, 0.0));
	TNBeachTrapKit::FBuffers B;
	TNQuicksandDetail::BuildPuddle(B, QuicksandRadius, static_cast<uint32>(GetUniqueID()) * 2654435761u);
	TNBeachTrapKit::SetMesh(PuddleMesh, this, B);
}

bool ATN_Quicksand::IsInsidePuddle(const ACharacter* Turtle) const
{
	if (!IsValid(Turtle))
	{
		return false;
	}
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Turtle->GetActorLocation());
	const float HalfHeight = Turtle->GetSimpleCollisionHalfHeight();
	const double FeetZ = Local.Z - HalfHeight;
	return FVector2D(Local.X, Local.Y).Size() <= QuicksandRadius && FeetZ >= -SinkDepth - 60.0 && FeetZ <= 80.0;
}

void ATN_Quicksand::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const bool bServer = World->GetNetMode() != NM_Client;
	const double Now = World->GetTimeSeconds();
	TArray<ATortugaCharacter*> Turtles;
	ATN_BeachEnemy::GatherTurtles(this, Turtles);
	TSet<ACharacter*> Seen;
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (!IsInsidePuddle(Turtle))
		{
			continue;
		}
		Seen.Add(Turtle);
		FInside& State = Inside.FindOrAdd(Turtle);
		State.Seconds += DeltaTime;
		if (TNBeachTrapKit::SimulatesMovement(Turtle))
		{
			if (UTN_StaminaComponent* Stamina = Turtle->FindComponentByClass<UTN_StaminaComponent>())
			{
				const float Factor = TNBeachCreatureRules::Quicksand::SpeedFactor(State.Seconds, StartSpeedFactor, MinSpeedFactor, RampSeconds);
				Stamina->SetSpeedCap(ProgressiveSource(), Stamina->GetWalkSpeed() * Factor);
			}
		}
		if (bServer)
		{
			ServerUpdate(Turtle, State, Now);
		}
		LocalFeedback(Turtle, State);
	}
	for (auto It = Inside.CreateIterator(); It; ++It)
	{
		ACharacter* Turtle = It.Key().Get();
		if (Turtle && Seen.Contains(Turtle))
		{
			continue;
		}
		ClearLocalCap(Turtle);
		It.RemoveCurrent();
	}
}

void ATN_Quicksand::ServerUpdate(ACharacter* Turtle, FInside& State, double Now)
{
	UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOrAddTo(Turtle);
	if (!Status)
	{
		return;
	}
	if (Status->IsTrappedBy(this))
	{
		const double* Since = TrappedAt.Find(Turtle);
		const float Trapped = Since ? static_cast<float>(Now - *Since) : MaxTrappedSeconds;
		const bool bRelease = TNBeachCreatureRules::Quicksand::ShouldRelease(Trapped, MaxTrappedSeconds, Status->HasEscaped());
		const FTNHazardEffect& SinkEffect = UTN_HazardTuning::Get().Quicksand;
		if (TNHazard::QuicksandKills(bRelease, Status->HasEscaped(), SinkEffect.bKills))
		{
			// Arenas movedizas (#871): hundida hasta el final sin soltarse, muere donde está (sin el saltito de salida).
			Status->ServerRelease(FVector::ZeroVector, 0.f);
			TrappedAt.Remove(Turtle);
			ImmuneUntil.Add(Turtle, Now + ImmuneSeconds);
			State.Seconds = 0.f;
			TNHazard::Apply(SinkEffect, Turtle, this);
			return;
		}
		if (bRelease)
		{
			const FVector Out = (Turtle->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
			const FVector Dir = Out.IsNearlyZero() ? GetActorForwardVector() : Out;
			// Sube a la cota de antes de hundirse para que el saltito no arranque dentro de la arena.
			Turtle->SetActorLocation(Turtle->GetActorLocation() + FVector(0.0, 0.0, SinkDepth), false, nullptr, ETeleportType::TeleportPhysics);
			Status->ServerRelease(Dir * EscapeHopOut + FVector(0.0, 0.0, EscapeHopUp), DizzySeconds);
			TrappedAt.Remove(Turtle);
			ImmuneUntil.Add(Turtle, Now + ImmuneSeconds);
			State.Seconds = 0.f;
		}
		return;
	}
	const double* Immune = ImmuneUntil.Find(Turtle);
	if ((Immune && Now < *Immune) || Status->IsTrapped() || !TNBeachTrapKit::IsFreeTurtle(Turtle))
	{
		return;
	}
	const UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
	if (Move && Move->IsMovingOnGround() && TNBeachCreatureRules::Quicksand::ShouldTrap(State.Seconds, TrapAfterSeconds))
	{
		Status->ServerTrap(this, Turtle->GetActorLocation() - FVector(0.0, 0.0, SinkDepth));
		TrappedAt.Add(Turtle, Now);
	}
}

void ATN_Quicksand::LocalFeedback(ACharacter* Turtle, FInside& State)
{
	const UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOn(Turtle);
	// En los clientes no se sabe qué trampa la tiene (solo el servidor): basta con que esté atrapada dentro de este charco.
	const bool bTrapped = Status && Status->IsTrapped();
	if (bTrapped == State.bWasTrapped || GetNetMode() == NM_DedicatedServer)
	{
		State.bWasTrapped = bTrapped;
		return;
	}
	State.bWasTrapped = bTrapped;
	const FVector At = Turtle->GetActorLocation();
	if (bTrapped)
	{
		if (TrapSound) { UGameplayStatics::SpawnSoundAtLocation(this, TrapSound, At); }
		return;
	}
	if (EscapeSound) { UGameplayStatics::SpawnSoundAtLocation(this, EscapeSound, At); }
	if (EscapeVFX) { UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, EscapeVFX, At); }
}

void ATN_Quicksand::ClearLocalCap(ACharacter* Turtle) const
{
	if (UTN_StaminaComponent* Stamina = Turtle ? Turtle->FindComponentByClass<UTN_StaminaComponent>() : nullptr)
	{
		Stamina->ClearSpeedCap(ProgressiveSource());
	}
}

void ATN_Quicksand::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const TPair<TWeakObjectPtr<ACharacter>, FInside>& Pair : Inside)
	{
		ACharacter* Turtle = Pair.Key.Get();
		ClearLocalCap(Turtle);
		UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOn(Turtle);
		if (Status && Status->IsTrappedBy(this) && GetNetMode() != NM_Client)
		{
			Status->ServerRelease(FVector(0.0, 0.0, EscapeHopUp), 0.f);
		}
	}
	Inside.Reset();
	Super::EndPlay(EndPlayReason);
}
