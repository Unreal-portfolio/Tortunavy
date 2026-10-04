#include "World/TN_CrabActor.h"
#include "Core/TN_Log.h"
#include "Core/TN_DebugCVars.h"
#include "DrawDebugHelpers.h"
#include "Player/TortugaCharacter.h"
#include "Core/TN_CoopPlayerState.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Animation/AnimInstance.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "World/TN_EnemyDecisions.h"
#include "World/TN_WorldTuning.h"

ATN_CrabActor::ATN_CrabActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// Relevancy por distancia (default del engine, ~150m): un cangrejo lejano no
	// replica su patrol a quien no puede verlo. Espectadores no afectados — la
	// relevancy se evalúa desde el ViewTarget del PlayerController, no desde su
	// posición. (Fase 2.5: antes bAlwaysRelevant = true.)
	SetReplicateMovement(true);
	// 25Hz bastan para el movimiento del cangrejo: snaps de ~40ms poco visibles
	// en un enemigo pequeño sin interpolación cliente. El default (100) replicaba
	// el patrol casi cada frame de servidor a todas las conexiones.
	SetNetUpdateFrequency(25.f);

	CrabMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CrabMesh"));
	SetRootComponent(CrabMesh);
	CrabMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CrabMesh->SetIsReplicated(false);

	DetectionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("DetectionSphere"));
	DetectionSphere->SetupAttachment(CrabMesh);
	DetectionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DetectionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	DetectionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// Cuerpo físico para items. Channel ECC_Pawn + Block to WorldDynamic.
	// Matriz UE5: la respuesta resultante entre dos componentes es la MÁS PERMISIVA
	// (Ignore < Overlap < Block).
	//   Concha OverlapSphere (WorldDynamic, Pawn=Overlap) ↔ BodyCollision (Pawn,
	//     WorldDynamic=Block) → min(Overlap,Block)=Overlap → overlap event en concha ✓
	//   Throwable Ball Mesh (WorldDynamic, BlockAllDynamic con Pawn=Block) ↔
	//     BodyCollision (Pawn, WorldDynamic=Block) → min(Block,Block)=Block →
	//     OnComponentHit dispara en la bola ✓
	// Mi intento anterior con Overlap a WorldDynamic generaba min(Block,Overlap)=
	// Overlap y la bola NUNCA hacía Hit (commit anterior tenía un comentario
	// erróneo justificando un comportamiento que UE5 no entrega).
	BodyCollision = CreateDefaultSubobject<USphereComponent>(TEXT("BodyCollision"));
	BodyCollision->SetupAttachment(CrabMesh);
	BodyCollision->InitSphereRadius(BodyCollisionRadius);
	BodyCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BodyCollision->SetCollisionObjectType(ECC_Pawn);
	BodyCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	BodyCollision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	BodyCollision->SetGenerateOverlapEvents(true);
	BodyCollision->SetNotifyRigidBodyCollision(true);
	// El pivote de la malla está en las patas: la esfera va a media altura del caparazón (16,5 cm de la malla sin escalar),
	// no medio enterrada.
	BodyCollision->SetRelativeLocation(FVector(0.f, 0.f, 16.5f));
}

void ATN_CrabActor::BeginPlay()
{
	Super::BeginPlay();

	// En todas las máquinas: la escala no se replica. Las esferas escalan con el actor, así que sus radios se dividen
	// para que midan en el mundo lo que dicen BodyCollisionRadius y DetectionRadius.
	SetActorScale3D(FVector(VisualScale));
	DetectionSphere->SetSphereRadius(DetectionRadius / VisualScale);
	BodyCollision->SetSphereRadius(BodyCollisionRadius / VisualScale);

	// Solo el servidor hace tick de lógica
	SetActorTickEnabled(HasAuthority());

	if (!HasAuthority()) { return; }

	// Diferir 1 tick: compatibilidad con chunks (Child Actor Components terminan
	// de posicionarse después de BeginPlay)
	FTimerDelegate Delegate;
	Delegate.BindUObject(this, &ATN_CrabActor::InitializePatrolPoints);
	GetWorldTimerManager().SetTimer(InitTimerHandle, Delegate, TNWorldTuning::ChunkChildActorSettleDelay, false);

	// OnDetectionBeginOverlap se registra en InitializePatrolPoints (deferred 1 tick)
	// para garantizar que SpawnLocation está inicializado antes de que pueda dispararse.
}

