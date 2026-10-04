#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapLayout.h"

/**
 * Camino fino: geometría dentro de cada módulo, anchura, perfil de alturas y ramas.
 *
 * Dentro de un módulo el camino lo traza un "caminante" con inercia de rumbo: cada
 * paso de 6 m evalúa unos pocos giros posibles y puntúa alineación con un rumbo
 * deseado que oscila con ruido alrededor de la dirección al portal de salida (el
 * meandro de un río), repulsión del borde del módulo y de su propio recorrido, y
 * una atracción a la salida que crece al agotarse el presupuesto de longitud.
 * Luego se suaviza (Chaikin) y se remuestrea a espaciado fijo.
 */

namespace TNProcMap
{
	namespace PathDetail
	{
		struct FWalkInput
		{
			int32 Module = INDEX_NONE;
			FVector2D Entry = FVector2D::ZeroVector;
			/** Dirección de entrada, apuntando HACIA DENTRO del módulo. */
			FVector2D EntryDir = FVector2D(0.0, 1.0);
			FVector2D Exit = FVector2D::ZeroVector;
			/** Dirección de salida, apuntando HACIA FUERA del módulo. */
			FVector2D ExitDir = FVector2D(0.0, 1.0);
			double TargetLength = 40000.0;
			double Margin = 3000.0;
			uint32 NoiseSeed = 0;
			double MeanderAmp = 1.2;
			double Wavelength = 16000.0;
			/** Si true, el final puede salir del módulo (tramo de costa). */
			bool bFreeExit = false;
			/** Escala de las medidas fijas del caminante (pasos, tramos rectos, márgenes); 1 en el Coop. */
			double Scale = 1.0;
		};

		constexpr double WalkStep = 600.0;
		constexpr double PortalRelax = 5000.0;
		constexpr double LeadIn = 2500.0;
		constexpr double LeadOut = 3000.0;

		inline double RelaxFactor(const FWalkInput& In, const FVector2D& P)
		{
			const double D = FMath::Min(FVector2D::Distance(P, In.Entry), FVector2D::Distance(P, In.Exit));
			return Saturate(D / (PortalRelax * In.Scale));
		}

		inline bool Walk(const FLayout& L, const FWalkInput& In, TArray<FVector2D>& Out)
		{
			Out.Reset();
			const FVector2D PostEntry = In.Entry + In.EntryDir * (LeadIn * In.Scale);
			const FVector2D PreExit = In.Exit - In.ExitDir * (LeadOut * In.Scale);
			Out.Add(In.Entry);
			Out.Add(PostEntry);

			FVector2D Pos = PostEntry;
			double Heading = AngleOf(In.EntryDir);
			double Traveled = LeadIn * In.Scale;
			// Fase del meandro: el rumbo deseado oscila como un río (sinuosidad ≈ 1/J0(Amp)).
			double Phase = static_cast<double>(In.NoiseSeed % 6283u) / 1000.0;
			const double MaxTurn = FMath::DegreesToRadians(16.0);
			const double Step = WalkStep * In.Scale;
			const double SelfClear = FMath::Max(5200.0 * In.Scale, In.Margin * 1.7);
			const int32 SkipRecent = FMath::CeilToInt(SelfClear * 1.6 / Step);
			const int32 MaxSteps = FMath::CeilToInt(In.TargetLength * 2.6 / Step) + 60;

			for (int32 It = 0; It < MaxSteps; ++It)
			{
				const FVector2D ToT = PreExit - Pos;
				const double DistT = ToT.Size();
				if (DistT < Step * 1.5)
				{
					Out.Add(PreExit);
					Out.Add(In.Exit);
					return true;
				}

				const double Remaining = In.TargetLength - LeadOut * In.Scale - Traveled;
				// Amplitud del meandro: se apaga cuando el presupuesto no da para más que ir recto.
				const double Slack = Saturate((Remaining - DistT * 1.1) / FMath::Max(1.0, 0.18 * In.TargetLength));
				const double Amp = In.MeanderAmp * Slack * (0.8 + 0.35 * Noise1(In.NoiseSeed + 11u, Traveled / (In.Wavelength * 2.3)));
				const double AngT = AngleOf(ToT);
				const double Wave = FMath::Sin(Phase) + 0.3 * Noise1(In.NoiseSeed, Traveled / (9000.0 * In.Scale));
				const double Desired = AngT + Amp * Wave;
				const double ExitPull = 0.2 + 2.8 * (1.0 - Slack);
				// La longitud de onda varía con ruido para que no quede un zigzag regular.
				Phase += TwoPi * Step / (In.Wavelength * (0.8 + 0.4 * (0.5 + 0.5 * Noise1(In.NoiseSeed + 29u, Traveled / 30000.0))));

				double BestScore = -1e300;
				double BestHeading = Heading;
				bool bFound = false;
				for (int32 k = -3; k <= 3; ++k)
				{
					const double Phi = Heading + MaxTurn * (static_cast<double>(k) / 3.0);
					const FVector2D Next = Pos + DirFromAngle(Phi) * Step;
					const double Relax = RelaxFactor(In, Next);

					const bool bInside = L.ModuleAt(Next) == In.Module;
					if (!bInside && Relax > 0.35 && !In.bFreeExit) { continue; }

					double Score = FMath::Cos(WrapAngle(Phi - Desired)) + ExitPull * FMath::Cos(WrapAngle(Phi - AngT));

					const double Required = In.Margin * Relax;
					const double Bd = bInside ? L.BorderDistAt(Next) : 0.0;
					if (Bd < Required)
					{
						if (Bd < Required * 0.35 && Relax > 0.6 && !In.bFreeExit) { continue; }
						Score -= 3.0 * (Required - Bd) / 1000.0;
					}

					double MinSelf = 1e300;
					for (int32 j = 0; j < Out.Num() - SkipRecent; ++j)
					{
						MinSelf = FMath::Min(MinSelf, FVector2D::DistSquared(Next, Out[j]));
					}
					MinSelf = FMath::Sqrt(MinSelf);
					if (MinSelf < SelfClear)
					{
						if (MinSelf < SelfClear * 0.55) { continue; }
						Score -= 4.0 * (SelfClear - MinSelf) / 1000.0;
					}

					if (Score > BestScore)
					{
						BestScore = Score;
						BestHeading = Phi;
						bFound = true;
					}
				}
				if (!bFound) { return false; }

				Heading = BestHeading;
				Pos = Pos + DirFromAngle(Heading) * Step;
				Out.Add(Pos);
				Traveled += Step;
			}
			return false;
		}

		/** Comprueba que un camino de módulo queda dentro, lejos del borde y sin tocarse a sí mismo. */
		inline bool ValidateModulePath(const FLayout& L, const FWalkInput& In, const TArray<FVector2D>& Pts)
		{
			const double MinBorder = In.Margin * 0.45;
			double S = 0.0;
			const double Total = PolylineLength(Pts);
			TArray<double> Acc;
			Acc.SetNum(Pts.Num());
			for (int32 i = 0; i < Pts.Num(); ++i)
			{
				if (i > 0) { S += FVector2D::Distance(Pts[i - 1], Pts[i]); }
				Acc[i] = S;
				const bool bNearEnd = S < 3500.0 || Total - S < 3500.0;
				if (bNearEnd) { continue; }
				if (!In.bFreeExit || Total - S > 9000.0 * In.Scale)
				{
					if (L.ModuleAt(Pts[i]) != In.Module) { return false; }
					if (L.BorderDistAt(Pts[i]) < MinBorder) { return false; }
				}
			}
			for (int32 i = 0; i < Pts.Num(); i += 2)
			{
				for (int32 j = i + 2; j < Pts.Num(); j += 2)
				{
					if (Acc[j] - Acc[i] < 9000.0 * In.Scale) { continue; }
					if (FVector2D::DistSquared(Pts[i], Pts[j]) < 3600.0 * 3600.0) { return false; }
				}
			}
			return true;
		}

		inline TArray<FVector2D> CatmullRom(const TArray<FVector2D>& Ctrl, int32 PerSegment)
		{
			TArray<FVector2D> Out;
			if (Ctrl.Num() < 2) { return Ctrl; }
			for (int32 i = 0; i < Ctrl.Num() - 1; ++i)
			{
				const FVector2D P0 = Ctrl[FMath::Max(0, i - 1)];
				const FVector2D P1 = Ctrl[i];
				const FVector2D P2 = Ctrl[i + 1];
				const FVector2D P3 = Ctrl[FMath::Min(Ctrl.Num() - 1, i + 2)];
				for (int32 k = 0; k < PerSegment; ++k)
				{
					const double T = static_cast<double>(k) / PerSegment;
					const double T2 = T * T;
					const double T3 = T2 * T;
					Out.Add((P1 * 2.0 + (P2 - P0) * T + (P0 * 2.0 - P1 * 5.0 + P2 * 4.0 - P3) * T2 + (P1 * 3.0 - P0 - P2 * 3.0 + P3) * T3) * 0.5);
				}
			}
			Out.Add(Ctrl.Last());
			return Out;
		}

		/** Curva de reserva por el interior del módulo cuando el caminante falla. */
		inline TArray<FVector2D> FallbackCurve(const FLayout& L, const FWalkInput& In, bool bThroughInterior)
		{
			TArray<FVector2D> Ctrl;
			Ctrl.Add(In.Entry);
			Ctrl.Add(In.Entry + In.EntryDir * (LeadIn * In.Scale));
			if (bThroughInterior)
			{
				// Punto interior con más holgura cerca del punto medio entre portales.
				const FVector2D Mid = (In.Entry + In.Exit) * 0.5;
				FVector2D Best = L.Modules[In.Module].Centroid;
				double BestScore = -1e300;
				const FModule& M = L.Modules[In.Module];
				for (int32 y = 0; y < L.RasterH; y += 3)
				{
					for (int32 x = 0; x < L.RasterW; x += 3)
					{
						if (L.ModuleOfCell[L.CellIndex(x, y)] != M.Id) { continue; }
						const FVector2D C = L.CellCenter(x, y);
						const double Score = L.BorderDist[L.CellIndex(x, y)] - 0.35 * FVector2D::Distance(C, Mid);
						if (Score > BestScore) { BestScore = Score; Best = C; }
					}
				}
				Ctrl.Add(Best);
			}
			Ctrl.Add(In.Exit - In.ExitDir * (LeadOut * In.Scale));
			Ctrl.Add(In.Exit);
			return CatmullRom(Ctrl, 12);
		}

		/** Pasada casi recta (módulos de cruce): cuerda con un ligero arco. */
		inline TArray<FVector2D> ChordCurve(const FWalkInput& In, FRng& Rng)
		{
			const FVector2D A = In.Entry + In.EntryDir * (LeadIn * In.Scale);
			const FVector2D B = In.Exit - In.ExitDir * (LeadOut * In.Scale);
			const FVector2D Mid = (A + B) * 0.5 + LeftNormal((B - A).GetSafeNormal()) * (FVector2D::Distance(A, B) * Rng.Range(-0.05, 0.05));
			TArray<FVector2D> Ctrl;
			Ctrl.Add(In.Entry);
			Ctrl.Add(A);
			Ctrl.Add(Mid);
			Ctrl.Add(B);
			Ctrl.Add(In.Exit);
			return CatmullRom(Ctrl, 10);
		}

		/** Posición del camino principal a una distancia S (interpolada). */
		inline FVector2D MainPointAt(const TArray<FPathSample>& Main, double S, FVector2D* OutDir = nullptr, int32* OutIndex = nullptr)
		{
			if (Main.Num() == 0) { return FVector2D::ZeroVector; }
			int32 Lo = 0, Hi = Main.Num() - 1;
			if (S <= Main[0].S) { if (OutDir) { *OutDir = Main[0].Dir; } if (OutIndex) { *OutIndex = 0; } return Main[0].P; }
			if (S >= Main[Hi].S) { if (OutDir) { *OutDir = Main[Hi].Dir; } if (OutIndex) { *OutIndex = Hi; } return Main[Hi].P; }
			while (Hi - Lo > 1)
			{
				const int32 Mid = (Lo + Hi) / 2;
				if (Main[Mid].S <= S) { Lo = Mid; } else { Hi = Mid; }
			}
			const double T = (S - Main[Lo].S) / FMath::Max(1e-6, Main[Hi].S - Main[Lo].S);
			if (OutDir) { *OutDir = (Main[Lo].Dir * (1.0 - T) + Main[Hi].Dir * T).GetSafeNormal(); }
			if (OutIndex) { *OutIndex = Lo; }
			return Main[Lo].P + (Main[Hi].P - Main[Lo].P) * T;
		}

		/** Recalcula S y Dir de una lista de muestras. */
		inline void FinalizeSamples(TArray<FPathSample>& Samples)
		{
			double S = 0.0;
			for (int32 i = 0; i < Samples.Num(); ++i)
			{
				if (i > 0) { S += FVector2D::Distance(Samples[i - 1].P, Samples[i].P); }
				Samples[i].S = S;
			}
			for (int32 i = 0; i < Samples.Num(); ++i)
			{
				const FVector2D A = Samples[FMath::Max(0, i - 2)].P;
				const FVector2D B = Samples[FMath::Min(Samples.Num() - 1, i + 2)].P;
				const FVector2D D = (B - A).GetSafeNormal();
				Samples[i].Dir = D.IsNearlyZero() ? FVector2D(0.0, 1.0) : D;
			}
		}

		/** Envolvente inferior con pendiente máxima K (cm por cm), in-place en [From, To]. */
		inline void SlopeLimit(TArray<double>& Z, const TArray<FPathSample>& Samples, int32 From, int32 To, double K)
		{
			for (int32 i = From + 1; i <= To; ++i)
			{
				const double Ds = Samples[i].S - Samples[i - 1].S;
				Z[i] = FMath::Min(Z[i], Z[i - 1] + K * Ds);
			}
			for (int32 i = To - 1; i >= From; --i)
			{
				const double Ds = Samples[i + 1].S - Samples[i].S;
				Z[i] = FMath::Min(Z[i], Z[i + 1] + K * Ds);
			}
		}

		/** Rejilla de cubos de muestras para consultas de distancia al camino principal. */
		struct FSampleGrid
		{
			double Cell = 4000.0;
			int32 W = 0, H = 0;
			double Origin = -20000.0;
			TArray<TArray<int32>> Buckets;
			const TArray<FPathSample>* Samples = nullptr;

