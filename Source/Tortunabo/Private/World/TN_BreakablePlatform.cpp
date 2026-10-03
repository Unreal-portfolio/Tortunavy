#include "World/TN_BreakablePlatform.h"
#include "Core/TN_Log.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ATN_BreakablePlatform::ATN_BreakablePlatform()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;  // solo tickea durante el shake

	PlatformMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlatformMesh"));
	SetRootComponent(PlatformMesh);
	PlatformMesh->SetCollisionProfileName(TEXT("BlockAll"));
	PlatformMesh->Mobility = EComponentMobility::Movable;
	// Cubo de serie para la que se crea por código sin Blueprint (los puzles colocados del manifest, #652, la escalan a
	// losa); un Blueprint hijo pone su propia malla encima.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		PlatformMesh->SetStaticMesh(Cube.Object);
	}

	// Trigger delgado encima del mesh — detecta si alguien está sobre la plataforma.
	// Ajustar extensión en el BP hijo para que coincida con el mesh.
	StandTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("StandTrigger"));
	StandTrigger->SetupAttachment(PlatformMesh);
	StandTrigger->SetRelativeLocation(FVector(0.f, 0.f, 55.f));
	StandTrigger->SetBoxExtent(FVector(90.f, 90.f, 10.f));
	StandTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	StandTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	StandTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	bReplicates = true;
	bAlwaysRelevant = true;
	// La oscilación se calcula localmente en cada máquina — evitar que el server
	// envíe snapshots de posición durante el shake (introduciría jitter en clientes).
	SetReplicateMovement(false);
}

void ATN_BreakablePlatform::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BreakablePlatform, bBroken);
}

// ─────────────────────────────────────────────────────────────────────────────
// Overlap — solo el servidor gestiona el timer de rotura
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BreakablePlatform::OnStandTriggerBeginOverlap(UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority() || bBroken) { return; }

	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn) { return; }

	PawnsOnPlatform.Remove(nullptr);
	PawnsOnPlatform.Add(Pawn);

	UE_LOG(LogTortunabo, Verbose, TEXT("[BreakablePlatform] %s ENTER by %s — count=%d threshold=%d"),
		*GetNameSafe(this), *GetNameSafe(Pawn), PawnsOnPlatform.Num(), PlayerThreshold);

	if (PawnsOnPlatform.Num() >= PlayerThreshold && !GetWorldTimerManager().IsTimerActive(BreakTimerHandle))
	{
		UE_LOG(LogTortunabo, Log, TEXT("[BreakablePlatform] %s threshold reached — break in %.1fs"),
			*GetNameSafe(this), TimeToBreak);

		// La vibración arranca ShakeDuration segundos antes del break
		const float ShakeAt = FMath::Max(0.f, TimeToBreak - ShakeDuration);
		FTimerDelegate ShakeDelegate;
		ShakeDelegate.BindUObject(this, &ATN_BreakablePlatform::FireShake);
		GetWorldTimerManager().SetTimer(ShakeTimerHandle, ShakeDelegate, ShakeAt, false);

		FTimerDelegate BreakDelegate;
		BreakDelegate.BindUObject(this, &ATN_BreakablePlatform::BreakPlatform);
		GetWorldTimerManager().SetTimer(BreakTimerHandle, BreakDelegate, TimeToBreak, false);
	}
}