void ATN_CrabActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	ChaseTarget.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATN_CrabActor::InitializePatrolPoints()
{
	// Al suelo: la zona lo crea a su altura, que sobre el terreno generado puede quedar por encima o por debajo.
	float FloorZ = 0.f;
	if (FindFloor(GetActorLocation(), 300.f, 1000.f, FloorZ))
	{
		SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, FloorZ));
	}
	SpawnLocation = GetActorLocation();
	WorldPatrolPoints.Reset();

	for (const FVector& Offset : PatrolPoints)
	{
		WorldPatrolPoints.Add(SpawnLocation + Offset);
	}

	if (WorldPatrolPoints.IsEmpty())
	{
		WorldPatrolPoints.Add(SpawnLocation);
	}

	// Registrar el overlap AHORA que SpawnLocation está inicializado.
	// Si lo registráramos en BeginPlay, podría dispararse en el tick 0 antes de que
	// SpawnLocation esté seteado, causando un MaxChaseDistance check contra (0,0,0).
	DetectionSphere->OnComponentBeginOverlap.AddDynamic(
		this, &ATN_CrabActor::OnDetectionBeginOverlap);
}

void ATN_CrabActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_CrabActor, CrabState);
	DOREPLIFETIME(ATN_CrabActor, StunEndServerTime);
	DOREPLIFETIME(ATN_CrabActor, BlindEndServerTime);
}

// ── Tick ─────────────────────────────────────────────────────────────────────

void ATN_CrabActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!HasAuthority()) { return; }

	// TN.Enemy.Debug: radios de detección/ataque + estado FSM (no-op en Shipping)
	if (TNDebug::EnemyDebug != 0)
	{
		const FVector Loc = GetActorLocation();
		DrawDebugCircle(GetWorld(), Loc + FVector(0, 0, 5.f), DetectionRadius, 32,
			FColor::Yellow, false, -1.f, 0, 2.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
		DrawDebugCircle(GetWorld(), Loc + FVector(0, 0, 5.f), AttackRadius, 16,
			FColor::Red, false, -1.f, 0, 2.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
		DrawDebugString(GetWorld(), Loc + FVector(0, 0, 120.f),
			FString::Printf(TEXT("SRV %s stun=%.1f blind=%.1f"),
				*UEnum::GetValueAsString(CrabState), GetStunRemaining(), GetBlindRemaining()),
			nullptr, FColor::White, 0.f, true);
		if (const ATortugaCharacter* Chased = ChaseTarget.Get())
		{
			DrawDebugLine(GetWorld(), Loc, Chased->GetActorLocation(), FColor::Red,
				false, -1.f, 0, 2.f);
		}
	}

	// Stun: pausa toda la IA hasta que expira. Derivado del timestamp replicado —
	// sin decremento por tick (y sin tráfico de red continuo).
	if (IsStunned())
	{
		return;
	}
	// Blind: pierde target de chase y vuelve a patrol. Detection sphere ignorada
	// vía guard en OnDetectionBeginOverlap.
	if (IsBlinded())
	{
		if (CrabState == ETNCrabState::Chase || CrabState == ETNCrabState::Attack)
		{
			ChaseTarget.Reset();
			SetCrabState(ETNCrabState::Patrol);
		}
	}

	switch (CrabState)
	{
	case ETNCrabState::Patrol:   TickPatrol(DeltaTime);   break;
	case ETNCrabState::Chase:    TickChase(DeltaTime);    break;
	case ETNCrabState::Attack:   TickAttack();            break;
	case ETNCrabState::Cooldown: TickCooldown(DeltaTime); break;
	}
}

// ── Patrol ───────────────────────────────────────────────────────────────────

void ATN_CrabActor::TickPatrol(float DeltaTime)
{
	if (WorldPatrolPoints.Num() <= 1) { return; }

	const FVector& Target = WorldPatrolPoints[CurrentPatrolIndex];
	// Un punto tras una pared o al otro lado de un hueco no se alcanza: pasa al siguiente.
	const bool bMoved = MoveTowards(Target, PatrolSpeed, DeltaTime);

	if (!bMoved || FVector::Dist2D(GetActorLocation(), Target) < 50.f)
	{
		CurrentPatrolIndex = (CurrentPatrolIndex + 1) % WorldPatrolPoints.Num();
	}
}

// ── Chase ─────────────────────────────────────────────────────────────────────

void ATN_CrabActor::TickChase(float DeltaTime)
{
	ATortugaCharacter* Target = ChaseTarget.Get();

	// Decisión pura en TNCrabLogic (Fase 4.3) — el actor aporta distancias y
	// validez del target, y ejecuta la transición resultante.
	const bool  bAlive              = IsAliveAndValid(Target);
	const float DistTargetFromSpawn = bAlive ? FVector::Dist2D(Target->GetActorLocation(), SpawnLocation) : 0.f;
	float DistToTarget              = bAlive ? FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()) : 0.f;
	// A otra altura o tras una pared no ataca aunque esté cerca: sigue persiguiendo.
	if (bAlive && DistToTarget <= AttackRadius && !CanReach(Target))
	{
		DistToTarget = AttackRadius + 1.f;
	}

	using TNCrabLogic::EChaseTransition;
	switch (TNCrabLogic::DecideChaseTransition(bAlive, DistTargetFromSpawn, MaxChaseDistance,
		DistToTarget, AttackRadius))
	{
	case EChaseTransition::ReturnToPatrol_TargetLost:
		ChaseTarget.Reset();
		SetCrabState(ETNCrabState::Patrol);
		return;

	case EChaseTransition::ReturnToPatrol_OutOfZone:
		// Abandonar si el jugador salió de la zona delimitada del cangrejo
		ChaseTarget.Reset();
		CurrentPatrolIndex = FindNearestPatrolIndex();
		SetCrabState(ETNCrabState::Patrol);
		return;

	case EChaseTransition::StartAttack:
		SetCrabState(ETNCrabState::Attack);
		return;

	case EChaseTransition::KeepChasing:
		MoveTowards(Target->GetActorLocation(), ChaseSpeed, DeltaTime);
		return;
	}
}

