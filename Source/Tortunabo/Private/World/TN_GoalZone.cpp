#include "World/TN_GoalZone.h"
#include "Core/TN_Log.h"
#include "World/TN_PhysicsObjectActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

ATN_GoalZone::ATN_GoalZone()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	SetRootComponent(BaseMesh);
	BaseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	GoalBox = CreateDefaultSubobject<UBoxComponent>(TEXT("GoalBox"));
	GoalBox->SetupAttachment(BaseMesh);
	GoalBox->SetBoxExtent(FVector(150.f, 150.f, 150.f));
	// Trigger overlap-only. Responde a PhysicsBody (la bola simula físicas y su
	// ObjectType es PhysicsBody por defecto) y también a WorldDynamic por si
	// algún BP de bola usa otro canal.
	GoalBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GoalBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	GoalBox->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	GoalBox->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	GoalBox->SetGenerateOverlapEvents(true);

	ScoreWidget3D = CreateDefaultSubobject<UWidgetComponent>(TEXT("ScoreWidget3D"));
	ScoreWidget3D->SetupAttachment(BaseMesh);
	// Posición inicial 2m por encima del root — el BP la ajusta a la altura
	// real de la portería.
	ScoreWidget3D->SetRelativeLocation(FVector(0.f, 0.f, 200.f));
	ScoreWidget3D->SetWidgetSpace(EWidgetSpace::World);
	ScoreWidget3D->SetDrawSize(FVector2D(400.f, 200.f));
	ScoreWidget3D->SetTwoSided(true);
	ScoreWidget3D->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ATN_GoalZone::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		GoalBox->OnComponentBeginOverlap.AddDynamic(this, &ATN_GoalZone::OnBoxBeginOverlap);

		if (!BallSpawnTarget)
		{
			UE_LOG(LogTortunabo, Error,
				TEXT("[GoalZone] '%s' BallSpawnTarget NO asignado — los goles no respawnarán la bola. Asigna un Target Point en el editor."),
				*GetName());
		}
	}
}

void ATN_GoalZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_GoalZone, GoalCount);
	DOREPLIFETIME(ATN_GoalZone, bAllGoalsReached);
}

// ── Overlap ────────────────────────────────────────────────────────────────────

void ATN_GoalZone::OnBoxBeginOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!HasAuthority()) { return; }
	if (bAllGoalsReached && !bContinueScoringAfterGoal) { return; }

	ATN_PhysicsObjectActor* Ball = Cast<ATN_PhysicsObjectActor>(OtherActor);
	if (!Ball) { return; }

	// Filtro opcional por subclase (p.ej. solo BP_BouncePhysicsObject).
	if (AcceptedBallClass && !Ball->IsA(AcceptedBallClass)) { return; }

	// Cooldown anti-doble-conteo: la bola al teletransportarse puede generar
	// otro overlap inmediato si el spawn target queda dentro/cerca del trigger
	// — el ReentryCooldown protege contra eso.
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	if (Now - LastGoalServerTime < ReentryCooldown) { return; }

	if (!BallSpawnTarget)
	{
		UE_LOG(LogTortunabo, Warning,
			TEXT("[GoalZone] '%s' overlap con bola '%s' pero BallSpawnTarget es NULL → ignorado."),
			*GetName(), *Ball->GetName());
		return;
	}

	LastGoalServerTime = Now;
	++GoalCount;

	UE_LOG(LogTortunabo, Log,
		TEXT("[GoalZone] '%s' GOL · %s · %d/%d"),
		*GetName(), *Ball->GetName(), GoalCount, RequiredGoals);

	RespawnBall(Ball);
	MulticastOnGoalScored();

	if (!bAllGoalsReached && GoalCount >= RequiredGoals)
	{
		bAllGoalsReached = true;
		ApplyTargetTransforms();
		MulticastOnAllGoalsReached();
		OnZoneGoalReached.Broadcast(this);

		UE_LOG(LogTortunabo, Log,
			TEXT("[GoalZone] '%s' ALL GOALS REACHED (RequiredGoals=%d)"),
			*GetName(), RequiredGoals);
	}
}

// ── Server-auth respawn ───────────────────────────────────────────────────────

void ATN_GoalZone::RespawnBall(ATN_PhysicsObjectActor* Ball)
{
	if (!Ball || !BallSpawnTarget) { return; }

	const FTransform Target = BallSpawnTarget->GetActorTransform();
	Ball->SetActorLocationAndRotation(Target.GetLocation(), Target.GetRotation(),
		/*bSweep=*/false, /*OutSweepHitResult=*/nullptr,
		ETeleportType::TeleportPhysics);

	// Resetear velocidades — sin esto la bola conserva su momento del gol y
	// sale disparada del SpawnTarget. ETeleportType::TeleportPhysics hace casi
	// lo mismo, pero forzamos a 0 explícito para garantizar bola "depositada".
	if (UStaticMeshComponent* BallMesh = Ball->FindComponentByClass<UStaticMeshComponent>())
	{
		if (BallMesh->IsSimulatingPhysics())
		{
			BallMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
			BallMesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		}
	}

	// Despertar de dormancy: si la bola estaba dormida (DORM_DormantAll del
	// padre PhysicsObjectActor), sin Flush los clientes no recibirían la nueva
	// posición durante varios segundos.
	Ball->FlushNetDormancy();
	Ball->ForceNetUpdate();
}

// ── Activation ────────────────────────────────────────────────────────────────

void ATN_GoalZone::ApplyTargetTransforms()
{
	for (AActor* Target : TargetActorsOnGoalReached)
	{
		if (!Target) { continue; }
		Target->SetActorLocation(Target->GetActorLocation() + ActivationLocationOffset);
		Target->SetActorRotation(Target->GetActorRotation() + ActivationRotationOffset);
	}
}

// ── OnRep ─────────────────────────────────────────────────────────────────────

void ATN_GoalZone::OnRep_GoalCount()
{
	// El UMG bindea por GetGoalCount() — la replicación de la variable basta
	// para que el binding refresque el TextBlock automáticamente. Si necesitas
	// VFX/audio adicional cuando llega un gol, está en MulticastOnGoalScored.
}

void ATN_GoalZone::OnRep_AllGoalsReached()
{
	// Idem — el ApplyTargetTransforms ya se hizo en server, los TargetActors
	// son normalmente actores replicados (movimiento llega por bReplicateMovement).
}

// ── Multicast FX ──────────────────────────────────────────────────────────────

void ATN_GoalZone::MulticastOnGoalScored_Implementation()
{
	const FVector Loc = GetActorLocation();
	if (GoalScoredSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, GoalScoredSound, Loc);
	}
	if (GoalScoredVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, GoalScoredVFX, Loc);
	}
}

void ATN_GoalZone::MulticastOnAllGoalsReached_Implementation()
{
	const FVector Loc = GetActorLocation();
	if (AllGoalsReachedSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, AllGoalsReachedSound, Loc);
	}
	if (AllGoalsReachedVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, AllGoalsReachedVFX, Loc);
	}
}
