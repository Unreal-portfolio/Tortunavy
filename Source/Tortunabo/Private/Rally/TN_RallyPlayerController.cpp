#include "Rally/TN_RallyPlayerController.h"

#include "Components/InputComponent.h"
#include "Containers/Ticker.h"
#include "Core/TN_CoopPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "GameFramework/Pawn.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Rally/TN_RallyHUDWidget.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyPlayerState.h"
#include "Rally/UI/TN_RallyCopilotTablet.h"
#include "Voice/ProximityVoiceComponent.h"
#include "Rally/UI/TN_RallyDashboard.h"
#include "Rally/TN_RallyCameraDirector.h"
#include "Rally/TN_RallyCopilotComponent.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyGunnerPawn.h"
#include "VR/TN_VRMode.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace TNRallyPC
{
	/** Cada cuánto se comprueba que el buggy local lleva el salpicadero y el cartel del arco (s). */
	constexpr float DashboardCheckSeconds = 0.5f;
	/** Prioridad del postproceso del Rally: por encima de los volúmenes del nivel (que suelen ir en 0). */
	constexpr float PostProcessPriority = 1000.f;
}

ATN_RallyPlayerController::ATN_RallyPlayerController()
{
	HUDWidgetClass = UTN_RallyHUDWidget::StaticClass();
	static ConstructorHelpers::FObjectFinder<USoundBase> HitConfirmFinder(TEXT("/Game/Audio/Rally/SFX_Impact_Bubble_Pop.SFX_Impact_Bubble_Pop"));
	HitConfirmSound = HitConfirmFinder.Object;
}

void ATN_RallyPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
	if (HUDWidgetClass && !RallyHUD)
	{
		RallyHUD = CreateWidget<UTN_RallyHUDWidget>(this, HUDWidgetClass);
		if (RallyHUD)
		{
			// En VR, al panel del mundo como el resto de la interfaz.
			TNVR::AddToScreen(RallyHUD, 0);
		}
	}
	// Tableta de copiloto: grande para la artillera, compacta para la conductora sola; elige sola por la plaza.
	UTN_RallyCopilotTablet::FindOrCreateFor(this);
	// Cámara de llegada, podio y espectador (#306): solo en esta máquina.
	if (!CameraDirector)
	{
		CameraDirector = NewObject<UTN_RallyCameraDirector>(this, TEXT("RallyCameraDirector"));
		CameraDirector->RegisterComponent();
	}
	if (!Copilot)
	{
		Copilot = NewObject<UTN_RallyCopilotComponent>(this, TEXT("RallyCopilot"));
		Copilot->RegisterComponent();
	}
	ApplyRallyPostProcess();
	SyncCosmeticsToServer();
}

void ATN_RallyPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RallyHUD)
	{
		RallyHUD->RemoveFromParent();
		RallyHUD = nullptr;
	}
	if (IsValid(RallyPostProcess))
	{
		RallyPostProcess->Destroy();
	}
	RallyPostProcess = nullptr;
	Super::EndPlay(EndPlayReason);
}

FPostProcessSettings ATN_RallyPlayerController::MakeRallyPostProcess(float InMotionBlurAmount)
{
	FPostProcessSettings Settings;
	Settings.bOverride_MotionBlurAmount = true;
	Settings.MotionBlurAmount = FMath::Clamp(InMotionBlurAmount, 0.f, 1.f);
	return Settings;
}

