#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachTrapCommon.h"
#include "TN_BeachWobblyPlatform.generated.h"

class ACharacter;
class UBoxComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UTN_BeachTrapSynthComponent;

/**
 * Pose de la tabla del servidor tal como viaja (TNWobblyPlatformNet): alabeo, cabeceo y hundimiento, y del muelle de cada
 * eje su velocidad y hacia dónde tira. Con eso el cliente mueve el mismo muelle desde la última muestra en vez de ir detrás
 * de ella.
 */
USTRUCT()
struct FTNWobblyPoseNet
{
	GENERATED_BODY()

	/** Alabeo y cabeceo (pasos de RollStepDeg y PitchStepDeg). */
	UPROPERTY()
	int8 Roll = 0;

	UPROPERTY()
	int8 Pitch = 0;

	/** Velocidad del muelle (pasos de RollRateStepDeg y PitchRateStepDeg por segundo). */
	UPROPERTY()
	int8 RollRate = 0;

	UPROPERTY()
	int8 PitchRate = 0;

	/** Hacia dónde tira el muelle (los mismos pasos que la pose). */
	UPROPERTY()
	int8 RollTarget = 0;

	UPROPERTY()
	int8 PitchTarget = 0;

	/** Hundimiento (pasos de SagStepCm). */
	UPROPERTY()
	uint8 Sag = 0;

	bool operator==(const FTNWobblyPoseNet& Other) const = default;
};

/**
 * Red de la plataforma tambaleante (issue #20): la pose de la tabla que calcula el servidor viaja en siete bytes
 * (FTNWobblyPoseNet). Pasos pensados para la tabla más grande (150 cm de semiancho y 434 de semilargo): medio paso de error
 * son 2,6 mm en el borde por el alabeo, 1 mm en las puntas por el cabeceo y 0,5 mm de hundimiento.
 */
namespace TNWobblyPlatformNet
{
	/** Alabeo (grados por paso): ±25,4° en un int8 (el muelle llega a ±21° con MaxRollDeg = 15). */
	constexpr float RollStepDeg = 0.2f;
	/** Cabeceo (grados por paso): ±3,175° en un int8 (el muelle se queda en ±3°). */
	constexpr float PitchStepDeg = 0.025f;
	/** Hundimiento (cm por paso): de 0 a 25,5 cm en un uint8 (ocho tortugas encima y la grieta a punto: 18 cm). */
	constexpr float SagStepCm = 0.1f;

	inline int8 QuantizeRoll(float Deg)
	{
		return static_cast<int8>(FMath::Clamp(FMath::RoundToInt(Deg / RollStepDeg), -127, 127));
	}

	inline float DequantizeRoll(int8 Quantized)
	{
		return static_cast<float>(Quantized) * RollStepDeg;
	}

	inline int8 QuantizePitch(float Deg)
	{
		return static_cast<int8>(FMath::Clamp(FMath::RoundToInt(Deg / PitchStepDeg), -127, 127));
	}

	inline float DequantizePitch(int8 Quantized)
	{
		return static_cast<float>(Quantized) * PitchStepDeg;
	}

