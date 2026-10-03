#pragma once

#include "CoreMinimal.h"

/**
 * Núcleo de CPU y calidad de servicio (QoS) del proceso para las herramientas de medida (TN.Stress, TN.Monkey; no Shipping).
 *
 * En CPU híbridas (núcleos de rendimiento P y de eficiencia E), Windows 11 baja la QoS de un proceso sin ventana visible ni
 * audio (-nullrhi -nosound) y el planificador manda sus hilos a núcleos E a ratos: el fotograma sube 1,6-2 veces en ráfagas,
 * no por el juego. Docs/Analisis/2026-10-03-Pico-25Hz-control.md.
 */
namespace TNCpuCore
{
	/** Clase de eficiencia del núcleo lógico en el que corre ahora el hilo que llama: 0 = eficiencia (E), mayor = rendimiento (P). INDEX_NONE si la plataforma no lo dice. */
	TORTUNABO_API int32 CurrentEfficiencyClass();

	/** true si la CPU tiene núcleos de distinta clase de eficiencia. */
	TORTUNABO_API bool IsHybrid();

	/** true si el hilo que llama corre ahora en un núcleo de eficiencia de una CPU híbrida. */
	TORTUNABO_API bool IsOnEfficiencyCore();

	/**
	 * Marca el proceso y el hilo que llama como QoS alta (EcoQoS desactivado explícitamente), como si el juego estuviera en
	 * primer plano: el planificador deja de preferir los núcleos E para él. true si Windows lo ha aceptado.
	 */
	TORTUNABO_API bool RequestHighQoS();

	/**
	 * En una CPU híbrida, pone los núcleos de rendimiento como conjunto de CPU por defecto del proceso (los hilos que no fijan
	 * el suyo solo usan núcleos P). false si la CPU no es híbrida o Windows no lo acepta.
	 */
	TORTUNABO_API bool PreferPerformanceCores();
}
