#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "UI/HUD/TN_RadialWheelTypes.h"
#include "Core/TN_CosmeticsTypes.h"
#include "Voice/TN_VoiceRouting.h"
#include "Player/TN_SecretEmote.h"
#include "MP_GamePlayerController.generated.h"

struct FInputActionValue;
class UUserWidget;
class UInputAction;
class UTexture2D;
class UTN_RadialWheelWidgetBase;
class UTN_EmoteWheelDataAsset;
class UTN_QuickChatWheelDataAsset;
class UMP_GameInstance;
class APlayerState;
class AGameStateBase;
class ATN_ShopKeeper;
class ATN_ChangingBooth;
class ATN_GeneralBriefing;
class UTN_AmbientSoundscapeComponent;

/**
 * @brief PlayerController principal del gameplay. Centraliza HUD, espectador, cosméticos, ruedas radiales, VOIP, Quick Chat y emotes.
 *
 * Responsabilidades:
 *  - Creación/refresh del HUD (Stamina, CoopFlow, VoiceIndicator, Cosmetics, PlayerHUD).
 *  - Modo espectador: enter, next/previous con filtro por vivos/no eliminados.
 *  - Sincronización servidor↔cliente de cosméticos (helmet + skin) con Server RPCs y persistencia en GameInstance.
 *  - Quick Chat (RL-style): envío por catálogo con cooldown server-side.
 *  - Ruedas radiales (Emote + QuickChat) con input mouse/stick.
 *  - VOIP: receptor de audio filtrado por proximidad desde el servidor.
 *  - Auto-rejoin: hooks de seamless travel para preservar la sesión Steam.
 */
UCLASS()
class TORTUNABO_API AMP_GamePlayerController : public APlayerController, public ITN_VoiceListener
{
	GENERATED_BODY()

public:
	AMP_GamePlayerController();

	/** @brief Entra en modo espectador: posee un SpectatorPawn y comienza a observar al siguiente vivo. */
	UFUNCTION(BlueprintCallable, Category = "Spectator")
	void EnterSpectateMode();

	/** @brief Cambia el target del espectador al siguiente jugador vivo y no eliminado. */
	UFUNCTION(BlueprintCallable, Category = "Spectator")
	void SpectateNextPlayer();

	/** @brief Cambia el target del espectador al anterior jugador vivo y no eliminado. */
	UFUNCTION(BlueprintCallable, Category = "Spectator")
	void SpectatePreviousPlayer();

	/** @brief true mientras está abierta la rueda de emotes o la de frases (el modo VR las maneja con el stick, no con el puntero). */
	bool IsRadialWheelOpen() const { return ActiveWheelType != ETN_RadialWheelType::None; }

	/** @brief Abre el widget de cosméticos. Disponible sólo en el lobby HQ. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	void OpenCosmeticsMenu();

	/**
	 * @brief Solicita equipar un casco (con verificación de unlock vía GameInstance).
	 * @param HelmetId ID del casco a equipar.
	 * @return true si el GameInstance lo aceptó. Persiste y replica vía PlayerState.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool RequestEquipHelmet(FName HelmetId);

	/** @brief Desequipa el casco actual. Actualiza PlayerState en el servidor. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	void RequestUnequipHelmet();

	/** @brief Abre una caja de cascos: sortea uno por pesos y lo equipa. Devuelve el ID. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	FName OpenHelmetCrate();

	/** @brief Client RPC: abre el widget de cosméticos en el cliente owner (llamado desde estatuas del lobby). */
	UFUNCTION(Client, Reliable)
	void ClientOpenCosmeticsMenu();

	// ── Tienda y probador del lobby ──────────────────────────────────────────

