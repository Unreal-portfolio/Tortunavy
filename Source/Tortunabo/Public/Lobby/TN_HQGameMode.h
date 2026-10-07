#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "Core/TN_MatchFlowTypes.h"
#include "TN_HQGameMode.generated.h"

class APlayerController;
class APlayerStart;
struct FUniqueNetIdRepl;

/**
 * @brief GameMode del lobby HQ (LVL_HQ). Gestiona ready-up, countdown y travel a LVL_Demo01.
 *
 * Responsabilidades:
 *  - Spawn de jugadores. Hasta ocho: si los PlayerStart del mapa (cuatro) se acaban, los siguientes salen en sitios nuevos
 *    junto a ellos (TN_PickSpreadPlayerStart).
 *  - Lectura del estado ready (ATN_LobbyReadyZone) y countdown cuando todos los conectados están listos.
 *  - Reseteo del countdown si alguien sale de la zona o se desconecta.
 *  - Seamless Travel hacia LVL_Demo01 con persistencia de PendingTravelPlayerCount en GameInstance.
 */
UCLASS()
class TORTUNABO_API ATN_HQGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	ATN_HQGameMode();

	/** @brief Inicializa estado y coloca tiendas y castillo. */
	virtual void BeginPlay() override;

	/** @brief Selecciona un PlayerStart libre (nunca los que llevan la etiqueta de la zona de tutorial antigua). */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** @brief Recupera el nombre completo del jugador que llega en la URL de entrada. */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options,
		const FString& Portal = TEXT("")) override;

	/** @brief Hook post-spawn: aplica cosméticos guardados (helmet/skin) al pawn recién creado. */
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	/** @brief Login de jugador nuevo (no por seamless travel): registra como conectado. */
	virtual void PostLogin(APlayerController* NewPlayer) override;

	/** @brief Logout: actualiza ConnectedPlayers en GameState y resetea countdown si quedan pocos. */
	virtual void Logout(AController* Exiting) override;

	/**
	 * @brief Seamless travel: limpia estado de espectador del PC ANTES de que UE intente respawnearlo.
	 * @note Evita que un jugador eliminado en la run anterior llegue al lobby ya en modo espectador.
	 */
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;

	/** Un ServerTravel a un mapa que no existe no empieza (UTN_TravelFailureSubsystem::CanServerTravelTo): los invitados no lo reciben. */
	virtual bool CanServerTravel(const FString& URL, bool bAbsolute) override;

	/**
	 * @brief Setup final tras seamless travel: re-aplica cosméticos a todos los jugadores que han vuelto.
	 *        Usa retry timer (5 × 0.1s) para cubrir la race PlayerState/Pawn possession.
	 */
	virtual void PostSeamlessTravel() override;

	/**
	 * @brief Marca el estado ready/not-ready de un jugador y refresca el countdown del lobby.
	 * @param PlayerController Jugador cuyo estado cambia.
	 * @param bReady true = entrar en zona ready; false = salir.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void SetPlayerReadyState(APlayerController* PlayerController, bool bReady);

protected:
	/**
	 * Plazas que enseña el marcador «Sala: X/Y» del lobby (ATN_CoopGameState::ExpectedPlayers). 0 = las de la sesión
	 * (UMP_GameInstance::GetMaxPlayers, ocho); mayor que 0 lo fija a mano.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Lobby")
	int32 LobbyExpectedPlayers = 0;

	/** Mínimo de jugadores conectados para que el countdown pueda arrancar. Default=1 para pruebas en solitario. */
	UPROPERTY(EditDefaultsOnly, Category = "Lobby")
	int32 LobbyMinPlayersForStart = 1;

	/**
	 * Etiqueta de los PlayerStart de la zona de tutorial antigua, que siguen colocados en el nivel: se quedan fuera del
	 * reparto normal.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Lobby")
	FName TutorialStartTag = TEXT("TutorialStart");

	UPROPERTY(EditDefaultsOnly, Category = "Lobby")
	int32 CountdownStartValue = 3;

	UPROPERTY(EditDefaultsOnly, Category = "Lobby")
	float CinematicDelaySeconds = 2.0f;

	/** Nivel de la partida (modo único): LVL_Demo01, que lleva BP_RunGameMode como GameMode Override. */
	UPROPERTY(EditDefaultsOnly, Category = "Lobby")
	FString MatchMapPath = TEXT("/Game/Maps/Run/LVL_Demo01");

private:
	FTimerHandle CountdownTimerHandle;
	FTimerHandle TravelTimerHandle;
	bool bCountdownRunning = false;
	int32 CurrentCountdownValue = 0;

	/** @brief Recalcula ConnectedPlayers/PlayersInStartZone y decide si arrancar/parar el countdown. */
	void RefreshLobbyState();

	/** @brief Arranca el countdown si están las condiciones (todos ready, mínimo conectados). */
	void StartCountdown();

	/** @brief Tick 1Hz del countdown. Al llegar a 0 dispara BeginMatchTravel. */
	void TickCountdown();

	/** @brief Para el countdown y vuelve al estado WaitingForPlayers (alguien dejó la zona ready). */
	void ResetCountdown();

	/** @brief Persiste PendingTravelPlayerCount en GameInstance y hace ServerTravel a MatchMapPath. */
	void BeginMatchTravel();

	/** @brief Spawnea el pawn del jugador si todavía no lo tiene (cubre re-entries tras travel). */
	void EnsurePlayerSpawned(APlayerController* PlayerController);

	/** @brief Devuelve un PlayerStart libre como fallback si no hay otros candidatos. */
	APlayerStart* EnsureFallbackPlayerStart();

	/** @brief Cambia el MatchFlowState replicado + dispara broadcast manual a listen-server. */
	void SetFlowState(ETNMatchFlowState NewState) const;

	/**
	 * @brief Coloca la tienda (ATN_ShopKeeper) y los probadores (ATN_ChangingBooth) si el nivel no los trae puestos.
	 *        Sitios: actores con la etiqueta TN_ShopAnchor / TN_BoothAnchor; si no hay, los de la maqueta del lobby
	 *        (el tendero es la tortuga grande junto a la carpa; los probadores, las botellas BP_VestidorBotella).
	 */
	void SpawnLobbyShops();
};
