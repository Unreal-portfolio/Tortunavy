#include "World/Beach/TN_BeachClamTrap.h"
#include "World/Beach/TN_BeachNearby.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachTrapSynthComponent.h"
#include "Core/TN_Log.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "TN_BeachRideKit.h"
#include "TN_BeachTrapKit.h"

/**
 * Geometría de la concha, en el espacio del marco (X = sentido de la carrera, origen en la arena, en el centro): una
 * elipse de semiejes A (X) y B (Y) con la charnela (umbo) en el borde (0, H·B), H = ±1. Las dos valvas se parametrizan
 * igual (radio normalizado Rho y ángulo Phi); los pliegues salen en abanico desde la charnela y hacen que los labios de
 * las dos valvas encajen en zigzag. La valva de arriba se construye cerrada y en el espacio de su charnela, que gira sobre
 * X para abrirla.
 */
namespace TNBeachClamDetail
{
	/** Margen (cm) del radio en el que se busca a quién pisa la valva: la cápsula y los pies por fuera del borde. */
	constexpr double GatherMargin = 150.0;
	using FBuffers = TNBeachTrapKit::FBuffers;
	using FHulls = TNBeachTrapKit::FHulls;

	constexpr int32 RimSegs = 44;
	/** Rendija entre los labios con la concha cerrada (por ahí sale el humo). */
	constexpr double LipGap = 5.0;
	constexpr double ShellThick = 16.0;
	constexpr double FoldAmp = 13.0;
	/** Subida de la valva en el aviso (grados). */
	constexpr double TwitchDeg = 7.0;
	/** Sacudidas de la presa pataleando. */
	constexpr double StruggleInterval = 0.62;
	constexpr double StruggleDecay = 0.13;
	/** Últimos segundos antes de abrirse: tiembla sin parar, cada vez más. */
	constexpr double BuildUpSeconds = 0.7;

	struct FClamDims
	{
		double A = 450.0;
		double B = 324.0;
		double RimZ = 38.0;
		double FloorZ = 10.0;
		double Dome = 162.0;
		double H = 1.0;
	};

	struct FClamPalette
	{
		FLinearColor Outer;
		FLinearColor OuterDark;
		FLinearColor Nacre;
		FLinearColor NacreDeep;
		FLinearColor Mantle;
		FLinearColor MantleSpot;
		FLinearColor MantleEdge;
		FLinearColor Siphon;
		FLinearColor Pearl;
	};

	/** Cuatro mantos de almeja gigante: azul eléctrico, verde esmeralda con oro, morado con azul y dorado con ojos azules. */
	FClamPalette MakePalette(int32 Variant)
	{
		static const uint32 OuterHex[4] = { 0xF1E6CF, 0xF6DCD2, 0xE4E1DA, 0xEFE0B8 };
		static const uint32 MantleHex[4] = { 0x1E6FD9, 0x1FA36B, 0x7B3FC4, 0xC98A2E };
		static const uint32 SpotHex[4] = { 0x7FE3FF, 0xFFD95A, 0x59D1F0, 0x3FB5E8 };
		static const uint32 EdgeHex[4] = { 0x0B3C8C, 0x0E6B45, 0x3E1C75, 0x7A4E14 };
		const int32 V = ((Variant % 4) + 4) % 4;
		FClamPalette Pal;
		Pal.Outer = TNPlaygroundKit::Rgb(OuterHex[V], 0.15f);
		Pal.OuterDark = TNPlaygroundKit::Shade(Pal.Outer, 0.72);
		Pal.Nacre = TNPlaygroundKit::Rgb(0xF6E9F2, 0.75f);
		Pal.NacreDeep = TNPlaygroundKit::Rgb(0xD9C8EE, 0.85f);
		Pal.Mantle = TNPlaygroundKit::Rgb(MantleHex[V], 0.45f);
		Pal.MantleSpot = TNPlaygroundKit::Rgb(SpotHex[V], 0.6f);
		Pal.MantleEdge = TNPlaygroundKit::Rgb(EdgeHex[V], 0.35f);
		Pal.Siphon = TNPlaygroundKit::Rgb(0x1C1030, 0.2f);
		Pal.Pearl = TNPlaygroundKit::Rgb(0xFFF4F6, 0.95f);
		return Pal;
	}

	FVector2D EllipseAt(const FClamDims& D, double Rho, double Phi)
	{
		return FVector2D(D.A * Rho * FMath::Cos(Phi), D.B * Rho * FMath::Sin(Phi));
	}

	/** Radio de la elipse (cm) en la dirección Phi: convierte distancias en Rho. */
	double RadiusAt(const FClamDims& D, double Phi)
	{
		return FMath::Max(1.0, FVector2D(D.A * FMath::Cos(Phi), D.B * FMath::Sin(Phi)).Size());
	}

	/** Pliegue en -1..1 (zigzag) según el ángulo visto desde la charnela; crece al alejarse de ella. */
	double FoldAt(const FClamDims& D, const FVector2D& P)
	{
		const double U = P.X / D.A;
		const double V = D.H * P.Y / D.B;
		const double Away = FMath::Max(1e-3, 1.0 - V);
		const double Psi = FMath::Atan2(U, Away);
		const double Zig = (2.0 / TNPlaygroundKit::KitPi) * FMath::Asin(FMath::Clamp(FMath::Cos(8.0 * Psi), -1.0, 1.0));
		return Zig * FMath::Clamp(0.5 * FMath::Sqrt(U * U + Away * Away), 0.0, 1.0);
	}

	/** Cara de fuera de la valva de arriba cerrada (Z en el marco). */
	double UpperOuterZ(const FClamDims& D, double Rho, double Fold)
	{
		const double R2 = FMath::Clamp(Rho * Rho, 0.0, 1.0);
		return D.RimZ + LipGap + FoldAmp * Fold * R2 + D.Dome * FMath::Pow(1.0 - R2, 0.7);
	}

	double LowerLipZ(const FClamDims& D, double Fold)
	{
		return D.RimZ + FoldAmp * Fold;
	}

	FLinearColor OuterColor(const FClamPalette& Pal, double Fold, int32 Ring)
	{
		FLinearColor Col = Fold > 0.35 ? TNPlaygroundKit::Mix(Pal.Outer, TNPlaygroundKit::Rgb(0xFFFFFF, 0.2f), 0.3)
			: (Fold < -0.35 ? TNPlaygroundKit::Shade(Pal.Outer, 0.8) : Pal.Outer);
		if (Ring % 2 == 1)
		{
			Col = TNPlaygroundKit::Shade(Col, 0.93);
		}
		return Col;
	}

