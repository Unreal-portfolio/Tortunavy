#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachTrapCommon.h"
#include "TN_BeachShellGate.generated.h"

class ACharacter;
class UBoxComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UTN_BeachTrapSynthComponent;

/** Estado de la puerta de conchas (lo decide el servidor; cada máquina anima las hojas desde aquí). */
USTRUCT()
struct FTNShellGateState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bOpen = false;

	/** Hacia dónde se abren las hojas: +1 hacia +X, -1 hacia -X (lejos de quien empuja). */
	UPROPERTY()
	int8 Dir = 1;

	/** Hora del servidor del último cambio y ángulo que tenían entonces (grados). */
	UPROPERTY()
	float ChangedAt = -100.f;

	UPROPERTY()
	float FromAngle = 0.f;
};

/**
 * Puerta de dos hojas hechas de conchas de vieira (mosaico de colores pastel sobre un bastidor de madera) atravesada en el
 * camino: el paso va a lo largo de X (X local = sentido de la carrera) y la puerta, a lo largo de Y. Hueco de 300 de
 * ancho y 330 de alto. Dos variantes según la semilla: en una pared de arena (de ±0,95 de la huella, 600·SizeScale, 440 de
 * alto y 200 de grueso) o entre dos rocas. Se rodea, pero cruzarla es el camino corto.
 *
 * Se abre (servidor) empujándola PushSeconds (andar contra una hoja cerrada, desde cualquiera de los dos lados: se abre
 * hacia el otro) o pisando el interruptor, una concha grande en el suelo a 3,8 m por delante y a un lado. Se queda
 * abierta mientras alguien esté en el hueco o sobre el interruptor y OpenHold segundos más; luego se cierra (si alguien
 * entra mientras se cierra, se vuelve a abrir). Las hojas solo chocan cuando están cerradas.
 *
 * Con Spec.Extent > 0 es una puerta «desnuda» para un hueco de Extent de ancho y BareDoorHeight de alto (la usa el
 * castillo de arena con salas): solo bastidor, hojas e interruptor (a -Y), sin pared ni rocas.
 *
 * Red: estado replicado (FTNShellGateState); lo visual, en cada máquina con la hora del servidor.
 */
UCLASS()
class TORTUNABO_API ATN_BeachShellGate : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	/** Alto del hueco de una puerta desnuda (castillo de arena): el dintel del bastidor llega hasta aquí. */
	static constexpr double BareDoorHeight = 420.0;

	ATN_BeachShellGate();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool IsOpen() const { return GateState.bOpen; }

	/** Tick por distancia (#59): lo que ocupa (con el ancho de una puerta desnuda) más TNBeachTickWake::ReachMargin. */
	virtual float GetTickWakeDistance() const override;

	/** Abierta, cerrándose o con las chispas de abrirse en el aire: sigue despierta hasta quedarse cerrada. */
	virtual bool IsTickBusy() const override;

	/** Segundos empujando para abrirla. */
	UPROPERTY(EditAnywhere, Category = "Puerta", meta = (ClampMin = "0.1"))
	float PushSeconds = 0.6f;

	/** Segundos que sigue abierta cuando ya nadie la retiene. */
	UPROPERTY(EditAnywhere, Category = "Puerta", meta = (ClampMin = "0.5"))
	float OpenHold = 3.f;

	/** Ángulo de las hojas abiertas (grados). */
	UPROPERTY(EditAnywhere, Category = "Puerta", meta = (ClampMin = "30.0", ClampMax = "120.0"))
	float OpenDeg = 95.f;

protected:
	virtual void BeginPlay() override;
	virtual void ApplySpec() override;

	UFUNCTION()
	void OnRep_GateState();

	UPROPERTY(ReplicatedUsing = OnRep_GateState)
	FTNShellGateState GateState;

	/** Pared o rocas, bastidor y peana del interruptor. */
	UPROPERTY(VisibleAnywhere, Category = "Puerta")
	TObjectPtr<UStaticMeshComponent> FrameMesh;

	UPROPERTY(VisibleAnywhere, Category = "Puerta")
	TObjectPtr<UProceduralMeshComponent> FrameCollision;

	/** Bisagras de las hojas (en Y = -hueco/2 y +hueco/2; la de +Y está girada 180°). */
	UPROPERTY(VisibleAnywhere, Category = "Puerta")
	TObjectPtr<USceneComponent> HingeLeft;

	UPROPERTY(VisibleAnywhere, Category = "Puerta")
	TObjectPtr<USceneComponent> HingeRight;

	UPROPERTY(VisibleAnywhere, Category = "Puerta")
	TObjectPtr<UStaticMeshComponent> LeafMeshLeft;

	UPROPERTY(VisibleAnywhere, Category = "Puerta")
	TObjectPtr<UStaticMeshComponent> LeafMeshRight;

	UPROPERTY(VisibleAnywhere, Category = "Puerta")
	TObjectPtr<UBoxComponent> LeafBoxLeft;

	UPROPERTY(VisibleAnywhere, Category = "Puerta")
	TObjectPtr<UBoxComponent> LeafBoxRight;

	/** Concha del interruptor (baja al pisarla). */
	UPROPERTY(VisibleAnywhere, Category = "Puerta")
	TObjectPtr<UStaticMeshComponent> SwitchMesh;

	UPROPERTY(Transient)
	TObjectPtr<UTN_BeachTrapSynthComponent> Voice;

private:
	void ServerUpdate(float DeltaSeconds, double ServerTime);

	/** Servidor: cambia de estado desde el ángulo actual. */
	void SetOpen(bool bInOpen, int8 InDir, double ServerTime);

	/** Ángulo de las hojas (grados) a la hora del servidor dada. */
	double LeafAngle(double ServerTime) const;

	/** Tras cambiar GateState (servidor) o recibirlo: sonidos. */
	void HandleStateChanged();

	/** Tortuga sobre el interruptor. */
	bool IsOnSwitch(const ACharacter* Character) const;

	/** Radio (cm, en planta, con la escala del actor) en el que un personaje puede pisar el interruptor, estar en el vano o empujar. */
	double ReachRadius() const;

	double DoorWidth = 300.0;
	double DoorHeight = 330.0;
	FVector SwitchLocal = FVector(-380.0, -320.0, 0.0);
	double SwitchRadius = 85.0;
	double LastHoldTime = -100.0;
	bool bSwitchDown = false;
	float SwitchPress = 0.f;
	bool bLastOpen = false;
	bool bLeavesSolid = true;

	/** Ángulo de las hojas y bajada del interruptor ya aplicados a las mallas: sin cambio, no se mueven (cada movimiento rehace sus hijos). */
	double ShownLeafAngle = -1.0;
	int8 ShownLeafDir = 0;
	float ShownSwitchPress = -1.f;

	TMap<TWeakObjectPtr<ACharacter>, float> PushTime;
	FTNTrapClock Clock;
	FTNTrapBurst Sparkle;
};
