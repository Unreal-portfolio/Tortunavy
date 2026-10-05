#include "Player/TN_TurtleMovementComponent.h"
#include "Core/TN_Log.h"
#include "Player/TN_DiveDecisions.h"
#include "Player/TN_MovementLimits.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TN_SwimHopRules.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_WadingComponent.h"
#include "World/Beach/TN_BeachTrampoline.h"
#include "World/Beach/TN_RaceItemComponent.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"

namespace TNBellySlide
{
	// ─────────────────────────────────────────────────────────────────────────
	// Consola (afecta a la simulación: tiene que valer lo mismo en el servidor y en los clientes; en PIE es un proceso)
	// ─────────────────────────────────────────────────────────────────────────

	static int32 GSlideEnabled = 1;
	static FAutoConsoleVariableRef CVarSlideEnabled(
		TEXT("TN.Dive.Slide"),
		GSlideEnabled,
		TEXT("1 = el panzazo acaba en un arrastre sobre la tripa; 0 = se para en seco al caer, como antes. Igual en el servidor y los clientes."),
		ECVF_Cheat);

	static float GFrictionScale = 1.f;
	static FAutoConsoleVariableRef CVarFrictionScale(
		TEXT("TN.Dive.Friction"),
		GFrictionScale,
		TEXT("Multiplica el rozamiento del arrastre del panzazo en todas las superficies (1 = normal; 0,5 = resbala el doble; 2 = se para antes)."),
		ECVF_Cheat);

	static float GSlopeScale = 1.f;
	static FAutoConsoleVariableRef CVarSlopeScale(
		TEXT("TN.Dive.Slope"),
		GSlopeScale,
		TEXT("Multiplica cuánto tiran las pendientes del arrastre del panzazo (1 = normal; 0 = como en llano)."),
		ECVF_Cheat);

	static int32 GSlopeFall = 1;
	static FAutoConsoleVariableRef CVarSlopeFall(
		TEXT("TN.Dive.SlopeFall"),
		GSlopeFall,
		TEXT("1 = cuesta abajo (desde BellySlopeMinAngle, 12°) el arrastre del panzazo sigue cayendo: menos rozamiento y freno, y su tiempo no corre (#62); 0 = como antes. Igual en el servidor y los clientes."),
		ECVF_Cheat);

	static int32 GWallBounce = 1;
	static FAutoConsoleVariableRef CVarWallBounce(
		TEXT("TN.Dive.WallBounce"),
		GWallBounce,
		TEXT("1 = en el vuelo del panzazo rebota contra las paredes (restitución 0,45, 60 % a lo largo; #63); 0 = resbala por ellas, como antes. Igual en el servidor y los clientes."),
		ECVF_Cheat);

	static int32 GSplat = 1;
	static FAutoConsoleVariableRef CVarSplat(
		TEXT("TN.Dive.Splat"),
		GSplat,
		TEXT("1 = en el vuelo del panzazo, contra una pared a DiveSplatMinSpeed (650 cm/s) o más se estampa: acaba el panzazo y sale rodando como bola, con polvo y pajaritos (#355); 0 = solo rebota. Igual en el servidor y los clientes."),
		ECVF_Cheat);

	static float GMaxSeconds = 0.f;
	static FAutoConsoleVariableRef CVarMaxSeconds(
		TEXT("TN.Dive.MaxTime"),
		GMaxSeconds,
		TEXT("Tope de segundos arrastrándose tras el panzazo (0 = el del componente, 2,6 s)."),
		ECVF_Cheat);

	static int32 GBodyProbe = 1;
	static FAutoConsoleVariableRef CVarBodyProbe(
		TEXT("TN.Dive.Body"),
		GBodyProbe,
		TEXT("1 = tumbada (panzazo, arrastre y reptar), la cabeza y las patas no se meten en las paredes: la tortuga se aparta lo justo; 0 = solo choca la cápsula, como antes. Igual en el servidor y los clientes."),
		ECVF_Cheat);

	static int32 GDebug = 0;
	static FAutoConsoleVariableRef CVarDebug(
		TEXT("TN.Dive.Debug"),
		GDebug,
		TEXT("1 = muestra, por cada tortuga que se simula en esta máquina, la fase del arrastre, el tiempo, la velocidad, la superficie y el rozamiento, con flechas de la velocidad (verde) y de la pendiente (naranja)."),
		ECVF_Cheat);

	// ─────────────────────────────────────────────────────────────────────────
	// Movimientos guardados del cliente: el estado del arrastre y el turbo viajan con cada uno para repetirlos tras una
	// corrección
	// ─────────────────────────────────────────────────────────────────────────

	// Marcas comprimidas del movimiento, una por bit: FLAG_Custom_0 = petición de sprint (#250), FLAG_Custom_1 = turbo de
	// carrera (#22), FLAG_Custom_2 = petición de panzazo (#24).
	/** Marca del movimiento que va con el turbo de carrera (al servidor solo el bit; el multiplicador lo pone él). */
	constexpr uint8 RaceBoostFlag = FSavedMove_Character::FLAG_Custom_1;
	static_assert(TNDiveLogic::DiveRequestFlag == FSavedMove_Character::FLAG_Custom_2, "El panzazo pedido va en FLAG_Custom_2");
	static_assert((RaceBoostFlag & TNDiveLogic::DiveRequestFlag) == 0 && (FSavedMove_Character::FLAG_Custom_0 & (RaceBoostFlag | TNDiveLogic::DiveRequestFlag)) == 0,
		"Sprint, turbo y panzazo van en bits distintos");

	class FTNSavedMove_Turtle : public FSavedMove_Character
	{
	public:
		using Super = FSavedMove_Character;

		virtual void Clear() override
		{
			Super::Clear();
			SavedBellyPhase = 0;
			SavedBellyTime = 0.f;
			SavedSlideSerial = 0;
			SavedCapsuleHalfHeight = 0.f;
			bSavedWantsToSprint = false;
			SavedRaceBoost = 1.f;
			SavedBellySlopeTime = 0.f;
			bSavedWantsDive = false;
			SavedDiveYaw = 0;
			SavedJumpStartVelocity = FVector::ZeroVector;
			SavedSwimHopCooldown = 0.f;
			SavedPredictedCaps = 0;
		}

		virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
		{
			Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
			const UTN_TurtleMovementComponent* TurtleMove = C ? Cast<UTN_TurtleMovementComponent>(C->GetCharacterMovement()) : nullptr;
			bSavedWantsToSprint = TurtleMove && TurtleMove->InputWantsToSprint();
			// El turbo de este movimiento (ControlledCharacterMove lo acaba de tomar de los objetos de carrera).
			SavedRaceBoost = TurtleMove ? TurtleMove->GetRaceBoostMultiplier() : 1.f;
			// Los topes predichos de este movimiento (también los ha tomado ControlledCharacterMove; #575, #574).
			SavedPredictedCaps = TurtleMove ? TurtleMove->GetMovePredictedCaps() : 0;
		}

		virtual uint8 GetCompressedFlags() const override
		{
			// La petición de sprint viaja con el movimiento: el servidor corre en el mismo movimiento que el cliente.
			uint8 Flags = Super::GetCompressedFlags() | (bSavedWantsToSprint ? FLAG_Custom_0 : 0);
			if (SavedRaceBoost > 1.f)
			{
				Flags |= RaceBoostFlag;
			}
			// El panzazo pedido viaja con el movimiento: el servidor lo empieza en el mismo que el dueño (#24).
			if (bSavedWantsDive)
			{
				Flags |= TNDiveLogic::DiveRequestFlag;
			}
			return Flags;
		}

		virtual void SetInitialPosition(ACharacter* C) override
		{
			Super::SetInitialPosition(C);
			// Si el salto de este movimiento (que el cliente lee antes de guardarlo) la ha levantado de la tripa, lo de
			// antes del salto: al repetirlo, el salto vuelve a levantarla igual.
			if (UTN_TurtleMovementComponent* TurtleMove = C ? Cast<UTN_TurtleMovementComponent>(C->GetCharacterMovement()) : nullptr)
			{
				TurtleMove->ConsumeMoveStartBellyState(SavedBellyPhase, SavedBellyTime, SavedSlideSerial, SavedCapsuleHalfHeight, SavedBellySlopeTime);
				TurtleMove->CaptureMoveStartDive(bSavedWantsDive, SavedDiveYaw, SavedJumpStartVelocity);
				// La espera del brinco desde el agua de antes del brinco de este movimiento, si lo hay (#573).
				SavedSwimHopCooldown = TurtleMove->ConsumeMoveStartSwimHopCooldown();
			}
		}

		virtual void CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation) override
		{
			Super::CombineWith(OldMove, InCharacter, PC, OldStartLocation);
			// El motor vuelve al principio del movimiento pendiente y simula los dos juntos: la espera del brinco, también
			// (si no, se descontaría dos veces el tiempo del pendiente). SetInitialPosition la guarda después.
			UTN_TurtleMovementComponent* TurtleMove = InCharacter ? Cast<UTN_TurtleMovementComponent>(InCharacter->GetCharacterMovement()) : nullptr;
			if (TurtleMove && OldMove)
			{
				TurtleMove->RestoreSwimHopCooldown(static_cast<const FTNSavedMove_Turtle*>(OldMove)->SavedSwimHopCooldown);
			}
		}

		virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
		{
			// Sobre la tripa cada movimiento cuenta (el tiempo del arrastre y los cambios de fase no se pueden juntar).
			const FTNSavedMove_Turtle* Other = static_cast<const FTNSavedMove_Turtle*>(NewMove.Get());
			if (SavedBellyPhase != 0 || (Other && (Other->SavedBellyPhase != 0 || Other->SavedSlideSerial != SavedSlideSerial)))
			{
				return false;
			}
			// Con otra petición de sprint u otro turbo no (la marca ya la compara el motor; el multiplicador también cuenta al
			// repetirlos).
			if (Other && (Other->bSavedWantsToSprint != bSavedWantsToSprint || !FMath::IsNearlyEqual(Other->SavedRaceBoost, SavedRaceBoost)))
			{
				return false;
			}
			// Con otros topes predichos tampoco: el tope empieza y acaba en un movimiento concreto.
			if (Other && Other->SavedPredictedCaps != SavedPredictedCaps)
			{
				return false;
			}
			return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
		}