void ATN_BreakablePlatform::OnStandTriggerEndOverlap(UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!HasAuthority()) { return; }

	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn) { return; }

	// Durante la vibración, el StandTrigger se mueve con el mesh (±ShakeAmplitude)
	// y puede "soltar" momentáneamente los pies del jugador → EndOverlap espurio
	// cada ~1/ShakeFrequencyHz segundos. Sin este guard, los timers se cancelan,
	// el shake se detiene, el BeginOverlap re-arma todo desde cero y el puente
	// nunca llega a romperse (loop infinito de shake-cancel-shake).
	// Los EndOverlap reales mientras vibra son raros: este guard los bloquea a
	// todos, reales o espurios. Una vez arranca el shake el break es
	// irreversible — no hay re-evaluación posterior en BreakPlatform.
	if (bIsShaking) { return; }

	PawnsOnPlatform.Remove(Pawn);
	PawnsOnPlatform.Remove(nullptr);

	UE_LOG(LogTortunabo, Verbose, TEXT("[BreakablePlatform] %s EXIT by %s — count=%d threshold=%d mode=%d"),
		*GetNameSafe(this), *GetNameSafe(Pawn), PawnsOnPlatform.Num(), PlayerThreshold,
		static_cast<int32>(BreakMode));

	// Modo Latched: si el timer de rotura ya está corriendo (threshold alcanzado
	// en algún momento), el puente caerá sí o sí — ignorar la bajada de count.
	if (BreakMode == EBreakablePlatformMode::Latched &&
		GetWorldTimerManager().IsTimerActive(BreakTimerHandle))
	{
		return;
	}

	if (PawnsOnPlatform.Num() < PlayerThreshold)
	{
		GetWorldTimerManager().ClearTimer(ShakeTimerHandle);
		GetWorldTimerManager().ClearTimer(BreakTimerHandle);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Rotura y respawn — solo servidor
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BreakablePlatform::BreakPlatform()
{
	bBroken = true;
	StopShakeAndResetPosition();
	ApplyBrokenState();

	if (BreakSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, BreakSound, GetActorLocation());
	}
	if (BreakVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BreakVFX, GetActorLocation());
	}

	if (RespawnTime > 0.f)
	{
		FTimerDelegate RespawnDelegate;
		RespawnDelegate.BindUObject(this, &ATN_BreakablePlatform::RespawnPlatform);
		GetWorldTimerManager().SetTimer(RespawnTimerHandle, RespawnDelegate, RespawnTime, false);
		UE_LOG(LogTortunabo, Log, TEXT("[BreakablePlatform] %s broke — respawn en %.1fs"),
			*GetNameSafe(this), RespawnTime);
	}
	else
	{
		// Si el BP dejó RespawnTime=0 y el diseñador esperaba respawn, lo avisamos
		UE_LOG(LogTortunabo, Warning, TEXT("[BreakablePlatform] %s broke con RespawnTime=0 — "
			"no reaparecerá. Ajustar en BP si se esperaba respawn."), *GetNameSafe(this));
	}
}

void ATN_BreakablePlatform::RespawnPlatform()
{
	bBroken = false;
	PawnsOnPlatform.Empty();

	PlatformMesh->SetVisibility(true);
	PlatformMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	StandTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	if (RespawnSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, RespawnSound, GetActorLocation());
	}

	// Re-detectar pawns que sigan sobre la plataforma al respawnear. Si la
	// plataforma reaparece bajo un jugador que NO salió del volumen, UE no
	// emite BeginOverlap (ya estaba dentro) → el threshold nunca volvería
	// a dispararse y el puente se volvería "invencible" para ese player.
	TArray<AActor*> OverlappingActors;
	StandTrigger->GetOverlappingActors(OverlappingActors, APawn::StaticClass());
	for (AActor* A : OverlappingActors)
	{
		if (APawn* P = Cast<APawn>(A))
		{
			PawnsOnPlatform.Add(P);
		}
	}

	if (PawnsOnPlatform.Num() >= PlayerThreshold && !GetWorldTimerManager().IsTimerActive(BreakTimerHandle))
	{
		const float ShakeAt = FMath::Max(0.f, TimeToBreak - ShakeDuration);
		FTimerDelegate ShakeDelegate;
		ShakeDelegate.BindUObject(this, &ATN_BreakablePlatform::FireShake);
		GetWorldTimerManager().SetTimer(ShakeTimerHandle, ShakeDelegate, ShakeAt, false);

		FTimerDelegate BreakDelegate;
		BreakDelegate.BindUObject(this, &ATN_BreakablePlatform::BreakPlatform);
		GetWorldTimerManager().SetTimer(BreakTimerHandle, BreakDelegate, TimeToBreak, false);
	}

	UE_LOG(LogTortunabo, Log, TEXT("[BreakablePlatform] %s respawned — count=%d threshold=%d"),
		*GetNameSafe(this), PawnsOnPlatform.Num(), PlayerThreshold);
}

