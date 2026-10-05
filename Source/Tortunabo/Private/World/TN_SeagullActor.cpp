#include "World/TN_SeagullActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Game/TN_RunGameMode.h"
#include "Core/TN_Log.h"
#include "UObject/ConstructorHelpers.h"

ATN_SeagullActor::ATN_SeagullActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;      // Visible para espectadores aunque estén lejos
	// No replicamos movimiento: cada máquina lo simula localmente.
	SetReplicateMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(Root);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	DangerZone = CreateDefaultSubobject<UCapsuleComponent>(TEXT("DangerZone"));
	DangerZone->SetupAttachment(Root);
	DangerZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DangerZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	DangerZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	SeagullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SeagullMesh"));
	SeagullMesh->SetupAttachment(Root);
	SeagullMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// BodyMesh: cono — silueta de pájaro que produce sombra en punta.
	// SeagullMesh (el "cuerpo volante"): cilindro — sombra circular.
	// BP hijo puede sobreescribir ambos con assets personalizados.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMeshAsset(
		TEXT("/Engine/BasicShapes/Cone"));
	if (ConeMeshAsset.Succeeded())
	{
		BodyMesh->SetStaticMesh(ConeMeshAsset.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMeshAsset(
		TEXT("/Engine/BasicShapes/Cylinder"));
	if (CylinderMeshAsset.Succeeded())
	{
		SeagullMesh->SetStaticMesh(CylinderMeshAsset.Object);
	}
}

void ATN_SeagullActor::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTortunabo, Warning,
		TEXT("ATN_SeagullActor está deprecado: sustituir %s en %s por ATN_SeagullSpawnZone."),
		*GetName(), *GetNameSafe(GetLevel() ? GetLevel()->GetOuter() : nullptr));

	// Compensar escala heredada del chunk padre.
	// SetCapsuleRadius/HalfHeight establece valores UNSCALED; si el actor tiene
	// escala 2x (heredada del chunk), la cápsula en mundo sería el doble.
	// Dividimos por la escala del actor para que el radio/semialtura mundo
	// coincida con los valores configurados en las UPROPERTYs.
	const FVector WorldScale = GetActorScale3D();
	const float ScaleXY = FMath::Max(FMath::Abs(WorldScale.X), 0.001f);
	const float ScaleZ  = FMath::Max(FMath::Abs(WorldScale.Z), 0.001f);
	DangerZone->SetCapsuleRadius(DangerCapsuleRadius / ScaleXY);
	DangerZone->SetCapsuleHalfHeight(DangerCapsuleHalfHeight / ScaleZ);

	// Posición de reposo del cubo
	SeagullMesh->SetRelativeLocation(FVector(0.f, 0.f, SeagullRestHeight));

	// Bind overlaps (solo servidor para hit detection)
	if (HasAuthority())
	{
		DangerZone->OnComponentBeginOverlap.AddDynamic(this, &ATN_SeagullActor::OnDangerBeginOverlap);
		DangerZone->OnComponentEndOverlap.AddDynamic(this, &ATN_SeagullActor::OnDangerEndOverlap);

		// Capturar posición de inicio (ahora correcta: el chunk ya está en su posición final).
		// Un tick de delay para garantizar que ChildActorComponent ha terminado de posicionar.
		// Usar FTimerDelegate bound a UObject (no lambda) para que EndPlay→
		// ClearAllTimersForObject(this) cancele el timer si el actor es destruido
		// (ej. chunk temporal del ChunkManager).
		GetWorldTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &ATN_SeagullActor::DeferredCaptureInitialLocation));
	}
}

void ATN_SeagullActor::DeferredCaptureInitialLocation()
{
	InitialLocation = GetActorLocation();
	bPositionSynced = true; // Servidor: posición correcta; clientes reciben vía OnRep_InitialLocation
}

void ATN_SeagullActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_SeagullActor, InitialLocation);
}

void ATN_SeagullActor::OnRep_InitialLocation()
{
	// El cliente recibió la posición inicial → sincronizar y arrancar movimiento local
	SetActorLocation(InitialLocation);
	bPositionSynced = true;
}


// ─────────────────────────────────────────────────────────────────────────────
// Tick — movimiento lateral (local en todas las máquinas) + animación de strike
// ─────────────────────────────────────────────────────────────────────────────

