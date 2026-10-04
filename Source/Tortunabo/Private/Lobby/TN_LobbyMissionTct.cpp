// TNLobbyMission, parte de Todos contra Todos (#651): la arena de LVL_Tct que el anfitrión elige al crear la sala y con el
// general, y la URL del viaje al salir del lobby. Las arenas son las variantes de Scripts/terrain_volumes/Variants con
// "mode": "tct".
#include "Lobby/TN_LobbyMission.h"

#include "Core/TN_LocText.h"
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

namespace TNLobbyTctDetail
{
	struct FKnownArena
	{
		const TCHAR* Id;
		FText Name;
	};

	/** Arenas con nombre traducido, en el orden del menú (la de por defecto, primero). */
	const TArray<FKnownArena>& KnownArenas()
	{
		static const TArray<FKnownArena> Arenas = {
			{ TNLobbyMission::DefaultTctArena, NSLOCTEXT("Tortunabo", "TctArenaA01", "Diana") },
			{ TEXT("A02_donut"), NSLOCTEXT("Tortunabo", "TctArenaA02", "Dónut") },
			{ TEXT("A03_espiral"), NSLOCTEXT("Tortunabo", "TctArenaA03", "Espiral") },
			{ TEXT("A04_reloj"), NSLOCTEXT("Tortunabo", "TctArenaA04", "Reloj") },
			{ TEXT("A05_tablero"), NSLOCTEXT("Tortunabo", "TctArenaA05", "Tablero de mesetas") },
			{ TEXT("A06_panal"), NSLOCTEXT("Tortunabo", "TctArenaA06", "Panal") },
			{ TEXT("A07_panal_piramide"), NSLOCTEXT("Tortunabo", "TctArenaA07", "Panal en pirámide") },
			{ TEXT("A08_panal_roto"), NSLOCTEXT("Tortunabo", "TctArenaA08", "Panal roto") },
			{ TEXT("A09_colmena"), NSLOCTEXT("Tortunabo", "TctArenaA09", "Colmena") },
			{ TEXT("A10_ajedrez"), NSLOCTEXT("Tortunabo", "TctArenaA10", "Ajedrez") },
			{ TEXT("A11_zigurat"), NSLOCTEXT("Tortunabo", "TctArenaA11", "Zigurat") },
			{ TEXT("A12_damas"), NSLOCTEXT("Tortunabo", "TctArenaA12", "Damas") },
			{ TEXT("N01_coliseo"), NSLOCTEXT("Tortunabo", "TctArenaN01", "Coliseo") },
			{ TEXT("N02_anfiteatro"), NSLOCTEXT("Tortunabo", "TctArenaN02", "Anfiteatro") },
			{ TEXT("N03_volcan_arena"), NSLOCTEXT("Tortunabo", "TctArenaN03", "Volcán-arena") },
			{ TEXT("N04_atolon"), NSLOCTEXT("Tortunabo", "TctArenaN04", "Atolón") },
			{ TEXT("N17_fortaleza_estrella"), NSLOCTEXT("Tortunabo", "TctArenaN17", "Fortaleza en estrella") },
			{ TEXT("N18_yin_yang"), NSLOCTEXT("Tortunabo", "TctArenaN18", "Yin-yang") },
			{ TEXT("I01_filipinas_rara"), NSLOCTEXT("Tortunabo", "TctArenaI01", "Archipiélago de las Mil Bolas") },
			{ TEXT("I02_galapagos"), NSLOCTEXT("Tortunabo", "TctArenaI02", "Galápagos") },
			{ TEXT("I03T_tortuga_magna"), NSLOCTEXT("Tortunabo", "TctArenaI03T", "Tortuga Magna") },
			{ TEXT("I05_santorini"), NSLOCTEXT("Tortunabo", "TctArenaI05", "Santorini") },
		};
		return Arenas;
	}

