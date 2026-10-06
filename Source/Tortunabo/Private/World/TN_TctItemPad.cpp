#include "World/TN_TctItemPad.h"
#include "Core/TN_Log.h"
#include "Game/TN_TctGameState.h"
#include "Game/TN_TctItemComponent.h"
#include "Player/TortugaCharacter.h"
#include "Game/TN_TctItems.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace TNTctItemPadDetail
{
	/** Cada cuánto mira el servidor el reloj del punto (s). */
	constexpr float UpdateInterval = 0.25f;
	/** Si no ha podido sacar un objeto, lo reintenta pasado esto (s). */
	constexpr float RetrySeconds = 2.f;
	/** Agua «sin agua» (sin GameState de TcT). */
	constexpr float NoWaterZ = -1.e6f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pickup
// ─────────────────────────────────────────────────────────────────────────────

ATN_TctItemPickup::ATN_TctItemPickup()
{
	RefreshPrompt();
}

void ATN_TctItemPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ATN_TctItemPickup, Kind, COND_InitialOnly);
}

void ATN_TctItemPickup::SetUp(ATN_TctItemPad* InPad, ETNTctItem InKind, const FTN_InventoryItem& Item)
{
	Pad = InPad;
	Kind = static_cast<uint8>(InKind);
	InitializeFromInventoryItem(Item);
	RefreshPrompt();
}

void ATN_TctItemPickup::OnRep_Kind()
{
	RefreshPrompt();
}

void ATN_TctItemPickup::RefreshPrompt()
{
	const ETNTctItem Item = GetKind();
	PromptText = Item == ETNTctItem::None
		? NSLOCTEXT("Tortunabo", "PickupPrompt", "Recoger")
		: FText::Format(NSLOCTEXT("TNTct", "PickupItemPrompt", "Coger {0}"), TNTctItems::DisplayName(Item));
}

bool ATN_TctItemPickup::CanInteract(APawn* Interactor) const
{
	if (GetKind() != ETNTctItem::Flotador)
	{
		return Super::CanInteract(Interactor);
	}
	// El flotador no ocupa la mano: basta con no llevar ya uno.
	const UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOn(Interactor);
	return ATN_InteractableBase::CanInteract(Interactor) && !bTaken && Cast<ATortugaCharacter>(Interactor)
		&& !(Effects && Effects->HasFloat());
}

void ATN_TctItemPickup::TakeFloat(APawn* Interactor)
{
	UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOrAddOn(Cast<ATortugaCharacter>(Interactor));
	if (!Effects || !Effects->ServerGrantFloat())
	{
		return;
	}
	SetNetDormancy(DORM_Awake);
	bTaken = true;
	SetInteractionEnabled(false);
	SetActorHiddenInGame(true);
	ForceNetUpdate();
	TNTctItems::PlayCue(Cast<ATortugaCharacter>(Interactor), ETNRaceSound::BoxOpen, 1.f);
	// Como el resto de pickups: la destrucción replicada en el siguiente fotograma.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { Destroy(); }));
	UE_LOG(LogTortunabo, Log, TEXT("[TcT] %s se cuelga el flotador del caparazón."), *GetNameSafe(Interactor));
}

void ATN_TctItemPickup::Interact(APawn* Interactor)
{
	if (GetKind() == ETNTctItem::Flotador)
	{
		if (HasAuthority() && CanInteract(Interactor))
		{
			TakeFloat(Interactor);
		}
	}
	else
	{
		Super::Interact(Interactor);
	}
	if (bTaken)
	{
		if (ATN_TctItemPad* OwningPad = Pad.Get())
		{
			OwningPad->NotifyTaken(this);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Punto de objetos
// ─────────────────────────────────────────────────────────────────────────────

ATN_TctItemPad::ATN_TctItemPad()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(1.f);

	Disc = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Disc"));
	RootComponent = Disc;
	Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Disc->SetCanEverAffectNavigation(false);
	Disc->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		Disc->SetStaticMesh(Cylinder.Object);
	}
	// Disco de 1,6 m y 3 cm de alto, apenas por encima del suelo (el cilindro del motor mide 100 uu, con el centro en medio).
	Disc->SetRelativeScale3D(FVector(1.6f, 1.6f, 0.03f));
}

void ATN_TctItemPad::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInstanceDynamic* Material = Disc ? Disc->CreateDynamicMaterialInstance(0) : nullptr)
	{
		Material->SetVectorParameterValue(TEXT("Color"), DiscColor);
	}
}