		virtual void PrepMoveFor(ACharacter* C) override
		{
			Super::PrepMoveFor(C);
			if (UTN_TurtleMovementComponent* TurtleMove = C ? Cast<UTN_TurtleMovementComponent>(C->GetCharacterMovement()) : nullptr)
			{
				TurtleMove->RestoreBellyState(SavedBellyPhase, SavedBellyTime, SavedSlideSerial, SavedCapsuleHalfHeight, SavedBellySlopeTime);
				TurtleMove->RestoreRaceBoost(SavedRaceBoost);
				TurtleMove->RestoreMoveStartDive(SavedDiveYaw, SavedJumpStartVelocity);
				TurtleMove->RestoreSwimHopCooldown(SavedSwimHopCooldown);
				TurtleMove->RestoreMovePredictedCaps(SavedPredictedCaps);
			}
		}

		virtual bool IsImportantMove(const FSavedMovePtr& LastAckedMove) const override
		{
			// Un cambio de fase (caer de tripa, levantarse) se reenvía si se pierde.
			const FTNSavedMove_Turtle* Acked = static_cast<const FTNSavedMove_Turtle*>(LastAckedMove.Get());
			if (Acked && Acked->SavedBellyPhase != SavedBellyPhase)
			{
				return true;
			}
			// Poner o quitar un tope predicho también: si se pierde, el servidor seguiría con el de antes.
			if (Acked && Acked->SavedPredictedCaps != SavedPredictedCaps)
			{
				return true;
			}
			return Super::IsImportantMove(LastAckedMove);
		}

