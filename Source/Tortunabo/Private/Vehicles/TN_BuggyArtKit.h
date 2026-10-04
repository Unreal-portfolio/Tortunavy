// Primitivas de caras planas para el arte del buggy (TN_BuggyArt.cpp). A diferencia de algunas del kit del mapa, no
// tocan el alfa del color: en el buggy el alfa es el código de zona de M_BuggyPaint (TNBuggyArt::Zone).
#pragma once

#include "CoreMinimal.h"
#include "Algo/Reverse.h"
#include "TN_BuggyArt.h"
#include "../World/ProcMap/TN_ProcMapMeshKit.h"
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"

namespace TNBuggyKit
{
	using TNProcMesh::FTNProcMeshBuffers;

	// ── Colores: el alfa es la zona ────────────────────────────────────────────

	/** Color sRGB 0xRRGGBB en lineal (el material lo recibe tal cual: MakeStaticMesh lo guarda sin aclararlo). */
	inline FLinearColor Lin(uint32 Hex, float ZoneAlpha)
	{
		using TNProcRuntimeMesh::SRGBToLinear;
		return FLinearColor(SRGBToLinear(((Hex >> 16) & 255) / 255.f), SRGBToLinear(((Hex >> 8) & 255) / 255.f),
			SRGBToLinear((Hex & 255) / 255.f), ZoneAlpha);
	}
	inline FLinearColor Matte(uint32 Hex) { return Lin(Hex, TNBuggyArt::Zone::Matte); }
	inline FLinearColor Metal(uint32 Hex) { return Lin(Hex, TNBuggyArt::Zone::Metal); }
	inline FLinearColor Light(uint32 Hex) { return Lin(Hex, TNBuggyArt::Zone::Light); }
	/** Pintura: carrocería (R), placas (G) y piel (B), con su sombreado. */
	inline FLinearColor Base(float Shade = 1.f) { return FLinearColor(Shade, 0.f, 0.f, TNBuggyArt::Zone::Paint); }
	inline FLinearColor Plate(float Shade = 1.f) { return FLinearColor(0.f, Shade, 0.f, TNBuggyArt::Zone::Paint); }
	inline FLinearColor Skin(float Shade = 1.f) { return FLinearColor(0.f, 0.f, Shade, TNBuggyArt::Zone::Paint); }
	inline FLinearColor Team(float Shade = 1.f) { return FLinearColor(Shade, Shade, Shade, TNBuggyArt::Zone::Team); }
	/** Pintura sin dibujo (llantas y detalles pequeños, donde el dibujo sería ruido). */
	inline FLinearColor SkinPlain(float Shade = 1.f) { return FLinearColor(0.f, 0.f, Shade, TNBuggyArt::Zone::PaintPlain); }
	inline FLinearColor PlatePlain(float Shade = 1.f) { return FLinearColor(0.f, Shade, 0.f, TNBuggyArt::Zone::PaintPlain); }
	/** Oscurece o aclara el RGB sin tocar la zona. */
	inline FLinearColor Shade(const FLinearColor& C, float K) { return FLinearColor(C.R * K, C.G * K, C.B * K, C.A); }

	// ── Geometría 3D ───────────────────────────────────────────────────────────

