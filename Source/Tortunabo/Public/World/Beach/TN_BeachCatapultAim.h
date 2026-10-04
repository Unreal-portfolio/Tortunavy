#pragma once

#include "CoreMinimal.h"

/**
 * Puntería de la catapulta de playa (ATN_BeachCatapult): con qué rapidez sale la bola de caparazón para caer donde acaba el
 * arco de conchitas que dibuja su vuelo (UTN_BeachLootSubsystem). La bola frena en el aire (amortiguación lineal de su caja,
 * ATN_ShellBody): con la rapidez de un tiro sin rozamiento se quedaba en poco más de la mitad del arco (#257). Lógica pura;
 * tests Tortunabo.Beach.CatapultAim.
 */
namespace TNBeachCatapultAim
{
	/**
	 * Amortiguación lineal (1/s) de la caja de la bola de caparazón: la misma que pone ATN_ShellBody en su caja (lo comprueba
	 * Tortunabo.Beach.CatapultAim con el objeto por defecto de ATN_ShellBody).
	 */
	inline constexpr float ShellBallLinearDamping = 0.25f;

	/**
	 * Distancia horizontal (cm) a la que vuelve a bajar a la altura Height (cm, respecto a la salida) un tiro con rapidez Speed
	 * (cm/s) y elevación PitchRad, con gravedad Gravity (cm/s², positiva) y amortiguación Damping (1/s). Con amortiguación,
	 * v(t) = v0·e^(-k·t) más la gravedad: x(t) = vx/k·(1 - e^(-k·t)), z(t) = (vz + g/k)/k·(1 - e^(-k·t)) - g·t/k.
	 * -1 si no llega a esa altura (tiro demasiado flojo para subir hasta ella).
	 */
	inline double LandingDistance(double Speed, double PitchRad, double Height, double Damping, double Gravity)
	{
		const double Vx = Speed * FMath::Cos(PitchRad);
		const double Vz = Speed * FMath::Sin(PitchRad);
		const auto Position = [&](double T, double& OutX, double& OutZ)
		{
			if (Damping < 1e-4)
			{
				OutX = Vx * T;
				OutZ = Vz * T - 0.5 * Gravity * T * T;
				return;
			}
			const double Decay = 1.0 - FMath::Exp(-Damping * T);
			OutX = Vx / Damping * Decay;
			OutZ = (Vz + Gravity / Damping) / Damping * Decay - Gravity * T / Damping;
		};
		// Hacia delante en pasos de 10 ms hasta que, ya bajando, cruza la altura; luego se afina entre los dos pasos.
		constexpr double Step = 0.01;
		constexpr double MaxTime = 30.0;
		double PrevX = 0.0;
		double PrevZ = 0.0;
		bool bRose = false;
		for (double T = Step; T <= MaxTime; T += Step)
		{
			double X = 0.0;
			double Z = 0.0;
			Position(T, X, Z);
			bRose |= Z > Height;
			if (bRose && Z <= Height)
			{
				const double Alpha = (PrevZ - Height) / FMath::Max(PrevZ - Z, 1e-6);
				return FMath::Lerp(PrevX, X, Alpha);
			}
			PrevX = X;
			PrevZ = Z;
		}
		return -1.0;
	}

	/**
	 * Rapidez (cm/s) con la que un tiro a PitchRad cae a Distance cm por delante y Height cm por encima (negativo: por
	 * debajo) de la salida. Se busca entre MinSpeed y MaxSpeed; si no se alcanza ni con MaxSpeed, MaxSpeed.
	 */
	inline double SpeedToReach(double Distance, double Height, double PitchRad, double Damping, double Gravity, double MinSpeed, double MaxSpeed)
	{
		double Low = MinSpeed;
		double High = MaxSpeed;
		if (LandingDistance(High, PitchRad, Height, Damping, Gravity) < Distance)
		{
			return MaxSpeed;
		}
		for (int32 Iteration = 0; Iteration < 40; ++Iteration)
		{
			const double Mid = 0.5 * (Low + High);
			if (LandingDistance(Mid, PitchRad, Height, Damping, Gravity) < Distance)
			{
				Low = Mid;
			}
			else
			{
				High = Mid;
			}
		}
		return High;
	}
}
