#include "World/Beach/TN_BeachSandDungeon.h"
#include "World/Beach/TN_BeachShellGate.h"
#include "Core/TN_Log.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "UObject/UObjectGlobals.h"
#include "TN_BeachTrapKit.h"
#include "TN_BeachCastlePrizes.h"

/**
 * Planta del castillo (espacio del actor, cm; X = sentido de la carrera). Murallas de fuera de OuterWall de grueso; dentro:
 *
 *   IX0 ......... XA | XAw ........ XS0 ... XS1 ........ IX1
 *   +------------+---+---------------------------------------+  IY1
 *   |            | P |  pasillo (planta baja, techado)  esc. |
 *   |  sala de   | U |---------------------------------+     |  YC0
 *   |  columnas  | E |  terraza D (arriba, +3,2 m)      |  C  |
 *   |  (baja)    | R |                                 | (arr)|
 *   +------------+---+---------------------------------+-----+  IY0
 *
 * La sala C y la terraza D forman el piso de arriba (bloques macizos hasta UpZ); la escalera del pasillo sube a C.
 * Entrada por la muralla -X (planta baja) y salida por la +X (piso de arriba) a una rampa exterior.
 */
namespace TNBeachDungeonDetail
{
	using FBuffers = TNBeachTrapKit::FBuffers;
	using FHulls = TNBeachTrapKit::FHulls;

	constexpr double OuterWall = 240.0;
	constexpr double InnerWall = 180.0;
	constexpr double WallTop = 820.0;
	constexpr double FloorZ = 50.0;
	constexpr double UpH = 320.0;
	constexpr double CorrW = 360.0;
	constexpr double EntryW = 400.0;
	constexpr double EntryH = 460.0;
	constexpr double ExitW = 360.0;
	constexpr double ExitH = 400.0;
	constexpr double GateW = 320.0;
	constexpr double StepRise = 40.0;
	constexpr double StepRun = 45.0;
	constexpr int32 Steps = 8;
	constexpr double ExitRamp = 640.0;
	constexpr double TowerH = 1150.0;
	constexpr double RoofBottom = 430.0;
	constexpr double RoofTop = 500.0;
	constexpr double WindowW = 200.0;
	constexpr double WindowH = 190.0;
	constexpr double WindowSill = 80.0;
	constexpr double LowWallH = 70.0;

	struct FOpening
	{
		double Center = 0.0;
		double Width = 0.0;
		double Bottom = 0.0;
		double Top = 0.0;
	};

	struct FLayout
	{
		double HX = 2880.0;
		double HY = 2200.0;
		double IX0 = 0.0;
		double IX1 = 0.0;
		double IY0 = 0.0;
		double IY1 = 0.0;
		double XA = 0.0;
		double XAw = 0.0;
		double XS0 = 0.0;
		double XS1 = 0.0;
		double YC0 = 0.0;
		double UpZ = FloorZ + UpH;
		double TowerR = 240.0;
		double EntY = 0.0;
		double ExitY = 0.0;
	};

	FLayout MakeLayout(double Fit)
	{
		FLayout Out;
		Out.HX = FMath::Min(0.72 * Fit, 0.95 * Fit - ExitRamp);
		Out.HY = 0.55 * Fit;
		Out.IX0 = -Out.HX + OuterWall;
		Out.IX1 = Out.HX - OuterWall;
		Out.IY0 = -Out.HY + OuterWall;
		Out.IY1 = Out.HY - OuterWall;
		const double ILen = Out.IX1 - Out.IX0;
		const double StairRun = Steps * StepRun;
		double RoomA = 0.36 * ILen;
		double RoomC = FMath::Max(900.0, 0.26 * ILen);
		double Corr = ILen - InnerWall - RoomA - StairRun - RoomC;
		if (Corr < 700.0)
		{
			const double Deficit = 700.0 - Corr;
			RoomA -= Deficit * 0.5;
			RoomC = FMath::Max(700.0, RoomC - Deficit * 0.5);
			Corr = ILen - InnerWall - RoomA - StairRun - RoomC;
		}
		Out.XA = Out.IX0 + RoomA;
		Out.XAw = Out.XA + InnerWall;
		Out.XS0 = Out.XAw + FMath::Max(300.0, Corr);
		Out.XS1 = Out.XS0 + StairRun;
		Out.YC0 = Out.IY1 - CorrW;
		Out.UpZ = FloorZ + UpH;
		Out.TowerR = FMath::Clamp(0.06 * Fit, 200.0, 320.0);
		const double IWid = Out.IY1 - Out.IY0;
		Out.EntY = Out.IY0 + 0.3 * IWid;
		Out.ExitY = Out.IY0 + 0.62 * IWid;
		return Out;
	}

