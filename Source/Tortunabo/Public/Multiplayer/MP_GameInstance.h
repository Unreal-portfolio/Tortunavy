#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Engine/EngineBaseTypes.h"
#include "Core/TN_CosmeticsTypes.h"
#include "Lobby/TN_MysteryBox.h"
#include "Multiplayer/TN_RoomTypes.h"
#include "MP_GameInstance.generated.h"

class AGameModeBase;
class APlayerController;
class APlayerState;
class ATN_RoomInfo;
class UNetDriver;
class UUserWidget;
class UTN_CosmeticSaveGame;
struct FTN_HelmetData;
struct FTN_SkinData;
struct FUniqueNetIdRepl;

/** Aviso de las salas para la interfaz (sala cerrada, llena, código que no existe...). bError: en coral; si no, en dorado. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FTNOnRoomNotice, const FText& /*Message*/, bool /*bError*/);

/** Aviso que espera al menú principal (tras volver a él: expulsado, sala cerrada, el anfitrión se fue...). */
struct FTNMenuNotice
{
	FText Text;
	bool bError = true;
	/** Abrir la pantalla «Unirse» al llegar (el rechazo vino al intentar entrar en una sala). */
	bool bOpenJoin = false;
};

/** @brief Entrada de la tabla de loot de cascos: id + peso para sorteo ponderado. */
USTRUCT(BlueprintType)
struct FTN_HelmetCrateEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	FName HelmetId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics", meta=(ClampMin="0.0"))
	float Weight = 1.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStatusChanged, const FString&, StatusMessage);

/**
 * @brief GameInstance global del proyecto. Sobrevive a los travels y centraliza:
 *  - Subsistema online (sesiones Steam): host, find/join, invite, destroy.
 *  - Persistencia de cosméticos (helmet + skin) vía UTN_CosmeticSaveGame.
 *  - Persistencia de score acumulado.
 *  - Loading screen entre mapas y status log.
 *  - PendingTravelPlayerCount: contador puente HQ → Run para saber cuántos esperar.
 *  - Auto-rejoin a la sesión Steam si el cliente pierde conexión durante un ServerTravel legítimo.
 */
