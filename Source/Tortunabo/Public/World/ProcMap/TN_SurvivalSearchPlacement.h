#pragma once

// Rebuscables nuevos de los mapas de Supervivencia (#724): el catálogo apenas trae decorado que se pueda rebuscar
// (formaciones, objetos del camino, agujas y peñascos grandes), así que se añaden objetos del camino del bioma (cajas,
// barriles, cántaros...) pegados al borde, en los huecos libres entre trampas, hasta la densidad de la dificultad
// (TNSurvivalLogic::SearchSpotsPer100mTenths: 1 cada 2 trampas). Lógica pura y determinista: cada máquina añade las
// mismas piezas al layout. Tests Tortunabo.Survival.Catalogo.Rebuscables.

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapFeatures.h"
#include "World/ProcMap/TN_SurvivalTrapPlacement.h"

namespace TNSurvivalCatalog
{
	/** Distancia (cm) a la salida y a la meta en la que no se ponen. */
	inline constexpr double SearchPropEndMargin = 2000.0;
	/** Distancia (cm, a lo largo del camino) a la que se quedan de cualquier trampa. */
	inline constexpr double SearchPropTrapClearance = 250.0;
	/** Distancia (cm, en planta) a otro rebuscable: la de SpawnSearchSpots, para que ninguno se quede sin serlo. */
	inline constexpr double SearchPropSpacing = 900.0;
	/** Desnivel (cm) máximo entre el suelo de la pieza y el camino: ni colgando de un cortado ni metida en una pared. */
	inline constexpr double SearchPropMaxDrop = 80.0;

	/** Si un objeto del camino sirve de rebuscable añadido: los compactos que se pueden rebuscar (sin barcas, carros ni puestos). */
	inline bool IsSearchPropKind(TNProcMap::EPathProp Kind)
	{
		using TNProcMap::EPathProp;
		switch (Kind)
		{
			case EPathProp::CrateStack:
			case EPathProp::BarrelGroup:
			case EPathProp::HayBales:
			case EPathProp::Sandcastle:
			case EPathProp::Totem:
			case EPathProp::SkullRock:
			case EPathProp::PotteryJars:
			case EPathProp::CrystalSpikes:
			case EPathProp::Cairn:
			case EPathProp::CrabTraps:
				return true;
			default:
				return false;
		}
	}

	/** Si una pieza del layout ya es un rebuscable de los de siempre (para contarla y no amontonar al lado). */
	inline bool IsExistingSearchable(const TNProcMap::FFeature& F)
	{
		using TNProcMap::EFeature;
		return F.Type == EFeature::Formation || F.Type == EFeature::PathProp || F.Type == EFeature::RockSpire
			|| (F.Type == EFeature::Boulder && F.Radius >= 140.0);
	}

