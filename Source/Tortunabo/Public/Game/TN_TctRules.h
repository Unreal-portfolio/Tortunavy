#pragma once

#include "CoreMinimal.h"

/**
 * Reglas de Todos contra Todos (ATN_TctGameMode, #651) como funciones PURAS, igual que TN_SurvivalRules.h: sin mundo ni
 * actores, para que los tests (Tortunabo.Tct.*) prueben exactamente lo que corre en juego.
 *
 * Una partida es una serie de rondas de supervivencia de 2 a 8 tortugas: gana la ronda la última en pie (caer al agua, a una
 * zona de muerte o fuera del mapa elimina) y la partida, la primera que llega a WinsToWin rondas ganadas. El agua sube por
 * escalones durante la ronda (FTNTctFloodPlan, un escalón cada 24 s) y, al final, todo el mapa se inunda despacio (muerte
 * súbita: aguanta más quien está más alto).
 */

/**
 * Ritmo del agua por defecto (#778, director 05-10: «que el agua suba más despacio»). Con una arena de MaxSteps pisos, la
 * muerte súbita empieza a los StartDelay + MaxSteps × StepSeconds = 25 + 4 × 24 = 121 s y cubre la cima a los 121 + 40 =
 * 161 s (antes, 15 + 4 × 17 = 83 s y 108 s). El tiempo máximo de la ronda deja que el agua llegue arriba con margen.
 */
namespace TNTctFloodDefaults
{
	inline constexpr float StartDelay = 25.f;
	inline constexpr float StepSeconds = 24.f;
	inline constexpr float RiseSeconds = 7.f;
	inline constexpr float SuddenDeathRiseSeconds = 40.f;
	inline constexpr int32 MaxSteps = 4;
	inline constexpr float RoundTimeLimitSeconds = 180.f;
}

/**
 * El agua de TcT es veneno (#831): no mata al tocarla; intoxica mientras se está dentro (los pies bajo la superficie) y se
 * recupera fuera. Una tortuga limpia aguanta 1 / Rate = 5 s de agua seguidos; al llegar a 1, queda eliminada.
 */
namespace TNTctPoisonDefaults
{
	/** Nivel (0-1) que sube por segundo dentro del agua. */
	inline constexpr float Rate = 0.2f;
	/** Nivel que baja por segundo fuera del agua. */
	inline constexpr float Recovery = 0.07f;
	/** Con el flotador sin estrenar, salta al llegar a este nivel (no se gasta por mojarse los pies). */
	inline constexpr float FloatTrigger = 0.5f;
	/** Segundos de aviso (cuenta atrás y marca del nivel) antes de cada subida. */
	inline constexpr float WarnSeconds = 5.f;
	/** Segundos antes de cada subida en que empieza a asomar la línea de espuma y algas de la orilla futura (#920). */
	inline constexpr float ShoreWarnLead = 20.f;
	/** Fundido del tinte verde de la vista dentro del agua venenosa: segundos en entrar y en salir (#920). */
	inline constexpr float VisionFadeIn = 0.4f;
	inline constexpr float VisionFadeOut = 0.9f;
}

/**
 * Intoxicación de una tortuga como una recta (replicable una vez por cambio): el nivel al hora T0 y lo que sube (+) o baja (-)
 * por segundo. Cada máquina calcula el nivel de ahora con la hora del servidor (TNTctRules::PoisonLevel), sin réplica por
 * fotograma.
 */
struct FTNTctPoison
{
	float Level0 = 0.f;
	double T0 = 0.0;
	float Rate = 0.f;
};

