// Aviso de reaparición manual del HUD del Rally. Ver TN_RallyRespawnHint.h.

#include "Rally/TN_RallyRespawnHint.h"

namespace TNRallyRespawnHint
{
	bool IsStranded(const FInput& Input)
	{
		if (Input.bFlipped)
		{
			return true;
		}
		const bool bSlow = FMath::IsFinite(Input.SpeedCms) && FMath::Abs(Input.SpeedCms) < SlowSpeedCms;
		const bool bOffRoad = FMath::IsFinite(Input.DistanceToAxisCm) && Input.DistanceToAxisCm > OffRoadDistanceCm;
		return bSlow && bOffRoad;
	}

	bool Update(FState& State, const FInput& Input, float DeltaSeconds)
	{
		const bool bRecentRespawn = Input.SecondsSinceRespawn >= 0.f && Input.SecondsSinceRespawn < QuietAfterRespawnSeconds;
		if (!Input.bRacing || Input.bRespawning || bRecentRespawn || !IsStranded(Input))
		{
			State = FState();
			return false;
		}
		// Pasar de atascado a volcado (o al revés) vuelve a contar: cada caso tiene su espera.
		if (State.bWasFlipped != Input.bFlipped)
		{
			State.StrandedSeconds = 0.f;
			State.bWasFlipped = Input.bFlipped;
		}
		State.StrandedSeconds += FMath::Clamp(FMath::IsFinite(DeltaSeconds) ? DeltaSeconds : 0.f, 0.f, 1.f);
		return State.StrandedSeconds >= (Input.bFlipped ? FlippedHintSeconds : StuckHintSeconds);
	}
}
