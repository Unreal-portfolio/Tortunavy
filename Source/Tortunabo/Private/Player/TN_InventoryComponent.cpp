#include "Player/TN_InventoryComponent.h"
#include "Player/TN_InventoryDecisions.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_TurtleAnimInstance.h"
#include "Player/TN_TurtleFoleyComponent.h"
#include "Game/TN_ItemRuntime.h"
#include "Game/TN_CoopItems.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

namespace TNInventoryHold
{
	/** Huesos de TotugaDemo_Rig (Mixamo) que sujetan el objeto: la aleta derecha, la izquierda (abrazando) y el pecho. */
	const FName RightHandBone(TEXT("RightHand"));
	const FName RightForeArmBone(TEXT("RightForeArm"));
	const FName LeftHandBone(TEXT("LeftHand"));
	const FName ChestBone(TEXT("Spine2"));

	// Medidas en unidades de la malla (sin la escala del personaje, 2,5): mira a +Y, arriba +Z, su izquierda +X.

	/** Del hueso de la mano (la muñeca) hacia la punta de la aleta, donde se agarra. */
	constexpr float GripAlong = 2.5f;
	/** Grosor de la aleta bajo lo que lleva apoyado encima. */
	constexpr float FlipperThickness = 1.f;
	/** Del hueso del pecho a la tripa por delante: lo abrazado no se mete en el cuerpo. */
	constexpr float ChestFront = 7.f;
	/** Cogido por un extremo: grados sobre la horizontal a los que apunta y cuánto se abre hacia su derecha. */
	constexpr float ByEndPitchDeg = 40.f;
	constexpr float ByEndOutward = 0.2f;

	// Guardar o sacar del caparazón, en fracción de StashSeconds: la aleta llega a la espalda, se queda y vuelve; lo
	// de la mano encoge y entra («toc»), cambia por lo guardado y eso sale creciendo («toc» más agudo).
	constexpr float ReachEnd = 0.36f;
	constexpr float ReturnStart = 0.6f;
	constexpr float InShrinkStart = 0.2f;
	constexpr float InShrinkEnd = 0.44f;
	constexpr float InSound = 0.4f;
	constexpr float SwitchAt = 0.48f;
	constexpr float OutSound = 0.54f;
	constexpr float OutGrowStart = 0.52f;
	constexpr float OutGrowEnd = 0.84f;

	/** Segundos del «pop» al aparecer en la aleta (al cogerlo del suelo). */
	constexpr float PopSeconds = 0.2f;

	inline float Smooth01(float X)
	{
		const float C = FMath::Clamp(X, 0.f, 1.f);
		return C * C * (3.f - 2.f * C);
	}
}

UTN_InventoryComponent::UTN_InventoryComponent()
{
	// Solo para colocar el objeto de las aletas tras la animación; se enciende en BeginPlay fuera del servidor dedicado.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

float UTN_InventoryComponent::GetTotalCarriedWeight() const
{
	float Total = 0.f;
	if (bHasEquippedItem) { Total += EquippedItem.ItemWeight; }
	if (bHasStoredItem)   { Total += StoredItem.ItemWeight; }
	return Total;
}

void UTN_InventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!GetOwner())
	{
		return;
	}

	EquippedVisualMesh = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("EquippedItemVisual"));
	if (!EquippedVisualMesh)
	{
		return;
	}

	EquippedVisualMesh->SetIsReplicated(false);
	EquippedVisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EquippedVisualMesh->SetCastShadow(false);
	EquippedVisualMesh->SetVisibility(false);
	EquippedVisualMesh->RegisterComponent();

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		USkeletalMeshComponent* Body = Character->GetMesh();
		USceneComponent* Parent = Body ? static_cast<USceneComponent*>(Body) : static_cast<USceneComponent*>(Character->GetRootComponent());
		if (Parent)
		{
			// De momento en la malla; PlaceShownItem lo pasa a la aleta (o al socket de siempre si la malla no tiene aletas).
			EquippedVisualMesh->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
			EquippedVisualMesh->SetRelativeLocationAndRotation(EquippedRelativeLocation, EquippedRelativeRotation);
			VisualMeshParent = Parent;
		}
		// Colocar el objeto cada fotograma después de la animación de la malla (los huesos de las aletas ya al día).
		if (Body && GetNetMode() != NM_DedicatedServer)
		{
			PrimaryComponentTick.AddPrerequisite(Body, Body->PrimaryComponentTick);
			SetComponentTickEnabled(true);
		}
	}

	RefreshEquippedVisual();
}

