#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "World/Beach/TN_RaceItemSynth.h"
#include "World/ProcMap/TN_ProcMapAmbientFX.h"
#include "TN_RaceItemComponent.generated.h"

class ACharacter;
class ATN_BeachRaceGenerator;
class UPointLightComponent;

/**
 * Efectos de objetos de carrera que lleva puestos una tortuga, en un solo struct replicado (una sola notificación): el
 * turbo (coco turbo y dorado), el protector solar y el vuelo en el pelícano taxi. Los tiempos son de la hora del servidor.
 */
USTRUCT()
struct FTNRaceEffectState
{
	GENERATED_BODY()

	/** Hora del servidor en que se acaba el turbo (0 = sin turbo) y cuánto multiplica la velocidad. */
	UPROPERTY()
	float BoostEnd = 0.f;

	UPROPERTY()
	float BoostMultiplier = 1.f;

	/** El turbo es el del coco dorado (color y brillo distintos). */
	UPROPERTY()
	bool bGolden = false;

	/** Hora del servidor en que se acaba el protector solar (0 = sin él). */
	UPROPERTY()
	float StarEnd = 0.f;

	/** La lleva el pelícano taxi. */
	UPROPERTY()
	bool bRiding = false;

	/** Hora del servidor en que se acaba la ola de la tabla de surf (0 = sin ola; #786). */
	UPROPERTY()
	float SurfEnd = 0.f;

	/** Hora del servidor en que se acaba el cohete de feria (0 = sin cohete; #786). */
	UPROPERTY()
	float RocketEnd = 0.f;

	/** El cohete ha acabado con la voltereta en el aire (empieza en RocketEnd y dura TNRaceItemRules::FlipSeconds). */
	UPROPERTY()
	bool bRocketFlip = false;
};

class FTNRaceRideFX;

/**
 * Lo que un objeto de carrera hace a la propia tortuga (Docs/Modo_Carrera.md, «Objetos de carrera»). No viene en la tortuga:
 * el servidor lo añade en ejecución la primera vez que hace falta y se replica solo (componente dinámico replicado, como
 * UTN_BeachStunComponent), así que el cooperativo no lo lleva nunca.
 *
 *  - Turbo (coco turbo y dorado): la velocidad se multiplica durante unos segundos. Va en la predicción del movimiento
 *    (issue #22): quien mueve la tortuga (su dueño o el anfitrión) mira GetSpeedMultiplier en cada movimiento y lo guarda en
 *    él (FTNSavedMove_Turtle, marca de turbo); el servidor simula los movimientos marcados con el multiplicador que él le
 *    reconoce (ResolveOwnerBoostMultiplier) y UTN_TurtleMovementComponent lo aplica a la velocidad y la aceleración. Así no
 *    hay corrección al empezar ni al acabar. Con pantalla: estela de rayas y arena, luz cálida, «fiuuum» y, en la tortuga
 *    local, un empujón de campo de visión.
 *  - Protector solar (la estrella): invulnerable (nada la aturde ni la derriba: TNBeach::StunTurtle, KnockDownTurtle y
 *    ATortugaCharacter::ApplyKnockdown lo miran con TNRaceItems::IsInvulnerable), algo más rápida y, en el servidor, derriba
 *    a las tortugas que toca y marea a los enemigos que toca. Con pantalla: brillo dorado, chispas y una luz que late.
 *  - Vuelo en el pelícano taxi (SetRiding): invulnerable mientras la lleva; no puede usar objetos.
 *  - Tabla de surf y cohete de feria (#786): multiplicadores propios y exactos (TNRaceItemRules::MoveStyleOf) que viajan en
 *    la predicción como el turbo; UTN_TurtleMovementComponent cambia con ellos el rumbo (la ola va hacia el mar, el cohete
 *    gira muy poco). El servidor derriba lo que encuentra la ola, la acaba contra una pared y da la voltereta del cohete.
 *
 * Servidor: GrantBoost, GrantStar, SetRiding, GrantSurf y GrantRocket. Todas las máquinas: los IsX. Los sonidos van con MulticastCue.
 */
