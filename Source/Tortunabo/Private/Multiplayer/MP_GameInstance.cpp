#include "Multiplayer/MP_GameInstance.h"
#include "Core/TN_Log.h"
#include "Core/TN_CosmeticsTypes.h"
#include "Engine/DataTable.h"
#include "OnlineSubsystem.h"
#include "Online.h"
#include "OnlineSessionSettings.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"
#include "Multiplayer/TN_CosmeticSlot.h"
#include "Multiplayer/TN_CosmeticSaveGame.h"
#include "Vehicles/TN_BuggyCosmetics.h"
#include "HAL/IConsoleManager.h"
#include "Multiplayer/TN_LocalPlayRules.h"
#include "Multiplayer/TN_LocalPlaySubsystem.h"
#include "Multiplayer/TN_LocalPlayerProfile.h"
#include "Lobby/TN_LobbyMission.h"
#include "Engine/NetDriver.h"
#include "Multiplayer/TN_NetworkFailureDecisions.h"
#include "Multiplayer/TN_RoomInfo.h"
#include "Multiplayer/TN_SaveGameIO.h"
#include "Settings/TN_GameplayAssetSettings.h"
#include "Multiplayer/TN_RoomNames.h"
#include "Multiplayer/TN_TutorialSaveGame.h"
#include "UI/HUD/TN_LoadingScreenWidget.h"
#include "UI/Loading/TN_LoadingScreenSubsystem.h"
#include "Voice/ProximityVoiceComponent.h"
#include "VR/TN_VRMode.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

namespace
{
	/** @brief Devuelve el Online Subsystem preferido: Steam si esta disponible, si no el por defecto. */
	IOnlineSubsystem* MPGameInstance_GetPreferredOnlineSubsystem()
	{
		IOnlineSubsystem* OSS = IOnlineSubsystem::Get(FName(TEXT("Steam")));
		if (!OSS)
		{
			OSS = IOnlineSubsystem::Get();
		}
		return OSS;
	}

	/** Segundos que se espera una búsqueda de salas antes de darla por fallida. */
	constexpr double MPGameInstance_RoomSearchTimeout = 25.0;

	/** Segundos que tiene un expulsado para irse solo antes de que el servidor lo eche. */
	constexpr float MPGameInstance_KickGraceSeconds = 2.5f;

#if !UE_BUILD_SHIPPING
	// Comando de prueba: fuera de la build de Steam, como TNStorm y TNBooth.
	void MPGameInstance_HandleFakeRoomError(const TArray<FString>& Args, UWorld* World)
	{
		UMP_GameInstance* GI = World ? Cast<UMP_GameInstance>(World->GetGameInstance()) : nullptr;
		if (!GI)
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Salas] TN.Rooms.FakeError: no hay GameInstance de Tortunavy."));
			return;
		}
		GI->DebugFakeRoomError(Args.Num() > 0 ? Args[0] : FString());
	}

	FAutoConsoleCommandWithWorldAndArgs MPGameInstance_FakeRoomErrorCommand(
		TEXT("TN.Rooms.FakeError"),
		TEXT("Simula un fallo al entrar en una sala: TN.Rooms.FakeError <locked|full|kicked|other|build|checksum|joinfull|gone|noaddress>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&MPGameInstance_HandleFakeRoomError));
#endif
}

UMP_GameInstance::UMP_GameInstance()
{
	// LoadingScreenWidgetClass is assigned in a BP derived GameInstance.
	// If not set, ShowLoadingScreen falls back to the C++ base widget.
	DefaultUnlockedHelmets = { FName(TEXT("Helmet_Default")) };
	HelmetCrateTable =
	{
		{ FName(TEXT("Helmet_Default")), 40.0f },
		{ FName(TEXT("Helmet_Bronze")), 30.0f },
		{ FName(TEXT("Helmet_Silver")), 20.0f },
		{ FName(TEXT("Helmet_Gold")), 9.0f },
		{ FName(TEXT("Helmet_Mythic")), 1.0f }
	};
}

void UMP_GameInstance::Init()
{
	Super::Init();
	EnsureSteamAppIdFile();
	// La concha de puntos se precarga ya: nada la carga en frío al morir un lagarto o abrirse un cofre.
	UTN_GameplayAssetSettings::PreloadAsync();

	IOnlineSubsystem* OSS = MPGameInstance_GetPreferredOnlineSubsystem();

	if (OSS)
	{
		const FString SubsystemName = OSS->GetSubsystemName().ToString();
		UpdateStatus(FString::Printf(TEXT("Online Subsystem: %s"), *SubsystemName));

		if (SubsystemName == TEXT("NULL"))
		{
			UpdateStatus(TEXT("WARNING: Steam not available in PIE. Use Standalone Game to test multiplayer."));
		}
		else
		{
			IOnlineSessionPtr Sessions = OSS->GetSessionInterface();
			if (Sessions.IsValid())
			{
				InviteAcceptedDelegateHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(
					FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &UMP_GameInstance::OnSessionUserInviteAccepted));
			}
		}
	}
	else
	{
		UpdateStatus(TEXT("ERROR: No Online Subsystem. Is Steam running?"));
	}

	if (GEngine)
	{
		GEngine->OnNetworkFailure().AddUObject(this, &UMP_GameInstance::OnNetworkFailure);
	}

	FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UMP_GameInstance::HandlePreLoadMap);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMP_GameInstance::HandlePostLoadMap);

	// Salas: el cierre, las plazas y los expulsados se aplican al entrar, en cualquier GameMode (sin tocar ninguno).
	PreLoginHandle = FGameModeEvents::GameModePreLoginEvent.AddUObject(this, &UMP_GameInstance::HandleGameModePreLogin);
	PostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddUObject(this, &UMP_GameInstance::HandleGameModePostLogin);
	// El temporizador de la GameInstance sobrevive a los viajes.
	GetTimerManager().SetTimer(RoomTickHandle, FTimerDelegate::CreateUObject(this, &UMP_GameInstance::RoomTick), 1.f, true);

	LoadCosmeticProfile();
	LoadTutorialProfile();
}

void UMP_GameInstance::EnsureSteamAppIdFile()
{
#if !UE_BUILD_SHIPPING
	if (SteamDevAppId <= 0)
	{
		UpdateStatus(TEXT("WARNING: SteamDevAppId invalido; no se genero steam_appid.txt"));
		return;
	}

	const FString AppIdText = FString::Printf(TEXT("%d\n"), SteamDevAppId);
	const FString AppIdValue = FString::FromInt(SteamDevAppId);
	const FString PrimaryPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), TEXT("Win64"), TEXT("steam_appid.txt"));
	const FString FallbackPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("steam_appid.txt"));

	if (GConfig)
	{
		GConfig->SetInt(TEXT("OnlineSubsystemSteam"), TEXT("SteamDevAppId"), SteamDevAppId, GEngineIni);
	}

	FPlatformMisc::SetEnvironmentVar(TEXT("SteamAppId"), *AppIdValue);
	FPlatformMisc::SetEnvironmentVar(TEXT("SteamGameId"), *AppIdValue);

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(PrimaryPath), true);

	bool bWritten = FFileHelper::SaveStringToFile(AppIdText, *PrimaryPath, FFileHelper::EEncodingOptions::ForceAnsi);
	if (!bWritten)
	{
		bWritten = FFileHelper::SaveStringToFile(AppIdText, *FallbackPath, FFileHelper::EEncodingOptions::ForceAnsi);
	}

	if (bWritten)
	{
		UpdateStatus(FString::Printf(TEXT("Steam AppID %d preparado automaticamente"), SteamDevAppId));
	}
	else
	{
		UpdateStatus(TEXT("WARNING: No se pudo escribir steam_appid.txt automaticamente"));
	}
#endif
}

void UMP_GameInstance::Shutdown()
{
	FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	if (GEngine)
	{
		GEngine->OnNetworkFailure().RemoveAll(this);
	}
	// Los eventos de los GameModes son de todo el proceso: sin esto, otra GameInstance (el editor) llamaría a esta muerta.
	FGameModeEvents::GameModePreLoginEvent.Remove(PreLoginHandle);
	FGameModeEvents::GameModePostLoginEvent.Remove(PostLoginHandle);
	PreLoginHandle.Reset();
	PostLoginHandle.Reset();
	GetTimerManager().ClearTimer(RoomTickHandle);
	SaveCosmeticProfile();
	SaveTutorialProfile();

	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid() && InviteAcceptedDelegateHandle.IsValid())
	{
		Sessions->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteAcceptedDelegateHandle);
		InviteAcceptedDelegateHandle.Reset();
	}

	Super::Shutdown();
}

void UMP_GameInstance::ShowLoadingScreen(const FString& Reason)
{
	// El huevo de la pantalla de carga (UTN_LoadingScreenSubsystem) se cierra en el acto con este mensaje y se queda
	// cerrado hasta que el viaje acabe; la pantalla de texto solo sale si no hay huevo que enseñar.
	if (UTN_LoadingScreenSubsystem* EggLoading = GetSubsystem<UTN_LoadingScreenSubsystem>())
	{
		if (EggLoading->CloseForTravel(Reason))
		{
			if (LoadingScreenWidget)
			{
				LoadingScreenWidget->RemoveFromParent();
				LoadingScreenWidget = nullptr;
			}
			bIsLoadingScreenVisible = false;
			return;
		}
	}

	// Si la loading screen ya estaba "visible" pero el widget fue destruido
	// (ej. map transition destruye el PC que era outer del widget), resetear estado.
	if (bIsLoadingScreenVisible && (!LoadingScreenWidget || !TNVR::IsOnScreen(LoadingScreenWidget)))
	{
		bIsLoadingScreenVisible = false;
		LoadingScreenWidget = nullptr;
	}

	if (bIsLoadingScreenVisible)
	{
		RefreshLoadingText(Reason);
		return;
	}

	APlayerController* PC = GetFirstLocalPlayerController();
	if (!PC)
	{
		return;
	}

	UClass* WidgetClass = LoadingScreenWidgetClass
		? LoadingScreenWidgetClass.Get()
		: UTN_LoadingScreenWidget::StaticClass();

	LoadingScreenWidget = CreateWidget<UUserWidget>(PC, WidgetClass);
	if (!LoadingScreenWidget)
	{
		return;
	}

	TNVR::AddToFullScreen(LoadingScreenWidget, 100000);
	bIsLoadingScreenVisible = true;
	RefreshLoadingText(Reason);
}

void UMP_GameInstance::HideLoadingScreen()
{
	// Si el huevo se cerró para un viaje que no ha llegado a empezar (sesión fallida, sin partidas...), se abre otra vez.
	if (UTN_LoadingScreenSubsystem* EggLoading = GetSubsystem<UTN_LoadingScreenSubsystem>())
	{
		EggLoading->CancelPendingClose();
	}

	if (LoadingScreenWidget)
	{
		LoadingScreenWidget->RemoveFromParent();
		LoadingScreenWidget = nullptr;
	}

	bIsLoadingScreenVisible = false;
}

TArray<FName> UMP_GameInstance::GetUnlockedHelmetIds() const
{
	return GetUnlockedHelmetIdsFor(nullptr);
}

bool UMP_GameInstance::IsHelmetUnlocked(FName HelmetId) const
{
	return CosmeticProfile && HelmetId != NAME_None && CosmeticProfile->UnlockedHelmetIds.Contains(HelmetId);
}

bool UMP_GameInstance::UnlockHelmet(FName HelmetId)
{
	return UnlockHelmetFor(nullptr, HelmetId);
}

bool UMP_GameInstance::EquipHelmet(FName HelmetId)
{
	return EquipHelmetFor(nullptr, HelmetId);
}

FName UMP_GameInstance::GetEquippedHelmetId() const
{
	return GetEquippedHelmetIdFor(nullptr);
}

bool UMP_GameInstance::ForceEquipHelmet(FName HelmetId)
{
	return ForceEquipHelmetFor(nullptr, HelmetId);
}

bool UMP_GameInstance::EquipSkin(FName SkinId)
{
	return EquipSkinFor(nullptr, SkinId);
}

FName UMP_GameInstance::GetEquippedSkinId() const
{
	return GetEquippedSkinIdFor(nullptr);
}

bool UMP_GameInstance::IsCosmeticUnlocked(ETNCosmeticCategory Category, FName Id) const
{
	return IsCosmeticUnlockedFor(nullptr, Category, Id);
}

int32 UMP_GameInstance::GetCosmeticPrice(ETNCosmeticCategory Category, FName Id) const
{
	if (Id == NAME_None) { return 0; }
	if (TNIsBuggyCategory(Category)) { return TNBuggyCosmetics::PriceOf(Category, Id); }
	if (Category == ETNCosmeticCategory::Helmet)
	{
		const FTN_HelmetData* HelmRow = FindHelmetRow(Id, TEXT("GetCosmeticPrice"));
		return HelmRow ? FMath::Max(0, HelmRow->Price) : 0;
	}
	const FTN_SkinData* SkinRow = FindSkinRow(Id, TEXT("GetCosmeticPrice"));
	return SkinRow ? FMath::Max(0, SkinRow->Price) : 0;
}

bool UMP_GameInstance::PurchaseCosmetic(ETNCosmeticCategory Category, FName Id)
{
	return PurchaseCosmeticFor(nullptr, Category, Id);
}

TArray<FName> UMP_GameInstance::GetCosmeticCatalog(ETNCosmeticCategory Category) const
{
	if (TNIsBuggyCategory(Category)) { return TNBuggyCosmetics::CatalogIds(Category); }
	TArray<FName> Out;
	if (Category == ETNCosmeticCategory::Helmet)
	{
		if (const UDataTable* HelmDT = GetHelmetDataTable())
		{
			for (const FName Row : HelmDT->GetRowNames())
			{
				const FTN_HelmetData* HelmRow = HelmDT->FindRow<FTN_HelmetData>(Row, TEXT("GetCosmeticCatalog"));
				if (HelmRow && HelmRow->DisplayMesh) { Out.Add(Row); }
			}
		}
		return Out;
	}
	if (const UDataTable* SkinDT = GetSkinDataTable())
	{
		for (const FName Row : SkinDT->GetRowNames())
		{
			// Todas las filas de la categoría: en la malla de demo el aspecto sale de sus colores y su dibujo (M_TurtleBody).
			const FTN_SkinData* SkinRow = SkinDT->FindRow<FTN_SkinData>(Row, TEXT("GetCosmeticCatalog"));
			if (SkinRow && SkinRow->Category == Category) { Out.Add(Row); }
		}
	}
	return Out;
}

