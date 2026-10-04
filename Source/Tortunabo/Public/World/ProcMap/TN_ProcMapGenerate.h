#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapLayout.h"
#include "World/ProcMap/TN_ProcMapModules.h"
#include "World/ProcMap/TN_ProcMapRoute.h"
#include "World/ProcMap/TN_ProcMapPath.h"
#include "World/ProcMap/TN_ProcMapFeatures.h"
#include "World/ProcMap/TN_ProcMapFormations.h"
#include "World/ProcMap/TN_ProcMapCaves.h"
#include "World/ProcMap/TN_ProcMapTerrain.h"

/**
 * Punto de entrada de la generación PURA del mapa. Misma semilla y parámetros →
 * mismo layout en cualquier máquina: en red basta con replicar FGenParams.
 *
 * Cada fase recibe su propio sub-generador (FRng::Fork) para que ajustar una fase
 * no cambie el resultado de las siguientes con la misma semilla.
 */

namespace TNProcMap
{
	inline FGenParams SanitizeParams(const FGenParams& In)
	{
		FGenParams P = In;
		if (P.bDrivable)
		{
			// Karts: sin lo que el kart no puede recorrer (FGenParams::bDrivable). Los desniveles grandes entre módulos se
			// quedan como en el cooperativo: se suben en géiser y se bajan por la cascada (#293).
			P.NumCrossings = 0;
			P.NumBranches = 0;
			P.NumLanes = 0;
			P.GapsPerKm = 0.0;
			P.NarrowChance *= 0.5;
			P.PathWidthMin = FMath::Max(P.PathWidthMin, DrivableMinPathWidth);
			P.PathWidthMax = FMath::Max(P.PathWidthMax, P.PathWidthMin);
			P.PortalWidthMin = FMath::Max(P.PortalWidthMin, DrivableMinPathWidth + 200.0);
			P.PortalWidthMax = FMath::Max(P.PortalWidthMax, P.PortalWidthMin);
		}
		P.GridSize = FMath::Clamp(P.GridSize, 1, 10);
		P.GridSizeX = FMath::Clamp(P.GridSizeX, 0, 10);
		P.ModuleSize = FMath::Clamp(P.ModuleSize, 3000.0, 80000.0);
		P.CellSize = FMath::Clamp(P.CellSize, 200.0, 1600.0);
		P.Coverage = FMath::Clamp(P.Coverage, 0.1, 1.0);
		P.NumCrossings = FMath::Clamp(P.NumCrossings, 0, 8);
		P.NumBranches = FMath::Clamp(P.NumBranches, 0, 32);
		P.NumLanes = FMath::Clamp(P.NumLanes, 0, 8);
		P.BranchMaxModules = FMath::Clamp(P.BranchMaxModules, 1, 3);
		P.PathWidthMin = FMath::Clamp(P.PathWidthMin, 300.0, 3000.0);
		P.PathWidthMax = FMath::Clamp(P.PathWidthMax, P.PathWidthMin, 6000.0);
		P.PortalWidthMin = FMath::Clamp(P.PortalWidthMin, P.PathWidthMin, P.PathWidthMax);
		P.PortalWidthMax = FMath::Clamp(P.PortalWidthMax, P.PortalWidthMin, P.PathWidthMax);
		P.Sinuosity = FMath::Clamp(P.Sinuosity, 1.0, 3.0);
		P.SampleSpacing = FMath::Clamp(P.SampleSpacing, 200.0, 1000.0);
		P.GapMin = FMath::Clamp(P.GapMin, 50.0, 500.0);
		P.GapMax = FMath::Clamp(P.GapMax, P.GapMin, 500.0);
		P.IsletGapMin = FMath::Clamp(P.IsletGapMin, 50.0, 500.0);
		P.IsletGapMax = FMath::Clamp(P.IsletGapMax, P.IsletGapMin, 500.0);
		P.MaxPathSlope = FMath::Clamp(P.MaxPathSlope, 0.02, 0.6);
		P.SlideAngleDeg = FMath::Clamp(P.SlideAngleDeg, 46.0, 75.0);
		P.ColossalHeightMin = FMath::Clamp(P.ColossalHeightMin, 1500.0, 12000.0);
		P.ColossalHeightMax = FMath::Clamp(P.ColossalHeightMax, P.ColossalHeightMin, 15000.0);
		P.Difficulty01 = FMath::Clamp(P.Difficulty01, 0.0, 1.0);
		P.EggNestEveryNPortals = FMath::Clamp(P.EggNestEveryNPortals, 1, 10);
		// Módulos pequeños: bordes y márgenes proporcionales para que quepa el camino.
		const double Scale = P.ModuleSize / 40000.0;
		if (Scale < 1.0)
		{
			P.MapEdgeClearance *= Scale;
			P.WallInsetMin *= Scale;
			P.WallInsetMax *= Scale;
			P.CoastInset *= FMath::Max(0.5, Scale);
			P.StartClearingRadius *= FMath::Max(0.5, Scale);
		}
		return P;
	}

