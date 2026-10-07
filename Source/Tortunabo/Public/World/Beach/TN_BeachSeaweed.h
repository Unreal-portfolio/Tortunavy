#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachTrapCommon.h"
#include "TN_BeachSeaweed.generated.h"

class ACharacter;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UTN_BeachTrapSynthComponent;

/** Una tortuga enganchada en las algas (lo decide el servidor y se replica). */
USTRUCT()
struct FTNSeaweedCatch
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<ACharacter> Turtle = nullptr;

	/** Hora del servidor a la que se suelta sola (cada tirón la adelanta). */
	UPROPERTY()
	float ReleaseAt = 0.f;

	/** Tirones que lleva (saltos y meneos): cada uno sacude las algas en todas las máquinas. */
	UPROPERTY()
	uint8 Tugs = 0;
};

/**
 * Montón de algas que enredan: una pila de algas oscuras y mojadas con cintas sueltas alrededor y unos tallos de pie que
 * se mecen. Elipse de semiejes ~0,92·huella (700·SizeScale) en X e Y; con Spec.Extent > 0, Extent es el ancho máximo en
 * Y (para meterla en un pasillo). Sin colisión: se vadea con las patas metidas en las algas.
 * Un guantazo (UTN_FlipperSlapComponent) sobre ellas o al alcance de la aleta las corta (UTN_HazardTuning::SeaweedHitsToCut,
 * #871): sueltan a quien enganchaban y desaparecen.
 *
 * Reglas (servidor): quien la pisa dentro de la zona (85 % de la elipse, con los pies en el suelo) se queda enganchada
 * CatchSeconds: anda a HeldSpeed y el salto no la levanta (JumpZVelocity = 0: cada intento es un tirón). Cada salto
 * adelanta la suelta JumpTug segundos y cada meneo (cambiar de golpe la dirección del movimiento) WiggleTug; también se
 * suelta si la arrastran fuera de la elipse. Al soltarse, ImmuneSeconds sin volver a engancharse; mientras siga dentro,
 * vadea a WadeSpeed. Las tortugas aturdidas, en el caparazón o muertas no se enganchan (y si las aturden, se sueltan).
 *
 * Red: el servidor decide y replica Catches (quién, cuándo se suelta y cuántos tirones). El freno (tope de velocidad del
 * UTN_StaminaComponent y JumpZVelocity) se aplica en el servidor y en el cliente dueño, como TN_SlowZoneVolume; el
 * cliente dueño predice el enganche al pisarla (si el servidor no lo confirma en 0,6 s, lo deshace). Las algas que se
 * enrollan alrededor de cada tortuga (hasta 4 a la vez) se animan en cada máquina con el estado replicado.
 */
UCLASS()
class TORTUNABO_API ATN_BeachSeaweed : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachSeaweed();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Hasta donde se mece la malla (TNSeaweedLogic::FREEZE_DISTANCE) más su huella: más lejos, su Tick no hace nada. */
	virtual float GetTickWakeDistance() const override;

	/** Con alguien enganchado, frenado o con las algas enrolladas, sigue despierta (nadie se queda frenado). */
	virtual bool IsTickBusy() const override;

	/** true si la tortuga está enganchada según el estado replicado. */
	bool IsCaught(const ACharacter* Turtle) const;

	/** Cortadas de un golpe (#871): ya no enganchan ni frenan y no se ven. */
	bool IsCut() const { return bCut; }

	/**
	 * Servidor: un guantazo dado desde SlapOrigin hacia SlapPoint (UTN_FlipperSlapComponent). Si alguno de los dos cae en
	 * las algas, cuenta como golpe y, con UTN_HazardTuning::SeaweedHitsToCut, las corta: sueltan a quien enganchaban.
	 * true si les ha dado.
	 */
	bool ServerHitBySlap(const FVector& SlapOrigin, const FVector& SlapPoint);

	/** Segundos que se queda enganchada sin forcejear. */
	UPROPERTY(EditAnywhere, Category = "Algas", meta = (ClampMin = "0.5"))
	float CatchSeconds = 3.2f;

	/** Segundos que adelanta la suelta cada salto. */
	UPROPERTY(EditAnywhere, Category = "Algas", meta = (ClampMin = "0.0"))
	float JumpTug = 0.4f;

	/** Segundos que adelanta la suelta cada meneo. */
	UPROPERTY(EditAnywhere, Category = "Algas", meta = (ClampMin = "0.0"))
	float WiggleTug = 0.2f;

	/** Velocidad máxima enganchada (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Algas", meta = (ClampMin = "0.0"))
	float HeldSpeed = 55.f;

	/** Velocidad máxima vadeando las algas sin estar enganchada (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Algas", meta = (ClampMin = "0.0"))
	float WadeSpeed = 320.f;

	/** Segundos sin poder volver a engancharse tras soltarse. */
	UPROPERTY(EditAnywhere, Category = "Algas", meta = (ClampMin = "0.0"))
	float ImmuneSeconds = 2.5f;

