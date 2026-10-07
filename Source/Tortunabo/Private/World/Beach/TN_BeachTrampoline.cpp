#include "World/Beach/TN_BeachTrampoline.h"
#include "Lobby/Playground/TN_PlaygroundSynthComponent.h"
#include "Player/TN_ShellBody.h"
#include "Core/TN_Log.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/RotationMatrix.h"
#include "ProceduralMeshComponent.h"
#include "TN_BeachRideKit.h"
#include "TN_BeachSignKit.h"
#include "TN_BeachTrapKit.h"
#include "Kismet/GameplayStatics.h"
#include "World/Beach/TN_BeachTrapStatusComponent.h"

/**
 * Geometría de los trampolines (espacio del marco: X hacia el mar, origen en la arena, en el centro). El cuerpo que rebota
 * se construye con caras planas (vértices sin compartir): la abolladura mueve cada vértice según su posición, así que las
 * copias de un mismo punto se mueven igual y la malla no se abre.
 */
namespace TNBeachTrampolineDetail
{
	using FBuffers = TNBeachTrapKit::FBuffers;
	using FHulls = TNBeachTrapKit::FHulls;

	enum : int32
	{
		VariantJelly = 0,
		VariantPillow = 1,
		VariantDonut = 2,
		VariantHat = 3,
	};

	constexpr double SensorMargin = 15.0;
	constexpr double JellyExp = 0.62;

	double JellyZ(double Q, double RimZ, double TopZ)
	{
		const double Q2 = FMath::Clamp(Q * Q, 0.0, 1.0);
		return RimZ + (TopZ - RimZ) * FMath::Pow(1.0 - Q2, JellyExp);
	}

	/** Casco agrandado Margin (para el sensor): cada punto se aleja del centro del casco, y los de arriba suben. */
	TArray<FVector> Grow(const TArray<FVector>& Hull, double Margin)
	{
		FVector Center = FVector::ZeroVector;
		for (const FVector& P : Hull)
		{
			Center += P;
		}
		Center /= static_cast<double>(FMath::Max(1, Hull.Num()));
		TArray<FVector> Out;
		Out.Reserve(Hull.Num());
		for (const FVector& P : Hull)
		{
			const FVector Dir = (P - Center).GetSafeNormal();
			Out.Add(P + Dir * Margin + (P.Z > Center.Z ? FVector(0.0, 0.0, Margin) : FVector::ZeroVector));
		}
		return Out;
	}

	/** Medusa gorda varada: campana con trébol, motas y cara (mirando a -X); casco de la cúpula. */
	void BuildJelly(FBuffers& B, FHulls& Hulls, double R, double RimZ, double TopZ, uint32 Seed)
	{
		static const uint32 Tones[3][5] = { { 0xFFD6EC, 0xFF8CC6, 0xE0569E, 0xFF4FA0, 0xFFF0F8 }, { 0xEEDCFF, 0xC49BFF, 0x8D5BE0, 0xA86BFF, 0xF8F0FF },
			{ 0xD8F4FF, 0x86D3FF, 0x3E9BE0, 0x4FB6FF, 0xF0FAFF } };
		const int32 T = static_cast<int32>((Seed >> 3) % 3u);
		const FLinearColor Top = TNPlaygroundKit::Rgb(Tones[T][0], 0.55f);
		const FLinearColor Mid = TNPlaygroundKit::Rgb(Tones[T][1], 0.55f);
		const FLinearColor Rim = TNPlaygroundKit::Rgb(Tones[T][2], 0.5f);
		const FLinearColor Clover = TNPlaygroundKit::Rgb(Tones[T][3], 0.5f);
		const FLinearColor Spot = TNPlaygroundKit::Rgb(Tones[T][4], 0.6f);
		constexpr int32 Seg = 32;
		static const double Qs[11] = { 0.97, 1.03, 1.0, 0.94, 0.85, 0.74, 0.6, 0.45, 0.3, 0.15, 0.0 };
		constexpr int32 NumQ = 11;
		TArray<FVector> Grid;
		Grid.SetNum(NumQ * Seg);
		for (int32 q = 0; q < NumQ; ++q)
		{
			for (int32 k = 0; k < Seg; ++k)
			{
				const double A = TNPlaygroundKit::KitTwoPi * k / Seg;
				const double Wob = 1.0 + 0.025 * TNProcMesh::TNProcHashNoise(q, k, Seed);
				const double Rad = R * Qs[q] * (q >= 2 ? Wob : 1.0);
				const double Z = q == 0 ? 6.0 : (q == 1 ? RimZ - 8.0 : JellyZ(Qs[q], RimZ, TopZ));
				Grid[q * Seg + k] = FVector(Rad * FMath::Cos(A), Rad * FMath::Sin(A), Z);
			}
		}
		for (int32 q = 0; q + 1 < NumQ; ++q)
		{
			for (int32 k = 0; k < Seg; ++k)
			{
				const int32 K1 = (k + 1) % Seg;
				const double Am = TNPlaygroundKit::KitTwoPi * (k + 0.5) / Seg;
				const double Qm = 0.5 * (Qs[q] + Qs[q + 1]);
				FLinearColor Col = Qm > 0.85 ? Rim : (Qm > 0.45 ? Mid : Top);
				// Trébol: cuatro óvalos alrededor de la cima.
				const double ToLobe = FMath::Abs(FMath::Fmod(FMath::RadiansToDegrees(Am) + 360.0 - 45.0, 90.0) - 45.0);
				if (Qm > 0.2 && Qm < 0.5 && ToLobe > 30.0)
				{
					Col = Clover;
				}
				else if (q >= 3 && TNBeachTrapKit::Hash01(q, k, Seed) < 0.12)
				{
					Col = Spot;
				}
				const FVector Hint = q == 0 ? FVector(FMath::Cos(Am), FMath::Sin(Am), -0.5) : FVector(FMath::Cos(Am), FMath::Sin(Am), 1.4);
				B.AddQuad(Grid[q * Seg + k], Grid[q * Seg + K1], Grid[(q + 1) * Seg + K1], Grid[(q + 1) * Seg + k], Hint, Col);
			}
		}
		// Cara hacia la salida (-X): ojos con brillo y una sonrisa.
		const auto OnDome = [R, RimZ, TopZ](double Q, double AngleDeg, FVector& OutN, FVector& OutT)
		{
			const double A = FMath::DegreesToRadians(AngleDeg);
			const FVector Radial(FMath::Cos(A), FMath::Sin(A), 0.0);
			const double Z = JellyZ(Q, RimZ, TopZ);
			const double Q2 = FMath::Clamp(Q * Q, 0.0, 0.98);
			const double Slope = (TopZ - RimZ) * JellyExp * FMath::Pow(1.0 - Q2, JellyExp - 1.0) * 2.0 * Q / FMath::Max(1.0, R);
			OutN = (Radial * Slope + FVector::UpVector).GetSafeNormal();
			OutT = FVector(-Radial.Y, Radial.X, 0.0);
			return FVector(R * Q * Radial.X, R * Q * Radial.Y, Z);
		};
		const FLinearColor Eye = TNPlaygroundKit::Rgb(0x1B1B2F, 0.4f);
		const FLinearColor Shine = TNPlaygroundKit::Rgb(0xFFFFFF, 0.8f);
		for (const double Side : { -1.0, 1.0 })
		{
			FVector N;
			FVector Tan;
			const FVector P = OnDome(0.58, 180.0 + Side * 13.0, N, Tan);
			const FVector Up = FVector::CrossProduct(N, Tan).GetSafeNormal();
			TNPlaygroundKit::AddEllipsoid(B, P + N * 4.0, N, Tan, Up, FVector(8.0, 24.0, 32.0), 10, 5, Eye);
			TNPlaygroundKit::AddEllipsoid(B, P + N * 10.0 + Up * 12.0 + Tan * 6.0, N, Tan, Up, FVector(3.0, 8.0, 9.0), 6, 3, Shine);
		}
		for (int32 i = 0; i < 7; ++i)
		{
			FVector N;
			FVector Tan;
			const double U = (i - 3) / 3.0;
			const FVector P = OnDome(0.66 + 0.035 * (1.0 - U * U), 180.0 + U * 9.0, N, Tan);
			TNPlaygroundKit::AddEllipsoid(B, P + N * 3.0, N, Tan, FVector::CrossProduct(N, Tan).GetSafeNormal(), FVector(4.0, 13.0, 6.0), 6, 3, Rim);
		}
		// Colisión: la cúpula hasta la arena.
		TArray<FVector> Dome;
		for (const double Q : { 1.0, 0.8, 0.55, 0.3, 0.0 })
		{
			for (int32 k = 0; k < 16; ++k)
			{
				const double A = TNPlaygroundKit::KitTwoPi * k / 16.0;
				Dome.Add(FVector(R * Q * FMath::Cos(A), R * Q * FMath::Sin(A), JellyZ(Q, RimZ, TopZ)));
				if (Q >= 1.0)
				{
					Dome.Add(FVector(0.97 * R * FMath::Cos(A), 0.97 * R * FMath::Sin(A), 0.0));
				}
			}
		}
		Hulls.Add(Dome);
	}

