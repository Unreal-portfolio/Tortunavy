#include "World/TN_PhysicsObjectActor.h"
#include "Core/TN_Log.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Player/TortugaCharacter.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Sound/SoundBase.h"
#include "NiagaraSystem.h"
#include "GameFramework/CharacterMovementComponent.h"

ATN_PhysicsObjectActor::ATN_PhysicsObjectActor()
{
	// bCanEverTick DEBE configurarse en el ctor (el motor registra la TickFunction
	// en PostInitializeComponents, antes de BeginPlay → cambiarlo después NO surte
	// efecto). bStartWithTickEnabled=false mantiene el actor sin tickear hasta que
	// BeginPlay decida activarlo según el modo (kinematic-push o anti-phase-through).
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetSimulatePhysics(false);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));

	// CCD: evita el tunneling clásico cuando el jugador empuja el objeto
	// contra un muro estático y se mete dentro de la geometría.
	// Coste pequeño para actores puntuales — aceptable aquí.
	Mesh->SetUseCCD(true);

	// PushDetector: sphere overlap usada en modo kinematic-push para detectar
	// tortugas en contacto. En modo físico (BouncePhysicsObject) queda inerte.
	PushDetector = CreateDefaultSubobject<USphereComponent>(TEXT("PushDetector"));
	PushDetector->SetupAttachment(Mesh);
	PushDetector->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PushDetector->SetCollisionResponseToAllChannels(ECR_Ignore);
	PushDetector->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PushDetector->SetGenerateOverlapEvents(true);

	bReplicates = true;
	SetReplicatingMovement(true);

	// Actualizaciones para un objeto físico — 30Hz es suficiente para objetos
	// que rebotan (no son jugadores) y reduce ancho de banda a la mitad.
	SetNetUpdateFrequency(30.f);
	SetMinNetUpdateFrequency(10.f);

	// Empieza dormido — 0 bytes hasta que algo lo golpee.
	NetDormancy = DORM_DormantAll;
}

void ATN_PhysicsObjectActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DormancyCheckTimer);
	GetWorldTimerManager().ClearTimer(CrushCheckTimer);
	Super::EndPlay(EndPlayReason);
}

