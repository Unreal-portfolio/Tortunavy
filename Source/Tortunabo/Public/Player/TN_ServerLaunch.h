#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementReplication.h"

/**
 * Lanzamiento que decide el servidor y que el cliente dueño estrena en su propio movimiento
 * (UTN_TurtleMovementComponent::LaunchFromServer).
 *
 * Un LaunchCharacter del servidor sobre la tortuga de un cliente remoto entra en el siguiente movimiento de ese cliente que
 * simula el servidor; el cliente no sabe nada hasta que le llega la corrección (de 12 a 85 cm con 120 ms de latencia en
 * el empujón de la mina, #18). Así no hay corrección: el servidor concede el lanzamiento con un número (Grant) y se lo manda
 * al dueño; el dueño lo aplica al empezar su siguiente movimiento y apunta en él el número; el servidor, al simular ese
 * mismo movimiento, aplica la misma velocidad (Take). Los dos lanzan en el mismo paso y con la misma velocidad (en cm/s
 * enteros, Quantize, que viajan por red sin perder nada). Si tras una corrección el dueño repite sus movimientos, el del
 * lanzamiento lo vuelve a aplicar (GetForMove). Si el movimiento con el número no llega a tiempo, el servidor lanza por su
 * cuenta y corrige, como antes.
 *
 * Lógica pura, sin mundo ni red: la recorre la prueba Tortunabo.Movement.ServerLaunch.
 */
struct TORTUNABO_API FTNServerLaunch
{
	/** Segundos que espera el servidor el movimiento con el número antes de lanzar por su cuenta (y que el dueño lo acepta). */
	static constexpr float TimeoutSeconds = 1.f;

	/** La velocidad tal como viaja por red sin perder nada: cm/s enteros. */
	static FVector Quantize(const FVector& Velocity);

	// ── Servidor ─────────────────────────────────────────────────────────────

	/**
	 * Concede a la hora del servidor Now un lanzamiento (sustituye al que hubiera sin usar): su número (nunca 0) y la
	 * velocidad que hay que mandar.
	 */
	uint8 Grant(const FVector& Velocity, double Now, FVector& OutSentVelocity);

	/** Si Id es el del lanzamiento concedido y sin usar, lo gasta y da su velocidad. */
	bool Take(uint8 Id, FVector& OutVelocity);

	/** Si el lanzamiento concedido sigue sin usar pasado TimeoutSeconds a la hora Now, lo gasta y da su velocidad. */
	bool TakeExpired(double Now, FVector& OutVelocity);

	// ── Cliente dueño ────────────────────────────────────────────────────────

	/** Llega un lanzamiento concedido a la hora local Now: se aplica al empezar el siguiente movimiento. */
	void Receive(uint8 Id, const FVector& Velocity, double Now);

	/** Al empezar un movimiento nuevo a la hora local Now: el lanzamiento recibido que toca aplicar (pasa a esperar el movimiento). */
	bool BeginMove(double Now, FVector& OutVelocity);

	/** Esperando su movimiento (entre BeginMove y ConfirmMove). */
	bool IsStarting() const { return ClientState == EClientState::Starting; }

	/**
	 * El movimiento TimeStamp aplica ahora el lanzamiento Velocity: si es el concedido, queda apuntado en ese movimiento. Si
	 * otro lanzamiento lo ha sustituido, se reintenta en el siguiente movimiento.
	 */
	bool ConfirmMove(float TimeStamp, const FVector& Velocity);

	/** El motor no llegó a hacer el movimiento: se reintenta en el siguiente. */
	void RetryMove();

	/** Número que va con el movimiento TimeStamp (0 si no estrena ningún lanzamiento). */
	uint8 GetIdForMove(float TimeStamp) const;

	/** Al repetir movimientos tras una corrección: la velocidad del lanzamiento que estrenó el movimiento TimeStamp. */
	bool GetForMove(float TimeStamp, FVector& OutVelocity) const;

private:
	enum class EClientState : uint8
	{
		None,
		Received,
		Starting,
		Applied,
	};

	uint8 LastGrantedId = 0;
	/** Lanzamiento concedido sin usar (0 = ninguno). */
	uint8 GrantedId = 0;
	FVector GrantedVelocity = FVector::ZeroVector;
	double GrantedAt = 0.0;

	EClientState ClientState = EClientState::None;
	uint8 ClientId = 0;
	FVector ClientVelocity = FVector::ZeroVector;
	double ClientReceivedAt = 0.0;
	float ClientTimeStamp = -1.f;
};

/**
 * Lo que manda el cliente al servidor en cada movimiento: lo de serie más el número del lanzamiento concedido que estrena
 * ese movimiento (FTNServerLaunch) y, si el movimiento pide el panzazo (marca TNDiveLogic::DiveRequestFlag, #24), su
 * dirección. Sin lanzamiento ocupa un bit; sin panzazo, nada.
 */
struct FTNTurtleNetworkMoveData : public FCharacterNetworkMoveData
{
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;

	uint8 LaunchId = 0;

	/** Giro del panzazo que pide este movimiento (TNDiveLogic::CompressDiveYaw; 0 sin panzazo). */
	uint16 DiveYaw = 0;

	/** Topes predichos que pide este movimiento (bits de TNMovementLimits: llevar a otra, mareo; #575, #574). */
	uint8 PredictedCaps = 0;
};
