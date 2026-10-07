#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachTypes.h"
#include "World/ProcMap/TN_ProcMapFaunaMeshes.h"
#include "World/ProcMap/TN_ProcMapMeshKit.h"

/**
 * Mallas low-poly de caras planas de los enemigos y la tormenta de la playa (solo geometría, sin motor): el cangrejo
 * por piezas (el arrastrador y el subterráneo), el quad con su piloto, los
 * trastos que vuela la tormenta, las piernas de los bañistas, la cagada y su mancha. Todo a TNBeach::Scale veces su
 * tamaño real salvo lo que se construye con las medidas de la fauna (se escala al usarlo). Colores en sRGB.
 */
namespace TNBeachMeshes
{
	using TNProcMesh::FTNProcMeshBuffers;

	inline FLinearColor Rgb(float R, float G, float B)
	{
		return FLinearColor(R, G, B, 1.f);
	}

	/** Caja orientada con ejes ortonormales cualesquiera. */
	inline void AddOBB(FTNProcMeshBuffers& M, const FVector& C, const FVector& X, const FVector& Y, const FVector& Z, const FVector& Half, const FLinearColor& Color)
	{
		auto P = [&](double Sx, double Sy, double Sz) { return C + X * (Sx * Half.X) + Y * (Sy * Half.Y) + Z * (Sz * Half.Z); };
		M.AddQuad(P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), Z, Color);
		M.AddQuad(P(-1, -1, -1), P(-1, 1, -1), P(1, 1, -1), P(1, -1, -1), -Z, Color * 0.85f);
		M.AddQuad(P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), P(1, -1, 1), X, Color);
		M.AddQuad(P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1), -X, Color * 0.95f);
		M.AddQuad(P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1), P(1, 1, -1), Y, Color * 0.95f);
		M.AddQuad(P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), -Y, Color * 0.95f);
	}

	/** Placa de A a B, de ancho Width hacia SideHint (casi perpendicular) y grosor Thick. */
	inline void AddPlate(FTNProcMeshBuffers& M, const FVector& A, const FVector& B, const FVector& SideHint, double Width, double Thick, const FLinearColor& Color)
	{
		const FVector X = (B - A).GetSafeNormal();
		if (X.IsNearlyZero())
		{
			return;
		}
		FVector Z = FVector::CrossProduct(X, SideHint).GetSafeNormal();
		if (Z.IsNearlyZero())
		{
			Z = FVector::UpVector;
		}
		const FVector Y = FVector::CrossProduct(Z, X);
		AddOBB(M, (A + B) * 0.5, X, Y, Z, FVector((B - A).Size() * 0.5, Width * 0.5, Thick * 0.5), Color);
	}

	/** Tubo abierto de dos caras (cubos, vasos): anillos de Seg lados entre A y B. */
	inline void AddOpenTube(FTNProcMeshBuffers& M, const FVector& A, const FVector& B, double RA, double RB, int32 Seg, const FLinearColor& Outside, const FLinearColor& Inside)
	{
		const FVector Ax = (B - A).GetSafeNormal();
		if (Ax.IsNearlyZero())
		{
			return;
		}
		const FVector U = FVector::CrossProduct(Ax, FMath::Abs(Ax.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
		const FVector V = FVector::CrossProduct(Ax, U);
		for (int32 k = 0; k < Seg; ++k)
		{
			const double A0 = TNProcMap::TwoPi * k / Seg;
			const double A1 = TNProcMap::TwoPi * (k + 1) / Seg;
			const FVector D0 = U * FMath::Cos(A0) + V * FMath::Sin(A0);
			const FVector D1 = U * FMath::Cos(A1) + V * FMath::Sin(A1);
			const FVector Mid = (D0 + D1) * 0.5;
			M.AddQuad(A + D0 * RA, A + D1 * RA, B + D1 * RB, B + D0 * RB, Mid, Outside);
			M.AddQuad(A + D0 * RA, A + D1 * RA, B + D1 * RB, B + D0 * RB, -Mid, Inside);
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Cangrejo gigante
	// ─────────────────────────────────────────────────────────────────────────

	/** Medidas reales (cm) del caparazón: semiancho, semifondo y alto. */
	constexpr double CrabW = 9.0;
	constexpr double CrabD = 6.2;
	constexpr double CrabH = 4.2;

	struct FCrabLook
	{
		FLinearColor ShellTop = Rgb(0.86f, 0.2f, 0.1f);
		FLinearColor Shell = Rgb(0.98f, 0.62f, 0.4f);
		FLinearColor Leg = Rgb(0.88f, 0.32f, 0.14f);
		FLinearColor Big = Rgb(1.f, 0.78f, 0.14f);
		FLinearColor Tip = Rgb(1.f, 0.95f, 0.8f);
		FLinearColor Small = Rgb(0.9f, 0.28f, 0.12f);
	};

	/** Cuatro paletas: rojo con la pinza amarilla, violinista azul, violeta y fantasma de arena. */
	inline FCrabLook CrabPalette(int32 Index)
	{
		FCrabLook L;
		switch (((Index % 4) + 4) % 4)
		{
		case 1:
			L.ShellTop = Rgb(0.24f, 0.34f, 0.88f); L.Shell = Rgb(0.86f, 0.84f, 0.74f); L.Leg = Rgb(0.52f, 0.42f, 0.8f);
			L.Big = Rgb(1.f, 0.56f, 0.1f); L.Tip = Rgb(1.f, 0.93f, 0.72f); L.Small = Rgb(0.5f, 0.4f, 0.78f);
			break;
		case 2:
			L.ShellTop = Rgb(0.55f, 0.16f, 0.5f); L.Shell = Rgb(0.95f, 0.76f, 0.82f); L.Leg = Rgb(0.72f, 0.3f, 0.55f);
			L.Big = Rgb(0.97f, 0.96f, 0.88f); L.Tip = Rgb(0.98f, 0.6f, 0.7f); L.Small = Rgb(0.7f, 0.28f, 0.55f);
			break;
		case 3:
			L.ShellTop = Rgb(0.86f, 0.74f, 0.5f); L.Shell = Rgb(0.98f, 0.92f, 0.78f); L.Leg = Rgb(0.82f, 0.7f, 0.48f);
			L.Big = Rgb(0.95f, 0.42f, 0.18f); L.Tip = Rgb(1.f, 0.9f, 0.7f); L.Small = Rgb(0.8f, 0.66f, 0.44f);
			break;
		default:
			break;
		}
		return L;
	}

	/** Pivotes y medidas del cangrejo ya a escala (cm de juego), calculados de la geometría. */
	struct FCrabRig
	{
		/** Centro del cuerpo sobre el suelo. */
		double BodyZ = 0.0;
		/** Pedúnculos (espacio del cuerpo). */
		FVector EyeL = FVector::ZeroVector;
		FVector EyeR = FVector::ZeroVector;
		/** Patas: 0..3 a la izquierda (-Y) de delante a atrás, 4..7 a la derecha. */
		FVector LegPivot[8];
		float LegSplay[8] = {};
		/** Pinza grande (derecha): hombro (cuerpo), codo (brazo) y nudillo del dedo móvil (mano). */
		FVector BigShoulder = FVector::ZeroVector;
		FVector BigElbow = FVector::ZeroVector;
		FVector BigKnuckle = FVector::ZeroVector;
		FVector SmallShoulder = FVector::ZeroVector;
		/** Boca (espuma), espacio del cuerpo. */
		FVector Mouth = FVector::ZeroVector;
		/** Distancia al centro a la que cae la pinza, cabeceo del brazo con la punta en la arena y giro hacia dentro. */
		double Reach = 0.0;
		double SlamPitch = 0.0;
		double ArmYaw = 0.0;
		/** Radio del cuerpo con las patas (colisión). */
		double BodyRadius = 0.0;
	};

	inline FCrabRig CrabRig()
	{
		const double S = TNBeach::Scale;
		const double W = CrabW * S, D = CrabD * S, H = CrabH * S, U = CrabD * 1.5 * S;
		FCrabRig R;
		R.BodyZ = H * 1.25;
		R.EyeL = FVector(D * 0.72, -W * 0.2, H * 0.32);
		R.EyeR = FVector(D * 0.72, W * 0.2, H * 0.32);
		const double LegX[4] = { 0.55, 0.2, -0.15, -0.5 };
		const float Splay[4] = { 25.f, 8.f, -8.f, -25.f };
		for (int32 k = 0; k < 4; ++k)
		{
			R.LegPivot[k] = FVector(D * LegX[k], -W * 0.78, -H * 0.1);
			R.LegPivot[k + 4] = FVector(D * LegX[k], W * 0.78, -H * 0.1);
			// Las de delante apuntan hacia delante y las de atrás hacia atrás (el signo depende del lado).
			R.LegSplay[k] = Splay[k];
			R.LegSplay[k + 4] = -Splay[k];
		}
		R.BigShoulder = FVector(D * 0.78, W * 0.5, -H * 0.05);
		R.BigElbow = FVector(U * 0.5, 0.0, H * 0.3);
		R.BigKnuckle = FVector(U * 0.95, 0.0, H * 0.3);
		R.SmallShoulder = FVector(D * 0.78, -W * 0.48, -H * 0.05);
		R.Mouth = FVector(D * 1.0, 0.0, -H * 0.15);
		// Punta del dedo fijo en reposo, desde el hombro.
		const FVector Tip = R.BigElbow + FVector(U * 1.8, 0.0, -H * 0.5);
		const double Len = Tip.Size();
		const double E0 = FMath::Atan2(Tip.Z, Tip.X);
		const double ShoulderH = R.BodyZ + R.BigShoulder.Z;
		const double Elev = FMath::Asin(FMath::Clamp((H * 0.3 - ShoulderH) / Len, -1.0, 1.0));
		R.SlamPitch = FMath::RadiansToDegrees(Elev - E0);
		const double Flat = Len * FMath::Cos(Elev);
		const double Yaw = FMath::Asin(FMath::Clamp(-R.BigShoulder.Y / FMath::Max(1.0, Flat), -1.0, 1.0));
		R.ArmYaw = FMath::RadiansToDegrees(Yaw);
		R.Reach = R.BigShoulder.X + Flat * FMath::Cos(Yaw);
		R.BodyRadius = W * 1.05;
		return R;
	}

	inline void BuildCrabBody(FTNProcMeshBuffers& M, const FCrabLook& L)
	{
		const double S = TNBeach::Scale;
		const double W = CrabW * S, D = CrabD * S, H = CrabH * S;
		TNFauna::TNFaunaBlob(M, FVector::ZeroVector, FVector(D, W, H * 0.55), L.ShellTop, L.Shell, 10, 4);
		TNFauna::TNFaunaBlob(M, FVector(0.0, 0.0, -H * 0.18), FVector(D * 0.86, W * 0.82, H * 0.3), L.Shell, L.Shell * 0.9f, 8, 3);
		// Bultos del caparazón, algo más claros.
		const double BumpX[5] = { 0.35, 0.05, -0.3, 0.1, -0.15 };
		const double BumpY[5] = { 0.0, 0.35, 0.15, -0.35, -0.2 };
		for (int32 k = 0; k < 5; ++k)
		{
			const double Rel = FMath::Square(BumpX[k]) + FMath::Square(BumpY[k]);
			const double Z = H * 0.55 * FMath::Sqrt(FMath::Max(0.05, 1.0 - Rel)) * 0.96;
			M.AddBox(FVector(D * BumpX[k], W * BumpY[k], Z), FVector::ForwardVector, FVector(D * 0.1, W * 0.08, H * 0.07), L.ShellTop * 1.18f);
		}
		// Boca: placas delante y una raja oscura.
		M.AddBox(FVector(D * 0.9, 0.0, -H * 0.12), FVector::ForwardVector, FVector(D * 0.1, W * 0.26, H * 0.16), L.Shell * 0.8f);
		M.AddBox(FVector(D * 0.97, 0.0, -H * 0.2), FVector::ForwardVector, FVector(D * 0.05, W * 0.14, H * 0.07), Rgb(0.35f, 0.1f, 0.12f));
		// Pinchitos del borde del caparazón.
		for (const double Side : { -1.0, 1.0 })
		{
			TNProcMesh::TNProcAddCylinder(M, FVector(D * 0.55, Side * W * 0.82, H * 0.1), FVector(D * 0.78, Side * W * 1.02, H * 0.22), H * 0.12, H * 0.02, 4, L.ShellTop, true);
			TNProcMesh::TNProcAddCylinder(M, FVector(D * 0.2, Side * W * 0.95, H * 0.1), FVector(D * 0.3, Side * W * 1.12, H * 0.2), H * 0.1, H * 0.02, 4, L.ShellTop, true);
		}
	}

	/** Pedúnculo con su ojo de dibujo (pivote en la base; vale para los dos lados). */
	inline void BuildCrabEye(FTNProcMeshBuffers& M, const FCrabLook& L)
	{
		const double H = CrabH * TNBeach::Scale;
		const FVector Top(H * 0.08, 0.0, H * 0.95);
		M.AddBeam(FVector::ZeroVector, Top, H * 0.09, L.ShellTop);
		const FVector Ball = Top + FVector(0.0, 0.0, H * 0.18);
		TNFauna::TNFaunaBlob(M, Ball, FVector(H * 0.26, H * 0.24, H * 0.26), Rgb(0.97f, 0.97f, 0.95f), Rgb(0.9f, 0.9f, 0.88f), 7, 3);
		M.AddBox(Ball + FVector(H * 0.22, 0.0, H * 0.02), FVector::ForwardVector, FVector(H * 0.07, H * 0.12, H * 0.14), Rgb(0.03f, 0.03f, 0.04f));
		M.AddBox(Ball + FVector(H * 0.26, H * 0.04, H * 0.09), FVector::ForwardVector, FVector(H * 0.03, H * 0.04, H * 0.04), Rgb(0.98f, 0.98f, 0.98f));
		// Ceja gruñona.
		M.AddBeam(Ball + FVector(H * 0.16, -H * 0.18, H * 0.28), Ball + FVector(H * 0.2, H * 0.18, H * 0.2), H * 0.045, L.ShellTop * 0.5f);
	}

	/** Pata de dos tramos hacia el lado Side (+1 derecha); pivote en el borde del cuerpo, el pie en el suelo. */
	inline void BuildCrabLeg(FTNProcMeshBuffers& M, const FCrabLook& L, double Side)
	{
		const double S = TNBeach::Scale;
		const double W = CrabW * S, H = CrabH * S;
		const double PivotHeight = CrabH * 1.25 * S - H * 0.1;
		const FVector Knee(0.0, Side * W * 0.5, H * 0.75);
		const FVector Ankle(0.0, Side * W * 1.02, -PivotHeight + H * 0.28);
		const FVector Foot(0.0, Side * W * 1.08, -PivotHeight);
		M.AddBeam(FVector::ZeroVector, Knee, H * 0.15, L.Leg);
		TNFauna::TNFaunaBlob(M, Knee, FVector(H * 0.2, H * 0.2, H * 0.2), L.Leg, L.Leg * 0.9f, 6, 3);
		M.AddBeam(Knee, Ankle, H * 0.11, L.Leg * 0.92f);
		TNProcMesh::TNProcAddCylinder(M, Ankle, Foot, H * 0.1, H * 0.02, 4, L.Tip, true);
	}

	/** Mano de la pinza grande (pivote en el codo): palma enorme, dedo fijo con dientes. */
	inline void BuildCrabBigHand(FTNProcMeshBuffers& M, const FCrabLook& L)
	{
		const double S = TNBeach::Scale;
		const double H = CrabH * S, U = CrabD * 1.5 * S;
		TNFauna::TNFaunaBlob(M, FVector(U * 0.55, 0.0, 0.0), FVector(U * 0.55, H * 0.45, H * 0.6), L.Big, L.Big * 0.85f, 9, 4);
		const FVector F0(U * 0.9, 0.0, -H * 0.25);
		const FVector F1(U * 1.8, 0.0, -H * 0.5);
		TNProcMesh::TNProcAddCylinder(M, F0, F1, H * 0.32, H * 0.05, 5, L.Tip, true);
		for (int32 k = 0; k < 4; ++k)
		{
			const FVector At = FMath::Lerp(F0, F1, 0.15 + 0.2 * k) + FVector(0.0, 0.0, H * (0.26 - 0.05 * k));
			M.AddBox(At, FVector::ForwardVector, FVector(H * 0.06, H * 0.1, H * 0.07), Rgb(0.99f, 0.98f, 0.94f));
		}
	}

	/** Dedo móvil (pivote en el nudillo): dos tramos que se curvan hacia abajo. */
	inline void BuildCrabBigFinger(FTNProcMeshBuffers& M, const FCrabLook& L)
	{
		const double S = TNBeach::Scale;
		const double H = CrabH * S, U = CrabD * 1.5 * S;
		const FVector Mid(U * 0.45, 0.0, -H * 0.05);
		const FVector End(U * 0.9, 0.0, -H * 0.32);
		TNProcMesh::TNProcAddCylinder(M, FVector::ZeroVector, Mid, H * 0.28, H * 0.18, 5, L.Tip, true);
		TNProcMesh::TNProcAddCylinder(M, Mid, End, H * 0.18, H * 0.04, 5, L.Tip * 0.95f, true);
		for (int32 k = 0; k < 3; ++k)
		{
			const FVector At = FMath::Lerp(FVector::ZeroVector, Mid, 0.4 + 0.3 * k) - FVector(0.0, 0.0, H * 0.2);
			M.AddBox(At, FVector::ForwardVector, FVector(H * 0.05, H * 0.08, H * 0.06), Rgb(0.99f, 0.98f, 0.94f));
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Quad gigante (medidas reales en cm; X adelante, Y derecha, Z arriba; origen en el suelo bajo su centro)
	// ─────────────────────────────────────────────────────────────────────────

	constexpr double QuadWheelR = 30.0;
	constexpr double QuadWheelHalfW = 12.0;
	constexpr double QuadTrackHalf = 31.0;
	constexpr double QuadBaseHalf = 62.5;
	constexpr double QuadLength = 200.0;

	struct FQuadLook
	{
		FLinearColor Body = Rgb(0.86f, 0.12f, 0.1f);
		FLinearColor Frame = Rgb(0.16f, 0.16f, 0.18f);
		FLinearColor Tire = Rgb(0.1f, 0.1f, 0.11f);
		FLinearColor Rim = Rgb(0.86f, 0.86f, 0.88f);
		FLinearColor Seat = Rgb(0.08f, 0.08f, 0.09f);
		FLinearColor Shirt = Rgb(0.2f, 0.62f, 0.86f);
		FLinearColor Pants = Rgb(0.18f, 0.26f, 0.5f);
		FLinearColor Helmet = Rgb(1.f, 0.85f, 0.1f);
		FLinearColor Skin = Rgb(0.93f, 0.72f, 0.56f);
	};

	inline FQuadLook QuadPalette(int32 Index)
	{
		FQuadLook L;
		switch (((Index % 5) + 5) % 5)
		{
		case 1: L.Body = Rgb(1.f, 0.78f, 0.1f); L.Shirt = Rgb(0.9f, 0.2f, 0.2f); L.Helmet = Rgb(0.1f, 0.1f, 0.12f); L.Skin = Rgb(0.62f, 0.44f, 0.3f); break;
		case 2: L.Body = Rgb(0.12f, 0.35f, 0.85f); L.Shirt = Rgb(1.f, 0.6f, 0.1f); L.Helmet = Rgb(0.95f, 0.95f, 0.95f); L.Rim = Rgb(1.f, 0.8f, 0.1f); break;
		case 3: L.Body = Rgb(0.2f, 0.6f, 0.2f); L.Shirt = Rgb(0.95f, 0.95f, 0.9f); L.Helmet = Rgb(0.85f, 0.12f, 0.1f); L.Skin = Rgb(0.4f, 0.27f, 0.18f); break;
		case 4: L.Body = Rgb(1.f, 0.45f, 0.08f); L.Shirt = Rgb(0.3f, 0.75f, 0.35f); L.Helmet = Rgb(0.2f, 0.4f, 0.9f); break;
		default: break;
		}
		return L;
	}

	/**
	 * Rueda con el eje en Y centrada en su origen: neumático con tacos y llanta con buje por las dos caras.
	 *
	 * Cerrada por las dos caras: la banda de rodadura no lleva tapas (las cierran los flancos), y cada flanco (cono de R a 0,9 R)
	 * termina en un hombro plano que llega hasta la llanta. Sin ese hombro quedaba un anillo abierto entre el flanco y la
	 * llanta y, como el material es de una cara, desde fuera se veía a través de la rueda (y su interior, sin pintar). Los
	 * tacos llevan tapa en la punta por lo mismo.
	 */
	inline void BuildQuadWheel(FTNProcMeshBuffers& M, const FQuadLook& L)
	{
		const double S = TNBeach::Scale;
		const double R = QuadWheelR * S;
		const double Hw = QuadWheelHalfW * S;
		constexpr int32 TireSeg = 18;
		// Corona plana entre dos radios en el plano Y, mirando hacia Facing (los vértices coinciden con los del cono de 18 lados).
		auto AddRing = [&M](double Y, double RIn, double ROut, const FVector& Facing, const FLinearColor& Color)
		{
			for (int32 k = 0; k < TireSeg; ++k)
			{
				const double A0 = TNProcMap::TwoPi * k / TireSeg;
				const double A1 = TNProcMap::TwoPi * (k + 1) / TireSeg;
				M.AddQuad(FVector(FMath::Cos(A0) * ROut, Y, FMath::Sin(A0) * ROut), FVector(FMath::Cos(A1) * ROut, Y, FMath::Sin(A1) * ROut),
					FVector(FMath::Cos(A1) * RIn, Y, FMath::Sin(A1) * RIn), FVector(FMath::Cos(A0) * RIn, Y, FMath::Sin(A0) * RIn), Facing, Color);
			}
		};
		TNProcMesh::TNProcAddCylinder(M, FVector(0.0, -Hw, 0.0), FVector(0.0, Hw, 0.0), R, R, TireSeg, L.Tire, false);
		for (const double Side : { -1.0, 1.0 })
		{
			TNProcMesh::TNProcAddCylinder(M, FVector(0.0, Side * Hw, 0.0), FVector(0.0, Side * (Hw + 1.5 * S), 0.0), R, R * 0.9, TireSeg, L.Tire * 0.85f, false);
			// Hombro: del flanco (0,9 R) hacia dentro, por debajo de la llanta (0,5 R < radio de la llanta de 12 lados).
			AddRing(Side * (Hw + 1.5 * S), R * 0.5, R * 0.9, FVector(0.0, Side, 0.0), L.Tire * 0.8f);
			TNProcMesh::TNProcAddCylinder(M, FVector(0.0, Side * (Hw + 1.4 * S), 0.0), FVector(0.0, Side * (Hw + 2.0 * S), 0.0), R * 0.62, R * 0.58, 12, L.Rim, true);
			TNProcMesh::TNProcAddCylinder(M, FVector(0.0, Side * (Hw + 1.9 * S), 0.0), FVector(0.0, Side * (Hw + 3.2 * S), 0.0), R * 0.2, R * 0.16, 8, L.Frame, true);
			for (int32 b = 0; b < 5; ++b)
			{
				const double A = TNProcMap::TwoPi * b / 5.0;
				M.AddBox(FVector(FMath::Cos(A) * R * 0.38, Side * (Hw + 2.2 * S), FMath::Sin(A) * R * 0.38), FVector::ForwardVector, FVector(1.2 * S, 0.5 * S, 1.2 * S), L.Frame);
			}
		}
		// Tacos en dos filas alternas: viga cuadrada (AddBeam no lleva tapas) con su tapa en la punta; la base queda dentro de la banda.
		constexpr int32 Knobs = 18;
		const double KnobHalf = 2.4 * S;
		const FLinearColor KnobColor = L.Tire * 0.75f;
		for (int32 k = 0; k < Knobs; ++k)
		{
			for (int32 Row = 0; Row < 2; ++Row)
			{
				const double A = TNProcMap::TwoPi * (k + 0.5 * Row) / Knobs;
				const FVector Dir(FMath::Cos(A), 0.0, FMath::Sin(A));
				const FVector Off(0.0, (Row == 0 ? -0.45 : 0.45) * Hw, 0.0);
				const FVector Tip = Dir * (R + 2.6 * S) + Off;
				M.AddBeam(Dir * (R * 0.97) + Off, Tip, KnobHalf, KnobColor);
				// Mismo marco que AddBeam: eje = Dir, Y = arriba x eje (o el eje Y del mundo si es vertical), Z = eje x Y.
				FVector KnobY = FVector::CrossProduct(FVector::UpVector, Dir);
				if (KnobY.SizeSquared() < 1e-6)
				{
					KnobY = FVector(0.0, 1.0, 0.0);
				}
				KnobY.Normalize();
				const FVector KnobZ = FVector::CrossProduct(Dir, KnobY);
				M.AddQuad(Tip + (KnobY + KnobZ) * KnobHalf, Tip + (KnobZ - KnobY) * KnobHalf, Tip + (-KnobY - KnobZ) * KnobHalf, Tip + (KnobY - KnobZ) * KnobHalf, Dir,
					KnobColor * 1.15f);
			}
		}
	}

	/** Cuerpo del quad con el piloto (todo menos las ruedas). */
	inline void BuildQuadBody(FTNProcMeshBuffers& M, const FQuadLook& L)
	{
		const double S = TNBeach::Scale;
		auto V = [S](double X, double Y, double Z) { return FVector(X * S, Y * S, Z * S); };
		const FLinearColor Dark = L.Frame;
		const FLinearColor Spring = Rgb(0.95f, 0.78f, 0.1f);
		// Chasis.
		for (const double Side : { -1.0, 1.0 })
		{
			M.AddBeam(V(-70.0, Side * 16.0, 30.0), V(70.0, Side * 16.0, 30.0), 2.5 * S, Dark);
			M.AddBeam(V(55.0, Side * 16.0, 32.0), V(62.5, Side * 19.0, 30.0), 2.2 * S, Dark);
			M.AddBeam(V(70.0, Side * 16.0, 34.0), V(62.5, Side * 19.0, 30.0), 2.2 * S, Dark);
			M.AddBeam(V(55.0, Side * 16.0, 58.0), V(62.5, Side * 19.0, 34.0), 2.4 * S, Spring);
			M.AddBeam(V(-30.0, Side * 12.0, 34.0), V(-62.5, Side * 19.0, 30.0), 2.5 * S, Dark);
			M.AddBox(V(0.0, Side * 28.0, 34.0), FVector::ForwardVector, V(18.0, 7.0, 1.5), Dark * 1.3f);
		}
		for (const double X : { -60.0, 0.0, 60.0 })
		{
			M.AddBeam(V(X, -16.0, 30.0), V(X, 16.0, 30.0), 2.2 * S, Dark);
		}
		M.AddBeam(V(-62.5, -19.0, 30.0), V(-62.5, 19.0, 30.0), 2.8 * S, Dark);
		// Motor, carrocería, depósito, paneles y asiento.
		M.AddBox(V(0.0, 0.0, 42.0), FVector::ForwardVector, V(22.0, 15.0, 12.0), Dark * 1.4f);
		M.AddBox(V(10.0, 0.0, 60.0), FVector::ForwardVector, V(40.0, 24.0, 9.0), L.Body);
		M.AddBox(V(25.0, 0.0, 72.0), FVector::ForwardVector, V(16.0, 15.0, 6.0), L.Body * 1.08f);
		for (const double Side : { -1.0, 1.0 })
		{
			M.AddBox(V(-20.0, Side * 22.0, 58.0), FVector::ForwardVector, V(22.0, 3.0, 10.0), L.Body * 0.9f);
		}
		M.AddBox(V(-30.0, 0.0, 76.0), FVector::ForwardVector, V(27.0, 14.0, 5.0), L.Seat);
		// Guardabarros sobre las cuatro ruedas.
		for (const double Side : { -1.0, 1.0 })
		{
			for (const double Front : { -1.0, 1.0 })
			{
				AddPlate(M, V(Front * 38.0, Side * 31.0, 56.0), V(Front * 62.5, Side * 31.0, 68.0), FVector::RightVector, 32.0 * S, 2.5 * S, L.Body);
				AddPlate(M, V(Front * 62.5, Side * 31.0, 68.0), V(Front * 90.0, Side * 31.0, 58.0), FVector::RightVector, 32.0 * S, 2.5 * S, L.Body * 0.95f);
			}
		}
		// Parachoques, faros y rejilla.
		M.AddBeam(V(95.0, -30.0, 40.0), V(95.0, 30.0, 40.0), 2.5 * S, Dark);
		for (const double Side : { -1.0, 1.0 })
		{
			M.AddBeam(V(95.0, Side * 20.0, 40.0), V(85.0, Side * 16.0, 32.0), 2.0 * S, Dark);
			M.AddBox(V(93.0, Side * 12.0, 64.0), FVector::ForwardVector, V(3.0, 6.0, 4.0), Rgb(1.f, 0.95f, 0.7f));
		}
		M.AddBox(V(94.0, 0.0, 56.0), FVector::ForwardVector, V(2.0, 16.0, 6.0), Dark);
		// Portaequipajes delante y detrás.
		for (const double Front : { -1.0, 1.0 })
		{
			for (const double Side : { -1.0, 1.0 })
			{
				M.AddBeam(V(Front * 62.0, Side * 26.0, 76.0), V(Front * 94.0, Side * 26.0, 76.0), 1.3 * S, Dark);
				M.AddBeam(V(Front * 66.0, Side * 26.0, 76.0), V(Front * 60.0, Side * 22.0, 66.0), 1.2 * S, Dark);
			}
			for (int32 k = 0; k < 5; ++k)
			{
				const double X = Front * (62.0 + 8.0 * k);
				M.AddBeam(V(X, -26.0, 76.0), V(X, 26.0, 76.0), 1.1 * S, Dark);
			}
		}
		// Manillar.
		M.AddBeam(V(46.0, 0.0, 66.0), V(50.0, 0.0, 92.0), 2.4 * S, Dark);
		M.AddBeam(V(50.0, -38.0, 94.0), V(50.0, 38.0, 94.0), 2.0 * S, Dark * 1.2f);
		for (const double Side : { -1.0, 1.0 })
		{
			M.AddBeam(V(50.0, Side * 30.0, 94.0), V(50.0, Side * 40.0, 94.0), 2.6 * S, L.Seat);
		}
		// Escape.
		TNProcMesh::TNProcAddCylinder(M, V(-40.0, 16.0, 46.0), V(-88.0, 18.0, 52.0), 3.0 * S, 3.0 * S, 8, Rgb(0.7f, 0.7f, 0.72f), true);
		TNProcMesh::TNProcAddCylinder(M, V(-88.0, 18.0, 52.0), V(-92.0, 18.4, 52.5), 3.3 * S, 3.3 * S, 8, Rgb(0.12f, 0.12f, 0.12f), true);
		// Piloto: piernas, torso, brazos al manillar y casco.
		TNFauna::TNFaunaBlob(M, V(-28.0, 0.0, 88.0), V(14.0, 17.0, 10.0), L.Pants, L.Pants * 0.9f, 8, 3);
		for (const double Side : { -1.0, 1.0 })
		{
			TNProcMesh::TNProcAddCylinder(M, V(-24.0, Side * 11.0, 86.0), V(12.0, Side * 20.0, 76.0), 8.0 * S, 7.0 * S, 8, L.Pants, true);
			TNProcMesh::TNProcAddCylinder(M, V(12.0, Side * 20.0, 76.0), V(6.0, Side * 28.0, 40.0), 6.5 * S, 5.5 * S, 8, L.Pants * 0.95f, true);
			M.AddBox(V(10.0, Side * 28.0, 37.0), FVector::ForwardVector, V(11.0, 5.0, 5.0), Rgb(0.25f, 0.15f, 0.08f));
			TNProcMesh::TNProcAddCylinder(M, V(-12.0, Side * 21.0, 132.0), V(18.0, Side * 31.0, 112.0), 5.5 * S, 5.0 * S, 7, L.Shirt, true);
			TNProcMesh::TNProcAddCylinder(M, V(18.0, Side * 31.0, 112.0), V(48.0, Side * 34.0, 97.0), 4.5 * S, 4.0 * S, 7, L.Skin, true);
			M.AddBox(V(50.0, Side * 34.0, 95.0), FVector::ForwardVector, V(5.0, 4.0, 4.0), L.Seat);
		}
		TNFauna::TNFaunaBlob(M, V(-18.0, 0.0, 116.0), V(14.0, 19.0, 28.0), L.Shirt, L.Shirt * 0.9f, 9, 4);
		TNProcMesh::TNProcAddCylinder(M, V(-16.0, 0.0, 140.0), V(-14.0, 0.0, 150.0), 6.0 * S, 6.0 * S, 7, L.Skin, false);
		TNFauna::TNFaunaBlob(M, V(-12.0, 0.0, 160.0), V(15.0, 13.0, 14.0), L.Helmet, L.Helmet * 0.9f, 9, 4);
		M.AddBox(V(1.0, 0.0, 158.0), FVector::ForwardVector, V(4.0, 10.0, 5.0), Rgb(0.08f, 0.1f, 0.14f));
		M.AddBox(V(-12.0, 0.0, 173.0), FVector::ForwardVector, V(14.0, 2.5, 1.5), Rgb(0.97f, 0.97f, 0.97f));
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Gaviotas y pelícanos: el pico que se abre (medidas de la fauna, se escalan con el pájaro)
	// ─────────────────────────────────────────────────────────────────────────

	/**
	 * Dónde van el cuerpo, la cabeza y el pico del ave de la fauna (TNFaunaBuildBird con las medidas de la gaviota y del
	 * pelícano de TNFaunaBuildSpecies: si cambian allí, hay que cambiarlas aquí). BodyPivot en el espacio de la raíz del
	 * pájaro, HeadPivot en el del cuerpo y el pico en el de la cabeza (su pivote es la base del cuello).
	 */
	struct FBirdGeom
	{
		FVector BodyPivot = FVector::ZeroVector;
		FVector HeadPivot = FVector::ZeroVector;
		FVector BeakBase = FVector::ZeroVector;
		FVector BeakTip = FVector::ZeroVector;
		double BeakR = 1.4;
		/** Punto del pico que sujeta a la tortuga (espacio de la cabeza): entre la mitad y la punta, abajo. */
		FVector Grip = FVector::ZeroVector;
		FLinearColor BeakC = FLinearColor::White;
		FLinearColor BeakTipC = FLinearColor::White;
	};

	inline FBirdGeom BirdGeom(bool bPelican)
	{
		// Gaviota: Len 16, Girth 8, Leg 9, Neck 4 (NeckFwd 0,3), Head 5,5, Beak 6, BeakR 1,4.
		// Pelícano: Len 32, Girth 17, Leg 14, Neck 18 (NeckFwd 0,2), Head 8, Beak 34, BeakR 3, BeakDroop 0,1.
		const double Len = bPelican ? 32.0 : 16.0;
		const double Girth = bPelican ? 17.0 : 8.0;
		const double Leg = bPelican ? 14.0 : 9.0;
		const double Neck = bPelican ? 18.0 : 4.0;
		const double NeckFwd = bPelican ? 0.2 : 0.3;
		const double Head = bPelican ? 8.0 : 5.5;
		const double Beak = bPelican ? 34.0 : 6.0;
		const double Droop = bPelican ? 0.1 : 0.0;
		FBirdGeom G;
		G.BeakR = bPelican ? 3.0 : 1.4;
		G.BodyPivot = FVector(0.0, 0.0, Leg + Girth * 0.45);
		G.HeadPivot = FVector(Len * 0.7, 0.0, Girth * 0.45);
		const FVector NeckTop(Neck * NeckFwd, 0.0, Neck);
		const FVector HeadAt = NeckTop + FVector(Head * 0.25, 0.0, Head * 0.35);
		G.BeakBase = HeadAt + FVector(Head * 0.95, 0.0, -Head * 0.1);
		const FVector Mid = G.BeakBase + FVector(Beak * 0.5, 0.0, -Beak * 0.1 * Droop);
		G.BeakTip = Mid + FVector(Beak * 0.5 * (1.0 - 0.45 * Droop), 0.0, -Beak * 0.42 * Droop);
		G.Grip = FMath::Lerp(G.BeakBase, G.BeakTip, bPelican ? 0.62 : 0.72) - FVector(0.0, 0.0, G.BeakR * 0.6);
		G.BeakC = bPelican ? Rgb(1.f, 0.78f, 0.25f) : Rgb(1.f, 0.8f, 0.15f);
		G.BeakTipC = bPelican ? Rgb(0.95f, 0.5f, 0.15f) : Rgb(0.9f, 0.2f, 0.1f);
		return G;
	}

	/**
	 * Piezas del ave de la fauna que abre el pico (las gaviotas y el pelícano de la zona de gaviotas, la gaviota justiciera
	 * y el pelícano taxi): la cabeza lleva la mitad de arriba del pico y Jaw, la de abajo (con la bolsa del pelícano), con
	 * su pivote en BirdGeom(bPelican).BeakBase. Cerrado es un solo pico; girando Jaw hacia abajo se abre. Antes iba el pico
	 * entero en la cabeza y una mandíbula suelta debajo, que se veía como un segundo pico mal puesto (#738).
	 */
	inline void BuildBirdParts(bool bPelican, TArray<TNFauna::FTNFaunaPart>& Parts, TNFauna::FTNFaunaRig& Rig, TNFauna::FTNFaunaBirdJaw& Jaw)
	{
		TNFauna::TNFaunaBuildSpecies(bPelican ? TNFauna::ETNFaunaSpecies::Pelican : TNFauna::ETNFaunaSpecies::Gull, Parts, Rig, &Jaw);
	}

	/**
	 * Nombres en la caché de mallas (TNBeachKit::CachedMesh) de la pieza Index de BuildBirdParts y de su pico de abajo. No
	 * son los de las piezas de la gaviota de TNFaunaBuildSpecies sin más (Beach.Gull.N, la de ATN_EnemySeagull, con el pico
	 * entero): la cabeza es otra.
	 */
	inline FString BirdPartKey(bool bPelican, int32 Index)
	{
		return FString::Printf(TEXT("Beach.%s.Hinged.%d"), bPelican ? TEXT("Pelican") : TEXT("Gull"), Index);
	}

	inline FString BirdJawKey(bool bPelican)
	{
		return bPelican ? TEXT("Beach.Pelican.Jaw") : TEXT("Beach.Gull.Jaw");
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Gaviotas: la cagada y su mancha (cm de juego)
	// ─────────────────────────────────────────────────────────────────────────

	inline void BuildDropping(FTNProcMeshBuffers& M)
	{
		// Pegote blanco con su punta al caer (hacia arriba) y los grumos oscuros: bien visible desde lejos.
		TNFauna::TNFaunaBlob(M, FVector::ZeroVector, FVector(55.0, 55.0, 62.0), Rgb(0.98f, 0.98f, 0.95f), Rgb(0.9f, 0.9f, 0.86f), 8, 4);
		TNFauna::TNFaunaBlob(M, FVector(0.0, 0.0, 70.0), FVector(26.0, 26.0, 42.0), Rgb(0.98f, 0.98f, 0.95f), Rgb(0.92f, 0.92f, 0.88f), 6, 3);
		TNFauna::TNFaunaBlob(M, FVector(18.0, 14.0, 30.0), FVector(18.0, 16.0, 16.0), Rgb(0.45f, 0.42f, 0.36f), Rgb(0.4f, 0.38f, 0.32f), 6, 3);
		TNFauna::TNFaunaBlob(M, FVector(-20.0, -10.0, 10.0), FVector(14.0, 12.0, 12.0), Rgb(0.55f, 0.52f, 0.45f), Rgb(0.5f, 0.48f, 0.4f), 6, 3);
	}

	/** Mancha blanca en la arena, radio ~100 cm (se escala al usarla). */
	inline void BuildSplat(FTNProcMeshBuffers& M, uint32 Seed)
	{
		const FLinearColor White = Rgb(0.98f, 0.98f, 0.95f);
		constexpr int32 Pts = 16;
		FVector Rim[Pts];
		for (int32 k = 0; k < Pts; ++k)
		{
			const double A = TNProcMap::TwoPi * k / Pts;
			const double R = 100.0 * (0.62 + 0.42 * (0.5 + 0.5 * TNProcMesh::TNProcHashNoise(k, 1, Seed)));
			Rim[k] = FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0.0);
		}
		const FVector Top(0.0, 0.0, 6.0);
		for (int32 k = 0; k < Pts; ++k)
		{
			const FVector& A = Rim[k];
			const FVector& B = Rim[(k + 1) % Pts];
			M.AddTri(Top, A + FVector(0.0, 0.0, 3.0), B + FVector(0.0, 0.0, 3.0), FVector::UpVector, White);
			M.AddQuad(A, B, B + FVector(0.0, 0.0, 3.0), A + FVector(0.0, 0.0, 3.0), (A + B) * 0.5, White * 0.9f);
		}
		for (int32 d = 0; d < 6; ++d)
		{
			const double A = TNProcMap::TwoPi * (d + 0.3 * TNProcMesh::TNProcHashNoise(d, 7, Seed)) / 6.0;
			const double R = 125.0 + 45.0 * (0.5 + 0.5 * TNProcMesh::TNProcHashNoise(d, 9, Seed));
			const double Size = 10.0 + 9.0 * (0.5 + 0.5 * TNProcMesh::TNProcHashNoise(d, 11, Seed));
			TNFauna::TNFaunaBlob(M, FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 2.0), FVector(Size, Size, Size * 0.35), White, White * 0.92f, 6, 3);
		}
	}

	/** Pegote blanco que se queda sobre el caparazón de la tortuga manchada (cm de juego). */
	inline void BuildShellSplat(FTNProcMeshBuffers& M)
	{
		const FLinearColor White = Rgb(0.98f, 0.98f, 0.95f);
		TNFauna::TNFaunaBlob(M, FVector::ZeroVector, FVector(34.0, 30.0, 9.0), White, White * 0.9f, 8, 3);
		TNFauna::TNFaunaBlob(M, FVector(22.0, 18.0, -4.0), FVector(8.0, 7.0, 10.0), White, White * 0.9f, 6, 3);
		TNFauna::TNFaunaBlob(M, FVector(-20.0, -16.0, -6.0), FVector(7.0, 6.0, 12.0), White, White * 0.9f, 6, 3);
		TNFauna::TNFaunaBlob(M, FVector(8.0, -6.0, 7.0), FVector(10.0, 9.0, 5.0), Rgb(0.45f, 0.42f, 0.36f), Rgb(0.4f, 0.38f, 0.32f), 6, 3);
	}

	/**
	 * Signo de exclamación de aviso (cm de juego) sobre la tortuga a la que va la cagada: barra y punto amarillos con borde
	 * rojo oscuro, planos en YZ (mira a +X), de 105 cm de alto con el pie en Z = 0. Sin caras solapadas (el borde es un
	 * anillo aparte del relleno) para que no se pisen al ser translúcido. Se hace mirar a la cámara al usarlo.
	 */
	inline void BuildWarningMark(FTNProcMeshBuffers& M)
	{
		const FLinearColor Fill = Rgb(1.f, 0.86f, 0.08f);
		const FLinearColor Rim = Rgb(0.78f, 0.06f, 0.04f);
		const FVector Front = FVector::ForwardVector;
		auto P = [](double Y, double Z) { return FVector(0.0, Y, Z); };
		// Barra: trapecio ancho arriba y estrecho abajo, con su borde.
		const FVector BarIn[4] = { P(-11.0, 100.0), P(11.0, 100.0), P(6.5, 35.0), P(-6.5, 35.0) };
		const FVector BarOut[4] = { P(-15.5, 104.5), P(15.5, 104.5), P(10.5, 30.5), P(-10.5, 30.5) };
		M.AddQuad(BarIn[0], BarIn[1], BarIn[2], BarIn[3], Front, Fill);
		for (int32 k = 0; k < 4; ++k)
		{
			const int32 Next = (k + 1) % 4;
			M.AddQuad(BarOut[k], BarOut[Next], BarIn[Next], BarIn[k], Front, Rim);
		}
		// Punto: disco de 12 lados con su borde.
		constexpr int32 Seg = 12;
		const FVector Center = P(0.0, 14.0);
		for (int32 k = 0; k < Seg; ++k)
		{
			const double A0 = TNProcMap::TwoPi * k / Seg;
			const double A1 = TNProcMap::TwoPi * (k + 1) / Seg;
			const FVector I0 = P(FMath::Cos(A0) * 8.5, 14.0 + FMath::Sin(A0) * 8.5);
			const FVector I1 = P(FMath::Cos(A1) * 8.5, 14.0 + FMath::Sin(A1) * 8.5);
			const FVector O0 = P(FMath::Cos(A0) * 13.0, 14.0 + FMath::Sin(A0) * 13.0);
			const FVector O1 = P(FMath::Cos(A1) * 13.0, 14.0 + FMath::Sin(A1) * 13.0);
			M.AddTri(Center, I0, I1, Front, Fill);
			M.AddQuad(O0, O1, I1, I0, Front, Rim);
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Tormenta de bañistas: trastos que vuelan y piernas que pisan (medidas reales a escala)
	// ─────────────────────────────────────────────────────────────────────────

	enum class EStormItem : uint8 { Umbrella, Bucket, Chair, Towel, Float, Spade, FlipFlop, Ball, Count };

	/** Colores de plástico de playa (se eligen dos por trasto). */
	inline FLinearColor BeachColor(int32 Index)
	{
		static const FLinearColor Colors[8] = {
			Rgb(0.95f, 0.2f, 0.18f), Rgb(1.f, 0.82f, 0.12f), Rgb(0.15f, 0.5f, 0.95f), Rgb(0.2f, 0.75f, 0.35f),
			Rgb(1.f, 0.5f, 0.1f), Rgb(0.95f, 0.4f, 0.7f), Rgb(0.2f, 0.8f, 0.85f), Rgb(0.98f, 0.98f, 0.96f) };
		return Colors[((Index % 8) + 8) % 8];
	}

	/** Un trasto centrado cerca de su centro de masas (para que gire bien al volar). */
	inline void BuildStormItem(FTNProcMeshBuffers& M, EStormItem Item, int32 Variant)
	{
		const double S = TNBeach::Scale;
		auto V = [S](double X, double Y, double Z) { return FVector(X * S, Y * S, Z * S); };
		const FLinearColor A = BeachColor(Variant);
		const FLinearColor B = BeachColor(Variant + 3);
		const FLinearColor White = BeachColor(7);
		switch (Item)
		{
		case EStormItem::Umbrella:
		{
			// Sombrilla de 1,8 m de diámetro: gajos de dos colores, faldón y palo (centrada en el palo, a media altura).
			constexpr int32 Seg = 8;
			const FVector Apex = V(0.0, 0.0, 60.0);
			for (int32 k = 0; k < Seg; ++k)
			{
				const double A0 = TNProcMap::TwoPi * k / Seg;
				const double A1 = TNProcMap::TwoPi * (k + 1) / Seg;
				const FVector R0 = V(FMath::Cos(A0) * 90.0, FMath::Sin(A0) * 90.0, 34.0);
				const FVector R1 = V(FMath::Cos(A1) * 90.0, FMath::Sin(A1) * 90.0, 34.0);
				const FLinearColor C = (k % 2 == 0) ? A : White;
				M.AddTri(Apex, R0, R1, FVector::UpVector, C);
				M.AddTri(Apex, R0, R1, -FVector::UpVector, C * 0.75f);
				const FVector Mid = (R0 + R1) * 0.5;
				M.AddTri(R0, R1, Mid - V(0.0, 0.0, 8.0), Mid, C * 0.95f);
				M.AddTri(R0, R1, Mid - V(0.0, 0.0, 8.0), -Mid, C * 0.7f);
			}
			TNProcMesh::TNProcAddCylinder(M, V(0.0, 0.0, 62.0), V(0.0, 0.0, -145.0), 1.6 * S, 1.6 * S, 6, White * 0.9f, true);
			TNFauna::TNFaunaBlob(M, V(0.0, 0.0, 64.0), V(3.0, 3.0, 3.0), A, A * 0.9f, 6, 3);
			break;
		}
		case EStormItem::Bucket:
		{
			// Cubo de 18 cm: paredes de dos caras, fondo y asa.
			AddOpenTube(M, V(0.0, 0.0, -9.0), V(0.0, 0.0, 9.0), 8.0 * S, 11.0 * S, 12, A, A * 0.7f);
			TNProcMesh::TNProcAddCylinder(M, V(0.0, 0.0, -9.5), V(0.0, 0.0, -8.5), 8.2 * S, 8.2 * S, 12, A * 0.9f, true);
			TNProcMesh::TNProcAddCylinder(M, V(0.0, 0.0, 8.0), V(0.0, 0.0, 9.5), 11.3 * S, 11.3 * S, 12, A * 1.1f, false);
			FVector Prev = V(-11.0, 0.0, 8.0);
			for (int32 k = 1; k <= 6; ++k)
			{
				const double T = static_cast<double>(k) / 6.0;
				const FVector Next = V(-11.0 + 22.0 * T, 0.0, 8.0 + 12.0 * FMath::Sin(PI * T));
				M.AddBeam(Prev, Next, 0.6 * S, B);
				Prev = Next;
			}
			break;
		}
		case EStormItem::Chair:
		{
			// Silla de tijera: barras, asiento y respaldo de tela a rayas.
			const FLinearColor Wood = Rgb(0.72f, 0.52f, 0.3f);
			for (const double Side : { -1.0, 1.0 })
			{
				M.AddBeam(V(20.0, Side * 28.0, 0.0), V(28.0, Side * 28.0, -35.0), 1.4 * S, Wood);
				M.AddBeam(V(-20.0, Side * 28.0, -10.0), V(-30.0, Side * 28.0, -35.0), 1.4 * S, Wood);
				M.AddBeam(V(20.0, Side * 28.0, 0.0), V(-35.0, Side * 28.0, 45.0), 1.3 * S, Wood);
				M.AddBeam(V(28.0, Side * 28.0, -35.0), V(-20.0, Side * 28.0, -10.0), 1.2 * S, Wood);
			}
			M.AddBeam(V(20.0, -28.0, 0.0), V(20.0, 28.0, 0.0), 1.3 * S, Wood);
			M.AddBeam(V(-20.0, -28.0, -10.0), V(-20.0, 28.0, -10.0), 1.3 * S, Wood);
			M.AddBeam(V(-35.0, -28.0, 45.0), V(-35.0, 28.0, 45.0), 1.3 * S, Wood);
			for (int32 k = 0; k < 5; ++k)
			{
				const double Y = -22.0 + 11.0 * k;
				const FLinearColor C = (k % 2 == 0) ? A : White;
				AddPlate(M, V(19.0, Y, -1.0), V(-19.0, Y, -11.0), FVector::RightVector, 10.8 * S, 0.8 * S, C);
				AddPlate(M, V(-20.0, Y, -9.0), V(-34.0, Y, 44.0), FVector::RightVector, 10.8 * S, 0.8 * S, C);
			}
			break;
		}
		case EStormItem::Towel:
		{
			// Toalla de 150 x 80 cm ondulada, a rayas, de dos caras.
			constexpr int32 NX = 6, NY = 3;
			auto P = [&](int32 i, int32 j)
			{
				const double X = -75.0 + 150.0 * i / NX;
				const double Y = -40.0 + 80.0 * j / NY;
				return V(X, Y, 6.0 * FMath::Sin(X * 0.07 + j * 0.9) + 3.0 * FMath::Sin(Y * 0.12));
			};
			for (int32 i = 0; i < NX; ++i)
			{
				for (int32 j = 0; j < NY; ++j)
				{
					const FLinearColor C = (i % 2 == 0) ? A : B;
					M.AddQuad(P(i, j), P(i + 1, j), P(i + 1, j + 1), P(i, j + 1), FVector::UpVector, C);
					M.AddQuad(P(i, j), P(i + 1, j), P(i + 1, j + 1), P(i, j + 1), -FVector::UpVector, C * 0.8f);
				}
			}
			break;
		}
		case EStormItem::Float:
		{
			// Flotador de 90 cm a gajos rojos y blancos.
			constexpr int32 Major = 16, Minor = 8;
			const double R = 38.0, Rt = 12.0;
			auto P = [&](int32 i, int32 j)
			{
				const double U = TNProcMap::TwoPi * i / Major;
				const double W = TNProcMap::TwoPi * j / Minor;
				return V((R + Rt * FMath::Cos(W)) * FMath::Cos(U), (R + Rt * FMath::Cos(W)) * FMath::Sin(U), Rt * FMath::Sin(W));
			};
			for (int32 i = 0; i < Major; ++i)
			{
				const double Um = TNProcMap::TwoPi * (i + 0.5) / Major;
				const FVector TubeCenter = V(R * FMath::Cos(Um), R * FMath::Sin(Um), 0.0);
				const FLinearColor C = ((i / 2) % 2 == 0) ? A : White;
				for (int32 j = 0; j < Minor; ++j)
				{
					const FVector Q0 = P(i, j), Q1 = P(i + 1, j), Q2 = P(i + 1, j + 1), Q3 = P(i, j + 1);
					M.AddQuad(Q0, Q1, Q2, Q3, (Q0 + Q2) * 0.5 - TubeCenter, C);
				}
			}
			break;
		}
		case EStormItem::Spade:
		{
			// Pala de plástico.
			M.AddBox(V(0.0, 0.0, -12.0), FVector::ForwardVector, V(9.0, 1.2, 10.0), A);
			TNProcMesh::TNProcAddCylinder(M, V(0.0, 0.0, -2.0), V(0.0, 0.0, 24.0), 2.0 * S, 2.0 * S, 6, A * 0.9f, true);
			M.AddBox(V(0.0, 0.0, 26.0), FVector::ForwardVector, V(7.0, 2.5, 2.5), A * 0.9f);
			break;
		}
		case EStormItem::FlipFlop:
		{
			// Chancla.
			M.AddBox(V(0.0, 0.0, 0.0), FVector::ForwardVector, V(13.0, 5.0, 1.0), A);
			M.AddBox(V(0.0, 0.0, 1.1), FVector::ForwardVector, V(12.5, 4.6, 0.2), White);
			for (const double Side : { -1.0, 1.0 })
			{
				M.AddBeam(V(7.0, 0.0, 1.3), V(1.0, Side * 4.0, 4.0), 0.8 * S, B);
				M.AddBeam(V(1.0, Side * 4.0, 4.0), V(-3.0, Side * 4.6, 1.0), 0.8 * S, B);
			}
			break;
		}
		default:
		{
			// Pelota de playa de gajos.
			constexpr int32 Lat = 6, Lon = 12;
			const double R = 15.0;
			auto P = [&](int32 i, int32 j)
			{
				const double La = -HALF_PI + PI * i / Lat;
				const double Lo = TNProcMap::TwoPi * j / Lon;
				return V(R * FMath::Cos(La) * FMath::Cos(Lo), R * FMath::Cos(La) * FMath::Sin(Lo), R * FMath::Sin(La));
			};
			for (int32 i = 0; i < Lat; ++i)
			{
				for (int32 j = 0; j < Lon; ++j)
				{
					const FLinearColor C = (i == 0 || i == Lat - 1) ? White : BeachColor(Variant + j / 2);
					const FVector Q0 = P(i, j), Q1 = P(i, j + 1), Q2 = P(i + 1, j + 1), Q3 = P(i + 1, j);
					M.AddTri(Q0, Q1, Q2, (Q0 + Q2) * 0.5, C);
					M.AddTri(Q0, Q2, Q3, (Q0 + Q2) * 0.5, C);
				}
			}
			break;
		}
		}
	}

	struct FBatherLook
	{
		FLinearColor Skin = Rgb(0.95f, 0.74f, 0.6f);
		FLinearColor Suit = Rgb(0.95f, 0.2f, 0.18f);
		FLinearColor Sandal = Rgb(0.15f, 0.5f, 0.95f);
	};

	inline FBatherLook BatherPalette(int32 Index)
	{
		static const FLinearColor Skins[4] = { Rgb(0.96f, 0.76f, 0.62f), Rgb(0.84f, 0.6f, 0.44f), Rgb(0.6f, 0.4f, 0.26f), Rgb(0.98f, 0.62f, 0.52f) };
		FBatherLook L;
		L.Skin = Skins[((Index % 4) + 4) % 4];
		L.Suit = BeachColor(Index * 3 + 1);
		L.Sandal = BeachColor(Index * 5 + 2);
		return L;
	}

	/** Pierna de bañista (pivote en la cadera): muslo, rodilla, espinilla, pie y chancla. Mide 96 cm reales. */
	inline void BuildBatherLeg(FTNProcMeshBuffers& M, const FBatherLook& L)
	{
		const double S = TNBeach::Scale;
		auto V = [S](double X, double Y, double Z) { return FVector(X * S, Y * S, Z * S); };
		TNProcMesh::TNProcAddCylinder(M, V(0.0, 0.0, 0.0), V(4.0, 0.0, -45.0), 9.0 * S, 6.5 * S, 9, L.Skin, true);
		TNFauna::TNFaunaBlob(M, V(4.0, 0.0, -45.0), V(6.8, 6.8, 6.8), L.Skin, L.Skin * 0.92f, 7, 3);
		TNProcMesh::TNProcAddCylinder(M, V(4.0, 0.0, -45.0), V(0.0, 0.0, -86.0), 6.5 * S, 4.2 * S, 9, L.Skin, true);
		M.AddBox(V(7.0, 0.0, -91.0), FVector::ForwardVector, V(11.0, 4.5, 3.2), L.Skin * 0.97f);
		M.AddBox(V(8.0, 0.0, -94.8), FVector::ForwardVector, V(13.0, 5.6, 0.9), L.Sandal);
		for (const double Side : { -1.0, 1.0 })
		{
			M.AddBeam(V(13.0, 0.0, -93.9), V(6.0, Side * 4.6, -90.0), 0.7 * S, L.Sandal * 0.8f);
		}
	}

	/** Caderas del bañista: bañador y tripa (hasta donde el polvo deja ver). */
	inline void BuildBatherHips(FTNProcMeshBuffers& M, const FBatherLook& L)
	{
		const double S = TNBeach::Scale;
		auto V = [S](double X, double Y, double Z) { return FVector(X * S, Y * S, Z * S); };
		TNFauna::TNFaunaBlob(M, V(0.0, 0.0, 4.0), V(13.0, 20.0, 12.0), L.Suit, L.Suit * 0.9f, 9, 4);
		TNFauna::TNFaunaBlob(M, V(2.0, 0.0, 26.0), V(14.0, 18.0, 22.0), L.Skin, L.Skin * 0.9f, 9, 4);
		M.AddBox(V(15.5, 0.0, 22.0), FVector::ForwardVector, V(0.6, 1.2, 1.2), L.Skin * 0.6f);
	}
}
