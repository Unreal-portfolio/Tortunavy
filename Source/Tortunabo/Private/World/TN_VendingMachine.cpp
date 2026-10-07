// Máquina expendedora: se compra metiendo chapas por la ranura (#859). Ver TN_VendingMachine.h.

#include "World/TN_VendingMachine.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Core/TN_Log.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/TN_ItemRuntime.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_EconomySettings.h"
#include "UObject/ConstructorHelpers.h"
#include "World/TN_PickupInteractableBase.h"

namespace TNVendingMachineDetail
{
	/** Medidas del cuerpo (cm): fondo (X), ancho (Y) y alto (Z). La cara con la ranura mira a +X. */
	constexpr double BodyDepth = 90.0;
	constexpr double BodyWidth = 110.0;
	constexpr double BodyHeight = 190.0;
	/** Ranura: medio tamaño de la caja (una rendija vertical, para la chapa de canto) y altura de su centro. */
	const FVector SlotExtent(10.0, 14.0, 20.0);
	constexpr double SlotHeight = 120.0;
	/** La chapa solo se busca en máquinas a menos de esta distancia del tramo de vuelo (cm). */
	constexpr double SlotSearchRadius = 400.0;
	/** Distancia (cm) delante de la cara desde la que se usa la máquina. */
	constexpr double UseDistance = 90.0;
	/** Cada cuánto (s) se rehace la pantalla en cada máquina. */
	constexpr float DisplayRefreshSeconds = 0.25f;

	const FLinearColor BodyColor(0.75f, 0.08f, 0.06f);
	const FLinearColor SlotColor(0.03f, 0.03f, 0.03f);
	const FLinearColor TrayColor(0.2f, 0.2f, 0.22f);

	void Paint(UStaticMeshComponent* Component, const FLinearColor& Color)
	{
		if (UMaterialInstanceDynamic* Material = Component ? Component->CreateDynamicMaterialInstance(0) : nullptr)
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
}

ATN_VendingMachine::ATN_VendingMachine()
{
	using namespace TNVendingMachineDetail;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
	PromptText = NSLOCTEXT("TNEconomy", "VendingPrompt", "Pulsa: otro objeto · Mantén: comprar");

	// Falta el arte: cubos del motor (cuerpo, rendija y bandeja) con color.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* CubeMesh = Cube.Succeeded() ? Cube.Object : nullptr;

	// El cuerpo es la malla del interactuable (la encuentra el escaneo de interacción), pero aquí es un mueble: bloquea a las tortugas.
	Mesh->SetStaticMesh(CubeMesh);
	Mesh->SetRelativeLocation(FVector(0.0, 0.0, BodyHeight * 0.5));
	Mesh->SetRelativeScale3D(FVector(BodyDepth, BodyWidth, BodyHeight) / 100.0);
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Mesh->SetCanEverAffectNavigation(true);

	PromptWidgetComponent->SetupAttachment(SceneRoot);
	PromptWidgetComponent->SetRelativeLocation(FVector(BodyDepth * 0.5 + 20.0, 0.0, BodyHeight + 40.0));

	SlotBox = CreateDefaultSubobject<UBoxComponent>(TEXT("SlotBox"));
	SlotBox->SetupAttachment(SceneRoot);
	SlotBox->SetBoxExtent(SlotExtent);
	SlotBox->SetRelativeLocation(FVector(BodyDepth * 0.5 + SlotExtent.X - 2.0, 0.0, SlotHeight));
	SlotBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SlotBox->SetGenerateOverlapEvents(false);
	SlotBox->SetHiddenInGame(true);

	SlotMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SlotMesh"));
	SlotMesh->SetupAttachment(SceneRoot);
	SlotMesh->SetStaticMesh(CubeMesh);
	SlotMesh->SetRelativeLocation(FVector(BodyDepth * 0.5 + 0.5, 0.0, SlotHeight));
	SlotMesh->SetRelativeScale3D(FVector(0.02, SlotExtent.Y * 2.0 / 100.0, SlotExtent.Z * 2.0 / 100.0));
	SlotMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	TrayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TrayMesh"));
	TrayMesh->SetupAttachment(SceneRoot);
	TrayMesh->SetStaticMesh(CubeMesh);
	TrayMesh->SetRelativeLocation(FVector(BodyDepth * 0.5 + 10.0, 0.0, 30.0));
	TrayMesh->SetRelativeScale3D(FVector(0.2, 0.6, 0.06));
	TrayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	TrayPoint = CreateDefaultSubobject<USceneComponent>(TEXT("TrayPoint"));
	TrayPoint->SetupAttachment(SceneRoot);
	TrayPoint->SetRelativeLocation(FVector(BodyDepth * 0.5 + 45.0, 0.0, 25.0));

	Display = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Display"));
	Display->SetupAttachment(SceneRoot);
	Display->SetRelativeLocation(FVector(BodyDepth * 0.5 + 1.0, 0.0, 165.0));
	Display->SetHorizontalAlignment(EHTA_Center);
	Display->SetVerticalAlignment(EVRTA_TextCenter);
	Display->SetWorldSize(11.f);
	Display->SetTextRenderColor(FColor(255, 236, 160));
	Display->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ATN_VendingMachine::BeginPlay()
{
	Super::BeginPlay();
	ApplyCodeArt();
	RefreshDisplay();
}

void ATN_VendingMachine::ApplyCodeArt()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	using namespace TNVendingMachineDetail;
	Paint(Mesh, BodyColor);
	Paint(SlotMesh, SlotColor);
	Paint(TrayMesh, TrayColor);
}

void ATN_VendingMachine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_VendingMachine, Credits);
	DOREPLIFETIME(ATN_VendingMachine, SelectedIndex);
	DOREPLIFETIME(ATN_VendingMachine, HoldPawn);
	DOREPLIFETIME(ATN_VendingMachine, HoldStart);
}

