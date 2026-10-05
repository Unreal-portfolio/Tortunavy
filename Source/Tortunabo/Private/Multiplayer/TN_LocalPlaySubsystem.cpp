#include "Multiplayer/TN_LocalPlaySubsystem.h"
#include "Core/TN_Log.h"
#include "Lobby/TN_HQGameMode.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Multiplayer/TN_LocalPlayRules.h"
#include "Multiplayer/TN_LocalPlayerProfile.h"
#include "Player/MP_GamePlayerController.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "UI/HUD/TN_LocalSplitOverlay.h"
#include "UI/Loading/TN_LoadingScreenSubsystem.h"
#include "VR/TN_VRMode.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Scalability.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del bloque.
namespace TNLocalPlayDetail
{
	/** La capa de la pantalla partida, por debajo de todo lo que va a pantalla completa (pausa 60, carga 100000). */
	constexpr int32 OverlayZOrder = 1;

	/** Segundos que se ve el número de cada jugador en su vista al cambiar el reparto. */
	constexpr double TagSeconds = 5.0;

	/** Segundos que se ve el aviso de que con gafas no entran invitados (#639). */
	constexpr double VRNoticeSeconds = 6.0;

	/** Segundos manteniendo B antes de enseñar que se está saliendo (un toque de B es meterse en el caparazón). */
	constexpr double LeaveShowAfter = 0.2;

	/** Nombre en la partida local: «Jugador 1» a «Jugador 4» (se ve en el marcador y en las tarjetas). */
	FString PlayerName(int32 Number)
	{
		return FText::Format(NSLOCTEXT("TNLocal", "PlayerName", "Jugador {0}"), FText::AsNumber(Number)).ToString();
	}

	UTN_LocalPlaySubsystem* FindForCommand(UWorld* World)
	{
		UTN_LocalPlaySubsystem* LocalPlay = UTN_LocalPlaySubsystem::Get(World);
		if (!LocalPlay)
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Local] No hay subsistema del modo local en este mundo."));
		}
		return LocalPlay;
	}

#if !UE_BUILD_SHIPPING
	FAutoConsoleCommandWithWorldAndArgs CmdAddGuest(TEXT("TN.Local.AddGuest"),
		TEXT("Modo local: añade un invitado sin mando (para ver la pantalla partida sin tener cuatro mandos). Solo en el lobby de una partida local."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UTN_LocalPlaySubsystem* LocalPlay = FindForCommand(World))
			{
				const int32 Count = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 3) : 1;
				for (int32 i = 0; i < Count; ++i)
				{
					if (!LocalPlay->AddTestGuest()) { break; }
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdRemoveGuest(TEXT("TN.Local.RemoveGuest"),
		TEXT("Modo local: saca al último invitado (o al jugador N: TN.Local.RemoveGuest N, de 2 a 4)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UTN_LocalPlaySubsystem* LocalPlay = FindForCommand(World);
			UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
			if (!LocalPlay || !GameInstance || GameInstance->GetNumLocalPlayers() < 2)
			{
				UE_LOG(LogTortunabo, Display, TEXT("[Local] TN.Local.RemoveGuest: no hay invitados."));
				return;
			}
			const int32 Wanted = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
			ULocalPlayer* Target = GameInstance->GetLocalPlayers().Last();
			for (ULocalPlayer* Player : GameInstance->GetLocalPlayers())
			{
				const UTN_LocalPlayerProfile* Profile = UTN_LocalPlayerProfile::Get(Player);
				if (Wanted > 1 && Profile && Profile->GetPlayerNumber() == Wanted) { Target = Player; }
			}
			LocalPlay->RemoveGuest(Target);
		}));

	FAutoConsoleCommandWithWorld CmdInfo(TEXT("TN.Local.Info"),
		TEXT("Modo local: si está activo, los jugadores locales, su usuario de la plataforma y sus aparatos."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			UTN_LocalPlaySubsystem* LocalPlay = FindForCommand(World);
			const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
			if (!LocalPlay || !GameInstance)
			{
				return;
			}
			IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
			UE_LOG(LogTortunabo, Display, TEXT("[Local] Modo local: %s · %d jugadores locales · escala de la interfaz %.2f."),
				LocalPlay->IsLocalMode() ? TEXT("sí") : TEXT("no"), GameInstance->GetNumLocalPlayers(), LocalPlay->GetSplitUIScale());
			for (const ULocalPlayer* Player : GameInstance->GetLocalPlayers())
			{
				TArray<FInputDeviceId> Devices;
				Mapper.GetAllInputDevicesForUser(Player->GetPlatformUserId(), Devices);
				FString DeviceList;
				for (const FInputDeviceId Device : Devices) { DeviceList += FString::Printf(TEXT(" %d"), Device.GetId()); }
				const UTN_LocalPlayerProfile* Profile = UTN_LocalPlayerProfile::Get(Player);
				UE_LOG(LogTortunabo, Display, TEXT("[Local]   Jugador %d: usuario %d, aparatos:%s"), Profile ? Profile->GetPlayerNumber() : 0,
					Player->GetPlatformUserId().GetInternalId(), DeviceList.IsEmpty() ? TEXT(" ninguno") : *DeviceList);
			}
		}));
