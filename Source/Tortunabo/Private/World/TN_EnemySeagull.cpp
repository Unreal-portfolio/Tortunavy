#include "World/TN_EnemySeagull.h"
#include "Core/TN_Log.h"
#include "Player/TortugaCharacter.h"
#include "Core/TN_CoopPlayerState.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Core/TN_DebugCVars.h"
#include "World/TN_EnemyDecisions.h"
#include "DrawDebugHelpers.h"
#include "World/TN_PlaceholderArt.h"
#include "World/Beach/TN_BeachEnemyKit.h"
#include "World/ProcMap/TN_ProcMapFaunaMeshes.h"

namespace TNEnemySeagullArt
{
	/** Por debajo de esta velocidad horizontal (cm/s) la gaviota no cambia de rumbo: se cierne. */
	constexpr float MinHeadingSpeed = 40.f;
	/** Rapidez con la que gira hacia su rumbo (1/s). */
	constexpr float HeadingInterpSpeed = 4.f;
	/** Patas recogidas en vuelo y alas plegadas hacia atrás en el picado (grados). */
	constexpr float LegTuckDegrees = -60.f;
	constexpr float DiveWingSweepDegrees = 55.f;
	constexpr float DiveWingFoldDegrees = 12.f;
	/** Cabeceo del cuerpo en el picado (grados, morro abajo). */
	constexpr float DivePitchDegrees = -35.f;
}

ATN_EnemySeagull::ATN_EnemySeagull()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SeagullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SeagullMesh"));
	SeagullMesh->SetupAttachment(SceneRoot);
	SeagullMesh->SetIsReplicated(false);
	SeagullMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	DangerDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("DangerDecal"));
	DangerDecal->SetupAttachment(SceneRoot);
	// Apuntar hacia abajo para proyectar en el suelo
	DangerDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
	DangerDecal->DecalSize = FVector(DecalDepth, MaxDangerRadius, MaxDangerRadius);
}

TArray<TWeakObjectPtr<ATN_EnemySeagull>> ATN_EnemySeagull::Active;

const ATN_EnemySeagull* ATN_EnemySeagull::FindMarking(const UWorld* World, const APlayerState* Player)
{
	if (!Player) { return nullptr; }
	for (const TWeakObjectPtr<ATN_EnemySeagull>& Weak : Active)
	{
		const ATN_EnemySeagull* Seagull = Weak.Get();
		if (Seagull && Seagull->GetWorld() == World && Seagull->TargetPlayerState == Player
			&& !Seagull->bIsStriking && !Seagull->bIsRetreating && !Seagull->bAttackResolved)
		{
			return Seagull;
		}
	}
	return nullptr;
}

void ATN_EnemySeagull::BeginPlay()
{
	Super::BeginPlay();
	Active.AddUnique(this);
	BuildCodeArt();
	// Tick activo también en clientes: el decal se anima localmente derivando el
	// countdown de AttackStartServerTime (la lógica de juego sigue siendo server-only,
	// ver el early-branch de Tick).
}

void ATN_EnemySeagull::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Active.RemoveSingleSwap(this);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	TargetCharacter.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATN_EnemySeagull::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_EnemySeagull, AttackStartServerTime);
	DOREPLIFETIME(ATN_EnemySeagull, TargetPlayerState);
}

// ── API Pública ────────────────────────────────────────────────────────────────

void ATN_EnemySeagull::InitializeWithTarget(ATortugaCharacter* Target)
{
	if (!HasAuthority() || !Target) { return; }

	// Arrancar el countdown: timestamp del reloj sincronizado, replicado una vez.
	if (const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr)
	{
		AttackStartServerTime = GS->GetServerWorldTimeSeconds();
	}

	// Alinear tamaño de decal en frame 1 antes de cualquier otra lógica —
	// evita el pop visual entre el default del ctor y el primer tick.
	UpdateDecalSize();

	TargetCharacter   = Target;
	TargetPlayerState = Target->GetPlayerState();

	const FVector StartLoc = Target->GetActorLocation() + FVector(0.f, 0.f, FollowHeight);
	SetActorLocation(StartLoc);

	UE_LOG(LogTortunabo, Verbose, TEXT("[EnemySeagull] Initialized on '%s' — countdown %.1fs"),
		*GetNameSafe(Target), AttackTimerSeconds);
}