	inline uint8 QuantizeSag(float Cm)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Cm / SagStepCm), 0, 255));
	}

	inline float DequantizeSag(uint8 Quantized)
	{
		return static_cast<float>(Quantized) * SagStepCm;
	}

	/** Velocidad del alabeo (°/s por paso): ±127 °/s en un int8 (un aterrizaje la empuja hasta 49 °/s). */
	constexpr float RollRateStepDeg = 1.f;
	/** Velocidad del cabeceo (°/s por paso): ±38 °/s en un int8 (un aterrizaje, hasta 11 °/s). */
	constexpr float PitchRateStepDeg = 0.3f;

	inline int8 QuantizeRate(float DegPerSec, float StepDeg)
	{
		return static_cast<int8>(FMath::Clamp(FMath::RoundToInt(DegPerSec / StepDeg), -127, 127));
	}

	inline float DequantizeRate(int8 Quantized, float StepDeg)
	{
		return static_cast<float>(Quantized) * StepDeg;
	}

	/** Muelle poco amortiguado de cada eje: rigidez (1/s²) y amortiguación (1/s). */
	constexpr float RollStiffness = 55.f;
	constexpr float RollDamping = 3.5f;
	constexpr float PitchStiffness = 80.f;
	constexpr float PitchDamping = 6.f;
	/** El alabeo se pasa de MaxRollDeg hasta este factor; el cabeceo no sale de ±PitchLimitDeg. */
	constexpr float RollOvershoot = 1.4f;
	constexpr float PitchLimitDeg = 3.f;
	/** Paso de integración más largo (s): con más, el muelle se saldría. */
	constexpr float MaxSpringStep = 0.05f;

	/** Ladeo hacia quien está encima: grados por tortuga en el borde (alabeo) o en la punta (cabeceo, con su tope). */
	constexpr float RollLeanDeg = 4.5f;
	constexpr float PitchLeanDeg = 1.2f;
	constexpr float PitchLeanLimitDeg = 2.f;
	/** Mecerse al andar encima: amplitud (°) con la tortuga más rápida y pulsación (rad/s). */
	constexpr float RockDeg = 2.5f;
	constexpr float RockRadPerSec = 5.5f;
	/** Sacudida de un aterrizaje en la velocidad del muelle (°/s por unidad de golpe). */
	constexpr float RollKickDegPerSec = 35.f;
	constexpr float PitchKickDegPerSec = 8.f;

	/** Estado de la tabla: pose, velocidad del muelle y hacia dónde tira cada eje (grados, °/s y cm de hundimiento). */
	struct FPoseState
	{
		float Roll = 0.f;
		float RollVel = 0.f;
		float RollTarget = 0.f;
		float Pitch = 0.f;
		float PitchVel = 0.f;
		float PitchTarget = 0.f;
		float Sag = 0.f;
	};

	/** Lo que ve el servidor encima en un fotograma: ladeo medio (-1..1 por tortuga), golpe de los aterrizajes y meneo. */
	struct FRiderInput
	{
		float SumY = 0.f;
		float SumX = 0.f;
		float Kick = 0.f;
		float Motion = 0.f;
	};

	FTNWobblyPoseNet EncodePose(const FPoseState& State);
	FPoseState DecodePose(const FTNWobblyPoseNet& Net);

	/** Servidor: un fotograma del muelle con lo que hay encima (Now, reloj de la trampa para el meneo). */
	void StepServerPose(FPoseState& State, const FRiderInput& Input, double Now, float DeltaSeconds, float MaxRollDeg);

	/** Cliente: adelanta el muelle Seconds con lo que tira el servidor en la última muestra (en pasos de MaxSpringStep). */
	void AdvancePose(FPoseState& State, float Seconds, float MaxRollDeg);
}

