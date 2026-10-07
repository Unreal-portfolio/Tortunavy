#include "World/ProcMap/TN_ProcAnnelid.h"

#include "World/ProcMap/TN_ProcMapActorUtils.h"
#include "GameFramework/Character.h"
#include "World/TN_HazardEffects.h"
#include "Core/TN_Log.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "UObject/ConstructorHelpers.h"

namespace TNAnnelidDetail
{
	const FLinearColor MouthColor(0.18f, 0.08f, 0.07f);
	const FLinearColor BodyColor(0.85f, 0.38f, 0.32f);
}

ATN_ProcAnnelid::ATN_ProcAnnelid()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	// Boca: disco oscuro a ras de suelo (radio 60 cm). Su colisión es la que encuentra el escaneo de interacción.
	if (Mesh && Cylinder.Succeeded())
	{
		Mesh->SetStaticMesh(Cylinder.Object);
		Mesh->SetRelativeScale3D(FVector(1.2f, 1.2f, 0.06f));
		Mesh->SetRelativeLocation(FVector(0.f, 0.f, 2.f));
	}

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(SceneRoot);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);
	Body->SetVisibility(false);
	if (Cylinder.Succeeded())
	{
		Body->SetStaticMesh(Cylinder.Object);
		Body->SetRelativeScale3D(FVector(0.55f, 0.55f, 1.8f));
	}

	InteractionDistance = TNAnnelid::HUNT_RADIUS;
	CooldownSeconds = 0.f;
	PromptText = NSLOCTEXT("Tortunabo", "AnnelidPrompt", "Cazar el gusano");
}

void ATN_ProcAnnelid::BeginPlay()
{
	Super::BeginPlay();
	TNProcActors::Tint(Mesh, TNAnnelidDetail::MouthColor);
	TNProcActors::Tint(Body, TNAnnelidDetail::BodyColor);
	// Escondido bajo la boca: la parte de arriba del gusano a 10 cm bajo el suelo.
	BodyHiddenZ = -(Body ? Body->GetRelativeScale3D().Z * 50.f : 90.f) - 10.f;
	if (Body) { Body->SetRelativeLocation(FVector(0.f, 0.f, BodyHiddenZ)); }
	// Un cliente que entra tarde (o recibe la réplica antes que BeginPlay) ya la ve cazada.
	if (bConsumedReplicated && SinceHunt < 0.f)
	{
		OnRep_Consumed();
	}
}

void ATN_ProcAnnelid::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_ProcAnnelid, bConsumedReplicated);
}

bool ATN_ProcAnnelid::CanInteract(APawn* Interactor) const
{
	return !HuntState.bConsumed && !bConsumedReplicated && Super::CanInteract(Interactor);
}

FVector ATN_ProcAnnelid::GetInteractionPointFor(const APawn* Interactor) const
{
	const FVector Center = GetInteractionPoint();
	if (!Interactor)
	{
		return Center;
	}
	const FVector ToInteractor = Interactor->GetActorLocation() - Center;
	return Center + ToInteractor.GetSafeNormal() * TNAnnelid::InteractionPointShift(static_cast<float>(ToInteractor.Size()));
}

void ATN_ProcAnnelid::OnInteracted_Implementation(APawn* Interactor)
{
	Super::OnInteracted_Implementation(Interactor);
	if (!HasAuthority() || !Interactor)
	{
		return;
	}
	const float Distance = static_cast<float>(FVector::Dist(Interactor->GetActorLocation(), GetInteractionPoint()));
	// La distancia ya la ha validado el servidor con margen de ping (ServerTryInteract): se recorta a HUNT_RADIUS para
	// que ese margen no la rechace aquí. Lo que decide TryHunt es que se caza una sola vez.
	if (!HuntState.TryHunt(FMath::Min(Distance, TNAnnelid::HUNT_RADIUS)))
	{
		return;
	}
	// Hoja EnemyAndObstacleData (#871): cura +25 de vida (UTN_HazardTuning::AnnelidHeal); ya no repone la estamina.
	const float HealAmount = UTN_HazardTuning::Get().AnnelidHeal;
	TNHazard::Heal(Cast<ACharacter>(Interactor), HealAmount);
	UE_LOG(LogTortunabo, Log, TEXT("[Anelido] %s caza el anélido %s: +%.0f de vida."), *Interactor->GetName(), *GetName(), HealAmount);

	bConsumedReplicated = true;
	SetInteractionEnabled(false);
	FlushNetDormancy();
	ForceNetUpdate();
	PlayHuntLocal();
	SetLifeSpan(TNAnnelid::LIFE_AFTER_HUNT);
}

void ATN_ProcAnnelid::OnRep_Consumed()
{
	if (!bConsumedReplicated)
	{
		return;
	}
	HuntState.bConsumed = true;
	PlayHuntLocal();
}

void ATN_ProcAnnelid::PlayHuntLocal()
{
	if (SinceHunt >= 0.f)
	{
		return;
	}
	SinceHunt = 0.f;
	if (Body) { Body->SetVisibility(true); }
	SetActorTickEnabled(true);

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (HuntSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, HuntSound, GetActorLocation());
	}
	if (EmergeFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, EmergeFX, GetActorLocation());
	}
}

void ATN_ProcAnnelid::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (SinceHunt < 0.f || !Body)
	{
		SetActorTickEnabled(false);
		return;
	}
	SinceHunt += DeltaSeconds;
	const float Height = TNAnnelid::EmergeHeight01(SinceHunt);
	// Fuera del todo, la punta del gusano queda EmergeHeight sobre la boca.
	const float HalfHeight = Body->GetRelativeScale3D().Z * 50.f;
	Body->SetRelativeLocation(FVector(0.f, 0.f, FMath::Lerp(BodyHiddenZ, EmergeHeight - HalfHeight, Height)));
	// Al meterse del todo la boca se cierra (se encoge) hasta que el actor desaparece.
	if (SinceHunt > TNAnnelid::EMERGE_SECONDS + TNAnnelid::HOLD_SECONDS && Mesh)
	{
		const float Close = FMath::Clamp((SinceHunt - TNAnnelid::EMERGE_SECONDS - TNAnnelid::HOLD_SECONDS) / TNAnnelid::SINK_SECONDS, 0.f, 1.f);
		Mesh->SetRelativeScale3D(FVector(1.2f * (1.f - 0.8f * Close), 1.2f * (1.f - 0.8f * Close), 0.06f));
	}
	if (Height <= 0.f && SinceHunt > TNAnnelid::EMERGE_SECONDS)
	{
		Body->SetVisibility(false);
		SetActorTickEnabled(false);
	}
}