	/** @brief Equipa un color de cuerpo desbloqueado (NAME_None = el de serie): lo guarda y lo replica. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool RequestEquipSkin(FName SkinId);

	/** @brief Equipa un caparazón desbloqueado (NAME_None = el de serie): lo guarda y lo replica. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool RequestEquipShell(FName ShellId);

	/** @brief Equipa unos ojos desbloqueados (NAME_None = los clásicos): los guarda y los replica. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool RequestEquipEyes(FName EyesId);

	/** @brief Tienda: lo compra con conchas (los de la tortuga hoy cuestan 0), lo guarda y manda los desbloqueos al servidor. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool RequestPurchaseCosmetic(ETNCosmeticCategory Category, FName Id);

	/** @brief Probador: pone el buggy del Rally (modelo y pintura comprados o gratis), lo guarda y lo replica. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool RequestEquipBuggyLook(const FTN_BuggyLook& Look);

	/** @brief Client RPC: abre la tienda del tendero (UTN_ShopWidget). */
	UFUNCTION(Client, Reliable)
	void ClientOpenShop(ATN_ShopKeeper* Shop);

	/** @brief Client RPC: abre la sesión informativa del general del cuartel (UTN_BriefingWidget). */
	UFUNCTION(Client, Reliable)
	void ClientOpenBriefing(ATN_GeneralBriefing* General);

	/** @brief Client RPC: el probador se ha cerrado contigo dentro: la cámara se aparta y sale el selector (UTN_BoothWidget). */
	UFUNCTION(Client, Reliable)
	void ClientOpenBooth(ATN_ChangingBooth* Booth);

	/** @brief Server RPC: sales del probador (lo elegido ya va equipado): se abre la puerta y vuelves a moverte. */
	UFUNCTION(Server, Reliable)
	void ServerLeaveBooth(ATN_ChangingBooth* Booth);

	/** @brief Cierra la tienda o el probador (lo llaman sus widgets) y devuelve el control al juego. */
	void CloseShopUI();

	/**
	 * Server → Client: "Prepárate, el servidor va a hacer ServerTravel."
	 * El cliente marca bIsPendingTravel en su GameInstance y muestra loading screen.
	 * Cuando el NetDriver se destruya y el cliente reciba ConnectionLost,
	 * OnNetworkFailure verá bIsPendingTravel=true y activará auto-rejoin.
	 */
	UFUNCTION(Client, Reliable)
	void ClientNotifyServerTravel();

	// ── Quick Chat (estilo Rocket League) ────────────────────────────────────

	/**
	 * @brief Envía un mensaje de Quick Chat por ID compacto del catálogo local.
	 *        El servidor valida el ID + cooldown y escribe en el historial replicado del GameState.
	 */
	UFUNCTION(BlueprintCallable, Category = "QuickChat")
	void SendQuickChat(uint8 MessageID);

	/**
	 * @brief Consola (pruebas y vista previa): abre la rueda de emotes (Type 0) o de frases (1) y la apunta en la
	 *        dirección (X, Y) (X a la derecha, Y hacia arriba); Type -1 la cierra sin elegir.
	 */
	UFUNCTION(Exec)
	void TNWheel(int32 Type, float X, float Y);

	/**
	 * @brief Consola (pruebas de la tormenta en el mapa procedural): lleva a tu tortuga a un sitio y pone la tormenta
	 *        encima, inofensiva. Where: un bioma (Selva, Playa, Desierto, Volcan, Agua, Rocas, Manglar, Pueblo), Geiser
	 *        o Cascada (cada vez el siguiente del mapa) u Off (tormenta normal otra vez). Ahead: cm entre el frente y
	 *        la tortuga (positivo = llega por detrás; negativo = ya estás dentro).
	 */
	UFUNCTION(Exec)
	void TNStorm(const FString& Where, float Ahead = 900.f);

	/**
	 * @brief Consola (pruebas de las conchas de puntos): «TNShells 1|25|50|100 [N]» suelta N conchas (hasta 20) de ese
	 *        valor en fila delante de tu tortuga, para cogerlas corriendo y ver el estallido y el contador; «TNShells
	 *        Especial» te lleva cada vez a la siguiente concha especial (50 o 100) del mapa procedural; «TNShells Lista»
	 *        dice cuántas hay de cada tamaño y dónde van las especiales.
	 */
	UFUNCTION(Exec)
	void TNShells(const FString& What, int32 Count = 1);