void UTN_InventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UTN_InventoryComponent, EquippedItem);
	DOREPLIFETIME(UTN_InventoryComponent, bHasEquippedItem);
	DOREPLIFETIME(UTN_InventoryComponent, StoredItem);
	DOREPLIFETIME(UTN_InventoryComponent, bHasStoredItem);
	DOREPLIFETIME(UTN_InventoryComponent, StashSerial);
	DOREPLIFETIME(UTN_InventoryComponent, StashKind);
}

bool UTN_InventoryComponent::TryAddItem(const FTN_InventoryItem& NewItem)
{
	if (!NewItem.IsValid())
	{
		return false;
	}

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	return AddItemInternal(NewItem);
}

bool UTN_InventoryComponent::TryAddOrReplaceEquipped(const FTN_InventoryItem& NewItem, bool bReplaceIfFull)
{
	if (!NewItem.IsValid())
	{
		return false;
	}

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	return AddOrReplaceEquippedInternal(NewItem, bReplaceIfFull);
}

bool UTN_InventoryComponent::TryReplaceEquippedItem(const FTN_InventoryItem& NewItem)
{
	if (!NewItem.IsValid() || !bHasEquippedItem || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	EquippedItem = NewItem;
	TNItemRuntime::ResolveVisuals(EquippedItem);
	RefreshEquippedVisual();
	return true;
}

bool UTN_InventoryComponent::CanReceiveItem(const FTN_InventoryItem& NewItem, bool bAllowReplaceIfFull) const
{
	if (!NewItem.IsValid())
	{
		return false;
	}

	// Un objeto del coop que ya se lleva: se coge si se puede apilar (o recargar) y no si ya está al máximo.
	int32 Slot = INDEX_NONE;
	FTN_InventoryItem Merged;
	const ETNCoopStack Stack = static_cast<ETNCoopStack>(DecideCoopStack(NewItem, Slot, Merged));
	if (Stack != ETNCoopStack::Separate)
	{
		return Stack == ETNCoopStack::Merge;
	}
	return TNInventoryLogic::CanReceiveItem(bHasEquippedItem, bHasStoredItem, bAllowReplaceIfFull);
}

namespace
{
	/** Rotar saca el guardado a la mano: no vale en el caparazón, en brazos, noqueada ni muerta (#891). */
	bool CanOwnerRotateItems(const AActor* Owner)
	{
		const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Owner);
		return !Turtle || Turtle->CanUseHandsForInteraction();
	}
}

void UTN_InventoryComponent::RotateItems()
{
	if (!GetOwner() || !CanOwnerRotateItems(GetOwner()))
	{
		return;
	}

	if (GetOwner()->HasAuthority())
	{
		SwapSlotsInternal();
		RefreshEquippedVisual();
	}
	else
	{
		ServerRotateItems();
	}
}

bool UTN_InventoryComponent::TryConsumeEquippedItem(FTN_InventoryItem& OutConsumedItem)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	return ConsumeEquippedInternal(OutConsumedItem);
}

bool UTN_InventoryComponent::TryExtractEquippedItem(FTN_InventoryItem& OutExtractedItem)
{
	return TryConsumeEquippedItem(OutExtractedItem);
}

bool UTN_InventoryComponent::TryConsumeItemByUseType(ETN_ItemUseType InUseType, FTN_InventoryItem& OutConsumedItem)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	using namespace TNInventoryLogic;

	const bool bEquippedMatches = bHasEquippedItem && EquippedItem.UseType == InUseType;
	const bool bStoredMatches   = bHasStoredItem && StoredItem.UseType == InUseType;

	switch (DecideConsumeByUseType(bEquippedMatches, bStoredMatches))
	{
		case EUseTypeSource::Equipped:
			if (!ConsumeEquippedInternal(OutConsumedItem))
			{
				return false;
			}
			break;

		case EUseTypeSource::Stored:
			OutConsumedItem = StoredItem;
			StoredItem      = FTN_InventoryItem();
			bHasStoredItem  = false;
			// RefreshEquippedVisual no cambia aquí (slot equipado intacto)
			break;

		default:
			return false;
	}

	if (ATortugaCharacter* Char = Cast<ATortugaCharacter>(GetOwner()))
	{
		PlayInventorySfx(Char->ConsumeSound);
	}
	return true;
}

void UTN_InventoryComponent::ServerRotateItems_Implementation()
{
	// El cliente ya lo comprueba, pero con latencia pide antes de recibir que está en el caparazón o en brazos.
	if (!CanOwnerRotateItems(GetOwner()))
	{
		return;
	}
	SwapSlotsInternal();
	RefreshEquippedVisual();
}