	/** Brazos orales tendidos en la arena (no rebotan). */
	void BuildJellyArms(FBuffers& B, double R, double MaxR, uint32 Seed)
	{
		static const uint32 ArmHex[3] = { 0xE0569E, 0x8D5BE0, 0x3E9BE0 };
		const FLinearColor Col = TNPlaygroundKit::Rgb(ArmHex[(Seed >> 3) % 3u], 0.5f);
		const FLinearColor Light = TNPlaygroundKit::Mix(Col, TNPlaygroundKit::Rgb(0xFFFFFF, 0.5f), 0.45);
		for (int32 i = 0; i < 8; ++i)
		{
			const double A = TNPlaygroundKit::KitTwoPi * (i + 0.4 * TNBeachTrapKit::Hash01(i, 1, Seed)) / 8.0;
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.0);
			const FVector Side(-Dir.Y, Dir.X, 0.0);
			const double R0 = 0.8 * R;
			const double R1 = FMath::Max(R0 + 80.0, MaxR * (0.9 + 0.1 * TNBeachTrapKit::Hash01(i, 2, Seed)));
			const double Wiggle = TNPlaygroundKit::KitTwoPi * TNBeachTrapKit::Hash01(i, 3, Seed);
			constexpr int32 Steps = 8;
			for (int32 s = 0; s < Steps; ++s)
			{
				const double T0 = static_cast<double>(s) / Steps;
				const double T1 = static_cast<double>(s + 1) / Steps;
				const auto At = [&Dir, &Side, R0, R1, Wiggle](double T, double Across)
				{
					const double Off = 45.0 * FMath::Sin(T * 7.0 + Wiggle) * T;
					return Dir * FMath::Lerp(R0, R1, T) + Side * (Off + Across) + FVector(0.0, 0.0, 4.0 + 5.0 * FMath::Sin(T * 5.0 + Wiggle));
				};
				const double W0 = FMath::Lerp(62.0, 16.0, T0);
				const double W1 = FMath::Lerp(62.0, 16.0, T1);
				B.AddQuad(At(T0, -W0), At(T1, -W1), At(T1, W1), At(T0, W0), FVector::UpVector, (s % 2) ? Col : Light);
			}
		}
	}

	/** Colchoneta hinchable: cinco tubos a rayas y la almohada (+X); cascos de caja. */
	void BuildPillow(FBuffers& B, FHulls& Hulls, double HalfLength, double TubeR, double PillowR, uint32 Seed)
	{
		static const uint32 Pairs[4][2] = { { 0x2E86DE, 0xF4F1EA }, { 0xFF7EB6, 0xFFE066 }, { 0x2BB673, 0xF4F1EA }, { 0xFF6A52, 0x4CC9F0 } };
		const uint32 Pick = (Seed >> 3) % 4u;
		const FLinearColor ColA = TNPlaygroundKit::Rgb(Pairs[Pick][0], 0.45f);
		const FLinearColor ColB = TNPlaygroundKit::Rgb(Pairs[Pick][1], 0.45f);
		const double Step = 1.9 * TubeR;
		const double HalfW = 2.0 * Step + TubeR;
		const double PillowX = HalfLength - PillowR;
		for (int32 i = 0; i < 5; ++i)
		{
			const double Y = (i - 2) * Step;
			const FLinearColor Col = (i % 2) ? ColB : ColA;
			const FVector From(-HalfLength + TubeR, Y, TubeR);
			const FVector To(PillowX - 0.6 * PillowR, Y, TubeR);
			TNPlaygroundKit::AddRod(B, From, To, TubeR, 14, Col, FVector::UpVector);
			TNPlaygroundKit::AddBall(B, From, TubeR, 14, Col);
			TNPlaygroundKit::AddBall(B, To, TubeR, 14, Col);
		}
		// Almohada atravesada y la válvula.
		const FLinearColor PillowCol = TNPlaygroundKit::Mix(ColA, ColB, 0.5);
		const FVector P0(PillowX, -(HalfW - PillowR), PillowR);
		const FVector P1(PillowX, HalfW - PillowR, PillowR);
		TNPlaygroundKit::AddRod(B, P0, P1, PillowR, 16, PillowCol, FVector::UpVector);
		TNPlaygroundKit::AddBall(B, P0, PillowR, 16, PillowCol);
		TNPlaygroundKit::AddBall(B, P1, PillowR, 16, PillowCol);
		TNPlaygroundKit::AddFrustum(B, FVector(-HalfLength + 2.5 * TubeR, -2.0 * Step, 2.0 * TubeR - 6.0), FVector(-HalfLength + 2.5 * TubeR, -2.0 * Step, 2.0 * TubeR + 14.0),
			14.0, 11.0, 10, TNPlaygroundKit::Rgb(0xF4F1EA, 0.4f), TNPlaygroundKit::Rgb(0xF4F1EA, 0.4f), false, true);
		Hulls.Add(TNPlaygroundKit::HullAxisBox(FVector(0.0, 0.0, TubeR), FVector(HalfLength, HalfW, TubeR)));
		Hulls.Add(TNPlaygroundKit::HullAxisBox(FVector(PillowX, 0.0, PillowR), FVector(PillowR, HalfW - 10.0, PillowR)));
	}

	/** Flotador de donut con glaseado que gotea y virutas; cascos por sectores (el agujero queda libre). */
	void BuildDonut(FBuffers& B, FHulls& Hulls, double MajorR, double TubeR, uint32 Seed)
	{
		static const uint32 IcingHex[4] = { 0xFF7EB6, 0x7A4A2A, 0x8FD8FF, 0x9BE8C8 };
		const FLinearColor Icing = TNPlaygroundKit::Rgb(IcingHex[(Seed >> 3) % 4u], 0.45f);
		const FLinearColor Dough = TNPlaygroundKit::Rgb(0xE9B872, 0.1f);
		const FLinearColor DoughLight = TNPlaygroundKit::Rgb(0xF3CD8E, 0.1f);
		constexpr int32 NU = 30;
		constexpr int32 NV = 14;
		const double DripPhase = TNPlaygroundKit::KitTwoPi * TNBeachTrapKit::Hash01(1, 1, Seed);
		const auto At = [MajorR, TubeR](double U, double V)
		{
			const double Ring = MajorR + TubeR * FMath::Cos(V);
			return FVector(Ring * FMath::Cos(U), Ring * FMath::Sin(U), TubeR + TubeR * FMath::Sin(V));
		};
		for (int32 i = 0; i < NU; ++i)
		{
			const double U0 = TNPlaygroundKit::KitTwoPi * i / NU;
			const double U1 = TNPlaygroundKit::KitTwoPi * (i + 1) / NU;
			const double Um = 0.5 * (U0 + U1);
			for (int32 j = 0; j < NV; ++j)
			{
				const double V0 = TNPlaygroundKit::KitTwoPi * j / NV;
				const double V1 = TNPlaygroundKit::KitTwoPi * (j + 1) / NV;
				const double Vm = 0.5 * (V0 + V1);
				const FVector Core(MajorR * FMath::Cos(Um), MajorR * FMath::Sin(Um), TubeR);
				const FVector Mid = At(Um, Vm);
				const FVector N = (Mid - Core).GetSafeNormal();
				const double Edge = -0.05 + 0.2 * FMath::Sin(5.0 * Um + DripPhase);
				const double Sv = FMath::Sin(Vm);
				const FLinearColor Col = Sv > Edge ? Icing : (Sv > Edge - 0.25 ? DoughLight : Dough);
				B.AddQuad(At(U0, V0), At(U1, V0), At(U1, V1), At(U0, V1), N, Col);
				// Virutas sobre el glaseado de arriba.
				if (Sv > 0.25 && TNBeachTrapKit::Hash01(i, j, Seed) < 0.3)
				{
					const FVector TanU(-FMath::Sin(Um), FMath::Cos(Um), 0.0);
					const double Spin = TNPlaygroundKit::KitTwoPi * TNBeachTrapKit::Hash01(j, i, Seed + 7u);
					const FVector Along = (TanU * FMath::Cos(Spin) + FVector::CrossProduct(N, TanU) * FMath::Sin(Spin)).GetSafeNormal();
					const FRotator Rot = FRotationMatrix::MakeFromZX(N, Along).Rotator();
					TNPlaygroundKit::AddXfBox(B, FTransform(Rot, Mid + N * 3.0), FVector::ZeroVector, FVector(11.0, 3.5, 3.5),
						TNPlaygroundKit::ToyColor(static_cast<int32>((i * 7 + j) % 7), 0.3f));
				}
			}
		}
		constexpr int32 Sectors = 12;
		for (int32 s = 0; s < Sectors; ++s)
		{
			TArray<FVector> Pts;
			for (int32 u = 0; u <= 2; ++u)
			{
				const double U = TNPlaygroundKit::KitTwoPi * (s + 0.5 * u) / Sectors;
				for (int32 v = 0; v < 12; ++v)
				{
					Pts.Add(At(U, TNPlaygroundKit::KitTwoPi * v / 12.0));
				}
			}
			Hulls.Add(Pts);
		}
	}

	/** Sombrero de paja tenso: ala ancha y baja que se riza en el borde, copa con la tapa tensa, cinta y lazo. */
	void BuildHat(FBuffers& B, FHulls& Hulls, double CrownR, double BrimR, double CrownTop, uint32 Seed)
	{
		const FLinearColor StrawA = TNPlaygroundKit::Rgb(0xEBD292, 0.05f);
		const FLinearColor StrawB = TNPlaygroundKit::Rgb(0xD9BC74, 0.05f);
		static const uint32 BandHex[4] = { 0xD7263D, 0x1B3A6B, 0x222222, 0x2EC4B6 };
		const FLinearColor Band = TNPlaygroundKit::Rgb(BandHex[(Seed >> 3) % 4u], 0.25f);
		constexpr int32 Seg = 40;
		const auto BrimZ = [CrownR, BrimR](double Rho)
		{
			const double T = FMath::Clamp((Rho - CrownR) / FMath::Max(1.0, BrimR - CrownR), 0.0, 1.0);
			return 22.0 + 16.0 * T * T;
		};
		// Ala: cara de arriba en anillos, canto y cara de abajo.
		static const double Rings[5] = { 0.0, 0.3, 0.6, 0.85, 1.0 };
		for (int32 r = 0; r < 4; ++r)
		{
			const double Ra = FMath::Lerp(CrownR, BrimR, Rings[r]);
			const double Rb = FMath::Lerp(CrownR, BrimR, Rings[r + 1]);
			for (int32 k = 0; k < Seg; ++k)
			{
				const double A0 = TNPlaygroundKit::KitTwoPi * k / Seg;
				const double A1 = TNPlaygroundKit::KitTwoPi * (k + 1) / Seg;
				const double Wa = 1.0 + 0.02 * FMath::Sin(A0 * 6.0);
				const double Wb = 1.0 + 0.02 * FMath::Sin(A1 * 6.0);
				const FVector P00(Ra * FMath::Cos(A0), Ra * FMath::Sin(A0), BrimZ(Ra));
				const FVector P01(Ra * FMath::Cos(A1), Ra * FMath::Sin(A1), BrimZ(Ra));
				const FVector P11(Rb * Wb * FMath::Cos(A1), Rb * Wb * FMath::Sin(A1), BrimZ(Rb) + (r == 3 ? 4.0 * FMath::Sin(A1 * 5.0) : 0.0));
				const FVector P10(Rb * Wa * FMath::Cos(A0), Rb * Wa * FMath::Sin(A0), BrimZ(Rb) + (r == 3 ? 4.0 * FMath::Sin(A0 * 5.0) : 0.0));
				B.AddQuad(P00, P01, P11, P10, FVector::UpVector, ((k + r) % 2) ? StrawA : StrawB);
				B.AddQuad(P00 - FVector(0.0, 0.0, 10.0), P01 - FVector(0.0, 0.0, 10.0), P11 - FVector(0.0, 0.0, 10.0), P10 - FVector(0.0, 0.0, 10.0), -FVector::UpVector,
					TNPlaygroundKit::Shade(StrawB, 0.8));
				if (r == 3)
				{
					const double Am = 0.5 * (A0 + A1);
					B.AddQuad(P10, P11, P11 - FVector(0.0, 0.0, 10.0), P10 - FVector(0.0, 0.0, 10.0), FVector(FMath::Cos(Am), FMath::Sin(Am), 0.0), TNPlaygroundKit::Shade(StrawA, 0.9));
				}
			}
		}
		// Copa: pared un poco en cono y tapa tensa con una comba mínima.
		constexpr int32 Tiers = 4;
		for (int32 t = 0; t < Tiers; ++t)
		{
			const double Z0 = FMath::Lerp(20.0, CrownTop, static_cast<double>(t) / Tiers);
			const double Z1 = FMath::Lerp(20.0, CrownTop, static_cast<double>(t + 1) / Tiers);
			const double R0 = CrownR * (1.0 - 0.06 * t / Tiers);
			const double R1 = CrownR * (1.0 - 0.06 * (t + 1) / Tiers);
			for (int32 k = 0; k < Seg; ++k)
			{
				const double A0 = TNPlaygroundKit::KitTwoPi * k / Seg;
				const double A1 = TNPlaygroundKit::KitTwoPi * (k + 1) / Seg;
				const double Am = 0.5 * (A0 + A1);
				B.AddQuad(FVector(R0 * FMath::Cos(A0), R0 * FMath::Sin(A0), Z0), FVector(R0 * FMath::Cos(A1), R0 * FMath::Sin(A1), Z0),
					FVector(R1 * FMath::Cos(A1), R1 * FMath::Sin(A1), Z1), FVector(R1 * FMath::Cos(A0), R1 * FMath::Sin(A0), Z1), FVector(FMath::Cos(Am), FMath::Sin(Am), 0.1),
					((k + t) % 2) ? StrawA : StrawB);
			}
		}
		const double TopR = CrownR * 0.94;
		for (int32 r = 0; r < 3; ++r)
		{
			const double Ra = TopR * (1.0 - r / 3.0);
			const double Rb = TopR * (1.0 - (r + 1) / 3.0);
			const double Za = CrownTop + 10.0 * (1.0 - FMath::Square(Ra / TopR));
			const double Zb = CrownTop + 10.0 * (1.0 - FMath::Square(Rb / TopR));
			for (int32 k = 0; k < Seg; ++k)
			{
				const double A0 = TNPlaygroundKit::KitTwoPi * k / Seg;
				const double A1 = TNPlaygroundKit::KitTwoPi * (k + 1) / Seg;
				B.AddQuad(FVector(Ra * FMath::Cos(A0), Ra * FMath::Sin(A0), Za), FVector(Ra * FMath::Cos(A1), Ra * FMath::Sin(A1), Za),
					FVector(Rb * FMath::Cos(A1), Rb * FMath::Sin(A1), Zb), FVector(Rb * FMath::Cos(A0), Rb * FMath::Sin(A0), Zb), FVector::UpVector,
					((k / 2 + r) % 2) ? StrawA : StrawB);
			}
		}
		// Cinta y lazo (hacia la salida).
		TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, 22.0), FVector(0.0, 0.0, 72.0), CrownR + 4.0, CrownR * 0.985 + 4.0, Seg, Band, Band, false, false);
		const FVector Knot(-(CrownR + 10.0), 0.0, 48.0);
		TNPlaygroundKit::AddBall(B, Knot, 20.0, 8, TNPlaygroundKit::Shade(Band, 0.85));
		for (const double Side : { -1.0, 1.0 })
		{
			TNPlaygroundKit::AddEllipsoid(B, Knot + FVector(-6.0, Side * 44.0, 4.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector,
				FVector(12.0, 40.0, 24.0), 8, 4, Band);
			TNPlaygroundKit::AddEllipsoid(B, Knot + FVector(-14.0, Side * 30.0, -38.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector,
				FVector(8.0, 16.0, 36.0), 6, 3, TNPlaygroundKit::Shade(Band, 0.9));
		}
		Hulls.Add(TNPlaygroundKit::HullCylinder(FVector::ZeroVector, 38.0, BrimR, BrimR - 30.0, 24));
		Hulls.Add(TNPlaygroundKit::HullCylinder(FVector::ZeroVector, CrownTop + 10.0, CrownR + 4.0, CrownR * 0.94, 20));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachTrampoline
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachTrampoline::ATN_BeachTrampoline()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);

	Frame = CreateDefaultSubobject<USceneComponent>(TEXT("Frame"));
	Frame->SetupAttachment(GetRootComponent());

	BodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
	BodyPivot->SetupAttachment(Frame);

	BodyMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(BodyPivot);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetGenerateOverlapEvents(false);
	BodyMesh->SetCanEverAffectNavigation(false);

	DecorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DecorMesh"));
	DecorMesh->SetupAttachment(Frame);
	TNBeachTrapKit::ConfigureVisual(DecorMesh);

	// Cuerpo: bloquea como el resto del mundo (la cámara lo atraviesa) y no se sube andando: todo él rebota.
	BodyCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BodyCollision"));
	BodyCollision->SetupAttachment(Frame);
	TNBeachTrapKit::ConfigureSolid(BodyCollision, false);
	BodyCollision->CanCharacterStepUpOn = ECB_No;

	// Sensor: la misma forma 15 cm más grande que solo solapa con personajes.
	BounceSensor = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BounceSensor"));
	BounceSensor->SetupAttachment(Frame);
	BounceSensor->bUseComplexAsSimpleCollision = false;
	BounceSensor->bUseAsyncCooking = false;
	BounceSensor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BounceSensor->SetCollisionObjectType(ECC_WorldDynamic);
	BounceSensor->SetCollisionResponseToAllChannels(ECR_Ignore);
	BounceSensor->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	BounceSensor->SetGenerateOverlapEvents(true);
	BounceSensor->SetCanEverAffectNavigation(false);

	// Cartel de madera (sin colisión): tabla con el icono y rótulo.
	SignPivot = CreateDefaultSubobject<USceneComponent>(TEXT("SignPivot"));
	SignPivot->SetupAttachment(Frame);
	SignMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SignMesh"));
	SignMesh->SetupAttachment(SignPivot);
	TNBeachTrapKit::ConfigureVisual(SignMesh);
	SignText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("SignText"));
	SignText->SetupAttachment(SignPivot);
	TNBeachSignKit::ConfigureText(SignText);
}