UCLASS(Config=Game)
class TORTUNABO_API UMP_GameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UMP_GameInstance();

	/** @brief Registra delegates online, carga el saveGame de cosméticos y prepara el listener de network failures. */
	virtual void Init() override;

	/** @brief Desregistra delegates y libera handles de timer antes del shutdown del engine. */
	virtual void Shutdown() override;

	UPROPERTY(BlueprintAssignable, Category = "Multiplayer")
	FOnStatusChanged OnStatusChanged;

	/**
	 * @brief Crea la sesión de la sala activa (ActiveRoom; si no hay, una pública nueva con nombre y código al azar) y carga
	 *        el mapa lobby como servidor escucha.
	 */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer")
	void HostSession();

	/** @brief Busca salas públicas y se une a la primera en la que se pueda entrar (abierta y con sitio). */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer")
	void FindAndJoinSession();

	// ── Salas públicas y privadas (Docs/Salas.md) ───────────────────────────

	/**
	 * @brief Crea una sala: pública o privada, plazas (4, 6 u 8), nombre y código. Empieza de cero las listas de
	 *        expulsados y de miembros, y crea la sesión (HostSession) y viaja al lobby.
	 */
	void HostRoom(const FTNRoomConfig& Config);

	/** Borrador para la pantalla «Crear partida»: lo último elegido en esta ejecución, con un nombre y un código nuevos. */
	FTNRoomConfig MakeRoomDraft() const;

	/** Apunta lo elegido en «Crear partida» para la próxima vez. */
	void RememberRoomDraft(const FTNRoomConfig& Draft);

	/** Plazas que se pueden elegir (4, 6 y 8, sin pasar de MaxPlayers de DefaultGame.ini). */
	TArray<int32> GetRoomSizeOptions() const;

	/** Busca las salas públicas (hasta 200). Al acabar, OnRoomListChanged; mientras tanto, IsSearchingRooms. */
	void RefreshRoomList();

	/** true mientras hay una búsqueda de salas en marcha (la lista o un código). */
	bool IsSearchingRooms() const;

	/**
	 * true mientras se cierra la sesión vieja, se crea la sala, se entra en otra o se viaja a su mapa. Entonces «Crear» y
	 * «Unirse» no hacen nada: un segundo intento destruiría la sesión que se está creando.
	 */
	bool IsRoomTransitionBusy() const { return RoomOp.IsBusy(); }

	/** Salas públicas de la última búsqueda: primero las que tienen sitio, luego las más llenas. */
	const TArray<FTNRoomListing>& GetRoomListings() const { return RoomListings; }

	/** true si ya ha acabado alguna búsqueda de la lista (para distinguir «buscando» de «no hay salas»). */
	bool HasRoomListResult() const { return bRoomListReady; }

	/** Entra en una sala de la lista (índice en GetRoomListings). Si está cerrada o llena, lo dice sin intentarlo. */
	void JoinListedRoom(int32 ListingIndex);

	/** Busca la sala del código (pública o privada) y entra; si no existe, está cerrada o llena, lo dice. */
	void JoinRoomByCode(const FString& Code);

	/**
	 * @brief La sala en la que se está: en el anfitrión, la suya; en un invitado, la que replica ATN_RoomInfo (o, mientras
	 *        llega, la del anuncio de la sesión). false fuera de una partida en red.
	 */
	bool GetRoomSnapshot(FTNRoomSnapshot& Out) const;

	/** true en el anfitrión de una partida en red (puede cerrar la sala y expulsar). */
	bool CanManageRoom() const;

	/** Anfitrión: cierra (nadie nuevo entra; los que ya estaban sí pueden volver) o abre la sala. Actualiza el anuncio. */
	void SetRoomLocked(bool bLocked);

	/**
	 * @brief Anfitrión: expulsa a un jugador. Su cliente se va al menú con el aviso (ATN_RoomInfo) y, si no se ha ido en
	 *        2,5 s, el servidor lo echa (AGameSession::KickPlayer). No puede volver a esta sala mientras dure.
	 * @return false si no se puede (no eres el anfitrión, eres tú o no es un jugador conectado).
	 */
	bool KickFromRoom(APlayerState* Target);

	/** Cliente: el anfitrión me ha expulsado (lo llama ATN_RoomInfo). Vuelve al menú con el aviso. */
	void HandleKickedFromRoom(int32 RoomNameId);

	/** true si hay una sesión y un overlay de Steam para invitar a amigos. */
	bool CanInviteFriends() const;

	/** Aviso pendiente para el menú principal (lo deja vacío al leerlo). */
	FTNMenuNotice ConsumeMenuNotice();

	/** Deja un aviso para el menú principal (lo enseña al volver a él); sustituye al que hubiera. */
	void SetMenuNotice(const FTNMenuNotice& Notice) { PendingMenuNotice = Notice; }

	/** Mapa del menú principal. */
	const FString& GetMenuMapPath() const { return MenuMapPath; }

	/** true si World es el del menú principal. */
	bool IsInMenuWorld(const UWorld* World) const { return IsMenuWorld(World); }

	/** Olvida que el servidor iba a viajar: tras un viaje fallido, un corte de conexión no es un ServerTravel que reconectar. */
	void ClearPendingTravel() { bIsPendingTravel = false; }

	/** Avisos de las salas (la pantalla de salas del menú los enseña). */
	FTNOnRoomNotice OnRoomNotice;

	/** La lista de salas públicas ha cambiado (o ha empezado o acabado una búsqueda). */
	FSimpleMulticastDelegate OnRoomListChanged;

	/** @brief Destruye la sesión Steam actual liberando el slot. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer")
	void DestroyCurrentSession();

	/** @brief Abre el overlay de Steam con la lista de amigos para invitar. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer")
	void InviteFriends();

	/** @brief Vuelve al menú principal cerrando la sesión activa de forma limpia. */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer")
	void HandleReturnToMenu();

	/**
	 * Llamado por ClientNotifyServerTravel (Client RPC) en el lado del CLIENTE.
	 * Marca bIsPendingTravel = true y muestra loading screen, de modo que cuando
	 * el servidor destruya el NetDriver y el cliente reciba ConnectionLost,
	 * OnNetworkFailure active auto-rejoin en vez de destruir la sesión.
	 */
	void NotifyClientPendingTravel();

	/** @brief Muestra el widget de loading screen con un mensaje opcional. */
	UFUNCTION(BlueprintCallable, Category = "UI|Loading")
	void ShowLoadingScreen(const FString& Reason = TEXT("Cargando..."));

	/** @brief Quita el widget de loading screen del viewport. */
	UFUNCTION(BlueprintCallable, Category = "UI|Loading")
	void HideLoadingScreen();

	/** @brief Devuelve la lista de IDs de cascos desbloqueados del save game local. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	TArray<FName> GetUnlockedHelmetIds() const;

	/** @brief Indica si el casco está desbloqueado en el save game del jugador local. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool IsHelmetUnlocked(FName HelmetId) const;

	/** @brief Marca el casco como desbloqueado y persiste. Devuelve false si ya lo estaba. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool UnlockHelmet(FName HelmetId);

	/** @brief Equipa un casco YA desbloqueado. Devuelve false si no estaba en la lista. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool EquipHelmet(FName HelmetId);

	/**
	 * Equipa el helmet directamente sin verificar si está desbloqueado.
	 * Usado por las estatuas de cosmético del lobby (donde el helmet siempre está disponible).
	 * Si HelmetId != NAME_None, lo añade automáticamente a la lista de desbloqueados.
	 * NAME_None = desequipar (válido siempre).
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool ForceEquipHelmet(FName HelmetId);

	/** @brief Devuelve el ID del casco equipado actualmente (NAME_None si ninguno). */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	FName GetEquippedHelmetId() const;

	/** @brief Sortea un casco aleatorio según los pesos de HelmetCrateTable, lo desbloquea y devuelve su ID. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	FName OpenHelmetCrate();

	/** @brief Devuelve el log de status formateado (últimos MaxStatusLines mensajes). */
	FString BuildStatusLog() const;

	/**
	 * @brief Plazas de la sesión: en el anfitrión con sala, las de su sala (4, 6 u 8, las mismas que aplica el PreLogin);
	 *        si no, el tope de DefaultGame.ini (MaxPlayers, 8). En la partida local, cuatro (TNLocalPlay::MaxPlayers). Lo lee el
	 *        marcador «Sala: X/Y» del lobby (ATN_HQGameMode).
	 */
	UFUNCTION(BlueprintCallable, Category = "Multiplayer")
	int32 GetMaxPlayers() const;

	// ── Partida local (#311) ─────────────────────────────────────────────────

	/**
	 * @brief «Local» en el menú principal: hasta cuatro jugadores en este PC a pantalla partida, sin Steam ni sesión. El
	 *        jugador 1 va derecho al lobby (Standalone) y los mandos se unen allí con Start (UTN_LocalPlaySubsystem).
	 */
	void StartLocalGame();

	// ── Aspecto de cada jugador local (#311) ─────────────────────────────────
	// Las mismas operaciones que las de arriba para el jugador de PC: en red, o el jugador 1 de la partida local, su perfil
	// guardado; un invitado de la partida local, su aspecto de la partida (UTN_LocalPlayerProfile), que nunca se guarda.
	// Con PC nulo, el perfil guardado (como las de arriba).

	TArray<FName> GetUnlockedHelmetIdsFor(const APlayerController* PC) const;
	TArray<FName> GetUnlockedSkinIdsFor(const APlayerController* PC) const;
	bool IsCosmeticUnlockedFor(const APlayerController* PC, ETNCosmeticCategory Category, FName Id) const;
	bool PurchaseCosmeticFor(const APlayerController* PC, ETNCosmeticCategory Category, FName Id);
	bool EquipHelmetFor(const APlayerController* PC, FName HelmetId);
	bool ForceEquipHelmetFor(const APlayerController* PC, FName HelmetId);
	FName OpenHelmetCrateFor(const APlayerController* PC);
	bool EquipSkinFor(const APlayerController* PC, FName SkinId);
	bool EquipShellFor(const APlayerController* PC, FName ShellId);
	bool EquipEyesFor(const APlayerController* PC, FName EyesId);
	FName GetEquippedHelmetIdFor(const APlayerController* PC) const;
	FName GetEquippedSkinIdFor(const APlayerController* PC) const;
	FName GetEquippedShellIdFor(const APlayerController* PC) const;
	FName GetEquippedEyesIdFor(const APlayerController* PC) const;
	/** Saldo de puntos de la tienda de este jugador (#873): el de su perfil guardado, o el de la partida si es invitado. */
	int32 GetShopPointsFor(const APlayerController* PC) const;

	/**
	 * Caja sorpresa de la tienda (#873, TNMysteryBox::Open con UTN_PointsEconomy::MysteryBox): cobra la caja del saldo
	 * de PC, desbloquea la skin que sale o, si ya la tenía, devuelve parte de los puntos, y guarda. Sin saldo no cobra
	 * (bOpened = false). Stream decide la tirada (las pruebas pasan una semilla fija).
	 */
	FTN_MysteryBoxResult OpenMysteryBoxFor(const APlayerController* PC, FRandomStream& Stream);

	/** Lo que puede salir en la caja sorpresa: todas las filas de DT_Skins (caparazones, colores y ojos) con su rareza. */
	TArray<TNMysteryBox::FCandidate> GetMysteryBoxCandidates() const;

	/** Devuelve el DataTable de cascos para lookup externo (TortugaCharacter, widget). */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	UDataTable* GetHelmetDataTable() const { return HelmetDataTable; }

	/**
	 * @brief Busca una fila en HelmetDataTable. No loguea: cada llamador conserva su propio
	 *        logging (difiere sitio a sitio) y solo comprueba el nullptr de retorno.
	 * @param HelmetId Id de la fila buscada.
	 * @param Ctx Texto de contexto pasado a FindRow (aparece en el log interno de FindRow si falla).
	 * @return La fila encontrada, o nullptr si el DataTable no está asignado o el Id no existe.
	 */
	const FTN_HelmetData* FindHelmetRow(FName HelmetId, const TCHAR* Ctx) const;

	// ── Skin de personaje ────────────────────────────────────────────────────

	/** Equipa el skin de personaje indicado y lo persiste. NAME_None = sin skin. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool EquipSkin(FName SkinId);

	/** Devuelve el skin equipado actualmente (NAME_None = sin skin). */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	FName GetEquippedSkinId() const;

	// ── Tienda y probador (casco, caparazón y color por separado) ────────────

	/** NAME_None (el aspecto de serie) siempre está desbloqueado. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool IsCosmeticUnlocked(ETNCosmeticCategory Category, FName Id) const;

	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	int32 GetCosmeticPrice(ETNCosmeticCategory Category, FName Id) const;

	/**
	 * Compra de la tienda: si hay puntos para el precio, los descuenta del saldo (ShopPoints, #873), lo desbloquea y
	 * guarda. Devuelve true si queda desbloqueado.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool PurchaseCosmetic(ETNCosmeticCategory Category, FName Id);

	/** Catálogo de la tienda: filas de la categoría en el orden del DataTable (cascos sin malla y skins vacíos fuera). */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	TArray<FName> GetCosmeticCatalog(ETNCosmeticCategory Category) const;

	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	TArray<FName> GetUnlockedSkinIds() const;

	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool EquipShell(FName ShellId);

	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	FName GetEquippedShellId() const;

	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool EquipEyes(FName EyesId);

	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	FName GetEquippedEyesId() const;

	/** Devuelve el DataTable de skins para lookup externo. */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	UDataTable* GetSkinDataTable() const { return SkinDataTable; }

	/**
	 * @brief Busca una fila en SkinDataTable. No loguea: cada llamador conserva su propio
	 *        logging (difiere sitio a sitio) y solo comprueba el nullptr de retorno.
	 * @param SkinId Id de la fila buscada.
	 * @param Ctx Texto de contexto pasado a FindRow (aparece en el log interno de FindRow si falla).
	 * @return La fila encontrada, o nullptr si el DataTable no está asignado o el Id no existe.
	 */
	const FTN_SkinData* FindSkinRow(FName SkinId, const TCHAR* Ctx) const;

	// ── Race Score ───────────────────────────────────────────────────────────

	/**
	 * Añade puntos al marcador acumulado del jugador local y los persiste (#26).
	 * Llamado al entrar en Results cuando RaceScore del PlayerState es > 0.
	 */
	UFUNCTION(BlueprintCallable, Category = "Score")
	void AddRaceScore(int32 Points);

	/** Devuelve el total de puntos de carrera acumulados del jugador local. */
	UFUNCTION(BlueprintCallable, Category = "Score")
	int32 GetAccumulatedRaceScore() const;

	// ── Coleccionables ──────────────────────────────────────────────────────

	/**
	 * Suma muñecos tortuga al contador del perfil local y lo guarda (#797). Lo llama ATN_CoopGameState al entrar en
	 * Results, por diferencia con lo ya guardado en esa partida. No toca las conchas (AccumulatedRaceScore).
	 */
	UFUNCTION(BlueprintCallable, Category = "Collectibles")
	void AddTurtleDolls(int32 Count);

	/** Muñecos tortuga cogidos en total por el jugador local. */
	UFUNCTION(BlueprintPure, Category = "Collectibles")
	int32 GetTurtleDollsCollected() const;

	/**
	 * Suma los puntos de final de partida al perfil local (ganados en total y saldo de la tienda) y lo guarda (#789,
	 * #873). Lo llama ATN_CoopGameState al entrar en Results, por diferencia con lo ya guardado en esa partida.
	 */
	UFUNCTION(BlueprintCallable, Category = "Score")
	void AddCoopScore(int32 Points);

	/** Puntuación final del Coop acumulada por el jugador local. */
	UFUNCTION(BlueprintPure, Category = "Score")
	int32 GetAccumulatedCoopScore() const;

	/**
	 * Número de jugadores conectados en el lobby ANTES de hacer ServerTravel al Run.
	 * TN_HQGameMode lo asigna justo antes de viajar; TN_RunGameMode lo lee para
	 * saber cuántos jugadores esperar en el nuevo mapa.
	 * Persiste a través del non-seamless travel (GameInstance sobrevive).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Multiplayer")
	int32 PendingTravelPlayerCount = 0;

	/**
	 * Lobby del que salió la partida (ruta del paquete, sin el prefijo de PIE): lo apunta ATN_HQGameMode al empezar y
	 * ATN_RunGameMode (y el mapa procedural) vuelve ahí al acabar la ronda, sea cual sea su LobbyMapPath.
	 */
	UPROPERTY(Transient)
	FString LobbyReturnMapPath;

	/**
	 * Prueba (TN.Rooms.FakeError): simula un fallo al entrar en una sala sin necesitar otra instancia. locked, full,
	 * kicked y other pasan por el rechazo del servidor (y recargan el menú, como el motor tras un fallo al conectar);
	 * joinfull, gone y noaddress, por el fallo de JoinSession. Solo existe fuera de Shipping (en Shipping no se define:
	 * no llamarla desde código de juego).
	 */
	void DebugFakeRoomError(const FString& Kind);

