#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_PathStorm.generated.h"

class UStaticMeshComponent;
class UPostProcessComponent;
class UProceduralMeshComponent;
class UExponentialHeightFogComponent;
class ATN_ProcMapGenerator;
class APlayerController;
class ATortugaCharacter;

/**
 * Tormenta del mapa procedural (modo Coop). A diferencia de ATN_StormVolume, que
 * es una caja que crece en línea recta, esta avanza A LO LARGO DEL CAMINO: su
 * frente es una distancia sobre el camino principal. Quien quede por detrás del
 * frente (según su progreso proyectado sobre el camino, en 3D para distinguir
 * puentes y cuevas) empieza la cuenta atrás de muerte.
 *
 * Replicada: el servidor avanza FrontProgress; los clientes lo extrapolan con la
 * velocidad para mover el frente visual y activar el efecto local de dentro.
 *
 * Aspecto (TN_PathStormFX.h, solo en máquinas con pantalla): velo translúcido por capas en el
 * frente y lo que arrastra según el bioma (arena, hojas, brasas y ceniza, espuma, lluvia, polvo,
 * humo), mezclado con pesos suavizados para que al cambiar de bioma cambie en degradado. Dentro, la
 * niebla del nivel se cierra y la imagen se tiñe según el bioma del jugador, con transición.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_PathStorm : public AActor
{
	GENERATED_BODY()

public:
	ATN_PathStorm();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Servidor: crea la tormenta del mapa de Generator (parada hasta StartStorm). La clase es la de los ajustes del mapa
	 * (UTN_ProcMapSettings::PathStormClass) si la hay; si no, DefaultClass, y sin ella, esta.
	 */
	static ATN_PathStorm* SpawnFor(UWorld* World, const ATN_ProcMapGenerator* Generator, TSubclassOf<ATN_PathStorm> DefaultClass = nullptr);

	/** Servidor: arranca la tormenta tras GraceSeconds, a Speed cm/s por el camino. */
	void StartStorm(ATN_ProcMapGenerator* InGenerator, float InSpeed, float GraceSeconds);

	/** Servidor: detiene y reinicia (entre rondas). */
	void StopStorm();

	UFUNCTION(BlueprintPure, Category = "Storm")
	float GetFrontProgress() const { return FrontProgress; }

	UFUNCTION(BlueprintPure, Category = "Storm")
	bool IsStormActive() const { return bActive; }

	/**
	 * Si una posición del mundo queda dentro de la tormenta: su progreso sobre el camino (en 3D, para distinguir puentes
	 * y cuevas) está más de InsideMargin por detrás del frente. Es la cuenta de la cuenta atrás de muerte del servidor;
	 * en los clientes usa el frente replicado y extrapolado. Falso con la tormenta parada o el mapa sin generar.
	 */
	bool IsLocationInside(const FVector& WorldLocation) const;

	/** Fuerza a re-evaluar a un jugador (tras revivir). */
	void ForceCheckPlayer(APlayerController* PC);

	/**
	 * Servidor (pruebas, comando TNStorm): pone el frente en Progress (cm del camino) sin gracia y lo deja avanzar.
	 * Con bHarmless no mata a nadie; con bHarmless = false vuelve el tiempo de muerte normal.
	 */
	void DebugPlaceFront(float Progress, bool bHarmless);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Storm")
	TObjectPtr<USceneComponent> Root;

	/** Muro de cubo de antes: ya no se ve (el frente es el velo y las partículas). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Storm")
	TObjectPtr<UStaticMeshComponent> FrontWall;

	/** Velo del frente: láminas translúcidas del color del bioma. */
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Veil;

	/** Post-proceso local de visión degradada (configurar en el BP). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Storm")
	TObjectPtr<UPostProcessComponent> InsidePostProcess;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storm", meta = (ClampMin = "0.1"))
	float SecondsInsideToDie = 5.f;

	/** Margen por detrás del frente antes de contar como dentro (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storm", meta = (ClampMin = "0.0"))
	float InsideMargin = 400.f;

private:
	UPROPERTY(Replicated)
	float FrontProgress = 0.f;

	UPROPERTY(Replicated)
	float Speed = 0.f;

	UPROPERTY(Replicated)
	bool bActive = false;

	UPROPERTY(Replicated)
	TObjectPtr<ATN_ProcMapGenerator> Generator;

	float GraceRemaining = 0.f;
	float CheckAccumulator = 0.f;
	/** SecondsInsideToDie de verdad mientras DebugPlaceFront la deja inofensiva (-1 = sin tocar). */
	float DefaultSecondsInsideToDie = -1.f;
	TMap<TWeakObjectPtr<APlayerController>, float> InsideTime;

	void ServerCheckPlayers(float Interval);
	void UpdateVisual(float DeltaTime);

	// ── Tos de las tortugas (solo en máquinas con audio) ────────────────────
	/**
	 * Cada 0,1 s da a cada tortuga viva su UTN_StormCoughComponent y le pasa si está dentro según esta máquina (con el
	 * frente replicado, sin RPC) y qué fracción lleva de SecondsInsideToDie; a las muertas las calla.
	 */
	void TickCough(float DeltaTime);
	float CoughAccumulator = 0.f;
	/** Segundos que lleva dentro cada tortuga según esta máquina (como InsideTime en el servidor). */
	TMap<TWeakObjectPtr<ATortugaCharacter>, float> CoughInsideTime;

	// ── Efectos (solo con pantalla) ─────────────────────────────────────────
	static constexpr int32 MaxBiomes = 8;
	void SetupFX();
	void TickFX(float DeltaTime, bool bFrontVisible, bool bLocalInside, const FVector& FrontLoc, const FVector& Dir);
	void ApplyInsideLook();
	void RestoreFog();

	bool bFXReady = false;
	/** Pesos de bioma suavizados en el frente y donde está la cámara. */
	float FrontW[MaxBiomes] = {};
	float ViewW[MaxBiomes] = {};
	/** 0..1: el frente a la vista y el jugador local dentro, con transición. */
	float FrontBlend = 0.f;
	float InsideBlend = 0.f;
	float FXTime = 0.f;
	float VeilRefresh = 0.f;
	FLinearColor VeilShown = FLinearColor::Black;
	float VeilShownAlpha = -1.f;
	TArray<FVector> VeilVerts;
	TArray<int32> VeilTris;
	TArray<FVector> VeilNormals;
	TArray<FVector2D> VeilUVs;
	TArray<FLinearColor> VeilBase;
	TArray<int32> FrontEmitters;
	TArray<int32> ViewEmitters;

	/** Niebla del nivel (se cierra dentro y vuelve a su estado al salir). */
	TWeakObjectPtr<UExponentialHeightFogComponent> Fog;
	bool bFogCached = false;
	bool bFogApplied = false;
	float FogDensity0 = 0.f;
	float FogFalloff0 = 0.f;
	float FogStart0 = 0.f;
	float FogOpacity0 = 1.f;
	FLinearColor FogColor0 = FLinearColor::White;
};
