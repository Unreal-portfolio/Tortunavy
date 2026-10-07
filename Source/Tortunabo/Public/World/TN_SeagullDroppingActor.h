#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_SeagullDroppingActor.generated.h"

class UStaticMeshComponent;
class UDecalComponent;

/**
 * Caca de gaviota (#12).
 *
 * Sistema INDEPENDIENTE de la gaviota dinámica (#11).
 *
 * Comportamiento:
 *   1. Spawnea sobre un jugador (o punto aleatorio) a DropSpawnHeight.
 *   2. Cae en línea recta a FallSpeed cm/s.
 *   3. Una sombra (Decal) en el suelo se ENCOGE a medida que la caca baja,
 *      dando feedback visual de dónde y cuándo impacta.
 *   4. Al impactar el suelo: hitbox instantáneo → si hay jugador → daño (UTN_HazardTuning::SeagullDropping, #871).
 *   5. El proyectil no persigue — el jugador solo necesita moverse.
 *
 * Autoridad: servidor controla el fall + detección del impacto.
 *   Posición se replica via SetReplicateMovement.
 *
 * Uso:
 *   1. Crear BP_SeagullDropping hijo, asignar meshes para la caca y el Decal.
 *   2. Llamar SpawnDroppingOnPlayer() / SpawnDroppingAtLocation() desde Level BP
 *      o desde el RunGameMode en un evento temporizado.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_SeagullDroppingActor : public AActor
{
	GENERATED_BODY()

public:
	ATN_SeagullDroppingActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Spawnea la caca directamente encima del jugador objetivo a DropSpawnHeight,
	 * con la caída apuntando a su posición en el momento del spawn.
	 * La caca NO persigue — si el jugador se mueve, escapa.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeagullDropping",
		meta = (WorldContext = "WorldContextObject"))
	static ATN_SeagullDroppingActor* SpawnDroppingOnPlayer(const UObject* WorldContextObject,
		TSubclassOf<ATN_SeagullDroppingActor> DroppingClass,
		class ATortugaCharacter* Target);

	/**
	 * Spawnea la caca sobre una posición XY determinada.
	 * Útil para eventos scriptados donde la posición es fija.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeagullDropping",
		meta = (WorldContext = "WorldContextObject"))
	static ATN_SeagullDroppingActor* SpawnDroppingAtLocation(const UObject* WorldContextObject,
		TSubclassOf<ATN_SeagullDroppingActor> DroppingClass,
		FVector GroundTarget);

protected:
	// ── Componentes ──────────────────────────────────────────────────────────

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeagullDropping")
	TObjectPtr<UStaticMeshComponent> DroppingMesh;

	/**
	 * Sombra proyectada en el suelo.
	 * Se encoge conforme la caca baja: DecalSize.Y = DecalSize.Z = CurrentShadowRadius.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeagullDropping")
	TObjectPtr<UDecalComponent> ShadowDecal;

	// ── Config ────────────────────────────────────────────────────────────────

	/** Altura sobre el suelo desde la que spawnea (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeagullDropping",
		meta = (ClampMin = "200.0"))
	float DropSpawnHeight = 1200.f;

	/** Velocidad de caída (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeagullDropping",
		meta = (ClampMin = "50.0"))
	float FallSpeed = 600.f;

	/** Radio inicial de la sombra (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeagullDropping",
		meta = (ClampMin = "10.0"))
	float MaxShadowRadius = 300.f;

	/** Radio mínimo de la sombra justo antes del impacto (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeagullDropping",
		meta = (ClampMin = "10.0"))
	float MinShadowRadius = 40.f;

	/** Radio del hitbox de impacto (cm; antes 100: se esquivaba mal). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeagullDropping",
		meta = (ClampMin = "10.0"))
	float ImpactRadius = 75.f;

	/**
	 * Tirarse en plancha en el momento justo (en el aire o aún arrastrándose deprisa, TNBeach::IsDodgingByBellyDive) libra
	 * del impacto: la caca le pasa por encima.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeagullDropping")
	bool bBellyDiveDodges = true;

	/** Profundidad del Decal de sombra (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeagullDropping",
		meta = (ClampMin = "50.0"))
	float DecalDepth = 600.f;

	/**
	 * Factor de escala mínima del mesh en el momento del impacto (0..1).
	 * El mesh encoge linealmente desde 1.0 (spawn) hasta este factor aplicado
	 * sobre la escala original del BP, dando feedback de "se aproxima" sin
	 * cambios de velocidad de caída.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeagullDropping",
		meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MinMeshScaleFactor = 0.35f;

	// ── Eventos BP ───────────────────────────────────────────────────────────

	/** Llamado en TODAS las máquinas al impactar en el suelo. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeagullDropping")
	void OnImpact(bool bHitPlayer);

private:
	/** Posición en Z del suelo objetivo (calculada al spawnear). */
	UPROPERTY(Replicated)
	float GroundTargetZ = 0.f;

	/** Posición XY del impacto (no cambia tras el spawn). */
	UPROPERTY(Replicated)
	FVector2D ImpactXY = FVector2D::ZeroVector;

	bool bImpacted = false;

	/** Escala original del mesh capturada en BeginPlay (respeta el valor del BP). */
	FVector InitialMeshScaleCache = FVector::OneVector;

	void UpdateShadowScale();
	void ResolveImpact();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnImpact(bool bHitPlayer);
};
