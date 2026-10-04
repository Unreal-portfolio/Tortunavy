#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapLayout.h"
#include "World/ProcMap/TN_SurvivalCatalog.h"

/**
 * Colocación de las trampas del catálogo de Supervivencia sobre el layout (#516): pasa el % del recorrido de cada
 * trampa (TN_SurvivalCatalog.h) a un punto del camino principal y aplica las reglas. Lógica pura, igual en todas
 * las máquinas; el generador crea los actores (TN_ProcMapGenerator_Survival.cpp) y los tests la comprueban sobre
 * los 50 mapas (Tortunabo.Survival.Catalogo.Colocacion).
 *
 * Reglas:
 *   - Nada en un hueco de salto, en la salida, en la meta ni junto a la unión de una rama (se corre al punto
 *     libre más cercano, antes hacia delante).
 *   - Ninguna zona lenta en los 30 m anteriores a un hueco: con menos velocidad un salto puede quedar imposible
 *     (se pasa a después del hueco).
 *   - Los obstáculos (cáscaras, medusas y sombrillas) dejan siempre al menos 3 m de paso libre: van tan al centro
 *     como lo permite el ancho del camino, alternando de lado.
 */

namespace TNSurvivalCatalog
{
	/** Trampas que coloca #516 (PlaceLooseTraps). Los quads, el puente que se rompe y las placas son de #517 (PlaceTerrainTraps). */
	inline bool IsLooseTrap(ETrap T)
	{
		return T == ETrap::BananaPeel || T == ETrap::SlowZone || T == ETrap::Jellyfish || T == ETrap::Crab || T == ETrap::Seagull
			|| T == ETrap::Quicksand || T == ETrap::DragCrab || T == ETrap::BurrowCrab || T == ETrap::UrchinSpikes || T == ETrap::TankTrap
			|| T == ETrap::TrashPile || T == ETrap::Trench;
	}

	/** Paso libre mínimo (cm) que dejan los obstáculos en cualquier sección del camino. */
	constexpr double MinFreePassage = 300.0;
	/** Sin huecos de salto en estos cm después del final de una zona lenta. */
	constexpr double SlowZoneGapClearance = 3000.0;
	/** Radio (cm) que ocupa en el suelo cada obstáculo, para el paso libre. */
	constexpr double BananaRadius = 60.0;
	constexpr double JellyfishRadius = 150.0;
	constexpr double UmbrellaRadius = 50.0;
	/**
	 * Criaturas del Excel (lote #691) que estorban en el suelo: el montículo del cangrejo subterráneo, los pinchos del
	 * erizo enterrado, el erizo checo (bloquea) y el montón de basura (bloquea un poco). El cangrejo arrastrador se mueve
	 * y la trinchera se cruza por su rampa: no ocupan sección.
	 */
	constexpr double BurrowCrabRadius = 130.0;
	constexpr double UrchinSpikesRadius = 110.0;
	constexpr double TankTrapRadius = 130.0;
	constexpr double TrashPileRadius = 120.0;
	/** Radio (cm) del charco de arenas movedizas como mucho (y no más que el camino). */
	constexpr double QuicksandMaxRadius = 380.0;
	/** Separación (cm) a lo largo del camino entre las cáscaras de un grupo. */
	constexpr double BananaSpacing = 250.0;
	/** Medio largo (cm) de una zona lenta a lo largo del camino. */
	constexpr double SlowZoneHalfLength = 500.0;
	/** Medio largo (cm) de la zona de un grupo de cangrejos colocado en un punto. */
	constexpr double CrabZoneHalfLength = 800.0;
	/** Sombrillas por tramo de gaviotas si el catálogo no lo dice. */
	constexpr int32 DefaultUmbrellas = 2;

	/** Muestras del camino donde no se pone nada. */
	constexpr uint32 BlockedFlags = TNProcMap::PathFlags::Gap | TNProcMap::PathFlags::Start | TNProcMap::PathFlags::End
		| TNProcMap::PathFlags::Junction;