/** Lo que dice el plan del agua en un instante (TNTctRules::NextRise): la próxima subida y si el agua está subiendo. */
struct FTNTctNextRise
{
	/** Hay una subida por venir (si no, el agua ya lo ha cubierto todo o no hay ronda). */
	bool bUpcoming = false;
	/** Índice de la subida por venir (Levels.Num() = la muerte súbita). */
	int32 Step = INDEX_NONE;
	/** Subidas del plan contando la muerte súbita («tramo Step + 1 de Tiers»). */
	int32 Tiers = 0;
	float SecondsLeft = -1.f;
	/** Altura a la que llegará el agua con esa subida. */
	float TargetZ = 0.f;
	bool bSuddenDeath = false;
	/** El agua está subiendo ahora (entre el inicio y el final de una subida); RisingTargetZ, hasta dónde. */
	bool bRising = false;
	float RisingTargetZ = 0.f;
};

/** Lo que el GameMode sabe de cada tortuga para decidir. */
struct FTNTctFighter
{
	int32 Id = INDEX_NONE;
	/** Sigue en pie en la ronda en curso. */
	bool bAlive = true;
	/** Sigue en la partida (quien se ha ido no cuenta para la ronda ni para el campeonato). */
	bool bConnected = true;
	/** Rondas ganadas en la partida. */
	int32 Wins = 0;
};

enum class ETNTctRoundOutcome : uint8
{
	/** Quedan dos o más en pie (o una sola en una partida de una). */
	Continue,
	/** Queda una en pie: gana la ronda (WinnerId). */
	Winner,
	/** No queda nadie en pie (caídas a la vez) o se acabó el tiempo: nadie gana la ronda. */
	Draw,
};

struct FTNTctRoundDecision
{
	ETNTctRoundOutcome Outcome = ETNTctRoundOutcome::Continue;
	int32 WinnerId = INDEX_NONE;
};

enum class ETNTctMatchOutcome : uint8
{
	/** Nadie ha llegado a las rondas para ganar: otra ronda. */
	NextRound,
	/** Hay campeona (ChampionId): la que llegó a las rondas para ganar o la única que queda en la partida. */
	Champion,
	/** Ya no queda nadie en la partida. */
	Abandoned,
};

struct FTNTctMatchDecision
{
	ETNTctMatchOutcome Outcome = ETNTctMatchOutcome::NextRound;
	int32 ChampionId = INDEX_NONE;
};

/**
 * Subida del agua de una ronda: escalones (Levels, de abajo arriba) cada StepSeconds desde StartDelay, cada uno subiendo en
 * RiseSeconds, y después la muerte súbita hasta SuddenDeathZ en SuddenDeathRiseSeconds. Los tiempos cuentan desde la salida.
 */
struct FTNTctFloodPlan
{
	float BaseZ = 0.f;
	TArray<float> Levels;
	float SuddenDeathZ = 0.f;
	float StartDelay = TNTctFloodDefaults::StartDelay;
	float StepSeconds = TNTctFloodDefaults::StepSeconds;
	float RiseSeconds = TNTctFloodDefaults::RiseSeconds;
	float SuddenDeathRiseSeconds = TNTctFloodDefaults::SuddenDeathRiseSeconds;
};

/** Por qué cae una tortuga en la ronda (TNTctRules::FallCause). */
enum class ETNTctFall : uint8
{
	None,
	/** Los pies bajo el agua (lo único de lo que salva el flotador). */
	Water,
	/** Por debajo de la arena o lejos de ella en horizontal. */
	OutOfArena,
};

/** El flotador de una tortuga (#777): si lo lleva y hasta cuándo flota o está a salvo del agua (hora del servidor, s). */
struct FTNTctFloatState
{
	bool bHasFloat = false;
	double FloatEnd = -1.0;
	double SafeUntil = -1.0;
};

/** Un cuerpo para mirar si sigue dentro de la arena (centro de la cápsula y su media altura). */
struct FTNTctBody
{
	FVector Location = FVector::ZeroVector;
	float HalfHeight = 90.f;
};

