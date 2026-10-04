#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapGenerate.h"

/**
 * Mapa de Supervivencia (#273, Docs/Mapa_Supervivencia.md): el generador del Coop con un perfil propio. Un mapa
 * alargado de unos 120 × 300 m (2 × 5 módulos de 60 m), la salida al sur y la meta al norte, sin pasadas de cruce,
 * poco sinuoso y con la dificultad de entrada 1–5 (el nivel N de la partida pide min(N, 5)). El Coop no cambia.
 */

namespace TNProcMap
{
	constexpr int32 SurvivalMinDifficulty = 1;
	constexpr int32 SurvivalMaxDifficulty = 5;
	/** Cota del agua en el formato del banco (WATER_M de Scripts/terrain_vol): el mar del generador está a 0. */
	constexpr double SurvivalBenchWaterM = -4.0;
	/** Ancho (cm) de la ventana que mide el banco y en la que debe caber el camino (Scripts/terrain_survival/spec.py). */
	constexpr double SurvivalWindowWidth = 11000.0;

	/** Parámetros del mapa de Supervivencia para una semilla y una dificultad 1–5 (GenerateLayout los sanea). */
	inline FGenParams MakeSurvivalParams(uint32 Seed, int32 Difficulty)
	{
		const int32 D = FMath::Clamp(Difficulty, SurvivalMinDifficulty, SurvivalMaxDifficulty);
		const double T = static_cast<double>(D - SurvivalMinDifficulty) / (SurvivalMaxDifficulty - SurvivalMinDifficulty);

		FGenParams P;
		P.Seed = Seed;
		P.GridSize = 5;
		P.GridSizeX = 2;
		P.ModuleSize = 6000.0;
		// El trazador de cada módulo está hecho para módulos de 400 m: sus pasos y márgenes, a la escala de 40 m
		// (con un radio de giro que no baje del ancho del camino).
		P.WalkScale = 0.45;
		P.CellSize = 200.0;
		P.SampleSpacing = 200.0;

		// Más o menos lineal: la ruta avanza hacia la meta, sin cruces ni lazos, y el camino serpentea poco.
		// Más dificultad = el principal recorre más módulos (de 5 a 8 de 10) y serpentea más.
		P.Coverage = LerpD(0.5, 0.8, T);
		P.bMonotonicRoute = true;
		P.NumCrossings = 0;
		P.Sinuosity = LerpD(1.1, 1.5, T);
		P.NumLanes = 0;
		P.NumBranches = 1 + D;
		P.BranchMaxModules = 1;
		P.bRiver = false;
		// Módulos a alturas parecidas y sin toboganes ni géiseres entre ellos: los desniveles van en rampa.
		P.LevelSpread = 0.5;
		P.SmoothTransitionMax = 4000.0;
		P.MinPathZ = 150.0;
		P.NumBiomeRegions = 2;
		// Sin isletas ni pasarelas (su suelo son mallas sobre el agua) y el camino lejos de los bordes largos.
		P.bWetBiomes = false;
		P.SideMargin = 1600.0;

		// El camino nunca baja de 3 m (la especificación); la dificultad lo estrecha y pone más huecos y más largos.
		P.PathWidthMin = LerpD(650.0, 450.0, T);
		P.PathWidthMax = LerpD(1300.0, 900.0, T);
		P.PortalWidthMin = P.PathWidthMin;
		P.PortalWidthMax = P.PathWidthMax;
		P.NarrowChance = LerpD(0.1, 0.35, T);
		P.GapsPerKm = LerpD(10.0, 30.0, T);
		P.GapMax = LerpD(260.0, 390.0, T);
		P.Difficulty01 = T;
		P.EggNestEveryNPortals = 2;
		// La salida y la meta a unos 12 m de los extremos: claro de salida pequeño y la costa unos 8 m pasado el
		// borde norte, para que la orilla quede al final del mapa. SanitizeParams reduce a la mitad los dos en
		// módulos pequeños, y se aplica una sola vez, en GenerateLayout.
		P.StartClearingRadius = 1000.0;
		P.CoastInset = -1600.0;
		return P;
	}

