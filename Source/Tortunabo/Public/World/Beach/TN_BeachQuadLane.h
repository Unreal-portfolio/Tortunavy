#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachTrapCommon.h"
#include "World/ProcMap/TN_ProcMapAmbientFX.h"
#include "TN_BeachQuadLane.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/**
 * Una pasada del quad sin actor: lo que replica el servidor (hora de salida y sentido) y las medidas del paso. Todas las
 * máquinas sacan de aquí dónde va el quad con su reloj del servidor (FTNTrapClock en lo visual, la hora del servidor en los
 * golpes), así que con la misma hora del servidor dan la misma posición. Pruebas: TN_BeachQuadLaneTest.cpp.
 */
struct TORTUNABO_API FTNQuadPass
{
	/** Velocidad del quad (cm/s). */
	static constexpr float Speed = 4200.f;
	/** Lo que recorre metido entre las palmeras antes de salir y después de entrar (cm). */
	static constexpr float PalmMargin = 1500.f;
	/** Tope del retraso con el que el servidor mira a una tortuga de un cliente (s). */
	static constexpr double MaxLagCompensation = 0.35;

	/** Reloj del servidor en que empieza la pasada (en double: en float, a las 20 h de mundo, el quad iría 16 cm desfasado). */
	double PassTime = -1.0;
	/** Sentido: +1 hacia +X del paso, -1 hacia -X. */
	int8 Dir = 1;
	/** Medio largo del paso y medio largo del quad (cm). */
	float HalfLength = 14000.f;
	float QuadHalfLen = 2800.f;

	/** Segundos que dura la pasada entera, de palmera a palmera. */
	float TravelSeconds() const;

	/** Centro del quad a lo largo del paso (X local) en el instante Now del reloj del servidor; false fuera de la pasada. */
	bool QuadXAt(double Now, float& OutX) const;

	/**
	 * Instante del servidor con el que se juzga a una tortuga: la de un cliente va medio ping por detrás en su reloj y sus
	 * movimientos llegan medio ping tarde, así que se mira dónde iba el quad un ping antes (como el molinillo del patio).
	 */
	static double HitEvalTime(double Now, bool bLocallyControlled, float PingMs);
};

/**
 * Paso de quads (ETNBeachElement::QuadLane): el quad gigante de siempre (ATN_QuadActor, deprecado) con aspecto nuevo y sin
 * actor propio. El paso va por el eje X local del actor, centrado en él (girado 90° cruza la playa de lado a lado), con
 * Spec.Extent de largo (0 = el ancho de la playa) y la huella del contrato como semiancho
 * por su Y local (a lo largo del camino). En la arena se ven las rodadas.
 *
 *  - Cada 12-20 s sale un quad enorme (a escala: 56 m de largo, ruedas de 17 m de alto y 7 m de ancho, con su piloto).
 *    Antes, 3,5 s de aviso: temblor de pantalla creciente para quien esté cerca, motor que se acerca y nube de humo y
 *    hojas entre las palmeras del lado por el que va a salir. Cruza a 42 m/s y se mete en las palmeras del otro lado.
 *  - Las ruedas atropellan: derribo con ragdoll y mareo, lanzada dando vueltas (TNBeach::KnockDownTurtle). Entre las
 *    ruedas de un lado y las del otro hay un hueco de 10 m (y 7 m de altura libre bajo el chasis) en el que se sobrevive.
 *  - Red: el servidor solo replica la hora de la próxima pasada y el sentido; cada máquina calcula dónde va el quad con
 *    el reloj de trampa compartido (FTNTrapClock, sin replicar movimiento ni saltos cuando se corrige la hora). Los golpes
 *    los decide el servidor, mirando a la tortuga de cada cliente en el instante que ese cliente veía (FTNQuadPass).
 */
UCLASS()
class TORTUNABO_API ATN_BeachQuadLane : public ATN_BeachEnemy
{
	GENERATED_BODY()

public:
	ATN_BeachQuadLane();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Servidor (pruebas): la próxima pasada empieza ya con su aviso. */
	void DebugPassNow();

	/** Largo del paso (cm, de un borde de la playa al otro). */
	float GetLaneLength() const { return HalfLength * 2.f; }

	/** Un quad lanzado a 42 m/s no se marea con una piedra. */
	virtual bool AcceptsHitStun() const override { return false; }

protected:
	virtual void BeginPlay() override;
	virtual void ApplySpec() override;
	virtual void ServerTick(float DeltaSeconds) override;
	virtual void VisualTick(float DeltaSeconds) override;
	virtual float GetVisualRange() const override { return 60000.f; }

	/** Todas las máquinas: una rueda ha pasado por encima de Victim. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastRunOver(ATortugaCharacter* Victim);

private:
	/** Reloj del servidor en que empieza la próxima pasada (el quad sale de entre las palmeras) y su sentido (+1 hacia +X). */
	UPROPERTY(Replicated)
	double PassTime = -1.0;

	UPROPERTY(Replicated)
	int8 PassDir = 1;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> QuadRoot;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> QuadBody;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Wheels;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Ruts;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> RutsMesh;

	float SizeK = 1.f;
	/** Medio largo del paso y medio largo del quad (cm). */
	float HalfLength = 14000.f;
	float QuadHalfLen = 2800.f;

	// Servidor.
	TMap<TWeakObjectPtr<ATortugaCharacter>, double> LastHit;

	// Visual.
	float WheelSpin = 0.f;
	float WheelGround[4] = {};
	float GroundTimer = 0.f;
	float RutsRetry = 1.f;
	int32 RutsTries = 0;
	double ShownPass = -1.0;
	/** Hora del servidor suavizada con la que se dibuja el quad (la misma que usan las demás trampas de la playa). */
	FTNTrapClock Clock;
	bool bCrashIn = false;
	bool bCrashOut = false;
	TNAmbientFX::FEmitter Smoke;
	TNAmbientFX::FEmitter Leaves;
	TNAmbientFX::FEmitter Dust;

	/** Servidor: programa la próxima pasada dentro de Delay s (más el aviso). */
	void SchedulePass(double Now, float Delay);
	/** La pasada en curso (o la próxima) con lo replicado y las medidas de este paso. */
	FTNQuadPass CurrentPass() const;
	float TravelSeconds() const { return CurrentPass().TravelSeconds(); }
	bool QuadXAt(double Now, float& OutX) const { return CurrentPass().QuadXAt(Now, OutX); }
	/** Rueda i (0 delantera izquierda, 1 delantera derecha, 2 trasera izquierda, 3 trasera derecha) en el espacio del paso. */
	FVector WheelLocal(int32 Index, float QuadX) const;
	void BuildQuad();
	bool BuildRuts();
};
