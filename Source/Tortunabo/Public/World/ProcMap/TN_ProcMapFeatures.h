#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapLayout.h"
#include "World/ProcMap/TN_ProcMapPath.h"

/**
 * Elementos colocados sobre el camino y el terreno: estructura de los cruces
 * colosales, huecos de salto, isletas, pasarelas, pilas de huevos, puzles 2vs2,
 * río opcional, pozas de lava e islas. Todo determinista a partir del FRng.
 */

namespace TNProcMap
{
	namespace FeatureDetail
	{
		inline bool AnyFlag(const TArray<FPathSample>& Samples, int32 From, int32 To, uint32 Mask)
		{
			for (int32 i = FMath::Max(0, From); i <= FMath::Min(Samples.Num() - 1, To); ++i)
			{
				if ((Samples[i].Flags & Mask) != 0) { return true; }
			}
			return false;
		}

		inline FFeature MakeAtSample(EFeature Type, const FPathSample& Sm, int32 PathIndex, int32 BranchIndex)
		{
			FFeature F;
			F.Type = Type;
			F.Location = FVector(Sm.P, Sm.Z);
			F.Dir = Sm.Dir;
			F.Width = Sm.Width;
			F.PathIndex = PathIndex;
			F.BranchIndex = BranchIndex;
			F.Biome = Sm.Biome;
			return F;
		}

		/** Desenfoque de caja separable, Channels valores por celda, in-place. */
		inline void BoxBlur(TArray<float>& Data, int32 W, int32 H, int32 Channels, int32 Radius)
		{
			if (Radius <= 0) { return; }
			TArray<float> Tmp;
			Tmp.SetNum(Data.Num());
			for (int32 y = 0; y < H; ++y)
			{
				for (int32 x = 0; x < W; ++x)
				{
					for (int32 c = 0; c < Channels; ++c)
					{
						double Sum = 0.0;
						int32 N = 0;
						for (int32 k = FMath::Max(0, x - Radius); k <= FMath::Min(W - 1, x + Radius); ++k) { Sum += Data[(y * W + k) * Channels + c]; ++N; }
						Tmp[(y * W + x) * Channels + c] = static_cast<float>(Sum / N);
					}
				}
			}
			for (int32 y = 0; y < H; ++y)
			{
				for (int32 x = 0; x < W; ++x)
				{
					for (int32 c = 0; c < Channels; ++c)
					{
						double Sum = 0.0;
						int32 N = 0;
						for (int32 k = FMath::Max(0, y - Radius); k <= FMath::Min(H - 1, y + Radius); ++k) { Sum += Tmp[(k * W + x) * Channels + c]; ++N; }
						Data[(y * W + x) * Channels + c] = static_cast<float>(Sum / N);
					}
				}
			}
		}
	}

	/** Campos suaves de bioma, nivel y "módulo elevado" en un raster grueso. */
	inline void BuildBiomeFields(FLayout& L)
	{
		using namespace FeatureDetail;
		L.BiomeCell = 800.0;
		L.BiomeW = FMath::Max(2, FMath::CeilToInt(L.WorldSizeX / L.BiomeCell));
		L.BiomeH = FMath::Max(2, FMath::CeilToInt(L.WorldSize / L.BiomeCell));
		const int32 Cells = L.BiomeW * L.BiomeH;
		L.BiomeWeights.Init(0.0f, Cells * NumBiomes);
		L.LevelField.Init(0.0f, Cells);
		L.ElevatedField.Init(0.0f, Cells);
		for (int32 y = 0; y < L.BiomeH; ++y)
		{
			for (int32 x = 0; x < L.BiomeW; ++x)
			{
				const FVector2D C((x + 0.5) * L.BiomeCell, (y + 0.5) * L.BiomeCell);
				int32 Mod = L.ModuleAt(C);
				if (Mod == INDEX_NONE) { Mod = L.ModuleAt(FVector2D(FMath::Min(C.X, L.WorldSizeX - 1.0), FMath::Min(C.Y, L.WorldSize - 1.0))); }
				if (Mod == INDEX_NONE) { continue; }
				const FModule& M = L.Modules[Mod];
				const int32 Idx = y * L.BiomeW + x;
				L.BiomeWeights[Idx * NumBiomes + BiomeIndex(M.Biome)] = 1.0f;
				L.LevelField[Idx] = static_cast<float>(M.Level);
				// Todo módulo sin camino principal es un macizo: fuera del camino nada es transitable.
				L.ElevatedField[Idx] = M.VisitCount > 0 ? 0.0f : (M.bHasBranch && M.EmptyKind != ETNProcEmptyModuleMode::Elevated ? 0.6f : 1.0f);
			}
		}
		BoxBlur(L.BiomeWeights, L.BiomeW, L.BiomeH, NumBiomes, 4);
		BoxBlur(L.BiomeWeights, L.BiomeW, L.BiomeH, NumBiomes, 3);
		BoxBlur(L.LevelField, L.BiomeW, L.BiomeH, 1, 8);
		BoxBlur(L.LevelField, L.BiomeW, L.BiomeH, 1, 5);
		// Desenfoque amplio: el macizo culmina en el centro del módulo y baja en ladera hacia sus bordes.
		BoxBlur(L.ElevatedField, L.BiomeW, L.BiomeH, 1, 9);
		BoxBlur(L.ElevatedField, L.BiomeW, L.BiomeH, 1, 7);
	}