void ATN_PhysicsObjectActor::BeginPlay()
{
	Super::BeginPlay();

	// Locks de rotación — aplicar antes de que el cuerpo físico arranque para
	// que el constraint DOF se construya con los ejes ya fijados. CreateDOFLock
	// fuerza la reconstrucción si el body ya existía (seguridad ante reruns).
	if (Mesh)
	{
		Mesh->BodyInstance.bLockXRotation = bLockRotationX;
		Mesh->BodyInstance.bLockYRotation = bLockRotationY;
		Mesh->BodyInstance.bLockZRotation = bLockRotationZ;
		Mesh->BodyInstance.CreateDOFLock();
	}

	if (HasAuthority())
	{
		// Despertarse inmediatamente para que los clientes reciban la posición inicial.
		FlushNetDormancy();

		if (bUseKinematicPush)
		{
			// ── Modo kinematic-push ──────────────────────────────────────────
			// El cubo NO simula físicas. Se mueve por SetActorLocation desde
			// TickKinematicPush cuando una tortuga lo empuja. Sweep cancela
			// componente normal vs paredes. Server-auth puro → sin divergencia
			// entre máquinas, sin "tieso a veces".
			Mesh->SetSimulatePhysics(false);
			Mesh->SetMobility(EComponentMobility::Movable);

			// IMPORTANTE: kinematic-push requiere que el Mesh BLOQUEE Pawns para
			// que la tortuga lo "toque" físicamente y el PushDetector se active.
			// Sin esto el jugador atravesaría el cubo sin empujarlo.
			Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);

			// PushDetector: radio basado en bounds del componente (incluye
			// escala del actor). GetActorBounds en world space da el tamaño REAL.
			FVector BoundsOrigin = FVector::ZeroVector;
			FVector BoundsExtent = FVector::ZeroVector;
			GetActorBounds(/*bOnlyCollidingComponents=*/true, BoundsOrigin, BoundsExtent);
			const float MeshRadius = BoundsExtent.Size2D();

			// Mínimo 50cm para evitar que un mesh diminuto desactive el push.
			const float DetectorRadius = FMath::Max(MeshRadius + PushDetectionRadiusExtra, 50.f);
			PushDetector->SetSphereRadius(DetectorRadius);

			UE_LOG(LogTortunabo, Verbose, TEXT("[CUBE-PUSH] '%s' BeginPlay setup OK · bUseKinematicPush=true · BoundsExtent=%s · DetectorRadius=%.1f · ActorTickEnabled=Y"),
				*GetName(), *BoundsExtent.ToString(), DetectorRadius);

			// Delegate de overlap: sirve para diagnosticar si el detector NUNCA
			// recibe a la tortuga (radio insuficiente, collision matrix mal,
			// MeshBlock impide al jugador entrar al volumen del detector).
			PushDetector->OnComponentBeginOverlap.AddDynamic(this, &ATN_PhysicsObjectActor::OnPushDetectorBeginOverlap);

			SetActorTickEnabled(true);
		}
		else
		{
			UE_LOG(LogTortunabo, Verbose, TEXT("[CUBE-PUSH] '%s' BeginPlay · bUseKinematicPush=FALSE → fallback a SimulatePhysics tradicional"),
				*GetName());

			// ── Modo físico tradicional (BouncePhysicsObject hijo) ───────────
			Mesh->SetSimulatePhysics(true);
			Mesh->OnComponentHit.AddDynamic(this, &ATN_PhysicsObjectActor::OnMeshHit);

			// PushDetector inerte en modo físico
			PushDetector->SetCollisionEnabled(ECollisionEnabled::NoCollision);

			if (bEnableCrushDetection && CrushCheckInterval > 0.f)
			{
				GetWorldTimerManager().SetTimer(
					CrushCheckTimer, this,
					&ATN_PhysicsObjectActor::CheckForCrush,
					CrushCheckInterval, /*bLoop=*/true);
			}

			// Activar Tick solo si el BP activó anti-phase-through. Default off.
			if (bEnableAntiPhaseThrough)
			{
				SetActorTickEnabled(true);
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick — anti phase-through sweep (server only)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_PhysicsObjectActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!HasAuthority() || !Mesh) { return; }

	// Modo kinematic-push tiene su propio path; los demás siguen con anti-phase.
	if (bUseKinematicPush)
	{
		TickKinematicPush(DeltaTime);
		return;
	}

	if (!bEnableAntiPhaseThrough) { return; }

	// Sweep en dirección de la velocity 3D completa. Antes solo componente
	// horizontal — pero la bola con dash + caída tunelaba el suelo (Vel.Z
	// dominaba) y no se detectaba. Ahora cubrimos cualquier dirección.
	const FVector Vel = Mesh->GetPhysicsLinearVelocity();
	const float SpeedSq = Vel.SizeSquared();
	if (SpeedSq < AntiPhaseMinSpeed * AntiPhaseMinSpeed) { return; }

	const float Speed = FMath::Sqrt(SpeedSq);
	const FVector Dir = Vel / Speed;
	const FVector Origin = GetActorLocation();

	const float SweepRadius = FMath::Max(AntiPhaseSweepRadius, 1.f);
	const float SweepDist = Speed * DeltaTime * 2.f + AntiPhaseProbePadding;

	UWorld* World = GetWorld();
	if (!World) { return; }

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PhysicsObjectAntiPhase), /*bTraceComplex=*/false, this);
	const FCollisionShape Shape = FCollisionShape::MakeSphere(SweepRadius);

	const bool bHit = World->SweepSingleByChannel(
		Hit, Origin, Origin + Dir * SweepDist, FQuat::Identity,
		ECC_WorldStatic, Shape, Params);
	if (!bHit) { return; }

	// Ignorar contactos ya presentes (asentado contra pared/suelo). Sin esto
	// el sweep cancelaría velocity cada tick aunque vaya tangente.
	if (Hit.bStartPenetrating) { return; }

	// Cancelar la componente de velocity "into surface" — proyección escalar
	// sobre -Normal. Funciona igual contra paredes (slide horizontal) que contra
	// suelo (cancela componente Z de caída → bola "se va para un lado" en lugar
	// de atravesar). El componente tangencial queda intacto.
	const FVector Normal = Hit.ImpactNormal;
	const float IntoSurfaceSpeed = -FVector::DotProduct(Vel, Normal);
	if (IntoSurfaceSpeed > 0.f)
	{
		const FVector ClampedVel = Vel + Normal * IntoSurfaceSpeed;
		Mesh->SetPhysicsLinearVelocity(ClampedVel);
	}
}

