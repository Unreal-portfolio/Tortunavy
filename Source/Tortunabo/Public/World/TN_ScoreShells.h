#pragma once

#include "CoreMinimal.h"

/**
 * Conchas de puntos (ATN_ScorePickup): cuatro tamaños con su valor, su aspecto base y el reparto de una recogida en
 * los iconos que vuelan al contador del HUD. Lógica pura (sin UObjects), igual en todas las máquinas; la prueban
 * Tortunabo.ScoreShells.* (Private/Tests/TN_ScoreShellsTest.cpp).
 *
 * «Shell» aquí es la concha que se recoge, no el caparazón de la tortuga (TNShellLogic, UTN_ShellComponent).
 */
namespace TNScoreShells
{
	enum class ETier : uint8
	{
		/** Conchita de 1: pequeña y dorada clara, en rachas por el camino y los desvíos y en arcos sobre los saltos. */
		Small = 0,
		/** La de siempre, de 25 (peligros por bioma, atalayas y torres de escalada). */
		Normal = 1,
		/** Nacarada de 50: turquesa, con halo, destellos, luz y columna de luz; pocas, en retos o escondidas. */
		Big = 2,
		/** Reina de 100: rosa y violeta con filo dorado, más grande; una o dos por mapa, en los retos más difíciles. */
		Grand = 3,
	};

	constexpr int32 NumTiers = 4;

	/** Puntos de cada tamaño. */
	constexpr int32 ValueOf(ETier Tier)
	{
		return Tier == ETier::Small ? 1 : (Tier == ETier::Normal ? 25 : (Tier == ETier::Big ? 50 : 100));
	}

	/**
	 * Tamaño que corresponde a un valor (un Blueprint con ScoreValue propio): hasta 5, pequeña;
	 * hasta 37, normal; hasta 75, grande; más, reina.
	 */
	constexpr ETier TierForValue(int32 Value)
	{
		return Value <= 5 ? ETier::Small : (Value <= 37 ? ETier::Normal : (Value <= 75 ? ETier::Big : ETier::Grand));
	}

	/** Índice seguro (0-3) de un tamaño llegado por red. */
	constexpr ETier TierFromIndex(int32 Index)
	{
		return static_cast<ETier>(Index < 0 ? 0 : (Index > 3 ? 3 : Index));
	}

	// ── Aspecto en el mundo (lo usan ATN_ScorePickup y ATN_ScoreShellBurst) ──

	/** Escala de la malla de la vieira (~68 cm de ancho a escala 1). La normal es la de siempre (1,5). */
	constexpr float MeshScale(ETier Tier)
	{
		return Tier == ETier::Small ? 0.8f : (Tier == ETier::Normal ? 1.5f : (Tier == ETier::Big ? 2.f : 2.5f));
	}

	/** Radio de la esfera de recogida (cm); su centro va a Hover sobre el suelo. */
	constexpr float CollectRadius(ETier Tier)
	{
		return Tier == ETier::Small ? 48.f : (Tier == ETier::Normal ? 60.f : (Tier == ETier::Big ? 72.f : 80.f));
	}

	/** Altura del centro de la concha y de su esfera de recogida sobre el suelo (cm). */
	constexpr double Hover = 60.0;

	/** Altura extra de la malla sobre el centro del actor (cm): la vieira gira por encima de la esfera. */
	constexpr float MeshLift(ETier Tier)
	{
		return Tier == ETier::Small ? 30.f : (Tier == ETier::Normal ? 55.f : (Tier == ETier::Big ? 70.f : 85.f));
	}

	/** Vueltas por segundo del giro de moneda. */
	constexpr float SpinTurns(ETier Tier)
	{
		return Tier == ETier::Small ? 0.6f : (Tier == ETier::Normal ? 0.45f : (Tier == ETier::Big ? 0.35f : 0.3f));
	}

	/** Distancia de dibujo de la malla (cm; 0 = sin límite): las pequeñas desaparecen a 70 m. */
	constexpr float CullDistance(ETier Tier)
	{
		return Tier == ETier::Small ? 7000.f : 0.f;
	}

	/** A partir de esta distancia a la cámara local la concha deja de girar y de mover sus destellos (cm). */
	constexpr float WakeDistance(ETier Tier)
	{
		return Tier == ETier::Small ? 7000.f : (Tier == ETier::Normal ? 9000.f : 25000.f);
	}

	/** Color del brillo de cada tamaño (lineal): destellos, halo, luz, estallido y columna. */
	inline FLinearColor GlowColor(ETier Tier)
	{
		switch (Tier)
		{
			case ETier::Small:  return FLinearColor(1.f, 0.93f, 0.62f);
			case ETier::Normal: return FLinearColor(1.f, 0.92f, 0.55f);
			case ETier::Big:    return FLinearColor(0.45f, 1.f, 0.92f);
			default:            return FLinearColor(1.f, 0.45f, 0.85f);
		}
	}

	// ── Recogida en el HUD ──

	/** Como mucho, iconos que vuelan al contador por recogida. */
	constexpr int32 MaxIcons = 15;

	/**
	 * Cuántos iconos para Value puntos: ~2·√Value (1 → 1, 4 → 4, 25 → 10, 50 → 14, 100 → 15), nunca más que puntos ni
	 * que InMaxIcons.
	 */
	inline int32 IconCountFor(int32 Value, int32 InMaxIcons = MaxIcons)
	{
		if (Value <= 0) { return 0; }
		const int32 ByRoot = FMath::Max(1, FMath::RoundToInt32(2.0 * FMath::Sqrt(static_cast<double>(Value))));
		return FMath::Clamp(FMath::Min(Value, ByRoot), 1, FMath::Max(1, InMaxIcons));
	}

	/**
	 * Reparte Value entre los iconos de IconCountFor: todos valen al menos 1, suman Value exacto y el resto de la
	 * división va a los últimos (el contador acelera al final). Vacío si Value <= 0.
	 */
	inline void SplitIntoIcons(int32 Value, int32 InMaxIcons, TArray<int32>& Out)
	{
		Out.Reset();
		const int32 Count = IconCountFor(Value, InMaxIcons);
		if (Count <= 0) { return; }
		const int32 Base = Value / Count;
		const int32 Extra = Value % Count;
		for (int32 i = 0; i < Count; ++i)
		{
			Out.Add(Base + (i >= Count - Extra ? 1 : 0));
		}
	}

	/** Pasos del «pom» que siguen subiendo; a partir de aquí se repite la nota más alta. */
	constexpr int32 PomTopStep = 10;

	/**
	 * Semitonos (sobre do5) del «pom» número Step de una tanda: escala pentatónica mayor que sube (do, re, mi, sol, la,
	 * do...) hasta dos octavas y allí se queda.
	 */
	inline int32 PomSemitones(int32 Step)
	{
		static constexpr int32 Scale[5] = { 0, 2, 4, 7, 9 };
		const int32 S = FMath::Clamp(Step, 0, PomTopStep);
		return Scale[S % 5] + 12 * (S / 5);
	}

	/** Sin llegar un icono en este tiempo (s), la tanda de «pom» vuelve a empezar desde abajo. */
	constexpr float PomChainSeconds = 0.9f;
}