	/** Salida, meta, géiseres, toboganes, torres, tablero/mesa y techo de cueva. */
	inline void BuildStructuralFeatures(FLayout& L)
	{
		using namespace FeatureDetail;
		const FGenParams& P = L.Params;
		const TArray<FPathSample>& M = L.Main;

		{
			FFeature F = MakeAtSample(EFeature::StartArea, M[0], 0, INDEX_NONE);
			F.Location = FVector(L.StartPoint, M[0].Z);
			F.Radius = P.StartClearingRadius;
			L.Features.Add(F);
		}
		// Meta: la línea cruza toda la boca de la playa unos metros mar adentro, bajo el arco.
		{
			const double LineY = FinishLineY(L);
			int32 i = M.Num() - 1;
			for (int32 k = 0; k < M.Num(); ++k)
			{
				if ((M[k].Flags & PathFlags::Shore) != 0 && M[k].P.Y >= LineY) { i = k; break; }
			}
			const FPathSample& A = M[FMath::Max(0, i - 1)];
			const double Span = M[i].P.Y - A.P.Y;
			const double T = Span > 1.0 ? FMath::Clamp((LineY - A.P.Y) / Span, 0.0, 1.0) : 1.0;
			FFeature F = MakeAtSample(EFeature::Finish, M[i], i, INDEX_NONE);
			F.Location = FVector(M[i].P.X, LineY, LerpD(A.Z, M[i].Z, T));
			F.Target = FVector(M[i].P.X, FinishWaterY(L), 0.0);
			F.Dir = FVector2D(0.0, 1.0);
			F.Width = FinishBeachWidthAt(L, LineY);
			F.Length = FinishDims::TriggerDepth;
			F.Height = FinishBeachWidthAt(L, LineY + F.Length);
			// Arco de unos 25 m de luz, sin tocar los brazos de la playa.
			F.Radius = FMath::Clamp(0.5 * F.Width - 1000.0, 800.0, 1250.0);
			F.Aux = static_cast<int32>(Hash32(static_cast<uint32>(P.Seed) ^ 0xF1A15u) & 0x7fffffffu);
			L.Features.Add(F);
		}

		// Géiseres: aterrizan en la torre (cruce colosal) o pasado el escalón.
		for (int32 i = 0; i < M.Num(); ++i)
		{
			if ((M[i].Flags & PathFlags::GeyserBase) == 0) { continue; }
			FFeature F = MakeAtSample(EFeature::Geyser, M[i], i, INDEX_NONE);
			F.Target = F.Location;
			if ((M[i].Flags & PathFlags::UnderTower) != 0)
			{
				// Dentro de la torre de entrada: en su centro, al nivel del suelo. Lanza por el hueco del forjado y se
				// aterriza en la cima junto al hueco, hacia el puente o el adarve.
				for (int32 c = 0; c < L.Crossings.Num(); ++c)
				{
					const FRouteStep& High = L.Route[L.Crossings[c].HighStep];
					if (High.FirstSample <= i || High.FirstSample > i + 3) { continue; }
					const FVector2D Cp = M[High.FirstSample].P;
					const FVector2D Deck = (M[FMath::Min(High.FirstSample + 3, High.LastSample)].P - Cp).GetSafeNormal();
					F.Location = FVector(Cp, M[i].Z);
					F.Target = FVector(Cp + Deck * TowerDims::Land, L.Crossings[c].TopZ);
					F.Aux = c;
					F.Aux2 = TowerDims::HollowBit;
					break;
				}
				F.Height = F.Target.Z - F.Location.Z;
				L.Features.Add(F);
				continue;
			}
			for (int32 j = i + 1; j < M.Num(); ++j)
			{
				if ((M[j].Flags & PathFlags::UnderTower) != 0)
				{
					// Centro de la torre = muestra de portal (última de la zona bajo torre).
					int32 k = j;
					while (k + 1 < M.Num() && (M[k + 1].Flags & PathFlags::UnderTower) != 0) { ++k; }
					F.Target = FVector(M[k].P, M[k].Z);
					break;
				}
				if ((M[j].Flags & PathFlags::CliffUp) != 0)
				{
					int32 k = j;
					while (k + 1 < M.Num() && M[k].S - M[j].S < 900.0) { ++k; }
					F.Target = FVector(M[k].P, M[k].Z);
					break;
				}
				if (M[j].S - M[i].S > 6000.0) { break; }
			}
			F.Height = F.Target.Z - F.Location.Z;
			L.Features.Add(F);
		}

		// Toboganes: tramos contiguos con Slide (principal y rutas altas de las ramas).
		auto AddSlides = [&L](const TArray<FPathSample>& S, int32 BranchIndex)
		{
			for (int32 i = 0; i < S.Num(); ++i)
			{
				if ((S[i].Flags & PathFlags::Slide) == 0) { continue; }
				int32 j = i;
				while (j + 1 < S.Num() && (S[j + 1].Flags & PathFlags::Slide) != 0) { ++j; }
				const int32 Top = FMath::Max(0, i - 1);
				FFeature F = MakeAtSample(EFeature::SlideZone, S[Top], Top, BranchIndex);
				F.Aux = j;
				F.Target = FVector(S[j].P, S[j].Z);
				F.Height = S[Top].Z - S[j].Z;
				F.Length = S[j].S - S[Top].S;
				for (int32 k = Top; k <= j; ++k) { F.Width = FMath::Max(F.Width, S[k].Width); }
				L.Features.Add(F);
				i = j;
			}
		};
		AddSlides(M, INDEX_NONE);
		for (int32 b = 0; b < L.Branches.Num(); ++b) { AddSlides(L.Branches[b].Samples, b); }

		// Cruces colosales.
		for (int32 c = 0; c < L.Crossings.Num(); ++c)
		{
			const FCrossing& C = L.Crossings[c];
			const FRouteStep& High = L.Route[C.HighStep];
			for (int32 End = 0; End < 2; ++End)
			{
				const int32 Si = End == 0 ? High.FirstSample : High.LastSample;
				FFeature T = MakeAtSample(EFeature::Tower, M[Si], Si, INDEX_NONE);
				T.Location = FVector(M[Si].P, C.TopZ);
				T.Radius = P.TowerRadius;
				T.Height = C.TopZ;
				T.Aux = c;
				if (End == 0 && C.HighStep > 0 && Si > 0 && (M[Si - 1].Flags & PathFlags::UnderTower) != 0)
				{
					// Torre de entrada, hueca: la puerta mira al camino que llega (primera muestra fuera de la torre).
					int32 j = Si - 1;
					while (j > 0 && (M[j].Flags & PathFlags::UnderTower) != 0) { --j; }
					// Dirección del camino que llega, ajustada al vértice del polígono de la torre más cercano.
					const FVector2D ToPath = (M[j].P - M[Si].P).GetSafeNormal();
					const double Step = TwoPi / TowerDims::Sides;
					const double DoorA = FMath::RoundToInt(FMath::Atan2(ToPath.Y, ToPath.X) / Step) * Step;
					const FVector2D DoorDir(FMath::Cos(DoorA), FMath::Sin(DoorA));
					T.Aux2 = TowerDims::HollowBit;
					T.Target = FVector(M[Si].P + DoorDir * P.TowerRadius, M[Si - 1].Z);
				}
				L.Features.Add(T);
			}
			// Puente colgante: pilares de roca cada 70-100 m en los vanos largos, lejos de cualquier
			// otro camino de abajo (si no cabe en su sitio se desplaza hasta 30 m; si no, se omite).
			if (C.Type == ETNProcCrossingType::Bridge)
			{
				const double PillarR = 450.0;
				const double S0 = M[High.FirstSample].S + P.TowerRadius + 2500.0;
				const double S1 = M[High.LastSample].S - P.TowerRadius - 2500.0;
				const int32 NumPillars = S1 - S0 > 9000.0 ? FMath::FloorToInt((S1 - S0) / 8500.0) : 0;
				auto Clear = [&](const FVector2D& Pt)
				{
					auto Far = [&](const TArray<FPathSample>& Arr, int32 SkipFrom, int32 SkipTo)
					{
						for (int32 i = 0; i < Arr.Num(); ++i)
						{
							if (i >= SkipFrom && i <= SkipTo) { continue; }
							if (FVector2D::Distance(Arr[i].P, Pt) < PillarR + 600.0 + Arr[i].Width * 0.5 + 1500.0) { return false; }
						}
						return true;
					};
					if (!Far(M, High.FirstSample, High.LastSample)) { return false; }
					for (const FBranch& B : L.Branches) { if (!Far(B.Samples, INDEX_NONE, INDEX_NONE)) { return false; } }
					return true;
				};
				for (int32 k = 1; k <= NumPillars; ++k)
				{
					const double Target = S0 + (S1 - S0) * k / (NumPillars + 1);
					for (const double Off : { 0.0, 1500.0, -1500.0, 3000.0, -3000.0 })
					{
						int32 Idx = INDEX_NONE;
						PathDetail::MainPointAt(M, Target + Off, nullptr, &Idx);
						if (Idx == INDEX_NONE || !Clear(M[Idx].P)) { continue; }
						FFeature Pl = MakeAtSample(EFeature::DeckPillar, M[Idx], Idx, INDEX_NONE);
						Pl.Location = FVector(M[Idx].P, C.TopZ - 30.0);
						Pl.Radius = PillarR;
						Pl.Height = C.TopZ - 30.0;
						Pl.Aux = c;
						L.Features.Add(Pl);
						break;
					}
				}
			}
			// Caerse del puente colgante mata: cajas desde 60 cm bajo el tablero hasta 12,5 m, en tramos
			// de <= 20 m desde el borde de un pilar de torre hasta el del otro. Nunca sobre el vuelo del
			// géiser (tramo de llegada a la torre de entrada) ni sobre el tobogán de la de salida.
			if (C.Type == ETNProcCrossingType::Bridge)
			{
				const FVector2D TowerIn = M[High.FirstSample].P;
				const FVector2D TowerOut = M[High.LastSample].P;
				TArray<FVector> Avoid;
				if (C.HighStep > 0)
				{
					const FRouteStep& Prev = L.Route[C.HighStep - 1];
					for (int32 i = Prev.FirstSample; i <= Prev.LastSample; ++i)
					{
						if (FVector2D::Distance(M[i].P, TowerIn) < P.TowerRadius + 3500.0) { Avoid.Add(FVector(M[i].P, M[i].Z)); }
					}
				}
				if (C.HighStep + 1 < L.Route.Num())
				{
					const FRouteStep& Next = L.Route[C.HighStep + 1];
					for (int32 i = Next.FirstSample; i <= Next.LastSample; ++i)
					{
						if (FVector2D::Distance(M[i].P, TowerOut) < P.TowerRadius + 6000.0) { Avoid.Add(FVector(M[i].P, M[i].Z)); }
					}
				}
				auto Emit = [&](int32 From, int32 To)
				{
					const FVector2D A = M[From].P;
					const FVector2D B = M[To].P;
					FKillBox K;
					K.Dir = (B - A).GetSafeNormal();
					if (K.Dir.IsNearlyZero()) { return; }
					double HalfW = 0.0;
					for (int32 i = From; i <= To; ++i) { HalfW = FMath::Max(HalfW, M[i].Width * 0.5); }
					K.Center = FVector((A + B) * 0.5, C.TopZ - 655.0);
					K.Half = FVector(FVector2D::Distance(A, B) * 0.5 + 150.0, HalfW + 800.0, 595.0);
					for (const FVector& Q : Avoid)
					{
						const FVector2D Rel(Q.X - K.Center.X, Q.Y - K.Center.Y);
						const bool bIn = FMath::Abs(FVector2D::DotProduct(Rel, K.Dir)) <= K.Half.X + 400.0
							&& FMath::Abs(FVector2D::DotProduct(Rel, FVector2D(-K.Dir.Y, K.Dir.X))) <= K.Half.Y + 400.0;
						if (bIn) { return; }
					}
					L.KillBoxes.Add(K);
				};
				int32 From = INDEX_NONE;
				for (int32 i = High.FirstSample; i <= High.LastSample; ++i)
				{
					const bool bFree = FVector2D::Distance(M[i].P, TowerIn) > P.TowerRadius + 50.0 && FVector2D::Distance(M[i].P, TowerOut) > P.TowerRadius + 50.0;
					if (bFree && From == INDEX_NONE) { From = i; }
					const bool bFlush = From != INDEX_NONE && (!bFree || i == High.LastSample || M[i].S - M[From].S >= 2000.0);
					if (!bFlush) { continue; }
					const int32 To = bFree ? i : i - 1;
					if (To > From) { Emit(From, To); }
					From = bFree ? i : INDEX_NONE;
				}
			}
			FFeature S = MakeAtSample(EFeature::Deck, M[High.FirstSample], High.FirstSample, INDEX_NONE);
			S.Aux = c;
			S.Aux2 = High.LastSample;
			S.Height = C.TopZ;
			S.Location = FVector(C.CrossPoint, C.TopZ);
			L.Features.Add(S);

			// Plaza en los puentes de piedra y de hierro: sobre la pila más cercana al centro del vano (si
			// está a menos de un cuarto de él) o en el centro, con una atalaya de bloques con recompensa
			// arriba y una medusa al pie para subir de un bote. Cabe dentro de las cajas de muerte del vano
			// (semiancho del tablero + 7,6 m como mucho).
			if (C.Type == ETNProcCrossingType::Bridge)
			{
				const EBridgeStyle Style = BridgeStyleOf(L, c);
				const double SA = M[High.FirstSample].S + P.TowerRadius + 2500.0;
				const double SB = M[High.LastSample].S - P.TowerRadius - 2500.0;
				if ((Style == EBridgeStyle::Stone || Style == EBridgeStyle::Iron) && SB - SA > 2400.0)
				{
					const double Mid = 0.5 * (SA + SB);
					double Sc = Mid;
					bool bPier = false;
					for (const FFeature& Pl : L.Features)
					{
						if (Pl.Type != EFeature::DeckPillar || Pl.Aux != c) { continue; }
						const double Sp = M[Pl.PathIndex].S;
						if (FMath::Abs(Sp - Mid) < 0.25 * (SB - SA) && (!bPier || FMath::Abs(Sp - Mid) < FMath::Abs(Sc - Mid))) { Sc = Sp; bPier = true; }
					}
					int32 Idx = INDEX_NONE;
					FVector2D Dir;
					const FVector2D Pc = PathDetail::MainPointAt(M, Sc, &Dir, &Idx);
					if (Idx != INDEX_NONE)
					{
						const uint32 H = HashCell(P.Seed ^ 0x9A2Au, c, 7);
						FFeature Pz = MakeAtSample(EFeature::BridgePlaza, M[Idx], Idx, INDEX_NONE);
						Pz.Location = FVector(Pc, C.TopZ);
						Pz.Dir = Dir.GetSafeNormal();
						const double Hw = M[Idx].Width * 0.5;
						Pz.Radius = FMath::Clamp(Hw + 450.0 + 250.0 * ((H >> 8) & 0xFF) / 255.0, 750.0, Hw + 760.0);
						Pz.Aux = c;
						Pz.Aux2 = static_cast<int32>(H & 0xFFFFF);
						Pz.Height = bPier ? 1.0 : 0.0;
						Pz.Length = Sc;
						L.Features.Add(Pz);
						const FVector2D Tw = PlazaDims::TowerAt(Pz);
						FFeature Bonus = MakeAtSample(EFeature::BonusPickup, M[Idx], Idx, INDEX_NONE);
						Bonus.Location = FVector(Tw, C.TopZ + PlazaDims::TowerH + 70.0);
						L.Features.Add(Bonus);
						FFeature Jelly = MakeAtSample(EFeature::Bouncer, M[Idx], Idx, INDEX_NONE);
						Jelly.Location = FVector(PlazaDims::BouncerAt(Pz), C.TopZ);
						L.Features.Add(Jelly);
					}
				}
			}

			// Puerta de la muralla donde la cruza el tramo bajo: atraviesa el muro en perpendicular, con
			// luz para el camino (más si este cruza en diagonal) y arco de medio punto con la clave 5 m
			// bajo el adarve: una puerta altísima cuyo arco hace de puente.
			if (C.Type == ETNProcCrossingType::Wall)
			{
				const FRouteStep& Low = L.Route[C.LowStep];
				int32 Lo = Low.FirstSample, Hi = High.FirstSample;
				double Best = 1e300;
				for (int32 j = Low.FirstSample; j <= Low.LastSample; ++j)
				{
					const double D = FVector2D::DistSquared(M[j].P, C.CrossPoint);
					if (D < Best) { Best = D; Lo = j; }
				}
				Best = 1e300;
				for (int32 i = High.FirstSample; i <= High.LastSample; ++i)
				{
					const double D = FVector2D::DistSquared(M[i].P, C.CrossPoint);
					if (D < Best) { Best = D; Hi = i; }
				}
				const FVector2D Along = M[Hi].Dir;
				const double Sin = FMath::Max(0.35, FMath::Abs(FVector2D::CrossProduct(Along, M[Lo].Dir)));
				const double Cot = FMath::Sqrt(FMath::Max(0.0, 1.0 - Sin * Sin)) / Sin;
				const double Floor = M[Lo].Z;
				const double Thick = 2.0 * WallDims::HalfAt(M[Hi].Width * 0.5, C.TopZ - Floor);
				FFeature G = MakeAtSample(EFeature::Gate, M[Lo], Lo, INDEX_NONE);
				G.Location = FVector(C.CrossPoint, Floor);
				G.Dir = Along;
				// Luz monumental (al menos el 40 % de la altura) y siempre la que pide el camino.
				const double Need = M[Lo].Width * 0.5 / Sin + 0.5 * Thick * Cot + 200.0;
				G.Radius = FMath::Min(FMath::Max(Need, 0.2 * (C.TopZ - Floor)), C.TopZ - WallDims::Crown - Floor - 600.0);
				G.Width = 2.0 * G.Radius;
				G.Length = Thick;
				G.Height = C.TopZ;
				G.Aux = c;
				G.Aux2 = Hi;
				L.Features.Add(G);
			}
		}
	}