	/**
	 * Si el camino principal se pliega sobre sí mismo: dos tramos que se solapan en planta con más desnivel que
	 * separación (más de 45°) dejan un escalón entre ellos (en módulos de 80 m el trazador a veces da media vuelta).
	 * En una curva normal no pasa: el desnivel lo limita la pendiente del camino (MaxPathSlope).
	 */
	inline bool SurvivalPathFolds(const FLayout& L)
	{
		const TArray<FPathSample>& M = L.Main;
		for (int32 i = 0; i < M.Num(); ++i)
		{
			for (int32 j = i + 1; j < M.Num(); ++j)
			{
				const double Apart = FVector2D::Distance(M[i].P, M[j].P);
				if (Apart < 0.5 * (M[i].Width + M[j].Width) && FMath::Abs(M[i].Z - M[j].Z) > FMath::Max(100.0, Apart)) { return true; }
			}
		}
		return false;
	}

	/** Si cada rama empieza y acaba a la cota del camino del que sale y al que llega (sin bordillo en la unión). */
	inline bool SurvivalBranchesFlush(const FLayout& L)
	{
		auto ZOf = [&L](int32 Branch, int32 BranchSample, int32 MainSample)
		{
			return Branch == INDEX_NONE ? L.Main[MainSample].Z : L.Branches[Branch].Samples[BranchSample].Z;
		};
		for (const FBranch& B : L.Branches)
		{
			if (B.Samples.Num() < 2) { continue; }
			if (FMath::Abs(B.Samples[0].Z - ZOf(B.FromBranch, B.FromSample, B.ForkSample)) > 50.0
				|| FMath::Abs(B.Samples.Last().Z - ZOf(B.ToBranch, B.ToSample, B.RejoinSample)) > 50.0)
			{
				return false;
			}
		}
		return true;
	}

	/** Si ninguna zanja de hueco de salto toca la salida ni la meta (las zanjas siguen 15 m a cada lado del camino). */
	inline bool SurvivalEndsClear(const FLayout& L)
	{
		const FVector2D Goal = L.Main.Num() > 0 ? L.Main.Last().P : L.EndPoint;
		for (const FFeature& F : L.Features)
		{
			if (F.Type != EFeature::Gap) { continue; }
			for (const FVector2D& P : { L.StartPoint, L.EndPoint, Goal })
			{
				const FVector2D Rel = P - FVector2D(F.Location.X, F.Location.Y);
				if (FMath::Abs(FVector2D::DotProduct(Rel, F.Dir)) <= F.Height * 0.5 + 300.0
					&& FMath::Abs(FVector2D::DotProduct(Rel, LeftNormal(F.Dir))) <= F.Width * 0.5 + GapTrenchSideOf(F) + 300.0)
				{
					return false;
				}
			}
		}
		return true;
	}

	/** Si el camino principal cabe entero en los 110 m centrales del ancho (lo que se mide y lo que se juega). */
	inline bool SurvivalPathInside(const FLayout& L)
	{
		const double Margin = 0.5 * (L.WorldSizeX - SurvivalWindowWidth) + 100.0;
		for (const FPathSample& S : L.Main)
		{
			if ((S.Flags & PathFlags::Shore) != 0) { continue; }   // la playa final se abre en abanico hasta el agua
			const double Half = 0.5 * S.Width;
			if (S.P.X - Half < Margin || S.P.X + Half > L.WorldSizeX - Margin) { return false; }
		}
		return true;
	}

	/**
	 * Genera el mapa de Supervivencia. Si sale un camino plegado, prueba la siguiente semilla de una secuencia fija
	 * (la misma en todas las máquinas). Devuelve la semilla usada, o 0 si ninguna sirvió.
	 */
	inline uint32 GenerateSurvivalLayout(uint32 Seed, int32 Difficulty, FLayout& Out)
	{
		for (int32 Attempt = 0; Attempt < 12; ++Attempt)
		{
			const uint32 Try = Seed + static_cast<uint32>(Attempt) * 7919u;
			if (GenerateLayout(MakeSurvivalParams(Try, Difficulty), Out) && Out.bValid && !SurvivalPathFolds(Out) && SurvivalPathInside(Out)
				&& SurvivalBranchesFlush(Out) && SurvivalEndsClear(Out))
			{
				return Try;
			}
		}
		Out.bValid = false;
		Out.FailReason = "Supervivencia: sin mapa valido en 12 semillas";
		return 0;
	}