void ATN_VendingMachine::OnRep_Machine()
{
	RefreshDisplay();
}

const TArray<FTNVendingOffer>& ATN_VendingMachine::GetOffers() const
{
	return TNVending::OffersOf(Stock);
}

void ATN_VendingMachine::SetStock(UTN_VendingStockData* InStock)
{
	Stock = InStock;
	SelectedIndex = 0;
	RefreshDisplay();
}

FVector ATN_VendingMachine::GetInteractionPoint() const
{
	const FVector Forward = GetActorForwardVector();
	return GetActorLocation() + Forward * (TNVendingMachineDetail::BodyDepth * 0.5 + TNVendingMachineDetail::UseDistance);
}

FVector ATN_VendingMachine::GetSlotLocation() const
{
	return SlotBox ? SlotBox->GetComponentLocation() : GetActorLocation();
}

FVector ATN_VendingMachine::GetTrayLocation() const
{
	return TrayPoint ? TrayPoint->GetComponentLocation() : GetActorLocation();
}

bool ATN_VendingMachine::CanInteract(APawn* Interactor) const
{
	return Super::CanInteract(Interactor) && GetOffers().Num() > 0;
}

// ── Ranura y crédito ───────────────────────────────────────────────────────────────────────────────────────────────

bool ATN_VendingMachine::DoesSegmentCrossSlot(const FVector& A, const FVector& B) const
{
	if (!SlotBox)
	{
		return false;
	}
	const FTransform& Slot = SlotBox->GetComponentTransform();
	return TNChapaRules::SegmentHitsBox(Slot.InverseTransformPosition(A), Slot.InverseTransformPosition(B), SlotBox->GetUnscaledBoxExtent());
}

ATN_VendingMachine* ATN_VendingMachine::FindSlotCrossed(const UWorld* World, const FVector& A, const FVector& B)
{
	if (!World)
	{
		return nullptr;
	}
	const FVector Mid = (A + B) * 0.5;
	const double Reach = TNVendingMachineDetail::SlotSearchRadius + FVector::Dist(A, B) * 0.5;
	for (TActorIterator<ATN_VendingMachine> It(World); It; ++It)
	{
		ATN_VendingMachine* Machine = *It;
		if (FVector::DistSquared(Machine->GetSlotLocation(), Mid) <= Reach * Reach && Machine->DoesSegmentCrossSlot(A, B))
		{
			return Machine;
		}
	}
	return nullptr;
}

int32 ATN_VendingMachine::KeyOf(const APawn* Pawn)
{
	if (!Pawn)
	{
		return 0;
	}
	// El jugador (sobrevive a morir y revivir); sin PlayerState (pruebas, bots), la propia tortuga en negativo.
	const APlayerState* PlayerState = Pawn->GetPlayerState();
	return PlayerState ? PlayerState->GetPlayerId() : -static_cast<int32>(Pawn->GetUniqueID()) - 1;
}

int32 ATN_VendingMachine::FindCreditIndex(int32 Key) const
{
	return Credits.IndexOfByPredicate([Key](const FTNVendingCredit& Entry) { return Entry.PlayerKey == Key; });
}

