#include "Kart/TN_KartItemBox.h"

#include "../Rally/TN_RallyMeshUtils.h"
#include "../World/Beach/TN_RaceItemArt.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Kart/TN_KartItemComponent.h"
#include "Net/UnrealNetwork.h"
#include "Rally/TN_RallyLogic.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicles/TN_Buggy.h"
#include "World/Beach/TN_RaceBurstFX.h"
#include "World/TN_PickupGlowComponent.h"

namespace TNKartItemBoxDetail
{
	/** Lado (cm) del cubo de la caja de objetos de la carrera tal como lo dibuja TNRaceItemArt. */
	constexpr float ArtBoxSideCm = 36.f;
	/** Al abrirse, la caja se hincha y desaparece en este tiempo (s); al volver, crece con rebote en este otro. */
	constexpr float PopSeconds = 0.18f;
	constexpr float GrowSeconds = 0.45f;

	/** Escala de la caja al volver: de 0 a 1 con un rebote de un 18 % (easeOutBack). */
	float GrowCurve(float Alpha)
	{
		const float T = FMath::Clamp(Alpha, 0.f, 1.f) - 1.f;
		constexpr float Back = 2.2f;
		return 1.f + (Back + 1.f) * T * T * T + Back * T * T;
	}
}

ATN_KartItemBox::ATN_KartItemBox()
{
	PrimaryActorTick.bCanEverTick = true;
	// Solo hace falta el tick mientras se anima un cambio (se enciende en OnRep_Available).
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	SetNetCullDistanceSquared(FMath::Square(30000.f));
	// Una caja quieta solo cambia al abrirse o volver (con ForceNetUpdate): pocas actualizaciones bastan.
	SetNetUpdateFrequency(2.f);
	SetMinNetUpdateFrequency(0.5f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);

	// La concha cerrada solo queda si no se puede dibujar la caja de la carrera (servidor dedicado, sin material).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ShellFinder(TNRallyMesh::AmmoShellPath);
	if (ShellFinder.Succeeded())
	{
		Mesh->SetStaticMesh(ShellFinder.Object);
		Mesh->SetRelativeTransform(TNRallyMesh::FitToBox(ShellFinder.Object, FVector(0.0, 0.0, 60.0), FVector(120.0)));
	}

	// Brillo de lo que se coge, a lo grande: se ve de lejos por el camino y la caja gira y flota desde 90 m.
	Glow = CreateDefaultSubobject<UTN_PickupGlowComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Root);
	Glow->RingRadius = 170.f;
	Glow->BeamHeight = 1100.f;
	Glow->FloatLift = 30.f;
	Glow->FloatBob = 16.f;
	Glow->SpinTurnsPerSecond = 0.4f;
	Glow->LightLumens = 1400.f;
	Glow->LightRadius = 650.f;
	Glow->LightRange = 4000.f;
	Glow->SparkleRange = 5000.f;
	Glow->AnimRange = 9000.f;
	Glow->RingDrawDistance = 12000.f;
	Glow->BeamDrawDistance = 30000.f;
}

void ATN_KartItemBox::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_KartItemBox, bAvailable);
}

void ATN_KartItemBox::BeginPlay()
{
	Super::BeginPlay();
	ApplyLook();
	Mesh->SetVisibility(bAvailable);
	Glow->SetGlowEnabled(bAvailable);
}

void ATN_KartItemBox::ApplyLook()
{
	if (bLookApplied)
	{
		return;
	}
	bLookApplied = true;
	TNRaceItemArt::FHeldLook Look;
	if (!TNRaceItemArt::GetHeldLook(ETNRaceItem::Box, Look) || !Look.Mesh)
	{
		BaseScale = Mesh->GetRelativeScale3D();
		Glow->SetFloatTarget(Mesh, static_cast<float>(Mesh->GetRelativeLocation().Z));
		return;
	}
	// La malla de la carrera tiene el pivote en el centro: se escala al tamaño pedido y se inclina «de pico».
	Mesh->SetStaticMesh(Look.Mesh);
	BaseScale = FVector(BoxSizeCm / TNKartItemBoxDetail::ArtBoxSideCm);
	Mesh->SetRelativeLocationAndRotation(FVector(0.0, 0.0, BoxCenterZCm), BoxTilt);
	Mesh->SetRelativeScale3D(BaseScale);
	Glow->SetFloatTarget(Mesh, BoxCenterZCm);
}

