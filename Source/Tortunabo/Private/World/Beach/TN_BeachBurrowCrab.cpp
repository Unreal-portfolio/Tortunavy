#include "World/Beach/TN_BeachBurrowCrab.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TN_BeachEnemyKit.h"
#include "TN_BeachEnemyMeshes.h"
#include "TN_BeachTrapKit.h"
#include "World/Beach/TN_BeachTrapStatusComponent.h"

namespace TNBeachBurrowCrabDetail
{
	using EState = TNBeachCreatureRules::BurrowCrab::EState;

	inline uint8 ToByte(EState S) { return static_cast<uint8>(S); }

	/** Escala de la pinza del cangrejo gigante para este (la mano mide ~2,5 m en el gigante). */
	constexpr float ClawScale = 0.45f;
	/** Alto de la pinza fuera (cm sobre la arena). */
	constexpr float ClawOutZ = 170.f;
}

ATN_BeachBurrowCrab::ATN_BeachBurrowCrab()
{
	bThrottleWhenFar = true;
}

void ATN_BeachBurrowCrab::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachBurrowCrab, Grabbed);
}

void ATN_BeachBurrowCrab::ApplySpec()
{
	Build();
}

void ATN_BeachBurrowCrab::Build()
{
	if (Mound)
	{
		return;
	}
	USceneComponent* RigRoot = MakeRig();
	if (!bHasScreen)
	{
		return;
	}
	using TNProcMesh::FTNProcMeshBuffers;
	UStaticMesh* MoundMesh = TNBeachKit::CachedMesh(TEXT("Beach.BurrowCrab.Mound"), [](FTNProcMeshBuffers& M)
	{
		TNProcMesh::TNProcAddCylinder(M, FVector(0.0, 0.0, -5.0), FVector(0.0, 0.0, 45.0), 150.0, 60.0, 12, FLinearColor(0.85f, 0.72f, 0.5f), true);
		M.AddBox(FVector(30.0, -20.0, 46.0), FVector::ForwardVector, FVector(14.0, 10.0, 4.0), FLinearColor(0.7f, 0.58f, 0.4f));
	});
	Mound = TNBeachKit::AddPart(this, RigRoot, MoundMesh, FVector::ZeroVector, true);

	const TNBeachMeshes::FCrabLook Look = TNBeachMeshes::CrabPalette(Spec.Seed + 1);
	const FString Pal = FString::Printf(TEXT("Beach.Crab.%d."), (((Spec.Seed + 1) % 4) + 4) % 4);
	UStaticMesh* HandMesh = TNBeachKit::CachedMesh(Pal + TEXT("Hand"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabBigHand(M, Look); });
	UStaticMesh* FingerMesh = TNBeachKit::CachedMesh(Pal + TEXT("Finger"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabBigFinger(M, Look); });
	ClawRoot = NewObject<USceneComponent>(this, TEXT("BurrowClawRoot"));
	ClawRoot->SetupAttachment(RigRoot);
	ClawRoot->SetRelativeScale3D(FVector(TNBeachBurrowCrabDetail::ClawScale));
	// La mano apunta hacia arriba: la pinza sale de la arena en vertical.
	ClawRoot->SetRelativeRotation(FRotator(80.f, 0.f, 0.f));
	ClawRoot->RegisterComponent();
	UStaticMeshComponent* Hand = TNBeachKit::AddPart(this, ClawRoot, HandMesh, FVector::ZeroVector, true);
	const TNBeachMeshes::FCrabRig R = TNBeachMeshes::CrabRig();
	Finger = TNBeachKit::AddPart(this, Hand, FingerMesh, R.BigKnuckle - R.BigElbow, true);
	ClawRoot->SetVisibility(false, true);
}

