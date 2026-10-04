#pragma once

#include "CoreMinimal.h"

/**
 * Frecuencia de muestreo real de la captura de voz (#154). La que dice el dispositivo (PreferredSampleRate) no siempre es
 * la del flujo que entrega: con la cancelación de eco de Windows, un micrófono de 48 kHz puede llegar a 16 kHz en mono. Si
 * la voz sale etiquetada con la del dispositivo, el que la oye la reproduce más deprisa y más aguda (efecto ardilla). Aquí
 * se mide con cuántas muestras llegan de verdad por segundo (la primera medida, en medio segundo), y de esa frecuencia
 * salen a la vez el factor con el que se reducen las muestras y la frecuencia con la que se etiquetan (MakeSendPlan).
 * Hasta tener la primera medida no se sabe con cuál etiquetar y no se envía voz (ResolveCaptureRate). Lógica pura: los
 * tests de Tortunabo.Voice la cubren.
 */
namespace TNVoiceRate
{
	/**
	 * Límites de frecuencia (Hz) que aceptan el servidor (Server_SendVoiceData) y el receptor (PlayRemoteVoice). Un paquete
	 * fuera de ellos se descarta: acotarlo a otro valor dejaría la etiqueta distinta de la de las muestras que viajan y la
	 * voz sonaría acelerada o lenta.
	 */
	inline constexpr int32 MinVoiceRate = 8000;
	inline constexpr int32 MaxVoiceRate = 96000;

	/** Frecuencia objetivo de envío de serie (Hz): voz de banda ancha; con mu-law de 8 bits son 16 KB/s por quien habla. */
	inline constexpr int32 DefaultTargetRate = 16000;

	/** true si Hz es una frecuencia que el servidor y el receptor aceptan tal cual. */
	inline bool IsRateAccepted(int32 Hz)
	{
		return Hz >= MinVoiceRate && Hz <= MaxVoiceRate;
	}

	/**
	 * @brief Factor entero de reducción de una captura de CaptureRate Hz: max(1, CaptureRate / TargetRate). Con un factor
	 *        fijo una captura a 16 kHz salía a 5333 Hz, por debajo del mínimo aceptado (8000), y se oía 1,5 veces más rápida.
	 */
	inline int32 DownsampleFactorFor(int32 CaptureRate, int32 TargetRate = DefaultTargetRate)
	{
		return FMath::Max(1, CaptureRate / FMath::Max(1, TargetRate));
	}

	/**
	 * Cómo se reduce y se etiqueta una captura: el factor con el que se reducen las muestras y la frecuencia con la que
	 * salen son SIEMPRE los dos de este plan (SendRate = CaptureRate / Factor), así que la etiqueta es la de lo que viaja.
	 */
	struct FSendPlan
	{
		/** Frecuencia de la captura en mono (Hz) para la que se hizo el plan; 0 = sin plan. */
		int32 CaptureRate = 0;

		/** Cuántas muestras de la captura se promedian en una de las que se envían. */
		int32 Factor = 1;

		/** Frecuencia (Hz) con la que se etiqueta y se reproduce lo enviado. */
		int32 SendRate = 0;

		/** true si el servidor y el receptor aceptarán SendRate sin acotarla. */
		bool IsValid() const
		{
			return IsRateAccepted(SendRate);
		}
	};

	inline FSendPlan MakeSendPlan(int32 CaptureRate, int32 TargetRate = DefaultTargetRate)
	{
		FSendPlan Plan;
		Plan.CaptureRate = FMath::Max(0, CaptureRate);
		Plan.Factor = DownsampleFactorFor(Plan.CaptureRate, TargetRate);
		Plan.SendRate = Plan.CaptureRate / Plan.Factor;
		return Plan;
	}

	/**
	 * @brief Reduce In a 1/Factor con un filtro de caja (media de Factor muestras seguidas: sin el efecto «lata» de la
	 *        decimación simple) y añade el resultado a Out. Lo que sobra (menos de Factor muestras) queda en Carry y se suma
	 *        al bloque siguiente: la salida son siempre Total / Factor muestras aunque los bloques de la captura no sean
	 *        múltiplos del factor, y la frecuencia de envío (MakeSendPlan) es exacta. Al cambiar de factor hay que vaciar Carry.
	 */
	inline void Decimate(const TArray<float>& In, int32 Factor, TArray<float>& Carry, TArray<float>& Out)
	{
		Factor = FMath::Max(1, Factor);
		if (Factor == 1)
		{
			Carry.Reset();
			Out.Append(In);
			return;
		}
		Carry.Append(In);
		const int32 NumBlocks = Carry.Num() / Factor;
		Out.Reserve(Out.Num() + NumBlocks);
		for (int32 Block = 0; Block < NumBlocks; ++Block)
		{
			float Sum = 0.f;
			for (int32 Offset = 0; Offset < Factor; ++Offset)
			{
				Sum += Carry[Block * Factor + Offset];
			}
			Out.Add(Sum / Factor);
		}
		Carry.RemoveAt(0, NumBlocks * Factor, EAllowShrinking::No);
	}

