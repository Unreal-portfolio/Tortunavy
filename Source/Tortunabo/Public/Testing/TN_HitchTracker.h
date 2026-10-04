#pragma once

#include "CoreMinimal.h"

/**
 * Lógica pura del registro de tirones (#152, Docs/Analisis/2026-10-03-Tirones-lobby.md): a qué hilo se debe un fotograma
 * largo y cada cuánto se repiten. Sin mundo ni motor: la usa UTN_HitchMonitorSubsystem y se testea igual
 * (Tortunabo.Testing.HitchTracker).
 */
namespace TNHitch
{
	/** Quién se ha llevado el fotograma largo. */
	enum class EBound : uint8
	{
		/** Lógica, red, cargas síncronas o recolector de basura en el hilo de juego. */
		GameThread,
		/** Preparación de la escena en el hilo de render. */
		RenderThread,
		/** Envío de órdenes al controlador en el hilo RHI. */
		RhiThread,
		/** La GPU no termina a tiempo y la CPU la espera. */
		Gpu,
		/**
		 * Ningún hilo ha estado ocupado la mitad del fotograma. Los tiempos de hilo excluyen las esperas: puede ser una espera
		 * (presentación, vsync, controlador) o que el proceso no corriera (planificador, otro proceso).
		 */
		Unknown
	};

	const TCHAR* BoundName(EBound Bound);

	/**
	 * El hilo con más tiempo, si explica al menos la mitad del fotograma; si no, Unknown. Los tiempos de hilo son los
	 * que el motor publica (GGameThreadTime, GRenderThreadTime, GRHIThreadTime, RHIGetGPUFrameCycles), en milisegundos.
	 */
	EBound Classify(float FrameMs, float GameThreadMs, float RenderThreadMs, float RhiThreadMs, float GpuMs);

	/** Últimos tirones: cuántos hay y cada cuánto llegan (mediana de los intervalos). */
	class FTracker
	{
	public:
		/** Tirones que se recuerdan para el intervalo. */
		static constexpr int32 MaxRemembered = 16;

		/** Anota un tirón en el instante TimeSeconds (reloj real, creciente). */
		void Add(double TimeSeconds);

		/** Tirones anotados desde el principio. */
		int32 Total() const { return TotalCount; }

		/** Mediana de los intervalos entre los últimos tirones; 0 si hay menos de dos. */
		double MedianIntervalSeconds() const;

		/** Tiempo entre el último tirón y el anterior; 0 si hay menos de dos. */
		double LastIntervalSeconds() const;

	private:
		TArray<double> Times;
		int32 TotalCount = 0;
	};
}