void ATN_BeachTrampoline::ApplySpec()
{
	using namespace TNBeachTrampolineDetail;
	const double Fit = TNBeachTrapKit::FitRadius(Spec.Element, Spec.SizeScale);
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 109u);
	Variant = static_cast<int32>(Seed % 4u);
	BreathPhase = static_cast<float>(TNPlaygroundKit::KitTwoPi * TNBeachTrapKit::Hash01(2, 2, Seed));

	TNBeachTrapKit::FBuffers Body;
	TNBeachTrapKit::FBuffers Decor;
	TNBeachTrapKit::FHulls Hulls;
	const double Sc = FMath::Clamp(Fit / 700.0, 0.85, 1.15);
	TentacleR = 0.0;
	switch (Variant)
	{
	case VariantJelly:
		BodyR = 0.72 * Fit;
		RimZ = 40.0;
		TopZ = FMath::Clamp(0.45 * Fit, 260.0, 340.0);
		BuildJelly(Body, Hulls, BodyR, RimZ, TopZ, Seed);
		BuildJellyArms(Decor, BodyR, 0.96 * Fit, Seed);
		TentacleR = 0.96 * Fit;
		UpScale = 1.f;
		BoingPitch = 1.f;
		break;
	case VariantPillow:
	{
		const double TubeR = 72.0 * Sc;
		const double PillowR = 95.0 * Sc;
		HalfLength = 0.7 * Fit;
		HalfWidth = 2.0 * 1.9 * TubeR + TubeR;
		RimZ = 2.0 * TubeR;
		TopZ = 2.0 * PillowR;
		BodyR = FVector2D(HalfLength, HalfWidth).Size();
		BuildPillow(Body, Hulls, HalfLength, TubeR, PillowR, Seed);
		UpScale = 0.92f;
		BoingPitch = 1.3f;
		break;
	}
	case VariantDonut:
	{
		const double MajorR = 0.58 * Fit;
		const double TubeR = 0.3 * Fit;
		InnerR = MajorR - TubeR;
		BodyR = MajorR + TubeR;
		RimZ = TubeR;
		TopZ = 2.0 * TubeR;
		BuildDonut(Body, Hulls, MajorR, TubeR, Seed);
		UpScale = 1.08f;
		BoingPitch = 0.85f;
		break;
	}
	default:
		CrownR = 0.38 * Fit;
		BodyR = 0.93 * Fit;
		TopZ = FMath::Clamp(0.3 * Fit, 180.0, 240.0);
		RimZ = 38.0;
		BuildHat(Body, Hulls, CrownR, BodyR, TopZ, Seed);
		TopZ += 10.0;
		UpScale = 0.96f;
		BoingPitch = 1.5f;
		break;
	}
	// Arena removida alrededor (no rebota).
	TNPlaygroundKit::AddDisc(Decor, FVector(0.0, 0.0, 1.0), FVector::UpVector, FMath::Min(0.97 * Fit, BodyR + 40.0), 28, TNBeachTrapKit::SandMark());
	TNBeachTrapKit::SetMesh(DecorMesh, this, Decor, TN_ART("Beach.Trampoline.Decor"));
	PlaceSign(Fit, Seed);

	BodyCollision->SetCollisionConvexMeshes(Hulls);
	TNBeachTrapKit::FHulls Grown;
	for (const TArray<FVector>& Hull : Hulls)
	{
		Grown.Add(Grow(Hull, SensorMargin));
	}
	BounceSensor->SetCollisionConvexMeshes(Grown);

	// Cuerpo que se deforma: malla procedural (solo en máquinas con pantalla).
	RestVerts = Body.Verts;
	RestNormals = Body.Normals;
	WorkVerts = Body.Verts;
	BodyMesh->ClearAllMeshSections();
	if (GetNetMode() != NM_DedicatedServer && !Body.IsEmpty())
	{
		const TArray<FProcMeshTangent> NoTangents;
		BodyMesh->CreateMeshSection_LinearColor(0, Body.Verts, Body.Tris, Body.Normals, Body.UVs, Body.Colors, NoTangents, false);
		BodyMesh->SetMaterial(0, TNPlaygroundKit::VertexColorMaterial());
	}
	bBodyDirty = false;
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Trampolín %s: variante %d, radio %.0f cm, alto %.0f cm."), *GetName(), Variant, BodyR, TopZ);
}

