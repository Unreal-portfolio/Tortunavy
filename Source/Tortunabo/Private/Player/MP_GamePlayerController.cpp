#include "Player/MP_GamePlayerController.h"
#include "Settings/TN_GameplayAssetSettings.h"
#include "Core/TN_Log.h"
#include "Blueprint/UserWidget.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Voice/ProximityVoiceComponent.h"
#include "UI/HUD/TN_CoopFlowHUDWidget.h"
#include "UI/HUD/TN_RunHUDWidget.h"
#include "UI/Shop/TN_ShopWidgets.h"
#include "UI/Briefing/TN_BriefingWidget.h"
#include "Lobby/TN_ChangingBooth.h"
#include "Lobby/TN_ShopKeeper.h"
#include "Audio/TN_AmbientSoundscape.h"
#include "World/ProcMap/TN_PathStorm.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "World/TN_ScorePickup.h"
#include "World/TN_ScoreShells.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UI/HUD/TN_RadialWheelWidgetBase.h"
#include "UI/HUD/TN_EmoteWheelDataAsset.h"
#include "UI/HUD/TN_QuickChatWheelDataAsset.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Multiplayer/TN_RoomInfo.h"
#include "Core/TN_CoopGameState.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_MatchFlowTypes.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_Ghost.h"
#include "Player/TN_DebugRpcDecisions.h"
#include "TN_GhostInternal.h"
#include "Game/TN_ProcMapGameMode.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "VR/TN_VRMode.h"
#include "Multiplayer/TN_LocalPlaySubsystem.h"
#include "Engine/LocalPlayer.h"

namespace
{
	// Pila de capas del HUD (ZOrder de AddToViewport): HUD < CoopFlow < Voice < ruedas < menús.
	constexpr int32 MPGamePlayerController_ZOrderPlayerHUD = 4;
	constexpr int32 MPGamePlayerController_ZOrderCoopFlow = 5;
	constexpr int32 MPGamePlayerController_ZOrderVoiceIndicator = 10;
	constexpr int32 MPGamePlayerController_ZOrderEmoteWheel = 30;
	constexpr int32 MPGamePlayerController_ZOrderQuickChatWheel = 31;
	constexpr int32 MPGamePlayerController_ZOrderCosmetics = 40;

	// Frecuencia del timer que recalcula la opción apuntada en la rueda radial.
	constexpr float MPGamePlayerController_RadialWheelUpdateHz = 60.f;
}

AMP_GamePlayerController::AMP_GamePlayerController()
{
	// Widget classes are assigned via EditDefaultsOnly in a BP derived class (e.g. BP_GamePlayerController).
	// No hardcoded defaults — set CoopFlowWidgetClass, VoiceIndicatorWidgetClass and CosmeticsWidgetClass
	// in the BP CDO so they can live at any content path.
	OpenEmoteWheelAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/Blueprints/Gameplay/Controls/IA_OpenEmoteWheel.IA_OpenEmoteWheel")));
	OpenQuickChatWheelAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/Blueprints/Gameplay/Controls/IA_OpenChatWheel.IA_OpenChatWheel")));
	RadialNavigateAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/Blueprints/Gameplay/Controls/IA_RadialNavigate.IA_RadialNavigate")));

	AmbientSoundscape = CreateDefaultSubobject<UTN_AmbientSoundscapeComponent>(TEXT("AmbientSoundscape"));
}

void AMP_GamePlayerController::BeginPlay()
{
	Super::BeginPlay();


	ApplyGameplayInputMode();

	if (IsLocalController() && GetPawn())
	{
		CreateVoiceHUD();
	}

	if (IsLocalController())
	{
		CacheRadialInputAssets();
		CreateCoopFlowHUD();
		CreatePlayerHUD();
		CreateRadialWidgets();
		SyncCosmeticsToServer();
	}
}

void AMP_GamePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AMP_GamePlayerController::SpectateNextPlayer);
		InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AMP_GamePlayerController::SpectatePreviousPlayer);
		InputComponent->BindKey(EKeys::PageDown, IE_Pressed, this, &AMP_GamePlayerController::SpectateNextPlayer);
		InputComponent->BindKey(EKeys::PageUp, IE_Pressed, this, &AMP_GamePlayerController::SpectatePreviousPlayer);
	}

	CacheRadialInputAssets();

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (LoadedOpenEmoteWheelAction)
		{
			EnhancedInput->BindAction(LoadedOpenEmoteWheelAction, ETriggerEvent::Started, this, &AMP_GamePlayerController::OnOpenEmoteWheelStarted);
			EnhancedInput->BindAction(LoadedOpenEmoteWheelAction, ETriggerEvent::Completed, this, &AMP_GamePlayerController::OnOpenEmoteWheelReleased);
			EnhancedInput->BindAction(LoadedOpenEmoteWheelAction, ETriggerEvent::Canceled, this, &AMP_GamePlayerController::OnOpenEmoteWheelReleased);
		}

		if (LoadedOpenQuickChatWheelAction)
		{
			EnhancedInput->BindAction(LoadedOpenQuickChatWheelAction, ETriggerEvent::Started, this, &AMP_GamePlayerController::OnOpenQuickChatWheelStarted);
			EnhancedInput->BindAction(LoadedOpenQuickChatWheelAction, ETriggerEvent::Completed, this, &AMP_GamePlayerController::OnOpenQuickChatWheelReleased);
			EnhancedInput->BindAction(LoadedOpenQuickChatWheelAction, ETriggerEvent::Canceled, this, &AMP_GamePlayerController::OnOpenQuickChatWheelReleased);
		}

		if (LoadedRadialNavigateAction)
		{
			EnhancedInput->BindAction(LoadedRadialNavigateAction, ETriggerEvent::Triggered, this, &AMP_GamePlayerController::OnRadialNavigateTriggered);
			EnhancedInput->BindAction(LoadedRadialNavigateAction, ETriggerEvent::Completed, this, &AMP_GamePlayerController::OnRadialNavigateCompleted);
			EnhancedInput->BindAction(LoadedRadialNavigateAction, ETriggerEvent::Canceled, this, &AMP_GamePlayerController::OnRadialNavigateCompleted);
		}

		if (LoadedReturnToMenuAction)
		{
			EnhancedInput->BindAction(LoadedReturnToMenuAction, ETriggerEvent::Started, this, &AMP_GamePlayerController::OnReturnToMenuPressed);
		}
	}
}

UMP_GameInstance* AMP_GamePlayerController::GetTNGameInstance() const
{
	return Cast<UMP_GameInstance>(GetGameInstance());
}

void AMP_GamePlayerController::OnReturnToMenuPressed()
{
	// Partida local: solo el jugador 1 cierra la partida (un invitado sale con B en el lobby o desde su pausa).
	if (UTN_LocalPlaySubsystem::IsGuest(this))
	{
		return;
	}
	// Un cliente remoto abandona la partida individualmente: destruye su propio
	// registro de sesión y viaja a su menú, sin pedir al servidor que cierre la
	// sesión de todos. Solo el host termina la partida para el resto.
	if (GetNetMode() == NM_Client)
	{
		if (UMP_GameInstance* GI = GetTNGameInstance())
		{
			GI->HandleReturnToMenu();
		}
		return;
	}

	ServerRequestReturnToMenu();
}

void AMP_GamePlayerController::ServerRequestReturnToMenu_Implementation()
{
	// Guard server-side: solo el PlayerController del host (local en el listen-server)
	// puede cerrar la sesión para todos. Sin esto, cualquier cliente remoto podía
	// invocar este RPC y expulsar a toda la partida al menú (grief).
	if (!IsLocalController())
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[MP] ServerRequestReturnToMenu rechazado: '%s' no es el host."),
			*GetNameSafe(this));
		return;
	}

	if (UMP_GameInstance* GI = GetTNGameInstance())
	{
		GI->HandleReturnToMenu();
	}
}

void AMP_GamePlayerController::ClientWasKicked_Implementation(const FText& KickReason)
{
	Super::ClientWasKicked_Implementation(KickReason);

	UE_LOG(LogTortunabo, Log, TEXT("[Salas] El servidor me ha expulsado: %s"), *KickReason.ToString());
	// Mismo camino que la expulsión por ATN_RoomInfo: aviso con el nombre de la sala y al menú. Deja bKickedFromRoom
	// puesto, así que el corte de conexión que viene detrás no enseña «el anfitrión se ha ido» ni reconecta. Si el
	// aviso ya había llegado por la réplica, HandleKickedFromRoom no hace nada.
	if (UMP_GameInstance* GI = GetTNGameInstance())
	{
		const ATN_RoomInfo* Info = ATN_RoomInfo::Find(GetWorld());
		GI->HandleKickedFromRoom(Info ? Info->GetRoomNameId() : INDEX_NONE);
	}
}

