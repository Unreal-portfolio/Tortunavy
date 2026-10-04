// ATN_Buggy: input de la conductora (Enhanced Input creado en C++, UTN_BuggyInputSet).

#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyData.h"
#include "Vehicles/TN_BuggyInput.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "Engine/World.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"

UTN_BuggyInputSet* ATN_Buggy::GetInputSet()
{
	if (!InputSet)
	{
		InputSet = UTN_BuggyInputSet::Create(this);
	}
	return InputSet;
}

void ATN_Buggy::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogTNBuggy, Error, TEXT("%s: el buggy necesita un UEnhancedInputComponent"), *GetName());
		return;
	}
	const UTN_BuggyInputSet* Set = GetInputSet();
	Input->BindAction(Set->Throttle, ETriggerEvent::Triggered, this, &ATN_Buggy::OnThrottle);
	Input->BindAction(Set->Throttle, ETriggerEvent::Completed, this, &ATN_Buggy::OnThrottle);
	Input->BindAction(Set->Brake, ETriggerEvent::Triggered, this, &ATN_Buggy::OnBrake);
	Input->BindAction(Set->Brake, ETriggerEvent::Completed, this, &ATN_Buggy::OnBrake);
	Input->BindAction(Set->Steer, ETriggerEvent::Triggered, this, &ATN_Buggy::OnSteer);
	Input->BindAction(Set->Steer, ETriggerEvent::Completed, this, &ATN_Buggy::OnSteer);
	Input->BindAction(Set->Handbrake, ETriggerEvent::Started, this, &ATN_Buggy::OnHandbrakePressed);
	Input->BindAction(Set->Handbrake, ETriggerEvent::Completed, this, &ATN_Buggy::OnHandbrakeReleased);
	Input->BindAction(Set->Boost, ETriggerEvent::Started, this, &ATN_Buggy::OnBoostPressed);
	Input->BindAction(Set->Boost, ETriggerEvent::Completed, this, &ATN_Buggy::OnBoostReleased);
	// Started: una muesca de la rueda o una pulsación de la cruceta cambia una vez.
	Input->BindAction(Set->CycleAmmo, ETriggerEvent::Started, this, &ATN_Buggy::OnCycleAmmo);
	Input->BindAction(Set->SelfRight, ETriggerEvent::Started, this, &ATN_Buggy::OnSelfRightPressed);
	Input->BindAction(Set->SelfRight, ETriggerEvent::Completed, this, &ATN_Buggy::OnSelfRightReleased);
	// Mantener el botón repite el disparo: la cadencia la limita el servidor.
	Input->BindAction(Set->FireCoco, ETriggerEvent::Triggered, this, &ATN_Buggy::OnFireCoco);
	Input->BindAction(Set->FireCoco, ETriggerEvent::Completed, this, &ATN_Buggy::OnFireCocoReleased);
	Input->BindAction(Set->FireSpecial, ETriggerEvent::Started, this, &ATN_Buggy::OnFireSpecial);
	Input->BindAction(Set->FireBack, ETriggerEvent::Started, this, &ATN_Buggy::OnFireBackPressed);
	Input->BindAction(Set->FireBack, ETriggerEvent::Completed, this, &ATN_Buggy::OnFireBackReleased);
}

void ATN_Buggy::NotifyControllerChanged()
{
	if (InputSet)
	{
		UTN_BuggyInputSet::RemoveContext(Cast<APlayerController>(PreviousController), InputSet->DriverContext);
	}
	if (const APlayerController* PC = Cast<APlayerController>(Controller); PC && PC->IsLocalController())
	{
		UTN_BuggyInputSet::AddContext(PC, GetInputSet()->DriverContext);
	}
	bSelfRightHeld = false;
	bAimBackward = false;
	bDriverFireLatched = false;
	// También en el servidor (PossessedBy y UnPossessed pasan por aquí): una conductora que sale no deja el turbo pisado.
	bBoostHeld = false;
	RespawnHold = TNBuggy::FHold();
	Super::NotifyControllerChanged();
}

void ATN_Buggy::OnThrottle(const FInputActionValue& Value)
{
	if (UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement())
	{
		Move->SetThrottleInput(IsEngineLocked() || bRaceBrakeHeld ? 0.f : Value.Get<float>());
	}
}