	/** @brief Consola (pruebas del lobby): abre la tienda del tendero más cercano sin ir hasta él. */
	UFUNCTION(Exec)
	void TNShop();

	/** @brief Consola (pruebas del lobby): entra en el probador libre más cercano sin ir hasta él. */
	UFUNCTION(Exec)
	void TNBooth();

	/**
	 * @brief Pide al servidor reproducir un emote por ID (Multicast tras validación).
	 * @param EmoteID Id del emote según el DataAsset.
	 */
	UFUNCTION(BlueprintCallable, Category = "Emotes")
	void RequestPlayEmoteById(uint8 EmoteID);

	/**
	 * Llamado desde ATortugaCharacter::PawnClientRestart (cliente) para re-añadir
	 * los widgets del HUD al viewport tras seamless travel.
	 * En seamless travel el PC persiste pero UWorld::CleanupWorld elimina todos
	 * los widgets del viewport. Este método los vuelve a añadir.
	 */
	void RefreshHUDAfterPossession();

	/**
	 * Client RPC: limpia flags de ignorar input (del modo espectador)
	 * y restaura el modo de juego. Llamado desde RevivePlayer en el servidor.
	 * Sin esto, tras ser revivido el jugador no puede moverse porque
	 * ChangeState(Spectating) / BeginSpectatingState incrementa IgnoreMoveInput
	 * y la secuencia Possess+ClientRestart no lo decrementa en el cliente.
	 */
	UFUNCTION(Client, Reliable)
	void ClientRestorePlayerInput();

	/** @brief Versión sin RPC para el listen-server (los Client RPCs no se ejecutan en el host). */
	void ForceRestoreInput();

	/**
	 * @brief El cliente avisa de que ya construyó el mapa procedural de esa generación.
	 * @note Lo llama ATN_ProcMapGenerator en el cliente; el servidor lo reenvía a
	 *       ATN_ProcMapGameMode para arrancar la ronda cuando todos lo tienen.
	 */
	UFUNCTION(Server, Reliable)
	void ServerReportProcMapReady(int32 Generation);

	/**
	 * @brief Recibe audio de voz filtrado por proximidad desde el servidor.
	 * @param CompressedData Buffer comprimido del emisor.
	 * @param SenderSampleRate SampleRate original del emisor.
	 * @param SpeakerActor Actor emisor (para calcular distancia).
	 * @param bIntercom Comparte interfono con este jugador: se oye sin atenuar (TNVoiceRouting).
	 */
	UFUNCTION(Client, Unreliable)
	void ClientReceiveVoice(const TArray<uint8>& CompressedData, int32 SenderSampleRate, AActor* SpeakerActor, bool bIntercom);

	// ITN_VoiceListener
	virtual void SendVoiceToOwningClient(const TArray<uint8>& CompressedData, int32 SenderSampleRate, AActor* SpeakerActor,
		bool bIntercom) override;

	/** @brief Notifica al cliente dueño que guarde el SkinId. Llamado desde estatuas del lobby (servidor). */
	void NotifySkinEquipped(FName SkinId);

	/** @brief Notifica al cliente dueño que guarde el HelmetId. Llamado desde estatuas del lobby (servidor). */
	void NotifyHelmetEquipped(FName HelmetId);

	/**
	 * @brief Resuelve la info de display (nombre, mensaje, icono) de una entrada de Quick Chat.
	 * @param Entry Entrada bruta replicada.
	 * @param OutSenderName Nombre del emisor (resuelto vía PlayerState).
	 * @param OutMessageText Texto del catálogo.
	 * @param OutIcon Icono asociado.
	 * @return true si se pudo resolver la entrada.
	 */
	UFUNCTION(BlueprintPure, Category = "QuickChat")
	bool ResolveQuickChatDisplayData(const FTN_QuickChatEntry& Entry, FText& OutSenderName, FText& OutMessageText, UTexture2D*& OutIcon) const;