TArray<FName> UMP_GameInstance::GetUnlockedSkinIds() const
{
	return GetUnlockedSkinIdsFor(nullptr);
}

bool UMP_GameInstance::EquipShell(FName ShellId)
{
	return EquipShellFor(nullptr, ShellId);
}

FName UMP_GameInstance::GetEquippedShellId() const
{
	return GetEquippedShellIdFor(nullptr);
}

bool UMP_GameInstance::EquipEyes(FName EyesId)
{
	return EquipEyesFor(nullptr, EyesId);
}

FName UMP_GameInstance::GetEquippedEyesId() const
{
	return GetEquippedEyesIdFor(nullptr);
}

// ── Aspecto de cada jugador local (#311) ──────────────────────────────────────

UTN_CosmeticSaveGame* UMP_GameInstance::CosmeticsFor(const APlayerController* PC) const
{
	// Un invitado de la partida local: su aspecto de la partida (empieza con el de serie y no se guarda).
	if (PC && UTN_LocalPlaySubsystem::IsGuest(PC))
	{
		if (UTN_LocalPlayerProfile* Profile = UTN_LocalPlayerProfile::Get(PC))
		{
			return Profile->GetGuestCosmetics(DefaultUnlockedHelmets);
		}
	}
	return CosmeticProfile;
}

void UMP_GameInstance::SaveCosmeticsFor(const APlayerController* PC) const
{
	if (CosmeticsFor(PC) == CosmeticProfile)
	{
		SaveCosmeticProfile();
	}
}

TArray<FName> UMP_GameInstance::GetUnlockedHelmetIdsFor(const APlayerController* PC) const
{
	const UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	return Profile ? Profile->UnlockedHelmetIds : TArray<FName>();
}

TArray<FName> UMP_GameInstance::GetUnlockedSkinIdsFor(const APlayerController* PC) const
{
	const UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	return Profile ? Profile->UnlockedSkinIds : TArray<FName>();
}

bool UMP_GameInstance::UnlockHelmetFor(const APlayerController* PC, FName HelmetId)
{
	UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	if (!Profile || HelmetId == NAME_None)
	{
		return false;
	}
	if (Profile->UnlockedHelmetIds.Contains(HelmetId))
	{
		return true;
	}
	Profile->UnlockedHelmetIds.Add(HelmetId);
	SaveCosmeticsFor(PC);
	return true;
}

bool UMP_GameInstance::EquipHelmetFor(const APlayerController* PC, FName HelmetId)
{
	UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	if (!Profile || HelmetId == NAME_None || !Profile->UnlockedHelmetIds.Contains(HelmetId))
	{
		return false;
	}
	Profile->EquippedHelmetId = HelmetId;
	SaveCosmeticsFor(PC);
	return true;
}

bool UMP_GameInstance::ForceEquipHelmetFor(const APlayerController* PC, FName HelmetId)
{
	UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	if (!Profile)
	{
		return false;
	}
	// Auto-desbloquear si viene de una estatua de lobby
	if (HelmetId != NAME_None)
	{
		UnlockHelmetFor(PC, HelmetId);
	}
	Profile->EquippedHelmetId = HelmetId; // NAME_None = desequipar
	SaveCosmeticsFor(PC);
	return true;
}

FName UMP_GameInstance::GetEquippedHelmetIdFor(const APlayerController* PC) const
{
	const UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	return Profile ? Profile->EquippedHelmetId : NAME_None;
}

bool UMP_GameInstance::EquipSkinFor(const APlayerController* PC, FName SkinId)
{
	UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	if (!Profile)
	{
		return false;
	}
	Profile->EquippedSkinId = SkinId; // NAME_None = sin skin (válido)
	SaveCosmeticsFor(PC);
	return true;
}

FName UMP_GameInstance::GetEquippedSkinIdFor(const APlayerController* PC) const
{
	const UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	return Profile ? Profile->EquippedSkinId : NAME_None;
}

bool UMP_GameInstance::IsCosmeticUnlockedFor(const APlayerController* PC, ETNCosmeticCategory Category, FName Id) const
{
	if (Id == NAME_None) { return true; }
	const UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	if (TNIsBuggyCategory(Category))
	{
		// Lo gratis del catálogo no hace falta comprarlo.
		if (!TNBuggyCosmetics::IsKnown(Category, Id)) { return false; }
		return TNBuggyCosmetics::PriceOf(Category, Id) == 0 || (Profile && Profile->UnlockedBuggyIds.Contains(Id));
	}
	if (!Profile) { return false; }
	return Category == ETNCosmeticCategory::Helmet ? Profile->UnlockedHelmetIds.Contains(Id) : Profile->UnlockedSkinIds.Contains(Id);
}

bool UMP_GameInstance::PurchaseCosmeticFor(const APlayerController* PC, ETNCosmeticCategory Category, FName Id)
{
	UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	if (!Profile || Id == NAME_None) { return false; }
	if (TNIsBuggyCategory(Category) && !TNBuggyCosmetics::IsKnown(Category, Id)) { return false; }
	if (IsCosmeticUnlockedFor(PC, Category, Id)) { return true; }
	const int32 Price = GetCosmeticPrice(Category, Id);
	if (Price > Profile->AccumulatedRaceScore) { return false; }
	Profile->AccumulatedRaceScore -= Price;
	if (Category == ETNCosmeticCategory::Helmet) { Profile->UnlockedHelmetIds.AddUnique(Id); }
	else if (TNIsBuggyCategory(Category)) { Profile->UnlockedBuggyIds.AddUnique(Id); }
	else { Profile->UnlockedSkinIds.AddUnique(Id); }
	SaveCosmeticsFor(PC);
	UE_LOG(LogTortunabo, Log, TEXT("[Tienda] Desbloqueado '%s' por %d (quedan %d)%s."), *Id.ToString(), Price, Profile->AccumulatedRaceScore,
		Profile == CosmeticProfile ? TEXT("") : TEXT(" para esta partida (invitado local)"));
	return true;
}

bool UMP_GameInstance::EquipShellFor(const APlayerController* PC, FName ShellId)
{
	UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	if (!Profile) { return false; }
	Profile->EquippedShellId = ShellId;
	SaveCosmeticsFor(PC);
	return true;
}

FName UMP_GameInstance::GetEquippedShellIdFor(const APlayerController* PC) const
{
	const UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	return Profile ? Profile->EquippedShellId : NAME_None;
}

bool UMP_GameInstance::EquipEyesFor(const APlayerController* PC, FName EyesId)
{
	UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	if (!Profile) { return false; }
	Profile->EquippedEyesId = EyesId;
	SaveCosmeticsFor(PC);
	return true;
}

FName UMP_GameInstance::GetEquippedEyesIdFor(const APlayerController* PC) const
{
	const UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	return Profile ? Profile->EquippedEyesId : NAME_None;
}

int32 UMP_GameInstance::GetAccumulatedRaceScoreFor(const APlayerController* PC) const
{
	const UTN_CosmeticSaveGame* Profile = CosmeticsFor(PC);
	return Profile ? Profile->AccumulatedRaceScore : 0;
}

TArray<FName> UMP_GameInstance::GetUnlockedBuggyIds() const
{
	return CosmeticProfile ? CosmeticProfile->UnlockedBuggyIds : TArray<FName>();
}

bool UMP_GameInstance::EquipBuggyLook(const FTN_BuggyLook& Look)
{
	if (!CosmeticProfile) { return false; }
	const FTN_BuggyLook Clean = TNBuggyCosmetics::Sanitize(Look);
	if (!IsCosmeticUnlocked(ETNCosmeticCategory::BuggyModel, Clean.ModelId) || !IsCosmeticUnlocked(ETNCosmeticCategory::BuggyPaint, Clean.PaintId))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Tienda] Buggy '%s' sin desbloquear: no se equipa."), *TNBuggyCosmetics::LookKey(Clean));
		return false;
	}
	CosmeticProfile->EquippedBuggyLook = Clean;
	SaveCosmeticProfile();
	return true;
}

FTN_BuggyLook UMP_GameInstance::GetEquippedBuggyLook() const
{
	return CosmeticProfile ? TNBuggyCosmetics::Sanitize(CosmeticProfile->EquippedBuggyLook) : FTN_BuggyLook();
}

const FTN_HelmetData* UMP_GameInstance::FindHelmetRow(FName HelmetId, const TCHAR* Ctx) const
{
	const UDataTable* HelmDT = GetHelmetDataTable();
	return HelmDT ? HelmDT->FindRow<FTN_HelmetData>(HelmetId, Ctx) : nullptr;
}

const FTN_SkinData* UMP_GameInstance::FindSkinRow(FName SkinId, const TCHAR* Ctx) const
{
	const UDataTable* SkinDT = GetSkinDataTable();
	return SkinDT ? SkinDT->FindRow<FTN_SkinData>(SkinId, Ctx) : nullptr;
}

FName UMP_GameInstance::OpenHelmetCrate()
{
	return OpenHelmetCrateFor(nullptr);
}

FName UMP_GameInstance::OpenHelmetCrateFor(const APlayerController* PC)
{
	if (HelmetCrateTable.Num() == 0)
	{
		return NAME_None;
	}

	float TotalWeight = 0.0f;
	for (const FTN_HelmetCrateEntry& Entry : HelmetCrateTable)
	{
		TotalWeight += FMath::Max(0.0f, Entry.Weight);
	}

	if (TotalWeight <= KINDA_SMALL_NUMBER)
	{
		return NAME_None;
	}

	float Roll = FMath::FRandRange(0.0f, TotalWeight);
	for (const FTN_HelmetCrateEntry& Entry : HelmetCrateTable)
	{
		Roll -= FMath::Max(0.0f, Entry.Weight);
		if (Roll <= 0.0f && Entry.HelmetId != NAME_None)
		{
			UnlockHelmetFor(PC, Entry.HelmetId);
			return Entry.HelmetId;
		}
	}

	return NAME_None;
}

IOnlineSessionPtr UMP_GameInstance::GetSessionInterface() const
{
	IOnlineSubsystem* OSS = MPGameInstance_GetPreferredOnlineSubsystem();

	if (!OSS)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[MP] Online Subsystem is NULL. Make sure Steam is running."));
		return nullptr;
	}

	return OSS->GetSessionInterface();
}

void UMP_GameInstance::UpdateStatus(const FString& Message)
{
	UE_LOG(LogTortunabo, Log, TEXT("[MP] %s"), *Message);

	// Un mensaje idéntico al anterior no se apunta otra vez (p. ej. el aviso de sala que además es estado).
	if (StatusLog.Num() > 0 && StatusLog.Last() == Message)
	{
		return;
	}
	if (StatusLog.Num() >= MaxStatusLines)
	{
		StatusLog.RemoveAt(0);
	}
	StatusLog.Add(Message);

	OnStatusChanged.Broadcast(BuildStatusLog());
}

FString UMP_GameInstance::BuildStatusLog() const
{
	return FString::Join(StatusLog, TEXT("\n"));
}

void UMP_GameInstance::HostSession()
{
	// Otro «Crear» (o «Crear» con una entrada en marcha) no empieza una sala nueva: destruiría la sesión que se está creando. El
	// primer intento sigue y la pantalla de carga ya es la suya.
	if (RoomOp.IsBusy())
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Salas] Crear sala ignorado: ya hay una sala creándose o una entrada en marcha."));
		return;
	}
	EnsureActiveRoom();
	ShowLoadingScreen(FText::Format(NSLOCTEXT("TNRooms", "CreatingRoom", "Creando la sala «{0}»..."), TNRoomNames::Get(ActiveRoom.NameId)).ToString());

	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		UpdateStatus(TEXT("ERROR: No session interface"));
		HideLoadingScreen();
		PostRoomNotice(NSLOCTEXT("TNRooms", "NoOnline", "No hay conexión con Steam: ábrelo y vuelve a intentarlo."), true);
		return;
	}

	FNamedOnlineSession* Existing = Sessions->GetNamedSession(NAME_GameSession);
	if (Existing)
	{
		RoomOp.bHostAfterDestroy = true;
		RoomOp.bJoinAfterDestroy = false;
		RoomOpStartTime = FPlatformTime::Seconds();
		Sessions->ClearOnDestroySessionCompleteDelegates(this);
		Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UMP_GameInstance::OnDestroySessionComplete));
		Sessions->DestroySession(NAME_GameSession);
		UE_LOG(LogTortunabo, Log, TEXT("[MP] Destroying old session first..."));
		return;
	}

	Sessions->ClearOnCreateSessionCompleteDelegates(this);
	Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UMP_GameInstance::OnCreateSessionComplete));

	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = false;
	Settings.NumPublicConnections = ActiveRoom.MaxPlayers;
	Settings.bShouldAdvertise = true;
	Settings.bUsesPresence = true;
	Settings.bUseLobbiesIfAvailable = true;
	// Siempre «se puede unir» para Steam: así una sala cerrada sigue saliendo en la lista (con su candado). El cierre y las
	// plazas los aplica el servidor al entrar (HandleGameModePreLogin), y la lista y el código avisan antes de intentarlo.
	Settings.bAllowJoinInProgress = true;
	// También las privadas son lobbies públicos de Steam: uno privado no sale en ninguna búsqueda y el código no lo
	// encontraría. La lista las deja fuera con PRIVATE = 0 (en el servidor de Steam y aquí).
	Settings.bAllowJoinViaPresence = true;
	Settings.bAllowInvites = true;
	Settings.bAllowJoinViaPresenceFriendsOnly = false;
	ApplyRoomSettings(Settings, 1);
	AdvertisedPlayers = 1;
	AdvertisedMode = static_cast<int32>(SelectedProcMode);
	AdvertisedLocked = ActiveRoom.bLocked ? 1 : 0;

	UpdateStatus(FString::Printf(TEXT("Creating Steam lobby (%s, %d plazas, código %s)..."), ActiveRoom.bPrivate ? TEXT("privada") : TEXT("pública"),
		ActiveRoom.MaxPlayers, *ActiveRoom.Code));
	RoomOp.bCreating = true;
	RoomOpStartTime = FPlatformTime::Seconds();
	if (!Sessions->CreateSession(0, NAME_GameSession, Settings) && RoomOp.bCreating)
	{
		// No ha arrancado y no ha avisado: se da por fallida ya (si ha avisado, OnCreateSessionComplete ya la cerró).
		OnCreateSessionComplete(NAME_GameSession, false);
	}
}