			void Build(const TArray<FPathSample>& In, double WorldSize)
			{
				Samples = &In;
				W = H = FMath::CeilToInt((WorldSize - 2.0 * Origin) / Cell) + 1;
				Buckets.Reset();
				Buckets.SetNum(W * H);
				for (int32 i = 0; i < In.Num(); ++i)
				{
					const int32 X = FMath::Clamp(FMath::FloorToInt((In[i].P.X - Origin) / Cell), 0, W - 1);
					const int32 Y = FMath::Clamp(FMath::FloorToInt((In[i].P.Y - Origin) / Cell), 0, H - 1);
					Buckets[Y * W + X].Add(i);
				}
			}

			/**
			 * Muestra más cercana dentro de Radius (INDEX_NONE si ninguna). Con MinAlong > 0
			 * ignora las que están a menos de MinAlong de AlongS medido por el camino.
			 */
			int32 Nearest(const FVector2D& P, double Radius, double& OutDist, double AlongS = 0.0, double MinAlong = 0.0) const
			{
				OutDist = 1e300;
				int32 Best = INDEX_NONE;
				const int32 R = FMath::CeilToInt(Radius / Cell);
				const int32 CX = FMath::FloorToInt((P.X - Origin) / Cell);
				const int32 CY = FMath::FloorToInt((P.Y - Origin) / Cell);
				for (int32 Y = FMath::Max(0, CY - R); Y <= FMath::Min(H - 1, CY + R); ++Y)
				{
					for (int32 X = FMath::Max(0, CX - R); X <= FMath::Min(W - 1, CX + R); ++X)
					{
						for (const int32 Idx : Buckets[Y * W + X])
						{
							if (MinAlong > 0.0 && FMath::Abs((*Samples)[Idx].S - AlongS) < MinAlong) { continue; }
							const double D = FVector2D::Distance(P, (*Samples)[Idx].P);
							if (D < OutDist) { OutDist = D; Best = Idx; }
						}
					}
				}
				return OutDist <= Radius ? Best : INDEX_NONE;
			}
		};

		/**
		 * Anchura del suelo a cota de cima en una muestra a distancia D del centro de una torre:
		 * amplio en el centro (el géiser aterriza ahí, lejos del talud) y sin salirse del pilar,
		 * para no enterrar el pie de la subida (géiser) ni el arranque del tobogán.
		 */
		inline double TowerTopWidth(const FGenParams& P, double D)
		{
			return FMath::Clamp(2.0 * (P.TowerRadius + 150.0 - D), 300.0, P.TowerRadius * 1.6);
		}

		/** Tramos por anchura, de desfiladero a explanada. */
		enum class EWidthKind : uint8 { Narrow, Tight, Normal, Wide, Open };
		constexpr int32 NumWidthKinds = 5;

		/**
		 * Reparto de tramos por bioma: cañones en desierto y roca, arenales abiertos en la
		 * playa. Pocos tramos intermedios: o estrecho o amplio, que se note el contraste.
		 */
		inline void WidthKindWeights(ETNProcBiome Biome, double NarrowChance, double (&Out)[NumWidthKinds])
		{
			Out[0] = NarrowChance * 1.3; Out[1] = 0.22; Out[2] = 0.12; Out[3] = 0.24; Out[4] = 0.14;
			switch (Biome)
			{
				case ETNProcBiome::Desert:   Out[0] *= 1.6; Out[1] *= 1.3; Out[4] *= 0.8; break;
				case ETNProcBiome::Rocky:    Out[0] *= 1.8; Out[1] *= 1.4; Out[3] *= 0.7; break;
				case ETNProcBiome::Beach:    Out[0] *= 0.4; Out[3] *= 1.5; Out[4] *= 2.2; break;
				case ETNProcBiome::Jungle:   Out[1] *= 1.5; Out[4] *= 0.8; break;
				case ETNProcBiome::Volcanic: Out[0] *= 1.2; Out[4] *= 1.4; break;
				case ETNProcBiome::Human:    Out[2] *= 1.4; Out[3] *= 1.3; break;
				default: break;
			}
		}

		/** Anchura (U en [0,1]) y longitud de cada tipo de tramo. */
		inline double WidthOfKind(const FGenParams& P, EWidthKind Kind, double U)
		{
			const double Mn = P.PathWidthMin;
			const double Mx = FMath::Max(P.PathWidthMax, Mn * 4.0);
			switch (Kind)
			{
				case EWidthKind::Narrow: return LerpD(Mn * 0.9, Mn * 1.3, U);
				case EWidthKind::Tight:  return LerpD(Mn * 1.6, Mn * 2.6, U);
				case EWidthKind::Normal: return LerpD(Mn * 2.8, FMath::Max(Mn * 3.2, Mx * 0.5), U);
				case EWidthKind::Wide:   return LerpD(Mx * 0.6, Mx, U);
				default:                 return LerpD(Mx * 1.15, Mx * 1.7, U);
			}
		}

		inline double LengthOfKind(EWidthKind Kind, double U)
		{
			switch (Kind)
			{
				case EWidthKind::Narrow: return LerpD(3000.0, 8000.0, U);
				case EWidthKind::Tight:  return LerpD(3500.0, 10000.0, U);
				case EWidthKind::Normal: return LerpD(5000.0, 15000.0, U);
				case EWidthKind::Wide:   return LerpD(4500.0, 13000.0, U);
				default:                 return LerpD(4500.0, 11000.0, U);
			}
		}

		/**
		 * Anchura objetivo por tramos a lo largo de una polilínea, antes de los recortes por
		 * holgura: secuencia de tipos sin repetir, transiciones largas (15-50 m) y ninguna
		 * anchura constante (deriva lenta dentro del tramo y bordes que respiran), para que
		 * el cauce se abra y se cierre como uno natural en vez de a escalones.
		 */
		inline TArray<double> SectionWidths(const TArray<FPathSample>& S, const FGenParams& P, FRng& Rng, uint32 WSeed)
		{
			TArray<double> W;
			const int32 NumS = S.Num();
			W.SetNum(NumS);
			if (NumS == 0) { return W; }

			struct FSection { double S0 = 0.0; double W = 0.0; double Len = 0.0; };
			TArray<FSection> Sections;
			{
				const double Total = S.Last().S;
				double Cursor = 0.0;
				int32 Prev = INDEX_NONE;
				while (Cursor <= Total)
				{
					int32 Near = 0;
					MainPointAt(S, Cursor, nullptr, &Near);
					double Wt[NumWidthKinds];
					WidthKindWeights(S[Near].Biome, P.NarrowChance, Wt);
					if (Prev != INDEX_NONE) { Wt[Prev] = 0.0; }
					double Sum = 0.0;
					for (const double V : Wt) { Sum += V; }
					double Pick = Rng.Unit() * Sum;
					int32 Kind = NumWidthKinds - 1;
					for (int32 k = 0; k < NumWidthKinds; ++k)
					{
						if (Pick < Wt[k]) { Kind = k; break; }
						Pick -= Wt[k];
					}
					FSection Sec;
					Sec.S0 = Cursor;
					Sec.W = WidthOfKind(P, static_cast<EWidthKind>(Kind), Rng.Unit());
					Sec.Len = LengthOfKind(static_cast<EWidthKind>(Kind), Rng.Unit());
					Sections.Add(Sec);
					Cursor += Sec.Len;
					Prev = Kind;
				}
			}
			auto TransLen = [&Sections](int32 A, int32 B) { return FMath::Clamp(0.5 * FMath::Min(Sections[A].Len, Sections[B].Len), 1500.0, 5000.0); };

			int32 Sec = 0;
			for (int32 i = 0; i < NumS; ++i)
			{
				const FPathSample& Sm = S[i];
				while (Sec + 1 < Sections.Num() && Sections[Sec + 1].S0 <= Sm.S) { ++Sec; }
				double Wi = Sections[Sec].W;
				if (Sec > 0)
				{
					const double T = TransLen(Sec - 1, Sec);
					Wi = LerpD(Sections[Sec - 1].W, Wi, SmoothStep(-0.5 * T, 0.5 * T, Sm.S - Sections[Sec].S0));
				}
				if (Sec + 1 < Sections.Num())
				{
					const double T = TransLen(Sec, Sec + 1);
					Wi = LerpD(Wi, Sections[Sec + 1].W, SmoothStep(-0.5 * T, 0.5 * T, Sm.S - Sections[Sec + 1].S0));
				}
				// Deriva lenta (±22 % a ~50 m) y bordes que respiran (±14 % a ~20 m).
				W[i] = Wi * (1.0 + 0.22 * Fbm1(WSeed + 9u, Sm.S / 5000.0, 2)) * (1.0 + 0.14 * Fbm1(WSeed + 5u, Sm.S / 1800.0, 2));
			}
			return SmoothScalars(W, 4);
		}
	}