		uint8 SavedBellyPhase = 0;
		float SavedBellyTime = 0.f;
		uint8 SavedSlideSerial = 0;
		/** Semialtura de la cápsula sin escalar al empezar el movimiento. */
		float SavedCapsuleHalfHeight = 0.f;
		bool bSavedWantsToSprint = false;
		/** Multiplicador del turbo de carrera con que se hizo (1 = sin turbo). */
		float SavedRaceBoost = 1.f;
		/** Tiempo del arrastre cuesta abajo al empezar el movimiento (#62). */
		float SavedBellySlopeTime = 0.f;
		/** Este movimiento pide el panzazo, hacia este giro (#24). */
		bool bSavedWantsDive = false;
		uint16 SavedDiveYaw = 0;
		/** Velocidad horizontal del último salto al empezar el movimiento (la inercia del panzazo). */
		FVector SavedJumpStartVelocity = FVector::ZeroVector;
		/** Espera del brinco desde el agua al empezar el movimiento (s de simulación, #573). */
		float SavedSwimHopCooldown = 0.f;
		/** Topes de velocidad predichos con que se hizo (bits de TNMovementLimits; #575, #574). */
		uint8 SavedPredictedCaps = 0;
	};

	class FTNNetworkPredictionData_Client_Turtle : public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FTNNetworkPredictionData_Client_Turtle(const UCharacterMovementComponent& ClientMovement)
			: FNetworkPredictionData_Client_Character(ClientMovement)
		{
		}

		virtual FSavedMovePtr AllocateNewMove() override
		{
			return FSavedMovePtr(new FTNSavedMove_Turtle());
		}
	};

	inline float BellySmoothStep(float X)
	{
		const float C = FMath::Clamp(X, 0.f, 1.f);
		return C * C * (3.f - 2.f * C);
	}

	const TCHAR* PhaseName(ETNBellyPhase Phase)
	{
		switch (Phase)
		{
		case ETNBellyPhase::Slide: return TEXT("arrastre");
		case ETNBellyPhase::Rest: return TEXT("repta (sin sitio para levantarse)");
		case ETNBellyPhase::GetUp: return TEXT("levantándose");
		default: return TEXT("normal");
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// UTN_TurtleMovementComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_TurtleMovementComponent::UTN_TurtleMovementComponent()
{
	// Movimientos del cliente sin bases que el servidor no encuentra por red (FTNTurtleNetworkMoveDataContainer).
	SetNetworkMoveDataContainer(TurtleNetworkMoveData);
	// Correcciones con el estado del panzazo del servidor (FTNTurtleMoveResponseDataContainer, #24).
	SetMoveResponseDataContainer(TurtleMoveResponseData);
	// Para el RPC de los lanzamientos que concede el servidor (LaunchFromServer).
	SetIsReplicatedByDefault(true);
}

ATortugaCharacter* UTN_TurtleMovementComponent::GetTurtle() const
{
	return Cast<ATortugaCharacter>(CharacterOwner);
}

bool UTN_TurtleMovementComponent::SimulatesBelly() const
{
	// Los proxies simulados solo reciben el movimiento replicado: ni fase ni rozamiento propios.
	return CharacterOwner != nullptr && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy;
}

bool UTN_TurtleMovementComponent::HasStoodUpFromDive(uint8 DiveSerial) const
{
	return DiveSerial != 0 && SlideSerial == DiveSerial && !IsOnBelly();
}

bool UTN_TurtleMovementComponent::AcceptsInputDuringDive(uint8 DiveSerial) const
{
	switch (BellyPhase)
	{
	case ETNBellyPhase::Slide:
		return CanLeaveSlide();
	case ETNBellyPhase::Rest:
	case ETNBellyPhase::GetUp:
		return true;
	default:
		// En el aire, sin control (como siempre); ya levantada del todo mientras llega el fin del panzazo, sí.
		return HasStoodUpFromDive(DiveSerial);
	}
}

bool UTN_TurtleMovementComponent::CanLeaveSlide() const
{
	return BellyPhase == ETNBellyPhase::Slide && IsMovingOnGround() && BellyTime + BellySlopeTime >= BellyMinExitSeconds
		&& Velocity.Size2D() <= BellyExitSpeed;
}

void UTN_TurtleMovementComponent::RestoreBellyState(uint8 InPhase, float InTime, uint8 InSerial, float InCapsuleHalfHeight, float InSlopeTime)
{
	BellyPhase = static_cast<ETNBellyPhase>(FMath::Min<uint8>(InPhase, static_cast<uint8>(ETNBellyPhase::GetUp)));
	BellyTime = InTime;
	BellySlopeTime = InSlopeTime;
	SlideSerial = InSerial;
	bPendingBounce = false;

	// Sobre la tripa la cápsula iba encogida. Si ahora ya está de pie (se levantó después), vuelve a encogerse para
	// repetir el arrastre desde donde dice el servidor; al repetir el movimiento en que se levantó, se vuelve a estirar.
	// En el resto de fases no se toca: el encogido del panzazo lo manda el servidor (ApplyDiveVisual por bIsDiving).
	UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
	if (Capsule && IsOnBelly() && InCapsuleHalfHeight > 0.f && Capsule->GetUnscaledCapsuleHalfHeight() > InCapsuleHalfHeight + 0.5f)
	{
		Capsule->SetCapsuleSize(Capsule->GetUnscaledCapsuleRadius(), InCapsuleHalfHeight, false);
	}
}

void UTN_TurtleMovementComponent::ConsumeMoveStartBellyState(uint8& OutPhase, float& OutTime, uint8& OutSerial, float& OutCapsuleHalfHeight,
	float& OutSlopeTime)
{
	// El brinco no lo cambia: el de ahora es el de antes del salto.
	OutSlopeTime = BellySlopeTime;
	if (bHasPreJumpBelly)
	{
		bHasPreJumpBelly = false;
		OutPhase = PreJumpPhase;
		OutTime = PreJumpTime;
		OutSerial = PreJumpSerial;
		OutCapsuleHalfHeight = PreJumpCapsuleHalfHeight;
		return;
	}
	const UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
	OutPhase = static_cast<uint8>(BellyPhase);
	OutTime = BellyTime;
	OutSerial = SlideSerial;
	OutCapsuleHalfHeight = Capsule ? Capsule->GetUnscaledCapsuleHalfHeight() : 0.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fases
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TurtleMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
	// Fuera de una repetición, quien la controla corre con lo que pide ahora (al repetir se quedó lo del último movimiento).
	if (CharacterOwner && CharacterOwner->IsLocallyControlled() && !CharacterOwner->bClientUpdating)
	{
		bWantsToSprint = bInputWantsToSprint;
	}
	UpdateMoveWadingMultiplier();
	bPendingBounce = false;
	bPendingAirBounce = false;
	// El movimiento ya se ha guardado (el cliente guarda antes de simular): lo de antes del brinco ya no sirve.
	bHasPreJumpBelly = false;
	bHasPreJumpSwimHop = false;
	// La espera del brinco desde el agua corre con el tiempo de este movimiento, después de leer su salto (#573).
	SwimHopCooldown = TNSwimHop::Advance(SwimHopCooldown, DeltaSeconds);
	// El panzazo pedido (#24): quien la controla, el que acaba de pedir (ya está en el movimiento guardado); el servidor y
	// la repetición en el dueño, el de las marcas del movimiento (UpdateFromCompressedFlags).
	if (CharacterOwner && CharacterOwner->IsLocallyControlled() && !CharacterOwner->bClientUpdating)
	{
		bMoveWantsDive = bDiveRequested;
		MoveDiveYaw = DiveRequestYaw;
		bDiveRequested = false;
	}
	const bool bMoveDive = bMoveWantsDive;
	bMoveWantsDive = false;
	if (SimulatesBelly())
	{
		// Lo primero: el panzazo pedido, si empieza, lanza en este paso (otro lanzamiento ya pendiente manda).
		if (bMoveDive)
		{
			TickDiveStart();
		}
		// Antes que el panzazo: si rebota, el arrastre no empieza en este paso (lo mira PendingLaunchVelocity).
		TickTrampolineBounce();
		TickBellyPhase(DeltaSeconds);
		// Fuera del panzazo con la cápsula aún encogida (se acabó sin levantarse, o el fin llegó con el movimiento parado):
		// de pie con los pies en su sitio, dentro del movimiento (el servidor y el dueño igual). Si ya está de pie, nada.
		const ATortugaCharacter* Turtle = GetTurtle();
		if (Turtle && !Turtle->IsDiving() && BellyPhase == ETNBellyPhase::None && !CharacterOwner->bIsCrouched)
		{
			RestoreStandingCapsule();
		}
	}
}

void UTN_TurtleMovementComponent::TickTrampolineBounce()
{
	ATortugaCharacter* Turtle = GetTurtle();
	// Metida en el caparazón es una caja con física (la lanza el trampolín desde el servidor); otro lanzamiento pendiente
	// manda en este paso.
	if (!Turtle || !UpdatedPrimitive || MovementMode == MOVE_None || !PendingLaunchVelocity.IsZero() || Turtle->IsDead()
		|| Turtle->IsInShell() || !TNTrampolineRules::CanBounce(Velocity))
	{
		return;
	}
	// Lo que toca la cápsula al acabar el paso anterior (también tras la corrección que se está repitiendo).
	for (const FOverlapInfo& Overlap : UpdatedPrimitive->GetOverlapInfos())
	{
		const UPrimitiveComponent* Touched = Overlap.OverlapInfo.GetComponent();
		ATN_BeachTrampoline* Trampoline = Touched ? Cast<ATN_BeachTrampoline>(Touched->GetOwner()) : nullptr;
		FVector BounceVelocity = FVector::ZeroVector;
		float Strength = 0.f;
		if (!Trampoline || !Trampoline->IsBounceSensor(Touched) || !Trampoline->ComputeTurtleBounce(Velocity, BounceVelocity, Strength))
		{
			continue;
		}
		Launch(BounceVelocity);
		// El vuelo pasa de 5 m: que no se meta sola en el caparazón al caer.
		Turtle->SetFallImmuneUntilLanded();
		if (!CharacterOwner->bClientUpdating)
		{
			Trampoline->NotifyTurtleBounced(Turtle, Strength);
			UE_LOG(LogTortunabo, Verbose, TEXT("[Trampolín] %s rebota en %s a (%.0f, %.0f, %.0f) cm/s."), *GetNameSafe(Turtle), *GetNameSafe(Trampoline),
				BounceVelocity.X, BounceVelocity.Y, BounceVelocity.Z);
		}
		return;
	}
}

void UTN_TurtleMovementComponent::TickBellyPhase(float DeltaSeconds)
{
	ATortugaCharacter* Turtle = GetTurtle();
	const bool bDiving = Turtle && Turtle->IsDiving();
	const uint8 Serial = Turtle ? Turtle->GetDiveSerial() : 0;
	const bool bReplaying = CharacterOwner && CharacterOwner->bClientUpdating;

	switch (BellyPhase)
	{
	case ETNBellyPhase::None:
		// Normalmente entra al aterrizar (ProcessLanded). Por si el panzazo empezó ya en el suelo o su aviso llegó tarde.
		if (bDiving && Serial != 0 && Serial != SlideSerial && TNBellySlide::GSlideEnabled != 0 && IsMovingOnGround()
			&& PendingLaunchVelocity.IsZero())
		{
			StartBellySlide(CurrentFloor.HitResult, Serial, false);
		}
		break;

	case ETNBellyPhase::Slide:
	case ETNBellyPhase::Rest:
	{
		if (!bDiving || Serial != SlideSerial)
		{
			// El servidor ha cortado el panzazo por otra cosa (derribo, muerte, tope de tiempo): la cápsula vuelve a estar de
			// pie con los pies donde están (antes crecía en su sitio: la mitad de abajo quedaba dentro del terreno y, al
			// desincrustarse, podía caer por debajo del mapa).
			BellyPhase = ETNBellyPhase::None;
			BellyTime = 0.f;
			BellySlopeTime = 0.f;
			RestoreStandingCapsule();
			break;
		}
		// Cuesta abajo (#62) el tiempo del arrastre no corre: ni la rampa de rozamiento ni BellyMaxSeconds la paran en una
		// ladera; el tope entonces es BellySlopeMaxSeconds, con todo el tiempo sobre la tripa.
		const bool bDownhill = BellyPhase == ETNBellyPhase::Slide && IsMovingOnGround()
			&& !TNDiveLogic::ShouldAdvanceBellyTimer(BellyFloorNormal(), Velocity, SlopeMinAngleNow());
		if (bDownhill)
		{
			BellySlopeTime += DeltaSeconds;
		}
		else
		{
			BellyTime += DeltaSeconds;
		}
		if (IsSwimming())
		{
			// Al agua: nada (la cápsula la devuelve el fin del panzazo, que llega en seguida).
			EnterGetUp();
			break;
		}
		if (!IsMovingOnGround())
		{
			// Cayendo por un borde: sigue sobre la tripa y al volver al suelo continúa el arrastre.
			break;
		}

		bool bWantStand = BellyPhase == ETNBellyPhase::Rest;
		if (BellyPhase == ETNBellyPhase::Slide)
		{
			const float Speed = static_cast<float>(Velocity.Size2D());
			const float MaxSeconds = TNBellySlide::GMaxSeconds > 0.f ? TNBellySlide::GMaxSeconds : BellyMaxSeconds;
			const float OnBellySeconds = BellyTime + BellySlopeTime;
			// Casi parada se levanta; cuesta abajo, solo si la cuesta no la va a llevar más deprisa (si no, sigue cayendo).
			const bool bStopped = OnBellySeconds >= BellyMinSeconds && Speed <= BellyStopSpeed
				&& (!bDownhill || TNDiveLogic::DownhillTerminalSpeed(MakeBellyStepInput()) <= BellyStopSpeed);
			const bool bTooLong = BellyTime >= MaxSeconds || OnBellySeconds >= BellySlopeMaxSeconds;
			// Moverse casi parada la levanta (el movimiento del jugador solo llega aquí entonces: ATortugaCharacter::Move).
			const bool bWantsOut = !Acceleration.IsNearlyZero() && CanLeaveSlide();
			bWantStand = bStopped || bTooLong || bWantsOut;
		}
		if (bWantStand)
		{
			if (TryStandUp())
			{
				EnterGetUp();
				if (!bReplaying)
				{
					UE_LOG(LogTortunabo, Verbose, TEXT("[Panzazo] %s se levanta (%.0f cm/s)."), *GetNameSafe(CharacterOwner), Velocity.Size2D());
				}
			}
			else if (BellyPhase == ETNBellyPhase::Slide)
			{
				// Algo bajo encima: sigue sobre la tripa y repta hasta que quepa de pie.
				BellyPhase = ETNBellyPhase::Rest;
				if (!bReplaying)
				{
					UE_LOG(LogTortunabo, Verbose, TEXT("[Panzazo] %s no cabe de pie: repta."), *GetNameSafe(CharacterOwner));
				}
			}
		}
		break;
	}

	case ETNBellyPhase::GetUp:
		BellyTime += DeltaSeconds;
		if (BellyTime >= BellyGetUpSeconds)
		{
			BellyPhase = ETNBellyPhase::None;
			BellyTime = 0.f;
			BellySlopeTime = 0.f;
		}
		break;
	}
}

void UTN_TurtleMovementComponent::StartBellySlide(const FHitResult& FloorHit, uint8 Serial, bool bFromAir)
{
	BellyPhase = ETNBellyPhase::Slide;
	BellyTime = 0.f;
	BellySlopeTime = 0.f;
	SlideSerial = Serial;
	bPendingBounce = false;
	RedirectAlongFloor(FloorHit, bFromAir ? BellyLandingKeep : 1.f, BellyMaxEntrySpeed, BellyMaxEntrySpeedDownhill);
	UpdateSlideSurface();
	if (CharacterOwner && !CharacterOwner->bClientUpdating)
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[Panzazo] %s cae de tripa y se arrastra a %.0f cm/s."), *GetNameSafe(CharacterOwner), Velocity.Size2D());
	}
}

void UTN_TurtleMovementComponent::RedirectAlongFloor(const FHitResult& FloorHit, float Keep, float Cap, float DownhillCap)
{
	// La inercia a lo largo del suelo: se quita lo que iba contra él (el golpe). En llano es la velocidad horizontal; en
	// una bajada, la caída se convierte en arrastre (entera desde BellySlopeMinAngle); en una subida, se pierde.
	const FVector FloorNormal = (FloorHit.bBlockingHit && !FloorHit.ImpactNormal.IsNearlyZero()) ? FloorHit.ImpactNormal : FVector::ZeroVector;
	const FVector Slide = TNDiveLogic::LandingSlideVelocity(Velocity, FloorNormal, Keep, Cap, DownhillCap, SlopeMinAngleNow());
	Velocity.X = Slide.X;
	Velocity.Y = Slide.Y;
}

TNDiveLogic::FBellyStepInput UTN_TurtleMovementComponent::MakeBellyStepInput() const
{
	TNDiveLogic::FBellyStepInput Step;
	Step.Normal = BellyFloorNormal();
	Step.Gravity = FMath::Abs(GetGravityZ());
	Step.Friction = SlideFrictionNow();
	Step.Drag = BellyDrag;
	Step.Slope.SlopeGravity = BellySlopeGravity * FMath::Max(0.f, TNBellySlide::GSlopeScale);
	Step.Slope.MinAngleDeg = SlopeMinAngleNow();
	Step.Slope.FrictionScale = BellySlopeFrictionScale;
	Step.Slope.DragScale = BellySlopeDragScale;
	Step.Slope.MaxSpeed = BellyMaxSpeed;
	return Step;
}

float UTN_TurtleMovementComponent::SlopeMinAngleNow() const
{
	return TNBellySlide::GSlopeFall != 0 ? BellySlopeMinAngle : 90.f;
}

FVector UTN_TurtleMovementComponent::BellyFloorNormal() const
{
	if (CurrentFloor.IsWalkableFloor() && !CurrentFloor.HitResult.ImpactNormal.IsNearlyZero())
	{
		return CurrentFloor.HitResult.ImpactNormal.GetSafeNormal();
	}
	return FVector::UpVector;
}

void UTN_TurtleMovementComponent::EnterGetUp()
{
	BellyPhase = ETNBellyPhase::GetUp;
	BellyTime = 0.f;
	bPendingBounce = false;
}

bool UTN_TurtleMovementComponent::TryStandUp()
{
	const ATortugaCharacter* Turtle = GetTurtle();
	UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
	const UWorld* MoveWorld = GetWorld();
	if (!Turtle || !Capsule || !UpdatedComponent || !MoveWorld)
	{
		return true;
	}
	const float StandHalf = Turtle->GetStandingCapsuleHalfHeight();
	const float CurrentHalf = Capsule->GetUnscaledCapsuleHalfHeight();
	if (CurrentHalf >= StandHalf - 0.5f)
	{
		return true;
	}

	// Como UnCrouch con la base fija: la cápsula de pie (un pelín más alta) donde quedaría con los pies en el mismo
	// sitio. Si se mete en algo, no se levanta (así nunca sube por dentro de un techo ni sale por el otro lado).
	const float SweepInflation = UE_KINDA_SMALL_NUMBER * 10.f;
	const float ScaledAdjust = (StandHalf - CurrentHalf) * Capsule->GetShapeScale();
	FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(TNBellyStandUp), false, CharacterOwner);
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(CapsuleParams, ResponseParam);
	const FCollisionShape StandingShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, -SweepInflation - ScaledAdjust);
	const ECollisionChannel Channel = UpdatedComponent->GetCollisionObjectType();
	const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
	FVector StandLocation = PawnLocation
		+ FVector(0.0, 0.0, static_cast<double>(StandingShape.GetCapsuleHalfHeight() - Capsule->GetScaledCapsuleHalfHeight()));
	bool bEncroached = MoveWorld->OverlapBlockingTestByChannel(StandLocation, FQuat::Identity, Channel, StandingShape, CapsuleParams, ResponseParam);
	if (bEncroached && IsMovingOnGround() && CurrentFloor.bBlockingHit && CurrentFloor.FloorDist > SweepInflation)
	{
		// Algo justo encima: prueba pegada al suelo.
		StandLocation.Z -= static_cast<double>(CurrentFloor.FloorDist - SweepInflation);
		bEncroached = MoveWorld->OverlapBlockingTestByChannel(StandLocation, FQuat::Identity, Channel, StandingShape, CapsuleParams, ResponseParam);
	}
	if (bEncroached)
	{
		return false;
	}

	UpdatedComponent->MoveComponent(StandLocation - PawnLocation, UpdatedComponent->GetComponentQuat(), false, nullptr,
		EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
	Capsule->SetCapsuleSize(Capsule->GetUnscaledCapsuleRadius(), StandHalf, true);
	bForceNextFloorCheck = true;
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Física del arrastre
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TurtleMovementComponent::ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations)
{
	// Antes que el aterrizaje normal: así el resto de este mismo movimiento ya se arrastra (sin el frenazo de andar).
	if (SimulatesBelly() && TNBellySlide::GSlideEnabled != 0 && !(CanEverSwim() && IsInWater()))
	{
		const ATortugaCharacter* Turtle = GetTurtle();
		const uint8 Serial = Turtle ? Turtle->GetDiveSerial() : 0;
		if (Turtle && Turtle->IsDiving() && Serial != 0)
		{
			if (Serial != SlideSerial && (BellyPhase == ETNBellyPhase::None || BellyPhase == ETNBellyPhase::GetUp))
			{
				StartBellySlide(Hit, Serial, true);
			}
			else if (BellyPhase == ETNBellyPhase::Slide && Serial == SlideSerial)
			{
				// Se había caído por un borde arrastrándose: sigue con la inercia a lo largo del suelo nuevo.
				RedirectAlongFloor(Hit, 1.f, BellyMaxSpeed, BellyMaxSpeed);
			}
		}
	}
	Super::ProcessLanded(Hit, remainingTime, Iterations);
}

