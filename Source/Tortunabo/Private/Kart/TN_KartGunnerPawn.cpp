#include "Kart/TN_KartGunnerPawn.h"

#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Kart/TN_KartBuggy.h"
#include "Kart/TN_KartInput.h"
#include "Kart/TN_KartItemComponent.h"
#include "VR/TN_VRSeatComponent.h"
#include "VR/TN_VRVehicleMath.h"

UTN_KartInputSet* ATN_KartGunnerPawn::GetKartInput()
{
	if (!KartInput)
	{
		KartInput = UTN_KartInputSet::Create(this);
	}
	return KartInput;
}

void ATN_KartGunnerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		return;
	}
	const UTN_KartInputSet* Set = GetKartInput();
	Input->BindAction(Set->UseItem, ETriggerEvent::Started, this, &ATN_KartGunnerPawn::OnUseItem);
	Input->BindAction(Set->Backward, ETriggerEvent::Started, this, &ATN_KartGunnerPawn::OnBackwardPressed);
	Input->BindAction(Set->Backward, ETriggerEvent::Completed, this, &ATN_KartGunnerPawn::OnBackwardReleased);
	Input->BindAction(Set->Lean, ETriggerEvent::Triggered, this, &ATN_KartGunnerPawn::OnLean);
	Input->BindAction(Set->Lean, ETriggerEvent::Completed, this, &ATN_KartGunnerPawn::OnLean);
}

void ATN_KartGunnerPawn::NotifyControllerChanged()
{
	if (KartInput)
	{
		UTN_KartInputSet::RemoveContext(Cast<APlayerController>(PreviousController), KartInput->GunnerContext);
	}
	if (const APlayerController* PC = Cast<APlayerController>(Controller); PC && PC->IsLocalController())
	{
		UTN_KartInputSet::AddContext(PC, GetKartInput()->GunnerContext);
	}
	LeanInput = 0.f;
	bBackwardHeld = false;
	Super::NotifyControllerChanged();
}

void ATN_KartGunnerPawn::OnUseItem(const FInputActionValue& Value)
{
	const ATN_KartBuggy* Kart = Cast<ATN_KartBuggy>(GetBuggy());
	const UTN_KartItemComponent* Items = Kart ? Kart->GetItems() : nullptr;
	if (Items && Items->CanUseItem())
	{
		ServerUseKartItem(bBackwardHeld);
	}
}

void ATN_KartGunnerPawn::OnBackwardPressed(const FInputActionValue& Value)
{
	bBackwardHeld = true;
}

void ATN_KartGunnerPawn::OnBackwardReleased(const FInputActionValue& Value)
{
	bBackwardHeld = false;
}

void ATN_KartGunnerPawn::OnLean(const FInputActionValue& Value)
{
	LeanInput = FMath::Clamp(Value.Get<float>(), -1.f, 1.f);
}

bool ATN_KartGunnerPawn::ServerUseKartItem_Validate(bool bBackward)
{
	return true;
}

void ATN_KartGunnerPawn::ServerUseKartItem_Implementation(bool bBackward)
{
	ATN_KartBuggy* Kart = Cast<ATN_KartBuggy>(GetBuggy());
	if (Kart && Kart->GetItems() && Kart->MayUseItems(GetController()))
	{
		Kart->GetItems()->UseItem(bBackward);
	}
}

bool ATN_KartGunnerPawn::ServerSetLean_Validate(int8 LeanQ)
{
	return LeanQ >= -100 && LeanQ <= 100;
}

void ATN_KartGunnerPawn::ServerSetLean_Implementation(int8 LeanQ)
{
	ATN_KartBuggy* Kart = Cast<ATN_KartBuggy>(GetBuggy());
	// Solo la artillera sentada en ese kart.
	if (Kart && GetController() && Kart->GetSeatController(ETNRallySeat::Gunner) == GetController())
	{
		Kart->SetGunnerLean(static_cast<float>(LeanQ) / 100.f);
	}
}

void ATN_KartGunnerPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsLocallyControlled() || !IsPlayerControlled())
	{
		return;
	}
	// Con gafas, el cuerpo también: la cabeza apartada a un lado del asiento inclina hacia ese lado (se suma al stick).
	float Wanted = LeanInput;
	if (const UTN_VRSeatComponent* Seat = GetVRSeat(); Seat && Seat->IsHeadsetView())
	{
		Wanted = TNVRVehicle::CombineLean(LeanInput, TNVRVehicle::LeanFromHead(Seat->GetHeadLocal().Y));
	}
	// La inclinación sigue al mando con un poco de inercia (el cuerpo no se tumba de golpe).
	Lean = FMath::FInterpTo(Lean, Wanted, DeltaSeconds, LeanResponse);
	LeanSendAccumulator += DeltaSeconds;
	const int8 Quantized = static_cast<int8>(FMath::RoundToInt(Lean * 100.f));
	const bool bChanged = FMath::Abs(Quantized - LastSentLean) >= 3 || (Quantized == 0 && LastSentLean != 0);
	if (LeanSendAccumulator >= 1.f / FMath::Max(1.f, LeanSendRate) && bChanged)
	{
		LeanSendAccumulator = 0.f;
		LastSentLean = Quantized;
		ServerSetLean(Quantized);
	}
}