float ATN_EnemySeagull::ComputeCountdownRemaining() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GS) { return AttackTimerSeconds; }

	// Lógica pura en TNSeagullLogic (Fase 4.3) — testeada por Automation.
	return TNSeagullLogic::ComputeCountdownRemaining(
		AttackStartServerTime, GS->GetServerWorldTimeSeconds(), AttackTimerSeconds);
}

float ATN_EnemySeagull::GetCurrentDangerRadius() const
{
	return TNSeagullLogic::ComputeDangerRadius(
		ComputeCountdownRemaining(), AttackTimerSeconds, MinKillRadius, MaxDangerRadius);
}

// ── Tick principal ─────────────────────────────────────────────────────────────

void ATN_EnemySeagull::BuildCodeArt()
{
	if (GetNetMode() == NM_DedicatedServer || !TNPlaceholderArt::NeedsCodeArt(SeagullMesh))
	{
		return;
	}
	// La gaviota de la fauna (la de las zonas de gaviotas y la de la carrera): mismas piezas y misma caché de mallas.
	TArray<TNFauna::FTNFaunaPart> Parts;
	TNFauna::FTNFaunaRig Rig;
	TNFauna::TNFaunaBuildSpecies(TNFauna::ETNFaunaSpecies::Gull, Parts, Rig);
	CodeArtRoot = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
	CodeArtRoot->SetupAttachment(SceneRoot);
	CodeArtRoot->SetRelativeScale3D(FVector(CodeArtScale));
	CodeArtRoot->RegisterComponent();
	UStaticMeshComponent* Body = nullptr;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 PartIndex = 0; PartIndex < Parts.Num(); ++PartIndex)
		{
			const TNFauna::FTNFaunaPart& Part = Parts[PartIndex];
			const bool bIsBody = Part.Bone == TNFauna::ETNFaunaBone::Body;
			if ((Pass == 0) != bIsBody)
			{
				continue;
			}
			const TNProcMesh::FTNProcMeshBuffers& Buffers = Part.Mesh;
			UStaticMesh* PartMesh = TNBeachKit::CachedMesh(FString::Printf(TEXT("Beach.Gull.%d"), PartIndex), [&Buffers](TNProcMesh::FTNProcMeshBuffers& M) { M = Buffers; });
			USceneComponent* Parent = (bIsBody || !Body) ? CodeArtRoot.Get() : static_cast<USceneComponent*>(Body);
			UStaticMeshComponent* Comp = TNBeachKit::AddPart(this, Parent, PartMesh, Part.Pivot, true);
			Body = (bIsBody && !Body) ? Comp : Body;
			const int32 Index = CodeArtParts.Add(Comp);
			CodeArtPivots.Add(Part.Pivot);
			switch (Part.Bone)
			{
			case TNFauna::ETNFaunaBone::WingL: CodeArtWingLeft = Index; break;
			case TNFauna::ETNFaunaBone::WingR: CodeArtWingRight = Index; break;
			case TNFauna::ETNFaunaBone::LegBL: CodeArtLegLeft = Index; break;
			case TNFauna::ETNFaunaBone::LegBR: CodeArtLegRight = Index; break;
			default: break;
			}
		}
	}
	CodeArtLastLocation = GetActorLocation();
	TNPlaceholderArt::HidePlaceholders(this, true);
}

void ATN_EnemySeagull::PoseCodeArtPart(int32 Index, const FRotator& Rotation)
{
	if (CodeArtParts.IsValidIndex(Index) && CodeArtPivots.IsValidIndex(Index))
	{
		TNBeachKit::Pose(CodeArtParts[Index], CodeArtPivots[Index], Rotation);
	}
}

