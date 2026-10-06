#include "World/Beach/TN_RaceItems.h"
#include "Game/TN_TctItemComponent.h"
#include "Game/TN_TctItems.h"
#include "Game/TN_CoopItems.h"
#include "Game/TN_CoopItemComponent.h"
#include "Core/TN_GameplayPreload.h"
#include "TN_RaceItemArt.h"
#include "World/Beach/TN_RaceItemBox.h"
#include "World/Beach/TN_RaceItemComponent.h"
#include "World/Beach/TN_RaceFrisbee.h"
#include "World/Beach/TN_RaceGullStrike.h"
#include "World/Beach/TN_RaceHomingCrab.h"
#include "World/Beach/TN_RaceMine.h"
#include "World/Beach/TN_RacePelicanTaxi.h"
#include "World/Beach/TN_RaceStormCloud.h"
#include "World/Beach/TN_RaceFishingHook.h"
#include "World/Beach/TN_RaceItemRules.h"
#include "World/Beach/TN_RaceWhirlpool.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_BeachSandWorm.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/TN_PickupInteractableBase.h"
#include "World/TN_CatalogItemVisuals.h"
#include "Core/TN_Log.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TortugaCharacter.h"
#include "UObject/SoftObjectPtr.h"

// ─────────────────────────────────────────────────────────────────────────────
// Tabla de objetos: nombres y pesos por posición
// ─────────────────────────────────────────────────────────────────────────────

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del
// bloque.
namespace TNRaceItemsDetail
{
	/** Un objeto de carrera: nombres y pesos según el puesto (primera / a medias / última) y factor del cofre. */
	struct FItemInfo
	{
		ETNRaceItem Kind;
		const TCHAR* Code;
		const TCHAR* Spanish;
		const TCHAR* AliasA;
		const TCHAR* AliasB;
		float WeightLead;
		float WeightMid;
		float WeightLast;
		float ChestFactor;
		int32 MinRacers;
	};