void UTN_InventoryComponent::OnRep_EquippedItem()
{
	// Los objetos de carrera llegan sin malla ni icono (se construyen en cada máquina): se los pone el ItemId.
	TNItemRuntime::ResolveVisuals(EquippedItem);
	RefreshEquippedVisual();
}

void UTN_InventoryComponent::OnRep_StoredItem()
{
	TNItemRuntime::ResolveVisuals(StoredItem);
	// Lo guardado no se ve (está dentro del caparazón); si ha salido a la mano, lo cuenta RefreshEquippedVisual.
	RefreshEquippedVisual();
}

void UTN_InventoryComponent::OnRep_StashSerial()
{
	RefreshEquippedVisual();
}

void UTN_InventoryComponent::NoteStash(uint8 Kind)
{
	if (Kind == 0)
	{
		return;
	}
	StashKind = Kind;
	StashSerial = StashSerial >= 255 ? static_cast<uint8>(1) : static_cast<uint8>(StashSerial + 1);
}

void UTN_InventoryComponent::RefreshEquippedVisual()
{
	// Cosmético y local: lo que se ve en las aletas sigue a lo replicado (en el servidor dedicado, nada).
	if (!EquippedVisualMesh || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Los OnRep de un mismo paquete llegan después de aplicarlo entero: aquí ya están el objeto y StashSerial nuevos.
	const bool bNewStash = bVisualSynced && StashSerial != SeenStashSerial;
	SeenStashSerial = StashSerial;
	if (!bVisualSynced)
	{
		// Lo primero que llega (al aparecer o entrar a media partida) se pone sin animar.
		bVisualSynced = true;
		ShowEquippedNow(false);
		return;
	}
	if (bNewStash)
	{
		// Algo ha entrado o salido del caparazón: la aleta va a la espalda. Si sale lo guardado porque se ha gastado lo de
		// la mano (lanzarlo o comérselo: bit 4), un momento después, cuando la aleta ha acabado.
		StartStash(StashKind, (StashKind & 4) != 0 ? 0.3f : 0.f);
		return;
	}
	if (StashTime > -50.f)
	{
		// Guardando o sacando: lo que haya en la mano sale al llegar la aleta a la espalda.
		return;
	}
	const UStaticMesh* WantMesh = bHasEquippedItem ? EquippedItem.EquippedMesh.Get() : nullptr;
	const FName WantId = bHasEquippedItem ? EquippedItem.ItemId : NAME_None;
	if (WantMesh != ShownMesh.Get() || WantId != ShownItemId || (WantMesh && (!EquippedItem.EquippedMeshScale.Equals(ShownScale)
		|| !EquippedItem.EquippedMeshRotation.Equals(ShownRotation))))
	{
		// Recogido del suelo (aparece con un «pop») o gastado (desaparece).
		ShowEquippedNow(true);
	}
}

void UTN_InventoryComponent::ShowEquippedNow(bool bPop)
{
	const bool bHas = bHasEquippedItem && EquippedItem.EquippedMesh != nullptr;
	ShownMesh = bHas ? EquippedItem.EquippedMesh.Get() : nullptr;
	ShownScale = bHas ? EquippedItem.EquippedMeshScale : FVector::OneVector;
	ShownRotation = bHas ? EquippedItem.EquippedMeshRotation : FRotator::ZeroRotator;
	ShownItemId = bHas ? EquippedItem.ItemId : NAME_None;
	ShownWeight = bHas ? EquippedItem.ItemWeight : 0.f;
	MeasureShownItem();
	bShownPlaced = false;
	PopTime = (bPop && bHas) ? 0.f : -1.f;
	if (EquippedVisualMesh)
	{
		EquippedVisualMesh->SetStaticMesh(ShownMesh);
		EquippedVisualMesh->SetVisibility(false);
	}
}

void UTN_InventoryComponent::StartStash(uint8 Kind, float Delay)
{
	bStashIn = (Kind & 1) != 0 && ShownMesh != nullptr;
	bStashOut = (Kind & 2) != 0;
	if ((Kind & 1) == 0 && ShownMesh)
	{
		// Lo de la mano ya no está (se ha gastado): la aleta queda vacía mientras espera.
		ShownMesh = nullptr;
		ShownItemId = NAME_None;
		MeasureShownItem();
		if (EquippedVisualMesh)
		{
			EquippedVisualMesh->SetStaticMesh(nullptr);
			EquippedVisualMesh->SetVisibility(false);
		}
	}
	// Nada que animar, o no se puede (metida en el caparazón, llevando a alguien): cambia sin más.
	if ((!bStashIn && !bStashOut) || ShouldHideShownItem())
	{
		StashTime = -100.f;
		ShowEquippedNow(false);
		return;
	}
	StashTime = -FMath::Max(0.f, Delay);
	bStashSwitched = false;
	bStashInSounded = false;
	bStashOutSounded = false;
}

ETNItemHold UTN_InventoryComponent::GetShownHold() const
{
	if (!ShownMesh || ShouldHideShownItem())
	{
		return ETNItemHold::Auto;
	}
	// Guardándolo o sacándolo, en la aleta derecha (es la que va a la espalda).
	return StashTime >= 0.f ? ETNItemHold::OneFlipper : ShownHold;
}

float UTN_InventoryComponent::GetStashReach() const
{
	if (StashTime < 0.f)
	{
		return 0.f;
	}
	using namespace TNInventoryHold;
	const float X = StashTime / FMath::Max(0.2f, StashSeconds);
	if (X < ReachEnd)
	{
		return Smooth01(X / ReachEnd);
	}
	if (X < ReturnStart)
	{
		return 1.f;
	}
	return 1.f - Smooth01((X - ReturnStart) / (1.f - ReturnStart));
}

bool UTN_InventoryComponent::ShouldHideShownItem() const
{
	const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner());
	if (!Turtle)
	{
		return false;
	}
	if (Turtle->IsInShell() || Turtle->IsDead())
	{
		return true;
	}
	// Llevando a otra tortuga en alto (las dos aletas ocupadas) o llevada.
	const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	return Carry && (Carry->IsCarrying() || Carry->IsBeingCarried());
}