void UTN_TurtleMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (BellyPhase != ETNBellyPhase::Slide || !IsMovingOnGround() || !SimulatesBelly() || HasAnimRootMotion()
		|| CurrentRootMotion.HasOverrideVelocity())
	{
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		return;
	}
	CalcBellySlideVelocity(DeltaTime);
}

void UTN_TurtleMovementComponent::UpdateSlideSurface()
{
	// El suelo del movimiento es un barrido de la cápsula: sin índice de cara, las estructuras cuentan mitad madera y
	// mitad piedra (TNTurtleSurface::Resolve). El mapa se busca cada 2 s mientras falte.
	if (!Generator.IsValid())
	{
		const UWorld* MoveWorld = GetWorld();
		const double Now = MoveWorld ? MoveWorld->GetTimeSeconds() : 0.0;
		if (Now >= NextGeneratorLookup)
		{
			NextGeneratorLookup = Now + 2.0;
			Generator = TNTurtleSurface::FindGenerator(GetWorld());
		}
	}
	const FVector Foot = UpdatedComponent ? UpdatedComponent->GetComponentLocation()
		- FVector(0.0, 0.0, static_cast<double>(CharacterOwner ? CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f))
		: FVector::ZeroVector;
	const FHitResult* FloorHit = CurrentFloor.bBlockingHit ? &CurrentFloor.HitResult : nullptr;
	TNTurtleSurface::Resolve(FloorHit, Foot, Generator.Get(), &SurfaceNameCache, SlideSurface);
}

float UTN_TurtleMovementComponent::SlideFrictionNow() const
{
	const float PerSurface[TNTurtleSurface::Num] = { BellyFrictionSand, BellyFrictionSoil, BellyFrictionRock, BellyFrictionWood, BellyFrictionWater };
	float Mixed = 0.f;
	for (int32 s = 0; s < TNTurtleSurface::Num; ++s)
	{
		Mixed += SlideSurface[s] * PerSurface[s];
	}
	// Pasado un rato, cada vez más: ninguna bajada la arrastra para siempre.
	const float Ramp = 1.f + FMath::Max(0.f, BellyFrictionRamp) * FMath::Max(0.f, BellyTime - BellyFrictionRampStart);
	return Mixed * Ramp * FMath::Max(0.f, TNBellySlide::GFrictionScale);
}

void UTN_TurtleMovementComponent::CalcBellySlideVelocity(float DeltaTime)
{
	UpdateSlideSurface();

	// Gravedad a lo largo del suelo, rozamiento con el peso que apoya y freno por velocidad, en pasos cortos; cuesta abajo
	// desde BellySlopeMinAngle, menos rozamiento y freno (#62). Las cuentas, en TNDiveLogic::IntegrateBellyVelocity.
	const TNDiveLogic::FBellyStepInput Step = MakeBellyStepInput();
	float StepFriction = 0.f;
	const FVector V = TNDiveLogic::IntegrateBellyVelocity(Velocity, Step, DeltaTime, &StepFriction);
	Velocity.X = V.X;
	Velocity.Y = V.Y;
	SlideIntentVelocity = V;
	LastSlideFriction = StepFriction;
	LastSlopeAccel = TNDiveLogic::SlopeAcceleration(Step.Normal, Step.Gravity, Step.Slope.SlopeGravity);
	bLastSlideDownhill = TNDiveLogic::IsGoingDownhill(Step.Normal, V, Step.Slope.MinAngleDeg);
}

void UTN_TurtleMovementComponent::KeepBellyBodyOutOfWalls()
{
	LastBodyPush = FVector::ZeroVector;
	const ATortugaCharacter* Turtle = GetTurtle();
	UWorld* MoveWorld = GetWorld();
	if (TNBellySlide::GBodyProbe == 0 || !Turtle || !MoveWorld || !UpdatedComponent || !CharacterOwner || !SimulatesBelly())
	{
		return;
	}
	// Solo tumbada: el panzazo en el aire, arrastrándose y reptando (ni nadando, ni sin movimiento, ni ya de pie).
	if (!Turtle->IsBellyPoseActive() || !(IsMovingOnGround() || IsFalling()))
	{
		return;
	}
	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	if (!Capsule)
	{
		return;
	}
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	// La esfera sale de dentro de la cápsula (así nunca empieza metida en nada) a la altura del caparazón.
	const float Radius = FMath::Clamp(BellyBodyRadius, 5.f, FMath::Max(5.f, FMath::Min(CapsuleRadius, HalfHeight) - 1.f));
	const float ProbeZ = FMath::Clamp(BellyBodyProbeHeight - HalfHeight, Radius - HalfHeight, HalfHeight - Radius);
	const FVector Origin = UpdatedComponent->GetComponentLocation() + FVector(0.0, 0.0, static_cast<double>(ProbeZ));
	FVector Forward = UpdatedComponent->GetForwardVector();
	Forward.Z = 0.0;
	if (!Forward.Normalize())
	{
		return;
	}

	// Lo mismo que bloquea a la cápsula menos otras tortugas y cuerpos con física: se mueven distinto en cada máquina y
	// el cliente dueño y el servidor tienen que llegar al mismo sitio.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNBellyBodyProbe), false, CharacterOwner);
	FCollisionResponseParams Responses;
	InitCollisionParams(Params, Responses);
	Responses.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	Responses.CollisionResponse.SetResponse(ECC_PhysicsBody, ECR_Ignore);
	const ECollisionChannel Channel = UpdatedComponent->GetCollisionObjectType();
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Radius);

	FVector Push = FVector::ZeroVector;
	FVector MainNormal = FVector::ZeroVector;
	FHitResult MainHit;
	double MainDepth = 0.0;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		// Hacia la cabeza y hacia las patas.
		const FVector Dir = Side == 0 ? Forward : -Forward;
		const double Reach = static_cast<double>((Side == 0 ? BellyBodyReachFront : BellyBodyReachBack) - Radius);
		if (Reach <= 1.0)
		{
			continue;
		}
		FHitResult Hit;
		if (!MoveWorld->SweepSingleByChannel(Hit, Origin, Origin + Dir * Reach, FQuat::Identity, Channel, Sphere, Params, Responses)
			|| Hit.bStartPenetrating)
		{
			continue;
		}
		// El suelo o una cuesta por delante (se pisan) y los techos no son pared.
		const FVector N = Hit.Normal;
		if (N.Z >= static_cast<double>(GetWalkableFloorZ()) || N.Z <= -0.7)
		{
			continue;
		}
		const FVector Flat(N.X, N.Y, 0.0);
		if (Flat.SizeSquared() < 0.09)
		{
			continue;
		}
		const FVector WallNormal = Flat.GetSafeNormal();
		const double Facing = -FVector::DotProduct(Dir, WallNormal);
		if (Facing <= 0.05)
		{
			continue;
		}
		// Lo que se metería esa punta en la pared, medido hacia fuera de ella.
		const double Depth = Reach * (1.0 - static_cast<double>(Hit.Time)) * Facing;
		if (Depth <= 0.1)
		{
			continue;
		}
		Push += WallNormal * (Depth + 0.5);
		if (Depth > MainDepth)
		{
			MainDepth = Depth;
			MainNormal = WallNormal;
			MainHit = Hit;
		}
	}
	if (Push.IsNearlyZero() || MainNormal.IsNearlyZero())
	{
		return;
	}

	// Se aparta lo justo, con barrido: la cápsula tampoco atraviesa lo que tenga detrás.
	Push = Push.GetClampedToMaxSize(60.0);
	FHitResult MoveHit;
	SafeMoveUpdatedComponent(Push, UpdatedComponent->GetComponentQuat(), true, MoveHit);
	if (MoveHit.IsValidBlockingHit())
	{
		SlideAlongSurface(Push, 1.f - MoveHit.Time, MoveHit.Normal, MoveHit, false);
	}
	bForceNextFloorCheck = true;
	LastBodyPush = Push;

	// En el vuelo del panzazo la cabeza suele llegar a la pared antes que la cápsula: rebota como si hubiera chocado ella
	// (#63), con la velocidad de antes de apartarse.
	if (IsDiveFlight())
	{
		NoteAirImpact(MainHit, Velocity);
	}

	// Deja de ir contra la pared; arrastrándose, además rebota como si hubiera chocado la cápsula (en OnMovementUpdated).
	const double Into = FVector::DotProduct(Velocity, MainNormal);
	if (Into < 0.0)
	{
		Velocity -= MainNormal * Into;
	}
	if (BellyPhase == ETNBellyPhase::Slide && IsMovingOnGround()
		&& (!bPendingBounce || FVector::DotProduct(SlideIntentVelocity, MainNormal) < FVector::DotProduct(SlideIntentVelocity, PendingBounceNormal)))
	{
		PendingBounceNormal = MainNormal;
		bPendingBounce = true;
	}
}

