#include "Rally/TN_RallyAmmoBox.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Rally/TN_RallyGameMode.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyVehicle.h"
#include "TimerManager.h"
#include "TN_RallyMeshUtils.h"
#include "UObject/ConstructorHelpers.h"

ATN_RallyAmmoBox::ATN_RallyAmmoBox()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetNetCullDistanceSquared(FMath::Square(30000.f));

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(PickupRadiusCm);
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->SetCanEverAffectNavigation(false);
	RootComponent = Trigger;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Trigger);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ShellFinder(TNRallyMesh::AmmoShellPath);
	if (ShellFinder.Succeeded())
	{
		Mesh->SetStaticMesh(ShellFinder.Object);
		Mesh->SetRelativeTransform(TNRallyMesh::FitToBox(ShellFinder.Object, FVector(0.0, 0.0, 60.0), FVector(120.0)));
	}

	// Giro solo cosmético: cada máquina lo anima en local.
	Spin = CreateDefaultSubobject<URotatingMovementComponent>(TEXT("Spin"));
	Spin->RotationRate = FRotator(0.0, 90.0, 0.0);
	Spin->UpdatedComponent = Mesh;
	Spin->bUpdateOnlyIfRendered = true;
}

void ATN_RallyAmmoBox::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_RallyAmmoBox, bAvailable);
}

void ATN_RallyAmmoBox::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);
	TryCollect(OtherActor);
}

void ATN_RallyAmmoBox::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ReactivateHandle);
	Super::EndPlay(EndPlayReason);
}

bool ATN_RallyAmmoBox::TryCollect(AActor* Vehicle)
{
	if (!HasAuthority() || !bAvailable || !Vehicle)
	{
		return false;
	}
	ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Vehicle);
	if (!RallyVehicle)
	{
		return false;
	}
	ETNRallyAmmo Ammo = ETNRallyAmmo::None;
	int32 Charges = 0;
	if (ATN_RallyGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ATN_RallyGameMode>() : nullptr)
	{
		if (!GameMode->RollAmmoFor(Vehicle, Ammo, Charges))
		{
			return false;
		}
	}
	else
	{
		Ammo = TNRally::PickAmmo(TNRally::AmmoWeightsForPlace(1, 1), FMath::FRand());
		Charges = TNRally::ChargesFor(Ammo);
	}
	RallyVehicle->GiveSpecialAmmo(Ammo, Charges);
	UE_LOG(LogTNRally, Verbose, TEXT("[RallyAmmoBox] %s recoge %s ×%d."), *Vehicle->GetName(),
		*StaticEnum<ETNRallyAmmo>()->GetNameStringByValue(static_cast<int64>(Ammo)), Charges);

	bAvailable = false;
	OnRep_Available();
	ForceNetUpdate();
	GetWorldTimerManager().SetTimer(ReactivateHandle, this, &ATN_RallyAmmoBox::Reactivate, RespawnSeconds, false);
	return true;
}

void ATN_RallyAmmoBox::Reactivate()
{
	bAvailable = true;
	OnRep_Available();
	ForceNetUpdate();
}

void ATN_RallyAmmoBox::OnRep_Available()
{
	Mesh->SetVisibility(bAvailable);
}