UCLASS(ClassGroup = (Custom))
class TORTUNABO_API UTN_RaceItemComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_RaceItemComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** El componente de la tortuga, si ya lo tiene (en cualquier máquina). */
	static UTN_RaceItemComponent* FindOn(const AActor* Turtle);

	/** Servidor: el de la tortuga, creándolo y registrándolo (replicado) si aún no lo tiene. */
	static UTN_RaceItemComponent* FindOrAddOn(ACharacter* Turtle);

	// ── Servidor ─────────────────────────────────────────────────────────────

	/** Turbo: la velocidad se multiplica por Multiplier durante Seconds (alarga hasta el mayor de los dos finales). */
	void GrantBoost(float Multiplier, float Seconds, bool bGolden);

	/** Protector solar durante Seconds (alarga hasta el mayor de los dos finales). */
	void GrantStar(float Seconds);

	/** Empieza o acaba el vuelo en el pelícano taxi. */
	void SetRiding(bool bInRiding);

	/**
	 * Tabla de surf (#786): una ola la lleva Seconds hacia el mar, más rápido que corriendo, derribando a las tortugas que
	 * encuentra; se acaba antes contra una pared de frente. false si ya va en una ola o con el cohete.
	 */
	bool GrantSurf(float Seconds);

	/** Cohete de feria (#786): acelerón muy fuerte con poco giro durante Seconds y voltereta al acabar. false si ya lo lleva o va en una ola. */
	bool GrantRocket(float Seconds);

	/** Quita todos los efectos ya (se acabó la ronda, la tortuga llega a la meta...). */
	void CancelEffects();

	/** Un sonido de la tortuga para todas las máquinas (el «nop» de no poder usar algo, el soplido de lanzar...). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastCue(ETNRaceSound Sound, float Pitch);

	/** Silbato del sargento: la onda y el silbido en Center para todas las máquinas. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastWhistle(FVector_NetQuantize10 Center, float Radius);

	// ── Cualquier máquina ────────────────────────────────────────────────────

	bool IsBoosting() const;
	bool IsGoldenBoosting() const;
	bool HasStar() const;
	bool IsRiding() const { return Effects.bRiding; }
	bool IsSurfing() const;
	bool IsRocketing() const;

	/** Hacia dónde avanza la carrera en el plano (hacia el mar del generador; sin él, +X). Lo usa el rumbo de la ola. */
	FVector GetCourseForward() const;

	/** Nada la puede aturdir ni derribar: protector solar puesto o vuelo en el pelícano. */
	bool IsInvulnerable() const { return HasStar() || Effects.bRiding; }

	/** Multiplicador de velocidad que suman ahora el turbo y el protector (1 = ninguno). */
	float GetSpeedMultiplier() const;

	/**
	 * Servidor: multiplicador con el que simula un movimiento de la tortuga que llega marcado con turbo. El de ahora o, si
	 * aquí se acaba de terminar (por tiempo o cancelado), el que tenía, durante TNRaceItems::BoostGraceSeconds de su ping:
	 * el dueño lo ve acabar más tarde. 1 si nada lo justifica (TNRaceItems::ResolveClaimedBoost).
	 */
	float ResolveOwnerBoostMultiplier() const;

	float GetBoostSecondsLeft() const;
	float GetStarSecondsLeft() const;

protected:
	/** Sonido sintetizado de la tortuga (se crea la primera vez; null en servidor dedicado o sin audio). */
	UTN_RaceItemSynthComponent* GetSfx();

