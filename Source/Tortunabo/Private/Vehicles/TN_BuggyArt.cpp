// Arte del buggy del Rally (#297): ver TN_BuggyArt.h. Espacio del chasis en cm: X hacia delante, Y a la derecha, Z
// arriba, el suelo en Z = 0. Las tortugas se sientan como en el buggy de serie (sockets de SM_TN_BuggyBody): la conductora
// en el centro de la bañera y la artillera en un sillín sobre el lomo, con los pies en un hueco del caparazón, y la
// torreta (TNBuggyTurretMesh) se sujeta a las barandillas de la cabina.

#include "TN_BuggyArt.h"
#include "TN_BuggyArtKit.h"
#include "Vehicles/TN_BuggyMath.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"

using namespace TNBuggyKit;

namespace TNBuggyArtDetail
{
	using TNBuggyArt::EPiece;
	namespace Frame = TNBuggyArt::Frame;

	const TCHAR* const PaintMaterialPath = TEXT("/Game/Vehicles/Buggy/M_BuggyPaint.M_BuggyPaint");
	const TCHAR* const VertexColorMaterialPath = TEXT("/Game/Cosmetics/Materials/M_CosmeticVertexColor.M_CosmeticVertexColor");

	// El buggy de serie (Art/Source/Vehicles/Buggy importado por Scripts/tools/import_buggy_rally.py): las rutas de ATN_Buggy.
	const TCHAR* const StockBodyPath = TEXT("/Game/Art/Source/Vehicles/Buggy/export/SM_TN_BuggyBody.SM_TN_BuggyBody");
	const TCHAR* const StockTirePath = TEXT("/Game/Art/Source/Vehicles/Buggy/export/SM_TN_BuggyTire.SM_TN_BuggyTire");
	const TCHAR* const StockSkinPaths[] = {
		TEXT("/Game/Art/Source/Vehicles/Buggy/export/MI_TN_Buggy_Mar.MI_TN_Buggy_Mar"),
		TEXT("/Game/Art/Source/Vehicles/Buggy/export/MI_TN_Buggy_Alga.MI_TN_Buggy_Alga"),
		TEXT("/Game/Art/Source/Vehicles/Buggy/export/MI_TN_Buggy_Medusa.MI_TN_Buggy_Medusa"),
	};

	/** Suelo de la bañera (cm): donde apoya los pies la conductora. */
	constexpr double FloorZ = Frame::DriverFloorZ;

	/** Antena del buggy de serie: en la esquina trasera izquierda del parachoques, lejos de la torreta y de los tirantes. */
	const FVector StockAntennaMount(-198.0, -58.0, 63.0);

	inline double Smooth(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** Interpolación lineal en una tabla de 5 nudos repartidos en [0, 1]. */
	inline double Table5(const double (&V)[5], double T)
	{
		const double S = FMath::Clamp(T, 0.0, 1.0) * 4.0;
		const int32 I = FMath::Min(3, FMath::FloorToInt(S));
		return FMath::Lerp(V[I], V[I + 1], S - I);
	}

	/** Valor estable en [0, 1] por índice (tonos de las placas). */
	inline float Hash01(int32 A, int32 B)
	{
		return static_cast<float>(0.5 + 0.5 * TNProcMesh::TNProcHashNoise(A, B, 811u));
	}

	// ─────────────────────────────────────────────────────────────────────────────
	// Caparazón: cúpula elíptica con la escotadura de la cabina delante (como la muesca de la nuca de una tortuga)
	// ─────────────────────────────────────────────────────────────────────────────

	struct FDome
	{
		double Cx = -70.0;
		double A = 145.0;
		double B = 110.0;
		/** Lo más alto queda por debajo del cojín de la artillera (Frame::GunnerCushionZ), que va sentada encima. */
		double Z0 = 62.0;
		double H = 44.0;
		double K = 0.62;
		/** Escotadura de la cabina: desde NotchX hacia delante, |Y| <= NotchHalfY, esquinas de radio NotchCorner. */
		double NotchX = -14.0;
		double NotchHalfY = 68.0;
		double NotchCorner = 16.0;
		/**
		 * Hueco de los pies de la artillera, detrás de la cabina: desde WellX hacia delante, |Y| <= WellHalfY, con el suelo
		 * a Frame::GunnerFloorZ (las piernas bajan del sillín hacia delante, como en el de serie).
		 */
		double WellX = -64.0;
		double WellHalfY = 30.0;
		double WellCorner = 10.0;
		/** Borde de la escotadura alargado por delante, para PushOutOfNotch (lo rellena Finish). */
		TArray<FVector2D> Edge;

		double R2(double X, double Y) const
		{
			const double U = (X - Cx) / A;
			const double V = Y / B;
			return U * U + V * V;
		}

		double Height(double X, double Y) const
		{
			return Z0 + H * FMath::Pow(FMath::Max(0.0, 1.0 - FMath::Min(R2(X, Y), 1.0)), K);
		}

		FVector Normal(double X, double Y) const
		{
			const double U = (X - Cx) / A;
			const double V = Y / B;
			const double Rest = FMath::Max(0.012, 1.0 - (U * U + V * V));
			const double Slope = H * K * FMath::Pow(Rest, K - 1.0);
			const double DzDx = Slope * (-2.0 * U / A);
			const double DzDy = Slope * (-2.0 * V / B);
			return FVector(-DzDx, -DzDy, 1.0).GetSafeNormal();
		}

		/** Punto de la cúpula levantado Lift a lo largo de su normal. */
		FVector At(double X, double Y, double Lift = 0.0) const
		{
			return FVector(X, Y, Height(X, Y)) + Normal(X, Y) * Lift;
		}

		/** Distancia con signo a una franja abierta hacia +X (desde X0, |Y| <= HalfY) con las esquinas de atrás redondeadas. */
		static double SlotSdf(double X, double Y, double X0, double HalfY, double Corner)
		{
			const double Px = (X0 + Corner) - X;
			const double Py = FMath::Abs(Y) - (HalfY - Corner);
			const double Outside = FVector2D(FMath::Max(Px, 0.0), FMath::Max(Py, 0.0)).Size();
			return Outside + FMath::Min(FMath::Max(Px, Py), 0.0) - Corner;
		}

		/** Distancia con signo a la escotadura con el hueco de la artillera (negativa dentro). */
		double NotchSdf(double X, double Y) const
		{
			return FMath::Min(SlotSdf(X, Y, NotchX, NotchHalfY, NotchCorner), SlotSdf(X, Y, WellX, WellHalfY, WellCorner));
		}

		bool InNotch(double X, double Y, double Margin) const { return NotchSdf(X, Y) < Margin; }

		/** Un punto dentro de la escotadura pasa a su borde (el más cercano); fuera, no se toca. */
		FVector2D PushOutOfNotch(const FVector2D& P) const
		{
			if (NotchSdf(P.X, P.Y) >= 0.0 || Edge.Num() < 2) { return P; }
			FVector2D Best = P;
			double BestD = TNumericLimits<double>::Max();
			for (int32 i = 0; i + 1 < Edge.Num(); ++i)
			{
				const FVector2D Q = FMath::ClosestPointOnSegment2D(P, Edge[i], Edge[i + 1]);
				const double D = FVector2D::DistSquared(P, Q);
				if (D < BestD) { BestD = D; Best = Q; }
			}
			return Best;
		}

		/** Prepara Edge: el borde de la escotadura con los dos extremos alargados más allá del morro de la cúpula. */
		void Finish()
		{
			Edge = NotchPath(2.0);
			Edge.Insert(FVector2D(Cx + A + 40.0, -NotchHalfY), 0);
			Edge.Add(FVector2D(Cx + A + 40.0, NotchHalfY));
		}

		FVector2D Ellipse(double Theta, double Scale) const
		{
			return FVector2D(Cx + A * Scale * FMath::Cos(Theta), B * Scale * FMath::Sin(Theta));
		}

		/** X donde el borde de la cúpula corta los lados de la escotadura. */
		double RimXAtNotchSide() const
		{
			const double V = NotchHalfY / B;
			return Cx + A * FMath::Sqrt(FMath::Max(0.0, 1.0 - V * V));
		}

		/**
		 * Borde de la escotadura de delante-izquierda a delante-derecha (XY), cada unos Step cm, rodeando el hueco de la
		 * artillera; el interior queda a la derecha del sentido de avance.
		 */
		TArray<FVector2D> NotchPath(double Step) const
		{
			TArray<FVector2D> Out;
			const double CornerX = NotchX + NotchCorner;
			const double CornerY = NotchHalfY - NotchCorner;
			const double WellCornerX = WellX + WellCorner;
			const double WellCornerY = WellHalfY - WellCorner;
			const double XRim = RimXAtNotchSide();
			const auto Line = [&Out, Step](const FVector2D& From, const FVector2D& To)
			{
				const int32 N = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(From, To) / Step));
				for (int32 i = 0; i < N; ++i) { Out.Add(FMath::Lerp(From, To, static_cast<double>(i) / N)); }
			};
			const auto Arc = [&Out](const FVector2D& C, double Radius, double From, double To)
			{
				constexpr int32 N = 6;
				for (int32 i = 0; i < N; ++i)
				{
					const double A0 = FMath::DegreesToRadians(FMath::Lerp(From, To, static_cast<double>(i) / N));
					Out.Add(C + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius);
				}
			};
			Line(FVector2D(XRim, -NotchHalfY), FVector2D(CornerX, -NotchHalfY));
			Arc(FVector2D(CornerX, -CornerY), NotchCorner, -90.0, -180.0);
			Line(FVector2D(NotchX, -CornerY), FVector2D(NotchX, -WellHalfY));
			Line(FVector2D(NotchX, -WellHalfY), FVector2D(WellCornerX, -WellHalfY));
			Arc(FVector2D(WellCornerX, -WellCornerY), WellCorner, -90.0, -180.0);
			Line(FVector2D(WellX, -WellCornerY), FVector2D(WellX, WellCornerY));
			Arc(FVector2D(WellCornerX, WellCornerY), WellCorner, 180.0, 90.0);
			Line(FVector2D(WellCornerX, WellHalfY), FVector2D(NotchX, WellHalfY));
			Line(FVector2D(NotchX, WellHalfY), FVector2D(NotchX, CornerY));
			Arc(FVector2D(CornerX, CornerY), NotchCorner, 180.0, 90.0);
			Line(FVector2D(CornerX, NotchHalfY), FVector2D(XRim, NotchHalfY));
			Out.Add(FVector2D(XRim, NotchHalfY));
			return Out;
		}