void UMP_GameInstance::HostSessionWithMode(ETNProcGameMode Mode)
{
	// Desde el menú solo se ofrecen los modos de TNLobbyMission::GetMenuModes; el lobby lo lee de aquí al viajar (ATN_HQGameMode::BeginMatchTravel).
	FTNRoomConfig Config = MakeRoomDraft();
	Config.Mode = TNLobbyMission::NormalizeMenuMode(Mode);
	UE_LOG(LogTortunabo, Log, TEXT("[MP] Crear partida en modo %s."), *UEnum::GetValueAsString(Config.Mode));
	HostRoom(Config);
}

void UMP_GameInstance::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegates(this);
	}
	RoomOp.bCreating = false;

	if (!bWasSuccessful)
	{
		UpdateStatus(FString::Printf(TEXT("ERROR: Failed to create session '%s'"), *SessionName.ToString()));
		HideLoadingScreen();
		PostRoomNotice(NSLOCTEXT("TNRooms", "CreateFailed", "No se ha podido crear la sala. ¿Está Steam abierto y conectado?"), true);
		return;
	}

	// La sesión ya está: falta el viaje (hasta que cargue el mapa, HandlePostLoadMap, otro «Crear» o «Unirse» sigue sobrando).
	RoomOp.bTravelling = true;
	RoomOpStartTime = FPlatformTime::Seconds();
	UpdateStatus(FString::Printf(TEXT("Lobby '%s' created! Travelling to game map..."), *SessionName.ToString()));

	// El viaje sale cuando el huevo de la pantalla de carga ha terminado de cerrarse (con el subsistema NULL la sesión se
	// crea en el acto y el LoadMap congelaría el cierre a medias).
	const FString TravelURL = GameMapPath + TEXT("?listen");
	TWeakObjectPtr<UMP_GameInstance> WeakThis(this);
	TFunction<void()> Travel = [WeakThis, TravelURL]()
	{
		UMP_GameInstance* Self = WeakThis.Get();
		UWorld* TravelWorld = Self ? Self->GetWorld() : nullptr;
		if (!TravelWorld)
		{
			if (Self)
			{
				Self->RoomOp.bTravelling = false;
			}
			return;
		}
		// Menú que ya escucha (el Standalone del editor como servidor escuchando arranca en LVL_Menu?Listen): con Steam, el
		// socket de escucha del puerto virtual 17777 se cierra un tic después de apagar su driver, y el LoadMap del
		// ServerTravel intentaba escuchar en el lobby antes; fallaba (NetDriverListenFailure) y el motor devolvía al menú.
		// Se apaga aquí el driver del menú y se viaja medio segundo después, con el puerto ya libre.
		if (TravelWorld->GetNetDriver() && GEngine)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[MP] El menú ya escuchaba: se cierra su driver de red antes de viajar al lobby."));
			GEngine->ShutdownWorldNetDriver(TravelWorld);
			FTimerHandle DelayedTravel;
			TravelWorld->GetTimerManager().SetTimer(DelayedTravel, FTimerDelegate::CreateWeakLambda(Self, [WeakThis, TravelURL]()
			{
				if (UWorld* LaterWorld = WeakThis.IsValid() ? WeakThis->GetWorld() : nullptr)
				{
					LaterWorld->ServerTravel(TravelURL);
				}
			}), 0.5f, false);
			return;
		}
		TravelWorld->ServerTravel(TravelURL);
	};
	if (UTN_LoadingScreenSubsystem* EggLoading = GetSubsystem<UTN_LoadingScreenSubsystem>())
	{
		EggLoading->RunWhenClosed(MoveTemp(Travel));
	}
	else
	{
		Travel();
	}
}

void UMP_GameInstance::FindAndJoinSession()
{
	if (RoomOp.IsBusy())
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Salas] Unirse a la primera ignorado: ya hay una sala creándose o una entrada en marcha."));
		return;
	}
	ShowLoadingScreen(TEXT("Buscando salas..."));
	StartRoomSearch(ETNRoomSearch::QuickJoin);
}

void UMP_GameInstance::OnFindSessionsComplete(bool bWasSuccessful)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnFindSessionsCompleteDelegates(this);
	}

	const ETNRoomSearch Purpose = RoomSearchPurpose;
	const FString Code = RoomSearchCode;
	RoomSearchPurpose = ETNRoomSearch::None;
	RoomSearchCode.Reset();
	const TSharedPtr<FOnlineSessionSearch> Search = SessionSearch;
	if (Purpose == ETNRoomSearch::None)
	{
		// Respuesta tardía de una búsqueda que ya se dio por perdida (RoomTick).
		return;
	}

	// Las salas de Tortunavy de los resultados (con el NULL llega todo lo de la red local; con Steam, ya filtrado).
	TArray<FTNRoomListing> Found;
	if (bWasSuccessful && Search.IsValid())
	{
		for (int32 i = 0; i < Search->SearchResults.Num(); ++i)
		{
			FTNRoomListing Listing;
			if (ReadRoomListing(Search->SearchResults[i].Session, i, Listing))
			{
				Found.Add(MoveTemp(Listing));
			}
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Salas] %s"), *FString::Printf(TEXT("Búsqueda de salas: %s, %d resultados, %d de Tortunavy."), bWasSuccessful ? TEXT("ok") : TEXT("fallida"),
		Search.IsValid() ? Search->SearchResults.Num() : 0, Found.Num()));

	if (Purpose == ETNRoomSearch::Code)
	{
		const FTNRoomListing* Match = Found.FindByPredicate([&Code](const FTNRoomListing& Listing) { return Listing.Code == Code; });
		if (!bWasSuccessful)
		{
			PostRoomNotice(NSLOCTEXT("TNRooms", "SearchFailed", "No se ha podido buscar salas. ¿Está Steam abierto y conectado?"), true);
		}
		else if (!Match)
		{
			PostRoomNotice(FText::Format(NSLOCTEXT("TNRooms", "CodeNotFound",
				"No hay ninguna sala con el código {0}. Revisa el código (con Steam, una sala llena tampoco aparece)."), FText::FromString(Code)), true);
		}
		else
		{
			const FText RoomName = TNRoomNames::Get(Match->NameId);
			if (Match->bLocked)
			{
				PostRoomNotice(FText::Format(NSLOCTEXT("TNRooms", "CodeLocked", "«{0}» está cerrada: el anfitrión no deja entrar a nadie más."), RoomName), true);
			}
			else if (Match->IsFull())
			{
				PostRoomNotice(FText::Format(NSLOCTEXT("TNRooms", "CodeFull", "«{0}» está llena ({1}/{2})."), RoomName, FText::AsNumber(Match->Players),
					FText::AsNumber(Match->MaxPlayers)), true);
			}
			else
			{
				JoinRoomResult(Search->SearchResults[Match->SearchIndex], RoomName);
			}
		}
	}
	else
	{
		// La lista (y «unirse a la primera», que mira la misma): solo las públicas; primero las que tienen sitio.
		Found.RemoveAll([](const FTNRoomListing& Listing) { return Listing.bPrivate; });
		Found.StableSort([](const FTNRoomListing& A, const FTNRoomListing& B)
		{
			if (A.CanJoin() != B.CanJoin())
			{
				return A.CanJoin();
			}
			return A.Players > B.Players;
		});
		if (bWasSuccessful)
		{
			RoomListings = MoveTemp(Found);
			RoomListSearch = Search;
		}
		bRoomListReady = true;

		if (Purpose == ETNRoomSearch::QuickJoin)
		{
			const int32 FirstOpen = RoomListings.IndexOfByPredicate([](const FTNRoomListing& Listing) { return Listing.CanJoin(); });
			if (!bWasSuccessful)
			{
				HideLoadingScreen();
				PostRoomNotice(NSLOCTEXT("TNRooms", "SearchFailed", "No se ha podido buscar salas. ¿Está Steam abierto y conectado?"), true);
			}
			else if (FirstOpen == INDEX_NONE)
			{
				HideLoadingScreen();
				PostRoomNotice(NSLOCTEXT("TNRooms", "NoOpenRooms", "No hay partidas públicas abiertas ahora mismo: crea una o entra con un código."), true);
			}
			else
			{
				JoinListedRoom(FirstOpen);
			}
		}
		else if (!bWasSuccessful)
		{
			PostRoomNotice(NSLOCTEXT("TNRooms", "SearchFailed", "No se ha podido buscar salas. ¿Está Steam abierto y conectado?"), true);
		}
		OnRoomListChanged.Broadcast();
	}

	// La que esperaba turno.
	if (QueuedRoomSearch != ETNRoomSearch::None)
	{
		const ETNRoomSearch Next = QueuedRoomSearch;
		const FString NextCode = QueuedRoomCode;
		QueuedRoomSearch = ETNRoomSearch::None;
		QueuedRoomCode.Reset();
		StartRoomSearch(Next, NextCode);
	}
}

void UMP_GameInstance::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegates(this);
	}
	RoomOp.bJoining = false;

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		UpdateStatus(FString::Printf(TEXT("ERROR joining '%s': code %d"), *SessionName.ToString(), static_cast<int32>(Result)));
		HideLoadingScreen();
		FText Why;
		switch (Result)
		{
		case EOnJoinSessionCompleteResult::SessionIsFull:
			Why = TNRoomText::RefusedMessage(TNRoomKeys::RefuseFull());
			break;
		case EOnJoinSessionCompleteResult::SessionDoesNotExist:
			Why = NSLOCTEXT("TNRooms", "JoinGone", "Esa sala ya no existe: puede que el anfitrión se haya ido. Actualiza la lista.");
			break;
		case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:
			Why = NSLOCTEXT("TNRooms", "JoinNoAddress", "No se ha podido contactar con el anfitrión de la sala.");
			break;
		default:
			Why = TNRoomText::RefusedMessage(FString());
			break;
		}
		PostRoomNotice(Why, true);
		return;
	}

	FString ConnectInfo;
	if (Sessions.IsValid() && Sessions->GetResolvedConnectString(SessionName, ConnectInfo) && !ConnectInfo.IsEmpty())
	{
		// Primera partida de esta máquina: se dice al entrar y el servidor la pone ya en el tutorial (Docs/Tutorial.md).
		if (!HasCompletedTutorial())
		{
			ConnectInfo += FString::Printf(TEXT("?%s=1"), TutorialJoinOption());
		}
		// Ya se está dentro de la sesión: falta la conexión (hasta que cargue el mapa, otro «Crear» o «Unirse» sigue sobrando).
		RoomOp.bTravelling = true;
		RoomOpStartTime = FPlatformTime::Seconds();
		ShowLoadingScreen(PendingJoinRoomName.IsEmpty() ? FString(TEXT("Conectando a la partida..."))
			: FText::Format(NSLOCTEXT("TNRooms", "Connecting", "Entrando en «{0}»..."), PendingJoinRoomName).ToString());
		// Igual que al crear la sesión: se conecta cuando el huevo ya está cerrado del todo.
		TWeakObjectPtr<UMP_GameInstance> WeakThis(this);
		TFunction<void()> Travel = [WeakThis, ConnectInfo]()
		{
			if (APlayerController* PC = WeakThis.IsValid() ? WeakThis->GetFirstLocalPlayerController() : nullptr)
			{
				PC->ClientTravel(ConnectInfo, TRAVEL_Absolute);
			}
			else if (WeakThis.IsValid())
			{
				WeakThis->RoomOp.bTravelling = false;
			}
		};
		if (UTN_LoadingScreenSubsystem* EggLoading = GetSubsystem<UTN_LoadingScreenSubsystem>())
		{
			EggLoading->RunWhenClosed(MoveTemp(Travel));
		}
		else
		{
			Travel();
		}
	}
	else
	{
		UpdateStatus(TEXT("ERROR: Could not resolve connect string"));
		HideLoadingScreen();
		// Se había entrado en la sesión, pero sin dirección no hay partida: fuera de ella para poder intentarlo otra vez.
		DestroyCurrentSession();
		PostRoomNotice(NSLOCTEXT("TNRooms", "JoinNoAddress", "No se ha podido contactar con el anfitrión de la sala."), true);
	}
}

void UMP_GameInstance::InviteFriends()
{
	IOnlineSubsystem* OSS = MPGameInstance_GetPreferredOnlineSubsystem();

	if (!OSS)
	{
		UpdateStatus(TEXT("ERROR: Steam not available for invites."));
		return;
	}

	IOnlineSessionPtr Sessions = OSS->GetSessionInterface();
	if (!Sessions.IsValid())
	{
		UpdateStatus(TEXT("ERROR: No session interface for invites."));
		return;
	}

	if (!Sessions->GetNamedSession(NAME_GameSession))
	{
		UpdateStatus(TEXT("No active session. Host a game first before inviting friends."));
		return;
	}

	IOnlineExternalUIPtr ExternalUI = OSS->GetExternalUIInterface();
	if (ExternalUI.IsValid())
	{
		ExternalUI->ShowInviteUI(0, NAME_GameSession);
		UpdateStatus(TEXT("Opening Steam invite overlay..."));
	}
	else
	{
		UpdateStatus(TEXT("ERROR: Steam overlay not available."));
	}
}

void UMP_GameInstance::OnSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult)
{
	if (!bWasSuccessful)
	{
		UpdateStatus(TEXT("ERROR: Failed to accept Steam invite."));
		return;
	}

	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		UpdateStatus(TEXT("ERROR: No session interface to join invited session."));
		return;
	}

	// Con una sala creándose o una entrada en marcha, la invitación sobra: aceptarla destruiría esa sesión. Se acepta otra vez al acabar.
	if (RoomOp.IsBusy())
	{
		UpdateStatus(TEXT("Invite ignored: a room is being created or joined. Accept it again when it finishes."));
		return;
	}

	// Una invitación entra también en salas privadas; el cierre, las plazas y los expulsados los mira el servidor al entrar.
	bKickedFromRoom = false;
	PendingJoinRoomName = FText::GetEmpty();

	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		RoomOp.bHostAfterDestroy = false;
		RoomOp.bJoinAfterDestroy = true;
		RoomOpStartTime = FPlatformTime::Seconds();
		PendingInviteResult = InviteResult;
		Sessions->ClearOnDestroySessionCompleteDelegates(this);
		Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UMP_GameInstance::OnDestroySessionComplete));
		Sessions->DestroySession(NAME_GameSession);
		UE_LOG(LogTortunabo, Log, TEXT("[MP] Destroying current session to join invite..."));
		return;
	}

	UpdateStatus(TEXT("Joining invited session..."));
	BeginSessionJoin(InviteResult, ControllerId);
}