	/** Bloque de arena entre dos esquinas cualesquiera (ejes del actor), con su casco. */
	void AddBlock(FBuffers& B, FHulls& Hulls, double X0, double X1, double Y0, double Y1, double Z0, double Z1, bool bMarks = true)
	{
		TNBeachTrapKit::AddSandBox(B, &Hulls, FVector(FMath::Min(X0, X1), FMath::Min(Y0, Y1), Z0), FVector(FMath::Max(X0, X1), FMath::Max(Y0, Y1), Z1), bMarks);
	}

	/**
	 * Muralla recta: a lo largo de X (bAlongX) o de Y, de A0 a A1, con el grosor de T0 a T1 y de Z0 a Z1, dejando los huecos
	 * de Openings (puertas y ventanas: tramo bajo el alféizar y dintel encima). Con adornos: arco oscuro sobre cada hueco y
	 * alféizar claro en las ventanas.
	 */
	void AddWall(FBuffers& B, FBuffers& Decor, FHulls& Hulls, bool bAlongX, double A0, double A1, double T0, double T1, double Z0, double Z1, TArray<FOpening> Openings)
	{
		Openings.Sort([](const FOpening& Lhs, const FOpening& Rhs) { return Lhs.Center < Rhs.Center; });
		const auto Piece = [&B, &Hulls, bAlongX, T0, T1](double P0, double P1, double Za, double Zb)
		{
			if (P1 - P0 < 1.0 || Zb - Za < 1.0)
			{
				return;
			}
			if (bAlongX)
			{
				AddBlock(B, Hulls, P0, P1, T0, T1, Za, Zb);
			}
			else
			{
				AddBlock(B, Hulls, T0, T1, P0, P1, Za, Zb);
			}
		};
		double Cursor = A0;
		const FLinearColor Trim = TNPlaygroundKit::Shade(TNBeachTrapKit::SandMark(), 0.85);
		for (const FOpening& Hole : Openings)
		{
			const double O0 = Hole.Center - Hole.Width * 0.5;
			const double O1 = Hole.Center + Hole.Width * 0.5;
			Piece(Cursor, O0, Z0, Z1);
			Piece(O0, O1, Z0, Hole.Bottom);
			Piece(O0, O1, Hole.Top, Z1);
			Cursor = O1;
			// Arco oscuro por encima y jambas, en las dos caras.
			for (const double Face : { T0, T1 })
			{
				const double Out = Face == T0 ? -1.0 : 1.0;
				const double Tf = Face + Out * 3.0;
				const FVector ArchC = bAlongX ? FVector(Hole.Center, Tf, Hole.Top + 14.0) : FVector(Tf, Hole.Center, Hole.Top + 14.0);
				const FVector ArchH = bAlongX ? FVector(Hole.Width * 0.5 + 22.0, 3.0, 14.0) : FVector(3.0, Hole.Width * 0.5 + 22.0, 14.0);
				TNPlaygroundKit::AddAxisBox(Decor, ArchC, ArchH, Trim);
				for (const double Jamb : { O0 - 11.0, O1 + 11.0 })
				{
					const double Zm = 0.5 * (Hole.Bottom + Hole.Top);
					const FVector JambC = bAlongX ? FVector(Jamb, Tf, Zm) : FVector(Tf, Jamb, Zm);
					const FVector JambH = bAlongX ? FVector(11.0, 3.0, 0.5 * (Hole.Top - Hole.Bottom)) : FVector(3.0, 11.0, 0.5 * (Hole.Top - Hole.Bottom));
					TNPlaygroundKit::AddAxisBox(Decor, JambC, JambH, Trim);
				}
				if (Hole.Bottom > Z0 + 1.0)
				{
					const FVector SillC = bAlongX ? FVector(Hole.Center, Tf, Hole.Bottom - 4.0) : FVector(Tf, Hole.Center, Hole.Bottom - 4.0);
					const FVector SillH = bAlongX ? FVector(Hole.Width * 0.5 + 12.0, 8.0, 5.0) : FVector(8.0, Hole.Width * 0.5 + 12.0, 5.0);
					TNPlaygroundKit::AddAxisBox(Decor, SillC, SillH, TNBeachTrapKit::SandTop());
				}
			}
		}
		Piece(Cursor, A1, Z0, Z1);
	}

