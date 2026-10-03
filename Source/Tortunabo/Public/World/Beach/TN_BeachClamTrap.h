#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachTrapCommon.h"
#include "TN_BeachClamTrap.generated.h"

class ACharacter;
class APawn;
class APlayerController;
class UCameraComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UTN_BeachTrapSynthComponent;

/** Lo que la concha replica de cada cierre: horas del servidor y quién está dentro. */
USTRUCT()
struct FTNClamCatch
{
	GENERATED_BODY()

	/** Hora del servidor en que la concha nota a alguien y avisa (TellSeconds) antes de cerrarse; < 0 = nunca. */
	UPROPERTY()
	float SnapAt = -1.f;

	/** Hora del servidor en que se vuelve a abrir. */
	UPROPERTY()
	float OpenAt = -1.f;

	/** Hora del servidor en que escupió a la presa (>= SnapAt: este cierre acabó escupiendo; si no, se soltó antes). */
	UPROPERTY()
	float SpitAt = -1.f;

	/** Quien está dentro (null: cierre en vacío o ya fuera). */
	UPROPERTY()
	TObjectPtr<ACharacter> Captive = nullptr;
};

/**
 * Concha gigante que atrapa (una almeja gigante de 32 cm a escala: ~9 x 6,5 m con SizeScale = 1), abierta en la arena con
 * la valva de abajo medio enterrada (labio a ~38 cm de la arena, montículo de arena alrededor que se sube andando), el
 * manto de colores dentro y una perla que brilla. La valva de arriba está levantada ~68° sobre la charnela (a un lado,
 * ±Y según la semilla) y respira despacio. La pieza se orienta sola hacia el mar (X local = sentido de la carrera).
 *
 * Reglas (servidor): si una tortuga libre (TNBeachRideKit::IsFreeRider) pisa el manto (el 72 % central de la valva),
 * la concha tiembla TellSeconds y se cierra de golpe en CloseSeconds: atrapa a la tortuga libre más cercana al centro
 * que siga dentro (una sola; al resto de las que estén en la valva las empuja fuera). Dentro, quieta y sin control
 * (MOVE_None en el servidor y en su cliente, como el probador del lobby), entre HoldMin y HoldMax segundos; la concha
 * vibra a sacudidas (la tortuga pataleando) y suelta humo y burbujas de arena por la rendija. Al abrirse la escupe de un
 * saltito hacia el lado de la boca y hacia el mar (como al salir del huevo o del probador) y queda mareada DizzySeconds
 * al aterrizar (pajaritos y sin mover las patas). Luego RechargeSeconds sin volver a cerrarse: el manto encogido y la
 * perla apagada; al recargar, el manto se abre y la perla vuelve a destellar. Sin nadie dentro al cerrarse, se queda
 * cerrada EmptyHoldSeconds y se abre.
 *
 * Red: el estado es FTNClamCatch (replicado): cada máquina anima la valva, las sacudidas, el humo y los sonidos desde esas
 * horas con el reloj del servidor suavizado. A la presa la sujetan a la vez el servidor y su cliente (como el probador);
 * el saltito lo lanzan los dos a la hora de OpenAt (el cliente no espera a la réplica). La cámara de la presa pasa a la
 * de la concha (fuera, mirando la boca) mientras está dentro. Se suelta antes si muere, la aturden, se mete en el
 * caparazón, la derriban, la cogen o se desconecta. En el recuento y el podio no atrapa.
 */