void ATN_EnemySeagull::AnimateCodeArt(float DeltaTime)
{
	using namespace TNEnemySeagullArt;
	if (!CodeArtRoot || DeltaTime <= 0.f)
	{
		return;
	}
	CodeArtClock += DeltaTime;
	const FVector Location = GetActorLocation();
	const FVector Velocity = (Location - CodeArtLastLocation) / DeltaTime;
	CodeArtLastLocation = Location;
	const bool bDiving = Velocity.Z < -CodeArtDiveSpeed;
	FRotator Heading = CodeArtRoot->GetRelativeRotation();
	if (Velocity.Size2D() > MinHeadingSpeed)
	{
		Heading.Yaw = FMath::FixedTurn(Heading.Yaw, static_cast<float>(Velocity.Rotation().Yaw), 360.f * HeadingInterpSpeed * DeltaTime);
	}
	Heading.Pitch = FMath::FInterpTo(Heading.Pitch, bDiving ? DivePitchDegrees : 0.f, DeltaTime, HeadingInterpSpeed * 2.f);
	CodeArtRoot->SetRelativeRotation(Heading);
	const float Flap = bDiving ? DiveWingFoldDegrees : CodeArtFlapDegrees * FMath::Sin(CodeArtClock * UE_TWO_PI * CodeArtFlapHz);
	const float Sweep = bDiving ? DiveWingSweepDegrees : 0.f;
	PoseCodeArtPart(CodeArtWingLeft, FRotator(0.f, -Sweep, Flap));
	PoseCodeArtPart(CodeArtWingRight, FRotator(0.f, Sweep, -Flap));
	PoseCodeArtPart(CodeArtLegLeft, FRotator(LegTuckDegrees, 0.f, 0.f));
	PoseCodeArtPart(CodeArtLegRight, FRotator(LegTuckDegrees, 0.f, 0.f));
}

void ATN_EnemySeagull::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	AnimateCodeArt(DeltaTime);

	if (!HasAuthority())
	{
		// Cliente: solo visual — el decal se deriva del timestamp replicado y se
		// anima localmente (suave, sin depender de updates de red).
		UpdateDecalSize();

		// TN.Enemy.Debug: círculo que VE el cliente (naranja) para compararlo
		// con el del servidor en PIE multi-ventana.
		if (TNDebug::EnemyDebug != 0)
		{
			DrawDebugCircle(GetWorld(),
				GetActorLocation() - FVector(0.f, 0.f, FollowHeight - 10.f),
				GetCurrentDangerRadius(), 32, FColor::Orange, false, -1.f, 0, 2.f,
				FVector(1, 0, 0), FVector(0, 1, 0), false);
		}
		return;
	}

	// TN.Enemy.Debug: círculo real del servidor + estado (no-op en Shipping)
	if (TNDebug::EnemyDebug != 0)
	{
		const ATortugaCharacter* T = TargetCharacter.Get();
		const float GroundZ = T ? T->GetActorLocation().Z : GetActorLocation().Z - FollowHeight;
		const FColor Color = bIsStriking ? FColor::Red
		                   : bIsRetreating ? FColor::Cyan : FColor::Green;
		DrawDebugCircle(GetWorld(),
			FVector(GetActorLocation().X, GetActorLocation().Y, GroundZ + 5.f),
			GetCurrentDangerRadius(), 32, Color, false, -1.f, 0, 3.f,
			FVector(1, 0, 0), FVector(0, 1, 0), false);
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 80.f),
			FString::Printf(TEXT("SRV %.1fs fuera=%.1fs %s"),
				ComputeCountdownRemaining(), TimeOutsideShadow,
				bIsStriking ? TEXT("STRIKE") : bIsRetreating ? TEXT("RETREAT") : TEXT("FOLLOW")),
			nullptr, Color, 0.f, true);
	}

	// Máquinas de estado tienen prioridad — early-out garantiza que solo una corre
	if (bIsStriking)
	{
		TickStrike(DeltaTime);
		return;
	}
	if (bIsRetreating)
	{
		TickRetreat(DeltaTime);
		return;
	}
	if (bAttackResolved) { return; }

	TickFollowTarget(DeltaTime);
	TickEscapeCheck(DeltaTime);
	TickRoofCheck(DeltaTime);
	TickCountdown(DeltaTime);
}