	namespace FeatureDetail
	{
		/**
		 * Cajas de muerte de un mordisco del adarve: desde 60 cm bajo el adarve hasta 3 m bajo su fondo, a lo ancho de todo el
		 * muro y 9 m más allá de cada cara (quien salta hacia fuera por él), y a lo largo del tramo, sus parapetos rotos y 6 m
		 * más (sin pasar de MinS..MaxS), en piezas de 4 m como mucho que siguen el eje.
		 */
		inline void AddWallBreachKillBoxes(FLayout& L, const FWallAxis& Axis, const FFeature& F, double TopZ, double MinS, double MaxS)
		{
			const double Reach = WallBreachDims::ParapetBreakMax + WallBreachDims::KillAlong;
			const double A = FMath::Max(MinS, F.Target.X - Reach);
			const double B = FMath::Min(MaxS, F.Target.Y + Reach);
			if (B <= A) { return; }
			const double KillZ = TopZ - WallBreachDims::KillTop;
			const double Bottom = TopZ - F.Height - 300.0;
			const int32 Pieces = FMath::Max(1, FMath::CeilToInt32((B - A) / 400.0));
			for (int32 k = 0; k < Pieces; ++k)
			{
				FVector2D P0, T0, N0, P1, T1, N1;
				double Hw0 = 0.0, Hw1 = 0.0;
				Axis.At(LerpD(A, B, static_cast<double>(k) / Pieces), P0, T0, N0, Hw0);
				Axis.At(LerpD(A, B, static_cast<double>(k + 1) / Pieces), P1, T1, N1, Hw1);
				FKillBox K;
				K.Dir = (P1 - P0).GetSafeNormal();
				if (K.Dir.IsNearlyZero()) { K.Dir = T0; }
				K.Center = FVector((P0 + P1) * 0.5, 0.5 * (KillZ + Bottom));
				// Solapadas 60 cm por cada lado: en las curvas no queda cuña sin cubrir sobre el muro.
				K.Half = FVector(FVector2D::Distance(P0, P1) * 0.5 + 60.0, WallDims::HalfAt(FMath::Max(Hw0, Hw1), TopZ - Bottom) + WallBreachDims::KillSide,
					0.5 * (KillZ - Bottom));
				L.KillBoxes.Add(K);
			}
		}

		/** Si el eje gira menos de 20° entre A, el medio y B (los mordiscos van en tramos casi rectos). */
		inline bool WallStraight(const FWallAxis& Axis, double A, double B)
		{
			FVector2D P, T0, T1, T2, N;
			double Hw = 0.0;
			Axis.At(A, P, T0, N, Hw);
			Axis.At(0.5 * (A + B), P, T1, N, Hw);
			Axis.At(B, P, T2, N, Hw);
			const double MinCos = FMath::Cos(FMath::DegreesToRadians(20.0));
			return FVector2D::DotProduct(T0, T1) > MinCos && FVector2D::DotProduct(T1, T2) > MinCos;
		}
	}

	/**
	 * Adarve roto de las murallas colosales (EFeature::WallBreach, WallBreachDims): grupos de mordiscos en lo alto del muro,
	 * que desde lejos se ven como bocados en la silueta almenada. Cada grupo es una a tres brechas de lado a lado seguidas
	 * (con 1,5-2,7 m de adarve entero entre ellas), una cornisa pegada a un parapeto o una cornisa y una brecha. Entre dos
	 * grupos, 15-47 m de adarve entero; como mucho 3, 5 o 7 grupos por muralla (fácil, normal, difícil). Nunca a menos de
	 * 9 m del borde de una torre ni de 6 m del arco de la puerta, y en tramos que giran menos de 20°. Caer en un mordisco
	 * mata (cajas de muerte, AddWallBreachKillBoxes) y las muestras del adarve que lo tocan llevan PathFlags::Gap: allí no
	 * hay suelo continuo. El cuerpo de la muralla sigue entero.
	 */
	inline void BuildWallBreaches(FLayout& L, FRng Rng)
	{
		using namespace FeatureDetail;
		const FGenParams& P = L.Params;
		const double Diff = Saturate(P.Difficulty01);
		const int32 MaxGroups = Diff < 0.35 ? 3 : (Diff < 0.75 ? 5 : 7);
		const double SpaceMin = LerpD(3200.0, 1300.0, Diff);
		const double SpaceMax = LerpD(5200.0, 2600.0, Diff);
		for (int32 c = 0; c < L.Crossings.Num(); ++c)
		{
			const FCrossing& C = L.Crossings[c];
			if (C.Type != ETNProcCrossingType::Wall) { continue; }
			const FRouteStep& High = L.Route[C.HighStep];
			FWallAxis Axis;
			Axis.Build(L.Main, High.FirstSample, High.LastSample);
			const double Len = Axis.Length();
			const double From = P.TowerRadius + WallBreachDims::TowerClear;
			const double To = Len - P.TowerRadius - WallBreachDims::TowerClear;
			if (To - From < 1200.0) { continue; }
			double GateA = 1e300, GateB = -1e300;
			for (const FFeature& G : L.Features)
			{
				if (G.Type != EFeature::Gate || G.Aux != c) { continue; }
				const double Sg = Axis.Project(FVector2D(G.Location.X, G.Location.Y));
				GateA = Sg - G.Radius - WallBreachDims::GateClear;
				GateB = Sg + G.Radius + WallBreachDims::GateClear;
			}

			double Cursor = From + Rng.Range(0.0, 1200.0);
			int32 Groups = 0;
			for (int32 Guard = 0; Guard < 200 && Groups < MaxGroups; ++Guard)
			{
				// Composición del grupo: tipo, largo de cada tramo roto y adarve entero entre ellos.
				struct FPiece { EWallBreach Kind = EWallBreach::Gap; double Len = 0.0; double Before = 0.0; };
				TArray<FPiece, TInlineAllocator<4>> Pieces;
				const double U = Rng.Unit();
				auto AddGap = [&](double Before) { Pieces.Add(FPiece{ EWallBreach::Gap, Rng.Range(WallBreachDims::GapMin(Diff), WallBreachDims::GapMax(Diff)), Before }); };
				auto AddLedge = [&]()
				{
					const EWallBreach Kind = Rng.Chance(0.5) ? EWallBreach::LedgeLeft : EWallBreach::LedgeRight;
					Pieces.Add(FPiece{ Kind, Rng.Range(WallBreachDims::LedgeLenMin(Diff), WallBreachDims::LedgeLenMax(Diff)), 0.0 });
				};
				if (U < (Diff < 0.35 ? 0.55 : 0.45))
				{
					const int32 Count = Rng.RangeInt(1, Diff < 0.35 ? 2 : 3);
					for (int32 n = 0; n < Count; ++n) { AddGap(n == 0 ? 0.0 : WallBreachDims::Island(Diff) + Rng.Range(-25.0, 25.0)); }
				}
				else if (U < (Diff < 0.35 ? 1.0 : 0.8))
				{
					AddLedge();
				}
				else
				{
					AddLedge();
					AddGap(WallBreachDims::Island(Diff) + 60.0 + Rng.Range(0.0, 60.0));
				}
				double Total = 0.0;
				for (const FPiece& Pc : Pieces) { Total += Pc.Before + Pc.Len; }
				if (Cursor + Total > To) { break; }
				if (Cursor + Total > GateA && Cursor < GateB) { Cursor = GateB; continue; }
				if (!WallStraight(Axis, Cursor - 300.0, Cursor + Total + 300.0)) { Cursor += 400.0; continue; }

				double S = Cursor;
				for (const FPiece& Pc : Pieces)
				{
					S += Pc.Before;
					const double Sa = S;
					const double Sb = S + Pc.Len;
					S = Sb;
					FVector2D Pm, Tm, Nm;
					double Hw = 0.0;
					Axis.At(0.5 * (Sa + Sb), Pm, Tm, Nm, Hw);
					const int32 Idx = High.FirstSample + Axis.NearestIndex(0.5 * (Sa + Sb));
					FFeature F = MakeAtSample(EFeature::WallBreach, L.Main[Idx], Idx, INDEX_NONE);
					F.Location = FVector(Pm, C.TopZ);
					F.Dir = Tm;
					F.Width = 2.0 * Hw;
					F.Length = Sb - Sa;
					F.Height = Rng.Range(WallBreachDims::DepthMin, WallBreachDims::DepthMax);
					F.Radius = Pc.Kind == EWallBreach::Gap ? 0.0 : WallBreachDims::LedgeWidth(Diff);
					F.Aux = c;
					F.Aux2 = static_cast<int32>(Pc.Kind);
					F.Target = FVector(Sa, Sb, 0.0);
					L.Features.Add(F);
					AddWallBreachKillBoxes(L, Axis, F, C.TopZ, P.TowerRadius + 400.0, Len - P.TowerRadius - 400.0);
					// Sin suelo continuo en el adarve: las muestras que tocan el mordisco (y su parapeto roto) llevan Gap.
					for (int32 i = High.FirstSample; i <= High.LastSample; ++i)
					{
						const double Si = L.Main[i].S - L.Main[High.FirstSample].S;
						if (Si >= Sa - WallBreachDims::ParapetBreakMax - 100.0 && Si <= Sb + WallBreachDims::ParapetBreakMax + 100.0) { L.Main[i].Flags |= PathFlags::Gap; }
					}
				}
				++Groups;
				Cursor = S + Rng.Range(SpaceMin, SpaceMax);
			}
		}
	}

