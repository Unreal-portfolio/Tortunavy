#include "Player/TN_StaminaComponent.h"
#include "Player/TN_InventoryComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

UTN_StaminaComponent::UTN_StaminaComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UTN_StaminaComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentStamina = MaxStamina;

	ApplyMovementSpeed();
}

void UTN_StaminaComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (!Owner->HasAuthority())
	{
		return;
	}

	TickUnlimitedTimer(DeltaTime);
	TickStamina(DeltaTime);
	SyncStaminaShared();
}

void UTN_StaminaComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// La stamina exacta, solo al dueño; los demás (espectadores que la miran, las caras del HUD y el foley) la reciben en un
	// byte que solo se manda al cambiar (StaminaShared). bIsExhausted, a todos.
	DOREPLIFETIME_CONDITION(UTN_StaminaComponent, CurrentStamina, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UTN_StaminaComponent, StaminaShared, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UTN_StaminaComponent, bIsSprinting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UTN_StaminaComponent, bUnlimitedStamina, COND_OwnerOnly);
	DOREPLIFETIME(UTN_StaminaComponent, bIsExhausted);
	DOREPLIFETIME_CONDITION(UTN_StaminaComponent, bPostBoostPenaltyActive, COND_OwnerOnly);
}

void UTN_StaminaComponent::SetSprintRequested(bool bRequested)
{
	if (bRequested == bSprintRequested)
	{
		return;
	}
	bSprintRequested = bRequested;
	RecomputeSprintState();
}

bool UTN_StaminaComponent::CanSprint(bool bRequested) const
{
	return bRequested && (bUnlimitedStamina || CurrentStamina > KINDA_SMALL_NUMBER);
}

float UTN_StaminaComponent::ComputeMaxWalkSpeed(bool bSprinting, float EnvironmentMultiplier, float RaceMultiplier) const
{
	TNMovementLimits::FWalkSpeedInputs In;
	In.WalkSpeed = WalkSpeed;
	In.SprintSpeed = SprintSpeed;
	In.bSprinting = bSprinting;
	In.PostBoostMultiplier = bPostBoostPenaltyActive ? PostBoostSpeedMultiplier : 1.f;
	In.EnvironmentMultiplier = EnvironmentMultiplier;
	In.RaceMultiplier = RaceMultiplier;
	In.Cap = ActiveSpeedCap;
	return TNMovementLimits::ResolveWalkSpeed(In);
}

float UTN_StaminaComponent::ComputeMoveMaxWalkSpeed(bool bSprinting, float EnvironmentMultiplier, float RaceMultiplier, uint8 MovePredictedCaps) const
{
	TNMovementLimits::FWalkSpeedInputs In;
	In.WalkSpeed = WalkSpeed;
	In.SprintSpeed = SprintSpeed;
	In.bSprinting = bSprinting;
	In.PostBoostMultiplier = bPostBoostPenaltyActive ? PostBoostSpeedMultiplier : 1.f;
	In.EnvironmentMultiplier = EnvironmentMultiplier;
	In.RaceMultiplier = RaceMultiplier;
	In.Cap = TNMovementLimits::ResolveMoveSpeedCap(UnpredictedSpeedCap, MovePredictedCaps, PredictedCapValues);
	return TNMovementLimits::ResolveWalkSpeed(In);
}

void UTN_StaminaComponent::GrantUnlimitedStamina(float DurationSeconds)
{
	if (DurationSeconds <= 0.0f)
	{
		return;
	}

	// Solo la concede el servidor al usar el objeto: no hay RPC para pedirla desde el cliente (#895).
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	bUnlimitedStamina = true;
	UnlimitedStaminaRemaining = DurationSeconds;
	CurrentStamina = MaxStamina;
	// Fuera el agotamiento, como en RestoreStaminaToFull: solo se descuenta sin esprintar, y con shift pulsado el HUD, la
	// cara y el «sin aliento» lo seguirían enseñando con la barra llena.
	bIsExhausted = false;
	ExhaustionTimer = 0.f;
	RecomputeSprintState();
}