	/**
	 * Pesos por posición (Docs/Modo_Carrera.md, «Pesos por posición»). A los de atrás les tocan los que hacen remontar (el
	 * pelícano taxi, el protector solar, el triple coco y el dorado); a los de delante, lo que se lanza y lo defensivo (la
	 * mina, el disco, el cangrejo). La gaviota justiciera y la nube de tormenta no salen a la primera y necesitan a otra
	 * tortuga a la que fastidiar. Los triples de 2 y 1 usos nunca salen de un sorteo: son lo que queda del de 3.
	 */
	const FItemInfo Infos[] = {
		{ ETNRaceItem::Box,            TEXT("Box"),            TEXT("Caja de objetos"),      TEXT("caja"),      TEXT(""),          0.f, 0.f,  0.f,  0.f, 1 },
		{ ETNRaceItem::Coconut,        TEXT("Coconut"),        TEXT("Coco turbo"),           TEXT("coco"),      TEXT("turbo"),     2.0f, 2.2f, 1.4f, 1.0f, 1 },
		{ ETNRaceItem::TripleCoconut3, TEXT("TripleCoconut3"), TEXT("Triple coco"),          TEXT("triple"),    TEXT("tripleturbo"), 0.f, 0.8f, 1.6f, 1.4f, 1 },
		{ ETNRaceItem::TripleCoconut2, TEXT("TripleCoconut2"), TEXT("Triple coco (2)"),      TEXT("triple2"),   TEXT(""),          0.f, 0.f,  0.f,  0.f, 1 },
		{ ETNRaceItem::TripleCoconut1, TEXT("TripleCoconut1"), TEXT("Triple coco (1)"),      TEXT("triple1"),   TEXT(""),          0.f, 0.f,  0.f,  0.f, 1 },
		{ ETNRaceItem::GoldenCoconut,  TEXT("GoldenCoconut"),  TEXT("Coco dorado"),          TEXT("dorado"),    TEXT("oro"),       0.f, 0.1f, 0.9f, 2.0f, 1 },
		{ ETNRaceItem::PelicanTaxi,    TEXT("PelicanTaxi"),    TEXT("Pelícano taxi"),        TEXT("pelicano"),  TEXT("taxi"),      0.f, 0.25f, 2.6f, 1.6f, 1 },
		{ ETNRaceItem::Sunscreen,      TEXT("Sunscreen"),      TEXT("Protector solar"),      TEXT("protector"), TEXT("estrella"),  0.f, 0.5f, 2.0f, 1.4f, 1 },
		{ ETNRaceItem::HomingCrab,     TEXT("HomingCrab"),     TEXT("Cangrejo teledirigido"), TEXT("cangrejo"), TEXT("roja"),     0.7f, 1.5f, 1.0f, 1.0f, 1 },
		{ ETNRaceItem::GullStrike,     TEXT("GullStrike"),     TEXT("Gaviota justiciera"),   TEXT("gaviota"),   TEXT("azul"),      0.f, 0.1f, 1.1f, 1.0f, 2 },
		{ ETNRaceItem::SandMine,       TEXT("SandMine"),       TEXT("Mina de arena"),        TEXT("mina"),      TEXT("bomba"),     1.8f, 1.2f, 0.5f, 0.8f, 1 },
		{ ETNRaceItem::StormCloud,     TEXT("StormCloud"),     TEXT("Nube de tormenta"),     TEXT("nube"),      TEXT("rayo"),      0.f, 0.2f, 1.3f, 1.0f, 2 },
		{ ETNRaceItem::Frisbee,        TEXT("Frisbee"),        TEXT("Disco volador"),        TEXT("disco"),     TEXT("boomerang"), 1.4f, 1.3f, 0.9f, 1.0f, 1 },
		{ ETNRaceItem::Whistle,        TEXT("Whistle"),        TEXT("Silbato del sargento"), TEXT("silbato"),   TEXT("sargento"),  1.0f, 1.0f, 0.8f, 0.6f, 1 },
		// #786. La tabla sale más a medias y al final; la caña, a las de atrás (nunca a la primera: no tiene a nadie delante);
		// el remolino, a las de delante (se deja detrás); el cohete, a las últimas.
		{ ETNRaceItem::TablaSurf,      TEXT("TablaSurf"),      TEXT("Tabla de surf"),        TEXT("tabla"),     TEXT("surf"),      0.2f, 1.2f, 1.5f, 1.4f, 1 },
		{ ETNRaceItem::CanaPescar,     TEXT("CanaPescar"),     TEXT("Caña de pescar"),       TEXT("cana"),      TEXT("pescar"),    0.f,  0.9f, 1.6f, 1.2f, 2 },
		{ ETNRaceItem::Remolino,       TEXT("Remolino"),       TEXT("Remolino"),             TEXT("whirlpool"), TEXT("trampa"),    1.6f, 1.0f, 0.3f, 0.8f, 2 },
		{ ETNRaceItem::CoheteFeria,    TEXT("CoheteFeria"),    TEXT("Cohete de feria"),      TEXT("cohete"),    TEXT("rocket"),    0.f,  0.4f, 1.8f, 1.6f, 1 },
	};

	const FItemInfo* FindInfo(ETNRaceItem Kind)
	{
		for (const FItemInfo& Info : Infos)
		{
			if (Info.Kind == Kind)
			{
				return &Info;
			}
		}
		return nullptr;
	}

	/** Los ItemId de cada objeto, calculados una vez (el índice es el valor del enum). */
	const TArray<FName>& Ids()
	{
		static TArray<FName> Cached;
		if (Cached.Num() == 0)
		{
			Cached.Init(NAME_None, static_cast<int32>(ETNRaceItem::Count));
			for (const FItemInfo& Info : Infos)
			{
				Cached[static_cast<int32>(Info.Kind)] = FName(*FString::Printf(TEXT("Race_%s"), Info.Code));
			}
		}
		return Cached;
	}

	/** La posición con la que se pesa un sorteo: la cima de una fortaleza siempre pesa como la última (1), sea cual sea el puesto. */
	float EffectiveNorm(float Norm, ETNRaceLootSource Source)
	{
		return Source == ETNRaceLootSource::Summit ? 1.f : Norm;
	}

	/** Interpola el peso entre primera (0), a medias (0,5) y última (1). */
	float Blend(const FItemInfo& Info, float Norm)
	{
		const float Clamped = FMath::Clamp(Norm, 0.f, 1.f);
		return Clamped <= 0.5f ? FMath::Lerp(Info.WeightLead, Info.WeightMid, Clamped * 2.f)
			: FMath::Lerp(Info.WeightMid, Info.WeightLast, (Clamped - 0.5f) * 2.f);
	}

	/** Sin acentos ni mayúsculas para comparar lo que se escribe en la consola. */
	FString Simplify(const FString& Text)
	{
		FString Out = Text.ToLower();
		Out = Out.Replace(TEXT("í"), TEXT("i")).Replace(TEXT("é"), TEXT("e")).Replace(TEXT("á"), TEXT("a"))
			.Replace(TEXT("ó"), TEXT("o")).Replace(TEXT("ú"), TEXT("u")).Replace(TEXT(" "), TEXT("")).Replace(TEXT("_"), TEXT(""));
		return Out;
	}

