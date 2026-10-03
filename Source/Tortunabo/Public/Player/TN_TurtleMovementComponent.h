#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/TN_DiveDecisions.h"
#include "Player/TN_ServerLaunch.h"
#include "Player/TN_TurtleSurface.h"
#include "TN_TurtleMovementComponent.generated.h"

class ATortugaCharacter;
class ATN_ProcMapGenerator;

/** Fase del panzazo en el suelo. La simulan igual el cliente dueño (predicha y guardada en sus movimientos) y el servidor. */
enum class ETNBellyPhase : uint8
{
	/** Normal, o el panzazo aún en el aire. */
	None = 0,
	/** Arrastrándose sobre la tripa: poco rozamiento, sigue las pendientes y rebota contra lo que encuentra. */
	Slide = 1,
	/** Ya parada pero sin sitio encima para ponerse de pie (algo bajo encima): repta despacio hasta que lo haya. */
	Rest = 2,
	/** De pie otra vez (cápsula entera): unos instantes con la velocidad recortada mientras se levanta. */
	GetUp = 3,
};

/**
 * Movimientos del cliente al servidor sin bases que el servidor no puede encontrar por red (ver
 * UTN_TurtleMovementComponent::IsNetResolvableBase). Sobre una base que se mueve (movilidad Movable) el motor manda la
 * posición relativa a ella y la base; si esa base es una malla creada en ejecución (las teselas del terreno de la playa,
 * el decorado local, las piezas de los elementos), el servidor la recibe nula y toma la posición relativa como absoluta.
 * Con estas bases se manda la posición y la aceleración del mundo, sin base.
 */
struct FTNTurtleNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	/** Con los datos de la tortuga (FTNTurtleNetworkMoveData: también el número del lanzamiento concedido que estrenan). */
	FTNTurtleNetworkMoveDataContainer();

	/** Número del lanzamiento concedido que lleva Data, si es uno de estos datos (0 si no). */
	uint8 GetLaunchId(const FCharacterNetworkMoveData* Data) const;

	virtual void ClientFillNetworkMoveData(const FSavedMove_Character* ClientNewMove, const FSavedMove_Character* ClientPendingMove,
		const FSavedMove_Character* ClientOldMove) override;

private:
	FTNTurtleNetworkMoveData TurtleMoveData[3];
};

/**
 * Movimiento de la tortuga: el de UCharacterMovementComponent más el arrastre del panzazo.
 *
 * Al caer de tripa (ATortugaCharacter::IsDiving, número de panzazo nuevo), en vez de quedarse tiesa se arrastra: conserva
 * la inercia a lo largo del suelo (en una bajada, parte de la caída se convierte en arrastre), frena según la superficie
 * (TNTurtleSurface: arena mucho, agua y fango poco; entre 0,6 y 1,2 s), sigue las pendientes (cuesta abajo se desliza
 * más; en las suaves se queda quieta) y rebota un poco contra paredes y obstáculos. Casi parada, se levanta: la cápsula
 * vuelve a su altura sin subir por dentro de nada (si no cabe, repta sobre la tripa hasta que quepa) y unos instantes
 * anda más despacio. Saltando o moviéndose por debajo de BellyExitSpeed sale antes (el salto es un brinco).
 *
 * Todo ocurre dentro de la simulación del movimiento, con el estado (fase, tiempo y número de panzazo) guardado en cada
 * movimiento del cliente (FTNSavedMove_Turtle): el cliente dueño lo predice igual que el servidor y, si hay corrección,
 * lo repite desde el estado de entonces. No hay física de verdad (desincronizaría y atravesaría paredes al volver a
 * poner la cápsula); la cápsula barre como siempre. El servidor acaba el panzazo (ATortugaCharacter::TickDive) cuando
 * este componente dice que ya se ha levantado; el resto de máquinas solo ven el movimiento replicado.
 *
 * Tumbada, además, el cuerpo entero choca: la cabeza y las patas, que sobresalen de la cápsula, no se meten en las paredes
 * (KeepBellyBodyOutOfWalls).
 *
 * Consola (igual en todas las máquinas; en PIE es una sola): TN.Dive.Slide, TN.Dive.Friction, TN.Dive.Slope,
 * TN.Dive.SlopeFall, TN.Dive.WallBounce, TN.Dive.MaxTime, TN.Dive.Body y TN.Dive.Debug. Ver Docs/Animacion_Tortuga.md.
 */