void UTN_InventoryComponent::MeasureShownItem()
{
	ShownHold = ETNItemHold::Auto;
	ShownHalfSize = FVector::ZeroVector;
	ShownLongAxis = 2;
	ShownCenter = FVector::ZeroVector;
	if (!ShownMesh)
	{
		return;
	}

	// Medio tamaño en el mundo (cm) en los ejes del objeto tras su giro fino: la caja de la malla escalada y girada.
	const FBoxSphereBounds Bounds = ShownMesh->GetBounds();
	const FVector SafeScale(FMath::Max(FMath::Abs(ShownScale.X), 0.01), FMath::Max(FMath::Abs(ShownScale.Y), 0.01), FMath::Max(FMath::Abs(ShownScale.Z), 0.01));
	const FVector HalfLocal = Bounds.BoxExtent * SafeScale;
	const FQuat Fine = ShownRotation.Quaternion();
	ShownHalfSize = Fine.RotateVector(FVector(HalfLocal.X, 0.0, 0.0)).GetAbs()
		+ Fine.RotateVector(FVector(0.0, HalfLocal.Y, 0.0)).GetAbs()
		+ Fine.RotateVector(FVector(0.0, 0.0, HalfLocal.Z)).GetAbs();
	ShownCenter = Bounds.Origin;
	ShownLongAxis = ShownHalfSize.X >= ShownHalfSize.Y ? (ShownHalfSize.X >= ShownHalfSize.Z ? 0 : 2) : (ShownHalfSize.Y >= ShownHalfSize.Z ? 1 : 2);

	// Cómo se lleva: el de HoldOverrides o por tamaño (alargado por un extremo; grande o pesado abrazado; si no, en una aleta).
	if (const ETNItemHold* Forced = HoldOverrides.Find(ShownItemId))
	{
		if (*Forced != ETNItemHold::Auto)
		{
			ShownHold = *Forced;
		}
	}
	if (ShownHold == ETNItemHold::Auto)
	{
		const double Longest = 2.0 * ShownHalfSize[ShownLongAxis];
		const double Other1 = 2.0 * ShownHalfSize[(ShownLongAxis + 1) % 3];
		const double Other2 = 2.0 * ShownHalfSize[(ShownLongAxis + 2) % 3];
		const double Second = FMath::Max(Other1, Other2);
		if (Longest >= ByEndMinLength && Longest >= ByEndMinRatio * FMath::Max(Second, 1.0))
		{
			ShownHold = ETNItemHold::ByEnd;
		}
		else if (Longest >= HugMinSize || ShownWeight >= HugMinWeight)
		{
			ShownHold = ETNItemHold::Hug;
		}
		else
		{
			ShownHold = ETNItemHold::OneFlipper;
		}
	}
	// Apertura de partida para abrazarlo (luego se ajusta sola midiendo las manos): unos 20 cm juntas, 60 muy abiertas.
	HugOpen = FMath::Clamp((static_cast<float>(ShownHalfSize.Y) * 2.f - 20.f) / 40.f, 0.f, 1.f);
}