	/** Almenas a lo largo de una muralla (solo adorno). */
	void AddMerlons(FBuffers& Decor, bool bAlongX, double A0, double A1, double TMid, double Thick)
	{
		const int32 Count = FMath::Max(2, FMath::RoundToInt32((A1 - A0) / 120.0));
		const double Step = (A1 - A0) / Count;
		for (int32 m = 0; m < Count; m += 2)
		{
			const double C = A0 + (m + 0.5) * Step;
			const FVector Center = bAlongX ? FVector(C, TMid, WallTop + 28.0) : FVector(TMid, C, WallTop + 28.0);
			const FVector Half = bAlongX ? FVector(Step * 0.45, Thick * 0.42, 28.0) : FVector(Thick * 0.42, Step * 0.45, 28.0);
			TNPlaygroundKit::AddAxisBox(Decor, Center, Half, TNBeachTrapKit::SandTop());
		}
	}

	/** Torre de cubo de arena con marcas, cornisa, almenas y bandera. */
	void AddTower(FBuffers& B, FBuffers& Decor, FHulls& Hulls, const FVector2D& At, double Radius, int32 Variant)
	{
		const FVector Base(At.X, At.Y, -20.0);
		const FVector Top(At.X, At.Y, TowerH);
		TNPlaygroundKit::AddFrustum(B, Base, Top, Radius, Radius * 0.9, 22, TNBeachTrapKit::SandSide(), TNBeachTrapKit::SandTop(), false, true);
		for (const double K : { 0.22, 0.47, 0.72 })
		{
			const double Rr = FMath::Lerp(Radius, Radius * 0.9, K) + 6.0;
			const double Zk = FMath::Lerp(Base.Z, Top.Z, K);
			TNPlaygroundKit::AddFrustum(Decor, FVector(At.X, At.Y, Zk - 9.0), FVector(At.X, At.Y, Zk + 9.0), Rr, Rr, 22, TNBeachTrapKit::SandMark(), TNBeachTrapKit::SandMark(),
				false, false);
		}
		const double Rt = Radius * 0.9 + 26.0;
		TNPlaygroundKit::AddFrustum(Decor, FVector(At.X, At.Y, TowerH - 8.0), FVector(At.X, At.Y, TowerH + 34.0), Rt, Rt, 22, TNBeachTrapKit::SandMark(), TNBeachTrapKit::SandTop(),
			true, true);
		constexpr int32 Merlons = 10;
		for (int32 k = 0; k < Merlons; k += 1)
		{
			const double Ang = TNPlaygroundKit::KitTwoPi * k / Merlons;
			const FVector Dir(FMath::Cos(Ang), FMath::Sin(Ang), 0.0);
			TNPlaygroundKit::AddXfBox(Decor, FTransform(FRotator(0.0, FMath::RadiansToDegrees(Ang), 0.0), FVector(At.X, At.Y, 0.0) + Dir * (Rt - 16.0)),
				FVector(0.0, 0.0, TowerH + 34.0 + 36.0), FVector(16.0, Rt * 0.26, 36.0), TNBeachTrapKit::SandTop());
		}
		const FVector PoleFoot(At.X, At.Y, TowerH + 34.0);
		TNPlaygroundKit::AddRod(Decor, PoleFoot, PoleFoot + FVector(0.0, 0.0, 420.0), 7.0, 6, TNPlaygroundKit::Rgb(0x7A4E2B), FVector::ForwardVector);
		TNPlaygroundKit::AddPennant(Decor, PoleFoot + FVector(0.0, 0.0, 415.0), FVector(FMath::Cos(Variant * 1.7), FMath::Sin(Variant * 1.7), 0.0), 190.0, 110.0,
			TNPlaygroundKit::ToyColor(Variant));
		Hulls.Add(TNPlaygroundKit::HullCylinder(Base, TowerH + 20.0, Radius, Radius * 0.9, 12));
	}