UCLASS()
class TORTUNABO_API ATN_BeachClamTrap : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachClamTrap();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Quien está dentro ahora (en cualquier máquina, con el estado replicado). */
	ACharacter* GetCaptive() const;

	/** Aviso (tiembla la valva) antes de cerrarse. */
	UPROPERTY(EditAnywhere, Category = "Concha", meta = (ClampMin = "0.0"))
	float TellSeconds = 0.25f;

	/** Lo que tarda en cerrarse de golpe. */
	UPROPERTY(EditAnywhere, Category = "Concha", meta = (ClampMin = "0.05"))
	float CloseSeconds = 0.14f;

	/** Segundos dentro (al azar entre los dos, en el servidor). */
	UPROPERTY(EditAnywhere, Category = "Concha", meta = (ClampMin = "0.5"))
	float HoldMin = 3.2f;

	UPROPERTY(EditAnywhere, Category = "Concha", meta = (ClampMin = "0.5"))
	float HoldMax = 4.0f;

	/** Cerrada sin nadie dentro. */
	UPROPERTY(EditAnywhere, Category = "Concha", meta = (ClampMin = "0.2"))
	float EmptyHoldSeconds = 1.0f;

	/** Lo que tarda en abrirse. */
	UPROPERTY(EditAnywhere, Category = "Concha", meta = (ClampMin = "0.1"))
	float OpenSeconds = 0.4f;

	/** Desde que empieza a abrirse hasta que escupe a la presa. */
	UPROPERTY(EditAnywhere, Category = "Concha", meta = (ClampMin = "0.0"))
	float SpitDelay = 0.16f;

	/** Tras abrirse, sin volver a cerrarse. */
	UPROPERTY(EditAnywhere, Category = "Concha", meta = (ClampMin = "0.0"))
	float RechargeSeconds = 4.5f;

	/** Saltito al escupirla: velocidad horizontal (hacia la boca y el mar) y vertical (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Concha", meta = (ClampMin = "0.0"))
	float SpitHorizontal = 560.f;

	UPROPERTY(EditAnywhere, Category = "Concha", meta = (ClampMin = "0.0"))
	float SpitUp = 560.f;

	/** Mareo al aterrizar tras escupirla (pajaritos y sin mover las patas). */
	UPROPERTY(EditAnywhere, Category = "Concha", meta = (ClampMin = "0.0"))
	float DizzySeconds = 1.0f;

	/** Empujón a las demás que estén en la valva al cerrarse (hacia fuera y hacia arriba, cm/s). */
	UPROPERTY(EditAnywhere, Category = "Concha", meta = (ClampMin = "0.0"))
	float ShoveOut = 700.f;

	UPROPERTY(EditAnywhere, Category = "Concha", meta = (ClampMin = "0.0"))
	float ShoveUp = 450.f;

	/** Abertura de la valva de arriba en reposo (grados). */
	UPROPERTY(EditAnywhere, Category = "Concha", meta = (ClampMin = "20.0", ClampMax = "85.0"))
	float OpenDeg = 68.f;

protected:
	virtual void BeginPlay() override;
	virtual void ApplySpec() override;

	UFUNCTION()
	void OnRep_State();

	/** Empujón a las que estaban en la valva al cerrarse: su cliente aplica el mismo lanzamiento (fiable). */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastShove(const TArray<APawn*>& Pawns, const TArray<FVector>& Velocities);

	UPROPERTY(ReplicatedUsing = OnRep_State)
	FTNClamCatch State;

	/** Marco orientado hacia el mar (todo lo demás cuelga de aquí). */
	UPROPERTY(VisibleAnywhere, Category = "Concha")
	TObjectPtr<USceneComponent> Frame;

	/** Arena amontonada alrededor (no tiembla). */
	UPROPERTY(VisibleAnywhere, Category = "Concha")
	TObjectPtr<UStaticMeshComponent> MoundMesh;

	/** Suelo de la valva, labio y montículo. */
	UPROPERTY(VisibleAnywhere, Category = "Concha")
	TObjectPtr<UProceduralMeshComponent> LowerCollision;

	/** Charnela de la colisión de la valva de arriba (gira igual que la del dibujo, sin temblar). */
	UPROPERTY(VisibleAnywhere, Category = "Concha")
	TObjectPtr<USceneComponent> HingeCollision;

	UPROPERTY(VisibleAnywhere, Category = "Concha")
	TObjectPtr<UProceduralMeshComponent> UpperCollision;

	/** Lo que tiembla: las dos valvas, el manto y la perla. */
	UPROPERTY(VisibleAnywhere, Category = "Concha")
	TObjectPtr<USceneComponent> ShakeRoot;

	UPROPERTY(VisibleAnywhere, Category = "Concha")
	TObjectPtr<UStaticMeshComponent> LowerMesh;

	/** Manto de colores (encoge al cerrarse y mientras recarga). */
	UPROPERTY(VisibleAnywhere, Category = "Concha")
	TObjectPtr<UStaticMeshComponent> MantleMesh;

	UPROPERTY(VisibleAnywhere, Category = "Concha")
	TObjectPtr<UStaticMeshComponent> PearlMesh;

	/** Destello de la perla (solo con la concha lista). */
	UPROPERTY(VisibleAnywhere, Category = "Concha")
	TObjectPtr<UStaticMeshComponent> SparkleMesh;

	UPROPERTY(VisibleAnywhere, Category = "Concha")
	TObjectPtr<USceneComponent> HingeVisual;

	UPROPERTY(VisibleAnywhere, Category = "Concha")
	TObjectPtr<UStaticMeshComponent> UpperMesh;

	/** Cámara de la presa mientras está dentro (fuera, mirando la boca de la concha). */
	UPROPERTY(VisibleAnywhere, Category = "Concha")
	TObjectPtr<UCameraComponent> ViewCamera;

	UPROPERTY(Transient)
	TObjectPtr<UTN_BeachTrapSynthComponent> Voice;

