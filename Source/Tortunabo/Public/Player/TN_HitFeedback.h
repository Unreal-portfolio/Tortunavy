#pragma once

#include "CoreMinimal.h"

class APlayerController;

/**
 * Respuesta a los golpes de la tortuga (#350): una sacudida corta de cámara y una vibración del mando proporcional al
 * golpe, solo en la máquina del jugador que lo recibe. El servidor decide el golpe (ATortugaCharacter::NotifyHitFeedback)
 * y el cliente lo nota con sus ajustes: «Temblor de cámara» y «Vibración del mando». En VR no hay sacudida (marea).
 */
namespace TNHitFeedback
{
	/** Ajustes del jugador que deciden qué se nota. */
	struct FToggles
	{
		bool bCameraShake = true;
		bool bVibration = true;
		bool bVR = false;
	};

	/** Lo que se hace con un golpe: trauma del temblor de cámara (0..1) y vibración (intensidad 0..1 y segundos). */
	struct FPlan
	{
		float ShakeTrauma = 0.f;
		float VibrationIntensity = 0.f;
		float VibrationSeconds = 0.f;
	};

	/** Fuerza mínima de un golpe que derriba: todo derribo se nota aunque el empujón sea pequeño. */
	constexpr float MinStrength = 0.35f;

	/** Empujón (cm/s) con el que el golpe se nota entero. */
	constexpr float FullImpulse = 1500.f;

	/** Fuerza (0..1) de un golpe a partir del empujón o la velocidad del impacto (cm/s). */
	TORTUNABO_API float StrengthFromImpulse(float ImpulseSize);

	/** Sacudida y vibración para un golpe de fuerza Strength (0..1) con esos ajustes. Lógica pura. */
	TORTUNABO_API FPlan MakePlan(float Strength, const FToggles& Toggles);

	/** Aplica el golpe en el jugador local PC con sus ajustes. No hace nada si PC no es local. */
	TORTUNABO_API void PlayLocal(APlayerController* PC, float Strength);
}
