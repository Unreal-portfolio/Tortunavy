#pragma once

#include "CoreMinimal.h"
#include "TN_RagdollNet.generated.h"

/**
 * Ragdoll del derribo en red (#153, Docs/Ragdoll_Red.md). El servidor decide el derribo y simula el ragdoll que manda;
 * replica una pose raíz comprimida (cuerpo raíz del Physics Asset) a SendRateHz mientras el cuerpo se mueve, y una vez más
 * cuando se asienta. Los clientes simulan su propio ragdoll y lo corrigen hacia esa pose: con velocidad mientras se
 * mueve, de golpe si se ha ido lejos y, asentado, colocado en el mismo punto que el servidor y dormido. Al levantarse, la
 * cápsula va al punto que eligió el servidor (KnockdownStandLocation), el mismo en todas las máquinas.
 *
 * Este fichero tiene los datos que viajan, los ajustes y las decisiones puras (sin mundo ni actores); las usa
 * ATortugaCharacter (TortugaCharacter_RagdollNet.cpp) y las prueba Tortunabo.RagdollNet.
 */

/** Pose raíz del ragdoll del derribo que manda el servidor. Viaja entera (NetSerialize): unos 25 bytes por envío. */
USTRUCT()
struct TORTUNABO_API FTNRagdollRootPose
{
	GENERATED_BODY()

	/** Posición del cuerpo raíz en el mundo; viaja con 0,1 cm de precisión. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** Giro del cuerpo raíz; viaja en 16 bits por eje (0,0055°). */
	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	/** Velocidad lineal del cuerpo raíz (cm/s); viaja con 1 cm/s de precisión. Cero si está asentado. */
	UPROPERTY()
	FVector Velocity = FVector::ZeroVector;

	/** El ragdoll del derribo está activo en el servidor. */
	UPROPERTY()
	bool bActive = false;

