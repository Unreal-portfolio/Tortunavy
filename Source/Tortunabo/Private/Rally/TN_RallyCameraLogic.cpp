#include "Rally/TN_RallyCameraLogic.h"

namespace TNRallyCamera
{
	namespace CameraDetail
	{
		/** Plano lateral: cm por delante, al lado y por encima del buggy. */
		constexpr double SideAheadCm = 650.0;
		constexpr double SideLateralCm = 750.0;
		constexpr double SideUpCm = 160.0;
		/** Dron: cm por detrás y por encima del líder. */
		constexpr double DroneBehindCm = 1900.0;
		constexpr double DroneUpCm = 1300.0;
		/** A más de esto, la cámara que sigue salta en vez de deslizarse (cm). */
		constexpr double FollowSnapCm = 8000.0;
		/** Cámara del podio: distancia, altura sobre la losa y giro (grados de amplitud y rad/s). */
		constexpr double PodiumCameraDistanceCm = 1700.0;
		constexpr double PodiumCameraUpCm = 450.0;
		constexpr double PodiumOrbitDeg = 25.0;
		constexpr double PodiumOrbitSpeed = 0.35;
		/** Alturas de los escalones (1.º, 2.º, 3.º) y separación de la fila de detrás (cm). */
		constexpr double StepHeights[3] = { 120.0, 80.0, 45.0 };
		constexpr double BackRowCm = 700.0;
		constexpr double BackRowSpacingCm = 380.0;

		bool IsRaceOn(ETNRallyPhase Phase)
		{
			return Phase == ETNRallyPhase::Racing || Phase == ETNRallyPhase::Finishing;
		}
	}

	using namespace CameraDetail;

	EShot DecideShot(const FShotInput& In)
	{
		if (In.Phase == ETNRallyPhase::Results)
		{
			return EShot::Podium;
		}
		if (!IsRaceOn(In.Phase))
		{
			// Antes de la salida sin buggy (espera del mapa en Karts): a un buggy de la parrilla, no a la vista del mando sin
			// peón, que en el mapa generado queda bajo el terreno (#756).
			return In.bSeated ? EShot::Own : EShot::Spectate;
		}
		if (!In.bSeated)
		{
			return EShot::Spectate;
		}
		if (!In.bFinished)
		{
			return EShot::Own;
		}
		const double Since = In.SecondsSinceFinish;
		if (Since < 0.0)
		{
			return EShot::Spectate;
		}
		// Con VR, corte seco al podio fijo: ni plano lateral ni cámara lenta.
		const double SideSeconds = In.bVR ? 0.0 : FinishSideSeconds;
		if (Since < SideSeconds)
		{
			return EShot::FinishSide;
		}
		return Since < SideSeconds + PodiumHoldSeconds ? EShot::Podium : EShot::Spectate;
	}

	bool CanUseSlowMotion(bool bVR, bool bStandalone, int32 HumanPlayers)
	{
		return !bVR && (bStandalone || HumanPlayers <= 1);
	}

	int32 CycleSpectate(int32 Current, int32 Delta, int32 NumRacing, bool bAllowDrone)
	{
		const int32 Slots = NumRacing + (bAllowDrone ? 1 : 0);
		if (Slots <= 0)
		{
			return INDEX_NONE;
		}
		const int32 From = (Current >= 0 && Current < Slots) ? Current : 0;
		return ((From + Delta) % Slots + Slots) % Slots;
	}

	namespace CameraDetail
	{
		FSpectatePick PickSlot(TConstArrayView<int32> RacingTeams, int32 Slot)
		{
			FSpectatePick Pick;
			Pick.Slot = Slot;
			Pick.bDrone = Slot == RacingTeams.Num();
			Pick.Team = RacingTeams.IsValidIndex(Slot) ? RacingTeams[Slot] : INDEX_NONE;
			return Pick;
		}
	}

	FSpectatePick FollowSpectate(TConstArrayView<int32> RacingTeams, const FSpectatePick& Current, bool bAllowDrone)
	{
		const int32 NumRacing = RacingTeams.Num();
		// Sin nadie corriendo no hay buggy ni líder para el dron.
		if (NumRacing == 0)
		{
			return FSpectatePick();
		}
		if (Current.bDrone && bAllowDrone)
		{
			return PickSlot(RacingTeams, NumRacing);
		}
		const int32 TeamSlot = Current.Team != INDEX_NONE ? RacingTeams.IndexOfByKey(Current.Team) : INDEX_NONE;
		if (TeamSlot != INDEX_NONE)
		{
			return PickSlot(RacingTeams, TeamSlot);
		}
		return PickSlot(RacingTeams, RacingTeams.IsValidIndex(Current.Slot) ? Current.Slot : 0);
	}