void ATN_RallyPlayerController::ApplyRallyPostProcess()
{
	UWorld* World = GetWorld();
	if (IsValid(RallyPostProcess) || !World || !FApp::CanEverRender())
	{
		return;
	}
	// Local y sin replicar: cada pantalla quita su desenfoque (#605). Sin límites y por encima de los volúmenes del nivel,
	// así vale para todas las cámaras del Rally (persecución, artillera, llegada, podio y espectador).
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.ObjectFlags |= RF_Transient;
	RallyPostProcess = World->SpawnActor<APostProcessVolume>(Params);
	if (!RallyPostProcess)
	{
		UE_LOG(LogTNRally, Warning, TEXT("[RallyPC] No se pudo crear el postproceso del Rally: queda el desenfoque del nivel."));
		return;
	}
	RallyPostProcess->bUnbound = true;
	RallyPostProcess->Priority = TNRallyPC::PostProcessPriority;
	RallyPostProcess->BlendWeight = 1.f;
	RallyPostProcess->Settings = MakeRallyPostProcess(MotionBlurAmount);
	UE_LOG(LogTNRally, Log, TEXT("[RallyPC] Desenfoque de movimiento del Rally: %.2f."), RallyPostProcess->Settings.MotionBlurAmount);
}

void ATN_RallyPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent)
	{
		return;
	}
	const TPair<FKey, int32> Bindings[] = { { EKeys::A, -1 }, { EKeys::Left, -1 }, { EKeys::Gamepad_LeftShoulder, -1 },
		{ EKeys::D, 1 }, { EKeys::Right, 1 }, { EKeys::Gamepad_RightShoulder, 1 } };
	for (const TPair<FKey, int32>& Pair : Bindings)
	{
		FInputKeyBinding Binding{ FInputChord(Pair.Key), IE_Pressed };
		// Sin consumirla: la misma tecla sigue llegando al buggy o a la artillera.
		Binding.bConsumeInput = false;
		const int32 Delta = Pair.Value;
		Binding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, Delta]()
		{
			if (CameraDirector)
			{
				CameraDirector->CycleSpectate(Delta);
			}
		});
		InputComponent->KeyBindings.Add(MoveTemp(Binding));
	}
}

void ATN_RallyPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController())
	{
		return;
	}
	DashboardCheckAccumulator += DeltaTime;
	if (DashboardCheckAccumulator >= TNRallyPC::DashboardCheckSeconds)
	{
		DashboardCheckAccumulator = 0.f;
		// Solo para la conductora: a la artillera, sentada detrás, los paneles le taparían la vista (y ella tiene su HUD).
		ATN_Buggy* Driven = Cast<ATN_Buggy>(GetPawn());
		if (Driven)
		{
			UTN_RallyDashboardComponent::AttachTo(Driven, this);
		}
		else
		{
			UTN_RallyDashboardComponent::RemoveFrom(FindLocalBuggy());
		}
	}
}

ATN_Buggy* ATN_RallyPlayerController::FindLocalBuggy() const
{
	APawn* MyPawn = GetPawn();
	if (ATN_Buggy* Driven = Cast<ATN_Buggy>(MyPawn))
	{
		return Driven;
	}
	const ATN_BuggyGunnerPawn* Gunner = Cast<ATN_BuggyGunnerPawn>(MyPawn);
	return Gunner ? Gunner->GetBuggy() : nullptr;
}

void ATN_RallyPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	UProximityVoiceComponent::EnsureOn(InPawn);
}

void ATN_RallyPlayerController::SendVoiceToOwningClient(const TArray<uint8>& CompressedData, int32 SenderSampleRate,
	AActor* SpeakerActor, bool bIntercom)
{
	ClientReceiveVoice(CompressedData, SenderSampleRate, SpeakerActor, bIntercom);
}

void ATN_RallyPlayerController::ClientReceiveVoice_Implementation(const TArray<uint8>& CompressedData, int32 SenderSampleRate,
	AActor* SpeakerActor, bool bIntercom)
{
	if (UProximityVoiceComponent* Voice = SpeakerActor ? SpeakerActor->FindComponentByClass<UProximityVoiceComponent>() : nullptr)
	{
		Voice->PlayRemoteVoice(CompressedData, SenderSampleRate, bIntercom);
	}
}

