// Objetos de Todos contra Todos: catálogo (filas del inventario, mallas e iconos). El uso está en TN_TctItemUse.cpp.

#include "Game/TN_TctItems.h"
#include "TN_TctItemArt.h"
#include "TN_TctItemMeshes.h"
#include "Core/TN_Log.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_RaceItemComponent.h"
#include "World/Beach/TN_RaceItems.h"
#include "World/TN_ConchPickup.h"
#include "World/TN_InkProjectile.h"
#include "World/TN_TctItemPad.h"
#include "World/TN_ThrowableItemActor.h"

namespace TNTctItemsDetail
{
	/** Carpeta de la biblioteca IA importada (#600). */
	const TCHAR* const IaRoot = TEXT("/Game/Art/IA/todos_contra_todos");

	/** Malla IA (carpeta y nombre) de cada objeto de código, en la mano y como proyectil. */
	struct FMeshInfo
	{
		const TCHAR* Folder;
		const TCHAR* Held;
		const TCHAR* Projectile;
		/** Forma básica del motor si falta la IA. */
		const TCHAR* Fallback;
	};

	bool GetMeshInfo(ETNTctItem Kind, FMeshInfo& Out)
	{
		switch (Kind)
		{
		case ETNTctItem::KnockoutPistol: Out = { TEXT("pistola_noqueo"), TEXT("SM_TN_PistolaNoqueo"), nullptr, TEXT("Cube") }; return true;
		case ETNTctItem::AirBlunderbuss: Out = { TEXT("trabuco_aire"), TEXT("SM_TN_TrabucoAire"), nullptr, TEXT("Cone") }; return true;
		case ETNTctItem::Grapple:        Out = { TEXT("garfio_ancla"), TEXT("SM_TN_Garfio"), nullptr, TEXT("Cylinder") }; return true;
		case ETNTctItem::Shovel:         Out = { TEXT("pala_mano"), TEXT("SM_TN_PalaMano"), nullptr, TEXT("Cube") }; return true;
		case ETNTctItem::BeachBall:      Out = { TEXT("balon_playa"), TEXT("SM_TN_BalonPlaya"), TEXT("SM_TN_BalonPlaya"), TEXT("Sphere") }; return true;
		case ETNTctItem::Anchor:         Out = { TEXT("garfio_ancla"), TEXT("SM_TN_Ancla"), TEXT("SM_TN_Ancla"), TEXT("Cube") }; return true;
		case ETNTctItem::JellyDart:      Out = { TEXT("pistola_noqueo"), TEXT("SM_TN_DardoMedusa"), TEXT("SM_TN_DardoMedusa"), TEXT("Cone") }; return true;
		case ETNTctItem::InkPistol:      Out = { TEXT("pistola_tinta"), TEXT("SM_TN_PistolaTinta"), nullptr, TEXT("Cube") }; return true;
		default: return false;
		}
	}

	FString AssetPath(const TCHAR* Folder, const TCHAR* Name)
	{
		return FString::Printf(TEXT("%s/%s/%s.%s"), IaRoot, Folder, Name, Name);
	}

	bool CanRender()
	{
		return !IsRunningDedicatedServer() && FApp::CanEverRender();
	}

	/** Mallas ya buscadas (las que no están en el proyecto, como null: no se vuelven a buscar en disco). */
	TMap<FString, TWeakObjectPtr<UStaticMesh>>& MeshCache()
	{
		static TMap<FString, TWeakObjectPtr<UStaticMesh>> Cache;
		return Cache;
	}

	TSet<FString>& MissingMeshes()
	{
		static TSet<FString> Missing;
		return Missing;
	}