/** Límites de la arena: caja del terreno con colisión, margen fuera de ella y lo que se tolera de pie en el agua. */
struct FTNTctArenaBounds
{
	FVector Min = FVector::ZeroVector;
	FVector Max = FVector::ZeroVector;
	/** Distancia horizontal fuera de la caja que ya es «fuera del mapa». */
	float OutMargin = 4000.f;
	/** Cuánto por debajo del agua pueden quedar los pies sin que cuente como caída (vadear la orilla). */
	float WadeDepth = 20.f;
	/** Por debajo de Min.Z menos esto, fuera del mapa aunque no haya agua. */
	float FallDepth = 3000.f;
};

namespace TNTctRules
{
	inline constexpr int32 MinPlayers = 2;
	inline constexpr int32 MaxPlayers = 8;

	/**
	 * Qué pasa con la ronda tras un cambio (eliminación, salida de una jugadora o fin del tiempo).
	 * @param Fighters         Las tortugas de la partida (las desconectadas no cuentan).
	 * @param StartingPlayers  Las que empezaron la ronda: con una sola (prueba en solitario) la ronda dura hasta que cae.
	 * @param bTimeUp          Se ha acabado el tiempo de la ronda.
	 */
	inline FTNTctRoundDecision DecideRound(const TArray<FTNTctFighter>& Fighters, int32 StartingPlayers, bool bTimeUp)
	{
		FTNTctRoundDecision Result;
		int32 Alive = 0;
		const FTNTctFighter* LastAlive = nullptr;
		for (const FTNTctFighter& Fighter : Fighters)
		{
			if (Fighter.bConnected && Fighter.bAlive)
			{
				++Alive;
				LastAlive = &Fighter;
			}
		}
		if (StartingPlayers >= MinPlayers && Alive == 1)
		{
			Result.Outcome = ETNTctRoundOutcome::Winner;
			Result.WinnerId = LastAlive->Id;
			return Result;
		}
		if (Alive == 0 || bTimeUp)
		{
			Result.Outcome = ETNTctRoundOutcome::Draw;
		}
		return Result;
	}

	/**
	 * Qué pasa con la partida al cerrar una ronda (las rondas ganadas ya contadas en Fighters).
	 * @param bSolo  La partida empezó con una sola tortuga (prueba): no acaba por quedarse sola.
	 */
	inline FTNTctMatchDecision DecideMatch(const TArray<FTNTctFighter>& Fighters, int32 WinsToWin, bool bSolo)
	{
		FTNTctMatchDecision Result;
		int32 Connected = 0;
		const FTNTctFighter* OnlyConnected = nullptr;
		for (const FTNTctFighter& Fighter : Fighters)
		{
			if (!Fighter.bConnected)
			{
				continue;
			}
			++Connected;
			OnlyConnected = &Fighter;
			// Solo una puede ganar cada ronda: la primera que llega a las rondas para ganar es única.
			if (Fighter.Wins >= FMath::Max(1, WinsToWin) && Result.ChampionId == INDEX_NONE)
			{
				Result.Outcome = ETNTctMatchOutcome::Champion;
				Result.ChampionId = Fighter.Id;
			}
		}
		if (Result.Outcome == ETNTctMatchOutcome::Champion)
		{
			return Result;
		}
		if (Connected == 0)
		{
			Result.Outcome = ETNTctMatchOutcome::Abandoned;
		}
		else if (Connected == 1 && !bSolo)
		{
			// Las demás se han ido: la que queda gana la partida.
			Result.Outcome = ETNTctMatchOutcome::Champion;
			Result.ChampionId = OnlyConnected->Id;
		}
		return Result;
	}

	/** Podio: la campeona primero; las demás por rondas ganadas y, a igualdad, por orden de llegada (Id). */
	inline TArray<int32> RankFighters(const TArray<FTNTctFighter>& Fighters, int32 ChampionId)
	{
		TArray<FTNTctFighter> Sorted = Fighters;
		Sorted.Sort([ChampionId](const FTNTctFighter& A, const FTNTctFighter& B)
		{
			if ((A.Id == ChampionId) != (B.Id == ChampionId)) { return A.Id == ChampionId; }
			if (A.Wins != B.Wins) { return A.Wins > B.Wins; }
			return A.Id < B.Id;
		});
		TArray<int32> Ids;
		for (const FTNTctFighter& Fighter : Sorted) { Ids.Add(Fighter.Id); }
		return Ids;
	}

