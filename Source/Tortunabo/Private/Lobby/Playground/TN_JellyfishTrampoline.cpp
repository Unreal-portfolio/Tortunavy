#include "Lobby/Playground/TN_JellyfishTrampoline.h"
#include "Lobby/Playground/TN_PlaygroundSynthComponent.h"
#include "Art/TN_Art.h"
#include "Core/TN_Log.h"
#include "Player/TN_ShellBody.h"
#include "Player/TortugaCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "TN_PlaygroundMeshKit.h"

/**
 * Geometría de la medusa. La campana es una superficie de revolución (perfil de Catmull-Rom de la cima al borde, labio
 * que se mete por debajo y panza) con festones en el borde; las motas, el trébol, el brillo y la cara son calcas pegadas
 * a la superficie (se proyectan desde la planta o desde el frente y se levantan un poco sobre la normal).
 */
namespace TNJellyfishDetail
{
	using FBuffers = TNPlaygroundKit::FBuffers;

	/** Medidas con Size = 1 (cm): altura del borde sobre la arena, radio del borde y altura de la campana sobre el borde. */
	constexpr double RimZ = 42.0;
	constexpr double BellR = 110.0;
	constexpr double BellH = 82.0;
	/** Grosor (cm) del sensor de aterrizaje por fuera de la colisión de la cúpula. */
	constexpr double SensorMargin = 15.0;
	/** Radio relativo del costado más ancho de la campana (ProfileR): hasta ahí rebota, no solo en la parte de arriba. */
	constexpr double SideReach = 1.03;
	/** Cota relativa (fracción de BellH sobre el borde) del labio, lo más bajo de la campana: por debajo no hay rebote. */
	constexpr double LipZRel = -0.075;
	constexpr int32 BellSeg = 44;
	constexpr int32 NumTentacles = 8;
	constexpr int32 NumArms = 4;
	constexpr int32 TentacleRings = 14;
	constexpr int32 TentacleSides = 6;
	constexpr int32 ArmRings = 12;
	constexpr int32 ArmSides = 7;

	/** Perfil (radio/BellR, altura/BellH sobre el borde): cima, costado gordo, borde, labio por debajo y panza. */
	constexpr double ProfileR[] = { 0.00, 0.30, 0.58, 0.80, 0.95, 1.03, 1.03, 0.98, 0.90, 0.82, 0.62, 0.35, 0.00 };
	constexpr double ProfileZ[] = { 1.00, 0.97, 0.87, 0.68, 0.44, 0.20, 0.05, -0.04, -0.075, -0.035, 0.02, 0.06, 0.08 };
	constexpr int32 NumKeys = static_cast<int32>(UE_ARRAY_COUNT(ProfileR));
	/** Índice del perfil donde acaba la cara de fuera (el borde) y donde acaba el labio (empieza la panza). */
	constexpr double OuterEndU = 7.0;
	constexpr double LipEndU = 9.0;
	/** Altura (fracción de BellH sobre el borde) del centro de la cara. */
	constexpr double FaceZRel = 0.43;

	struct FJellyPalette
	{
		FLinearColor Top;
		FLinearColor Mid;
		FLinearColor Rim;
		FLinearColor Lip;
		FLinearColor Under;
		FLinearColor Spot;
		FLinearColor Gonad;
		FLinearColor Tentacle;
		FLinearColor TentacleBand;
		FLinearColor Arm;
		FLinearColor ArmBand;
	};

	FJellyPalette MakeJellyPalette(ETNJellyfishColor Preset, const FLinearColor& Custom)
	{
		FJellyPalette P;
		switch (Preset)
		{
		case ETNJellyfishColor::Lilac:
			P.Top = TNPlaygroundKit::Rgb(0xEADBFF); P.Mid = TNPlaygroundKit::Rgb(0xC7A6FF); P.Rim = TNPlaygroundKit::Rgb(0xD9C2FF);
			P.Lip = TNPlaygroundKit::Rgb(0xF4ECFF); P.Under = TNPlaygroundKit::Rgb(0x9063E0); P.Spot = TNPlaygroundKit::Rgb(0xF8F2FF);
			P.Gonad = TNPlaygroundKit::Rgb(0x7C4FD1); P.Tentacle = TNPlaygroundKit::Rgb(0xCDB0FF); P.TentacleBand = TNPlaygroundKit::Rgb(0xEFE5FF);
			P.Arm = TNPlaygroundKit::Rgb(0xA985F2); P.ArmBand = TNPlaygroundKit::Rgb(0xDCCBFF);
			break;
		case ETNJellyfishColor::Sky:
			P.Top = TNPlaygroundKit::Rgb(0xD4F4FF); P.Mid = TNPlaygroundKit::Rgb(0x94DDF7); P.Rim = TNPlaygroundKit::Rgb(0xB8EAFB);
			P.Lip = TNPlaygroundKit::Rgb(0xE8F9FF); P.Under = TNPlaygroundKit::Rgb(0x3FAFDD); P.Spot = TNPlaygroundKit::Rgb(0xF0FBFF);
			P.Gonad = TNPlaygroundKit::Rgb(0x2B96CC); P.Tentacle = TNPlaygroundKit::Rgb(0xA6E3F8); P.TentacleBand = TNPlaygroundKit::Rgb(0xE2F7FF);
			P.Arm = TNPlaygroundKit::Rgb(0x6CC8EE); P.ArmBand = TNPlaygroundKit::Rgb(0xC4EEFC);
			break;
		case ETNJellyfishColor::Custom:
		{
			const FLinearColor BaseTone(Custom.R, Custom.G, Custom.B, 0.f);
			const FLinearColor Blank(1.f, 1.f, 1.f, 0.f);
			P.Top = TNPlaygroundKit::Mix(BaseTone, Blank, 0.45); P.Mid = BaseTone; P.Rim = TNPlaygroundKit::Mix(BaseTone, Blank, 0.3);
			P.Lip = TNPlaygroundKit::Mix(BaseTone, Blank, 0.72); P.Under = TNPlaygroundKit::Shade(BaseTone, 0.62); P.Spot = TNPlaygroundKit::Mix(BaseTone, Blank, 0.82);
			P.Gonad = TNPlaygroundKit::Shade(BaseTone, 0.55); P.Tentacle = TNPlaygroundKit::Mix(BaseTone, Blank, 0.22);
			P.TentacleBand = TNPlaygroundKit::Mix(BaseTone, Blank, 0.72); P.Arm = TNPlaygroundKit::Shade(BaseTone, 0.85);
			P.ArmBand = TNPlaygroundKit::Mix(BaseTone, Blank, 0.5);
			break;
		}
		default:
			P.Top = TNPlaygroundKit::Rgb(0xFFD0E4); P.Mid = TNPlaygroundKit::Rgb(0xFF9CC8); P.Rim = TNPlaygroundKit::Rgb(0xFFB5D6);
			P.Lip = TNPlaygroundKit::Rgb(0xFFE6F1); P.Under = TNPlaygroundKit::Rgb(0xE8609E); P.Spot = TNPlaygroundKit::Rgb(0xFFF0F7);
			P.Gonad = TNPlaygroundKit::Rgb(0xD94C92); P.Tentacle = TNPlaygroundKit::Rgb(0xFFA8CF); P.TentacleBand = TNPlaygroundKit::Rgb(0xFFE0EE);
			P.Arm = TNPlaygroundKit::Rgb(0xF57AB4); P.ArmBand = TNPlaygroundKit::Rgb(0xFFC2DE);
			break;
		}
		return P;
	}

