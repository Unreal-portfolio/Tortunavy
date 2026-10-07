#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_StormVolume.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UNiagaraComponent;
class UPostProcessComponent;
class APlayerController;
class ATN_RunGameMode;

/**
 * Tormenta al estilo Fortnite: volumen que crece indefinidamente en una dirección.
 *
 * Mecánica de muerte:
 *   Idéntica a TN_DeathZoneVolume — si el jugador permanece dentro más de
 *   SecondsInsideToDie segundos, se llama a RunGameMode->MarkPlayerDead().
 *
 * Crecimiento:
 *   La pared trasera permanece fija; sólo avanza la frontal (GrowthDirection).
 *   El servidor crece en Tick y replica ReplicatedBoxHalfExtent + posición del actor.
 *
 * Visual (configurar en el Blueprint hijo):
 *   StormVolumeMesh  — cubo translúcido que cubre todo el interior.
 *                      Asignar material de tormenta translúcido de dos caras.
 *   EdgeVFX          — Niagara situado en el plano frontal de avance (humo/polvo borde).
 *   InteriorVFX      — Niagara interior (polvo/niebla ambiente constante).
 *   InsidePostProcess — post-process de visión degradada (color shift, niebla).
 *                       bUnbound=true; se activa/desactiva por cliente según si el
 *                       pawn local está dentro. Configurar los ajustes PP en el BP hijo.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_StormVolume : public AActor
{
	GENERATED_BODY()

public:
	ATN_StormVolume();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Fuerza un re-check del jugador. Necesario tras revivir cuando el pawn
	 * sigue dentro del volumen — UE no re-emite OnBoxBeginOverlap si el pawn
	 * nunca salió geométricamente. Sin esto, un jugador rescatado dentro de la
	 * tormenta queda inmune al countdown (el temporizador no le cuenta).
	 */
	void ForceCheckPlayer(APlayerController* PC);

protected:
	// ─── Componentes ─────────────────────────────────────────────────────────

	/** Caja de colisión. Ajustar extensión inicial en el Viewport del Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Storm")
	TObjectPtr<UBoxComponent> TriggerBox;

	/**
	 * Cubo escalado para cubrir todo el interior de la tormenta.
	 * Asignar un material translúcido de dos caras en el BP hijo.
	 * Se escala automáticamente con TriggerBox.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Storm|Visual")
	TObjectPtr<UStaticMeshComponent> StormVolumeMesh;

	/**
	 * Niagara situado en el plano frontal de avance.
	 * Asignar un sistema de humo/polvo denso en el BP hijo.
	 * Se reposiciona automáticamente al crecer el volumen.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Storm|Visual")
	TObjectPtr<UNiagaraComponent> EdgeVFX;

	/**
	 * Niagara de atmósfera interior (polvo/niebla constante dentro de la tormenta).
	 * Asignar un sistema de partículas atmosférico en el BP hijo.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Storm|Visual")
	TObjectPtr<UNiagaraComponent> InteriorVFX;

	/**
	 * Post-process de visión degradada: color shift, niebla, aberración cromática, etc.
	 * bUnbound=true — afecta globalmente, pero se activa/desactiva
	 * por cada cliente sólo cuando su pawn local está dentro del volumen.
	 * Configurar los ajustes de post-process en el Blueprint hijo.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Storm|Visual")
	TObjectPtr<UPostProcessComponent> InsidePostProcess;

	// ─── Configuración de muerte ──────────────────────────────────────────

	/** Segundos que el jugador puede permanecer dentro antes de morir. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storm|Death", meta = (ClampMin = "0.1"))
	float SecondsInsideToDie = 5.0f;

	/** Si true, el countdown sólo corre mientras el GameMode está en InProgress. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storm|Death")
	bool bDestroyOnlyDuringRun = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storm|Death", meta = (ClampMin = "0.05"))
	float CountdownTickInterval = 0.1f;

	// ─── Configuración de crecimiento ─────────────────────────────────────

	/**
	 * Velocidad de avance del borde frontal (UU/s).
	 * La pared trasera permanece fija; sólo avanza la frontal.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storm|Growth", meta = (ClampMin = "0.0"))
	float GrowthSpeed = 150.0f;

	/**
	 * Dirección LOCAL en la que avanza el borde frontal (se normaliza en runtime).
	 * Ejemplo: (1,0,0) avanza en +X local del actor, (-1,0,0) en -X, (0,1,0) en +Y, etc.
	 * Para controlar la dirección world, simplemente rota el actor en el editor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storm|Growth")
	FVector GrowthDirection = FVector(1.0f, 0.0f, 0.0f);

	/** Si false, el volumen deja de crecer (útil para eventos scripteados o pausas). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storm|Growth",
		ReplicatedUsing = OnRep_GrowthEnabled)
	bool bGrowthEnabled = true;

	UFUNCTION()
	void OnRep_GrowthEnabled() {}   // sin efecto extra — sólo necesitamos el valor

private:
	// ─── Replicación de extensión ─────────────────────────────────────────

	/**
	 * Media extensión de la caja replicada.
	 * El servidor la actualiza en Tick; los clientes la aplican en OnRep.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_BoxHalfExtent)
	FVector ReplicatedBoxHalfExtent;

	UFUNCTION()
	void OnRep_BoxHalfExtent();

	/** Escala StormVolumeMesh y reposiciona EdgeVFX para coincidir con TriggerBox. */
	void SyncVisualToBox();

	// ─── Lógica de muerte (sólo servidor) ────────────────────────────────

	UFUNCTION()
	void OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnBoxEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void HandlePlayerDeath(APlayerController* PC);
	void TickAllCountdowns();
	ATN_RunGameMode* ResolveRunGameMode() const;

	FTimerHandle SharedCountdownTimerHandle;
	TMap<TWeakObjectPtr<APlayerController>, float> PendingDeathRemaining;

	// ─── Estado visual local ──────────────────────────────────────────────

	/** Dirección normalizada en espacio local (caché; calculada en BeginPlay). */
	FVector NormalizedGrowthDir;

	/** Conteo de pawns locales dentro (para toggle del post-process). */
	int32 LocalPlayersInside = 0;
};