void AMP_GamePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Vuelve a tener tortuga: su fantasma espectador se desvanece (Docs/Fantasma_Espectador.md).
	TNGhostInternal::OnPossess(this, InPawn);

	ApplyGameplayInputMode();

	if (!InPawn)
	{
		return;
	}

	// Partida local (#311): sin chat de voz (están en la misma sala) ni micrófono abierto.
	UProximityVoiceComponent* ExistingVoice = InPawn->FindComponentByClass<UProximityVoiceComponent>();
	if (!ExistingVoice && !UTN_LocalPlaySubsystem::IsLocalGame(this))
	{
		UProximityVoiceComponent* VoiceComp = NewObject<UProximityVoiceComponent>(InPawn, TEXT("ProximityVoice"));
		if (VoiceComp)
		{
			VoiceComp->RegisterComponent();
		}
	}

	// Aplicar cosméticos al pawn recién poseído (server-side, para TODOS los jugadores).
	// El listen-server no recibe OnRep de su propio PlayerState → aplica directamente.
	// Para jugadores remotos, esto asegura que el pawn del servidor tenga el visual correcto.
	if (ATN_CoopPlayerState* TNPS = GetPlayerState<ATN_CoopPlayerState>())
	{
		if (ATortugaCharacter* TurtleChar = Cast<ATortugaCharacter>(InPawn))
		{
			TurtleChar->UpdateHelmetMesh(TNPS->EquippedHelmetId);
			TurtleChar->UpdateSkinVisual(TNPS->EquippedSkinId);
		}
	}

	if (IsLocalController())
	{
		CreateVoiceHUD();
		CreateCoopFlowHUD();
		CreatePlayerHUD();
		CreateRadialWidgets();
		SyncCosmeticsToServer();
	}
}

void AMP_GamePlayerController::ApplyGameplayInputMode()
{
	if (!IsLocalController())
	{
		return;
	}

	// ResetIgnoreInputFlags() pone los contadores a 0 en lugar de solo decrementar.
	// Esto es crítico en builds empaquetados donde el contador puede estar > 0
	// por alguna llamada previa de la inicialización del engine antes de BeginPlay.
	ResetIgnoreInputFlags();

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	SetShowMouseCursor(false);

	if (GEngine && GEngine->GameViewport)
	{
		// Con la pantalla partida, solo el foco de este jugador: los menús de los demás (tienda, probador) siguen con el suyo.
		const ULocalPlayer* LocalPlayer = GetLocalPlayer();
		if (LocalPlayer && UTN_LocalPlaySubsystem::IsLocalGame(this))
		{
			FSlateApplication::Get().SetUserFocusToGameViewport(LocalPlayer->GetControllerId(), EFocusCause::SetDirectly);
		}
		else
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport(EFocusCause::SetDirectly);
		}
	}
}

void AMP_GamePlayerController::ApplyRadialInputMode()
{
	if (!IsLocalController())
	{
		return;
	}

	// Un invitado de la partida local elige con el stick: el ratón es del jugador 1.
	if (UTN_LocalPlaySubsystem::IsGuest(this))
	{
		SetIgnoreLookInput(true);
		return;
	}
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
	SetIgnoreLookInput(true);
}

void AMP_GamePlayerController::RestorePostRadialInputMode()
{
	ApplyGameplayInputMode();
}

void AMP_GamePlayerController::ForceRestoreInput()
{
	ResetIgnoreInputFlags();
	ApplyGameplayInputMode();
}

void AMP_GamePlayerController::ServerReportProcMapReady_Implementation(int32 Generation)
{
	if (ATN_ProcMapGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ATN_ProcMapGameMode>() : nullptr)
	{
		GM->NotifyClientMapReady(this, Generation);
	}
}

void AMP_GamePlayerController::ClientReceiveVoice_Implementation(const TArray<uint8>& CompressedData, int32 SenderSampleRate, AActor* SpeakerActor)
{
	if (!SpeakerActor)
	{
		return;
	}

	if (UProximityVoiceComponent* VoiceComp = SpeakerActor->FindComponentByClass<UProximityVoiceComponent>())
	{
		VoiceComp->PlayRemoteVoice(CompressedData, SenderSampleRate);
	}
}

void AMP_GamePlayerController::ClientRestorePlayerInput_Implementation()
{
	// ResetIgnoreInputFlags() establece IgnoreMoveInput = 0 e IgnoreLookInput = 0.
	// Esto limpia cualquier contador incremental dejado por ChangeState(Spectating)
	// / StartSpectatingOnly, que incrementa IgnoreMoveInput y no lo decrementa
	// cuando el servidor llama Possess + ClientRestart.
	ForceRestoreInput();

	UE_LOG(LogTortunabo, Log, TEXT("[PC] ClientRestorePlayerInput: input flags reset, gameplay mode restored."));
}

void AMP_GamePlayerController::CacheRadialInputAssets()
{
	if (!LoadedOpenEmoteWheelAction && !OpenEmoteWheelAction.IsNull())
	{
		LoadedOpenEmoteWheelAction = OpenEmoteWheelAction.LoadSynchronous();
	}

	if (!LoadedOpenQuickChatWheelAction && !OpenQuickChatWheelAction.IsNull())
	{
		LoadedOpenQuickChatWheelAction = OpenQuickChatWheelAction.LoadSynchronous();
	}

	if (!LoadedRadialNavigateAction && !RadialNavigateAction.IsNull())
	{
		LoadedRadialNavigateAction = RadialNavigateAction.LoadSynchronous();
	}

	if (!LoadedReturnToMenuAction && !ReturnToMenuAction.IsNull())
	{
		LoadedReturnToMenuAction = ReturnToMenuAction.LoadSynchronous();
	}
}

void AMP_GamePlayerController::EnterSpectateMode()
{
	// Cerrar cualquier rueda radial o menú de cosméticos para evitar que queden
	// visibles con cursor en pantalla durante el modo espectador, lo que bloquea
	// el scroll de cambio de cámara y deja FInputModeGameAndUI activo.
	if (ActiveWheelType != ETN_RadialWheelType::None)
	{
		CloseRadialWheel(false);
	}
	if (CosmeticsWidget && CosmeticsWidget->GetVisibility() == ESlateVisibility::Visible)
	{
		CosmeticsWidget->SetVisibility(ESlateVisibility::Hidden);
		ApplyGameplayInputMode();
	}

	// Fantasma espectador (Docs/Fantasma_Espectador.md): el servidor crea el de este jugador y apunta la tortuga que deja.
	TNGhostInternal::OnEnterSpectate(this);

	// ── Cambiar estado en el servidor ─────────────────────────────────────────
	ChangeState(NAME_Spectating);
	StartSpectatingOnly();

	// ── Notificar al CLIENTE que entre en modo espectador ─────────────────────
	// ChangeState/StartSpectatingOnly solo afectan al servidor.
	// Sin esta llamada, el cliente mantiene el input activo, su cámara pegada
	// al pawn (ahora oculto) y puede seguir "caminando" por predicción local.
	// ClientGotoState envía un RPC fiable al cliente que llama ChangeState(Spectating)
	// localmente → BeginSpectatingState() → IgnoreMoveInput = MAX_uint8.
	// Para el listen-server, los Client RPCs también se ejecutan localmente.
	ClientGotoState(NAME_Spectating);

	SpectateNextPlayer();
}

void AMP_GamePlayerController::SpectateNextPlayer()
{
	SpectateByDirection(1);
}

void AMP_GamePlayerController::SpectatePreviousPlayer()
{
	SpectateByDirection(-1);
}