	/**
	 * Escalones de la subida del agua a partir de las alturas del suelo pisable de la arena. Las alturas se cuentan en franjas
	 * de TierTolerance; una franja con al menos MinTierFraction del suelo es un piso (las rampas y taludes, con poco suelo por
	 * franja, no cuentan) y cada escalón inunda un piso entero (su punto más alto más Margin). Nunca se inunda tanto que quede
	 * menos de KeepFraction del suelo seco: eso lo hace la muerte súbita. Con más pisos que MaxSteps se reparten los escalones
	 * entre ellos (el más alto siempre se queda). Sin pisos que inundar (arena plana), vacío.
	 */
	inline TArray<float> ComputeFloodLevels(TArray<float> Heights, int32 MaxSteps, float KeepFraction, float TierTolerance, float Margin,
		float MinTierFraction = 0.03f)
	{
		TArray<float> Levels;
		if (Heights.Num() == 0 || MaxSteps <= 0 || TierTolerance <= 0.f)
		{
			return Levels;
		}
		Heights.Sort();
		const int32 Num = Heights.Num();
		const float MaxFlooded = 1.f - FMath::Clamp(KeepFraction, 0.f, 1.f);
		TArray<float> Candidates;
		int32 BinStart = 0;
		while (BinStart < Num)
		{
			const int32 Bin = FMath::FloorToInt(Heights[BinStart] / TierTolerance);
			int32 BinEnd = BinStart;
			while (BinEnd + 1 < Num && FMath::FloorToInt(Heights[BinEnd + 1] / TierTolerance) == Bin)
			{
				++BinEnd;
			}
			const int32 InBin = BinEnd - BinStart + 1;
			const int32 First = BinStart;
			BinStart = BinEnd + 1;
			if (static_cast<float>(InBin) / static_cast<float>(Num) < MinTierFraction)
			{
				continue;
			}
			// La altura del piso: la del 90 % de su franja (las pocas muestras de rampa de encima no lo suben).
			const float Level = Heights[First + FMath::FloorToInt(0.9f * static_cast<float>(InBin - 1))] + Margin;
			// Lo que queda bajo el agua con este escalón: todo el suelo hasta Level (también lo de franjas siguientes).
			int32 Flooded = BinEnd + 1;
			while (Flooded < Num && Heights[Flooded] <= Level)
			{
				++Flooded;
			}
			if (static_cast<float>(Flooded) / static_cast<float>(Num) > MaxFlooded + KINDA_SMALL_NUMBER)
			{
				break;
			}
			if (Candidates.Num() > 0 && Level - Candidates.Last() < TierTolerance)
			{
				// Un piso partido entre dos franjas (p. ej. a la altura 0, con muestras a -1 y a +1): un solo escalón, el alto.
				Candidates.Last() = FMath::Max(Candidates.Last(), Level);
			}
			else
			{
				Candidates.Add(Level);
			}
		}
		if (Candidates.Num() <= MaxSteps)
		{
			return Candidates;
		}
		// Más pisos que escalones: se reparten de arriba abajo, siempre con el más alto.
		for (int32 Step = 1; Step <= MaxSteps; ++Step)
		{
			const int32 Index = FMath::Clamp(FMath::RoundToInt(static_cast<float>(Step * Candidates.Num()) / MaxSteps) - 1, 0, Candidates.Num() - 1);
			if (Levels.Num() == 0 || Candidates[Index] > Levels.Last())
			{
				Levels.Add(Candidates[Index]);
			}
		}
		return Levels;
	}

	/** Segundo (desde la salida) en que empieza a subir el escalón Step (Levels.Num() = la muerte súbita). */
	inline float StepStartSeconds(const FTNTctFloodPlan& Plan, int32 Step)
	{
		return Plan.StartDelay + static_cast<float>(Step) * Plan.StepSeconds;
	}

