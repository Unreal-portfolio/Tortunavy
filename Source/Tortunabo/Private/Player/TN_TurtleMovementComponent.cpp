#include "Player/TN_TurtleMovementComponent.h"
#include "Core/TN_Log.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachTrampoline.h"
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
	// Movimientos guardados del cliente: el estado del arrastre viaja con cada uno para repetirlo tras una corrección
	// ─────────────────────────────────────────────────────────────────────────

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
		}

		virtual void SetInitialPosition(ACharacter* C) override
		{
			Super::SetInitialPosition(C);
			// Si el salto de este movimiento (que el cliente lee antes de guardarlo) la ha levantado de la tripa, lo de
			// antes del salto: al repetirlo, el salto vuelve a levantarla igual.
			if (UTN_TurtleMovementComponent* TurtleMove = C ? Cast<UTN_TurtleMovementComponent>(C->GetCharacterMovement()) : nullptr)
			{
				TurtleMove->ConsumeMoveStartBellyState(SavedBellyPhase, SavedBellyTime, SavedSlideSerial, SavedCapsuleHalfHeight);
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
			return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
		}

		virtual void PrepMoveFor(ACharacter* C) override
		{
			Super::PrepMoveFor(C);
			if (UTN_TurtleMovementComponent* TurtleMove = C ? Cast<UTN_TurtleMovementComponent>(C->GetCharacterMovement()) : nullptr)
			{
				TurtleMove->RestoreBellyState(SavedBellyPhase, SavedBellyTime, SavedSlideSerial, SavedCapsuleHalfHeight);
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
			return Super::IsImportantMove(LastAckedMove);
		}

		uint8 SavedBellyPhase = 0;
		float SavedBellyTime = 0.f;
		uint8 SavedSlideSerial = 0;
		/** Semialtura de la cápsula sin escalar al empezar el movimiento. */
		float SavedCapsuleHalfHeight = 0.f;
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
	return BellyPhase == ETNBellyPhase::Slide && IsMovingOnGround() && BellyTime >= BellyMinExitSeconds
		&& Velocity.Size2D() <= BellyExitSpeed;
}

void UTN_TurtleMovementComponent::RestoreBellyState(uint8 InPhase, float InTime, uint8 InSerial, float InCapsuleHalfHeight)
{
	BellyPhase = static_cast<ETNBellyPhase>(FMath::Min<uint8>(InPhase, static_cast<uint8>(ETNBellyPhase::GetUp)));
	BellyTime = InTime;
	SlideSerial = InSerial;
	bPendingBounce = false;

	// Sobre la tripa la cápsula iba encogida. Si ahora ya está de pie (se levantó después), vuelve a encogerse para
	// repetir el arrastre desde donde dice el servidor; al repetir el movimiento en que se levantó, se vuelve a estirar.
	// En el resto de fases no se toca: el encogido del panzazo lo manda el servidor (Multicast_OnDiveVisual).
	UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
	if (Capsule && IsOnBelly() && InCapsuleHalfHeight > 0.f && Capsule->GetUnscaledCapsuleHalfHeight() > InCapsuleHalfHeight + 0.5f)
	{
		Capsule->SetCapsuleSize(Capsule->GetUnscaledCapsuleRadius(), InCapsuleHalfHeight, false);
	}
}

void UTN_TurtleMovementComponent::ConsumeMoveStartBellyState(uint8& OutPhase, float& OutTime, uint8& OutSerial, float& OutCapsuleHalfHeight)
{
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
	bPendingBounce = false;
	// El movimiento ya se ha guardado (el cliente guarda antes de simular): lo de antes del brinco ya no sirve.
	bHasPreJumpBelly = false;
	if (SimulatesBelly())
	{
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
			RestoreStandingCapsule();
			break;
		}
		BellyTime += DeltaSeconds;
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
			const bool bStopped = BellyTime >= BellyMinSeconds && Speed <= BellyStopSpeed;
			// Moverse casi parada la levanta (el movimiento del jugador solo llega aquí entonces: ATortugaCharacter::Move).
			const bool bWantsOut = !Acceleration.IsNearlyZero() && CanLeaveSlide();
			bWantStand = bStopped || BellyTime >= MaxSeconds || bWantsOut;
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
		}
		break;
	}
}

void UTN_TurtleMovementComponent::StartBellySlide(const FHitResult& FloorHit, uint8 Serial, bool bFromAir)
{
	BellyPhase = ETNBellyPhase::Slide;
	BellyTime = 0.f;
	SlideSerial = Serial;
	bPendingBounce = false;
	RedirectAlongFloor(FloorHit, bFromAir ? BellyLandingKeep : 1.f, BellyMaxEntrySpeed);
	UpdateSlideSurface();
	if (CharacterOwner && !CharacterOwner->bClientUpdating)
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[Panzazo] %s cae de tripa y se arrastra a %.0f cm/s."), *GetNameSafe(CharacterOwner), Velocity.Size2D());
	}
}