// ── Attack ───────────────────────────────────────────────────────────────────

void ATN_CrabActor::TickAttack()
{
	ATortugaCharacter* Target = ChaseTarget.Get();
	if (IsAliveAndValid(Target))
	{
		Target->RequestKill(this);
	}

	CooldownRemaining = CooldownDuration;
	ChaseTarget.Reset();
	SetCrabState(ETNCrabState::Cooldown);
}

// ── Cooldown ─────────────────────────────────────────────────────────────────

void ATN_CrabActor::TickCooldown(float DeltaTime)
{
	CooldownRemaining -= DeltaTime;
	if (CooldownRemaining <= 0.f)
	{
		CurrentPatrolIndex = FindNearestPatrolIndex();
		SetCrabState(ETNCrabState::Patrol);
	}
}

// ── Helpers ──────────────────────────────────────────────────────────────────

bool ATN_CrabActor::MoveTowards(const FVector& Target, float Speed, float DeltaTime)
{
	const FVector CurrentLoc = GetActorLocation();
	const FVector Dir        = (Target - CurrentLoc).GetSafeNormal2D();
	const float   Step       = Speed * DeltaTime;
	const float   Dist       = FVector::Dist2D(CurrentLoc, Target);
	if (Dir.IsNearlyZero())
	{
		return true;
	}
	SetActorRotation(Dir.Rotation());

	FVector Next;
	if (!TryStep(CurrentLoc, Dir * FMath::Min(Step, Dist), Next))
	{
		return false;
	}
	SetActorLocation(Next);
	return true;
}

namespace
{
	/** Suelo y paredes del cangrejo: lo estático y lo dinámico del mundo (no las tortugas ni otros cangrejos). */
	FCollisionObjectQueryParams CrabWorldObjects()
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Objects;
	}
}