	inline FVector Perp(const FVector& Axis)
	{
		const FVector Ref = FMath::Abs(Axis.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector;
		return FVector::CrossProduct(Axis, Ref).GetSafeNormal();
	}

	inline FVector Centroid(const TArray<FVector>& Pts)
	{
		FVector C = FVector::ZeroVector;
		for (const FVector& P : Pts) { C += P; }
		return Pts.Num() ? C / static_cast<double>(Pts.Num()) : C;
	}

	/**
	 * Anillos del mismo tamaño unidos por quads (cada cara mira hacia fuera del eje de los anillos) y tapas opcionales.
	 * ColorAt(anillo, arista) da el color de cada quad.
	 */
	template <typename FColorAt>
	void AddLoft(FTNProcMeshBuffers& B, const TArray<TArray<FVector>>& Rings, bool bClosed, FColorAt&& ColorAt, bool bCapStart = false,
		bool bCapEnd = false, const FLinearColor& CapColor = FLinearColor::White)
	{
		if (Rings.Num() < 2) { return; }
		TArray<FVector> Centers;
		for (const TArray<FVector>& R : Rings) { Centers.Add(Centroid(R)); }
		for (int32 i = 0; i + 1 < Rings.Num(); ++i)
		{
			const TArray<FVector>& R0 = Rings[i];
			const TArray<FVector>& R1 = Rings[i + 1];
			const int32 N = FMath::Min(R0.Num(), R1.Num());
			const int32 Edges = bClosed ? N : N - 1;
			const FVector Axis = (Centers[i] + Centers[i + 1]) * 0.5;
			for (int32 k = 0; k < Edges; ++k)
			{
				const int32 K1 = (k + 1) % N;
				const FVector Mid = (R0[k] + R0[K1] + R1[k] + R1[K1]) * 0.25;
				B.AddQuad(R0[k], R0[K1], R1[K1], R1[k], Mid - Axis, ColorAt(i, k));
			}
		}
		const auto Cap = [&B, &CapColor](const TArray<FVector>& Ring, const FVector& Center, const FVector& Out)
		{
			for (int32 k = 0; k < Ring.Num(); ++k) { B.AddTri(Center, Ring[k], Ring[(k + 1) % Ring.Num()], Out, CapColor); }
		};
		if (bCapStart) { Cap(Rings[0], Centers[0], Centers[0] - Centers[1]); }
		if (bCapEnd) { Cap(Rings.Last(), Centers.Last(), Centers.Last() - Centers[Centers.Num() - 2]); }
	}

	inline void AddLoft(FTNProcMeshBuffers& B, const TArray<TArray<FVector>>& Rings, bool bClosed, const FLinearColor& Color,
		bool bCapStart = false, bool bCapEnd = false)
	{
		AddLoft(B, Rings, bClosed, [&Color](int32, int32) { return Color; }, bCapStart, bCapEnd, Color);
	}

	/** Elipsoide de ejes AxX, AxY, AxZ (vectores con su semieje); BottomShade oscurece la mitad de abajo. */
	inline void AddEllipsoid(FTNProcMeshBuffers& B, const FVector& C, const FVector& AxX, const FVector& AxY, const FVector& AxZ, int32 Rings,
		int32 Segs, const FLinearColor& Color, float BottomShade = 1.f)
	{
		TArray<TArray<FVector>> Pts;
		for (int32 r = 0; r <= Rings; ++r)
		{
			const double Th = PI * r / Rings;
			TArray<FVector> Ring;
			for (int32 s = 0; s < Segs; ++s)
			{
				const double Ph = 2.0 * PI * s / Segs;
				Ring.Add(C + AxX * (FMath::Sin(Th) * FMath::Cos(Ph)) + AxY * (FMath::Sin(Th) * FMath::Sin(Ph)) + AxZ * FMath::Cos(Th));
			}
			Pts.Add(Ring);
		}
		for (int32 r = 0; r < Rings; ++r)
		{
			const float T = (r + 0.5f) / Rings;
			const FLinearColor RingColor = Shade(Color, FMath::Lerp(1.f, BottomShade, FMath::Clamp((T - 0.45f) / 0.55f, 0.f, 1.f)));
			for (int32 s = 0; s < Segs; ++s)
			{
				const int32 S1 = (s + 1) % Segs;
				const FVector Mid = (Pts[r][s] + Pts[r][S1] + Pts[r + 1][s] + Pts[r + 1][S1]) * 0.25;
				B.AddQuad(Pts[r][s], Pts[r][S1], Pts[r + 1][S1], Pts[r + 1][s], Mid - C, RingColor);
			}
		}
	}

	/** Casquete de esfera alrededor de Axis (ángulo desde el polo en grados). */
	inline void AddSphereCap(FTNProcMeshBuffers& B, const FVector& C, double R, const FVector& AxisIn, double AngleDeg, int32 Rings, int32 Segs,
		const FLinearColor& Color)
	{
		const FVector Axis = AxisIn.GetSafeNormal();
		const FVector U = Perp(Axis);
		const FVector V = FVector::CrossProduct(Axis, U);
		const FVector Pole = C + Axis * R;
		TArray<FVector> Prev;
		for (int32 r = 1; r <= Rings; ++r)
		{
			const double A = FMath::DegreesToRadians(AngleDeg) * r / Rings;
			TArray<FVector> Ring;
			for (int32 s = 0; s < Segs; ++s)
			{
				const double Ph = 2.0 * PI * s / Segs;
				Ring.Add(C + (Axis * FMath::Cos(A) + (U * FMath::Cos(Ph) + V * FMath::Sin(Ph)) * FMath::Sin(A)) * R);
			}
			for (int32 s = 0; s < Segs; ++s)
			{
				const int32 S1 = (s + 1) % Segs;
				if (r == 1) { B.AddTri(Pole, Ring[s], Ring[S1], Pole - C, Color); }
				else { B.AddQuad(Prev[s], Prev[S1], Ring[S1], Ring[s], (Prev[s] + Ring[S1]) * 0.5 - C, Color); }
			}
			Prev = MoveTemp(Ring);
		}
	}

	/** Tubo de sección circular por una polilínea, con radio por punto. */
	inline void AddTube(FTNProcMeshBuffers& B, const TArray<FVector>& Pts, const TArray<double>& Radii, int32 Segs, const FLinearColor& Color,
		bool bCapStart = true, bool bCapEnd = true)
	{
		const int32 N = Pts.Num();
		if (N < 2) { return; }
		TArray<TArray<FVector>> Rings;
		FVector Normal = FVector::ZeroVector;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector T = (Pts[FMath::Min(i + 1, N - 1)] - Pts[FMath::Max(i - 1, 0)]).GetSafeNormal();
			Normal = i == 0 ? Perp(T) : (Normal - T * FVector::DotProduct(Normal, T)).GetSafeNormal();
			if (Normal.IsNearlyZero()) { Normal = Perp(T); }
			const FVector Bin = FVector::CrossProduct(T, Normal);
			const double R = Radii.IsValidIndex(i) ? Radii[i] : (Radii.Num() ? Radii.Last() : 1.0);
			TArray<FVector> Ring;
			for (int32 s = 0; s < Segs; ++s)
			{
				const double Ph = 2.0 * PI * s / Segs;
				Ring.Add(Pts[i] + (Normal * FMath::Cos(Ph) + Bin * FMath::Sin(Ph)) * R);
			}
			Rings.Add(Ring);
		}
		AddLoft(B, Rings, true, Color, bCapStart, bCapEnd);
	}

