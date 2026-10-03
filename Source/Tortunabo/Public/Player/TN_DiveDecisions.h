#pragma once

#include "CoreMinimal.h"

/**
 * Lógica pura del panzazo (doble salto físico, plan maestro §3.5; Docs/Analisis/2026-09-29/G_doble_salto_fisico.md §2).
 * Sin mundo ni red: la usan UTN_TurtleMovementComponent y ATortugaCharacter dentro del movimiento, igual en el servidor y
 * en el cliente dueño (también al repetir movimientos), y la recorren las pruebas Tortunabo.Dive.*.
 *
 * - Pendiente (E9-01, #62): desde BellySlopeMinAngle, cuesta abajo, menos rozamiento y menos freno; el tiempo del arrastre
 *   no corre en bajada.
 */
namespace TNDiveLogic
{
	// ─────────────────────────────────────────────────────────────────────────
	// Arrastre en pendiente (#62)
	// ─────────────────────────────────────────────────────────────────────────

	/** Ajustes de la pendiente (UTN_TurtleMovementComponent, Belly Slide|Slope). */
	struct FBellySlopeParams
	{
		/** Multiplica la gravedad a lo largo de la cuesta (BellySlopeGravity por TN.Dive.Slope). */
		float SlopeGravity = 1.15f;
		/** Desde esta inclinación del suelo (grados) se sigue cayendo; 90 o más lo apaga (como antes). */
		float MinAngleDeg = 12.f;
		/** Rozamiento y freno por velocidad en bajada, multiplicados. */
		float FrictionScale = 0.3f;
		float DragScale = 0.4f;
		/** Tope de la velocidad arrastrándose (cm/s). */
		float MaxSpeed = 1000.f;
	};

	/** El suelo con normal N (unitaria) está inclinado MinAngleDeg o más: cuesta en la que se sigue cayendo. */
	inline bool IsFallSlope(const FVector& N, float MinAngleDeg)
	{
		if (MinAngleDeg >= 90.f)
		{
			return false;
		}
		return N.Z <= static_cast<double>(FMath::Cos(FMath::DegreesToRadians(FMath::Max(0.f, MinAngleDeg))));
	}

	/**
	 * Gravedad a lo largo del suelo, en horizontal (la velocidad andando es horizontal y el suelo la inclina al moverse). Con
	 * la normal N, la componente horizontal de g·senθ por el suelo es g·(Nx, Ny)·Nz, hacia abajo de la cuesta.
	 */
	inline FVector SlopeAcceleration(const FVector& N, float Gravity, float SlopeGravity)
	{
		return FVector(N.X, N.Y, 0.0) * static_cast<double>(Gravity * static_cast<float>(N.Z) * SlopeGravity);
	}

	/** Cuesta abajo: cuesta de MinAngleDeg o más y la velocidad horizontal V no sube por ella (parada también cuenta). */
	inline bool IsGoingDownhill(const FVector& N, const FVector& V, float MinAngleDeg)
	{
		return IsFallSlope(N, MinAngleDeg) && (N.X * V.X + N.Y * V.Y) >= 0.0;
	}

	/** El tiempo del arrastre (rampa de rozamiento y tope de BellyMaxSeconds) corre en llano y en subida, no en bajada. */
	inline bool ShouldAdvanceBellyTimer(const FVector& N, const FVector& V, float MinAngleDeg)
	{
		return !IsGoingDownhill(N, V, MinAngleDeg);
	}

	/** Lo que necesita un paso del arrastre sobre la tripa. */
	struct FBellyStepInput
	{
		/** Normal del suelo (unitaria; arriba si no hay suelo caminable). */
		FVector Normal = FVector::UpVector;
		/** Gravedad (cm/s², positiva). */
		float Gravity = 980.f;
		/** Rozamiento de la superficie (cm/s²), ya con la rampa del tiempo y TN.Dive.Friction. */
		float Friction = 800.f;
		/** Freno proporcional a la velocidad (1/s). */
		float Drag = 1.5f;
		FBellySlopeParams Slope;
	};

