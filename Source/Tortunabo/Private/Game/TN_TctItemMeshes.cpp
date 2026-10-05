#include "TN_TctItemMeshes.h"
#include "../World/Beach/TN_BeachEnemyKit.h"
#include "../World/Beach/TN_RaceItemArt.h"
#include "Engine/StaticMesh.h"
#include "Misc/App.h"
#include "World/Beach/TN_RaceItems.h"

namespace TNTctItemMeshesDetail
{
	using TNProcMesh::FTNProcMeshBuffers;

	/** Color sRGB 0xRRGGBB en lineal, con alfa 0 (el material opaco usa el alfa para el viento). */
	FLinearColor Col(uint32 Hex)
	{
		FLinearColor Out = FLinearColor::FromSRGBColor(FColor((Hex >> 16) & 0xFF, (Hex >> 8) & 0xFF, Hex & 0xFF));
		Out.A = 0.f;
		return Out;
	}

	bool CanRender()
	{
		return !IsRunningDedicatedServer() && FApp::CanEverRender();
	}

	/** Esfera aplastada de radio Radius y alto Height centrada en Center (Seg lados). */
	void AddBlob(FTNProcMeshBuffers& M, const FVector& Center, double Radius, double Height, const FLinearColor& Color, uint32 Seed, double Jitter)
	{
		TArray<double> Z;
		TArray<double> R;
		constexpr int32 Rings = 7;
		for (int32 Ring = 0; Ring <= Rings; ++Ring)
		{
			const double Angle = -UE_DOUBLE_HALF_PI + UE_DOUBLE_PI * Ring / Rings;
			Z.Add(FMath::Sin(Angle) * Height * 0.5);
			R.Add(FMath::Max(1.0, FMath::Cos(Angle) * Radius));
		}
		TNProcMesh::TNProcAddLathe(M, Center, Z, R, Jitter, Seed, Color, 12, 0.f);
	}

	/** Toro en el plano XY: radio mayor Major, menor Minor; los gajos alternan entre A y B. */
	void AddTorus(FTNProcMeshBuffers& M, double Major, double Minor, int32 Segments, const FLinearColor& A, const FLinearColor& B)
	{
		constexpr int32 Profile = 8;
		const auto RingAt = [Major, Minor](int32 Index, int32 Count)
		{
			const double Theta = UE_DOUBLE_TWO_PI * Index / Count;
			const FVector Out(FMath::Cos(Theta), FMath::Sin(Theta), 0.0);
			TArray<FVector> Ring;
			for (int32 K = 0; K < Profile; ++K)
			{
				const double Phi = UE_DOUBLE_TWO_PI * K / Profile;
				Ring.Add(Out * (Major + Minor * FMath::Cos(Phi)) + FVector::UpVector * (Minor * FMath::Sin(Phi)));
			}
			return Ring;
		};
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			// Cada gajo es un barrido de dos anillos (para darle su color); las caras miran fuera de su anillo.
			const TArray<TArray<FVector>> Rings = { RingAt(Segment, Segments), RingAt(Segment + 1, Segments) };
			M.AddSweep(Rings, true, ((Segment / 2) % 2 == 0) ? A : B);
		}
	}

	void BuildCoconut(FTNProcMeshBuffers& M)
	{
		AddBlob(M, FVector::ZeroVector, 14.0, 26.0, Col(0x6B4423), 11u, 0.06);
		// Los tres ojos del coco y la mecha con su chispa.
		for (int32 Eye = 0; Eye < 3; ++Eye)
		{
			const double A = UE_DOUBLE_TWO_PI * Eye / 3.0;
			M.AddBox(FVector(FMath::Cos(A) * 4.0, FMath::Sin(A) * 4.0, 12.6), FVector::ForwardVector, FVector(1.6, 1.6, 0.6), Col(0x2A1A0E));
		}
		M.AddBeam(FVector(0.0, 0.0, 12.0), FVector(3.0, 0.0, 21.0), 1.1, Col(0xD8C7A0));
		AddBlob(M, FVector(3.4, 0.0, 22.5), 2.6, 4.0, Col(0xFFC23A), 5u, 0.2);
	}

	void BuildAlgaClump(FTNProcMeshBuffers& M)
	{
		AddBlob(M, FVector::ZeroVector, 13.0, 14.0, Col(0x2F6B2A), 21u, 0.25);
		for (int32 Strand = 0; Strand < 5; ++Strand)
		{
			const double A = UE_DOUBLE_TWO_PI * Strand / 5.0;
			const FVector Root(FMath::Cos(A) * 6.0, FMath::Sin(A) * 6.0, 4.0);
			M.AddBeam(Root, Root + FVector(FMath::Cos(A) * 9.0, FMath::Sin(A) * 9.0, 10.0), 1.3, Col(0x4E9A3A));
		}
	}

	void BuildAlgaPuddle(FTNProcMeshBuffers& M)
	{
		// Mancha irregular plana (radio 100) con grumos de alga encima.
		TArray<FVector2D> Edge;
		constexpr int32 Points = 22;
		for (int32 Index = 0; Index < Points; ++Index)
		{
			const double A = UE_DOUBLE_TWO_PI * Index / Points;
			const double Radius = 100.0 * (0.86 + 0.14 * TNProcMesh::TNProcHashNoise(Index, 3, 77u));
			Edge.Add(FVector2D(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius));
		}
		M.AddPrism(Edge, 1.5, 0.0, Col(0x2E5A2B), false);
		for (int32 Clump = 0; Clump < 9; ++Clump)
		{
			const double A = UE_DOUBLE_TWO_PI * (Clump + 0.4 * TNProcMesh::TNProcHashNoise(Clump, 1, 9u)) / 9.0;
			const double Radius = 25.0 + 55.0 * FMath::Abs(TNProcMesh::TNProcHashNoise(Clump, 2, 9u));
			AddBlob(M, FVector(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, 2.0), 9.0, 5.0,
				Clump % 2 == 0 ? Col(0x4E9A3A) : Col(0x3B7A33), 30u + Clump, 0.3);
		}
	}

	void BuildFloatRing(FTNProcMeshBuffers& M)
	{
		AddTorus(M, 40.0, 12.0, 24, Col(0xE8402F), Col(0xF5F1E8));
	}

	void BuildJellyDome(FTNProcMeshBuffers& M)
	{
		const FLinearColor Bell = Col(0xF07FC8);
		const FLinearColor Rim = Col(0xC8509E);
		const TArray<double> Z = { 0.0, 8.0, 20.0, 32.0, 40.0 };
		const TArray<double> R = { 100.0, 98.0, 86.0, 62.0, 30.0 };
		TNProcMesh::TNProcAddLathe(M, FVector::ZeroVector, Z, R, 0.03, 41u, Bell, 18, 0.25);
		// Borde ondulado y manchas claras de la campana.
		for (int32 Lobe = 0; Lobe < 12; ++Lobe)
		{
			const double A = UE_DOUBLE_TWO_PI * Lobe / 12.0;
			AddBlob(M, FVector(FMath::Cos(A) * 96.0, FMath::Sin(A) * 96.0, 2.0), 14.0, 8.0, Rim, 50u + Lobe, 0.1);
		}
		for (int32 Spot = 0; Spot < 6; ++Spot)
		{
			const double A = UE_DOUBLE_TWO_PI * (Spot + 0.5) / 6.0;
			AddBlob(M, FVector(FMath::Cos(A) * 50.0, FMath::Sin(A) * 50.0, 33.0), 9.0, 6.0, Col(0xFFD2F0), 70u + Spot, 0.1);
		}
	}
}