void UMP_GameInstance::DestroyCurrentSession()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		return;
	}

	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->ClearOnDestroySessionCompleteDelegates(this);
		Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UMP_GameInstance::OnDestroySessionComplete));
		Sessions->DestroySession(NAME_GameSession);
		// Solo al log: el aviso del motivo (checksum, host perdido…) se escribe antes y debe seguir siendo el último.
		UE_LOG(LogTortunabo, Log, TEXT("[MP] Destroying session..."));
	}
}

void UMP_GameInstance::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnDestroySessionCompleteDelegates(this);
	}

	// Solo al log: el menú enseña el último estado y esto taparía el motivo por el que se cerró la sesión.
	UE_LOG(LogTortunabo, Log, TEXT("[MP] Session '%s' destroyed (ok=%d)"), *SessionName.ToString(), bWasSuccessful);

	if (RoomOp.bHostAfterDestroy)
	{
		RoomOp.bHostAfterDestroy = false;
		RoomOp.bJoinAfterDestroy = false;
		HostSession();
	}
	else if (RoomOp.bJoinAfterDestroy)
	{
		RoomOp.bJoinAfterDestroy = false;
		RoomOp.bHostAfterDestroy = false;
		BeginSessionJoin(PendingInviteResult, 0);
	}
}

void UMP_GameInstance::BeginSessionJoin(const FOnlineSessionSearchResult& Result, int32 ControllerId)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		HideLoadingScreen();
		PostRoomNotice(NSLOCTEXT("TNRooms", "NoOnline", "No hay conexión con Steam: ábrelo y vuelve a intentarlo."), true);
		return;
	}
	RoomOp.bJoining = true;
	RoomOpStartTime = FPlatformTime::Seconds();
	Sessions->ClearOnJoinSessionCompleteDelegates(this);
	Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UMP_GameInstance::OnJoinSessionComplete));
	if (!Sessions->JoinSession(ControllerId, NAME_GameSession, Result) && RoomOp.bJoining)
	{
		// No ha arrancado y no ha avisado: se da por fallida ya (si ha avisado, OnJoinSessionComplete ya la cerró).
		OnJoinSessionComplete(NAME_GameSession, EOnJoinSessionCompleteResult::UnknownError);
	}
}

int32 UMP_GameInstance::GetMaxPlayers() const
{
	if (UTN_LocalPlaySubsystem::IsLocalGame(this))
	{
		return TNLocalPlay::MaxPlayers;
	}
	return bHasActiveRoom ? ActiveRoom.MaxPlayers : MaxPlayers;
}

void UMP_GameInstance::StartLocalGame()
{
	UTN_LocalPlaySubsystem* LocalPlay = GetSubsystem<UTN_LocalPlaySubsystem>();
	if (!LocalPlay)
	{
		return;
	}
	// Sin sesión ni sala: nada de Steam en la partida local (y si quedaba una sesión vieja, fuera).
	DestroyCurrentSession();
	ResetRoomState();
	UpdateStatus(TEXT("Partida local: hasta 4 jugadores en este PC."));
	LocalPlay->StartLocalGame(GameMapPath);
}

void UMP_GameInstance::HandleReturnToMenu()
{
	ShowLoadingScreen(TEXT("Volviendo al menú..."));

	// Stop all audio capture before travel to prevent WASAPI crash.
	UProximityVoiceComponent::ShutdownAllCapture(GetWorld());

	// Partida local: los invitados fuera (sus tortugas y sus vistas) y la pantalla, los mandos y la calidad como estaban.
	if (UTN_LocalPlaySubsystem* LocalPlay = GetSubsystem<UTN_LocalPlaySubsystem>())
	{
		LocalPlay->EndLocalGame();
	}

	DestroyCurrentSession();

	if (APlayerController* PC = GetFirstLocalPlayerController())
	{
		PC->ClientTravel(MenuMapPath, TRAVEL_Absolute);
	}
}

void UMP_GameInstance::NotifyClientPendingTravel()
{
	bIsPendingTravel = true;
	ShowLoadingScreen(TEXT("El servidor está cambiando de mapa..."));
	UE_LOG(LogTortunabo, Log, TEXT("[MP] NotifyClientPendingTravel: bIsPendingTravel=true, loading screen shown."));
}

void UMP_GameInstance::HandlePreLoadMap(const FString& MapName)
{
	// Marcar que estamos en una transición de nivel.
	// OnNetworkFailure usa este flag para no destruir la sesión si el error
	// es transitorio (ej. colisión de socket Steam durante ServerTravel).
	bIsPendingTravel = true;
	bNeedsListenRetry = false; // Reset: se establece en OnNetworkFailure si falla el listen

	// Guardar el nombre del mapa para el retry de listen server.
	PendingListenURL = MapName;

	// Safety net: stop all audio capture streams before any map transition.
	// Individual GameModes already call ShutdownAllCapture explicitly, but this
	// catches any travel path we might have missed (invites, network failures, etc.).
	UProximityVoiceComponent::ShutdownAllCapture(GetWorld());

	ShowLoadingScreen(UTN_LoadingScreenSubsystem::FriendlyStatusForMap(MapName));
}

void UMP_GameInstance::HandlePostLoadMap(UWorld* LoadedWorld)
{
	bIsPendingTravel = false;

	// De vuelta en el menú principal: la sala de antes (si se era anfitrión) ya no existe.
	// PostLoadMapWithWorld salta para todas las GameInstance del proceso (PIE con varias ventanas): solo cuenta el mundo propio.
	if (LoadedWorld && LoadedWorld->GetGameInstance() == this)
	{
		// El viaje de crear o entrar en una sala ha terminado (llegó al mapa de la partida o volvió al menú): se puede volver a pedir.
		RoomOp.bTravelling = false;
		if (IsMenuWorld(LoadedWorld))
		{
			ResetRoomState();
			// Si se llegó al menú sin pasar por HandleReturnToMenu (un fallo, un viaje de consola), la partida local acaba aquí.
			if (UTN_LocalPlaySubsystem* LocalPlay = GetSubsystem<UTN_LocalPlaySubsystem>())
			{
				LocalPlay->EndLocalGame();
			}
		}
	}

	// ── Si un auto-rejoin estaba pendiente y llegamos a un mapa ──
	if (bPendingAutoRejoin)
	{
		// Recrear la loading screen si fue destruida durante el map transition
		// (el widget se destruye junto al PC viejo que era su outer).
		ShowLoadingScreen(TEXT("Reconectando a la partida..."));

		if (LoadedWorld)
		{
			// Si no se pudo arrancar el timer antes (no había World), arrancarlo ahora
			if (!LoadedWorld->GetTimerManager().IsTimerActive(AutoRejoinTimerHandle))
			{
				UE_LOG(LogTortunabo, Log, TEXT("[MP] PostLoadMap: arrancando auto-rejoin timer (deferred)."));
				LoadedWorld->GetTimerManager().SetTimer(
					AutoRejoinTimerHandle,
					FTimerDelegate::CreateUObject(this, &UMP_GameInstance::AttemptAutoRejoin),
					3.0f, false);
			}
		}
		// No ocultar loading screen — AttemptAutoRejoin lo hace
		return;
	}

	// ── Listen retry: si el listen socket falló durante LoadMap, reintentar ──
	if (bNeedsListenRetry && LoadedWorld)
	{
		bNeedsListenRetry = false;
		ListenRetryCount = MaxListenRetries;

		UE_LOG(LogTortunabo, Warning, TEXT("[MP] Listen socket failed during travel. Scheduling retry (%d attempts, every 400ms)..."), ListenRetryCount);

		// Usar un timer del mundo nuevo para reintentar
		if (UWorld* World = LoadedWorld)
		{
			World->GetTimerManager().SetTimer(
				ListenRetryTimerHandle,
				FTimerDelegate::CreateUObject(this, &UMP_GameInstance::RetryListenServer),
				0.4f, true);
		}
		// No ocultar la loading screen todavía — la ocultamos cuando el listen tenga éxito o los reintentos se agoten.
		return;
	}

	HideLoadingScreen();
}

void UMP_GameInstance::RetryListenServer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[MP] RetryListenServer: World is null."));
		if (World) { World->GetTimerManager().ClearTimer(ListenRetryTimerHandle); }
		HideLoadingScreen();
		return;
	}

	// Si ya tiene NetDriver, el listen server ya funciona (quizá otro path lo creó).
	if (World->GetNetDriver())
	{
		UE_LOG(LogTortunabo, Log, TEXT("[MP] RetryListenServer: NetDriver already exists — listen server is running."));
		World->GetTimerManager().ClearTimer(ListenRetryTimerHandle);
		HideLoadingScreen();
		return;
	}

	--ListenRetryCount;

	FURL ListenURL(nullptr, *PendingListenURL, TRAVEL_Absolute);
	ListenURL.AddOption(TEXT("listen"));

	FString Error;
	if (World->Listen(ListenURL))
	{
		UE_LOG(LogTortunabo, Log, TEXT("[MP] RetryListenServer: ✓ Listen server created successfully on retry!"));
		World->GetTimerManager().ClearTimer(ListenRetryTimerHandle);
		HideLoadingScreen();
		return;
	}

	UE_LOG(LogTortunabo, Warning, TEXT("[MP] RetryListenServer: Listen failed (retries left: %d)"), ListenRetryCount);

	if (ListenRetryCount <= 0)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[MP] RetryListenServer: All retries exhausted. Destroying session and returning to menu."));
		World->GetTimerManager().ClearTimer(ListenRetryTimerHandle);
		HideLoadingScreen();
		UpdateStatus(TEXT("ERROR: No se pudo crear el servidor. Volviendo al menú..."));
		DestroyCurrentSession();

		if (APlayerController* PC = GetFirstLocalPlayerController())
		{
			PC->ClientTravel(MenuMapPath, TRAVEL_Absolute);
		}
	}
}

void UMP_GameInstance::AttemptAutoRejoin()
{
	if (!bPendingAutoRejoin)
	{
		return;
	}

	--AutoRejoinRetryCount;

	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		UE_LOG(LogTortunabo, Error, TEXT("[MP] AttemptAutoRejoin: No session interface."));
		bPendingAutoRejoin = false;
		HideLoadingScreen();
		UpdateStatus(TEXT("ERROR: No se pudo reconectar — sin interfaz de sesión."));
		return;
	}

	// Verificar que la sesión sigue existiendo
	FNamedOnlineSession* ExistingSession = Sessions->GetNamedSession(NAME_GameSession);
	if (!ExistingSession)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[MP] AttemptAutoRejoin: No hay sesión activa. Buscando sesión..."));
		// La sesión se perdió — intentar buscar vía Find
		bPendingAutoRejoin = false;
		HideLoadingScreen();
		UpdateStatus(TEXT("Sesión perdida. Usa 'Buscar Partida' para reconectar."));
		return;
	}

	FString ConnectInfo;
	if (Sessions->GetResolvedConnectString(NAME_GameSession, ConnectInfo) && !ConnectInfo.IsEmpty())
	{
		ShowLoadingScreen(FString::Printf(TEXT("Reconectando... (intento %d)"), MaxAutoRejoinRetries - AutoRejoinRetryCount));
		APlayerController* PC = GetFirstLocalPlayerController();
		if (PC)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[MP] AttemptAutoRejoin: ClientTravel to '%s'"), *ConnectInfo);
			bPendingAutoRejoin = false; // El travel cargará un mapa → HandlePostLoadMap limpiará
			PC->ClientTravel(ConnectInfo, TRAVEL_Absolute);
			return;
		}
		else
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[MP] AttemptAutoRejoin: No PlayerController for ClientTravel."));
		}
	}
	else
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[MP] AttemptAutoRejoin: Could not resolve connect string (retries left: %d)"), AutoRejoinRetryCount);
	}

	if (AutoRejoinRetryCount <= 0)
	{
		bPendingAutoRejoin = false;
		HideLoadingScreen();
		UpdateStatus(TEXT("ERROR: No se pudo reconectar tras varios intentos. Volviendo al menú..."));
		DestroyCurrentSession();
		APlayerController* PC = GetFirstLocalPlayerController();
		if (PC)
		{
			PC->ClientTravel(MenuMapPath, TRAVEL_Absolute);
		}
		return;
	}

	// Reintentar en 2.5 segundos
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			AutoRejoinTimerHandle,
			FTimerDelegate::CreateUObject(this, &UMP_GameInstance::AttemptAutoRejoin),
			2.5f, false);
	}
}

namespace
{
	/** Migra en memoria un perfil cosmético antiguo a la versión actual. Devuelve si hay que volver a guardarlo. */
	bool TNMigrateCosmeticProfile(UTN_CosmeticSaveGame& Profile, const FString& Slot)
	{
		switch (TNSaveLogic::DecideMigration(Profile.SaveVersion, TNSaveLogic::COSMETIC_SAVE_VERSION))
		{
		case TNSaveLogic::EMigration::Upgrade:
			// v0 → v1: listas sin NAME_None ni repetidos y puntos nunca negativos.
			UE_LOG(LogTortunabo, Log, TEXT("[SaveGame] Perfil cosmético '%s' migrado de v%d a v%d."),
				*Slot, Profile.SaveVersion, TNSaveLogic::COSMETIC_SAVE_VERSION);
			Profile.UnlockedHelmetIds = TNSaveLogic::SanitizeIds(Profile.UnlockedHelmetIds);
			Profile.UnlockedSkinIds = TNSaveLogic::SanitizeIds(Profile.UnlockedSkinIds);
			Profile.AccumulatedRaceScore = FMath::Max(0, Profile.AccumulatedRaceScore);
			Profile.StampCurrentVersion();
			return true;
		case TNSaveLogic::EMigration::FromNewerBuild:
			UE_LOG(LogTortunabo, Warning, TEXT("[SaveGame] Perfil cosmético '%s' guardado por una versión más nueva (v%d > v%d)."),
				*Slot, Profile.SaveVersion, TNSaveLogic::COSMETIC_SAVE_VERSION);
			return false;
		case TNSaveLogic::EMigration::UpToDate:
			return false;
		}
		return false;
	}
}