void ATN_RallyPlayerController::AcknowledgePossession(APawn* InPawn)
{
	Super::AcknowledgePossession(InPawn);
	const UEnum* Roles = StaticEnum<ENetRole>();
	UE_LOG(LogTNRally, Log, TEXT("[RallyPC] %s posee %s (%s): rol local %s, remoto %s"), *GetNameSafe(this), *GetNameSafe(InPawn),
		*GetNameSafe(InPawn ? InPawn->GetClass() : nullptr),
		InPawn ? *Roles->GetNameStringByValue(InPawn->GetLocalRole()) : TEXT("-"),
		InPawn ? *Roles->GetNameStringByValue(InPawn->GetRemoteRole()) : TEXT("-"));
}

void ATN_RallyPlayerController::PawnLeavingGame()
{
	const ATN_RallyPlayerState* RallyPlayer = GetPlayerState<ATN_RallyPlayerState>();
	if (RallyPlayer && RallyPlayer->IsSeated())
	{
		UE_LOG(LogTNRally, Log, TEXT("[RallyPC] %s se desconecta sentada en el equipo %d: su peón %s queda para Logout."),
			*GetNameSafe(this), RallyPlayer->GetRallyTeamIndex(), *GetNameSafe(GetPawn()));
		return;
	}
	Super::PawnLeavingGame();
}

void ATN_RallyPlayerController::SyncCosmeticsToServer()
{
	const UMP_GameInstance* GameInstance = Cast<UMP_GameInstance>(GetGameInstance());
	if (!IsLocalController() || !GameInstance)
	{
		return;
	}
	ServerSyncCosmetics(TNCosmeticsSync::ReadLocalLoadout(*GameInstance));
}

#if !UE_BUILD_SHIPPING
void ATN_RallyPlayerController::DebugSendCosmetics(FName SkinId, FName ShellId, FName EyesId)
{
	const UMP_GameInstance* GameInstance = Cast<UMP_GameInstance>(GetGameInstance());
	if (!IsLocalController() || !GameInstance)
	{
		return;
	}
	FTNCosmeticLoadout Loadout = TNCosmeticsSync::ReadLocalLoadout(*GameInstance);
	for (const TPair<FName*, FName>& Override : { TPair<FName*, FName>(&Loadout.SkinId, SkinId),
		TPair<FName*, FName>(&Loadout.ShellId, ShellId), TPair<FName*, FName>(&Loadout.EyesId, EyesId) })
	{
		if (!Override.Value.IsNone())
		{
			*Override.Key = Override.Value;
			Loadout.UnlockedSkinIds.AddUnique(Override.Value);
		}
	}
	UE_LOG(LogTNRally, Log, TEXT("[RallyPC] %s manda aspecto de prueba: color=%s caparazón=%s ojos=%s"), *GetNameSafe(this),
		*Loadout.SkinId.ToString(), *Loadout.ShellId.ToString(), *Loadout.EyesId.ToString());
	ServerSyncCosmetics(Loadout);
}

void ATN_RallyPlayerController::DebugSendBuggy(FName ModelId, FName PaintId)
{
	const UMP_GameInstance* GameInstance = Cast<UMP_GameInstance>(GetGameInstance());
	if (!IsLocalController() || !GameInstance)
	{
		return;
	}
	FTNCosmeticLoadout Loadout = TNCosmeticsSync::ReadLocalLoadout(*GameInstance);
	Loadout.BuggyLook.ModelId = ModelId;
	Loadout.BuggyLook.PaintId = PaintId;
	for (const FName Id : { ModelId, PaintId })
	{
		if (!Id.IsNone()) { Loadout.UnlockedBuggyIds.AddUnique(Id); }
	}
	UE_LOG(LogTNRally, Log, TEXT("[RallyPC] %s manda buggy de prueba: modelo=%s pintura=%s"), *GetNameSafe(this), *ModelId.ToString(),
		*PaintId.ToString());
	ServerSyncCosmetics(Loadout);
}