void UTN_TurtleMovementComponent::HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta)
{
	// Arrastrándose contra una pared u obstáculo (no un escalón que sube): se apunta para rebotar al final del movimiento.
	if (BellyPhase == ETNBellyPhase::Slide && IsMovingOnGround() && SimulatesBelly() && Hit.bBlockingHit)
	{
		const FVector Flat(Hit.Normal.X, Hit.Normal.Y, 0.0);
		if (Flat.SizeSquared() > 0.25)
		{
			const FVector WallNormal = Flat.GetSafeNormal();
			// Si choca con varias cosas, manda la que más de frente se lleva.
			if (!bPendingBounce || FVector::DotProduct(SlideIntentVelocity, WallNormal) < FVector::DotProduct(SlideIntentVelocity, PendingBounceNormal))
			{
				PendingBounceNormal = WallNormal;
				bPendingBounce = true;
			}
		}
	}
	else if (Hit.bBlockingHit && IsDiveFlight())
	{
		// Volando de tripa contra una pared (#63): la velocidad de este subpaso, antes de resbalar por ella.
		NoteAirImpact(Hit, Velocity);
	}
	Super::HandleImpact(Hit, TimeSlice, MoveDelta);
}

TNDiveLogic::FDiveWallParams UTN_TurtleMovementComponent::GetDiveWallParams() const
{
	TNDiveLogic::FDiveWallParams Params;
	Params.MaxNormalZ = DiveWallMaxNormalZ;
	Params.MinSpeed = BellyBounceMinSpeed;
	Params.Restitution = DiveWallRestitution;
	Params.TangentKeep = DiveWallTangentKeep;
	Params.SplatMinSpeed = TNBellySlide::GSplat != 0 ? DiveSplatMinSpeed : 0.f;
	return Params;
}

TNDiveLogic::FDiveWallParams UTN_TurtleMovementComponent::GetBellyBounceParams() const
{
	TNDiveLogic::FDiveWallParams Params;
	Params.MinSpeed = BellyBounceMinSpeed;
	Params.Restitution = BellyBounceRestitution;
	Params.TangentKeep = BellyBounceTangentKeep;
	return Params;
}

bool UTN_TurtleMovementComponent::IsDiveFlight() const
{
	if (TNBellySlide::GWallBounce == 0 || BellyPhase != ETNBellyPhase::None || !IsFalling() || !SimulatesBelly())
	{
		return false;
	}
	// En un panzazo que aún no se ha arrastrado (tras levantarse, un brinco con el panzazo sin acabar no cuenta).
	const ATortugaCharacter* Turtle = GetTurtle();
	return Turtle && Turtle->IsDiving() && Turtle->GetDiveSerial() != 0 && Turtle->GetDiveSerial() != SlideSerial;
}

void UTN_TurtleMovementComponent::NoteAirImpact(const FHitResult& Hit, const FVector& ImpactVelocity)
{
	const UPrimitiveComponent* Other = Hit.GetComponent();
	// Otras tortugas y cuerpos con física se mueven distinto en cada máquina: no rebota en ellos.
	if (Other && (Other->IsSimulatingPhysics() || Other->GetCollisionObjectType() == ECC_Pawn
		|| Other->GetCollisionObjectType() == ECC_PhysicsBody || Cast<APawn>(Other->GetOwner())))
	{
		return;
	}
	const FVector OtherVelocity = Other ? Other->GetComponentVelocity() : FVector::ZeroVector;
	const TNDiveLogic::FDiveWallParams Params = GetDiveWallParams();
	// Rebote o estampado (#355): la velocidad sale igual; lo que cambia, lo hace el servidor fuera del movimiento.
	if (TNDiveLogic::ClassifyDiveImpact(Hit.Normal, ImpactVelocity, OtherVelocity, Params) == TNDiveLogic::EDiveImpact::None)
	{
		return;
	}
	const FVector WallN = TNDiveLogic::WallNormal(Hit.Normal, Params.MaxNormalZ);
	const float Speed = TNDiveLogic::WallImpactSpeed(ImpactVelocity, OtherVelocity, WallN);
	// Si choca con varias cosas en el mismo movimiento, manda la que más de frente se lleva.
	if (!bPendingAirBounce || Speed > AirImpactSpeed)
	{
		bPendingAirBounce = true;
		AirBounceNormal = WallN;
		AirImpactVelocity = ImpactVelocity;
		AirImpactOtherVelocity = OtherVelocity;
		AirImpactPoint = Hit.ImpactPoint.IsNearlyZero() ? FVector(Hit.Location) : FVector(Hit.ImpactPoint);
		AirImpactSpeed = Speed;
	}
}

void UTN_TurtleMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);

	// Tumbada, la cabeza y las patas (fuera de la cápsula) tampoco se meten en las paredes: puede apuntar un rebote.
	KeepBellyBodyOutOfWalls();

	// Rebote: de lo que iba contra la pared, devuelve una parte hacia fuera; lo que iba a lo largo, casi todo.
	if (bPendingBounce)
	{
		bPendingBounce = false;
		if (BellyPhase == ETNBellyPhase::Slide && IsMovingOnGround())
		{
			const TNDiveLogic::FDiveWallParams Params = GetBellyBounceParams();
			if (TNDiveLogic::WallImpactSpeed(SlideIntentVelocity, FVector::ZeroVector, PendingBounceNormal) >= Params.MinSpeed)
			{
				const FVector Bounced = TNDiveLogic::ReflectDiveVelocity(SlideIntentVelocity, FVector::ZeroVector, PendingBounceNormal, Params);
				Velocity.X = Bounced.X;
				Velocity.Y = Bounced.Y;
				if (CharacterOwner && !CharacterOwner->bClientUpdating)
				{
					UE_LOG(LogTortunabo, Verbose, TEXT("[Panzazo] %s rebota a %.0f cm/s."), *GetNameSafe(CharacterOwner), Bounced.Size2D());
				}
			}
		}
	}

	// Rebote en el vuelo del panzazo (#63): la velocidad horizontal sale reflejada de la pared más de frente; la vertical,
	// la de ahora (sigue cayendo). Si en este mismo movimiento ha caído de tripa, ya manda el arrastre.
	if (bPendingAirBounce)
	{
		bPendingAirBounce = false;
		if (IsDiveFlight())
		{
			const TNDiveLogic::FDiveWallParams Params = GetDiveWallParams();
			const FVector Bounced = TNDiveLogic::ReflectDiveVelocity(AirImpactVelocity, AirImpactOtherVelocity, AirBounceNormal, Params);
			Velocity.X = Bounced.X;
			Velocity.Y = Bounced.Y;
			const bool bSplat = TNDiveLogic::IsSplatSpeed(AirImpactSpeed, Params);
			if (CharacterOwner && !CharacterOwner->bClientUpdating)
			{
				LastAirBounceSpeed = AirImpactSpeed;
				LastAirBounceTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
				UE_LOG(LogTortunabo, Log, TEXT("[Panzazo] %s %s en vuelo: %.0f cm/s contra la pared, sale a %.0f cm/s."),
					*GetNameSafe(CharacterOwner), bSplat ? TEXT("se estampa") : TEXT("rebota"), AirImpactSpeed, Bounced.Size2D());
				// Estampado (#355): el servidor lo apunta y lo hace en el siguiente Tick del personaje (la bola es un actor
				// nuevo: nada se crea dentro del movimiento). El dueño sigue con el rebote hasta que le llega la bola.
				if (bSplat && CharacterOwner->HasAuthority())
				{
					if (ATortugaCharacter* Turtle = GetTurtle())
					{
						Turtle->NoteDiveSplat(FVector(Bounced.X, Bounced.Y, Velocity.Z), AirImpactPoint, AirBounceNormal,
							TNDiveLogic::SplatStrength(AirImpactSpeed, Params));
					}
				}
			}
		}
	}

	if (TNBellySlide::GDebug > 0 && CharacterOwner && !CharacterOwner->bClientUpdating)
	{
		ShowBellyDebug();
	}
}