	/** Perfil de Catmull-Rom (radio relativo, altura relativa) en el índice continuo U (0 cima, 12 centro de la panza). */
	FVector2D ProfileAt(double U)
	{
		const double Uc = FMath::Clamp(U, 0.0, static_cast<double>(NumKeys - 1));
		const int32 I = FMath::Min(static_cast<int32>(Uc), NumKeys - 2);
		const double T = Uc - I;
		const auto KeyAt = [](int32 Index)
		{
			const int32 C = FMath::Clamp(Index, 0, NumKeys - 1);
			return FVector2D(ProfileR[C], ProfileZ[C]);
		};
		const FVector2D P0 = KeyAt(I - 1);
		const FVector2D P1 = KeyAt(I);
		const FVector2D P2 = KeyAt(I + 1);
		const FVector2D P3 = KeyAt(I + 2);
		const double T2 = T * T;
		const double T3 = T2 * T;
		return (P1 * 2.0 + (P2 - P0) * T + (P0 * 2.0 - P1 * 5.0 + P2 * 4.0 - P3) * T2 + (P1 * 3.0 - P0 - P2 * 3.0 + P3) * T3) * 0.5;
	}

	/** Festones del borde: ocho lóbulos que solo se notan cerca del borde y en el labio. */
	double Scallop(double Theta, double U)
	{
		const double Weight = TNPlaygroundKit::Smooth01(4.6, 6.2, U) * (1.0 - TNPlaygroundKit::Smooth01(8.4, 9.6, U));
		return 1.0 + 0.045 * FMath::Cos(8.0 * Theta) * Weight;
	}

	/** Punto de la campana relativo al pivote (centro del borde). */
	FVector BellPoint(double Theta, double U, double Scale)
	{
		const FVector2D Prof = ProfileAt(U);
		const double Rad = Prof.X * BellR * Scallop(Theta, U) * Scale;
		return FVector(Rad * FMath::Cos(Theta), Rad * FMath::Sin(Theta), Prof.Y * BellH * Scale);
	}

	/** Normal de la cara visible (fuera y arriba en la cúpula, abajo en la panza), por diferencias centradas. */
	FVector BellNormal(double Theta, double U, double Scale)
	{
		constexpr double Du = 1e-3;
		constexpr double Dt = 1e-3;
		const FVector Su = BellPoint(Theta, U + Du, Scale) - BellPoint(Theta, U - Du, Scale);
		const FVector St = BellPoint(Theta + Dt, U, Scale) - BellPoint(Theta - Dt, U, Scale);
		FVector N = FVector::CrossProduct(Su, St);
		if (!N.Normalize(1e-12))
		{
			N = U < 3.0 ? FVector::UpVector : -FVector::UpVector;
		}
		return N;
	}

	FLinearColor BellColorAt(const FJellyPalette& Pal, double U)
	{
		if (U <= OuterEndU)
		{
			// Cima pálida, costado con el color y borde otra vez más claro (la gelatina es más fina allí).
			const double T = U / OuterEndU;
			const FLinearColor C = T < 0.55 ? TNPlaygroundKit::Mix(Pal.Top, Pal.Mid, T / 0.55) : TNPlaygroundKit::Mix(Pal.Mid, Pal.Rim, (T - 0.55) / 0.45);
			return TNPlaygroundKit::WithShine(C, 0.3f);
		}
		if (U <= LipEndU)
		{
			return TNPlaygroundKit::WithShine(TNPlaygroundKit::Mix(Pal.Rim, Pal.Lip, (U - OuterEndU) / 1.2), 0.22f);
		}
		return TNPlaygroundKit::WithShine(TNPlaygroundKit::Mix(Pal.Lip, Pal.Under, (U - LipEndU) / 0.8), 0.1f);
	}

	/** U de la cara de fuera con radio relativo Rho (sin festón), por bisección: el radio crece entre la cima y la clave 5. */
	double UAtRadius(double Rho)
	{
		double Lo = 0.0;
		double Hi = 5.0;
		for (int32 It = 0; It < 26; ++It)
		{
			const double Probe = 0.5 * (Lo + Hi);
			if (ProfileAt(Probe).X < Rho) { Lo = Probe; } else { Hi = Probe; }
		}
		return 0.5 * (Lo + Hi);
	}

	/** U de la cara de fuera a la altura relativa ZRel sobre el borde: la altura baja sin parar de la cima al borde. */
	double UAtHeight(double ZRel)
	{
		double Lo = 0.0;
		double Hi = OuterEndU - 0.6;
		for (int32 It = 0; It < 26; ++It)
		{
			const double Probe = 0.5 * (Lo + Hi);
			if (ProfileAt(Probe).Y > ZRel) { Lo = Probe; } else { Hi = Probe; }
		}
		return 0.5 * (Lo + Hi);
	}

	/** Punto y normal de la cúpula bajo el punto (X, Y) de la planta (cm, ya escalados). */
	void TopSurface(double X, double Y, double Scale, FVector& OutP, FVector& OutN)
	{
		const double Theta = FMath::Atan2(Y, X);
		const double Rho = FMath::Sqrt(X * X + Y * Y) / (BellR * Scale);
		const double U = UAtRadius(FMath::Min(Rho, 1.0));
		OutP = BellPoint(Theta, U, Scale);
		OutN = BellNormal(Theta, U, Scale);
	}

	/** Punto y normal de la cara de fuera en el ángulo Theta y a la altura relativa ZRel. */
	void SideSurface(double Theta, double ZRel, double Scale, FVector& OutP, FVector& OutN)
	{
		const double U = UAtHeight(ZRel);
		OutP = BellPoint(Theta, U, Scale);
		OutN = BellNormal(Theta, U, Scale);
	}

	/** Altura (cm, sobre el origen del actor) de la cúpula a la distancia relativa Rho (0..1) del eje. */
	double DomeHeightAt(double Rho, double Scale)
	{
		return (RimZ + ProfileAt(UAtRadius(FMath::Clamp(Rho, 0.0, 1.0))).Y * BellH) * Scale;
	}