void UMP_GameInstance::LoadCosmeticProfile()
{
	// Un perfil por cuenta de Steam (#83); la primera que entra hereda el _Local de antes.
	CosmeticAccountId = TNCosmeticSlot::SteamAccountIdOf(MPGameInstance_GetPreferredOnlineSubsystem());
	TNCosmeticSlot::MigrateLocalToAccount(CosmeticSaveSlotPrefix, CosmeticAccountId);
	const FString SlotName = BuildCosmeticSaveSlot();
	const TNSaveGameIO::FLoadResult Loaded = TNSaveGameIO::LoadOrQuarantine(SlotName, 0, UTN_CosmeticSaveGame::StaticClass(),
		[](const USaveGame& Save) { return CastChecked<UTN_CosmeticSaveGame>(&Save)->IsIntact(); },
		TEXT("Perfil cosmético"));
	bCosmeticSaveBlocked = Loaded.bSaveBlocked;
	CosmeticProfile = Cast<UTN_CosmeticSaveGame>(Loaded.Loaded);

	bool bNeedsSave = false;
	if (CosmeticProfile)
	{
		bNeedsSave = TNMigrateCosmeticProfile(*CosmeticProfile, SlotName);
	}
	else
	{
		CosmeticProfile = Cast<UTN_CosmeticSaveGame>(UGameplayStatics::CreateSaveGameObject(UTN_CosmeticSaveGame::StaticClass()));
		if (CosmeticProfile)
		{
			CosmeticProfile->StampCurrentVersion();
		}
	}

	if (!CosmeticProfile)
	{
		return;
	}

	for (const FName DefaultHelmet : DefaultUnlockedHelmets)
	{
		if (DefaultHelmet != NAME_None)
		{
			CosmeticProfile->UnlockedHelmetIds.AddUnique(DefaultHelmet);
		}
	}

	if (CosmeticProfile->EquippedHelmetId == NAME_None && CosmeticProfile->UnlockedHelmetIds.Num() > 0)
	{
		CosmeticProfile->EquippedHelmetId = CosmeticProfile->UnlockedHelmetIds[0];
	}

	if (bNeedsSave)
	{
		SaveCosmeticProfile();
	}
}

void UMP_GameInstance::SaveCosmeticProfile() const
{
	if (!CosmeticProfile)
	{
		return;
	}
	if (bCosmeticSaveBlocked)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[SaveGame] Perfil cosmético sin guardar: el fichero dañado no se pudo apartar y no se pisa."));
		return;
	}

	CosmeticProfile->bWriteComplete = true;
	TNSaveGameIO::SaveChecked(CosmeticProfile, BuildCosmeticSaveSlot(), 0, TEXT("Perfil cosmético"));
}

FString UMP_GameInstance::BuildCosmeticSaveSlot() const
{
	// La cuenta se lee del subsistema en línea en Init (no del nick del jugador, que aún no existe y que con el
	// subsistema NULL lleva un GUID distinto cada sesión): misma ranura al cargar y al guardar.
	return TNCosmeticSlot::SlotFor(CosmeticSaveSlotPrefix, CosmeticAccountId);
}

// ── Race Score ────────────────────────────────────────────────────────────────

void UMP_GameInstance::AddRaceScore(int32 Points)
{
	if (Points <= 0 || !CosmeticProfile)
	{
		return;
	}
	CosmeticProfile->AccumulatedRaceScore += Points;
	SaveCosmeticProfile();
	UE_LOG(LogTortunabo, Log, TEXT("[GameInstance] AddRaceScore: +%d → total=%d"), Points, CosmeticProfile->AccumulatedRaceScore);
}

int32 UMP_GameInstance::GetAccumulatedRaceScore() const
{
	return CosmeticProfile ? CosmeticProfile->AccumulatedRaceScore : 0;
}

void UMP_GameInstance::AddTurtleDolls(int32 Count)
{
	if (Count <= 0 || !CosmeticProfile)
	{
		return;
	}
	CosmeticProfile->TurtleDollsCollected += Count;
	SaveCosmeticProfile();
	UE_LOG(LogTortunabo, Log, TEXT("[GameInstance] AddTurtleDolls: +%d → total=%d"), Count, CosmeticProfile->TurtleDollsCollected);
}

int32 UMP_GameInstance::GetTurtleDollsCollected() const
{
	return CosmeticProfile ? CosmeticProfile->TurtleDollsCollected : 0;
}

#if !UE_BUILD_SHIPPING
// Para probar la tienda (los buggies cuestan conchas): suma conchas al perfil local y las guarda.
static FAutoConsoleCommandWithWorldAndArgs GTNShopAddShellsCommand(
	TEXT("TN.Shop.AddShells"),
	TEXT("Tienda: TN.Shop.AddShells <conchas = 5000>: suma conchas al perfil cosmético local (para comprar buggies y pinturas)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UMP_GameInstance* GI = World ? Cast<UMP_GameInstance>(World->GetGameInstance()) : nullptr;
		if (!GI)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("TN.Shop.AddShells: no hay UMP_GameInstance"));
			return;
		}
		GI->AddRaceScore(Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 5000);
	}));
#endif

// ── Tutorial state ────────────────────────────────────────────────────────────

bool UMP_GameInstance::HasCompletedTutorial() const
{
	return TutorialProfile && TutorialProfile->bHasCompletedTutorial;
}

void UMP_GameInstance::SetTutorialCompleted()
{
	if (!TutorialProfile)
	{
		return;
	}
	TutorialProfile->bHasCompletedTutorial = true;
	TutorialProfile->TimesCompleted = FMath::Max(0, TutorialProfile->TimesCompleted) + 1;
	SaveTutorialProfile();
	UE_LOG(LogTortunabo, Log, TEXT("[GameInstance] Tutorial marcado como completado y guardado (%s)."), *GetTutorialSlotName());
}

void UMP_GameInstance::ResetTutorialProgress()
{
	if (!TutorialProfile)
	{
		TutorialProfile = Cast<UTN_TutorialSaveGame>(UGameplayStatics::CreateSaveGameObject(UTN_TutorialSaveGame::StaticClass()));
		if (TutorialProfile)
		{
			TutorialProfile->StampCurrentVersion();
		}
	}
	if (!TutorialProfile)
	{
		return;
	}
	TutorialProfile->bHasCompletedTutorial = false;
	SaveTutorialProfile();
	UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] Estado reiniciado (%s): la próxima vez que llegues a un lobby empiezas en el tutorial."), *GetTutorialSlotName());
}

FString UMP_GameInstance::GetTutorialSlotName() const
{
	// En el editor cada ventana de PIE es una «máquina»: su propia ranura (la primera ventana, la de siempre).
	const FWorldContext* Context = GetWorldContext();
	const int32 PIEInstance = (Context && Context->WorldType == EWorldType::PIE) ? Context->PIEInstance : INDEX_NONE;
	return PIEInstance > 0 ? FString::Printf(TEXT("TutorialState_0_PIE%d"), PIEInstance) : FString(TEXT("TutorialState_0"));
}

void UMP_GameInstance::LoadTutorialProfile()
{
	const FString Slot = GetTutorialSlotName();
	const TNSaveGameIO::FLoadResult Loaded = TNSaveGameIO::LoadOrQuarantine(Slot, 0, UTN_TutorialSaveGame::StaticClass(),
		[](const USaveGame& Save) { return CastChecked<UTN_TutorialSaveGame>(&Save)->IsIntact(); },
		TEXT("Tutorial"));
	bTutorialSaveBlocked = Loaded.bSaveBlocked;
	TutorialProfile = Cast<UTN_TutorialSaveGame>(Loaded.Loaded);

	if (TutorialProfile)
	{
		const TNSaveLogic::EMigration Migration =
			TNSaveLogic::DecideMigration(TutorialProfile->SaveVersion, TNSaveLogic::TUTORIAL_SAVE_VERSION);
		if (Migration == TNSaveLogic::EMigration::Upgrade)
		{
			// v0 → v1: sin cambios de datos salvo el contador, que nunca es negativo.
			UE_LOG(LogTortunabo, Log, TEXT("[SaveGame] Tutorial '%s' migrado de v%d a v%d."),
				*Slot, TutorialProfile->SaveVersion, TNSaveLogic::TUTORIAL_SAVE_VERSION);
			TutorialProfile->TimesCompleted = FMath::Max(0, TutorialProfile->TimesCompleted);
			TutorialProfile->StampCurrentVersion();
			SaveTutorialProfile();
		}
		else if (Migration == TNSaveLogic::EMigration::FromNewerBuild)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[SaveGame] Tutorial '%s' guardado por una versión más nueva (v%d)."),
				*Slot, TutorialProfile->SaveVersion);
		}
	}
	else
	{
		TutorialProfile = Cast<UTN_TutorialSaveGame>(UGameplayStatics::CreateSaveGameObject(UTN_TutorialSaveGame::StaticClass()));
		if (TutorialProfile)
		{
			TutorialProfile->StampCurrentVersion();
		}
	}

#if !UE_BUILD_SHIPPING
	// Solo para probar, nunca en Shipping (Docs/Tutorial.md): con Saved/ResetTutorial.txt, cada vez que arranca el juego (o cada PIE) el tutorial vuelve
	// a estar por hacer. Vacío = todas las ventanas; con números, solo esas (0 = la primera ventana o el juego suelto,
	// 1 = «Cliente 1», 2 = «Cliente 2»...). Se deja el archivo: para volver a lo normal, se borra.
	const FString ResetFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ResetTutorial.txt"));
	if (TutorialProfile && FPaths::FileExists(ResetFile))
	{
		FString Contents;
		FFileHelper::LoadFileToString(Contents, *ResetFile);
		TArray<FString> Tokens;
		Contents.ParseIntoArrayWS(Tokens, TEXT(",;"));
		const FWorldContext* Context = GetWorldContext();
		const int32 Window = (Context && Context->WorldType == EWorldType::PIE) ? FMath::Max(0, Context->PIEInstance) : 0;
		bool bAll = true;
		bool bMine = false;
		for (const FString& Token : Tokens)
		{
			if (Token.IsNumeric())
			{
				bAll = false;
				bMine |= FCString::Atoi(*Token) == Window;
			}
		}
		if (bAll || bMine)
		{
			TutorialProfile->bHasCompletedTutorial = false;
			SaveTutorialProfile();
			UE_LOG(LogTortunabo, Warning, TEXT("[Tutorial] %s existe: tutorial reiniciado para la ventana %d (%s). Bórralo para volver a lo normal."),
				*ResetFile, Window, *Slot);
		}
		else
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] %s no nombra la ventana %d: su tutorial no se toca."), *ResetFile, Window);
		}
	}
#endif
	UE_LOG(LogTortunabo, Log, TEXT("[Tutorial] Guardado %s: %s."), *Slot,
		TutorialProfile && TutorialProfile->bHasCompletedTutorial ? TEXT("tutorial hecho") : TEXT("tutorial por hacer"));
}

void UMP_GameInstance::SaveTutorialProfile() const
{
	if (!TutorialProfile)
	{
		return;
	}
	if (bTutorialSaveBlocked)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[SaveGame] Tutorial sin guardar: el fichero dañado no se pudo apartar y no se pisa."));
		return;
	}
	TutorialProfile->bWriteComplete = true;
	TNSaveGameIO::SaveChecked(TutorialProfile, GetTutorialSlotName(), 0, TEXT("Tutorial"));
}

void UMP_GameInstance::RefreshLoadingText(const FString& Reason) const
{
	if (!LoadingScreenWidget)
	{
		return;
	}

	if (UTextBlock* Status = Cast<UTextBlock>(LoadingScreenWidget->GetWidgetFromName(TEXT("StatusText"))))
	{
		Status->SetText(FText::FromString(Reason));
		return;
	}

	if (UTN_LoadingScreenWidget* TypedLoading = Cast<UTN_LoadingScreenWidget>(LoadingScreenWidget))
	{
		TypedLoading->SetStatusMessage(FText::FromString(Reason));
	}
}

void UMP_GameInstance::OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	FString FailureTypeStr;
	switch (FailureType)
	{
	case ENetworkFailure::NetDriverAlreadyExists:   FailureTypeStr = TEXT("NetDriverAlreadyExists"); break;
	case ENetworkFailure::NetDriverCreateFailure:   FailureTypeStr = TEXT("NetDriverCreateFailure"); break;
	case ENetworkFailure::NetDriverListenFailure:   FailureTypeStr = TEXT("NetDriverListenFailure"); break;
	case ENetworkFailure::ConnectionLost:           FailureTypeStr = TEXT("ConnectionLost"); break;
	case ENetworkFailure::ConnectionTimeout:        FailureTypeStr = TEXT("ConnectionTimeout"); break;
	case ENetworkFailure::FailureReceived:          FailureTypeStr = TEXT("FailureReceived"); break;
	case ENetworkFailure::OutdatedClient:           FailureTypeStr = TEXT("OutdatedClient"); break;
	case ENetworkFailure::OutdatedServer:           FailureTypeStr = TEXT("OutdatedServer"); break;
	case ENetworkFailure::PendingConnectionFailure: FailureTypeStr = TEXT("PendingConnectionFailure"); break;
	case ENetworkFailure::NetChecksumMismatch:      FailureTypeStr = TEXT("NetChecksumMismatch"); break;
	default:                                        FailureTypeStr = TEXT("Unknown"); break;
	}

	// OnNetworkFailure es de todo el motor: en PIE con varias ventanas el fallo de un cliente no es asunto del host.
	if (GEngine)
	{
		const FWorldContext* FailedContext = World ? GEngine->GetWorldContextFromWorld(World)
			: GEngine->GetWorldContextFromPendingNetGameNetDriver(NetDriver);
		if (FailedContext && FailedContext->OwningGameInstance && FailedContext->OwningGameInstance != this)
		{
			return;
		}
	}

	UE_LOG(LogTortunabo, Warning, TEXT("[MP] NETWORK ERROR: %s - %s"), *FailureTypeStr, *ErrorString);

	// ---------- SERVIDOR: se cae la conexión de UN invitado (timeout, red caída) ----------
	// El motor cierra esa conexión y solo sale ese invitado; el anfitrión y los demás siguen en la partida (#657).
	// Sin driver no se sabe de qué conexión es: se trata como hasta ahora.
	const ENetMode FailedMode = NetDriver ? NetDriver->GetNetMode() : NM_Standalone;
	if (TNNetFailure::IsGuestFailureOnHost(FailureType, FailedMode))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[MP] %s en la conexión de un invitado: solo sale ese invitado, la partida sigue."), *FailureTypeStr);
		return;
	}

	// ---------- CLIENTE: la sala no nos deja entrar (cerrada, llena o expulsado: HandleGameModePreLogin) ----------
	if (ErrorString.StartsWith(TNRoomKeys::RefusePrefix()))
	{
		HandleRoomRefused(ErrorString);
		return;
	}

	// ---------- SERVIDOR: errores de socket/driver ----------
	// Pueden ocurrir en dos contextos:
	// 1. Inicio de conexión (sesión zombi de Steam) → destruir sesión para permitir reintentar.
	// 2. Durante un ServerTravel lobby→game → el socket Steam tarda en liberarse (transitorio).
	if (FailureType == ENetworkFailure::NetDriverListenFailure ||
		FailureType == ENetworkFailure::NetDriverCreateFailure ||
		FailureType == ENetworkFailure::NetDriverAlreadyExists)
	{
		HandleDriverFailure(FailureTypeStr, ErrorString);
		return;
	}

	// ---------- CLIENTE: checksum mismatch (versiones incompatibles) ----------
	// Ocurre cuando el cliente compiló con Live Coding o tiene un build distinto al servidor.
	// El engine ya desconecta solo; aquí solo limpiamos la sesión y mostramos mensaje claro.
	if (FailureType == ENetworkFailure::NetChecksumMismatch)
	{
		HandleChecksumMismatch(ErrorString);
		return;
	}

	// ---------- CLIENTE: otras desconexiones (pérdida de conexión, timeout, etc.) ----------
	if (FailureType == ENetworkFailure::ConnectionLost   ||
		FailureType == ENetworkFailure::ConnectionTimeout ||
		FailureType == ENetworkFailure::FailureReceived   ||
		FailureType == ENetworkFailure::PendingConnectionFailure)
	{
		HandleConnectionLost(FailureTypeStr);
		return;
	}

	// Resto de errores (OutdatedClient, OutdatedServer, etc.) — solo loguear.
	UpdateStatus(FString::Printf(TEXT("NETWORK ERROR: %s - %s"), *FailureTypeStr, *ErrorString));
}