void ATN_KartItemBox::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ReactivateHandle);
	Super::EndPlay(EndPlayReason);
}

bool ATN_KartItemBox::TryCollect(ATN_Buggy* Kart, int32 Place, int32 NumKarts)
{
	if (!HasAuthority() || !bAvailable || !Kart)
	{
		return false;
	}
	UTN_KartItemComponent* Items = Kart->FindComponentByClass<UTN_KartItemComponent>();
	// Con un objeto en la mano la caja se abre igual (como en las carreras de karts), pero no da otro.
	if (Items)
	{
		Items->TryGiveFromBox(Place, NumKarts);
	}
	bAvailable = false;
	OnRep_Available();
	ForceNetUpdate();
	GetWorldTimerManager().SetTimer(ReactivateHandle, this, &ATN_KartItemBox::Reactivate, RespawnSeconds, false);
	return true;
}

void ATN_KartItemBox::Reactivate()
{
	UE_LOG(LogTNRally, Verbose, TEXT("[KartItems] La caja %s vuelve."), *GetName());
	bAvailable = true;
	OnRep_Available();
	ForceNetUpdate();
}

void ATN_KartItemBox::OnRep_Available()
{
	ApplyLook();
	// En el servidor dedicado no hay nada que ver: solo se cambia el estado.
	if (GetNetMode() == NM_DedicatedServer)
	{
		Mesh->SetVisibility(bAvailable);
		return;
	}
	Glow->SetGlowEnabled(bAvailable);
	Mesh->SetVisibility(true);
	SinceChange = 0.f;
	SetActorTickEnabled(true);
	PlayBurst(!bAvailable);
}

void ATN_KartItemBox::PlayBurst(bool bOpened)
{
	UWorld* World = GetWorld();
	if (!World || !HasActorBegunPlay())
	{
		return;
	}
	const FVector Center = Mesh->GetComponentLocation();
	if (bOpened)
	{
		ATN_RaceBurstFX::SpawnLocal(World, ETNRaceBurst::StarPop, Center, BoxSizeCm / 80.f);
		ATN_RaceBurstFX::SpawnLocal(World, ETNRaceBurst::Poof, Center, BoxSizeCm / 90.f);
	}
	else
	{
		ATN_RaceBurstFX::SpawnLocal(World, ETNRaceBurst::Sparkle, Center, BoxSizeCm / 90.f);
	}
}

void ATN_KartItemBox::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SinceChange += DeltaSeconds;
	if (!bAvailable)
	{
		// Se abre: se hincha un 40 % y desaparece.
		const float Alpha = SinceChange / TNKartItemBoxDetail::PopSeconds;
		if (Alpha >= 1.f)
		{
			Mesh->SetVisibility(false);
			Mesh->SetRelativeScale3D(BaseScale);
			SetActorTickEnabled(false);
			return;
		}
		Mesh->SetRelativeScale3D(BaseScale * (1.f + 0.4f * Alpha) * (1.f - Alpha * Alpha));
		return;
	}
	// Vuelve: crece desde nada con un pequeño rebote.
	const float Alpha = SinceChange / TNKartItemBoxDetail::GrowSeconds;
	if (Alpha >= 1.f)
	{
		Mesh->SetRelativeScale3D(BaseScale);
		SetActorTickEnabled(false);
		return;
	}
	Mesh->SetRelativeScale3D(BaseScale * FMath::Max(0.01f, TNKartItemBoxDetail::GrowCurve(Alpha)));
}