	/**
	 * Puntos del casco convexo de la cúpula, desplazados Offset cm hacia fuera sobre la normal del perfil, más un anillo
	 * en la arena con el radio más ancho (la campana y el nudo de tentáculos forman un bulto macizo hasta el suelo).
	 */
	TArray<FVector> DomeHull(double Scale, double Offset)
	{
		constexpr int32 Around = 16;
		const double Samples[] = { 0.0, 0.7, 1.4, 2.1, 2.8, 3.5, 4.2, 4.9, 5.5 };
		TArray<FVector> Pts;
		Pts.Reserve(static_cast<int32>(UE_ARRAY_COUNT(Samples)) * Around + Around);
		double Widest = 0.0;
		for (const double U : Samples)
		{
			const FVector2D Prof = ProfileAt(U);
			const FVector2D Ahead = ProfileAt(U + 0.01);
			const FVector2D Behind = ProfileAt(FMath::Max(0.0, U - 0.01));
			// Normal hacia fuera del perfil (r, z): la tangente girada 90°.
			FVector2D Out((Behind.Y - Ahead.Y) * BellH, (Ahead.X - Behind.X) * BellR);
			if (!Out.Normalize())
			{
				Out = FVector2D(0.0, 1.0);
			}
			const double Rad = (Prof.X * BellR + Out.X * Offset) * Scale;
			const double Zh = (RimZ + Prof.Y * BellH + Out.Y * Offset) * Scale;
			Widest = FMath::Max(Widest, Rad);
			if (U <= 0.0)
			{
				Pts.Add(FVector(0.0, 0.0, Zh));
				continue;
			}
			for (int32 k = 0; k < Around; ++k)
			{
				const double A = TNPlaygroundKit::KitTwoPi * k / Around;
				Pts.Add(FVector(Rad * FMath::Cos(A), Rad * FMath::Sin(A), Zh));
			}
		}
		for (int32 k = 0; k < Around; ++k)
		{
			const double A = TNPlaygroundKit::KitTwoPi * k / Around;
			Pts.Add(FVector(Widest * FMath::Cos(A), Widest * FMath::Sin(A), 0.0));
		}
		return Pts;
	}

	/** Contorno de una elipse de semiejes (Rx, Ry) girada Rotation, alrededor de Center. */
	TArray<FVector2D> EllipseAt(const FVector2D& Center, double Rx, double Ry, int32 Count, double Rotation)
	{
		TArray<FVector2D> Outline;
		Outline.Reserve(Count);
		const double Cr = FMath::Cos(Rotation);
		const double Sr = FMath::Sin(Rotation);
		for (int32 i = 0; i < Count; ++i)
		{
			const double A = TNPlaygroundKit::KitTwoPi * i / Count;
			const double Qx = Rx * FMath::Cos(A);
			const double Qy = Ry * FMath::Sin(A);
			Outline.Add(Center + FVector2D(Qx * Cr - Qy * Sr, Qx * Sr + Qy * Cr));
		}
		return Outline;
	}