	/**
	 * Valva de arriba cerrada, en el espacio de su charnela (Pivot = (0, H·B, RimZ)): cara de fuera con los pliegues y los
	 * anillos de crecimiento, nácar por dentro, filo del labio, y sus cascos de colisión (sectores por dos bandas).
	 */
	void BuildUpperValve(FBuffers& B, FHulls& Hulls, const FClamDims& D, const FClamPalette& Pal, uint32 Seed)
	{
		const FVector Pivot(0.0, D.H * D.B, D.RimZ);
		static const double Rings[8] = { 1.0, 0.95, 0.86, 0.73, 0.57, 0.39, 0.2, 0.0 };
		constexpr int32 NumRings = 8;
		TArray<FVector> Outer;
		TArray<FVector> Inner;
		TArray<double> Folds;
		Outer.SetNum(NumRings * RimSegs);
		Inner.SetNum(NumRings * RimSegs);
		Folds.SetNum(NumRings * RimSegs);
		for (int32 r = 0; r < NumRings; ++r)
		{
			for (int32 k = 0; k < RimSegs; ++k)
			{
				const double Phi = TNPlaygroundKit::KitTwoPi * k / RimSegs;
				const FVector2D P = EllipseAt(D, Rings[r], Phi);
				const double Fold = FoldAt(D, P);
				Folds[r * RimSegs + k] = Fold;
				Outer[r * RimSegs + k] = FVector(P.X, P.Y, UpperOuterZ(D, Rings[r], Fold)) - Pivot;
				const FVector2D PIn = EllipseAt(D, Rings[r] * 0.965, Phi);
				Inner[r * RimSegs + k] = FVector(PIn.X, PIn.Y, UpperOuterZ(D, Rings[r] * 0.965, FoldAt(D, PIn)) - ShellThick) - Pivot;
			}
		}
		for (int32 r = 0; r + 1 < NumRings; ++r)
		{
			for (int32 k = 0; k < RimSegs; ++k)
			{
				const int32 K1 = (k + 1) % RimSegs;
				const double Am = TNPlaygroundKit::KitTwoPi * (k + 0.5) / RimSegs;
				const FVector Up(FMath::Cos(Am) * 0.7, FMath::Sin(Am) * 0.7, 1.0);
				const FVector Down(-FMath::Cos(Am) * 0.3, -FMath::Sin(Am) * 0.3, -1.0);
				const int32 I00 = r * RimSegs + k;
				const int32 I01 = r * RimSegs + K1;
				const int32 I10 = (r + 1) * RimSegs + k;
				const int32 I11 = (r + 1) * RimSegs + K1;
				const double Fold = 0.5 * (Folds[I00] + Folds[I01]);
				FLinearColor OutCol = OuterColor(Pal, Fold, r);
				if (r == 0)
				{
					OutCol = TNPlaygroundKit::Mix(OutCol, Pal.OuterDark, 0.45);
				}
				B.AddQuad(Outer[I00], Outer[I01], Outer[I11], Outer[I10], Up, OutCol);
				// Nácar: más hondo y violeta hacia el centro, con vetas irisadas.
				const double Deep = static_cast<double>(r) / (NumRings - 1);
				FLinearColor InCol = TNPlaygroundKit::Mix(Pal.Nacre, Pal.NacreDeep, Deep);
				if (TNBeachTrapKit::Hash01(r, k, Seed) < 0.12)
				{
					InCol = TNPlaygroundKit::Mix(InCol, Pal.MantleSpot, 0.25);
				}
				B.AddQuad(Inner[I00], Inner[I01], Inner[I11], Inner[I10], Down, InCol);
			}
		}
		// Filo del labio: de la cara de fuera al nácar.
		for (int32 k = 0; k < RimSegs; ++k)
		{
			const int32 K1 = (k + 1) % RimSegs;
			const double Am = TNPlaygroundKit::KitTwoPi * (k + 0.5) / RimSegs;
			B.AddQuad(Outer[k], Outer[K1], Inner[K1], Inner[k], FVector(FMath::Cos(Am), FMath::Sin(Am), -0.4), Pal.OuterDark);
		}

		// Colisión: sectores en dos bandas (cascos casi planos: poco relleno por debajo de la bóveda).
		constexpr int32 HullSectors = 11;
		const double Bands[2][2] = { { 0.0, 0.58 }, { 0.52, 1.0 } };
		for (int32 s = 0; s < HullSectors; ++s)
		{
			const double P0 = TNPlaygroundKit::KitTwoPi * s / HullSectors;
			const double P1 = TNPlaygroundKit::KitTwoPi * (s + 1) / HullSectors;
			for (const auto& Band : Bands)
			{
				TArray<FVector> Pts;
				for (const double Phi : { P0, 0.5 * (P0 + P1), P1 })
				{
					for (const double Rho : { Band[0], 0.5 * (Band[0] + Band[1]), Band[1] })
					{
						const FVector2D P = EllipseAt(D, Rho, Phi);
						const double Z = UpperOuterZ(D, Rho, FoldAt(D, P));
						Pts.Add(FVector(P.X, P.Y, Z) - Pivot);
						Pts.Add(FVector(P.X, P.Y, Z - ShellThick - 4.0) - Pivot);
					}
				}
				Hulls.Add(Pts);
			}
		}
	}

	/** Valva de abajo (se ve el labio y el nácar hasta el manto) y el ligamento de la charnela. */
	void BuildLowerValve(FBuffers& B, const FClamDims& D, const FClamPalette& Pal)
	{
		static const double Rings[5] = { 1.045, 1.0, 0.955, 0.9, 0.86 };
		for (int32 k = 0; k < RimSegs; ++k)
		{
			const int32 K1 = (k + 1) % RimSegs;
			const double A0 = TNPlaygroundKit::KitTwoPi * k / RimSegs;
			const double A1 = TNPlaygroundKit::KitTwoPi * K1 / RimSegs;
			const double Am = TNPlaygroundKit::KitTwoPi * (k + 0.5) / RimSegs;
			FVector Ring0[5];
			FVector Ring1[5];
			for (int32 r = 0; r < 5; ++r)
			{
				for (int32 Side = 0; Side < 2; ++Side)
				{
					const double Phi = Side == 0 ? A0 : A1;
					const FVector2D P = EllipseAt(D, Rings[r], Phi);
					const double Lip = LowerLipZ(D, FoldAt(D, EllipseAt(D, 1.0, Phi)));
					const double Z = r == 0 ? Lip - 26.0 : (r == 1 ? Lip : (r == 2 ? Lip - 2.0 : (r == 3 ? D.FloorZ + 24.0 : D.FloorZ + 14.0)));
					(Side == 0 ? Ring0 : Ring1)[r] = FVector(P.X, P.Y, Z);
				}
			}
			const double Fold = FoldAt(D, EllipseAt(D, 1.0, Am));
			const FVector Out(FMath::Cos(Am), FMath::Sin(Am), 0.3);
			const FVector In(-FMath::Cos(Am), -FMath::Sin(Am), 1.2);
			B.AddQuad(Ring0[0], Ring1[0], Ring1[1], Ring0[1], Out, OuterColor(Pal, Fold, 0));
			B.AddQuad(Ring0[1], Ring1[1], Ring1[2], Ring0[2], FVector::UpVector, Pal.OuterDark);
			B.AddQuad(Ring0[2], Ring1[2], Ring1[3], Ring0[3], In, Pal.Nacre);
			B.AddQuad(Ring0[3], Ring1[3], Ring1[4], Ring0[4], In, Pal.NacreDeep);
		}
		// Ligamento de la charnela, pardo, a lo largo de X.
		TNPlaygroundKit::AddRod(B, FVector(-0.24 * D.A, D.H * D.B, D.RimZ + 6.0), FVector(0.24 * D.A, D.H * D.B, D.RimZ + 6.0), 20.0, 8,
			TNPlaygroundKit::Rgb(0x5A3A22, 0.1f), FVector::UpVector);
	}

