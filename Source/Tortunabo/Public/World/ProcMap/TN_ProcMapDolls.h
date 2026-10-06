#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapShells.h"

/**
 * Reparto de los muñecos tortuga del Coop (#797): coleccionables del nivel que cuentan en la puntuación final
 * (TN_CoopScore.h). Lógica pura y determinista (mismo mapa, mismo reparto); la usa ATN_ProcMapGenerator en el servidor y la
 * prueba Tortunabo.ProcMap.Dolls.
 *
 *  - DollDims::PerLevel muñecos por mapa.
 *  - Premian explorar: primero a media rama (las más largas antes; ni carriles 2vs2 ni rutas altas) y, si faltan sitios,
 *    en el camino principal a las fracciones de DollDims::MainFractions.
 *  - Nada pisa huecos, estructuras, obstáculos, huevos, pozas, agua ni lo que el servidor ya ha puesto (Occupied: peligros
 *    y conchas), y dos muñecos quedan a DollDims::Spacing como mínimo.
 */
namespace TNProcMap
{
	namespace DollDims
	{
		/** Muñecos por nivel (mapa). */
		constexpr int32 PerLevel = 3;
		/** Separación mínima entre dos muñecos (cm). */
		constexpr double Spacing = 15000.0;
		/** Largo mínimo de una rama (cm) para esconder un muñeco a su mitad. */
		constexpr double MinBranchLength = 6000.0;
		/** Altura del centro del muñeco sobre el suelo (cm): flota y gira. */
		constexpr double Hover = 70.0;
		/** Reserva: fracciones del camino principal que se prueban en orden. */
		inline TConstArrayView<double> MainFractions()
		{
			static const double Fractions[] = { 0.35, 0.6, 0.85, 0.2, 0.5, 0.75 };
			return MakeArrayView(Fractions);
		}
	}

	/** Un muñeco del plan, en espacio del mapa. */
	struct FDollSpawn
	{
		/** XY y la cota de referencia del camino: la capa UE lo asienta en el terreno y le suma DollDims::Hover. */
		FVector Location = FVector::ZeroVector;
		FVector2D Facing = FVector2D(1.0, 0.0);
		/** Rama en la que está (INDEX_NONE = camino principal) y su muestra. */
		int32 BranchIndex = INDEX_NONE;
		int32 PathIndex = INDEX_NONE;
	};

	namespace DollDetail
	{
		/** Largo de una polilínea del camino (cm). */
		inline double LengthOf(const TArray<FPathSample>& S)
		{
			return S.Num() > 1 ? S.Last().S - S[0].S : 0.0;
		}

		/** Ramas candidatas, de la más larga a la más corta (empate: la de índice menor). */
		inline TArray<int32> BranchOrder(const FLayout& L)
		{
			TArray<int32> Order;
			for (int32 b = 0; b < L.Branches.Num(); ++b)
			{
				const FBranch& Branch = L.Branches[b];
				if (Branch.Kind == EBranchKind::Lane || Branch.Kind == EBranchKind::High) { continue; }
				if (LengthOf(Branch.Samples) < DollDims::MinBranchLength) { continue; }
				Order.Add(b);
			}
			Order.StableSort([&L](int32 A, int32 B) { return LengthOf(L.Branches[A].Samples) > LengthOf(L.Branches[B].Samples); });
			return Order;
		}

		/** Intenta poner un muñeco cerca de Sq en la polilínea S; lo añade a Out y aparta su zona en Keep. */
		inline bool TryPlace(const TArray<FPathSample>& S, double Sq, int32 BranchIndex, FShellKeepOut& Keep, TArray<FDollSpawn>& Out)
		{
			FShellSpawn Spot;
			if (!ShellDetail::FindGroundSpot(S, Sq, Keep, Spot)) { return false; }
			FDollSpawn Doll;
			Doll.Location = Spot.Location;
			Doll.Facing = Spot.Facing;
			Doll.BranchIndex = BranchIndex;
			Doll.PathIndex = Spot.PathIndex;
			Out.Add(Doll);
			Keep.Add(FVector2D(Doll.Location.X, Doll.Location.Y), DollDims::Spacing);
			return true;
		}
	}

	/** Reparte los muñecos del mapa. Occupied: (x, y, radio) de lo que ya ha puesto el servidor. */
	inline void PlanTurtleDolls(const FLayout& L, const TArray<FVector>& Occupied, TArray<FDollSpawn>& Out)
	{
		Out.Reset();
		if (!L.bValid || L.Main.Num() < 24) { return; }

		FShellKeepOut Keep;
		ShellDetail::AddFeatureKeepOut(L, Keep);
		for (const FVector& O : Occupied) { Keep.Add(FVector2D(O.X, O.Y), O.Z); }

		for (const int32 b : DollDetail::BranchOrder(L))
		{
			if (Out.Num() >= DollDims::PerLevel) { break; }
			const TArray<FPathSample>& S = L.Branches[b].Samples;
			DollDetail::TryPlace(S, S[0].S + 0.5 * DollDetail::LengthOf(S), b, Keep, Out);
		}
		for (const double Fraction : DollDims::MainFractions())
		{
			if (Out.Num() >= DollDims::PerLevel) { break; }
			DollDetail::TryPlace(L.Main, L.Main[0].S + Fraction * DollDetail::LengthOf(L.Main), INDEX_NONE, Keep, Out);
		}
	}
}