		/** Polígono convexo (antihorario) de la elipse a escala Scale, para recortar placas. */
		TArray<FVector2D> EllipsePoly(double Scale, int32 Sides = 40) const
		{
			TArray<FVector2D> Out;
			for (int32 i = 0; i < Sides; ++i) { Out.Add(Ellipse(2.0 * PI * i / Sides, Scale)); }
			return Out;
		}
	};

	FDome DomeFor(ETNBuggyBodyStyle Style)
	{
		FDome D;
		switch (Style)
		{
		case ETNBuggyBodyStyle::Offroad:
			// Más ancho y cuadrado, a la misma altura bajo el sillín de la artillera.
			D.Cx = -72.0; D.A = 148.0; D.B = 114.0; D.Z0 = 64.0; D.H = 42.0; D.K = 0.5;
			break;
		case ETNBuggyBodyStyle::Racer:
			// Bajo y largo: el sillín de la artillera va sobre una torreta.
			D.Cx = -66.0; D.A = 152.0; D.B = 108.0; D.Z0 = 58.0; D.H = 44.0; D.K = 0.75;
			break;
		default:
			break;
		}
		D.Finish();
		return D;
	}

	/** Recorta un polígono convexo por la elipse a escala Scale. */
	TArray<FVector2D> ClipToEllipse(TArray<FVector2D> Poly, const FDome& D, double Scale)
	{
		const TArray<FVector2D> E = D.EllipsePoly(Scale);
		for (int32 i = 0; i < E.Num() && Poly.Num() >= 3; ++i)
		{
			const FVector2D P0 = E[i];
			const FVector2D Edge = E[(i + 1) % E.Num()] - P0;
			Poly = ClipHalfPlane(Poly, P0, FVector2D(-Edge.Y, Edge.X));
		}
		return Poly;
	}

	/**
	 * Recorta una placa para que no entre en la escotadura ni en el hueco de la artillera: cada franja la deja atrás por
	 * el lado del que la placa queda más fuera (por detrás o por un costado).
	 */
	TArray<FVector2D> ClipOutOfNotch(TArray<FVector2D> Poly, const FDome& D, const FVector2D& Center, double Margin)
	{
		const auto ClipSlot = [&Poly, &Center, Margin](double X0, double HalfY)
		{
			if (Poly.Num() < 3) { return; }
			const double Behind = X0 - Center.X;
			const double Side = FMath::Abs(Center.Y) - HalfY;
			if (Behind >= Side)
			{
				Poly = ClipHalfPlane(Poly, FVector2D(X0 - Margin, 0.0), FVector2D(-1.0, 0.0));
			}
			else
			{
				const double Sy = Center.Y >= 0.0 ? 1.0 : -1.0;
				Poly = ClipHalfPlane(Poly, FVector2D(0.0, Sy * (HalfY + Margin)), FVector2D(0.0, Sy));
			}
		};
		ClipSlot(D.NotchX, D.NotchHalfY);
		ClipSlot(D.WellX, D.WellHalfY);
		return Poly;
	}

	/**
	 * Placa levantada sobre la cúpula: paredes con bisel y tapa en abanico que sigue la curva (Crown la abomba; con
	 * Crown grande es un pincho).
	 */
	void AddRaisedPlate(FTNProcMeshBuffers& B, const FDome& D, const TArray<FVector2D>& Poly, double Thick, double Bevel, double Crown,
		const FLinearColor& Top, const FLinearColor& Side, int32 Subdiv = 2)
	{
		TArray<FVector2D> TopPoly = Inset(Poly, Bevel);
		const FVector2D C = Centroid2D(Poly);
		if (TopPoly.Num() < 3)
		{
			for (const FVector2D& P : Poly) { TopPoly.Add(FMath::Lerp(C, P, 0.6)); }
		}
		const auto Subdivide = [Subdiv](const TArray<FVector2D>& P)
		{
			TArray<FVector2D> Out;
			for (int32 i = 0; i < P.Num(); ++i)
			{
				for (int32 s = 0; s < Subdiv; ++s) { Out.Add(FMath::Lerp(P[i], P[(i + 1) % P.Num()], static_cast<double>(s) / Subdiv)); }
			}
			return Out;
		};
		const TArray<FVector2D> BaseRing = Subdivide(Poly);
		const TArray<FVector2D> TopRing = Subdivide(TopPoly);
		TArray<FVector> Base3;
		TArray<FVector> Top3;
		for (const FVector2D& P : BaseRing) { Base3.Add(D.At(P.X, P.Y, 0.4)); }
		for (const FVector2D& P : TopRing) { Top3.Add(D.At(P.X, P.Y, Thick)); }
		const FVector2D TC = Centroid2D(TopPoly);
		const FVector Apex = D.At(TC.X, TC.Y, Thick + Crown);
		const FVector Ground = D.At(C.X, C.Y, 0.0);
		const FVector Nc = D.Normal(TC.X, TC.Y);
		const int32 N = Base3.Num();
		for (int32 k = 0; k < N; ++k)
		{
			const int32 K1 = (k + 1) % N;
			B.AddQuad(Base3[k], Base3[K1], Top3[K1], Top3[k], (Base3[k] + Top3[K1]) * 0.5 - Ground, Side);
			B.AddTri(Apex, Top3[k], Top3[K1], Nc, Top);
		}
	}

	/** Cúpula base (se ve en las juntas entre placas), faldón con labio y la escotadura con sus paredes y su acolchado. */
	void AddShellBase(FTNProcMeshBuffers& B, const FDome& D)
	{
		constexpr int32 NR = 12;
		constexpr int32 NS = 56;
		const auto Pt = [&D](double R, double Th)
		{
			const FVector2D P = D.PushOutOfNotch(D.Ellipse(Th, R));
			return FVector(P.X, P.Y, D.Height(P.X, P.Y));
		};
		for (int32 i = 0; i < NR; ++i)
		{
			const double R0 = FMath::Sin(HALF_PI * i / NR);
			const double R1 = FMath::Sin(HALF_PI * (i + 1) / NR);
			for (int32 j = 0; j < NS; ++j)
			{
				const double T0 = 2.0 * PI * j / NS;
				const double T1 = 2.0 * PI * (j + 1) / NS;
				const FVector2D Mid = D.Ellipse((T0 + T1) * 0.5, (R0 + R1) * 0.5);
				if (D.InNotch(Mid.X, Mid.Y, 0.0)) { continue; }
				B.AddQuad(Pt(R0, T0), Pt(R0, T1), Pt(R1, T1), Pt(R1, T0), D.Normal(Mid.X, Mid.Y), Base(0.58f));
			}
		}
		// Faldón: del borde sale un labio y baja hasta los bajos.
		constexpr double BottomZ = 44.0;
		for (int32 j = 0; j < NS; ++j)
		{
			const double T0 = 2.0 * PI * j / NS;
			const double T1 = 2.0 * PI * (j + 1) / NS;
			const FVector2D Mid = D.Ellipse((T0 + T1) * 0.5, 1.0);
			if (D.InNotch(Mid.X, Mid.Y, 0.0)) { continue; }
			const auto Ring = [&D](double Th, double Scale, double Z)
			{
				const FVector2D P = D.PushOutOfNotch(D.Ellipse(Th, Scale));
				return FVector(P.X, P.Y, Z);
			};
			const FVector Out = FVector(Mid.X - D.Cx, Mid.Y, 0.0).GetSafeNormal();
			const FVector Rim0 = Ring(T0, 1.0, D.Z0);
			const FVector Rim1 = Ring(T1, 1.0, D.Z0);
			const FVector Lip0 = Ring(T0, 1.04, D.Z0 - 3.0);
			const FVector Lip1 = Ring(T1, 1.04, D.Z0 - 3.0);
			const FVector Bot0 = Ring(T0, 0.975, BottomZ);
			const FVector Bot1 = Ring(T1, 0.975, BottomZ);
			B.AddQuad(Rim0, Rim1, Lip1, Lip0, Out + FVector::UpVector, Base(0.82f));
			B.AddQuad(Lip0, Lip1, Bot1, Bot0, Out - FVector::UpVector * 0.3, Base(0.68f));
		}
		// Escotadura: paredes hasta el suelo de la bañera (en el hueco de la artillera, hasta su reposapiés) y acolchado de
		// cuero en el borde.
		const TArray<FVector2D> Path = D.NotchPath(7.0);
		TArray<FVector> Coaming;
		for (int32 k = 0; k < Path.Num(); ++k)
		{
			const FVector Top(Path[k].X, Path[k].Y, D.Height(Path[k].X, Path[k].Y));
			Coaming.Add(Top + FVector(0.0, 0.0, 2.5));
			if (k + 1 < Path.Num())
			{
				const FVector Top1(Path[k + 1].X, Path[k + 1].Y, D.Height(Path[k + 1].X, Path[k + 1].Y));
				const FVector2D Dir = (Path[k + 1] - Path[k]).GetSafeNormal();
				const double Bottom = (Path[k].X + Path[k + 1].X) * 0.5 < D.NotchX - 0.5 ? Frame::GunnerFloorZ : FloorZ;
				B.AddQuad(Top, Top1, FVector(Top1.X, Top1.Y, Bottom), FVector(Top.X, Top.Y, Bottom), FVector(Dir.Y, -Dir.X, 0.0), Base(0.5f));
			}
		}
		AddTube(B, Coaming, 5.0, 8, Matte(0x5C3B24));
	}

	/** Placas hexagonales (Clásico) o con pincho (Caimán) en panal, más la fila de placas marginales del borde. */
	void AddHexPlates(FTNProcMeshBuffers& B, const FDome& D, bool bSpiky)
	{
		constexpr double Rh = 24.0;
		const double DX = 1.5 * Rh;
		const double DY = FMath::Sqrt(3.0) * Rh;
		constexpr double Gap = 2.6;
		for (int32 i = -8; i <= 5; ++i)
		{
			for (int32 j = -4; j <= 4; ++j)
			{
				const double Xc = Frame::GunnerHip.X + i * DX;
				const double Yc = j * DY + ((i & 1) ? DY * 0.5 : 0.0);
				if (D.R2(Xc, Yc) > 0.80 * 0.80 || D.InNotch(Xc, Yc, 10.0)) { continue; }
				// Bajo el sillín de la artillera no hay placa: lo tapa el cojín.
				if (FVector2D::Distance(FVector2D(Xc, Yc), FVector2D(Frame::GunnerHip.X, Frame::GunnerHip.Y)) < 22.0) { continue; }
				TArray<FVector2D> Poly;
				for (int32 k = 0; k < 6; ++k)
				{
					const double A = FMath::DegreesToRadians(60.0 * k);
					Poly.Add(FVector2D(Xc + Rh * FMath::Cos(A), Yc + Rh * FMath::Sin(A)));
				}
				Poly = ClipToEllipse(Poly, D, 0.86);
				Poly = ClipOutOfNotch(Poly, D, FVector2D(Xc, Yc), 3.0);
				if (Poly.Num() < 3) { continue; }
				MakeCCW(Poly);
				Poly = Inset(Poly, Gap);
				if (Poly.Num() < 3 || SignedArea(Poly) < 140.0) { continue; }
				const float Tone = 0.86f + 0.14f * Hash01(i, j);
				double Crown = 2.2;
				if (bSpiky)
				{
					// Tortuga caimán: tres quillas de pinchos; lisa bajo la artillera y el aro de su torreta.
					const bool bKeel = FMath::Abs(Yc) < 10.0 || FMath::Abs(FMath::Abs(Yc) - DY) < 10.0;
					const bool bUnderGunner = FVector2D::Distance(FVector2D(Xc, Yc), FVector2D(Frame::GunnerHip.X, Frame::GunnerHip.Y)) < 60.0;
					Crown = bUnderGunner ? 1.5 : (bKeel ? 17.0 : 9.0);
				}
				AddRaisedPlate(B, D, Poly, 4.5, 3.0, Crown, Plate(Tone), Plate(Tone * 0.72f), 2);
			}
		}
		// Marginales: anillo de placas entre 0,885 y 0,985 del radio.
		constexpr int32 NM = 28;
		for (int32 m = 0; m < NM; ++m)
		{
			const double T0 = 2.0 * PI * (m + 0.04) / NM;
			const double T1 = 2.0 * PI * (m + 0.96) / NM;
			TArray<FVector2D> Poly = { D.Ellipse(T0, 0.885), D.Ellipse(T1, 0.885), D.Ellipse(T1, 0.985), D.Ellipse(T0, 0.985) };
			bool bInNotch = false;
			for (const FVector2D& P : Poly) { bInNotch |= D.InNotch(P.X, P.Y, 5.0); }
			if (bInNotch) { continue; }
			MakeCCW(Poly);
			Poly = Inset(Poly, 1.6);
			if (Poly.Num() < 3) { continue; }
			const float Tone = (m % 2) ? 0.8f : 0.9f;
			AddRaisedPlate(B, D, Poly, 3.5, 2.0, bSpiky ? 4.0 : 1.0, Plate(Tone), Plate(Tone * 0.72f), 2);
		}
	}

	/** Tortuga laúd: siete crestas a lo largo del caparazón, cortadas por la cabina y por la torreta de la artillera. */
	void AddRidges(FTNProcMeshBuffers& B, const FDome& D)
	{
		const double Ys[] = { 0.0, 30.0, -30.0, 58.0, -58.0, 84.0, -84.0 };
		for (const double Y0 : Ys)
		{
			const double Height = Y0 == 0.0 ? 8.0 : 5.5;
			constexpr double Half = 5.0;
			TArray<FVector> L;
			TArray<FVector> T;
			TArray<FVector> R;
			const auto Emit = [&B](const TArray<FVector>& Ls, const TArray<FVector>& Ts, const TArray<FVector>& Rs)
			{
				for (int32 k = 0; k + 1 < Ts.Num(); ++k)
				{
					const FVector Up = (Ts[k] - (Ls[k] + Rs[k]) * 0.5).GetSafeNormal();
					B.AddQuad(Ls[k], Ls[k + 1], Ts[k + 1], Ts[k], Up + (Ls[k] - Rs[k]).GetSafeNormal(), Plate(1.f));
					B.AddQuad(Ts[k], Ts[k + 1], Rs[k + 1], Rs[k], Up + (Rs[k] - Ls[k]).GetSafeNormal(), Plate(0.8f));
				}
				if (Ts.Num() >= 2)
				{
					B.AddTri(Ls[0], Ts[0], Rs[0], Ts[0] - Ts[1], Plate(0.7f));
					const int32 E = Ts.Num() - 1;
					B.AddTri(Ls[E], Ts[E], Rs[E], Ts[E] - Ts[E - 1], Plate(0.7f));
				}
			};
			for (double X = D.Cx - D.A; X <= D.Cx + D.A; X += 8.0)
			{
				const bool bOff = D.R2(X, Y0) > 0.95 * 0.95 || D.InNotch(X, Y0, 8.0)
					|| FVector2D::Distance(FVector2D(X, Y0), FVector2D(Frame::GunnerHip.X, Frame::GunnerHip.Y)) < 36.0;
				if (bOff)
				{
					Emit(L, T, R);
					L.Reset(); T.Reset(); R.Reset();
					continue;
				}
				L.Add(D.At(X, Y0 - Half, 0.3));
				T.Add(D.At(X, Y0, Height));
				R.Add(D.At(X, Y0 + Half, 0.3));
			}
			Emit(L, T, R);
		}
	}

	/**
	 * Sillín y respaldo de la artillera a la altura del de serie (en el bólido, sobre su torreta). El sillín baja hasta el
	 * reposapiés: por delante es el frente del asiento.
	 */
	void AddGunnerSaddle(FTNProcMeshBuffers& B, const FDome& D, bool bPod)
	{
		const FVector Hip = Frame::GunnerHip;
		const double Top = Frame::GunnerCushionZ;
		const double DomeZ = D.Height(Hip.X, Hip.Y);
		const double Bottom = Frame::GunnerFloorZ - 1.0;
		if (bPod)
		{
			AddLathe(B, FVector(Hip.X, Hip.Y, Bottom), FVector::UpVector,
				{ FVector2D(0.0, 27.0), FVector2D(DomeZ - Bottom + 2.0, 26.5), FVector2D(Top - Bottom - 8.0, 26.0) }, 14, Metal(0x3A3F47), false, true);
			AddLathe(B, FVector(Hip.X, Hip.Y, DomeZ + 3.0), FVector::UpVector, { FVector2D(0.0, 27.4), FVector2D(3.0, 27.4) }, 14, Skin(0.85f), false, false);
		}
		AddLathe(B, FVector(Hip.X, Hip.Y, Bottom), FVector::UpVector,
			{ FVector2D(0.0, 24.0), FVector2D(Top - Bottom - 3.0, 25.0), FVector2D(Top - Bottom + 0.2, 22.0) }, 14, Matte(0xE8D2A8), false, true);
		AddLathe(B, FVector(Hip.X, Hip.Y, Top - 6.5), FVector::UpVector, { FVector2D(0.0, 25.4), FVector2D(2.2, 25.4) }, 14, Matte(0x7A4A2A), false, false);
		// Respaldo detrás del caparazón de la tortuga sentada, por dentro del carro de la torreta, y su soporte.
		const FVector Up = FVector(-0.22, 0.0, 1.0).GetSafeNormal();
		const FVector Fwd = FVector(1.0, 0.0, 0.22).GetSafeNormal();
		AddOBox(B, FVector(Hip.X - 25.5, Hip.Y, Top + 14.0), Fwd * 3.5, FVector(0.0, 21.0, 0.0), Up * 14.5, Matte(0xE8D2A8));
		AddOBox(B, FVector(Hip.X - 30.0, Hip.Y, Top + 2.0), Fwd * 1.6, FVector(0.0, 5.0, 0.0), Up * 13.0, Matte(0x7A4A2A));
	}

	FTNProcMeshBuffers BuildShell(ETNBuggyBodyStyle Style)
	{
		FTNProcMeshBuffers B;
		const FDome D = DomeFor(Style);
		AddShellBase(B, D);
		if (Style == ETNBuggyBodyStyle::Racer)
		{
			AddRidges(B, D);
		}
		else
		{
			AddHexPlates(B, D, Style == ETNBuggyBodyStyle::Offroad);
		}
		AddGunnerSaddle(B, D, Style == ETNBuggyBodyStyle::Racer);
		return B;
	}

	// ─────────────────────────────────────────────────────────────────────────────
	// Cabina: bañera delante del caparazón, asiento, timón, salpicadero y caja de cocos
	// ─────────────────────────────────────────────────────────────────────────────

	/** Borde de la bañera: sale a la altura del borde del caparazón y sube hacia el cuello. */
	double SillZ(const FDome& D, double X)
	{
		const double Start = D.Z0 + 2.0;
		return Start + (80.0 - Start) * Smooth(D.RimXAtNotchSide(), 100.0, X);
	}

	double TubHalfY(double X, double StartX)
	{
		return 74.0 + 4.0 * FMath::Sin(PI * FMath::Clamp((X - StartX) / (122.0 - StartX), 0.0, 1.0));
	}

	/** Timón de barco como volante, donde el de serie tiene el suyo, mirando a la conductora. */
	void AddHelm(FTNProcMeshBuffers& B)
	{
		const FVector Hub = Frame::Helm;
		// Columna hacia el salpicadero, como la de build_buggy.py: el timón queda perpendicular a ella.
		const FVector ColumnBase(97.0, 0.0, 98.0);
		const FVector Axis = (Hub - ColumnBase).GetSafeNormal();
		const FVector U = FVector(0.0, 1.0, 0.0);
		const FVector V = FVector::CrossProduct(Axis, U).GetSafeNormal();
		constexpr double Ring = 16.0;
		TArray<FVector> RingPts;
		for (int32 s = 0; s <= 16; ++s)
		{
			const double A = 2.0 * PI * s / 16.0;
			RingPts.Add(Hub + (U * FMath::Cos(A) + V * FMath::Sin(A)) * Ring);
		}
		AddTube(B, RingPts, 2.0, 6, Matte(0x9A5B2B), false, false);
		for (int32 k = 0; k < 8; ++k)
		{
			const double A = 2.0 * PI * (k + 0.5) / 8.0;
			const FVector Dir = U * FMath::Cos(A) + V * FMath::Sin(A);
			B.AddBeam(Hub, Hub + Dir * Ring, 1.1, Matte(0x7E4A22));
			AddTube(B, { Hub + Dir * (Ring + 1.0), Hub + Dir * (Ring + 6.5) }, { 1.9, 1.5 }, 6, Matte(0x9A5B2B));
		}
		AddLathe(B, Hub - Axis * 2.0, Axis, { FVector2D(0.0, 4.5), FVector2D(3.5, 4.0), FVector2D(4.5, 2.0) }, 10, Metal(0xD4A84A));
		B.AddBeam(Hub + Axis * 1.0, ColumnBase, 2.0, Metal(0x3A3F47));
	}

	void AddCoconuts(FTNProcMeshBuffers& B, const FVector& At)
	{
		AddOBox(B, At, FVector(17.0, 0.0, 0.0), FVector(0.0, 17.0, 0.0), FVector(0.0, 0.0, 10.0), Matte(0x9C6B3C));
		for (int32 k = 0; k < 2; ++k)
		{
			const double Off = k == 0 ? -6.0 : 6.0;
			AddOBox(B, At + FVector(17.4, 0.0, Off), FVector(0.4, 0.0, 0.0), FVector(0.0, 16.0, 0.0), FVector(0.0, 0.0, 1.6), Matte(0x6E4A28));
			AddOBox(B, At + FVector(0.0, 17.4, Off), FVector(16.0, 0.0, 0.0), FVector(0.0, 0.4, 0.0), FVector(0.0, 0.0, 1.6), Matte(0x6E4A28));
		}
		const FVector Nuts[] = { FVector(-6.0, -5.0, 16.0), FVector(6.0, 6.0, 16.0), FVector(-2.0, 7.0, 24.0) };
		for (const FVector& N : Nuts)
		{
			AddEllipsoid(B, At + N, FVector(7.0, 0.0, 0.0), FVector(0.0, 7.0, 0.0), FVector(0.0, 0.0, 6.5), 5, 8, Matte(0x5B3A1E), 0.7f);
		}
	}

	FTNProcMeshBuffers BuildCockpit(ETNBuggyBodyStyle Style)
	{
		FTNProcMeshBuffers B;
		const FDome D = DomeFor(Style);
		const double StartX = D.RimXAtNotchSide() - 8.0;
		constexpr double EndX = 122.0;
		// Paredes de la bañera (fuera pintura, dentro más oscura) con su borde acolchado, que sigue al del caparazón.
		for (const double Side : { -1.0, 1.0 })
		{
			TArray<FVector> Sill;
			for (double X = StartX; X < EndX; X += 7.0)
			{
				const double X1 = FMath::Min(X + 7.0, EndX);
				const double Y0 = TubHalfY(X, StartX) * Side;
				const double Y1 = TubHalfY(X1, StartX) * Side;
				const double Z0 = SillZ(D, X);
				const double Z1 = SillZ(D, X1);
				const FVector Out(0.0, Side, 0.0);
				B.AddQuad(FVector(X, Y0, 46.0), FVector(X1, Y1, 46.0), FVector(X1, Y1, Z1), FVector(X, Y0, Z0), Out, Base(0.9f));
				const double In0 = Y0 - Side * 4.0;
				const double In1 = Y1 - Side * 4.0;
				B.AddQuad(FVector(X, In0, FloorZ), FVector(X1, In1, FloorZ), FVector(X1, In1, Z1), FVector(X, In0, Z0), -Out, Base(0.5f));
				B.AddQuad(FVector(X, Y0, Z0), FVector(X1, Y1, Z1), FVector(X1, In1, Z1), FVector(X, In0, Z0), FVector::UpVector, Base(0.8f));
				Sill.Add(FVector(X, Y0 - Side * 2.0, Z0 + 2.5));
			}
			Sill.Add(FVector(EndX, TubHalfY(EndX, StartX) * Side - Side * 2.0, SillZ(D, EndX) + 2.5));
			AddTube(B, Sill, 4.5, 8, Matte(0x5C3B24));
		}
		// Suelo de goma.
		for (double X = D.NotchX - 2.0; X < EndX; X += 16.0)
		{
			const double X1 = FMath::Min(X + 16.0, EndX);
			B.AddQuad(FVector(X, -72.0, FloorZ), FVector(X1, -72.0, FloorZ), FVector(X1, 72.0, FloorZ), FVector(X, 72.0, FloorZ), FVector::UpVector,
				Matte(FMath::RoundToInt(X / 16.0) % 2 ? 0x30353D : 0x363C45));
		}
		// Asiento de la conductora como el de serie: pedestal, cojín bajo la cadera, respaldo detrás de su caparazón y orejeras.
		const FVector Hip = Frame::DriverHip;
		const double Cushion = Frame::DriverCushionZ;
		const double BackX = Hip.X - 20.7;
		const double PedestalTop = Cushion - 10.0;
		AddOBox(B, FVector(Hip.X, 0.0, (FloorZ + PedestalTop) * 0.5), FVector(15.0, 0.0, 0.0), FVector(0.0, 17.0, 0.0),
			FVector(0.0, 0.0, (PedestalTop - FloorZ) * 0.5), Metal(0x3A3F47));
		AddOBox(B, FVector((BackX + Hip.X + 22.0) * 0.5, 0.0, Cushion - 5.0), FVector((Hip.X + 22.0 - BackX) * 0.5, 0.0, 0.0), FVector(0.0, 24.0, 0.0),
			FVector(0.0, 0.0, 5.0), Matte(0xEAD7B0));
		const FVector BackUp = FVector(-0.18, 0.0, 1.0).GetSafeNormal();
		const FVector BackFwd = FVector(1.0, 0.0, 0.18).GetSafeNormal();
		AddOBox(B, FVector(BackX - 4.5, 0.0, Cushion + 16.0), BackFwd * 4.0, FVector(0.0, 24.0, 0.0), BackUp * 21.0, Matte(0xEAD7B0));
		for (const double S : { -1.0, 1.0 })
		{
			AddOBox(B, FVector(BackX - 1.0, S * 25.0, Cushion + 10.0), BackFwd * 6.0, FVector(0.0, 3.0, 0.0), BackUp * 14.0, Matte(0xC9A97A));
		}
		AddHelm(B);
		// Salpicadero con dos relojes, delante del timón.
		AddOBox(B, FVector(102.0, 0.0, 93.0), FVector(5.0, 0.0, 0.0), FVector(0.0, 60.0, 0.0), FVector(0.0, 0.0, 8.0), Matte(0x2B2F36));
		for (const double Y : { -38.0, 38.0 })
		{
			AddDisc(B, FVector(96.8, Y, 95.0), FVector(-1.0, 0.0, 0.0), 6.0, 12, Metal(0xC9CED6));
			AddDisc(B, FVector(96.6, Y, 95.0), FVector(-1.0, 0.0, 0.0), 4.8, 12, Light(0xFFF2D4));
			B.AddBeam(FVector(96.4, Y, 95.0), FVector(96.4, Y + 3.0, 98.0), 0.5, Matte(0xD9432F));
		}
		// Munición de cocos al lado del asiento.
		AddCoconuts(B, FVector(30.0, 46.0, FloorZ + 10.0));
		// Reposapiés de la artillera: el suelo del hueco del caparazón, con su frente hacia la conductora.
		const double FootZ = Frame::GunnerFloorZ;
		const double FootY = D.WellHalfY + 2.0;
		B.AddQuad(FVector(D.WellX - 2.0, -FootY, FootZ), FVector(D.NotchX, -FootY, FootZ), FVector(D.NotchX, FootY, FootZ), FVector(D.WellX - 2.0, FootY, FootZ),
			FVector::UpVector, Matte(0x363C45));
		B.AddQuad(FVector(D.NotchX, -FootY, FloorZ), FVector(D.NotchX, FootY, FloorZ), FVector(D.NotchX, FootY, FootZ), FVector(D.NotchX, -FootY, FootZ),
			FVector(1.0, 0.0, 0.0), Base(0.55f));
		for (const double X : { D.WellX + 14.0, D.WellX + 30.0 })
		{
			AddOBox(B, FVector(X, 0.0, FootZ + 0.8), FVector(4.0, 0.0, 0.0), FVector(0.0, D.WellHalfY - 4.0, 0.0), FVector(0.0, 0.0, 0.8), Matte(0x2A2F36));
		}
		// Barandillas de la artillera, como las del de serie: el aro de la torreta se sujeta a ellas. Postes de delante desde
		// el suelo de la bañera, de detrás desde el lomo, travesaño detrás de la cabeza de la conductora y pomos de latón.
		const FLinearColor Brass = Metal(0xC9A04A);
		constexpr double RailR = 2.8;
		for (const double S : { -1.0, 1.0 })
		{
			const double Y = Frame::RailY * S;
			AddTube(B, { FVector(Frame::RailFrontX, Y, Frame::RailZ), FVector(Frame::RailBackX, Y, Frame::RailZ) }, RailR, 8, Brass);
			AddTube(B, { FVector(Frame::RailFrontX, Y, FloorZ), FVector(Frame::RailFrontX, Y, Frame::RailZ) }, RailR, 8, Brass);
			AddTube(B, { FVector(Frame::RailBackX, Y, D.Height(Frame::RailBackX, Y) - 2.0), FVector(Frame::RailBackX, Y, Frame::RailZ) }, RailR, 8, Brass);
			for (const double X : { Frame::RailFrontX, Frame::RailBackX })
			{
				AddEllipsoid(B, FVector(X, Y, Frame::RailZ + 3.0), FVector(4.2, 0.0, 0.0), FVector(0.0, 4.2, 0.0), FVector(0.0, 0.0, 4.2), 4, 8, Brass);
			}
		}
		AddTube(B, { FVector(Frame::RailFrontX, -Frame::RailY, Frame::RailZ), FVector(Frame::RailFrontX, Frame::RailY, Frame::RailZ) }, RailR, 8, Brass);
		return B;
	}

	// ─────────────────────────────────────────────────────────────────────────────
	// Cabeza: cuello, cabeza, ojos-faro (iris del color del equipo), párpados, boca y orificios
	// ─────────────────────────────────────────────────────────────────────────────

	struct FHeadSpec
	{
		FVector NeckC;
		FVector NeckAx;
		FVector HeadC;
		FVector HeadAx;
		int32 Segs = 16;
		/** Ojo izquierdo (Y < 0); el derecho es el espejo. */
		FVector EyeC;
		double EyeR = 17.0;
		/** Eje del párpado del ojo izquierdo y ángulo que tapa (grados). */
		FVector LidAxis;
		double LidAngle = 55.0;
	};

	FHeadSpec HeadFor(ETNBuggyBodyStyle Style)
	{
		FHeadSpec S;
		switch (Style)
		{
		case ETNBuggyBodyStyle::Offroad:
			S.NeckC = FVector(126.0, 0.0, 76.0); S.NeckAx = FVector(44.0, 46.0, 30.0);
			S.HeadC = FVector(182.0, 0.0, 90.0); S.HeadAx = FVector(62.0, 54.0, 38.0); S.Segs = 10;
			S.EyeC = FVector(214.0, -29.0, 116.0); S.EyeR = 16.0;
			// Ceño: el párpado tapa más por dentro (hacia la nariz).
			S.LidAxis = FVector(0.35, 0.55, 1.0); S.LidAngle = 64.0;
			break;
		case ETNBuggyBodyStyle::Racer:
			S.NeckC = FVector(128.0, 0.0, 68.0); S.NeckAx = FVector(44.0, 40.0, 20.0);
			S.HeadC = FVector(186.0, 0.0, 74.0); S.HeadAx = FVector(64.0, 46.0, 26.0); S.Segs = 16;
			S.EyeC = FVector(216.0, -23.0, 94.0); S.EyeR = 14.0;
			// Mirada de concentración: párpados a media asta, inclinados hacia dentro.
			S.LidAxis = FVector(0.45, 0.3, 1.0); S.LidAngle = 56.0;
			break;
		default:
			S.NeckC = FVector(124.0, 0.0, 72.0); S.NeckAx = FVector(42.0, 42.0, 26.0);
			S.HeadC = FVector(180.0, 0.0, 86.0); S.HeadAx = FVector(60.0, 50.0, 37.0); S.Segs = 16;
			S.EyeC = FVector(212.0, -27.0, 113.0); S.EyeR = 18.5;
			S.LidAxis = FVector(-0.2, 0.0, 1.0); S.LidAngle = 60.0;
			break;
		}
		return S;
	}

	/** Punto de la superficie de la cabeza de frente (X positiva) a la altura (Y, Z). */
	FVector HeadFront(const FHeadSpec& S, double Y, double Z, double Out = 0.6)
	{
		const double Ty = Y / S.HeadAx.Y;
		const double Tz = (Z - S.HeadC.Z) / S.HeadAx.Z;
		const double X = S.HeadC.X + S.HeadAx.X * FMath::Sqrt(FMath::Max(0.0, 1.0 - Ty * Ty - Tz * Tz));
		return FVector(X + Out, Y, Z);
	}

	/** Ojo de dibujo: esclerótica que brilla (es el faro), iris del equipo, pupila, brillo y párpado de la piel. */
	void AddEye(FTNProcMeshBuffers& B, const FVector& C, double R, const FVector& Look, const FVector& LidAxis, double LidAngle)
	{
		AddEllipsoid(B, C, FVector(R, 0.0, 0.0), FVector(0.0, R, 0.0), FVector(0.0, 0.0, R), 8, 14, Light(0xFFFFFF));
		AddSphereCap(B, C, R * 1.012, Look, 33.0, 2, 14, Team(1.f));
		AddSphereCap(B, C, R * 1.024, Look, 18.0, 2, 12, Matte(0x0D0D12));
		const FVector Up = FVector::UpVector;
		const FVector Glint = (Look + Up * 0.45 + FVector(0.0, -0.25, 0.0)).GetSafeNormal();
		AddSphereCap(B, C, R * 1.036, Glint, 7.0, 1, 8, Light(0xFFFFFF));
		AddSphereCap(B, C, R * 1.09, LidAxis, LidAngle, 3, 16, Skin(0.88f));
	}

	FTNProcMeshBuffers BuildHead(ETNBuggyBodyStyle Style)
	{
		FTNProcMeshBuffers B;
		const FHeadSpec S = HeadFor(Style);
		AddEllipsoid(B, S.NeckC, FVector(S.NeckAx.X, 0.0, 0.0), FVector(0.0, S.NeckAx.Y, 0.0), FVector(0.0, 0.0, S.NeckAx.Z), 7, S.Segs,
			Skin(0.92f), 0.6f);
		AddEllipsoid(B, S.HeadC, FVector(S.HeadAx.X, 0.0, 0.0), FVector(0.0, S.HeadAx.Y, 0.0), FVector(0.0, 0.0, S.HeadAx.Z), 8, S.Segs,
			Skin(1.f), 0.62f);
		for (const double Side : { -1.0, 1.0 })
		{
			const FVector EyeCenter(S.EyeC.X, FMath::Abs(S.EyeC.Y) * Side, S.EyeC.Z);
			const FVector Look = FVector(1.0, 0.22 * Side, 0.04).GetSafeNormal();
			// El eje del párpado del ojo izquierdo apunta hacia la nariz con Y positiva: en el derecho, al revés.
			const FVector Lid = FVector(S.LidAxis.X, -S.LidAxis.Y * Side, S.LidAxis.Z).GetSafeNormal();
			AddEye(B, EyeCenter, S.EyeR, Look, Lid, S.LidAngle);
		}
		// Boca: sonrisa (Clásico), mueca con colmillos (Caimán) o media sonrisa chulesca (Bólido).
		const double MouthZ = S.HeadC.Z - S.HeadAx.Z * 0.32;
		TArray<FVector> Mouth;
		for (int32 k = 0; k <= 10; ++k)
		{
			const double T = -1.0 + 2.0 * k / 10.0;
			const double Y = T * S.HeadAx.Y * 0.56;
			double Z = MouthZ;
			if (Style == ETNBuggyBodyStyle::Classic) { Z -= 9.0 * (1.0 - T * T); }
			else if (Style == ETNBuggyBodyStyle::Racer) { Z += 3.0 * T - 4.0 * (1.0 - T * T); }
			else { Z -= 2.0 * (1.0 - T * T); }
			Mouth.Add(HeadFront(S, Y, Z, 0.8));
		}
		AddTube(B, Mouth, 2.2, 6, Matte(0x2A140C));
		if (Style == ETNBuggyBodyStyle::Offroad)
		{
			for (const double Side : { -1.0, 1.0 })
			{
				const FVector Root = HeadFront(S, Side * 14.0, MouthZ - 2.0, 0.5);
				B.AddTri(Root + FVector(0.0, -3.5, 0.0), Root + FVector(0.0, 3.5, 0.0), Root + FVector(1.5, 0.0, -8.0), FVector(1.0, 0.0, 0.0), Matte(0xFFF8E7));
			}
		}
		// Orificios de la nariz.
		for (const double Side : { -1.0, 1.0 })
		{
			const FVector N = HeadFront(S, Side * 8.0, S.HeadC.Z + S.HeadAx.Z * 0.12, 0.2);
			AddEllipsoid(B, N, FVector(1.4, 0.0, 0.0), FVector(0.0, 2.6, 0.0), FVector(0.0, 0.0, 2.0), 3, 6, Matte(0x1E120B));
		}
		return B;
	}

	// ─────────────────────────────────────────────────────────────────────────────
	// Aletas: guardabarros en arco sobre cada rueda, con la punta de aleta delante (delanteras) o detrás (traseras)
	// ─────────────────────────────────────────────────────────────────────────────

	struct FFenderSpec
	{
		FVector Wheel;
		double Radius = 72.0;
		/** Ángulo del arco (grados: 0 delante, 90 arriba, 180 detrás) en el principio y el final. */
		double A0 = 10.0;
		double A1 = 172.0;
		double YIn[5] = { 0, 0, 0, 0, 0 };
		double YOut[5] = { 0, 0, 0, 0, 0 };
		double Thick = 5.0;
		double Bulge = 3.0;
		/** Cuánto baja el borde de dentro hacia la carrocería en el centro del arco (la aleta sale del cuerpo). */
		double Drop = 0.0;
		/** El paso de rueda (pared de dentro) solo donde el borde queda por dentro de la rueda. */
		double WellMaxY = 100.0;
	};

	/** Punto de la aleta en la estación T: centro del arco, dirección radial y caída del borde de dentro. */
	void FenderStation(const FFenderSpec& F, double T, FVector& OutCenter, FVector& OutDir, double& OutDrop)
	{
		const double A = FMath::DegreesToRadians(FMath::Lerp(F.A0, F.A1, T));
		OutDir = FVector(FMath::Cos(A), 0.0, FMath::Sin(A));
		OutCenter = FVector(F.Wheel.X, 0.0, F.Wheel.Z) + OutDir * F.Radius;
		OutDrop = F.Drop * FMath::Pow(FMath::Max(0.0, FMath::Sin(PI * T)), 0.7);
	}

	void AddFender(FTNProcMeshBuffers& B, const FFenderSpec& F, double Side)
	{
		constexpr int32 N = 18;
		TArray<TArray<FVector>> Rings;
		TArray<FVector> WellTop;
		TArray<bool> WellOn;
		const FVector Y(0.0, Side, 0.0);
		for (int32 s = 0; s <= N; ++s)
		{
			const double T = static_cast<double>(s) / N;
			FVector C;
			FVector Dir;
			double Drop = 0.0;
			FenderStation(F, T, C, Dir, Drop);
			const double In = Table5(F.YIn, T);
			const double Out = FMath::Max(Table5(F.YOut, T), In + 1.0);
			// Sección de aleta: el borde de dentro cae hacia el cuerpo, la cresta va un poco hacia fuera y el de fuera es fino.
			Rings.Add({ C + Y * In - Dir * Drop, C + Y * Out, C + Y * (Out - 2.0) + Dir * (F.Thick * 0.7),
				C + Y * ((In + Out) * 0.5 + 6.0) + Dir * (F.Thick + F.Bulge - Drop * 0.35), C + Y * In + Dir * (F.Thick - Drop) });
			WellTop.Add(C + Y * In - Dir * Drop);
			WellOn.Add(In <= F.WellMaxY && WellTop.Last().Z > 52.0);
		}
		AddLoft(B, Rings, true, [](int32, int32 Edge)
		{
			switch (Edge)
			{
			case 0: return Skin(0.5f);
			case 3: return Skin(0.88f);
			case 4: return Skin(0.62f);
			default: return Skin(1.f);
			}
		}, true, true, Skin(0.7f));
		// Paso de rueda: pared oscura de la aleta al chasis por dentro de la rueda.
		for (int32 s = 0; s + 1 < WellTop.Num(); ++s)
		{
			if (!WellOn[s] || !WellOn[s + 1]) { continue; }
			const FVector A = WellTop[s];
			const FVector Bv = WellTop[s + 1];
			B.AddQuad(A, Bv, FVector(Bv.X, Bv.Y, 46.0), FVector(A.X, A.Y, 46.0), Y, Skin(0.4f));
		}
	}

	void FendersFor(ETNBuggyBodyStyle Style, FFenderSpec& Front, FFenderSpec& Rear)
	{
		Front.Wheel = Frame::FrontWheel;
		Rear.Wheel = Frame::RearWheel;
		const auto Set = [](double (&Dst)[5], std::initializer_list<double> V) { int32 i = 0; for (const double X : V) { Dst[i++] = X; } };
		switch (Style)
		{
		case ETNBuggyBodyStyle::Offroad:
			// Aletas gruesas y anchas, más altas (suspensión de todoterreno).
			Front.Radius = 68.0; Front.A0 = 16.0; Front.A1 = 160.0; Front.Thick = 8.0; Front.Bulge = 3.0; Front.Drop = 18.0;
			Set(Front.YIn, { 120.0, 48.0, 44.0, 58.0, 76.0 }); Set(Front.YOut, { 142.0, 154.0, 158.0, 152.0, 122.0 });
			Rear.Radius = 68.0; Rear.A0 = 20.0; Rear.A1 = 165.0; Rear.Thick = 8.0; Rear.Bulge = 3.0; Rear.Drop = 26.0; Rear.WellMaxY = 116.0;
			Set(Rear.YIn, { 96.0, 86.0, 84.0, 88.0, 114.0 }); Set(Rear.YOut, { 154.0, 170.0, 174.0, 170.0, 156.0 });
			break;
		case ETNBuggyBodyStyle::Racer:
			// Aletas finas y pegadas a la rueda.
			Front.Radius = 60.0; Front.A0 = 10.0; Front.A1 = 165.0; Front.Thick = 4.0; Front.Bulge = 1.5; Front.Drop = 14.0;
			Set(Front.YIn, { 128.0, 50.0, 42.0, 54.0, 76.0 }); Set(Front.YOut, { 140.0, 150.0, 152.0, 148.0, 116.0 });
			Rear.Radius = 60.0; Rear.A0 = 20.0; Rear.A1 = 168.0; Rear.Thick = 4.0; Rear.Bulge = 1.5; Rear.Drop = 24.0; Rear.WellMaxY = 116.0;
			Set(Rear.YIn, { 96.0, 86.0, 84.0, 90.0, 120.0 }); Set(Rear.YOut, { 146.0, 162.0, 164.0, 160.0, 146.0 });
			break;
		default:
			Front.Radius = 64.0; Front.A0 = 14.0; Front.A1 = 162.0; Front.Thick = 6.0; Front.Bulge = 4.0; Front.Drop = 18.0;
			Set(Front.YIn, { 120.0, 46.0, 42.0, 56.0, 74.0 }); Set(Front.YOut, { 140.0, 152.0, 156.0, 152.0, 120.0 });
			Rear.Radius = 64.0; Rear.A0 = 20.0; Rear.A1 = 165.0; Rear.Thick = 6.0; Rear.Bulge = 4.0; Rear.Drop = 28.0; Rear.WellMaxY = 116.0;
			Set(Rear.YIn, { 96.0, 86.0, 84.0, 88.0, 112.0 }); Set(Rear.YOut, { 150.0, 166.0, 168.0, 164.0, 150.0 });
			break;
		}
	}

	FTNProcMeshBuffers BuildFenders(ETNBuggyBodyStyle Style)
	{
		FTNProcMeshBuffers B;
		FFenderSpec Front;
		FFenderSpec Rear;
		FendersFor(Style, Front, Rear);
		for (const double Side : { -1.0, 1.0 })
		{
			AddFender(B, Front, Side);
			AddFender(B, Rear, Side);
			if (Style != ETNBuggyBodyStyle::Racer)
			{
				// Uñas de aleta en la punta de las delanteras.
				for (const double T : { 0.1, 0.17 })
				{
					FVector C;
					FVector Dir;
					double Drop = 0.0;
					FenderStation(Front, T, C, Dir, Drop);
					const double Y = (Table5(Front.YIn, T) + Table5(Front.YOut, T)) * 0.5 + 6.0 + (T < 0.15 ? 3.0 : -2.0);
					const FVector P = C + FVector(0.0, Y * Side, 0.0) + Dir * (Front.Thick + Front.Bulge - Drop * 0.35 - 0.5);
					AddEllipsoid(B, P, Dir.RotateAngleAxis(90.0, FVector(0.0, 1.0, 0.0)) * 4.0, FVector(0.0, 3.0, 0.0), Dir * 2.6, 3, 6, Skin(0.62f));
				}
			}
		}
		return B;
	}

	// ─────────────────────────────────────────────────────────────────────────────
	// Bajos: chapa, brazos y amortiguadores de la suspensión, pilotos traseros
	// ─────────────────────────────────────────────────────────────────────────────

	FTNProcMeshBuffers BuildChassis(ETNBuggyBodyStyle Style)
	{
		FTNProcMeshBuffers B;
		const FDome D = DomeFor(Style);
		AddOBox(B, FVector(-30.0, 0.0, 41.0), FVector(158.0, 0.0, 0.0), FVector(0.0, 58.0, 0.0), FVector(0.0, 0.0, 4.5), Matte(0x2A2F36));
		const FLinearColor ArmColor = Metal(0x3A3F47);
		const FLinearColor ShockColor = Style == ETNBuggyBodyStyle::Racer ? Matte(0xD9432F) : Matte(0xFFB300);
		for (const double Side : { -1.0, 1.0 })
		{
			const FVector F = Frame::FrontWheel;
			const FVector HubF(F.X, (F.Y - 20.0) * Side, F.Z);
			B.AddBeam(FVector(F.X - 20.0, 56.0 * Side, 58.0), HubF, 2.4, ArmColor);
			B.AddBeam(FVector(F.X + 18.0, 56.0 * Side, 58.0), HubF, 2.4, ArmColor);
			B.AddBeam(FVector(F.X - 4.0, 60.0 * Side, 90.0), FVector(F.X, (F.Y - 24.0) * Side, 58.0), 3.0, ShockColor);
			const FVector R = Frame::RearWheel;
			const FVector HubR(R.X, (R.Y - 20.0) * Side, R.Z);
			B.AddBeam(FVector(R.X + 18.0, 78.0 * Side, 56.0), HubR, 2.4, ArmColor);
			B.AddBeam(FVector(R.X - 18.0, 78.0 * Side, 56.0), HubR, 2.4, ArmColor);
			B.AddBeam(FVector(R.X, 84.0 * Side, 88.0), FVector(R.X, (R.Y - 24.0) * Side, 58.0), 3.0, ShockColor);
			// Pilotos traseros en el faldón del caparazón.
			const double Th = PI + Side * FMath::DegreesToRadians(22.0);
			const FVector2D Rim = D.Ellipse(Th, 1.018);
			const FVector Out = FVector(Rim.X - D.Cx, Rim.Y, 0.0).GetSafeNormal();
			const FVector Lamp(Rim.X, Rim.Y, D.Z0 - 10.0);
			AddDisc(B, Lamp + Out * 0.6, Out, 7.5, 12, Matte(0x1E1E22));
			AddDisc(B, Lamp + Out * 1.4, Out, 5.8, 12, Light(0xFF3B30));
		}
		return B;
	}

	// ─────────────────────────────────────────────────────────────────────────────
	// Cola de escape
	// ─────────────────────────────────────────────────────────────────────────────

	FTNProcMeshBuffers BuildTail(ETNBuggyBodyStyle Style)
	{
		FTNProcMeshBuffers B;
		const FDome D = DomeFor(Style);
		const double X0 = D.Cx - D.A + 14.0;
		if (Style == ETNBuggyBodyStyle::Racer)
		{
			AddTube(B, { FVector(X0, 0.0, 56.0), FVector(X0 - 22.0, 0.0, 58.0), FVector(X0 - 36.0, 0.0, 61.0), FVector(X0 - 44.0, 0.0, 63.0) },
				{ 12.0, 8.5, 4.5, 0.8 }, 10, Skin(0.95f));
			return B;
		}
		const double K = Style == ETNBuggyBodyStyle::Offroad ? 1.2 : 1.0;
		const TArray<FVector> Tail = { FVector(X0, 0.0, 60.0), FVector(X0 - 18.0, 0.0, 60.0), FVector(X0 - 31.0, 0.0, 63.0),
			FVector(X0 - 41.0, 0.0, 68.0) };
		AddTube(B, Tail, { 15.0 * K, 13.0 * K, 10.0 * K, 7.6 * K }, 12, Skin(0.95f), true, false);
		// Punta cromada con el agujero del escape.
		const FVector Dir = (Tail[3] - Tail[2]).GetSafeNormal();
		const FVector Tip = Tail[3] + Dir * 9.0;
		AddTube(B, { Tail[3] - Dir * 1.0, Tip }, { 8.0 * K, 8.4 * K }, 12, Metal(0xC9CED6), true, false);
		AddDisc(B, Tip, Dir, 8.4 * K, 12, Metal(0xA8AEB6));
		AddDisc(B, Tip + Dir * 0.3, Dir, 5.6 * K, 12, Matte(0x101012));
		return B;
	}

	// ─────────────────────────────────────────────────────────────────────────────
	// Detrás: alerón de vieira (Clásico), rueda de repuesto (Caimán) o alerón de carreras (Bólido)
	// ─────────────────────────────────────────────────────────────────────────────

	void AddScallop(FTNProcMeshBuffers& B, const FVector& Hinge, const FVector& Up, const FVector& SideDir, double Radius)
	{
		const FVector Normal = FVector::CrossProduct(Up, SideDir).GetSafeNormal();
		constexpr int32 Ribs = 12;
		TArray<FVector> Mid;
		TArray<FVector> Edge;
		for (int32 k = 0; k <= Ribs; ++k)
		{
			const double Phi = FMath::DegreesToRadians(FMath::Lerp(-72.0, 72.0, static_cast<double>(k) / Ribs));
			const FVector Dir = Up * FMath::Cos(Phi) + SideDir * FMath::Sin(Phi);
			const double Ridge = (k % 2) ? -1.0 : 3.5;
			Mid.Add(Hinge + Dir * (Radius * 0.5) + Normal * (Ridge * 0.45));
			Edge.Add(Hinge + Dir * (Radius * ((k % 2) ? 0.97 : 1.03)) + Normal * Ridge);
		}
		for (int32 Face = 0; Face < 2; ++Face)
		{
			const FVector Off = Face == 0 ? FVector::ZeroVector : -Normal * 2.2;
			const FVector Hint = Face == 0 ? Normal : -Normal;
			for (int32 k = 0; k < Ribs; ++k)
			{
				const FLinearColor C = Face == 1 ? Plate(0.58f) : Plate((k % 2) ? 0.82f : 1.f);
				B.AddTri(Hinge + Off, Mid[k] + Off, Mid[k + 1] + Off, Hint, C);
				B.AddQuad(Mid[k] + Off, Mid[k + 1] + Off, Edge[k + 1] + Off, Edge[k] + Off, Hint, C);
			}
		}
		// Canto y orejetas de la charnela.
		for (int32 k = 0; k < Ribs; ++k)
		{
			B.AddQuad(Edge[k], Edge[k + 1], Edge[k + 1] - Normal * 2.2, Edge[k] - Normal * 2.2, (Edge[k] + Edge[k + 1]) * 0.5 - Hinge, Plate(0.7f));
		}
		for (const double S : { -1.0, 1.0 })
		{
			const FVector A = Hinge + SideDir * (S * 2.0);
			const FVector Bp = Hinge + SideDir * (S * 15.0) - Up * 2.0;
			const FVector C = Hinge + SideDir * (S * 11.0) + Up * 9.0;
			B.AddTri(A, Bp, C, Normal, Plate(0.9f));
			B.AddTri(A, Bp, C, -Normal, Plate(0.6f));
		}
	}

	FTNProcMeshBuffers BuildRear(ETNBuggyBodyStyle Style)
	{
		FTNProcMeshBuffers B;
		const FDome D = DomeFor(Style);
		const FLinearColor Strut = Metal(0x3A3F47);
		if (Style == ETNBuggyBodyStyle::Classic)
		{
			const FVector Hinge(-200.0, 0.0, 102.0);
			const FVector Up = FVector(-0.5, 0.0, 0.866).GetSafeNormal();
			AddScallop(B, Hinge, Up, FVector(0.0, 1.0, 0.0), 52.0);
			for (const double S : { -1.0, 1.0 })
			{
				B.AddBeam(D.At(-168.0, 18.0 * S, -1.0), Hinge + FVector(4.0, 18.0 * S, 4.0), 2.0, Strut);
			}
		}
		else if (Style == ETNBuggyBodyStyle::Offroad)
		{
			// Rueda de repuesto atornillada al lomo, mirando atrás.
			FTNProcMeshBuffers Spare = BuildPiece(ETNBuggyBodyStyle::Offroad, EPiece::Wheel);
			const FTransform Xf(FRotator(0.0, -90.0, 0.0), FVector(-226.0, 0.0, 98.0), FVector(0.76));
			Append(B, Spare, Xf);
			AddOBox(B, FVector(-210.0, 0.0, 98.0), FVector(9.0, 0.0, 0.0), FVector(0.0, 8.0, 0.0), FVector(0.0, 0.0, 8.0), Strut);
		}
		else
		{
			// Alerón de carreras con derivas en forma de aleta.
			constexpr double Span = 104.0;
			const TArray<FVector2D> Airfoil = { FVector2D(-196.0, 147.0), FVector2D(-206.0, 151.5), FVector2D(-226.0, 152.0), FVector2D(-244.0, 150.0),
				FVector2D(-226.0, 146.8), FVector2D(-206.0, 145.0) };
			TArray<TArray<FVector>> Rings;
			for (int32 s = 0; s <= 6; ++s)
			{
				const double Y = FMath::Lerp(-Span, Span, s / 6.0);
				TArray<FVector> Ring;
				for (const FVector2D& P : Airfoil) { Ring.Add(FVector(P.X, Y, P.Y)); }
				Rings.Add(Ring);
			}
			// Las caras del perfil van a lo largo de Y: el eje de los anillos es Y, así que cada cara mira afuera del perfil.
			TArray<TArray<FVector>> Faces;
			for (int32 k = 0; k < Airfoil.Num(); ++k)
			{
				TArray<FVector> Line;
				for (const TArray<FVector>& R : Rings) { Line.Add(R[k]); }
				Faces.Add(Line);
			}
			const FVector2D Center = Centroid2D(Airfoil);
			for (int32 k = 0; k < Airfoil.Num(); ++k)
			{
				const int32 K1 = (k + 1) % Airfoil.Num();
				const FVector2D Mid = (Airfoil[k] + Airfoil[K1]) * 0.5 - Center;
				for (int32 s = 0; s < 6; ++s)
				{
					B.AddQuad(Faces[k][s], Faces[k][s + 1], Faces[K1][s + 1], Faces[K1][s], FVector(Mid.X, 0.0, Mid.Y), Mid.Y > 0.0 ? Skin(1.f) : Skin(0.6f));
				}
			}
			for (const double S : { -1.0, 1.0 })
			{
				TArray<FVector2D> Plate2D = { FVector2D(-190.0, 128.0), FVector2D(-200.0, 158.0), FVector2D(-236.0, 160.0), FVector2D(-252.0, 150.0),
					FVector2D(-240.0, 136.0) };
				const double Y = (Span + 1.0) * S;
				for (int32 k = 1; k + 1 < Plate2D.Num(); ++k)
				{
					const FVector A(Plate2D[0].X, Y, Plate2D[0].Y);
					const FVector Bv(Plate2D[k].X, Y, Plate2D[k].Y);
					const FVector C(Plate2D[k + 1].X, Y, Plate2D[k + 1].Y);
					B.AddTri(A, Bv, C, FVector(0.0, S, 0.0), Plate(1.f));
					B.AddTri(A - FVector(0.0, 2.0 * S, 0.0), Bv - FVector(0.0, 2.0 * S, 0.0), C - FVector(0.0, 2.0 * S, 0.0), FVector(0.0, -S, 0.0), Plate(0.7f));
				}
				B.AddBeam(D.At(-176.0, 42.0 * S, -1.0), FVector(-214.0, 42.0 * S, 146.0), 2.2, Strut);
			}
		}
		return B;
	}

	// ─────────────────────────────────────────────────────────────────────────────
	// Extras de cada modelo
	// ─────────────────────────────────────────────────────────────────────────────

	void AddLamp(FTNProcMeshBuffers& B, const FVector& At, const FVector& Facing, double R)
	{
		AddLathe(B, At - Facing * 6.0, Facing, { FVector2D(0.0, R * 0.7), FVector2D(5.0, R), FVector2D(6.0, R) }, 12, Metal(0x2E3238), true, false);
		AddDisc(B, At + Facing * 0.1, Facing, R * 0.86, 12, Light(0xFFF1C9));
	}

	FTNProcMeshBuffers BuildExtras(ETNBuggyBodyStyle Style)
	{
		FTNProcMeshBuffers B;
		const FDome D = DomeFor(Style);
		if (Style == ETNBuggyBodyStyle::Classic)
		{
			// Parachoques de goma bajo la barbilla con dos faros antiniebla.
			AddTube(B, { FVector(214.0, -56.0, 48.0), FVector(230.0, -32.0, 50.0), FVector(234.0, 0.0, 51.0), FVector(230.0, 32.0, 50.0),
				FVector(214.0, 56.0, 48.0) }, 6.0, 8, Matte(0x2A2F36));
			for (const double S : { -1.0, 1.0 })
			{
				AddLamp(B, FVector(232.0, 30.0 * S, 58.0), FVector(1.0, 0.1 * S, 0.0).GetSafeNormal(), 5.0);
			}
		}
		else if (Style == ETNBuggyBodyStyle::Offroad)
		{
			const FLinearColor Bar = Metal(0x2E3238);
			// Defensa tubular con dos faros.
			for (const double S : { -1.0, 1.0 })
			{
				AddTube(B, { FVector(212.0, 50.0 * S, 40.0), FVector(236.0, 46.0 * S, 48.0), FVector(244.0, 42.0 * S, 82.0), FVector(236.0, 36.0 * S, 104.0) },
					3.4, 8, Bar);
				AddLamp(B, FVector(246.0, 24.0 * S, 96.0), FVector(1.0, 0.0, 0.0), 6.5);
			}
			AddTube(B, { FVector(244.0, -42.0, 70.0), FVector(248.0, 0.0, 72.0), FVector(244.0, 42.0, 70.0) }, 3.0, 8, Bar);
			AddTube(B, { FVector(236.0, -36.0, 104.0), FVector(238.0, 0.0, 106.0), FVector(236.0, 36.0, 104.0) }, 3.0, 8, Bar);
			// Arco antivuelco detrás de la conductora con barra de luces.
			const double HoopX = D.NotchX - 4.0;
			TArray<FVector> Hoop = { FVector(HoopX, -70.0, D.Height(HoopX, -70.0) - 4.0), FVector(HoopX, -64.0, 150.0), FVector(HoopX, -50.0, 160.0),
				FVector(HoopX, 50.0, 160.0), FVector(HoopX, 64.0, 150.0), FVector(HoopX, 70.0, D.Height(HoopX, 70.0) - 4.0) };
			AddTube(B, Hoop, 3.6, 8, Bar);
			AddOBox(B, FVector(HoopX + 4.0, 0.0, 160.0), FVector(3.0, 0.0, 0.0), FVector(0.0, 46.0, 0.0), FVector(0.0, 0.0, 4.0), Bar);
			for (const double Y : { -33.0, -11.0, 11.0, 33.0 })
			{
				AddLamp(B, FVector(HoopX + 9.0, Y, 160.0), FVector(1.0, 0.0, 0.0), 5.5);
			}
			// Tubo de buceo naranja junto a la aleta derecha, con su boquilla.
			const FLinearColor Snorkel = Matte(0xFF8C42);
			AddTube(B, { FVector(112.0, 82.0, 78.0), FVector(108.0, 84.0, 120.0), FVector(108.0, 84.0, 148.0), FVector(116.0, 84.0, 160.0),
				FVector(130.0, 84.0, 162.0) }, 4.2, 8, Snorkel);
			AddTube(B, { FVector(130.0, 84.0, 162.0), FVector(138.0, 84.0, 162.0) }, { 5.0, 6.5 }, 8, Matte(0x2A2F36));
			// Faldillas detrás de las ruedas traseras.
			for (const double S : { -1.0, 1.0 })
			{
				const FVector R = Frame::RearWheel;
				AddOBox(B, FVector(R.X - 62.0, R.Y * S, 46.0), FVector(1.2, 0.0, 0.0), FVector(0.0, 17.0, 0.0), FVector(0.0, 0.0, 18.0), Matte(0x1E1E22));
			}
		}
		else
		{
			// Faldón delantero de fibra de carbono y escapes laterales cromados.
			const FLinearColor Carbon = Matte(0x22262B);
			AddOBox(B, FVector(232.0, 0.0, 36.0), FVector(18.0, 0.0, 0.0), FVector(0.0, 66.0, 0.0), FVector(0.0, 0.0, 1.6), Carbon);
			for (const double S : { -1.0, 1.0 })
			{
				AddOBox(B, FVector(238.0, 66.0 * S, 42.0), FVector(12.0, 0.0, 0.0), FVector(0.0, 1.2, 0.0), FVector(0.0, 0.0, 7.0), Carbon);
				for (const double Off : { 0.0, 13.0 })
				{
					const FVector A(-14.0 - Off, 100.0 * S, 58.0 + Off * 0.2);
					const FVector Bp(-58.0 - Off, 114.0 * S, 63.0 + Off * 0.2);
					AddTube(B, { A, Bp }, 5.2, 10, Metal(0xD0D6DE), true, false);
					const FVector Dir = (Bp - A).GetSafeNormal();
					AddDisc(B, Bp, Dir, 5.2, 10, Metal(0xA8AEB6));
					AddDisc(B, Bp + Dir * 0.3, Dir, 3.6, 10, Matte(0x101012));
				}
			}
		}
		return B;
	}

	// ─────────────────────────────────────────────────────────────────────────────
	// Rueda: neumático con tacos y llanta con tapacubos de estrella de mar, beadlock o turbina de nautilo.
	// Eje en Y y la cara de fuera en -Y, como SM_TN_BuggyTire (ATN_Buggy gira 180 grados los neumáticos derechos).
	// ─────────────────────────────────────────────────────────────────────────────

	inline FVector Radial(double T, double R, double Angle)
	{
		return FVector(R * FMath::Cos(Angle), T, R * FMath::Sin(Angle));
	}

	/** Anillo plano o cilíndrico alrededor del eje Y entre (T0, R0) y (T1, R1), mirando hacia Hint (+1 fuera, -1 al eje). */
	void AddBand(FTNProcMeshBuffers& B, double T0, double R0, double T1, double R1, int32 Segs, const FLinearColor& Color, const FVector& FaceHint, bool bTowardAxis = false)
	{
		for (int32 s = 0; s < Segs; ++s)
		{
			const double A0 = 2.0 * PI * s / Segs;
			const double A1 = 2.0 * PI * (s + 1) / Segs;
			const FVector P0 = Radial(T0, R0, A0);
			const FVector P1 = Radial(T0, R0, A1);
			const FVector P2 = Radial(T1, R1, A1);
			const FVector P3 = Radial(T1, R1, A0);
			const FVector RadialOut = Radial(0.0, 1.0, (A0 + A1) * 0.5);
			const FVector Hint = FaceHint + (bTowardAxis ? -RadialOut : FVector::ZeroVector);
			B.AddQuad(P0, P1, P2, P3, Hint.IsNearlyZero() ? RadialOut : Hint, Color);
		}
	}

	/** Rueda con la cara de fuera en +Y; BuildWheel la gira. */
	FTNProcMeshBuffers BuildWheelOutwardY(ETNBuggyBodyStyle Style)
	{
		FTNProcMeshBuffers B;
		constexpr int32 Segs = 28;
		const bool bOff = Style == ETNBuggyBodyStyle::Offroad;
		// Neumático: perfil (posición en el eje, radio) de dentro a fuera.
		const TArray<FVector2D> Profile = { FVector2D(-16.0, 30.0), FVector2D(-17.5, 37.0), FVector2D(-17.5, 44.0), FVector2D(-15.5, 48.4),
			FVector2D(15.5, 48.4), FVector2D(17.5, 44.0), FVector2D(17.5, 37.0), FVector2D(16.0, 30.0) };
		TArray<TArray<FVector>> Rings;
		for (const FVector2D& P : Profile)
		{
			TArray<FVector> Ring;
			for (int32 s = 0; s < Segs; ++s) { Ring.Add(Radial(P.X, P.Y, 2.0 * PI * s / Segs)); }
			Rings.Add(Ring);
		}
		// Las caras del lathe miran fuera del eje Y; los flancos, a los lados.
		for (int32 i = 0; i + 1 < Rings.Num(); ++i)
		{
			const bool bTread = i == 3;
			const bool bOuterSide = Profile[i].X > 0.0 && Profile[i + 1].X > 0.0;
			const FLinearColor C = bTread ? Matte(0x26262A) : Matte(bOuterSide ? 0x3B3B42 : 0x323238);
			for (int32 s = 0; s < Segs; ++s)
			{
				const int32 S1 = (s + 1) % Segs;
				const FVector Mid = (Rings[i][s] + Rings[i][S1] + Rings[i + 1][s] + Rings[i + 1][S1]) * 0.25;
				const FVector Side = FVector(0.0, (Profile[i].X + Profile[i + 1].X) * 0.5, 0.0) * 0.08;
				B.AddQuad(Rings[i][s], Rings[i][S1], Rings[i + 1][S1], Rings[i + 1][s], FVector(Mid.X, 0.0, Mid.Z).GetSafeNormal() + Side, C);
			}
		}
		// Letras en relieve del flanco de fuera (un anillo más claro).
		AddBand(B, 17.6, 38.5, 17.6, 41.5, Segs, Matte(0x55565C), FVector(0.0, 1.0, 0.0));
		// Tacos en dos filas al tresbolillo (más grandes en el Caimán).
		const int32 Blocks = bOff ? 14 : 16;
		const double Outer = bOff ? 52.0 : 51.0;
		for (int32 Row = 0; Row < 2; ++Row)
		{
			for (int32 k = 0; k < Blocks; ++k)
			{
				const double A = 2.0 * PI * (k + (Row ? 0.5 : 0.0)) / Blocks;
				const double Half = PI / Blocks * (bOff ? 0.62 : 0.5);
				const double T0 = Row ? (bOff ? 1.0 : 2.0) : (bOff ? -17.5 : -14.5);
				const double T1 = Row ? (bOff ? 17.5 : 14.5) : (bOff ? -1.0 : -2.0);
				const FVector Tan(-FMath::Sin(A), 0.0, FMath::Cos(A));
				const FVector Out(FMath::Cos(A), 0.0, FMath::Sin(A));
				const double Rm = (48.0 + Outer) * 0.5;
				AddOBox(B, Out * Rm + FVector(0.0, (T0 + T1) * 0.5, 0.0), Out * ((Outer - 48.0) * 0.5), FVector(0.0, (T1 - T0) * 0.5, 0.0),
					Tan * (Rm * Half), Matte(0x2A2A2E), 0.85f, 0.85f);
			}
		}
		// Llanta (pintura de la piel): labio, barril hacia dentro y plato hundido.
		AddBand(B, 15.0, 30.0, 16.6, 31.6, Segs, SkinPlain(0.95f), FVector(0.0, 1.0, 0.0));
		AddBand(B, 9.0, 30.0, 15.0, 30.0, Segs, SkinPlain(0.55f), FVector::ZeroVector, true);
		const FLinearColor Dish = Style == ETNBuggyBodyStyle::Racer ? Metal(0xB8C0C8) : SkinPlain(bOff ? 0.7f : 0.8f);
		AddBand(B, 9.0, 8.0, 9.0, 30.0, Segs, Dish, FVector(0.0, 1.0, 0.0));
		// Por dentro: un disco oscuro.
		AddDisc(B, FVector(0.0, -16.0, 0.0), FVector(0.0, -1.0, 0.0), 30.5, Segs, Matte(0x1E1E22));
		// Buje cromado.
		AddLathe(B, FVector(0.0, 9.0, 0.0), FVector(0.0, 1.0, 0.0), { FVector2D(0.0, 8.0), FVector2D(3.0, 7.4), FVector2D(4.4, 4.0) }, 12,
			Metal(0xC9CED6), false, true);
		if (Style == ETNBuggyBodyStyle::Classic)
		{
			// Tapacubos de estrella de mar.
			for (int32 k = 0; k < 5; ++k)
			{
				const double A = 2.0 * PI * k / 5.0 + HALF_PI;
				const double W = FMath::DegreesToRadians(15.0);
				const FVector B1 = Radial(9.6, 8.5, A - W);
				const FVector B2 = Radial(9.6, 8.5, A + W);
				const FVector Tip = Radial(9.6, 27.0, A);
				const FVector Top = Radial(12.6, 13.0, A);
				B.AddTri(B1, Tip, Top, FVector(0.0, 1.0, 0.0) + Radial(0.0, 1.0, A - HALF_PI) * 0.6, PlatePlain(1.f));
				B.AddTri(Top, Tip, B2, FVector(0.0, 1.0, 0.0) + Radial(0.0, 1.0, A + HALF_PI) * 0.6, PlatePlain(0.8f));
				B.AddTri(B1, Top, B2, FVector(0.0, 1.0, 0.0), PlatePlain(0.9f));
			}
		}
		else if (bOff)
		{
			// Aro beadlock oscuro con tornillos y agujeros en el plato.
			AddBand(B, 16.8, 25.0, 16.8, 31.8, Segs, Matte(0x2B2F36), FVector(0.0, 1.0, 0.0));
			for (int32 k = 0; k < 12; ++k)
			{
				const double A = 2.0 * PI * k / 12.0;
				const FVector P = Radial(17.6, 28.4, A);
				AddOBox(B, P, FVector(1.2, 0.0, 0.0), FVector(0.0, 0.8, 0.0), FVector(0.0, 0.0, 1.2), Metal(0xD0D6DE));
			}
			for (int32 k = 0; k < 6; ++k)
			{
				AddDisc(B, Radial(9.15, 18.5, 2.0 * PI * (k + 0.5) / 6.0), FVector(0.0, 1.0, 0.0), 4.2, 8, Matte(0x111114));
			}
		}
		else
		{
			// Turbina de nautilo: álabes en espiral.
			for (int32 k = 0; k < 7; ++k)
			{
				TArray<FVector> Foot;
				TArray<FVector> Top;
				for (int32 s = 0; s <= 5; ++s)
				{
					const double R = FMath::Lerp(8.5, 28.5, s / 5.0);
					const double A = 2.0 * PI * k / 7.0 + FMath::DegreesToRadians(55.0) * (s / 5.0);
					Foot.Add(Radial(9.2, R, A));
					Top.Add(Radial(12.4, R, A + FMath::DegreesToRadians(4.0)));
				}
				for (int32 s = 0; s < 5; ++s)
				{
					const FVector Fwd = Radial(0.0, 1.0, 2.0 * PI * k / 7.0 + FMath::DegreesToRadians(55.0) * (s / 5.0) + HALF_PI);
					B.AddQuad(Foot[s], Foot[s + 1], Top[s + 1], Top[s], Fwd + FVector(0.0, 0.5, 0.0), SkinPlain(1.f));
					B.AddQuad(Foot[s], Foot[s + 1], Top[s + 1], Top[s], -Fwd + FVector(0.0, 0.5, 0.0), SkinPlain(0.75f));
				}
			}
		}
		return B;
	}

	FTNProcMeshBuffers BuildWheel(ETNBuggyBodyStyle Style)
	{
		FTNProcMeshBuffers B;
		Append(B, BuildWheelOutwardY(Style), FTransform(FRotator(0.0, 180.0, 0.0)));
		return B;
	}

	// ─────────────────────────────────────────────────────────────────────────────
	// Antena con banderín del equipo
	// ─────────────────────────────────────────────────────────────────────────────

	FTNProcMeshBuffers BuildAntenna()
	{
		FTNProcMeshBuffers B;
		AddLathe(B, FVector::ZeroVector, FVector::UpVector, { FVector2D(0.0, 4.5), FVector2D(3.0, 3.6), FVector2D(6.0, 1.6) }, 8, Matte(0x1E1E22), true, false);
		B.AddBeam(FVector(0.0, 0.0, 4.0), FVector(0.0, 0.0, 150.0), 0.8, Matte(0x1E1E22));
		AddEllipsoid(B, FVector(0.0, 0.0, 152.0), FVector(3.8, 0.0, 0.0), FVector(0.0, 3.8, 0.0), FVector(0.0, 0.0, 3.8), 4, 8, Team(1.f));
		// Banderín: triángulo ondulado, de las dos caras.
		constexpr int32 Cols = 5;
		TArray<FVector> TopEdge;
		TArray<FVector> BottomEdge;
		for (int32 c = 0; c <= Cols; ++c)
		{
			const double T = static_cast<double>(c) / Cols;
			const double X = -50.0 * T;
			const double Y = 5.0 * FMath::Sin(T * 4.2) * T;
			TopEdge.Add(FVector(X, Y, FMath::Lerp(146.0, 133.0, T)));
			BottomEdge.Add(FVector(X, Y, FMath::Lerp(116.0, 131.0, T)));
		}
		for (int32 c = 0; c < Cols; ++c)
		{
			B.AddQuad(BottomEdge[c], BottomEdge[c + 1], TopEdge[c + 1], TopEdge[c], FVector(0.0, 1.0, 0.0), Team(1.f));
			B.AddQuad(BottomEdge[c], BottomEdge[c + 1], TopEdge[c + 1], TopEdge[c], FVector(0.0, -1.0, 0.0), Team(0.82f));
		}
		return B;
	}

	const TCHAR* ModelName(ETNBuggyBodyStyle Style)
	{
		switch (Style)
		{
		case ETNBuggyBodyStyle::Stock: return TEXT("Serie");
		case ETNBuggyBodyStyle::Offroad: return TEXT("Caiman");
		case ETNBuggyBodyStyle::Racer: return TEXT("Laud");
		default: return TEXT("Clasico");
		}
	}

	const TCHAR* PieceString(EPiece Piece)
	{
		switch (Piece)
		{
		case EPiece::Chassis: return TEXT("Chassis");
		case EPiece::Cockpit: return TEXT("Cockpit");
		case EPiece::Shell: return TEXT("Shell");
		case EPiece::Head: return TEXT("Head");
		case EPiece::Fenders: return TEXT("Fenders");
		case EPiece::Tail: return TEXT("Tail");
		case EPiece::Rear: return TEXT("Rear");
		case EPiece::Extras: return TEXT("Extras");
		case EPiece::Wheel: return TEXT("Wheel");
		case EPiece::Antenna: return TEXT("Antenna");
		default: return TEXT("Unknown");
		}
	}
}

