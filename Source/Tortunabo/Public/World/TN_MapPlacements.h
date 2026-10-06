#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachTypes.h"

class FJsonObject;

/**
 * Bloque "placements" del manifest de una variante de terreno fijo (#652): lo genera Scripts/place_terrain_path.py
 * (formato en Scripts/terrain_path/placement_io.py) y lo coloca ATN_MapPlacementSpawner al cargar el mapa.
 *
 * Aquí solo se lee y se clasifica (puro, sin mundo): qué entradas valen, cuáles están suprimidas por los diseñadores,
 * qué pieza del juego crea cada (category, kind) y por dónde van las piezas de un tramo. Lo automático de un bloque
 * marcado "stale" (la semilla del terreno cambió desde la última colocación) no se coloca; lo manual, sí.
 */
namespace TNMapPlacements
{
	/** Qué crea el cargador para una entrada. */
	enum class ESpawn : uint8
	{
		/** ATN_BeachElement::SpawnElement (trampas, enemigos y lanzadores): servidor, replicado. */
		BeachElement,
		/** Pieza de ATN_BeachDecorField (decorado de la categoría Decor, también la pasarela): local en cada máquina. */
		Decor,
		/** Mata o palmera instanciada (mallas de flora del mapa procedural): local, sin colisión. */
		Vegetation,
		SearchSpot,
		ScoreShell,
		/** Charco de pesca (ATN_FishingPool): servidor, replicado. */
		FishingPool,
		/** Puzles: plate_balance, breakable_chain y wobbly_run. */
		PlateBalance,
		BreakableChain,
		WobblyRun,
		/** Sin pieza en el juego todavía (puzles pendientes, kinds desconocidos): se registra en el log y se salta. */
		Unsupported
	};

	struct FPlacement
	{
		FString Id;
		FString Category;
		FString Kind;
		/** "auto" o "manual". */
		FString Source;
		ESpawn Spawn = ESpawn::Unsupported;
		/** Elemento de la playa (BeachElement y Decor). */
		ETNBeachElement Element = ETNBeachElement::Coconut;
		/** Suelo del camino (uu) según el generador; el cargador ajusta la cota con una traza. */
		FVector Location = FVector::ZeroVector;
		double YawDeg = 0.0;
		/** Huella a lo largo del camino (cm; 0 = puntual) y Extent del elemento (cm). */
		double LengthCm = 0.0;
		double ExtentCm = 0.0;
		float SizeScale = 1.f;
		/** Línea del grafo y avance por ella (m), si los trae. */
		int32 Line = INDEX_NONE;
		double S = 0.0;
		/** Avance por el recorrido en metros del principal (progress_m), comparable entre líneas; < 0 si no lo trae. */
		double ProgressM = -1.0;
		/** Polilínea del camino bajo la huella (path_uu); vacía en lo puntual. */
		TArray<FVector> Path;
		/** Parámetros numéricos del puzle (params), con su valor por defecto del catálogo si faltan. */
		TMap<FString, double> Params;

		double Param(const TCHAR* Name, double Default) const
		{
			const double* Value = Params.Find(Name);
			return Value ? *Value : Default;
		}
	};

	struct FParseResult
	{
		TArray<FPlacement> Placements;
		/** El bloque existe (un manifest sin él no coloca nada y no es un error). */
		bool bHasBlock = false;
		/** Bloque marcado "stale": lo automático se ha saltado. */
		bool bStale = false;
		int32 Suppressed = 0;
		int32 SkippedStale = 0;
		int32 Invalid = 0;
		TArray<FString> Warnings;
	};

	/** Lee el bloque "placements" del manifest. False solo si el bloque existe y no tiene la forma esperada. */
	TORTUNABO_API bool ParseBlock(const FJsonObject& Manifest, FParseResult& Out);

	/** Lo que crea el cargador para (category, kind); OutElement, si es un elemento de la playa. */
	TORTUNABO_API ESpawn SpawnOf(const FString& Category, const FString& Kind, ETNBeachElement& OutElement);

	/** Elemento de la playa por su nombre corto ("Rock", "BarbedWire"...); false si no existe. */
	TORTUNABO_API bool ElementFromName(const FString& Name, ETNBeachElement& OutElement);

	/** Nombre de un ESpawn (log y recuentos). */
	TORTUNABO_API const TCHAR* SpawnName(ESpawn Spawn);

	/**
	 * Punto a la fracción Alpha (0-1) de la huella de P, por su polilínea si la trae o en recta a lo largo de su yaw si no
	 * (centrada en Location, LengthCm de largo). OutYawDeg, la dirección del camino en ese punto.
	 */
	TORTUNABO_API FVector PointAlong(const FPlacement& P, double Alpha, double& OutYawDeg);
}
