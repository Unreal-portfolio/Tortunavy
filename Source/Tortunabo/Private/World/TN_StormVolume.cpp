#include "World/TN_StormVolume.h"
#include "World/TN_RunGameModeAccess.h"
#include "Core/TN_Log.h"
#include "Game/TN_RunGameMode.h"
#include "Core/TN_CoopPlayerState.h"
#include "Player/TortugaCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "NiagaraComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "World/Beach/TN_BeachShelterVolume.h"

ATN_StormVolume::ATN_StormVolume()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	// ─── TriggerBox ───────────────────────────────────────────────────────
	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	SetRootComponent(TriggerBox);
	TriggerBox->SetBoxExtent(FVector(2000.f, 2000.f, 1000.f));
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);
	TriggerBox->SetHiddenInGame(true);

#if WITH_EDITORONLY_DATA
	TriggerBox->ShapeColor = FColor(255, 100, 0);
	TriggerBox->SetLineThickness(2.f);
#endif

	// ─── StormVolumeMesh ─────────────────────────────────────────────────
	// Cubo de 100x100x100 UU — se escala en SyncVisualToBox para cubrir TriggerBox.
	StormVolumeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StormVolumeMesh"));
	StormVolumeMesh->SetupAttachment(TriggerBox);
	StormVolumeMesh->SetRelativeLocation(FVector::ZeroVector);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		StormVolumeMesh->SetStaticMesh(CubeMeshFinder.Object);
	}

	// Escala inicial: extensión 2000×2000×1000 → cubo 100 → escala 40×40×20
	StormVolumeMesh->SetRelativeScale3D(FVector(40.f, 40.f, 20.f));
	StormVolumeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StormVolumeMesh->SetCastShadow(false);
	// ⚠ Asignar material translúcido de dos caras en el Blueprint hijo.
	//    El material debería tener: opacidad media (0.3-0.5), backface culling off,
	//    noise/distorsión para dar sensación de humo denso.

	// ─── EdgeVFX ─────────────────────────────────────────────────────────
	EdgeVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("EdgeVFX"));
	EdgeVFX->SetupAttachment(TriggerBox);
	EdgeVFX->SetAutoActivate(true);
	// Posición inicial: en la cara +X local (GrowthDirection por defecto)
	EdgeVFX->SetRelativeLocation(FVector(2000.f, 0.f, 0.f));
	// ⚠ Asignar sistema Niagara de humo/polvo denso en el Blueprint hijo.
	//    Recomendado: NS_SandstormWall o similar. El componente se reposiciona
	//    automáticamente al crecer el volumen.

	// ─── InteriorVFX ─────────────────────────────────────────────────────
	InteriorVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("InteriorVFX"));
	InteriorVFX->SetupAttachment(TriggerBox);
	InteriorVFX->SetAutoActivate(true);
	InteriorVFX->SetRelativeLocation(FVector::ZeroVector);
	// ⚠ Asignar sistema Niagara de polvo/niebla ambiental en el Blueprint hijo.
	//    Debería tener box-shape spawn para cubrir el volumen. Pasar el parámetro
	//    "BoxSize" como FVector si el sistema lo soporta.

	// ─── InsidePostProcess ───────────────────────────────────────────────
	InsidePostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("InsidePostProcess"));
	InsidePostProcess->SetupAttachment(TriggerBox);
	InsidePostProcess->bUnbound = true;           // afecta toda la cámara local
	InsidePostProcess->BlendWeight = 1.0f;
	InsidePostProcess->Priority = 5.0f;
	InsidePostProcess->bEnabled = false;           // se activa sólo cuando el pawn local está dentro
	// ⚠ Configurar en el Blueprint hijo:
	//    - Color Grading → Shadow Tint naranja/marrón, Saturation reducida
	//    - Exponential Height Fog override con densidad alta
	//    - Vignette Intensity ~ 0.8
	//    - Chromatic Aberration ~ 0.5
	//    - Lens Flares desactivados / Bloom reducido
	//    - Depth of Field para reducir visibilidad a distancia
}

void ATN_StormVolume::BeginPlay()
{
	Super::BeginPlay();

	// AddUnique: BP_StormVolume y sus instancias en los niveles traen el enlace serializado de cuando se hacía en el
	// constructor; un AddDynamic lo duplicaba (ensure de ScriptDelegates.h y cada overlap contado dos veces).
	TriggerBox->OnComponentBeginOverlap.AddUniqueDynamic(this, &ATN_StormVolume::OnBoxBeginOverlap);
	TriggerBox->OnComponentEndOverlap.AddUniqueDynamic(this, &ATN_StormVolume::OnBoxEndOverlap);

	NormalizedGrowthDir = GrowthDirection.GetSafeNormal();
	if (NormalizedGrowthDir.IsNearlyZero())
	{
		NormalizedGrowthDir = FVector(1.0f, 0.0f, 0.0f);
	}

	if (HasAuthority())
	{
		ReplicatedBoxHalfExtent = TriggerBox->GetUnscaledBoxExtent();
	}

	SyncVisualToBox();
}

