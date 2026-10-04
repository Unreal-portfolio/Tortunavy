// Conchas de la torreta (#304, #629): la concha y la concha teledirigida que dan las cajas «?» como munición especial
// (ETNRallyAmmo::Concha y ::ConchaGuiada) y que dispara la torreta del buggy (UTN_BuggyTurretComponent). Corren pegadas al
// suelo (ATN_KartShell). Lógica pura: blanco de la teledirigida, guiado y trompo del alcanzado. Tests en
// Tortunabo.Rally.ItemBox.*.
#pragma once

#include "CoreMinimal.h"

namespace TNKart
{
	/** Vida de la concha recta y de la teledirigida (s), radio de impacto (cm) y giro máximo de la teledirigida (grados por segundo). */
	inline constexpr float ShellLifeSeconds = 6.f;
	inline constexpr float HomingShellLifeSeconds = 12.f;
	inline constexpr float ShellHitRadiusCm = 260.f;
	inline constexpr float ShellTurnDegPerSecond = 150.f;
	/** Al trompear, el buggy conserva esto de su velocidad y gira sobre sí mismo (grados por segundo). */
	inline constexpr float SpinOutKeepSpeed = 0.35f;
	inline constexpr float SpinOutYawDegPerSecond = 420.f;

	/** Puesto al que va la concha teledirigida: el de justo delante; INDEX_NONE si va la primera (sale recta). */
	TORTUNABO_API int32 HomingTargetPlace(int32 Place);

	/**
	 * Nueva dirección de la concha teledirigida: gira de Current hacia ToTarget como mucho MaxTurnDeg (en el plano; la
	 * altura la pone el suelo). Unitaria.
	 */
	TORTUNABO_API FVector SteerShell(const FVector& Current, const FVector& ToTarget, float MaxTurnDeg);
}