	/**
	 * Objetos del camino que hay que añadir al layout L para que su camino principal tenga SpotsPer100m rebuscables cada
	 * 100 m, contando los que ya trae. Repartidos a partes iguales entre la salida y la meta (sin los 20 m de cada punta),
	 * a lados alternos y por fuera del borde (asoman un poco al camino), lejos de los huecos, de las trampas (Loose y
	 * Terrain) y de otro rebuscable; GroundZ da el suelo del mapa (si no está a la altura del camino, prueba el otro lado o
	 * un poco más allá). Puede devolver menos si no caben.
	 */
	inline TArray<TNProcMap::FFeature> PlaceSearchProps(const TNProcMap::FLayout& L, const TArray<FTrapPlacement>& Loose,
		const FTerrainTrapPlan& Terrain, double SpotsPer100m, uint32 Seed, TFunctionRef<double(const FVector2D&)> GroundZ)
	{
		using namespace TNProcMap;
		using namespace Placement;
		TArray<FFeature> Out;
		const TArray<FPathSample>& M = L.Main;
		if (M.Num() < 2 || SpotsPer100m <= 0.0)
		{
			return Out;
		}
		const double Total = M.Last().S;
		const double Usable = Total - 2.0 * SearchPropEndMargin;
		if (Usable <= 0.0)
		{
			return Out;
		}

		// Lo que ya hay: cuenta para el total y nadie se pone al lado.
		TArray<FVector2D> Taken;
		for (const FFeature& F : L.Features)
		{
			if (IsExistingSearchable(F)) { Taken.Add(FVector2D(F.Location.X, F.Location.Y)); }
		}
		const int32 Need = FMath::RoundToInt32(SpotsPer100m * Total / 10000.0) - Taken.Num();
		if (Need <= 0)
		{
			return Out;
		}

		// Dónde hay trampas, a lo largo del camino.
		TArray<double> TrapAlong;
		for (const FTrapPlacement& P : Loose) { TrapAlong.Add(P.Along); }
		for (const FQuadCrossing& Q : Terrain.Quads) { if (M.IsValidIndex(Q.Sample)) { TrapAlong.Add(M[Q.Sample].S); } }
		auto NearTrap = [&TrapAlong](double S)
		{
			for (const double A : TrapAlong) { if (FMath::Abs(A - S) < SearchPropTrapClearance) { return true; } }
			return false;
		};
		auto Crowded = [&Taken](const FVector2D& C)
		{
			for (const FVector2D& O : Taken) { if (FVector2D::DistSquared(C, O) < FMath::Square(SearchPropSpacing)) { return true; } }
			return false;
		};

		FRng Rng(static_cast<uint64>(Seed) * 0x9E3779B1ull + 0x5EA2C4ull);
		const double Step = Usable / Need;
		for (int32 k = 0; k < Need; ++k)
		{
			const double Center = SearchPropEndMargin + Step * (k + 0.5);
			const double PreferSide = (k % 2 == 0) ? 1.0 : -1.0;
			bool bPlaced = false;
			// Del punto ideal hacia fuera, de metro en metro, sin salir de su parte del recorrido.
			for (int32 Try = 0; !bPlaced && Try * 100.0 <= Step * 0.5; ++Try)
			{
				for (const double Sign : { 1.0, -1.0 })
				{
					if (bPlaced || (Try == 0 && Sign < 0.0)) { continue; }
					const double S = Center + Sign * Try * 100.0;
					const int32 i = SampleAtDistance(M, S);
					if (!IsFree(M, i) || !IsFree(M, i - 1) || !IsFree(M, i + 1) || NearTrap(M[i].S))
					{
						continue;
					}
					TArray<EPathProp> Kinds;
					FeatureDetail::PathPropsFor(M[i].Biome, Kinds);
					Kinds.RemoveAll([](EPathProp K) { return !IsSearchPropKind(K); });
					if (Kinds.Num() == 0)
					{
						Kinds.Add(EPathProp::Cairn);
					}
					const EPathProp Kind = Kinds[Rng.RangeInt(0, Kinds.Num() - 1)];
					double R = 0.0, H = 0.0, Len = 0.0;
					FeatureDetail::PathPropSize(Kind, Rng, R, H, Len);
					const FVector2D Normal = LeftOf(M[i].Dir.GetSafeNormal());
					for (const double Side : { PreferSide, -PreferSide })
					{
						// Por fuera del borde: el centro a 0,6 radios del filo, así asoma un poco al camino y se rebusca desde él.
						const FVector2D C = M[i].P + Normal * (Side * (M[i].Width * 0.5 + R * 0.6));
						if (Crowded(C) || FMath::Abs(GroundZ(C) - M[i].Z) > SearchPropMaxDrop)
						{
							continue;
						}
						FFeature F = FeatureDetail::MakeAtSample(EFeature::PathProp, M[i], i, INDEX_NONE);
						F.Location = FVector(C, M[i].Z);
						F.Dir = M[i].Dir;
						F.Radius = R;
						F.Height = H;
						F.Length = Len;
						F.Aux = static_cast<int32>(Kind);
						F.Aux2 = static_cast<int32>(Rng.RangeInt(0, 1 << 20));
						Out.Add(F);
						Taken.Add(C);
						bPlaced = true;
						break;
					}
				}
			}
		}
		return Out;
	}
}