void AMP_GamePlayerController::SpectateByDirection(int32 Direction)
{
	if (Direction == 0 || !GetWorld() || !PlayerState)
	{
		return;
	}

	// Solo permitir espectear si el jugador local terminó, murió o fue eliminado (o es un fantasma, en cualquier modo).
	// Evita que la rueda del ratón cambie la cámara mientras se está jugando.
	const ATN_CoopPlayerState* LocalPS = GetPlayerState<ATN_CoopPlayerState>();
	if (!LocalPS || (LocalPS->IsAliveAndPlaying() && !LocalPS->bHasFinishedRun && !TNGhost::IsGhost(this)))
	{
		return;
	}

	// GameState puede ser null durante travel/teardown (World existe pero el GS aún
	// no ha replicado/spawneado). Sin este guard, la rueda del ratón al espectar en
	// esa ventana crashea al iterar PlayerArray.
	AGameStateBase* GS = GetWorld()->GetGameState();
	if (!GS) { return; }

	TArray<APlayerState*> Candidates = BuildSpectateCandidates(GS);

	if (Candidates.Num() == 0)
	{
		// No hay jugadores vivos para espectear. Apuntar al propio pawn (oculto pero
		// válido) para evitar que el ViewTarget quede apuntando a un actor destruido
		// o null, lo que causaría pantalla negra hasta que aparezca la pantalla de resultados.
		if (APawn* OwnPawn = GetPawn())
		{
			SetViewTargetWithBlend(OwnPawn, 0.f);
		}
		return;
	}

	Candidates.Sort([](const APlayerState& A, const APlayerState& B)
	{
		return A.GetPlayerId() < B.GetPlayerId();
	});

	int32 CurrentIndex = INDEX_NONE;
	AActor* CurrentViewTarget = GetViewTarget();
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		if (Candidates[i]->GetPawn() == CurrentViewTarget)
		{
			CurrentIndex = i;
			break;
		}
	}

	int32 NextIndex = 0;
	if (CurrentIndex != INDEX_NONE)
	{
		NextIndex = (CurrentIndex + Direction + Candidates.Num()) % Candidates.Num();
	}
	else if (Direction < 0)
	{
		NextIndex = Candidates.Num() - 1;
	}

	// En VR, corte seco: un fundido de cámara con gafas marea.
	SetViewTargetWithBlend(Candidates[NextIndex]->GetPawn(), TNVR::ViewBlendTime(0.25f));
}

TArray<APlayerState*> AMP_GamePlayerController::BuildSpectateCandidates(AGameStateBase* GS) const
{
	TArray<APlayerState*> Candidates;
	for (APlayerState* PS : GS->PlayerArray)
	{
		ATN_CoopPlayerState* CoopPS = Cast<ATN_CoopPlayerState>(PS);
		// Skip self
		if (!CoopPS || CoopPS == Cast<ATN_CoopPlayerState>(PlayerState))
		{
			continue;
		}
		// Skip players without a live pawn
		if (!CoopPS->GetPawn())
		{
			continue;
		}
		// Skip eliminated/dead/finished players.
		// Finished players (bHasFinishedRun=true) have their pawn hidden via
		// SetActorHiddenInGame(true) in MarkPlayerFinished → spectating them
		// results in a black/invisible screen.
		if (!CoopPS->IsAliveAndPlaying() || CoopPS->bHasFinishedRun)
		{
			continue;
		}
		Candidates.Add(CoopPS);
	}
	return Candidates;
}

void AMP_GamePlayerController::RefreshHUDAfterPossession()
{
	if (!IsLocalController())
	{
		return;
	}

	// Re-añadir todos los widgets al viewport.
	// Necesario después de seamless travel: UWorld::CleanupWorld elimina todos los
	// widgets del viewport, pero el PC persiste y los widgets siguen vivos en memoria.
	// Sin esto, los widgets existen (pointer no nulo) pero no son visibles.
	CreateVoiceHUD();
	CreateCoopFlowHUD();
	CreatePlayerHUD();
	CreateRadialWidgets();

	// Re-sincronizar cosméticos al servidor tras seamless travel.
	// Después del viaje, ni BeginPlay ni OnPossess se ejecutan en el cliente (el PC
	// persiste). Este es el único hook del cliente post-travel para re-enviar los
	// cosméticos locales al servidor, cubriendo cualquier caso donde el PlayerState
	// haya perdido los valores durante la transición.
	SyncCosmeticsToServer();
}

void AMP_GamePlayerController::CreateVoiceHUD()
{
	// El HUD hecho en código (UTN_RunHUDWidget) ya dice cuándo habla la tortuga: la cara del distintivo rebota y
	// sale un bocadillo. El indicador suelto solo hace falta con el HUD de Blueprint.
	if (!IsLocalController() || !VoiceIndicatorWidgetClass || bUseCodeHUD || !bShowHUD)
	{
		return;
	}

	// Crear el widget solo si no existe.
	if (!VoiceIndicatorWidget)
	{
		VoiceIndicatorWidget = CreateWidget<UUserWidget>(this, VoiceIndicatorWidgetClass);
	}

	// Re-añadir al viewport si fue eliminado durante seamless travel.
	// AddToViewport es idempotente: no-op si el widget ya está en el viewport.
	if (VoiceIndicatorWidget && !TNVR::IsOnScreen(VoiceIndicatorWidget))
	{
		TNVR::AddToScreen(VoiceIndicatorWidget, MPGamePlayerController_ZOrderVoiceIndicator);
	}
}

void AMP_GamePlayerController::CreateCoopFlowHUD()
{
	if (!IsLocalController() || !bShowHUD)
	{
		return;
	}

	// Crear el widget solo si no existe.
	if (!CoopFlowWidget)
	{
		UClass* WidgetClass = bUseCodeHUD
			? UTN_RunFlowHUDWidget::StaticClass()
			: (CoopFlowWidgetClass ? CoopFlowWidgetClass.Get() : UTN_CoopFlowHUDWidget::StaticClass());

		if (!CoopFlowWidgetClass && !bUseCodeHUD)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[HUD] CoopFlowWidgetClass no asignado en %s. Asignalo en el BP derivado del PlayerController. Usando clase C++ como fallback."), *GetNameSafe(this));
		}

		CoopFlowWidget = CreateWidget<UUserWidget>(this, WidgetClass);
	}

	// Re-añadir al viewport si fue eliminado durante seamless travel.
	if (CoopFlowWidget && !TNVR::IsOnScreen(CoopFlowWidget))
	{
		TNVR::AddToScreen(CoopFlowWidget, MPGamePlayerController_ZOrderCoopFlow);
	}
}

void AMP_GamePlayerController::CreatePlayerHUD()
{
	if (!IsLocalController() || !bShowHUD)
	{
		return;
	}

	// Crear el widget solo si no existe.
	if (!PlayerHUDWidget)
	{
		UClass* HudClass = bUseCodeHUD ? UTN_RunHUDWidget::StaticClass() : PlayerHUDWidgetClass.Get();
		if (!HudClass)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[HUD] PlayerHUDWidgetClass no asignado en %s. Asignalo en BP_GamePlayerController → Class Defaults."), *GetNameSafe(this));
			return;
		}

		PlayerHUDWidget = CreateWidget<UUserWidget>(this, HudClass);
	}

	// Re-añadir al viewport si fue eliminado durante seamless travel.
	// Tras travel, OnPossess llama a esta función. El widget existe (pointer no nulo)
	// pero fue eliminado del viewport por UWorld::CleanupWorld → RemoveAllViewportWidgets.
	if (PlayerHUDWidget && !TNVR::IsOnScreen(PlayerHUDWidget))
	{
		TNVR::AddToScreen(PlayerHUDWidget, MPGamePlayerController_ZOrderPlayerHUD);
		UE_LOG(LogTortunabo, Log, TEXT("[HUD] PlayerHUDWidget re-añadido al viewport (tras seamless travel)"));
	}
}