	int32 KnownIndex(FName Variant)
	{
		const TArray<FKnownArena>& Arenas = KnownArenas();
		for (int32 Index = 0; Index < Arenas.Num(); ++Index)
		{
			if (Variant.ToString().Equals(Arenas[Index].Id, ESearchCase::IgnoreCase))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	UMP_GameInstance* GameInstanceOf(const UObject* WorldContext, bool bHostOnly)
	{
		const UWorld* World = WorldContext && GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		if (!World || (bHostOnly && World->GetNetMode() == NM_Client))
		{
			return nullptr;
		}
		return Cast<UMP_GameInstance>(World->GetGameInstance());
	}

	TArray<FName> ScanArenas()
	{
		const FString Root = FPaths::ProjectDir() / TEXT("Scripts/terrain_volumes/Variants");
		TArray<FString> Folders;
		IFileManager::Get().FindFiles(Folders, *(Root / TEXT("*")), false, true);
		TArray<FName> Arenas;
		for (const FString& Folder : Folders)
		{
			FString Json;
			if (FFileHelper::LoadFileToString(Json, *(Root / Folder / TEXT("manifest.json"))) && TNLobbyMission::IsTctArenaManifest(Json))
			{
				Arenas.Add(FName(*Folder));
			}
		}
		UE_LOG(LogTortunabo, Log, TEXT("[Misión] %d arena(s) de Todos contra Todos en %s."), Arenas.Num(), *Root);
		return Arenas;
	}
}

bool TNLobbyMission::IsTctArenaManifest(const FString& JsonText)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	FString Mode;
	const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
	return Root->TryGetStringField(TEXT("mode"), Mode) && Mode.Equals(TEXT("tct"), ESearchCase::IgnoreCase)
		&& Root->TryGetArrayField(TEXT("cells"), Cells) && Cells && Cells->Num() > 0;
}

TArray<FName> TNLobbyMission::SortTctArenas(const TArray<FName>& Arenas)
{
	TArray<FName> Sorted;
	for (const FName Arena : Arenas)
	{
		if (!Arena.IsNone())
		{
			Sorted.AddUnique(Arena);
		}
	}
	Sorted.Sort([](const FName& A, const FName& B)
	{
		const int32 KnownA = TNLobbyTctDetail::KnownIndex(A);
		const int32 KnownB = TNLobbyTctDetail::KnownIndex(B);
		if (KnownA != KnownB)
		{
			// Las conocidas primero, en su orden; las demás detrás.
			return KnownB == INDEX_NONE || (KnownA != INDEX_NONE && KnownA < KnownB);
		}
		return A.LexicalLess(B);
	});
	return Sorted;
}

const TArray<FName>& TNLobbyMission::TctArenaOptions()
{
	static const TArray<FName> Options = SortTctArenas(TNLobbyTctDetail::ScanArenas());
	return Options;
}

FName TNLobbyMission::ResolveTctArena(FName Selected, const TArray<FName>& Options)
{
	if (Options.Contains(Selected))
	{
		return Selected;
	}
	return Options.Num() > 0 ? Options[0] : FName(NAME_None);
}

FText TNLobbyMission::TctArenaName(FName Variant)
{
	const int32 Known = TNLobbyTctDetail::KnownIndex(Variant);
	if (Known != INDEX_NONE)
	{
		return TNLobbyTctDetail::KnownArenas()[Known].Name;
	}
	// Una variante que aún no tiene nombre traducido: su identificador, tal cual.
	return TNLocText::Literal(Variant.IsNone() ? FString() : Variant.ToString());
}

FString TNLobbyMission::TctTravelURL(FName Arena, const FString& TctMapPath)
{
	return FString::Printf(TEXT("%s?game=Tct?Arena=%s"), *TctMapPath, *Arena.ToString());
}

FName TNLobbyMission::GetHostTctArena(const UObject* WorldContext)
{
	const UMP_GameInstance* GI = TNLobbyTctDetail::GameInstanceOf(WorldContext, false);
	return ResolveTctArena(GI ? GI->SelectedTctArena : FName(DefaultTctArena), TctArenaOptions());
}

bool TNLobbyMission::SetTctArena(const UObject* WorldContext, FName Arena)
{
	UMP_GameInstance* GI = TNLobbyTctDetail::GameInstanceOf(WorldContext, true);
	if (!GI || !TctArenaOptions().Contains(Arena))
	{
		return false;
	}
	if (GI->SelectedTctArena != Arena)
	{
		GI->SelectedTctArena = Arena;
		UE_LOG(LogTortunabo, Log, TEXT("[Misión] Arena de Todos contra Todos: %s"), *Arena.ToString());
	}
	SyncLobby(WorldContext);
	return true;
}

FName TNLobbyMission::GetHostMissionMap(const UObject* WorldContext)
{
	switch (GetHostMode(WorldContext))
	{
	case ETNProcGameMode::Rally:      return GetHostRallyMap(WorldContext);
	case ETNProcGameMode::FreeForAll: return GetHostTctArena(WorldContext);
	default:                          return NAME_None;
	}
}