	struct FTrapPlacement
	{
		ETrap Trap = ETrap::BananaPeel;
		/** Sombrilla de un tramo de gaviotas (Trap = Seagull): un obstáculo en el borde, no la zona. */
		bool bUmbrella = false;
		/** Muestra del camino principal donde va y su distancia (cm) a lo largo del camino. */
		int32 Sample = INDEX_NONE;
		double Along = 0.0;
		/** Desplazamiento (cm) hacia la izquierda del centro del camino (negativo: a la derecha). */
		double Lateral = 0.0;
		/** Punto en espacio del mapa; Z = cota del suelo caminable de la muestra. */
		FVector Location = FVector::ZeroVector;
		/** Dirección del camino (grados, en el plano del mapa). Las zonas de gaviotas van en los ejes del mapa (0). */
		double YawDeg = 0.0;
		/** Semiejes (cm) de la caja de las zonas: X a lo largo del camino (o del mapa), Y a lo ancho. */
		FVector Extent = FVector::ZeroVector;
		/** Cangrejos de la zona. */
		int32 Count = 1;
	};

	/** Radio del obstáculo para el paso libre (0 si no es un obstáculo). */
	inline double ObstacleRadius(const FTrapPlacement& P)
	{
		if (P.bUmbrella) { return UmbrellaRadius; }
		switch (P.Trap)
		{
			case ETrap::BananaPeel: return BananaRadius;
			case ETrap::Jellyfish: return JellyfishRadius;
			case ETrap::BurrowCrab: return BurrowCrabRadius;
			case ETrap::UrchinSpikes: return UrchinSpikesRadius;
			case ETrap::TankTrap: return TankTrapRadius;
			case ETrap::TrashPile: return TrashPileRadius;
			default: return 0.0;
		}
	}
	/** Distancia (cm) a lo largo del camino dentro de la que dos obstáculos comparten sección. */
	constexpr double SharedSection = 150.0;

	namespace Placement
	{
		inline int32 SampleAtDistance(const TArray<TNProcMap::FPathSample>& M, double S)
		{
			for (int32 i = 0; i < M.Num(); ++i) { if (M[i].S >= S) { return i; } }
			return M.Num() - 1;
		}

		inline bool IsFree(const TArray<TNProcMap::FPathSample>& M, int32 i)
		{
			return M.IsValidIndex(i) && (M[i].Flags & BlockedFlags) == 0;
		}

		/** La muestra libre más cercana a i, primero hacia delante (hasta 60 m en cada sentido). */
		inline int32 NearestFree(const TArray<TNProcMap::FPathSample>& M, int32 i)
		{
			if (IsFree(M, i)) { return i; }
			for (int32 d = 1; d < M.Num(); ++d)
			{
				if (IsFree(M, i + d) && M[i + d].S - M[i].S <= 6000.0) { return i + d; }
				if (IsFree(M, i - d) && M[i].S - M[i - d].S <= 6000.0) { return i - d; }
				if ((!M.IsValidIndex(i + d) || M[i + d].S - M[i].S > 6000.0) && (!M.IsValidIndex(i - d) || M[i].S - M[i - d].S > 6000.0)) { break; }
			}
			return INDEX_NONE;
		}

		inline FVector2D LeftOf(const FVector2D& Dir) { return FVector2D(-Dir.Y, Dir.X); }

		/** Lo más al centro que puede ir un obstáculo de radio R dejando MinFreePassage al otro lado. */
		inline double CentralOffset(double Width, double R)
		{
			const double Half = Width * 0.5;
			// Como mucho en el borde (medio fuera del camino, pero a la vista): en caminos de menos de 4,2 m no cabe dentro.
			return FMath::Clamp(MinFreePassage + R - Half + 10.0, 0.0, Half);
		}

		inline FTrapPlacement At(const TArray<TNProcMap::FPathSample>& M, ETrap Trap, int32 i, double Lateral)
		{
			const TNProcMap::FPathSample& S = M[i];
			FTrapPlacement Out;
			Out.Trap = Trap;
			Out.Sample = i;
			Out.Along = S.S;
			Out.Lateral = Lateral;
			const FVector2D P = S.P + LeftOf(S.Dir) * Lateral;
			Out.Location = FVector(P.X, P.Y, S.Z);
			Out.YawDeg = FMath::RadiansToDegrees(FMath::Atan2(S.Dir.Y, S.Dir.X));
			return Out;
		}

		/** Distancias (cm) donde van N elementos: en el punto o repartidos por el tramo. */
		inline TArray<double> Spread(double From, double To, int32 N, double PointSpacing)
		{
			TArray<double> Out;
			N = FMath::Max(1, N);
			for (int32 k = 0; k < N; ++k)
			{
				Out.Add(To > From ? From + (To - From) * (k + 0.5) / N : From + (k - (N - 1) * 0.5) * PointSpacing);
			}
			return Out;
		}