void UMP_GameInstance::HandleDriverFailure(const FString& FailureTypeStr, const FString& ErrorString)
{
	if (bIsPendingTravel)
	{
		// Error durante transición de nivel (ServerTravel lobby→game).
		// El socket Steam aún no se ha liberado. Marcar para reintento
		// en HandlePostLoadMap, donde el mundo nuevo ya existe.
		bNeedsListenRetry = true;
		UE_LOG(LogTortunabo, Warning,
			TEXT("[MP] %s durante transición de nivel — se reintentará el listen server "
			     "cuando el mapa nuevo termine de cargar."),
			*FailureTypeStr);
		// NO hacer bIsPendingTravel = false aquí: PostLoadMap lo reseteará.
		// NO ocultar loading screen: PostLoadMap la ocultará.
		return;
	}
	else
	{
		// Error al iniciar el listen server desde cero (sesión Steam zombi).
		HideLoadingScreen();
		UpdateStatus(FString::Printf(TEXT("NETWORK ERROR: %s - %s"), *FailureTypeStr, *ErrorString));
		DestroyCurrentSession();
		UE_LOG(LogTortunabo, Warning,
			TEXT("[MP] %s en inicio de conexión — sesión Steam destruida. "
			     "Reinicia Steam si el error persiste."),
			*FailureTypeStr);
	}
	return;
}

void UMP_GameInstance::HandleChecksumMismatch(const FString& ErrorString)
{
	HideLoadingScreen();
	// Una sola línea: el menú enseña lo que va tras el último salto de línea del estado (#280); la pista de Live Coding va al registro.
	UpdateStatus(NSLOCTEXT("TNRooms", "BuildMismatchStatus", "Versiones incompatibles con el servidor.").ToString());
	// Destruir la sesión huérfana del lado cliente para poder reintentar.
	DestroyCurrentSession();
	// El motor vuelve solo al menú (?closed): que diga por qué y no parezca un fallo de la sala (#245).
	PendingMenuNotice.Text = NSLOCTEXT("TNRooms", "BuildMismatch",
		"Tu versión del juego no es la misma que la del anfitrión. Poneos los dos en la misma versión y volved a intentarlo.");
	PendingMenuNotice.bError = true;
	PendingMenuNotice.bOpenJoin = true;
	UE_LOG(LogTortunabo, Error,
		TEXT("[MP] NetChecksumMismatch — El cliente tiene un build distinto al servidor. "
		     "Recompila sin Live Coding y asegúrate de que todos usan el mismo binario. "
		     "Detalle: %s"),
		*ErrorString);
}

void UMP_GameInstance::HandleConnectionLost(const FString& FailureTypeStr)
{
	// Expulsado: ya va camino del menú con su aviso (HandleKickedFromRoom); nada de reconectar.
	if (bKickedFromRoom)
	{
		bPendingAutoRejoin = false;
		UE_LOG(LogTortunabo, Log, TEXT("[MP] %s tras la expulsión: sin reconexión."), *FailureTypeStr);
		return;
	}

	// Solo intentar auto-rejoin si el cliente sabía que el servidor iba a viajar.
	// bIsPendingTravel=true significa que ClientNotifyServerTravel llegó antes
	// de la desconexión (o que PreLoadMap empezó el travel).
	// Si bIsPendingTravel=false, el host se fue de verdad (crasheó, salió del juego)
	// y NO debemos quedarnos en "Reconectando" indefinidamente.
	if (bIsPendingTravel)
	{
		bPendingAutoRejoin = true;
		AutoRejoinRetryCount = MaxAutoRejoinRetries;
		ShowLoadingScreen(TEXT("Reconectando a la partida..."));
		UpdateStatus(FString::Printf(TEXT("Conexión perdida durante travel (%s). Reconectando..."), *FailureTypeStr));
		UE_LOG(LogTortunabo, Warning,
			TEXT("[MP] %s durante travel — NO destruyendo sesión. Intentando auto-rejoin en 3s (%d reintentos)."),
			*FailureTypeStr, AutoRejoinRetryCount);

		// Esperar a que el host arranque su listen server en el nuevo mapa
		if (UWorld* CurrentWorld = GetWorld())
		{
			CurrentWorld->GetTimerManager().SetTimer(
				AutoRejoinTimerHandle, FTimerDelegate::CreateUObject(this, &UMP_GameInstance::AttemptAutoRejoin),
				3.0f, false);
		}
		else
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[MP] No World for timer — AttemptAutoRejoin will fire from HandlePostLoadMap."));
		}
		return;
	}

	// El host se desconectó sin que fuera un travel → ir al menú directamente.
	// No intentar auto-rejoin: el host se fue, la sesión ya no tiene servidor.
	HideLoadingScreen();
	DestroyCurrentSession();
	UpdateStatus(FString::Printf(TEXT("El host abandonó la partida (%s)."), *FailureTypeStr));
	// Lo verá el menú principal al llegar: desde el menú (entrando en una sala) o desde una partida.
	const bool bWasJoining = IsMenuWorld(GetWorld());
	PendingMenuNotice.Text = bWasJoining
		? NSLOCTEXT("TNRooms", "ConnectFailed", "No se ha podido conectar con la sala: puede que el anfitrión se haya ido.")
		: NSLOCTEXT("TNRooms", "HostLeft", "Se ha acabado la partida: el anfitrión se ha ido o se ha perdido la conexión.");
	PendingMenuNotice.bError = true;
	PendingMenuNotice.bOpenJoin = bWasJoining;
	UE_LOG(LogTortunabo, Warning,
		TEXT("[MP] %s sin travel pendiente — el host se fue. Destruyendo sesión y volviendo al menú."),
		*FailureTypeStr);
	if (APlayerController* PC = GetFirstLocalPlayerController())
	{
		PC->ClientTravel(MenuMapPath, TRAVEL_Absolute);
	}
	return;
}

// ── Salas públicas y privadas (Docs/Salas.md) ─────────────────────────────────

void UMP_GameInstance::HostRoom(const FTNRoomConfig& Config)
{
	// Antes de tocar la sala activa: un segundo «Crear» con otro nombre y otro código no puede cambiar la que ya se está creando.
	if (RoomOp.IsBusy())
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Salas] Crear sala ignorado: ya hay una sala creándose o una entrada en marcha."));
		return;
	}
	const TArray<int32> Sizes = GetRoomSizeOptions();
	ActiveRoom = Config;
	ActiveRoom.Mode = TNLobbyMission::NormalizeMenuMode(Config.Mode);
	ActiveRoom.MaxPlayers = FMath::Clamp(Config.MaxPlayers, 2, Sizes.Num() > 0 ? Sizes.Last() : TNRoomLimits::Max);
	if (Config.NameId < 0 || Config.NameId >= TNRoomNames::Num())
	{
		ActiveRoom.NameId = TNRoomNames::Random();
	}
	ActiveRoom.Code = TNRoomCode::Normalize(Config.Code);
	if (!TNRoomCode::IsComplete(ActiveRoom.Code))
	{
		ActiveRoom.Code = TNRoomCode::Generate();
	}
	ActiveRoom.bLocked = false;
	bHasActiveRoom = true;

	// Sala nueva: nadie expulsado ni miembro todavía, y el anuncio, desde cero.
	RoomMemberIds.Reset();
	KickedRoomIds.Reset();
	AdvertisedPlayers = INDEX_NONE;
	AdvertisedMode = INDEX_NONE;
	AdvertisedLocked = INDEX_NONE;
	bKickedFromRoom = false;
	SelectedProcMode = ActiveRoom.Mode;
	SelectedRallyVariant = ActiveRoom.RallyVariant;
	SelectedTctArena = ActiveRoom.TctArena;
	SelectedKartSeats = FMath::Clamp(ActiveRoom.RallySeats, 1, 2);

	UE_LOG(LogTortunabo, Log, TEXT("[Salas] Crear sala «%s» (%s, %s, %d plazas, código %s)."), *TNRoomNames::GetIn(ActiveRoom.NameId, true),
		*UEnum::GetValueAsString(ActiveRoom.Mode), ActiveRoom.bPrivate ? TEXT("privada") : TEXT("pública"), ActiveRoom.MaxPlayers, *ActiveRoom.Code);
	HostSession();
}

FTNRoomConfig UMP_GameInstance::MakeRoomDraft() const
{
	const TArray<int32> Sizes = GetRoomSizeOptions();
	FTNRoomConfig Draft;
	if (bHasRoomDraft)
	{
		Draft = RoomDraft;
	}
	else
	{
		Draft.Mode = TNLobbyMission::NormalizeMenuMode(SelectedProcMode);
		Draft.MaxPlayers = Sizes.Num() > 0 ? Sizes.Last() : TNRoomLimits::Max;
		Draft.RallyVariant = SelectedRallyVariant;
		Draft.TctArena = SelectedTctArena;
		Draft.RallySeats = FMath::Clamp(SelectedKartSeats, 1, 2);
	}
	if (!Sizes.Contains(Draft.MaxPlayers) && Sizes.Num() > 0)
	{
		Draft.MaxPlayers = Sizes.Last();
	}
	Draft.RallyVariant = TNLobbyMission::ResolveRallyMap(Draft.RallyVariant, TNLobbyMission::RallyMapOptions());
	Draft.TctArena = TNLobbyMission::ResolveTctArena(Draft.TctArena, TNLobbyMission::TctArenaOptions());
	// Nombre y código nuevos cada vez que se abre la pantalla (el nombre, distinto del de la última vez).
	Draft.NameId = TNRoomNames::Random(bHasRoomDraft ? RoomDraft.NameId : INDEX_NONE);
	Draft.Code = TNRoomCode::Generate();
	Draft.bLocked = false;
	return Draft;
}

void UMP_GameInstance::RememberRoomDraft(const FTNRoomConfig& Draft)
{
	RoomDraft = Draft;
	bHasRoomDraft = true;
}

TArray<int32> UMP_GameInstance::GetRoomSizeOptions() const
{
	TArray<int32> Sizes;
	for (const int32 Size : TNRoomLimits::Options)
	{
		if (Size <= MaxPlayers)
		{
			Sizes.Add(Size);
		}
	}
	if (Sizes.Num() == 0)
	{
		Sizes.Add(FMath::Clamp(MaxPlayers, 2, TNRoomLimits::Max));
	}
	return Sizes;
}

void UMP_GameInstance::RefreshRoomList()
{
	StartRoomSearch(ETNRoomSearch::List);
}

bool UMP_GameInstance::IsSearchingRooms() const
{
	return RoomSearchPurpose != ETNRoomSearch::None;
}

void UMP_GameInstance::JoinListedRoom(int32 ListingIndex)
{
	// Antes de nada: los avisos de abajo quitan la pantalla de carga, que en ese caso es la de la operación en marcha.
	if (RoomOp.IsBusy())
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Salas] Unirse ignorado: ya hay una sala creándose o una entrada en marcha."));
		return;
	}
	if (!RoomListings.IsValidIndex(ListingIndex) || !RoomListSearch.IsValid()
		|| !RoomListSearch->SearchResults.IsValidIndex(RoomListings[ListingIndex].SearchIndex))
	{
		HideLoadingScreen();
		PostRoomNotice(NSLOCTEXT("TNRooms", "ListStale", "Esa sala ya no está en la lista: actualízala."), true);
		return;
	}
	const FTNRoomListing& Listing = RoomListings[ListingIndex];
	const FText RoomName = TNRoomNames::Get(Listing.NameId);
	if (Listing.bLocked)
	{
		HideLoadingScreen();
		PostRoomNotice(FText::Format(NSLOCTEXT("TNRooms", "ListLocked", "«{0}» está cerrada: el anfitrión no deja entrar a nadie más."), RoomName), true);
		return;
	}
	if (Listing.IsFull())
	{
		HideLoadingScreen();
		PostRoomNotice(FText::Format(NSLOCTEXT("TNRooms", "ListFull", "«{0}» está llena ({1}/{2})."), RoomName, FText::AsNumber(Listing.Players),
			FText::AsNumber(Listing.MaxPlayers)), true);
		return;
	}
	JoinRoomResult(RoomListSearch->SearchResults[Listing.SearchIndex], RoomName);
}