#endif
}

/**
 * Ve cada tecla antes que ningún widget: Start de un mando sin jugador (entrar) y B de un invitado (mantener para salir). No
 * se queda con nada salvo el Start que crea un jugador (ese mando aún no movía a nadie).
 */
class FTNLocalPlayInput : public IInputProcessor
{
public:
	explicit FTNLocalPlayInput(UTN_LocalPlaySubsystem* InOwner) : Owner(InOwner) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		UTN_LocalPlaySubsystem* LocalPlay = Owner.Get();
		return LocalPlay && LocalPlay->HandleKeyDown(InKeyEvent);
	}

	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (UTN_LocalPlaySubsystem* LocalPlay = Owner.Get())
		{
			LocalPlay->HandleKeyUp(InKeyEvent);
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("TNLocalPlayInput"); }

private:
	TWeakObjectPtr<UTN_LocalPlaySubsystem> Owner;
};

// ─────────────────────────────────────────────────────────────────────────────
// Consultas
// ─────────────────────────────────────────────────────────────────────────────

UTN_LocalPlaySubsystem* UTN_LocalPlaySubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext && GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		// Un widget o un objeto creado con la GameInstance de outer.
		GameInstance = Cast<UGameInstance>(WorldContext);
	}
	return GameInstance ? GameInstance->GetSubsystem<UTN_LocalPlaySubsystem>() : nullptr;
}

bool UTN_LocalPlaySubsystem::IsLocalGame(const UObject* WorldContext)
{
	const UTN_LocalPlaySubsystem* LocalPlay = Get(WorldContext);
	return LocalPlay && LocalPlay->IsLocalMode();
}

bool UTN_LocalPlaySubsystem::IsPrimaryPlayer(const APlayerController* PC)
{
	const ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
	const UGameInstance* GameInstance = PC ? PC->GetGameInstance() : nullptr;
	return Player && GameInstance && GameInstance->GetFirstGamePlayer() == Player;
}

bool UTN_LocalPlaySubsystem::IsGuest(const APlayerController* PC)
{
	return PC && PC->GetLocalPlayer() && IsLocalGame(PC) && !IsPrimaryPlayer(PC);
}

int32 UTN_LocalPlaySubsystem::GetPlayerNumber(const APlayerController* PC)
{
	if (!IsLocalGame(PC))
	{
		return 1;
	}
	if (const UTN_LocalPlayerProfile* Profile = UTN_LocalPlayerProfile::Get(PC))
	{
		if (Profile->GetPlayerNumber() > 0)
		{
			return Profile->GetPlayerNumber();
		}
	}
	const ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
	const UGameInstance* GameInstance = PC ? PC->GetGameInstance() : nullptr;
	const int32 Index = GameInstance && Player ? GameInstance->GetLocalPlayers().IndexOfByKey(Player) : INDEX_NONE;
	return Index == INDEX_NONE ? 1 : Index + 1;
}

bool UTN_LocalPlaySubsystem::ShouldSaveFor(const APlayerController* PC)
{
	return TNLocalPlay::ShouldSave(IsLocalGame(PC), !IsGuest(PC));
}

bool UTN_LocalPlaySubsystem::IsLobbyWorld(const UObject* WorldContext)
{
	const UWorld* World = WorldContext && GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return false;
	}
	if (const AGameModeBase* GameMode = World->GetAuthGameMode())
	{
		return GameMode->IsA<ATN_HQGameMode>();
	}
	const AGameStateBase* State = World->GetGameState();
	return State && State->GameModeClass && State->GameModeClass->IsChildOf(ATN_HQGameMode::StaticClass());
}

int32 UTN_LocalPlaySubsystem::GetNumPlayers() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetNumLocalPlayers() : 0;
}

float UTN_LocalPlaySubsystem::GetSplitUIScale() const
{
	return bLocalMode ? TNLocalPlay::UIScaleForViews(GetNumPlayers()) : 1.f;
}

float UTN_LocalPlaySubsystem::GetLeaveProgress(const ULocalPlayer* Player) const
{
	if (!Player)
	{
		return -1.f;
	}
	const double Now = FPlatformTime::Seconds();
	for (const FLeaveHold& Hold : LeaveHolds)
	{
		if (Hold.User == Player->GetPlatformUserId() && Now - Hold.Since >= TNLocalPlayDetail::LeaveShowAfter)
		{
			return FMath::Clamp(static_cast<float>((Now - Hold.Since) / TNLocalPlay::LeaveHoldSeconds), 0.f, 1.f);
		}
	}
	return -1.f;
}

