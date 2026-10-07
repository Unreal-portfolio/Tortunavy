#pragma once

#include "CoreMinimal.h"
#include "TN_ProcMapPropMeshes.h"

/**
 * Mallas de Todos contra Todos (#920) con las piezas y los colores de los props del mapa generado (caras planas y color de
 * vértice): lo que rodea a cada punto de objetos (cofre medio enterrado, nido, restos de barco, rocas, sombrilla), las
 * estructuras de playa con colisión (caseta, casco de barco varado) y las marcas de la orilla futura (espuma, algas, poste).
 * Locales: base en el origen (Z = suelo), X al frente, cm a escala 1. Sin dependencias del motor más allá de CoreMinimal.
 */
namespace TNTctMesh
{
	using namespace TNPropMesh;

	/** Cómo se rodea un punto de objetos. */
	enum class EPadStyle : uint8
	{
		Chest,       ///< Cofre medio enterrado (épico).
		Nest,        ///< Nido de paja con huevos (raro).
		WreckPile,   ///< Tablones, barril y caja de un barco (raro).
		Rocks,       ///< Corro de piedras con conchas (común).
		Parasol,     ///< Sombrilla clavada sobre una toalla (común).
		Count
	};

	inline const TCHAR* PadStyleName(EPadStyle Style)
	{
		switch (Style)
		{
		case EPadStyle::Chest:     return TEXT("Chest");
		case EPadStyle::Nest:      return TEXT("Nest");
		case EPadStyle::WreckPile: return TEXT("WreckPile");
		case EPadStyle::Rocks:     return TEXT("Rocks");
		default:                   return TEXT("Parasol");
		}
	}

	/** Un número estable en [Lo, Hi] a partir de una semilla y un índice. */
	inline double Rand(uint32 Seed, int32 K, double Lo, double Hi)
	{
		return TNPropRand(Seed, K, Lo, Hi);
	}

	/** Cofre de ~85 cm medio enterrado en un montículo de arena, con herrajes dorados y algo de oro desparramado. */
	inline void BuildPadChest(FTNProcMeshBuffers& M, uint32 Seed)
	{
		using namespace PropColors;
		const FVector Ax(1.0, 0.0, 0.0), Ay(0.0, 1.0, 0.0), Az(0.0, 0.0, 1.0);
		// Montículo de arena alrededor: el cofre sale de él, inclinado un poco.
		TNPropBall(M, FVector(0.0, 0.0, 6.0), 70.0, Sand * 0.92f, 8, 4, 0.34);
		FTNProcMeshBuffers Chest;
		TNPropBox(Chest, FVector(0.0, 0.0, 20.0), Ax, Ay, Az, FVector(42.0, 27.0, 20.0), WoodDark * 1.35f);
		TNProcAddCylinder(Chest, FVector(-42.0, 0.0, 40.0), FVector(42.0, 0.0, 40.0), 27.0, 27.0, 8, Wood * 1.15f);
		for (const double X : { -30.0, 30.0 })
		{
			TNPropBox(Chest, FVector(X, 0.0, 22.0), Ax, Ay, Az, FVector(4.0, 29.0, 22.0), Yellow * 0.8f);
			TNProcAddCylinder(Chest, FVector(X - 4.0, 0.0, 40.0), FVector(X + 4.0, 0.0, 40.0), 29.0, 29.0, 8, Yellow * 0.8f);
		}
		TNPropBox(Chest, FVector(0.0, 28.0, 36.0), Ax, Ay, Az, FVector(7.0, 2.0, 8.0), Glow);
		// Se hunde por detrás y se ladea.
		const double Tilt = FMath::DegreesToRadians(Rand(Seed, 3, 6.0, 12.0));
		const double Ca = FMath::Cos(Tilt), Sa = FMath::Sin(Tilt);
		for (FVector& V : Chest.Verts) { V = FVector(V.X, V.Y * Ca - (V.Z - 14.0) * Sa, 14.0 + V.Y * Sa + (V.Z - 14.0) * Ca); }
		for (FVector& N : Chest.Normals) { N = FVector(N.X, N.Y * Ca - N.Z * Sa, N.Y * Sa + N.Z * Ca); }
		TNPropAppend(M, Chest, FVector(0.0, 0.0, 6.0), 0.0);
		// Monedas sobre la arena, delante.
		for (int32 K = 0; K < 5; ++K)
		{
			const double A = Rand(Seed, 10 + K, -0.9, 0.9);
			const double R = Rand(Seed, 20 + K, 48.0, 78.0);
			TNProcAddCylinder(M, FVector(FMath::Cos(A + PI * 0.5) * R * 0.7, FMath::Sin(A + PI * 0.5) * R, 7.0),
				FVector(FMath::Cos(A + PI * 0.5) * R * 0.7, FMath::Sin(A + PI * 0.5) * R, 8.5), 4.5, 4.5, 6, Yellow);
		}
	}