	inline void AddTube(FTNProcMeshBuffers& B, const TArray<FVector>& Pts, double Radius, int32 Segs, const FLinearColor& Color,
		bool bCapStart = true, bool bCapEnd = true)
	{
		AddTube(B, Pts, TArray<double>{ Radius }, Segs, Color, bCapStart, bCapEnd);
	}

	/** Cuerpo de revolución alrededor de Axis: perfil (distancia por el eje, radio). */
	inline void AddLathe(FTNProcMeshBuffers& B, const FVector& Origin, const FVector& AxisIn, const TArray<FVector2D>& Profile, int32 Segs,
		const FLinearColor& Color, bool bCapStart = true, bool bCapEnd = true)
	{
		const FVector Axis = AxisIn.GetSafeNormal();
		const FVector U = Perp(Axis);
		const FVector V = FVector::CrossProduct(Axis, U);
		TArray<TArray<FVector>> Rings;
		for (const FVector2D& P : Profile)
		{
			TArray<FVector> Ring;
			for (int32 s = 0; s < Segs; ++s)
			{
				const double Ph = 2.0 * PI * s / Segs;
				Ring.Add(Origin + Axis * P.X + (U * FMath::Cos(Ph) + V * FMath::Sin(Ph)) * FMath::Max(P.Y, 0.05));
			}
			Rings.Add(Ring);
		}
		AddLoft(B, Rings, true, Color, bCapStart, bCapEnd);
	}