	/**
	 * Alturas del mapa en el formato del banco (Scripts/terrain_survival/mapa.py): una muestra por metro, filas =
	 * ancho (X del mapa) y columnas = avance (Y), así que la salida queda al oeste y la meta al este. Una ventana de
	 * 110 × 300 m centrada en el ancho: lo que sobra a los lados es muro del borde. Los huecos de salto van aparte
	 * (Jumps): el banco los cruza saltando si el salto más largo cabe en el dive (decisión pendiente en #273).
	 */
	struct FSurvivalTop
	{
		static constexpr int32 Rows = 111;
		static constexpr int32 Cols = 301;
		/** Cota (m) de cada muestra, fila a fila: Top[Row * Cols + Col]. */
		TArray<float> Top;
		/** (fila, columna) de la salida y de la meta. */
		FIntPoint Start = FIntPoint::ZeroValue;
		FIntPoint Goal = FIntPoint::ZeroValue;
		/** Un hueco de salto: (fila, columna) de un borde y del otro, y el salto más largo para cruzarlo (m). */
		struct FJump { FIntPoint From; FIntPoint To; double LeapM = 0.0; };
		TArray<FJump> Jumps;
		/** Camino principal (diagnóstico): fila, columna, cota (m), ancho (m) y flags de cada muestra. */
		TArray<double> MainPath;
		/** Ramas (diagnóstico): rama, fila, columna, cota (m) y ancho (m) de cada muestra. */
		TArray<double> BranchPaths;
	};

	/** Salto más largo (cm) para cruzar un hueco: entero en los de borde y de panzazo, entre filas en los de postes. */
	inline double GapLongestLeap(const FLayout& L, const FFeature& F)
	{
		switch (GapStyleOf(F))
		{
			case EGapStyle::Beam: return 0.0;   // la viga se cruza andando
			case EGapStyle::Posts:
			{
				// Mismo reparto de filas que los postes del actor (TN_ProcMapGenerator_Build.cpp).
				const double MaxJump = LerpD(FMath::Min(L.Params.GapMax, 200.0), L.Params.GapMax, Saturate(L.Params.Difficulty01)) * 0.8;
				const int32 Rows = FMath::Max(1, FMath::CeilToInt(F.Length / FMath::Max(150.0, MaxJump)) - 1);
				return F.Length / (Rows + 1);
			}
			default: return F.Length;
		}
	}