void UTN_StaminaComponent::RestoreStaminaToFull()
{
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		// Solo el servidor aplica el efecto, al usar el objeto (ServerUseEquippedItem).
		return;
	}

	CurrentStamina    = GetEffectiveMaxStamina();
	bIsExhausted      = false;
	ExhaustionTimer   = 0.f;
	RechargeElapsed   = 0.f;
	TimeSinceSprintStopped = 0.f;
	RecomputeSprintState();
}

void UTN_StaminaComponent::SetInventoryComponent(UTN_InventoryComponent* InvComp)
{
	InventoryComponentRef = InvComp;
}

float UTN_StaminaComponent::GetEffectiveMaxStamina() const
{
	float TotalWeight = 0.f;
	if (InventoryComponentRef.IsValid())
	{
		TotalWeight = InventoryComponentRef->GetTotalCarriedWeight();
	}
	const float Penalty = TotalWeight * StaminaPerWeightUnit;
	// La stamina efectiva nunca puede bajar de 1 (evitar división por cero en la UI)
	return FMath::Max(1.f, MaxStamina - Penalty);
}

void UTN_StaminaComponent::OnRep_IsSprinting()
{
	ApplyMovementSpeed();
}

void UTN_StaminaComponent::OnRep_CurrentStamina()
{
	RecomputeSprintState();
}

void UTN_StaminaComponent::SyncStaminaShared()
{
	const float Fraction = FMath::Clamp(CurrentStamina / FMath::Max(1.f, MaxStamina), 0.f, 1.f);
	const uint8 Byte = static_cast<uint8>(FMath::RoundToInt(Fraction * 255.f));
	if (Byte != StaminaShared)
	{
		StaminaShared = Byte;
	}
}

void UTN_StaminaComponent::OnRep_StaminaShared()
{
	CurrentStamina = StaminaShared / 255.f * MaxStamina;
}

void UTN_StaminaComponent::OnRep_UnlimitedStamina()
{
	if (bUnlimitedStamina)
	{
		CurrentStamina = MaxStamina;
	}
}

void UTN_StaminaComponent::TickUnlimitedTimer(float DeltaTime)
{
	if (!bUnlimitedStamina)
	{
		return;
	}

	UnlimitedStaminaRemaining -= DeltaTime;
	CurrentStamina = MaxStamina;
	if (UnlimitedStaminaRemaining <= 0.0f)
	{
		bUnlimitedStamina = false;
		UnlimitedStaminaRemaining = 0.0f;

		// Penalización blanda post-boost: durante PostBoostExhaustionSeconds el
		// jugador se mueve más lento y drena stamina x2. NO se bloquea recuperación
		// ni se resetea CurrentStamina — queremos desincentivar el sprint sin castrar
		// al jugador.
		if (PostBoostExhaustionSeconds > 0.f)
		{
			bPostBoostPenaltyActive = true;
			PostBoostPenaltyTimer   = PostBoostExhaustionSeconds;
		}
	}
}