void UMP_GameInstance::JoinRoomByCode(const FString& Code)
{
	// Sin el aviso «Buscando la sala X...»: nadie iba a contestarlo (la búsqueda no sale mientras haya otra operación).
	if (RoomOp.IsBusy())
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Salas] Entrar con código ignorado: ya hay una sala creándose o una entrada en marcha."));
		return;
	}
	const FString Clean = TNRoomCode::Normalize(Code);
	if (!TNRoomCode::IsComplete(Clean))
	{
		PostRoomNotice(NSLOCTEXT("TNRooms", "CodeIncomplete", "El código tiene 5 letras y números (nunca lleva O, 0, I, 1 ni L)."), true);
		return;
	}
	PostRoomNotice(FText::Format(NSLOCTEXT("TNRooms", "CodeSearching", "Buscando la sala {0}..."), FText::FromString(Clean)), false);
	StartRoomSearch(ETNRoomSearch::Code, Clean);
}

void UMP_GameInstance::StartRoomSearch(ETNRoomSearch Purpose, const FString& Code)
{
	if (Purpose == ETNRoomSearch::None)
	{
		return;
	}
	// Con una sala creándose o una entrada en marcha no se busca más (tampoco la que esperaba turno): ya hay a dónde ir.
	if (RoomOp.IsBusy())
	{
		QueuedRoomSearch = ETNRoomSearch::None;
		QueuedRoomCode.Reset();
		return;
	}
	// Una sola búsqueda a la vez (el NULL ignora la segunda sin avisar): la nueva espera su turno. Solo hay un sitio en la cola y
	// lo ocupa la más importante: la lista, que se repite sola, no pisa un código que espera respuesta ni un «unirse a la primera».
	if (RoomSearchPurpose != ETNRoomSearch::None)
	{
		if (RoomSearchPurpose != Purpose || RoomSearchCode != Code)
		{
			if (TNRoomSearchRules::CanTakeQueue(QueuedRoomSearch, Purpose))
			{
				QueuedRoomSearch = Purpose;
				QueuedRoomCode = Code;
			}
			else
			{
				UE_LOG(LogTortunabo, Log, TEXT("[Salas] Búsqueda de salas (%d) descartada: espera otra más importante (%d)."), static_cast<int32>(Purpose),
					static_cast<int32>(QueuedRoomSearch));
			}
		}
		return;
	}

	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		UpdateStatus(TEXT("ERROR: No session interface"));
		if (Purpose == ETNRoomSearch::QuickJoin)
		{
			HideLoadingScreen();
		}
		PostRoomNotice(NSLOCTEXT("TNRooms", "NoOnline", "No hay conexión con Steam: ábrelo y vuelve a intentarlo."), true);
		if (Purpose != ETNRoomSearch::Code)
		{
			bRoomListReady = true;
			OnRoomListChanged.Broadcast();
		}
		return;
	}

	RoomSearchPurpose = Purpose;
	RoomSearchCode = Code;
	RoomSearchStartTime = FPlatformTime::Seconds();
	const int32 Serial = ++RoomSearchSerial;

	Sessions->ClearOnFindSessionsCompleteDelegates(this);
	Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UMP_GameInstance::OnFindSessionsComplete));

	SessionSearch = MakeShared<FOnlineSessionSearch>();
	SessionSearch->bIsLanQuery = false;
	SessionSearch->MaxSearchResults = Purpose == ETNRoomSearch::Code ? 50 : 200;
	SessionSearch->QuerySettings.Set(TNRoomKeys::PresenceSearch(), true, EOnlineComparisonOp::Equals);
	// Filtros en el servidor de Steam (el NULL los ignora y se filtra al leer los resultados, en ReadRoomListing y
	// OnFindSessionsComplete): solo salas de Tortunavy (el AppId 480 lo comparten muchos proyectos), y además la del código
	// o solo las públicas.
	SessionSearch->QuerySettings.Set(TNRoomKeys::Keywords(), FString(TNRoomKeys::KeywordsValue()), EOnlineComparisonOp::Equals);
	if (Purpose == ETNRoomSearch::Code)
	{
		SessionSearch->QuerySettings.Set(TNRoomKeys::Code(), Code, EOnlineComparisonOp::Equals);
	}
	else
	{
		SessionSearch->QuerySettings.Set(TNRoomKeys::Private(), 0, EOnlineComparisonOp::Equals);
	}

	if (Purpose != ETNRoomSearch::Code)
	{
		OnRoomListChanged.Broadcast();
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Salas] %s"), Purpose == ETNRoomSearch::Code ? *FString::Printf(TEXT("Buscando la sala %s..."), *Code) : TEXT("Buscando salas públicas..."));
	if (!Sessions->FindSessions(0, SessionSearch.ToSharedRef()) && RoomSearchSerial == Serial && RoomSearchPurpose == Purpose)
	{
		// No ha arrancado y no ha avisado: se da por fallida ya.
		OnFindSessionsComplete(false);
	}
}

bool UMP_GameInstance::ReadRoomListing(const FOnlineSession& Session, int32 Index, FTNRoomListing& Out)
{
	const FOnlineSessionSettings& Settings = Session.SessionSettings;
	FString Keywords;
	if (!Settings.Get(TNRoomKeys::Keywords(), Keywords) || Keywords != TNRoomKeys::KeywordsValue())
	{
		return false;
	}
	Out = FTNRoomListing();
	Out.SearchIndex = Index;
	int32 Value = 0;
	Out.NameId = Settings.Get(TNRoomKeys::NameId(), Value) ? Value : INDEX_NONE;
	FString Code;
	Settings.Get(TNRoomKeys::Code(), Code);
	Out.Code = TNRoomCode::Normalize(Code);
	Value = 0;
	Out.bPrivate = Settings.Get(TNRoomKeys::Private(), Value) && Value != 0;
	Value = 0;
	Out.bLocked = Settings.Get(TNRoomKeys::Locked(), Value) && Value != 0;
	Value = 0;
	const bool bHasMode = Settings.Get(TNRoomKeys::Mode(), Value);
	int32 Schema = 1;
	Settings.Get(TNRoomKeys::ModeSchema(), Schema);
	Out.Mode = TNRoomKeys::DecodeMode(bHasMode, Value, Schema);
	Out.MaxPlayers = Settings.NumPublicConnections;
	Value = 0;
	Out.Players = Settings.Get(TNRoomKeys::Players(), Value) ? Value
		: FMath::Max(0, Settings.NumPublicConnections - Session.NumOpenPublicConnections);
	Out.HostName = Session.OwningUserName;
	return true;
}

void UMP_GameInstance::JoinRoomResult(const FOnlineSessionSearchResult& Result, const FText& RoomName)
{
	// Un segundo «Unirse» (o «Unirse» con una sala creándose) no empieza otra entrada: destruiría la sesión que ya está en marcha
	// o en espera de viajar. La primera sigue y la pantalla de carga ya es la suya.
	if (RoomOp.IsBusy())
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Salas] Entrar en «%s» ignorado: ya hay una sala creándose o una entrada en marcha."), *RoomName.ToString());
		return;
	}
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		HideLoadingScreen();
		PostRoomNotice(NSLOCTEXT("TNRooms", "NoOnline", "No hay conexión con Steam: ábrelo y vuelve a intentarlo."), true);
		return;
	}
	bKickedFromRoom = false;
	PendingJoinRoomName = RoomName;
	ShowLoadingScreen(FText::Format(NSLOCTEXT("TNRooms", "Connecting", "Entrando en «{0}»..."), RoomName).ToString());
	UpdateStatus(FString::Printf(TEXT("Entrando en la sala «%s»..."), *RoomName.ToString()));

	// Si queda una sesión de antes, se cierra primero y se entra al acabar (OnDestroySessionComplete).
	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		RoomOp.bHostAfterDestroy = false;
		RoomOp.bJoinAfterDestroy = true;
		RoomOpStartTime = FPlatformTime::Seconds();
		PendingInviteResult = Result;
		Sessions->ClearOnDestroySessionCompleteDelegates(this);
		Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UMP_GameInstance::OnDestroySessionComplete));
		Sessions->DestroySession(NAME_GameSession);
		return;
	}

	BeginSessionJoin(Result, 0);
}

bool UMP_GameInstance::GetRoomSnapshot(FTNRoomSnapshot& Out) const
{
	Out = FTNRoomSnapshot();
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Standalone)
	{
		return false;
	}
	Out.Players = CountRoomPlayers(World);

	// Anfitrión: su sala.
	if (World->GetNetMode() != NM_Client)
	{
		if (!bHasActiveRoom)
		{
			return false;
		}
		const APlayerController* HostPC = GetFirstLocalPlayerController(World);
		Out.bValid = true;
		Out.bIsHost = true;
		Out.NameId = ActiveRoom.NameId;
		Out.Code = ActiveRoom.Code;
		Out.HostName = HostPC && HostPC->PlayerState ? HostPC->PlayerState->GetPlayerName() : FString();
		Out.bPrivate = ActiveRoom.bPrivate;
		Out.bLocked = ActiveRoom.bLocked;
		Out.MaxPlayers = ActiveRoom.MaxPlayers;
		return true;
	}

	// Invitado: lo que replica el anfitrión.
	if (const ATN_RoomInfo* Info = ATN_RoomInfo::Find(World))
	{
		if (Info->GetMaxPlayers() > 0)
		{
			Out.bValid = true;
			Out.NameId = Info->GetRoomNameId();
			Out.Code = Info->GetRoomCode();
			Out.HostName = Info->GetHostName();
			Out.bPrivate = Info->IsPrivate();
			Out.bLocked = Info->IsLocked();
			Out.MaxPlayers = Info->GetMaxPlayers();
			return true;
		}
	}

	// Mientras llega (recién entrado o tras un cambio de mapa), el anuncio de la sesión en la que se entró.
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FNamedOnlineSession* Named = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	FTNRoomListing Listing;
	if (Named && ReadRoomListing(*Named, INDEX_NONE, Listing))
	{
		Out.bValid = true;
		Out.NameId = Listing.NameId;
		Out.Code = Listing.Code;
		Out.HostName = Named->OwningUserName;
		Out.bPrivate = Listing.bPrivate;
		Out.bLocked = Listing.bLocked;
		Out.MaxPlayers = FMath::Max(Listing.MaxPlayers, Out.Players);
	}
	return Out.bValid;
}

bool UMP_GameInstance::CanManageRoom() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_ListenServer;
}

void UMP_GameInstance::SetRoomLocked(bool bLocked)
{
	UWorld* World = GetWorld();
	if (!CanManageRoom())
	{
		return;
	}
	EnsureActiveRoom();
	if (ActiveRoom.bLocked == bLocked)
	{
		return;
	}
	ActiveRoom.bLocked = bLocked;
	// Los que están dentro ahora son de la sala: aunque esté cerrada, pueden volver si se les cae la conexión.
	if (const AGameStateBase* State = World ? World->GetGameState() : nullptr)
	{
		for (const APlayerState* PS : State->PlayerArray)
		{
			if (PS && PS->GetUniqueId().IsValid())
			{
				RoomMemberIds.Add(PS->GetUniqueId().ToString());
			}
		}
	}
	EnsureRoomInfo(World);
	UpdateRoomAdvertisement();
	UE_LOG(LogTortunabo, Log, TEXT("[Salas] Sala %s."), bLocked ? TEXT("cerrada") : TEXT("abierta"));
	PostRoomNotice(bLocked ? NSLOCTEXT("TNRooms", "Locked", "Sala cerrada: no entra nadie más (los que ya estaban pueden volver).")
		: NSLOCTEXT("TNRooms", "Unlocked", "Sala abierta: puede entrar gente otra vez."), false);
}

bool UMP_GameInstance::KickFromRoom(APlayerState* Target)
{
	UWorld* World = GetWorld();
	if (!Target || !CanManageRoom() || Target->GetWorld() != World)
	{
		return false;
	}
	APlayerController* TargetPC = Cast<APlayerController>(Target->GetOwner());
	if (!TargetPC || TargetPC->IsLocalController())
	{
		return false;
	}
	const FString TargetName = Target->GetPlayerName();
	const FUniqueNetIdRepl& TargetId = Target->GetUniqueId();
	if (TargetId.IsValid())
	{
		KickedRoomIds.Add(TargetId.ToString());
		RoomMemberIds.Remove(TargetId.ToString());
	}
	// Su cliente lo ve (ATN_RoomInfo) y se va solo al menú con el aviso.
	if (ATN_RoomInfo* Info = EnsureRoomInfo(World))
	{
		Info->ServerMarkKicked(Target->GetPlayerId());
	}
	// Si no se ha ido en un momento (réplica perdida, cliente colgado), fuera igualmente.
	TWeakObjectPtr<APlayerController> WeakTarget(TargetPC);
	FTimerHandle KickHandle;
	GetTimerManager().SetTimer(KickHandle, FTimerDelegate::CreateWeakLambda(this, [WeakTarget, TargetName]()
	{
		APlayerController* Stayed = WeakTarget.Get();
		UWorld* StayedWorld = Stayed ? Stayed->GetWorld() : nullptr;
		AGameModeBase* GameMode = StayedWorld ? StayedWorld->GetAuthGameMode() : nullptr;
		if (GameMode && GameMode->GameSession)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[Salas] %s no se ha ido solo: lo saca el servidor."), *TargetName);
			GameMode->GameSession->KickPlayer(Stayed, NSLOCTEXT("TNRooms", "KickReason", "El anfitrión te ha expulsado de la sala."));
		}
	}), MPGameInstance_KickGraceSeconds, false);

	UE_LOG(LogTortunabo, Log, TEXT("[Salas] Expulsado %s (id %s)."), *TargetName, TargetId.IsValid() ? *TargetId.ToString() : TEXT("sin id"));
	UpdateStatus(FString::Printf(TEXT("%s ha sido expulsado de la sala."), *TargetName));
	return true;
}

void UMP_GameInstance::HandleKickedFromRoom(int32 RoomNameId)
{
	if (bKickedFromRoom)
	{
		return;
	}
	bKickedFromRoom = true;
	bPendingAutoRejoin = false;
	PendingMenuNotice.Text = FText::Format(NSLOCTEXT("TNRooms", "KickedNotice", "El anfitrión te ha expulsado de la sala «{0}»."), TNRoomNames::Get(RoomNameId));
	PendingMenuNotice.bError = true;
	PendingMenuNotice.bOpenJoin = false;
	UpdateStatus(PendingMenuNotice.Text.ToString());
	HandleReturnToMenu();
}

bool UMP_GameInstance::CanInviteFriends() const
{
	IOnlineSubsystem* OSS = MPGameInstance_GetPreferredOnlineSubsystem();
	if (!OSS || OSS->GetSubsystemName() == FName(TEXT("NULL")))
	{
		return false;
	}
	const IOnlineSessionPtr Sessions = OSS->GetSessionInterface();
	return Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession) && OSS->GetExternalUIInterface().IsValid();
}

