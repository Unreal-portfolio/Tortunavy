#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/PlayerState.h"
#include "Core/TN_CosmeticsTypes.h"
#include "TN_CoopPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRaceScoreChanged, int32, NewScore);
/** Una concha de este jugador acaba de sumar: Value puntos, Tier (TNScoreShells::ETier) y dónde estaba (mundo). */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnScoreShellCollected, int32 /*Value*/, uint8 /*Tier*/, const FVector& /*WorldLocation*/);

class ATortugaCharacter;
class ATN_CoopPlayerState;

/** Un PlayerState ha cambiado de buggy del Rally (en cualquier máquina): lo escucha ATN_Buggy para repintarse ya. */
DECLARE_MULTICAST_DELEGATE_OneParam(FTNOnBuggyLookChanged, const ATN_CoopPlayerState* /*PlayerState*/);

/**
 * @brief PlayerState replicado por jugador — contiene estado individual de partida y cosméticos equipados.
 *
 * Datos replicados:
 *  - Flujo: bIsInReadyZone, bHasFinishedRun, bIsAlive, bIsEliminated, bIsDBNO.
 *  - Métricas: FinishRank, FinishTimeSeconds, RaceScore (con delegate OnRaceScoreChanged).
 *  - Tiempos visibles en HUD: DBNOBleedoutTimeRemaining, DeathZoneTimeRemaining.
 *  - Cosméticos equipados: EquippedHelmetId, EquippedSkinId (ambos con OnRep).
 *
 * Rate-limit server-side: timestamps para QuickChat y emotes (anti-spam por jugador).
 */