bool ATN_CrabActor::FindFloor(const FVector& At, float Up, float Down, float& OutZ) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TNCrabFloor), false, this);
	FHitResult Hit;
	if (World->LineTraceSingleByObjectType(Hit, At + FVector(0.f, 0.f, Up), At - FVector(0.f, 0.f, Down), CrabWorldObjects(), Query)
		&& !Hit.bStartPenetrating && Hit.ImpactNormal.Z > 0.6f)
	{
		OutZ = Hit.ImpactPoint.Z;
		return true;
	}
	return false;
}

bool ATN_CrabActor::TryStep(const FVector& From, const FVector& Delta, FVector& Out) const
{
	const UWorld* World = GetWorld();
	if (!World || Delta.IsNearlyZero())
	{
		Out = From;
		return World != nullptr;
	}
	// La pared se busca por encima de lo que sube de un paso: un escalón bajo no para, un muro sí.
	const FVector Lift(0.f, 0.f, MaxStepHeight + WallProbeRadius);
	const FCollisionShape Probe = FCollisionShape::MakeSphere(WallProbeRadius);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TNCrabWall), false, this);
	FVector Move = Delta;
	FHitResult Hit;
	if (World->SweepSingleByObjectType(Hit, From + Lift, From + Lift + Move, FQuat::Identity, CrabWorldObjects(), Probe, Query))
	{
		// Desliza a lo largo de la pared con lo que quede de paso; si tampoco cabe, no se mueve.
		const FVector Normal2D = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
		Move = FVector::VectorPlaneProject(Move, Normal2D) * (1.f - Hit.Time);
		if (Move.Size2D() < 1.f
			|| World->SweepSingleByObjectType(Hit, From + Lift, From + Lift + Move, FQuat::Identity, CrabWorldObjects(), Probe, Query))
		{
			return false;
		}
	}
	// Suelo en el destino: ni hueco ni cortado (más de MaxDropHeight hacia abajo) ni rampa demasiado empinada.
	const FVector Candidate = From + FVector(Move.X, Move.Y, 0.f);
	float FloorZ = 0.f;
	if (!FindFloor(Candidate, MaxStepHeight, MaxDropHeight, FloorZ))
	{
		return false;
	}
	Out = FVector(Candidate.X, Candidate.Y, FloorZ);
	return true;
}

bool ATN_CrabActor::CanReach(const ATortugaCharacter* Char) const
{
	const UWorld* World = GetWorld();
	if (!World || !Char)
	{
		return false;
	}
	// Patas con patas: el centro de la tortuga está media cápsula por encima del suelo.
	const float CharFeetZ = Char->GetActorLocation().Z - Char->GetSimpleCollisionHalfHeight();
	if (FMath::Abs(CharFeetZ - GetActorLocation().Z) > AttackHeight)
	{
		return false;
	}
	// Sin pared en medio, a la altura del caparazón.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TNCrabReach), false, this);
	Query.AddIgnoredActor(Char);
	FHitResult Hit;
	const FVector Up(0.f, 0.f, 25.f);
	return !World->LineTraceSingleByObjectType(Hit, GetActorLocation() + Up, FVector(Char->GetActorLocation().X, Char->GetActorLocation().Y, CharFeetZ) + Up,
		FCollisionObjectQueryParams(ECC_WorldStatic), Query);
}

int32 ATN_CrabActor::FindNearestPatrolIndex() const
{
	int32 Best = 0;
	float BestDist = MAX_FLT;
	for (int32 i = 0; i < WorldPatrolPoints.Num(); ++i)
	{
		const float D = FVector::Dist2D(GetActorLocation(), WorldPatrolPoints[i]);
		if (D < BestDist) { BestDist = D; Best = i; }
	}
	return Best;
}

bool ATN_CrabActor::IsAliveAndValid(ATortugaCharacter* Char) const
{
	if (!IsValid(Char)) { return false; }
	const ATN_CoopPlayerState* PS = Char->GetPlayerState<ATN_CoopPlayerState>();
	return PS && PS->IsAliveAndPlaying();
}

void ATN_CrabActor::SetCrabState(ETNCrabState NewState)
{
	if (CrabState == NewState) { return; }
	CrabState = NewState;
	OnRep_CrabState();
}