protected:
	/** @brief Callback online: sesión Steam creada — dispara ServerTravel al mapa lobby. */
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);

	/** @brief Callback online: búsqueda de salas terminada — rellena la lista, entra con el código o en la primera libre. */
	void OnFindSessionsComplete(bool bWasSuccessful);

	/** @brief Callback online: join completado — resuelve connect string y conecta al host. */
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

	/** @brief Callback online: sesión destruida — relanza host/join si había uno pendiente. */
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	/** @brief Callback online: el jugador aceptó una invitación de Steam — guarda y hace join. */
	void OnSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);

	TSharedPtr<FOnlineSessionSearch> SessionSearch;

	/** Lobby al que va el anfitrión al crear la partida: el castillo de arena (LVL_HQ es el lobby antiguo). */
	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer")
	FString GameMapPath = TEXT("/Game/Maps/Lobby/LVL_Lobby");

	UPROPERTY(EditDefaultsOnly, Category = "Multiplayer")
	FString MenuMapPath = TEXT("/Game/Maps/Lobby/LVL_Menu");

	/**
	 * Plazas de la sesión de Steam (el anfitrión incluido). 8: la carrera en la playa está preparada para ocho (salida,
	 * nido del sprint, HUD, recuento y red: Docs/Modo_Carrera.md, «Red y rendimiento con 4 y 8 jugadores»). Se lee también de
	 * DefaultGame.ini ([/Script/Tortunabo.MP_GameInstance] MaxPlayers) para bajarlo sin compilar.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Multiplayer", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxPlayers = 8;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Multiplayer|Steam", meta=(ClampMin="1"))
	int32 SteamDevAppId = 480;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Loading")
	TSubclassOf<UUserWidget> LoadingScreenWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmetics")
	TArray<FName> DefaultUnlockedHelmets;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmetics")
	TArray<FTN_HelmetCrateEntry> HelmetCrateTable;

	UPROPERTY(EditDefaultsOnly, Category = "Cosmetics")
	FString CosmeticSaveSlotPrefix = TEXT("Cosmetics");

	/**
	 * DataTable con filas FTN_HelmetData (ID, mesh, icono, escala, offset).
	 * Asigna DT_Helmets aquí en BP_GameInstance → Class Defaults.
	 * Usado por TortugaCharacter::UpdateHelmetMesh para instanciar el mesh del casco.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmetics")
	TObjectPtr<UDataTable> HelmetDataTable;

	/**
	 * DataTable con filas FTN_SkinData (ID, material de cuerpo, icono).
	 * Asigna DT_Skins aquí en BP_GameInstance → Class Defaults.
	 * Usado por TortugaCharacter::UpdateSkinVisual para cambiar el material del personaje.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmetics")
	TObjectPtr<UDataTable> SkinDataTable;

	/** @brief Añade un mensaje al log circular y emite OnStatusChanged. */
	void UpdateStatus(const FString& Message);

	static constexpr int32 MaxStatusLines = 12;
	TArray<FString> StatusLog;

	/** @brief Último aviso de sala apuntado en StatusLog; el siguiente aviso lo sustituye. */
	FString LastRoomNoticeStatus;