		/** Paso libre (cm) que quedaría en la sección de un obstáculo nuevo (Along, Lateral, R) con los ya puestos. */
		inline double FreePassageWith(const TArray<TNProcMap::FPathSample>& M, const TArray<FTrapPlacement>& Placed,
			int32 Sample, double Along, double Lateral, double R)
		{
			double Half = M[Sample].Width * 0.5;
			TArray<FVector2D, TInlineAllocator<8>> Taken;
			Taken.Add(FVector2D(Lateral - R, Lateral + R));
			for (const FTrapPlacement& Q : Placed)
			{
				const double RQ = ObstacleRadius(Q);
				if (RQ <= 0.0 || FMath::Abs(Q.Along - Along) > SharedSection) { continue; }
				Half = FMath::Min(Half, M[Q.Sample].Width * 0.5);
				Taken.Add(FVector2D(Q.Lateral - RQ, Q.Lateral + RQ));
			}
			Taken.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
			double Free = 0.0, Edge = -Half;
			for (const FVector2D& T : Taken)
			{
				Free = FMath::Max(Free, FMath::Max(-Half, T.X) - Edge);
				Edge = FMath::Max(Edge, FMath::Min(Half, T.Y));
			}
			return FMath::Max(Free, Half - Edge);
		}

		/**
		 * Pone un obstáculo lo más cerca posible de la muestra Start: primero en el lado que toca y en el otro, y si no
		 * deja el paso libre, más adelante (hasta 40 m) o más atrás. OffsetFor(ancho) da su distancia al centro.
		 */
		template <typename FOffset>
		inline bool PlaceObstacle(const TArray<TNProcMap::FPathSample>& M, TArray<FTrapPlacement>& Out, ETrap Trap,
			bool bUmbrella, int32 Start, double R, int32& Side, FOffset OffsetFor)
		{
			if (!M.IsValidIndex(Start)) { return false; }
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				const int32 Step = Pass == 0 ? 1 : -1;
				for (int32 i = Start; M.IsValidIndex(i) && FMath::Abs(M[i].S - M[Start].S) <= 4000.0; i += Step)
				{
					if (!IsFree(M, i)) { continue; }
					for (const int32 Try : { Side, -Side })
					{
						const double Lateral = Try * OffsetFor(M[i].Width);
						if (FreePassageWith(M, Out, i, M[i].S, Lateral, R) >= MinFreePassage)
						{
							FTrapPlacement P = At(M, Trap, i, Lateral);
							P.bUmbrella = bUmbrella;
							Out.Add(P);
							Side = -Try;
							return true;
						}
					}
				}
			}
			return false;
		}