FRotator UTN_TurtleMovementComponent::ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime, FRotator& DeltaRotation) const
{
	if (BellyPhase != ETNBellyPhase::Slide)
	{
		// En el vuelo del panzazo, el cuerpo gira hacia su dirección dentro del movimiento: igual en el dueño y el servidor
		// (#24). Desde que cae de tripa, como siempre.
		float DiveYaw = 0.f;
		float DiveTurnRate = 0.f;
		const ATortugaCharacter* Turtle = GetTurtle();
		if (Turtle && BellyPhase == ETNBellyPhase::None && Turtle->GetDiveSerial() != SlideSerial && Turtle->GetDiveYawTurn(DiveYaw, DiveTurnRate))
		{
			DeltaRotation = FRotator(0.f, DiveTurnRate * DeltaTime, 0.f);
			return FRotator(0.f, DiveYaw, 0.f);
		}
		return Super::ComputeOrientToMovementRotation(CurrentRotation, DeltaTime, DeltaRotation);
	}
	// Sobre la tripa el cuerpo sigue despacio hacia donde se desliza (curvas por la pendiente o a lo largo de una pared);
	// tras un rebote hacia atrás no se da la vuelta.
	const FVector Flat(Velocity.X, Velocity.Y, 0.0);
	if (Flat.SizeSquared() > FMath::Square(static_cast<double>(BellyTurnMinSpeed)))
	{
		const float SlideYaw = static_cast<float>(Flat.Rotation().Yaw);
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(CurrentRotation.Yaw), SlideYaw)) < BellyTurnMaxAngle)
		{
			DeltaRotation = FRotator(0.f, BellyTurnRate * DeltaTime, 0.f);
			return FRotator(0.f, SlideYaw, 0.f);
		}
	}
	return CurrentRotation;
}

void UTN_TurtleMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;

	// Servidor: la estamina (gasto, réplica a los demás y animación) corre cuando corre el movimiento del cliente.
	if (CharacterOwner && CharacterOwner->HasAuthority() && !CharacterOwner->IsLocallyControlled())
	{
		if (const ATortugaCharacter* Turtle = GetTurtle())
		{
			if (UTN_StaminaComponent* Stamina = Turtle->GetStaminaComponent())
			{
				Stamina->SetSprintRequested(bWantsToSprint);
			}
		}
	}

	// Turbo: solo el servidor con los movimientos de un cliente: el cliente, al repetir los suyos, ya tiene el multiplicador
	// con que los hizo (PrepMoveFor). Un movimiento sin marca va sin turbo aunque aquí ya lo tenga (el dueño aún no lo sabía).
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_Authority)
	{
		const UTN_RaceItemComponent* Items = RaceItems.Get();
		const bool bClaimsBoost = (Flags & TNBellySlide::RaceBoostFlag) != 0;
		RaceBoostMultiplier = (bClaimsBoost && Items) ? FMath::Max(1.f, Items->ResolveOwnerBoostMultiplier()) : 1.f;
	}

	// Topes predichos (#575, #574). Servidor, movimiento de un cliente: los que pide, si los acepta (gracia tras cada cambio
	// en el servidor); al repetir en el dueño, ya los puso PrepMoveFor.
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_Authority && !CharacterOwner->IsLocallyControlled())
	{
		const ATortugaCharacter* Turtle = GetTurtle();
		const UTN_StaminaComponent* Stamina = Turtle ? Turtle->GetStaminaComponent() : nullptr;
		const uint8 Claimed = TurtleNetworkMoveData.GetPredictedCaps(GetCurrentNetworkMoveData());
		MovePredictedCaps = Stamina ? Stamina->ResolveClientPredictedCaps(Claimed) : 0;
	}

	// Panzazo pedido (#24). Servidor: el giro viene en los datos del movimiento del cliente; al repetir en el dueño, ya lo
	// puso PrepMoveFor.
	bMoveWantsDive = (Flags & TNDiveLogic::DiveRequestFlag) != 0;
	if (bMoveWantsDive && CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_Authority)
	{
		MoveDiveYaw = TurtleNetworkMoveData.GetDiveYaw(GetCurrentNetworkMoveData());
	}
}

void UTN_TurtleMovementComponent::UpdateMoveWadingMultiplier()
{
	MoveWadingMultiplier = 1.f;
	const ATortugaCharacter* Turtle = GetTurtle();
	const UTN_WadingComponent* Wading = Turtle ? Turtle->FindComponentByClass<UTN_WadingComponent>() : nullptr;
	const UCapsuleComponent* Capsule = Turtle ? Turtle->GetCapsuleComponent() : nullptr;
	if (Wading && Capsule && UpdatedComponent)
	{
		const double FeetZ = UpdatedComponent->GetComponentLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
		MoveWadingMultiplier = Wading->GetSpeedMultiplierAt(FeetZ, IsMovingOnGround());
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Turbo de los objetos de carrera en la predicción (issue #22)
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TurtleMovementComponent::SetRaceItems(UTN_RaceItemComponent* InRaceItems)
{
	RaceItems = InRaceItems;
}

void UTN_TurtleMovementComponent::ControlledCharacterMove(const FVector& InputVector, float DeltaSeconds)
{
	// El dueño (o el anfitrión con la suya) decide el turbo de este movimiento con lo que sabe ahora: lo guarda el movimiento
	// (FTNSavedMove_Turtle::SetMoveFor) antes de simularlo y el servidor lo valida al recibirlo.
	const UTN_RaceItemComponent* Items = RaceItems.Get();
	RaceBoostMultiplier = Items ? FMath::Max(1.f, Items->GetSpeedMultiplier()) : 1.f;
	// Igual con los topes predichos (#575, #574): los que conoce ahora.
	const ATortugaCharacter* Turtle = GetTurtle();
	const UTN_StaminaComponent* Stamina = Turtle ? Turtle->GetStaminaComponent() : nullptr;
	MovePredictedCaps = Stamina ? Stamina->GetPredictedCapMask() : 0;
	Super::ControlledCharacterMove(InputVector, DeltaSeconds);
}

float UTN_TurtleMovementComponent::GetMaxAcceleration() const
{
	return TNMovementLimits::RaceBoostAcceleration(Super::GetMaxAcceleration(), RaceBoostMultiplier);
}

float UTN_TurtleMovementComponent::GetMaxSpeed() const
{
	float Base = Super::GetMaxSpeed();
	// Andando (y en el aire, que usa la misma): la de este movimiento, con su sprint, su vadeo (ver SetWantsToSprint) y su
	// turbo de carrera (al menos la velocidad de correr, por el multiplicador y con los topes).
	const ATortugaCharacter* Turtle = GetTurtle();
	const UTN_StaminaComponent* Stamina = Turtle ? Turtle->GetStaminaComponent() : nullptr;
	if (Stamina && !IsCrouching() && (MovementMode == MOVE_Walking || MovementMode == MOVE_NavWalking || MovementMode == MOVE_Falling))
	{
		// Los topes predichos (llevar a otra, mareo) son los del movimiento; un proxy simulado no simula: los de la máquina.
		const bool bSimulatedProxy = CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy;
		const uint8 PredictedCaps = bSimulatedProxy ? Stamina->GetPredictedCapMask() : MovePredictedCaps;
		Base = Stamina->ComputeMoveMaxWalkSpeed(Stamina->CanSprint(bWantsToSprint), MoveWadingMultiplier, RaceBoostMultiplier, PredictedCaps);
	}
	if (!IsMovingOnGround())
	{
		return Base;
	}
	switch (BellyPhase)
	{
	case ETNBellyPhase::Rest:
		// Reptando sobre la tripa.
		return FMath::Min(Base, BellyCrawlSpeed);
	case ETNBellyPhase::GetUp:
	{
		// Levantándose: empieza despacio y en BellyGetUpSeconds ya anda normal.
		const float Alpha = TNBellySlide::BellySmoothStep(BellyTime / FMath::Max(0.05f, BellyGetUpSeconds));
		return Base * FMath::Lerp(BellyGetUpSpeedFraction, 1.f, Alpha);
	}
	default:
		return Base;
	}
}

bool UTN_TurtleMovementComponent::CanAttemptJump() const
{
	if (IsSwimming())
	{
		// Nadando, el salto es el brinco desde el agua (#573): pasada la espera y si la tortuga puede (ni derribada, ni
		// muerta, ni en el caparazón; el servidor lo comprueba igual en el mismo movimiento).
		const ATortugaCharacter* Turtle = GetTurtle();
		return IsJumpAllowed() && !bWantsToCrouch && Turtle && Turtle->CanSwimHopNow() && TNSwimHop::IsReady(SwimHopCooldown);
	}
	switch (BellyPhase)
	{
	case ETNBellyPhase::Slide:
		// Arrastrándose deprisa no; casi parada, un brinco la levanta.
		return CanLeaveSlide() && Super::CanAttemptJump();
	case ETNBellyPhase::Rest:
		// Sin sitio encima para ponerse de pie, tampoco para saltar.
		return false;
	default:
		return Super::CanAttemptJump();
	}
}

bool UTN_TurtleMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	if (IsSwimming())
	{
		return DoSwimHop(bReplayingMoves);
	}
	if (IsOnBelly())
	{
		// El brinco para levantarse de la tripa: primero la cápsula de pie; si no cabe, no salta.
		const UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
		const uint8 PhaseBefore = static_cast<uint8>(BellyPhase);
		const float TimeBefore = BellyTime;
		const float CapsuleBefore = Capsule ? Capsule->GetUnscaledCapsuleHalfHeight() : 0.f;
		if (!TryStandUp())
		{
			return false;
		}
		EnterGetUp();
		// El cliente guarda este movimiento después de leer el salto: se queda con cómo estaba antes (no al repetir).
		if (!bReplayingMoves)
		{
			bHasPreJumpBelly = true;
			PreJumpPhase = PhaseBefore;
			PreJumpTime = TimeBefore;
			PreJumpSerial = SlideSerial;
			PreJumpCapsuleHalfHeight = CapsuleBefore;
		}
	}
	return Super::DoJump(bReplayingMoves, DeltaTime);
}

bool UTN_TurtleMovementComponent::DoSwimHop(bool bReplayingMoves)
{
	const ATortugaCharacter* Turtle = GetTurtle();
	if (!Turtle || !Turtle->CanJump())
	{
		return false;
	}
	// El cliente guarda este movimiento después de leer el salto: se queda con la espera de antes (no al repetir).
	if (!bReplayingMoves)
	{
		bHasPreJumpSwimHop = true;
		PreJumpSwimHopCooldown = SwimHopCooldown;
	}
	SwimHopCooldown = TNSwimHop::CooldownSeconds;
	// Como el LaunchCharacter de antes (las dos componentes sustituidas), pero dentro de este movimiento.
	Velocity = Turtle->GetSwimHopVelocity();
	SetMovementMode(MOVE_Falling);
	return true;
}

float UTN_TurtleMovementComponent::ConsumeMoveStartSwimHopCooldown()
{
	if (bHasPreJumpSwimHop)
	{
		bHasPreJumpSwimHop = false;
		return PreJumpSwimHopCooldown;
	}
	return SwimHopCooldown;
}

void UTN_TurtleMovementComponent::RestoreSwimHopCooldown(float InSeconds)
{
	SwimHopCooldown = FMath::Max(0.f, InSeconds);
	bHasPreJumpSwimHop = false;
}

FNetworkPredictionData_Client* UTN_TurtleMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		UTN_TurtleMovementComponent* MutableThis = const_cast<UTN_TurtleMovementComponent*>(this);
		MutableThis->ClientPredictionData = new TNBellySlide::FTNNetworkPredictionData_Client_Turtle(*this);
	}
	return ClientPredictionData;
}