	/** Cooldown entre mensajes de Quick Chat (segundos). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "QuickChat", meta = (ClampMin = "0.5"))
	float QuickChatCooldownSeconds = 2.f;

protected:
	/** @brief Bindea delegates, crea HUD y carga assets de input radial. */
	virtual void BeginPlay() override;

	/** @brief OnPossess: refresca cosméticos del pawn poseído y sincroniza con servidor. */
	virtual void OnPossess(APawn* InPawn) override;

	/** @brief Carga UInputActions (soft refs) y bindea acciones de ruedas radiales y menú. */
	virtual void SetupInputComponent() override;

	/** @brief Mira cada tecla para el código secreto de los emotes ocultos (#839); no la consume. */
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	/**
	 * @brief El servidor me echa (AGameSession::KickPlayer, cuando la expulsión por ATN_RoomInfo no ha llegado a tiempo).
	 *        Vuelve al menú con el aviso de expulsión (UMP_GameInstance::HandleKickedFromRoom) antes de que se corte la
	 *        conexión, para que no salga «el anfitrión se ha ido» ni se intente reconectar.
	 */
	virtual void ClientWasKicked_Implementation(const FText& KickReason) override;

	/**
	 * @brief `ServerExec` (consola del motor) manda cualquier orden al servidor fuera de Shipping sin mirar quién la pide: solo
	 *        se atiende al anfitrión, con la misma regla que los RPC de pruebas (TNDebugRpcLogic, #16).
	 */
	virtual void ServerExecRPC_Implementation(const FString& Msg) override;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> VoiceIndicatorWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> CoopFlowWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> CosmeticsWidgetClass;

	/**
	 * HUD principal del jugador (barra de stamina, etc.).
	 * Asigna WBP_PlayerHUD en BP_GamePlayerController → Class Defaults.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> PlayerHUDWidgetClass;

	/**
	 * Usar el HUD hecho en código (UTN_RunHUDWidget y UTN_RunFlowHUDWidget, estilo común) en vez de
	 * PlayerHUDWidgetClass y CoopFlowWidgetClass.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	bool bUseCodeHUD = true;

	/** Con false no se crea ninguna interfaz de partida (HUD, flujo, voz, ruedas): nivel de solo terreno. */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	bool bShowHUD = true;

	/** Paisaje sonoro sintetizado por bioma (solo suena en el jugador local). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UTN_AmbientSoundscapeComponent> AmbientSoundscape;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Radial")
	TSubclassOf<UTN_RadialWheelWidgetBase> EmoteWheelWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Radial")
	TSubclassOf<UTN_RadialWheelWidgetBase> QuickChatWheelWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Data|Radial")
	TObjectPtr<UTN_EmoteWheelDataAsset> EmoteWheelDataAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Data|Radial")
	TObjectPtr<UTN_QuickChatWheelDataAsset> QuickChatWheelDataAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Radial")
	TSoftObjectPtr<UInputAction> OpenEmoteWheelAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Radial")
	TSoftObjectPtr<UInputAction> OpenQuickChatWheelAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Radial")
	TSoftObjectPtr<UInputAction> RadialNavigateAction;

	/** Input Action para volver al menú principal. Asignar IA_ReturnToMenu en el BP hijo. */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Menu")
	TSoftObjectPtr<UInputAction> ReturnToMenuAction;

private:
	UPROPERTY()
	TObjectPtr<UUserWidget> VoiceIndicatorWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> CoopFlowWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> CosmeticsWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> PlayerHUDWidget;

	UPROPERTY()
	TObjectPtr<UTN_RadialWheelWidgetBase> EmoteWheelWidget;

	UPROPERTY()
	TObjectPtr<UTN_RadialWheelWidgetBase> QuickChatWheelWidget;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedOpenEmoteWheelAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedOpenQuickChatWheelAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedRadialNavigateAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedReturnToMenuAction;

