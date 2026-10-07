#pragma once

#include "CoreMinimal.h"

/**
 * Calibración de la cabeza con gafas (#916, Docs/Modo_VR.md): el motor da la pose de las gafas respecto del origen del
 * seguimiento, y ese origen no tiene por qué estar donde está la cabeza (el recentrado del arranque puede no haberse hecho
 * todavía, el jugador se ha movido o se ha quitado y puesto las gafas). Sin calibrar, la cámara salía a un metro de los ojos
 * de la tortuga. Aquí se guarda la posición de las gafas en el momento de calibrar y el origen se desplaza para que esa
 * posición caiga en los ojos de la tortuga; lo que la cabeza se mueva después sí se ve.
 *
 * Lógica pura (sin motor): la prueba Tortunabo.VR.HeadCalibration la comprueba sin gafas.
 */
struct FTNVRHeadCalibration
{
	/** Fotogramas que se esperan tras pedir la calibración (un recentrado tarda en notarse en la pose). */
	static constexpr int32 SettleFrames = 3;

	/** Lo más que se espera a que se pongan las gafas antes de calibrar de todos modos (segundos). */
	static constexpr float MaxWaitSeconds = 3.f;

	/** Posición de las gafas (espacio del seguimiento) cuando se calibró. */
	FVector Base = FVector::ZeroVector;

	bool bValid = false;
	int32 FramesLeft = SettleFrames;
	float Waited = 0.f;

	/** Pide calibrar de nuevo (recentrar, ponerse las gafas, volver a aparecer). Hasta entonces se mantiene la anterior. */
	void Request()
	{
		bValid = false;
		FramesLeft = SettleFrames;
		Waited = 0.f;
	}

	/** Olvida la calibración y pide otra: la posición vuelve a contar entera hasta que se mida. */
	void Reset()
	{
		Base = FVector::ZeroVector;
		Request();
	}

	/**
	 * Un fotograma con la posición de las gafas. Calibra pasados SettleFrames fotogramas, con las gafas puestas (o, si el
	 * dispositivo no lo dice, tras MaxWaitSeconds). true si acaba de calibrar.
	 */
	bool Update(const FVector& HmdPosition, bool bHmdWorn, float DeltaSeconds)
	{
		if (bValid)
		{
			return false;
		}
		Waited += FMath::Max(0.f, DeltaSeconds);
		if (FramesLeft > 0)
		{
			--FramesLeft;
			return false;
		}
		if (!bHmdWorn && Waited < MaxWaitSeconds)
		{
			return false;
		}
		Base = HmdPosition;
		bValid = true;
		return true;
	}

	/**
	 * Lo que hay que restar a los ojos (mundo) para poner el origen del seguimiento: con el origen girado OriginYaw, la
	 * cámara está en origen + giro(OriginYaw) * pose, así que origen = ojos - giro(OriginYaw) * Base deja la cámara en los
	 * ojos con la cabeza en su posición de calibrar.
	 */
	FVector OriginShift(double OriginYaw) const
	{
		return FRotator(0.0, OriginYaw, 0.0).RotateVector(Base);
	}
};
