#pragma once

#include "CoreMinimal.h"

/**
 * Punto de mira que manda el dueño con un lanzamiento (#894): lógica pura, sin mundo. El cliente calcula el punto del
 * centro de la pantalla con su cámara real (la del servidor no tiene los ajustes locales: subida sobre el suelo, brazo al
 * esprintar) y el servidor solo lo acepta si cae dentro de un cono alrededor de la rotación de control y del alcance del
 * rayo de mira. Si no, se usa el cálculo del servidor de siempre. La prueban los tests de Tortunabo.Throw.ClientAim.
 */
namespace TNThrowAim
{
	/** Alcance del rayo de mira (ATortugaCharacter::GetCrosshairPoint). */
	inline constexpr float AimRangeCm = 8000.f;

	/** Holgura del alcance: la cámara del cliente no está donde la del servidor. */
	inline constexpr float AimRangeSlackCm = 600.f;

	/** Ángulo máximo entre la rotación de control y la dirección al punto de mira, medido desde la cámara del servidor. */
	inline constexpr float MaxAngleDeg = 30.f;

	/**
	 * Verdadero si AimPoint es un punto de mira creíble para quien mira desde ViewOrigin en la dirección ViewDir: sin NaN,
	 * a menos de MaxRange y a no más de MaxAngle grados de ViewDir. Un punto pegado al origen no da dirección y se rechaza.
	 */
	inline bool IsClientAimPointValid(const FVector& ViewOrigin, const FVector& ViewDir, const FVector& AimPoint,
		float MaxAngle = MaxAngleDeg, float MaxRange = AimRangeCm + AimRangeSlackCm)
	{
		if (AimPoint.ContainsNaN() || ViewOrigin.ContainsNaN() || ViewDir.ContainsNaN())
		{
			return false;
		}
		const FVector Dir = ViewDir.GetSafeNormal();
		const FVector Delta = AimPoint - ViewOrigin;
		const double Dist = Delta.Size();
		if (Dir.IsNearlyZero() || Dist < 1.0 || Dist > MaxRange)
		{
			return false;
		}
		const double CosAngle = FVector::DotProduct(Delta / Dist, Dir);
		return CosAngle >= FMath::Cos(FMath::DegreesToRadians(static_cast<double>(MaxAngle)));
	}
}