void UTN_StaminaComponent::TickStamina(float DeltaTime)
{
	// Techo dinámico por peso — si el jugador coge un objeto pesado,
	// la stamina se recorta inmediatamente al nuevo máximo efectivo.
	const float EffMax = GetEffectiveMaxStamina();
	if (CurrentStamina > EffMax)
	{
		CurrentStamina = EffMax;
	}

	// Penalización blanda post-boost corre en paralelo — no bloquea nada,
	// solo escala drain y velocidad mientras el timer expira.
	if (bPostBoostPenaltyActive)
	{
		PostBoostPenaltyTimer -= DeltaTime;
		if (PostBoostPenaltyTimer <= 0.0f)
		{
			bPostBoostPenaltyActive = false;
			PostBoostPenaltyTimer   = 0.0f;
			// ApplyMovementSpeed al quitar el flag — restaura velocidad completa.
			ApplyMovementSpeed();
		}
	}

	if (bIsSprinting)
	{
		TimeSinceSprintStopped = 0.0f;
		RechargeElapsed = 0.0f;

		// Contra una pared (u otro tope que la frena del todo) el sprint sigue pedido pero la tortuga no avanza: no gasta.
		// El sprint no se quita, para que al despegarse corra sin soltar la tecla.
		const AActor* Owner = GetOwner();
		const bool bAdvancing = Owner && Owner->GetVelocity().Size2D() > WalkSpeed * 0.1f;

		if (!bUnlimitedStamina && bAdvancing)
		{
			const float DrainMul = bPostBoostPenaltyActive ? PostBoostDrainMultiplier : 1.0f;
			CurrentStamina = FMath::Max(0.0f, CurrentStamina - (SprintDrainPerSecond * DrainMul * DeltaTime));

			// Marcar agotamiento cuando la stamina llega a cero
			if (CurrentStamina <= 0.0f && !bIsExhausted)
			{
				bIsExhausted = true;
				ExhaustionTimer = ExhaustionPenaltySeconds;
			}
		}
	}
	else
	{
		// Contar la penalización de agotamiento antes del delay normal de recarga
		if (bIsExhausted)
		{
			ExhaustionTimer -= DeltaTime;
			if (ExhaustionTimer <= 0.0f)
			{
				bIsExhausted            = false;
				ExhaustionTimer         = 0.0f;
				// Reiniciar timer de delay normal también
				TimeSinceSprintStopped = 0.0f;
				RechargeElapsed = 0.0f;
			}
			// Bloquear recuperación durante la penalización
			RecomputeSprintState();
			return;
		}

		TimeSinceSprintStopped += DeltaTime;
		if (TimeSinceSprintStopped >= RechargeDelaySeconds)
		{
			RechargeElapsed += DeltaTime;
			const float RechargeRate = RechargeBasePerSecond * FMath::Exp(RechargeExponentGrowth * RechargeElapsed);
			// La recarga se limita al effective max (techo de peso), no al MaxStamina base.
			CurrentStamina = FMath::Min(EffMax, CurrentStamina + (RechargeRate * DeltaTime));
		}
	}

	RecomputeSprintState();
}

void UTN_StaminaComponent::RecomputeSprintState()
{
	// Puede sprintar si la stamina actual supera un mínimo — comprobamos contra 0,
	// ya que GetEffectiveMaxStamina es el techo, no el suelo.
	const bool bCanSprint = CanSprint(bSprintRequested);

	if (bIsSprinting != bCanSprint)
	{
		bIsSprinting = bCanSprint;
	}

	ApplyMovementSpeed();
}

void UTN_StaminaComponent::ApplyMovementSpeed() const
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			// La tortuga la calcula en cada paso del movimiento (UTN_TurtleMovementComponent::GetMaxSpeed), con el sprint, el
			// vadeo y el turbo de carrera de ese movimiento; esto queda, sin turbo, para quien lee MaxWalkSpeed (el sonido) y
			// para personajes sin ese movimiento.
			Movement->MaxWalkSpeed = ComputeMaxWalkSpeed(bIsSprinting, EnvironmentSpeedMultiplier);
		}
	}
}

void UTN_StaminaComponent::SetSpeedCap(FName Source, float Cap)
{
	if (const uint8 Bit = TNMovementLimits::PredictedCapBit(Source))
	{
		const int32 Index = TNMovementLimits::PredictedCapIndex(Bit);
		if ((PredictedCapMask & Bit) == 0)
		{
			PredictedCapGrace[Index] = TNMovementLimits::OpenPredictedCapGrace(PredictedCapGrace[Index], ServerMoveClock);
		}
		PredictedCapMask |= Bit;
		PredictedCapValues[Index] = Cap;
	}
	SpeedCaps.Add(Source, Cap);
	RefreshSpeedCaps();
}

