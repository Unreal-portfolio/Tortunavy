#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class AActor;
class AController;
class AGameModeBase;
class AGameStateBase;
class APawn;
class APlayerController;
class APlayerStart;
class APlayerState;
class UWorld;

/**
 * @brief Baraja PlayerStarts (Fisher-Yates) y devuelve el primero sin ningún pawn ajeno a menos de 200cm.
 * @param World Mundo donde iterar los pawns actuales.
 * @param PlayerStarts Pool de candidatos; se baraja in-place (el llamador puede usar PlayerStarts[0] como fallback tras la llamada).
 * @param Player Controller que va a poseer el spawn — se excluye de la comprobación de ocupación.
 * @return El primer PlayerStart libre encontrado, o nullptr si todos están ocupados.
 * @note Compartido por ATN_RunGameMode y ATN_HQGameMode::ChoosePlayerStart_Implementation.
 *       El manejo del pool vacío y del fallback "todos ocupados" queda en cada llamador
 *       (difieren en logging entre Run y HQ).
 */
AActor* TN_PickUnoccupiedPlayerStart(UWorld* World, TArray<AActor*>& PlayerStarts, AController* Player);

/**
 * @brief Como TN_PickUnoccupiedPlayerStart, pero si todos los PlayerStart están ocupados (partidas de más jugadores que
 *        PlayerStart tiene el mapa: hasta 8) crea uno nuevo junto a los del mapa en vez de apilar al jugador encima.
 * @details Los sitios nuevos salen de desplazar un PlayerStart existente a los lados o hacia atrás, en pasos de 2,2 m
 *          (o de ~1,1 m si no cabe así), en el orden en que menos se alejan. Cada uno se comprueba: hay suelo firme
 *          y casi a la misma cota que el de origen, cabe la cápsula del peón, un barrido desde el origen no choca con
 *          nada y no queda encima de otro peón. Entre los sitios posibles de la misma distancia se elige el más
 *          despejado. Los PlayerStart del mapa se usan igual que antes: solo se crea uno nuevo cuando no queda ninguno
 *          libre. Los nuevos son actores de servidor, copian la etiqueta y la orientación del PlayerStart de origen, se
 *          añaden a PlayerStarts y se reutilizan en llamadas siguientes (el jugador que se va deja libre el suyo).
 * @param World Mundo donde iterar los peones actuales y hacer las comprobaciones.
 * @param PlayerStarts Pool de candidatos; se baraja in-place y, si se crea un sitio nuevo, se le añade.
 * @param Player Controller que va a poseer el spawn: su peón, si tiene, no cuenta como ocupante.
 * @param PawnClass Clase del peón que va a aparecer (su cápsula manda en las comprobaciones); nula = una tortuga normal.
 * @param LogTag Prefijo de log ("Run" / "Lobby").
 * @return Un PlayerStart libre (del pool o nuevo), o nullptr si no hay sitio ni se pudo crear uno: el llamador usa su respaldo.
 */
AActor* TN_PickSpreadPlayerStart(UWorld* World, TArray<AActor*>& PlayerStarts, AController* Player, TSubclassOf<APawn> PawnClass, const TCHAR* LogTag);

/**
 * @brief Garantiza que el jugador tenga un pawn vivo: RestartPlayer, y si sigue sin pawn,
 *        busca PlayerStart (o usa FallbackProvider) y spawnea+posee un pawn por defecto.
 * @param GameMode GameMode que orquesta el spawn (RestartPlayer/FindPlayerStart/SpawnDefaultPawnFor/SetPlayerDefaults son públicos en AGameModeBase).
 * @param PlayerController Jugador a garantizar con pawn.
 * @param FallbackProvider Invocado solo si FindPlayerStart no encuentra nada; produce el PlayerStart de emergencia del GameMode.
 * @param LogTag Prefijo de log ("Run" / "Lobby").
 * @note Compartido por ATN_RunGameMode::EnsurePlayerSpawned y ATN_HQGameMode::EnsurePlayerSpawned.
 */
void TN_EnsurePlayerSpawned(AGameModeBase* GameMode, APlayerController* PlayerController, TFunctionRef<APlayerStart* ()> FallbackProvider, const TCHAR* LogTag);

/**
 * @brief Devuelve el primer APlayerStart existente en el mundo, o spawnea uno de emergencia en el origen si no hay ninguno.
 * @param World Mundo donde buscar/spawnear.
 * @param SpawnActorName Nombre único del actor de emergencia (evita colisión de nombres entre mapas).
 * @param LogTag Prefijo de log ("Run" / "Lobby").
 * @param MapDescriptor Texto insertado en el warning ("run map" / "lobby map").
 * @note Compartido por ATN_RunGameMode::EnsureFallbackPlayerStart y ATN_HQGameMode::EnsureFallbackPlayerStart.
 */
APlayerStart* TN_EnsureFallbackPlayerStart(UWorld* World, FName SpawnActorName, const TCHAR* LogTag, const TCHAR* MapDescriptor);