void ATN_BeachTrampoline::PlaceSign(double Fit, uint32 Seed)
{
	// Por el lado por el que se llega (-X del marco), 24° a un lado (no delante del salto) y por fuera de todo lo que
	// rebota, con la tabla de cara hacia fuera: a quien llega.
	const double SideSign = TNBeachSignKit::SideOf(Spec.Seed);
	const double AngDeg = 180.0 - SideSign * 24.0;
	const FVector2D Dir(FMath::Cos(FMath::DegreesToRadians(AngDeg)), FMath::Sin(FMath::DegreesToRadians(AngDeg)));
	double Reach = 0.0;
	for (double R = 0.0; R <= 2.0 * Fit + 200.0; R += 10.0)
	{
		if (IsNearBody(FVector(Dir.X * R, Dir.Y * R, 20.0), 0.0) || IsNearBody(FVector(Dir.X * R, Dir.Y * R, 0.5 * TopZ), 0.0))
		{
			Reach = R;
		}
	}
	const double SignR = FMath::Max(Reach, 0.8 * BodyR) + 80.0;
	SignYawDeg = AngDeg - 180.0;
	SignPivot->SetRelativeLocationAndRotation(FVector(Dir.X * SignR, Dir.Y * SignR, 0.0), FRotator(0.0, SignYawDeg, 0.0));
	SignPivot->SetRelativeScale3D(FVector::OneVector);
	bSignMoving = false;
	TNBeachTrapKit::FBuffers Sign;
	TNBeachSignKit::BuildSign(Sign, Seed);
	TNBeachTrapKit::SetMesh(SignMesh, this, Sign, TN_ART("Beach.Trampoline.Sign"));
	TNBeachSignKit::SetText(SignText, NSLOCTEXT("TNBeach", "TrampolineSign", "¡BOING!"), TNBeachSignKit::TextColor());
	SignGlowApplied = -1.f;
}