	/** Segundo (desde la salida) en que la muerte súbita termina de cubrir la arena. */
	inline float FloodTopSeconds(const FTNTctFloodPlan& Plan)
	{
		return StepStartSeconds(Plan, Plan.Levels.Num()) + Plan.SuddenDeathRiseSeconds;
	}

	/** La próxima subida del plan a los Elapsed segundos de la salida y, si el agua sube ahora, hasta dónde (#831). */
	inline FTNTctNextRise NextRise(const FTNTctFloodPlan& Plan, float Elapsed)
	{
		FTNTctNextRise Result;
		Result.Tiers = Plan.Levels.Num() + 1;
		for (int32 Step = 0; Step < Result.Tiers; ++Step)
		{
			const float Start = StepStartSeconds(Plan, Step);
			const bool bSuddenDeath = Step == Plan.Levels.Num();
			const float Target = bSuddenDeath ? Plan.SuddenDeathZ : Plan.Levels[Step];
			const float Rise = FMath::Max(0.01f, bSuddenDeath ? Plan.SuddenDeathRiseSeconds : Plan.RiseSeconds);
			if (Elapsed >= Start && Elapsed < Start + Rise)
			{
				Result.bRising = true;
				Result.RisingTargetZ = Target;
			}
			if (!Result.bUpcoming && Start > Elapsed)
			{
				Result.bUpcoming = true;
				Result.Step = Step;
				Result.SecondsLeft = Start - Elapsed;
				Result.TargetZ = Target;
				Result.bSuddenDeath = bSuddenDeath;
			}
		}
		return Result;
	}

	/**
	 * Hora de salida nueva tras retrasar el agua Seconds (el tapón de marea, #830): el reloj del agua cuenta desde la salida, así
	 * que retrasar la salida baja el agua lo que había subido en esos segundos y aplaza lo que falta. Nunca en el futuro.
	 */
	inline float DelayedFloodStart(float StartServerTime, float Seconds, float Now)
	{
		return FMath::Min(StartServerTime + FMath::Max(0.f, Seconds), Now);
	}

	/** Cuánto va la ronda por el agua (0 = sin empezar a subir, 1 = en la marea final), para dar mejores objetos según avanza (#830). */
	inline float RoundProgress(const FTNTctFloodPlan& Plan, float Elapsed)
	{
		const float Span = FMath::Max(1.f, StepStartSeconds(Plan, Plan.Levels.Num()) - Plan.StartDelay);
		return FMath::Clamp((Elapsed - Plan.StartDelay) / Span, 0.f, 1.f);
	}

	/**
	 * Cuánto se ha dejado ver ya el aviso diegético de la orilla (#920), de 0 a 1: nada hasta ShoreWarnLead segundos antes de la
	 * próxima subida, de ahí crece hasta 1 al llegar; mientras el agua sube, entera. Sin ronda o sin más subidas, 0.
	 */
	inline float ShoreWarnProgress(const FTNTctNextRise& Next)
	{
		if (Next.bRising)
		{
			return 1.f;
		}
		if (!Next.bUpcoming || Next.SecondsLeft > TNTctPoisonDefaults::ShoreWarnLead)
		{
			return 0.f;
		}
		return FMath::Clamp(1.f - Next.SecondsLeft / TNTctPoisonDefaults::ShoreWarnLead, 0.f, 1.f);
	}

	/** Cota a la que se marca la orilla futura: la del escalón que sube ahora o, si no, la del siguiente (la misma que el HUD). */
	inline float ShoreWarnTargetZ(const FTNTctNextRise& Next)
	{
		return Next.bRising ? Next.RisingTargetZ : Next.TargetZ;
	}

	/** Un paso del fundido del tinte verde de la vista (0-1): entra en VisionFadeIn s dentro del agua y sale en VisionFadeOut s. */
	inline float PoisonVisionStep(float Fade, bool bInside, float DeltaSeconds)
	{
		const float Step = DeltaSeconds / (bInside ? TNTctPoisonDefaults::VisionFadeIn : TNTctPoisonDefaults::VisionFadeOut);
		return FMath::Clamp(Fade + (bInside ? Step : -Step), 0.f, 1.f);
	}