	inline void SampleSurvivalTop(const FLayout& L, FSurvivalTop& Out)
	{
		const double Spacing = 100.0;
		const FVector2D Origin((L.WorldSizeX - (FSurvivalTop::Rows - 1) * Spacing) * 0.5, 0.0);
		// El mallado del terreno va en X (ancho) por filas de Y: NX = filas del banco, NY = columnas.
		FTerrainBuilder TB;
		TB.Build(L, Origin, Spacing, FSurvivalTop::Rows, FSurvivalTop::Cols);
		TArray<float> H;
		TArray<uint8> Mask;
		H.SetNum(FSurvivalTop::Rows * FSurvivalTop::Cols);
		Mask.SetNum(H.Num());
		TB.ComputeRows(0, FSurvivalTop::Cols, H, Mask);

		Out.Top.SetNum(H.Num());
		for (int32 iy = 0; iy < FSurvivalTop::Cols; ++iy)
		{
			for (int32 ix = 0; ix < FSurvivalTop::Rows; ++ix)
			{
				Out.Top[ix * FSurvivalTop::Cols + iy] = static_cast<float>(H[iy * FSurvivalTop::Rows + ix] / 100.0 + SurvivalBenchWaterM);
			}
		}
		auto ToIndex = [&Origin, Spacing](const FVector2D& P)
		{
			return FIntPoint(FMath::Clamp(FMath::RoundToInt((P.X - Origin.X) / Spacing), 0, FSurvivalTop::Rows - 1),
				FMath::Clamp(FMath::RoundToInt((P.Y - Origin.Y) / Spacing), 0, FSurvivalTop::Cols - 1));
		};
		Out.Start = ToIndex(L.StartPoint);
		// Meta: el último punto seco del camino, en la playa (la línea de llegada ya está dentro del agua).
		FVector2D Goal = L.EndPoint;
		for (int32 i = L.Main.Num() - 1; i >= 0; --i)
		{
			if (L.Main[i].Z > SeaLevel + 30.0 && (L.Main[i].Flags & PathFlags::Gap) == 0) { Goal = L.Main[i].P; break; }
		}
		Out.Goal = ToIndex(Goal);
		Out.MainPath.Reset();
		for (const FPathSample& S : L.Main)
		{
			const FVector2D RC = (S.P - Origin) / Spacing;
			Out.MainPath.Append({ RC.X, RC.Y, S.Z / 100.0 + SurvivalBenchWaterM, S.Width / 100.0, static_cast<double>(S.Flags) });
		}
		Out.BranchPaths.Reset();
		for (int32 b = 0; b < L.Branches.Num(); ++b)
		{
			for (const FPathSample& S : L.Branches[b].Samples)
			{
				const FVector2D RC = (S.P - Origin) / Spacing;
				Out.BranchPaths.Append({ static_cast<double>(b), RC.X, RC.Y, S.Z / 100.0 + SurvivalBenchWaterM, S.Width / 100.0 });
			}
		}

		// Huecos de salto: los labios (mallas del actor, no terreno) reducen la zanja al hueco exacto. Se estampan a
		// la cota del camino, desde el borde del salto hasta pasada la zanja, y el salto va de labio a labio.
		Out.Jumps.Reset();
		for (const FFeature& F : L.Features)
		{
			if (F.Type != EFeature::Gap) { continue; }
			const FVector2D C(F.Location.X, F.Location.Y);
			const FVector2D N = LeftNormal(F.Dir);
			const double Inner = F.Length * 0.5;
			const double Outer = F.Height * 0.5 + 150.0;
			const float LipM = static_cast<float>(F.Location.Z / 100.0 + SurvivalBenchWaterM);
			const double Reach = Outer + F.Width * 0.5;
			const FIntPoint Lo = ToIndex(C - FVector2D(Reach, Reach));
			const FIntPoint Hi = ToIndex(C + FVector2D(Reach, Reach));
			for (int32 Row = Lo.X; Row <= Hi.X; ++Row)
			{
				for (int32 Col = Lo.Y; Col <= Hi.Y; ++Col)
				{
					const FVector2D Rel = Origin + FVector2D(Row * Spacing, Col * Spacing) - C;
					const double Along = FMath::Abs(FVector2D::DotProduct(Rel, F.Dir));
					if (Along >= Inner && Along <= Outer && FMath::Abs(FVector2D::DotProduct(Rel, N)) <= F.Width * 0.5)
					{
						float& T = Out.Top[Row * FSurvivalTop::Cols + Col];
						T = FMath::Max(T, LipM);
					}
				}
			}
			// Despegue y aterrizaje 2,2 m dentro del labio (mide al menos 3 m): el banco pide 3 m de suelo alrededor
			// del camino y en un hueco en diagonal la rejilla de 1 m acerca el borde.
			const FVector2D Half = F.Dir * (Inner + 220.0);
			Out.Jumps.Add({ ToIndex(C - Half), ToIndex(C + Half), GapLongestLeap(L, F) / 100.0 });
		}
	}

	/** Huella del layout: camino, ramas y elementos redondeados al centímetro. La usan el test del Coop sin
	 * cambios y el catálogo de Supervivencia (TN_SurvivalCatalog.h) para notar si el generador cambia. */
	inline uint64 LayoutFingerprint(const FLayout& L)
	{
		uint64 H = 1469598103934665603ull;
		auto Mix = [&H](int64 V)
		{
			for (int32 b = 0; b < 8; ++b) { H = (H ^ static_cast<uint64>((V >> (8 * b)) & 0xFF)) * 1099511628211ull; }
		};
		auto MixVec = [&Mix](const FVector2D& V) { Mix(FMath::RoundToInt64(V.X)); Mix(FMath::RoundToInt64(V.Y)); };
		Mix(FMath::RoundToInt64(L.WorldSize));
		for (const FRouteStep& St : L.Route) { Mix(St.Module); }
		for (const FPathSample& S : L.Main)
		{
			MixVec(S.P); Mix(FMath::RoundToInt64(S.Z)); Mix(FMath::RoundToInt64(S.Width)); Mix(S.Flags);
		}
		for (const FBranch& B : L.Branches)
		{
			Mix(B.Samples.Num()); Mix(static_cast<int64>(B.Kind)); Mix(B.ForkSample); Mix(B.RejoinSample);
			for (const FPathSample& S : B.Samples) { MixVec(S.P); Mix(FMath::RoundToInt64(S.Z)); }
		}
		for (const FFeature& F : L.Features)
		{
			Mix(static_cast<int64>(F.Type)); Mix(FMath::RoundToInt64(F.Location.X)); Mix(FMath::RoundToInt64(F.Location.Y));
			Mix(FMath::RoundToInt64(F.Location.Z));
		}
		return H;
	}
}