void ATN_TctItemPad::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(UpdateHandle);
	if (HasAuthority())
	{
		RemoveCurrent();
	}
	Super::EndPlay(EndPlayReason);
}

void ATN_TctItemPad::ServerStartRound(double Now, int32 PadIndex)
{
	if (!HasAuthority())
	{
		return;
	}
	RemoveCurrent();
	Available = TNTctItems::AvailableKinds();
	Clock.StartRound(Now, TNTctItemRules::PadFirstSpawnDelay(PadIndex));
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(UpdateHandle, this, &ATN_TctItemPad::UpdateFromTimer, TNTctItemPadDetail::UpdateInterval, true);
	}
}

void ATN_TctItemPad::ServerStopRound()
{
	if (!HasAuthority())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(UpdateHandle);
	Clock.Stop();
	RemoveCurrent();
}

void ATN_TctItemPad::UpdateFromTimer()
{
	const UWorld* World = GetWorld();
	const ATN_TctGameState* State = World ? World->GetGameState<ATN_TctGameState>() : nullptr;
	ServerUpdate(World ? World->GetTimeSeconds() : 0.0, State ? State->GetWaterZ() : TNTctItemPadDetail::NoWaterZ);
}

void ATN_TctItemPad::ServerUpdate(double Now, float WaterZ)
{
	if (!HasAuthority())
	{
		return;
	}
	// El objeto ha desaparecido sin avisar (destruido por otra cosa): cuenta como cogido.
	ATN_TctItemPickup* Pickup = Current.Get();
	if (Clock.bHasItem && (!IsValid(Pickup) || Pickup->IsTaken()))
	{
		Current.Reset();
		Clock.MarkTaken(Now, RespawnSeconds);
	}
	if (TNTctItemRules::IsPadSubmerged(static_cast<float>(GetActorLocation().Z), WaterZ))
	{
		if (Clock.IsRunning())
		{
			// El mar se lo lleva: este punto ya no saca nada esta ronda.
			RemoveCurrent();
			Clock.Stop();
			GetWorldTimerManager().ClearTimer(UpdateHandle);
		}
		return;
	}
	if (Clock.ShouldSpawn(Now, false) && !SpawnItem())
	{
		Clock.StartRound(Now, TNTctItemPadDetail::RetrySeconds);
	}
}

void ATN_TctItemPad::NotifyTaken(ATN_TctItemPickup* Pickup)
{
	if (!HasAuthority() || !Pickup || Pickup != Current.Get())
	{
		return;
	}
	Current.Reset();
	Clock.MarkTaken(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0, RespawnSeconds);
}

bool ATN_TctItemPad::SpawnItem()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	if (Available.Num() == 0)
	{
		Available = TNTctItems::AvailableKinds();
	}
	const ETNTctItem Kind = TNTctItemRules::PickPadItem(Available, LastKind, FMath::FRand());
	FTN_InventoryItem Item;
	if (Kind == ETNTctItem::None || !TNTctItems::MakeItem(Kind, Item))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[TcT] El punto de objetos %s no ha podido sacar nada."), *GetName());
		return false;
	}
	const FTransform Where(FRotator(0.0, FMath::FRandRange(0.0, 360.0), 0.0), GetActorLocation() + FVector(0.0, 0.0, ItemLift));
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	ATN_TctItemPickup* Pickup = World->SpawnActor<ATN_TctItemPickup>(ATN_TctItemPickup::StaticClass(), Where, Params);
	if (!Pickup)
	{
		return false;
	}
	Pickup->SetUp(this, Kind, Item);
	Pickup->FinishSpawning(Where);
	Current = Pickup;
	LastKind = Kind;
	Clock.MarkSpawned();
	UE_LOG(LogTortunabo, Verbose, TEXT("[TcT] Punto %s: sale %s."), *GetName(), TNTctItemRules::Spec(Kind).Code);
	return true;
}

void ATN_TctItemPad::RemoveCurrent()
{
	if (ATN_TctItemPickup* Pickup = Current.Get())
	{
		if (!Pickup->IsTaken() && !Pickup->IsActorBeingDestroyed())
		{
			Pickup->Destroy();
		}
	}
	Current.Reset();
}
