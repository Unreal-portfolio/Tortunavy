#include "World/TN_MapPlacements.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace TNMapPlacementsDetail
{
	bool ReadVector(const FJsonObject& Object, const TCHAR* Field, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.TryGetArrayField(Field, Values) || !Values || Values->Num() < 3)
		{
			return false;
		}
		double Xyz[3] = {};
		for (int32 i = 0; i < 3; ++i)
		{
			if (!(*Values)[i].IsValid() || !(*Values)[i]->TryGetNumber(Xyz[i]))
			{
				return false;
			}
		}
		Out = FVector(Xyz[0], Xyz[1], Xyz[2]);
		return true;
	}

	double NumberOr(const FJsonObject& Object, const TCHAR* Field, double Default)
	{
		double Value = Default;
		return Object.TryGetNumberField(Field, Value) ? Value : Default;
	}

	/** Los parámetros numéricos y booleanos (true = 1) de "params"; el resto (listas, textos) no le hace falta al cargador. */
	void ReadParams(const FJsonObject& Entry, TMap<FString, double>& Out)
	{
		const TSharedPtr<FJsonObject>* Params = nullptr;
		if (!Entry.TryGetObjectField(TEXT("params"), Params) || !Params || !Params->IsValid())
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
		{
			double Number = 0.0;
			bool bFlag = false;
			if (Pair.Value.IsValid() && Pair.Value->TryGetNumber(Number))
			{
				Out.Add(Pair.Key, Number);
			}
			else if (Pair.Value.IsValid() && Pair.Value->TryGetBool(bFlag))
			{
				Out.Add(Pair.Key, bFlag ? 1.0 : 0.0);
			}
		}
	}

	/** Una entrada del bloque; false (con el motivo) si le falta lo imprescindible: category, kind y location_uu. */
	bool ReadEntry(const FJsonObject& Entry, const FString& Source, int32 Index, TNMapPlacements::FPlacement& Out, FString& OutWhy)
	{
		using namespace TNMapPlacements;
		if (!Entry.TryGetStringField(TEXT("category"), Out.Category) || !Entry.TryGetStringField(TEXT("kind"), Out.Kind))
		{
			OutWhy = TEXT("sin category o kind");
			return false;
		}
		if (!ReadVector(Entry, TEXT("location_uu"), Out.Location))
		{
			OutWhy = TEXT("sin location_uu válida");
			return false;
		}
		if (!Entry.TryGetStringField(TEXT("id"), Out.Id) || Out.Id.IsEmpty())
		{
			Out.Id = FString::Printf(TEXT("%s-%d"), *Source, Index);
		}
		Out.Source = Source;
		Out.YawDeg = NumberOr(Entry, TEXT("yaw_deg"), 0.0);
		Out.LengthCm = FMath::Max(0.0, NumberOr(Entry, TEXT("length_m"), 0.0)) * 100.0;
		Out.ExtentCm = FMath::Max(0.0, NumberOr(Entry, TEXT("extent_uu"), 0.0));
		Out.SizeScale = static_cast<float>(FMath::Clamp(NumberOr(Entry, TEXT("size_scale"), 1.0), 0.1, 10.0));
		Out.Line = static_cast<int32>(NumberOr(Entry, TEXT("line"), INDEX_NONE));
		Out.S = NumberOr(Entry, TEXT("s_m"), 0.0);
		Out.ProgressM = NumberOr(Entry, TEXT("progress_m"), -1.0);
		const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
		if (Entry.TryGetArrayField(TEXT("path_uu"), Path) && Path)
		{
			for (const TSharedPtr<FJsonValue>& Point : *Path)
			{
				const TArray<TSharedPtr<FJsonValue>>* Xyz = nullptr;
				if (Point.IsValid() && Point->TryGetArray(Xyz) && Xyz && Xyz->Num() >= 3)
				{
					Out.Path.Add(FVector((*Xyz)[0]->AsNumber(), (*Xyz)[1]->AsNumber(), (*Xyz)[2]->AsNumber()));
				}
			}
		}
		ReadParams(Entry, Out.Params);
		Out.Spawn = SpawnOf(Out.Category, Out.Kind, Out.Element);
		return true;
	}

	void ReadList(const FJsonObject& Block, const TCHAR* Field, const FString& Source, const TSet<FString>& Suppressed,
		TNMapPlacements::FParseResult& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Block.TryGetArrayField(Field, Entries) || !Entries)
		{
			return;
		}
		for (int32 i = 0; i < Entries->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			TNMapPlacements::FPlacement Placement;
			FString Why = TEXT("no es un objeto");
			if (!(*Entries)[i].IsValid() || !(*Entries)[i]->TryGetObject(Entry) || !Entry || !Entry->IsValid()
				|| !ReadEntry(**Entry, Source, i, Placement, Why))
			{
				++Out.Invalid;
				Out.Warnings.Add(FString::Printf(TEXT("%s[%d]: %s"), Field, i, *Why));
				continue;
			}
			if (Suppressed.Contains(Placement.Id))
			{
				++Out.Suppressed;
				continue;
			}
			Out.Placements.Add(MoveTemp(Placement));
		}
	}

	int32 CountOf(const FJsonObject& Block, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		return Block.TryGetArrayField(Field, Entries) && Entries ? Entries->Num() : 0;
	}

	TNMapPlacements::ESpawn PuzzleSpawn(const FString& Kind)
	{
		using TNMapPlacements::ESpawn;
		if (Kind == TEXT("plate_balance")) { return ESpawn::PlateBalance; }
		if (Kind == TEXT("breakable_chain")) { return ESpawn::BreakableChain; }
		if (Kind == TEXT("wobbly_run")) { return ESpawn::WobblyRun; }
		return ESpawn::Unsupported;
	}

	TNMapPlacements::ESpawn LootSpawn(const FString& Kind, ETNBeachElement& OutElement)
	{
		using TNMapPlacements::ESpawn;
		if (Kind == TEXT("SearchSpot")) { return ESpawn::SearchSpot; }
		if (Kind == TEXT("FishingPool")) { return ESpawn::FishingPool; }
		if (TNMapPlacements::ElementFromName(Kind, OutElement) && TNBeach::CategoryOf(OutElement) != ETNBeachCategory::Decor)
		{
			return ESpawn::BeachElement;
		}
		return ESpawn::Unsupported;
	}
}

