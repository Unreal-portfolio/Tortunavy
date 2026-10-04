// TNLobbyMission, parte del Rally (#632): circuito de LVL_Rally y plazas por buggy (estas, también de Karts), que el
// anfitrión elige al crear la sala y con el general, y la URL del viaje al salir del lobby.
#include "Lobby/TN_LobbyMission.h"

#include "Core/TN_Log.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace TNLobbyRallyDetail
{
	/** Circuitos con nombre traducido, en el orden del menú: solo los del generador de vueltas (#692). */
	const TCHAR* const KnownCircuits[] = { TNLobbyMission::DefaultRallyCircuit, TEXT("R02_circuito_tierra"),
		TEXT("R03_circuito_dunas_costeras"), TEXT("R04_circuito_cantera"), TEXT("R05_circuito_marismas"), TEXT("R06_circuito_lomas") };

	UMP_GameInstance* GameInstanceOf(const UObject* WorldContext, bool bHostOnly)
	{
		const UWorld* World = WorldContext && GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		if (!World || (bHostOnly && World->GetNetMode() == NM_Client))
		{
			return nullptr;
		}
		return Cast<UMP_GameInstance>(World->GetGameInstance());
	}

	int32 KnownIndex(FName Variant)
	{
		for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(KnownCircuits)); ++Index)
		{
			if (Variant.ToString().Equals(KnownCircuits[Index], ESearchCase::IgnoreCase))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	TArray<FName> ScanCircuits()
	{
		const FString Root = FPaths::ProjectDir() / TEXT("Scripts/terrain_volumes/Variants");
		TArray<FString> Folders;
		IFileManager::Get().FindFiles(Folders, *(Root / TEXT("*")), false, true);
		TArray<FName> Circuits;
		for (const FString& Folder : Folders)
		{
			FString Json;
			if (FFileHelper::LoadFileToString(Json, *(Root / Folder / TEXT("manifest.json"))) && TNLobbyMission::IsRallyCircuitManifest(Json))
			{
				Circuits.Add(FName(*Folder));
			}
		}
		UE_LOG(LogTortunabo, Log, TEXT("[Misión] %d circuito(s) del Rally en %s."), Circuits.Num(), *Root);
		return Circuits;
	}
}

FText TNLobbyMission::MissionTitle(ETNProcGameMode Mode, FName MapVariant)
{
	if (Mode == ETNProcGameMode::FreeForAll && !MapVariant.IsNone())
	{
		return FText::Format(NSLOCTEXT("Tortunabo", "MissionRallyTitle", "{0} · {1}"), ModeName(Mode), TctArenaName(MapVariant));
	}
	if (Mode != ETNProcGameMode::Rally)
	{
		return ModeName(Mode);
	}
	return FText::Format(NSLOCTEXT("Tortunabo", "MissionRallyTitle", "{0} · {1}"), ModeName(Mode), RallyMapName(MapVariant));
}

FText TNLobbyMission::RallySeatsName(int32 Seats)
{
	return Seats <= 1 ? NSLOCTEXT("Tortunabo", "BriefingRallySeatsOne", "Una por buggy") : NSLOCTEXT("Tortunabo", "BriefingKartSeatsTwo", "Por parejas");
}

bool TNLobbyMission::IsRallyCircuitManifest(const FString& JsonText)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	FString Mode;
	const TArray<TSharedPtr<FJsonValue>>* Checkpoints = nullptr;
	const TSharedPtr<FJsonObject>* Generator = nullptr;
	FString GeneratorName;
	// Solo los circuitos del generador de vueltas (#692): una variante de autor con "mode": "rally" no entra en el Rally.
	return Root->TryGetStringField(TEXT("mode"), Mode) && Mode.Equals(TEXT("rally"), ESearchCase::IgnoreCase)
		&& Root->TryGetArrayField(TEXT("checkpoints_uu"), Checkpoints) && Checkpoints && Checkpoints->Num() >= 2
		&& Root->TryGetObjectField(TEXT("generator"), Generator) && Generator && Generator->IsValid()
		&& (*Generator)->TryGetStringField(TEXT("generator"), GeneratorName) && GeneratorName.Equals(RallyCircuitGenerator);
}

TArray<FName> TNLobbyMission::SortRallyMaps(const TArray<FName>& Circuits)
{
	TArray<FName> Sorted;
	for (const FName Circuit : Circuits)
	{
		if (!Circuit.IsNone())
		{
			Sorted.AddUnique(Circuit);
		}
	}
	Sorted.Sort([](const FName& A, const FName& B)
	{
		const int32 KnownA = TNLobbyRallyDetail::KnownIndex(A);
		const int32 KnownB = TNLobbyRallyDetail::KnownIndex(B);
		if (KnownA != KnownB)
		{
			// Los conocidos primero, en su orden; los demás detrás.
			return KnownB == INDEX_NONE || (KnownA != INDEX_NONE && KnownA < KnownB);
		}
		return A.LexicalLess(B);
	});
	return Sorted;
}

const TArray<FName>& TNLobbyMission::RallyMapOptions()
{
	static const TArray<FName> Options = SortRallyMaps(TNLobbyRallyDetail::ScanCircuits());
	return Options;
}

FName TNLobbyMission::ResolveRallyMap(FName Selected, const TArray<FName>& Options)
{
	if (Options.Contains(Selected))
	{
		return Selected;
	}
	return Options.Num() > 0 ? Options[0] : FName(NAME_None);
}

FString TNLobbyMission::RallyTravelURL(FName Variant, const FString& RallyMapPath)
{
	return FString::Printf(TEXT("%s?Variant=%s?FromLobby"), *RallyMapPath, *Variant.ToString());
}

FName TNLobbyMission::GetHostRallyMap(const UObject* WorldContext)
{
	const UMP_GameInstance* GI = TNLobbyRallyDetail::GameInstanceOf(WorldContext, false);
	return ResolveRallyMap(GI ? GI->SelectedRallyVariant : FName(DefaultRallyCircuit), RallyMapOptions());
}

int32 TNLobbyMission::GetHostRallySeats(const UObject* WorldContext)
{
	const UMP_GameInstance* GI = TNLobbyRallyDetail::GameInstanceOf(WorldContext, false);
	return GI ? FMath::Clamp(GI->SelectedKartSeats, 1, 2) : 2;
}

bool TNLobbyMission::SetRallyMap(const UObject* WorldContext, FName Variant)
{
	UMP_GameInstance* GI = TNLobbyRallyDetail::GameInstanceOf(WorldContext, true);
	if (!GI || !RallyMapOptions().Contains(Variant))
	{
		return false;
	}
	if (GI->SelectedRallyVariant != Variant)
	{
		GI->SelectedRallyVariant = Variant;
		UE_LOG(LogTortunabo, Log, TEXT("[Misión] Circuito del Rally: %s"), *Variant.ToString());
	}
	SyncLobby(WorldContext);
	return true;
}

bool TNLobbyMission::SetRallySeats(const UObject* WorldContext, int32 Seats)
{
	UMP_GameInstance* GI = TNLobbyRallyDetail::GameInstanceOf(WorldContext, true);
	if (!GI)
	{
		return false;
	}
	GI->SelectedKartSeats = FMath::Clamp(Seats, 1, 2);
	SyncLobby(WorldContext);
	return true;
}