void AMP_GamePlayerController::CreateRadialWidgets()
{
	if (!IsLocalController() || !bShowHUD)
	{
		return;
	}

	// Con el HUD en código, las ruedas también lo son (estilo Tortunavy, centradas donde se mide el ratón).
	if (!EmoteWheelWidget && (bUseCodeHUD || EmoteWheelWidgetClass))
	{
		EmoteWheelWidget = CreateWidget<UTN_RadialWheelWidgetBase>(this, bUseCodeHUD ? UTN_RunRadialWheelWidget::StaticClass() : EmoteWheelWidgetClass.Get());
		if (UTN_RunRadialWheelWidget* CodeWheel = Cast<UTN_RunRadialWheelWidget>(EmoteWheelWidget))
		{
			CodeWheel->SetTitle(NSLOCTEXT("TNHUD", "EmoteWheelTitle", "EMOTES"));
		}
	}
	if (EmoteWheelWidget && !TNVR::IsOnScreen(EmoteWheelWidget))
	{
		TNVR::AddToScreen(EmoteWheelWidget, MPGamePlayerController_ZOrderEmoteWheel);
		EmoteWheelWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (!QuickChatWheelWidget && (bUseCodeHUD || QuickChatWheelWidgetClass))
	{
		QuickChatWheelWidget = CreateWidget<UTN_RadialWheelWidgetBase>(this, bUseCodeHUD ? UTN_RunRadialWheelWidget::StaticClass() : QuickChatWheelWidgetClass.Get());
		if (UTN_RunRadialWheelWidget* CodeWheel = Cast<UTN_RunRadialWheelWidget>(QuickChatWheelWidget))
		{
			CodeWheel->SetTitle(NSLOCTEXT("TNHUD", "ChatWheelTitle", "FRASES"));
		}
	}
	if (QuickChatWheelWidget && !TNVR::IsOnScreen(QuickChatWheelWidget))
	{
		TNVR::AddToScreen(QuickChatWheelWidget, MPGamePlayerController_ZOrderQuickChatWheel);
		QuickChatWheelWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void AMP_GamePlayerController::OnOpenEmoteWheelStarted()
{
	OpenRadialWheel(ETN_RadialWheelType::Emote);
}

void AMP_GamePlayerController::OnOpenEmoteWheelReleased()
{
	if (ActiveWheelType == ETN_RadialWheelType::Emote)
	{
		CloseRadialWheel(true);
	}
}

void AMP_GamePlayerController::OnOpenQuickChatWheelStarted()
{
	OpenRadialWheel(ETN_RadialWheelType::QuickChat);
}

void AMP_GamePlayerController::OnOpenQuickChatWheelReleased()
{
	if (ActiveWheelType == ETN_RadialWheelType::QuickChat)
	{
		CloseRadialWheel(true);
	}
}

void AMP_GamePlayerController::OnRadialNavigateTriggered(const FInputActionValue& Value)
{
	CachedStickVector = Value.Get<FVector2D>().GetClampedToMaxSize(1.f);
	LastStickInputRealTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
}

void AMP_GamePlayerController::OnRadialNavigateCompleted(const FInputActionValue& Value)
{
	CachedStickVector = FVector2D::ZeroVector;
}

UTN_RadialWheelWidgetBase* AMP_GamePlayerController::GetActiveWheelWidget() const
{
	switch (ActiveWheelType)
	{
	case ETN_RadialWheelType::Emote:
		return EmoteWheelWidget;
	case ETN_RadialWheelType::QuickChat:
		return QuickChatWheelWidget;
	default:
		return nullptr;
	}
}

void AMP_GamePlayerController::OpenRadialWheel(ETN_RadialWheelType WheelType)
{
	if (!IsLocalController() || ActiveWheelType != ETN_RadialWheelType::None)
	{
		return;
	}

	CreateRadialWidgets();

	UTN_RadialWheelWidgetBase* Widget = nullptr;
	TArray<FTN_RadialWheelEntryView> Entries;

	if (WheelType == ETN_RadialWheelType::Emote)
	{
		Widget = EmoteWheelWidget;
		if (EmoteWheelDataAsset)
		{
			Entries = EmoteWheelDataAsset->BuildWheelEntries();
		}
	}
	else if (WheelType == ETN_RadialWheelType::QuickChat)
	{
		Widget = QuickChatWheelWidget;
		if (QuickChatWheelDataAsset)
		{
			Entries = QuickChatWheelDataAsset->BuildWheelEntries();
		}
	}

	if (!Widget || Entries.Num() == 0)
	{
		return;
	}

	ActiveWheelType = WheelType;
	Widget->SetEntries(Entries);
	Widget->SetVisibility(ESlateVisibility::Visible);

	float MouseX = 0.f;
	float MouseY = 0.f;
	const bool bGuest = UTN_LocalPlaySubsystem::IsGuest(this);
	bHadMousePositionBeforeWheel = !bGuest && GetMousePosition(MouseX, MouseY);
	if (bHadMousePositionBeforeWheel)
	{
		CachedMousePositionBeforeWheel = FVector2D(MouseX, MouseY);
	}

	// El ratón, al centro de la rueda (el centro de la vista de este jugador: con la pantalla partida, su trozo).
	if (!bGuest)
	{
		const FVector2D Center = GetWheelCenter();
		SetMouseLocation(FMath::RoundToInt(Center.X), FMath::RoundToInt(Center.Y));
	}

	CachedStickVector = FVector2D::ZeroVector;
	ApplyRadialInputMode();

	if (GetWorld())
	{
		GetWorldTimerManager().SetTimer(RadialWheelUpdateTimerHandle, this, &AMP_GamePlayerController::UpdateRadialWheelInput, 1.f / MPGamePlayerController_RadialWheelUpdateHz, true);
	}
}

void AMP_GamePlayerController::CloseRadialWheel(bool bConfirmSelection)
{
	UTN_RadialWheelWidgetBase* Widget = GetActiveWheelWidget();
	if (!Widget)
	{
		ActiveWheelType = ETN_RadialWheelType::None;
		return;
	}

	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(RadialWheelUpdateTimerHandle);
	}

	if (bConfirmSelection)
	{
		FTN_RadialWheelEntryView SelectedEntry;
		if (Widget->TryConfirmSelection(SelectedEntry))
		{
			if (ActiveWheelType == ETN_RadialWheelType::Emote)
			{
				RequestPlayEmoteById(SelectedEntry.EntryId);
			}
			else if (ActiveWheelType == ETN_RadialWheelType::QuickChat)
			{
				SendQuickChat(SelectedEntry.EntryId);
			}
		}
	}

	Widget->ClearSelection();
	Widget->SetVisibility(ESlateVisibility::Collapsed);
	ActiveWheelType = ETN_RadialWheelType::None;

	if (bHadMousePositionBeforeWheel)
	{
		SetMouseLocation(FMath::RoundToInt(CachedMousePositionBeforeWheel.X), FMath::RoundToInt(CachedMousePositionBeforeWheel.Y));
	}

	RestorePostRadialInputMode();
}

FVector2D AMP_GamePlayerController::ComputeMouseWheelVector() const
{
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return FVector2D::ZeroVector;
	}

	int32 ViewX = 0;
	int32 ViewY = 0;
	GetViewportSize(ViewX, ViewY);
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const FVector2D Size = LocalPlayer ? FVector2D(ViewX * LocalPlayer->Size.X, ViewY * LocalPlayer->Size.Y) : FVector2D(ViewX, ViewY);

	const FVector2D Center = GetWheelCenter();
	const FVector2D Delta(MouseX - Center.X, MouseY - Center.Y);
	const float Radius = FMath::Max(1.f, static_cast<float>(FMath::Min(Size.X, Size.Y)) * 0.25f);
	return FVector2D(Delta.X / Radius, -Delta.Y / Radius).GetClampedToMaxSize(1.f);
}

FVector2D AMP_GamePlayerController::GetWheelCenter() const
{
	int32 ViewX = 0;
	int32 ViewY = 0;
	GetViewportSize(ViewX, ViewY);
	// El centro de la vista de este jugador (sin pantalla partida, el de la pantalla).
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const FVector2D Origin = LocalPlayer ? FVector2D(LocalPlayer->Origin) : FVector2D::ZeroVector;
	const FVector2D Size = LocalPlayer ? FVector2D(LocalPlayer->Size) : FVector2D(1.0, 1.0);
	return FVector2D(ViewX * (Origin.X + Size.X * 0.5), ViewY * (Origin.Y + Size.Y * 0.5));
}

FVector2D AMP_GamePlayerController::ResolveCurrentWheelVector() const
{
	const float Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
	const bool bUseStick = CachedStickVector.SizeSquared() > 0.04f && (Now - LastStickInputRealTime) <= 0.2f;
	// Un invitado de la partida local, siempre con el stick (el ratón es del jugador 1).
	if (bUseStick || UTN_LocalPlaySubsystem::IsGuest(this))
	{
		return (Now - LastStickInputRealTime) <= 0.2f ? CachedStickVector : FVector2D::ZeroVector;
	}
	return ComputeMouseWheelVector();
}

void AMP_GamePlayerController::TNWheel(int32 Type, float X, float Y)
{
	if (Type < 0)
	{
		CloseRadialWheel(false);
		return;
	}
	if (ActiveWheelType == ETN_RadialWheelType::None)
	{
		OpenRadialWheel(Type == 0 ? ETN_RadialWheelType::Emote : ETN_RadialWheelType::QuickChat);
	}
	// Sin el temporizador del ratón, que machacaría la dirección pedida.
	GetWorldTimerManager().ClearTimer(RadialWheelUpdateTimerHandle);
	if (UTN_RadialWheelWidgetBase* Widget = GetActiveWheelWidget())
	{
		Widget->UpdateInputVector(FVector2D(X, Y));
	}
}

void AMP_GamePlayerController::UpdateRadialWheelInput()
{
	if (UTN_RadialWheelWidgetBase* Widget = GetActiveWheelWidget())
	{
		Widget->UpdateInputVector(ResolveCurrentWheelVector());
	}
}

void AMP_GamePlayerController::OpenCosmeticsMenu()
{
	if (!IsLocalController())
	{
		return;
	}

	if (CosmeticsWidget)
	{
		CosmeticsWidget->SetVisibility(ESlateVisibility::Visible);
		return;
	}

	if (!CosmeticsWidgetClass)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[UI] CosmeticsWidgetClass no asignado en %s."), *GetNameSafe(this));
		return;
	}

	CosmeticsWidget = CreateWidget<UUserWidget>(this, CosmeticsWidgetClass);
	if (!CosmeticsWidget)
	{
		return;
	}

	TNVR::AddToScreen(CosmeticsWidget, MPGamePlayerController_ZOrderCosmetics);

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
}