FTNMenuNotice UMP_GameInstance::ConsumeMenuNotice()
{
	FTNMenuNotice Notice = PendingMenuNotice;
	PendingMenuNotice = FTNMenuNotice();
	return Notice;
}

void UMP_GameInstance::AbortRoomOperation()
{
	const bool bWasWaitingOnline = RoomOp.IsWaitingOnline();
	const bool bWasHosting = RoomOp.bHostAfterDestroy || RoomOp.bCreating;
	UE_LOG(LogTortunabo, Warning, TEXT("[Salas] La operación de sesión no contesta (%s): se da por fallida."),
		bWasWaitingOnline ? (bWasHosting ? TEXT("crear") : TEXT("entrar")) : TEXT("viaje"));
	RoomOp = FTNRoomOpState();
	if (!bWasWaitingOnline)
	{
		// Solo faltaba el viaje: el motor tiene sus propios plazos de conexión. Solo se deja de esperar.
		return;
	}

	// Una respuesta tardía de Steam no debe llegar a una operación que ya no existe.
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegates(this);
		Sessions->ClearOnJoinSessionCompleteDelegates(this);
	}
	HideLoadingScreen();
	DestroyCurrentSession();
	PostRoomNotice(bWasHosting ? NSLOCTEXT("TNRooms", "CreateFailed", "No se ha podido crear la sala. ¿Está Steam abierto y conectado?")
		: TNRoomText::RefusedMessage(FString()), true);
}

void UMP_GameInstance::RoomTick()
{
	// Cerrar, crear o entrar que no contesta (o un viaje que no llega): se deja de esperar para no bloquear el menú para siempre.
	if (RoomOp.IsBusy() && TNRoomOpRules::HasTimedOut(RoomOp, FPlatformTime::Seconds() - RoomOpStartTime))
	{
		AbortRoomOperation();
	}

	// Búsqueda que no contesta (Steam sin conexión, o el NULL con otra en marcha): se da por fallida.
	if (RoomSearchPurpose != ETNRoomSearch::None && FPlatformTime::Seconds() - RoomSearchStartTime > MPGameInstance_RoomSearchTimeout)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Salas] La búsqueda de salas no contesta: se da por fallida."));
		IOnlineSessionPtr Sessions = GetSessionInterface();
		if (Sessions.IsValid())
		{
			Sessions->ClearOnFindSessionsCompleteDelegates(this);
			Sessions->CancelFindSessions();
		}
		OnFindSessionsComplete(false);
	}

	// Anfitrión de una partida en red: la sala replicada y su anuncio, al día.
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() != NM_ListenServer || World->bIsTearingDown)
	{
		return;
	}
	EnsureActiveRoom();
	ActiveRoom.Mode = SelectedProcMode;
	EnsureRoomInfo(World);
	const int32 Players = CountRoomPlayers(World);
	if (Players != AdvertisedPlayers || static_cast<int32>(SelectedProcMode) != AdvertisedMode || (ActiveRoom.bLocked ? 1 : 0) != AdvertisedLocked)
	{
		UpdateRoomAdvertisement();
	}
}

void UMP_GameInstance::HandleGameModePreLogin(AGameModeBase* GameMode, const FUniqueNetIdRepl& NewPlayer, FString& ErrorMessage)
{
	// Los eventos son de todo el proceso (en el editor hay una GameInstance por ventana): solo el servidor de esta.
	if (!GameMode || GameMode->GetGameInstance() != this || !ErrorMessage.IsEmpty())
	{
		return;
	}
	EnsureActiveRoom();
	const FString Id = NewPlayer.IsValid() ? NewPlayer.ToString() : FString();
	const bool bMember = !Id.IsEmpty() && RoomMemberIds.Contains(Id);
	const int32 Inside = GameMode->GetNumPlayers() + GameMode->GetNumSpectators();
	if (!Id.IsEmpty() && KickedRoomIds.Contains(Id))
	{
		ErrorMessage = TNRoomKeys::RefuseKicked();
	}
	else if (ActiveRoom.bLocked && !bMember)
	{
		ErrorMessage = TNRoomKeys::RefuseLocked();
	}
	// Los miembros que vuelven (conexión caída) no cuentan contra el tope: su tortuga vieja puede seguir contando un rato.
	else if (!bMember && Inside >= ActiveRoom.MaxPlayers)
	{
		ErrorMessage = TNRoomKeys::RefuseFull();
	}
	if (!ErrorMessage.IsEmpty())
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Salas] Rechazado al entrar (%s): %s (dentro %d/%d)."), Id.IsEmpty() ? TEXT("sin id") : *Id, *ErrorMessage,
			Inside, ActiveRoom.MaxPlayers);
	}
}

void UMP_GameInstance::HandleGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	if (!GameMode || GameMode->GetGameInstance() != this || !NewPlayer || !NewPlayer->PlayerState)
	{
		return;
	}
	const FUniqueNetIdRepl& Id = NewPlayer->PlayerState->GetUniqueId();
	if (Id.IsValid())
	{
		RoomMemberIds.Add(Id.ToString());
	}
	UWorld* World = GameMode->GetWorld();
	if (World && World->GetNetMode() == NM_ListenServer)
	{
		EnsureActiveRoom();
		EnsureRoomInfo(World);
	}
}

void UMP_GameInstance::EnsureActiveRoom()
{
	if (bHasActiveRoom)
	{
		return;
	}
	const TArray<int32> Sizes = GetRoomSizeOptions();
	ActiveRoom = FTNRoomConfig();
	ActiveRoom.Mode = SelectedProcMode;
	ActiveRoom.MaxPlayers = Sizes.Num() > 0 ? Sizes.Last() : TNRoomLimits::Max;
	ActiveRoom.NameId = TNRoomNames::Random();
	ActiveRoom.Code = TNRoomCode::Generate();
	bHasActiveRoom = true;
	UE_LOG(LogTortunabo, Log, TEXT("[Salas] Sala por defecto: «%s», pública, %d plazas, código %s."), *TNRoomNames::GetIn(ActiveRoom.NameId, true),
		ActiveRoom.MaxPlayers, *ActiveRoom.Code);
}

ATN_RoomInfo* UMP_GameInstance::EnsureRoomInfo(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client || World->bIsTearingDown)
	{
		return nullptr;
	}
	ATN_RoomInfo* Info = RoomInfoActor.Get();
	if (!IsValid(Info) || Info->GetWorld() != World)
	{
		Info = ATN_RoomInfo::Find(World);
		if (!Info)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.ObjectFlags |= RF_Transient;
			Info = World->SpawnActor<ATN_RoomInfo>(ATN_RoomInfo::StaticClass(), FTransform::Identity, Params);
		}
		RoomInfoActor = Info;
	}
	if (Info)
	{
		const APlayerController* HostPC = GetFirstLocalPlayerController(World);
		Info->ServerApply(ActiveRoom, HostPC && HostPC->PlayerState ? HostPC->PlayerState->GetPlayerName() : FString());
	}
	return Info;
}

void UMP_GameInstance::UpdateRoomAdvertisement()
{
	const int32 Players = CountRoomPlayers(GetWorld());
	AdvertisedPlayers = Players;
	AdvertisedMode = static_cast<int32>(SelectedProcMode);
	AdvertisedLocked = ActiveRoom.bLocked ? 1 : 0;

	IOnlineSessionPtr Sessions = GetSessionInterface();
	FNamedOnlineSession* Named = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (!Named || !Named->bHosting)
	{
		return;
	}
	FOnlineSessionSettings Settings = Named->SessionSettings;
	ApplyRoomSettings(Settings, Players);
	Sessions->UpdateSession(NAME_GameSession, Settings, true);
	UE_LOG(LogTortunabo, Log, TEXT("[Salas] Anuncio actualizado: %d/%d, %s, modo %d."), Players, ActiveRoom.MaxPlayers,
		ActiveRoom.bLocked ? TEXT("cerrada") : TEXT("abierta"), AdvertisedMode);
}

void UMP_GameInstance::ApplyRoomSettings(FOnlineSessionSettings& Settings, int32 Players) const
{
	const EOnlineDataAdvertisementType::Type Advertise = EOnlineDataAdvertisementType::ViaOnlineService;
	Settings.Set(TNRoomKeys::Keywords(), FString(TNRoomKeys::KeywordsValue()), Advertise);
	Settings.Set(TNRoomKeys::NameId(), ActiveRoom.NameId, Advertise);
	Settings.Set(TNRoomKeys::Code(), ActiveRoom.Code, Advertise);
	Settings.Set(TNRoomKeys::Private(), ActiveRoom.bPrivate ? 1 : 0, Advertise);
	Settings.Set(TNRoomKeys::Locked(), ActiveRoom.bLocked ? 1 : 0, Advertise);
	Settings.Set(TNRoomKeys::Mode(), static_cast<int32>(SelectedProcMode), Advertise);
	Settings.Set(TNRoomKeys::ModeSchema(), TNRoomKeys::ModeSchemaVersion, Advertise);
	Settings.Set(TNRoomKeys::Players(), FMath::Max(1, Players), Advertise);
}

void UMP_GameInstance::ResetRoomState()
{
	bHasActiveRoom = false;
	ActiveRoom = FTNRoomConfig();
	RoomMemberIds.Reset();
	KickedRoomIds.Reset();
	AdvertisedPlayers = INDEX_NONE;
	AdvertisedMode = INDEX_NONE;
	AdvertisedLocked = INDEX_NONE;
	RoomInfoActor.Reset();
	bKickedFromRoom = false;
	PendingJoinRoomName = FText::GetEmpty();
}

int32 UMP_GameInstance::CountRoomPlayers(const UWorld* World)
{
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	if (!State)
	{
		return 1;
	}
	int32 Count = 0;
	for (const APlayerState* PS : State->PlayerArray)
	{
		if (PS && !PS->IsABot() && !PS->IsInactive())
		{
			++Count;
		}
	}
	return FMath::Max(1, Count);
}

bool UMP_GameInstance::IsMenuWorld(const UWorld* World) const
{
	return World && UWorld::RemovePIEPrefix(World->GetMapName()) == FPackageName::GetShortName(MenuMapPath);
}

void UMP_GameInstance::HandleRoomRefused(const FString& Reason)
{
	const FText Message = TNRoomText::RefusedMessage(Reason);
	UE_LOG(LogTortunabo, Warning, TEXT("[Salas] El servidor no nos deja entrar: %s"), *Reason);
	// La entrada ha terminado (mal): se puede volver a intentar sin esperar a que cargue el menú.
	RoomOp = FTNRoomOpState();
	bPendingAutoRejoin = false;
	GetTimerManager().ClearTimer(AutoRejoinTimerHandle);
	HideLoadingScreen();
	DestroyCurrentSession();
	UpdateStatus(Message.ToString());
	// El motor vuelve a cargar el menú tras un fallo al conectar: el aviso lo enseña el menú nuevo, en «Unirse».
	PendingMenuNotice.Text = Message;
	PendingMenuNotice.bError = true;
	PendingMenuNotice.bOpenJoin = true;
	UWorld* World = GetWorld();
	if (World && !IsMenuWorld(World))
	{
		if (APlayerController* PC = GetFirstLocalPlayerController())
		{
			PC->ClientTravel(MenuMapPath, TRAVEL_Absolute);
		}
	}
}

#if !UE_BUILD_SHIPPING
void UMP_GameInstance::DebugFakeRoomError(const FString& Kind)
{
	const FString K = Kind.ToLower();
	FString Reason;
	if (K == TEXT("locked")) { Reason = TNRoomKeys::RefuseLocked(); }
	else if (K == TEXT("full")) { Reason = TNRoomKeys::RefuseFull(); }
	else if (K == TEXT("kicked")) { Reason = TNRoomKeys::RefuseKicked(); }
	else if (K == TEXT("other")) { Reason = TEXT("TNRoom:Prueba"); }

	// Versión distinta a la del anfitrión (NetChecksumMismatch, #245): el aviso de dos líneas en «Unirse».
	const bool bBuildMismatch = K == TEXT("build");
	if (bBuildMismatch || !Reason.IsEmpty())
	{
		if (bBuildMismatch)
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Salas] Prueba: versión distinta a la del anfitrión."));
			HandleChecksumMismatch(TEXT("prueba (TN.Rooms.FakeError build)"));
		}
		else
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Salas] Prueba: rechazo del servidor «%s»."), *Reason);
			HandleRoomRefused(Reason);
		}
		// En el menú, HandleRoomRefused no viaja: se recarga como hace el motor tras un fallo al conectar.
		UWorld* World = GetWorld();
		if (World && IsMenuWorld(World))
		{
			if (APlayerController* PC = GetFirstLocalPlayerController())
			{
				PC->ClientTravel(MenuMapPath, TRAVEL_Absolute);
			}
		}
		return;
	}

	if (K == TEXT("checksum"))
	{
		UE_LOG(LogTortunabo, Display, TEXT("[Salas] Prueba: versiones distintas con el host."));
		HandleChecksumMismatch(TEXT("Prueba"));
		OnDestroySessionComplete(NAME_GameSession, true);
		return;
	}

	EOnJoinSessionCompleteResult::Type Result;
	if (K == TEXT("joinfull")) { Result = EOnJoinSessionCompleteResult::SessionIsFull; }
	else if (K == TEXT("gone")) { Result = EOnJoinSessionCompleteResult::SessionDoesNotExist; }
	else if (K == TEXT("noaddress")) { Result = EOnJoinSessionCompleteResult::CouldNotRetrieveAddress; }
	else
	{
		UE_LOG(LogTortunabo, Display, TEXT("[Salas] TN.Rooms.FakeError <locked|full|kicked|other|build|checksum|joinfull|gone|noaddress>"));
		return;
	}
	UE_LOG(LogTortunabo, Display, TEXT("[Salas] Prueba: JoinSession falla con «%s»."), *K);
	OnJoinSessionComplete(NAME_GameSession, Result);
}
#endif

void UMP_GameInstance::PostRoomNotice(const FText& Message, bool bError)
{
	// Cada aviso de sala sustituye al anterior en el registro de estado: no se acumulan los de intentos previos.
	if (!LastRoomNoticeStatus.IsEmpty())
	{
		StatusLog.RemoveSingle(LastRoomNoticeStatus);
	}
	LastRoomNoticeStatus = Message.ToString();
	UpdateStatus(LastRoomNoticeStatus);
	OnRoomNotice.Broadcast(Message, bError);
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