int32 ATN_VendingMachine::GetCreditFor(const APawn* Pawn) const
{
	const int32 Index = Pawn ? FindCreditIndex(KeyOf(Pawn)) : INDEX_NONE;
	return Credits.IsValidIndex(Index) ? Credits[Index].Credit : 0;
}

bool ATN_VendingMachine::ServerInsertChapa(ATortugaCharacter* Thrower, int32 Value)
{
	if (!HasAuthority() || !Thrower)
	{
		return false;
	}
	FlushNetDormancy();
	const int32 Key = KeyOf(Thrower);
	int32 Index = FindCreditIndex(Key);
	if (Index == INDEX_NONE)
	{
		Index = Credits.Add(FTNVendingCredit{Key, 0});
	}
	Credits[Index].Credit = TNChapaRules::CreditAfterInsert(Credits[Index].Credit, Value, UTN_EconomySettings::Get().MaxVendingCredit);
	PlayMachineSound(InsertSound, Thrower, ETNRaceSound::Beep, 1.6f);
	RefreshDisplay();
	UE_LOG(LogTortunabo, Log, TEXT("[Máquina] %s mete una chapa en %s (crédito %d)."), *GetNameSafe(Thrower), *GetName(), Credits[Index].Credit);
	return true;
}

// ── Elegir y comprar ───────────────────────────────────────────────────────────────────────────────────────────────

void ATN_VendingMachine::ServerCycleOffer()
{
	if (!HasAuthority())
	{
		return;
	}
	FlushNetDormancy();
	SelectedIndex = TNChapaRules::NextOffer(SelectedIndex, GetOffers().Num());
	RefreshDisplay();
}

void ATN_VendingMachine::Interact(APawn* Interactor)
{
	if (HasAuthority() && CanInteract(Interactor))
	{
		ServerCycleOffer();
		OnInteracted(Interactor);
	}
}

TNChapaRules::EBuy ATN_VendingMachine::ServerTryBuy(ATortugaCharacter* Buyer)
{
	using TNChapaRules::EBuy;
	if (!HasAuthority() || !Buyer)
	{
		return EBuy::NoOffer;
	}
	const TArray<FTNVendingOffer>& Offers = GetOffers();
	const FTNVendingOffer* Offer = Offers.IsValidIndex(SelectedIndex) ? &Offers[SelectedIndex] : nullptr;
	FTN_InventoryItem Item;
	const bool bValid = Offer && TNVending::ResolveOfferItem(Offer->ItemId, Item);
	const int32 Index = FindCreditIndex(KeyOf(Buyer));
	const int32 Credit = Credits.IsValidIndex(Index) ? Credits[Index].Credit : 0;
	EBuy Result = TNChapaRules::DecideBuy(Credit, Offer ? Offer->Price : 0, bValid);
	// Primero sale el objeto y luego se cobra: si no se ha podido sacar, no se cobra nada.
	if (Result == EBuy::Bought && !SpawnTrayPickup(Item))
	{
		Result = EBuy::NoOffer;
	}
	if (Result != EBuy::Bought)
	{
		PlayMachineSound(DenySound, Buyer, ETNRaceSound::Nope, 1.f);
		UE_LOG(LogTortunabo, Log, TEXT("[Máquina] %s no puede comprar en %s (crédito %d)."), *GetNameSafe(Buyer), *GetName(), Credit);
		return Result;
	}
	// Con precio 0 puede no tener crédito apuntado: no hay nada que cobrar.
	if (Credits.IsValidIndex(Index))
	{
		FlushNetDormancy();
		Credits[Index].Credit -= Offer->Price;
	}
	PlayMachineSound(VendSound, Buyer, ETNRaceSound::BoxOpen, 1.f);
	RefreshDisplay();
	UE_LOG(LogTortunabo, Log, TEXT("[Máquina] %s compra %s por %d (le quedan %d)."), *GetNameSafe(Buyer), *Offer->ItemId.ToString(), Offer->Price,
		GetCreditFor(Buyer));
	return Result;
}

bool ATN_VendingMachine::SpawnTrayPickup(const FTN_InventoryItem& Item) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const TSubclassOf<ATN_PickupInteractableBase> PickupClass = Item.PickupActorClass ? Item.PickupActorClass : TSubclassOf<ATN_PickupInteractableBase>(ATN_PickupInteractableBase::StaticClass());
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_PickupInteractableBase* Pickup = World->SpawnActor<ATN_PickupInteractableBase>(PickupClass, GetTrayLocation(), GetActorRotation(), Params);
	if (!Pickup)
	{
		return false;
	}
	Pickup->InitializeFromInventoryItem(Item);
	return true;
}

