// ATN_Buggy: control de estabilidad sin freno de mano (#288) y turbo (#294). La barra la gasta y la recarga el servidor; el
// par y el empuje se aplican en cada máquina que simula el chasis (como el antivuelco), y la llama y el sonido, en cada
// máquina con pantalla a partir del estado replicado.

#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyData.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "TN_BuggyFlameMesh.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

void ATN_Buggy::UpdateAirborne()
{
	const UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	if (!Move || !Move->HasValidPhysicsState())
	{
		bAirborne = false;
		return;
	}
	bool bAnyContact = false;
	for (int32 Index = 0; Index < Move->Wheels.Num() && !bAnyContact; ++Index)
	{
		bAnyContact = Move->GetWheelState(Index).bInContact;
	}
	bAirborne = !bAnyContact;
}

void ATN_Buggy::ApplyStability()
{
	USkeletalMeshComponent* Chassis = GetMesh();
	if (!Chassis->IsSimulatingPhysics())
	{
		return;
	}
	const UTN_BuggyData* BuggyData = GetData();
	TNBuggy::FStabilityTuning Tuning;
	Tuning.StartSlipDeg = BuggyData->StabilityStartSlipDeg;
	Tuning.Stiffness = BuggyData->StabilityStiffness;
	Tuning.Damping = BuggyData->StabilityDamping;
	Tuning.MaxAccel = BuggyData->StabilityMaxAccel;
	if (Tuning.Stiffness <= 0.f)
	{
		return;
	}
	const FVector Up = GetActorUpVector();
	const FVector Velocity = GetVelocity();
	const float Slip = TNBuggy::SlipAngleDeg(GetActorForwardVector(), Velocity);
	const float YawRate = static_cast<float>(Chassis->GetPhysicsAngularVelocityInRadians() | Up);
	const float Flat = static_cast<float>(FVector(Velocity.X, Velocity.Y, 0.f).Size());
	const float Accel = TNBuggy::StabilityYawAccel(Slip, YawRate, Flat, bHandbrakeHeld, bAirborne, Tuning);
	if (Accel != 0.f)
	{
		// Como aceleración (bAccelChange): igual para cualquier inercia del chasis.
		Chassis->AddTorqueInRadians(Up * Accel, NAME_None, true);
	}
}

bool ATN_Buggy::IsBoosting() const
{
	// Con el motor cortado o el freno de carrera puesto no hay turbo en ninguna máquina, aunque bBoostActive aún no haya
	// llegado (en los clientes llega con retraso).
	if (IsEngineLocked() || bRaceBrakeHeld)
	{
		return false;
	}
	// La conductora local (cliente) no recibe bBoostActive: lo predice con la misma regla que el servidor.
	if (IsLocallyControlled() && !HasAuthority())
	{
		return bBoostHeld && BoostCharge01 > 0.f;
	}
	return bBoostActive;
}

void ATN_Buggy::UpdateBoost(float DeltaSeconds)
{
	const UTN_BuggyData* Tuning = GetData();
	TNBuggy::FBoostTuning Boost;
	Boost.DrainPerSecond = Tuning->BoostDrainPerSecond;
	Boost.DriftRechargePerSecond = Tuning->BoostDriftRechargePerSecond;
	Boost.AirRechargePerSecond = Tuning->BoostAirRechargePerSecond;
	Boost.MinDriftSlipDeg = Tuning->BoostMinDriftSlipDeg;

	const FVector Velocity = GetVelocity();
	TNBuggy::FBoostInput In;
	In.bWantBoost = bBoostHeld && DriverController != nullptr;
	// El freno de carrera cuenta como motor cortado: ni empuja ni gasta la barra.
	In.bEngineLocked = IsEngineLocked() || bRaceBrakeHeld;
	In.bHandbrake = bHandbrakeHeld;
	In.bAirborne = bAirborne;
	In.SlipDeg = TNBuggy::SlipAngleDeg(GetActorForwardVector(), Velocity);
	In.SpeedCms = static_cast<float>(FVector(Velocity.X, Velocity.Y, 0.f).Size());

	const TNBuggy::FBoostStep Step = TNBuggy::AdvanceBoost(BoostCharge01, In, DeltaSeconds, Boost);
	BoostCharge01 = Step.Charge01;
	if (Step.bActive != bBoostActive)
	{
		bBoostActive = Step.bActive;
		ForceNetUpdate();
	}
}