	/** Punto de salida (sur) y de llegada (costa norte). */
	inline void ChooseStartAndEnd(FLayout& L, FRng& Rng)
	{
		const FGenParams& P = L.Params;
		const int32 StartModule = L.Route[0].Module;
		const int32 EndModule = L.Route.Last().Module;

		// Salida: celda del módulo inicial con holgura para el claro, lo más al sur posible.
		double BestScore = -1e300;
		FVector2D BestStart = L.Modules[StartModule].Centroid;
		// Holgura de 15 m alrededor del claro; en módulos pequeños (Supervivencia), el 10 % del lado.
		const double Need = P.StartClearingRadius + FMath::Min(1500.0, 0.1 * P.ModuleSize);
		for (int32 y = 0; y < L.RasterH; ++y)
		{
			for (int32 x = 0; x < L.RasterW; ++x)
			{
				const int32 Idx = L.CellIndex(x, y);
				if (L.ModuleOfCell[Idx] != StartModule) { continue; }
				const double Bd = L.BorderDist[Idx];
				const FVector2D C = L.CellCenter(x, y);
				const double Score = (Bd >= Need ? 100000.0 : Bd) - C.Y * 0.6 + Rng.Range(0.0, 300.0);
				if (Score > BestScore) { BestScore = Score; BestStart = C; }
			}
		}
		L.StartPoint = BestStart;

		// Llegada: a 55-80 m de la costa (desde ahí la playa final va recta y se abre hasta el agua; si el
		// módulo no llega tan lejos, junto a la costa), en la parte del módulo final más alejada de sus vecinos.
		FVector2D BestEnd = L.Modules[EndModule].Centroid;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const double Near = Pass == 0 ? FinishDims::EndNear : 1800.0;
			const double Far = Pass == 0 ? FinishDims::EndFar : 5000.0;
			BestScore = -1e300;
			for (int32 y = 0; y < L.RasterH; ++y)
			{
				for (int32 x = 0; x < L.RasterW; ++x)
				{
					const int32 Idx = L.CellIndex(x, y);
					if (L.ModuleOfCell[Idx] != EndModule) { continue; }
					const FVector2D C = L.CellCenter(x, y);
					const double Coast = L.CoastY(C.X);
					if (C.Y < Coast - Far || C.Y > Coast - Near) { continue; }
					const double EdgeX = FMath::Min(C.X, L.WorldSizeX - C.X) - P.MapEdgeClearance;
					const double Score = FMath::Min(static_cast<double>(L.ModuleDist[Idx]), EdgeX) + Rng.Range(0.0, 400.0);
					if (Score > BestScore) { BestScore = Score; BestEnd = C; }
				}
			}
			if (BestScore > -1e299) { break; }
		}
		L.EndPoint = BestEnd;
	}

	/** Traza el camino principal módulo a módulo. */
	inline bool BuildMainPath(FLayout& L, FRng Rng)
	{
		using namespace PathDetail;
		const FGenParams& P = L.Params;
		ChooseStartAndEnd(L, Rng);

		L.Main.Reset();
		const int32 NumSteps = L.Route.Num();
		for (int32 k = 0; k < NumSteps; ++k)
		{
			FRouteStep& Step = L.Route[k];
			FWalkInput In;
			In.Module = Step.Module;
			In.Entry = k == 0 ? L.StartPoint : L.Portals[Step.EntryPortal].Point;
			In.Exit = k == NumSteps - 1 ? L.EndPoint : L.Portals[Step.ExitPortal].Point;
			In.ExitDir = k == NumSteps - 1 ? FVector2D(0.0, 1.0) : L.Portals[Step.ExitPortal].Dir;
			In.EntryDir = k == 0 ? (In.Exit - In.Entry).GetSafeNormal() : L.Portals[Step.EntryPortal].Dir;
			In.bFreeExit = k == NumSteps - 1;
			In.Scale = P.WalkScale;
			In.Margin = 3200.0 * P.WalkScale;
			In.NoiseSeed = P.Seed ^ Hash32(static_cast<uint32>(k) * 2654435761u);
			const double Straight = FVector2D::Distance(In.Entry, In.Exit);
			In.TargetLength = FMath::Max(Straight * P.Sinuosity * Rng.Range(0.85, 1.15), Straight + LeadIn * P.WalkScale + LeadOut * P.WalkScale);
			In.MeanderAmp = Rng.Range(1.2, 1.55);
			// Longitud de onda del meandro grande: una o dos curvas amplias por módulo.
			In.Wavelength = Rng.Range(0.8, 1.4) * P.ModuleSize;

			TArray<FVector2D> Pts;
			const bool bCrossModule = Step.CrossingIndex != INDEX_NONE;
			bool bOk = false;
			if (bCrossModule)
			{
				Pts = ChordCurve(In, Rng);
				bOk = true;
			}
			else
			{
				for (int32 Attempt = 0; Attempt < 6 && !bOk; ++Attempt)
				{
					FWalkInput Try = In;
					Try.NoiseSeed = In.NoiseSeed + static_cast<uint32>(Attempt) * 7717u;
					Try.MeanderAmp = In.MeanderAmp * (1.0 - 0.15 * Attempt);
					Try.TargetLength = FMath::Max(Straight + LeadIn * P.WalkScale + LeadOut * P.WalkScale, In.TargetLength * (1.0 - 0.1 * Attempt));
					TArray<FVector2D> Raw;
					if (Walk(L, Try, Raw))
					{
						Pts = ResamplePolyline(ChaikinSmooth(Raw, 2), P.SampleSpacing);
						bOk = ValidateModulePath(L, Try, Pts);
					}
				}
				if (!bOk)
				{
					++L.WalkFallbacks;
					Pts = FallbackCurve(L, In, true);
					bOk = ValidateModulePath(L, In, ResamplePolyline(Pts, P.SampleSpacing));
					if (!bOk) { Pts = FallbackCurve(L, In, false); }
				}
			}
			Pts = ResamplePolyline(Pts, P.SampleSpacing);

			Step.FirstSample = L.Main.Num() > 0 ? L.Main.Num() - 1 : 0;
			for (int32 i = (k == 0 ? 0 : 1); i < Pts.Num(); ++i)
			{
				FPathSample Sm;
				Sm.P = Pts[i];
				Sm.Step = k;
				Sm.Module = Step.Module;
				Sm.Biome = L.Modules[Step.Module].Biome;
				L.Main.Add(Sm);
			}
			Step.LastSample = L.Main.Num() - 1;
			L.Main[Step.LastSample].Flags |= (k < NumSteps - 1) ? PathFlags::Portal : PathFlags::None;
		}

		// Playa final: recta hacia el mar desde la llegada, hasta pasada la línea de meta (la meta es
		// entrar en el agua).
		{
			const FVector2D From = L.Main.Last().P;
			const double EndY = FMath::Max(FinishLineY(L) + FinishDims::PastLine, From.Y + 3600.0);
			const int32 Extra = FMath::CeilToInt((EndY - From.Y) / P.SampleSpacing);
			for (int32 i = 1; i <= Extra; ++i)
			{
				FPathSample Sm = L.Main.Last();
				Sm.P = From + FVector2D(0.0, P.SampleSpacing * i);
				Sm.Flags = PathFlags::Shore;
				L.Main.Add(Sm);
			}
			L.Route.Last().LastSample = L.Main.Num() - 1;
		}

		FinalizeSamples(L.Main);
		L.Main[0].Flags |= PathFlags::Start;
		L.Main.Last().Flags |= PathFlags::End;
		for (FPathSample& Sm : L.Main)
		{
			if (FVector2D::Distance(Sm.P, L.StartPoint) < P.StartClearingRadius) { Sm.Flags |= PathFlags::Start; }
		}
		return L.Main.Num() > 4;
	}

	/**
	 * Anchura muy variable por tramos: desfiladeros (3,5-5 m), pasos cerrados, tramos
	 * normales, anchos y explanadas (40-60 m), con transiciones de 8-25 m y bordes que
	 * respiran. Respeta la holgura del módulo y la de otras partes del camino: entre
	 * dos cauces queda siempre un muro (no se funden ni se ataja por ellos).
	 */
	inline void ComputeWidths(FLayout& L, FRng Rng)
	{
		using namespace PathDetail;
		const FGenParams& P = L.Params;
		const int32 NumS = L.Main.Num();
		if (NumS == 0) { return; }
		TArray<double> W = SectionWidths(L.Main, P, Rng, P.Seed ^ 0xA11CEu);

		// Holgura con el borde del módulo y con otras partes del camino (muro de al menos 18 m).
		// El tope se aplica con un mínimo suave y ondulado para que no deje mesetas de anchura
		// constante con esquinas (se verían como escalones en el cauce).
		FSampleGrid Grid;
		Grid.Build(L.Main, L.MaxExtent());
		const uint32 CapSeed = P.Seed ^ 0xCA9u;
		for (int32 i = 0; i < NumS; ++i)
		{
			const FPathSample& Sm = L.Main[i];
			const double Bd = L.BorderDistAt(Sm.P);
			double Cap = FMath::Max(P.PathWidthMin, 2.0 * (Bd - 1800.0));
			double DSelf = 0.0;
			if (Grid.Nearest(Sm.P, 12000.0, DSelf, Sm.S, FMath::Max(9000.0, 2.0 * W[i])) != INDEX_NONE)
			{
				Cap = FMath::Min(Cap, FMath::Max(P.PathWidthMin, DSelf - 1800.0));
			}
			Cap *= 0.86 + 0.14 * (0.5 + 0.5 * Noise1(CapSeed, Sm.S / 2200.0));
			W[i] = FMath::Max(330.0, SoftMinD(W[i], Cap, 250.0));
		}
		// Cerca de portales: anchura del portal.
		for (const FRouteStep& Step : L.Route)
		{
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const int32 PortalIdx = Side == 0 ? Step.EntryPortal : Step.ExitPortal;
				if (PortalIdx == INDEX_NONE) { continue; }
				const FPortal& Portal = L.Portals[PortalIdx];
				for (int32 i = Step.FirstSample; i <= Step.LastSample && i < L.Main.Num(); ++i)
				{
					const double D = FVector2D::Distance(L.Main[i].P, Portal.Point);
					if (D < 2500.0) { W[i] = LerpD(Portal.Width, W[i], SmoothStep(700.0, 2500.0, D)); }
				}
			}
		}
		TArray<double> Smoothed = SmoothScalars(W, 3);
		for (int32 i = 0; i < L.Main.Num(); ++i)
		{
			L.Main[i].Width = Smoothed[i];
			if ((L.Main[i].Flags & PathFlags::Start) != 0) { L.Main[i].Width = FMath::Max(L.Main[i].Width, P.StartClearingRadius * 1.2); }
		}

		// Playa final en campana: la llegada (al menos ArrivalWidth) sigue recta FlareStart y luego sus
		// brazos se abren en arco de radio R, tangentes al cauce, hasta la orilla, donde han girado unos
		// 65°; bajo el agua siguen en recta con ese ángulo. La playa no se ve entera de golpe: se
		// descubre al avanzar. (El terreno trata esta recta como una franja, ver StampPathField.)
		int32 FirstShore = INDEX_NONE;
		for (int32 i = 0; i < L.Main.Num(); ++i) { if ((L.Main[i].Flags & PathFlags::Shore) != 0) { FirstShore = i; break; } }
		if (FirstShore > 0)
		{
			const double W0 = FMath::Max(FinishDims::ArrivalWidth, L.Main[FirstShore - 1].Width);
			const double S0 = L.Main[FirstShore].S + FinishDims::FlareStart;
			const double ToWater = FMath::Max(500.0, FinishWaterY(L) - L.Main[FirstShore].P.Y - FinishDims::FlareStart);
			const double R = ToWater / FMath::Sin(FMath::DegreesToRadians(FinishDims::FlareTurnDeg));
			const double Rise = R - FMath::Sqrt(R * R - ToWater * ToWater);
			const double Tangent = FMath::Tan(FMath::DegreesToRadians(FinishDims::FlareTurnDeg));
			for (int32 i = FirstShore - 1; i >= 0; --i)
			{
				const double D = L.Main[FirstShore].S - L.Main[i].S;
				if (D > 2000.0) { break; }
				L.Main[i].Width = FMath::Max(L.Main[i].Width, LerpD(W0, L.Main[i].Width, SmoothStep(0.0, 2000.0, D)));
			}
			for (int32 i = FirstShore; i < L.Main.Num(); ++i)
			{
				const double X = FMath::Max(0.0, L.Main[i].S - S0);
				const double Half = X <= ToWater ? R - FMath::Sqrt(R * R - X * X) : Rise + (X - ToWater) * Tangent;
				L.Main[i].Width = W0 + 2.0 * Half;
			}
		}
	}

	/**
	 * Alturas del camino: nivel de cada módulo + ondulación suave con pendiente
	 * limitada; géiser/tobogán en saltos de nivel grandes; tablero o mesa a cota
	 * colosal en las pasadas altas de los cruces; torres en sus extremos.
	 */
	inline void ComputeZProfile(FLayout& L, FRng Rng)
	{
		using namespace PathDetail;
		const FGenParams& P = L.Params;
		const int32 NumS = L.Main.Num();
		if (NumS == 0) { return; }
		const uint32 ZSeed = P.Seed ^ 0x2E7Au;

		TArray<double> Z;
		Z.SetNum(NumS);
		for (int32 i = 0; i < NumS; ++i)
		{
			const FPathSample& Sm = L.Main[i];
			const bool bWet = IsWetBiome(Sm.Biome);
			// Lomas y hondonadas de ±6,5 m a escala de ~80 m, con detalle de ±1,5 m (en el agua, a ras);
			// la pendiente se limita después.
			Z[i] = L.Modules[Sm.Module].Level
				+ (bWet ? 0.0 : 650.0 * Fbm1(ZSeed, Sm.S / 8000.0, 3) + 150.0 * Fbm1(ZSeed + 3u, Sm.S / 2500.0, 2));
		}

		// Segmentos continuos separados por cortes (cruces colosales y desniveles grandes).
		TArray<int32> Cuts; // índice de la muestra de portal donde hay corte
		TArray<uint8> CutIsUp;
		for (int32 k = 0; k + 1 < L.Route.Num(); ++k)
		{
			const FRouteStep& A = L.Route[k];
			const FRouteStep& B = L.Route[k + 1];
			const int32 PortalSample = A.LastSample;
			if (B.bHigh) { Cuts.Add(PortalSample); CutIsUp.Add(1); continue; }
			if (A.bHigh) { Cuts.Add(PortalSample); CutIsUp.Add(0); continue; }
			const double Delta = L.Modules[B.Module].Level - L.Modules[A.Module].Level;
			if (FMath::Abs(Delta) > P.SmoothTransitionMax)
			{
				Cuts.Add(PortalSample);
				CutIsUp.Add(Delta > 0.0 ? 1 : 0);
			}
		}

		// Ondulación de todo el recorrido: lomas de 0,6-2,2 m cada 50-110 m (el suavizado de ~80 m de
		// abajo deja casi llano lo que hay entre módulos). Se suma después de ese suavizado, con pendiente
		// de la ola <= 0,14 (y el límite general luego); nada en el agua (con transición), al salir ni en
		// la llegada a la playa.
		TArray<double> Wave;
		Wave.SetNumZeroed(NumS);
		{
			TArray<double> Dry;
			Dry.SetNum(NumS);
			for (int32 i = 0; i < NumS; ++i) { Dry[i] = IsWetBiome(L.Main[i].Biome) ? 0.0 : 1.0; }
			Dry = SmoothScalars(Dry, 8);
			const double Total = L.Main.Last().S;
			double Phase = Rng.Range(0.0, TwoPi);
			for (int32 i = 0; i < NumS; ++i)
			{
				const double S = L.Main[i].S;
				const double Lambda = LerpD(5000.0, 11000.0, 0.5 + 0.5 * Noise1(ZSeed + 41u, S / 30000.0));
				if (i > 0) { Phase += TwoPi * (S - L.Main[i - 1].S) / Lambda; }
				const double Amp = FMath::Min(LerpD(60.0, 220.0, 0.5 + 0.5 * Noise1(ZSeed + 43u, S / 45000.0)), 0.14 * Lambda / TwoPi);
				const double Fade = SmoothStep(3000.0, 9000.0, S) * SmoothStep(Total - 1000.0, Total - 16000.0, S);
				Wave[i] = Amp * FMath::Sin(Phase) * Dry[i] * Fade;
			}
		}

		// Suavizado + pendiente limitada por segmento (las pasadas altas se sobrescriben luego).
		{
			int32 SegStart = 0;
			TArray<int32> Bounds = Cuts;
			Bounds.Add(NumS - 1);
			for (const int32 SegEnd : Bounds)
			{
				if (SegEnd <= SegStart) { SegStart = SegEnd; continue; }
				TArray<double> Part;
				for (int32 i = SegStart; i <= SegEnd; ++i) { Part.Add(Z[i]); }
				Part = SmoothScalars(Part, 10);
				// La ola no baja los tramos cercanos al nivel del mar (no crea agua nadable junto a la costa).
				for (int32 i = SegStart; i <= SegEnd; ++i) { Z[i] = Part[i - SegStart] + Wave[i] * SmoothStep(250.0, 900.0, Part[i - SegStart]); }
				SlopeLimit(Z, L.Main, SegStart, SegEnd, P.MaxPathSlope);
				// Subidas redondeadas: sin esquinas donde actúa el límite de pendiente (y se limita de nuevo).
				for (int32 i = SegStart; i <= SegEnd; ++i) { Part[i - SegStart] = Z[i]; }
				Part = SmoothScalars(Part, 4);
				for (int32 i = SegStart; i <= SegEnd; ++i) { Z[i] = Part[i - SegStart]; }
				SlopeLimit(Z, L.Main, SegStart, SegEnd, P.MaxPathSlope);
				SegStart = SegEnd + 1;
			}
		}

		// Cota colosal de cada cruce.
		for (FCrossing& C : L.Crossings)
		{
			const FRouteStep& High = L.Route[C.HighStep];
			const FRouteStep& Low = L.Route[C.LowStep];
			double Ground = L.Modules[C.Module].Level;
			for (int32 i = Low.FirstSample; i <= Low.LastSample; ++i) { Ground = FMath::Max(Ground, Z[i]); }
			if (C.HighStep > 0) { Ground = FMath::Max(Ground, Z[L.Route[C.HighStep - 1].LastSample]); }
			if (C.HighStep + 1 < L.Route.Num()) { Ground = FMath::Max(Ground, Z[L.Route[C.HighStep + 1].FirstSample]); }
			C.TopZ = Ground + Rng.Range(P.ColossalHeightMin, P.ColossalHeightMax);
			for (int32 i = High.FirstSample; i <= High.LastSample; ++i)
			{
				Z[i] = C.TopZ;
				// Tablero del puente o adarve de la muralla: malla, no terreno.
				L.Main[i].Flags |= PathFlags::Elevated;
				L.Main[i].Width = FMath::Clamp(L.Main[i].Width, 550.0, 900.0);
			}
			// Punto de cruce entre las dos pasadas.
			bool bFoundCross = false;
			for (int32 i = High.FirstSample; i < High.LastSample && !bFoundCross; ++i)
			{
				for (int32 j = Low.FirstSample; j < Low.LastSample; ++j)
				{
					FVector2D X;
					if (SegmentsIntersect(L.Main[i].P, L.Main[i + 1].P, L.Main[j].P, L.Main[j + 1].P, &X))
					{
						C.CrossPoint = X;
						bFoundCross = true;
						break;
					}
				}
			}
			if (!bFoundCross) { C.CrossPoint = L.Modules[C.Module].Centroid; }
		}

		// Extremos de las pasadas altas: torre al entrar (géiser al pie) y al salir (tobogán).
		const double TanSlide = FMath::Tan(FMath::DegreesToRadians(P.SlideAngleDeg));
		for (const FCrossing& C : L.Crossings)
		{
			const FRouteStep& High = L.Route[C.HighStep];
			const FVector2D InPortal = L.Main[High.FirstSample].P;
			const FVector2D OutPortal = L.Main[High.LastSample].P;

			// Pasada alta: plataforma de torre en ambos extremos.
			for (int32 i = High.FirstSample; i <= High.LastSample; ++i)
			{
				if (FVector2D::Distance(L.Main[i].P, InPortal) < P.TowerRadius || FVector2D::Distance(L.Main[i].P, OutPortal) < P.TowerRadius)
				{
					L.Main[i].Flags |= PathFlags::TowerTop;
				}
			}

			// Paso previo: entra a ras de suelo en la torre (hueca) por su puerta y acaba en el centro, donde el géiser
			// lanza por el hueco del forjado hasta la cima. Dentro, suelo llano a la cota de la puerta; los últimos
			// 15 m se estrechan en embudo hasta el ancho de la puerta. El portal (la última muestra) es la cima.
			if (C.HighStep > 0)
			{
				const FRouteStep& Prev = L.Route[C.HighStep - 1];
				double FloorZ = Z[FMath::Max(Prev.FirstSample, Prev.LastSample - 1)];
				for (int32 i = Prev.LastSample - 1; i >= Prev.FirstSample; --i)
				{
					if (FVector2D::Distance(L.Main[i].P, InPortal) >= P.TowerRadius + 200.0) { FloorZ = Z[i]; break; }
				}
				const double DoorW = 2.0 * TowerDims::DoorHalf - 60.0;
				int32 GeyserIdx = INDEX_NONE;
				for (int32 i = Prev.LastSample - 1; i >= Prev.FirstSample; --i)
				{
					const double D = FVector2D::Distance(L.Main[i].P, InPortal);
					if (D < P.TowerRadius + 200.0)
					{
						L.Main[i].Flags |= PathFlags::UnderTower;
						Z[i] = FloorZ;
						L.Main[i].Width = FMath::Min(L.Main[i].Width, DoorW);
						if (GeyserIdx == INDEX_NONE) { GeyserIdx = i; }
					}
					else if (D < P.TowerRadius + 1500.0)
					{
						// Embudo: 7 m en la boca del paso de la puerta (el muro en talud) y el ancho del camino a 15 m.
						L.Main[i].Width = FMath::Min(L.Main[i].Width, LerpD(700.0, L.Main[i].Width, (D - P.TowerRadius - 200.0) / 1300.0));
					}
					else
					{
						break;
					}
				}
				if (GeyserIdx != INDEX_NONE) { L.Main[GeyserIdx].Flags |= PathFlags::GeyserBase; }
			}

			// Paso siguiente: cima de torre y tobogán hasta el suelo.
			if (C.HighStep + 1 < L.Route.Num())
			{
				const FRouteStep& Next = L.Route[C.HighStep + 1];
				double SlideStartS = -1.0;
				for (int32 i = Next.FirstSample + 1; i <= Next.LastSample; ++i)
				{
					const double D = FVector2D::Distance(L.Main[i].P, OutPortal);
					if (SlideStartS < 0.0 && D < P.TowerRadius)
					{
						L.Main[i].Flags |= PathFlags::TowerTop;
						Z[i] = C.TopZ;
						L.Main[i].Width = TowerTopWidth(P, D);
						continue;
					}
					if (SlideStartS < 0.0) { SlideStartS = L.Main[i - 1].S; }
					const double Ramp = C.TopZ - TanSlide * (L.Main[i].S - SlideStartS);
					if (Ramp <= Z[i]) { break; }
					Z[i] = Ramp;
					L.Main[i].Flags |= PathFlags::Slide;
				}
			}
		}

		// Desniveles grandes entre módulos normales.
		for (int32 c = 0; c < Cuts.Num(); ++c)
		{
			const int32 CutIdx = Cuts[c];
			const int32 StepA = L.Main[CutIdx].Step;
			if (L.Route[StepA].bHigh || (StepA + 1 < L.Route.Num() && L.Route[StepA + 1].bHigh)) { continue; }
			if (CutIsUp[c])
			{
				// Géiser en el lado bajo, a ~8 m del escalón; aterriza pasado el borde.
				for (int32 i = CutIdx; i >= FMath::Max(0, CutIdx - 40); --i)
				{
					if (L.Main[CutIdx].S - L.Main[i].S >= 800.0) { L.Main[i].Flags |= PathFlags::GeyserBase; break; }
				}
				if (CutIdx + 1 < NumS) { L.Main[CutIdx + 1].Flags |= PathFlags::CliffUp; }
			}
			else
			{
				const double Top = Z[CutIdx];
				for (int32 i = CutIdx + 1; i < NumS; ++i)
				{
					const double Ramp = Top - TanSlide * (L.Main[i].S - L.Main[CutIdx].S);
					if (Ramp <= Z[i]) { break; }
					Z[i] = Ramp;
					L.Main[i].Flags |= PathFlags::Slide;
				}
			}
		}

		// Supervivencia: el camino no baja al nivel del mar antes de la playa final.
		if (P.MinPathZ > -1e8)
		{
			for (int32 i = 0; i < NumS; ++i) { Z[i] = FMath::Max(Z[i], P.MinPathZ); }
		}

		// Claro de salida plano y bajada final al mar.
		const double StartZ = Z[0];
		for (int32 i = 0; i < NumS; ++i)
		{
			if ((L.Main[i].Flags & PathFlags::Start) != 0) { Z[i] = StartZ; }
		}
		int32 FirstShore = NumS;
		for (int32 i = 0; i < NumS; ++i) { if ((L.Main[i].Flags & PathFlags::Shore) != 0) { FirstShore = i; break; } }
		// Rampa suave hasta la cota de la playa antes de llegar a ella.
		if (FirstShore > 0 && FirstShore < NumS)
		{
			for (int32 i = FirstShore - 1; i >= 0; --i)
			{
				const double D = L.Main[FirstShore - 1].S - L.Main[i].S;
				if (D > 6000.0 || (L.Main[i].Flags & PathFlags::Special & ~PathFlags::Portal) != 0) { break; }
				Z[i] = LerpD(FinishDims::BeachZ, Z[i], SmoothStep(0.0, 6000.0, D));
			}
		}
		// Playa: arena en suave pendiente hasta la orilla y fondo que baja mar adentro (en la línea de
		// meta el agua cubre unos 75 cm; al final del camino enlaza con el fondo del mar).
		if (FirstShore < NumS)
		{
			const double WaterY = FinishWaterY(L);
			const double Land = FMath::Max(1.0, WaterY - L.Main[FirstShore].P.Y);
			for (int32 i = FirstShore; i < NumS; ++i)
			{
				const double D = WaterY - L.Main[i].P.Y;
				const double Depth = FMath::Max(0.0, -D);
				Z[i] = D >= 0.0 ? LerpD(FinishDims::WaterEdgeZ, FinishDims::BeachZ, FMath::Min(1.0, D / Land))
					: FMath::Max(FinishDims::SeaFloorMin, FinishDims::WaterEdgeZ - FMath::Min(Depth, FinishDims::LineInWater) * FinishDims::SeaSlope
						- FMath::Max(0.0, Depth - FinishDims::LineInWater) * FinishDims::SeaSlopePastLine);
			}
		}

		for (int32 i = 0; i < NumS; ++i) { L.Main[i].Z = Z[i]; }

		// Tramos sobre agua: alternan suelo firme (barras de arena / barro) con isletas o
		// pasarelas, salvo junto a torres, géiseres y toboganes, que necesitan suelo.
		double NextToggleS = -1.0;
		bool bWetStretch = false;
		for (int32 i = 0; i < NumS; ++i)
		{
			FPathSample& Sm = L.Main[i];
			if (!IsWetBiome(Sm.Biome)) { NextToggleS = -1.0; continue; }
			if (NextToggleS < 0.0)
			{
				bWetStretch = Rng.Chance(0.5);
				NextToggleS = Sm.S + (bWetStretch ? Rng.Range(4000.0, 11000.0) : Rng.Range(5000.0, 14000.0));
			}
			else if (Sm.S >= NextToggleS)
			{
				bWetStretch = !bWetStretch;
				NextToggleS = Sm.S + (bWetStretch ? Rng.Range(4000.0, 11000.0) : Rng.Range(5000.0, 14000.0));
			}
			if (!bWetStretch) { continue; }
			bool bNearSolid = false;
			for (int32 j = FMath::Max(0, i - 8); j <= FMath::Min(NumS - 1, i + 8); ++j)
			{
				const uint32 Solid = PathFlags::GeyserBase | PathFlags::Slide | PathFlags::UnderTower | PathFlags::TowerTop | PathFlags::CliffUp | PathFlags::Start;
				if ((L.Main[j].Flags & Solid) != 0) { bNearSolid = true; break; }
			}
			if (bNearSolid || (Sm.Flags & (PathFlags::Elevated | PathFlags::Colossal)) != 0) { continue; }
			Sm.Flags |= (Sm.Biome == ETNProcBiome::Water) ? PathFlags::Islet : PathFlags::Boardwalk;
			// Pasarela de tablones de 5,5-8 m (el agua de alrededor la da la poza).
			if (Sm.Biome != ETNProcBiome::Water) { Sm.Width = FMath::Clamp(Sm.Width, 550.0, 800.0); }
		}

		// Tramo bajo de las murallas: pasa por su puerta. Sus muestras dentro del grueso del muro (y 3 m
		// de explanada a cada lado) van bajo una estructura: sin huecos, obstáculos ni peligros, y no
		// más anchas que la puerta.
		for (const FCrossing& C : L.Crossings)
		{
			if (C.Type != ETNProcCrossingType::Wall) { continue; }
			const FRouteStep& High = L.Route[C.HighStep];
			const FRouteStep& Low = L.Route[C.LowStep];
			for (int32 j = Low.FirstSample; j <= Low.LastSample; ++j)
			{
				double MinD = 1e300;
				int32 Near = High.FirstSample;
				for (int32 i = High.FirstSample; i < High.LastSample; ++i)
				{
					double T = 0.0;
					const double D = DistPointSegment(L.Main[j].P, L.Main[i].P, L.Main[i + 1].P, T);
					if (D < MinD) { MinD = D; Near = i; }
				}
				if (MinD < WallDims::HalfAt(L.Main[Near].Width * 0.5, C.TopZ - L.Main[j].Z) + 300.0)
				{
					L.Main[j].Flags |= PathFlags::Tunnel;
					L.Main[j].Width = FMath::Min(L.Main[j].Width, 900.0);
				}
			}
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Ramas
	// ─────────────────────────────────────────────────────────────────────────

	namespace PathDetail
	{
		inline bool IsCrossingModule(const FLayout& L, int32 Module)
		{
			for (const FCrossing& C : L.Crossings) { if (C.Module == Module) { return true; } }
			return false;
		}

		/** Forma de cada tipo de rama (cm). */
		struct FBranchShape
		{
			/** Longitud del tramo del principal que rodea. */
			double LenMin = 15000.0, LenMax = 45000.0;
			/** Separación lateral máxima respecto al principal. */
			double AmpMin = 4500.0, AmpMax = 11000.0;
			double WMin = 750.0, WMax = 1500.0;
			/** Ondulación de alturas (0 en la ruta alta, que lleva su propio perfil). */
			double Wave = 280.0;
		};

		inline FBranchShape BranchShapeOf(EBranchKind Kind)
		{
			FBranchShape S;
			switch (Kind)
			{
				case EBranchKind::Lane:
					S.LenMin = 12000.0; S.LenMax = 20000.0; S.AmpMin = 3800.0; S.AmpMax = 5200.0; S.WMin = 650.0; S.WMax = 850.0; break;
				case EBranchKind::Risky:
					S.LenMin = 9000.0; S.LenMax = 26000.0; S.AmpMin = 3500.0; S.AmpMax = 8000.0; S.WMin = 340.0; S.WMax = 520.0; break;
				case EBranchKind::High:
					S.LenMin = 18000.0; S.LenMax = 40000.0; S.AmpMin = 5000.0; S.AmpMax = 10000.0; S.WMin = 500.0; S.WMax = 900.0; S.Wave = 0.0; break;
				case EBranchKind::Bypass:
					S.LenMin = 5000.0; S.LenMax = 12000.0; S.AmpMin = 2600.0; S.AmpMax = 4200.0; S.WMin = 450.0; S.WMax = 900.0; S.Wave = 120.0; break;
				case EBranchKind::Scenic:
				default:
					break;
			}
			return S;
		}

		/** Tipo de rama dentro de un módulo; bLong excluye el rodeo corto. */
		inline EBranchKind PickBranchKind(FRng& Rng, bool bLong = false)
		{
			const double U = Rng.Unit() * (bLong ? 0.85 : 1.0);
			if (U < 0.35) { return EBranchKind::Scenic; }
			if (U < 0.6) { return EBranchKind::Risky; }
			if (U < 0.85) { return EBranchKind::High; }
			return EBranchKind::Bypass;
		}

		/**
		 * Validación común de la polilínea de una rama (remuestreada a SampleSpacing): sin
		 * pliegues ni tramos que se rocen, dentro del mapa, fuera de módulos de cruce, de
		 * agua y de mesetas, separada del principal salvo junto a su horquilla y su unión,
		 * lejos de torres, de la salida y de las demás ramas.
		 */
		inline bool ValidateBranchPolyline(const FLayout& L, const TArray<FVector2D>& Pts, int32 I0, int32 I1, double BranchW,
			const FSampleGrid& Grid, const TArray<FVector2D>& Towers, double EndZoneMax = 5500.0, int32 HostModule = INDEX_NONE)
		{
			const FGenParams& P = L.Params;
			const double BranchLen = PolylineLength(Pts);
			const double EndZone = FMath::Min(EndZoneMax, 0.3 * BranchLen);
			for (int32 k = 2; k < Pts.Num(); ++k)
			{
				const FVector2D D0 = (Pts[k - 1] - Pts[k - 2]).GetSafeNormal();
				const FVector2D D1 = (Pts[k] - Pts[k - 1]).GetSafeNormal();
				if (FVector2D::DotProduct(D0, D1) < 0.64) { return false; }
			}
			for (int32 i = 0; i < Pts.Num(); i += 2)
			{
				for (int32 j = i + 2; j < Pts.Num(); j += 2)
				{
					if ((j - i) * P.SampleSpacing < 6000.0) { continue; }
					if (FVector2D::DistSquared(Pts[i], Pts[j]) < 3000.0 * 3000.0) { return false; }
				}
			}
			double Acc = 0.0;
			for (int32 k = 0; k < Pts.Num(); ++k)
			{
				if (k > 0) { Acc += FVector2D::Distance(Pts[k - 1], Pts[k]); }
				if (Acc < 2500.0 || BranchLen - Acc < 2500.0) { continue; }
				const FVector2D& Pt = Pts[k];
				if (Pt.X < P.MapEdgeClearance * 0.7 || Pt.X > L.WorldSizeX - P.MapEdgeClearance * 0.7 || Pt.Y < P.MapEdgeClearance * 0.7 || Pt.Y > L.CoastY(Pt.X) - 6000.0)
				{
					return false;
				}
				const int32 Mod = L.ModuleAt(Pt);
				if (Mod == INDEX_NONE || IsCrossingModule(L, Mod) || IsWetBiome(L.Modules[Mod].Biome)) { return false; }
				if (Mod != HostModule && L.Modules[Mod].VisitCount == 0 && L.Modules[Mod].EmptyKind == ETNProcEmptyModuleMode::Elevated) { return false; }
				double DMain = 0.0;
				const int32 Near = Grid.Nearest(Pt, 20000.0, DMain);
				if (Near != INDEX_NONE)
				{
					// Cerca del principal solo junto a su propia horquilla o unión (no junto a otra
					// parte del principal que pase por allí: se fundirían los cauces).
					const bool bNearFork = Acc < EndZone && FMath::Abs(Near - I0) <= 20;
					const bool bNearJoin = BranchLen - Acc < EndZone && FMath::Abs(Near - I1) <= 20;
					const double Need = L.Main[Near].Width * 0.5 + BranchW * 0.5 + 1600.0;
					if (DMain < Need && !bNearFork && !bNearJoin) { return false; }
				}
				for (const FVector2D& T : Towers) { if (FVector2D::Distance(T, Pt) < P.TowerRadius + 3500.0) { return false; } }
				if (FVector2D::Distance(L.StartPoint, Pt) < P.StartClearingRadius + 3000.0) { return false; }
				for (const FBranch& Other : L.Branches)
				{
					for (const FPathSample& Os : Other.Samples)
					{
						if (FVector2D::DistSquared(Os.P, Pt) < 5500.0 * 5500.0) { return false; }
					}
				}
			}
			return true;
		}

		/** Horquilla y unión separadas de las de las demás ramas (el tramo del principal entre ellas puede compartirse). */
		inline bool ForksClear(const FLayout& L, int32 I0, int32 I1)
		{
			for (const FBranch& Other : L.Branches)
			{
				for (const int32 A : { I0, I1 })
				{
					if (FMath::Abs(A - Other.ForkSample) < 16 || FMath::Abs(A - Other.RejoinSample) < 16) { return false; }
				}
			}
			return true;
		}

		/** Muestras de rama a partir de la polilínea (módulo, bioma y paso de la horquilla). */
		inline TArray<FPathSample> MakeBranchSamples(const FLayout& L, const TArray<FVector2D>& Pts, int32 I0)
		{
			TArray<FPathSample> Out;
			for (const FVector2D& Pt : Pts)
			{
				FPathSample Sm;
				Sm.P = Pt;
				Sm.Module = L.ModuleAt(Pt);
				if (Sm.Module == INDEX_NONE) { Sm.Module = L.Main[I0].Module; }
				Sm.Biome = L.Modules[Sm.Module].Biome;
				Sm.Step = L.Main[I0].Step;
				Out.Add(Sm);
			}
			FinalizeSamples(Out);
			return Out;
		}

		inline bool SlopesOk(const TArray<FPathSample>& S, const TArray<double>& Z, double MaxSlope)
		{
			for (int32 k = 1; k < Z.Num(); ++k)
			{
				const double Ds = FMath::Max(1.0, S[k].S - S[k - 1].S);
				if (FMath::Abs(Z[k] - Z[k - 1]) / Ds > MaxSlope) { return false; }
			}
			return true;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Red de sendas por el terreno libre
	// ─────────────────────────────────────────────────────────────────────────

	namespace PathDetail
	{
		/** Camino por identificador de red: 0 = principal, b + 1 = rama b. */
		inline const TArray<FPathSample>& NetPath(const FLayout& L, int32 Id)
		{
			return Id == 0 ? L.Main : L.Branches[Id - 1].Samples;
		}

		inline TArray<FPathSample>& NetPathMutable(FLayout& L, int32 Id)
		{
			return Id == 0 ? L.Main : L.Branches[Id - 1].Samples;
		}

		/** Progreso (S del principal) de una muestra de cualquier camino de la red. */
		inline double NetProgress(const FLayout& L, int32 Id, int32 Index)
		{
			if (Id == 0) { return L.Main[Index].S; }
			const FBranch& B = L.Branches[Id - 1];
			const double Len = FMath::Max(1.0, B.Samples.Last().S);
			return LerpD(L.Main[B.ForkSample].S, L.Main[B.RejoinSample].S, B.Samples[Index].S / Len);
		}

		/** Marca las muestras junto a una unión (sin huecos, obstáculos ni peligros encima). */
		inline void MarkJunction(FLayout& L, int32 Id, int32 Index)
		{
			TArray<FPathSample>& S = NetPathMutable(L, Id);
			for (int32 j = FMath::Max(0, Index - 12); j <= FMath::Min(S.Num() - 1, Index + 12); ++j) { S[j].Flags |= PathFlags::Junction; }
		}

		/** Muestras de todos los caminos de la red en cubos de 40 m, para medir holguras exactas. */
		struct FNetGrid
		{
			double Cell = 4000.0;
			double Origin = -20000.0;
			int32 W = 0;
			double MaxHalfWidth = 0.0;
			TArray<TArray<FIntPoint>> Buckets;

			void Init(double WorldSize)
			{
				W = FMath::CeilToInt((WorldSize - 2.0 * Origin) / Cell) + 1;
				Buckets.Reset();
				Buckets.SetNum(W * W);
				MaxHalfWidth = 0.0;
			}

			void Add(const FLayout& L, int32 Id)
			{
				const TArray<FPathSample>& S = NetPath(L, Id);
				for (int32 i = 0; i < S.Num(); ++i)
				{
					const int32 X = FMath::Clamp(FMath::FloorToInt((S[i].P.X - Origin) / Cell), 0, W - 1);
					const int32 Y = FMath::Clamp(FMath::FloorToInt((S[i].P.Y - Origin) / Cell), 0, W - 1);
					Buckets[Y * W + X].Add(FIntPoint(Id, i));
					MaxHalfWidth = FMath::Max(MaxHalfWidth, S[i].Width * 0.5);
				}
			}

			/** Distancia de P al borde del cauce más cercano (como mucho Radius), sin las muestras que Skip(Id, i) descarte. */
			template <typename FSkip>
			double EdgeDist(const FLayout& L, const FVector2D& P, double Radius, FSkip Skip) const
			{
				double Best = Radius;
				const int32 R = FMath::CeilToInt((Radius + MaxHalfWidth) / Cell);
				const int32 CX = FMath::FloorToInt((P.X - Origin) / Cell);
				const int32 CY = FMath::FloorToInt((P.Y - Origin) / Cell);
				for (int32 Y = FMath::Max(0, CY - R); Y <= FMath::Min(W - 1, CY + R); ++Y)
				{
					for (int32 X = FMath::Max(0, CX - R); X <= FMath::Min(W - 1, CX + R); ++X)
					{
						for (const FIntPoint& E : Buckets[Y * W + X])
						{
							if (Skip(E.X, E.Y)) { continue; }
							const FPathSample& S = NetPath(L, E.X)[E.Y];
							Best = FMath::Min(Best, FVector2D::Distance(P, S.P) - S.Width * 0.5);
						}
					}
				}
				return Best;
			}
		};

		/**
		 * Rejilla de 10 m para trazar sendas: coste base de cada celda (0 = prohibida: junto a los
		 * bordes y la costa, en módulos de cruce o de agua, junto a torres y a la salida; las
		 * mesetas se cruzan en desfiladero pero cuestan más), distancia a la celda prohibida más
		 * cercana y distancia al borde del cauce más cercano de los caminos ya trazados.
		 */
		struct FTrailGrid
		{
			double Cell = 1000.0;
			int32 W = 0, H = 0;
			TArray<float> Base;
			TArray<float> Wall;
			TArray<float> Edge;

			int32 Index(const FIntPoint& C) const { return C.Y * W + C.X; }
			FVector2D Center(int32 Idx) const { return FVector2D((Idx % W + 0.5) * Cell, (Idx / W + 0.5) * Cell); }
			FIntPoint CellOf(const FVector2D& P) const
			{
				return FIntPoint(FMath::Clamp(FMath::FloorToInt(P.X / Cell), 0, W - 1), FMath::Clamp(FMath::FloorToInt(P.Y / Cell), 0, H - 1));
			}
			bool Free(const FVector2D& P) const { return Base[Index(CellOf(P))] > 0.0f; }
			double EdgeAt(const FVector2D& P) const { return Edge[Index(CellOf(P))]; }
			/** Holgura lateral de una senda en P: hasta los otros cauces (más allá de Clear) y hasta lo prohibido. */
			double RoomAt(const FVector2D& P, double Clear) const
			{
				const int32 Idx = Index(CellOf(P));
				return FMath::Max(0.0, FMath::Min(Edge[Idx] - Clear, Wall[Idx] - 1500.0));
			}

			void Build(const FLayout& L, const TArray<FVector2D>& Towers)
			{
				const FGenParams& P = L.Params;
				W = H = FMath::Max(1, FMath::CeilToInt(L.MaxExtent() / Cell));
				Base.Init(1.0f, W * H);
				Edge.Init(1e9f, W * H);
				const double Margin = P.MapEdgeClearance * 0.7 + 1500.0;
				for (int32 Idx = 0; Idx < W * H; ++Idx)
				{
					const FVector2D C = Center(Idx);
					float& B = Base[Idx];
					if (C.X < Margin || C.X > L.WorldSizeX - Margin || C.Y < Margin || C.Y > L.CoastY(C.X) - 7500.0) { B = 0.0f; continue; }
					const int32 Mod = L.ModuleAt(C);
					if (Mod == INDEX_NONE || IsCrossingModule(L, Mod) || IsWetBiome(L.Modules[Mod].Biome)) { B = 0.0f; continue; }
					if (FVector2D::Distance(C, L.StartPoint) < P.StartClearingRadius + 4000.0) { B = 0.0f; continue; }
					for (const FVector2D& T : Towers) { if (FVector2D::Distance(C, T) < P.TowerRadius + 4000.0) { B = 0.0f; break; } }
					if (B <= 0.0f) { continue; }
					const FModule& M = L.Modules[Mod];
					if (M.VisitCount == 0 && M.EmptyKind == ETNProcEmptyModuleMode::Elevated) { B = 1.8f; }
				}
				Wall.Init(1e9f, W * H);
				for (int32 Idx = 0; Idx < W * H; ++Idx) { if (Base[Idx] <= 0.0f) { Wall[Idx] = 0.0f; } }
				Chamfer(Wall);
			}

			/** Anota un camino en la distancia a los cauces (exacta hasta 60 m de su borde; Relax propaga el resto). */
			void Stamp(const TArray<FPathSample>& S)
			{
				for (const FPathSample& Sm : S)
				{
					const double Hw = Sm.Width * 0.5;
					const int32 R = FMath::CeilToInt((Hw + 6000.0) / Cell);
					const FIntPoint C = CellOf(Sm.P);
					for (int32 Y = FMath::Max(0, C.Y - R); Y <= FMath::Min(H - 1, C.Y + R); ++Y)
					{
						for (int32 X = FMath::Max(0, C.X - R); X <= FMath::Min(W - 1, C.X + R); ++X)
						{
							const int32 Idx = Y * W + X;
							const float D = static_cast<float>(FVector2D::Distance(Center(Idx), Sm.P) - Hw);
							if (D < Edge[Idx]) { Edge[Idx] = D; }
						}
					}
				}
			}

			/** Componentes conexas (8-vecindad) de las celdas libres a Clear de los cauces; -1 en el resto. */
			void Components(double Clear, TArray<int32>& Out) const
			{
				Out.Init(-1, W * H);
				TArray<int32> Queue;
				int32 Next = 0;
				for (int32 Seed = 0; Seed < W * H; ++Seed)
				{
					if (Out[Seed] >= 0 || Base[Seed] <= 0.0f || Edge[Seed] < Clear) { continue; }
					Queue.Reset();
					Queue.Add(Seed);
					Out[Seed] = Next;
					for (int32 h = 0; h < Queue.Num(); ++h)
					{
						const int32 C = Queue[h];
						const int32 CX = C % W;
						const int32 CY = C / W;
						for (int32 Y = FMath::Max(0, CY - 1); Y <= FMath::Min(H - 1, CY + 1); ++Y)
						{
							for (int32 X = FMath::Max(0, CX - 1); X <= FMath::Min(W - 1, CX + 1); ++X)
							{
								const int32 Nb = Y * W + X;
								if (Out[Nb] < 0 && Base[Nb] > 0.0f && Edge[Nb] >= Clear) { Out[Nb] = Next; Queue.Add(Nb); }
							}
						}
					}
					++Next;
				}
			}

			/** Propaga lo anotado en la distancia a los cauces a todo el mapa. */
			void Relax() { Chamfer(Edge); }

			/** Transformada de distancia (chamfer 3x3) in situ. */
			void Chamfer(TArray<float>& D) const
			{
				const float O = static_cast<float>(Cell);
				const float Dg = static_cast<float>(Cell * 1.41421356);
				auto Pass = [&D](int32 Idx, int32 From, float C) { if (D[From] + C < D[Idx]) { D[Idx] = D[From] + C; } };
				for (int32 Y = 0; Y < H; ++Y)
				{
					for (int32 X = 0; X < W; ++X)
					{
						const int32 Idx = Y * W + X;
						if (X > 0) { Pass(Idx, Idx - 1, O); }
						if (Y > 0)
						{
							Pass(Idx, Idx - W, O);
							if (X > 0) { Pass(Idx, Idx - W - 1, Dg); }
							if (X < W - 1) { Pass(Idx, Idx - W + 1, Dg); }
						}
					}
				}
				for (int32 Y = H - 1; Y >= 0; --Y)
				{
					for (int32 X = W - 1; X >= 0; --X)
					{
						const int32 Idx = Y * W + X;
						if (X < W - 1) { Pass(Idx, Idx + 1, O); }
						if (Y < H - 1)
						{
							Pass(Idx, Idx + W, O);
							if (X < W - 1) { Pass(Idx, Idx + W + 1, Dg); }
							if (X > 0) { Pass(Idx, Idx + W - 1, Dg); }
						}
					}
				}
			}
		};

		/**
		 * Senda de menor coste (A*) entre dos puntos por celdas libres a Clear del borde de todos
		 * los cauces. El coste lleva ruido (cada intento serpentea distinto) y encarece acercarse
		 * a otros cauces, para que la senda vaya por el medio del terreno libre. Devuelve los
		 * centros de celda (con A y B exactos en los extremos).
		 */
		inline bool FindTrail(const FTrailGrid& G, const FVector2D& A, const FVector2D& B, double Clear, uint32 NoiseSeed, TArray<FVector2D>& Out)
		{
			Out.Reset();
			const int32 N = G.W * G.H;
			const int32 Start = G.Index(G.CellOf(A));
			const int32 Goal = G.Index(G.CellOf(B));
			TArray<float> CostCache;
			CostCache.Init(-2.0f, N);
			auto CellCost = [&](int32 Idx) -> double
			{
				if (CostCache[Idx] > -1.5f) { return CostCache[Idx]; }
				double Value = -1.0;
				if (G.Base[Idx] > 0.0f && G.Edge[Idx] >= Clear)
				{
					const FVector2D Q = G.Center(Idx);
					const double Noise = 0.5 + 0.5 * Fbm2(NoiseSeed, Q.X / 20000.0, Q.Y / 20000.0, 3);
					Value = G.Base[Idx] + 1.6 * Noise + 2.5 * SmoothStep(Clear + 6000.0, Clear, G.Edge[Idx]);
				}
				CostCache[Idx] = static_cast<float>(Value);
				return Value;
			};
			if (CellCost(Start) < 0.0 || CellCost(Goal) < 0.0) { return false; }

			TArray<double> Gs;
			Gs.Init(1e300, N);
			TArray<int32> From;
			From.Init(INDEX_NONE, N);
			const FVector2D GoalP = G.Center(Goal);
			FMinHeap Open;
			Gs[Start] = 0.0;
			Open.Push(FVector2D::Distance(G.Center(Start), GoalP), Start);
			static const int32 DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
			static const int32 DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
			int32 Expanded = 0;
			bool bFound = Start == Goal;
			while (!Open.IsEmpty() && !bFound)
			{
				const FHeapItem It = Open.Pop();
				const int32 Cur = It.Value;
				if (Cur == Goal) { bFound = true; break; }
				if (It.Key > Gs[Cur] + FVector2D::Distance(G.Center(Cur), GoalP) + 1.0) { continue; }
				if (++Expanded > 200000) { return false; }
				const double CostCur = CellCost(Cur);
				const int32 CX = Cur % G.W;
				const int32 CY = Cur / G.W;
				for (int32 k = 0; k < 8; ++k)
				{
					const int32 X = CX + DX[k];
					const int32 Y = CY + DY[k];
					if (X < 0 || Y < 0 || X >= G.W || Y >= G.H) { continue; }
					const int32 Nb = Y * G.W + X;
					const double CostNb = CellCost(Nb);
					if (CostNb < 0.0) { continue; }
					const double Ng = Gs[Cur] + (k < 4 ? 1.0 : 1.41421356) * G.Cell * 0.5 * (CostCur + CostNb);
					if (Ng < Gs[Nb])
					{
						Gs[Nb] = Ng;
						From[Nb] = Cur;
						Open.Push(Ng + FVector2D::Distance(G.Center(Nb), GoalP), Nb);
					}
				}
			}
			if (!bFound) { return false; }
			TArray<int32> Rev;
			for (int32 C = Goal; C != INDEX_NONE; C = From[C]) { Rev.Add(C); }
			for (int32 i = Rev.Num() - 1; i >= 0; --i) { Out.Add(G.Center(Rev[i])); }
			Out[0] = A;
			if (Out.Num() == 1) { Out.Add(B); } else { Out.Last() = B; }
			return true;
		}

		/**
		 * Puntos de control de una senda de celdas, uno cada ~30 m, con meandros de 90-160 m de
		 * onda cuya amplitud (hasta 30 m por Amp) cabe en la holgura y se apaga junto a los extremos.
		 */
		inline TArray<FVector2D> TrailControl(const FTrailGrid& G, const TArray<FVector2D>& Cells, double Clear, uint32 Seed, double Amp)
		{
			const int32 N = Cells.Num();
			TArray<double> Acc;
			Acc.SetNum(N);
			Acc[0] = 0.0;
			for (int32 i = 1; i < N; ++i) { Acc[i] = Acc[i - 1] + FVector2D::Distance(Cells[i - 1], Cells[i]); }
			TArray<FVector2D> Ctrl;
			Ctrl.Add(Cells[0]);
			double Phase = static_cast<double>(Seed % 6283u) / 1000.0;
			double Prev = 0.0;
			for (int32 i = 3; i < N - 2; i += 3)
			{
				const double S = Acc[i];
				const double Wave = 9000.0 + 7000.0 * (0.5 + 0.5 * Noise1(Seed + 5u, S / 40000.0));
				Phase += TwoPi * (S - Prev) / Wave;
				Prev = S;
				const FVector2D D = (Cells[FMath::Min(i + 3, N - 1)] - Cells[FMath::Max(0, i - 3)]).GetSafeNormal();
				const double Taper = SmoothStep(0.0, 4000.0, S) * SmoothStep(0.0, 4000.0, Acc[N - 1] - S);
				const double Room = FMath::Min(G.RoomAt(Cells[i], Clear) * 0.8, 3000.0 * Amp);
				const double Off = Room * Taper * (0.75 * FMath::Sin(Phase) + 0.25 * Noise1(Seed, S / 5000.0));
				Ctrl.Add(Cells[i] + LeftNormal(D) * Off);
			}
			Ctrl.Add(Cells.Last());
			return Ctrl;
		}

		/** Curva de Hermite de A a B con tangentes TA y TB (sin el punto B), un punto cada 3 m. */
		inline void AppendHermite(const FVector2D& A, const FVector2D& TA, const FVector2D& B, const FVector2D& TB, TArray<FVector2D>& Out)
		{
			const double Len = FVector2D::Distance(A, B);
			// Las salidas de las sendas (TrailDeparture) están a 30, 60, 90 o 120 m exactos: Len / 300 cae en un entero
			// salvo el último bit, que no es igual en DebugGame y en Development (#579). Sin el margen, el techo daba un
			// punto más o menos según la compilación y cambiaba la senda.
			const int32 N = FMath::Max(2, FMath::CeilToInt(Len / 300.0 - 1e-6));
			for (int32 k = 0; k < N; ++k)
			{
				const double T = static_cast<double>(k) / N;
				const double T2 = T * T;
				const double T3 = T2 * T;
				Out.Add(A * (2.0 * T3 - 3.0 * T2 + 1.0) + TA * (Len * (T3 - 2.0 * T2 + T)) + B * (-2.0 * T3 + 3.0 * T2) + TB * (Len * (T3 - T2)));
			}
		}

		/** Si en la muestra i de un camino puede salir o llegar una senda: tramo normal y llano, en tierra y sin otras uniones cerca. */
		inline bool TrailAttachable(const FLayout& L, const TArray<FPathSample>& S, int32 i)
		{
			if (i < 20 || i > S.Num() - 21) { return false; }
			const FPathSample& Sm = S[i];
			if (IsWetBiome(Sm.Biome) || Sm.Module == INDEX_NONE || IsCrossingModule(L, Sm.Module) || Sm.Width > 4000.0) { return false; }
			for (int32 j = i - 14; j <= i + 14; ++j)
			{
				if ((S[j].Flags & (PathFlags::Special | PathFlags::Lane)) != 0) { return false; }
			}
			return FMath::Abs(S[i + 10].Z - S[i - 10].Z) < 450.0;
		}

		/**
		 * Punto de salida de una senda desde la muestra i de un camino, hacia Toward: se abre 45-80°
		 * del sentido Sign * Dir (hacia delante al salir, hacia atrás al llegar: la unión queda en
		 * "Y") y se aleja hasta quedar a Clear del borde de todos los cauces, el suyo incluido,
		 * sin que el enlace roce otros cauces.
		 */
		inline bool TrailDeparture(const FLayout& L, const FTrailGrid& G, const FNetGrid& Net, int32 PathId, int32 i, double Sign,
			const FVector2D& Toward, double Clear, double Hw, FVector2D& OutPoint, FVector2D& OutDir)
		{
			const FPathSample& Sm = NetPath(L, PathId)[i];
			const FVector2D Dir = Sm.Dir * Sign;
			const double Side = FVector2D::CrossProduct(Dir, Toward - Sm.P) >= 0.0 ? 1.0 : -1.0;
			auto Skip = [PathId, i](int32 Id, int32 k) { return Id == PathId && FMath::Abs(k - i) <= 45; };
			for (const double Deg : { 45.0, 60.0, 80.0 })
			{
				const double Ang = FMath::DegreesToRadians(Deg);
				const FVector2D D = (Dir * FMath::Cos(Ang) + LeftNormal(Dir) * (Side * FMath::Sin(Ang))).GetSafeNormal();
				for (double Len = 3000.0; Len <= 14000.0; Len += 1000.0)
				{
					const FVector2D Q = Sm.P + D * Len;
					bool bOk = true;
					for (double T = 1500.0; T <= Len && bOk; T += 500.0)
					{
						bOk = Net.EdgeDist(L, Sm.P + D * T, Hw + 1601.0, Skip) >= Hw + 1600.0;
					}
					if (!bOk) { break; }
					if (!G.Free(Q) || G.EdgeAt(Q) < Clear) { continue; }
					OutPoint = Q;
					OutDir = D;
					return true;
				}
			}
			return false;
		}

		/**
		 * Validación de una senda: sin pliegues ni tramos que se rocen, por terreno libre y a 16 m
		 * de cualquier otro cauce, salvo el camino padre junto a cada unión.
		 */
		inline bool ValidateTrail(const FLayout& L, const FTrailGrid& G, const FNetGrid& Net, const TArray<FVector2D>& Pts,
			int32 PathA, int32 IdxA, int32 PathB, int32 IdxB, double Hw)
		{
			const FGenParams& P = L.Params;
			for (int32 k = 2; k < Pts.Num(); ++k)
			{
				const FVector2D D0 = (Pts[k - 1] - Pts[k - 2]).GetSafeNormal();
				const FVector2D D1 = (Pts[k] - Pts[k - 1]).GetSafeNormal();
				if (FVector2D::DotProduct(D0, D1) < 0.6) { return false; }
			}
			for (int32 i = 0; i < Pts.Num(); i += 2)
			{
				for (int32 j = i + 2; j < Pts.Num(); j += 2)
				{
					if ((j - i) * P.SampleSpacing < 6000.0) { continue; }
					if (FVector2D::DistSquared(Pts[i], Pts[j]) < 3000.0 * 3000.0) { return false; }
				}
			}
			const double Len = PolylineLength(Pts);
			double Acc = 0.0;
			for (int32 k = 0; k < Pts.Num(); ++k)
			{
				if (k > 0) { Acc += FVector2D::Distance(Pts[k - 1], Pts[k]); }
				if (Acc < 1500.0 || Len - Acc < 1500.0) { continue; }
				if (!G.Free(Pts[k])) { return false; }
				const bool bNearA = Acc < 20000.0;
				const bool bNearB = Len - Acc < 20000.0;
				auto Skip = [&](int32 Id, int32 i)
				{
					return (bNearA && Id == PathA && FMath::Abs(i - IdxA) <= 45) || (bNearB && Id == PathB && FMath::Abs(i - IdxB) <= 45);
				};
				if (Net.EdgeDist(L, Pts[k], Hw + 1601.0, Skip) < Hw + 1600.0) { return false; }
			}
			return true;
		}

		/**
		 * Anchura de una senda por tramos como el principal según su carácter (0 sendero, 1 camino,
		 * 2 valle; siempre más estrecha que las explanadas del principal, que sigue mandando), sin
		 * comerse la holgura con los demás cauces (muro de 18 m).
		 */
		inline void TrailWidths(const FLayout& L, const FNetGrid& Net, TArray<FPathSample>& S, int32 PathA, int32 IdxA, int32 PathB, int32 IdxB,
			int32 Character, FRng& Rng)
		{
			const FGenParams& P = L.Params;
			TArray<double> Wd = SectionWidths(S, P, Rng, P.Seed ^ Hash32(static_cast<uint32>(IdxA) * 7u + static_cast<uint32>(IdxB) * 13u + 0x7A11u));
			const double Len = FMath::Max(1.0, S.Last().S);
			for (int32 k = 0; k < S.Num(); ++k)
			{
				double W = Wd[k];
				if (Character == 0) { W = FMath::Clamp(W * 0.5, 380.0, 850.0); }
				else if (Character == 1) { W = FMath::Clamp(W * 0.8, 450.0, 2000.0); }
				else { W = FMath::Clamp(W, 700.0, 2600.0); }
				const bool bEnds = S[k].S < 6000.0 || Len - S[k].S < 6000.0;
				auto Skip = [&](int32 Id, int32 i) { return bEnds && ((Id == PathA && FMath::Abs(i - IdxA) <= 45) || (Id == PathB && FMath::Abs(i - IdxB) <= 45)); };
				const double E = Net.EdgeDist(L, S[k].P, 6000.0, Skip);
				Wd[k] = FMath::Min(W, FMath::Max(P.PathWidthMin * 0.95, 2.0 * (E - 1800.0)));
			}
			Wd = SmoothScalars(Wd, 3);
			for (int32 k = 0; k < S.Num(); ++k) { S[k].Width = FMath::Max(360.0, Wd[k]); }
		}

		/**
		 * Junto a la horquilla (I0) y la unión (I1) con el principal, mientras la rama va dentro de su cauce
		 * (hasta 7 m fuera de su borde), su cota es la del principal, y se suelta en los 15 m siguientes: si
		 * no, donde el principal sube o baja (ondulación) quedaría un escalón entre los dos suelos.
		 */
		inline void HoldBranchToMain(const FLayout& L, const TArray<FPathSample>& S, int32 I0, int32 I1, TArray<double>& Z)
		{
			const TArray<FPathSample>& M = L.Main;
			const int32 N = S.Num();
			// Solo el suelo del principal cerca de la unión: ni cimas de torre, tableros o toboganes.
			constexpr uint32 NotGround = PathFlags::Elevated | PathFlags::TowerTop | PathFlags::UnderTower | PathFlags::Slide;
			auto MainZ = [&M](int32 Center, const FPathSample& Q, double& OutEdge)
			{
				OutEdge = 1e300;
				double Zp = M[Center].Z;
				for (int32 j = FMath::Max(0, Center - 20); j <= FMath::Min(M.Num() - 1, Center + 20); ++j)
				{
					if ((M[j].Flags & NotGround) != 0) { continue; }
					const double D = FVector2D::Distance(M[j].P, Q.P) - M[j].Width * 0.5;
					if (D < OutEdge) { OutEdge = D; Zp = M[j].Z; }
				}
				return Zp;
			};
			for (int32 End = 0; End < 2; ++End)
			{
				const int32 Center = End == 0 ? I0 : I1;
				for (int32 n = 0; n < N; ++n)
				{
					const int32 k = End == 0 ? n : N - 1 - n;
					double E = 0.0;
					const double Zp = MainZ(Center, S[k], E);
					const double W = SmoothStep(S[k].Width * 0.5 + 2200.0, S[k].Width * 0.5 + 700.0, E);
					if (W <= 0.0 && n > 0) { break; }
					Z[k] = LerpD(Z[k], Zp, W);
				}
			}
		}

		/**
		 * Alturas de una senda: junto a cada unión, mientras va dentro del cauce del camino padre,
		 * la cota de ese cauce (subir o bajar dentro dejaría escalones); por el medio sigue el
		 * nivel de los módulos que cruza con lomas y un collado o una hondonada, con la pendiente
		 * limitada y las rampas redondeadas. False si las pendientes no caben.
		 */
		inline bool TrailZ(const FLayout& L, TArray<FPathSample>& S, int32 PathA, int32 IdxA, int32 PathB, int32 IdxB, FRng& Rng)
		{
			const FGenParams& P = L.Params;
			const int32 N = S.Num();
			if (N < 30) { return false; }
			auto ParentZ = [&L](int32 Id, int32 Center, const FPathSample& Q, double& OutEdge)
			{
				const TArray<FPathSample>& Par = NetPath(L, Id);
				OutEdge = 1e300;
				double Zp = Par[Center].Z;
				for (int32 j = FMath::Max(0, Center - 45); j <= FMath::Min(Par.Num() - 1, Center + 45); ++j)
				{
					const double D = FVector2D::Distance(Par[j].P, Q.P) - Par[j].Width * 0.5;
					if (D < OutEdge) { OutEdge = D; Zp = Par[j].Z; }
				}
				return Zp;
			};

			TArray<double> Level;
			for (const FPathSample& Sm : S) { Level.Add(L.Modules[Sm.Module].Level); }
			Level = SmoothScalars(Level, 15);
			const uint32 ZSeed = P.Seed ^ Hash32(static_cast<uint32>(IdxA) * 131u + static_cast<uint32>(IdxB) * 7u + static_cast<uint32>(PathA + PathB));
			const double Hump = Rng.Range(-400.0, 1500.0);
			const double Len = FMath::Max(1.0, S.Last().S);
			TArray<double> Z;
			Z.SetNum(N);
			for (int32 k = 0; k < N; ++k)
			{
				Z[k] = Level[k] + 450.0 * Fbm1(ZSeed, S[k].S / 7000.0, 3) + Hump * FMath::Pow(FMath::Sin(Pi * S[k].S / Len), 1.5);
			}

			TArray<uint8> Hold;
			Hold.Init(0, N);
			int32 H0 = 0, H1 = N - 1;
			for (int32 k = 0; k < N; ++k)
			{
				double E = 0.0;
				const double Zp = ParentZ(PathA, IdxA, S[k], E);
				if (k > 0 && E > S[k].Width * 0.5 + 700.0) { break; }
				Z[k] = Zp;
				Hold[k] = 1;
				H0 = k;
			}
			for (int32 k = N - 1; k >= 0; --k)
			{
				double E = 0.0;
				const double Zp = ParentZ(PathB, IdxB, S[k], E);
				if (k < N - 1 && E > S[k].Width * 0.5 + 700.0) { break; }
				Z[k] = Zp;
				Hold[k] = 1;
				H1 = k;
			}
			if (H1 <= H0 + 10) { return false; }

			// El límite de pendiente solo rebaja cimas: el tramo libre sube lo necesario para poder
			// bajar a la cota de cada unión, que así queda intacta.
			const double K = P.MaxPathSlope * 0.85;
			for (int32 k = H0 + 1; k < H1; ++k)
			{
				Z[k] = FMath::Max3(Z[k], Z[H0] - K * (S[k].S - S[H0].S), Z[H1] - K * (S[H1].S - S[k].S));
			}
			const TArray<double> Fixed = Z;
			SlopeLimit(Z, S, H0, H1, K);
			if (FMath::Abs(Z[H0] - Fixed[H0]) > 1.0 || FMath::Abs(Z[H1] - Fixed[H1]) > 1.0) { return false; }
			// Rampas redondeadas: suavizado que se funde con el perfil bruto junto a cada unión (sin
			// escalón donde vuelve a mandar la cota del cauce padre).
			const TArray<double> Smooth = SmoothScalars(Z, 6);
			for (int32 k = 0; k < N; ++k)
			{
				const double W = SmoothStep(static_cast<double>(H0), H0 + 8.0, static_cast<double>(k)) * SmoothStep(static_cast<double>(H1), H1 - 8.0, static_cast<double>(k));
				Z[k] = Hold[k] ? Fixed[k] : LerpD(Z[k], Smooth[k], W);
			}
			if (!SlopesOk(S, Z, P.MaxPathSlope * 1.35)) { return false; }
			for (int32 k = 0; k < N; ++k) { S[k].Z = Z[k]; }
			return true;
		}
	}

	/**
	 * Red de sendas: une zonas por el terreno libre. Busca el mayor hueco del mapa (la celda más
	 * alejada de todos los cauces), elige dos puntos de camino a su alrededor separados por el
	 * recorrido y traza entre ellos una senda natural (A* con ruido y holgura, meandros) con
	 * uniones en "Y": si están en lados opuestos del hueco, lo cruza por su centro; si no (el
	 * hueco solo tiene camino por un lado, p. ej. junto al borde del mapa), la senda es un lazo
	 * en "U" que se adentra 150-300 m en él y vuelve. Con bLinks los extremos pueden estar también en sendas ya trazadas
	 * (enlaces que tejen la red); sin él, solo en el principal. Las que atajan mucho recorrido
	 * son senderos estrechos con más huecos (arriesgadas). Devuelve cuántas colocó.
	 */
	inline int32 BuildTrails(FLayout& L, FRng& Rng, PathDetail::FTrailGrid& G, PathDetail::FNetGrid& Net, int32 Count, bool bLinks)
	{
		using namespace PathDetail;
		const FGenParams& P = L.Params;
		int32 Placed = 0;
		// Huecos ya probados: centro y radio de exclusión (proporcional al hueco).
		TArray<FVector> Tried;
		for (int32 Round = 0; Round < Count * 4 && Placed < Count; ++Round)
		{
			// Mayor hueco aún sin probar (las mesetas cuentan menos: se prefieren los valles).
			int32 BestIdx = INDEX_NONE;
			double BestScore = 0.0;
			for (int32 Idx = 0; Idx < G.W * G.H; ++Idx)
			{
				if (G.Base[Idx] <= 0.0f) { continue; }
				const double Score = G.Edge[Idx] * (G.Base[Idx] > 1.0f ? 0.7 : 1.0);
				if (Score <= BestScore) { continue; }
				const FVector2D C = G.Center(Idx);
				bool bTried = false;
				for (const FVector& T : Tried) { if (FVector2D::DistSquared(FVector2D(T.X, T.Y), C) < T.Z * T.Z) { bTried = true; break; } }
				if (!bTried) { BestScore = Score; BestIdx = Idx; }
			}
			if (BestIdx == INDEX_NONE || G.Edge[BestIdx] < (bLinks ? 5000.0 : 6500.0)) { break; }
			const FVector2D Center = G.Center(BestIdx);
			const double Blank = G.Edge[BestIdx];
			Tried.Add(FVector(Center.X, Center.Y, FMath::Max(15000.0, 0.6 * Blank)));

			// Extremos posibles alrededor del hueco.
			struct FEnd { int32 Path = 0; int32 Index = 0; FVector2D P = FVector2D::ZeroVector; double Progress = 0.0; double Dist = 0.0; double Angle = 0.0; int32 Module = INDEX_NONE; };
			TArray<FEnd> Ends;
			for (int32 Id = 0; Id <= (bLinks ? L.Branches.Num() : 0); ++Id)
			{
				if (Id > 0)
				{
					const EBranchKind K = L.Branches[Id - 1].Kind;
					if (K != EBranchKind::Trail && K != EBranchKind::Link && K != EBranchKind::Scenic) { continue; }
				}
				const TArray<FPathSample>& S = NetPath(L, Id);
				for (int32 i = 20; i < S.Num() - 20; i += 3)
				{
					const double D = FVector2D::Distance(S[i].P, Center);
					// Uniones lejos de lo prohibido (torres, cruces, agua): el enlace en "Y" no lo pisa.
					if (D > Blank + 30000.0 || G.Wall[G.Index(G.CellOf(S[i].P))] < 4000.0f || !TrailAttachable(L, S, i)) { continue; }
					FEnd E;
					E.Path = Id;
					E.Index = i;
					E.P = S[i].P;
					E.Progress = NetProgress(L, Id, i);
					E.Dist = D;
					E.Angle = AngleOf(S[i].P - Center);
					E.Module = S[i].Module;
					Ends.Add(E);
				}
			}

			// Parejas separadas por el recorrido, sin atajar demasiado. En lados opuestos del hueco la
			// senda pasa por su centro; si no, por dos puntos que se adentran en él (lazo en "U").
			struct FPair { int32 A = 0; int32 B = 0; double Score = 0.0; FVector2D Via1 = FVector2D::ZeroVector; FVector2D Via2 = FVector2D::ZeroVector; };
			const double Hw = 450.0;
			const double Clear = Hw + 1600.0 + 700.0;
			TArray<int32> Comp;
			G.Components(Clear, Comp);
			const int32 CenterComp = Comp[BestIdx];
			TArray<FPair> Pairs;
			for (int32 a = 0; a < Ends.Num(); ++a)
			{
				for (int32 b = a + 1; b < Ends.Num(); ++b)
				{
					const FEnd& Ea = Ends[a];
					const FEnd& Eb = Ends[b];
					if (bLinks && Ea.Path == 0 && Eb.Path == 0) { continue; }
					const double Ang = FMath::Abs(WrapAngle(Ea.Angle - Eb.Angle));
					const double Chord = FVector2D::Distance(Ea.P, Eb.P);
					const bool bCross = Ang >= FMath::DegreesToRadians(65.0);
					if (Chord < (bCross ? 9000.0 : 12000.0)) { continue; }
					const double DP = FMath::Abs(Ea.Progress - Eb.Progress);
					if (DP < 15000.0) { continue; }
					if (Ea.Path == Eb.Path && FMath::Abs(NetPath(L, Ea.Path)[Ea.Index].S - NetPath(L, Eb.Path)[Eb.Index].S) < 15000.0) { continue; }
					FVector2D Via1 = Center, Via2 = Center;
					double Depth = 0.0;
					if (!bCross)
					{
						const FVector2D Mid = (Ea.P + Eb.P) * 0.5;
						Depth = FMath::Min3(FVector2D::Distance(Mid, Center), 8000.0 + 0.6 * Chord, 32000.0);
						const FVector2D In = (Center - Mid).GetSafeNormal();
						Via1 = Ea.P + (Eb.P - Ea.P) * 0.25 + In * Depth;
						Via2 = Ea.P + (Eb.P - Ea.P) * 0.75 + In * Depth;
						if (Comp[G.Index(G.CellOf(Via1))] != CenterComp || Comp[G.Index(G.CellOf(Via2))] != CenterComp) { continue; }
					}
					const double Est = FVector2D::Distance(Ea.P, Via1) + FVector2D::Distance(Via1, Via2) + FVector2D::Distance(Via2, Eb.P);
					if (Est < 0.3 * DP) { continue; }
					FPair Pr;
					Pr.A = a;
					Pr.B = b;
					Pr.Via1 = Via1;
					Pr.Via2 = Via2;
					// Preferencias: que llene el hueco (cruzándolo de lado a lado o, si no, adentrándose en
					// él) uniendo zonas distintas, sin atajar demasiado ni alargarse sin necesidad.
					Pr.Score = 0.1 * Est + 0.8 * FMath::Max(0.0, 0.6 * DP - Est) - (bCross ? Blank + 8000.0 : Depth)
						- (Ea.Module != Eb.Module ? 6000.0 : 0.0) - (L.Modules[Ea.Module].Biome != L.Modules[Eb.Module].Biome ? 4000.0 : 0.0);
					Pairs.Add(Pr);
				}
			}
			Pairs.Sort([](const FPair& X, const FPair& Y) { return X.Score < Y.Score; });

			int32 Tries = 0;
			TArray<FVector2D> TriedEnds;
			for (const FPair& Pr : Pairs)
			{
				if (Tries >= 5) { break; }
				FEnd Ea = Ends[Pr.A];
				FEnd Eb = Ends[Pr.B];
				if (Ea.Progress > Eb.Progress) { Swap(Ea, Eb); }
				bool bSeen = false;
				for (int32 t = 0; t + 1 < TriedEnds.Num(); t += 2)
				{
					if (FVector2D::Distance(TriedEnds[t], Ea.P) < 4000.0 && FVector2D::Distance(TriedEnds[t + 1], Eb.P) < 4000.0) { bSeen = true; break; }
				}
				if (bSeen) { continue; }
				TriedEnds.Add(Ea.P);
				TriedEnds.Add(Eb.P);
				++Tries;

				FVector2D D0, T0, D1, T1;
				if (!TrailDeparture(L, G, Net, Ea.Path, Ea.Index, 1.0, Pr.Via1, Clear, Hw, D0, T0)) { continue; }
				if (!TrailDeparture(L, G, Net, Eb.Path, Eb.Index, -1.0, Pr.Via2, Clear, Hw, D1, T1)) { continue; }
				// Salidas en la misma bolsa de terreno libre que el hueco (si no, el A* no llega).
				if (Comp[G.Index(G.CellOf(D0))] != CenterComp || Comp[G.Index(G.CellOf(D1))] != CenterComp) { continue; }
				const uint32 NSeed = P.Seed ^ Hash32(static_cast<uint32>(Round) * 7919u + static_cast<uint32>(Tries) * 104729u + (bLinks ? 77u : 0u));
				TArray<FVector2D> Cells, Leg;
				bool bRouted = FindTrail(G, D0, Pr.Via1, Clear, NSeed, Cells);
				if (bRouted && Pr.Via2 != Pr.Via1)
				{
					bRouted = FindTrail(G, Pr.Via1, Pr.Via2, Clear, NSeed, Leg);
					Cells.Pop();
					Cells.Append(Leg);
				}
				bRouted = bRouted && FindTrail(G, Pr.Via2, D1, Clear, NSeed, Leg);
				if (!bRouted) { continue; }
				Cells.Pop();
				Cells.Append(Leg);
				// Sin idas y vueltas: si la ruta vuelve junto a donde ya pasó (entrar y salir de un punto
				// de paso por el mismo sitio), se ataja el bucle.
				for (int32 i = 0; i < Cells.Num(); ++i)
				{
					for (int32 j = Cells.Num() - 1; j > i + 1; --j)
					{
						if (FVector2D::DistSquared(Cells[i], Cells[j]) < 1.5 * G.Cell * 1.5 * G.Cell) { Cells.RemoveAt(i + 1, j - i - 1); break; }
					}
				}
				if (Cells.Num() < 8) { continue; }

				// Unión en "Y" a cada camino (Hermite) y ruta por el hueco con meandros; suavizado y
				// remuestreo. Si los meandros no caben, se prueba con meandros más suaves.
				const FVector2D DirA = NetPath(L, Ea.Path)[Ea.Index].Dir;
				const FVector2D DirB = NetPath(L, Eb.Path)[Eb.Index].Dir;
				TArray<FVector2D> Pts;
				bool bValid = false;
				for (const double Amp : { 1.0, 0.4 })
				{
					const TArray<FVector2D> Route = TrailControl(G, Cells, Clear, NSeed + 3u, Amp);
					if (Route.Num() < 3) { break; }
					const FVector2D R0 = (Route[FMath::Min(2, Route.Num() - 1)] - D0).GetSafeNormal();
					const FVector2D R1 = (D1 - Route[FMath::Max(0, Route.Num() - 3)]).GetSafeNormal();
					TArray<FVector2D> Ctrl;
					AppendHermite(Ea.P, (DirA + T0).GetSafeNormal(), D0, (T0 + R0).GetSafeNormal(), Ctrl);
					for (int32 k = 0; k < Route.Num() - 1; ++k) { Ctrl.Add(Route[k]); }
					AppendHermite(D1, (R1 - T1).GetSafeNormal(), Eb.P, (DirB - T1).GetSafeNormal(), Ctrl);
					Ctrl.Add(Eb.P);
					Pts = ResamplePolyline(ChaikinSmooth(Ctrl, 3), P.SampleSpacing);
					Pts[0] = Ea.P;
					Pts.Last() = Eb.P;
					if (ValidateTrail(L, G, Net, Pts, Ea.Path, Ea.Index, Eb.Path, Eb.Index, Hw)) { bValid = true; break; }
				}
				if (!bValid) { continue; }

				int32 ForkS = Ea.Index, JoinS = Eb.Index;
				if (Ea.Path != 0) { MainPointAt(L.Main, Ea.Progress, nullptr, &ForkS); }
				if (Eb.Path != 0) { MainPointAt(L.Main, Eb.Progress, nullptr, &JoinS); }
				if (JoinS <= ForkS) { continue; }

				FBranch Br;
				Br.ForkSample = ForkS;
				Br.RejoinSample = JoinS;
				Br.FromBranch = Ea.Path - 1;
				Br.FromSample = Ea.Path == 0 ? INDEX_NONE : Ea.Index;
				Br.ToBranch = Eb.Path - 1;
				Br.ToSample = Eb.Path == 0 ? INDEX_NONE : Eb.Index;
				Br.Side = FVector2D::CrossProduct(DirA, D0 - Ea.P) >= 0.0 ? 1 : -1;
				Br.Samples = MakeBranchSamples(L, Pts, ForkS);
				const bool bShortcut = Br.Samples.Last().S < 0.6 * (Eb.Progress - Ea.Progress);
				const double UChar = Rng.Unit();
				TrailWidths(L, Net, Br.Samples, Ea.Path, Ea.Index, Eb.Path, Eb.Index, bShortcut || UChar < 0.4 ? 0 : (UChar < 0.8 ? 1 : 2), Rng);
				if (!TrailZ(L, Br.Samples, Ea.Path, Ea.Index, Eb.Path, Eb.Index, Rng)) { continue; }
				Br.Kind = bShortcut ? EBranchKind::Risky : (bLinks ? EBranchKind::Link : EBranchKind::Trail);

				for (const FPathSample& Sm : Br.Samples) { L.Modules[Sm.Module].bHasBranch = true; }
				MarkJunction(L, Ea.Path, Ea.Index);
				MarkJunction(L, Eb.Path, Eb.Index);
				L.Branches.Add(Br);
				Net.Add(L, L.Branches.Num());
				G.Stamp(L.Branches.Last().Samples);
				G.Relax();
				++Placed;
				break;
			}
		}
		return Placed;
	}

	/**
	 * Bifurcaciones: primero los carriles del 2vs2; luego la red de sendas (un 40 % de
	 * NumBranches de sendas largas entre zonas del principal por los huecos del mapa y un 25 %
	 * de enlaces que salen también de ellas); y por último NumBranches ramas dentro de los
	 * módulos, que se separan y vuelven a unirse (1..BranchMaxModules módulos), de varios
	 * tipos: alternativa tranquila y holgada, cornisa estrecha con más huecos, ruta alta (sube
	 * poco a poco y baja en tobogán) y rodeo corto.
	 */
	inline void BuildBranches(FLayout& L, FRng Rng)
	{
		using namespace PathDetail;
		const FGenParams& P = L.Params;
		L.Branches.Reset();
		if (L.Main.Num() < 20) { return; }

		FSampleGrid Grid;
		Grid.Build(L.Main, L.MaxExtent());
		const double Total = L.MainLength();
		const uint32 BSeed = P.Seed ^ 0xB4A2Cu;
		const double TanSlide = FMath::Tan(FMath::DegreesToRadians(P.SlideAngleDeg));

		TArray<FVector2D> Towers;
		for (const FCrossing& C : L.Crossings)
		{
			Towers.Add(L.Main[L.Route[C.HighStep].FirstSample].P);
			Towers.Add(L.Main[L.Route[C.HighStep].LastSample].P);
		}

		auto PlaceInModule = [&](int32 b, bool bLane)
		{
			EBranchKind Kind = bLane ? EBranchKind::Lane : PickBranchKind(Rng);
			for (int32 Attempt = 0; Attempt < 80; ++Attempt)
			{
				// Si un tipo no cabe, se prueba otro largo; el rodeo corto queda como último recurso.
				if (!bLane && Attempt == 40) { Kind = PickBranchKind(Rng, true); }
				if (!bLane && Attempt == 65) { Kind = EBranchKind::Bypass; }
				const FBranchShape Shape = BranchShapeOf(Kind);
				const double SegLen = Rng.Range(Shape.LenMin, Shape.LenMax);
				const double S0 = Rng.Range(9000.0, FMath::Max(9001.0, Total - SegLen - 9000.0));
				int32 I0 = INDEX_NONE, I1 = INDEX_NONE;
				MainPointAt(L.Main, S0, nullptr, &I0);
				MainPointAt(L.Main, S0 + SegLen, nullptr, &I1);
				if (I0 == INDEX_NONE || I1 <= I0 + 8) { continue; }

				// Tramo principal elegible: sin estructuras, sin agua, pocos módulos.
				bool bEligible = true;
				TArray<int32> Modules;
				for (int32 i = I0; i <= I1 && bEligible; ++i)
				{
					const FPathSample& Sm = L.Main[i];
					if ((Sm.Flags & (PathFlags::Special & ~(PathFlags::Gap | PathFlags::Portal | PathFlags::Junction))) != 0) { bEligible = false; }
					if (IsWetBiome(Sm.Biome) || IsCrossingModule(L, Sm.Module)) { bEligible = false; }
					if ((Sm.Flags & PathFlags::Lane) != 0) { bEligible = false; }
					if (!Modules.Contains(Sm.Module)) { Modules.Add(Sm.Module); }
				}
				if (!bEligible || Modules.Num() > P.BranchMaxModules || !ForksClear(L, I0, I1)) { continue; }

				const int32 Side = Rng.Chance(0.5) ? 1 : -1;
				const double Amp = Rng.Range(Shape.AmpMin, Shape.AmpMax);
				const double BranchW = Rng.Range(Shape.WMin, Shape.WMax);
				// Panza simétrica o cargada hacia la horquilla o hacia la unión.
				const double Skew = bLane ? 1.0 : Rng.Range(0.6, 1.6);

				TArray<FVector2D> Ctrl;
				const int32 NumCtrl = 36;
				for (int32 k = 0; k < NumCtrl; ++k)
				{
					const double T = static_cast<double>(k) / (NumCtrl - 1);
					FVector2D Dir;
					const FVector2D Base = MainPointAt(L.Main, L.Main[I0].S + T * (L.Main[I1].S - L.Main[I0].S), &Dir);
					const double Sin = FMath::Sin(Pi * FMath::Pow(T, Skew));
					const double Off = Amp * FMath::Pow(FMath::Max(0.0, Sin), 0.6) * (1.0 + 0.25 * Noise1(BSeed + static_cast<uint32>(b * 31 + Attempt), T * 4.0));
					Ctrl.Add(Base + LeftNormal(Dir) * (Off * Side));
				}
				Ctrl[0] = L.Main[I0].P;
				Ctrl.Last() = L.Main[I1].P;
				const TArray<FVector2D> Pts = ResamplePolyline(ChaikinSmooth(Ctrl, 2), P.SampleSpacing);
				if (!ValidateBranchPolyline(L, Pts, I0, I1, BranchW, Grid, Towers)) { continue; }

				FBranch Br;
				Br.ForkSample = I0;
				Br.RejoinSample = I1;
				Br.Side = Side;
				Br.Kind = Kind;
				Br.Samples = MakeBranchSamples(L, Pts, I0);
				for (int32 k = 0; k < Br.Samples.Num(); ++k)
				{
					Br.Samples[k].Width = BranchW * (bLane ? 1.0 : (0.85 + 0.3 * (0.5 + 0.5 * Noise1(BSeed + 7u, static_cast<double>(k) / 20.0))));
					if (bLane) { Br.Samples[k].Flags |= PathFlags::Lane; }
				}

				// Alturas: se une en ambos extremos a la cota del principal. La ruta alta sube con
				// pendiente suave desde que sale del cauce principal, sigue por lo alto y baja en
				// tobogán hasta antes de volver a tocarlo (dentro del cauce manda el suelo del
				// principal: subir o bajar ahí dejaría escalones); las demás ondulan.
				const double Z0 = L.Main[I0].Z;
				const double Z1 = L.Main[I1].Z;
				const double BLen = FMath::Max(1.0, Br.Samples.Last().S);
				const bool bHigh = Kind == EBranchKind::High;
				const double Peak = bHigh ? Rng.Range(1000.0, 1800.0) : 0.0;
				const double Climb = P.MaxPathSlope * 0.7;
				double SClear0 = BLen, SClear1 = 0.0;
				if (bHigh)
				{
					for (const FPathSample& Sm : Br.Samples)
					{
						double DMain = 0.0;
						const int32 Near = Grid.Nearest(Sm.P, 20000.0, DMain);
						const double Sep = Near == INDEX_NONE ? 1e9 : DMain - L.Main[Near].Width * 0.5;
						if (Sep > Sm.Width * 0.5 + 600.0) { SClear0 = FMath::Min(SClear0, Sm.S); SClear1 = FMath::Max(SClear1, Sm.S); }
					}
				}
				TArray<double> Z;
				for (const FPathSample& Sm : Br.Samples)
				{
					const double T = Sm.S / BLen;
					const double Base = LerpD(Z0, Z1, T);
					if (bHigh)
					{
						const double Up = Sm.S - SClear0;
						const double Down = SClear1 - Sm.S;
						Z.Add(Base + (Up > 0.0 && Down > 0.0 ? FMath::Min3(Peak, Climb * Up, TanSlide * Down) : 0.0));
					}
					else { Z.Add(Base + Shape.Wave * Fbm1(BSeed + 13u, Sm.S / 12000.0, 2) * FMath::Sin(Pi * T)); }
				}
				if (!bHigh) { Z = SmoothScalars(Z, 6); }
				Z[0] = Z0;
				Z.Last() = Z1;
				PathDetail::HoldBranchToMain(L, Br.Samples, I0, I1, Z);
				bool bSlopeOk = true;
				double MaxRise = 0.0;
				for (int32 k = 1; k < Z.Num(); ++k)
				{
					const double Ds = FMath::Max(1.0, Br.Samples[k].S - Br.Samples[k - 1].S);
					const double Slope = (Z[k] - Z[k - 1]) / Ds;
					// La ruta alta baja en tobogán: solo su subida tiene que ser una rampa andable.
					if (bHigh ? Slope > P.MaxPathSlope * 1.2 : FMath::Abs(Slope) > P.MaxPathSlope * 1.6) { bSlopeOk = false; break; }
					MaxRise = FMath::Max(MaxRise, Z[k] - LerpD(Z0, Z1, Br.Samples[k].S / BLen));
				}
				// Si no llega a subir de verdad no es ruta alta (el tramo es demasiado corto).
				if (!bSlopeOk || (bHigh && MaxRise < 700.0)) { continue; }
				for (int32 k = 0; k < Z.Num(); ++k)
				{
					Br.Samples[k].Z = Z[k];
					if (bHigh && k > 0 && (Z[k - 1] - Z[k]) / FMath::Max(1.0, Br.Samples[k].S - Br.Samples[k - 1].S) > P.MaxPathSlope * 1.5)
					{
						Br.Samples[k].Flags |= PathFlags::Slide;
					}
				}

				for (const FPathSample& Sm : Br.Samples) { L.Modules[Sm.Module].bHasBranch = true; }
				if (bLane)
				{
					for (int32 i = I0; i <= I1; ++i) { L.Main[i].Flags |= PathFlags::Lane; L.Main[i].Width = FMath::Min(L.Main[i].Width, 900.0); }
				}
				L.Branches.Add(Br);
				return;
			}
		};

		for (int32 b = 0; b < FMath::Max(0, P.NumLanes); ++b) { PlaceInModule(b, true); }
		{
			FTrailGrid TrailGrid;
			TrailGrid.Build(L, Towers);
			FNetGrid Net;
			Net.Init(L.MaxExtent());
			for (int32 Id = 0; Id <= L.Branches.Num(); ++Id)
			{
				Net.Add(L, Id);
				TrailGrid.Stamp(NetPath(L, Id));
			}
			TrailGrid.Relax();
			const int32 NB = FMath::Max(0, P.NumBranches);
			BuildTrails(L, Rng, TrailGrid, Net, FMath::RoundToInt(NB * 0.4), false);
			BuildTrails(L, Rng, TrailGrid, Net, FMath::RoundToInt(NB * 0.25), true);
		}
		for (int32 b = 0; b < FMath::Max(0, P.NumBranches); ++b) { PlaceInModule(P.NumLanes + b, false); }
		// Uniones de las ramas con el principal: sin huecos, obstáculos ni peligros encima.
		for (const FBranch& B : L.Branches)
		{
			if (B.Kind == EBranchKind::Lane) { continue; }
			if (B.FromBranch == INDEX_NONE) { MarkJunction(L, 0, B.ForkSample); }
			if (B.ToBranch == INDEX_NONE) { MarkJunction(L, 0, B.RejoinSample); }
		}
	}
}