	UStaticMesh* FindOrLoadMesh(const FString& Path, bool bQuiet)
	{
		if (MissingMeshes().Contains(Path))
		{
			return nullptr;
		}
		if (const TWeakObjectPtr<UStaticMesh>* Found = MeshCache().Find(Path))
		{
			if (UStaticMesh* Mesh = Found->Get())
			{
				return Mesh;
			}
		}
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, bQuiet ? (LOAD_NoWarn | LOAD_Quiet) : LOAD_None);
		if (Mesh)
		{
			MeshCache().Add(Path, Mesh);
		}
		else
		{
			MissingMeshes().Add(Path);
			UE_LOG(LogTortunabo, Log, TEXT("[TcT] %s no está en el proyecto: se usa una forma básica."), *Path);
		}
		return Mesh;
	}

	/**
	 * La fila de DT_Items del UseType Use (la primera cuyo nombre contiene Hint, si la hay; si no, la primera de ese uso) con
	 * lo que hace falta para usarla. null si no hay.
	 */
	const FTN_InventoryItem* FindCatalogRow(ETN_ItemUseType Use, const TCHAR* Hint)
	{
		const UDataTable* Catalog = TNRaceItems::LoadCatalog();
		if (!Catalog || !Catalog->GetRowStruct() || !Catalog->GetRowStruct()->IsChildOf(FTN_InventoryItem::StaticStruct()))
		{
			return nullptr;
		}
		const FTN_InventoryItem* First = nullptr;
		const FTN_InventoryItem* Hinted = nullptr;
		Catalog->ForeachRow<FTN_InventoryItem>(TEXT("TNTctItems::FindCatalogRow"),
			[Use, Hint, &First, &Hinted](const FName& RowName, const FTN_InventoryItem& Row)
			{
				if (!Row.IsValid() || Row.UseType != Use || !Row.PickupActorClass)
				{
					return;
				}
				if (Use == ETN_ItemUseType::Throwable && !Row.ThrowableData.ActorClass) { return; }
				if (Use == ETN_ItemUseType::Conch && !Row.ConchData.ActorClass) { return; }
				if (Use == ETN_ItemUseType::InkThrower && !Row.InkData.ProjectileClass) { return; }
				First = First ? First : &Row;
				if (!Hinted && Hint && RowName.ToString().Contains(Hint))
				{
					Hinted = &Row;
				}
			});
		return Hinted ? Hinted : First;
	}

	const FTN_InventoryItem* CatalogRowFor(ETNTctItem Kind)
	{
		switch (Kind)
		{
		case ETNTctItem::Ball:      return FindCatalogRow(ETN_ItemUseType::Throwable, TEXT("Ball"));
		case ETNTctItem::ConchTrap: return FindCatalogRow(ETN_ItemUseType::Conch, TEXT("Conch"));
		case ETNTctItem::BigHead:   return FindCatalogRow(ETN_ItemUseType::BigHead, TEXT("Head"));
		case ETNTctItem::InkPistol: return FindCatalogRow(ETN_ItemUseType::InkThrower, TEXT("Ink"));
		default:                    return nullptr;
		}
	}

	ETNRaceItem RaceKindFor(ETNTctItem Kind)
	{
		switch (Kind)
		{
		case ETNTctItem::SandMine: return ETNRaceItem::SandMine;
		case ETNTctItem::Frisbee:  return ETNRaceItem::Frisbee;
		default:                   return ETNRaceItem::None;
		}
	}
}

FText TNTctItems::DisplayName(ETNTctItem Kind)
{
	switch (Kind)
	{
	case ETNTctItem::KnockoutPistol: return NSLOCTEXT("TNTct", "ItemKnockoutPistol", "Pistola de noqueo");
	case ETNTctItem::AirBlunderbuss: return NSLOCTEXT("TNTct", "ItemAirBlunderbuss", "Trabuco de aire");
	case ETNTctItem::Grapple:        return NSLOCTEXT("TNTct", "ItemGrapple", "Garfio");
	case ETNTctItem::Shovel:         return NSLOCTEXT("TNTct", "ItemShovel", "Pala");
	case ETNTctItem::BeachBall:      return NSLOCTEXT("TNTct", "ItemBeachBall", "Balón de playa");
	case ETNTctItem::Anchor:         return NSLOCTEXT("TNTct", "ItemAnchor", "Ancla");
	case ETNTctItem::JellyDart:      return NSLOCTEXT("TNTct", "ItemJellyDart", "Dardo de medusa");
	case ETNTctItem::InkPistol:      return NSLOCTEXT("TNTct", "ItemInkPistol", "Pistola de tinta");
	case ETNTctItem::Ball:           return NSLOCTEXT("TNTct", "ItemBall", "Bola");
	case ETNTctItem::ConchTrap:      return NSLOCTEXT("TNTct", "ItemConchTrap", "Concha trampa");
	case ETNTctItem::BigHead:        return NSLOCTEXT("TNTct", "ItemBigHead", "Cabezota");
	case ETNTctItem::SandMine:       return TNRaceItems::DisplayName(ETNRaceItem::SandMine);
	case ETNTctItem::Frisbee:        return TNRaceItems::DisplayName(ETNRaceItem::Frisbee);
	case ETNTctItem::Cocobomba:      return NSLOCTEXT("TNTct", "ItemCocobomba", "Cocobomba");
	case ETNTctItem::Alga:           return NSLOCTEXT("TNTct", "ItemAlga", "Charco de alga");
	case ETNTctItem::GaviotaLadrona: return NSLOCTEXT("TNTct", "ItemGaviotaLadrona", "Gaviota ladrona");
	default:                         return NSLOCTEXT("TNRace", "ItemUnknown", "Objeto");
	}
}