void ATN_PhysicsObjectActor::OnPushDetectorBeginOverlap(UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	// Diagnóstico: este log dispara la primera vez que una tortuga entra en
	// el sphere del detector. Si NUNCA dispara aunque el jugador empuje el
	// cubo, el problema es que el Mesh (Block to Pawn) impide a la tortuga
	// llegar dentro del PushDetector — fix: ampliar PushDetectionRadiusExtra.
	if (OtherActor && OtherActor->IsA(ATortugaCharacter::StaticClass()))
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[CUBE-PUSH] '%s' DETECTOR overlap BEGIN with '%s' (centro a %.0f cm) authority=%d"),
			*GetName(), *OtherActor->GetName(),
			FVector::Dist(GetActorLocation(), OtherActor->GetActorLocation()),
			HasAuthority() ? 1 : 0);
	}
}

void ATN_PhysicsObjectActor::OnMeshHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// Despertar: el servidor empieza a enviar actualizaciones de posición.
	FlushNetDormancy();

	if (Mesh)
	{
		const FVector Vel = Mesh->GetComponentVelocity();
		if (Vel.SizeSquared() > MaxPushVelocity * MaxPushVelocity)
		{
			// Cap de seguridad para fuentes de impulso externas (bola lanzada, etc.)
			Mesh->SetPhysicsLinearVelocity(Vel.GetSafeNormal() * MaxPushVelocity);
		}
	}

	// Iniciar comprobación periódica para volver a dormir cuando pare.
	if (!GetWorldTimerManager().IsTimerActive(DormancyCheckTimer))
	{
		GetWorldTimerManager().SetTimer(
			DormancyCheckTimer, this,
			&ATN_PhysicsObjectActor::TryEnterDormancy,
			DormancyCheckInterval, /*bLoop=*/true);
	}
}

void ATN_PhysicsObjectActor::TryEnterDormancy()
{
	const float SpeedSq = Mesh->GetComponentVelocity().SizeSquared();
	if (SpeedSq > SleepVelocityThreshold * SleepVelocityThreshold)
	{
		return; // Aún en movimiento — seguir comprobando.
	}

	// Detenido: un último FlushNetDormancy antes de dormir para que el cliente
	// reciba la posición final exacta (si no, podría quedarse "a medio parar"
	// respecto al servidor cuando entra dormancy).
	FlushNetDormancy();
	SetNetDormancy(DORM_DormantAll);
	GetWorldTimerManager().ClearTimer(DormancyCheckTimer);
}

// ─────────────────────────────────────────────────────────────────────────────
// Crush detection — destruye el actor si queda atrapado entre geometría estática
// ─────────────────────────────────────────────────────────────────────────────