private:
	UPROPERTY(ReplicatedUsing = OnRep_Effects)
	FTNRaceEffectState Effects;

	UFUNCTION()
	void OnRep_Effects();

	/**
	 * Pone los efectos como dicen los datos: se da a conocer al movimiento de la tortuga (que lee el multiplicador en cada
	 * movimiento), apunta el último multiplicador del servidor y enciende el tick y los efectos visuales.
	 */
	void ApplyEffects();

	/** Servidor: si hay multiplicador ahora, lo apunta con su hora (el margen de ResolveOwnerBoostMultiplier cuenta desde ahí). */
	void NoteRecentSpeed();

	/** Servidor: último multiplicador mayor que 1 y la hora del servidor en que aún valía (< 0 = nunca). */
	float RecentSpeedMultiplier = 1.f;
	double RecentSpeedTime = -1.0;

	/** Servidor: el protector derriba a las tortugas y marea a los enemigos que toca. */
	void ServerStarContacts(float DeltaTime);

	/** Servidor: la ola derriba lo que encuentra y se acaba contra una pared; la ola y el cohete se cortan si la derriban o la aturden. */
	void ServerRideTick();

	/** Servidor: el cohete ha llegado a su hora: voltereta (salto con LaunchFromServer) si sigue de pie. */
	void ServerFinishRocket();

	/** Servidor: la ola choca de frente con una pared delante de la tortuga. */
	bool ServerSurfHitsWall() const;

	/** Servidor: tortugas que ya ha derribado la ola de ahora (una vez por ola) y hora a la que empezó. */
	TSet<TWeakObjectPtr<AActor>> SurfVictims;
	double SurfStartTime = 0.0;

	/** Servidor: ya se ha decidido el final del cohete de ahora. */
	bool bRocketEndHandled = true;

	/** Segundos de voltereta que lleva ahora (-1 si no hay). Cualquier máquina. */
	float GetFlipAge() const;

	/** Tabla, ola, cohete, llama y voltereta (máquinas con pantalla). */
	TSharedPtr<FTNRaceRideFX> RideFX;

	// ── Visual (máquinas con pantalla) ──

	void EnsureVisuals();
	void TickVisuals(float DeltaTime, bool bBoost, bool bStar);
	void StopVisuals();
	void UpdateFov(float DeltaTime, bool bBoost);

	/** La tabla, la ola, el cohete y la voltereta (máquinas con pantalla). */
	void TickRideVisuals(float DeltaTime);

	/** Tick necesario mientras haya efectos, emisores vivos o campo de visión por devolver. */
	void RefreshTickState();

	double Now() const;

	UPROPERTY(Transient)
	TObjectPtr<UTN_RaceItemSynthComponent> Sfx;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> Glow;

	TNAmbientFX::FEmitter Streaks;
	TNAmbientFX::FEmitter Dust;
	TNAmbientFX::FEmitter Sparks;
	bool bEmittersReady = false;

	/** Efectos que se están viendo en esta máquina (para notar cuándo empiezan y acaban). */
	bool bShownBoost = false;
	bool bShownGolden = false;
	bool bShownStar = false;
	float AppliedMultiplier = 1.f;

	/** Campo de visión de antes del turbo (tortuga local) y cuánto se le ha sumado. */
	bool bFovSaved = false;
	float SavedFovDefault = 72.f;
	float SavedFovSprint = 82.f;
	float FovKick = 0.f;

	/** Servidor: ronda del generador en que se dieron los efectos (-1 ninguna) y cada cuánto se mira si ha cambiado. */
	int32 EffectsRound = -1;
	float RoundCheckClock = 0.5f;
	TWeakObjectPtr<ATN_BeachRaceGenerator> RoundGenerator;

	/** Servidor: apunta la ronda actual del generador (los efectos no pasan a la siguiente). */
	void NoteRound();

	/** Servidor: a quién ha derribado el protector y hasta cuándo no le vuelve a dar (hora del mundo). */
	TMap<TWeakObjectPtr<AActor>, double> StarHitUntil;
	float StarScanClock = 0.f;
	float PulseClock = 0.f;
};