	/** Calca en abanico: FanCenter y el contorno en las coordenadas de Map, levantada Lift sobre la superficie. */
	template <typename FMapFn>
	void AddDecalFan(FBuffers& B, const FMapFn& Map, const FVector2D& FanCenter, const TArray<FVector2D>& Outline, double Lift, const FLinearColor& Color)
	{
		const int32 Count = Outline.Num();
		if (Count < 3)
		{
			return;
		}
		FVector Pc;
		FVector Nc;
		Map(FanCenter, Pc, Nc);
		TArray<FVector> Ps;
		TArray<FVector> Ns;
		Ps.Reserve(Count);
		Ns.Reserve(Count);
		for (const FVector2D& Q : Outline)
		{
			FVector Pq;
			FVector Nq;
			Map(Q, Pq, Nq);
			Ps.Add(Pq + Nq * Lift);
			Ns.Add(Nq);
		}
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 j = (i + 1) % Count;
			TNPlaygroundKit::SmoothTri(B, Pc + Nc * Lift, Ps[i], Ps[j], Nc, Ns[i], Ns[j], Color);
		}
	}

	/** Calca en tira entre dos contornos paralelos (arcos): Inner[i]-Outer[i]. */
	template <typename FMapFn>
	void AddDecalStrip(FBuffers& B, const FMapFn& Map, const TArray<FVector2D>& Inner, const TArray<FVector2D>& Outer, double Lift, const FLinearColor& Color)
	{
		const int32 Count = FMath::Min(Inner.Num(), Outer.Num());
		for (int32 i = 0; i + 1 < Count; ++i)
		{
			FVector P[4];
			FVector N[4];
			Map(Inner[i], P[0], N[0]);
			Map(Inner[i + 1], P[1], N[1]);
			Map(Outer[i + 1], P[2], N[2]);
			Map(Outer[i], P[3], N[3]);
			for (int32 k = 0; k < 4; ++k)
			{
				P[k] += N[k] * Lift;
			}
			TNPlaygroundKit::SmoothQuad1(B, P, N, Color);
		}
	}

	/** Cúpula, labio y panza. */
	void BuildBell(FBuffers& B, const FJellyPalette& Pal, double Scale)
	{
		TArray<double> Us;
		for (int32 i = 0; i <= 26; ++i) { Us.Add(OuterEndU * i / 26.0); }
		for (int32 i = 1; i <= 5; ++i) { Us.Add(OuterEndU + (LipEndU - OuterEndU) * i / 5.0); }
		for (int32 i = 1; i <= 6; ++i) { Us.Add(LipEndU + (NumKeys - 1 - LipEndU) * i / 6.0); }
		const int32 NumU = Us.Num();
		const int32 Row = BellSeg + 1;
		TArray<FVector> Pts;
		TArray<FVector> Nrms;
		TArray<FLinearColor> Cols;
		Pts.SetNum(NumU * Row);
		Nrms.SetNum(NumU * Row);
		Cols.SetNum(NumU * Row);
		for (int32 r = 0; r < NumU; ++r)
		{
			const FLinearColor RingCol = BellColorAt(Pal, Us[r]);
			for (int32 k = 0; k < Row; ++k)
			{
				const double Theta = TNPlaygroundKit::KitTwoPi * k / BellSeg;
				Pts[r * Row + k] = BellPoint(Theta, Us[r], Scale);
				Nrms[r * Row + k] = BellNormal(Theta, Us[r], Scale);
				Cols[r * Row + k] = RingCol;
			}
		}
		for (int32 r = 0; r + 1 < NumU; ++r)
		{
			for (int32 k = 0; k < BellSeg; ++k)
			{
				const int32 I00 = r * Row + k;
				const int32 I01 = I00 + 1;
				const int32 I10 = I00 + Row;
				const int32 I11 = I10 + 1;
				const FVector P[4] = { Pts[I00], Pts[I01], Pts[I11], Pts[I10] };
				const FVector N[4] = { Nrms[I00], Nrms[I01], Nrms[I11], Nrms[I10] };
				const FLinearColor C[4] = { Cols[I00], Cols[I01], Cols[I11], Cols[I10] };
				TNPlaygroundKit::SmoothQuad(B, P, N, C);
			}
		}
	}

	/** Trébol de cuatro órganos en herradura alrededor de la cima (lo que se transparenta de una medusa luna). */
	void BuildClover(FBuffers& B, const FJellyPalette& Pal, double Scale)
	{
		const auto TopMap = [Scale](const FVector2D& Plan, FVector& OutP, FVector& OutN) { TopSurface(Plan.X, Plan.Y, Scale, OutP, OutN); };
		const FLinearColor Organ = TNPlaygroundKit::WithShine(Pal.Gonad, 0.4f);
		for (int32 k = 0; k < 4; ++k)
		{
			const double Phi = TNPlaygroundKit::KitPi * (0.25 + 0.5 * k);
			const FVector2D Hub = FVector2D(FMath::Cos(Phi), FMath::Sin(Phi)) * (0.25 * BellR * Scale);
			TArray<FVector2D> Inner;
			TArray<FVector2D> Outer;
			constexpr int32 Steps = 14;
			for (int32 i = 0; i <= Steps; ++i)
			{
				// Herradura de 280° abierta hacia fuera.
				const double A = Phi + TNPlaygroundKit::KitPi + FMath::DegreesToRadians(-140.0 + 280.0 * i / Steps);
				const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
				Inner.Add(Hub + Dir * (6.5 * Scale));
				Outer.Add(Hub + Dir * (12.0 * Scale));
			}
			AddDecalStrip(B, TopMap, Inner, Outer, 0.5 * Scale, Organ);
		}
	}

	/** Motas claras repartidas por la cúpula, lejos de la cara, del trébol y del brillo. */
	void BuildSpots(FBuffers& B, const FJellyPalette& Pal, double Scale, uint32 Seed)
	{
		const auto TopMap = [Scale](const FVector2D& Plan, FVector& OutP, FVector& OutN) { TopSurface(Plan.X, Plan.Y, Scale, OutP, OutN); };
		const FLinearColor Dot = TNPlaygroundKit::WithShine(Pal.Spot, 0.34f);
		int32 Made = 0;
		for (int32 Try = 0; Try < 80 && Made < 11; ++Try)
		{
			const double Rho = 0.3 + 0.54 * TNPlaygroundKit::Hash01(Try, 1, Seed);
			const double Theta = TNPlaygroundKit::KitTwoPi * TNPlaygroundKit::Hash01(Try, 2, Seed);
			const double Wrapped = FMath::UnwindRadians(Theta);
			// La cara (delante, +X, en el costado) y el brillo (arriba a la izquierda de la cara).
			if (FMath::Abs(Wrapped) < 0.8 && Rho > 0.5) { continue; }
			if (Wrapped > 0.45 && Wrapped < 1.55 && Rho > 0.36 && Rho < 0.66) { continue; }
			const double Rad = (4.5 + 4.5 * TNPlaygroundKit::Hash01(Try, 3, Seed)) * Scale;
			const FVector2D Where = FVector2D(FMath::Cos(Theta), FMath::Sin(Theta)) * (Rho * BellR * Scale);
			bool bClear = true;
			for (int32 k = 0; k < 4 && bClear; ++k)
			{
				const double Phi = TNPlaygroundKit::KitPi * (0.25 + 0.5 * k);
				const FVector2D Hub = FVector2D(FMath::Cos(Phi), FMath::Sin(Phi)) * (0.25 * BellR * Scale);
				bClear = FVector2D::Distance(Where, Hub) > 15.0 * Scale + Rad;
			}
			if (!bClear) { continue; }
			AddDecalFan(B, TopMap, Where, EllipseAt(Where, Rad, Rad * 0.86, 12, Theta), 0.55 * Scale, Dot);
			++Made;
		}
	}

	/** Brillo de goma: una media luna blanca arriba a la izquierda de la cara y un puntito. */
	void BuildHighlight(FBuffers& B, double Scale)
	{
		const auto TopMap = [Scale](const FVector2D& Plan, FVector& OutP, FVector& OutN) { TopSurface(Plan.X, Plan.Y, Scale, OutP, OutN); };
		const FLinearColor Gloss = TNPlaygroundKit::Rgb(0xFFFFFF, 0.35f);
		const double Radius = 0.5 * BellR * Scale;
		const double A0 = FMath::DegreesToRadians(36.0);
		const double A1 = FMath::DegreesToRadians(84.0);
		TArray<FVector2D> Inner;
		TArray<FVector2D> Outer;
		constexpr int32 Steps = 16;
		for (int32 i = 0; i <= Steps; ++i)
		{
			const double T = static_cast<double>(i) / Steps;
			const double A = FMath::Lerp(A0, A1, T);
			const double HalfWidth = 4.4 * Scale * FMath::Pow(FMath::Sin(TNPlaygroundKit::KitPi * T), 0.7);
			const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
			Inner.Add(Dir * (Radius - HalfWidth));
			Outer.Add(Dir * (Radius + HalfWidth));
		}
		AddDecalStrip(B, TopMap, Inner, Outer, 0.9 * Scale, Gloss);
		const double Ad = FMath::DegreesToRadians(97.0);
		const FVector2D Speck = FVector2D(FMath::Cos(Ad), FMath::Sin(Ad)) * Radius;
		AddDecalFan(B, TopMap, Speck, EllipseAt(Speck, 3.6 * Scale, 3.6 * Scale, 10, 0.0), 0.9 * Scale, Gloss);
	}

	/**
	 * Cara en el +X: ojos negros brillantes en relieve con dos brillos, mofletes, boca abierta en «D» con lengua. Las
	 * coordenadas de la cara son (arco horizontal, altura) en cm sobre el costado de la campana.
	 */
	void BuildFace(FBuffers& B, double Scale)
	{
		const double Uc = UAtHeight(FaceZRel);
		const double RadC = FMath::Max(1.0, ProfileAt(Uc).X * BellR * Scale);
		const auto FaceMap = [Scale, RadC](const FVector2D& Local, FVector& OutP, FVector& OutN)
		{
			SideSurface(Local.X / RadC, FaceZRel + Local.Y / (BellH * Scale), Scale, OutP, OutN);
		};
		const FLinearColor EyeDark = TNPlaygroundKit::Rgb(0x2B1636, 0.5f);
		const FLinearColor EyeGlint = TNPlaygroundKit::Rgb(0xFFFFFF, 0.2f);
		const FLinearColor Cheek = TNPlaygroundKit::Rgb(0xFF7FA6, 0.15f);
		const FLinearColor MouthDark = TNPlaygroundKit::Rgb(0x6A1B3F, 0.2f);
		const FLinearColor Tongue = TNPlaygroundKit::Rgb(0xFF7F96, 0.2f);

		for (const double Side : { -1.0, 1.0 })
		{
			// Ojo: elipsoide medio hundido en la gelatina, en el marco (a lo ancho, hacia arriba, normal) de la superficie.
			FVector Pe;
			FVector Ne;
			FaceMap(FVector2D(Side * 18.0 * Scale, 8.0 * Scale), Pe, Ne);
			FVector Upward = FVector::UpVector - Ne * FVector::DotProduct(FVector::UpVector, Ne);
			if (!Upward.Normalize())
			{
				Upward = FVector::UpVector;
			}
			const FVector Across = FVector::CrossProduct(Upward, Ne).GetSafeNormal();
			const FVector EyeRadii(8.5 * Scale, 11.0 * Scale, 5.0 * Scale);
			const FVector EyeCenter = Pe - Ne * (1.2 * Scale);
			TNPlaygroundKit::AddEllipsoid(B, EyeCenter, Across, Upward, Ne, EyeRadii, 14, 8, EyeDark);
			// Brillos sobre la cara del ojo (los dos ojos miran igual: brillo grande arriba, hacia el +Y).
			const auto OnEye = [&EyeCenter, &Across, &Upward, &Ne, &EyeRadii, Scale](double X, double Y)
			{
				const double Rx = X / EyeRadii.X;
				const double Ry = Y / EyeRadii.Y;
				const double Depth = EyeRadii.Z * FMath::Sqrt(FMath::Max(0.0, 1.0 - Rx * Rx - Ry * Ry));
				return EyeCenter + Across * X + Upward * Y + Ne * (Depth + 0.15 * Scale);
			};
			TNPlaygroundKit::AddEllipsoid(B, OnEye(2.6 * Scale, 3.8 * Scale), Across, Upward, Ne, FVector(2.6, 3.0, 1.0) * Scale, 8, 5, EyeGlint);
			TNPlaygroundKit::AddEllipsoid(B, OnEye(-2.4 * Scale, -3.6 * Scale), Across, Upward, Ne, FVector(1.3, 1.5, 0.7) * Scale, 6, 4, EyeGlint);
			// Moflete.
			const FVector2D CheekAt(Side * 31.0 * Scale, -4.0 * Scale);
			AddDecalFan(B, FaceMap, CheekAt, EllipseAt(CheekAt, 8.0 * Scale, 5.0 * Scale, 14, 0.0), 0.5 * Scale, Cheek);
		}

		// Boca abierta en «D»: borde de arriba recto y media elipse por debajo.
		{
			const double TopY = -6.0 * Scale;
			TArray<FVector2D> Outline;
			constexpr int32 Steps = 12;
			for (int32 i = 0; i <= Steps; ++i)
			{
				const double A = TNPlaygroundKit::KitPi * i / Steps;
				Outline.Add(FVector2D(7.0 * Scale * FMath::Cos(A), TopY - 6.5 * Scale * FMath::Sin(A)));
			}
			AddDecalFan(B, FaceMap, FVector2D(0.0, TopY - 2.8 * Scale), Outline, 0.5 * Scale, MouthDark);
			const FVector2D TongueAt(0.0, TopY - 4.6 * Scale);
			AddDecalFan(B, FaceMap, TongueAt, EllipseAt(TongueAt, 3.6 * Scale, 1.9 * Scale, 12, 0.0), 0.9 * Scale, Tongue);
		}
	}

	/**
	 * Tentáculos (ocho finos y cuatro brazos orales más gruesos y rizados) en el instante Time: bajan de debajo de la
	 * campana a la arena y se tumban hacia fuera con una onda que viaja hacia la punta; las puntas se enroscan. Wiggle
	 * (0..1) agranda la onda justo después de un rebote. Topología fija: siempre los mismos vértices.
	 */
	void BuildTentacles(FBuffers& B, const FJellyPalette& Pal, double Scale, double Time, double Wiggle)
	{
		for (int32 k = 0; k < NumTentacles + NumArms; ++k)
		{
			const bool bArm = k >= NumTentacles;
			const int32 Index = bArm ? k - NumTentacles : k;
			const double Theta = bArm ? TNPlaygroundKit::KitTwoPi * (Index + 0.5) / NumArms : TNPlaygroundKit::KitTwoPi * (Index + 0.25) / NumTentacles;
			const FVector Er(FMath::Cos(Theta), FMath::Sin(Theta), 0.0);
			const FVector Et(-Er.Y, Er.X, 0.0);
			const double Phase = 1.7 * Index + (bArm ? 0.9 : 0.0);
			const double Curl = (Index % 2 == 0) ? 1.0 : -1.0;
			const int32 Rings = bArm ? ArmRings : TentacleRings;
			const double Omega = TNPlaygroundKit::KitTwoPi * (bArm ? 0.42 : 0.55) * (1.0 + 0.06 * Index);
			const FLinearColor BaseCol = TNPlaygroundKit::WithShine(bArm ? Pal.Arm : Pal.Tentacle, 0.28f);
			const FLinearColor BandCol = TNPlaygroundKit::WithShine(bArm ? Pal.ArmBand : Pal.TentacleBand, 0.28f);
			TArray<FVector> Path;
			TArray<double> Radii;
			TArray<FLinearColor> Colors;
			Path.Reserve(Rings);
			Radii.Reserve(Rings);
			Colors.Reserve(Rings);
			for (int32 i = 0; i < Rings; ++i)
			{
				const double S = static_cast<double>(i) / (Rings - 1);
				double Rho = 0.0;
				double Z = 0.0;
				double Rad = 0.0;
				if (bArm)
				{
					Rho = FMath::Lerp(0.16, 1.28, S) * BellR;
					Rad = FMath::Lerp(9.5, 3.5, S);
					Z = FMath::Lerp(RimZ + 6.0, Rad + 0.5, TNPlaygroundKit::Smooth01(0.0, 0.45, S));
				}
				else
				{
					Rho = (0.8 + 0.92 * S) * BellR;
					Rad = FMath::Lerp(5.4, 1.8, S);
					Z = FMath::Lerp(RimZ - 6.0, Rad + 0.5, TNPlaygroundKit::Smooth01(0.0, 0.36, S));
				}
				const double Grow = TNPlaygroundKit::Smooth01(0.05, 0.45, S);
				const double Amp = (bArm ? (7.0 + 9.0 * S) : (4.0 + 11.0 * S)) * (1.0 + 1.3 * Wiggle);
				double Lateral = Amp * Grow * FMath::Sin(TNPlaygroundKit::KitTwoPi * (bArm ? 1.8 : 1.35) * S - Omega * Time + Phase);
				Lateral += Curl * 15.0 * FMath::Square(TNPlaygroundKit::Smooth01(0.72, 1.0, S));
				Rho += 4.0 * S * FMath::Sin(Omega * 0.6 * Time + Phase * 1.3);
				Z += (bArm ? 2.0 : 3.0) * S * Grow * (0.5 + 0.5 * FMath::Sin(TNPlaygroundKit::KitTwoPi * 1.1 * S - Omega * 0.8 * Time + Phase));
				Z += 5.0 * TNPlaygroundKit::Smooth01(0.8, 1.0, S) * (1.0 + Wiggle);
				Path.Add((Er * Rho + Et * Lateral + FVector(0.0, 0.0, Z)) * Scale);
				Radii.Add(Rad * Scale);
				Colors.Add(((i / 2) % 2 == 0) ? BaseCol : BandCol);
			}
			TNPlaygroundKit::AddTube(B, Path, Radii, bArm ? ArmSides : TentacleSides, Colors, Et, true);
		}
	}

	/** Solo simulan el movimiento de un personaje el servidor y el cliente que lo controla. */
	bool SimulatesMovement(const APawn* Pawn)
	{
		return Pawn && (Pawn->IsLocallyControlled() || Pawn->HasAuthority());
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_JellyfishTrampoline
// ─────────────────────────────────────────────────────────────────────────────

ATN_JellyfishTrampoline::ATN_JellyfishTrampoline()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(5.f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	BellPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BellPivot"));
	BellPivot->SetupAttachment(SceneRoot);
	BellPivot->SetRelativeLocation(FVector(0.0, 0.0, TNJellyfishDetail::RimZ));

	BellMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BellMesh"));
	BellMesh->SetupAttachment(BellPivot);
	BellMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BellMesh->SetCanEverAffectNavigation(false);

	TentaclePreview = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TentaclePreview"));
	TentaclePreview->SetupAttachment(SceneRoot);
	TentaclePreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TentaclePreview->SetCanEverAffectNavigation(false);

	// Cúpula: casco convexo con la forma de la campana; bloquea como el resto del mundo (la cámara la atraviesa) y no
	// se sube andando: hay que saltar encima. Subobjeto por defecto: se puede nombrar por red si alguien lo pisa.
	BellCollider = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BellCollider"));
	BellCollider->SetupAttachment(SceneRoot);
	BellCollider->bUseComplexAsSimpleCollision = false;
	BellCollider->bUseAsyncCooking = false;
	BellCollider->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	BellCollider->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	BellCollider->SetGenerateOverlapEvents(false);
	BellCollider->SetCanEverAffectNavigation(false);
	BellCollider->CanCharacterStepUpOn = ECB_No;

	// Sensor: el mismo casco 15 cm más grande (un volumen fino sobre la cúpula) que solo solapa con personajes.
	BounceSensor = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BounceSensor"));
	BounceSensor->SetupAttachment(SceneRoot);
	BounceSensor->bUseComplexAsSimpleCollision = false;
	BounceSensor->bUseAsyncCooking = false;
	BounceSensor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BounceSensor->SetCollisionObjectType(ECC_WorldDynamic);
	BounceSensor->SetCollisionResponseToAllChannels(ECR_Ignore);
	BounceSensor->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	BounceSensor->SetGenerateOverlapEvents(true);
	BounceSensor->SetCanEverAffectNavigation(false);
}

void ATN_JellyfishTrampoline::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_JellyfishTrampoline, ColorPreset);
	DOREPLIFETIME(ATN_JellyfishTrampoline, CustomColor);
	DOREPLIFETIME(ATN_JellyfishTrampoline, Size);
	DOREPLIFETIME(ATN_JellyfishTrampoline, LaunchZ);
	DOREPLIFETIME(ATN_JellyfishTrampoline, BounceCooldown);
	DOREPLIFETIME(ATN_JellyfishTrampoline, SpotSeed);
}