bool AMP_GamePlayerController::RequestEquipHelmet(FName HelmetId)
{
	if (HelmetId == NAME_None)
	{
		return false;
	}

	if (UMP_GameInstance* GI = GetTNGameInstance())
	{
		if (!GI->EquipHelmetFor(this, HelmetId))
		{
			return false;
		}
	}

	ServerSetEquippedHelmet(HelmetId);
	return true;
}

void AMP_GamePlayerController::RequestUnequipHelmet()
{
	// Limpiar localmente el casco equipado en el save
	if (UMP_GameInstance* GI = GetTNGameInstance())
	{
		GI->ForceEquipHelmetFor(this, NAME_None);
	}
	// Enviar al servidor para actualizar PlayerState + notificar a todos
	ServerSetEquippedHelmet(NAME_None);
}

FName AMP_GamePlayerController::OpenHelmetCrate()
{
	if (UMP_GameInstance* GI = GetTNGameInstance())
	{
		const FName Result = GI->OpenHelmetCrateFor(this);
		SyncCosmeticsToServer();
		if (Result != NAME_None)
		{
			ServerSetEquippedHelmet(Result);
		}
		return Result;
	}

	return NAME_None;
}

void AMP_GamePlayerController::ClientOpenCosmeticsMenu_Implementation()
{
	OpenCosmeticsMenu();
}

// ── Tienda y probador ─────────────────────────────────────────────────────────

bool AMP_GamePlayerController::RequestEquipSkin(FName SkinId)
{
	UMP_GameInstance* GI = GetTNGameInstance();
	if (GI && !GI->IsCosmeticUnlockedFor(this, ETNCosmeticCategory::Body, SkinId)) { return false; }
	if (GI) { GI->EquipSkinFor(this, SkinId); }
	ServerSetEquippedSkin(SkinId);
	return true;
}

bool AMP_GamePlayerController::RequestEquipShell(FName ShellId)
{
	UMP_GameInstance* GI = GetTNGameInstance();
	if (GI && !GI->IsCosmeticUnlockedFor(this, ETNCosmeticCategory::Shell, ShellId)) { return false; }
	if (GI) { GI->EquipShellFor(this, ShellId); }
	ServerSetEquippedShell(ShellId);
	return true;
}

bool AMP_GamePlayerController::RequestEquipEyes(FName EyesId)
{
	UMP_GameInstance* GI = GetTNGameInstance();
	if (GI && !GI->IsCosmeticUnlockedFor(this, ETNCosmeticCategory::Eyes, EyesId)) { return false; }
	if (GI) { GI->EquipEyesFor(this, EyesId); }
	ServerSetEquippedEyes(EyesId);
	return true;
}

bool AMP_GamePlayerController::RequestPurchaseCosmetic(ETNCosmeticCategory Category, FName Id)
{
	UMP_GameInstance* GI = GetTNGameInstance();
	if (!GI || !GI->PurchaseCosmeticFor(this, Category, Id)) { return false; }
	if (Category == ETNCosmeticCategory::Helmet) { ServerSyncUnlockedHelmets(GI->GetUnlockedHelmetIdsFor(this)); }
	else { ServerSyncUnlockedSkins(GI->GetUnlockedSkinIdsFor(this)); }
	return true;
}

void AMP_GamePlayerController::ClientOpenShop_Implementation(ATN_ShopKeeper* Shop)
{
	if (!IsLocalController()) { return; }
	CloseShopUI();
	UTN_ShopWidget* Widget = CreateWidget<UTN_ShopWidget>(this, UTN_ShopWidget::StaticClass());
	if (!Widget) { return; }
	Widget->SetShop(Shop);
	TNVR::AddToScreen(Widget, MPGamePlayerController_ZOrderCosmetics);
	ShopUIWidget = Widget;
	// Solo la interfaz: el menú recibe todas las teclas (Escape lo cierra) y la tortuga no se mueve.
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Widget->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	SetShowMouseCursor(true);
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
}

void AMP_GamePlayerController::ClientOpenBriefing_Implementation(ATN_GeneralBriefing* General)
{
	if (!IsLocalController()) { return; }
	CloseShopUI();
	UTN_BriefingWidget* Widget = CreateWidget<UTN_BriefingWidget>(this, UTN_BriefingWidget::StaticClass());
	if (!Widget) { return; }
	Widget->SetGeneral(General);
	TNVR::AddToScreen(Widget, MPGamePlayerController_ZOrderCosmetics);
	ShopUIWidget = Widget;
	// Como la tienda: solo la interfaz (Escape cierra) y la tortuga quieta mientras escucha.
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Widget->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	SetShowMouseCursor(true);
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
}

void AMP_GamePlayerController::ClientOpenBooth_Implementation(ATN_ChangingBooth* Booth)
{
	if (!IsLocalController()) { return; }
	CloseShopUI();
	ActiveBooth = Booth;
	// En VR se entra al probador de golpe (sin fundido) y con las gafas puestas uno se ve desde su cámara.
	if (Booth) { SetViewTargetWithBlend(Booth, TNVR::ViewBlendTime(0.7f), VTBlend_EaseInOut, 2.f); }
	UTN_BoothWidget* Widget = CreateWidget<UTN_BoothWidget>(this, UTN_BoothWidget::StaticClass());
	if (!Widget) { return; }
	Widget->SetBooth(Booth);
	TNVR::AddToScreen(Widget, MPGamePlayerController_ZOrderCosmetics);
	ShopUIWidget = Widget;
	// Solo la interfaz: el menú recibe todas las teclas (Escape lo cierra) y la tortuga no se mueve.
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Widget->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	SetShowMouseCursor(true);
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
}

void AMP_GamePlayerController::CloseShopUI()
{
	if (ShopUIWidget)
	{
		ShopUIWidget->RemoveFromParent();
		ShopUIWidget = nullptr;
		SetShowMouseCursor(false);
		ResetIgnoreMoveInput();
		ResetIgnoreLookInput();
		ApplyGameplayInputMode();
	}
	if (ATN_ChangingBooth* Booth = ActiveBooth.Get())
	{
		if (APawn* MyPawn = GetPawn()) { SetViewTargetWithBlend(MyPawn, TNVR::ViewBlendTime(0.5f), VTBlend_EaseInOut, 2.f); }
		ServerLeaveBooth(Booth);
	}
	ActiveBooth.Reset();
}

void AMP_GamePlayerController::ServerLeaveBooth_Implementation(ATN_ChangingBooth* Booth)
{
	if (Booth) { Booth->ReleaseOccupant(GetPawn()); }
}

bool AMP_GamePlayerController::ServerSyncUnlockedSkins_Validate(const TArray<FName>& UnlockedSkinIds)
{
	return UnlockedSkinIds.Num() <= 256;
}

void AMP_GamePlayerController::ServerSyncUnlockedSkins_Implementation(const TArray<FName>& UnlockedSkinIds)
{
	// Como los cascos: solo lo que exista en el DataTable del servidor.
	const UMP_GameInstance* GI = GetTNGameInstance();
	const UDataTable* SkinTable = GI ? GI->GetSkinDataTable() : nullptr;
	if (!SkinTable || UnlockedSkinIds.Num() > 100) { return; }
	const TArray<FName> Known = SkinTable->GetRowNames();
	ServerUnlockedSkins.Reset();
	for (const FName SkinId : UnlockedSkinIds)
	{
		if (SkinId != NAME_None && Known.Contains(SkinId)) { ServerUnlockedSkins.Add(SkinId); }
	}
}

void AMP_GamePlayerController::ServerSetEquippedShell_Implementation(FName ShellId)
{
	if (ShellId != NAME_None)
	{
		const UMP_GameInstance* GI = GetTNGameInstance();
		const FTN_SkinData* Row = GI ? GI->FindSkinRow(ShellId, TEXT("ServerSetEquippedShell")) : nullptr;
		if (!Row || Row->Category != ETNCosmeticCategory::Shell || !ServerUnlockedSkins.Contains(ShellId))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[PC] ServerSetEquippedShell: '%s' no es un caparazón desbloqueado de %s"), *ShellId.ToString(), *GetNameSafe(this));
			return;
		}
	}
	if (ATN_CoopPlayerState* TNPS = GetPlayerState<ATN_CoopPlayerState>())
	{
		TNPS->EquippedShellId = ShellId;
		TNPS->ForceNetUpdate();
		// El servidor (autoridad) no recibe OnRep: aplica aquí.
		if (ATortugaCharacter* TurtleChar = Cast<ATortugaCharacter>(GetPawn()))
		{
			TurtleChar->UpdateSkinVisual(TNPS->EquippedSkinId);
		}
	}
}