	/** true si la zanja de un hueco (rectángulo con sus orillas) alcanza muestras de otra parte de algún camino. */
	inline bool TrenchHitsOtherPath(const FLayout& L, const TArray<FPathSample>& Own, const FPathSample& Sm, const FFeature& F)
	{
		const FVector2D C(F.Location.X, F.Location.Y);
		const FVector2D N = LeftNormal(F.Dir);
		auto Hits = [&](const TArray<FPathSample>& Arr)
		{
			for (const FPathSample& Q : Arr)
			{
				if (&Arr == &Own && FMath::Abs(Q.S - Sm.S) <= F.Height * 0.5 + 2000.0) { continue; }
				const FVector2D Rel = Q.P - C;
				const double Hq = Q.Width * 0.5 + 300.0;
				if (FMath::Abs(FVector2D::DotProduct(Rel, F.Dir)) <= F.Height * 0.5 + Hq
					&& FMath::Abs(FVector2D::DotProduct(Rel, N)) <= F.Width * 0.5 + GapTrenchSide + Hq)
				{
					return true;
				}
			}
			return false;
		};
		if (Hits(L.Main)) { return true; }
		for (const FBranch& B : L.Branches) { if (Hits(B.Samples)) { return true; } }
		return false;
	}

	/** Huecos de salto sobre un array de muestras (principal o rama). */
	inline void PlaceGapsOn(FLayout& L, TArray<FPathSample>& Samples, int32 BranchIndex, FRng& Rng)
	{
		using namespace FeatureDetail;
		const FGenParams& P = L.Params;
		const double GapMaxD = LerpD(FMath::Min(P.GapMax, 200.0), P.GapMax, Saturate(P.Difficulty01));
		// Al menos 30 m entre huecos (antes 40): saltos muy a menudo, con sitio para aterrizar y coger carrerilla.
		// GapsPerKm es la densidad que sale en los tramos donde caben: la probabilidad por muestra compensa los 30 m muertos
		// tras cada hueco (antes, con 9 por km salían unos 6,6). Como mucho 26,7 por km.
		constexpr double MinSpacing = 3000.0;
		const double Rate = FMath::Min(P.GapsPerKm, 80000.0 / MinSpacing) / 100000.0;
		const double Chance = FMath::Min(0.9, Rate / (1.0 - Rate * MinSpacing) * P.SampleSpacing);
		// Huecos de panzazo: desde Normal y si caben en las métricas del mapa.
		const bool bDiveGaps = P.Difficulty01 >= 0.35 && GapMaxD >= DiveGapMin + 10.0;
		double LastS = -1e9;

		TArray<int32> Forks;
		if (BranchIndex == INDEX_NONE)
		{
			for (const FBranch& B : L.Branches) { Forks.Add(B.ForkSample); Forks.Add(B.RejoinSample); }
		}

		for (int32 i = 8; i < Samples.Num() - 8; ++i)
		{
			const FPathSample& Sm = Samples[i];
			if (Sm.S - LastS < MinSpacing || IsWetBiome(Sm.Biome)) { continue; }
			// En explanadas no: la zanja cruzaría toda la plaza. Ni a ras de agua: la zanja
			// (con el fondo sobre el agua) tiene que tener al menos 3 m de hondo.
			if (Sm.Width > 2600.0 || Sm.Z < 450.0) { continue; }
			if (AnyFlag(Samples, i - 6, i + 6, PathFlags::Special | PathFlags::Lane)) { continue; }
			bool bFlat = true;
			for (int32 j = i - 5; j <= i + 5; ++j) { if (FMath::Abs(Samples[j].Z - Sm.Z) > 110.0) { bFlat = false; break; } }
			if (!bFlat) { continue; }
			bool bNearFork = false;
			for (const int32 Fk : Forks) { if (FMath::Abs(Fk - i) < 14) { bNearFork = true; break; } }
			if (bNearFork || !Rng.Chance(Chance)) { continue; }

			FFeature F = MakeAtSample(EFeature::Gap, Sm, i, BranchIndex);
			F.Length = Rng.Range(P.GapMin, GapMaxD);
			F.Width = Sm.Width + 500.0;
			// Estilo: labios de siempre, postes que parten un hueco más largo en saltos cortos o tronco de
			// equilibrio de labio a labio (el hueco se sigue pudiendo saltar).
			const double StyleU = Rng.Unit();
			F.Aux = static_cast<int32>(StyleU < 0.4 ? EGapStyle::Lips : (StyleU < 0.75 ? EGapStyle::Posts : EGapStyle::Beam));
			if (F.Aux == static_cast<int32>(EGapStyle::Posts)) { F.Length = FMath::Max(F.Length, GapMaxD) * Rng.Range(1.35, 1.8); }
			// Desde Normal, algunos labios pasan a salto largo que obliga al panzazo (con hash, sin tocar la secuencia
			// del generador).
			const uint32 DiveHash = HashCell(P.Seed ^ 0xD1FEu, i, BranchIndex + 7);
			if (bDiveGaps && F.Aux == static_cast<int32>(EGapStyle::Lips) && (DiveHash & 0xFF) < 110u)
			{
				F.Aux = static_cast<int32>(EGapStyle::Dive);
				F.Length = LerpD(DiveGapMin, FMath::Min(DiveGapMax, GapMaxD), ((DiveHash >> 8) & 0xFF) / 255.0);
			}
			// Zanja del terreno más larga que el hueco: los labios (mallas) la estrechan al valor exacto.
			F.Height = FMath::Max(F.Length + 500.0, 800.0);
			// La zanja no puede pisar otra parte de ningún camino (curvas que vuelven, ramas, horquillas).
			if (TrenchHitsOtherPath(L, Samples, Sm, F)) { continue; }
			L.Features.Add(F);
			for (int32 j = 0; j < Samples.Num(); ++j)
			{
				if (FMath::Abs(Samples[j].S - Sm.S) <= F.Height * 0.5 + 100.0) { Samples[j].Flags |= PathFlags::Gap; }
			}
			LastS = Sm.S;
		}

		// En el camino principal siempre hay al menos un salto de panzazo (Normal y Difícil): el hueco de labios con
		// la zanja más larga pasa a serlo (cabe dentro de su zanja, así que no pisa nada nuevo).
		if (bDiveGaps && BranchIndex == INDEX_NONE)
		{
			FFeature* Best = nullptr;
			bool bHasDive = false;
			for (FFeature& F : L.Features)
			{
				if (F.Type != EFeature::Gap || F.BranchIndex != INDEX_NONE || IsLavaGap(F)) { continue; }
				bHasDive |= GapStyleOf(F) == EGapStyle::Dive;
				if (GapStyleOf(F) == EGapStyle::Lips && (!Best || F.Height > Best->Height)) { Best = &F; }
			}
			if (!bHasDive && Best && Best->Height - 500.0 >= DiveGapMin)
			{
				Best->Aux = static_cast<int32>(EGapStyle::Dive);
				Best->Length = FMath::Clamp(Best->Height - 500.0, DiveGapMin, FMath::Min(DiveGapMax, GapMaxD));
			}
		}
	}

	inline void BuildGaps(FLayout& L, FRng Rng)
	{
		PlaceGapsOn(L, L.Main, INDEX_NONE, Rng);
		for (int32 b = 0; b < L.Branches.Num(); ++b)
		{
			// Las ramas "arriesgadas" llevan el doble de huecos.
			FGenParams Saved = L.Params;
			if (L.Branches[b].Kind == EBranchKind::Risky) { L.Params.GapsPerKm *= 2.0; }
			if (L.Branches[b].Kind != EBranchKind::Lane) { PlaceGapsOn(L, L.Branches[b].Samples, b, Rng); }
			L.Params = Saved;
		}
	}