protected:
	virtual void BeginPlay() override;
	virtual void ApplySpec() override;

	UFUNCTION()
	void OnRep_Catches();

	UFUNCTION()
	void OnRep_Cut();

	/** Saltos de las tortugas frenadas en esta máquina (servidor: tirones; cliente dueño: sacudida inmediata). */
	UFUNCTION()
	void OnHeldModeChanged(ACharacter* Turtle, EMovementMode PrevMovementMode, uint8 PreviousCustomMode);

	UPROPERTY(ReplicatedUsing = OnRep_Catches)
	TArray<FTNSeaweedCatch> Catches;

	/** Cortadas (#871). Lo decide el servidor; cada máquina las esconde al recibirlo. */
	UPROPERTY(ReplicatedUsing = OnRep_Cut)
	bool bCut = false;

	/** Pila, cintas y mancha de arena mojada. */
	UPROPERTY(VisibleAnywhere, Category = "Algas")
	TObjectPtr<UStaticMeshComponent> PatchMesh;

	/** Tallos que se mecen y algas que se enrollan (malla procedural local, solo en máquinas con pantalla). */
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> LiveMesh;

	UPROPERTY(Transient)
	TObjectPtr<UTN_BeachTrapSynthComponent> Voice;

private:
	/** Seguimiento de meneos en el servidor. */
	struct FTugTrack
	{
		FVector2D LastDir = FVector2D::ZeroVector;
		double LastWiggle = -10.0;
	};

	/** Freno aplicado en esta máquina a una tortuga que simula. */
	struct FHoldState
	{
		float AppliedCap = -1.f;
		bool bHeld = false;
	};

	/** Nombre de los límites de esta alga en UTN_StaminaComponent (velocidad y salto; varias algas a la vez no se pisan). */
	FName LimitSource() const { return FName(TEXT("Seaweed"), static_cast<int32>(GetUniqueID())); }

	/** Hueco de la animación de algas enrolladas. */
	struct FWrapSlot
	{
		TWeakObjectPtr<ACharacter> Turtle;
		FVector Anchor = FVector::ZeroVector;
		float Curl = 0.f;
		float Jerk = 0.f;
		float Phase = 0.f;
	};

	void ServerUpdate();
	void UpdateLocalPrediction();
	void UpdateHolds();
	void UpdateWraps(float DeltaSeconds);
	void RebuildLiveMesh();

	/** Tras cambiar Catches (servidor) o recibirlo (clientes): efectos de enganches, tirones y sueltas. */
	void HandleCatchesChanged();

	/** Aplica o quita el freno (Cap < 0 = ninguno) en esta máquina. */
	void ApplyHold(ACharacter* Turtle, float Cap, bool bHeld);
	void RemoveHold(ACharacter* Turtle);

	/** La tortuga (sus pies) dentro de la elipse de enganche agrandada Grow veces. */
	bool IsInPatch(const ACharacter* Turtle, double Grow) const;

	/** Se ve enganchada en esta máquina (replicado o predicho por el cliente dueño). */
	bool IsVisuallyHeld(const ACharacter* Turtle) const;

	int32 FindCatchIndex(const ACharacter* Turtle) const;
	void PlayCatchFX(const ACharacter* Turtle, int32 Kind);

	double Ax = 600.0;
	double Ay = 600.0;
	double MoundH = 50.0;
	TArray<FVector> FrondBases;
	TArray<float> FrondPhases;
	TArray<float> FrondHeights;

	TMap<TWeakObjectPtr<ACharacter>, FTugTrack> Tracks;
	TMap<TWeakObjectPtr<ACharacter>, double> ImmuneUntil;
	TMap<TWeakObjectPtr<ACharacter>, FHoldState> Holds;
	TArray<FTNSeaweedCatch> SeenCatches;
	TArray<FWrapSlot> WrapSlots;

	/** Cliente dueño: enganche predicho (hora del servidor, < 0 = ninguno) y gracia local tras soltarse. */
	double PredictedSince = -1.0;
	double LocalImmuneUntil = 0.0;
	double LocalFXTime = -10.0;

	float AnimTime = 0.f;

	/** Segundos desde la última reconstrucción de LiveMesh (TNSeaweedLogic decide cuándo toca). */
	float SinceLiveRebuild = 0.f;

	FTNTrapBurst Splash;

	/** Servidor: golpes recibidos (SeaweedHitsToCut los cortan). */
	int32 HitsTaken = 0;
	bool bCutApplied = false;

	/** El punto (en el mundo) cae sobre las algas, con algo de margen. */
	bool ContainsPoint(const FVector& WorldPoint) const;

	/** En esta máquina: esconde las algas, suelta los frenos y salpica (una vez). */
	void ApplyCutLocal();
};