void ATN_JellyfishTrampoline::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildAll(false);
}

void ATN_JellyfishTrampoline::BeginPlay()
{
	Super::BeginPlay();
	BuildAll(false);
	BuildRuntimeTentacles();
	BellCollider->OnComponentHit.AddUniqueDynamic(this, &ATN_JellyfishTrampoline::OnBellHit);
	BounceSensor->OnComponentBeginOverlap.AddUniqueDynamic(this, &ATN_JellyfishTrampoline::OnSensorOverlap);
	BreathPhase = static_cast<float>(TNPlaygroundKit::KitTwoPi * TNPlaygroundKit::Hash01(SpotSeed, 7, 0x51u));
	if (GetNetMode() != NM_DedicatedServer)
	{
		Voice = UTN_PlaygroundSynthComponent::AttachTo(this, GetActorTransform().TransformPosition(FVector(0.0, 0.0, TNJellyfishDetail::RimZ * Size)),
			450.f, 2600.f);
	}
	UE_LOG(LogTortunabo, Verbose, TEXT("[Parque] Medusa %s lista (tamaño %.2f, lanzamiento %.0f cm/s)."), *GetName(), Size, LaunchZ);
}

void ATN_JellyfishTrampoline::OnRep_Config()
{
	BuildAll(false);
}