float ATN_BeachBurrowCrab::ClawHeight(TNBeachCreatureRules::BurrowCrab::EState State, float Age) const
{
	using namespace TNBeachBurrowCrabDetail;
	switch (State)
	{
		case EState::Strike: return FMath::Lerp(-120.f, ClawOutZ, FMath::Clamp(Age / FMath::Max(0.05f, Times.Strike), 0.f, 1.f));
		case EState::Hold:   return ClawOutZ + 10.f * FMath::Sin(Age * 14.f);
		case EState::Recharge:
		case EState::Hide:   return FMath::Lerp(ClawOutZ, -150.f, FMath::Clamp(Age / 0.35f, 0.f, 1.f));
		default:             return -150.f;
	}
}

FVector ATN_BeachBurrowCrab::GripPoint() const
{
	return ShownLoc + FVector(0.0, 0.0, TNBeachBurrowCrabDetail::ClawOutZ + 60.0);
}

void ATN_BeachBurrowCrab::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateHold();
}

void ATN_BeachBurrowCrab::UpdateHold()
{
	using namespace TNBeachBurrowCrabDetail;
	ATortugaCharacter* Victim = Grabbed;
	const bool bHold = IsValid(Victim) && !Victim->IsDead() && static_cast<EState>(GetMoverState()) == EState::Hold;
	if (!bHold)
	{
		if (GetHeldTurtle())
		{
			EndHoldTurtle();
		}
		return;
	}
	BeginHoldTurtle(Victim);
	PlaceHeldTurtle(GripPoint(), ShownYaw + 25.f * FMath::Sin(GetStateAge() * 9.f));
}

void ATN_BeachBurrowCrab::ServerTick(float DeltaSeconds)
{
	using namespace TNBeachBurrowCrabDetail;
	using TNBeachCreatureRules::BurrowCrab::FInput;
	if (!bPlaced)
	{
		bPlaced = true;
		ServerMoveTo(Home, static_cast<float>(GetActorRotation().Yaw));
		ServerSetState(ToByte(EState::Buried), Home);
	}
	const EState State = static_cast<EState>(GetMoverState());
	FInput In;
	In.Age = GetStateAge();
	In.bHitStunned = IsHitStunned();
	ATortugaCharacter* Victim = Grabbed;
	In.bHolding = IsValid(Victim) && !Victim->IsDead() && !Victim->IsInShell() && !Victim->IsKnockedDown() && !TNBeach::IsTurtleRelocating(Victim);
	const UTN_BeachTrapStatusComponent* Status = Victim ? UTN_BeachTrapStatusComponent::FindOn(Victim) : nullptr;
	In.bEscaped = Status && Status->HasEscaped();
	if (State == EState::Buried && IsRaceLive(this))
	{
		ATortugaCharacter* Near = FindTarget(SimLoc, TellRadius, Home, 0.f);
		In.bTargetNear = Near != nullptr;
		Target = Near;
	}
	ATortugaCharacter* InGrab = nullptr;
	if (State == EState::Strike)
	{
		InGrab = FindTarget(SimLoc, GrabRadius, Home, 0.f);
		In.bTargetInGrab = InGrab && InGrab->GetCharacterMovement() && !InGrab->GetCharacterMovement()->IsFalling();
	}
	const EState Next = TNBeachCreatureRules::BurrowCrab::Next(State, In, Times);
	if (Next == State)
	{
		return;
	}
	if (State == EState::Hold)
	{
		ReleaseVictim(Next == EState::Recharge && In.bHolding);
	}
	if (Next == EState::Hold && InGrab)
	{
		Grabbed = InGrab;
		if (UTN_BeachTrapStatusComponent* GrabStatus = UTN_BeachTrapStatusComponent::FindOrAddTo(InGrab))
		{
			GrabStatus->ServerArmEscape(true);
		}
		IgnoreTurtle(InGrab, Times.HoldMax + Times.Recharge);
	}
	ServerSetState(ToByte(Next), SimLoc);
	ForceNetUpdate();
}