	/** Peso del postproceso verde: suave al entrar y algo más fuerte cuanto más intoxicada va (Level 0-1). */
	inline float PoisonVisionWeight(float Fade, float Level)
	{
		return FMath::Clamp(Fade, 0.f, 1.f) * (0.4f + 0.35f * FMath::Clamp(Level, 0.f, 1.f));
	}

	/** Nivel de intoxicación (0-1) a la hora Now. */
	inline float PoisonLevel(const FTNTctPoison& Poison, double Now)
	{
		return FMath::Clamp(Poison.Level0 + Poison.Rate * static_cast<float>(FMath::Max(0.0, Now - Poison.T0)), 0.f, 1.f);
	}

	/** Cambia lo que sube o baja (por segundo) a partir de Now, conservando el nivel de ahora. */
	inline void SetPoisonRate(FTNTctPoison& Poison, double Now, float NewRate)
	{
		Poison.Level0 = PoisonLevel(Poison, Now);
		Poison.T0 = Now;
		Poison.Rate = NewRate;
	}

	/** Lo que cambia el nivel por segundo: sube dentro del agua (por Scale, p. ej. las aletas) y baja fuera. */
	inline float PoisonRateFor(bool bPoisoned, float Scale = 1.f)
	{
		return bPoisoned ? TNTctPoisonDefaults::Rate * FMath::Max(0.f, Scale) : -TNTctPoisonDefaults::Recovery;
	}

	/** Altura del agua a los Elapsed segundos de la salida (BaseZ antes de empezar a subir). */
	inline float WaterZAt(const FTNTctFloodPlan& Plan, float Elapsed)
	{
		const int32 NumSteps = Plan.Levels.Num() + 1;
		float Z = Plan.BaseZ;
		for (int32 Step = 0; Step < NumSteps; ++Step)
		{
			const float Start = StepStartSeconds(Plan, Step);
			if (Elapsed < Start)
			{
				break;
			}
			const bool bSuddenDeath = Step == Plan.Levels.Num();
			const float Target = bSuddenDeath ? Plan.SuddenDeathZ : Plan.Levels[Step];
			const float Rise = FMath::Max(0.01f, bSuddenDeath ? Plan.SuddenDeathRiseSeconds : Plan.RiseSeconds);
			const float Alpha = FMath::Clamp((Elapsed - Start) / Rise, 0.f, 1.f);
			// Nunca baja: un escalón por debajo del anterior (no debería haberlo) se queda en el anterior.
			Z = FMath::Max(Z, FMath::Lerp(Z, Target, Alpha));
		}
		return Z;
	}

	/** Por qué cae el cuerpo: los pies en el agua, o por debajo de la arena o lejos de ella en horizontal. None si sigue. */
	inline ETNTctFall FallCause(const FTNTctBody& Body, const FTNTctArenaBounds& Bounds, float WaterZ)
	{
		const float FeetZ = Body.Location.Z - Body.HalfHeight;
		if (FeetZ < WaterZ - Bounds.WadeDepth)
		{
			return ETNTctFall::Water;
		}
		if (Body.Location.Z < Bounds.Min.Z - Bounds.FallDepth)
		{
			return ETNTctFall::OutOfArena;
		}
		const double DX = FMath::Max3(Bounds.Min.X - Body.Location.X, 0.0, Body.Location.X - Bounds.Max.X);
		const double DY = FMath::Max3(Bounds.Min.Y - Body.Location.Y, 0.0, Body.Location.Y - Bounds.Max.Y);
		return FMath::Sqrt(DX * DX + DY * DY) > static_cast<double>(Bounds.OutMargin) ? ETNTctFall::OutOfArena : ETNTctFall::None;
	}