FPlatformUserId UTN_LocalPlaySubsystem::GetPrimaryUser() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const ULocalPlayer* First = GameInstance ? GameInstance->GetFirstGamePlayer() : nullptr;
	return First ? First->GetPlatformUserId() : IPlatformInputDeviceMapper::Get().GetPrimaryPlatformUser();
}

ULocalPlayer* UTN_LocalPlaySubsystem::FindPlayerForUser(FPlatformUserId User) const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance && User.IsValid() ? GameInstance->FindLocalPlayerFromPlatformUserId(User) : nullptr;
}

FPlatformUserId UTN_LocalPlaySubsystem::FindFreeUser() const
{
	IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
	const FPlatformUserId Candidate = Mapper.GetFirstPlatformUserWithNoInputDevice();
	if (Candidate.IsValid() && Candidate != GetPrimaryUser() && !FindPlayerForUser(Candidate))
	{
		return Candidate;
	}
	return Mapper.AllocateNewUserId();
}

bool UTN_LocalPlaySubsystem::IsGamepadDevice(FInputDeviceId Device)
{
	if (!Device.IsValid() || Device == IPlatformInputDeviceMapper::Get().GetDefaultInputDevice())
	{
		return false;
	}
	// El teclado y el ratón van con el aparato de serie; si el sistema sabe qué es, se le pregunta.
	if (const UInputDeviceSubsystem* Devices = UInputDeviceSubsystem::Get())
	{
		const EHardwareDevicePrimaryType Type = Devices->GetInputDeviceHardwareIdentifier(Device).PrimaryDeviceType;
		if (Type != EHardwareDevicePrimaryType::Unspecified)
		{
			return Type == EHardwareDevicePrimaryType::Gamepad;
		}
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ciclo de vida
// ─────────────────────────────────────────────────────────────────────────────

bool UTN_LocalPlaySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UTN_LocalPlaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (FSlateApplication::IsInitialized())
	{
		InputProcessor = MakeShared<FTNLocalPlayInput>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
	}
	TWeakObjectPtr<UTN_LocalPlaySubsystem> WeakThis(this);
	ConnectionChangeHandle = IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddLambda(
		[WeakThis](EInputDeviceConnectionState NewState, FPlatformUserId User, FInputDeviceId Device)
		{
			if (UTN_LocalPlaySubsystem* LocalPlay = WeakThis.Get())
			{
				LocalPlay->HandleDeviceConnectionChange(NewState, User, Device);
			}
		});
}

void UTN_LocalPlaySubsystem::Deinitialize()
{
	EndLocalGame();
	if (InputProcessor.IsValid())
	{
		if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor); }
		InputProcessor.Reset();
	}
	IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().Remove(ConnectionChangeHandle);
	ConnectionChangeHandle.Reset();
	Super::Deinitialize();
}

ETickableTickType UTN_LocalPlaySubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UTN_LocalPlaySubsystem::IsTickable() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance && GameInstance->GetGameViewportClient();
}

TStatId UTN_LocalPlaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTN_LocalPlaySubsystem, STATGROUP_Tickables);
}

UWorld* UTN_LocalPlaySubsystem::GetTickableGameObjectWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

void UTN_LocalPlaySubsystem::Tick(float DeltaTime)
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!bLocalMode)
	{
		if (Overlay && TNVR::IsOnScreen(Overlay)) { Overlay->RemoveFromParent(); }
		return;
	}
	if (!World || World->bIsTearingDown || World->IsInSeamlessTravel())
	{
		return;
	}

	// Lo que llegó por la entrada de Slate o por los avisos de los mandos se atiende aquí, fuera de sus repartos.
	TArray<FInputDeviceId> Connections = MoveTemp(PendingConnections);
	for (const FInputDeviceId Device : Connections) { ProcessConnection(Device); }
	TArray<TWeakObjectPtr<ULocalPlayer>> Removals = MoveTemp(PendingRemovals);
	for (const TWeakObjectPtr<ULocalPlayer>& Player : Removals) { RemoveGuest(Player.Get()); }
	TArray<FInputDeviceId> Joins = MoveTemp(PendingJoins);
	for (const FInputDeviceId Device : Joins) { TryJoin(Device); }

	const int32 Views = GetNumPlayers();
	UGameViewportClient* Viewport = GameInstance->GetGameViewportClient();
	if (Viewport && PatchedViewport.Get() != Viewport)
	{
		ApplySplitLayout(false);
	}
	if (Views != AppliedViews)
	{
		AppliedViews = Views;
		TagsUntil = FPlatformTime::Seconds() + TNLocalPlayDetail::TagSeconds;
		if (Viewport) { Viewport->LayoutPlayers(); }
	}
	ApplyQuality(Views, false);
	ApplyPlayerNames(World);
	TickLeaveHolds();
	UpdateOverlay(World);
}

// ─────────────────────────────────────────────────────────────────────────────
// Empezar y acabar
// ─────────────────────────────────────────────────────────────────────────────