ETNTctItem TNTctItems::KindOf(const FTN_InventoryItem& Item)
{
	ETNTctItem Kind = ETNTctItem::None;
	int32 Charges = 0;
	if (Item.UseType != ETN_ItemUseType::TctItem || !TNTctItemRules::ParseItemId(Item.ItemId, Kind, Charges))
	{
		return ETNTctItem::None;
	}
	return Kind;
}

int32 TNTctItems::ChargesOf(const FTN_InventoryItem& Item)
{
	ETNTctItem Kind = ETNTctItem::None;
	int32 Charges = 0;
	return (Item.UseType == ETN_ItemUseType::TctItem && TNTctItemRules::ParseItemId(Item.ItemId, Kind, Charges)) ? Charges : 0;
}

FString TNTctItems::MeshPath(ETNTctItem Kind, bool bProjectile)
{
	TNTctItemsDetail::FMeshInfo Info;
	if (!TNTctItemsDetail::GetMeshInfo(Kind, Info))
	{
		return FString();
	}
	const TCHAR* Name = bProjectile ? Info.Projectile : Info.Held;
	return Name ? TNTctItemsDetail::AssetPath(Info.Folder, Name) : FString();
}

FVector TNTctItems::MeshScale(ETNTctItem Kind, bool bProjectile, bool bFallback)
{
	// Mallas propias en ejecución (TNTctItemMeshes): ya van a su tamaño.
	switch (Kind)
	{
	case ETNTctItem::Cocobomba:      return bProjectile ? FVector(1.3f) : FVector(1.f);
	case ETNTctItem::Alga:           return FVector(1.f);
	case ETNTctItem::GaviotaLadrona: return FVector(1.f);
	default: break;
	}
	if (bFallback)
	{
		// Formas del motor de 100 uu: al tamaño aproximado de la malla IA.
		switch (Kind)
		{
		case ETNTctItem::BeachBall:      return FVector(0.5f);
		case ETNTctItem::Anchor:         return bProjectile ? FVector(0.5f) : FVector(0.3f);
		case ETNTctItem::JellyDart:      return FVector(0.08f, 0.08f, 0.2f);
		case ETNTctItem::Shovel:         return FVector(0.7f, 0.2f, 0.05f);
		case ETNTctItem::AirBlunderbuss: return FVector(0.25f, 0.25f, 0.6f);
		default:                         return FVector(0.3f, 0.12f, 0.25f);
		}
	}
	switch (Kind)
	{
	case ETNTctItem::Anchor:    return bProjectile ? FVector(2.f) : FVector(1.4f);
	case ETNTctItem::JellyDart: return bProjectile ? FVector(1.6f) : FVector(1.4f);
	case ETNTctItem::BeachBall: return bProjectile ? FVector(1.f) : FVector(0.8f);
	default:                    return FVector(1.f);
	}
}

UStaticMesh* TNTctItems::LoadMesh(ETNTctItem Kind, bool bProjectile)
{
	if (!TNTctItemsDetail::CanRender())
	{
		return nullptr;
	}
	if (UStaticMesh* Own = TNTctItemMeshes::ForKind(Kind))
	{
		return Own;
	}
	const FString Path = MeshPath(Kind, bProjectile);
	if (!Path.IsEmpty())
	{
		// Ruta blanda: la malla IA puede no estar aún en el proyecto (llega con #600); entonces, la forma básica.
		if (UStaticMesh* Mesh = TNTctItemsDetail::FindOrLoadMesh(Path, true))
		{
			return Mesh;
		}
	}
	TNTctItemsDetail::FMeshInfo Info;
	const TCHAR* Shape = TNTctItemsDetail::GetMeshInfo(Kind, Info) ? Info.Fallback : TEXT("Sphere");
	return TNTctItemsDetail::FindOrLoadMesh(FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Shape, Shape), false);
}

void TNTctItems::PreloadMeshes()
{
	for (const ETNTctItem Kind : TNTctItemRules::AllKinds())
	{
		if (TNTctItemRules::Spec(Kind).Source == ETNTctItemSource::Code)
		{
			LoadMesh(Kind, false);
			LoadMesh(Kind, true);
		}
	}
}