void UTN_InventoryComponent::PlayStashSound(bool bIn) const
{
	if (const AActor* Owner = GetOwner())
	{
		if (UTN_TurtleFoleyComponent* Foley = Owner->FindComponentByClass<UTN_TurtleFoleyComponent>())
		{
			Foley->PlayStash(bIn);
		}
	}
}

void UTN_InventoryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!EquippedVisualMesh)
	{
		return;
	}
	using namespace TNInventoryHold;
	const float Dt = FMath::Max(DeltaTime, 0.f);
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;

	// Guardar o sacar del caparazón: sonidos y cambio de objeto en su momento.
	if (StashTime > -50.f)
	{
		StashTime += Dt;
		if (StashTime >= 0.f)
		{
			const float X = StashTime / FMath::Max(0.2f, StashSeconds);
			if (bStashIn && !bStashInSounded && X >= InSound)
			{
				bStashInSounded = true;
				PlayStashSound(true);
			}
			if (!bStashSwitched && X >= SwitchAt)
			{
				// Lo de la mano ya está dentro: sale lo que haya ahora en la ranura de la mano (o nada).
				bStashSwitched = true;
				ShowEquippedNow(false);
				bStashOut = ShownMesh != nullptr;
			}
			if (bStashSwitched && bStashOut && !bStashOutSounded && X >= OutSound)
			{
				bStashOutSounded = true;
				PlayStashSound(false);
			}
			if (X >= 1.f)
			{
				// Acabado: si mientras tanto ha cambiado lo de la mano, se pone ya.
				StashTime = -100.f;
				RefreshEquippedVisual();
			}
		}
	}
	if (PopTime >= 0.f)
	{
		PopTime += Dt;
		if (PopTime > PopSeconds)
		{
			PopTime = -1.f;
		}
	}
	// En el caparazón (o con las aletas ocupadas) encoge y desaparece; al salir, vuelve.
	VisibleAlpha = FMath::FInterpConstantTo(VisibleAlpha, ShouldHideShownItem() ? 0.f : 1.f, Dt, 8.f);

	if (!ShownMesh)
	{
		if (EquippedVisualMesh->IsVisible())
		{
			EquippedVisualMesh->SetVisibility(false);
		}
		return;
	}
	PlaceShownItem(Body, Dt, !bShownPlaced);
}