	/** Isletas (bioma agua) y pasarelas con tablones rotos (manglar). */
	inline void BuildWetFeatures(FLayout& L, FRng Rng)
	{
		using namespace PathDetail;
		const FGenParams& P = L.Params;
		const double Diff = Saturate(P.Difficulty01);
		const double GapMaxW = LerpD(FMath::Min(P.IsletGapMax, 220.0), P.IsletGapMax, Diff);
		const TArray<FPathSample>& M = L.Main;

		for (int32 i = 0; i < M.Num(); ++i)
		{
			const bool bIslet = (M[i].Flags & PathFlags::Islet) != 0;
			const bool bBoard = (M[i].Flags & PathFlags::Boardwalk) != 0;
			if (!bIslet && !bBoard) { continue; }
			const uint32 Flag = bIslet ? PathFlags::Islet : PathFlags::Boardwalk;
			int32 j = i;
			while (j + 1 < M.Num() && (M[j + 1].Flags & Flag) != 0) { ++j; }
			const double S0 = M[FMath::Max(0, i - 1)].S;
			const double S1 = M[FMath::Min(M.Num() - 1, j + 1)].S;

			// Karts: canal de agua abierta, sin isletas (el kart flota de una orilla a otra).
			if (bIslet && P.bDrivable)
			{
				i = j;
				continue;
			}
			if (bIslet)
			{
				const double LiMin = LerpD(900.0, 500.0, Diff);
				const double LiMax = LerpD(1600.0, 1000.0, Diff);
				double Cursor = S0;
				double PrevTop = M[FMath::Max(0, i - 1)].Z;
				int32 Guard = 0;
				while (Cursor < S1 && Guard++ < 400)
				{
					double Gap = Rng.Range(P.IsletGapMin, GapMaxW);
					double Len = Rng.Range(LiMin, LiMax);
					const double Remaining = S1 - Cursor;
					// Tras la última isleta con el hueco topado en GapMaxW queda justo GapMaxW hasta tierra: sin el margen, el
					// redondeo (distinto en DebugGame y Development, #579) decidía si salía otra isleta de más pisando la orilla.
					if (Remaining <= GapMaxW + 1e-6) { break; }
					if (Remaining < Gap + Len + P.IsletGapMin)
					{
						// Última isleta: reparte el resto para que el salto final a tierra sea válido.
						Gap = FMath::Min(GapMaxW, FMath::Max(P.IsletGapMin, (Remaining - LiMin) * 0.5));
						Len = FMath::Max(LiMin * 0.7, Remaining - 2.0 * Gap);
					}
					const double Center = Cursor + Gap + Len * 0.5;
					FVector2D Dir;
					const FVector2D C = MainPointAt(M, Center, &Dir);
					int32 Near = INDEX_NONE;
					MainPointAt(M, Center, nullptr, &Near);
					const double HalfAcross = FMath::Clamp(M[Near].Width * 0.5, 280.0, 750.0);
					double Top = M[Near].Z + Rng.Range(-35.0, 35.0);
					Top = FMath::Clamp(Top, PrevTop - 60.0, PrevTop + 60.0);
					Top = FMath::Max(Top, 45.0);

					FFeature F;
					F.Type = EFeature::Islet;
					F.Location = FVector(C, Top);
					F.Dir = Dir;
					F.Length = Len;
					F.Width = HalfAcross * 2.0;
					F.Height = Top + 450.0;
					F.PathIndex = Near;
					F.Biome = M[Near].Biome;
					const FVector2D N = LeftNormal(Dir);
					const int32 Verts = 12;
					for (int32 v = 0; v < Verts; ++v)
					{
						const double A = TwoPi * v / Verts;
						const double Rn = 1.0 + 0.16 * FMath::Abs(FMath::Sin(A)) * Noise1(P.Seed ^ 0x15137u, Center / 1000.0 + v);
						F.Polygon.Add(C + Dir * (FMath::Cos(A) * Len * 0.5) + N * (FMath::Sin(A) * HalfAcross * Rn));
					}
					L.Features.Add(F);
					PrevTop = Top;
					Cursor = Center + Len * 0.5;
				}
			}
			else
			{
				// Pasarela: tablones a trozos de 12-32 m (antes 15-40 m); los huecos, más a menudo y más largos con la dificultad.
				double Cursor = S0;
				int32 Guard = 0;
				while (Cursor < S1 - 200.0 && Guard++ < 200)
				{
					const double PieceLen = FMath::Min(S1 - Cursor, Rng.Range(1200.0, 3200.0));
					int32 A = INDEX_NONE, B = INDEX_NONE;
					MainPointAt(M, Cursor, nullptr, &A);
					MainPointAt(M, Cursor + PieceLen, nullptr, &B);
					FFeature F;
					F.Type = EFeature::Boardwalk;
					F.PathIndex = A;
					F.Aux2 = FMath::Max(A, B);
					F.Location = FVector(M[A].P, M[A].Z);
					F.Height = M[A].Z;
					F.Width = M[A].Width;
					F.Length = PieceLen;
					F.Biome = M[A].Biome;
					F.Target = FVector(M[F.Aux2].P, M[F.Aux2].Z);
					L.Features.Add(F);
					Cursor += PieceLen;
					if (Rng.Chance(0.5 + 0.35 * Diff)) { Cursor += Rng.Range(P.GapMin, LerpD(220.0, P.GapMax, Diff)); }
				}
			}
			i = j;
		}
	}

	/** Pilas de huevos de respawn al cruzar a otro módulo (cada N portales). */
	inline void BuildEggNests(FLayout& L, FRng Rng)
	{
		using namespace FeatureDetail;
		const FGenParams& P = L.Params;
		const TArray<FPathSample>& M = L.Main;
		const uint32 Bad = PathFlags::Elevated | PathFlags::Colossal | PathFlags::UnderTower | PathFlags::TowerTop | PathFlags::Slide
			| PathFlags::GeyserBase | PathFlags::Tunnel | PathFlags::Islet | PathFlags::Boardwalk | PathFlags::Gap | PathFlags::Shore;
		// Rally: los buggies reaparecen en las puertas de la carrera, no en huevos.
		if (P.bDrivable || M.Num() == 0)
		{
			return;
		}

		int32 Order = 0;
		{
			FFeature F = MakeAtSample(EFeature::EggNest, M[0], 0, INDEX_NONE);
			F.Location = FVector(L.StartPoint + LeftNormal(M[0].Dir) * (P.StartClearingRadius * 0.55), M[0].Z);
			F.Aux = Order++;
			L.Features.Add(F);
		}

		int32 PortalCount = 0;
		const int32 Every = FMath::Max(1, P.EggNestEveryNPortals);
		for (int32 k = 1; k < L.Route.Num(); ++k)
		{
			const int32 Pi0 = L.Route[k].FirstSample;
			if (AnyFlag(M, Pi0 - 6, Pi0 + 6, PathFlags::Elevated | PathFlags::Colossal | PathFlags::UnderTower | PathFlags::TowerTop)) { continue; }
			++PortalCount;
			if (PortalCount % Every != 0) { continue; }
			for (int32 i = Pi0 + 1; i < FMath::Min(M.Num(), Pi0 + 40); ++i)
			{
				if (M[i].S - M[Pi0].S < 1500.0) { continue; }
				if ((M[i].Flags & Bad) != 0 || AnyFlag(M, i - 3, i + 3, Bad)) { continue; }
				const double Side = Rng.Chance(0.5) ? 1.0 : -1.0;
				FFeature F = MakeAtSample(EFeature::EggNest, M[i], i, INDEX_NONE);
				F.Location = FVector(M[i].P + LeftNormal(M[i].Dir) * Side * FMath::Max(0.0, M[i].Width * 0.5 - 300.0), M[i].Z);
				F.Aux = Order++;
				L.Features.Add(F);
				break;
			}
		}
	}

	/** Muro de lanzamiento, compuerta e interruptor de sabotaje en cada carril 2vs2. */
	inline void BuildLanePuzzles(FLayout& L, FRng Rng)
	{
		using namespace FeatureDetail;
		for (int32 b = 0; b < L.Branches.Num(); ++b)
		{
			const FBranch& Br = L.Branches[b];
			if (Br.Kind != EBranchKind::Lane) { continue; }

			// Carril A = tramo del principal; carril B = la rama.
			TArray<FPathSample> LaneA;
			for (int32 i = Br.ForkSample; i <= Br.RejoinSample; ++i) { LaneA.Add(L.Main[i]); }
			const TArray<FPathSample>* Lanes[2] = { &LaneA, &Br.Samples };
			const int32 BranchIdx[2] = { INDEX_NONE, b };
			const int32 IndexOffset[2] = { Br.ForkSample, 0 };
			int32 GateIdx[2] = { INDEX_NONE, INDEX_NONE };
			int32 SwitchIdx[2] = { INDEX_NONE, INDEX_NONE };

			for (int32 Lane = 0; Lane < 2; ++Lane)
			{
				const TArray<FPathSample>& S = *Lanes[Lane];
				auto At = [&](double Frac) { return FMath::Clamp(FMath::RoundToInt(Frac * (S.Num() - 1)), 1, S.Num() - 2); };

				const int32 WallI = At(Rng.Range(0.36, 0.44));
				FFeature Wall = MakeAtSample(EFeature::ThrowWall, S[WallI], IndexOffset[Lane] + WallI, BranchIdx[Lane]);
				Wall.Width = S[WallI].Width + 1800.0;
				Wall.Height = 480.0;
				Wall.Length = 300.0;
				Wall.Aux = b;
				L.Features.Add(Wall);

				const int32 SwitchI = At(Rng.Range(0.54, 0.6));
				FFeature Sw = MakeAtSample(EFeature::SabotageSwitch, S[SwitchI], IndexOffset[Lane] + SwitchI, BranchIdx[Lane]);
				const double Side = Rng.Chance(0.5) ? 1.0 : -1.0;
				Sw.Location = FVector(S[SwitchI].P + LeftNormal(S[SwitchI].Dir) * Side * (S[SwitchI].Width * 0.5 - 150.0), S[SwitchI].Z);
				SwitchIdx[Lane] = L.Features.Add(Sw);

				const int32 GateI = At(Rng.Range(0.74, 0.82));
				FFeature Gate = MakeAtSample(EFeature::SabotageGate, S[GateI], IndexOffset[Lane] + GateI, BranchIdx[Lane]);
				Gate.Width = S[GateI].Width + 1800.0;
				Gate.Height = 350.0;
				Gate.Length = 200.0;
				Gate.Aux = b;
				GateIdx[Lane] = L.Features.Add(Gate);
			}
			// Cada interruptor levanta la compuerta del OTRO carril.
			L.Features[SwitchIdx[0]].Aux = GateIdx[1];
			L.Features[SwitchIdx[1]].Aux = GateIdx[0];
		}
	}

	/** Río opcional: de la costa hacia el interior, con puentes donde cruza caminos. */
	inline void BuildRiver(FLayout& L, FRng Rng)
	{
		using namespace PathDetail;
		const FGenParams& P = L.Params;
		L.River.Reset();
		L.RiverWidth.Reset();

		TArray<FVector2D> Avoid;
		TArray<double> AvoidR;
		for (const FFeature& F : L.Features)
		{
			if (F.Type == EFeature::Tower || F.Type == EFeature::Geyser || F.Type == EFeature::EggNest || F.Type == EFeature::SlideZone || F.Type == EFeature::StartArea)
			{
				Avoid.Add(FVector2D(F.Location.X, F.Location.Y));
				AvoidR.Add(FMath::Max(F.Radius, 2500.0) + 3500.0);
			}
		}

		for (int32 Attempt = 0; Attempt < 30; ++Attempt)
		{
			const double X0 = Rng.Range(0.2, 0.8) * L.WorldSizeX;
			if (FMath::Abs(X0 - L.EndPoint.X) < 12000.0) { continue; }
			TArray<FVector2D> Pts;
			TArray<double> Widths;
			FVector2D Pos(X0, L.CoastY(X0) + 2500.0);
			double Heading = -Pi * 0.5;
			const double Target = Rng.Range(0.35, 0.6) * L.WorldSize;
			double Len = 0.0;
			bool bOk = true;
			const uint32 RSeed = P.Seed ^ (0x51BE5u + static_cast<uint32>(Attempt));
			while (Len < Target)
			{
				Pts.Add(Pos);
				Widths.Add(LerpD(2400.0, 1200.0, Len / Target));
				Heading = -Pi * 0.5 + 0.8 * Fbm1(RSeed, Len / 20000.0, 2);
				Pos = Pos + DirFromAngle(Heading) * 1000.0;
				Len += 1000.0;
				if (Pos.X < P.MapEdgeClearance || Pos.X > L.WorldSizeX - P.MapEdgeClearance || Pos.Y < P.MapEdgeClearance * 1.5) { break; }
				const int32 Mod = L.ModuleAt(Pos);
				if (Mod != INDEX_NONE && IsCrossingModule(L, Mod)) { bOk = false; break; }
				for (int32 a = 0; a < Avoid.Num(); ++a) { if (FVector2D::Distance(Avoid[a], Pos) < AvoidR[a]) { bOk = false; break; } }
				if (!bOk) { break; }
			}
			if (!bOk || Pts.Num() < 12) { continue; }

			// Cruces con caminos: solo en tramos normales, con cota suficiente para un puente.
			struct FHit { int32 Branch; int32 Index; FVector2D Point; double Width; };
			TArray<FHit> Hits;
			auto Check = [&](const TArray<FPathSample>& S, int32 BranchIndex)
			{
				for (int32 i = 0; i + 1 < S.Num() && bOk; ++i)
				{
					for (int32 r = 0; r + 1 < Pts.Num(); ++r)
					{
						FVector2D X;
						if (!SegmentsIntersect(S[i].P, S[i + 1].P, Pts[r], Pts[r + 1], &X)) { continue; }
						if (FeatureDetail::AnyFlag(S, i - 8, i + 8, PathFlags::Special | PathFlags::Lane) || S[i].Z < 250.0 || IsWetBiome(S[i].Biome))
						{
							bOk = false;
							break;
						}
						FHit H;
						H.Branch = BranchIndex;
						H.Index = i;
						H.Point = X;
						H.Width = Widths[r];
						Hits.Add(H);
					}
				}
			};
			Check(L.Main, INDEX_NONE);
			for (int32 b = 0; b < L.Branches.Num() && bOk; ++b) { Check(L.Branches[b].Samples, b); }
			if (!bOk) { continue; }

			L.River = Pts;
			L.RiverWidth = Widths;
			for (const FHit& H : Hits)
			{
				TArray<FPathSample>& S = H.Branch == INDEX_NONE ? L.Main : L.Branches[H.Branch].Samples;
				FFeature F = FeatureDetail::MakeAtSample(EFeature::RiverBridge, S[H.Index], H.Index, H.Branch);
				F.Location = FVector(H.Point, S[H.Index].Z);
				F.Length = H.Width + 2.0 * (S[H.Index].Z + 300.0) / 1.2 + 800.0;
				F.Width = S[H.Index].Width + 200.0;
				L.Features.Add(F);
				for (FPathSample& Sm : S)
				{
					if (FVector2D::Distance(Sm.P, H.Point) < F.Length * 0.5 + 200.0) { Sm.Flags |= PathFlags::RiverCross; }
				}
			}
			return;
		}
	}