namespace TNBuggyArt
{
	using namespace TNBuggyArtDetail;

	FName PieceName(ETNBuggyBodyStyle Style, EPiece Piece)
	{
		return FName(*FString::Printf(TEXT("Rally.Buggy.%s.%s"), PieceString(Piece), ModelName(Style)));
	}

	FTNProcMeshBuffers BuildPiece(ETNBuggyBodyStyle Style, EPiece Piece)
	{
		// El de serie es SM_TN_BuggyBody: solo lleva la antena (con una pintura de la tienda).
		if (Style == ETNBuggyBodyStyle::Stock && Piece != EPiece::Antenna)
		{
			return FTNProcMeshBuffers();
		}
		switch (Piece)
		{
		case EPiece::Chassis: return BuildChassis(Style);
		case EPiece::Cockpit: return BuildCockpit(Style);
		case EPiece::Shell: return BuildShell(Style);
		case EPiece::Head: return BuildHead(Style);
		case EPiece::Fenders: return BuildFenders(Style);
		case EPiece::Tail: return BuildTail(Style);
		case EPiece::Rear: return BuildRear(Style);
		case EPiece::Extras: return BuildExtras(Style);
		case EPiece::Wheel: return BuildWheel(Style);
		case EPiece::Antenna: return BuildAntenna();
		default: return FTNProcMeshBuffers();
		}
	}