	/** Manto de colores con motas, el volante del borde y el sifón (en el espacio del marco; se encoge sobre el centro). */
	void BuildMantle(FBuffers& B, const FClamDims& D, const FClamPalette& Pal, uint32 Seed)
	{
		static const double Rings[8] = { 0.9, 0.85, 0.76, 0.62, 0.46, 0.3, 0.15, 0.0 };
		constexpr int32 NumRings = 8;
		TArray<FVector> Grid;
		Grid.SetNum(NumRings * RimSegs);
		for (int32 r = 0; r < NumRings; ++r)
		{
			for (int32 k = 0; k < RimSegs; ++k)
			{
				const double Phi = TNPlaygroundKit::KitTwoPi * k / RimSegs;
				const FVector2D P = EllipseAt(D, Rings[r], Phi);
				const double Frill = Rings[r] > 0.8 ? 10.0 * (Rings[r] - 0.8) / 0.1 : 0.0;
				const double Wave = 5.0 * FMath::Sin(Rings[r] * 11.0 + 3.0 * Phi) * Rings[r] + (r == 0 ? 4.0 * ((k % 2) ? 1.0 : -1.0) : 0.0);
				Grid[r * RimSegs + k] = FVector(P.X, P.Y, D.FloorZ + 9.0 + Frill + Wave);
			}
		}
		for (int32 r = 0; r + 1 < NumRings; ++r)
		{
			for (int32 k = 0; k < RimSegs; ++k)
			{
				const int32 K1 = (k + 1) % RimSegs;
				FLinearColor Col = Pal.Mantle;
				if (r == 0)
				{
					Col = (k % 2) ? Pal.MantleEdge : TNPlaygroundKit::Mix(Pal.MantleEdge, Pal.MantleSpot, 0.5);
				}
				else if (TNBeachTrapKit::Hash01(r, k, Seed) < 0.2)
				{
					Col = Pal.MantleSpot;
				}
				else if (r == 2 || r == 4)
				{
					Col = TNPlaygroundKit::Mix(Pal.Mantle, Pal.MantleSpot, 0.3);
				}
				B.AddQuad(Grid[r * RimSegs + k], Grid[r * RimSegs + K1], Grid[(r + 1) * RimSegs + K1], Grid[(r + 1) * RimSegs + k], FVector::UpVector, Col);
			}
		}
		// Sifón: una boca oscura con un aro claro, hacia +X.
		const FVector Siphon(0.34 * D.A, -D.H * 0.12 * D.B, D.FloorZ + 17.0);
		TNPlaygroundKit::AddEllipsoid(B, Siphon - FVector(0.0, 0.0, 2.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(74.0, 50.0, 5.0), 14, 4,
			Pal.MantleSpot);
		TNPlaygroundKit::AddEllipsoid(B, Siphon, FVector::ForwardVector, FVector::RightVector, FVector::UpVector, FVector(58.0, 36.0, 6.0), 14, 4, Pal.Siphon);
	}

	/** Arena amontonada alrededor de la valva de abajo, con conchitas y guijarros. */
	void BuildMound(FBuffers& B, const FClamDims& D, uint32 Seed)
	{
		const TArray<FLinearColor> Cols = { TNBeachTrapKit::SandWet(), TNBeachTrapKit::SandTop(), TNBeachTrapKit::SandSide() };
		for (int32 k = 0; k < RimSegs; ++k)
		{
			const int32 K1 = (k + 1) % RimSegs;
			FVector Prof0[4];
			FVector Prof1[4];
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const double Phi = TNPlaygroundKit::KitTwoPi * (Side == 0 ? k : K1) / RimSegs;
				const double R = RadiusAt(D, Phi);
				const double Rho[4] = { 1.03, 1.03 + 26.0 / R, 1.03 + 92.0 / R, 1.03 + 112.0 / R };
				const double Z[4] = { D.RimZ - 16.0, D.RimZ - 20.0, 0.0, -25.0 };
				for (int32 p = 0; p < 4; ++p)
				{
					const FVector2D P = EllipseAt(D, Rho[p], Phi);
					(Side == 0 ? Prof0 : Prof1)[p] = FVector(P.X, P.Y, Z[p]);
				}
			}
			const double Am = TNPlaygroundKit::KitTwoPi * (k + 0.5) / RimSegs;
			const FVector Hint(FMath::Cos(Am), FMath::Sin(Am), 1.5);
			for (int32 p = 0; p < 3; ++p)
			{
				B.AddQuad(Prof0[p], Prof1[p], Prof1[p + 1], Prof0[p + 1], Hint, Cols[p]);
			}
		}
		// Adornos: conchitas y guijarros en la ladera (lejos de la charnela).
		for (int32 i = 0; i < 7; ++i)
		{
			const double Phi = TNPlaygroundKit::KitTwoPi * (i + 0.5 * TNBeachTrapKit::Hash01(i, 1, Seed)) / 7.0;
			const double R = RadiusAt(D, Phi);
			const FVector2D P = EllipseAt(D, 1.03 + 58.0 / R, Phi);
			const FVector At(P.X, P.Y, D.RimZ * 0.45);
			if (i % 2 == 0)
			{
				TNPlaygroundKit::AddShellFan(B, At, FVector(FMath::Cos(Phi), FMath::Sin(Phi), 2.0), FVector::UpVector, 26.0 + 14.0 * TNBeachTrapKit::Hash01(i, 2, Seed),
					TNBeachTrapKit::ShellTone(i));
			}
			else
			{
				TNBeachTrapKit::AddPebble(B, At, 12.0 + 8.0 * TNBeachTrapKit::Hash01(i, 3, Seed), Seed + static_cast<uint32>(i), TNBeachTrapKit::RockTone(i));
			}
		}
	}

