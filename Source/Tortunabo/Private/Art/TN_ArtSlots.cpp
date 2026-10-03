#include "TN_ArtSlots.h"

namespace TNArtSlotsDetail
{
	// TN_ART_SLOT(Nombre, Tipo, Fichero, Qué es, Tamaño, Pivote): cadenas entre comillas dobles, sin comillas dentro (las lee
	// también Scripts/arte/rellenar_catalogos.py).
#define TN_ART_SLOT(Name, Kind, Source, What, Size, Pivot) { TEXT(Name), TEXT(Kind), TEXT(Source), TEXT(What), TEXT(Size), TEXT(Pivot) },
	const TNArt::FSlotInfo Table[] = {
#include "TN_ArtSlots_Lobby.inl"
#include "TN_ArtSlots_LobbyValley.inl"
#include "TN_ArtSlots_LobbyPlayground.inl"
#include "TN_ArtSlots_ProcMap.inl"
#include "TN_ArtSlots_Beach.inl"
#include "TN_ArtSlots_Turtle.inl"
	};
#undef TN_ART_SLOT
}

TArrayView<const TNArt::FSlotInfo> TNArt::GetSlotTable()
{
	return MakeArrayView(TNArtSlotsDetail::Table, UE_ARRAY_COUNT(TNArtSlotsDetail::Table));
}

const TNArt::FSlotInfo* TNArt::FindSlotInfo(FName Slot)
{
	static const TMap<FName, int32> Index = []()
	{
		TMap<FName, int32> Map;
		const TArrayView<const FSlotInfo> Table = GetSlotTable();
		for (int32 i = 0; i < Table.Num(); ++i)
		{
			Map.FindOrAdd(FName(Table[i].Name), i);
		}
		return Map;
	}();
	const int32* Found = Index.Find(Slot);
	return Found ? &GetSlotTable()[*Found] : nullptr;
}