	/** true si el cuerpo ya está fuera: los pies en el agua, por debajo de la arena o lejos de ella en horizontal. */
	inline bool ShouldEliminate(const FTNTctBody& Body, const FTNTctArenaBounds& Bounds, float WaterZ)
	{
		return FallCause(Body, Bounds, WaterZ) != ETNTctFall::None;
	}

	/**
	 * La regla del flotador (#777) ante una caída de Cause a la hora Now: true si la tortuga queda eliminada. El agua no
	 * elimina a quien lleva el flotador: se gasta, flota FloatSeconds (hasta State.FloatEnd) y aún tiene GraceSeconds de
	 * respiro tras el rescate (hasta State.SafeUntil). Caer fuera de la arena elimina siempre.
	 */
	inline bool ResolveFall(ETNTctFall Cause, FTNTctFloatState& State, double Now, float FloatSeconds, float GraceSeconds)
	{
		if (Cause == ETNTctFall::None)
		{
			return false;
		}
		if (Cause != ETNTctFall::Water)
		{
			return true;
		}
		if (Now < State.SafeUntil)
		{
			return false;
		}
		if (!State.bHasFloat)
		{
			return true;
		}
		State.bHasFloat = false;
		State.FloatEnd = Now + FloatSeconds;
		State.SafeUntil = State.FloatEnd + GraceSeconds;
		return false;
	}

	/**
	 * Count sitios de salida bien repartidos entre Candidates (índices). Para que nadie salga con ventaja, solo de la franja de
	 * fuera (a OuterBandFraction o más de la distancia del candidato más lejano a Center; si ahí no caben todas, de cualquier
	 * sitio): el primero, el más lejano de Center; cada siguiente, el que más lejos queda del más cercano de los ya elegidos.
	 * Determinista (a igualdad, el de índice menor). Con menos candidatos que Count, todos.
	 */
	inline TArray<int32> PickSpreadPoints(const TArray<FVector>& Candidates, int32 Count, const FVector& Center, float OuterBandFraction = 0.6f)
	{
		TArray<int32> Picked;
		const int32 Wanted = FMath::Min(Count, Candidates.Num());
		if (Wanted <= 0)
		{
			return Picked;
		}
		double MaxDist = 0.0;
		for (const FVector& Candidate : Candidates)
		{
			MaxDist = FMath::Max(MaxDist, FVector::Dist2D(Candidate, Center));
		}
		TArray<int32> Pool;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			if (FVector::Dist2D(Candidates[Index], Center) >= MaxDist * OuterBandFraction)
			{
				Pool.Add(Index);
			}
		}
		if (Pool.Num() < Wanted)
		{
			Pool.Reset();
			for (int32 Index = 0; Index < Candidates.Num(); ++Index) { Pool.Add(Index); }
		}

		TArray<double> Nearest;
		Nearest.Init(TNumericLimits<double>::Max(), Pool.Num());
		TArray<bool> Taken;
		Taken.Init(false, Pool.Num());
		int32 Next = 0;
		for (int32 Slot = 1; Slot < Pool.Num(); ++Slot)
		{
			if (FVector::DistSquared2D(Candidates[Pool[Slot]], Center) > FVector::DistSquared2D(Candidates[Pool[Next]], Center))
			{
				Next = Slot;
			}
		}
		while (Picked.Num() < Wanted)
		{
			Picked.Add(Pool[Next]);
			Taken[Next] = true;
			int32 Best = INDEX_NONE;
			for (int32 Slot = 0; Slot < Pool.Num(); ++Slot)
			{
				Nearest[Slot] = FMath::Min(Nearest[Slot], FVector::DistSquared2D(Candidates[Pool[Slot]], Candidates[Pool[Next]]));
				if (!Taken[Slot] && (Best == INDEX_NONE || Nearest[Slot] > Nearest[Best]))
				{
					Best = Slot;
				}
			}
			if (Best == INDEX_NONE)
			{
				break;
			}
			Next = Best;
		}
		return Picked;
	}
}