void UTN_LocalPlaySubsystem::StartLocalGame(const FString& LobbyMap)
{
	UMP_GameInstance* GameInstance = Cast<UMP_GameInstance>(GetGameInstance());
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!GameInstance || !World)
	{
		return;
	}
	// Por si quedaba algo de otra partida local (no debería: se acaba al volver al menú).
	EndLocalGame();
	bLocalMode = true;
	AppliedViews = 0;

	// Lo que usó el jugador 1 para elegir «Local»: si fue un mando, es suyo; el resto queda libre para unirse.
	PrimaryPad = INPUTDEVICEID_NONE;
	const FPlatformUserId PrimaryUser = GetPrimaryUser();
	if (const UInputDeviceSubsystem* Devices = UInputDeviceSubsystem::Get())
	{
		const FInputDeviceId Last = Devices->GetMostRecentlyUsedInputDeviceId(PrimaryUser);
		if (IsGamepadDevice(Last) && IPlatformInputDeviceMapper::Get().GetUserForInputDevice(Last) == PrimaryUser)
		{
			PrimaryPad = Last;
		}
	}
	ReleaseSecondaryPads();
	if (UTN_LocalPlayerProfile* Profile = UTN_LocalPlayerProfile::Get(GameInstance->GetFirstGamePlayer()))
	{
		Profile->SetPlayerNumber(1);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Local] Partida local: el jugador 1 juega con %s; los demás mandos se unen con Start en el lobby."),
		PrimaryPad.IsValid() ? *FString::Printf(TEXT("el teclado y el mando %d"), PrimaryPad.GetId()) : TEXT("el teclado y el ratón"));

	// Al lobby sin red (sin ?listen: Standalone), con la pantalla de carga de siempre cerrada antes del LoadMap.
	GameInstance->ShowLoadingScreen(NSLOCTEXT("TNLocal", "Starting", "Preparando la partida local...").ToString());
	TWeakObjectPtr<UMP_GameInstance> WeakGameInstance(GameInstance);
	TFunction<void()> Travel = [WeakGameInstance, LobbyMap]()
	{
		if (UWorld* TravelWorld = WeakGameInstance.IsValid() ? WeakGameInstance->GetWorld() : nullptr)
		{
			UGameplayStatics::OpenLevel(TravelWorld, FName(*LobbyMap), true);
		}
	};
	if (UTN_LoadingScreenSubsystem* EggLoading = GameInstance->GetSubsystem<UTN_LoadingScreenSubsystem>())
	{
		EggLoading->RunWhenClosed(MoveTemp(Travel));
	}
	else
	{
		Travel();
	}
}

void UTN_LocalPlaySubsystem::EndLocalGame()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance)
	{
		// Los invitados fuera (del último al primero; el jugador 1 se queda).
		TArray<ULocalPlayer*> Players = GameInstance->GetLocalPlayers();
		for (int32 i = Players.Num() - 1; i >= 1; --i)
		{
			RemoveGuest(Players[i]);
		}
		if (UTN_LocalPlayerProfile* Profile = UTN_LocalPlayerProfile::Get(GameInstance->GetFirstGamePlayer()))
		{
			Profile->SetPlayerNumber(0);
		}
	}
	const bool bWasLocal = bLocalMode;
	RestoreReleasedPads();
	ApplySplitLayout(true);
	ApplyQuality(0, true);
	if (Overlay)
	{
		Overlay->RemoveFromParent();
		Overlay = nullptr;
	}
	LeaveHolds.Reset();
	PendingJoins.Reset();
	PendingRemovals.Reset();
	PendingConnections.Reset();
	GuestPads.Reset();
	PrimaryPad = INPUTDEVICEID_NONE;
	AppliedViews = 0;
	bLocalMode = false;
	if (bWasLocal)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Local] Fin de la partida local: sin invitados, mandos, pantalla y calidad como estaban."));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Entrar y salir
// ─────────────────────────────────────────────────────────────────────────────

bool UTN_LocalPlaySubsystem::TryJoin(FInputDeviceId Device)
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!GameInstance || !World)
	{
		return false;
	}
	IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
	FPlatformUserId User = Mapper.GetUserForInputDevice(Device);
	TNLocalPlay::FJoinQuery Query;
	Query.bLocalMode = bLocalMode;
	Query.bInLobby = IsLobbyWorld(World);
	Query.Players = GameInstance->GetNumLocalPlayers();
	Query.bGamepad = IsGamepadDevice(Device);
	Query.bDeviceHasPlayer = User == GetPrimaryUser() || FindPlayerForUser(User) != nullptr;
	Query.bVR = TNVR::IsEnabled();
	const TNLocalPlay::EJoin Decision = TNLocalPlay::DecideJoin(Query);
	if (Decision == TNLocalPlay::EJoin::VR)
	{
		// Con gafas el invitado no entra (destruiría el rig del jugador 1): se avisa en el panel VR (UpdateOverlay).
		VRNoticeUntil = FPlatformTime::Seconds() + TNLocalPlayDetail::VRNoticeSeconds;
	}
	if (Decision != TNLocalPlay::EJoin::Accept)
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[Local] El mando %d no entra (%d)."), Device.GetId(), static_cast<int32>(Decision));
		return false;
	}
	// Un mando sin usuario (recién conectado y sin dueño) recibe uno libre: será el de su jugador.
	if (!User.IsValid())
	{
		const FPlatformUserId Free = FindFreeUser();
		Mapper.Internal_ChangeInputDeviceUserMapping(Device, Free, User);
		User = Free;
	}
	if (!CreateGuest(User))
	{
		return false;
	}
	GuestPads.Add(Device, User);
	return true;
}

