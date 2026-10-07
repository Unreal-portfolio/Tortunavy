#pragma once

#include "CoreMinimal.h"
#include "TN_TctPropMeshes.h"

/**
 * Mallas de las marcas de la orilla futura del agua de Todos contra Todos (#920): espuma, algas y un poste con marcas, con las
 * piezas y los colores de los props del mapa generado. Locales: base en el origen (Z = suelo), cm a escala 1.
 */
namespace TNTctMesh
{

	// ── Marcas de la orilla futura del agua ──────────────────────────────────────────────────────────

	/** Mancha de espuma de ~2 m sobre la arena: el centro blanco y el borde de un verde pálido, casi a ras de suelo. */
	inline void BuildFoam(FTNProcMeshBuffers& M, int32 Variant, uint32 Seed)
	{
		const FLinearColor Foam(0.94f, 0.98f, 0.93f);
		const FLinearColor Edge(0.58f, 0.86f, 0.5f);
		const int32 N = 10;
		const double R0 = 62.0 + 16.0 * Variant;
		const FVector Up(0.0, 0.0, 1.0);
		TArray<FVector> Inner, Outer;
		for (int32 I = 0; I < N; ++I)
		{
			const double A = TNProcMap::TwoPi * I / N;
			const double R = R0 * Rand(Seed, I, 0.7, 1.25);
			Inner.Add(FVector(FMath::Cos(A) * R * 0.5, FMath::Sin(A) * R * 0.5, 7.0));
			Outer.Add(FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 3.5));
		}
		for (int32 I = 0; I < N; ++I)
		{
			const int32 J = (I + 1) % N;
			M.AddTri(FVector(0.0, 0.0, 8.0), Inner[I], Inner[J], Up, Foam);
			M.AddQuad(Inner[I], Outer[I], Outer[J], Inner[J], Up, (I % 3 == 0) ? Edge : Foam * 0.97f);
		}
		// Burbujas encima.
		for (int32 K = 0; K < 3; ++K)
		{
			TNPropBall(M, FVector(Rand(Seed, 30 + K, -30.0, 30.0), Rand(Seed, 40 + K, -30.0, 30.0), 6.0), Rand(Seed, 50 + K, 7.0, 13.0), Foam, 6, 3, 0.8);
		}
	}

	/** Mata de algas verdes de ~60 cm: una mancha oscura en el suelo y frondas largas que se curvan. */
	inline void BuildAlgae(FTNProcMeshBuffers& M, int32 Variant, uint32 Seed)
	{
		using namespace PropColors;
		TNPropBall(M, FVector(0.0, 0.0, 0.0), 26.0, Leaf * 0.55f, 6, 3, 0.3);
		const int32 Blades = 7;
		for (int32 K = 0; K < Blades; ++K)
		{
			const double A = TNProcMap::TwoPi * K / Blades + Rand(Seed, K, -0.3, 0.3);
			const double Lean = Rand(Seed, 10 + K, 0.0, TNProcMap::TwoPi);
			const double Height = Rand(Seed, 20 + K, 38.0, 72.0);
			const FVector Dir(FMath::Cos(Lean), FMath::Sin(Lean), 0.0);
			const FVector P0(FMath::Cos(A) * Rand(Seed, 30 + K, 4.0, 18.0), FMath::Sin(A) * Rand(Seed, 30 + K, 4.0, 18.0), 3.0);
			const FVector P1 = P0 + Dir * Height * 0.12 + FVector(0.0, 0.0, Height * 0.4);
			const FVector P2 = P1 + Dir * Height * 0.3 + FVector(0.0, 0.0, Height * 0.35);
			const FVector P3 = P2 + Dir * Height * 0.3 + FVector(0.0, 0.0, Height * 0.15);
			const FLinearColor Col = (K + Variant) % 3 == 0 ? Green : ((K + Variant) % 3 == 1 ? Leaf : Teal * 0.6f);
			M.AddBeam(P0, P1, 2.8, Col);
			M.AddBeam(P1, P2, 2.1, Col * 1.1f);
			M.AddBeam(P2, P3, 1.4, Col * 1.2f);
		}
	}

	/** Poste de madera de ~1,6 m con tres marcas pintadas y un casquete donde va la lámpara (que pone el actor). */
	inline void BuildPost(FTNProcMeshBuffers& M)
	{
		using namespace PropColors;
		const FVector Ax(1.0, 0.0, 0.0);
		M.AddBeam(FVector(0.0, 0.0, -16.0), FVector(0.0, 0.0, 150.0), 5.5, Driftwood * 0.9f);
		const double Marks[3] = { 52.0, 86.0, 120.0 };
		for (int32 K = 0; K < 3; ++K)
		{
			M.AddBox(FVector(0.0, 0.0, Marks[K]), Ax, FVector(7.0, 7.0, 6.0), K % 2 == 0 ? Red : White);
		}
		TNPropBall(M, FVector(0.0, 0.0, 150.0), 9.0, Iron, 6, 3, 0.9);
	}

	/** Dónde queda la lámpara sobre el pie de un poste (a escala 1, uu). */
	inline constexpr double PostLampHeight = 168.0;
}