UCLASS()
class TORTUNABO_API UTN_TurtleMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UTN_TurtleMovementComponent();

	// ── Estado del arrastre (lo leen el personaje, la animación, el sonido y el polvo) ──

	ETNBellyPhase GetBellyPhase() const { return BellyPhase; }

	/** Segundos en la fase actual (arrastrándose, reptando o levantándose), sin los de cuesta abajo (GetBellySlopeTime). */
	float GetBellyTime() const { return BellyTime; }

	/** Segundos de este arrastre cuesta abajo (pendiente de BellySlopeMinAngle o más): en ellos BellyTime no corre. */
	float GetBellySlopeTime() const { return BellySlopeTime; }

	/** Número del panzazo del que viene el arrastre (0 = ninguno todavía). */
	uint8 GetSlideSerial() const { return SlideSerial; }

	bool IsBellySliding() const { return BellyPhase == ETNBellyPhase::Slide; }

	/** En el suelo sobre la tripa (arrastrándose o reptando sin sitio para levantarse). */
	bool IsOnBelly() const { return BellyPhase == ETNBellyPhase::Slide || BellyPhase == ETNBellyPhase::Rest; }

	/** Ya se ha levantado del panzazo número DiveSerial (su cápsula vuelve a ser la de pie). */
	bool HasStoodUpFromDive(uint8 DiveSerial) const;

	/** Durante el panzazo número DiveSerial admite el movimiento del jugador: casi parada, reptando o ya levantándose. */
	bool AcceptsInputDuringDive(uint8 DiveSerial) const;

	/** Pesos de la superficie bajo la tripa en el último paso del arrastre (arena, tierra, roca, madera y agua). */
	const float* GetSlideSurface() const { return SlideSurface; }

	/**
	 * Repetición de movimientos tras una corrección (FTNSavedMove_Turtle::PrepMoveFor): deja el estado del arrastre como
	 * estaba al empezar ese movimiento. Si entonces iba sobre la tripa, también la cápsula encogida.
	 */
	void RestoreBellyState(uint8 InPhase, float InTime, uint8 InSerial, float InCapsuleHalfHeight, float InSlopeTime);

	/**
	 * Estado del arrastre al empezar el movimiento que se va a guardar (FTNSavedMove_Turtle::SetInitialPosition). El
	 * cliente lee el salto antes de guardar el movimiento: si ese salto la ha levantado de la tripa, devuelve (y olvida)
	 * el estado de antes del salto, como hace el motor con JumpCurrentCountPreJump.
	 */
	void ConsumeMoveStartBellyState(uint8& OutPhase, float& OutTime, uint8& OutSerial, float& OutCapsuleHalfHeight, float& OutSlopeTime);

	// ── UCharacterMovementComponent ──────────────────────────────────────────

	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual FRotator ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime, FRotator& DeltaRotation) const override;
	virtual float GetMaxSpeed() const override;
	virtual bool CanAttemptJump() const override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	/**
	 * Cliente: corrección del servidor. Si el servidor ha quitado la base por no poderse encontrar por red (ver
	 * ServerMoveHandleClientError) y la tortuga anda, busca aquí su suelo, como hace el motor con una base sin resolver: sin
	 * él, el primer paso de los movimientos que se repiten se perdería y daría otra corrección.
	 */
	virtual void ClientAdjustPosition_Implementation(float TimeStamp, FVector NewLoc, FVector NewVel, UPrimitiveComponent* NewBase,
		FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode,
		TOptional<FRotator> OptionalRotation = TOptional<FRotator>()) override;

	// ── Red: bases de movimiento que no se encuentran por red ───────────────

	/**
	 * Si la otra máquina puede encontrar Base por red (o sea, si vale como base relativa en correcciones y movimientos).
	 * No valen las mallas creadas en ejecución sin nombre estable ni réplica: las teselas del terreno y el decorado local de
	 * la playa, las piezas que los elementos montan en ApplySpec, los montículos... Con ellas el motor mandaba posiciones
	 * relativas a algo que el otro lado recibe nulo: el cliente ignoraba todas las correcciones («could not resolve the new
	 * relative movement base actor, ignoring server correction!») y el servidor tomaba posiciones relativas por absolutas.
	 * Con estas bases todo va en coordenadas del mundo (Docs/Modo_Carrera.md, «Seguridad: nunca bajo el mapa»).
	 */
	static bool IsNetResolvableBase(const UPrimitiveComponent* Base);

	// ── Red: lanzamientos que decide el servidor ────────────────────────────

	/**
	 * Servidor: lanza a Character con LaunchVelocity (como LaunchCharacter con las dos componentes sustituidas). Si es la
	 * tortuga de un cliente remoto, el lanzamiento lo estrena su dueño en su siguiente movimiento y el servidor lo aplica en
	 * ese mismo movimiento (FTNServerLaunch): sin corrección. Si no (el anfitrión, otro personaje), LaunchCharacter.
	 */
	static void LaunchFromServer(ACharacter* Character, const FVector& LaunchVelocity);

	const FTNServerLaunch& GetServerLaunch() const { return ServerLaunch; }

	// ── Cápsula ─────────────────────────────────────────────────────────────

	/**
	 * La cápsula de pie ya (el panzazo ha acabado o se ha cortado sin que se levantara), con los pies donde están: como
	 * TryStandUp y, si no cabe de pie (algo encima), igual, sin barrer. Nunca crece en su sitio: así la mitad de abajo
	 * quedaba metida en la malla fina del terreno y, al desincrustarse, la tortuga caía por debajo del mapa. La llaman el
	 * fin del panzazo (ATortugaCharacter::RestoreDiveCapsule) y, por si acaso, cada movimiento fuera del panzazo. true si ha
	 * cambiado algo.
	 */
	bool RestoreStandingCapsule();

	// ── Ajustes del arrastre ─────────────────────────────────────────────────
	// Rozamiento por superficie: lo que frena por sí solo (cm/s²), aparte del freno por velocidad (BellyDrag). Con la
	// entrada típica (panzazo andando: 350 + 450 cm/s, un 90 % tras el golpe = 720 cm/s) y BellyDrag 1,5 se para en:
	// arena 0,57 s (1,8 m), tierra 0,79 s (2,3 m), roca 0,87 s (2,5 m), madera 1 s (2,7 m) y agua o fango 1,18 s (3,1 m).
	// TN.Dive.Friction los multiplica todos en caliente.

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Friction", meta = (ClampMin = "0.0"))
	float BellyFrictionSand = 800.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Friction", meta = (ClampMin = "0.0"))
	float BellyFrictionSoil = 480.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Friction", meta = (ClampMin = "0.0"))
	float BellyFrictionRock = 400.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Friction", meta = (ClampMin = "0.0"))
	float BellyFrictionWood = 310.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Friction", meta = (ClampMin = "0.0"))
	float BellyFrictionWater = 220.f;

	/** Freno proporcional a la velocidad (1/s): corta antes los arrastres muy rápidos. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Friction", meta = (ClampMin = "0.0"))
	float BellyDrag = 1.5f;

	/** Pasado este tiempo arrastrándose (s), el rozamiento crece: ninguna bajada la arrastra para siempre. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Friction", meta = (ClampMin = "0.0"))
	float BellyFrictionRampStart = 1.2f;

	/** Cuánto crece el rozamiento por segundo pasado BellyFrictionRampStart (2,5 = x3,5 al segundo). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Friction", meta = (ClampMin = "0.0"))
	float BellyFrictionRamp = 2.5f;

	/** Velocidad a lo largo del suelo que conserva al caer de tripa (el resto se lo come el golpe). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Speed", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BellyLandingKeep = 0.9f;

	/** Tope de la velocidad al empezar a arrastrarse (cm/s): algo más que esprintar, pero frena en seguida. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Speed", meta = (ClampMin = "0.0"))
	float BellyMaxEntrySpeed = 850.f;

	/** Tope de la velocidad arrastrándose (cm/s), también cuesta abajo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Speed", meta = (ClampMin = "0.0"))
	float BellyMaxSpeed = 1000.f;

	/** Multiplica la gravedad a lo largo de las pendientes (cuesta abajo acelera; cuesta arriba frena antes). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Speed", meta = (ClampMin = "0.0"))
	float BellySlopeGravity = 1.15f;

	// ── Pendiente: cuesta abajo sigue cayendo (E9-01, #62) ───────────────────
	// Con el rozamiento de la arena (800) solo aceleraba por encima de 45°, que ya no es suelo: en la playa no se deslizaba
	// por ninguna cuesta. Ahora, cuesta abajo desde BellySlopeMinAngle, el rozamiento y el freno por velocidad se multiplican
	// por BellySlopeFrictionScale y BellySlopeDragScale (arena a 25°: más de 200 cm/s tras 2 s; el llano no cambia). Cuesta
	// abajo el tiempo del arrastre no corre (ni la rampa de rozamiento ni BellyMaxSeconds); el tope es BellySlopeMaxSeconds.
	// Al caer de tripa en una bajada, la caída cuenta entera (módulo 3D) con tope BellyMaxEntrySpeedDownhill. Las cuentas,
	// en TNDiveLogic (TN_DiveDecisions.h). TN.Dive.SlopeFall 0 lo apaga.

	/** Inclinación del suelo (grados) desde la que, cuesta abajo, sigue cayendo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Slope", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float BellySlopeMinAngle = 12.f;

	/** Rozamiento de la superficie cuesta abajo, multiplicado. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Slope", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BellySlopeFrictionScale = 0.3f;

	/** Freno por velocidad (BellyDrag) cuesta abajo, multiplicado. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Slope", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BellySlopeDragScale = 0.4f;

	/** Tope de todo el arrastre (s) contando el tiempo cuesta abajo: ninguna ladera la arrastra para siempre. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Slope", meta = (ClampMin = "0.5"))
	float BellySlopeMaxSeconds = 6.f;

	/** Tope de la velocidad al empezar a arrastrarse en una bajada (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Slope", meta = (ClampMin = "0.0"))
	float BellyMaxEntrySpeedDownhill = 1000.f;

	/** Tiempo mínimo arrastrándose antes de levantarse sola (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Time", meta = (ClampMin = "0.0"))
	float BellyMinSeconds = 0.3f;

	/** Por debajo de esta velocidad (cm/s), pasado el tiempo mínimo, se levanta. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Time", meta = (ClampMin = "0.0"))
	float BellyStopSpeed = 60.f;

	/** Tope de tiempo arrastrándose (s): se levanta aunque siga moviéndose. TN.Dive.MaxTime lo cambia en caliente. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Time", meta = (ClampMin = "0.2"))
	float BellyMaxSeconds = 2.6f;

	/**
	 * Por debajo de esta velocidad (cm/s) se puede salir del arrastre saltando (brinco) o moviéndose. Quien lleva el
	 * avance pulsado todo el panzazo se levanta aquí: se pierde solo el final lento (unos 0,3 s y 30 cm).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Exit", meta = (ClampMin = "0.0"))
	float BellyExitSpeed = 180.f;

	/** Tiempo mínimo en el suelo antes de poder salir a propósito (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Exit", meta = (ClampMin = "0.0"))
	float BellyMinExitSeconds = 0.15f;

	/** Reptar sin sitio para ponerse de pie (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Exit", meta = (ClampMin = "0.0"))
	float BellyCrawlSpeed = 150.f;

	/** Lo que tarda en levantarse (s): mientras, la velocidad máxima sube desde BellyGetUpSpeedFraction hasta la normal. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Exit", meta = (ClampMin = "0.0"))
	float BellyGetUpSeconds = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Exit", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BellyGetUpSpeedFraction = 0.35f;

	/** Rebote: fracción de la velocidad contra la pared que devuelve. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Bounce", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BellyBounceRestitution = 0.35f;

	/** Rebote: fracción de la velocidad a lo largo de la pared que conserva. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Bounce", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BellyBounceTangentKeep = 0.75f;

	/** Velocidad contra la pared (cm/s) por debajo de la cual no rebota: se queda pegada, como andando. También en el vuelo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Bounce", meta = (ClampMin = "0.0"))
	float BellyBounceMinSpeed = 120.f;

	// ── Rebote en el vuelo del panzazo (E9-02, #63) ──────────────────────────
	// Volando de tripa (antes de tocar el suelo) contra una pared (normal con Z por debajo de DiveWallMaxNormalZ; lo demás
	// es suelo o pendiente) a BellyBounceMinSpeed o más (velocidad relativa a lo que toca), la velocidad horizontal contra la
	// pared vuelve con DiveWallRestitution y la de a lo largo se queda con DiveWallTangentKeep; la vertical sigue. Lo
	// detectan el choque de la cápsula (HandleImpact) y el del cuerpo tumbado (KeepBellyBodyOutOfWalls), y se aplica al
	// final del movimiento: igual en el servidor y en el dueño, también al repetir. No cuentan otras tortugas ni cuerpos con
	// física. El rebote arrastrándose en el suelo no cambia. TN.Dive.WallBounce 0 lo apaga.

	/** Pared en vuelo: normal con Z por debajo de esto. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dive|Wall", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float DiveWallMaxNormalZ = 0.35f;

	/** Rebote en vuelo: fracción de la velocidad contra la pared que devuelve. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dive|Wall", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DiveWallRestitution = 0.45f;

	/** Rebote en vuelo: fracción de la velocidad horizontal a lo largo de la pared que conserva. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dive|Wall", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DiveWallTangentKeep = 0.6f;

	/** Ajustes del rebote en vuelo (TNDiveLogic). */
	TNDiveLogic::FDiveWallParams GetDiveWallParams() const;

	/** Ajustes del rebote arrastrándose en el suelo (los de siempre, BellyBounce*), con la misma cuenta. */
	TNDiveLogic::FDiveWallParams GetBellyBounceParams() const;

	/** El cuerpo gira hacia donde se desliza (grados/s) si va a más de BellyTurnMinSpeed y la diferencia es menor que BellyTurnMaxAngle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Turn", meta = (ClampMin = "0.0"))
	float BellyTurnRate = 220.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Turn", meta = (ClampMin = "0.0"))
	float BellyTurnMinSpeed = 120.f;

	/** Tras un rebote hacia atrás no se da la vuelta: se aleja de la pared mirándola. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Turn", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float BellyTurnMaxAngle = 100.f;

	// ── Cuerpo tumbado ───────────────────────────────────────────────────────
	// La cápsula del movimiento es vertical (radio 34) y solo cubre el centro del cuerpo tumbado, que mide ~1,4 m de
	// largo: la cabeza y las aletas sobresalen por delante y las patas por detrás. Tumbada (panzazo en el aire, arrastre y
	// reptar), tras cada movimiento una esfera a la altura del caparazón barre desde el centro hacia cada punta; si una
	// punta se metería en una pared, la tortuga se aparta lo justo (y rebota si iba contra ella). Las medidas suponen el
	// cuerpo centrado sobre la cápsula (ATortugaCharacter::DiveBodyCenterShift). TN.Dive.Body 0 lo apaga.

	/** Del centro de la cápsula a la punta de delante del cuerpo tumbado (cabeza y aletas), en cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Body", meta = (ClampMin = "0.0"))
	float BellyBodyReachFront = 66.f;

	/** Del centro de la cápsula a la punta de detrás (las patas), en cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Body", meta = (ClampMin = "0.0"))
	float BellyBodyReachBack = 62.f;

	/** Medio grosor del cuerpo tumbado: radio de la esfera que barre (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Body", meta = (ClampMin = "5.0"))
	float BellyBodyRadius = 14.f;

	/**
	 * Altura del centro de la esfera sobre la base de la cápsula (cm). Lo que quede por debajo de esa altura menos el radio
	 * (bordillos y baches bajos) no frena al cuerpo: lo pisa la cápsula como siempre.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Belly Slide|Body", meta = (ClampMin = "0.0"))
	float BellyBodyProbeHeight = 32.f;

protected:
	virtual void ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations) override;
	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.f, const FVector& MoveDelta = FVector::ZeroVector) override;
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;

	/**
	 * Servidor: tras decidir si corrige al cliente, si la corrección va relativa a una base que el cliente no puede
	 * encontrar por red (IsNetResolvableBase), la pasa a coordenadas del mundo y sin base. Si no, el cliente la ignoraba
	 * entera y se quedaba donde creía estar mientras el servidor la tenía en otro sitio (bajo el mapa, metida en algo...).
	 */
	virtual void ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& RelativeClientLocation,
		UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode) override;

	/** Servidor: el movimiento del cliente que estrena un lanzamiento concedido lo aplica; el cliente, al repetirlo, también. */
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;

	/** Cliente dueño: el lanzamiento concedido que ha llegado entra en este movimiento. */
	virtual void ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration) override;

	/** Cliente dueño: apunta en qué movimiento ha entrado el lanzamiento concedido. */
	virtual bool HandlePendingLaunch() override;

	/**
	 * Servidor: mientras la mueve su caja del caparazón (UTN_ShellComponent::HasLocalBody), los pasos que el dueño aún manda
	 * andando hasta que le llega la bola no se corrigen: la bola replicada ya lo coloca.
	 */
	virtual bool ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& ClientWorldLocation,
		const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode) override;

