#include "Voice/TN_VoiceRouting.h"

#include "GameFramework/PlayerState.h"

namespace TNVoiceRouting
{
	bool SharesIntercom(int32 SpeakerGroup, int32 ListenerGroup)
	{
		return SpeakerGroup != INDEX_NONE && SpeakerGroup == ListenerGroup;
	}

	ERoute Route(int32 SpeakerGroup, const FCandidate& Listener, double OuterRadiusCm)
	{
		if (SharesIntercom(SpeakerGroup, Listener.IntercomGroup))
		{
			return ERoute::Intercom;
		}
		return Listener.DistanceSquared <= FMath::Square(OuterRadiusCm) ? ERoute::Proximity : ERoute::None;
	}

	TArray<ERoute> SelectListeners(int32 SpeakerGroup, TConstArrayView<FCandidate> Candidates, double OuterRadiusCm,
		int32 MaxProximityListeners)
	{
		TArray<ERoute> Routes;
		Routes.Reserve(Candidates.Num());
		TArray<int32> Proximity;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const ERoute Found = Route(SpeakerGroup, Candidates[Index], OuterRadiusCm);
			Routes.Add(Found);
			if (Found == ERoute::Proximity)
			{
				Proximity.Add(Index);
			}
		}
		if (MaxProximityListeners <= 0 || Proximity.Num() <= MaxProximityListeners)
		{
			return Routes;
		}
		// Solo los más cercanos: los de lejos la oirían muy baja de todos modos.
		Proximity.StableSort([&Candidates](int32 A, int32 B) { return Candidates[A].DistanceSquared < Candidates[B].DistanceSquared; });
		for (int32 Rank = MaxProximityListeners; Rank < Proximity.Num(); ++Rank)
		{
			Routes[Proximity[Rank]] = ERoute::None;
		}
		return Routes;
	}

	int32 IntercomGroupOf(const APlayerState* PlayerState)
	{
		const ITN_VoiceIntercom* Intercom = Cast<ITN_VoiceIntercom>(PlayerState);
		return Intercom ? Intercom->GetVoiceIntercomGroup() : INDEX_NONE;
	}
}
