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
	DOREPLIFETIME_CONDITION(UTN_StaminaComponent, bSprintRequested, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UTN_StaminaComponent, bUnlimitedStamina, COND_OwnerOnly);
	DOREPLIFETIME(UTN_StaminaComponent, bIsExhausted);
	DOREPLIFETIME_CONDITION(UTN_StaminaComponent, bPostBoostPenaltyActive, COND_OwnerOnly);
}

void UTN_StaminaComponent::SetSprintRequested(bool bRequested)
{
	const bool bChanged = bRequested != bSprintRequested;
	bSprintRequested = bRequested;
	RecomputeSprintState();

	// Al servidor solo cuando cambia: el personaje lo pide en cada fotograma mientras se mueve (RefreshSprintRequest) y un
	// RPC fiable por fotograma y jugador llenaba el búfer de fiables del cliente. Si el servidor lo cambia por su cuenta (al
	// meterse en el caparazón), la réplica (solo al dueño) lo trae aquí y la siguiente petición vuelve a salir.
	if (bChanged && GetOwner() && !GetOwner()->HasAuthority())
	{
		ServerSetSprintRequested(bRequested);
	}
}

void UTN_StaminaComponent::GrantUnlimitedStamina(float DurationSeconds)
{
	if (DurationSeconds <= 0.0f)
	{
		return;
	}

	if (!GetOwner())
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		ServerGrantUnlimitedStamina(DurationSeconds);
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

void UTN_StaminaComponent::ServerSetSprintRequested_Implementation(bool bRequested)
{
	bSprintRequested = bRequested;
	RecomputeSprintState();
}

bool UTN_StaminaComponent::ServerGrantUnlimitedStamina_Validate(float DurationSeconds)
{
	// Cota generosa: rechaza (el engine desconecta) clientes que envíen valores
	// absurdos/NaN. El clamp real a 15s lo hace el _Implementation; esta validación
	// es una red de seguridad a nivel de engine que no rechaza tráfico legítimo.
	return DurationSeconds >= 0.f && DurationSeconds <= 300.f;
}

void UTN_StaminaComponent::ServerGrantUnlimitedStamina_Implementation(float DurationSeconds)
{
	// Clamp to prevent clients from granting themselves permanent unlimited stamina.
	constexpr float MaxGrantDuration = 15.f;
	GrantUnlimitedStamina(FMath::Clamp(DurationSeconds, 0.f, MaxGrantDuration));
}

void UTN_StaminaComponent::RestoreStaminaToFull()
{
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		// El servidor aplica el efecto; el cliente solo lo solicita.
		// Reutilizamos el patron de GrantUnlimitedStamina sin RPC dedicada:
		// el item pickup ya debería ejecutarse via Server RPC en el pickup base.
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

		if (!bUnlimitedStamina)
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
	bool bCanSprint = bSprintRequested;

	if (!bUnlimitedStamina)
	{
		// Puede sprintar si la stamina actual supera un mínimo — comprobamos contra 0,
		// ya que GetEffectiveMaxStamina es el techo, no el suelo.
		bCanSprint = bCanSprint && (CurrentStamina > KINDA_SMALL_NUMBER);
	}

	if (bIsSprinting != bCanSprint)
	{
		bIsSprinting = bCanSprint;
	}

	ApplyMovementSpeed();
}

float UTN_StaminaComponent::GetBaseMoveSpeed() const
{
	float BaseSpeed = bIsSprinting ? SprintSpeed : WalkSpeed;
	if (bPostBoostPenaltyActive)
	{
		BaseSpeed *= PostBoostSpeedMultiplier;
	}
	return BaseSpeed * EnvironmentSpeedMultiplier;
}

float UTN_StaminaComponent::GetRaceBoostWalkSpeed(float Multiplier) const
{
	return TNMovementLimits::RaceBoostWalkSpeed(GetBaseMoveSpeed(), SprintSpeed, Multiplier, ActiveSpeedCap);
}

void UTN_StaminaComponent::ApplyMovementSpeed() const
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			// Sin el turbo de carrera: ese lo pone cada movimiento (GetRaceBoostWalkSpeed).
			Movement->MaxWalkSpeed = GetRaceBoostWalkSpeed(1.f);
		}
	}
}

void UTN_StaminaComponent::SetSpeedCap(FName Source, float Cap)
{
	SpeedCaps.Add(Source, Cap);
	ActiveSpeedCap = TNMovementLimits::ResolveSpeedCap(SpeedCaps);
	ApplyMovementSpeed();
}

void UTN_StaminaComponent::ClearSpeedCap(FName Source)
{
	SpeedCaps.Remove(Source);
	ActiveSpeedCap = TNMovementLimits::ResolveSpeedCap(SpeedCaps);
	ApplyMovementSpeed();
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