	/** Suelo de la valva y anillo del labio con el montículo (cascos en el espacio del marco). */
	void BuildLowerHulls(FHulls& Hulls, const FClamDims& D)
	{
		constexpr int32 FloorSegs = 16;
		TArray<FVector> Floor;
		for (int32 k = 0; k < FloorSegs; ++k)
		{
			const FVector2D P = EllipseAt(D, 0.9, TNPlaygroundKit::KitTwoPi * k / FloorSegs);
			Floor.Add(FVector(P.X, P.Y, D.FloorZ + 8.0));
			Floor.Add(FVector(P.X, P.Y, D.FloorZ - 40.0));
		}
		Hulls.Add(Floor);
		constexpr int32 RimSectors = 16;
		for (int32 s = 0; s < RimSectors; ++s)
		{
			TArray<FVector> Pts;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const double Phi = TNPlaygroundKit::KitTwoPi * (s + Side) / RimSectors;
				const double R = RadiusAt(D, Phi);
				const double Foot = 1.03 + 92.0 / R;
				const double Rho[6] = { 0.86, 0.97, 1.03, Foot, Foot, 0.86 };
				const double Z[6] = { D.FloorZ + 8.0, D.RimZ, D.RimZ, 0.0, -30.0, D.FloorZ - 30.0 };
				for (int32 p = 0; p < 6; ++p)
				{
					const FVector2D P = EllipseAt(D, Rho[p], Phi);
					Pts.Add(FVector(P.X, P.Y, Z[p]));
				}
			}
			Hulls.Add(Pts);
		}
	}

	/** Perla con un brillo. */
	void BuildPearl(FBuffers& B, double Radius, const FClamPalette& Pal)
	{
		TNPlaygroundKit::AddBall(B, FVector::ZeroVector, Radius, 14, Pal.Pearl);
		TNPlaygroundKit::AddEllipsoid(B, FVector(-0.3 * Radius, -0.3 * Radius, 0.62 * Radius), FVector::ForwardVector, FVector::RightVector, FVector::UpVector,
			FVector(0.3 * Radius, 0.22 * Radius, 0.12 * Radius), 8, 4, TNPlaygroundKit::Rgb(0xFFFFFF, 1.f));
	}

	/** Destello de cuatro puntas en dos planos cruzados (100 cm de punta a punta). */
	void BuildSparkle(FBuffers& B)
	{
		const FLinearColor Col = TNPlaygroundKit::Rgb(0xFFF6C8, 1.f);
		for (int32 Plane = 0; Plane < 2; ++Plane)
		{
			const FVector U = Plane == 0 ? FVector::ForwardVector : FVector::RightVector;
			const FVector N = Plane == 0 ? FVector::RightVector : FVector::ForwardVector;
			const FVector Dirs[4] = { U, FVector::UpVector, -U, -FVector::UpVector };
			for (int32 a = 0; a < 4; ++a)
			{
				const FVector Dir = Dirs[a];
				const FVector Perp = Dirs[(a + 1) % 4];
				const FVector Tip = Dir * 50.0;
				const FVector SideA = (Dir + Perp) * 8.0;
				const FVector SideB = (Dir - Perp) * 8.0;
				for (const FVector& Face : { N, -N })
				{
					B.AddTri(FVector::ZeroVector, SideA, Tip, Face, Col);
					B.AddTri(FVector::ZeroVector, Tip, SideB, Face, Col);
				}
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachClamTrap
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachClamTrap::ATN_BeachClamTrap()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(4.f);

	Frame = CreateDefaultSubobject<USceneComponent>(TEXT("Frame"));
	Frame->SetupAttachment(GetRootComponent());

	MoundMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoundMesh"));
	MoundMesh->SetupAttachment(Frame);
	TNBeachTrapKit::ConfigureVisual(MoundMesh);

	LowerCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("LowerCollision"));
	LowerCollision->SetupAttachment(Frame);
	TNBeachTrapKit::ConfigureSolid(LowerCollision, false);

	// La colisión de la valva de arriba gira con la charnela pero no tiembla (base estable para quien se suba encima).
	HingeCollision = CreateDefaultSubobject<USceneComponent>(TEXT("HingeCollision"));
	HingeCollision->SetupAttachment(Frame);
	UpperCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("UpperCollision"));
	UpperCollision->SetupAttachment(HingeCollision);
	TNBeachTrapKit::ConfigureSolid(UpperCollision, false);

	ShakeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ShakeRoot"));
	ShakeRoot->SetupAttachment(Frame);

	LowerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LowerMesh"));
	LowerMesh->SetupAttachment(ShakeRoot);
	TNBeachTrapKit::ConfigureVisual(LowerMesh);

	MantleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MantleMesh"));
	MantleMesh->SetupAttachment(ShakeRoot);
	TNBeachTrapKit::ConfigureVisual(MantleMesh);

	PearlMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PearlMesh"));
	PearlMesh->SetupAttachment(ShakeRoot);
	TNBeachTrapKit::ConfigureVisual(PearlMesh);

	SparkleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SparkleMesh"));
	SparkleMesh->SetupAttachment(ShakeRoot);
	TNBeachTrapKit::ConfigureVisual(SparkleMesh);
	SparkleMesh->SetCastShadow(false);

	HingeVisual = CreateDefaultSubobject<USceneComponent>(TEXT("HingeVisual"));
	HingeVisual->SetupAttachment(ShakeRoot);
	UpperMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("UpperMesh"));
	UpperMesh->SetupAttachment(HingeVisual);
	TNBeachTrapKit::ConfigureVisual(UpperMesh);

	ViewCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ViewCamera"));
	ViewCamera->SetupAttachment(Frame);
	ViewCamera->SetFieldOfView(72.f);
}

void ATN_BeachClamTrap::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachClamTrap, State);
}

void ATN_BeachClamTrap::ApplySpec()
{
	using namespace TNBeachClamDetail;
	const double Fit = TNBeachTrapKit::FitRadius(Spec.Element, Spec.SizeScale);
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 71u);
	const int32 Variant = static_cast<int32>(Seed % 4u);
	HingeSide = ((Seed >> 3) & 1u) != 0u ? 1 : -1;

	FClamDims D;
	D.A = 0.5 * Fit;
	D.B = 0.36 * Fit;
	D.RimZ = 38.0;
	D.FloorZ = 10.0;
	// La bóveda de arriba deja ~1,7 m libres dentro con la concha cerrada (la tortuga cabe de pie).
	D.Dome = FMath::Max(155.0, 0.5 * D.B);
	D.H = static_cast<double>(HingeSide);
	HalfX = D.A;
	HalfY = D.B;
	RimZ = D.RimZ;
	FloorZ = D.FloorZ;
	DomeZ = D.Dome;
	BreathPhase = static_cast<float>(TNPlaygroundKit::KitTwoPi * TNBeachTrapKit::Hash01(5, 5, Seed));

	// Hacia el mar, con un poco de giro.
	const double Jitter = (TNBeachTrapKit::Hash01(3, 9, Seed) - 0.5) * 50.0;
	Frame->SetRelativeRotation(FRotator(0.0, TNBeachRideKit::SeaYawInActor(this) + Jitter, 0.0));

	const FClamPalette Pal = MakePalette(Variant);
	TNBeachTrapKit::FBuffers Mound;
	BuildMound(Mound, D, Seed);
	TNBeachTrapKit::SetMesh(MoundMesh, this, Mound, TN_ART("Beach.ClamTrap.Mound"));

	TNBeachTrapKit::FBuffers Lower;
	BuildLowerValve(Lower, D, Pal);
	TNBeachTrapKit::SetMesh(LowerMesh, this, Lower, TN_ART("Beach.ClamTrap.LowerShell"));

	TNBeachTrapKit::FBuffers Mantle;
	BuildMantle(Mantle, D, Pal, Seed);
	TNBeachTrapKit::SetMesh(MantleMesh, this, Mantle, TN_ART("Beach.ClamTrap.Mantle"));

	const double PearlR = FMath::Clamp(0.055 * Fit, 38.0, 60.0);
	TNBeachTrapKit::FBuffers Pearl;
	BuildPearl(Pearl, PearlR, Pal);
	TNBeachTrapKit::SetMesh(PearlMesh, this, Pearl, TN_ART("Beach.ClamTrap.Pearl"));
	const FVector PearlAt(0.0, D.H * 0.46 * D.B, D.FloorZ + 14.0 + 0.85 * PearlR);
	PearlMesh->SetRelativeLocation(PearlAt);

	TNBeachTrapKit::FBuffers Sparkle;
	BuildSparkle(Sparkle);
	TNBeachTrapKit::SetMesh(SparkleMesh, this, Sparkle);
	SparkleMesh->SetRelativeLocation(PearlAt + FVector(-0.2 * PearlR, 0.0, 1.1 * PearlR));

	TNBeachTrapKit::FBuffers Upper;
	TNBeachTrapKit::FHulls UpperHulls;
	BuildUpperValve(Upper, UpperHulls, D, Pal, Seed);
	TNBeachTrapKit::SetMesh(UpperMesh, this, Upper, TN_ART("Beach.ClamTrap.UpperShell"));
	UpperCollision->SetCollisionConvexMeshes(UpperHulls);

	TNBeachTrapKit::FHulls LowerHulls;
	BuildLowerHulls(LowerHulls, D);
	LowerCollision->SetCollisionConvexMeshes(LowerHulls);

	const FVector Pivot(0.0, D.H * D.B, D.RimZ);
	HingeCollision->SetRelativeLocation(Pivot);
	HingeVisual->SetRelativeLocation(Pivot);
	ApplyHingeAngle(OpenDeg, 0.0);

	// Cámara de la presa: fuera, del lado de la boca y un poco por detrás, mirando dentro.
	const double Sc = Fit / 900.0;
	const FVector CamAt(-420.0 * Sc, -D.H * (D.B + 820.0 * Sc), 470.0 * Sc);
	const FVector Look(0.0, -D.H * 0.1 * D.B, 110.0);
	ViewCamera->SetRelativeLocationAndRotation(CamAt, (Look - CamAt).Rotation());

	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Concha %s: %.0f x %.0f cm, manto %d, charnela a %s."), *GetName(), 2.0 * D.A, 2.0 * D.B, Variant,
		HingeSide > 0 ? TEXT("+Y") : TEXT("-Y"));
}