// ── Sub-ticks del estado de seguimiento ───────────────────────────────────────

void ATN_EnemySeagull::TickFollowTarget(float DeltaTime)
{
	ATortugaCharacter* Target = TargetCharacter.Get();
	if (!Target) { return; }

	// Bobbing natural: 3 sines desfasadas (X/Y/Z) con frecuencias ligeramente
	// distintas para que el movimiento parezca orgánico (no un loop perfecto).
	// Solo se aplica al TargetLoc (no acumula sobre la posición previa).
	const float Now  = GetWorld()->GetTimeSeconds();
	const float BobZ = FMath::Sin(Now * IdleBobFrequency)             * IdleBobAmplitude;
	const float BobX = FMath::Sin(Now * IdleBobFrequency * 0.7f + 1.3f) * IdleBobAmplitude * 0.4f;
	const float BobY = FMath::Sin(Now * IdleBobFrequency * 0.9f + 2.1f) * IdleBobAmplitude * 0.4f;

	const FVector TargetLoc  = Target->GetActorLocation() + FVector(BobX, BobY, FollowHeight + BobZ);
	const FVector CurrentLoc = GetActorLocation();
	const float   Dist       = FVector::Dist(CurrentLoc, TargetLoc);
	if (Dist > 1.f)
	{
		const FVector Dir = (TargetLoc - CurrentLoc).GetSafeNormal();
		SetActorLocation(CurrentLoc + Dir * FMath::Min(FollowSpeed * DeltaTime, Dist));
	}
}

void ATN_EnemySeagull::TickCountdown(float DeltaTime)
{
	if (bAttackResolved) { return; }

	UpdateDecalSize();

	if (ComputeCountdownRemaining() <= 0.f)
	{
		ResolveAttack();
	}
}

void ATN_EnemySeagull::TickEscapeCheck(float DeltaTime)
{
	if (bAttackResolved) { return; }

	// TargetPlayerState null → InitializeWithTarget aún no fue llamado
	if (!TargetPlayerState) { return; }

	ATortugaCharacter* Target = TargetCharacter.Get();
	if (!Target)
	{
		AbortAndRetreat();
		return;
	}

	const float DistXY = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
	bool bAbort = false;
	TimeOutsideShadow = TNSeagullLogic::AdvanceEscapeTimer(
		DistXY, GetCurrentDangerRadius(), TimeOutsideShadow, DeltaTime, EscapeSeconds, bAbort);
	if (bAbort)
	{
		AbortAndRetreat();
	}
}

void ATN_EnemySeagull::TickRoofCheck(float DeltaTime)
{
	if (bAttackResolved) { return; }

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextRoofCheckTime) { return; }
	NextRoofCheckTime = Now + RoofCheckInterval;

	if (HasRoofBetweenSeagullAndTarget() || IsTargetUnderUmbrella())
	{
		AbortAndRetreat();
	}
}

// ── Máquina de estado: picotazo físico ────────────────────────────────────────

