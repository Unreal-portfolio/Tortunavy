#pragma once

#include "CoreMinimal.h"

/**
 * Arco de un lanzamiento al punto de mira (ATortugaCharacter::GetThrowDirectionToCrosshair): lógica pura, sin mundo. La
 * usa el personaje y la prueban los tests de Tortunabo.ThrowArc, así cubren el código real y no una copia.
 */
namespace TNThrowArc
{
	/**
	 * Altura (cm) a la que llega, sobre el punto de salida, un lanzamiento con velocidad Speed (cm/s) y ángulo Theta (rad)
	 * al llegar a Dist (cm) en horizontal, con gravedad G (cm/s²) y amortiguación lineal Damping (1/s; la de la caja de
	 * la concha frena un poco en el aire). Falso si con ese ángulo no llega a Dist.
	 */
	inline bool HeightAtDistance(double Dist, double Speed, double Theta, double G, double Damping, double& OutHeight)
	{
		const double Cos = FMath::Cos(Theta);
		const double Sin = FMath::Sin(Theta);
		if (Cos <= KINDA_SMALL_NUMBER)
		{
			return false;
		}
		if (Damping < 0.01)
		{
			const double T = Dist / (Speed * Cos);
			OutHeight = Speed * Sin * T - 0.5 * G * T * T;
			return true;
		}
		// x(t) = v·cosθ/C·(1 - e^-Ct), z(t) = (v·sinθ + G/C)/C·(1 - e^-Ct) - G·t/C (la misma cuenta que BallisticLaunch).
		const double Ratio = Damping * Dist / (Speed * Cos);
		if (Ratio >= 1.0)
		{
			return false;
		}
		const double T = -FMath::Loge(1.0 - Ratio) / Damping;
		const double E = (1.0 - FMath::Exp(-Damping * T)) / Damping;
		OutHeight = (Speed * Sin + G / Damping) * E - G * T / Damping;
		return true;
	}

	/**
	 * Ángulo (rad) del arco bajo que, saliendo a Speed con gravedad G y amortiguación Damping, llega a un punto a Dist
	 * en horizontal y DeltaZ de altura. Sin alcance (punto demasiado lejos): el ángulo de máximo alcance, o 45° con
	 * amortiguación.
	 */
	inline double LaunchPitch(double Dist, double DeltaZ, double Speed, double G, double Damping)
	{
		if (Damping < 0.01)
		{
			const double V2 = Speed * Speed;
			const double Disc = V2 * V2 - G * (G * Dist * Dist + 2.0 * DeltaZ * V2);
			return FMath::Atan(Disc >= 0.0 ? (V2 - FMath::Sqrt(Disc)) / (G * Dist) : 1.0);
		}
		// Con amortiguación no hay fórmula cerrada: se barre de abajo arriba el primer ángulo que llega a la altura (arco
		// bajo) y se afina por interpolación.
		constexpr double StepRad = PI / 360.0;
		constexpr double MinRad = -PI / 3.0;
		constexpr double MaxRad = PI / 4.0;
		double PrevTheta = MinRad;
		double PrevErr = 0.0;
		bool bHavePrev = false;
		for (double Theta = MinRad; Theta <= MaxRad; Theta += StepRad)
		{
			double Height = 0.0;
			if (!HeightAtDistance(Dist, Speed, Theta, G, Damping, Height))
			{
				bHavePrev = false;
				continue;
			}
			const double Err = Height - DeltaZ;
			if (Err >= 0.0)
			{
				if (!bHavePrev || Err <= KINDA_SMALL_NUMBER)
				{
					return Theta;
				}
				return PrevTheta + (Theta - PrevTheta) * (-PrevErr) / (Err - PrevErr);
			}
			PrevTheta = Theta;
			PrevErr = Err;
			bHavePrev = true;
		}
		return PI / 4.0;
	}
}
