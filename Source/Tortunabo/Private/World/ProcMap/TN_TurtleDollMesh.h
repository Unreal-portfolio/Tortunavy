#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapMeshKit.h"

/**
 * Figurita del muñeco tortuga (#797, ATN_TurtleDoll), en caras planas y sin .uasset: una tortuga de juguete (caparazón con
 * placas, peto, cabeza con ojos, cuatro patas y cola) sobre una peana dorada. Marco local: X hacia donde mira la cabeza, Z
 * arriba, origen en el centro de la base de la peana. Medidas (cm) en TNTurtleDollMesh::Size; la prueba
 * Tortunabo.ProcMap.Dolls comprueba la caja y el número de triángulos.
 */
namespace TNTurtleDollMesh
{
	using namespace TNProcMesh;

	/** Caja que ocupa la figurita (largo, ancho, alto), con margen. */
	inline FVector Size() { return FVector(80.0, 64.0, 60.0); }

	namespace Colors
	{
		const FLinearColor Shell(0.12f, 0.55f, 0.24f);
		const FLinearColor Plate(0.30f, 0.72f, 0.30f);
		const FLinearColor Skin(0.62f, 0.84f, 0.38f);
		const FLinearColor Belly(0.95f, 0.88f, 0.62f);
		const FLinearColor Eye(0.03f, 0.03f, 0.04f);
		const FLinearColor Gold(1.0f, 0.74f, 0.12f);
	}

	/** Peana: disco dorado con un bisel. */
	inline void AddStand(FTNProcMeshBuffers& M)
	{
		TNProcAddCylinder(M, FVector(0.0, 0.0, 0.0), FVector(0.0, 0.0, 4.0), 30.0, 30.0, 16, Colors::Gold * 0.85f);
		TNProcAddCylinder(M, FVector(0.0, 0.0, 4.0), FVector(0.0, 0.0, 6.0), 30.0, 27.0, 16, Colors::Gold);
	}

	/** Patas, cola y cabeza (con ojos) alrededor del cuerpo, cuya base está a BodyZ. */
	inline void AddLimbs(FTNProcMeshBuffers& M, double BodyZ)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			const double Sx = (i < 2) ? 1.0 : -1.0;
			const double Sy = (i % 2 == 0) ? 1.0 : -1.0;
			const FVector Hip(Sx * 14.0, Sy * 13.0, BodyZ + 3.0);
			const FVector Foot(Sx * 20.0, Sy * 21.0, 6.0);
			TNProcAddCylinder(M, Foot, Hip, 5.5, 4.5, 8, Colors::Skin);
		}
		TNProcAddCylinder(M, FVector(-24.0, 0.0, BodyZ + 3.0), FVector(-33.0, 0.0, BodyZ + 1.0), 3.5, 1.0, 6, Colors::Skin);
		// Cabeza: un torno vertical delante del caparazón, un poco levantada.
		const FVector Neck(27.0, 0.0, BodyZ + 2.0);
		TNProcAddCylinder(M, FVector(18.0, 0.0, BodyZ + 4.0), Neck + FVector(0.0, 0.0, 4.0), 5.5, 6.0, 8, Colors::Skin);
		const TArray<double> HeadZ = { 0.0, 6.0, 13.0, 18.0 };
		const TArray<double> HeadR = { 7.0, 10.0, 9.0, 5.0 };
		TNProcAddLathe(M, Neck, HeadZ, HeadR, 0.0, 7u, Colors::Skin, 10, 0.4);
		for (const double Sy : { 1.0, -1.0 })
		{
			M.AddBox(Neck + FVector(7.5, Sy * 4.5, 11.0), FVector(1.0, 0.0, 0.0), FVector(1.6, 1.8, 2.4), Colors::Eye);
		}
	}

	/** Caparazón (cúpula con placas más claras arriba) y peto. */
	inline void AddShell(FTNProcMeshBuffers& M, double BodyZ)
	{
		TNProcAddCylinder(M, FVector(0.0, 0.0, BodyZ - 2.0), FVector(0.0, 0.0, BodyZ + 2.0), 24.0, 25.0, 14, Colors::Belly);
		const TArray<double> DomeZ = { 0.0, 6.0, 13.0, 19.0 };
		const TArray<double> DomeR = { 26.0, 25.0, 20.0, 11.0 };
		TNProcAddLathe(M, FVector(0.0, 0.0, BodyZ + 2.0), DomeZ, DomeR, 0.0, 3u, Colors::Shell, 12, 0.35);
		// Placas: hexágonos aplastados sobre la cúpula (el del centro y seis alrededor).
		M.AddBox(FVector(0.0, 0.0, BodyZ + 23.5), FVector(1.0, 0.0, 0.0), FVector(7.0, 6.0, 1.2), Colors::Plate);
		for (int32 k = 0; k < 6; ++k)
		{
			const double A = TNProcMap::TwoPi * k / 6.0;
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.0);
			M.AddBox(FVector(0.0, 0.0, BodyZ + 16.5) + Dir * 15.0, Dir, FVector(4.5, 5.5, 1.2), Colors::Plate);
		}
	}

	/** La figurita entera. */
	inline void Build(FTNProcMeshBuffers& M)
	{
		constexpr double BodyZ = 14.0;
		AddStand(M);
		AddShell(M, BodyZ);
		AddLimbs(M, BodyZ);
	}
}