void ATN_EnemySeagull::TickStrike(float DeltaTime)
{
	if (bStrikeGoingDown)
	{
		StrikeAlpha += DeltaTime / FMath::Max(StrikeDuration, KINDA_SMALL_NUMBER);

		const float ClampedAlpha = FMath::Min(StrikeAlpha, 1.f);
		// SmoothStep da curva ease-in-out: arranque suave, aceleración media,
		// frenada al impactar — más natural que el Lerp lineal anterior.
		const float SmoothAlpha = FMath::SmoothStep(0.f, 1.f, ClampedAlpha);
		const float NewZ = FMath::Lerp(StrikeStartZ, StrikeTargetZ, SmoothAlpha);
		SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, NewZ));

		if (StrikeAlpha >= 1.f)
		{
			// Punto de impacto alcanzado — resolver daño
			ATortugaCharacter* Target = TargetCharacter.Get();
			MulticastPlayStrikeEffects(GetActorLocation());

			if (Target)
			{
				if (Target->HasUmbrellaProtection())
				{
					// Sombrilla bloquea el golpe; la gaviota retrocede sin matar
					MulticastPlayRetreatEffect();
				}
				else if (Target->HasBigHeadActive())
				{
					Target->RemoveBigHeadEffect();
					MulticastPlayBigHeadAbsorbEffect(Target);
				}
				else
				{
					MulticastPlayKillEffect(Target);
					Target->RequestKill(this);
				}
			}

			// Iniciar subida de vuelta
			bStrikeGoingDown = false;
			StrikeAlpha      = 0.f;
			RetreatStartZ    = StrikeTargetZ;
			RetreatEndZ      = StrikeStartZ + RetreatHeight;
		}
	}
	else
	{
		// Fase de subida tras el picotazo
		StrikeAlpha += DeltaTime / FMath::Max(RiseDuration, KINDA_SMALL_NUMBER);

		const float ClampedAlpha = FMath::Min(StrikeAlpha, 1.f);
		const float SmoothAlpha = FMath::SmoothStep(0.f, 1.f, ClampedAlpha);
		const float NewZ = FMath::Lerp(RetreatStartZ, RetreatEndZ, SmoothAlpha);
		SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, NewZ));

		if (StrikeAlpha >= 1.f)
		{
			bIsStriking = false;
			SetLifeSpan(0.2f); // Dar tiempo a que los Multicasts lleguen a clientes
		}
	}
}

// ── Máquina de estado: retirada ───────────────────────────────────────────────

void ATN_EnemySeagull::TickRetreat(float DeltaTime)
{
	StrikeAlpha += DeltaTime / FMath::Max(RiseDuration, KINDA_SMALL_NUMBER);

	const float ClampedAlpha = FMath::Min(StrikeAlpha, 1.f);
	const float SmoothAlpha = FMath::SmoothStep(0.f, 1.f, ClampedAlpha);
	const float NewZ = FMath::Lerp(RetreatStartZ, RetreatEndZ, SmoothAlpha);
	SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, NewZ));

	if (StrikeAlpha >= 1.f)
	{
		bIsRetreating = false;
		SetLifeSpan(0.2f);
	}
}

// ── Lógica de ataque / retirada ───────────────────────────────────────────────

void ATN_EnemySeagull::ResolveAttack()
{
	if (bAttackResolved) { return; }
	bAttackResolved = true;

	ATortugaCharacter* Target = TargetCharacter.Get();
	const float DistXY = Target
		? FVector::Dist2D(GetActorLocation(), Target->GetActorLocation())
		: 0.f;

	// Decisión pura en TNSeagullLogic (Fase 4.3) — el actor solo aporta los datos
	// del mundo (target, techo, distancia) y ejecuta el resultado.
	using TNSeagullLogic::EAttackDecision;
	const EAttackDecision Decision = TNSeagullLogic::DecideAttack(
		Target != nullptr, HasRoofBetweenSeagullAndTarget() || IsTargetUnderUmbrella(), DistXY, MinKillRadius);

	switch (Decision)
	{
	case EAttackDecision::Retreat_TargetLost:
		UE_LOG(LogTortunabo, Log, TEXT("[SEAGULL] ResolveAttack: target lost → retreat"));
		AbortAndRetreat();
		return;

	case EAttackDecision::Retreat_Roof:
		UE_LOG(LogTortunabo, Log, TEXT("[SEAGULL] ResolveAttack: target '%s' under roof → retreat"),
			*GetNameSafe(Target));
		AbortAndRetreat();
		return;

	case EAttackDecision::Retreat_Escaped:
		UE_LOG(LogTortunabo, Log, TEXT("[SEAGULL] ResolveAttack: target '%s' escaped (dist=%.0f > %.0f) → retreat"),
			*GetNameSafe(Target), DistXY, MinKillRadius);
		AbortAndRetreat();
		return;

	case EAttackDecision::Strike:
		UE_LOG(LogTortunabo, Log, TEXT("[SEAGULL] STRIKE on '%s' (dist=%.0f)"),
			*GetNameSafe(Target), DistXY);
		bIsStriking      = true;
		bStrikeGoingDown = true;
		StrikeAlpha      = 0.f;
		StrikeStartZ     = GetActorLocation().Z;
		StrikeTargetZ    = Target->GetActorLocation().Z;
		return;
	}
}