	/** Columna de la sala (cubos apilados) con capitel. */
	void AddPillar(FBuffers& B, FBuffers& Decor, FHulls& Hulls, const FVector2D& At, double Radius, double Height)
	{
		const FVector Base(At.X, At.Y, FloorZ);
		TNPlaygroundKit::AddFrustum(B, Base, Base + FVector(0.0, 0.0, Height), Radius, Radius * 0.85, 18, TNBeachTrapKit::SandSide(), TNBeachTrapKit::SandTop(), false, true);
		for (double Z = 70.0; Z < Height - 40.0; Z += 90.0)
		{
			const double Rr = FMath::Lerp(Radius, Radius * 0.85, Z / Height) + 4.0;
			TNPlaygroundKit::AddFrustum(Decor, Base + FVector(0.0, 0.0, Z - 6.0), Base + FVector(0.0, 0.0, Z + 6.0), Rr, Rr, 18, TNBeachTrapKit::SandMark(),
				TNBeachTrapKit::SandMark(), false, false);
		}
		TNPlaygroundKit::AddAxisBox(Decor, Base + FVector(0.0, 0.0, Height + 18.0), FVector(Radius * 1.1, Radius * 1.1, 18.0), TNBeachTrapKit::SandTop());
		Hulls.Add(TNPlaygroundKit::HullCylinder(Base, Height, Radius, Radius * 0.85, 10));
	}

