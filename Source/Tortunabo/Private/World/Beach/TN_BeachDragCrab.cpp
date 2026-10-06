#include "World/Beach/TN_BeachDragCrab.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TN_BeachEnemyKit.h"
#include "TN_BeachEnemyMeshes.h"
#include "TN_BeachTrapKit.h"
#include "World/Beach/TN_BeachCreatureRules.h"
#include "World/Beach/TN_BeachTrapStatusComponent.h"
#include "World/TN_DeathZoneVolume.h"

namespace TNBeachDragCrabDetail
{
	using EState = TNBeachCreatureRules::DragCrab::EState;
	using EDragEnd = TNBeachCreatureRules::DragCrab::EDragEnd;

	inline uint8 ToByte(EState S) { return static_cast<uint8>(S); }

	/** Segundos que se queda quieto tras soltar (o mareado) antes de volver a rondar. */
	constexpr float RecoverSeconds = 2.5f;
	/** No vuelve a ir a por la misma tortuga en estos segundos tras soltarla. */
	constexpr float IgnoreAfterRelease = 4.f;
}

ATN_BeachDragCrab::ATN_BeachDragCrab()
{
	bThrottleWhenFar = true;
}

void ATN_BeachDragCrab::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachDragCrab, Grabbed);
}

void ATN_BeachDragCrab::ApplySpec()
{
	SizeK = 0.32f * FMath::Clamp(Spec.SizeScale, 0.8f, 1.2f);
	BodyRadius = static_cast<float>(TNBeachMeshes::CrabW * TNBeach::Scale * 1.05 * SizeK);
	BuildCrab();
}

void ATN_BeachDragCrab::BuildCrab()
{
	if (Scaler)
	{
		return;
	}
	USceneComponent* RigRoot = MakeRig();
	Scaler = NewObject<USceneComponent>(this, TEXT("DragCrabScaler"));
	Scaler->SetupAttachment(RigRoot);
	Scaler->SetRelativeScale3D(FVector(SizeK));
	Scaler->RegisterComponent();
	if (!bHasScreen)
	{
		return;
	}
	const TNBeachMeshes::FCrabRig R = TNBeachMeshes::CrabRig();
	const TNBeachMeshes::FCrabLook Look = TNBeachMeshes::CrabPalette(Spec.Seed + 2);
	const FString Pal = FString::Printf(TEXT("Beach.Crab.%d."), (((Spec.Seed + 2) % 4) + 4) % 4);
	using TNProcMesh::FTNProcMeshBuffers;
	UStaticMesh* BodyMesh = TNBeachKit::CachedMesh(Pal + TEXT("Body"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabBody(M, Look); });
	UStaticMesh* LegL = TNBeachKit::CachedMesh(Pal + TEXT("LegL"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabLeg(M, Look, -1.0); });
	UStaticMesh* LegR = TNBeachKit::CachedMesh(Pal + TEXT("LegR"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabLeg(M, Look, 1.0); });
	UStaticMesh* EyeMesh = TNBeachKit::CachedMesh(Pal + TEXT("Eye"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabEye(M, Look); });
	UStaticMesh* HandMesh = TNBeachKit::CachedMesh(Pal + TEXT("Hand"), [&Look](FTNProcMeshBuffers& M) { TNBeachMeshes::BuildCrabBigHand(M, Look); });
	Body = TNBeachKit::AddPart(this, Scaler, BodyMesh, FVector(0.0, 0.0, R.BodyZ), true);
	TNBeachKit::AddPart(this, Body, EyeMesh, R.EyeL, false);
	TNBeachKit::AddPart(this, Body, EyeMesh, R.EyeR, false);
	for (int32 k = 0; k < 8; ++k)
	{
		UStaticMeshComponent* Leg = TNBeachKit::AddPart(this, Body, k < 4 ? LegL : LegR, R.LegPivot[k], true);
		if (Leg)
		{
			Leg->SetRelativeRotation(FRotator(0.f, R.LegSplay[k], 0.f));
		}
		Legs.Add(Leg);
	}
	Claw = TNBeachKit::AddPart(this, Body, HandMesh, R.BigShoulder, true);
}

FVector ATN_BeachDragCrab::GripPoint() const
{
	const FVector Forward = FRotator(0.f, ShownYaw, 0.f).Vector();
	return ShownLoc + Forward * (BodyRadius + 70.f) + FVector(0.0, 0.0, 70.0);
}

void ATN_BeachDragCrab::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateHold();
}

void ATN_BeachDragCrab::UpdateHold()
{
	using namespace TNBeachDragCrabDetail;
	ATortugaCharacter* Victim = Grabbed;
	const bool bHold = IsValid(Victim) && !Victim->IsDead() && static_cast<EState>(GetMoverState()) == EState::Drag;
	if (!bHold)
	{
		if (GetHeldTurtle())
		{
			EndHoldTurtle();
		}
		return;
	}
	BeginHoldTurtle(Victim);
	PlaceHeldTurtle(GripPoint(), ShownYaw + 180.f);
}