	/**
	 * Un volcán enorme por región volcánica, en el punto más alejado de los caminos:
	 * cono con cráter y lago de lava. Sus laderas pueden cubrir caminos cercanos, que
	 * lo atraviesan encajonados en su cauce.
	 */
	inline void BuildLandmarks(FLayout& L, FRng Rng)
	{
		using namespace PathDetail;
		TArray<FPathSample> All = L.Main;
		for (const FBranch& B : L.Branches) { All.Append(B.Samples); }
		FSampleGrid Grid;
		Grid.Build(All, L.MaxExtent());

		TArray<int32> Regions;
		for (const FModule& M : L.Modules)
		{
			if (M.Biome == ETNProcBiome::Volcanic && !Regions.Contains(M.Region)) { Regions.Add(M.Region); }
		}
		for (const int32 Region : Regions)
		{
			double BestScore = -1e300;
			FVector2D Best = FVector2D::ZeroVector;
			double BestClear = 0.0;
			for (int32 y = 0; y < L.RasterH; y += 3)
			{
				for (int32 x = 0; x < L.RasterW; x += 3)
				{
					const int32 Mod = L.ModuleOfCell[L.CellIndex(x, y)];
					if (Mod < 0 || L.Modules[Mod].Region != Region) { continue; }
					const FVector2D C = L.CellCenter(x, y);
					const double Edge = FMath::Min(FMath::Min(C.X, L.WorldSizeX - C.X), FMath::Min(C.Y, L.CoastY(C.X) - C.Y));
					if (Edge < 12000.0) { continue; }
					double D = 0.0;
					const int32 Near = Grid.Nearest(C, 40000.0, D);
					const double Clear = Near == INDEX_NONE ? 40000.0 : D - All[Near].Width * 0.5;
					const double Score = FMath::Min(Clear, 30000.0) + 0.15 * FMath::Min(Edge, 30000.0) + Rng.Range(0.0, 800.0);
					if (Score > BestScore) { BestScore = Score; Best = C; BestClear = Clear; }
				}
			}
			if (BestClear < 6000.0) { continue; }

			FFeature V;
			V.Type = EFeature::Volcano;
			V.Biome = ETNProcBiome::Volcanic;
			V.Radius = FMath::Clamp(BestClear * 2.0 + 6000.0, 24000.0, 42000.0);
			V.Height = V.Radius * Rng.Range(0.38, 0.48);
			const double CraterR = V.Radius * Rng.Range(0.11, 0.14);
			V.Width = CraterR * 2.0;
			V.Length = Rng.Range(1800.0, 2800.0);
			const double Base = L.SampleCoarse(L.LevelField, Best);
			V.Location = FVector(Best, Base);
			L.Features.Add(V);

			// Lago de lava a media altura del cráter (mismo perfil que TN_ProcMapTerrain).
			const double Rim = V.Height * FMath::Pow(1.0 - CraterR / V.Radius, 1.35);
			FFeature Lava;
			Lava.Type = EFeature::LavaPool;
			Lava.Biome = ETNProcBiome::Volcanic;
			Lava.Radius = CraterR * 0.72;
			Lava.Location = FVector(Best, Base + Rim - V.Length * 0.45);
			L.Features.Add(Lava);
		}
	}

	/**
	 * Volcanes pequeños (volcánico: 60-140 m de base, 18-77 m de alto, con lago de lava en el
	 * cráter) e islas decorativas (agua), lejos de los caminos.
	 */
	inline void BuildDecor(FLayout& L, FRng Rng)
	{
		using namespace PathDetail;
		TArray<FPathSample> All = L.Main;
		for (const FBranch& B : L.Branches) { All.Append(B.Samples); }
		FSampleGrid Grid;
		Grid.Build(All, L.MaxExtent());

		for (const FModule& M : L.Modules)
		{
			const bool bVolcanic = M.Biome == ETNProcBiome::Volcanic;
			const bool bWater = M.Biome == ETNProcBiome::Water;
			if (!bVolcanic && !bWater) { continue; }
			const int32 Count = bVolcanic ? Rng.RangeInt(1, 3) : Rng.RangeInt(2, 5);
			for (int32 n = 0; n < Count; ++n)
			{
				for (int32 Try = 0; Try < 25; ++Try)
				{
					const int32 X = Rng.RangeInt(0, L.RasterW - 1);
					const int32 Y = Rng.RangeInt(0, L.RasterH - 1);
					if (L.ModuleOfCell[L.CellIndex(X, Y)] != M.Id || L.BorderDist[L.CellIndex(X, Y)] < 3000.0f) { continue; }
					const FVector2D C = L.CellCenter(X, Y);
					const double Radius = bVolcanic ? Rng.Range(6000.0, 14000.0) : Rng.Range(700.0, 2200.0);
					double D = 0.0;
					const int32 Near = Grid.Nearest(C, 20000.0, D);
					// Un cono solo necesita libre su parte alta: los cauces cortan sus faldas.
					const double Clear = (bVolcanic ? Radius * 0.45 : Radius) + (Near != INDEX_NONE ? All[Near].Width * 0.5 : 0.0) + 3500.0;
					if (Near != INDEX_NONE && D < Clear) { continue; }
					bool bOverlap = false;
					for (const FFeature& F : L.Features)
					{
						if ((F.Type == EFeature::LavaPool || F.Type == EFeature::Island || F.Type == EFeature::Volcano)
							&& FVector2D::Distance(FVector2D(F.Location.X, F.Location.Y), C) < (F.Radius + Radius) * (bVolcanic ? 0.7 : 1.0) + 1500.0)
						{
							bOverlap = true;
							break;
						}
					}
					if (bOverlap) { continue; }
					FFeature F;
					if (bVolcanic)
					{
						// Cono con cráter y lago de lava a media altura del cráter (mismo perfil que el terreno).
						FFeature V;
						V.Type = EFeature::Volcano;
						V.Biome = ETNProcBiome::Volcanic;
						V.Radius = Radius;
						V.Height = Radius * Rng.Range(0.3, 0.55);
						const double CraterR = Radius * Rng.Range(0.12, 0.18);
						V.Width = CraterR * 2.0;
						V.Length = Rng.Range(700.0, 1400.0);
						const double Base = L.SampleCoarse(L.LevelField, C);
						V.Location = FVector(C, Base);
						L.Features.Add(V);
						const double Rim = V.Height * FMath::Pow(1.0 - CraterR / V.Radius, 1.35);
						FFeature Lava;
						Lava.Type = EFeature::LavaPool;
						Lava.Biome = ETNProcBiome::Volcanic;
						Lava.Radius = CraterR * 0.72;
						Lava.Location = FVector(C, Base + Rim - V.Length * 0.45);
						L.Features.Add(Lava);
						break;
					}
					F.Type = EFeature::Island;
					F.Radius = Radius;
					F.Biome = M.Biome;
					F.Location = FVector(C, Rng.Range(150.0, 520.0));
					L.Features.Add(F);
					break;
				}
			}
		}
	}

	namespace FeatureDetail
	{
		/** Obstáculos de objetos de cada bioma (ninguno en los de agua). */
		inline void PathPropsFor(ETNProcBiome Biome, TArray<EPathProp>& Out)
		{
			using PP = EPathProp;
			Out.Reset();
			switch (Biome)
			{
				case ETNProcBiome::Jungle:   Out = { PP::Totem, PP::RuinColumn, PP::GiantMushrooms, PP::PotteryJars, PP::CrateStack }; break;
				case ETNProcBiome::Beach:    Out = { PP::Sandcastle, PP::Rowboat, PP::BeachSet, PP::CrateStack, PP::BarrelGroup, PP::CrabTraps }; break;
				case ETNProcBiome::Desert:   Out = { PP::SkullRock, PP::PotteryJars, PP::BarrelGroup, PP::CrateStack, PP::CrystalSpikes, PP::Cairn }; break;
				case ETNProcBiome::Volcanic: Out = { PP::CrystalSpikes, PP::SkullRock, PP::Cairn }; break;
				case ETNProcBiome::Rocky:    Out = { PP::Cairn, PP::MineCart, PP::CrateStack, PP::BarrelGroup, PP::CrystalSpikes }; break;
				case ETNProcBiome::Human:    Out = { PP::CrateStack, PP::BarrelGroup, PP::Barricade, PP::HayBales, PP::MarketStall, PP::ConeLine }; break;
				default: break;
			}
		}

