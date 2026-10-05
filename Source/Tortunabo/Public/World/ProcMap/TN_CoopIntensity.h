#pragma once

#include "CoreMinimal.h"

/**
 * Tabla de intensidad del cooperativo (#788, GDD oficial: Excel_DayT, hoja IntensityData).
 *
 * Dos tablas en un fichero de texto versionado (Content/Data/Coop/IntensityTable.json, sin .uasset):
 *   - Rondas x tramos: qué dificultad toca a cada tramo de cada ronda (Fácil, Medio, Difícil o Puzle).
 *   - Módulos de diseño (1-40): su dificultad, su intensidad y hasta cinco enemigos con su cantidad.
 *
 * El generador del mapa procedural (ATN_ProcMapGenerator) parte el recorrido principal en tramos iguales, pregunta la
 * dificultad de cada uno a la tabla y elige, con la semilla del mapa, un módulo de diseño de esa dificultad: sus
 * enemigos se colocan en el tramo y los peligros por bioma se filtran por la dificultad del tramo (un tramo Puzle no
 * lleva enemigos). Los enemigos de la tabla que el juego no tiene se ignoran con un aviso en el log.
 *
 * Lógica pura (sin mundo ni UObject): la prueban Tortunabo.ProcMap.CoopIntensity.*.
 */
namespace TNCoopIntensity
{
	enum class EDifficulty : uint8
	{
		Easy,
		Medium,
		Hard,
		Puzzle,
	};

	/** Tramos por ronda si el fichero no dice otra cosa. */
	constexpr int32 DEFAULT_TRAMOS = 5;

	/** Un enemigo de un módulo de diseño y cuántos lleva. */
	struct FEnemyCount
	{
		FString Name;
		int32 Count = 0;
	};

	/** Un módulo de diseño de la tabla (1-40). */
	struct FModuleRow
	{
		int32 Id = INDEX_NONE;
		EDifficulty Difficulty = EDifficulty::Easy;
		int32 Intensity = 0;
		TArray<FEnemyCount> Enemies;
	};

	/** Las dos tablas del fichero. */
	struct FTable
	{
		int32 Version = 0;
		int32 NumTramos = DEFAULT_TRAMOS;
		/** Rounds[r][t]: dificultad del tramo t (desde 0) de la ronda r + 1. */
		TArray<TArray<EDifficulty>> Rounds;
		TArray<FModuleRow> Modules;

		bool IsValid() const { return Rounds.Num() > 0 && Modules.Num() > 0 && NumTramos > 0; }
	};

	/** Lo que el generador hace en un tramo de la ronda. */
	struct FTramoPlan
	{
		EDifficulty Difficulty = EDifficulty::Easy;
		/** Módulo de diseño elegido (INDEX_NONE si la tabla no tiene ninguno de esa dificultad). */
		int32 ModuleId = INDEX_NONE;
		int32 Intensity = 0;
		TArray<FEnemyCount> Enemies;
	};

	/** Enemigos de la tabla que el juego sabe colocar en el mapa procedural. */
	enum class EKnownEnemy : uint8
	{
		None,
		/** «Algas»: ATN_BeachSeaweed (enredan y frenan; sin daño). */
		Seaweed,
	};

	/** «Fácil», «Facil», «Medio», «Difícil», «Dificil» o «Puzle» (sin distinguir mayúsculas ni tildes). */
	bool ParseDifficulty(const FString& Name, EDifficulty& Out);

	/** Nombre para el log («Fácil», «Medio», «Difícil», «Puzle»). */
	const TCHAR* DifficultyName(EDifficulty Difficulty);

	/** Lee el JSON de la tabla. false (y OutError) si falta algo o una dificultad no se entiende. */
	bool ParseTable(const FString& JsonText, FTable& Out, FString& OutError);

	/** Ruta del fichero versionado (Content/Data/Coop/IntensityTable.json). */
	FString DefaultTablePath();

	/**
	 * La tabla del fichero versionado, leída una vez y guardada. nullptr si no existe o no se entiende (se avisa en el log
	 * una vez): el generador coloca entonces como antes de la tabla.
	 */
	const FTable* GetDefaultTable();

	/**
	 * Dificultad del tramo Tramo (desde 0) de la ronda Round (desde 1). Las rondas fuera de la tabla usan la más cercana
	 * (una ronda 7 juega como la última); los tramos fuera de rango, el más cercano.
	 */
	EDifficulty DifficultyFor(const FTable& Table, int32 Round, int32 Tramo);

	/** Tramo (0..NumTramos-1) del paso Step de un recorrido de NumSteps pasos, en partes iguales. */
	int32 TramoOfStep(int32 Step, int32 NumSteps, int32 NumTramos);

	/** Un módulo de diseño de esa dificultad, elegido con Seed (siempre el mismo con la misma semilla). */
	const FModuleRow* PickModule(const FTable& Table, EDifficulty Difficulty, uint32 Seed);

	/** Plan de la ronda: dificultad, módulo de diseño, intensidad y enemigos de cada tramo. */
	TArray<FTramoPlan> PlanRound(const FTable& Table, int32 Round, uint32 Seed);

	/** Qué enemigo del juego es un nombre de la tabla (EKnownEnemy::None si el juego no lo tiene). */
	EKnownEnemy ResolveEnemy(const FString& Name);

	/** Nombres del plan que el juego no tiene, sin repetir (para el aviso del log). */
	TArray<FString> UnknownEnemies(const TArray<FTramoPlan>& Plan);

	/**
	 * Si un peligro por bioma va en un tramo de esta dificultad. Lo que no es enemigo (objetos, conchas, fauna de agua)
	 * va siempre. Los enemigos: nunca en Puzle; en Fácil solo los que ya salían en dificultad fácil (MinDifficulty 0), en
	 * Medio hasta los de normal (1) y en Difícil todos.
	 */
	bool AllowsHazard(EDifficulty Tramo, bool bEnemy, int32 MinDifficulty);
}