bool TNMapPlacements::ElementFromName(const FString& Name, ETNBeachElement& OutElement)
{
	const UEnum* Enum = StaticEnum<ETNBeachElement>();
	if (!Enum || Name.IsEmpty() || Name.Contains(TEXT("::")))
	{
		return false;
	}
	const int64 Value = Enum->GetValueByNameString(Name, EGetByNameFlags::CaseSensitive);
	if (Value == INDEX_NONE || Value >= static_cast<int64>(ETNBeachElement::Count))
	{
		return false;
	}
	OutElement = static_cast<ETNBeachElement>(Value);
	return true;
}

TNMapPlacements::ESpawn TNMapPlacements::SpawnOf(const FString& Category, const FString& Kind, ETNBeachElement& OutElement)
{
	using namespace TNMapPlacementsDetail;
	if (Category == TEXT("puzzle")) { return PuzzleSpawn(Kind); }
	if (Category == TEXT("loot")) { return LootSpawn(Kind, OutElement); }
	if (Category == TEXT("vegetation"))
	{
		return Kind == TEXT("Palm") || Kind == TEXT("Shrub") || Kind == TEXT("Grass") ? ESpawn::Vegetation : ESpawn::Unsupported;
	}
	if (!ElementFromName(Kind, OutElement))
	{
		return ESpawn::Unsupported;
	}
	const ETNBeachCategory BeachCategory = TNBeach::CategoryOf(OutElement);
	if (Category == TEXT("decor"))
	{
		return BeachCategory == ETNBeachCategory::Decor ? ESpawn::Decor : ESpawn::Unsupported;
	}
	if (Category == TEXT("mechanic"))
	{
		// La pasarela es decorado que se recorre (ATN_BeachDecorField); el resto de mecánicas, elementos replicados.
		return BeachCategory == ETNBeachCategory::Decor ? ESpawn::Decor : ESpawn::BeachElement;
	}
	if (Category == TEXT("enemy") || Category == TEXT("obstacle"))
	{
		return BeachCategory == ETNBeachCategory::Decor ? ESpawn::Unsupported : ESpawn::BeachElement;
	}
	return ESpawn::Unsupported;
}