/**
 * @brief true si el PlayerState es de un jugador que se está yendo de la partida y no debe contar.
 * @details AController::Destroyed llama a GameMode->Logout antes de CleanupPlayerState, y AGameMode::AddInactivePlayer
 *          solo saca del PlayerArray la copia inactiva: durante Logout el PlayerState del que se va sigue en
 *          GameState->PlayerArray. Se reconoce porque su dueño (el controlador) se está destruyendo. También cuentan
 *          como idos los PlayerState nulos, en destrucción o inactivos.
 * @note Sin esto, si se iba el único jugador que faltaba por llegar (Clásico, Coop) o por ponerse listo (lobby), la
 *       ronda o la cuenta atrás no se reevaluaban nunca (#558, #559).
 */
bool TN_IsPlayerStateLeaving(const APlayerState* PlayerState);

/**
 * @brief true si el PlayerState es de un bot: marcado con IsABot o con un controlador que no es de jugador como dueño.
 * @details APlayerState::PostInitializeComponents marca IsABot cuando el dueño es un controlador sin jugador; lo
 *          segundo cubre al PlayerState que se le asigna después a ese controlador (SetPlayerState) sin pasar por ahí.
 * @note Los recuentos del lobby (todos listos, PendingTravelPlayerCount) no cuentan bots (#694).
 */
bool TN_IsBotPlayerState(const APlayerState* PlayerState);

/**
 * @brief Cuenta cuántos elementos del PlayerArray son ATN_CoopPlayerState (jugadores coop conectados), sin contar al
 *        que se está yendo (TN_IsPlayerStateLeaving) ni a los bots (TN_IsBotPlayerState, #694).
 * @param GameState GameState del que iterar PlayerArray; nulo devuelve 0.
 * @note Compartido por ATN_RunGameMode y ATN_HQGameMode: 7 sitios reimplementaban el mismo bucle
 *       de conteo (solo cambiaba el nombre de la variable acumuladora).
 */
int32 TN_CountConnectedCoopPlayers(const AGameStateBase* GameState);

/** Recuento de la ronda de Clásico y Coop sobre los jugadores que siguen en la partida. */
struct FTNCoopRoundCount
{
	/** Jugadores coop que siguen en la partida. */
	int32 Total = 0;
	/** Los que ya han terminado la ronda (bHasFinishedRun: meta o eliminados). */
	int32 Resolved = 0;
	/** Los que siguen vivos. */
	int32 Alive = 0;

	/** La ronda acaba cuando hay alguien y o bien todos la han terminado o no queda nadie vivo. */
	bool IsRoundOver() const { return Total > 0 && (Resolved >= Total || Alive == 0); }
};

/**
 * @brief Cuenta Total, Resolved y Alive sobre los ATN_CoopPlayerState del PlayerArray, sin el que se está yendo.
 * @param GameState GameState del que iterar PlayerArray; nulo devuelve todo a 0.
 * @note Lo usa ATN_RunGameMode::UpdateRoundProgressAndMaybeFinish, también desde Logout (#558).
 */
FTNCoopRoundCount TN_CountCoopRound(const AGameStateBase* GameState);

/** Recuento de jugadores listos en el lobby. */
struct FTNLobbyReadyCount
{
	/** Jugadores coop conectados que siguen en la partida. */
	int32 Connected = 0;
	/** Los que están en la zona de listos (bIsInReadyZone). */
	int32 Ready = 0;

	/** Todos los conectados (y al menos MinPlayers) están listos: arranca la cuenta atrás. */
	bool AllReady(int32 MinPlayers) const { return Connected > 0 && Connected >= MinPlayers && Ready >= Connected; }
};

/**
 * @brief Cuenta conectados y listos sobre los ATN_CoopPlayerState del PlayerArray, sin el que se está yendo ni los bots
 *        (#694: un bot nunca se pone listo y bloqueaba la cuenta atrás).
 * @param GameState GameState del que iterar PlayerArray; nulo devuelve todo a 0.
 * @note Lo usan ATN_HQGameMode::RefreshLobbyState (también desde Logout, #559) y TickCountdown.
 */
FTNLobbyReadyCount TN_CountLobbyReady(const AGameStateBase* GameState);

/** Largo máximo de un nombre de jugador: el de Steam (32). */
constexpr int32 TN_MaxPlayerNameLength = 32;

/**
 * @brief Devuelve al jugador su nombre completo tras AGameModeBase::InitNewPlayer, que corta la opción ?Name= a 20
 *        caracteres (un nombre de Steam de hasta 32 llegaba recortado al HUD, al tendero y a los resultados).
 * @param GameMode GameMode que acaba de inicializar al jugador.
 * @param PlayerController Jugador recién entrado.
 * @param Options Opciones del login (las mismas que recibió InitNewPlayer).
 * @note Se llama desde InitNewPlayer de ATN_HQGameMode y ATN_RunGameMode, justo después de Super.
 */
void TN_RestoreFullPlayerName(AGameModeBase* GameMode, APlayerController* PlayerController, const FString& Options);