	/** Frecuencias de muestreo habituales de un micrófono. */
	inline const TArray<int32>& StandardRates()
	{
		static const TArray<int32> Rates = { 8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000 };
		return Rates;
	}

	/**
	 * @brief La frecuencia estándar más cercana a la medida (en proporción), o 0 si ninguna está a menos de Tolerance.
	 *        44,1 y 48 kHz se distinguen: están a un 8,8 % la una de la otra.
	 */
	inline int32 SnapToStandardRate(double MeasuredHz, double Tolerance = 0.06)
	{
		if (MeasuredHz <= 0.0)
		{
			return 0;
		}
		int32 Best = 0;
		double BestError = TNumericLimits<double>::Max();
		for (const int32 Rate : StandardRates())
		{
			const double Error = FMath::Abs(MeasuredHz / Rate - 1.0);
			if (Error < BestError)
			{
				BestError = Error;
				Best = Rate;
			}
		}
		return BestError <= Tolerance ? Best : 0;
	}

	/**
	 * Cuenta las muestras (ya en mono) que llegan de la captura y confirma su frecuencia real (Rate):
	 *  - Primera medida, rápida: en cuanto hay FirstWindowSeconds de audio y las muestras por segundo cuadran con una
	 *    frecuencia estándar. Si no cuadran, la ventana sigue creciendo y se vuelve a mirar (con menos error cada vez: las
	 *    muestras llegan a ráfagas de un callback). Hasta entonces no se sabe con qué frecuencia etiquetar (Rate = 0).
	 *  - Afinado: con Rate conocida, en ventanas de WindowSeconds; solo la cambia cuando dos ventanas seguidas dan la misma
	 *    frecuencia y es otra: un tirón o una ventana con muestras perdidas no la cambia.
	 *  - Si a los GiveUpSeconds no hay primera medida (un dispositivo raro), bGaveUp: se usa la frecuencia del dispositivo
	 *    (ResolveCaptureRate). Sigue midiendo por si luego cuadra.
	 */
	struct FCaptureRateMeter
	{
		/** Audio necesario para la primera medida (s). */
		static constexpr double FirstWindowSeconds = 0.5;

		/** Ventana de afinado una vez hay primera medida (s). */
		static constexpr double WindowSeconds = 2.0;

		/** Tiempo sin primera medida tras el que se da por no medible (s). */
		static constexpr double GiveUpSeconds = 4.0;

		/** Frecuencia confirmada (Hz); 0 mientras no se sabe. */
		int32 Rate = 0;

		/** true si pasaron GiveUpSeconds sin primera medida: toca usar la del dispositivo. */
		bool bGaveUp = false;

		/**
		 * @brief Anota Samples muestras recibidas en el instante Now (s). Las de la primera llamada solo marcan el inicio
		 *        (no se sabe desde cuándo estaban en el búfer).
		 * @return true si la frecuencia confirmada ha cambiado.
		 */
		bool Add(int32 Samples, double Now)
		{
			if (WindowStart < 0.0)
			{
				WindowStart = Now;
				WindowSamples = 0;
				return false;
			}
			WindowSamples += FMath::Max(0, Samples);
			const double Elapsed = Now - WindowStart;

			if (Rate == 0)
			{
				if (Elapsed < FirstWindowSeconds)
				{
					return false;
				}
				const int32 First = SnapToStandardRate(static_cast<double>(WindowSamples) / Elapsed);
				if (First > 0)
				{
					Rate = First;
					LastSnapped = First;
					WindowStart = Now;
					WindowSamples = 0;
					return true;
				}
				if (Elapsed >= GiveUpSeconds)
				{
					bGaveUp = true;
					WindowStart = Now;
					WindowSamples = 0;
				}
				return false;
			}

			if (Elapsed < WindowSeconds)
			{
				return false;
			}
			const int32 Snapped = SnapToStandardRate(static_cast<double>(WindowSamples) / Elapsed);
			WindowStart = Now;
			WindowSamples = 0;
			const bool bConfirmed = Snapped > 0 && Snapped == LastSnapped && Snapped != Rate;
			LastSnapped = Snapped;
			if (bConfirmed)
			{
				Rate = Snapped;
			}
			return bConfirmed;
		}

	private:
		double WindowStart = -1.0;
		int64 WindowSamples = 0;
		int32 LastSnapped = 0;
	};

	/**
	 * @brief La frecuencia de la captura con la que hacer el plan de envío (MakeSendPlan), o 0 si todavía no se puede enviar
	 *        voz: la medida (FCaptureRateMeter::Rate) en cuanto hay primera medida; si no la hay y ya se dio por no medible
	 *        (bGaveUp), la del dispositivo; antes de eso, 0.
	 */
	inline int32 ResolveCaptureRate(int32 MeasuredRate, bool bGaveUp, int32 DeviceRate)
	{
		if (MeasuredRate > 0)
		{
			return MeasuredRate;
		}
		return bGaveUp ? FMath::Max(0, DeviceRate) : 0;
	}
}
