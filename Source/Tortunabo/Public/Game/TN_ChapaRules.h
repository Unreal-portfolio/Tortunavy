#pragma once

#include "CoreMinimal.h"

/**
 * Economía de la partida (plan maestro del modo único, §1 decisiones 6 y 10, y §4): la chapa, la máquina expendedora y revivir
 * pagando. Lógica pura, sin mundo ni red: la usan UTN_InventoryComponent (el contador de chapas), ATN_Chapa (el vuelo de
 * frisbee), ATN_VendingMachine (crédito y compra) y ATN_RescuePickup (revivir con chapas), y la prueban los tests
 * Tortunabo.Economy.*, así cubren el código que corre en el juego y no una copia.
 */
namespace TNChapaRules
{
	// ── Contador de chapas ──────────────────────────────────────────────────────────────────────────────────────────

	/** Cuántas de Amount chapas caben teniendo Current con el tope Max (0..Amount). */
	inline int32 AcceptedChapas(int32 Current, int32 Amount, int32 Max)
	{
		if (Amount <= 0)
		{
			return 0;
		}
		return FMath::Clamp(Max - Current, 0, Amount);
	}

	/** true si con Current chapas se puede pagar Cost (un coste negativo no se paga nunca). */
	inline bool CanSpend(int32 Current, int32 Cost)
	{
		return Cost >= 0 && Current >= Cost;
	}

	/** Revivir a una compañera: con coste 0 (desactivado en el dato) no hace falta pagar nada. */
	inline bool CanPayRevive(int32 Chapas, int32 Cost)
	{
		return Cost <= 0 || Chapas >= Cost;
	}

	// ── Vuelo de frisbee ────────────────────────────────────────────────────────────────────────────────────────────

	/** Punto del vuelo a los T segundos de salir de Origin con Velocity y gravedad Gravity (cm/s², hacia abajo). */
	inline FVector FlightPoint(const FVector& Origin, const FVector& Velocity, double Gravity, double T)
	{
		return Origin + Velocity * T - FVector(0.0, 0.0, 0.5 * Gravity * T * T);
	}

	/** Velocidad del vuelo a los T segundos. */
	inline FVector FlightVelocity(const FVector& Velocity, double Gravity, double T)
	{
		return Velocity - FVector(0.0, 0.0, Gravity * T);
	}

	/**
	 * Giro de la chapa en el aire: un disco de canto (la malla es un cilindro con el eje en Z) con el eje horizontal y de lado
	 * respecto a su rumbo, que gira sobre ese eje SpinDeg grados. Sin rumbo en planta, mira a +X.
	 */
	inline FQuat DiscRotation(const FVector& Velocity, double SpinDeg)
	{
		const FVector Flat(Velocity.X, Velocity.Y, 0.0);
		const double Yaw = Flat.IsNearlyZero() ? 0.0 : FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X));
		const FQuat Heading(FVector::ZAxisVector, FMath::DegreesToRadians(Yaw));
		const FQuat Spin(FVector::YAxisVector, FMath::DegreesToRadians(SpinDeg));
		const FQuat OnEdge(FVector::XAxisVector, UE_DOUBLE_HALF_PI);
		return Heading * Spin * OnEdge;
	}

	/** true si una superficie con esta normal sostiene la chapa tumbada (suelo o rampa suave). */
	inline bool IsRestingSurface(const FVector& Normal)
	{
		return Normal.Z >= 0.6;
	}

	/**
	 * true si el segmento de A a B (en el espacio de una caja centrada en el origen con medio tamaño Extent) entra en la caja.
	 * Corte por planos: también vale si A ya está dentro.
	 */
	inline bool SegmentHitsBox(const FVector& A, const FVector& B, const FVector& Extent)
	{
		double Enter = 0.0;
		double Leave = 1.0;
		const FVector Delta = B - A;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double Start = A[Axis];
			const double Step = Delta[Axis];
			const double Half = Extent[Axis];
			if (FMath::Abs(Step) < UE_DOUBLE_SMALL_NUMBER)
			{
				if (Start < -Half || Start > Half)
				{
					return false;
				}
				continue;
			}
			double T0 = (-Half - Start) / Step;
			double T1 = (Half - Start) / Step;
			if (T0 > T1)
			{
				Swap(T0, T1);
			}
			Enter = FMath::Max(Enter, T0);
			Leave = FMath::Min(Leave, T1);
			if (Enter > Leave)
			{
				return false;
			}
		}
		return true;
	}

	// ── Máquina expendedora ─────────────────────────────────────────────────────────────────────────────────────────

	/** Lo que pasa al pedir el objeto elegido. */
	enum class EBuy : uint8
	{
		/** Hay crédito: se cobra el precio y sale el objeto. */
		Bought,
		/** No llega el crédito: no se cobra nada. */
		NotEnoughCredit,
		/** La máquina no tiene ese objeto (hueco vacío o un objeto que no existe): no se cobra nada. */
		NoOffer
	};

	inline EBuy DecideBuy(int32 Credit, int32 Price, bool bOfferValid)
	{
		if (!bOfferValid || Price < 0)
		{
			return EBuy::NoOffer;
		}
		return Credit >= Price ? EBuy::Bought : EBuy::NotEnoughCredit;
	}

	/** Crédito tras meter una chapa de valor Value (sin pasar de MaxCredit). */
	inline int32 CreditAfterInsert(int32 Credit, int32 Value, int32 MaxCredit)
	{
		return FMath::Clamp(Credit + FMath::Max(0, Value), 0, FMath::Max(0, MaxCredit));
	}

	/** El objeto siguiente al elegido (vuelve al primero); -1 si la máquina no tiene ninguno. */
	inline int32 NextOffer(int32 Current, int32 Num)
	{
		if (Num <= 0)
		{
			return -1;
		}
		const int32 Next = Current < 0 ? 0 : Current + 1;
		return Next >= Num ? 0 : Next;
	}

	/**
	 * Mantener la tecla de la máquina: si se suelta antes de HoldSeconds es una pulsación (cambia de objeto); si se mantiene
	 * hasta ahí, compra.
	 */
	inline bool IsTap(double HeldSeconds, double HoldSeconds)
	{
		return HeldSeconds < HoldSeconds;
	}
}