	/** @brief Server RPC: sincroniza la lista de cascos desbloqueados del cliente al PC del servidor. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSyncUnlockedHelmets(const TArray<FName>& UnlockedHelmetIds);

	/** @brief Server RPC: asigna el casco equipado en el PlayerState. */
	UFUNCTION(Server, Reliable)
	void ServerSetEquippedHelmet(FName HelmetId);

	/** @brief Server RPC: aplica el skin de personaje en el PlayerState (llamado desde SyncCosmeticsToServer). */
	UFUNCTION(Server, Reliable)
	void ServerSetEquippedSkin(FName SkinId);

	/** @brief Server RPC: colores y caparazones desbloqueados del cliente (validados contra DT_Skins). */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSyncUnlockedSkins(const TArray<FName>& UnlockedSkinIds);

	/** @brief Server RPC: asigna el caparazón equipado en el PlayerState. */
	UFUNCTION(Server, Reliable)
	void ServerSetEquippedShell(FName ShellId);

	/** @brief Server RPC: asigna los ojos equipados en el PlayerState. */
	UFUNCTION(Server, Reliable)
	void ServerSetEquippedEyes(FName EyesId);

	/** @brief Server RPC: modelos y pinturas del buggy desbloqueados del cliente (filtrados con TNBuggyCosmetics). */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSyncUnlockedBuggy(const TArray<FName>& UnlockedBuggyIds);

	/** @brief Server RPC: asigna el buggy equipado en el PlayerState si está en el catálogo y desbloqueado. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetEquippedBuggyLook(const FTN_BuggyLook& Look);

	/** @brief Client RPC: guarda SkinId en GameInstance del cliente dueño. */
	UFUNCTION(Client, Reliable)
	void ClientSaveSkin(FName SkinId);

	/** @brief Client RPC: guarda HelmetId en GameInstance del cliente dueño. */
	UFUNCTION(Client, Reliable)
	void ClientSaveHelmet(FName HelmetId);

	/** @brief Empuja al servidor la lista local de unlocks + cosméticos equipados (skin/helmet). */
	void SyncCosmeticsToServer();

	/** @brief Cast centralizado de GetGameInstance() a UMP_GameInstance; nullptr si no aplica. */
	UMP_GameInstance* GetTNGameInstance() const;

	/**
	 * @brief Lleva la cuenta del código «tortunabo» (TNSecretEmote) con las teclas del teclado y, al completarlo, activa un emote
	 *        oculto de la tortuga (#839). Solo cuenta con la tortuga a los mandos, sin menú a la vista (pausa, tienda, ruedas) y
	 *        sin un campo de texto con el foco; cualquier otra tecla lo reinicia.
	 */
	void FeedSecretEmoteCode(const FInputKeyEventArgs& Params);

	/** @brief Letras seguidas del código secreto que lleva escritas (#839). */
	TNSecretEmote::FCodeMatcher SecretEmoteMatcher;

	/** @brief Pone el input mode a Game (focus al viewport, sin cursor). */
	void ApplyGameplayInputMode();

	/** @brief Pone el input mode a GameAndUI con cursor visible para el wheel radial. */
	void ApplyRadialInputMode();

	/** @brief Restaura el input mode a Game tras cerrar una rueda radial. */
	void RestorePostRadialInputMode();

	/** @brief Crea el widget VoiceIndicator y lo añade al viewport. */
	void CreateVoiceHUD();

	/** @brief Crea el widget CoopFlow (estado de partida) y lo añade al viewport. */
	void CreateCoopFlowHUD();

	/** @brief Crea el widget PlayerHUD (stamina, inventario) y lo añade al viewport. */
	void CreatePlayerHUD();

	/** @brief Crea los widgets de las ruedas radiales (Emote + QuickChat) sin añadirlos al viewport. */
	void CreateRadialWidgets();