	/**
	 * Velocidad horizontal tras DeltaTime arrastrándose desde V0, en pasos de 1/60 s como mucho (igual a cualquier ritmo de
	 * fotogramas y al repetir movimientos). Sin la entrada del jugador: sobre la tripa no se dirige. Rozamiento seco con el
	 * peso que apoya (menos en cuesta) más freno por velocidad; cuesta abajo, los dos multiplicados por los de la pendiente.
	 * OutFriction (opcional): el rozamiento del último paso (cm/s², para TN.Dive.Debug).
	 */
	inline FVector IntegrateBellyVelocity(const FVector& V0, const FBellyStepInput& In, float DeltaTime, float* OutFriction = nullptr)
	{
		const FVector SlopeAccel = SlopeAcceleration(In.Normal, In.Gravity, In.Slope.SlopeGravity);
		const float NormalShare = FMath::Clamp(static_cast<float>(In.Normal.Z), 0.2f, 1.f);
		const float Friction = FMath::Max(0.f, In.Friction);
		const float Drag = FMath::Max(0.f, In.Drag);
		FVector V(V0.X, V0.Y, 0.0);
		float LastFriction = Friction * NormalShare;
		float Remaining = DeltaTime;
		constexpr float MaxStep = 1.f / 60.f;
		while (Remaining > UE_KINDA_SMALL_NUMBER)
		{
			const float H = FMath::Min(Remaining, MaxStep);
			Remaining -= H;
			const bool bDownhill = IsGoingDownhill(In.Normal, V, In.Slope.MinAngleDeg);
			const float StepFriction = Friction * NormalShare * (bDownhill ? In.Slope.FrictionScale : 1.f);
			const float StepDrag = Drag * (bDownhill ? In.Slope.DragScale : 1.f);
			LastFriction = StepFriction;
			V += SlopeAccel * static_cast<double>(H);
			const float Speed = static_cast<float>(V.Size());
			if (Speed <= UE_KINDA_SMALL_NUMBER)
			{
				V = FVector::ZeroVector;
				continue;
			}
			// En una cuesta suave el rozamiento puede más y se queda quieta.
			const float Loss = (StepFriction + StepDrag * Speed) * H;
			V = Loss >= Speed ? FVector::ZeroVector : V * static_cast<double>((Speed - Loss) / Speed);
		}
		if (OutFriction)
		{
			*OutFriction = LastFriction;
		}
		return V.GetClampedToMaxSize(static_cast<double>(In.Slope.MaxSpeed));
	}

	/**
	 * Velocidad a la que tiende cuesta abajo (cm/s): la gravedad a lo largo de la cuesta menos el rozamiento, entre el freno
	 * por velocidad. 0 si no es cuesta de caer o el rozamiento puede más. Por debajo de BellyStopSpeed no sigue cayendo de
	 * verdad (se arrastraría a paso de tortuga hasta el tope): se levanta como en llano.
	 */
	inline float DownhillTerminalSpeed(const FBellyStepInput& In)
	{
		if (!IsFallSlope(In.Normal, In.Slope.MinAngleDeg))
		{
			return 0.f;
		}
		const float Pull = static_cast<float>(SlopeAcceleration(In.Normal, In.Gravity, In.Slope.SlopeGravity).Size());
		const float NormalShare = FMath::Clamp(static_cast<float>(In.Normal.Z), 0.2f, 1.f);
		const float Net = Pull - FMath::Max(0.f, In.Friction) * NormalShare * In.Slope.FrictionScale;
		const float Drag = FMath::Max(0.f, In.Drag) * In.Slope.DragScale;
		if (Net <= 0.f)
		{
			return 0.f;
		}
		return Drag > UE_KINDA_SMALL_NUMBER ? FMath::Min(Net / Drag, In.Slope.MaxSpeed) : In.Slope.MaxSpeed;
	}

	/**
	 * Velocidad horizontal con que se arrastra al tocar el suelo de normal FloorNormal (cero: sin suelo): se quita lo que iba
	 * contra el suelo (el golpe), queda Keep de lo demás y como mucho Cap. En una bajada de MinAngleDeg o más, la caída que
	 * se convierte en arrastre cuenta entera (módulo 3D, no solo su parte horizontal), con tope DownhillCap.
	 */
	inline FVector LandingSlideVelocity(const FVector& V, const FVector& FloorNormal, float Keep, float Cap, float DownhillCap, float MinAngleDeg)
	{
		FVector Along = V;
		bool bDownhill = false;
		if (!FloorNormal.IsNearlyZero())
		{
			const FVector N = FloorNormal.GetSafeNormal();
			const double Into = FVector::DotProduct(Along, N);
			if (Into < 0.0)
			{
				Along -= N * Into;
			}
			bDownhill = IsFallSlope(N, MinAngleDeg) && Along.Z < 0.0;
		}
		const FVector Flat(Along.X, Along.Y, 0.0);
		const double Speed = bDownhill ? Along.Size() : Flat.Size();
		const double NewSpeed = FMath::Min(Speed * static_cast<double>(Keep), static_cast<double>(bDownhill ? DownhillCap : Cap));
		return Flat.GetSafeNormal() * NewSpeed;
	}
}