// ── Mantener la tecla: pulsar cambia de objeto, mantener compra ────────────────────────────────────────────────────

float ATN_VendingMachine::GetHoldDuration() const
{
	return UTN_EconomySettings::Get().VendingBuyHoldSeconds;
}

void ATN_VendingMachine::BeginHoldInteract(APawn* Interactor)
{
	if (!HasAuthority() || !Interactor || (HoldPawn && HoldPawn != Interactor))
	{
		return;
	}
	FlushNetDormancy();
	HoldPawn = Interactor;
	HoldStart = static_cast<float>(TNItemRuntime::ServerNow(GetWorld()));
	bHoldResolved = false;
}

void ATN_VendingMachine::EndHoldInteract(APawn* Interactor)
{
	if (!HasAuthority() || !Interactor || HoldPawn != Interactor)
	{
		return;
	}
	const double Held = TNItemRuntime::ServerNow(GetWorld()) - static_cast<double>(HoldStart);
	if (!bHoldResolved && TNChapaRules::IsTap(Held, GetHoldDuration()))
	{
		ServerCycleOffer();
	}
	ClearHold();
}

float ATN_VendingMachine::GetHoldProgress(const APawn* Interactor) const
{
	if (!Interactor || HoldPawn != Interactor)
	{
		return -1.f;
	}
	const double Held = TNItemRuntime::ServerNow(GetWorld()) - static_cast<double>(HoldStart);
	return FMath::Clamp(static_cast<float>(Held / FMath::Max(0.01f, GetHoldDuration())), 0.f, 1.f);
}

void ATN_VendingMachine::ClearHold()
{
	FlushNetDormancy();
	HoldPawn = nullptr;
	HoldStart = 0.f;
}

void ATN_VendingMachine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && HoldPawn && !bHoldResolved)
	{
		const double Held = TNItemRuntime::ServerNow(GetWorld()) - static_cast<double>(HoldStart);
		const double MaxReach = ATortugaCharacter::DefaultInteractionDistance + 100.0;
		if (!IsValid(HoldPawn) || FVector::DistSquared(HoldPawn->GetActorLocation(), GetInteractionPoint()) > MaxReach * MaxReach)
		{
			ClearHold();
		}
		else if (!TNChapaRules::IsTap(Held, GetHoldDuration()))
		{
			// Se queda resuelto hasta que suelte la tecla: soltarla después ya no cambia de objeto.
			bHoldResolved = true;
			ServerTryBuy(Cast<ATortugaCharacter>(HoldPawn));
		}
	}
	DisplayClock -= DeltaSeconds;
	if (DisplayClock <= 0.f)
	{
		DisplayClock = TNVendingMachineDetail::DisplayRefreshSeconds;
		RefreshDisplay();
	}
}

// ── Pantalla y sonido ──────────────────────────────────────────────────────────────────────────────────────────────

void ATN_VendingMachine::RefreshDisplay()
{
	if (!Display || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const TArray<FTNVendingOffer>& Offers = GetOffers();
	if (!Offers.IsValidIndex(SelectedIndex))
	{
		Display->SetText(NSLOCTEXT("TNEconomy", "VendingEmpty", "Agotada"));
		return;
	}
	const FTNVendingOffer& Offer = Offers[SelectedIndex];
	const APlayerController* Viewer = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const int32 Credit = GetCreditFor(Viewer ? Viewer->GetPawn() : nullptr);
	FFormatNamedArguments Args;
	Args.Add(TEXT("Index"), SelectedIndex + 1);
	Args.Add(TEXT("Count"), Offers.Num());
	Args.Add(TEXT("Item"), TNVending::OfferName(Offer));
	Args.Add(TEXT("Price"), Offer.Price);
	Args.Add(TEXT("Credit"), Credit);
	Display->SetText(FText::Format(NSLOCTEXT("TNEconomy", "VendingScreen",
		"{Index}/{Count} {Item}\n{Price} {Price}|plural(one=chapa,other=chapas)\nCrédito: {Credit}"), Args));
}

void ATN_VendingMachine::PlayMachineSound(USoundBase* Sound, ATortugaCharacter* Turtle, ETNRaceSound Fallback, float Pitch)
{
	if (Sound)
	{
		MulticastSound(Sound);
		return;
	}
	if (Turtle)
	{
		TNItemRuntime::PlayCue(Turtle, Fallback, Pitch);
	}
}

void ATN_VendingMachine::MulticastSound_Implementation(USoundBase* Sound)
{
	if (Sound && GetNetMode() != NM_DedicatedServer)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, Sound, GetSlotLocation());
	}
}
