#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapLayout.h"
#include "TN_ProcMapMeshKit.h"
#include "TN_ProcMapFloraMeshes.h"
#include "TN_ProcMapPropMeshes.h"
#include "World/Beach/TN_BeachCreatureRules.h"

/**
 * Mallas de las formaciones temáticas (low-poly de caras planas con color de vértice). Cada receta
 * se construye en coordenadas locales (X a lo largo de Dir, Y a su izquierda, Z arriba, origen en
 * el suelo del camino o en la base del hito) y se lleva al mapa con TNFormAppend. El suelo local lo
 * da un functor Ground(X, Y) -> Z (relativo al origen): así los pies de los arcos se hunden en los
 * taludes y los hitos se asientan en el terreno.
 */
namespace TNFormMesh
{
	using namespace TNProcMesh;
	using namespace TNFloraMesh;
	using TNProcMap::EFormation;

	/** Colores de las formaciones de un bioma. */
	struct FTNFormColors
	{
		FLinearColor Rock;
		FLinearColor Strata;    ///< Segunda capa de la roca (estratos).
		FLinearColor Moss;
		FLinearColor Wood;
		FLinearColor Bone;
		FLinearColor Cloth;
		FLinearColor Metal;
		FLinearColor Concrete;
		FLinearColor Paint;     ///< Rojo de faro, bandera o sombrilla.
		FLinearColor White;
		FLinearColor Dark;      ///< Huecos: troneras, puertas, bocas.
	};

	inline FTNFormColors TNFormColorsFor(ETNProcBiome Biome, const FLinearColor& RockColor)
	{
		FTNFormColors C;
		C.Rock = RockColor;
		C.Strata = RockColor * 1.15f;
		C.Moss = FLinearColor(0.12f, 0.3f, 0.08f);
		C.Wood = FLinearColor(0.36f, 0.24f, 0.13f);
		C.Bone = FLinearColor(0.86f, 0.83f, 0.72f);
		C.Cloth = FLinearColor(0.82f, 0.78f, 0.66f);
		C.Metal = FLinearColor(0.24f, 0.26f, 0.16f);
		C.Concrete = FLinearColor(0.5f, 0.5f, 0.48f);
		C.Paint = FLinearColor(0.72f, 0.1f, 0.08f);
		C.White = FLinearColor(0.9f, 0.9f, 0.86f);
		C.Dark = FLinearColor(0.04f, 0.04f, 0.04f);
		switch (Biome)
		{
			case ETNProcBiome::Desert:
				C.Rock = FLinearColor(0.62f, 0.34f, 0.18f); C.Strata = FLinearColor(0.78f, 0.55f, 0.34f); C.Concrete = FLinearColor(0.66f, 0.55f, 0.4f);
				break;
			case ETNProcBiome::Beach:
				C.Rock = FLinearColor(0.58f, 0.52f, 0.42f); C.Strata = FLinearColor(0.7f, 0.64f, 0.52f);
				break;
			case ETNProcBiome::Volcanic:
				C.Rock = FLinearColor(0.1f, 0.1f, 0.12f); C.Strata = FLinearColor(0.16f, 0.15f, 0.17f); C.Moss = FLinearColor(0.85f, 0.75f, 0.15f);
				break;
			case ETNProcBiome::Rocky:
				C.Rock = FLinearColor(0.36f, 0.36f, 0.37f); C.Strata = FLinearColor(0.44f, 0.43f, 0.42f); C.Moss = FLinearColor(0.3f, 0.36f, 0.2f);
				break;
			case ETNProcBiome::Jungle:
			case ETNProcBiome::Mangrove:
				C.Rock = FLinearColor(0.4f, 0.41f, 0.36f); C.Strata = FLinearColor(0.46f, 0.47f, 0.4f);
				break;
			default:
				break;
		}
		return C;
	}

	/** Parámetros de una formación en su marco local. */
	struct FTNFormParams
	{
		double Width = 0.0;
		double Height = 0.0;
		double Length = 0.0;
		double Radius = 0.0;
		uint32 Seed = 0;
		/** Cota local del nivel del mar (palafitos, farallones). */
		double WaterZ = -1e9;
	};

	/** Copia Src (local) en Dst (mapa): origen Origin, X local a lo largo de Dir. */
	inline void TNFormAppend(FTNProcMeshBuffers& Dst, const FTNProcMeshBuffers& Src, const FVector& Origin, const FVector2D& Dir)
	{
		const FVector D(Dir.X, Dir.Y, 0.0);
		const FVector N(-Dir.Y, Dir.X, 0.0);
		auto Xf = [&](const FVector& V) { return D * V.X + N * V.Y + FVector(0.0, 0.0, V.Z); };
		const int32 Base = Dst.Verts.Num();
		for (int32 i = 0; i < Src.Verts.Num(); ++i)
		{
			const FVector P = Origin + Xf(Src.Verts[i]);
			Dst.Verts.Add(P);
			Dst.Normals.Add(Xf(Src.Normals[i]));
			Dst.UVs.Add(Src.UVs[i]);
			Dst.Colors.Add(Src.Colors[i]);
		}
		for (const int32 T : Src.Tris) { Dst.Tris.Add(Base + T); }
	}

	/** Cilindro de Seg lados entre A y B, con tapas (el del kit). */
	inline void TNFormCylinder(FTNProcMeshBuffers& M, const FVector& A, const FVector& B, double RA, double RB, int32 Seg, const FLinearColor& Color, bool bCaps = true)
	{
		TNProcAddCylinder(M, A, B, RA, RB, Seg, Color, bCaps);
	}

	/** Tubo a lo largo de una polilínea (huesos, raíces, costillas), con radio por punto. */
	inline void TNFormTube(FTNProcMeshBuffers& M, const TArray<FVector>& Pts, const TArray<double>& Radii, int32 Seg, const FLinearColor& Color,
		const FLinearColor* TopColor = nullptr)
	{
		if (Pts.Num() < 2) { return; }
		TArray<TArray<FVector>> Rings;
		for (int32 i = 0; i < Pts.Num(); ++i)
		{
			const FVector T = (Pts[FMath::Min(i + 1, Pts.Num() - 1)] - Pts[FMath::Max(i - 1, 0)]).GetSafeNormal();
			FVector U = FVector::CrossProduct(T, FMath::Abs(T.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
			const FVector V = FVector::CrossProduct(T, U);
			TArray<FVector> Ring;
			for (int32 k = 0; k < Seg; ++k)
			{
				const double Ang = TNProcMap::TwoPi * k / Seg;
				Ring.Add(Pts[i] + (U * FMath::Cos(Ang) + V * FMath::Sin(Ang)) * Radii[i]);
			}
			Rings.Add(Ring);
		}
		for (int32 i = 0; i + 1 < Rings.Num(); ++i)
		{
			for (int32 k = 0; k < Seg; ++k)
			{
				const int32 K1 = (k + 1) % Seg;
				const FVector Mid = (Rings[i][k] + Rings[i][K1]) * 0.5 - Pts[i];
				// Lo que mira arriba, del color de encima (musgo, arena).
				const FLinearColor C = TopColor && Mid.GetSafeNormal().Z > 0.55 ? *TopColor : Color;
				M.AddQuad(Rings[i][k], Rings[i][K1], Rings[i + 1][K1], Rings[i + 1][k], Mid, C);
			}
		}
		const FVector TA = (Pts[0] - Pts[1]).GetSafeNormal();
		const FVector TB = (Pts.Last() - Pts[Pts.Num() - 2]).GetSafeNormal();
		for (int32 k = 0; k < Seg; ++k)
		{
			const int32 K1 = (k + 1) % Seg;
			M.AddTri(Pts[0], Rings[0][k], Rings[0][K1], TA, Color * 0.9f);
			M.AddTri(Pts.Last(), Rings.Last()[k], Rings.Last()[K1], TB, Color);
		}
	}

	/** Caja girada un ángulo Yaw (rad) en planta. */
	inline void TNFormBox(FTNProcMeshBuffers& M, const FVector& Center, const FVector& Half, double Yaw, const FLinearColor& Color)
	{
		M.AddBox(Center, FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0), Half, Color);
	}

	/** Rueda de radios en el plano XZ (eje Y), centrada en C. */
	inline void TNFormWheel(FTNProcMeshBuffers& M, const FVector& C, double R, const FLinearColor& Color)
	{
		constexpr int32 N = 10;
		for (int32 k = 0; k < N; ++k)
		{
			const double A0 = TNProcMap::TwoPi * k / N, A1 = TNProcMap::TwoPi * (k + 1) / N;
			M.AddBeam(C + FVector(FMath::Cos(A0) * R, 0.0, FMath::Sin(A0) * R), C + FVector(FMath::Cos(A1) * R, 0.0, FMath::Sin(A1) * R), R * 0.08, Color);
			if (k % 2 == 0) { M.AddBeam(C, C + FVector(FMath::Cos(A0) * R, 0.0, FMath::Sin(A0) * R), R * 0.045, Color * 0.9f); }
		}
		TNFormCylinder(M, C - FVector(0.0, R * 0.12, 0.0), C + FVector(0.0, R * 0.12, 0.0), R * 0.14, R * 0.14, 6, Color * 0.8f);
	}

	/**
	 * Estatua de tortuga sobre pedestal escalonado (local: base en el origen, mirando a +X, ~2,5 m de
	 * alto): caparazón en cúpula con escudos hexagonales, cabeza, aletas y cola; ojos que brillan.
	 * Crown: 0 nada, 1 corona de oro, 2 guirnalda de hojas, 3 cristal en la frente. Delante, dos velas
	 * y una vasija de ofrenda.
	 */
	inline void TNTurtleStatue(FTNProcMeshBuffers& Solid, FTNProcMeshBuffers& Glow, uint32 Seed, const FLinearColor& Stone, const FLinearColor& Accent,
		const FLinearColor& Eyes, int32 Crown, bool bMoss, bool bOfferings = true)
	{
		const FLinearColor Dark = Stone * 0.78f;
		const FVector X(1.0, 0.0, 0.0);
		Solid.AddBox(FVector(0.0, 0.0, 18.0), X, FVector(125.0, 112.0, 18.0), Dark);
		Solid.AddBox(FVector(0.0, 0.0, 70.0), X, FVector(104.0, 92.0, 34.0), Stone * 0.9f);
		Solid.AddBox(FVector(0.0, 0.0, 112.0), X, FVector(116.0, 102.0, 8.0), Dark);
		const double Z0 = 120.0;
		// Plastrón y cúpula del caparazón.
		TNProcAddCylinder(Solid, FVector(0.0, 0.0, Z0), FVector(0.0, 0.0, Z0 + 14.0), 98.0, 100.0, 14, Stone * 0.86f);
		TNProcAddLathe(Solid, FVector(0.0, 0.0, Z0 + 14.0), { 0.0, 18.0, 42.0, 64.0, 80.0, 90.0 }, { 100.0, 96.0, 84.0, 62.0, 34.0, 8.0 }, 0.02, Seed, Stone, 14, 0.2);
		// Escudos: uno en lo alto, seis alrededor y diez en el borde.
		auto Plate = [&](double Theta, double Phi, double R)
		{
			const double St = FMath::Sin(Theta), Ct = FMath::Cos(Theta);
			const FVector P(100.0 * St * FMath::Cos(Phi), 100.0 * St * FMath::Sin(Phi), Z0 + 14.0 + 88.0 * Ct);
			const FVector Nrm = FVector(St * FMath::Cos(Phi) / 100.0, St * FMath::Sin(Phi) / 100.0, Ct / 88.0).GetSafeNormal();
			TNProcAddCylinder(Solid, P - Nrm * 3.0, P + Nrm * 5.0, R, R * 0.86, 6, Accent);
		};
		Plate(0.0, 0.0, 24.0);
		for (int32 k = 0; k < 6; ++k) { Plate(FMath::DegreesToRadians(46.0), TNProcMap::TwoPi * k / 6.0, 21.0); }
		for (int32 k = 0; k < 10; ++k) { Plate(FMath::DegreesToRadians(76.0), TNProcMap::TwoPi * (k + 0.5) / 10.0, 13.0); }
		// Cabeza, hocico y ojos.
		TNProcAddCylinder(Solid, FVector(78.0, 0.0, Z0 + 38.0), FVector(118.0, 0.0, Z0 + 58.0), 21.0, 17.0, 8, Stone * 0.95f);
		TNPropMesh::TNPropBall(Solid, FVector(136.0, 0.0, Z0 + 62.0), 30.0, Stone, 9, 5, 0.82);
		TNPropMesh::TNPropBall(Solid, FVector(160.0, 0.0, Z0 + 56.0), 15.0, Stone * 0.95f, 7, 3, 0.8);
		for (const double Sy : { -1.0, 1.0 })
		{
			TNPropMesh::TNPropBall(Glow, FVector(151.0, Sy * 17.0, Z0 + 71.0), 5.5, Eyes, 6, 3);
			// Aletas delanteras y traseras, y cola.
			TNFormTube(Solid, { FVector(45.0, Sy * 74.0, Z0 + 18.0), FVector(82.0, Sy * 120.0, Z0 + 10.0), FVector(108.0, Sy * 150.0, Z0 + 4.0) }, { 22.0, 15.0, 6.0 }, 6, Stone * 0.92f);
			TNFormTube(Solid, { FVector(-55.0, Sy * 70.0, Z0 + 16.0), FVector(-84.0, Sy * 98.0, Z0 + 8.0), FVector(-100.0, Sy * 112.0, Z0 + 4.0) }, { 18.0, 12.0, 5.0 }, 6, Stone * 0.92f);
		}
		TNProcAddCylinder(Solid, FVector(-94.0, 0.0, Z0 + 18.0), FVector(-128.0, 0.0, Z0 + 10.0), 12.0, 2.0, 6, Stone * 0.92f);
		if (Crown == 1)
		{
			TNPropMesh::TNPropTorus(Solid, FVector(136.0, 0.0, Z0 + 86.0), FVector::UpVector, 19.0, 4.5, 12, 4, Accent, Accent, 0);
			for (int32 k = 0; k < 5; ++k)
			{
				const double A = TNProcMap::TwoPi * k / 5.0;
				const FVector B(136.0 + FMath::Cos(A) * 19.0, FMath::Sin(A) * 19.0, Z0 + 88.0);
				TNProcAddCylinder(Solid, B, B + FVector(0.0, 0.0, 16.0), 5.0, 0.5, 4, Accent);
			}
		}
		else if (Crown == 2)
		{
			for (int32 k = 0; k < 9; ++k)
			{
				const double A = TNProcMap::TwoPi * k / 9.0;
				TNPropMesh::TNPropBall(Solid, FVector(100.0 + FMath::Cos(A) * 8.0, FMath::Sin(A) * 25.0, Z0 + 48.0 + FMath::Cos(A) * 12.0), 9.0, FLinearColor(0.2f, 0.5f, 0.16f) * (0.85f + 0.3f * static_cast<float>(k % 3) / 2.f), 5, 3, 0.6);
			}
		}
		else if (Crown == 3)
		{
			const FVector B(158.0, 0.0, Z0 + 82.0);
			const FVector Ax = FVector(0.5, 0.0, 1.0).GetSafeNormal();
			TNProcAddCylinder(Glow, B - Ax * 6.0, B + Ax * 16.0, 7.0, 6.5, 6, Eyes, false);
			TNProcAddCylinder(Glow, B + Ax * 16.0, B + Ax * 27.0, 6.5, 0.5, 6, Eyes * 1.2f, false);
		}
		if (bMoss)
		{
			for (int32 k = 0; k < 7; ++k)
			{
				const double A = TNProcHashNoise(k, 3, Seed) * PI;
				const double R = 60.0 + 30.0 * TNProcHashNoise(k, 4, Seed);
				TNPropMesh::TNPropBall(Solid, FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, Z0 + 14.0 + 88.0 * FMath::Cos(FMath::Asin(FMath::Min(0.95, R / 100.0)))),
					18.0 + 8.0 * TNProcHashNoise(k, 5, Seed), FLinearColor(0.22f, 0.4f, 0.14f), 6, 3, 0.35);
			}
			TNPropMesh::TNPropBall(Solid, FVector(-60.0, 70.0, 124.0), 30.0, FLinearColor(0.2f, 0.38f, 0.13f), 6, 3, 0.3);
		}
		// Ofrendas: dos velas y una vasija.
		if (!bOfferings) { return; }
		for (const double Sy : { -1.0, 1.0 })
		{
			const FVector C(172.0, Sy * 48.0, 0.0);
			TNProcAddCylinder(Solid, C, C + FVector(0.0, 0.0, 18.0 + 6.0 * Sy), 5.0, 5.0, 6, FLinearColor(0.9f, 0.86f, 0.74f));
			const FVector Wick = C + FVector(0.0, 0.0, 18.0 + 6.0 * Sy);
			TNProcAddCylinder(Glow, Wick, Wick + FVector(0.0, 0.0, 12.0), 3.5, 0.3, 5, FLinearColor(1.f, 0.72f, 0.25f), false);
		}
		FTNProcMeshBuffers Pot;
		TNPropMesh::TNPropClayPot(Pot, static_cast<int32>(Seed % 2u), Seed);
		TNPropMesh::TNPropAppend(Solid, Pot, FVector(182.0, 0.0, 0.0), 20.0, 0.75);
	}