void ATN_SeagullActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bPositionSynced)
	{
		return;
	}

	// ── Movimiento lateral determinista ──────────────────────────────────────
	CurrentOffset += MoveSpeed * DeltaTime * MoveDirection;
	const float HalfRange = MoveRange * 0.5f;
	if (CurrentOffset >= HalfRange)
	{
		CurrentOffset = HalfRange;
		MoveDirection = -1.f;
	}
	else if (CurrentOffset <= -HalfRange)
	{
		CurrentOffset = -HalfRange;
		MoveDirection = 1.f;
	}

	const FVector NewLocation = InitialLocation + GetActorForwardVector() * CurrentOffset;
	SetActorLocation(NewLocation);

	// ── Animación del cubo (strike) ──────────────────────────────────────────
	if (bIsStriking)
	{
		const float Speed = bStrikeGoingDown
			? (1.f / FMath::Max(StrikeDuration, 0.01f))
			: (1.f / FMath::Max(RiseDuration, 0.01f));

		const float Delta = Speed * DeltaTime;

		if (bStrikeGoingDown)
		{
			StrikeAlpha = FMath::Min(StrikeAlpha + Delta, 1.f);
		}
		else
		{
			StrikeAlpha = FMath::Max(StrikeAlpha - Delta, 0.f);
		}

		// Interpolar entre posición de reposo y objetivo.
		// TargetZ: delta Z mundo (jugador - actor root) dividido por escala Z del actor.
		// SetRelativeLocation trabaja en espacio local (sin escalar); sin esta división
		// el strike sobrepasa/no alcanza al jugador cuando el chunk tiene escala ≠ 1.
		const float RestZ   = SeagullRestHeight;
		const float ScaleZ  = FMath::Max(GetActorScale3D().Z, 0.001f);
		const float TargetZ = (StrikeTargetZ - GetActorLocation().Z) / ScaleZ;
		const float CurrentZ = FMath::Lerp(RestZ, TargetZ, StrikeAlpha);
		SeagullMesh->SetRelativeLocation(FVector(0.f, 0.f, CurrentZ));

		// Cuando llega abajo, iniciar subida
		if (bStrikeGoingDown && StrikeAlpha >= 1.f)
		{
			bStrikeGoingDown = false;
		}
		// Cuando vuelve arriba, terminar animación
		else if (!bStrikeGoingDown && StrikeAlpha <= 0.f)
		{
			bIsStriking = false;
			SeagullMesh->SetRelativeLocation(FVector(0.f, 0.f, SeagullRestHeight));
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Danger Zone — Overlap (solo servidor)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_SeagullActor::OnDangerBeginOverlap(UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority() || !OtherActor) { return; }

	const APawn* P = Cast<APawn>(OtherActor);
	APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	if (!PC || PendingStrikeRemaining.Contains(PC)) { return; }

	PendingStrikeRemaining.Add(PC, DangerTimeToStrike);

	if (!GetWorldTimerManager().IsTimerActive(StrikeTickHandle))
	{
		GetWorldTimerManager().SetTimer(StrikeTickHandle, this,
			&ATN_SeagullActor::TickStrikeCountdowns, 0.1f, true);
	}
}

void ATN_SeagullActor::OnDangerEndOverlap(UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!HasAuthority() || !OtherActor) { return; }

	const APawn* P = Cast<APawn>(OtherActor);
	APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	if (!PC) { return; }

	PendingStrikeRemaining.Remove(PC);

	if (PendingStrikeRemaining.Num() == 0)
	{
		GetWorldTimerManager().ClearTimer(StrikeTickHandle);
	}
}

void ATN_SeagullActor::TickStrikeCountdowns()
{
	if (!HasAuthority()) { return; }

	TArray<TWeakObjectPtr<APlayerController>> Keys;
	PendingStrikeRemaining.GetKeys(Keys);

	for (const TWeakObjectPtr<APlayerController>& WeakPC : Keys)
	{
		APlayerController* PC = WeakPC.Get();
		if (!PC)
		{
			PendingStrikeRemaining.Remove(WeakPC);
			continue;
		}

		float* Remaining = PendingStrikeRemaining.Find(WeakPC);
		if (!Remaining) { continue; }

		*Remaining -= 0.1f;
		if (*Remaining <= KINDA_SMALL_NUMBER)
		{
			// Obtener posición del pawn como objetivo del strike
			FVector TargetLoc = GetActorLocation();
			if (APawn* Pawn = PC->GetPawn())
			{
				TargetLoc = Pawn->GetActorLocation();
			}

			// Resetear countdown para este jugador (puede picar de nuevo)
			PendingStrikeRemaining.Remove(WeakPC);

			// Emitir el strike visual a todos los clientes
			MulticastDoStrike(TargetLoc);

			// Matar al jugador (mismo efecto que zona de muerte)
			if (ATN_RunGameMode* RunGameMode = GetWorld()->GetAuthGameMode<ATN_RunGameMode>())
			{
				RunGameMode->MarkPlayerDeadBy(PC, ETNDeathCause::Seagull);
			}
		}
	}

	if (PendingStrikeRemaining.Num() == 0)
	{
		GetWorldTimerManager().ClearTimer(StrikeTickHandle);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// MulticastDoStrike — animación visual en todas las máquinas
// ─────────────────────────────────────────────────────────────────────────────

void ATN_SeagullActor::MulticastDoStrike_Implementation(FVector TargetWorldLocation)
{
	if (bIsStriking)
	{
		// Ignorar si ya hay un strike en curso
		return;
	}

	StrikeTargetZ    = TargetWorldLocation.Z;
	StrikeAlpha      = 0.f;
	bStrikeGoingDown = true;
	bIsStriking      = true;
}
