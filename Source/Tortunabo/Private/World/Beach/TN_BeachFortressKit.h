#pragma once

#include "CoreMinimal.h"
#include "Math/RotationMatrix.h"
#include "World/Beach/TN_BeachTypes.h"
#include "TN_BeachBoostKit.h"
#include "TN_BeachTrapKit.h"

/**
 * Planta y malla de las fortalezas de arena (ATN_BeachFortress; Docs/Modo_Carrera.md, «Fortalezas de arena»). Espacio
 * de la fortaleza en cm: origen en la arena, en el centro; X = sentido de la carrera (hacia el mar), Z arriba. Todo se
 * construye sin espejo (la rampa del patio a +Y, las torrecillas a -Y) y, según la semilla, se refleja en Y al final.
 *
 * Una fortaleza es una muralla cuadrada con cuatro torres (adarve arriba, puertas a -X y +X, patio dentro) y en medio
 * una «mota» de terrazas macizas de arena, cada una más alta y más pequeña, con la cima arriba:
 *
 *   - mediana: muralla de 28 m (adarve a 5 m) y cima de 11 m de lado a 8,5 m (con las banderas, ~14 m);
 *   - grande: muralla de 43 m (adarve a 6,5 m), terraza a 11 m y cima de 12,6 m de lado a 16,5 m (~22 m);
 *   - colosal: muralla de 64 m (adarve a 8 m), terrazas a 12,5 y 19 m y cima de 14,8 m de lado a 25,5 m (~32 m).
 *
 * Subidas (todas se andan: rampas de 14-28°, escalones de 40 cm como mucho, saltos de 70 cm como mucho):
 *   - fuera: rampa por la cara -Y de la muralla hasta el adarve y escalera por la cara +Y;
 *   - dentro: escalera del patio al adarve de +X (junto a la puerta del mar) y rampa del patio a la primera terraza por
 *     la franja +Y (a media subida pasa junto al adarve de +Y: se cambia de una a otra);
 *   - de terraza en terraza: rampas pegadas a la cara de la de arriba, alternando -Y y +Y (una espiral);
 *   - atajos arriesgados: torrecillas de cubo que se saltan del adarve de -Y a la primera terraza por la franja -Y (en la
 *     colosal, otra fila de la primera terraza a la segunda) y una pala tendida del adarve de -X a una cornisa de 70 cm
 *     que sube por la cara -X de la primera terraza.
 *
 * Premios (la cima): el lanzador potenciado en su borde +X, el cofre y las conchas de 50 y 100 (más alguna de 50 al final
 * de cada atajo en las terrazas de en medio).
 */
namespace TNBeachFortressKit
{
	using FBuffers = TNBeachTrapKit::FBuffers;
	using FHulls = TNBeachTrapKit::FHulls;

	// ── Medidas de tortuga (cm): no escalan con el tamaño ──
	constexpr double MinScale = 0.85;
	constexpr double MaxScale = 1.2;
	constexpr double FloorZ = 15.0;
	constexpr double TerraceRampW = 340.0;
	constexpr double InStairW = 260.0;
	constexpr double ExtRampW = 380.0;
	constexpr double ExtStairW = 300.0;
	constexpr double LedgeW = 70.0;
	constexpr double LedgeThick = 50.0;
	constexpr double GateW = 440.0;
	constexpr double GateMaxH = 480.0;
	constexpr double StepRun = 45.0;
	constexpr double MaxStepRise = 40.0;
	constexpr double MaxHop = 70.0;
	constexpr double ParapetT = 60.0;
	constexpr double ParapetH = 55.0;
	constexpr double MerlonH = 45.0;
	constexpr double ExtRampDeg = 20.0;
	constexpr double TerraceRampDeg = 24.0;
	constexpr double LedgeDeg = 30.0;
	constexpr double SpadeW = 70.0;
	constexpr double SpadeThick = 14.0;
	/** Margen de la cima entre el pretil y lo que se pone encima. */
	constexpr double SummitMargin = 70.0;

	/** Borde de una terraza o de la muralla. */
	enum class ESide : uint8
	{
		PosX,
		NegX,
		PosY,
		NegY
	};

	/** Terraza maciza: semilado y altura de su suelo. */
	struct FTier
	{
		double K = 0.0;
		double Z = 0.0;
	};

	/** Hueco en un pretil (Tier = -1: la muralla), a lo largo del borde (Y en los bordes ±X, X en los ±Y). */
	struct FGap
	{
		int32 Tier = -1;
		ESide Side = ESide::PosX;
		double From = 0.0;
		double To = 0.0;
	};

	/** Fila de torrecillas de cubo que se saltan de una en una. */
	struct FChain
	{
		double Radius = 100.0;
		double BaseZ = 0.0;
		/** Centro de la cara de arriba de cada una, en el orden de los saltos. */
		TArray<FVector> Tops;
	};

	/** Rampa de terraza en terraza: pegada a la cara -Y o +Y de la terraza Upper, de XLow (abajo) a XHigh (arriba). */
	struct FTerraceRamp
	{
		int32 Upper = 1;
		bool bNegY = true;
		double XLow = 0.0;
		double XHigh = 0.0;
	};

	/** Concha de puntos: dónde (sobre el suelo, sin la altura de la concha) y cuánto vale. */
	struct FShellSpot
	{
		FVector At = FVector::ZeroVector;
		int32 Value = 50;
	};

	struct FPlan
	{
		/** 0 = mediana, 1 = grande, 2 = colosal. */
		int32 Size = 0;
		double S = 1.0;
		double Fit = 2200.0;
		double A0 = 1400.0;
		double T0 = 380.0;
		double Z0 = 500.0;
		double In = 1020.0;
		double TowerR = 290.0;
		double TowerInset = 100.0;
		double GateH = 380.0;
		double TopH = 1400.0;
		TArray<FTier> Tiers;

		double ExtRampX0 = 0.0;
		double ExtRampX1 = 0.0;
		int32 ExtSteps = 0;
		double ExtRise = 40.0;
		double ExtStairX0 = 0.0;
		int32 InSteps = 0;
		double InRise = 40.0;
		double InStairY0 = 0.0;
		double SpadeY = 0.0;
		double LedgeY0 = 0.0;
		double LedgeY1 = 0.0;
		TArray<FTerraceRamp> TerraceRamps;
		FChain ChainA;
		FChain ChainB;
		TArray<FGap> Gaps;

		bool bCatapult = true;
		float LauncherSize = 1.f;
		FVector LauncherAt = FVector::ZeroVector;
		FVector ChestAt = FVector::ZeroVector;
		double ChestYaw = 0.0;
		TArray<FShellSpot> Shells;

		const FTier& Summit() const { return Tiers.Last(); }
	};

	/** Tamaño (0-2) de cada tipo de fortaleza. */
	inline int32 SizeOf(ETNBeachElement Element)
	{
		return Element == ETNBeachElement::FortressColossal ? 2 : (Element == ETNBeachElement::FortressLarge ? 1 : 0);
	}