void ATN_BeachClamTrap::BeginPlay()
{
	Super::BeginPlay();
	Voice = UTN_BeachTrapSynthComponent::AttachTo(this, Frame->GetComponentTransform().TransformPosition(FVector(0.0, 0.0, RimZ + 60.0)), 700.f, 3200.f);
	Smoke.Init(this, ETNTrapBurstShape::Blob, TNPlaygroundKit::Rgb(0xEDE3CF), 48);
	Smoke.SetMotion(90.f, 1.6f, 45.f, 150.f, 0.8f, 1.4f);
	Bubbles.Init(this, ETNTrapBurstShape::Blob, TNPlaygroundKit::Rgb(0xE6F7FF, 0.6f), 32);
	Bubbles.SetMotion(-600.f, 0.8f, 22.f, 6.f, 0.4f, 0.8f);
	Sand.Init(this, ETNTrapBurstShape::Blob, TNBeachTrapKit::SandTop(), 28);
	Sand.SetMotion(-700.f, 1.5f, 32.f, 8.f, 0.4f, 0.8f);
}

void ATN_BeachClamTrap::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bHeldApplied)
	{
		ReleaseLocal(false);
	}
	EndDizzy();
	RestoreLocalInput();
	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Consultas
// ─────────────────────────────────────────────────────────────────────────────

ACharacter* ATN_BeachClamTrap::GetCaptive() const
{
	return State.Captive.Get();
}

void ATN_BeachClamTrap::GatherNearValve(TArray<ACharacter*>& Out) const
{
	// La valva más grande que se mira (Rho 1,05) cabe en el semieje mayor; más el margen, con la escala de la valva.
	const FTransform FrameXf = Frame->GetComponentTransform();
	const double Radius = (FMath::Max(HalfX, HalfY) * 1.1 + TNBeachClamDetail::GatherMargin) * FrameXf.GetMaximumAxisScale();
	TNBeachNearby::Gather(GetWorld(), FrameXf.GetLocation(), Radius, Out);
}

bool ATN_BeachClamTrap::IsInBowl(const ACharacter* Character, double RhoMax, double& OutRho) const
{
	OutRho = 10.0;
	if (!Character)
	{
		return false;
	}
	const FVector Feet = TNBeachRideKit::FeetIn(Frame->GetComponentTransform(), Character);
	if (Feet.Z < FloorZ - 60.0 || Feet.Z > FloorZ + 160.0)
	{
		return false;
	}
	OutRho = FMath::Sqrt(FMath::Square(Feet.X / HalfX) + FMath::Square(Feet.Y / HalfY));
	return OutRho < RhoMax;
}

FVector ATN_BeachClamTrap::HoldPointWorld(const ACharacter* Character) const
{
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	const double Half = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0;
	return Frame->GetComponentTransform().TransformPosition(FVector(0.0, -HingeSide * 0.1 * HalfY, FloorZ + 8.0 + Half + 4.0));
}

FVector ATN_BeachClamTrap::SpitVelocityWorld() const
{
	// Hacia la boca (el lado contrario a la charnela) y hacia el mar.
	const FVector Local = FVector(0.55, -HingeSide * 0.84, 0.0).GetSafeNormal();
	const FVector Dir = Frame->GetComponentTransform().TransformVectorNoScale(Local).GetSafeNormal2D();
	return Dir * SpitHorizontal + FVector::UpVector * SpitUp;
}

double ATN_BeachClamTrap::ReadyAt() const
{
	if (State.SnapAt < 0.f)
	{
		return -1.0;
	}
	return static_cast<double>(State.OpenAt) + OpenSeconds + RechargeSeconds;
}

double ATN_BeachClamTrap::UpperAngleAt(double Now) const
{
	using namespace TNBeachClamDetail;
	if (State.SnapAt < 0.f)
	{
		return OpenDeg;
	}
	const double T = Now - static_cast<double>(State.SnapAt);
	if (T < 0.0)
	{
		return OpenDeg;
	}
	if (T < TellSeconds)
	{
		// Aviso: se levanta un poco temblando, como si cogiera aire.
		return OpenDeg + TwitchDeg * FMath::Sin(TNPlaygroundKit::KitPi * T / FMath::Max(0.01, static_cast<double>(TellSeconds)))
			+ 1.5 * FMath::Sin(T * 70.0);
	}
	const double C = T - TellSeconds;
	if (C < CloseSeconds)
	{
		// Golpe: acelera hasta cerrarse.
		const double U = C / FMath::Max(0.01, static_cast<double>(CloseSeconds));
		return OpenDeg * (1.0 - U * U);
	}
	const double OpenAt = static_cast<double>(State.OpenAt);
	if (Now < OpenAt)
	{
		return 0.0;
	}
	const double O = Now - OpenAt;
	if (O < OpenSeconds)
	{
		// Se abre de golpe con un pasito de más.
		const double U = O / FMath::Max(0.01, static_cast<double>(OpenSeconds));
		return OpenDeg * (1.0 - FMath::Pow(1.0 - U, 3.0)) + 6.0 * FMath::Sin(TNPlaygroundKit::KitPi * U);
	}
	return OpenDeg;
}