static FAutoConsoleCommandWithWorldAndArgs GTNRallyDebugBuggyCommand(
	TEXT("TN.Rally.DebugBuggy"),
	TEXT("Rally: TN.Rally.DebugBuggy <modelo|-> [pintura|-] [espera]: la jugadora local manda ese buggy al servidor (BuggyModel_Caiman, BuggyModel_Laud, BuggyPaint_Lava...; - = el de serie; no toca el save)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		const auto Arg = [&Args](int32 Index) { return Args.IsValidIndex(Index) && Args[Index] != TEXT("-") ? FName(*Args[Index]) : NAME_None; };
		const FName Model = Arg(0);
		const FName Paint = Arg(1);
		const float Wait = Args.IsValidIndex(2) ? FCString::Atof(*Args[2]) : 0.f;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Model, Paint](float)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				ATN_RallyPlayerController* PC = World && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
					? Cast<ATN_RallyPlayerController>(World->GetFirstPlayerController()) : nullptr;
				if (PC)
				{
					PC->DebugSendBuggy(Model, Paint);
					return false;
				}
			}
			UE_LOG(LogTNRally, Warning, TEXT("TN.Rally.DebugBuggy: no hay un PlayerController del Rally"));
			return false;
		}), FMath::Max(Wait, 0.01f));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTNRallyDebugCosmeticsCommand(
	TEXT("TN.Rally.DebugCosmetics"),
	TEXT("Rally: TN.Rally.DebugCosmetics <color> [caparazón] [ojos] [espera]: la jugadora local manda ese aspecto al servidor (filas de DT_Skins; no toca el save)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		const auto Arg = [&Args](int32 Index) { return Args.IsValidIndex(Index) && Args[Index] != TEXT("-") ? FName(*Args[Index]) : NAME_None; };
		const FName Skin = Arg(0);
		const FName Shell = Arg(1);
		const FName Eyes = Arg(2);
		const float Wait = Args.IsValidIndex(3) ? FCString::Atof(*Args[3]) : 0.f;
		// Con el ticker del motor: en un cliente, -ExecCmds corre antes de llegar al mapa del servidor.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Skin, Shell, Eyes](float)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				ATN_RallyPlayerController* PC = World && Context.WorldType == EWorldType::Game
					? Cast<ATN_RallyPlayerController>(World->GetFirstPlayerController()) : nullptr;
				if (PC)
				{
					PC->DebugSendCosmetics(Skin, Shell, Eyes);
					return false;
				}
			}
			UE_LOG(LogTNRally, Warning, TEXT("TN.Rally.DebugCosmetics: no hay un PlayerController del Rally"));
			return false;
		}), FMath::Max(Wait, 0.01f));
	}));
#endif

bool ATN_RallyPlayerController::ServerSyncCosmetics_Validate(const FTNCosmeticLoadout& Loadout)
{
	return TNCosmeticsSync::IsLoadoutWithinRpcCaps(Loadout);
}

void ATN_RallyPlayerController::ServerSyncCosmetics_Implementation(const FTNCosmeticLoadout& Loadout)
{
	ATN_CoopPlayerState* CoopState = GetPlayerState<ATN_CoopPlayerState>();
	if (!CoopState)
	{
		UE_LOG(LogTNRally, Warning, TEXT("[RallyPC] %s manda cosméticos sin PlayerState: se ignoran."), *GetNameSafe(this));
		return;
	}
	const int32 Applied = TNCosmeticsSync::ApplyLoadoutOnServer(Cast<UMP_GameInstance>(GetGameInstance()), *CoopState, Loadout);
	UE_LOG(LogTNRally, Log, TEXT("[RallyPC] cosméticos de %s: casco=%s color=%s caparazón=%s ojos=%s buggy=%s|%s (%d/5 aplicados)"),
		*CoopState->GetPlayerName(), *CoopState->EquippedHelmetId.ToString(), *CoopState->EquippedSkinId.ToString(),
		*CoopState->EquippedShellId.ToString(), *CoopState->EquippedEyesId.ToString(), *CoopState->EquippedBuggyLook.ModelId.ToString(),
		*CoopState->EquippedBuggyLook.PaintId.ToString(), Applied);
}