/**
 * Plataforma sobre un hoyo de la arena. El hoyo lo trae el elemento en su malla: como el terreno es fijo y no se cava, es
 * un cráter de arena amontonada alrededor (la que sacaron los niños al cavar) con el fondo a ras del suelo (Z = 0) y una
 * brecha en el lado +Y por la que se sale andando. Medidas con SizeScale = 1 (se ajustan a la huella, 900·SizeScale):
 * borde de fuera a 870, cresta de 90 de ancho a ~240 de alto, ladera de fuera a 30° (se sube andando), pared de dentro
 * empinada y hoyo de ~364 de radio arriba.
 *
 * Encima, a lo largo de X (X local = sentido de la carrera), una tabla vieja o una tapa de nevera (según la semilla) de
 * 2·(radio del hoyo + 70) de largo, apoyada en la cresta. Se tambalea con una tortuga (balanceo de lado hasta MaxRollDeg
 * con un muelle poco amortiguado; los aterrizajes la sacuden) y cruje. Con BreakRiders o más tortugas a la vez, se va
 * agrietando (tiembla y cruje cada vez más) y a los CrackSeconds se parte por la mitad: las dos mitades caen al hoyo y
 * se quedan como rampas (~34°) de la cresta al fondo; quien estuviera encima cae al hoyo y sale por la brecha o subiendo
 * por una mitad. No se recompone en la ronda (el generador recoloca todo en la siguiente).
 *
 * Red: la tabla es una base móvil (UBoxComponent con nombre estable). Su pose (alabeo, cabeceo y hundimiento) la calcula
 * el servidor con las tortugas que ve él y la replica en siete bytes (NetPose: la pose y, de cada eje del muelle, su
 * velocidad y hacia dónde tira) hasta 15 veces por segundo, solo mientras cambia (despierta la réplica dormida), y en el
 * acto si un aterrizaje la sacude. Cada cliente mueve el mismo muelle desde la última muestra, adelantada media ida y
 * vuelta: suavizarla hacia la muestra la dejaba unos 90 ms detrás, hasta 9,6 cm en el borde al aterrizar. El temblor
 * de la grieta solo está en la malla visual. Antes cada máquina movía el muelle con las tortugas que veía (las demás le
 * llegan con retraso) y la tabla podía ir hasta 9,8° distinta: 19-25 cm en el borde (issue #20). La rotura la decide el
 * servidor (BrokenAt, hora del servidor replicada) y cada máquina anima la caída de las mitades desde esa hora.
 */