void ATN_BeachDragCrab::WalkToward(const FVector& Goal, float Speed, float DeltaSeconds)
{
	FVector Dir = Goal - SimLoc;
	Dir.Z = 0.0;
	const double Dist = Dir.Size();
	if (Dist < 5.0)
	{
		return;
	}
	Dir /= Dist;
	FVector Next = ResolveStep(SimLoc + Dir * FMath::Min(Dist, static_cast<double>(Speed * DeltaSeconds)), BodyRadius);
	float Z = static_cast<float>(SimLoc.Z);
	if (GroundHeightAt(Next, Z))
	{
		Next.Z = Z;
	}
	ServerMoveTo(Next, static_cast<float>(Dir.Rotation().Yaw));
}

FVector ATN_BeachDragCrab::DragDirectionFor(const ATortugaCharacter* Victim) const
{
	// Hacia su sitio.
	FVector ToHome = Home - SimLoc;
	ToHome.Z = 0.0;
	if (ToHome.SizeSquared() > FMath::Square(100.0))
	{
		return ToHome.GetSafeNormal();
	}
	return Victim ? -Victim->GetActorForwardVector().GetSafeNormal2D() : -GetActorForwardVector();
}

bool ATN_BeachDragCrab::IsSafeAhead(const FVector& Next) const
{
	float Z = 0.f;
	const bool bGround = GroundHeightAt(Next, Z);
	bool bDanger = false;
	for (TActorIterator<ATN_DeathZoneVolume> It(GetWorld()); It && !bDanger; ++It)
	{
		bDanger = It->GetComponentsBoundingBox().ExpandBy(BodyRadius).IsInsideXY(Next);
	}
	return TNBeachCreatureRules::DragCrab::IsSafeAhead(bGround, Z - static_cast<float>(SimLoc.Z), bDanger);
}

void ATN_BeachDragCrab::ServerTick(float DeltaSeconds)
{
	using namespace TNBeachDragCrabDetail;
	if (!bPlaced)
	{
		bPlaced = true;
		RoamGoal = Home;
		ServerMoveTo(Home, static_cast<float>(GetActorRotation().Yaw));
		ServerSetState(ToByte(EState::Roam), Home);
	}
	const EState State = static_cast<EState>(GetMoverState());
	const bool bStunned = IsHitStunned();
	switch (State)
	{
	case EState::Roam:
	{
		if (bStunned)
		{
			break;
		}
		RoamTimer -= DeltaSeconds;
		if (RoamTimer <= 0.f || FVector::Dist2D(SimLoc, RoamGoal) < 40.0)
		{
			RoamTimer = ServerRng.FRandRange(2.5f, 5.f);
			const float Angle = ServerRng.FRandRange(0.f, 2.f * PI);
			RoamGoal = Home + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * ServerRng.FRandRange(50.f, 400.f);
		}
		WalkToward(RoamGoal, RoamSpeed, DeltaSeconds);
		if (ATortugaCharacter* Found = FindTarget(SimLoc, DetectRange, Home, Leash))
		{
			Target = Found;
			ServerSetState(ToByte(EState::Chase), Found->GetActorLocation());
		}
		break;
	}
	case EState::Chase:
	{
		ATortugaCharacter* Victim = Target.Get();
		if (bStunned || !IsTargetable(Victim) || FVector::Dist2D(SimLoc, Victim->GetActorLocation()) > DetectRange * 1.6f
			|| FVector::Dist2D(SimLoc, Home) > Leash)
		{
			Target.Reset();
			ServerSetState(ToByte(bStunned ? EState::Recover : EState::Roam), SimLoc);
			break;
		}
		const FVector At = Victim->GetActorLocation();
		ServerSetAim(At);
		WalkToward(At, ChaseSpeed, DeltaSeconds);
		if (FVector::Dist2D(SimLoc, At) < BodyRadius + 110.f && Victim->GetCharacterMovement() && Victim->GetCharacterMovement()->IsMovingOnGround())
		{
			Grabbed = Victim;
			Dragged = 0.f;
			DragDir = DragDirectionFor(Victim);
			if (UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOrAddTo(Victim))
			{
				Status->ServerArmEscape(true);
			}
			IgnoreTurtle(Victim, IgnoreAfterRelease + MaxDragDistance / FMath::Max(1.f, DragSpeed));
			ServerSetState(ToByte(EState::Drag), At);
			ForceNetUpdate();
		}
		break;
	}
	case EState::Drag:
	{
		ATortugaCharacter* Victim = Grabbed;
		const bool bTaken = !IsValid(Victim) || Victim->IsDead() || Victim->IsInShell() || Victim->IsKnockedDown()
			|| TNBeach::IsTurtleStunned(Victim) || TNBeach::IsTurtleRelocating(Victim);
		if (bTaken)
		{
			ReleaseDrag(static_cast<uint8>(EDragEnd::HitStunned));
			break;
		}
		const UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOn(Victim);
		const float Step = TNBeachCreatureRules::DragCrab::DragStep(Dragged, MaxDragDistance, DragSpeed, DeltaSeconds);
		FVector Next = SimLoc + DragDir * Step;
		const bool bSafe = IsSafeAhead(Next + DragDir * (BodyRadius + 60.f));
		const EDragEnd End = TNBeachCreatureRules::DragCrab::DragEnd(Dragged, MaxDragDistance, Status && Status->HasEscaped(), bStunned, bSafe);
		if (End != EDragEnd::None)
		{
			ReleaseDrag(static_cast<uint8>(End));
			break;
		}
		Next = ResolveStep(Next, BodyRadius);
		float Z = static_cast<float>(SimLoc.Z);
		if (GroundHeightAt(Next, Z))
		{
			Next.Z = Z;
		}
		Dragged += static_cast<float>(FVector::Dist2D(Next, SimLoc));
		// Anda de espaldas: la pinza (delante) mira a la tortuga, que va detrás de él en el sentido del arrastre.
		ServerMoveTo(Next, static_cast<float>((-DragDir).Rotation().Yaw));
		if (Step <= KINDA_SMALL_NUMBER || FVector::Dist2D(Next, SimLoc + DragDir * Step) > Step * 0.8f)
		{
			// Atascado contra algo: se acaba el arrastre aquí.
			Dragged = MaxDragDistance;
		}
		break;
	}
	case EState::Recover:
	default:
		if (GetStateAge() >= RecoverSeconds && !bStunned)
		{
			ServerSetState(ToByte(EState::Roam), SimLoc);
		}
		break;
	}
}

