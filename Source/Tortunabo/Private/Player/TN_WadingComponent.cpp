#include "Player/TN_WadingComponent.h"
#include "Player/TN_WadingDecisions.h"
#include "Player/TN_StaminaComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "EngineUtils.h"

UTN_WadingComponent::UTN_WadingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f; // 10 Hz — de sobra para un efecto ambiental.
}

void UTN_WadingComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	AActor* WaterActor = nullptr;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(WaterActorTag))
			{
				WaterActor = *It;
				break;
			}
		}
	}

	if (!WaterActor)
	{
		// Sin agua en el nivel — apagar el componente, cero coste.
		bHasWater = false;
		SetComponentTickEnabled(false);
		return;
	}

	WaterZ   = WaterActor->GetActorLocation().Z;
	bHasWater = true;

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		StaminaComponentRef = Character->FindComponentByClass<UTN_StaminaComponent>();
	}
}

void UTN_WadingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bHasWater)
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}

	UCharacterMovementComponent* CMC = Character->GetCharacterMovement();
	if (!CMC)
	{
		return;
	}

	const float HalfHeight = Character->GetCapsuleComponent()
		? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 0.f;
	const float FeetZ = Character->GetActorLocation().Z - HalfHeight;

	CurrentDepth = WaterZ - FeetZ;

	const bool bWasInWater = bIsInWater;
	bIsInWater = CurrentDepth > MinDepth && CMC->IsMovingOnGround();

	// Multiplicador de velocidad/salto: solo servidor o cliente dueño del pawn — evita
	// pisar la predicción del CMC en máquinas ajenas (mismo criterio que TN_SlowZoneVolume).
	if (Character->HasAuthority() || Character->IsLocallyControlled())
	{
		ApplyMovementEffects(bWasInWater);
	}

	// Salpicaduras: en toda máquina con render, también para pawns simulados.
	UpdateSplashEffects(DeltaTime, bWasInWater, CMC);
}

float UTN_WadingComponent::GetSpeedMultiplierAt(double FeetZ, bool bOnGround) const
{
	if (!bHasWater || !bOnGround)
	{
		return 1.f;
	}
	return TNWadingLogic::ComputeSpeedMultiplier(static_cast<float>(WaterZ - FeetZ), MinDepth, FullDepth, WadeSpeedMultiplier);
}

void UTN_WadingComponent::ApplyMovementEffects(bool bWasInWater)
{
	UTN_StaminaComponent* Stamina = StaminaComponentRef.Get();
	if (!Stamina)
	{
		return;
	}
	const float SpeedMult = bIsInWater
		? TNWadingLogic::ComputeSpeedMultiplier(CurrentDepth, MinDepth, FullDepth, WadeSpeedMultiplier)
		: 1.f;
	Stamina->SetEnvironmentSpeedMultiplier(SpeedMult);

	if (bIsInWater == bWasInWater)
	{
		return;
	}

	// El salto en el agua es un límite con nombre (UTN_StaminaComponent), como el sirope de TN_SlowZoneVolume: el componente
	// guarda el salto de base y lo devuelve al quitar el último, así que salir del agua dentro de una zona lenta (o al
	// revés) ya no deja el salto cambiado.
	if (bIsInWater)
	{
		Stamina->SetJumpLimit(TNMovementLimits::WadingSource(), TNMovementLimits::NoCap, JumpInWaterMultiplier);
	}
	else
	{
		Stamina->ClearJumpLimit(TNMovementLimits::WadingSource());
	}
}

void UTN_WadingComponent::UpdateSplashEffects(float DeltaTime, bool bWasInWater, const UCharacterMovementComponent* CMC)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (bIsInWater && !bWasInWater)
	{
		// Entra en el agua (caminando o justo al aterrizar) → salpicadura de entrada.
		SpawnSplashEffect();
		TimeSinceLastSplash = 0.f;
		return;
	}

	if (!bIsInWater)
	{
		return;
	}

	TimeSinceLastSplash += DeltaTime;
	if (CMC->Velocity.Size2D() > SplashSpeedThreshold && TimeSinceLastSplash >= SplashInterval)
	{
		TimeSinceLastSplash = 0.f;
		SpawnSplashEffect();
	}
}

void UTN_WadingComponent::SpawnSplashEffect() const
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}

	FVector Location = Character->GetActorLocation();
	Location.Z = WaterZ;

	if (WadeSplashFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, WadeSplashFX, Location);
	}
	if (WadeStepSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, WadeStepSound, Location);
	}
}