void ATN_StormVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (TriggerBox)
	{
		TriggerBox->OnComponentBeginOverlap.RemoveDynamic(this, &ATN_StormVolume::OnBoxBeginOverlap);
		TriggerBox->OnComponentEndOverlap.RemoveDynamic(this, &ATN_StormVolume::OnBoxEndOverlap);
	}
	GetWorldTimerManager().ClearTimer(SharedCountdownTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void ATN_StormVolume::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!HasAuthority() || !bGrowthEnabled) return;

	const float GrowThisFrame = GrowthSpeed * DeltaTime;

	// Incremento de extensión a lo largo del eje de crecimiento (en espacio local).
	// La pared trasera queda fija: el actor avanza 0.5× y la extensión crece 0.5×,
	// resultado: la cara frontal avanza GrowThisFrame completo.
	const FVector ExtentDelta = FVector(
		FMath::Abs(NormalizedGrowthDir.X),
		FMath::Abs(NormalizedGrowthDir.Y),
		FMath::Abs(NormalizedGrowthDir.Z)
	) * GrowThisFrame * 0.5f;

	const FVector NewExtent = TriggerBox->GetUnscaledBoxExtent() + ExtentDelta;
	TriggerBox->SetBoxExtent(NewExtent);
	ReplicatedBoxHalfExtent = NewExtent;

	// Mover el centro del actor en la dirección de crecimiento (espacio world).
	const FVector WorldGrowthDir = GetActorTransform().TransformVectorNoScale(NormalizedGrowthDir);
	AddActorWorldOffset(WorldGrowthDir * GrowThisFrame * 0.5f);

	SyncVisualToBox();
}

void ATN_StormVolume::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_StormVolume, ReplicatedBoxHalfExtent);
	DOREPLIFETIME(ATN_StormVolume, bGrowthEnabled);
}

void ATN_StormVolume::OnRep_BoxHalfExtent()
{
	if (!TriggerBox) return;
	TriggerBox->SetBoxExtent(ReplicatedBoxHalfExtent);
	SyncVisualToBox();
}

void ATN_StormVolume::SyncVisualToBox()
{
	if (!TriggerBox) return;

	const FVector Extent = TriggerBox->GetUnscaledBoxExtent();

	// Escalar el cubo (100×100×100 UU nativo) para cubrir la extensión completa.
	if (StormVolumeMesh)
	{
		StormVolumeMesh->SetRelativeScale3D(Extent * 2.0f / 100.0f);
	}

	// Repositionar EdgeVFX en el plano frontal de avance (espacio local de la caja).
	if (EdgeVFX)
	{
		const FVector EdgeLocalPos = FVector(
			NormalizedGrowthDir.X * Extent.X,
			NormalizedGrowthDir.Y * Extent.Y,
			NormalizedGrowthDir.Z * Extent.Z
		);
		EdgeVFX->SetRelativeLocation(EdgeLocalPos);
	}
}

// ─── Overlaps ─────────────────────────────────────────────────────────────────

void ATN_StormVolume::OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor) return;

	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn) return;

	// ── Post-process: activar para el jugador local en esta máquina ──────
	if (Pawn->IsLocallyControlled())
	{
		LocalPlayersInside++;
		if (InsidePostProcess && LocalPlayersInside > 0)
		{
			InsidePostProcess->bEnabled = true;
			InsidePostProcess->MarkRenderStateDirty();
		}
	}

	// ── Death countdown: sólo en el servidor ─────────────────────────────
	if (!HasAuthority()) return;

	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC) return;

	ATN_RunGameMode* RunGameMode = ResolveRunGameMode();
	if (!RunGameMode) return;

	if (bDestroyOnlyDuringRun && RunGameMode->GetMatchState() != MatchState::InProgress) return;
	if (PendingDeathRemaining.Contains(PC)) return;
	if (RunGameMode->IsPlayerReviveImmune(PC)) return;

	PendingDeathRemaining.Add(PC, SecondsInsideToDie);
	if (ATN_CoopPlayerState* TNPS = PC->GetPlayerState<ATN_CoopPlayerState>())
	{
		TNPS->DeathZoneTimeRemaining = SecondsInsideToDie;
	}

	if (!GetWorldTimerManager().IsTimerActive(SharedCountdownTimerHandle))
	{
		GetWorldTimerManager().SetTimer(SharedCountdownTimerHandle, this,
			&ATN_StormVolume::TickAllCountdowns, CountdownTickInterval, true);
	}
}

void ATN_StormVolume::OnBoxEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!OtherActor) return;

	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn) return;

	// ── Post-process: desactivar para el jugador local ───────────────────
	if (Pawn->IsLocallyControlled())
	{
		LocalPlayersInside = FMath::Max(0, LocalPlayersInside - 1);
		if (InsidePostProcess && LocalPlayersInside == 0)
		{
			InsidePostProcess->bEnabled = false;
			InsidePostProcess->MarkRenderStateDirty();
		}
	}

	// ── Death countdown: sólo en el servidor ─────────────────────────────
	if (!HasAuthority()) return;

	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC) return;

	PendingDeathRemaining.Remove(PC);
	if (ATN_CoopPlayerState* TNPS = PC->GetPlayerState<ATN_CoopPlayerState>())
	{
		TNPS->DeathZoneTimeRemaining = -1.f;
	}

	if (PendingDeathRemaining.Num() == 0)
	{
		GetWorldTimerManager().ClearTimer(SharedCountdownTimerHandle);
	}
}

