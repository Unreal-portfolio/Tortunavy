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
};

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
 *
 * Servidor: GrantBoost, GrantStar y SetRiding. Todas las máquinas: los IsX. Los sonidos van con MulticastCue.
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

	// ── Visual (máquinas con pantalla) ──

	void EnsureVisuals();
	void TickVisuals(float DeltaTime, bool bBoost, bool bStar);
	void StopVisuals();
	void UpdateFov(float DeltaTime, bool bBoost);

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
