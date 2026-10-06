#include "World/ProcMap/TN_TurtleDoll.h"

#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_Log.h"
#include "World/TN_PlaceholderArtMeshes.h"
#include "TN_TurtleDollMesh.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

namespace TNTurtleDollDetail
{
	/** La figurita mide unos 40 cm de alto: se escala para que se vea bien junto a la tortuga. */
	constexpr float VisualScale = 1.4f;
	/** Radio de recogida (cm). */
	constexpr float PickupRadius = 110.f;

	/** Cota de la figurita respecto al actor: su origen está en la base de la peana y el actor queda a media altura. */
	inline float VisualBaseZ() { return -0.3f * VisualScale * static_cast<float>(TNTurtleDollMesh::Size().Z); }
}

ATN_TurtleDoll::ATN_TurtleDoll()
{
	bReplicates = true;
	SetNetUpdateFrequency(2.f);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->InitSphereRadius(TNTurtleDollDetail::PickupRadius);
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->SetCanEverAffectNavigation(false);

	// Sin malla en el constructor: la figurita de código se pone en BeginPlay (no en el servidor dedicado).
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Root);
	Visual->SetMobility(EComponentMobility::Movable);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);
	Visual->SetGenerateOverlapEvents(false);
	Visual->SetRelativeScale3D(FVector(TNTurtleDollDetail::VisualScale));
	Visual->SetRelativeLocation(FVector(0.f, 0.f, TNTurtleDollDetail::VisualBaseZ()));
}

void ATN_TurtleDoll::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &ATN_TurtleDoll::OnTriggerOverlap);
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (UStaticMesh* Mesh = TNPlaceholderArt::CachedArtMesh(TEXT("TurtleDoll.Figurine"), [](TNProcMesh::FTNProcMeshBuffers& B)
	{
		TNTurtleDollMesh::Build(B);
	}, TNPlaceholderArt::MatteAlpha))
	{
		Visual->SetStaticMesh(Mesh);
	}
	else
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[TurtleDoll] %s: no se ha podido construir la figurita."), *GetName());
	}
	// Cada muñeco empieza el giro en un punto distinto para que no vayan todos a la vez.
	AnimTime = FMath::Frac(GetActorLocation().X * 0.0013f + GetActorLocation().Y * 0.0007f) * 10.f;
	SetActorTickEnabled(true);
	ApplyLocalVisibility();
}

void ATN_TurtleDoll::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bLocallyCollected || !Visual)
	{
		return;
	}
	AnimTime += DeltaSeconds;
	const float Yaw = FMath::Fmod(AnimTime * SpinTurnsPerSecond * 360.f, 360.f);
	Visual->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
	Visual->SetRelativeLocation(FVector(0.f, 0.f, TNTurtleDollDetail::VisualBaseZ() + BobAmplitude * FMath::Sin(AnimTime * 2.f)));
}

void ATN_TurtleDoll::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_TurtleDoll, CollectorIds);
}

int32 ATN_TurtleDoll::CountInWorld(const UWorld* World)
{
	int32 Count = 0;
	if (!World)
	{
		return Count;
	}
	for (TActorIterator<ATN_TurtleDoll> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (IsValid(*It) && !It->IsActorBeingDestroyed())
		{
			++Count;
		}
	}
	return Count;
}

void ATN_TurtleDoll::OnRep_CollectorIds()
{
	ApplyLocalVisibility();
}

void ATN_TurtleDoll::ApplyLocalVisibility()
{
	if (GetNetMode() == NM_DedicatedServer || bLocallyCollected)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const APlayerController* LocalPC = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerState* LocalPS = LocalPC ? LocalPC->PlayerState : nullptr;
	if (!LocalPS || !HasCollected(LocalPS->GetPlayerId()))
	{
		return;
	}
	bLocallyCollected = true;
	Visual->SetVisibility(false, true);
	SetActorTickEnabled(false);
	if (PickupSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, PickupSound, GetActorLocation());
	}
}

void ATN_TurtleDoll::OnTriggerOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!HasAuthority())
	{
		return;
	}
	const APawn* Pawn = Cast<APawn>(OtherActor);
	ATN_CoopPlayerState* PS = Pawn ? Pawn->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
	if (!PS || !TNTurtleDollRules::CanCollect(CollectorIds, PS->GetPlayerId(), PS->IsAliveAndPlaying()))
	{
		return;
	}
	CollectorIds.Add(PS->GetPlayerId());
	PS->AddTurtleDoll();
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[TurtleDoll] %s cogió %s (lleva %d)."), *PS->GetPlayerName(), *GetName(), PS->TurtleDollsCollected);
	// En el anfitrión el OnRep no salta: si quien lo coge es la jugadora de esta máquina, desaparece aquí.
	ApplyLocalVisibility();
}
