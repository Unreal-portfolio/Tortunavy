#include "Kart/TN_KartPlayerController.h"

#include "Engine/World.h"
#include "Kart/TN_KartGameMode.h"
#include "Kart/TN_KartGameState.h"
#include "Kart/TN_KartHUDWidget.h"
#include "VR/TN_VRMode.h"

namespace TNKartPlayer
{
	/** Cada cuánto mira un cliente si ya tiene la pista y el suelo (s). */
	constexpr float ReadyCheckSeconds = 0.5f;
	/** Tope de la generación que acepta el servidor (una carrera no llega ni de lejos). */
	constexpr int32 MaxGeneration = 1000000;
}

ATN_KartPlayerController::ATN_KartPlayerController()
{
	KartHUDClass = UTN_KartHUDWidget::StaticClass();
}

void ATN_KartPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController() && KartHUDClass && !KartHUD)
	{
		KartHUD = CreateWidget<UTN_KartHUDWidget>(this, KartHUDClass);
		if (KartHUD)
		{
			// Por encima del HUD del Rally; en VR, al panel del mundo como el resto de la interfaz.
			TNVR::AddToScreen(KartHUD, 1);
		}
	}
}

void ATN_KartPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (KartHUD)
	{
		KartHUD->RemoveFromParent();
		KartHUD = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ATN_KartPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	// El anfitrión usa la pista del servidor: solo avisan los clientes.
	if (HasAuthority() || !IsLocalController())
	{
		return;
	}
	ReadyCheckAccumulator += DeltaTime;
	if (ReadyCheckAccumulator < TNKartPlayer::ReadyCheckSeconds)
	{
		return;
	}
	ReadyCheckAccumulator = 0.f;
	const ATN_KartGameState* KartState = GetWorld() ? GetWorld()->GetGameState<ATN_KartGameState>() : nullptr;
	if (KartState && KartState->MapGeneration != ReportedGeneration && KartState->IsLocalTrackPlayable())
	{
		ReportedGeneration = KartState->MapGeneration;
		ServerReportKartTrackReady(ReportedGeneration);
	}
}

bool ATN_KartPlayerController::ServerReportKartTrackReady_Validate(int32 Generation)
{
	return Generation > 0 && Generation < TNKartPlayer::MaxGeneration;
}

void ATN_KartPlayerController::ServerReportKartTrackReady_Implementation(int32 Generation)
{
	if (ATN_KartGameMode* KartMode = GetWorld() ? GetWorld()->GetAuthGameMode<ATN_KartGameMode>() : nullptr)
	{
		KartMode->NotifyClientTrackReady(this, Generation);
	}
}