	FVector AntennaMount(ETNBuggyBodyStyle Style)
	{
		if (Style == ETNBuggyBodyStyle::Stock)
		{
			return StockAntennaMount;
		}
		// En la punta de la aleta trasera izquierda: lejos de la torreta.
		FFenderSpec Front;
		FFenderSpec Rear;
		FendersFor(Style, Front, Rear);
		constexpr double T = 0.9;
		FVector C;
		FVector Dir;
		double Drop = 0.0;
		FenderStation(Rear, T, C, Dir, Drop);
		const double Y = (Table5(Rear.YIn, T) + Table5(Rear.YOut, T)) * 0.5 + 6.0;
		return C + FVector(0.0, -Y, 0.0) + Dir * (Rear.Thick + Rear.Bulge - Drop * 0.35 - 1.0);
	}

	FVector ExhaustLocal(ETNBuggyBodyStyle Style, const FVector& Default)
	{
		if (Style == ETNBuggyBodyStyle::Stock || Style == ETNBuggyBodyStyle::Count)
		{
			return Default;
		}
		// La punta de la cola (BuildTail): en el clásico y el caimán, la boca del escape cromado.
		const FDome D = DomeFor(Style);
		const double X0 = D.Cx - D.A + 14.0;
		return Style == ETNBuggyBodyStyle::Racer ? FVector(X0 - 46.0, 0.0, 63.5) : FVector(X0 - 49.0, 0.0, 72.0);
	}

