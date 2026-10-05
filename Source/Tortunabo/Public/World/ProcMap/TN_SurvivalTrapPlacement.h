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
 *   - Los obstáculos (cáscaras, medusas, sombrillas, minas, conchas y alambres) dejan siempre al menos 3 m de paso
 *     libre: van tan al centro como lo permite el ancho del camino, alternando de lado.
 *   - Las algas y las conchas que atrapan, como las zonas lentas: nunca en los 30 m anteriores a un hueco.
 *   - Lo que se mueve en línea recta (tanque y cangrejo ermitaño) va en un tramo recto del camino; el cangrejo gigante,
 *     donde el camino es más ancho.
 */

namespace TNSurvivalCatalog
{
	/**
	 * Trampas que coloca #516 (PlaceLooseTraps). Los quads, el puente que se rompe y los atajos de las ramas (placas o puerta
	 * de conchas) son de #517 (PlaceTerrainTraps).
	 */
	inline bool IsLooseTrap(ETrap T)
	{
		return T != ETrap::Quad && T != ETrap::BreakableBridge && T != ETrap::PressurePlate && T != ETrap::ShellGate;
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
	/**
	 * Piezas de la carrera de la playa (#731, #732): la tapa y el montón de la mina, y la valva con su montículo de la concha
	 * que atrapa (SurvivalSizeScale en el generador). El alambre ocupa su medio largo (FTrapPlacement::Extent.X).
	 */
	constexpr double MineRadius = 120.0;
	constexpr double ClamTrapRadius = 170.0;
	/** El alambre deja al menos este largo (cm) sin poner si no cabe: un alambre más corto no merece la pena. */
	constexpr double MinWireLength = 150.0;
	/** Tanque y cangrejo ermitaño: tramo recto (cm) que buscan, como mucho y como poco (el ermitaño necesita su calle). */
	constexpr double ToyTankMaxPatrol = 2000.0;
	constexpr double ToyTankMinPatrol = 800.0;
	constexpr double HermitMaxLane = 3000.0;
	constexpr double HermitMinLane = 1500.0;
	/** Lo que se aparta (cm) como mucho un tanque, un ermitaño o un cangrejo gigante de su punto del catálogo buscando sitio. */
	constexpr double StraightSearchReach = 8000.0;
	constexpr double GiantCrabSearchReach = 2000.0;
	/** Un cangrejo gigante cada tanto de un tramo (cm), sin pasar de los que diga el catálogo. */
	constexpr double GiantCrabSpacing = 2000.0;
	/** Zonas de gaviotas de un tramo: una cada tanto (cm) del camino. */
	constexpr double GullZoneSpacing = 3000.0;
	/** Radio (cm) del charco de arenas movedizas como mucho (y no más que el camino). */
	constexpr double QuicksandMaxRadius = 380.0;
	/** Separación (cm) a lo largo del camino entre las cáscaras de un grupo. */
	constexpr double BananaSpacing = 250.0;
	/** Medio largo (cm) de una zona lenta a lo largo del camino: 8 m de zona, un 20 % menos que los 10 m de antes (#724). */
	constexpr double SlowZoneHalfLength = 400.0;
	/**
	 * Ancho de una zona lenta respecto al de antes (el camino entero más 1 m por lado): un 20 % menos (#724). En los tramos
	 * anchos deja un poco de paso por los bordes.
	 */
	constexpr double SlowZoneWidthScale = 0.8;
	/** Cangrejos gigantes de un grupo de Count del tramo [From, To] (cm): uno cada GiantCrabSpacing, sin pasar de Count. */
	inline int32 GiantCrabCount(double From, double To, int32 Count)
	{
		const int32 BySpan = FMath::CeilToInt32(FMath::Max(0.0, To - From) / GiantCrabSpacing);
		return FMath::Clamp(BySpan, 1, FMath::Max(1, Count));
	}

	/** Zonas de gaviotas del tramo [From, To] (cm): una cada GullZoneSpacing, al menos una. */
	inline int32 GullZoneCount(double From, double To)
	{
		return FMath::Max(1, FMath::CeilToInt32(FMath::Max(0.0, To - From) / GullZoneSpacing));
	}
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
		/** Otra zona de gaviotas del mismo tramo: no cuenta como trampa aparte (CountTraps). */
		bool bPart = false;
		/** Muestra del camino principal donde va y su distancia (cm) a lo largo del camino. */
		int32 Sample = INDEX_NONE;
		double Along = 0.0;
		/** Desplazamiento (cm) hacia la izquierda del centro del camino (negativo: a la derecha). */
		double Lateral = 0.0;
		/** Punto en espacio del mapa; Z = cota del suelo caminable de la muestra. */
		FVector Location = FVector::ZeroVector;
		/** Dirección del camino (grados, en el plano del mapa); el alambre, de lado y el ermitaño, hacia la salida. */
		double YawDeg = 0.0;
		/**
		 * Semiejes (cm) de la caja de las zonas: X a lo largo del camino (o de YawDeg), Y a lo ancho. Algas: Y, el medio ancho
		 * del camino. Alambre: X, su medio largo (YawDeg va de lado). Tanque y ermitaño: X, el medio tramo recto.
		 */
		FVector Extent = FVector::ZeroVector;
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
			case ETrap::Mine: return MineRadius;
			case ETrap::ClamTrap: return ClamTrapRadius;
			case ETrap::BarbedWire: return P.Extent.X;
			default: return 0.0;
		}
	}
	/**
	 * Si la trampa ocupa su sitio del camino. No lo ocupan las zonas de gaviotas (están en el aire; sus sombrillas sí) ni los
	 * enemigos que se mueven: nacen ahí, pero van y vienen. Los rebuscables se apartan solo de las que lo ocupan.
	 */
	inline bool HoldsPlace(const FTrapPlacement& P)
	{
		switch (P.Trap)
		{
			case ETrap::Seagull: return P.bUmbrella;
			case ETrap::Crab:
			case ETrap::DragCrab:
			case ETrap::SandFleas:
			case ETrap::SeaUrchin:
			case ETrap::ToyTank:
			case ETrap::HermitCrab:
				return false;
			default:
				return true;
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

		/**
		 * Semiancho (cm) de una caja recta de semilargo HalfLength centrada en la muestra i y orientada con ella que cabe en el
		 * camino: en cada muestra que cubre, su medio ancho menos lo que el eje se aparta del de la caja (curvas y estrechamientos).
		 */
		inline double BoxHalfWidthInPath(const TArray<TNProcMap::FPathSample>& M, int32 i, double HalfLength)
		{
			double Half = M[i].Width * 0.5;
			for (int32 j = 0; j < M.Num(); ++j)
			{
				if (FMath::Abs(M[j].S - M[i].S) > 2.0 * HalfLength) { continue; }
				const FVector2D D = M[j].P - M[i].P;
				if (FMath::Abs(FVector2D::DotProduct(M[i].Dir, D)) > HalfLength) { continue; }
				Half = FMath::Min(Half, M[j].Width * 0.5 - FMath::Abs(FVector2D::CrossProduct(M[i].Dir, D)));
			}
			return Half;
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

		/**
		 * Medio largo (cm) del tramo recto centrado en la muestra i, hasta MaxHalf: lo que se puede alargar una caja recta a lo
		 * largo del camino con al menos MinHalfWidth de semiancho dentro de él (como BoxHalfWidthInPath, mirando solo las
		 * muestras cercanas). 0 si ni lo más corto cabe.
		 */
		inline double StraightHalfLength(const TArray<TNProcMap::FPathSample>& M, int32 i, double MaxHalf, double MinHalfWidth)
		{
			for (double Half = MaxHalf; Half >= 100.0; Half *= 0.8)
			{
				double HalfWidth = M[i].Width * 0.5;
				bool bBlocked = false;
				for (const int32 Step : { 1, -1 })
				{
					for (int32 j = i; M.IsValidIndex(j) && FMath::Abs(M[j].S - M[i].S) <= Half; j += Step)
					{
						bBlocked |= !IsFree(M, j);
						const FVector2D D = M[j].P - M[i].P;
						HalfWidth = FMath::Min(HalfWidth, M[j].Width * 0.5 - FMath::Abs(FVector2D::CrossProduct(M[i].Dir, D)));
					}
				}
				if (!bBlocked && HalfWidth >= MinHalfWidth)
				{
					return Half;
				}
			}
			return 0.0;
		}

		/**
		 * La muestra libre más cercana a Start (hasta Reach cm en cada sentido, antes hacia delante) con un tramo recto de al
		 * menos MinHalf de medio largo (OutHalf, hasta MaxHalf). INDEX_NONE si no hay.
		 */
		inline int32 FindStraight(const TArray<TNProcMap::FPathSample>& M, int32 Start, double MinHalf, double MaxHalf,
			double MinHalfWidth, double Reach, double& OutHalf)
		{
			if (!M.IsValidIndex(Start)) { return INDEX_NONE; }
			for (int32 d = 0; d < M.Num(); ++d)
			{
				bool bAny = false;
				for (const int32 i : { Start + d, Start - d })
				{
					if (!M.IsValidIndex(i) || FMath::Abs(M[i].S - M[Start].S) > Reach) { continue; }
					bAny = true;
					if (!IsFree(M, i)) { continue; }
					const double Half = StraightHalfLength(M, i, MaxHalf, MinHalfWidth);
					if (Half >= MinHalf)
					{
						OutHalf = Half;
						return i;
					}
				}
				if (!bAny) { break; }
			}
			return INDEX_NONE;
		}

		/** La muestra libre más ancha a menos de Reach cm de Start (la primera si empatan). INDEX_NONE si no hay. */
		inline int32 WidestNear(const TArray<TNProcMap::FPathSample>& M, int32 Start, double Reach)
		{
			int32 Best = INDEX_NONE;
			if (!M.IsValidIndex(Start)) { return Best; }
			for (int32 i = 0; i < M.Num(); ++i)
			{
				if (FMath::Abs(M[i].S - M[Start].S) > Reach || !IsFree(M, i)) { continue; }
				if (Best == INDEX_NONE || M[i].Width > M[Best].Width) { Best = i; }
			}
			return Best;
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

		/** S movida a después de los huecos que tenga en los Clearance cm siguientes (más su radio R), como las zonas lentas. */
		inline double AfterGaps(const TArray<TNProcMap::FPathSample>& M, double S, double R, double Clearance)
		{
			for (int32 Guard = 0; Guard < 8; ++Guard)
			{
				const double GapEnd = GapEndBetween(M, S - R, S + R + Clearance);
				if (GapEnd < 0.0) { break; }
				S = GapEnd + R + 200.0;
			}
			return S;
		}
	}

	/**
	 * Las trampas de #516 de un mapa del catálogo sobre su layout, con DensityPct % de densidad (TrapsOf, #730). Vacío si la
	 * semilla no está en el catálogo.
	 */
	inline TArray<FTrapPlacement> PlaceLooseTraps(const TNProcMap::FLayout& L, uint32 Seed, int32 DensityPct = 100)
	{
		using namespace Placement;
		TArray<FTrapPlacement> Out;
		const TArray<TNProcMap::FPathSample>& M = L.Main;
		if (M.Num() < 2 || !FindMap(Seed)) { return Out; }
		const double Total = M.Last().S;
		int32 Side = 1;

		for (const FTrapSpot& Spot : TrapsOf(Seed, DensityPct))
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
						P.Extent = FVector(SlowZoneHalfLength, (M[i].Width * 0.5 + 100.0) * SlowZoneWidthScale, 300.0);
						Out.Add(P);
					}
					break;
				}
				case ETrap::Crab:
				{
					// Cangrejos gigantes (#734): uno cada 20 m del tramo, cada uno donde el camino es más ancho cerca de su punto
					// (patrulla dentro del camino: ATN_BeachEnemy::SetRoamCorridor).
					for (const double S : Spread(From, To, GiantCrabCount(From, To, Spot.Count), 0.0))
					{
						const int32 i = WidestNear(M, NearestFree(M, SampleAtDistance(M, S)), GiantCrabSearchReach);
						if (i != INDEX_NONE) { Out.Add(At(M, ETrap::Crab, i, 0.0)); }
					}
					break;
				}
				case ETrap::Seagull:
				{
					// Zonas de gaviotas de la playa (#733) repartidas por el tramo: la primera cuenta como la trampa y las demás son
					// parte de ella (bPart). Las gaviotas atacan en un círculo alrededor de cada una.
					const int32 Zones = GullZoneCount(From, To);
					int32 k = 0;
					for (const double S : Spread(From, To, Zones, 0.0))
					{
						const int32 i = SampleAtDistance(M, S);
						FTrapPlacement Zone = At(M, ETrap::Seagull, i, 0.0);
						Zone.bPart = k++ > 0;
						Out.Add(Zone);
					}
					// Sombrillas repartidas por el tramo, en el borde.
					const int32 Umbrellas = Spot.Umbrellas > 0 ? Spot.Umbrellas : DefaultUmbrellas;
					for (const double S : Spread(From, To, Umbrellas, 0.0))
					{
						PlaceObstacle(M, Out, ETrap::Seagull, true, NearestFree(M, SampleAtDistance(M, S)), UmbrellaRadius, Side,
							[](double Width) { return Width * 0.5 - UmbrellaRadius - 20.0; });
					}
					break;
				}
				case ETrap::Mine:
				case ETrap::ClamTrap:
				{
					// Tan al centro como deja el paso libre: se ven y se rodean. La concha atrapa: nunca justo antes de un hueco.
					FTrapPlacement Probe;
					Probe.Trap = Spot.Trap;
					const double R = ObstacleRadius(Probe);
					const bool bClam = Spot.Trap == ETrap::ClamTrap;
					for (double S : Spread(From, To, Spot.Count, 2.0 * R + 300.0))
					{
						// La concha: si al buscar el paso libre acaba delante de un hueco, se prueba después de él.
						for (int32 Guard = 0; Guard < 4; ++Guard)
						{
							if (bClam) { S = AfterGaps(M, S, R, SlowZoneGapClearance); }
							if (!PlaceObstacle(M, Out, Spot.Trap, false, NearestFree(M, SampleAtDistance(M, S)), R, Side,
								[R](double Width) { return CentralOffset(Width, R); }))
							{
								break;
							}
							const double GapEnd = bClam ? GapEndBetween(M, Out.Last().Along - R, Out.Last().Along + R + SlowZoneGapClearance) : -1.0;
							if (GapEnd < 0.0)
							{
								break;
							}
							Out.Pop();
							S = GapEnd + R + 200.0;
						}
					}
					break;
				}
				case ETrap::Seaweed:
				{
					// De lado a lado (se vadea; no hay colisión) y nunca justo antes de un hueco: enganchada, el salto no llega.
					for (double S : Spread(From, To, Spot.Count, 1500.0))
					{
						S = AfterGaps(M, S, 500.0, SlowZoneGapClearance);
						const int32 i = NearestFree(M, SampleAtDistance(M, S));
						if (i == INDEX_NONE) { continue; }
						FTrapPlacement P = At(M, ETrap::Seaweed, i, 0.0);
						P.Extent = FVector(0.0, M[i].Width * 0.5, 0.0);
						Out.Add(P);
					}
					break;
				}
				case ETrap::BarbedWire:
				{
					// Atravesado (YawDeg de lado) donde el camino es más ancho cerca; deja MinFreePassage por un lado, alternando:
					// se salta o se rodea. Si ni así cabe un alambre que merezca la pena, no se pone.
					for (const double S : Spread(From, To, Spot.Count, 1500.0))
					{
						const int32 i = WidestNear(M, NearestFree(M, SampleAtDistance(M, S)), 1000.0);
						if (i == INDEX_NONE) { continue; }
						const double Length = M[i].Width - MinFreePassage;
						if (Length < MinWireLength) { continue; }
						for (const int32 Try : { Side, -Side })
						{
							const double Lateral = Try * (M[i].Width - Length) * 0.5;
							if (FreePassageWith(M, Out, i, M[i].S, Lateral, Length * 0.5) >= MinFreePassage)
							{
								FTrapPlacement P = At(M, ETrap::BarbedWire, i, Lateral);
								P.YawDeg += 90.0;
								P.Extent = FVector(Length * 0.5, 0.0, 0.0);
								Out.Add(P);
								Side = -Try;
								break;
							}
						}
					}
					break;
				}
				case ETrap::ToyTank:
				case ETrap::HermitCrab:
				{
					// Van y vienen (el tanque) o ruedan (el ermitaño) en línea recta: en un tramo recto del camino cerca del punto.
					// El ermitaño espera en el lado de la meta y rueda hacia quien llega (YawDeg hacia la salida).
					const bool bTank = Spot.Trap == ETrap::ToyTank;
					const double MinHalf = 0.5 * (bTank ? ToyTankMinPatrol : HermitMinLane);
					const double MaxHalf = 0.5 * (bTank ? ToyTankMaxPatrol : HermitMaxLane);
					for (const double S : Spread(From, To, Spot.Count, 2.0 * MaxHalf + 500.0))
					{
						double Half = 0.0;
						const int32 i = FindStraight(M, NearestFree(M, SampleAtDistance(M, S)), MinHalf, MaxHalf, 150.0,
							StraightSearchReach, Half);
						if (i == INDEX_NONE) { continue; }
						FTrapPlacement P = At(M, Spot.Trap, i, 0.0);
						P.YawDeg += bTank ? 0.0 : 180.0;
						P.Extent = FVector(Half, 0.0, 0.0);
						Out.Add(P);
					}
					break;
				}
				case ETrap::SandFleas:
				case ETrap::SeaUrchin:
				{
					// En el centro: se mueven por su zona, dentro del camino (ATN_BeachEnemy::SetRoamCorridor).
					for (const double S : Spread(From, To, Spot.Count, 1500.0))
					{
						const int32 i = NearestFree(M, SampleAtDistance(M, S));
						if (i != INDEX_NONE) { Out.Add(At(M, Spot.Trap, i, 0.0)); }
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

	/**
	 * Atajo de una rama: una compuerta la corta y la abren las placas de su entrada (Latched: basta un jugador). Con
	 * bShellGate, en su lugar una puerta de conchas de la playa en una pared (#731), sin placas: se abre empujándola o con
	 * su interruptor.
	 */
	struct FPlateShortcut
	{
		int32 Branch = INDEX_NONE;
		bool bShellGate = false;
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

	/**
	 * Los quads, puentes y placas de un mapa del catálogo sobre su layout, con DensityPct % de densidad (más quads; los
	 * puentes y las placas no cambian). Vacío si la semilla no está en el catálogo.
	 */
	inline FTerrainTrapPlan PlaceTerrainTraps(const TNProcMap::FLayout& L, uint32 Seed, int32 DensityPct = 100)
	{
		using namespace Placement;
		FTerrainTrapPlan Out;
		const TArray<TNProcMap::FPathSample>& M = L.Main;
		if (M.Num() < 2 || !FindMap(Seed)) { return Out; }
		const double Total = M.Last().S;
		auto PctOf = [&M, Total](int32 i) { return M.IsValidIndex(i) ? 100.0 * M[i].S / Total : -1000.0; };

		for (const FTrapSpot& Spot : TrapsOf(Seed, DensityPct))
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
				case ETrap::ShellGate:
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
					Sc.bShellGate = Spot.Trap == ETrap::ShellGate;
					const TNProcMap::FPathSample& G = B[AlongBranch(ShortcutGateAlong)];
					Sc.Gate = FVector(G.P, G.Z);
					Sc.GateYawDeg = YawOf(G.Dir);
					Sc.GateWidth = G.Width + ShortcutGateMargin;
					const TNProcMap::FPathSample& P = B[AlongBranch(ShortcutPlateAlong)];
					const int32 N = Sc.bShellGate ? 0 : FMath::Max(1, static_cast<int32>(Spot.Count));
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

	/**
	 * Trampas de un plan, como las cuenta el registro: las sueltas sin las sombrillas ni las zonas de gaviotas que son parte
	 * de otra, los cruces de quads y los puentes.
	 */
	inline int32 CountTraps(const TArray<FTrapPlacement>& Loose, const FTerrainTrapPlan& Terrain)
	{
		int32 Count = Terrain.Quads.Num() + Terrain.Bridges.Num();
		for (const FTrapPlacement& P : Loose)
		{
			Count += (P.bUmbrella || P.bPart) ? 0 : 1;
		}
		return Count;
	}

	/** Tope del % de puntos que se prueba para llegar a una densidad (×15 los del catálogo). */
	inline constexpr int32 MaxDensityPct = 1500;

	/**
	 * El % de puntos (ScaleTrapSpots) con el que el mapa de Seed sobre L llega a TrapsPer100m trampas cada 100 m de camino
	 * (#730): el más bajo que llega, o MaxDensityPct si ni con ese caben. 100 si el catálogo ya trae bastantes (no se quitan).
	 * Determinista: cada máquina saca el mismo con la misma densidad.
	 */
	inline int32 DensityPctForTarget(const TNProcMap::FLayout& L, uint32 Seed, double TrapsPer100m)
	{
		if (L.Main.Num() < 2 || TrapsPer100m <= 0.0)
		{
			return 100;
		}
		const int32 Target = FMath::RoundToInt32(TrapsPer100m * L.Main.Last().S / 10000.0);
		auto CountAt = [&L, Seed](int32 Pct) { return CountTraps(PlaceLooseTraps(L, Seed, Pct), PlaceTerrainTraps(L, Seed, Pct)); };
		if (CountAt(100) >= Target)
		{
			return 100;
		}
		if (CountAt(MaxDensityPct) < Target)
		{
			return MaxDensityPct;
		}
		// Búsqueda binaria del más bajo que llega (de 5 en 5 %: no hace falta más fino).
		int32 Low = 100;
		int32 High = MaxDensityPct;
		while (High - Low > 5)
		{
			const int32 Mid = (Low + High) / 2;
			if (CountAt(Mid) >= Target) { High = Mid; } else { Low = Mid; }
		}
		return High;
	}
}