	/** Escala de la planta (el SizeScale del reparto, recortado a lo que deja pasillos de tortuga). */
	inline double ScaleOf(float SizeScale)
	{
		return FMath::Clamp(static_cast<double>(SizeScale), MinScale, MaxScale);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Planta
	// ─────────────────────────────────────────────────────────────────────────

	/**
	 * Fila de torrecillas entre dos bordes paralelos: a lo largo de un eje (Along), alternando entre Cross1 (junto al
	 * borde de salida) y Cross2 (junto al de llegada), de AlongFirst hacia AlongLast. Radius sale del hueco disponible.
	 */
	inline FChain MakeChain(double Cross1, double Cross2, double RadiusIn, double AlongLast, double AlongMin, double AlongMax, bool bAlongX, double BaseZ,
		double ZFrom, double ZTo, bool bMarchNegative)
	{
		FChain Out;
		Out.Radius = RadiusIn;
		Out.BaseZ = BaseZ;
		const double D = 2.0 * RadiusIn + 110.0;
		double C1 = Cross1;
		double C2 = Cross2;
		if (FMath::Abs(C1 - C2) > 0.8 * D)
		{
			// Más separadas de lo que se salta con holgura: se acercan hacia el centro.
			const double Mid = 0.5 * (C1 + C2);
			const double Sgn = C1 < C2 ? -1.0 : 1.0;
			C1 = Mid + Sgn * 0.4 * D;
			C2 = Mid - Sgn * 0.4 * D;
		}
		const double Delta = FMath::Abs(C1 - C2);
		double Step = FMath::Sqrt(FMath::Max(D * D - Delta * Delta, 150.0 * 150.0));
		int32 Count = FMath::Max(2, FMath::CeilToInt32((ZTo - ZFrom) / MaxHop));
		Count += Count % 2;
		const double Hop = (ZTo - ZFrom) / (Count + 1);
		// La última, junto al borde de llegada en AlongLast; las demás hacia atrás, sin salirse de [AlongMin, AlongMax].
		const double Dir = bMarchNegative ? -1.0 : 1.0;
		double First = AlongLast - Dir * (Count - 1) * Step;
		if (!bMarchNegative && First < AlongMin)
		{
			Step = (AlongLast - AlongMin) / (Count - 1);
			First = AlongMin;
		}
		else if (bMarchNegative && First > AlongMax)
		{
			Step = (AlongMax - AlongLast) / (Count - 1);
			First = AlongMax;
		}
		for (int32 k = 0; k < Count; ++k)
		{
			const double Along = First + Dir * k * Step;
			const double Cross = (k % 2 == 0) ? C1 : C2;
			const double Z = ZFrom + (k + 1) * Hop;
			Out.Tops.Add(bAlongX ? FVector(Along, Cross, Z) : FVector(Cross, Along, Z));
		}
		return Out;
	}

	/** La semilla ya no decide nada del plan (antes, catapulta o trampolín; ahora siempre catapulta): se queda por quien llama. */
	inline FPlan MakePlan(ETNBeachElement Element, float SizeScale, uint32 /*Seed*/)
	{
		FPlan P;
		P.Size = SizeOf(Element);
		P.S = ScaleOf(SizeScale);
		P.Fit = TNBeach::FootprintRadius(Element) * P.S;
		const double S = P.S;
		switch (P.Size)
		{
		case 0:
			P.A0 = 1400.0 * S;
			P.T0 = 380.0 * S;
			P.Z0 = 500.0 * S;
			P.TowerR = 290.0 * S;
			P.TowerInset = 100.0 * S;
			P.TopH = 1400.0 * S;
			P.Tiers = { { 560.0 * S, 850.0 * S } };
			break;
		case 1:
			P.A0 = 2150.0 * S;
			P.T0 = 440.0 * S;
			P.Z0 = 650.0 * S;
			P.TowerR = 340.0 * S;
			P.TowerInset = 130.0 * S;
			P.TopH = 2200.0 * S;
			P.Tiers = { { 1250.0 * S, 1100.0 * S }, { 630.0 * S, 1650.0 * S } };
			break;
		default:
			P.A0 = 3200.0 * S;
			P.T0 = 520.0 * S;
			P.Z0 = 800.0 * S;
			P.TowerR = 400.0 * S;
			P.TowerInset = 150.0 * S;
			P.TopH = 3200.0 * S;
			P.Tiers = { { 2100.0 * S, 1250.0 * S }, { 1400.0 * S, 1900.0 * S }, { 740.0 * S, 2550.0 * S } };
			break;
		}
		P.In = P.A0 - P.T0;
		P.GateH = FMath::Min(GateMaxH, P.Z0 - 120.0);
		const double K1 = P.Tiers[0].K;
		const double Z1 = P.Tiers[0].Z;

		// Fuera: rampa por la cara -Y hasta el adarve (sube hacia +X y acaba antes de la torre) y escalera por la +Y.
		P.ExtRampX1 = P.A0 - P.TowerInset - P.TowerR - 50.0;
		P.ExtRampX0 = P.ExtRampX1 - P.Z0 / FMath::Tan(FMath::DegreesToRadians(ExtRampDeg));
		P.Gaps.Add({ -1, ESide::NegY, P.ExtRampX1 - 420.0, P.ExtRampX1 + 30.0 });
		P.ExtSteps = FMath::CeilToInt32(P.Z0 / MaxStepRise);
		P.ExtRise = P.Z0 / P.ExtSteps;
		P.ExtStairX0 = 0.5 * P.ExtSteps * StepRun;
		const double ExtTopX = P.ExtStairX0 - P.ExtSteps * StepRun;
		P.Gaps.Add({ -1, ESide::PosY, ExtTopX - 40.0, ExtTopX + StepRun + 280.0 });

		// Dentro: escalera del patio al adarve de +X (baja junto a la puerta del mar y sube hacia -Y).
		P.InSteps = FMath::CeilToInt32((P.Z0 - FloorZ) / MaxStepRise);
		P.InRise = (P.Z0 - FloorZ) / P.InSteps;
		P.InStairY0 = -(0.5 * GateW + 60.0);

		// Rampa del patio (franja +Y) hasta la primera terraza: llega junto a su esquina +X.
		P.Gaps.Add({ 0, ESide::PosY, K1 - 450.0, K1 + 10.0 });

		// Atajo de la pala y la cornisa: del adarve de -X a la cara -X de la primera terraza.
		P.SpadeY = K1 - 200.0;
		P.LedgeY0 = P.SpadeY - 0.5 * SpadeW;
		P.LedgeY1 = P.LedgeY0 - (Z1 - P.Z0) / FMath::Tan(FMath::DegreesToRadians(LedgeDeg));
		P.Gaps.Add({ 0, ESide::NegX, P.LedgeY1 - 90.0, P.LedgeY1 + 300.0 });

		// Torrecillas de la franja -Y: del adarve de -Y a la primera terraza.
		{
			const double Gap = P.In - K1;
			const double Rp = FMath::Clamp((Gap - 50.0 - 220.0) * 0.5, 90.0, 160.0);
			const double Last = K1 - Rp - 60.0;
			P.ChainA = MakeChain(-(P.In - Rp - 25.0), -(K1 + Rp + 25.0), Rp, Last, -P.In + Rp + 30.0, Last, true, FloorZ, P.Z0, Z1, false);
			P.Gaps.Add({ 0, ESide::NegY, Last - 200.0, Last + 200.0 });
		}

		// De terraza en terraza: rampas pegadas a la cara de la de arriba, alternando -Y (hacia -X) y +Y (hacia +X).
		for (int32 j = 1; j < P.Tiers.Num(); ++j)
		{
			const FTier& Lower = P.Tiers[j - 1];
			const FTier& Upper = P.Tiers[j];
			const double Run = FMath::Min(2.0 * Upper.K - 80.0, (Upper.Z - Lower.Z) / FMath::Tan(FMath::DegreesToRadians(TerraceRampDeg)));
			FTerraceRamp Ramp;
			Ramp.Upper = j;
			Ramp.bNegY = (j % 2) == 1;
			if (Ramp.bNegY)
			{
				Ramp.XHigh = -(Upper.K - 40.0);
				Ramp.XLow = Ramp.XHigh + Run;
				P.Gaps.Add({ j, ESide::NegY, Ramp.XHigh - 40.0, Ramp.XHigh + 380.0 });
			}
			else
			{
				Ramp.XHigh = Upper.K - 40.0;
				Ramp.XLow = Ramp.XHigh - Run;
				P.Gaps.Add({ j, ESide::PosY, Ramp.XHigh - 380.0, Ramp.XHigh + 40.0 });
			}
			P.TerraceRamps.Add(Ramp);
		}

		// Colosal: otra fila de torrecillas por la terraza +X de la primera a la segunda.
		if (P.Tiers.Num() >= 3)
		{
			const double K2 = P.Tiers[1].K;
			const double Gap = K1 - K2;
			const double Rp = FMath::Clamp((Gap - 50.0 - 220.0 - ParapetT) * 0.5, 90.0, 160.0);
			const double Last = -(K2 - Rp - 80.0);
			P.ChainB = MakeChain(K1 - ParapetT - Rp - 20.0, K2 + Rp + 25.0, Rp, Last, Last, K1 - Rp - 220.0, false, Z1, Z1, P.Tiers[1].Z, true);
			P.Gaps.Add({ 1, ESide::PosX, Last - 200.0, Last + 200.0 });
		}

		// ── Cima: lanzador potenciado en el borde +X, cofre y conchas ──
		const double Ks = P.Summit().K;
		const double Zs = P.Summit().Z;
		// Siempre catapulta (antes, el 45 % de las veces, un trampolín que no compensaba la subida; #741): la que lanza más lejos
		// hacia delante, a los 80-90 m que el reparto deja libres (FortressLandingAt).
		P.bCatapult = true;
		// Brazo de lado a lado de la cima (las medidas de ATN_BeachCatapult::ApplySpec: largo 1,22·huella, entre 8,2 y 11,5 m).
		const double Sc = FMath::Clamp((2.0 * Ks - 2.0 * SummitMargin) / (1.22 * 900.0), 0.72, 1.15);
		const double Fc = 900.0 * Sc;
		const double Len = FMath::Clamp(1.22 * Fc, 820.0, 1150.0);
		const double PivotX = FMath::Clamp(0.2 * Fc, 110.0, 190.0);
		P.LauncherSize = static_cast<float>(Sc);
		P.LauncherAt = FVector(Ks - SummitMargin - PivotX - 0.32 * Len, 0.0, Zs);
		// Cofre (ATN_BeachChest: 3,4 m de ancho) en el cuarto -X/+Y, al lado del cazo de la catapulta, con el frente (+X) hacia
		// el mar como el lanzador: lo que suelta cae delante, en la cima. Da lo mejor de la carrera (TNBeach::FlagSummitPrize).
		P.ChestAt = FVector(-0.5 * Ks, 0.54 * Ks, Zs);
		P.ChestYaw = 0.0;
		const double C = Ks - 130.0;
		switch (P.Size)
		{
		case 0:
			P.Shells = { { FVector(C, -C, Zs), 100 }, { FVector(-C, -C, Zs), 50 } };
			break;
		case 1:
			P.Shells = { { FVector(C, -C, Zs), 100 }, { FVector(-C, -C, Zs), 50 }, { FVector(C, C, Zs), 50 } };
			break;
		default:
			P.Shells = { { FVector(C, -C, Zs), 100 }, { FVector(-C, -C, Zs), 100 }, { FVector(C, C, Zs), 50 }, { FVector(-C, C, Zs), 50 } };
			break;
		}
		if (P.Tiers.Num() >= 2)
		{
			// Al final de cada atajo, en la primera terraza (y en la segunda, la de la otra fila).
			if (P.ChainA.Tops.Num() > 0)
			{
				P.Shells.Add({ FVector(P.ChainA.Tops.Last().X, -(K1 - 130.0), Z1), 50 });
			}
			P.Shells.Add({ FVector(-(K1 - 130.0), P.LedgeY1 - 40.0, Z1), 50 });
		}
		if (P.Tiers.Num() >= 3 && P.ChainB.Tops.Num() > 0)
		{
			P.Shells.Add({ FVector(P.Tiers[1].K - 130.0, P.ChainB.Tops.Last().Y, P.Tiers[1].Z), 50 });
		}
		return P;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Piezas
	// ─────────────────────────────────────────────────────────────────────────

	inline FLinearColor WindowDark() { return TNPlaygroundKit::Rgb(0x6A4A2C); }
	inline FLinearColor Trim() { return TNPlaygroundKit::Shade(TNBeachTrapKit::SandMark(), 0.85); }

	/** Bloque de arena de molde (lados, tapa clara y marcas de cubo cada MarkStep desde MarkFrom) con su casco. */
	inline void AddSand(FBuffers& B, FHulls* Hulls, double X0, double X1, double Y0, double Y1, double Z0, double Z1, double MarkFrom = 0.0, double MarkStep = 75.0)
	{
		const FVector Min(FMath::Min(X0, X1), FMath::Min(Y0, Y1), FMath::Min(Z0, Z1));
		const FVector Max(FMath::Max(X0, X1), FMath::Max(Y0, Y1), FMath::Max(Z0, Z1));
		TNBeachTrapKit::AddSandBox(B, Hulls, Min, Max, false);
		if (MarkStep < 1.0 || Max.X - Min.X < 1.0 || Max.Y - Min.Y < 1.0)
		{
			return;
		}
		const FVector Center = (Min + Max) * 0.5;
		const FVector Half = (Max - Min) * 0.5;
		for (double Z = FMath::Max(Min.Z, MarkFrom) + 55.0; Z < Max.Z - 30.0; Z += MarkStep)
		{
			TNPlaygroundKit::AddAxisBox(B, FVector(Center.X, Center.Y, Z), FVector(Half.X + 2.5, Half.Y + 2.5, 4.0), TNBeachTrapKit::SandMark());
		}
	}

	/** Rampa de arena en planta de From a To (sube de ZLow a ZHigh), de HalfWidth de semiancho y la base en ZBottom. */
	inline void AddRamp(FBuffers& B, FHulls& Hulls, const FVector2D& From, const FVector2D& To, double HalfWidth, double ZLow, double ZHigh, double ZBottom)
	{
		const FVector2D D = To - From;
		const double Len = D.Size();
		if (Len < 1.0)
		{
			return;
		}
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
		TNBeachTrapKit::AddSandRamp(B, &Hulls, FTransform(FRotator(0.0, Yaw, 0.0), FVector(From.X, From.Y, 0.0)), Len, HalfWidth, ZLow, ZHigh, ZBottom);
	}

	/**
	 * Cornisa: repisa de X0 a X1 (70 cm) que sube a lo largo de Y de (YA, ZA) a (YB, ZB) con Thick de grueso, colgada de la
	 * cara de la terraza (sin nada debajo salvo unas ménsulas de adorno).
	 */
	inline void AddLedge(FBuffers& B, FBuffers& Decor, FHulls& Hulls, double X0, double X1, double YA, double ZA, double YB, double ZB, double Thick)
	{
		const FVector T0(X0, YA, ZA);
		const FVector T1(X1, YA, ZA);
		const FVector T2(X1, YB, ZB);
		const FVector T3(X0, YB, ZB);
		const FVector Dn(0.0, 0.0, Thick);
		const double Sy = YB > YA ? 1.0 : -1.0;
		B.AddQuad(T0, T1, T2, T3, FVector::UpVector, TNBeachTrapKit::SandTop());
		B.AddQuad(T0 - Dn, T1 - Dn, T2 - Dn, T3 - Dn, -FVector::UpVector, TNBeachTrapKit::SandDeep());
		B.AddQuad(T0, T3, T3 - Dn, T0 - Dn, -FVector::ForwardVector, TNBeachTrapKit::SandSide());
		B.AddQuad(T1, T2, T2 - Dn, T1 - Dn, FVector::ForwardVector, TNBeachTrapKit::SandSide());
		B.AddQuad(T0, T1, T1 - Dn, T0 - Dn, FVector(0.0, -Sy, 0.0), TNBeachTrapKit::SandSide());
		B.AddQuad(T3, T2, T2 - Dn, T3 - Dn, FVector(0.0, Sy, 0.0), TNBeachTrapKit::SandSide());
		Hulls.Add(TArray<FVector>{ T0, T1, T2, T3, T0 - Dn, T1 - Dn, T2 - Dn, T3 - Dn });
		// Ménsulas: cuñas bajo la repisa cada ~1,6 m, pegadas a la cara (X1 = la cara de la terraza).
		const double Len = FMath::Abs(YB - YA);
		const int32 Count = FMath::Max(1, FMath::FloorToInt32(Len / 160.0));
		for (int32 i = 0; i <= Count; ++i)
		{
			const double U = static_cast<double>(i) / Count;
			const double Y = FMath::Lerp(YA, YB, U);
			const double Z = FMath::Lerp(ZA, ZB, U) - Thick;
			const FVector Face(X1, Y, Z);
			const FVector Out(X0, Y, Z);
			const FVector Low(X1, Y, Z - 55.0);
			for (const double Dy : { -9.0, 9.0 })
			{
				Decor.AddTri(Face + FVector(0.0, Dy, 0.0), Out + FVector(0.0, Dy, 0.0), Low + FVector(0.0, Dy, 0.0), FVector(0.0, Dy, 0.0), TNBeachTrapKit::SandMark());
			}
			Decor.AddQuad(Out + FVector(0.0, -9.0, 0.0), Out + FVector(0.0, 9.0, 0.0), Low + FVector(0.0, 9.0, 0.0), Low + FVector(0.0, -9.0, 0.0), FVector(X0 - X1, 0.0, -1.0),
				TNPlaygroundKit::Shade(TNBeachTrapKit::SandMark(), 0.9));
		}
	}

	/** Almenas de adorno a lo largo de un pretil (bAlongX: X de A0 a A1 en Y = TMid; si no, Y en X = TMid). */
	inline void AddMerlons(FBuffers& Decor, bool bAlongX, double A0, double A1, double TMid, double Thick, double ZTop)
	{
		const int32 Count = FMath::Max(2, FMath::RoundToInt32((A1 - A0) / 110.0));
		const double Step = (A1 - A0) / Count;
		for (int32 m = 0; m < Count; m += 2)
		{
			const double Cm = A0 + (m + 0.5) * Step;
			const FVector Center = bAlongX ? FVector(Cm, TMid, ZTop + 0.5 * MerlonH) : FVector(TMid, Cm, ZTop + 0.5 * MerlonH);
			const FVector Half = bAlongX ? FVector(Step * 0.45, Thick * 0.46, 0.5 * MerlonH) : FVector(Thick * 0.46, Step * 0.45, 0.5 * MerlonH);
			TNPlaygroundKit::AddAxisBox(Decor, Center, Half, TNBeachTrapKit::SandTop());
		}
	}

	/** Tramos de [From, To] que quedan fuera de los huecos de ese borde. */
	inline TArray<FVector2D> OpenSpans(const TArray<FGap>& Gaps, int32 Tier, ESide Side, double From, double To)
	{
		TArray<FVector2D> Spans = { FVector2D(From, To) };
		for (const FGap& Gap : Gaps)
		{
			if (Gap.Tier != Tier || Gap.Side != Side)
			{
				continue;
			}
			TArray<FVector2D> Next;
			for (const FVector2D& Span : Spans)
			{
				if (Gap.To <= Span.X || Gap.From >= Span.Y)
				{
					Next.Add(Span);
					continue;
				}
				if (Gap.From > Span.X)
				{
					Next.Add(FVector2D(Span.X, Gap.From));
				}
				if (Gap.To < Span.Y)
				{
					Next.Add(FVector2D(Gap.To, Span.Y));
				}
			}
			Spans = MoveTemp(Next);
		}
		return Spans;
	}

	/**
	 * Pretil (con colisión: se salta, no se pasa andando) y almenas de adorno por un borde, de From a To a lo largo de él,
	 * sin los huecos de ese borde. Edge = la coordenada del borde (+K o -K).
	 */
	inline void AddParapet(FBuffers& B, FBuffers& Decor, FHulls& Hulls, const TArray<FGap>& Gaps, int32 Tier, ESide Side, double Edge, double From, double To, double Z)
	{
		const bool bAlongX = Side == ESide::PosY || Side == ESide::NegY;
		const double Inward = (Side == ESide::PosX || Side == ESide::PosY) ? -1.0 : 1.0;
		const double T0 = Edge;
		const double T1 = Edge + Inward * ParapetT;
		for (const FVector2D& Span : OpenSpans(Gaps, Tier, Side, From, To))
		{
			if (Span.Y - Span.X < 40.0)
			{
				continue;
			}
			if (bAlongX)
			{
				AddSand(B, &Hulls, Span.X, Span.Y, T0, T1, Z, Z + ParapetH, 0.0, 0.0);
			}
			else
			{
				AddSand(B, &Hulls, T0, T1, Span.X, Span.Y, Z, Z + ParapetH, 0.0, 0.0);
			}
			AddMerlons(Decor, bAlongX, Span.X, Span.Y, 0.5 * (T0 + T1), ParapetT, Z + ParapetH);
		}
	}

	/** Ventana de adorno en una cara (Normal hacia fuera): hueco oscuro, arco y alféizar. */
	inline void AddWindow(FBuffers& Decor, const FVector& Center, const FVector& Normal, double Width, double Height)
	{
		const FTransform Xf(FRotationMatrix::MakeFromXZ(Normal, FVector::UpVector).Rotator(), Center);
		TNPlaygroundKit::AddXfBox(Decor, Xf, FVector(2.0, 0.0, 0.0), FVector(3.0, 0.5 * Width, 0.5 * Height), WindowDark());
		TNPlaygroundKit::AddXfBox(Decor, Xf, FVector(4.0, 0.0, 0.5 * Height + 12.0), FVector(5.0, 0.5 * Width + 14.0, 12.0), Trim());
		TNPlaygroundKit::AddXfBox(Decor, Xf, FVector(6.0, 0.0, -0.5 * Height - 5.0), FVector(7.0, 0.5 * Width + 10.0, 5.0), TNBeachTrapKit::SandTop());
	}

	/** Puerta de adorno al pie de una cara: arco oscuro, hoja de tablones, herrajes y la estrella dorada encima. */
	inline void AddFakeDoor(FBuffers& Decor, const FVector& Foot, const FVector& Normal, double Width, double Height)
	{
		const FTransform Xf(FRotationMatrix::MakeFromXZ(Normal, FVector::UpVector).Rotator(), Foot);
		TNPlaygroundKit::AddXfBox(Decor, Xf, FVector(2.0, 0.0, 0.5 * Height), FVector(3.0, 0.5 * Width + 20.0, 0.5 * Height + 14.0), Trim());
		const int32 Planks = 5;
		for (int32 i = 0; i < Planks; ++i)
		{
			const double Y = -0.5 * Width + (i + 0.5) * Width / Planks;
			TNPlaygroundKit::AddXfBox(Decor, Xf, FVector(6.0, Y, 0.5 * Height), FVector(3.0, 0.5 * Width / Planks - 2.0, 0.5 * Height - 6.0),
				TNBeachTrapKit::WoodTone(i));
		}
		for (const double Z : { 0.25 * Height, 0.72 * Height })
		{
			TNPlaygroundKit::AddXfBox(Decor, Xf, FVector(10.0, 0.0, Z), FVector(2.0, 0.5 * Width - 8.0, 7.0), TNPlaygroundKit::Rgb(0x3B3F4A, 0.5f));
		}
		TNBeachBoostKit::AddStar(Decor, Xf.TransformPosition(FVector(5.0, 0.0, Height + 60.0)), Normal, FVector::UpVector, 45.0, TNBeachBoostKit::Gold());
	}

	/**
	 * Torrecilla de cubo (arena moldeada con un cubo boca abajo): tronco de cono de Radius arriba, crestas del molde y un
	 * borde; con su casco. Top = el centro de su cara de arriba.
	 */
	inline void AddPillar(FBuffers& B, FBuffers& Decor, FHulls& Hulls, const FVector& Top, double BaseZ, double Radius, uint32 Seed, int32 Index)
	{
		const FVector Base(Top.X, Top.Y, BaseZ - 20.0);
		const double BaseR = Radius * 1.12;
		TNPlaygroundKit::AddFrustum(B, Base, Top, BaseR, Radius, 16, TNBeachTrapKit::SandSide(), TNBeachTrapKit::SandTop(), false, true);
		for (double Z = BaseZ + 45.0; Z < Top.Z - 30.0; Z += 48.0)
		{
			const double U = (Z - Base.Z) / FMath::Max(1.0, Top.Z - Base.Z);
			const double Rr = FMath::Lerp(BaseR, Radius, U) + 3.0;
			TNPlaygroundKit::AddFrustum(Decor, FVector(Top.X, Top.Y, Z - 4.0), FVector(Top.X, Top.Y, Z + 4.0), Rr, Rr, 16, TNBeachTrapKit::SandMark(), TNBeachTrapKit::SandMark(),
				false, false);
		}
		TNPlaygroundKit::AddFrustum(Decor, Top - FVector(0.0, 0.0, 14.0), Top - FVector(0.0, 0.0, 1.0), Radius + 7.0, Radius + 7.0, 16, TNBeachTrapKit::SandMark(),
			TNBeachTrapKit::SandTop(), false, false);
		// Una concha o una estrella pegada al costado.
		const double Ang = TNPlaygroundKit::KitTwoPi * TNBeachTrapKit::Hash01(Index, 3, Seed);
		const FVector Dir(FMath::Cos(Ang), FMath::Sin(Ang), 0.0);
		const double Zs = FMath::Lerp(BaseZ + 40.0, Top.Z - 40.0, 0.35 + 0.4 * TNBeachTrapKit::Hash01(Index, 4, Seed));
		const double Rs = FMath::Lerp(BaseR, Radius, (Zs - Base.Z) / FMath::Max(1.0, Top.Z - Base.Z));
		if (Index % 2 == 0)
		{
			TNPlaygroundKit::AddShellFan(Decor, FVector(Top.X, Top.Y, Zs) + Dir * (Rs + 1.0), Dir, FVector::UpVector, 26.0, TNBeachTrapKit::ShellTone(Index));
		}
		else
		{
			TNPlaygroundKit::AddStarfish(Decor, FVector(Top.X, Top.Y, Zs) + Dir * (Rs + 1.0), Dir, FVector::UpVector, 24.0, 3.0, TNPlaygroundKit::Rgb(0xFF8A70));
		}
		Hulls.Add(TNPlaygroundKit::HullCylinder(Base, Top.Z - Base.Z, BaseR, Radius, 12));
	}

	/**
	 * Cubo de juguete boca abajo como torreta (en las torres de la muralla): boca con reborde abajo, nervios, pegatina de
	 * estrella y el asa colgando; con su casco. Devuelve la altura de su cara de arriba.
	 */
	inline double AddBucketTurret(FBuffers& Decor, FHulls& Hulls, const FVector& Foot, double Radius, double Height, const FLinearColor& Color, const FVector& Outward)
	{
		const FVector Top = Foot + FVector(0.0, 0.0, Height);
		const double TopR = Radius * 0.8;
		TNPlaygroundKit::AddFrustum(Decor, Foot, Top, Radius, TopR, 20, Color, TNPlaygroundKit::Shade(Color, 1.08), false, true);
		TNPlaygroundKit::AddFrustum(Decor, Foot - FVector(0.0, 0.0, 2.0), Foot + FVector(0.0, 0.0, 16.0), Radius + 9.0, Radius + 9.0, 20, TNPlaygroundKit::Shade(Color, 1.12),
			TNPlaygroundKit::Shade(Color, 1.12), false, true);
		for (const double K : { 0.35, 0.7 })
		{
			const double Rr = FMath::Lerp(Radius, TopR, K) + 3.0;
			const FVector Ring = Foot + FVector(0.0, 0.0, Height * K);
			TNPlaygroundKit::AddFrustum(Decor, Ring - FVector(0.0, 0.0, 5.0), Ring + FVector(0.0, 0.0, 5.0), Rr, Rr, 20, TNPlaygroundKit::Shade(Color, 0.88),
				TNPlaygroundKit::Shade(Color, 0.88), false, false);
		}
		// Asa: medio aro que cuelga de las dos orejetas, hacia fuera.
		const FVector Out = Outward.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Outward.GetSafeNormal2D();
		const FVector Side(-Out.Y, Out.X, 0.0);
		const double LugZ = Height * 0.78;
		const double LugR = FMath::Lerp(Radius, TopR, 0.78) + 6.0;
		TArray<FVector> Handle;
		for (int32 i = 0; i <= 10; ++i)
		{
			const double T = TNPlaygroundKit::KitPi * i / 10.0;
			const double Across = FMath::Cos(T) * LugR;
			const double Drop = FMath::Sin(T) * Height * 0.42;
			Handle.Add(Foot + Side * Across + Out * (FMath::Sin(T) * (Radius * 0.35) + 10.0) + FVector(0.0, 0.0, LugZ - Drop));
		}
		TNPlaygroundKit::AddTube(Decor, Handle, { 5.0 }, 6, { TNPlaygroundKit::Shade(Color, 0.8) }, FVector::UpVector, false);
		const double StarR = FMath::Lerp(Radius, TopR, 0.45);
		TNBeachBoostKit::AddStar(Decor, Foot + Out * (StarR + 2.0) + FVector(0.0, 0.0, Height * 0.45), Out, FVector::UpVector, Radius * 0.28,
			TNPlaygroundKit::Rgb(0xFFF1A8, 0.3f));
		Hulls.Add(TNPlaygroundKit::HullCylinder(Foot, Height, Radius + 9.0, TopR, 12));
		return Top.Z;
	}

	/** Pala de juguete tendida como puente: hoja sobre el adarve (de XBlade0 a XBlade1) y mango de XBlade1 a XEnd, en Y, a Z. */
	inline void AddSpadeBridge(FBuffers& Decor, FHulls& Hulls, double XBlade0, double XBlade1, double XEnd, double Y, double Z, uint32 Seed)
	{
		static const uint32 SpadeHex[4] = { 0xE63946, 0x1D7FD1, 0xFFB703, 0x2BB673 };
		const FLinearColor Plastic = TNPlaygroundKit::Rgb(SpadeHex[Seed % 4u], 0.35f);
		// Mango: de la hoja a la cornisa, con la cara de arriba a Z.
		const double Hx0 = FMath::Min(XBlade1, XEnd);
		const double Hx1 = FMath::Max(XBlade1, XEnd);
		TNPlaygroundKit::AddAxisBox(Decor, FVector(0.5 * (Hx0 + Hx1), Y, Z - 0.5 * SpadeThick), FVector(0.5 * (Hx1 - Hx0), 0.5 * SpadeW, 0.5 * SpadeThick), Plastic);
		for (const double Sy : { -1.0, 1.0 })
		{
			TNPlaygroundKit::AddAxisBox(Decor, FVector(0.5 * (Hx0 + Hx1), Y + Sy * (0.5 * SpadeW - 4.0), Z + 1.0), FVector(0.5 * (Hx1 - Hx0), 4.0, 1.5),
				TNPlaygroundKit::Shade(Plastic, 1.12));
		}
		Hulls.Add(TNPlaygroundKit::HullAxisBox(FVector(0.5 * (Hx0 + Hx1), Y, Z - 0.5 * SpadeThick), FVector(0.5 * (Hx1 - Hx0) + 8.0, 0.5 * SpadeW, 0.5 * SpadeThick)));
		// Hoja: plancha con la punta redondeada y el borde de atrás levantado, apoyada en el adarve.
		const double Bx0 = FMath::Min(XBlade0, XBlade1);
		const double Bx1 = FMath::Max(XBlade0, XBlade1);
		const double BladeLen = Bx1 - Bx0;
		const double BladeW = 170.0;
		const bool bTipAtLow = XBlade0 < XBlade1;
		TArray<FVector2D> Outline;
		for (int32 i = 0; i <= 6; ++i)
		{
			const double A = -0.5 * TNPlaygroundKit::KitPi + TNPlaygroundKit::KitPi * i / 6.0;
			Outline.Add(FVector2D((bTipAtLow ? -1.0 : 1.0) * (0.5 * BladeLen - 0.5 * BladeW * 0.6 + 0.5 * BladeW * 0.6 * FMath::Cos(A)), 0.5 * BladeW * FMath::Sin(A)));
		}
		Outline.Add(FVector2D((bTipAtLow ? 1.0 : -1.0) * 0.5 * BladeLen, 0.5 * BladeW * 0.7));
		Outline.Add(FVector2D((bTipAtLow ? 1.0 : -1.0) * 0.5 * BladeLen, -0.5 * BladeW * 0.7));
		// AddSlab orienta cada cara por su normal: el sentido del contorno da igual.
		TNPlaygroundKit::AddSlab(Decor, FVector(0.5 * (Bx0 + Bx1), Y, Z + 3.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, Outline, 6.0, Plastic);
		// Nervio central de la hoja (plano: se pisa sin tropezar).
		TNPlaygroundKit::AddAxisBox(Decor, FVector(0.5 * (Bx0 + Bx1), Y, Z + 6.5), FVector(0.4 * BladeLen, 6.0, 1.0), TNPlaygroundKit::Shade(Plastic, 0.88));
		Hulls.Add(TNPlaygroundKit::HullAxisBox(FVector(0.5 * (Bx0 + Bx1), Y, Z + 3.0), FVector(0.5 * BladeLen, 0.5 * BladeW * 0.7, 3.0)));
	}

	/** Anillo de pretil (con colisión) y almenas por el arco de fuera de una torre, de A0 a A1 (grados). */
	inline void AddTowerRing(FBuffers& B, FBuffers& Decor, FHulls& Hulls, const FVector2D& C, double Radius, double Z, double A0Deg, double A1Deg)
	{
		const int32 Segs = FMath::Max(3, FMath::CeilToInt32((A1Deg - A0Deg) / 22.0));
		const double Da = (A1Deg - A0Deg) / Segs;
		const double Mid = Radius - 0.5 * ParapetT;
		for (int32 s = 0; s < Segs; ++s)
		{
			const double Am = A0Deg + (s + 0.5) * Da;
			const double Chord = 2.0 * Radius * FMath::Sin(FMath::DegreesToRadians(0.5 * Da)) + 4.0;
			const FVector Dir(FMath::Cos(FMath::DegreesToRadians(Am)), FMath::Sin(FMath::DegreesToRadians(Am)), 0.0);
			const FTransform Xf(FRotator(0.0, Am, 0.0), FVector(C.X, C.Y, 0.0) + Dir * Mid);
			const FVector Half(0.5 * ParapetT, 0.5 * Chord, 0.5 * ParapetH);
			TNPlaygroundKit::AddXfBox(B, Xf, FVector(0.0, 0.0, Z + 0.5 * ParapetH), Half, TNBeachTrapKit::SandSide());
			Hulls.Add(TNPlaygroundKit::HullBox(Xf, FVector(0.0, 0.0, Z + 0.5 * ParapetH), Half));
			if (s % 2 == 0)
			{
				TNPlaygroundKit::AddXfBox(Decor, Xf, FVector(0.0, 0.0, Z + ParapetH + 0.5 * MerlonH), FVector(0.46 * ParapetT, 0.42 * Chord, 0.5 * MerlonH),
					TNBeachTrapKit::SandTop());
			}
		}
	}

	/** Rectángulo de una cara (U a lo largo, Z de alto) donde no hay pared libre: puerta, ventanas, estandartes. */
	struct FFaceKeepOut
	{
		double U0 = 0.0;
		double U1 = 0.0;
		double Z0 = 0.0;
		double Z1 = 0.0;
	};

	/**
	 * Pivote de una pieza de arte de la fortaleza (Docs/Arte_Assets.md) en At, girado YawDeg, ya con el espejo en Y si la
	 * fortaleza lo lleva (MirrorY): la malla de arte no se refleja, solo cambia de sitio y de giro.
	 */
	inline FTransform PiecePivot(bool bMirror, const FVector& At, double YawDeg, const FVector& Scale = FVector::OneVector)
	{
		return bMirror ? TNArt::PiecePivot(FVector(At.X, -At.Y, At.Z), -YawDeg, Scale) : TNArt::PiecePivot(At, YawDeg, Scale);
	}

	/** Giro (grados) de una dirección en planta. */
	inline double YawOf(const FVector& Dir)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	}

	/**
	 * Adornos de una cara vertical (Normal hacia fuera, U a lo largo): conchas y estrellas al azar entre Z0 y Z1. Los que
	 * tocarían un KeepOut (con su radio) se descartan: nada flota delante de un hueco. Con Log, cada uno es una pieza de arte.
	 */
	inline void AddFaceShells(FBuffers& Decor, const FVector& Origin, const FVector& Normal, const FVector& Along, double HalfLen, double Z0, double Z1, int32 Count,
		uint32 Seed, const TArray<FFaceKeepOut>& KeepOuts = {}, TNArt::FPieceLog* Log = nullptr, bool bMirror = false)
	{
		for (int32 d = 0; d < Count; ++d)
		{
			const double U = FMath::Lerp(-HalfLen + 60.0, HalfLen - 60.0, TNBeachTrapKit::Hash01(d, 1, Seed));
			const double Z = FMath::Lerp(Z0 + 60.0, Z1 - 60.0, TNBeachTrapKit::Hash01(d, 2, Seed));
			const FVector At = Origin + Along * U + FVector(0.0, 0.0, Z) + Normal * 2.0;
			const double Reach = d % 3 == 0 ? 70.0 : 60.0;
			bool bFloating = false;
			for (const FFaceKeepOut& K : KeepOuts)
			{
				if (U + Reach > K.U0 && U - Reach < K.U1 && Z + Reach > K.Z0 && Z - Reach < K.Z1)
				{
					bFloating = true;
					break;
				}
			}
			if (bFloating)
			{
				continue;
			}
			if (d % 3 == 0)
			{
				// Pivote: centro, +X hacia fuera de la pared; escala 1 = 50 de radio.
				const double Size = 50.0 + 20.0 * TNBeachTrapKit::Hash01(d, 3, Seed);
				TNArt::FPieceScope Piece(Log, TN_ART("Beach.SandCastle.Starfish"), PiecePivot(bMirror, At, YawOf(Normal), FVector(Size / 50.0)), { &Decor });
				TNPlaygroundKit::AddStarfish(Decor, At, Normal, FVector::UpVector, Size, 5.0, TNPlaygroundKit::Rgb(0xFF8A70));
			}
			else
			{
				const double Size = 38.0 + 22.0 * TNBeachTrapKit::Hash01(d, 4, Seed);
				TNArt::FPieceScope Piece(Log, TN_ART("Beach.SandCastle.Shell"), PiecePivot(bMirror, At, YawOf(Normal), FVector(Size / 50.0)), { &Decor });
				TNPlaygroundKit::AddShellFan(Decor, At, Normal, FVector::UpVector, Size, TNBeachTrapKit::ShellTone(d));
			}
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Fortaleza entera
	// ─────────────────────────────────────────────────────────────────────────

	/** Normal hacia fuera de cada lado. */
	inline FVector SideNormal(ESide Side)
	{
		switch (Side)
		{
		case ESide::PosX: return FVector::ForwardVector;
		case ESide::NegX: return -FVector::ForwardVector;
		case ESide::PosY: return FVector::RightVector;
		default:          return -FVector::RightVector;
		}
	}

	/**
	 * Construye la fortaleza sin espejo: Castle (arena, con colisión en Hulls) y Decor (almenas, banderas, conchas,
	 * ventanas, cubos y pala; sin colisión salvo lo que se pisa, que va en Hulls). Con Log, marca las piezas de arte (torres,
	 * torrecillas, banderas, estandartes, banderines, pala, conchas y estrellas) con su pivote ya reflejado si bMirror.
	 */
	inline void BuildFortress(FBuffers& Castle, FBuffers& Decor, FHulls& Hulls, const FPlan& P, uint32 Seed, TNArt::FPieceLog* Log = nullptr, bool bMirror = false)
	{
		const double A0 = P.A0;
		const double In = P.In;
		const double Z0 = P.Z0;
		const double K1 = P.Tiers[0].K;
		const double Z1 = P.Tiers[0].Z;

		// ── Zócalo (el suelo del patio) y murallas ──
		AddSand(Castle, &Hulls, -A0, A0, -A0, A0, -300.0, FloorZ, 0.0, 0.0);
		for (const double Sy : { -1.0, 1.0 })
		{
			AddSand(Castle, &Hulls, -A0, A0, Sy * In, Sy * A0, -200.0, Z0);
		}
		const double Gw = 0.5 * GateW;
		for (const double Sx : { -1.0, 1.0 })
		{
			AddSand(Castle, &Hulls, Sx * In, Sx * A0, -In, -Gw, -200.0, Z0);
			AddSand(Castle, &Hulls, Sx * In, Sx * A0, Gw, In, -200.0, Z0);
			AddSand(Castle, &Hulls, Sx * In, Sx * A0, -Gw, Gw, FloorZ + P.GateH, Z0, FloorZ + P.GateH);
			// Arco de la puerta por las dos caras, concha de clave y estandartes de Tortunavy por fuera.
			for (const double Face : { Sx * A0 + Sx * 3.0, Sx * In - Sx * 3.0 })
			{
				TNPlaygroundKit::AddAxisBox(Decor, FVector(Face, 0.0, FloorZ + P.GateH + 14.0), FVector(3.0, Gw + 24.0, 14.0), Trim());
				for (const double Jamb : { -Gw - 12.0, Gw + 12.0 })
				{
					TNPlaygroundKit::AddAxisBox(Decor, FVector(Face, Jamb, FloorZ + 0.5 * P.GateH), FVector(3.0, 12.0, 0.5 * P.GateH), Trim());
				}
			}
			const FVector OutN(Sx, 0.0, 0.0);
			TNPlaygroundKit::AddShellFan(Decor, FVector(Sx * (A0 + 3.0), 0.0, FloorZ + P.GateH + 40.0), OutN, FVector::UpVector, 70.0, TNBeachTrapKit::ShellTone(static_cast<int32>(Seed % 5u)));
			const double BannerH = FMath::Min(0.62 * Z0, 420.0);
			for (const double By : { -Gw - 130.0, Gw + 130.0 })
			{
				// Pivote: centro del borde de arriba, +X hacia fuera de la pared; escala 1 = 150 de ancho y 420 de alto.
				TNArt::FPieceScope BannerPiece(Log, TN_ART("Beach.Fortress.Banner"), PiecePivot(bMirror, FVector(Sx * (A0 + 2.0), By, Z0 - 40.0), YawOf(OutN),
					FVector(1.0, 1.0, BannerH / 420.0)), { &Decor });
				TNBeachBoostKit::AddBanner(Decor, FVector(Sx * (A0 + 2.0), By, Z0 - 40.0), OutN, 150.0, BannerH);
			}
		}
		// Caras de fuera: conchas y estrellas incrustadas.
		for (int32 f = 0; f < 4; ++f)
		{
			const ESide Side = static_cast<ESide>(f);
			const FVector N = SideNormal(Side);
			const FVector Along(-N.Y, N.X, 0.0);
			// En las caras ±X, la puerta con su arco, la concha de clave y los estandartes ocupan la franja central.
			TArray<FFaceKeepOut> KeepOuts;
			if (Side == ESide::PosX || Side == ESide::NegX)
			{
				KeepOuts.Add({ -(Gw + 130.0 + 75.0), Gw + 130.0 + 75.0, 0.0, Z0 });
			}
			AddFaceShells(Decor, N * A0, N, Along, A0 - P.TowerInset - P.TowerR, 0.0, Z0, 6 + P.Size * 4, Seed + 101u * static_cast<uint32>(f), KeepOuts, Log, bMirror);
		}

		// ── Pretil del adarve (por fuera), torres de las esquinas con su cubo y su bandera ──
		const double TowerSpan = A0 - P.TowerInset - P.TowerR + 20.0;
		AddParapet(Castle, Decor, Hulls, P.Gaps, -1, ESide::PosY, A0, -TowerSpan, TowerSpan, Z0);
		AddParapet(Castle, Decor, Hulls, P.Gaps, -1, ESide::NegY, -A0, -TowerSpan, TowerSpan, Z0);
		AddParapet(Castle, Decor, Hulls, P.Gaps, -1, ESide::PosX, A0, -TowerSpan, TowerSpan, Z0);
		AddParapet(Castle, Decor, Hulls, P.Gaps, -1, ESide::NegX, -A0, -TowerSpan, TowerSpan, Z0);
		const double Tr = P.TowerR;
		const double TopTr = Tr * 0.97;
		const double RingK = FMath::Clamp(P.TowerInset / TopTr, 0.0, 0.99);
		int32 TowerIndex = 0;
		for (const double Sx : { -1.0, 1.0 })
		{
			for (const double Sy : { -1.0, 1.0 })
			{
				const FVector2D C(Sx * (A0 - P.TowerInset), Sy * (A0 - P.TowerInset));
				// Torre entera con su pretil, su cubo y su bandera: centro de la base a ras de suelo, +X hacia fuera (en diagonal);
				// escala 1 = radio 290 y 500 hasta el adarve.
				TNArt::FPieceScope TowerPiece(Log, TN_ART("Beach.Fortress.Tower"), PiecePivot(bMirror, FVector(C.X, C.Y, 0.0), YawOf(FVector(Sx, Sy, 0.0)),
					FVector(Tr / 290.0, Tr / 290.0, Z0 / 500.0)), { &Castle, &Decor });
				TNPlaygroundKit::AddFrustum(Castle, FVector(C.X, C.Y, -200.0), FVector(C.X, C.Y, Z0), Tr, TopTr, 24, TNBeachTrapKit::SandSide(), TNBeachTrapKit::SandTop(), false, true);
				Hulls.Add(TNPlaygroundKit::HullCylinder(FVector(C.X, C.Y, -200.0), Z0 + 200.0, Tr, TopTr, 14));
				for (const double K : { 0.3, 0.55, 0.8 })
				{
					const double Rr = FMath::Lerp(Tr, TopTr, (K * Z0 + 200.0) / (Z0 + 200.0)) + 5.0;
					TNPlaygroundKit::AddFrustum(Decor, FVector(C.X, C.Y, K * Z0 - 8.0), FVector(C.X, C.Y, K * Z0 + 8.0), Rr, Rr, 24, TNBeachTrapKit::SandMark(),
						TNBeachTrapKit::SandMark(), false, false);
				}
				TNPlaygroundKit::AddFrustum(Decor, FVector(C.X, C.Y, Z0 - 34.0), FVector(C.X, C.Y, Z0 - 2.0), TopTr + 16.0, TopTr + 16.0, 24, TNBeachTrapKit::SandMark(),
					TNBeachTrapKit::SandMark(), true, false);
				// El anillo solo por la parte de la torre que sobresale del cuadrado de la muralla.
				const double Out = FMath::RadiansToDegrees(FMath::Atan2(Sy, Sx));
				const double A0Deg = Out - 45.0 - FMath::RadiansToDegrees(FMath::Acos(RingK));
				const double A1Deg = Out + 135.0 - FMath::RadiansToDegrees(FMath::Asin(RingK));
				AddTowerRing(Castle, Decor, Hulls, C, TopTr, Z0, A0Deg, A1Deg);
				const FVector OutDir(Sx, Sy, 0.0);
				const double BucketR = 0.42 * Tr;
				const double BucketTop = AddBucketTurret(Decor, Hulls, FVector(C.X, C.Y, Z0), BucketR, 0.9 * Tr, TNPlaygroundKit::ToyColor(static_cast<int32>(Seed % 7u) + TowerIndex, 0.3f),
					OutDir);
				{
					// Bandera (dentro de la torre: si la torre tiene sustituto, va con ella). Pie del mástil, +X hacia donde ondea;
					// escala 1 = mástil de 300.
					const FVector FlagDir = OutDir.RotateAngleAxis(60.0, FVector::UpVector);
					TNArt::FPieceScope FlagPiece(Log, TN_ART("Beach.Fortress.Flag"), PiecePivot(bMirror, FVector(C.X, C.Y, BucketTop), YawOf(FlagDir),
						FVector((0.8 * Tr + 60.0) / 300.0)), { &Decor });
					TNBeachBoostKit::AddNavyFlag(Decor, FVector(C.X, C.Y, BucketTop), 0.8 * Tr + 60.0, FlagDir, 0.55 * Tr, 0.36 * Tr, Seed + static_cast<uint32>(TowerIndex));
				}
				++TowerIndex;
			}
		}

		// ── Terrazas: macizas, con su pretil (con los huecos de las subidas), ventanas, conchas y banderas ──
		for (int32 t = 0; t < P.Tiers.Num(); ++t)
		{
			const FTier& Tier = P.Tiers[t];
			const double K = Tier.K;
			const double FaceFrom = t == 0 ? FloorZ : P.Tiers[t - 1].Z;
			AddSand(Castle, &Hulls, -K, K, -K, K, -200.0, Tier.Z, FaceFrom, 80.0);
			// Cornisa de adorno 4 cm por debajo del borde (sin caras a la altura del suelo de la terraza).
			TNPlaygroundKit::AddAxisBox(Decor, FVector(0.0, 0.0, Tier.Z - 14.0), FVector(K + 7.0, K + 7.0, 10.0), TNBeachTrapKit::SandMark());
			AddParapet(Castle, Decor, Hulls, P.Gaps, t, ESide::PosX, K, -K, K, Tier.Z);
			AddParapet(Castle, Decor, Hulls, P.Gaps, t, ESide::NegX, -K, -K, K, Tier.Z);
			AddParapet(Castle, Decor, Hulls, P.Gaps, t, ESide::PosY, K, -K + ParapetT, K - ParapetT, Tier.Z);
			AddParapet(Castle, Decor, Hulls, P.Gaps, t, ESide::NegY, -K, -K + ParapetT, K - ParapetT, Tier.Z);
			const double FaceH = Tier.Z - FaceFrom;
			const uint32 TierSeed = Seed + 977u * static_cast<uint32>(t + 1);
			// Ventanas en las caras ±X (la -X de la primera lleva la cornisa: allí, la puerta de adorno).
			const int32 Windows = FMath::Max(1, FMath::FloorToInt32(2.0 * K / 520.0));
			const double WinZ = FaceFrom + 0.55 * FaceH;
			for (const double Sx : { -1.0, 1.0 })
			{
				if (t == 0 && Sx < 0.0)
				{
					continue;
				}
				for (int32 w = 0; w < Windows; ++w)
				{
					const double Y = -K + (w + 0.5) * 2.0 * K / Windows;
					AddWindow(Decor, FVector(Sx * K, Y, WinZ), FVector(Sx, 0.0, 0.0), 90.0, FMath::Min(150.0, 0.3 * FaceH));
				}
			}
			AddFakeDoor(Decor, FVector(-K - 1.0, -(K - 180.0), FaceFrom), -FVector::ForwardVector, 240.0, FMath::Min(340.0, 0.55 * FaceH));
			// Estandartes en la cara +X (la del mar) y conchas por las cuatro caras.
			{
				TNArt::FPieceScope BannerPiece(Log, TN_ART("Beach.Fortress.Banner"), PiecePivot(bMirror, FVector(K + 1.0, 0.0, Tier.Z - 30.0), 0.0,
					FVector(1.0, FMath::Min(220.0, 0.3 * K) / 150.0, FMath::Min(0.7 * FaceH, 600.0) / 420.0)), { &Decor });
				TNBeachBoostKit::AddBanner(Decor, FVector(K + 1.0, 0.0, Tier.Z - 30.0), FVector::ForwardVector, FMath::Min(220.0, 0.3 * K), FMath::Min(0.7 * FaceH, 600.0));
			}
			for (int32 f = 0; f < 4; ++f)
			{
				const ESide Side = static_cast<ESide>(f);
				const FVector N = SideNormal(Side);
				const FVector Along(-N.Y, N.X, 0.0);
				// Ventanas, puerta de adorno y estandarte de la terraza: sus rectángulos (en U de esa cara) quedan libres.
				TArray<FFaceKeepOut> KeepOuts;
				if (Side == ESide::PosX || Side == ESide::NegX)
				{
					const double Sx = N.X;
					if (!(t == 0 && Sx < 0.0))
					{
						for (int32 w = 0; w < Windows; ++w)
						{
							const double Uw = Sx * (-K + (w + 0.5) * 2.0 * K / Windows);
							const double HalfWinH = FMath::Min(150.0, 0.3 * FaceH);
							KeepOuts.Add({ Uw - 90.0, Uw + 90.0, WinZ - HalfWinH, WinZ + HalfWinH });
						}
					}
					if (Sx < 0.0 && t == 0)
					{
						const double Ud = K - 180.0;
						KeepOuts.Add({ Ud - 140.0, Ud + 140.0, FaceFrom, FaceFrom + FMath::Min(340.0, 0.55 * FaceH) + 120.0 });
					}
					if (Sx > 0.0)
					{
						const double HalfBanner = 0.5 * FMath::Min(220.0, 0.3 * K);
						KeepOuts.Add({ -HalfBanner, HalfBanner, Tier.Z - FMath::Min(0.7 * FaceH, 600.0) - 30.0, Tier.Z });
					}
				}
				AddFaceShells(Decor, N * K, N, Along, K, FaceFrom, Tier.Z, 3 + P.Size * 2, TierSeed + 31u * static_cast<uint32>(f), KeepOuts, Log, bMirror);
			}
			// Banderines de colores en las esquinas de las terrazas de en medio; banderas de Tortunavy en las de la cima.
			const bool bSummit = t == P.Tiers.Num() - 1;
			for (int32 c = 0; c < 4; ++c)
			{
				const double Cx = (c & 1) ? K - 30.0 : -(K - 30.0);
				const double Cy = (c & 2) ? K - 30.0 : -(K - 30.0);
				const FVector Foot(Cx, Cy, Tier.Z);
				const FVector OutDir(Cx, Cy, 0.0);
				if (bSummit)
				{
					const double PoleH = FMath::Max(300.0, P.TopH - Tier.Z - 20.0);
					const FVector FlagDir = OutDir.RotateAngleAxis(50.0, FVector::UpVector);
					TNArt::FPieceScope FlagPiece(Log, TN_ART("Beach.Fortress.Flag"), PiecePivot(bMirror, Foot, YawOf(FlagDir), FVector(PoleH / 300.0)), { &Decor });
					TNBeachBoostKit::AddNavyFlag(Decor, Foot, PoleH, FlagDir, 0.36 * PoleH, 0.24 * PoleH, TierSeed + static_cast<uint32>(c));
				}
				else
				{
					// Palo y banderín: pie del palo, +X hacia donde ondea.
					TNArt::FPieceScope PennantPiece(Log, TN_ART("Beach.Fortress.Pennant"), PiecePivot(bMirror, Foot, YawOf(OutDir)), { &Decor });
					const FVector PoleTop = Foot + FVector(0.0, 0.0, 260.0);
					TNPlaygroundKit::AddRod(Decor, Foot, PoleTop, 5.0, 6, TNBeachBoostKit::PoleWood(), FVector::ForwardVector);
					TNPlaygroundKit::AddPennant(Decor, PoleTop, OutDir, 130.0, 80.0, TNPlaygroundKit::ToyColor(static_cast<int32>(TierSeed % 7u) + c, 0.2f));
				}
			}
		}

		// ── Subidas por fuera: rampa por la cara -Y y escalera por la +Y ──
		AddRamp(Castle, Hulls, FVector2D(P.ExtRampX0, -(A0 + 0.5 * ExtRampW)), FVector2D(P.ExtRampX1, -(A0 + 0.5 * ExtRampW)), 0.5 * ExtRampW, -5.0, Z0, -200.0);
		for (int32 k = 0; k < P.ExtSteps; ++k)
		{
			const double X0 = P.ExtStairX0 - (k + 1) * StepRun;
			const double X1 = P.ExtStairX0 - k * StepRun;
			AddSand(Castle, &Hulls, X0, X1, A0, A0 + ExtStairW, -100.0, (k + 1) * P.ExtRise, 0.0, 0.0);
		}

		// ── Subidas por dentro: escalera al adarve de +X y rampa del patio a la primera terraza ──
		for (int32 k = 0; k < P.InSteps; ++k)
		{
			const double Y0 = P.InStairY0 - (k + 1) * StepRun;
			const double Y1 = P.InStairY0 - k * StepRun;
			AddSand(Castle, &Hulls, In - InStairW, In, Y0, Y1, FloorZ - 10.0, FloorZ + (k + 1) * P.InRise, 0.0, 0.0);
		}
		AddRamp(Castle, Hulls, FVector2D(-In, 0.5 * (K1 + In)), FVector2D(K1, 0.5 * (K1 + In)), 0.5 * (In - K1), FloorZ, Z1, -100.0);

		// ── De terraza en terraza ──
		for (const FTerraceRamp& Ramp : P.TerraceRamps)
		{
			const FTier& Lower = P.Tiers[Ramp.Upper - 1];
			const FTier& Upper = P.Tiers[Ramp.Upper];
			const double Yc = (Ramp.bNegY ? -1.0 : 1.0) * (Upper.K + 0.5 * TerraceRampW);
			AddRamp(Castle, Hulls, FVector2D(Ramp.XLow, Yc), FVector2D(Ramp.XHigh, Yc), 0.5 * TerraceRampW, Lower.Z, Upper.Z, Lower.Z - 5.0);
		}

		// ── Atajo de la pala y la cornisa ──
		const double LedgeX0 = -(K1 + LedgeW);
		const double LedgeX1 = -K1;
		const double BladeLen = FMath::Min(P.T0 - ParapetT - 20.0, 240.0);
		{
			// Pala de juguete: donde la hoja se une al mango, a la altura de su cara de arriba; +X hacia el mango.
			TNArt::FPieceScope SpadePiece(Log, TN_ART("Beach.Fortress.Spade"), PiecePivot(bMirror, FVector(-In + 10.0, P.SpadeY, Z0), 0.0), { &Decor });
			AddSpadeBridge(Decor, Hulls, -In - BladeLen, -In + 10.0, LedgeX0 + 8.0, P.SpadeY, Z0, Seed >> 3);
		}
		// Rellano de la cornisa donde llega la pala, repisa que sube por la cara -X y rellano de arriba.
		AddSand(Castle, &Hulls, LedgeX0, LedgeX1, P.LedgeY0, P.LedgeY0 + SpadeW, Z0 - LedgeThick, Z0, 0.0, 0.0);
		AddLedge(Castle, Decor, Hulls, LedgeX0, LedgeX1, P.LedgeY0, Z0, P.LedgeY1, Z1, LedgeThick);
		AddSand(Castle, &Hulls, LedgeX0, LedgeX1, P.LedgeY1 - LedgeW, P.LedgeY1, Z1 - LedgeThick, Z1, 0.0, 0.0);

		// ── Torrecillas de cubo ──
		// Pieza de arte de cada una: centro de la base, ejes de la fortaleza; escala 1 = radio 100 y 200 de alto.
		auto PillarPivot = [bMirror](const FVector& Top, double BaseZ, double Radius)
		{
			return PiecePivot(bMirror, FVector(Top.X, Top.Y, BaseZ), 0.0, FVector(Radius / 100.0, Radius / 100.0, FMath::Max(1.0, Top.Z - BaseZ) / 200.0));
		};
		for (int32 i = 0; i < P.ChainA.Tops.Num(); ++i)
		{
			TNArt::FPieceScope PillarPiece(Log, TN_ART("Beach.Fortress.Pillar"), PillarPivot(P.ChainA.Tops[i], P.ChainA.BaseZ, P.ChainA.Radius), { &Castle, &Decor });
			AddPillar(Castle, Decor, Hulls, P.ChainA.Tops[i], P.ChainA.BaseZ, P.ChainA.Radius, Seed, i);
		}
		for (int32 i = 0; i < P.ChainB.Tops.Num(); ++i)
		{
			TNArt::FPieceScope PillarPiece(Log, TN_ART("Beach.Fortress.Pillar"), PillarPivot(P.ChainB.Tops[i], P.ChainB.BaseZ, P.ChainB.Radius), { &Castle, &Decor });
			AddPillar(Castle, Decor, Hulls, P.ChainB.Tops[i], P.ChainB.BaseZ, P.ChainB.Radius, Seed + 17u, i);
		}

		// ── Patio: guijarros, charquitos y una estrella grande en el suelo ──
		for (int32 p = 0; p < 10 + 6 * P.Size; ++p)
		{
			const double Px = FMath::Lerp(-In + 60.0, In - 60.0, TNBeachTrapKit::Hash01(p, 5, Seed));
			const double Py = FMath::Lerp(-In + 60.0, -K1 - 60.0, TNBeachTrapKit::Hash01(p, 6, Seed));
			TNBeachTrapKit::AddPebble(Decor, FVector(Px, Py, FloorZ + 2.0), FMath::Lerp(10.0, 24.0, TNBeachTrapKit::Hash01(p, 7, Seed)), Seed + static_cast<uint32>(p),
				TNBeachTrapKit::RockTone(p));
		}
		// La franja +X al lado +Y está libre (la escalera va al lado -Y y la rampa acaba en la esquina de la terraza).
		TNPlaygroundKit::AddStarfish(Decor, FVector(0.5 * (K1 + In), 0.5 * K1, FloorZ + 1.0), FVector::UpVector, FVector::ForwardVector, 90.0, 8.0,
			TNPlaygroundKit::Rgb(0xFF8A70));
		for (const double Sx : { -1.0, 1.0 })
		{
			TNPlaygroundKit::AddDisc(Decor, FVector(Sx * (0.5 * (K1 + In)), Sx * 0.6 * In, FloorZ + 1.0), FVector::UpVector, 0.3 * (In - K1), 14, TNBeachTrapKit::SandWet());
		}
	}

	/** Refleja en Y los buffers (vértices, normales y el sentido de cada triángulo). */
	inline void MirrorY(FBuffers& B)
	{
		for (FVector& V : B.Verts)
		{
			V.Y = -V.Y;
		}
		for (FVector& N : B.Normals)
		{
			N.Y = -N.Y;
		}
		for (int32 i = 0; i + 2 < B.Tris.Num(); i += 3)
		{
			Swap(B.Tris[i + 1], B.Tris[i + 2]);
		}
	}

	inline void MirrorY(FHulls& Hulls)
	{
		for (TArray<FVector>& Hull : Hulls)
		{
			for (FVector& Pt : Hull)
			{
				Pt.Y = -Pt.Y;
			}
		}
	}

	/** Tinte suave de la arena según la semilla (más dorada, más blanca o más rosada). */
	inline void TintSand(FBuffers& B, uint32 Seed)
	{
		const float R = 1.f + 0.05f * static_cast<float>(TNBeachTrapKit::Hash01(1, 11, Seed) - 0.5);
		const float G = 1.f + 0.03f * static_cast<float>(TNBeachTrapKit::Hash01(2, 11, Seed) - 0.5);
		const float Bl = 1.f - 0.06f * static_cast<float>(TNBeachTrapKit::Hash01(3, 11, Seed));
		for (FLinearColor& Col : B.Colors)
		{
			Col.R *= R;
			Col.G *= G;
			Col.B *= Bl;
		}
	}
}
