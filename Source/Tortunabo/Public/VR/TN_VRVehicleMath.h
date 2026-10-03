#pragma once

#include "CoreMinimal.h"
#include "Vehicles/TN_RallyTurretLogic.h"

/**
 * Cuentas de los vehículos con gafas (Docs/Modo_VR.md, «Vehículos»), sin mundo ni actores: el volante que se gira con las
 * manos, el apuntado de la torreta con la mano y la inclinación de la artillera con la cabeza. Las prueban
 * Tortunabo.VR.Vehicle.*.
 */
namespace TNVRVehicle
{
	// ── Volante ─────────────────────────────────────────────────────────────────

	/** Volante en los ejes del vehículo (X delante, Y derecha, Z arriba). */
	struct FWheelFrame
	{
		FVector Center = FVector::ZeroVector;
		/** Eje del volante, hacia la conductora. */
		FVector Axis = FVector(-1.0, 0.0, 0.0);
		/** Las 12 del volante recto (perpendicular a Axis). */
		FVector Up = FVector(0.0, 0.0, 1.0);
		/** Radio del aro (cm). */
		double Radius = 16.0;

		/** Derecha de la conductora en el plano del volante (de las 9 a las 3). */
		FVector Right() const { return FVector::CrossProduct(Axis, Up).GetSafeNormal(); }
	};

	/** Ajuste del volante. */
	struct FWheelTuning
	{
		/** Giro máximo del volante a cada lado (grados): ahí la dirección llega a tope. */
		double MaxWheelDeg = 90.0;
		/** Vuelta al centro sin manos (grados por segundo). */
		double ReturnDegPerSec = 270.0;
		/** Suavizado de la dirección que sale (1/s). */
		float SmoothRate = 14.f;
		/** Una mano más cerca del centro que esto (cm) no da ángulo (se espera a que se aparte). */
		double MinHandRadius = 3.0;
	};

	/** Estado del volante entre fotogramas. */
	struct FWheelState
	{
		/** Giro del volante (grados, + a la derecha). */
		double WheelDeg = 0.0;
		/** Ángulo de las manos del fotograma anterior (el que se sigue). */
		double LastHandDeg = 0.0;
		/** Manos en el volante el fotograma anterior (1 izquierda, 2 derecha). */
		uint8 Mask = 0;
		/** Si LastHandDeg vale (se acaba de coger o soltar una mano: se vuelve a medir desde ahí, sin salto). */
		bool bAnchored = false;
		/** Dirección pedida (-1..1, + a la derecha), suavizada. */
		float Steer = 0.f;
	};

	/** Coordenadas de un punto en el plano del volante: X a la derecha de la conductora e Y hacia las 12. */
	inline FVector2D PlanePoint(const FVector& Point, const FWheelFrame& W)
	{
		const FVector V = Point - W.Center;
		return FVector2D(FVector::DotProduct(V, W.Right()), FVector::DotProduct(V, W.Up.GetSafeNormal()));
	}

	/**
	 * Ángulo (grados) de una mano alrededor del volante, como un reloj visto desde el asiento: 0 arriba, +90 a la derecha,
	 * -90 a la izquierda. bOutValid = false si la mano está casi en el centro (sin ángulo fiable).
	 */
	inline double HandAngleDeg(const FVector& Hand, const FWheelFrame& W, double MinRadius, bool& bOutValid)
	{
		const FVector2D P = PlanePoint(Hand, W);
		bOutValid = P.Size() >= MinRadius;
		return bOutValid ? FMath::RadiansToDegrees(FMath::Atan2(P.X, P.Y)) : 0.0;
	}

	/**
	 * Ángulo (grados) de la recta de la mano izquierda a la derecha en el plano del volante: 0 con las manos a las 9 y a las
	 * 3; girar a la derecha (la izquierda sube y la derecha baja) lo hace positivo. bOutValid = false con las manos juntas.
	 */
	inline double TwoHandAngleDeg(const FVector& Left, const FVector& Right, const FWheelFrame& W, double MinSeparation, bool& bOutValid)
	{
		const FVector2D D = PlanePoint(Right, W) - PlanePoint(Left, W);
		bOutValid = D.Size() >= MinSeparation;
		return bOutValid ? FMath::RadiansToDegrees(FMath::Atan2(-D.Y, D.X)) : 0.0;
	}

	/** Dirección (-1..1) que pide el volante girado WheelDeg con el tope en MaxWheelDeg. */
	inline float SteerFromWheel(double WheelDeg, double MaxWheelDeg)
	{
		return static_cast<float>(FMath::Clamp(WheelDeg / FMath::Max(1.0, MaxWheelDeg), -1.0, 1.0));
	}