bool UTN_LocalPlaySubsystem::AddTestGuest()
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!bLocalMode || !World || !IsLobbyWorld(World) || GameInstance->GetNumLocalPlayers() >= TNLocalPlay::MaxPlayers)
	{
		UE_LOG(LogTortunabo, Display, TEXT("[Local] TN.Local.AddGuest: solo en el lobby de una partida local y con sitio (hasta %d)."), TNLocalPlay::MaxPlayers);
		return false;
	}
	// Un usuario sin aparatos: su tortuga aparece y su vista se reparte, pero no la mueve nadie.
	return CreateGuest(IPlatformInputDeviceMapper::Get().AllocateNewUserId());
}

bool UTN_LocalPlaySubsystem::CreateGuest(FPlatformUserId User)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return false;
	}
	TArray<int32> Taken;
	for (const ULocalPlayer* Player : GameInstance->GetLocalPlayers())
	{
		if (const UTN_LocalPlayerProfile* Profile = UTN_LocalPlayerProfile::Get(Player))
		{
			Taken.Add(Profile->GetPlayerNumber());
		}
	}
	const int32 Number = TNLocalPlay::NextGuestNumber(Taken);
	if (Number == INDEX_NONE)
	{
		return false;
	}
	FString Error;
	ULocalPlayer* NewPlayer = GameInstance->CreateLocalPlayer(User, Error, true);
	if (!NewPlayer)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Local] No se ha podido crear el jugador %d: %s"), Number, *Error);
		return false;
	}
	if (UTN_LocalPlayerProfile* Profile = UTN_LocalPlayerProfile::Get(NewPlayer))
	{
		Profile->SetPlayerNumber(Number);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Local] Entra el jugador %d (usuario %d): %d jugadores locales."), Number, User.GetInternalId(),
		GameInstance->GetNumLocalPlayers());
	return true;
}

bool UTN_LocalPlaySubsystem::LeaveGame(APlayerController* PC)
{
	ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
	if (!Player || !TNLocalPlay::CanLeave(bLocalMode, IsLobbyWorld(PC), IsPrimaryPlayer(PC)))
	{
		return false;
	}
	// En el siguiente fotograma: quien lo pide suele ser un widget de ese mismo jugador.
	PendingRemovals.AddUnique(Player);
	return true;
}

bool UTN_LocalPlaySubsystem::RemoveGuest(ULocalPlayer* Player)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance || !Player)
	{
		return false;
	}
	const int32 Index = GameInstance->GetLocalPlayers().IndexOfByKey(Player);
	if (Index <= 0)
	{
		// El jugador 1 no se quita nunca (ni uno que ya no está).
		return false;
	}
	UWorld* World = GameInstance->GetWorld();
	const UTN_LocalPlayerProfile* Profile = UTN_LocalPlayerProfile::Get(Player);
	const int32 Number = Profile ? Profile->GetPlayerNumber() : Index + 1;
	if (APlayerController* PC = World ? Player->GetPlayerController(World) : nullptr)
	{
		// Su menú de pausa, su tienda o su probador se cierran con él; su tortuga se va (al quitar el jugador local el
		// PlayerController suelta el peón, pero no lo destruye).
		if (UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(GameInstance))
		{
			if (Settings->GetPauseMenuOwner() == PC) { Settings->ClosePauseMenu(); }
		}
		if (AMP_GamePlayerController* GamePC = Cast<AMP_GamePlayerController>(PC))
		{
			GamePC->CloseShopUI();
		}
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->Destroy();
		}
	}
	LeaveHolds.RemoveAll([Player](const FLeaveHold& Hold) { return Hold.User == Player->GetPlatformUserId(); });
	for (auto It = GuestPads.CreateIterator(); It; ++It)
	{
		if (It.Value() == Player->GetPlatformUserId()) { It.RemoveCurrent(); }
	}
	const bool bRemoved = GameInstance->RemoveLocalPlayer(Player);
	UE_LOG(LogTortunabo, Log, TEXT("[Local] Sale el jugador %d: %d jugadores locales."), Number, GameInstance->GetNumLocalPlayers());
	return bRemoved;
}