void AMP_GamePlayerController::ServerSetEquippedEyes_Implementation(FName EyesId)
{
	if (EyesId != NAME_None)
	{
		const UMP_GameInstance* GI = GetTNGameInstance();
		const FTN_SkinData* Row = GI ? GI->FindSkinRow(EyesId, TEXT("ServerSetEquippedEyes")) : nullptr;
		if (!Row || Row->Category != ETNCosmeticCategory::Eyes || !ServerUnlockedSkins.Contains(EyesId))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[PC] ServerSetEquippedEyes: '%s' no son unos ojos desbloqueados de %s"), *EyesId.ToString(), *GetNameSafe(this));
			return;
		}
	}
	if (ATN_CoopPlayerState* TNPS = GetPlayerState<ATN_CoopPlayerState>())
	{
		TNPS->EquippedEyesId = EyesId;
		TNPS->ForceNetUpdate();
		// El servidor (autoridad) no recibe OnRep: aplica aquí.
		if (ATortugaCharacter* TurtleChar = Cast<ATortugaCharacter>(GetPawn()))
		{
			TurtleChar->UpdateSkinVisual(TNPS->EquippedSkinId);
		}
	}
}

void AMP_GamePlayerController::ClientNotifyServerTravel_Implementation()
{
	// Cerrar cualquier menú o rueda abiertos antes del travel para restaurar el
	// input mode. Si el jugador tiene FInputModeGameAndUI activo (cosméticos,
	// rueda de emotes) y el travel ocurre, los widgets se destruyen pero el
	// input mode no se restaura automáticamente → input bloqueado en el Run.
	if (ActiveWheelType != ETN_RadialWheelType::None)
	{
		CloseRadialWheel(false);
	}
	if (CosmeticsWidget && CosmeticsWidget->GetVisibility() == ESlateVisibility::Visible)
	{
		CosmeticsWidget->SetVisibility(ESlateVisibility::Hidden);
		ApplyGameplayInputMode();
	}
	CloseShopUI();

	// Marcar que estamos en travel para que OnNetworkFailure active auto-rejoin
	// en vez de destruir la sesión y mostrar error.
	if (UMP_GameInstance* GI = GetTNGameInstance())
	{
		GI->NotifyClientPendingTravel();
	}
	UE_LOG(LogTortunabo, Log, TEXT("[PC] ClientNotifyServerTravel received — prepared for reconnection."));
}

bool AMP_GamePlayerController::ServerSyncUnlockedHelmets_Validate(const TArray<FName>& UnlockedHelmetIds)
{
	// Gate de engine contra flooding: cota generosa (el legítimo manda ≤50, ver
	// MaxSyncedHelmets en _Implementation). Arrays enormes = manipulado → kick.
	return UnlockedHelmetIds.Num() <= 256;
}

void AMP_GamePlayerController::ServerSyncUnlockedHelmets_Implementation(const TArray<FName>& UnlockedHelmetIds)
{
	// Cap to prevent clients from flooding the server with a massive array.
	constexpr int32 MaxSyncedHelmets = 50;
	if (UnlockedHelmetIds.Num() > MaxSyncedHelmets)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[PC] ServerSyncUnlockedHelmets: oversized array (%d) from %s — rejected"),
			UnlockedHelmetIds.Num(), *GetNameSafe(this));
		return;
	}

	// Fase 4.2 — fuente de confianza: no creerse la lista del cliente tal cual.
	// Paridad con ServerSetEquippedSkin: cada ID debe existir en el DataTable del
	// servidor; FNames desconocidos (cliente manipulado) se descartan. Riesgo
	// residual aceptado: reclamar cascos existentes no desbloqueados equivale a
	// editarse el save local — cosmético, sin economía real detrás.
	const UMP_GameInstance* GI = GetTNGameInstance();
	const UDataTable* HelmetTable = GI ? GI->GetHelmetDataTable() : nullptr;
	if (!HelmetTable)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[PC] ServerSyncUnlockedHelmets: sin HelmetDataTable en el server — sync rechazado para %s"),
			*GetNameSafe(this));
		return;
	}
	const TArray<FName> KnownHelmets = HelmetTable->GetRowNames();

	ServerUnlockedHelmets.Reset();
	for (const FName HelmetId : UnlockedHelmetIds)
	{
		if (HelmetId == NAME_None)
		{
			continue;
		}
		if (!KnownHelmets.Contains(HelmetId))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[PC] ServerSyncUnlockedHelmets: '%s' no existe en DT_Helmets — descartado (%s)"),
				*HelmetId.ToString(), *GetNameSafe(this));
			continue;
		}
		ServerUnlockedHelmets.Add(HelmetId);
	}
}

void AMP_GamePlayerController::ServerSetEquippedHelmet_Implementation(FName HelmetId)
{
	// NAME_None = desequipar (siempre permitido).
	// Otro ID: debe estar en el conjunto de cascos desbloqueados del jugador.
	if (HelmetId != NAME_None && !ServerUnlockedHelmets.Contains(HelmetId))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[PC] ServerSetEquippedHelmet: '%s' no desbloqueado para %s"),
			*HelmetId.ToString(), *GetNameSafe(this));
		return;
	}

	if (ATN_CoopPlayerState* TNPS = GetPlayerState<ATN_CoopPlayerState>())
	{
		TNPS->EquippedHelmetId = HelmetId;
		TNPS->ForceNetUpdate(); // Forzar replicación inmediata del helmet a todos los clientes

		// El listen-server (authority) no recibe OnRep → aplica el mesh directamente.
		if (ATortugaCharacter* TurtleChar = Cast<ATortugaCharacter>(GetPawn()))
		{
			TurtleChar->UpdateHelmetMesh(HelmetId);
		}
	}
}

void AMP_GamePlayerController::ServerSetEquippedSkin_Implementation(FName SkinId)
{
	// NAME_None = el color de serie (siempre permitido). Cualquier otro: una fila de DT_Skins que el jugador tenga
	// desbloqueada en la tienda (ServerSyncUnlockedSkins).
	if (SkinId != NAME_None)
	{
		const UMP_GameInstance* GI = GetTNGameInstance();
		const UDataTable* SkinTable = GI ? GI->GetSkinDataTable() : nullptr;
		if (!SkinTable || !SkinTable->GetRowNames().Contains(SkinId) || !ServerUnlockedSkins.Contains(SkinId))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[PC] ServerSetEquippedSkin: '%s' no es un color desbloqueado de %s"),
				*SkinId.ToString(), *GetNameSafe(this));
			return;
		}
	}

	if (ATN_CoopPlayerState* TNPS = GetPlayerState<ATN_CoopPlayerState>())
	{
		TNPS->EquippedSkinId = SkinId;
		TNPS->ForceNetUpdate();

		// El listen-server (authority) no recibe OnRep → aplica visualmente de forma directa.
		if (ATortugaCharacter* TurtleChar = Cast<ATortugaCharacter>(GetPawn()))
		{
			TurtleChar->UpdateSkinVisual(SkinId);
		}
	}
}

void AMP_GamePlayerController::ClientSaveSkin_Implementation(FName SkinId)
{
	if (UMP_GameInstance* GI = GetTNGameInstance())
	{
		GI->EquipSkinFor(this, SkinId);
	}
}

void AMP_GamePlayerController::ClientSaveHelmet_Implementation(FName HelmetId)
{
	if (UMP_GameInstance* GI = GetTNGameInstance())
	{
		GI->ForceEquipHelmetFor(this, HelmetId);
	}
}

void AMP_GamePlayerController::NotifySkinEquipped(FName SkinId)
{
	ClientSaveSkin(SkinId);
}

void AMP_GamePlayerController::NotifyHelmetEquipped(FName HelmetId)
{
	ClientSaveHelmet(HelmetId);
}

void AMP_GamePlayerController::SyncCosmeticsToServer()
{
	if (!IsLocalController())
	{
		return;
	}

	if (UMP_GameInstance* GI = GetTNGameInstance())
	{
		// El perfil de este jugador: el guardado o, si es un invitado de la partida local, el de la partida (#311).
		ServerSyncUnlockedHelmets(GI->GetUnlockedHelmetIdsFor(this));
		ServerSyncUnlockedSkins(GI->GetUnlockedSkinIdsFor(this));
		// Sincronizar casco (NAME_None = sin casco, siempre enviar para no revertir un desequipado explícito)
		ServerSetEquippedHelmet(GI->GetEquippedHelmetIdFor(this));
		// Sincronizar color y caparazón (NAME_None = los de serie, siempre enviar)
		ServerSetEquippedSkin(GI->GetEquippedSkinIdFor(this));
		ServerSetEquippedShell(GI->GetEquippedShellIdFor(this));
		ServerSetEquippedEyes(GI->GetEquippedEyesIdFor(this));
	}
}