private:
	/** @brief Devuelve la interfaz online de sesiones (o nullptr si OnlineSubsystem no está disponible). */
	IOnlineSessionPtr GetSessionInterface() const;

	/** Qué hace ahora con la sesión de la sala (cerrar la vieja, crear, entrar o viajar); RoomOpStartTime: desde cuándo. */
	FTNRoomOpState RoomOp;
	double RoomOpStartTime = 0.0;
	FOnlineSessionSearchResult PendingInviteResult;

	/** Empieza a entrar en una sesión (JoinSession): apunta la operación y, si ni arranca ni avisa, la da por fallida. */
	void BeginSessionJoin(const FOnlineSessionSearchResult& Result, int32 ControllerId);

	/** Una operación de sesión que no contesta a tiempo: se deshace (cierra la sesión, quita la pantalla de carga y avisa). */
	void AbortRoomOperation();

	/**
	 * true durante el intervalo entre PreLoadMap y PostLoadMap.
	 * Usado para diferenciar errores de red en inicio de conexión (sesión zombi)
	 * vs. errores transitorios durante un ServerTravel normal (no destruir sesión).
	 */
	bool bIsPendingTravel = false;

	/** @brief Hook de fallo de red: decide si reconectar (auto-rejoin), reintentar listen o destruir sesión. */
	void OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);

	/** @brief Rama de OnNetworkFailure para fallos de driver/listen server (inicio de conexión o durante travel). */
	void HandleDriverFailure(const FString& FailureTypeStr, const FString& ErrorString);

	/** @brief Rama de OnNetworkFailure para NetChecksumMismatch (build incompatible con el servidor). */
	void HandleChecksumMismatch(const FString& ErrorString);

	/** @brief Rama de OnNetworkFailure para desconexiones de cliente (pérdida de conexión, timeout, etc.). */
	void HandleConnectionLost(const FString& FailureTypeStr);

	/** @brief Garantiza que existe el fichero steam_appid.txt junto al ejecutable. */
	void EnsureSteamAppIdFile();

	/** @brief Hook pre-load del mapa: muestra loading screen y captura URL para retries. */
	void HandlePreLoadMap(const FString& MapName);

	/** @brief Hook post-load del mapa: oculta loading y reanuda listen retry si aplica. */
	void HandlePostLoadMap(UWorld* LoadedWorld);

	/** @brief Actualiza el texto del loading screen sin destruir el widget. */
	void RefreshLoadingText(const FString& Reason) const;

	/** @brief Carga el UTN_CosmeticSaveGame del slot (o crea uno vacío). */
	void LoadCosmeticProfile();

	/** @brief Persiste el UTN_CosmeticSaveGame en disco. */
	void SaveCosmeticProfile() const;

	/** Perfil de aspecto de PC: el guardado (PC nulo, en red o el jugador 1) o el de la partida de un invitado local. */
	UTN_CosmeticSaveGame* CosmeticsFor(const APlayerController* PC) const;

	/** Guarda el perfil de PC si es el guardado (lo de un invitado local dura la partida: TNLocalPlay::ShouldSave). */
	void SaveCosmeticsFor(const APlayerController* PC) const;

	/** Desbloquea un casco en el perfil de PC (y lo guarda si es el guardado). */
	bool UnlockHelmetFor(const APlayerController* PC, FName HelmetId);

	/** @brief Construye el nombre de slot del save (incluye sufijo de Steam ID si está disponible). */
	FString BuildCosmeticSaveSlot() const;

	/** Reintenta crear el listen server tras un NetDriverListenFailure durante travel. */
	void RetryListenServer();

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> LoadingScreenWidget;

	UPROPERTY(Transient)
	TObjectPtr<UTN_CosmeticSaveGame> CosmeticProfile;

	/** SteamID64 de la cuenta dueña del perfil cosmético; vacío sin Steam (ranura _Local). Se fija al cargarlo. */
	FString CosmeticAccountId;

	/** El perfil cosmético del disco no se pudo leer ni apartar: no se escribe encima en esta sesión. */
	bool bCosmeticSaveBlocked = false;

	bool bIsLoadingScreenVisible = false;

	/** true si HandlePostLoadMap debe reintentar crear el listen server. */
	bool bNeedsListenRetry = false;

	/** URL pendiente para el listen retry (guardada desde HandlePreLoadMap). */
	FString PendingListenURL;

	/** Reintentos restantes para el listen server. */
	int32 ListenRetryCount = 0;

	/** Máximo de reintentos para crear el listen server. */
	static constexpr int32 MaxListenRetries = 5;

	/** Timer para reintentos de listen. */
	FTimerHandle ListenRetryTimerHandle;

	FDelegateHandle InviteAcceptedDelegateHandle;

	// ── Auto-rejoin tras ConnectionLost durante travel ─────────────────────
	/**
	 * true cuando el cliente pierde conexión durante un travel legítimo del servidor.
	 * En vez de destruir la sesión, intentamos reconectar vía Steam session.
	 */
	bool bPendingAutoRejoin = false;

	int32 AutoRejoinRetryCount = 0;
	static constexpr int32 MaxAutoRejoinRetries = 8;

	FTimerHandle AutoRejoinTimerHandle;

	/** Intenta reconectar al host resolviendo el connect string de la sesión Steam. */
	void AttemptAutoRejoin();

	// ── Salas ───────────────────────────────────────────────────────────────

	/** La sala de este anfitrión (válida con bHasActiveRoom). */
	FTNRoomConfig ActiveRoom;
	bool bHasActiveRoom = false;

	/** Lo último elegido en «Crear partida» (modo, pública o privada, plazas). */
	FTNRoomConfig RoomDraft;
	bool bHasRoomDraft = false;

	/** Quién ha estado en la sala (id único): con la sala cerrada, estos sí pueden volver a entrar. */
	TSet<FString> RoomMemberIds;

	/** Expulsados de la sala (id único): no vuelven a entrar mientras dure. */
	TSet<FString> KickedRoomIds;

	/** Salas públicas de la última búsqueda y la búsqueda que las encontró (sus resultados sirven para unirse). */
	TArray<FTNRoomListing> RoomListings;
	TSharedPtr<FOnlineSessionSearch> RoomListSearch;
	bool bRoomListReady = false;

	/** Búsqueda en marcha (en SessionSearch), cuándo empezó y la que espera turno (solo puede haber una a la vez). */
	ETNRoomSearch RoomSearchPurpose = ETNRoomSearch::None;
	FString RoomSearchCode;
	double RoomSearchStartTime = 0.0;
	int32 RoomSearchSerial = 0;
	ETNRoomSearch QueuedRoomSearch = ETNRoomSearch::None;
	FString QueuedRoomCode;

	/** Resultado al que se entra después de cerrar la sesión vieja (unirse desde la lista o con código). */
	FText PendingJoinRoomName;

	/** Lo último anunciado en la sesión (para actualizarla solo si cambia). */
	int32 AdvertisedPlayers = INDEX_NONE;
	int32 AdvertisedLocked = INDEX_NONE;

	/** Aviso que verá el menú principal al volver a él. */
	FTNMenuNotice PendingMenuNotice;

	/** Cliente expulsado camino del menú: sin reconexión automática. */
	bool bKickedFromRoom = false;

	TWeakObjectPtr<ATN_RoomInfo> RoomInfoActor;

	FTimerHandle RoomTickHandle;
	FDelegateHandle PreLoginHandle;
	FDelegateHandle PostLoginHandle;

	/** Cada segundo: en el anfitrión, la sala (ATN_RoomInfo y anuncio de la sesión); en todos, el plazo de las búsquedas. */
	void RoomTick();

	/** Servidor: rechaza a quien llegue con la sala cerrada o llena, o expulsado (FGameModeEvents, todos los GameModes). */
	void HandleGameModePreLogin(AGameModeBase* GameMode, const FUniqueNetIdRepl& NewPlayer, FString& ErrorMessage);

	/** Servidor: apunta al que entra como miembro de la sala. */
	void HandleGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);

	/** Anfitrión sin sala elegida (p. ej. servidor escucha del editor): una pública con nombre y código al azar. */
	void EnsureActiveRoom();

	/** Servidor: el ATN_RoomInfo de ese mundo (lo crea si no hay) con la sala al día. */
	ATN_RoomInfo* EnsureRoomInfo(UWorld* World);

	/** Anfitrión: vuelve a anunciar la sesión (cerrada, jugadores y modo). */
	void UpdateRoomAdvertisement();

	/** Escribe en la sesión los ajustes de la sala activa. */
	void ApplyRoomSettings(FOnlineSessionSettings& Settings, int32 Players) const;

	/** Al volver al menú: sin sala activa ni listas de la anterior. */
	void ResetRoomState();

	/** Tortugas en la partida de ese mundo (el anfitrión incluido, sin bots). */
	static int32 CountRoomPlayers(const UWorld* World);

	/** true si el mundo es el del menú principal. */
	bool IsMenuWorld(const UWorld* World) const;

	/** Busca salas para Purpose (con Steam, filtrando ya en el servidor). Si hay otra en marcha, espera su turno. */
	void StartRoomSearch(ETNRoomSearch Purpose, const FString& Code = FString());

	/** Lee una sala de una sesión (resultado de búsqueda o la sesión en la que se está); false si no es de Tortunavy. */
	static bool ReadRoomListing(const FOnlineSession& Session, int32 Index, FTNRoomListing& Out);

	/** Entra en la sala de ese resultado (cierra antes la sesión vieja si la hay). */
	void JoinRoomResult(const FOnlineSessionSearchResult& Result, const FText& RoomName);

	/** El servidor no nos ha dejado entrar (TNRoomKeys::Refuse...): aviso claro y de vuelta al menú. */
	void HandleRoomRefused(const FString& Reason);

	/** Aviso de las salas: al registro de estado y a OnRoomNotice. */
	void PostRoomNotice(const FText& Message, bool bError);
};