bool UTN_LocalPlaySubsystem::HandleKeyDown(const FKeyEvent& Event)
{
	if (!bLocalMode || Event.IsRepeat())
	{
		return false;
	}
	const FKey Key = Event.GetKey();
	if (!Key.IsGamepadKey())
	{
		return false;
	}
	const FInputDeviceId Device = Event.GetInputDeviceId();
	const FPlatformUserId User = IPlatformInputDeviceMapper::Get().GetUserForInputDevice(Device);
	ULocalPlayer* Player = FindPlayerForUser(User);
	if (Key == EKeys::Gamepad_Special_Right)
	{
		// Start de un mando que aún no juega: pide entrar. Ese Start no es de nadie, así que no sigue.
		if (!Player && User != GetPrimaryUser())
		{
			PendingJoins.AddUnique(Device);
			return true;
		}
		return false;
	}
	if (Key == EKeys::Gamepad_FaceButton_Right && Player)
	{
		const UGameInstance* GameInstance = GetGameInstance();
		UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
		const APlayerController* PC = World ? Player->GetPlayerController(World) : nullptr;
		// Un invitado en el lobby, jugando (sin menú ni pausa): empieza a contar el tiempo que mantiene B.
		if (PC && !World->IsPaused() && !PC->ShouldShowMouseCursor() && TNLocalPlay::CanLeave(bLocalMode, IsLobbyWorld(World), IsPrimaryPlayer(PC))
			&& !LeaveHolds.ContainsByPredicate([User](const FLeaveHold& Hold) { return Hold.User == User; }))
		{
			FLeaveHold Hold;
			Hold.User = User;
			Hold.Since = FPlatformTime::Seconds();
			LeaveHolds.Add(Hold);
		}
	}
	return false;
}

void UTN_LocalPlaySubsystem::HandleKeyUp(const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Gamepad_FaceButton_Right)
	{
		const FPlatformUserId User = IPlatformInputDeviceMapper::Get().GetUserForInputDevice(Event.GetInputDeviceId());
		LeaveHolds.RemoveAll([User](const FLeaveHold& Hold) { return Hold.User == User; });
	}
}