float ATN_BeachClamTrap::StruggleAt(double Now, int32& OutPulse) const
{
	using namespace TNBeachClamDetail;
	OutPulse = -1;
	if (!State.Captive && !HeldLocal.IsValid())
	{
		return 0.f;
	}
	const double Start = static_cast<double>(State.SnapAt) + TellSeconds + CloseSeconds + 0.3;
	const double OpenAt = static_cast<double>(State.OpenAt);
	if (State.SnapAt < 0.f || Now < Start || Now >= OpenAt)
	{
		return 0.f;
	}
	if (Now >= OpenAt - BuildUpSeconds)
	{
		OutPulse = 100000;
		return static_cast<float>(0.45 + 0.55 * (Now - (OpenAt - BuildUpSeconds)) / BuildUpSeconds);
	}
	const double Local = Now - Start;
	const int32 K = FMath::FloorToInt32(Local / StruggleInterval);
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 73u);
	for (int32 Back = 0; Back < 2; ++Back)
	{
		const int32 J = K - Back;
		if (J < 0)
		{
			break;
		}
		const double Tj = J * StruggleInterval + 0.18 * TNBeachTrapKit::Hash01(J, 5, Seed);
		if (Local >= Tj)
		{
			OutPulse = J;
			return static_cast<float>(FMath::Exp(-(Local - Tj) / StruggleDecay));
		}
	}
	return 0.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachClamTrap::ServerTick(double Now)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Presa dentro: se suelta antes si ya no está libre; si no, la escupe al abrirse.
	if (State.Captive)
	{
		ACharacter* Held = State.Captive;
		ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Held);
		const UTN_CarryComponent* Carry = Turtle ? Turtle->GetCarryComponent() : nullptr;
		const bool bBroken = !IsValid(Held) || Held->IsActorBeingDestroyed() || !Held->GetController() || !Turtle || Turtle->IsDead() || Turtle->IsInShell()
			|| Turtle->IsKnockedDown() || TNBeach::IsTurtleStunned(Turtle) || (Carry && Carry->GetCarrier() != nullptr);
		if (bBroken)
		{
			if (Now < static_cast<double>(State.OpenAt))
			{
				State.OpenAt = static_cast<float>(Now);
			}
			ReleaseLocal(false);
			State.Captive = nullptr;
			ForceNetUpdate();
			UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Concha %s: la presa se suelta antes de tiempo."), *GetName());
		}
		else if (Now >= static_cast<double>(State.OpenAt) + SpitDelay)
		{
			State.SpitAt = static_cast<float>(Now);
			ReleaseLocal(true);
			ImmuneUntil.Add(Held, Now + 3.0);
			State.Captive = nullptr;
			ForceNetUpdate();
			UE_LOG(LogTortunabo, Log, TEXT("[Playa] Concha %s escupe a %s."), *GetName(), *GetNameSafe(Held));
		}
		return;
	}

	// Aviso en marcha: al acabar, se cierra.
	if (bSnapPending)
	{
		if (Now >= static_cast<double>(State.SnapAt) + TellSeconds)
		{
			bSnapPending = false;
			Slam(Now);
		}
		return;
	}

	// Cerrada o recargando.
	if (Now < ReadyAt() || !TNBeachRideKit::IsRaceLive(this))
	{
		return;
	}
	for (auto It = ImmuneUntil.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || Now >= It.Value())
		{
			It.RemoveCurrent();
		}
	}
	// ¿Alguien pisa el manto? (solo quien está al alcance de la valva)
	TArray<ACharacter*> Near;
	GatherNearValve(Near);
	for (ACharacter* Walker : Near)
	{
		double Rho = 0.0;
		if (!TNBeachRideKit::IsFreeRider(Walker) || !IsInBowl(Walker, 0.72, Rho) || ImmuneUntil.Contains(Walker))
		{
			continue;
		}
		const UCharacterMovementComponent* Move = Walker->GetCharacterMovement();
		if (!Move || !Move->IsMovingOnGround())
		{
			continue;
		}
		State.SnapAt = static_cast<float>(Now);
		State.OpenAt = static_cast<float>(Now + TellSeconds + CloseSeconds + EmptyHoldSeconds);
		State.Captive = nullptr;
		bSnapPending = true;
		ForceNetUpdate();
		UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Concha %s: %s pisa el manto."), *GetName(), *GetNameSafe(Walker));
		break;
	}
}

void ATN_BeachClamTrap::Slam(double Now)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FTransform FrameXf = Frame->GetComponentTransform();
	ACharacter* Best = nullptr;
	double BestRho = 10.0;
	TArray<ACharacter*> InValve;
	if (TNBeachRideKit::IsRaceLive(this))
	{
		TArray<ACharacter*> Near;
		GatherNearValve(Near);
		for (ACharacter* Walker : Near)
		{
			double Rho = 0.0;
			if (!TNBeachRideKit::IsFreeRider(Walker) || !IsInBowl(Walker, 1.05, Rho))
			{
				continue;
			}
			InValve.Add(Walker);
			if (Rho < 0.86 && Rho < BestRho && !ImmuneUntil.Contains(Walker))
			{
				Best = Walker;
				BestRho = Rho;
			}
		}
	}

	const double Closed = static_cast<double>(State.SnapAt) + TellSeconds + CloseSeconds;
	const float Hold = FMath::FRandRange(FMath::Min(HoldMin, HoldMax), FMath::Max(HoldMin, HoldMax));
	State.OpenAt = static_cast<float>(Closed + (Best ? static_cast<double>(Hold) : static_cast<double>(EmptyHoldSeconds)));
	State.Captive = Best;
	if (Best)
	{
		HoldLocal(Best);
	}

	// Las demás que estaban en la valva salen despedidas hacia fuera.
	TArray<APawn*> Pawns;
	TArray<FVector> Velocities;
	for (ACharacter* Other : InValve)
	{
		if (Other == Best)
		{
			continue;
		}
		const FVector Local = FrameXf.InverseTransformPosition(Other->GetActorLocation());
		FVector2D Out(Local.X / HalfX, Local.Y / HalfY);
		if (!Out.Normalize())
		{
			Out = FVector2D(0.0, -static_cast<double>(HingeSide));
		}
		const FVector Dir = FrameXf.TransformVectorNoScale(FVector(Out.X, Out.Y, 0.0)).GetSafeNormal2D();
		const FVector Velocity = Dir * ShoveOut + FVector::UpVector * ShoveUp;
		Other->LaunchCharacter(Velocity, true, true);
		Pawns.Add(Other);
		Velocities.Add(Velocity);
	}
	if (Pawns.Num() > 0)
	{
		MulticastShove(Pawns, Velocities);
	}
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] Concha %s se cierra: %s dentro (%.1f s), %d fuera."), *GetName(), Best ? *GetNameSafe(Best) : TEXT("nadie"),
		static_cast<double>(State.OpenAt) - Closed, Pawns.Num());
}