	/** Nido de paja de 130 cm de ancho con tres huevos moteados: el objeto queda en el centro. */
	inline void BuildPadNest(FTNProcMeshBuffers& M, uint32 Seed)
	{
		using namespace PropColors;
		TNPropTorus(M, FVector(0.0, 0.0, 9.0), FVector(0.0, 0.0, 1.0), 56.0, 11.0, 12, 4, Straw, Burlap, 3);
		TNPropTorus(M, FVector(0.0, 0.0, 15.0), FVector(0.0, 0.0, 1.0), 48.0, 7.0, 11, 4, Burlap, Straw, 2);
		// Ramitas que sobresalen.
		for (int32 K = 0; K < 9; ++K)
		{
			const double A = TNProcMap::TwoPi * K / 9.0 + Rand(Seed, K, -0.2, 0.2);
			const FVector From(FMath::Cos(A) * 52.0, FMath::Sin(A) * 52.0, 14.0);
			const FVector To(FMath::Cos(A + 0.25) * Rand(Seed, 30 + K, 78.0, 98.0), FMath::Sin(A + 0.25) * Rand(Seed, 30 + K, 78.0, 98.0), Rand(Seed, 50 + K, 14.0, 30.0));
			M.AddBeam(From, To, 1.6, WoodDark);
		}
		// Huevos pegados al borde de dentro.
		for (int32 K = 0; K < 3; ++K)
		{
			const double A = TNProcMap::TwoPi * (K + 0.3) / 3.0 + Rand(Seed, 70, 0.0, 1.0);
			TNPropBall(M, FVector(FMath::Cos(A) * 30.0, FMath::Sin(A) * 30.0, 11.0), 11.0, K == 1 ? Bone : White * 0.95f, 7, 4, 1.25);
		}
	}

	/** Tablones, un barril y una caja de un barco varado, en un corro abierto. */
	inline void BuildPadWreckPile(FTNProcMeshBuffers& M, uint32 Seed)
	{
		using namespace PropColors;
		for (int32 K = 0; K < 4; ++K)
		{
			const double A = TNProcMap::TwoPi * K / 4.0 + Rand(Seed, K, -0.3, 0.3);
			const double Len = Rand(Seed, 10 + K, 90.0, 150.0);
			const FVector Dir(FMath::Cos(A + 1.4), FMath::Sin(A + 1.4), 0.0);
			const FVector Start(FMath::Cos(A) * 72.0, FMath::Sin(A) * 72.0, 5.0);
			M.AddBeam(Start, Start + Dir * Len + FVector(0.0, 0.0, Rand(Seed, 20 + K, 4.0, 40.0)), 6.0, K % 2 ? WoodDark * 1.3f : Driftwood);
		}
		FTNProcMeshBuffers Barrel;
		TNPropBuild(Barrel, TNProcMap::EPropKind::WoodBarrel, 0, Seed, FLinearColor::White, false);
		TNPropAppend(M, Barrel, FVector(-70.0, 60.0, 0.0), Rand(Seed, 40, 0.0, 360.0), 0.8);
		FTNProcMeshBuffers Crate;
		TNPropBuild(Crate, TNProcMap::EPropKind::Crate, 1, Seed, FLinearColor::White, false);
		TNPropAppend(M, Crate, FVector(60.0, -74.0, 0.0), Rand(Seed, 41, 0.0, 360.0), 0.7);
		// Un trozo de cuerda enrollada.
		TNPropTorus(M, FVector(-52.0, -62.0, 5.0), FVector(0.0, 0.0, 1.0), 18.0, 4.0, 9, 3, Rope, Rope * 0.8f, 0);
	}