void ATN_JellyfishTrampoline::RebuildJellyfish()
{
	BuildAll(true);
}

float ATN_JellyfishTrampoline::GetBellTopHeight() const
{
	return static_cast<float>((TNJellyfishDetail::RimZ + TNJellyfishDetail::BellH) * Size * GetActorScale3D().Z);
}

float ATN_JellyfishTrampoline::GetBellRadius() const
{
	return static_cast<float>(TNJellyfishDetail::BellR * Size * GetActorScale3D().GetAbsMax());
}

uint32 ATN_JellyfishTrampoline::ConfigHash() const
{
	uint32 Acc = GetTypeHash(static_cast<uint8>(ColorPreset));
	Acc = HashCombine(Acc, GetTypeHash(CustomColor));
	Acc = HashCombine(Acc, GetTypeHash(Size));
	Acc = HashCombine(Acc, GetTypeHash(SpotSeed));
	return Acc;
}

void ATN_JellyfishTrampoline::BuildAll(bool bForce)
{
	const uint32 NewHash = ConfigHash();
	if (!bForce && NewHash == BuiltHash && BellMesh->GetStaticMesh())
	{
		return;
	}
	BuiltHash = NewHash;

	const double Scale = FMath::Clamp(static_cast<double>(Size), 0.5, 2.5);
	const TNJellyfishDetail::FJellyPalette Pal = TNJellyfishDetail::MakeJellyPalette(ColorPreset, CustomColor);
	UMaterialInterface* Mat = TNPlaygroundKit::VertexColorMaterial();

	// Campana con sus calcas (en el espacio del pivote del aplastamiento).
	TNPlaygroundKit::FBuffers Bell;
	TNJellyfishDetail::BuildBell(Bell, Pal, Scale);
	TNJellyfishDetail::BuildClover(Bell, Pal, Scale);
	TNJellyfishDetail::BuildSpots(Bell, Pal, Scale, static_cast<uint32>(SpotSeed) * 2654435761u + 17u);
	TNJellyfishDetail::BuildHighlight(Bell, Scale);
	TNJellyfishDetail::BuildFace(Bell, Scale);
	// Campana: pieza de arte (se modela con Size = 1 y sigue el aplastamiento del pivote). Los tentáculos que se mecen son
	// una malla que se deforma en cada fotograma y se quedan como están.
	TNArt::SetMesh(BellMesh, TNPlaygroundKit::BuildMesh(this, Bell, Mat), TN_ART("Lobby.Playground.Jellyfish.Bell"));
	TNPlaygroundKit::ScaleArt(BellMesh, FVector(Scale));
	BellPivot->SetRelativeLocation(FVector(0.0, 0.0, TNJellyfishDetail::RimZ * Scale));

	// Colisión y sensor: la cúpula y la misma cúpula algo más grande.
	TArray<TArray<FVector>> Solid;
	Solid.Add(TNJellyfishDetail::DomeHull(Scale, 0.0));
	BellCollider->SetCollisionConvexMeshes(Solid);
	TArray<TArray<FVector>> Shell;
	Shell.Add(TNJellyfishDetail::DomeHull(Scale, TNJellyfishDetail::SensorMargin));
	BounceSensor->SetCollisionConvexMeshes(Shell);

	// Tentáculos: quietos en el editor; en juego, la malla procedural que se mece.
	const UWorld* World = GetWorld();
	const bool bGameWorld = World && World->IsGameWorld();
	if (bGameWorld)
	{
		// El servidor dedicado no carga los componentes sin colisión (UPrimitiveComponent::NeedsLoadForServer, #658).
		if (TentaclePreview)
		{
			TentaclePreview->SetStaticMesh(nullptr);
			TentaclePreview->SetVisibility(false);
		}
		if (TentacleMesh)
		{
			BuildRuntimeTentacles();
		}
	}
	else
	{
		TNPlaygroundKit::FBuffers Legs;
		TNJellyfishDetail::BuildTentacles(Legs, Pal, Scale, 0.0, 0.0);
		if (TentaclePreview)
		{
			TentaclePreview->SetStaticMesh(TNPlaygroundKit::BuildMesh(this, Legs, Mat));
			TentaclePreview->SetVisibility(true);
		}
	}
}