void ATN_BeachDragCrab::ReleaseDrag(uint8 InEnd)
{
	using namespace TNBeachDragCrabDetail;
	const EDragEnd End = static_cast<EDragEnd>(InEnd);
	ATortugaCharacter* Victim = Grabbed;
	EndHoldTurtle();
	Grabbed = nullptr;
	if (IsValid(Victim))
	{
		if (UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOn(Victim))
		{
			Status->ServerArmEscape(false);
		}
		IgnoreTurtle(Victim, IgnoreAfterRelease);
		if (End == EDragEnd::Distance || End == EDragEnd::Unsafe)
		{
			// Suelta derribada con un empujón en el sentido del arrastre (nunca hacia lo que no es seguro: ahí, hacia arriba).
			const FVector Push = End == EDragEnd::Unsafe ? FVector(0.0, 0.0, 300.0) : DragDir * 380.f + FVector(0.0, 0.0, 260.0);
			KnockDownTurtle(Victim, KnockSeconds, Push);
		}
	}
	if (End == EDragEnd::Escaped)
	{
		// Se le ha escapado: se queda aturdido un momento, sin volver a por ella.
		ApplyHitStun(1.5f, nullptr);
	}
	ServerSetState(ToByte(EState::Recover), SimLoc);
	ForceNetUpdate();
}

void ATN_BeachDragCrab::EndPlay(const EEndPlayReason::Type EndPlayReason)
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

void ATN_BeachDragCrab::OnHoldAborted(ATortugaCharacter* Turtle)
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
	ServerSetState(TNBeachDragCrabDetail::ToByte(TNBeachDragCrabDetail::EState::Recover), SimLoc);
	ForceNetUpdate();
}

void ATN_BeachDragCrab::OnMoverStateChanged(uint8 OldState)
{
	using namespace TNBeachDragCrabDetail;
	if (!bHasScreen || static_cast<EState>(GetMoverState()) != EState::Drag)
	{
		return;
	}
	if (GrabSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, GrabSound, ShownLoc);
	}
	ShowPop(NSLOCTEXT("TNBeachDragCrab", "Grab", "¡ZAS!"), FColor(255, 120, 60), ShownLoc + FVector(0.0, 0.0, 200.0));
}

void ATN_BeachDragCrab::VisualTick(float DeltaSeconds)
{
	using namespace TNBeachDragCrabDetail;
	const EState State = static_cast<EState>(GetMoverState());
	const bool bMoving = State == EState::Chase || State == EState::Drag || State == EState::Roam;
	LegPhase += DeltaSeconds * (State == EState::Chase ? 16.f : (bMoving ? 9.f : 0.f));
	const TNBeachMeshes::FCrabRig R = TNBeachMeshes::CrabRig();
	for (int32 k = 0; k < Legs.Num(); ++k)
	{
		if (UStaticMeshComponent* Leg = Legs[k])
		{
			const float Swing = bMoving ? 14.f * FMath::Sin(LegPhase + k * 1.3f) : 0.f;
			Leg->SetRelativeRotation(FRotator(Swing, R.LegSplay[k], 0.f));
		}
	}
	if (Claw)
	{
		const float Pinch = State == EState::Drag ? -25.f : (State == EState::Chase ? 15.f * FMath::Sin(LegPhase * 0.7f) : 0.f);
		Claw->SetRelativeRotation(FRotator(Pinch, 0.f, 0.f));
	}
}