UStaticMesh* TNTctItemMeshes::Coconut()
{
	using namespace TNTctItemMeshesDetail;
	return CanRender() ? TNBeachKit::CachedMesh(TEXT("Tct.Coconut"), [](FTNProcMeshBuffers& M) { BuildCoconut(M); }) : nullptr;
}

UStaticMesh* TNTctItemMeshes::AlgaClump()
{
	using namespace TNTctItemMeshesDetail;
	return CanRender() ? TNBeachKit::CachedMesh(TEXT("Tct.AlgaClump"), [](FTNProcMeshBuffers& M) { BuildAlgaClump(M); }) : nullptr;
}

UStaticMesh* TNTctItemMeshes::AlgaPuddle()
{
	using namespace TNTctItemMeshesDetail;
	return CanRender() ? TNBeachKit::CachedMesh(TEXT("Tct.AlgaPuddle"), [](FTNProcMeshBuffers& M) { BuildAlgaPuddle(M); }) : nullptr;
}

UStaticMesh* TNTctItemMeshes::FloatRing()
{
	using namespace TNTctItemMeshesDetail;
	return CanRender() ? TNBeachKit::CachedMesh(TEXT("Tct.FloatRing"), [](FTNProcMeshBuffers& M) { BuildFloatRing(M); }) : nullptr;
}

UStaticMesh* TNTctItemMeshes::JellyDome()
{
	using namespace TNTctItemMeshesDetail;
	return CanRender() ? TNBeachKit::CachedMesh(TEXT("Tct.JellyDome"), [](FTNProcMeshBuffers& M) { BuildJellyDome(M); }) : nullptr;
}

UStaticMesh* TNTctItemMeshes::ForKind(ETNTctItem Kind)
{
	switch (Kind)
	{
	case ETNTctItem::Cocobomba: return Coconut();
	case ETNTctItem::Alga:      return AlgaClump();
	case ETNTctItem::GaviotaLadrona:
	{
		TNRaceItemArt::FHeldLook Look;
		return TNRaceItemArt::GetHeldLook(ETNRaceItem::GullStrike, Look) ? Look.Mesh : nullptr;
	}
	default: return nullptr;
	}
}