void ATN_BeachClamTrap::MulticastShove_Implementation(const TArray<APawn*>& Pawns, const TArray<FVector>& Velocities)
{
	if (GetNetMode() != NM_Client)
	{
		return;
	}
	for (int32 i = 0; i < Pawns.Num(); ++i)
	{
		ACharacter* Pushed = Cast<ACharacter>(Pawns[i]);
		if (Pushed && Pushed->IsLocallyControlled() && Velocities.IsValidIndex(i))
		{
			// Su cliente aplica el mismo empujón: la corrección del servidor queda pequeña.
			Pushed->LaunchCharacter(Velocities[i], true, true);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Presa (cada máquina)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachClamTrap::OnRep_State()
{
	ACharacter* NewCaptive = State.Captive;
	if (bHeldApplied && HeldLocal.Get() != NewCaptive)
	{
		// Se ha soltado (o ha cambiado): escupida si el servidor la escupió en este cierre, si no, suelta sin más.
		const bool bSpat = State.SpitAt >= 0.f && State.SpitAt >= State.SnapAt;
		ReleaseLocal(bSpat);
	}
	if (NewCaptive && !bHeldApplied && TNBeachTrapKit::ServerNow(GetWorld()) < static_cast<double>(State.OpenAt) + SpitDelay)
	{
		HoldLocal(NewCaptive);
	}
}

void ATN_BeachClamTrap::HoldLocal(ACharacter* Victim)
{
	if (!Victim)
	{
		return;
	}
	if (bHeldApplied)
	{
		if (HeldLocal.Get() == Victim)
		{
			return;
		}
		ReleaseLocal(false);
	}
	// Si seguía mareada de esta misma concha, se acaba aquí (las teclas siguen sin mover las patas: ahora está dentro).
	if (DizzyTurtle.Get() == Victim)
	{
		TNBeachRideKit::SetDizzyBirds(Victim, false);
		DizzyTurtle.Reset();
		DizzyUntil = -1.0;
	}
	HeldLocal = Victim;
	bHeldApplied = true;

	// Solo mueve a la tortuga quien la simula: el servidor y su cliente, a la vez (como el probador del lobby).
	if (HasAuthority() || Victim->IsLocallyControlled())
	{
		if (UCharacterMovementComponent* Move = Victim->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
		}
		const FVector Mouth = Frame->GetComponentTransform().TransformVectorNoScale(FVector(0.55, -HingeSide * 0.84, 0.0)).GetSafeNormal2D();
		Victim->SetActorLocationAndRotation(HoldPointWorld(Victim), FRotator(0.0, Mouth.Rotation().Yaw, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (Victim->IsLocallyControlled())
	{
		APlayerController* PC = Cast<APlayerController>(Victim->GetController());
		if (PC && PC->IsLocalController())
		{
			if (!bInputIgnored)
			{
				PC->SetIgnoreMoveInput(true);
				bInputIgnored = true;
				IgnoringController = PC;
			}
			// Se ve desde fuera: la concha temblando y el humo.
			PC->SetViewTargetWithBlend(this, 0.35f, VTBlend_EaseInOut, 2.f);
			bViewOnClam = true;
		}
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		const FVector Top = Frame->GetComponentTransform().TransformPosition(FVector(0.0, 0.0, RimZ + DomeZ + 120.0));
		Pop.Show(this, NSLOCTEXT("TNBeach", "ClamGulp", "¡ÑAM!"), FColor(255, 150, 90), Top, 170.f);
		if (Voice)
		{
			Voice->TriggerSoundAt(ETNBeachTrapSound::Ouch, Victim->GetActorLocation(), 1.25f, 0.45f);
		}
	}
}

void ATN_BeachClamTrap::ReleaseLocal(bool bSpit)
{
	ACharacter* Victim = HeldLocal.Get();
	const bool bWasHeld = bHeldApplied;
	bHeldApplied = false;
	HeldLocal.Reset();

	// La cámara vuelve a la tortuga (solo si sigue en la concha: otra cosa pudo cambiarla).
	if (bViewOnClam)
	{
		bViewOnClam = false;
		APlayerController* PC = IgnoringController.Get();
		if (PC && PC->GetViewTarget() == this)
		{
			AActor* Back = Victim ? static_cast<AActor*>(Victim) : static_cast<AActor*>(PC->GetPawn());
			if (Back)
			{
				PC->SetViewTargetWithBlend(Back, 0.4f, VTBlend_EaseInOut, 2.f);
			}
		}
	}
	if (!bWasHeld || !Victim)
	{
		RestoreLocalInput();
		return;
	}

	if (HasAuthority() || Victim->IsLocallyControlled())
	{
		UCharacterMovementComponent* Move = Victim->GetCharacterMovement();
		const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Victim);
		const UTN_ShellComponent* Shell = Turtle ? Turtle->GetShellComponent() : nullptr;
		const bool bStill = Move && Move->MovementMode == MOVE_None && !(Shell && (Shell->IsInShell() || Shell->HasLocalBody()))
			&& !(Turtle && (Turtle->IsDead() || Turtle->IsKnockedDown()));
		// En el recuento o el podio la deja quieta: el GameMode la tiene congelada.
		if (bStill && TNBeachRideKit::IsRaceLive(this))
		{
			Move->SetMovementMode(MOVE_Falling);
			if (bSpit)
			{
				// Un saltito como al salir del huevo o del probador.
				const FVector Velocity = SpitVelocityWorld();
				Victim->SetActorRotation(FRotator(0.0, Velocity.Rotation().Yaw, 0.0));
				Victim->LaunchCharacter(Velocity, true, true);
			}
		}
	}

	if (!bSpit)
	{
		RestoreLocalInput();
		return;
	}
	// Mareada: pajaritos y, la local, sin mover las patas hasta DizzySeconds después de aterrizar (tope de 3 s).
	const double Now = Clock.Now() > 0.0 ? Clock.Now() : TNBeachTrapKit::ServerNow(GetWorld());
	if (ACharacter* Previous = DizzyTurtle.Get())
	{
		if (Previous != Victim)
		{
			TNBeachRideKit::SetDizzyBirds(Previous, false);
		}
	}
	DizzyTurtle = Victim;
	DizzySpitAt = Now;
	DizzyLandedAt = -1.0;
	DizzyUntil = Now + 3.0;
	TNBeachRideKit::SetDizzyBirds(Victim, true);
	if (GetNetMode() != NM_DedicatedServer)
	{
		const FVector At = Victim->GetActorLocation() + FVector(0.0, 0.0, 140.0);
		Pop.Show(this, NSLOCTEXT("TNBeach", "ClamSpit", "¡PTUI!"), FColor(170, 240, 150), At, 150.f);
		Bubbles.Burst(Victim->GetActorLocation(), 10, FVector::UpVector, 360.f, 0.9f, 40.f);
		if (Voice)
		{
			Voice->TriggerSoundAt(ETNBeachTrapSound::Squelch, Victim->GetActorLocation(), FMath::FRandRange(1.25f, 1.4f), 1.f);
		}
	}
}

void ATN_BeachClamTrap::EndDizzy()
{
	if (ACharacter* Dizzy = DizzyTurtle.Get())
	{
		TNBeachRideKit::SetDizzyBirds(Dizzy, false);
	}
	DizzyTurtle.Reset();
	DizzyUntil = -1.0;
	DizzyLandedAt = -1.0;
	if (!bHeldApplied)
	{
		RestoreLocalInput();
	}
}

void ATN_BeachClamTrap::RestoreLocalInput()
{
	if (!bInputIgnored)
	{
		return;
	}
	if (APlayerController* PC = IgnoringController.Get())
	{
		PC->SetIgnoreMoveInput(false);
	}
	bInputIgnored = false;
	IgnoringController.Reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachClamTrap::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = Clock.Advance(GetWorld(), DeltaSeconds);
	if (HasAuthority())
	{
		ServerTick(TNBeachTrapKit::ServerNow(GetWorld()));
	}
	else
	{
		// La presa (y quien la ve) la suelta a la hora de OpenAt sin esperar a la réplica.
		if (bHeldApplied && State.OpenAt >= 0.f && Now >= static_cast<double>(State.OpenAt) + SpitDelay)
		{
			ReleaseLocal(true);
		}
		// El peón de la presa desapareció (desconexión): cámara y teclas en su sitio.
		if (bHeldApplied && !HeldLocal.IsValid())
		{
			ReleaseLocal(false);
		}
	}

	// Mareo: DizzySeconds después de aterrizar.
	if (ACharacter* Dizzy = DizzyTurtle.Get())
	{
		if (DizzyLandedAt < 0.0)
		{
			const UCharacterMovementComponent* Move = Dizzy->GetCharacterMovement();
			if (Move && Move->IsMovingOnGround() && Now > DizzySpitAt + 0.25)
			{
				DizzyLandedAt = Now;
				DizzyUntil = FMath::Min(DizzyUntil, Now + static_cast<double>(DizzySeconds));
			}
		}
		if (Now >= DizzyUntil)
		{
			EndDizzy();
		}
	}
	else if (DizzyUntil >= 0.0)
	{
		EndDizzy();
	}

	TickVisuals(Now, DeltaSeconds);
	Smoke.Tick(DeltaSeconds);
	Bubbles.Tick(DeltaSeconds);
	Sand.Tick(DeltaSeconds);
	Pop.Tick(DeltaSeconds, GetWorld());
}

void ATN_BeachClamTrap::ApplyHingeAngle(double Degrees, double Burp)
{
	// La boca queda a -H·Y de la charnela: abrir es girar sobre X hacia arriba (-H·ángulo).
	const double Side = static_cast<double>(HingeSide);
	const FQuat Gameplay(FVector::ForwardVector, FMath::DegreesToRadians(-Side * Degrees));
	if (!HingeCollision->GetRelativeRotation().Quaternion().Equals(Gameplay, 1e-4))
	{
		HingeCollision->SetRelativeRotation(Gameplay);
	}
	HingeVisual->SetRelativeRotation(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-Side * (Degrees + Burp))));
}

void ATN_BeachClamTrap::PuffAtRim(int32 Count, float Strength)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const FTransform FrameXf = ShakeRoot->GetComponentTransform();
	for (int32 i = 0; i < Count; ++i)
	{
		// Por la rendija, lejos de la charnela.
		const double Phi = FMath::FRandRange(0.0, TNPlaygroundKit::KitTwoPi);
		if (FMath::Sin(Phi) * HingeSide > 0.55)
		{
			continue;
		}
		const FVector Local(HalfX * FMath::Cos(Phi), HalfY * FMath::Sin(Phi), RimZ + 12.0);
		const FVector Out = FrameXf.TransformVectorNoScale(FVector(FMath::Cos(Phi), FMath::Sin(Phi), 0.8)).GetSafeNormal();
		const FVector At = FrameXf.TransformPosition(Local);
		Smoke.Burst(At, 2, Out, 170.f * Strength, 0.6f, 25.f);
		Bubbles.Burst(At, 2, FVector::UpVector, 300.f * Strength, 0.5f, 15.f);
	}
}

void ATN_BeachClamTrap::TickVisuals(double Now, float DeltaSeconds)
{
	using namespace TNBeachClamDetail;
	const double Snap = static_cast<double>(State.SnapAt);
	const double OpenAt = static_cast<double>(State.OpenAt);
	const double SlamAt = Snap + TellSeconds + CloseSeconds;
	const bool bIdle = State.SnapAt < 0.f || Now >= OpenAt + OpenSeconds || Now < Snap;
	const bool bReady = Now >= ReadyAt() && bIdle;

	// Valva de arriba: la del juego, más la respiración en reposo y el eructo de cada sacudida.
	int32 Pulse = -1;
	const float Struggle = StruggleAt(Now, Pulse);
	const double Breath = bIdle ? 2.0 * FMath::Sin(Now * 1.3 + BreathPhase) : 0.0;
	ApplyHingeAngle(UpperAngleAt(Now), Breath + 3.0 * Struggle);

	// Temblor de las valvas (solo lo que se ve).
	double Shake = static_cast<double>(Struggle);
	if (!bIdle && Now >= Snap && Now < Snap + TellSeconds)
	{
		Shake = FMath::Max(Shake, 0.25);
	}
	if (Shake > 0.001 && GetNetMode() != NM_DedicatedServer)
	{
		const FVector Offset(FMath::Sin(Now * 53.0) * 5.0, FMath::Sin(Now * 61.0 + 1.0) * 4.0, FMath::Abs(FMath::Sin(Now * 47.0)) * 3.0);
		ShakeRoot->SetRelativeLocationAndRotation(Offset * Shake, FRotator(FMath::Sin(Now * 41.0) * 1.4 * Shake, 0.0, FMath::Sin(Now * 44.0 + 0.5) * 2.4 * Shake));
	}
	else if (!ShakeRoot->GetRelativeLocation().IsNearlyZero(0.01) || !ShakeRoot->GetRelativeRotation().IsNearlyZero(0.01))
	{
		ShakeRoot->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	}

	// Manto: encogido con la concha cerrada y mientras recarga; se abre al estar lista.
	float MantleTarget = 1.f;
	if (!bIdle && Now >= Snap + TellSeconds)
	{
		MantleTarget = 0.78f;
	}
	else if (!bReady && State.SnapAt >= 0.f)
	{
		const double Left = ReadyAt() - Now;
		MantleTarget = Left > 0.8 ? 0.86f : static_cast<float>(FMath::Lerp(1.0, 0.86, FMath::Clamp(Left / 0.8, 0.0, 1.0)));
	}
	MantleOpen = FMath::FInterpTo(MantleOpen, MantleTarget, DeltaSeconds, 6.f);
	MantleMesh->SetRelativeScale3D(FVector(MantleOpen, MantleOpen, 0.7f + 0.3f * MantleOpen));

	// Destello de la perla: solo lista, a golpes.
	if (GetNetMode() != NM_DedicatedServer)
	{
		SparkleMesh->SetVisibility(bReady);
		if (bReady)
		{
			const double Twinkle = FMath::Pow(FMath::Max(0.0, FMath::Sin(Now * 3.3 + BreathPhase)), 6.0);
			SparkleMesh->SetRelativeScale3D(FVector(0.2 + 0.95 * Twinkle));
			SparkleMesh->SetRelativeRotation(FRotator(0.0, Now * 25.0, Now * 40.0));
		}
	}

	// Efectos por horas: aviso, golpe, sacudidas, apertura y recarga.
	if (GetNetMode() != NM_DedicatedServer)
	{
		const double Before = LastVisualNow;
		auto Crossed = [Before, Now](double At) { return At >= 0.0 && Before < At && Now >= At && Now - At < 1.0; };
		const FVector Center = Frame->GetComponentTransform().TransformPosition(FVector(0.0, 0.0, RimZ + 40.0));
		if (Crossed(Snap) && Voice)
		{
			Voice->TriggerSoundAt(ETNBeachTrapSound::Clack, Center, 1.35f, 0.6f);
		}
		if (Crossed(SlamAt))
		{
			Sand.Burst(Center - FVector(0.0, 0.0, 30.0), 16, FVector::UpVector, 380.f, 1.3f, static_cast<float>(0.8 * HalfY));
			if (Voice)
			{
				Voice->TriggerSoundAt(ETNBeachTrapSound::Clack, Center, 0.55f, 1.3f);
				Voice->TriggerSoundAt(ETNBeachTrapSound::Thud, Center, 0.75f, 1.1f);
			}
			UTN_BeachCameraShake::Kick(this, Center, 0.35f, 500.f, 2400.f);
		}
		if (Pulse >= 0 && Pulse != LastPulse && Pulse < 100000)
		{
			PuffAtRim(3, 0.8f + 0.4f * Struggle);
			if (Voice)
			{
				Voice->TriggerSoundAt(ETNBeachTrapSound::Thud, Center, FMath::FRandRange(1.0f, 1.25f), 0.55f);
				if (Pulse % 3 == 1)
				{
					Voice->TriggerSoundAt(ETNBeachTrapSound::Ouch, Center, 1.35f, 0.3f);
				}
			}
		}
		if (Pulse == 100000)
		{
			// Últimos instantes: humo sin parar.
			if (FMath::FRand() < DeltaSeconds * 14.f)
			{
				PuffAtRim(1, 1.1f);
			}
			if (LastPulse != 100000 && Voice)
			{
				Voice->TriggerSoundAt(ETNBeachTrapSound::Grind, Center, 0.8f, 0.8f);
			}
		}
		if (Crossed(OpenAt) && State.SnapAt >= 0.f)
		{
			Bubbles.Burst(Center, 12, FVector::UpVector, 420.f, 0.8f, static_cast<float>(0.5 * HalfY));
			if (Voice)
			{
				Voice->TriggerSoundAt(ETNBeachTrapSound::Clack, Center, 0.85f, 0.9f);
			}
		}
		if (Crossed(ReadyAt()) && Voice)
		{
			Voice->TriggerSoundAt(ETNBeachTrapSound::Plink, PearlMesh->GetComponentLocation(), 1.5f, 0.55f);
		}
	}
	LastPulse = Pulse;
	LastVisualNow = Now;
}