UCLASS()
class TORTUNABO_API ATN_CoopPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ATN_CoopPlayerState();

	/** Disparado en clientes cuando RaceScore cambia. HUD se suscribe en NativeConstruct. */
	UPROPERTY(BlueprintAssignable, Category = "Coop|Score")
	FOnRaceScoreChanged OnRaceScoreChanged;

	/**
	 * @brief Suma Delta al RaceScore (server-authoritative) y refresca el HUD del host.
	 * @param Delta Puntos a sumar (puede ser negativo; ignora 0).
	 * @note En el listen-server OnRep_RaceScore NO dispara en la máquina con autoridad,
	 *       así que difundimos OnRaceScoreChanged manualmente aquí para que el HUD del
	 *       host se actualice en vivo (mismo patrón que Server_UpsertRaceResult). Usar
	 *       este método en todos los sitios server que suman score (pickups, zonas, finish).
	 */
	void AddRaceScore(int32 Delta);

	/**
	 * Solo en la máquina de este jugador: una concha suya acaba de sumar (lo escucha el HUD para que los iconos vuelen
	 * al contador). Lo difunde MulticastScoreShellCollected.
	 */
	FOnScoreShellCollected OnScoreShellCollected;

	/**
	 * Servidor (ATN_ScorePickup, tras AddRaceScore): este jugador ha cogido una concha de Value puntos y tamaño Tier
	 * (TNScoreShells::ETier) en WorldLocation. Cada máquina con pantalla hace allí el estallido (ATN_ScoreShellBurst:
	 * destello, chispas y «¡plin!»); la del propio jugador, además, difunde OnScoreShellCollected. Va por el
	 * PlayerState (que no se destruye ni duerme, así que da igual que la concha se destruya justo después) y no fiable:
	 * es solo lo que se ve y se oye (los puntos van en RaceScore, y el contador del HUD acaba siempre en él); con ocho
	 * jugadores cogiendo conchas, un multicast fiable por concha llenaba los búferes de fiables.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastScoreShellCollected(FVector_NetQuantize10 WorldLocation, uint8 Tier, int32 Value);

	/**
	 * @brief Resetea el estado a valores de ARRANQUE de carrera (vivo, sin finish/DBNO/
	 *        eliminación, timers a -1, RaceScore 0). Server-side.
	 * @note Usar al INICIAR una run (BeginPlay/PostLogin/PostSeamlessTravel). NO en un
	 *       revive — el revive conserva el RaceScore ganado (ver TN_RunGameMode::RevivePlayer).
	 */
	void ResetForNewRace();

	/**
	 * @brief Indica si el servidor puede aceptar otro QuickChat de este jugador respetando el cooldown.
	 * @param Now Tiempo actual del servidor (s).
	 * @param CooldownSeconds Cooldown configurado entre mensajes (s).
	 */
	bool CanServerSendQuickChat(float Now, float CooldownSeconds) const;

	/** @brief Marca el tiempo del último QuickChat aceptado (server-side). */
	void MarkServerQuickChatSent(float Now);

	/**
	 * @brief Indica si el servidor puede aceptar otro emote del tipo dado respetando su cooldown.
	 * @param EmoteID ID del emote consultado.
	 * @param Now Tiempo actual del servidor (s).
	 * @param CooldownSeconds Cooldown configurado para emotes (s).
	 */
	bool CanServerPlayEmote(uint8 EmoteID, float Now, float CooldownSeconds) const;

	/** @brief Marca el tiempo del último emote aceptado de este tipo (server-side). */
	void MarkServerEmotePlayed(uint8 EmoteID, float Now);

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop")
	bool bIsInReadyZone = false;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop")
	bool bHasFinishedRun = false;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop")
	bool bIsAlive = true;

	/**
	 * Down But Not Out: true cuando el jugador está incapacitado pero aún no muerto.
	 * Los compañeros pueden revivirle vía emote en rango. Si expira el bleedout, muere.
	 */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop|DBNO")
	bool bIsDBNO = false;

	/**
	 * Segundos restantes antes de que el DBNO bleed-out termine y el jugador muera definitivamente.
	 * -1 = no está en DBNO. Replicado sólo al owner (para la barra de bleedout del HUD).
	 */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop|DBNO")
	float DBNOBleedoutTimeRemaining = -1.f;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop")
	float DeathZoneTimeRemaining = -1.f;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_EquippedHelmetId, Category = "Cosmetics")
	FName EquippedHelmetId = NAME_None;

	/** @brief OnRep de EquippedHelmetId: reaplica el mesh del casco en el pawn local. */
	UFUNCTION()
	void OnRep_EquippedHelmetId();

	/** ID del skin de personaje activo. NAME_None = aspecto por defecto del BP. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_EquippedSkinId, Category = "Cosmetics")
	FName EquippedSkinId = NAME_None;

	/** @brief OnRep de EquippedSkinId: reaplica los materiales del skin en el pawn local. */
	UFUNCTION()
	void OnRep_EquippedSkinId();

	/**
	 * Caparazón equipado (fila de DT_Skins de categoría Shell, de la tienda). Manda sobre la ranura del caparazón que
	 * ponga el color (EquippedSkinId). NAME_None = el de serie o el del color.
	 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_EquippedShellId, Category = "Cosmetics")
	FName EquippedShellId = NAME_None;

	/** @brief OnRep de EquippedShellId: reaplica los materiales en el pawn local. */
	UFUNCTION()
	void OnRep_EquippedShellId();

	/** Ojos equipados (fila de DT_Skins de categoría Eyes). NAME_None = los clásicos. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_EquippedEyesId, Category = "Cosmetics")
	FName EquippedEyesId = NAME_None;

	/** @brief OnRep de EquippedEyesId: reaplica los materiales en el pawn local. */
	UFUNCTION()
	void OnRep_EquippedEyesId();

	/**
	 * Buggy del Rally equipado (modelo y pintura del catálogo de Vehicles/TN_BuggyCosmetics.h). Lo escribe solo el
	 * servidor tras validarlo (SetEquippedBuggyLook) y lo pinta el ATN_Buggy que conduce esta jugadora en todas las
	 * máquinas. NAME_None = el de serie.
	 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_EquippedBuggyLook, Category = "Cosmetics")
	FTN_BuggyLook EquippedBuggyLook;

	/** Servidor: pone el buggy (ya validado), lo empuja a los clientes y lo aplica aquí (el anfitrión no recibe OnRep). */
	void SetEquippedBuggyLook(const FTN_BuggyLook& Look);

	/** @brief OnRep de EquippedBuggyLook: avisa (OnAnyBuggyLookChanged) para que se repinte el buggy que conduce. */
	UFUNCTION()
	void OnRep_EquippedBuggyLook();

	/** Cualquier PlayerState que cambia de buggy (OnRep, o en el servidor al ponérselo). */
	static FTNOnBuggyLookChanged OnAnyBuggyLookChanged;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop")
	float FinishTimeSeconds = -1.f;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop")
	int32 FinishRank = 0;

	/**
	 * True si el jugador fue eliminado (muerto) en lugar de terminar normalmente.
	 * La UI muestra "ELIMINADO" cuando esto es true, usando FinishRank para ordenar
	 * entre jugadores eliminados (asignado por orden de muerte en el servidor).
	 */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop")
	bool bIsEliminated = false;

	/** @brief Predicado "vivo y jugable": true si el jugador puede participar activamente (no muerto, no eliminado). */
	UFUNCTION(BlueprintPure, Category = "Coop|Estado")
	bool IsAliveAndPlaying() const { return bIsAlive && !bIsEliminated; }

	/**
	 * Puntos ganados en esta carrera (#26).
	 * Calculados por ATN_RunGameMode al cruzar la meta según posición de llegada
	 * + sumas de ScorePickups durante la run.
	 * Replicado para que el HUD lo muestre en tiempo real (delegate OnRaceScoreChanged).
	 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RaceScore, Category = "Coop|Score")
	int32 RaceScore = 0;

	/** @brief OnRep de RaceScore: dispara OnRaceScoreChanged para refrescar el HUD. */
	UFUNCTION()
	void OnRep_RaceScore();

	/**
	 * Rondas ganadas en la partida del mapa procedural (Carrera y 2vs2: gana quien
	 * llega a 3). Lo resetea ATN_ProcMapGameMode al empezar la partida; no se toca
	 * en ResetForNewRace porque este se llama en cada ronda.
	 */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop|Rounds")
	int32 RoundWins = 0;

	/**
	 * Carrera en la playa (ATN_BeachRaceGameMode): conchas de la partida en medias (2 = una concha entera). La primera en
	 * tocar el agua se lleva una entera y quien llega en la cuenta atrás de después, media. Gana quien llega a
	 * RoundTarget conchas (RoundTarget * 2 medias). Lo resetea el GameMode al empezar la partida; no ResetForNewRace.
	 */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop|Rounds")
	int32 RaceShellHalves = 0;

	/** Pareja de la ronda actual en 2vs2 (0 o 1). -1 fuera de 2vs2. */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Coop|Rounds")
	int32 TeamIndex = -1;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** @brief Viaje sin cortes: marca la copia para que CopyProperties no arrastre la carrera anterior. */
	virtual void SeamlessTravelTo(APlayerState* NewPlayerState) override;

protected:
	/**
	 * @brief Copia el estado de carrera (puntos, vivo o muerto, meta) al PlayerState inactivo de quien se desconecta, para
	 *        devolvérselo si vuelve a la sala (#345). Derribado cuenta como muerto. En el viaje sin cortes no se copia:
	 *        la ronda nueva empieza de cero.
	 */
	virtual void CopyProperties(APlayerState* PlayerState) override;

private:
	/** true solo dentro de SeamlessTravelTo. */
	bool bCopyingForSeamlessTravel = false;

	float ServerLastQuickChatTime = -10000.f;
	TMap<uint8, float> ServerLastEmoteTimes;

	/**
	 * @brief Reintenta aplicar un cosmético (helmet/skin) sobre el pawn hasta que esté disponible.
	 * @param Applier Setter concreto a invocar sobre el pawn (UpdateHelmetMesh / UpdateSkinVisual).
	 * @param LogTag Prefijo usado en el log de reintentos agotados.
	 * @note Server/multicast-side. El intento inmediato y la asignación del Id replicado
	 *       corren en el llamador; este helper solo cubre el camino de reintento con timer.
	 */
};