		/** ¿Hay un hueco de salto entre FromS y ToS (cm)? Devuelve el final del primero o -1. */
		inline double GapEndBetween(const TArray<TNProcMap::FPathSample>& M, double FromS, double ToS)
		{
			for (int32 i = 0; i < M.Num(); ++i)
			{
				if (M[i].S < FromS || (M[i].Flags & TNProcMap::PathFlags::Gap) == 0) { continue; }
				if (M[i].S > ToS) { break; }
				int32 j = i;
				while (j + 1 < M.Num() && (M[j + 1].Flags & TNProcMap::PathFlags::Gap) != 0) { ++j; }
				return M[j].S;
			}
			return -1.0;
		}
	}

	/** Las trampas de #516 de un mapa del catálogo sobre su layout. Vacío si la semilla no está en el catálogo. */
	inline TArray<FTrapPlacement> PlaceLooseTraps(const TNProcMap::FLayout& L, uint32 Seed)
	{
		using namespace Placement;
		TArray<FTrapPlacement> Out;
		const TArray<TNProcMap::FPathSample>& M = L.Main;
		if (M.Num() < 2 || !FindMap(Seed)) { return Out; }
		const double Total = M.Last().S;
		int32 Side = 1;

		for (const FTrapSpot& Spot : TrapsOf(Seed))
		{
			if (!IsLooseTrap(Spot.Trap)) { continue; }
			const double From = Total * Spot.FromPct / 100.0;
			const double To = Total * Spot.ToPct / 100.0;

			switch (Spot.Trap)
			{
				case ETrap::BananaPeel:
				case ETrap::Jellyfish:
				{
					const bool bBanana = Spot.Trap == ETrap::BananaPeel;
					const double R = bBanana ? BananaRadius : JellyfishRadius;
					for (const double S : Spread(From, To, Spot.Count, BananaSpacing))
					{
						// Cáscaras tan al centro como deja el paso libre; medusas en el borde (rebote opcional).
						PlaceObstacle(M, Out, Spot.Trap, false, NearestFree(M, SampleAtDistance(M, S)), R, Side,
							[bBanana, R](double Width) { return bBanana ? CentralOffset(Width, R) : Width * 0.5; });
					}
					break;
				}
				case ETrap::SlowZone:
				{
					for (double S : Spread(From, To, Spot.Count, 2.0 * SlowZoneHalfLength + 500.0))
					{
						// Si hay un hueco justo después, la zona pasa a después del hueco.
						for (int32 Guard = 0; Guard < 8; ++Guard)
						{
							const double GapEnd = GapEndBetween(M, S - SlowZoneHalfLength, S + SlowZoneHalfLength + SlowZoneGapClearance);
							if (GapEnd < 0.0) { break; }
							S = GapEnd + SlowZoneHalfLength + 200.0;
						}
						const int32 i = NearestFree(M, SampleAtDistance(M, S));
						if (i == INDEX_NONE) { continue; }
						FTrapPlacement P = At(M, ETrap::SlowZone, i, 0.0);
						P.Extent = FVector(SlowZoneHalfLength, M[i].Width * 0.5 + 100.0, 300.0);
						Out.Add(P);
					}
					break;
				}
				case ETrap::Crab:
				{
					const int32 i = NearestFree(M, SampleAtDistance(M, (From + To) * 0.5));
					if (i == INDEX_NONE) { break; }
					FTrapPlacement P = At(M, ETrap::Crab, i, 0.0);
					// Solo el ancho del camino: la zona hace nacer los cangrejos en cualquier punto de su caja, y fuera del
					// camino caerían en las paredes.
					P.Extent = FVector(FMath::Max(CrabZoneHalfLength, (To - From) * 0.5), M[i].Width * 0.5, 300.0);
					P.Count = Spot.Count;
					Out.Add(P);
					break;
				}
				case ETrap::Seagull:
				{
					// La zona cubre el tramo entero: la caja de sus muestras en los ejes del mapa, con margen.
					FBox2D Box(ForceInit);
					double Z = 0.0;
					int32 N = 0;
					for (const TNProcMap::FPathSample& S : M)
					{
						if (S.S < From || S.S > To) { continue; }
						const FVector2D Margin(S.Width * 0.5 + 500.0);
						Box += FBox2D(S.P - Margin, S.P + Margin);
						Z += S.Z;
						++N;
					}
					if (N == 0) { break; }
					const int32 Mid = SampleAtDistance(M, (From + To) * 0.5);
					FTrapPlacement Zone = At(M, ETrap::Seagull, Mid, 0.0);
					Zone.Location = FVector(Box.GetCenter().X, Box.GetCenter().Y, Z / N);
					Zone.YawDeg = 0.0;
					Zone.Extent = FVector(Box.GetExtent().X, Box.GetExtent().Y, 1500.0);
					Out.Add(Zone);
					// Sombrillas repartidas por el tramo, en el borde.
					const int32 Umbrellas = Spot.Umbrellas > 0 ? Spot.Umbrellas : DefaultUmbrellas;
					for (const double S : Spread(From, To, Umbrellas, 0.0))
					{
						PlaceObstacle(M, Out, ETrap::Seagull, true, NearestFree(M, SampleAtDistance(M, S)), UmbrellaRadius, Side,
							[](double Width) { return Width * 0.5 - UmbrellaRadius - 20.0; });
					}
					break;
				}
				case ETrap::BurrowCrab:
				case ETrap::UrchinSpikes:
				case ETrap::TankTrap:
				case ETrap::TrashPile:
				{
					// Obstáculos de las criaturas: tan al centro como deja el paso libre, alternando de lado.
					FTrapPlacement Probe;
					Probe.Trap = Spot.Trap;
					const double R = ObstacleRadius(Probe);
					for (const double S : Spread(From, To, Spot.Count, 2.0 * R + 300.0))
					{
						PlaceObstacle(M, Out, Spot.Trap, false, NearestFree(M, SampleAtDistance(M, S)), R, Side,
							[R](double Width) { return CentralOffset(Width, R); });
					}
					break;
				}
				case ETrap::Quicksand:
				{
					// Como la zona lenta: nunca justo antes de un hueco (con la tortuga atrapada o lenta, el salto no llega).
					for (double S : Spread(From, To, Spot.Count, 2.0 * QuicksandMaxRadius + 500.0))
					{
						for (int32 Guard = 0; Guard < 8; ++Guard)
						{
							const double GapEnd = GapEndBetween(M, S - QuicksandMaxRadius, S + QuicksandMaxRadius + SlowZoneGapClearance);
							if (GapEnd < 0.0) { break; }
							S = GapEnd + QuicksandMaxRadius + 200.0;
						}
						const int32 i = NearestFree(M, SampleAtDistance(M, S));
						if (i == INDEX_NONE) { continue; }
						FTrapPlacement P = At(M, ETrap::Quicksand, i, 0.0);
						const double R = FMath::Min(QuicksandMaxRadius, M[i].Width * 0.5 + 50.0);
						P.Extent = FVector(R, R, 150.0);
						Out.Add(P);
					}
					break;
				}
				case ETrap::DragCrab:
				case ETrap::Trench:
				{
					// En el centro del camino: el cangrejo ronda desde ahí y la trinchera se cruza por su rampa (+X, hacia la meta).
					for (const double S : Spread(From, To, Spot.Count, 1500.0))
					{
						const int32 i = NearestFree(M, SampleAtDistance(M, S));
						if (i != INDEX_NONE) { Out.Add(At(M, Spot.Trap, i, 0.0)); }
					}
					break;
				}
				default:
					break;
			}
		}
		return Out;
	}


	// ─────────────────────────────────────────────────────────────────────────
	// #517: quads, puente que se rompe y placas (las trampas que dependen de la forma del terreno)
	// ─────────────────────────────────────────────────────────────────────────

	/** Los quads cruzan hasta 15 m más allá de cada borde del camino. */
	constexpr double QuadCrossingMargin = 1500.0;
	/** Distancia máxima (% del recorrido) entre la trampa del catálogo y su hueco o su rama. */
	constexpr double TerrainTrapMaxPctDistance = 8.0;
	/** Placas y compuerta: a cuántos cm de la entrada de la rama, y separación entre placas a lo ancho. */
	constexpr double ShortcutPlateAlong = 300.0;
	constexpr double ShortcutGateAlong = 900.0;
	constexpr double ShortcutPlateSpacing = 250.0;
	/** La compuerta es más ancha que la rama para que no se pueda rodear. */
	constexpr double ShortcutGateMargin = 400.0;

	/** Cruce de quads perpendicular al camino, de lado a lado. */
	struct FQuadCrossing
	{
		int32 Sample = INDEX_NONE;
		/** Centro del cruce (espacio del mapa, Z = cota del camino). */
		FVector Location = FVector::ZeroVector;
		/** Dirección en que cruza (grados): la izquierda del camino. */
		double YawDeg = 0.0;
		/** De centro a cada extremo del recorrido del quad (cm). */
		double HalfSpan = 0.0;
		/** Medio ancho del camino (cm): lo que pintan las franjas de aviso. */
		double PathHalfWidth = 0.0;
	};

	/** Puente que se rompe en lugar de la viga de un hueco que se cruza andando (EGapStyle::Beam). */
	struct FBreakableBridge
	{
		/** Índice del hueco en FLayout::Features: el generador no le pone la viga. */
		int32 Feature = INDEX_NONE;
		/** Centro del puente (espacio del mapa); Z = cota del camino en el hueco. */
		FVector Location = FVector::ZeroVector;
		/** Dirección del camino a través del hueco (grados). */
		double YawDeg = 0.0;
		/** Largo de labio a labio (cm), el mismo que tenía la viga. */
		double Length = 0.0;
	};

	/** Atajo de una rama: una compuerta la corta y la abren las placas de su entrada (Latched: basta un jugador). */
	struct FPlateShortcut
	{
		int32 Branch = INDEX_NONE;
		TArray<FVector> Plates;
		FVector Gate = FVector::ZeroVector;
		double GateYawDeg = 0.0;
		double GateWidth = 0.0;
	};

	struct FTerrainTrapPlan
	{
		TArray<FQuadCrossing> Quads;
		TArray<FBreakableBridge> Bridges;
		TArray<FPlateShortcut> Shortcuts;
	};

	inline double YawOf(const FVector2D& Dir) { return FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)); }

	/** Los quads, puentes y placas de un mapa del catálogo sobre su layout. Vacío si la semilla no está en el catálogo. */
	inline FTerrainTrapPlan PlaceTerrainTraps(const TNProcMap::FLayout& L, uint32 Seed)
	{
		using namespace Placement;
		FTerrainTrapPlan Out;
		const TArray<TNProcMap::FPathSample>& M = L.Main;
		if (M.Num() < 2 || !FindMap(Seed)) { return Out; }
		const double Total = M.Last().S;
		auto PctOf = [&M, Total](int32 i) { return M.IsValidIndex(i) ? 100.0 * M[i].S / Total : -1000.0; };

		for (const FTrapSpot& Spot : TrapsOf(Seed))
		{
			const double From = Total * Spot.FromPct / 100.0;
			const double To = Total * Spot.ToPct / 100.0;
			const double Pct = (Spot.FromPct + Spot.ToPct) * 0.5;
			switch (Spot.Trap)
			{
				case ETrap::Quad:
				{
					for (const double S : Spread(From, To, Spot.Count, 1500.0))
					{
						const int32 i = NearestFree(M, SampleAtDistance(M, S));
						if (i == INDEX_NONE) { continue; }
						FQuadCrossing Q;
						Q.Sample = i;
						Q.Location = FVector(M[i].P, M[i].Z);
						Q.YawDeg = YawOf(LeftOf(M[i].Dir));
						Q.PathHalfWidth = M[i].Width * 0.5;
						Q.HalfSpan = Q.PathHalfWidth + QuadCrossingMargin;
						Out.Quads.Add(Q);
					}
					break;
				}
				case ETrap::BreakableBridge:
				{
					// La viga del camino principal más cercana al %.
					int32 Best = INDEX_NONE;
					double BestDist = TerrainTrapMaxPctDistance;
					for (int32 f = 0; f < L.Features.Num(); ++f)
					{
						const TNProcMap::FFeature& F = L.Features[f];
						if (F.Type != TNProcMap::EFeature::Gap || F.BranchIndex != INDEX_NONE || TNProcMap::GapStyleOf(F) != TNProcMap::EGapStyle::Beam) { continue; }
						const double D = FMath::Abs(PctOf(F.PathIndex) - Pct);
						if (D <= BestDist) { BestDist = D; Best = f; }
					}
					if (Best == INDEX_NONE) { break; }
					const TNProcMap::FFeature& F = L.Features[Best];
					FBreakableBridge B;
					B.Feature = Best;
					B.Location = F.Location;
					B.YawDeg = YawOf(F.Dir);
					// Como la viga (TN_ProcMapGenerator_Build.cpp): 70 cm sobre cada labio.
					B.Length = F.Length + 140.0;
					Out.Bridges.Add(B);
					break;
				}
				case ETrap::PressurePlate:
				{
					// La rama que sale más cerca del %.
					int32 Best = INDEX_NONE;
					double BestDist = TerrainTrapMaxPctDistance;
					for (int32 b = 0; b < L.Branches.Num(); ++b)
					{
						const TNProcMap::FBranch& Br = L.Branches[b];
						if (Br.FromBranch != INDEX_NONE || Br.Samples.Num() < 4) { continue; }
						const double D = FMath::Abs(PctOf(Br.ForkSample) - Pct);
						if (D <= BestDist) { BestDist = D; Best = b; }
					}
					if (Best == INDEX_NONE) { break; }
					const TArray<TNProcMap::FPathSample>& B = L.Branches[Best].Samples;
					auto AlongBranch = [&B](double Along)
					{
						for (int32 k = 0; k < B.Num(); ++k) { if (B[k].S - B[0].S >= Along) { return k; } }
						return B.Num() - 1;
					};
					FPlateShortcut Sc;
					Sc.Branch = Best;
					const TNProcMap::FPathSample& G = B[AlongBranch(ShortcutGateAlong)];
					Sc.Gate = FVector(G.P, G.Z);
					Sc.GateYawDeg = YawOf(G.Dir);
					Sc.GateWidth = G.Width + ShortcutGateMargin;
					const TNProcMap::FPathSample& P = B[AlongBranch(ShortcutPlateAlong)];
					const int32 N = FMath::Max(1, static_cast<int32>(Spot.Count));
					for (int32 k = 0; k < N; ++k)
					{
						const double Lateral = (k - (N - 1) * 0.5) * ShortcutPlateSpacing;
						Sc.Plates.Add(FVector(P.P + LeftOf(P.Dir) * Lateral, P.Z));
					}
					Out.Shortcuts.Add(Sc);
					break;
				}
				default:
					break;
			}
		}
		return Out;
	}
}