void UTN_InventoryComponent::PlaceShownItem(USkeletalMeshComponent* Body, float DeltaTime, bool bSnap)
{
	using namespace TNInventoryHold;

	// Escala de aparecer, esconderse y entrar o salir del caparazón.
	float Grow = VisibleAlpha;
	if (PopTime >= 0.f)
	{
		const float P = FMath::Clamp(PopTime / PopSeconds, 0.f, 1.f);
		Grow *= Smooth01(P) * (1.f + 0.15f * FMath::Sin(P * PI));
	}
	const bool bStashing = StashTime >= 0.f;
	if (bStashing)
	{
		const float X = StashTime / FMath::Max(0.2f, StashSeconds);
		if (!bStashSwitched)
		{
			Grow *= bStashIn ? 1.f - Smooth01((X - InShrinkStart) / (InShrinkEnd - InShrinkStart)) : 0.f;
		}
		else
		{
			Grow *= Smooth01((X - OutGrowStart) / (OutGrowEnd - OutGrowStart));
		}
	}
	const bool bVisible = Grow > 0.01f;
	if (EquippedVisualMesh->IsVisible() != bVisible)
	{
		EquippedVisualMesh->SetVisibility(bVisible);
	}
	if (!bVisible)
	{
		return;
	}

	// Malla sin los huesos de las aletas: el socket de siempre (o la raíz), con su tamaño en el mundo.
	const bool bHasFlippers = Body && Body->GetBoneIndex(RightHandBone) != INDEX_NONE && Body->GetBoneIndex(RightForeArmBone) != INDEX_NONE;
	if (!bHasFlippers)
	{
		USceneComponent* Parent = VisualMeshParent ? VisualMeshParent.Get() : static_cast<USceneComponent*>(Body);
		if (!Parent)
		{
			return;
		}
		const bool bSocket = EquippedAttachSocket != NAME_None && Parent->DoesSocketExist(EquippedAttachSocket);
		const FName Socket = bSocket ? EquippedAttachSocket : NAME_None;
		if (EquippedVisualMesh->GetAttachParent() != Parent || EquippedVisualMesh->GetAttachSocketName() != Socket)
		{
			EquippedVisualMesh->AttachToComponent(Parent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
		}
		const FVector ParentScale = Parent->GetComponentScale();
		const FVector WorldScale = ShownScale * static_cast<double>(Grow);
		const FVector RelScale(ParentScale.X > KINDA_SMALL_NUMBER ? WorldScale.X / ParentScale.X : WorldScale.X,
			ParentScale.Y > KINDA_SMALL_NUMBER ? WorldScale.Y / ParentScale.Y : WorldScale.Y,
			ParentScale.Z > KINDA_SMALL_NUMBER ? WorldScale.Z / ParentScale.Z : WorldScale.Z);
		EquippedVisualMesh->SetRelativeTransform(FTransform(bSocket ? ShownRotation : EquippedRelativeRotation + ShownRotation,
			bSocket ? FVector::ZeroVector : EquippedRelativeLocation, RelScale));
		ShownBone = NAME_None;
		bShownPlaced = true;
		return;
	}

	// Cómo se lleva ahora (guardándolo o sacándolo, en la aleta derecha) y de qué hueso cuelga: de la aleta derecha o,
	// abrazado, del pecho (las dos aletas lo rodean).
	const bool bHasHug = Body->GetBoneIndex(LeftHandBone) != INDEX_NONE && Body->GetBoneIndex(ChestBone) != INDEX_NONE;
	ETNItemHold Hold = bStashing ? ETNItemHold::OneFlipper : ShownHold;
	if (Hold == ETNItemHold::Hug && !bHasHug)
	{
		Hold = ETNItemHold::OneFlipper;
	}
	const FName WantBone = Hold == ETNItemHold::Hug ? ChestBone : RightHandBone;
	if (ShownBone != WantBone || EquippedVisualMesh->GetAttachParent() != Body)
	{
		// Al cambiar de hueso se queda donde estaba en el mundo y se desliza a su sitio nuevo (sin saltos).
		EquippedVisualMesh->AttachToComponent(Body, bShownPlaced ? FAttachmentTransformRules::KeepWorldTransform
			: FAttachmentTransformRules::SnapToTargetNotIncludingScale, WantBone);
		ShownBone = WantBone;
		ShownRelative = EquippedVisualMesh->GetRelativeTransform();
	}

	// Todo en el espacio de la malla (sin la escala del personaje): mira a +Y, arriba +Z, su derecha -X.
	const FVector Forward(0.0, 1.0, 0.0);
	const FVector Up(0.0, 0.0, 1.0);
	const FVector RightSide(-1.0, 0.0, 0.0);
	const FVector BodyScale = Body->GetComponentScale();
	const double MeshScale = FMath::Max(0.01, (FMath::Abs(BodyScale.X) + FMath::Abs(BodyScale.Y) + FMath::Abs(BodyScale.Z)) / 3.0);
	const FVector Half = ShownHalfSize / MeshScale;

	const FVector Hand = Body->GetSocketTransform(RightHandBone, RTS_Component).GetLocation();
	FVector Along = Hand - Body->GetSocketTransform(RightForeArmBone, RTS_Component).GetLocation();
	if (!Along.Normalize())
	{
		Along = Forward;
	}
	const FVector Grip = Hand + Along * static_cast<double>(GripAlong);

	// De pie y de frente: su eje X hacia delante y su Z hacia arriba (como está en el suelo); encima, su giro fino.
	const FQuat Upright = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
	FQuat HoldRot = Upright;
	FVector Center = Grip;
	switch (Hold)
	{
	case ETNItemHold::Hug:
	{
		// Entre las dos aletas, por delante de la tripa, un poco hundido en los brazos.
		const FVector LeftHand = Body->GetSocketTransform(LeftHandBone, RTS_Component).GetLocation();
		const FVector Chest = Body->GetSocketTransform(ChestBone, RTS_Component).GetLocation();
		Center = (LeftHand + Hand) * 0.5;
		Center.Y = FMath::Max(Center.Y, Chest.Y + static_cast<double>(ChestFront) + Half.X);
		Center.Z -= Half.Z * 0.15;
		// Las aletas se abren o se cierran solas hasta rodearlo (lo lee la animación): hueco entre las manos = su ancho.
		const UTN_TurtleAnimInstance* HugAnim = Cast<UTN_TurtleAnimInstance>(Body->GetAnimInstance());
		if (HugAnim && HugAnim->GetHoldWeight() > 0.8f)
		{
			const double Gap = FVector::Dist(LeftHand, Hand);
			const double Want = 2.0 * Half.Y + 2.0 * static_cast<double>(FlipperThickness);
			const float Error = static_cast<float>(FMath::Clamp((Want - Gap) / 15.0, -1.0, 1.0));
			HugOpen = FMath::Clamp(HugOpen + Error * DeltaTime * 2.5f, 0.f, 1.f);
		}
		break;
	}
	case ETNItemHold::ByEnd:
	{
		// Cogido por un extremo: apunta hacia delante y arriba, algo abierto hacia fuera, con la punta de atrás en la aleta.
		const double PitchRad = FMath::DegreesToRadians(static_cast<double>(ByEndPitchDeg));
		const FVector Point = (Forward * FMath::Cos(PitchRad) + Up * FMath::Sin(PitchRad) + RightSide * static_cast<double>(ByEndOutward)).GetSafeNormal();
		switch (ShownLongAxis)
		{
		case 0: HoldRot = FRotationMatrix::MakeFromXZ(Point, Up).ToQuat(); break;
		case 1: HoldRot = FRotationMatrix::MakeFromYZ(Point, Up).ToQuat(); break;
		default: HoldRot = FRotationMatrix::MakeFromZX(Point, Forward).ToQuat(); break;
		}
		const double HalfLength = Half[ShownLongAxis];
		Center = Grip + Point * (HalfLength - FMath::Min(HalfLength * 0.25, 3.0));
		break;
	}
	default:
		// En una aleta: apoyado encima (guardándolo o sacándolo, dentro de la aleta).
		Center = bStashing ? Grip : Grip + Up * (static_cast<double>(FlipperThickness) + Half.Z * 0.85);
		break;
	}

	// El pivote de la malla no tiene por qué ser su centro: se coloca de forma que su centro caiga en Center.
	const FQuat ItemRot = (HoldRot * ShownRotation.Quaternion()).GetNormalized();
	const FVector ItemScale = ShownScale * static_cast<double>(Grow) / MeshScale;
	const FVector Location = Center - ItemRot.RotateVector(ShownCenter * ItemScale);
	const FTransform Desired(ItemRot, Location, ItemScale);

	// Respecto al hueso (sin su escala: si el brazo encoge al meterse en el caparazón, el objeto encoge con él).
	FTransform BoneCS = Body->GetSocketTransform(WantBone, RTS_Component);
	BoneCS.SetScale3D(FVector::OneVector);
	const FTransform Target = Desired.GetRelativeTransform(BoneCS);

	// Con los brazos sujetándolo (o guardándolo) va a su sitio; si la aleta hace otra cosa (nadar, un emote, el panzazo),
	// se queda como estaba en ella y la sigue.
	const UTN_TurtleAnimInstance* TurtleAnim = Cast<UTN_TurtleAnimInstance>(Body->GetAnimInstance());
	const bool bFollowPose = bSnap || bStashing || !TurtleAnim || TurtleAnim->GetHoldWeight() >= 0.6f;
	if (bFollowPose)
	{
		const float K = bSnap ? 1.f : 1.f - FMath::Exp(-DeltaTime * (bStashing ? 18.f : 14.f));
		ShownRelative.SetLocation(FMath::Lerp(ShownRelative.GetLocation(), Target.GetLocation(), static_cast<double>(K)));
		ShownRelative.SetRotation(FQuat::Slerp(ShownRelative.GetRotation(), Target.GetRotation(), static_cast<double>(K)).GetNormalized());
	}
	ShownRelative.SetScale3D(Target.GetScale3D());
	EquippedVisualMesh->SetRelativeTransform(ShownRelative);
	bShownPlaced = true;
}

bool UTN_InventoryComponent::AddItemInternal(const FTN_InventoryItem& NewItem)
{
	using namespace TNInventoryLogic;

	// Objetos del coop: el mismo que ya se lleva se suma a su hueco (o no se coge si está al máximo).
	int32 StackSlot = INDEX_NONE;
	FTN_InventoryItem Merged;
	const ETNCoopStack Stack = static_cast<ETNCoopStack>(DecideCoopStack(NewItem, StackSlot, Merged));
	if (Stack == ETNCoopStack::Full)
	{
		return false;
	}
	if (Stack == ETNCoopStack::Merge)
	{
		TNCoopItems::ResolveVisuals(Merged);
		if (StackSlot == 0)
		{
			EquippedItem = Merged;
			RefreshEquippedVisual();
		}
		else
		{
			StoredItem = Merged;
		}
		if (ATortugaCharacter* Char = Cast<ATortugaCharacter>(GetOwner()))
		{
			PlayInventorySfx(Char->PickupSound);
		}
		return true;
	}

	switch (DecideAddSlot(bHasEquippedItem, bHasStoredItem))
	{
		case EAddDecision::ToEquipped:
			EquippedItem = NewItem;
			bHasEquippedItem = true;
			TNItemRuntime::ResolveVisuals(EquippedItem);
			RefreshEquippedVisual();
			break;
		case EAddDecision::ToStored:
			StoredItem = NewItem;
			bHasStoredItem = true;
			TNItemRuntime::ResolveVisuals(StoredItem);
			break;
		default:
			return false;
	}

	if (ATortugaCharacter* Char = Cast<ATortugaCharacter>(GetOwner()))
	{
		PlayInventorySfx(Char->PickupSound);
	}
	return true;
}

bool UTN_InventoryComponent::AddOrReplaceEquippedInternal(const FTN_InventoryItem& NewItem, bool bReplaceIfFull)
{
	using namespace TNInventoryLogic;

	// Un objeto del coop que ya se lleva se apila (o no se coge) en vez de ocupar otro hueco o sustituir lo de la mano.
	int32 StackSlot = INDEX_NONE;
	FTN_InventoryItem Merged;
	if (static_cast<ETNCoopStack>(DecideCoopStack(NewItem, StackSlot, Merged)) != ETNCoopStack::Separate)
	{
		return AddItemInternal(NewItem);
	}

	const EAddDecision Decision = DecideAddOrReplace(bHasEquippedItem, bHasStoredItem, bReplaceIfFull);
	if (Decision == EAddDecision::ToEquipped || Decision == EAddDecision::ToStored)
	{
		// La ocupación no cambia entre las dos decisiones → AddItemInternal elige el mismo slot.
		return AddItemInternal(NewItem);
	}

	if (Decision != EAddDecision::ReplaceEquipped)
	{
		return false;
	}

	EquippedItem = NewItem;
	bHasEquippedItem = true;
	TNItemRuntime::ResolveVisuals(EquippedItem);
	RefreshEquippedVisual();
	if (ATortugaCharacter* Char = Cast<ATortugaCharacter>(GetOwner()))
	{
		PlayInventorySfx(Char->PickupSound);
	}
	return true;
}

bool UTN_InventoryComponent::ConsumeEquippedInternal(FTN_InventoryItem& OutItem)
{
	using namespace TNInventoryLogic;

	const EConsumeDecision Decision = DecideConsumeEquipped(bHasEquippedItem, bHasStoredItem);
	if (Decision == EConsumeDecision::Rejected)
	{
		return false;
	}

	OutItem = EquippedItem;

	if (Decision == EConsumeDecision::ConsumeAndPromoteStored)
	{
		EquippedItem = StoredItem;
		bHasEquippedItem = true;
		StoredItem = FTN_InventoryItem();
		bHasStoredItem = false;
		// Lo guardado sale del caparazón a la aleta, después de gastar lo que había en ella.
		NoteStash(2 | 4);
	}
	else
	{
		EquippedItem = FTN_InventoryItem();
		bHasEquippedItem = false;
	}

	RefreshEquippedVisual();
	return true;
}

uint8 UTN_InventoryComponent::DecideCoopStack(const FTN_InventoryItem& NewItem, int32& OutSlot, FTN_InventoryItem& OutMerged) const
{
	OutSlot = INDEX_NONE;
	if (NewItem.UseType != ETN_ItemUseType::CoopItem)
	{
		return static_cast<uint8>(ETNCoopStack::Separate);
	}
	// La mano primero, luego el caparazón: el primer hueco con el mismo objeto decide.
	const FTN_InventoryItem* const Slots[2] = { bHasEquippedItem ? &EquippedItem : nullptr, bHasStoredItem ? &StoredItem : nullptr };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const ETNCoopStack Decision = Slots[Index] ? TNCoopItems::DecideStack(*Slots[Index], NewItem, OutMerged) : ETNCoopStack::Separate;
		if (Decision != ETNCoopStack::Separate)
		{
			OutSlot = Index;
			return static_cast<uint8>(Decision);
		}
	}
	return static_cast<uint8>(ETNCoopStack::Separate);
}

void UTN_InventoryComponent::SwapSlotsInternal()
{
	if (!TNInventoryLogic::ShouldSwapSlots(bHasEquippedItem, bHasStoredItem))
	{
		return;
	}

	// Lo de la mano entra en el caparazón (si había) y lo guardado sale (si había): cada máquina lo anima.
	const uint8 Kind = static_cast<uint8>((bHasEquippedItem ? 1 : 0) | (bHasStoredItem ? 2 : 0));
	Swap(EquippedItem, StoredItem);
	Swap(bHasEquippedItem, bHasStoredItem);
	NoteStash(Kind);
}

void UTN_InventoryComponent::PlayInventorySfx(USoundBase* Sound) const
{
	if (ATortugaCharacter* Char = Cast<ATortugaCharacter>(GetOwner()))
	{
		if (Sound) { Char->MulticastPlaySfx(Sound); }
	}
}

