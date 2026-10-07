// Lógica pura de la playa de la antigua carrera (TNBeachLayout): terreno fijo (relieve, corredores, crestas, pozas y
// trincheras), salida, sprint, meta, zambullida y reparto por ronda con su dificultad. Sin mundo ni actores.
//
// Umbrales de cantidad: los que se afinaron con 1200 m de recorrido por TNBeachLayout::LengthScale (2/3 con 800 m), que es
// lo que escala el reparto por ronda; los de calidad (paso libre, sin solapes, sin líneas rectas largas, ocupación por
// metro cuadrado, terreno) no cambian. Medidos con esta misma prueba (24 rondas en Normal, 8 en Fácil y en Difícil).
// Correr desde Session Frontend (categoría "Tortunabo.Beach") o sin ventana:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Beach; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "UObject/Class.h"
#include "World/Beach/TN_BeachLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBeachLayoutTest
{
	bool SameItem(const TNBeachLayout::FItem& A, const TNBeachLayout::FItem& B)
	{
		return A.Element == B.Element && A.Pos.Equals(B.Pos, 0.001) && FMath::IsNearlyEqual(A.Yaw, B.Yaw, 1e-6)
			&& FMath::IsNearlyEqual(A.Radius, B.Radius, 1e-6) && FMath::IsNearlyEqual(A.Core, B.Core, 1e-6) && FMath::IsNearlyEqual(A.HalfLength, B.HalfLength, 1e-6)
			&& A.Spec.Seed == B.Spec.Seed && A.Spec.SizeScale == B.Spec.SizeScale && A.Spec.Extent == B.Spec.Extent && A.Role == B.Role;
	}

	bool SameLayout(const TNBeachLayout::FRoundLayout& A, const TNBeachLayout::FRoundLayout& B)
	{
		if (A.Difficulty != B.Difficulty || A.Items.Num() != B.Items.Num() || A.Stamps.Num() != B.Stamps.Num() || A.Interest.Num() != B.Interest.Num()) { return false; }
		for (int32 i = 0; i < A.Items.Num(); ++i)
		{
			if (!SameItem(A.Items[i], B.Items[i])) { return false; }
		}
		for (int32 i = 0; i < A.Stamps.Num(); ++i)
		{
			const TNBeachLayout::FStamp& SA = A.Stamps[i];
			const TNBeachLayout::FStamp& SB = B.Stamps[i];
			if (!SA.A.Equals(SB.A, 0.001) || !SA.B.Equals(SB.B, 0.001) || !FMath::IsNearlyEqual(SA.LevelZ, SB.LevelZ, 1e-6) || !FMath::IsNearlyEqual(SA.Radius, SB.Radius, 1e-6))
			{
				return false;
			}
		}
		for (int32 i = 0; i < A.Interest.Num(); ++i)
		{
			if (A.Interest[i].Kind != B.Interest[i].Kind || !A.Interest[i].Pos.Equals(B.Interest[i].Pos, 0.001) || !A.Interest[i].To.Equals(B.Interest[i].To, 0.001))
			{
				return false;
			}
		}
		return true;
	}

	/** La línea de P0 a P1 pasa por encima del suelo (con Margin) en todo el tramo de playa que cruza. */
	bool LineClearsGround(const FVector& P0, const FVector& P1, double Margin)
	{
		const FVector D = P1 - P0;
		const int32 Steps = FMath::CeilToInt32(FMath::Abs(D.X) / 250.0);
		for (int32 s = 1; s < Steps; ++s)
		{
			const FVector P = P0 + D * (static_cast<double>(s) / Steps);
			if (P.X < P0.X + 500.0 || P.X > TNBeachLayout::EdgeX(P.Y)) { continue; }
			if (P.Z < TNBeachLayout::GroundZ(P.X, P.Y) + Margin) { return false; }
		}
		return true;
	}

	double SlopeDegAt(double X, double Y)
	{
		constexpr double H = 50.0;
		const double Dx = (TNBeachLayout::GroundZ(X + H, Y) - TNBeachLayout::GroundZ(X - H, Y)) / (2.0 * H);
		const double Dy = (TNBeachLayout::GroundZ(X, Y + H) - TNBeachLayout::GroundZ(X, Y - H)) / (2.0 * H);
		return FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(Dx * Dx + Dy * Dy)));
	}

	/**
	 * Tramos rectos libres hacia el mar (como PlugStraightLines): cada 4 m a lo ancho, el tramo más largo sin nada de lo que
	 * ocupa cada elemento (ni pasos de quads), pozas, trincheras ni crestas. Devuelve cuántas filas pasan de MaxRun.
	 */
	int32 CountLongStraightRuns(const TNBeachLayout::FRoundLayout& L, double MaxRun, double& OutLongest)
	{
		using namespace TNBeachLayout;
		constexpr double Cell = 200.0;
		const double Usable = SideReach - 100.0;
		const int32 NXc = FMath::CeilToInt32((ItemsEndX - ItemsStartX) / Cell);
		const int32 NYc = FMath::CeilToInt32(2.0 * Usable / Cell);
		TArray<uint8> Occ;
		Occ.Init(0, NXc * NYc);
		auto Mark = [&Occ, NXc, NYc, Usable](const FVector2D& A, const FVector2D& B, double R)
		{
			const int32 IX0 = FMath::Max(0, FMath::FloorToInt32((FMath::Min(A.X, B.X) - R - ItemsStartX) / Cell));
			const int32 IX1 = FMath::Min(NXc - 1, FMath::CeilToInt32((FMath::Max(A.X, B.X) + R - ItemsStartX) / Cell));
			const int32 IY0 = FMath::Max(0, FMath::FloorToInt32((FMath::Min(A.Y, B.Y) - R + Usable) / Cell));
			const int32 IY1 = FMath::Min(NYc - 1, FMath::CeilToInt32((FMath::Max(A.Y, B.Y) + R + Usable) / Cell));
			for (int32 IY = IY0; IY <= IY1; ++IY)
			{
				for (int32 IX = IX0; IX <= IX1; ++IX)
				{
					double T = 0.0;
					if (TNProcMap::DistPointSegment(FVector2D(ItemsStartX + (IX + 0.5) * Cell, -Usable + (IY + 0.5) * Cell), A, B, T) <= R) { Occ[IY * NXc + IX] = 1; }
				}
			}
		};
		for (const FItem& Item : L.Items)
		{
			if (!Item.bOverlay) { Mark(Item.EndA(), Item.EndB(), Item.Core); }
		}
		MarkTerrainObstacles(Mark);
		int32 Long = 0;
		OutLongest = 0.0;
		for (int32 IY = 1; IY < NYc; IY += 2)
		{
			int32 Run = 0;
			int32 Best = 0;
			for (int32 IX = 0; IX < NXc; ++IX)
			{
				Run = Occ[IY * NXc + IX] ? 0 : Run + 1;
				Best = FMath::Max(Best, Run);
			}
			OutLongest = FMath::Max(OutLongest, Best * Cell);
			if (Best * Cell > MaxRun) { ++Long; }
		}
		return Long;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Terreno fijo: perfil, salida, meta, zambullida y asientos
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachTerrainTest,
	"Tortunabo.Beach.Terrain",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachTerrainTest::RunTest(const FString& Parameters)
{
	using TNBeachLayout::GroundZ;

	// Perfil: baja hacia el mar y acaba en el borde a la altura del acantilado del contrato.
	TestEqual(TEXT("el borde queda CliffHeight sobre el agua"), TNBeachLayout::CliffTopZ - TNBeachLayout::WaterZ, TNBeach::CliffHeight);
	TestTrue(TEXT("la arena baja hacia el mar"), TNBeachLayout::ProfileZ(0.0) > TNBeachLayout::ProfileZ(60000.0)
		&& TNBeachLayout::ProfileZ(60000.0) > TNBeachLayout::ProfileZ(TNBeachLayout::Length));
	TestTrue(TEXT("la salida, 24 m (el 3 % del recorrido) por encima del borde"), FMath::IsNearlyEqual(TNBeachLayout::ProfileZ(0.0) - TNBeachLayout::CliffTopZ, TNBeachLayout::BeachDrop, 1.0)
		&& FMath::IsNearlyEqual(TNBeachLayout::BeachDrop, 0.03 * TNBeach::CourseLength, 1.0));
	for (double Y = -12000.0; Y <= 12000.0; Y += 3000.0)
	{
		const double Edge = TNBeachLayout::EdgeX(Y);
		TestTrue(FString::Printf(TEXT("borde a 800 m ± 2,5 m (Y %.0f)"), Y), FMath::Abs(Edge - TNBeachLayout::Length) <= TNBeachLayout::EdgeWobble + 1.0);
		const double Lip = GroundZ(Edge - 10.0, Y);
		TestTrue(FString::Printf(TEXT("la repisa de roca, a ~15,5 m del agua (Y %.0f)"), Y),
			Lip > TNBeachLayout::CliffTopZ && Lip < TNBeachLayout::CliffTopZ + TNBeachLayout::RockRise + 60.0);
		TestTrue(FString::Printf(TEXT("11 m de agua al pie (Y %.0f)"), Y), TNBeachLayout::SeabedZ(Edge + 300.0, Y) < TNBeachLayout::WaterZ - 900.0);
	}

	// Salida: cuatro huevos distintos, en llano, detrás de todo el reparto; desde cada uno se ve el mar por encima del borde
	// y las banderas de meta que flotan.
	for (int32 i = 0; i < TNBeachLayout::NumStartSpots; ++i)
	{
		const FVector Spot = TNBeachLayout::StartSpot(i);
		TestTrue(FString::Printf(TEXT("salida %d por detrás del reparto (15 m libres)"), i), Spot.X <= TNBeachLayout::ItemsStartX - 1500.0);
		TestTrue(FString::Printf(TEXT("salida %d delante del muro de detrás"), i), Spot.X > TNBeachLayout::BackWallX + 500.0);
		TestTrue(FString::Printf(TEXT("salida %d casi en llano (la cuesta suave de la playa)"), i), TNBeachLayoutTest::SlopeDegAt(Spot.X, Spot.Y) < 4.0);
		TestTrue(FString::Printf(TEXT("salida %d, en la zona que lanzan los huevos"), i), Spot.X > TNBeachLayout::BackWallX && Spot.X < TNBeachLayout::EggLaunchReachX);
		for (int32 j = 0; j < i; ++j)
		{
			TestTrue(FString::Printf(TEXT("salidas %d y %d separadas"), i, j), FVector::Dist2D(Spot, TNBeachLayout::StartSpot(j)) >= 900.0);
		}
		const FVector Eye = Spot + FVector(0.0, 0.0, 400.0);
		TestTrue(FString::Printf(TEXT("desde la salida %d se ve el mar por encima del borde"), i),
			TNBeachLayoutTest::LineClearsGround(Eye, FVector(TNBeachLayout::Length + 200000.0, Spot.Y, TNBeachLayout::WaterZ), 50.0));
		TestTrue(FString::Printf(TEXT("desde la salida %d se ven las banderas de meta"), i),
			TNBeachLayoutTest::LineClearsGround(Eye, FVector(TNBeachLayout::Length + 2600.0, Spot.Y, TNBeachLayout::WaterZ + 2800.0), 50.0));
	}

	// Sprint de desempate: a mitad del recorrido, todos los sitios en arena seca y casi llana.
	const double SprintX = TNBeachLayout::SprintLineX();
	TestTrue(FString::Printf(TEXT("sprint a mitad del recorrido (%.0f m)"), SprintX / 100.0), FMath::Abs(SprintX - 0.5 * TNBeachLayout::Length) <= 3000.0);
	for (int32 i = 0; i < TNBeachLayout::NumStartSpots * 3; ++i)
	{
		const FVector Spot = TNBeachLayout::SprintSpot(i);
		TestTrue(FString::Printf(TEXT("sitio %d del sprint en arena seca y llana"), i), TNBeachLayout::IsDryFlatSpot(FVector2D(Spot.X, Spot.Y)));
	}

	// Meta: el agua más allá del filo; la repisa no.
	const double Edge0 = TNBeachLayout::EdgeX(0.0);
	TestTrue(TEXT("agua de meta al pie del acantilado"), TNBeachLayout::IsFinishWaterLocal(FVector(Edge0 + 800.0, 0.0, -20.0)));
	TestFalse(TEXT("la repisa no es meta"), TNBeachLayout::IsFinishWaterLocal(FVector(Edge0 - 300.0, 0.0, GroundZ(Edge0 - 300.0, 0.0))));
	TestFalse(TEXT("cayendo aún no es meta"), TNBeachLayout::IsFinishWaterLocal(FVector(Edge0 + 800.0, 0.0, 600.0)));

	// Zambullida: los últimos 7,5 m de la repisa y el vacío sobre el agua.
	TestTrue(TEXT("zambullida al borde"), TNBeachLayout::IsCliffJumpZoneLocal(FVector(Edge0 - 500.0, 0.0, GroundZ(Edge0 - 500.0, 0.0) + 70.0)));
	TestTrue(TEXT("zambullida sobre el vacío"), TNBeachLayout::IsCliffJumpZoneLocal(FVector(Edge0 + 1000.0, 0.0, 800.0)));
	TestFalse(TEXT("sin zambullida lejos del borde"), TNBeachLayout::IsCliffJumpZoneLocal(FVector(Edge0 - 2000.0, 0.0, GroundZ(Edge0 - 2000.0, 0.0) + 70.0)));
	TestFalse(TEXT("sin zambullida dentro del agua"), TNBeachLayout::IsCliffJumpZoneLocal(FVector(Edge0 + 1000.0, 0.0, -100.0)));

	// Asientos: el suelo bajo la huella queda liso y a nivel a la cota natural del centro, fuera del borde vuelve a la arena
	// natural y el borde se sube andando; el paso de quads no cambia la altura (solo oscurece).
	struct FStampCase
	{
		ETNBeachElement Element;
		FVector2D Pos;
		double HalfLength;
	};
	const FStampCase Cases[] = {
		{ ETNBeachElement::SandCastleHuge, FVector2D(30000.0, 0.0), 0.0 },
		{ ETNBeachElement::SandCastleHuge, FVector2D(60000.0, 9500.0), 0.0 },
		{ ETNBeachElement::RockCluster, FVector2D(20000.0, 12000.0), 0.0 },
		{ ETNBeachElement::Coconut, FVector2D(45000.0, -13000.0), 0.0 },
		{ ETNBeachElement::Boardwalk, FVector2D(26000.0, 3000.0), 3000.0 },
		{ ETNBeachElement::QuadLane, FVector2D(40000.0, 0.0), TNBeachLayout::HalfWidth },
	};
	for (const FStampCase& Case : Cases)
	{
		TNBeachLayout::FItem Item;
		Item.Element = Case.Element;
		Item.Pos = Case.Pos;
		Item.Yaw = Case.Element == ETNBeachElement::QuadLane ? 90.0 : 0.0;
		Item.Radius = TNBeach::FootprintRadius(Case.Element);
		Item.Core = Item.Radius;
		Item.HalfLength = Case.HalfLength;
		TArray<TNBeachLayout::FStamp> Stamps;
		Stamps.Add(TNBeachLayout::MakeStamp(Item));
		const TNBeachLayout::FStamp& Stamp = Stamps[0];
		auto Seated = [&Stamps](const FVector2D& P) { return TNBeachLayout::StampedZ(Stamps, P.X, P.Y, TNBeachLayout::GroundZ(P.X, P.Y)); };
		const FString Name = UEnum::GetValueAsString(Case.Element);
		TestTrue(Name + TEXT(": el centro, a la cota de su asiento (donde va su origen)"),
			FMath::IsNearlyEqual(Seated(Case.Pos), TNBeachLayout::PlacementZ(Item), 0.01));
		if (Stamp.bTintOnly)
		{
			bool bUnchanged = true;
			float Tint = 0.f;
			for (double Y = -12000.0; Y <= 12000.0; Y += 1500.0)
			{
				const FVector2D P(Case.Pos.X + 300.0, Y);
				bUnchanged &= FMath::IsNearlyEqual(Seated(P), TNBeachLayout::GroundZ(P.X, P.Y), 0.01);
			}
			TNBeachLayout::StampedZ(Stamps, Case.Pos.X, 0.0, 0.0, &Tint);
			TestTrue(Name + TEXT(": no cambia el suelo"), bUnchanged);
			TestTrue(Name + TEXT(": oscurece sus rodadas"), Tint > 0.f);
			continue;
		}
		double MaxInside = 0.0;
		double MaxBorder = 0.0;
		for (int32 a = 0; a < 16; ++a)
		{
			const double Ang = TNProcMap::TwoPi * a / 16.0;
			const FVector2D Dir(FMath::Cos(Ang), FMath::Sin(Ang));
			const FVector2D Base = Item.EndA() + (Item.EndB() - Item.EndA()) * (a % 3 == 0 ? 0.0 : (a % 3 == 1 ? 0.5 : 1.0));
			// Dentro: a nivel.
			const double Z0 = Seated(Base);
			const double Z1 = Seated(Base + Dir * (Item.Radius * 0.9));
			MaxInside = FMath::Max(MaxInside, FMath::Abs(Z1 - Z0) / FMath::Max(1.0, Item.Radius * 0.9));
			// Borde: de la huella a la arena natural.
			for (double R = Item.Radius; R < Item.Radius + Stamp.Blend; R += 20.0)
			{
				const double Za = Seated(Base + Dir * R);
				const double Zb = Seated(Base + Dir * (R + 20.0));
				MaxBorder = FMath::Max(MaxBorder, FMath::RadiansToDegrees(FMath::Atan(FMath::Abs(Zb - Za) / 20.0)));
			}
			const FVector2D Outside = Base + Dir * (Item.Radius + Stamp.Blend + 10.0);
			double T = 0.0;
			if (TNProcMap::DistPointSegment(Outside, Item.EndA(), Item.EndB(), T) >= Item.Radius + Stamp.Blend)
			{
				TestTrue(Name + TEXT(": fuera del borde, la arena natural"), FMath::IsNearlyEqual(Seated(Outside), TNBeachLayout::GroundZ(Outside.X, Outside.Y), 0.01));
			}
		}
		TestTrue(FString::Printf(TEXT("%s: liso por dentro (pendiente %.3f)"), *Name, MaxInside), MaxInside < 0.001);
		// Solo los asientos que el reparto admitiría (sin paredes): sobre una cresta, el reparto no lo pondría ahí.
		if (TNBeachLayout::SeatIsGentle(Item))
		{
			TestTrue(FString::Printf(TEXT("%s: el borde se sube andando (%.1f°)"), *Name, MaxBorder), MaxBorder < 44.0);
		}
		else
		{
			AddInfo(FString::Printf(TEXT("%s en (%.0f, %.0f) m: el reparto no lo asentaría ahí (borde de %.1f°)."), *Name, Case.Pos.X / 100.0, Case.Pos.Y / 100.0, MaxBorder));
		}
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Terreno fijo: relieve, corredores, crestas, pozas y trincheras
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachReliefTest,
	"Tortunabo.Beach.Relief",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachReliefTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachLayout;

	// Irregular pero andable: nada por encima de ~42° (la tortuga sube hasta ~44°), bastante relieve y cuestas variadas.
	double MaxSlope = 0.0;
	double Sum = 0.0;
	double SumSq = 0.0;
	int32 Samples = 0;
	int32 Steep = 0;
	for (double X = 3000.0; X < Length - RockStart; X += 700.0)
	{
		for (double Y = -HalfWidth + 500.0; Y <= HalfWidth - 500.0; Y += 700.0)
		{
			const double Slope = TNBeachLayoutTest::SlopeDegAt(X, Y);
			MaxSlope = FMath::Max(MaxSlope, Slope);
			Steep += Slope > 10.0 ? 1 : 0;
			const double Relief = SandZ(X, Y) - BaseZ(X, Y);
			Sum += Relief;
			SumSq += Relief * Relief;
			++Samples;
		}
	}
	const double Mean = Sum / Samples;
	const double Deviation = FMath::Sqrt(FMath::Max(0.0, SumSq / Samples - Mean * Mean));
	TestTrue(FString::Printf(TEXT("se sube andando (pendiente máxima %.1f° < 42°)"), MaxSlope), MaxSlope < 42.0);
	TestTrue(FString::Printf(TEXT("relieve irregular (desviación %.0f cm > 90)"), Deviation), Deviation > 90.0);
	TestTrue(FString::Printf(TEXT("cuestas variadas (%.0f %% por encima de 10°)"), 100.0 * Steep / Samples), Steep > Samples / 10);

	// Corredores: más bajos que las dunas de al lado donde van separados.
	double Lower = 0.0;
	int32 CorridorSamples = 0;
	for (double X = 12000.0; X < Length - 12000.0; X += 2000.0)
	{
		for (int32 K = 0; K < NumCorridors; ++K)
		{
			const FCorridorSample C = CorridorAt(K, X);
			if (C.Weight < 0.9 || (K < 2 && CorridorSplit(X) < 0.8)) { continue; }
			const double Floor = ReliefZ(X, C.Y);
			const double Sides = 0.5 * (ReliefZ(X, C.Y - 2.2 * C.HalfW) + ReliefZ(X, C.Y + 2.2 * C.HalfW));
			Lower += Sides - Floor;
			++CorridorSamples;
		}
	}
	// 66 muestras con 1200 m y 43 con 800 m (se toma una cada 20 m de recorrido): el umbral de antes (40) por LengthScale.
	TestTrue(FString::Printf(TEXT("hay corredores separados (%d muestras)"), CorridorSamples), CorridorSamples > static_cast<int32>(40.0 * LengthScale));
	TestTrue(FString::Printf(TEXT("los corredores van más bajos que las dunas de al lado (%.0f cm de media)"), Lower / FMath::Max(1, CorridorSamples)),
		CorridorSamples > 0 && Lower / CorridorSamples > 60.0);

	// Crestas: altas, con su cara empinada hacia la salida en las que cruzan, dentro de la playa; alguna con cornisa.
	int32 Lipped = 0;
	for (int32 r = 0; r < Ridges().Num(); ++r)
	{
		const FRidge& Ridge = Ridges()[r];
		const FString Ctx = FString::Printf(TEXT("cresta %d"), r);
		TestTrue(Ctx + TEXT(": de 1,5 m o más"), Ridge.Height >= 150.0);
		TestTrue(Ctx + TEXT(": dentro de la playa"), FMath::Abs(Ridge.Center.Y) + Ridge.HalfLength * FMath::Abs(Ridge.Along.Y) <= HalfWidth);
		TestTrue(Ctx + TEXT(": después de la salida y antes de la roca"), Ridge.Center.X > 12000.0 && Ridge.Center.X < Length - 9000.0);
		if (!Ridge.bDivider) { TestTrue(Ctx + TEXT(": la cara empinada mira a la salida"), Ridge.Windward.X > 0.8); }
		Lipped += Ridge.bLip ? 1 : 0;
		// La cara de sotavento, a ~30° (la sola duna, sin el resto del relieve).
		const double H = RidgeCrestHeight(Ridge, 0.0);
		const FVector2D Crest = RidgeCrestPoint(Ridge, 0.0);
		const FVector2D SlipMid = Crest - Ridge.Windward * (0.5 * RidgeSlipWidth(H));
		const double Rise = RidgeZ(Ridge, SlipMid + Ridge.Windward * 25.0) - RidgeZ(Ridge, SlipMid - Ridge.Windward * 25.0);
		TestTrue(FString::Printf(TEXT("%s: sotavento a menos de 35° (%.1f°)"), *Ctx, FMath::RadiansToDegrees(FMath::Atan(Rise / 50.0))), Rise / 50.0 < FMath::Tan(FMath::DegreesToRadians(35.0)));
	}
	TestTrue(TEXT("hay crestas con cornisa"), Lipped >= 3 && LipSamples().Num() > 20);

	// Pozas: agua por debajo de toda su orilla, honda en medio (se nada), orillas que se suben andando y agua nadable.
	for (int32 p = 0; p < Pools().Num(); ++p)
	{
		const FPool& Pool = Pools()[p];
		const FString Ctx = FString::Printf(TEXT("poza %d"), p);
		double LowestRim = TNumericLimits<double>::Max();
		for (int32 k = 0; k < 32; ++k)
		{
			const FVector2D Q = PoolPoint(Pool, TNProcMap::TwoPi * k / 32.0, 1.05);
			LowestRim = FMath::Min(LowestRim, SandZ(Q.X, Q.Y));
		}
		TestTrue(FString::Printf(TEXT("%s: el agua no rebosa (orilla %.0f cm por encima)"), *Ctx, LowestRim - Pool.Water), LowestRim >= Pool.Water);
		TestTrue(FString::Printf(TEXT("%s: honda en medio (%.0f cm)"), *Ctx, Pool.Water - SandZ(Pool.Center.X, Pool.Center.Y)), Pool.Water - SandZ(Pool.Center.X, Pool.Center.Y) >= 130.0);
		TestTrue(Ctx + TEXT(": entre la salida y la roca"), Pool.Center.X - Pool.OuterR() > ItemsStartX && Pool.Center.X + Pool.OuterR() < Length - RockStart);
		double MaxBank = 0.0;
		for (int32 k = 0; k < 12; ++k)
		{
			const double Theta = TNProcMap::TwoPi * k / 12.0;
			for (double U = 0.3; U < 1.6; U += 0.05)
			{
				const FVector2D A = PoolPoint(Pool, Theta, U);
				const FVector2D B = PoolPoint(Pool, Theta, U + 0.05);
				const double Run = FMath::Max(1.0, FVector2D::Distance(A, B));
				MaxBank = FMath::Max(MaxBank, FMath::RadiansToDegrees(FMath::Atan(FMath::Abs(SandZ(B.X, B.Y) - SandZ(A.X, A.Y)) / Run)));
			}
		}
		TestTrue(FString::Printf(TEXT("%s: se sale andando (orilla a %.1f°)"), *Ctx, MaxBank), MaxBank < 40.0);
		TArray<FBox> Boxes;
		PoolSwimBoxes(Pool, Boxes);
		bool bBoxesOk = Boxes.Num() > 0;
		for (const FBox& Box : Boxes) { bBoxesOk &= FMath::IsNearlyEqual(Box.Max.Z, Pool.Water, 0.01) && Box.Min.Z < Pool.Water - 300.0; }
		TestTrue(Ctx + TEXT(": agua nadable hasta la superficie"), bBoxesOk);
		TestTrue(Ctx + TEXT(": es agua (PoolAt)"), PoolAt(Pool.Center, 1.0) == p);
	}

	// Trincheras: el canal cavado en su eje, entero junto a él y la arena natural lejos de todos los canales. Se mira una
	// rejilla alrededor de cada línea y no un punto fijo por delante: las dos líneas están a ~30 m y ese punto de la de
	// detrás caía a 5 m de la de delante.
	for (const FTrench& Trench : Trenches())
	{
		TestTrue(TEXT("trinchera de un lado a otro de buena parte de la playa"), Trench.Max.Y - Trench.Min.Y > 15000.0 && Trench.Points.Num() > 8);
		const FVector2D Mid = 0.5 * (Trench.Points[4] + Trench.Points[5]);
		TestTrue(TEXT("canal cavado en el eje"), FMath::IsNearlyEqual(NaturalZ(Mid.X, Mid.Y) - SandZ(Mid.X, Mid.Y), TrenchDig, 1.0));
		int32 FarSamples = 0;
		bool bFarNatural = true;
		bool bNearDug = true;
		for (double X = Trench.Min.X - 4000.0; X <= Trench.Max.X + 4000.0; X += 250.0)
		{
			for (double Y = Trench.Min.Y; Y <= Trench.Max.Y; Y += 1000.0)
			{
				const double Dist = TrenchDistance(FVector2D(X, Y));
				const double Carve = TrenchCarve(X, Y);
				if (Dist >= TrenchDigReach)
				{
					++FarSamples;
					bFarNatural &= Carve == 0.0;
				}
				else if (Dist <= TrenchDigFlat)
				{
					bNearDug &= FMath::IsNearlyEqual(Carve, TrenchDig, 0.01);
				}
			}
		}
		TestTrue(FString::Printf(TEXT("lejos de los canales, la arena natural (%d puntos)"), FarSamples), FarSamples > 100 && bFarNatural);
		TestTrue(TEXT("junto al eje, el canal entero"), bNearDug);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Reparto: ayudas de las pruebas
// ─────────────────────────────────────────────────────────────────────────────

namespace TNBeachLayoutTest
{
	const TCHAR* DifficultyName(ETNProcDifficulty Difficulty)
	{
		switch (Difficulty)
		{
			case ETNProcDifficulty::Easy: return TEXT("fácil");
			case ETNProcDifficulty::Hard: return TEXT("difícil");
			default: return TEXT("normal");
		}
	}

	/**
	 * Lo que un lanzador delante de un obstáculo (rol Launcher) puede pisar con su arco porque es lo que salta: castillos
	 * enormes, piezas de las filas y plataformas.
	 */
	bool IsJumpTarget(const TNBeachLayout::FItem& Item)
	{
		using TNBeachLayout::EItemRole;
		return Item.Role == EItemRole::Row || Item.Role == EItemRole::Castle || Item.Element == ETNBeachElement::SandCastleHuge
			|| Item.Element == ETNBeachElement::WobblyPlatform;
	}

	/** Pares de elementos de la misma capa que se pisan más de 1 cm (con una rejilla de 25 m: son unos 5000). */
	int32 CountOverlaps(const TNBeachLayout::FRoundLayout& L)
	{
		constexpr double GridCell = 2500.0;
		TMap<FIntPoint, TArray<int32>> Grid;
		for (int32 i = 0; i < L.Items.Num(); ++i)
		{
			const TNBeachLayout::FItem& It = L.Items[i];
			const FVector2D A = It.EndA();
			const FVector2D B = It.EndB();
			const int32 X0 = FMath::FloorToInt32((FMath::Min(A.X, B.X) - It.Core) / GridCell);
			const int32 X1 = FMath::FloorToInt32((FMath::Max(A.X, B.X) + It.Core) / GridCell);
			const int32 Y0 = FMath::FloorToInt32((FMath::Min(A.Y, B.Y) - It.Core) / GridCell);
			const int32 Y1 = FMath::FloorToInt32((FMath::Max(A.Y, B.Y) + It.Core) / GridCell);
			for (int32 GX = X0; GX <= X1; ++GX)
			{
				for (int32 GY = Y0; GY <= Y1; ++GY) { Grid.FindOrAdd(FIntPoint(GX, GY)).Add(i); }
			}
		}
		TSet<uint64> Seen;
		int32 Bad = 0;
		for (const TPair<FIntPoint, TArray<int32>>& Entry : Grid)
		{
			const TArray<int32>& Here = Entry.Value;
			for (int32 a = 0; a < Here.Num(); ++a)
			{
				for (int32 b = 0; b < a; ++b)
				{
					const uint64 Key = (static_cast<uint64>(FMath::Min(Here[a], Here[b])) << 32) | static_cast<uint64>(FMath::Max(Here[a], Here[b]));
					bool bAlready = false;
					Seen.Add(Key, &bAlready);
					if (bAlready) { continue; }
					const TNBeachLayout::FItem& P = L.Items[Here[a]];
					const TNBeachLayout::FItem& Q = L.Items[Here[b]];
					if (P.bOverlay == Q.bOverlay && TNBeachLayout::Clearance(P, Q) < -1.0) { ++Bad; }
				}
			}
		}
		return Bad;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Reparto: determinismo
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachLayoutDeterminismTest,
	"Tortunabo.Beach.Layout.Determinism",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachLayoutDeterminismTest::RunTest(const FString& Parameters)
{
	// Con la semilla y la dificultad (lo que se replica con la ronda), el mismo reparto en todas las máquinas.
	for (const ETNProcDifficulty Difficulty : { ETNProcDifficulty::Easy, ETNProcDifficulty::Normal, ETNProcDifficulty::Hard })
	{
		const TCHAR* Name = TNBeachLayoutTest::DifficultyName(Difficulty);
		for (const int32 Seed : { 1, 7, 12345, -99, 2026 })
		{
			TNBeachLayout::FRoundLayout A;
			TNBeachLayout::FRoundLayout B;
			TNBeachLayout::GenerateRound(Seed, Difficulty, A);
			TNBeachLayout::GenerateRound(Seed, Difficulty, B);
			TestTrue(FString::Printf(TEXT("%s, semilla %d: el mismo reparto dos veces"), Name, Seed), TNBeachLayoutTest::SameLayout(A, B));
			// 3500 con 1200 m (medido: 3900-5200); 2300 con 800 m (medido: 2450-3400).
			TestTrue(FString::Printf(TEXT("%s, semilla %d: hay reparto a rebosar (%d elementos)"), Name, Seed, A.Items.Num()), A.Items.Num() > 2300);
			TestTrue(FString::Printf(TEXT("%s, semilla %d: el reparto sabe su dificultad"), Name, Seed), A.Difficulty == Difficulty);
		}
	}
	TNBeachLayout::FRoundLayout One;
	TNBeachLayout::FRoundLayout Two;
	TNBeachLayout::FRoundLayout OneHard;
	TNBeachLayout::GenerateRound(1, ETNProcDifficulty::Normal, One);
	TNBeachLayout::GenerateRound(2, ETNProcDifficulty::Normal, Two);
	TNBeachLayout::GenerateRound(1, ETNProcDifficulty::Hard, OneHard);
	TestFalse(TEXT("semillas distintas, repartos distintos"), TNBeachLayoutTest::SameLayout(One, Two));
	TestFalse(TEXT("dificultades distintas, repartos distintos"), TNBeachLayoutTest::SameLayout(One, OneHard));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Decorado suelto sobre la malla de la arena
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachLitterOnMeshTest,
	"Tortunabo.Beach.Layout.LitterOnMesh",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachLitterOnMeshTest::RunTest(const FString& Parameters)
{
	// MeshSandZ es la arena tal como se dibuja (triángulos de la rejilla de 3 m): en los vértices, la cota con los asientos;
	// y el plano de una pieza suelta apoyada con su normal no se separa de la malla dentro de su triángulo (#253).
	TNBeachLayout::FRoundLayout L;
	TNBeachLayout::GenerateRound(1, ETNProcDifficulty::Normal, L);
	const double S = TNBeachLayout::TerrainGridStep;
	int32 NodeMisses = 0;
	for (int32 k = 0; k < 40; ++k)
	{
		const double X = TNBeachLayout::TerrainGridMaxX - S * (3 + k * 37);
		const double Y = S * ((k * 13) % 40 - 20);
		const double Seated = TNBeachLayout::SeatedZ(L, X, Y, TNBeachLayout::SandZ(X, Y));
		NodeMisses += FMath::IsNearlyEqual(TNBeachLayout::MeshSandZ(L, X, Y), Seated, 0.01) ? 0 : 1;
	}
	TestEqual(TEXT("en los vértices de la rejilla, la cota de la arena con los asientos"), NodeMisses, 0);

	auto Cell = [S](double X, double Y)
	{
		const double Fx = (X - TNBeachLayout::TerrainGridMaxX) / S;
		const double Fy = Y / S;
		const double U = Fx - FMath::FloorToDouble(Fx);
		const double V = Fy - FMath::FloorToDouble(Fy);
		return FIntVector(FMath::FloorToInt32(Fx), FMath::FloorToInt32(Fy), U + V <= 1.0 ? 0 : 1);
	};
	int32 Litter = 0;
	int32 Sloped = 0;
	double WorstGap = 0.0;
	for (const TNBeachLayout::FItem& Item : L.Items)
	{
		if (!TNBeachLayout::IsLitter(Item) || FMath::Abs(Item.Pos.Y) > TNBeachLayout::HalfWidth || Item.Pos.X < 0.0
			|| Item.Pos.X > TNBeachLayout::TerrainGridMaxX - S)
		{
			continue;
		}
		++Litter;
		FVector Normal;
		const double Z = TNBeachLayout::MeshSandZ(L, Item.Pos.X, Item.Pos.Y, &Normal);
		if (Normal.Z > FMath::Cos(FMath::DegreesToRadians(30.0)) && Normal.Z < 0.999)
		{
			++Sloped;
		}
		const FVector Center(Item.Pos.X, Item.Pos.Y, Z);
		const FIntVector Home = Cell(Item.Pos.X, Item.Pos.Y);
		const double R = FMath::Min(Item.Radius, 60.0);
		for (int32 a = 0; a < 8; ++a)
		{
			const double Ang = UE_DOUBLE_TWO_PI * a / 8.0;
			const double PX = Item.Pos.X + R * FMath::Cos(Ang);
			const double PY = Item.Pos.Y + R * FMath::Sin(Ang);
			if (Cell(PX, PY) != Home)
			{
				continue;
			}
			// Altura del plano de la pieza (por el centro, con su normal) en ese punto, contra la de la malla.
			const double PlaneZ = Center.Z - (Normal.X * (PX - Center.X) + Normal.Y * (PY - Center.Y)) / Normal.Z;
			WorstGap = FMath::Max(WorstGap, FMath::Abs(PlaneZ - TNBeachLayout::MeshSandZ(L, PX, PY)));
		}
	}
	TestTrue(FString::Printf(TEXT("hay decorado suelto en la playa (%d) y parte en cuesta (%d)"), Litter, Sloped), Litter > 100 && Sloped > 0);
	TestTrue(FString::Printf(TEXT("apoyado con su normal, a menos de 5 cm de la malla en su triángulo (peor: %.2f cm)"), WorstGap), WorstGap < 5.0);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Reparto: reglas sobre muchas semillas (y con cada dificultad)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachLayoutRulesTest,
	"Tortunabo.Beach.Layout.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachLayoutRulesTest::RunTest(const FString& Parameters)
{
	using TNBeachLayout::FItem;
	using TNBeachLayout::EItemRole;
	using TNBeachLayout::EInterestKind;

	// Todo lo del contrato que no tiene pasada propia puede salir en las bandas (lo nuevo entra solo).
	for (int32 i = 0; i < static_cast<int32>(ETNBeachElement::Count); ++i)
	{
		const ETNBeachElement E = static_cast<ETNBeachElement>(i);
		const TNBeachLayout::FElementRule Rule = TNBeachLayout::RuleOf(E);
		TestTrue(FString::Printf(TEXT("%s se reparte"), *UEnum::GetValueAsString(E)), Rule.bSpecial || (Rule.Weight > 0.0 && Rule.MinT < Rule.MaxT && Rule.MaxPerRound > 0));
	}

	// Normal con 24 semillas y las otras dos con 8. Mínimos con margen de lo medido con
	// 800 m (los de 1200 m, entre paréntesis, eran los medidos con el puerto a Python del reparto, 72 rondas): elementos
	// 2876 / 2869 / 2458 (4431 / 4641 / 3936), pasos de quads 1-2 / 1-2 / 2-3 (2-3 / 1-2 / 3-4) y zonas de gaviotas
	// 3-4 / 2-3 / 4-6 (4-6 / 3-5 / 6-9), en Normal / Fácil /
	// Difícil. Los pasos de quads y las gaviotas salen de su cuota de 1200 m por LengthScale (con redondeo al azar en los
	// quads, mínimo uno); los demás mínimos son los de antes por LengthScale, bajados algo si la medida quedaba pegada.
	// Enemigos y cangrejos, medidos tras el recorte de #850 (solo los cangrejos del Excel, los quads y las gaviotas):
	// enemigos 12-15 / 8-9 / 27-30 y cangrejos 8-9 / 4-6 / 21-22.
	struct FProfileCase
	{
		ETNProcDifficulty Difficulty;
		int32 NumSeeds;
		int32 MinItems;
		int32 MinEnemies;
		int32 MinCrabs;
		int32 MinQuads;
		int32 MaxQuads;
		int32 MinGulls;
		int32 MaxGulls;
	};
	const FProfileCase Cases[] = {
		{ ETNProcDifficulty::Normal, 24, 2600, 10, 6, 1, 2, 3, 4 },
		{ ETNProcDifficulty::Easy, 8, 2600, 6, 3, 1, 2, 2, 3 },
		{ ETNProcDifficulty::Hard, 8, 2300, 22, 16, 2, 3, 4, 6 },
	};

	double AreaFirst = 0.0;
	double AreaLast = 0.0;
	int32 Rounds = 0;
	double NormalMs = 0.0;
	double NormalMaxMs = 0.0;
	TArray<int32> Seen;
	Seen.Init(0, static_cast<int32>(ETNBeachElement::Count));

	for (const FProfileCase& Case : Cases)
	{
		const bool bNormal = Case.Difficulty == ETNProcDifficulty::Normal;
		for (int32 s = 0; s < Case.NumSeeds; ++s)
		{
			const int32 Seed = 1000 + s * 7919;
			const FString Ctx = FString::Printf(TEXT("%s, semilla %d"), TNBeachLayoutTest::DifficultyName(Case.Difficulty), Seed);
			TNBeachLayout::FRoundLayout L;
			const double T0 = FPlatformTime::Seconds();
			TNBeachLayout::GenerateRound(Seed, Case.Difficulty, L);
			const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
			if (bNormal)
			{
				// La primera ronda del proceso paga las tablas fijas (entre ellas la rejilla de la arena): no cuenta.
				if (s > 0)
				{
					NormalMs += Ms;
					NormalMaxMs = FMath::Max(NormalMaxMs, Ms);
				}
			}
			++Rounds;
			TestTrue(Ctx + TEXT(": paso libre"), L.bPassageOk);
			TestTrue(FString::Printf(TEXT("%s: a rebosar (%d elementos)"), *Ctx, L.Items.Num()), L.Items.Num() >= Case.MinItems);

			int32 Lanes = 0;
			int32 Seated = 0;
			int32 Trampolines = 0;
			bool bBounds = true;
			bool bFarFromStart = true;
			bool bSpecOk = true;
			bool bLanesOk = true;
			bool bTerrainOk = true;
			bool bArcsFree = true;
			TArray<const FItem*> Gulls;
			TNBeachLayout::FPassGrid Grid;
			Grid.Init();
			for (int32 i = 0; i < L.Items.Num(); ++i)
			{
				const FItem& It = L.Items[i];
				++Seen[static_cast<int32>(It.Element)];
				Grid.Stamp(It, 1);
				bSpecOk &= It.Spec.Element == It.Element && It.Spec.SizeScale >= 0.59f && It.Spec.SizeScale <= 1.41f && It.Core <= It.Radius + 0.01;
				bSpecOk &= FMath::IsNearlyEqual(static_cast<double>(It.Spec.Extent), It.HalfLength * 2.0, 1.0);
				bTerrainOk &= TNBeachLayout::TerrainAllows(It) && TNBeachLayout::SeatIsGentle(It);
				if (!It.bOverlay) { bBounds &= TNBeachLayout::InBounds(It); }
				if (TNBeachLayout::HasSeat(It)) { ++Seated; }
				if (!It.bOverlay)
				{
					for (int32 k = 0; k < TNBeachLayout::NumStartSpots; ++k)
					{
						const FVector2D Spot(TNBeachLayout::StartSpot(k));
						double T = 0.0;
						bFarFromStart &= TNProcMap::DistPointSegment(Spot, It.EndA(), It.EndB(), T) - It.Radius >= TNBeachLayout::ItemsStartX;
					}
				}
				// El arco de salto de todos los trampolines, libre de todo lo demás (los pasos de quads lo cruzan por debajo). Los
				// de delante de un obstáculo (rol Launcher) pisan, además, lo que saltan.
				const bool bArc = TNBeachLayout::RuleOf(It.Element).bLauncher;
				const bool bAimed = bArc && It.Role == EItemRole::Launcher;
				if (bArc)
				{
					const FItem Zone = TNBeachLayout::FBuilder::JumpArcZone(It);
					for (int32 j = 0; j < L.Items.Num(); ++j)
					{
						const FItem& Other = L.Items[j];
						if (j == i || Other.bOverlay || Other.Element == ETNBeachElement::QuadLane) { continue; }
						if (bAimed && TNBeachLayoutTest::IsJumpTarget(Other)) { continue; }
						if (TNBeachLayout::Clearance(Zone, Other) < -1.0) { bArcsFree = false; }
					}
				}
				switch (It.Element)
				{
					case ETNBeachElement::GullZone: Gulls.Add(&It); break;
					case ETNBeachElement::Trampoline: ++Trampolines; break;
					case ETNBeachElement::QuadLane:
						++Lanes;
						bLanesOk &= FMath::IsNearlyEqual(static_cast<double>(It.Spec.Extent), TNBeach::CourseWidth, 1.0) && FMath::IsNearlyEqual(It.Yaw, 90.0, 0.01);
						break;
					default: break;
				}
				if (bNormal && !It.bOverlay && It.Role == EItemRole::Fill)
				{
					const double T = TNBeachLayout::ProgressOfX(It.Pos.X);
					if (T < 1.0 / 3.0) { AreaFirst += It.CoreArea(); }
					if (T > 2.0 / 3.0) { AreaLast += It.CoreArea(); }
				}
			}
			// Gaviotas: repartidas y cada zona con su círculo.
			bool bGullsApart = true;
			bool bGullsDistinct = true;
			const double GullSpacing = TNBeachLayout::GullZoneSpacing(Gulls.Num());
			for (int32 a = 0; a < Gulls.Num(); ++a)
			{
				for (int32 b = 0; b < a; ++b)
				{
					bGullsApart &= FVector2D::Distance(Gulls[a]->Pos, Gulls[b]->Pos) >= GullSpacing - 1.0;
					bGullsDistinct &= FMath::Abs(Gulls[a]->Spec.SizeScale - Gulls[b]->Spec.SizeScale) > 0.01f;
				}
			}
			// Puntos interesantes.
			int32 Kinds[static_cast<int32>(EInterestKind::Detour) + 1] = {};
			for (const TNBeachLayout::FInterestPoint& Point : L.Interest) { ++Kinds[static_cast<int32>(Point.Kind)]; }
			double Longest = 0.0;
			const int32 LongRuns = TNBeachLayoutTest::CountLongStraightRuns(L, 1.6 * TNBeachLayout::MaxStraightRun, Longest);
			const int32 Overlaps = TNBeachLayoutTest::CountOverlaps(L);

			TestTrue(Ctx + TEXT(": todo dentro de la playa repartible (15 m de la salida, 30 m del borde)"), bBounds);
			TestTrue(FString::Printf(TEXT("%s: sin solapes (%d)"), *Ctx, Overlaps), Overlaps == 0);
			TestTrue(Ctx + TEXT(": nada a menos de 15 m de los huevos de la salida"), bFarFromStart);
			TestTrue(Ctx + TEXT(": especificaciones coherentes"), bSpecOk);
			TestTrue(Ctx + TEXT(": nada pisa pozas, trincheras ni cornisas, y sin asientos con paredes"), bTerrainOk);
			TestTrue(Ctx + TEXT(": arcos de salto libres"), bArcsFree);
			TestTrue(FString::Printf(TEXT("%s: %d-%d pasos de quads a lo ancho (%d)"), *Ctx, Case.MinQuads, Case.MaxQuads, Lanes),
				Lanes >= Case.MinQuads && Lanes <= Case.MaxQuads && bLanesOk);
			TestTrue(FString::Printf(TEXT("%s: %d-%d zonas de gaviotas separadas y distintas (%d)"), *Ctx, Case.MinGulls, Case.MaxGulls, Gulls.Num()),
				Gulls.Num() >= Case.MinGulls && Gulls.Num() <= Case.MaxGulls && bGullsApart && bGullsDistinct);
			TestEqual(Ctx + TEXT(": un asiento por elemento que lo lleva"), L.Stamps.Num(), Seated);
			TestTrue(Ctx + TEXT(": el paso sigue libre al rehacer la rejilla"), Grid.IsConnected());
			TestTrue(FString::Printf(TEXT("%s: muchos enemigos (%d, %d cangrejos)"), *Ctx, L.NumEnemies, L.NumCrabs), L.NumEnemies >= Case.MinEnemies && L.NumCrabs >= Case.MinCrabs);
			TestTrue(FString::Printf(TEXT("%s: trampolines (%d)"), *Ctx, Trampolines), Trampolines >= 5);
			TestTrue(FString::Printf(TEXT("%s: filas que obligan a zigzaguear (%d)"), *Ctx, L.NumRows), L.NumRows >= 2);
			TestTrue(FString::Printf(TEXT("%s: piezas militares (%d)"), *Ctx, L.NumMilitary), L.NumMilitary >= 4);
			TestTrue(FString::Printf(TEXT("%s: ocupación (media %.0f %%, primer tercio %.0f %%)"), *Ctx, L.CoverMean * 100.0, L.CoverFirstThird * 100.0),
				L.CoverMean >= 0.42 && L.CoverFirstThird >= 0.38 && L.BandCover.Num() == TNBeachLayout::NumBands());
			// Antes (1200 m): 10 arcos, 10 cimas, 3 atajos, 10 trincheras (las dos líneas fijas: no cambian) y 10 caminos.
			TestTrue(FString::Printf(TEXT("%s: puntos interesantes (%d arcos, %d cimas, %d atajos, %d trincheras, %d caminos)"), *Ctx,
				Kinds[static_cast<int32>(EInterestKind::JumpArc)], Kinds[static_cast<int32>(EInterestKind::Summit)], Kinds[static_cast<int32>(EInterestKind::Shortcut)],
				Kinds[static_cast<int32>(EInterestKind::Trench)], Kinds[static_cast<int32>(EInterestKind::Detour)]),
				Kinds[static_cast<int32>(EInterestKind::JumpArc)] >= 7 && Kinds[static_cast<int32>(EInterestKind::Summit)] >= 7
				&& Kinds[static_cast<int32>(EInterestKind::Shortcut)] >= 2 && Kinds[static_cast<int32>(EInterestKind::Trench)] >= 10
				&& Kinds[static_cast<int32>(EInterestKind::Detour)] >= 7);
			TestTrue(FString::Printf(TEXT("%s: sin líneas rectas libres hacia el mar (%d filas de más de %.0f m; la más larga, %.0f m)"), *Ctx, LongRuns,
				1.6 * TNBeachLayout::MaxStraightRun / 100.0, Longest / 100.0), LongRuns <= 2);
		}
	}

	const int32 NormalSeeds = Cases[0].NumSeeds;
	TestTrue(FString::Printf(TEXT("más denso hacia el mar (%.0f m² en el último tercio frente a %.0f m² en el primero)"), AreaLast / 1e4, AreaFirst / 1e4),
		AreaLast > 1.05 * AreaFirst);

	// Tiempo del reparto en esta máquina (en el juego va en otro hilo; lo pedido: unos 150 ms como mucho).
	const double MeanMs = NormalSeeds > 1 ? NormalMs / (NormalSeeds - 1) : 0.0;
	AddInfo(FString::Printf(TEXT("Reparto en Normal: %.0f ms de media, %.0f ms el más lento (sin la primera ronda, que hace las tablas fijas)."), MeanMs, NormalMaxMs));
	if (MeanMs > 150.0)
	{
		AddWarning(FString::Printf(TEXT("El reparto tarda %.0f ms de media (se buscaban ~150 ms como mucho)."), MeanMs));
	}

	FString Counts;
	for (int32 i = 0; i < Seen.Num(); ++i)
	{
		Counts += FString::Printf(TEXT("%s%s %d"), Counts.IsEmpty() ? TEXT("") : TEXT(", "), *UEnum::GetValueAsString(static_cast<ETNBeachElement>(i)), Seen[i]);
	}
	AddInfo(FString::Printf(TEXT("Elementos en %d rondas: %s"), Rounds, *Counts));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Reparto: perfiles de dificultad
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachLayoutDifficultyTest,
	"Tortunabo.Beach.Layout.Difficulty",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachLayoutDifficultyTest::RunTest(const FString& Parameters)
{
	// Los multiplicadores de la dificultad.
	const TNBeachLayout::FDifficultyProfile Easy = TNBeachLayout::DifficultyProfileOf(ETNProcDifficulty::Easy);
	const TNBeachLayout::FDifficultyProfile Normal = TNBeachLayout::DifficultyProfileOf(ETNProcDifficulty::Normal);
	const TNBeachLayout::FDifficultyProfile Hard = TNBeachLayout::DifficultyProfileOf(ETNProcDifficulty::Hard);
	TestTrue(TEXT("Fácil: ayudas x1,6, trampas x0,7, enemigos x0,6"), Easy.Aids == 1.6 && Easy.Traps == 0.7 && Easy.Enemies == 0.6);
	TestTrue(TEXT("Normal: x1"), Normal.Aids == 1.0 && Normal.Traps == 1.0 && Normal.Enemies == 1.0);
	TestTrue(TEXT("Difícil: ayudas x1,4, trampas x1,8, enemigos x2,5"), Hard.Aids == 1.4 && Hard.Traps == 1.8 && Hard.Enemies == 2.5);
	TestTrue(TEXT("grupos: el trampolín es ayuda, las algas trampa, el cangrejo arrastrador enemigo y el coco nada"),
		TNBeachLayout::ScaleGroupOf(ETNBeachElement::Trampoline) == TNBeachLayout::EScaleGroup::Aid
		&& TNBeachLayout::ScaleGroupOf(ETNBeachElement::Seaweed) == TNBeachLayout::EScaleGroup::Trap
		&& TNBeachLayout::ScaleGroupOf(ETNBeachElement::DragCrab) == TNBeachLayout::EScaleGroup::Enemy
		&& TNBeachLayout::ScaleGroupOf(ETNBeachElement::Coconut) == TNBeachLayout::EScaleGroup::None);

	// Lo que sale de verdad en 8 semillas por perfil. La playa ya está llena en Normal: los cupos llevan el multiplicador
	// entero, pero en Difícil las trampas y las ayudas no caben todas (medido con 800 m: enemigos x2,6, trampas x1,3, ayudas
	// x1,1; en Fácil, x0,70, x0,69 y x1,40; con 1200 m, casi igual).
	struct FTotals
	{
		int32 Enemies = 0;
		int32 Hazards = 0;
		int32 Aids = 0;
	};
	FTotals Totals[3];
	const ETNProcDifficulty Order[3] = { ETNProcDifficulty::Easy, ETNProcDifficulty::Normal, ETNProcDifficulty::Hard };
	for (int32 d = 0; d < 3; ++d)
	{
		for (int32 s = 0; s < 8; ++s)
		{
			TNBeachLayout::FRoundLayout L;
			TNBeachLayout::GenerateRound(1000 + s * 7919, Order[d], L);
			TestTrue(FString::Printf(TEXT("%s: el reparto lleva su perfil"), TNBeachLayoutTest::DifficultyName(Order[d])),
				L.Profile.Aids == TNBeachLayout::DifficultyProfileOf(Order[d]).Aids && L.Profile.Enemies == TNBeachLayout::DifficultyProfileOf(Order[d]).Enemies);
			for (const TNBeachLayout::FItem& It : L.Items)
			{
				switch (TNBeachLayout::ScaleGroupOf(It.Element))
				{
					case TNBeachLayout::EScaleGroup::Enemy: ++Totals[d].Enemies; break;
					case TNBeachLayout::EScaleGroup::Trap: ++Totals[d].Hazards; break;
					case TNBeachLayout::EScaleGroup::Aid: ++Totals[d].Aids; break;
					default: break;
				}
			}
		}
	}
	const FTotals& E = Totals[0];
	const FTotals& N = Totals[1];
	const FTotals& H = Totals[2];
	auto Ratio = [](int32 A, int32 B) { return B > 0 ? static_cast<double>(A) / B : 0.0; };
	AddInfo(FString::Printf(TEXT("Fácil / Normal / Difícil en 8 rondas: enemigos %d / %d / %d, trampas %d / %d / %d, ayudas %d / %d / %d."),
		E.Enemies, N.Enemies, H.Enemies, E.Hazards, N.Hazards, H.Hazards, E.Aids, N.Aids, H.Aids));
	TestTrue(FString::Printf(TEXT("enemigos: Fácil x%.2f (<= 0,75), Difícil x%.2f (>= 2)"), Ratio(E.Enemies, N.Enemies), Ratio(H.Enemies, N.Enemies)),
		Ratio(E.Enemies, N.Enemies) <= 0.75 && Ratio(H.Enemies, N.Enemies) >= 2.0);
	TestTrue(FString::Printf(TEXT("trampas: Fácil x%.2f (<= 0,8), Difícil x%.2f (>= 1,2)"), Ratio(E.Hazards, N.Hazards), Ratio(H.Hazards, N.Hazards)),
		Ratio(E.Hazards, N.Hazards) <= 0.8 && Ratio(H.Hazards, N.Hazards) >= 1.2);
	TestTrue(FString::Printf(TEXT("ayudas: Fácil x%.2f (>= 1,25), Difícil x%.2f (>= 1)"), Ratio(E.Aids, N.Aids), Ratio(H.Aids, N.Aids)),
		Ratio(E.Aids, N.Aids) >= 1.25 && Ratio(H.Aids, N.Aids) >= 1.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