	FSpectatePick StepSpectate(TConstArrayView<int32> RacingTeams, const FSpectatePick& Current, int32 Delta, bool bAllowDrone)
	{
		const FSpectatePick From = FollowSpectate(RacingTeams, Current, bAllowDrone);
		if (!From.HasTarget())
		{
			return From;
		}
		return PickSlot(RacingTeams, CycleSpectate(From.Slot, Delta, RacingTeams.Num(), bAllowDrone));
	}

	FVector FinishSideLocation(const FVector& BuggyLocation, const FVector& BuggyForward)
	{
		const FVector Forward = BuggyForward.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : BuggyForward.GetSafeNormal2D();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		return BuggyLocation + Forward * SideAheadCm + Right * SideLateralCm + FVector::UpVector * SideUpCm;
	}

	FVector DroneLocation(const FVector& LeaderLocation, const FVector& LeaderForward)
	{
		const FVector Forward = LeaderForward.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : LeaderForward.GetSafeNormal2D();
		return LeaderLocation - Forward * DroneBehindCm + FVector::UpVector * DroneUpCm;
	}

	FVector SmoothFollow(const FVector& Current, const FVector& Target, float DeltaSeconds, float Speed)
	{
		if (FVector::DistSquared(Current, Target) > FMath::Square(FollowSnapCm))
		{
			return Target;
		}
		const double Alpha = 1.0 - FMath::Exp(-static_cast<double>(FMath::Max(Speed, 0.f)) * FMath::Max(DeltaSeconds, 0.f));
		return FMath::Lerp(Current, Target, Alpha);
	}

	FPodiumFrame PodiumFrameFromFinish(const FVector& FinishCenter, const FVector& RaceForward)
	{
		FPodiumFrame Frame;
		const FVector Forward = RaceForward.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : RaceForward.GetSafeNormal2D();
		// Los buggies del podio miran hacia el tramo por el que llegan (contra el sentido de la carrera).
		Frame.Facing = -Forward;
		Frame.Right = FVector::CrossProduct(FVector::UpVector, Frame.Facing);
		Frame.Origin = FinishCenter + FVector::UpVector * PodiumHeightCm;
		return Frame;
	}

	double PodiumStepHeightCm(int32 FinishOrder)
	{
		return (FinishOrder >= 1 && FinishOrder <= 3) ? StepHeights[FinishOrder - 1] : 0.0;
	}

	FVector PodiumSlotLocation(const FPodiumFrame& Frame, int32 FinishOrder)
	{
		if (FinishOrder >= 1 && FinishOrder <= 3)
		{
			// 2.º a la izquierda y 3.º a la derecha del 1.º, mirados desde la cámara.
			const double Side = FinishOrder == 1 ? 0.0 : (FinishOrder == 2 ? 1.0 : -1.0);
			return Frame.Origin + Frame.Right * (Side * PodiumStepWidthCm) + FVector::UpVector * PodiumStepHeightCm(FinishOrder);
		}
		// Del 4.º en adelante, en fila detrás de los escalones.
		const int32 Back = FMath::Max(0, FinishOrder - 4);
		const double Lateral = (Back % 5 - 2) * BackRowSpacingCm;
		const double Depth = BackRowCm + (Back / 5) * BackRowCm * 0.8;
		return Frame.Origin - Frame.Facing * Depth + Frame.Right * Lateral;
	}

	FVector PodiumCameraLocation(const FPodiumFrame& Frame, bool bVR, double TimeSeconds)
	{
		const double Angle = bVR ? 0.0 : FMath::DegreesToRadians(PodiumOrbitDeg) * FMath::Sin(TimeSeconds * PodiumOrbitSpeed);
		const FVector Out = Frame.Facing * FMath::Cos(Angle) + Frame.Right * FMath::Sin(Angle);
		return Frame.Origin + Out * PodiumCameraDistanceCm + FVector::UpVector * PodiumCameraUpCm;
	}

	FVector PodiumLookAt(const FPodiumFrame& Frame)
	{
		return Frame.Origin + FVector::UpVector * (StepHeights[0] + 60.0);
	}
}