	/**
	 * Un fotograma del volante. Con una mano sigue su ángulo alrededor del centro; con las dos, el de la recta entre ellas.
	 * Al coger o soltar una mano se vuelve a medir desde donde está (el volante no salta), el giro se acumula (pasar de
	 * ±180 no da la vuelta) y se queda en ±MaxWheelDeg; sin manos vuelve al centro. La dirección sale suavizada.
	 */
	inline FWheelState StepWheel(const FWheelState& In, bool bLeft, bool bRight, const FVector& LeftHand, const FVector& RightHand,
		const FWheelFrame& W, float DeltaSeconds, const FWheelTuning& T = FWheelTuning())
	{
		FWheelState S = In;
		const uint8 Mask = static_cast<uint8>((bLeft ? 1 : 0) | (bRight ? 2 : 0));
		const double Dt = FMath::Max(0.0, static_cast<double>(DeltaSeconds));
		if (Mask == 0)
		{
			const double Step = T.ReturnDegPerSec * Dt;
			S.WheelDeg = FMath::Abs(S.WheelDeg) <= Step ? 0.0 : S.WheelDeg - FMath::Sign(S.WheelDeg) * Step;
			S.bAnchored = false;
		}
		else
		{
			bool bValid = false;
			const double HandDeg = Mask == 3 ? TwoHandAngleDeg(LeftHand, RightHand, W, 2.0 * T.MinHandRadius, bValid)
				: HandAngleDeg(Mask == 1 ? LeftHand : RightHand, W, T.MinHandRadius, bValid);
			if (!bValid)
			{
				S.bAnchored = false;
			}
			else if (!In.bAnchored || In.Mask != Mask)
			{
				S.LastHandDeg = HandDeg;
				S.bAnchored = true;
			}
			else
			{
				const double Delta = FMath::UnwindDegrees(HandDeg - In.LastHandDeg);
				S.WheelDeg = FMath::Clamp(In.WheelDeg + Delta, -T.MaxWheelDeg, T.MaxWheelDeg);
				S.LastHandDeg = HandDeg;
			}
		}
		S.Mask = Mask;
		const float Target = SteerFromWheel(S.WheelDeg, T.MaxWheelDeg);
		S.Steer = Dt > 0.0 ? FMath::FInterpTo(In.Steer, Target, static_cast<float>(Dt), T.SmoothRate) : Target;
		return S;
	}

	/** Punto del aro más cerca de la mano (donde se ve agarrada): la mano llevada al plano y al radio del volante. */
	inline FVector RimPoint(const FVector& Hand, const FWheelFrame& W)
	{
		const FVector2D P = PlanePoint(Hand, W);
		const FVector2D Dir = P.Size() > 1e-3 ? P.GetSafeNormal() : FVector2D(0.0, 1.0);
		return W.Center + W.Right() * (Dir.X * W.Radius) + W.Up.GetSafeNormal() * (Dir.Y * W.Radius);
	}

	/** Distancia de la mano al aro (cm). */
	inline double DistanceToRim(const FVector& Hand, const FWheelFrame& W)
	{
		return FVector::Dist(Hand, RimPoint(Hand, W));
	}

	/** La mano izquierda y la derecha giradas WheelDeg desde las 9 y las 3 (para simular manos en pruebas). */
	inline void HandsOnWheel(const FWheelFrame& W, double WheelDeg, FVector& OutLeft, FVector& OutRight)
	{
		const double Rad = FMath::DegreesToRadians(WheelDeg);
		// Girar a la derecha: la mano de las 9 sube hacia las 12 y la de las 3 baja hacia las 6.
		const FVector Up = W.Up.GetSafeNormal();
		const FVector Right = W.Right();
		OutLeft = W.Center + (-Right * FMath::Cos(Rad) + Up * FMath::Sin(Rad)) * W.Radius;
		OutRight = W.Center + (Right * FMath::Cos(Rad) - Up * FMath::Sin(Rad)) * W.Radius;
	}

	// ── Torreta ─────────────────────────────────────────────────────────────────

	/**
	 * Hacia dónde apuntan las manos que agarran las asas: la media de las dos (o la de la única). Sin ninguna, o si se
	 * anulan, ZeroVector.
	 */
	inline FVector AverageAimDir(const FVector& Left, bool bLeft, const FVector& Right, bool bRight)
	{
		FVector Sum = FVector::ZeroVector;
		if (bLeft) { Sum += Left.GetSafeNormal(); }
		if (bRight) { Sum += Right.GetSafeNormal(); }
		if (Sum.SizeSquared() < 1e-4)
		{
			return bRight ? Right.GetSafeNormal() : FVector::ZeroVector;
		}
		return Sum.GetSafeNormal();
	}

	/**
	 * Apuntado de la torreta relativo al vehículo para una dirección en mundo (la de las manos): guiñada y cabeceo en los
	 * ejes del vehículo, limitados como siempre (TNRallyTurret::ClampAim: cabeceo de -10 a 45).
	 */
	inline FRotator AimFromHands(const FQuat& VehicleRotation, const FVector& AimDirWorld)
	{
		const FVector Local = VehicleRotation.Inverse().RotateVector(AimDirWorld.GetSafeNormal());
		if (Local.IsNearlyZero())
		{
			return FRotator::ZeroRotator;
		}
		const FRotator Rot = Local.Rotation();
		return TNRallyTurret::ClampAim(FRotator(Rot.Pitch, Rot.Yaw, 0.0));
	}

	// ── Inclinación de la artillera ──────────────────────────────────────────────

	/**
	 * Inclinación (-1..1, + a la derecha) por lo que la cabeza se aparta del centro del asiento hacia un lado (cm, + a la
	 * derecha): nada dentro de DeadZoneCm y a tope a FullCm.
	 */
	inline float LeanFromHead(double LateralCm, double DeadZoneCm = 4.0, double FullCm = 20.0)
	{
		const double Span = FMath::Max(1.0, FullCm - DeadZoneCm);
		const double Over = FMath::Max(0.0, FMath::Abs(LateralCm) - DeadZoneCm);
		return static_cast<float>(FMath::Sign(LateralCm) * FMath::Min(1.0, Over / Span));
	}

	/** El stick y la cabeza juntos (se suman y se limitan a -1..1). */
	inline float CombineLean(float Stick, float Head)
	{
		return FMath::Clamp(Stick + Head, -1.f, 1.f);
	}
}