	/** Las murallas, suelos, escaleras y rampas siguen la planta; con Log, las torres, columnas, conchas y estrellas son piezas de arte. */
	void BuildCastle(FBuffers& B, FBuffers& Decor, FHulls& Hulls, const FLayout& L, uint32 Seed, TNArt::FPieceLog& Log)
	{
		const double IWid = L.IY1 - L.IY0;

		// Zócalo (el suelo de dentro) y rampita de la entrada.
		AddBlock(B, Hulls, -L.HX, L.HX, -L.HY, L.HY, -300.0, FloorZ, false);
		TNBeachTrapKit::AddSandRamp(B, &Hulls, FTransform(FVector(-L.HX - 160.0, L.EntY, 0.0)), 160.0, EntryW * 0.5 + 30.0, 0.0, FloorZ, -30.0);

		// Piso de arriba macizo: terraza D y sala C.
		AddBlock(B, Hulls, L.XAw, L.XS1, L.IY0, L.YC0, FloorZ, L.UpZ);
		AddBlock(B, Hulls, L.XS1, L.IX1, L.IY0, L.IY1, FloorZ, L.UpZ);

		// Escalera del pasillo al piso de arriba.
		for (int32 k = 0; k < Steps; ++k)
		{
			AddBlock(B, Hulls, L.XS0 + k * StepRun, L.XS0 + (k + 1) * StepRun, L.YC0, L.IY1, FloorZ, FloorZ + (k + 1) * StepRise, false);
		}

		// Pretil de la terraza sobre el pasillo y techo del pasillo con lucernarios.
		AddBlock(B, Hulls, L.XAw, L.XS1, L.YC0 - 60.0, L.YC0, L.UpZ, FloorZ + RoofBottom, false);
		const double RoofX1 = L.XS0 - 40.0;
		const int32 RoofSegs = FMath::Max(1, FMath::RoundToInt32((RoofX1 - L.XAw) / 520.0));
		const double RoofStep = (RoofX1 - L.XAw) / RoofSegs;
		for (int32 s = 0; s < RoofSegs; ++s)
		{
			const double X0 = L.XAw + s * RoofStep + (s > 0 ? 55.0 : 0.0);
			const double X1 = L.XAw + (s + 1) * RoofStep - (s + 1 < RoofSegs ? 55.0 : 0.0);
			AddBlock(B, Hulls, X0, X1, L.YC0 - 60.0, L.IY1, FloorZ + RoofBottom, FloorZ + RoofTop, false);
		}

		// Murallas de fuera: -X con la entrada; +X con la salida alta y dos ventanas al mar; +Y y -Y con ventanas.
		const double RoomAx = 0.5 * (L.IX0 + L.XA);
		const double RoomCx = 0.5 * (L.XS1 + L.IX1);
		const double TerraceX = 0.5 * (L.XAw + L.XS1);
		const FOpening Entry = { L.EntY, EntryW, FloorZ, FloorZ + EntryH };
		const FOpening Exit = { L.ExitY, ExitW, L.UpZ, L.UpZ + ExitH };
		const FOpening SeaA = { L.IY0 + 0.18 * IWid, WindowW, L.UpZ + WindowSill, L.UpZ + WindowSill + WindowH };
		const FOpening SeaB = { L.IY0 + 0.9 * IWid, WindowW, L.UpZ + WindowSill, L.UpZ + WindowSill + WindowH };
		const FOpening LowA = { RoomAx, WindowW, FloorZ + 120.0, FloorZ + 290.0 };
		const FOpening HighC = { RoomCx, WindowW, L.UpZ + WindowSill, L.UpZ + WindowSill + WindowH };
		const FOpening HighD = { TerraceX, WindowW, L.UpZ + WindowSill, L.UpZ + WindowSill + WindowH };
		AddWall(B, Decor, Hulls, false, -L.HY, L.HY, -L.HX, -L.HX + OuterWall, FloorZ, WallTop, { Entry });
		AddWall(B, Decor, Hulls, false, -L.HY, L.HY, L.HX - OuterWall, L.HX, FloorZ, WallTop, { Exit, SeaA, SeaB });
		AddWall(B, Decor, Hulls, true, -L.HX + OuterWall, L.HX - OuterWall, L.HY - OuterWall, L.HY, FloorZ, WallTop, { LowA, HighC });
		AddWall(B, Decor, Hulls, true, -L.HX + OuterWall, L.HX - OuterWall, -L.HY, -L.HY + OuterWall, FloorZ, WallTop, { LowA, HighD, HighC });
		AddMerlons(Decor, false, -L.HY, L.HY, -L.HX + OuterWall * 0.5, OuterWall);
		AddMerlons(Decor, false, -L.HY, L.HY, L.HX - OuterWall * 0.5, OuterWall);
		AddMerlons(Decor, true, -L.HX, L.HX, L.HY - OuterWall * 0.5, OuterWall);
		AddMerlons(Decor, true, -L.HX, L.HX, -L.HY + OuterWall * 0.5, OuterWall);

		// Muro de dentro entre la sala de las columnas y el resto, con el hueco de la puerta de conchas.
		const FOpening Gate = { L.YC0 + CorrW * 0.5, GateW, FloorZ, FloorZ + ATN_BeachShellGate::BareDoorHeight };
		AddWall(B, Decor, Hulls, false, L.IY0, L.IY1, L.XA, L.XAw, FloorZ, WallTop, { Gate });

		// Muretes de la sala de las ventanas (se saltan o se rodean por el hueco, cada uno a un lado).
		const double CLen = L.IX1 - L.XS1;
		AddBlock(B, Hulls, L.XS1 + 0.35 * CLen - 30.0, L.XS1 + 0.35 * CLen + 30.0, L.IY0, L.IY1 - 220.0, L.UpZ, L.UpZ + LowWallH, false);
		AddBlock(B, Hulls, L.XS1 + 0.7 * CLen - 30.0, L.XS1 + 0.7 * CLen + 30.0, L.IY0 + 220.0, L.IY1, L.UpZ, L.UpZ + LowWallH, false);

		// Rampa de la salida, fuera de la muralla +X.
		TNBeachTrapKit::AddSandRamp(B, &Hulls, FTransform(FRotator(0.0, 180.0, 0.0), FVector(L.HX + ExitRamp, L.ExitY, 0.0)), ExitRamp, ExitW * 0.5 + 40.0, 0.0, L.UpZ, -30.0);

		// Columnas de la sala de la entrada.
		const double RoomA = L.XA - L.IX0;
		const FVector2D PillarAt[3] = { FVector2D(L.IX0 + 0.3 * RoomA, L.IY0 + 0.58 * IWid), FVector2D(L.IX0 + 0.62 * RoomA, L.IY0 + 0.22 * IWid),
			FVector2D(L.IX0 + 0.78 * RoomA, L.IY0 + 0.72 * IWid) };
		for (const FVector2D& P : PillarAt)
		{
			// Pieza de arte: centro de la base sobre el suelo, ejes del castillo.
			TNArt::FPieceScope Piece(Log, TN_ART("Beach.Dungeon.Column"), TNArt::PiecePivot(FVector(P, FloorZ)), { &B, &Decor });
			AddPillar(B, Decor, Hulls, P, 90.0, WallTop - 220.0);
		}

		// Torres en las esquinas.
		const double Inset = L.TowerR * 0.4;
		int32 Variant = static_cast<int32>(Seed % 7u);
		for (const double Sx : { -1.0, 1.0 })
		{
			for (const double Sy : { -1.0, 1.0 })
			{
				// Pieza de arte: centro de la base a ras de suelo, +X hacia fuera (en diagonal); escala 1 = radio 240.
				const FVector2D TowerAt(Sx * (L.HX - Inset), Sy * (L.HY - Inset));
				TNArt::FPieceScope Piece(Log, TN_ART("Beach.Dungeon.Tower"),
					TNArt::PiecePivot(FVector(TowerAt, 0.0), FMath::RadiansToDegrees(FMath::Atan2(Sy, Sx)), FVector(L.TowerR / 240.0, L.TowerR / 240.0, 1.0)), { &B, &Decor });
				AddTower(B, Decor, Hulls, TowerAt, L.TowerR, Variant++);
			}
		}

		// Adornos: concha grande sobre la entrada, estrellas y conchas en las caras de fuera, guijarros por dentro. Conchas y
		// estrellas, como las de las fortalezas: centro, +X hacia fuera de la pared, escala 1 = 50 de radio.
		{
			const FVector ShellAt(-L.HX - 3.0, L.EntY, FloorZ + EntryH + 70.0);
			TNArt::FPieceScope Piece(Log, TN_ART("Beach.SandCastle.Shell"), TNArt::PiecePivot(ShellAt, 180.0, FVector(110.0 / 50.0)), { &Decor });
			TNPlaygroundKit::AddShellFan(Decor, ShellAt, -FVector::ForwardVector, FVector::UpVector, 110.0, TNBeachTrapKit::ShellTone(static_cast<int32>(Seed % 5u)));
		}
		// Aberturas de cada muralla de fuera, para no pegar adornos donde no hay pared (entrada, salida, ventanas y el hueco de encima).
		const TArray<FOpening> SideOpenings[4] = { { Entry }, { Exit, SeaA, SeaB }, { LowA, HighC }, { LowA, HighD, HighC } };
		for (int32 d = 0; d < 14; ++d)
		{
			const int32 Side = d % 4;
			const double U = TNPlaygroundKit::Hash01(d, 1, Seed);
			const double Z = FMath::Lerp(160.0, WallTop - 120.0, TNPlaygroundKit::Hash01(d, 2, Seed));
			FVector At;
			FVector Normal;
			switch (Side)
			{
			case 0: At = FVector(-L.HX - 3.0, FMath::Lerp(-L.HY + 400.0, L.HY - 400.0, U), Z); Normal = -FVector::ForwardVector; break;
			case 1: At = FVector(L.HX + 3.0, FMath::Lerp(-L.HY + 400.0, L.HY - 400.0, U), Z); Normal = FVector::ForwardVector; break;
			case 2: At = FVector(FMath::Lerp(-L.HX + 400.0, L.HX - 400.0, U), L.HY + 3.0, Z); Normal = FVector::RightVector; break;
			default: At = FVector(FMath::Lerp(-L.HX + 400.0, L.HX - 400.0, U), -L.HY - 3.0, Z); Normal = -FVector::RightVector; break;
			}
			const double Along = Side < 2 ? At.Y : At.X;
			const double Reach = d % 3 == 0 ? 55.0 : 48.0;
			bool bFloating = false;
			for (const FOpening& Op : SideOpenings[Side])
			{
				// El adorno entero (radio Reach) tiene que quedar fuera de la abertura, y también de la concha grande de encima de la entrada.
				const bool bBeside = FMath::Abs(Along - Op.Center) > 0.5 * Op.Width + Reach;
				const bool bBelowOrAbove = Z + Reach < Op.Bottom || Z - Reach > Op.Top;
				if (!bBeside && !bBelowOrAbove)
				{
					bFloating = true;
					break;
				}
			}
			if (Side == 0 && FMath::Abs(Along - L.EntY) < 110.0 + Reach && Z - Reach < FloorZ + EntryH + 70.0 + 110.0)
			{
				bFloating = true;
			}
			if (bFloating)
			{
				continue;
			}
			const double NormalYaw = FMath::RadiansToDegrees(FMath::Atan2(Normal.Y, Normal.X));
			if (d % 3 == 0)
			{
				TNArt::FPieceScope Piece(Log, TN_ART("Beach.SandCastle.Starfish"), TNArt::PiecePivot(At, NormalYaw, FVector(55.0 / 50.0)), { &Decor });
				TNPlaygroundKit::AddStarfish(Decor, At, Normal, FVector::UpVector, 55.0, 5.0, TNPlaygroundKit::Rgb(0xFF8A70));
			}
			else
			{
				TNArt::FPieceScope Piece(Log, TN_ART("Beach.SandCastle.Shell"), TNArt::PiecePivot(At, NormalYaw, FVector(48.0 / 50.0)), { &Decor });
				TNPlaygroundKit::AddShellFan(Decor, At, Normal, FVector::UpVector, 48.0, TNBeachTrapKit::ShellTone(d));
			}
		}
		for (int32 p = 0; p < 10; ++p)
		{
			const double Px = FMath::Lerp(L.IX0 + 80.0, L.XA - 80.0, TNPlaygroundKit::Hash01(p, 5, Seed));
			const double Py = FMath::Lerp(L.IY0 + 80.0, L.IY1 - 80.0, TNPlaygroundKit::Hash01(p, 6, Seed));
			TNBeachTrapKit::AddPebble(Decor, FVector(Px, Py, FloorZ + 2.0), FMath::Lerp(10.0, 22.0, TNPlaygroundKit::Hash01(p, 7, Seed)), Seed + static_cast<uint32>(p),
				TNBeachTrapKit::RockTone(p));
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachSandDungeon
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachSandDungeon::ATN_BeachSandDungeon()
{
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(1.f);

	CastleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CastleMesh"));
	CastleMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(CastleMesh);

	DecorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DecorMesh"));
	DecorMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(DecorMesh);

	// Murallas, suelos y torres: también paran la cámara (dentro se ve la sala, no a través de las paredes).
	CastleCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CastleCollision"));
	CastleCollision->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureSolid(CastleCollision, true);
}

void ATN_BeachSandDungeon::ApplySpec()
{
	using namespace TNBeachDungeonDetail;
	// Por debajo de 0,7 no caben las salas (el contrato reparte de 0,7 a 1,4).
	const double Fit = TNBeachTrapKit::FitRadius(ETNBeachElement::SandDungeon, FMath::Clamp(Spec.SizeScale, 0.7f, 1.4f));
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 71u);
	const FLayout Plan = MakeLayout(Fit);

	TNBeachTrapKit::FBuffers Castle;
	TNBeachTrapKit::FBuffers Decor;
	TNBeachTrapKit::FHulls Hulls;
	// Piezas que Arte puede sustituir (Docs/Arte_Assets.md); la colisión es la de los cascos.
	TNArt::FPieceLog Log(TEXT("Dungeon"));
	BuildCastle(Castle, Decor, Hulls, Plan, Seed, Log);
	TNBeachTrapKit::SetMeshWithPieces(CastleMesh, this, Castle, Log);
	TNBeachTrapKit::SetMeshWithPieces(DecorMesh, this, Decor, Log);
	TNArt::SpawnPieceArt(CastleMesh, Log);
	CastleCollision->SetCollisionConvexMeshes(Hulls);

	// Sitios de las piezas de dentro.
	GateWidth = GateW;
	CorridorWidth = CorrW;
	CorridorLength = Plan.XS0 - Plan.XAw;
	RoomAShort = FMath::Min(Plan.XA - Plan.IX0, Plan.IY1 - Plan.IY0);
	const double CorrY = Plan.YC0 + CorrW * 0.5;
	GateAt = FVector(0.5 * (Plan.XA + Plan.XAw), CorrY, FloorZ);
	SeaweedAt = FVector(Plan.XAw + 0.5 * CorridorLength, CorrY, FloorZ);
	UrchinAt = FVector(Plan.XS0 - 170.0, CorrY, FloorZ);
	CrabAt = FVector(0.5 * (Plan.IX0 + Plan.XA), 0.5 * (Plan.IY0 + Plan.IY1), FloorZ);
	// Premio de arriba, en la terraza (suelo macizo a UpZ entre el muro de dentro y la sala de las ventanas; su borde sobre el
	// pasillo lleva un pretil de 60 cm). La catapulta a lo largo de X, a media terraza y con su brazo (de -4,3 a +4,7 m de su
	// pie, el más corto) entre el muro de dentro y la sala de las ventanas, que se le abre delante: lanza por encima de la
	// muralla +X, a más de 25 m. El cofre, en el cuarto de -Y, lejos del cartel de la catapulta.
	const double TerraceY0 = Plan.IY0;
	const double TerraceY1 = Plan.YC0 - 60.0;
	CatapultAt = FVector(Plan.XAw + 600.0, 0.5 * (TerraceY0 + TerraceY1), Plan.UpZ);
	ChestAt = FVector(Plan.XAw + 450.0, TerraceY0 + 450.0, Plan.UpZ);
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Castillo con salas %s: %.0f x %.0f cm, pasillo de %.0f, %d cascos."), *GetName(), 2.0 * Plan.HX, 2.0 * Plan.HY,
		CorridorLength, Hulls.Num());
}

void ATN_BeachSandDungeon::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		SpawnChildren();
	}
}

