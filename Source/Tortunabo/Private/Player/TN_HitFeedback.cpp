#include "Player/TN_HitFeedback.h"

#include "Core/TN_Log.h"
#include "GameFramework/PlayerController.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "Settings/TN_SettingsSaveGame.h"
#include "World/Beach/TN_BeachCameraShake.h"

namespace TNHitFeedbackDetail
{
	// Trauma del temblor (UTN_BeachCameraShake) del golpe más flojo y del más fuerte: corto, por debajo del mazazo del
	// cangrejo gigante (0,8), que es un golpe de jefe.
	constexpr float MinShakeTrauma = 0.2f;
	constexpr float MaxShakeTrauma = 0.55f;

	// Vibración del mando: intensidad (0..1) y duración (s) del golpe más flojo y del más fuerte.
	constexpr float MinVibration = 0.3f;
	constexpr float MaxVibration = 1.f;
	constexpr float MinVibrationSeconds = 0.12f;
	constexpr float MaxVibrationSeconds = 0.35f;
}

float TNHitFeedback::StrengthFromImpulse(float ImpulseSize)
{
	const float Alpha = FMath::Clamp(FMath::Max(0.f, ImpulseSize) / FullImpulse, 0.f, 1.f);
	return FMath::Lerp(MinStrength, 1.f, Alpha);
}

TNHitFeedback::FPlan TNHitFeedback::MakePlan(float Strength, const FToggles& Toggles)
{
	using namespace TNHitFeedbackDetail;
	FPlan Plan;
	const float S = FMath::Clamp(Strength, 0.f, 1.f);
	if (S <= 0.f)
	{
		return Plan;
	}
	if (Toggles.bCameraShake)
	{
		Plan.ShakeTrauma = FMath::Lerp(MinShakeTrauma, MaxShakeTrauma, S);
	}
	if (Toggles.bVibration)
	{
		Plan.VibrationIntensity = FMath::Lerp(MinVibration, MaxVibration, S);
		Plan.VibrationSeconds = FMath::Lerp(MinVibrationSeconds, MaxVibrationSeconds, S);
	}
	return Plan;
}

void TNHitFeedback::PlayLocal(APlayerController* PC, float Strength)
{
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	FToggles Toggles;
	if (const UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(PC))
	{
		// Los de quien recibe el golpe: en la partida local cada invitado tiene su temblor y su vibración (#311).
		const FTNGameSettings Own = Settings->GetSettingsFor(PC);
		Toggles.bCameraShake = Own.bCameraShake;
		Toggles.bVibration = Own.bGamepadVibration;
	}

	const FPlan Plan = MakePlan(Strength, Toggles);
	if (Plan.ShakeTrauma > 0.f)
	{
		if (UTN_BeachCameraShake* Shake = UTN_BeachCameraShake::GetFor(PC))
		{
			Shake->AddTrauma(Plan.ShakeTrauma);
		}
	}
	if (Plan.VibrationIntensity > 0.f)
	{
		PC->PlayDynamicForceFeedback(Plan.VibrationIntensity, Plan.VibrationSeconds, true, true, true, true);
	}
	UE_LOG(LogTortunabo, Log, TEXT("[HitFeedback] %s fuerza=%.2f temblor=%.2f vibracion=%.2f/%.2fs"),
		*GetNameSafe(PC), Strength, Plan.ShakeTrauma, Plan.VibrationIntensity, Plan.VibrationSeconds);
}