// ── Quick Chat ────────────────────────────────────────────────────────────────

void AMP_GamePlayerController::SendQuickChat(uint8 MessageID)
{
	if (!IsLocalController()) { return; }
	ServerSendQuickChat(MessageID);
}

void AMP_GamePlayerController::RequestPlayEmoteById(uint8 EmoteID)
{
	if (!IsLocalController())
	{
		return;
	}

	if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetPawn()))
	{
		Turtle->RequestWheelEmote(EmoteID);
	}
}

bool AMP_GamePlayerController::ResolveQuickChatDisplayData(const FTN_QuickChatEntry& Entry, FText& OutSenderName, FText& OutMessageText, UTexture2D*& OutIcon) const
{
	OutSenderName = FText::GetEmpty();
	OutMessageText = FText::GetEmpty();
	OutIcon = nullptr;

	const ATN_CoopGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATN_CoopGameState>() : nullptr;
	if (GS)
	{
		OutSenderName = GS->ResolveQuickChatSenderName(Entry.SenderPlayerId);
	}

	if (!QuickChatWheelDataAsset)
	{
		return false;
	}

	if (const FTN_QuickChatWheelEntry* ChatEntry = QuickChatWheelDataAsset->FindEntryById(Entry.MessageID))
	{
		OutMessageText = ChatEntry->Text;
		OutIcon = ChatEntry->Icon;
		return true;
	}

	return false;
}

void AMP_GamePlayerController::ServerSendQuickChat_Implementation(uint8 MessageID)
{
	UWorld* World = GetWorld();
	if (!World || !QuickChatWheelDataAsset) { return; }

	const FTN_QuickChatWheelEntry* ChatEntry = QuickChatWheelDataAsset->FindEntryById(MessageID);
	if (!ChatEntry)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[QuickChat] Invalid MessageID %d from %s"), static_cast<int32>(MessageID), *GetNameSafe(this));
		return;
	}

	ATN_CoopPlayerState* TNPS = GetPlayerState<ATN_CoopPlayerState>();
	if (!TNPS)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	const float Cooldown = ChatEntry->CooldownOverride > 0.f ? ChatEntry->CooldownOverride : QuickChatCooldownSeconds;
	if (!TNPS->CanServerSendQuickChat(Now, Cooldown))
	{
		return;
	}
	TNPS->MarkServerQuickChatSent(Now);

	ATN_CoopGameState* GS = World->GetGameState<ATN_CoopGameState>();
	if (!GS) { return; }

	const int32 SenderId = PlayerState ? PlayerState->GetPlayerId() : 0;
	GS->AddQuickChatEntry(SenderId, MessageID, Now);
}

// ── Pruebas de la tormenta (TNStorm) ──────────────────────────────────────────

namespace
{
	/**
	 * Guard de los RPC de pruebas: fuera de Shipping y solo para el anfitrión.
	 * Un invitado con un cliente modificado no puede parar ni mover la tormenta de todos.
	 */
	bool TNIsHostDebugCallAllowed(AMP_GamePlayerController* PC, const TCHAR* Command)
	{
		const UWorld* World = PC ? PC->GetWorld() : nullptr;
		const ENetMode NetMode = World ? World->GetNetMode() : NM_DedicatedServer;
		const bool bIsLocal = PC && PC->IsLocalController();
		if (TNDebugRpcLogic::CanRunHostOnlyDebugRpc(TNDebugRpcLogic::IsShippingBuild(), NetMode, bIsLocal))
		{
			return true;
		}
		UE_LOG(LogTortunabo, Warning, TEXT("[Debug] %s rechazado: '%s' no es el anfitrión o la build es Shipping."),
			Command, *GetNameSafe(PC));
		return false;
	}
}

void AMP_GamePlayerController::ServerExecRPC_Implementation(const FString& Msg)
{
	// Sin esto, un invitado con `ServerExec TN.Ghost.Become 1` (o TN.Race.*, TN.Tutorial.Station) lo ejecutaba en el anfitrión.
	if (TNIsHostDebugCallAllowed(this, TEXT("ServerExec")))
	{
		Super::ServerExecRPC_Implementation(Msg);
	}
}

void AMP_GamePlayerController::TNStorm(const FString& Where, float Ahead)
{
	ServerStormTest(Where, Ahead);
}

void AMP_GamePlayerController::ServerStormTest_Implementation(const FString& Where, float Ahead)
{
#if UE_BUILD_SHIPPING
	// Mueve la tormenta de todos: solo en las builds de desarrollo.
	TNIsHostDebugCallAllowed(this, TEXT("TNStorm"));
#else
	if (!TNIsHostDebugCallAllowed(this, TEXT("TNStorm")))
	{
		ClientMessage(TEXT("TNStorm: solo el anfitrión."));
		return;
	}
	UWorld* World = GetWorld();
	ATN_ProcMapGenerator* Gen = nullptr;
	ATN_PathStorm* Storm = nullptr;
	for (TActorIterator<ATN_ProcMapGenerator> It(World); It; ++It) { Gen = *It; break; }
	for (TActorIterator<ATN_PathStorm> It(World); It; ++It) { Storm = *It; break; }
	APawn* MyPawn = GetPawn();
	if (!Gen || !Storm || !MyPawn || !Gen->IsMapReady())
	{
		ClientMessage(TEXT("TNStorm: solo en el mapa procedural, con el mapa listo y la tortuga viva."));
		return;
	}
	const FString Key = Where.ToLower();
	if (Key == TEXT("off") || Key == TEXT("apagar"))
	{
		Storm->DebugPlaceFront(-3000.f, false);
		Storm->StopStorm();
		ClientMessage(TEXT("TNStorm: tormenta parada y otra vez peligrosa."));
		return;
	}

	const TNProcMap::FLayout& L = Gen->GetLayout();
	const FTransform MapXf = Gen->GetActorTransform();
	FVector Spot = FVector::ZeroVector;
	FVector Facing = FVector::ForwardVector;
	bool bFound = false;

	static const TMap<FString, ETNProcBiome> BiomeNames = {
		{ TEXT("selva"), ETNProcBiome::Jungle }, { TEXT("jungle"), ETNProcBiome::Jungle },
		{ TEXT("playa"), ETNProcBiome::Beach }, { TEXT("beach"), ETNProcBiome::Beach },
		{ TEXT("desierto"), ETNProcBiome::Desert }, { TEXT("desert"), ETNProcBiome::Desert },
		{ TEXT("volcan"), ETNProcBiome::Volcanic }, { TEXT("volcán"), ETNProcBiome::Volcanic }, { TEXT("volcanic"), ETNProcBiome::Volcanic },
		{ TEXT("agua"), ETNProcBiome::Water }, { TEXT("water"), ETNProcBiome::Water },
		{ TEXT("rocas"), ETNProcBiome::Rocky }, { TEXT("rocky"), ETNProcBiome::Rocky }, { TEXT("acantilados"), ETNProcBiome::Rocky },
		{ TEXT("manglar"), ETNProcBiome::Mangrove }, { TEXT("mangrove"), ETNProcBiome::Mangrove },
		{ TEXT("pueblo"), ETNProcBiome::Human }, { TEXT("human"), ETNProcBiome::Human }, { TEXT("humana"), ETNProcBiome::Human },
	};
	if (const ETNProcBiome* Biome = BiomeNames.Find(Key))
	{
		// El tramo más largo del camino principal en ese bioma: su muestra del medio.
		int32 BestFrom = INDEX_NONE, BestLen = 0;
		for (int32 i = 0; i < L.Main.Num();)
		{
			if (L.Main[i].Biome != *Biome) { ++i; continue; }
			int32 j = i;
			while (j < L.Main.Num() && L.Main[j].Biome == *Biome) { ++j; }
			if (j - i > BestLen) { BestLen = j - i; BestFrom = i; }
			i = j;
		}
		if (BestFrom != INDEX_NONE)
		{
			const TNProcMap::FPathSample& S = L.Main[BestFrom + BestLen / 2];
			Spot = MapXf.TransformPosition(FVector(S.P.X, S.P.Y, S.Z + 110.0));
			Facing = MapXf.TransformVectorNoScale(FVector(S.Dir.X, S.Dir.Y, 0.0));
			bFound = true;
		}
	}
	else if (Key.StartsWith(TEXT("gey")) || Key.StartsWith(TEXT("gei")) || Key.StartsWith(TEXT("géi")) || Key.StartsWith(TEXT("casc")) || Key.StartsWith(TEXT("water")))
	{
		// Cada vez el siguiente géiser (o cascada) del mapa.
		const bool bGeyser = !Key.StartsWith(TEXT("casc")) && !Key.StartsWith(TEXT("water"));
		static int32 NextGeyser = 0;
		static int32 NextFall = 0;
		TArray<const TNProcMap::FFeature*> Found;
		for (const TNProcMap::FFeature& F : L.Features)
		{
			if (F.Type == (bGeyser ? TNProcMap::EFeature::Geyser : TNProcMap::EFeature::SlideZone)) { Found.Add(&F); }
		}
		if (Found.Num() > 0)
		{
			int32& Next = bGeyser ? NextGeyser : NextFall;
			const TNProcMap::FFeature& F = *Found[Next++ % Found.Num()];
			if (bGeyser)
			{
				const FVector Base = MapXf.TransformPosition(F.Location);
				FVector PathDir;
				Gen->GetPathLocationAtProgress(Gen->GetPathProgress(Base), PathDir);
				Facing = PathDir.GetSafeNormal2D();
				Spot = Base - Facing * 320.f + FVector(0.f, 0.f, 120.f);
			}
			else
			{
				const TArray<TNProcMap::FPathSample>& Samples = F.BranchIndex == INDEX_NONE ? L.Main : L.Branches[F.BranchIndex].Samples;
				if (Samples.IsValidIndex(F.PathIndex))
				{
					const TNProcMap::FPathSample& Lip = Samples[FMath::Max(0, F.PathIndex - 2)];
					Spot = MapXf.TransformPosition(FVector(Lip.P.X, Lip.P.Y, Lip.Z + 110.0));
					Facing = MapXf.TransformVectorNoScale(FVector(Lip.Dir.X, Lip.Dir.Y, 0.0));
				}
			}
			bFound = !Spot.IsZero();
		}
	}
	if (!bFound)
	{
		ClientMessage(FString::Printf(TEXT("TNStorm: no hay '%s' en este mapa."), *Where));
		return;
	}

	if (ACharacter* Char = Cast<ACharacter>(MyPawn)) { Char->GetCharacterMovement()->StopMovementImmediately(); }
	MyPawn->TeleportTo(Spot, Facing.Rotation(), false, true);
	ClientSetRotation(Facing.Rotation());
	const float Progress = Gen->GetPathProgress(Spot);
	Storm->DebugPlaceFront(Progress - Ahead, true);
	ClientMessage(FString::Printf(TEXT("TNStorm: %s (progreso %.0f), frente a %.0f cm."), *Where, Progress, Ahead));
#endif
}