void ATN_BeachBurrowCrab::ReleaseVictim(bool bThrow)
{
	ATortugaCharacter* Victim = Grabbed;
	EndHoldTurtle();
	Grabbed = nullptr;
	if (!IsValid(Victim))
	{
		return;
	}
	if (UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOn(Victim))
	{
		Status->ServerArmEscape(false);
	}
	if (bThrow)
	{
		FVector Out = Victim->GetActorLocation() - SimLoc;
		Out.Z = 0.0;
		Out = Out.IsNearlyZero() ? -GetActorForwardVector() : Out.GetSafeNormal();
		StunTurtle(Victim, ThrowStunSeconds, Out * ThrowOut + FVector(0.0, 0.0, ThrowUp));
	}
	IgnoreTurtle(Victim, Times.Recharge);
}

void ATN_BeachBurrowCrab::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Destruido con una tortuga agarrada (p. ej., al cerrar una ronda): se le desarma el forcejeo; si no, cada salto
	// suyo seguiría siendo forcejeo el resto de la vida del peón.
	if (HasAuthority() && Grabbed)
	{
		if (UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOn(Grabbed))
		{
			Status->ServerArmEscape(false);
		}
	}
	Grabbed = nullptr;
	Super::EndPlay(EndPlayReason);
}

void ATN_BeachBurrowCrab::OnHoldAborted(ATortugaCharacter* Turtle)
{
	if (!HasAuthority() || !Turtle || Grabbed != Turtle)
	{
		return;
	}
	if (UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOn(Turtle))
	{
		Status->ServerArmEscape(false);
	}
	Grabbed = nullptr;
	ServerSetState(TNBeachBurrowCrabDetail::ToByte(TNBeachBurrowCrabDetail::EState::Recharge), SimLoc);
	ForceNetUpdate();
}

void ATN_BeachBurrowCrab::OnMoverStateChanged(uint8 OldState)
{
	using namespace TNBeachBurrowCrabDetail;
	if (!bHasScreen)
	{
		return;
	}
	const EState State = static_cast<EState>(GetMoverState());
	if (ClawRoot)
	{
		ClawRoot->SetVisibility(State == EState::Strike || State == EState::Hold || State == EState::Recharge || State == EState::Hide, true);
	}
	if (State == EState::Strike && SnapSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, SnapSound, ShownLoc);
	}
	if (State == EState::Hide)
	{
		ShowPop(NSLOCTEXT("TNBeachBurrowCrab", "Hide", "¡GLUP!"), FColor(240, 200, 120), ShownLoc + FVector(0.0, 0.0, 160.0));
	}
}

void ATN_BeachBurrowCrab::VisualTick(float DeltaSeconds)
{
	using namespace TNBeachBurrowCrabDetail;
	const EState State = static_cast<EState>(GetMoverState());
	const float Age = GetStateAge();
	ShakeClock += DeltaSeconds;
	if (Mound)
	{
		// Aviso: el montículo tiembla y se hincha; enterrado, respira muy despacio.
		const float Shake = State == EState::Tell ? 6.f * FMath::Sin(ShakeClock * 55.f) : 0.f;
		const float Swell = State == EState::Tell ? 1.f + 0.12f * FMath::Clamp(Age / FMath::Max(0.05f, Times.Tell), 0.f, 1.f)
			: 1.f + 0.02f * FMath::Sin(ShakeClock * 1.5f);
		Mound->SetRelativeLocation(FVector(Shake, -Shake * 0.6f, 0.0));
		Mound->SetRelativeScale3D(FVector(1.0, 1.0, Swell));
	}
	if (ClawRoot)
	{
		ClawRoot->SetRelativeLocation(FVector(0.0, 0.0, ClawHeight(State, Age)));
	}
	if (Finger)
	{
		const float Pinch = State == EState::Hold ? -30.f : -5.f + 20.f * FMath::Clamp(Age * 4.f, 0.f, 1.f);
		Finger->SetRelativeRotation(FRotator(Pinch, 0.f, 0.f));
	}
}