bool TNTctItems::MakeItem(ETNTctItem Kind, FTN_InventoryItem& OutItem)
{
	OutItem = FTN_InventoryItem();
	const FTNTctItemSpec& Spec = TNTctItemRules::Spec(Kind);
	if (Kind == ETNTctItem::None || Kind >= ETNTctItem::Count)
	{
		return false;
	}
	if (Spec.Source == ETNTctItemSource::Race)
	{
		OutItem = TNRaceItems::MakeItem(TNTctItemsDetail::RaceKindFor(Kind));
		return OutItem.IsValid();
	}
	if (Spec.Source == ETNTctItemSource::Catalog)
	{
		const FTN_InventoryItem* Row = TNTctItemsDetail::CatalogRowFor(Kind);
		if (!Row)
		{
			return false;
		}
		OutItem = *Row;
		return true;
	}
	// La pistola de tinta dispara el proyectil de la fila de tinta de DT_Items: sin ella no se puede dar.
	if (Kind == ETNTctItem::InkPistol && !TNTctItemsDetail::CatalogRowFor(Kind))
	{
		return false;
	}
	OutItem.ItemId = TNTctItemRules::MakeItemId(Kind, Spec.Charges);
	OutItem.UseType = ETN_ItemUseType::TctItem;
	OutItem.ItemWeight = 0.f;
	OutItem.PickupActorClass = ATN_TctItemPickup::StaticClass();
	ResolveVisuals(OutItem);
	return true;
}

TArray<ETNTctItem> TNTctItems::AvailableKinds()
{
	TArray<ETNTctItem> Out;
	for (const ETNTctItem Kind : TNTctItemRules::AllKinds())
	{
		// Lo mismo que mira MakeItem, sin montar la fila: los de DT_Items (y la pistola de tinta) necesitan su fila.
		const bool bNeedsRow = TNTctItemRules::Spec(Kind).Source == ETNTctItemSource::Catalog || Kind == ETNTctItem::InkPistol;
		if (!bNeedsRow || TNTctItemsDetail::CatalogRowFor(Kind))
		{
			Out.Add(Kind);
		}
	}
	return Out;
}

void TNTctItems::ResolveVisuals(FTN_InventoryItem& Item)
{
	const ETNTctItem Kind = KindOf(Item);
	if (Kind == ETNTctItem::None || !TNTctItemsDetail::CanRender())
	{
		return;
	}
	if (UStaticMesh* Mesh = LoadMesh(Kind, false))
	{
		const bool bFallback = Mesh->GetPathName().StartsWith(TEXT("/Engine/BasicShapes/"));
		Item.EquippedMesh = Mesh;
		Item.EquippedMeshScale = MeshScale(Kind, false, bFallback);
	}
	if (UTexture2D* Icon = TNTctItemArt::GetIcon(Kind, ChargesOf(Item)))
	{
		Item.ItemIcon = Icon;
	}
}

bool TNTctItems::GiveItem(ATortugaCharacter* Turtle, ETNTctItem Kind)
{
	if (!Turtle || !Turtle->HasAuthority())
	{
		return false;
	}
	FTN_InventoryItem Item;
	UTN_InventoryComponent* Inventory = Turtle->GetInventoryComponent();
	return Inventory && MakeItem(Kind, Item) && Inventory->TryAddOrReplaceEquipped(Item, true);
}

void TNTctItems::ClearInventory(ATortugaCharacter* Turtle)
{
	UTN_InventoryComponent* Inventory = Turtle && Turtle->HasAuthority() ? Turtle->GetInventoryComponent() : nullptr;
	if (!Inventory)
	{
		return;
	}
	FTN_InventoryItem Dropped;
	// Los dos huecos: lo de la mano y, al rotar, lo del caparazón (como mucho dos vueltas).
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		if (Inventory->HasEquippedItem())
		{
			Inventory->TryConsumeEquippedItem(Dropped);
		}
		if (Inventory->HasStoredItem())
		{
			Inventory->RotateItems();
		}
	}
}

bool TNTctItems::CanAffect(const ATortugaCharacter* Turtle, bool bPush)
{
	if (!IsValid(Turtle) || Turtle->IsDead() || !TNRaceItems::CanBeHurt(Turtle))
	{
		return false;
	}
	if (bPush && Turtle->IsInShell())
	{
		return false;
	}
	const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	return !(Carry && Carry->IsBeingCarried());
}

void TNTctItems::GatherTurtles(const UObject* WorldContext, const AActor* Except, TArray<ATortugaCharacter*>& Out)
{
	Out.Reset();
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	for (TActorIterator<ATortugaCharacter> It(World); It; ++It)
	{
		ATortugaCharacter* Turtle = *It;
		if (Turtle && Turtle != Except && !Turtle->IsDead())
		{
			Out.Add(Turtle);
		}
	}
}

void TNTctItems::PlayCue(ACharacter* Turtle, ETNRaceSound Sound, float Pitch)
{
	if (UTN_RaceItemComponent* Comp = UTN_RaceItemComponent::FindOrAddOn(Turtle))
	{
		Comp->MulticastCue(Sound, Pitch);
	}
}