void ATN_EnemySeagull::AbortAndRetreat()
{
	if (bIsRetreating || bIsStriking) { return; }

	bIsRetreating   = true;
	bAttackResolved = true;
	StrikeAlpha     = 0.f;
	RetreatStartZ   = GetActorLocation().Z;
	RetreatEndZ     = RetreatStartZ + RetreatHeight;

	MulticastPlayRetreatEffect();
}

bool ATN_EnemySeagull::IsTargetUnderUmbrella() const
{
	// La sombrilla abierta cuenta como techo: la gaviota se retira al momento (#164).
	const ATortugaCharacter* Target = TargetCharacter.Get();
	return Target && Target->HasUmbrellaProtection();
}

bool ATN_EnemySeagull::HasRoofBetweenSeagullAndTarget() const
{
	ATortugaCharacter* Target = TargetCharacter.Get();
	if (!Target) { return false; }

	UWorld* World = GetWorld();
	if (!World) { return false; }

	FHitResult Hit;
	FCollisionQueryParams Params(TEXT("SeagullRoofCheck"), false);
	Params.AddIgnoredActor(this);
	Params.AddIgnoredActor(Target);

	// Trazar SOLO contra geometría estática del nivel (object type WorldStatic). Un techo
	// real es geometría estática; las cacas (NoCollision) y los physics objects
	// (WorldDynamic) quedan excluidos por su object type. Esto sustituye a las dos
	// iteraciones sobre TODOS los ATN_SeagullDroppingActor/ATN_PhysicsObjectActor del mundo
	// que construían una ignore-list en cada roof-check (0.25s por gaviota): pasa de
	// O(actores del mundo) a O(1) de setup, sin cambiar el resultado.
	FCollisionObjectQueryParams ObjParams(ECC_WorldStatic);
	World->LineTraceSingleByObjectType(
		Hit,
		GetActorLocation(),
		Target->GetActorLocation(),
		ObjParams,
		Params);

	return Hit.bBlockingHit;
}

// ── Visual ────────────────────────────────────────────────────────────────────

void ATN_EnemySeagull::UpdateDecalSize()
{
	if (!DangerDecal) { return; }
	const float R = GetCurrentDangerRadius();
	DangerDecal->DecalSize = FVector(DecalDepth, R, R);
}

// ── OnRep ─────────────────────────────────────────────────────────────────────

void ATN_EnemySeagull::OnRep_TargetPlayerState()
{
	TargetCharacter = TargetPlayerState
		? Cast<ATortugaCharacter>(TargetPlayerState->GetPawn())
		: nullptr;
}

// ── Multicast — efectos visuales/sonoros ──────────────────────────────────────

void ATN_EnemySeagull::MulticastPlayStrikeEffects_Implementation(FVector TargetLocation)
{
	if (StrikeVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, StrikeVFX, TargetLocation);
	}
	if (StrikeSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, StrikeSound, TargetLocation);
	}
}

void ATN_EnemySeagull::MulticastPlayRetreatEffect_Implementation()
{
	if (RetreatSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, RetreatSound, GetActorLocation());
	}
}

void ATN_EnemySeagull::MulticastPlayKillEffect_Implementation(ATortugaCharacter* Victim)
{
	if (!Victim) { return; }
	if (KillSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, KillSound, Victim->GetActorLocation());
	}
}

void ATN_EnemySeagull::MulticastPlayBigHeadAbsorbEffect_Implementation(ATortugaCharacter* Target)
{
	if (!Target) { return; }
	if (BigHeadAbsorbSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, BigHeadAbsorbSound, Target->GetActorLocation());
	}
}