	/** Cinco piedras en corro con una concha: el objeto queda entre ellas. */
	inline void BuildPadRocks(FTNProcMeshBuffers& M, uint32 Seed)
	{
		using namespace PropColors;
		const int32 Count = 5;
		for (int32 K = 0; K < Count; ++K)
		{
			const double A = TNProcMap::TwoPi * K / Count + Rand(Seed, K, -0.25, 0.25);
			const double R = Rand(Seed, 10 + K, 62.0, 84.0);
			const double Size = Rand(Seed, 20 + K, 24.0, 40.0);
			TNPropBall(M, FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 2.0), Size, Stone * static_cast<float>(Rand(Seed, 30 + K, 0.8, 1.15)), 6, 3, Rand(Seed, 40 + K, 0.55, 0.85));
		}
		FTNProcMeshBuffers Shell;
		TNPropBuild(Shell, TNProcMap::EPropKind::Shell, static_cast<int32>(Seed % 3u), Seed, FLinearColor::White, false);
		TNPropAppend(M, Shell, FVector(36.0, -36.0, 0.0), Rand(Seed, 50, 0.0, 360.0), 0.9);
		FTNProcMeshBuffers Star;
		TNPropBuild(Star, TNProcMap::EPropKind::Starfish, 0, Seed, FLinearColor::White, false);
		TNPropAppend(M, Star, FVector(-40.0, 38.0, 0.0), Rand(Seed, 51, 0.0, 360.0), 0.9);
	}

	/** Sombrilla clavada detrás con una toalla extendida bajo el objeto. */
	inline void BuildPadParasol(FTNProcMeshBuffers& M, uint32 Seed)
	{
		FTNProcMeshBuffers Towel;
		TNPropBuild(Towel, TNProcMap::EPropKind::BeachTowel, static_cast<int32>(Seed % 3u), Seed, FLinearColor::White, false);
		TNPropAppend(M, Towel, FVector::ZeroVector, Rand(Seed, 1, 0.0, 360.0), 1.0);
		FTNProcMeshBuffers Parasol;
		TNPropBuild(Parasol, TNProcMap::EPropKind::Parasol, static_cast<int32>((Seed >> 3) % 3u), Seed, FLinearColor::White, false);
		TNPropAppend(M, Parasol, FVector(-80.0, 70.0, 0.0), Rand(Seed, 2, 0.0, 360.0), 1.0);
	}

	/** Cómo se rodea un punto: rareza (0 común, 1 raro, 2 épico) y una semilla del sitio. */
	inline EPadStyle PadStyleFor(int32 Rarity, uint32 SiteSeed)
	{
		if (Rarity >= 2) { return EPadStyle::Chest; }
		if (Rarity == 1) { return (SiteSeed & 1u) ? EPadStyle::Nest : EPadStyle::WreckPile; }
		return (SiteSeed & 1u) ? EPadStyle::Rocks : EPadStyle::Parasol;
	}

	/** Dónde destella el punto, relativo a su centro (el cofre, en la tapa; el resto, junto al objeto). */
	inline FVector PadGlintOffset(EPadStyle Style)
	{
		switch (Style)
		{
		case EPadStyle::Chest: return FVector(0.0, 0.0, 62.0);
		case EPadStyle::Nest:  return FVector(0.0, 0.0, 44.0);
		default:               return FVector(0.0, 0.0, 52.0);
		}
	}

	inline void BuildPad(FTNProcMeshBuffers& M, EPadStyle Style, uint32 Seed)
	{
		switch (Style)
		{
		case EPadStyle::Chest:     BuildPadChest(M, Seed); break;
		case EPadStyle::Nest:      BuildPadNest(M, Seed); break;
		case EPadStyle::WreckPile: BuildPadWreckPile(M, Seed); break;
		case EPadStyle::Rocks:     BuildPadRocks(M, Seed); break;
		default:                   BuildPadParasol(M, Seed); break;
		}
	}

	// ── Estructuras de playa con colisión ────────────────────────────────────────────────────────────

	/** Caseta de playa de 2,8 x 2,1 m: paredes de rayas, puerta, ventanas y tejado a dos aguas. Variant 0: azul y blanco; 1: rojo y crema. */
	inline void BuildHut(FTNProcMeshBuffers& M, int32 Variant, uint32 Seed)
	{
		using namespace PropColors;
		const FVector Ax(1.0, 0.0, 0.0);
		const bool bBlue = Variant == 0;
		const FLinearColor Wall = bBlue ? White * 0.94f : Straw * 1.05f;
		const FLinearColor Stripe = bBlue ? Blue : Red;
		const FLinearColor Roof = bBlue ? Terracotta : Teal;
		const double HalfX = 140.0, HalfY = 105.0, WallTop = 148.0, Eave = 144.0, Ridge = 218.0;
		auto P = [](double X, double Y, double Z) { return FVector(X, Y, Z); };
		// Paredes (algo enterradas) y rayas verticales en los cuatro lados.
		M.AddBox(P(0.0, 0.0, (WallTop - 14.0) * 0.5), Ax, FVector(HalfX, HalfY, (WallTop + 14.0) * 0.5), Wall);
		for (int32 K = 0; K < 7; K += 2)
		{
			const double Y = -HalfY + 15.0 + K * 30.0;
			M.AddBox(P(HalfX + 0.8, Y, 66.0), Ax, FVector(1.2, 15.0, 80.0), Stripe);
			M.AddBox(P(-HalfX - 0.8, Y, 66.0), Ax, FVector(1.2, 15.0, 80.0), Stripe);
		}
		for (int32 K = 0; K < 9; K += 2)
		{
			const double X = -HalfX + 16.0 + K * 31.0;
			M.AddBox(P(X, HalfY + 0.8, 66.0), Ax, FVector(15.0, 1.2, 80.0), Stripe);
			M.AddBox(P(X, -HalfY - 0.8, 66.0), Ax, FVector(15.0, 1.2, 80.0), Stripe);
		}
		// Puerta (en la cara +X) con su marco, y dos ventanas laterales.
		M.AddBox(P(HalfX + 2.0, 0.0, 56.0), Ax, FVector(2.5, 30.0, 56.0), WoodDark);
		M.AddBox(P(HalfX + 3.2, 20.0, 52.0), Ax, FVector(1.0, 2.0, 3.0), Yellow);
		M.AddBox(P(HalfX + 1.4, 0.0, 116.0), Ax, FVector(2.0, 36.0, 4.0), Wood);
		for (const double Y : { -HalfY - 2.5, HalfY + 2.5 })
		{
			M.AddBox(P(-20.0, Y, 100.0), Ax, FVector(30.0, 2.0, 22.0), Wood);
			M.AddBox(P(-20.0, Y + (Y > 0.0 ? 0.6 : -0.6), 100.0), Ax, FVector(26.0, 1.5, 18.0), Teal * 0.9f);
		}
		// Tejado a dos aguas con alero, cumbrera y los dos hastiales.
		const double OverX = HalfX + 22.0, OverY = HalfY + 22.0;
		M.AddQuad(P(-OverX, -OverY, Eave - 6.0), P(OverX, -OverY, Eave - 6.0), P(OverX, 0.0, Ridge), P(-OverX, 0.0, Ridge), FVector(0.0, -0.6, 0.8), Roof);
		M.AddQuad(P(-OverX, OverY, Eave - 6.0), P(-OverX, 0.0, Ridge), P(OverX, 0.0, Ridge), P(OverX, OverY, Eave - 6.0), FVector(0.0, 0.6, 0.8), Roof);
		M.AddQuad(P(-OverX, -OverY, Eave - 6.0), P(-OverX, 0.0, Ridge), P(OverX, 0.0, Ridge), P(OverX, -OverY, Eave - 6.0), FVector(0.0, 0.5, -0.8), Roof * 0.7f);
		M.AddQuad(P(-OverX, OverY, Eave - 6.0), P(OverX, OverY, Eave - 6.0), P(OverX, 0.0, Ridge), P(-OverX, 0.0, Ridge), FVector(0.0, -0.5, -0.8), Roof * 0.7f);
		M.AddBeam(P(-OverX - 2.0, 0.0, Ridge + 2.0), P(OverX + 2.0, 0.0, Ridge + 2.0), 4.0, WoodDark);
		for (const double X : { -HalfX, HalfX })
		{
			M.AddTri(P(X, -HalfY, WallTop), P(X, HalfY, WallTop), P(X, 0.0, Ridge - 3.0), FVector(X > 0.0 ? 1.0 : -1.0, 0.0, 0.0), Wall);
		}
		// Un banderín en la cumbrera, distinto en cada caseta.
		M.AddBeam(P(-OverX + 6.0, 0.0, Ridge), P(-OverX + 6.0, 0.0, Ridge + 44.0), 1.5, Wood);
		M.AddBox(P(-OverX + 6.0, Rand(Seed, 5, -0.5, 0.5) > 0.0 ? 12.0 : -12.0, Ridge + 36.0), Ax, FVector(1.0, 12.0, 7.0), bBlue ? Red : Yellow);
	}

	/** Casco de un barco varado de unos 3,6 m: cuadernas al aire, tablones, un trozo de cubierta y el palo roto; medio enterrado. */
	inline void BuildWreck(FTNProcMeshBuffers& M, int32 Variant, uint32 Seed)
	{
		using namespace PropColors;
		const FLinearColor Rib = Variant == 0 ? Wood : WoodDark * 1.4f;
		const FLinearColor Plank = Variant == 0 ? Driftwood : WoodLight;
		const int32 Ribs = 9;
		const int32 Steps = 6;
		constexpr double HalfLength = 180.0, MaxHalfWidth = 100.0, GunwaleZ = 92.0, KeelDepth = 120.0;
		TArray<TArray<FVector>> Points;
		Points.SetNum(Ribs);
		for (int32 I = 0; I < Ribs; ++I)
		{
			const double X = -HalfLength + 2.0 * HalfLength * I / (Ribs - 1);
			const double U = X / (HalfLength + 12.0);
			const double Width = MaxHalfWidth * FMath::Sqrt(FMath::Max(0.05, 1.0 - U * U));
			// Una cuaderna rota se corta más abajo por un lado (nunca las de las puntas, que dan forma al casco).
			const bool bBroken = I > 0 && I < Ribs - 1 && Rand(Seed, I, 0.0, 1.0) > 0.7;
			const int32 CutLeft = bBroken ? static_cast<int32>(Rand(Seed, 20 + I, 1.0, 3.0)) : 0;
			const int32 CutRight = (bBroken && Rand(Seed, 40 + I, 0.0, 1.0) > 0.5) ? 1 : 0;
			for (int32 K = CutLeft; K <= Steps - CutRight; ++K)
			{
				const double A = PI * K / Steps;
				const double Z = GunwaleZ - KeelDepth * FMath::Sin(A) * (Width / MaxHalfWidth);
				Points[I].Add(FVector(X, Width * FMath::Cos(A), Z));
			}
		}
		for (int32 I = 0; I < Ribs; ++I)
		{
			for (int32 K = 0; K + 1 < Points[I].Num(); ++K)
			{
				M.AddBeam(Points[I][K], Points[I][K + 1], 4.0, Rib);
			}
		}
		// Tablones a lo largo de los costados, entre cuadernas enteras vecinas, a la misma altura.
		for (int32 I = 0; I + 1 < Ribs; ++I)
		{
			if (Points[I].Num() != Steps + 1 || Points[I + 1].Num() != Steps + 1)
			{
				continue;
			}
			for (const int32 Row : { 1, 2, 4, 5 })
			{
				if (Rand(Seed, 100 + I * 7 + Row, 0.0, 1.0) > 0.25)
				{
					M.AddBeam(Points[I][Row], Points[I + 1][Row], 3.0, Plank);
				}
			}
		}
		// La quilla, un trozo de cubierta a media nave y el palo roto con su vela rasgada.
		M.AddBeam(FVector(-HalfLength, 0.0, GunwaleZ - KeelDepth + 2.0), FVector(HalfLength, 0.0, GunwaleZ - KeelDepth + 2.0), 6.0, WoodDark);
		M.AddBox(FVector(20.0, 0.0, 52.0), FVector(1.0, 0.0, 0.0), FVector(70.0, 70.0, 3.5), Plank);
		M.AddBeam(FVector(30.0, 0.0, 50.0), FVector(30.0, 0.0, 170.0), 6.5, WoodDark);
		M.AddBeam(FVector(30.0, 0.0, 170.0), FVector(80.0, 34.0, 100.0), 5.0, WoodDark);
		M.AddQuad(FVector(32.0, 4.0, 160.0), FVector(32.0, 4.0, 90.0), FVector(70.0, 40.0, 70.0), FVector(70.0, 36.0, 120.0), FVector(0.0, -1.0, 0.0), White * 0.85f);
	}
}