	/** Caja orientada (semiejes como vectores): arriba Color, lados y abajo más oscuros. */
	inline void AddOBox(FTNProcMeshBuffers& B, const FVector& C, const FVector& X, const FVector& Y, const FVector& Z, const FLinearColor& Color,
		float SideShade = 0.86f, float BottomShade = 0.7f)
	{
		auto P = [&](double Sx, double Sy, double Sz) { return C + X * Sx + Y * Sy + Z * Sz; };
		const FLinearColor Side = Shade(Color, SideShade);
		B.AddQuad(P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), Z, Color);
		B.AddQuad(P(-1, -1, -1), P(-1, 1, -1), P(1, 1, -1), P(1, -1, -1), -Z, Shade(Color, BottomShade));
		B.AddQuad(P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), P(1, -1, 1), X, Side);
		B.AddQuad(P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1), -X, Side);
		B.AddQuad(P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1), P(1, 1, -1), Y, Side);
		B.AddQuad(P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), -Y, Side);
	}

	/** Disco plano (abanico) mirando a Normal. */
	inline void AddDisc(FTNProcMeshBuffers& B, const FVector& C, const FVector& NormalIn, double R, int32 Segs, const FLinearColor& Color)
	{
		const FVector N = NormalIn.GetSafeNormal();
		const FVector U = Perp(N);
		const FVector V = FVector::CrossProduct(N, U);
		for (int32 s = 0; s < Segs; ++s)
		{
			const double A0 = 2.0 * PI * s / Segs;
			const double A1 = 2.0 * PI * (s + 1) / Segs;
			B.AddTri(C, C + (U * FMath::Cos(A0) + V * FMath::Sin(A0)) * R, C + (U * FMath::Cos(A1) + V * FMath::Sin(A1)) * R, N, Color);
		}
	}

	/** Copia Src en Dst transformada (posición y normales). */
	inline void Append(FTNProcMeshBuffers& Dst, const FTNProcMeshBuffers& Src, const FTransform& Xf)
	{
		const int32 Base = Dst.Verts.Num();
		for (int32 i = 0; i < Src.Verts.Num(); ++i)
		{
			Dst.Verts.Add(Xf.TransformPosition(Src.Verts[i]));
			Dst.Normals.Add(Xf.TransformVectorNoScale(Src.Normals[i]));
			Dst.UVs.Add(Src.UVs[i]);
			Dst.Colors.Add(Src.Colors[i]);
		}
		for (const int32 T : Src.Tris) { Dst.Tris.Add(Base + T); }
	}

	// ── Polígonos 2D (placas del caparazón) ───────────────────────────────────

	inline double SignedArea(const TArray<FVector2D>& P)
	{
		double A = 0.0;
		for (int32 i = 0; i < P.Num(); ++i)
		{
			const FVector2D& U = P[i];
			const FVector2D& W = P[(i + 1) % P.Num()];
			A += U.X * W.Y - W.X * U.Y;
		}
		return A * 0.5;
	}

	inline void MakeCCW(TArray<FVector2D>& P)
	{
		if (SignedArea(P) < 0.0) { Algo::Reverse(P); }
	}

	inline FVector2D Centroid2D(const TArray<FVector2D>& P)
	{
		FVector2D C = FVector2D::ZeroVector;
		for (const FVector2D& V : P) { C += V; }
		return P.Num() ? C / static_cast<double>(P.Num()) : C;
	}

	/** Recorta un polígono convexo por el semiplano dot(p - P0, Inward) >= 0. */
	inline TArray<FVector2D> ClipHalfPlane(const TArray<FVector2D>& Poly, const FVector2D& P0, const FVector2D& Inward)
	{
		TArray<FVector2D> Out;
		const int32 N = Poly.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& A = Poly[i];
			const FVector2D& B = Poly[(i + 1) % N];
			const double Da = FVector2D::DotProduct(A - P0, Inward);
			const double Db = FVector2D::DotProduct(B - P0, Inward);
			if (Da >= 0.0) { Out.Add(A); }
			if ((Da >= 0.0) != (Db >= 0.0))
			{
				const double T = Da / (Da - Db);
				Out.Add(A + (B - A) * T);
			}
		}
		return Out;
	}

	/** Encoge un polígono convexo en sentido antihorario: cada lado entra D. Vacío si desaparece. */
	inline TArray<FVector2D> Inset(const TArray<FVector2D>& Poly, double D)
	{
		const int32 N = Poly.Num();
		TArray<FVector2D> Out;
		if (N < 3) { return Out; }
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& P0 = Poly[(i + N - 1) % N];
			const FVector2D& P1 = Poly[i];
			const FVector2D& P2 = Poly[(i + 1) % N];
			const FVector2D E0 = (P1 - P0).GetSafeNormal();
			const FVector2D E1 = (P2 - P1).GetSafeNormal();
			const FVector2D N0(-E0.Y, E0.X);
			const FVector2D N1(-E1.Y, E1.X);
			const FVector2D A = P0 + N0 * D;
			const FVector2D B = P1 + N1 * D;
			const double Cross = E0.X * E1.Y - E0.Y * E1.X;
			if (FMath::Abs(Cross) < 1e-6) { Out.Add(P1 + N0 * D); continue; }
			const FVector2D Diff = B - A;
			const double T = (Diff.X * E1.Y - Diff.Y * E1.X) / Cross;
			Out.Add(A + E0 * T);
		}
		if (SignedArea(Out) <= 1.0) { Out.Reset(); }
		return Out;
	}
}