// ─── Muerte ───────────────────────────────────────────────────────────────────

void ATN_StormVolume::ForceCheckPlayer(APlayerController* PC)
{
	if (!HasAuthority() || !PC || !TriggerBox) { return; }

	// Ya rastreado → no duplicar.
	if (PendingDeathRemaining.Contains(PC)) { return; }

	// Solo aplicar a jugadores vivos.
	ATN_CoopPlayerState* TNPS = PC->GetPlayerState<ATN_CoopPlayerState>();
	if (!TNPS || !TNPS->bIsAlive) { return; }

	// Restricción de fase de juego.
	if (bDestroyOnlyDuringRun)
	{
		ATN_RunGameMode* RunGameMode = ResolveRunGameMode();
		if (!RunGameMode || RunGameMode->GetMatchState() != MatchState::InProgress) { return; }
	}

	APawn* Pawn = PC->GetPawn();
	if (!Pawn) { return; }

	// Comprobación geométrica directa (no depende del estado de overlaps que
	// puede estar desactualizado tras togglear collision en revive).
	const FTransform BoxTransform = TriggerBox->GetComponentTransform();
	const FVector LocalPos = BoxTransform.InverseTransformPosition(Pawn->GetActorLocation());
	const FVector Extent = TriggerBox->GetUnscaledBoxExtent();

	const bool bInside = FMath::Abs(LocalPos.X) <= Extent.X
	                  && FMath::Abs(LocalPos.Y) <= Extent.Y
	                  && FMath::Abs(LocalPos.Z) <= Extent.Z;

	if (!bInside) { return; }

	PendingDeathRemaining.Add(PC, SecondsInsideToDie);
	TNPS->DeathZoneTimeRemaining = SecondsInsideToDie;

	if (!GetWorldTimerManager().IsTimerActive(SharedCountdownTimerHandle))
	{
		GetWorldTimerManager().SetTimer(SharedCountdownTimerHandle, this,
			&ATN_StormVolume::TickAllCountdowns, CountdownTickInterval, true);
	}

	UE_LOG(LogTortunabo, Log, TEXT("[Storm] ForceCheckPlayer: '%s' dentro de '%s' tras revive — countdown reiniciado (%.1fs)"),
		*GetNameSafe(PC), *GetName(), SecondsInsideToDie);
}

void ATN_StormVolume::HandlePlayerDeath(APlayerController* PC)
{
	if (!HasAuthority() || !PC) return;

	if (ATN_RunGameMode* RunGameMode = ResolveRunGameMode())
	{
		RunGameMode->MarkPlayerDeadBy(PC, ETNDeathCause::Storm);
	}

	PendingDeathRemaining.Remove(PC);
	if (ATN_CoopPlayerState* TNPS = PC->GetPlayerState<ATN_CoopPlayerState>())
	{
		TNPS->DeathZoneTimeRemaining = -1.f;
	}

	if (PendingDeathRemaining.Num() == 0)
	{
		GetWorldTimerManager().ClearTimer(SharedCountdownTimerHandle);
	}
}

void ATN_StormVolume::TickAllCountdowns()
{
	if (!HasAuthority()) return;

	TArray<TWeakObjectPtr<APlayerController>> Keys;
	PendingDeathRemaining.GetKeys(Keys);

	for (const TWeakObjectPtr<APlayerController>& WeakPC : Keys)
	{
		APlayerController* PC = WeakPC.Get();
		if (!PC)
		{
			PendingDeathRemaining.Remove(WeakPC);
			continue;
		}

		float* Remaining = PendingDeathRemaining.Find(WeakPC);
		if (!Remaining) continue;

		// Pausar mientras el jugador está noquedado (igual que DeathZoneVolume).
		if (const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(PC->GetPawn()))
		{
			if (Turtle->IsKnockedDown()) continue;
			// Dentro de un búnker (#689) la cuenta atrás se para: es refugio.
			if (ATN_BeachShelterVolume::IsSheltered(Turtle)) continue;
		}

		*Remaining = FMath::Max(0.f, *Remaining - CountdownTickInterval);
		if (ATN_CoopPlayerState* TNPS = PC->GetPlayerState<ATN_CoopPlayerState>())
		{
			TNPS->DeathZoneTimeRemaining = *Remaining;
		}

		if (*Remaining <= KINDA_SMALL_NUMBER)
		{
			HandlePlayerDeath(PC);
		}
	}

	if (PendingDeathRemaining.Num() == 0)
	{
		GetWorldTimerManager().ClearTimer(SharedCountdownTimerHandle);
	}
}

ATN_RunGameMode* ATN_StormVolume::ResolveRunGameMode() const
{
	return TN_ResolveRunGameMode(GetWorld());
}