void ATN_BeachSandDungeon::SpawnChildren()
{
	if (bChildrenSpawned)
	{
		return;
	}
	bChildrenSpawned = true;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FTransform ActorXf = GetActorTransform();
	const uint32 BaseSeed = static_cast<uint32>(Spec.Seed) * 31u;
	const auto SpawnChild = [this, World, &ActorXf, BaseSeed](ETNBeachElement Kind, const FVector& LocalAt, float Size, float Width, uint32 Salt)
	{
		FTNBeachElementSpec ChildSpec;
		ChildSpec.Element = Kind;
		ChildSpec.Seed = static_cast<int32>(BaseSeed + Salt);
		ChildSpec.SizeScale = Size;
		ChildSpec.Extent = Width;
		const FTransform ChildXf(ActorXf.GetRotation(), ActorXf.TransformPosition(LocalAt));
		if (ATN_BeachElement* Child = ATN_BeachElement::SpawnElement(World, ChildXf, ChildSpec))
		{
			SpawnedPieces.Add(Child);
		}
	};
	// Puerta de conchas desnuda en el hueco del muro de dentro y algas en el pasillo (clases de esta misma parte).
	SpawnChild(ETNBeachElement::ShellGate, GateAt, 1.f, static_cast<float>(GateWidth), 7u);
	const float WeedSize = static_cast<float>(FMath::Clamp(CorridorLength * 0.3 / TNBeach::FootprintRadius(ETNBeachElement::Seaweed), 0.3, 0.6));
	SpawnChild(ETNBeachElement::Seaweed, SeaweedAt, WeedSize, static_cast<float>(CorridorWidth - 40.0), 11u);
	// Enemigos pequeños (bSpawnEnemiesInside) solo si ya existen sus clases (los hace otra parte): sin avisos en el log si no.
	if (bSpawnEnemiesInside && FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Tortunabo.%s"), TNBeach::ClassNameOf(ETNBeachElement::SeaUrchin))))
	{
		SpawnChild(ETNBeachElement::SeaUrchin, UrchinAt, 0.35f, 0.f, 13u);
	}
	if (bSpawnEnemiesInside && RoomAShort >= 1600.0
		&& FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Tortunabo.%s"), TNBeach::ClassNameOf(ETNBeachElement::GiantCrab))))
	{
		SpawnChild(ETNBeachElement::GiantCrab, CrabAt, 0.4f, 0.f, 17u);
	}
	// Arriba, en la terraza: la catapulta potenciada y el cofre de cima (si no existen sus clases, no se crean y se avisa).
	TArray<ATN_BeachElement*> Prizes;
	TNBeachCastlePrizes::SpawnSummit(World, FTransform(ActorXf.GetRotation(), ActorXf.TransformPosition(CatapultAt)), TNBeachCastlePrizes::CatapultSeedOf(Spec.Seed),
		FTransform(ActorXf.GetRotation(), ActorXf.TransformPosition(ChestAt)), TNBeachCastlePrizes::ChestSeedOf(Spec.Seed), Prizes);
	for (ATN_BeachElement* Prize : Prizes)
	{
		SpawnedPieces.Add(Prize);
	}
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Castillo con salas %s: %d piezas dentro."), *GetName(), SpawnedPieces.Num());
}

void ATN_BeachSandDungeon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		for (const TWeakObjectPtr<ATN_BeachElement>& Child : SpawnedPieces)
		{
			if (ATN_BeachElement* Piece = Child.Get())
			{
				Piece->Destroy();
			}
		}
	}
	SpawnedPieces.Reset();
	Super::EndPlay(EndPlayReason);
}
