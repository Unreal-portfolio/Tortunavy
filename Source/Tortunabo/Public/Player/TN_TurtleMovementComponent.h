#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/TN_DiveDecisions.h"
#include "Player/TN_ServerLaunch.h"
#include "Player/TN_TurtleSurface.h"
#include "TN_TurtleMovementComponent.generated.h"

class ATortugaCharacter;

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

	/** Giro del panzazo que pide Data, si es uno de estos datos (0 si no). */
	uint16 GetDiveYaw(const FCharacterNetworkMoveData* Data) const;

	/** Topes predichos que pide Data, si es uno de estos datos (0 si no). */
	uint8 GetPredictedCaps(const FCharacterNetworkMoveData* Data) const;

	virtual void ClientFillNetworkMoveData(const FSavedMove_Character* ClientNewMove, const FSavedMove_Character* ClientPendingMove,
		const FSavedMove_Character* ClientOldMove) override;

private:
	const FTNTurtleNetworkMoveData* FindTurtleData(const FCharacterNetworkMoveData* Data) const;

	FTNTurtleNetworkMoveData TurtleMoveData[3];
};

/**
 * Estado del servidor tras el movimiento que corrige: el del panzazo (#24: si estaba en un panzazo, su número y la
 * semialtura sin escalar de la cápsula) y la espera del brinco desde el agua (#573). Con él, el dueño repite sus
 * movimientos desde lo mismo que el servidor.
 */
struct FTNDiveNetState
{
	bool bDiving = false;
	uint8 Serial = 0;
	float CapsuleHalfHeight = 0.f;
	/** Espera del brinco desde el agua al acabar ese movimiento (s de simulación). */
	float SwimHopCooldown = 0.f;
	/** Movimiento del cliente tras el que se tomó (el de la corrección). */
	float TimeStamp = -1.f;
};

/**
 * Respuesta del servidor a los movimientos del cliente: la de serie y, en las correcciones, el estado del panzazo y la
 * espera del brinco del servidor en el movimiento corregido (FTNDiveNetState, 10 bytes). El panzazo empieza dentro del movimiento (predicho): si el
 * servidor no lo empezó (o sí y el dueño no), la corrección lleva también eso y el dueño lo repite desde ahí.
 */