UCLASS()
class TORTUNABO_API ATN_BeachWobblyPlatform : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachWobblyPlatform();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool IsBroken() const { return BrokenAt >= 0.f; }

	/** Tortugas a la vez que la rompen. */
	UPROPERTY(EditAnywhere, Category = "Plataforma", meta = (ClampMin = "1", ClampMax = "8"))
	int32 BreakRiders = 2;

	/** Segundos con BreakRiders tortugas encima hasta partirse (baja despacio si se bajan). */
	UPROPERTY(EditAnywhere, Category = "Plataforma", meta = (ClampMin = "0.1"))
	float CrackSeconds = 1.1f;

	/** Balanceo máximo de lado a lado (grados). */
	UPROPERTY(EditAnywhere, Category = "Plataforma", meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float MaxRollDeg = 7.f;

	/** Volumen de los crujidos. */
	UPROPERTY(EditAnywhere, Category = "Plataforma", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float CreakVolume = 0.9f;

	/** Veces por segundo que la pose de la tabla puede salir hacia los clientes mientras cambia. */
	static constexpr float PoseNetFrequency = 15.f;

protected:
	virtual void BeginPlay() override;
	virtual void ApplySpec() override;

	/**
	 * La red de la ronda (relevancia y dormida al aparecer) y, despierta, PoseNetFrequency sin bajar: con la frecuencia
	 * adaptativa, tras un rato quieta, tardaría más de un segundo en volver a subir.
	 */
	virtual void ApplyRoundNetProfile() override;

	UFUNCTION()
	void OnRep_Broken();

	/** Hora del servidor en que se partió (< 0 = entera). */
	UPROPERTY(ReplicatedUsing = OnRep_Broken)
	float BrokenAt = -1.f;

	/** Clientes: la muestra nueva del servidor pasa a ser su pose, adelantada media ida y vuelta. */
	UFUNCTION()
	void OnRep_NetPose();

	/** Pose de la tabla del servidor (TNWobblyPlatformNet): alabeo, cabeceo, hundimiento y el muelle de cada eje. */
	UPROPERTY(ReplicatedUsing = OnRep_NetPose)
	FTNWobblyPoseNet NetPose;

	/** Cráter de arena con la brecha. */
	UPROPERTY(VisibleAnywhere, Category = "Plataforma")
	TObjectPtr<UStaticMeshComponent> CraterMesh;

	UPROPERTY(VisibleAnywhere, Category = "Plataforma")
	TObjectPtr<UProceduralMeshComponent> CraterCollision;

	/** Centro de la tabla sobre la cresta: se inclina. */
	UPROPERTY(VisibleAnywhere, Category = "Plataforma")
	TObjectPtr<USceneComponent> BoardPivot;

	UPROPERTY(VisibleAnywhere, Category = "Plataforma")
	TObjectPtr<UStaticMeshComponent> BoardMesh;

	/** Tabla entera: base móvil. */
	UPROPERTY(VisibleAnywhere, Category = "Plataforma")
	TObjectPtr<UBoxComponent> BoardBox;

	/** Bisagras de las mitades en el borde de dentro de la cresta (-X y +X; la de +X mira hacia -X). */
	UPROPERTY(VisibleAnywhere, Category = "Plataforma")
	TObjectPtr<USceneComponent> HalfPivotA;

	UPROPERTY(VisibleAnywhere, Category = "Plataforma")
	TObjectPtr<USceneComponent> HalfPivotB;

	UPROPERTY(VisibleAnywhere, Category = "Plataforma")
	TObjectPtr<UStaticMeshComponent> HalfMeshA;

	UPROPERTY(VisibleAnywhere, Category = "Plataforma")
	TObjectPtr<UStaticMeshComponent> HalfMeshB;

	/** Mitades caídas: rampas del fondo a la cresta (solo con colisión al terminar de caer). */
	UPROPERTY(VisibleAnywhere, Category = "Plataforma")
	TObjectPtr<UBoxComponent> HalfBoxA;

	UPROPERTY(VisibleAnywhere, Category = "Plataforma")
	TObjectPtr<UBoxComponent> HalfBoxB;

	UPROPERTY(Transient)
	TObjectPtr<UTN_BeachTrapSynthComponent> Voice;

private:
	struct FRider
	{
		float LastVz = 0.f;
		float CreakTimer = 0.f;
		bool bOn = false;
		bool bNear = false;
	};

	void TickBoard(float DeltaSeconds, double Now);
	void TickHalves(double Now);

	/** Servidor: la parte. */
	void BreakBoard();

	/** Estado roto en esta máquina (colisiones, mallas y efectos). */
	void ApplyBroken(bool bWithFX);

	void PlayCreak(float Volume, float PitchMul, const FVector& WorldAt);

	double RimHeight = 240.0;
	double PitRadius = 364.0;
	double FloorRadius = 280.0;
	double OuterRadius = 870.0;
	double BoardHalfL = 434.0;
	double BoardHalfW = 110.0;
	double BoardThick = 24.0;
	double DropAngleDeg = 33.0;

	/**
	 * Servidor: pose cuantizada en NetPose; si ha cambiado, despierta la réplica (una vez por segundo como mucho) y, con
	 * bUrgent (un aterrizaje la ha sacudido), la manda en este mismo fotograma.
	 */
	void PublishPose(double Now, bool bUrgent);

	/** Clientes: el muelle desde la última muestra del servidor (la primera vuelta tras recibirla ya está al día). */
	void FollowServerPose(float DeltaSeconds);

	/** Clientes: media ida y vuelta (s) con el servidor, lo que tiene de vieja una muestra al llegar. */
	float ClientLeadSeconds() const;

	/**
	 * Pose de la tabla: en el servidor, su muelle; en los clientes, el mismo muelle desde la última muestra. El hundimiento
	 * (cm) lo dan las tortugas encima y la grieta del servidor.
	 */
	TNWobblyPlatformNet::FPoseState Pose;
	/** Clientes: la muestra acaba de llegar en este fotograma y ya está adelantada; no se mueve otra vez. */
	bool bPoseJustSeeded = false;
	/** Servidor: no despierta la réplica otra vez antes de esta hora. */
	double NextNetWake = 0.0;
	/** Grieta en esta máquina: en el servidor decide la rotura; en los clientes solo crujidos, astillas y temblor de la malla. */
	float Crack = 0.f;
	float CrackCreakTimer = 0.f;
	bool bBrokenApplied = false;
	bool bHalvesLanded = false;
	bool bHalvesSettled = false;

	TMap<TWeakObjectPtr<ACharacter>, FRider> Riders;
	FTNTrapClock Clock;
	FTNTrapBurst Chips;
	FTNTrapBurst Dust;
	FTNTrapPopText Pop;
};