void ATN_Buggy::ApplyBoostPush()
{
	USkeletalMeshComponent* Chassis = GetMesh();
	if (!IsBoosting() || bAirborne || !Chassis->IsSimulatingPhysics())
	{
		return;
	}
	const UTN_BuggyData* Tuning = GetData();
	const float BoostTop = TNRallyTurret::BuggyTopSpeedCms * Tuning->BoostTopSpeedMultiplier;
	const float Accel = TNBuggy::BoostPushAccel(GetForwardSpeedCms(), BoostTop, Tuning->BoostPushAccel, Tuning->BoostPushFadeBandCms);
	if (Accel > 0.f)
	{
		// En el centro de masas y como aceleración: lleva la punta por encima del corte de régimen del motor.
		Chassis->AddForce(GetActorForwardVector() * Accel, NAME_None, true);
	}
}

void ATN_Buggy::RefreshBoostEffects()
{
	const bool bWanted = HasActorBegunPlay() && !IsActorBeingDestroyed() && IsBoosting() && GetNetMode() != NM_DedicatedServer;
	if (bWanted == bBoostEffectsOn)
	{
		return;
	}
	bBoostEffectsOn = bWanted;
	ShowBoostFlames(bWanted && !BoostEffect);
	if (!bWanted)
	{
		if (BoostEffectComponent)
		{
			BoostEffectComponent->Deactivate();
			BoostEffectComponent = nullptr;
		}
		if (BoostSoundComponent)
		{
			BoostSoundComponent->FadeOut(0.25f, 0.f);
			BoostSoundComponent = nullptr;
		}
		return;
	}
	// Sujetos a la carrocería: la llama sigue al escape. Se destruyen solos al desactivarse.
	if (BoostEffect)
	{
		BoostEffectComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(BoostEffect, Body, NAME_None, BoostEffectOffset,
			FRotator(0.f, 180.f, 0.f), EAttachLocation::KeepRelativeOffset, true);
	}
	if (BoostStartSound)
	{
		UGameplayStatics::SpawnSoundAttached(BoostStartSound, Body, NAME_None, BoostEffectOffset, EAttachLocation::KeepRelativeOffset, true);
	}
	if (BoostSound)
	{
		BoostSoundComponent = UGameplayStatics::SpawnSoundAttached(BoostSound, Body, NAME_None, BoostEffectOffset,
			EAttachLocation::KeepRelativeOffset, true);
	}
}

bool ATN_Buggy::HasBoostVisual() const
{
	return BoostEffect != nullptr || (GetBoostFlameMesh() != nullptr && GetBoostFlameMaterial() != nullptr);
}

UStaticMesh* ATN_Buggy::GetBoostFlameMesh() const
{
	return BoostFlameMesh ? BoostFlameMesh.Get() : TNBuggyFlameMesh::GlowCone(BoostFlameColor);
}

UMaterialInterface* ATN_Buggy::GetBoostFlameMaterial() const
{
	return BoostFlameMesh ? BoostFlameMaterial.Get() : TNBuggyFlameMesh::GlowMaterial();
}

void ATN_Buggy::ShowBoostFlames(bool bShow)
{
	UStaticMesh* FlameMesh = bShow && BoostFlames.IsEmpty() ? GetBoostFlameMesh() : nullptr;
	if (FlameMesh && Body)
	{
		UMaterialInterface* FlameMaterial = GetBoostFlameMaterial();
		for (int32 Index = 0; Index < 2; ++Index)
		{
			UStaticMeshComponent* Flame = NewObject<UStaticMeshComponent>(this);
			Flame->SetStaticMesh(FlameMesh);
			if (FlameMaterial)
			{
				Flame->SetMaterial(0, FlameMaterial);
			}
			Flame->SetCollisionProfileName(TEXT("NoCollision"));
			Flame->SetGenerateOverlapEvents(false);
			Flame->SetCastShadow(false);
			Flame->SetupAttachment(Body);
			Flame->RegisterComponent();
			// El cono propio lleva el color en los vértices; una malla asignada se tiñe con el parámetro «Color».
			if (BoostFlameMesh)
			{
				if (UMaterialInstanceDynamic* Mid = Flame->CreateDynamicMaterialInstance(0))
				{
					Mid->SetVectorParameterValue(TEXT("Color"), BoostFlameColor);
				}
			}
			BoostFlames.Add(Flame);
		}
	}
	for (UStaticMeshComponent* Flame : BoostFlames)
	{
		if (Flame)
		{
			Flame->SetVisibility(bShow);
		}
	}
	UpdateBoostFlames();
}