// ─────────────────────────────────────────────────────────────────────────────
// Estado replicado — clientes aplican vía OnRep
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BreakablePlatform::OnRep_bBroken()
{
	if (bBroken)
	{
		StopShakeAndResetPosition();
		ApplyBrokenState();
		if (BreakSound)
		{
			UGameplayStatics::SpawnSoundAtLocation(this, BreakSound, GetActorLocation());
		}
		if (BreakVFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BreakVFX, GetActorLocation());
		}
	}
	else
	{
		PlatformMesh->SetVisibility(true);
		PlatformMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		StandTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		if (RespawnSound)
		{
			UGameplayStatics::SpawnSoundAtLocation(this, RespawnSound, GetActorLocation());
		}
	}
}

void ATN_BreakablePlatform::ApplyBrokenState()
{
	PlatformMesh->SetVisibility(false);
	PlatformMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StandTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

// ─────────────────────────────────────────────────────────────────────────────
// Vibración (Reliable — es señal de gameplay, no cosmético puro)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BreakablePlatform::FireShake()
{
	if (!HasAuthority()) { return; }
	PawnsOnPlatform.Remove(nullptr);
	MulticastShake(PawnsOnPlatform.Num());
}

void ATN_BreakablePlatform::MulticastShake_Implementation(int32 PlayerCount)
{
	if (ShakeSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, ShakeSound, GetActorLocation());
	}
	if (ShakeVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ShakeVFX, GetActorLocation());
	}

	StartShake(PlayerCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// Oscilación visual — ejecutada localmente en cada máquina mientras bIsShaking
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BreakablePlatform::StartShake(int32 PlayerCount)
{
	bIsShaking = true;
	ShakeElapsedSeconds = 0.f;
	OriginalRootLocation = GetActorLocation();
	EffectiveShakeAmplitude = ResolveShakeAmplitude(PlayerCount);
	SetActorTickEnabled(true);
}

float ATN_BreakablePlatform::ResolveShakeAmplitude(int32 PlayerCount) const
{
	if (!bScaleShakeByPlayerCount || ShakeAmplitudeByPlayerCount.Num() == 0)
	{
		return ShakeAmplitude;
	}
	const int32 Idx = FMath::Clamp(PlayerCount - 1, 0, ShakeAmplitudeByPlayerCount.Num() - 1);
	return ShakeAmplitudeByPlayerCount[Idx];
}

void ATN_BreakablePlatform::StopShakeAndResetPosition()
{
	if (!bIsShaking) { return; }
	bIsShaking = false;
	SetActorTickEnabled(false);
	// Volver a posición original para evitar deriva si la oscilación terminó fuera de 0
	SetActorLocation(OriginalRootLocation);
}

void ATN_BreakablePlatform::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bIsShaking) { return; }

	ShakeElapsedSeconds += DeltaTime;
	const float OffsetZ = FMath::Sin(ShakeElapsedSeconds * ShakeFrequencyHz * 2.f * PI) * EffectiveShakeAmplitude;
	SetActorLocation(OriginalRootLocation + FVector(0.f, 0.f, OffsetZ));
}

// ─────────────────────────────────────────────────────────────────────────────
// Registrar overlaps en BeginPlay
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BreakablePlatform::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		StandTrigger->OnComponentBeginOverlap.AddDynamic(
			this, &ATN_BreakablePlatform::OnStandTriggerBeginOverlap);
		StandTrigger->OnComponentEndOverlap.AddDynamic(
			this, &ATN_BreakablePlatform::OnStandTriggerEndOverlap);
	}
}

void ATN_BreakablePlatform::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ShakeTimerHandle);
	GetWorldTimerManager().ClearTimer(BreakTimerHandle);
	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);
	Super::EndPlay(EndPlayReason);
}