void ATN_JellyfishTrampoline::BuildRuntimeTentacles()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!TentacleMesh)
	{
		TentacleMesh = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
		TentacleMesh->SetupAttachment(SceneRoot);
		TentacleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TentacleMesh->SetCanEverAffectNavigation(false);
		TentacleMesh->bUseAsyncCooking = true;
		TentacleMesh->RegisterComponent();
	}
	const double Scale = FMath::Clamp(static_cast<double>(Size), 0.5, 2.5);
	const TNJellyfishDetail::FJellyPalette Pal = TNJellyfishDetail::MakeJellyPalette(ColorPreset, CustomColor);
	TNPlaygroundKit::FBuffers Legs;
	TNJellyfishDetail::BuildTentacles(Legs, Pal, Scale, AnimClock, Wiggle);
	const TArray<FProcMeshTangent> NoTangents;
	TentacleMesh->ClearAllMeshSections();
	TentacleMesh->CreateMeshSection_LinearColor(0, Legs.Verts, Legs.Tris, Legs.Normals, Legs.UVs, Legs.Colors, NoTangents, false);
	TentacleMesh->SetMaterial(0, TNPlaygroundKit::VertexColorMaterial());
}

void ATN_JellyfishTrampoline::UpdateTentacles()
{
	if (!TentacleMesh || TentacleMesh->GetNumSections() == 0)
	{
		return;
	}
	const double Scale = FMath::Clamp(static_cast<double>(Size), 0.5, 2.5);
	const TNJellyfishDetail::FJellyPalette Pal = TNJellyfishDetail::MakeJellyPalette(ColorPreset, CustomColor);
	TNPlaygroundKit::FBuffers Legs;
	if (const FProcMeshSection* Section = TentacleMesh->GetProcMeshSection(0))
	{
		// Mismo tamaño que la sección creada: sin realojar los arrays en cada fotograma.
		Legs.Verts.Reserve(Section->ProcVertexBuffer.Num());
		Legs.Normals.Reserve(Section->ProcVertexBuffer.Num());
		Legs.UVs.Reserve(Section->ProcVertexBuffer.Num());
		Legs.Colors.Reserve(Section->ProcVertexBuffer.Num());
		Legs.Tris.Reserve(Section->ProcIndexBuffer.Num());
	}
	TNJellyfishDetail::BuildTentacles(Legs, Pal, Scale, AnimClock, Wiggle);
	// Solo posiciones y normales: colores, UV y triángulos se quedan los de la creación (misma topología).
	const TArray<FVector2D> KeepUVs;
	const TArray<FColor> KeepColors;
	const TArray<FProcMeshTangent> KeepTangents;
	TentacleMesh->UpdateMeshSection(0, Legs.Verts, Legs.Normals, KeepUVs, KeepColors, KeepTangents);
}

// ─────────────────────────────────────────────────────────────────────────────
// Rebote
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_JellyfishTrampoline::IsOnBell(const ACharacter* Character) const
{
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Capsule)
	{
		return false;
	}
	// En el espacio del actor: rebota toda la campana (cima, costados y borde), así que basta con que la cápsula llegue
	// a ella (o al sensor, SensorMargin por fuera) de lado o desde arriba y no esté por debajo del labio, donde solo hay
	// tentáculos sin colisión.
	const double Scale = FMath::Clamp(static_cast<double>(Size), 0.5, 2.5);
	const FVector Center = GetActorTransform().InverseTransformPosition(Capsule->GetComponentLocation());
	const double HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const double Reach = TNJellyfishDetail::BellR * TNJellyfishDetail::SideReach * Scale + Capsule->GetScaledCapsuleRadius()
		+ TNJellyfishDetail::SensorMargin + 5.0;
	const double LipZ = (TNJellyfishDetail::RimZ + TNJellyfishDetail::LipZRel * TNJellyfishDetail::BellH) * Scale;
	const double TopZ = (TNJellyfishDetail::RimZ + TNJellyfishDetail::BellH) * Scale;
	return FVector2D(Center.X, Center.Y).Size() <= Reach && Center.Z + HalfHeight > LipZ
		&& Center.Z - HalfHeight < TopZ + TNJellyfishDetail::SensorMargin + 32.0;
}

bool ATN_JellyfishTrampoline::TryBounce(ACharacter* Character)
{
	UWorld* World = GetWorld();
	if (!Character || !World || !TNJellyfishDetail::SimulatesMovement(Character))
	{
		return false;
	}
	if (const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Character))
	{
		// La tortuga metida en el caparazón es una caja con física: la lanza BounceShells.
		if (Turtle->IsDead() || Turtle->IsInShell())
		{
			return false;
		}
	}
	UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	if (!Move || Move->MovementMode == MOVE_None || !IsOnBell(Character))
	{
		return false;
	}
	// Todavía subiendo del rebote anterior (o saltando desde encima): nada.
	if (Move->Velocity.Z > 150.0)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	const TWeakObjectPtr<ACharacter> Key(Character);
	if (const double* Last = LastBounceTime.Find(Key))
	{
		if (Now - *Last < BounceCooldown)
		{
			return false;
		}
	}
	LastBounceTime.Add(Key, Now);

	// Mismo impulso en el servidor y en el cliente dueño, en el mismo movimiento: la predicción cuadra. La vertical se
	// sustituye (no se acumula altura) y la horizontal se conserva para ir de una medusa a otra.
	Character->LaunchCharacter(FVector(0.0, 0.0, LaunchZ), false, true);
	if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Character))
	{
		// El vuelo puede pasar de 5 m: que no se meta sola en el caparazón al caer.
		Turtle->SetFallImmuneUntilLanded();
	}
	SpreadBounceFX(Character, 1.f);
	return true;
}