void ATN_Buggy::UpdateBoostFlames()
{
	if (!bBoostEffectsOn || BoostFlames.IsEmpty())
	{
		return;
	}
	const float Time = static_cast<float>(GetWorld()->GetTimeSeconds());
	for (int32 Index = 0; Index < BoostFlames.Num(); ++Index)
	{
		UStaticMeshComponent* Flame = BoostFlames[Index];
		if (!Flame)
		{
			continue;
		}
		// Una llama por tubo: el índice 0 a la izquierda (Y negativa) y el 1 a la derecha, con la dirección reflejada.
		const float Side = Index == 0 ? -1.f : 1.f;
		const FVector Exhaust = BoostEffectOffset + FVector(0.f, Side * BoostFlameSideOffsetCm, 0.f);
		const FVector Dir(BoostFlameDirection.X, Side * FMath::Abs(BoostFlameDirection.Y), BoostFlameDirection.Z);
		// Desfase por tubo para que no parpadeen a la vez.
		const float Flicker = TNBuggy::BoostFlameFlicker(Time + 0.37f * Index, BoostFlameFlickerAmount);
		Flame->SetRelativeTransform(TNBuggy::BoostFlameTransform(Exhaust, Dir, BoostFlameLengthCm, BoostFlameDiameterCm, Flicker));
	}
}

void ATN_Buggy::DebugHoldBoost(bool bHold)
{
#if !UE_BUILD_SHIPPING
	if (!HasAuthority())
	{
		return;
	}
	BoostCharge01 = 1.f;
	SetBoostHeld(bHold);
#endif
}

void ATN_Buggy::SetBoostHeld(bool bHeld)
{
	bBoostHeld = bHeld;
	if (!HasAuthority())
	{
		ServerSetBoostHeld(bHeld);
	}
}

void ATN_Buggy::ServerSetBoostHeld_Implementation(bool bHeld)
{
	// Solo se guarda el botón: UpdateBoost decide con la carga y el motor si el turbo empuja.
	bBoostHeld = bHeld;
}

void ATN_Buggy::SetRaceBrakeHeld(bool bHeld)
{
	if (!HasAuthority() || bRaceBrakeHeld == bHeld)
	{
		return;
	}
	bRaceBrakeHeld = bHeld;
	if (bHeld)
	{
		bBoostActive = false;
		ApplyRaceBrake();
	}
	else
	{
		ReleaseRaceBrake();
	}
	ApplyEngineTorque();
	ForceNetUpdate();
}

void ATN_Buggy::OnRep_RaceBrake()
{
	if (bRaceBrakeHeld)
	{
		ApplyRaceBrake();
	}
	else
	{
		ReleaseRaceBrake();
	}
}

void ATN_Buggy::ApplyRaceBrake()
{
	UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	if (!Move)
	{
		return;
	}
	// Freno de estacionamiento de Chaos (par del freno de mano en las cuatro ruedas), en cada máquina. El pedal de freno no
	// sirve parado: con bReverseAsBrake, frenar a menos de WrongDirectionThreshold mete la marcha atrás y el freno pasa a
	// acelerador (ChaosVehicleMovementComponent::CalcThrottleBrakeInput), y con el motor cortado el buggy rodaba (#611).
	Move->SetParked(true);
	// Cada fotograma: la entrada de la conductora (o del piloto IA) puede haber vuelto a pisar el acelerador o el freno.
	Move->SetThrottleInput(0.f);
	Move->SetBrakeInput(0.f);
}

void ATN_Buggy::ReleaseRaceBrake()
{
	bHasGridAnchor = false;
	UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	if (!Move)
	{
		return;
	}
	// Todos los buggies se sueltan a la vez con el verde (StartRacing), sin tirón: solo se quita el freno de estacionamiento.
	Move->SetParked(false);
	if (HasAuthority() || IsLocallyControlled())
	{
		Move->SetBrakeInput(0.f);
	}
}

void ATN_Buggy::HoldOnGrid()
{
	USkeletalMeshComponent* Chassis = GetMesh();
	const float Gain = GetData()->GridHoldGain;
	if (!bRaceBrakeHeld || bAirborne || Gain <= 0.f || !Chassis || !Chassis->IsSimulatingPhysics())
	{
		// En el aire (al nacer cae unos centímetros hasta apoyarse) no se ancla: se toma el sitio al tocar el suelo.
		return;
	}
	TNBuggy::FGridHoldTuning Tuning;
	Tuning.PositionGain = Gain;
	const FVector Location = GetActorLocation();
	if (!bHasGridAnchor || FVector::Dist(Location, GridAnchor) > Tuning.ReanchorDistanceCm)
	{
		// Primer apoyo o lo han recolocado (hueco de la parrilla, reaparición): ese es su sitio.
		GridAnchor = Location;
		bHasGridAnchor = true;
	}
	const FVector Up = GetActorUpVector();
	Chassis->SetPhysicsLinearVelocity(TNBuggy::GridHoldVelocity(Chassis->GetPhysicsLinearVelocity(), Up, Location - GridAnchor, Tuning));
	Chassis->SetPhysicsAngularVelocityInRadians(TNBuggy::GridHoldAngularVelocity(Chassis->GetPhysicsAngularVelocityInRadians(), Up));
}