void ATN_BeachTrampoline::BeginPlay()
{
	Super::BeginPlay();
	// Las tortugas rebotan desde su movimiento (UTN_TurtleMovementComponent, al empezar cada paso en que tocan el sensor),
	// no desde el golpe ni el solape: así cae en el mismo paso en el servidor y en el cliente dueño (#21).
	if (GetNetMode() != NM_DedicatedServer)
	{
		Toy = UTN_PlaygroundSynthComponent::AttachTo(this, Frame->GetComponentLocation() + FVector(0.0, 0.0, 0.6 * TopZ), 600.f, 3000.f);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Rebote
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_BeachTrampoline::IsNearBody(const FVector& Local, double Margin) const
{
	using namespace TNBeachTrampolineDetail;
	const double Rho = FVector2D(Local.X, Local.Y).Size();
	if (Local.Z < -Margin)
	{
		return false;
	}
	switch (Variant)
	{
	case VariantJelly:
		return Rho <= BodyR + Margin && Local.Z <= JellyZ(FMath::Min(1.0, Rho / FMath::Max(1.0, BodyR)), RimZ, TopZ) + Margin;
	case VariantPillow:
		return FMath::Abs(Local.X) <= HalfLength + Margin && FMath::Abs(Local.Y) <= HalfWidth + Margin
			&& Local.Z <= (Local.X > HalfLength - TopZ ? TopZ : RimZ) + Margin;
	case VariantDonut:
	{
		const double Core = 0.5 * (BodyR + InnerR);
		const double Tube = 0.5 * (BodyR - InnerR);
		return FVector2D(Rho - Core, Local.Z - Tube).Size() <= Tube + Margin;
	}
	default:
		return (Rho <= BodyR + Margin && Local.Z <= RimZ + Margin) || (Rho <= CrownR + Margin && Local.Z <= TopZ + Margin);
	}
}

bool ATN_BeachTrampoline::IsBounceSensor(const UPrimitiveComponent* Component) const
{
	return Component && Component == BounceSensor.Get();
}

TNTrampolineRules::FBounceTuning ATN_BeachTrampoline::TurtleTuning() const
{
	TNTrampolineRules::FBounceTuning Tuning;
	Tuning.BaseUp = static_cast<double>(BaseUp) * UpScale;
	Tuning.FallGain = FallGain;
	Tuning.MaxUp = EffectiveMaxUp();
	Tuning.KeepHorizontal = KeepHorizontal;
	Tuning.Push = EffectivePush();
	Tuning.MaxHorizontal = EffectiveMaxHorizontal();
	return Tuning;
}

bool ATN_BeachTrampoline::ComputeTurtleBounce(const FVector& Velocity, FVector& OutLaunch, float& OutStrength) const
{
	if (!TNTrampolineRules::CanBounce(Velocity))
	{
		return false;
	}
	// Caer de más alto rebota más (con tope); la horizontal se conserva en parte y se empuja hacia el mar (el potenciado,
	// mucho más). Solo con la velocidad del paso: lo mismo en el servidor, en el cliente dueño y al repetir el paso.
	const TNTrampolineRules::FBounceTuning Tuning = TurtleTuning();
	OutLaunch = TNTrampolineRules::BounceVelocity(Velocity, Frame->GetForwardVector().GetSafeNormal2D(), Tuning);
	OutStrength = TNTrampolineRules::BounceStrength(OutLaunch.Z, Tuning);
	return true;
}

void ATN_BeachTrampoline::NotifyTurtleBounced(ACharacter* Turtle, float Strength)
{
	UWorld* World = GetWorld();
	if (!Turtle || !World)
	{
		return;
	}
	// El efecto, como mucho uno por BounceCooldown (un techo justo encima puede devolverla al trampolín en el paso siguiente).
	const double Now = World->GetTimeSeconds();
	const TWeakObjectPtr<ACharacter> Key(Turtle);
	const double* Last = LastBounceTime.Find(Key);
	const bool bShowFX = !Last || Now - *Last >= static_cast<double>(BounceCooldown);
	LastBounceTime.Add(Key, Now);
	if (bShowFX)
	{
		SpreadBounceFX(Turtle, Strength);
	}
}

double ATN_BeachTrampoline::DropOntoTrampoline(const ACharacter& Character, double MaxDrop)
{
	const UWorld* World = Character.GetWorld();
	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	if (!World || !Capsule || MaxDrop <= 0.0)
	{
		return TNTrampolineRules::NoTrampolineBelow;
	}
	// La cápsula en vertical con el canal de los personajes, sin chocar con otras tortugas: los toques llegan antes que el
	// primer bloqueo, así que el sensor (solapa) cuenta aunque el cuerpo (bloquea) esté debajo.
	const FVector From = Capsule->GetComponentLocation();
	const FVector To = From - FVector(0.0, 0.0, MaxDrop);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNTrampolineBelow), false, &Character);
	FCollisionResponseParams Response;
	Capsule->InitSweepCollisionParams(Params, Response);
	Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	TArray<FHitResult> Hits;
	World->SweepMultiByChannel(Hits, From, To, Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(), Params,
		Response);
	for (const FHitResult& Hit : Hits)
	{
		if (Cast<ATN_BeachTrampoline>(Hit.GetActor()))
		{
			return FMath::Max(0.0, static_cast<double>(Hit.Distance));
		}
	}
	return TNTrampolineRules::NoTrampolineBelow;
}