void UTN_TurtleMovementComponent::RedirectAlongFloor(const FHitResult& FloorHit, float Keep, float Cap)
{
	// La inercia a lo largo del suelo: se quita lo que iba contra él (el golpe). En llano es la velocidad horizontal; en
	// una bajada, parte de la caída se convierte en arrastre; en una subida, se pierde.
	FVector Along = Velocity;
	if (FloorHit.bBlockingHit && !FloorHit.ImpactNormal.IsNearlyZero())
	{
		const FVector N = FloorHit.ImpactNormal.GetSafeNormal();
		const double Into = FVector::DotProduct(Along, N);
		if (Into < 0.0)
		{
			Along -= N * Into;
		}
	}
	const FVector Flat(Along.X, Along.Y, 0.0);
	const double NewSpeed = FMath::Min(Flat.Size() * static_cast<double>(Keep), static_cast<double>(Cap));
	const FVector Dir = Flat.GetSafeNormal();
	Velocity.X = Dir.X * NewSpeed;
	Velocity.Y = Dir.Y * NewSpeed;
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
				RedirectAlongFloor(Hit, 1.f, BellyMaxSpeed);
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
	const float SlideFriction = SlideFrictionNow();

	// Gravedad a lo largo del suelo, en horizontal: la velocidad al andar es horizontal y el suelo la inclina al moverse.
	// Con la normal N, la componente horizontal de g·senθ por el suelo es g·(Nx, Ny)·Nz, hacia abajo de la cuesta.
	FVector N = FVector::UpVector;
	if (CurrentFloor.IsWalkableFloor() && !CurrentFloor.HitResult.ImpactNormal.IsNearlyZero())
	{
		N = CurrentFloor.HitResult.ImpactNormal.GetSafeNormal();
	}
	const float G = FMath::Abs(GetGravityZ());
	const float SlopeScale = BellySlopeGravity * FMath::Max(0.f, TNBellySlide::GSlopeScale);
	const FVector SlopeAccel = FVector(N.X, N.Y, 0.0) * static_cast<double>(G * static_cast<float>(N.Z) * SlopeScale);
	// El rozamiento va con el peso que apoya (menos en cuesta).
	const float NormalShare = FMath::Clamp(static_cast<float>(N.Z), 0.2f, 1.f);
	const float DragK = FMath::Max(0.f, BellyDrag);

	// Integración en pasos cortos (igual a cualquier ritmo de fotogramas y al repetir movimientos). Sin la entrada del
	// jugador: sobre la tripa no se dirige.
	FVector V(Velocity.X, Velocity.Y, 0.0);
	float Remaining = DeltaTime;
	constexpr float MaxStep = 1.f / 60.f;
	while (Remaining > UE_KINDA_SMALL_NUMBER)
	{
		const float H = FMath::Min(Remaining, MaxStep);
		Remaining -= H;
		V += SlopeAccel * static_cast<double>(H);
		const float Speed = static_cast<float>(V.Size());
		if (Speed <= UE_KINDA_SMALL_NUMBER)
		{
			V = FVector::ZeroVector;
			continue;
		}
		// Rozamiento seco más freno por velocidad; en una cuesta suave el rozamiento puede más y se queda quieta.
		const float Loss = (SlideFriction * NormalShare + DragK * Speed) * H;
		V = Loss >= Speed ? FVector::ZeroVector : V * static_cast<double>((Speed - Loss) / Speed);
	}
	V = V.GetClampedToMaxSize(static_cast<double>(BellyMaxSpeed));
	Velocity.X = V.X;
	Velocity.Y = V.Y;
	SlideIntentVelocity = V;
	LastSlideFriction = SlideFriction * NormalShare;
	LastSlopeAccel = SlopeAccel;
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
	Super::HandleImpact(Hit, TimeSlice, MoveDelta);
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
			const double Into = FVector::DotProduct(SlideIntentVelocity, PendingBounceNormal);
			if (-Into >= static_cast<double>(BellyBounceMinSpeed))
			{
				const FVector Tangent = SlideIntentVelocity - PendingBounceNormal * Into;
				const FVector Bounced = Tangent * static_cast<double>(BellyBounceTangentKeep)
					- PendingBounceNormal * (Into * static_cast<double>(BellyBounceRestitution));
				Velocity.X = Bounced.X;
				Velocity.Y = Bounced.Y;
				if (CharacterOwner && !CharacterOwner->bClientUpdating)
				{
					UE_LOG(LogTortunabo, Verbose, TEXT("[Panzazo] %s rebota a %.0f cm/s."), *GetNameSafe(CharacterOwner), Bounced.Size2D());
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

float UTN_TurtleMovementComponent::GetMaxSpeed() const
{
	const float Base = Super::GetMaxSpeed();
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

void UTN_TurtleMovementComponent::ClientAdjustPosition_Implementation(float TimeStamp, FVector NewLoc, FVector NewVel, UPrimitiveComponent* NewBase,
	FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, TOptional<FRotator> OptionalRotation)
{
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
	const FString Text = FString::Printf(TEXT("[Panzazo] %s (%s) · %s %.2f s · %.0f cm/s · %s· roce %.0f cm/s² · pendiente %.0f cm/s² · panzazo %s nº %d (arrastre del nº %d)"),
		*GetNameSafe(CharacterOwner), bLocal ? TEXT("local") : TEXT("servidor"), TNBellySlide::PhaseName(BellyPhase), BellyTime,
		Velocity.Size2D(), *SurfaceText, LastSlideFriction, LastSlopeAccel.Size(),
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