		/** Semiancho de la huella (a lo ancho del camino), alto y largo de un obstáculo de objetos. */
		inline void PathPropSize(EPathProp Kind, FRng& Rng, double& R, double& H, double& Len)
		{
			Len = 0.0;
			switch (Kind)
			{
				case EPathProp::CrateStack:     R = Rng.Range(110.0, 170.0); H = Rng.Range(100.0, 230.0); break;
				case EPathProp::BarrelGroup:    R = Rng.Range(100.0, 150.0); H = 105.0; break;
				case EPathProp::Barricade:      Len = Rng.Range(260.0, 400.0); R = Len * 0.5; H = 125.0; break;
				case EPathProp::HayBales:       R = Rng.Range(120.0, 180.0); H = Rng.Range(90.0, 140.0); break;
				case EPathProp::Sandcastle:     R = Rng.Range(110.0, 170.0); H = Rng.Range(100.0, 160.0); break;
				case EPathProp::Rowboat:        Len = Rng.Range(380.0, 460.0); R = 85.0; H = 80.0; break;
				case EPathProp::BeachSet:       R = Rng.Range(160.0, 200.0); H = 260.0; break;
				case EPathProp::Totem:          R = Rng.Range(60.0, 80.0); H = Rng.Range(280.0, 420.0); break;
				case EPathProp::RuinColumn:     R = Rng.Range(140.0, 220.0); H = Rng.Range(160.0, 380.0); break;
				case EPathProp::GiantMushrooms: R = Rng.Range(140.0, 220.0); H = Rng.Range(160.0, 300.0); break;
				case EPathProp::SkullRock:      R = Rng.Range(130.0, 200.0); H = Rng.Range(120.0, 180.0); break;
				case EPathProp::PotteryJars:    R = Rng.Range(90.0, 130.0); H = Rng.Range(80.0, 120.0); break;
				case EPathProp::CrystalSpikes:  R = Rng.Range(110.0, 170.0); H = Rng.Range(160.0, 300.0); break;
				case EPathProp::Cairn:          R = Rng.Range(70.0, 110.0); H = Rng.Range(160.0, 260.0); break;
				case EPathProp::MineCart:       Len = 280.0; R = 90.0; H = 150.0; break;
				case EPathProp::CrabTraps:      R = Rng.Range(100.0, 140.0); H = Rng.Range(90.0, 130.0); break;
				case EPathProp::MarketStall:    R = Rng.Range(180.0, 220.0); H = 270.0; break;
				case EPathProp::ConeLine:       Len = Rng.Range(300.0, 450.0); R = Len * 0.5; H = 70.0; break;
				default:                        R = 120.0; H = 120.0; break;
			}
		}
	}