	/**
	 * Los volcanes arrancan de la cota del relieve que los rodea, no del nivel base: entre montañas
	 * quedarían hundidos y ocultos. Sube a la vez el cono y el lago de lava de su cráter (misma XY).
	 */
	inline void SettleVolcanoes(FLayout& L)
	{
		bool bAny = false;
		for (const FFeature& F : L.Features) { bAny |= F.Type == EFeature::Volcano; }
		if (!bAny) { return; }
		// Rejilla gruesa con el mismo encuadre que el terreno del juego (250 m de margen, 300 m de mar).
		const double Cell = 1000.0;
		FTerrainBuilder TB;
		TB.BuildCoarse(L, FVector2D(-25000.0, -25000.0), Cell,
			FMath::CeilToInt((L.WorldSizeX + 50000.0) / Cell) + 1, FMath::CeilToInt((L.WorldSize + 55000.0) / Cell) + 1);
		for (FFeature& V : L.Features)
		{
			if (V.Type != EFeature::Volcano) { continue; }
			const FVector2D C(V.Location.X, V.Location.Y);
			TArray<double> Ring;
			for (int32 s = 0; s < 32; ++s) { Ring.Add(TB.LandAt(C + DirFromAngle(TwoPi * s / 32.0) * (V.Radius * 0.75))); }
			Ring.Sort();
			// Percentil 75 del anillo: la cota del relieve alto que lo rodea.
			const double Lift = FMath::Max(0.0, Ring[24] - V.Location.Z);
			for (FFeature& Lp : L.Features)
			{
				if (Lp.Type == EFeature::LavaPool && FVector2D::Distance(FVector2D(Lp.Location.X, Lp.Location.Y), C) < 1.0) { Lp.Location.Z += Lift; }
			}
			V.Location.Z += Lift;
		}
	}

	/**
	 * Genera el layout completo. Devuelve false (y FailReason) si no encontró ruta
	 * de módulos; nunca devuelve un layout a medias marcado como válido.
	 */
	inline bool GenerateLayout(const FGenParams& InParams, FLayout& Out)
	{
		Out = FLayout();
		Out.Params = SanitizeParams(InParams);
		const FRng Root(static_cast<uint64>(Out.Params.Seed) * 0x2545F4914F6CDD1Dull + 0x1234567ull);

		BuildModules(Out, Root.Fork(1));
		if (!BuildRoute(Out, Root.Fork(2))) { return false; }
		AssignBiomes(Out, Root.Fork(3));
		AssignLevels(Out, Root.Fork(4));
		BuildPortals(Out, Root.Fork(5));
		if (!BuildMainPath(Out, Root.Fork(6)))
		{
			Out.FailReason = "Sin camino principal";
			return false;
		}
		ComputeWidths(Out, Root.Fork(7));
		ComputeZProfile(Out, Root.Fork(8));
		BuildBranches(Out, Root.Fork(9));
		// Después de las ramas: un módulo vacío con desvío deja de ser macizo.
		BuildBiomeFields(Out);
		BuildStructuralFeatures(Out);
		// Adarve roto de las murallas: después de la puerta (la pone BuildStructuralFeatures), que no se toca.
		BuildWallBreaches(Out, Root.Fork(22));
		// Antes de los huecos y obstáculos: las cuevas cambian anchos y marcan sus tramos como túnel. Buscan
		// tramos que crucen terreno alto (el paisaje en rejilla gruesa, como el de los volcanes).
		{
			const double Cell = 1000.0;
			FTerrainBuilder TB;
			TB.BuildCoarse(Out, FVector2D(-25000.0, -25000.0), Cell,
				FMath::CeilToInt((Out.WorldSizeX + 50000.0) / Cell) + 1, FMath::CeilToInt((Out.WorldSize + 55000.0) / Cell) + 1);
			BuildCaves(Out, Root.Fork(19), [&TB](const FVector2D& P) { return TB.LandAt(P); });
		}
		BuildGaps(Out, Root.Fork(10));
		BuildWetFeatures(Out, Root.Fork(11));
		BuildEggNests(Out, Root.Fork(12));
		BuildLanePuzzles(Out, Root.Fork(13));
		if (Out.Params.bRiver) { BuildRiver(Out, Root.Fork(14)); }
		BuildLandmarks(Out, Root.Fork(16));
		BuildDecor(Out, Root.Fork(15));
		BuildCaveVolcanoes(Out, Root.Fork(21));
		SettleVolcanoes(Out);
		// Formaciones antes que obstáculos: con el camino tan poblado de saltos, los arcos y las piezas de explanada ya no
		// encontrarían sitio; ahora son los obstáculos (y las secuoyas del manglar) los que se apartan de ellas.
		BuildFormations(Out, Root.Fork(18));
		BuildObstacles(Out, Root.Fork(17));
		Out.bValid = true;
		return true;
	}

	/** Estimación de duración del recorrido principal (segundos) a una velocidad media. */
	inline double EstimateTraversalSeconds(const FLayout& L, double AvgSpeedCmPerSec)
	{
		return AvgSpeedCmPerSec > 0.0 ? L.MainLength() / AvgSpeedCmPerSec : 0.0;
	}
}