// ── Pruebas de las conchas de puntos (TNShells) ─────────────────────────────────

void AMP_GamePlayerController::TNShells(const FString& What, int32 Count)
{
	ServerShellsTest(What, Count);
}

void AMP_GamePlayerController::ServerShellsTest_Implementation(const FString& What, int32 Count)
{
#if UE_BUILD_SHIPPING
	// Suelta puntos que van a la tienda: solo en las builds de desarrollo.
	ClientMessage(TEXT("TNShells: solo en las builds de desarrollo."));
#else
	if (!TNIsHostDebugCallAllowed(this, TEXT("TNShells")))
	{
		ClientMessage(TEXT("TNShells: solo el anfitrión."));
		return;
	}
	UWorld* World = GetWorld();
	APawn* MyPawn = GetPawn();
	if (!World || !MyPawn)
	{
		ClientMessage(TEXT("TNShells: hace falta una tortuga viva."));
		return;
	}
	ATN_ProcMapGenerator* Gen = nullptr;
	for (TActorIterator<ATN_ProcMapGenerator> It(World); It; ++It) { Gen = *It; break; }
	const FString Key = What.ToLower();

	if (Key.StartsWith(TEXT("list")))
	{
		ClientMessage(Gen && !Gen->GetShellSummary().IsEmpty() ? FString(TEXT("TNShells: ")) + Gen->GetShellSummary()
			: FString(TEXT("TNShells: no hay conchas del mapa procedural en este nivel.")));
		return;
	}

	if (Key.StartsWith(TEXT("esp")) || Key.StartsWith(TEXT("spe")))
	{
		const TArray<FTNShellSpot>* Spots = Gen ? &Gen->GetSpecialShellSpots() : nullptr;
		if (!Spots || Spots->Num() == 0)
		{
			ClientMessage(TEXT("TNShells: este mapa no tiene conchas especiales (o no es el mapa procedural)."));
			return;
		}
		// Cada vez la siguiente, en orden por el camino.
		static int32 NextSpecial = 0;
		const int32 Index = NextSpecial++ % Spots->Num();
		const FTNShellSpot& Spot = (*Spots)[Index];
		const FVector Facing = Spot.Facing.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Spot.Facing.GetSafeNormal2D();
		if (ACharacter* MovingChar = Cast<ACharacter>(MyPawn)) { MovingChar->GetCharacterMovement()->StopMovementImmediately(); }
		MyPawn->TeleportTo(Spot.Stand, Facing.Rotation(), false, true);
		ClientSetRotation(Facing.Rotation());
		ClientMessage(FString::Printf(TEXT("TNShells: especial %d de %d, de %d en %s (a %.0f m)."), Index + 1, Spots->Num(), Spot.Value, *Spot.Where,
			FVector::Dist(Spot.Stand, Spot.Shell) / 100.0));
		return;
	}

	const int32 Value = FCString::Atoi(*What);
	if (Value <= 0)
	{
		ClientMessage(TEXT("TNShells: 1|25|50|100 [cantidad] suelta conchas delante; Especial lleva a la siguiente especial; Lista las cuenta."));
		return;
	}
	UClass* ShellClass = UTN_GameplayAssetSettings::GetScorePickupClass();
	// En fila delante de la tortuga, a la altura de siempre sobre sus pies, para cogerlas de una carrera.
	const int32 Number = FMath::Clamp(Count, 1, 20);
	const FVector Forward = MyPawn->GetActorForwardVector().GetSafeNormal2D();
	const ACharacter* Char = Cast<ACharacter>(MyPawn);
	const float HalfHeight = Char ? Char->GetSimpleCollisionHalfHeight() : 70.f;
	const FVector Feet = MyPawn->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	for (int32 i = 0; i < Number; ++i)
	{
		const FTransform Where(Forward.Rotation(), Feet + Forward * (260.f + 170.f * i) + FVector(0.f, 0.f, TNScoreShells::Hover));
		ATN_ScorePickup* Shell = World->SpawnActorDeferred<ATN_ScorePickup>(ShellClass, Where, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Shell)
		{
			continue;
		}
		Shell->SetScoreValue(Value);
		Shell->FinishSpawning(Where);
		// Que no se queden para siempre si nadie las coge.
		Shell->SetLifeSpan(300.f);
	}
	ClientMessage(FString::Printf(TEXT("TNShells: %d conchas de %d delante de ti."), Number, Value));
#endif
}

// ── Pruebas del lobby (TNShop, TNBooth) ────────────────────────────────────────

void AMP_GamePlayerController::TNShop()
{
	ATN_ShopKeeper* Nearest = nullptr;
	double Best = TNumericLimits<double>::Max();
	const FVector From = GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;
	for (TActorIterator<ATN_ShopKeeper> It(GetWorld()); It; ++It)
	{
		const double D = FVector::DistSquared(From, It->GetActorLocation());
		if (D < Best) { Best = D; Nearest = *It; }
	}
	ClientOpenShop(Nearest);
}

void AMP_GamePlayerController::TNBooth()
{
	ServerTestBooth();
}

void AMP_GamePlayerController::ServerTestBooth_Implementation()
{
#if UE_BUILD_SHIPPING
	TNIsHostDebugCallAllowed(this, TEXT("TNBooth"));
#else
	if (!TNIsHostDebugCallAllowed(this, TEXT("TNBooth")))
	{
		ClientMessage(TEXT("TNBooth: solo el anfitrión."));
		return;
	}
	APawn* MyPawn = GetPawn();
	if (!MyPawn) { return; }
	ATN_ChangingBooth* Nearest = nullptr;
	double Best = TNumericLimits<double>::Max();
	for (TActorIterator<ATN_ChangingBooth> It(GetWorld()); It; ++It)
	{
		const double D = FVector::DistSquared(MyPawn->GetActorLocation(), It->GetActorLocation());
		if (It->CanInteract(MyPawn) && D < Best) { Best = D; Nearest = *It; }
	}
	if (Nearest) { Nearest->Interact(MyPawn); }
	else { ClientMessage(TEXT("TNBooth: no hay ningún probador libre.")); }
#endif
}
