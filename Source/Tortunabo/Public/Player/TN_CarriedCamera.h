#pragma once

#include "CoreMinimal.h"

/**
 * Cámara de tercera persona mientras un ave se lleva a la tortuga por el aire (ATortugaCharacter::TickCameraInterp,
 * ATN_BeachEnemy::IsTurtleCarriedThroughAir): se aleja (más brazo y algo más alta) para ver adónde la lleva y, al soltarla,
 * vuelve a su sitio sin saltos. En primera persona y en VR no cambia nada. Cuentas puras.
 */
namespace TNCarriedCamera
{
	/** Avanza el tirón (0 nada, 1 del todo) a ritmo constante: llega a 1 en RiseSeconds llevada y vuelve a 0 en ReturnSeconds. */
	inline float StepPull(float Pull, bool bCarried, float DeltaSeconds, float RiseSeconds, float ReturnSeconds)
	{
		const float Seconds = FMath::Max(bCarried ? RiseSeconds : ReturnSeconds, UE_KINDA_SMALL_NUMBER);
		const float Step = FMath::Max(DeltaSeconds, 0.f) / Seconds;
		return FMath::Clamp(Pull + (bCarried ? Step : -Step), 0.f, 1.f);
	}

	/** Cuánto se aplica del tirón: arranca y llega despacio (sin velocidad en los extremos), sin tirones al empezar ni al acabar. */
	inline float Ease(float Pull)
	{
		return FMath::SmoothStep(0.f, 1.f, Pull);
	}
}
