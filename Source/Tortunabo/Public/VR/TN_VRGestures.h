#pragma once

#include "CoreMinimal.h"

/**
 * Gestos físicos con las gafas (#918, Docs/Modo_VR.md, «Gestos»), sin mundo ni actores: la cuenta de cada uno la usan
 * ATN_VRRig y ATortugaCharacter y la prueba Tortunabo.VR.Gestures. Los botones de siempre siguen funcionando.
 */
namespace TNVRGestures
{
	// ── Guantazo: la mano derecha de lado a lado ─────────────────────────────────

	/** Velocidad lateral de la mano (respecto del cuerpo, cm/s) a partir de la cual cuenta como guantazo. */
	constexpr float SlapMinSpeed = 420.f;
	/** Cuánto de la velocidad tiene que ser de lado (0..1): un puñetazo hacia delante o un lanzamiento no son un guantazo. */
	constexpr float SlapLateralRatio = 0.7f;
	/** Segundos entre dos guantazos por gesto (el componente del guantazo tiene su propio enfriamiento). */
	constexpr float SlapCooldownSeconds = 0.8f;

	/** Parte de la velocidad de la mano que va hacia la derecha del cuerpo (cm/s; negativa, hacia la izquierda). */
	inline float LateralSpeed(const FVector& HandVelocity, const FVector& BodyRight)
	{
		return static_cast<float>(FVector::DotProduct(HandVelocity, BodyRight.GetSafeNormal()));
	}

	/** ¿Va esta velocidad de la mano (respecto del cuerpo) de lado a lado y lo bastante deprisa para ser un guantazo? */
	inline bool IsSlapSwipe(const FVector& HandVelocity, const FVector& BodyRight, float MinSpeed = SlapMinSpeed, float LateralRatio = SlapLateralRatio)
	{
		const float Lateral = FMath::Abs(LateralSpeed(HandVelocity, BodyRight));
		const float Speed = static_cast<float>(HandVelocity.Size());
		return Lateral >= MinSpeed && Lateral >= Speed * LateralRatio;
	}

	/** Detector del guantazo por gesto: el gesto dispara una vez y no vuelve a hacerlo hasta pasar el enfriamiento. */
	struct FSlapGesture
	{
		float Cooldown = 0.f;

		void Reset() { Cooldown = 0.f; }

		/** Un fotograma con la velocidad de la mano. true si acaba de empezar un guantazo. */
		bool Step(const FVector& HandVelocity, const FVector& BodyRight, float DeltaSeconds)
		{
			Cooldown = FMath::Max(0.f, Cooldown - FMath::Max(0.f, DeltaSeconds));
			if (Cooldown > 0.f || !IsSlapSwipe(HandVelocity, BodyRight))
			{
				return false;
			}
			Cooldown = SlapCooldownSeconds;
			return true;
		}
	};

	// ── Caparazón: agachar la cabeza ────────────────────────────────────────────

	/** Lo que baja la cabeza (cm) desde su altura de siempre para meterse (o salir) del caparazón. */
	constexpr float DuckDropCm = 28.f;
	/** Cuánto hay que volver a subir (fracción del descenso) para poder agacharse otra vez. */
	constexpr float DuckRearmFraction = 0.4f;
	/** Lo que baja por segundo la altura de referencia (cm/s): quien juega sentado o se encoge poco a poco no la dispara. */
	constexpr float DuckReferenceDecayCmPerSec = 4.f;
	/** Un salto de la altura mayor que esto en un fotograma (recentrar, recalibrar) no cuenta como agacharse (cm). */
	constexpr float DuckMaxFrameJumpCm = 60.f;
	/** Segundos mínimos entre dos agachadas. */
	constexpr float DuckCooldownSeconds = 1.2f;

	/**
	 * Agacharse con la cabeza (altura de las gafas respecto del seguimiento): la altura de referencia sube con la cabeza al
	 * momento y baja despacio; al quedar la cabeza DuckDropCm por debajo salta una vez y se rearma al volver a subir.
	 */
	struct FDuckGesture
	{
		float Reference = 0.f;
		float LastHeight = 0.f;
		float Cooldown = 0.f;
		bool bStarted = false;
		bool bLatched = false;

		void Reset() { *this = FDuckGesture(); }

		/** Un fotograma con la altura de la cabeza (cm). true si acaba de agacharse. */
		bool Step(float Height, float DeltaSeconds)
		{
			const float Dt = FMath::Max(0.f, DeltaSeconds);
			Cooldown = FMath::Max(0.f, Cooldown - Dt);
			if (!bStarted || FMath::Abs(Height - LastHeight) > DuckMaxFrameJumpCm)
			{
				// Primera medida o un salto de la pose (recentrar): se vuelve a medir desde aquí, sin disparar.
				Reference = Height;
				bStarted = true;
				bLatched = false;
				LastHeight = Height;
				return false;
			}
			LastHeight = Height;
			Reference = FMath::Max(Height, Reference - DuckReferenceDecayCmPerSec * Dt);
			const float Drop = Reference - Height;
			if (bLatched)
			{
				if (Drop < DuckDropCm * DuckRearmFraction)
				{
					bLatched = false;
				}
				return false;
			}
			if (Drop >= DuckDropCm && Cooldown <= 0.f)
			{
				bLatched = true;
				Cooldown = DuckCooldownSeconds;
				return true;
			}
			return false;
		}
	};

	// ── Cabeza adelantada ───────────────────────────────────────────────────────

	/**
	 * Cuello y cabeza de la tortuga con gafas (grados sobre el eje X de la malla): el cuello se echa hacia delante (negativo
	 * = hacia delante) y la cabeza se endereza para seguir mirando al frente. Con esto los ojos quedan unos 24 cm delante
	 * del centro de la cápsula (ATortugaCharacter::VREyeOffset) y la lengua ~10 cm delante y ~10 cm debajo de ellos.
	 */
	constexpr float NeckForwardDeg = -14.f;
	constexpr float HeadLevelDeg = 10.f;
}