private:
	/** Servidor: decide cierres, capturas, sueltas y escupitajos. */
	void ServerTick(double Now);

	/** Servidor: se cierra (acaba el aviso) y atrapa a la más cercana al centro. */
	void Slam(double Now);

	/** En esta máquina: sujeta a la presa (movimiento si la simula aquí, cámara y teclas si es la local, efectos). */
	void HoldLocal(ACharacter* Victim);

	/** En esta máquina: la suelta; con bSpit, de un saltito y mareada. */
	void ReleaseLocal(bool bSpit);

	/** Fin del mareo: vuelven las patas y se van los pajaritos. */
	void EndDizzy();

	/** Devuelve las teclas de mover a la tortuga local (si esta concha se las había quitado). */
	void RestoreLocalInput();

	/** Animación de la valva, sacudidas, manto, destello y efectos por horas, en esta máquina. */
	void TickVisuals(double Now, float DeltaSeconds);

	/** Abertura de la valva de arriba (grados) a la hora Now. */
	double UpperAngleAt(double Now) const;

	/** Sacudida (0..1) de la presa pataleando a la hora Now, y cuál es la sacudida en curso. */
	float StruggleAt(double Now, int32& OutPulse) const;

	/** Radio normalizado en la elipse de la valva y altura de los pies sobre el suelo de la valva. */
	/** Personajes al alcance de la valva (en planta): los únicos que pueden pisar el manto o quedar dentro al cerrarse. */
	void GatherNearValve(TArray<ACharacter*>& Out) const;

	bool IsInBowl(const ACharacter* Character, double RhoMax, double& OutRho) const;

	FVector HoldPointWorld(const ACharacter* Character) const;
	FVector SpitVelocityWorld() const;
	double ReadyAt() const;
	void ApplyHingeAngle(double Degrees, double Burp);
	void PuffAtRim(int32 Count, float Strength);

	// Medidas (espacio del marco; cm).
	double HalfX = 450.0;
	double HalfY = 324.0;
	double RimZ = 38.0;
	double FloorZ = 10.0;
	double DomeZ = 162.0;
	int32 HingeSide = 1;
	float BreathPhase = 0.f;

	// Estado local (cada máquina).
	TWeakObjectPtr<ACharacter> HeldLocal;
	bool bHeldApplied = false;
	TWeakObjectPtr<APlayerController> IgnoringController;
	bool bInputIgnored = false;
	bool bViewOnClam = false;
	TWeakObjectPtr<ACharacter> DizzyTurtle;
	double DizzyUntil = -1.0;
	double DizzyLandedAt = -1.0;
	double DizzySpitAt = -1.0;
	double LastVisualNow = -1.0;
	int32 LastPulse = -1;
	float MantleOpen = 1.f;
	bool bSnapPending = false;
	TMap<TWeakObjectPtr<ACharacter>, double> ImmuneUntil;

	FTNTrapClock Clock;
	FTNTrapBurst Smoke;
	FTNTrapBurst Bubbles;
	FTNTrapBurst Sand;
	FTNTrapPopText Pop;
};