	/** @brief Cambia el target del espectador en la dirección dada (+1 / -1). */
	void SpectateByDirection(int32 Direction);

	/** @brief Recopila los PlayerState candidatos a espectar (vivos, no eliminados) a partir del GameState dado. */
	TArray<APlayerState*> BuildSpectateCandidates(AGameStateBase* GS) const;

	/** @brief Resuelve los SoftObjectPtr de InputAction a TObjectPtr cargados. */
	void CacheRadialInputAssets();

	void OnOpenEmoteWheelStarted();
	void OnOpenEmoteWheelReleased();
	void OnOpenQuickChatWheelStarted();
	void OnOpenQuickChatWheelReleased();

	void OnRadialNavigateTriggered(const FInputActionValue& Value);
	void OnRadialNavigateCompleted(const FInputActionValue& Value);

	/** @brief Handler del input "volver al menú": pide al servidor cerrar la sesión y travel a LVL_Menu. */
	void OnReturnToMenuPressed();

	/** @brief Server RPC: cierra la sesión y manda a todos al menú principal. */
	UFUNCTION(Server, Reliable)
	void ServerRequestReturnToMenu();

	/** @brief Abre una rueda radial (Emote o QuickChat) según WheelType. */
	void OpenRadialWheel(ETN_RadialWheelType WheelType);

	/** @brief Cierra la rueda activa, opcionalmente confirmando la opción seleccionada. */
	void CloseRadialWheel(bool bConfirmSelection);

	/** @brief Timer (alta frecuencia) que recalcula la opción apuntada por mouse/stick. */
	void UpdateRadialWheelInput();

	/** @brief Vector normalizado de la posición del ratón respecto al centro de la rueda. */
	FVector2D ComputeMouseWheelVector() const;

	/** @brief Centro de la rueda en píxeles del viewport: el de la vista de este jugador (con la pantalla partida, su trozo). */
	FVector2D GetWheelCenter() const;

	/** @brief Devuelve el vector del input activo (mouse o stick) según contexto. */
	FVector2D ResolveCurrentWheelVector() const;

	/** @brief Devuelve el widget de la rueda activa o nullptr si no hay ninguna. */
	UTN_RadialWheelWidgetBase* GetActiveWheelWidget() const;

	TSet<FName> ServerUnlockedHelmets;

	/** Colores y caparazones desbloqueados de este jugador (servidor). */
	TSet<FName> ServerUnlockedSkins;

	/** Modelos y pinturas del buggy desbloqueados de este jugador (servidor). */
	TSet<FName> ServerUnlockedBuggy;

	/** Tienda o probador abiertos (solo en el cliente dueño). */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ShopUIWidget;

	/** Probador en el que está este jugador (cliente dueño). */
	TWeakObjectPtr<ATN_ChangingBooth> ActiveBooth;

	/** @brief Server RPC: valida ID, aplica rate limit y escribe el QuickChat en el GameState. */
	UFUNCTION(Server, Reliable)
	void ServerSendQuickChat(uint8 MessageID);

	/** Servidor: el trabajo de TNStorm (mueve la tortuga y el frente de la tormenta). */
	UFUNCTION(Server, Reliable)
	void ServerStormTest(const FString& Where, float Ahead);

	/** Servidor: el trabajo de TNShells (suelta conchas o lleva a una especial). */
	UFUNCTION(Server, Reliable)
	void ServerShellsTest(const FString& What, int32 Count);

	/** Servidor: el trabajo de TNBooth. */
	UFUNCTION(Server, Reliable)
	void ServerTestBooth();

	ETN_RadialWheelType ActiveWheelType = ETN_RadialWheelType::None;
	FVector2D CachedStickVector = FVector2D::ZeroVector;
	float LastStickInputRealTime = -1000.f;
	FTimerHandle RadialWheelUpdateTimerHandle;
	FVector2D CachedMousePositionBeforeWheel = FVector2D::ZeroVector;
	bool bHadMousePositionBeforeWheel = false;
};