// ─────────────────────────────────────────────────────────────────────────────
// Panzazo pedido dentro del movimiento (#24)
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TurtleMovementComponent::RequestDive(const FVector& DiveDir)
{
	bDiveRequested = true;
	DiveRequestYaw = TNDiveLogic::CompressDiveYaw(DiveDir);
}

void UTN_TurtleMovementComponent::CaptureMoveStartDive(bool& bOutWantsDive, uint16& OutYaw, FVector& OutJumpStartVelocity) const
{
	bOutWantsDive = bDiveRequested;
	OutYaw = bDiveRequested ? DiveRequestYaw : 0;
	const ATortugaCharacter* Turtle = GetTurtle();
	OutJumpStartVelocity = Turtle ? Turtle->GetJumpStartHorizontalVelocity() : FVector::ZeroVector;
}

void UTN_TurtleMovementComponent::RestoreMoveStartDive(uint16 InYaw, const FVector& InJumpStartVelocity)
{
	// La marca la lee después UpdateFromCompressedFlags; aquí, el giro y la inercia del salto de entonces.
	MoveDiveYaw = InYaw;
	if (ATortugaCharacter* Turtle = GetTurtle())
	{
		Turtle->SetJumpStartHorizontalVelocity(InJumpStartVelocity);
	}
}

uint8 UTN_TurtleMovementComponent::GetSavedMovePredictedCaps(const FSavedMove_Character& Move)
{
	return static_cast<const TNBellySlide::FTNSavedMove_Turtle&>(Move).SavedPredictedCaps;
}

uint16 UTN_TurtleMovementComponent::GetSavedMoveDiveYaw(const FSavedMove_Character& Move)
{
	// Todos los movimientos guardados de la tortuga son FTNSavedMove_Turtle (FTNNetworkPredictionData_Client_Turtle).
	return static_cast<const TNBellySlide::FTNSavedMove_Turtle&>(Move).SavedDiveYaw;
}

void UTN_TurtleMovementComponent::TickDiveStart()
{
	ATortugaCharacter* Turtle = GetTurtle();
	if (!Turtle || !CharacterOwner)
	{
		return;
	}
	// Las mismas reglas en el servidor y en el dueño, con el estado de este paso: en el aire, sin otro panzazo, sin otro
	// lanzamiento en este paso (el que concede el servidor ya está en PendingLaunchVelocity)...
	FVector LaunchVelocity = FVector::ZeroVector;
	if (Turtle->StartDiveFromMove(MoveDiveYaw, IsFalling(), !PendingLaunchVelocity.IsZero(), CharacterOwner->bClientUpdating, Velocity, LaunchVelocity))
	{
		Launch(LaunchVelocity);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Red: bases de movimiento que no se encuentran por red
// ─────────────────────────────────────────────────────────────────────────────

namespace TNTurtleNetBase
{
	/**
	 * Un movimiento del cliente que iba relativo a una base que el servidor no puede encontrar: posición y aceleración del
	 * mundo y sin base (como sobre suelo quieto). El servidor, con la base nula, usa entonces la suya para las comparaciones.
	 */
	void SendAbsolute(FCharacterNetworkMoveData* Data, const FSavedMove_Character* Move)
	{
		if (!Data || !Move || !Data->MovementBase || UTN_TurtleMovementComponent::IsNetResolvableBase(Data->MovementBase))
		{
			return;
		}
		const ACharacter* Owner = Move->CharacterOwner;
		const UCharacterMovementComponent* OwnerMove = Owner ? Owner->GetCharacterMovement() : nullptr;
		Data->Location = FRepMovement::RebaseOntoZeroOrigin(Move->SavedLocation, OwnerMove);
		Data->Acceleration = Move->Acceleration;
		Data->MovementBase = nullptr;
		Data->MovementBaseBoneName = NAME_None;
	}
}

void FTNTurtleNetworkMoveDataContainer::ClientFillNetworkMoveData(const FSavedMove_Character* ClientNewMove, const FSavedMove_Character* ClientPendingMove,
	const FSavedMove_Character* ClientOldMove)
{
	FCharacterNetworkMoveDataContainer::ClientFillNetworkMoveData(ClientNewMove, ClientPendingMove, ClientOldMove);
	TNTurtleNetBase::SendAbsolute(GetNewMoveData(), ClientNewMove);
	if (bHasPendingMove)
	{
		TNTurtleNetBase::SendAbsolute(GetPendingMoveData(), ClientPendingMove);
	}
	if (bHasOldMove)
	{
		TNTurtleNetBase::SendAbsolute(GetOldMoveData(), ClientOldMove);
	}
}

bool UTN_TurtleMovementComponent::IsNetResolvableBase(const UPrimitiveComponent* Base)
{
	if (!Base)
	{
		return false;
	}
	// Lo que el motor sabe mandar (FNetGUIDCache::SupportsObject): nombre estable (subobjeto por defecto, cargado con el
	// nivel o marcado como direccionable) o componente replicado. Además, su actor tiene que llegar a la otra máquina: si no
	// se replica ni está en el nivel (el campo de decorado local, por ejemplo), tampoco se encuentra.
	if (!Base->IsSupportedForNetworking() && !Base->IsFullNameStableForNetworking())
	{
		return false;
	}
	const AActor* BaseOwner = Base->GetOwner();
	return !BaseOwner || BaseOwner->GetIsReplicated() || BaseOwner->IsFullNameStableForNetworking();
}

void UTN_TurtleMovementComponent::ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
	const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	Super::ServerMoveHandleClientError(ClientTimeStamp, DeltaTime, Accel, RelativeClientLocation, ClientMovementBase, ClientBaseBoneName, ClientMovementMode);

	FNetworkPredictionData_Server_Character* ServerData = GetPredictionData_Server_Character();
	if (!ServerData)
	{
		return;
	}
	FClientAdjustment& Adjustment = ServerData->PendingAdjustment;
	// Corrige este movimiento: el estado del panzazo tras él va con la corrección (#24, FTNTurtleMoveResponseDataContainer).
	if (!Adjustment.bAckGoodMove && Adjustment.TimeStamp == ClientTimeStamp)
	{
		const ATortugaCharacter* Turtle = GetTurtle();
		const UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
		CorrectionDiveState.bDiving = Turtle && Turtle->IsDiving();
		CorrectionDiveState.Serial = Turtle ? Turtle->GetDiveSerial() : 0;
		CorrectionDiveState.CapsuleHalfHeight = Capsule ? Capsule->GetUnscaledCapsuleHalfHeight() : 0.f;
		CorrectionDiveState.TimeStamp = ClientTimeStamp;
	}
	UPrimitiveComponent* AdjustBase = Adjustment.NewBase;
	if (Adjustment.bAckGoodMove || Adjustment.TimeStamp != ClientTimeStamp || !AdjustBase || IsNetResolvableBase(AdjustBase))
	{
		return;
	}
	// Corrección relativa a una base que el cliente no encuentra: al mundo (la base la tiene este servidor) y sin base. El
	// cliente la aplica entera y busca su suelo (ClientAdjustPosition_Implementation).
	if (Adjustment.bBaseRelativePosition)
	{
		FVector WorldLocation = FVector::ZeroVector;
		if (MovementBaseUtility::TransformLocationToWorld(AdjustBase, Adjustment.NewBaseBoneName, Adjustment.NewLoc, WorldLocation))
		{
			Adjustment.NewLoc = FRepMovement::RebaseOntoZeroOrigin(WorldLocation, this);
		}
		else
		{
			Adjustment.NewLoc = FRepMovement::RebaseOntoZeroOrigin(UpdatedComponent->GetComponentLocation(), this);
		}
	}
	if (Adjustment.bBaseRelativeVelocity)
	{
		FVector WorldVelocity = Adjustment.NewVel;
		if (MovementBaseUtility::TransformDirectionToWorld(AdjustBase, Adjustment.NewBaseBoneName, Adjustment.NewVel, WorldVelocity))
		{
			Adjustment.NewVel = WorldVelocity;
		}
	}
	Adjustment.bBaseRelativePosition = false;
	Adjustment.bBaseRelativeVelocity = false;
	Adjustment.NewBase = nullptr;
	Adjustment.NewBaseBoneName = NAME_None;
}

void UTN_TurtleMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse)
{
	// Una corrección de un movimiento que aún se guarda (si no, el motor la ignora): el panzazo y la cápsula del servidor
	// en ese movimiento, antes de colocarla y de repetir los siguientes (que vuelven a pedir el panzazo si lo pedían).
	const FNetworkPredictionData_Client_Character* ClientData = MoveResponse.IsCorrection() ? GetPredictionData_Client_Character() : nullptr;
	if (ClientData && &MoveResponse == &TurtleMoveResponseData && ClientData->GetSavedMoveIndex(MoveResponse.ClientAdjustment.TimeStamp) != INDEX_NONE)
	{
		const FTNDiveNetState& Server = TurtleMoveResponseData.DiveState;
		if (ATortugaCharacter* Turtle = GetTurtle())
		{
			Turtle->ApplyServerDiveCorrection(Server.bDiving, Server.Serial);
		}
		UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
		if (Capsule && Server.CapsuleHalfHeight > 1.f && !FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleHalfHeight(), Server.CapsuleHalfHeight, 0.01f))
		{
			// En su sitio: la posición de la corrección es la del centro de esa cápsula en el servidor.
			Capsule->SetCapsuleHalfHeight(Server.CapsuleHalfHeight);
		}
	}
	Super::ClientHandleMoveResponse(MoveResponse);
}