void ATN_CrabActor::OnRep_CrabState()
{
	// TN.Enemy.Debug: estado que VE el cliente (naranja) al llegar el OnRep,
	// para detectar desfases con el estado del servidor.
	if (TNDebug::EnemyDebug != 0 && !HasAuthority())
	{
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 150.f),
			FString::Printf(TEXT("CLI %s"), *UEnum::GetValueAsString(CrabState)),
			nullptr, FColor::Orange, 1.5f, true);
	}

	// Reproducir montage y sonido directamente — sin Blueprint
	UAnimInstance* AnimInst = CrabMesh ? CrabMesh->GetAnimInstance() : nullptr;

	UAnimMontage* Montage = nullptr;
	USoundBase*   Sound   = nullptr;

	switch (CrabState)
	{
	case ETNCrabState::Patrol:
		Montage = PatrolMontage;
		Sound   = PatrolSound;
		break;
	case ETNCrabState::Chase:
		Montage = ChaseMontage;
		Sound   = ChaseSound;
		break;
	case ETNCrabState::Attack:
		Montage = AttackMontage;
		Sound   = AttackSound;
		break;
	case ETNCrabState::Cooldown:
		Montage = CooldownMontage;
		Sound   = CooldownSound;
		break;
	}

	if (AnimInst && Montage)
	{
		AnimInst->Montage_Play(Montage);
	}
	if (Sound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, Sound, GetActorLocation());
	}
}

// ── Overlap de detección ──────────────────────────────────────────────────────

void ATN_CrabActor::OnDetectionBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	// No interrumpir si ya está en cooldown, persiguiendo, aturdido o cegado
	if (CrabState == ETNCrabState::Chase || CrabState == ETNCrabState::Attack) { return; }
	if (CooldownRemaining > 0.f) { return; }
	if (IsStunned() || IsBlinded()) { return; }

	ATortugaCharacter* Char = Cast<ATortugaCharacter>(OtherActor);
	if (!IsAliveAndValid(Char)) { return; }

	// Solo perseguir si el jugador está dentro de la zona del cangrejo
	const float DistFromSpawn = FVector::Dist2D(Char->GetActorLocation(), SpawnLocation);
	if (DistFromSpawn > MaxChaseDistance) { return; }

	ChaseTarget = Char;
	SetCrabState(ETNCrabState::Chase);
}

// ── ITN_EnemyTargetInterface ──────────────────────────────────────────────────

float ATN_CrabActor::GetStunRemaining() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GS ? TNCrabLogic::ComputeEffectRemaining(StunEndServerTime, GS->GetServerWorldTimeSeconds()) : 0.f;
}

float ATN_CrabActor::GetBlindRemaining() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GS ? TNCrabLogic::ComputeEffectRemaining(BlindEndServerTime, GS->GetServerWorldTimeSeconds()) : 0.f;
}

void ATN_CrabActor::ApplyStun(float Duration)
{
	if (!HasAuthority() || Duration <= 0.f) { return; }
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GS) { return; }
	// Max: un stun nuevo no acorta uno en curso más largo (TNCrabLogic, testeado).
	StunEndServerTime = TNCrabLogic::ExtendEffectEndTime(StunEndServerTime, GS->GetServerWorldTimeSeconds(), Duration);
	UE_LOG(LogTortunabo, Log, TEXT("[STUN] Crab '%s' stunned for %.2fs"), *GetName(), Duration);
	MulticastPlayStunEffect(Duration);
}

void ATN_CrabActor::ApplyBlind(float Duration)
{
	if (!HasAuthority() || Duration <= 0.f) { return; }
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GS) { return; }
	BlindEndServerTime = TNCrabLogic::ExtendEffectEndTime(BlindEndServerTime, GS->GetServerWorldTimeSeconds(), Duration);
	UE_LOG(LogTortunabo, Log, TEXT("[BLIND] Crab '%s' blinded for %.2fs"), *GetName(), Duration);
	if (CrabState == ETNCrabState::Chase || CrabState == ETNCrabState::Attack)
	{
		ChaseTarget.Reset();
		SetCrabState(ETNCrabState::Patrol);
	}
}

void ATN_CrabActor::MulticastPlayStunEffect_Implementation(float Duration)
{
	if (StunSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, StunSound, GetActorLocation());
	}
	if (StunVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, StunVFX, GetActorLocation());
	}
}
