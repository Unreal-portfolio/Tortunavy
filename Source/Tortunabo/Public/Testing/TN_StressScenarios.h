#pragma once

#include "CoreMinimal.h"

/**
 * Escenarios de estrés (TN.Stress <escenario>): cuánto se crea de cada cosa y en qué orden. Lógica pura, testeada en
 * Tortunabo.Monkey. Docs/Estres-Monkey-2026-09-29.md.
 */
namespace TNStress
{
	/** Qué se añade al empezar cada fase (las anteriores se quedan). */
	enum class EGroup : uint8
	{
		Baseline,
		Crabs,
		Gulls,
		Tanks,
		Items,
		Throwables,
		Count
	};

	inline const TCHAR* GroupName(EGroup Group)
	{
		static const TCHAR* const Names[] = { TEXT("baseline"), TEXT("crabs"), TEXT("gulls"), TEXT("tanks"), TEXT("race_items"), TEXT("throwables") };
		static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(EGroup::Count), "GroupName: falta un nombre");
		const int32 Index = static_cast<int32>(Group);
		return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : TEXT("?");
	}

	struct FScenario
	{
		FString Name;
		/** Enemigos de playa (cangrejos, gaviotas y tanques), lanzables y cajas de objetos de carrera. */
		int32 Enemies = 0;
		int32 Throwables = 0;
		int32 Items = 0;
		/** Tortugas jugando (locales; las que faltan se crean como jugadores extra). */
		int32 Turtles = 1;
		/** Fases de referencia (sin crear nada); el control usa varias para medir cuánto deriva el juego solo con el tiempo. */
		int32 BaselinePhases = 1;
	};

	/** light (50/100/20), heavy (200/500/100), race8 (8 tortugas, sin extras) y control (una tortuga, nada que crear, 6 fases). false si no existe. */
	inline bool Parse(const FString& Name, FScenario& Out)
	{
		Out = FScenario();
		Out.Name = Name.ToLower();
		if (Out.Name == TEXT("light"))
		{
			Out.Enemies = 50;
			Out.Throwables = 100;
			Out.Items = 20;
			return true;
		}
		if (Out.Name == TEXT("heavy"))
		{
			Out.Enemies = 200;
			Out.Throwables = 500;
			Out.Items = 100;
			return true;
		}
		if (Out.Name == TEXT("control"))
		{
			Out.BaselinePhases = 6;
			return true;
		}
		if (Out.Name == TEXT("race8"))
		{
			Out.Turtles = 8;
			return true;
		}
		return false;
	}

	/** Reparto de los enemigos: mitad cangrejos (gigantes y ermitaños), un cuarto de zonas de gaviotas y el resto tanques. */
	struct FEnemySplit
	{
		int32 Crabs = 0;
		int32 Gulls = 0;
		int32 Tanks = 0;
		int32 Total() const { return Crabs + Gulls + Tanks; }
	};

	inline FEnemySplit SplitEnemies(int32 Enemies)
	{
		FEnemySplit Out;
		const int32 Safe = FMath::Max(0, Enemies);
		Out.Gulls = Safe / 4;
		Out.Tanks = Safe / 4;
		Out.Crabs = Safe - Out.Gulls - Out.Tanks;
		return Out;
	}

	/**
	 * Presupuesto (ms) para crear enemigos y cajas en un fotograma: el mismo que usa el generador de la playa al montar una
	 * ronda (TN.Beach.BuildBudgetMs). Crearlos todos de golpe daba tirones de 30-96 ms que en el juego no existen (#79).
	 */
	constexpr double SPAWN_BUDGET_MS = 6.0;

	/** Grupos que se crean repartidos en varios fotogramas (los lanzables ya se reponen de 25 en 25 cada 0,5 s). */
	inline bool IsSpreadGroup(EGroup Group)
	{
		return Group == EGroup::Crabs || Group == EGroup::Gulls || Group == EGroup::Tanks || Group == EGroup::Items;
	}

	/** Se crea uno más en este fotograma: quedan por crear y es el primero del fotograma o aún queda presupuesto. */
	inline bool ShouldSpawnMore(int32 Left, int32 MadeThisFrame, double ElapsedMs, double BudgetMs = SPAWN_BUDGET_MS)
	{
		return Left > 0 && (MadeThisFrame == 0 || ElapsedMs < BudgetMs);
	}

	struct FPhase
	{
		EGroup Group = EGroup::Baseline;
		int32 Count = 0;
		float Start = 0.f;
		float End = 0.f;
	};

	/**
	 * Fases de la medición: una de referencia sin nada y una por cada grupo con algo que crear, todas de la misma duración
	 * (Total repartido a partes iguales). Así el coste de cada grupo sale de restar a su fase la anterior.
	 */
	inline TArray<FPhase> BuildTimeline(const FScenario& Scenario, float TotalSeconds)
	{
		const FEnemySplit Split = SplitEnemies(Scenario.Enemies);
		TArray<FPhase> Phases;
		for (int32 Index = 0; Index < FMath::Max(1, Scenario.BaselinePhases); ++Index)
		{
			Phases.Add({ EGroup::Baseline, 0, 0.f, 0.f });
		}
		if (Split.Crabs > 0) { Phases.Add({ EGroup::Crabs, Split.Crabs, 0.f, 0.f }); }
		if (Split.Gulls > 0) { Phases.Add({ EGroup::Gulls, Split.Gulls, 0.f, 0.f }); }
		if (Split.Tanks > 0) { Phases.Add({ EGroup::Tanks, Split.Tanks, 0.f, 0.f }); }
		if (Scenario.Items > 0) { Phases.Add({ EGroup::Items, Scenario.Items, 0.f, 0.f }); }
		// Los lanzables caducan solos, así que van los últimos: no falsean el coste de lo que se crea después.
		if (Scenario.Throwables > 0) { Phases.Add({ EGroup::Throwables, Scenario.Throwables, 0.f, 0.f }); }
		const float Each = TotalSeconds / static_cast<float>(Phases.Num());
		for (int32 Index = 0; Index < Phases.Num(); ++Index)
		{
			Phases[Index].Start = Each * static_cast<float>(Index);
			Phases[Index].End = Each * static_cast<float>(Index + 1);
		}
		return Phases;
	}
}