void ATN_BeachTrampoline::BounceShells(double Now)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FTransform FrameXf = Frame->GetComponentTransform();
	const FVector Sea = Frame->GetForwardVector().GetSafeNormal2D();
	for (TActorIterator<ATN_ShellBody> It(World); It; ++It)
	{
		ATN_ShellBody* ShellActor = *It;
		UBoxComponent* ShellBox = ShellActor ? ShellActor->GetBox() : nullptr;
		if (!ShellBox || !ShellBox->IsSimulatingPhysics())
		{
			continue;
		}
		const FVector Local = FrameXf.InverseTransformPosition(ShellBox->GetComponentLocation());
		const FVector Vel = ShellBox->GetPhysicsLinearVelocity();
		if (Local.Z < 15.0 || Vel.Z > 120.0 || !IsNearBody(Local, 45.0))
		{
			continue;
		}
		const TWeakObjectPtr<AActor> Key(ShellActor);
		if (const double* Last = LastShellBounce.Find(Key))
		{
			if (Now - *Last < BounceCooldown + 0.2)
			{
				continue;
			}
		}
		LastShellBounce.Add(Key, Now);
		// La física del caparazón se replica desde el servidor: basta con cambiar su velocidad aquí.
		const double Base = 0.9 * static_cast<double>(BaseUp) * UpScale;
		const double Up = FMath::Min(static_cast<double>(EffectiveMaxUp()), Base + FallGain * FMath::Max(0.0, -Vel.Z - 300.0));
		const FVector Horizontal = (FVector(Vel.X, Vel.Y, 0.0) * KeepHorizontal + Sea * EffectivePush()).GetClampedToMaxSize(EffectiveMaxHorizontal());
		ShellBox->SetPhysicsLinearVelocity(FVector(Horizontal.X, Horizontal.Y, Up));
		SpreadBounceFX(ShellActor->GetTurtle(), 0.8f);
	}
	for (auto It = LastShellBounce.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || Now - It.Value() > 5.0)
		{
			It.RemoveCurrent();
		}
	}
}