	void BakePaint(FTNProcMeshBuffers& Buffers, const FTNBuggyPaintInfo& Paint, const FLinearColor& TeamColor)
	{
		for (FLinearColor& C : Buffers.Colors)
		{
			// Octavos: 8 pintura, 7 pintura sin dibujo, 6 equipo, 4 luz, 2 metal, 0 mate.
			const int32 Code = FMath::RoundToInt(C.A * 8.f);
			if (Code >= 7)
			{
				const FLinearColor Mixed = Paint.Base * C.R + Paint.Plates * C.G + Paint.Accent * C.B;
				C = FLinearColor(Mixed.R, Mixed.G, Mixed.B, Paint.Shine);
			}
			else if (Code == 6)
			{
				C = FLinearColor(TeamColor.R * C.R, TeamColor.G * C.R, TeamColor.B * C.R, 0.f);
			}
			else
			{
				C.A = Code == 2 ? 1.f : 0.f;
			}
		}
	}

	UMaterialInterface* PaintMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		static bool bWarned = false;
		if (!Cached.IsValid())
		{
			Cached = LoadObject<UMaterialInterface>(nullptr, PaintMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Cached.IsValid() && !bWarned)
			{
				bWarned = true;
				UE_LOG(LogTNBuggy, Warning, TEXT("Falta %s (Scripts/build_buggy_paint.py): el buggy se pinta con los colores horneados."), PaintMaterialPath);
			}
		}
		return Cached.Get();
	}

	UStaticMesh* GetPieceMesh(ETNBuggyBodyStyle Style, EPiece Piece, const FTNBuggyPaintInfo* BakedPaint)
	{
		static TMap<FString, UStaticMesh*> Cache;
		const FString Key = PieceName(Style, Piece).ToString() + (BakedPaint ? FString(TEXT("|")) + BakedPaint->Id.ToString() : FString());
		if (UStaticMesh** Found = Cache.Find(Key)) { return *Found; }
		FTNProcMeshBuffers Buffers = BuildPiece(Style, Piece);
		UMaterialInterface* Material = nullptr;
		if (BakedPaint)
		{
			BakePaint(Buffers, *BakedPaint, FLinearColor::White);
			Material = LoadObject<UMaterialInterface>(nullptr, VertexColorMaterialPath);
		}
		else
		{
			Material = PaintMaterial();
		}
		// El alfa de los propios buffers (FixedAlpha = -2): la zona de M_BuggyPaint o el brillo de M_CosmeticVertexColor.
		UStaticMesh* Mesh = Buffers.IsEmpty() ? nullptr : TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), Buffers, Material, false, 0.f, 1.f, -2.f);
		if (Mesh) { Mesh->AddToRoot(); }
		Cache.Add(Key, Mesh);
		return Mesh;
	}

	void ApplyPaint(UMaterialInstanceDynamic* MID, const FTNBuggyPaintInfo& Paint, const FLinearColor& TeamColor, bool bStockZones)
	{
		if (!MID) { return; }
		// Zonas de M_TN_BuggyZones en el de serie; sus luces brillan como en su material (LightEmissive 2).
		MID->SetScalarParameterValue(TEXT("ZoneScheme"), bStockZones ? 1.f : 0.f);
		MID->SetScalarParameterValue(TEXT("LightGlow"), bStockZones ? 2.f : 0.9f);
		MID->SetVectorParameterValue(TEXT("BaseColor"), Paint.Base);
		MID->SetVectorParameterValue(TEXT("PlateColor"), Paint.Plates);
		MID->SetVectorParameterValue(TEXT("AccentColor"), Paint.Accent);
		MID->SetVectorParameterValue(TEXT("PatternColor"), Paint.PatternColor);
		MID->SetVectorParameterValue(TEXT("TeamColor"), TeamColor);
		MID->SetScalarParameterValue(TEXT("Pattern"), static_cast<float>(Paint.Pattern));
		MID->SetScalarParameterValue(TEXT("PatternScale"), Paint.PatternScale);
		MID->SetScalarParameterValue(TEXT("Shine"), Paint.Shine);
		MID->SetScalarParameterValue(TEXT("Glow"), Paint.Glow);
	}

	namespace Stock
	{
		UStaticMesh* BodyMesh()
		{
			return LoadObject<UStaticMesh>(nullptr, StockBodyPath);
		}

		UStaticMesh* TireMesh()
		{
			return LoadObject<UStaticMesh>(nullptr, StockTirePath);
		}

		UMaterialInterface* Skin(int32 TeamIndex)
		{
			const int32 Index = TeamIndex >= 0 ? TeamIndex % UE_ARRAY_COUNT(StockSkinPaths) : 0;
			return LoadObject<UMaterialInterface>(nullptr, StockSkinPaths[Index]);
		}
	}
}