void ATN_PhysicsObjectActor::CheckForCrush()
{
	if (!Mesh || !HasAuthority()) { return; }

	// Lanzamos 3 pares de rays opuestos desde el centro del actor. Si un par
	// (por ejemplo +X y -X) golpea geometría estática dentro del bounds, ese eje
	// está "bloqueado". Con 2 o más ejes bloqueados el actor no tiene salida →
	// lo consideramos aplastado y lo destruimos con un poof.
	const FVector Origin = GetActorLocation();
	const float Radius = Mesh->Bounds.SphereRadius;
	const float CheckDist = Radius * 1.15f;

	static const FVector Dirs[3] = {
		FVector(1.f, 0.f, 0.f),
		FVector(0.f, 1.f, 0.f),
		FVector(0.f, 0.f, 1.f)
	};

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PhysicsObjectCrush), /*bTraceComplex=*/false, this);
	int32 BlockedAxes = 0;

	UWorld* World = GetWorld();
	if (!World) { return; }

	for (const FVector& Dir : Dirs)
	{
		FHitResult HitPos, HitNeg;
		const bool bPos = World->LineTraceSingleByChannel(HitPos, Origin, Origin + Dir * CheckDist, ECC_WorldStatic, Params);
		const bool bNeg = World->LineTraceSingleByChannel(HitNeg, Origin, Origin - Dir * CheckDist, ECC_WorldStatic, Params);
		if (bPos && bNeg) { ++BlockedAxes; }
	}

	if (BlockedAxes >= 2)
	{
		MulticastCrushPoof();
		// LifeSpan corto da tiempo al Multicast a replicarse antes del Destroy.
		SetLifeSpan(0.2f);
		GetWorldTimerManager().ClearTimer(CrushCheckTimer);
		GetWorldTimerManager().ClearTimer(DormancyCheckTimer);
	}
}

