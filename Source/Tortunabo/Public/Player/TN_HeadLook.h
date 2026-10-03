#pragma once

#include "CoreMinimal.h"

/**
 * Cuentas puras de la cabeza que sigue a la cámara en tercera persona (#623): cuánto gira la cabeza para una vista dada y
 * cómo viaja la guiñada de la vista por la red. Sin mundo ni actores; las usan ATortugaCharacter y UTN_TurtleAnimInstance
 * y las prueba Tortunabo.HeadLook.
 *
 * Ángulos en grados, respecto del cuerpo: guiñada positiva hacia la derecha de la tortuga y cabeceo positivo hacia arriba
 * (los del giro del mando, FRotator).
 */
namespace TNHeadLook
{
	/** Guiñada máxima de la cabeza a cada lado. */
	constexpr float MaxYaw = 70.f;
	/** Hasta aquí la cabeza se queda en el tope; más atrás empieza a volver al frente. */
	constexpr float HoldYaw = 95.f;
	/** Con la vista a partir de aquí (mirando hacia atrás), la cabeza está al frente. */
	constexpr float FrontYaw = 140.f;
	/** Cabeceo: hacia abajo y hacia arriba. */
	constexpr float MinPitch = -35.f;
	constexpr float MaxPitch = 45.f;
	/** Parte del giro que lleva el cuello; la cabeza, el resto. */
	constexpr float NeckShare = 0.4f;
	/** Muelle crítico que sigue a la vista: segundos de retraso (2 / tiempo = 10 por segundo). */
	constexpr float SmoothSeconds = 0.2f;
	/** Pasos del byte (de 360/256 = 1,4°) que tiene que moverse la guiñada para que el servidor la vuelva a mandar. */
	constexpr int32 SendSteps = 2;

	/** Giro de la cabeza respecto del cuerpo. */
	struct FAngles
	{
		float Yaw = 0.f;
		float Pitch = 0.f;
	};

	/**
	 * Cuánto de la vista sigue la cabeza según lo atrás que mire (0..1): toda hasta HoldYaw, nada desde FrontYaw y una curva
	 * suave entre medias. Con la vista justo detrás (±180°) vale 0 por los dos lados: la cabeza no salta de uno a otro.
	 */
	inline float FrontBlend(float ViewYaw)
	{
		const float Behind = FMath::Abs(FRotator3f::NormalizeAxis(ViewYaw));
		if (Behind <= HoldYaw) { return 1.f; }
		if (Behind >= FrontYaw) { return 0.f; }
		const float X = (Behind - HoldYaw) / (FrontYaw - HoldYaw);
		return 1.f - X * X * (3.f - 2.f * X);
	}

	/**
	 * Giro de la cabeza para una vista ViewYaw/ViewPitch respecto del cuerpo: la sigue hasta ±MaxYaw y entre MinPitch y
	 * MaxPitch; mirando hacia atrás vuelve al frente sin saltos (FrontBlend, también para el cabeceo).
	 */
	inline FAngles Target(float ViewYaw, float ViewPitch)
	{
		const float Yaw = FRotator3f::NormalizeAxis(ViewYaw);
		const float Blend = FrontBlend(Yaw);
		FAngles Out;
		Out.Yaw = FMath::Clamp(Yaw, -MaxYaw, MaxYaw) * Blend;
		Out.Pitch = FMath::Clamp(FRotator3f::NormalizeAxis(ViewPitch), MinPitch, MaxPitch) * Blend;
		return Out;
	}

	/** Guiñada de la vista (-180..180) en un byte, en pasos de 360/256 grados (como FRotator::CompressAxisToByte). */
	inline uint8 EncodeYaw(float ViewYaw)
	{
		return FRotator3f::CompressAxisToByte(FRotator3f::NormalizeAxis(ViewYaw));
	}

	/** De vuelta a grados (-180..180]. */
	inline float DecodeYaw(uint8 Byte)
	{
		return FRotator3f::NormalizeAxis(FRotator3f::DecompressAxisFromByte(Byte));
	}

	/** ¿Se ha movido la guiñada lo bastante (SendSteps pasos o más, por el lado corto) para volver a mandarla? */
	inline bool ShouldSend(uint8 Sent, uint8 Now)
	{
		const int32 Steps = static_cast<int8>(static_cast<uint8>(Now - Sent));
		return FMath::Abs(Steps) >= SendSteps;
	}
}
