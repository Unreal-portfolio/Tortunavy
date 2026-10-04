// Copiloto automático del Rally (#331): cuando el buggy va sin artillera humana, la máquina de la conductora canta cada nota
// de copiloto (TNRallyPaceNotes) unos segundos antes de llegar, con una señal sonora localizada (al lado de la curva, tantos
// pitidos como el grado) y una placa breve en el salpicadero. Lógica pura sin mundo (tests Tortunabo.Rally.Copilot.*); la
// aplica UTN_RallyCopilotComponent. La señal y la placa son las mismas que usará la artillera al cantar notas (#330).
#pragma once

#include "CoreMinimal.h"
#include "Rally/TN_RallyPaceNotes.h"

namespace TNRallyCopilot
{
	/** Antelación con que se canta una nota a la velocidad actual (s). */
	inline constexpr double DefaultLeadSeconds = 3.0;
	/** Distancia mínima a la que se canta, aunque el buggy vaya despacio o parado (cm). */
	inline constexpr double DefaultMinCallCm = 6000.0;
	/** Separación mínima entre dos cantos, para que las señales no se pisen (s). */
	inline constexpr double MinSecondsBetweenCalls = 0.9;
	/** Separación entre los pitidos de una señal (s): seis pitidos caben en 0,66 s. */
	inline constexpr float BeepIntervalSeconds = 0.11f;
	/** Lo que dura la placa en el salpicadero (s). */
	inline constexpr float PlateSeconds = 1.5f;

	/** Sonido de la señal. */
	enum class ECallSound : uint8
	{
		/** Curva: pitidos en el lado de la curva. */
		Beep,
		Crest,
		Jump,
		Water
	};

	/** Señal sonora de una nota. */
	struct FCallSignal
	{
		ECallSound Sound = ECallSound::Beep;
		/** Veces que suena: en las curvas, el grado (1 = horquilla .. 6 = casi recta); en el resto, 1. */
		int32 Beeps = 1;
		/** Lado: -1 izquierda, 0 centro, +1 derecha. */
		float Pan = 0.f;
		/** Más aguda cuanto más cerrada es la curva. */
		float Pitch = 1.f;
	};

	/** Distancia a la que se canta a SpeedCms: la que se recorre en LeadSeconds, nunca menos de MinCm (cm). */
	TORTUNABO_API double CallDistanceCm(double SpeedCms, double LeadSeconds = DefaultLeadSeconds, double MinCm = DefaultMinCallCm);

	/** Ya toca cantar una nota que está DistanceCm por delante (0 <= distancia <= CallDistanceCm). */
	TORTUNABO_API bool IsTimeToCall(double DistanceCm, double SpeedCms, double LeadSeconds = DefaultLeadSeconds,
		double MinCm = DefaultMinCallCm);

	/** Solo canta la máquina de la conductora y solo si su buggy no lleva artillera humana (una sola jugadora o un bot). */
	TORTUNABO_API bool IsAutoCopilotActive(bool bLocalIsDriver, bool bHasHumanGunner);

	/** Señal de una nota: lado de la curva y pitidos según el grado; crestas, saltos y agua con su sonido, centrados. */
	TORTUNABO_API FCallSignal SignalFor(const TNRallyPaceNotes::FPaceNote& Note);

	/**
	 * Índice en Ahead (de la más cercana a la más lejana) de la nota que hay que cantar ahora, o INDEX_NONE: la primera sin
	 * cantar (su arco no está en CalledArcs) a la que ya le toca, si desde el último canto han pasado al menos
	 * MinSecondsBetweenCalls. Una nota que se queda esperando y se pasa ya no sale en Ahead: no se canta tarde.
	 */
	TORTUNABO_API int32 PickNoteToCall(TConstArrayView<TNRallyPaceNotes::FNoteAhead> Ahead, double SpeedCms,
		TConstArrayView<double> CalledArcs, double SecondsSinceLastCall);

	/** Línea grande de la placa: en las curvas, el lado y el grado («‹ 3», «3 ›»); en el resto, la nota («cresta»). */
	TORTUNABO_API FText PlateHeadline(const TNRallyPaceNotes::FPaceNote& Note);

	/** Línea pequeña de la placa: en las curvas, la nota entera («horquilla izquierda, no cortes»); en el resto, vacía. */
	TORTUNABO_API FText PlateDetail(const TNRallyPaceNotes::FPaceNote& Note);

	/** Arcos ya cantados que siguen por delante (en Ahead); los demás se olvidan para cantarlos en la vuelta siguiente. */
	TORTUNABO_API TArray<double> KeepCalledAhead(TConstArrayView<double> CalledArcs, TConstArrayView<TNRallyPaceNotes::FNoteAhead> Ahead);
}