void ATN_Buggy::OnBrake(const FInputActionValue& Value)
{
	if (UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement())
	{
		// En la parrilla frena el estacionamiento (ApplyRaceBrake): el pedal, parado, metería la marcha atrás (#611).
		Move->SetBrakeInput(bRaceBrakeHeld ? 0.f : Value.Get<float>());
	}
}

void ATN_Buggy::OnSteer(const FInputActionValue& Value)
{
	// Tick añade el contravolante y el bamboleo antes de mandarlo al movimiento.
	SteerRequest = FMath::Clamp(Value.Get<float>(), -1.f, 1.f);
}

void ATN_Buggy::OnHandbrakePressed(const FInputActionValue& Value)
{
	SetHandbrakeHeld(true);
}

void ATN_Buggy::OnHandbrakeReleased(const FInputActionValue& Value)
{
	SetHandbrakeHeld(false);
}

void ATN_Buggy::SetHandbrakeHeld(bool bHeld)
{
	if (UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement())
	{
		Move->SetHandbrakeInput(bHeld);
	}
	bHandbrakeHeld = bHeld;
	ApplyWheelFriction();
	if (!HasAuthority())
	{
		ServerSetHandbrake(bHeld);
	}
}

void ATN_Buggy::ServerSetHandbrake_Implementation(bool bHeld)
{
	bHandbrakeHeld = bHeld;
}

void ATN_Buggy::HandleSelfRightInput(bool bPressed)
{
	bSelfRightHeld = bPressed;
	RespawnHold = TNBuggy::FHold();
	if (bPressed)
	{
		ServerSelfRight();
	}
}

void ATN_Buggy::OnSelfRightPressed(const FInputActionValue& Value)
{
	HandleSelfRightInput(true);
}

void ATN_Buggy::OnSelfRightReleased(const FInputActionValue& Value)
{
	HandleSelfRightInput(false);
}

void ATN_Buggy::OnFireCoco(const FInputActionValue& Value)
{
	// Con artillera, la conductora no dispara (lo revalida el servidor). Botón principal: la munición seleccionada. Con el
	// coco, mantenerlo pide a su cadencia (no cada frame); con una especial, un disparo por pulsación (no gasta las cargas
	// manteniendo el botón).
	if (bGunnerSeated || bDriverFireLatched)
	{
		return;
	}
	const ETNRallyAmmo Selected = Turret ? Turret->GetSelectedAmmo() : ETNRallyAmmo::Coco;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastDriverFireRequest >= TNRallyTurret::SpecFor(Selected).FireInterval)
	{
		LastDriverFireRequest = Now;
		bDriverFireLatched = TNRallyTurret::IsSpecial(Selected);
		RequestDriverFire(false, bAimBackward);
	}
}

void ATN_Buggy::OnFireCocoReleased(const FInputActionValue& Value)
{
	bDriverFireLatched = false;
}

void ATN_Buggy::OnFireSpecial(const FInputActionValue& Value)
{
	if (!bGunnerSeated)
	{
		RequestDriverFire(true, bAimBackward);
	}
}

void ATN_Buggy::RequestDriverFire(bool bSpecial, bool bBackward)
{
	ServerDriverFire(bSpecial, bBackward);
}

void ATN_Buggy::OnBoostPressed(const FInputActionValue& Value)
{
	SetBoostHeld(true);
}

void ATN_Buggy::OnBoostReleased(const FInputActionValue& Value)
{
	SetBoostHeld(false);
}

void ATN_Buggy::OnCycleAmmo(const FInputActionValue& Value)
{
	// Con artillera, la munición la elige ella (su peón liga la misma acción); la torreta manda la petición al servidor.
	const int32 Direction = UTN_BuggyInputSet::CycleDirection(Value);
	if (!bGunnerSeated && Turret && Direction != 0)
	{
		Turret->CycleAmmo(Direction);
	}
}

void ATN_Buggy::OnFireBackPressed(const FInputActionValue& Value)
{
	bAimBackward = true;
}

void ATN_Buggy::OnFireBackReleased(const FInputActionValue& Value)
{
	bAimBackward = false;
}
