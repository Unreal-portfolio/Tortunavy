#pragma once

#include "CoreMinimal.h"
#include "TN_CoopScore.generated.h"

/**
 * Puntuación final del Coop (#789, hoja «Puntuación» del Excel de diseño): cuatro términos con su peso.
 *
 *  - Muñecos tortuga: recogidos por la jugadora / puestos en la partida (#797).
 *  - Conchas: puntos de concha recogidos por el equipo / puntos de concha que había en el nivel.
 *  - Finalización: la jugadora llegó a la meta (no eliminada).
 *  - Eficiencia de puzle: media de los puzles del nivel (TNCoopScore::PuzzleEfficiency).
 *
 * Un término sin nada que medir (nivel sin muñecos, sin conchas o sin puzles) da sus puntos enteros: así un nivel sin
 * puzles no castiga y el máximo sigue siendo MaxScore. Es aparte de la economía de conchas: no toca RaceScore ni la tienda.
 * Lo calcula el servidor al entrar en Results (ATN_ProcMapGameMode) y llega a cada jugadora en
 * ATN_CoopPlayerState::CoopScore. Pruebas: Tortunabo.Coop.Score.
 */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTN_CoopScoreBreakdown
{
	GENERATED_BODY()

	/** False hasta que el servidor la calcula (fuera del Coop no hay puntuación final). */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 DollsCollected = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 DollsTotal = 0;

	/** Puntos de concha recogidos por el equipo. */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 ShellsCollected = 0;

	/** Puntos de concha que había en el nivel. */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 ShellsTotal = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	bool bFinished = false;

	/** Eficiencia de puzle en [0, 1]; negativa si el nivel no tenía puzles. */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	float PuzzleEfficiency = -1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 DollPoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 ShellPoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 FinishPoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 PuzzlePoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 Total = 0;

	bool HasPuzzles() const { return PuzzleEfficiency >= 0.f; }
};

namespace TNCoopScore
{
	/** Pesos de cada término (suman MaxScore). El Excel no da cifras: provisionales hasta que los fije el director. */
	constexpr int32 DollWeight = 250;
	constexpr int32 ShellWeight = 250;
	constexpr int32 FinishWeight = 300;
	constexpr int32 PuzzleWeight = 200;
	constexpr int32 MaxScore = DollWeight + ShellWeight + FinishWeight + PuzzleWeight;

	/** Segundos para resolver un puzle desde el primer avance (primera placa o botón) sin perder eficiencia. */
	constexpr float PuzzleParSeconds = 30.f;

	/** Un puzle del nivel: si se resolvió y cuándo (segundos del mundo; ProgressTime < 0 = sin avance registrado). */
	struct FPuzzleRun
	{
		bool bSolved = false;
		float ProgressTime = -1.f;
		float SolveTime = -1.f;
	};

	/** Lo que mide la partida para una jugadora. Totales <= 0 o PuzzleEfficiency < 0 = término sin nada que medir. */
	struct FInputs
	{
		int32 DollsCollected = 0;
		int32 DollsTotal = 0;
		int32 ShellsCollected = 0;
		int32 ShellsTotal = 0;
		bool bFinished = false;
		float PuzzleEfficiency = -1.f;
	};

	/**
	 * Eficiencia de un puzle: 0 sin resolver; 1 si se resolvió sin avance registrado (un interruptor que se pulsa una
	 * vez) o dentro de ParSeconds desde el primer avance; si tardó más, ParSeconds / tiempo.
	 */
	inline float RunEfficiency(const FPuzzleRun& Run, float ParSeconds = PuzzleParSeconds)
	{
		if (!Run.bSolved) { return 0.f; }
		if (Run.ProgressTime < 0.f || ParSeconds <= 0.f) { return 1.f; }
		const float Took = FMath::Max(0.f, Run.SolveTime - Run.ProgressTime);
		return Took <= ParSeconds ? 1.f : ParSeconds / Took;
	}

	/** Media de los puzles del nivel; -1 si no había ninguno. */
	inline float PuzzleEfficiency(TConstArrayView<FPuzzleRun> Runs, float ParSeconds = PuzzleParSeconds)
	{
		if (Runs.Num() == 0) { return -1.f; }
		float Sum = 0.f;
		for (const FPuzzleRun& Run : Runs) { Sum += RunEfficiency(Run, ParSeconds); }
		return Sum / static_cast<float>(Runs.Num());
	}

	/** Fracción en [0, 1] de Got sobre Total; 1 si no hay nada que medir (Total <= 0). */
	inline double RatioOrFull(int32 Got, int32 Total)
	{
		return Total <= 0 ? 1.0 : FMath::Clamp(static_cast<double>(Got) / static_cast<double>(Total), 0.0, 1.0);
	}

	/** Puntos de un término: su peso por la fracción, redondeado. */
	inline int32 Points(int32 Weight, double Ratio)
	{
		return FMath::RoundToInt32(static_cast<double>(Weight) * FMath::Clamp(Ratio, 0.0, 1.0));
	}

	/** La puntuación final con su desglose (bValid = true). */
	inline FTN_CoopScoreBreakdown Compute(const FInputs& In)
	{
		FTN_CoopScoreBreakdown Out;
		Out.bValid = true;
		Out.DollsCollected = FMath::Max(0, In.DollsCollected);
		Out.DollsTotal = FMath::Max(0, In.DollsTotal);
		Out.ShellsCollected = FMath::Max(0, In.ShellsCollected);
		Out.ShellsTotal = FMath::Max(0, In.ShellsTotal);
		Out.bFinished = In.bFinished;
		Out.PuzzleEfficiency = In.PuzzleEfficiency < 0.f ? -1.f : FMath::Clamp(In.PuzzleEfficiency, 0.f, 1.f);

		Out.DollPoints = Points(DollWeight, RatioOrFull(Out.DollsCollected, Out.DollsTotal));
		Out.ShellPoints = Points(ShellWeight, RatioOrFull(Out.ShellsCollected, Out.ShellsTotal));
		Out.FinishPoints = Out.bFinished ? FinishWeight : 0;
		Out.PuzzlePoints = Points(PuzzleWeight, Out.HasPuzzles() ? Out.PuzzleEfficiency : 1.0);
		Out.Total = Out.DollPoints + Out.ShellPoints + Out.FinishPoints + Out.PuzzlePoints;
		return Out;
	}
}
