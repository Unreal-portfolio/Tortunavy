#pragma once

#include "CoreMinimal.h"

/**
 * Calidad gráfica del primer arranque (#556): qué hace UTN_GameSettingsSubsystem::Initialize con la prueba del equipo
 * (UGameUserSettings::RunHardwareBenchmark + ApplyHardwareBenchmarkResults). Lógica pura, sin motor, para que
 * Tortunabo.Settings.AutoQuality cubra la decisión «primera vez sí, segunda no».
 *
 * La marca de «ya se hizo» es la propia del motor: GameUserSettings.ini guarda LastCPUBenchmarkResult y
 * LastGPUBenchmarkResult, que valen -1 mientras no ha habido prueba. Así, borrar ese fichero (para volver a los ajustes de
 * serie) vuelve a pasar la prueba, y el que ya la tiene no la repite ni pisa lo que el jugador haya cambiado después.
 */
namespace TNAutoQuality
{
	/** Tope de FPS del primer arranque si el motor no trae ninguno (0 = sin límite): menú y lobby no ponen la GPU al 100 %. */
	constexpr float FIRST_BOOT_FRAME_RATE_LIMIT = 60.f;

	/** Qué hacer con la prueba del equipo al arrancar. */
	enum class EDecision : uint8
	{
		/** Primer arranque con pantalla: pasar la prueba y aplicar la calidad que aguanta. */
		RunBenchmark,
		/** Dentro del editor la calidad, los FPS y la sincronización son los del editor: no se toca nada. */
		SkipEditor,
		/** Sin pantalla (-nullrhi, servidor, commandlet, monkey y tests sin GPU): no hay nada que medir. */
		SkipNoRender,
		/** La prueba ya se pasó en un arranque anterior: se respeta lo que haya ahora. */
		SkipAlreadyDone
	};

	/** Una prueba hecha deja los dos resultados en GameUserSettings.ini con valor >= 0 (sin prueba, el motor guarda -1). */
	inline bool HasBenchmarkResults(float LastCPUResult, float LastGPUResult)
	{
		return LastCPUResult >= 0.f && LastGPUResult >= 0.f;
	}

	/**
	 * @brief Decide si este arranque pasa la prueba del equipo.
	 * @param bIsEditor Se ejecuta dentro del editor (GIsEditor).
	 * @param bCanRender El proceso dibuja (FApp::CanEverRender()).
	 * @param LastCPUResult GetLastCPUBenchmarkResult() leído de GameUserSettings.ini (-1 si nunca hubo prueba).
	 * @param LastGPUResult GetLastGPUBenchmarkResult() leído de GameUserSettings.ini (-1 si nunca hubo prueba).
	 */
	inline EDecision Decide(bool bIsEditor, bool bCanRender, float LastCPUResult, float LastGPUResult)
	{
		if (bIsEditor) { return EDecision::SkipEditor; }
		if (!bCanRender) { return EDecision::SkipNoRender; }
		return HasBenchmarkResults(LastCPUResult, LastGPUResult) ? EDecision::SkipAlreadyDone : EDecision::RunBenchmark;
	}

	/** Tope de FPS tras el primer arranque: el que ya tiene si lo hay (> 0) y, si no (0 = sin límite), FIRST_BOOT_FRAME_RATE_LIMIT. */
	inline float FirstBootFrameRateLimit(float CurrentLimit)
	{
		return CurrentLimit > 0.f ? CurrentLimit : FIRST_BOOT_FRAME_RATE_LIMIT;
	}

	/** Nombre para el log. */
	inline const TCHAR* DecisionName(EDecision Decision)
	{
		switch (Decision)
		{
		case EDecision::RunBenchmark: return TEXT("pasar la prueba");
		case EDecision::SkipEditor: return TEXT("editor: no se toca");
		case EDecision::SkipNoRender: return TEXT("sin pantalla: no hay nada que medir");
		case EDecision::SkipAlreadyDone: return TEXT("la prueba ya se pasó: no se repite");
		}
		return TEXT("?");
	}
}
