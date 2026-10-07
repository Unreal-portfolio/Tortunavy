#pragma once

#include "CoreMinimal.h"
#include "TN_CoopScore.generated.h"

/** Títulos de fin de partida como banderas de FTN_CoopScoreBreakdown::TitleFlags (#873). */
namespace TNEndTitleFlags
{
	constexpr uint8 Jumper = 1 << 0;
	constexpr uint8 Treasurer = 1 << 1;
	constexpr uint8 Healer = 1 << 2;
	constexpr uint8 All = Jumper | Treasurer | Healer;
}

/**
 * Valores de la fórmula de puntos de final de partida (#873, decisión del director del 07-10). Viven en el DataAsset
 * UTN_PointsEconomy (DA_PointsEconomy); los de aquí son los de la decisión y sirven si el asset no carga.
 */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTN_EndScoreRules
{
	GENERATED_BODY()

	FTN_EndScoreRules() { PositionPoints = { 50, 30, 20, 10 }; }

	/** Por llegar a la meta. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score", meta = (ClampMin = "0"))
	int32 FinishPoints = 100;

	/** Por puesto de llegada (índice 0 = primera); fuera de la lista, nada. Solo para quien llega. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score")
	TArray<int32> PositionPoints;

	/** Segundos por debajo del tiempo objetivo del nivel que valen un punto. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score", meta = (ClampMin = "1.0"))
	float SecondsPerTimePoint = 10.f;

	/** Tope de los puntos por tiempo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score", meta = (ClampMin = "0"))
	int32 MaxTimePoints = 60;

	/** Por cada muñeco tortuga cogido. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score", meta = (ClampMin = "0"))
	int32 PointsPerDoll = 25;

	/** Objetos recogidos / objetos del nivel, por este peso. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score", meta = (ClampMin = "0"))
	int32 CollectedRatioPoints = 50;

	/** Eficiencia en los puzles del nivel (0-1), por este peso. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score", meta = (ClampMin = "0"))
	int32 PuzzleEfficiencyPoints = 30;

	/** Por cada título (Saltarín, Tesorero, Curandero). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score", meta = (ClampMin = "0"))
	int32 PointsPerTitle = 20;
};

/**
 * Puntos de final de partida de una jugadora con su desglose (#873, antes #789). Los calcula el servidor al entrar en
 * Results (ATN_CoopGameState::AwardEndScores) y llegan a cada jugadora en ATN_CoopPlayerState::CoopScore; su máquina
 * suma Total a su perfil (saldo de la tienda). Pruebas: Tortunabo.Coop.Score.
 */
USTRUCT(BlueprintType)
struct TORTUNABO_API FTN_CoopScoreBreakdown
{
	GENERATED_BODY()

	/** False hasta que el servidor la calcula. */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	bool bFinished = false;

	/** Puesto de llegada (1 = primera); 0 si no llegó. */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 FinishRank = 0;

	/** Segundos de carrera al llegar; negativo si no llegó. */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	float FinishTimeSeconds = -1.f;

	/** Tiempo objetivo del nivel (segundos). */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	float TargetSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 DollsCollected = 0;

	/** Objetos del nivel que ha recogido ella (UTN_LevelCollectSubsystem). */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 ItemsCollected = 0;

	/** Objetos que había en el nivel. */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 ItemsTotal = 0;

	/** Eficiencia de puzle en [0, 1]; negativa si el nivel no tenía puzles. */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	float PuzzleEfficiency = -1.f;

	/** Títulos que se ha llevado (TNEndTitleFlags). */
	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	uint8 TitleFlags = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 FinishPoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 PositionPoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 TimePoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 DollPoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 CollectPoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 PuzzlePoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 TitlePoints = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Coop|Score")
	int32 Total = 0;

	bool HasPuzzles() const { return PuzzleEfficiency >= 0.f; }
	bool HasItems() const { return ItemsTotal > 0; }
	bool HasTitle(uint8 Flag) const { return (TitleFlags & Flag) != 0; }
};

namespace TNCoopScore
{
	/** Segundos para resolver un puzle desde el primer avance (primera placa o botón) sin perder eficiencia. */
	constexpr float PuzzleParSeconds = 30.f;

	/** Un puzle del nivel: si se resolvió y cuándo (segundos del mundo; ProgressTime < 0 = sin avance registrado). */
	struct FPuzzleRun
	{
		bool bSolved = false;
		float ProgressTime = -1.f;
		float SolveTime = -1.f;
	};

	/** Lo que mide la partida para una jugadora. */
	struct FInputs
	{
		bool bFinished = false;
		int32 FinishRank = 0;
		float FinishTimeSeconds = -1.f;
		float TargetSeconds = 0.f;
		int32 DollsCollected = 0;
		int32 ItemsCollected = 0;
		int32 ItemsTotal = 0;
		/** Negativa si el nivel no tenía puzles. */
		float PuzzleEfficiency = -1.f;
		uint8 TitleFlags = 0;
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

	/** Puntos de un término proporcional: su peso por la fracción en [0, 1], redondeado. */
	inline int32 Points(int32 Weight, double Ratio)
	{
		return FMath::RoundToInt32(static_cast<double>(FMath::Max(0, Weight)) * FMath::Clamp(Ratio, 0.0, 1.0));
	}

	/** Puntos por puesto (1 = primera); 0 fuera de la tabla o sin puesto. */
	inline int32 PositionPoints(const FTN_EndScoreRules& Rules, int32 Rank)
	{
		return Rules.PositionPoints.IsValidIndex(Rank - 1) ? FMath::Max(0, Rules.PositionPoints[Rank - 1]) : 0;
	}

	/** Un punto por cada SecondsPerTimePoint enteros por debajo del objetivo, hasta MaxTimePoints. */
	inline int32 TimePoints(const FTN_EndScoreRules& Rules, float FinishSeconds, float TargetSeconds)
	{
		if (FinishSeconds < 0.f || TargetSeconds <= 0.f || Rules.SecondsPerTimePoint <= 0.f) { return 0; }
		const int32 Steps = FMath::FloorToInt32((TargetSeconds - FinishSeconds) / Rules.SecondsPerTimePoint);
		return FMath::Clamp(Steps, 0, FMath::Max(0, Rules.MaxTimePoints));
	}

	/** Cuántos títulos hay en Flags. */
	inline int32 CountTitles(uint8 Flags)
	{
		return FMath::CountBits(static_cast<uint64>(Flags & TNEndTitleFlags::All));
	}

	/**
	 * Los puntos con su desglose (bValid = true). Meta, puesto y tiempo solo para quien llega; muñecos, objetos, puzles y
	 * títulos para todas. Un término sin nada que medir (nivel sin objetos o sin puzles) no da puntos.
	 */
	inline FTN_CoopScoreBreakdown Compute(const FInputs& In, const FTN_EndScoreRules& Rules)
	{
		FTN_CoopScoreBreakdown Out;
		Out.bValid = true;
		Out.bFinished = In.bFinished;
		Out.FinishRank = In.bFinished ? FMath::Max(0, In.FinishRank) : 0;
		Out.FinishTimeSeconds = In.bFinished ? In.FinishTimeSeconds : -1.f;
		Out.TargetSeconds = FMath::Max(0.f, In.TargetSeconds);
		Out.DollsCollected = FMath::Max(0, In.DollsCollected);
		Out.ItemsTotal = FMath::Max(0, In.ItemsTotal);
		Out.ItemsCollected = FMath::Clamp(In.ItemsCollected, 0, Out.ItemsTotal);
		Out.PuzzleEfficiency = In.PuzzleEfficiency < 0.f ? -1.f : FMath::Clamp(In.PuzzleEfficiency, 0.f, 1.f);
		Out.TitleFlags = In.TitleFlags & TNEndTitleFlags::All;

		Out.FinishPoints = Out.bFinished ? FMath::Max(0, Rules.FinishPoints) : 0;
		Out.PositionPoints = Out.bFinished ? PositionPoints(Rules, Out.FinishRank) : 0;
		Out.TimePoints = Out.bFinished ? TimePoints(Rules, Out.FinishTimeSeconds, Out.TargetSeconds) : 0;
		Out.DollPoints = Out.DollsCollected * FMath::Max(0, Rules.PointsPerDoll);
		Out.CollectPoints = Out.HasItems()
			? Points(Rules.CollectedRatioPoints, static_cast<double>(Out.ItemsCollected) / static_cast<double>(Out.ItemsTotal)) : 0;
		Out.PuzzlePoints = Out.HasPuzzles() ? Points(Rules.PuzzleEfficiencyPoints, Out.PuzzleEfficiency) : 0;
		Out.TitlePoints = CountTitles(Out.TitleFlags) * FMath::Max(0, Rules.PointsPerTitle);
		Out.Total = Out.FinishPoints + Out.PositionPoints + Out.TimePoints + Out.DollPoints + Out.CollectPoints
			+ Out.PuzzlePoints + Out.TitlePoints;
		return Out;
	}
}