	/**
	 * Construye una formación en local (Out) según Kind. Ground(X, Y) da la cota local del terreno.
	 * Los arcos se apoyan en el suelo a ambos lados del camino (Y = +-Width/2).
	 */
	template <typename FGround>
	void TNFormBuild(FTNProcMeshBuffers& M, EFormation Kind, const FTNFormParams& P, const FTNFormColors& C, const FGround& Ground)
	{
		const uint32 Seed = P.Seed;
		auto Rand = [Seed](int32 K, double Lo, double Hi) { return TNFloraRand(Seed, K, Lo, Hi); };
		switch (Kind)
		{
			case EFormation::StoneArch:
			{
				// Arco natural: banda de roca de pie a pie con la cima sobre el camino; estratos por
				// tramos y musgo o arena en lo que mira arriba.
				const double Half = P.Width * 0.5;
				const double Thick = Rand(1, 170.0, 260.0);
				const double Apex = P.Height + Thick * 0.5;
				const double GL = Ground(0.0, -Half) - 150.0;
				const double GR = Ground(0.0, Half) - 150.0;
				constexpr int32 Stations = 16;
				constexpr int32 Seg = 8;
				TArray<TArray<FVector>> Rings;
				TArray<FVector> Centers;
				for (int32 s = 0; s <= Stations; ++s)
				{
					const double U = static_cast<double>(s) / Stations;
					const double Sh = FMath::Pow(FMath::Sin(PI * U), 0.55);
					Centers.Add(FVector(0.0, (U - 0.5) * P.Width, FMath::Lerp(GL, GR, U) * (1.0 - Sh) + Apex * Sh));
				}
				for (int32 s = 0; s <= Stations; ++s)
				{
					const FVector Tn = (Centers[FMath::Min(s + 1, Stations)] - Centers[FMath::Max(s - 1, 0)]).GetSafeNormal();
					const FVector Nc(0.0, -Tn.Z, Tn.Y);
					const double U = static_cast<double>(s) / Stations;
					const double Grow = 1.0 + 0.9 * FMath::Pow(FMath::Abs(U - 0.5) * 2.0, 3.0);
					TArray<FVector> Ring;
					for (int32 k = 0; k < Seg; ++k)
					{
						const double A = TNProcMap::TwoPi * k / Seg;
						const double J = 1.0 + 0.16 * TNProcHashNoise(s, k, Seed);
						Ring.Add(Centers[s] + FVector(1.0, 0.0, 0.0) * (FMath::Cos(A) * P.Length * 0.5 * Grow * J) + Nc * (FMath::Sin(A) * Thick * 0.5 * Grow * J));
					}
					Rings.Add(Ring);
				}
				for (int32 s = 0; s < Stations; ++s)
				{
					const FLinearColor Band = (s / 2) % 2 ? C.Rock : C.Strata;
					for (int32 k = 0; k < Seg; ++k)
					{
						const int32 K1 = (k + 1) % Seg;
						const FVector Mid = (Rings[s][k] + Rings[s][K1]) * 0.5 - Centers[s];
						const bool bTop = FVector::CrossProduct(Rings[s][K1] - Rings[s][k], Rings[s + 1][k] - Rings[s][k]).GetSafeNormal().Z > 0.6
							|| Mid.GetSafeNormal().Z > 0.6;
						M.AddQuad(Rings[s][k], Rings[s][K1], Rings[s + 1][K1], Rings[s + 1][k], Mid, bTop ? TNProcLerpColor(Band, C.Moss, 0.6f) : Band);
					}
				}
				break;
			}
			case EFormation::WhaleRibs:
			{
				// Esqueleto de ballena varado sobre el camino: la columna va en arco a la altura de las
				// costillas (más altas en el pecho que en los extremos), con una vértebra por costilla
				// (cuerpo, apófisis espinosa y transversas); cada costilla nace de su vértebra, se abre hacia
				// fuera y baja hasta el suelo a cada lado. Por detrás la cola baja curvándose hasta la arena
				// junto a una pared, y por delante la columna baja al cráneo, tumbado junto a la otra con sus
				// dos mandíbulas.
				const double Half = P.Width * 0.5;
				const int32 Ribs = FMath::Max(5, FMath::RoundToInt(P.Length / 140.0));
				const double Step = P.Length / Ribs;
				const double Side = (Seed & 1u) ? 1.0 : -1.0;
				auto Taper = [&](double X)
				{
					const double T = FMath::Clamp(FMath::Abs(X) / (P.Length * 0.5 + Step * 0.5), 0.0, 1.0);
					return 1.0 - 0.4 * T * T;
				};
				auto SpineZ = [&](double X) { return (P.Height + 90.0) * Taper(X) + 20.0; };
				const double EndX = P.Length * 0.5 - Step * 0.5;
				const double TailLen = 700.0 + 0.3 * P.Length;
				const double SkullX = -EndX - 420.0;
				const FVector SkullC(SkullX, -Side * Half * 0.8, Ground(SkullX, -Side * Half * 0.8) + 95.0);
				// Eje de la columna: cola (en el suelo junto a una pared), arco sobre el camino y bajada al cráneo.
				TArray<FVector> Axis;
				TArray<double> AxisR;
				for (int32 k = 0; k <= 10; ++k)
				{
					const double U = k / 10.0;
					const double Sm = U * U * (3.0 - 2.0 * U);
					const FVector2D Q(EndX + TailLen * U, Side * Half * 0.85 * Sm);
					const double Z = FMath::Lerp(SpineZ(EndX), Ground(Q.X, Q.Y) + 22.0, FMath::Pow(Sm, 0.8));
					Axis.Insert(FVector(Q.X, Q.Y, Z), 0);
					AxisR.Insert(FMath::Lerp(22.0, 7.0, U), 0);
				}
				for (int32 k = 1; k <= 16; ++k)
				{
					const double X = FMath::Lerp(EndX, -EndX, k / 16.0);
					Axis.Add(FVector(X, 0.0, SpineZ(X)));
					AxisR.Add(22.0);
				}
				for (int32 k = 1; k <= 6; ++k)
				{
					const double U = k / 6.0;
					const FVector From(-EndX, 0.0, SpineZ(-EndX));
					const FVector To = SkullC + FVector(160.0, Side * Half * 0.2, 40.0);
					FVector Pt = FMath::Lerp(From, To, U);
					Pt.Z = FMath::Lerp(From.Z, To.Z, U * U);
					Axis.Add(Pt);
					AxisR.Add(FMath::Lerp(22.0, 26.0, U));
				}
				TNFormTube(M, Axis, AxisR, 7, C.Bone * 0.9f);
				// Vértebra en P (dirección D de la columna) de escala S.
				auto Vertebra = [&](const FVector& Pv, const FVector& Dv, double S)
				{
					const FVector D = Dv.GetSafeNormal();
					FVector Y = FVector::CrossProduct(FVector::UpVector, D).GetSafeNormal();
					if (Y.IsNearlyZero()) { Y = FVector(0.0, 1.0, 0.0); }
					const FVector Up = FVector::CrossProduct(D, Y).GetSafeNormal();
					TNFormCylinder(M, Pv - D * (20.0 * S), Pv + D * (20.0 * S), 38.0 * S, 38.0 * S, 8, C.Bone);
					M.AddBeam(Pv + Up * (30.0 * S), Pv + Up * (78.0 * S) - D * (10.0 * S), 9.0 * S, C.Bone * 0.95f);
					for (const double Sd : { -1.0, 1.0 })
					{
						M.AddBeam(Pv + Y * (Sd * 30.0 * S), Pv + Y * (Sd * 72.0 * S) - Up * (8.0 * S), 7.0 * S, C.Bone * 0.95f);
					}
				};
				// Costillas, una por vértebra del arco.
				for (int32 r = 0; r < Ribs; ++r)
				{
					const double X = (r - (Ribs - 1) * 0.5) * Step;
					const double T = Taper(X);
					const double Top = SpineZ(X);
					Vertebra(FVector(X, 0.0, Top), FVector(1.0, 0.0, 0.0), 0.75 + 0.35 * T);
					for (const double Sd : { -1.0, 1.0 })
					{
						const double Ry = Half * 1.05;
						const double Foot = Ground(X + 50.0, Sd * Ry * 0.93) - 35.0;
						const double Rz = (Top - Foot) / 1.37;
						const double Zc = Top - Rz;
						TArray<FVector> Pts;
						TArray<double> Rad;
						constexpr int32 N = 9;
						for (int32 t = 0; t <= N; ++t)
						{
							const double U = static_cast<double>(t) / N;
							const double Phi = FMath::DegreesToRadians(FMath::Lerp(86.0, -22.0, U));
							Pts.Add(FVector(X + 50.0 * (1.0 - FMath::Sin(Phi)), Sd * (Ry * FMath::Cos(Phi) + 22.0 * (1.0 - U)), Zc + Rz * FMath::Sin(Phi)));
							Rad.Add(FMath::Lerp(23.0, 10.0, U) * (0.6 + 0.4 * T) + 4.0);
						}
						TNFormTube(M, Pts, Rad, 6, C.Bone);
					}
				}
				// Vértebras de la cola, más pequeñas hacia la punta.
				for (int32 k = 1; k + 1 < 11; k += 1)
				{
					const double S = FMath::Lerp(0.3, 0.8, k / 10.0);
					Vertebra(Axis[k], Axis[k + 1] - Axis[k - 1], S);
				}
				// Cráneo: bóveda aplanada y hocico, y las dos mandíbulas largas por delante.
				TNFloraBlob(M, SkullC, 200.0, 95.0, Seed + 5u, C.Bone, 9);
				TNFloraBlob(M, SkullC + FVector(-230.0, Side * 30.0, -35.0), 150.0, 60.0, Seed + 6u, C.Bone * 0.95f, 8);
				for (const double Sd : { -1.0, 1.0 })
				{
					const double Y0 = SkullC.Y + Sd * 110.0;
					TArray<FVector> Jaw = {
						FVector(SkullX + 90.0, Y0, Ground(SkullX + 90.0, Y0) + 45.0),
						FVector(SkullX - 250.0, Y0 - Sd * 25.0, Ground(SkullX - 250.0, Y0) + 32.0),
						FVector(SkullX - 560.0, Y0 - Sd * 80.0, Ground(SkullX - 560.0, Y0 - Sd * 80.0) + 22.0) };
					TNFormTube(M, Jaw, { 26.0, 20.0, 12.0 }, 6, C.Bone * 0.97f);
				}
				break;
			}
			case EFormation::RootArch:
			{
				// Raíces gigantes enroscadas que cruzan por encima, con raicillas colgando.
				const double Half = P.Width * 0.5;
				const int32 Roots = 3 + static_cast<int32>(Seed % 2u);
				for (int32 r = 0; r < Roots; ++r)
				{
					const double X0 = Rand(10 + r, -P.Length, P.Length);
					const double X1 = Rand(20 + r, -P.Length, P.Length);
					const double Top = P.Height * Rand(30 + r, 0.9, 1.15) + 60.0;
					TArray<FVector> Pts;
					TArray<double> Rad;
					for (int32 t = 0; t <= 10; ++t)
					{
						const double U = t / 10.0;
						const double Y = (U - 0.5) * P.Width * 1.05;
						const double G = FMath::Lerp(Ground(X0, -Half), Ground(X1, Half), U) - 60.0;
						const double Z = FMath::Lerp(G, Top, FMath::Pow(FMath::Sin(PI * U), 0.6)) + 40.0 * FMath::Sin(U * 9.0 + r);
						Pts.Add(FVector(FMath::Lerp(X0, X1, U) + 50.0 * FMath::Sin(U * 7.0 + r * 2.0), Y, Z));
						Rad.Add(FMath::Lerp(55.0, 34.0, FMath::Sin(PI * U)));
					}
					TNFormTube(M, Pts, Rad, 7, C.Wood, &C.Moss);
					for (int32 h = 2; h <= 8; h += 3)
					{
						M.AddBeam(Pts[h] - FVector(0.0, 0.0, Rad[h]), Pts[h] - FVector(0.0, 0.0, Rad[h] + Rand(40 + h + r, 150.0, 320.0)), 5.0, C.Wood * 0.9f);
					}
				}
				break;
			}
			case EFormation::TempleGate:
			{
				// Pórtico: dos pilares de sillares desiguales, dintel con remate y musgo; a veces el dintel
				// está roto y un trozo caído junto a un pilar.
				const double Half = P.Width * 0.5;
				const double Top = P.Height;
				const bool bBroken = (Seed % 3u) == 0u;
				for (int32 Side = -1; Side <= 1; Side += 2)
				{
					const double Y = Side * Half;
					const double G = Ground(0.0, Y) - 60.0;
					const int32 Blocks = FMath::Max(2, FMath::CeilToInt((Top - G) / 110.0));
					for (int32 b = 0; b < Blocks; ++b)
					{
						const double Z0 = G + (Top - G) * b / Blocks;
						const double Hb = (Top - G) / Blocks * 0.5;
						const FLinearColor Col = (b % 2) ? C.Rock : C.Strata;
						TNFormBox(M, FVector(Rand(10 + b + Side, -8.0, 8.0), Y + Rand(20 + b + Side, -8.0, 8.0), Z0 + Hb), FVector(95.0, 95.0, Hb), Rand(30 + b + Side, -0.06, 0.06), Col);
					}
					TNFloraBlob(M, FVector(0.0, Y, Top + 10.0), 80.0, 18.0, Seed + 40u + Side, C.Moss, 6);
				}
				if (!bBroken)
				{
					TNFormBox(M, FVector(0.0, 0.0, Top + 60.0), FVector(110.0, Half + 160.0, 60.0), 0.0, C.Strata);
					TNFormBox(M, FVector(0.0, 0.0, Top + 150.0), FVector(80.0, Half * 0.55, 30.0), 0.0, C.Rock);
					for (int32 Side = -1; Side <= 1; Side += 2)
					{
						TNFormBox(M, FVector(0.0, Side * (Half + 60.0), Top + 170.0), FVector(70.0, 70.0, 50.0), 0.0, C.Rock * 0.95f);
					}
					TNFloraBlob(M, FVector(0.0, Rand(50, -Half, Half) * 0.5, Top + 125.0), 140.0, 22.0, Seed + 60u, C.Moss, 7);
				}
				else
				{
					// Medio dintel en su sitio y el otro medio tirado al pie.
					TNFormBox(M, FVector(0.0, -Half * 0.45, Top + 60.0), FVector(110.0, Half * 0.62, 60.0), 0.0, C.Strata);
					const double Y = Half * 0.55;
					TNFormBox(M, FVector(160.0, Y, Ground(160.0, Y) + 50.0), FVector(110.0, Half * 0.5, 55.0), 0.35, C.Strata * 0.95f);
				}
				break;
			}
			case EFormation::FallenTrunk:
			{
				// Tronco colosal caído de pared a pared por encima del camino: cepellón de raíces en un
				// extremo, muñones de ramas, musgo encima y lianas que cuelgan de la panza (sin bajar de 3 m).
				const double Half = P.Width * 0.5;
				const double R = FMath::Clamp(P.Length * 0.45, 110.0, 190.0);
				const double Skew = Rand(1, -0.35, 0.35);
				const double Ya = -(Half + 350.0), Yb = Half + 350.0;
				const FVector A(Ya * Skew, Ya, FMath::Max(Ground(Ya * Skew, Ya), P.Height + R * Rand(2, 0.9, 1.4)));
				const FVector B(Yb * Skew, Yb, FMath::Max(Ground(Yb * Skew, Yb), P.Height + R * Rand(3, 0.9, 1.4)));
				TNProcAddLog(M, A, B, R, Seed, C.Wood, C.Wood * 1.4f);
				const FVector Axis = (B - A).GetSafeNormal();
				const FVector Ux = FVector::CrossProduct(Axis, FVector::UpVector).GetSafeNormal();
				const FVector Vx = FVector::CrossProduct(Ux, Axis);
				for (int32 k = 0; k < 9; ++k)
				{
					const double Ang = TNProcMap::TwoPi * k / 9.0;
					const FVector Out = Ux * FMath::Cos(Ang) + Vx * FMath::Sin(Ang);
					const double Len = R * Rand(10 + k, 1.6, 2.6);
					TNFormTube(M, { A + Out * (R * 0.6), A + Out * (R + Len * 0.5) + Axis * (Len * 0.3), A + Out * (R + Len) + Axis * (Len * 0.2) - FVector(0.0, 0.0, Len * 0.2) },
						{ R * 0.28, R * 0.18, R * 0.06 }, 5, C.Wood * 0.9f);
				}
				TNFloraBlob(M, A - Axis * (R * 0.4), R * 1.3, R * 1.1, Seed + 3u, FLinearColor(0.3f, 0.22f, 0.14f), 8);
				for (int32 k = 0; k < 7; ++k)
				{
					const FVector P0 = FMath::Lerp(A, B, Rand(20 + k, 0.2, 0.9));
					TNFloraBlob(M, P0 + FVector(0.0, 0.0, R * 0.85), R * Rand(30 + k, 0.5, 0.9), R * 0.2, Seed + 40u + k, C.Moss, 6);
				}
				for (int32 k = 0; k < 9; ++k)
				{
					const FVector Top = FMath::Lerp(A, B, Rand(50 + k, 0.25, 0.85)) - FVector(0.0, 0.0, R * 0.9);
					const double Len = FMath::Min(Rand(60 + k, 150.0, 380.0), Top.Z - 300.0);
					if (Len < 60.0) { continue; }
					TArray<FVector> Pts;
					TArray<double> Rad;
					for (int32 t = 0; t <= 5; ++t)
					{
						Pts.Add(Top + FVector(Rand(70 + k, -20.0, 20.0) * FMath::Sin(t * 0.9), 0.0, -Len * t / 5.0));
						Rad.Add(FMath::Lerp(4.0, 2.0, t / 5.0));
					}
					TNFormTube(M, Pts, Rad, 4, C.Moss * 0.9f);
				}
				break;
			}
			case EFormation::RuinedAqueduct:
			{
				// Tramo de acueducto en ruinas de lado a lado del cañón: pilas hasta el suelo, arco rebajado de
				// dovelas, relleno de fábrica sobre cada dovela hasta la cornisa y el canal arriba; a veces un
				// pretil del canal se ha caído y sus sillares están junto al camino.
				const double Half = P.Width * 0.5;
				const double Depth = FMath::Clamp(P.Length * 0.5, 120.0, 200.0);
				const double Top = P.Height + 60.0;
				const double Rise = FMath::Min(Half * 0.45, P.Height * 0.4);
				const double Span = Half + 40.0;
				const double Rc = (Span * Span + Rise * Rise) / (2.0 * Rise);
				const double Zc = Top - Rc;
				const double DeckZ = Top + 130.0;
				const bool bBroken = (Seed % 3u) == 1u;
				for (int32 Side = -1; Side <= 1; Side += 2)
				{
					const double Y = Side * (Span + 90.0);
					const double G = Ground(0.0, Y) - 80.0;
					TNFormBox(M, FVector(0.0, Y, 0.5 * (G + DeckZ - 80.0)), FVector(Depth + 20.0, 110.0, 0.5 * (DeckZ - 80.0 - G)), 0.0, C.Strata);
				}
				constexpr int32 N = 15;
				const double A0 = FMath::Asin(FMath::Clamp(Span / Rc, -1.0, 1.0));
				for (int32 k = 0; k < N; ++k)
				{
					const double Aa = -A0 + 2.0 * A0 * k / N, Ab = -A0 + 2.0 * A0 * (k + 1) / N;
					const FVector Pa(0.0, Rc * FMath::Sin(Aa), Zc + Rc * FMath::Cos(Aa));
					const FVector Pb(0.0, Rc * FMath::Sin(Ab), Zc + Rc * FMath::Cos(Ab));
					const FVector Mid = (Pa + Pb) * 0.5;
					const FVector Out = FVector(0.0, Mid.Y, Mid.Z - Zc).GetSafeNormal();
					const FVector Tan = (Pb - Pa).GetSafeNormal();
					TNPropMesh::TNPropBox(M, Mid + Out * 45.0, FVector(1.0, 0.0, 0.0), Tan, Out, FVector(Depth, FVector::Dist(Pa, Pb) * 0.5 + 2.0, 45.0), (k % 2) ? C.Rock : C.Strata * 1.05f);
					// Relleno de fábrica desde el trasdós de la dovela hasta la cornisa.
					const double Ext = Mid.Z + Out.Z * 90.0;
					if (DeckZ - 80.0 - Ext > 10.0)
					{
						TNFormBox(M, FVector(0.0, Mid.Y, 0.5 * (Ext + DeckZ - 80.0)), FVector(Depth - 6.0, FVector::Dist(Pa, Pb) * 0.5 + 3.0, 0.5 * (DeckZ - 80.0 - Ext)), 0.0, C.Rock * 0.95f);
					}
				}
				TNFormBox(M, FVector(0.0, 0.0, DeckZ - 40.0), FVector(Depth + 15.0, Span + 230.0, 40.0), 0.0, C.Strata * 0.95f);
				for (int32 Side = -1; Side <= 1; Side += 2)
				{
					if (bBroken && Side > 0) { continue; }
					TNFormBox(M, FVector(Side * (Depth - 20.0), 0.0, DeckZ + 40.0), FVector(20.0, Span + 230.0, 40.0), 0.0, C.Strata);
				}
				if (bBroken)
				{
					for (int32 k = 0; k < 4; ++k)
					{
						const double Y = (k % 2 ? 1.0 : -1.0) * Half * Rand(80 + k, 0.7, 0.92);
						const double X = Depth + Rand(90 + k, 100.0, 260.0);
						TNFormBox(M, FVector(X, Y, Ground(X, Y) + 35.0), FVector(55.0, 90.0, 35.0), Rand(100 + k, -0.6, 0.6), C.Strata * 0.9f);
					}
				}
				TNFloraBlob(M, FVector(0.0, Rand(120, -Span, Span) * 0.6, DeckZ + 10.0), 120.0, 20.0, Seed + 7u, C.Moss, 6);
				break;
			}
			case EFormation::Shipwreck:
			{
				// Casco de costado medio enterrado, cuadernas a la vista por un boquete, mástil roto y vela rota.
				const double Len = P.Radius * 2.0;
				const double Beam = P.Radius * 0.62;
				const double Depth = P.Radius * 0.5;
				const double Roll = FMath::DegreesToRadians(Rand(1, 24.0, 38.0)) * (Seed % 2u ? 1.0 : -1.0);
				const double Sink = Depth * 0.3;
				constexpr int32 Stations = 9;
				constexpr int32 Seg = 8;
				auto Rot = [Roll](const FVector& V) { return FVector(V.X, V.Y * FMath::Cos(Roll) - V.Z * FMath::Sin(Roll), V.Y * FMath::Sin(Roll) + V.Z * FMath::Cos(Roll)); };
				TArray<TArray<FVector>> Rings;
				for (int32 s = 0; s <= Stations; ++s)
				{
					const double U = static_cast<double>(s) / Stations;
					const double Fullness = FMath::Pow(FMath::Sin(PI * FMath::Lerp(0.04, 0.96, U)), 0.55);
					const double Rise = 60.0 * FMath::Pow(FMath::Abs(U - 0.5) * 2.0, 2.0);
					TArray<FVector> Ring;
					for (int32 k = 0; k < Seg; ++k)
					{
						// Sección en U cerrada por la cubierta: de babor a estribor por la quilla.
						const double A = PI * (0.0 + static_cast<double>(k) / (Seg - 1));
						const double Y = FMath::Cos(A) * Beam * 0.5 * Fullness;
						const double Z = -FMath::Sin(A) * Depth * FMath::Lerp(0.7, 1.0, Fullness) + Rise;
						Ring.Add(Rot(FVector((U - 0.5) * Len, Y, Z + Depth * 0.55)) - FVector(0.0, 0.0, Sink));
					}
					Rings.Add(Ring);
				}
				for (int32 s = 0; s < Stations; ++s)
				{
					for (int32 k = 0; k < Seg; ++k)
					{
						const int32 K1 = (k + 1) % Seg;
						const FVector Ctr = Rot(FVector(((static_cast<double>(s) + 0.5) / Stations - 0.5) * Len, 0.0, Depth * 0.2)) - FVector(0.0, 0.0, Sink);
						const FVector Mid = (Rings[s][k] + Rings[s][K1]) * 0.5 - Ctr;
						const FLinearColor Col = k == Seg - 1 ? C.Wood * 1.25f : ((k + s) % 2 ? C.Wood : C.Wood * 0.8f);
						// Boquete en el costado de arriba: faltan dos tracas en medio del casco.
						if (s >= 3 && s <= 5 && (k == 1 || k == 2) && Roll > 0.0) { continue; }
						if (s >= 3 && s <= 5 && (k == Seg - 3 || k == Seg - 4) && Roll < 0.0) { continue; }
						M.AddQuad(Rings[s][k], Rings[s][K1], Rings[s + 1][K1], Rings[s + 1][k], Mid, Col);
					}
				}
				for (int32 End = 0; End <= 1; ++End)
				{
					const TArray<FVector>& R0 = End ? Rings.Last() : Rings[0];
					FVector Ctr = FVector::ZeroVector;
					for (const FVector& V : R0) { Ctr += V; }
					Ctr /= static_cast<double>(R0.Num());
					for (int32 k = 0; k < Seg; ++k) { M.AddTri(Ctr, R0[k], R0[(k + 1) % Seg], FVector(End ? 1.0 : -1.0, 0.0, 0.0), C.Wood * 0.85f); }
				}
				const FVector Down(0.0, 0.0, Sink);
				// Cuadernas vistas por el boquete.
				for (int32 f = 0; f < 3; ++f)
				{
					const double X = (0.36 + 0.14 * f - 0.5) * Len;
					M.AddBeam(Rot(FVector(X, -Beam * 0.45, Depth * 0.5)) - Down, Rot(FVector(X, -Beam * 0.1, -Depth * 0.3)) - Down, 9.0, C.Wood * 0.7f);
					M.AddBeam(Rot(FVector(X, Beam * 0.45, Depth * 0.5)) - Down, Rot(FVector(X, Beam * 0.1, -Depth * 0.3)) - Down, 9.0, C.Wood * 0.7f);
				}
				// Castillo de popa sobre la cubierta, con su barandilla.
				{
					FTNProcMeshBuffers Stern;
					TNFormBox(Stern, FVector(Len * 0.36, 0.0, Depth * 0.55 + 70.0), FVector(Len * 0.12, Beam * 0.42, 70.0), 0.0, C.Wood * 1.1f);
					TNFormBox(Stern, FVector(Len * 0.47, 0.0, Depth * 0.55 + 110.0), FVector(8.0, Beam * 0.34, 60.0), 0.0, C.Wood * 0.8f);
					TNFormBox(Stern, FVector(Len * 0.37, Beam * 0.3, Depth * 0.55 + 105.0), FVector(18.0, 2.0, 18.0), 0.0, C.Dark * 3.0f);
					for (int32 i = 0; i < Stern.Verts.Num(); ++i)
					{
						Stern.Verts[i] = Rot(Stern.Verts[i]) - Down;
						Stern.Normals[i] = Rot(Stern.Normals[i]);
					}
					TNFormAppend(M, Stern, FVector::ZeroVector, FVector2D(1.0, 0.0));
				}
				// Muñón del trinquete a proa.
				TNFormCylinder(M, Rot(FVector(-Len * 0.3, 0.0, Depth * 0.5)) - Down, Rot(FVector(-Len * 0.3 - 30.0, 0.0, Depth * 0.5 + P.Radius * 0.3)) - Down, 13.0, 11.0, 6, C.Wood * 0.85f);
				// Palo mayor partido, con cofa, y vela hecha jirones colgando de la verga.
				const FVector MastBase = Rot(FVector(Len * 0.05, 0.0, Depth * 0.55)) - Down;
				const FVector MastTop = MastBase + Rot(FVector(40.0, 0.0, P.Radius * 1.0));
				TNFormCylinder(M, MastBase, MastTop, 18.0, 12.0, 6, C.Wood * 0.9f);
				TNFormCylinder(M, FMath::Lerp(MastBase, MastTop, 0.82), FMath::Lerp(MastBase, MastTop, 0.9), 45.0, 45.0, 8, C.Wood * 0.8f);
				const FVector YardA = MastBase + Rot(FVector(20.0, -P.Radius * 0.28, P.Radius * 0.55));
				const FVector YardB = MastBase + Rot(FVector(20.0, P.Radius * 0.28, P.Radius * 0.55));
				M.AddBeam(YardA, YardB, 6.0, C.Wood * 0.85f);
				const FVector Drop = Rot(FVector(0.0, 0.0, -P.Radius * 0.35));
				const FVector SailMid = (YardA + YardB) * 0.5;
				M.AddQuad(YardA, SailMid, SailMid + Drop * 0.6, YardA + Drop, FVector(1.0, 0.0, 0.0), C.Cloth);
				M.AddQuad(YardA, YardA + Drop, SailMid + Drop * 0.6, SailMid, FVector(-1.0, 0.0, 0.0), C.Cloth * 0.85f);
				M.AddTri(SailMid, YardB, SailMid + Drop * 0.35, FVector(1.0, 0.0, 0.0), C.Cloth);
				M.AddTri(SailMid, SailMid + Drop * 0.35, YardB, FVector(-1.0, 0.0, 0.0), C.Cloth * 0.85f);
				break;
			}
			case EFormation::StoneHead:
			{
				// Cabeza colosal: casco con banda, orejeras, cejas, nariz ancha, labios gruesos y musgo arriba.
				const double R = P.Radius;
				const double H = P.Height;
				// Cara: barbilla más estrecha, pómulos anchos; casco con banda y cimera.
				TNFloraLathe(M, FVector(0.0, 0.0, -40.0), { 0.0, H * 0.18, H * 0.45, H * 0.68 }, { R * 0.72, R * 0.9, R, R * 0.98 },
					0.03, Seed, C.Rock, 12, false);
				TNFloraLathe(M, FVector(0.0, 0.0, H * 0.68 - 40.0), { 0.0, H * 0.14, H * 0.3, H * 0.36 }, { R * 1.08, R * 1.06, R * 0.8, R * 0.3 },
					0.02, Seed + 1u, C.Strata, 12, false);
				for (int32 Side = -1; Side <= 1; Side += 2)
				{
					TNFormCylinder(M, FVector(0.0, Side * R * 0.9, H * 0.5), FVector(0.0, Side * R * 1.08, H * 0.5), R * 0.28, R * 0.24, 8, C.Strata);
					// Ojo: párpado en relieve y pupila oscura.
					TNFormBox(M, FVector(R * 0.93, Side * R * 0.33, H * 0.58), FVector(R * 0.08, R * 0.2, R * 0.08), 0.0, C.Strata);
					TNFormBox(M, FVector(R * 0.99, Side * R * 0.33, H * 0.57), FVector(R * 0.03, R * 0.08, R * 0.05), 0.0, C.Dark);
					TNFormBox(M, FVector(R * 0.92, Side * R * 0.33, H * 0.66), FVector(R * 0.12, R * 0.24, R * 0.05), 0.0, C.Rock * 0.9f);
				}
				TNFormBox(M, FVector(R * 1.0, 0.0, H * 0.43), FVector(R * 0.18, R * 0.17, R * 0.16), 0.0, C.Rock);
				TNFormBox(M, FVector(R * 0.96, 0.0, H * 0.27), FVector(R * 0.12, R * 0.34, R * 0.07), 0.0, C.Strata * 0.9f);
				TNFloraBlob(M, FVector(0.0, 0.0, H * 0.98), R * 0.6, R * 0.12, Seed + 3u, C.Moss, 8);
				break;
			}
			case EFormation::BasaltColumns:
			{
				// Columnas hexagonales de alturas escalonadas (más altas por detrás).
				const double Cell = FMath::Max(55.0, P.Radius / 5.0);
				const double Hx = Cell * 0.58;
				for (int32 q = -8; q <= 8; ++q)
				{
					for (int32 r = -8; r <= 8; ++r)
					{
						const double X = Cell * (q + r * 0.5);
						const double Y = Cell * r * 0.866;
						if (X * X + Y * Y > P.Radius * P.Radius) { continue; }
						const double Back = 0.5 + 0.5 * (-X / P.Radius);
						const double H = P.Height * FMath::Clamp(0.25 + 0.75 * Back + 0.25 * TNProcHashNoise(q, r, Seed), 0.15, 1.1);
						TArray<FVector2D> Hex;
						for (int32 k = 0; k < 6; ++k)
						{
							const double A = TNProcMap::TwoPi * (k + 0.5) / 6.0;
							Hex.Add(FVector2D(X + FMath::Cos(A) * Hx, Y + FMath::Sin(A) * Hx));
						}
						const double G = Ground(X, Y) - 30.0;
						const float Tone = 0.85f + 0.3f * static_cast<float>(0.5 + 0.5 * TNProcHashNoise(r, q, Seed + 7u));
						M.AddPrism(Hex, G + H, G - 40.0, C.Rock * Tone);
					}
				}
				break;
			}
			case EFormation::Fumarole:
			{
				// Cono bajo de fumarola con costra de azufre alrededor de una boca oscura.
				const double R = P.Radius;
				const double H = P.Height;
				TNFloraLathe(M, FVector(0.0, 0.0, -30.0), { 0.0, H * 0.55, H }, { R, R * 0.62, R * 0.36 }, 0.14, Seed, C.Rock, 10, false);
				TArray<FVector2D> Mouth, Crust;
				for (int32 k = 0; k < 10; ++k)
				{
					const double A = TNProcMap::TwoPi * k / 10.0;
					Mouth.Add(FVector2D(FMath::Cos(A) * R * 0.26, FMath::Sin(A) * R * 0.26));
					Crust.Add(FVector2D(FMath::Cos(A) * R * 0.44, FMath::Sin(A) * R * 0.44));
				}
				M.AddPrism(Crust, H - 30.0 + R * 0.12, H - 50.0, C.Moss, false);
				M.AddPrism(Mouth, H - 25.0 + R * 0.13, H - 45.0, C.Dark, false);
				for (int32 k = 0; k < 4; ++k)
				{
					const double A = TNProcMap::TwoPi * (k + 0.3) / 4.0;
					TNFloraBead(M, FVector(FMath::Cos(A) * R * 0.75, FMath::Sin(A) * R * 0.75, H * 0.35), Rand(10 + k, 18.0, 30.0), Seed + k, C.Moss * 0.9f);
				}
				break;
			}
			case EFormation::Hoodoo:
			case EFormation::BalancedRock:
			{
				const double R = P.Radius;
				const double H = P.Height;
				if (Kind == EFormation::Hoodoo)
				{
					// Tambores de roca en capas con cinturas, rematados por un sombrero más ancho.
					const int32 Drums = 3 + static_cast<int32>(Seed % 3u);
					double Z = -40.0;
					for (int32 d = 0; d < Drums; ++d)
					{
						const double Hd = (H - 40.0) / Drums;
						const double Rd = R * FMath::Lerp(1.0, 0.7, static_cast<double>(d) / Drums);
						TNFloraLathe(M, FVector(0.0, 0.0, Z), { 0.0, Hd * 0.35, Hd * 0.75, Hd }, { Rd, Rd * 1.08, Rd * 0.8, Rd * 0.62 }, 0.12, Seed + d, (d % 2) ? C.Rock : C.Strata, 9, false);
						Z += Hd;
					}
					TNProcAddBoulder(M, FVector(0.0, 0.0, Z - 10.0), R * 1.35, R * 0.55, Seed + 50u, C.Strata * 0.9f);
				}
				else
				{
					// Pedestal fino y un peñasco enorme encima, como a punto de caer.
					TNFloraLathe(M, FVector(0.0, 0.0, -40.0), { 0.0, H * 0.3, H * 0.56 }, { R * 0.9, R * 0.5, R * 0.32 }, 0.12, Seed, C.Rock, 9, false);
					TNProcAddBoulder(M, FVector(R * 0.15, 0.0, H * 0.5), R * 1.35, H * 0.52, Seed + 9u, C.Strata);
				}
				break;
			}
			case EFormation::Wagon:
			{
				// Carreta de lona: caja, cuatro ruedas (una rota), aros y lona rasgada, lanza.
				const FLinearColor Wood = C.Wood * 1.1f;
				TNFormBox(M, FVector(0.0, 0.0, 95.0), FVector(200.0, 85.0, 30.0), 0.0, Wood);
				for (int32 Wx = -1; Wx <= 1; Wx += 2)
				{
					for (int32 Wy = -1; Wy <= 1; Wy += 2)
					{
						const bool bBroken = Wx == 1 && Wy == 1;
						TNFormWheel(M, FVector(Wx * 130.0, Wy * 100.0, bBroken ? 35.0 : 58.0), 58.0, C.Wood * 0.8f);
					}
				}
				for (int32 h = 0; h < 4; ++h)
				{
					const double X = -160.0 + 107.0 * h;
					TArray<FVector> Pts;
					TArray<double> Rad;
					for (int32 t = 0; t <= 6; ++t)
					{
						const double A = PI * t / 6.0;
						Pts.Add(FVector(X, FMath::Cos(A) * 90.0, 125.0 + FMath::Sin(A) * 110.0));
						Rad.Add(3.0);
					}
					TNFormTube(M, Pts, Rad, 4, C.Wood * 0.7f);
					if (h < 3)
					{
						// Lona entre aros (a trozos: le falta alguno).
						for (int32 t = 0; t < 6; ++t)
						{
							if ((t + h + static_cast<int32>(Seed % 4u)) % 5 == 0) { continue; }
							const FVector A0 = Pts[t], A1 = Pts[t + 1];
							const FVector B0 = A0 + FVector(107.0, 0.0, 0.0), B1 = A1 + FVector(107.0, 0.0, 0.0);
							const FVector Out = (A0 + A1) * 0.5 - FVector(X, 0.0, 125.0);
							M.AddQuad(A0, A1, B1, B0, Out, C.Cloth);
							M.AddQuad(A0, B0, B1, A1, -Out, C.Cloth * 0.8f);
						}
					}
				}
				M.AddBeam(FVector(200.0, 0.0, 80.0), FVector(420.0, 0.0, 20.0), 6.0, C.Wood * 0.85f);
				break;
			}
			case EFormation::Cannon:
			{
				// Cañón de bronce en su cureña, apuntando al frente, y una pila de balas.
				const FLinearColor Bronze(0.22f, 0.17f, 0.1f);
				TNFormCylinder(M, FVector(-90.0, 0.0, 80.0), FVector(170.0, 0.0, 110.0), 24.0, 16.0, 10, Bronze);
				TNFormCylinder(M, FVector(-100.0, 0.0, 79.0), FVector(-80.0, 0.0, 81.0), 28.0, 28.0, 10, Bronze * 0.9f);
				TNFormCylinder(M, FVector(160.0, 0.0, 108.0), FVector(176.0, 0.0, 111.0), 20.0, 20.0, 10, Bronze * 0.9f);
				for (int32 Side = -1; Side <= 1; Side += 2)
				{
					TNFormBox(M, FVector(-20.0, Side * 34.0, 55.0), FVector(110.0, 7.0, 28.0), 0.0, C.Wood);
					TNFormWheel(M, FVector(20.0, Side * 55.0, 45.0), 45.0, C.Wood * 0.85f);
				}
				M.AddBeam(FVector(20.0, -60.0, 45.0), FVector(20.0, 60.0, 45.0), 5.0, C.Metal);
				int32 Ball = 0;
				for (int32 Lv = 0; Lv < 3; ++Lv)
				{
					for (int32 a = 0; a < 3 - Lv; ++a)
					{
						for (int32 b = 0; b < 3 - Lv; ++b)
						{
							TNFloraBead(M, FVector(60.0 + (a + Lv * 0.5) * 21.0, 130.0 + (b + Lv * 0.5) * 21.0, 10.0 + Lv * 17.0), 11.0, Seed + (Ball++), FLinearColor(0.08f, 0.08f, 0.09f));
						}
					}
				}
				break;
			}
			case EFormation::Sandbags:
			{
				// Parapeto en media luna de tres hileras de sacos terreros, abierto por detrás.
				const double R = P.Radius;
				const FLinearColor Khaki(0.46f, 0.4f, 0.26f);
				int32 Bag = 0;
				for (int32 Row = 0; Row < 3; ++Row)
				{
					const double Rr = R - Row * 8.0;
					const int32 N = FMath::Max(6, FMath::RoundToInt(PI * Rr / 62.0));
					for (int32 k = 0; k <= N; ++k)
					{
						const double A = -PI * 0.5 + PI * (k + (Row % 2) * 0.5) / N;
						if (A > PI * 0.5) { continue; }
						const FVector Pos(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, 12.0 + Row * 23.0);
						const float Tone = 0.88f + 0.24f * static_cast<float>(0.5 + 0.5 * TNProcHashNoise(Bag++, Row, Seed));
						TNFormBox(M, Pos, FVector(16.0, 31.0, 11.5), A, Khaki * Tone);
					}
				}
				break;
			}
			case EFormation::Bunker:
			{
				// Búnker de hormigón entrable (#689): paredes, puerta por detrás (-X), losa volada, tronera oscura al frente y
				// camuflaje. Dentro, el refugio (ATN_BeachShelterVolume, lo pone el generador con las mismas medidas).
				const double R = P.Radius;
				const double H = P.Height;
				const TNBeachCreatureRules::Shelter::FBunkerDims Dims = TNBeachCreatureRules::Shelter::FormationBunker(R, H);
				const TArray<TNBeachCreatureRules::Shelter::FBunkerBox> Boxes = TNBeachCreatureRules::Shelter::BunkerBoxes(Dims);
				for (int32 b = 0; b < Boxes.Num(); ++b)
				{
					// Las paredes bajan 40 cm para asentarse en el terreno; el techo, no.
					const bool bRoof = b == Boxes.Num() - 1;
					const FVector Half = bRoof ? Boxes[b].Half : Boxes[b].Half + FVector(0.0, 0.0, 20.0);
					const FVector Center = bRoof ? Boxes[b].Center : Boxes[b].Center - FVector(0.0, 0.0, 20.0);
					TNFormBox(M, Center, Half, 0.0, bRoof ? C.Concrete * 1.08f : C.Concrete);
				}
				TNFormBox(M, FVector(Dims.Interior.X + Dims.Wall + 1.0, 0.0, Dims.Interior.Z * 0.62), FVector(3.0, Dims.Interior.Y * 0.5, 14.0), 0.0, C.Dark);
				TNFormBox(M, FVector(0.0, 0.0, 1.0), FVector(Dims.Interior.X, Dims.Interior.Y, 2.0), 0.0, C.Concrete * 0.8f);
				for (int32 k = 0; k < 3; ++k)
				{
					TNFloraBlob(M, FVector(Rand(10 + k, -R * 0.5, R * 0.5), Rand(20 + k, -R * 0.4, R * 0.4), H + 50.0), Rand(30 + k, 60.0, 110.0), 16.0, Seed + k, (k % 2) ? C.Metal : FLinearColor(0.3f, 0.25f, 0.14f), 6);
				}
				break;
			}
			case EFormation::WatchTower:
			{
				// Torre de vigía: cuatro patas que se cierran, arriostrada, plataforma con barandilla,
				// tejado a cuatro aguas y escalera.
				const double H = P.Height;
				const double Plat = H * 0.72;
				const FLinearColor Wood = C.Wood;
				for (int32 Cx = -1; Cx <= 1; Cx += 2)
				{
					for (int32 Cy = -1; Cy <= 1; Cy += 2)
					{
						M.AddBeam(FVector(Cx * 130.0, Cy * 130.0, -40.0), FVector(Cx * 85.0, Cy * 85.0, Plat), 9.0, Wood);
						M.AddBeam(FVector(Cx * 85.0, Cy * 85.0, Plat), FVector(Cx * 85.0, Cy * 85.0, H - 60.0), 6.0, Wood * 0.9f);
					}
				}
				for (int32 Face = 0; Face < 4; ++Face)
				{
					const double A = PI * 0.5 * Face;
					const FVector Dx(FMath::Cos(A), FMath::Sin(A), 0.0);
					const FVector Dy(-FMath::Sin(A), FMath::Cos(A), 0.0);
					for (int32 Lv = 0; Lv < 2; ++Lv)
					{
						const double Z0 = Plat * (0.1 + 0.45 * Lv), Z1 = Plat * (0.45 + 0.45 * Lv);
						const double S0 = FMath::Lerp(130.0, 85.0, Z0 / Plat), S1 = FMath::Lerp(130.0, 85.0, Z1 / Plat);
						M.AddBeam(Dx * S0 + Dy * S0 + FVector(0.0, 0.0, Z0), Dx * S1 - Dy * S1 + FVector(0.0, 0.0, Z1), 4.5, Wood * 0.85f);
					}
					M.AddBeam(Dx * 92.0 + Dy * 92.0 + FVector(0.0, 0.0, Plat + 95.0), Dx * 92.0 - Dy * 92.0 + FVector(0.0, 0.0, Plat + 95.0), 4.0, Wood * 0.95f);
				}
				TNFormBox(M, FVector(0.0, 0.0, Plat), FVector(105.0, 105.0, 8.0), 0.0, Wood * 1.1f);
				const FVector Apex(0.0, 0.0, H + 40.0);
				const FVector Eave[4] = { FVector(125.0, 125.0, H - 60.0), FVector(-125.0, 125.0, H - 60.0), FVector(-125.0, -125.0, H - 60.0), FVector(125.0, -125.0, H - 60.0) };
				for (int32 k = 0; k < 4; ++k)
				{
					M.AddTri(Apex, Eave[k], Eave[(k + 1) % 4], (Eave[k] + Eave[(k + 1) % 4]) * 0.5, C.Wood * 0.75f);
					M.AddTri(Apex, Eave[(k + 1) % 4], Eave[k], FVector(0.0, 0.0, -1.0), C.Wood * 0.5f);
				}
				for (int32 Side = -1; Side <= 1; Side += 2) { M.AddBeam(FVector(170.0, Side * 30.0, -30.0), FVector(100.0, Side * 30.0, Plat), 3.5, Wood); }
				for (int32 Rung = 1; Rung < 12; ++Rung)
				{
					const double U = Rung / 12.0;
					const FVector P0 = FMath::Lerp(FVector(170.0, -30.0, -30.0), FVector(100.0, -30.0, Plat), U);
					M.AddBeam(P0, P0 + FVector(0.0, 60.0, 0.0), 2.5, Wood * 0.9f);
				}
				break;
			}
			case EFormation::TankWreck:
			{
				// Carro de combate abandonado: casco con glacis, orugas con ruedas, torreta y cañón caído;
				// algo ladeado y hundido.
				const double Tilt = FMath::DegreesToRadians(Rand(1, -7.0, 7.0));
				auto Rot = [Tilt](const FVector& V) { return FVector(V.X, V.Y * FMath::Cos(Tilt) - V.Z * FMath::Sin(Tilt), V.Y * FMath::Sin(Tilt) + V.Z * FMath::Cos(Tilt)); };
				FTNProcMeshBuffers Tank;
				const FLinearColor Olive = C.Metal;
				const FLinearColor Rust(0.36f, 0.18f, 0.07f);
				TNFormBox(Tank, FVector(-10.0, 0.0, 95.0), FVector(250.0, 120.0, 45.0), 0.0, Olive);
				Tank.AddQuad(FVector(240.0, -120.0, 140.0), FVector(240.0, 120.0, 140.0), FVector(320.0, 120.0, 75.0), FVector(320.0, -120.0, 75.0), FVector(1.0, 0.0, 1.0), Olive * 1.05f);
				for (int32 Side = -1; Side <= 1; Side += 2)
				{
					TNFormBox(Tank, FVector(20.0, Side * 150.0, 50.0), FVector(290.0, 38.0, 45.0), 0.0, FLinearColor(0.1f, 0.1f, 0.09f));
					for (int32 w = 0; w < 5; ++w)
					{
						const double X = -200.0 + 110.0 * w;
						TNFormCylinder(Tank, FVector(X, Side * 150.0, 40.0), FVector(X, Side * 192.0, 40.0), 33.0, 33.0, 8, (w % 2) ? Rust : FLinearColor(0.16f, 0.16f, 0.14f));
					}
				}
				TArray<FVector2D> Turret;
				for (int32 k = 0; k < 8; ++k)
				{
					const double A = TNProcMap::TwoPi * (k + 0.5) / 8.0;
					Turret.Add(FVector2D(-40.0 + FMath::Cos(A) * 115.0, FMath::Sin(A) * 95.0));
				}
				Tank.AddPrism(Turret, 215.0, 140.0, Olive * 1.1f);
				TNFormCylinder(Tank, FVector(60.0, 0.0, 180.0), FVector(330.0, 0.0, 150.0), 13.0, 10.0, 8, Olive * 0.9f);
				TNFloraBlob(Tank, FVector(-120.0, 60.0, 142.0), 70.0, 10.0, Seed + 1u, Rust, 6);
				for (int32 i = 0; i < Tank.Verts.Num(); ++i)
				{
					Tank.Verts[i] = Rot(Tank.Verts[i]) - FVector(0.0, 0.0, 22.0);
					Tank.Normals[i] = Rot(Tank.Normals[i]);
				}
				TNFormAppend(M, Tank, FVector::ZeroVector, FVector2D(1.0, 0.0));
				break;
			}
			case EFormation::GiantShell:
			{
				// Caracola gigante de pie sobre su boca: cuerpo en espiral que se afila hacia la punta, costilla
				// en hélice con púas y el labio rosado abierto hacia el camino (+X).
				const double R = P.Radius, H = P.Height;
				const FLinearColor Shell(0.93f, 0.84f, 0.7f), Band(0.86f, 0.55f, 0.35f), Lip(0.98f, 0.66f, 0.62f);
				const double Zs[8] = { 0.0, 0.12, 0.3, 0.48, 0.64, 0.78, 0.9, 1.0 };
				const double Rs[8] = { 0.85, 1.0, 0.95, 0.78, 0.56, 0.36, 0.18, 0.05 };
				TNFloraLathe(M, FVector(0.0, 0.0, -30.0), { 0.0, H * 0.12, H * 0.3, H * 0.48, H * 0.64, H * 0.78, H * 0.9, H },
					{ R * 0.85, R, R * 0.95, R * 0.78, R * 0.56, R * 0.36, R * 0.18, R * 0.05 }, 0.04, Seed, Shell, 12, false, 0.93);
				// Radio del cuerpo a la altura Z (el perfil del torno), para que la costilla vaya por encima.
				auto BodyR = [&](double Z)
				{
					const double T = FMath::Clamp((Z + 30.0) / H, 0.0, 1.0);
					int32 i = 0;
					while (i < 6 && Zs[i + 1] < T) { ++i; }
					return R * FMath::Lerp(Rs[i], Rs[i + 1], (T - Zs[i]) / FMath::Max(1e-3, Zs[i + 1] - Zs[i]));
				};
				TArray<FVector> Hx;
				TArray<double> Hr;
				for (int32 t = 0; t <= 40; ++t)
				{
					const double U = t / 40.0;
					const double Z = H * (0.1 + 0.85 * U);
					const double Rr = BodyR(Z) * 1.04 + 8.0;
					const double A = U * TNProcMap::TwoPi * 3.2;
					Hx.Add(FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, Z));
					Hr.Add(FMath::Lerp(R * 0.09, R * 0.02, U));
					if (t % 3 == 0 && U < 0.85)
					{
						const FVector Tip = FVector(FMath::Cos(A), FMath::Sin(A), 0.25) * (Rr + R * 0.25 * (1.0 - U)) + FVector(0.0, 0.0, Z);
						TNFormCylinder(M, Hx.Last(), Tip, R * 0.07 * (1.0 - U) + 4.0, 1.0, 5, Band);
					}
				}
				TNFormTube(M, Hx, Hr, 5, Band);
				// Labio: ala de dos caras que se abre hacia el camino, más alta en el centro y con el borde enrollado.
				const FLinearColor LipIn(0.9f, 0.45f, 0.45f);
				TArray<FVector> Rim;
				TArray<double> RimR;
				for (int32 k = 0; k <= 10; ++k)
				{
					const double A = -1.1 + 2.2 * k / 10.0;
					const double E = 1.0 - FMath::Square(A / 1.1) * 0.55;
					Rim.Add(FVector(FMath::Cos(A) * R * 1.3, FMath::Sin(A) * R * 1.3, H * 0.36 * E));
					RimR.Add(R * 0.045 * E + 3.0);
				}
				for (int32 k = 0; k < 10; ++k)
				{
					const double A0 = -1.1 + 2.2 * k / 10.0, A1 = -1.1 + 2.2 * (k + 1) / 10.0;
					const FVector P0(FMath::Cos(A0) * R * 0.98, FMath::Sin(A0) * R * 0.98, -5.0), P1(FMath::Cos(A1) * R * 0.98, FMath::Sin(A1) * R * 0.98, -5.0);
					const FVector Out(P0.X + P1.X, P0.Y + P1.Y, 0.0);
					M.AddQuad(P0, P1, Rim[k + 1], Rim[k], Out, Lip);
					M.AddQuad(P0, P1, Rim[k + 1], Rim[k], -Out, LipIn);
				}
				TNFormTube(M, Rim, RimR, 5, Lip);
				break;
			}
			case EFormation::Anchor:
			{
				// Ancla oxidada clavada en la arena, algo inclinada: caña, cepo, arganeo, brazos con uñas medio
				// enterrados y la cadena que baja en curva hasta el suelo y sigue por la arena.
				const double H = P.Height;
				const FLinearColor Rust(0.36f, 0.19f, 0.11f), RustDark(0.24f, 0.13f, 0.08f);
				const FVector Base(0.0, 0.0, -40.0);
				const FVector TopP(H * Rand(1, 0.12, 0.25), 0.0, H);
				TNFormCylinder(M, Base, TopP, 34.0, 28.0, 8, Rust);
				const FVector Stock = FMath::Lerp(Base, TopP, 0.86);
				TNFormCylinder(M, Stock - FVector(0.0, H * 0.32, 0.0), Stock + FVector(0.0, H * 0.32, 0.0), 22.0, 22.0, 8, RustDark);
				for (const double S : { -1.0, 1.0 }) { TNFloraBlob(M, Stock + FVector(0.0, S * H * 0.32, 0.0), 30.0, 30.0, Seed + 2u, RustDark, 6); }
				TArray<FVector> Ring;
				TArray<double> RingR;
				for (int32 k = 0; k <= 12; ++k)
				{
					const double A = TNProcMap::TwoPi * k / 12.0;
					Ring.Add(TopP + FVector(FMath::Cos(A) * 55.0, 0.0, 55.0 + FMath::Sin(A) * 55.0));
					RingR.Add(9.0);
				}
				TNFormTube(M, Ring, RingR, 5, RustDark);
				for (const double S : { -1.0, 1.0 })
				{
					TArray<FVector> Arm;
					TArray<double> ArmR;
					for (int32 t = 0; t <= 6; ++t)
					{
						const double A = PI * 0.5 * t / 6.0;
						Arm.Add(Base + FVector(0.0, S * FMath::Sin(A) * H * 0.3, 60.0 + (1.0 - FMath::Cos(A)) * H * 0.25));
						ArmR.Add(FMath::Lerp(30.0, 20.0, t / 6.0));
					}
					TNFormTube(M, Arm, ArmR, 6, Rust);
					TNFormBox(M, Arm.Last() + FVector(0.0, S * 30.0, 30.0), FVector(20.0, 70.0, 60.0), 0.0, RustDark);
				}
				const FVector C0 = TopP + FVector(0.0, 0.0, 55.0);
				FVector PrevL = C0;
				for (int32 k = 1; k <= 26; ++k)
				{
					const double U = k / 26.0;
					const FVector Lk(C0.X - U * H * 0.9, U * H * 0.25, FMath::Max(15.0, C0.Z * (1.0 - U) * (1.0 - U)));
					M.AddBeam(PrevL, Lk, 7.0, (k % 2) ? Rust : RustDark);
					PrevL = Lk;
				}
				break;
			}
			case EFormation::StoneCircle:
			{
				// Círculo de piedras en pie con huecos para pasar entre ellas, dinteles sobre algunos pares,
				// alguna tumbada y un altar bajo en el centro.
				const double R = P.Radius, H = P.Height;
				const int32 N = FMath::Clamp(FMath::RoundToInt(TNProcMap::TwoPi * R / 330.0), 8, 16);
				TArray<FVector> Tops;
				TArray<uint8> Up;
				for (int32 k = 0; k < N; ++k)
				{
					const double A = TNProcMap::TwoPi * (k + 0.5) / N;
					const FVector Pk(FMath::Cos(A) * R, FMath::Sin(A) * R, 0.0);
					const double Hk = H * Rand(10 + k, 0.8, 1.05);
					const double G = Ground(Pk.X, Pk.Y) - 40.0;
					if (Rand(20 + k, 0.0, 1.0) < 0.12)
					{
						TNFormBox(M, Pk + FVector(0.0, 0.0, G + 80.0), FVector(Hk * 0.5, 75.0, 40.0), A + Rand(30 + k, -0.4, 0.4), C.Strata * 0.9f);
						Tops.Add(Pk);
						Up.Add(0);
						continue;
					}
					TNFormBox(M, Pk + FVector(0.0, 0.0, G + Hk * 0.5 + 20.0), FVector(45.0, 75.0, Hk * 0.5 + 20.0), A, (k % 2) ? C.Rock : C.Strata);
					TNFloraBlob(M, Pk + FVector(0.0, 0.0, G + Hk + 40.0), 55.0, 12.0, Seed + 50u + k, C.Moss, 6);
					Tops.Add(Pk + FVector(0.0, 0.0, G + Hk + 40.0));
					Up.Add(1);
				}
				for (int32 k = 0; k + 1 < N; k += 2)
				{
					if (!Up[k] || !Up[k + 1] || Rand(40 + k, 0.0, 1.0) < 0.35) { continue; }
					const FVector Mid = (Tops[k] + Tops[k + 1]) * 0.5 + FVector(0.0, 0.0, 30.0);
					const FVector Along = (Tops[k + 1] - Tops[k]).GetSafeNormal2D();
					M.AddBox(Mid, Along, FVector(FVector::Dist2D(Tops[k], Tops[k + 1]) * 0.5 + 60.0, 70.0, 30.0), C.Strata * 1.05f);
				}
				TNFormBox(M, FVector(0.0, 0.0, Ground(0.0, 0.0) + 35.0), FVector(130.0, 80.0, 35.0), Rand(60, 0.0, 3.0), C.Rock * 0.9f);
				break;
			}
			case EFormation::Obelisk:
			{
				// Obelisco: basa escalonada, fuste de base cuadrada que se afila, bandas talladas y punta dorada.
				const double R = P.Radius, H = P.Height;
				const FLinearColor Stone = C.Strata, Gold(1.f, 0.78f, 0.22f);
				TNFormBox(M, FVector(0.0, 0.0, 30.0), FVector(R * 1.9, R * 1.9, 40.0), 0.0, Stone * 0.85f);
				TNFormBox(M, FVector(0.0, 0.0, 100.0), FVector(R * 1.5, R * 1.5, 30.0), 0.0, Stone * 0.92f);
				const double Z0 = 130.0, Z1 = H * 0.9;
				TNFormCylinder(M, FVector(0.0, 0.0, Z0), FVector(0.0, 0.0, Z1), R * 1.414, R * 0.95, 4, Stone);
				TNFormCylinder(M, FVector(0.0, 0.0, Z1), FVector(0.0, 0.0, H), R * 0.95, 1.0, 4, Gold);
				for (int32 b = 0; b < 5; ++b)
				{
					const double Z = FMath::Lerp(Z0 + 120.0, Z1 - 120.0, b / 4.0);
					const double Rz = FMath::Lerp(R * 1.414, R * 0.95, (Z - Z0) / (Z1 - Z0));
					TNFormCylinder(M, FVector(0.0, 0.0, Z - 7.0), FVector(0.0, 0.0, Z + 7.0), Rz + 4.0, Rz + 4.0, 4, Stone * 0.68f);
				}
				break;
			}
			case EFormation::FossilSkull:
			{
				// Cráneo fósil gigante medio enterrado mirando al camino (+X): bóveda, cuencas de los ojos, hocico
				// largo con dientes y dos cuernos curvos.
				const double R = P.Radius, H = P.Height;
				TNFloraBlob(M, FVector(-R * 0.3, 0.0, H * 0.3), R * 0.75, H * 0.55, Seed, C.Bone, 9);
				TNFormTube(M, { FVector(-R * 0.1, 0.0, H * 0.35), FVector(R * 0.5, 0.0, H * 0.28), FVector(R * 1.0, 0.0, H * 0.18) }, { R * 0.42, R * 0.32, R * 0.2 }, 8, C.Bone * 0.97f);
				for (const double S : { -1.0, 1.0 })
				{
					TNFloraBlob(M, FVector(R * 0.05, S * R * 0.38, H * 0.55), R * 0.16, R * 0.13, Seed + 2u, C.Dark, 6);
					TNFormTube(M, { FVector(-R * 0.35, S * R * 0.45, H * 0.7), FVector(-R * 0.2, S * R * 0.95, H * 0.95), FVector(R * 0.15, S * R * 1.15, H * 1.25) },
						{ R * 0.14, R * 0.1, R * 0.03 }, 6, C.Bone * 1.03f);
				}
				for (int32 k = 0; k < 7; ++k)
				{
					const double X = R * (0.2 + 0.12 * k);
					for (const double S : { -1.0, 1.0 })
					{
						const FVector Root(X, S * R * (0.28 - 0.02 * k), H * (0.22 - 0.02 * k));
						TNFormCylinder(M, Root, Root - FVector(0.0, 0.0, R * 0.14), R * 0.035, 1.0, 5, C.Bone * 1.08f);
					}
				}
				break;
			}
			case EFormation::ObsidianSpires:
			{
				// Agujas de obsidiana: esquirlas negras de arista violácea que salen en haz de una base de escoria.
				const double R = P.Radius, H = P.Height;
				const FLinearColor Obs(0.05f, 0.045f, 0.06f), Edge(0.2f, 0.17f, 0.24f);
				TNFloraBlob(M, FVector(0.0, 0.0, 20.0), R * 1.1, R * 0.35, Seed, C.Rock * 0.8f, 8);
				const int32 N = 7 + static_cast<int32>(Seed % 4u);
				for (int32 k = 0; k < N; ++k)
				{
					const double A = TNProcMap::TwoPi * k / N + Rand(10 + k, -0.3, 0.3);
					const double Lean = k == 0 ? 0.05 : Rand(20 + k, 0.12, 0.4);
					const double Hk = H * (k == 0 ? 1.0 : Rand(30 + k, 0.35, 0.8));
					const double Off = k == 0 ? 0.0 : R * 0.45;
					const FVector B0(FMath::Cos(A) * Off, FMath::Sin(A) * Off, -20.0);
					const FVector Dir = FVector(FMath::Cos(A) * Lean, FMath::Sin(A) * Lean, 1.0).GetSafeNormal();
					TNFormCylinder(M, B0, B0 + Dir * Hk, Hk * Rand(40 + k, 0.1, 0.16), 2.0, 5, (k % 3 == 0) ? Edge : Obs);
				}
				break;
			}
			case EFormation::ColossalTurtle:
			{
				// Tortuga colosal de piedra (la de las cuevas a escala de explanada), mirando a lo largo del
				// camino; con musgo o corona según la semilla.
				FTNProcMeshBuffers Stat, Eyes;
				TNTurtleStatue(Stat, Eyes, Seed, C.Strata, C.Rock * 0.85f, FLinearColor(0.15f, 0.4f, 0.8f), (Seed % 3u) == 0u ? 1 : 0, (Seed % 2u) == 0u, false);
				const double Scale = P.Radius / 160.0;
				TNPropMesh::TNPropAppend(M, Stat, FVector(0.0, 0.0, -30.0), 0.0, Scale);
				TNPropMesh::TNPropAppend(M, Eyes, FVector(0.0, 0.0, -30.0), 0.0, Scale);
				break;
			}
			case EFormation::WaterTower:
			{
				// Depósito de agua de madera sobre cuatro patas con riostras en aspa, escalera y tejado cónico.
				const double R = P.Radius, H = P.Height;
				const double TankZ = H * 0.62, TankH = H * 0.26, TankR = R * 0.75;
				auto LegAt = [&](double A, double U)
				{
					return FMath::Lerp(FVector(FMath::Cos(A) * R * 0.85, FMath::Sin(A) * R * 0.85, 0.0), FVector(FMath::Cos(A) * TankR * 0.8, FMath::Sin(A) * TankR * 0.8, TankZ), U);
				};
				for (int32 k = 0; k < 4; ++k)
				{
					const double A = PI * 0.25 + HALF_PI * k;
					const FVector Foot = LegAt(A, 0.0);
					TNFormCylinder(M, FVector(Foot.X, Foot.Y, Ground(Foot.X, Foot.Y) - 30.0), LegAt(A, 1.0), 18.0, 14.0, 6, C.Wood * 0.8f);
					const double A1 = A + HALF_PI;
					for (const double U0 : { 0.15, 0.5 })
					{
						M.AddBeam(LegAt(A, U0), LegAt(A1, U0 + 0.3), 5.0, C.Wood * 0.7f);
						M.AddBeam(LegAt(A1, U0), LegAt(A, U0 + 0.3), 5.0, C.Wood * 0.7f);
					}
				}
				TNFormCylinder(M, FVector(0.0, 0.0, TankZ - 20.0), FVector(0.0, 0.0, TankZ), TankR + 30.0, TankR + 30.0, 14, C.Wood * 0.7f);
				TNFloraLathe(M, FVector(0.0, 0.0, TankZ), { 0.0, TankH }, { TankR, TankR }, 0.0, Seed, C.Wood, 16, false, 0.96);
				for (const double Fh : { 0.25, 0.75 })
				{
					TNFormCylinder(M, FVector(0.0, 0.0, TankZ + TankH * Fh - 6.0), FVector(0.0, 0.0, TankZ + TankH * Fh + 6.0), TankR + 4.0, TankR + 4.0, 16, C.Metal);
				}
				TNFormCylinder(M, FVector(0.0, 0.0, TankZ + TankH), FVector(0.0, 0.0, H), TankR + 25.0, 8.0, 16, C.Paint * 0.8f);
				const FVector L0(R * 0.95, 0.0, 0.0), L1(TankR + 35.0, 0.0, TankZ);
				for (const double S : { -1.0, 1.0 }) { M.AddBeam(L0 + FVector(0.0, S * 25.0, 0.0), L1 + FVector(0.0, S * 25.0, 0.0), 3.0, C.Wood * 0.6f); }
				for (int32 r = 1; r < 18; ++r)
				{
					const FVector Q = FMath::Lerp(L0, L1, r / 18.0);
					M.AddBeam(Q - FVector(0.0, 25.0, 0.0), Q + FVector(0.0, 25.0, 0.0), 2.5, C.Wood * 0.6f);
				}
				break;
			}
			case EFormation::Pyramid:
			{
				// Pirámide escalonada con escalinata al frente (+X), templete arriba y musgo en las cornisas.
				const double R = P.Radius;
				const double H = P.Height;
				constexpr int32 Levels = 6;
				const double Hl = H / Levels;
				const double Base = FMath::Min(FMath::Min(Ground(R, R), Ground(-R, R)), FMath::Min(Ground(R, -R), Ground(-R, -R))) - 80.0;
				for (int32 Lv = 0; Lv < Levels; ++Lv)
				{
					const double S = R * (1.0 - static_cast<double>(Lv) / (Levels + 0.8));
					const double Z0 = Lv == 0 ? Base : Lv * Hl;
					TNFormBox(M, FVector(0.0, 0.0, (Z0 + (Lv + 1) * Hl) * 0.5), FVector(S, S, ((Lv + 1) * Hl - Z0) * 0.5), 0.0, (Lv % 2) ? C.Rock : C.Strata);
					if (Lv % 2 == 0) { TNFloraBlob(M, FVector(Rand(10 + Lv, -S, S) * 0.6, -S * 0.9, (Lv + 1) * Hl + 5.0), S * 0.25, 20.0, Seed + Lv, C.Moss, 6); }
				}
				const double StairW = R * 0.22;
				const int32 Steps = 18;
				for (int32 s = 0; s < Steps; ++s)
				{
					const double U = static_cast<double>(s) / Steps;
					const double X = FMath::Lerp(R * 1.05, R * 0.2, U);
					const double Z = FMath::Lerp(Base, H, U);
					TNFormBox(M, FVector(X, 0.0, (Base + Z) * 0.5 + H / Steps * 0.5), FVector(R * 0.06, StairW, ((Z - Base) + H / Steps) * 0.5), 0.0, C.Strata * 0.95f);
				}
				TNFormBox(M, FVector(0.0, 0.0, H + Hl * 0.9), FVector(R * 0.2, R * 0.2, Hl * 0.9), 0.0, C.Rock * 0.95f);
				TNFormBox(M, FVector(R * 0.2, 0.0, H + Hl * 0.6), FVector(4.0, R * 0.07, Hl * 0.55), 0.0, C.Dark);
				break;
			}
			case EFormation::Lighthouse:
			{
				// Faro a rayas rojas y blancas sobre un zócalo de roca, con galería, linterna y cúpula.
				const double R = P.Radius;
				const double H = P.Height;
				const double Base = Ground(0.0, 0.0) - 60.0;
				for (int32 k = 0; k < 5; ++k)
				{
					const double A = TNProcMap::TwoPi * k / 5.0;
					TNProcAddBoulder(M, FVector(FMath::Cos(A) * R * 1.4, FMath::Sin(A) * R * 1.4, Ground(FMath::Cos(A) * R * 1.4, FMath::Sin(A) * R * 1.4)), R * 0.8, R * 0.7, Seed + k, C.Rock);
				}
				constexpr int32 Bands = 8;
				const double Tower = H * 0.82;
				for (int32 b = 0; b < Bands; ++b)
				{
					const double Z0 = FMath::Lerp(Base, Tower, static_cast<double>(b) / Bands);
					const double Z1 = FMath::Lerp(Base, Tower, static_cast<double>(b + 1) / Bands);
					const double R0 = FMath::Lerp(R, R * 0.66, static_cast<double>(b) / Bands);
					const double R1 = FMath::Lerp(R, R * 0.66, static_cast<double>(b + 1) / Bands);
					TNFloraLathe(M, FVector(0.0, 0.0, Z0), { 0.0, Z1 - Z0 }, { R0, R1 }, 0.0, Seed + b, (b % 2) ? C.White : C.Paint, 12, false);
				}
				TArray<FVector2D> Gallery, Lamp;
				for (int32 k = 0; k < 12; ++k)
				{
					const double A = TNProcMap::TwoPi * k / 12.0;
					Gallery.Add(FVector2D(FMath::Cos(A) * R * 0.95, FMath::Sin(A) * R * 0.95));
					Lamp.Add(FVector2D(FMath::Cos(A) * R * 0.45, FMath::Sin(A) * R * 0.45));
				}
				M.AddPrism(Gallery, Tower + 25.0, Tower, C.Dark * 3.0f);
				M.AddPrism(Lamp, H * 0.93, Tower + 25.0, FLinearColor(1.f, 0.9f, 0.45f));
				TNFloraLathe(M, FVector(0.0, 0.0, H * 0.93), { 0.0, H * 0.05 }, { R * 0.52, R * 0.12 }, 0.0, Seed, C.Paint, 12, false);
				break;
			}
			case EFormation::Mesa:
			{
				// Mesa: paredes casi verticales con estratos, techo plano y derrubios al pie.
				const double R = P.Radius;
				const double H = P.Height;
				const double Base = FMath::Min(Ground(0.0, 0.0), FMath::Min(Ground(R, 0.0), Ground(-R, 0.0))) - 300.0;
				constexpr int32 Layers = 5;
				for (int32 l = 0; l < Layers; ++l)
				{
					const double Z0 = FMath::Lerp(Base, H, static_cast<double>(l) / Layers);
					const double Z1 = FMath::Lerp(Base, H, static_cast<double>(l + 1) / Layers);
					const double R0 = R * FMath::Lerp(1.12, 0.9, static_cast<double>(l) / Layers);
					const double R1 = R * FMath::Lerp(1.12, 0.9, static_cast<double>(l + 1) / Layers);
					TNFloraLathe(M, FVector(0.0, 0.0, Z0), { 0.0, Z1 - Z0 }, { R0, R1 }, 0.07, Seed + 3u, (l % 2) ? C.Rock : C.Strata, 16, false);
				}
				for (int32 k = 0; k < 7; ++k)
				{
					const double A = TNProcMap::TwoPi * (k + 0.4) / 7.0;
					const FVector2D D(FMath::Cos(A) * R * 1.2, FMath::Sin(A) * R * 1.2);
					TNProcAddBoulder(M, FVector(D.X, D.Y, Ground(D.X, D.Y)), R * Rand(20 + k, 0.12, 0.22), R * 0.12, Seed + 30u + k, C.Rock * 0.9f);
				}
				break;
			}
			case EFormation::SeaStack:
			{
				// Farallón irregular que sale del mar, con una mata de hierba arriba.
				const double R = P.Radius;
				const double H = P.Height;
				// Asoma del agua un 55 % de su alto; la base, hundida en el fondo.
				const double Base = Ground(0.0, 0.0) - 200.0;
				const double Top = FMath::Max(Base + 400.0, FMath::Max(P.WaterZ, Ground(0.0, 0.0)) + H * 0.55);
				TNFloraLathe(M, FVector(0.0, 0.0, Base), { 0.0, (Top - Base) * 0.3, (Top - Base) * 0.7, Top - Base }, { R * 1.15, R, R * 0.85, R * 0.6 }, 0.22, Seed, C.Rock, 10, false);
				TNFloraBlob(M, FVector(0.0, 0.0, Top + 8.0), R * 0.55, 25.0, Seed + 1u, FLinearColor(0.25f, 0.42f, 0.14f), 7);
				break;
			}
			case EFormation::CastleRuin:
			{
				// Castillo en ruinas: lienzos de muralla almenados con brechas, torre redonda desmochada
				// y cascotes.
				const double R = P.Radius;
				const double H = P.Height;
				const double S = R * 0.6;
				const double WallH = H * 0.45;
				for (int32 Side = 0; Side < 4; ++Side)
				{
					const double A = PI * 0.5 * Side;
					const FVector Dx(FMath::Cos(A), FMath::Sin(A), 0.0);
					const FVector Dy(-FMath::Sin(A), FMath::Cos(A), 0.0);
					const int32 Segs = 5;
					for (int32 g = 0; g < Segs; ++g)
					{
						if ((g + Side + static_cast<int32>(Seed % 5u)) % 4 == 0) { continue; }
						const double T = (g + 0.5) / Segs - 0.5;
						const FVector Ctr = Dx * S + Dy * (T * 2.0 * S);
						const double Hh = WallH * Rand(10 + g + Side * 7, 0.55, 1.0);
						const double G = Ground(Ctr.X, Ctr.Y) - 60.0;
						TNFormBox(M, FVector(Ctr.X, Ctr.Y, (G + Hh) * 0.5), FVector(40.0, S / Segs, (Hh - G) * 0.5), A, (g % 2) ? C.Rock : C.Strata);
						if (Hh > WallH * 0.8)
						{
							TNFormBox(M, FVector(Ctr.X, Ctr.Y, Hh + 40.0), FVector(44.0, S / Segs * 0.4, 40.0), A, C.Rock * 0.95f);
						}
					}
				}
				const FVector2D Tw(S, S);
				const double Gt = Ground(Tw.X, Tw.Y) - 80.0;
				TNFloraLathe(M, FVector(Tw.X, Tw.Y, Gt), { 0.0, (H - Gt) * 0.85, H - Gt }, { R * 0.2, R * 0.19, R * 0.17 }, 0.04, Seed + 1u, C.Strata, 12, false);
				for (int32 k = 0; k < 6; ++k)
				{
					if (k == 2) { continue; }
					const double A = TNProcMap::TwoPi * k / 6.0;
					TNFormBox(M, FVector(Tw.X + FMath::Cos(A) * R * 0.17, Tw.Y + FMath::Sin(A) * R * 0.17, H + 30.0), FVector(28.0, 28.0, 30.0), A, C.Rock);
				}
				for (int32 k = 0; k < 5; ++k)
				{
					const FVector2D D(Rand(40 + k, -S, S), Rand(50 + k, -S, S));
					TNProcAddBoulder(M, FVector(D.X, D.Y, Ground(D.X, D.Y)), Rand(60 + k, 60.0, 120.0), 70.0, Seed + 60u + k, C.Strata * 0.9f);
				}
				break;
			}
			case EFormation::Windmill:
			{
				// Molino: torre encalada troncocónica, caperuza, cuatro aspas en X al frente y puerta.
				const double R = P.Radius;
				const double H = P.Height;
				const double G = Ground(0.0, 0.0) - 60.0;
				TNFloraLathe(M, FVector(0.0, 0.0, G), { 0.0, H * 0.78 - G }, { R, R * 0.72 }, 0.0, Seed, C.White, 12, false);
				TNFloraLathe(M, FVector(0.0, 0.0, H * 0.78), { 0.0, H * 0.14 }, { R * 0.8, R * 0.08 }, 0.0, Seed + 1u, C.Wood * 0.8f, 12, false);
				const FVector Hub(R * 0.85, 0.0, H * 0.74);
				TNFormCylinder(M, Hub - FVector(R * 0.25, 0.0, 0.0), Hub + FVector(15.0, 0.0, 0.0), 16.0, 16.0, 8, C.Wood);
				for (int32 k = 0; k < 4; ++k)
				{
					const double A = PI * 0.25 + PI * 0.5 * k;
					const FVector Dir(0.0, FMath::Cos(A), FMath::Sin(A));
					const FVector Side = FVector::CrossProduct(FVector(1.0, 0.0, 0.0), Dir);
					const double Len = H * 0.42;
					M.AddBeam(Hub + FVector(10.0, 0.0, 0.0), Hub + FVector(10.0, 0.0, 0.0) + Dir * Len, 5.0, C.Wood);
					const FVector A0 = Hub + FVector(14.0, 0.0, 0.0) + Dir * (Len * 0.2);
					const FVector A1 = Hub + FVector(14.0, 0.0, 0.0) + Dir * Len;
					M.AddQuad(A0, A1, A1 + Side * 55.0, A0 + Side * 55.0, FVector(1.0, 0.0, 0.0), C.Cloth);
					M.AddQuad(A0, A0 + Side * 55.0, A1 + Side * 55.0, A1, FVector(-1.0, 0.0, 0.0), C.Cloth * 0.8f);
				}
				TNFormBox(M, FVector(R * 0.98, 0.0, G + 110.0), FVector(6.0, 45.0, 90.0), 0.0, C.Dark * 3.0f);
				break;
			}
			case EFormation::StiltHut:
			{
				// Palafito: pilotes, plataforma, choza con tejado de paja a dos aguas, escalera y canoa.
				const double G = Ground(0.0, 0.0);
				const double Surface = FMath::Max(G, P.WaterZ);
				const double Deck = Surface + 260.0;
				for (int32 Px = -1; Px <= 1; ++Px)
				{
					for (int32 Py = -1; Py <= 1; Py += 2)
					{
						M.AddBeam(FVector(Px * 170.0, Py * 140.0, G - 150.0), FVector(Px * 170.0, Py * 140.0, Deck), 10.0, C.Wood * 0.8f);
					}
				}
				TNFormBox(M, FVector(0.0, 0.0, Deck + 10.0), FVector(220.0, 175.0, 10.0), 0.0, C.Wood * 1.1f);
				TNFormBox(M, FVector(-20.0, 0.0, Deck + 115.0), FVector(150.0, 120.0, 95.0), 0.0, C.Wood);
				TNFormBox(M, FVector(131.0, 0.0, Deck + 85.0), FVector(2.0, 35.0, 65.0), 0.0, C.Dark * 3.0f);
				const FLinearColor Thatch(0.62f, 0.52f, 0.3f);
				const double Rz = Deck + 210.0, Ez = Deck + 150.0;
				const FVector R0(-195.0, 0.0, Rz + 70.0), R1(155.0, 0.0, Rz + 70.0);
				for (int32 Side = -1; Side <= 1; Side += 2)
				{
					const FVector E0(-195.0, Side * 165.0, Ez), E1(155.0, Side * 165.0, Ez);
					M.AddQuad(R0, R1, E1, E0, FVector(0.0, Side * 1.0, 1.0), Thatch);
					M.AddQuad(R0, E0, E1, R1, FVector(0.0, -Side * 1.0, -1.0), Thatch * 0.6f);
				}
				for (int32 End = -1; End <= 1; End += 2)
				{
					const double X = End < 0 ? -170.0 : 130.0;
					M.AddTri(FVector(X, -120.0, Deck + 210.0), FVector(X, 120.0, Deck + 210.0), FVector(X, 0.0, Rz + 60.0), FVector(End * 1.0, 0.0, 0.0), C.Wood * 0.9f);
				}
				for (int32 Side = -1; Side <= 1; Side += 2) { M.AddBeam(FVector(300.0, Side * 30.0, G - 20.0), FVector(220.0, Side * 30.0, Deck), 3.5, C.Wood); }
				for (int32 Rung = 1; Rung < 8; ++Rung)
				{
					const FVector P0 = FMath::Lerp(FVector(300.0, -30.0, G - 20.0), FVector(220.0, -30.0, Deck), Rung / 8.0);
					M.AddBeam(P0, P0 + FVector(0.0, 60.0, 0.0), 2.5, C.Wood * 0.9f);
				}
				TNFloraLathe(M, FVector(0.0, -300.0, Surface - 20.0), { 0.0, 35.0 }, { 45.0, 55.0 }, 0.0, Seed, C.Wood * 0.7f, 6, true);
				break;
			}
			default:
				break;
		}
	}
}