void ATN_BeachTrampoline::SpreadBounceFX(APawn* Bouncer, float Strength)
{
	if (GetNetMode() != NM_Client)
	{
		// Servidor (o partida local): a todas las máquinas; el multicast también se ejecuta aquí.
		MulticastBounceFX(Bouncer, Strength);
		ForceNetUpdate();
	}
	else if (Bouncer && Bouncer->IsLocallyControlled())
	{
		// Cliente dueño: lo ve y lo oye al predecir, sin esperar al servidor.
		PlayBounceFX(Bouncer->GetActorLocation(), Strength);
	}
}

void ATN_BeachTrampoline::MulticastBounceFX_Implementation(APawn* Bouncer, float Strength)
{
	if (GetNetMode() == NM_Client && Bouncer && Bouncer->IsLocallyControlled())
	{
		return;
	}
	PlayBounceFX(Bouncer ? Bouncer->GetActorLocation() : Frame->GetComponentLocation() + FVector(0.0, 0.0, TopZ), Strength);
}

void ATN_BeachTrampoline::PlayBounceFX(const FVector& WorldAt, float Strength)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const float S = FMath::Clamp(Strength, 0.3f, 1.f);
	DentLocal = BodyMesh->GetComponentTransform().InverseTransformPosition(WorldAt);
	DentAge = 0.f;
	DentAmp = 22.f + 42.f * S;
	SquashAge = 0.f;
	SquashAmp = 0.1f + 0.16f * S;
	// Los grandes suenan más graves.
	const float SizePitch = FMath::Clamp(1.f / FMath::Pow(FMath::Max(0.3f, Spec.SizeScale), 0.4f), 0.7f, 1.4f);
	if (Toy)
	{
		Toy->TriggerSound(ETNPlaygroundSound::Boing, BoingPitch * SizePitch * FMath::FRandRange(0.94f, 1.06f), BoingVolume * FMath::Clamp(S, 0.4f, 1.f));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick y deformación
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachTrampoline::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	// Los recién lanzados siguen sin auto-caparazón durante el vuelo. (Quien sigue de pie sobre el cuerpo ya no necesita
	// rescate: su movimiento lo rebota en cuanto toca el sensor sin subir.)
	for (auto It = LastBounceTime.CreateIterator(); It; ++It)
	{
		ACharacter* Flyer = It.Key().Get();
		const double Since = Now - It.Value();
		if (!Flyer || Since > 5.0)
		{
			It.RemoveCurrent();
			continue;
		}
		if (Since < 0.8 && TNBeachTrapKit::SimulatesMovement(Flyer))
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
		StingTurtles(Now);
	}
	AnimateBody(DeltaSeconds);

	// Cartel: un botecito al acercarse la tortuga de esta máquina y el rótulo más claro mientras está cerca.
	if (GetNetMode() != NM_DedicatedServer)
	{
		float Stretch = 0.f;
		float Sway = 0.f;
		TNBeachSignKit::TickSignAnim(DeltaSeconds, World, SignPivot->GetComponentLocation(), SignAge, bSignNear, SignGlow, Stretch, Sway);
		const bool bMoving = FMath::Abs(Stretch) > 0.0005f || FMath::Abs(Sway) > 0.01f;
		if (bMoving || bSignMoving)
		{
			SignPivot->SetRelativeRotation(FRotator(0.0, SignYawDeg, bMoving ? Sway : 0.f));
			SignPivot->SetRelativeScale3D(FVector(1.0, 1.0, bMoving ? 1.0 + Stretch : 1.0));
			bSignMoving = bMoving;
		}
		TNBeachSignKit::ApplySignGlow(SignText, TNBeachSignKit::TextColor(), SignGlow, SignGlowApplied);
	}
}