const TCHAR* TNMapPlacements::SpawnName(ESpawn Spawn)
{
	switch (Spawn)
	{
	case ESpawn::BeachElement:   return TEXT("BeachElement");
	case ESpawn::Decor:          return TEXT("Decor");
	case ESpawn::Vegetation:     return TEXT("Vegetation");
	case ESpawn::SearchSpot:     return TEXT("SearchSpot");
	case ESpawn::FishingPool:    return TEXT("FishingPool");
	case ESpawn::PlateBalance:   return TEXT("PlateBalance");
	case ESpawn::BreakableChain: return TEXT("BreakableChain");
	case ESpawn::WobblyRun:      return TEXT("WobblyRun");
	default:                     return TEXT("Unsupported");
	}
}

bool TNMapPlacements::ParseBlock(const FJsonObject& Manifest, FParseResult& Out)
{
	using namespace TNMapPlacementsDetail;
	Out = FParseResult();
	if (!Manifest.HasField(TEXT("placements")))
	{
		return true;
	}
	const TSharedPtr<FJsonObject>* Block = nullptr;
	if (!Manifest.TryGetObjectField(TEXT("placements"), Block) || !Block || !Block->IsValid())
	{
		Out.Warnings.Add(TEXT("el campo placements no es un objeto"));
		return false;
	}
	Out.bHasBlock = true;
	TSet<FString> Suppressed;
	const TArray<TSharedPtr<FJsonValue>>* SuppressedList = nullptr;
	if ((*Block)->TryGetArrayField(TEXT("suppressed"), SuppressedList) && SuppressedList)
	{
		for (const TSharedPtr<FJsonValue>& Value : *SuppressedList)
		{
			FString Id;
			if (Value.IsValid() && Value->TryGetString(Id)) { Suppressed.Add(Id); }
		}
	}
	(*Block)->TryGetBoolField(TEXT("stale"), Out.bStale);
	if (Out.bStale)
	{
		Out.SkippedStale = CountOf(**Block, TEXT("auto"));
	}
	else
	{
		ReadList(**Block, TEXT("auto"), TEXT("auto"), Suppressed, Out);
	}
	ReadList(**Block, TEXT("manual"), TEXT("manual"), Suppressed, Out);
	return true;
}

FVector TNMapPlacements::PointAlong(const FPlacement& P, double Alpha, double& OutYawDeg)
{
	Alpha = FMath::Clamp(Alpha, 0.0, 1.0);
	OutYawDeg = P.YawDeg;
	if (P.Path.Num() < 2)
	{
		const FVector Dir = FRotator(0.0, P.YawDeg, 0.0).Vector();
		return P.Location + Dir * (Alpha - 0.5) * P.LengthCm;
	}
	double Total = 0.0;
	for (int32 i = 1; i < P.Path.Num(); ++i)
	{
		Total += FVector::Dist2D(P.Path[i - 1], P.Path[i]);
	}
	double Remaining = Alpha * Total;
	for (int32 i = 1; i < P.Path.Num(); ++i)
	{
		const FVector& A = P.Path[i - 1];
		const FVector& B = P.Path[i];
		const double Segment = FVector::Dist2D(A, B);
		if (Segment <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		OutYawDeg = FMath::RadiansToDegrees(FMath::Atan2(B.Y - A.Y, B.X - A.X));
		if (Remaining <= Segment || i == P.Path.Num() - 1)
		{
			return FMath::Lerp(A, B, FMath::Clamp(Remaining / Segment, 0.0, 1.0));
		}
		Remaining -= Segment;
	}
	return P.Path.Last();
}