void UTN_TurtleMovementComponent::ClientAdjustPosition_Implementation(float TimeStamp, FVector NewLoc, FVector NewVel, UPrimitiveComponent* NewBase,
	FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, TOptional<FRotator> OptionalRotation)
{
	// TN.Dive.Debug: cuántas correcciones y de cuánto (la posición guardada de ese movimiento frente a la del servidor).
	if (const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character())
	{
		const int32 MoveIndex = ClientData->GetSavedMoveIndex(TimeStamp);
		if (MoveIndex != INDEX_NONE && !bBaseRelativePosition && ClientData->SavedMoves.IsValidIndex(MoveIndex) && ClientData->SavedMoves[MoveIndex].IsValid())
		{
			++ClientCorrectionCount;
			LastClientCorrectionCm = static_cast<float>(FVector::Dist(ClientData->SavedMoves[MoveIndex]->SavedLocation,
				FRepMovement::RebaseOntoLocalOrigin(NewLoc, this)));
		}
	}

	Super::ClientAdjustPosition_Implementation(TimeStamp, NewLoc, NewVel, NewBase, NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode,
		OptionalRotation);

	// El servidor anda sobre una base que quitó de la corrección (no se encuentra por red): el motor la ha dejado sin base
	// ni suelo. Se busca el suelo aquí, en la posición ya corregida, antes de repetir los movimientos pendientes.
	if (bHasBase || NewBase || !HasValidData() || !IsMovingOnGround() || CharacterOwner->GetMovementBase())
	{
		return;
	}
	FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, false);
	if (CurrentFloor.IsWalkableFloor())
	{
		SetBaseFromFloor(CurrentFloor);
		SaveBaseLocation();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Cápsula de pie sin crecer en su sitio
// ─────────────────────────────────────────────────────────────────────────────

bool UTN_TurtleMovementComponent::RestoreStandingCapsule()
{
	const ATortugaCharacter* Turtle = GetTurtle();
	UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
	if (!Turtle || !Capsule || !UpdatedComponent)
	{
		return false;
	}
	const float StandHalf = Turtle->GetStandingCapsuleHalfHeight();
	const float CurrentHalf = Capsule->GetUnscaledCapsuleHalfHeight();
	if (CurrentHalf >= StandHalf - 0.5f)
	{
		return false;
	}
	// Lo normal: de pie con los pies en su sitio, sin meterse en nada.
	if (TryStandUp())
	{
		return true;
	}
	// No cabe de pie (algo encima): igual, con los pies en su sitio y sin barrer. La cápsula no cruza el suelo, así que el
	// movimiento la aparta de lo de arriba (o la deja atascada encima del suelo), pero nunca la desincrusta hacia abajo.
	const double Rise = static_cast<double>(StandHalf - CurrentHalf) * static_cast<double>(Capsule->GetShapeScale());
	UpdatedComponent->MoveComponent(FVector(0.0, 0.0, Rise), UpdatedComponent->GetComponentQuat(), false, nullptr,
		EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
	Capsule->SetCapsuleSize(Capsule->GetUnscaledCapsuleRadius(), StandHalf, true);
	bForceNextFloorCheck = true;
	UE_LOG(LogTortunabo, Verbose, TEXT("[Panzazo] %s: de pie sin sitio encima (sube %.0f cm con los pies en su sitio)."), *GetNameSafe(CharacterOwner), Rise);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Depuración
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TurtleMovementComponent::ShowBellyDebug() const
{
	if (!GEngine || !CharacterOwner || !UpdatedComponent)
	{
		return;
	}
	static const TCHAR* const SurfaceNames[TNTurtleSurface::Num] = { TEXT("arena"), TEXT("tierra"), TEXT("roca"), TEXT("madera"), TEXT("agua") };
	FString SurfaceText;
	if (BellyPhase == ETNBellyPhase::Slide)
	{
		for (int32 s = 0; s < TNTurtleSurface::Num; ++s)
		{
			if (SlideSurface[s] >= 0.05f) { SurfaceText += FString::Printf(TEXT("%s %.2f "), SurfaceNames[s], SlideSurface[s]); }
		}
	}
	const ATortugaCharacter* Turtle = GetTurtle();
	if (BellyPhase == ETNBellyPhase::None && !(Turtle && Turtle->IsDiving()))
	{
		// Sin panzazo no hay nada que contar.
		return;
	}
	const bool bLocal = CharacterOwner->IsLocallyControlled();
	FString SlopeText = BellySlopeTime > 0.f || bLastSlideDownhill
		? FString::Printf(TEXT(" (%s, %.2f s cuesta abajo)"), bLastSlideDownhill ? TEXT("bajando") : TEXT("ya no baja"), BellySlopeTime)
		: FString();
	const UWorld* DebugWorld = GetWorld();
	if (DebugWorld && LastAirBounceTime >= 0.0 && DebugWorld->GetTimeSeconds() - LastAirBounceTime < 1.5)
	{
		SlopeText += FString::Printf(TEXT(" · rebote en vuelo a %.0f cm/s"), LastAirBounceSpeed);
	}
	if (CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy)
	{
		// El dueño en un cliente: las correcciones del servidor (con el panzazo predicho no debe sumar al empezar).
		SlopeText += FString::Printf(TEXT(" · correcciones %d (última %.0f cm)"), ClientCorrectionCount, LastClientCorrectionCm);
	}
	const FString Text = FString::Printf(TEXT("[Panzazo] %s (%s) · %s %.2f s · %.0f cm/s · %s· roce %.0f cm/s² · pendiente %.0f cm/s²%s · panzazo %s nº %d (arrastre del nº %d)"),
		*GetNameSafe(CharacterOwner), bLocal ? TEXT("local") : TEXT("servidor"), TNBellySlide::PhaseName(BellyPhase), BellyTime,
		Velocity.Size2D(), *SurfaceText, LastSlideFriction, LastSlopeAccel.Size(), *SlopeText,
		(Turtle && Turtle->IsDiving()) ? TEXT("sí") : TEXT("no"), Turtle ? static_cast<int32>(Turtle->GetDiveSerial()) : 0,
		static_cast<int32>(SlideSerial));
	GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()) + 0xB311F10Bull, 0.f,
		bLocal ? FColor::Yellow : FColor::Orange, Text);

	if (BellyPhase != ETNBellyPhase::None)
	{
		const UWorld* MoveWorld = GetWorld();
		const FVector From = UpdatedComponent->GetComponentLocation();
		DrawDebugDirectionalArrow(MoveWorld, From, From + FVector(Velocity.X, Velocity.Y, 0.0) * 0.25, 20.f, FColor::Green, false, -1.f, 0, 2.f);
		if (!LastSlopeAccel.IsNearlyZero())
		{
			DrawDebugDirectionalArrow(MoveWorld, From, From + LastSlopeAccel * 0.25, 15.f, FColor::Orange, false, -1.f, 0, 1.5f);
		}
	}

	// El cuerpo tumbado que choca (celeste; rojo si se ha apartado de una pared en este movimiento, con la flecha).
	if (TNBellySlide::GBodyProbe != 0 && Turtle && Turtle->IsBellyPoseActive() && CharacterOwner->GetCapsuleComponent())
	{
		const UWorld* MoveWorld = GetWorld();
		FVector Fwd = UpdatedComponent->GetForwardVector();
		Fwd.Z = 0.0;
		Fwd = Fwd.GetSafeNormal();
		const float HalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Mid = UpdatedComponent->GetComponentLocation() + FVector(0.0, 0.0, static_cast<double>(BellyBodyProbeHeight - HalfHeight));
		const FColor BodyColor = LastBodyPush.IsNearlyZero() ? FColor::Cyan : FColor::Red;
		const FVector Head = Mid + Fwd * static_cast<double>(BellyBodyReachFront - BellyBodyRadius);
		const FVector Feet = Mid - Fwd * static_cast<double>(BellyBodyReachBack - BellyBodyRadius);
		DrawDebugLine(MoveWorld, Feet, Head, BodyColor, false, -1.f, 0, 2.f);
		DrawDebugSphere(MoveWorld, Head, BellyBodyRadius, 8, BodyColor, false, -1.f, 0, 1.f);
		DrawDebugSphere(MoveWorld, Feet, BellyBodyRadius, 8, BodyColor, false, -1.f, 0, 1.f);
		if (!LastBodyPush.IsNearlyZero())
		{
			DrawDebugDirectionalArrow(MoveWorld, Mid, Mid + LastBodyPush * 3.0, 15.f, FColor::Red, false, -1.f, 0, 2.f);
		}
	}
}