void ATN_JellyfishTrampoline::OnBellHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse,
	const FHitResult& Hit)
{
	// Llega dentro del movimiento del personaje (servidor y cliente dueño): el lanzamiento se aplica en el siguiente.
	TryBounce(Cast<ACharacter>(OtherActor));
}

void ATN_JellyfishTrampoline::OnSensorOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	TryBounce(Cast<ACharacter>(OtherActor));
}

void ATN_JellyfishTrampoline::BounceShells(double Now)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Scale = FMath::Clamp(static_cast<double>(Size), 0.5, 2.5);
	const FTransform ActorXf = GetActorTransform();
	for (TActorIterator<ATN_ShellBody> It(World); It; ++It)
	{
		ATN_ShellBody* Shell = *It;
		UBoxComponent* ShellBox = Shell ? Shell->GetBox() : nullptr;
		if (!ShellBox || !ShellBox->IsSimulatingPhysics())
		{
			continue;
		}
		const FVector BoxPos = ActorXf.InverseTransformPosition(ShellBox->GetComponentLocation());
		// También de lado: hasta el costado más ancho más media caja.
		const double Rho = FVector2D(BoxPos.X, BoxPos.Y).Size() / (TNJellyfishDetail::BellR * Scale);
		if (Rho > TNJellyfishDetail::SideReach + 35.0 / (TNJellyfishDetail::BellR * Scale))
		{
			continue;
		}
		// El centro de la caja (21 cm de semialto) apoyada en la campana, por encima del labio.
		const double SurfaceZ = TNJellyfishDetail::DomeHeightAt(Rho, Scale);
		const double LipZ = (TNJellyfishDetail::RimZ + TNJellyfishDetail::LipZRel * TNJellyfishDetail::BellH) * Scale;
		const FVector Vel = ShellBox->GetPhysicsLinearVelocity();
		if (BoxPos.Z < LipZ - 5.0 || BoxPos.Z > SurfaceZ + 60.0 || Vel.Z > 120.0)
		{
			continue;
		}
		const TWeakObjectPtr<AActor> Key(Shell);
		if (const double* Last = LastShellBounce.Find(Key))
		{
			if (Now - *Last < BounceCooldown + 0.2)
			{
				continue;
			}
		}
		LastShellBounce.Add(Key, Now);
		// La física del caparazón se replica desde el servidor: basta con cambiar su velocidad aquí.
		ShellBox->SetPhysicsLinearVelocity(FVector(Vel.X, Vel.Y, LaunchZ * 0.9));
		SpreadBounceFX(Shell->GetTurtle(), 1.f);
	}
}

void ATN_JellyfishTrampoline::SpreadBounceFX(APawn* Bouncer, float Strength)
{
	if (GetNetMode() != NM_Client)
	{
		// Servidor (o partida local): a todas las máquinas; el multicast también se ejecuta aquí.
		if (GetIsReplicated())
		{
			MulticastBounceFX(Bouncer, Strength);
			ForceNetUpdate();
		}
		else
		{
			PlayBounceFX(Strength);
		}
	}
	else if (Bouncer && Bouncer->IsLocallyControlled())
	{
		// Cliente dueño: lo ve y lo oye al predecir, sin esperar al servidor.
		PlayBounceFX(Strength);
	}
}

void ATN_JellyfishTrampoline::MulticastBounceFX_Implementation(APawn* Bouncer, float Strength)
{
	if (GetNetMode() == NM_Client && Bouncer && Bouncer->IsLocallyControlled())
	{
		return;
	}
	PlayBounceFX(Strength);
}

void ATN_JellyfishTrampoline::PlayBounceFX(float Strength)
{
	SquashAge = 0.f;
	SquashAmp = 0.42f * FMath::Clamp(Strength, 0.3f, 1.2f);
	Wiggle = 1.f;
	if (Voice)
	{
		// Las medusas grandes suenan más graves.
		const float Pitch = FMath::Clamp(1.2f / FMath::Pow(FMath::Max(0.3f, Size), 0.6f), 0.55f, 1.9f) * FMath::FRandRange(0.94f, 1.06f);
		Voice->TriggerSound(ETNPlaygroundSound::Boing, Pitch, BoingVolume * FMath::Clamp(Strength, 0.4f, 1.f));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void ATN_JellyfishTrampoline::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	AnimClock += DeltaSeconds;
	const double Now = World->GetTimeSeconds();

	// Rescate: quien sigue de pie sobre la cúpula sin haber rebotado (el golpe y el solape ya lo cubren casi siempre).
	TArray<AActor*> Touching;
	BounceSensor->GetOverlappingActors(Touching, ACharacter::StaticClass());
	for (AActor* Other : Touching)
	{
		ACharacter* Standing = Cast<ACharacter>(Other);
		if (Standing && Standing->GetMovementBase() == BellCollider.Get())
		{
			TryBounce(Standing);
		}
	}

	// Los recién lanzados siguen sin auto-caparazón durante el vuelo (Landed lo quita al tocar la campana).
	for (auto It = LastBounceTime.CreateIterator(); It; ++It)
	{
		ACharacter* Flyer = It.Key().Get();
		const double Since = Now - It.Value();
		if (!Flyer || Since > 5.0)
		{
			It.RemoveCurrent();
			continue;
		}
		if (Since < 0.8 && TNJellyfishDetail::SimulatesMovement(Flyer))
		{
			ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Flyer);
			const UCharacterMovementComponent* Move = Flyer->GetCharacterMovement();
			if (Turtle && Move && Move->IsFalling())
			{
				Turtle->SetFallImmuneUntilLanded();
			}
		}
	}

	if (GetNetMode() != NM_Client)
	{
		BounceShells(Now);
	}
	AnimateVisuals(DeltaSeconds);
}

void ATN_JellyfishTrampoline::AnimateVisuals(float DeltaSeconds)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Aplastamiento: muelle amortiguado que empieza hundiendo la campana (~0,1 s) y rebota estirándola (~0,3 s).
	SquashAge += DeltaSeconds;
	Wiggle = FMath::Max(0.f, Wiggle - DeltaSeconds * 0.9f);
	const float Wave = -SquashAmp * FMath::Exp(-SquashAge / 0.28f) * FMath::Sin(static_cast<float>(TNPlaygroundKit::KitTwoPi) * 2.6f * SquashAge);
	const float Breath = 0.028f * FMath::Sin(static_cast<float>(AnimClock) * 2.2f + BreathPhase);
	const float ScaleZ = FMath::Max(0.4f, 1.f + Wave + Breath);
	const float ScaleXY = 1.f / FMath::Sqrt(ScaleZ);
	BellPivot->SetRelativeScale3D(FVector(ScaleXY, ScaleXY, ScaleZ));

	if (TentacleMesh && (Wiggle > 0.f || TentacleMesh->WasRecentlyRendered(0.3f)))
	{
		UpdateTentacles();
	}
}