struct FTNTurtleMoveResponseDataContainer : public FCharacterMoveResponseDataContainer
{
	virtual void ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap) override;

	FTNDiveNetState DiveState;
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
 * TN.Dive.SlopeFall, TN.Dive.WallBounce, TN.Dive.Splat, TN.Dive.MaxTime, TN.Dive.Body y TN.Dive.Debug. Ver Docs/Animacion_Tortuga.md.
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

	/**
	 * Lo del panzazo que guarda el movimiento nuevo (FTNSavedMove_Turtle::SetInitialPosition, #24): si pide el panzazo, su
	 * giro, y la velocidad horizontal del último salto (la inercia del panzazo sale de ella).
	 */
	void CaptureMoveStartDive(bool& bOutWantsDive, uint16& OutYaw, FVector& OutJumpStartVelocity) const;

	/** Repetición de movimientos (FTNSavedMove_Turtle::PrepMoveFor): el giro pedido y la velocidad del salto de entonces. */
	void RestoreMoveStartDive(uint16 InYaw, const FVector& InJumpStartVelocity);

	// ── Sprint y vadeo, predichos ────────────────────────────────────────────
	// La velocidad máxima andando se calcula en cada paso (GetMaxSpeed) con la petición de sprint de ese movimiento, que el
	// cliente dueño manda en sus movimientos (FLAG_Custom_0), y con el vadeo de la posición de ese paso. Antes la ponía
	// UTN_StaminaComponent en MaxWalkSpeed cuando a cada máquina le llegaba el cambio (el sprint por RPC, el vadeo en su Tick
	// a 10 Hz): el cliente y el servidor andaban a velocidades distintas durante un momento y el servidor lo corregía, que
	// se veía como temblores al andar (#250).

	/** El jugador pide correr (ATortugaCharacter::RefreshSprintRequest, en la máquina que la controla). */
	void SetWantsToSprint(bool bWants) { bInputWantsToSprint = bWants; bWantsToSprint = bWants; }

	/** Lo que pide ahora el jugador (lo que se guarda en el movimiento nuevo). */
	bool InputWantsToSprint() const { return bInputWantsToSprint; }

	// ── Brinco desde el agua, predicho (#573) ───────────────────────────────
	// Nadando, el salto de serie (ACharacter::Jump, marca FLAG_JumpPressed del movimiento) es un brinco: CanAttemptJump mira
	// la espera y DoJump pone la velocidad del brinco. La espera (TNSwimHop) corre con el tiempo de simulación de los
	// movimientos y va guardada en cada uno (FTNSavedMove_Turtle), así que el dueño y el servidor brincan en el mismo.

	/** Espera que queda para el siguiente brinco (s de simulación; 0 = listo). */
	float GetSwimHopCooldown() const { return SwimHopCooldown; }

	/**
	 * La espera al empezar el movimiento que se va a guardar (FTNSavedMove_Turtle::SetInitialPosition). El cliente lee el
	 * salto antes de guardar el movimiento: si en él ha brincado, devuelve (y olvida) la de antes del brinco.
	 */
	float ConsumeMoveStartSwimHopCooldown();

	/**
	 * Combinación de movimientos (FTNSavedMove_Turtle::CombineWith): la espera con que empezó el pendiente. Corrección del
	 * servidor: la suya tras el movimiento corregido. Al repetir los movimientos no se restaura la guardada: se recalcula
	 * desde la de la corrección.
	 */
	void RestoreSwimHopCooldown(float InSeconds);

	// ── Topes de velocidad predichos: llevar a otra y mareo (#575, #574) ────
	// Quien la mueve toma al empezar cada movimiento los topes predichos que conoce (UTN_StaminaComponent::
	// GetPredictedCapMask) y los guarda en él; el cliente los manda al servidor en FTNTurtleNetworkMoveData y el servidor
	// simula ese movimiento con los que acepta (UTN_StaminaComponent::ConsumeClientPredictedCaps, en MoveAutonomous).
	// GetMaxSpeed usa los del movimiento, no los de la máquina: así el tope empieza y acaba en el mismo movimiento en el
	// dueño y en el servidor.

	/** Topes predichos del movimiento que se simula (bits de TNMovementLimits). */
	uint8 GetMovePredictedCaps() const { return MovePredictedCaps; }

	/** Repetición de movimientos (FTNSavedMove_Turtle::PrepMoveFor): los topes con que se hizo. */
	void RestoreMovePredictedCaps(uint8 InMask) { MovePredictedCaps = InMask; }

	/** Topes predichos que pide el movimiento guardado Move. */
	static uint8 GetSavedMovePredictedCaps(const FSavedMove_Character& Move);

	// ── Turbo (issue #22) ─────────────────────────────────────────────────────
	// Va en la predicción como el panzazo: quien mueve la tortuga (su dueño o el anfitrión) toma el multiplicador de
	// SetBoostMultiplier al empezar cada movimiento y lo guarda en él (FTNSavedMove_Turtle: marca FLAG_Custom_1 al servidor
	// y el valor para repetirlo); el servidor simula los movimientos marcados con el que él le reconoce (con el margen de
	// TNItemRuntime::BoostGraceSeconds) y los demás sin turbo. GetMaxSpeed y GetMaxAcceleration lo aplican. Lo usarán los
	// efectos que cambien la velocidad (Docs/2026-10-06-Plan-Maestro-Modo-Unico.md).

	/** Turbo que tiene la tortuga ahora (1 = sin turbo). Lo pone cada máquina con lo que sabe del efecto, a la vez. */
	void SetBoostMultiplier(float InMultiplier);

	/** Multiplicador del turbo en el movimiento que se simula (1 = sin turbo). */
	float GetRaceBoostMultiplier() const { return RaceBoostMultiplier; }

	/** Repetición de movimientos tras una corrección (FTNSavedMove_Turtle::PrepMoveFor): el turbo con que se hizo. */
	void RestoreRaceBoost(float InMultiplier) { RaceBoostMultiplier = FMath::Max(1.f, InMultiplier); }

	// ── UCharacterMovementComponent ──────────────────────────────────────────

	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual FRotator ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime, FRotator& DeltaRotation) const override;
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxAcceleration() const override;
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

	// ── Red: el panzazo empieza dentro del movimiento (E9-04, #24) ───────────
	// Antes el dueño mandaba Server_StartDive y el servidor lanzaba a la tortuga (LaunchCharacter): el dueño lo recibía como
	// corrección una ida y vuelta después (el tirón al empezar). Ahora quien la controla pide el panzazo (RequestDive) y la
	// petición va en su siguiente movimiento guardado: marca FSavedMove_Character::FLAG_Custom_2 (TNDiveLogic::
	// DiveRequestFlag) y el giro en 16 bits en FTNTurtleNetworkMoveData. En ese movimiento, el dueño y el servidor deciden
	// con las mismas reglas (ATortugaCharacter::StartDiveFromMove: en el aire, sin otro panzazo ni otro lanzamiento...) y,
	// si empieza, los dos lanzan, encogen la cápsula y cuentan el panzazo igual; al repetir movimientos tras una
	// corrección, otra vez. Si el servidor decide otra cosa, su corrección lleva su estado del panzazo
	// (FTNTurtleMoveResponseDataContainer) y el dueño repite desde él. TN.Net.DivePredict 0 vuelve a Server_StartDive.

	/** Quien la controla: el panzazo hacia DiveDir (horizontal) en el siguiente movimiento. */
	void RequestDive(const FVector& DiveDir);

	/** Hay un panzazo pedido que aún no ha entrado en un movimiento. */
	bool HasDiveRequest() const { return bDiveRequested; }

	/** Giro (comprimido) del panzazo que pide el movimiento que se simula o se repite. */
	uint16 GetMoveDiveYaw() const { return MoveDiveYaw; }

	/** Giro del panzazo que pide el movimiento guardado Move (0 si no lo pide: mirar su marca). */
	static uint16 GetSavedMoveDiveYaw(const FSavedMove_Character& Move);

	/** Servidor: el estado del panzazo tras el último movimiento corregido (lo que manda la corrección). */
	const FTNDiveNetState& GetCorrectionDiveState() const { return CorrectionDiveState; }

	/** Cliente dueño: correcciones recibidas y tamaño de la última (cm), para TN.Dive.Debug. */
	int32 GetClientCorrectionCount() const { return ClientCorrectionCount; }
	float GetLastClientCorrectionCm() const { return LastClientCorrectionCm; }

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

	/**
	 * Estampado (#355): desde esta velocidad contra la pared (cm/s) rebota igual, pero el servidor acaba el panzazo y la
	 * lanza como bola de caparazón con la velocidad reflejada (ATortugaCharacter::ServerDiveSplat, fuera del movimiento),
	 * con polvo, pajaritos y el golpe sintetizado. 0 = nunca. TN.Dive.Splat 0 lo apaga.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dive|Wall", meta = (ClampMin = "0.0"))
	float DiveSplatMinSpeed = 650.f;

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
	/** Quien mueve la tortuga en esta máquina: antes de guardar y simular el movimiento, el turbo que lleva ahora. */
	virtual void ControlledCharacterMove(const FVector& InputVector, float DeltaSeconds) override;

	/**
	 * Servidor (y repetición en el cliente): la petición de sprint del movimiento (FLAG_Custom_0). Servidor, movimiento de un
	 * cliente: con la marca de turbo (FLAG_Custom_1), el multiplicador que le reconoce; sin ella, ninguno. Y la petición de
	 * panzazo del movimiento (TNDiveLogic::DiveRequestFlag = FLAG_Custom_2, #24).
	 */
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

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

	/**
	 * Servidor: el movimiento del cliente que estrena un lanzamiento concedido lo aplica; el cliente, al repetirlo, también.
	 * Y el servidor decide aquí con qué topes predichos simula el movimiento del cliente (con su DeltaTime validado).
	 */
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;

	/** Cliente dueño: el lanzamiento concedido que ha llegado entra en este movimiento. */
	virtual void ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration) override;

	/** Cliente dueño: apunta en qué movimiento ha entrado el lanzamiento concedido. */
	virtual bool HandlePendingLaunch() override;

	/** Cliente dueño: en una corrección, el estado del panzazo del servidor (cápsula incluida) antes de repetir movimientos. */
	virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse) override;

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

	/** Vadeo en la posición de este paso (UpdateCharacterStateBeforeMovement). */
	void UpdateMoveWadingMultiplier();

	/** Lo que pide el jugador ahora y lo que pedía en el movimiento que se simula (distintos al repetir movimientos). */
	bool bInputWantsToSprint = false;
	bool bWantsToSprint = false;

	/** Multiplicador del vadeo en este paso del movimiento (1 = fuera del agua). */
	float MoveWadingMultiplier = 1.f;

	/** Topes predichos del movimiento que se simula (ver GetMovePredictedCaps). */
	uint8 MovePredictedCaps = 0;

	/** Espera del brinco desde el agua (s de simulación) y la de antes del brinco del movimiento que se está guardando. */
	float SwimHopCooldown = 0.f;
	bool bHasPreJumpSwimHop = false;
	float PreJumpSwimHopCooldown = 0.f;

	/** DoJump nadando: el brinco desde el agua en este movimiento (velocidad, espera y a caer). */
	bool DoSwimHop(bool bReplayingMoves);

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
	/** Punto del choque (para el polvo del estampado, #355). */
	FVector AirImpactPoint = FVector::ZeroVector;
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

	/** Lo que el cliente manda al servidor en cada movimiento (SetNetworkMoveDataContainer en el constructor). */
	FTNTurtleNetworkMoveDataContainer TurtleNetworkMoveData;

	/** Turbo de esta máquina (SetBoostMultiplier) y, en el servidor, el último mayor que 1 y su hora del servidor. */
	float BoostMultiplier = 1.f;
	float RecentBoostMultiplier = 1.f;
	double RecentBoostTime = -1.0;
	/** Multiplicador del movimiento que se simula (el que guarda el movimiento o reconoce el servidor). */
	float RaceBoostMultiplier = 1.f;

	/** Servidor: el turbo que reconoce a un movimiento del dueño marcado con turbo (TNItemRuntime::ResolveClaimedBoost). */
	float ResolveOwnerBoostMultiplier() const;

	/** Lo que el servidor contesta (SetMoveResponseDataContainer en el constructor): en las correcciones, su panzazo. */
	FTNTurtleMoveResponseDataContainer TurtleMoveResponseData;

	// ── Panzazo pedido (#24) ─────────────────────────────────────────────────

	/** Al empezar el movimiento: si pide el panzazo, que lo decida el personaje y, si empieza, el lanzamiento. */
	void TickDiveStart();

	/** Pedido por el jugador y aún sin movimiento (lo guarda el siguiente). */
	bool bDiveRequested = false;
	uint16 DiveRequestYaw = 0;

	/** El movimiento que se simula pide el panzazo (del jugador, de las marcas del cliente o del movimiento repetido). */
	bool bMoveWantsDive = false;
	uint16 MoveDiveYaw = 0;

	/** Servidor: estado del panzazo tras el último movimiento corregido. */
	FTNDiveNetState CorrectionDiveState;

	/** Cliente dueño: correcciones recibidas (TN.Dive.Debug). */
	int32 ClientCorrectionCount = 0;
	float LastClientCorrectionCm = 0.f;
};