void UTN_LocalPlaySubsystem::TickLeaveHolds()
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	// Sin la ventana activa se pierden los «soltar»: nadie sale por eso.
	if (!World || (FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsActive()))
	{
		LeaveHolds.Reset();
		return;
	}
	const double Now = FPlatformTime::Seconds();
	for (int32 i = LeaveHolds.Num() - 1; i >= 0; --i)
	{
		ULocalPlayer* Player = FindPlayerForUser(LeaveHolds[i].User);
		const APlayerController* PC = Player ? Player->GetPlayerController(World) : nullptr;
		if (!PC || World->IsPaused() || PC->ShouldShowMouseCursor() || !TNLocalPlay::CanLeave(bLocalMode, IsLobbyWorld(World), IsPrimaryPlayer(PC)))
		{
			LeaveHolds.RemoveAt(i);
			continue;
		}
		if (Now - LeaveHolds[i].Since >= TNLocalPlay::LeaveHoldSeconds)
		{
			LeaveHolds.RemoveAt(i);
			RemoveGuest(Player);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Mandos
// ─────────────────────────────────────────────────────────────────────────────

void UTN_LocalPlaySubsystem::ReleaseSecondaryPads()
{
	IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
	const FPlatformUserId PrimaryUser = GetPrimaryUser();
	TArray<FInputDeviceId> Devices;
	Mapper.GetAllInputDevicesForUser(PrimaryUser, Devices);
	TArray<int32> Pads;
	for (const FInputDeviceId Device : Devices)
	{
		if (IsGamepadDevice(Device) && Mapper.GetInputDeviceConnectionState(Device) == EInputDeviceConnectionState::Connected)
		{
			Pads.Add(Device.GetId());
		}
	}
	for (const int32 PadId : TNLocalPlay::PadsToRelease(Pads, PrimaryPad.IsValid() ? PrimaryPad.GetId() : INDEX_NONE))
	{
		const FInputDeviceId Pad = FInputDeviceId::CreateFromInternalId(PadId);
		const FPlatformUserId Free = FindFreeUser();
		if (Mapper.Internal_ChangeInputDeviceUserMapping(Pad, Free, PrimaryUser))
		{
			FReleasedPad Released;
			Released.Device = Pad;
			Released.Original = PrimaryUser;
			Released.Assigned = Free;
			ReleasedPads.Add(Released);
			UE_LOG(LogTortunabo, Log, TEXT("[Local] El mando %d queda libre (usuario %d): se une con Start."), PadId, Free.GetInternalId());
		}
	}
}

void UTN_LocalPlaySubsystem::RestoreReleasedPads()
{
	IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
	for (const FReleasedPad& Released : ReleasedPads)
	{
		const FPlatformUserId Now = Mapper.GetUserForInputDevice(Released.Device);
		if (Now != Released.Original)
		{
			Mapper.Internal_ChangeInputDeviceUserMapping(Released.Device, Released.Original, Now);
		}
	}
	ReleasedPads.Reset();
}

void UTN_LocalPlaySubsystem::HandleDeviceConnectionChange(EInputDeviceConnectionState NewState, FPlatformUserId User, FInputDeviceId Device)
{
	if (bLocalMode && NewState == EInputDeviceConnectionState::Connected)
	{
		PendingConnections.AddUnique(Device);
	}
}

void UTN_LocalPlaySubsystem::ProcessConnection(FInputDeviceId Device)
{
	IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
	if (!IsGamepadDevice(Device))
	{
		return;
	}
	const FPlatformUserId User = Mapper.GetUserForInputDevice(Device);
	const FPlatformUserId PrimaryUser = GetPrimaryUser();
	// El mando de un invitado que vuelve: otra vez suyo.
	if (const FPlatformUserId* Owner = GuestPads.Find(Device))
	{
		if (FindPlayerForUser(*Owner) && *Owner != User)
		{
			Mapper.Internal_ChangeInputDeviceUserMapping(Device, *Owner, User);
		}
		return;
	}
	// El del jugador 1, suyo.
	if (Device == PrimaryPad)
	{
		if (User != PrimaryUser) { Mapper.Internal_ChangeInputDeviceUserMapping(Device, PrimaryUser, User); }
		return;
	}
	// Cualquier otro que el sistema le dé al jugador 1, libre (se une con Start).
	if (User == PrimaryUser)
	{
		const FPlatformUserId Free = FindFreeUser();
		if (Mapper.Internal_ChangeInputDeviceUserMapping(Device, Free, PrimaryUser))
		{
			FReleasedPad Released;
			Released.Device = Device;
			Released.Original = PrimaryUser;
			Released.Assigned = Free;
			ReleasedPads.Add(Released);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Pantalla partida, calidad, nombres y capa
// ─────────────────────────────────────────────────────────────────────────────

void UTN_LocalPlaySubsystem::ApplySplitLayout(bool bRestore)
{
	if (bRestore)
	{
		if (UGameViewportClient* Patched = PatchedViewport.Get())
		{
			if (SavedSplitscreenInfo.Num() > 0) { Patched->SplitscreenInfo = SavedSplitscreenInfo; }
			Patched->LayoutPlayers();
		}
		PatchedViewport.Reset();
		SavedSplitscreenInfo.Reset();
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UGameViewportClient* Viewport = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	if (!Viewport)
	{
		return;
	}
	if (PatchedViewport.Get() != Viewport)
	{
		SavedSplitscreenInfo = Viewport->SplitscreenInfo;
		PatchedViewport = Viewport;
	}
	// El reparto de TNLocalPlay::SplitLayout en todas las variantes de 2, 3 y 4 jugadores: así manda la regla sea cual sea la
	// configuración ([/Script/EngineSettings.GameMapsSettings]); el online (un jugador por máquina) no lo ve nunca.
	auto Write = [Viewport](ESplitScreenType::Type Type, int32 Players)
	{
		if (!Viewport->SplitscreenInfo.IsValidIndex(Type))
		{
			return;
		}
		FSplitscreenData& Data = Viewport->SplitscreenInfo[Type];
		Data.PlayerData.Reset();
		for (const TNLocalPlay::FViewRect& Rect : TNLocalPlay::SplitLayout(Players))
		{
			Data.PlayerData.Add(FPerPlayerSplitscreenData(Rect.W, Rect.H, Rect.X, Rect.Y));
		}
	};
	Write(ESplitScreenType::TwoPlayer_Horizontal, 2);
	Write(ESplitScreenType::TwoPlayer_Vertical, 2);
	Write(ESplitScreenType::ThreePlayer_FavorTop, 3);
	Write(ESplitScreenType::ThreePlayer_FavorBottom, 3);
	Write(ESplitScreenType::ThreePlayer_Vertical, 3);
	Write(ESplitScreenType::ThreePlayer_Horizontal, 3);
	Write(ESplitScreenType::FourPlayer_Grid, 4);
	Write(ESplitScreenType::FourPlayer_Vertical, 4);
	Write(ESplitScreenType::FourPlayer_Horizontal, 4);
	// Pantalla partida encendida (y el reparto al momento).
	Viewport->SetForceDisableSplitscreen(false);
}

void UTN_LocalPlaySubsystem::ApplyQuality(int32 Views, bool bRestore)
{
	const bool bWant = !bRestore && bLocalMode && TNLocalPlay::ShouldReduceQuality(Views);
	if (bWant == bQualityReduced)
	{
		return;
	}
	if (!bWant)
	{
		// Vuelve la de verdad (y lo que se haya elegido en los gráficos mientras tanto).
		Scalability::ToggleTemporaryQualityLevels(false);
		bQualityReduced = false;
		UE_LOG(LogTortunabo, Log, TEXT("[Local] Calidad de siempre."));
		return;
	}
	if (Scalability::IsTemporaryQualityLevelActive())
	{
		// Otro ya usa la calidad temporal: no se toca.
		return;
	}
	const Scalability::FQualityLevels Current = Scalability::GetQualityLevels();
	// Solo la distancia de dibujo y las sombras (-1: el resto, sin tocar).
	Scalability::FQualityLevels Override(false);
	Override.ResolutionQuality = -1.f;
	Override.ViewDistanceQuality = TNLocalPlay::ReducedQualityLevel(Current.ViewDistanceQuality);
	Override.AntiAliasingQuality = -1;
	Override.ShadowQuality = TNLocalPlay::ReducedQualityLevel(Current.ShadowQuality);
	Override.GlobalIlluminationQuality = -1;
	Override.ReflectionQuality = -1;
	Override.PostProcessQuality = -1;
	Override.TextureQuality = -1;
	Override.EffectsQuality = -1;
	Override.FoliageQuality = -1;
	Override.ShadingQuality = -1;
	Override.LandscapeQuality = -1;
	Scalability::ToggleTemporaryQualityLevels(true, Override);
	Scalability::FQualityLevels Reduced = Current;
	Reduced.ViewDistanceQuality = Override.ViewDistanceQuality;
	Reduced.ShadowQuality = Override.ShadowQuality;
	Scalability::SetQualityLevels(Reduced, true);
	bQualityReduced = true;
	UE_LOG(LogTortunabo, Log, TEXT("[Local] %d vistas: distancia de dibujo %d → %d y sombras %d → %d mientras dure."), Views,
		Current.ViewDistanceQuality, Reduced.ViewDistanceQuality, Current.ShadowQuality, Reduced.ShadowQuality);
}

void UTN_LocalPlaySubsystem::ApplyPlayerNames(UWorld* World)
{
	AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr;
	const UGameInstance* GameInstance = GetGameInstance();
	if (!GameMode || !GameInstance)
	{
		return;
	}
	for (const ULocalPlayer* Player : GameInstance->GetLocalPlayers())
	{
		APlayerController* PC = Player ? Player->GetPlayerController(World) : nullptr;
		if (!PC || !PC->PlayerState)
		{
			continue;
		}
		const FString Wanted = TNLocalPlayDetail::PlayerName(GetPlayerNumber(PC));
		if (PC->PlayerState->GetPlayerName() != Wanted)
		{
			GameMode->ChangeName(PC, Wanted, false);
		}
	}
}

void UTN_LocalPlaySubsystem::UpdateOverlay(UWorld* World)
{
	UGameInstance* GameInstance = GetGameInstance();
	// Con gafas la capa no tiene nada que repartir; solo se enseña mientras dura el aviso de que no entran invitados (#639).
	const bool bVR = TNVR::IsEnabled();
	const bool bVRNotice = bVR && FPlatformTime::Seconds() < VRNoticeUntil;
	if (!GameInstance || !World || (bVR && !bVRNotice))
	{
		if (Overlay && TNVR::IsOnScreen(Overlay)) { Overlay->RemoveFromParent(); }
		return;
	}
	if (!Overlay)
	{
		Overlay = CreateWidget<UTN_LocalSplitOverlay>(GameInstance, UTN_LocalSplitOverlay::StaticClass());
	}
	if (!Overlay)
	{
		return;
	}
	// Tras un viaje el mundo quita todo lo de la pantalla: se vuelve a poner. A toda la pantalla, por encima de las vistas.
	if (!TNVR::IsOnScreen(Overlay))
	{
		TNVR::AddToFullScreen(Overlay, TNLocalPlayDetail::OverlayZOrder);
	}

	const bool bLobby = IsLobbyWorld(World);
	const TArray<ULocalPlayer*>& Players = GameInstance->GetLocalPlayers();
	const TArray<TNLocalPlay::FViewRect> Rects = TNLocalPlay::SplitLayout(Players.Num());
	const bool bTags = FPlatformTime::Seconds() < TagsUntil && Players.Num() > 1;
	FTNSplitOverlayState State;
	for (int32 i = 0; i < Players.Num() && i < Rects.Num(); ++i)
	{
		const APlayerController* PC = Players[i] ? Players[i]->GetPlayerController(World) : nullptr;
		FTNSplitOverlayView View;
		View.Rect = Rects[i];
		View.PlayerNumber = PC ? GetPlayerNumber(PC) : i + 1;
		View.LeaveProgress = GetLeaveProgress(Players[i]);
		View.bShowTag = bTags;
		View.bCanLeave = bLobby && i > 0;
		State.Views.Add(View);
	}
	State.bHasEmptyRect = TNLocalPlay::EmptyQuadrant(Players.Num(), State.EmptyRect);
	State.bCanJoin = bLobby && !bVR && Players.Num() < TNLocalPlay::MaxPlayers;
	if (bVRNotice)
	{
		State.Notice = NSLOCTEXT("TNLocal", "VRNoGuests", "Con las gafas de VR puestas no se puede jugar a pantalla partida: quítatelas para que se una otro jugador.");
	}
	Overlay->Refresh(State);
}