void ATN_PhysicsObjectActor::MulticastCrushPoof_Implementation()
{
	const FVector Loc = GetActorLocation();
	if (CrushPoofVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, CrushPoofVFX, Loc);
	}
	if (CrushPoofSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, CrushPoofSound, Loc);
	}
	// Ocultar mesh inmediatamente para que el poof sea el único visual.
	if (Mesh)
	{
		Mesh->SetVisibility(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// TickKinematicPush — empuje server-auth sin físicas reales
// ─────────────────────────────────────────────────────────────────────────────
//
// Diseño:
//   1. Detectar tortugas en overlap del PushDetector (sphere ligeramente más
//      grande que la collision del mesh).
//   2. Para cada una, calcular cuánto está empujando hacia el cubo:
//        push = max(0, dot(velocity, dir_a_cubo)) * factor
//   3. Sumar contribuciones de varias tortugas (multi-jugador empujando junto).
//   4. Sweep en la dirección resultante:
//        - Si impacta WorldStatic / WorldDynamic con normal vertical-ish,
//          cancelar componente de la velocidad en la normal (la pared "absorbe"
//          su parte). Slide tangencial libre.
//   5. SetActorLocation con sweep — server-auth, replica vía bReplicateMovement.
//
// Multiplayer: solo server hace cálculo + movement. Clientes interpolan posición
// recibida (sin SimulatePhysics → cero divergencia, cero "tieso ↔ móvil").
//
// ─────────────────────────────────────────────────────────────────────────────

void ATN_PhysicsObjectActor::TickKinematicPush(float DeltaTime)
{
	if (!PushDetector) { return; }

	// ── Gravedad simulada (siempre, antes del empuje) ──────────────────────
	// El cubo está en SimulatePhysics=false, no recibe gravedad nativa. Aplicamos
	// caída por ticks acumulando velocidad y haciendo SetActorLocation con sweep:
	// si el suelo bloquea, el sweep cancela el delta vertical y la velocidad
	// se resetea (snap a suelo).
	if (KinematicGravity > 0.f)
	{
		KinematicFallSpeed = FMath::Min(KinematicFallSpeed + KinematicGravity * DeltaTime, KinematicMaxFallSpeed);
		const FVector FallDelta(0.f, 0.f, -KinematicFallSpeed * DeltaTime);

		FHitResult FallHit;
		AddActorWorldOffset(FallDelta, /*bSweep=*/true, &FallHit);
		if (FallHit.bBlockingHit)
		{
			// Suelo (o cualquier blocker) → reset acumulador, el cubo se queda en pie.
			KinematicFallSpeed = 0.f;
		}
	}

	// 1. Detectar tortugas en overlap (server-side; Tick está guarded por
	//    HasAuthority arriba). Probamos también sin filtro de clase para
	//    diagnosticar si el filtro está fallando.
	TArray<AActor*> Overlapping;
	PushDetector->GetOverlappingActors(Overlapping, ATortugaCharacter::StaticClass());

	TArray<AActor*> AllOverlapping;
	PushDetector->GetOverlappingActors(AllOverlapping);

	// Log SIEMPRE (throttled 1s) — confirma si Tick ve overlap. Si delegate
	// dispara overlap BEGIN pero este log dice 0, hay desync entre el state
	// del delegate y GetOverlappingActors (autorización mismatch o el Mesh
	// Block to Pawn empuja al char fuera del sphere antes del siguiente Tick).
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now - LastPushLogTime > 1.0)
		{
			LastPushLogTime = Now;
			UE_LOG(LogTortunabo, Verbose, TEXT("[CUBE-PUSH] '%s' Tick auth=%d · overlap(Char)=%d overlap(All)=%d radius=%.0f"),
				*GetName(), HasAuthority() ? 1 : 0,
				Overlapping.Num(), AllOverlapping.Num(),
				PushDetector->GetScaledSphereRadius());
		}
	}

	if (Overlapping.Num() == 0)
	{
		return; // sin contacto, cubo estático
	}

	const FVector CubeLoc = GetActorLocation();
	FVector AccumulatedPush = FVector::ZeroVector;

	// 2. Sumar empuje de cada tortuga que INTENTA moverse hacia el cubo.
	// Fuente de "intención" del jugador (Codex review):
	// GetLastMovementInputVector() NO es fiable para clientes remotos server-side
	// (es input del pawn local consumido). El server lee la intent autoritativa
	// del CMC vía GetCurrentAcceleration() — survives "velocity=0 por collision
	// block". Fallback a LastMovementInputVector para listen-server local.
	for (AActor* A : Overlapping)
	{
		ATortugaCharacter* Char = Cast<ATortugaCharacter>(A);
		if (!Char) { continue; }

		UCharacterMovementComponent* CMC = Char->GetCharacterMovement();
		if (!CMC) { continue; }

		FVector Intent = CMC->GetCurrentAcceleration();
		Intent.Z = 0.f;

		float IntentMagnitude = 0.f;
		if (!Intent.IsNearlyZero())
		{
			const float MaxAccel = FMath::Max(CMC->GetMaxAcceleration(), 1.f);
			IntentMagnitude = FMath::Clamp(Intent.Size() / MaxAccel, 0.f, 1.f);
			Intent.Normalize();
		}
		else
		{
			// Fallback host/listen-server local owner.
			Intent = Char->GetLastMovementInputVector();
			Intent.Z = 0.f;
			IntentMagnitude = FMath::Clamp(Intent.Size(), 0.f, 1.f);
			if (!Intent.IsNearlyZero()) { Intent.Normalize(); }
		}

		FVector ToCube = CubeLoc - Char->GetActorLocation();
		ToCube.Z = 0.f;
		const bool bToCubeOK = ToCube.Normalize();
		const float Alignment = bToCubeOK ? FVector::DotProduct(Intent, ToCube) : 0.f;

		// Log diagnóstico throttled.
		{
			const double Now = GetWorld()->GetTimeSeconds();
			if (Now - LastZeroPushLogTime > 0.5)
			{
				LastZeroPushLogTime = Now;
				UE_LOG(LogTortunabo, Verbose, TEXT("[CUBE-PUSH-CALC] '%s' vs '%s' · Intent=%.2f Align=%.2f ToCubeOK=%d"),
					*GetName(), *Char->GetName(), IntentMagnitude, Alignment, bToCubeOK ? 1 : 0);
			}
		}

		if (IntentMagnitude < 0.05f) { continue; }
		if (!bToCubeOK)              { continue; }
		if (Alignment <= 0.05f)      { continue; }

		const float WalkSpeed = CMC->GetMaxSpeed();
		const float ProjectedSpeed = WalkSpeed * IntentMagnitude * Alignment * PushSpeedFactor;
		AccumulatedPush += ToCube * ProjectedSpeed;
	}

	if (AccumulatedPush.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		// Diagnóstico: hay tortugas en overlap pero ninguna empuja hacia el cubo.
		// Causas comunes: tortuga parada, alineación negativa, factor cero.
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now - LastZeroPushLogTime > 1.0)
		{
			LastZeroPushLogTime = Now;
			UE_LOG(LogTortunabo, Verbose, TEXT("[CUBE-PUSH] '%s' overlap pero AccumulatedPush=0 (tortuga no empuja hacia cubo)"),
				*GetName());
		}
		return;
	}

	UE_LOG(LogTortunabo, Verbose, TEXT("[CUBE-PUSH] '%s' applying push=%s mag=%.0f"),
		*GetName(), *AccumulatedPush.ToCompactString(), AccumulatedPush.Size());

	// 3. Cap de seguridad
	const float PushSpeed = AccumulatedPush.Size();
	if (PushSpeed > MaxKinematicPushSpeed)
	{
		AccumulatedPush = (AccumulatedPush / PushSpeed) * MaxKinematicPushSpeed;
	}

	// 4. Sweep contra paredes — cancelar componente normal
	const FVector StartLoc  = CubeLoc;
	FVector       Velocity  = AccumulatedPush;
	FVector       Delta     = Velocity * DeltaTime;
	FVector       EndLoc    = StartLoc + Delta;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(KinematicPushSweep), false, this);
	FHitResult Hit;
	const FQuat ActorQuat = GetActorQuat();

	// Usar bounding del mesh como shape del sweep
	FVector BoxExtent = FVector(50.f);
	if (Mesh && Mesh->GetStaticMesh())
	{
		BoxExtent = Mesh->GetStaticMesh()->GetBounds().BoxExtent * GetActorScale3D();
	}

	const bool bBlocked = GetWorld()->SweepSingleByChannel(
		Hit, StartLoc, EndLoc, ActorQuat,
		ECC_WorldStatic, FCollisionShape::MakeBox(BoxExtent), QueryParams);

	if (bBlocked && Hit.bBlockingHit)
	{
		// Cancelar componente de velocity en la dirección de la normal de la pared.
		// Equivale a la fuerza normal que una pared real aplicaría — el slide
		// tangencial queda intacto, lateral siempre libre.
		FVector Normal = Hit.Normal;
		Normal.Z = 0.f; // ignorar suelo/techo
		if (!Normal.IsNearlyZero())
		{
			Normal.Normalize();
			const float NormalComp = FVector::DotProduct(Velocity, Normal);
			if (NormalComp < 0.f)
			{
				Velocity -= Normal * NormalComp;
				Delta = Velocity * DeltaTime;
				EndLoc = StartLoc + Delta;
			}
		}
	}

	if (Delta.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		return; // tras cancelar componente normal no queda movimiento
	}

	// 5. Despertar replicación: el cubo arranca con DORM_DormantAll (0 bytes en
	//    reposo). En kinematic-push, SetActorLocation server-side no replica
	//    al cliente si el actor sigue dormido — cliente ve el cubo congelado.
	//    FlushNetDormancy ANTES del cambio (patrón Vorixo) garantiza que el
	//    movimiento llegue al cliente. Cuando deja de empujarse, TryEnterDormancy
	//    lo vuelve a dormir tras DormancyCheckInterval.
	FlushNetDormancy();

	// 6. Aplicar movimiento con sweep — bSweep=true respeta cualquier collider
	//    nuevo que aparezca en la trayectoria final.
	SetActorLocation(EndLoc, /*bSweep=*/true);
}