void UTN_StaminaComponent::ClearSpeedCap(FName Source)
{
	if (const uint8 Bit = TNMovementLimits::PredictedCapBit(Source); (PredictedCapMask & Bit) != 0)
	{
		const int32 Index = TNMovementLimits::PredictedCapIndex(Bit);
		PredictedCapGrace[Index] = TNMovementLimits::OpenPredictedCapGrace(PredictedCapGrace[Index], ServerMoveClock);
		PredictedCapMask &= ~Bit;
	}
	SpeedCaps.Remove(Source);
	RefreshSpeedCaps();
}

void UTN_StaminaComponent::RefreshSpeedCaps()
{
	ActiveSpeedCap = TNMovementLimits::ResolveSpeedCap(SpeedCaps);
	UnpredictedSpeedCap = TNMovementLimits::NoCap;
	for (const TPair<FName, float>& Pair : SpeedCaps)
	{
		if (TNMovementLimits::PredictedCapBit(Pair.Key) == 0)
		{
			UnpredictedSpeedCap = FMath::Min(UnpredictedSpeedCap, Pair.Value);
		}
	}
	ApplyMovementSpeed();
}

uint8 UTN_StaminaComponent::ConsumeClientPredictedCaps(uint8 ClaimedMask, float MoveDeltaSeconds)
{
	const float Delta = FMath::Max(0.f, MoveDeltaSeconds);
	uint8 Result = 0;
	for (int32 Index = 0; Index < TNMovementLimits::NumPredictedCaps; ++Index)
	{
		const uint8 Bit = static_cast<uint8>(1 << Index);
		const TNMovementLimits::FPredictedCapStep Step = TNMovementLimits::StepPredictedCap(PredictedCapGrace[Index],
			(ClaimedMask & Bit) != 0, (PredictedCapMask & Bit) != 0, ServerMoveClock, Delta);
		PredictedCapGrace[Index] = Step.Grace;
		Result |= Step.bApply ? Bit : 0;
	}
	ServerMoveClock += Delta;
	return Result;
}

void UTN_StaminaComponent::SetJumpLimit(FName Source, float Cap, float Multiplier)
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}
	// El salto de base se guarda con el primer límite (con alguno puesto, JumpZVelocity ya no es el de base).
	if (JumpLimits.Num() == 0)
	{
		BaseJumpZVelocity = Movement->JumpZVelocity;
	}
	TNMovementLimits::FJumpLimit& Limit = JumpLimits.FindOrAdd(Source);
	Limit.Cap = Cap;
	Limit.Multiplier = Multiplier;
	ApplyJumpLimits();
}

void UTN_StaminaComponent::ClearJumpLimit(FName Source)
{
	if (JumpLimits.Remove(Source) > 0)
	{
		ApplyJumpLimits();
	}
}

void UTN_StaminaComponent::ApplyJumpLimits()
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->JumpZVelocity = TNMovementLimits::ResolveJumpZ(BaseJumpZVelocity, JumpLimits);
		}
	}
}

void UTN_StaminaComponent::SetGravityScaleOverride(FName Source, float Scale)
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}
	if (GravityScaleOverrides.Num() == 0)
	{
		BaseGravityScale = Movement->GravityScale;
	}
	GravityScaleOverrides.Add(Source, Scale);
	ApplyGravityScaleOverrides();
}

void UTN_StaminaComponent::ClearGravityScaleOverride(FName Source)
{
	if (GravityScaleOverrides.Remove(Source) > 0)
	{
		ApplyGravityScaleOverrides();
	}
}

void UTN_StaminaComponent::ApplyGravityScaleOverrides()
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->GravityScale = TNMovementLimits::ResolveGravityScale(BaseGravityScale, GravityScaleOverrides);
		}
	}
}

void UTN_StaminaComponent::SetEnvironmentSpeedMultiplier(float Multiplier)
{
	EnvironmentSpeedMultiplier = Multiplier;
	ApplyMovementSpeed();
}