	/**
	 * Vida y obstáculos del camino (principal y ramas, salvo carriles): peñascos en grupos de
	 * 1-3 que siempre dejan un carril libre de al menos 3,5 m, agujas y mogotes de roca en las
	 * explanadas, troncos caídos que se saltan (<= 1,1 m) en selva, manglar y volcán, y obstáculos
	 * de objetos de cada bioma (pilas de cajas, barriles, vallas, pacas, castillos de arena, barcas,
	 * tótems, columnas en ruinas, setas gigantes, calaveras, vasijas, cristales, hitos, vagonetas,
	 * nasas, puestos de mercado y filas de conos), también con carril libre. Nunca
	 * junto a géiseres, toboganes, torres, puertas, cuevas, portales, horquillas ni sobre el agua;
	 * de un hueco basta con 12 m (aterrizar y coger carrerilla), y lejos de las formaciones del
	 * camino (arcos y piezas de explanada, que se ponen antes). Uno cada 15-36 m (antes 22-55 m) y,
	 * donde no cabe, se prueba en la muestra siguiente: el camino va muy poblado de saltos.
	 * Además, secuoyas con raíces zancudas en los módulos de manglar, de tamaños muy variados
	 * (muchas medianas y pocas gigantes).
	 */
	inline void BuildObstacles(FLayout& L, FRng Rng)
	{
		using namespace FeatureDetail;
		const uint32 Avoid = (PathFlags::Special | PathFlags::Lane) & ~PathFlags::Gap;
		// Formaciones del camino (ya puestas): centro y radio libre alrededor (arcos: su fondo o sus pies; piezas: su huella).
		TArray<FVector> Forms;
		for (const FFeature& F : L.Features)
		{
			if (F.Type != EFeature::Formation) { continue; }
			const EFormation Kind = static_cast<EFormation>(F.Aux);
			if (IsLandmarkFormation(Kind)) { continue; }
			Forms.Add(FVector(F.Location.X, F.Location.Y, IsArchFormation(Kind) ? FMath::Max(F.Length * 0.5, F.Radius) + 400.0 : F.Radius + 700.0));
		}
		auto NearForm = [&Forms](const FVector2D& At)
		{
			for (const FVector& Fm : Forms)
			{
				if (FVector2D::DistSquared(At, FVector2D(Fm.X, Fm.Y)) < FMath::Square(Fm.Z + 300.0)) { return true; }
			}
			return false;
		};
		auto OnPolyline = [&](const TArray<FPathSample>& S, int32 BranchIndex)
		{
			if (S.Num() < 20) { return; }
			double NextS = Rng.Range(1500.0, 3500.0);
			for (int32 i = 8; i < S.Num() - 8; ++i)
			{
				const FPathSample& Sm = S[i];
				if (Sm.S < NextS) { continue; }
				if (IsWetBiome(Sm.Biome) || AnyFlag(S, i - 6, i + 6, Avoid) || AnyFlag(S, i - 3, i + 3, PathFlags::Gap)) { continue; }
				// Cerca de una horquilla o unión de rama tampoco (dos cauces se cruzan ahí).
				bool bFork = false;
				for (const FBranch& B : L.Branches)
				{
					if (BranchIndex == INDEX_NONE && (FMath::Abs(B.ForkSample - i) < 10 || FMath::Abs(B.RejoinSample - i) < 10)) { bFork = true; break; }
				}
				if (bFork || (BranchIndex != INDEX_NONE && (i < 14 || i > S.Num() - 14)) || NearForm(Sm.P)) { continue; }
				NextS = Sm.S + Rng.Range(1500.0, 3600.0);

				const double W = Sm.Width;
				const FVector2D N = LeftNormal(Sm.Dir);
				const bool bForest = Sm.Biome == ETNProcBiome::Jungle || Sm.Biome == ETNProcBiome::Mangrove || Sm.Biome == ETNProcBiome::Volcanic;
				// En la playa, troncos a la deriva atravesados.
				const bool bDrift = Sm.Biome == ETNProcBiome::Beach;
				const double U = Rng.Unit();
				if (W >= 2000.0 && U < 0.3)
				{
					// Aguja (alta y fina) o mogote (bajo y ancho) en mitad de la explanada, con carriles a ambos lados.
					const bool bSpire = Rng.Chance(0.6);
					const double R = bSpire ? Rng.Range(150.0, 320.0) : Rng.Range(380.0, 700.0);
					const double Room = W * 0.5 - R - (L.Params.bDrivable ? 800.0 : 500.0);
					if (Room < 0.0) { continue; }
					FFeature F = MakeAtSample(EFeature::RockSpire, Sm, i, BranchIndex);
					F.Location = FVector(Sm.P + N * Rng.Range(-Room, Room) * 0.6, Sm.Z);
					F.Radius = R;
					F.Height = bSpire ? Rng.Range(500.0, 1400.0) : Rng.Range(220.0, 600.0);
					F.Aux = static_cast<int32>(Rng.RangeInt(0, 1 << 20));
					L.Features.Add(F);
					// El siguiente, pasada la roca.
					NextS = FMath::Max(NextS, Sm.S + R + 1200.0);
					continue;
				}
				// Rally: solo las agujas y mogotes de las explanadas (con carriles a los lados); troncos, torres, obstáculos de
				// objetos y peñascos pararían en seco al buggy en mitad del camino.
				if (L.Params.bDrivable)
				{
					continue;
				}
				if ((bForest || bDrift) && W >= 500.0 && W <= 2600.0 && U < (bForest ? 0.5 : 0.28))
				{
					// Tronco caído atravesado: se salta (radio 35-55 cm); deja hueco en un extremo o no.
					FFeature F = MakeAtSample(EFeature::Log, Sm, i, BranchIndex);
					const double Ang = FMath::DegreesToRadians(Rng.Range(60.0, 90.0)) * (Rng.Chance(0.5) ? 1.0 : -1.0);
					F.Dir = (Sm.Dir * FMath::Cos(Ang) + N * FMath::Sin(Ang)).GetSafeNormal();
					F.Length = W * Rng.Range(0.55, 0.9) / FMath::Max(0.3, FMath::Abs(FMath::Sin(Ang)));
					F.Radius = Rng.Range(35.0, 55.0);
					F.Location = FVector(Sm.P + N * Rng.Range(-0.15, 0.15) * W, Sm.Z);
					F.Aux = static_cast<int32>(Rng.RangeInt(0, 1 << 20));
					L.Features.Add(F);
					// El siguiente, pasado el tronco (en diagonal ocupa más a lo largo del camino).
					NextS = FMath::Max(NextS, Sm.S + 0.5 * F.Length * FMath::Abs(FMath::Cos(Ang)) + 1200.0);
					continue;
				}
				// Torre de escalada del bioma pegada a un borde (carril libre de sobra): escalones de 1 m, la
				// recompensa arriba y la medusa al pie para subir de un bote.
				if (W >= 1300.0 && U < 0.78 && Rng.Chance(0.24))
				{
					const double Side = Rng.Chance(0.5) ? 1.0 : -1.0;
					FFeature T = MakeAtSample(EFeature::ClimbTower, Sm, i, BranchIndex);
					T.Location = FVector(Sm.P + N * (Side * (W * 0.5 - PlazaDims::TowerHalf - 80.0)), Sm.Z);
					T.Dir = Sm.Dir;
					T.Radius = PlazaDims::TowerHalf;
					T.Height = Rng.Chance(0.5) ? 300.0 : 400.0;
					T.Aux2 = static_cast<int32>(Rng.RangeInt(0, 1 << 20));
					L.Features.Add(T);
					FFeature Bonus = MakeAtSample(EFeature::BonusPickup, Sm, i, BranchIndex);
					Bonus.Location = T.Location + FVector(0.0, 0.0, T.Height + 70.0);
					L.Features.Add(Bonus);
					FFeature Jelly = MakeAtSample(EFeature::Bouncer, Sm, i, BranchIndex);
					Jelly.Location = T.Location + FVector(T.Dir * (PlazaDims::TowerHalf + 210.0), 0.0);
					L.Features.Add(Jelly);
					continue;
				}
				// Obstáculo de objetos del bioma (pila de cajas, castillo de arena, tótem...) a un lado, siempre
				// con carril libre; las vallas y filas de conos, atravesadas desde un borde.
				if (U < 0.78)
				{
					TArray<EPathProp> Kinds;
					PathPropsFor(Sm.Biome, Kinds);
					if (Kinds.Num() > 0)
					{
						const EPathProp Kind = Kinds[Rng.RangeInt(0, Kinds.Num() - 1)];
						double R = 0.0, H = 0.0, Len = 0.0;
						PathPropSize(Kind, Rng, R, H, Len);
						const bool bAcross = Kind == EPathProp::Barricade || Kind == EPathProp::ConeLine;
						const double PropLane = FMath::Max(350.0, 0.4 * W);
						const double PropSide = Rng.Chance(0.5) ? 1.0 : -1.0;
						const double MinOff = R + PropLane - W * 0.5;
						const double MaxOff = W * 0.5 - (bAcross ? R : R * 0.5);
						if ((Kind != EPathProp::MarketStall || W >= 1400.0) && MaxOff > FMath::Max(0.0, MinOff))
						{
							FFeature F = MakeAtSample(EFeature::PathProp, Sm, i, BranchIndex);
							F.Location = FVector(Sm.P + N * (PropSide * Rng.Range(FMath::Max(0.0, MinOff), MaxOff)), Sm.Z);
							F.Dir = bAcross ? N * PropSide : Sm.Dir;
							F.Radius = R;
							F.Height = H;
							F.Length = Len;
							F.Aux = static_cast<int32>(Kind);
							F.Aux2 = static_cast<int32>(Rng.RangeInt(0, 1 << 20));
							L.Features.Add(F);
							continue;
						}
					}
				}
				// Grupo de 1-3 peñascos a un lado, dejando libre el otro (>= 3,5 m y >= 40 % del ancho).
				const double Lane = FMath::Max(350.0, 0.4 * W);
				const double Side = Rng.Chance(0.5) ? 1.0 : -1.0;
				const int32 Count = Rng.RangeInt(1, 3);
				for (int32 k = 0; k < Count; ++k)
				{
					const double R = Rng.Range(70.0, 220.0);
					const double MinOff = R + Lane - W * 0.5;
					const double MaxOff = W * 0.5 - R * 0.4;
					if (MaxOff <= FMath::Max(0.0, MinOff)) { break; }
					FFeature F = MakeAtSample(EFeature::Boulder, Sm, i, BranchIndex);
					const double Along = Rng.Range(-250.0, 250.0) * k;
					F.Location = FVector(Sm.P + Sm.Dir * Along + N * (Side * Rng.Range(FMath::Max(0.0, MinOff), MaxOff)), Sm.Z);
					F.Radius = R;
					F.Height = R * Rng.Range(0.8, 1.7);
					F.Aux = static_cast<int32>(Rng.RangeInt(0, 1 << 20));
					L.Features.Add(F);
				}
			}
		};
		OnPolyline(L.Main, INDEX_NONE);
		for (int32 b = 0; b < L.Branches.Num(); ++b)
		{
			if (L.Branches[b].Kind != EBranchKind::Lane) { OnPolyline(L.Branches[b].Samples, b); }
		}

		// Secuoyas del manglar: fuera de los cauces (en tierra o en las pozas), con raíces zancudas.
		TArray<FPathSample> All = L.Main;
		for (const FBranch& B : L.Branches) { All.Append(B.Samples); }
		PathDetail::FSampleGrid Grid;
		Grid.Build(All, L.MaxExtent());
		for (const FModule& M : L.Modules)
		{
			if (M.Biome != ETNProcBiome::Mangrove) { continue; }
			const int32 Count = Rng.RangeInt(16, 26);
			for (int32 n = 0; n < Count; ++n)
			{
				for (int32 Try = 0; Try < 20; ++Try)
				{
					const int32 X = Rng.RangeInt(0, L.RasterW - 1);
					const int32 Y = Rng.RangeInt(0, L.RasterH - 1);
					if (L.ModuleOfCell[L.CellIndex(X, Y)] != M.Id) { continue; }
					const FVector2D C = L.CellCenter(X, Y) + FVector2D(Rng.Range(-150.0, 150.0), Rng.Range(-150.0, 150.0));
					const double R = LerpD(90.0, 320.0, FMath::Pow(Rng.Unit(), 1.6));
					double D = 0.0;
					const int32 Near = Grid.Nearest(C, 20000.0, D);
					if (Near != INDEX_NONE && D < All[Near].Width * 0.5 + R * 3.0 + 900.0) { continue; }
					bool bClose = false;
					for (const FFeature& F : L.Features)
					{
						const double Dist = FVector2D::Distance(FVector2D(F.Location.X, F.Location.Y), C);
						if (F.Type == EFeature::GiantTree && Dist < (F.Radius + R) * 5.0 + 600.0) { bClose = true; break; }
						// Las formaciones ya están puestas: ni encima de un palafito ni junto a los pies de un arco.
						if (F.Type == EFeature::Formation && Dist < F.Radius + R * 3.0 + 1000.0) { bClose = true; break; }
					}
					if (bClose) { continue; }
					FFeature T;
					T.Type = EFeature::GiantTree;
					T.Biome = ETNProcBiome::Mangrove;
					T.Location = FVector(C, 0.0);
					T.Radius = R;
					T.Height = R * Rng.Range(17.0, 22.0);
					T.Aux = static_cast<int32>(Rng.RangeInt(0, 1 << 20));
					L.Features.Add(T);
					break;
				}
			}
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Planificación de peligros (enemigos, spawners) — determinista y pura
	// ─────────────────────────────────────────────────────────────────────────

	enum class EHazardPlacement : uint8
	{
		OnPath,
		PathEdge,
		NearPath,
		InWater,
		AbovePath,
		OffPathFar
	};

	struct FHazardRule
	{
		int32 Id = INDEX_NONE;
		double PerKm = 1.0;
		EHazardPlacement Placement = EHazardPlacement::OnPath;
		/** Bit b = bioma b permitido. */
		uint32 BiomeMask = 0xFFFFFFFFu;
		double MinDifficulty01 = 0.0;
		/** Distancia mínima entre dos del mismo tipo. */
		double Clearance = 3000.0;
		bool bMainOnly = false;
	};

	struct FHazardSpawn
	{
		int32 RuleId = INDEX_NONE;
		FVector2D P = FVector2D::ZeroVector;
		/** Cota de referencia (suelo del camino); el actor hace su propio trazado. */
		double RefZ = 0.0;
		double Yaw = 0.0;
		int32 PathIndex = INDEX_NONE;
		int32 BranchIndex = INDEX_NONE;
	};

	inline TArray<FHazardSpawn> PlanHazards(const FLayout& L, const TArray<FHazardRule>& Rules, double DensityMul, uint64 Salt)
	{
		TArray<FHazardSpawn> Out;
		FRng Rng = FRng(static_cast<uint64>(L.Params.Seed) * 0x9E37ull + Salt);

		// Zonas reservadas (x, y, radio): estructuras del recorrido y obstáculos del camino.
		TArray<FVector> Keep;
		for (const FFeature& F : L.Features)
		{
			if (F.Type == EFeature::Geyser || F.Type == EFeature::EggNest || F.Type == EFeature::Gap || F.Type == EFeature::ThrowWall
				|| F.Type == EFeature::SabotageGate || F.Type == EFeature::SabotageSwitch || F.Type == EFeature::StartArea || F.Type == EFeature::Finish)
			{
				Keep.Add(FVector(F.Location.X, F.Location.Y, 1600.0));
			}
			else if (F.Type == EFeature::Boulder || F.Type == EFeature::RockSpire)
			{
				Keep.Add(FVector(F.Location.X, F.Location.Y, F.Radius + 250.0));
			}
			else if (F.Type == EFeature::PathProp)
			{
				Keep.Add(FVector(F.Location.X, F.Location.Y, FMath::Max(F.Radius, F.Length * 0.5) + 250.0));
			}
			else if (F.Type == EFeature::Log)
			{
				Keep.Add(FVector(F.Location.X, F.Location.Y, F.Length * 0.5 + 200.0));
			}
		}

		auto Visit = [&](const TArray<FPathSample>& Samples, int32 BranchIndex)
		{
			for (const FHazardRule& R : Rules)
			{
				if (R.bMainOnly && BranchIndex != INDEX_NONE) { continue; }
				if (L.Params.Difficulty01 + 1e-6 < R.MinDifficulty01) { continue; }
				const double Chance = R.PerKm * DensityMul * L.Params.SampleSpacing / 100000.0;
				TArray<FVector2D> Placed;
				for (int32 i = 4; i < Samples.Num() - 4; ++i)
				{
					const FPathSample& Sm = Samples[i];
					if ((R.BiomeMask & (1u << BiomeIndex(Sm.Biome))) == 0) { continue; }
					const bool bWet = (Sm.Flags & (PathFlags::Islet | PathFlags::Boardwalk)) != 0;
					if (R.Placement == EHazardPlacement::InWater)
					{
						if (!bWet) { continue; }
					}
					else if ((Sm.Flags & (PathFlags::Special & ~PathFlags::Portal)) != 0 && !(bWet && R.Placement != EHazardPlacement::OnPath))
					{
						continue;
					}
					if (!Rng.Chance(Chance)) { continue; }

					const FVector2D N = LeftNormal(Sm.Dir);
					const double Side = Rng.Chance(0.5) ? 1.0 : -1.0;
					FVector2D Pos = Sm.P;
					switch (R.Placement)
					{
						// Fuera del cauce todo son taludes y paredes: lo "cercano" y lo "lejano" va dentro del cauce,
						// hacia el borde; lo del agua, en la poza junto al camino.
						case EHazardPlacement::OnPath:     Pos += N * (Rng.Range(-0.35, 0.35) * Sm.Width); break;
						case EHazardPlacement::PathEdge:   Pos += N * Side * FMath::Max(0.0, Sm.Width * 0.5 - 150.0); break;
						case EHazardPlacement::NearPath:   Pos += N * Side * (Sm.Width * Rng.Range(0.2, 0.42)); break;
						case EHazardPlacement::InWater:    Pos += N * Side * (Sm.Width * 0.5 + Rng.Range(200.0, 600.0)); break;
						case EHazardPlacement::AbovePath:  break;
						case EHazardPlacement::OffPathFar: Pos += N * Side * (Sm.Width * Rng.Range(0.3, 0.45)); break;
					}

					bool bOk = true;
					for (const FVector2D& Pl : Placed) { if (FVector2D::DistSquared(Pl, Pos) < R.Clearance * R.Clearance) { bOk = false; break; } }
					for (const FVector& Kp : Keep) { if (bOk && FVector2D::DistSquared(FVector2D(Kp.X, Kp.Y), Pos) < Kp.Z * Kp.Z) { bOk = false; } }
					if (!bOk) { continue; }

					FHazardSpawn H;
					H.RuleId = R.Id;
					H.P = Pos;
					H.RefZ = Sm.Z;
					H.Yaw = FMath::RadiansToDegrees(AngleOf(Sm.Dir));
					H.PathIndex = i;
					H.BranchIndex = BranchIndex;
					Out.Add(H);
					Placed.Add(Pos);
				}
			}
		};

		Visit(L.Main, INDEX_NONE);
		for (int32 b = 0; b < L.Branches.Num(); ++b) { Visit(L.Branches[b].Samples, b); }
		return Out;
	}
}