private:
	ATortugaCharacter* GetTurtle() const;

	/** Lanzamiento concedido por el servidor para el dueño (LaunchFromServer). */
	UFUNCTION(Client, Reliable)
	void ClientReceiveServerLaunch(uint8 Id, FVector_NetQuantize LaunchVelocity);

	/**
	 * El lanzamiento concedido que entra en el movimiento ClientTimeStamp: en el servidor, el que estrena ese movimiento del
	 * cliente (o, pasado el plazo, el que no llegó); en el dueño que repite sus movimientos, el que estrenó ese.
	 */
	bool FindServerLaunchForMove(float ClientTimeStamp, FVector& OutVelocity);

	FTNServerLaunch ServerLaunch;

	/** Esta máquina simula el movimiento de la tortuga (el dueño, el servidor o el propio anfitrión; no los demás). */
	bool SimulatesBelly() const;

	/** Empieza el arrastre del panzazo Serial con la inercia a lo largo del suelo tocado. */
	void StartBellySlide(const FHitResult& FloorHit, uint8 Serial, bool bFromAir);

	/**
	 * Deja la velocidad horizontal a lo largo del suelo tocado, con Keep de lo que tenía y como mucho Cap (DownhillCap en una
	 * bajada, donde cuenta el módulo 3D: TNDiveLogic::LandingSlideVelocity).
	 */
	void RedirectAlongFloor(const FHitResult& FloorHit, float Keep, float Cap, float DownhillCap);

	/** Inclinación desde la que, cuesta abajo, sigue cayendo (90 con TN.Dive.SlopeFall 0: nunca). */
	float SlopeMinAngleNow() const;

	/** Lo que necesitan las cuentas del arrastre en este paso: suelo, gravedad, rozamiento de la superficie y pendiente. */
	TNDiveLogic::FBellyStepInput MakeBellyStepInput() const;

	/** Normal del suelo del movimiento (arriba si no es caminable). */
	FVector BellyFloorNormal() const;

	/** Antes de cada movimiento: entra, sigue, se levanta o repta. */
	void TickBellyPhase(float DeltaSeconds);

	/** Cápsula de pie sin moverse de base; false (sin tocar nada) si no cabe. */
	bool TryStandUp();

	void EnterGetUp();

	/** Arrastrándose, casi parada y pasado el mínimo: se puede salir saltando o moviéndose. */
	bool CanLeaveSlide() const;

	/** Superficie bajo la tripa (el suelo del movimiento) y rozamiento que toca ahora. */
	void UpdateSlideSurface();
	float SlideFrictionNow() const;

	/** Velocidad del arrastre en este paso: pendiente, rozamiento, freno por velocidad y tope. */
	void CalcBellySlideVelocity(float DeltaTime);

	/**
	 * Tumbada, tras cada movimiento: si la cabeza o las patas (fuera de la cápsula) se meterían en una pared, aparta a la
	 * tortuga lo justo, le quita la velocidad contra la pared y, arrastrándose, apunta el rebote.
	 */
	void KeepBellyBodyOutOfWalls();

	/** TN.Dive.Debug: línea en pantalla y flechas. */
	void ShowBellyDebug() const;

	/**
	 * Trampolines de la playa (#21), al empezar cada paso: si la cápsula toca el sensor de uno (ATN_BeachTrampoline) y no
	 * sube (TNTrampolineRules::CanBounce), el rebote entra en este mismo paso (HandlePendingLaunch va justo después). Lo
	 * hacen igual el servidor y el cliente dueño, también al repetir pasos tras una corrección: solo depende del estado del
	 * paso. El boing y la deformación, solo en un paso nuevo.
	 */
	void TickTrampolineBounce();

	ETNBellyPhase BellyPhase = ETNBellyPhase::None;
	float BellyTime = 0.f;
	/** Tiempo de este arrastre cuesta abajo (no cuenta en BellyTime). */
	float BellySlopeTime = 0.f;
	uint8 SlideSerial = 0;

	float SlideSurface[TNTurtleSurface::Num] = { 0.f, 0.f, 1.f, 0.f, 0.f };
	float LastSlideFriction = 0.f;
	FVector LastSlopeAccel = FVector::ZeroVector;
	/** El último paso del arrastre iba cuesta abajo (TN.Dive.Debug). */
	bool bLastSlideDownhill = false;

	/** Velocidad que quería llevar el arrastre en este movimiento (antes de chocar) y pared contra la que ha chocado. */
	FVector SlideIntentVelocity = FVector::ZeroVector;
	FVector PendingBounceNormal = FVector::ZeroVector;
	bool bPendingBounce = false;

	/** Último apartón del cuerpo tumbado contra una pared (TN.Dive.Debug). */
	FVector LastBodyPush = FVector::ZeroVector;

	// Rebote en el vuelo del panzazo (#63): la pared más de frente con que ha chocado en este movimiento.
	bool bPendingAirBounce = false;
	FVector AirBounceNormal = FVector::ZeroVector;
	FVector AirImpactVelocity = FVector::ZeroVector;
	FVector AirImpactOtherVelocity = FVector::ZeroVector;
	float AirImpactSpeed = 0.f;
	/** Último rebote en vuelo (TN.Dive.Debug): velocidad contra la pared y hora del mundo. */
	float LastAirBounceSpeed = 0.f;
	double LastAirBounceTime = -1.0;

	/** Volando de tripa en el panzazo (aún sin tocar el suelo) en una máquina que simula el movimiento. */
	bool IsDiveFlight() const;

	/** En el vuelo del panzazo, Hit es una pared contra la que rebotar (yendo a ImpactVelocity): se apunta la más de frente. */
	void NoteAirImpact(const FHitResult& Hit, const FVector& ImpactVelocity);

	/** Estado de antes del brinco que la levantó de la tripa en el movimiento que se está guardando (ver ConsumeMoveStartBellyState). */
	bool bHasPreJumpBelly = false;
	uint8 PreJumpPhase = 0;
	float PreJumpTime = 0.f;
	uint8 PreJumpSerial = 0;
	float PreJumpCapsuleHalfHeight = 0.f;

	TNTurtleSurface::FNameCache SurfaceNameCache;
	TWeakObjectPtr<ATN_ProcMapGenerator> Generator;
	double NextGeneratorLookup = 0.0;

	/** Lo que el cliente manda al servidor en cada movimiento (SetNetworkMoveDataContainer en el constructor). */
	FTNTurtleNetworkMoveDataContainer TurtleNetworkMoveData;
};