void ATN_BeachTrampoline::AnimateBody(float DeltaSeconds)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	AnimClock += DeltaSeconds;
	SquashAge += DeltaSeconds;
	DentAge += DeltaSeconds;

	// Aplastamiento entero: muelle que empieza hundiendo y rebota estirando (y la medusa respira).
	const float Wave = -SquashAmp * FMath::Exp(-SquashAge / 0.28f) * FMath::Sin(static_cast<float>(TNPlaygroundKit::KitTwoPi) * 2.4f * SquashAge);
	const float Breath = Variant == TNBeachTrampolineDetail::VariantJelly ? 0.02f * FMath::Sin(static_cast<float>(AnimClock) * 2.2f + BreathPhase) : 0.f;
	const float ScaleZ = FMath::Max(0.5f, 1.f + Wave + Breath);
	const float ScaleXY = 1.f / FMath::Sqrt(ScaleZ);
	BodyPivot->SetRelativeScale3D(FVector(ScaleXY, ScaleXY, ScaleZ));

	// Abolladura donde cae: se hunde, vibra y se recupera.
	if (RestVerts.Num() == 0 || BodyMesh->GetNumSections() == 0)
	{
		return;
	}
	if (DentAge < 1.1f && BodyMesh->WasRecentlyRendered(0.5f))
	{
		WorkVerts = RestVerts;
		const double Env = -static_cast<double>(DentAmp) * FMath::Exp(-static_cast<double>(DentAge) / 0.22)
			* FMath::Cos(TNPlaygroundKit::KitTwoPi * 2.2 * static_cast<double>(DentAge));
		const double Sigma2 = FMath::Square(160.0 + 0.22 * BodyR);
		const double Height = FMath::Max(1.0, TopZ);
		for (int32 i = 0; i < WorkVerts.Num(); ++i)
		{
			const FVector& P = RestVerts[i];
			const double D2 = FMath::Square(P.X - DentLocal.X) + FMath::Square(P.Y - DentLocal.Y);
			const double Fall = FMath::Exp(-D2 / Sigma2);
			const double Weight = FMath::Clamp(P.Z / Height, 0.15, 1.0);
			WorkVerts[i].Z = P.Z + Env * Fall * Weight;
		}
		const TArray<FVector2D> KeepUVs;
		const TArray<FColor> KeepColors;
		const TArray<FProcMeshTangent> KeepTangents;
		BodyMesh->UpdateMeshSection(0, WorkVerts, RestNormals, KeepUVs, KeepColors, KeepTangents);
		bBodyDirty = true;
	}
	else if (bBodyDirty)
	{
		const TArray<FVector2D> KeepUVs;
		const TArray<FColor> KeepColors;
		const TArray<FProcMeshTangent> KeepTangents;
		BodyMesh->UpdateMeshSection(0, RestVerts, RestNormals, KeepUVs, KeepColors, KeepTangents);
		bBodyDirty = false;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tentáculos de la medusa (#683): aturdimiento corto y ralentización, sin daño ni veneno
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachTrampoline::StingTurtles(double Now)
{
	if (Variant != TNBeachTrampolineDetail::VariantJelly || TentacleR <= BodyR)
	{
		return;
	}
	TArray<ATortugaCharacter*> Turtles;
	ATN_BeachEnemy::GatherTurtles(this, Turtles);
	const FTransform Xf = Frame->GetComponentTransform();
	for (ATortugaCharacter* Turtle : Turtles)
	{
		const FVector Local = Xf.InverseTransformPositionNoScale(Turtle->GetActorLocation());
		const double FeetZ = Local.Z - Turtle->GetSimpleCollisionHalfHeight();
		const double Rho = FVector2D(Local.X, Local.Y).Size();
		// Los tentáculos están tendidos en la arena: pican hasta un poco por encima del labio de la campana.
		if (TNTrampolineRules::TentacleContact(Rho, FeetZ, BodyR, TentacleR, RimZ + 30.0) != TNTrampolineRules::ETentacleContact::Sting)
		{
			continue;
		}
		const double* Last = LastSting.Find(Turtle);
		if ((Last && Now - *Last < TNTrampolineRules::StingCooldown) || !TNBeachTrapKit::IsFreeTurtle(Turtle))
		{
			continue;
		}
		LastSting.Add(Turtle, Now);
		TNBeach::StunTurtle(Turtle, TNTrampolineRules::StingStunSeconds);
		if (UTN_BeachTrapStatusComponent* Status = UTN_BeachTrapStatusComponent::FindOrAddTo(Turtle))
		{
			Status->ServerSlow(TNTrampolineRules::StingSpeedFactor, TNTrampolineRules::StingSlowSeconds);
		}
		MulticastSting(Turtle->GetActorLocation());
	}
}

void ATN_BeachTrampoline::MulticastSting_Implementation(FVector_NetQuantize At)
{
	if (StingSound && GetNetMode() != NM_DedicatedServer)
	{
		UGameplayStatics::SpawnSoundAtLocation(this, StingSound, At);
	}
}