	/** El pickup con el que se ve cada objeto en el suelo. */
	TSubclassOf<ATN_PickupInteractableBase> PickupClassFor(ETNRaceItem Kind)
	{
		if (Kind == ETNRaceItem::Box)
		{
			return TSubclassOf<ATN_PickupInteractableBase>(ATN_RaceItemBox::StaticClass());
		}
		return TSubclassOf<ATN_PickupInteractableBase>(ATN_PickupInteractableBase::StaticClass());
	}

	/** Suelta el rastro de un enemigo mareado por el silbato. */
	int32 WhistleEnemies(ATortugaCharacter* Turtle)
	{
		UWorld* World = Turtle ? Turtle->GetWorld() : nullptr;
		if (!World)
		{
			return 0;
		}
		const FVector At = Turtle->GetActorLocation();
		const double RadiusSq = FMath::Square(static_cast<double>(TNRaceItems::WhistleRadius));
		int32 Stunned = 0;
		for (TActorIterator<ATN_BeachEnemy> It(World); It; ++It)
		{
			ATN_BeachEnemy* Enemy = *It;
			if (!IsValid(Enemy) || !Enemy->AcceptsHitStun())
			{
				continue;
			}
			FVector CapsuleA = FVector::ZeroVector;
			FVector CapsuleB = FVector::ZeroVector;
			float CapsuleRadius = 0.f;
			FVector Nearest = Enemy->GetActorLocation();
			if (Enemy->GetHitCapsule(CapsuleA, CapsuleB, CapsuleRadius))
			{
				Nearest = FMath::ClosestPointOnSegment(At, CapsuleA, CapsuleB);
			}
			if (FVector::DistSquared(Nearest, At) > RadiusSq)
			{
				continue;
			}
			Enemy->ApplyHitStun(TNRaceItems::WhistleStunSeconds, Turtle);
			++Stunned;
		}
		return Stunned;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Catálogo
// ─────────────────────────────────────────────────────────────────────────────

FName TNRaceItems::IdOf(ETNRaceItem Item)
{
	const int32 Index = static_cast<int32>(Item);
	const TArray<FName>& All = TNRaceItemsDetail::Ids();
	return All.IsValidIndex(Index) ? All[Index] : NAME_None;
}

ETNRaceItem TNRaceItems::KindOfId(FName ItemId)
{
	if (ItemId.IsNone())
	{
		return ETNRaceItem::None;
	}
	for (const TNRaceItemsDetail::FItemInfo& Info : TNRaceItemsDetail::Infos)
	{
		if (TNRaceItemsDetail::Ids()[static_cast<int32>(Info.Kind)] == ItemId)
		{
			return Info.Kind;
		}
	}
	return ETNRaceItem::None;
}

ETNRaceItem TNRaceItems::KindOf(const FTN_InventoryItem& Item)
{
	return Item.UseType == ETN_ItemUseType::RaceItem ? KindOfId(Item.ItemId) : ETNRaceItem::None;
}

FText TNRaceItems::DisplayName(ETNRaceItem Item)
{
	// Un NSLOCTEXT por objeto (la columna «Spanish» de la tabla solo sirve para reconocer lo que se escribe en la consola).
	switch (Item)
	{
	case ETNRaceItem::Box:            return NSLOCTEXT("TNRace", "ItemBox", "Caja de objetos");
	case ETNRaceItem::Coconut:        return NSLOCTEXT("TNRace", "ItemCoconut", "Coco turbo");
	case ETNRaceItem::TripleCoconut3: return NSLOCTEXT("TNRace", "ItemTripleCoconut", "Triple coco");
	case ETNRaceItem::TripleCoconut2: return NSLOCTEXT("TNRace", "ItemTripleCoconut2", "Triple coco (2)");
	case ETNRaceItem::TripleCoconut1: return NSLOCTEXT("TNRace", "ItemTripleCoconut1", "Triple coco (1)");
	case ETNRaceItem::GoldenCoconut:  return NSLOCTEXT("TNRace", "ItemGoldenCoconut", "Coco dorado");
	case ETNRaceItem::PelicanTaxi:    return NSLOCTEXT("TNRace", "ItemPelicanTaxi", "Pelícano taxi");
	case ETNRaceItem::Sunscreen:      return NSLOCTEXT("TNRace", "ItemSunscreen", "Protector solar");
	case ETNRaceItem::HomingCrab:     return NSLOCTEXT("TNRace", "ItemHomingCrab", "Cangrejo teledirigido");
	case ETNRaceItem::GullStrike:     return NSLOCTEXT("TNRace", "ItemGullStrike", "Gaviota justiciera");
	case ETNRaceItem::SandMine:       return NSLOCTEXT("TNRace", "ItemSandMine", "Mina de arena");
	case ETNRaceItem::StormCloud:     return NSLOCTEXT("TNRace", "ItemStormCloud", "Nube de tormenta");
	case ETNRaceItem::Frisbee:        return NSLOCTEXT("TNRace", "ItemFrisbee", "Disco volador");
	case ETNRaceItem::Whistle:        return NSLOCTEXT("TNRace", "ItemWhistle", "Silbato del sargento");
	case ETNRaceItem::TablaSurf:      return NSLOCTEXT("TNRace", "ItemTablaSurf", "Tabla de surf");
	case ETNRaceItem::CanaPescar:     return NSLOCTEXT("TNRace", "ItemCanaPescar", "Caña de pescar");
	case ETNRaceItem::Remolino:       return NSLOCTEXT("TNRace", "ItemRemolino", "Remolino");
	case ETNRaceItem::CoheteFeria:    return NSLOCTEXT("TNRace", "ItemCoheteFeria", "Cohete de feria");
	default:                          return NSLOCTEXT("TNRace", "ItemUnknown", "Objeto");
	}
}

FString TNRaceItems::CodeName(ETNRaceItem Item)
{
	const TNRaceItemsDetail::FItemInfo* Info = TNRaceItemsDetail::FindInfo(Item);
	return Info ? FString(Info->Code) : FString(TEXT("None"));
}

bool TNRaceItems::ParseKind(const FString& Text, ETNRaceItem& OutKind)
{
	const FString Wanted = TNRaceItemsDetail::Simplify(Text);
	if (Wanted.IsEmpty())
	{
		return false;
	}
	if (Wanted.IsNumeric())
	{
		const int32 Value = FCString::Atoi(*Wanted);
		if (Value > static_cast<int32>(ETNRaceItem::None) && Value < static_cast<int32>(ETNRaceItem::Count))
		{
			OutKind = static_cast<ETNRaceItem>(Value);
			return true;
		}
		return false;
	}
	for (const TNRaceItemsDetail::FItemInfo& Info : TNRaceItemsDetail::Infos)
	{
		if (Wanted == TNRaceItemsDetail::Simplify(Info.Code) || Wanted == TNRaceItemsDetail::Simplify(Info.Spanish)
			|| Wanted == TNRaceItemsDetail::Simplify(Info.AliasA) || (Info.AliasB[0] != TEXT('\0') && Wanted == TNRaceItemsDetail::Simplify(Info.AliasB)))
		{
			OutKind = Info.Kind;
			return true;
		}
	}
	return false;
}

FTN_InventoryItem TNRaceItems::MakeItem(ETNRaceItem Item)
{
	FTN_InventoryItem Out;
	Out.ItemId = IdOf(Item);
	Out.UseType = ETN_ItemUseType::RaceItem;
	Out.ItemWeight = 0.f;
	Out.PickupActorClass = TNRaceItemsDetail::PickupClassFor(Item);
	return Out;
}

void TNRaceItems::ResolveVisuals(FTN_InventoryItem& Item)
{
	// Los objetos de siempre de DT_Items: icono (y malla, si la fila trae una del motor) dibujados en código (#787).
	if (TNCatalogItemVisuals::ResolveVisuals(Item))
	{
		return;
	}
	// Los de Todos contra Todos también se definen en código: el inventario y los pickups los resuelven por aquí.
	if (Item.UseType == ETN_ItemUseType::TctItem)
	{
		TNTctItems::ResolveVisuals(Item);
		return;
	}
	// Y los del cooperativo (TN_CoopItems.h).
	if (Item.UseType == ETN_ItemUseType::CoopItem)
	{
		TNCoopItems::ResolveVisuals(Item);
		return;
	}
	if (Item.UseType != ETN_ItemUseType::RaceItem || IsRunningDedicatedServer() || !FApp::CanEverRender())
	{
		return;
	}
	const ETNRaceItem Kind = KindOfId(Item.ItemId);
	if (Kind == ETNRaceItem::None)
	{
		return;
	}
	TNRaceItemArt::FHeldLook Look;
	if (TNRaceItemArt::GetHeldLook(Kind, Look) && Look.Mesh)
	{
		Item.EquippedMesh = Look.Mesh;
		Item.EquippedMeshScale = Look.Scale;
		Item.EquippedMeshRotation = Look.Rotation;
	}
	if (UTexture2D* Icon = TNRaceItemArt::GetIcon(Kind))
	{
		Item.ItemIcon = Icon;
	}
}

const TCHAR* TNRaceItems::CatalogPath()
{
	return TEXT("/Game/Blueprints/Gameplay/Items/DT_Items.DT_Items");
}

const UDataTable* TNRaceItems::LoadCatalog()
{
	// Precargado al arrancar (UTN_GameplayPreloadSubsystem): en partida solo se resuelve.
	return TNPreload::ItemCatalog();
}

// ─────────────────────────────────────────────────────────────────────────────
// Posición y sorteo
// ─────────────────────────────────────────────────────────────────────────────

double TNRaceItems::ServerNow(const UWorld* World)
{
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

void TNRaceItems::GatherRacers(const UObject* WorldContext, TArray<ATortugaCharacter*>& Out)
{
	ATN_BeachEnemy::GatherTurtles(WorldContext, Out);
}

float TNRaceItems::CourseProgress(const UObject* WorldContext, const FVector& Where)
{
	if (const ATN_BeachRaceGenerator* Generator = ATN_BeachRaceGenerator::Find(WorldContext))
	{
		return Generator->GetCourseProgress(Where);
	}
	// Sin playa (pruebas en otro mapa): a lo largo del eje X.
	return static_cast<float>(Where.X * 1.0e-5);
}

FTNRaceRank TNRaceItems::GetRank(const APawn* Pawn)
{
	FTNRaceRank Rank;
	if (!Pawn)
	{
		return Rank;
	}
	TArray<ATortugaCharacter*> Racers;
	GatherRacers(Pawn, Racers);
	const float Mine = CourseProgress(Pawn, Pawn->GetActorLocation());
	int32 Ahead = 0;
	int32 Count = 0;
	bool bSelfCounted = false;
	for (const ATortugaCharacter* Other : Racers)
	{
		if (!Other)
		{
			continue;
		}
		++Count;
		if (Other == Pawn)
		{
			bSelfCounted = true;
			continue;
		}
		if (CourseProgress(Pawn, Other->GetActorLocation()) > Mine + 1.0e-4f)
		{
			++Ahead;
		}
	}
	if (!bSelfCounted)
	{
		++Count;
	}
	Rank.Place = Ahead;
	Rank.Count = FMath::Max(1, Count);
	Rank.Norm = Rank.Count <= 1 ? 0.5f : FMath::Clamp(static_cast<float>(Ahead) / static_cast<float>(Rank.Count - 1), 0.f, 1.f);
	return Rank;
}

float TNRaceItems::PositionWeight(ETNRaceItem Item, float Norm, int32 Racers, ETNRaceLootSource Source)
{
	const TNRaceItemsDetail::FItemInfo* Info = TNRaceItemsDetail::FindInfo(Item);
	if (!Info || Racers < Info->MinRacers)
	{
		return 0.f;
	}
	// Arriba de una fortaleza vale la tabla de las últimas para cualquier puesto, con los factores del cofre.
	float Weight = TNRaceItemsDetail::Blend(*Info, TNRaceItemsDetail::EffectiveNorm(Norm, Source));
	if (Source == ETNRaceLootSource::Chest || Source == ETNRaceLootSource::Summit)
	{
		Weight *= Info->ChestFactor;
	}
	return FMath::Max(0.f, Weight);
}

float TNRaceItems::PositionWeightForUse(ETN_ItemUseType Use, float Norm, ETNRaceLootSource Source)
{
	// Tres puntos (primera / a medias / última) y factor del cofre, como en la tabla de los objetos de carrera.
	float Lead = 1.f;
	float Mid = 1.f;
	float Last = 1.f;
	float ChestFactor = 1.f;
	switch (Use)
	{
		// Para ti: energía sin fin unos segundos (más a las de atrás) o la barra llena de golpe.
		case ETN_ItemUseType::SelfStaminaBoost: Lead = 1.0f; Mid = 1.5f; Last = 1.8f; ChestFactor = 2.0f; break;
		case ETN_ItemUseType::SelfStaminaFull:  Lead = 1.4f; Mid = 1.2f; Last = 1.0f; ChestFactor = 1.6f; break;
		// Para fastidiar: la bola derriba, la tinta ciega (más a las de delante, que tienen a quién apuntar por detrás).
		case ETN_ItemUseType::Throwable:        Lead = 1.4f; Mid = 1.3f; Last = 0.9f; ChestFactor = 1.2f; break;
		case ETN_ItemUseType::InkThrower:       Lead = 1.4f; Mid = 1.3f; Last = 0.9f; ChestFactor = 1.2f; break;
		// La concha trampa se deja atrás: es para quien va delante.
		case ETN_ItemUseType::Conch:            Lead = 1.6f; Mid = 1.0f; Last = 0.4f; ChestFactor = 0.6f; break;
		// En la playa no protege de nada.
		case ETN_ItemUseType::BigHead:          Lead = 0.3f; Mid = 0.3f; Last = 0.3f; ChestFactor = 0.f; break;
		// No se muere: no revive a nadie. Y los de carrera salen del otro sorteo.
		case ETN_ItemUseType::Totem:
		case ETN_ItemUseType::RaceItem:
		case ETN_ItemUseType::None:             return 0.f;
		default:                                break;
	}
	const float Clamped = FMath::Clamp(TNRaceItemsDetail::EffectiveNorm(Norm, Source), 0.f, 1.f);
	float Weight = Clamped <= 0.5f ? FMath::Lerp(Lead, Mid, Clamped * 2.f) : FMath::Lerp(Mid, Last, (Clamped - 0.5f) * 2.f);
	if (Source == ETNRaceLootSource::Chest || Source == ETNRaceLootSource::Summit)
	{
		Weight *= ChestFactor;
	}
	return FMath::Max(0.f, Weight);
}

bool TNRaceItems::RollLoot(const APawn* Picker, ETNRaceLootSource Source, const UDataTable* Catalog, FTN_InventoryItem& OutItem)
{
	const FTNRaceRank Rank = GetRank(Picker);

	struct FOption
	{
		const FTN_InventoryItem* Row = nullptr;
		ETNRaceItem Kind = ETNRaceItem::None;
		float Weight = 0.f;
	};
	TArray<FOption> Options;
	float Total = 0.f;

	// Los objetos de siempre de DT_Items (los que se pueden recoger y usar), con el peso de su uso según el puesto.
	if (Catalog && Catalog->GetRowStruct() && Catalog->GetRowStruct()->IsChildOf(FTN_InventoryItem::StaticStruct()))
	{
		Catalog->ForeachRow<FTN_InventoryItem>(TEXT("TNRaceItems::RollLoot"), [&Options, &Total, &Rank, Source](const FName& /*RowName*/, const FTN_InventoryItem& Row)
		{
			if (!Row.IsValid() || !Row.PickupActorClass || Row.UseType == ETN_ItemUseType::None || Row.UseType == ETN_ItemUseType::RaceItem)
			{
				return;
			}
			const float Weight = PositionWeightForUse(Row.UseType, Rank.Norm, Source);
			if (Weight <= 0.f)
			{
				return;
			}
			FOption Option;
			Option.Row = &Row;
			Option.Weight = Weight;
			Options.Add(Option);
			Total += Weight;
		});
	}

	// Los de carrera, definidos en código.
	for (int32 Index = static_cast<int32>(ETNRaceItem::Coconut); Index < static_cast<int32>(ETNRaceItem::Count); ++Index)
	{
		const ETNRaceItem Kind = static_cast<ETNRaceItem>(Index);
		const float Weight = PositionWeight(Kind, Rank.Norm, Rank.Count, Source);
		if (Weight <= 0.f)
		{
			continue;
		}
		FOption Option;
		Option.Kind = Kind;
		Option.Weight = Weight;
		Options.Add(Option);
		Total += Weight;
	}
	if (Options.Num() == 0 || Total <= 0.f)
	{
		return false;
	}

	float Pick = FMath::FRand() * Total;
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		Pick -= Options[Index].Weight;
		if (Pick <= 0.f || Index == Options.Num() - 1)
		{
			OutItem = Options[Index].Row ? *Options[Index].Row : MakeItem(Options[Index].Kind);
			return true;
		}
	}
	return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Estado de la tortuga y utilidades
// ─────────────────────────────────────────────────────────────────────────────

bool TNRaceItems::IsInvulnerable(const AActor* Turtle)
{
	const UTN_RaceItemComponent* Comp = UTN_RaceItemComponent::FindOn(Turtle);
	// También la protección del pez globo (objeto del coop): ni derribo ni aturdimiento mientras dura.
	if ((Comp && Comp->IsInvulnerable()) || UTN_CoopItemComponent::IsTurtleProtected(Turtle))
	{
		return true;
	}
	// La burbuja de Todos contra Todos (#830): nada la empuja, derriba ni marea mientras dura.
	const UTN_TctItemComponent* Tct = UTN_TctItemComponent::FindOn(Turtle);
	return Tct && Tct->IsFxActive(ETNTctFx::Bubble);
}

bool TNRaceItems::IsRiding(const AActor* Turtle)
{
	const UTN_RaceItemComponent* Comp = UTN_RaceItemComponent::FindOn(Turtle);
	return Comp && Comp->IsRiding();
}

bool TNRaceItems::CanBeHurt(const ATortugaCharacter* Turtle)
{
	return IsValid(Turtle) && !IsInvulnerable(Turtle);
}

bool TNRaceItems::CanUseNow(const ATortugaCharacter* Turtle)
{
	if (!IsValid(Turtle) || Turtle->IsDead() || Turtle->IsKnockedDown() || Turtle->IsInShell())
	{
		return false;
	}
	if (TNBeach::IsTurtleStunned(Turtle) || ATN_BeachEnemy::IsTurtleHeld(Turtle) || ATN_BeachSandWorm::IsBeingEaten(Turtle)
		|| TNBeach::IsTurtleRelocating(Turtle) || IsRiding(Turtle))
	{
		return false;
	}
	if (const UTN_CarryComponent* Carry = Turtle->GetCarryComponent())
	{
		if (Carry->IsCarrying() || Carry->IsBeingCarried())
		{
			return false;
		}
	}
	return ATN_BeachEnemy::IsRaceLive(Turtle);
}

FVector TNRaceItems::ThrowDirection(const ATortugaCharacter* Turtle, float PitchDeg)
{
	// Hacia donde mira la cámara (en VR, hacia donde apunta la aleta derecha: ATortugaCharacter::GetTurtleAimRotation).
	const FRotator View = Turtle ? Turtle->GetTurtleAimRotation() : FRotator::ZeroRotator;
	return FRotator(PitchDeg, View.Yaw, 0.f).Vector();
}

ATortugaCharacter* TNRaceItems::FindTurtleTarget(const ATortugaCharacter* Attacker, bool bNearestAhead)
{
	if (!Attacker)
	{
		return nullptr;
	}
	TArray<ATortugaCharacter*> Racers;
	GatherRacers(Attacker, Racers);
	const float Mine = CourseProgress(Attacker, Attacker->GetActorLocation());
	ATortugaCharacter* Best = nullptr;
	float BestProgress = 0.f;
	for (ATortugaCharacter* Other : Racers)
	{
		if (!Other || Other == Attacker)
		{
			continue;
		}
		const float Theirs = CourseProgress(Attacker, Other->GetActorLocation());
		// Solo las que van por delante: la más cercana (la que menos ventaja lleva) o la que va la primera.
		if (Theirs <= Mine)
		{
			continue;
		}
		const bool bBetter = !Best || (bNearestAhead ? Theirs < BestProgress : Theirs > BestProgress);
		if (bBetter)
		{
			Best = Other;
			BestProgress = Theirs;
		}
	}
	return Best;
}

// ─────────────────────────────────────────────────────────────────────────────
// Uso
// ─────────────────────────────────────────────────────────────────────────────

bool TNRaceItems::GiveItem(ATortugaCharacter* Turtle, ETNRaceItem Kind)
{
	if (!Turtle || !Turtle->HasAuthority() || Kind == ETNRaceItem::None || Kind == ETNRaceItem::Box)
	{
		return false;
	}
	UTN_InventoryComponent* Inventory = Turtle->GetInventoryComponent();
	return Inventory && Inventory->TryAddOrReplaceEquipped(MakeItem(Kind), true);
}

void TNRaceItems::ServerUse(ATortugaCharacter* Turtle, const FTN_InventoryItem& Item)
{
	if (!Turtle || !Turtle->HasAuthority())
	{
		return;
	}
	const ETNRaceItem Kind = KindOf(Item);
	UTN_InventoryComponent* Inventory = Turtle->GetInventoryComponent();
	UTN_RaceItemComponent* Effects = UTN_RaceItemComponent::FindOrAddOn(Turtle);
	if (Kind == ETNRaceItem::None || Kind == ETNRaceItem::Box || !Inventory || !Effects)
	{
		return;
	}
	if (!CanUseNow(Turtle))
	{
		Effects->MulticastCue(ETNRaceSound::Nope, 1.f);
		return;
	}

	bool bUsed = false;
	switch (Kind)
	{
		case ETNRaceItem::Coconut:
		case ETNRaceItem::TripleCoconut3:
		case ETNRaceItem::TripleCoconut2:
		case ETNRaceItem::TripleCoconut1:
			Effects->GrantBoost(TurboMultiplier, TurboSeconds, false);
			Effects->MulticastCue(ETNRaceSound::Turbo, 1.f);
			bUsed = true;
			break;
		case ETNRaceItem::GoldenCoconut:
			Effects->GrantBoost(TurboMultiplier, GoldenSeconds, true);
			if (UTN_StaminaComponent* Stamina = Turtle->GetStaminaComponent())
			{
				// Energía sin fin mientras dura, y sin el cansancio de después: el dorado son turbos sin parar.
				Stamina->SetPostBoostExhaustionSeconds(0.f);
				Stamina->GrantUnlimitedStamina(GoldenSeconds);
			}
			Effects->MulticastCue(ETNRaceSound::Golden, 1.f);
			bUsed = true;
			break;
		case ETNRaceItem::Sunscreen:
			Effects->GrantStar(StarSeconds);
			Effects->MulticastCue(ETNRaceSound::StarUp, 1.f);
			bUsed = true;
			break;
		case ETNRaceItem::PelicanTaxi:
			bUsed = ATN_RacePelicanTaxi::ServerLaunch(Turtle);
			break;
		case ETNRaceItem::HomingCrab:
			bUsed = ATN_RaceHomingCrab::ServerLaunch(Turtle);
			break;
		case ETNRaceItem::GullStrike:
			bUsed = ATN_RaceGullStrike::ServerLaunch(Turtle);
			break;
		case ETNRaceItem::SandMine:
			bUsed = ATN_RaceMine::ServerThrow(Turtle, ThrowDirection(Turtle, 28.f));
			if (bUsed)
			{
				Effects->MulticastCue(ETNRaceSound::Throw, 1.f);
			}
			break;
		case ETNRaceItem::StormCloud:
			bUsed = ATN_RaceStormCloud::ServerCast(Turtle);
			break;
		case ETNRaceItem::Frisbee:
			bUsed = ATN_RaceFrisbee::ServerThrow(Turtle, ThrowDirection(Turtle, 6.f));
			if (bUsed)
			{
				Effects->MulticastCue(ETNRaceSound::Throw, 1.2f);
			}
			break;
		case ETNRaceItem::Whistle:
		{
			const int32 Stunned = TNRaceItemsDetail::WhistleEnemies(Turtle);
			Effects->MulticastWhistle(FVector_NetQuantize10(Turtle->GetActorLocation()), WhistleRadius);
			UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s silba: %d enemigos mareados."), *GetNameSafe(Turtle), Stunned);
			bUsed = true;
			break;
		}
		case ETNRaceItem::TablaSurf:
			bUsed = Effects->GrantSurf(TNRaceItemRules::SurfSeconds);
			break;
		case ETNRaceItem::CanaPescar:
			bUsed = ATN_RaceFishingHook::ServerCast(Turtle);
			break;
		case ETNRaceItem::Remolino:
			bUsed = ATN_RaceWhirlpool::ServerDrop(Turtle);
			break;
		case ETNRaceItem::CoheteFeria:
			bUsed = Effects->GrantRocket(TNRaceItemRules::RocketSeconds);
			break;
		default:
			break;
	}
	if (!bUsed)
	{
		// Sin nadie a quien apuntar, sin sitio...: el objeto se queda.
		Effects->MulticastCue(ETNRaceSound::Nope, 1.f);
		UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s no puede usar %s ahora."), *GetNameSafe(Turtle), *CodeName(Kind));
		return;
	}

	// Gastarlo: el triple coco pasa a tener un uso menos; el resto se consume.
	if (Kind == ETNRaceItem::TripleCoconut3 || Kind == ETNRaceItem::TripleCoconut2)
	{
		Inventory->TryReplaceEquippedItem(MakeItem(Kind == ETNRaceItem::TripleCoconut3 ? ETNRaceItem::TripleCoconut2 : ETNRaceItem::TripleCoconut1));
	}
	else
	{
		FTN_InventoryItem Consumed;
		Inventory->TryConsumeEquippedItem(Consumed);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Carrera] %s usa %s."), *GetNameSafe(Turtle), *CodeName(Kind));
}
