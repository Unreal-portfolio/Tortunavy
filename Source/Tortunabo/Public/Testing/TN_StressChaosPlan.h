#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/**
 * Escenario de estrés «caos» (TN.Stress caos, -TNStress=caos): el peor caso de juego real. Cuatro tortugas que se cogen
 * y se lanzan, ruedan en su bola y lanzan objetos en ráfagas, con cangrejos, gaviotas y tanques
 * persiguiéndolas. Las fases van sumando carga (cada una mantiene lo de la anterior). Lógica pura, testeada en
 * Tortunabo.Stress.Chaos. Docs/Analisis/2026-10-03-Estres-caos.md.
 */
namespace TNChaos
{
	/** Qué se suma al empezar cada fase. */
	enum class EStep : uint8
	{
		Baseline,
		Carry,
		Ball,
		Items,
		Enemies,
		Peak,
		Count
	};

	/** Lo que puede estar haciendo una tortuga. */
	enum class ETask : uint8
	{
		Wander,
		Carry,
		Ball,
		Items,
		Count
	};

	inline const TCHAR* StepName(EStep Step)
	{
		static const TCHAR* const Names[] = { TEXT("baseline"), TEXT("carry"), TEXT("ball"), TEXT("items"), TEXT("enemies"), TEXT("peak") };
		static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(EStep::Count), "StepName: falta un nombre");
		const int32 Index = static_cast<int32>(Step);
		return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : TEXT("?");
	}

	inline const TCHAR* TaskName(ETask Task)
	{
		static const TCHAR* const Names[] = { TEXT("wander"), TEXT("carry"), TEXT("ball"), TEXT("items") };
		static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(ETask::Count), "TaskName: falta un nombre");
		const int32 Index = static_cast<int32>(Task);
		return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : TEXT("?");
	}

	constexpr uint8 TaskBit(ETask Task) { return static_cast<uint8>(1u << static_cast<uint8>(Task)); }

	inline bool HasTask(uint8 Mask, ETask Task) { return (Mask & TaskBit(Task)) != 0; }

	struct FConfig
	{
		/** Segundos de cada fase. */
		float PhaseSeconds = 20.f;
		/** Tortugas (locales del anfitrión; las que faltan se crean como jugadores extra). */
		int32 Turtles = 4;
		/** Multiplicador de enemigos (1 = 12 cangrejos, 4 zonas de gaviotas y 4 tanques por tanda). */
		float EnemyScale = 1.f;
	};

	struct FPhase
	{
		EStep Step = EStep::Baseline;
		float Start = 0.f;
		float End = 0.f;
		/** Tareas permitidas (máscara de TaskBit). */
		uint8 Tasks = 0;
		/** Lo que se crea al empezar la fase (se suma a lo de las anteriores). */
		int32 Crabs = 0;
		int32 Gulls = 0;
		int32 Tanks = 0;
		/** Segundos entre dos objetos lanzados por la misma tortuga (0 = no lanza). */
		float ItemEverySeconds = 0.f;
	};

	/** Enemigos de una tanda con el multiplicador (al menos uno de cada si la escala es positiva). */
	inline int32 Scaled(int32 Base, float Scale)
	{
		return Scale <= 0.f ? 0 : FMath::Max(1, FMath::RoundToInt(static_cast<float>(Base) * Scale));
	}

	/**
	 * Seis fases de PhaseSeconds: referencia (solo andan), + coger y lanzar, + bola, + ráfagas de objetos, + enemigos y pico
	 * (el doble de enemigos y ráfagas el doble de seguidas). Cada fase permite lo de las anteriores.
	 */
	inline TArray<FPhase> BuildTimeline(const FConfig& Config)
	{
		TArray<FPhase> Phases;
		const float Each = FMath::Max(1.f, Config.PhaseSeconds);
		uint8 Mask = TaskBit(ETask::Wander);
		for (int32 Index = 0; Index < static_cast<int32>(EStep::Count); ++Index)
		{
			FPhase Phase;
			Phase.Step = static_cast<EStep>(Index);
			switch (Phase.Step)
			{
				case EStep::Carry: Mask |= TaskBit(ETask::Carry); break;
				case EStep::Ball:  Mask |= TaskBit(ETask::Ball); break;
				case EStep::Items: Mask |= TaskBit(ETask::Items); break;
				case EStep::Enemies:
				case EStep::Peak:
					Phase.Crabs = Scaled(12, Config.EnemyScale);
					Phase.Gulls = Scaled(4, Config.EnemyScale);
					Phase.Tanks = Scaled(4, Config.EnemyScale);
					break;
				default: break;
			}
			Phase.Tasks = Mask;
			if (HasTask(Mask, ETask::Items))
			{
				Phase.ItemEverySeconds = Phase.Step == EStep::Peak ? 0.6f : 1.2f;
			}
			Phase.Start = Each * static_cast<float>(Index);
			Phase.End = Each * static_cast<float>(Index + 1);
			Phases.Add(Phase);
		}
		return Phases;
	}

	/** La tarea que más sale en una fase (la que estrena o, con los enemigos y en el pico, la más cara; Count = ninguna). */
	inline ETask FocusOf(EStep Step)
	{
		switch (Step)
		{
			case EStep::Carry:   return ETask::Carry;
			case EStep::Ball:    return ETask::Ball;
			case EStep::Items:   return ETask::Items;
			// Con los enemigos y en el pico se insiste en lo más caro: la bola y coger y lanzar.
			case EStep::Enemies: return ETask::Ball;
			case EStep::Peak:    return ETask::Carry;
			default:             return ETask::Count;
		}
	}

	/**
	 * Siguiente tarea de una tortuga entre las permitidas. La que estrena la fase (Focus) pesa más, porque es lo que se mide en
	 * ella, y coger y lanzar solo sale si hay pareja. Siempre devuelve una permitida (Wander si no hay otra).
	 */
	inline ETask PickTask(FRandomStream& Stream, uint8 Mask, bool bCanCarry, ETask Focus = ETask::Count)
	{
		struct FWeight { ETask Task; int32 Weight; };
		FWeight Weights[] = { { ETask::Wander, 1 }, { ETask::Carry, 3 }, { ETask::Ball, 2 }, { ETask::Items, 3 } };
		for (FWeight& Item : Weights)
		{
			Item.Weight += Item.Task == Focus ? 8 : 0;
		}
		int32 Total = 0;
		for (const FWeight& Item : Weights)
		{
			const bool bAllowed = HasTask(Mask, Item.Task) && (Item.Task != ETask::Carry || bCanCarry);
			Total += bAllowed ? Item.Weight : 0;
		}
		if (Total <= 0)
		{
			return ETask::Wander;
		}
		int32 Roll = Stream.RandRange(0, Total - 1);
		for (const FWeight& Item : Weights)
		{
			const bool bAllowed = HasTask(Mask, Item.Task) && (Item.Task != ETask::Carry || bCanCarry);
			if (!bAllowed)
			{
				continue;
			}
			if (Roll < Item.Weight)
			{
				return Item.Task;
			}
			Roll -= Item.Weight;
		}
		return ETask::Wander;
	}

	/** Tiempo mínimo entre dos pulsaciones del caparazón de una misma tortuga (s). */
	constexpr float ShellPressInterval = 1.f;

	/**
	 * Pulsaciones del caparazón espaciadas: en un cliente IsInShell llega por réplica con retraso, y pulsar cada fotograma
	 * mientras no se ve el cambio haría entrar y salir de la bola sin parar.
	 */
	struct FShellGate
	{
		float SincePress = TNumericLimits<float>::Max();

		void Tick(float DeltaTime) { SincePress = FMath::Min(SincePress + DeltaTime, TNumericLimits<float>::Max()); }

		bool TryPress()
		{
			if (SincePress < ShellPressInterval)
			{
				return false;
			}
			SincePress = 0.f;
			return true;
		}
	};

	/** «caos» (sin distinguir mayúsculas). */
	inline bool IsChaosName(const FString& Name)
	{
		return Name.Equals(TEXT("caos"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("chaos"), ESearchCase::IgnoreCase);
	}
}