	/** El cuerpo se ha asentado (quieto y dormido en el servidor): los clientes lo colocan en el mismo punto y lo duermen. */
	UPROPERTY()
	bool bSettled = false;

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FTNRagdollRootPose> : public TStructOpsTypeTraitsBase2<FTNRagdollRootPose>
{
	enum
	{
		WithNetSerializer = true,
	};
};

/** Ajustes del ragdoll del derribo en red (Class Defaults de la tortuga, Knockdown|Red). */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTNRagdollNetTuning
{
	GENERATED_BODY()

	/** Envíos por segundo de la pose raíz mientras el cuerpo se mueve. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Red", meta = (ClampMin = "5.0", ClampMax = "30.0"))
	float SendRateHz = 15.f;

	/** Desplazamiento mínimo (cm) desde el último envío para volver a mandar la pose. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Red", meta = (ClampMin = "0.0"))
	float SendMinMove = 1.f;

	/** Por debajo de esta velocidad lineal (cm/s) el cuerpo cuenta como quieto. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asentado", meta = (ClampMin = "0.0"))
	float SettleLinearSpeed = 15.f;

	/** Por debajo de esta velocidad de giro (°/s) el cuerpo cuenta como quieto. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asentado", meta = (ClampMin = "0.0"))
	float SettleAngularSpeedDeg = 45.f;

	/** Segundos seguidos quieto para darlo por asentado (y dormirlo). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Asentado", meta = (ClampMin = "0.0"))
	float SettleSeconds = 0.4f;

	/** Error (cm) a partir del cual el cliente coloca su ragdoll de golpe en la pose del servidor (p. ej., al entrar tarde). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Correccion", meta = (ClampMin = "10.0"))
	float SnapDistance = 200.f;

	/** Error (cm) que se tolera sin corregir mientras se mueve (evita temblores). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Correccion", meta = (ClampMin = "0.0"))
	float DeadZone = 3.f;

	/** Error vertical (cm) que se tolera mientras se mueve: la altura del cuerpo raíz depende de la postura. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Correccion", meta = (ClampMin = "0.0"))
	float VerticalDeadZone = 25.f;

	/** Error (cm) con el cuerpo asentado a partir del cual el cliente lo coloca en el punto del servidor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Correccion", meta = (ClampMin = "0.0"))
	float SettledTolerance = 2.f;

	/** Velocidad de corrección por cm de error (1/s): cuánto tira la pose del servidor del ragdoll del cliente. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Correccion", meta = (ClampMin = "0.0"))
	float PositionGain = 6.f;

	/** Rapidez (1/s) con la que la velocidad del cliente se acerca a la buscada. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Correccion", meta = (ClampMin = "0.0"))
	float VelocityBlendRate = 10.f;

	/** Tope (cm/s) de la velocidad que añade la corrección por el error de posición. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Correccion", meta = (ClampMin = "0.0"))
	float MaxCorrectionSpeed = 1200.f;

	/** Tope (s) de la extrapolación de la pose recibida (antigüedad más media latencia). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Correccion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxExtrapolationSeconds = 0.25f;

	/** Distancia horizontal (cm) del cuerpo a la que otra tortuga lo empuja. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Empuje", meta = (ClampMin = "0.0"))
	float PushRadius = 75.f;

	/** Diferencia de altura (cm) máxima entre la tortuga que empuja y el cuerpo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Empuje", meta = (ClampMin = "0.0"))
	float PushMaxHeight = 120.f;

	/** Velocidad horizontal mínima (cm/s) de quien empuja. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Empuje", meta = (ClampMin = "0.0"))
	float PushMinSpeed = 50.f;

	/** Aceleración (cm/s²) que da el empuje al cuerpo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Empuje", meta = (ClampMin = "0.0"))
	float PushAcceleration = 1800.f;

	/** El cuerpo empujado no pasa de la velocidad de quien empuja por este factor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Empuje", meta = (ClampMin = "0.0"))
	float PushSpeedFactor = 1.f;
};

namespace TNRagdollNet
{
	/** Qué hace el cliente con su ragdoll en este fotograma. */
	enum class ECorrectionMode : uint8
	{
		/** Dentro de la tolerancia: nada. */
		None,
		/** Cambio de velocidad hacia la pose del servidor (DeltaVelocity a todos los cuerpos). */
		Nudge,
		/** Lejos: traslada todo el ragdoll (Translation), gira alrededor del raíz y toma la velocidad del servidor. */
		Snap,
		/** Asentado: traslada todo el ragdoll al punto del servidor, sin velocidad, y lo duerme. */
		SnapAndSleep,
	};

	struct FCorrection
	{
		ECorrectionMode Mode = ECorrectionMode::None;
		FVector Translation = FVector::ZeroVector;
		FVector DeltaVelocity = FVector::ZeroVector;
	};

	/** Redondea a la décima de centímetro: la misma precisión con la que viaja (FVector_NetQuantize10). */
	TORTUNABO_API FVector QuantizeLocation(const FVector& Location);

	/**
	 * Dónde se levanta la tortuga (fase 1, antes #23). El servidor usa el punto que calcula; un cliente, el del servidor
	 * si ha llegado. Solo si no hay ninguno (no debería), el suyo.
	 */
	TORTUNABO_API FVector ResolveStandLocation(bool bHasAuthority, const FVector& ServerStandLocation, const FVector& LocalStandLocation);

	/** Pose del servidor que persigue el cliente: la recibida, adelantada con su velocidad (antigüedad + media latencia, con tope). */
	TORTUNABO_API FVector ExtrapolateTarget(const FTNRagdollRootPose& Pose, float AgeSeconds, float OneWayLatencySeconds, const FTNRagdollNetTuning& Tuning);

	/** Servidor: si toca mandar la pose nueva (ritmo de envío y desplazamiento mínimo; siempre al cambiar el asentado). */
	TORTUNABO_API bool ShouldSendPose(float SecondsSinceLastSend, const FTNRagdollRootPose& LastSent, const FTNRagdollRootPose& Current, const FTNRagdollNetTuning& Tuning);

	/** Servidor: segundos seguidos quieto, con este fotograma (vuelve a 0 en cuanto se mueve). */
	TORTUNABO_API float AdvanceSettleTimer(float SettleTimer, float LinearSpeed, float AngularSpeedDeg, float DeltaSeconds, const FTNRagdollNetTuning& Tuning);

	/** Servidor: asentado si lleva SettleSeconds quieto. */
	TORTUNABO_API bool IsSettled(float SettleTimer, const FTNRagdollNetTuning& Tuning);

	/** Cliente: corrección de su ragdoll (cuerpo raíz en LocalLocation con LocalVelocity) hacia la pose del servidor. */
	TORTUNABO_API FCorrection ComputeCorrection(const FVector& LocalLocation, const FVector& LocalVelocity, const FVector& TargetLocation,
		const FVector& TargetVelocity, bool bTargetSettled, float DeltaSeconds, const FTNRagdollNetTuning& Tuning);

	/**
	 * Servidor: cambio de velocidad que da a un cuerpo derribado (raíz en BodyLocation con BodyVelocity) una tortuga que
	 * camina contra él. Horizontal, solo si está cerca, a una altura parecida y yendo hacia el cuerpo; nunca lo lleva más
	 * rápido que ella.
	 */
	TORTUNABO_API FVector ComputePushVelocityChange(const FVector& PusherLocation, const FVector& PusherVelocity, const FVector& BodyLocation,
		const FVector& BodyVelocity, float DeltaSeconds, const FTNRagdollNetTuning& Tuning);
}
